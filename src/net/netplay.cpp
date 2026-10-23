// dbz3 - Online P2P: sesion UDP, emparejamiento (mods + partida guardada) y lockstep.
// Ver netplay.h. Protocolo (little-endian, cabecera de 6 bytes "DBZ3" + tipo + version):
//   HELLO / HELLO_ACK  build[32] mods_hash[40] delay save_size save_hash[40] save_rel[96]
//   MODREQ / MODLIST   lista "mod|hash" troceada
//   SAVE_REQ idx  ->  SAVE_DATA idx size bytes[<=1024]
//   START / START_ACK
//   INPUT  n x {tick, pad[12]} + {rng_tick, rng} + {stamp u32, eco u32, espera u16, retardo u8 (+0x80 AUTO)}
//   BYE
//   TEAM n ids u16 / ROOM elo u16 tramo u8 reglas u8 reversion u16 (cada 0,5 s antes de jugar)
#include "netplay.h"
#include "room.h"
#include "../tagteam.h"

#include "../launcher/i18n.h"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <stdexcept>
#include <thread>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hash.h>
#include <rex/logging.h>

#include "../launcher/mod_pipeline.h"
#include "../launcher/settings.h"
#include "../guest_region.h"
#include "../eu_map.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// Existing Tag Team control bits; no resource or swap state is touched here.
namespace dbz3::tag { extern std::atomic<uint16_t> g_net_local; }
namespace dbz3::net {
namespace {

namespace fs = std::filesystem;
bool HexDigest(const std::string& s, size_t width = 40) {
  return s.size() == width && std::all_of(s.begin(), s.end(), [](char x) {
    return (x >= '0' && x <= '9') || (x >= 'a' && x <= 'f');
  });
}
uint32_t NewNonce() {
  std::random_device rd;
  uint32_t n = (uint32_t(rd()) ^ uint32_t(rd() << 1));
  return n ? n : 1;
}
namespace T = dbz3::i18n;
// PRUEBAS (DBZ3_NET_NOFAIL): una diferencia del RNG solo se registra (los menus de dos copias no van al mismo ritmo)
bool NoFail() { static const bool v = std::getenv("DBZ3_NET_NOFAIL") != nullptr; return v; }

constexpr uint8_t kProto = 4;   // 4: INPUT con ping y retardo (adaptativo); 3: identidad, reto y nonces
constexpr size_t kInputBase = 34;   // INPUT sin mandos: cabecera 6 + nonces 8 + n 1 + rng 8 + ping/retardo 11
constexpr char kMagic[4] = {'D', 'B', 'Z', '3'};
enum Msg : uint8_t {
  kHello = 1, kHelloAck, kModReq, kModList, kSaveReq, kSaveData, kStart, kStartAck, kInput, kBye,
  kTeam, kChallenge, kReject,  // equipo de Tag Team de quien lo manda (n, n ids u16); al final: los PCs viejos lo ignoran
  kRoom,                       // sala: rango (elo u16, tramo u8: 0-4, 255 = sin combates) y reglas de combate
                               // (u8: bit0 v0, bit1 definitiva directa, bits2-3 cancelacion lb/rb; u16 reversion)
  kSum,                        // R1: n u8 x {tick u32, 5 x u32 de bloques} (hasta 8; cada tick viaja en varios paquetes)
  kSnap, kSnapAck,             // R4: instantanea del anfitrion en trozos (tick u32, total u32, idx u16, cnt u16, n u16, bytes) y su acuse (tick u32)
  kVanillaOffer                // ---- VANILLA (V): "jugad sin mods"; sin cuerpo, se repite mientras uno espera (UDP)
};
constexpr uint32_t kChunk = 1024;
constexpr uint64_t kLostMs = 10000;
// MODLIST: first u16, total u16, n u8 y n entradas "carpeta|hash" de 256 bytes (con 128 un nombre largo se
// cortaba y salia como "version distinta"). 5 por paquete: 6+8+5+5*256 = 1299 B cabe en un paquete de
// Ethernet (1472 B de UDP) sin fragmentar; con 7 (1811 B) un router que tire fragmentos dejaba la lista colgada
constexpr size_t kModsPerPacket = 5, kModEntry = 256, kMaxMods = 512;
std::vector<uint8_t> StartPacket(uint32_t seed, const std::vector<uint8_t>& snap);

uint64_t NowMs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Writer {
  std::vector<uint8_t> b;
  explicit Writer(uint8_t type) {
    b.insert(b.end(), kMagic, kMagic + 4);
    b.push_back(type);
    b.push_back(kProto);
  }
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
  void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
  void str(const std::string& s, size_t n) {
    for (size_t i = 0; i < n; ++i) b.push_back(i < s.size() ? uint8_t(s[i]) : 0);
  }
  void bytes(const uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); }
  void pad(const Pad& p) {
    u16(p.buttons); u8(p.lt); u8(p.rt);
    u16(uint16_t(p.lx)); u16(uint16_t(p.ly)); u16(uint16_t(p.rx)); u16(uint16_t(p.ry));
  }
};

struct Reader {
  const std::vector<uint8_t>& b;
  size_t o = 6;
  bool ok = true;
  explicit Reader(const std::vector<uint8_t>& v) : b(v) {}
  bool need(size_t n) { ok = ok && o <= b.size() && n <= b.size() - o; return ok; }
  uint8_t u8() { return need(1) ? b[o++] : 0; }
  uint16_t u16() { if (!need(2)) return 0; uint16_t v = uint16_t(b[o] | b[o + 1] << 8); o += 2; return v; }
  uint32_t u32() {
    if (!need(4)) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(b[o + i]) << (8 * i);
    o += 4;
    return v;
  }
  std::string str(size_t n) {
    if (!need(n)) return {};
    std::string s(reinterpret_cast<const char*>(&b[o]), n);
    o += n;
    return s.substr(0, s.find('\0'));
  }
  Pad pad() {
    Pad p;
    p.buttons = u16(); p.lt = u8(); p.rt = u8();
    p.lx = int16_t(u16()); p.ly = int16_t(u16()); p.rx = int16_t(u16()); p.ry = int16_t(u16());
    return p;
  }
};

// ---- huella de los mods: lo que cambia la partida (personajes, modelos, movesets, AFS).
// Fuera solo los packs de texturas (solo aspecto). _roster y las vistas previas SI cuentan: dos
// kits distintos pueden montar plazas distintas con los mismos mods (mejor rechazar que desincronizar).
std::string FileHash(const fs::path& p) {
  const auto hash = rex::hash_file(p);
  if (!HexDigest(hash, 32)) throw std::runtime_error("no se pudo verificar contenido");
  return hash;
}

std::vector<std::string> ModFingerprints() {
  std::vector<std::string> out;
  if (std::getenv("DBZ3_VANILLA_SESSION")) return out;   // ---- VANILLA (V): sesion sin mods = contenido vacio (un rival con mods no coincide)
  const fs::path root = launcher::ModPipeline::ModsDir();
  std::error_code ec;
  std::vector<fs::path> mods;
  for (auto [it, iec] = std::pair{fs::directory_iterator(root, ec), std::error_code{}};
       !iec && it != fs::directory_iterator(); it.increment(iec)) {
    if (!it->is_directory(ec)) continue;
    const std::string name = rex::path_to_utf8(it->path().filename());
    // ".respaldo", ".install_*": carpetas internas del launcher, no mods
    if (name.empty() || name[0] == '.' || name.ends_with(".disabled") || fs::exists(it->path() / ".disabled", ec)) continue;
    // _forma_unica: lo genera el juego en cada PC (nombres en el idioma de cada uno, src/transformaciones.cpp) y
    // la capsula unica no actua en online: contarlo rechazaba a dos jugadores con distinto idioma
    if (name == "_forma_unica") continue;
    mods.push_back(it->path());
  }
  std::sort(mods.begin(), mods.end());
  for (const auto& m : mods) {
    std::vector<std::pair<std::string, fs::path>> files;
    for (auto [it, iec] = std::pair{fs::recursive_directory_iterator(m, ec), std::error_code{}};
         !iec && it != fs::recursive_directory_iterator(); it.increment(iec)) {
      if (!it->is_regular_file(ec)) continue;
      // Texturas de pack (<hash XXH3>.png/.dds): solo aspecto, cada jugador puede llevar el suyo.
      // Ademas un pack HD son miles de PNG (GB): hashearlos bloqueaba el emparejamiento.
      std::string ext = rex::path_to_utf8(it->path().extension());
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if ((ext == ".png" || ext == ".dds") &&
          settings::IsTexturePackFileName(rex::path_to_utf8(it->path().stem())))
        continue;
      const std::string rel = rex::path_to_utf8(fs::relative(it->path(), m, ec));
      // nombre/version/autor del mod: el launcher deja editarlo y el juego no lo lee
      if (rel == "manifest.txt") continue;
      // Include previews: conservative false rejection is preferable to false equality.
      files.emplace_back(rel, it->path());
    }
    if (files.empty()) continue;   // mod solo de texturas: no cuenta para el online
    std::sort(files.begin(), files.end());
    std::string acc;
    for (const auto& [rel, p] : files) acc += rel + "|" + FileHash(p) + "\n";
    out.push_back(rex::path_to_utf8(m.filename()) + "|" + rex::hash_bytes(acc));
  }
  return out;
}

// ---- partida guardada (data.bin del perfil de la consola emulada)
fs::path SavePath() {
  const fs::path root = settings::UserDataRoot();
  std::error_code ec;
  for (auto [it, iec] = std::pair{fs::recursive_directory_iterator(root, ec), std::error_code{}};
       !iec && it != fs::recursive_directory_iterator(); it.increment(iec)) {
    const auto& p = it->path();
    if (p.filename() == "data.bin" && p.parent_path().filename() == "DBZ3" &&
        rex::path_to_utf8(p).find("_online") == std::string::npos) {
      return p;
    }
  }
  return {};
}

fs::path BackupDir() { return settings::UserDataRoot() / "_online_respaldo"; }

std::vector<uint8_t> ReadFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

}  // namespace

Session& Session::Get() {
  static Session s;
  return s;
}

// F4 v3: complete structural and phase validation BEFORE peer claim or mutation.
// Directional nonce pair rejects delayed packets from previous sessions.
bool Session::ConfigureIdentity(const std::string& digest, bool safe_selection, bool safe_tagteam) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (link_ != Link::kIdle || guest_owner_) return false;
  identity_ready_ = safe_selection && HexDigest(digest, 32);   // XXH3-128 (rex::hash_bytes)
  identity_admission_ = identity_ready_;
  safe_tagteam_ = identity_ready_ && safe_tagteam;
  identity_ = identity_ready_ ? digest : "";
  return identity_ready_;
}

bool Session::ValidPacket(const std::vector<uint8_t>& pkt) const {
  if (pkt.size() < 6 || pkt.size() > 2048 ||
      std::memcmp(pkt.data(), kMagic, 4) || pkt[5] != kProto) return false;
  const auto state = link_.load();
  if (state == Link::kIdle || state == Link::kLost || state == Link::kError) return false;
  const auto type = pkt[4];
  Reader r(pkt);
  if (type == kHello || type == kHelloAck) {
    if (state != Link::kWaiting && state != Link::kConnected) return false;
    if ((type == kHello) != (role_ == Role::kHost)) return false;
    auto build = r.str(40), mods = r.str(40);
    const auto delay = r.u8();
    const auto save = r.u32();
    const auto region = r.u8();
    const auto nonce = r.u32(), echo = r.u32();
    return r.ok && r.o == pkt.size() && identity_ready_ && build == identity_ &&
           HexDigest(mods, 32) && delay >= 1 && delay <= 10 && !save && region <= 1 &&   // 0 US, 1 EU (distinta: Handle avisa)
           nonce && (!peer_nonce_ || peer_nonce_ == nonce) &&
           echo == local_nonce_;
  }
  if (!peer_nonce_ || r.u32() != peer_nonce_ || r.u32() != local_nonce_) return false;
  switch (type) {
    // F4 uses memory UI only. No save transfer. Mod lists: bounded, only before playing.
    case kSaveReq: case kSaveData: return false;
    case kModReq:
      return (state == Link::kWaiting || state == Link::kConnected) && r.ok && r.o == pkt.size();
    case kModList: {
      if (state != Link::kWaiting && state != Link::kConnected) return false;
      const size_t first = r.u16(), total = r.u16(), n = r.u8();
      return r.ok && total <= kMaxMods && n <= kModsPerPacket && first + n <= total &&
             r.o + n * kModEntry == pkt.size();
    }
    case kTeam: {
      if (state != Link::kConnected && state != Link::kStarting) return false;
      const auto n = r.u8();
      if (n > 3) return false;
      if (n >= 2 && !safe_tagteam_) return false;
      std::vector<int> ids;
      for (int i = 0; i < n; ++i) { const auto id = r.u16(); if (id >= 64) return false; ids.push_back(id); }
      if (team_ready_ && ids != peer_team_) return false;
      return r.ok && r.o == pkt.size();
    }
    case kRoom: {
      if (state != Link::kConnected && state != Link::kStarting) return false;
      r.u16();
      const auto tramo = r.u8(), reglas = r.u8();
      r.u16();
      return r.ok && r.o == pkt.size() && (tramo <= 4 || tramo == 255) && !(reglas & 0xE0) && (reglas & 0x0C) != 0x0C;
    }
    case kStart: {
      if (role_ != Role::kGuest || !mods_match_ || !team_ready_ || !peer_profile_ ||
          (state != Link::kConnected && state != Link::kStarting)) return false;
      const auto seed = r.u32();
      const auto n = r.u16();
      if (!seed || n != kUiSnapSize || !r.need(n) || r.o + n != pkt.size()) return false;
      if (state == Link::kStarting && (seed != seed_ ||
          !std::equal(pkt.begin() + r.o, pkt.end(), ui_snap_.begin(), ui_snap_.end()))) return false;
      return true;
    }
    case kStartAck:
      return role_ == Role::kHost && state == Link::kStarting && r.o == pkt.size();
    case kInput: {
      if (state != Link::kPlaying && !(role_ == Role::kHost && state == Link::kStarting)) return false;
      const auto n = r.u8();
      if (!n || n > 8 || pkt.size() != kInputBase + 16u * n) return false;
      const uint32_t now = tick_.load();
      uint32_t prev = 0;
      for (int i = 0; i < n; ++i) {
        const auto t = r.u32();
        const auto pad = r.pad();
        // small window prevents poisoning another modulo slot; no rollover session.
        if (t == 0xFFFFFFFFu || (i && t <= prev) ||
            uint64_t(t) + 8 < now || uint64_t(t) > uint64_t(now) + 32) return false;
        const auto& slot = theirs_[t % kRing];
        if (slot.tick == t && (slot.pad.buttons != pad.buttons || slot.pad.lt != pad.lt ||
            slot.pad.rt != pad.rt || slot.pad.lx != pad.lx || slot.pad.ly != pad.ly ||
            slot.pad.rx != pad.rx || slot.pad.ry != pad.ry)) return false;
        prev = t;
      }
      const auto rt = r.u32(); r.u32();
      r.u32(); r.u32(); r.u16();   // marca, eco y espera: cualquier valor (solo miden el ping)
      const auto d = r.u8() & 0x7F;
      return r.ok && r.o == pkt.size() && d >= 1 && d <= Session::kMaxDelay && (rt == 0xFFFFFFFFu ||
          (rt % 60 == 0 && uint64_t(rt) <= uint64_t(now) + 32 && uint64_t(rt) + 240 >= now));
    }
    case kSum: {
      if (state != Link::kPlaying) return false;
      const auto n = r.u8();
      if (!n || n > 8 || pkt.size() != 6 + 8 + 1 + 24u * n) return false;
      return r.ok;
    }
    case kSnap: {
      if (state != Link::kPlaying || !rb_on_ || role_ != Role::kGuest) return false;
      r.u32();
      const auto total = r.u32();
      const auto idx = r.u16(), cnt = r.u16(), n = r.u16();
      return r.ok && total > 0 && total <= 262144 && cnt > 0 && idx < cnt && n > 0 && n <= 1100 && r.o + n == pkt.size();
    }
    case kSnapAck:
      if (state != Link::kPlaying || !rb_on_ || role_ != Role::kHost) return false;
      r.u32();
      return r.ok && r.o == pkt.size();
    case kBye: return r.ok && r.o == pkt.size();
    case kVanillaOffer:   // ---- VANILLA (V)
      return (state == Link::kConnected) && r.ok && r.o == pkt.size();
    default: return false;
  }
}

// ---------------------------------------------------------------- socket
bool Session::OpenSocket(uint16_t port, std::string* error) {
#ifdef _WIN32
  static bool wsa = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
  if (!wsa) { if (error) *error = "WSAStartup"; return false; }
#endif
  const auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
  if (s == INVALID_SOCKET) { if (error) *error = "socket"; return false; }
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
#else
  if (s < 0) { if (error) *error = "socket"; return false; }
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port);
  if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    if (error) *error = T::T("el puerto ", "port ") + std::to_string(port) + T::T(" esta ocupado", " is in use");
    return false;
  }
  sock_ = intptr_t(s);
  return true;
}

void Session::Send(const std::vector<uint8_t>& pkt) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (sock_ < 0 || !have_peer_) return;
  auto wire = pkt;
  if (pkt.size() >= 6 && pkt[4] != kHello && pkt[4] != kHelloAck) {
    Writer ids(0); ids.u32(local_nonce_); ids.u32(peer_nonce_);
    wire.insert(wire.begin() + 6, ids.b.begin() + 6, ids.b.end());
  }
  ::sendto(decltype(::socket(0, 0, 0))(sock_), reinterpret_cast<const char*>(wire.data()), int(wire.size()), 0,
           reinterpret_cast<const sockaddr*>(peer_addr_), peer_len_);
  last_send_ms_ = NowMs();
}

bool Session::Recv(std::vector<uint8_t>& pkt) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (sock_ < 0) return false;
  uint8_t buf[2048];
  sockaddr_in from{};
  socklen_t len = sizeof(from);
  const int n = int(::recvfrom(decltype(::socket(0, 0, 0))(sock_), reinterpret_cast<char*>(buf), sizeof(buf), 0,
                               reinterpret_cast<sockaddr*>(&from), &len));
  if (n < 0) return false;                       // nada pendiente
  pkt.clear();
  if (role_ == Role::kHost && IsStun(buf, n)) {   // respuesta STUN: nuestra IP publica y puerto
    uint32_t ip = 0;
    uint16_t port = 0;
    std::lock_guard<std::mutex> lock(mu_);
    if (stun_ready_ && std::memcmp(stun_addr_, &from, sizeof(stun_addr_)) == 0 &&
        StunMapped(buf, n, &ip, &port) && pub_how_ != "router") {
      pub_ip_ = ip;
      pub_port_ = port;
      pub_how_ = "stun";
    }
    return true;
  }
  if (n < 6 || std::memcmp(buf, kMagic, 4) != 0) return true;
  if (buf[5] != kProto) {
    if (have_peer_ && role_ == Role::kGuest &&
        std::memcmp(peer_addr_, &from, sizeof(from)) == 0)
      Fail(Link::kError, T::T("el otro PC tiene otra version del juego", "the other PC has another game version"));
    return true;
  }
  std::vector<uint8_t> candidate(buf, buf + n);
  if (role_ == Role::kGuest && link_ == Link::kWaiting && have_peer_ &&
      std::memcmp(peer_addr_, &from, sizeof(from)) == 0) {
    Reader reply(candidate);
    if (buf[4] == kChallenge && n == 14 && reply.u32() == local_nonce_) {
      const auto challenge = reply.u32();
      if (challenge && (!peer_nonce_ || peer_nonce_ == challenge)) {
        peer_nonce_ = challenge;
        SendHello(kHello);
      }
      return true;
    }
    if (buf[4] == kReject && n == 10 && reply.u32() == local_nonce_) {
      Fail(Link::kError, T::T("el otro PC tiene otra version del juego", "the other PC has another game version"));
      return true;
    }
  }
  // Stateless admission challenge: no endpoint claim on initial HELLO.
  // Random echo prevents stale HELLO capture, not user authentication.
  if (!have_peer_ && role_ == Role::kHost && link_ == Link::kWaiting &&
      buf[4] == kHello && n == 100) {
    Reader hello(candidate);
    const auto identity = hello.str(40), mods = hello.str(40);
    const auto delay = hello.u8(); const auto save = hello.u32(); const auto region = hello.u8();
    const auto nonce = hello.u32(), echo = hello.u32();
    if (!hello.ok || !nonce || delay < 1 || delay > 10 || save || region > 1 || !HexDigest(mods, 32)) return true;   // region 0 US, 1 EU
    if (identity != identity_) {
      Writer reject(kReject); reject.u32(nonce);
      ::sendto(decltype(::socket(0, 0, 0))(sock_), reinterpret_cast<const char*>(reject.b.data()),
               int(reject.b.size()), 0, reinterpret_cast<const sockaddr*>(&from), len);
      return true;
    }
    if (echo != local_nonce_) {
      Writer challenge(kChallenge); challenge.u32(nonce); challenge.u32(local_nonce_);
      ::sendto(decltype(::socket(0, 0, 0))(sock_), reinterpret_cast<const char*>(challenge.b.data()),
               int(challenge.b.size()), 0, reinterpret_cast<const sockaddr*>(&from), len);
      return true;
    }
  }
  if (!ValidPacket(candidate)) return true;
  if (!have_peer_ && role_ == Role::kHost && buf[4] == kHello) {
    std::memcpy(peer_addr_, &from, sizeof(from));
    peer_len_ = int(len);
    have_peer_ = true;
  }
  if (!have_peer_ || std::memcmp(peer_addr_, &from, sizeof(from)) != 0) return true;
  pkt = std::move(candidate);
  last_recv_ms_ = NowMs();
  return true;
}

// ---------------------------------------------------------------- emparejamiento
bool Session::Host(uint16_t port, uint8_t delay, std::string* error) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (guest_owner_) {
    if (error) *error = T::T("la partida online anterior aun se esta cerrando", "the previous online match is still closing");
    return false;
  }
  Close();
  if (!identity_ready_ || !identity_admission_) {
    if (error) *error = T::T("Online: no se pudo verificar este juego",
                             "Online: this game could not be verified");
    return false;
  }
  identity_admission_ = false;
  local_nonce_ = NewNonce();
  local_team_ = dbz3::tag::MyLastTeam();
  if (local_team_.size() >= 2 && !safe_tagteam_) {
    // Tag Team online sin autorizar: se juega 1 contra 1. Antes el ultimo equipo del Tag Team de 1P (que no
    // se borra) dejaba CREAR y UNIRSE fallando hasta reiniciar el juego (2026-10-15)
    REXLOG_INFO("dbz3 online: el equipo de Tag Team ({} luchadores) no viaja al online: 1 contra 1", local_team_.size());
    local_team_.clear();
  }
  if (local_team_.size() > 3 || std::any_of(local_team_.begin(), local_team_.end(), [](int x) { return x < 0 || x >= 64; })) {
    if (error) *error = T::T("Online: equipo de Tag Team no valido", "Online: invalid Tag Team team");
    return false;
  }
  role_ = Role::kHost;
  auto_delay_ = delay == 0;
  auto_view_ = auto_delay_;
  delay_ = auto_delay_ ? kAutoStart : uint8_t(std::clamp<int>(delay, 1, kMaxDelay));
  if (!OpenSocket(port, error)) { role_ = Role::kNone; return false; }
  link_ = Link::kWaiting;
  last_recv_ms_ = NowMs();
  StartModScan();
  static const bool room_ok = RoomSelfTest();   // codigo de sala: ida y vuelta
  if (!room_ok) REXLOG_ERROR("dbz3 online: el codigo de sala no pasa su prueba");
  // Internet: el router abre el puerto (UPnP) y, si no puede, un servidor STUN dice la IP publica
  // (Poll). En un hilo: tarda segundos y no debe parar el juego. Solo da el codigo de sala; la
  // admision (identidad, nonces, mods) no cambia. Lo de una sesion ya cerrada se descarta (generation_).
  if (std::getenv("DBZ3_NET_LOCAL")) {   // PRUEBA: solo red local (las pruebas cierran el juego a la fuerza)
    REXLOG_INFO("dbz3 online: PRUEBA: sin UPnP ni STUN (DBZ3_NET_LOCAL)");
    return true;
  }
  std::string local = LocalAddresses();
  local = local.substr(0, local.find(' '));
  const uint64_t gen = generation_;
  std::thread([this, port, local, gen] {
    sockaddr_in stun{};
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    const bool resolved = getaddrinfo("stun.l.google.com", "19302", &hints, &res) == 0 && res;
    if (resolved) {
      std::memcpy(&stun, res->ai_addr, sizeof(stun));
      freeaddrinfo(res);
    }
    {
      std::lock_guard<std::recursive_mutex> io(io_mu_);
      std::lock_guard<std::mutex> lock(mu_);
      if (gen != generation_) return;
      if (resolved) {
        std::memcpy(stun_addr_, &stun, sizeof(stun));
        stun_ready_ = true;
      }
    }
    const std::string ext = UpnpOpen(port, local);
    uint32_t ip = 0;
    if (ext.empty()) return;
    if (inet_pton(AF_INET, ext.c_str(), &ip) != 1) {   // IP publica ilegible: el puerto abierto se cierra (cuenta de room.cpp)
      UpnpClose(port);
      return;
    }
    UpnpCloseAtExit(port);
    std::lock_guard<std::recursive_mutex> io(io_mu_);
    std::lock_guard<std::mutex> lock(mu_);
    if (gen != generation_) {   // la sesion ya se cerro: el puerto se vuelve a cerrar
      std::thread(UpnpClose, port).detach();
      return;
    }
    pub_ip_ = ip;
    pub_port_ = upnp_port_ = port;
    pub_how_ = "router";
    REXLOG_INFO("dbz3 online: el router abrio el puerto {} (IP publica {})", port, ext);
  }).detach();
  return true;
}

bool Session::Join(const std::string& ip, uint16_t port, std::string* error) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (guest_owner_) {
    if (error) *error = T::T("la partida online anterior aun se esta cerrando", "the previous online match is still closing");
    return false;
  }
  Close();
  if (!identity_ready_ || !identity_admission_) {
    if (error) *error = T::T("Online: no se pudo verificar este juego",
                             "Online: this game could not be verified");
    return false;
  }
  identity_admission_ = false;
  local_nonce_ = NewNonce();
  local_team_ = dbz3::tag::MyLastTeam();
  if (local_team_.size() >= 2 && !safe_tagteam_) {
    // Tag Team online sin autorizar: se juega 1 contra 1. Antes el ultimo equipo del Tag Team de 1P (que no
    // se borra) dejaba CREAR y UNIRSE fallando hasta reiniciar el juego (2026-10-15)
    REXLOG_INFO("dbz3 online: el equipo de Tag Team ({} luchadores) no viaja al online: 1 contra 1", local_team_.size());
    local_team_.clear();
  }
  if (local_team_.size() > 3 || std::any_of(local_team_.begin(), local_team_.end(), [](int x) { return x < 0 || x >= 64; })) {
    if (error) *error = T::T("Online: equipo de Tag Team no valido", "Online: invalid Tag Team team");
    return false;
  }
  role_ = Role::kGuest;
  auto_delay_ = false;   // el retardo lo marca el anfitrion
  auto_view_ = false;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  uint32_t code_ip = 0;
  uint16_t code_port = 0;
  if (DecodeRoom(ip, &code_ip, &code_port)) {          // codigo de sala: IP publica y puerto dentro
    std::memcpy(&a.sin_addr, &code_ip, 4);
    a.sin_port = htons(code_port);
  } else if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) {
    if (error) *error = T::T("Codigo o IP no valido: ", "Invalid code or IP: ") + ip;
    role_ = Role::kNone;
    return false;
  }
  if (!OpenSocket(0, error)) { role_ = Role::kNone; return false; }
  std::memcpy(peer_addr_, &a, sizeof(a));
  peer_len_ = sizeof(a);
  have_peer_ = true;
  link_ = Link::kWaiting;
  last_recv_ms_ = NowMs();
  StartModScan();
  return true;
}

// La huella de los mods son segundos de disco: ni la interfaz (Poll) ni io_mu_ esperan por ella. El hilo solo
// toma io_mu_ para dejar el resultado; SendHello calla hasta que esta (el invitado repite el HELLO cada 300 ms)
// y una sesion ya cerrada (generation_) descarta lo suyo.
// ponytail: un escaneo por sesion, sin cancelar; si Crear/Salir se repite rapido los viejos acaban solos.
void Session::StartModScan() {
  const uint64_t gen = generation_;
  std::thread([this, gen] {
    const auto t0 = NowMs();
    std::vector<std::string> mods;
    std::string hash;
    bool ok = true;
    try {
      mods = ModFingerprints();
      std::string acc;
      for (const auto& m : mods) acc += m + "\n";
      hash = rex::hash_bytes(acc);
    } catch (const std::exception&) {
      ok = false;
    }
    std::lock_guard<std::recursive_mutex> io(io_mu_);
    if (gen != generation_) return;
    if (!ok) {
      Fail(Link::kError, T::T("no se pudieron comprobar los mods de este PC", "could not check this PC's mods"));
      return;
    }
    my_mods_ = std::move(mods);
    my_mods_hash_ = hash;
    my_build_ = identity_;
    // UI snapshot only: never read/write/download the personal save.
    save_size_ = 0;
    save_ready_ = true;
    if (link_ == Link::kWaiting) last_recv_ms_ = NowMs();   // los 30 s del invitado cuentan desde su primer HELLO
    REXLOG_INFO("dbz3 online: huella de {} mods lista en {} ms", my_mods_.size(), NowMs() - t0);
  }).detach();
}

void Session::SendHello(uint8_t type) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (my_build_.empty()) return;   // StartModScan aun calcula la huella de los mods propios
  Writer w(type);
  w.str(my_build_, 40);
  w.str(my_mods_hash_, 40);
  w.u8(delay_);
  w.u32(save_size_);
  w.u8(dbz3::g_guest_eu ? 1 : 0);
  w.u32(local_nonce_);
  w.u32(peer_nonce_);
  Send(w.b);
}

void Session::Handle(const std::vector<uint8_t>& pkt, bool) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (!ValidPacket(pkt)) return;
  Reader r(pkt);
  const uint8_t type = pkt[4];
  if (type != kHello && type != kHelloAck) r.o += 8;
  switch (type) {
    case kHello:
    case kHelloAck: {
      const std::string build = r.str(40), mods = r.str(40);
      const uint8_t delay = r.u8();
      const uint32_t save_size = r.u32();
      const uint8_t region = r.u8();   // 1 = EU
      const uint32_t nonce = r.u32(), echo = r.u32();
      (void)echo;
      if (!r.ok) return;
      if (peer_nonce_ && peer_nonce_ != nonce) return;
      peer_nonce_ = nonce;
      if (my_build_.empty()) return;   // aun sin huella de mods propia: el otro repite el HELLO
      if (type == kHello && role_ == Role::kHost) SendHello(kHelloAck);
      if (build != my_build_) {
        Fail(Link::kError, T::T("el otro PC tiene otra version del juego", "the other PC has another game version"));
        return;
      }
      if (region != uint8_t(dbz3::g_guest_eu ? 1 : 0)) {   // US y EU no son el mismo juego (semilla, datos)
        Fail(Link::kError, T::T("el otro PC tiene otra region del juego (US/EU)",
                                "the other PC has another game region (US/EU)"));
        return;
      }
      if (role_ == Role::kGuest) {
        delay_ = delay;
        if (save_size > (16u << 20)) {   // una partida guardada no pasa de unos KB: paquete roto o hostil
          Fail(Link::kError, T::T("datos del otro PC no validos", "invalid data from the other PC"));
          return;
        }
        if (save_size_ != save_size || save_have_.empty()) {
          save_size_ = save_size;
          save_.assign(save_size, 0);
          save_have_.assign((save_size + kChunk - 1) / kChunk, 0);
          save_ready_ = save_size == 0;
        }
      }
      // Mods distintos: se queda conectado SIN poder empezar (RequestStart, kStart y TakeStart exigen
      // mods_match_) y se cruzan las listas para decir cuales faltan y ofrecer bajarlos de Nova's Mods.
      const bool was = mods_match_.exchange(mods == my_mods_hash_);
      if (!mods_match_ && (was || link_ == Link::kWaiting))
        REXLOG_INFO("dbz3 online: mods distintos; se cruzan las listas");
      if (link_ == Link::kWaiting) link_ = Link::kConnected;
      break;
    }
    case kModReq: {   // lista troceada (kModsPerPacket por paquete)
      if (my_mods_.size() > kMaxMods) break;
      for (size_t i = 0; i < my_mods_.size() || i == 0; i += kModsPerPacket) {
        Writer w(kModList);
        w.u16(uint16_t(i));
        w.u16(uint16_t(my_mods_.size()));
        const size_t n = std::min(kModsPerPacket, my_mods_.size() - std::min(i, my_mods_.size()));
        w.u8(uint8_t(n));
        for (size_t k = 0; k < n; ++k) w.str(my_mods_[i + k], kModEntry);
        Send(w.b);
        if (my_mods_.empty()) break;
      }
      break;
    }
    case kModList: {
      const uint16_t first = r.u16(), total = r.u16();
      const uint8_t n = r.u8();
      std::lock_guard<std::mutex> lock(mu_);
      if (!peer_mods_known_ || peer_mods_.size() != total) peer_mods_.assign(total, {});
      peer_mods_known_ = true;
      for (uint8_t k = 0; k < n; ++k) peer_mods_[first + k] = r.str(kModEntry);
      break;
    }
    case kSaveReq: {
      const uint32_t idx = r.u32();
      if (!r.ok || role_ != Role::kHost || size_t(idx) * kChunk >= save_.size()) return;
      const uint32_t n = std::min<uint32_t>(kChunk, uint32_t(save_.size() - size_t(idx) * kChunk));
      Writer w(kSaveData);
      w.u32(idx);
      w.u32(n);
      w.bytes(save_.data() + size_t(idx) * kChunk, n);
      Send(w.b);
      break;
    }
    case kSaveData: {
      const uint32_t idx = r.u32(), n = r.u32();
      if (!r.need(n) || role_ != Role::kGuest || idx >= save_have_.size() ||
          size_t(idx) * kChunk + n > save_.size()) return;
      std::memcpy(save_.data() + size_t(idx) * kChunk, &pkt[r.o], n);
      save_have_[idx] = 1;
      save_ready_ = std::all_of(save_have_.begin(), save_have_.end(), [](uint8_t v) { return v != 0; });
      break;
    }
    case kStart:   // semilla + estado de menu del anfitrion
      if (role_ == Role::kGuest && link_ != Link::kPlaying) {
        const uint32_t seed = r.u32();
        const uint16_t n = r.u16();
        if (!r.need(n)) return;
        Send(Writer(kStartAck).b);
        if (link_ != Link::kStarting) {
          seed_ = seed;
          std::lock_guard<std::mutex> lock(mu_);
          ui_snap_.assign(pkt.begin() + long(r.o), pkt.begin() + long(r.o + n));
          link_ = Link::kStarting;
          launch_ = true;
        }
      }
      break;
    case kStartAck:
      if (role_ == Role::kHost && link_ == Link::kStarting) launch_ = true;
      break;
    case kInput: {
      const uint8_t n = r.u8();
      uint32_t newest = 0;
      for (uint8_t k = 0; k < n; ++k) {
        const uint32_t t = r.u32();
        const Pad p = r.pad();
        if (!r.ok) return;
        Slot& s = theirs_[t % kRing];
        if (s.tick == 0xFFFFFFFFu || t > s.tick) s = {t, p};
        newest = t;   // en orden creciente (ValidPacket)
      }
      const uint32_t rt = r.u32(), rv = r.u32();
      const uint32_t stamp = r.u32(), echo = r.u32();
      const uint16_t held = r.u16();
      const uint8_t dly = r.u8();
      if (!r.ok) return;
      const uint64_t now = NowMs();
      if (newest >= (dly & 0x7F) && int32_t(newest - (dly & 0x7F) - peer_est_.load()) > 0) { peer_est_ = newest - (dly & 0x7F); peer_est_ms_ = now; }
      // su marca mas nueva (los paquetes pueden llegar desordenados) se le devuelve como eco
      if (!peer_stamp_ || int32_t(stamp - peer_stamp_) > 0) { peer_stamp_ = stamp; peer_stamp_at_ = now; }
      // ping = ahora - mi marca que vuelve - lo que el otro la tuvo guardada antes de contestar
      if (echo) {
        const uint32_t rtt = uint32_t(now) - echo - held;
        if (rtt < 5000) batch_rtt_ = std::min(batch_rtt_, rtt);   // cuenta el mas fresco de la tanda (FlushRtt)
      }
      // invitado: el retardo lo marca el anfitrion (solo de paquetes mas nuevos que el ultimo aplicado)
      if (role_ == Role::kGuest && link_ == Link::kPlaying && newest > peer_delay_tick_) {
        peer_delay_tick_ = newest;
        auto_view_ = (dly & 0x80) != 0;
        if ((dly & 0x7F) != delay_) SetDelay(dly & 0x7F, "lo marca el anfitrion");
      }
      if (r.ok && rt != 0xFFFFFFFFu) {
        const int i = int((rt / 60) % kRing);
        if (their_rng_tick_[i] != rt && my_rng_tick_[i] == rt && my_rng_[i] == rv) ++rng_ok_;   // una vez por punto
        their_rng_tick_[i] = rt;
        their_rng_[i] = rv;
        if (my_rng_tick_[i] == rt && my_rng_[i] != rv && NoFail()) {
          static int n = 0;
          if (n++ < 10) REXLOG_WARN("dbz3 online: (NOFAIL) RNG distinto en el tick {} ({:08X} / {:08X})", rt, my_rng_[i], rv);
        } else if (my_rng_tick_[i] == rt && my_rng_[i] != rv && rb_mode_.load()) {
          static int nr = 0;   // con rollback no se corta la partida: las sumas por tick detectan y la reparacion corrige
          if (nr++ < 10) REXLOG_WARN("dbz3 online: RNG distinto en el tick {} ({:08X} / {:08X}); lo trata la reparacion", rt, my_rng_[i], rv);
        } else if (my_rng_tick_[i] == rt && my_rng_[i] != rv && !desync_.exchange(true)) {
          REXLOG_WARN("dbz3 online: DESINCRONIA en el tick {} (rng {:08X} / {:08X})", rt, my_rng_[i], rv);
          Fail(Link::kError, T::T("DESINCRONIA: los dos PCs ya no veian lo mismo", "DESYNC: both PCs no longer saw the same thing"));
        }
      }
      if (role_ == Role::kHost && link_ == Link::kStarting) launch_ = true;   // el invitado ya juega
      break;
    }
    case kTeam: {   // su equipo de Tag Team (se aplica al empezar, TakeStart)
      const uint8_t n = r.u8();
      std::vector<int> ids;
      for (uint8_t i = 0; i < n && i < 3 && r.need(2); ++i) {
        const int id = int(r.u16());
        if (id < 64) ids.push_back(id);   // IDs del juego 0-43 y extra 44-63: otro valor se saldria de la tabla
      }
      std::lock_guard<std::mutex> lock(mu_);
      if (team_ready_ && peer_team_ != ids) return; // frozen agreement
      peer_team_ = ids;
      team_ready_ = true;
      break;
    }
    case kRoom: {
      const uint16_t elo = r.u16();
      const uint8_t tramo = r.u8(), reglas = r.u8();
      const uint16_t reversion = r.u16();
      std::lock_guard<std::mutex> lock(mu_);
      peer_elo_ = elo;
      peer_tramo_ = tramo == 255 ? -1 : tramo;
      peer_rules_ = {reglas, reversion};
      peer_profile_ = true;
      break;
    }
    case kSum: {
      const uint8_t n = r.u8();
      for (uint8_t k = 0; k < n; ++k) {
        SumSlot v;
        v.tick = r.u32();
        for (auto& x : v.b) x = r.u32();
        if (!r.ok) return;
        SumSlot& s = their_sum_[v.tick % kRing];
        if (s.tick == v.tick) continue;   // ya comparada o repetida
        s = v;
        CompareSum(v.tick);
      }
      break;
    }
    case kSnap: {   // invitado: trozos de la instantanea del anfitrion
      const uint32_t tick = r.u32(), total = r.u32();
      const uint16_t idx = r.u16(), cnt = r.u16(), n = r.u16();
      if (!r.need(n)) return;
      if (snap_rx_tick_ != tick || snap_rx_.size() != total || snap_rx_cnt_ != cnt) {   // instantanea nueva
        snap_rx_tick_ = tick; snap_rx_.assign(total, 0); snap_rx_have_.assign(cnt, 0); snap_rx_cnt_ = cnt; snap_rx_got_ = 0; snap_rx_ready_ = false;
      }
      const size_t off = size_t(idx) * 1000;
      if (off + n > snap_rx_.size()) return;
      if (!snap_rx_have_[idx]) { std::memcpy(snap_rx_.data() + off, &pkt[r.o], n); snap_rx_have_[idx] = 1; ++snap_rx_got_; }
      if (snap_rx_got_ == snap_rx_cnt_) {
        if (!snap_rx_ready_) snap_rx_ready_ = true;
        Writer w(kSnapAck);   // se repite en cada trozo que sigue llegando (el acuse pudo perderse)
        w.u32(tick);
        Send(w.b);
      }
      break;
    }
    case kSnapAck:
      if (snap_tx_on_ && r.u32() == snap_tx_tick_) {
        snap_tx_on_ = false;
        REXLOG_INFO("dbz3 online: rollback: instantanea del tick {} entregada ({} ms)", snap_tx_tick_, NowMs() - snap_tx_start_ms_);
      }
      break;
    case kBye:
      Fail(Link::kLost, T::T("el otro jugador se ha ido", "the other player left"));
      break;
    case kVanillaOffer:   // ---- VANILLA (V)
      if (!peer_vanilla_.exchange(true)) REXLOG_INFO("dbz3 online: el otro PC propone jugar vanilla (sin mods)");
      break;
    default:
      break;
  }
}

// ---- VANILLA (V): oferta de partida sin mods. Los dos tienen que aceptar; solo entonces cada PC se reinicia en modo
// vanilla (mods.cpp RelaunchVanilla) y se reconecta. Aqui solo el acuerdo: nada de ficheros del jugador.
void Session::OfferVanilla() {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (link_ != Link::kConnected) return;
  my_vanilla_ = true;
  Send(Writer(kVanillaOffer).b);
}


void Session::Poll() {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (guest_owner_ || link_ == Link::kIdle || link_ == Link::kPlaying || link_ == Link::kError || link_ == Link::kLost) return;
  std::vector<uint8_t> pkt;
  for (int i = 0; i < 256 && Recv(pkt); ++i) {
    if (!pkt.empty()) Handle(pkt, true);
    pkt.clear();
  }
  const uint64_t now = NowMs();
  if (role_ == Role::kGuest && link_ == Link::kWaiting && now - last_send_ms_ > 300) SendHello(kHello);
  // anfitrion sin UPnP: pregunta su IP publica al STUN desde el mismo socket (asi el puerto que ve
  // el STUN es el que vera el rival)
  if (role_ == Role::kHost && link_ == Link::kWaiting && stun_tries_ < 5 && now - stun_sent_ms_ > 1500) {
    sockaddr_in stun{};
    bool ask = false;
    {
      std::lock_guard<std::mutex> lock(mu_);
      ask = stun_ready_ && !pub_ip_;
      if (ask) std::memcpy(&stun, stun_addr_, sizeof(stun));
    }
    if (ask) {
      const auto req = StunRequest();
      ::sendto(decltype(::socket(0, 0, 0))(sock_), reinterpret_cast<const char*>(req.data()), int(req.size()), 0,
               reinterpret_cast<const sockaddr*>(&stun), sizeof(stun));
      stun_sent_ms_ = now;
      ++stun_tries_;
    }
  }
  if (link_ == Link::kConnected && my_vanilla_ && now - vanilla_sent_ms_ > 300) {   // ---- VANILLA (V)
    vanilla_sent_ms_ = now;
    Send(Writer(kVanillaOffer).b);
  }
  if (link_ == Link::kConnected || link_ == Link::kStarting) {
    if (role_ == Role::kGuest && !save_ready_ && now - last_send_ms_ > 30) {
      int asked = 0;   // pide hasta 8 trozos que falten
      for (uint32_t i = 0; i < save_have_.size() && asked < 8; ++i) {
        if (!save_have_[i]) { Writer w(kSaveReq); w.u32(i); Send(w.b); ++asked; }
      }
    }
    if (role_ == Role::kHost && link_ == Link::kStarting && !launch_ && now - last_send_ms_ > 200) {
      std::lock_guard<std::mutex> lock(mu_);
      Send(StartPacket(seed_, ui_snap_));
    }
    if (link_ == Link::kConnected && !mods_match_ && now - mod_req_ms_ > 1000) {
      std::vector<ModDelta> diff;
      if (!ModDiff(diff)) {   // su lista aun no esta entera: se pide otra vez (UDP)
        mod_req_ms_ = now;
        Send(Writer(kModReq).b);
      }
    }
    if (now - team_sent_ms_ > 500) {   // mi equipo de Tag Team, cada medio segundo (UDP)
      team_sent_ms_ = now;
      const std::vector<int>& team = local_team_;
      Writer w(kTeam);
      w.u8(uint8_t(std::min<size_t>(team.size(), 3)));
      for (size_t i = 0; i < team.size() && i < 3; ++i) w.u16(uint16_t(team[i]));
      Send(w.b);
      Writer pf(kRoom);   // y mi rango y mis reglas de combate para la sala (tambien cada medio segundo)
      {
        std::lock_guard<std::mutex> lock(mu_);
        pf.u16(uint16_t(std::clamp(my_elo_, 0, 65535)));
        pf.u8(my_tramo_ < 0 ? 255 : uint8_t(std::min(my_tramo_, 4)));
        pf.u8(my_rules_.flags);
        pf.u16(my_rules_.reversion);
      }
      Send(pf.b);
    }
    if (now - last_recv_ms_ > kLostMs) Fail(Link::kLost, T::T("sin respuesta del otro PC", "no reply from the other PC"));
  }
  if (link_ == Link::kWaiting && role_ == Role::kGuest && now - last_recv_ms_ > 30000) {
    Fail(Link::kError, T::T("no contesta: revisa el codigo, el puerto y el cortafuegos del anfitrion (en la misma casa, usa su IP local)",
                            "no answer: check the code, the port and the host's firewall (in the same house, use their local IP)"));
  }
}

void Session::SetUiSnapshot(const uint8_t* p, size_t n) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  std::lock_guard<std::mutex> lock(mu_);
  if (link_ != Link::kStarting && p && n == kUiSnapSize) ui_snap_.assign(p, p + n);
}

namespace {
std::vector<uint8_t> StartPacket(uint32_t seed, const std::vector<uint8_t>& snap) {
  Writer w(kStart);
  w.u32(seed);
  w.u16(uint16_t(snap.size()));
  w.bytes(snap.data(), snap.size());
  return w.b;
}
}  // namespace

void Session::RequestStart() {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (role_ != Role::kHost || link_ != Link::kConnected || !mods_match_ || !team_ready_ || ui_snap_.size() != kUiSnapSize) return;
  {   // sin la sala del otro (sus reglas) no se empieza: los dos tienen que decidir lo mismo en TakeStart
    std::lock_guard<std::mutex> lock(mu_);
    if (!peer_profile_) return;
  }
  link_ = Link::kStarting;
  start_sent_ = true;
  seed_ = uint32_t(NowMs() * 2654435761u) | 1u;
  if (const char* e = std::getenv("DBZ3_NET_SEED")) seed_ = uint32_t(std::strtoul(e, nullptr, 0)) | 1u;   // PRUEBAS: corridas comparables
  std::lock_guard<std::mutex> lock(mu_);
  Send(StartPacket(seed_, ui_snap_));
}

void Session::ReseedBattle(uint8_t* base) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (!active() || !base) return;
  const uint32_t v = (seed_ ^ (++battles_ * 0x9E3779B9u)) | 1u;   // nunca 0 (el LFSR se quedaria)
  const uint8_t be[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
  std::memcpy(base + dbz3::GA(0x82310670u), be, 4);
  REXLOG_INFO("dbz3 online: combate {}: semilla del RNG igualada", battles_);
}

uint32_t Session::AzarCompartido(uint32_t sal) {
  if (!active()) {   // sin partida online: generador local (no importa que los dos PCs coincidan)
    static std::mt19937 rng{uint32_t(NowMs()) * 2654435761u ^ 0x9E3779B9u};
    return rng() ^ (sal * 0x85EBCA6Bu);
  }
  // Mezcla determinista de la semilla compartida, el numero de combate y `sal`: igual en los dos PCs.
  uint32_t x = seed_ ^ (battles_ * 0x9E3779B9u) ^ (sal * 0x85EBCA6Bu);
  x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
  return x;
}

void Session::BattleBarrier() {
  if (!active()) return;
  // bit propio en el mando (0x0400 anfitrion, 0x0800 invitado; XInput no los usa): los dos PCs ven
  // los dos bits en el mismo tick aplicado y salen a la vez
  const uint16_t mine = role_ == Role::kHost ? 0x0400 : 0x0800;
  const uint32_t from = tick_;
  const auto deadline = NowMs() + 60000;
  for (int i = 0; i < 60 * 60 && active() && NowMs() < deadline; ++i) {   // bounded both by ticks and monotonic time
    Pad p{};
    p.buttons = mine;
    RunTick(p, 0);
    if ((applied_[0].buttons & 0x0400) && (applied_[1].buttons & 0x0800)) {
      REXLOG_INFO("dbz3 online: combate alineado en el tick {} ({} ticks de espera)", tick_.load(), tick_.load() - from);
      return;
    }
  }
  if (active()) Fail(Link::kLost, T::T("el otro PC no llego al combate", "the other PC did not reach the fight"));
  REXLOG_WARN("dbz3 online: el otro PC no llego al combate");
}

bool Session::TakeStart(uint8_t* base) {
  // Lo llama el hilo del juego en cada vuelta: sin arranque pendiente sale sin tocar io_mu_ (si la interfaz lo
  // tenia, p. ej. con el DNS de RoomCode o el hash de los mods del HELLO, el juego esperaba: tirones en los menus)
  if (!launch_.load() || link_.load() != Link::kStarting) return false;
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (link_ != Link::kStarting || !mods_match_ || !team_ready_ || !peer_profile_ ||
      ui_snap_.size() != kUiSnapSize || !base || !launch_.exchange(false)) return false;
  guest_owner_ = true; // explicit handoff under io_mu_; Poll now excluded
  {
    std::lock_guard<std::mutex> lock(mu_);
    // invitado: desbloqueos, ultima eleccion... del anfitrion (misma rueda, mismos cursores)
    if (role_ == Role::kGuest && base && !ui_snap_.empty()) {
      std::memcpy(base + dbz3::GuestAddr(kUiSnap, kUiSnapEu), ui_snap_.data(),
                  std::min<size_t>(ui_snap_.size(), kUiSnapSize));
    }
  }
  if (base) {   // misma semilla del RNG del juego (LFSR, 0x82310670 en US y EU)
    const uint8_t be[4] = {uint8_t(seed_ >> 24), uint8_t(seed_ >> 16), uint8_t(seed_ >> 8), uint8_t(seed_)};
    std::memcpy(base + dbz3::GA(0x82310670u), be, 4);
  }
  {   // Tag Team: lado 1 el anfitrion, lado 2 el invitado, igual en los dos PCs (sin COM)
    std::vector<int> mine = local_team_, peer;
    { std::lock_guard<std::mutex> lock(mu_); peer = peer_team_; }
    dbz3::tag::SetDynamicTeam(0, role_ == Role::kHost ? mine : peer, false);   // el invitado no hace suyo el del anfitrion
    dbz3::tag::SetDynamicTeam(1, role_ == Role::kHost ? peer : mine, false);
    dbz3::tag::SetComSide(0, false);
    dbz3::tag::SetComSide(1, false);
    REXLOG_INFO("dbz3 online: equipos de Tag Team {} y {} luchadores", mine.size(), peer.size());
  }
  tick_ = 0;
  battles_ = 0;
  for (auto& s : mine_) s.tick = 0xFFFFFFFFu;
  for (auto& s : theirs_) s.tick = 0xFFFFFFFFu;
  my_rng_tick_.fill(0xFFFFFFFFu);
  my_sum_.fill(SumSlot{});
  their_sum_.fill(SumSlot{});
  sum_cmp_ = sum_bad_ = 0;
  their_rng_tick_.fill(0xFFFFFFFFu);
  {   // reglas opcionales del combate: activas solo si las dos fotos son iguales (los dos PCs deciden lo mismo)
    std::lock_guard<std::mutex> lock(mu_);
    rules_agreed_ = my_rules_ == peer_rules_;
    play_rules_ = my_rules_;
    REXLOG_INFO("dbz3 online: reglas de combate {} (aqui {:02X}/{}, el otro {:02X}/{})",
                rules_agreed_ ? "iguales: activas en la partida" : "distintas: apagadas (combate original)",
                my_rules_.flags, my_rules_.reversion, peer_rules_.flags, peer_rules_.reversion);
  }
  RbApplyW();
  delay0_ = delay_;   // el invitado ya tiene el del anfitrion (HELLO): los dos empiezan igual
  sched_ = delay0_;
  peer_delay_tick_ = 0;
  rtt_n_ = rtt_sum_ = rtt_max_ = stalls_ = 0;
  batch_rtt_ = UINT32_MAX;
  down_votes_ = 0;
  no_down_until_ = 0;
  rng_ok_ = 0;
  ping_ms_ = -1;
  link_ = Link::kPlaying;
  last_recv_ms_ = NowMs();
  REXLOG_INFO("dbz3 online: empieza la partida ({} , retardo {})", role_ == Role::kHost ? "anfitrion" : "invitado",
              delay_.load());
  return true;
}

void Session::Close() {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  if (guest_owner_) { leave_req_ = true; return; }
  ResetTransport();
}

void Session::ResetTransport() {
  if (sock_ >= 0) {
    if (have_peer_) Send(Writer(kBye).b);
#ifdef _WIN32
    closesocket(SOCKET(sock_));
#else
    ::close(int(sock_));
#endif
  }
  sock_ = -1;
  have_peer_ = false;
  link_ = Link::kIdle;
  dbz3::tag::ClearDynamicTeams();   // los equipos de la partida online no pasan a los combates normales
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (upnp_port_) std::thread(UpnpClose, upnp_port_).detach();   // se cierra el puerto del router
    pub_ip_ = 0;
    pub_port_ = upnp_port_ = 0;
    pub_how_.clear();
  }
  stun_sent_ms_ = 0;
  stun_tries_ = 0;
  role_ = Role::kNone;
  mods_match_ = false;
  my_vanilla_ = peer_vanilla_ = false;   // ---- VANILLA (V)
  save_ready_ = false;
  save_.clear();
  save_have_.clear();
  save_size_ = 0;
  start_sent_ = false;
  launch_ = false;
  leave_req_ = false;
  // sin ticks propios todavia: el rng que mande el rival antes de empezar aqui no se compara
  my_rng_tick_.fill(0xFFFFFFFFu);
  my_sum_.fill(SumSlot{});
  their_sum_.fill(SumSlot{});
  sum_cmp_ = sum_bad_ = 0;
  their_rng_tick_.fill(0xFFFFFFFFu);
  desync_ = false;
  my_build_.clear();
  ++generation_;
  guest_owner_ = false;
  local_nonce_ = peer_nonce_ = 0;
  last_send_ms_ = last_recv_ms_ = team_sent_ms_ = 0;
  peer_len_ = 0;
  std::memset(peer_addr_, 0, sizeof(peer_addr_));
  local_team_.clear(); peer_team_.clear(); ui_snap_.clear();
  team_ready_ = false;
  tick_ = 0; battles_ = seed_ = 0; armed_ = false; rb_mode_ = false; rb_on_ = false;
  applied_[0] = {}; applied_[1] = {};
  dbz3::tag::g_net_local = 0;
  dbz3::tag::NetApplied(0, 0); dbz3::tag::NetApplied(1, 0);
  for (auto& s : mine_) s = {};
  for (auto& s : theirs_) s = {};
  sched_ = peer_stamp_ = peer_delay_tick_ = 0;
  peer_stamp_at_ = 0;
  rtt_n_ = rtt_sum_ = rtt_max_ = stalls_ = 0;
  batch_rtt_ = UINT32_MAX;
  down_votes_ = 0;
  no_down_until_ = 0;
  rng_ok_ = 0;
  ping_ms_ = -1;
  mod_req_ms_ = 0;
  std::lock_guard<std::mutex> lock(mu_);
  peer_mods_.clear();
  peer_mods_known_ = false;
  peer_profile_ = false;
  peer_rules_ = {};
  rules_agreed_ = false;
  error_.clear();
}

void Session::SetProfile(int elo, int tramo) {
  const CombatRules now = CombatRules::Now();
  std::lock_guard<std::mutex> lock(mu_);
  my_elo_ = elo;
  my_tramo_ = tramo;
  my_rules_ = now;   // foto de las reglas al crear/unirse: es la que se compara y la que vale en la partida
}

void Session::MyRoom(int* elo, int* tramo, CombatRules* rules) const {
  std::lock_guard<std::mutex> lock(mu_);
  *elo = my_elo_;
  *tramo = my_tramo_;
  *rules = my_rules_;
}

bool Session::PeerRoom(int* elo, int* tramo, CombatRules* rules) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!peer_profile_) return false;
  *elo = peer_elo_;
  *tramo = peer_tramo_;
  *rules = peer_rules_;
  return true;
}

// Por nombre: combate.cpp (dueno de las opciones) no entra en la build solo EU.
CombatRules CombatRules::Now() {
  auto on = [](const char* n) {
    const std::string v = rex::cvar::GetFlagByName(n);
    return v == "true" || v == "1";
  };
  const std::string c = rex::cvar::GetFlagByName("dbz3_combate_boton_cancel");
  CombatRules r;
  r.flags = uint8_t((on("dbz3_combate_reglas_v0") ? 1 : 0) | (on("dbz3_combate_definitiva_directa") ? 2 : 0) |
                    (c == "lb" ? 4 : c == "rb" ? 8 : 0));
  r.reversion = uint16_t(std::clamp(std::atoi(rex::cvar::GetFlagByName("dbz3_combate_reversion").c_str()), 0, 65535));
  if (on("dbz3_net_rollback") || std::getenv("DBZ3_RB_ON")) r.flags |= 0x10;   // R4: pide rollback (se acuerda en TakeStart)
  return r;
}

void CombatRules::Apply() const {
  rex::cvar::SetFlagByName("dbz3_combate_reglas_v0", (flags & 1) ? "true" : "false");
  rex::cvar::SetFlagByName("dbz3_combate_definitiva_directa", (flags & 2) ? "true" : "false");
  rex::cvar::SetFlagByName("dbz3_combate_boton_cancel", (flags & 4) ? "lb" : (flags & 8) ? "rb" : "no");
  rex::cvar::SetFlagByName("dbz3_combate_reversion", std::to_string(reversion));
}

void Session::Fail(Link l, const std::string& why) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  guest_owner_ = false;
  ResetTransport();
  std::lock_guard<std::mutex> lock(mu_);
  error_ = why;
  link_ = l;
}

std::string Session::Status() const {
  std::lock_guard<std::mutex> lock(mu_);
  switch (link_.load()) {
    case Link::kIdle: return "";
    case Link::kWaiting: return role_ == Role::kHost ? T::T("Esperando al rival...", "Waiting for the other player...") : T::T("Llamando al anfitrion...", "Calling the host...");
    case Link::kConnected: return T::T("conectados", "connected");
    case Link::kStarting: return T::T("Empezando...", "Starting...");
    case Link::kPlaying: return T::T("jugando", "playing");
    case Link::kLost: return T::T("conexion perdida: ", "connection lost: ") + error_;
    case Link::kError: return "error: " + error_;
  }
  return "";
}

bool Session::ModDiff(std::vector<ModDelta>& out) const {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  std::lock_guard<std::mutex> lock(mu_);
  out.clear();
  if (!peer_mods_known_ ||
      std::any_of(peer_mods_.begin(), peer_mods_.end(), [](const std::string& m) { return m.empty(); }))
    return false;
  // "carpeta|hash": por carpeta, para distinguir "te falta" de "otra version"
  auto split = [](const std::vector<std::string>& v) {
    std::map<std::string, std::string> m;
    for (const auto& s : v) m[s.substr(0, s.find('|'))] = s;
    return m;
  };
  const auto mine = split(my_mods_), theirs = split(peer_mods_);
  for (const auto& [name, full] : theirs) {
    const auto it = mine.find(name);
    if (it == mine.end()) out.push_back({ModDelta::kMissing, name});
    else if (it->second != full) out.push_back({ModDelta::kDifferent, name});
  }
  for (const auto& [name, full] : mine) {
    if (!theirs.count(name)) out.push_back({ModDelta::kExtra, name});
  }
  return true;
}

std::string Session::RoomCode(std::string* how) const {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (pub_ip_) {
      if (how) *how = pub_how_;
      return EncodeRoom(pub_ip_, pub_port_);
    }
  }
  if (how) *how = "local";
  // La pantalla lo pide en cada fotograma: gethostname + DNS cada vez (y con io_mu_ cogido) daba tirones
  // mientras se espera al rival sin IP publica. La IP local se recuerda 5 s y se busca sin el candado.
  static std::mutex cache_mu;
  static std::string cache;
  static uint64_t cache_ms = 0;
  std::string local;
  {
    std::lock_guard<std::mutex> lock(cache_mu);
    if (!cache.empty() && NowMs() - cache_ms < 5000) local = cache;
  }
  if (local.empty()) {
    local = LocalAddresses();
    local = local.substr(0, local.find(' '));
    std::lock_guard<std::mutex> lock(cache_mu);
    cache = local;
    cache_ms = NowMs();
  }
  std::lock_guard<std::recursive_mutex> io(io_mu_);   // sock_
  uint32_t ip = 0;
  sockaddr_in a{};
  socklen_t len = sizeof(a);
  if (local.empty() || inet_pton(AF_INET, local.c_str(), &ip) != 1 || sock_ < 0 ||
      getsockname(decltype(::socket(0, 0, 0))(sock_), reinterpret_cast<sockaddr*>(&a), &len) != 0)
    return {};
  return EncodeRoom(ip, ntohs(a.sin_port));
}

std::string Session::LocalAddresses() const {
  std::string out;
#ifdef _WIN32
  char host[256] = {};
  if (gethostname(host, sizeof(host)) != 0) return out;
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  if (getaddrinfo(host, nullptr, &hints, &res) != 0) return out;
  for (auto* p = res; p; p = p->ai_next) {
    char ip[64] = {};
    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr, ip, sizeof(ip));
    if (out.find(ip) == std::string::npos) out += (out.empty() ? "" : "  ") + std::string(ip);
  }
  freeaddrinfo(res);
#else
  ifaddrs* ifs = nullptr;
  if (getifaddrs(&ifs) != 0) return out;
  for (auto* p = ifs; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
    char ip[64] = {};
    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr, ip, sizeof(ip));
    if (std::string(ip) != "127.0.0.1") out += (out.empty() ? "" : "  ") + std::string(ip);
  }
  freeifaddrs(ifs);
#endif
  return out;
}

// ---------------------------------------------------------------- partida guardada
void Session::ApplySave() {
  if (save_.empty()) return;
  fs::path dst = SavePath();
  std::error_code ec;
  if (dst.empty()) {
    REXLOG_WARN("dbz3 online: este PC no tiene partida guardada; se juega sin la del anfitrion");
    return;
  }
  const fs::path bak = BackupDir();
  fs::create_directories(bak, ec);
  if (!fs::exists(bak / "pendiente.txt", ec)) {   // la copia buena es la primera (la del jugador)
    fs::copy_file(dst, bak / "data.bin", fs::copy_options::overwrite_existing, ec);
    std::ofstream(bak / "pendiente.txt") << rex::path_to_utf8(dst) << "\n";
  }
  std::ofstream(dst, std::ios::binary).write(reinterpret_cast<const char*>(save_.data()), std::streamsize(save_.size()));
  REXLOG_INFO("dbz3 online: partida del anfitrion puesta ({} bytes); la tuya en {}", save_.size(),
              rex::path_to_utf8(bak));
}

void Session::RestoreSave() {
  const fs::path bak = BackupDir();
  std::error_code ec;
  if (!fs::exists(bak / "pendiente.txt", ec) || !fs::exists(bak / "data.bin", ec)) return;
  std::string dst;
  std::getline(std::ifstream(bak / "pendiente.txt"), dst);
  if (dst.empty()) return;
  fs::copy_file(bak / "data.bin", rex::to_path(dst), fs::copy_options::overwrite_existing, ec);
  if (ec) {
    REXLOG_WARN("dbz3 online: no pude devolver tu partida guardada ({}); sigue en {}", ec.message(),
                rex::path_to_utf8(bak));
    return;
  }
  // la copia se queda (nunca se borra): solo se marca como devuelta
  const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
  fs::rename(bak / "pendiente.txt", bak / ("devuelta_" + stamp + ".txt"), ec);
  REXLOG_INFO("dbz3 online: tu partida guardada ha vuelto a su sitio");
}

// ---------------------------------------------------------------- R1: suma por fotograma
void Session::CompareSum(uint32_t tick) {
  const SumSlot& a = my_sum_[tick % kRing];
  const SumSlot& b = their_sum_[tick % kRing];
  if (a.tick != tick || b.tick != tick) return;
  uint32_t mask = 0;
  for (int i = 0; i < 5; ++i) mask |= (a.b[i] != b.b[i]) << i;
  ++sum_cmp_;
  if (!mask) return;
  const uint32_t bad = ++sum_bad_;
  if (rb_mode_.load() && tick > rb_ignore_until_ && (rb_bad_first_.load() == 0xFFFFFFFFu || tick < rb_bad_first_.load())) rb_bad_first_ = tick;
  if (bad <= 20 || bad % 100 == 0)
    REXLOG_WARN("dbz3 online: SUMA distinta en el tick {} (bloques {:05b}: gestor/luch0/luch1/resto/rng; propios {:08X} {:08X} {:08X} {:08X} {:08X}; "
                "del otro {:08X} {:08X} {:08X} {:08X} {:08X}; {} iguales antes)", tick, mask, a.b[0], a.b[1], a.b[2], a.b[3], a.b[4],
                b.b[0], b.b[1], b.b[2], b.b[3], b.b[4], sum_cmp_.load() - bad);
  if (sum_hook && bad <= 8) sum_hook(tick, mask);
}

void Session::SendSums() {
  const uint32_t last = sum_sent_;   // ultimo tick guardado
  Writer w(kSum);
  std::vector<const SumSlot*> v;
  for (uint32_t t = last >= 7 ? last - 7 : 0; t <= last; ++t) {
    const SumSlot& s = my_sum_[t % kRing];
    if (s.tick == t) v.push_back(&s);
  }
  if (v.empty()) return;
  w.u8(uint8_t(v.size()));
  for (const SumSlot* s : v) { w.u32(s->tick); for (uint32_t x : s->b) w.u32(x); }
  Send(w.b);
}

void Session::NoteSum(uint32_t tick, const uint32_t* b) {
  if (!active()) return;
  SumSlot& s = my_sum_[tick % kRing];
  s.tick = tick;
  std::memcpy(s.b, b, sizeof(s.b));
  sum_sent_ = tick;
  SendSums();
  CompareSum(tick);
}

// ---------------------------------------------------------------- lockstep
void Session::SendInputs() {
  if (sched_ == 0) return;   // aun sin mandos propios
  Writer w(kInput);
  const uint32_t last = sched_ - 1;   // la casilla mas nueva (con D recien bajado puede pasar de tick + D)
  const uint32_t first = last >= 7 ? last - 7 : 0;
  std::vector<const Slot*> v;
  for (uint32_t t = first; t <= last; ++t) {
    const Slot& s = mine_[t % kRing];
    if (s.tick == t) v.push_back(&s);
  }
  if (v.empty()) return;
  w.u8(uint8_t(v.size()));
  for (const Slot* s : v) { w.u32(s->tick); w.pad(s->pad); }
  const int i = int((tick_ / 60) % kRing);
  w.u32(my_rng_tick_[i]);
  w.u32(my_rng_[i]);
  const uint64_t now = NowMs();
  w.u32(uint32_t(now) | 1u);   // marca (nunca 0: 0 = sin eco)
  w.u32(peer_stamp_);
  w.u16(uint16_t(peer_stamp_ ? std::min<uint64_t>(now - peer_stamp_at_, 0xFFFF) : 0));
  w.u8(uint8_t(delay_ | (auto_delay_ ? 0x80 : 0)));
  Send(w.b);
}

void Session::Drain() {
  std::vector<uint8_t> pkt;
  for (int i = 0; i < 1024 && active() && Recv(pkt); ++i) {   // hasta vaciarlo (tras una carga hay muchos)
    if (!pkt.empty()) Handle(pkt, true);
    pkt.clear();
  }
  FlushRtt();
}

// Un paquete que espero en el socket mientras este PC cargaba parece ping (medido 2026-10-15: el retardo subia a
// 10 al entrar en cada combate). De cada tanda leida de golpe solo cuenta la muestra mas pequena: la del ultimo
// en llegar.
void Session::FlushRtt() {
  if (batch_rtt_ == UINT32_MAX) return;
  ++rtt_n_;
  rtt_sum_ += batch_rtt_;
  rtt_max_ = std::max(rtt_max_, batch_rtt_);
  batch_rtt_ = UINT32_MAX;
}

void Session::SetDelay(uint8_t d, const char* why) {
  const uint8_t was = delay_;
  if (d == was) return;
  delay_ = d;
  delay_changed_ms_ = NowMs();
  REXLOG_INFO("dbz3 online: retardo {} -> {} en el tick {} ({}; ping {} ms)", was, d, tick_.load(), why,
              ping_ms_.load());
}

void Session::AdaptDelay(uint32_t t) {
  std::lock_guard<std::recursive_mutex> io(io_mu_);
  const uint32_t n = rtt_n_, worst = rtt_max_, stalls = stalls_;
  if (n) ping_ms_ = int(rtt_sum_ / n);
  rtt_n_ = rtt_sum_ = rtt_max_ = stalls_ = 0;
  if (t % 600 == 0)   // cada 10 s: estado de la conexion en el log (para diagnosticar cortes y tirones)
    REXLOG_INFO("dbz3 online: tick {}: retardo {}{}, ping {} ms (peor {}), {} esperas, {} comprobaciones del RNG iguales",
                t, delay_.load(), auto_view_.load() ? " AUTO" : "", ping_ms_.load(), worst, stalls, rng_ok_);
  if (role_ != Role::kHost || !auto_delay_ || !n) return;
  // D cubre medio ping (el peor del segundo, con la espera de un cuadro a cada lado ya dentro) + 4 ms;
  // con un tick de 1/60 s. ponytail: 60 Hz fijos; medir el ritmo real si algun modo corre a 30.
  const int d = delay_;
  // R4 retardo hibrido: con rollback el retardo cubre el medio ping menos la ventana de prediccion (los ticks de
  // diferencia los absorbe una resimulacion de <= W ticks); sin el, el minimo de siempre.
  const int w = rb_mode_.load() ? rb_w_eff_.load() : 0;
  auto need = [worst, w](double margin_ms) {
    const int lo = w ? 1 : int(kMinDelay);
    return std::clamp(int(std::ceil((worst * 0.5 + margin_ms) * 60.0 / 1000.0)) - w, lo, int(kMaxDelay));
  };
  int want = need(4.0);
  const bool stalled = stalls >= 6;   // >= 10 % de los ticks esperando al otro: falta retardo
  if (stalled) {
    want = std::max(want, std::min(d + 1, int(kMaxDelay)));
    no_down_until_ = t + 600;   // 10 s sin bajar: si no, sube y baja sin parar
  }
  if (want > d) {
    down_votes_ = 0;
    SetDelay(uint8_t(want), stalled ? "esperas" : "ping");
  } else if (need(12.0) < d && !stalls && t >= no_down_until_) {   // 8 ms de margen: sin ir y venir 8 <-> 9
    if (++down_votes_ >= 3) {   // tras 3 s seguidos sobrando, baja uno por segundo mientras siga sobrando
      down_votes_ = 3;
      SetDelay(uint8_t(d - 1), "sobra retardo");
    }
  } else {
    down_votes_ = 0;
  }
}

namespace {
// Misma pulsacion para el juego: botones, gatillos y palancas por direccion (una muestra asi se puede
// saltar al bajar el retardo sin perder nada; el valor analogico exacto da igual un cuadro).
bool SameInput(const Pad& a, const Pad& b) {
  auto dir = [](int16_t v) { return v > 8000 ? 1 : v < -8000 ? -1 : 0; };
  return a.buttons == b.buttons && (a.lt > 30) == (b.lt > 30) && (a.rt > 30) == (b.rt > 30) &&
         dir(a.lx) == dir(b.lx) && dir(a.ly) == dir(b.ly) && dir(a.rx) == dir(b.rx) && dir(a.ry) == dir(b.ry);
}
}  // namespace

std::atomic<uint32_t> g_vblank{0};   // R7: vblanks del guest (lo incrementa el gancho de netplay_hooks.cpp)
namespace {
// R5 (DBZ3_NET_PADLOG=1, apagado por defecto): mandos aplicados por tick en lockstep, para comparar los dos PCs (padlog_host|guest_<pid>.csv en D:\tmp\padlog)
void PadLogTick(uint32_t t, bool host, const Pad& u0, const Pad& u1) {
  static const bool on = std::getenv("DBZ3_NET_PADLOG") != nullptr;
  if (!on) return;
  static FILE* f = nullptr;
  if (!f) {
    std::error_code ec;
    std::filesystem::create_directories("D:\\tmp\\padlog", ec);
    f = std::fopen((std::string("D:\\tmp\\padlog\\padlog_") + (host ? "host" : "guest") + "_" + std::to_string(unsigned(std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFF)) + ".csv").c_str(), "w");
    if (!f) return;
  }
  // R7: columnas 16-17 = vblanks del guest acumulados y microsegundos de pared (un lado que pierde vblanks entre ticks se ve aqui)
  static const auto t0 = std::chrono::steady_clock::now();
  const long long us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  std::fprintf(f, "%u,%04X,%u,%u,%d,%d,%d,%d,%04X,%u,%u,%d,%d,%d,%d,%u,%lld\n", t, u0.buttons, u0.lt, u0.rt, u0.lx, u0.ly, u0.rx, u0.ry,
               u1.buttons, u1.lt, u1.rt, u1.lx, u1.ly, u1.rx, u1.ry, g_vblank.load(), us);
  if (t % 30 == 0) std::fflush(f);
}
}  // namespace

void Session::RunTick(const Pad& local, uint32_t rng) {
  armed_ = false;
  if (!active()) return;
  if (leave_req_.exchange(false)) {   // salir pedido desde la pantalla: lo cierra el hilo del socket
    std::lock_guard<std::recursive_mutex> io(io_mu_);
    guest_owner_ = false;
    ResetTransport();
    REXLOG_INFO("dbz3 online: sesion cerrada por el jugador");
    return;
  }
  const uint32_t t = tick_;
  if (rules_agreed_ && !(CombatRules::Now() == play_rules_)) {
    play_rules_.Apply();
    REXLOG_INFO("dbz3 online: las reglas de combate quedan fijas durante el online (las de la sala)");
  }
  // todo lo llegado, cada tick (antes solo al esperar): el ping no cuenta lo que espera en el socket y el
  // invitado ve enseguida el retardo nuevo del anfitrion
  Drain();
  if (!active()) return;
  if (t && t % 60 == 0) AdaptDelay(t);
  // casillas propias: cada una una vez y en orden (ver netplay.h). D subio: la muestra va tambien a la casilla
  // siguiente, como mucho una de mas por tick (cada casilla viaja en varios paquetes: aguanta perdidas); D bajo:
  // se salta la muestra si es igual a la anterior (si no, se guarda y la bajada espera)
  const uint32_t target = t + delay_;
  if (sched_ <= target) {
    for (int k = 0; k < 2 && sched_ <= target; ++k, ++sched_) mine_[sched_ % kRing] = {sched_, local};
  } else if (!SameInput(local, mine_[(sched_ - 1) % kRing].pad)) {
    mine_[sched_ % kRing] = {sched_, local};
    ++sched_;
  }
  if (t % 60 == 0 && rng && (!rb_mode_ || t <= rb_conf_)) {   // con rollback solo vale si todo lo anterior esta confirmado
    const int i = int((t / 60) % kRing);
    my_rng_tick_[i] = t;
    my_rng_[i] = rng;
    // el del otro pudo llegar antes que el propio (antes ese segundo no se comparaba)
    if (their_rng_tick_[i] == t && their_rng_[i] != rng && NoFail()) {
      static int n = 0;
      if (n++ < 10) REXLOG_WARN("dbz3 online: (NOFAIL) RNG distinto en el tick {} ({:08X} / {:08X})", t, rng, their_rng_[i]);
    } else if (their_rng_tick_[i] == t && their_rng_[i] != rng && rb_mode_.load()) {
      // con rollback lo trata la reparacion (ver Handle)
    } else if (their_rng_tick_[i] == t && their_rng_[i] != rng && !desync_.exchange(true)) {
      REXLOG_WARN("dbz3 online: DESINCRONIA en el tick {} (rng {:08X} / {:08X})", t, rng, their_rng_[i]);
      Fail(Link::kError, T::T("DESINCRONIA: los dos PCs ya no veian lo mismo", "DESYNC: both PCs no longer saw the same thing"));
      return;
    }
    if (their_rng_tick_[i] == t) ++rng_ok_;
  }
  SendInputs();
  const Pad mine = mine_[t % kRing].tick == t ? mine_[t % kRing].pad : Pad{};
  Pad theirs{};
  if (rb_mode_.load()) {   // R4: el rival no se espera aqui (RbWait, antes del tick): si falta, se predice (se repite su ultimo mando)
    const Slot& sl = theirs_[t % kRing];
    const bool real = sl.tick == t;
    theirs = real ? sl.pad : rb_last_;
    rb_used_[t % kRing] = {t, theirs};
    rb_stats_.predicted += !real;
    ++rb_stats_.ticks;
    applied_[role_ == Role::kHost ? 0 : 1] = mine;
    applied_[role_ == Role::kHost ? 1 : 0] = theirs;
    ++tick_;
    return;
  }
  if (t >= delay0_) {
    std::vector<uint8_t> pkt;
    uint64_t start = NowMs();
    bool waited = false;
    while (theirs_[t % kRing].tick != t) {
      waited = true;
      if (leave_req_.load()) {
        std::lock_guard<std::recursive_mutex> io(io_mu_);
        guest_owner_ = false; ResetTransport(); return;
      }
      bool got = false;
      for (int batch = 0; batch < 64 && Recv(pkt); ++batch) {
        if (!pkt.empty()) { Handle(pkt, true); got = true; }
        if (!active()) return;   // BYE u otro fallo: sin esperar 10 s a que caduque
        pkt.clear();
      }
      FlushRtt();
      if (theirs_[t % kRing].tick == t) break;
      const uint64_t now = NowMs();
      if (now - last_send_ms_ > 15) SendInputs();
      // el primer tick espera a que el otro PC termine de arrancar
      if (now - start > (t <= delay0_ ? 30000 : kLostMs)) {
        Fail(Link::kLost, T::T("sin respuesta del otro PC", "no reply from the other PC"));
        REXLOG_WARN("dbz3 online: conexion perdida en el tick {}", t);
        return;
      }
      if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // espera por la red (3-100 ms): falta retardo. Mas larga es una carga del otro PC, no la conexion
    if (waited) {
      const uint64_t w = NowMs() - start;
      if (w >= 3 && w < 100) ++stalls_;
    }
    theirs = theirs_[t % kRing].pad;
  }
  // usuario 0 = anfitrion, usuario 1 = invitado (en los dos PCs)
  applied_[role_ == Role::kHost ? 0 : 1] = mine;
  applied_[role_ == Role::kHost ? 1 : 0] = theirs;
  PadLogTick(t, role_ == Role::kHost, applied_[0], applied_[1]);   // R5: DBZ3_NET_PADLOG=1
  ++tick_;
}

// ---------------------------------------------------------------- R4: rollback
namespace {
bool SamePad(const Pad& a, const Pad& b) {
  return a.buttons == b.buttons && a.lt == b.lt && a.rt == b.rt && a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry;
}
}  // namespace

// Al empezar el combate (TakeStart): configuracion acordada. Los dos piden rollback (bit 0x10 de la sala) o ninguno.
void Session::RbApplyW() {
  rb_mode_ = false;
  bool both;
  { std::lock_guard<std::mutex> lock(mu_); both = (my_rules_.flags & 0x10) && (peer_rules_.flags & 0x10); }
  rb_on_ = both;
  int w = 2;
  if (const char* e = std::getenv("DBZ3_RB_W")) w = std::atoi(e);
  else w = std::atoi(rex::cvar::GetFlagByName("dbz3_net_rollback_ventana").c_str());
  rb_w_ = std::clamp(w, 0, 6);
  if (const char* e = std::getenv("DBZ3_RB_BUDGET")) rb_budget_ms_ = std::atof(e);
  rb_ms_tick_ = 3.5;
  rb_w_eff_ = rb_w_;
  rb_stats_ = {};
  REXLOG_INFO("dbz3 online: rollback {} (ventana {}, presupuesto {:.0f} ms)", rb_on_ ? "ACTIVO" : "apagado", rb_w_, rb_budget_ms_);
}

void Session::RbEnter(uint32_t t) {
  rb_conf_ = t;
  rb_t0_ = t;
  rb_sum_next_ = t;
  rb_resim_ = false;
  rb_stats_ = {};
  rb_bad_first_ = 0xFFFFFFFFu; rb_ignore_until_ = 0; rb_repairs_ = 0; snap_tx_on_ = false; snap_rx_cnt_ = 0; snap_rx_ready_ = false;
  peer_est_ms_ = 0;
  // base de la prediccion: el mando del rival del tick anterior (ya confirmado por el lockstep)
  rb_last_ = applied_[role_ == Role::kHost ? 1 : 0];
  down_votes_ = 3;       // el retardo del lockstep de los menus (sin ventana) baja ya, sin esperar los 3 s
  no_down_until_ = 0;
  rb_mode_ = true;
  REXLOG_INFO("dbz3 online: rollback: combate desde el tick {} (ventana {})", t, rb_w_eff_.load());
}

void Session::RbLeave() {
  rb_mode_ = false;
  rb_resim_ = false;
  REXLOG_INFO("dbz3 online: rollback: fin del combate en el tick {} ({} ticks, {} rollbacks, {} ticks resimulados, {} esperas, {} predichos)",
              tick_.load(), rb_stats_.ticks, rb_stats_.rollbacks, rb_stats_.resim_ticks, rb_stats_.stalls, rb_stats_.predicted);
}

// Lee la red y revisa los ticks sin confirmar [rb_conf_, t): avanza la frontera por los que ya llegaron y devuelve el
// primero cuyo mando real difiere del aplicado (UINT32_MAX si ninguno).
uint32_t Session::RbScan(uint32_t t) {
  Drain();
  if (!active()) return UINT32_MAX;
  uint32_t first = UINT32_MAX;
  bool gap = false;
  uint32_t conf = rb_conf_;
  for (uint32_t k = rb_conf_; k <= t; ++k) {   // el propio t solo avanza la frontera (aun no se aplico nada)
    const Slot& s = theirs_[k % kRing];
    if (s.tick != k) { gap = true; continue; }
    const Slot& u = rb_used_[k % kRing];
    if (k < t && (u.tick != k || !SamePad(u.pad, s.pad)) && first == UINT32_MAX) first = k;
    if (!gap) { conf = k + 1; rb_last_ = s.pad; }
  }
  rb_conf_ = conf;
  rb_stats_.max_unconf = std::max(rb_stats_.max_unconf, t + 1 - std::min(conf, t + 1));
  return first;
}

// Puede empezar el tick t? 1 si; 0 = hay que seguir esperando al rival (duerme 1 ms); -1 = se perdio la conexion.
// Con `all` pide que todo lo anterior a t este confirmado (al salir del combate). Sin `all`: tras jugar t no puede haber
// mas de W ticks sin confirmar (con W = 0 es el lockstep de siempre: hace falta el mando de t).
int Session::RbWait(uint32_t t, uint64_t* start_ms, bool all) {
  const uint32_t unconf = all ? t - std::min(rb_conf_, t) : t + 1 - std::min(rb_conf_, t + 1);
  // los primeros ticks del combate (presentacion: camaras y animaciones fuera de la region) van en lockstep puro
  static const uint32_t warm = std::getenv("DBZ3_RB_WARMUP") ? uint32_t(std::atoi(std::getenv("DBZ3_RB_WARMUP"))) : 30u;
  const uint32_t w = t - rb_t0_ < warm ? 0u : uint32_t(rb_w_eff_.load());
  if (unconf <= (all ? 0u : w)) return 1;
  if (!*start_ms) { *start_ms = NowMs(); ++stalls_; ++rb_stats_.stalls; }
  if (leave_req_.load()) { std::lock_guard<std::recursive_mutex> io(io_mu_); guest_owner_ = false; ResetTransport(); return -1; }
  const uint64_t now = NowMs();
  if (now - last_send_ms_ > 15) SendInputs();
  if (now - *start_ms > kLostMs) {
    Fail(Link::kLost, T::T("sin respuesta del otro PC", "no reply from the other PC"));
    REXLOG_WARN("dbz3 online: conexion perdida en el tick {} (rollback)", t);
    return -1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return 0;
}

double Session::RbLead(uint32_t t) const {
  const uint64_t at = peer_est_ms_.load();
  if (!at) return 0;
  const int ping = ping_ms_.load();
  const double peer_now = double(peer_est_.load()) + (double(NowMs() - at) + (ping > 0 ? ping * 0.5 : 0.0)) * 60.0 / 1000.0;
  return double(t) - peer_now;
}

bool Session::RbSnapSend(uint32_t tick, const uint8_t* data, size_t n) {
  if (snap_tx_on_ || role_ != Role::kHost || n == 0 || n > 262144) return false;
  snap_tx_.assign(data, data + n);
  snap_tx_tick_ = tick;
  snap_tx_next_ = 0;
  snap_tx_start_ms_ = NowMs();
  snap_tx_on_ = true;
  return true;
}

// 12 trozos por tick (~12 KB) en vuelta hasta el acuse; a los 5 s sin acuse se abandona
void Session::RbSnapPump() {
  if (!snap_tx_on_) return;
  if (NowMs() - snap_tx_start_ms_ > 5000) { snap_tx_on_ = false; REXLOG_WARN("dbz3 online: rollback: la instantanea del tick {} no fue acusada", snap_tx_tick_); return; }
  const uint32_t cnt = uint32_t((snap_tx_.size() + 999) / 1000);
  for (int i = 0; i < 12; ++i) {
    const uint32_t idx = snap_tx_next_++ % cnt;
    const size_t off = size_t(idx) * 1000, n = std::min<size_t>(1000, snap_tx_.size() - off);
    Writer w(kSnap);
    w.u32(snap_tx_tick_); w.u32(uint32_t(snap_tx_.size())); w.u16(uint16_t(idx)); w.u16(uint16_t(cnt)); w.u16(uint16_t(n));
    w.bytes(snap_tx_.data() + off, n);
    Send(w.b);
  }
}

bool Session::RbSnapTake(uint32_t* tick, std::vector<uint8_t>* data) {
  if (!snap_rx_ready_) return false;
  snap_rx_ready_ = false;
  *tick = snap_rx_tick_;
  *data = snap_rx_;
  snap_rx_cnt_ = 0;   // una nueva con el mismo tick vuelve a armarse
  return true;
}

void Session::RbRepaired(uint32_t k) {
  rb_ignore_until_ = k + 40;
  rb_bad_first_ = 0xFFFFFFFFu;
  ++rb_repairs_;
}

// Resimulacion del tick k: mando propio real, del rival el real si llego y si no el predicho (el ultimo confirmado).
void Session::RbBeginResim(uint32_t k, bool wrong) {
  const Slot& m = mine_[k % kRing];
  const Slot& th = theirs_[k % kRing];
  Pad mine = m.tick == k ? m.pad : Pad{};
  Pad theirs = th.tick == k ? th.pad : rb_last_;
  if (wrong) {   // PRUEBA DE PUREZA (DBZ3_RB_ORACLE): mandos falsos para ensuciar lo que el rollback no restaura
    static const uint16_t kB[4] = {0x1000, 0x2000, 0x4000, 0x8000};
    mine.buttons = kB[(k * 7) & 3]; mine.lx = (k & 1) ? 32000 : -32000; mine.ly = (k & 2) ? 32000 : -32000;
    theirs.buttons = kB[(k * 5 + 1) & 3]; theirs.lx = (k & 2) ? 32000 : -32000; theirs.ly = (k & 1) ? -32000 : 32000;
  } else {
    rb_used_[k % kRing] = {k, theirs};
  }
  applied_[role_ == Role::kHost ? 0 : 1] = mine;
  applied_[role_ == Role::kHost ? 1 : 0] = theirs;
  rb_resim_k_ = k;
  rb_resim_ = true;
}

// Mide el coste de resimular y ajusta la ventana: si no cabe en el presupuesto del cuadro, la ventana se encoge (hasta 0 =
// lockstep puro) y vuelve a crecer cuando los cuadros resimulados salen baratos.
void Session::RbNoteResim(uint32_t depth, double ms_per_tick) {
  ++rb_stats_.rollbacks;
  rb_stats_.resim_ticks += depth;
  rb_stats_.deepest = std::max(rb_stats_.deepest, depth);
  ++rb_stats_.depth[std::min<uint32_t>(depth, 8)];
  rb_ms_tick_ = rb_ms_tick_ * 0.9 + ms_per_tick * 0.1;
  const int fit = int(rb_budget_ms_ / std::max(rb_ms_tick_, 0.5));
  const int was = rb_w_eff_.load();
  const int now = std::clamp(fit, 0, rb_w_);
  if (now != was) {
    rb_w_eff_ = now;
    REXLOG_INFO("dbz3 online: rollback: ventana {} -> {} (resimular cuesta {:.2f} ms/tick, presupuesto {:.0f} ms)", was, now, rb_ms_tick_, rb_budget_ms_);
  }
}

}  // namespace dbz3::net

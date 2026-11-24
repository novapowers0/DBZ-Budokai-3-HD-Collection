// dbz3 - Sonidos XMA de B3 HD para codigo propio (ver xma_propio.h). La division en tramas sigue a XmaContext::Decode/GetPacketInfo
// (rexglue-sdk-0.10/src/audio/xma_context.cpp): cabecera de paquete de 32 bits (desplazamiento de la primera trama en bits), tramas de
// longitud de 15 bits que pueden cruzar paquetes, ultimo bit de la trama = "sigue otra en este paquete". Cada trama va al decodificador
// xmaframes con el byte de relleno que espera (PreparePacket) y la salida se realinea 192 muestras (kDecoderStartPadding).
#include "xma_propio.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>

extern "C" {
#include "libavcodec/avcodec.h"
}

#ifndef XMA_PROPIO_TEST   // tests/xma_propio_test.cpp: solo el decodificador, sin el juego
#include <rex/logging.h>

#include "ingame/native_ui.h"
#if REX_PLATFORM_WIN32
#include <windows.h>
#include <mmsystem.h>
#endif
#endif

namespace dbz3::xma {
namespace {

constexpr uint32_t kPaquete = 2048, kBitsCarga = (kPaquete - 4) * 8, kRelleno = 192;

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

// Desplazamiento (bits, desde la carga del paquete) de la primera trama que empieza en el paquete; > kBitsCarga = ninguna.
uint32_t PrimeraTrama(const uint8_t* p) { return uint32_t(((p[0] & 3) << 13) | (p[1] << 5) | (p[2] >> 3)); }

// Flujo de bits hecho de las cargas de los paquetes (sin sus cabeceras), MSB primero.
struct Bits {
  const uint8_t* d;
  size_t paquetes;
  uint64_t total() const { return uint64_t(paquetes) * kBitsCarga; }
  int bit(uint64_t pos) const {
    const uint64_t pq = pos / kBitsCarga, r = pos % kBitsCarga;
    return d[pq * kPaquete + 4 + r / 8] >> (7 - r % 8) & 1;
  }
  uint32_t leer(uint64_t pos, int n) const {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = v << 1 | uint32_t(bit(pos + uint64_t(i)));
    return v;
  }
};

}  // namespace

std::vector<int16_t> Decodificar(const uint8_t* datos, size_t bytes, int canales, int muestras, int hz) {
  std::vector<int16_t> out;
  const size_t np = bytes / kPaquete;
  if (!datos || !np || canales < 1 || canales > 2) return out;
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
  if (!codec) return out;
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  AVPacket* pkt = av_packet_alloc();
  AVFrame* fr = av_frame_alloc();
  std::vector<uint8_t> trama(1 + 0x8000 / 8 + 2);
  if (ctx && pkt && fr) {
    ctx->sample_rate = hz;   // la del banco: decide las bandas del decodificador (con 48000 fijo salen tramas rotas)
    ctx->channels = canales;
    ctx->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;
  }
  if (ctx && pkt && fr && avcodec_open2(ctx, codec, nullptr) >= 0) {
    const Bits b{datos, np};
    uint64_t pos = PrimeraTrama(datos);
    int tramas = 0;
    while (pos + 15 <= b.total() && tramas < 100000) {
      const uint32_t largo = b.leer(pos, 15);
      if (largo <= 15 || largo == 0x7FFF || pos + largo > b.total()) break;
      // trama -> paquete del decodificador: byte 0 = (relleno inicial << 5) | (relleno final << 2); bits desde el byte 1
      std::fill(trama.begin(), trama.end(), uint8_t(0));
      for (uint32_t i = 0; i < largo; ++i)
        if (b.bit(pos + i)) trama[1 + i / 8] |= uint8_t(0x80 >> (i % 8));
      const int tam = int(1 + (largo + 7) / 8);
      trama[0] = uint8_t(((tam * 8 - 8 - int(largo)) & 7) << 2);
      pkt->data = trama.data();
      pkt->size = tam;
      if (avcodec_send_packet(ctx, pkt) >= 0 && avcodec_receive_frame(ctx, fr) >= 0) {
        const float* c0 = reinterpret_cast<const float*>(fr->data[0]);
        const float* c1 = canales == 2 ? reinterpret_cast<const float*>(fr->data[1]) : nullptr;
        for (int i = 0; i < fr->nb_samples; ++i) {
          out.push_back(int16_t(std::clamp(c0[i], -1.0f, 1.0f) * 32767.0f));
          if (c1) out.push_back(int16_t(std::clamp(c1[i], -1.0f, 1.0f) * 32767.0f));
        }
      }
      ++tramas;
      // siguiente trama: seguida si el ultimo bit es 1 y no cruza paquete; si no, la primera del paquete siguiente que tenga una
      const uint64_t pq = pos / kBitsCarga, fin = pos + largo;
      if (b.bit(fin - 1) && fin / kBitsCarga == pq) {
        pos = fin;
        continue;
      }
      uint64_t sig = pq + 1;
      while (sig < np && PrimeraTrama(datos + sig * kPaquete) >= kBitsCarga) ++sig;
      if (sig >= np) break;
      pos = sig * kBitsCarga + PrimeraTrama(datos + sig * kPaquete);
      if (pos < fin) break;   // no deberia: trama que se pisa
    }
  }
  if (fr) av_frame_free(&fr);
  if (pkt) av_packet_free(&pkt);
  if (ctx) avcodec_free_context(&ctx);
  const size_t pad = size_t(kRelleno) * size_t(canales);
  out.erase(out.begin(), out.begin() + std::ptrdiff_t(std::min(pad, out.size())));
  if (muestras > 0 && out.size() > size_t(muestras) * size_t(canales)) out.resize(size_t(muestras) * size_t(canales));
  return out;
}

namespace {
// Banco de B3 HD (BE): +0x00 0x00020100, +0x08 id, +0x0C sonidos, +0x10 inicio de los datos, +0x18 tabla de registros (u32 por
// sonido, desde el banco). Registro: +0x00 "xma", +0x14 tamano, +0x18 desplazamiento (desde los datos); XMA2WAVEFORMATEX en +0xBC
// (+0xBE canales, +0xC0 Hz, +0xE0 PlayLength). Va suelto (data_cmn 2195/2198/3983/3986 = bancos 0..3 del combate) o como hijo 0 de
// un #AMB (gritos de lang_*: tabla de hijos en +0x20, 16 B {desplazamiento, tamano, -1, 0}).
bool Banco(const std::vector<uint8_t>& d, size_t& b0, size_t& tam) {
  b0 = 0, tam = d.size();
  if (d.size() >= 0x40 && std::memcmp(d.data(), "#AMB", 4) == 0) b0 = Be32(&d[0x20]), tam = Be32(&d[0x24]);
  return tam >= 0x100 && b0 + tam <= d.size() && Be32(&d[b0]) == 0x00020100;
}
const uint8_t* Registro(const std::vector<uint8_t>& d, size_t b0, size_t tam, int i) {
  const size_t t = Be32(&d[b0 + 0x18]) + 4 * size_t(i);
  if (t + 4 > tam) return nullptr;
  const size_t r = Be32(&d[b0 + t]);
  return r + 0x100 <= tam ? &d[b0 + r] : nullptr;
}
}  // namespace

int SonidosEnBanco(const std::vector<uint8_t>& amb) {
  size_t b0 = 0, tam = 0;
  if (!Banco(amb, b0, tam)) return 0;
  const uint32_t n = Be32(&amb[b0 + 0x0C]);
  return n <= (tam - 0x20) / 4 ? int(n) : 0;
}

bool SonidoDeBanco(const std::vector<uint8_t>& amb, int i, Sonido& out) {
  out = Sonido{};
  size_t b0 = 0, tam = 0;
  if (i < 0 || i >= SonidosEnBanco(amb) || !Banco(amb, b0, tam)) return false;
  const uint8_t* bk = &amb[b0];
  const uint8_t* r = Registro(amb, b0, tam, i);
  if (!r || std::memcmp(r, "xma", 3) != 0 || Be16(r + 0xBC) != 0x0166) return false;
  const size_t datos = Be32(bk + 0x10), largo = Be32(r + 0x14), desde = Be32(r + 0x18);
  if (!largo || datos + desde + largo > tam) return false;
  out.canales = std::clamp<int>(Be16(r + 0xBE), 1, 2);
  out.hz = int(Be32(r + 0xC0));
  out.pcm = Decodificar(bk + datos + desde, largo, out.canales, int(Be32(r + 0xE0)), out.hz);
  return !out.pcm.empty() && out.hz >= 4000 && out.hz <= 96000;
}

float VolumenSecuencia(const std::vector<uint8_t>& sq, int i) {
  // IECS "Sequ" (LE): secuencia por sonido con la nota 0x009000ii seguida del byte de volumen (0..127). 1 si no esta.
  for (size_t o = 0; o + 6 <= sq.size(); o += 4)
    if (sq[o] == 0 && sq[o + 1] == 0x90 && sq[o + 2] == 0 && sq[o + 3] == uint8_t(i) && sq[o + 4] <= 127) return float(sq[o + 4]) / 127.0f;
  return 1.0f;
}

#ifndef XMA_PROPIO_TEST
namespace {
std::mutex g_mx;
std::map<std::pair<std::string, int>, std::vector<uint8_t>> g_amb;   // entrada descomprimida
std::map<std::tuple<std::string, int, int>, Sonido> g_cache;
std::atomic<uint32_t> g_gen[8], g_todo{0};
std::atomic<int> g_vivos{0};
std::atomic<int> g_log{0};

bool Cargar(const std::filesystem::path& afs, int entrada, int i, Sonido& s) {
  const std::string k = afs.string();
  std::vector<uint8_t>* amb = nullptr;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    if (auto it = g_cache.find({k, entrada, i}); it != g_cache.end()) { s = it->second; return !s.pcm.empty(); }
    if (auto it = g_amb.find({k, entrada}); it != g_amb.end()) amb = &it->second;
  }
  if (!amb) {
    std::vector<uint8_t> d = dbz3::ingame::native_ui::LeerEntrada(afs, entrada);
    std::lock_guard<std::mutex> lk(g_mx);
    amb = &(g_amb[{k, entrada}] = std::move(d));
    if (g_log++ < 20)
      REXLOG_INFO("dbz3 xma: banco {} {}: {} B, {} sonidos", afs.filename().string(), entrada, amb->size(), SonidosEnBanco(*amb));
  }
  Sonido n;
  if (SonidoDeBanco(*amb, i, n)) {   // los bancos no se sueltan: viven lo que el proceso (map estable)
    // volumen de su secuencia (como el gestor de efectos): hijo 2 del #AMB o, suelto, la entrada +2 (3983 -> 3985)
    std::vector<uint8_t> sq;
    if (amb->size() >= 0x60 && std::memcmp(amb->data(), "#AMB", 4) == 0 && Be32(&(*amb)[0x10]) >= 3) {
      const size_t o = Be32(&(*amb)[0x40]), t = Be32(&(*amb)[0x44]);
      if (o + t <= amb->size()) sq.assign(amb->begin() + std::ptrdiff_t(o), amb->begin() + std::ptrdiff_t(o + t));
    } else {
      sq = dbz3::ingame::native_ui::LeerEntrada(afs, entrada + 2);
      if (sq.size() < 4 || std::memcmp(sq.data(), "IECS", 4) != 0) sq.clear();
    }
    const float v = VolumenSecuencia(sq, i);
    for (int16_t& x : n.pcm) x = int16_t(float(x) * v);
  }
  std::lock_guard<std::mutex> lk(g_mx);
  if (g_log++ < 60)
    REXLOG_INFO("dbz3 xma: sonido {} {} id {}: {} Hz, {} canal(es), {:.2f} s", afs.filename().string(), entrada, i, n.hz, n.canales,
                n.hz ? double(n.pcm.size()) / n.canales / n.hz : 0.0);
  s = g_cache[{k, entrada, i}] = std::move(n);
  return !s.pcm.empty();
}
}  // namespace

void Precargar(const std::filesystem::path& afs, int entrada, int i) {
  std::thread([afs, entrada, i] { Sonido s; Cargar(afs, entrada, i, s); }).detach();
}

bool Oir(const std::filesystem::path& afs, int entrada, int i, float volumen, int tipo) {
  {
    std::lock_guard<std::mutex> lk(g_mx);
    if (auto it = g_cache.find({afs.string(), entrada, i}); it != g_cache.end() && it->second.pcm.empty()) return false;
  }
  if (volumen <= 0) {   // en silencio solo se decodifica (queda en la cache y en el registro)
    Precargar(afs, entrada, i);
    return true;
  }
  if (g_vivos.load() >= 12) return true;   // tope de voces a la vez (cada una abre su waveOut)
  const int t = tipo >= 0 && tipo < 8 ? tipo : -1;
  const uint32_t gen = t >= 0 ? ++g_gen[t] : 0, todo = g_todo.load();
  ++g_vivos;
  std::thread([=] {
    Sonido s;
    if (Cargar(afs, entrada, i, s)) {
      for (int16_t& x : s.pcm) x = int16_t(std::clamp(int(float(x) * volumen), -32768, 32767));
#if REX_PLATFORM_WIN32
      auto vivo = [&] { return (t < 0 || gen == g_gen[t].load()) && todo == g_todo.load(); };
      WAVEFORMATEX f{};
      f.wFormatTag = WAVE_FORMAT_PCM;
      f.nChannels = WORD(s.canales);
      f.nSamplesPerSec = DWORD(s.hz);
      f.wBitsPerSample = 16;
      f.nBlockAlign = WORD(2 * s.canales);
      f.nAvgBytesPerSec = DWORD(s.hz) * f.nBlockAlign;
      HWAVEOUT wo = nullptr;
      if (vivo() && waveOutOpen(&wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        WAVEHDR h{};
        h.lpData = reinterpret_cast<LPSTR>(s.pcm.data());
        h.dwBufferLength = DWORD(s.pcm.size() * 2);
        if (waveOutPrepareHeader(wo, &h, sizeof(h)) == MMSYSERR_NOERROR) {
          if (waveOutWrite(wo, &h, sizeof(h)) == MMSYSERR_NOERROR)
            while (!(h.dwFlags & WHDR_DONE) && vivo()) Sleep(10);
          waveOutReset(wo);
          waveOutUnprepareHeader(wo, &h, sizeof(h));
        }
        waveOutClose(wo);
      }
#endif
    }
    --g_vivos;
  }).detach();
  return true;
}

void CallarTodo() { ++g_todo; }
#endif

}  // namespace dbz3::xma

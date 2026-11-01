// dbz3 - Videos HD opcionales: opening y Ending00 reescalados por SSGPrinceVegeta (<exe>/videos_hd/*.mp4, descarga en el
// launcher, novas_mods.cpp). Incluido desde video_sfd.cpp (solo Windows).
//
// El reproductor del juego (amSofdec, MWSFD 5.46) sigue haciendo TODO con el .sfd original: reloj, audio, volumen, pausa,
// salto, fin de video, y el dibujo (ending: un circulo de triangulos con los creditos alrededor, no una pantalla entera).
// Aqui solo se cambian los pixeles: se agrandan las texturas Y/U/V que crea sub_82091DA8 y, tras la copia del fotograma
// original (sub_820914D8), se copia encima el fotograma HD del mismo instante por la misma funcion del juego (vtable +12).
// Decodifica Media Foundation (de Windows: GPU si la hay, si no CPU); si no abre o no hay H.264, sigue el original.
#include <d3d10_1.h>
#include <d3d11.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <condition_variable>

namespace dbz3::video::hd {
namespace {

constexpr dbz3::GA kPly = 0x8238CAE8;   // unica ficha del reproductor (408 B): +0 mwPly, +8/+20/+32 Y/U/V x3, +44 indice,
                                        // +104 objeto de fotograma, +116/+120 ancho/alto; sub_820917F8 saca de ahi los UV
constexpr size_t kCola = 6;
constexpr uint32_t kFotoNum = 4;   // objeto de fotograma: numero de fotograma, medido (0, 1, 2...)
constexpr uint32_t kFotoFps = 40;  // fps x 1000 (30000 US, 29970 EU y creditos), medido

struct Foto {
  double t = 0;
  std::vector<uint8_t> yuv;   // Y, U, V seguidos
};

// mfplat/mfreadwrite por LoadLibrary: Windows N no las trae y el exe no debe depender de ellas para arrancar.
struct Mf {
  decltype(&MFStartup) Startup = nullptr;
  decltype(&MFCreateAttributes) CreateAttributes = nullptr;
  decltype(&MFCreateMediaType) CreateMediaType = nullptr;
  decltype(&MFCreateDXGIDeviceManager) CreateDXGIDeviceManager = nullptr;
  decltype(&MFCreateSourceReaderFromURL) CreateReader = nullptr;
  decltype(&D3D11CreateDevice) CreateDevice = nullptr;
  bool ok = false;
  Mf() {
    HMODULE p = LoadLibraryW(L"mfplat.dll"), r = LoadLibraryW(L"mfreadwrite.dll");
    if (HMODULE d = LoadLibraryW(L"d3d11.dll")) CreateDevice = reinterpret_cast<decltype(CreateDevice)>(GetProcAddress(d, "D3D11CreateDevice"));
    if (!p || !r) return;
    Startup = reinterpret_cast<decltype(Startup)>(GetProcAddress(p, "MFStartup"));
    CreateAttributes = reinterpret_cast<decltype(CreateAttributes)>(GetProcAddress(p, "MFCreateAttributes"));
    CreateMediaType = reinterpret_cast<decltype(CreateMediaType)>(GetProcAddress(p, "MFCreateMediaType"));
    CreateDXGIDeviceManager = reinterpret_cast<decltype(CreateDXGIDeviceManager)>(GetProcAddress(p, "MFCreateDXGIDeviceManager"));
    CreateReader = reinterpret_cast<decltype(CreateReader)>(GetProcAddress(r, "MFCreateSourceReaderFromURL"));
    ok = Startup && CreateAttributes && CreateMediaType && CreateReader && SUCCEEDED(Startup(MF_VERSION, MFSTARTUP_LITE));
  }
};
Mf& M() {
  static Mf m;
  return m;
}
template <class T> void Soltar(T*& p) {
  if (p) p->Release();
  p = nullptr;
}

class Decoder {
 public:
  int w = 0, h = 0;
  bool gpu = false;
  std::atomic<bool> parar{false};
  std::atomic<int> hechos{0};
  std::atomic<double> ms{0};
  ~Decoder() { Cerrar(); }

  bool Abrir(const std::filesystem::path& ruta) {
    if (!M().ok) return false;
    // GPU primero; sin GPU capaz (o sin D3D11) el decodificador de Windows por CPU.
    if (const char* f = std::getenv("DBZ3_TEST_VIDEOHD_SW"); !(f && *f == '1') && AbrirReader(ruta, true)) return true;
    return AbrirReader(ruta, false);
  }
  void Empezar() { hilo_ = std::thread([this] { Bucle(); }); }
  // Fotograma mas nuevo con t <= `t`; lo que queda atras se suelta. false si aun no hay ninguno.
  bool Tomar(double t, Foto& f) {
    std::unique_lock<std::mutex> lk(mu_);
    bool hay = false;
    while (!cola_.empty() && cola_.front().t <= t) {
      f = std::move(cola_.front());
      cola_.pop_front();
      hay = true;
    }
    if (hay) cv_.notify_all();
    return hay;
  }
  void EsperarPrimero(int ms_max) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait_for(lk, std::chrono::milliseconds(ms_max), [this] { return !cola_.empty() || fin_; });
  }
  double PrimerT() {
    std::lock_guard<std::mutex> lk(mu_);
    return cola_.empty() ? 1e18 : cola_.front().t;
  }
  void Cerrar() {
    parar = true;
    cv_.notify_all();
    if (hilo_.joinable()) hilo_.join();
    Soltar(reader_);
    Soltar(mgr_);
    Soltar(dev_);
  }

 private:
  bool AbrirReader(const std::filesystem::path& ruta, bool con_gpu) {
    Soltar(reader_);
    Soltar(mgr_);
    Soltar(dev_);
    IMFAttributes* at = nullptr;
    if (FAILED(M().CreateAttributes(&at, 4))) return false;
    at->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    at->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, con_gpu ? FALSE : TRUE);
    gpu = false;
    if (con_gpu && M().CreateDXGIDeviceManager && M().CreateDevice) {
      UINT tok = 0;
      if (SUCCEEDED(M().CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
                                      D3D11_SDK_VERSION, &dev_, nullptr, nullptr)) &&
          SUCCEEDED(M().CreateDXGIDeviceManager(&tok, &mgr_)) && SUCCEEDED(mgr_->ResetDevice(dev_, tok))) {
        // Media Foundation usa el dispositivo desde sus hilos: sin esto, cierres al azar (medido: 1 de 7 arranques)
        ID3D10Multithread* mt = nullptr;
        if (SUCCEEDED(dev_->QueryInterface(IID_PPV_ARGS(&mt)))) {
          mt->SetMultithreadProtected(TRUE);
          mt->Release();
        }
        at->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, mgr_);
        gpu = true;
      }
    }
    if (!gpu && con_gpu) {
      at->Release();
      return false;
    }
    const HRESULT hr = M().CreateReader(ruta.c_str(), at, &reader_);
    at->Release();
    IMFMediaType* ty = nullptr;
    if (FAILED(hr) || FAILED(M().CreateMediaType(&ty))) {
      REXLOG_WARN("dbz3 videos HD: Media Foundation no abre {} ({:x})", ruta.string(), uint32_t(hr));
      return false;
    }
    ty->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    ty->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    const DWORD s = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    reader_->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader_->SetStreamSelection(s, TRUE);
    const HRESULT hr2 = reader_->SetCurrentMediaType(s, nullptr, ty);
    ty->Release();
    IMFMediaType* cur = nullptr;
    if (FAILED(hr2) || FAILED(reader_->GetCurrentMediaType(s, &cur))) {
      REXLOG_WARN("dbz3 videos HD: {} sin decodificador NV12 ({:x}, GPU {})", ruta.string(), uint32_t(hr2), con_gpu);
      Soltar(reader_);
      return false;
    }
    UINT32 cw = 0, ch = 0;
    MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &cw, &ch);
    cur->Release();
    w = int(cw), h = int(ch);
    return w > 0 && h > 0;
  }
  void Bucle() {
    const DWORD s = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    while (!parar) {
      {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] { return parar || cola_.size() < kCola; });
      }
      if (parar) break;
      const int64_t t0 = AhoraUs();
      DWORD flags = 0;
      LONGLONG ts = 0;
      IMFSample* m = nullptr;
      const HRESULT hr = reader_->ReadSample(s, 0, nullptr, &flags, &ts, &m);
      if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
        if (FAILED(hr)) REXLOG_WARN("dbz3 videos HD: lectura {:x}", uint32_t(hr));
        Soltar(m);
        break;
      }
      if (!m) continue;
      IMFMediaBuffer* b = nullptr;
      Foto f;
      if (SUCCEEDED(m->ConvertToContiguousBuffer(&b))) {
        f.t = double(ts) / 1e7;
        Copiar(b, f);
        b->Release();
      }
      m->Release();
      if (f.yuv.empty()) continue;
      ms = ms.load() + double(AhoraUs() - t0) / 1000;
      ++hechos;
      std::lock_guard<std::mutex> lk(mu_);
      cola_.push_back(std::move(f));
      cv_.notify_all();
    }
    std::lock_guard<std::mutex> lk(mu_);
    fin_ = true;
    cv_.notify_all();
  }
  // NV12 -> Y, U, V seguidos a w x h (el escalon y la altura alineada los dice Lock2D / el tamano del bufer).
  void Copiar(IMFMediaBuffer* b, Foto& f) {
    BYTE* d = nullptr;
    DWORD maxl = 0, cur = 0;
    LONG paso = 0;
    IMF2DBuffer* b2 = nullptr;
    const bool dos = SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&b2))) && SUCCEEDED(b2->Lock2D(&d, &paso));
    if (!dos && FAILED(b->Lock(&d, &maxl, &cur))) {
      Soltar(b2);
      return;
    }
    if (!dos) paso = LONG(w);
    b->GetCurrentLength(&cur);
    const size_t ancho_u = size_t(w) / 2, alto_u = size_t(h) / 2;
    // Filas reales de luma (H.264 redondea a 16): el bufer contiguo mide paso * filas * 3 / 2.
    const size_t filas = paso > 0 && cur >= size_t(paso) * h * 3 / 2 ? size_t(cur) * 2 / 3 / size_t(paso) : size_t(h);
    f.yuv.resize(size_t(w) * h + ancho_u * alto_u * 2);
    uint8_t* o = f.yuv.data();
    for (int y = 0; y < h; ++y, o += w) std::memcpy(o, d + ptrdiff_t(y) * paso, size_t(w));
    const uint8_t* uv = d + filas * size_t(paso);
    uint8_t* v = o + ancho_u * alto_u;
    for (size_t y = 0; y < alto_u; ++y) {
      const uint8_t* q = uv + y * size_t(paso);
      for (size_t x = 0; x < ancho_u; ++x) o[x] = q[2 * x], v[x] = q[2 * x + 1];
      o += ancho_u, v += ancho_u;
    }
    if (dos) b2->Unlock2D();
    else b->Unlock();
    Soltar(b2);
  }

  IMFSourceReader* reader_ = nullptr;
  IMFDXGIDeviceManager* mgr_ = nullptr;
  ID3D11Device* dev_ = nullptr;
  std::thread hilo_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Foto> cola_;
  bool fin_ = false;
};

struct Sesion {
  std::unique_ptr<Decoder> dec;
  std::string nombre;
  std::filesystem::path ruta;
  int w = 0, h = 0;
  double t = 0, t_hd = 0;   // ultimo instante del juego y del fotograma HD puesto
  uint32_t buf = 0;         // planos en memoria del juego
  uint32_t buf_tam = 0;
  int n = 0;                // fotogramas del .sfd vistos
  int subidos = 0, nuevos = 0, tarde = 0;
  double ms_subir = 0, ms_max = 0;
};
std::unique_ptr<Sesion> g_s;   // preparada al crear el reproductor, en marcha al empezar el video
bool g_en_marcha = false;
bool g_forzar_512 = false;

std::filesystem::path Carpeta() {
  std::filesystem::path p = dbz3::ModsRoot().parent_path() / "videos_hd";
  return p;
}
// Que video toca por el tamano que pide cada llamador (sub_821AF298 960x720; sub_821CB960 224x224). Ending01 no lo pide
// el juego: el ejecutable no nombra ese fichero y la version EU no lo trae; solo la prueba lo fuerza.
const char* Forzado() {
  static const char* e = std::getenv("DBZ3_TEST_VIDEOHD_E1");
  return e && *e == '1' ? "ending01" : nullptr;
}
const char* Elegir(uint32_t w, uint32_t h) {
  if (w == 960 && h == 720) return "opening";
  if (w == 224 && h == 224) return "ending00";
  if (w == 512 && h == 512 && g_forzar_512) return "ending01";
  return nullptr;
}
void Resumen(const Sesion& s) {
  const int hechos = s.dec->hechos.load();
  REXLOG_INFO("dbz3 videos HD: {} resumen: {} fotogramas del juego (s {:.2f}), {} HD nuevos (s {:.2f}), {} sin HD a tiempo; decodificar "
              "{:.1f} ms/f ({}), subir {:.1f} ms/f (max {:.1f})", s.nombre, s.n, s.t, s.nuevos, s.t_hd, s.tarde,
              hechos ? (s.dec->ms.load() / hechos) : 0.0, s.dec->gpu ? "GPU" : "CPU", s.subidos ? s.ms_subir / s.subidos : 0.0, s.ms_max);
}
void Parar() {
  if (!g_s) return;
  Sesion& s = *g_s;
  if (g_en_marcha) Resumen(s);
  s.dec->Cerrar();
  if (s.buf)
    if (auto* rt = rex::Runtime::instance()) rt->memory()->SystemHeapFree(s.buf);
  g_s.reset();
  g_en_marcha = false;
}

}  // namespace
}  // namespace dbz3::video::hd

// sub_821F9920 = mwPlyCreateSofdec, solo lo llama sub_82091DA8, que guarda en r31 los parametros (+16 ancho, +20 alto) y
// despues los lee para crear las texturas: al volver de aqui, Sofdec ya reservo lo suyo y solo crecen las texturas.
DBZ3_HOOK_EU(sub_821F9920) {
  using namespace dbz3::video::hd;
  orig(ctx, base);
  const uint32_t par = ctx.r31.u32;
  Parar();
  if (!ctx.r3.u32 || !dbz3::settings::VideosHd()) return;
  const char* nombre = Elegir(REX_LOAD_U32(par + 16), REX_LOAD_U32(par + 20));
  if (!nombre) return;
  const std::filesystem::path ruta = Carpeta() / (std::string(nombre) + ".mp4");
  std::error_code ec;
  if (!std::filesystem::is_regular_file(ruta, ec)) {
    REXLOG_INFO("dbz3 videos HD: sin {}: se ve el original", ruta.filename().string());
    return;
  }
  auto s = std::make_unique<Sesion>();
  s->dec = std::make_unique<Decoder>();
  if (!s->dec->Abrir(ruta)) {
    REXLOG_WARN("dbz3 videos HD: no se pudo abrir {}: se ve el original", ruta.string());
    return;
  }
  s->nombre = nombre;
  s->ruta = ruta;
  s->w = s->dec->w, s->h = s->dec->h;
  s->buf_tam = uint32_t(s->w * s->h + (s->w / 2) * (s->h / 2) * 2);
  s->buf = rex::Runtime::instance()->memory()->SystemHeapAlloc(s->buf_tam, 0x1000);
  if (!s->buf) {
    REXLOG_WARN("dbz3 videos HD: sin memoria del juego para {} B: se ve el original", s->buf_tam);
    return;
  }
  REX_STORE_U32(par + 16, uint32_t(s->w));
  REX_STORE_U32(par + 20, uint32_t(s->h));
  REXLOG_INFO("dbz3 videos HD: {}: texturas {}x{}, decodifica {}", nombre, s->w, s->h, s->dec->gpu ? "GPU" : "CPU");
  std::memset(base + s->buf, 0, s->w * s->h);
  std::memset(base + s->buf + s->w * s->h, 128, s->buf_tam - s->w * s->h);
  g_s = std::move(s);
}

// PRUEBA (DBZ3_TEST_VIDEOHD_E1=1): el reproductor de los creditos con Ending01.sfd, que el juego nunca pide (512x448).
DBZ3_HOOK_EU(sub_82091DA8) {
  using namespace dbz3::video::hd;
  if (Forzado() && REX_LOAD_U32(ctx.r3.u32 + 16) == 224) {
    g_forzar_512 = true;
    REX_STORE_U32(ctx.r3.u32 + 16, 512);
    REX_STORE_U32(ctx.r3.u32 + 20, 512);
  }
  orig(ctx, base);
}

// sub_820919D8 = empezar el video: arranca la decodificacion y espera un momento al primer fotograma.
DBZ3_HOOK_EU(sub_820919D8) {
  using namespace dbz3::video::hd;
  if (g_s) {
    if (g_en_marcha) {   // el mismo reproductor lo vuelve a empezar: desde el principio
      Resumen(*g_s);
      g_s->dec->Cerrar();
      auto d = std::make_unique<Decoder>();
      if (d->Abrir(g_s->ruta) && d->w == g_s->w && d->h == g_s->h) g_s->dec = std::move(d);
      else REXLOG_WARN("dbz3 videos HD: no se pudo reabrir {}", g_s->ruta.string());   // se queda el ultimo fotograma
    }
    g_en_marcha = true;
    g_s->dec->Empezar();
    g_s->dec->EsperarPrimero(400);
  }
  if (Forzado() && g_forzar_512) {
    static const uint32_t nombre = [] {
      const uint32_t a = rex::Runtime::instance()->memory()->SystemHeapAlloc(32, 16);
      std::memcpy(rex::Runtime::instance()->virtual_membase() + a, "/USR/ENDING01.SFD", 18);
      return a;
    }();
    ctx.r4.u64 = nombre;
    REXLOG_INFO("dbz3 videos HD: prueba: se abre Ending01.sfd");
  }
  orig(ctx, base);
}

// sub_820914D8 = servidor del reproductor, una vez por cuadro: copia el fotograma nuevo a las texturas del indice +44.
DBZ3_HOOK_EU(sub_820914D8) {
  using namespace dbz3::video::hd;
  orig(ctx, base);
  static const bool log = std::getenv("DBZ3_TEST_VIDEOHD_LOG") != nullptr;
  const uint32_t ply = kPly;
  if (log && REX_LOAD_U32(ply) && REX_LOAD_U32(ply + 104)) {
    static int nl = 0;
    if (nl++ % 90 < 3) {
      std::string d;
      for (int i = 0; i < 168; i += 4) d += fmt::format("{:x} ", REX_LOAD_U32(ply + 104 + i));
      REXLOG_INFO("dbz3 videohd: n={} idx={} frm: {}", nl, REX_LOAD_U32(ply + 44), d);
    }
  }
  if (!g_s || !g_en_marcha || !REX_LOAD_U32(ply) || !REX_LOAD_U32(ply + 104)) return;
  Sesion& s = *g_s;
  const int64_t t0 = dbz3::video::AhoraUs();
  // Instante del fotograma del .sfd: su numero entre sus fps (los dos los da el reproductor del juego).
  const uint32_t mfps = REX_LOAD_U32(ply + 104 + kFotoFps);
  const double t = double(REX_LOAD_U32(ply + 104 + kFotoNum)) * 1000.0 / (mfps ? mfps : 30000);
  ++s.n;
  s.t = t;
  Foto f;
  const uint32_t yn = uint32_t(s.w * s.h), un = uint32_t((s.w / 2) * (s.h / 2));
  if (s.dec->Tomar(t + 0.001, f) && f.yuv.size() == s.buf_tam) {
    std::memcpy(base + s.buf, f.yuv.data(), s.buf_tam);
    s.t_hd = f.t;
    ++s.nuevos;
  } else if (s.dec->PrimerT() > t + 0.1) {
    ++s.tarde;
  }
  // Por la misma funcion que el juego (vtable +12: r3 textura, r4 escalon, r5 filas, r6 datos): siempre, porque la copia del juego
  // acaba de pisar la esquina de esta textura.
  const uint32_t idx = REX_LOAD_U32(ply + 44);
  const struct { uint32_t tex, src; int filas, paso; } pl[3] = {
      {REX_LOAD_U32(ply + 8 + idx * 4), s.buf, s.h, s.w},
      {REX_LOAD_U32(ply + 20 + idx * 4), s.buf + yn, s.h / 2, s.w / 2},
      {REX_LOAD_U32(ply + 32 + idx * 4), s.buf + yn + un, s.h / 2, s.w / 2}};
  const uint64_t r3 = ctx.r3.u64;
  for (const auto& p : pl) {
    ctx.r3.u64 = p.tex;
    ctx.r4.s64 = p.paso;
    ctx.r5.s64 = p.filas;
    ctx.r6.u64 = p.src;
    const uint32_t fn = REX_LOAD_U32(REX_LOAD_U32(p.tex) + 12);
    REX_CALL_INDIRECT_FUNC(fn);
  }
  ctx.r3.u64 = r3;
  REX_STORE_U32(ply + 116, uint32_t(s.w));
  REX_STORE_U32(ply + 120, uint32_t(s.h));
  const double ms = double(dbz3::video::AhoraUs() - t0) / 1000;
  s.ms_subir += ms, s.ms_max = std::max(s.ms_max, ms);
  if (++s.subidos % 300 == 0) Resumen(s);
}

// sub_820920C8 = destruir el reproductor: suelta el decodificador y su memoria.
DBZ3_HOOK_EU(sub_820920C8) {
  dbz3::video::hd::Parar();
  orig(ctx, base);
}

// Creditos: sub_821CA658 pinta el circulo con coordenadas en texeles del video de 224 (sub_82085A70: r3 posicion, r5 UV).
// Con la textura HD solo se veria la esquina (cian): se escalan al tamano del HD.
namespace dbz3::video::hd { static float g_uv_escala = 0; }
DBZ3_HOOK_EU(sub_821CA658) {
  using namespace dbz3::video::hd;
  g_uv_escala = g_s && g_en_marcha ? float(g_s->w) / 224.0f : 0.0f;
  orig(ctx, base);
  g_uv_escala = 0;
}
// Pareja EU a mano: eu_map la ve ambigua (dos cuerpos iguales); es la que llama la pareja de sub_821CA658, las 5 veces.
REX_EXTERN(dbz3eu_sub_82085918);
REX_EXTERN(__imp__dbz3eu_sub_82085918);
DBZ3_HOOK(sub_82085A70, dbz3eu_sub_82085918) {
  const float e = dbz3::video::hd::g_uv_escala;
  if (e == 0) return orig(ctx, base);
  const uint32_t uv = ctx.r5.u32;
  const auto f = [&](uint32_t a) { PPCRegister r; r.u32 = REX_LOAD_U32(a); return r.f32; };
  const auto g = [&](uint32_t a, float v) { PPCRegister r; r.f32 = v; REX_STORE_U32(a, r.u32); };
  const float u = f(uv), v = f(uv + 4);
  g(uv, u * e);
  g(uv + 4, v * e);
  orig(ctx, base);
  g(uv, u);
  g(uv + 4, v);
}

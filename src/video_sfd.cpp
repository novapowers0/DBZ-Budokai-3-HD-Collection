// dbz3 - Reproductor de videos SFD de CRI Sofdec (frente R7). Ver video_sfd.h.
//
// Formato medido en los 22 videos de IW USA (E:\DBZ3HD_trabajo\R7\ps.py, adx.py): MPEG-1 PS (paquetes 0xBA de 12 B),
// 0xBF = cabecera "SofdecStream" con el nombre de cada stream (FD01b.m2v -> 0xE0, FD01b_jp.sfa -> 0xC0,
// FD01b_us.sfa -> 0xC1: nombre de 12 B + fecha de 12 B + id), video MPEG-2 512x448 (secuencia: aspecto 4:3), audio ADX
// tipo 3 de 4 bits, mono, 48 kHz (los dos streams llevan la misma musica: solo cambia la voz).
#include "video_sfd.h"

#include <rex/logging.h>
#include <rex/platform.h>

#if REX_PLATFORM_WIN32
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"
#include "video_sfd_channels.h"

namespace dbz3::video {
namespace {

constexpr size_t kColaMax = 30;     // fotogramas RGBA decodificados por delante (~1 s, 27 MB)
constexpr size_t kColaArranque = 12;   // empieza a sonar con 0,4 s de video listo
constexpr float kAltoPs2 = 448.0f;  // alto de la pantalla de PS2 para la que estan hechos los subtitulos

int64_t AhoraUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
uint32_t BE16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }
uint32_t BE32(const uint8_t* p) { return BE16(p) << 16 | BE16(p + 2); }

struct Fotograma {
  int n = 0;
  std::vector<uint8_t> rgba;
};
struct Bloque {
  WAVEHDR h = {};
  std::vector<int16_t> pcm;
};

struct Linea {
  double desde = 0, hasta = 0;
  int imagen = -1;
};
// Bloque CRITAG de los subtitulos (canal `canal`): clave de 8 B + longitud en 7 cifras hex + NUL + valor. Las claves
// contenedor (CRITAGS, USRINFS, CHDAT, TIMEDAT) se recorren por dentro; las demas se saltan por su longitud.
std::vector<Linea> LeerCritag(const std::string& bf, int canal) {
  std::vector<Linea> out;
  int ch = -1;
  double unidad = 1.0 / 30;
  for (size_t i = bf.find("CRITAGS"); i != std::string::npos && i + 16 <= bf.size();) {
    const std::string clave = bf.substr(i, 8).c_str();
    char* fin = nullptr;
    const std::string lon = bf.substr(i + 8, 7);
    const size_t L = std::strtoul(lon.c_str(), &fin, 16);
    if (fin != lon.c_str() + 7) break;
    const std::string valor = bf.substr(i + 16, L).c_str();
    if (clave == "CRITAGS" || clave == "USRINFS" || clave == "CHDAT" || clave == "TIMEDAT") {
      if (clave == "TIMEDAT" && ch == canal) out.push_back({});
      i += 16;
      continue;
    }
    i += 16 + L;
    const int v = std::atoi(valor.c_str());
    if (clave == "CHNO") ch = v;
    if (ch != canal) continue;
    if (clave == "TUNIT" && v > 0) unidad = 1.0 / v;
    if (out.empty()) continue;
    if (clave == "INTIME") out.back().desde = v * unidad;
    if (clave == "DURTIME") out.back().hasta = out.back().desde + v * unidad;
    if (clave == "CONTENT") out.back().imagen = v;
  }
  return out;
}

struct Sesion {
  std::filesystem::path ruta;
  std::vector<pasarela::Imagen> imagenes;
  std::vector<Linea> lineas;   // del CRITAG (las escribe el hilo de trabajo antes del primer fotograma)
  int canal_subtitulos = 2;
  bool japones = false;        // voces en japones (stream "_jp.sfa"); si no, en ingles ("_us.sfa")
  std::atomic<bool> parar{false}, leido{false};
  std::mutex mu;   // todo lo de abajo
  std::deque<Fotograma> cola;
  int w = 0, h = 0;
  double fps = 30000.0 / 1001, duracion = 0;
  float aspecto = 4.0f / 3;
  HWAVEOUT wo = nullptr;
  int hz = 0, canales = 0;
  uint64_t enviadas = 0;          // muestras (por canal) entregadas a waveOut
  bool sonando = false, reloj_audio = false;
  int64_t t0_us = 0, t_fin_us = 0;
  std::deque<std::unique_ptr<Bloque>> bloques;

  // segundos de video que tocan ahora (con s.mu): el del audio si suena, si no el de pared
  double Reloj() {
    if (!sonando) return 0;
    if (reloj_audio && wo && hz > 0) {
      MMTIME mt = {};
      mt.wType = TIME_SAMPLES;
      if (waveOutGetPosition(wo, &mt, sizeof(mt)) == MMSYSERR_NOERROR && mt.wType == TIME_SAMPLES) {
        const double pos = double(mt.u.sample) / hz;
        if (!leido || mt.u.sample + 64 < enviadas) return pos;
        if (!t_fin_us) t_fin_us = AhoraUs();   // audio acabado: sigue el reloj de pared
        return pos + double(AhoraUs() - t_fin_us) / 1e6;
      }
    }
    return double(AhoraUs() - t0_us) / 1e6;
  }
  void Recoger() {   // con s.mu: suelta los bloques ya sonados
    while (!bloques.empty() && (bloques.front()->h.dwFlags & WHDR_DONE)) {
      waveOutUnprepareHeader(wo, &bloques.front()->h, sizeof(WAVEHDR));
      bloques.pop_front();
    }
  }
  void CerrarAudio() {   // con s.mu
    if (!wo) return;
    waveOutReset(wo);
    for (auto& b : bloques) waveOutUnprepareHeader(wo, &b->h, sizeof(WAVEHDR));
    bloques.clear();
    waveOutClose(wo);
    wo = nullptr;
  }
};

std::mutex g_mu;
std::shared_ptr<Sesion> g_actual;
std::shared_ptr<Sesion> Actual() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_actual;
}
// Mientras hay video el juego lee un mando neutro (bloqueo de la interfaz del runtime, InputSystem::AddUIInputBlocker:
// lo que siga pulsado al quitarlo, como la A que salta, queda tapado hasta soltarlo) y su mezcla calla (`audio_gain`,
// la ganancia que el driver SDL del SDK aplica a toda la mezcla del juego, sdl_audio_driver.cpp; al acabar vuelve al
// volumen general, el mismo valor que pone settings::ApplyRuntimeSettingsToSdk).
void Silenciar(bool si) {
  if (auto* rt = rex::Runtime::instance())
    if (auto* in = static_cast<rex::input::InputSystem*>(rt->input_system()))
      si ? in->AddUIInputBlocker() : in->RemoveUIInputBlocker();
  const double vol = si ? 0.0 : std::clamp(dbz3::settings::MasterVolume(), 0.0, 1.0);
  rex::cvar::SetFlagByName("audio_gain", std::to_string(vol));
  REXLOG_INFO("dbz3 video: mando y sonido del juego {} (audio_gain {:.2f})", si ? "parados" : "devueltos", vol);
}
// Idioma de las voces del juego (Opciones de B3): byte G + 0x2C0C9 (US 0x824E61D9; G = 0x824BA110, bloque de menus;
// EU por dbz3::GA). 0 = japones (adx_jpn + lang_jpn), 1 = ingles. Lo escribe sub_8218D280 (de mn_option.cpp: elige en
// su tabla /usr/adx_jpn.afs 0x82020E04, /usr/adx_usa.afs 0x82020DF0, /usr/lang_jpn.afs, /usr/lang_usa.afs; lo que no
// sea 0 o 1 lo toma como 1) y lo aplica al arrancar sub_8218F8B8. Medido en el codigo recompilado (R7, 2026-10-16).
std::atomic<int> g_forzar_japones{-1};   // prueba: DBZ3_TEST_VIDEO=<video>,jp
int ByteVoces() {
  rex::Runtime* rt = rex::Runtime::instance();
  uint8_t* base = rt ? rt->virtual_membase() : nullptr;
  return base ? int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9)) : -1;
}
bool VocesJaponesas() {
  const int f = g_forzar_japones.load();
  return f >= 0 ? f == 1 : ByteVoces() == 0;
}
void Terminar(const std::shared_ptr<Sesion>& s, const char* por) {
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_actual != s) return;
    g_actual.reset();
  }
  s->parar = true;   // el hilo de trabajo cierra el audio y suelta FFmpeg
  REXLOG_INFO("dbz3 video: {} {}", s->ruta.filename().string(), por);
  Silenciar(false);
}

// ---- ADX (CRI, como pasarela_b1.cpp Adx, pero por trozos e intercalando canales) ----
struct Adx {
  std::vector<uint8_t> b;
  size_t p = 0;
  bool cabecera = false, fin = false;
  int hz = 0, ch = 0, bs = 0, c1 = 0, c2 = 0, hist[2][2] = {};
  uint64_t total = 0, hechas = 0;
  // añade datos del stream y devuelve las muestras nuevas (intercaladas)
  std::vector<int16_t> Meter(const uint8_t* d, size_t n) {
    std::vector<int16_t> out;
    if (fin) return out;
    b.insert(b.end(), d, d + n);
    if (!cabecera) {
      if (b.size() < 4) return out;
      const size_t off = BE16(&b[2]) + 4;
      if (b.size() < std::max<size_t>(off, 20)) return out;
      bs = b[5], ch = b[7], hz = int(BE32(&b[8]));
      total = BE32(&b[12]);
      const uint32_t cut = BE16(&b[16]);
      if (b[0] != 0x80 || hz <= 0 || ch < 1 || ch > 2 || bs < 3) {
        fin = true;
        return out;
      }
      const double z = std::cos(2 * 3.14159265358979 * cut / hz), a = std::sqrt(2.0) - z, bb = std::sqrt(2.0) - 1;
      const double c = (a - std::sqrt((a + bb) * (a - bb))) / bb;
      c1 = int(std::floor(c * 8192)), c2 = int(std::floor(c * c * -4096));
      p = off;
      cabecera = true;
    }
    const size_t spb = size_t(bs - 2) * 2;
    while (!fin && p + size_t(bs * ch) <= b.size()) {
      const size_t base = out.size();
      out.resize(base + spb * ch);
      for (int k = 0; k < ch; ++k) {
        const uint8_t* fr = &b[p + size_t(k * bs)];
        if (fr[0] & 0x80) {   // fin de stream (0x8001 + "(c)CRI")
          fin = true;
          break;
        }
        const int scale = int(BE16(fr)) + 1;
        int& h1 = hist[k][0];
        int& h2 = hist[k][1];
        for (size_t i = 0; i < spb; ++i) {
          int nib = (i & 1) ? fr[2 + i / 2] & 15 : fr[2 + i / 2] >> 4;
          if (nib >= 8) nib -= 16;
          const int v = std::clamp(nib * scale + ((c1 * h1 + c2 * h2) >> 12), -32768, 32767);
          out[base + i * ch + size_t(k)] = int16_t(v);
          h2 = h1;
          h1 = v;
        }
      }
      if (fin) {
        out.resize(base);
        break;
      }
      p += size_t(bs * ch);
      hechas += spb;
      if (total && hechas >= total) {   // la cuenta de la cabecera manda sobre el relleno del ultimo bloque
        out.resize(out.size() - size_t(hechas - total) * ch);
        hechas = total;
        fin = true;
      }
    }
    if (p > (1u << 16)) {
      b.erase(b.begin(), b.begin() + std::ptrdiff_t(p));
      p = 0;
    }
    return out;
  }
};

// Entrega PCM a waveOut con el volumen del juego del momento (silencio = ceros; el reloj sigue igual).
void Enviar(Sesion& s, std::vector<int16_t>& pcm) {
  if (pcm.empty()) return;
  const float vol = dbz3::settings::AudioMute() ? 0.0f : std::clamp(float(dbz3::settings::MasterVolume()), 0.0f, 1.0f);
  if (vol < 0.999f)
    for (int16_t& x : pcm) x = int16_t(float(x) * vol);
  auto b = std::make_unique<Bloque>();
  b->pcm = std::move(pcm);
  pcm.clear();
  std::lock_guard<std::mutex> lk(s.mu);
  if (!s.wo) return;
  b->h.lpData = reinterpret_cast<LPSTR>(b->pcm.data());
  b->h.dwBufferLength = DWORD(b->pcm.size() * 2);
  if (waveOutPrepareHeader(s.wo, &b->h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return;
  if (waveOutWrite(s.wo, &b->h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
    waveOutUnprepareHeader(s.wo, &b->h, sizeof(WAVEHDR));
    return;
  }
  s.enviadas += b->pcm.size() / size_t(std::max(s.canales, 1));
  s.bloques.push_back(std::move(b));
  s.Recoger();
}

// YUV 4:2:0 (BT.601, rango limitado) -> RGBA
void ARgba(const AVFrame* f, std::vector<uint8_t>& out) {
  const int w = f->width, h = f->height;
  out.resize(size_t(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    const uint8_t* py = f->data[0] + ptrdiff_t(y) * f->linesize[0];
    const uint8_t* pu = f->data[1] + ptrdiff_t(y >> 1) * f->linesize[1];
    const uint8_t* pv = f->data[2] + ptrdiff_t(y >> 1) * f->linesize[2];
    uint8_t* o = &out[size_t(y) * w * 4];
    for (int x = 0; x < w; ++x, o += 4) {
      const int c = (py[x] - 16) * 298, d = pu[x >> 1] - 128, e = pv[x >> 1] - 128;
      o[0] = uint8_t(std::clamp((c + 409 * e + 128) >> 8, 0, 255));
      o[1] = uint8_t(std::clamp((c - 100 * d - 208 * e + 128) >> 8, 0, 255));
      o[2] = uint8_t(std::clamp((c + 516 * d + 128) >> 8, 0, 255));
      o[3] = 255;
    }
  }
}

// ---- Hilo de trabajo: lee el SFD por trozos, separa los streams, decodifica y llena la cola y waveOut ----
void Trabajar(std::shared_ptr<Sesion> s) {
  const int64_t t_ini = AhoraUs();
  std::ifstream f(s->ruta, std::ios::binary);
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
  AVCodecContext* cx = codec ? avcodec_alloc_context3(codec) : nullptr;
  AVCodecParserContext* pc = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
  AVPacket* pkt = av_packet_alloc();
  AVFrame* fr = av_frame_alloc();
  if (cx) cx->thread_count = 1;
  const bool ok = f && cx && pc && pkt && fr && avcodec_open2(cx, codec, nullptr) >= 0;
  if (!ok) REXLOG_WARN("dbz3 video: no se puede abrir {} (FFmpeg {})", s->ruta.string(), codec ? "ok" : "sin MPEG-2");
  int ingles = -1, audio = -1, fotogramas = 0;   // ingles = id del stream del idioma pedido
  double ms_suma = 0, ms_max = 0;
  Adx adx;
  std::vector<int16_t> pend;
  std::vector<uint8_t> es;
  std::string bf;   // paquetes 0xBF (cabecera Sofdec + CRITAG; todos al principio del archivo)
  bool critag = false;

  auto sacar = [&] {   // fotogramas que tenga el decodificador
    while (avcodec_receive_frame(cx, fr) == 0) {
      Fotograma fo;
      fo.n = fotogramas++;
      ARgba(fr, fo.rgba);
      std::lock_guard<std::mutex> lk(s->mu);
      if (!s->w) {
        s->w = fr->width, s->h = fr->height;
        if (cx->framerate.num > 0 && cx->framerate.den > 0) s->fps = av_q2d(cx->framerate);
        const AVRational sar = fr->sample_aspect_ratio;
        if (sar.num > 0 && sar.den > 0) s->aspecto = float(fr->width * av_q2d(sar) / fr->height);
      }
      s->cola.push_back(std::move(fo));
      av_frame_unref(fr);
    }
  };
  auto video = [&](const uint8_t* d, size_t n) {
    const int64_t t0 = AhoraUs();
    const int antes = fotogramas;
    es.assign(d, d + n);
    es.resize(n + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    const uint8_t* q = es.data();
    int resto = int(n);
    while (resto > 0 || !d) {
      uint8_t* out = nullptr;
      int out_n = 0;
      const int usado = av_parser_parse2(pc, cx, &out, &out_n, d ? q : nullptr, d ? resto : 0, AV_NOPTS_VALUE,
                                         AV_NOPTS_VALUE, 0);
      if (usado < 0) break;
      q += usado, resto -= usado;
      if (out_n > 0) {
        pkt->data = out;
        pkt->size = out_n;
        if (avcodec_send_packet(cx, pkt) >= 0) sacar();
      }
      if (!d) break;
    }
    if (fotogramas > antes) {
      const double ms = double(AhoraUs() - t0) / 1000 / (fotogramas - antes);
      ms_suma += ms * (fotogramas - antes);
      ms_max = std::max(ms_max, ms);
    }
  };
  auto sonido = [&](int sid, const uint8_t* d, size_t n) {
    if (audio < 0) audio = ingles >= 0 ? ingles : sid;
    if (sid != audio) return;
    const bool tenia = adx.cabecera;
    std::vector<int16_t> nuevas = adx.Meter(d, n);
    if (!tenia && adx.cabecera) {   // abre la salida (en pausa hasta que haya video listo)
      WAVEFORMATEX fmt = {};
      fmt.wFormatTag = WAVE_FORMAT_PCM;
      fmt.nChannels = WORD(adx.ch);
      fmt.nSamplesPerSec = DWORD(adx.hz);
      fmt.wBitsPerSample = 16;
      fmt.nBlockAlign = WORD(2 * adx.ch);
      fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
      std::lock_guard<std::mutex> lk(s->mu);
      if (waveOutOpen(&s->wo, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        s->hz = adx.hz, s->canales = adx.ch;
        s->reloj_audio = !s->sonando;
        if (!s->sonando) waveOutPause(s->wo);
      } else {
        s->wo = nullptr;
      }
    }
    pend.insert(pend.end(), nuevas.begin(), nuevas.end());
    if (adx.hz && pend.size() >= size_t(adx.hz * adx.ch / 10)) Enviar(*s, pend);   // trozos de 0,1 s
  };
  auto arrancar = [&](bool ya) {
    std::lock_guard<std::mutex> lk(s->mu);
    if (s->sonando || (!ya && s->cola.size() < kColaArranque)) return;
    s->sonando = true;
    s->t0_us = AhoraUs();
    if (s->wo) waveOutRestart(s->wo);
  };

  // Lectura y decodificacion por separado: el video comprimido espera en `es_cola` y se decodifica solo con hueco en la
  // cola de fotogramas (~1 s); la lectura sigue mientras el audio vaya corto (el reloj es el audio: si se queda sin
  // datos, el video se para).
  std::vector<uint8_t> buf;
  size_t p = 0, es_bytes = 0;
  std::deque<std::vector<uint8_t>> es_cola;
  bool fin = !ok, eof = !ok;
  auto paquete = [&]() -> bool {   // lee y reparte un paquete; false al acabar el archivo
    for (;;) {
      if (!fin && buf.size() - p < (256u << 10)) {   // rellena (paquetes de 2 KB en un SFD, como mucho 64 KB)
        buf.erase(buf.begin(), buf.begin() + std::ptrdiff_t(p));
        p = 0;
        const size_t viejo = buf.size();
        buf.resize(viejo + (1u << 20));
        f.read(reinterpret_cast<char*>(buf.data() + viejo), 1 << 20);
        buf.resize(viejo + size_t(f.gcount()));
        fin = f.gcount() == 0;
      }
      const size_t n = buf.size();
      if (p + 6 > n) return false;
      const uint8_t* b = &buf[p];
      if (b[0] != 0 || b[1] != 0 || b[2] != 1) {
        ++p;   // resincroniza
        continue;
      }
      const uint8_t sid = b[3];
      if (sid == 0xB9) return false;   // fin del programa
      if (sid == 0xBA) {               // paquete: MPEG-1 12 B, MPEG-2 14 B + relleno
        p += (b[4] >> 6) == 1 ? (p + 14 <= n ? 14 + (b[13] & 7) : n - p) : 12;
        continue;
      }
      const size_t L = BE16(b + 4);
      if (p + 6 + L > n) {
        if (fin) return false;
        continue;
      }
      p += 6 + L;
      const uint8_t* d = b + 6;
      if (sid == 0xBF) {   // cabecera Sofdec: el stream del idioma ("..._us.sfa"/"_jp.sfa" + fecha de 12 B + id)
        const std::string_view t(reinterpret_cast<const char*>(d), L);
        const char* idioma = s->japones ? "_jp.sfa" : "_us.sfa";
        for (size_t k = t.find(idioma); k != std::string_view::npos && ingles < 0; k = t.find(idioma, k + 1))
          if (k + 7 + 12 < L && (d[k + 19] & 0xE0) == 0xC0) ingles = d[k + 19];
        if (!critag) bf.append(t);
        return true;
      }
      if (sid < 0xC0 || sid > 0xEF) return true;   // sistema, relleno...
      if (!critag) {   // primer paquete de audio o video: el CRITAG ya esta entero
        critag = true;
        std::vector<Linea> v = LeerCritag(bf, s->canal_subtitulos);
        // Los SFD USA pueden no llevar los canales europeos 3..6: si el idioma
        // elegido no aparece en este vídeo, conservar el subtítulo inglés del canal 2.
        if (v.empty() && s->canal_subtitulos != 2) {
          v = LeerCritag(bf, 2);
          if (!v.empty()) s->canal_subtitulos = 2;
        }
        REXLOG_INFO("dbz3 video: {}: {} lineas de subtitulo en el CRITAG (canal {}), {} imagenes",
                    s->ruta.filename().string(), v.size(), s->canal_subtitulos, s->imagenes.size());
        std::lock_guard<std::mutex> lk(s->mu);
        s->lineas = std::move(v);
        bf.clear();
      }
      size_t k = 0;   // cabecera PES
      if (L >= 3 && (d[0] & 0xC0) == 0x80) {
        k = 3 + size_t(d[2]);   // MPEG-2
      } else {
        while (k < L && d[k] == 0xFF) ++k;
        if (k < L && (d[k] >> 6) == 1) k += 2;
        if (k < L) k += (d[k] >> 4) == 2 ? 5 : (d[k] >> 4) == 3 ? 10 : d[k] == 0x0F ? 1 : 0;
      }
      if (k >= L) return true;
      if (sid >= 0xE0) {
        es_cola.emplace_back(d + k, d + L);
        es_bytes += L - k;
      } else {
        sonido(sid, d + k, L - k);
      }
      return true;
    }
  };
  while (ok && !s->parar) {
    size_t cola = 0;
    double adelanto = 0;   // s de audio entregado por delante del reloj
    bool con_audio = false;
    {
      std::lock_guard<std::mutex> lk(s->mu);
      s->Recoger();
      cola = s->cola.size();
      con_audio = s->wo && s->hz > 0;
      if (con_audio) adelanto = double(s->enviadas) / s->hz - s->Reloj();
    }
    if (!es_cola.empty() && cola < kColaMax) {
      video(es_cola.front().data(), es_cola.front().size());
      es_bytes -= es_cola.front().size();
      es_cola.pop_front();
      arrancar(false);
    } else if (!eof && (es_bytes < (2u << 20) || (con_audio && adelanto < 1.5))) {
      eof = !paquete();
    } else if (eof && es_cola.empty()) {
      break;
    } else {
      arrancar(eof);   // (cola llena sin sonar todavia: arranca)
      std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
  }
  if (ok && !s->parar) {
    video(nullptr, 0);   // lo que quede en el parser
    if (avcodec_send_packet(cx, nullptr) >= 0) sacar();
    Enviar(*s, pend);
  }
  FILETIME c0, c1, k0, u0;
  GetThreadTimes(GetCurrentThread(), &c0, &c1, &k0, &u0);
  const double cpu = (ULARGE_INTEGER{{k0.dwLowDateTime, k0.dwHighDateTime}}.QuadPart +
                      ULARGE_INTEGER{{u0.dwLowDateTime, u0.dwHighDateTime}}.QuadPart) / 1e7;
  {
    std::lock_guard<std::mutex> lk(s->mu);
    s->duracion = std::max(fotogramas / s->fps, s->hz ? double(s->enviadas) / s->hz : 0.0);
  }
  arrancar(true);
  s->leido = true;
  if (ok)
    REXLOG_INFO("dbz3 video: {} leido: {} fotogramas {}x{} a {:.3f} fps ({:.1f} s), audio {} {} Hz x{} ({:.1f} s); "
                "decodificar+RGBA {:.2f} ms/fotograma (max {:.1f}), CPU del hilo {:.2f} s",
                s->ruta.filename().string(), fotogramas, s->w, s->h, s->fps, fotogramas / s->fps,
                audio == ingles && ingles >= 0 ? (s->japones ? "japones" : "ingles") : audio >= 0 ? "unico" : "sin",
                adx.hz, adx.ch,
                adx.hz ? double(adx.hechas) / adx.hz : 0.0, fotogramas ? ms_suma / fotogramas : 0.0, ms_max, cpu);
  av_frame_free(&fr);
  av_packet_free(&pkt);
  if (pc) av_parser_close(pc);
  avcodec_free_context(&cx);
  while (!s->parar) {   // que acabe de sonar (o lo salten) antes de cerrar la salida
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::lock_guard<std::mutex> lk(s->mu);
    s->Recoger();
    if (!s->wo || s->bloques.empty()) break;
  }
  std::lock_guard<std::mutex> lk(s->mu);
  s->CerrarAudio();
  REXLOG_INFO("dbz3 video: {} tiempo total {:.1f} s", s->ruta.filename().string(), double(AhoraUs() - t_ini) / 1e6);
}

// ---- Panel (hilo de la interfaz): video a pantalla completa, subtitulo y salto ----
class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const std::shared_ptr<Sesion> s = Actual();
    if (s.get() != sesion_) {
      sesion_ = s.get();
      for (auto& t : tex_) t.reset();
      sub_tex_.clear();
      antes_ = true;   // un boton que ya estaba pulsado no salta
      n_tex_ = 0, ms_tex_ = 0, ms_tex_max_ = 0;
      if (s) {   // al final de la lista de dialogos: encima de todo (el dibujante lo vuelve a llamar este fotograma)
        imgui_drawer()->RemoveDialog(this);
        imgui_drawer()->AddDialog(this);
        return;
      }
    }
    if (!s) return;
    if (auto* rt = rex::Runtime::instance())
      if (auto* in = static_cast<rex::input::InputSystem*>(rt->input_system())) {
        rex::input::X_INPUT_STATE st = {};
        const bool b = in->GetStateForUI(0, &st) == 0 && (st.gamepad.buttons & (0x1000 | 0x0010));   // A, START
        if (b && !antes_) {
          Terminar(s, "saltado");
          return;
        }
        antes_ = b;
      }
    Fotograma fo;
    bool nuevo = false, acabado = false;
    double t = 0, fps = 30;
    float aspecto = 4.0f / 3;
    {
      std::lock_guard<std::mutex> lk(s->mu);
      t = s->Reloj();
      fps = s->fps;
      aspecto = s->aspecto;
      while (!s->cola.empty() && s->cola.front().n / fps <= t) {
        fo = std::move(s->cola.front());
        s->cola.pop_front();
        nuevo = true;
      }
      acabado = s->leido && s->cola.empty() && t >= s->duracion;
    }
    if (nuevo && fo.rgba.size() >= 4)
      if (auto* dr = dbz3::launcher::PreviewDrawer()) {
        const int64_t t0 = AhoraUs();
        const int w = s->w, h = int(fo.rgba.size() / 4 / size_t(std::max(w, 1)));
        uno_ = (uno_ + 1) % 3;   // tres texturas en rueda: la anterior puede seguir en la GPU
        tex_[uno_] = dr->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false,
                                       fo.rgba.data());
        const double ms = double(AhoraUs() - t0) / 1000;
        if (!n_tex_) REXLOG_INFO("dbz3 video: primer fotograma en pantalla (reloj {:.2f} s)", t);
        ++n_tex_, ms_tex_ += ms, ms_tex_max_ = std::max(ms_tex_max_, ms);
      }
    if (acabado) {
      REXLOG_INFO("dbz3 video: subir textura {:.2f} ms/fotograma (max {:.1f}, {} texturas)",
                  n_tex_ ? ms_tex_ / n_tex_ : 0.0, ms_tex_max_, n_tex_);
      Terminar(s, "acabado");
      return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 255));
    float vw = H * aspecto, vh = H;
    if (vw > W) vw = W, vh = W / aspecto;
    const ImVec2 p0((W - vw) * 0.5f, (H - vh) * 0.5f);
    if (tex_[uno_]) dl->AddImage(reinterpret_cast<ImTextureID>(tex_[uno_].get()), p0, ImVec2(p0.x + vw, p0.y + vh));
    // subtitulos: las imagenes del original a la escala de la pantalla de PS2, centradas abajo (como IW); si se
    // solapan (FD12) se apilan hacia arriba. ponytail: margen inferior medido a ojo (24 px de PS2).
    float y = p0.y + vh - 24 * (vh / kAltoPs2);
    for (const Linea& l : s->lineas) {
      if (t < l.desde || t >= l.hasta || l.imagen < 0 || l.imagen >= int(s->imagenes.size())) continue;
      const pasarela::Imagen& im = s->imagenes[size_t(l.imagen)];
      if (!im) continue;
      auto& tx = sub_tex_[l.imagen];
      if (!tx)
        if (auto* dr = dbz3::launcher::PreviewDrawer())
          tx = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false,
                                 im.rgba.data());
      if (!tx) continue;
      const float e = vh / kAltoPs2, sw = im.w * e, sh = im.h * e;
      y -= sh;
      dl->AddImage(reinterpret_cast<ImTextureID>(tx.get()), ImVec2(p0.x + (vw - sw) * 0.5f, y),
                   ImVec2(p0.x + (vw + sw) * 0.5f, y + sh));
    }
  }

 private:
  const Sesion* sesion_ = nullptr;
  std::unique_ptr<rex::ui::ImmediateTexture> tex_[3];
  std::map<int, std::unique_ptr<rex::ui::ImmediateTexture>> sub_tex_;   // por imagen, mientras dura el video
  int uno_ = 0, n_tex_ = 0;
  double ms_tex_ = 0, ms_tex_max_ = 0;
  bool antes_ = true;
};
void CrearPanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

// Nombres de los videos de IW por indice: los de la tabla del ELF USA 0x4811B0 (0x100 B cada uno; los mismos indices
// que las tablas de escenas, EscenaIW::video) + ENDING.SFD, que no esta en ella (lo pone el final del juego).
constexpr const char* kVideosIW[kFinalIW + 1] = {"FD01A", "FD01B", "FD02", "FD03", "FD04", "FD05", "FD06", "FD07",
                                                 "FD08A", "FD08B", "FD09", "FD10", "FD11", "FD12", "FD13", "FD14",
                                                 "FD15", "FD16", "FD17", "FD18", "FD19", "FD20", "ENDING"};

// Prueba (tools/pruebas.py _r7_video): con DBZ3_TEST_VIDEO=FD01B, cuando aparece dbz3_video.req en la carpeta del juego
// (la escribe la prueba ya en el menu; se renombra a .usado) reproduce ese video de la copia de IW (USR/<nombre>.SFD)
// con sus subtitulos (DATA_USA 77 + video), como lo pediria R1.
struct Prueba {
  Prueba() {
    const char* v = std::getenv("DBZ3_TEST_VIDEO");
    if (!v || !*v) return;
    std::string arg = v;
    std::thread([arg] {
      const std::string nombre = arg.substr(0, arg.find(','));
      if (arg.find(",jp") != std::string::npos) g_forzar_japones = 1;
      for (int i = 0; i < 600; ++i) {   // hasta 5 min a que haya interfaz
        auto* rt = rex::Runtime::instance();
        if (rt && rt->imgui_drawer() && dbz3::launcher::PreviewDrawer()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
      std::error_code ec;
      while (!std::filesystem::exists("dbz3_video.req", ec))
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      std::filesystem::rename("dbz3_video.req", "dbz3_video.req.usado", ec);
      int v = kFinalIW;
      while (v >= 0 && nombre != kVideosIW[v]) --v;
      REXLOG_INFO("dbz3 video: prueba {} (video {})", nombre, v);
      ReproducirIW(v);
    }).detach();
  }
} g_prueba;

}  // namespace

bool Reproducir(const std::filesystem::path& ruta, std::vector<pasarela::Imagen> subtitulos, int canal_subtitulos) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(ruta, ec)) {
    REXLOG_WARN("dbz3 video: no existe {}", ruta.string());
    return false;
  }
  auto s = std::make_shared<Sesion>();
  s->ruta = ruta;
  s->imagenes = std::move(subtitulos);
  s->canal_subtitulos = canal_subtitulos;
  s->japones = VocesJaponesas();
  std::shared_ptr<Sesion> vieja;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    vieja = std::move(g_actual);
    g_actual = s;
  }
  if (vieja) vieja->parar = true;
  else Silenciar(true);
  static std::atomic<bool> panel{false};
  if (!panel.exchange(true)) dbz3::launcher::DeferUi(&CrearPanel);
  REXLOG_INFO("dbz3 video: reproducir {} ({} imagenes de subtitulo, voces {}: opcion del juego {})", ruta.string(),
              s->imagenes.size(), s->japones ? "japonesas" : "inglesas", ByteVoces());
  std::thread(Trabajar, s).detach();
  return true;
}
bool Activo() { return Actual() != nullptr; }
void Saltar() {
  if (auto s = Actual()) Terminar(s, "saltado");
}
bool ReproducirIW(int video) {
  if (video < 0 || video > kFinalIW) return false;
  const std::filesystem::path iw = pasarela::BuscarIW();
  std::vector<pasarela::Imagen> subs;
  if (video < kFinalIW) {   // subtitulos: DATA_USA 77 + video (tabla del ELF USA 0x4827B0)
    pasarela::AfsPs2 usa;
    if (usa.Abrir(iw, "DATA_USA.AFS")) {
      const std::vector<uint8_t> amt = usa.Entrada(size_t(77 + video));
      for (int j = 0, n = pasarela::CuantasAmt(amt); j < n; ++j) subs.push_back(pasarela::TexturaAmt(amt, j));
    }
  }
  const int canal = CanalSubtitulosIW(dbz3::settings::Language());
  return Reproducir(iw / "USR" / (std::string(kVideosIW[video]) + ".SFD"), std::move(subs), canal);
}

}  // namespace dbz3::video

#else   // sin Windows: no hay reproductor (el modo sigue sin video)
namespace dbz3::video {
bool Reproducir(const std::filesystem::path&, std::vector<pasarela::Imagen>, int) { return false; }
bool Activo() { return false; }
void Saltar() {}
bool ReproducirIW(int) { return false; }
}  // namespace dbz3::video
#endif

// TRAD/3 (13/11): el BUCLE PRINCIPAL del IW original (FUN_001498d0) dentro del juego, con el hardware minimo del oraculo
// (tools/iw_mips/oraculo.py + E:\DBZ3HD_trabajo\iwfin\p3\cuadro_min.py, que da las mismas ordenes que el oraculo entero):
//  - una vuelta del cuerpo del bucle por llamada (el salto atras 0x149944 se cambia por un retorno);
//  - syscalls sin nucleo de hilos: los semaforos no bloquean; SleepThread = tick: fines de DMA pendientes (manejadores
//    DMAC) y despues un vblank (manejadores INTC causa 2), hasta que pasa uno;
//  - registros de hardware como Hw del oraculo; DMA 8/9 (scratchpad) copia de verdad; DMA 1/2 -> traductor GS;
//  - libpad: un DualShock 2 en el puerto 0 con los botones que da el juego; cache: sin efecto; sin CD ni IOP.
// Estado de partida: un punto del oraculo (RAM + manejadores + registros), de momento dentro de 0x7D5 (m7d5).
// ponytail: el punto viene de un archivo de pruebas (DBZ3_IWTRAD_PUNTO); arrancar desde el ELF necesita CD/IOP en C++.
// Todo tras dbz3_iw_traducido (de serie apagado).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/presenter.h>

#include "iw_anim_intro7db.h"
#include "iw_gs_traductor.h"
#include "iw_minijuegos.h"
#include "iw_mips.h"
#include "iw_trad_trabajador.h"
#include "pasarela_iw.h"
#include "pasarela.h"
#include "iw_gs.h"
#include "ps2_modelo.h"

REXCVAR_DECLARE(bool, dbz3_iw_r23_eventos);   // IW-06: definido en iw_mini_disparo.cpp

namespace dbz3::iwtrad {
namespace {
using iwmips::Ee;
constexpr uint32_t kGpEu = 0x4A3DF0, kCuerpo = 0x1498E0, kIntcStat = 0x1000F000;
constexpr size_t kElfEu = 3786800, kElfUsa = 3784752;   // SLES_553.47 / SLUS_218.42

// R23 IW-07: el ELF arrancado es el USA (SLUS_218.42). El codigo por debajo de 0x1FC6A4 (bucle, nucleo, bibliotecas) es
// identico; lo demas se mueve por tramos. Pares verificados en E:/DBZ3HD_trabajo/iw_eu_usa_tabla.md (instrucciones
// emparejadas y bytes); solo las direcciones EU que lee o vigila la ruta traducida.
std::atomic<bool> g_usa{false};
uint32_t U(uint32_t eu) {
  if (!g_usa.load(std::memory_order_relaxed) || eu < 0x1FC6A4) return eu;
  static constexpr std::pair<uint32_t, uint32_t> k[] = {
      {0x207080, 0x207060}, {0x2E9AF0, 0x2E98E0}, {0x2ED8F0, 0x2ED6E0}, {0x2EFA10, 0x2EF800},   // codigo
      {0x481920, 0x481170}, {0x498690, 0x497E90},                                               // solo lectura
      {0x49C600, 0x49BE00}, {0x49C8D0, 0x49C0D0}, {0x49CA70, 0x49C270}, {0x49CBA4, 0x49C394},   // .bss / gp
      {0x49CD6C, 0x49C54C},   // UiAnim_Start / Camera_IsTimerElapsed
      {0x49CBA8, 0x49C398}, {0x49CD68, 0x49C548}, {0x49CD70, 0x49C550}, {0x49CD8C, 0x49C56C},
      {0x49CDC8, 0x49C5A8}, {0x49CDF8, 0x49C5D8}, {0x49CE7C, 0x49C660}, {0x49CE94, 0x49C678},
      {0x49CEBC, 0x49C6A0}, {0x4A3DF0, 0x4A35F0}, {0x4AD0C4, 0x4AC8C4}, {0x4ADE84, 0x4AD684},
      {0x5C0DC4, 0x5C05C4}, {0x5C0DC8, 0x5C05C8}, {0x5C1490, 0x5C0C90}, {0x5D9184, 0x5D8984},
      {0x5D9F00, 0x5D9700}, {0x4DB2CC, 0x4DAACC}};   // 0x4DB2CC: lw de RAMSwitch_Background-1 (0x1E10D0) en los dos ELF
  for (const auto& [a, b] : k) if (a == eu) return b;
  REXLOG_ERROR("dbz3 iwtrad: direccion EU 0x{:X} sin pareja USA", eu);
  return eu;
}

// TRAD/4: lo que el original dibuja en 2D este cuadro, ya en pantalla 512x448, para el pintor (hilo de la interfaz)
struct Quad { float x0, y0, x1, y1, u0, v0, u1, v1; uint32_t col; int tex; int mezcla;   // mezcla 0 alfa, 1 suma, 2 resta
              float x2 = 0, y2 = 0, u2 = 0, v2 = 0; bool tri = false;
              float cx0 = 0, cy0 = 0, cx1 = 512, cy1 = 448; int borde_velo7 = 0; };       // SCISSOR del GS, extremos exclusivos
struct Dibujo { std::vector<Quad> q; std::vector<std::pair<uint64_t, std::shared_ptr<const pasarela::Imagen>>> tex; float alto = 448;
                bool fig = false; float fx = 0, fy = 0, fcuadro = 0, fesc = 1; uint32_t fanim = 0;
                std::vector<FiguraTableroB2> figuras_b2;
                size_t figuras_antes = 0; };   // quads anteriores al cierre de la lista original del tablero
void (*g_figura_b2)(float, float, uint32_t, float, float) = nullptr;   // B2-TRAD: quien pinta la figura (pasarela_b2.cpp)
void (*g_figuras_b2)(const std::vector<FiguraTableroB2>&) = nullptr;
std::mutex g_mu;
std::shared_ptr<const Dibujo> g_dibujo;
// El trabajador construye la lista, pero solo el consumidor publica el par
// estado/lista al comienzo del cuadro siguiente. No se mezcla HUD de dos pasos.
thread_local std::shared_ptr<const Dibujo>* g_lista_pendiente = nullptr;
// dsB2_MAPA (R16): latido del tablero de B2. CuadroB2 lo renueva cada cuadro en que el tablero esta en pantalla;
// la capa traducida solo se pinta si el latido es reciente. Sin esto, al salir del tablero (combate, resultado,
// menus) la ultima lista quedaba visible encima de todo y tapaba la pantalla con el tablero (o con su negro).
std::atomic<long long> g_b2_latido{0};
std::atomic<bool> g_b2_comic{false};
bool B2Fresco() {
  if (!ActivoB2()) return false;
  const long long ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch()).count();
  const long long ultimo = g_b2_latido.load(std::memory_order_relaxed);
  return ultimo != 0 && ahora - ultimo < 400;   // 400 ms: bastantes cuadros PAL; oculto en cuanto no hay tablero
}

// RITMO (R16 dsIW7DB_RITMO): reparto del coste del bucle original traducido. DBZ3_IW_RITMO=1 -> una linea cada 60
// cuadros de CuadroBucle con lo que gasta el interprete/la traduccion DMA y lo que gasta el pintado del cuadro (Pintura).
struct RitmoTrad {
  uint64_t cuadros = 0, vblanks = 0, instr = 0, envios = 0, dma = 0;
  double us_cuadro = 0, us_pintura = 0, us_subidas = 0, us_firmas = 0, us_texturas = 0;
};
RitmoTrad g_ritmo_trad;
void RitmoTradLinea() {
  auto& r = g_ritmo_trad;
  const double c = double(r.cuadros ? r.cuadros : 1), v = double(r.vblanks ? r.vblanks : 1);
  REXLOG_INFO("dbz3 ritmo trad: cuadros {} vblank/cuadro {:.2f} ms/cuadro {:.2f} ms/pintura {:.2f} instr/cuadro {:.0f} "
              "instr/vblank {:.0f} envios/cuadro {:.0f} cuadros-dma/cuadro {:.2f}",
              r.cuadros, v / c, r.us_cuadro / c / 1000.0, r.us_pintura / c / 1000.0, double(r.instr) / c, double(r.instr) / v,
              double(r.envios) / c, double(r.dma) / c);
  REXLOG_INFO("dbz3 ritmo pintura: ms/subidas {:.3f} ms/firmas {:.3f} ms/texturas {:.3f}",
              r.us_subidas / c / 1000.0, r.us_firmas / c / 1000.0, r.us_texturas / c / 1000.0);
  r = RitmoTrad{};
}

// TRAD/5e: todo lo que cambia al correr el original salvo la CPU/RAM del Ee y la ISO: copiable, para la foto de tras el
// arranque previo (cada entrada en 7D5 vuelve a ella: ronda nueva).
struct Estado {
  bool listo = false, roto = false;
  std::map<uint32_t, uint32_t> reg;
  std::vector<int> pendientes;
  std::map<int, std::vector<std::pair<uint32_t, uint32_t>>> intc, dmac;
  uint64_t n = 0, vblanks = 0, cuadro_dma = ~0ull, cuadros = 0;
  uint16_t botones = 0;
  Vram vram;
  size_t aplicadas = 0;
  uint64_t n0 = 0, vueltas = 0;
  std::map<uint32_t, uint64_t> entradas;
  double reloj = 0;
  enum : uint32_t { RUN = 1, READY = 2, WAIT = 4, SUSP = 8, DORM = 0x10 };
  struct Hilo {
    uint32_t tid = 0, func = 0, pila = 0, tam = 0, gp = 0, prio = 0, prio0 = 0, estado = DORM;
    bool susp = false, tiene_ctx = false;
    int espera = 0;                                                 // 0 ninguna, 1 sueno, 2 semaforo (`sema`)
    uint32_t sema = 0, despertar = 0, arg = 0, pc = 0;
    int64_t seq = 0;
    Ee::Cpu ctx{};
    uint32_t Status() const { return susp ? (estado == WAIT ? 0xCu : uint32_t(SUSP)) : estado; }
  };
  bool elf = false, en_espera = false, pend_ceder = false;
  std::map<uint32_t, Hilo> hilos;
  struct Sema { uint32_t cuenta, max, ini; };
  std::map<uint32_t, Sema> semas;
  uint32_t sig_sema = 1, sig_hilo = 2, actual = 0, sig_dma = 0;
  int64_t seq = 0;
  int en_int = 0, salir = 0;                                         // salir: 1 ExitThread, 2 ExitDeleteThread
  uint64_t sin_tiempo = 0, hasta = 0;
  std::map<uint64_t, std::string> guardas;                         // pruebas: vblank -> ruta de la imagen
  std::map<uint64_t, uint16_t> guion;                                // vblank -> mascara (se mantiene)
  std::multimap<uint64_t, std::pair<uint32_t, uint32_t>> escribe;    // vblank -> (direccion, valor)
  uint16_t pad_actual = 0;
  int voz_intro7[2] = {-1,-1};
  // IOP
  std::map<uint32_t, uint32_t> sid;
  uint32_t iop_libre = 0x100000, sig_id = 1, snd_buf = 0;
  bool snd_nuevo = false;
  std::vector<std::pair<uint32_t, uint32_t>> fines;
};

std::atomic<uint64_t> g_vb_control{~0ull};   // R23 IW-7 (pruebas): vblank del primer control del original
std::atomic<uint32_t> g_clave{0x7D5};   // TRAD/6: minijuego que escribe el arranque (indice = clave - 0x7D0 + (clave >= 0x7D4))

// R23 BOCA-02: cola de MinigameMouthAnimations del original (hilo del interprete -> Tick de anim_iw)
std::mutex g_boca_mu;
std::vector<std::pair<int, int>> g_boca;
void PonerBoca(int lado, int clip) {
  std::lock_guard<std::mutex> lk(g_boca_mu);
  if (g_boca.size() < 16) g_boca.emplace_back(lado, clip);
}
// R23 IW-06: camara (UiAnim_Start EU 0x2ED8F0: 'C' clip | 0x10000 si a2 = 0) y vibracion (s_UTL_ACTUATE EU 0x2EFA10: 'V')
// del original para la ronda de botones, como los eventos de la ruta MIPS (iw_mips_botones.inl:152/171)
std::vector<std::pair<char, int>> g_evbot;
void PonerEvBot(char t, int n) {
  std::lock_guard<std::mutex> lk(g_boca_mu);
  if (g_evbot.size() < 32) g_evbot.emplace_back(t, n);
}

// Parches panoramicos de PCSX2 (ElHecht): FOV horizontal x0.75. Cada palabra solo se escribe si la original coincide.
struct PalabraWs { uint32_t dir, antes, nueva; };
void AplicarWs(Ee& e, const PalabraWs* t, size_t n, const char* que) {
  if (!Panoramico()) return;
  for (size_t i = 0; i < n; ++i) if (e.R32(t[i].dir) != t[i].antes) { REXLOG_WARN("dbz3 iwtrad: parche panoramico {} omitido (0x{:X} no coincide)", que, t[i].dir); return; }
  for (size_t i = 0; i < n; ++i) e.W32(t[i].dir, t[i].nueva);
  REXLOG_INFO("dbz3 iwtrad: parche panoramico {} aplicado", que);
}
const PalabraWs kWsB2[] = {{0x13192C, 0, 0x3c013f40}, {0x131930, 0, 0x4481f000}, {0x1317FC, 0, 0x461ead42}};
const PalabraWs kWsIwEu[] = {{0x1363C4, 0x3c010041, 0x3c1b3f40}, {0x1363D4, 0x3c01004b, 0x449b4800}, {0x136400, 0x3c01004b, 0x3c1b004b},
                             {0x136404, 0xe420d000, 0x46090242}, {0x136408, 0x3c01004b, 0xe769d000}, {0x13640C, 0xe420d014, 0xe760d014}};
// IW-07: el mismo parche en SLUS_218.42 (mismas instrucciones; el global que guardan esta 0x800 antes)
const PalabraWs kWsIwUsa[] = {{0x1363C4, 0x3c010041, 0x3c1b3f40}, {0x1363D4, 0x3c01004b, 0x449b4800}, {0x136400, 0x3c01004b, 0x3c1b004b},
                              {0x136404, 0xe420c800, 0x46090242}, {0x136408, 0x3c01004b, 0xe769c800}, {0x13640C, 0xe420c814, 0xe760c814}};

struct Bucle : Estado {
  Ee e;
  bool b2 = false;                                                  // B2-TRAD: el ELF es el de Budokai 2 PAL (IniciarB2)
  int fase_b2 = 0;
  bool fase_b2_cargada = false;
  PartidaB2 partida_b2;
  bool combate_b2 = false, salida_b2 = false;
  uint32_t principal_b2 = 0;
  int jugador_b2 = 0, rival_b2 = 0, casilla_b2 = 0, victoria_b2 = 0;
  int piloto_destino_b2 = -1;
  uint16_t piloto_boton_b2 = 0;
  bool piloto_auto_b2 = false;
  int retorno_perdedor_b2 = -1, barras_antes_b2 = 0;
  int contacto_hd_b2 = -1;
  bool retorno_t5_b2 = false;
  uint32_t tex_min = 0x2C00;                                        // primer bloque de texturas; antes, fotograma y Z
  struct Figura { uint64_t n = ~0ull; float x = 0, y = 0, cuadro = 0, esc = 1; uint32_t anim = 0; } figura;   // B2-TRAD: ultima orden (0x23A050)
  uint64_t figuras_n = ~0ull;
  std::vector<FiguraTableroB2> figuras_b2;
  struct CapaFigurasB2 { size_t prims = 0; std::vector<FiguraTableroB2> figuras; };
  std::map<uint64_t, CapaFigurasB2> capas_b2;   // la lista pertenece al cuadro DMA, no al siguiente vblank
  struct Foto { Estado est; std::vector<uint8_t> mem; Ee::Cpu cpu{}; uint64_t en = 0; uint32_t st = 0, pila = 0; };
  std::unique_ptr<Foto> foto;
  struct TexturaCache {
    uint64_t revision = ~0ull, huella = 0;
    uint64_t uso = 0;
    std::shared_ptr<const pasarela::Imagen> im;
  };
  std::map<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>, TexturaCache> texturas_iw;
  // R23 G3: copias con niebla horneada (huella de la textura, F, FOGCOL, color del vertice) -> (huella, imagen)
  std::map<std::tuple<uint64_t, uint32_t, uint32_t, uint32_t>,
           std::pair<uint64_t, std::shared_ptr<const pasarela::Imagen>>> niebla_iw;
  uint64_t cache_comprobadas = 0;
  void Fotografiar() {
    foto = std::make_unique<Foto>();
    foto->est = static_cast<const Estado&>(*this);
    foto->mem = e.Imagen();
    e.Guardar(foto->cpu);
    foto->en = e.n; foto->st = e.cop0_status; foto->pila = e.pila;
  }
  bool Restaurar() {
    if (!foto) return false;
    static_cast<Estado&>(*this) = foto->est;
    texturas_iw.clear();   // la foto rebobina tambien las generaciones de VRAM
    e.CargarImagen(foto->mem.data(), foto->mem.size());
    e.Poner(foto->cpu);
    e.n = foto->en; e.cop0_status = foto->st; e.pila = foto->pila; e.ceder = false; e.Error("");
    return true;
  }

  bool finish_observado = false;
  bool Leer(uint32_t a, uint64_t& v) {
    if (a == kIntcStat) v = 0x0C;                                   // VBLANK_S y VBLANK_E pendientes
    else if ((a & 0xFFFF0000u) == 0x10000000u && reg.count(a)) v = reg[a];
    else if (a == 0x12001000u) v = ((vblanks & 1) << 13) | (Global().terminado ? 2u : 0u); // FIELD y FINISH
    else v = 0;
    // PAQ_5: registrar el FINISH real de 0x7DE (registro 0x61), sin alterar el valor devuelto.
    if (a == 0x12001000u && (v & 2u) && !finish_observado && std::getenv("DBZ3_TEST_IW_7DE_FINISH")) {
      finish_observado = true;
      REXLOG_INFO("dbz3 iwtrad 7de: FINISH asserted en vblank {}", n);
    }
    return true;                                                    // como el oraculo: todo lo que no es RAM lee 0
  }

  // R23 G2 (pruebas): DBZ3_IWTRAD_RAM_EN=N guarda los 32 MB de RAM justo antes de pasar del vblank N al N+1, como
  // Reloj.guardar_punto del oraculo (nodo_7d0.pkl = 3700). Fichero: DBZ3_IWTRAD_VOLCADO + ".ram".
  void RamEn() {
    static const std::set<uint64_t> ram_en = [] {   // lista separada por comas
      std::set<uint64_t> r;
      const char* s = std::getenv("DBZ3_IWTRAD_RAM_EN");
      for (std::istringstream is(s ? s : ""); is.good();) { std::string x; std::getline(is, x, ','); if (!x.empty()) r.insert(std::stoull(x)); }
      return r;
    }();
    static const char* volcado = std::getenv("DBZ3_IWTRAD_VOLCADO");
    if (!ram_en.count(n) || !volcado || !*volcado) return;
    if (std::FILE* f = std::fopen((std::string(volcado) + "." + std::to_string(n) + ".ram").c_str(), "wb")) {
      std::vector<uint64_t> m(Ee::kRam / 8);
      for (uint32_t i = 0; i < m.size(); ++i) m[i] = e.R64(i * 8);
      std::fwrite(m.data(), 8, m.size(), f);
      std::fclose(f);
      REXLOG_INFO("dbz3 iwtrad: RAM del vblank {} guardada", n);
    }
  }

  void Fin() {                                                     // cierra el cuadro del traductor
    const auto t_fin0 = std::chrono::steady_clock::now();          // RITMO: lo que cuesta pintar el cuadro
    auto& t = Global();
    if ((cuadros < 6 || cuadro_dma % 100 == 0) && (b2 || std::getenv("DBZ3_TEST_IW_GS_HUELLA"))) {
      const std::string s = t.Texto();
      uint64_t h = 0xCBF29CE484222325ull;
      size_t lineas = 0;
      for (char ch : s) { h = (h ^ uint8_t(ch)) * 0x100000001B3ull; lineas += ch == '\n'; }
      REXLOG_INFO("dbz3 iwtrad: cuadro {} lineas {} huella {:016X} envios {}", cuadro_dma, lineas, h, GlobalDma().envios);
    }
    static const char* volcado = std::getenv("DBZ3_IWTRAD_VOLCADO");   // pruebas: texto de cada cuadro (P3 del bucle)
    // R23 G2/G3: DBZ3_IWTRAD_VOLCADO_DESDE=N vuelca 2D + entrada de la VU1 de los cuadros N..N+800 (comparar_trad.py).
    static const uint64_t volcado_desde = [] { const char* s = std::getenv("DBZ3_IWTRAD_VOLCADO_DESDE"); return s && *s ? std::stoull(s) : ~0ull; }();
    static uint64_t volcados = 0;
    GlobalDma().vif_registrar = volcado_desde != ~0ull;
    const bool en_ventana = volcado_desde == ~0ull ? cuadros < 400 : cuadro_dma >= volcado_desde && volcados < 801;
    if (volcado && *volcado && en_ventana) {
      if (std::FILE* f = std::fopen(volcado, volcados ? "ab" : "wb")) {
        const std::string s = t.Texto();
        std::fwrite(s.data(), 1, s.size(), f);
        std::fwrite(GlobalDma().vif_texto.data(), 1, GlobalDma().vif_texto.size(), f);
        std::fprintf(f, "F %llu\n", (unsigned long long)cuadro_dma);
        std::fclose(f);
      }
      if (++volcados == 801 && volcado_desde != ~0ull)
        REXLOG_INFO("dbz3 iwtrad: volcado completo ({} cuadros, hasta el {})", volcados, cuadro_dma);
    }
    GlobalDma().vif_texto.clear();
    if (!elf || cuadro_dma >= hasta) Pintura(t);                    // no publicar el ultimo cuadro del arranque al cruzar `hasta`
    // R23 G3 (pruebas): DBZ3_IWTRAD_PAUSA_EN=n1,n2,.. detiene el bucle 3 s al cerrar esos cuadros para capturar la
    // ventana en un cuadro exacto (se compara con PCSX2 en tools/iw_mips/g3_hojas.py).
    if (static const std::set<uint64_t> pausa_en = [] {
          std::set<uint64_t> r;
          const char* v = std::getenv("DBZ3_IWTRAD_PAUSA_EN");
          for (std::istringstream is(v ? v : ""); is.good();) { std::string x; std::getline(is, x, ','); if (!x.empty()) r.insert(std::stoull(x)); }
          return r; }(); pausa_en.count(cuadro_dma)) {
      REXLOG_INFO("dbz3 iwtrad: pausa {}", cuadro_dma);
      std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
    // R23 IW-7 (pruebas): DBZ3_IWTRAD_PAUSA_TRAS_CONTROL=n1,n2,.. igual, al pasar n vblanks del primer control del original
    // (alinea con las referencias de PCSX2 de E:/DBZ3HD_trabajo/g23/iw7/ref_juego.py, que cuentan vblanks).
    static const std::vector<uint64_t> tras = [] {
      std::vector<uint64_t> r;
      const char* v = std::getenv("DBZ3_IWTRAD_PAUSA_TRAS_CONTROL");
      for (std::istringstream is(v ? v : ""); is.good();) { std::string x; std::getline(is, x, ','); if (!x.empty()) r.push_back(std::stoull(x)); }
      std::sort(r.begin(), r.end());
      return r; }();
    static size_t tras_i = 0;
    if (tras_i < tras.size() && g_vb_control != ~0ull && n >= g_vb_control + tras[tras_i]) {
      REXLOG_INFO("dbz3 iwtrad: pausa tras control {} (vblank {})", tras[tras_i], n);
      ++tras_i;
      std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
    ++cuadros;
    t.FinCuadro();
    g_ritmo_trad.us_pintura += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_fin0).count();
  }

  // Sprites 2D del cuadro (tipo 6) -> quads con su textura de la VRAM en el momento del dibujo (las subidas van en orden).
  void Pintura(const Traductor& t) {
    auto d = std::make_shared<Dibujo>();
    d->alto = b2 ? 512.0f : 448.0f;                                 // B2 dibuja en un bufer de 512x512
    std::map<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>, int> cache;   // TEX0, TEXA, version y ventana leida
    // La misma vista se consulta varias veces entre subidas. Una subida cambia
    // version, por lo que nunca se reutiliza la firma de una VRAM anterior.
    std::map<std::tuple<uint64_t, uint32_t, uint32_t, uint32_t>, int> niebla_cuadro;   // misma clave que niebla_iw
    if (niebla_iw.size() > 256) niebla_iw.clear();   // ponytail: tope simple; las copias con niebla son pocas por cuadro
    std::map<std::tuple<uint64_t, uint64_t, uint64_t>, uint64_t> firmas_cuadro;
    while (texturas_iw.size() > 512) {   // LRU: conservar los atlas recurrentes aunque haya texturas animadas
      auto vieja = std::min_element(texturas_iw.begin(), texturas_iw.end(),
          [](const auto& a, const auto& b) { return a.second.uso < b.second.uso; });
      texturas_iw.erase(vieja);
    }
    // R18: la huella por bloques evita construir una mascara de 4 MB por TEX0. Tambien sirve a los
    // carriles: IW mueve los mismos atlas/paletas entre direcciones distintas a cada envio.
    const bool cache_iw = !b2 && !std::getenv("DBZ3_TEST_IW_SIN_CACHE");
    const bool recortar_iw = !b2 && !std::getenv("DBZ3_TEST_IW_SIN_RECORTES");
    const bool verificar_iw = std::getenv("DBZ3_TEST_IW_CACHE_VERIFICAR") != nullptr;
    const bool reutilizar_iw = cache_iw && (!elf || (e.R32(U(0x5C1490)) & 0xFF) == 0x10);
    const auto capa = capas_b2.find(cuadro_dma);
    const size_t antes_figuras = capa != capas_b2.end() && GlobalDma().corte_primitivas != size_t(-1)
                                    ? GlobalDma().corte_primitivas : t.prims.size();
    if (b2 && capa != capas_b2.end()) {
      static bool dicho = false;
      if (!dicho && GlobalDma().corte_primitivas != size_t(-1)) {
        dicho = true;
        REXLOG_INFO("dbz3 b2trad: frontera DMA resuelta: {} de {} primitivas, {} figuras",
                    antes_figuras, t.prims.size(), capa->second.figuras.size());
      }
    }
    size_t k = 0;
    int cache_nuevas = 0, cache_reusadas = 0;
    size_t prim = 0;
    std::vector<uint32_t> idx_prim;
    // dsB2_MAPA (R16, solo pruebas): recuento por cuadro de las primitivas del tablero traducido y de las descartadas.
    const bool traza_mapa = std::getenv("DBZ3_TEST_B2_MAPA_TRAZA") != nullptr;
    int n_tri = 0, n_spr = 0, drop_min = 0, drop_tw = 0, drop_nv = 0;
    int tw_hist[16] = {0};
    const bool medir = std::getenv("DBZ3_IW_RITMO") != nullptr;
    auto subir = [&](size_t hasta) {
      if (k >= hasta) return;
      const auto inicio = std::chrono::steady_clock::now();
      for (; k < hasta && k < t.subidas.size(); ++k) vram.Subir(t.subidas[k], cache_iw);
      if (medir) g_ritmo_trad.us_subidas += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - inicio).count();
    };
    const uint32_t work7 = elf && !b2 && g_clave.load() == 0x7DB && anim7db::escena.load() ? e.R32(U(0x49CE7C)) : 0;
    const bool abre7 = work7 >= 0x100000 && work7 < Ee::kRam - 8 && e.R32(work7 + 4) == 0;
    for (const auto& p : t.prims) {
      const bool debajo_figuras = prim++ < antes_figuras;
      subir(p.nsub);
      const uint32_t tipo = p.prim & 7;
      const bool tri = tipo >= 3 && tipo <= 5 && p.nv == 3;   // tambien las barras 2D de IW; VU1/3D no entra por DIRECT
      if (traza_mapa) { if (tri) ++n_tri; else if (tipo == 6 && p.nv == 2) ++n_spr; else ++drop_nv; }
      if (!tri && (tipo != 6 || p.nv != 2)) continue;
      const bool tme = p.prim & 0x10, abe = p.prim & 0x40, fst = p.prim & 0x100;
      if (traza_mapa && tme) ++tw_hist[(p.tex0 >> 26) & 15];
      // ponytail: un sprite opaco sin textura es el borrado de pantalla/fondo del original: taparia la escena 3D del port
      if (!tme && !abe && !b2 && !tri) continue;                    // las tiras de color del HUD no son el borrado por sprite
      const float ofx = float(p.xyoffset & 0xFFFF) / 16, ofy = float((p.xyoffset >> 32) & 0xFFFF) / 16;
      Quad q{};
      q.x0 = p.v[0].x / 16.0f - ofx; q.y0 = p.v[0].y / 16.0f - ofy; q.x1 = p.v[1].x / 16.0f - ofx; q.y1 = p.v[1].y / 16.0f - ofy;
      if (tri) { q.tri = true; q.x2 = p.v[2].x / 16.0f - ofx; q.y2 = p.v[2].y / 16.0f - ofy; }
      if (!b2) {
        q.cx0 = float(p.scissor & 0x7FF); q.cx1 = float((p.scissor >> 16) & 0x7FF) + 1;
        q.cy0 = float((p.scissor >> 32) & 0x7FF); q.cy1 = float((p.scissor >> 48) & 0x7FF) + 1;
        const float xmin = tri ? std::min({q.x0, q.x1, q.x2}) : std::min(q.x0, q.x1);
        const float xmax = tri ? std::max({q.x0, q.x1, q.x2}) : std::max(q.x0, q.x1);
        const float ymin = tri ? std::min({q.y0, q.y1, q.y2}) : std::min(q.y0, q.y1);
        const float ymax = tri ? std::max({q.y0, q.y1, q.y2}) : std::max(q.y0, q.y1);
        // Las filas inactivas del original se aparcan fuera de SCISSOR. Aplicar sus subidas en orden,
        // pero no convertir atlas de sprites que el GS no dibuja (7DB llegaba a gastar 20 ms aqui).
        if (xmin >= q.cx1 || xmax <= q.cx0 || ymin >= q.cy1 || ymax <= q.cy0 ||
            q.cx0 >= q.cx1 || q.cy0 >= q.cy1) continue;
      }
      const uint32_t c = p.v[tri ? 2 : 1].rgba;                                // el sprite toma el color del segundo vertice
      auto x2 = [](uint32_t v) { return v >= 0x80 ? 255u : v * 2; };
      // ALPHA del GS: (A-B)*C>>7 + D con A,B,D en {Cs, Cd, 0} y C en {As, Ad, FIX}
      const uint32_t al = uint32_t(p.alpha & 0xFF), A = al & 3, B = (al >> 2) & 3, C = (al >> 4) & 3, D = (al >> 6) & 3;
      if (abe && A == B && D == 1) continue;                         // = Cd: no cambia el color (solo alfa de destino)
      uint32_t a = abe ? x2((c >> 24) & 0xFF) : 255;
      if (abe && C == 2) a = x2(uint32_t((p.alpha >> 32) & 0xFF));   // FIX
      if (abe && A == B) a = 255;                                    // = Cs o 0: opaco
      q.col = x2(c & 0xFF) | (x2((c >> 8) & 0xFF) << 8) | (x2((c >> 16) & 0xFF) << 16) | (a << 24);
      q.mezcla = !abe ? 0 : (A == 0 && B == 2 && D == 1) ? 1 : (A == 2 && B == 0 && D == 1) ? 2 : 0;
      // Ocho sprites PAL de 64x448; no ampliar sprites de HUD, texturas ni otros fundidos.
      if (abre7 && p.prim == 0x46 && p.alpha == 0x44 && p.scissor == 0x01BF000001FF0000ull &&
          (c & 0xFFFFFFu) == 0xFFFFFFu && q.y0 == 0 && q.y1 == 448 && q.x1 - q.x0 == 64)
        q.borde_velo7 = q.x0 == 0 ? -1 : q.x1 == 512 ? 1 : 0;
      q.tex = -1;
      if (tme) {
        // ponytail: el original lee aqui su propio fotograma (efectos de pantalla); sin el no se pinta. Zona de
        // texturas medida en 7D5 desde el bloque 0x2C00; lo de antes es fotograma/profundidad.
        if ((p.tex0 & 0x3FFF) < tex_min) { if (traza_mapa) ++drop_min; continue; }
        // B2: tira de 32 lineas que lee 1024 px de ancho (TW=10) en CT32 con TBW=10 (640): copia del fotograma por paginas
        // (efecto de pantalla) sobre el area de texturas, que el original no sube asi; Textura() solo da 512 y salia ruido.
        // dsB2_MAPA (R16): el filtro se limita a ESA copia (TW=10, TBW=10, CT32). Las demas texturas TW>9
        // (un mapa de 1024 px de ancho, TBW=16, tambien es TW=10) se traducen como cualquier otra: antes se
        // descartaban y esa parte del mapa desaparecia. DBZ3_TEST_B2_MAPA_TW9_VIEJO=1 restaura el filtro viejo.
        const uint32_t tw = (p.tex0 >> 26) & 15;
        if (b2 && tw > 9) {
          const bool copia_fotograma = tw == 10 && ((p.tex0 >> 14) & 0x3F) == 10 && ((p.tex0 >> 20) & 0x3F) == 0;
          if (std::getenv("DBZ3_TEST_B2_MAPA_TW9_VIEJO") || copia_fotograma) {
            if (traza_mapa) ++drop_tw;
            continue;
          }
        }
        int cx = 0, cy = 0, cw = 0, ch = 0;
        uint64_t ventana = 0;
        if (recortar_iw && fst && tw <= 10 && ((p.tex0 >> 30) & 15) <= 10) {
          const int iw = 1 << tw, ih = 1 << ((p.tex0 >> 30) & 15);
          const float u[3] = {float(p.v[0].uv & 0x3FFF) / 16 / iw, float(p.v[1].uv & 0x3FFF) / 16 / iw,
                              float(p.v[2].uv & 0x3FFF) / 16 / iw};
          const float v[3] = {float((p.v[0].uv >> 16) & 0x3FFF) / 16 / ih, float((p.v[1].uv >> 16) & 0x3FFF) / 16 / ih,
                              float((p.v[2].uv >> 16) & 0x3FFF) / 16 / ih};
          const int n = tri ? 3 : 2;
          if (VentanaTextura(iw, ih, *std::min_element(u, u + n), *std::min_element(v, v + n),
                            *std::max_element(u, u + n), *std::max_element(v, v + n), cx, cy, cw, ch))
            ventana = uint64_t(cx + 1) | (uint64_t(cy + 1) << 11) | (uint64_t(cw) << 22) | (uint64_t(ch) << 33);
          else cx = cy = cw = ch = 0;
        }
        TexturaCache* guardada = nullptr;
        uint64_t revision = vram.version;
        uint64_t identidad = p.tex0;
        if (reutilizar_iw) {
          const auto inicio = std::chrono::steady_clock::now();
          const auto clave_firma = std::make_tuple(p.tex0, ventana, vram.version);
          auto firma = firmas_cuadro.find(clave_firma);
          if (firma == firmas_cuadro.end())
            firma = firmas_cuadro.emplace(clave_firma, vram.FirmaTextura(p.tex0, cx, cy, cw, ch)).first;
          revision = firma->second;
          if (medir) g_ritmo_trad.us_firmas += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - inicio).count();
          // TBP/CBP son ubicaciones, CLD es una orden de carga: los bytes y el formato identifican la imagen.
          identidad &= ~(0x3FFFull | (0x3FFFull << 37) | (7ull << 61));
          guardada = &texturas_iw[{identidad, p.texa, revision, ventana}];
          guardada->uso = cuadros;
        }
        const auto clave = std::make_tuple(identidad, p.texa, revision, ventana);
        auto it = cache.find(clave);
        if (it == cache.end()) {
          TexturaCache temporal;
          auto& tex = guardada ? *guardada : temporal;
          if (tex.revision != revision) {
            ++cache_nuevas;
            auto im = std::make_shared<pasarela::Imagen>();
            const auto inicio = std::chrono::steady_clock::now();
            if (!vram.Textura(p.tex0, p.texa, im->w, im->h, im->rgba, b2, cx, cy, cw, ch)) continue;
            tex.huella = HuellaImagen(im->rgba.data(), im->rgba.size());
            tex.huella ^= uint64_t(im->w) << 48 ^ uint64_t(im->h) << 32;
            if (medir) g_ritmo_trad.us_texturas += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - inicio).count();
            tex.im = std::move(im);
            tex.revision = revision;
          } else ++cache_reusadas;
          if (guardada && verificar_iw) {
            pasarela::Imagen referencia;
            if (!vram.Textura(p.tex0, p.texa, referencia.w, referencia.h, referencia.rgba, b2, cx, cy, cw, ch) ||
                referencia.w != tex.im->w || referencia.h != tex.im->h || referencia.rgba != tex.im->rgba) {
              REXLOG_ERROR("dbz3 iwtrad: cache DIFERENCIA tex0 {:X}", p.tex0);
              roto = true;
              return;
            }
            if (++cache_comprobadas % 1000 == 0)
              REXLOG_INFO("dbz3 iwtrad: cache verificada {} texturas, 0 diferencias", cache_comprobadas);
          }
          it = cache.emplace(clave, int(d->tex.size())).first;
          d->tex.push_back({tex.huella, tex.im});   // imagen inmutable compartida; sin copiar megabytes cada cuadro
        }
        q.tex = it->second;
        const auto& im = *d->tex[size_t(q.tex)].second;
        if (fst) {
          q.u0 = (float(p.v[0].uv & 0x3FFF) / 16 - cx) / im.w; q.v0 = (float((p.v[0].uv >> 16) & 0x3FFF) / 16 - cy) / im.h;
          q.u1 = (float(p.v[1].uv & 0x3FFF) / 16 - cx) / im.w; q.v1 = (float((p.v[1].uv >> 16) & 0x3FFF) / 16 - cy) / im.h;
          if (tri) { q.u2 = (float(p.v[2].uv & 0x3FFF) / 16 - cx) / im.w; q.v2 = (float((p.v[2].uv >> 16) & 0x3FFF) / 16 - cy) / im.h; }
        } else {
          auto st = [](const Vertice& v, int i) { float f; uint32_t b = uint32_t(v.st >> (32 * i)); std::memcpy(&f, &b, 4);
                                                  float qq; std::memcpy(&qq, &v.q, 4); return qq ? f / qq : f; };
          q.u0 = st(p.v[0], 0); q.v0 = st(p.v[0], 1); q.u1 = st(p.v[1], 0); q.v1 = st(p.v[1], 1);
          if (tri) { q.u2 = st(p.v[2], 0); q.v2 = st(p.v[2], 1); }
        }
      }
      // R23 G3: niebla del GS (PRIM FGE), tras la funcion de textura y antes de la mezcla; el alfa no cambia:
      // C = (F*C + (255-F)*FOGCOL) / 255. Medido en un volcado GS de PCSX2 (7D7): el anillo de la mira va con F=0 y
      // FOGCOL 0x64FFFF (la paleta negra sale amarillo claro) y los digitos del contador con F=205. Sin textura se
      // aplica al color; con textura se hornea en una copia (pocos sprites con niebla por cuadro).
      if (p.prim & 0x20) {
        const uint32_t F = p.v[tri ? 2 : 1].f, fc = uint32_t(p.fogcol & 0xFFFFFF);
        auto niebla = [&](uint32_t c, int k) { return (F * c + (255 - F) * ((fc >> (8 * k)) & 0xFF)) / 255; };
        if (F < 255 && q.tex < 0) {
          q.col = niebla(q.col & 0xFF, 0) | niebla((q.col >> 8) & 0xFF, 1) << 8 | niebla((q.col >> 16) & 0xFF, 2) << 16 |
                  (q.col & 0xFF000000u);
        } else if (F < 255) {
          const auto [huella, im] = d->tex[size_t(q.tex)];
          const auto clave_n = std::make_tuple(huella, F, fc, q.col & 0xFFFFFFu);
          auto idx = niebla_cuadro.find(clave_n);
          if (idx == niebla_cuadro.end()) {
            auto& hecha = niebla_iw[clave_n];
            if (!hecha.second) {
              auto nim = std::make_shared<pasarela::Imagen>(*im);
              for (size_t i = 0; i + 3 < nim->rgba.size(); i += 4)
                for (int k = 0; k < 3; ++k)
                  nim->rgba[i + k] = uint8_t(niebla(nim->rgba[i + k] * ((q.col >> (8 * k)) & 0xFF) / 255, k));
              hecha = {HuellaImagen(nim->rgba.data(), nim->rgba.size()) ^ uint64_t(nim->w) << 48 ^ uint64_t(nim->h) << 32, nim};
            }
            idx = niebla_cuadro.emplace(clave_n, int(d->tex.size())).first;
            d->tex.push_back(hecha);
          }
          q.tex = idx->second;
          q.col |= 0xFFFFFFu;   // el color del vertice ya va dentro de la copia
        }
      }
      if (!b2 && cuadros % 120 == 0 && std::getenv("DBZ3_TEST_IW_PANEL_TRAZA") && q.y0 > 290) {
        REXLOG_INFO("dbz3 iw panel: xy {:.2f},{:.2f}..{:.2f},{:.2f} uv {:.4f},{:.4f}..{:.4f},{:.4f} tex0 {:016X} clamp {:X} fst {} tri {}",
                    q.x0, q.y0, q.x1, q.y1, q.u0, q.v0, q.u1, q.v1, p.tex0, p.clamp, fst, tri);
      }
      if (static const bool qg = std::getenv("DBZ3_TEST_IW_QUAD_GRANDE") != nullptr;   // R23 7DB: que tapa la escena
          qg && !b2 && cuadros % 30 == 0 && std::fabs(q.x1 - q.x0) > 200 && std::fabs(q.y1 - q.y0) > 150)
        REXLOG_INFO("dbz3 iw quad grande: xy {:.0f},{:.0f}..{:.0f},{:.0f} tex0 {:016X} tme {} abe {} alpha {:X} col {:08X} tri {}",
                    q.x0, q.y0, q.x1, q.y1, p.tex0, tme ? 1 : 0, abe ? 1 : 0, p.alpha, q.col, tri ? 1 : 0);
      d->q.push_back(q);
      idx_prim.push_back(uint32_t(prim - 1));
      if (debajo_figuras) ++d->figuras_antes;
    }
    subir(t.subidas.size());
    // R23 G3 (pruebas): DBZ3_IWTRAD_QUADS=n1,n2,.. vuelca los quads que recibe el pintor de esos cuadros (indice de la
    // primitiva del traductor que los origino) en DBZ3_IWTRAD_VOLCADO + ".q<n>.txt" (tools/iw_mips/g3_pantalla.py).
    static const char* const qs = std::getenv("DBZ3_IWTRAD_QUADS");
    static const char* const vo = std::getenv("DBZ3_IWTRAD_VOLCADO");
    if (qs && vo && !b2) {
      bool toca = false;
      for (std::istringstream is(qs); is.good();) { std::string x; std::getline(is, x, ','); if (!x.empty() && std::stoull(x) == cuadro_dma) toca = true; }
      if (toca)
        if (std::FILE* f = std::fopen((std::string(vo) + ".q" + std::to_string(cuadro_dma) + ".txt").c_str(), "w")) {
          for (size_t i = 0; i < d->q.size(); ++i) {
            const Quad& q = d->q[i];
            int tw = 0, th = 0;
            if (q.tex >= 0) { tw = d->tex[size_t(q.tex)].second->w; th = d->tex[size_t(q.tex)].second->h; }
            std::fprintf(f, "%u %.4f %.4f %.4f %.4f %.6f %.6f %.6f %.6f %08X %d %d %d %.4f %.4f %.6f %.6f %.0f %.0f %.0f %.0f %d %d\n",
                         idx_prim[i], q.x0, q.y0, q.x1, q.y1, q.u0, q.v0, q.u1, q.v1, q.col, q.tex, q.mezcla, q.tri ? 1 : 0,
                         q.x2, q.y2, q.u2, q.v2, q.cx0, q.cy0, q.cx1, q.cy1, tw, th);
          }
          std::fclose(f);
          for (size_t i = 0; i < d->tex.size(); ++i)
            if (std::FILE* g = std::fopen((std::string(vo) + ".q" + std::to_string(cuadro_dma) + ".t" + std::to_string(i) + ".rgba").c_str(), "wb")) {
              const auto& im = *d->tex[i].second;
              std::fwrite(&im.w, 4, 1, g); std::fwrite(&im.h, 4, 1, g);
              std::fwrite(im.rgba.data(), 1, im.rgba.size(), g);
              std::fclose(g);
            }
        }
    }
    if (reutilizar_iw && std::getenv("DBZ3_IW_RITMO") && cuadros % 60 == 0) {
      REXLOG_INFO("dbz3 iwtrad cache: entradas {} nuevas {} reutilizadas {}", texturas_iw.size(), cache_nuevas, cache_reusadas);
      if (!t.prims.empty()) {
        const auto& p = t.prims.back();
        REXLOG_INFO("dbz3 iwtrad lienzo: frame {:X} scissor {:X} xyoffset {:X}", p.frame, p.scissor, p.xyoffset);
      }
    }
    if (traza_mapa) {   // dsB2_MAPA: una linea por cuadro del tablero traducido
      int texq = 0, triq = 0;
      for (const auto& q : d->q) if (q.tex >= 0) { ++texq; if (q.tri) ++triq; }
      std::string tw;
      for (int i = 0; i < 16; ++i) {
        if (!tw_hist[i]) continue;
        if (!tw.empty()) tw += ",";
        tw += std::to_string(i) + ":" + std::to_string(tw_hist[i]);
      }
      REXLOG_INFO("dbz3 b2mapa capa: dma {} prims {} tri {} spr {} quads {} tex {} tri_tex {} fig_antes {} min {} tw9 {} nv {} tw {} b2 {}",
                  (unsigned long long)cuadro_dma, int(t.prims.size()), n_tri, n_spr, int(d->q.size()), texq, triq,
                  int(antes_figuras), drop_min, drop_tw, drop_nv, tw.empty() ? "-" : tw.c_str(), b2 ? 1 : 0);
    }
    if (b2 && figura.n != ~0ull && n - figura.n <= 2) {             // B2-TRAD: la orden de figura de este cuadro (si la hubo)
      d->fig = true; d->fx = figura.x; d->fy = figura.y; d->fanim = figura.anim; d->fcuadro = figura.cuadro; d->fesc = figura.esc;
    }
    if (b2 && capa != capas_b2.end()) {
      d->figuras_b2 = capa->second.figuras;
      d->fig = false;   // nunca reutilizar el jugador de una lista anterior
    }
    if (b2) {
      capas_b2.erase(capas_b2.begin(), capas_b2.upper_bound(cuadro_dma));
      GlobalDma().corte_direccion = 0;
      GlobalDma().corte_primitivas = size_t(-1);
    }
    if (g_lista_pendiente) *g_lista_pendiente = std::move(d);
    else { std::lock_guard<std::mutex> lk(g_mu); g_dibujo = std::move(d); }
  }

  void Dma(int canal, uint32_t base, uint32_t chcr) {
    const uint32_t madr = reg[base + 0x10], qwc = reg[base + 0x20], tadr = reg[base + 0x30];
    // IW EU FUN_00128B90: antes de `hasta`, VIF1 DIRECT de 0x486C20 lleva GS A+D 0x61.
    // Procesar DMA tambien en ese arranque; Fin descarta el dibujo, pero CSR debe ver FINISH real.
    if ((canal == 1 || canal == 2) && !(b2 && elf && n < hasta)) {
      if (cuadro_dma != n) { if (cuadro_dma != ~0ull) Fin(); cuadro_dma = n; }
      GlobalDma().Envio(canal, chcr, madr, qwc, tadr, Global(), &e,
          [](void* c, uint32_t a) { return static_cast<Ee*>(c)->R64(a); },
          [](void* c, uint32_t a, uint32_t n) { return static_cast<Ee*>(c)->Bloque(a, n); });
    } else if (canal == 8 || canal == 9) {                          // scratchpad: copia de verdad (modo normal)
      const uint32_t sadr = reg[base + 0x80] & 0x3FF0, tam = (qwc & 0xFFFF) * 16;
      const uint32_t spr = 0x70000000u + sadr, ram = madr & 0x1FFFFFFF;
      const uint32_t org = canal == 8 ? spr : ram, dst = canal == 8 ? ram : spr;
      if (((chcr >> 2) & 3) == 0)
        for (uint32_t k = 0; k < tam; k += 4) e.W32(dst + k, e.R32(org + k));
    }
    reg[base] = chcr & ~0x100u;
    pendientes.push_back(canal);
  }

  bool Escribir(uint32_t a, uint64_t v) {
    if (a == 0x12001000u && (v & 2)) Global().terminado = false; // CSR FINISH: escribir uno lo reconoce
    reg[a] = uint32_t(v);                                           // como el oraculo: toda escritura fuera de RAM se guarda
    if (a == kIntcStat && (v & 4)) ++vblanks;
    static const std::map<uint32_t, int> kCanales = {{0x10008000u, 0}, {0x10009000u, 1}, {0x1000A000u, 2}, {0x1000B000u, 3},
        {0x1000B400u, 4}, {0x1000C000u, 5}, {0x1000C400u, 6}, {0x1000C800u, 7}, {0x1000D000u, 8}, {0x1000D400u, 9}};
    if (auto it = kCanales.find(a); it != kCanales.end() && (v & 0x100)) Dma(it->second, a, uint32_t(v));
    return true;
  }

  void Tick() {                                                    // oraculo Reloj.vblank, sin hilos
    for (int vuelta = 0; vuelta < 100000; ++vuelta) {
      if (!pendientes.empty()) {
        auto p = std::move(pendientes);
        pendientes.clear();
        for (int canal : p)
          for (auto [f, arg] : dmac[canal])
            if (!e.Anidada(f, {uint32_t(canal), arg}, 5000000)) { e.Error("manejador DMAC: " + e.error()); return; }
        continue;
      }
      RamEn(); ++n; ++vblanks;
      for (auto [f, arg] : intc[2])
        if (!e.Anidada(f, {2u, arg}, 5000000)) { e.Error("manejador VBLANK: " + e.error()); return; }
      return;
    }
    e.Error("los fines de DMA se realimentan sin fin");
  }

  // ---------------- TRAD/5: arranque desde la entrada del ELF (oraculo.py: Nucleo, Iop, Cd, Reloj, Mando) ----------------
  // Mismo comportamiento que el oraculo Python, numero a numero; el cambio de hilo sale de Ee::Correr (bandera `ceder`) y
  // lo hace Planificar() fuera del interprete, con los mismos pasos y en el mismo orden que Nucleo.ceder/cambiar/salir.
  // CD
  std::ifstream iso;
  bool iso_bad = false;          // PAQ_2/Paso 2: algun sector salio corto -> se vuelve al espejo
  std::vector<uint8_t> Sector(uint32_t lsn, uint32_t k) {
    std::vector<uint8_t> b(size_t(k) * 2048);
    iso.clear();
    iso.seekg(std::streamoff(uint64_t(lsn) * 2048));
    iso.read(reinterpret_cast<char*>(b.data()), std::streamsize(b.size()));
    const std::streamsize leidos = iso.gcount();
    if (leidos != std::streamsize(b.size())) {   // PAQ_2/Paso 2: no dar por bueno un sector incompleto
      iso_bad = true;
      REXLOG_WARN("dbz3 iwtrad: sector ISO incompleto LSN {} ({} de {} bytes)", lsn, leidos, b.size());
    }
    b.resize(size_t(std::max<std::streamsize>(0, leidos)));
    return b;
  }
  static uint32_t U32(const std::vector<uint8_t>& d, size_t o) { uint32_t v = 0; if (o + 4 <= d.size()) std::memcpy(&v, d.data() + o, 4); return v; }
  static std::string Mayus(std::string s) { for (auto& ch : s) ch = char(std::toupper(uint8_t(ch))); if (auto p = s.find(';'); p != std::string::npos) s.resize(p); return s; }
  bool IsoBuscar(const std::string& ruta, uint32_t& lsn, uint32_t& tam, std::string& nom) {
    const auto pvd = Sector(16, 1);
    lsn = U32(pvd, 156 + 2); tam = U32(pvd, 156 + 10);
    std::vector<std::string> partes;
    std::string cur;
    for (char ch : ruta) { if (ch == '\\' || ch == '/') { if (!cur.empty()) partes.push_back(cur); cur.clear(); } else cur += ch; }
    if (!cur.empty()) partes.push_back(cur);
    bool hallado = false;
    for (const auto& parte : partes) {
      const std::string busca = Mayus(parte);
      const auto d = Sector(lsn, (tam + 2047) / 2048);
      hallado = false;
      for (size_t i = 0; i < d.size();) {
        const uint8_t l = d[i];
        if (l == 0) { i = (i / 2048 + 1) * 2048; continue; }
        if (i + 33 > d.size()) break;
        const uint8_t nl = d[i + 32];
        const std::string n = Mayus(std::string(reinterpret_cast<const char*>(d.data() + i + 33), std::min<size_t>(nl, d.size() - i - 33)));
        if (n == busca) { lsn = U32(d, i + 2); tam = U32(d, i + 10); nom = n; hallado = true; break; }
        i += l;
      }
      if (!hallado) return false;
    }
    return hallado;
  }

  static uint64_t Sx(int64_t v) { return uint64_t(int64_t(int32_t(uint32_t(v)))); }
  void Listo(Hilo& h) { ++seq; h.estado = READY; h.espera = 0; h.seq = seq; }
  Hilo* Elegible() {
    Hilo* m = nullptr;
    for (auto& [t, h] : hilos)
      if (h.estado == READY && !h.susp && (!m || h.prio < m->prio || (h.prio == m->prio && h.seq < m->seq))) m = &h;
    return m;
  }
  Hilo* H(uint32_t t) {
    auto it = hilos.find(t);
    if (it == hilos.end()) { e.Error("hilo desconocido " + std::to_string(t)); return nullptr; }
    return &it->second;
  }

  void SyscallNucleo() {                                           // oraculo Nucleo.despachar
    const int64_t num = int64_t(e.r[3]);
    const uint32_t k = uint32_t(num < 0 ? -num : num);
    const uint32_t a0 = uint32_t(e.r[4]), a1 = uint32_t(e.r[5]);
    int64_t v = 0;
    bool ced = false;
    auto tid = [&](uint32_t a) { return a == 0 ? actual : a; };
    Hilo* h = nullptr;
    switch (k) {
      case 0x3C: v = a1 != 0xFFFFFFFFu ? int64_t(a1) + uint32_t(e.r[6]) : 0x2000000 - 0x10000; break;
      case 0x3D: v = a0; break;
      case 0x3E: v = 0x1F00000; break;
      case 0x40: {
        v = sig_sema;
        const uint32_t c8 = e.R32(a0 + 8), c4 = e.R32(a0 + 4);
        semas[uint32_t(v)] = {c8, c4 ? c4 : 0x7FFFFFFFu, c8};
        ++sig_sema;
        break;
      }
      case 0x41: semas.erase(a0); v = a0; break;
      case 0x42: case 0x43: {
        Hilo* m = nullptr;
        for (auto& [t, x] : hilos)
          if (x.estado == WAIT && x.espera == 2 && x.sema == a0 && (!m || x.seq < m->seq)) m = &x;
        if (m) Listo(*m);
        else if (auto it = semas.find(a0); it != semas.end()) it->second.cuenta = std::min(it->second.cuenta + 1, it->second.max);
        v = a0; ced = true;
        break;
      }
      case 0x44: {
        auto it = semas.find(a0);
        if (it != semas.end() && it->second.cuenta > 0) --it->second.cuenta;
        else if (it != semas.end()) {
          if (!(h = H(actual))) return;
          ++seq; h->estado = WAIT; h->espera = 2; h->sema = a0; h->seq = seq;
          ced = true;
        }
        v = a0;
        break;
      }
      case 0x45: case 0x46: {
        auto it = semas.find(a0);
        if (it != semas.end() && it->second.cuenta > 0) { --it->second.cuenta; v = a0; } else v = -419;
        break;
      }
      case 0x47: case 0x48: {
        Sema se{0, 0, 0};
        if (auto it = semas.find(a0); it != semas.end()) se = it->second;
        uint32_t esp = 0;
        for (auto& [t, x] : hilos) esp += x.estado == WAIT && x.espera == 2 && x.sema == a0;
        const uint32_t w[4] = {se.cuenta, se.max, se.ini, esp};
        for (int i = 0; i < 4; ++i) e.W32(a1 + 4 * i, w[i]);
        v = a0;
        break;
      }
      case 0x20: {
        v = sig_hilo;
        Hilo n;
        n.tid = uint32_t(v); n.func = e.R32(a0 + 4); n.pila = e.R32(a0 + 8); n.tam = e.R32(a0 + 12); n.gp = e.R32(a0 + 16);
        n.prio = n.prio0 = e.R32(a0 + 20);
        hilos[n.tid] = n;
        ++sig_hilo;
        break;
      }
      case 0x21: hilos.erase(a0); v = a0; break;
      case 0x22:
        if (!(h = H(a0))) return;
        h->arg = a1; h->tiene_ctx = false; h->prio = h->prio0;
        Listo(*h);
        v = a0; ced = true;
        break;
      case 0x23: case 0x24:
        if (!(h = H(actual))) return;
        h->estado = DORM; h->tiene_ctx = false;
        salir = k == 0x24 ? 2 : 1;
        e.ceder = true;
        return;
      case 0x25:
        if (!(h = H(a0))) return;
        h->estado = DORM; h->tiene_ctx = false;
        v = a0;
        break;
      case 0x29: case 0x2A:
        if (!(h = H(tid(a0)))) return;
        v = h->prio; h->prio = a1; ced = true;
        break;
      case 0x2B: case 0x2C:
        if (!(h = H(actual))) return;
        if (h->prio == a0 && h->estado == RUN) Listo(*h);
        v = a0; ced = true;
        break;
      case 0x2F: v = actual; break;
      case 0x30: case 0x31: {
        auto it = hilos.find(tid(a0));
        if (it == hilos.end()) { v = -407; break; }
        Hilo& x = it->second;
        if (a1) {
          const uint32_t tipo = x.espera == 0 ? 0 : x.espera == 1 ? 1 : 2, sema = x.espera == 2 ? x.sema : 0;
          const uint32_t w[12] = {x.Status(), x.func, x.pila, x.tam, x.gp, x.prio0, x.prio, 0, 0, tipo, sema, x.despertar};
          for (int i = 0; i < 12; ++i) e.W32(a1 + 4 * i, w[i]);
        }
        v = x.Status();
        break;
      }
      case 0x32:
        if (!(h = H(actual))) return;
        if (h->despertar) --h->despertar;
        else { ++seq; h->estado = WAIT; h->espera = 1; h->seq = seq; ced = true; }
        v = actual;
        break;
      case 0x33: case 0x34:
        if (!(h = H(a0))) return;
        if (h->estado == WAIT && h->espera == 1) Listo(*h); else ++h->despertar;
        v = a0; ced = true;
        break;
      case 0x35: case 0x36:
        if (!(h = H(tid(a0)))) return;
        v = h->despertar; h->despertar = 0;
        break;
      case 0x37: case 0x38: if (!(h = H(a0))) return; h->susp = true; v = a0; ced = true; break;
      case 0x39: case 0x3A: if (!(h = H(a0))) return; h->susp = false; v = a0; ced = true; break;
      case 0x77: v = ++sig_dma; break;
      case 0x76: v = -1; break;
      case 0x7A: v = a0 == 4 ? 0x60000 : 0x10000; break;
      case 0x10: intc[int(a0)].push_back({a1, uint32_t(e.r[7])}); v = int64_t(intc[int(a0)].size()); break;
      case 0x12: dmac[int(a0)].push_back({a1, uint32_t(e.r[7])}); v = int64_t(dmac[int(a0)].size()); break;
      case 0x7F: v = 0x2000000; break;
      default: break;
    }
    e.r[2] = Sx(v);                                                  // (el oraculo lo pone al volver de ceder: mismo valor)
    if (ced && !en_int) e.ceder = true;
  }

  // Cambio de hilo (Nucleo.cambiar). act = nullptr: nadie (arranque o el que salio).
  void Cambiar(Hilo* act, Hilo& mejor, bool corre) {
    if (&mejor == act) { act->estado = RUN; return; }
    if (corre) { act->estado = READY; act->seq = -seq; }
    if (act) { e.Guardar(act->ctx); act->tiene_ctx = true; }
    if (!mejor.tiene_ctx) {                                          // arranque: a0 = argumento, pila y gp propios
      e.r[4] = mejor.arg; e.r[28] = mejor.gp;
      e.r[29] = (mejor.pila + mejor.tam - 0x10) & ~0xFu; e.r[31] = Ee::kRetorno;
      mejor.pc = mejor.func;
    } else e.Poner(mejor.ctx);
    mejor.tiene_ctx = false;
    mejor.estado = RUN;
    actual = mejor.tid;
  }

  // Reloj.vblank: entrega lo pendiente sin que pase el tiempo; si no hay nada, un vblank.
  void Vblank() {
    for (auto it = escribe.begin(); it != escribe.end();)              // explora.py ESCRIBE: al entrar con n == vblank
      if (it->first == n) { e.W32(it->second.first, it->second.second); it = escribe.erase(it); } else ++it;
    auto pend = std::move(pendientes); pendientes.clear();
    auto fin = std::move(fines); fines.clear();
    ++en_int;
    struct Baja { int& x; ~Baja() { --x; } } baja{en_int};
    if (!pend.empty() || !fin.empty()) {
      for (int canal : pend) {
        const auto lista = dmac[canal];
        for (auto [f, arg] : lista)
          if (!e.Anidada(f, {uint32_t(canal), arg}, 5000000)) { e.Error("manejador DMAC: " + e.error()); return; }
      }
      for (auto [f, arg] : fin)
        if (!e.Anidada(f, {arg}, 5000000)) { e.Error("fin de RPC: " + e.error()); return; }
      if (++sin_tiempo < 100000) return;
    }
    sin_tiempo = 0;
    if (!guardas.empty() && guardas.begin()->first <= n && !en_espera) {   // pruebas: imagen entera como oraculo guardar_punto
      const std::string guarda_ruta = guardas.begin()->second;
      guardas.erase(guardas.begin());
      if (std::FILE* f = std::fopen(guarda_ruta.c_str(), "wb")) {
        std::fwrite(e.Imagen().data(), 1, e.Imagen().size(), f);
        std::fclose(f);
        REXLOG_INFO("dbz3 iwtrad: imagen del vblank {} -> {}", n, guarda_ruta);
      }
    }
    if (static const bool traza = std::getenv("DBZ3_TEST_IWTRAD_TAREAS") != nullptr; traza && elf && !b2) {
      // pruebas (IW-07, como explora.py): lista de tareas del original (cabeza EU 0x4AD558, fin 0x4AD570; +0x1C funcion, +0x28 sig.)
      std::string t;
      const uint32_t fin = g_usa ? 0x4ACD70 : 0x4AD570;
      uint32_t a = e.R32(g_usa ? 0x4ACD58 : 0x4AD558);
      for (int k = 0; a != fin && a && a < Ee::kRam - 0x40 && k < 60; ++k, a = e.R32(a + 0x28)) {
        char q[12]; std::snprintf(q, sizeof q, "%X ", e.R32(a + 0x1C)); t += q;
      }
      static std::string ant;
      if (t != ant) { ant = t; REXLOG_INFO("dbz3 iwtrad tareas: vb {} pad {:04X} pantalla {:X} | {}", n, pad_actual, e.R32(U(0x5C1490)) & 0xFF, t); }
    }
    RamEn(); ++n; ++vblanks;
    const auto lista = intc[2];
    for (auto [f, arg] : lista)
      if (!e.Anidada(f, {2u, arg}, 5000000)) { e.Error("manejador VBLANK: " + e.error()); return; }
  }

  // Corre los hilos hasta el vblank `objetivo` (parando solo donde el oraculo pasaria el tiempo). false = error.
  bool Planificar(const uint64_t& objetivo) {
    for (;;) {
      if (!e.error().empty()) return false;
      if (actual == 0) {                                              // director / el hilo que salio: elegir
        Hilo* m = Elegible();
        if (!m) { if (n >= objetivo) return true; Vblank(); continue; }
        Cambiar(nullptr, *m, false);
      }
      if (pend_ceder) {                                               // Nucleo.ceder
        Hilo* act = H(actual);
        if (!act) return false;
        const bool corre = act->estado == RUN && !act->susp;
        if (Hilo* m = Elegible()) {
          if (!(corre && act->prio <= m->prio)) Cambiar(act, *m, corre);
          pend_ceder = false;
        } else if (corre) pend_ceder = false;
        else { if (n >= objetivo) return true; Vblank(); continue; }
      }
      Hilo* h = H(actual);
      if (!h) return false;
      uint32_t pc = h->pc;
      salir = 0;
      if (!e.Correr(pc, 4000000000ull)) return false;
      if (pc == Ee::kRetorno || salir) {                              // fin del hilo (vuelta de su funcion o ExitThread)
        h->estado = DORM; h->tiene_ctx = false;
        if (salir == 2) hilos.erase(actual);
        actual = 0;
        continue;
      }
      h->pc = pc;
      pend_ceder = true;
    }
  }

  uint16_t Pad() {                                                   // Mando.leer: guion hasta `hasta`, luego el jugador
    if (!elf || n >= hasta) {
      if (b2 && piloto_auto_b2) {
        const uint32_t w = e.R32(e.gp - 0x7734);
        const uint32_t fn = w && w < Ee::kRam - 0x80 ? e.R32(w + 0x1C) : 0;
        if (fn == 0x237990 || fn == 0x236EA0) return 0;
        return n % 24 < 4 ? 0x4008 : 0; // confirmar / saltar escenas del piloto
      }
      // Piloto de prueba en el reloj que CONSUME el mando. El piloto del render iba a 60 Hz y
      // el ELF PAL a 50: dos actualizaciones podian borrar el flanco antes de scePadRead.
      // Solo entrega botones por libpad; vida, aciertos, dificultad y resultado los decide el ELF.
      static const bool auto7db = [] {
        const char* p = std::getenv("DBZ3_TEST_IWMINI");
        return p && std::strstr(p, "auto") && !std::strstr(p, "pierde") && !std::strstr(p, "fallos");
      }();
      uint16_t jugador = botones;
      if (elf && !b2 && g_clave.load() == 0x7DB && anim7db::escena.load()) {
        const uint32_t pw = e.R32(U(0x49CE7C));
        if (pw >= 0x100000 && pw < Ee::kRam - 8 && e.R32(pw + 4) == 0) {
          static const bool forzado = [] { const char* v = std::getenv("DBZ3_IWTRAD_BOTONES"); return v && *v; }();
          if (!forzado) { std::lock_guard lk(anim7db::mando_mu); jugador = anim7db::mando.Leer(n, botones); }
        }
      }
      if (elf && !b2 && g_clave.load() == 0x7DB && auto7db) {
        const uint32_t pw = e.R32(U(0x49CE7C)), q = e.R32(U(0x49CD70)), choque = e.R32(U(0x49CE94));
        auto ok = [](uint32_t a) { return a >= 0x100000 && a < Ee::kRam - 0x4000; };
        if (ok(pw) && e.R32(pw + 4) == 0 && V2_7DB()) return jugador & 8;
        if (!ok(pw) || !ok(q) || !ok(choque) || e.R32(pw + 4) != 1) return 0;
        uint16_t out = 0;
        auto boton = [&](int l, int k) {
          if (l < 0 || l > 1 || k < 0 || k >= 50) return;
          const int bt = e.R8(q + 0x28 + uint32_t(l * 100 + k));
          if (bt >= 0 && bt < 16) out |= uint16_t(e.R32(U(0x498690) + uint32_t(bt) * 4));
        };
        const int sub = int(e.R32(pw + 8));
        if (sub == 3 && e.R32(choque) == 0) {
          if ((n & 3) < 2) boton(int(e.R32(choque + 0x80)), int(e.R32(choque + 0x7C)));
        } else if (sub == 1) {
          const int activo = int(e.R32(pw + 0x2E0));
          for (int l = 0; l < 2; ++l) {
            const int k = int(e.R32(pw + 0x2C + uint32_t(l) * 4));
            if ((activo == l || activo == 2) && k >= 0 && k < 50 && e.R8(pw + 0x2D0 + uint32_t(l)) &&
                e.R8(pw + 0x68 + uint32_t(l * 100 + k)) && !e.R8(pw + 0x1F8 + uint32_t(l * 100 + k)))
              boton(l, k);
          }
        }
        if (out != pad_actual) {
          REXLOG_INFO("dbz3 soliw: libpad n {} sub {} entrada {:04X} aciertos {} {}", n, sub, out,
                      e.R32(pw + 0x3C), e.R32(pw + 0x40));
          pad_actual = out;
        }
        return out;
      }
      return jugador;
    }
    if (auto it = guion.upper_bound(n); it != guion.begin()) pad_actual = std::prev(it)->second;
    return pad_actual;
  }

  // Guion de menus validado 13/11 (p3/trad5/ref.py): idioma, sin tarjeta, titulo, cargar -> continuar sin guardar, Dragon
  // Mission, mapa, Goku arriba hasta el nodo 0x7D0 y X; en el vblank 4201 la mision 6/0x7D5 (ESCRIBE). 0x10 en el 4500.
  static constexpr const char* kGuion =
      "320:4000,324:0,410:40,414:0,430:4000,434:0,2050:4000,2054:0,2150:4000,2154:0,2250:4000,2254:0,2290:40,2294:0,2330:4000,2334:0,2650:4000,2654:0,3260:4000,3264:0,3630:10,3654:0,3700:4000,3704:0,3900:4000,3904:0,4000:4000,4004:0,4100:4000,4104:0,4200:4000,4204:0";

  // La ISO EU de IW (el CD lee por LSN: hace falta la imagen, no la carpeta): DBZ3_IWTRAD_ISO; si no, BuscarIW(); si no es
  // la EU, cualquier .iso o .ruta de la carpeta de juegos de PS2 cuyo ELF sea SLES_553.47 (3 786 800 B). Una .ruta sin
  // "infinite world" en el nombre no cambia lo que BuscarIW() elige para el resto del port.
  // R23 IW-07: si no hay ISO EU vale la USA (SLUS_218.42, 3 784 752 B); la EU tiene prioridad.
  // IW-07: guion de la USA (sin pantalla de idioma; NTSC). Ver studies 5/ENTREGAS/R23/R23_IW_4.md.
  // Sacado con la traza DBZ3_TEST_IWTRAD_TAREAS (20/11): el aviso de tarjeta sale en el 108 (EU 398) y no hay idioma
  // (EU 320 fuera); desde la pantalla de titulo las esperas del original van por tiempo: EU 1985 -> USA 2386 y x1,2
  // (60/50). Mapa en el 4291, mision en el 5045, pantalla 0x10 en el 5250 (EU 4372).
  static constexpr const char* kGuionUsa =
      "120:40,124:0,140:4000,144:0,2464:4000,2468:0,2584:4000,2588:0,2704:4000,2708:0,2752:40,2756:0,2800:4000,2804:0,3184:4000,3188:0,3916:4000,3920:0,4360:10,4389:0,4444:4000,4448:0,4684:4000,4688:0,4804:4000,4808:0,4924:4000,4928:0,5044:4000,5048:0";
  static constexpr const char* kHastaUsa = "5524";
  static unsigned long long kEscribeVb() { return g_usa ? 5045 : 4201; }
  static std::filesystem::path IsoEu() {
    if (auto p = IsoTam(kElfEu); !p.empty()) return p;
    return IsoTam(kElfUsa);
  }
  static std::filesystem::path IsoTam(size_t tam) {
    std::error_code ec;
    auto eu = [&](const std::filesystem::path& p) {
      return !p.empty() && std::filesystem::is_regular_file(p, ec) && pasarela::LeerElf(p).size() == tam;
    };
    if (const char* ie = std::getenv("DBZ3_IWTRAD_ISO"); ie && *ie) {
      const auto p = std::filesystem::u8path(ie);
      return eu(p) ? p : std::filesystem::path{};
    }
    if (const auto p = pasarela::BuscarIW(); eu(p)) return p;
    std::vector<std::filesystem::path> cand;
    for (const auto& e : std::filesystem::directory_iterator(pasarela::CarpetaPs2(), ec)) {
      std::string n = e.path().filename().string();
      for (auto& ch : n) ch = char(std::tolower(uint8_t(ch)));
      if (n.size() > 5 && n.compare(n.size() - 5, 5, ".ruta") == 0) {
        std::ifstream f(e.path(), std::ios::binary);
        std::string t((std::istreambuf_iterator<char>(f)), {});
        while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back();
        if (!t.empty()) cand.push_back(std::filesystem::u8path(t));
      } else if (n.size() > 4 && n.compare(n.size() - 4, 4, ".iso") == 0) cand.push_back(e.path());
    }
    std::sort(cand.begin(), cand.end());
    for (const auto& p : cand) if (eu(p)) return p;
    return {};
  }

  bool IniciarElf() {                                               // oraculo.montar + arranque
    iso_bad = false;
    finish_observado = false;
    const std::filesystem::path ruta = IsoEu();
    iso.open(ruta, std::ios::binary);
    const auto elfb = pasarela::LeerElf(ruta);
    g_usa = elfb.size() == kElfUsa;
    if (g_usa) REXLOG_INFO("dbz3 iwtrad: ELF USA (SLUS_218.42), direcciones por tramos y reloj NTSC");
    if (!iso || elfb.empty() || !e.Cargar(elfb, 0x100000, U(0x49C600))) { REXLOG_WARN("dbz3 iwtrad: sin ISO/ELF de IW para arrancar ({})", ruta.string()); return false; }
    if (e.R32(0x149944) != 0x1000FFE6u) { REXLOG_WARN("dbz3 iwtrad: el ELF no es el EU/USA esperado"); return false; }
    if (g_usa) AplicarWs(e, kWsIwUsa, std::size(kWsIwUsa), "IW USA");
    else AplicarWs(e, kWsIwEu, std::size(kWsIwEu), "IW EU");
    e.Cero(U(0x49C600), U(0x5D9F00) - U(0x49C600));
    e.gp = U(kGpEu);
    e.cop0_status = 0x70030C13;
    e.r[28] = U(kGpEu); e.r[29] = Ee::kPila + 0x20000;
    Hilo m;
    m.tid = 1; m.func = 0x1498D0; m.pila = Ee::kPila; m.tam = 0xFF010; m.gp = U(kGpEu); m.prio = m.prio0 = 1; m.estado = READY;
    hilos[1] = m;
    auto lee = [](const char* var, const char* defecto) { const char* s = std::getenv(var); return std::string(s && *s ? s : defecto); };
    // guion de menus del oraculo (vblank:mascara hex) y escrituras (vblank:dir:valor hex); hasta = fin del modo rapido
    for (std::istringstream s(lee("DBZ3_IWTRAD_GUION", g_usa ? kGuionUsa : kGuion)); s.good();) {
      std::string x; std::getline(s, x, ',');
      if (auto p = x.find(':'); p != std::string::npos) guion[std::stoull(x.substr(0, p))] = uint16_t(std::stoul(x.substr(p + 1), nullptr, 16));
    }
    for (std::istringstream s(lee("DBZ3_IWTRAD_ESCRIBE", ([] { char q[64]; const uint32_t k = g_clave.load(); std::snprintf(q, sizeof q, "%llu:%x:%x,%llu:%x:%x", kEscribeVb(), U(0x5C0DC4), k - 0x7D0 + (k >= 0x7D4 ? 1 : 0), kEscribeVb(), U(0x5C0DC8), k); static std::string s; s = q; return s.c_str(); })())); s.good();) {
      std::string x; std::getline(s, x, ',');
      const auto p = x.find(':'), q = x.find(':', p + 1);
      if (p != std::string::npos && q != std::string::npos)
        escribe.insert({std::stoull(x.substr(0, p)), {uint32_t(std::stoul(x.substr(p + 1, q - p - 1), nullptr, 16)), uint32_t(std::stoul(x.substr(q + 1), nullptr, 16))}});
    }
    hasta = std::stoull(lee("DBZ3_IWTRAD_HASTA", g_usa ? kHastaUsa : "4600"));
    for (std::istringstream s(lee("DBZ3_IWTRAD_GUARDA", "")); s.good();) {   // pruebas: 'vblank=ruta;...' como oraculo --guardar
      std::string x; std::getline(s, x, ';');
      if (auto p = x.find('='); p != std::string::npos) guardas[std::stoull(x.substr(0, p))] = x.substr(p + 1);
    }
    PuentesElf();
    // Previo pasa hasta por referencia: al detectar el JAL que abre 876/0,
    // completa este campo y cede en la frontera normal del planificador.
    // R23 BOCA-02: la boca de los minijuegos la arranca el original (Char_PlayAttackVoiceAndMouth*, Minigame_PlayEventVoice...)
    // con MinigameMouthAnimations(personaje+0x640, clip, ignorar); se anota sin alterar el ELF y el port la pone en el B3.
    e.observadores[U(0x207080)] = [this](Ee& c) {
      const uint32_t ret = uint32_t(c.r[31]);
      if (g_clave.load() == 0x7DB && anim7db::escena.load()) {
        const int clip = int(c.r[5] & 0xFFFF);
        if (ret == (g_usa ? 0x34FEACu : 0x3503ACu) && (clip == 0xD6 || clip == 0xD7)) { voz_intro7[0] = 9 + (clip & 1); return; }
        if (ret == (g_usa ? 0x34FF04u : 0x350404u) && clip == 0xDF) { voz_intro7[1] = 0; return; }
      }
      PonerBoca(int(c.R32(uint32_t(c.r[4]) - 0x640 + 0x1380)), int(c.r[5] & 0xFFFF));
    };
    {
      std::lock_guard<std::mutex> lk(g_boca_mu);
      g_evbot.clear();
    }
    const char* hasta_manual = std::getenv("DBZ3_IWTRAD_HASTA");
    const bool arranque_fiel = anim7db::Solicitada(g_clave.load(), V2_7DB()) && !(hasta_manual && *hasta_manual);
    const bool eventos = REXCVAR_GET(dbz3_iw_r23_eventos);
    if (arranque_fiel || eventos) {
      e.observadores[U(0x2ED8F0)] = [this, arranque_fiel, eventos](Ee& c) {
        if (arranque_fiel && n < hasta && c.r[31] == (g_usa ? 0x3550F8u : 0x354EC8u) &&
            c.r[4] == 1 && c.r[5] == 0 && c.r[6] == 1) {
          hasta = n;
          REXLOG_INFO("dbz3 astra7db: paisaje 876/0; termina el precalentamiento en {}", n);
        }
        if (eventos) PonerEvBot('C', int(c.r[5] & 0xFFFF) | ((c.r[6] & 0xFF) ? 0 : 0x10000));
      };
    }
    if (eventos) {
      e.observadores[U(0x2EFA10)] = [](Ee& c) { PonerEvBot('V', int((c.r[4] & 0xFF) | ((c.r[5] & 0xFF) << 8) | ((c.r[6] & 0xFFFF) << 16))); };
    }
    if (g_clave.load() == 0x7E1)
      e.observadores[U(0x2E9AF0)] = [this](Ee&) { ++entradas[0x2E9AF0]; }; // Results_ComputeTotalsAndRewards, sin alterar el ELF
    REXLOG_INFO("dbz3 iwtrad: arranque desde la entrada del ELF (guion {} pulsaciones, rapido hasta el vblank {})", guion.size(), hasta);
    return true;
  }

  void PuentesElf() {                                               // fronteras del oraculo (las comparte IniciarB2)
    e.syscall = [this](Ee&) { SyscallNucleo(); };
    e.puente_fallo = true;
    auto v0 = [](Ee& c, int64_t v) { c.r[2] = uint64_t(v); };
    // fronteras del oraculo (IOP_FIJO, texto de depuracion, printf de CRI)
    e.puentes[0x10B658] = e.puentes[0x10B608] = [v0](Ee& c) { v0(c, 1); };
    e.puentes[0x10B128] = [v0](Ee& c) { v0(c, 0); };
    e.puentes[0x1034F8] = [](Ee& c) { c.r[2] = uint32_t(c.r[5]); };
    e.puentes[0x159B38] = [v0](Ee& c) { v0(c, 0); };
    // IOP por RPC (Iop.bind / Iop.call)
    e.puentes[0x105C90] = [this](Ee& c) {
      const uint32_t cli = uint32_t(c.r[4]), s = uint32_t(c.r[5]);
      sid[cli] = s;
      c.W32(cli + 0x24, 0x1F000000u | (s & 0xFFFF));
      c.r[2] = 0;
    };
    e.puentes[0x105E70] = [this](Ee& c) {
      const uint32_t cli = uint32_t(c.r[4]), fno = uint32_t(c.r[5]), env = uint32_t(c.r[7]);
      const uint32_t resp = uint32_t(c.r[9]), tam_resp = uint32_t(c.r[10]);
      const uint32_t s = sid.count(cli) ? sid[cli] : 0;
      if (resp && tam_resp) for (uint32_t k = 0; k < tam_resp; k += 4) c.W32(resp + k, 0);
      if (s == 0x80000003u) {
        if (fno == 1) { const uint32_t tam = (c.R32(env) + 0xFF) & ~0xFFu; c.W32(resp, iop_libre); iop_libre += tam; }
      } else if (s == 0x90000200u) {
        const uint32_t d = fno - 0x400;
        if (resp && (fno == 2 || d == 0 || d == 8 || d == 0x20 || d == 0x21 || d == 0x22)) { c.W32(resp, 0x80000 + 0x100 * sig_id); ++sig_id; }
      } else if (s == 0x11) {
        if (fno == 0x18) { snd_buf = 0; snd_nuevo = true; }
        else if (fno == 0x1A) { snd_buf = c.R32(env); snd_nuevo = true; }
        else if (fno == 0x19 && resp) { c.W32(resp, snd_nuevo ? snd_buf : 0xFFFFFFFFu); snd_nuevo = false; }
      } else if (s == 0x80000400u && resp) {
        if (fno == 0xFE) { c.W32(resp, 0); c.W32(resp + 4, 0x20E); c.W32(resp + 8, 0x20E); }
        else c.W32(resp, uint32_t(-10));
      }
      if (const uint32_t fin = uint32_t(c.r[11])) fines.push_back({fin, c.R32(uint32_t(c.r[29]))});
      c.r[2] = 0;
    };
    // CD (Cd): sectores de la ISO del jugador, al instante
    e.puentes[0x1113D0] = [this](Ee& c) {
      const uint32_t lsn = uint32_t(c.r[4]), k = uint32_t(c.r[5]);
      const auto d = Sector(lsn, k);
      if (d.size() != size_t(k) * 2048u) {   // PAQ_2/Paso 2: no publicar datos cortos como buenos
        e.Error("lectura de CD incompleta (LSN " + std::to_string(lsn) + ")");
        c.r[2] = 0;
        return;
      }
      c.Bytes(uint32_t(c.r[6]), d.data(), d.size());
      c.r[2] = 1;
    };
    e.puentes[0x110998] = e.puentes[0x111680] = [v0](Ee& c) { v0(c, 0); };
    e.puentes[0x1111B0] = [v0](Ee& c) { v0(c, 2); };
    e.puentes[0x110488] = [this](Ee& c) {
      const uint32_t fich = uint32_t(c.r[4]);
      std::string ruta;
      for (uint32_t a = uint32_t(c.r[5]); ruta.size() < 512; ++a) { const uint8_t ch = c.R8(a); if (!ch) break; ruta += char(ch); }
      uint32_t lsn, tam;
      std::string nom;
      if (!IsoBuscar(ruta, lsn, tam, nom)) { c.r[2] = 0; return; }
      c.W32(fich, lsn); c.W32(fich + 4, tam);
      for (uint32_t i = 0; i < 16; ++i) c.W8(fich + 8 + i, i < nom.size() ? uint8_t(nom[i]) : 0);
      c.r[2] = 1;
    };
    // Reloj.espera_cola (FUN_0013b630): mientras la cola de envios al GS no se vacia, lo pendiente o un vblank
    e.puentes[0x13B630] = [this](Ee& c) {
      const uint32_t canal = uint32_t(c.r[4]);
      en_espera = true;
      for (int i = 0; i < 100000; ++i) {
        if (c.R16(canal * 0x4C + U(0x4AD0C4)) == 0 && (c.R32(canal * 4 + U(0x49C8D0)) & 0x80000000u)) { en_espera = false; return; }
        Vblank();
        if (!c.error().empty()) break;
      }
      en_espera = false;
      if (c.error().empty()) c.Error("la cola de envios al GS no se vacia (FUN_0013b630)");
    };
  }

  // ---------------- B2-TRAD (13/11): el mismo arranque con el ELF de Budokai 2 PAL (SLES-51839) ----------------
  // Gemelo de tools/b2_oraculo/oraculo_b2.py; direcciones y motivos en studies 5/ENTREGAS/B2_TRAD_RELEVO.md. El nucleo,
  // el IOP, el CD y el mando son los de IW (PuentesElf + Comunes) mudados de direccion; solo cambian la cola de DMA y Sofdec.
  static constexpr const char* kGuionB2 =
      "100:40,104:0,120:40,124:0,140:40,144:0,160:40,164:0,180:4000,184:0,1200:10,1204:0,1220:4000,1224:0,2410:8,2414:0,2810:4000,2814:0,3210:4000,3214:0,3710:10,3714:0,3730:4000,3734:0,4310:4000,4314:0,5010:4000,5014:0,5090:4000,5094:0,5170:4000,5174:0,5250:4000,5254:0,5330:4000,5334:0,5410:4000,5414:0,5490:4000,5494:0,5570:4000,5574:0,5650:4000,5654:0,6010:8,6014:0,6800:8,6804:0,6950:8,6954:0,7100:4000,7104:0,7250:4000,7254:0,7400:4000,7404:0,7500:20,7530:0,7560:4000,7564:0";
  bool IniciarB2() {
    b2 = elf = true;
    tex_min = 0x2000;   // dibuja en el bufer FBP 0x40 (bloques 0x800-0xFFF) y lo copia a pantalla: esa copia no se pinta
    std::error_code ec;
    std::filesystem::path ruta;
    std::vector<uint8_t> elfb;
    for (const auto& en : std::filesystem::directory_iterator(pasarela::CarpetaPs2(), ec)) {
      std::string nom = en.path().extension().string();
      for (auto& ch : nom) ch = char(std::tolower(uint8_t(ch)));
      if (nom != ".iso" || !en.is_regular_file(ec)) continue;
      elfb = pasarela::LeerElf(en.path());
      if (elfb.size() == 2769712) { ruta = en.path(); break; }
    }
    if (ruta.empty()) { REXLOG_WARN("dbz3 b2trad: sin ISO de Budokai 2 PAL (SLES-51839) en la carpeta de juegos de PS2"); return false; }
    iso.open(ruta, std::ios::binary);
    if (!iso || !e.Cargar(elfb, 0x100000, 0x3A4100)) { REXLOG_WARN("dbz3 b2trad: no se puede cargar el ELF de {}", ruta.string()); return false; }
    AplicarWs(e, kWsB2, std::size(kWsB2), "B2 PAL");
    e.Cero(0x3A4100, 0x69F580 - 0x3A4100);
    e.gp = 0x3ABCF0;
    e.cop0_status = 0x70030C13;
    e.r[28] = 0x3ABCF0; e.r[29] = Ee::kPila + 0x20000;
    Hilo m;
    m.tid = 1; m.func = 0x150340; m.pila = Ee::kPila; m.tam = 0xFF010; m.gp = 0x3ABCF0; m.prio = m.prio0 = 1; m.estado = READY;
    hilos[1] = m;
    const char* g = std::getenv("DBZ3_B2TRAD_GUION");
    for (std::istringstream s(g && *g ? g : kGuionB2); s.good();) {
      std::string x; std::getline(s, x, ',');
      if (auto p = x.find(':'); p != std::string::npos) guion[std::stoull(x.substr(0, p))] = uint16_t(std::stoul(x.substr(p + 1), nullptr, 16));
    }
    const char* h = std::getenv("DBZ3_B2TRAD_HASTA");
    hasta = h && *h ? std::stoull(h) : 7700;                          // tablero de la fase I, seleccion de companero
    PuentesElf();
    Comunes();
    static constexpr std::pair<uint32_t, uint32_t> kMud[] = {         // IW EU -> B2 PAL
        {0x10B658, 0x112168}, {0x10B608, 0x112118}, {0x10B128, 0x111C38}, {0x1034F8, 0x10B0A8},   // RebootIop SyncIop LoadModule TTY
        {0x105C90, 0x10CCD8}, {0x105E70, 0x10CEA8},                                               // BindRpc CallRpc
        {0x1113D0, 0x117BF0}, {0x110998, 0x1171E0}, {0x111680, 0x117EA0}, {0x1111B0, 0x1179E8}, {0x110488, 0x116CA0},   // libcdvd
        {0x10EB18, 0x115400}, {0x10EB90, 0x115568}, {0x10EFB8, 0x115948}, {0x10ED88, 0x115708}, {0x10F4A8, 0x115E38},   // libpad
        {0x102698, 0x10A3D8}, {0x1025F0, 0x10A330}};                                              // cache
    auto iw = std::move(e.puentes);
    e.puentes.clear();
    for (const auto& par : kMud) {
      auto it = iw.find(par.first);
      if (it == iw.end()) { REXLOG_WARN("dbz3 b2trad: falta el puente de IW {:X}", par.first); return false; }
      e.puentes[par.second] = it->second;
    }
    e.puentes[0x10A518] = e.puentes[0x10A330];                        // 2.a copia de SyncDCache
    // pelicula Sofdec: "sin ranura" (-1), el camino que el propio juego trata (FUN_002a47a0 sigue sin pelicula)
    e.puentes[0x145F20] = [](Ee& c) { c.r[2] = ~0ull; };
    // FUN_001434c0(canal): espera activa a que se vacie la cola de DMA (IW 0x13B630, con las variables de B2)
    e.puentes[0x1434C0] = [this](Ee& c) {
      const uint32_t canal = uint32_t(c.r[4]);
      en_espera = true;
      for (int i = 0; i < 100000; ++i) {
        if (c.R16(canal * 0xC + 0x3B4344) == 0 && (c.R32(canal * 4 + 0x3A4420) & 0x80000000u)) { en_espera = false; return; }
        Vblank();
        if (!c.error().empty()) break;
      }
      en_espera = false;
      if (c.error().empty()) c.Error("la cola de envios al GS no se vacia (FUN_001434c0)");
    };
    // R17/TRAD2: la partida del port entra por el guardado ORIGINAL, no solo por el numero de mapa.
    // 238320 serializa los registros; 238870(3) los restaura y elige nuevo/reanudado con el turno guardado.
    e.observadores[0x238870] = [this](Ee& c) {
      if (fase_b2_cargada || !partida_b2.entrada) return;
      if (partida_b2.guardado_valido) {
        c.Bytes(0x69380C, partida_b2.guardado, sizeof partida_b2.guardado);
        c.r[4] = 3;
        guion.erase(guion.lower_bound(n), guion.end()); guion[n] = 0; pad_actual = 0;
        REXLOG_INFO("dbz3 b2trad: guardado ORIGINAL completo cargado (equipo, inventario, historial y tablero)");
        return;
      }
      // Estos bloques aun no han sido cargados al runtime: conservar el equipo/inventario
      // que 238870(3) iba a leer, antes de que 238320 los copie en sentido inverso.
      c.Bytes(0x698E50, c.Imagen().data() + 0x69380C, 0x1FA);
      c.Bytes(0x699050, c.Imagen().data() + 0x694074, 0x210);
      for (uint32_t i = 0; i < 30; ++i) c.W32(0x699260 + i * 4, partida_b2.banderas[i]);
      c.W16(0x6992D8, partida_b2.reanudada ? partida_b2.turno : 0);
      c.W8(0x6992DA, uint8_t(fase_b2)); c.W8(0x6992DB, partida_b2.musica);
      c.W8(0x6992DC, partida_b2.radar); c.W8(0x6992DD, partida_b2.bolas);
      if (partida_b2.reanudada) {
        // El resto del guion abre/acepta aliados de una partida nueva. En una carga
        // moveria las fichas restauradas antes de que el jugador recibiera el mando.
        guion.erase(guion.lower_bound(n), guion.end());
        guion[n] = 0; pad_actual = 0;
        c.Bytes(0x69B7A0, partida_b2.piezas, sizeof partida_b2.piezas);
        c.Bytes(0x69BC20, partida_b2.objetos, sizeof partida_b2.objetos);
        c.Bytes(0x69BC60, partida_b2.casillas, sizeof partida_b2.casillas);
        c.Bytes(c.gp - 0x72C8, partida_b2.bolas_fase, sizeof partida_b2.bolas_fase);
      }
      if (!c.Anidada(0x238320, {}, 2000000)) return;
      c.r[4] = 3;
      REXLOG_INFO("dbz3 b2trad: guardado original compartido: fase {} turno {} reanudada {} bolas {:#x}",
                  fase_b2 + 1, partida_b2.reanudada ? partida_b2.turno : 0, partida_b2.reanudada, partida_b2.bolas);
    };
    // FUN_00238870 copia el guardado antes de llamar a FUN_002350c0. La fase debe estar puesta ANTES de que esta
    // ultima lea las tablas 0x377AF0/0x377B70/0x377C50 (b2.c:188737-188753), no con los recursos ya cargados.
    // Solo se establece la entrada de la sesion; las transiciones posteriores siguen siendo del ELF.
    e.observadores[0x2350C0] = [this](Ee& c) {
      if (fase_b2_cargada) return;
      fase_b2_cargada = true;
      if (partida_b2.reanudada && !partida_b2.guardado_valido) {
        const auto* ram = c.Imagen().data();
        const bool ok = c.R16(0x6992D8) == partida_b2.turno && c.R8(0x6992DD) == partida_b2.bolas &&
            std::memcmp(ram + 0x69B7A0, partida_b2.piezas, sizeof partida_b2.piezas) == 0 &&
            std::memcmp(ram + 0x69BC20, partida_b2.objetos, sizeof partida_b2.objetos) == 0 &&
            std::memcmp(ram + 0x69BC60, partida_b2.casillas, sizeof partida_b2.casillas) == 0 &&
            std::memcmp(ram + c.gp - 0x72C8, partida_b2.bolas_fase, sizeof partida_b2.bolas_fase) == 0;
        REXLOG_INFO("dbz3 b2trad: restauracion original verificada {} (32 piezas, objetos, casillas, Bolas y turno)", ok);
        if (!ok) c.Error("B2: el guardado original no coincide con la partida de entrada");
      }
      c.W8(0x6992DA, uint8_t(fase_b2));
      REXLOG_INFO("dbz3 b2trad: carga original de fase {} (antes de leer los recursos)", fase_b2 + 1);
    };
    // Frontera nativa: la preparacion, T4 y desmontaje del tablero los ejecuta el ELF.
    // Su modo 7 se atiende con el combate HD; no se arranca un segundo combate PS2.
    e.observadores[0x279050] = [this](Ee& c) {
      jugador_b2 = int(c.r[4]); rival_b2 = int(c.r[5]); casilla_b2 = int(c.r[6]);
      contacto_hd_b2 = int32_t(c.R32(c.gp - 0x7274)); // iGpffff8d8c, fila T4 que debe consumir T5
    };
    // El combate externo desmonta/recarga el tablero. Conservar el contacto junto
    // al ganador HD: 27F3A0 necesita la fila T4 anterior, no la de otro contacto
    // producido al recolocar las fichas. El ELF decide codigo 1/2/3/4 y banderas,
    // selecciona T5 y ejecuta premios y orden 41/42/43 sin una victoria inventada.
    e.observadores[0x27F3A0] = [this](Ee& c) {
      if (!retorno_t5_b2) return;
      retorno_t5_b2 = false;
      const int anterior = int32_t(c.R32(c.gp - 0x7274));
      c.W32(c.gp - 0x7274, uint32_t(contacto_hd_b2));
      REXLOG_INFO("dbz3 b2trad: puente HD T5: contacto {} codigo {} barras {} (fila antes {})", contacto_hd_b2,
                  int(c.r[4]), c.R8(0x69B7A1 + uint32_t(retorno_perdedor_b2) * 0x24), anterior);
    };
    e.observadores[0x231AD0] = [this](Ee& c) {
      if (c.R32(c.gp - 0x7E80) != 7) return;
      principal_b2 = uint32_t(c.r[4]);
      combate_b2 = true;
      c.W32(c.gp - 0x7E80, ~0u); // modo externo en espera; 231AD0 no carga recursos PS2
      REXLOG_INFO("dbz3 b2trad: combate HD solicitado por original: piezas {} / {}, casilla {}", jugador_b2, rival_b2, casilla_b2);
    };
    e.observadores[0x294660] = [this](Ee& c) {
      if (c.R32(uint32_t(c.r[4]) + 0x40) != 0) return;
      victoria_b2 = int(c.R8(0x6992DA)) + 1;
      REXLOG_INFO("dbz3 b2trad: victoria ORIGINAL fase {} (FUN_00294660)", victoria_b2);
    };
    e.observadores[0x294510] = [this](Ee& c) {
      if (c.R32(uint32_t(c.r[4]) + 0x40) != 0) return;
      victoria_b2 = int(c.R8(0x6992DA)) + 1;
      REXLOG_INFO("dbz3 b2trad: victoria ORIGINAL fase {} final (FUN_00294510)", victoria_b2);
    };
    // El piloto solamente cambia el cursor y el flanco del mando; 236EA0 valida la
    // vecindad, consume la accion y ejecuta contacto/busqueda/guiones originales.
    for (uint32_t fn : {0x237990u, 0x236EA0u})
      e.observadores[fn] = [this, fn](Ee& c) {
        if (!piloto_boton_b2 || !std::getenv("DBZ3_TEST_MISIONES")) return;
        const uint32_t w = uint32_t(c.r[4]);
        if (fn == 0x237990 && c.R32(w + 0x30) < 26) return;
        if (fn == 0x236EA0 && piloto_destino_b2 > 0) c.W32(w + 0x3C, uint32_t(piloto_destino_b2));
        const uint32_t pad = c.R32(0x693738) * 0xB8 + 0x3B43AE;
        c.W16(pad, piloto_boton_b2);
        piloto_boton_b2 = 0;
      };
    for (uint32_t fn : {0x2A1E30u, 0x2A1640u})
      e.observadores[fn] = [this, fn](Ee& c) {
        // R18 dsB2_VINETA: con el estado compartido el tablero del port ya no avanza los guiones,
        // asi que el aviso de la eleccion de aliados se toma del original (mismas frases que el port).
        {
          static uint64_t entrada_aviso = 0;
          static int abierta = -1, marcadas_antes = -1, estado_antes = -1;
          if (entrada_aviso != partida_b2.entrada) {
            entrada_aviso = partida_b2.entrada;
            abierta = -1; marcadas_antes = -1; estado_antes = -1;
          }
          const uint32_t wa = uint32_t(c.r[4]);
          const int elegir = int(c.R8(c.gp - 0x70D4)), total = int(c.R32(c.gp - 0x70D0));
          auto contar = [&](int& ult) {
            int n = 0; ult = -1;
            for (int r = 0; r < total && r < 32; ++r)
              if (c.R8(0x69D030 + uint32_t(r) * 4)) { ++n; ult = r; }
            return n;
          };
          if (fn == 0x2A1E30u) {
            if (abierta < 0) {
              abierta = 1;
              REXLOG_INFO("dbz3 tablero b2: script 301: eleccion de {} aliado(s) entre {} (original)", elegir, total);
            }
            int ult = -1;
            const int n = contar(ult);
            // La ultima marca cambia de funcion (2A1640) sin otra llamada aqui: el recuento de
            // fichas marcadas da el "quedan" real en cada punto, tambien al llegar a 0.
            if (marcadas_antes >= 0 && n != marcadas_antes) {
              const int id = ult >= 0 ? int(c.R8(0x69D031 + uint32_t(ult) * 4)) : -1;
              REXLOG_INFO("dbz3 tablero b2: aliado marcado: {}, quedan {}", id, std::max(0, elegir - n));
            }
            marcadas_antes = n;
          } else {
            const int estado = int(c.R32(wa + 0x30));
            // R18 dsB2_VINETA2: la ultima marca se hace dentro de la llamada de 2A1E30 que pasa a la
            // pregunta; desde entonces ya no se entra en 2A1E30 y el aviso "quedan 0" que espera la
            // prueba no salia nunca. Se cuenta al entrar en el estado 0 de la pregunta, con las
            // fichas ya marcadas del todo.
            if (estado == 0) {
              int ult = -1;
              const int n = contar(ult);
              if (marcadas_antes >= 0 && n != marcadas_antes) {
                const int id = ult >= 0 ? int(c.R8(0x69D031 + uint32_t(ult) * 4)) : -1;
                REXLOG_INFO("dbz3 tablero b2: aliado marcado: {}, quedan {}", id, std::max(0, elegir - n));
              }
              marcadas_antes = n;
            }
            if (estado == 4 && estado_antes != 4) {
              int ult = -1;
              const int n = contar(ult);
              // Al confirmar Si el original desmarca una ficha: solo se avisa si el recuento crece,
              // para no publicar un "quedan" falso despues del "quedan 0".
              if (n > marcadas_antes) {
                const int id = ult >= 0 ? int(c.R8(0x69D031 + uint32_t(ult) * 4)) : -1;
                REXLOG_INFO("dbz3 tablero b2: aliado marcado: {}, quedan {}", id, std::max(0, elegir - n));
              }
              REXLOG_INFO("dbz3 tablero b2: aliado elegido (confirmacion original)");
              marcadas_antes = n;
            }
            estado_antes = estado;
          }
        }
        // R18 dsB2_VINETA: el piloto de aliados tambien puede pedirse solo para la eleccion, sin el
        // modo automatico del tablero (las teclas manuales son demasiado cortas para el interprete).
        // Nunca durante el arranque previo: alli manda el guion de 60 pulsaciones.
        if (!piloto_auto_b2 && (!std::getenv("DBZ3_TEST_B2_ALIADOS") || !listo)) return;
        const uint32_t w = uint32_t(c.r[4]), pad = c.R32(0x693738) * 0xB8;
        c.W16(pad + 0x3B43AE, 0); c.W16(pad + 0x3B43B2, 0);
        if (n % 24 != 0) return;
        if (fn == 0x2A1640) {
          if (c.R32(w + 0x30) != 2) return;
          // La pregunta ORIGINAL arranca en No; elegir Si con Arriba antes de confirmar.
          if (c.R32(w + 0x44) == 1) c.W16(pad + 0x3B43B2, 0x10);
          else c.W16(pad + 0x3B43AE, 0x4000);
        } else {
          const uint32_t fila = c.R32(w + 0x40);
          if (fila < 32 && c.R8(0x69D030 + fila * 4)) c.W16(pad + 0x3B43B2, 0x40);
          else c.W16(pad + 0x3B43AE, 0x4000);
        }
      };
    // Figura 3D del jugador (lista de dibujo del tablero, tipo 4, FUN_0023a050): se apunta la ORDEN (posicion en pantalla,
    // animacion banco << 16 | clip, mezcla de pose, cuadro). El port usa el modelo pequeno nativo CMN 337/338.
    e.observadores[0x23A050] = [this](Ee& c) {
      const uint32_t a = uint32_t(c.r[4]);
      figura.n = n; figura.x = c.Rf(a); figura.y = c.Rf(a + 4);
      figura.anim = c.R32(a + 0x10); figura.esc = c.Rf(a + 0x14); figura.cuadro = c.Rf(a + 0x18);
      FiguraTableroB2 f;
      f.x = figura.x; f.y = figura.y; f.anim = figura.anim; f.escala = figura.esc; f.cuadro = figura.cuadro;
      f.giro = float(int16_t(c.R32(a + 0x20))) * (6.283185307f / 65536.0f);   // FUN_0023a050 -> FUN_0013c7c0
      f.atenuacion = c.Rf(a + 0x1C);   // FUN_00133560, no escala geometrica
      f.distancia = -c.Rf(c.gp - 0x7E60);   // uGpffff81a0, -63 por defecto
      f.variante = (c.R32(0x6992DC) & 4) != 0;   // orden 82: CMN 339/340
      figuras_b2.push_back(f);
    };
    // FUN_0023a9f0 recorre y vacia UNA lista por cuadro: no acumular modelos de listas anteriores.
    e.observadores[0x23A9F0] = [this](Ee&) { figuras_b2.clear(); figuras_n = n; };
    // FUN_0023a9f0 vacia la cola 2D y despues limpia su pool (b2.c:191669-191670).
    // Ese punto separa tablero/piezas de los dialogos posteriores sin adelantar ni repetir ninguna funcion original.
    e.observadores[0x123C18] = [this](Ee& c) {
      const uint32_t vuelta = uint32_t(c.r[31]);
      if (vuelta >= 0x23A9F0 && vuelta < 0x23AB60 && uint32_t(c.r[6]) == 32000 &&
          uint32_t(c.r[4]) == c.R32(c.gp - 0x7708) && cuadro_dma == n) {
        capas_b2[cuadro_dma] = {Global().prims.size(), figuras_b2};
        // puGpffff8480 (FUN_00139310/139420): fin del paquete construido; la DMA puede llegar DESPUES.
        // Contar las primitivas ya recibidas aqui dejaba las figuras debajo del propio mapa.
        GlobalDma().corte_direccion = c.R32(c.gp - 0x7B80) & 0x1FFFFFFFu;
        GlobalDma().corte_primitivas = size_t(-1);
        static bool dicho = false;
        if (!dicho && !figuras_b2.empty()) {
          dicho = true;
          REXLOG_INFO("dbz3 b2trad: capa original del tablero: {} figuras tras {} primitivas, antes del HUD/dialogos",
                      figuras_b2.size(), Global().prims.size());
        }
      }
    };
    e.observadores[0x23A2E0] = [this](Ee& c) {
      const uint32_t a = uint32_t(c.r[4]), slot = c.R32(a + 0x28);
      // FUN_002766d0: estado[slot]*0x50, +0x48 = slot; ficha[slot]*0x24, +4 = personaje.
      const uint32_t estados = c.R32(c.gp - 0x72D0);
      if (slot >= 32 || !estados || estados > Ee::kRam - 0xA00 || !c.R32(0x6994C0 + slot * 4) ||
          c.R16(estados + slot * 0x50 + 0x48) != slot) return;
      const int id = int16_t(c.R16(0x69B7A0 + slot * 0x24 + 4));
      if (id < 0 || id > 40 || id == 22) return;
      FiguraTableroB2 f;
      f.id = id; f.tipo = 3; f.slot = int(slot); f.anim = c.R32(a + 0x10); f.cuadro = c.Rf(a + 0x1C);
      f.x = c.Rf(a); f.y = c.Rf(a + 4);
      f.giro = float(int16_t(c.R32(a + 0x24))) * (6.283185307f / 65536.0f);   // +0x20 es transparencia, no giro
      f.idx = int(c.R32(a + 0x18)) / 2;   // FUN_0023bbe0: indice ENTERO del retrato << 1
      f.atenuacion = c.Rf(a + 0x20);
      f.distancia = 50;   // FUN_0023a2e0 -> FUN_0013cdf0(0,0,-50)
      figuras_b2.push_back(f);
    };
    // FUN_002762b0 (id 22) -> 23bac0/23b900 -> 23a620: las capas AMA originales
    // del Saibaman se ejecutan enteras. Sustituirlas por un luchador B3 borraba su hoyo y brote.
    e.observadores[0x12D100] = [this](Ee& c) {
      const uint32_t recurso = uint32_t(c.r[4]);
      if (n >= 9000 && (recurso == 619 || recurso == 827 || recurso == 844))
        REXLOG_INFO("dbz3 b2trad: recurso original de contacto CMN {} en el vblank {}", recurso, n);
    };
    // Observacion del flujo ORIGINAL: no sustituye funciones, no completa acciones ni escribe el estado.
    // FUN_278EE0/278F90 recargan rivales/aliados, 279050 prepara el combate y 27A180 consume una accion.
    if (std::getenv("DBZ3_TEST_B2TRAD_MEDIR"))
      for (const uint32_t fn : {0x278EE0u, 0x278F90u, 0x279050u, 0x279E30u, 0x27A180u})
        e.observadores[fn] = [this, fn](Ee& c) {
          REXLOG_INFO("dbz3 b2trad flujo: funcion {:X} vblank {} a0 {} a1 {} a2 {}",
                      fn, n, uint32_t(c.r[4]), uint32_t(c.r[5]), uint32_t(c.r[6]));
        };
    if (std::getenv("DBZ3_TEST_B2TRAD_MEDIR"))
      for (const uint32_t fn : {0x235780u, 0x235FD0u, 0x236420u, 0x237990u, 0x237F70u, 0x236830u})
        e.observadores[fn] = [this, fn, ultimo = ~0u](Ee& c) mutable {
          const uint32_t estado = c.R32(uint32_t(c.r[4]) + 0x30);
          if (estado == ultimo) return;
          ultimo = estado;
          // Estado y contador leidos; las transiciones las sigue ejecutando el ELF.
          REXLOG_INFO("dbz3 b2trad estado: funcion {:X} paso {} vblank {} turno {} bando {:X}",
                      fn, estado, n, c.R16(0x6992D8), c.R32(0x3A3E80));   // gp + int16_t(0x8190)
        };
    REXLOG_INFO("dbz3 b2trad: arranque desde la entrada del ELF de B2 (guion {} pulsaciones, rapido hasta el vblank {})", guion.size(), hasta);
    if (std::getenv("DBZ3_TEST_B2_ORDENES"))
      e.observadores[0x295F20] = [this, ultimo = ~0u](Ee& c) mutable {
        const uint32_t pc = c.R32(c.gp - 0x7238), pendiente = c.R32(c.gp - 0x7248);
        if (pc == ultimo) return;
        ultimo = pc;
        REXLOG_INFO("dbz3 b2trad: guion original {} pc {:X} orden anterior {} siguiente {} espera {} piloto {}",
                    c.R32(c.gp - 0x7228), pc, pc >= 24 ? c.R32(pc - 20) : 0, c.R32(pc + 4), pendiente, piloto_auto_b2);
      };
    return true;
  }

  void Syscall() {
    const int64_t num = int64_t(e.r[3]);
    const uint64_t k = uint64_t(num < 0 ? -num : num);
    const uint32_t a0 = uint32_t(e.r[4]);
    e.r[2] = (k >= 0x41 && k <= 0x46) ? uint64_t(int64_t(int32_t(a0))) : 0;
    if (k == 0x32) { Tick(); e.r[2] = 0; }
  }

  bool Iniciar() {
    // TRAD/5c: de serie arranca desde el ELF si hay ISO EU; DBZ3_IWTRAD_DESDE=punto (pruebas) usa el punto m7d5
    const char* d = std::getenv("DBZ3_IWTRAD_DESDE");
    const bool punto = d && std::string(d) == "punto";
    if (!punto && ((d && std::string(d) == "elf") || !IsoEu().empty())) {
      elf = true;
      if (!IniciarElf()) return false;
      Comunes();
      return true;
    }
    const char* env = std::getenv("DBZ3_IWTRAD_PUNTO");
    if (!punto && !(env && *env)) return false;   // sin ISO: nunca cargar material de desarrollo implicitamente
    const std::string base = env && *env ? env : "E:\\DBZ3HD_trabajo\\iwfin\\oraculo_puntos\\m7d5";
    std::ifstream fr(base + ".ram", std::ios::binary), ft(base + ".txt");
    if (!fr || !ft) { REXLOG_WARN("dbz3 iwtrad: sin punto de partida ({}.ram/.txt)", base); return false; }
    const auto elf = pasarela::LeerElf(pasarela::BuscarIW());
    if (elf.empty() || !e.Cargar(elf, 0x100000, 0x49C600)) { REXLOG_WARN("dbz3 iwtrad: sin ELF de IW"); return false; }
    std::vector<uint8_t> ram((std::istreambuf_iterator<char>(fr)), {});
    e.CargarImagen(ram.data(), ram.size());
    for (std::string l; std::getline(ft, l);) {
      std::istringstream s(l);
      std::string q;
      s >> q;
      if (q == "n") s >> n;
      else if (q == "vblanks") s >> vblanks;
      else if (q == "intc" || q == "dmac") {
        int c; uint32_t f, arg;
        s >> c >> std::hex >> f >> arg;
        (q == "intc" ? intc : dmac)[c].push_back({f, arg});
      } else if (q == "reg") { uint32_t a, v; s >> std::hex >> a >> v; reg[a] = v; }
    }
    // el salto atras del bucle -> ra = retorno; jr ra (0x149948/4C son relleno): una vuelta del cuerpo por llamada
    if (e.R32(0x149944) != 0x1000FFE6u) { REXLOG_WARN("dbz3 iwtrad: el ELF no es el EU esperado"); return false; }
    e.W32(0x149944, 0x3C1F0000u | (Ee::kRetorno >> 16));
    e.W32(0x149948, 0x37FF0000u | (Ee::kRetorno & 0xFFFF));
    e.W32(0x14994C, 0x03E00008u);
    e.gp = kGpEu;
    if (std::getenv("DBZ3_IWTRAD_RAMVOLCADO"))                      // pruebas: entradas a funciones de la cadena 3D
      for (uint32_t f : {0x139DC0u, 0x1384F0u, 0x1324A0u, 0x1327F0u}) e.observadores[f] = [this, f](Ee&) { ++entradas[f]; };
    e.cop0_status = 0x70030C13;                                     // como oraculo.montar: IE/EIE activos
    e.syscall = [this](Ee&) { Syscall(); };
    Comunes();
    if (std::ifstream fv(base + ".vram", std::ios::binary); fv)        // pruebas: VRAM de partida (las texturas cargadas antes)
      fv.read(reinterpret_cast<char*>(vram.m.data()), std::streamsize(vram.m.size() * 4));
    REXLOG_INFO("dbz3 iwtrad: bucle principal del IW listo desde {} (vblank {})", base, n);
    return true;
  }

  void Comunes() {                                                  // hardware y libpad (los dos arranques)
    e.io_leer = [this](Ee&, uint32_t a, uint64_t& v) { return Leer(a, v); };
    e.io_escribir = [this](Ee&, uint32_t a, uint64_t v) { return Escribir(a, v); };
    auto v0 = [](Ee& c, int64_t v) { c.r[2] = uint64_t(v); };
    e.puentes[0x10EB18] = [this, v0](Ee& c) {                       // scePadRead: 32 bytes, botones activos a nivel bajo
      const uint32_t puerto = uint32_t(c.r[4]), datos = uint32_t(c.r[6]);
      const uint16_t b = uint16_t(puerto == 0 ? ~Pad() : 0xFFFF);
      const uint8_t bs[8] = {0x00, 0x73, uint8_t(b & 0xFF), uint8_t(b >> 8), 0x80, 0x80, 0x80, 0x80};
      for (uint32_t i = 0; i < 32; ++i) c.W8(datos + i, i < 8 ? bs[i] : 0);
      v0(c, 32);
    };
    e.puentes[0x10EB90] = [v0](Ee& c) { v0(c, uint32_t(c.r[4]) == 0 ? 6 : 0); };          // scePadGetState
    e.puentes[0x10EFB8] = [v0](Ee& c) {                                                     // scePadInfoMode
      const uint32_t puerto = uint32_t(c.r[4]), term = uint32_t(c.r[6]);
      const int64_t offs = int64_t(c.r[7]);
      int64_t v = 0;
      if (!puerto) v = term == 1 || term == 2 ? 7 : term == 3 ? 1 : term == 4 ? (offs < 0 ? 2 : (offs & 1) ? 7 : 4) : 0;
      v0(c, v);
    };
    e.puentes[0x10ED88] = e.puentes[0x10F4A8] = [v0](Ee& c) { v0(c, 0); };                  // InfoAct / InfoPressMode
    e.puentes[0x102698] = e.puentes[0x1025F0] = [v0](Ee& c) { v0(c, 0); };                  // Sync/InvalidDCache
    // Sofdec (oraculo.IOP_FIJO, TRAD/5b): pelicula/voz "terminada": ranura 0 sin crear el mwPly (0x4AD160 sigue a 0, el
    // envoltorio no hace nada) y estado FUN_0013db50 = 3 (MWE_PLY_STAT_PLAYEND), el que los llamadores tratan como fin
    e.puentes[0x13D4D0] = [v0](Ee& c) { v0(c, 0); };
    e.puentes[0x13DB50] = [v0](Ee& c) { v0(c, 3); };
  }
};

std::unique_ptr<Bucle>& Hueco() { static auto p = std::make_unique<Bucle>(); return p; }
Bucle& B() { return *Hueco(); }
std::unique_ptr<Bucle>& HuecoB2() { static auto p = std::make_unique<Bucle>(); return p; }
Bucle& BB2() { return *HuecoB2(); }   // B2-TRAD: el de Budokai 2, aparte del de IW

class Panel : public iwgs::Drawer {
 protected:
  bool Visible() const override {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_dibujo) return false;
    return g_dibujo->alto == 512 ? B2Fresco() : Activo() && iwmini::Activo();
  }
  void OnDraw(ImGuiIO& io) override {
    std::shared_ptr<const Dibujo> d;
    { std::lock_guard<std::mutex> lk(g_mu); d = g_dibujo; }
    if (!d) return;
    if (d->alto == 512 ? !B2Fresco() : !(Activo() && iwmini::Activo())) return;
    ++n_;
    std::vector<ImTextureID> ids(d->tex.size(), ImTextureID{});
    std::vector<bool> necesarias(d->tex.size());
    for (const auto& q : d->q) if (q.tex >= 0) necesarias[size_t(q.tex)] = true;
    // dsB2_BORDE (R16): el tablero de B2 (alto 512) se pinta con CLAMP, como lo lee el GS dentro de cada textura. Con
    // REPEAT, las UV que caen en el borde (el mapa usa 0,5..255,5 de 256; las capas del Saibaman, 0..96 de 128) mezclan
    // medio texel con el borde OPUESTO de la textura (negro) y dejan una linea oscura de 1 px en cada quad. El IW se
    // queda con REPEAT (sus capas de fondo si barren la textura). DBZ3_TEST_B2_SIN_CLAMP=1 para comparar el defecto.
    const bool clamp_b2 = d->alto == 512 && std::getenv("DBZ3_TEST_B2_SIN_CLAMP") == nullptr;
    for (size_t i = 0; i < d->tex.size(); ++i) {
      if (!necesarias[i]) continue;  // el atlas entero no se sube si sus quads solo leen recortes pequenos
      auto& e = tex_[{d->tex[i].first, clamp_b2}];
      if (!e.t) { e.t = ps2::Crear(*d->tex[i].second, false, !clamp_b2); RegistrarTextura(e.t.id(), *d->tex[i].second); }
      e.uso = n_;
      ids[i] = e.t.id();
    }
    for (auto it = tex_.begin(); it != tex_.end();) it = n_ - it->second.uso > 120 ? tex_.erase(it) : std::next(it);
    // Las tandas 3D de Escena crean ventanas: un fondo en Foreground las taparia al superar 65535 vertices.
    const bool ventana_b2 = d->alto == 512;
    if (ventana_b2) {
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::Begin("##b2trad_lista", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
      ImGui::BringWindowToDisplayFront(ImGui::FindWindowByName("##b2trad_lista"));
    }
    ImDrawList* dl = ventana_b2 ? ImGui::GetWindowDrawList() : ImGui::GetForegroundDrawList();
    float caja = io.DisplaySize.x, ox = 0;
    CajaX(io.DisplaySize.x, io.DisplaySize.y, caja, ox);   // 2D en 4:3 centrado: el original no se estira a 16:9
    const float sx = caja / 512.0f, sy = io.DisplaySize.y / d->alto;
    static int n_panel = 0;
    ++n_panel;
    int texq = 0;
    for (const auto& q : d->q) if (q.tex >= 0) ++texq;
    // dsB2_MAPA3 (R17): un cuadro del original sin una sola textura (solo la copia del fotograma, que el traductor
    // descarta y quads de color) no es tablero. Antes se pintaba el fondo negro y encima nada: el mapa del port
    // quedaba tapado en 16:9. Se deja ver el tablero del port en vez de un negro.
    if (ventana_b2 && texq == 0 && d->figuras_b2.empty() && !d->fig) {
      if (std::getenv("DBZ3_TEST_B2_MAPA_TRAZA"))
        REXLOG_INFO("dbz3 b2mapa capa3: cuadro {} sin textura ni figuras; no se pinta la capa (se ve el tablero del port) q {} fig_antes {}",
                    n_panel, int(d->q.size()), int(d->figuras_antes));
      ImGui::End();
      return;
    }
    if (std::getenv("DBZ3_TEST_B2_MAPA_TRAZA")) {   // dsB2_MAPA: geometria de la capa traducida y quads con textura
      if (n_panel % 30 == 0)
        REXLOG_INFO("dbz3 b2mapa capa2: cuadro {} q {} tex {} ventana {} caja {:.1f} ox {:.1f} sx {:.4f} sy {:.4f} W {:.0f}x{:.0f} alto {:.0f} fig_antes {}",
                    n_panel, int(d->q.size()), texq, ventana_b2 ? 1 : 0, caja, ox, sx, sy, io.DisplaySize.x,
                    io.DisplaySize.y, d->alto, int(d->figuras_antes));
    }
    if (ventana_b2) {
      // El tablero sustituye la imagen anterior entera, tambien fuera del rectangulo 4:3.
      // Los sprites del mapa pueden sobrepasarlo: no deben descubrir la recreacion de debajo.
      dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32_BLACK);
      dl->PushClipRect(ImVec2(ox, 0), ImVec2(ox + caja, io.DisplaySize.y), false);
    }
    int modo = -1;
    auto pintar_figuras = [&] {
      dl->AddCallback(rex::ui::kImDrawBlendAlpha, nullptr);
      if (g_figuras_b2 && !d->figuras_b2.empty()) g_figuras_b2(d->figuras_b2);
      else if (d->fig && g_figura_b2) g_figura_b2(d->fx, d->fy, d->fanim, d->fcuadro, d->fesc);
    };
    auto capa_superior = [&] {
      pintar_figuras();
      // Escena::Pintar abre tandas de 65535 vertices. Su orden debe renovarse tambien cuando
      // terminan de cargarse modelos: el HUD ya existia antes que esas nuevas ventanas.
      for (int i = 0;; ++i) {
        char nombre[32];
        std::snprintf(nombre, sizeof nombre, "##dt_tanda%d", i);
        ImGuiWindow* w = ImGui::FindWindowByName(nombre);
        if (!w || w->LastFrameActive != ImGui::GetFrameCount()) break;
        ImGui::BringWindowToDisplayFront(w);
      }
      dl->PopClipRect();
      ImGui::End();
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::Begin("##b2trad_encima", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
      ImGui::BringWindowToDisplayFront(ImGui::FindWindowByName("##b2trad_encima"));
      dl = ImGui::GetWindowDrawList();
      dl->PushClipRect(ImVec2(ox, 0), ImVec2(ox + caja, io.DisplaySize.y), false);
      modo = -1;
    };
    for (size_t i = 0; i < d->q.size(); ++i) {
      if (ventana_b2 && i == d->figuras_antes) {
        capa_superior();
        // El comic HD pone su propia caja y texto. No incrustar detras una seleccion de aliados o un dialogo
        // de la otra sesion: conservar solo el mapa/piezas hasta la frontera DMA original de esta lista.
        if (g_b2_comic.load(std::memory_order_relaxed)) break;
      }
      const auto& q = d->q[i];
      if (!ventana_b2) dl->PushClipRect(ImVec2(q.borde_velo7 < 0 ? 0.f : ox + q.cx0 * sx, q.cy0 * sy),
                                      ImVec2(q.borde_velo7 > 0 ? io.DisplaySize.x : ox + q.cx1 * sx, q.cy1 * sy), true);
      if (q.mezcla != modo) {
        modo = q.mezcla;
        dl->AddCallback(modo == 1 ? rex::ui::kImDrawBlendAdditive : modo == 2 ? iwgs::Resta : rex::ui::kImDrawBlendAlpha, nullptr);
      }
      const ImVec2 a(q.borde_velo7 < 0 ? 0.f : ox + q.x0 * sx, q.y0 * sy),
                   b(q.borde_velo7 > 0 ? io.DisplaySize.x : ox + q.x1 * sx, q.y1 * sy);
      // dsB2_BORDE (R16): medio texel dentro del recorte. El GS indexa texeles por su borde (u = ancho = borde derecho);
      // D3D/DXGI muestrea con el centro en (u*W - 0,5), asi que el ultimo pixel del quad mezcla el ultimo texel del
      // recorte con el SIGUIENTE de la hoja (negro en las capas del Saibaman y del terreno) y deja una linea de 1 px.
      // Mover cada UV medio texel hacia dentro la quita sin tocar el arte. R23 G3: tambien los sprites del IW. En el GS
      // el primer pixel de un sprite cae ya dentro de su rectangulo de UV (sin centro de pixel); en D3D el borde
      // mezclaba ~13 % del texel vecino del atlas y dejaba el rectangulo gris fino de 7D3/7DE (START!, contador).
      float eu = 0, ev = 0;
      if ((ventana_b2 || !q.tri) && q.tex >= 0) {
        const auto& im = *d->tex[size_t(q.tex)].second;
        eu = im.w > 0 ? 0.5f / im.w : 0.0f;
        ev = im.h > 0 ? 0.5f / im.h : 0.0f;
        // un trozo estirado de menos de 1 texel (marcos de 9 trozos del IW) ya muestrea igual que el GS
        if (!ventana_b2 && std::fabs(q.u1 - q.u0) * im.w < 1.0f) eu = 0;
        if (!ventana_b2 && std::fabs(q.v1 - q.v0) * im.h < 1.0f) ev = 0;
      }
      if (q.tri) {                                                  // B2-TRAD: triangulo 2D (un color: el del ultimo vertice)
        const ImVec2 c(ox + q.x2 * sx, q.y2 * sy), u2(q.u2 + eu, q.v2 + ev);
        if (q.tex >= 0 && ids[size_t(q.tex)])
          dl->AddImageQuad(ids[size_t(q.tex)], a, b, c, c, ImVec2(q.u0 + eu, q.v0 + ev), ImVec2(q.u1 + eu, q.v1 + ev),
                           u2, u2, q.col);
        else if (q.tex < 0) dl->AddTriangleFilled(a, b, c, q.col);
        if (!ventana_b2) dl->PopClipRect();
        continue;
      }
      if (q.tex >= 0 && ids[size_t(q.tex)]) {
        const float du = q.u1 >= q.u0 ? eu : -eu, dv = q.v1 >= q.v0 ? ev : -ev;
        dl->AddImage(ids[size_t(q.tex)], a, b, ImVec2(q.u0 + du, q.v0 + dv), ImVec2(q.u1 - du, q.v1 - dv), q.col);
      } else if (q.tex < 0) dl->AddRectFilled(a, b, q.col);
      if (!ventana_b2) dl->PopClipRect();
    }
    dl->AddCallback(rex::ui::kImDrawBlendAlpha, nullptr);
    if (!ventana_b2) pintar_figuras();
    else {
      if (d->figuras_antes >= d->q.size()) capa_superior();
      dl->PopClipRect();
      dl->AddCallback(rex::ui::kImDrawBlendAlpha, nullptr);
      // Las tandas 3D tienen ventanas propias; las bandas se cierran por encima de todas ellas.
      if (ox > 0) {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(ox, io.DisplaySize.y), IM_COL32_BLACK);
        dl->AddRectFilled(ImVec2(ox + caja, 0), io.DisplaySize, IM_COL32_BLACK);
      }
      ImGui::End();
    }
  }
 private:
  struct E { ps2::Tex t; uint64_t uso = 0; };
  std::map<std::pair<uint64_t, bool>, E> tex_;   // la clave lleva el direccionamiento: la misma imagen vale en CLAMP y REPEAT
  uint64_t n_ = 0;
};

void CrearPanel() {
  static Panel* panel = nullptr;
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer() && rt->graphics_system() && rt->graphics_system()->presenter()) {
    panel = new Panel();
    rt->graphics_system()->presenter()->AddUIDrawerFromUIThread(panel, 64);
  }
}
}  // namespace

// TRAD/5d: el arranque (Iniciar + menus del ELF hasta `hasta`) va en un hilo aparte; nadie mas toca el Bucle mientras
// g_previo == 1; el hilo de arranque consume DMA sin publicar dibujo. 0 sin empezar, 1 corriendo, 2 acabado.
std::atomic<int> g_previo{0};
uint32_t g_clave_bucle = 0x7D5;   // solo el hilo del juego; el trabajador usa g_clave congelada durante su arranque
std::atomic<uint32_t> g_clave_pedida{0x7D5};

bool CuadroBucleDirecto(uint16_t botones);
bool LeerBotDirecto(uint32_t clave, EstadoBot& s);
bool Leer19Directo(Estado19& s);
namespace {
struct VistaIW {
  bool ok = true, bot_valido = false, videl_valido = false, arrancando = false;
  uint32_t clave = 0;
  EstadoBot bot;
  Estado19 videl;
  std::shared_ptr<const Dibujo> dibujo;
};
std::mutex g_vista_mu;
VistaIW g_vista;
std::atomic<bool> g_paralelo{false};
bool g_paso_enviado = false;   // solo el consumidor; el trabajador nunca llama a Recoger
std::unique_ptr<Trabajador<VistaIW>>& TrabajoIW() {
  // Destruir/join del trabajador ANTES de destruir su CPU y sus traductores.
  B(); Global(); GlobalDma();
  static std::unique_ptr<Trabajador<VistaIW>> t;
  return t;
}
VistaIW LeerVistaIW() {
  VistaIW v;
  auto& b = B();
  v.clave = g_clave.load();
  v.ok = !b.roto;
  v.arrancando = !b.roto && (!b.listo || (b.elf && b.n < b.hasta));
  if (!v.arrancando) {
    v.bot_valido = (v.clave == 0x7DB || v.clave == 0x7D4 || v.clave == 0x7DC) && LeerBotDirecto(v.clave, v.bot);
    v.videl_valido = v.clave == 0x7E1 && Leer19Directo(v.videl);
  }
  return v;
}
void PublicarVistaIW(VistaIW v) {
  if (v.dibujo) { std::lock_guard<std::mutex> lk(g_mu); g_dibujo = v.dibujo; }
  std::lock_guard<std::mutex> lk(g_vista_mu);
  g_vista = std::move(v);
}
void TerminarPasoIW() {
  if (!g_paso_enviado) return;
  auto v = TrabajoIW()->Recoger();
  g_paso_enviado = false;
  PublicarVistaIW(std::move(v));
}
}  // namespace

bool DisponibleIW() {
  static const bool disponible = [] {
    const char* d = std::getenv("DBZ3_IWTRAD_DESDE");
    const char* p = std::getenv("DBZ3_IWTRAD_PUNTO");
    return (d && std::string(d) == "punto") || (p && *p) || !Bucle::IsoEu().empty();
  }();
  return disponible;
}

void Previo() {
  auto& b = B();
  const auto t0 = std::chrono::steady_clock::now();
  if (!b.Iniciar()) b.roto = true;
  else if (b.elf && !b.Planificar(b.hasta)) {
    REXLOG_WARN("dbz3 iwtrad: el arranque desde el ELF se para en el vblank {}: {}", b.n, b.e.error());
    b.roto = true;
  }
  REXLOG_INFO("dbz3 iwtrad: arranque previo acabado en {} ms (vblank {}, pantalla {:X}{})",
              std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(), b.n,
              b.roto ? 0 : b.e.R32(U(0x5C1490)) & 0xFF, b.roto ? ", roto" : "");
  g_previo = 2;
}

bool Precalentar() {
  if (!Activo()) return false;
  // La ficha puede cambiar de minijuego mientras el mapa termina el arranque anterior.
  // Consumir la seleccion pendiente tambien aqui, sin esperar al primer cuadro jugable.
  PonerClave(g_clave_pedida.load());
  int esperado = 0;
  if (!g_previo.compare_exchange_strong(esperado, 1)) return true;
  if (B().listo || B().roto) { g_previo = 2; return true; }
  REXLOG_INFO("dbz3 iwtrad: arranque previo del IW original en segundo plano");
  std::thread(Previo).detach();
  return true;
}

void PonerClave(uint32_t clave) {
  if (!Activo()) return;
  g_clave_pedida = clave;   // no perder la seleccion aunque el mapa siga precalentando otra mision
  if (clave == g_clave_bucle || g_previo.load() == 1) return;
  TerminarPasoIW();
  g_paralelo = false;
  g_clave = clave;
  g_clave_bucle = clave;
  Hueco() = std::make_unique<Bucle>();   // otro minijuego = otro arranque (6 s); g_previo a 0 lo relanza Precalentar
  Global() = Traductor{};
  GlobalDma() = Dma{};
  { std::lock_guard<std::mutex> lk(g_mu); g_dibujo.reset(); }
  g_previo = 0;
  REXLOG_INFO("dbz3 iwtrad: nueva clave {:X}: el original arranca otra vez", clave);
}

void NuevaEntrada() {
  TerminarPasoIW();
  g_paralelo = false;
  if (g_previo.load() != 2) return;               // aun arrancando (o sin empezar): la ronda ya sera nueva
  auto& b = B();
  if (b.roto || !b.listo || !b.foto || b.n == b.foto->est.n) return;
  const uint64_t n = b.n;
  if (b.Restaurar()) REXLOG_INFO("dbz3 iwtrad: entrada nueva en {:X}: el original vuelve al vblank {} (estaba en el {})", g_clave_bucle, b.n, n);
}

bool Arrancando() {
  if (g_paralelo.load()) { std::lock_guard<std::mutex> lk(g_vista_mu); return g_vista.arrancando; }
  if (g_previo.load() == 1) return true;
  auto& b = B();
  return !b.roto && (!b.listo || (b.elf && b.n < b.hasta));
}

bool LeerD5(EstadoD5& s, uint32_t pw) {
  if (g_previo.load() != 2) return false;
  auto& b = B();
  if (b.roto || !b.listo || !b.elf || b.n < b.hasta) return false;
  const uint32_t w = b.e.R32(U(pw)), q = b.e.R32(U(0x49CD70));
  if (w < 0x100000 || w >= Ee::kRam || q < 0x100000 || q >= Ee::kRam) return false;
  s.fase = int(b.e.R32(w)); s.sub = int(b.e.R32(w + 4)); s.ronda = int(int32_t(b.e.R32(w + 0x54)));
  s.n = int(b.e.R32(w + 0xB8)); s.cuenta = int(b.e.R32(w + 0x5C)); s.total = b.e.Rf(w + 0x44); s.rango = b.e.R8(w + 0x88);
  s.w80 = b.e.R8(w + 0x80);
  s.juicio = int(b.e.R32(w + 0xB4));
  const uint32_t ui = b.e.R32(U(0x49CD8C));
  for (int i = 0; i < 10; ++i) {
    s.seq[i] = b.e.R8(q + 0x28 + uint32_t(i));
    s.pts[i] = b.e.Rf(w + 0x1C + 4 * uint32_t(i));
    s.vivo[i] = b.e.R8(w + 0x89 + uint32_t(i)); s.fallado[i] = b.e.R8(w + 0x9D + uint32_t(i));
    s.acierto[i] = b.e.R8(w + 0x60 + uint32_t(i)); s.w93[i] = b.e.R8(w + 0x93 + uint32_t(i));
    s.x[i] = ui >= 0x100000 && ui + uint32_t(i + 2) * 400 < Ee::kRam ? b.e.Rf(ui + uint32_t(i + 1) * 400 + 0x20) : 0.f;
  }
  for (int i = 0; i < 3; ++i) s.cat[i] = b.e.Rf(w + 0x48 + 4 * uint32_t(i));
  return true;
}

bool LeerBotDirecto(uint32_t clave, EstadoBot& s) {
  if (g_previo.load() != 2) return false;
  auto& b = B();
  if (b.roto || !b.listo || !b.elf || b.n < b.hasta) return false;
  const bool dc = clave == 0x7DC;
  const uint32_t pw = b.e.R32(U(clave == 0x7DC ? 0x49CDF8 : clave == 0x7DB ? 0x49CE7C : 0x49CEBC));
  const uint32_t q = b.e.R32(U(0x49CD70)), ui = b.e.R32(U(0x49CD8C)), act = b.e.R32(U(0x49CA70));
  auto ok = [](uint32_t a) { return a >= 0x100000 && a < Ee::kRam - 0x4000; };
  if (!ok(pw) || !ok(q) || !ok(ui) || !ok(act)) return false;
  s.fase = int(b.e.R32(pw + 4)); s.sub = int(b.e.R32(pw + 8)); s.rango = int(b.e.R32(pw + 0x1C)); s.nota = b.e.Rf(pw + 0x20);
  s.fondo = int(b.e.R32(U(0x4DB2CC)) & 1);   // IW-08: bit 0 = escenario visible (RAMSwitch_Background-1/+1, EU 0x1E10D0/0x1E10F0)
  // PAQ_4/PAQ_5: un solo bloque de ampliacion; banderas de 0x7DB y cfg de 0x7DC (diagnostico)
  if (clave == 0x7DB) for (int i = 0; i < 6; ++i) s.banderas[i] = b.e.R8(pw + 0x2F8 + uint32_t(i));
  if (dc) {
    s.cfg_nivel = int(b.e.R32(pw + 0x18));
    for (int i = 0; i < 5; ++i) s.cfg_puntos[i] = int(b.e.R32(U(0x481920) + 4 * uint32_t(i)));
  }
  s.activo = int(b.e.R32(pw + 0x2E0)); s.choques = int(b.e.R32(pw + 0x48));
  if (clave == 0x7DB) {
    if (anim7db::escena.load() && (s.fase == 0 || (s.fase == 1 && s.sub == 0))) {
      s.campo_intro7db = b.n;
      s.paso_anim7db = int(b.e.R32(pw + 0x10));
      for (int r = 0; r < 2; ++r) {
        const uint32_t a = act + (r ? 0x28D0u : 0x800u);
        s.codigo7db[r] = int(b.e.R32(a + 0xB60));
        s.cuadro7db[r] = b.e.Rf(a + 0xC68);
      }
      std::copy_n(b.voz_intro7, 2, s.lineas7db);
    }
    s.paso_apertura = s.fase == 0 ? int(b.e.R32(pw + 0x10)) : -1;
    if (anim7db::Habilitada()) {
      s.cam_intro = -1;
      const bool ready_inicial = s.fase == 1 && s.sub == 0 && !b.e.R8(pw + 0x15);
      const uint32_t cam = b.e.R32(U(0x49CD6C));
      if ((s.fase == 0 || ready_inicial) && cam >= 0x100000 && cam <= Ee::kRam - 0x184) {
        const auto slot = b.e.R32(cam + 0x180), id = b.e.R32(cam + 0x160);
        const float f = b.e.Rf(cam + 0x168);
        if (f >= 0 && f <= b.e.Rf(cam + 0x16C) &&
            ((s.fase == 0 && ((slot == 1 && id == 0) || (slot == 0 && id == 0x47E))) ||
             (ready_inicial && slot == 0 && id == 0x186))) {
          s.cam_intro = int(id);
          s.cuadro_intro = f;
        }
      }
    }
    const uint32_t choque = b.e.R32(U(0x49CE94));
    if (!ok(choque)) return false;
    s.choque_fase = int(b.e.R32(choque));
    s.pulsos = int(b.e.R32(choque + 8));
    s.choque_lado = int(b.e.R32(choque + 0x80));
    s.marca_indice = int32_t(b.e.R32(choque + 0x7C));
    const int nivel = int(b.e.R32(pw + 0x18));
    if (s.choque_lado < 0 || s.choque_lado > 1 || nivel < 0 || nivel > 5) return false;
    s.objetivo = int(b.e.R32(choque + 4 * uint32_t(s.choque_lado * 6 + nivel + 3)));
    // QTE_CheckPressedMatch EU 0x2EEBF0 compara SOLO este boton, no cualquiera de los cuatro.
    if (s.marca_indice >= 0 && s.marca_indice < 50)
      s.choque_boton = b.e.R8(q + 0x28 + uint32_t(s.choque_lado * 100 + s.marca_indice));
    for (int i = 0; i < 3; ++i) s.marca_x[i] = b.e.Rf(ui + uint32_t(0x2A + i) * 400 + 0x20);
    s.marca_y = b.e.Rf(ui + 0x2A * 400 + 0x24);
  }
  s.vida[0] = b.e.Rf(act + 0x800 + 0x1850); s.vida[1] = b.e.Rf(act + 0x28D0 + 0x1850);
  const int ven = dc ? 15 : 10, base = dc ? 0x15 : 0x16;
  for (int l = 0; l < 2; ++l) {
    s.aciertos[l] = int(b.e.R32(pw + (dc ? 0x3C : 0x3C) + 4 * uint32_t(l)));
    s.intentos[l] = int(b.e.R32(pw + 0x44 + 4 * uint32_t(l)));
    s.llegaron[l] = std::min(50, int(b.e.R32(pw + 0x2C + 4 * uint32_t(l))));
    const int cnt = std::min(50, int(b.e.R32(pw + 0x2C8 + 4 * uint32_t(l))));
    s.n_ic[l] = 0;
    for (int k = std::max(0, cnt - ven); k < cnt; ++k) {
      const uint32_t f = pw + uint32_t(l) * 100 + uint32_t(k);
      auto& ic = s.ic[l][s.n_ic[l]++];
      ic.k = k;
      ic.boton = b.e.R8(q + 0x28 + uint32_t(l) * 100 + uint32_t(k));
      ic.x = b.e.Rf(ui + uint32_t(k % ven + l * ven + base) * 400 + 0x20);
      ic.vivo = b.e.R8(f + 0x130) != 0;
      ic.fallado = b.e.R8(f + 0x1F8) != 0;
    }
  }
  return true;
}

bool Leer19Directo(Estado19& s) {
  if (g_previo.load() != 2 || g_clave_bucle != 0x7E1) return false;
  auto& b = B();
  if (b.roto || !b.listo || !b.elf || b.n < b.hasta) return false;
  auto ok = [](uint32_t a, uint32_t tam) { return a >= 0x100000 && a <= Ee::kRam - tam; };
  const uint32_t w = b.e.R32(U(0x49CD68)), ui = b.e.R32(U(0x49CD8C));
  if (!ok(w, 0x294) || !ok(ui, 0x30 * 400)) return false;
  const uint32_t tarea = b.e.R32(w);
  if (!ok(tarea, 0x34)) return false;
  s.estado = b.e.R32(tarea + 0x1C);
  // R23 IW-4: los estados de 7E1 (0x2E9920..0x2EAA90 EU) estan 0x210 mas abajo en SLUS_218.42
  // (comprobado en los dos ELF); el bot y las fases los comparan en direcciones EU.
  if (g_usa.load(std::memory_order_relaxed) && s.estado >= 0x2E9700 && s.estado < 0x2EA880 + 0x200) s.estado += 0x210;
  s.fila = b.e.R16(w + 0x68); s.n = b.e.R16(w + 0x66);
  if (s.fila >= 10 || s.n < 1 || s.n > 10) return false;
  s.aciertos = b.e.R16(w + 0x6C); s.intentos = b.e.R16(w + 0x6E);
  s.reaccion = int32_t(b.e.R32(w + 0x290));
  s.cursor = b.e.Rf(ui + 0x20);
  for (int i = 0; i < 10; ++i) {
    const uint32_t cat = b.e.R32(w + 0x80 + uint32_t(i) * 0x10);
    s.puntos[i] = cat < 4 ? b.e.R16(U(0x5D9184) + cat * 2) : 0;
    const uint32_t o = ui + uint32_t(0x11 + i) * 400;
    s.obj[i] = {b.e.Rf(o + 0x20), int(b.e.R32(o + 0x48)), int(b.e.R32(o + 0x44)),
                (b.e.R32(o + 0x150) & 3) == 3};
  }
  const auto it = b.entradas.find(0x2E9AF0);
  s.fin = it != b.entradas.end() && it->second != 0;
  if (s.fin) {
    const uint32_t cfg = b.e.R32(U(0x49CBA4));
    if (!ok(cfg, 0x170)) return false;
    // GameState_GetConfigBlock = *0x49CBA4 + 0x11C; total +0x34 y rango +0x48 (0..4 -> 1..5 del port).
    s.total = int(b.e.R32(cfg + 0x150));
    REXLOG_INFO("dbz3 iwtrad 7E1: fin cfg total {} rango {} (0x2E9AF0 escrito={})", s.total,
                int32_t(b.e.R32(cfg + 0x164)) + 1, b.entradas.count(0x2E9AF0) ? b.entradas.at(0x2E9AF0) : 0);
    s.rango = int32_t(b.e.R32(cfg + 0x164)) + 1;
  }
  return true;
}

bool TomarEvBot(char& tipo, int& n) {
  std::lock_guard<std::mutex> lk(g_boca_mu);
  if (g_evbot.empty()) return false;
  tipo = g_evbot.front().first, n = g_evbot.front().second;
  g_evbot.erase(g_evbot.begin());
  return true;
}
bool TomarBoca(int& lado, int& clip) {
  std::lock_guard<std::mutex> lk(g_boca_mu);
  if (g_boca.empty()) return false;
  lado = g_boca.front().first, clip = g_boca.front().second;
  g_boca.erase(g_boca.begin());
  return true;
}
// R22/PAQ_5: una palabra de la RAM del original (solo camino directo: con el trabajador la RAM es suya).
uint32_t DirIW(uint32_t eu) { return U(eu); }
bool LeerPalabra(uint32_t direccion, uint32_t& valor) {
  if (g_paralelo.load() || g_previo.load() != 2) return false;
  auto& b = B();
  if (b.roto || !b.listo || !b.elf || b.n < b.hasta || direccion >= Ee::kRam - 4) return false;
  valor = b.e.R32(direccion);
  return true;
}
bool LeerBot(uint32_t clave, EstadoBot& s) {
  if (!g_paralelo.load()) return LeerBotDirecto(clave, s);
  std::lock_guard<std::mutex> lk(g_vista_mu);
  if (!g_vista.bot_valido || g_vista.clave != clave) return false;
  s = g_vista.bot;
  return true;
}
bool Leer19(Estado19& s) {
  if (!g_paralelo.load()) return Leer19Directo(s);
  std::lock_guard<std::mutex> lk(g_vista_mu);
  if (!g_vista.videl_valido) return false;
  s = g_vista.videl;
  return true;
}

bool CuadroBucle(uint16_t botones) {
  if (!Activo()) { TerminarPasoIW(); g_paralelo = false; return false; }
  const auto inicio_espera = std::chrono::steady_clock::now();
  const bool medir_espera = g_paso_enviado && std::getenv("DBZ3_IW_RITMO");
  TerminarPasoIW();
  if (medir_espera) {
    static unsigned n = 0;
    static double total = 0, maximo = 0;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inicio_espera).count();
    total += ms; maximo = std::max(maximo, ms);
    if (++n == 300) {
      REXLOG_INFO("dbz3 iw trabajador: cuadros {} espera_ms {:.3f} max_ms {:.3f}", n, total / n, maximo);
      n = 0; total = maximo = 0;
    }
  }
  PonerClave(g_clave_pedida.load());
  auto& b = B();
  const uint32_t clave = g_clave.load();
  static const bool sin_trabajador = std::getenv("DBZ3_TEST_IW_SIN_TRABAJADOR") != nullptr;
  if (sin_trabajador || g_previo.load() != 2 || !b.listo || !b.elf || b.n < b.hasta ||
      !(clave == 0x7DA || clave == 0x7DB || clave == 0x7D4 || clave == 0x7DC || clave == 0x7E0 || clave == 0x7E1)) {
    g_paralelo = false;
    static uint32_t clave_directa = 0;   // PAQ_2/7D5: medir antes de meter una clave en el trabajador (una linea por clave)
    if (std::getenv("DBZ3_IW_RITMO") && clave != clave_directa && g_previo.load() == 2 && b.listo && b.n >= b.hasta) {
      clave_directa = clave;
      REXLOG_INFO("dbz3 iwtrad: clave {:X} por CuadroBucleDirecto (sin trabajador)", clave);
    }
    return CuadroBucleDirecto(botones);
  }
  if (b.roto) return false;
  if (!g_paralelo.load()) PublicarVistaIW(LeerVistaIW());
  auto& t = TrabajoIW();
  if (!t) {
    t = std::make_unique<Trabajador<VistaIW>>([](uint16_t entrada) {
      std::shared_ptr<const Dibujo> dibujo;
      struct Lista { ~Lista() { g_lista_pendiente = nullptr; } } lista;
      g_lista_pendiente = &dibujo;
      const bool ok = CuadroBucleDirecto(entrada);
      auto v = LeerVistaIW();
      v.ok = ok; v.dibujo = std::move(dibujo);
      return v;
    });
    REXLOG_INFO("dbz3 iwtrad: trabajador de pasos activo; estado y HUD publicados juntos, una entrada por cuadro");
  }
  g_paralelo = true;
  g_paso_enviado = true;
  t->Enviar(botones);
  return true;
}

bool CuadroBucleDirecto(uint16_t botones) {
  if (!Activo()) return false;
  if (g_previo.load() == 1) return true;   // el trabajador posee el Bucle hasta publicar su fin
  if (!g_lista_pendiente) PonerClave(g_clave_pedida.load());   // trabajador: clave congelada hasta recoger el paso
  auto& b = B();
  if (!b.listo) {                                   // TRAD/5d: el arranque va en segundo plano (Precalentar)
    Precalentar();
    if (g_previo.load() != 2) return true;
    if (b.roto) return false;
    b.listo = true;
    dbz3::launcher::DeferUi(&CrearPanel);           // fuera del dibujo de la interfaz (como CreatePanelB)
    b.n0 = b.n;
    b.reloj = 0;
    if (b.elf) b.Fotografiar();                   // TRAD/5e: cada entrada en 7D5 vuelve aqui (NuevaEntrada)
  }
  if (b.roto) return false;
  if (b.iso_bad) {   // PAQ_2/Paso 2: sin datos completos de la ISO, no colgarse en la carga; vuelve al espejo
    b.roto = true;
    REXLOG_WARN("dbz3 iwtrad: faltan datos de la ISO; vuelvo al espejo para no colgarme en una carga");
    return false;
  }
  if (const char* f = std::getenv("DBZ3_IWTRAD_BOTONES"); f && *f) botones = uint16_t(std::strtoul(f, nullptr, 16));
  if (const char* ej = std::getenv("DBZ3_IWTRAD_EJES"); ej && *ej) {   // PAQ_6/17.C: palanca cruda del piloto
    const uint32_t v = uint32_t(std::strtoul(ej, nullptr, 16));
    b.e.W32(U(0x4ADE84), v);
  }
  b.botones = botones;
  if (b.elf) {                                                      // TRAD/5: hilos del ELF; rapido hasta `hasta`, luego PAL
    uint64_t obj;
    if (b.n < b.hasta) { obj = std::min<uint64_t>(b.n + 150, b.hasta); b.n0 = obj; b.reloj = 0; }
    else { b.reloj += g_usa ? 1.0 : 50.0 / 60.0; obj = b.n0 + uint64_t(b.reloj); }   // USA: NTSC, 60 vblanks/s
    const uint64_t n_antes = b.n;
    if (b.n > b.hasta + 500 && b.e.perfil.empty() && std::getenv("DBZ3_TEST_IW_CPU_PERFIL")) b.e.perfil.resize(0x20000);
    const auto t_ritmo = std::chrono::steady_clock::now();
    const uint64_t instr_antes = b.e.n, envios_antes = GlobalDma().envios, dma_antes = b.cuadros;
    if (!b.Planificar(obj)) {
      REXLOG_WARN("dbz3 iwtrad: el arranque desde el ELF se para en el vblank {}: {}", b.n, b.e.error());
      b.roto = true;
      return false;
    }
    // RITMO (R16): lo que cuesta este cuadro del original traducido (interprete + traduccion DMA + pintado del cuadro).
    if (const char* r = std::getenv("DBZ3_IW_RITMO"); r && *r) {
      ++g_ritmo_trad.cuadros;
      g_ritmo_trad.vblanks += b.n - n_antes;
      g_ritmo_trad.instr += b.e.n - instr_antes;
      g_ritmo_trad.envios += GlobalDma().envios - envios_antes;
      g_ritmo_trad.dma += b.cuadros - dma_antes;
      g_ritmo_trad.us_cuadro += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_ritmo).count();
      if (g_ritmo_trad.cuadros >= 60) {
        RitmoTradLinea();
        if (!b.e.perfil.empty()) {
          std::vector<std::pair<uint64_t, uint32_t>> calientes;
          for (uint32_t i = 0; i < b.e.perfil.size(); ++i)
            if (b.e.perfil[i]) calientes.push_back({b.e.perfil[i], i << 6});
          std::sort(calientes.rbegin(), calientes.rend());
          for (size_t i = 0; i < std::min<size_t>(12, calientes.size()); ++i)
            REXLOG_INFO("dbz3 iw CPU: pc {:08X} muestras {}", calientes[i].second, calientes[i].first);
          std::fill(b.e.perfil.begin(), b.e.perfil.end(), 0);
        }
      }
    }
    // R23 IW-7: GameState_SetFlag2 (EU 0x28A980, *(0x49CBA4)+0x8C bit 1): el original da/quita el control al jugador.
    // Una linea por cambio: sirve para alinear capturas con PCSX2 (E:/DBZ3HD_trabajo/g23/iw7/ref_juego.py).
    if (b.n >= b.hasta) {
      static int control_ant = -1;
      const uint32_t cfg = b.e.R32(U(0x49CBA4));
      const int control = cfg >= 0x100000 && cfg < Ee::kRam - 0x100 ? int((b.e.R32(cfg + 0x8C) >> 1) & 1) : -1;
      if (control != control_ant) {
        if (control == 1 && control_ant == 0 && g_vb_control == ~0ull) g_vb_control = b.n;
        REXLOG_INFO("dbz3 iwtrad: control del original {} en el vblank {} cuadro {} (clave {:X})", control, b.n, b.cuadro_dma, g_clave.load());
        control_ant = control;
      }
    }
    if (b.n / 250 != n_antes / 250 || b.n == b.hasta)
      REXLOG_INFO("dbz3 iwtrad: ELF vblank {} pantalla {:X} mision {:X} hilos {} instrucciones {}", b.n, b.e.R32(U(0x5C1490)) & 0xFF,
                  b.e.R32(U(0x5C0DC8)), b.hilos.size(), b.e.n);
    return true;
  }
  b.reloj += 50.0 / 60.0;                                         // el reloj invitado no recupera tiempo de alt-tab
  unsigned sin_avance = 0;
  while (double(b.n - b.n0) < b.reloj) {
    const auto n_antes = b.n;
    if (!b.e.Llamar(kCuerpo, {}, 200000000)) {
      REXLOG_WARN("dbz3 iwtrad: el bucle principal se para en el vblank {}: {}", b.n, b.e.error());
      b.roto = true;
      return false;
    }
    sin_avance = b.n == n_antes ? sin_avance + 1 : 0;
    if (sin_avance >= 8) {
      REXLOG_WARN("dbz3 iwtrad: bucle sin avance de vblank ({})", b.n);
      b.roto = true;
      return false;
    }
    static const char* ramv = std::getenv("DBZ3_IWTRAD_RAMVOLCADO");   // pruebas: RAM tras cada vuelta (las 12 primeras)
    if (ramv && *ramv && b.vueltas < 12) {
      if (std::FILE* f = std::fopen((std::string(ramv) + "/ram_" + std::to_string(b.vueltas) + ".bin").c_str(), "wb")) {
        std::vector<uint64_t> m(Ee::kRam / 8);
        for (uint32_t i = 0; i < m.size(); ++i) m[i] = b.e.R64(i * 8);
        std::fwrite(m.data(), 8, m.size(), f);
        std::fclose(f);
      }
    }
    if (ramv && *ramv && b.vueltas < 3)
      for (auto [f, k] : b.entradas) REXLOG_INFO("dbz3 iwtrad: vuelta {} entradas {:X} = {}", b.vueltas, f, k);
    ++b.vueltas;
  }
  return true;
}

// ---------------- B2-TRAD (13/11): el Budokai 2 original desde su ELF, tras dbz3_b2_traducido (de serie apagado) ----------------
// El arranque (menus hasta el tablero, modo rapido) va en un hilo aparte; despues, una vuelta a ritmo PAL por cuadro y su
// 2D al traductor GS. ponytail: sin mando del jugador ni figuras 3D (ordenes de actor 0x23A050/0x23A2E0 -> modelos B3):
// el original se queda en la seleccion de companero. Lo que falta, en studies 5/ENTREGAS/B2_TRAD_RELEVO.md.
std::atomic<int> g_previo_b2{0};   // 0 sin empezar, 1 arrancando, 2 acabado

void PrevioB2() {
  auto& b = BB2();
  const auto t0 = std::chrono::steady_clock::now();
  if (!b.IniciarB2()) b.roto = true;
  else if (!b.Planificar(b.hasta)) {
    REXLOG_WARN("dbz3 b2trad: el arranque desde el ELF se para en el vblank {}: {}", b.n, b.e.error());
    b.roto = true;
  }
  REXLOG_INFO("dbz3 b2trad: arranque previo acabado en {} ms (vblank {}{})",
              std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(), b.n,
              b.roto ? ", roto" : "");
  g_previo_b2 = 2;
}

bool CuadroB2(uint16_t botones, int fase, bool comic, const PartidaB2* partida) {
  if (!ActivoB2()) return false;
  TerminarPasoIW();
  g_paralelo = false;
  g_b2_comic.store(comic, std::memory_order_relaxed);
  fase = std::clamp(fase, 0, 8);
  // No destruir el interprete mientras PrevioB2 trabaja. Al terminar, una fase nueva recibe su propia entrada;
  // nunca conservar el mapa de la I encima de las reglas de la IV, ni el ultimo paquete de la fase anterior.
  if (g_previo_b2.load() == 1) return true;
  if (partida && BB2().partida_b2.entrada != partida->entrada) {
    auto siguiente = std::make_unique<Bucle>();
    siguiente->fase_b2 = fase;
    if (partida) siguiente->partida_b2 = *partida;
    HuecoB2() = std::move(siguiente);
    g_previo_b2 = 0;
    g_b2_latido = 0;
    { std::lock_guard<std::mutex> lk(g_mu); if (g_dibujo && g_dibujo->alto == 512) g_dibujo.reset(); }
  }
  auto& b = BB2();
  if (!b.listo) {
    int esperado = 0;
    if (g_previo_b2.compare_exchange_strong(esperado, 1)) {
      REXLOG_INFO("dbz3 b2trad: arranque previo del B2 original en segundo plano");
      std::thread(PrevioB2).detach();
    }
    if (g_previo_b2.load() != 2) return true;
    if (b.roto) return false;
    b.listo = true;
    dbz3::launcher::DeferUi(&CrearPanel);
    b.n0 = b.n;
    b.reloj = 0;
  }
  if (b.roto) return false;
  if (b.combate_b2) return true; // reloj detenido durante el combate externo
  // dsB2_MAPA: el tablero esta en pantalla este cuadro; renueva el latido de la capa traducida.
  g_b2_latido.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
  if (botones != b.botones) REXLOG_INFO("dbz3 b2trad: mando {:04X} en el vblank {}", botones, b.n);
  b.botones = botones;
  b.reloj += 50.0 / 60.0;                                           // PAL: 50 vblanks por segundo
  const uint64_t obj = b.n0 + uint64_t(b.reloj), antes = b.n;
  if (obj > b.n && !b.Planificar(obj)) {
    REXLOG_WARN("dbz3 b2trad: el original se para en el vblank {}: {}", b.n, b.e.error());
    const uint32_t modelo = b.e.R32(b.e.gp - 0x7710);
    REXLOG_WARN("dbz3 b2trad: diagnostico modelo {:X} cantidad {:X} tabla {:X} registros v0 {:X} v1 {:X} a0 {:X} a1 {:X}",
                modelo, b.e.R32(modelo + 0x418), b.e.R32(modelo + 0x42C), uint32_t(b.e.r[2]),
                uint32_t(b.e.r[3]), uint32_t(b.e.r[4]), uint32_t(b.e.r[5]));
    b.roto = true;
    return false;
  }
  if (b.n / 250 != antes / 250)
    REXLOG_INFO("dbz3 b2trad: ELF vblank {} hilos {} instrucciones {} figura ({:.1f}, {:.1f}) anim {:X} cuadro {:.1f} hace {}", b.n,
                b.hilos.size(), b.e.n, b.figura.x, b.figura.y, b.figura.anim, b.figura.cuadro, int64_t(b.n - b.figura.n));
  return true;
}

bool LeerTableroB2(EstadoTableroB2& s) {
  if (!ActivoB2() || g_previo_b2.load() != 2 || !BB2().listo || BB2().roto) return false;
  auto& b = BB2(); auto& c = b.e;
  s = {};
  s.partida.entrada = b.partida_b2.entrada;
  s.partida.reanudada = true;
  s.partida.turno = c.R16(0x6992D8); s.partida.fase = c.R8(0x6992DA);
  s.partida.bolas = c.R8(0x6992DD); s.partida.radar = c.R8(0x6992DC); s.partida.musica = c.R8(0x6992DB);
  for (uint32_t i = 0; i < 30; ++i) s.partida.banderas[i] = c.R32(0x699260 + i * 4);
  const auto* ram = c.Imagen().data();
  std::memcpy(s.partida.piezas, ram + 0x69B7A0, sizeof s.partida.piezas);
  std::memcpy(s.partida.objetos, ram + 0x69BC20, sizeof s.partida.objetos);
  std::memcpy(s.partida.casillas, ram + 0x69BC60, sizeof s.partida.casillas);
  std::memcpy(s.partida.bolas_fase, ram + c.gp - 0x72C8, sizeof s.partida.bolas_fase);
  const uint32_t w = c.R32(c.gp - 0x7734);
  if (w && w < Ee::kRam - 0x80) {
    s.funcion = c.R32(w + 0x1C); s.paso = c.R32(w + 0x30); s.destino = int(c.R32(w + 0x3C));
  }
  s.seleccion = int(c.R32(c.gp - 0x772C));
  s.jugador = b.jugador_b2; s.rival = b.rival_b2; s.casilla_combate = b.casilla_b2;
  s.combate = b.combate_b2; s.victoria = b.victoria_b2; s.salida = b.salida_b2;
  if (b.retorno_perdedor_b2 >= 0 && s.funcion == 0x237990 && s.paso >= 26) {
    const int barras = c.R8(0x69B7A1 + uint32_t(b.retorno_perdedor_b2) * 0x24);
    REXLOG_INFO("dbz3 b2trad: retorno ORIGINAL {}: pieza {} barras {} -> {} turno {}",
                barras == std::max(0, b.barras_antes_b2 - 1) ? "confirmado" : "discrepancia",
                b.retorno_perdedor_b2, b.barras_antes_b2, barras, s.partida.turno);
    b.retorno_perdedor_b2 = -1;
  }
  if (s.funcion == 0x234520 && b.n % 250 == 0 && std::getenv("DBZ3_TEST_B2_ORDENES")) {
    std::string recursos;
    for (uint32_t i = 0; i < 16; ++i) {
      const uint32_t recurso = c.R32(0x6993C0 + i * 4);
      recursos += " " + std::to_string(i) + ":" + std::to_string(recurso) + "/" + std::to_string(c.R16(recurso + 4));
    }
    REXLOG_INFO("dbz3 b2trad: carga pendiente:{}", recursos);
  }
  if (std::getenv("DBZ3_TEST_B2_ORDENES")) {
    static uint32_t funcion = ~0u, paso = ~0u;
    if (funcion != s.funcion || paso != s.paso) {
      funcion = s.funcion; paso = s.paso;
      REXLOG_INFO("dbz3 b2trad: estado RAM {:X} paso {} turno {} seleccion {} destino {}", s.funcion, s.paso,
                  s.partida.turno, s.seleccion, s.destino);
    }
  }
  return true;
}

void PilotoTableroB2(int destino, uint16_t boton) {
  if (g_previo_b2.load() != 2 || !std::getenv("DBZ3_TEST_MISIONES")) return;
  BB2().piloto_destino_b2 = destino; BB2().piloto_boton_b2 = boton;
  BB2().piloto_auto_b2 = true;
}

bool ResultadoTableroB2(int ganador, int vida) {
  if (g_previo_b2.load() != 2 || !BB2().combate_b2 || ganador < 0 || ganador > 1) return false;
  auto& b = BB2(); auto& c = b.e;
  // Contrato de retorno que consume 234520: lado ganador, piezas y vida P1.
  // Las barras, reacciones, retirada, T5, historial y continuacion los aplica el
  // original. Solo se suministra el resultado externo y se comprueba el cambio.
  b.retorno_perdedor_b2 = ganador == 0 ? b.rival_b2 : b.jugador_b2;
  b.barras_antes_b2 = c.R8(0x69B7A1 + uint32_t(b.retorno_perdedor_b2) * 0x24);
  b.retorno_t5_b2 = true;
  c.W32(0x3A45AC, uint32_t(b.jugador_b2)); c.W32(0x3A45B0, uint32_t(b.rival_b2));
  c.W32(0x3A45B8, uint32_t(std::clamp(vida, 0, 32767))); c.W32(0x3A45C8, uint32_t(ganador + 1));
  c.W32(c.gp - 0x7E80, 6); c.W32(c.gp - 0x778C, ~0u); // modo actual / siguiente
  c.W32(c.gp - 0x7794, 0); c.W32(c.gp - 0x7790, 0);
  // 238870(-1) gana la pieza 88BC; 238870(0) gana la 88C0.
  // No son los indices 0/1 del contrato HD.
  c.W32(c.gp - 0x7798, ganador == 0 ? ~0u : 0u);
  c.W32(b.principal_b2 + 0x1C, 0x231C90); // carga y creacion originales del tablero tras el combate
  b.combate_b2 = false;
  b.botones = b.pad_actual = 0;
  b.n0 = b.n; b.reloj = 0;
  REXLOG_INFO("dbz3 b2trad: resultado HD devuelto al ORIGINAL: ganador {} vida {}", ganador, vida);
  return true;
}

bool GuardarTableroB2(PartidaB2& p) {
  if (g_previo_b2.load() != 2 || BB2().roto || BB2().combate_b2) return false;
  auto& c = BB2().e;
  const uint32_t w = c.R32(c.gp - 0x7734);
  if (!w || w >= Ee::kRam - 0x80 || c.R32(w + 0x1C) != 0x237990 || c.R32(w + 0x30) < 26) return false;
  if (!c.Anidada(0x238320, {}, 2000000)) return false;
  std::memcpy(p.guardado, c.Imagen().data() + 0x69380C, sizeof p.guardado);
  p.guardado_valido = true;
  return true;
}

void PonerFiguraB2(void (*f)(float x, float y, uint32_t anim, float cuadro, float escala)) { g_figura_b2 = f; }
void PonerFigurasB2(void (*f)(const std::vector<FiguraTableroB2>&)) { g_figuras_b2 = f; }

}  // namespace dbz3::iwtrad

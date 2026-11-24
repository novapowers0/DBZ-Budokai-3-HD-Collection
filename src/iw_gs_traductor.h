// TRAD/2 (13/11): traductor de ordenes GS del IW original (plan «traducir, no recrear», PLAN_TRADUCIR_NO_RECREAR.md).
// Recibe lo que el juego original manda al GS (paquetes GIF o escrituras de registro ya resueltas, mismo formato que
// tools/iw_mips/gs_captura.py) y arma las primitivas completas con su contexto (TEX0/ALPHA/TEST/...) listas para pintar.
// Gemelo de referencia: tools/iw_mips/gs_primitivas.py (la prueba P3 compara ambos: 0 diferencias).
// Ajuste dbz3_iw_traducido, de serie encendido (R23 IW-7). Sin dependencias del SDK (se compila suelto para la prueba).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <cstdlib>
#include <cstring>
// Presentacion de 7DB (V2): de serie ON; DBZ3_IW_V2_7DB=0 la apaga.
inline bool V2_7DB() { const char* v = std::getenv("DBZ3_IW_V2_7DB"); return !v || std::strcmp(v, "0") != 0; }

namespace dbz3::iwtrad {

uint64_t HuellaImagen(const uint8_t* datos, size_t n);
bool VentanaTextura(int iw, int ih, float u0, float v0, float u1, float v1, int& x, int& y, int& w, int& h);
bool RecortarTextura(const std::vector<uint8_t>& rgba, int iw, int ih, float u0, float v0, float u1, float v1,
                     int& x, int& y, int& w, int& h, std::vector<uint8_t>& salida);

struct Vertice {
  uint16_t x = 0, y = 0;   // 12.4 del GS, sin restar XYOFFSET
  uint32_t z = 0;
  uint8_t f = 0;           // niebla (XYZF)
  uint32_t rgba = 0, q = 0x3F800000;
  uint64_t st = 0;         // S y T (float) tal cual
  uint32_t uv = 0;         // U | V << 16 (10.4)
};

struct Primitiva {
  uint32_t prim = 0;       // tipo + IIP/TME/FGE/ABE/AA1/FST/CTXT/FIX efectivos (PRMODE si PRMODECONT=0)
  uint64_t tex0 = 0, tex1 = 0, clamp = 0, alpha = 0, test = 0, scissor = 0, xyoffset = 0, frame = 0, zbuf = 0;
  uint64_t fba = 0, pabe = 0, dimx = 0, dthe = 0, colclamp = 0, fogcol = 0, texa = 0;
  int nv = 0;
  Vertice v[3];
  uint32_t nsub = 0;        // subidas del cuadro hechas antes de esta primitiva (orden para el pintor)
};

struct Subida {             // IMAGE de host a local con su destino (BITBLTBUF/TRXPOS/TRXREG)
  uint64_t bitbltbuf = 0, trxpos = 0, trxreg = 0;
  std::vector<uint8_t> datos;
};

// Memoria local del GS (4 MB) con la disposicion en paginas/bloques/columnas de PSMCT32, PSMT8 y PSMT4 (manual del GS,
// cap. 8): las subidas de imagen escriben aqui y las texturas se leen de aqui (con su CLUT CSM1 en PSMCT32).
class Vram {
 public:
  void Subir(const Subida& s, bool reutilizar = false);
  // Textura de TEX0 (2^TW x 2^TH, hasta 1024) en RGBA8. limitar512 conserva la ruta del tablero B2.
  bool Textura(uint64_t tex0, uint64_t texa, int& w, int& h, std::vector<uint8_t>& rgba, bool limitar512 = false,
               int x0 = 0, int y0 = 0, int rw = 0, int rh = 0) const;
  // R17: bloques GS de 256 bytes leidos por Textura, incluida su paleta. Solo cambian al cambiar sus bytes.
  std::vector<uint16_t> PaginasTextura(uint64_t tex0) const;
  uint64_t Revision(const std::vector<uint16_t>& paginas) const;
  std::vector<std::pair<uint32_t, uint32_t>> LecturasTextura(uint64_t tex0) const;
  uint64_t FirmaLecturas(const std::vector<std::pair<uint32_t, uint32_t>>& lecturas) const;
  // R18: bloques en orden LOGICO y paleta exacta. La firma no depende de donde IW aloja el atlas.
  uint64_t FirmaTextura(uint64_t tex0, int x0 = 0, int y0 = 0, int rw = 0, int rh = 0) const;
  uint32_t Dir32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const;              // palabra
  uint32_t Dir8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const;               // byte
  uint32_t Dir4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const;               // nibble
  std::vector<uint32_t> m = std::vector<uint32_t>(1u << 20);
  uint64_t version = 0, sin_formato = 0;
  uint64_t paginas[16384]{};
 private:
  mutable uint64_t firmas_paginas_[16384]{}, firmas_revision_[16384]{};
  struct SubidaCache {
    std::vector<uint16_t> bloques;
    std::vector<uint8_t> datos;
    std::vector<uint64_t> mascaras;
    uint64_t uso = 0;
  };
  std::map<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>, SubidaCache> subidas_cache_;
  size_t subidas_bytes_ = 0;
  bool SubirBloques(const Subida& s);
};

class Traductor {
 public:
  Traductor() { g_[0x1A] = 1; }                            // PRMODECONT tras el reinicio del GS
  void Registro(int reg, uint64_t v);                       // escritura de registro GS ya resuelta (A+D / REGLIST)
  void Gif(const uint8_t* d, size_t n);                     // flujo GIF (PATH3 o DIRECT del VIF1)
  void Imagen(const uint8_t* d, size_t n);                  // datos IMAGE
  void FinCuadro() { prims.clear(); subidas.clear(); }      // el pintor ya los ha consumido
  std::vector<Primitiva> prims;
  std::vector<Subida> subidas;
  bool terminado = false;                                  // FINISH consumido por el GS sincrono (CSR bit 1)
  std::string Texto() const;                                // volcado de la prueba P3 (mismo formato que el gemelo)

 private:
  void Empaquetado(int r, uint64_t a, uint64_t b);
  void Vertice_(uint64_t v, bool f, bool kick);
  uint64_t g_[0x64] = {};   // registros GS por numero
  uint32_t q_ = 0x3F800000; // Q pendiente de ST (modo PACKED)
  Vertice cola_[3];
  int n_ = 0, total_ = 0;   // vertices en cola / desde el ultimo PRIM
};

// Productor (TRAD/2b): registros DMA de VIF1 (0x10009000) y GIF (0x1000A000). Al escribir CHCR con STR se recorre la
// cadena (normal / cadena de origen con TTE, mismo recorrido que tools/iw_mips/gs_captura.py) y lo que va al GS (GIF
// entero; del VIF1 solo DIRECT/DIRECTHL) entra en el traductor. Lo que alimenta a la VU1 se cuenta y se salta.
using Lee64 = uint64_t (*)(void* ctx, uint32_t a);
using LeeBloque = const uint8_t* (*)(void* ctx, uint32_t a, uint32_t n);
struct Dma {
  bool Escribir(uint32_t a, uint32_t v, Traductor& t, void* ctx, Lee64 r);   // true si `a` es suyo
  void Envio(int canal, uint32_t chcr, uint32_t madr, uint32_t qwc, uint32_t tadr, Traductor& t, void* ctx, Lee64 r,
             LeeBloque bloque = nullptr);
  uint32_t reg[3][4] = {};  // [canal][CHCR, MADR, QWC, TADR]
  uint32_t cl = 1, wl = 1;  // STCYCL del VIF1
  uint64_t envios = 0, vu1 = 0, errores = 0;
  // B2: frontera en la cadena ORIGINAL aun pendiente de envio. No cambia ni ejecuta ordenes.
  uint32_t corte_direccion = 0;
  size_t corte_primitivas = size_t(-1);
  // R23 G2 (pruebas): entrada de la VU1 del cuadro en el formato de gs_captura (U/M/P, md5) para compararla con el oraculo.
  bool vif_registrar = false;
  std::string vif_texto;
};

Traductor& Global();        // el del juego (vacio mientras dbz3_iw_traducido este apagado)
Dma& GlobalDma();
bool Activo();              // ajuste dbz3_iw_traducido
bool DisponibleIW();        // ISO EU utilizable (o punto de prueba explicito); sin ella se usa la recreacion
bool Panoramico();          // ajuste dbz3_trad_panoramico (de serie encendido)
void CajaX(float W, float H, float& ancho, float& ox);   // 2D 4:3 centrado: ancho y desplazamiento x
bool ActivoB2();            // ajuste dbz3_b2_traducido (tablero de Budokai 2; de serie apagado)
// Datos de entrada de UNA partida, en los registros originales de FUN_00238320/00238870.
// Se copia antes del hilo de arranque: nunca contiene punteros ni relojes del port.
struct PartidaB2 {
  uint64_t entrada = 0;
  bool reanudada = false;
  uint16_t turno = 0;
  uint8_t fase = 0, bolas = 0, radar = 0, musica = 0xFF;
  uint32_t banderas[30]{};   // 0x699260, 960 banderas
  uint8_t piezas[0x480]{}, objetos[0x40]{}, casillas[0x80]{}, bolas_fase[6]{0xFF,0,0xFF,0,0xFF,0};
  bool guardado_valido = false;
  uint8_t guardado[0xA78]{}; // 69380C..694283: equipo, inventario, historial y estado serializado original
};
bool CuadroB2(uint16_t botones, int fase = 0, bool comic = false, const PartidaB2* partida = nullptr);
// R18: vista de la RAM de LA sesion que dibuja el GS. El port no ejecuta reglas en paralelo.
struct EstadoTableroB2 {
  PartidaB2 partida;
  uint32_t funcion = 0, paso = 0;
  int seleccion = 0, destino = 0, jugador = 0, rival = 0, casilla_combate = 0;
  bool combate = false, salida = false;
  int victoria = 0;
};
bool LeerTableroB2(EstadoTableroB2& estado);
bool ResultadoTableroB2(int ganador, int vida);
bool GuardarTableroB2(PartidaB2& partida);
// Solo piloto de pruebas: destino de cursor y pulsacion, consumidos por las funciones originales.
void PilotoTableroB2(int destino, uint16_t boton);
// figura 3D del jugador en el tablero como ORDEN (x, y en la pantalla logica de 640x448; anim = banco << 16 | clip)
void PonerFiguraB2(void (*f)(float x, float y, uint32_t anim, float cuadro, float escala));
// Una tanda por lista original. Tipo 4 = Goku pequeno; tipo 3 = carta CMN 335.
// El Saibaman conserva las capas AMA del ELF. idx es el par de texturas del retrato/dorso.
struct FiguraTableroB2 {
  int id = 0, traje = 0, tipo = 4, slot = 0, idx = 0;
  float x = 0, y = 0, cuadro = 0, escala = 1, giro = 0;
  float atenuacion = 0, distancia = 63;
  bool variante = false;
  // escala conserva el +0x14 original: mezcla de pose de Goku, NO tamano del modelo.
  uint32_t anim = 0;
};
void PonerFigurasB2(void (*f)(const std::vector<FiguraTableroB2>&));
bool CuadroBucle(uint16_t botones);   // iw_trad_bucle.cpp: una vuelta del bucle principal del IW original
// TRAD/5c: con el arranque desde el ELF, la ronda de 0x7D5 que se juega es la del original. Arrancando() = el ELF aun va
// por sus menus (el port espera); LeerD5 = el trabajo de MiniGame (*0x49CBA8) y la QTE (*0x49CD70) del original.
struct EstadoD5 {
  int fase = 0, sub = 0, ronda = 0, n = 0, cuenta = 0, rango = 0, juicio = 0;
  float total = 0, pts[10]{}, cat[3]{}, x[10]{};   // TRAD/6: 0x7DA (NB012) tambien
  uint8_t seq[10]{}, w80 = 0, vivo[10]{}, fallado[10]{}, acierto[10]{}, w93[10]{};
};
bool Arrancando();
void NuevaEntrada();   // TRAD/5e: entrar en 7D5 otra vez = el original vuelve a la foto de tras el arranque
bool Precalentar();   // TRAD/5d: arranca el original en segundo plano (al entrar en el mapa de IW); idempotente
bool LeerD5(EstadoD5& s, uint32_t pw = 0x49CBA8);   // pw: puntero al trabajo (7D5 0x49CBA8, 7DA 0x49CDC8)
// TRAD/6: minijuegos de dos carriles (7DB NB013, 7D4 NB006, 7DC NB014): lo que lee el port del trabajo del original
struct EstadoBot {
  int fase = 0, sub = 0, rango = 0, activo = 0, choques = 0, n_ic[2]{}, aciertos[2]{}, intentos[2]{}, llegaron[2]{};
  uint64_t campo_intro7db = ~uint64_t(0);
  int paso_anim7db = -1, lineas7db[2] = {-1,-1}, codigo7db[2]{};
  float cuadro7db[2] = {-1.f,-1.f};
  int paso_apertura = -1;   // NB013: ce7c+0x10, solo mientras fase=0
  int cam_intro = -1;       // 0 = ranura 1/clip 0; 47E y 186 = ranura 0
  float cuadro_intro = 0;   // misma instantanea que fase/sub
  // NB013: estado y boton del MISMO original que publica fase/sub (Mg_UpdateClashPhase).
  int choque_fase = 0, choque_lado = 0, pulsos = 0, objetivo = 0, choque_boton = -1, marca_indice = -1;
  float marca_x[3]{}, marca_y = 0;
  float nota = 0, vida[2]{};
  int fondo = -1;   // IW-08: escenario del original visible (1) u oculto (0, choque de 7DB: Mg_StartClashSequence EU 0x352A50)
  int cfg_nivel = 0, cfg_puntos[5]{};   // PAQ_5: 0x7DC (QteMg_FinalizeResult: pw+0x18 y 0x481920..0x481934)
  uint8_t banderas[6]{};                // PAQ_4: 0x7DB, seis bytes +0x2F8..+0x2FD (solo diagnostico)
  struct Ic { int k = 0, boton = 0; float x = 0; bool vivo = false, fallado = false; } ic[2][15];
};
bool LeerBot(uint32_t clave, EstadoBot& s);
bool LeerPalabra(uint32_t direccion, uint32_t& valor);
uint32_t DirIW(uint32_t eu);   // R23 IW-07: direccion EU del original -> la del ELF arrancado (EU o USA)   // R22: RAM del original, solo sin trabajador (7DD, cotejos)
// R23 BOCA-02: llamadas del original a MinigameMouthAnimations (EU 0x207080): lado = personaje +0x1380, clip del banco 0
bool TomarBoca(int& lado, int& clip);
// R23 IW-06: 'C' camara (UiAnim_Start EU 0x2ED8F0) y 'V' vibracion (s_UTL_ACTUATE EU 0x2EFA10) del original; con dbz3_iw_r23_eventos
bool TomarEvBot(char& tipo, int& n);
// MSN019 desde el ELF: el original decide juicio, puntos y rango; la UI solo alimenta el piloto de pruebas.
struct Estado19 {
  uint32_t estado = 0;
  int fila = 0, n = 0, aciertos = 0, intentos = 0, reaccion = 0, total = 0, rango = 0;
  bool fin = false;
  float cursor = 0;
  int puntos[10]{};
  struct Objeto { float x = 0; int tipo = 0, estado = 3; bool visible = false; } obj[10];
};
bool Leer19(Estado19& s);
void PonerClave(uint32_t clave);   // TRAD/6: el arranque previo escribe esta mision (indice/clave) en el vblank 4201; cambiarla lo reinicia

}  // namespace dbz3::iwtrad

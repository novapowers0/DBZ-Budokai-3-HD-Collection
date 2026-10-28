// dbz3 - Animaciones de Infinite World en el motor de B3 (frente R10, 2026-10-16): el "scatto" de los minijuegos de
// campo de IW. Minigame_State14_DashStartEnter / State15_DashEnter (decomp EU 0x2A0CD0) / State16_Enter ponen los
// codigos 0x19 (arranque, 12 cuadros) -> 0x1A (bucle, 60) -> 0x1B (frenada, 22); en el aire 0x1C/0x1D/0x1E. El #BSK
// comun de IW (DATA_CMN 204 hijo 3) los lleva a las animaciones 257..267 de su almacen global (204 hijo 0: los mismos
// 46 huesos GOK_* y en el mismo orden que el global de B3, 165 hijo 0). B3 no las tiene: se leen en marcha de la copia
// del jugador y se anaden detras de las 359 del almacen global de B3 (sin tocar ninguna) con un #CSK propio de 6
// codigos en un hueco libre de los 3 #CSK del luchador. Andar sigue siendo el de B3 (el andar de IW es el mismo).
// (El estado 9 / codigo 0xE2 es el corredor de la IA, Bubbles o Piccolo: solo el #BSK de Bubbles, 912, lo define.)
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace dbz3::anim {

enum Fase : int { kApagado = 0, kInicio = 1, kBucle = 2, kFin = 3 };

// Pide la fase del scatto para el luchador x (direccion del luchador, p. ej. mgr + 2096). Cualquier hilo; se aplica
// en el siguiente cuadro en el hilo del juego (gancho propio al leer el mando). kInicio pasa sola a kBucle al acabar
// su animacion; kFin pasa sola a kApagado, que deja al luchador en guardia (accion 0) y al juego andar como siempre.
// Repetir la misma fase no hace nada. aire: las variantes 0x1C-0x1E.
void Scatto(uint32_t x, Fase f, bool aire = false);
// Fase en curso (la que se ve).
Fase FaseScatto();

// Corredor de la IA de IW (codigo 0xE2, estado 9 de MgPly) en bucle para cualquier luchador (p. ej. Piccolo, el
// segundo luchador, mgr + 2096 + 8240), aparte del scatto. IW solo define 0xE2 para Bubbles (DATA_CMN 912, esqueleto
// de mono): para humanos se usa el unico ciclo de carrera humano de sus datos, el de Piccolo (DATA_CMN 925, codigo 4,
// 32 cuadros). false: a la guardia.
void Corredor(uint32_t x, bool on);
// Andar libre de los campos de IW (auditoria6): una animacion de IW en bucle, como el corredor. kQuieto = el reposo de
// pie de IW (#BSK comun codigo 0x00, Char_EnterPoseState0), kCorrer = la carrera del #BSK comun (codigo 0x16, la del
// estado 3 de IW, +0xB4 = 4.0; elegida entre 0x07/0x08/0x15/0x16/0xE2 viendolas en el juego). Con la misma no hace nada.
// El reposo es el 0x00 comun (Char_EnterPoseState0: la pose de espera de IW, 61 cuadros). DATA_CMN 914 es Cell
// semiperfecto (huesos CEL_*), no un humano: sus animaciones solo valen para Cell (minijuego de Ten Shin Han).
constexpr uint32_t kQuieto = 0x800, kCorrer = 0x816;
// DS89: la maquina de estados de tierra de IW (Minigame_StateXX, decomp EU 0x2A0xxx-0x2A4xxx). Sus codigos 0x01 y
// 0x1F-0x25 son los del #BSK de DATA_CMN 914 (Cell/Gohan nino), pero el #BSK comun 204 NO los tiene: alli el mismo
// almacen global se pide con otros codigos (0x11 = anim 16 frenada; 0x103 = 23 flexion; 0x104 = 24 subida; 0x108 = 25
// aire; 0x107 = 26 caida; 0x105 = 27 aterrizaje; 0x106 = 28 vuelo; 0x109 = 29 despegue; 0x02 = 1 reposo). Estos alias
// 0x8xx/0xAxx dicen el codigo real de cada BSK, sin inventar animaciones (direcciones: State0C 0x2A2BF0, State0D
// 0x2A29A0, State0E 0x2A2700, State0F 0x2A2180, State10 0x2A1CA0, State11 0x2A19C0, State12 0x2A1760, State1 0x2A4820,
// Char_EnterState4 0x2A3DE0).
constexpr uint32_t kReposo = 0x802;      // 204 0x02 (= anim 1, la del estado 1)
constexpr uint32_t kParar = 0x811;       // 204 0x11 (= anim 16, la del estado 4)
constexpr uint32_t kSaltoPrep = 0xA03;   // 204 0x103 (= anim 23, la de 0x1F)
constexpr uint32_t kSaltoSube = 0xA04;   // 204 0x104 (= anim 24, la de 0x20)
constexpr uint32_t kSaltoAire = 0xA08;   // 204 0x108 (= anim 25, la de 0x21)
constexpr uint32_t kSaltoCae = 0xA07;    // 204 0x107 (= anim 26, la de 0x22)
constexpr uint32_t kAterrizar = 0xA05;   // 204 0x105 (= anim 27, la de 0x23)
constexpr uint32_t kSaltoVuelo = 0xA06;  // 204 0x106 (= anim 28, la de 0x24)
constexpr uint32_t kDespegueVuelo = 0xA09;   // 204 0x109 (= anim 29, la de 0x25)
constexpr uint32_t kRebote = 0x82A;      // 204 0x2A (estado 0x17; en 914 no existe)
constexpr uint32_t kRebote2 = 0x82B;     // 204 0x2B (estado 0x18; en 914 no existe)
constexpr uint32_t kDashAereoIni = 0x83D;    // 204 0x3D (estado 0x19 en el suelo)
constexpr uint32_t kDashAereo = 0x83E;       // 204 0x3E (estado 0x19 en el aire)
// DS91: pared. La maquina de estados SI tiene dos acciones de pared, que DS89 no conecto porque no las reconocio:
//   0x1F (Minigame_State1F_Enter EU 0x29E5D0) -> codigo 0x3F, +0x1850 -= 10, vibracion, efecto 0x0D (wall kick)
//   0x20 (Minigame_State20_Enter EU 0x29E260) -> codigo 0x40 en suelo / 0x41 en el aire, efecto 6 (rebote)
// Las elige Char_GetNextActionCode (EU 0x2A9440) por el tipo de superficie del barrido: Char_CheckActionAllowed2
// (EU 0x2A5F40) con superficie +0x1e8 == 3 -> 0x1F; Char_CheckActionAllowed (EU 0x2A5DE0) con +0x1e8 == 4 -> 0x20.
// En el #BSK comun 204: 0x3F -> anim 297 (60 cuadros), 0x40 -> anim 150 (40), 0x41 -> anim 158 (40). Los tres existen.
constexpr uint32_t kParedKick = 0x83F;       // 204 0x3F (estado 0x1F: patada contra la pared)
constexpr uint32_t kParedRebote = 0x840;     // 204 0x40 (estado 0x20 en el suelo)
constexpr uint32_t kParedReboteAire = 0x841; // 204 0x41 (estado 0x20 en el aire)
// Cell (914) si trae sus 0x1F-0x24; se instalan con alias propio para no usar los huesos GOK_* del comun.
constexpr uint32_t kCellPrep = 0xC05, kCellSube = 0xC06, kCellAire = 0xC07, kCellAterrizar = 0xC08,
                   kCellDespegueVuelo = 0xC09, kCellParar = 0xC0A;
// Andar (palanca a menos de 0.9, IW estado 2: codigo 0x15 del #BSK comun). Salto del estado 0x13: 0x29 (30 cuadros,
// rodillas recogidas). kCaer (0x104) es el alias viejo; en 204/0x104 es la animacion 24 (subida, la misma que pide el
// estado 0x0D), no una caida: desde DS89 el salto completo usa kSaltoPrep/kSaltoSube/kSaltoAire/kSaltoCae/kAterrizar.
constexpr uint32_t kAndar = 0x815, kCaer = 0xA04, kSubir = 0x829;
// DS37 (0x7DD): el minijuego 14 de IW lo juega Cell semiperfecto (SONNET1 §4), no Gohan nino: su #BSK es DATA_CMN
// 914 y trae su propia locomocion en los mismos codigos que el comun (0x00 reposo, 0x15 andar, 0x16 correr, 0x22 y
// 0x29 salto). Se instalan con estos alias para no pisar las del #BSK comun (kQuieto/kAndar/kCorrer/0x822/kSubir).
// El cambio de modelo (iw_minijuegos.cpp) NO es de este fichero: ver el informe de DS37.
constexpr uint32_t kCellQuieto = 0xC00, kCellAndar = 0xC01, kCellCorrer = 0xC02, kCellSalto = 0xC03,
                   kCellDespegue = 0xC04;
void Bucle(uint32_t x, uint32_t codigo);
// Cansado al vaciarse la barra del scatto: en IW State16_Update (EU 0x2A04A0) con la barra a 0 pasa a
// Char_EnterState7Transform: codigo 0x05 y luego 0x06 (Char_UpdateAnimToggle), las animaciones 4/5 del almacen
// global, iguales en B3 (no hace falta portarlas). true: 0x05 y despues 0x06 en bucle; false: a la guardia.
void Cansado(uint32_t x, bool on);
// R16: animacion de IW suelta (entrada 0x11D, comienzo y final de los minijuegos de campo; ver kExtras en anim_iw.cpp)
// una vez; al acabar, a la guardia, o sujeta en su ultimo cuadro (sujetar). codigo 0: quitarla.
// tope > 0: se queda quieta en ese cuadro (una pose de la animacion, p. ej. el salto con las rodillas recogidas)
void Reproducir(uint32_t x, uint32_t codigo, bool sujetar = false, uint32_t tope = 0, uint32_t luego = 0);
// luego != 0: al acabar la pose pasa sola a ese codigo en bucle (el reposo de IW; los minijuegos de botones).
// 0 nada pedido, 1 en curso, 2 acabada (o sin esa animacion en la copia del jugador).
int EstadoPose();
// R14 (minijuegos de botones NB006/NB013/NB014): las poses de IW de cada personaje salen de su #BSK de DATA_CMN
// (cmn = 922 Goku, 920 Gohan joven, 931 Vegeta, 928 Trunks, 921 Gohan nino, 925 Piccolo; 0x57A-0x57D, las de
// espera, de 915: se piden con cmn 915). El mismo codigo se repite entre personajes, asi que van con un alias propio
// (0xB00...): este lo da. 0 = esa pose no esta en la lista de kQte (anim_iw.cpp) o no la trae la copia del jugador.
uint32_t CodigoQte(uint32_t cmn, uint32_t codigo);
// iw_fix: altura (y de la cadera, unidades de IW) de la animacion de IW que lleva puesta el luchador x en este cuadro, para que
// la camara del final siga el salto de superado (0x52: hasta 69); -1 si no lleva ninguna (o va mezclando). Hilo del juego.
float AlturaRaiz(uint8_t* base, uint32_t x);
// Lo mismo con la posicion entera de la cadera (x, y, z; el salto de superado de IW va 50 u hacia delante, z): false si no hay.
bool RaizActual(uint8_t* base, uint32_t x, float out[3]);
// DS103: estado REAL del motor de animacion de un cuerpo del luchador x (0 = A, X+0x650; 1 = B, X+0xCD0), para la
// traza del minijuego. `codigo` = A+0x520 (lo que pide la accion), `clip` = A+0x604 (almacen<<16 | animacion, lo
// que resolvio el #CSK), `cuadro` = A+0x608 (reloj), `reloj` = A+0x63C (DS105: reloj del TERCER clip A+0x638, NO el
// cuadro que se dibuja), `cuadros` = A+0x60C (duracion del clip), `congelado` = A+0x66C, `bloque` = A+0x524 (bloque
// del #CSK), `oculto1/2` = las dos marcas de oculto del cuerpo (3260/3261 y 4924/4925). `hay` = tiene modelo valido.
struct EstadoCuerpo {
  bool hay = false;
  uint32_t objeto = 0, modelo = 0, acm = 0, csk[3] = {0, 0, 0};
  uint32_t codigo = 0, clip = 0, cuadros = 0, vueltas = 0, bloque = 0;
  float cuadro = 0.0f, reloj = 0.0f;
  uint8_t congelado = 0, oculto1 = 0, oculto2 = 0;
};
bool LeerEstadoCuerpo(uint8_t* base, uint32_t x, int cuerpo, EstadoCuerpo& e);

// =====================================================================================================================
// DS37 - Sprites 2D de Infinite World (#AMA): motor de animacion por nodos, segmentos y pistas.
//
// Fuente: iw_sles55347.c (decomp EU). Formato del disco: MAPAS_CODIGO/ANIMACIONES.md §2. El motor original vive en
// 0x1B1860-0x1B9510 (pool de sprites, nodos, pistas y muestreadores) y el modo de HUD en 0x2FFD30.
//
// Esta parte NO dibuja: entrega, por nodo visible, una Entrada (rect escalado, ventana de textura, tinte, textura,
// modo de mezcla y giro) que pinta el HUD del port con su atlas de imagenes (el mapa de IW ya lo hace en pasarela_iw.cpp).
// Los cuatro muestreadores que pide el encargo (escala 0x1B65D0, cuadro 0x1B5440, spline 0x1B5F40 y color 0x1B4FC0)
// son reutilizables por cualquier pantalla: ver MuestrearEscala/Cuadro/Posicion/Color.
namespace ama {

// Tipo de la entrada de la tabla de tiempos de una pista (IW:151450-151457).
enum TipoClave : int { kEscalon = 0, kLineal = 1, kSpline = 2, kSinDato = 3 };
// Entrada de la tabla de tiempos: 4 B {u16 cuadro, u16 tipo}.
struct Tiempo {
  uint16_t cuadro = 0, tipo = 0;
};
// Registro de la pista de eventos (16 B): id de la tabla iRam0049ca2c (-1 = ninguno) y tres argumentos.
struct Evento {
  int32_t id = -1;
  uint32_t arg[3] = {0, 0, 0};
};
// Un canal de una pista (claves de 0x20 B). Los campos se leen por tipo con los accesores.
struct Canal {
  uint32_t n = 0;      // numero de claves (+0x04 canal 1 / +0x08 canal 2)
  uint32_t dur = 0;    // duracion en cuadros (+0x06 / +0x0A)
  bool bucle = false;  // bit0 del canal 1 / bit1 del canal 2 de los flags de la pista
  std::vector<Tiempo> tiempos;
  std::vector<uint8_t> claves;   // n * 0x20 B, tal cual el disco
  uint8_t U8(size_t clave, size_t off) const;
  uint16_t U16(size_t clave, size_t off) const;
  uint32_t U32(size_t clave, size_t off) const;
  float F32(size_t clave, size_t off) const;
};
// Pista = dos canales con cabecera comun de 0x20 B (IW:151630-151781).
struct Pista {
  Canal c1, c2;
};
// Pista de efectos del segmento (+0x0C): cabecera de 0x30 B con tres canales de 0x20 B por clave.
//   +0x00 flags (bit0 bucle escala, bit1 bucle posicion, bit2 bucle color; 0x08/0x10/0x20 = usar la escala/posicion/
//   color del propio nodo en vez del canal), +0x04/0x06 escala (n/duracion), +0x08/0x0A posicion, +0x0C/0x0E color,
//   +0x18/0x1C tiempos/claves de escala, +0x20/0x24 de posicion, +0x28/0x2C de color (IW:149835-150061).
struct Fx {
  uint32_t flags = 0;
  Canal escala, pos, color;
};
// Segmento de animacion (apuntado por nodo +0x10): cadena por +0x18, duracion en +0x1C y rect local en +0x20.
struct Segmento {
  uint32_t flags = 0;         // +0x00: bits 0x6 van a la entrada; (flags & 0xFF00) >> 8 = modo (tabla de 16)
  Pista a, b;                 // +0x04 escala/posicion y +0x08 cuadro/color
  uint32_t efectos_crudo = 0; // +0x0C pista de efectos (direccion en el fichero)
  Fx fx;                      // la misma pista ya leida (vacia si no hay)
  bool tiene_fx = false;
  std::vector<Evento> eventos;   // +0x10 pista de eventos (solo las de tipo 1 disparan)
  std::vector<Tiempo> eventos_tiempos;   // tabla de tiempos de la pista de eventos (mismo indice que `eventos`)
  uint32_t eventos_dur = 0;      // duracion del canal de eventos (+6 de su cabecera)
  bool eventos_bucle = false;    // bit0 de los flags de la pista de eventos
  float rect[4] = {0, 0, 0, 0};   // +0x20 x0, y0, x1, y1
  uint32_t duracion = 0;          // +0x1C cuadros
  int siguiente = -1;             // +0x18, -1 = fin
  bool TieneRect() const { return rect[0] != 0.0f || rect[1] != 0.0f || rect[2] != 0.0f || rect[3] != 0.0f; }
};
// Nodo en disco (IW:153994-153996): hijo +0x04, hermano +0x08, padre +0x0C, animacion +0x10.
struct NodoDisco {
  int hijo = -1, hermano = -1, padre = -1, anim = -1;
};
// #AMA de disco (cabecera +0x10 objetos, +0x14 tabla de nodos raiz).
class Obra {
 public:
  bool Cargar(const uint8_t* datos, size_t tam);
  bool Cargar(const std::vector<uint8_t>& d) { return Cargar(d.data(), d.size()); }
  bool Valida() const { return valida_; }
  uint32_t NumObjetos() const { return uint32_t(objetos_.size()); }
  int Objeto(uint32_t i) const;   // indice de nodo de la tabla +0x14; -1 si no
  int Raiz() const { return raiz_; }
  size_t NumNodos() const { return nodos_.size(); }
  size_t NumSegmentos() const { return segmentos_.size(); }
  const NodoDisco& Nodo(int i) const { return nodos_[size_t(i)]; }
  const Segmento& Seg(int i) const { return segmentos_[size_t(i)]; }
  // Construccion (la usa el lector de #AMA de anim_iw.cpp).
  std::vector<NodoDisco>& Nodos() { return nodos_; }
  std::vector<Segmento>& Segs() { return segmentos_; }
  std::vector<int>& Objetos() { return objetos_; }
  std::map<uint32_t, int>& MapaSegs() { return seg_map_; }
  void PonerRaiz(int r) { raiz_ = r; }
  void PonerValida(bool v) { valida_ = v; }

 private:
  std::map<uint32_t, int> seg_map_;
  bool valida_ = false;
  int raiz_ = -1;
  std::vector<NodoDisco> nodos_;
  std::vector<Segmento> segmentos_;
  std::vector<int> objetos_;
};
// Nodo en memoria (IW:153988-153994): +0x04 flags (bit0 existe, bit1 muestrear), +0x10 tiempo local, +0x14..+0x24
// cache del indice de clave por canal, +0x2C hijo, +0x28 hermano.
struct Nodo {
  int disco = -1;
  int padre = -1, hermano = -1, hijo = -1;
  uint32_t flags = 0;
  float tiempo = 0.0f;
  int seg = -1;
  int c_escala = -1, c_pos = -1, c_cuadro = -1, c_color = -1, c_evento = -1;
  uint32_t fin = 0;
};
struct Arbol {
  const Obra* obra = nullptr;
  std::vector<Nodo> nodos;
  int raiz = -1;
};
// SprAnim_CreateNodeTree (EU 0x1B9510): crea la copia en memoria del arbol del objeto `nodoDisco`.
Arbol CrearArbol(const Obra& obra, int nodoDisco = 0, float tiempo = 0.0f);
// SprNode_MarkTreeActive (EU 0x1B82E0): fija el tiempo y el bit 1 en todo el arbol.
void MarcarArbol(Arbol& a, float tiempo);
// SprNode_GetMaxFrameCount (EU 0x1B23E0) y SprNode_HasNoAnimData (EU 0x1B2390).
uint32_t MaxCuadros(const Arbol& a);
bool SinAnimacion(const Arbol& a);

// Muestreadores (funciones reutilizables; `desde` = cache del indice de clave, -1 = buscar).
// EU 0x1B3450 SprAnim_FindKeyIndex.
int BuscarClave(const Canal& c, float t, int desde);
// EU 0x1B5720 (limita o envuelve el tiempo segun el bucle del canal).
float TiempoCanal(const Canal& c, float t);
// EU 0x1B65D0: escala 2D. out[0] sx, out[1] sy, out[2] giro; [3..7] como en el original (se copian tal cual cuando la
// clave es de escalon o extremo). Sin pista: (1, 1, 0).
void MuestrearEscala(const Canal& c, float t, int& desde, float out[8]);
// EU 0x1B5440: cuadro/textura. `crudo` = u16 de textura (bajo) + u16 extra; `tam` = u32 de +4; `uv` = ventana de
// textura en TEXELES (u0, v0, u1, v1; p. ej. las nubes del mapa traen 0,0,112,64 = el tamano de su imagen), no 0..1.
// Medido en 334/2: las texturas de los seis nodos son 4,3,5,7,6,8, las mismas de kNubes en pasarela_iw.cpp.
struct MuestraCuadro {
  uint32_t crudo = 0xFFFF;   // u16 textura + u16 extra
  uint32_t tam = 5;
  float uv[4] = {0, 0, 0, 0};   // texeles
};
void MuestrearCuadro(const Canal& c, float t, int& desde, MuestraCuadro& out);
// EU 0x1B5F40: posicion (vec4) con spline Catmull-Rom (tipo 2) o lineal con curva (tipo 1). Sin pista deja `out`
// como estaba (el llamador lo pone a 0: es el desplazamiento local, y la posicion heredada la suma la Entrada).
void MuestrearPosicion(const Canal& c, float t, int& desde, float out[4]);
// EU 0x1B4FC0: color. Interpola el tinte (vec4) y los bytes RGBA con sus dos curvas; el modo de mezcla es el de la
// clave actual. Sin pista: tinte (1,1,1,1), bytes 0 y modo 1.
struct MuestraColor {
  float tinte[4] = {1, 1, 1, 1};
  uint8_t rgba[4] = {0, 0, 0, 0};
  uint32_t modo = 1;
};
void MuestrearColor(const Canal& c, float t, int& desde, MuestraColor& out);

// Estado heredado por los nodos (IW 0x49c9d8, 0x50 B por nivel): posicion, tinte, suma de color, escala de posicion y
// de tamano y giro. El port lo rellena desde su pantalla (los globales de IW viven en pasarela_iw.cpp).
struct Contexto {
  float pos[4] = {0, 0, 0, 0};
  float tinte[4] = {1, 1, 1, 1};
  uint8_t rgba_add[4] = {0, 0, 0, 0};
  float escala_pos[2] = {1, 1};   // escala de la posicion (IW ctx +0x30/+0x34)
  float escala_tam[2] = {1, 1};   // escala del rect (IW ctx +0x38/+0x3C)
  float giro = 0.0f;              // grados que se suman al giro de cada entrada y giran los desplazamientos (+0x40)
};
// Entrada de dibujo (Sprite_BuildDrawEntry, EU 0x1B25E0): todo lo que necesita el pintor.
struct Entrada {
  // +0x00: el GIRO en grados, no la profundidad (lo demuestra Sprite_DrawEntry 0x1B2E00: si vale 0 no rota y si no
  // llama a FUN_00127980 con (valor*32768/180), IW:149287-149334). El mapa de ANIMACIONES.md lo llamaba profundidad.
  float giro = 0.0f;
  uint32_t flags = 0;         // +0x04
  uint32_t modo = 0;          // +0x08 modo de mezcla (0..3; ver ANIMACIONES.md §2.5)
  uint32_t cuadro_tam = 0;    // +0x0C dword de la clave de cuadro
  uint16_t textura = 0;       // +0x18
  uint8_t rgba[4] = {0, 0, 0, 0};   // +0x1C bytes (suma del contexto y del color, tope 255)
  float pos[4] = {0, 0, 0, 0};      // +0x20
  float rect[4] = {0, 0, 0, 0};     // +0x30 rect ya escalado (x0,y0,x1,y1)
  float uv[4] = {0, 0, 0, 0};       // +0x40 ventana de textura en texeles (ver MuestraCuadro)
  float tinte[4] = {1, 1, 1, 1};    // +0x50
  // true si no trae textura (0xFFFF): el objeto raiz de la capa de nubes del mapa es asi (velo a pantalla completa).
  bool SinTextura() const { return textura == 0xFFFF; }
};
// Quad listo para el pintor (Sprite_DrawEntry, EU 0x1B2E00, sin el estado del GS): cuatro esquinas giradas alrededor
// de `pos` y sus UV. El orden es (x0,y0) (x1,y0) (x1,y1) (x0,y1), el mismo de la tira del original.
struct Quad {
  float xy[4][2] = {};
  float uv[4][2] = {};
};
void DibujarEntrada(const Entrada& e, Quad& q);
// Muestras de un nodo en un cuadro (los cuatro canales que pide SprAnim_DrawAtTime).
struct Muestras {
  float escala[8] = {1, 1, 0, 0, 0, 0, 0, 0};
  float pos[4] = {0, 0, 0, 0};
  MuestraCuadro cuadro;
  MuestraColor color;
};
// EU 0x1B25E0 Sprite_BuildDrawEntry: compone la Entrada (rect escalado, posicion heredada + local, tinte, bytes,
// giro, textura y UV) de un segmento con las muestras ya tomadas. Falso si el segmento no tiene rect (no se dibuja).
// Lo usan SprAnim_DrawAtTime y SprAnim_UpdateAndDrawNode, como el original.
bool ConstruirEntrada(const Segmento& s, const Contexto& ctx, const Muestras& m, Entrada& e);
// EU 0x1B7520 SprAnim_DrawAtTime: muestrea las cuatro pistas del segmento en el tiempo dado. Devuelve true si hay
// entrada que pintar (rect no nulo); en ese caso rellena `e`.
bool DibujarEnTiempo(const Obra& obra, int seg, float t, const Contexto& ctx, Muestras& m, Entrada& e);
// EU 0x1B69B0 SprAnim_UpdateAndDrawNode: recorre el arbol y entrega una entrada por nodo visible (orden del original:
// hijo antes que hermano, como la recursion del decomp). `eventos` (opcional) recibe la pista de eventos del segmento
// cuando cambia de clave, como IW llama a iRam0049ca2c (IW:152136-152198).
using CbEvento = void (*)(void* user, const Evento& ev);
using CbEfecto = void (*)(void* user, const Segmento& seg, float t, Contexto& ctx);
void ActualizarNodo(Arbol& a, int nodo, Contexto ctx, std::vector<Entrada>& salida, CbEvento eventos = nullptr,
                    void* user = nullptr, CbEfecto efecto = nullptr);
// EU 0x1B7BB0 BackgroundDisplay: avanza el tiempo de la raiz (`factor` x velocidad del juego), la marca y la dibuja.
// `velocidad` = fRam0049ca1c (cuadros por cuadro; 1.0 en el mapa). Devuelve las banderas de fin (nodo +8).
uint32_t Fondo(Arbol& a, float factor, float velocidad, Contexto ctx, std::vector<Entrada>& salida,
               CbEvento eventos = nullptr, void* user = nullptr, CbEfecto efecto = nullptr);
// Aplica la pista de efectos (EU 0x1B3D50 Fx_ApplyKeyframeTracks) al contexto heredado, en el tiempo `t`; los bits
// 0x08/0x10/0x20 de la pista hacen que use la escala/posicion/color muestreadas del propio nodo (`m`).
void AplicarFx(const Segmento& s, float t, const Muestras& m, Contexto& ctx);
// EU 0x1B7810 SprAnim_GetTrackEndFlags: mascara de bits de las pistas terminadas en ese tiempo (1/2/4/8 canales,
// 0x10/0x20/0x40 efectos —posicion/color/escala—, 0x80 eventos; bit a 1 = terminada o sin datos). `seg` es el PRIMER
// segmento del nodo y `t` su tiempo: la funcion avanza el encadenado restando duraciones, como el original.
uint32_t BanderasFin(const Obra& obra, int seg, float t);

// Pool de 128 sprites de pantalla (IW 0x4C5200, 0x64 B cada uno; ANIMACIONES.md §2.7). En el original es un pool
// del guest con tablas de visuales en RAM; aqui cada registro lleva su arbol #AMA y el pintor de la pantalla pone el
// atlas. Flags de +0x00: 1 activo, 2 avanza su tiempo, 4 render de textura, 8 al terminar actualiza, 0x10 pausa,
// 0x20 varias raices, 0x40 mascara alterna, 0x80 oculto. +0x04 byte0: estado del fundido; byte5: 1 visuales/0 ruedas.
struct SpriteEstado {
  uint32_t flags = 0;
  uint8_t fundido = 0;
  uint8_t visuales = 0;
  float cuadro = 0.0f;      // +0x08
  float max = 0.0f;         // +0x0C (SprNode_GetMaxFrameCount)
  int32_t id = -1;          // +0x10 indice del objeto en el #AMA (lo usa Appear_FadeFlag_)
  int32_t slot1 = -1;       // +0x14
  int32_t slot2 = -1;       // +0x18
  int32_t anim = -1;        // +0x20 animacion actual en la tabla de visuales
  uint32_t mascara = 0;     // +0x44
  Arbol arbol;              // +0x48
};
class Pool {
 public:
  static constexpr int kMax = 128;
  SpriteEstado s[kMax];
  // EU 0x1B2030 MiscellaneousGameProperties_Display_: ocupa el primer registro libre y le pone el arbol ya creado.
  // `par1` (visuales/ruedas) va a +0x05; los tres indices de la tabla de visuales se guardan en id/slot1/slot2
  // (+0x10/+0x14/+0x18). Resolver esos indices a un arbol es del cargador (#AMA + tabla del modo): [H].
  int Crear(const Arbol& arbol, int id, int slot1 = -1, int slot2 = -1, uint32_t mascara = 1, int tipo = 1);
  // EU 0x1B1bb0 Sprite_SetFrame: cuadro acotado y propagado al arbol.
  static void PonerCuadro(SpriteEstado& e, float cuadro);
  // EU 0x1B1e20 Appear_FadeFlag_: pasa el registro al arbol de la animacion `slot` (en el original la busca en su
  // tabla de visuales; aqui la da quien llama), reinicia el cuadro, recalcula el maximo y lo pinta con
  // BackgroundDisplay(1.0): primer cuadro = `velocidad` (fRam0049ca1c). slot -1 = no hay animacion: no hace nada.
  void PonerAnim(SpriteEstado& e, const Arbol& nuevo, int slot, const Contexto& ctx, std::vector<Entrada>& salida,
                 CbEvento eventos = nullptr, void* user = nullptr, float velocidad = 1.0f);
  // EU 0x1B1960 Sprite_Update: callback del sprite (no modelado) y liberacion del arbol (1 raiz; el modo de varias
  // raices, bit 0x20, se deja al pintor: [H]).
  void Actualizar(SpriteEstado& e);
  // EU 0x1B1860 Sprite_UpdateAll: todos los activos, no pausados ni ocultos.
  void ActualizarTodos(const Contexto& ctx);
  // EU 0x1B1a00 GeneralGraphicEffects_Textures: una pasada de dibujo. modo 1: registros con mascara 0; modo 0 con
  // mascara 0: los que no llevan el bit 0x40; si no, mascara coincidente. Entrega las entradas y avanza el cuadro de
  // los que llevan el bit 2 (velocidad = fRam0049ca1c, 1.0). Devuelve el numero de registros dibujados.
  int Pases(int modo, uint32_t mascara, float velocidad, const Contexto& ctx, std::vector<Entrada>& salida,
            CbEvento eventos = nullptr, void* user = nullptr);
};

// EU 0x2FFD30 Sprite_DisplayHudSetByMode: objetos del HUD de cada modo (IW:388951-388999). `id` = indice del objeto
// en la tabla +0x14 del #AMA; -1 en slot1/slot2 = ninguno; mascara = rec+0x44 (1 mapa/HUD, 2 objeto 0). El objeto 0
// de los modos 0/2 no lleva callback de posicion (sus claves son pixeles de pantalla, 600x450); el resto si.
struct HudObjeto {
  int id = -1, slot1 = -1, slot2 = -1, mascara = 1;
  bool posicion_mapa = true;   // StoryMg_PositionAdjust (0x32B1A0)
};
size_t HudPorModo(int modo, const HudObjeto** lista);   // 0..5; lista terminada en id -1

}  // namespace ama

// =====================================================================================================================
// DS67 - Guion y efectos de IW: animaciones de efecto por ranura y comandos de la VM de guion.
//
// Fuente: iw_sles55347.c (decomp EU). Cada ranura de efecto ocupa 0x2140 B en la memoria del guest; los campos usados
// aqui son +0x4dc184 (tabla de bancos B), +0x4de1cc (flags), +0x4de1e4 (valor/cuadro B), +0x4de1ec (duracion B),
// +0x4de1f0 (base/velocidad B) y +0x4de1f4 (entrada B). El reloj es fRam0049c7dc (0x49c7dc) y, si existe, el bloque
// iRam0049ca70 (+0x8b7c pausa, +0x8b80 escala).
//
// IMPORTANTE: esto es la logica del original traducida y compilable; el llamante natural (la VM de guion del mapa y la
// historia) vive en src/pasarela_iw.cpp / src/escenarios.cpp y todavia no la usa. Hasta conectarla no cambia nada
// visible: se marca [VERIFICAR] en el informe DS67. El mapeo de huesos de la pista de efecto (FUN_0013bdf0) y el
// avance de la animacion del modelo de efecto (0x1EA300) son del motor de escenario: aqui se dejan como [H]/callback.
namespace efecto {

// EU 0x1ED730 Effect_SetValueBRelative: valor B = `valor` - base B de la ranura (una linea del original).
void ValorBRelativo(uint8_t* base, int slot, float valor);
// EU 0x1ED7F0 Effect_SetEntryBFromTable: fija la entrada B (id | 0x10000) y su duracion (cuadros de la tabla).
void EntradaBDeTabla(uint8_t* base, int slot, uint32_t id);
// EU 0x1ED3C0 Effect_AdvanceAnimFrame (contador): avanza el cuadro B de la ranura; true al dar una vuelta.
// El mapeo de huesos de la pista (FUN_0013bdf0) no se aplica aqui: es del motor de efectos del escenario [H].
bool AvanzarCuadroB(uint8_t* base, int slot, bool bucle);
// Estado de un comando de animacion de guion (los campos ctx+0x2a/0x2b/0x2c/0x30 del original).
struct GuionAnim {
  int slot = 0;              // ctx+0x2a
  int vueltas = 0;           // ctx+0x2b
  float temporizador = 0.0f; // ctx+0x2c
  float cuadros = 0.0f;      // ctx+0x30
};
// EU 0x1C11A0 ScriptCmd_PlayAnimTimed: arranca la entrada B (arg1) en la ranura (arg2) con `cuadros` (arg0; -1 =
// sin limite) e instala la espera por cuadros. Devuelve 3 = sigue, 1 = listo.
int ComandoAnimTemporizado(GuionAnim& g, uint8_t* base, int32_t cuadros, uint32_t entrada, int slot);
// EU 0x1C1100 ScriptWait_AnimFramesTimed: espera por cuadros con el mismo temporizador.
int EsperaAnimTemporizada(GuionAnim& g, uint8_t* base);
// EU 0x1C1320 ScriptCmd_PlayAnimLoops: arranca la entrada B (arg1) en la ranura (arg2) para `vueltas` ciclos (arg0)
// e instala la espera por ciclos. Devuelve 3 = sigue, 1 = listo.
int ComandoAnimVueltas(GuionAnim& g, uint8_t* base, int32_t vueltas, uint32_t entrada, int slot);
// EU 0x1C12B0 ScriptWait_AnimLoops: espera contando las vueltas completadas.
int EsperaAnimVueltas(GuionAnim& g, uint8_t* base);
// EU 0x1C1570 ScriptWait_ModelAnimCount: espera N ciclos de la animacion de un modelo de efecto. `avanzar` avanza un
// cuadro y devuelve true al completar una vuelta (en el port lo hace el motor de escenario, EU 0x1EA300 sustituta).
int EsperaModeloVueltas(int& vueltas, bool (*avanzar)(void* user, int slot), void* user, int slot);
// EU 0x25FA90 MouthAnimations: copia 9 matrices de hueso de boca (0x80 B) entre el hablante y el personaje.
// `hablante` = estructura con 9 indices s16 en +0 y el puntero al modelo en +0x1C; `personaje` = instancia con el
// puntero al modelo en +0x4C y 9 indices en +0x30. `invertir` != 0 copia del personaje al hablante.
void CopiarBoca(uint8_t* base, uint32_t hablante, uint32_t personaje, uint32_t invertir);

}  // namespace efecto

// =====================================================================================================================
// DS79 - Las 8 `falta` de ANIMACIONES: primitivas de la logica del original que no son del motor de sprites. No
// tienen llamante en el port (no carga personajes de escenario IW, ni el visor de IW, ni la tela PS2), asi que no
// cambian nada visible mientras no se conecten. Las operaciones del motor entran como callbacks (paso de material y
// modelo, pases de sombra, sonido de menu, tabla de anclajes). Las dos de tela PS2 (0x2603B0/0x2659B0) van a
// SUSTITUIDAS_ANIMACIONES.txt.
namespace puente {

using Accion = void (*)(uint8_t* base, uint32_t p);

// EU 0x1E3460 Stage_UpdateCharacterPose: material (+0xC) y modelo/pose (+8) de la instancia de escenario.
void ActualizarPosePersonajeEscenario(uint8_t* base, uint32_t inst, Accion pasoMaterial, Accion pasoModelo,
                                      Accion poses);
// EU 0x2F7E00 Audio_MenuFadeAndPop: si el volumen (fRam005c54e4) es > 0, fundido y popup del menu.
using Fade = void (*)(uint8_t* base, int a, int b);
using Pop = void (*)(uint8_t* base, int a, int b, uint32_t c, int d);
void SonidoMenuFundido(uint8_t* base, uint32_t param, Fade fade, Pop pop);
// EU 0x262A80 Shadow_RenderModel: los cuatro pasos del pase de sombra de un modelo.
using Shading = void (*)(uint8_t* base, int a, int b);
using Pase = void (*)(uint8_t* base, uint32_t modelo, uint32_t param);
void SombraModelo(uint8_t* base, uint32_t modelo, uint32_t param, Shading shading, Accion aTextura, Pase pase);
// EU 0x339DD0 Viewer_ResolveAttachPointer: modo 1 (ids 8/7/0) o tabla 0x49A380 para los ids < 0x12.
using Anclaje = uint32_t (*)(uint8_t* base, uint32_t id);
uint32_t ResolverAnclajeVisor(uint8_t* base, uint32_t id, uint32_t* flags, Anclaje tabla);
// EU 0x33A8C0 Viewer_ClearEffectHoldAt: suelta el efecto anclado (temporizador infinito y +0x18 a 0).
void LimpiarAnclajeVisor(uint8_t* base, uint32_t id, Anclaje tabla);
// EU 0x2A8850 Char_ConsumePendingHit: lee y pone a 0 el contador de golpes pendientes del minijuego.
int ConsumirGolpePendiente(uint8_t* base, uint32_t param);

}  // namespace puente

}  // namespace dbz3::anim

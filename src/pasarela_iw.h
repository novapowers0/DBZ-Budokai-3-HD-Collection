// dbz3 - Misiones del Dragon de Infinite World: tabla de misiones del ELF de la copia del jugador y pantalla del mapa
// (Dragon Mission) con sus graficos leidos en marcha (pasarela_iw.cpp).
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "pasarela.h"

namespace dbz3::pasarela {

// Un registro de 0x60 B de la tabla del ELF (USA SLUS_218.42: fichero 0x376D50; 201 registros: 1-96 Misiones del
// Dragon, 97-201 Dragon Road). Medido 2026-10-15 contra los titulos de DATA_USA (entrada 131 + numero).
struct MisionTablaIW {
  int numero = 0;        // 1..96; titulo y texto en DATA_USA 131 + numero
  int nivel = 0;         // +0x04: vida objetivo en barras (x1000) de "Fight" y "Endure" (mgr+0x7d4 en la nota de IW)
  int notas = 0;         // +0x06: tipo de notas del rango (1 Defeat, 2 Fight, 3 Endure, 4 Road; ponytail: emparejado
                         //        por los valores con el modo de Char_ComputeStatGrade, sin ver la copia en el codigo)
  int condicion = 0;     // +0x02: -1 entrenamiento, 0 "Defeat" (KO), 1 "Fight" (baja su vida o aguanta), 2 dejar
                         //        ganar al rival ("Let Hercule win!"), 3 aguantar sin atacar ("Endure ...")
  int escenario = 0;     // +0x08: indice de la tabla de escenarios del ELF (0 BUDO ... 16 R&R BASE)
  int tiempo = 0;        // +0x0C indice -> s (1: 60, 3: 180, 5: 120, 4: sin limite = -1)
  int premio = 0;        // +0x0E: 10..400; Zenie = premio x 50/80/100/130/160 segun la dificultad (EU 0x2ABB80)
  int jugador = 0, forma1 = 0;   // +0x14/+0x16 (IDs de IW)
  int rival = 0, forma2 = 0;     // +0x3A/+0x3C
};
// Lee la tabla del ELF de la carpeta extraida o de la ISO (busca la tabla por su numeracion; vale USA y PAL).
std::vector<MisionTablaIW> LeerTablaIW(const std::filesystem::path& iw, std::string& error);
// ELF de arranque (SLUS_*/SLES_*) de la carpeta extraida o de la ISO; vacio si no hay.
std::vector<uint8_t> LeerElf(const std::filesystem::path& iw);
int IWaB3(int id_iw);             // -1 = personaje solo de IW (sin su port instalado con el Mod Kit)
// Fusiones de IW con el registro nativo de B3 (s7 frente 06): forma inicial del luchador (0 = normal).
// Medido en el char372 de B3 (out/analysis/guest_image): 64 Gotenks forma 3 (capsula 150), 70 Gogeta SS4 forma 4
// (capsula 175), 74 Vegito = registro 72 forma 2 (capsula 187). false = vuelve a los ports de antes.
constexpr bool kFusionesNativas = true;
int FormaFusionIW(int id_iw);
int MapaIW(int escenario);        // entrada de data_cmn del mapa de B3 parecido

// ---- Dragon Mission: el mapa del mundo de IW (2026-10-16, frente P1) ----
// Casilla del #DMD de DATA_CMN 1093 (149 registros de 0x50 B; la 1092 es el de Dragon Road: 105 + 1). Comprobado con
// la tabla del ELF: +0x20 clave 0x7E1 + mision (las 96 con +0x18 = 1), +0x04 saga (0 Saiyan, 1 Freezer, 2 Cell, 3 Bu,
// 4 GT, 5 Otra historia), +0x08/+0x0C x, y en el lienzo de 1280x960, +0x10 icono. Las de +0x18 = 0 (claves
// 0x7D0-0x7E1) son los minijuegos (Snake Way, Bubbles...): en el mapa, el "?" de 334/5 t00; las de +0x18 = 2 (claves
// 0x850-0x865), esferas de cristal (t06). ponytail: segun las capturas del original; +0x10 de esas no es una textura.
struct CasillaDM {
  int fila = 0;          // registro del #DMD = su frase en DATA_USA 334/6
  int tipo = 1;          // +0x18: 1 mision, 0 minijuego, 2 esfera
  int clave = 0;         // +0x20
  int mision = 0;        // 1..96; 0 = minijuego o esfera (no se juegan en B3)
  int saga = 0;
  float x = 0, y = 0;    // px del mapa de 1280x960
  int icono = -1;        // textura de DATA_USA 334/5
  int indice = -1;       // +0x1C: minijuego 0..18 (frente P4; clave de las escenas de la historia)
};
// Escenas de la historia (frente R1, 2026-10-16): en IW son 22 videos (USR/FD01A..FD20.SFD; nombres en la tabla del ELF
// USA 0x4811B0, 0x100 B cada uno) con los subtitulos en imagenes (DATA_USA 77 + video: tabla del ELF 0x4827B0; una
// textura por linea, en orden). Que video va antes o despues de cada mision lo dicen dos tablas del ELF (USA 0x472830
// antes y 0x472890 despues, 16 x {tipo, indice, video} de 16 bits, -1 = fin) que lee func 0x2899F0 al acabar
// (sysMissionMovie 0x2AF100). Tipo 1 = mision (indice = numero - 1), 0 = minijuego (indice = +0x1C del #DMD).
struct EscenaIW {
  int tipo = 1, indice = 0, video = 0;
  bool despues = false;
};
struct DragonMissionIW {
  Imagen piezas[4][4];         // mapas de 334/3 (4 piezas de 640x480): Tierra azul, Namek, sepia (GT), espacio
  std::vector<Imagen> hud;     // el resto de 334/3 que se usa: robot y cinta, nombres de saga, Bulma, Bolas, CLEAR...
  Imagen goku;                 // Goku del mapa (334/5 t90) a 4x con Scale2x (pixel art sin borrones)
  std::vector<Imagen> iconos;  // DATA_USA 334/5 por indice (insignias VS, ?, Goku t90, aro t89, New! t16)
  std::vector<Imagen> frases;  // DATA_USA 334/6 por fila del #DMD (texto oscuro)
  std::vector<Imagen> ui;      // DATA_USA 334/1 (paneles, MISSION START, BEST SCORE, TIME/RANK/SCORE, cifras)
  std::vector<Imagen> road;    // DATA_USA 333/0 (globo t18, EARNED! t19, ZENI t07, TOTAL t20)
  // s1 F04.3: titulo de cada minijuego (clave 0x7D0 + i): DATA_USA 335 + i, hijo 1 (una textura de 46 px de alto, letra
  // azul con borde blanco). Cotejado: 346 = "Hyperbolic Time Chamber (Trunks)" (NB013, captura iw_nb013_d), 340 =
  // "1, 2, 3, 4!" (NB007, nb007fin_200). DATA_ENG (IW europeo) tiene las mismas entradas.
  std::vector<Imagen> titulos_mini;
  std::vector<CasillaDM> casillas;
  std::vector<EscenaIW> escenas;                 // de las tablas del ELF
  std::vector<std::vector<Imagen>> subtitulos;   // por video (DATA_USA 77 + video), una imagen por linea
  // Fighter's Road (frente R1, 2026-10-16): la misma pantalla del mapa (func USA 0x300AB0 carga DATA_USA 334 en los
  // dos modos y, con mgr+0x4AC = 1, el #DMD de DATA_CMN 1092 y DATA_USA 333 en lugar de 1093): 105 casillas en 4
  // tramos (+0x04), clave 0x867 + combate (misiones 97-201 de la tabla del ELF); 333/2 = frase de cada casilla.
  std::vector<CasillaDM> casillas_road;
  std::vector<Imagen> frases_road;
  std::vector<std::string> videos;  // "/USR/FD01A.sfd"... (tabla del ELF USA 0x4811B0, 0x100 B cada uno)
  // Carpeta extraida o ISO de IW. Los SFD solo se reproducen desde fichero: con una ISO, RutaVideoIW busca una carpeta
  // extraida al lado (mismo nombre que la ISO, USR hermana o DBZ3_IW_VIDEO_DIR) y, si no la hay, extrae el SFD de la
  // ISO a user_data/iw_video_cache (una vez); solo si eso falla quedan los subtitulos.
  std::filesystem::path raiz;
};
bool LeerDragonMission(const std::filesystem::path& iw, DragonMissionIW& out, std::string& error);
int MapaDeSaga(int saga);   // mapa (indice de piezas) de cada saga
int PistaMinijuegoIW();     // musica (ADX_CMN) del ultimo minijuego empezado desde el mapa; -1 sin leer
int VideoIW(const DragonMissionIW& dm, int tipo, int indice, bool despues);   // video de la historia; -1 si no hay
// USR/FDxx.SFD resuelto (carpeta de IW, anidada o extraida junto a la ISO); vacio si no esta como fichero.
std::filesystem::path RutaVideoIW(const DragonMissionIW& dm, int video);
// Minijuegos de IW (frente P4): si se registra, elegir un "?" del mapa lo llama con la clave del #DMD (0x7D0-0x7E1) y su
// fila; devuelve false si ese minijuego aun no existe (entonces el mapa avisa de que no esta disponible).
void RegistrarMinijuegoIW(bool (*empezar)(int clave, int fila));

// Pantalla del mapa (hilo de la interfaz; el estado del modo lo lleva pasarela_misiones.cpp).
struct ResultadoDM {
  int mision = 0, segundos = 0, puntos = 0, mejor = 0, zeni = 0;
  bool nuevo_record = false;
  int rango = -1;        // 0 C .. 4 Z; -1 = el de los puntos (PantallaDM::Rango)
  int ladrillos = -1;    // minijuego de ladrillos (0x7D3): fila BRICKS DESTROYED; -1 = sin esa fila
  int clave_mini = 0;    // s1 F04.3: minijuego (0x7D0-0x7E1) para su titulo arriba; 0 = mision o sin titulo
};
constexpr int kTituloMini0 = 335;   // DATA_USA 335 + (clave - 0x7D0), hijo 1: titulo del minijuego (s1 F04.3)
struct InfoDM {
  const DragonMissionIW* dm = nullptr;
  const std::vector<MisionTablaIW>* tabla = nullptr;
  int abiertas = 1;                    // misiones abiertas: numero <= abiertas
  int hecho = 0;                       // ultima superada
  std::vector<int> mejor;              // BEST SCORE por mision (indice = numero)
  std::vector<std::string> motivo;     // por mision: vacio = jugable en B3; si no, por que no
  const DatosIW* textos = nullptr;     // titulo y texto de cada mision (DATA_USA 131 + n; misiones[n - 1])
  std::string regla;                   // ficha: la condicion en el idioma del juego (texto propio)
  bool road = false;                   // Fighter's Road: casillas_road y frases_road; logo de 333/0 en vez de la saga
};
class PantallaDM {
 public:
  enum Accion { kNada, kElegir, kSalir, kEmpezar, kVolver, kMinijuego, kRoad };
  // Mapa: cursor por las misiones abiertas y los minijuegos de la saga (cruceta), LB/RB cambia de saga, A elige, B sale,
  // SELECT ensena u oculta la guia (Bulma y su globo).
  Accion Mapa(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in);
  // Ficha de la mision elegida sobre el mapa: menu de IW (334/1 t11: MISSION SETTINGS, CONTROLS, DETAILS, SKILL EDIT)
  // y MISSION START (arriba/abajo; el panel de la izquierda ensena la entrada del cursor); dificultad con
  // izquierda/derecha; A en MISSION START empieza, B vuelve.
  Accion Ficha(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in);
  // Resultado sobre el mapa: las filas de 334/1 t17 (TIME, RANK, BRICKS DESTROYED, SCORE, TOTAL SCORE) entran una a una
  // con su sonido (ritmo de Results_StateBeginRoll/Results_UpdateTally) y luego EARNED! ... ZENI. A salta el conteo
  // y, acabado, lo cierra.
  bool Resultado(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in, const ResultadoDM& r);
  int Mision(const InfoDM& in) const;   // numero de la mision del cursor (0 si no hay)
  void Elegir(const InfoDM& in, int mision);   // pone el cursor (y la camara) en esa mision
  // Entrada al mapa: Story_MapIntroDelayTick (EU 0x2F8790). Lo llama DibujarFondo al detectar una entrada nueva
  // (hueco de tiempo) y queda como contrato para el coordinador si algun dia hay un aviso exacto de "abrir mapa".
  void ReiniciarMapa();
  // H03 P0: vuelta de un combate o minijuego al mapa. No repite Story_MapIntroDelayTick; corre
  // Story_MapResultDelayTick (60 cuadros) y Story_MapReloadAfterDelayTick (~100) para dar las casillas por recargadas.
  void VolverDelCombate();
  bool EnIntroMapa() const { return t_intro_ < 1.0f; }   // true mientras dura la espera de entrada
  float IntroMapa() const { return t_intro_; }           // 0..1 (+0,01 por cuadro, como el original)
  int NivelCom() const;                 // dificultad elegida -> nivel del COM de B3 (0..2)
  int Pista(const InfoDM& in) const;    // musica del mapa: entrada de ADX_CMN.AFS (-1 sin casilla)
  int Dificultad() const { return dificultad_; }   // 0 Very Easy .. 4 Very Hard (multiplica el Zenie)
  // Escena de la historia sobre el mapa (subtitulos del video de IW en bandas de cine; A pasa de linea, B la salta):
  // la dibuja Mapa() hasta acabar: el video original (SFD, video_sfd.h) y, si falta, sus subtitulos sobre el mapa (sin
  // tiempos de linea: no estan en el ELF ni en el #AMT; pasan con A).
  void Contar(int video) { video_ = video, linea_ = 0, probado_ = false, en_video_ = false; }
  bool Contando() const { return video_ >= 0; }
  static int Rango(int puntos);         // 0 C, 1 B, 2 A, 3 S, 4 Z
 private:
  ImTextureID Tex(int clave, const Imagen& img, ImVec2* tam = nullptr);
  void DibujarFondo(ImDrawList* dl, const ImGuiIO& io, float t, const InfoDM& in);
  void Imagen2(ImDrawList* dl, int clave, const Imagen& img, ImVec2 p0, ImVec2 p1, ImVec4 src = ImVec4(0, 0, 0, 0),
               ImU32 tint = IM_COL32_WHITE);
  float Cifras(ImDrawList* dl, bool grandes, const std::string& s, ImVec2 dcha, float alto, const InfoDM& in,
               bool rosa = false);
  int sel_ = -1;                        // casilla del cursor
  float mx_ = -1, my_ = 0, cx_ = -1, cy_ = 0;   // marcador de Goku y camara (px del mapa)
  float tcam_ = 1;                        // Story_MapScrollToTargetTick +0x4A4: seguimiento 0..1 (+0,01 por cuadro)
  // Story_MapIntroDelayTick (EU 0x2F8790): acumulador de entrada 0..1 (+0,01 por cuadro de 60 Hz; >1 -> scroll).
  // Story_MapObjectsAppearTick (EU 0x2FCCB0): cuadros desde que acabo la intro; cada 20 revela una casilla, de la
  // ultima de la lista a la primera, con fundido. Ver ReiniciarMapa().
  float t_intro_ = -1;                    // -1 = mapa sin entrar todavia
  float ap_t_ = 0;                        // cuadros desde el fin de la intro
  // Vuelta de combate (H03): sin intro; Story_MapResultDelayTick (60 cuadros) y Story_MapReloadAfterDelayTick (~100).
  bool volviendo_ = false;
  float t_vuelta_ = 0;                    // cuadros desde la vuelta
  bool res_delay_hecho_ = false, recarga_hecha_ = false;
  int dificultad_ = 2;                  // 0 Very Easy .. 4 Very Hard (lista de 334/1 t16)
  float t_ultimo_ = 0;
  bool guia_ = true;                    // SELECT: Bulma y su globo
  bool flechas_[4] = {};                // se puede desplazar hacia arriba, abajo, izquierda, derecha
  std::string aviso_;                   // minijuego elegido sin portar
  int video_ = -1, linea_ = 0;          // escena de la historia en curso (Contar)
  bool probado_ = false, en_video_ = false;   // el video original (video_sfd.h, R7) ya pedido / sonando
  int mini_clave_ = -1, mini_fila_ = 0; // minijuego que empieza al acabar su escena
  float res_t0_ = -1;                   // resultado: cuando empezo el conteo (-1 = sin empezar)
  int res_sonadas_ = 0;                 // filas cuyo sonido ya sono (la ultima + 1 = el del premio)
  int menu_ = 4;                        // ficha: 0 MISSION SETTINGS, 1 CONTROLS, 2 DETAILS, 3 SKILL EDIT, 4 MISSION START
  Accion Historia(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in);
  void SonidoResultado(const InfoDM& in, int evento);   // 0 fila, 1 premio, 2 sin premio
  struct Cache;
  std::shared_ptr<Cache> tex_;
};

}  // namespace dbz3::pasarela

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
  // +0x1E/+0x20/+0x22: capsulas que da la mision al ganarla (IDs del catalogo de IW; 0 y 0xFFFF = ninguna).
  // Las usa StoryModeLockedCapsules (EU 0x2ABEC0) y las entrega Battle_ApplyResultRewards (EU 0x28A020); el port las
  // traduce a las de B3 con el mismo nombre (ver kCapsulaIWAB3 en pasarela_misiones.cpp) [VERIFICAR].
  int caps[3] = {0, 0, 0};
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
  int tipo = 1;          // +0x18: 1 mision, 0 minijuego, 2 esfera, 3/4 desbloqueo de guion (eventos, fuera del cursor)
  int clave = 0;         // +0x20
  int mision = 0;        // 1..96; 0 = minijuego o esfera (no se juegan en B3)
  int saga = 0;
  float x = 0, y = 0;    // px del mapa de 1280x960
  int icono = -1;        // textura de DATA_USA 334/5
  int indice = -1;       // +0x1C: minijuego 0..18 (frente P4; clave de las escenas de la historia)
  // +0x10 crudo: en los eventos tipo 3 es el subtipo del destello (0x68 Fujo, 0x66 Iceto, 0x65 Kid Bu, EU
  // 0x2F8830: sonidos 0x1C/0x1B/0x1A). En las misiones es la insignia del rival (30..87).
  int cara = 0;
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
  // Casillas de evento (tipos 3 y 4 del #DMD) de Dragon Mission y de Fighter's Road. NO entran en `casillas` (no las
  // recorre el cursor): el original las crea aparte (LoadWaitGeneralDragonMissionIcons_PlayerCommands 0x2FD0F0 y
  // Story_MapObjectsAppearTick 0x2FCCB0) y al entrar al mapa lanza su puesta en escena (_StartUnlock___Func 0x2F8830).
  std::vector<CasillaDM> eventos, eventos_road;
  std::vector<EscenaIW> escenas;                 // de las tablas del ELF
  std::vector<std::vector<Imagen>> subtitulos;   // por video (DATA_USA 77 + video), una imagen por linea
  // Fighter's Road (frente R1, 2026-10-16): la misma pantalla del mapa (func USA 0x300AB0 carga DATA_USA 334 en los
  // dos modos y, con mgr+0x4AC = 1, el #DMD de DATA_CMN 1092 y DATA_USA 333 en lugar de 1093): 105 casillas en 4
  // tramos (+0x04), clave 0x867 + combate (misiones 97-201 de la tabla del ELF); 333/2 = frase de cada casilla.
  std::vector<CasillaDM> casillas_road;
  std::vector<Imagen> frases_road;
  // Pantallas que el port no tenia (DS55), con el arte de la copia del jugador: DATA_USA 65 (Warrior's Room / Skill
  // Edit: fondo, marcos, frascos de capsula y el Shenron de los deseos), DATA_USA 370 (opciones: fondo, rotulos y
  // deslizador del brillo) y DATA_USA 62 (frases de los deseos de Shenron, una textura por indice del banco).
  std::vector<Imagen> sala;
  std::vector<Imagen> opciones;
  std::vector<Imagen> deseos;
  std::vector<std::string> videos;  // "/USR/FD01A.sfd"... (tabla del ELF USA 0x4811B0, 0x100 B cada uno)
  // Carpeta extraida o ISO de IW. Los SFD solo se reproducen desde fichero: con una ISO, RutaVideoIW busca una carpeta
  // extraida al lado (mismo nombre que la ISO, USR hermana o DBZ3_IW_VIDEO_DIR) y, si no la hay, extrae el SFD de la
  // ISO a user_data/iw_video_cache (una vez); solo si eso falla quedan los subtitulos.
  std::filesystem::path raiz;
};
bool LeerDragonMission(const std::filesystem::path& iw, DragonMissionIW& out, std::string& error);
int MapaDeSaga(int saga);   // mapa (indice de piezas) de cada saga
int PistaMinijuegoIW();     // musica (ADX_CMN) del ultimo minijuego empezado desde el mapa; -1 sin leer
// EU 0x298380 MinigameAudioTriggerFilter: clave de disparo de audio del minijuego (0xC..0x6C) -> entrada 0..0x13;
// 0 si la clave no tiene disparador. Lo usa el frente de minijuegos al arrancar su audio.
int FiltroSonidoMinijuego(int clave);
int VideoIW(const DragonMissionIW& dm, int tipo, int indice, bool despues);   // video de la historia; -1 si no hay
// EU 0x289C20 LoadWaitDragonMissionMinigame_Battle: video de un minijuego por su clave del #DMD (casilla tipo 0;
// el indice de la tabla de escenas es su +0x1C). Tablas del ELF: EU 0x473030 (antes) y EU 0x473090 (despues). -1 = sin
// escena: el original entra directo al minijuego o vuelve al mapa. 0x7D0 -> FD01A (indice 0) y 0x7D9 -> FD08B (10).
int VideoMinijuegoIW(const DragonMissionIW& dm, int clave, bool despues);
// DS104: los dos videos (previo y final) de cualquiera de las 18 claves, resueltos de las tablas del ELF y, si el
// escaneo de tablas falla (copia rara), de la tabla medida EU/USA 0x473030/0x473090: indice 0 -> FD01A solo previo;
// indice 10 -> FD08B solo final; los otros 16 indices NO tienen entrada en el original. No depende de que la casilla
// del #DMD este en `casillas`. -1 = sin escena (el original no reproduce SFD).
struct VideosMinijuegoIW {
  int previo = -1, final = -1;
};
VideosMinijuegoIW VideosDeMinijuegoIW(const DragonMissionIW& dm, int clave);
// EU 0x001FC660 Audio_PickRandomVoiceVariant: variante (0..16; `femenina` = bloque +0x22) del ELF de la copia del
// jugador. El original acota el indice, cuenta hasta 4 variantes validas (bytes >= 0) y sortea con Memory(); -1 = sin
// variante. OJO: esa variante NO es una voz, indexa la lista de temas de batalla (ver ElegirTemaBatallaIW).
int ElegirVarianteVozIW(int id, bool femenina = false);
// EU 0x001FCE00 AudioFadeMainFunc / EU 0x001FCAE0 s_ADX_FADE: la variante de EU 0x1FC660 indexa la tabla de temas de
// batalla (ELF EU 0x44E3EC -> 0x44E120; USA 0x44D920) y da la pista de ADX_CMN que suena en el combate. Devuelve -1 si
// la variante no tiene tema. ElegirTemaBatallaIW hace las dos cosas y guarda la pista para el combate de mision.
int PistaTemaBatallaIW(int variante);
int ElegirTemaBatallaIW(int id, bool femenina = false);
int TemaBatallaIW();   // pista elegida en MISSION START (-1 = ninguna)
// EU 0x0018B138 s_E1122604_mwPlyPause_handle_is_in: pausa (true) o reanuda (false) el reproductor de SFD. Es
// idempotente (solo actua si cambia el estado +0x82) y aplica la pausa real al reproductor (video_sfd.cpp: congela
// imagen, audio y subtitulos). Devuelve true si cambio el estado.
bool PausaPeliculaIW(bool pausar);
// Indice de escena del #DMD (+0x1C) de una clave 0x7D0..0x7E1: 0x7D0..0x7D3 -> 0..3; 0x7D4 -> 5 (el indice 4 no
// tiene casilla) ... 0x7E1 -> 18. -1 si la clave no es de minijuego.
int IndiceMinijuegoIW(int clave);
// Las 22 marcas tipo 2 del #DMD 1093 llevan en +0x1C el indice de escena 0..21 y el cargador de peliculas del ELF
// (Movie_OpenContext; nombres en EU/USA 0x481960 + indice*0x100) lo pasa directo al SFD FD01A..FD20. Devuelve el
// indice de `dm.videos` (el SFD), no hay tabla intermedia; -1 fuera de rango.
int VideoEscenaIW(const DragonMissionIW& dm, int indice);
// USR/FDxx.SFD resuelto (carpeta de IW, anidada o extraida junto a la ISO); vacio si no esta como fichero.
std::filesystem::path RutaVideoIW(const DragonMissionIW& dm, int video);
// Minijuegos de IW (frente P4): si se registra, elegir un "?" del mapa lo llama con la clave del #DMD (0x7D0-0x7E1) y su
// fila; devuelve false si ese minijuego aun no existe (entonces el mapa avisa de que no esta disponible).
void RegistrarMinijuegoIW(bool (*empezar)(int clave, int fila));
// EU 0x2AE4D0 DragonMission_RandomizeMatchSetup: sorteo de personajes, trajes y escenario para el combate aleatorio
// (lo llama GoBackToNewGame/LoadGame/Continue/TitleFunc 0x002B0B00 con el codigo 3). Devuelve el ID de IW del jugador,
// su traje (0 = el de serie) y el escenario de IW (indice de MapaIW); -1 en `jugador` si no hay ninguno disponible.
struct SetupAleatorioIW {
  int jugador = -1, traje = 0, escenario = 5;
};
SetupAleatorioIW SeleccionAleatoriaIW();

// Pantalla del mapa (hilo de la interfaz; el estado del modo lo lleva pasarela_misiones.cpp).
struct ResultadoDM {
  int mision = 0, segundos = 0, puntos = 0, mejor = 0, zeni = 0;
  // DS108: los minijuegos separan puntuacion, premio y monedero (DS102). `puntos` queda como valor historico
  // (compatibilidad y respaldo); el mapa pinta SCORE/TOTAL SCORE con `score`, EARNED ZENI con `zeni` y TOTAL ZENI
  // con `saldo`. -1 = no informado (se usa `puntos` / `iw_zeni` + `zeni`).
  int score = -1;
  int saldo = -1;
  int objetos = -1;      // fila de objetos (BRICKS DESTROYED en 0x7D3; % en MSNMW); -1 = sin fila
  int objetos_total = -1;   // total de objetos de MSNMW (0x165a): la fila es objetos*100/total; -1 = no informado
  int great = -1, good = -1, normal = -1;   // GREAT/GOOD/NORMAL SCORE (0x7DA/0x7E1); -1 = sin fila
  int offence = -1, defence = -1;           // OFFENCE/DEFENCE SUCCESS RATE (QTE de dos carriles); -1 = sin fila
  int centesimas = -1;   // TIME exacto de IW (+0x74 del bloque de resultados); -1 = solo `segundos`
  bool con_tiempo = true;
  bool nuevo_record = false;
  int rango = -1;        // 0 C .. 4 Z; -1 = el de la puntuacion (PantallaDM::Rango)
  int ladrillos = -1;    // compatibilidad: fila BRICKS DESTROYED (0x7D3); -1 = sin esa fila
  int clave_mini = 0;    // s1 F04.3: minijuego (0x7D0-0x7E1) para su titulo arriba; 0 = mision o sin titulo
};
constexpr int kTituloMini0 = 335;   // DATA_USA 335 + (clave - 0x7D0), hijo 1: titulo del minijuego (s1 F04.3)
struct InfoDM {
  const DragonMissionIW* dm = nullptr;
  const std::vector<MisionTablaIW>* tabla = nullptr;
  int abiertas = 1;                    // misiones abiertas: numero <= abiertas
  int hecho = 0;                       // ultima superada
  std::vector<int> mejor;              // mejor RANGO + 1 por mision (indice = numero); 0 = sin superar
  std::vector<int> puntaje;            // mejor SCORE por mision (iw_mejor_N); 0 = sin marca (SCORE "-")
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
  // con su sonido (ritmo de Results_StateBeginRoll/Results_UpdateTally) y luego EARNED! ... ZENI (y TOTAL ... ZENI en
  // los minijuegos). SCORE/TOTAL SCORE salen de `score`; EARNED de `zeni`; TOTAL del monedero `saldo` (DS108). A salta
  // el conteo y, acabado, lo cierra.
  bool Resultado(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in, const ResultadoDM& r);
  // Guardado del bloque del registro (Save_UpdateRecordIfChanged, EU 0x2B81D0) forzado. Lo usa el resultado de un
  // minijuego (pasarela_misiones.cpp): cambia el monedero pero no `hecho`, y el bloque debe quedar al dia.
  void GuardarRegistro(const InfoDM& in) { ActualizarRegistro(in, true); }
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
  // tiempos de linea: no estan en el ELF ni en el #AMT; pasan con A). El video final de un minijuego (tabla de despues,
  // EU 0x473090) lo pide pasarela_misiones.cpp al cerrar el resultado con VideoIW(dm, 0, c.indice, true); solo el
  // 0x7D9 tiene escena (FD08B, indice 10). DS104: Mapa() pide ese final al volver del minijuego (EU 0x20D800, sin
  // exigir victoria) si pasarela_misiones no lo ha pedido ya; el previo (tabla de antes, EU 0x473030) lo pide antes
  // de arrancar el minijuego (antes del READY). Las 18 claves se resuelven por VideosDeMinijuegoIW; las que no tienen
  // entrada no reproducen SFD.
  // Las marcas tipo 2 (`esferas`) tambien reproducen su escena: su +0x1C es el indice directo del SFD (0..21) en el
  // cargador de peliculas; al acabar el video se marca la esfera (`esfera_video_`).
  void Contar(int video) { video_ = video, linea_ = 0, probado_ = false, en_video_ = false, pausado_ = false; }
  bool Contando() const { return video_ >= 0; }
  static int Rango(int puntos);         // 0 C, 1 B, 2 A, 3 S, 4 Z
  // Cola de ordenes del mapa. En IW el mapa no cambia de estado llamando a la funcion siguiente: empuja un comando en
  // la cola 0x5D9220/0x5D9260 (Story_PushMapState, EU 0x2F9FE0) y el despachador DragonMissionMiscPlayerFuncs
  // (EU 0x2F98D0) ejecuta el primero de la cola y lo saca (Story_PopMapStateAndReload, EU 0x2FA030). Comandos:
  // 1 desbloqueos pendientes, 2 cambio de saga L1/R1, 3 fin del cambio, 4 vuelta de combate, 5 entrar en la saga,
  // 7 titulo, 8 Bulma y Bolas, 9 menu de mision, 10 iconos de la saga, 0xB/0xC menu de START, 0xD Bolas nuevas,
  // 0xE camara al objetivo, 0xF esperar y sacar, 0x10 fin. Los mandos se recargan al sacar (LoadWaitPlayerCommands).
  struct OrdenMapa {
    int cmd = 0, arg = 0;
  };
  int OrdenesPendientes() const { return int(ordenes_.size()); }
  int EstadoMapa() const { return estado_; }   // piRam0049cdac[0x12A]: 7 = GeneralMapScripts (cursor libre)
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
  // B3: mandos bloqueados durante el primer ciclo del scroll (Story_MapScrollToTargetTick recarga los mandos
  // cuando su acumulador pasa de 1,0; ~100 cuadros). Antes solo se bloqueaba la intro.
  bool bloqueo_scroll_ = false;
  float t_vuelta_ = 0;                    // cuadros desde la vuelta
  bool res_delay_hecho_ = false, recarga_hecha_ = false;
  int dificultad_ = 2;                  // 0 Very Easy .. 4 Very Hard (lista de 334/1 t16)
  float t_ultimo_ = 0;
  bool guia_ = true;                    // SELECT: Bulma y su globo
  bool flechas_[4] = {};                // se puede desplazar hacia arriba, abajo, izquierda, derecha
  std::string aviso_;                   // minijuego elegido sin portar
  int video_ = -1, linea_ = 0;          // escena de la historia en curso (Contar)
  bool probado_ = false, en_video_ = false;   // el video original (video_sfd.h, R7) ya pedido / sonando
  bool pausado_ = false;                      // EU 0x18B138 mwPlyPause: escena pausada (SELECT)
  bool env_mision_aplicada_ = false;          // B3f: DBZ3_IW_MISION se consume una vez por mapa
  int mini_clave_ = -1, mini_fila_ = 0; // minijuego que empieza al acabar su escena
  int esfera_video_ = -1;               // DS104: marca tipo 2 cuya escena SFD(+0x1C) suena; se marca al acabar
  // DS104: video final del minijuego que se acaba de jugar (tabla de despues, EU 0x473090). Se pide al volver al
  // mapa, como la salida normal del original (EU 0x20D800), sin exigir haberlo superado; si pasarela_misiones ya lo
  // pidio, no se repite.
  int mini_final_ = -1;
  float mini_final_t0_ = -1;
  float res_t0_ = -1;                   // resultado: cuando empezo el conteo (-1 = sin empezar)
  int res_sonadas_ = 0;                 // filas cuyo sonido ya sono (la ultima + 1 = el del premio)
  bool res_saltar_ = false;             // DS153: A en el resultado de un minijuego = todo a la vista (segunda A cierra)
  int menu_ = 4;                        // ficha: 0 MISSION SETTINGS, 1 CONTROLS, 2 DETAILS, 3 SKILL EDIT, 4 MISSION START
  // Casillas de evento del mapa (tipos 3/4): puesta en escena de _StartUnlock___Func (EU 0x2F8830). `ev_` = indice en
  // dm->eventos, -1 = ninguna; la fase 0 panea la camara, la 1 es la espera de 420 cuadros de _StartUnlock___Func2
  // (EU 0x2F86B0) que solo hace el tipo 3.
  int ev_ = -1, ev_fase_ = 0;
  float ev_t_ = 0, ev_espera_ = 0;
  int ev_saga_ = -1;                              // saga cuyos eventos ya se han puesto en cola (cambia con LB/RB)
  bool EmpezarEventos(const InfoDM& in);          // arranca el siguiente evento pendiente de la saga; false si no hay
  void TerminarEvento(const InfoDM& in);          // apunta la clave como aplicada y pasa al siguiente o al scroll
  void SonidoEvento(const CasillaDM& e);          // sonidos de _StartUnlock___Func (banco del IOP: solo se registran)
  // --- Cola de ordenes del mapa (EU 0x2F9FE0 / 0x2FA030 / 0x2FF3E0 / 0x2F98D0 / 0x300F80) ---
  void EmpujarOrden(int cmd, int arg);                    // EU 0x2F9FE0 Story_PushMapState
  void SacarOrdenYRecargar();                             // EU 0x2FA030 Story_PopMapStateAndReload
  void EmpujarComandosDeVuelta(int saga, bool de_combate, int arg);   // EU 0x300F80 ScreenTransition_PushCommands
  void DespacharOrden(const InfoDM& in);                  // EU 0x2F98D0 DragonMissionMiscPlayerFuncs
  void EsperarYPop(float cuadros);                        // EU 0x2FF3E0 Story_MapWaitThenPop (comando 0xF)
  void CambiarSaga(int dir, const InfoDM& in);            // EU 0x2FDF80 DragonMissionScenarioChange_L1/R1
  void FinCambioSaga(const InfoDM& in);                   // EU 0x2FDC30 DragonMissionScenarioChangeLoadWaitEnd
  void PasoCambioSaga(float dt, const InfoDM& in);        // avance del cambio de saga (salida + fundido)
  void SalirDelCombate();                                 // EU 0x2F9200 DragonMissionFightScenarioExitLoadWaitEnd
  void PrepararEntradaMapa(const InfoDM& in);             // comandos al entrar al mapa (5, 0xE y 10)
  // EU 0x2B81D0 Save_UpdateRecordIfChanged: el registro de la historia se reescribe entero (o no se toca) si algun
  // campo cambio. Se llama al abrir el mapa y cuando cambia la mision superada. `forzar` = guardado explicito del
  // menu START (EU 0x2D75F0 MemCardDlg_Start): escribe el bloque aunque no haya cambiado.
  void ActualizarRegistro(const InfoDM& in, bool forzar = false);
  // START del mapa (EU 0x365090 Story_MapHandleStartButton), menu de pausa (EU 0x365110 Story_MapDrawMenu y
  // EU 0x3655C0 Story_MapSetupScreen) y menu de guardado (EU 0x365BC0 MemCard_StorySaveMenuTick: GUARDAR llama al
  // dialogo de tarjeta del original, EU 0x2D75F0, que el port resuelve escribiendo el registro). DS65: una sola
  // opcion (GUARDAR) hasta el primer guardado y dos despues (GUARDAR / VOLVER AL MAPA), como cRam005c1d88; B vuelve
  // al menu principal (transicion 5) y START cierra el menu. El brillo (EU 0x2CEF20) pasa a Y (pantalla de Opciones
  // de IW no portada).
  Accion MenuMapa(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in);
  // Pantallas nuevas de DS55 (art. del AFS del jugador; funciones del decompilado citadas en cada una):
  // - Skill Edit / capsulas: EU 0x2E3F40 SkillEditFunc1 (+ GeneralCapsuleProperties 0x2ACA10 y
  //   StoryModeLockedCapsules 0x2ABEC0). Se abre con A en la entrada SKILL EDIT de la ficha.
  // - Deseos de Shenron: EU 0x2B1220 CapsulePick_Start_LoadCmnFiles, 0x2B1210 LoadWaitShenronWishEndGameFunc y
  //   0x381A30 LoadWaitShenronWishMainMenuFunc. Se abre al entrar al mapa con las 7 Bolas (bRam005c1d78 = 0x7F).
  // - Brillo: EU 0x143920 GameBrightnessFunc y 0x2CEF20 Options_BrightnessInput_ (0..128, defecto 26).
  Accion PantallaCapsulas(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in);
  Accion PantallaShenron(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in);
  Accion PantallaBrillo(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in);
  struct CapsulaIW {
    int id = 0, mision = 0, ranura = 0;
    bool tiene = false;
  };
  std::vector<CapsulaIW> CapsulasDeMisiones(const InfoDM& in) const;   // premios de mision (sin repetir ID)
  void AbrirCapsulas(const InfoDM& in);
  void AbrirShenron(const InfoDM& in);
  bool edit_abierto_ = false;                    // Skill Edit (EU 0x2E3F40)
  int edit_sel_ = 0, edit_tab_ = 0;              // 0 capsulas, 1 tabla de bloqueadas (EU 0x2ABEC0)
  std::vector<CapsulaIW> edit_caps_;
  int shenron_fase_ = -1;                        // -1 cerrado, 0 presentacion, 1 elegir, 2 conseguida
  int shenron_sel_ = 0;
  float shenron_t_ = 0;
  std::vector<CapsulaIW> shenron_3_;             // las tres capsulas sorteadas (EU 0x280990)
  bool brillo_abierto_ = false;                  // EU 0x143920 / 0x2CEF20
  int brillo_ = 26;                              // 0..128, defecto 0x1A
  bool deseo_postpuesto_ = false;                // B en Shenron: no insistir hasta la proxima entrada al mapa
  float t_deseo_ = -1;                           // ultima lectura de iw_bolas/iw_deseo (0,5 s)
  int deseo_bolas_ = -1, deseo_hecho_ = 0;
  std::vector<OrdenMapa> ordenes_;                // la cola (0x5D9220/0x5D9260, cuenta 0x5D92A0)
  float t_orden_ = 0;                             // +0x34 del estado en curso (esperas)
  int estado_ = 7;                                // +0x12A: 7 = GeneralMapScripts (cursor libre)
  int saga_actual_ = 0;                           // +0xE5: saga que se esta viendo
  float t_registro_ = -1;                         // ultimo `t` en que se reviso el registro (0,5 s de cache)
  int hecho_registro_ = -1;                       // ultima mision superada que se paso al registro
  // Menu START del mapa: 0 cerrado, 1 opciones (EU 0x365BC0), 2 guardando (EU 0x2D75F0), 3 aviso de guardado,
  // 4 volviendo al mapa (MemCard_StorySaveTick, opcion 1: Transition_SetupWithParams(0xF,...), 30 cuadros).
  int menu_mapa_ = 0, sel_mapa_ = 0;              // sel_mapa_: 0 GUARDAR, 1 VOLVER AL MAPA (si ya se guardo)
  float t_menu_t0_ = 0;                           // inicio del guardado o del aviso
  // cRam005c1d88 al empezar el guardado: decide a donde se vuelve (1.a vez -> mapa; siguientes -> menu), EU 0x365900.
  bool guardado_antes_ = false;
  // Cambio de saga (EU 0x2FDF80): fase 1 salida de Goku (180 cuadros L1, 240 R1, 20 sin lado), fase 2 fundido a
  // negro a 0,02 por cuadro (Transition_SetSpeed), fase 3 vuelta del fundido y recarga de iconos.
  int cambio_fase_ = 0, cambio_dir_ = 0, cambio_saga_ = -1, cambio_mision_ = -1;
  float cambio_t_ = 0;
  Accion Historia(ImDrawList* dl, const ImGuiIO& io, float t, uint16_t pulsado, const InfoDM& in);
  void SonidoResultado(const InfoDM& in, int evento);   // 0 fila, 1 premio, 2 sin premio
  struct Cache;
  std::shared_ptr<Cache> tex_;
};

}  // namespace dbz3::pasarela

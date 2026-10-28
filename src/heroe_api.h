// dbz3 - Modo Heroe: lo que las pantallas al estilo Ultimate Tenkaichi (heroe_ui.cpp, studies 8 H3/H4) necesitan de
// heroe.cpp. heroe.cpp guarda el heroe de la ficha activa en sus globales (cuerpo, traje, estilo, progreso); el aspecto
// UT (HSV por parte, logo, scouter, pelo y sus 2 colores) vive directamente en la ficha de cac.toml (cac::Heroe).
// Todo se llama con el candado de rpg.cpp (g_mu) puesto.
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace dbz3::heroe {

// --- aspecto (Create) ---
int Cuerpo();                 // ID del cuerpo (luchador cuyo modelo lleva el heroe)
std::string NombreCuerpo();   // G06a: nombre de la constitución/cara del catálogo UT (100..105)
int Traje();                  // traje del cuerpo (0..NumTrajes-1)
int NumTrajes();              // trajes del cuerpo (tabla del select si esta en memoria)
void PonerTraje(int t);
const std::string& Nombre();  // nombre del heroe

// --- sagas para el mapa (studies 8 fase 2, H5) ---
int Saga();                                           // la siguiente por jugar (las de antes, completadas)
int SagasTotal();
constexpr int kSagasJugables = 8;   // DS20: sagas 5-8 con codigos nativos del bloque 0 + [codigos_saga] (campana.py)
const char* NombreSaga(int saga);
int MaestroQueEntrena(int saga);                          // ID del maestro que se entrena (-1 ninguno)
std::vector<std::pair<int, int>> CombatesDeSaga(int saga);   // (rival, escenario) de cada combate

// --- ficha -> modelo (studies 8 fase 2, H2) ---
std::string ArgsFicha(int id, int traje);   // texto de vista_heroe.py --ficha
bool FichaNativa();                         // sin colores/pelo/logo/scouter: el modelo del juego tal cual
int FichaNumero();                          // 1..N (mods/heroe_fichaN)
int TrajeCombate(int* estado);
std::string ArgsPropio();                   // studies 9: ArgsFicha + nombre: lo que monta mods/heroe_propio (el heroe con ID propio)
int IdPropio();                             // su ID real en el roster de esta sesion (-1: no esta montado)
int DonanteCombate();                       // donante (moveset) con el que se monta el heroe propio
int Luchador(int estilo);                   // el que pelea: IdPropio() o, sin el, el estilo              // 0 nativa, 1 en combate, 2 al reiniciar, 3 sin hacer

// --- movesets (studies 8 fase 2): -1 = el del estilo; el resto, donantes validos (char372 con moveset) ---
int EstiloId();
int MovesetId();
std::vector<int> Movesets();
void PonerMoveset(int id);
int CapsulasMoveset(int id);   // capsulas por defecto del donante (char96 +80)
// DS144: estilos de lucha desbloqueados (kEstilos que ya se pueden elegir) y su seleccion desde la pantalla
// Moveset > pestana Estilo. PonerEstilo aplica el estilo al combate siguiente y la ficha lo guarda (Guardar).
std::vector<int> Estilos();
void PonerEstilo(int id);
bool EsMaestro(int id);
std::string DetalleEstilo(int id);   // bonus y tecnicas del estilo (2 lineas), para la ficha de la pestana Estilo

// --- maestros (Select Master, H4) ---
constexpr int kMaestros = 6;
struct InfoMaestro {
  int id = -1;                 // ID del luchador
  int dano = 100, recibido = 100, vida = 0, ki = 0;   // modificadores (%, vida extra, ki < 0: se recarga solo)
  int capsula = 0;             // capsula de tecnica que da entrenado
  const char* capsula_n = "";  // su nombre
  const char* tecnicas = "";   // sus tecnicas
  int entreno = 0;             // % de entrenamiento (0..100)
  bool entrenado = false;      // 100 % o saga completada
  bool desbloqueado = true;    // s9: se elige solo al llegar a su saga (o entrenado)
  int saga = -1;               // s9: la saga en la que se desbloquea (-1: desde el principio)
};
InfoMaestro Maestro(int i);
int MaestroActual();           // indice del maestro del estilo elegido, -1 si el estilo no es maestro
int EntrenoMinimo();           // 60: % con el actual para poder cambiar
bool PuedeCambiarA(int i);     // regla del 60 %
bool ElegirMaestro(int i);     // el estilo pasa a ser el maestro i (false: bloqueado por la regla)

// --- inventario de tecnicas del heroe (studies 5 C2) ---
struct Tecnica {
  int cap;        // ID de capsula (#SKC)
  int donante;    // luchador duenyo (bit de la mascara de duenyos)
  int ki;         // decimas de barra de ki (10 = 1 barra)
  int rareza;     // 0..3
};
const std::vector<Tecnica>& CatalogoTecnicas();   // ataques (clase 0x21) del #SKC residente; vacio sin juego
std::vector<int> InventarioTecnicas();            // capsulas que posee: las de los donantes que tiene abiertos
int TipoTecnica(int cap);                         // C4: 0 definitiva, 1 E+derecha, 2 E+izquierda, -1 desconocido/sin entrada
std::filesystem::path TiposRutaPublica();
bool TiposListos();                               // C4: tecnicas_tipo.txt existe (lo hace tecnicas_tipo.py)
int Equipada(int k);                              // k = 0..2: definitiva, E+derecha, E+izquierda (0 = libre)
void Equipar(int k, int cap);                     // cap 0 vacia el hueco; solo cabe una carta de su tipo
int DonanteEfectivo();                            // luchador cuyo moveset usa el heroe ahora
bool TecnicaActiva(int cap);                      // true si su donante es el efectivo (si no, no tiene golpe)
std::string ArgsCartas();                         // "ID:entrada,..." de las caras del HUD (tecnicas_cartas.py --caras)

// --- Esferas del Dragon y deseos (C3) ---
int Esferas();                // mascara de 7 bits de las Esferas recogidas
int Deseos();                 // deseos de Shenron pendientes
bool RazaCerrada();           // el ultimo intento de cambiar raza/cuerpo se rechazo
void GanarEsfera(int i);      // 0..6; a la septima se apunta un deseo

inline bool CuerpoPropio(int id) { return (id >= 100 && id <= 105) || (id >= 110 && id <= 115); }   // UT (100..105) o XV2 (110..115)
inline bool CuerpoXv2(int id) { return id >= 110 && id <= 115; }
bool Xv2Instalado();   // data.cpk de Xenoverse 2 localizado (DBZ3_XENO2_CPK o Steam); sin el, heroe base de B3
bool Xv2PiezasActivas();   // XV2 instalado Y la opcion del launcher encendida (por defecto: piezas nativas de B3)
// C4: piezas del creador de Xenoverse 2 (cuerpo 110..115 = HUM, HUF, NMC, FRI, MAM, MAF). tipo: 0 Hair, 1 FaceBase, 2 Bust,
// 3 Pants, 4 Boots, 5 Rist. Las 3 primeras que existen de verdad (res/datos/xv2_cac.toml, como cac/piezas_xv2.py minimo()).
std::vector<int> PiezasXv2(int cuerpo, int tipo);
const char* NombreTipoXv2(int tipo);
struct Barras {   // C4: las barras de la pantalla principal (todo con el estilo y el nivel actuales)
  int vida = 0, vida_max = 6000;   // vida de combate
  int dano = 100, dano_max = 200;  // % de dano hecho
  int resist = 100, resist_max = 200;  // 200 - % de dano recibido
  int nivel = 1, xp = 0, xp_max = 1;
};
Barras Barras_();
std::string NombreMostrado();   // el nombre de la ficha (el "Heroe" por defecto se muestra en el idioma del juego)
int CasillaDu();            // C4: casilla de Dragon Universe de la ficha activa (35..37)
bool HistoriaMontada();     // C4: las 3 historias (mods/historia_heroe_35..37) estan instaladas
bool DuDisponible();          // el Heroe puede entrar en Dragon Universe (cuerpo propio montado + historia_heroe instalada)

// --- Desbloqueos con los maestros (C3, res/heroe/desbloqueos.toml): aura/ki de color y tecnicas con variante ---
struct InfoDesbloqueo {
  std::string id, tipo, nombre, color;   // tipo: aura | ki | tecnica; color "#RRGGBB"
  int capsula = 0;                        // tecnica con variante de color: la capsula (#SKC)
  bool conseguido = false;
};
std::vector<InfoDesbloqueo> Desbloqueos();
// Super Saiyan del heroe (25/10): niveles desbloqueados (0..3, raza Saiyan: tipo "ssj" de desbloqueos.toml, en orden) y el
// nivel recien desbloqueado aun sin anunciar (0 = nada; al leerlo queda anunciado).
int SsjNivel();
int IdHeroeCargado();   // el ID del heroe propio en el roster de ESTA sesion (sin mirar la ficha activa; -1: no hay)
int SsjAvisoPendiente();

}  // namespace dbz3::heroe

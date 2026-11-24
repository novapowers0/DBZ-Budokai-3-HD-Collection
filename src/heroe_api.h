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
void PonerNombre(const std::string& n);   // sanea y limita a 16 bytes

// --- sagas para el mapa (studies 8 fase 2, H5) ---
int Saga();                                           // la siguiente por jugar (las de antes, completadas)
int SagasTotal();
constexpr int kSagasJugables = 8;   // DS20: sagas 5-8 con codigos nativos del bloque 0 + [codigos_saga] (campana.py)
const char* NombreSaga(int saga);
int MaestroQueEntrena(int saga);                          // ID del maestro que se entrena (-1 ninguno)
std::vector<std::pair<int, int>> CombatesDeSaga(int saga);   // (rival, escenario) de cada combate

// --- ficha -> modelo (studies 8 fase 2, H2) ---
bool Materiales17();
bool PeloCac2();
std::string ArgsFicha(int id, int traje, bool local = true);   // texto de vista_heroe.py --ficha
bool FichaNativa();                         // sin colores/pelo/logo/scouter: el modelo del juego tal cual
int FichaNumero();                          // 1..N (mods/heroe_fichaN)
int TrajeCombate(int* estado);
bool FotoCac2();
std::string ArgsPropio(bool local = true);                   // studies 9: ArgsFicha + nombre: lo que monta mods/heroe_propio (el heroe con ID propio)
int IdPropio();                             // su ID real en el roster de esta sesion (-1: no esta montado)
std::string ArgsExportar();                 // 1.5.6: ArgsPropio + donante y tecnicas equipadas: lo que "Exportar como personaje" manda a vista_heroe.py --exportar
// cac1 (CAC-08/09/10/20): recetas = ArgsFicha sin arte. Ver heroe.cpp y mod center hd/cac/recetas.py.
std::string ArgsReceta();                                     // la apariencia de ahora (ArgsFicha + tiras ocultas)
bool AplicarFicha(const std::string& ficha);                  // inverso: la pone en la ficha activa (false: texto roto o cambio de cuerpo cerrado)
bool RecetaGuardar(int i, const std::string& nombre, bool persistir = true);         // hueco 0..3 de cac.toml v3 (guarda cac.toml)
bool RecetaAplicar(int i);
bool RecetaLlena(int i);
std::string RecetaNombre(int i);
bool RecetaFormaCac2();
bool RecetaCompatibleForma(int receta);
bool PonerFormaReceta(int forma, int receta, const std::string& estallido);   // CAC-19: forma 1..7 = receta (-1 nativa); ArgsPropio lleva formaN=hex;estallidoN=
int FormaReceta(int forma);
std::string ArgsGuardarTraje(const std::string& nombre);      // GenerarCon(..., recetas.py): mods/cac_traje_<nombre>/ ; json: estado, mod, formas
std::string ArgsCodigoReceta();                               // json: estado, codigo
std::string ArgsPegarCodigo(const std::string& codigo);       // json: estado + ficha | mensaje (luego AplicarFicha(ficha))
int DonanteCombate();                       // donante (moveset) con el que se monta el heroe propio
int Luchador(int estilo);                   // el que pelea: IdPropio() o, sin el, el estilo              // 0 nativa, 1 en combate, 2 al reiniciar, 3 sin hacer

// --- movesets (studies 8 fase 2): -1 = el del estilo; el resto, donantes validos (char372 con moveset) ---
int EstiloId();
int MovesetId();
int MovesetEleccion();   // clave estable para la lista, nunca para indexar tablas guest
int MovesetRetrato(int id);
std::string MovesetNombre(int id);
void PedirPaginaMoveset();
bool MovesetEsperandoPagina();
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
  uint64_t duenyos = 0;   // PM-32 TC-03: mascara completa de duenyos (la capsula vale con cualquiera de ellos)
  bool Dueno(int id) const { return id >= 0 && id < 64 && ((duenyos >> id) & 1); }
};
const std::vector<Tecnica>& CatalogoTecnicas();   // ataques (clase 0x21) del #SKC residente; vacio sin juego
std::vector<int> InventarioTecnicas();            // capsulas que posee: las de los donantes que tiene abiertos
int TipoTecnica(int cap, int donante = -1);   // TC-02: donante opcional (tipo segun su #CCM).                         // C4: 0 definitiva, 1 E+derecha, 2 E+izquierda, -1 desconocido/sin entrada
std::filesystem::path TiposRutaPublica();
bool TiposListos();                               // C4: tecnicas_tipo.txt existe (lo hace tecnicas_tipo.py)
int Equipada(int k);                              // k = 0..2: definitiva, E+derecha, E+izquierda (0 = libre)
void Equipar(int k, int cap);                     // cap 0 vacia el hueco; solo cabe una carta de su tipo
int DonanteEfectivo();                            // luchador cuyo moveset usa el heroe ahora
bool TecnicaFormaBloqueada(int cap);             // solo cuando se puede verificar el montaje actual
bool TecnicaActiva(int cap);                      // true si su donante es el efectivo (si no, no tiene golpe)
// HEROE-TEC (dbz3_heroe_tecnicas_v2, heroe_tecnicas_v2.inl): pantalla de tecnicas v2; tipo 3 = esquiva (g_equipo[3]).
struct TecnicaV2 {
  int escuela = -1;               // donante de escuela; -1 para maestros, estilo y premios
  int cap = 0, tipo = -1, ki = 0, donante = -1;
  bool abierta = false, nueva = false;
  int maestro = -1, entreno = 0;    // condicion de apertura (ID del luchador-maestro y %), -1 = premio / estilo propio
  int victorias = 0, nivel = 1;     // victorias con ella equipada; nivel 1-3 (5 / 15)
  int formas = 0xFF, requiere = 0;  // #SKC +14 (formas en las que se puede usar) y +16 (capsula que pide)
};
bool TecnicasV2On();
std::vector<TecnicaV2> TecnicasV2();
void TecnicaVista(int cap);                       // quita el NUEVO
std::string ArgsCartas();                         // "ID:entrada,..." de las caras del HUD (tecnicas_cartas.py --caras)

int BonoMaestria(const int* caps, int n);         // puntos porcentuales; capsulas finales de Reglas

// --- Esferas del Dragon y deseos (C3) ---
int Esferas();                // mascara visible; 0 mientras las siete estan reservadas por el menu nativo
int Deseos();                 // deseos de Shenron pendientes
bool RazaCerrada();           // el ultimo intento de cambiar raza/cuerpo se rechazo
void GanarEsfera(int i);      // 0..6: una por saga completada
bool EsferasDevueltas();      // "B Pensarlo": libera la reserva; false si ya estaba cerrada
bool ShenronAparece();        // reserva las siete sin quitarlas de la ficha; false sin siete o si ya esta abierto
bool ConsumirEsferas();       // confirma una eleccion nativa una sola vez; false sin reserva
bool ConcederDeseo();         // confirma la fila "cambiar raza y cuerpo" y apunta un deseo; false si ya se gasto
bool EsferaMapaRuta(int evento);   // H02: solo las dos marcas de reunion (5/6), una vez por ficha
bool EsferaRutaPendiente(int evento);   // productor del mapa: oculta las identidades ya cobradas
bool Rutas();
int PlanetaEtapaPendiente();       // R23: planeta (0 Tierra, 1 Namek) de la etapa que sigue; -1 sin rutas o sin etapa
int PlanetaTramo(int t);           // R23 HER-13: planeta del mapa del tramo t (0 Tierra, 1 Namek); -1 sin rutas
bool DeseoHistoriaPendiente();     // siete reunidas, etapa 5 pendiente y sin decision
bool ConfirmarDeseoHistoria(bool revivir);   // requiere reserva; decision y gasto se exportan juntos
void GuardarRutaActual();         // rpg: solo despues de guardar con exito la ficha

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
int NivelesTransformacion();   // niveles de transformacion de la raza del heroe (0 = no transforma)
int RazaHeroe();
int IdHeroeCargado();   // el ID del heroe propio en el roster de ESTA sesion (sin mirar la ficha activa; -1: no hay)
int SsjAvisoPendiente();
// R23 CAC: mascara (bit f) de las formas del heroe propio que puede usar en combate; caps[f] = capsula de su forma f (char372).
uint32_t FormasCombate(const uint16_t* caps, int n);

// Evolucion por raza (maqueta 12, dbz3_heroe_evolucion apagado de serie; regla en heroe_formas.h, datos en desbloqueos.toml).
struct InfoForma {
  std::string id, previa, rama, nombre, condicion, camino;   // rama "" tronco, "A"/"B"; camino = nombre de la rama
  int cara = -1, capsula = 0;                                // luchador de la cabeza nativa, capsula de transformacion
  int estado = 2;                                            // 0 abierta, 1 alcanzable (condicion pendiente), 2 camino cerrado
  int combate = -1;                                          // R23 CAC: 1 el heroe de combate (cuerpo + estilo) la tiene, 0 no, -1 sin heroe montado
};
bool EvolucionActiva();                          // el ajuste dbz3_heroe_evolucion esta encendido
int RazaEvolucion();                             // 0 Saiyan, 1 Humano, 2 Namekiano, 3 Raza de Freezer, 4 Majin, 5 Androide
std::vector<InfoForma> FormasDeRaza(int raza);
std::vector<std::string> MarcasRuta();             // marcas de ruta del heroe (camino_nave...), para la Habitacion   // en el orden de desbloqueos.toml

// Recompensas y Coleccion (HEROE-REC, dbz3_heroe_recompensas apagado de serie; datos res/heroe/recompensas.toml, heroe_recompensas.inl).
struct InfoPremio {
  std::string id, tipo, nombre, condicion;   // tipo: tecnica | titulo | escenario | luchador
  int capsula = 0, cara = -1;                // tecnica: capsula #SKC; cara: luchador de la cabeza nativa
  bool conseguido = false, nuevo = false;    // nuevo = conseguido y aun sin mirar en la Coleccion
  bool repetido = false;                     // solo en PremiosAviso: ya lo tenias (dio Zenie)
  int escenario = -1;                        // entrada AFS estable; no indice del selector
};
bool RecompensasActivas();
uint64_t LuchadoresPremiados();                   // R23 HER-06: bits de los luchadores premiados (selector)
bool EscenarioPremiadoDisponible(int entrada);   // H06: OFF y entradas no declaradas siempre disponibles
std::vector<InfoPremio> Premios();       // la tabla entera con su estado
std::vector<InfoPremio> PremiosAviso();  // lo ganado en el ultimo resultado (vacio si no hay)
int ColeccionPorcentaje();               // 0..100
void ColeccionVisto(const std::string& id);   // quita el "NUEVO" de uno
void ColeccionTodoVisto();                    // y de todos (al salir de la pantalla)

}  // namespace dbz3::heroe

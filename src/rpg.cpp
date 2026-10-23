// dbz3 - Retos de rol del HUB (fichas 23-28 de studies/11_vision_futura y lo que faltaba de la 03):
//   Simulacion (3 dias de entrenar/explorar/descansar y un jefe), Supervivencia con tienda (vida que se arrastra,
//   Zenie de la partida y tienda entre combates), Modo Heroe (3 variantes de Goku, 2 tecnicas a elegir y una mision),
//   Torres del Tiempo (torre de la semana y 2 fijas: 3 encuentros con tiempo y un modificador), Abyss (3 pisos y 3
//   cartas de mejora tras cada uno), Kumite de jefes (armadura con hueco castigable, golpes duros, aguantar) y Torneo
//   de 4 (cuadro propio con 3 bots).
// Selector: las entradas Torre / Supervivencia / Kumite del HUB abren una lista con el reto clasico y los nuevos.
// Combates: modos.cpp (StartPropio, Continue, SetVida, SetReglas, SetSiguiente) sobre el conector de hub.cpp. Las
// pantallas las dibuja el panel de modos.cpp (Dibujar) con las piezas del juego (native_ui). Mientras tanto el juego no
// recibe el mando.
// Guardado: user_data/dbz3/rpg.toml (version 1; .tmp + rename, la anterior en .bak; roto: se aparta a .roto y se
// empieza de cero; version mas nueva: no se toca). Monedero de Zenie del HUB (premios de los retos) y la partida a
// medias de cada reto (se reanuda tal cual: semilla, eventos y vida). La partida del juego no se toca.
// US y EU (como modos.cpp). Docs: docs/14_frentes/rpg.md.
#include <imgui.h>

#include <rex/logging.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "guest_region.h"
#include "ingame/game_style.h"
#include "launcher/i18n.h"
#include "launcher/settings.h"
#include "cinematica.h"
#include "escenarios.h"
#include "heroe_api.h"
#include "modos.h"
#include "mods.h"
#include "tagteam.h"
#include "cac/cac.h"
#include "heroe_ui.h"   // studies 8 H3/H4: Create / Edit y Select Master al estilo UT

namespace dbz3::rpg {
namespace {
namespace M = dbz3::modos;
namespace S = dbz3::ingame::style;
using M::L;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kLb = 0x0100, kRb = 0x0200,
                   kStart = 0x0010, kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;   // X/Y: moveset; Start: fichas (K)

uint32_t Hash(uint32_t a, uint32_t b) {   // mezcla entera (semilla, paso): lo mismo da lo mismo
  uint32_t h = a * 0x9E3779B1u ^ (b * 0x85EBCA6Bu + 0x7F4A7C15u);
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return h;
}
std::string Num(int n) { return std::to_string(n); }

// ---- Catalogos ----
// Simulacion (23): jefe del dia 4 segun la semilla
constexpr int kSimJefes[] = {33, 40, 36};
constexpr int kSimDias = 3;
// Tienda (24): 5 rivales con mas vida cada vez; la vida del jugador se arrastra
constexpr int kTiendaRivales[] = {42, 18, 19, 21, 20};
constexpr int kZeniVictoria = 100, kZeniPerfecto = 50;
struct Articulo {
  int precio;
  const char* es;
  const char* en;
  const char* fr;
  const char* de;
  const char* it;
};
const Articulo kArticulos[] = {
    {60, "Cura: +1000 de vida", "Heal: +1000 health", "Soin : +1000 vie", "Heilung: +1000 Leben", "Cura: +1000 vita"},
    {80, "Fuerza: +25% de daño (próximo combate)", "Strength: +25% damage (next fight)",
     "Force : +25% de dégâts (prochain combat)", "Kraft: +25% Schaden (nächster Kampf)",
     "Forza: +25% di danni (prossimo incontro)"},
    {80, "Defensa: -25% de daño recibido (próximo combate)", "Defense: -25% damage taken (next fight)",
     "Défense : -25% de dégâts subis (prochain combat)", "Abwehr: -25% erlittener Schaden (nächster Kampf)",
     "Difesa: -25% di danni subiti (prossimo incontro)"},
};
// Heroe (25): la creacion (raza, cuerpo, traje, maestro) y sus reglas estan en heroe.cpp
}  // namespace
}  // namespace dbz3::rpg
namespace dbz3::pasarela {
int RangoMejora(const std::string& clave, int valor, int pe_por_paso, int pe_primera);   // pasarela.h (Escalafon)
}
namespace dbz3::heroe {   // heroe.cpp (creacion del heroe, estilo, sagas; con g_mu puesto)
void Leer(const toml::table& t);
void Escribir(std::ostream& o);
int Cuerpo();
int Traje();
int EstiloId();
int MovesetId();   // frente 10: donante del moveset (-1: el del estilo)
int Saga();
int Nivel();
int NivelAntes();   // antes de la ultima saga
int Vida(int estilo);
int CuerpoDe(int extra, int jugador);
int PH();                   // puntos de habilidad sin gastar
bool GastarPH(int rama);    // 0 vida (+100), 1 dano (+2%)
int MaestroDeSaga(int saga);
bool EsCombateMaestro(int saga, int k);
int MaestroEnCombate(int saga, int k);   // ID del maestro que entra de relevo, -1 si no
int Combates(int saga);                 // combates de la saga (res/heroe/sagas.toml)
bool CambioBloqueado();                 // no se pudo cambiar de maestro (60% del actual)
std::string DetalleMaestros();          // pantalla de maestros: % y modificadores de cada uno
void Mover(int fila, int dir);
std::string Fila(int fila);
std::string Detalle();
std::string DetalleEstilo(int id);   // bonus y tecnicas del estilo (2 lineas)
std::string LineaNivel(int pendientes);   // "Nivel 2  XP 1/3" (+ los combates ganados de la saga en curso)
const char* NombreSaga(int saga);
int EstiloVecino(int d);   // luchador del estilo elegido +- d (carrusel)
bool EsMaestro(int id);
void ElegirEstilo(int id);   // el estilo de la partida es el elegido
dbz3::modos::Reglas Reglas(int estilo, int extra, int traje);
void Mision(int saga, int k, int estilo, int cuerpo, int* rival, int* vida, int* escenario);
std::string MisionTexto(int saga, int k, int rival);
std::vector<std::string> SagaTextos(int saga, bool intro);
void FinRun(int ganados, bool victoria);
void DuVictoria(int saga, int k);   // C3: combate de saga ganado en el mapa de Dragon Universe
bool DuDisponible();                 // C3: el Heroe puede entrar en Dragon Universe
void Sync(bool activo);
void Exportar(dbz3::cac::Heroe& h);   // heroe.cpp: el heroe actual pasa a su ficha (cac.toml)
void Importar(int slot);              // heroe.cpp: la ficha del slot pasa a ser el heroe actual
// studies 8 H6: guion de cada combate (forma del rival, nota jefe/gigante/pierde/maestro) y epilogo
int TotalSagas();
bool EsUltimaSaga(int saga);
int FormaRival(int saga, int k, int rival);
std::string NotaCombate(int saga, int k);
std::string TextoJefe(int saga, int k);
std::vector<std::string> Epilogo();
void PruebaPerder(bool en_combate);   // DBZ3_TEST_HEROE_PERDER
void MigrarProgreso(int slot);        // studies 8 H1: progreso global -> ficha (una vez, progreso = 2)
void CargarProgreso(int slot);        // studies 8 H1: progreso de la ficha -> partida
}  // namespace dbz3::heroe
namespace dbz3::hub { bool RequestDu(int slot); }   // hub.cpp: abre Dragon Universe del Heroe sin su rueda de personajes
namespace dbz3::rpg {
namespace {
namespace M = dbz3::modos;
namespace S = dbz3::ingame::style;
namespace H = dbz3::heroe;
namespace T = dbz3::i18n;   // textos nuevos: T::T(es, en)
using M::L;
// Texto con solo es/en en el codigo (titulos de mision, nombres de torre): es y en tal cual; fr/de/it por
// i18n::T (clave = texto ingles en res/ingame_ui/i18n_modos.toml, o kTable por el espanol).
const char* Tr(const char* es, const char* en) {
  return !std::strcmp(dbz3::i18n::LangCode(), "es") ? es : dbz3::i18n::T(es, en);
}
// Heroe: sagas de 3 combates en heroe.cpp (Mision, MisionTexto)
// Torres del Tiempo (26): catalogo (version 1). La de la semana rota cada lunes (semana UTC); 0 y 1, siempre.
struct Torre {
  const char* es;
  const char* en;
  int rivales[3];
  int tiempo;
  M::Reglas reglas;
  int regen;
  const char* mod_es;
  const char* mod_en;
  const char* mod_fr;
  const char* mod_de;
  const char* mod_it;
};
M::Reglas R(int hecho, int recibido, int armadura, int ki) {
  M::Reglas r;
  r.dano_hecho = hecho;
  r.dano_recibido = recibido;
  r.armadura = armadura;
  r.ki_drena = ki;
  return r;
}
const Torre kTorres[] = {
    {"Torre Saiyan", "Saiyan Tower", {18, 19, 7}, 60, R(100, 100, 0, 4), 0, "Suelo que drena el ki",
     "Floor that drains ki", "Sol qui draine le ki", "Boden, der Ki entzieht", "Suolo che prosciuga il ki"},
    {"Torre Androide", "Android Tower", {29, 30, 33}, 90, R(100, 100, 0, 0), 20, "El rival se regenera",
     "The rival regenerates", "L'adversaire se régénère", "Der Gegner regeneriert sich", "L'avversario si rigenera"},
    {"Torre Ginyu", "Ginyu Tower", {21, 20, 27}, 75, R(100, 150, 0, 0), 0, "Golpes duros: +50% de daño recibido",
     "Hard hits: +50% damage taken", "Coups durs : +50% de dégâts subis", "Harte Treffer: +50% erlittener Schaden",
     "Colpi duri: +50% di danni subiti"},
    {"Torre Majin", "Majin Tower", {34, 35, 36}, 99, R(100, 100, 30, 0), 0, "Rivales blindados: -30% de daño",
     "Armored rivals: -30% damage", "Adversaires blindés : -30% de dégâts", "Gepanzerte Gegner: -30% Schaden",
     "Avversari corazzati: -30% di danni"},
};
constexpr int kNumTorres = int(std::size(kTorres));
int SemanaUtc() {   // PRUEBA: DBZ3_TEST_SEMANA=N simula otra semana (otra fecha)
  static const char* e = std::getenv("DBZ3_TEST_SEMANA");
  return e ? std::atoi(e) : int(std::time(nullptr) / (7 * 86400));
}
// Cambia cada semana (con el hash salian muchas semanas seguidas la misma: medido 2963-2968).
int TorreDeLaSemana(int semana) { return 2 + int(uint32_t(semana) % uint32_t(kNumTorres - 2)); }
// Abyss (27): 3 pisos (rival de cada piso por la semilla) y cartas de mejora
constexpr int kAbyssPisos[3][3] = {{42, 18, 21}, {19, 20, 28}, {33, 27, 36}};
struct Carta {
  int max;
  const char* es;
  const char* en;
  const char* fr;
  const char* de;
  const char* it;
};
const Carta kCartas[] = {
    {2, "Fuerza: +25% de daño", "Strength: +25% damage", "Force : +25% de dégâts", "Kraft: +25% Schaden",
     "Forza: +25% di danni"},
    {2, "Piel de acero: -25% de daño recibido", "Steel skin: -25% damage taken", "Peau d'acier : -25% de dégâts subis",
     "Stahlhaut: -25% erlittener Schaden", "Pelle d'acciaio: -25% di danni subiti"},
    {2, "Vitalidad: +1000 de vida máxima", "Vitality: +1000 max health", "Vitalité : +1000 vie max",
     "Vitalität: +1000 max. Leben", "Vitalità: +1000 vita massima"},
    {1, "Ki eterno: el ki se recarga solo", "Eternal ki: ki recharges by itself", "Ki éternel : le ki se recharge seul",
     "Ewiges Ki: Ki lädt sich selbst auf", "Ki eterno: il ki si ricarica da solo"},
    {1, "Cápsula Rage!!!: +28% de ataque", "Rage!!! capsule: +28% attack", "Capsule Rage!!! : +28% d'attaque",
     "Kapsel Rage!!!: +28% Angriff", "Capsula Rage!!!: +28% attacco"},
};
constexpr int kNumCartas = int(std::size(kCartas));
// Kumite de jefes (28): tres jefes de tamano normal, vida finita, armadura con un hueco de 3 s y golpes duros
struct Jefe {
  int id, vida, armadura, ventana, recibido, objetivo;
  float gigante;   // escala (escenarios.md R1: gigantes.cpp); el 2 es el jefe gigante, con tormenta (clima.cpp)
};
const Jefe kJefes[] = {{33, 5000, 50, 10, 150, 0, 0}, {40, 6000, 60, 8, 175, 0, 1.8f}, {36, 7000, 70, 12, 150, 45, 0}};

// Mision 100 (la idea de Tenkaichi 3, sin sus datos): 20 paginas de 5 misiones por sagas (11 sagas). Todas siguen el
// mismo patron con la dificultad de su pagina: 0 combate, 1 contrarreloj, 2 aguantar, 3 regla especial (rota cada
// pagina) y 4 Tag Team (tu y un aliado contra un equipo; SELECT releva) o, cada 5 paginas, supervivencia (3 seguidos
// con la misma vida). Rango S/A/B/C por la vida que queda; la pagina siguiente se abre con 3 misiones superadas.
struct PaginaM100 {
  int a, b, jefe, aliado;   // rivales (distintos: forman el equipo rival) y el aliado de la mision de equipo
};
const PaginaM100 kPaginas[] = {
    {42, 10, 18, 11}, {42, 19, 7, 10},  {42, 21, 20, 7},  {20, 7, 27, 11}, {21, 20, 27, 0},    // Saiyans, Namek
    {27, 42, 38, 0},  {32, 29, 30, 8},  {29, 30, 28, 11}, {43, 28, 33, 3}, {43, 30, 33, 3},     // Cooler, Androides, Cell
    {7, 0, 40, 8},    {14, 15, 4, 9},   {7, 42, 37, 16},  {37, 7, 34, 4},  {37, 34, 35, 6},     // Broly, Torneo, Majin
    {34, 4, 35, 6},   {35, 34, 36, 7},  {36, 14, 17, 0},  {40, 38, 41, 0}, {40, 36, 41, 7}};   // Buu, Uub, Omega
constexpr int kNumPaginas = int(std::size(kPaginas)), kNumMisiones = kNumPaginas * 5;
struct MisionM100 {
  std::vector<int> rivales;   // un combate por rival (supervivencia) o, con equipo, el equipo rival en uno
  bool equipo = false;
  int aliado = -1;            // companero del jugador (Tag Team)
  int vida = 0, tiempo = 0, regen = 0, objetivo = 0;
  float gigante = 0;
  M::Reglas reglas;
};
MisionM100 MisionDe(int n, int jugador) {
  n = std::clamp(n, 0, kNumMisiones - 1);
  const int pg = n / 5, m = n % 5;
  const PaginaM100& p = kPaginas[pg];
  MisionM100 x;
  x.vida = 1800 + 120 * pg;
  switch (m) {
    case 0: x.rivales = {p.a}; break;
    case 1:
      x.rivales = {p.b};
      x.tiempo = std::max(45, 99 - 3 * pg);
      break;
    case 2:
      x.rivales = {p.jefe};
      x.vida = 4000 + 100 * pg;
      x.objetivo = 40 + pg;
      x.reglas.dano_recibido = std::min(200, 125 + 5 * pg);
      break;
    case 3:
      x.rivales = {p.jefe};
      x.vida = 2600 + 120 * pg;
      if (pg % 4 == 0) x.reglas.ki_drena = 4;
      if (pg % 4 == 1) x.regen = 15 + pg;
      if (pg % 4 == 2) x.reglas.armadura = 30 + 2 * pg, x.reglas.ventana = 10;
      if (pg % 4 == 3) x.gigante = 1.6f;
      break;
    default:
      if (pg % 5 == 4) {   // supervivencia: los tres seguidos, la vida se arrastra
        x.rivales = {p.a, p.b, p.jefe};
        break;
      }
      x.equipo = true;
      x.rivales = pg >= 10 ? std::vector<int>{p.a, p.b, p.jefe} : std::vector<int>{p.a, p.jefe};
      x.aliado = p.aliado != jugador ? p.aliado : jugador != 0 ? 0 : 7;
      x.vida = 0;   // cada miembro con su vida normal
      break;
  }
  return x;
}
// Titulo propio de cada mision (es, en; fr/de/it en ingles, como las torres)
const char* const kTitulos[100][2] = {
    {"Semilla saiyan", "Saiyan seed"}, {"Calentamiento en Kame House", "Kame House warm-up"},
    {"El hermano perdido", "The lost brother"}, {"Cola de mono", "Monkey tail"},
    {"Alianza inesperada", "Unlikely alliance"}, {"Cosecha de Saibaman", "Saibaman harvest"},
    {"Nappa no espera", "Nappa won't wait"}, {"Más de 8000", "Over 8000"},
    {"Orgullo del príncipe", "Prince's pride"}, {"Defensa de la Tierra", "Earth's defense"},
    {"Exploradores de Namek", "Namek scouts"}, {"La pose de Recoome", "Recoome's pose"},
    {"Cambio de cuerpo", "Body change"}, {"Armadura de élite", "Elite armor"},
    {"Tregua en Namek", "Namek truce"}, {"Capitán sin fuerzas", "Captain down"},
    {"Príncipe traicionado", "Betrayed prince"}, {"El emperador del universo", "Emperor of the universe"},
    {"Transformación final", "Final transformation"}, {"Fusión namekiana", "Namekian fusion"},
    {"Fuerzas especiales", "Special forces"}, {"Cuenta atrás de Namek", "Namek countdown"},
    {"Cinco minutos", "Five minutes"}, {"Planeta en llamas", "Planet ablaze"},
    {"Leyenda dorada", "Golden legend"}, {"Venganza de familia", "Family revenge"},
    {"Escuadrón de Cooler", "Cooler's squadron"}, {"El hermano mayor", "The older brother"},
    {"Metal que se repara", "Self-repairing metal"}, {"Dinastía helada", "Frozen dynasty"},
    {"Laboratorio secreto", "Secret laboratory"}, {"Ruta de los androides", "Androids' road"},
    {"Gemelos sin piedad", "Ruthless twins"}, {"Blindaje de Gero", "Gero's plating"},
    {"Viajero del futuro", "Time traveler"}, {"El 17 al volante", "17 at the wheel"},
    {"Duelo en la isla", "Island duel"}, {"El gigante pacífico", "The gentle giant"},
    {"Androide colosal", "Colossal android"}, {"Rival a la altura", "A worthy rival"},
    {"Hijos de Cell", "Cell's children"}, {"Último aliento del 16", "16's last stand"},
    {"Los Juegos de Cell", "The Cell Games"}, {"Energía absorbida", "Absorbed energy"},
    {"Padre e hijo", "Father and son"}, {"Ring del torneo", "Tournament ring"},
    {"La que escapó", "The one who got away"}, {"Forma perfecta", "Perfect form"},
    {"Regeneración celular", "Cellular regeneration"}, {"El fin de los juegos", "End of the games"},
    {"Príncipe en guardia", "Prince on guard"}, {"Saiyan de clase baja", "Low-class Saiyan"},
    {"El saiyan legendario", "The legendary Saiyan"}, {"Furia sin límites", "Limitless fury"},
    {"Nuevo planeta Vegeta", "New Planet Vegeta"}, {"Campeón del mundo", "World champion"},
    {"La hija del campeón", "The champion's daughter"}, {"Héroe enmascarado", "Masked hero"},
    {"Estrella del torneo", "Tournament star"}, {"Final de exhibición", "Exhibition final"},
    {"Marca de Majin", "Majin mark"}, {"Guerreros de Babidi", "Babidi's warriors"},
    {"Rey de los demonios", "King of demons"}, {"Escupitajo de piedra", "Stone spit"},
    {"Guardianes del cosmos", "Guardians of the cosmos"}, {"Nave de Babidi", "Babidi's ship"},
    {"Sacrificio del príncipe", "Prince's sacrifice"}, {"El despertar de Buu", "Buu awakens"},
    {"Cuerpo de goma", "Rubber body"}, {"Poder místico", "Mystic power"},
    {"Ejército de Babidi", "Babidi's army"}, {"Golosinas para Buu", "Candy for Buu"},
    {"Mal en estado puro", "Pure evil"}, {"Piel de chicle", "Chewing-gum skin"},
    {"Noche sin fin", "Endless night"}, {"Buu bueno", "Good Buu"},
    {"Gohan absorbido", "Gohan absorbed"}, {"Habitación del Tiempo", "Time Chamber"},
    {"Monstruo colosal", "Colossal monster"}, {"Fusión fallida", "Failed fusion"},
    {"Por dentro de Buu", "Inside Buu"}, {"La mitad buena", "The good half"},
    {"Forma original", "Original form"}, {"Genkidama", "Spirit Bomb"},
    {"Últimos saiyans", "Last Saiyans"}, {"Reencarnación", "Reincarnation"},
    {"El salvador del mundo", "Savior of the world"}, {"Nuevo discípulo", "New disciple"},
    {"Talento oculto", "Hidden talent"}, {"Relevo generacional", "Next generation"},
    {"Furia de Broly", "Broly's rage"}, {"El regreso de Cooler", "Cooler returns"},
    {"Dragón de una estrella", "One-Star Dragon"}, {"Esferas corruptas", "Corrupted Dragon Balls"},
    {"Energía negativa", "Negative energy"}, {"Último asalto", "Final assault"},
    {"Caos primigenio", "Primal chaos"}, {"El dragón definitivo", "The ultimate dragon"},
    {"Gigante de las sombras", "Shadow giant"}, {"Misión cumplida", "Mission complete"},
};
const char* TituloMision(int n) {
  const auto& t = kTitulos[std::clamp(n, 0, kNumMisiones - 1)];
  return Tr(t[0], t[1]);
}
const char* NombreMision(int n) {
  const int pg = n / 5, m = n % 5;
  switch (m) {
    case 0: return L("Combate", "Battle", "Combat", "Kampf", "Scontro");
    case 1: return L("Contrarreloj", "Time attack", "Contre la montre", "Zeitangriff", "Contro il tempo");
    case 2: return L("Aguanta", "Survive", "Tiens bon", "Durchhalten", "Resisti");
    case 3:
      switch (pg % 4) {
        case 0: return L("Ki que se agota", "Draining ki", "Ki qui s'épuise", "Schwindendes Ki", "Ki che si esaurisce");
        case 1: return L("Rival que se regenera", "Regenerating rival", "Adversaire qui se régénère",
                         "Gegner mit Regeneration", "Avversario che si rigenera");
        case 2: return L("Rival blindado", "Armored rival", "Adversaire blindé", "Gepanzerter Gegner", "Avversario corazzato");
        default: return L("Rival gigante", "Giant rival", "Adversaire géant", "Riesiger Gegner", "Avversario gigante");
      }
    default:
      return pg % 5 == 4 ? L("Supervivencia", "Survival", "Survie", "Überleben", "Sopravvivenza") : "Tag Team";
  }
}
std::string g_m100(kNumMisiones, '-');   // mejor rango de cada mision: - (sin superar), C, B, A, S
std::atomic<bool> g_equipos{false};      // la mision en curso lleva equipos (tagteam_slots.cpp no los borra)
std::atomic<bool> g_equipos_heroe{false};   // el combate del heroe lleva al maestro de relevo (Tag Team puntual)
bool PaginaAbierta(int pg) {   // con 3 misiones superadas se abre la siguiente (DBZ3_M100_TODO=1: todas abiertas)
  static const bool todo = std::getenv("DBZ3_M100_TODO") != nullptr;
  if (pg <= 0 || todo) return true;
  int n = 0;
  for (int k = 0; k < 5; ++k) n += g_m100[size_t((pg - 1) * 5 + k)] != '-';
  return n >= 3;
}
char RangoDe(int vida, int vida_max, int recibido) {   // por la vida que queda al acabar; Z = S sin recibir dano
  const int pc = vida * 100 / std::max(vida_max, 1);
  return pc >= 75 ? (recibido == 0 ? 'Z' : 'S') : pc >= 50 ? 'A' : pc >= 25 ? 'B' : 'C';
}

// ---- Partida a medias de cada reto y monedero (user_data/dbz3/rpg.toml) ----
struct Run {
  int modo = -1;   // M::Tipo (3-9); -1 = ninguna
  int jugador = 0, traje = 0;
  uint32_t semilla = 0;
  int ganados = 0;
  int vida = 3000, vida_max = 3000;
  int zeni = 0;           // Tienda: Zenie de la partida
  std::string eventos;    // Simulacion: E/X/D por dia; Abyss: cartas elegidas (0-4); Tienda: compras del combate
  int extra = 0;          // Heroe: variante; Torres: torre; Torneo: semifinal 2 ya jugada (1) y su ganador
  int tecnicas = 0;       // Heroe: mascara de kTecnicas
  int bots[3] = {-1, -1, -1};   // Torneo de 4
  int finalista = -1;            // Torneo de 4: ganador de la semifinal 2
};
std::mutex g_mu;
std::map<int, Run> g_runs;   // por modo
int g_zeni = 0;              // monedero del HUB (premios)
int g_tienda_record = 0;     // mas Zenie sin gastar al acabar una partida de la Tienda
int g_heroe_variante = 0, g_heroe_tecnicas = 3;   // receta del heroe (la ultima elegida)
bool g_leido = false, g_solo_lectura = false;
bool g_cac_ok = false;   // cac.toml leido o creado (no de una version mas nueva): se puede guardar
std::atomic<int> g_premio{0};   // Premio() desde modos.cpp (con su candado): se suma aqui despues

std::filesystem::path Ruta() { return dbz3::settings::UserDataRoot() / "rpg.toml"; }
std::filesystem::path CacRuta() { return dbz3::settings::UserDataRoot() / "cac.toml"; }

bool Leer(const std::filesystem::path& p, bool* nueva) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  try {
    const toml::table t = toml::parse(ss.str());
    const int64_t v = t["version"].value_or(int64_t(0));
    if (v > 1) {   // de una version mas nueva del port: no se reinterpreta ni se pisa
      *nueva = true;
      return false;
    }
    if (v != 1) return false;
    g_zeni = std::clamp(int(t["zeni"].value_or(int64_t(0))), 0, 9999999);
    g_tienda_record = int(t["tienda_record"].value_or(int64_t(0)));
    g_heroe_variante = std::clamp(int(t["heroe_variante"].value_or(int64_t(0))), 0, 2);
    g_heroe_tecnicas = int(t["heroe_tecnicas"].value_or(int64_t(3))) & 7;
    H::Leer(t);
    g_m100 = t["mision100"].value_or(std::string());
    g_m100.resize(size_t(kNumMisiones), '-');
    g_runs.clear();
    if (const toml::array* a = t["partida"].as_array()) {
      for (const toml::node& n : *a) {
        const toml::table* x = n.as_table();
        if (!x) continue;
        Run r;
        auto i = [&](const char* k, int d) { return int((*x)[k].value_or(int64_t(d))); };
        r.modo = i("modo", -1);
        r.jugador = i("jugador", 0);
        r.traje = i("traje", 0);
        r.semilla = uint32_t((*x)["semilla"].value_or(int64_t(0)));
        r.ganados = i("ganados", 0);
        r.vida_max = std::clamp(i("vida_max", 3000), 1, 7000);
        r.vida = std::clamp(i("vida", r.vida_max), 1, r.vida_max);
        r.zeni = std::max(i("zeni", 0), 0);
        r.eventos = (*x)["eventos"].value_or(std::string());
        r.extra = i("extra", 0);
        r.tecnicas = i("tecnicas", 0);
        r.finalista = i("finalista", -1);
        if (const toml::array* b = (*x)["bots"].as_array()) {
          for (size_t k = 0; k < 3 && k < b->size(); ++k) r.bots[k] = int((*b)[k].value_or(int64_t(-1)));
        }
        if (r.modo == M::kMision) r.extra = std::clamp(r.extra, 0, kNumMisiones - 1);
        if (r.modo == M::kTorres) r.extra = std::clamp(r.extra, 0, kNumTorres - 1);
        if (r.modo > M::kKumite && r.modo < M::kTipos && r.ganados >= 0 && M::EsJugable(r.jugador)) g_runs[r.modo] = r;
      }
    }
    return true;
  } catch (const toml::parse_error&) {
    return false;
  }
}

void CargarRpg() {   // con g_mu (lo llama Cargar)
  const auto p = Ruta();
  bool nueva = false;
  if (Leer(p, &nueva)) return;
  if (nueva) {
    g_solo_lectura = true;
    REXLOG_WARN("dbz3 rpg: {} es de una version mas nueva: no se usa ni se toca", p.string());
    return;
  }
  std::error_code ec;
  if (std::filesystem::exists(p, ec)) {   // roto: se aparta para mirarlo y se prueba la copia
    std::filesystem::rename(p, p.string() + ".roto", ec);
    REXLOG_WARN("dbz3 rpg: {} no se puede leer: apartado a .roto", p.string());
  }
  if (Leer(p.string() + ".bak", &nueva)) REXLOG_WARN("dbz3 rpg: se usa la copia de seguridad {}.bak", p.string());
}

// Fichas de heroe (cac.toml): van despues de rpg.toml y hub_heroe.toml; el heroe de la partida es el de la ficha
// activa. Primera vez (sin cac.toml): el heroe que ya hay pasa a ser la ficha 1, las partidas antiguas no cambian.
void CargarCac() {   // con g_mu
  // Catalogo real de piezas CaC (cuerpo/cara/pelo/ropa/color/voz/aura). Antes no se llamaba nunca y el
  // juego usaba el respaldo de 20 entradas: cara/ropa quedaban en 0..1 y el TOML de piezas no llegaba.
  for (const std::filesystem::path& cat : {dbz3::settings::UserDataRoot() / "heroe" / "piezas_cac.toml",
                                           dbz3::ModsRoot().parent_path() / "res" / "heroe" / "piezas_cac.toml",
                                           dbz3::ModsRoot().parent_path() / "mod center hd" / "cac" / "piezas_cac.toml"}) {
    std::error_code ec;
    if (std::filesystem::exists(cat, ec)) {
      dbz3::cac::CargarCatalogo(cat.string());
      break;
    }
  }
  const auto p = CacRuta();
  const std::string ruta = p.string();
  std::error_code ec;
  if (dbz3::cac::Leer(ruta)) {
    g_cac_ok = true;
    // studies 8 H1: el progreso (nivel, exp, saga, PH, maestros, entrenamiento) vive en cada ficha. Una sola vez
    // (ficha 1 sin `progreso = 2`): el progreso global de rpg.toml + hub_heroe.toml pasa a la ficha 1; las demas
    // fichas sin marca empiezan de cero. Despues manda la ficha activa.
    bool migrado = false;
    // el progreso global era el de la ficha activa: va a ella, no siempre a la 1 (si no, la activa saldria a cero)
    const int activa = dbz3::cac::SlotActivo();
    for (int i = 0; i < dbz3::cac::NumSlots(); ++i) {
      dbz3::cac::Heroe& f = dbz3::cac::SlotEditable(i);
      if (f.progreso == dbz3::cac::kProgresoFicha) continue;
      if (i == activa) {
        H::MigrarProgreso(i);
      } else {
        f.progreso = dbz3::cac::kProgresoFicha;   // sin progreso propio: de cero
      }
      migrado = true;
    }
    if (migrado) dbz3::cac::Guardar(ruta);
    H::Importar(dbz3::cac::SlotActivo());   // G06a: restaurar también cuerpo/apariencia de la ficha, no solo XP
    H::CargarProgreso(dbz3::cac::SlotActivo());
    return;
  }
  if (std::filesystem::exists(p, ec)) {   // de una version mas nueva: no se usa ni se pisa
    REXLOG_WARN("dbz3 rpg: {} es de una version mas nueva: no se toca", ruta);
    return;
  }
  H::Exportar(dbz3::cac::SlotEditable(0));   // sin cac.toml: el heroe y su progreso pasan a la ficha 1
  REXLOG_INFO("dbz3 heroe: sin cac.toml: el heroe actual y su progreso pasan a la ficha 1 (progreso = 2)");
  g_cac_ok = dbz3::cac::Guardar(ruta);
}

void Cargar() {   // con g_mu, una vez
  if (g_leido) return;
  g_leido = true;
  CargarRpg();
  CargarCac();
}

void Guardar() {   // con g_mu
  if (g_cac_ok) {   // la ficha activa de cac.toml = el heroe de la partida
    H::Exportar(dbz3::cac::SlotEditable(dbz3::cac::SlotActivo()));
    dbz3::cac::Guardar(CacRuta().string());
  }
  if (g_solo_lectura) return;
  std::ostringstream o;
  o << "# Retos de rol del HUB (src/rpg.cpp): monedero de Zenie y partidas a medias. No editar con el juego abierto.\n"
       "version = 1\nzeni = "
    << g_zeni << "\ntienda_record = " << g_tienda_record << "\nheroe_variante = " << g_heroe_variante
    << "\nheroe_tecnicas = " << g_heroe_tecnicas << "\n";
  H::Escribir(o);
  o << "mision100 = " << toml::value<std::string>(g_m100) << "\n";
  for (const auto& [modo, r] : g_runs) {
    o << "\n[[partida]]\nmodo = " << r.modo << "\njugador = " << r.jugador << "\ntraje = " << r.traje
      << "\nsemilla = " << r.semilla << "\nganados = " << r.ganados << "\nvida = " << r.vida << "\nvida_max = "
      << r.vida_max << "\nzeni = " << r.zeni << "\neventos = " << toml::value<std::string>(r.eventos)
      << "\nextra = " << r.extra << "\ntecnicas = " << r.tecnicas << "\nbots = [" << r.bots[0] << ", " << r.bots[1]
      << ", " << r.bots[2] << "]\nfinalista = " << r.finalista << "\n";
  }
  std::error_code ec;
  const auto p = Ruta();
  std::filesystem::create_directories(p.parent_path(), ec);
  const std::filesystem::path tmp = p.string() + ".tmp", bak = p.string() + ".bak";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << o.str();
    if (!f.flush()) {
      REXLOG_WARN("dbz3 rpg: no se puede escribir {}", tmp.string());
      return;
    }
  }
  if (std::filesystem::exists(p, ec)) std::filesystem::copy_file(p, bak, std::filesystem::copy_options::overwrite_existing, ec);
  std::filesystem::rename(tmp, p, ec);
  if (ec) REXLOG_WARN("dbz3 rpg: no se puede guardar {} ({})", p.string(), ec.message());
}

// ---- Simulacion (23): el estado sale de la semilla y los eventos (lo mismo da lo mismo) ----
struct Sim {
  int dia = 1;              // 1-3; 4 = toca el jefe
  int vida = 3000, vida_max = 3000;
  int fuerza = 0, fatiga = 0;
  std::vector<int> caps;    // capsulas encontradas (equipo de la partida, no de la del juego)
  std::string ultimo_es, ultimo_en, ultimo_fr, ultimo_de, ultimo_it;   // lo que paso el ultimo dia
};
Sim SimDe(const Run& r) {
  Sim s;
  for (const char e : r.eventos) {
    const uint32_t roll = Hash(r.semilla, uint32_t(s.dia)) % 4;
    if (e == 'E') {   // entrenar: +15% de dano, una de fatiga y -300 de vida
      s.fuerza += 15;
      s.fatiga += 1;
      s.vida = std::max(1, s.vida - 300);
      s.ultimo_es = "Entrenamiento duro: +15% de daño, +1 de fatiga.";
      s.ultimo_en = "Hard training: +15% damage, +1 fatigue.";
      s.ultimo_fr = "Entraînement intense : +15% de dégâts, +1 de fatigue.";
      s.ultimo_de = "Hartes Training: +15% Schaden, +1 Erschöpfung.";
      s.ultimo_it = "Allenamento duro: +15% di danni, +1 di fatica.";
    } else if (e == 'X') {   // explorar: tirada con la semilla de la partida
      if (roll == 0) {
        s.caps.push_back(366);
        s.ultimo_es = "Has encontrado la cápsula Potential (+24% de ataque en el combate).";
        s.ultimo_en = "You found the Potential capsule (+24% attack in the fight).";
        s.ultimo_fr = "Tu as trouvé la capsule Potential (+24% d'attaque au combat).";
        s.ultimo_de = "Du hast die Kapsel Potential gefunden (+24% Angriff im Kampf).";
        s.ultimo_it = "Hai trovato la capsula Potential (+24% di attacco nell'incontro).";
      } else if (roll == 1) {
        s.vida_max = std::min(7000, s.vida_max + 500);
        s.vida = std::min(s.vida_max, s.vida + 500);
        s.ultimo_es = "Has encontrado una alubia mágica: +500 de vida máxima.";
        s.ultimo_en = "You found a magic bean: +500 max health.";
        s.ultimo_fr = "Tu as trouvé un haricot magique : +500 vie max.";
        s.ultimo_de = "Du hast eine magische Bohne gefunden: +500 max. Leben.";
        s.ultimo_it = "Hai trovato un fagiolo magico: +500 vita massima.";
      } else if (roll == 2) {
        s.vida = std::max(1, s.vida - 500);
        s.ultimo_es = "¡Una emboscada! Pierdes 500 de vida.";
        s.ultimo_en = "An ambush! You lose 500 health.";
        s.ultimo_fr = "Une embuscade ! Tu perds 500 de vie.";
        s.ultimo_de = "Ein Hinterhalt! Du verlierst 500 Leben.";
        s.ultimo_it = "Un'imboscata! Perdi 500 di vita.";
      } else {
        s.caps.push_back(382);
        s.ultimo_es = "Has encontrado la cápsula Spirit!! (+20% de defensa un rato).";
        s.ultimo_en = "You found the Spirit!! capsule (+20% defense for a while).";
        s.ultimo_fr = "Tu as trouvé la capsule Spirit!! (+20% de défense un moment).";
        s.ultimo_de = "Du hast die Kapsel Spirit!! gefunden (+20% Abwehr für eine Weile).";
        s.ultimo_it = "Hai trovato la capsula Spirit!! (+20% di difesa per un po').";
      }
    } else {   // descansar: +1500 de vida (sin pasar del maximo) y sin fatiga
      s.vida = std::min(s.vida_max, s.vida + 1500);
      s.fatiga = 0;
      s.ultimo_es = "Has descansado: +1500 de vida y sin fatiga.";
      s.ultimo_en = "You rested: +1500 health and no fatigue.";
      s.ultimo_fr = "Tu t'es reposé : +1500 vie et plus de fatigue.";
      s.ultimo_de = "Du hast dich ausgeruht: +1500 Leben und keine Erschöpfung.";
      s.ultimo_it = "Ti sei riposato: +1500 vita e niente fatica.";
    }
    ++s.dia;
  }
  return s;
}

// ---- Abyss (27): cartas elegidas (eventos) y oferta de cada piso (de la semilla: no cambia al reabrir) ----
struct Abyss {
  int stacks[kNumCartas] = {};
};
Abyss AbyssDe(const Run& r) {
  Abyss a;
  for (const char c : r.eventos) {
    if (c >= '0' && c < '0' + kNumCartas) ++a.stacks[c - '0'];
  }
  return a;
}
std::vector<int> Oferta(const Run& r, int piso) {   // hasta 3 cartas que aun no estan al maximo
  const Abyss a = AbyssDe(r);
  std::vector<int> v;
  for (int k = 0; k < kNumCartas; ++k) {
    if (a.stacks[k] < kCartas[k].max) v.push_back(k);
  }
  for (int i = int(v.size()) - 1; i > 0; --i) {
    std::swap(v[size_t(i)], v[Hash(r.semilla + uint32_t(piso), uint32_t(i)) % uint32_t(i + 1)]);
  }
  if (v.size() > 3) v.resize(3);
  return v;
}

// Reglas del combate siguiente de una partida (con g_mu).
M::Reglas ReglasDe(const Run& r) {
  M::Reglas g;
  if (r.modo == M::kSimulacion) {
    const Sim s = SimDe(r);
    g.dano_hecho = 100 + s.fuerza;
    g.dano_recibido = 100 + 10 * s.fatiga;   // la fatiga se paga en el combate
    g.ncaps = int(std::min<size_t>(s.caps.size(), 7));
    for (int i = 0; i < g.ncaps; ++i) g.caps[i] = s.caps[size_t(i)];
  } else if (r.modo == M::kTienda) {
    if (r.eventos.find('1') != std::string::npos) g.dano_hecho = 125;
    if (r.eventos.find('2') != std::string::npos) g.dano_recibido = 75;
  } else if (r.modo == M::kHeroe) {   // heroe.cpp: bonus y tecnicas del maestro (extra) para ese cuerpo
    g = H::Reglas(r.jugador, r.extra, r.traje);
  } else if (r.modo == M::kTorres) {
    g = kTorres[std::clamp(r.extra, 0, kNumTorres - 1)].reglas;
  } else if (r.modo == M::kAbyss) {
    const Abyss a = AbyssDe(r);
    g.dano_hecho = 100 + 25 * a.stacks[0];
    g.dano_recibido = 100 - 25 * a.stacks[1];
    if (a.stacks[3] > 0) g.ki_drena = -5;   // el ki sube un 5% del tope por segundo
    if (a.stacks[4] > 0) {
      g.ncaps = 1;
      g.caps[0] = 380;
    }
  } else if (r.modo == M::kMision) {
    g = MisionDe(r.extra, r.jugador).reglas;
  } else if (r.modo == M::kJefes) {
    const Jefe& j = kJefes[std::clamp(r.ganados, 0, 2)];
    g.armadura = j.armadura;
    g.ventana = j.ventana;
    g.dano_recibido = j.recibido;
  }
  return g;
}

// studies 8 H6: lo que el guion de la saga (res/heroe/sagas.toml) pone en el combate k del heroe: la forma del rival
// (fusiones nativas de B3) y, con la nota "gigante", el rival a escala con el cielo oscuro. La escala 1.8 es la del
// jefe gigante del Kumite de jefes (kJefes, Omega; gigantes.cpp) [H]: no hay medida del Oozaru de B3 (no existe en B3).
constexpr float kEscalaGigante = 1.8f;
void GuionHeroe(int saga, int k, M::Pelea& p) {
  p.forma = H::FormaRival(saga, k, p.rival);
  if (H::NotaCombate(saga, k) == "gigante") {
    p.gigante = kEscalaGigante;
    p.clima = true;
  }
}

// Combates de una partida (para StartPropio; con g_mu).
std::vector<M::Pelea> PeleasDe(const Run& r) {
  std::vector<M::Pelea> v;
  auto add = [&v](int id, int vida, int esc, int tiempo = 0, int regen = 0, int objetivo = 0) {
    M::Pelea p;
    p.rival = id;
    p.vida = vida;
    p.escenario = esc;
    p.tiempo = tiempo;
    p.regen = regen;
    p.objetivo = objetivo;
    v.push_back(p);
  };
  switch (r.modo) {
    case M::kSimulacion: add(kSimJefes[r.semilla % 3], 4000, 6, 99); break;
    case M::kTienda:
      for (int k = 0; k < 5; ++k) add(kTiendaRivales[k], 1500 + 400 * k, k + 1);
      break;
    case M::kHeroe:
      for (int k = 0; k < H::Combates(r.tecnicas); ++k) {   // r.jugador = estilo, r.tecnicas = saga (heroe.cpp)
        int id = 0, vida = 0, esc = 0;
        H::Mision(r.tecnicas, k, r.jugador, H::CuerpoDe(r.extra, r.jugador), &id, &vida, &esc);
        add(id, vida, esc);
        GuionHeroe(r.tecnicas, k, v.back());
      }
      break;
    case M::kTorres: {
      const Torre& t = kTorres[std::clamp(r.extra, 0, kNumTorres - 1)];
      for (int k = 0; k < 3; ++k) add(t.rivales[k], 2000 + 500 * k, (r.extra * 3 + k) % 8, t.tiempo, t.regen);
      break;
    }
    case M::kAbyss:
      for (int k = 0; k < 3; ++k) add(kAbyssPisos[k][Hash(r.semilla, 100 + uint32_t(k)) % 3], 2000 + 1000 * k, 7 - k);
      break;
    case M::kJefes:
      for (int k = 0; k < 3; ++k) {
        add(kJefes[k].id, kJefes[k].vida, 3 + k, kJefes[k].objetivo ? 0 : 120, 0, kJefes[k].objetivo);
        v.back().gigante = kJefes[k].gigante;
        v.back().clima = kJefes[k].gigante > 0;
      }
      break;
    case M::kMision: {
      const MisionM100 x = MisionDe(r.extra, r.jugador);
      const size_t n = x.equipo ? 1 : x.rivales.size();
      for (size_t k = 0; k < n; ++k) {
        add(x.rivales[k], x.vida, (r.extra + int(k)) % 8, x.tiempo, x.regen, x.objetivo);
        v.back().gigante = x.gigante;
        v.back().clima = x.gigante > 0;
      }
      break;
    }
    case M::kTorneo4:   // semifinal contra el bot 1; la final contra quien gane la otra (SetSiguiente)
      add(r.bots[0], 3000, 0);
      add(r.finalista >= 0 ? r.finalista : r.bots[1], 3000, 0);
      break;
    default: break;
  }
  return v;
}

const char* Palabra(int modo) {
  switch (modo) {
    case M::kTienda: return L("Ronda", "Round", "Manche", "Runde", "Round");
    case M::kHeroe: return L("Misión", "Mission", "Mission", "Mission", "Missione");
    case M::kTorres:
    case M::kAbyss: return L("Piso", "Floor", "Étage", "Etage", "Piano");
    case M::kTorneo4: return L("Ronda", "Round", "Tour", "Runde", "Turno");
    case M::kMision: return L("Combate", "Fight", "Combat", "Kampf", "Incontro");
    default: return L("Jefe", "Boss", "Boss", "Boss", "Boss");
  }
}
int PremioDe(int modo, int extra = 0) {
  switch (modo) {
    case M::kMision: return 100 + 25 * (std::clamp(extra, 0, kNumMisiones - 1) / 5);
    case M::kSimulacion: return 500;
    case M::kHeroe: return 700;
    case M::kTorres: return 600;
    case M::kAbyss: return 800;
    case M::kJefes: return 1500;
    case M::kTorneo4: return 600;
    default: return 0;   // Tienda: su Zenie de la partida
  }
}

// ---- Pantallas ----
enum Vista { kCerrada, kSelector, kSimDia, kHeroeP, kTorresP, kCuadro, kMisionP, kHeroeCrear, kHeroeMaestro, kHeroeMoveset, kHeroeMapa, kHeroeLista };
Vista g_vista = kCerrada;
int g_entrada = 0, g_sel = 0, g_cursor = 0, g_cursor2 = 0;   // selector, pantallas, torre
int g_jugador = 0, g_traje = 0;
std::string g_aviso;          // lo que dice el bocadillo tras una accion
std::string g_rango;          // rango del jugador (ficha 01), al abrir el selector
int g_reto_visto = -1;        // Estado::ganados ya contado (resultados)
bool g_fin = false;           // pantalla final de un reto propio
bool g_fin_hecho = false;     // ... ya vista (hasta el siguiente StartPropio)
M::Estado g_fin_st;           // ... su estado
Run g_fin_run;
std::string g_fin_texto;
// studies 8 H6: pantalla final del heroe tras completar una saga: primero su cierre (y el epilogo tras la ultima)
std::vector<std::string> g_fin_cierre;
int g_fin_pagina = 1, g_fin_saga = 0;
bool g_fin_epilogo = false, g_fin_pierde = false, g_fin_cierre_dicho = false;
int g_pagina = 0, g_m100_sel = 0;   // Mision 100: pagina a la vista y mision elegida
char g_m100_ultimo = '-';           // rango de la ultima mision superada (pantalla final)

// Retos de cada entrada del HUB: el clasico (0-2) y los nuevos.
const std::vector<int>& Retos(int entrada) {
  static const std::vector<int> k[3] = {{M::kTorre, M::kTorres, M::kAbyss},
                                        {M::kSupervivencia, M::kTienda, M::kSimulacion},
                                        {M::kKumite, M::kJefes, M::kTorneo4, M::kHeroe}};
  return k[std::clamp(entrada, 0, 2)];
}

std::string Descripcion(int modo) {
  switch (modo) {
    case M::kTorre:
    case M::kSupervivencia:
    case M::kKumite:
      return L("El reto de siempre (o el que hayas puesto con el Kit). Si lo dejaste a medias, sigues ahí.",
               "The usual challenge (or the one set with the Kit). If you left it halfway, you continue there.",
               "Le défi habituel (ou celui choisi avec le Kit). Si tu l'as laissé en cours, tu reprends là.",
               "Die übliche Herausforderung (oder die im Kit gewählte). Wenn du mittendrin aufgehört hast, geht es dort weiter.",
               "La solita sfida (o quella scelta con il Kit). Se l'hai lasciata a metà, riprendi da lì.");
    case M::kSimulacion:
      return L("Tres días antes de un gran combate: entrena, explora o descansa. Cada elección gasta un día. "
               "El cuarto día llega el jefe.",
               "Three days before a great battle: train, explore or rest. Each choice uses a day. "
               "On the fourth day the boss arrives.",
               "Trois jours avant un grand combat : entraîne-toi, explore ou repose-toi. Chaque choix coûte un jour. "
               "Le quatrième jour, le boss arrive.",
               "Drei Tage vor einem großen Kampf: trainiere, erkunde oder ruh dich aus. Jede Wahl kostet einen Tag. "
               "Am vierten Tag kommt der Boss.",
               "Tre giorni prima di un grande scontro: allenati, esplora o riposa. Ogni scelta costa un giorno. "
               "Il quarto giorno arriva il boss.");
    case M::kTienda:
      return L("Cinco combates seguidos con la misma vida. Cada victoria da Zenie (más si no te tocan) para la "
               "tienda: curas y mejoras para el combate siguiente.",
               "Five fights in a row with the same health. Each win gives Zenie (more without taking damage) for "
               "the shop: heals and boosts for the next fight.",
               "Cinq combats d'affilée avec la même vie. Chaque victoire rapporte des Zenie (plus sans être touché) "
               "pour la boutique : soins et bonus pour le combat suivant.",
               "Fünf Kämpfe am Stück mit demselben Leben. Jeder Sieg bringt Zenie (mehr ohne Treffer) für den "
               "Laden: Heilung und Boosts für den nächsten Kampf.",
               "Cinque incontri di fila con la stessa vita. Ogni vittoria dà Zenie (di più senza subire danni) "
               "per il negozio: cure e potenziamenti per l'incontro successivo.");
    case M::kHeroe:
      return T::T("(Beta) Tu héroe = un cuerpo (su aspecto) + un estilo (los golpes de otro luchador; los maestros dan "
                  "bonus). Gana combates para subir de nivel.",
                  "(Beta) Your hero = a body (his look) + a style (another fighter's moves; masters give a bonus). "
                  "Win fights to level up.");
    case M::kTorres:
      return L("Torres de tres combates con tiempo límite y una regla especial. La de la semana cambia cada lunes.",
               "Three-fight towers with a time limit and a special rule. The weekly one changes every Monday.",
               "Tours de trois combats avec limite de temps et une règle spéciale. Celle de la semaine change chaque lundi.",
               "Türme mit drei Kämpfen, Zeitlimit und einer Sonderregel. Der Wochenturm wechselt jeden Montag.",
               "Torri di tre incontri con limite di tempo e una regola speciale. Quella della settimana cambia ogni lunedì.");
    case M::kAbyss:
      return L("Baja tres pisos. Tras cada victoria eliges una de tres cartas de mejora, solo para esta partida. "
               "Si caes, se acabó.",
               "Go down three floors. After each win you pick one of three upgrade cards, for this run only. "
               "If you fall, it's over.",
               "Descends trois étages. Après chaque victoire, choisis une carte d'amélioration parmi trois, pour "
               "cette partie seulement. Si tu tombes, c'est fini.",
               "Steige drei Etagen hinab. Nach jedem Sieg wählst du eine von drei Verbesserungskarten, nur für "
               "diesen Lauf. Wenn du fällst, ist es vorbei.",
               "Scendi tre piani. Dopo ogni vittoria scegli una di tre carte potenziamento, solo per questa "
               "partita. Se cadi, è finita.");
    case M::kJefes:
      return L("Tres jefes con armadura y golpes duros. Su armadura cae 3 segundos de vez en cuando: ¡aprovéchalo! "
               "Contra el último, aguanta.",
               "Three armored bosses that hit hard. Their armor drops for 3 seconds now and then: use it! "
               "Against the last one, survive.",
               "Trois boss blindés qui frappent fort. Leur armure tombe 3 secondes de temps en temps : profites-en ! "
               "Contre le dernier, tiens bon.",
               "Drei gepanzerte Bosse, die hart zuschlagen. Ihre Panzerung fällt ab und zu für 3 Sekunden: nutze es! "
               "Gegen den letzten musst du durchhalten.",
               "Tre boss corazzati che colpiscono duro. La loro armatura cade per 3 secondi ogni tanto: sfruttalo! "
               "Contro l'ultimo, resisti.");
    case M::kMision:
      return L("Cien misiones en 20 páginas, una por saga: combate, contrarreloj, aguantar, una regla especial y Tag Team "
               "o supervivencia. Rango S/A/B/C según la vida que te quede. Con 3 misiones superadas se abre la página siguiente.",
               "A hundred missions on 20 pages, one per saga: battle, time attack, survive, a special rule and Tag Team or "
               "survival. S/A/B/C rank by the health you have left. Clear 3 missions to open the next page.",
               "Cent missions sur 20 pages, une par saga : combat, contre la montre, tenir, une règle spéciale et Tag Team "
               "ou survie. Rang S/A/B/C selon la vie restante. Réussis 3 missions pour ouvrir la page suivante.",
               "Hundert Missionen auf 20 Seiten, eine pro Saga: Kampf, Zeitangriff, Durchhalten, eine Sonderregel und Tag "
               "Team oder Überleben. Rang S/A/B/C nach dem verbleibenden Leben. 3 geschaffte Missionen öffnen die nächste Seite.",
               "Cento missioni in 20 pagine, una per saga: scontro, contro il tempo, resistere, una regola speciale e Tag "
               "Team o sopravvivenza. Grado S/A/B/C in base alla vita rimasta. Supera 3 missioni per aprire la pagina dopo.");
    case M::kTorneo4:
      return L("Un torneo de cuatro con tres rivales de la máquina: semifinal y final. El otro combate se decide "
               "por la fuerza de cada luchador.",
               "A four-fighter tournament against three CPU rivals: semifinal and final. The other match is "
               "decided by each fighter's strength.",
               "Un tournoi à quatre contre trois adversaires de la console : demi-finale et finale. L'autre combat "
               "se décide selon la force de chaque combattant.",
               "Ein Turnier zu viert gegen drei CPU-Gegner: Halbfinale und Finale. Der andere Kampf wird nach der "
               "Stärke der Kämpfer entschieden.",
               "Un torneo a quattro contro tre avversari della CPU: semifinale e finale. L'altro incontro si decide "
               "in base alla forza di ogni lottatore.");
    default: return {};
  }
}

std::string Lang(const char* es, const char* en, const char* fr, const char* de, const char* it) {
  return L(es, en, fr, de, it);
}

// Lo que pide la mision n (bocadillo de la Mision 100).
std::string TextoMision(int n, int jugador) {
  const MisionM100 x = MisionDe(n, jugador);
  const int m = n % 5;
  std::string rivales;
  for (size_t k = 0; k < x.rivales.size(); ++k) {
    rivales += (k == 0 ? "" : k + 1 == x.rivales.size() ? Lang(" y ", " and ", " et ", " und ", " e ") : std::string(", ")) +
               M::Nombre(x.rivales[k]);
  }
  const std::string a = M::Nombre(x.rivales[0]);
  switch (m) {
    case 0: return Lang("Derrota a ", "Defeat ", "Bats ", "Besiege ", "Sconfiggi ") + a + ".";
    case 1:
      return Lang("Derrota a ", "Defeat ", "Bats ", "Besiege ", "Sconfiggi ") + a +
             Lang(" en menos de ", " in under ", " en moins de ", " in unter ", " in meno di ") + Num(x.tiempo) + " s.";
    case 2:
      return Lang("Aguanta ", "Survive ", "Tiens ", "Überlebe ", "Resisti ") + Num(x.objetivo) +
             Lang(" s contra ", " s against ", " s contre ", " s gegen ", " s contro ") + a +
             Lang(". Recibes un ", ". You take ", ". Tu subis ", ". Du erleidest ", ". Subisci il ") +
             Num(x.reglas.dano_recibido - 100) + Lang("% más de daño.", "% more damage.", "% de dégâts en plus.",
                                                       "% mehr Schaden.", "% di danni in più.");
    case 3: {
      std::string s = Lang("Derrota a ", "Defeat ", "Bats ", "Besiege ", "Sconfiggi ") + a + ": ";
      if (x.reglas.ki_drena > 0) return s + Lang("tu ki se agota poco a poco.", "your ki drains bit by bit.",
                                                  "ton ki s'épuise peu à peu.", "dein Ki schwindet nach und nach.",
                                                  "il tuo ki si esaurisce poco a poco.");
      if (x.regen > 0) return s + Lang("recupera ", "recovers ", "récupère ", "erholt ", "recupera ") + Num(x.regen) +
                              Lang(" de vida por segundo.", " health per second.", " de vie par seconde.",
                                   " Leben pro Sekunde.", " di vita al secondo.");
      if (x.reglas.armadura > 0) return s + Lang("su armadura quita un ", "his armor blocks ", "son armure bloque ",
                                                 "seine Panzerung blockt ", "la sua armatura blocca il ") +
                                        Num(x.reglas.armadura) + Lang("% de tu daño y cae cada 10 s.", "% of your damage "
                                        "and drops every 10 s.", "% de tes dégâts et tombe toutes les 10 s.",
                                        "% deines Schadens und fällt alle 10 s.", "% dei tuoi danni e cade ogni 10 s.");
      return s + Lang("es gigante y llega con tormenta.", "he is giant and brings a storm.", "il est géant et amène l'orage.",
                      "er ist riesig und bringt einen Sturm.", "è gigante e porta la tempesta.");
    }
    default:
      if (!x.equipo) return Lang("Derrota a ", "Defeat ", "Bats ", "Besiege ", "Sconfiggi ") + rivales +
                            Lang(" seguidos con la misma vida.", " in a row with the same health.",
                                 " d'affilée avec la même vie.", " nacheinander mit demselben Leben.",
                                 " di fila con la stessa vita.");
      return Lang("Tú y ", "You and ", "Toi et ", "Du und ", "Tu e ") + M::Nombre(x.aliado) +
             Lang(" contra el equipo de ", " against the team of ", " contre l'équipe de ", " gegen das Team von ",
                  " contro la squadra di ") + rivales + Lang(". SELECT: relevo.", ". SELECT: tag.", ". SELECT : relais.",
                                                            ". SELECT: Wechsel.", ". SELECT: cambio.");
  }
}

// Mision de equipo: los equipos del combate (tagteam_slots.cpp los deja mientras dura: EquiposMision).
// Modo Heroe: en el combate 3 de cada saga el maestro entra como companero de relevo (Tag Team nativo, puntos los
// del panel); en los demas combates no hay equipo.
void Equipos(const Run& r) {
  if (r.modo == M::kHeroe) {
    const int maestro = H::MaestroEnCombate(r.tecnicas, r.ganados);
    g_equipos_heroe = maestro >= 0 && maestro != r.jugador;
    if (!g_equipos_heroe) return;
    dbz3::tag::SetDynamicTeam(0, {H::Luchador(r.jugador), maestro}, false);   // studies 9: el heroe con ID propio
    dbz3::tag::SetComSide(0, false);
    REXLOG_INFO("dbz3 rpg: heroe: combate de maestro, relevo {}+{} (saga {}, combate {})", H::Luchador(r.jugador), maestro,
                r.tecnicas, r.ganados + 1);
    return;
  }
  const MisionM100 x = MisionDe(r.extra, r.jugador);
  g_equipos = x.equipo;
  if (!x.equipo) return;
  dbz3::tag::SetDynamicTeam(0, {r.jugador, x.aliado}, false);
  dbz3::tag::SetDynamicTeam(1, x.rivales, false);
  dbz3::tag::SetComSide(1, true);
  REXLOG_INFO("dbz3 rpg: mision 100 n {}: equipos {}+{} contra {} miembros", r.extra + 1, r.jugador, x.aliado,
              x.rivales.size());
}

// Heroe: el rival del combate siguiente segun el estilo y el cuerpo de ahora (heroe.cpp Mision da otro si el rival es el
// luchador del estilo, porque el registro de combate es por ID y saldria con el cuerpo del heroe, o si es el del
// cuerpo, que seria un espejo).
void RivalHeroe(const Run& r, int k) {
  int id = 0, vida = 0, esc = 0;
  H::Mision(r.tecnicas, k, r.jugador, H::CuerpoDe(r.extra, r.jugador), &id, &vida, &esc);
  M::Pelea f;
  f.rival = id;
  f.vida = vida;
  f.escenario = esc;
  GuionHeroe(r.tecnicas, k, f);   // studies 8 H6: forma / gigante del guion
  M::SetSiguiente(f);
}

// Escena de entrada del jefe de saga (cinematica.cpp: poses, planos y subtitulo; A pasa y START salta). Sin voces
// de B1: el texto es propio y va en los 5 idiomas del juego.
// studies 8 H6: es de jefe el combate con nota "jefe", "gigante" o "pierde" en sagas.toml, o el ultimo de la saga.
bool CombateDeJefe(const Run& r) {
  const std::string n = H::NotaCombate(r.tecnicas, r.ganados);
  return n == "jefe" || n == "gigante" || n == "pierde" || r.ganados == H::Combates(r.tecnicas) - 1;
}
// studies 9b: el retrato de las escenas es el del propio heroe (ID propio, su icono renderizado de su modelo); sin el, el del cuerpo
int CaraHeroe() {
  const int id = H::IdPropio();
  return id >= 0 ? id : H::Cuerpo();
}
void EscenaJefe(const Run& r, dbz3::cinematica::Escena& e) {
  namespace C = dbz3::cinematica;
  int id = 0, vida = 0, esc = 0;
  H::Mision(r.tecnicas, r.ganados, r.jugador, H::CuerpoDe(r.extra, r.jugador), &id, &vida, &esc);
  const std::string nota = H::NotaCombate(r.tecnicas, r.ganados), propia = H::TextoJefe(r.tecnicas, r.ganados);
  const int forma = H::FormaRival(r.tecnicas, r.ganados, id);
  C::Paso p;
  p.segundos = 2.5f;
  p.plano = C::kGeneral;
  e.pasos.push_back(p);
  p = C::Paso{};
  p.segundos = 4.5f;
  p.plano = C::kLado2;
  p.anim[1] = C::kPoseEntrada2;
  p.cara = id < 64 ? id : -1;   // registros de fusion (64+): sin retrato propio en la tabla de caras
  p.linea = !propia.empty() ? propia
            : nota == "gigante"
                ? std::string(T::T("¡Grrraaah! ¡El cielo tiembla!", "Grrraaah! The sky shakes!"))
                : std::string(L("Asi que tu eres el que busca las Esferas. ¡No te dejare cambiar este mundo!",
                                "So you are the one hunting the Dragon Balls. I will not let you change this world!",
                                "Alors c'est toi qui cherches les Boules de Cristal. Je ne te laisserai pas changer ce monde !",
                                "Du bist also derjenige, der die Dragon Balls sucht. Ich lasse dich diese Welt nicht verandern!",
                                "Quindi sei tu quello che cerca le Sfere del Drago. Non ti lascero cambiare questo mondo!"));
  e.pasos.push_back(p);
  p = C::Paso{};
  p.segundos = 4.5f;
  p.plano = C::kLado1;
  p.anim[0] = C::kPoseEntrada1;
  p.cara = CaraHeroe();
  p.cara_alt = H::Cuerpo();
  p.linea = L("¡Voy a devolver la paz a la Tierra, cueste lo que cueste!",
              "I will bring peace back to Earth, whatever it takes!",
              "Je ramenerai la paix sur Terre, quoi qu'il en coute !",
              "Ich bringe den Frieden zuruck auf die Erde, koste es, was es wolle!",
              "Riportero la pace sulla Terra, costi quel che costi!");
  e.pasos.push_back(p);
  REXLOG_INFO("dbz3 rpg: heroe: escena de jefe (saga {}, combate {}, rival {}, forma {}, nota '{}')", r.tecnicas,
              r.ganados + 1, id, forma, nota);
}

// Intro de la saga antes de su primer combate: subtitulos sobre el plano general. studies 8 H6: el cierre de cada
// saga ya no va aqui (salia al empezar la SIGUIENTE, y el de la ultima no salia nunca): sale en la pantalla final.
void EscenaSaga(const Run& r, dbz3::cinematica::Escena& e) {
  namespace C = dbz3::cinematica;
  const std::vector<std::string> lineas = H::SagaTextos(r.tecnicas, true);
  for (const auto& l : lineas) {
    C::Paso p;
    p.segundos = 4.5f;
    p.plano = C::kGeneral;
    p.cara = CaraHeroe();
    p.cara_alt = H::Cuerpo();
    p.linea = l;
    e.pasos.push_back(p);
  }
  REXLOG_INFO("dbz3 rpg: heroe: escena de saga {} ({} lineas)", r.tecnicas, lineas.size());
}
// Una sola escena antes del combate: la intro de la saga (combate 1) y, si es combate de jefe, su entrada.
void EscenasHeroe(const Run& r) {
  dbz3::cinematica::Escena e;
  if (r.ganados == 0) EscenaSaga(r, e);
  if (CombateDeJefe(r)) EscenaJefe(r, e);
  if (!e.pasos.empty()) dbz3::cinematica::Empezar(e);
}

// ---- Arranque de los combates ----
// Lanza el combate `r.ganados` de la partida (con g_mu). Primera vez: StartPropio; despues, Continue.
bool Lanzar(Run& r, bool primero, bool pausado = false) {
  const M::Reglas g = ReglasDe(r);
  if (r.modo == M::kMision || r.modo == M::kHeroe) Equipos(r);
  bool ok;
  if (primero) {
    if (r.modo == M::kHeroe && !pausado) EscenasHeroe(r);   // intro de la saga y/o entrada del jefe
    // studies 9: el heroe pelea como su personaje con ID propio (heroe.cpp IdPropio); si no esta montado, como el estilo
    const int luchador = r.modo == M::kHeroe ? H::Luchador(r.jugador) : r.jugador;
    const int traje_luch = luchador != r.jugador ? 0 : r.traje;
    ok = M::StartPropio(r.modo, M::NombreModo(r.modo), Palabra(r.modo), luchador, traje_luch, r.vida, r.vida_max,
                        PeleasDe(r), r.ganados, r.modo == M::kTorres || r.modo == M::kJefes || r.modo == M::kMision,
                        PremioDe(r.modo, r.extra),
                        pausado);
    if (ok) M::SetReglas(g);   // tras StartPropio: es para el combate que se acaba de pedir
    if (ok) g_fin_hecho = false;
  } else {
    M::SetVida(r.vida, r.vida_max);
    M::SetReglas(g);
    if (r.modo == M::kHeroe) EscenasHeroe(r);   // studies 8 H6: la entrada del jefe tambien en los combates 2+
    ok = M::Continue();
  }
  if (!ok && r.modo == M::kMision && g_equipos.exchange(false)) {   // no hubo combate: fuera los equipos
    dbz3::tag::ClearDynamicTeams();
    dbz3::tag::SetComSide(1, false);
  }
  REXLOG_INFO("dbz3 rpg: {} combate {}: {} (vida {}/{}, dano {}%, recibido {}%, armadura {}%, ki {}/s, {} capsulas)",
              M::NombreModo(r.modo), r.ganados + 1, !ok ? "no se pudo pedir" : pausado ? "reanudada (aun sin pedir)" : "pedido", r.vida, r.vida_max,
              g.dano_hecho, g.dano_recibido, g.armadura, g.ki_drena, g.ncaps);
  if (r.modo == M::kHeroe) {   // el estilo con el que sale de verdad el combate (el que dejo el carrusel)
    REXLOG_INFO("dbz3 rpg: heroe: estilo {} ({}) para el combate {}, cuerpo {} ({}), vida {}/{}: {}", r.jugador,
                M::Nombre(r.jugador), r.ganados + 1, H::CuerpoDe(r.extra, r.jugador),
                M::Nombre(H::CuerpoDe(r.extra, r.jugador)), r.vida, r.vida_max, ok ? "pedido" : "no se pudo pedir");
  }
  return ok;
}

uint32_t Semilla() {
  return uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ uint32_t(std::time(nullptr));
}

// Empieza (o sigue) un reto del selector (con g_mu).
void Empezar(int modo) {
  g_aviso.clear();
  // G06a: nunca sustituir silenciosamente el cuerpo CaC por el luchador del estilo.
  if (modo == M::kHeroe && g_vista == kHeroeP && H::CuerpoPropio(H::Cuerpo()) && H::IdPropio() < 0) {
    g_aviso = T::T("Confirma el cuerpo UT en Apariencia antes de jugar. Si acaba de montarse, reinicia el juego.",
                   "Confirm the UT body in Look before playing. If it was just installed, restart the game.");
    return;
  }
  if (modo <= M::kKumite) {
    if (M::Start(modo, g_jugador, g_traje)) g_vista = kCerrada;
    else g_aviso = L("No se pudo empezar el reto.", "The challenge could not start.", "Le défi n'a pas pu commencer.",
                     "Die Herausforderung konnte nicht starten.", "Impossibile avviare la sfida.");
    return;
  }
  auto it = g_runs.find(modo);
  if (modo == M::kMision && it != g_runs.end() && (it->second.extra != g_m100_sel || it->second.ganados == 0)) {
    g_runs.erase(it);   // otra mision (o la misma sin combates ganados): se empieza de cero
    it = g_runs.end();
  }
  if (modo == M::kHeroe && it != g_runs.end() && it->second.ganados == 0) {   // sin combates ganados: se vuelve a crear
    g_runs.erase(it);
    it = g_runs.end();
  }
  const bool sigue = it != g_runs.end();
  if (!sigue) {   // partida nueva
    Run r;
    r.modo = modo;
    r.jugador = g_jugador;
    r.traje = g_traje;
    r.semilla = Semilla();
    if (modo == M::kHeroe) {   // la receta de la pantalla de creacion (heroe.cpp)
      if (g_vista != kHeroeP) {   // desde el selector: primero se crea el heroe
        g_vista = kHeroeP;
        g_cursor = 0;
        return;
      }
      r.jugador = H::EstiloId();      // el combate es del luchador del estilo (su moveset)...
      {   // studies 8 fase 2 (H2): el traje extra de la ficha (colores cocidos) si esta montado; si no, el elegido
        int st = 0;
        r.traje = H::TrajeCombate(&st);
        static const char* const kSt[] = {"ficha nativa", "traje de la ficha en el combate", "hecho, se monta al reiniciar",
                                          "sin hacer o desactualizado"};
        REXLOG_INFO("dbz3 heroe: traje de combate {} ({}; elegido {})", r.traje + 1, kSt[std::clamp(st, 0, 3)],
                    H::Traje() + 1);
      }
      r.extra = 1000 + H::Cuerpo();   // ... con el modelo del cuerpo (heroe.cpp, Vestir)
      r.tecnicas = H::Saga();
      r.vida = r.vida_max = H::Vida(r.jugador);
    } else if (modo == M::kMision) {
      r.extra = g_m100_sel;
    } else if (modo == M::kTorres) {
      r.extra = g_cursor2 == 0 ? TorreDeLaSemana(SemanaUtc()) : g_cursor2 - 1;
      r.semilla = uint32_t(SemanaUtc());   // la torre queda fija hasta acabar, aunque cambie la semana
    } else if (modo == M::kTorneo4) {
      std::vector<int> pool;
      for (int id = 0; id < 44; ++id) {
        if (id != r.jugador && M::EsJugable(id) && id != 42 && id != 43) pool.push_back(id);
      }
      for (int k = 0; k < 3 && !pool.empty(); ++k) {
        const size_t i = Hash(r.semilla, uint32_t(k)) % pool.size();
        r.bots[k] = pool[i];
        pool.erase(pool.begin() + std::ptrdiff_t(i));
      }
    }
    it = g_runs.emplace(modo, r).first;
    Guardar();
    REXLOG_INFO("dbz3 rpg: {}: partida nueva (id {}, semilla {})", M::NombreModo(modo), r.jugador, r.semilla);
  }
  Run& r = it->second;
  if (sigue && r.ganados > 0 && modo != M::kSimulacion) {   // reanudar: primero su pantalla entre combates
    g_cursor = 0;
    if (Lanzar(r, true, true)) g_vista = kCerrada;
    return;
  }
  switch (modo) {
    case M::kSimulacion:
      g_vista = kSimDia;
      g_cursor = 0;
      break;
    case M::kTorneo4:
      g_vista = kCuadro;
      break;
    default:
      if (Lanzar(r, true)) g_vista = kCerrada;
      else g_aviso = L("No se pudo empezar el reto.", "The challenge could not start.", "Le défi n'a pas pu commencer.",
                       "Die Herausforderung konnte nicht starten.", "Impossibile avviare la sfida.");   // sin callejon mudo
      break;
  }
}

// ---- Resultados: lo que pasa en modos.cpp se apunta aqui (con g_mu, al volver al menu) ----
void Sincronizar() {
  Cargar();
  if (const int p = g_premio.exchange(0); p > 0) {
    g_zeni = std::min(g_zeni + p, 9999999);
    Guardar();
    REXLOG_INFO("dbz3 rpg: premio de {} Zenie (monedero {})", p, g_zeni);
  }
  const M::Estado st = M::Get();
  H::Sync(st.propio && st.tipo == M::kHeroe && (st.fase == M::kCombate || st.fase == M::kEntre));
  if (!(st.propio && st.tipo == M::kHeroe && st.fase == M::kCombate)) H::PruebaPerder(false);
  if (!st.propio) return;
  auto it = g_runs.find(st.tipo);
  if (it == g_runs.end()) return;
  Run& r = it->second;
  if (st.fase == M::kEntre && st.ganados > r.ganados) {   // victoria con mas combates por delante
    r.ganados = st.ganados;
    r.vida = st.vida;
    r.vida_max = st.vida_max;
    if (r.modo == M::kTienda) {
      const int z = kZeniVictoria + (st.recibido == 0 ? kZeniPerfecto : 0);
      r.zeni += z;
      r.eventos.clear();   // las mejoras compradas eran para ese combate
      g_aviso = Lang("¡Victoria! +", "Victory! +", "Victoire ! +", "Sieg! +", "Vittoria! +") + Num(z) + " Zenie" +
                (st.recibido == 0 ? Lang(" (perfecto)", " (perfect)", " (parfait)", " (perfekt)", " (perfetto)") : "");
    } else if (r.modo == M::kHeroe) {
      RivalHeroe(r, r.ganados);   // si se cambio de estilo, que el rival no sea el luchador del estilo
    } else if (r.modo == M::kJefes) {
      r.vida = r.vida_max;   // cada jefe con la vida llena
    } else if (r.modo == M::kAbyss) {
      r.vida = std::min(r.vida_max, r.vida + 300);
    } else if (r.modo == M::kTorneo4) {   // la otra semifinal: por la fuerza (Elo) y la semilla
      std::vector<M::Puesto> top(64);
      const int n = M::Ranking(top.data(), 64);
      auto elo = [&](int id) {
        for (int i = 0; i < n; ++i) {
          if (top[size_t(i)].id == id) return double(top[size_t(i)].elo);
        }
        return 1000.0;
      };
      const double p2 = 1.0 / (1.0 + std::pow(10.0, (elo(r.bots[2]) - elo(r.bots[1])) / 400.0));
      r.finalista = (Hash(r.semilla, 77) % 1000) < uint32_t(p2 * 1000) ? r.bots[1] : r.bots[2];
      M::Pelea f;
      f.rival = r.finalista;
      f.vida = 3000;
      M::SetSiguiente(f);
    }
    Guardar();
    REXLOG_INFO("dbz3 rpg: {}: combate {} ganado (vida {}/{}, dano recibido {}, zeni {})", M::NombreModo(r.modo),
                r.ganados, r.vida, r.vida_max, st.recibido, r.zeni);
  } else if (st.fase == M::kFin && !g_fin && !g_fin_hecho) {   // fin del reto: su pantalla, y la partida se cierra (o se guarda)
    g_fin = true;
    g_fin_st = st;
    g_fin_run = r;
    g_cursor = 0;
    g_fin_pagina = 1;   // studies 8 H6: la pagina del cierre solo para el heroe con saga completada
    g_fin_cierre.clear();
    g_fin_cierre_dicho = false;
    if (r.modo == M::kMision) {   // los equipos eran de esta mision; el rango, el mejor de cada una
      g_equipos = false;
      dbz3::tag::ClearDynamicTeams();
      dbz3::tag::SetComSide(1, false);
      g_m100_ultimo = '-';
      if (st.resultado == M::kVictoria) {
        static const std::string kOrden = "-CBASZ";
        g_m100_ultimo = RangoDe(st.vida, st.vida_max, st.recibido);
        char& mejor = g_m100[size_t(std::clamp(r.extra, 0, kNumMisiones - 1))];
        if (kOrden.find(g_m100_ultimo) > kOrden.find(mejor)) mejor = g_m100_ultimo;
        dbz3::pasarela::RangoMejora("rango_m100_" + Num(r.extra + 1), int(kOrden.find(mejor)), 10, 10);
        REXLOG_INFO("dbz3 rpg: mision 100 n {}: rango {} (vida {}/{}), mejor {}", r.extra + 1, g_m100_ultimo, st.vida,
                    st.vida_max, mejor);
      }
    }
    if (st.resultado == M::kGuardado || st.resultado == M::kAbandono) {   // se sigue la proxima vez
      r.ganados = st.ganados;
    } else {
      if (r.modo == M::kTienda) g_tienda_record = std::max(g_tienda_record, r.zeni);
      if (r.modo == M::kHeroe) {   // nivel y saga siguiente
        bool victoria = st.resultado == M::kVictoria;
        g_fin_cierre.clear();
        g_fin_pierde = false;
        // studies 8 H6: nota "pierde" (Omega, studies 6 heroe_sagas.json): la pelea "parece imposible" y la derrota es
        // del guion: cuenta como saga completada
        if (!victoria && st.resultado == M::kDerrota && H::NotaCombate(r.tecnicas, st.ganados) == "pierde") {
          victoria = g_fin_pierde = true;
          g_fin_st.resultado = M::kVictoria;
          REXLOG_INFO("dbz3 rpg: heroe: combate 'pierde' (saga {}, combate {}): la derrota es del guion, saga completada",
                      r.tecnicas, st.ganados + 1);
          H::FinRun(st.ganados + 1, true);
        } else {
          H::FinRun(st.ganados, victoria);
        }
        if (victoria) {   // cierre de la saga (y el epilogo tras la ultima) en la pantalla final
          g_fin_cierre = H::SagaTextos(r.tecnicas, false);
          g_fin_epilogo = H::EsUltimaSaga(r.tecnicas);
          if (g_fin_epilogo)
            for (const std::string& l : H::Epilogo()) g_fin_cierre.push_back(l);
          g_fin_pagina = g_fin_cierre.empty() ? 1 : 0;
          g_fin_saga = r.tecnicas;
        }
      }
      if (r.modo == M::kTorneo4) {   // campeon (extra): el jugador, o el resto del cuadro por la fuerza
        if (r.finalista < 0) r.finalista = (Hash(r.semilla, 77) & 1) ? r.bots[1] : r.bots[2];
        r.extra = st.resultado == M::kVictoria ? r.jugador
                  : st.ganados == 0           ? ((Hash(r.semilla, 78) & 1) ? r.bots[0] : r.finalista)
                                              : r.finalista;
        g_fin_run = r;
      }
      g_runs.erase(it);
    }
    Guardar();
    REXLOG_INFO("dbz3 rpg: fin de {} ({}; {} de {})", M::NombreModo(st.tipo), st.resultado, st.ganados, st.total);
  }
}

// ---- Dibujo ----
// Todas las pantallas siguen el esquema comun (game_style.h, S::mode): columna a la derecha del anfitrion, marcos
// separados kGap, letra kRow. Lista y Lineas devuelven el borde de abajo de su marco (para apilar el siguiente).
namespace D = S::mode;
float Lista(ImDrawList* dl, const S::Area& a, float y0, const std::vector<std::string>& filas, int cursor, float t,
            const std::vector<bool>& apagadas = {}, float letra = D::kRow, float x0 = D::kX0, float x1 = D::kX1,
            float paso = 52) {
  // filas desde kPad (la franja no pisa la linea del marco, como el Duelo)
  const float y1 = y0 + 2 * D::kPad + paso * float(filas.size());
  S::Frame(dl, a.At(x0, y0), a.At(x1, y1), a.s);
  for (size_t i = 0; i < filas.size(); ++i) {
    const float y = y0 + D::kPad + paso * (float(i) + 0.5f);
    const bool sel = int(i) == cursor, off = i < apagadas.size() && apagadas[i];
    if (sel) D::Stripe(dl, a, x0, y - paso * 0.5f + 2, x1, y + paso * 0.5f - 2, t);
    S::MenuText(dl, a.At((x0 + x1) * 0.5f, y), letra * a.s, filas[i].c_str(), sel, off, (x1 - x0 - 2 * D::kText) * a.s);
  }
  return y1;
}
// "Fuerza: +25% de dano" -> "Fuerza": el nombre corto para la lista; el texto entero va al bocadillo.
std::string Corto(const std::string& s) {
  std::string c = s.substr(0, s.find(':'));
  while (!c.empty() && c.back() == ' ') c.pop_back();
  return c;
}
// Lista de tienda como la Skill Shop del juego: botella de capsula (roja, verde, amarilla), nombre y precio; las filas
// sin precio (seguir) sin botella.
float ListaTienda(ImDrawList* dl, const S::Area& a, float y0, const std::vector<std::string>& nombres,
                  const std::vector<int>& precios, int cursor, float t, const std::vector<bool>& apagadas) {
  const float x0 = D::kX0, x1 = D::kX1, paso = 58, y1 = y0 + 2 * D::kPad + paso * float(nombres.size());
  S::Frame(dl, a.At(x0, y0), a.At(x1, y1), a.s);
  const S::Pic cap = S::Img("capsulas");
  for (size_t i = 0; i < nombres.size(); ++i) {
    const float y = y0 + D::kPad + paso * (float(i) + 0.5f);
    const bool sel = int(i) == cursor, off = i < apagadas.size() && apagadas[i];
    const bool precio = i < precios.size() && precios[i] > 0;
    if (sel) D::Stripe(dl, a, x0, y - paso * 0.5f + 2, x1, y + paso * 0.5f - 2, t);
    if (cap && precio) {   // botella al alto de la fila (en la Skill Shop ocupa casi toda la fila)
      const float bx = 3 + 36 * float(i % 3), h = 52 * a.s, w = h * 31 / 83;
      const ImVec2 c = a.At(x0 + 70, y);
      S::DrawPic(dl, cap, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f),
                 ImVec4(bx, 7, bx + 31, 90), off ? IM_COL32(255, 255, 255, 110) : IM_COL32_WHITE);
    }
    if (precio) {
      S::MenuText(dl, a.At((x0 + 110 + x1 - 170) * 0.5f, y), D::kRow * a.s, nombres[i].c_str(), sel, off,
                  (x1 - 170 - x0 - 110) * a.s);
      S::MenuText(dl, a.At(x1 - 90, y), D::kRow * a.s, (Num(precios[i]) + " Z").c_str(), sel, off);
    } else {
      S::MenuText(dl, a.At((x0 + x1) * 0.5f, y), D::kRow * a.s, nombres[i].c_str(), sel, off, (x1 - x0 - 40) * a.s);
    }
  }
  return y1;
}
// Caja del dinero de la Skill Shop arriba a la derecha (con las cifras naranjas del juego) y el rotulo ZENIE a su
// izquierda, sin pisarla.
void CajaZenie(ImDrawList* dl, const S::Area& a, int zeni) {
  const float k = 0.8f, bw = 322 * k, bh = 102 * k;
  const ImVec2 p0 = a.At(D::kX1 - bw, D::kCornerY0), p1 = a.At(D::kX1, D::kCornerY0 + bh);
  if (const S::Pic box = S::Img("caja_zenie")) S::DrawPic(dl, box, p0, p1, ImVec4(1, 3, 323, 105));
  else S::Frame(dl, p0, p1, a.s);
  if (const S::Pic z = S::Img("letras_zenie")) {
    const float zw = 150 * k * a.s, zh = 49 * k * a.s, cy = (p0.y + p1.y) * 0.5f;
    S::DrawPic(dl, z, ImVec2(p0.x - zw - 8 * a.s, cy - zh * 0.5f), ImVec2(p0.x - 8 * a.s, cy + zh * 0.5f),
               ImVec4(2, 6, 152, 55));
  }
  // cifras de data_usi 2053 (0-9 en una fila, de y 10 a 127), alineadas a la derecha de la caja
  static constexpr float kX[10][2] = {{4, 90},    {107, 166}, {196, 282}, {293, 372}, {387, 470},
                                      {489, 568}, {580, 660}, {682, 765}, {772, 863}, {870, 959}};
  const std::string n = Num(std::max(zeni, 0));
  const S::Pic d = S::Img("cifras_zenie");
  if (!d) {
    S::MenuText(dl, ImVec2(p0.x + (p1.x - p0.x) * 0.63f, (p0.y + p1.y) * 0.5f), 40 * a.s, n.c_str(), true, false);
    return;
  }
  const float h = (p1.y - p0.y) * 0.5f, kk = h / 112.0f, cy = (p0.y + p1.y) * 0.5f;
  float w = 0;
  for (const char c : n) w += (kX[c - '0'][1] - kX[c - '0'][0]) * kk - 4 * kk;
  float x = p1.x - (p1.x - p0.x) * 0.1f - w;
  for (const char c : n) {
    const float* g = kX[c - '0'];
    const float gw = (g[1] - g[0]) * kk;
    S::DrawPic(dl, d, ImVec2(x, cy - h * 0.5f), ImVec2(x + gw, cy + h * 0.5f), ImVec4(g[0], 10, g[1], 122));
    x += gw - 4 * kk;
  }
}
float Lineas(ImDrawList* dl, const S::Area& a, float y0, const std::vector<std::string>& l) {
  // sin franja: el texto empieza en el filo de la linea (kPad - 4)
  const float pad = D::kPad - 4, paso = 44, y1 = y0 + 2 * pad + paso * float(l.size());
  S::Frame(dl, a.At(D::kX0, y0), a.At(D::kX1, y1), a.s);
  for (size_t i = 0; i < l.size(); ++i) {
    S::MenuText(dl, a.At(D::kCx, y0 + pad + paso * (float(i) + 0.5f)), D::kRow * a.s, l[i].c_str(), i == 0, false,
                (D::kX1 - D::kX0 - 2 * D::kText) * a.s);
  }
  return y1;
}
void Bocadillo(ImDrawList* dl, const S::Area& a, const std::string& s) { D::Bubble(dl, a, s.c_str()); }
void Cara(ImDrawList* dl, const S::Area& a, int id, float x, float y, float h, int alfa = 255) {
  if (const S::Pic p = S::Img(("caras/" + Num(id)).c_str())) {
    const float w = h * p.w / std::max(p.h, 1.0f);
    S::DrawPic(dl, p, a.At(x - w * 0.5f, y - h * 0.5f), a.At(x + w * 0.5f, y + h * 0.5f), ImVec4(0, 0, 0, 0),
               IM_COL32(255, 255, 255, alfa));
  }
}
// Estrella de maestro (la letra del juego no la tiene): pentagono de dentro y cinco puntas, dorada con perfil oscuro.
void Estrella(ImDrawList* dl, ImVec2 c, float r) {
  for (const auto& [rad, col] : {std::pair<float, ImU32>{r + 2.0f, IM_COL32(90, 40, 5, 255)},
                                 std::pair<float, ImU32>{r, IM_COL32(255, 205, 40, 255)}}) {
    ImVec2 in[5], out[5];
    for (int i = 0; i < 5; ++i) {
      const float ang = -1.5708f + 1.2566f * float(i), ang2 = ang + 0.6283f;   // punta y valle (36 grados mas)
      out[i] = ImVec2(c.x + std::cos(ang) * rad, c.y + std::sin(ang) * rad);
      in[i] = ImVec2(c.x + std::cos(ang2) * rad * 0.42f, c.y + std::sin(ang2) * rad * 0.42f);
    }
    ImVec2 pent[5];
    for (int i = 0; i < 5; ++i) pent[i] = in[(i + 4) % 5];   // valles en orden de giro
    dl->AddConvexPolyFilled(pent, 5, col);
    for (int i = 0; i < 5; ++i) dl->AddTriangleFilled(in[(i + 4) % 5], out[i], in[i], col);
  }
}
// Carrusel de estilos del Modo Heroe (heroe.cpp): cinco caras con su nombre, la de en medio es el estilo elegido (franja
// azul, cara grande, nombre brillante). Los maestros llevan estrella. `activo`: salen las flechas del juego. LB/RB
// mueven el carrusel (en la creacion tambien izquierda/derecha sobre la fila Estilo); y1 - y0 >= 120.
void CarruselEstilos(ImDrawList* dl, const S::Area& a, float y0, float y1, float t, bool activo) {
  S::Frame(dl, a.At(D::kX0, y0), a.At(D::kX1, y1), a.s);
  const float paso = 120, hc = std::min(64.0f, (y1 - y0) - 66.0f), hl = hc * 0.72f;
  const float yf = y0 + D::kLine + 4 + hc * 0.5f, yn = y0 + D::kLine + 8 + hc + 11;   // caras y nombres
  S::Highlight(dl, a.At(D::kCx - paso * 0.5f + 4, y0 + D::kLine + 1), a.At(D::kCx + paso * 0.5f - 4, y1 - D::kLine - 1), t);
  for (int i = -2; i <= 2; ++i) {
    const int id = H::EstiloVecino(i);
    const float x = D::kCx + paso * float(i), h = i == 0 ? hc : hl;
    Cara(dl, a, id, x, yf, h, i == 0 ? 255 : (i == -1 || i == 1 ? 170 : 110));
    if (H::EsMaestro(id)) {
      const S::Pic p = S::Img(("caras/" + Num(id)).c_str());
      const float w = p ? h * p.w / std::max(p.h, 1.0f) : h;
      Estrella(dl, a.At(x + w * 0.5f - 3, yf - h * 0.5f + 5), (i == 0 ? 9.0f : 7.0f) * a.s);
    }
    S::MenuText(dl, a.At(x, yn), (i == 0 ? 24.0f : 18.0f) * a.s, M::Nombre(id).c_str(), i == 0, i != 0, (paso - 8) * a.s);
  }
  if (activo) S::Arrows(dl, a.At(D::kX0 - 10, y0), a.At(D::kX1 + 10, y1), 36 * a.s, t);
}
const char* Aceptar() { return L("Aceptar", "OK", "Valider", "OK", "Conferma"); }
const char* Atras() { return L("Atrás", "Back", "Retour", "Zurück", "Indietro"); }
std::string Vida(int v, int m) { return Lang("Vida ", "Health ", "Vie ", "Leben ", "Vita ") + Num(v) + " / " + Num(m); }

// Siguiente luchador jugable (dir +1/-1) para el selector.
int Siguiente(int id, int dir) {
  for (int k = 0; k < 64; ++k) {
    id = (id + dir + 64) % 64;
    if (M::EsJugable(id)) return id;
  }
  return id;
}

void DibujarSelector(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const std::vector<int>& lista = Retos(g_entrada);
  const int n = int(lista.size());
  g_sel = std::clamp(g_sel, 0, n - 1);
  if (p & (kUp | kDown)) g_aviso.clear();   // el aviso de un reto que no arranco no se queda pegado a los demas
  if (p & kUp) g_sel = (g_sel + n - 1) % n;
  if (p & kDown) g_sel = (g_sel + 1) % n;
  const int modo = lista[size_t(g_sel)];
  const bool torres = modo == M::kTorres;
  if (torres && g_runs.find(modo) == g_runs.end()) {   // LB/RB: torre de la semana o una fija
    if (p & kLb) g_cursor2 = (g_cursor2 + 2) % 3;
    if (p & kRb) g_cursor2 = (g_cursor2 + 1) % 3;
  }
  if (modo != M::kHeroe) {   // izquierda/derecha: el luchador (el heroe es el suyo)
    if (p & kLeft) g_jugador = Siguiente(g_jugador, -1), g_traje = 0;
    if (p & kRight) g_jugador = Siguiente(g_jugador, +1), g_traje = 0;
  }   // heroe: se crea en su pantalla (kHeroeP) al pulsar A
  if (p & kB) {
    Guardar();
    g_vista = kCerrada;
    return;
  }
  if (p & kA) {
    Guardar();   // la receta del heroe
    Empezar(modo);
    if (g_vista != kSelector) return;
  }
  const char* titulos[3] = {M::NombreModo(M::kTorre), M::NombreModo(M::kSupervivencia), M::NombreModo(M::kKumite)};
  S::Banner(dl, a, titulos[std::clamp(g_entrada, 0, 2)]);
  // arriba: el luchador (o la variante del heroe), con las flechas del juego
  // (alto 84: la cara del marcador, 50 px, cabe dentro de la linea del marco)
  constexpr float kSelH = 84;
  const ImVec2 g0 = a.At(D::kX0 + 60, D::kTop), g1 = a.At(D::kX1 - 60, D::kTop + kSelH);
  S::Frame(dl, g0, g1, a.s);
  std::string quien;
  const int heroe = g_runs.count(M::kHeroe) ? H::CuerpoDe(g_runs[M::kHeroe].extra, g_runs[M::kHeroe].jugador)
                                            : H::Cuerpo();   // el de la partida o el creado
  if (modo == M::kHeroe) {
    quien = Lang("Héroe: ", "Hero: ", "Héros : ", "Held: ", "Eroe: ") + M::Nombre(heroe) + "  -  " + H::LineaNivel(0);
  } else {
    quien = M::Nombre(g_jugador);
  }
  Cara(dl, a, modo == M::kHeroe ? heroe : g_jugador, D::kX0 + 60 + D::kLine + 40, D::kTop + kSelH * 0.5f, 50);
  S::MenuText(dl, ImVec2((g0.x + g1.x) * 0.5f + 30 * a.s, (g0.y + g1.y) * 0.5f), D::kHead * a.s, quien.c_str(), true,
              false, g1.x - g0.x - 160 * a.s);
  S::Arrows(dl, ImVec2(g0.x - 18 * a.s, g0.y), ImVec2(g1.x + 18 * a.s, g1.y), 40 * a.s, t);
  // la lista de retos
  std::vector<std::string> filas;
  for (const int m : lista) {
    std::string f = M::NombreModo(m);
    int gid = 0, gg = 0;
    if (g_runs.count(m) || (m <= M::kKumite && M::Guardado(m, &gid, &gg))) f += Lang("  (a medias)", "  (in progress)", "  (en cours)", "  (läuft)", "  (in corso)");
    filas.push_back(f);
  }
  Lista(dl, a, D::kTop + kSelH + D::kGap, filas, g_sel, t);
  // bocadillo: que es, record y Zenie (y la torre / las tecnicas)
  std::string s = Descripcion(modo);
  if (torres) {
    const int sem = SemanaUtc(), tw = TorreDeLaSemana(sem);
    const int elegida = g_runs.count(modo) ? g_runs[modo].extra : g_cursor2 == 0 ? tw : g_cursor2 - 1;
    const Torre& tr = kTorres[elegida];
    s += "\n" + Lang("LB/RB: ", "LB/RB: ", "LB/RB : ", "LB/RB: ", "LB/RB: ") +
         (g_cursor2 == 0 && !g_runs.count(modo) ? Lang("de la semana: ", "weekly: ", "de la semaine : ", "Wochenturm: ",
                                                        "della settimana: ")
                                                : std::string()) +
         Tr(tr.es, tr.en) + std::string(" (") + L(tr.mod_es, tr.mod_en, tr.mod_fr, tr.mod_de, tr.mod_it) + ")";
  } else if (modo == M::kHeroe && g_runs.count(modo) && g_runs[modo].ganados > 0) {   // a medias: su estilo y su saga
    const Run& hr = g_runs[modo];   // en lugar de la descripcion (que ya se leyo): cabe todo con letra grande
    s = Lang("Estilo: ", "Style: ", "Style : ", "Stil: ", "Stile: ") + M::Nombre(hr.jugador) + "  -  " +
        H::NombreSaga(hr.tecnicas) + " " + Num(hr.ganados + 1) + "/" + Num(H::Combates(hr.tecnicas)) + "\n" +
        H::LineaNivel(hr.ganados);
  }
  int id = 0, mejor = 0, total = 0, comp = 0;
  if (M::Record(modo, &id, &mejor, &total, &comp)) {
    s += "\n" + Lang("Récord: ", "Record: ", "Record : ", "Rekord: ", "Record: ") + Num(mejor) + "/" + Num(total) + " (" +
         M::Nombre(id) + ")" + (comp > 0 ? "  x" + Num(comp) : "");
  }
  if (modo == M::kTienda && g_tienda_record > 0) {
    s += Lang("  Zenie: ", "  Zenie: ", "  Zenie : ", "  Zenie: ", "  Zenie: ") + Num(g_tienda_record);
  }
  s += "\n" + Lang("Monedero: ", "Wallet: ", "Porte-monnaie : ", "Geldbörse: ", "Portafoglio: ") + Num(g_zeni) + " Zenie";
  if (!g_rango.empty()) s += "   " + g_rango;
  if (g_solo_lectura) {
    s += "\n" + Lang("(rpg.toml es de una versión más nueva: no se guarda)", "(rpg.toml is from a newer version: not saved)",
                     "(rpg.toml vient d'une version plus récente : non enregistré)",
                     "(rpg.toml stammt aus einer neueren Version: wird nicht gespeichert)",
                     "(rpg.toml è di una versione più recente: non viene salvato)");
  }
  if (!g_aviso.empty()) s = g_aviso;
  Bocadillo(dl, a, s);
  D::Help(dl, a, {{"A", modo == M::kHeroe ? (g_runs.count(modo) && g_runs[modo].ganados > 0 ? T::T("Continuar", "Continue") : T::T("Crear héroe", "Create hero"))
                                          : L("Empezar", "Start", "Commencer", "Starten", "Inizia")},
                  {"B", Atras()}});
}

// Mision 100: LB/RB la pagina, arriba/abajo la mision, izquierda/derecha el luchador, A empieza, B sale.
void DibujarM100(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const int antes = g_pagina * 5 + g_cursor;
  if (p & kLb) g_pagina = (g_pagina + kNumPaginas - 1) % kNumPaginas;
  if (p & kRb) g_pagina = (g_pagina + 1) % kNumPaginas;
  if (p & kUp) g_cursor = (g_cursor + 4) % 5;
  if (p & kDown) g_cursor = (g_cursor + 1) % 5;
  g_cursor = std::clamp(g_cursor, 0, 4);
  if (p & kLeft) g_jugador = Siguiente(g_jugador, -1), g_traje = 0;
  if (p & kRight) g_jugador = Siguiente(g_jugador, +1), g_traje = 0;
  const int n = g_pagina * 5 + g_cursor;
  if (n != antes) g_aviso.clear();
  if (p & kB) {
    Guardar();
    g_vista = kCerrada;
    return;
  }
  const bool abierta = PaginaAbierta(g_pagina);
  if (p & kA) {
    if (!abierta) {
      g_aviso = Lang("Supera 3 misiones de la página anterior para abrir esta.",
                     "Clear 3 missions of the previous page to open this one.",
                     "Réussis 3 missions de la page précédente pour ouvrir celle-ci.",
                     "Schaffe 3 Missionen der vorigen Seite, um diese zu öffnen.",
                     "Supera 3 missioni della pagina precedente per aprire questa.");
    } else {
      g_m100_sel = n;
      Empezar(M::kMision);
      if (g_vista != kMisionP) return;
    }
  }
  S::Banner(dl, a, M::NombreModo(M::kMision));
  // arriba a la derecha: la pagina (LB/RB) con las flechas del juego
  constexpr float c0 = D::kCornerY0, c1 = D::kTop - 24;
  const ImVec2 q0 = a.At(D::kCornerX0, c0), q1 = a.At(D::kX1, c1);
  S::Frame(dl, q0, q1, a.s);
  const std::string pag = Lang("Página ", "Page ", "Page ", "Seite ", "Pagina ") + Num(g_pagina + 1) + "/" +
                          Num(kNumPaginas) + "  " + M::Nombre(kPaginas[g_pagina].jefe);
  S::MenuText(dl, ImVec2((q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f), D::kRow * a.s, pag.c_str(), true, !abierta,
              q1.x - q0.x - 80 * a.s);
  S::Arrows(dl, ImVec2(q0.x - 18 * a.s, q0.y), ImVec2(q1.x + 18 * a.s, q1.y), 36 * a.s, t);
  // el luchador (como el selector de retos)
  constexpr float kSelH = 84;
  const ImVec2 g0 = a.At(D::kX0 + 60, D::kTop), g1 = a.At(D::kX1 - 60, D::kTop + kSelH);
  S::Frame(dl, g0, g1, a.s);
  Cara(dl, a, g_jugador, D::kX0 + 60 + D::kLine + 40, D::kTop + kSelH * 0.5f, 50);
  float izq = 0, der = 0;   // caras de la mision: aliado junto al jugador; rivales a la derecha (la ficha de T3)
  if (abierta) {
    const MisionM100 x = MisionDe(n, g_jugador);
    if (x.aliado >= 0) Cara(dl, a, x.aliado, D::kX0 + 60 + D::kLine + 96, D::kTop + kSelH * 0.5f, 50), izq = 56;
    for (size_t k = 0; k < x.rivales.size(); ++k)
      Cara(dl, a, x.rivales[x.rivales.size() - 1 - k], D::kX1 - 60 - D::kLine - 52 - 56 * float(k), D::kTop + kSelH * 0.5f, 50);
    der = 56 * float(x.rivales.size()) + 30;
    S::MenuText(dl, a.At(D::kX1 - 60 - D::kLine - 52 - der + 26, D::kTop + kSelH * 0.5f), D::kSmall * a.s, "VS", false, true);
  }
  S::MenuText(dl, ImVec2((g0.x + g1.x) * 0.5f + (30 + izq - der) * 0.5f * a.s, (g0.y + g1.y) * 0.5f), D::kHead * a.s,
              M::Nombre(g_jugador).c_str(), true, false, g1.x - g0.x - (160 + izq + der) * a.s);
  S::Arrows(dl, ImVec2(g0.x - 18 * a.s, g0.y), ImVec2(g1.x + 18 * a.s, g1.y), 40 * a.s, t);
  // las 5 misiones de la pagina con su mejor rango
  std::vector<std::string> filas;
  std::vector<bool> off;
  for (int k = 0; k < 5; ++k) {
    const int m = g_pagina * 5 + k;
    const char rg = g_m100[size_t(m)];
    filas.push_back(abierta ? Num(m + 1) + ". " + TituloMision(m) + (rg != '-' ? std::string("   [") + rg + "]" : "")
                            : Num(m + 1) + ". ???");
    off.push_back(!abierta);
  }
  Lista(dl, a, D::kTop + kSelH + 30, filas, g_cursor, t, off);
  int hechas = 0, eses = 0, zetas = 0;
  for (const char c : g_m100) hechas += c != '-', eses += c == 'S' || c == 'Z', zetas += c == 'Z';
  std::string b = abierta ? std::string(NombreMision(n)) + ": " + TextoMision(n, g_jugador) + "\n" +
                                Lang("Premio: ", "Prize: ", "Prix : ", "Preis: ", "Premio: ") +
                                Num(PremioDe(M::kMision, n)) + " Zenie"
                          : Lang("Página cerrada: supera 3 misiones de la anterior.",
                                 "Page locked: clear 3 missions of the previous one.",
                                 "Page fermée : réussis 3 missions de la précédente.",
                                 "Seite gesperrt: schaffe 3 Missionen der vorigen.",
                                 "Pagina chiusa: supera 3 missioni della precedente.");
  b += "\n" + Lang("Superadas ", "Cleared ", "Réussies ", "Geschafft ", "Superate ") + Num(hechas) + "/" + Num(kNumMisiones) +
       Lang("   Rango S ", "   S rank ", "   Rang S ", "   Rang S ", "   Grado S ") + Num(eses) + "   Z " + Num(zetas) +
       "   LB/RB: " + Lang("página", "page", "page", "Seite", "pagina");
  if (!g_aviso.empty()) b = g_aviso;
  Bocadillo(dl, a, b);
  D::Help(dl, a, {{"A", L("Empezar", "Start", "Commencer", "Starten", "Inizia")}, {"B", Atras()}});
}

// Modo Heroe (beta): creacion del heroe (heroe.cpp). Arriba/abajo la fila, izquierda/derecha la cambia, LB/RB cambian
// el estilo (carrusel de abajo) desde cualquier fila, A en "Empezar" lanza la mision con ese cuerpo, traje y estilo
// (bonus y tecnicas si es maestro); B vuelve al selector. Frente 10: la fila Moveset elige el donante de los golpes.
// Fichas de heroe: Y pasa a la siguiente; X crea una nueva (heroe base). Cada cambio guarda antes el heroe de la
// pantalla en su ficha (Guardar la exporta) y despues carga la del destino.
// studies 8 H1: con un combate de la saga a medias (partida del heroe con combates ganados) no se cambia de ficha:
// la partida es de la ficha activa y su progreso se apunta en ella al acabar.
bool FichaBloqueada() {
  const auto it = g_runs.find(M::kHeroe);
  if (it == g_runs.end() || it->second.ganados <= 0) return false;
  g_aviso = T::T("Tienes una saga a medias con este héroe: termínala antes de cambiar de ficha.",
                 "You have a saga in progress with this hero: finish it before switching slots.");
  REXLOG_INFO("dbz3 heroe: cambio de ficha bloqueado (saga a medias, combate {})", it->second.ganados + 1);
  return true;
}
void CambiarFicha(int d) {
  const int n = dbz3::cac::NumSlots();
  if (FichaBloqueada()) return;
  Guardar();
  dbz3::cac::ElegirSlot((dbz3::cac::SlotActivo() + d + n) % n);
  H::Importar(dbz3::cac::SlotActivo());
  Guardar();
}
void NuevaFicha() {
  if (dbz3::cac::NumSlots() >= dbz3::cac::kMaxSlots) {
    g_aviso = T::T("Ya tienes 3 héroes: archiva uno en la lista de héroes.", "You already have 3 heroes: archive one in the hero list.");
    return;
  }
  if (FichaBloqueada()) return;
  Guardar();
  H::Importar(dbz3::cac::NuevoSlot());
  Guardar();
}

void DibujarHeroe(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  constexpr int kFilas = 9;   // raza, cuerpo, apariencia, estilo, moveset, aura, puntos (PH), maestros, empezar
  g_cursor = std::clamp(g_cursor, 0, kFilas + 1);   // C4: 0..8, la 9 (color del ki, en Combate) y la 10 (piezas B3/XV2, en Apariencia)
  if (p & (kUp | kDown | kLeft | kRight | kLb | kRb)) g_aviso.clear();
  static int g_tab_h = 0;   // C4: pestana (0 Apariencia, 1 Combate, 2 Progreso); "Empezar" (fila 8) es fijo en las tres
  auto filas_tab = [&]() {
    std::vector<int> v;
    for (int f = g_tab_h * 3; f < std::min(g_tab_h * 3 + 3, kFilas - 1); ++f) v.push_back(f);
    if (g_tab_h == 0) v.push_back(kFilas + 1);   // fila 10: piezas Budokai 3 / Xenoverse 2
    if (g_tab_h == 1) v.push_back(kFilas);   // fila 9: color del ki
    v.push_back(kFilas - 1);
    return v;
  };
  {
    std::vector<int> v = filas_tab();
    if (std::find(v.begin(), v.end(), g_cursor) == v.end()) {
      g_tab_h = g_cursor == kFilas + 1 ? 0 : std::clamp(g_cursor / 3, 0, 2);
      v = filas_tab();
    }
    int i = int(std::find(v.begin(), v.end(), g_cursor) - v.begin());
    const int nv = int(v.size());
    if (p & kUp) i = (i + nv - 1) % nv;
    if (p & kDown) i = (i + 1) % nv;
    g_cursor = v[size_t(i)];
  }
  if ((p & kRight) && g_cursor == kFilas - 1) {   // studies 8 fase 2 (H5): Derecha en "Empezar" abre el mapa
    dbz3::heroe_ui::AbrirMapa();
    g_vista = kHeroeMapa;
    return;
  }
  static bool colores_cambiados = false;   // C4: aura / ki de color: el heroe propio se remonta al salir de la pantalla
  if (p & (kLeft | kRight)) colores_cambiados = colores_cambiados || g_cursor == 5 || g_cursor == 9 || g_cursor == kFilas + 1;
  if (p & kLeft) H::Mover(g_cursor, -1);
  if (p & kRight) H::Mover(g_cursor, +1);
  if (p & (kLb | kRb)) {   // C4: LB/RB cambian de pestana (Apariencia / Combate / Progreso)
    g_tab_h = (g_tab_h + ((p & kRb) ? 1 : 2)) % 3;
    g_cursor = g_tab_h * 3;
  }
  if (H::CambioBloqueado()) g_aviso = T::T("Para cambiar de maestro, completa al menos el 60% del entrenamiento con el actual.",
                                          "To switch master, complete at least 60% of training with the current one.");
  if (p & kY) CambiarFicha(+1);   // ficha de heroe (cac.toml): Y la siguiente, X una nueva
  if (p & kX) NuevaFicha();
  if (p & kStart) {   // K: lista de fichas (elegir / duplicar / archivar) sin salir del modo
    dbz3::heroe_ui::AbrirSlots();
    g_vista = kHeroeLista;
    return;
  }
  if ((p & kB) && colores_cambiados) {
    colores_cambiados = false;
    dbz3::heroe_ui::PedirCombate();
  }
  if (p & kB) {
    Guardar();
    g_vista = g_entrada == 4 ? kCerrada : kSelector;   // entrada propia del HUB: B vuelve al HUB
    return;
  }
  if (p & kA) {
    if (g_cursor == 2) {   // studies 8 H3: Apariencia -> pantalla Create (UT); Y dentro pasa a Edit (pelo)
      dbz3::heroe_ui::AbrirCrear(false);
      g_vista = kHeroeCrear;
      return;
    }
    if (g_cursor == 4) {   // studies 8 fase 2: Moveset -> pantalla con la lista de movesets
      dbz3::heroe_ui::AbrirMovesets();
      g_vista = kHeroeMoveset;
      return;
    }
    if (g_cursor == 7) {   // studies 8 H4: Maestros -> Select Master (UT)
      dbz3::heroe_ui::AbrirMaestros();
      g_vista = kHeroeMaestro;
      return;
    }
    if (g_cursor != kFilas - 1) {
      const std::vector<int> v = filas_tab();
      const auto it = std::find(v.begin(), v.end(), g_cursor);
      if (it != v.end() && it + 1 != v.end()) g_cursor = *(it + 1);
    } else {
      if (colores_cambiados) {
        colores_cambiados = false;
        dbz3::heroe_ui::PedirCombate();
      }
      Guardar();   // la receta
      Empezar(M::kHeroe);
      if (g_vista != kHeroeP) return;
    }
  }
  const std::string ficha = std::string(T::T("Ficha ", "Slot ")) + std::to_string(dbz3::cac::SlotActivo() + 1) +
                            "/" + std::to_string(dbz3::cac::NumSlots());
  std::vector<std::string> filas;   // C4: la pantalla la dibuja heroe_ui::Principal (antes: lista centrada + Kaio + carrusel)
  for (int f = 0; f <= kFilas + 1; ++f) filas.push_back(H::Fila(f));
  std::string b = g_aviso.empty() ? (g_cursor == 7 ? H::DetalleMaestros() : g_cursor == kFilas + 1 ? std::string() : H::Detalle()) : g_aviso;
  if (g_aviso.empty()) {   // F3: que es cada fila (ayudas_tutoriales.md, seccion 5)
    static const char* const kAyuda[kFilas][5] = {
        {"Raza: decide qué cuerpos y trajes puedes elegir.", "Race: decides which bodies and costumes you can pick.",
         "Race : décide quels corps et costumes tu peux choisir.", "Rasse: bestimmt, welche Körper und Kostüme du wählen kannst.",
         "Razza: decide quali corpi e costumi puoi scegliere."},
        {"Cuerpo: el modelo que usa tu héroe en combate.", "Body: the model your hero uses in battle.",
         "Corps : le modèle que ton héros utilise en combat.", "Körper: das Modell, das dein Held im Kampf nutzt.",
         "Corpo: il modello che il tuo eroe usa in combattimento."},
        {"Apariencia: el traje de ese cuerpo. A: pantalla Crear / Editar (colores, logo, scouter, pelo).",
         "Look: the costume of that body. A: Create / Edit screen (colors, logo, scouter, hair).",
         "Apparence : le costume de ce corps. A : écran Créer / Éditer.",
         "Aussehen: das Kostüm dieses Körpers. A: Erstellen / Bearbeiten.",
         "Aspetto: il costume di quel corpo. A: schermata Crea / Modifica."},
        {},   // estilo: su texto va aparte (T::T)
        {},   // moveset: su texto va aparte (T::T)
        {"Aura: la que sale al cargar ki en combate.", "Aura: the one shown when charging ki in battle.",
         "Aura : celle qui sort en chargeant le ki.", "Aura: die beim Ki-Aufladen im Kampf.",
         "Aura: quella che esce caricando il ki."},
        {"Puntos: izquierda los gasta en vida (+100), derecha en daño (+2%). Se ganan al vencer.",
         "Points: left spends them on health (+100), right on damage (+2%). Earned by winning.",
         "Points : gauche = vie (+100), droite = dégâts (+2 %). On les gagne en gagnant.",
         "Punkte: links für Leben (+100), rechts für Schaden (+2 %). Gibt es für Siege.",
         "Punti: sinistra per la vita (+100), destra per i danni (+2%). Si ottengono vincendo."},
        {"Maestros: A abre la elección de maestro. Con el 60% de entrenamiento puedes cambiar; entrenado, da su cápsula en cada combate.",
         "Masters: A opens Select Master. At 60% training you can switch; trained, it gives its capsule in every fight.",
         "Maîtres : chaque saga terminée en entraîne un. À 60 % d'entraînement tu peux changer de maître ; entraîné, il donne sa technique (une capsule) à chaque combat.",
         "Meister: jede abgeschlossene Saga trainiert einen. Ab 60 % Training kannst du den Meister wechseln; trainiert, gibt er seine Technik (eine Kapsel) in jedem Kampf.",
         "Maestri: ogni saga completata ne allena uno. Con il 60% di allenamento puoi cambiare maestro; allenato, dà la sua tecnica (una capsula) in ogni combattimento."},
        {"Empieza la misión. Cada victoria da experiencia y sube el nivel.",
         "Start the mission. Each win gives experience and raises the level.",
         "Commence la mission. Chaque victoire donne de l'expérience et fait monter le niveau.",
         "Starte die Mission. Jeder Sieg gibt Erfahrung und erhöht das Level.",
         "Inizia la missione. Ogni vittoria dà esperienza e fa salire il livello."}};
    const auto& h = kAyuda[std::clamp(g_cursor, 0, kFilas - 1)];
    const bool fila_ki = g_cursor == kFilas;
    b += std::string(1, '\n') +
         (g_cursor == kFilas + 1
              ? (H::Xv2Instalado()
                     ? T::T("Piezas: Budokai 3 usa las de tus personajes nativos. Xenoverse 2 usa las de tu juego (EXPERIMENTAL).",
                            "Parts: Budokai 3 uses your native characters' parts. Xenoverse 2 uses your game's (EXPERIMENTAL).")
                     : T::T("Piezas: Xenoverse 2 requiere tenerlo instalado. Usas las piezas de Budokai 3.",
                            "Parts: Xenoverse 2 requires it to be installed. You are using Budokai 3 parts."))
          : fila_ki ? T::T("Ki: el color de los efectos de tus técnicas. Se abren con los maestros (p. ej. ki dorado con Goku).",
                          "Ki: the color of your techniques' effects. Unlocked with the masters (e.g. golden ki with Goku).")
          : g_cursor == 3 ? T::T("Estilo: cómo peleas (los golpes de otro luchador). La estrella marca a "
                               "un maestro, que da bonus.",
                               "Style: how you fight (another fighter's moves). The star marks a "
                               "master, who gives a bonus.")
          : g_cursor == 4 ? T::T("Moveset: de quién son los golpes (animaciones, combos y cámara). A: lista de movesets. "
                                 "El estilo pone la vida, el daño y las transformaciones.",
                                 "Moveset: whose moves you use (animations, combos and camera). A: moveset list. The style "
                                 "keeps health, damage and transformations.")
          : g_cursor == kFilas - 1 ? T::T("A: empieza la saga siguiente. Derecha: el mapa del mundo (las sagas, "
                                          "tu héroe y el radar de las Esferas).",
                                          "A: start the next saga. Right: the world map (sagas, your hero and the "
                                          "Dragon Ball radar).")
                                   : L(h[0], h[1], h[2], h[3], h[4]));
  }
  dbz3::heroe_ui::Principal(dl, a, t, g_cursor, g_tab_h, filas, b, g_aviso, ficha);
  S::HelpLine(dl, a.At(600, 868), 38 * a.s,
              {{"A", g_cursor == kFilas - 1 ? L("Empezar", "Start", "Commencer", "Starten", "Inizia") : Aceptar()},
               {"B", Atras()},
               {"LB", T::T("Pestaña -", "Tab -")},
               {"RB", T::T("Pestaña +", "Tab +")},
               {"X", T::T("Ficha nueva", "New slot")},
               {"Y", T::T("Ficha", "Slot")},
               {"Start", T::T("Fichas", "Slots")}});
}

// Simulacion: un dia (entrenar / explorar / descansar) o, gastados los tres, el jefe.
void DibujarSim(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  auto it = g_runs.find(M::kSimulacion);
  if (it == g_runs.end()) {
    g_vista = kSelector;
    return;
  }
  Run& r = it->second;
  const Sim s = SimDe(r);
  const bool jefe = s.dia > kSimDias;
  std::vector<std::string> ops;
  if (jefe) {
    ops = {Lang("¡Al combate contra ", "To battle against ", "Au combat contre ", "In den Kampf gegen ",
                "Allo scontro con ") + M::Nombre(kSimJefes[r.semilla % 3]) + "!"};
  } else {
    ops = {L("Entrenar", "Train", "S'entraîner", "Trainieren", "Allenarsi"),
           L("Explorar", "Explore", "Explorer", "Erkunden", "Esplorare"),
           L("Descansar", "Rest", "Se reposer", "Ausruhen", "Riposare")};
  }
  const int n = int(ops.size());
  g_cursor = std::clamp(g_cursor, 0, n - 1);
  if (p & kUp) g_cursor = (g_cursor + n - 1) % n;
  if (p & kDown) g_cursor = (g_cursor + 1) % n;
  if (p & kB) {   // la partida queda guardada en ese dia
    g_vista = kSelector;
    return;
  }
  static float ultima = -10.0f;
  if ((p & kA) && t - ultima < 0.5f) {
    REXLOG_INFO("dbz3 rpg: simulacion: pulsacion repetida ignorada");
    p &= ~kA;
  }
  if (p & kA) {
    ultima = t;
    if (jefe) {
      r.vida = s.vida;
      r.vida_max = s.vida_max;
      if (Lanzar(r, true)) g_vista = kCerrada;
      return;
    }
    r.eventos += "EXD"[g_cursor];   // un dia por eleccion (una pulsacion = un evento)
    Guardar();
    const Sim d = SimDe(r);
    g_aviso = L(d.ultimo_es.c_str(), d.ultimo_en.c_str(), d.ultimo_fr.c_str(), d.ultimo_de.c_str(), d.ultimo_it.c_str());
    REXLOG_INFO("dbz3 rpg: simulacion: dia {} -> {} (vida {}/{}, fuerza +{}%, fatiga {}, {} capsulas)", s.dia,
                "EXD"[g_cursor], d.vida, d.vida_max, d.fuerza, d.fatiga, d.caps.size());
    g_cursor = 0;
    return;
  }
  S::Banner(dl, a, M::NombreModo(M::kSimulacion));
  const std::string dia = jefe ? Lang("¡Ha llegado el día!", "The day has come!", "Le jour est venu !",
                                      "Der Tag ist gekommen!", "È arrivato il giorno!")
                               : Lang("Día ", "Day ", "Jour ", "Tag ", "Giorno ") + Num(s.dia) + " / " + Num(kSimDias);
  const float sim_y = Lineas(dl, a, D::kTop, {dia, Vida(s.vida, s.vida_max) + "   " + Lang("Fuerza +", "Strength +", "Force +", "Kraft +", "Forza +") +
                               Num(s.fuerza) + "%   " + Lang("Fatiga ", "Fatigue ", "Fatigue ", "Erschöpfung ", "Fatica ") +
                               Num(s.fatiga)});
  Lista(dl, a, sim_y + D::kGap, ops, g_cursor, t);
  std::string b;
  if (!g_aviso.empty()) b = g_aviso + "\n";
  if (jefe) {
    b += Lang("La fatiga hace que recibas más daño (+10% cada punto). ", "Fatigue makes you take more damage (+10% each). ",
              "La fatigue te fait subir plus de dégâts (+10% par point). ",
              "Erschöpfung lässt dich mehr Schaden nehmen (+10% pro Punkt). ",
              "La fatica ti fa subire più danni (+10% per punto). ");
  } else if (g_cursor == 0) {
    b += Lang("Entrenar: +15% de daño, +1 de fatiga y -300 de vida.", "Train: +15% damage, +1 fatigue and -300 health.",
              "S'entraîner : +15% de dégâts, +1 de fatigue et -300 de vie.",
              "Trainieren: +15% Schaden, +1 Erschöpfung und -300 Leben.",
              "Allenarsi: +15% di danni, +1 di fatica e -300 di vita.");
  } else if (g_cursor == 1) {
    b += Lang("Explorar: puedes encontrar cápsulas o vida... o una emboscada.",
              "Explore: you may find capsules or health... or an ambush.",
              "Explorer : tu peux trouver des capsules ou de la vie... ou une embuscade.",
              "Erkunden: Du kannst Kapseln oder Leben finden... oder einen Hinterhalt.",
              "Esplorare: puoi trovare capsule o vita... o un'imboscata.");
  } else {
    b += Lang("Descansar: +1500 de vida y quita la fatiga.", "Rest: +1500 health and removes fatigue.",
              "Se reposer : +1500 de vie et enlève la fatigue.", "Ausruhen: +1500 Leben und keine Erschöpfung mehr.",
              "Riposare: +1500 di vita e toglie la fatica.");
  }
  if (!s.caps.empty()) {
    b += Lang("  Cápsulas: ", "  Capsules: ", "  Capsules : ", "  Kapseln: ", "  Capsule: ") + Num(int(s.caps.size()));
  }
  Bocadillo(dl, a, b);
  D::Help(dl, a, {{"A", Aceptar()}, {"B", L("Guardar y salir", "Save and exit",
                                                                         "Sauver et quitter", "Speichern und raus",
                                                                         "Salva ed esci")}});
}

// Torneo de 4: el cuadro (semifinales y final) con las caras del juego.
float DibujarCuadro(ImDrawList* dl, const S::Area& a, float t, uint16_t p, const Run& r, bool fin, bool campeon) {
  S::Banner(dl, a, M::NombreModo(M::kTorneo4));
  const int sf1 = r.ganados >= 1 || campeon ? r.jugador : (fin ? r.bots[0] : -1);   // ganador de la semifinal 1
  const int champ = fin ? r.extra : -1;
  struct Caja {
    int id;
    float x, y;
    bool gana;
  };
  // en la columna del esquema comun (a la derecha del anfitrion): semifinales arriba y abajo; la final en medio
  constexpr float y1 = D::kTop + 50, y2 = D::kTop + 170, ym = (y1 + y2) * 0.5f;
  constexpr float cl = D::kCx - 230, cr = D::kCx + 230, hw = 115, xl = D::kCx - 85, xr = D::kCx + 85;
  const Caja cajas[] = {{r.jugador, cl, y1, sf1 == r.jugador}, {r.bots[0], cl, y2, sf1 == r.bots[0]},
                        {r.bots[1], cr, y1, r.finalista == r.bots[1]}, {r.bots[2], cr, y2, r.finalista == r.bots[2]}};
  const ImU32 oro = IM_COL32(255, 210, 90, 255), gris = IM_COL32(200, 200, 200, 160);
  for (const Caja& c : cajas) {
    const bool izq = c.x < D::kCx;
    const ImVec2 p0 = a.At(c.x - hw, c.y - 40), p1 = a.At(c.x + hw, c.y + 40);
    S::Frame(dl, p0, p1, a.s);
    // la cara dentro de la linea del marco (alto 46 de los 80 del marco)
    const float cx = hw - D::kLine - 34;   // (-28 rozaba la linea: 20261016-032857_hub_test_r6_pantallas\b_torneo)
    Cara(dl, a, c.id, izq ? c.x - cx : c.x + cx, c.y, 46);
    S::MenuText(dl, a.At(c.x + (izq ? 30 : -30), c.y), D::kSmall * a.s, M::Nombre(c.id).c_str(), c.gana, false,
                (hw * 2 - 110) * a.s);
    const float xe = izq ? c.x + hw : c.x - hw, xm = izq ? xl : xr;
    dl->AddLine(a.At(xe, c.y), a.At(xm, c.y), c.gana ? oro : gris, 4 * a.s);
  }
  dl->AddLine(a.At(xl, y1), a.At(xl, y2), sf1 >= 0 ? oro : gris, 4 * a.s);
  dl->AddLine(a.At(xr, y1), a.At(xr, y2), r.finalista >= 0 ? oro : gris, 4 * a.s);
  dl->AddLine(a.At(xl, ym), a.At(D::kCx - 45, ym), sf1 >= 0 ? oro : gris, 4 * a.s);
  dl->AddLine(a.At(D::kCx + 45, ym), a.At(xr, ym), r.finalista >= 0 ? oro : gris, 4 * a.s);
  S::Frame(dl, a.At(D::kCx - 45, ym - 40), a.At(D::kCx + 45, ym + 40), a.s);
  if (champ >= 0) Cara(dl, a, champ, D::kCx, ym, 44);
  else if (const S::Pic vs = S::Img("vs_torneo"))   // el "VS" del Torneo Mundial del juego (data_usi 2047)
    S::DrawPic(dl, vs, a.At(D::kCx - 27, ym - 11), a.At(D::kCx + 27, ym + 12), ImVec4(0, 4, 236, 103));
  else S::MenuText(dl, a.At(D::kCx, ym), D::kHead * a.s, "VS", true, false);
  (void)t;
  (void)p;
  return y2 + 40;   // borde de abajo del cuadro
}

void DibujarTorneo(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  auto it = g_runs.find(M::kTorneo4);
  if (it == g_runs.end()) {
    g_vista = kSelector;
    return;
  }
  Run& r = it->second;
  if (p & kB) {
    g_vista = kSelector;
    return;
  }
  if (p & kA) {
    if (Lanzar(r, true)) g_vista = kCerrada;
    return;
  }
  Lineas(dl, a, DibujarCuadro(dl, a, t, p, r, false, false) + D::kGap, {Lang("Semifinal: ", "Semifinal: ", "Demi-finale : ", "Halbfinale: ", "Semifinale: ") +
                          M::Nombre(r.jugador) + " - " + M::Nombre(r.bots[0])});
  Bocadillo(dl, a, Descripcion(M::kTorneo4));
  D::Help(dl, a, {{"A", L("¡A luchar!", "Fight!", "Au combat !", "Kämpfen!", "Combatti!")},
                                             {"B", Atras()}});
}

// Entre combates de un reto propio: tienda, cartas, cuadro o el aviso del siguiente; A = siguiente combate.
void DibujarEntre(ImDrawList* dl, const S::Area& a, float t, uint16_t p, const M::Estado& st, Run& r) {
  S::Banner(dl, a, M::NombreModo(r.modo));
  const bool cargando = st.fase == M::kCombate;   // ya pedido: el juego esta entrando en el combate
  if (cargando) p = 0;
  const std::string siguiente = Lang("Siguiente: ", "Next: ", "Suivant : ", "Nächster: ", "Prossimo: ") +
                                M::Nombre(st.rival) + "  (" + dbz3::escenarios::Nombre(st.escenario) + ")";
  const std::string paso = std::string(Palabra(r.modo)) + " " + Num(std::min(r.ganados + 1, st.total)) + " / " + Num(st.total);
  std::string b = g_aviso;
  auto mas = [&b](const std::string& s) { b += (b.empty() ? "" : "\n") + s; };
  if (r.modo == M::kTienda) {
    // tienda: arriba/abajo, A compra; la ultima fila sigue
    std::vector<std::string> filas;
    std::vector<int> precios;
    std::vector<bool> off;
    for (int k = 0; k < 3; ++k) {
      const Articulo& ar = kArticulos[k];
      filas.push_back(Corto(L(ar.es, ar.en, ar.fr, ar.de, ar.it)));
      precios.push_back(ar.precio);
      const bool comprado = k > 0 && r.eventos.find(char('0' + k)) != std::string::npos;
      off.push_back(r.zeni < ar.precio || comprado || (k == 0 && r.vida >= r.vida_max));
    }
    filas.push_back(L("Siguiente combate", "Next fight", "Combat suivant", "Nächster Kampf", "Prossimo incontro"));
    off.push_back(false);
    const int n = int(filas.size());
    g_cursor = std::clamp(g_cursor, 0, n - 1);
    if (p & kUp) g_cursor = (g_cursor + n - 1) % n;
    if (p & kDown) g_cursor = (g_cursor + 1) % n;
    if ((p & kA) && g_cursor < 3) {
      const Articulo& ar = kArticulos[g_cursor];
      if (off[size_t(g_cursor)]) {
        g_aviso = Lang("No puedes comprarlo ahora.", "You can't buy that now.", "Tu ne peux pas l'acheter maintenant.",
                       "Das kannst du jetzt nicht kaufen.", "Non puoi comprarlo ora.");
      } else {   // una compra, un recibo: precio fuera, efecto dentro y se guarda ya
        r.zeni -= ar.precio;
        if (g_cursor == 0) r.vida = std::min(r.vida_max, r.vida + 1000);
        else r.eventos += char('0' + g_cursor);
        Guardar();
        g_aviso = Lang("Comprado: ", "Bought: ", "Acheté : ", "Gekauft: ", "Comprato: ") + L(ar.es, ar.en, ar.fr, ar.de, ar.it);
        REXLOG_INFO("dbz3 rpg: tienda: compra {} por {} (quedan {} Zenie, vida {}/{})", g_cursor, ar.precio, r.zeni,
                    r.vida, r.vida_max);
      }
      p = 0;
    }
    ListaTienda(dl, a, Lineas(dl, a, D::kTop, {paso + "   " + Vida(r.vida, r.vida_max)}) + D::kGap, filas, precios,
                g_cursor, t, off);
    CajaZenie(dl, a, r.zeni);
    if (g_cursor < 3 && g_aviso.empty()) {
      const Articulo& ar = kArticulos[g_cursor];
      mas(std::string(L(ar.es, ar.en, ar.fr, ar.de, ar.it)) + "  (" + Num(ar.precio) + " Zenie)");
    }
    mas(siguiente);
    if (g_cursor == 3 && (p & kA)) p = kA;   // sigue
    else p &= ~kA;
  } else if (r.modo == M::kAbyss) {
    const std::vector<int> oferta = Oferta(r, r.ganados);
    const bool elegida = int(r.eventos.size()) >= r.ganados || oferta.empty();
    if (!elegida) {   // elegir una carta (una vez por piso; queda guardada)
      const int n = int(oferta.size());
      g_cursor = std::clamp(g_cursor, 0, n - 1);
      if (p & kUp) g_cursor = (g_cursor + n - 1) % n;
      if (p & kDown) g_cursor = (g_cursor + 1) % n;
      if (p & kA) {
        const int c = oferta[size_t(g_cursor)];
        r.eventos += char('0' + c);
        if (c == 2) {
          r.vida_max = std::min(7000, r.vida_max + 1000);
          r.vida = std::min(r.vida_max, r.vida + 1000);
        }
        Guardar();
        g_aviso = Lang("Carta: ", "Card: ", "Carte : ", "Karte: ", "Carta: ") +
                  L(kCartas[c].es, kCartas[c].en, kCartas[c].fr, kCartas[c].de, kCartas[c].it);
        REXLOG_INFO("dbz3 rpg: abyss: piso {}: carta {} (cartas {})", r.ganados, c, r.eventos);
        g_cursor = 0;
        p = 0;
      } else {
        p &= ~kA;
      }
      std::vector<std::string> filas;
      for (const int c : oferta) filas.push_back(Corto(L(kCartas[c].es, kCartas[c].en, kCartas[c].fr, kCartas[c].de, kCartas[c].it)));
      Lista(dl, a, Lineas(dl, a, D::kTop, {paso + "   " + Vida(r.vida, r.vida_max)}) + D::kGap, filas, g_cursor, t);
      if (!oferta.empty()) {
        const Carta& ca = kCartas[oferta[size_t(g_cursor)]];
        mas(L(ca.es, ca.en, ca.fr, ca.de, ca.it));
      }
      mas(Lang("Elige una carta: dura hasta el final de esta partida.", "Pick a card: it lasts until the end of this run.",
               "Choisis une carte : elle dure jusqu'à la fin de cette partie.",
               "Wähle eine Karte: Sie gilt bis zum Ende dieses Laufs.",
               "Scegli una carta: dura fino alla fine di questa partita."));
    } else {
      const Abyss ab = AbyssDe(r);
      std::vector<std::string> l = {paso, Vida(r.vida, r.vida_max), siguiente};
      std::string cartas;
      for (int k = 0; k < kNumCartas; ++k) {
        if (ab.stacks[k]) {
          cartas += std::string(cartas.empty() ? "" : ", ") + L(kCartas[k].es, kCartas[k].en, kCartas[k].fr, kCartas[k].de,
                                                                kCartas[k].it) + (ab.stacks[k] > 1 ? " x2" : "");
        }
      }
      Lineas(dl, a, D::kTop, l);
      if (!cartas.empty()) mas(cartas);
    }
  } else if (r.modo == M::kTorneo4) {
    Lineas(dl, a, DibujarCuadro(dl, a, t, p, r, false, false) + D::kGap, {Lang("Final: ", "Final: ", "Finale : ", "Finale: ", "Finale: ") + M::Nombre(r.jugador) + " - " +
                        M::Nombre(r.finalista)});
    mas(M::Nombre(r.finalista) + Lang(" ha ganado la otra semifinal.", " won the other semifinal.",
                                      " a gagné l'autre demi-finale.", " hat das andere Halbfinale gewonnen.",
                                      " ha vinto l'altra semifinale."));
  } else {   // Heroe, Torres, Jefes, Simulacion: el siguiente combate
    std::vector<std::string> l = {paso, siguiente, Vida(r.vida, r.vida_max)};
    if (r.modo == M::kTorres) {
      const Torre& tr = kTorres[std::clamp(r.extra, 0, kNumTorres - 1)];
      l.push_back(L(tr.mod_es, tr.mod_en, tr.mod_fr, tr.mod_de, tr.mod_it));
    } else if (r.modo == M::kJefes) {
      const Jefe& j = kJefes[std::clamp(r.ganados, 0, 2)];
      l.push_back(j.objetivo ? Lang("Objetivo: aguanta ", "Goal: survive ", "Objectif : tiens ", "Ziel: überlebe ",
                                    "Obiettivo: resisti ") + Num(j.objetivo) + " s"
                             : Lang("Armadura ", "Armor ", "Armure ", "Panzerung ", "Armatura ") + Num(j.armadura) + "%" +
                                   (j.gigante > 0 ? Lang("  -  ¡Gigante!", "  -  Giant!", "  -  Géant !", "  -  Riesig!",
                                                         "  -  Gigante!")
                                                  : std::string()));
    }
    if (r.modo == M::kHeroe) {
      H::ElegirEstilo(r.jugador);   // el carrusel parte del estilo de la partida
      if (!cargando && (p & (kLb | kRb))) {   // otro estilo (moveset) para el combate siguiente
        const int vida_max_antes = r.vida_max;
        H::Mover(3, (p & kRb) ? +1 : -1);
        r.jugador = H::EstiloId();
        r.vida_max = H::Vida(r.jugador);
        // la vida de la partida pasa de un combate al otro: se conserva la misma fraccion (con el maximo del estilo
        // nuevo; antes un heroe sano quedaba a 3000/3500 al cambiar a un maestro con vida extra)
        r.vida = std::clamp(int(int64_t(r.vida) * r.vida_max / std::max(vida_max_antes, 1)), 1, r.vida_max);
        M::SetJugador(H::Luchador(r.jugador));
        RivalHeroe(r, r.ganados);
        Guardar();
        REXLOG_INFO("dbz3 rpg: heroe: estilo {} para el combate {}", r.jugador, r.ganados + 1);
        p &= uint16_t(~(kLb | kRb));
      }
      if (!cargando && (p & (kX | kY))) {   // frente 10: otro moveset donante (X/Y) para el combate siguiente
        H::Mover(4, (p & kY) ? +1 : -1);
        Guardar();
        REXLOG_INFO("dbz3 rpg: heroe: moveset {} para el combate {}", H::MovesetId(), r.ganados + 1);
        p &= uint16_t(~(kX | kY));
      }
      l.push_back(H::LineaNivel(r.ganados) + "  -  " + H::Fila(4));
    }
    const float y_lineas = Lineas(dl, a, D::kTop, l);
    if (r.modo == M::kHeroe) {
      CarruselEstilos(dl, a, y_lineas + D::kGap, y_lineas + D::kGap + 140, t, !cargando);
      int rival = 0, vida_rival = 0, esc = 0;
      H::Mision(r.tecnicas, r.ganados, r.jugador, H::CuerpoDe(r.extra, r.jugador), &rival, &vida_rival, &esc);
      mas(H::DetalleEstilo(r.jugador));
      mas(H::MisionTexto(r.tecnicas, r.ganados, rival));
    }
  }
  if (cargando) {
    mas(Lang("¡Prepárate!", "Get ready!", "Prépare-toi !", "Mach dich bereit!", "Preparati!"));
  } else {
    if (b.empty()) {
      mas(Lang("¡Prepárate para el siguiente combate! Si sales ahora, la próxima vez sigues aquí.",
               "Get ready for the next fight! If you leave now, next time you continue here.",
               "Prépare-toi pour le prochain combat ! Si tu pars maintenant, tu reprendras ici.",
               "Mach dich bereit für den nächsten Kampf! Wenn du jetzt gehst, machst du nächstes Mal hier weiter.",
               "Preparati per il prossimo incontro! Se esci ora, la prossima volta riprendi da qui."));
    }
    if (p & kA) {
      g_aviso.clear();
      g_cursor = 0;
      Lanzar(r, false);
    } else if (p & kB) {   // deja el reto (la partida queda guardada: se sigue la proxima vez)
      g_aviso.clear();
      M::Abort();
    }
  }
  Bocadillo(dl, a, b);
  if (!cargando) {
    std::vector<std::pair<const char*, const char*>> ayuda = {
        {"A", r.modo == M::kTienda && g_cursor < 3 ? L("Comprar", "Buy", "Acheter", "Kaufen", "Compra")
              : r.modo == M::kHeroe                ? L("¡A luchar!", "Fight!", "Au combat !", "Kämpfen!", "Combatti!")
                                                   : L("Siguiente", "Next", "Suivant", "Weiter", "Avanti")},
        {"B", L("Guardar y salir", "Save and exit", "Sauver et quitter", "Speichern und raus", "Salva ed esci")}};
    if (r.modo == M::kHeroe) {   // LB/RB cambian el estilo del carrusel; X/Y, el moveset donante (frente 10)
      ayuda.push_back({"LB", T::T("Estilo -", "Style -")});
      ayuda.push_back({"RB", T::T("Estilo +", "Style +")});
      ayuda.push_back({"X", T::T("Moveset -", "Moveset -")});
      ayuda.push_back({"Y", T::T("Moveset +", "Moveset +")});
    }
    D::Help(dl, a, ayuda);
  }
}

void DibujarFin(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const M::Estado& st = g_fin_st;
  const Run& r = g_fin_run;
  // studies 8 H6: cierre de la saga completada (y el epilogo tras la ultima) antes de los resultados
  if (st.tipo == M::kHeroe && g_fin_pagina == 0 && !g_fin_cierre.empty()) {
    if (!g_fin_cierre_dicho) {
      g_fin_cierre_dicho = true;
      REXLOG_INFO("dbz3 rpg: heroe: cierre de la saga {} en pantalla ({} lineas{}{})", g_fin_saga, g_fin_cierre.size(),
                  g_fin_epilogo ? ", con el epilogo de la ultima saga" : "", g_fin_pierde ? ", derrota del guion" : "");
    }
    if (p & (kA | kB)) {
      g_fin_pagina = 1;
      return;
    }
    S::Banner(dl, a, H::NombreSaga(g_fin_saga));
    std::vector<std::string> l = {g_fin_epilogo ? T::T("Fin de la historia", "The end of the story")
                                                : T::T("Fin de la saga", "End of the saga")};
    if (g_fin_pierde)
      l.push_back(T::T("Omega era demasiado fuerte... pero la historia sigue.", "Omega was too strong... but the story goes on."));
    for (const std::string& x : g_fin_cierre) {   // partidas por palabras: el marco reduce la letra si no caben
      std::string fila;
      size_t i = 0;
      while (i < x.size()) {
        size_t j = x.find(' ', i);
        if (j == std::string::npos) j = x.size();
        const std::string w = x.substr(i, j - i);
        if (!fila.empty() && fila.size() + 1 + w.size() > 52) {
          l.push_back(fila);
          fila.clear();
        }
        fila += (fila.empty() ? "" : " ") + w;
        i = j + 1;
      }
      if (!fila.empty()) l.push_back(fila);
    }
    Lineas(dl, a, D::kTop, l);
    S::Frame(dl, a.At(D::kCornerX0 + 120, D::kCornerY0), a.At(D::kX1, D::kTop - 24), a.s);   // el heroe, en su marco
    Cara(dl, a, H::CuerpoDe(r.extra, r.jugador), D::kCornerX0 + 200, (D::kCornerY0 + D::kTop - 24) * 0.5f, 62);
    S::MenuText(dl, a.At((D::kCornerX0 + 280 + D::kX1) * 0.5f, (D::kCornerY0 + D::kTop - 24) * 0.5f), D::kRow * a.s,
                (std::string(T::T("Nivel ", "Level ")) + Num(H::Nivel())).c_str(), true, false);
    Bocadillo(dl, a, H::Saga() >= H::kSagasJugables ? std::string(T::T("Saga 3 completada. Las siguientes sagas estan en desarrollo.", "Saga 3 cleared. The next sagas are in development."))
                                                    : std::string(T::T("Próxima saga: ", "Next saga: ")) + H::NombreSaga(H::Saga()));
    D::Help(dl, a, {{"A", Aceptar()}});
    return;
  }
  if (p & (kA | kB)) {
    g_fin = false;
    g_fin_hecho = true;
    g_aviso.clear();
    M::CerrarFin();   // el HUB vuelve
    if (st.tipo == M::kMision) {   // Mision 100: de vuelta a su pagina, en la siguiente mision si la supero
      const int sig = std::min(r.extra + (st.resultado == M::kVictoria ? 1 : 0), kNumMisiones - 1);
      g_pagina = sig / 5;
      g_cursor = sig % 5;
      if (!PaginaAbierta(g_pagina)) g_pagina = r.extra / 5, g_cursor = r.extra % 5;
      g_vista = kMisionP;
    }
    return;
  }
  S::Banner(dl, a, M::NombreModo(st.tipo));
  std::string titulo;
  if (st.resultado == M::kVictoria) {
    titulo = st.tipo == M::kTorneo4 ? Lang("¡Campeón del torneo!", "Tournament champion!", "Champion du tournoi !",
                                          "Turniersieger!", "Campione del torneo!")
                                    : Lang("¡Reto superado!", "Challenge cleared!", "Défi réussi !",
                                           "Herausforderung geschafft!", "Sfida superata!");
  } else if (st.resultado == M::kGuardado || st.resultado == M::kAbandono) {
    titulo = Lang("Partida guardada", "Run saved", "Partie sauvegardée", "Lauf gespeichert", "Partita salvata");
  } else {
    titulo = Lang("Derrota", "Defeat", "Défaite", "Niederlage", "Sconfitta");
  }
  std::vector<std::string> l = {titulo, M::Nombre(r.jugador) + ": " + Num(st.ganados) + " / " + Num(st.total)};
  if (st.tipo == M::kTienda) {
    l.push_back(Lang("Zenie sin gastar: ", "Unspent Zenie: ", "Zenie non dépensés : ", "Nicht ausgegebene Zenie: ",
                     "Zenie non spesi: ") + Num(r.zeni));
  }
  if (st.tipo == M::kHeroe) {   // el heroe: su cuerpo y su nivel (heroe.cpp, FinRun)
    l[1] = Lang("Héroe ", "Hero ", "Héros ", "Held ", "Eroe ") + M::Nombre(H::CuerpoDe(r.extra, r.jugador)) + ": " +
           Num(st.ganados) + " / " + Num(st.total);
    const int sube = st.resultado == M::kVictoria || st.resultado == M::kDerrota ? H::Nivel() - H::NivelAntes() : 0;
    if (st.resultado == M::kVictoria || st.resultado == M::kDerrota) l.push_back(H::LineaNivel(0));   // nivel y XP
    if (sube > 0) {   // lo que da cada nivel (heroe.cpp: Vida, Reglas)
      l.push_back(Lang("¡Sube de nivel! ", "Level up! ", "Niveau supérieur ! ", "Stufenaufstieg! ", "Livello su! ") +
                  Num(H::NivelAntes()) + " > " + Num(H::Nivel()) + "  (+" + Num(100 * sube) +
                  Lang(" vida, +", " health, +", " vie, +", " Leben, +", " vita, +") + Num(2 * sube) +
                  Lang("% daño)", "% damage)", "% dégâts)", "% Schaden)", "% danni)"));
    }
  }
  if (st.tipo == M::kMision) {
    l[1] = Lang("Misión ", "Mission ", "Mission ", "Mission ", "Missione ") + Num(r.extra + 1) + ": " + TituloMision(r.extra);
    if (st.resultado == M::kVictoria) {
      l.push_back(Lang("Rango ", "Rank ", "Rang ", "Rang ", "Grado ") + std::string(1, g_m100_ultimo) + "   " +
                  Lang("Mejor ", "Best ", "Meilleur ", "Bester ", "Migliore ") +
                  std::string(1, g_m100[size_t(std::clamp(r.extra, 0, kNumMisiones - 1))]));
    }
  }
  // studies 8 H6: la derrota "del guion" (nota pierde) completa la saga, pero modos.cpp no paga premio por ella
  const bool sin_premio = st.tipo == M::kHeroe && g_fin_pierde;
  if (st.resultado == M::kVictoria && PremioDe(st.tipo, r.extra) > 0 && !sin_premio) {
    l.push_back(Lang("Premio: ", "Prize: ", "Prix : ", "Preis: ", "Premio: ") + Num(PremioDe(st.tipo, r.extra)) + " Zenie");
  }
  if (st.tipo == M::kTorneo4) {
    Lineas(dl, a, DibujarCuadro(dl, a, t, p, r, true, st.resultado == M::kVictoria) + D::kGap, l);
  } else {
    Lineas(dl, a, D::kTop, l);
  }
  std::string b;
  if (sin_premio) {
    b = T::T("La derrota estaba escrita: la historia sigue.", "The defeat was written: the story goes on.");
  } else if (st.resultado == M::kVictoria && PremioDe(st.tipo, r.extra) == 0) {
    b = Lang("¡Increíble! Los Zenie que no gastaste quedan como tu récord.",
             "Amazing! The Zenie you didn't spend stay as your record.",
             "Incroyable ! Les Zenie non dépensés restent comme ton record.",
             "Unglaublich! Die nicht ausgegebenen Zenie bleiben als dein Rekord.",
             "Incredibile! Gli Zenie non spesi restano come tuo record.");
  } else if (st.resultado == M::kVictoria) {
    b = Lang("¡Increíble! El premio va a tu monedero del HUB.", "Amazing! The prize goes to your HUB wallet.",
             "Incroyable ! Le prix va dans ton porte-monnaie du HUB.", "Unglaublich! Der Preis kommt in deine HUB-Geldbörse.",
             "Incredibile! Il premio va nel tuo portafoglio del HUB.");
  } else if (st.resultado == M::kGuardado || st.resultado == M::kAbandono) {
    b = Lang("La próxima vez seguirás desde aquí.", "Next time you will continue from here.",
             "La prochaine fois, tu reprendras d'ici.", "Nächstes Mal machst du hier weiter.",
             "La prossima volta riprenderai da qui.");
  } else {
    b = Lang("Esta partida ha terminado. ¡Vuelve a intentarlo!", "This run is over. Try again!",
             "Cette partie est terminée. Réessaie !", "Dieser Lauf ist vorbei. Versuch es noch einmal!",
             "Questa partita è finita. Riprova!");
  }
  if (st.tipo == M::kHeroe && st.resultado == M::kVictoria) {   // la saga siguiente (heroe.cpp FinRun ya la avanzo)
    b += "\n" + std::string(T::T("Próxima saga: ", "Next saga: ")) + H::NombreSaga(H::Saga());
  }
  b += "\n" + Lang("Monedero: ", "Wallet: ", "Porte-monnaie : ", "Geldbörse: ", "Portafoglio: ") + Num(g_zeni) + " Zenie";
  Bocadillo(dl, a, b);
  D::Help(dl, a, {{"A", Aceptar()}});
}

}  // namespace

// tagteam_slots.cpp: la Mision 100 (o el combate de maestro del Modo Heroe) lleva equipos: no se borran al empezar
// su combate ni al reintentarlo. En los demas combates del heroe no hay equipo.
bool EquiposMision() {
  const M::Estado st = M::Get();
  if (g_equipos_heroe.load()) return st.propio && st.tipo == M::kHeroe && st.fase == M::kCombate;
  if (!g_equipos.load()) return false;
  return st.propio && st.tipo == M::kMision && st.fase == M::kCombate;
}

// Premio de un reto (modos.cpp, con su candado puesto): se suma en el siguiente Sincronizar.
void Premio(int zeni) { g_premio += std::max(zeni, 0); }

bool Activa() {
  std::lock_guard<std::mutex> lk(g_mu);
  Sincronizar();
  if (g_vista != kCerrada || g_fin) return true;
  const M::Estado st = M::Get();
  return st.propio && (st.fase == M::kEntre || st.fase == M::kCombate) && g_runs.count(st.tipo);
}

bool Busy() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_vista != kCerrada || g_fin;
}

// Fondo de las pantallas del Modo Heroe (selector en su fila, su pantalla o su reto en curso).
bool Mision100() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_vista == kMisionP;
}

bool Heroe() {
  std::lock_guard<std::mutex> lk(g_mu);
  const std::vector<int>& r = Retos(g_entrada);
  if (g_vista == kHeroeP || g_vista == kHeroeCrear || g_vista == kHeroeMaestro || g_vista == kHeroeMoveset || g_vista == kHeroeMapa || g_vista == kHeroeLista || (g_vista == kSelector && g_sel >= 0 && g_sel < int(r.size()) && r[size_t(g_sel)] == M::kHeroe))
    return true;
  const M::Estado st = M::Get();
  return st.propio && st.tipo == M::kHeroe && (st.fase == M::kEntre || st.fase == M::kCombate || g_fin);
}

// PRUEBAS (DBZ3_TEST_MODO=mision100,jugador,n, modos.cpp): empieza la mision n (1-100) sin pasar por sus paginas.
bool ProbarMision(int n, int jugador) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  g_jugador = jugador;
  g_traje = 0;
  g_m100_sel = std::clamp(n - 1, 0, kNumMisiones - 1);
  g_vista = kMisionP;
  Empezar(M::kMision);
  return g_vista == kCerrada;
}

void AbrirSelector(int entrada) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  g_entrada = entrada;
  g_sel = g_cursor = g_cursor2 = 0;
  g_aviso.clear();
  M::UltimoDuelo(&g_jugador, &g_traje);
  int elo = 0, tramo = 0;
  const char* nombre = nullptr;
  g_rango = M::PerfilJugador(&elo, &tramo, &nombre)
                ? Lang("Rango: ", "Rank: ", "Rang : ", "Rang: ", "Grado: ") + nombre + " (" + Num(elo) + ")"
                : std::string();
  g_vista = kSelector;
  if (entrada == 3) {   // Mision 100: sus paginas, en la primera mision sin superar de una pagina abierta
    const size_t n = std::min(g_m100.find('-'), size_t(kNumMisiones - 1));
    g_pagina = int(n) / 5;
    g_cursor = int(n) % 5;
    while (g_pagina > 0 && !PaginaAbierta(g_pagina)) --g_pagina, g_cursor = 0;
    g_vista = kMisionP;
  }
  REXLOG_INFO("dbz3 rpg: selector de {} (luchador {})", entrada == 3 ? "mision 100" : M::NombreModo(entrada), g_jugador);
}

// Modo Heroe como entrada propia del HUB (RegisterEntry "heroe" en modos.cpp): abre su pantalla de creacion sin pasar
// por el selector del Kumite. B vuelve al HUB (g_entrada = 4); la partida y su receta son las mismas de siempre.
void AbrirHeroe() {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  g_entrada = 4;
  g_cursor = 0;
  g_aviso.clear();
  M::UltimoDuelo(&g_jugador, &g_traje);
  g_vista = kHeroeP;
  REXLOG_INFO("dbz3 rpg: Modo Heroe, entrada propia del HUB (cuerpo {}, estilo {}, nivel {})", H::Cuerpo(),
              H::EstiloId(), H::Nivel());
  // PRUEBAS (studies 8): DBZ3_TEST_HEROE_PANTALLA=crear|editar|maestros abre esa pantalla sin navegar la lista (el
  // arnes pierde a veces la primera pulsacion y el cursor cae en otra fila).
  if (const char* e = std::getenv("DBZ3_TEST_HEROE_PANTALLA")) {
    const std::string v = e;
    if (v == "crear" || v == "editar") {
      dbz3::heroe_ui::AbrirCrear(v == "editar");
      g_vista = kHeroeCrear;
    } else if (v == "maestros") {
      dbz3::heroe_ui::AbrirMaestros();
      g_vista = kHeroeMaestro;
    } else if (v == "mapa") {
      dbz3::heroe_ui::AbrirMapa();
      g_vista = kHeroeMapa;
    } else if (v == "movesets") {
      dbz3::heroe_ui::AbrirMovesets();
      g_vista = kHeroeMoveset;
    } else if (v == "fichas") {
      dbz3::heroe_ui::AbrirSlots();
      g_vista = kHeroeLista;
    }
  }
}

void Dibujar(ImDrawList* dl, const S::Area& a, float t, uint16_t pulsado) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_fin) return DibujarFin(dl, a, t, pulsado);
  switch (g_vista) {
    case kSelector: return DibujarSelector(dl, a, t, pulsado);
    case kSimDia: return DibujarSim(dl, a, t, pulsado);
    case kHeroeP: return DibujarHeroe(dl, a, t, pulsado);
    case kHeroeLista: {   // K: lista de fichas; A elige, X duplica, Y archiva, B vuelve
      const auto r = dbz3::heroe_ui::SlotsUI(dl, a, t, pulsado);
      if (r == dbz3::heroe_ui::Accion::kConfirmar) {
        H::Importar(dbz3::cac::SlotActivo());
        Guardar();
        g_vista = kHeroeP;
      } else if (r == dbz3::heroe_ui::Accion::kCrear) {   // C2: A en "Nueva partida": el heroe nuevo abre la creacion
        H::Importar(dbz3::cac::SlotActivo());
        Guardar();
        dbz3::heroe_ui::AbrirCrear(false);
        g_vista = kHeroeCrear;
      } else if (r == dbz3::heroe_ui::Accion::kVolver) {
        Guardar();   // persiste duplicar/archivar aunque no se elija otra ficha
        g_vista = kHeroeP;
      }
      return;
    }
    case kHeroeMapa: {   // studies 8 fase 2 (H5): A en la saga siguiente la empieza; B vuelve a la creacion
      const auto r = dbz3::heroe_ui::MapaUI(dl, a, t, pulsado);
      if (r == dbz3::heroe_ui::Accion::kNada) return;
      g_vista = kHeroeP;
      if (r == dbz3::heroe_ui::Accion::kConfirmar) {
        Guardar();
        // C3: el Heroe es exclusivo de su modo: con su historia instalada entra en el mapa de Dragon Universe (casilla 36)
        // sin pasar por la rueda de personajes de DU; si no, las misiones de siempre
        if (H::DuDisponible() && dbz3::hub::RequestDu(H::CasillaDu())) {
          REXLOG_INFO("dbz3 rpg: el Heroe entra en el mapa de Dragon Universe");
          g_vista = kCerrada;
          return;
        }
        Empezar(M::kHeroe);
      }
      return;
    }
    case kHeroeCrear:
    case kHeroeMoveset:
    case kHeroeMaestro: {   // studies 8 H3/H4 (heroe_ui.cpp): Confirmar guarda la ficha; todas vuelven a la creacion
      namespace U = dbz3::heroe_ui;
      const U::Accion r = g_vista == kHeroeCrear     ? U::Crear(dl, a, t, pulsado)
                          : g_vista == kHeroeMoveset ? U::MovesetsUI(dl, a, t, pulsado)
                                                     : U::Maestros(dl, a, t, pulsado);
      if (r == U::Accion::kConfirmar) Guardar();
      if (r != U::Accion::kNada) g_vista = kHeroeP;
      return;
    }
    case kCuadro: return DibujarTorneo(dl, a, t, pulsado);
    case kMisionP: return DibujarM100(dl, a, t, pulsado);
    default: break;
  }
  const M::Estado st = M::Get();
  auto it = g_runs.find(st.tipo);
  if (st.propio && it != g_runs.end()) DibujarEntre(dl, a, t, pulsado, st, it->second);
}

// Durante la pelea de un reto propio, bajo el marcador (linea y del bloque del HUD de los modos, S::mode::kHud*):
// armadura del jefe y reglas del combate, con la misma letra que las lineas del marcador.
void Hud(ImDrawList* dl, const S::Area& a, float y) {
  std::lock_guard<std::mutex> lk(g_mu);
  const M::Estado st = M::Get();
  auto it = g_runs.find(st.tipo);
  if (it == g_runs.end()) return;
  const Run& r = it->second;
  if (r.modo == M::kHeroe) H::PruebaPerder(st.fase == M::kCombate);   // studies 8 H6: DBZ3_TEST_HEROE_PERDER
  std::string s;
  if (r.modo == M::kJefes && kJefes[std::clamp(r.ganados, 0, 2)].armadura > 0) {
    const bool on = M::ArmaduraActiva();
    S::MenuText(dl, a.At(600, y), 26 * a.s,
                on ? L("Armadura", "Armor", "Armure", "Panzerung", "Armatura") : L("¡Ahora!", "Now!", "Maintenant !",
                                                                                     "Jetzt!", "Ora!"),
                !on, on);
    return;
  }
  if (r.modo == M::kTorres) {
    const Torre& tr = kTorres[std::clamp(r.extra, 0, kNumTorres - 1)];
    s = L(tr.mod_es, tr.mod_en, tr.mod_fr, tr.mod_de, tr.mod_it);
  } else if (r.modo == M::kMision) {
    s = Lang("Misión ", "Mission ", "Mission ", "Mission ", "Missione ") + Num(r.extra + 1) + ": " + TituloMision(r.extra);
  } else {
    const M::Reglas g = ReglasDe(r);
    if (g.dano_hecho != 100) s += Lang("Daño ", "Damage ", "Dégâts ", "Schaden ", "Danni ") + Num(g.dano_hecho) + "%";
    if (g.dano_recibido != 100) {
      s += std::string(s.empty() ? "" : "  ") + Lang("Recibido ", "Taken ", "Subis ", "Erlitten ", "Subiti ") +
           Num(g.dano_recibido) + "%";
    }
  }
  if (!s.empty()) S::MenuText(dl, a.At(600, y), 24 * a.s, s.c_str(), true, false, 600 * a.s);
}

// C3: el Heroe gano el combate k de la saga `saga` en el mapa de Dragon Universe (campaigns.cpp): progreso al heroe y a disco.
void DuVictoria(int saga, int k) {
  std::lock_guard<std::mutex> lk(g_mu);
  Cargar();
  H::DuVictoria(saga, k);
  Guardar();
}

}  // namespace dbz3::rpg

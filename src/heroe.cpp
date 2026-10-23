// dbz3 - Modo Heroe (beta; ronda 3 frente G, ronda 4 frente R5): creacion de personaje al estilo Xenoverse /
// Ultimate Tenkaichi. Raza -> cuerpo (un luchador del juego), apariencia (sus trajes, tabla del select), estilo de
// lucha = el moveset de otro luchador (maestro o no). En combate el jugador ES el luchador del estilo (movimientos,
// tecnicas, capsulas, voz) con el MODELO del cuerpo: mientras dura la partida del heroe, el registro de combate del
// estilo (char372 +212 + 20*forma: +2 ID cuyo modelo se carga, +4 su forma en char96) apunta al modelo del cuerpo
// (Vestir/Quitar; es lo mismo que el cambio de modelo B3->B3 nativo validado, pero en memoria y solo en esa partida).
// Frente 10 (studies 3): el MOVESET tambien es cambiable. Un donante (otro ID) aporta sus golpes al registro del
// estilo: los 8 fids ANM del moveset (char372 +0), el CAM (+0x20: combos y camara) y su banco de gritos (+0x24/+0x28),
// y sus capsulas por defecto (char96 +80) se suman a las del combate para que salgan sus especiales. Su modelo NO se
// toca (el donante se lee, nunca se escribe). Se elige en el carrusel del apartado del heroe (rpg.cpp) y se guarda en
// rpg.toml (heroe_moveset); se aplica en el combate siguiente (Vestir corre en cada Reglas/StartPropio).
// Bucle de runs: sagas de 3 combates encadenadas; cada combate ganado da experiencia, el nivel sube vida y dano.
// Lo usa rpg.cpp (vista kHeroeP y el reto kHeroe) con su candado puesto. Guardado: claves heroe_* de rpg.toml.
#include <rex/logging.h>
#include <rex/runtime.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "launcher/i18n.h"
#include "launcher/settings.h"
#include "modos.h"
#include "roster_ext.h"   // DonorOf: el heroe con ID propio esta montado en esta sesion
#include "mods.h"   // dbz3::ModsRoot (res/heroe/sagas.toml junto al exe)
#include "cac/cac.h"
#include "heroe_api.h"

namespace dbz3::pasarela {
int RangoMejora(const std::string& clave, int valor, int pe_por_paso, int pe_primera);   // pasarela.h
}

namespace dbz3::heroe {
namespace {
namespace M = dbz3::modos;
namespace T = dbz3::i18n;   // textos nuevos: T::T(es, en)
using M::L;

struct Fisico {
  int id, trajes;   // trajes: los de la tabla del select (0x8245F150) si ya esta en memoria; si no, estos
};
struct Raza {
  const char *es, *en, *fr, *de, *it;
  Fisico cuerpos[6];
  int n;
};
const Raza kRazas[] = {
    {"Saiyan", "Saiyan", "Saiyan", "Saiyajin", "Saiyan", {{0, 3}, {7, 2}, {39, 2}, {8, 2}}, 4},
    {"Humano", "Human", "Humain", "Mensch", "Umano", {{10, 2}, {12, 2}, {13, 2}, {15, 2}}, 4},
    {"Namekiano", "Namekian", "Namek", "Namekianer", "Namecciano", {{11, 2}}, 1},
    {"Raza de Freezer", "Frieza Race", "Race de Freezer", "Freezers Volk", "Razza di Freezer", {{27, 2}, {38, 2}}, 2},
    {"Majin", "Majin", "Majin", "Majin", "Majin", {{34, 2}, {36, 2}}, 2},
    {"Androide", "Android", "Androïde", "Androide", "Androide", {{30, 2}, {29, 2}, {28, 2}, {33, 2}}, 4},
    // G06a: IDs de catálogo, NO IDs de luchador; la geometría sale del creador UT.
    {"Saiyan (CaC UT)", "Saiyan (UT CaC)", "Saiyan (CaC UT)", "Saiyajin (UT CaC)", "Saiyan (UT CaC)",
     {{100, 17}, {101, 17}, {102, 17}, {103, 17}, {104, 17}, {105, 17}}, 6},
    // C4: cuerpos propios con piezas del creador de Xenoverse 2 (C6): cuerpo = raza (110..115), una sola constitucion
    {"Humano (piezas XV2)", "Human (XV2 parts)", "Humain (pièces XV2)", "Mensch (XV2-Teile)", "Umano (parti XV2)", {{110, 1}}, 1},
    {"Humana (piezas XV2)", "Human F (XV2 parts)", "Humaine (pièces XV2)", "Menschin (XV2-Teile)", "Umana (parti XV2)", {{111, 1}}, 1},
    {"Namekiano (piezas XV2)", "Namekian (XV2 parts)", "Namek (pièces XV2)", "Namekianer (XV2-Teile)", "Namecciano (parti XV2)", {{112, 1}}, 1},
    {"Raza de Freezer (XV2)", "Frieza Race (XV2 parts)", "Race de Freezer (XV2)", "Freezers Volk (XV2)", "Razza di Freezer (XV2)", {{113, 1}}, 1},
    {"Majin (piezas XV2)", "Majin (XV2 parts)", "Majin (pièces XV2)", "Majin (XV2-Teile)", "Majin (parti XV2)", {{114, 1}}, 1},
    {"Majin F (piezas XV2)", "Majin F (XV2 parts)", "Majin F (pièces XV2)", "Majin F (XV2-Teile)", "Majin F (parti XV2)", {{115, 1}}, 1},
};
constexpr int kNumRazas = int(std::size(kRazas));

// Estilo de lucha: el moveset de ese luchador. Los 6 primeros son maestros (bonus por las Reglas + una capsula de
// habilidad); el resto, su moveset tal cual.
struct Estilo {
  int id;
  int dano = 100, recibido = 100;   // %
  int vida = 0;                     // vida extra sobre 3000
  int ki = 0;                       // Reglas::ki_drena (< 0: el ki se recarga solo)
  int hab = 0;                      // capsula de habilidad "para todos" (0 = ninguna)
  const char* hab_n = nullptr;
  const char* tec = nullptr;        // sus tecnicas (nombres del juego en ingles)
};
// ponytail: nombres de tecnicas escritos a mano (en B3 son imagenes de data_usi, no texto); solo los de los
// maestros, que ya estaban validados. Leer los rotulos #AZT del jugador si hace falta para todos.
const Estilo kEstilos[] = {
    {0, 110, 100, 500, 0, 378, "Rage!", "Kamehameha, Warp Kamehameha"},
    {7, 120, 110, 0, 0, 385, "Serious!!", "Galick Gun, Final Flash"},
    {11, 100, 85, 500, 0, 381, "Spirit!", "Makankosappo, Destructive Wave"},
    {10, 100, 100, 0, -3, 445, "Concentration", "Kienzan, Kamehameha"},
    {27, 115, 100, 0, -2, 384, "Serious!", "Death Beam, Second Form"},
    {33, 100, 90, 1000, 0, 382, "Spirit!!", "Kamehameha, #17 Absorption"},
    {4}, {8}, {12}, {13}, {18}, {19}, {20}, {30}, {29}, {28}, {34}, {36}, {37}, {38}, {39}, {40}, {14},
};
constexpr int kNumEstilos = int(std::size(kEstilos));
constexpr int kNumMaestros = 6;
bool Desbloqueado(int id);   // s9: maestros, estilos y movesets que se abren con el progreso (abajo)

// Sagas del Modo Heroe (studies 7, frente 02). Datos en res/heroe/sagas.toml: el de la carpeta del usuario (heroe/sagas.toml
// en sus datos) manda; si no, el de junto al exe; si no se puede leer, las 4 sagas de codigo de abajo (las de siempre).
// Cada saga: nombre ES/EN, maestro que se entrena al completarla (indice de kEstilos, -1 ninguno), relevo Tag Team
// (maestro y combate k en que entra) y sus combates (rival, alternativos, vida, escenario, texto ES/EN).
struct Lucha {
  int rival = -1, alt = 43, alt2 = 42, vida = 1500, escenario = 0;
  std::string es, en, fr, de, it;   // texto antes del combate (fr/de/it: el ingles si faltan)
  // studies 8 H6: forma inicial del rival (fusiones nativas: 64 f3, 70 f4, 72 f2) y nota del guion de studies 6
  // (heroe_sagas.json): "jefe" (escena de jefe), "gigante" (rival a escala, cielo oscuro), "pierde" (la derrota es del
  // guion: la saga se da por completada), "maestro" (relevo). Texto de la escena de jefe (opcional).
  int forma = 0;
  std::string nota;
  std::string jefe_es, jefe_en;
};
struct SagaDef {
  std::string es, en, fr, de, it;
  std::vector<std::string> intro_es, intro_en, cierre_es, cierre_en;
  int maestro = -1;                  // se entrena al completar la saga (0..kNumMaestros-1); -1 ninguno
  int relevo = -1, relevo_k = 2;     // maestro de relevo (Tag Team) y el combate k en que entra
  std::vector<Lucha> combates;
};
// Codigo de respaldo (si falta res/heroe/sagas.toml): las 4 sagas de siempre.
struct PasoCodigo {
  int rival, alt, vida, escenario;
  const char *es, *en, *fr, *de, *it;
};
struct SagaCodigo {
  const char *es, *en, *fr, *de, *it;
  PasoCodigo p[3];
};
const SagaCodigo kSagasCodigo[] = {
    {"Saga Saiyan", "Saiyan Saga", "Saga Saiyan", "Saiyajin-Saga", "Saga dei Saiyan",
     {{42, 43, 1500, 5, "Unos Saibaman atacan la ciudad del Oeste. ¡Demuestra quién eres!",
       "Saibamen are attacking West City. Show them who you are!",
       "Des Saibaman attaquent la Capitale de l'Ouest. Montre-leur qui tu es !",
       "Saibaman greifen die Westliche Hauptstadt an. Zeig ihnen, wer du bist!",
       "Dei Saibaman attaccano la Città dell'Ovest. Mostra loro chi sei!"},
      {19, 18, 3000, 4, "Nappa ha visto tu fuerza y viene a por ti. ¡No te rindas!",
       "Nappa saw your strength and is coming for you. Don't give up!",
       "Nappa a vu ta force et vient te chercher. N'abandonne pas !",
       "Nappa hat deine Stärke gesehen und kommt zu dir. Gib nicht auf!",
       "Nappa ha visto la tua forza e viene a cercarti. Non arrenderti!"},
      {7, 18, 3500, 6, "El príncipe Vegeta en persona. ¡Es el combate decisivo!",
       "Prince Vegeta himself. This is the decisive fight!", "Le prince Vegeta en personne. C'est le combat décisif !",
       "Prinz Vegeta höchstpersönlich. Das ist der entscheidende Kampf!",
       "Il principe Vegeta in persona. È lo scontro decisivo!"}}},
    {"Saga de Namek", "Namek Saga", "Saga de Namek", "Namek-Saga", "Saga di Namek",
     {{21, 20, 2500, 3, "Las Fuerzas Especiales llegan a Namek. Recoome quiere jugar contigo.",
       "The Ginyu Force lands on Namek. Recoome wants to play with you.",
       "Les Forces Spéciales arrivent sur Namek. Recoome veut jouer avec toi.",
       "Die Ginyu-Truppe landet auf Namek. Recoome will mit dir spielen.",
       "Le Forze Speciali arrivano su Namek. Recoome vuole giocare con te."},
      {20, 21, 3000, 3, "El capitán Ginyu en persona. ¡Que no te robe el cuerpo!",
       "Captain Ginyu himself. Don't let him steal your body!",
       "Le capitaine Ginyu en personne. Ne le laisse pas voler ton corps !",
       "Kommandant Ginyu persönlich. Lass dir nicht den Körper stehlen!",
       "Il capitano Ginyu in persona. Non farti rubare il corpo!"},
      {27, 38, 4000, 2, "Freezer, el emperador del universo. ¡Todo Namek depende de ti!",
       "Frieza, emperor of the universe. All of Namek depends on you!",
       "Freezer, l'empereur de l'univers. Tout Namek compte sur toi !",
       "Freezer, der Herrscher des Universums. Ganz Namek zählt auf dich!",
       "Freezer, l'imperatore dell'universo. Tutto Namek dipende da te!"}}},
    {"Saga de los Androides", "Android Saga", "Saga des Cyborgs", "Androiden-Saga", "Saga dei Cyborg",
     {{30, 29, 2500, 1, "La Androide 18 bloquea la carretera. Es rápida: ¡vigila su guardia!",
       "Android 18 blocks the road. She's fast: watch her guard!",
       "C-18 bloque la route. Elle est rapide : surveille sa garde !",
       "C18 blockiert die Straße. Sie ist schnell: achte auf ihre Deckung!",
       "C-18 blocca la strada. È veloce: attento alla sua guardia!"},
      {43, 42, 3000, 0, "Cell ha dejado a sus hijos en el ring. ¡Acaba con los Cell Jr.!",
       "Cell left his children in the ring. Take down the Cell Jr.!",
       "Cell a laissé ses enfants sur le ring. Bats les Cell Junior !",
       "Cell hat seine Kinder im Ring gelassen. Besiege die Cell Jr.!",
       "Cell ha lasciato i suoi figli sul ring. Sconfiggi i Cell Jr.!"},
      {33, 28, 4500, 0, "Cell perfecto te espera en su torneo. ¡Es el combate decisivo!",
       "Perfect Cell awaits you in his tournament. This is the decisive fight!",
       "Cell parfait t'attend dans son tournoi. C'est le combat décisif !",
       "Der perfekte Cell erwartet dich in seinem Turnier. Das ist der entscheidende Kampf!",
       "Cell perfetto ti aspetta nel suo torneo. È lo scontro decisivo!"}}},
    {"Saga de Bu", "Buu Saga", "Saga de Boo", "Boo-Saga", "Saga di Majin Bu",
     {{37, 35, 3000, 7, "Dabura, el rey de los demonios, quiere tu energía para Bu.",
       "Dabura, king of demons, wants your energy for Buu.",
       "Dabura, le roi des démons, veut ton énergie pour Boo.",
       "Dabura, der Dämonenkönig, will deine Energie für Boo.",
       "Dabura, il re dei demoni, vuole la tua energia per Bu."},
      {34, 36, 4000, 6, "Bu se ha despertado. ¡No dejes que te convierta en chocolate!",
       "Buu has awakened. Don't let him turn you into chocolate!",
       "Boo s'est réveillé. Ne le laisse pas te changer en chocolat !",
       "Boo ist erwacht. Lass dich nicht in Schokolade verwandeln!",
       "Bu si è risvegliato. Non lasciare che ti trasformi in cioccolato!"},
      {36, 35, 5000, 5, "Bu el puro, la forma más peligrosa. ¡Es el combate final!",
       "Kid Buu, the most dangerous form. This is the final fight!",
       "Boo originel, la forme la plus dangereuse. C'est le combat final !",
       "Kid Boo, die gefährlichste Form. Das ist der letzte Kampf!",
       "Bu puro, la forma più pericolosa. È lo scontro finale!"}}},
};
constexpr int kNumSagasCodigo = int(std::size(kSagasCodigo));
constexpr int kRelevoCodigo[kNumSagasCodigo] = {11, 0, 11, 11};   // Saiyan: Piccolo; Namek: Goku; Androides: Piccolo; Buu: Piccolo
// Tercer rival de cada combate para cuando el rival y su alternativo chocan con el estilo y el cuerpo del heroe
// (p. ej. cuerpo Freezer 2 con el estilo de Freezer). Salen luchadores de la misma saga; al final, 42/43 (nunca son
// estilo ni cuerpo).
constexpr int kAlt2Codigo[kNumSagasCodigo][3] = {{43, 43, 19}, {43, 43, 20}, {28, 43, 30}, {43, 37, 43}};

std::vector<std::string> Lineas(const toml::table& t, std::string_view clave) {
  std::vector<std::string> v;
  const toml::node* n = t.get(clave);
  const toml::array* arr = n ? n->as_array() : nullptr;
  if (!arr) return v;
  for (const toml::node& e : *arr) {
    if (auto x = e.value<std::string>()) v.push_back(*x);
  }
  return v;
}
std::string Texto(const toml::table& t, std::string_view clave) { return t[clave].value_or(std::string()); }
void Completar(std::string& fr, std::string& de, std::string& it, const std::string& en) {
  if (fr.empty()) fr = en;
  if (de.empty()) de = en;
  if (it.empty()) it = en;
}
std::vector<SagaDef> SagasDeCodigo() {
  std::vector<SagaDef> out;
  for (int i = 0; i < kNumSagasCodigo; ++i) {
    const SagaCodigo& c = kSagasCodigo[i];
    SagaDef s;
    s.es = c.es, s.en = c.en, s.fr = c.fr, s.de = c.de, s.it = c.it;
    s.maestro = i % kNumMaestros;   // lo que hacia FinRun (g_saga % kNumMaestros) para las 4 de codigo
    s.relevo = kRelevoCodigo[i], s.relevo_k = 2;
    for (int k = 0; k < 3; ++k) {
      const PasoCodigo& p = c.p[k];
      Lucha l;
      l.rival = p.rival, l.alt = p.alt, l.alt2 = kAlt2Codigo[i][k], l.vida = p.vida, l.escenario = p.escenario;
      l.es = p.es, l.en = p.en, l.fr = p.fr, l.de = p.de, l.it = p.it;
      s.combates.push_back(std::move(l));
    }
    out.push_back(std::move(s));
  }
  return out;
}
std::vector<std::string> g_epilogo_es, g_epilogo_en;   // [epilogo] de sagas.toml (tras la ultima saga)
// Lee el TOML de sagas. Una saga sin combates validos o con datos fuera de rango se salta.
std::vector<SagaDef> LeerSagas(const std::filesystem::path& ruta) {
  std::vector<SagaDef> out;
  std::ifstream f(ruta, std::ios::binary);
  if (!f) return out;
  std::stringstream ss;
  ss << f.rdbuf();
  try {
    const toml::table t = toml::parse(ss.str());
    if (const toml::table* ep = t["epilogo"].as_table()) {   // studies 8 H6: tras la ultima saga
      g_epilogo_es = Lineas(*ep, "es");
      g_epilogo_en = Lineas(*ep, "en");
    }
    const toml::node* sn = t.get("saga");
    const toml::array* sagas = sn ? sn->as_array() : nullptr;
    if (!sagas) return out;
    for (const toml::node& el : *sagas) {
      const toml::table* st = el.as_table();
      if (!st) continue;
      SagaDef s;
      s.es = Texto(*st, "nombre_es"), s.en = Texto(*st, "nombre_en");
      s.fr = Texto(*st, "nombre_fr"), s.de = Texto(*st, "nombre_de"), s.it = Texto(*st, "nombre_it");
      Completar(s.fr, s.de, s.it, s.en);
      s.intro_es = Lineas(*st, "intro_es"), s.intro_en = Lineas(*st, "intro_en");
      s.cierre_es = Lineas(*st, "cierre_es"), s.cierre_en = Lineas(*st, "cierre_en");
      s.maestro = int((*st)["maestro"].value_or(int64_t(-1)));
      s.relevo = int((*st)["relevo"].value_or(int64_t(-1)));
      s.relevo_k = int((*st)["relevo_k"].value_or(int64_t(2)));
      bool ok = s.maestro < kNumMaestros && s.relevo < 64;
      const toml::node* cn = st->get("combate");
      const toml::array* cs = cn ? cn->as_array() : nullptr;
      if (cs) {
        for (const toml::node& ce : *cs) {
          const toml::table* ct = ce.as_table();
          if (!ct) {
            ok = false;
            break;
          }
          Lucha l;
          l.rival = int((*ct)["rival"].value_or(int64_t(-1)));
          l.alt = int((*ct)["alt"].value_or(int64_t(43)));
          l.alt2 = int((*ct)["alt2"].value_or(int64_t(42)));
          l.vida = int((*ct)["vida"].value_or(int64_t(1500)));
          l.escenario = int((*ct)["escenario"].value_or(int64_t(0)));
          l.es = Texto(*ct, "es"), l.en = Texto(*ct, "en");
          l.fr = Texto(*ct, "fr"), l.de = Texto(*ct, "de"), l.it = Texto(*ct, "it");
          Completar(l.fr, l.de, l.it, l.en);
          l.forma = int((*ct)["forma"].value_or(int64_t(0)));
          l.nota = Texto(*ct, "nota");
          l.jefe_es = Texto(*ct, "jefe_es"), l.jefe_en = Texto(*ct, "jefe_en");
          // 0..63, o un registro de fusion nativo (64..104) pedido con su forma (studies 8 H6)
          if (l.rival < 0 || (l.rival >= 64 && !(l.forma > 0 && l.rival < 105)) || l.forma < 0 || l.forma > 7) ok = false;
          s.combates.push_back(std::move(l));
        }
      }
      if (!ok || s.combates.empty()) {
        REXLOG_WARN("dbz3 heroe: saga '{}' descartada en {} (datos fuera de rango)", s.en, ruta.string());
        continue;
      }
      out.push_back(std::move(s));
    }
  } catch (const toml::parse_error&) {
    REXLOG_WARN("dbz3 heroe: {} no se puede leer", ruta.string());
    return {};
  }
  return out;
}
const std::vector<SagaDef>& Sagas() {
  static const std::vector<SagaDef> s = [] {
    for (const std::filesystem::path& ruta : {dbz3::settings::UserDataRoot() / "heroe" / "sagas.toml",
                                              dbz3::ModsRoot().parent_path() / "res" / "heroe" / "sagas.toml"}) {
      std::error_code ec;
      if (!std::filesystem::exists(ruta, ec)) continue;
      std::vector<SagaDef> v = LeerSagas(ruta);
      if (!v.empty()) {
        REXLOG_INFO("dbz3 heroe: {} sagas desde {}", v.size(), ruta.string());
        return v;
      }
      REXLOG_WARN("dbz3 heroe: {} sin sagas validas: se prueba la siguiente fuente", ruta.string());
    }
    REXLOG_INFO("dbz3 heroe: sin sagas.toml: {} sagas de codigo", kNumSagasCodigo);
    return SagasDeCodigo();
  }();
  return s;
}
int NumSagas() { return int(Sagas().size()); }
const SagaDef& SagaDe(int saga) {
  const std::vector<SagaDef>& s = Sagas();
  return s[size_t(std::max(saga, 0)) % s.size()];
}
const Lucha& LuchaDe(int saga, int k) {
  const std::vector<Lucha>& c = SagaDe(saga).combates;
  return c[size_t(std::clamp(k, 0, int(c.size()) - 1))];
}

int g_raza = 0, g_cuerpo = 0, g_traje = 0, g_estilo = 0;
int g_moveset = -1;   // frente 10: ID cuyo moveset (ANM+CAM+idioma) pelea el heroe; -1 = el del estilo
constexpr int kNivelMax = 20, kExpNivel = 3;   // nivel = 1 + exp / 3, hasta el 20
int g_exp = 0, g_saga = 0;   // combates ganados (todas las partidas) y saga siguiente (crece sin tope: vueltas)
// Frente 02 (studies 6): datos de creacion que faltaban (nombre, color de piel, voz y aura).
std::string g_nombre = "Heroe";
int g_color = 0, g_voz = 0, g_aura = 0;
std::string g_aura_col, g_ki_col;   // C4: color de aura / de ki desbloqueado ("" = el normal)
// Puntos de habilidad (PH): 1 por combate ganado y 2 mas al cerrar una saga. Se gastan en vida (+100) o dano (+2%).
int g_ph = 0, g_ph_vida = 0, g_ph_dano = 0;
int g_maestros = 0;   // bit i = maestro i entrenado (kEstilos[0..kNumMaestros-1]); lo lee el HUB y el Kit
constexpr int kPHVida = 100, kPHDano = 2;
// Maestro de relevo (Tag Team) y maestro entrenado de cada saga: en res/heroe/sagas.toml (SagaDef).
// Si el heroe pelea con ese mismo ID, no entra (seria un espejo).
// Entrenamiento por maestro (UT: % de entrenamiento). g_entreno[i] 0..100; 100 entrena el maestro i.
int g_entreno[kNumMaestros] = {};
std::vector<std::string> g_desbloqueos;   // C3: ids conseguidos con los maestros (los guarda la ficha, cac.h)
int g_esferas = 0, g_deseos = 0;   // C3: Esferas del Dragon recogidas (mascara de 7) y deseos de Shenron pendientes
bool g_deseo_sesion = false;      // un deseo de cambio de raza/cuerpo esta abierto (hasta guardar)
bool g_raza_cerrada = false;      // el ultimo intento de cambiar raza/cuerpo se rechazo
int g_bloqueo_maestro = 0;   // 1 si el ultimo intento de cambiar de maestro no se pudo (60% del actual)

// ---- Memoria del guest (solo imagen US: firma "_charasel.cpp", como roster_ext.cpp) ----
constexpr uint32_t kChar372 = 0x82329CF0;
constexpr uint32_t kChar96 = 0x8234ABB8;   // registro por ID (96 B): +80 = lista de capsulas por defecto (n + 7 u16)
constexpr uint32_t kIdToSlot = 0x82020668;
constexpr uint32_t kCostumeCounts = 0x8245F150;
constexpr uint32_t kSignature = 0x82020700;
uint8_t* Base() {
  rex::Runtime* rt = rex::Runtime::instance();
  uint8_t* b = rt ? rt->virtual_membase() : nullptr;
  return b && std::memcmp(b + kSignature, "_charasel.cpp", 13) == 0 ? b : nullptr;
}
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }

void W32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v); }
void W16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 8), p[1] = uint8_t(v); }
// Cara del marcador (como tagteam.cpp): filas de 16 B (ID, mascara de trajes, fichero de caras, ficha; fin = ID -1).
// Al empezar el combate el juego busca la fila del jugador (sub_820E6D70) y carga su hoja de caras y su retrato.
constexpr uint32_t kFaceTable = 0x82324468;
uint32_t FilaCara(uint8_t* b, uint32_t id, uint32_t traje) {
  uint32_t any = 0;
  for (uint32_t row = kFaceTable; row < kFaceTable + 16 * 256; row += 16) {
    const uint32_t rid = Be32(b + row);
    if (rid == 0xFFFFFFFFu) break;
    if (rid != id) continue;
    if (Be32(b + row + 4) & (1u << (traje & 31))) return row;
    if (!any) any = row;
  }
  return any;
}

struct Puesto {   // lo original del estilo mientras lleva el cuerpo (y el moveset) del heroe
  int estilo = -1, cuerpo = -1, traje = -1, moveset = -1;
  uint8_t c372[372];
  std::vector<std::pair<uint32_t, uint32_t>> caras;   // fila de la tabla de caras -> su fichero original
} g_puesto;

// Aura elegida en la creacion (fila Aura): tipo de aura de la forma base del estilo. Mismas filas que roster_ext
// (aura_tipos_forma): 0x82321A38 + 40*ID + 5*forma, byte 0 = tipo (el juego dibuja 2..7). 0 = la del estilo,
// 1 = sin aura, 2..7 = la fila de Goku de la forma 0..5 (normal, kaioken, SSJ, SSJ2, SSJ3, SSJ4).
constexpr uint32_t kAuraTipo = 0x82321A38;
constexpr int kNumAuras = 8;
struct AuraPuesta { int estilo = -1, aura = 0; uint8_t orig[5]; } g_aura_puesta;
void QuitarAura() {
  if (g_aura_puesta.estilo < 0) return;
  if (uint8_t* b = Base()) std::memcpy(b + kAuraTipo + 40u * uint32_t(g_aura_puesta.estilo), g_aura_puesta.orig, 5);
  g_aura_puesta.estilo = -1;
}
void PonerAuraEstilo(int estilo, int aura) {
  if (g_aura_puesta.estilo == estilo && g_aura_puesta.aura == aura) return;
  QuitarAura();
  uint8_t* b = Base();
  if (!b || aura <= 0 || estilo < 0 || estilo >= 64) return;
  uint8_t* dst = b + kAuraTipo + 40u * uint32_t(estilo);
  std::memcpy(g_aura_puesta.orig, dst, 5);
  if (aura == 1) std::memset(dst, 0, 5);
  else std::memcpy(dst, b + kAuraTipo + 5u * uint32_t(aura - 2), 5);
  g_aura_puesta.estilo = estilo, g_aura_puesta.aura = aura;
  REXLOG_INFO("dbz3 heroe: aura {} puesta al estilo {} (tipo {})", aura, estilo, dst[0]);
}

void Quitar() {
  if (g_puesto.estilo < 0) return;
  if (uint8_t* b = Base()) {
    std::memcpy(b + kChar372 + uint32_t(g_puesto.estilo) * 372, g_puesto.c372, 372);
    for (const auto& [row, fid] : g_puesto.caras) W32(b + row + 8, fid);
    REXLOG_INFO("dbz3 heroe: estilo {} vuelve a su modelo", g_puesto.estilo);
  }
  g_puesto.caras.clear();
  g_puesto.estilo = g_puesto.cuerpo = g_puesto.traje = g_puesto.moveset = -1;
}

// Frente 10: un ID sirve de moveset donante si tiene registro de combate con moveset en la forma base (su fid #AMB
// de char372 +0; 0 y 0xFFFFFFFF son "sin moveset", como en los recortados 22-26/31). Los IDs de mods (>= 44) valen
// cuando su mod esta instalado (roster_ext ya relleno su char372). El donante solo se LEE.
bool MovesetValido(int id) {
  if (id < 0 || id >= 64) return false;
  uint8_t* b = Base();
  if (!b) return false;
  const uint32_t fid = Be32(b + kChar372 + uint32_t(id) * 372);
  return fid != 0 && fid != 0xFFFFFFFFu;
}
// Carrusel del moveset (rpg.cpp, fila Moveset / X-Y entre combates): -1 (el del estilo) y los donantes validos en
// orden de ID, saltando el propio estilo (elegirlo no haria nada: su registro ya es el del estilo).
int MovesetCiclo(int d) {
  int lista[65], n = 0;
  const int estilo = kEstilos[g_estilo].id;
  lista[n++] = -1;
  for (int id = 0; id < 64 && n < 65; ++id) {
    if (id != estilo && MovesetValido(id) && Desbloqueado(id)) lista[n++] = id;
  }
  int k = 0;
  for (int i = 0; i < n; ++i) {
    if (lista[i] == g_moveset) {
      k = i;
      break;
    }
  }
  return lista[(k + d % n + n) % n];
}

// El luchador `estilo` carga en combate el modelo de `cuerpo` (su forma base) y su cara en el marcador, con el
// moveset del donante si hay uno elegido (frente 10).
// Transformaciones: el cuerpo no tiene modelo (ni cara, ni aura) para las formas del estilo, asi que con otro cuerpo
// las formas 1+ piden una capsula que el estilo no puede llevar (char372 +212+20f = capsula que exige la forma):
// el heroe pelea en su forma base, sin transformaciones a medias. Con su propio cuerpo, todo como siempre.
// ponytail: la voz y los gritos siguen siendo los del estilo: el banco de gritos (char372 +0x24, lang_*) va por
// indices del moveset (los ports reconstruyen el banco del donante con gritos.py); el del cuerpo tiene otro orden
// y otro numero (55/56) y podria pedir un sonido que no existe. Altura: ver Altura().
void Vestir(int estilo, int cuerpo, int traje) {
  if (estilo == cuerpo || estilo < 0 || estilo >= 44 || cuerpo < 0 || cuerpo >= 44) return Quitar();
  const int donante = (g_moveset >= 0 && g_moveset != estilo && MovesetValido(g_moveset)) ? g_moveset : -1;
  if (g_puesto.estilo == estilo && g_puesto.cuerpo == cuerpo && g_puesto.traje == traje &&
      g_puesto.moveset == donante) {
    return;
  }
  Quitar();
  uint8_t* b = Base();
  if (!b) return;
  uint8_t* r = b + kChar372 + uint32_t(estilo) * 372;
  const uint8_t* rc = b + kChar372 + uint32_t(cuerpo) * 372;
  if (!Be32(r) || !Be32(rc)) return;   // sin registro de combate (recortados)
  std::memcpy(g_puesto.c372, r, 372);
  if (donante >= 0) {   // moveset del donante: sus 8 ANM por forma, su CAM (combos/camara) y su idioma (gritos)
    const uint8_t* rd = b + kChar372 + uint32_t(donante) * 372;
    for (uint32_t f = 0; f < 8; ++f) W32(r + 4 * f, Be32(rd + 4 * f));
    W32(r + 0x20, Be32(rd + 0x20));
    W32(r + 0x24, Be32(rd + 0x24));
    W32(r + 0x28, Be32(rd + 0x28));
  }
  const uint32_t nf = std::clamp<uint32_t>(Be32(r + 0xD0), 1, 8);
  const uint32_t tapon = estilo == 27 ? 6 : 96;   // capsula de otro (Kamehameha de Goku / Second Form de Freezer)
  for (uint32_t f = 0; f < nf; ++f) {
    std::memcpy(r + 212 + 20 * f + 2, rc + 212 + 2, 4);   // ID del modelo + su forma: la base del cuerpo
    if (f > 0) W16(r + 212 + 20 * f, tapon);
  }
  if (const uint32_t fila = FilaCara(b, uint32_t(cuerpo), uint32_t(traje))) {
    const uint32_t cara = Be32(b + fila + 8);
    for (uint32_t row = kFaceTable; row < kFaceTable + 16 * 256 && Be32(b + row) != 0xFFFFFFFFu; row += 16) {
      if (Be32(b + row) != uint32_t(estilo)) continue;
      g_puesto.caras.emplace_back(row, Be32(b + row + 8));
      W32(b + row + 8, cara);
    }
  }
  g_puesto.estilo = estilo;
  g_puesto.cuerpo = cuerpo;
  g_puesto.traje = traje;
  g_puesto.moveset = donante;
  REXLOG_INFO("dbz3 heroe: estilo {} ({}) con el cuerpo {} ({}) traje {}, {} formas (1+ bloqueadas), {} filas de cara",
              estilo, M::Nombre(estilo), cuerpo, M::Nombre(cuerpo), traje + 1, nf, g_puesto.caras.size());
  if (donante >= 0) {
    REXLOG_INFO("dbz3 heroe: moveset {} ({}) para el estilo {} en este combate", donante, M::Nombre(donante), estilo);
  }
}

const Fisico& CuerpoAct() { return kRazas[g_raza].cuerpos[g_cuerpo]; }
// Trajes del cuerpo: la tabla del select por casilla (la que llena el juego, con los trajes de mods), o los fijos.
int TrajesDe(const Fisico& c) {
  if (c.id >= 100 && c.id <= 105) return 17;   // G06a: apariencias originales UT
  if (c.id >= 110 && c.id <= 115) return 1;    // C4: XV2: las piezas hacen de traje
  if (uint8_t* b = Base()) {
    const uint32_t slot = uint32_t(b[kIdToSlot + c.id * 2]) << 8 | b[kIdToSlot + c.id * 2 + 1];
    if (slot < 38 && b[kCostumeCounts + slot] >= 1 && b[kCostumeCounts + slot] <= 8) return b[kCostumeCounts + slot];
  }
  return c.trajes;
}
int Ciclo(int v, int d, int n) { return (v + d + n) % n; }
void Ajustar() {
  if (!Xv2PiezasActivas() && g_raza >= 7) {   // C8b: sin XV2 (u opcion apagada) las razas XV2 no existen: heroe nativo de B3
    static bool avisado = false;
    if (!avisado) {
      avisado = true;
      if (!Xv2Instalado())
        REXLOG_WARN("dbz3 heroe: Xenoverse 2 no instalado: el creador usa el heroe base de B3 (cuerpos nativos)");
      else
        REXLOG_INFO("dbz3 heroe: piezas de Xenoverse 2 desactivadas (opcion del launcher): heroe base de B3");
    }
    g_raza = 0;
  }
  g_raza = std::clamp(g_raza, 0, kNumRazas - 1);
  g_cuerpo = std::clamp(g_cuerpo, 0, kRazas[g_raza].n - 1);
  g_traje = std::clamp(g_traje, 0, TrajesDe(CuerpoAct()) - 1);
  g_estilo = std::clamp(g_estilo, 0, kNumEstilos - 1);
  g_moveset = std::clamp(g_moveset, -1, 63);   // validez del donante: MovesetValido (mods instalados) al aplicarlo
}
const Estilo& EstiloDeId(int id) {
  for (const Estilo& e : kEstilos) {
    if (e.id == id) return e;
  }
  static const Estilo nada{-1};
  return nada;
}
bool EsMaestroId(int id) {
  for (int i = 0; i < kNumMaestros; ++i) {
    if (kEstilos[i].id == id) return true;
  }
  return false;
}
std::string Pct(int v) { return (v > 0 ? "+" : "") + std::to_string(v) + "%"; }
std::filesystem::path PropioPath() { return dbz3::settings::UserDataRoot() / "hub_heroe.toml"; }
}  // namespace
int Nivel();

// C8: sin Xenoverse 2 no hay piezas XV2. Se comprueba igual que xv2/xv2_leer.py: DBZ3_XENO2_CPK manda (si apunta a una
// ruta que no existe, XV2 cuenta como no instalado) y, sin la variable, la ruta habitual y las bibliotecas de Steam.
// C8b: las piezas XV2 son una OPCION (launcher, apagada por defecto). DBZ3_CAC_XV2=1/0 permite forzarla en pruebas.
bool Xv2PiezasActivas() {
  if (const char* e = std::getenv("DBZ3_CAC_XV2"); e && *e) {
    const std::string v(e);
    return !(v == "0" || v == "false" || v == "off" || v == "no");
  }
  return Xv2Instalado() && dbz3::settings::CacXv2Piezas();
}

bool Xv2Instalado() {
  static const bool ok = [] {
    namespace fs = std::filesystem;
    auto hay = [](const fs::path& p) {
      std::error_code ec;
      return !p.empty() && fs::is_regular_file(p, ec);
    };
    if (const char* e = std::getenv("DBZ3_XENO2_CPK"); e && *e) return hay(fs::u8path(e));
    std::vector<fs::path> bases;
    for (char d = 'C'; d <= 'J'; ++d) {
      bases.emplace_back(std::string(1, d) + ":/SteamLibrary");
      bases.emplace_back(std::string(1, d) + ":/Steam");
    }
    bases.emplace_back("C:/Program Files (x86)/Steam");
    bases.emplace_back("C:/Program Files/Steam");
    for (const fs::path& b : bases) {
      if (hay(b / "steamapps" / "common" / "DB Xenoverse 2" / "cpk" / "data.cpk")) return true;
    }
    // libraryfolders.vdf: bibliotecas de Steam en otras unidades
    for (const fs::path& b : {fs::path("C:/Program Files (x86)/Steam"), fs::path("C:/Program Files/Steam")}) {
      std::ifstream f(b / "steamapps" / "libraryfolders.vdf");
      std::string linea;
      while (std::getline(f, linea)) {
        const auto p = linea.find("\"path\"");
        if (p == std::string::npos) continue;
        const auto a = linea.find('"', p + 6), z = linea.find('"', a + 1);
        if (a == std::string::npos || z == std::string::npos) continue;
        if (hay(fs::u8path(linea.substr(a + 1, z - a - 1)) / "steamapps" / "common" / "DB Xenoverse 2" / "cpk" /
                "data.cpk"))
          return true;
      }
    }
    return false;
  }();
  return ok;
}
   // definido mas abajo (la traza del guardado propio lo usa)

// Guardado PROPIO del Modo Heroe (hub_heroe.toml): el modo ya no depende de rpg.toml ni del Kumite. Se lee despues
// de rpg.toml (si existe, manda) y se reescribe en cada guardado; las claves de rpg.toml se siguen escribiendo para
// que una partida antigua no se pierda. El prefijo hub_ hace que el HUB enseñe su aviso de guardado.
void LeerPropio() {
  std::ifstream f(PropioPath(), std::ios::binary);
  if (!f) return;
  std::stringstream ss;
  ss << f.rdbuf();
  try {
    const toml::table t = toml::parse(ss.str());
    if (int(t["version"].value_or(int64_t(0))) != 1) return;
    g_raza = int(t["heroe_raza"].value_or(int64_t(g_raza)));
    g_cuerpo = int(t["heroe_cuerpo"].value_or(int64_t(g_cuerpo)));
    g_traje = int(t["heroe_traje"].value_or(int64_t(g_traje)));
    g_estilo = int(t["heroe_estilo"].value_or(int64_t(g_estilo)));
    g_moveset = int(t["heroe_moveset"].value_or(int64_t(g_moveset)));
    g_exp = std::max(0, int(t["heroe_exp"].value_or(int64_t(g_exp))));
    g_saga = std::max(0, int(t["heroe_saga"].value_or(int64_t(g_saga))));
  g_nombre = t["heroe_nombre"].value_or(std::string("Heroe"));
  g_color = int(t["heroe_color"].value_or(int64_t(0)));
  g_voz = int(t["heroe_voz"].value_or(int64_t(0)));
  g_aura = int(t["heroe_aura"].value_or(int64_t(0)));
    g_ph = std::max(0, int(t["heroe_ph"].value_or(int64_t(g_ph))));
    g_ph_vida = std::max(0, int(t["heroe_ph_vida"].value_or(int64_t(g_ph_vida))));
    g_ph_dano = std::max(0, int(t["heroe_ph_dano"].value_or(int64_t(g_ph_dano))));
    g_maestros = int(t["heroe_maestros"].value_or(int64_t(g_maestros))) & ((1 << kNumMaestros) - 1);
    if (const toml::node* e = t.get("heroe_entreno")) {
      if (const toml::array* arr = e->as_array()) {
        int i = 0;
        for (const toml::node& v : *arr) {
          if (i < kNumMaestros) g_entreno[i++] = std::clamp(int(v.value_or(int64_t(0))), 0, 100);
        }
      }
    }
    REXLOG_INFO("dbz3 heroe: guardado propio {} (nivel {}, saga {}, {} PH, maestros {:02X})", PropioPath().string(),
                Nivel(), g_saga, g_ph, g_maestros);
  } catch (const toml::parse_error&) {
    REXLOG_WARN("dbz3 heroe: {} no se puede leer: se usa rpg.toml", PropioPath().string());
  }
}

void EscribirPropio() {
  std::error_code ec;
  const auto path = PropioPath();
  if (path.empty()) return;
  const auto tmp = path.string() + ".tmp";
  std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
  if (!o) return;
  o << "# Modo Heroe (src/heroe.cpp): guardado propio del modo, separado de la partida del juego.\n"
       "version = 1\n"
    << "heroe_raza = " << g_raza << "\nheroe_cuerpo = " << g_cuerpo << "\nheroe_traje = " << g_traje
    << "\nheroe_estilo = " << g_estilo << "\nheroe_moveset = " << g_moveset << "\nheroe_exp = " << g_exp
    << "\nheroe_saga = " << g_saga << "\nheroe_nombre = \"" << g_nombre << "\"\nheroe_color = " << g_color << "\nheroe_voz = " << g_voz << "\nheroe_aura = " << g_aura << "\nheroe_ph = " << g_ph << "\nheroe_ph_vida = " << g_ph_vida
    << "\nheroe_ph_dano = " << g_ph_dano << "\nheroe_maestros = " << g_maestros << "\nheroe_entreno = ["
    << g_entreno[0] << ", " << g_entreno[1] << ", " << g_entreno[2] << ", " << g_entreno[3] << ", "
    << g_entreno[4] << ", " << g_entreno[5] << "]\n";
  o.close();
  if (!o) return;
  std::filesystem::rename(tmp, path, ec);
  if (ec) REXLOG_WARN("dbz3 heroe: no se pudo escribir {} ({})", path.string(), ec.message());
}

// Pruebas automaticas (studies 7): hooks `DBZ3_TEST_HEROE_*` para fijar la creacion del heroe sin depender de las
// pulsaciones del arnes (la primera tecla a veces se pierde y el cursor no cae en la fila que toca). Solo se leen al
// cargar el heroe (Leer); sin las variables no cambia nada. Ver `studies 7\ENTREGAS\01_cac_en_juego\RUNBOOK.md`.
int HookEntero(const char* nombre, int actual, int minimo, int maximo) {
  const char* v = std::getenv(nombre);
  if (!v || !*v) return actual;
  return std::clamp(std::atoi(v), minimo, maximo);
}
void AplicarHooksDePrueba() {
  const bool alguno = std::getenv("DBZ3_TEST_HEROE_RAZA") || std::getenv("DBZ3_TEST_HEROE_CUERPO") ||
                      std::getenv("DBZ3_TEST_HEROE_TRAJE") || std::getenv("DBZ3_TEST_HEROE_ESTILO") ||
                      std::getenv("DBZ3_TEST_HEROE_MOVESET") || std::getenv("DBZ3_TEST_HEROE_AURA") ||
                      std::getenv("DBZ3_TEST_HEROE_PH") || std::getenv("DBZ3_TEST_HEROE_SAGA") ||
                      std::getenv("DBZ3_TEST_HEROE_MAESTROS") || std::getenv("DBZ3_TEST_HEROE_ENTRENO") ||
                      std::getenv("DBZ3_TEST_HEROE_ESFERAS");
  if (!alguno) return;
  // studies 8: saga de partida (H6: cierre de la ultima), maestros entrenados (mascara) y % de entrenamiento del
  // maestro 0..5 (formato "i:pct", H4)
  g_saga = HookEntero("DBZ3_TEST_HEROE_SAGA", g_saga, 0, 99);
  g_maestros = HookEntero("DBZ3_TEST_HEROE_MAESTROS", g_maestros, 0, (1 << kNumMaestros) - 1);
  if (const char* e = std::getenv("DBZ3_TEST_HEROE_ENTRENO")) {
    const int i = std::atoi(e);
    const char* c = std::strchr(e, ':');
    if (i >= 0 && i < kNumMaestros && c) g_entreno[i] = std::clamp(std::atoi(c + 1), 0, 100);
  }
  if (std::getenv("DBZ3_TEST_HEROE_ESFERAS")) {   // mascara de Esferas recogidas; 127 = las 7 -> un deseo
    g_esferas = HookEntero("DBZ3_TEST_HEROE_ESFERAS", g_esferas, 0, 127);
    if (g_esferas == 127) {
      g_esferas = 0;
      g_deseos = std::max(g_deseos, 1);
    }
  }
  g_raza = HookEntero("DBZ3_TEST_HEROE_RAZA", g_raza, 0, kNumRazas - 1);
  g_cuerpo = HookEntero("DBZ3_TEST_HEROE_CUERPO", g_cuerpo, 0, 15);   // Ajustar() lo lleva al cuerpo valido
  g_traje = HookEntero("DBZ3_TEST_HEROE_TRAJE", g_traje, 0, 16);
  g_estilo = HookEntero("DBZ3_TEST_HEROE_ESTILO", g_estilo, 0, kNumEstilos - 1);
  g_moveset = HookEntero("DBZ3_TEST_HEROE_MOVESET", g_moveset, -1, 400);
  g_aura = HookEntero("DBZ3_TEST_HEROE_AURA", g_aura, 0, kNumAuras - 1);
  g_ph = HookEntero("DBZ3_TEST_HEROE_PH", g_ph, 0, 99);
  REXLOG_INFO("dbz3 heroe: prueba: creacion forzada (raza {}, cuerpo {}, traje {}, estilo {}, moveset {}, aura {}, PH {}, "
              "saga {}, maestros {:02X})",
              g_raza, g_cuerpo, g_traje, g_estilo, g_moveset, g_aura, g_ph, g_saga, g_maestros);
}

void Leer(const toml::table& t) {
  g_raza = int(t["heroe_raza"].value_or(int64_t(0)));
  g_cuerpo = int(t["heroe_cuerpo"].value_or(int64_t(0)));
  g_traje = int(t["heroe_traje"].value_or(int64_t(0)));
  g_estilo = int(t["heroe_estilo"].value_or(t["heroe_maestro"].value_or(int64_t(0))));   // antes: maestro (0-5)
  g_moveset = int(t["heroe_moveset"].value_or(int64_t(-1)));   // frente 10: donante del moveset (-1: el del estilo)
  g_exp = std::max(0, int(t["heroe_exp"].value_or(int64_t(0))));
  g_saga = std::max(0, int(t["heroe_saga"].value_or(int64_t(0))));
  g_ph = std::max(0, int(t["heroe_ph"].value_or(int64_t(0))));
  g_ph_vida = std::max(0, int(t["heroe_ph_vida"].value_or(int64_t(0))));
  g_ph_dano = std::max(0, int(t["heroe_ph_dano"].value_or(int64_t(0))));
  g_maestros = int(t["heroe_maestros"].value_or(int64_t(0))) & ((1 << kNumMaestros) - 1);
  LeerPropio();   // su guardado manda sobre las claves de rpg.toml
  AplicarHooksDePrueba();
  Ajustar();
}
void Escribir(std::ostream& o) {
  o << "heroe_raza = " << g_raza << "\nheroe_cuerpo = " << g_cuerpo << "\nheroe_traje = " << g_traje
    << "\nheroe_estilo = " << g_estilo << "\nheroe_maestro = " << std::min(g_estilo, kNumMaestros - 1)
    << "\nheroe_moveset = " << g_moveset << "\nheroe_exp = " << g_exp << "\nheroe_saga = " << g_saga
    << "\nheroe_ph = " << g_ph << "\nheroe_ph_vida = " << g_ph_vida << "\nheroe_ph_dano = " << g_ph_dano
    << "\nheroe_maestros = " << g_maestros << "\n";
  EscribirPropio();
}

int Cuerpo() { return CuerpoAct().id; }
std::string NombreCuerpo() {   // G06a: no mostrar Goku/Vegeta como cuerpo del creador
  if (CuerpoXv2(Cuerpo())) return L(kRazas[g_raza].es, kRazas[g_raza].en, kRazas[g_raza].fr, kRazas[g_raza].de, kRazas[g_raza].it);
  if (Cuerpo() < 100 || Cuerpo() > 105) return M::Nombre(Cuerpo());
  const int k = Cuerpo() - 100;
  const char* constitucion = k < 2 ? T::T("Ligera", "Light") : k < 4 ? T::T("Estándar", "Standard")
                                                                      : T::T("Pesada", "Heavy");
  return std::string(constitucion) + " / " + T::T("Cara ", "Face ") + (k % 2 ? "B" : "A") + " (UT)";
}

// K: huella del conversor del heroe XV2 (tamano + fecha de los scripts que deciden su modelo). Va en la ficha: si cambia el
// codigo (o se instala otra version) el heroe del jugador se vuelve a montar solo, sin tocar la ficha.
static std::string HuellaConversor() {
  static const std::string h = [] {
    namespace fs = std::filesystem;
    const fs::path r = dbz3::ModsRoot().parent_path();
    uint64_t x = 1469598103934665603ull;
    for (const char* f : {"mod center hd/xv2/xv2_cac_b3.py", "mod center hd/xv2/xv2_a_b3.py", "mod center hd/xv2/xv2_leer.py",
                          "mod center hd/cac/vista_heroe.py", "awo_tools/sdbh_model.py"}) {
      std::error_code ec;
      const auto n = fs::file_size(r / f, ec);
      const auto t = fs::last_write_time(r / f, ec);
      if (ec) continue;
      x = (x ^ uint64_t(n)) * 1099511628211ull;
      x = (x ^ uint64_t(t.time_since_epoch().count())) * 1099511628211ull;
    }
    char b[24];
    std::snprintf(b, sizeof(b), "%u", uint32_t(x ^ (x >> 32)));   // decimal: leer_ficha lo lee como entero
    return std::string(b);
  }();
  return h;
}

// C4: catalogo de piezas XV2 (res/datos/xv2_cac.toml, el de C6): las 3 primeras de cada tipo que existen de verdad en esa raza.
std::vector<int> PiezasXv2(int cuerpo, int tipo) {
  static const char* kRaza[6] = {"HUM", "HUF", "NMC", "FRI", "MAM", "MAF"};
  static const char* kTipo[7] = {"Hair", "FaceBase", "Bust", "Pants", "Boots", "Rist", "FaceEye"};
  static std::map<int, std::vector<int>> cache;
  static std::mutex mu;
  if (!CuerpoXv2(cuerpo) || tipo < 0 || tipo > 6) return {};
  std::lock_guard<std::mutex> lk(mu);
  const int clave = (cuerpo - 110) * 8 + tipo;
  if (const auto it = cache.find(clave); it != cache.end()) return it->second;
  std::vector<int> out;
  try {
    const auto t = toml::parse_file((dbz3::ModsRoot().parent_path() / "res" / "datos" / "xv2_cac.toml").string());
    std::set<int> modelos;
    if (const auto* ps = t["raza"][kRaza[cuerpo - 110]]["parte"].as_array()) {
      for (const auto& n : *ps) {
        const auto* e = n.as_table();
        if (!e || (*e)["tipo"].value_or(std::string()) != kTipo[tipo]) continue;
        const auto ex = (*e)["existe"];
        if (!(ex.is_boolean() ? ex.value_or(false) : ex.value_or(int64_t{0}) != 0)) continue;
        modelos.insert(int((*e)["modelo"].value_or(int64_t{0})));
      }
    }
    for (int m : modelos)
      if (m != 0 && m != 290 && out.size() < 8) out.push_back(m);
    if (out.empty() && !modelos.empty()) out.push_back(*modelos.begin());
  } catch (const std::exception&) {
  }
  cache[clave] = out;
  return out;
}
const char* NombreTipoXv2(int tipo) {
  static const char* es[7] = {"Pelo", "Cara", "Torso", "Piernas", "Botas", "Guantes", "Ojos"};
  static const char* en[7] = {"Hair", "Face", "Torso", "Legs", "Boots", "Gloves", "Eyes"};
  const int k = std::clamp(tipo, 0, 6);
  return T::T(es[k], en[k]);
}
int Traje() { return g_traje; }

namespace C = dbz3::cac;
// studies 8 fase 2 (H2, heroe_api.h): los valores de la ficha activa que cambian el modelo, en el texto que entiende
// vista_heroe.py --ficha (y que se guarda en mods/heroe_fichaN/ficha.txt al montar su traje de combate).
std::string ArgsFicha(int id, int traje) {
  const C::Heroe& h = C::Slot(C::SlotActivo());
  auto hsv = [](const C::Hsv& c) { return std::to_string(c.h) + "," + std::to_string(c.s) + "," + std::to_string(c.v); };
  std::string s = "cuerpo=" + std::to_string(id) + ";traje=" + std::to_string(traje) + ";partes=";
  const int nz = CuerpoXv2(id) ? 6 : 4;   // XV2: piel|pelo|torso|piernas|botas|guantes; UT: Part 1..4
  for (int i = 0; i < nz; ++i) s += (i ? "|" : "") + hsv(h.parte[i]);
  if (CuerpoXv2(id)) {   // contrato de C6: xv2=bust:..,pants:..,boots:..,rist:..,cara:..,pelo:..
    auto pz = [&](int t) { const auto v = PiezasXv2(id, t); return h.xv2[t] ? h.xv2[t] : (v.empty() ? 1 : v[0]); };
    s += ";xv2=bust:" + std::to_string(pz(2)) + ",pants:" + std::to_string(pz(3)) + ",boots:" + std::to_string(pz(4)) +
         ",rist:" + std::to_string(pz(5)) + ",cara:" + std::to_string(pz(1)) + ",pelo:" + std::to_string(pz(0)) + ",ojos:" + std::to_string(pz(6)) + ";cv=" + HuellaConversor();
  }
  s += ";pelo=" + std::to_string(h.pelo) + ";c1=" + hsv(h.pelo_c1) + ";c2=" + hsv(h.pelo_c2) +
       ";logo=" + std::to_string(h.logo) + ";scouter=" + std::to_string(h.scouter);
  static constexpr const char* kB3Zone[] = {"torso", "piernas", "guantes", "botas"};
  for (int i = 0; i < 4; ++i)
    if (h.b3_piezas[i] >= 0) s += ";b3_" + std::string(kB3Zone[i]) + "=" + std::to_string(h.b3_piezas[i]);
  if (h.ropa > 0) s += ";ropa=" + std::to_string(h.ropa);   // studies 9b: prenda de Xenoverse (vista_heroe.py PRENDAS)
  if (!g_aura_col.empty()) s += ";aura_col=" + g_aura_col;   // C4: colores de aura / ki -> personaje.toml del heroe propio
  if (!g_ki_col.empty()) s += ";ki_col=" + g_ki_col;
  if (id >= 100 && id <= 105) s += ";ut=4";   // G06a: invalida tiras viejas del conversor; vista_heroe lo ignora
  return s;
}
bool FichaNativa() {
  if (CuerpoPropio(Cuerpo())) return false;   // G06a: siempre usar su malla propia
  const C::Heroe& h = C::Slot(C::SlotActivo());
  auto cero = [](const C::Hsv& c) { return c.h == 0 && c.s == 0 && c.v == 0; };
  for (const auto& c : h.parte)
    if (!cero(c)) return false;
  for (int fid : h.b3_piezas)
    if (fid >= 0) return false;
  return h.pelo == 0 && cero(h.pelo_c1) && cero(h.pelo_c2) && h.logo == 0 && h.scouter == 0 && h.ropa == 0;
}
int FichaNumero() { return C::SlotActivo() + 1; }

// ---- Heroe con ID propio (studies 9): el heroe de la ficha activa como PERSONAJE NUEVO ----
// vista_heroe.py --combate monta mods/heroe_propio (personaje.toml de roster_build: ID pedido 63, el modelo ya con pelo,
// logo y colores, el nombre de la ficha, moveset/tecnicas/aura/voz del donante = el estilo). roster_build lo instala al
// arrancar el juego (JUGAR) y asigna el ID real (roster.toml: "# Nombre (mod heroe_propio)" + "id = N"). El combate lo
// pelea ESE ID; lo que no se puede (otro moveset o estilo elegido entre combates) se pone en marcha en memoria (VestirPropio).
// Si el mod falta, no coincide con la ficha o aun no esta cargado en esta sesion, todo sigue como antes (estilo + cuerpo).
constexpr const char* kModPropio = "heroe_propio";
std::string g_propio_montado;   // ficha.txt que habia al arrancar: la que lleva el roster cargado en ESTA sesion
bool g_propio_capturado = false;
std::string LineaFicha(const std::filesystem::path& f) {
  std::ifstream ff(f);
  std::string l;
  std::getline(ff, l);
  while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
  return l;
}
void CapturarPropio() {
  if (g_propio_capturado) return;
  g_propio_capturado = true;
  g_propio_montado = LineaFicha(dbz3::ModsRoot() / kModPropio / "ficha.txt");
}
// Quita el ultimo caracter utf-8 si quedo cortado (por un tope en bytes); los acentos completos se quedan.
void Utf8Entero(std::string& s) {
  size_t k = s.size();
  while (k > 0 && (static_cast<unsigned char>(s[k - 1]) & 0xC0) == 0x80) --k;
  if (k == 0) return;
  const unsigned char lead = static_cast<unsigned char>(s[k - 1]);
  const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  if (s.size() - (k - 1) < need) s.resize(k - 1);
}
// El texto de la ficha + el nombre (sin ; = " \ ni control, 24 bytes): lo que vista_heroe.py --combate recibe y guarda.
std::string ArgsPropio() {
  std::string n;
  for (const unsigned char c : g_nombre) {
    if (c >= 32 && c != ';' && c != '=' && c != '"' && c != 92 && c != 127) n += char(c);
  }
  while (n.size() > 24) n.pop_back();
  Utf8Entero(n);
  if (n.empty()) n = "Heroe";
  return ArgsFicha(Cuerpo(), g_traje) + ";nombre=" + n;
}
struct PropioInfo {
  int id = -1;        // ID real del heroe en el roster de esta sesion (-1: no esta)
  int donante = -1;   // donante con el que se monto (su moveset/capsulas/aura ya estan en el registro)
  bool pendiente = false;   // montado en disco con la ficha de ahora, pero se activa al reiniciar
  std::string clave;
  std::chrono::steady_clock::time_point t{};
} g_propio;
const PropioInfo& Propio() {
  CapturarPropio();
  const std::string args = ArgsPropio();
  const auto ahora = std::chrono::steady_clock::now();
  if (g_propio.clave == args && ahora - g_propio.t < std::chrono::milliseconds(1500)) return g_propio;   // el HUD pregunta cada cuadro
  g_propio = PropioInfo{};
  g_propio.clave = args;
  g_propio.t = ahora;
  const std::string hecho = LineaFicha(dbz3::ModsRoot() / kModPropio / "ficha.txt");
  if (hecho != args) return g_propio;
  if (g_propio_montado != args) {   // el roster que carga esta sesion se hizo con otra ficha
    g_propio.pendiente = true;
    return g_propio;
  }
  std::ifstream rf(dbz3::ModsRoot() / "_roster" / "roster.toml");
  std::string l;
  bool es = false;
  while (std::getline(rf, l)) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.rfind("#", 0) == 0) es = l.find(std::string("(mod ") + kModPropio + ")") != std::string::npos;
    else if (es && l.rfind("id = ", 0) == 0) g_propio.id = std::atoi(l.c_str() + 5);
    else if (es && l.rfind("donante = ", 0) == 0) g_propio.donante = std::atoi(l.c_str() + 10);
    else if (l == "[[personaje]]") es = false;
  }
  if (g_propio.id < 0 || dbz3::roster::DonorOf(uint32_t(g_propio.id)) < 0) g_propio.id = -1;
  return g_propio;
}
int IdPropio() { return Propio().id; }
// El luchador de verdad de un combate del heroe: su ID propio si esta montado; si no, el del estilo (como antes).
int Luchador(int estilo) {
  const int id = IdPropio();
  return id >= 0 ? id : estilo;
}
// Donante con el que se monta el heroe propio: el moveset elegido (ID nativo) o el del estilo.
int DonanteCombate() {
  const int e = EstiloId();
  return (g_moveset >= 0 && g_moveset != e && g_moveset < 44 && MovesetValido(g_moveset)) ? g_moveset : e;
}
// El heroe con ID propio tiene el moveset del donante con el que se monto. Si el estilo (o el moveset elegido) es otro,
// se le copian en memoria los ANM/CAM/gritos del donante de ahora (sin la correccion de altura del montaje).
void VestirPropio(int id, int estilo) {
  const int don = (g_moveset >= 0 && g_moveset != estilo && MovesetValido(g_moveset)) ? g_moveset : estilo;
  uint8_t* b = Base();
  if (!b || don == Propio().donante || don < 0 || don >= 64 || !MovesetValido(don)) return Quitar();
  if (g_puesto.estilo == id && g_puesto.moveset == don) return;
  Quitar();
  uint8_t* r = b + kChar372 + uint32_t(id) * 372;
  const uint8_t* rd = b + kChar372 + uint32_t(don) * 372;
  std::memcpy(g_puesto.c372, r, 372);
  for (uint32_t f = 0; f < 8; ++f) W32(r + 4 * f, Be32(rd + 4 * f));
  W32(r + 0x20, Be32(rd + 0x20));
  W32(r + 0x24, Be32(rd + 0x24));
  W32(r + 0x28, Be32(rd + 0x28));
  g_puesto.estilo = id, g_puesto.cuerpo = id, g_puesto.traje = 0, g_puesto.moveset = don;
  REXLOG_INFO("dbz3 heroe: el heroe propio (ID {}) toma el moveset de {} ({}) en este combate (montado con {})", id, don,
              M::Nombre(don), Propio().donante);
}

// Traje del combate: el traje extra de la ficha (mods/heroe_fichaN, traje.toml de roster_build con los colores cocidos
// en las texturas) si esta montado en esta sesion y se hizo con los valores de ahora; si no, el elegido. Los trajes
// extra van detras de los del juego (roster_build), en el orden de mods/_roster/roster.toml.
// estado: 0 ficha nativa, 1 en el combate, 2 hecho pero sin montar (al reiniciar), 3 sin hacer / desactualizado.
int TrajeCombate(int* estado) {
  int st = 3, t = g_traje;
  const PropioInfo& pr = Propio();
  if (pr.id >= 0) {   // el heroe es un personaje propio: su traje es el unico suyo
    st = 1, t = 0;
  } else if (pr.pendiente) {
    st = 2;
  } else if (FichaNativa()) {
    st = 0;
  } else {
    const std::string mod = "heroe_ficha" + std::to_string(FichaNumero());
    std::ifstream ff(dbz3::ModsRoot() / mod / "ficha.txt");
    std::string hecho;
    std::getline(ff, hecho);
    while (!hecho.empty() && (hecho.back() == '\r' || hecho.back() == ' ')) hecho.pop_back();
    if (hecho == ArgsFicha(Cuerpo(), g_traje)) {
      st = 2;
      std::ifstream rf(dbz3::ModsRoot() / "_roster" / "roster.toml");
      std::string l, mod_de;
      int k = 0, propio = -1;
      while (std::getline(rf, l)) {
        if (const size_t p = l.find("(mod "); l.rfind("#", 0) == 0 && p != std::string::npos)
          mod_de = l.substr(p + 5, l.find(')', p) - p - 5);
        if (l.rfind("id = ", 0) == 0 && std::atoi(l.c_str() + 5) == Cuerpo() && l.size() > 5) {
          if (mod_de == mod) propio = k;
          ++k;
        }
      }
      // el traje del combate es el indice de la lista de char96 (+4 = numero de trajes; Goku 5: 3 del select + 2),
      // y roster_ext pone ahi los extra detras (log "traje 6 de id 0"): nativos = total - extras de ese ID
      uint8_t* b = Base();
      const int total = b ? b[kChar96 + uint32_t(Cuerpo()) * 96 + 4] : 0;
      if (propio >= 0 && total - k + propio >= 0 && total - k + propio < total) {
        st = 1;
        t = total - k + propio;
      }
    } else {
      REXLOG_INFO("dbz3 heroe: traje de combate de la ficha {}: hecho con '{}', la ficha es '{}'", FichaNumero(), hecho,
                  ArgsFicha(Cuerpo(), g_traje));
    }
  }
  if (estado) *estado = st;
  return t;
}
int NumTrajes() { return TrajesDe(CuerpoAct()); }   // studies 8 H3 (heroe_api.h)
void PonerTraje(int t) {
  g_traje = t;
  Ajustar();
}
int EstiloId() { return kEstilos[g_estilo].id; }
int MovesetId() { return g_moveset; }   // frente 10: donante elegido (-1: el del estilo)
// studies 8 fase 2 (heroe_api.h): la lista del carrusel de movesets entera, para la pantalla de movesets.
std::vector<int> Movesets() {
  std::vector<int> v{-1};
  const int estilo = kEstilos[g_estilo].id;
  for (int id = 0; id < 64; ++id)
    if (id != estilo && MovesetValido(id) && Desbloqueado(id)) v.push_back(id);
  return v;
}
void PonerMoveset(int id) {
  g_moveset = id;
  Ajustar();
  REXLOG_INFO("dbz3 heroe: moveset elegido {} ({}) para el estilo {}", g_moveset,
              g_moveset < 0 ? std::string("el del estilo") : M::Nombre(g_moveset), EstiloId());
}
// Capsulas por defecto del donante (char96 +80: n + 7 u16): las especiales que trae su moveset.
int CapsulasMoveset(int id) {
  uint8_t* b = Base();
  if (!b || id < 0 || id >= 64) return 0;
  return std::min<int>(Be16(b + kChar96 + uint32_t(id) * 96 + 80), 7);
}
// ---------------------------------------------------------------------------------------------------------------------
// studies 5 C2: INVENTARIO DE TECNICAS del heroe. Todo sale del catalogo #SKC que el juego ya tiene en memoria (duenyos,
// clase, coste de ki, rareza) y de los donantes que el heroe tiene abiertos (Movesets()): ningun dato por personaje a
// mano, asi que funciona igual con luchadores nativos, ports e importados. La carta de cada tecnica la arma
// mod center hd/cac/tecnicas_cartas.py con las piezas del propio juego (nombre, duenyo, descripcion, coste, cara).
// ---------------------------------------------------------------------------------------------------------------------
int g_equipo[4] = {0, 0, 0, 0};   // capsulas equipadas del heroe (0 = hueco libre); la ficha las guarda
const std::vector<Tecnica>& CatalogoTecnicas() {
  static std::vector<Tecnica> v;
  static bool listo = false;
  static std::mutex mu;   // lo piden el hilo de dibujo y el de las reglas: se construye una sola vez, bajo candado
  std::lock_guard<std::mutex> lk(mu);
  uint8_t* b = Base();
  if (!b || listo) return v;
  const uint32_t pc = Be32(b + 0x82375608), pr = Be32(b + 0x8237560C);
  if (pc < 0x82000000 || pr < 0x82000000) return v;
  const uint32_t n = std::min<uint32_t>(Be32(b + pc + 0x10), 2048);
  if (n < 100) return v;
  for (uint32_t i = 1; i < n; ++i) {
    const uint8_t* r = b + pr + 40 * i;
    if (r[8] != 0x21) continue;   // ataques (clase 0x21) con un solo duenyo (o dos)
    const uint64_t own = uint64_t(Be32(r)) << 32 | Be32(r + 4);
    int cuantos = 0, don = -1;
    for (int k = 0; k < 64; ++k) {
      if (own >> k & 1) {
        ++cuantos;
        if (don < 0) don = k;
      }
    }
    if (cuantos < 1 || cuantos > 2) continue;
    v.push_back({int(i), don, int(r[15]), int(r[10] >> 4)});
  }
  listo = true;
  REXLOG_INFO("dbz3 heroe: catalogo de tecnicas: {} ataques con duenyo (de {} capsulas)", v.size(), n);
  return v;
}
int DonanteEfectivo() { return g_moveset >= 0 ? g_moveset : EstiloId(); }
std::vector<int> InventarioTecnicas() {
  std::vector<int> donantes{EstiloId()};
  for (int d : Movesets())
    if (d >= 0) donantes.push_back(d);
  std::vector<int> inv;
  for (const Tecnica& t : CatalogoTecnicas()) {
    if (std::find(donantes.begin(), donantes.end(), t.donante) != donantes.end() && (!TiposListos() || TipoTecnica(t.cap) >= 0))
      inv.push_back(t.cap);
  }
  return inv;
}
// C4: estructura de tecnicas pedida por el usuario: UNA definitiva (hueco 0), una en E+derecha (1) y una en E+izquierda (2).
// El tipo de cada capsula sale de tecnicas_tipo.py (el #CCM del moveset de su duenyo, medido en los 44 nativos).
std::filesystem::path TiposRuta() { return dbz3::ModsRoot().parent_path() / "ingame_ui" / "heroe_vista" / "tecnicas_tipo.txt"; }
int TipoTecnica(int cap) {
  static std::unordered_map<int, int> mapa;
  static bool listo = false;
  static std::mutex mu;
  std::lock_guard<std::mutex> lk(mu);
  if (!listo) {
    std::ifstream f(TiposRuta());
    if (f) {
      int c, t, d;
      while (f >> c >> t >> d) mapa[c] = t;
      listo = true;
      REXLOG_INFO("dbz3 heroe: tipos de tecnica: {} capsulas (definitiva / E+derecha / E+izquierda)", mapa.size());
    }
  }
  const auto it = mapa.find(cap);
  return it == mapa.end() ? -1 : it->second;
}
std::filesystem::path TiposRutaPublica() { return TiposRuta(); }
bool TiposListos() { return std::filesystem::exists(TiposRuta()); }
int Equipada(int k) { return k >= 0 && k < 3 ? g_equipo[k] : 0; }
bool TecnicaActiva(int cap) {
  for (const Tecnica& t : CatalogoTecnicas())
    if (t.cap == cap) return t.donante == DonanteEfectivo();
  return false;
}
void Equipar(int k, int cap) {
  if (k < 0 || k >= 3) return;
  if (cap != 0 && TipoTecnica(cap) != k) {   // C4: cada hueco admite solo su tipo
    REXLOG_INFO("dbz3 heroe: tecnica {} no cabe en el hueco {} (tipo {})", cap, k + 1, TipoTecnica(cap));
    return;
  }
  g_equipo[k] = cap;
  REXLOG_INFO("dbz3 heroe: tecnica equipada: hueco {} = capsula {} (definitiva {}, E+der {}, E+izq {}, donante efectivo {})", k + 1,
              cap, g_equipo[0], g_equipo[1], g_equipo[2], DonanteEfectivo());
}
// Tabla de fichas del juego (0x82324468: ID, trajes, entrada data_cmn de la cara del HUD, indice; termina en ID -1)
// -> "--caras ID:entrada,..." para tecnicas_cartas.py (las caras de los luchadores que el juego conoce).
std::string ArgsCartas() {
  uint8_t* b = Base();
  std::string r;
  if (!b) return r;
  for (int i = 0; i < 256; ++i) {
    const uint8_t* e = b + 0x82324468 + 16 * i;
    const int32_t id = int32_t(Be32(e));
    if (id < 0) break;
    const int32_t cara = int32_t(Be32(e + 8));
    if (cara <= 0 || cara > 100000) continue;
    r += (r.empty() ? "" : ",") + std::to_string(id) + ":" + std::to_string(cara);
  }
  return r;
}
// C3: Esferas del Dragon del mapa y deseos. Las 7 recogidas llaman a Shenron (la escena nativa de B3 la pone el DU; aqui solo
// la contabilidad): se apunta un deseo y las esferas vuelven a esconderse.
int Esferas() { return g_esferas; }
int Deseos() { return g_deseos; }
bool RazaCerrada() { return g_raza_cerrada; }
void GanarEsfera(int i) {
  if (i < 0 || i >= 7) return;
  g_esferas |= 1 << i;
  REXLOG_INFO("dbz3 heroe: esfera {} recogida ({}/7)", i + 1, __builtin_popcount(unsigned(g_esferas)));
  if (g_esferas == 127) {
    g_esferas = 0;
    g_deseos = std::min(g_deseos + 1, 9);
    REXLOG_INFO("dbz3 heroe: 7 Esferas: Shenron concede un deseo (deseos {})", g_deseos);
  }
}
int Saga() { return g_saga; }
// Frente 02 (studies 6): nombre, color de piel, voz y aura del heroe creado. El nombre se
// sanea (sin comillas/backslash y 16 caracteres) para no romper el rpg.toml.
const std::string& Nombre() { return g_nombre; }
int Color() { return g_color; }
int Voz() { return g_voz; }
int Aura() { return g_aura; }
void PonerNombre(const std::string& n) {
  g_nombre.clear();
  for (char c : n) {
    if (c == '"' || c == '\\') continue;
    g_nombre += c;
    if (g_nombre.size() >= 16) break;
  }
  Utf8Entero(g_nombre);
  if (g_nombre.empty()) g_nombre = "Heroe";
}
void PonerColor(int c) { g_color = std::clamp(c, 0, 31); }
void PonerVoz(int v) { g_voz = std::clamp(v, 0, 31); }
void PonerAura(int a) { g_aura = std::clamp(a, 0, kNumAuras - 1); }
int Nivel() { return std::min(kNivelMax, 1 + g_exp / kExpNivel); }
int Combates(int saga) { return int(SagaDe(saga).combates.size()); }   // combates de la saga (sagas.toml)
bool CambioBloqueado() { return g_bloqueo_maestro != 0; }   // la ultima tecla de cambio de maestro no se aplico
int g_nivel_antes = 0;   // nivel antes de la ultima saga (pantalla final: subida de nivel)
int NivelAntes() { return g_nivel_antes ? g_nivel_antes : Nivel(); }
int Vida(int estilo) {
  // studies 5 frente 04: cada vuelta completa a las sagas (prestigio) suma vida permanente.
  return 3000 + std::max(EstiloDeId(estilo).vida, 0) + 100 * (Nivel() - 1) + kPHVida * g_ph_vida +
         25 * std::max(0, g_saga - NumSagas());
}
// Partida guardada: extra = 1000 + cuerpo (antes de la ronda 4: el indice del maestro y jugador = cuerpo).
int CuerpoDe(int extra, int jugador) { return extra >= 1000 ? extra - 1000 : jugador; }

// PH: puntos de habilidad del heroe (se ganan en FinRun; los gasta la pantalla de creacion/maestros de 02).
int PH() { return g_ph; }
bool GastarPH(int rama) {
  if (g_ph <= 0 || (rama != 0 && rama != 1)) return false;
  ++(rama == 0 ? g_ph_vida : g_ph_dano);
  --g_ph;
  REXLOG_INFO("dbz3 heroe: PH gastado en {} (quedan {}; vida +{}, dano +{}%)", rama == 0 ? "vida" : "dano", g_ph,
              kPHVida * g_ph_vida, kPHDano * g_ph_dano);
  return true;
}
// Maestro del combate 3 de la saga (Tag Team puntual). -1 si no hay o es el mismo luchador del estilo.
int MaestroDeSaga(int saga) { return SagaDe(saga).relevo; }
bool EsCombateMaestro(int saga, int k) { return MaestroDeSaga(saga) >= 0 && k == SagaDe(saga).relevo_k; }
int MaestroEnCombate(int saga, int k) { return EsCombateMaestro(saga, k) ? MaestroDeSaga(saga) : -1; }
int MaestrosSuperados() { return __builtin_popcount(unsigned(g_maestros)); }

// Carrusel de estilos (rpg.cpp): el luchador del estilo elegido +- d y si es maestro.
int EstiloVecino(int d) { return kEstilos[Ciclo(g_estilo, d, kNumEstilos)].id; }
bool EsMaestro(int id) { return EsMaestroId(id); }
// Pone el estilo de una partida guardada como el elegido (el carrusel y LB/RB parten de el). Un ID que ya no es estilo
// (partidas de antes de la ronda 4) deja el que estaba.
void ElegirEstilo(int id) {
  for (int i = 0; i < kNumEstilos; ++i) {
    if (kEstilos[i].id == id) g_estilo = i;
  }
}
// studies 8 H1: progreso por ficha. La ficha (cac.toml) es la fuente; los globales son el progreso de la ficha
// activa mientras se juega (hub_heroe.toml y las claves heroe_* de rpg.toml se siguen escribiendo como respaldo).
void ImportarProgreso(const dbz3::cac::Heroe& h) {
  g_exp = std::max(0, h.xp);
  g_saga = std::max(0, h.saga);
  g_ph = std::max(0, h.ph);
  g_ph_vida = std::max(0, h.ph_vida);
  g_ph_dano = std::max(0, h.ph_dano);
  g_maestros = h.maestros & ((1 << kNumMaestros) - 1);
  for (int i = 0; i < kNumMaestros; ++i) g_entreno[i] = std::clamp(h.entreno[i], 0, 100);
  g_esferas = std::clamp(h.esferas, 0, 127);
  g_deseos = std::clamp(h.deseos, 0, 9);
  g_desbloqueos = h.desbloqueos;
  g_deseo_sesion = false;
  g_bloqueo_maestro = 0;
  g_nivel_antes = 0;
}
void ExportarProgreso(dbz3::cac::Heroe& h) {
  h.xp = g_exp;
  h.nivel = Nivel();
  h.saga = g_saga;
  h.ph = g_ph;
  h.ph_vida = g_ph_vida;
  h.ph_dano = g_ph_dano;
  h.maestros = g_maestros;
  for (int i = 0; i < kNumMaestros; ++i) h.entreno[i] = g_entreno[i];
  // H4: capsulas de tecnica desbloqueadas (maestros entrenados, en orden), para mostrarlas
  for (int k = 0; k < dbz3::cac::kNumTecnicas; ++k) h.tecnicas[k] = 0;
  for (int i = 0, k = 0; i < kNumMaestros && k < dbz3::cac::kNumTecnicas; ++i) {
    if ((g_maestros >> i) & 1) h.tecnicas[k++] = kEstilos[i].hab;
  }
  h.desbloqueos = g_desbloqueos;
  h.progreso = dbz3::cac::kProgresoFicha;
  h.esferas = g_esferas;
  h.deseos = g_deseos;
  g_deseo_sesion = false;   // guardar cierra el deseo en curso
  // C2: inventario de tecnicas (sin catalogo en memoria se conserva el de la ficha) y las 4 equipadas
  const std::vector<int> inv = InventarioTecnicas();
  if (!inv.empty()) h.inv = inv;
  for (int k = 0; k < 4; ++k) h.equipo[k] = g_equipo[k];
}
// Migracion (una vez): el progreso global (rpg.toml + hub_heroe.toml, ya leidos) pasa a la ficha `slot`.
void MigrarProgreso(int slot) {
  dbz3::cac::Heroe& h = dbz3::cac::SlotEditable(slot);
  ExportarProgreso(h);
  REXLOG_INFO("dbz3 heroe: progreso migrado a la ficha {} (progreso = 2): exp {}, nivel {}, saga {}, PH {}, maestros {:02X}",
              slot + 1, g_exp, Nivel(), g_saga, g_ph, g_maestros);
}
// Al cargar: el progreso de la ficha activa pasa a ser el de la partida.
void CargarProgreso(int slot) {
  ImportarProgreso(dbz3::cac::Slot(slot));
  AplicarHooksDePrueba();   // las pruebas fijan PH/saga/maestros encima del progreso de la ficha
  Ajustar();
  REXLOG_INFO("dbz3 heroe: progreso de la ficha {}: exp {}, nivel {}, saga {}, PH {}, maestros {:02X}", slot + 1, g_exp,
              Nivel(), g_saga, g_ph, g_maestros);
}
// Ficha de un heroe (cac.toml) -> heroe actual. La receta (estilo, cuerpo, traje, moveset) sale de PresetSlot;
// el cuerpo se busca por su ID dentro de la raza (si no existe, el primero).
void Importar(int slot) {
  const dbz3::cac::Heroe& h = dbz3::cac::Slot(slot);
  const dbz3::cac::Preset pr = dbz3::cac::PresetSlot(slot);
  g_raza = std::clamp(h.raza, 0, kNumRazas - 1);
  g_cuerpo = 0;
  for (int i = 0; i < kRazas[g_raza].n; ++i) {
    if (kRazas[g_raza].cuerpos[i].id == pr.cuerpo) g_cuerpo = i;
  }
  g_traje = pr.traje;
  ElegirEstilo(pr.estilo);
  g_moveset = pr.moveset;
  PonerNombre(h.nombre);
  PonerColor(h.color);
  PonerVoz(h.voz);
  PonerAura(h.aura);
  g_aura_col = h.aura_col, g_ki_col = h.ki_col;
  Ajustar();
  ImportarProgreso(h);   // studies 8 H1: el progreso es de la ficha (no hereda el de la anterior)
  for (int k = 0; k < 4; ++k) g_equipo[k] = k < 3 ? h.equipo[k] : 0;   // C2: tecnicas equipadas de la ficha (C4: solo 3 huecos)
  REXLOG_INFO("dbz3 heroe: ficha {} importada (raza {}, cuerpo {}, traje {}, estilo {} ({}), moveset {}, aura {}, nombre '{}'; "
              "progreso: exp {}, nivel {}, saga {}, PH {}, maestros {:02X})",
              slot, g_raza, Cuerpo(), g_traje, EstiloId(), M::Nombre(EstiloId()), g_moveset, g_aura, g_nombre, g_exp,
              Nivel(), g_saga, g_ph, g_maestros);
}
// Heroe actual -> su ficha (lo que se guarda en cac.toml). Es el inverso de Importar.
void Exportar(dbz3::cac::Heroe& h) {
  ExportarProgreso(h);
  h.raza = g_raza;
  h.cuerpo = CuerpoAct().id;
  h.traje = g_traje;
  h.estilo = EstiloId();
  h.moveset = g_moveset;
  h.nombre = g_nombre;
  h.color = g_color;
  h.voz = g_voz;
  h.aura = g_aura;
  h.aura_col = g_aura_col;
  h.ki_col = g_ki_col;
}

// Maestro (indice en kEstilos) del luchador `id`, -1 si no es maestro.
int MaestroIndice(int id) {
  for (int i = 0; i < kNumMaestros; ++i) {
    if (kEstilos[i].id == id) return i;
  }
  return -1;
}
// s9 (el usuario: "maestros desbloqueables, no todos de una; igual con los movesets"). Se abre:
//  - el primer maestro (indice 0), desde el principio; los demas, al llegar a la saga en la que entrenan (sagas.toml
//    maestro=) o si ya estan entrenados;
//  - cualquier otro luchador (estilo o moveset donante), al vencerlo: rival de una saga ya completada;
//  - lo que el heroe ya lleva puesto (partidas de antes de s9).
// DBZ3_TEST_HEROE_TODO=1 lo abre todo (pruebas).
int SagaDelMaestro(int m) {
  const auto& s = Sagas();
  for (int i = 0; i < int(s.size()); ++i)
    if (s[i].maestro == m) return i;
  return -1;
}
namespace {   // el de la declaracion de arriba
bool Desbloqueado(int id) {
  static const bool todo = std::getenv("DBZ3_TEST_HEROE_TODO") != nullptr;
  if (todo || id == kEstilos[g_estilo].id || id == g_moveset) return true;
  const int m = MaestroIndice(id);
  if (m == 0 || (m > 0 && ((g_maestros >> m) & 1))) return true;
  const auto& s = Sagas();
  if (m > 0) {
    const int k = SagaDelMaestro(m);
    return k < 0 || g_saga >= k;
  }
  for (int i = 0; i < int(s.size()) && i < g_saga; ++i)
    for (const Lucha& l : s[i].combates)
      if (l.rival == id) return true;
  return false;
}
}  // namespace
// C3: el 60 % (kEntrenoMinimo) es el escalon de recompensa de cada maestro (su tecnica clave); ya no bloquea el cambio.
// [H] kEntrenoPorCombate = 20: 3 combates ganados = 60%.
constexpr int kEntrenoMinimo = 60, kEntrenoPorCombate = 20;

// C3: la raza y el cuerpo se eligen libres al CREAR al heroe; empezada la aventura (algun combate o saga hecha) solo cambian
// con un DESEO de Shenron (7 Esferas). El primer paso tras tener un deseo lo gasta y abre la "sesion de deseo" (varios pasos
// seguidos) hasta guardar; antes se copia cac.toml a cac.toml.deseo-<fecha>.bak. La apariencia (traje, colores, pelo) NO pasa
// por aqui: se edita siempre en la pantalla Create/Edit.
// ponytail: mientras las Esferas no se recojan en el mapa DU (nada llama a GanarEsfera fuera de la prueba), cerrar la raza
// dejaria al heroe sin salida: el candado solo se activa con DBZ3_TEST_HEROE_ESFERAS (pruebas). Al cablear los pickups: true.
bool AventuraEmpezada() {
  static const bool candado = std::getenv("DBZ3_TEST_HEROE_ESFERAS") != nullptr;
  return candado && (g_exp > 0 || g_saga > 0);
}
bool PermiteCambioProfundo() {
  g_raza_cerrada = false;
  if (!AventuraEmpezada() || g_deseo_sesion) return true;
  if (g_deseos <= 0) {
    g_raza_cerrada = true;
    REXLOG_INFO("dbz3 heroe: raza/cuerpo cerrados: hace falta un deseo de Shenron (7 Esferas; llevas {})", __builtin_popcount(unsigned(g_esferas)));
    return false;
  }
  const bool copia = dbz3::cac::RespaldarCopia("deseo");
  --g_deseos;
  g_deseo_sesion = true;
  REXLOG_INFO("dbz3 heroe: deseo concedido: cambio de raza/cuerpo abierto (copia {}; deseos que quedan {})", copia ? "hecha" : "no hecha", g_deseos);
  return true;
}
std::string CandadoRaza() {
  if (!AventuraEmpezada()) return "";
  if (g_deseo_sesion) return T::T("  (deseo abierto)", "  (wish open)");
  if (g_deseos > 0) return T::T("  (deseo disponible)", "  (wish ready)");
  return std::string(T::T("  [cerrada: 7 Esferas ", "  [locked: 7 Dragon Balls ")) + std::to_string(__builtin_popcount(unsigned(g_esferas))) + "/7]";
}
// Filas de la pantalla de creacion: 0 raza, 1 cuerpo, 2 apariencia, 3 estilo, 4 moveset, 5 aura, 6 PH (7 = empezar).
void Mover(int fila, int dir) {
  switch (fila) {
    case 0:
      if (!PermiteCambioProfundo()) break;
      g_raza = Ciclo(g_raza, dir, Xv2PiezasActivas() ? kNumRazas : 7), g_cuerpo = g_traje = 0;   // C8b: por defecto, 7 razas nativas
      break;
    case 1:
      if (!PermiteCambioProfundo()) break;
      g_cuerpo = Ciclo(g_cuerpo, dir, kRazas[g_raza].n), g_traje = 0;
      break;
    case 2: g_traje = Ciclo(g_traje, dir, TrajesDe(CuerpoAct())); break;
    case 3: {
      int nuevo = Ciclo(g_estilo, dir, kNumEstilos);
      for (int k = 0; k < kNumEstilos && !Desbloqueado(kEstilos[nuevo].id); ++k) nuevo = Ciclo(nuevo, dir, kNumEstilos);
      g_bloqueo_maestro = 0;   // cambio de maestro libre (C3)
      g_estilo = nuevo;
      break;
    }
    case 4: g_moveset = MovesetCiclo(dir); break;   // frente 10: donante valido +- d (-1: el del estilo)
    case 5: {   // C4: las auras del juego y, detras, los colores de aura desbloqueados con los maestros
      std::vector<std::string> cols;
      for (const InfoDesbloqueo& d : Desbloqueos())
        if (d.tipo == "aura" && d.conseguido) cols.push_back(d.color);
      const int n = kNumAuras + int(cols.size());
      int cur = g_aura;
      if (!g_aura_col.empty())
        for (size_t i = 0; i < cols.size(); ++i)
          if (cols[i] == g_aura_col) cur = kNumAuras + int(i);
      cur = Ciclo(cur, dir, n);
      if (cur < kNumAuras) g_aura = cur, g_aura_col.clear();
      else g_aura_col = cols[size_t(cur - kNumAuras)];
      break;
    }
    case 9: {   // C4: color del ki (efectos de las tecnicas): el normal o uno desbloqueado
      std::vector<std::string> cols;
      for (const InfoDesbloqueo& d : Desbloqueos())
        if (d.tipo == "ki" && d.conseguido) cols.push_back(d.color);
      int cur = 0;
      for (size_t i = 0; i < cols.size(); ++i)
        if (cols[i] == g_ki_col) cur = 1 + int(i);
      cur = Ciclo(cur, dir, 1 + int(cols.size()));
      g_ki_col = cur == 0 ? std::string() : cols[size_t(cur - 1)];
      break;
    }
    case 6: GastarPH(dir < 0 ? 0 : 1); break;   // PH: izquierda vida, derecha dano
    case 10:   // mismo ajuste que el launcher (dbz3_cac_xv2); gris y sin efecto si no hay Xenoverse 2
      if (Xv2Instalado()) {
        dbz3::settings::SetCacXv2Piezas(!dbz3::settings::CacXv2Piezas());
        dbz3::settings::SaveUserSettings();
        REXLOG_INFO("dbz3 heroe: piezas {}", dbz3::settings::CacXv2Piezas() ? "Xenoverse 2 (experimental)" : "Budokai 3");
      }
      break;
    default: break;
  }
  Ajustar();
}
// C4: nombre legible de un color "#RRGGBB" (por el tono): Dorado, Rojo, Verde, Violeta...
std::string NombreDeColor(const std::string& hex) {
  if (hex.size() != 7 || hex[0] != '#') return hex;
  const int r = std::stoi(hex.substr(1, 2), nullptr, 16), g = std::stoi(hex.substr(3, 2), nullptr, 16),
            b = std::stoi(hex.substr(5, 2), nullptr, 16);
  const int mx = std::max({r, g, b}), mn = std::min({r, g, b});
  if (mx - mn < 24) return mx > 200 ? T::T("Blanco", "White") : mx > 90 ? T::T("Gris", "Grey") : T::T("Negro", "Black");
  float h = 0;
  const float d = float(mx - mn);
  if (mx == r) h = 60.0f * std::fmod((g - b) / d, 6.0f);
  else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
  else h = 60.0f * ((r - g) / d + 4.0f);
  if (h < 0) h += 360.0f;
  if (h < 15 || h >= 345) return T::T("Rojo", "Red");
  if (h < 38) return T::T("Naranja", "Orange");
  if (h < 65) return T::T("Dorado", "Golden");
  if (h < 160) return T::T("Verde", "Green");
  if (h < 200) return T::T("Cian", "Cyan");
  if (h < 255) return T::T("Azul", "Blue");
  if (h < 300) return T::T("Violeta", "Violet");
  return T::T("Rosa", "Pink");
}
std::string FilaMaestros();
std::string Fila(int fila) {
  const Raza& r = kRazas[g_raza];
  switch (fila) {
    case 0: return std::string(L("Raza: ", "Race: ", "Race : ", "Volk: ", "Razza: ")) + L(r.es, r.en, r.fr, r.de, r.it) + CandadoRaza();
    case 1: return std::string(L("Cuerpo: ", "Body: ", "Corps : ", "Körper: ", "Corpo: ")) + NombreCuerpo();
    case 2:
      return std::string(L("Apariencia: traje ", "Look: costume ", "Apparence : tenue ", "Aussehen: Kostüm ",
                           "Aspetto: costume ")) + std::to_string(g_traje + 1) + " / " +
             std::to_string(TrajesDe(CuerpoAct())) + "  (A)";   // studies 8 H3: A abre Create/Edit
    case 3:
      return std::string(L("Estilo: ", "Style: ", "Style : ", "Stil: ", "Stile: ")) + M::Nombre(EstiloId()) +
             (g_estilo < kNumMaestros ? L(" (maestro)", " (master)", " (maître)", " (Meister)", " (maestro)") : "");
    case 4:
      return std::string(L("Moveset: ", "Moveset: ", "Moveset : ", "Moveset: ", "Moveset: ")) +
             (g_moveset < 0 ? M::Nombre(EstiloId()) + T::T(" (el del estilo)", " (the style's own)")
                            : M::Nombre(g_moveset)) + "  (A)";   // studies 8 fase 2: A abre la lista de movesets
    case 5: {
      static const char* const kAuras[kNumAuras][2] = {{"la del estilo", "the style's own"}, {"sin aura", "no aura"},
          {"normal", "normal"}, {"Kaioken", "Kaioken"}, {"Super Saiyan", "Super Saiyan"}, {"SSJ2 (rayos)", "SSJ2 (sparks)"},
          {"SSJ3", "SSJ3"}, {"SSJ4", "SSJ4"}};
      const int k = std::clamp(g_aura, 0, kNumAuras - 1);
      if (!g_aura_col.empty()) return std::string(T::T("Aura: ", "Aura: ")) + NombreDeColor(g_aura_col);
      return std::string(T::T("Aura: ", "Aura: ")) + T::T(kAuras[k][0], kAuras[k][1]);
    }
    case 9:
      return std::string(T::T("Ki: ", "Ki: ")) + (g_ki_col.empty() ? std::string(T::T("el normal", "normal")) : NombreDeColor(g_ki_col));
    case 6:
      return std::string(T::T("Puntos (", "Points (")) + std::to_string(g_ph) + "): " + T::T("vida +", "health +") +
             std::to_string(kPHVida * g_ph_vida) + "  " + T::T("daño +", "damage +") + std::to_string(kPHDano * g_ph_dano) + "%";
    case 7: return FilaMaestros();
    case 10:   // piezas del creador: Budokai 3 (nativas) o Xenoverse 2 (experimental, requiere XV2 instalado)
      return std::string(T::T("Piezas: ", "Parts: ")) +
             (!Xv2Instalado() ? std::string(T::T("Budokai 3 (XV2 no instalado)", "Budokai 3 (XV2 not installed)"))
              : Xv2PiezasActivas() ? std::string(T::T("Xenoverse 2 [EXPERIMENTAL]", "Xenoverse 2 [EXPERIMENTAL]"))
                                   : std::string("Budokai 3"));
    default: return L("¡Empezar la misión!", "Start the mission!", "Commencer la mission !", "Mission starten!",
                      "Inizia la missione!");
  }
}
// Nivel y experiencia en una linea: "Nivel 2  XP 1/3" (+ lo que se apunta al acabar la saga en curso).
std::string LineaNivel(int pendientes) {
  std::string s = std::string(L("Nivel ", "Level ", "Niveau ", "Stufe ", "Livello ")) + std::to_string(Nivel());
  if (g_saga > NumSagas()) s += std::string("  (") + T::T("prestigio ", "prestige ") + std::to_string(g_saga - NumSagas()) + ")";
  s += "  PH " + std::to_string(g_ph);
  if (Nivel() >= kNivelMax) return s + "  (" + T::T("máximo", "max") + ")";
  s += "  XP " + std::to_string(g_exp - kExpNivel * (Nivel() - 1)) + "/" + std::to_string(kExpNivel);
  if (pendientes > 0) s += "  (+" + std::to_string(pendientes) + " " + T::T("al acabar la saga", "when the saga ends") + ")";
  return s;
}
const char* NombreSaga(int saga) {
  const SagaDef& sg = SagaDe(saga);
  return L(sg.es.c_str(), sg.en.c_str(), sg.fr.c_str(), sg.de.c_str(), sg.it.c_str());
}
// studies 8 fase 2 (H5, heroe_api.h): datos de las sagas para el mapa.
int SagasTotal() { return NumSagas(); }
int MaestroQueEntrena(int saga) {
  const int m = SagaDe(saga).maestro;
  return m >= 0 && m < kNumMaestros ? kEstilos[m].id : -1;
}
std::vector<std::pair<int, int>> CombatesDeSaga(int saga) {
  std::vector<std::pair<int, int>> v;
  for (const Lucha& l : SagaDe(saga).combates) v.emplace_back(l.rival, l.escenario);
  return v;
}
// El estilo en dos lineas: bonus ("Piccolo (maestro): vida 3500, recibido -15%") y tecnicas.
std::string DetalleEstilo(int id) {
  const Estilo& x = EstiloDeId(id);
  std::string s = M::Nombre(id) + (EsMaestroId(id) ? L(" (maestro)", " (master)", " (maître)", " (Meister)", " (maestro)") : "") +
                  ": " + L("vida ", "health ", "vie ", "Leben ", "vita ") + std::to_string(Vida(id));
  if (x.dano != 100) s += std::string(", ") + L("daño ", "damage ", "dégâts ", "Schaden ", "danni ") + Pct(x.dano - 100);
  if (x.recibido != 100) {
    s += std::string(", ") + L("recibido ", "taken ", "subis ", "erlitten ", "subiti ") + Pct(x.recibido - 100);
  }
  if (x.ki < 0) s += std::string(", ") + L("el ki se recarga solo", "ki recharges", "ki auto", "Ki lädt sich auf", "ki si ricarica");
  s += std::string("\n") + L("Técnicas: ", "Techniques: ", "Techniques : ", "Techniken: ", "Tecniche: ");
  if (x.tec) {
    s += std::string(x.tec) + ", " + x.hab_n;
  } else {
    s += std::string(L("todo el moveset de ", "the whole moveset of ", "tout le style de ", "der ganze Stil von ",
                       "tutto lo stile di ")) + M::Nombre(id) +
         L(" (sus cápsulas)", " (his capsules)", " (ses capsules)", " (seine Kapseln)", " (le sue capsule)");
  }
  return s;
}
// Pantalla de maestros (fila 7 de la creacion, rpg.cpp), al estilo UT "Select Master": los 6 maestros con su % de
// entrenamiento (g_entreno, +kEntrenoPorCombate por combate ganado con ese maestro) y sus modificadores (kEstilos:
// vida, dano, recibido, ki). Entrenado (100 % o saga completada) = bit de g_maestros; da su capsula de tecnica (Reglas).
std::string FilaMaestros() {
  return std::string(T::T("Maestros: ", "Masters: ")) + std::to_string(MaestrosSuperados()) + " / " +
         std::to_string(kNumMaestros) + "  (A)";   // studies 8 H4: A abre Select Master
}
std::string DetalleMaestros() {
  std::string s;
  for (int i = 0; i < kNumMaestros; ++i) {
    const Estilo& x = kEstilos[i];
    if (i) s += "\n";
    s += std::string(((g_maestros >> i) & 1) ? "[x] " : "[ ] ") + M::Nombre(x.id) + " " + std::to_string(g_entreno[i]) +
         "%: " + T::T("vida ", "health ") + (x.vida > 0 ? "+" : "") + std::to_string(x.vida) + ", " + T::T("daño ", "damage ") + Pct(x.dano - 100) + ", " +
         T::T("recibido ", "taken ") + Pct(x.recibido - 100) + (x.ki < 0 ? T::T(", ki recarga", ", ki recharges") : "") +
         " (" + (x.hab_n ? x.hab_n : "") + ")";
  }
  return s;
}
Barras Barras_() {
  Barras b;
  const Estilo& x = EstiloDeId(EstiloId());
  b.vida = Vida(EstiloId());
  b.dano = (x.id >= 0 ? x.dano : 100) + 2 * (Nivel() - 1) + Nivel() / 4 + kPHDano * g_ph_dano;
  b.resist = 200 - std::max(40, (x.id >= 0 ? x.recibido : 100) - (Nivel() - 1));
  b.nivel = Nivel();
  b.xp = Nivel() >= kNivelMax ? kExpNivel : g_exp - kExpNivel * (Nivel() - 1);
  b.xp_max = kExpNivel;
  return b;
}
std::string NombreMostrado() {
  return g_nombre == "Heroe" || g_nombre == "Héroe" ? std::string(T::T("Héroe", "Hero")) : g_nombre;
}
// Vista previa del estilo elegido en la creacion (bocadillo): bonus, tecnicas, nivel y la saga siguiente.
std::string Detalle() {
  return DetalleEstilo(EstiloId()) + "\n" + LineaNivel(0) + "  -  " + NombreSaga(g_saga);
}

// Reglas del combate (bonus del estilo si es maestro + nivel) y el modelo del cuerpo puesto al estilo.
M::Reglas Reglas(int estilo, int extra, int traje) {
  // studies 8 fase 2: el traje extra de la ficha no tiene fila de cara propia: la del traje elegido
  const int propio = IdPropio();   // studies 9: el heroe con ID propio no lleva el modelo del cuerpo puesto a un estilo
  if (propio >= 0) VestirPropio(propio, estilo);
  else Vestir(estilo, CuerpoDe(extra, estilo), traje >= CuerpoAct().trajes ? g_traje : traje);
  PonerAuraEstilo(propio >= 0 ? propio : estilo, g_aura);
  const Estilo& x = EstiloDeId(estilo);
  M::Reglas g;
  // studies 5 frente 04: crecimiento por nivel (dano) y mitigacion que sube con el nivel (suelo 40%).
  // A nivel alto el ki sin coste negativo se recarga solo (mas ritmo en las sagas tardias).
  g.dano_hecho = (x.id >= 0 ? x.dano : 100) + 2 * (Nivel() - 1) + Nivel() / 4 + kPHDano * g_ph_dano;
  g.dano_recibido = std::max(40, (x.id >= 0 ? x.recibido : 100) - (Nivel() - 1));
  g.ki_drena = x.id >= 0 ? (x.ki == 0 && Nivel() >= 8 ? -1 : x.ki) : 0;
  if (x.hab) {
    g.ncaps = 1;
    g.caps[0] = x.hab;   // se suma a las suyas
  }
  // Maestros entrenados (g_maestros): la capsula de tecnica de cada uno (kEstilos[i].hab), hasta 7 y sin repetir.
  for (int i = 0; i < kNumMaestros; ++i) {
    const int hab = kEstilos[i].hab;
    if (!((g_maestros >> i) & 1) || hab <= 0 || g.ncaps >= 7) continue;
    bool dup = false;
    for (int j = 0; j < g.ncaps; ++j) dup = dup || g.caps[j] == hab;
    if (!dup) g.caps[g.ncaps++] = hab;
  }
  // C2: tecnicas EQUIPADAS del heroe (inventario): las del donante que se usa ahora (las de otro no tienen golpe en este
  // moveset), tras las de maestro y antes de las por defecto del donante; hasta 7 y sin repetir.
  {
    const int antes = std::max(g.ncaps, 0);
    int n = antes;
    for (int k = 0; k < 4; ++k) {
      const int cap = g_equipo[k];
      if (cap <= 0 || n >= 7 || !TecnicaActiva(cap)) continue;
      bool dup = false;
      for (int j = 0; j < n; ++j) dup = dup || g.caps[j] == cap;
      if (!dup) g.caps[n++] = cap;
    }
    if (n > antes) g.ncaps = n;
    static int ultimo_eq[5] = {-1, -1, -1, -1, -1};
    const int firma[5] = {g_equipo[0], g_equipo[1], g_equipo[2], g_equipo[3], DonanteEfectivo()};
    if (std::memcmp(ultimo_eq, firma, sizeof firma) != 0) {
      std::memcpy(ultimo_eq, firma, sizeof firma);
      REXLOG_INFO("dbz3 heroe: tecnicas equipadas en el combate: +{} (equipo {} {} {} {}; donante {}; capsulas {} {} {} {} {} {} {})",
                  n - antes, g_equipo[0], g_equipo[1], g_equipo[2], g_equipo[3], DonanteEfectivo(), g.caps[0], g.caps[1],
                  g.caps[2], g.caps[3], g.caps[4], g.caps[5], g.caps[6]);
    }
  }
  // Frente 10: con moveset donante, sus capsulas por defecto (char96 +80) se suman a las del combate (hasta 7) para
  // que salgan sus especiales (el #CCM exige la capsula w8). El tapon de las formas 1+ no entra nunca (Vestir ya lo
  // deja en el registro del estilo). [H] las capsulas de un mod dependen de su importacion (roster_ext).
  uint8_t* b = Base();
  // donante cuyas capsulas por defecto se suman: el moveset elegido; con heroe propio, el de ahora si no es el del montaje
  int don = g_moveset >= 0 && g_moveset != estilo ? g_moveset : -1;
  if (propio >= 0) {
    don = g_moveset >= 0 && g_moveset != estilo ? g_moveset : estilo;
    if (don == Propio().donante) don = -1;
  }
  if (b && don >= 0 && MovesetValido(don)) {
    const uint8_t* c96 = b + kChar96 + uint32_t(don) * 96;
    const uint32_t tapon = estilo == 27 ? 6 : 96;
    int n = std::max(g.ncaps, 0);
    const int base = n;
    for (uint32_t k = 0, nc = std::min<uint32_t>(Be16(c96 + 80), 7); k < nc && n < 7; ++k) {
      const uint16_t cap = Be16(c96 + 82 + 2 * k);
      if (cap == 0 || cap == tapon) continue;
      bool dup = false;
      for (int i = 0; i < n; ++i) dup = dup || g.caps[i] == cap;
      if (!dup) g.caps[n++] = cap;
    }
    if (n > base) {
      g.ncaps = n;
      static int ultimo[2] = {-2, -2};   // el HUD relee las reglas cada cuadro: la linea sale una vez por cambio
      if (ultimo[0] != don || ultimo[1] != estilo) {
        ultimo[0] = don;
        ultimo[1] = estilo;
        REXLOG_INFO("dbz3 heroe: capsulas del moveset {} ({}) sumadas al estilo {}: {}+{} = {} ({} {} {} {} {} {} {})",
                    don, M::Nombre(don), estilo, base, n - base, n, g.caps[0], g.caps[1], g.caps[2],
                    g.caps[3], g.caps[4], g.caps[5], g.caps[6]);
      }
    }
  }
  return g;
}

// Combate k de la saga. El rival sale distinto del luchador del estilo (el registro de combate es por ID: saldria con
// el cuerpo del heroe) y del cuerpo (seria un espejo): el primero de rival / alternativo / tercero / 42 / 43 que sirva.
void Mision(int saga, int k, int estilo, int cuerpo, int* rival, int* vida, int* escenario) {
  const Lucha& p = LuchaDe(saga, k);
  const int cand[] = {p.rival, p.alt, p.alt2, 42, 43};
  *rival = p.alt;
  for (const int c : cand) {
    if (c != estilo && c != cuerpo) {
      *rival = c;
      break;
    }
  }
  *vida = p.vida + 500 * (std::max(saga, 0) / NumSagas());   // cada vuelta a las sagas, rivales mas duros
  *escenario = p.escenario;
}
// Texto del combate k: la historia del rival de la saga; si por el estilo o el cuerpo salio otro (`rival`), no se
// nombra a uno que no va a salir.
std::string MisionTexto(int saga, int k, int rival) {
  const SagaDef& sg = SagaDe(saga);
  const Lucha& p = LuchaDe(saga, k);
  return std::string(L(sg.es.c_str(), sg.en.c_str(), sg.fr.c_str(), sg.de.c_str(), sg.it.c_str())) + ": " +
         (rival == p.rival ? L(p.es.c_str(), p.en.c_str(), p.fr.c_str(), p.de.c_str(), p.it.c_str())
                           : T::T("¡Un rival inesperado se cruza en tu camino!", "An unexpected rival crosses your path!"));
}

// Textos de la saga (res/heroe/sagas.toml): intro al empezarla, cierre al empezar la siguiente. fr/de/it: el ingles.
std::vector<std::string> SagaTextos(int saga, bool intro) {
  const SagaDef& sg = SagaDe(saga);
  const bool es = std::string(T::T("1", "0")) == "1";
  const auto& v = intro ? (es ? sg.intro_es : sg.intro_en) : (es ? sg.cierre_es : sg.cierre_en);
  return v.empty() ? (intro ? sg.intro_en : sg.cierre_en) : v;
}

// studies 8 H6: datos del guion de cada combate y el epilogo.
int TotalSagas() { return NumSagas(); }
bool EsUltimaSaga(int saga) { return NumSagas() > 0 && std::max(saga, 0) % NumSagas() == NumSagas() - 1; }
// Forma con la que sale el rival del combate k (solo si sale el rival de la saga, no un sustituto).
int FormaRival(int saga, int k, int rival) {
  const Lucha& p = LuchaDe(saga, k);
  return rival == p.rival ? std::clamp(p.forma, 0, 7) : 0;
}
std::string NotaCombate(int saga, int k) { return LuchaDe(saga, k).nota; }
std::string TextoJefe(int saga, int k) {
  const Lucha& p = LuchaDe(saga, k);
  const bool es = std::string(T::T("1", "0")) == "1";
  return es ? (p.jefe_es.empty() ? p.jefe_en : p.jefe_es) : p.jefe_en;
}
std::vector<std::string> Epilogo() {
  Sagas();   // se lee con las sagas
  const bool es = std::string(T::T("1", "0")) == "1";
  return es && !g_epilogo_es.empty() ? g_epilogo_es : g_epilogo_en;
}
// PRUEBA (studies 8 H6, DBZ3_TEST_HEROE_PERDER=1): a los ~6 s de pelea gana el rival (aviso de ring out con el hueco
// del ganador + 1 = 2, como DBZ3_TEST_RINGOUT en tagteam.cpp pero al reves). Para probar la nota "pierde" del guion.
// Lo llama el HUD del modo (rpg::Hud) en cada cuadro del combate del heroe.
void PruebaPerder(bool en_combate) {
  static const bool on = std::getenv("DBZ3_TEST_HEROE_PERDER") != nullptr;
  static std::chrono::steady_clock::time_point t0{};
  static bool contando = false, hecho = false;
  if (!on) return;
  if (!en_combate) {
    contando = hecho = false;
    return;
  }
  const auto ahora = std::chrono::steady_clock::now();
  if (!contando) contando = true, t0 = ahora;
  if (hecho || ahora - t0 < std::chrono::seconds(6)) return;
  rex::Runtime* rt = rex::Runtime::instance();
  uint8_t* b = rt ? rt->virtual_membase() : nullptr;
  if (!b) return;
  const uint32_t mgr = Be32(b + 0x823D6CF4);
  if (!mgr) return;
  b[mgr + 35569] = 2;
  hecho = true;
  REXLOG_INFO("dbz3 heroe: prueba: el rival gana (ring out del jugador)");
}

// Fin de una partida (no guardada a medias): experiencia por combate ganado; si se completo, la saga siguiente.
void FinRun(int ganados, bool victoria) {
  const int antes = g_nivel_antes = Nivel();
  const int ph_antes = g_ph;
  if (victoria)   // escalafon: saga completada (15 + nivel), una vez por saga
    dbz3::pasarela::RangoMejora("rango_heroe_saga_" + std::to_string(g_saga), 1, 15 + Nivel(), 0);
  g_exp += std::max(ganados, 0);
  g_ph += std::max(ganados, 0);   // 1 PH por combate ganado
  if (int i = MaestroIndice(kEstilos[g_estilo].id); i >= 0) {   // UT: % de entrenamiento con el maestro actual
    g_entreno[i] = std::min(100, g_entreno[i] + kEntrenoPorCombate * std::max(ganados, 0));
    if (g_entreno[i] >= 100) g_maestros |= 1 << i;
  }
  if (victoria) {
    g_ph += 2;                    // bonus de saga
    const int m = SagaDe(g_saga).maestro;   // el maestro de esa saga queda entrenado (sagas.toml)
    if (m >= 0 && m < kNumMaestros) g_maestros |= 1 << m;
    ++g_saga;
  }
  REXLOG_INFO("dbz3 heroe: fin de saga ({} ganados, {}): exp {}, nivel {} -> {}, PH {} -> {} (+{}), maestros {:02X}, "
              "saga siguiente {}",
              ganados, victoria ? "completada" : "perdida", g_exp, antes, Nivel(), ph_antes, g_ph, g_ph - ph_antes,
              g_maestros, g_saga);
}

// ---- C3: desbloqueos con los maestros (res/heroe/desbloqueos.toml), por datos: maestro -> condicion -> desbloqueo ----
// Una entrada: `maestro` = ID del luchador-maestro (0 Goku, 7 Vegeta, 11 Piccolo, 10 Krillin, 27 Freezer, 33 Cell), `entreno` =
// % minimo con el, `vence` / `forma` (opcionales) = hay que ganar un combate contra ese rival (y en esa forma) con el % ya
// logrado; `tipo` = aura | ki | tecnica; `color` "#RRGGBB" (aura_color / ki_color de colores.py) y `capsula` (tecnica con
// variante de color, p. ej. la 6 = Kamehameha rojo). Automatico para cualquier combinacion: se evalua en cada victoria del mapa.
struct Desbloqueo {
  std::string id, tipo, color, nombre_es, nombre_en;
  int maestro = -1, entreno = 0, vence = -1, forma = -1, capsula = 0;
};
const std::vector<Desbloqueo>& TablaDesbloqueos() {
  static const std::vector<Desbloqueo> v = [] {
    std::vector<Desbloqueo> out;
    for (const std::filesystem::path& ruta : {dbz3::settings::UserDataRoot() / "heroe" / "desbloqueos.toml",
                                              dbz3::ModsRoot().parent_path() / "res" / "heroe" / "desbloqueos.toml"}) {
      std::error_code ec;
      if (!std::filesystem::exists(ruta, ec)) continue;
      try {
        const toml::table t = toml::parse_file(ruta.string());
        const toml::array* a = t["desbloqueo"].as_array();
        if (!a) continue;
        for (const auto& e : *a) {
          const toml::table* x = e.as_table();
          if (!x) continue;
          Desbloqueo d;
          d.id = (*x)["id"].value_or(std::string{});
          d.tipo = (*x)["tipo"].value_or(std::string{});
          d.color = (*x)["color"].value_or(std::string{});
          d.nombre_es = (*x)["nombre_es"].value_or(d.id);
          d.nombre_en = (*x)["nombre_en"].value_or(d.id);
          d.maestro = int((*x)["maestro"].value_or(int64_t(-1)));
          d.entreno = int((*x)["entreno"].value_or(int64_t(0)));
          d.vence = int((*x)["vence"].value_or(int64_t(-1)));
          d.forma = int((*x)["forma"].value_or(int64_t(-1)));
          d.capsula = int((*x)["capsula"].value_or(int64_t(0)));
          if (!d.id.empty() && (d.tipo == "aura" || d.tipo == "ki" || d.tipo == "tecnica")) out.push_back(d);
        }
        REXLOG_INFO("dbz3 heroe: {} desbloqueos desde {}", out.size(), ruta.string());
        return out;
      } catch (const toml::parse_error&) {
        REXLOG_WARN("dbz3 heroe: {} no se puede leer", ruta.string());
      }
    }
    return out;
  }();
  return v;
}
bool TieneDesbloqueo(const std::string& id) { return std::find(g_desbloqueos.begin(), g_desbloqueos.end(), id) != g_desbloqueos.end(); }
// Tras una victoria contra `rival` (en la forma `forma`): lo que el % de cada maestro y esa victoria abren.
void ComprobarDesbloqueos(int rival, int forma) {
  for (const Desbloqueo& d : TablaDesbloqueos()) {
    if (TieneDesbloqueo(d.id)) continue;
    const int m = MaestroIndice(d.maestro);
    if (d.maestro >= 0 && (m < 0 || (g_entreno[m] < d.entreno && !((g_maestros >> m) & 1)))) continue;
    if (d.vence >= 0 && d.vence != rival) continue;
    if (d.forma >= 0 && d.forma != forma) continue;
    g_desbloqueos.push_back(d.id);
    REXLOG_INFO("dbz3 heroe: DESBLOQUEO {} ({} {}): maestro {} al {}%, victoria contra {} forma {}", d.id, d.tipo, d.color, d.maestro,
                m >= 0 ? g_entreno[m] : -1, rival, forma);
  }
}
std::vector<InfoDesbloqueo> Desbloqueos() {
  std::vector<InfoDesbloqueo> v;
  const bool es = std::string(T::T("1", "0")) == "1";
  for (const Desbloqueo& d : TablaDesbloqueos())
    v.push_back({d.id, d.tipo, es ? d.nombre_es : d.nombre_en, d.color, d.capsula, TieneDesbloqueo(d.id)});
  return v;
}

// C3: el Heroe puede entrar en Dragon Universe: su cuerpo propio (ID 63) esta montado en esta sesion y la historia de sus sagas
// (mods/historia_heroe, heroe_sagas_a_du.py --montar) esta instalada. Si no, el mapa del Heroe sigue siendo el de misiones.
int CasillaDu() { return 34 + std::clamp(FichaNumero(), 1, 3); }   // C4: ficha 1 = casilla 35, 2 = 36, 3 = 37 (libres en DU)
bool HistoriaMontada() {
  std::error_code ec;
  for (int c = 35; c <= 37; ++c)
    if (!std::filesystem::exists(dbz3::ModsRoot() / ("historia_heroe_" + std::to_string(c)) / "campana.toml", ec)) return false;
  return true;
}
bool DuDisponible() {
  static const bool forzar = std::getenv("DBZ3_TEST_DU_HEROE_FORZAR") != nullptr;   // PRUEBA: sin la ficha montada al dia
  return (forzar || IdPropio() >= 0) && HistoriaMontada();
}
// C3: victoria de un combate de la saga en el mapa de Dragon Universe (campaigns.cpp -> rpg::DuVictoria). Lo mismo que FinRun
// pero por combate: experiencia, PH y % de entrenamiento del maestro de ESA saga (sagas.toml `maestro`; el cambio de maestro es
// libre y el progreso va por maestro); al ganar el ultimo combate de la saga: bonus, maestro entrenado y la saga siguiente.
void DuVictoria(int saga, int k) {
  if (saga < 0 || saga >= NumSagas()) return;
  const bool ultimo = k + 1 >= int(SagaDe(saga).combates.size());
  const int antes = Nivel(), m = SagaDe(saga).maestro;
  ++g_exp, ++g_ph;
  if (m >= 0 && m < kNumMaestros) {
    g_entreno[m] = std::min(100, g_entreno[m] + kEntrenoPorCombate);
    if (g_entreno[m] >= 100) g_maestros |= 1 << m;
  }
  if (ultimo) {
    g_ph += 2;
    if (m >= 0 && m < kNumMaestros) g_maestros |= 1 << m;
    if (g_saga <= saga) g_saga = saga + 1;
  }
  {
    const int rival = LuchaDe(saga, k).rival;
    ComprobarDesbloqueos(rival, FormaRival(saga, k, rival));
  }
  REXLOG_INFO("dbz3 heroe: DU saga {} combate {} ganado{}: exp {}, nivel {} -> {}, PH {}, maestro {} {}%, maestros {:02X}, saga siguiente {}",
              saga, k, ultimo ? " (ultimo de la saga)" : "", g_exp, antes, Nivel(), g_ph, m, m >= 0 && m < kNumMaestros ? g_entreno[m] : -1,
              g_maestros, g_saga);
}

// studies 8 H4 (heroe_api.h): datos de cada maestro para la pantalla Select Master.
static_assert(kMaestros == kNumMaestros, "heroe_api.h kMaestros");
InfoMaestro Maestro(int i) {
  InfoMaestro m;
  if (i < 0 || i >= kNumMaestros) return m;
  const Estilo& x = kEstilos[i];
  m.id = x.id, m.dano = x.dano, m.recibido = x.recibido, m.vida = x.vida, m.ki = x.ki;
  m.capsula = x.hab, m.capsula_n = x.hab_n ? x.hab_n : "", m.tecnicas = x.tec ? x.tec : "";
  m.entrenado = ((g_maestros >> i) & 1) != 0;
  m.entreno = m.entrenado ? 100 : g_entreno[i];   // entrenado por su saga: 100 %
  m.desbloqueado = Desbloqueado(x.id);
  m.saga = i == 0 ? -1 : SagaDelMaestro(i);
  return m;
}
int MaestroActual() { return MaestroIndice(kEstilos[g_estilo].id); }
int EntrenoMinimo() { return kEntrenoMinimo; }
bool PuedeCambiarA(int i) {
  return i >= 0 && i < kNumMaestros && Desbloqueado(kEstilos[i].id);
}
bool ElegirMaestro(int i) {
  if (!PuedeCambiarA(i)) {
    g_bloqueo_maestro = 1;
    REXLOG_INFO("dbz3 heroe: maestro {} ({}) bloqueado: {}% con el actual (hace falta {}%)", i, M::Nombre(kEstilos[i].id),
                MaestroActual() >= 0 ? g_entreno[MaestroActual()] : 0, kEntrenoMinimo);
    return false;
  }
  g_bloqueo_maestro = 0;
  g_estilo = i;
  Ajustar();
  REXLOG_INFO("dbz3 heroe: maestro elegido {} ({}), entrenamiento {}%", i, M::Nombre(kEstilos[i].id), g_entreno[i]);
  return true;
}

// Fuera de la partida del heroe, el estilo vuelve a su modelo (rpg.cpp, Sincronizar).
void Sync(bool activo) {
  if (!activo) Quitar(), QuitarAura();   // el aura del estilo vuelve fuera del heroe
}

}  // namespace dbz3::heroe

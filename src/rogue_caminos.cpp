// dbz3 - Dragon Rogue (ROG-05): los 7 caminos como datos (res/datos/rogue_caminos.toml), la maldicion de la semana, el
// "Record clasico", que se LEE de hub_modos.toml (nunca se escribe: ROGUE_DISENO 2.1), y la pantalla 01 (elegir camino,
// maqueta ROGUE_VISUAL/01_elegir_camino.png). Diseno: studies 5/ENTREGAS/ROGUE_DISENO.md 2 y 3.
#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

#include "datos.h"
#include "heroe_ui_v2.h"
#include "launcher/settings.h"
#include "launcher/ui_kit.h"
#include "rogue.h"
#include "rogue_fx.h"
#include "survivors/recursos.h"

namespace dbz3::rogue {
namespace {
namespace D = dbz3::datos;
namespace M = dbz3::modos;
std::vector<int> Lista(const std::string& s) {   // "42,18,19" -> {42, 18, 19}
  std::vector<int> v;
  std::stringstream ss(s);
  for (std::string x; std::getline(ss, x, ',');)
    if (!x.empty()) v.push_back(std::atoi(x.c_str()));
  return v;
}
}  // namespace

std::vector<Camino> Caminos() {   // se relee cada vez: F5 en res/datos se ve sin reiniciar
  std::vector<Camino> v;
  std::stringstream orden(D::Str("rogue_caminos", "orden", ""));
  for (std::string id; std::getline(orden, id, ',');) {
    const std::string b = "caminos." + id + ".";
    auto num = [&](const char* k, double d) { return D::Num("rogue_caminos", (b + k).c_str(), d); };
    auto str = [&](const char* k) { return D::Str("rogue_caminos", (b + k).c_str(), ""); };
    if (!D::Existe("rogue_caminos", ("caminos." + id).c_str())) continue;
    Camino c;
    c.id = id;
    static constexpr const char* kLang[] = {"nombre", "en", "fr", "de", "it"};
    static constexpr const char* kTxt[] = {"t_es", "t_en", "t_fr", "t_de", "t_it"};
    for (int i = 0; i < 5; ++i) {
      c.nombre[i] = str(kLang[i]);
      c.texto[i] = str(kTxt[i]);
    }
    for (int i = 1; i < 5; ++i) {   // sin un idioma: el ingles y, sin el, el espanol
      if (c.nombre[i].empty()) c.nombre[i] = !c.nombre[1].empty() ? c.nombre[1] : c.nombre[0];
      if (c.texto[i].empty()) c.texto[i] = !c.texto[1].empty() ? c.texto[1] : c.texto[0];
    }
    c.arte = str("arte").empty() ? id : str("arte");
    c.clasico = str("clasico");
    c.clasico_ii = str("clasico_ii");
    c.vida = str("vida");
    c.rivales = str("rivales");
    c.pausa = str("pausa");
    c.maldicion = str("maldicion");
    c.escalera = Lista(str("escalera"));
    c.combates = std::clamp(int(num("combates", 0)), 0, kPisos);
    c.cada_jefe = std::clamp(int(num("cada_jefe", 5)), 0, kPisos);
    c.cura = int(num("cura", 0));
    c.cura_jefe = int(num("cura_jefe", 9999));
    c.tiempo = int(num("tiempo", 0));
    c.regen = int(num("regen", 0));
    c.premio = std::max(0, int(num("premio", 0)));
    c.vida0 = std::clamp(int(num("vida0", 2000)), 100, 9000);
    c.vida_paso = std::clamp(int(num("vida_paso", 250)), 0, 2000);
    c.abre = std::max(0, int(num("abre", 0)));
    c.abierto = num("abierto", 0) != 0;
    c.en_orden = num("en_orden", 0) != 0;
    {
      std::stringstream rt(str("reglas_tramo"));
      for (std::string t; std::getline(rt, t, ',');) c.reglas_tramo.push_back(t);
    }
    v.push_back(std::move(c));
  }
  return v;
}

Camino CaminoDe(const std::string& id) {
  const std::vector<Camino> v = Caminos();
  for (const Camino& c : v)
    if (c.id == id) return c;
  for (const Camino& c : v)
    if (c.id == "abismo") return c;
  Camino c;   // sin el archivo: El Abismo de ROG-01
  c.id = "abismo";
  c.nombre[0] = "El Abismo";
  for (int i = 1; i < 5; ++i) c.nombre[i] = "The Abyss";
  c.vida = "arrastra";
  c.rivales = "semilla";
  c.pausa = "cartas";
  c.cura = 300;
  c.premio = 800;
  c.abierto = true;
  return c;
}

// La de esta semana (lunes UTC, como SemanaUtc de rpg.cpp; DBZ3_TEST_SEMANA=N la fuerza). Rotan las 4.
Maldicion MaldicionDe(const std::string& id) {
  Maldicion m;
  m.id = id;
  const std::string b = "maldiciones." + m.id + ".";
  auto num = [&](const char* k, double d) { return int(D::Num("rogue_caminos", (b + k).c_str(), d)); };
  m.nombre[0] = D::Str("rogue_caminos", (b + "nombre").c_str(), m.id.c_str());
  m.nombre[1] = D::Str("rogue_caminos", (b + "en").c_str(), m.nombre[0].c_str());
  m.rivales = Lista(D::Str("rogue_caminos", (b + "rivales").c_str(), ""));
  m.tiempo = num("tiempo", 0);
  m.regen = num("regen", 0);
  m.ki = num("ki", 0);
  m.recibido = std::clamp(num("recibido", 100), 10, 300);
  m.armadura = std::clamp(num("armadura", 0), 0, 90);
  return m;
}

Maldicion MaldicionSemana() {
  std::vector<std::string> ids;
  std::stringstream orden(D::Str("rogue_caminos", "maldiciones.orden", ""));
  for (std::string id; std::getline(orden, id, ',');) ids.push_back(id);
  if (ids.empty()) return {};
  const char* e = std::getenv("DBZ3_TEST_SEMANA");
  const int semana = e ? std::atoi(e) : int(std::time(nullptr) / (7 * 86400));
  return MaldicionDe(ids[size_t(uint32_t(semana) % ids.size())]);
}

// ponytail: relee hub_modos.toml en cada llamada; la pantalla 01 lo pide solo al cambiar de fila o de luchador.
int RecordClasico(const std::string& clasico, int jugador) {
  std::ifstream f(dbz3::settings::UserDataRoot() / "hub_modos.toml", std::ios::binary);
  if (!f || clasico.empty()) return 0;
  std::stringstream ss;
  ss << f.rdbuf();
  int mejor = 0;
  try {
    const toml::table t = toml::parse(ss.str());
    if (const toml::array* a = t["marca"].as_array())
      for (const auto& n : *a)
        if (const toml::table* x = n.as_table())
          if ((*x)["tipo"].value_or(std::string()) == clasico && (*x)["id"].value_or(int64_t(-1)) == jugador)
            mejor = std::max(mejor, int((*x)["mejor"].value_or(int64_t(0))));
  } catch (...) {
  }
  return mejor;
}

// ---- Pantalla 01 (px de la maqueta 1280x720 -> area 4:3 de 1200x900, como rogue.cpp) ----
namespace {
namespace S = dbz3::ingame::style;
namespace R = dbz3::survivors;
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLb = 0x0100, kRb = 0x0200, kA = 0x1000, kB = 0x2000, kY = 0x8000;   // como rpg.cpp

const char* Idioma(const std::string* s) { return M::L(s[0].c_str(), s[1].c_str(), s[2].c_str(), s[3].c_str(), s[4].c_str()); }
ImFont* Fuente() {
  const auto& f = dbz3::launcher::ui::GetFonts();
  return f.menu ? f.menu : ImGui::GetFont();
}
float Ancho(float size, const char* s) { return Fuente()->CalcTextSizeA(size, FLT_MAX, 0, s).x; }
void Izq(ImDrawList* dl, ImVec2 p, float size, const char* s, bool oro, bool gris = false, float max_w = 0) {
  float w = Ancho(size, s);
  if (max_w > 0 && w > max_w) {
    size *= max_w / w;
    w = max_w;
  }
  S::MenuTextB3(dl, ImVec2(p.x + w * 0.5f, p.y), size, s, oro, gris);
}
void Der(ImDrawList* dl, ImVec2 p, float size, const char* s, bool oro, float max_w = 0) {   // p = borde derecho
  float w = Ancho(size, s);
  if (max_w > 0 && w > max_w) {
    size *= max_w / w;
    w = max_w;
  }
  S::MenuTextB3(dl, ImVec2(p.x - w * 0.5f, p.y), size, s, oro, false);
}
std::vector<std::string> Partir(const std::string& s, float size, float max_w) {
  std::vector<std::string> v;
  std::istringstream in(s);
  std::string w, l;
  while (in >> w) {
    const std::string c = l.empty() ? w : l + " " + w;
    if (!l.empty() && Ancho(size, c.c_str()) > max_w) {
      v.push_back(l);
      l = w;
    } else {
      l = c;
    }
  }
  if (!l.empty()) v.push_back(l);
  return v;
}
std::string Num(int n) { return std::to_string(n); }
std::string Resumen(const Camino& c) {   // "Sin fin - rivales al azar - la vida se arrastra"
  std::string s = c.combates > 0 ? Num(c.combates) + M::L(" combates", " fights", " combats", " Kämpfe", " incontri")
                                 : std::string(M::L("Sin fin", "Endless", "Sans fin", "Endlos", "Senza fine"));
  s += "  -  ";
  if (c.rivales == "escalera") s += M::L("escalera fija", "fixed ladder", "échelle fixe", "feste Leiter", "scala fissa");
  else if (c.rivales == "jefes") s += M::L("solo jefes", "bosses only", "que des boss", "nur Bosse", "solo boss");
  else if (c.rivales == "elo") s += M::L("rivales por su fuerza", "rivals by strength", "rivaux selon leur force", "Gegner nach Stärke", "rivali per forza");
  else s += M::L("rivales al azar", "random rivals", "rivaux au hasard", "Zufallsgegner", "rivali a caso");
  s += "  -  ";
  s += c.vida == "llena" ? M::L("vida llena", "full health", "vie pleine", "volles Leben", "vita piena")
                         : M::L("la vida se arrastra", "health carries over", "la vie se reporte", "Leben bleibt", "la vita si trascina");
  return s;
}
}  // namespace

bool CaminoAbierto(const Camino& c) {
  return c.abierto || std::getenv("DBZ3_TEST_ROGUE") || RecordCamino("", -1) >= c.abre || CaminoComprado(c.id);   // ROG-06: o comprado en Kami
}

int PantallaCaminos(ImDrawList* dl, const S::Area& area, float t, uint16_t p, int jugador, bool a_medias, int zeni, const char* aviso_rpg) {
  static int cursor = -1, ultimo_cuadro = -10;
  static std::string aviso;
  const std::vector<Camino> cs = Caminos();
  const int nc = int(cs.size()), n = nc + 3;   // + Reto diario, Entrenamiento de Kami (ROG-06/07) y Retos clasicos
  if (ImGui::GetFrameCount() > ultimo_cuadro + 2) {   // pantalla recien abierta: en el camino de la partida o El Abismo
    const std::string actual = a_medias ? CaminoActual() : std::string("abismo");
    cursor = 0;
    for (int i = 0; i < int(cs.size()); ++i)
      if (cs[size_t(i)].id == actual) cursor = i;
    aviso.clear();
  }
  ultimo_cuadro = ImGui::GetFrameCount();
  cursor = std::clamp(cursor, 0, n - 1);
  if (p & (kUp | kDown)) aviso.clear();
  if (p & kUp) cursor = (cursor + n - 1) % n;
  if (p & kDown) cursor = (cursor + 1) % n;
  if (p & kB) return 2;
  if (p & kY) return 4;    // atajos de ROG-06/07 (rpg.cpp): Y reto diario, RB Kami
  if (p & kRb) return 5;
  const bool clasicos = cursor >= nc;
  const Camino* c = clasicos ? nullptr : &cs[size_t(cursor)];
  const int anterior = Dificultad();
  int dsel = anterior;
  // La preferencia de otro camino no abre dificultades en el camino seleccionado.
  if (!a_medias && c && CaminoAbierto(*c)) {
    if (!DificultadAbierta(c->id, dsel)) dsel = 0;
    if (p & kLb) {
      do dsel = (dsel + 1) % 3; while (!DificultadAbierta(c->id, dsel));
      SetDificultad(dsel);
    }
  }
  if (p & kA) {
    if (clasicos) return cursor == nc ? 4 : cursor == nc + 1 ? 5 : 3;
    if (a_medias && c->id != CaminoActual()) {
      aviso = std::string(M::L("Tienes una partida a medias en ", "You have a run in progress in ", "Tu as une partie en cours dans ",
                               "Du hast einen laufenden Lauf in ", "Hai una partita in corso in ")) +
              Idioma(CaminoDe(CaminoActual()).nombre) +
              M::L(": termínala primero.", ": finish it first.", " : termine-la d'abord.", ": beende ihn zuerst.", ": finiscila prima.");
    } else if (!CaminoAbierto(*c)) {
      aviso = std::string(M::L("Se abre con un récord de ", "Opens with a record of ", "S'ouvre avec un record de ", "Öffnet sich mit einem Rekord von ",
                               "Si apre con un record di ")) +
              Num(c->abre) + M::L(" pisos en Dragon Rogue.", " floors in Dragon Rogue.", " étages dans Dragon Rogue.", " Etagen in Dragon Rogue.",
                                  " piani in Dragon Rogue.");
    } else {
      if (!a_medias && dsel != anterior && !(p & kLb)) SetDificultad(dsel);
      SetCamino(c->id);
      return 1;
    }
  }

  const S::Area lienzo = dbz3::heroe_ui::v2::Lienzo(area);
  S::Area a;
  a.s = lienzo.s;
  a.p0 = lienzo.At(200, 0);
  a.p1 = lienzo.At(1400, 900);
  auto P = [&a](float x, float y) { return a.At(x * 1.25f - 200.0f, y * 1.25f); };
  const float u = 1.25f * a.s;
  const S::Imagen im = S::GuestImage(ImGui::GetIO());
  dl->AddRectFilled(im.p0, im.p1, IM_COL32(8, 22, 34, 236));   // velo: tapa al anfitrion del selector
  S::BannerBeta(dl, a, "Dragon Rogue");

  // izquierda: los caminos y Retos clasicos
  S::Frame(dl, P(40, 128), P(545, 672), a.s);
  static const char* const kExtra[3][5] = {
      {"Reto diario", "Daily challenge", "Défi du jour", "Tagesaufgabe", "Sfida del giorno"},
      {"Entrenamiento de Kami", "Kami's training", "Entraînement de Kami", "Kamis Training", "Allenamento di Kami"},
      {"Retos clásicos", "Classic challenges", "Défis classiques", "Klassische Herausforderungen", "Sfide classiche"}};
  for (int i = 0; i < n; ++i) {
    const float y = i < nc ? 160 + 46.0f * float(i) : 160 + 46.0f * float(nc) + 18 + 46.0f * float(i - nc);
    if (i == cursor) S::mode::Stripe(dl, a, 54 * 1.25f - 200, (y - 20) * 1.25f, 530 * 1.25f - 200, (y + 20) * 1.25f, t);
    if (i >= nc) {
      if (i == nc) dl->AddLine(P(54, y - 32), P(528, y - 32), IM_COL32(232, 140, 32, 255), 1.5f * u);
      const char* const* e = kExtra[i - nc];
      Izq(dl, P(78, y), 28 * u, M::L(e[0], e[1], e[2], e[3], e[4]), i == cursor, false, 420 * u);
      continue;
    }
    const Camino& x = cs[size_t(i)];
    const bool abierto = CaminoAbierto(x);
    Izq(dl, P(78, y), 28 * u, Idioma(x.nombre), i == cursor, !abierto, 300 * u);
    if (!abierto) R::DibujarAjustada(dl, R::ArteMejoras("rogue/ui_candado.png"), P(482, y - 18), P(518, y + 18));
    else if (a_medias && x.id == CaminoActual()) Der(dl, P(520, y), 18 * u, M::L("a medias", "in progress", "en cours", "läuft", "in corso"), false);
  }

  // derecha: el camino elegido
  S::Frame(dl, P(600, 128), P(1245, 580), a.s);
  if (clasicos && cursor < nc + 2) {   // ROG-06/07: sus pantallas (rogue_resultado.cpp)
    const char* const* e = kExtra[cursor - nc];
    Izq(dl, P(640, 180), 40 * u, M::L(e[0], e[1], e[2], e[3], e[4]), true);
    const char* txt = cursor == nc ? M::L("El mismo camino, luchador y ofertas para todos hoy. Un intento puntuable al día.",
                                          "Same path, fighter and offers for everyone today. One scored attempt per day.",
                                          "Même chemin, combattant et offres pour tous aujourd'hui. Un essai noté par jour.",
                                          "Heute für alle derselbe Weg, Kämpfer und dieselben Angebote. Ein gewerteter Versuch pro Tag.",
                                          "Oggi stesso percorso, lottatore e offerte per tutti. Un tentativo a punti al giorno.")
                                   : M::L("Gasta tus Zenie en mejoras para siempre: caminos, cartas nuevas y ventajas de salida.",
                                          "Spend your Zenie on permanent upgrades: paths, new cards and starting perks.",
                                          "Dépense tes Zenie en améliorations permanentes : chemins, nouvelles cartes et bonus de départ.",
                                          "Gib deine Zenie für dauerhafte Verbesserungen aus: Wege, neue Karten und Startvorteile.",
                                          "Spendi i tuoi Zenie in potenziamenti permanenti: percorsi, nuove carte e vantaggi iniziali.");
    float y = 260;
    for (const std::string& l : Partir(txt, 26 * u, 590 * u)) {
      Izq(dl, P(626, y), 26 * u, l.c_str(), false);
      y += 34;
    }
  } else if (clasicos) {
    Izq(dl, P(640, 180), 40 * u, M::L("Retos clásicos", "Classic challenges", "Défis classiques", "Klassische Herausforderungen", "Sfide classiche"), true);
    float y = 260;
    for (const std::string& l : Partir(M::L("Los modos de siempre, tal cual: Torre, Torres del Tiempo, Abyss, Supervivencia, Tienda, Simulación, "
                                            "Kumite, Kumite de jefes y Torneo de 4. Mismas reglas, mismos récords.",
                                            "The usual modes, as they are: Tower, Towers of Time, Abyss, Survival, Shop, Simulation, Kumite, "
                                            "Boss Kumite and 4-fighter Tournament. Same rules, same records.",
                                            "Les modes habituels, tels quels : Tour, Tours du Temps, Abysse, Survie, Boutique, Simulation, "
                                            "Kumite, Kumite des boss et Tournoi à 4. Mêmes règles, mêmes records.",
                                            "Die üblichen Modi, unverändert: Turm, Zeittürme, Abyss, Überleben, Laden, Simulation, Kumite, "
                                            "Boss-Kumite und Turnier zu viert. Gleiche Regeln, gleiche Rekorde.",
                                            "Le solite modalità, così come sono: Torre, Torri del Tempo, Abyss, Sopravvivenza, Negozio, "
                                            "Simulazione, Kumite, Kumite dei boss e Torneo a 4. Stesse regole, stessi record."),
                                       26 * u, 590 * u)) {
      Izq(dl, P(626, y), 26 * u, l.c_str(), false);
      y += 34;
    }
  } else {
    R::DibujarAjustada(dl, R::ArteMejoras(("rogue/camino_" + (c->arte.empty() ? c->id : c->arte) + ".png").c_str()), P(620, 140), P(708, 228));
    Izq(dl, P(732, 172), 42 * u, Idioma(c->nombre), true, false, 490 * u);
    Izq(dl, P(732, 213), 19 * u, Resumen(*c).c_str(), false, false, 490 * u);
    dl->AddLine(P(614, 246), P(1228, 246), IM_COL32(232, 140, 32, 255), 1.5f * u);
    float y = 280;
    std::string txt = Idioma(c->texto);
    if (c->maldicion == "semana") {
      const Maldicion m = MaldicionSemana();
      txt += std::string(" ") + M::L("Esta semana: ", "This week: ", "Cette semaine : ", "Diese Woche: ", "Questa settimana: ") +
             M::L(m.nombre[0].c_str(), m.nombre[1].c_str(), m.nombre[1].c_str(), m.nombre[1].c_str(), m.nombre[1].c_str()) + ".";
    }
    for (const std::string& l : Partir(txt, 24 * u, 590 * u)) {
      Izq(dl, P(626, y), 24 * u, l.c_str(), false);
      y += 32;
    }
    const int rec = RecordCamino(c->id, jugador), cla = RecordClasico(c->clasico, jugador);
    const std::string premio = std::string(M::L("50 por piso + 300 por jefe", "50 per floor + 300 per boss", "50 par étage + 300 par boss",
                                                 "50 pro Etage + 300 pro Boss", "50 per piano + 300 per boss")) +
                               (c->premio > 0 ? " + " + Num(c->premio) : std::string());
    const std::string fila_rec = rec > 0 ? std::string(M::L("Piso ", "Floor ", "Étage ", "Etage ", "Piano ")) + Num(rec) : std::string("-");
    const std::string fila_cla = cla > 0 ? Num(cla) : std::string("-");
    const std::pair<std::string, std::string> filas[] = {
        {M::L("Récord Rogue", "Rogue record", "Record Rogue", "Rogue-Rekord", "Record Rogue"), fila_rec},
        {M::L("Récord clásico", "Classic record", "Record classique", "Klassischer Rekord", "Record classico"), fila_cla},
        {M::L("Premio", "Prize", "Prix", "Preis", "Premio"), premio}};
    float fy = std::max(y + 6, 352.0f);
    for (const auto& [nombre, valor] : filas) {
      if (fy > 520) break;
      dl->AddRectFilled(P(622, fy), P(1220, fy + 44), IM_COL32(34, 98, 138, 235), 9 * u);
      dl->AddRect(P(622, fy), P(1220, fy + 44), IM_COL32(90, 160, 200, 200), 9 * u, 0, 1.2f * u);
      Izq(dl, P(650, fy + 22), 22 * u, nombre.c_str(), false);
      Der(dl, P(1190, fy + 22), 22 * u, valor.c_str(), &nombre == &filas[0].first && rec > 0, 330 * u);
      fy += 54;
    }
    if (CaminoAbierto(*c)) {   // PM-24: dificultad I / II / III (arte de Kami); LB la cambia
      Izq(dl, P(650, 532), 22 * u, M::L("Dificultad", "Difficulty", "Difficulté", "Schwierigkeit", "Difficoltà"), false);
      for (int d = 0; d < 3; ++d) {
        const bool ab = DificultadAbierta(c->id, d);
        const float x0 = 860.0f + 90.0f * float(d);
        if (d == dsel) dl->AddRectFilled(P(x0 - 6, 506), P(x0 + 82, 560), IM_COL32(34, 98, 138, 235), 8 * u);
        const ps2::Tex* ic = R::ArteMejoras(("rogue/kami_dificultad_" + Num(d + 1) + ".png").c_str());
        R::DibujarAjustada(dl, ic, P(x0, 510), P(x0 + 46, 556));
        if (!ab) R::DibujarAjustada(dl, R::ArteMejoras("rogue/ui_candado.png"), P(x0 + 22, 530), P(x0 + 46, 556));
        Izq(dl, P(x0 + 54, 532), 24 * u, d == 0 ? "I" : d == 1 ? "II" : "III", d == dsel, !ab, 40 * u);
      }
    }
    if (!CaminoAbierto(*c)) {
      const std::string s = std::string(M::L("Se abre con un récord de ", "Opens with a record of ", "S'ouvre avec un record de ",
                                             "Öffnet sich mit einem Rekord von ", "Si apre con un record di ")) +
                            Num(c->abre) + M::L(" pisos (tienes ", " floors (you have ", " étages (tu as ", " Etagen (du hast ", " piani (hai ") +
                            Num(RecordCamino("", -1)) + ")";
      R::DibujarAjustada(dl, R::ArteMejoras("rogue/ui_candado.png"), P(626, 540), P(656, 570));
      Izq(dl, P(666, 556), 20 * u, s.c_str(), false, false, 540 * u);
    }
  }

  // abajo: luchador (izquierda/derecha lo cambia en rpg.cpp) y Zenie
  dl->AddRectFilled(P(590, 606), P(1250, 652), IM_COL32(8, 24, 36, 230), 10 * u);
  Izq(dl, P(612, 629), 22 * u, M::L("Luchador:", "Fighter:", "Combattant :", "Kämpfer:", "Lottatore:"), false);
  if (const S::Pic cara = S::Img(("caras/" + Num(jugador)).c_str())) S::DrawPic(dl, cara, P(750, 609), P(790, 649));
  Izq(dl, P(800, 629), 22 * u, M::Nombre(jugador).c_str(), true, false, 160 * u);
  if (!a_medias) S::Arrows(dl, P(742, 606), P(986, 652), 30 * u, t);
  Izq(dl, P(1040, 629), 22 * u, "Zenie", false);
  Der(dl, P(1232, 629), 26 * u, Num(zeni).c_str(), true);
  const std::string& av = !aviso.empty() ? aviso : std::string(aviso_rpg ? aviso_rpg : "");
  if (!av.empty()) Izq(dl, P(600, 590), 19 * u, av.c_str(), true, false, 640 * u);
  S::mode::Help(dl, a, {{"A", M::L("Elegir", "Choose", "Choisir", "Wählen", "Scegli")}, {"LB", M::L("Dificultad", "Difficulty", "Difficulté", "Schwierigkeit", "Difficoltà")}, {"B", M::L("Volver", "Back", "Retour", "Zurück", "Indietro")}});
  fx::Pintar(dl, a, fx::Tipo::Camino);
  return 0;
}

}  // namespace dbz3::rogue

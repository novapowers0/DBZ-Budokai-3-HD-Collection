// dbz3 - Dibujo del HUB (ver hub_panel.h): la fila "HUB" bajo la lista del menu principal y, dentro de su
// propio marco, la pantalla HUB (el juego sigue dibujando su fondo, su marco, su cinta, su bocadillo y su
// "BACK"; src/hub_menu.cpp le quita sus etiquetas, su franja y el texto de la cinta, y mueve sus
// animaciones de entrada y salida). Medidas de capturas 1600x900 del menu principal (area 4:3 desde x=200):
// interior del marco x 110..568 y 85..590, filas cada 83 px, franja de 80 px; bocadillo 30..790 x 650..858
// con el texto centrado en x 409, renglones cada 45 px; papel de la cinta del titulo x 760..1160, y 25..175.
// Piezas (res/ingame_ui, de assets_propios/generar.py): etq_<idioma>_<clave>, modo_<clave>, beta,
// hub/logo (logo "Dragon HUB" del autor del proyecto), titulo_hub y num_* (cifras de la pagina). Del juego
// (native_ui.cpp, sacadas del AFS del jugador): agua, marco, panel, flechas, botones, bocadillo, borde.
// De un pack opcional del jugador (mod center hd/packs_visuales/pack_hub.toml): hub/fondo y hub/vuelo_<n>
// (personaje que vuela hacia la camara al entrar; sin el, solo el fundido) y hub/guardado (icono del aviso de
// guardado; sin el pack, el nuestro CC0 de generar.py).
#include "../reloj_prueba.h"
#include "hub_panel.h"
#include "tutorial.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>

namespace dbz3::campaigns {
bool DuCover();   // campaigns.cpp: el Heroe entra en DU: pantalla en negro hasta su primera escena
}
#include <toml++/toml.hpp>

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include "../launcher/i18n.h"
#include "../launcher/settings.h"
#include "../launcher/ui_kit.h"
#include "../escenarios.h"
#include "../modos.h"
#include "../mods_opcionales.h"
#include "../pasarela.h"
#include "game_style.h"

namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
namespace tag {
bool InBattle();   // tagteam.cpp: hay un combate en marcha
}
namespace hub {
bool OwnBattle();  // hub.cpp: combate del conector en marcha (retos, combate rapido)
}
}

namespace dbz3::hub {
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;

// textos propios del panel, en los idiomas del juego (Tr)
constexpr Texts kSoon = {"Muy pronto.", "Coming soon.", "Bientôt disponible.", "Bald verfügbar.", "Prossimamente."};
constexpr Texts kRowHelp = {"Tag Team, Online, combate rápido, retos y los modos nuevos de esta versión.",
                            "Tag Team, Online, quick battle, challenges and this version's new modes.",
                            "Tag Team, en ligne, combat rapide, défis et les nouveaux modes de cette version.",
                            "Tag Team, Online, Schnellkampf, Herausforderungen und die neuen Modi dieser Version.",
                            "Tag Team, online, scontro rapido, sfide e le nuove modalità di questa versione."};
constexpr Texts kRecord = {"Récord: ", "Record: ", "Record : ", "Rekord: ", "Record: "};
constexpr Texts kWith = {" con ", " with ", " avec ", " mit ", " con "};
constexpr Texts kNoRecord = {"Sin récord", "No record", "Pas de record", "Kein Rekord", "Nessun record"};
constexpr Texts kSavedAt = {" - Guardado: ", " - Saved: ", " - Sauvegardé : ", " - Gespeichert: ", " - Salvato: "};
constexpr Texts kClassic = {"Clásica: ", "Classic: ", "Classique : ", "Klassisch: ", "Classica: "};
constexpr Texts kLastQuick = {"Último: ", "Last: ", "Dernier : ", "Zuletzt: ", "Ultimo: "};
constexpr Texts kVs = {" contra ", " vs ", " contre ", " gegen ", " contro "};
constexpr Texts kAnd = {" y ", " and ", " et ", " und ", " e "};
constexpr Texts kWallet = {"Monedero: ", "Wallet: ", "Porte-monnaie : ", "Geldbörse: ", "Portafoglio: "};
// Torre/Supervivencia/Kumite abren el selector de retos de rpg.cpp (docs/14_frentes/rpg.md): su lista y su ayuda
// ponytail: listas copiadas de rpg.md; si rpg.cpp cambia sus listas, cambiarlas aqui (o que exponga una API)
constexpr int kLists[3][4] = {{dbz3::modos::kTorre, dbz3::modos::kTorres, dbz3::modos::kAbyss, -1},
                              {dbz3::modos::kSupervivencia, dbz3::modos::kTienda, dbz3::modos::kSimulacion, -1},
                              {dbz3::modos::kKumite, dbz3::modos::kJefes, dbz3::modos::kTorneo4, dbz3::modos::kHeroe}};
constexpr Texts kListHelp[3] = {
    {"Sube piso a piso contra rivales cada vez más fuertes. Elige el reto en la lista.",
     "Climb floor by floor against ever stronger rivals. Pick the challenge from the list.",
     "Monte étage par étage face à des rivaux toujours plus forts. Choisis le défi dans la liste.",
     "Steige Etage für Etage gegen immer stärkere Gegner auf. Wähle die Herausforderung aus der Liste.",
     "Sali piano dopo piano contro rivali sempre più forti. Scegli la sfida dalla lista."},
    {"Combates seguidos con la vida que te queda. Elige el reto en la lista.",
     "Back-to-back fights with the health you have left. Pick the challenge from the list.",
     "Des combats à la suite avec la vie qui te reste. Choisis le défi dans la liste.",
     "Kämpfe am Stück mit deiner restlichen Energie. Wähle die Herausforderung aus der Liste.",
     "Scontri di fila con la vita che ti resta. Scegli la sfida dalla lista."},
    {"Jefes uno tras otro, un torneo de 4 y misiones de héroe. Elige el reto en la lista.",
     "Bosses one after another, a 4-fighter tournament and hero missions. Pick the challenge from the list.",
     "Des boss l'un après l'autre, un tournoi à 4 et des missions de héros. Choisis le défi dans la liste.",
     "Bosse nacheinander, ein Turnier zu viert und Heldenmissionen. Wähle die Herausforderung aus der Liste.",
     "Boss uno dopo l'altro, un torneo a 4 e missioni da eroe. Scegli la sfida dalla lista."}};
constexpr Texts kWord[3] = {{"piso", "floor", "étage", "Etage", "piano"},
                            {"oleada", "wave", "vague", "Welle", "ondata"},
                            {"jefe", "boss", "boss", "Boss", "boss"}};
// aviso de guardado (el juego: "Guardando contenido. No apagues tu consola." y "Guardado con exito.")
constexpr Texts kSaving = {"Guardando datos del HUB.\nNo cierres el juego.", "Saving HUB data.\nDo not close the game.",
                           "Sauvegarde des données du HUB.\nNe ferme pas le jeu.",
                           "HUB-Daten werden gespeichert.\nSchließe das Spiel nicht.",
                           "Salvataggio dei dati dell'HUB.\nNon chiudere il gioco."};
constexpr Texts kSaved = {"Guardado con éxito.", "Saved successfully.", "Sauvegarde réussie.", "Erfolgreich gespeichert.",
                          "Salvataggio riuscito."};
constexpr int kSayRow = kMaxEntries;   // el bocadillo dice lo de la fila HUB (no una entrada)

// geometria (px del area 4:3 a 900 de alto)
constexpr float kInX0 = 110, kInX1 = 568;                  // interior del marco del menu
// 8 filas en el sitio de las 6 del juego; franjas en 87..544 para que el marco propio (kFrame*, con su linea fuera de
// ellas, como el Duelo) acabe antes del recuadro de pagina (kBoxY0) y el bocadillo propio (kBubbleY0) despues
constexpr float kRow0 = 114, kPitch = 57.5f, kStripe = 54;
constexpr float kFrameY0 = 67, kFrameY1 = 566, kBubbleY0 = 684, kBubbleY1 = 864;
constexpr float kIconX = 120, kLabelX = 180, kLabelK = 0.6f, kLabelMax = 372;
// la fila HUB bajo el marco del menu principal, como ONLINE bajo el del Duelo: colgada de la linea de abajo del marco
// (sus esquinas la pisan) y su linea de abajo antes del bocadillo del juego (y 650). Entre el marco (~600) y el
// bocadillo no cabe entera: antes 606..650 pisaba el bocadillo (captura 20261016-021719_combate_test_iw_minijuego)
constexpr float kBoxX0 = 160, kBoxY0 = 596, kBoxX1 = 520, kBoxY1 = 636;
constexpr float kLogoX = 962, kLogoY = 94, kLogoW = 400;   // logo sobre el papel de la cinta
constexpr ImU32 kDim = IM_COL32(148, 148, 148, 255), kOff = IM_COL32(90, 90, 110, 200);
constexpr int kMaxFlyers = 8;                              // hub/vuelo_1..8 del pack
// Aviso de guardado, medido en el del juego (Opciones > Guardar partida, 2026-10-15, capturas
// D:/DBZ3HD/_pruebas/20261015-085521_hub_test_guardar_si): el area 4:3 se oscurece al 22 % (las tiras de los
// lados no), caja verde translucida centrada (x 60..1140, y 231..548 del area 1200x900), filo verde claro de 1 px,
// letra blanca perfilada centrada (renglones cada 40 px). "Guardando contenido" ~3 s (aunque escriba en 50 ms),
// "Guardado con exito" ~2 s; entra y sale escalando 0,85 -> 1 con fundido (~0,3 s).
// La letra del juego mide ~35 px de em a 900p; ImGui da el tamano por alto de renglon (em x 1,33): 46.
constexpr float kSaveX0 = 60, kSaveY0 = 231, kSaveX1 = 1140, kSaveY1 = 548, kSaveText = 46, kSaveStep = 40;
constexpr float kSaveDim = 0.78f, kSaveFill = 0.55f, kSaveIconH = 150;
constexpr int64_t kSavingMs = 3000, kSavedMs = 2000, kSaveAnimMs = 300;
std::atomic<int64_t> g_saving_at{-100000};

// Icono del guardado al tamano mas cercano por arriba (sin mipmaps: reducir mas de 2x se ve con dientes).
S::Pic SaveIcon(float h) {
  S::Pic p;
  if (h <= 64) p = S::Img("hub/guardado_64");
  if (!p && h <= 128) p = S::Img("hub/guardado_128");
  return p ? p : S::Img("hub/guardado");
}

float Ease(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}
float Clamp01(float t) { return std::clamp(t, 0.0f, 1.0f); }
ImU32 Alpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | (ImU32(float(c >> 24) * Clamp01(a)) << 24); }
void FadeVerts(ImDrawList* dl, int from, float a) {
  for (int i = from; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].col = Alpha(dl->VtxBuffer[i].col, a);
}

// Etiqueta de una entrada: la pieza con la letra del menu (etq_<idioma>_<clave>, 2x del atlas del juego)
// con el borde izquierdo en x y centrada en y; sin ella, la fuente. Si pasa de max_w (px del area), se
// encoge. Devuelve el borde derecho.
float Label(ImDrawList* dl, const S::Area& a, const char* key, const char* name, float x, float y, float k,
            ImU32 tint, bool selected, float max_w = 1e9f) {
  std::string pic = std::string("etq_") + T::LangCode() + "_" + key;
  S::Pic p = S::Img(pic.c_str());
  if (!p) p = S::Img((std::string("etq_en_") + key).c_str());
  if (p) {
    const float kk = std::min(k, max_w / p.w);
    // alineadas por arriba (las piezas con g/p son mas altas: centradas bailaban); encogida, centrada
    const float h = p.h * kk * a.s, w = p.w * kk * a.s;
    const float top = a.At(0, y).y - 94 * k * a.s * 0.5f + (k - kk) * 47 * a.s;
    const ImVec2 p0(a.At(x, 0).x, top);
    S::DrawPic(dl, p, p0, ImVec2(p0.x + w, p0.y + h), ImVec4(0, 0, 0, 0), tint);
    return x + p.w * kk;
  }
  const char* text = name;
  ImFont* font = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();
  float size = 52 * k / 0.66f * a.s;
  float w = font->CalcTextSizeA(size, FLT_MAX, 0, text).x;
  if (w > max_w * a.s) {
    size *= max_w * a.s / w;
    w = max_w * a.s;
  }
  const int v0 = dl->VtxBuffer.Size;
  S::MenuText(dl, ImVec2(a.At(x, 0).x + w * 0.5f, a.At(0, y).y), size, text, selected, false);
  for (int i = v0; i < dl->VtxBuffer.Size; ++i) {   // mismo tinte que las piezas
    ImU32& c = dl->VtxBuffer[i].col;
    const auto mul = [&](int sh) { return ImU32(((c >> sh) & 0xFF) * ((tint >> sh) & 0xFF) / 255) << sh; };
    c = mul(0) | mul(8) | mul(16) | mul(24);
  }
  return x + w / a.s;
}

// Texto en el bocadillo del menu principal (el del juego queda vacio): letra redonda gris oscuro; `title`
// (opcional) en un renglon propio en naranja oscuro, como los nombres del juego; `marks` (opcional) en el
// ultimo renglon con el icono del guardado delante (hub/guardado).
// cy/room: centro y alto libre (px del area) del bocadillo; de serie, los del bocadillo del juego.
void BubbleText(ImDrawList* dl, const S::Area& a, const std::string& title, const char* text, float alpha,
                const std::string& marks = {}, float cy = 754, float room = 1e9f) {
  if (alpha <= 0.01f) return;
  ImFont* font = dbz3::launcher::ui::GetFonts().menu;
  if (!font) font = ImGui::GetFont();
  float size = 31 * a.s, step = 45 * a.s;
  const float wrap = 600 * a.s;
  struct Line {
    const char *s, *e;
    int kind;   // 0 texto, 1 titulo, 2 marcas (el primero lleva el icono)
  };
  std::vector<Line> lines;
  const S::Pic icon = marks.empty() ? S::Pic{} : SaveIcon(size * 1.15f);
  auto split = [&](const char* s, int kind) {
    while (*s) {
      const float w = kind == 2 && icon && (lines.empty() || lines.back().kind != 2) ? wrap - size * 1.3f : wrap;
      const char* e = font->CalcWordWrapPosition(size, s, s + std::strlen(s), w);
      if (e == s) e = s + 1;
      lines.push_back({s, e, kind});
      s = e;
      while (*s == ' ' || *s == '\n') ++s;
    }
  };
  for (int k = 0; k < 4; ++k) {   // caben 4 renglones; con mas, letra y renglon mas pequenos
    lines.clear();
    if (!title.empty()) split(title.c_str(), 1);
    split(text, 0);
    if (!marks.empty()) split(marks.c_str(), 2);
    if (lines.size() <= 4 && step * float(lines.size()) <= room * a.s) break;
    size *= 0.88f;
    step *= 0.84f;
  }
  float y = a.At(0, cy).y - step * float(lines.size()) * 0.5f;
  bool first_mark = true;
  for (const Line& l : lines) {
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, l.s, l.e);
    const ImU32 col = l.kind == 1 ? IM_COL32(176, 70, 14, int(255 * alpha)) : IM_COL32(52, 52, 52, int(255 * alpha));
    float x = a.At(409, 0).x - ts.x * 0.5f;
    if (l.kind == 2 && first_mark && icon) {   // icono del guardado, un poco mas alto que la letra
      first_mark = false;
      const float h = size * 1.15f, w = icon.w * h / icon.h, gap = size * 0.25f;
      x += (w + gap) * 0.5f;
      const ImVec2 p0(x - gap - w, y + (step - h) * 0.5f);
      S::DrawPic(dl, icon, p0, ImVec2(p0.x + w, p0.y + h), ImVec4(0, 0, 0, 0), IM_COL32(255, 255, 255, int(255 * alpha)));
    }
    dl->AddText(font, size, ImVec2(x, y + (step - ts.y) * 0.5f), col, l.s, l.e);
    y += step;
  }
}

// Reto puesto en user_data/dbz3/modos/<torre|supervivencia|kumite>.toml (modos_reto.py /
// retos_predefinidos.py): su nombre, su descripcion y cuantos combates tiene. Se relee como mucho cada 2 s.
struct Reto {
  std::string name, desc;
  int fights = 0;
  bool file = false;
  std::filesystem::file_time_type mtime{};
  double checked = -10;
};
const Reto& RetoOf(int e, double t) {
  static Reto cache[3];
  Reto& r = cache[e - kTower];
  if (t - r.checked < 2.0) return r;
  r.checked = t;
  const auto path = dbz3::settings::UserDataRoot() / "modos" / (std::string(EntryKey(e)) + ".toml");
  std::error_code ec;
  const auto mt = std::filesystem::last_write_time(path, ec);
  if (ec) {
    r = Reto{};
    r.checked = t;
    return r;
  }
  if (r.file && mt == r.mtime) return r;
  r = Reto{};
  r.checked = t;
  r.file = true;
  r.mtime = mt;
  try {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const toml::table tb = toml::parse(ss.str());
    const bool es = std::strcmp(T::LangCode(), "es") == 0;
    auto str = [&](const char* k1, const char* k2) {
      std::string v = tb[es ? k1 : k2].value_or(std::string());
      if (v.empty()) v = tb[es ? k2 : k1].value_or(std::string());
      return v;
    };
    r.name = str("nombre", "name");
    r.desc = str("descripcion", "description");
    if (const toml::array* arr = tb["combate"].as_array()) r.fights = int(arr->size());
  } catch (const std::exception&) {
    r.name.clear();   // fichero roto: el juego usa el de serie
    r.file = false;
  }
  return r;
}

// Monedero de Zenie del HUB (user_data/dbz3/rpg.toml, clave zeni, de rpg.cpp; -1 sin el fichero). Se relee como
// mucho cada 2 s.
int Wallet(double t) {
  static int zeni = 0;
  static double checked = -10;
  static std::filesystem::file_time_type seen{};
  if (t - checked < 2.0) return zeni;
  checked = t;
  const auto path = dbz3::settings::UserDataRoot() / "rpg.toml";
  std::error_code ec;
  const auto mt = std::filesystem::last_write_time(path, ec);
  if (ec) return zeni = -1;   // sin rpg.toml: sin monedero
  if (mt == seen) return zeni;
  seen = mt;
  try {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    zeni = int(toml::parse(ss.str())["zeni"].value_or(int64_t(0)));
  } catch (const std::exception&) {
    zeni = 0;
  }
  return zeni;
}

// "Récord: piso 5/8 con Goku - Guardado: piso 3" (modos.cpp; tipo 0 torre, 1 supervivencia, 2 kumite).
// *any = hay record o reto guardado (el bocadillo pone entonces el icono del guardado delante).
std::string Marks(int tipo, bool* any) {
  const char* w = Tr(kWord[tipo]);
  int id = 0, best = 0, total = 0, done = 0, won = 0;
  const bool rec = dbz3::modos::Record(tipo, &id, &best, &total, &done);
  std::string s = rec ? std::string(Tr(kRecord)) + w + " " + std::to_string(best) + "/" + std::to_string(total) +
                            Tr(kWith) + dbz3::modos::Nombre(id)
                      : std::string(Tr(kNoRecord));
  const bool saved = dbz3::modos::Guardado(tipo, &id, &won);
  if (saved) s += std::string(Tr(kSavedAt)) + w + " " + std::to_string(won + 1);
  *any = rec || saved;
  return s;
}

class HubPanel : public rex::ui::ImGuiDialog {
 public:
  explicit HubPanel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    // El aviso de guardado va encima de las pantallas de los modos (dialogos creados despues, p. ej. la del
    // Laboratorio): al empezar uno, este dialogo pasa al final de la lista y el dibujante lo vuelve a llamar en
    // este mismo fotograma, el ultimo.
    if (const int64_t at = g_saving_at.load(); at != front_at_) {
      front_at_ = at;
      imgui_drawer()->RemoveDialog(this);
      imgui_drawer()->AddDialog(this);
      return;
    }
    if (dbz3::GameDimmed() || dbz3::OverlayOpen()) return;
    if (dbz3::campaigns::DuCover())   // el Heroe entra en DU sin que se vean su titulo, su menu ni su rueda de personajes
      ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 255));
    const View v = Snapshot();
    const float t = float(dbz3::RelojT(ImGui::GetTime())), dt = dbz3::RelojDt(std::min(io.DeltaTime, 0.1f));
    const bool shown = v.menu && !v.other;
    const float el = float(ClockMs() - v.fade_t0);
    // fundido a negro de toda la ventana (como el del juego entre sus pantallas)
    float black = 0;
    if (v.menu && v.fade == Fade::kOut) black = Clamp01(el / float(kFadeOutMs));
    if (v.menu && v.fade == Fade::kIn) black = 1.0f - Clamp01(el / float(kFadeInMs));
    if (v.fade == Fade::kOut && v.fade_to_hub && v.fade_t0 != fly_t0_) {   // un vuelo nuevo: el siguiente del pack
      fly_t0_ = v.fade_t0;
      fly_ = NextFlyer();
    }
    // la fila HUB: con el menu activo (entra al acabar de entrar el juego, sale con el)
    const bool row = shown && v.state == 3 && !v.leaving && v.fade == Fade::kNone;
    row_a_ = std::clamp(row_a_ + (row ? dt * 4.0f : -dt * 6.0f), 0.0f, 1.0f);
    // pantalla HUB: las filas entran escalonadas al abrirse y se van con el menu al salir
    const bool closing = v.fade == Fade::kOut && !v.fade_to_hub;
    const bool screen = shown && (v.open || v.leaving);
    if (screen && !was_open_) opened_at_ = t;
    was_open_ = screen;
    if (v.open && !closing) screen_a_ = 1.0f;
    else screen_a_ = screen ? std::max(0.0f, screen_a_ - dt * (closing ? 6.0f : 4.0f)) : 0.0f;
    auto* dl = ImGui::GetForegroundDrawList();
    has_bg_ = false;   // lo pone DrawScreen si el pack trae fondo
    if (shown) {
      const S::Area a = S::GameArea(io);
      // el bocadillo: su texto entra al cambiar (el del juego se funde a la vez)
      const int say = v.open && !closing ? v.cursor : (v.row_focus && v.fade == Fade::kNone ? kSayRow : -1);
      if (say != said_) {
        said_ = say;
        said_at_ = t;
      }
      if (screen_a_ > 0.0f) DrawScreen(dl, a, v, t, dt);
      if (v.open && !closing && v.fade == Fade::kNone) dbz3::ingame::tutorial::Mostrar("hub");   // F3
      if (row_a_ > 0.0f) DrawRow(dl, a, v, t);
      if (say >= 0 && (row_a_ > 0.0f || screen_a_ > 0.0f)) DrawHelp(dl, a, say, t, std::max(row_a_, screen_a_));
    }
    if (black > 0.0f) dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * black)));
    if (v.menu && v.fade != Fade::kNone && v.fade_to_hub) DrawFlyer(dl, io, v);
    WatchSaves(t);
    dbz3::musica::ActualizarCombate(dbz3::tag::InBattle());   // X1: musica de combate elegida por el jugador
    // el aviso, solo con el HUB o el menu principal a la vista y quietos: nunca en combate (ni en la espera en negro de
    // los encadenados), ni encima de las pantallas propias de los modos (selector, Laboratorio, entre combates,
    // pantalla final), ni en un fundido. Un guardado entonces espera y sale entero al volver (rpg.md, propuesta 2).
    const int64_t since = ClockMs() - g_saving_at.load();
    const bool calm = shown && v.fade == Fade::kNone && !OwnBattle() && !dbz3::tag::InBattle();
    TestSe(v, t);
    if (!calm) {
      if (since >= 0 && since <= kSavingMs + kSavedMs + kSaveAnimMs) save_wait_ = true;
    } else if (save_wait_) {
      save_wait_ = false;
      g_saving_at = ClockMs();
    } else {
      DrawSaving(dl, io);
    }
  }

 private:
  // Recuadro de la fila HUB bajo la lista (donde estaba el de TAG TEAM): marco del juego, agua si es la
  // elegida y la etiqueta con la letra del menu. En la pantalla HUB con varias paginas: LB < 1/2 > RB.
  void DrawRow(ImDrawList* dl, const S::Area& a, const View& v, float t) {
    const int v0 = dl->VtxBuffer.Size;
    const bool lit = v.row_focus || v.open;
    const ImVec2 r0 = a.At(kBoxX0, kBoxY0), r1 = a.At(kBoxX1, kBoxY1);
    // sin el parche de online_panel su recuadro TAG TEAM sigue debajo: se tapa entero
    dl->AddRectFilled(r0, r1, IM_COL32(8, 40, 56, 235));
    S::Frame(dl, r0, r1, a.s);
    if (lit) S::Highlight(dl, r0, r1, t);
    int vis[kMaxEntries];
    const int n = VisibleEntries(vis);
    const int pages = (n + kPerPage - 1) / kPerPage;
    if (v.open && pages > 1) {
      int pos = 0;
      while (pos < n && vis[pos] != v.cursor) ++pos;
      DrawPager(dl, a, std::min(pos, n - 1) / kPerPage, pages, t);
    } else {
      const S::Pic p = S::Img((std::string("etq_") + T::LangCode() + "_hub").c_str());
      const float k = 0.66f, w = p ? p.w * k : 120;
      Label(dl, a, "hub", "HUB", (kBoxX0 + kBoxX1 - w) * 0.5f, (kBoxY0 + kBoxY1) * 0.5f, k,
            lit ? IM_COL32_WHITE : kDim, lit);
    }
    FadeVerts(dl, v0, row_a_);
  }

  // Pagina: botones LB/RB del juego a los lados, flechas del juego y "1/2" con cifras doradas (num_*).
  void DrawPager(ImDrawList* dl, const S::Area& a, int page, int pages, float t) {
    const float cy = (kBoxY0 + kBoxY1) * 0.5f, gh = 30 * a.s;
    S::Glyph(dl, "LB", a.At(kBoxX0 + 14, cy), gh);
    const float rb = S::Glyph(nullptr, "RB", ImVec2(0, 0), gh);
    S::Glyph(dl, "RB", ImVec2(a.At(kBoxX1 - 14, cy).x - rb, a.At(0, cy).y), gh);
    const std::string txt = std::to_string(page + 1) + "/" + std::to_string(pages);
    float w = 0;
    std::vector<std::pair<S::Pic, float>> pics;
    const float dh = 34;   // alto de las cifras (px del area)
    for (char c : txt) {
      const S::Pic p = S::Img(c == '/' ? "retos/num_barra" : (std::string("retos/num_") + c).c_str());
      if (!p) {
        pics.clear();
        break;
      }
      const float pw = p.w * dh / p.h;
      pics.emplace_back(p, pw);
      w += pw - 4;
    }
    const float x0 = (kBoxX0 + kBoxX1) * 0.5f - w * 0.5f;
    if (!pics.empty()) {
      float x = x0;
      for (const auto& [p, pw] : pics) {
        S::DrawPic(dl, p, a.At(x, cy - dh * 0.5f), a.At(x + pw, cy + dh * 0.5f));
        x += pw - 4;
      }
    } else {
      S::MenuText(dl, a.At((kBoxX0 + kBoxX1) * 0.5f, cy), 34 * a.s, txt.c_str(), true, false);
      w = 60;
    }
    S::Arrows(dl, a.At(x0 - 14, cy - 13), a.At(x0 + w + 14, cy + 13), 26 * a.s, t);
  }

  void DrawHelp(ImDrawList* dl, const S::Area& a, int say, float t, float alpha) {
    alpha *= Ease((t - said_at_ - 0.12f) / 0.25f);
    std::string title;
    std::string body;
    std::string marks;   // record y reto guardado, con el icono del guardado delante
    if (say >= kSayRow) {
      body = Tr(kRowHelp);
    } else {
      body = EntryHelp(say);
      if (!EntryReady(say)) {
        if (const char* why = EntryWhy(say)) {   // gris por algo del jugador (RegisterGate): su explicacion
          body = why;
        } else {
          body += " ";
          body += Tr(kSoon);
        }
      } else if (std::strcmp(EntryKey(say), "pasarela") == 0) {   // el avance de cada campana (progreso.toml)
        namespace Pa = dbz3::pasarela;
        static double leido = -10;
        static std::string avance;
        if (t - leido > 2.0) {
          leido = t;
          char buf[160];
          std::snprintf(buf, sizeof(buf), "IW %d/%d - B2 %d/9 - %s %d/11", Pa::LeerProgreso("iw_hecho", 0), 96,
                        Pa::LeerProgreso("tablero_b2_fase", 0), dbz3::i18n::T("Satán", "Satan"),
                        Pa::LeerProgreso("hercule", 0));
          avance = buf;
        }
        marks = avance;
      } else if (say == kQuick) {   // el ultimo combate rapido, con el nombre del escenario
        if (int p1, p2, st; LastQuick(&p1, &p2, &st)) {
          body += std::string("\n") + Tr(kLastQuick) + dbz3::modos::Nombre(p1) + Tr(kVs) + dbz3::modos::Nombre(p2) +
                  " (" + dbz3::escenarios::Nombre(st) + ")";
        }
      } else if (say == kTower || say == kSurvival || say == kKumite) {
        // abren el selector de rpg.cpp: su lista arriba, la ayuda, el reto clasico puesto (si no es el de serie),
        // y abajo el record/reto guardado del clasico y el monedero con el icono del guardado
        const int k = say - kTower;
        int n = 0;
        while (n < 4 && kLists[k][n] >= 0) ++n;
        for (int i = 0; i < n; ++i) {   // "Torre, Torres del Tiempo y Abyss"
          if (i > 0) title += i == n - 1 ? Tr(kAnd) : ", ";
          title += dbz3::modos::NombreModo(kLists[k][i]);
        }
        body = Tr(kListHelp[k]);
        const Reto& r = RetoOf(say, t);
        if (!r.name.empty()) {
          body += std::string(" ") + Tr(kClassic) + r.name;
          if (r.fights > 0) body += " (" + std::to_string(r.fights) + ")";
          body += ".";
        }
        bool any = false;
        std::string m = Marks(k, &any);
        if (const int zeni = Wallet(t); zeni >= 0) {
          m += std::string(" - ") + Tr(kWallet) + std::to_string(zeni) + " Zenie";
          any = true;
        }
        if (any) marks = std::move(m);
        else body += "\n" + m;
      }
    }
    {   // Escalafon del Dojo (rangos al estilo Tekken, pasarela.h): el rango y sus PE en cada fila; al ascender, el
        // aviso arriba unos segundos (rango_ascenso = nivel + 1, lo pone RangoMejora)
      namespace Pa = dbz3::pasarela;
      static int pe = -1, ascenso = 0;
      static double leido = -10, aviso_hasta = 0;
      if (t - leido > 2.0) {
        leido = t;
        pe = Pa::RangoPE();
        if (const int a = Pa::LeerProgreso("rango_ascenso", 0); a > 0) {
          ascenso = a;
          aviso_hasta = t + 6.0;
          Pa::GuardarProgreso("rango_ascenso", 0);
        }
      }
      const std::string rango = std::string(dbz3::i18n::T("Rango", "Rank")) + ": " + Pa::RangoNombre(Pa::RangoNivel(pe)) +
                                " (" + std::to_string(pe) + " PE)";
      marks = marks.empty() ? rango : marks + " - " + rango;
      if (ascenso > 0 && t < aviso_hasta)
        title = std::string(dbz3::i18n::T("¡Ascenso! ", "Promotion! ")) + Pa::RangoNombre(ascenso - 1) + " - " + title;
      // placa del rango (pack Dragon HUB: hub/rango_01..11, 512x128) encima del bocadillo, a la derecha; al ascender,
      // grande en el centro con un golpe de escala. "Maestro" lleva el texto dibujado: otro idioma = la placa vacia
      // con el nombre encima
      auto placa = [&](int nivel, ImVec2 c, float w, float k_alpha) {
        const bool texto = nivel == 10 && std::string(dbz3::i18n::LangCode()) != "es";
        char nombre[32];
        std::snprintf(nombre, sizeof(nombre), texto ? "hub/rango_%02d_vacia" : "hub/rango_%02d", nivel + 1);
        const S::Pic pl = S::Img(nombre);
        if (!pl) return;
        const float h = pl.h * w / pl.w;
        const int v1 = dl->VtxBuffer.Size;
        S::DrawPic(dl, pl, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f));
        if (texto) {
          const char* tx = Pa::RangoNombre(nivel);
          const float fs = h * 0.42f, tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, tx).x;
          dl->AddText(ImGui::GetFont(), fs, ImVec2(c.x + w * 0.12f - tw * 0.5f, c.y - fs * 0.5f), IM_COL32_WHITE, tx);
        }
        FadeVerts(dl, v1, alpha * k_alpha);
      };
      placa(Pa::RangoNivel(pe), a.At(662, 646), 256 * a.s, 1.0f);
      if (ascenso > 0 && t < aviso_hasta) {
        const float u = Clamp01(float(t - (aviso_hasta - 6.0)) / 0.35f), fuera = Clamp01(float(aviso_hasta - t) / 0.5f);
        placa(ascenso - 1, a.At(600, 330), 640 * a.s * (1.0f + 0.35f * (1.0f - Ease(u))), Ease(u) * fuera);
      }
    }
    if (has_bg_) {   // con el fondo del pack, el bocadillo del juego queda debajo: el nuestro (vacio) y el texto encima
      const int v0 = dl->VtxBuffer.Size;
      S::Speech(dl, a.At(30, kBubbleY0), a.At(790, kBubbleY1), a.s, "", 0.88f);
      FadeVerts(dl, v0, alpha);
      BubbleText(dl, a, title, body.c_str(), alpha, marks, (kBubbleY0 + kBubbleY1) * 0.5f, kBubbleY1 - kBubbleY0 - 28);
      return;
    }
    BubbleText(dl, a, title, body.c_str(), alpha, marks);
  }

  void DrawScreen(ImDrawList* dl, const S::Area& a, const View& v, float t, float dt) {
    const int v0 = dl->VtxBuffer.Size;
    // Fondo nativo del menu principal por defecto (23/10, el usuario lo prefiere al hub/fondo de serie); solo un
    // pack del jugador con hub/fondo_menu lo tapa (y entonces nuestro marco y las tiras doradas)
    if (const S::Pic bg = S::Img("hub/fondo_menu")) {
      has_bg_ = true;
      const S::Imagen g = S::GuestImage(ImGui::GetIO());   // S1-F06: en la imagen del juego, no en la ventana
      const float sw = g.Size().x, sh = g.Size().y;
      const float k = std::max(sw / bg.w, sh / bg.h);
      const ImVec2 c(g.p0.x + sw * 0.5f, g.p0.y + sh * 0.5f), half(bg.w * k * 0.5f, bg.h * k * 0.5f);
      dl->PushClipRect(g.p0, g.p1, true);
      S::DrawPic(dl, bg, ImVec2(c.x - half.x, c.y - half.y), ImVec2(c.x + half.x, c.y + half.y));
      dl->PopClipRect();
      S::Sides(dl, a);
      S::Frame(dl, a.At(kInX0 - S::mode::kLine - 2, kFrameY0), a.At(kInX1 + S::mode::kLine + 2, kFrameY1), a.s);
    }
    DrawTitle(dl, a, t);
    // pagina del cursor y sus filas
    int vis[kMaxEntries];
    const int n = VisibleEntries(vis);
    int pos = 0;
    while (pos < n && vis[pos] != v.cursor) ++pos;
    pos = std::min(pos, std::max(0, n - 1));
    const int page = pos / kPerPage;
    if (page != page_) {
      if (page_ >= 0 && t - opened_at_ > 0.05f) {
        page_dir_ = page > page_ ? 1.0f : -1.0f;
        if ((page_ == 0 && page > 1) || (page == 0 && page_ > 1)) page_dir_ = -page_dir_;   // vuelta
        page_at_ = t;
      }
      page_ = page;
    }
    const float since = std::max(opened_at_, page_at_);
    const float dir = page_at_ > opened_at_ ? page_dir_ : -1.0f;
    // franja de agua del juego bajo la entrada elegida (se desliza al moverse)
    const float target = kRow0 + kPitch * float(pos % kPerPage);
    if (stripe_y_ < 0 || t - since < 0.01f) stripe_y_ = target;
    stripe_y_ += (target - stripe_y_) * std::min(1.0f, dt * 22.0f);
    if (v.open) S::Highlight(dl, a.At(kInX0, stripe_y_ - kStripe * 0.5f), a.At(kInX1, stripe_y_ + kStripe * 0.5f), t);
    for (int i = page * kPerPage; i < std::min(n, page * kPerPage + kPerPage); ++i) {
      const int e = vis[i], slot = i % kPerPage;
      const float y = kRow0 + kPitch * float(slot);
      const float in = Ease((t - since - 0.05f * float(slot)) / 0.25f);   // entran una tras otra
      const float dx = 40.0f * dir * (1.0f - in);
      const int e0 = dl->VtxBuffer.Size;
      const bool sel = e == v.cursor, ready = EntryReady(e);
      const ImU32 tint = !ready ? kOff : sel ? IM_COL32_WHITE : kDim;
      S::Pic ic = S::Img((std::string("modo_") + EntryKey(e)).c_str());
      if (!ic) ic = S::Img("modo_extra");   // entrada nueva sin insignia propia
      if (ic) {
        const float s = (sel ? 52.0f + 3.0f * std::sin(t * 5.0f) : 46.0f) * a.s;
        const ImVec2 c = a.At(kIconX + 23 + dx, y);
        S::DrawPic(dl, ic, ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f),
                   ImVec4(0, 0, 0, 0), ready ? (sel ? IM_COL32_WHITE : kDim) : kOff);
      }
      // estado de cada modo junto a su nombre: BETA (jugable, aun se pule) o EXPERIMENTAL (en construccion: las
      // campanas portadas de IW/B2 y los minijuegos de IW). Los demas, sin marca.
      const std::string key = EntryKey(e);
      const int estado = key == "pasarela" || key == "minijuegos" ? 2
                         : key == "tagteam" || key == "online" || key == "satan" || key == "mision100" ||
                                   key == "laboratorio" || key == "escenarios" || key == "heroe"
                             ? 1
                             : 0;
      const float right = Label(dl, a, EntryKey(e), EntryName(e), kLabelX + dx, y, kLabelK, tint, sel,
                                kLabelMax - (estado == 1 ? 70.0f : estado == 2 ? 150.0f : 0.0f));
      if (estado == 1) {
        if (const S::Pic b = S::Img("beta")) {
          const float h = 26 * a.s, w = b.w * h / b.h;
          const ImVec2 p0 = a.At(right + 6, y - 22);
          S::DrawPic(dl, b, p0, ImVec2(p0.x + w, p0.y + h), ImVec4(0, 0, 0, 0), sel ? IM_COL32_WHITE : kDim);
        }
      } else if (estado == 2) {   // pegatina de texto (sin pieza propia): ambar, como un aviso de obras
        static const char* const kExp[5] = {"EXPERIMENTAL", "EXPERIMENTAL", "EXPÉRIMENTAL", "EXPERIMENTELL",
                                            "SPERIMENTALE"};
        const char* tx = T::Otro("EXPERIMENTAL") ? T::Otro("EXPERIMENTAL") : kExp[std::clamp(T::LangCode()[0] == 'e' && T::LangCode()[1] == 's' ? 0
                                         : T::LangCode()[0] == 'f' ? 2 : T::LangCode()[0] == 'd' ? 3
                                         : T::LangCode()[0] == 'i' ? 4 : 1, 0, 4)];
        const float fs = 17 * a.s, tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, tx).x;
        const ImVec2 p0 = a.At(right + 6, y - 21), p1(p0.x + tw + 14 * a.s, p0.y + 24 * a.s);
        const int alpha_i = sel ? 255 : 190;
        dl->AddRectFilled(p0, p1, IM_COL32(232, 150, 20, alpha_i), 6 * a.s);
        dl->AddRect(p0, p1, IM_COL32(90, 40, 0, alpha_i), 6 * a.s, 0, 1.5f * a.s);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(p0.x + 7 * a.s, p0.y + (p1.y - p0.y - fs) * 0.5f),
                    IM_COL32(40, 20, 0, alpha_i), tx);
      }
      FadeVerts(dl, e0, in);
    }
    FadeVerts(dl, v0, screen_a_);
  }

  // Titulo sobre la cinta nativa: el logo "Dragon HUB" (hub/logo, 1024 o 512 de ancho segun el tamano en
  // pantalla) o, sin el, "HUB" con la letra de los titulos. Entra como un sello cuando la cinta ya llego.
  void DrawTitle(ImDrawList* dl, const S::Area& a, float t) {
    const float u = Clamp01((t - opened_at_ - 0.22f) / 0.30f);
    if (u <= 0.0f) return;
    const float pop = 1.0f + 0.28f * (1.0f - Ease(u)) - 0.04f * std::sin(Clamp01(u * 1.4f) * 3.14159f);
    const float w = kLogoW * a.s;
    // X1 (studies 5): el titulo se compone en marcha, letras doradas con perfil (las de los Banner del resto de pantallas)
    // sobre el papel de la cinta del juego. El logo "Dragon HUB" (hub/logo) solo sale si un pack del jugador lo trae.
    S::Pic p = S::Img(w > 600 ? "hub/logo" : "hub/logo_512");
    if (!p) p = S::Img("hub/logo");
    const int v0 = dl->VtxBuffer.Size;
    if (p) {
      const float h = p.h * w / p.w;
      const ImVec2 c = a.At(kLogoX, kLogoY);
      S::DrawPic(dl, p, ImVec2(c.x - w * 0.5f * pop, c.y - h * 0.5f * pop), ImVec2(c.x + w * 0.5f * pop, c.y + h * 0.5f * pop));
    } else {
      S::TitleText(dl, a, kLogoX, kLogoY, kLogoW * 0.9f, "Dragon HUB", pop);
    }
    FadeVerts(dl, v0, Ease(u * 1.6f));
  }

  // Pack opcional: el personaje vuela hacia la camara durante el fundido de entrada (crece cada vez mas
  // deprisa, la cara siempre cerca del centro, con dos estelas) y pasa de largo al volver la luz.
  void DrawFlyer(ImDrawList* dl, const ImGuiIO& io, const View& v) {
    if (fly_ <= 0) return;
    const S::Pic p = S::Img(("hub/vuelo_" + std::to_string(fly_)).c_str());
    if (!p) return;
    const float total = float(kFadeOutMs + kFadeHoldMs) + 320.0f;
    const float ms = float(ClockMs() - v.fade_t0) + (v.fade == Fade::kIn ? float(kFadeOutMs + kFadeHoldMs) : 0.0f);
    const float u = ms / total;
    if (u >= 1.0f) return;
    const S::Imagen g = S::GuestImage(io);   // S1-F06: centrado en la imagen del juego
    const float sw = g.Size().x, sh = g.Size().y;
    const float alpha = Clamp01(u / 0.12f) * (1.0f - Clamp01((u - 0.78f) / 0.22f));
    const float h0 = sh * 0.14f * std::exp(3.4f * std::pow(u, 1.5f));
    const float trail[3][2] = {{0.70f, 0.10f}, {0.84f, 0.22f}, {1.0f, 1.0f}};   // escala, alfa
    for (const auto& tr : trail) {
      const float h = h0 * tr[0], w = p.w * h / p.h;
      const ImVec2 c(g.p0.x + sw * 0.5f, g.p0.y + sh * 0.48f + h * 0.22f);
      S::DrawPic(dl, p, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), ImVec4(0, 0, 0, 0),
                 IM_COL32(255, 255, 255, int(255 * alpha * tr[1])));
    }
  }

  // Prueba de QueueSe (DBZ3_TEST_HUB_SE=1): con el menu principal 10 s seguidos a la vista, 12 s de sonidos desde este
  // hilo (mover y aceptar cada 0,3 s), para compararlos con la linea "audio pico/rms" del log (cada 5 s).
  void TestSe(const View& v, float t) {
    static const bool on = [] {   // DBZ3_TEST_HUB_SE=log: solo el log de lo que suena, sin rafaga
      const char* e = std::getenv("DBZ3_TEST_HUB_SE");
      return e && std::strcmp(e, "log") != 0;
    }();
    if (!on) return;
    if (!v.menu) {   // otra pantalla: se cuenta de nuevo al volver al menu
      se_menu_at_ = -1;
      return;
    }
    if (se_menu_at_ < 0) se_menu_at_ = t;
    const float el = t - se_menu_at_;
    if (el < 10.0f || el > 22.0f || t - se_last_ < 0.3f) return;
    if (se_last_ < 0) REXLOG_INFO("dbz3 hub: prueba: 12 s de sonidos de la cola (QueueSe) desde ahora");
    se_last_ = t;
    QueueSe(se_n_++ % 2 ? kSeOkId : kSeMoveId);
  }

  // Vigila user_data/dbz3/hub_*.toml y rpg.toml (cada 0,5 s): si alguno cambia con el HUB en uso (InSession),
  // aviso de guardado. La primera vuelta solo apunta las fechas (lo que habia al abrir el juego no es un guardado).
  void WatchSaves(float t) {
    if (t - watch_at_ < 0.5f) return;
    watch_at_ = t;
    std::error_code ec;
    int64_t newest = 0;
    for (const auto& f : std::filesystem::directory_iterator(dbz3::settings::UserDataRoot(), ec)) {
      const std::string n = f.path().filename().string();
      const bool hub = n.size() > 9 && n.compare(0, 4, "hub_") == 0 && n.compare(n.size() - 5, 5, ".toml") == 0;
      if (!hub && n != "rpg.toml") continue;   // rpg.toml: monedero y partidas de los retos de rol (rpg.cpp)
      const auto mt = f.last_write_time(ec);
      if (!ec) newest = std::max<int64_t>(newest, int64_t(mt.time_since_epoch().count()));
    }
    if (watched_ && newest > newest_ && InSession()) ShowSaving();   // fuera del HUB: el resumen de un Duelo
    newest_ = std::max(newest_, newest);
    watched_ = true;
  }

  // Aviso de guardado como el del juego (ver kSaveX0): oscurece el area 4:3, caja verde con el texto y nuestro
  // icono (hub/guardado) a la izquierda. Solo se dibuja: el mando sigue para el juego.
  void DrawSaving(ImDrawList* dl, const ImGuiIO& io) {
    const int64_t ms = ClockMs() - g_saving_at.load();
    const int64_t shown = kSavingMs + kSavedMs;
    if (ms < 0 || ms > shown + kSaveAnimMs) return;
    const float k = std::min(Clamp01(float(ms) / kSaveAnimMs), Clamp01(float(shown + kSaveAnimMs - ms) / kSaveAnimMs));
    const S::Area a = S::GameArea(io);
    dl->AddRectFilled(a.p0, a.p1, IM_COL32(0, 0, 0, int(255 * kSaveDim * k)));
    const float sc = 0.85f + 0.15f * Ease(k);
    const ImVec2 c = a.At((kSaveX0 + kSaveX1) * 0.5f, (kSaveY0 + kSaveY1) * 0.5f);
    const ImVec2 half((kSaveX1 - kSaveX0) * 0.5f * a.s * sc, (kSaveY1 - kSaveY0) * 0.5f * a.s * sc);
    const ImVec2 p0(c.x - half.x, c.y - half.y), p1(c.x + half.x, c.y + half.y);
    dl->AddRectFilled(p0, p1, IM_COL32(8, 187, 58, int(255 * kSaveFill * k)));
    dl->AddRect(p0, p1, IM_COL32(30, 170, 80, int(255 * k)), 0, 0, std::max(1.0f, a.s));
    if (k < 0.5f) return;   // el texto sale con la caja ya casi entera (como en el juego)
    const int v0 = dl->VtxBuffer.Size;
    const float ih = kSaveIconH * a.s * sc;
    if (const S::Pic icon = SaveIcon(ih)) {
      const float pulse = ms < kSavingMs ? 1.0f + 0.04f * std::sin(float(ms) * 0.008f) : 1.0f;   // late mientras guarda
      const float h = ih * pulse, w = icon.w * h / icon.h;
      const ImVec2 ic(p0.x + (40 * a.s + ih * 0.5f) * sc, c.y);
      S::DrawPic(dl, icon, ImVec2(ic.x - w * 0.5f, ic.y - h * 0.5f), ImVec2(ic.x + w * 0.5f, ic.y + h * 0.5f));
    }
    ImFont* font = dbz3::launcher::ui::GetFonts().speech ? dbz3::launcher::ui::GetFonts().speech : ImGui::GetFont();
    const float size = kSaveText * a.s * sc, step = kSaveStep * a.s * sc;
    const char* text = Tr(ms < kSavingMs ? kSaving : kSaved);
    int lines = 1;
    for (const char* q = text; *q; ++q) lines += *q == '\n';
    float y = c.y - step * float(lines) * 0.5f;
    for (const char* s0 = text; *s0;) {
      const char* e = std::strchr(s0, '\n');
      if (!e) e = s0 + std::strlen(s0);
      const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, s0, e);
      const ImVec2 at(c.x - ts.x * 0.5f, y + (step - ts.y) * 0.5f);
      const float o = std::max(1.0f, 1.5f * a.s);   // perfil negro fino, como la letra del juego
      for (const ImVec2 d : {ImVec2(-o, 0), ImVec2(o, 0), ImVec2(0, -o), ImVec2(0, o), ImVec2(-o, -o), ImVec2(o, o),
                             ImVec2(-o, o), ImVec2(o, -o)}) {
        dl->AddText(font, size, ImVec2(at.x + d.x, at.y + d.y), IM_COL32(0, 0, 0, 255), s0, e);
      }
      dl->AddText(font, size, at, IM_COL32(244, 244, 240, 255), s0, e);
      y += step;
      s0 = *e ? e + 1 : e;
    }
    FadeVerts(dl, v0, (k - 0.5f) * 2.0f);
  }

  // El siguiente hub/vuelo_<n> que tenga el pack (se turnan); 0 sin ninguno.
  int NextFlyer() {
    for (int k = 1; k <= kMaxFlyers; ++k) {
      const int n = (fly_last_ % kMaxFlyers) + 1;
      fly_last_ = n;
      if (S::Img(("hub/vuelo_" + std::to_string(n)).c_str())) return n;
    }
    return 0;
  }

  float row_a_ = 0, screen_a_ = 0, stripe_y_ = -1, opened_at_ = 0, said_at_ = 0, page_at_ = -10, page_dir_ = 1;
  float watch_at_ = -1;
  int64_t newest_ = 0, front_at_ = -100000;   // front_at_: aviso que ya paso al final de la lista
  bool watched_ = false, save_wait_ = false;
  float se_menu_at_ = -1, se_last_ = -1;   // TestSe
  int se_n_ = 0;
  static constexpr int kSeMoveId = 2, kSeOkId = 3;   // save_wait_: aviso guardado para la vuelta al menu
  bool was_open_ = false, has_bg_ = false;
  int said_ = -2, page_ = -1, fly_ = 0, fly_last_ = 0;
  int64_t fly_t0_ = 0;
};

}  // namespace

void ShowSaving() {   // otro guardado con el aviso en pantalla: vuelve a "Guardando" sin volver a entrar
  const int64_t now = ClockMs(), at = g_saving_at.load();
  const bool shown = now - at >= 0 && now - at <= kSavingMs + kSavedMs + kSaveAnimMs;
  g_saving_at = shown ? now - std::min<int64_t>(now - at, kSaveAnimMs) : now;
  REXLOG_INFO("dbz3 hub: aviso de guardado{}", shown ? " (sigue)" : "");
}

void CreatePanel() {
  static HubPanel* panel = nullptr;   // vive lo que el juego (el dibujante no borra sus dialogos)
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new HubPanel(rt->imgui_drawer());
}

}  // namespace dbz3::hub

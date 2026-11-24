// dbz3 - In-game quick settings (see quick_settings.h).

#include "quick_settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#if !defined(DBZ3_EU_VARIANT)
#include "../freecam.h"
#endif
#include "../launcher/i18n.h"
#include "../launcher/settings.h"
#include "../mods_opcionales.h"

namespace dbz3::ingame {
namespace {

using Item = rex::ui::QuickMenuItem;
using Choices = std::vector<std::pair<std::string, std::string>>;

// A video option changed from the menu: the quality preset becomes "manual",
// or "auto" would put the recommended values back on the next start.
std::atomic<bool> g_video_touched{false};
std::atomic<int> g_ayudas_gen{0};

const char* const kVideoCvars[] = {"dbz3_resolution_scale", "dbz3_present_effect", "dbz3_fsr_render",
                                   "dbz3_fsr_sharpness",    "dbz3_cas_sharpness",  "dbz3_fxaa",
                                   "dbz3_anisotropic"};

// The panel (SDK quick_menu.cpp) reads two conventions out of plain strings:
// "Name|TAG" draws TAG as a pill after the name, and "TITLE\nnote" gives a section a one-line note.
const char* const kBeta = "BETA";
const char* const kExperimental = "EXPERIMENTAL";
std::string Tagged(const char* label, const char* tag) { return std::string(label) + "|" + tag; }
std::string Titled(const char* title, const char* note) { return std::string(title) + "\n" + note; }

Item Toggle(std::string label, const char* cvar, const char* help) {
  Item item;
  item.kind = Item::Kind::kToggle;
  item.label = std::move(label);
  item.cvar = cvar;
  item.help = help;
  return item;
}

Item Choice(std::string label, const char* cvar, Choices choices, const char* help) {
  Item item;
  item.kind = Item::Kind::kChoice;
  item.label = std::move(label);
  item.cvar = cvar;
  item.choices = std::move(choices);
  item.help = help;
  return item;
}

Item Number(std::string label, const char* cvar, double min, double max, double step, const char* help) {
  Item item;
  item.kind = Item::Kind::kNumber;
  item.label = std::move(label);
  item.cvar = cvar;
  item.min = min;
  item.max = max;
  item.step = step;
  item.help = help;
  return item;
}

// After any change: forward the dbz3_* values onto the SDK cvars (they apply
// live) and save them where the launcher keeps them.
// Language the menu texts were built in (the launcher can change it after startup).
int32_t g_menu_language = -1;

void ApplyAndSave() {
  if (g_video_touched.exchange(false) && dbz3::settings::QualityPreset() != "manual") {
    dbz3::settings::SetQualityPreset("manual");
  }
  rex::cvar::SetFlagByName("fullscreen", dbz3::settings::FullscreenMode() != "windowed" ? "true" : "false");
  dbz3::settings::ApplyRuntimeSettingsToSdk(true);
  dbz3::settings::SaveUserSettings();
}

}  // namespace

// Tutoriales de los modos (src/ingame/tutorial.cpp): se muestran la primera vez que se entra en cada modo nuevo.
REXCVAR_DEFINE_BOOL(dbz3_tutoriales, true, "DBZ3/Video",
                    "Show the optional tutorial the first time you enter each new mode")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

void ReiniciarAyudas() {
  namespace fs = std::filesystem;
  const fs::path p = dbz3::settings::UserDataRoot() / "ayudas.toml";
  std::error_code ec;
  if (fs::exists(p, ec)) {
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
    const fs::path dst = p.parent_path() / "respaldo" / ("ayudas_" + std::to_string(stamp) + ".toml");
    fs::create_directories(dst.parent_path(), ec);
    fs::rename(p, dst, ec);
    if (ec) REXLOG_WARN("dbz3: ayudas.toml no se pudo mover a {}: {}", dst.string(), ec.message());
    else REXLOG_INFO("dbz3: tutoriales sin ver (ayudas.toml movido a {})", dst.string());
  }
  ++g_ayudas_gen;
}

int AyudasGeneracion() { return g_ayudas_gen.load(); }

void ConfigureQuickMenu(rex::ui::QuickMenuConfig& menu, std::function<void()> open_full_settings) {
  namespace T = dbz3::i18n;
#if !defined(DBZ3_EU_VARIANT)
  dbz3::freecam::IniciarSesion();   // DS162: opciones de sesion (nunca se guardan) + panel de la camara libre
#endif
  g_menu_language = dbz3::settings::Language();
  T::SetLanguage(g_menu_language);

  // Once: the menu is rebuilt when the language changes (RefreshQuickMenuLanguage).
  static bool callbacks_registered = false;
  if (!callbacks_registered) {
    callbacks_registered = true;
    for (const char* name : kVideoCvars) {
      rex::cvar::RegisterChangeCallback(name, [](std::string_view, std::string_view) {
        if (rex::ui::QuickMenuDialog::IsOpen()) g_video_touched.store(true);
      });
    }
    // F3 and the menu's "Show FPS" are the same switch.
    rex::cvar::RegisterChangeCallback("debug_overlay", [](std::string_view, std::string_view value) {
      const bool on = value == "true" || value == "1";
      const std::string want = on ? "true" : "false";
      if (rex::cvar::GetFlagByName("dbz3_show_fps") != want) rex::cvar::SetFlagByName("dbz3_show_fps", want);
    });
  }

  menu.title = T::T("AJUSTES RAPIDOS", "QUICK SETTINGS");
  menu.subtitle = "DRAGON BALL Z  BUDOKAI 3  HD";
  // "A" label, then (after the newline) the label of the LB / RB hint.
  menu.text_change = std::string(T::T("Cambiar", "Change")) + "\n" + T::T("Sección", "Section");
  menu.text_close = T::T("Cerrar", "Close");
  // Y (mando) enciende/apaga el pack HD y cierra el menu: lo mismo que Burst Limit hace con
  // su camara libre. El cambio es en vivo (dbz3_texture_packs_on es kHotReload).
  menu.quick_toggle_cvar = "dbz3_texture_packs_on";
  menu.quick_toggle_label = T::T("Pack HD", "HD pack");
  menu.text_saved = T::T("Se guarda solo", "Saved automatically");
  menu.text_restart = T::T("REINICIO", "RESTART");
  menu.text_on = T::T("SI", "ON");
  menu.text_off = T::T("NO", "OFF");
  menu.on_changed = ApplyAndSave;

  // Render-resolution choice: every upscaler; sharpening: not DLSS (it has none).
  const std::vector<std::string> fsr = {"fsr", "fsr3", "dlss"};
  const std::vector<std::string> fsr_sharp = {"fsr", "fsr3"};

  // Each section is finished before the next one starts (emplace_back moves the earlier ones).

  // 1. IMAGEN: lo que se ve y lo que pide a la grafica.
  {
    auto& image = menu.sections.emplace_back();
    image.title = Titled(T::T("IMAGEN", "PICTURE"),
                         T::T("Calidad de la imagen. Si el juego va lento, baja la resolución.",
                              "Picture quality. If the game runs slowly, lower the resolution."));
    image.items.push_back(Choice(
        T::T("Resolucion interna", "Internal resolution"), "dbz3_resolution_scale",
        {{"1", "1x  (720p)"}, {"2", "2x  (1440p)"}, {"3", "3x  (4K)"}, {"4", "4x  (5K)"}},
        T::T("A cuanta resolucion dibuja el juego. Mas alto es mas nitido pero pide mas grafica.",
             "The resolution the game draws at. Higher is sharper but needs a faster GPU.")));
    image.items.push_back(Choice(
        T::T("Escalado", "Upscaler"), "dbz3_present_effect",
        {{"bilinear", T::T("Basico", "Basic")},
         {"cas", "AMD CAS"},
         {"fsr", "AMD FSR"},
         {"fsr3", "AMD FSR 3 (beta)"},
         {"dlss", "NVIDIA DLSS (beta)"}},
        T::T("Cómo se adapta la imagen a tu pantalla. FSR es el más nítido y CAS solo afila. FSR 3 y DLSS (beta) "
             "suavizan más los bordes; DLSS solo en gráficas NVIDIA RTX.",
             "How the picture fits your screen. FSR is the sharpest and CAS only sharpens. FSR 3 and DLSS (beta) "
             "smooth edges more; DLSS needs an NVIDIA RTX card.")));
    {
      Item& more = image.items.emplace_back(Choice(
          T::T("Mas FPS", "More FPS"), "dbz3_fsr_render",
          {{"native", T::T("Nativa", "Native")},
           {"quality", T::T("Calidad", "Quality")},
           {"balanced", T::T("Equilibrado", "Balanced")},
           {"performance", T::T("Rendimiento", "Performance")},
           {"ultra_performance", T::T("Ultra rendimiento", "Ultra performance")}},
          T::T("Dibuja con menos píxeles y el escalado los recupera: más FPS con la imagen algo más suave. "
               "Con resolución 1x no cambia nada.",
               "Draws with fewer pixels and the upscaler brings them back: more FPS, a slightly softer picture. "
               "At 1x resolution nothing changes.")));
      more.shown_if_cvar = "dbz3_present_effect";
      more.shown_if_values = fsr;
    }
    {
      // FSR takes a sharpness reduction in stops: 0 = sharpest.
      Item& sharp = image.items.emplace_back(Number(
          T::T("Nitidez", "Sharpness"), "dbz3_fsr_sharpness", 0.0, 2.0, 0.2,
          T::T("Afilado despues de FSR.", "Sharpening after FSR.")));
      sharp.display_scale = -50.0;
      sharp.display_offset = 100.0;
      sharp.format = "%.0f%%";
      sharp.shown_if_cvar = "dbz3_present_effect";
      sharp.shown_if_values = fsr_sharp;
    }
    {
      Item& sharp = image.items.emplace_back(Number(
          T::T("Nitidez", "Sharpness"), "dbz3_cas_sharpness", 0.0, 1.0, 0.1,
          T::T("Afilado extra sobre AMD CAS.", "Extra sharpening on top of AMD CAS.")));
      sharp.display_scale = 100.0;
      sharp.format = "%.0f%%";
      sharp.shown_if_cvar = "dbz3_present_effect";
      sharp.shown_if_values = {"cas"};
    }
    image.items.push_back(Choice(
        T::T("Suavizado de bordes", "Edge smoothing"), "dbz3_fxaa",
        {{"none", T::T("No", "Off")}, {"fxaa", "FXAA"}, {"fxaa_extreme", T::T("FXAA fuerte", "FXAA (strong)")}},
        T::T("Suaviza los dientes de sierra. Muy barato; suaviza un poco la imagen.",
             "Smooths jagged edges. Very cheap; softens the picture a little.")));
    image.items.push_back(Choice(
        T::T("Filtrado de texturas", "Texture filtering"), "dbz3_anisotropic",
        {{"0", T::T("No", "Off")}, {"2", "2x"}, {"3", "4x"}, {"4", "8x"}, {"5", "16x"}},
        T::T("Mantiene nitido el suelo y lo que se ve de lado.",
             "Keeps the floor and anything seen at an angle sharp.")));
  }

  // 2. ASPECTO HD: lo que anade el remaster (todo se apaga al momento).
  {
    auto& look = menu.sections.emplace_back();
    look.title = Titled(T::T("ASPECTO HD", "HD LOOK"),
                        T::T("Texturas y brillo del remaster. Todo se puede apagar al momento.",
                             "Textures and shine of the remaster. All of it can be switched off right away."));
    look.items.push_back(Toggle(
        T::T("Pack HD de texturas", "HD texture pack"), "dbz3_texture_packs_on",
        T::T("Usa los packs de texturas HD instalados en mods. Apagado vuelve a las texturas "
             "originales al momento, sin reiniciar. Atajos: F2, o Y en el mando con este menu abierto.",
             "Uses the HD texture packs installed in mods. OFF switches back to the original "
             "textures right away, no restart. Shortcuts: F2, or Y on the pad while this menu is open.")));
    {
      Item& rim = look.items.emplace_back(Number(
          T::T("Brillo HD", "HD shine"), "dbz3_hd_rim_light", 0.0, 1.0, 0.25,
          T::T("Halo brillante en el borde de los personajes que anadio el remaster HD. 0% lo "
               "quita (aspecto PS2); 100% es el original.",
               "Bright halo on the characters' edges added by the HD remaster. 0% removes it "
               "(PS2 look); 100% is the original.")));
      rim.display_scale = 100.0;
      rim.format = "%.0f%%";
    }
    look.items.push_back(Choice(
        Tagged(T::T("Sombras de contacto", "Contact shadows"), kExperimental), "dbz3_relight",
        {{"0", T::T("No", "Off")}, {"1", T::T("Baja", "Low")}, {"2", T::T("Alta", "High")}},
        T::T("Experimental: oscurece un poco los rincones de la escena 3D (no la interfaz). "
             "Cuesta rendimiento.",
             "Experimental: slightly darkens the corners of the 3D scene (not the HUD). Costs "
             "performance.")));
    look.items.push_back(Toggle(
        T::T("Bordes dorados", "Golden borders"), "dbz3_ui_side_borders",
        T::T("Tiras doradas a los lados de los menus. Apagado: los fondos llenan toda la pantalla.",
             "Golden strips on the sides of the menus. OFF: backgrounds fill the whole screen.")));
  }

  // 3. PANTALLA: ventana, contador de FPS, segundo plano.
  {
    auto& screen = menu.sections.emplace_back();
    screen.title = Titled(T::T("PANTALLA", "DISPLAY"),
                          T::T("Ventana, contador de FPS y qué pasa en segundo plano.",
                               "Window, FPS counter and what happens in the background."));
    screen.items.push_back(Choice(
        T::T("Modo de pantalla", "Screen mode"), "dbz3_fullscreen_mode",
        {{"windowed", T::T("Ventana", "Window")}, {"borderless", T::T("Pantalla completa", "Fullscreen")}},
        T::T("Ventana o pantalla completa sin bordes.", "Window or borderless fullscreen.")));
    screen.items.push_back(Toggle(
        T::T("Mostrar FPS", "Show FPS"), "dbz3_show_fps",
        T::T("FPS, tiempo de cada fotograma con grafica, resolucion y escalado en una esquina (F3).",
             "FPS, frame time with a graph, resolution and upscaler in a corner (F3).")));
    {
      Item& corner = screen.items.emplace_back(Choice(
          T::T("Esquina de los FPS", "FPS corner"), "debug_overlay_position",
          {{"top-left", T::T("Arriba izquierda", "Top left")},
           {"top-right", T::T("Arriba derecha", "Top right")},
           {"bottom-left", T::T("Abajo izquierda", "Bottom left")},
           {"bottom-right", T::T("Abajo derecha", "Bottom right")}},
          T::T("Donde se ve el panel de FPS.", "Where the FPS panel sits.")));
      corner.shown_if_cvar = "dbz3_show_fps";
      corner.shown_if_values = {"true"};
    }
    screen.items.push_back(Toggle(
        T::T("Oscurecer en segundo plano", "Dim in the background"), "dbz3_dim_unfocused",
        T::T("Oscurece el juego cuando cambias a otra ventana.",
             "Darkens the game while you are in another window.")));
  }

  // 4. SONIDO Y MANDO.
  {
    auto& sound = menu.sections.emplace_back();
    sound.title = Titled(T::T("SONIDO Y MANDO", "SOUND & PAD"),
                         T::T("Volumen, vibración y con qué botones se abre este menú.",
                              "Volume, vibration and which buttons open this menu."));
    {
      Item& volume = sound.items.emplace_back(Number(T::T("Volumen", "Volume"), "dbz3_master_volume", 0.0,
                                                     1.0, 0.05, T::T("Volumen general del juego.",
                                                                     "Overall game volume.")));
      volume.display_scale = 100.0;
      volume.format = "%.0f%%";
    }
    sound.items.push_back(Toggle(T::T("Silenciar", "Mute"), "dbz3_mute",
                                 T::T("Quita todo el sonido.", "Turns all sound off.")));
    {
      // X1: musica de combate. Solo se ofrecen las fuentes que el jugador tiene (su copia y sus ISO de PS2, leidas al
      // abrir el menu; las pistas se cachean 5 s). "Original" deja el combate como esta (B3 solo tiene ambiente).
      Choices pistas = {{"original", T::T("Original (la del juego)", "Original (the game's)")}};
      std::vector<std::string> fuentes;
      for (const auto& p : dbz3::musica::Pistas())
        if (std::find(fuentes.begin(), fuentes.end(), p.fuente) == fuentes.end()) fuentes.push_back(p.fuente);
      if (fuentes.size() > 1) pistas.push_back({"aleatoria", T::T("Aleatoria (todas)", "Random (all)")});
      for (const std::string& f : fuentes) {
        const std::string nombre = f == "b3" ? "Budokai 3 HD" : f == "b2" ? "Budokai 2" : f == "iw" ? "Infinite World"
                                 : f == "b1" ? "Budokai 1" : f == "t3" ? "Tenkaichi 3" : f;
        pistas.push_back({"aleatoria:" + f, std::string(T::T("Aleatoria: ", "Random: ")) + nombre +
                                                (f == "b3" ? "" : T::T(" (tu ISO)", " (your ISO)"))});
      }
      {   // pistas fijas de Tenkaichi 3 (20): se eligen aqui o con L1/R1 en el selector de escenarios del Duelo
        int n = 0;
        for (const auto& p : dbz3::musica::Pistas()) {
          if (p.fuente != "t3") continue;
          pistas.push_back({"fija:t3:" + std::to_string(n), std::string(T::T("Fija: ", "Fixed: ")) + p.nombre});
          ++n;
        }
      }
      sound.items.push_back(Choice(
          Tagged(T::T("Musica de combate", "Battle music"), kBeta), "dbz3_musica_combate", std::move(pistas),
          T::T("Suena una pista durante los combates. Las pistas salen de tu copia y de tus ISO de PS2, leidas al "
               "momento; no se reparte nada. Original deja el combate como esta (solo ambiente).",
               "A track plays during battles. Tracks come from your own copy and your PS2 ISOs, read on the fly; "
               "nothing is distributed. Original leaves battles as they are (ambience only).")));
    }
    sound.items.push_back(Toggle(T::T("Vibracion", "Vibration"), "dbz3_rumble",
                                 T::T("Vibracion del mando.", "Controller vibration.")));
    sound.items.push_back(Choice(
        T::T("Botones de este menu", "Buttons for this menu"), "quick_menu_buttons",
        {{"back+start", "Back + Start"}, {"l3+r3", "L3 + R3"}, {"none", T::T("Solo teclado (F1)", "Keyboard only (F1)")}},
        T::T("Que botones del mando abren este menu. F1 en el teclado siempre funciona.",
             "Which controller buttons open this menu. F1 on the keyboard always works.")));
  }

  // 5. MODOS: las funciones nuevas del port (las que se estan probando llevan BETA) y los tutoriales.
  {
    auto& modes = menu.sections.emplace_back();
    modes.title = Titled(T::T("MODOS", "MODES"),
                         T::T("Funciones nuevas del port. Las marcadas BETA aún se están probando.",
                              "New features of the port. Those marked BETA are still being tested."));
    modes.items.push_back(Toggle(
        Tagged("HUB", kBeta), "dbz3_hub_enabled",
        T::T("Fila HUB en el menu principal: Tag Team, Online, combate rapido y los demas modos juntos. Por ahora "
             "solo con el juego USA (NTSC).",
             "HUB row in the main menu: Tag Team, Online, quick battle and the other modes together. For now only "
             "with the USA (NTSC) game.")));
    modes.items.push_back(Toggle(
        Tagged("Tag Team", kBeta), "dbz3_tag_team_enabled",
        T::T("Equipos de 2 o 3 luchadores con relevos (SELECT). Entrada TAG TEAM bajo el menu principal.",
             "Teams of 2 or 3 fighters with switches (SELECT). TAG TEAM entry under the main menu.")));
    modes.items.push_back(Toggle(
        Tagged("Online", kBeta), "dbz3_online_enabled",
        T::T("Combates por internet o en red local con un codigo de sala. Entrada ONLINE en el menu Duelo.",
             "Internet or local network fights with a room code. ONLINE entry in the Duel menu.")));
    modes.items.push_back(Toggle(
        Tagged(T::T("Selector de transformacion", "Transformation selector"), kBeta), "dbz3_form_select_enabled",
        T::T("LT/RT en la seleccion de personaje: empiezas transformado (con su capsula). En online no se usa.",
             "LT/RT at character select: start transformed (needs its capsule). Not used online.")));
    modes.items.push_back(Toggle(
        T::T("Mostrar tutoriales", "Show tutorials"), "dbz3_tutoriales",
        T::T("La primera vez que entras en cada modo nuevo, unos pasos que explican cómo se juega.",
             "The first time you enter each new mode, a few steps that explain how to play.")));
    {
      Item again;
      again.kind = Item::Kind::kAction;
      again.label = T::T("Volver a ver tutoriales", "See tutorials again");
      again.help = T::T("Los tutoriales que ya viste vuelven a salir al entrar en cada modo.",
                        "The tutorials you already saw show up again when you enter each mode.");
      again.action = [] { ReiniciarAyudas(); };
      again.shown_if_cvar = "dbz3_tutoriales";
      again.shown_if_values = {"true"};
      modes.items.push_back(std::move(again));
    }
    // TACTICS-01 (D2 TAC05): texturas/colores de tecnica de Dragon Tactics (fx_nativo.inc). Encendido = entrega actual.
    modes.items.push_back(Toggle(
        T::T("Tacticas: efectos nativos", "Tactics: native effects"), "dbz3_tactics_fx_nativo",
        T::T("Usa la textura y el color de la tecnica del juego en Dragon Tactics. Apagado deja el dibujo anterior.",
             "Uses the game's skill texture and colour in Dragon Tactics. Off uses the previous drawing.")));
    // TACTICS-03: carrusel de Dragon Tactics (apagado de serie; cuenta al empezar una partida nueva).
    modes.items.push_back(Toggle(
        T::T("Tacticas: carrusel", "Tactics: carousel"), "dbz3_tactics_carrusel",
        T::T("Cada 6 rondas, una fila de unidades gratis con cápsula: elige primero quien tiene menos vida. Cuenta al empezar una partida nueva.",
             "Every 6 rounds, a row of free units with a capsule: the trainer with the least health picks first. Applies when you start a new game.")));
    // TACTICS-V04: interfaz nueva de Dragon Tactics (maquetas v2; apagado de serie; solo cambia el dibujo, se puede cambiar en caliente).
    modes.items.push_back(Toggle(
        T::T("Tacticas: interfaz nueva", "Tactics: new interface"), "dbz3_tactics_visual_v2",
        T::T("Tienda compacta, fichas con placas y paneles con titulo, con piezas del propio juego. Apagado se ve como siempre.",
             "Compact shop, cards with plates and titled panels, built from the game's own art. Off looks as before.")));
    // TACTICS-04: bots listos de Dragon Tactics (apagado de serie; cuenta al empezar una partida nueva).
    modes.items.push_back(Toggle(
        T::T("Tacticas: bots listos", "Tactics: smart bots"), "dbz3_tactics_bots_listos",
        T::T("Los bots persiguen sus sinergias y las 3 estrellas y evitan las unidades que acaparan los rivales. Cuenta al empezar una partida nueva.",
             "Bots chase their synergies and 3 stars and avoid the units their rivals hoard. Applies when you start a new game.")));
    // TACTICS-05: variedad de composiciones de Dragon Tactics (apagado de serie; cuenta al empezar una partida nueva).
    modes.items.push_back(Toggle(
        T::T("Tacticas: variedad", "Tactics: variety"), "dbz3_tactics_variedad",
        T::T("Algunas unidades cambian de sinergia y se ajustan los tramos: menos Saiyan y mas equipos distintos que pueden ganar. Cuenta al empezar una partida nueva.",
             "Some units change synergy and the tiers are tuned: fewer Saiyans and more different teams that can win. Applies when you start a new game.")));
    // TACTICS-08: historial por rival guardado y registro de sinergias activas de Dragon Tactics (apagado de serie; cuenta al empezar una partida nueva).
    modes.items.push_back(Toggle(
        T::T("Tacticas: historial por rival", "Tactics: rival history"), "dbz3_tactics_historial",
        T::T("El último duelo contra cada rival se guarda con la partida y sigue al continuarla. Cuenta al empezar una partida nueva.",
             "The last duel against each rival is saved with the game and kept when you continue it. Applies when you start a new game.")));
    // Dragon Budokai Survivors (apagado de serie): su fila en el HUB sale al encenderlo (survivors/entrada.cpp).
    modes.items.push_back(Toggle(
        Tagged(T::T("Dragon Budokai Survivors", "Dragon Budokai Survivors"), kBeta), "dbz3_survivors",
        T::T("Añade al HUB el modo de hordas: aguanta 15 minutos mientras tus técnicas atacan solas. Al apagarlo, la fila se va al reiniciar.",
             "Adds the horde mode to the HUB: survive 15 minutes while your techniques fire on their own. When turned off, the row goes away after a restart.")));
    // Survivors comercial (12/11): ajustes de prueba por paquete, todos apagados de serie; solo tienen efecto con dbz3_survivors encendido.
    {
      struct SurvAj { const char* cvar; const char* es; const char* en; };
      static const SurvAj kSurv[] = {
          {"dbz3_survivors_jugo", "Survivors v2: impacto y luchador legible", "Survivors v2: impact and readable fighter"},
          {"dbz3_survivors_fx_b3", "Survivors v2: efectos de técnica como B3", "Survivors v2: B3-style technique effects"},
          {"dbz3_survivors_formas", "Survivors v2: transformaciones", "Survivors v2: transformations"},
          {"dbz3_survivors_ia", "Survivors v2: enemigos con conducta propia", "Survivors v2: enemies with their own behavior"},
          {"dbz3_survivors_jefes_v2", "Survivors v2: jefes con kit de B3 y eventos", "Survivors v2: B3-kit bosses and events"},
          {"dbz3_survivors_menu_v2", "Survivors v2: portada y menú propio", "Survivors v2: title screen and own menu"},
          {"dbz3_survivors_meta_v2", "Survivors v2: rareza, cofre, logros y colección", "Survivors v2: rarity, chest, achievements and collection"},
          {"dbz3_survivors_audio_v2", "Survivors v2: música y sonidos de B3", "Survivors v2: B3 music and sounds"},
          {"dbz3_survivors_contenido_v2", "Survivors v2: más luchadores, técnicas y escenarios", "Survivors v2: more fighters, techniques and stages"},
      };
      for (const SurvAj& a : kSurv)
        modes.items.push_back(Toggle(Tagged(T::T(a.es, a.en), kBeta), a.cvar,
                                     T::T("Parte de la subida de Dragon Budokai Survivors a juego completo (apagado de serie). Cuenta al abrir el modo.",
                                          "Part of the Dragon Budokai Survivors full-game upgrade (off by default). Applies when you open the mode.")));
    }
    // RANK-ONLINE: ranking mundial de Survivors y del reto diario de Rogue (apagado de serie; ranking_online.cpp).
    modes.items.push_back(Toggle(
        Tagged(T::T("Ranking online", "Online ranking"), kBeta), "dbz3_ranking_online",
        T::T("Un botón en la pantalla final envía la partida al ranking mundial (sin cuenta ni navegador: solo el alias, el luchador y los puntos). Sin red, se guarda y se envía sola después.",
             "A button on the final screen sends the run to the world ranking (no account or browser: only the alias, the fighter and the score). Offline, it is saved and sent later on its own.")));
    // DS162: camara libre de pruebas. Apagada siempre al arrancar y no se guarda (freecam.cpp la devuelve a la
    // fuente "linea de comandos" para que SaveConfig no la escriba).
    modes.items.push_back(Toggle(
        Tagged(T::T("Camara libre", "Free camera"), kBeta), "dbz3_freecam",
        T::T("Herramienta de pruebas: WASD mueve, Q/E baja y sube, el raton (boton derecho) o la palanca derecha mira, "
             "Mayus o los gatillos van mas rapido y la rueda o LB/RB cambian el zoom. R vuelve a la camara del juego. "
             "No se guarda.",
             "Testing tool: WASD moves, Q/E lowers and raises, the mouse (right button) or the right stick looks, "
             "Shift or the triggers go faster and the wheel or LB/RB change the zoom. R returns to the game camera. "
             "It is not saved.")));
    {
      Item& pausa = modes.items.emplace_back(Toggle(
          T::T("Pausar el juego", "Pause the game"), "dbz3_freecam_pausa",
          T::T("Congela el reloj del combate mientras mueves la camara.",
               "Freezes the fight clock while you move the camera.")));
      pausa.shown_if_cvar = "dbz3_freecam";
      pausa.shown_if_values = {"true"};
      Item& hud = modes.items.emplace_back(Toggle(
          T::T("Ocultar HUD", "Hide HUD"), "dbz3_freecam_hud",
          T::T("Oculta el marcador mientras mueves la camara.", "Hides the HUD while you move the camera.")));
      hud.shown_if_cvar = "dbz3_freecam";
      hud.shown_if_values = {"true"};
    }
    // Sin HUD: oculta barras, retratos y marcadores del combate, con o sin camara libre. No se guarda.
    modes.items.push_back(Toggle(
        T::T("Sin HUD (F9)", "No HUD (F9)"), "dbz3_sin_hud",
        T::T("Oculta todo el marcador del combate: barras, retratos y contadores. Sirve con la camara libre y sin ella. "
             "Tambien con la tecla F9. No se guarda: al abrir el juego vuelve a verse.",
             "Hides the whole fight HUD: bars, portraits and counters. Works with and without the free camera. Also "
             "with the F9 key. Not saved: the HUD is back next time you start the game.")));
    // B2-P01 (11/11): filas de los modos de B2 ya escritos (F6, F8, S4, Shenron). Sus valores de serie no cambian.
    modes.items.push_back(Toggle(
        Tagged(T::T("Pasos del tablero B2", "B2 board footsteps"), kExperimental), "dbz3_b2_pasos_estrictos",
        T::T("Prueba la frontera de sonido del original. Apagado de serie; sincronizacion pendiente de verificar.",
             "Tests the original sound boundary. Off by default; timing still needs verification.")));
    modes.items.push_back(Toggle(
        Tagged(T::T("Shenron de B2", "B2 Shenron"), kExperimental), "dbz3_b2_shenron",
        T::T("Bolas de Dragon con sus estrellas y columna de luz de bordes suaves al llamar a Shenron en B2.",
             "Dragon Balls with their stars and a soft-edged light column when calling Shenron in B2.")));
    // B2-FX-02 (11/11): recursos visuales del tablero como el original. Apagado de serie.
    modes.items.push_back(Toggle(
        Tagged(T::T("Efectos del tablero B2", "B2 board effects"), kExperimental), "dbz3_b2_fx_nativo",
        T::T("Tablero de B2: selector de aliados con el velo, los fundidos, las flechas y la barra de ayuda del "
             "original, y los fundidos de las subidas de ataque y defensa. Apagado deja lo anterior.",
             "B2 board: ally selector with the original's veil, fades, arrows and help bar, and the fades of the "
             "attack and defence boosts. Off keeps the previous look.")));
    // B2-P16 (11/11): requisitos de los deseos de Shenron desde el inventario de capsulas. Apagado de serie.
    modes.items.push_back(Toggle(
        Tagged(T::T("Deseos B2 por capsulas", "B2 wishes from capsules"), kExperimental), "dbz3_b2_requisitos_inventario",
        T::T("Shenron de B2: los deseos que en el original piden tener una capsula se abren con las capsulas que has "
             "ganado. Apagado deja lo anterior.",
             "B2 Shenron: wishes that need a capsule in the original open with the capsules you have won. Off keeps "
             "the previous behaviour.")));
    // F8 (noche 10/11): encuadre de la rueda y aro de las caras medidos en los datos de B2 (cvar en pasarela_nave_rueda.inl).
    modes.items.push_back(Toggle(
        Tagged(T::T("Rueda de la nave medida", "Measured ship wheel"), kBeta), "dbz3_nave_rueda_datos",
        T::T("Rueda de personajes de la Nave de Babidi: encuadre, escala y aro de las caras sacados de los datos de "
             "Budokai 2 (29 luchadores y extras). Apagado usa lo anterior.",
             "Babidi's ship character wheel: framing, scale and face ring taken from Budokai 2 data (29 fighters "
             "and extras). Off keeps the previous look.")));
    // S4 (B2-10): espera de la pantalla de resultado de la Nave. 0 = el original (sin espera extra).
    modes.items.push_back(Number(
        T::T("Espera del resultado de la Nave (s)", "Spaceship result hold (s)"), "dbz3_nave_resultado_seg", 0.0,
        30.0, 0.5,
        T::T("Solo Nave de Babidi (B2). 0 = como el original: la pantalla de resultado ya dura 7-10 s cuando hay "
             "Kili. Con un valor, se queda ese tiempo antes de volver a la torre; el total fin->torre se alarga, "
             "asi que se deja en 0 salvo en pruebas.",
             "Babidi's Spaceship only (B2). 0 = as the original: the result screen already lasts 7-10 s with Kili. "
             "With a value it stays that long before returning to the tower; the fin->tower total grows, so keep "
             "it at 0 except in tests.")));
  }

  // 6. COMBATE (frente combate, src/combate.cpp): todo apagado de serie; lo que cambia el combate no se usa online.
  {
    auto& fight = menu.sections.emplace_back();
    fight.title = Titled(T::T("COMBATE", "FIGHT"),
                         T::T("Opcional: todo viene apagado. Nada de esto se usa en el online.",
                              "Optional: everything starts off. None of this is used online."));
    fight.items.push_back(Toggle(
        T::T("Aviso de golpe", "Hit warning"), "dbz3_combate_aviso",
        T::T("Un aro bajo tu barra de vida cuando un golpe del rival va a llegar: es el momento de pulsar la guardia "
             "para el contraataque. Solo se ve: no cambia el combate.",
             "A ring under your health bar when a rival hit is about to land: the moment to press guard for the "
             "counter. Display only: the fight does not change.")));
    fight.items.push_back(Toggle(
        T::T("Reglas competitivas v0", "Competitive rules v0"), "dbz3_combate_reglas_v0",
        T::T("AVISO: cambia el dano de 2 golpes de Goku (patada 50 a 70, adelante + punetazo 35 a 50). Apagado es el "
             "juego original. No se usa en el online.",
             "WARNING: changes the damage of 2 Goku hits (kick 50 to 70, forward + punch 35 to 50). Off is the "
             "original game. Not used online.")));
    fight.items.push_back(Toggle(
        T::T("Modo Competitivo", "Competitive Mode"), "dbz3_modo_competitivo",
        T::T("AVISO: cambia las reglas del combate, como en Infinite World. L2 es Aura Burst: pide una barra de ki, "
             "gasta ki mientras dura y no deja cansado al acabar. La definitiva sale con Arriba + Circulo. No hay "
             "Dragon Rush. Apagado es el juego original. En el online, solo si los dos lo tienen encendido.",
             "WARNING: changes the fight rules, as in Infinite World. L2 is Aura Burst: needs one ki bar, drains ki "
             "while active and leaves no fatigue when it ends. The ultimate is Up + Circle. No Dragon Rush. Off is "
             "the original game. Online, only if both players have it on.")));
    fight.items.push_back(Toggle(
        T::T("Definitiva directa", "Direct ultimate"), "dbz3_combate_definitiva_directa",
        T::T("La definitiva sin entrar en el modo hiper: su misma entrada, pagando una vez el ki de su capsula. No se "
             "usa en el online.",
             "The ultimate without entering hyper mode: same input, paying its capsule's ki once. Not used online.")));
    fight.items.push_back(Choice(
        T::T("Boton de cancelacion", "Cancel button"), "dbz3_combate_boton_cancel",
        {{"no", T::T("No", "Off")}, {"lb", "LB"}, {"rb", "RB"}},
        T::T("Ese boton hace adelante + ki de una vez (la cancelacion al final de un combo) y deja de hacer su atajo. "
             "Mantenerlo no repite. No se usa en el online.",
             "That button does forward + ki at once (the cancel at the end of a combo) instead of its shortcut. "
             "Holding it does not repeat. Not used online.")));
    fight.items.push_back(Choice(
        T::T("Perder la transformacion", "Lose transformation"), "dbz3_combate_reversion",
        {{"0", T::T("No", "Off")}, {"50", T::T("Golpes de 50 o mas", "Hits of 50+")}, {"100", "100+"}, {"300", "300+"}},
        T::T("Un golpe de ese dano o mas te devuelve a la forma normal, como cuando te quedas sin ki. Las fusiones no "
             "cambian. No se usa en el online.",
             "A hit of that damage or more sends you back to normal form, like running out of ki. Fusions do not "
             "change. Not used online.")));
  }

  // 7. ESCENARIO (frente escenarios: clima.cpp, gigantes.cpp, cine.cpp): apagados de serie; ninguno actua en el online.
  {
    auto& stage = menu.sections.emplace_back();
    stage.title = Titled(T::T("ESCENARIO", "STAGE"),
                         T::T("Opcional: todo viene apagado. Nada de esto se usa en el online.",
                              "Optional: everything starts off. None of this is used online."));
    stage.items.push_back(Toggle(T::T("Clima vivo", "Living weather"), "dbz3_clima",
                                 T::T("Durante el combate el escenario pasa del día al atardecer (bruma cálida) y "
                                      "a la noche (cielo oscuro y bruma azul). Solo se ve: no cambia el combate. No "
                                      "se usa en el online.",
                                      "During the fight the stage goes from day to dusk (warm haze) and to night "
                                      "(dark sky and blue haze). Display only: the fight does not change. Not used "
                                      "online.")));
    stage.items.push_back(Choice(T::T("Tiempo del clima vivo", "Living weather sky"), "dbz3_clima_tiempo",
                                 {{"0", T::T("Despejado", "Clear")}, {"1", T::T("Lluvia", "Rain")},
                                  {"2", T::T("Nieve", "Snow")}},
                                 T::T("Con «Clima vivo» encendido, lluvia o nieve en los escenarios de B3. Despejado "
                                      "de serie.",
                                      "With Living weather on, rain or snow on the B3 stages. Clear by default.")));
    stage.items.push_back(Choice(T::T("Jefe gigante", "Giant boss"), "dbz3_jefe_gigante",
                                 {{"0", T::T("No", "Off")}, {"1.25", "125 %"}, {"1.5", "150 %"}, {"1.75", "175 %"},
                                  {"2", "200 %"}},
                                 T::T("El rival que controla la consola sale más grande, como un jefe gigante, y la"
                                      " cámara se abre para verlo. Solo cambia su tamaño en pantalla. No se usa en "
                                      "el online. (A partir de 200 % la cabeza puede salirse en el cuerpo a cuerpo:"
                                      " el tope que encuadra bien es 200 %.)",
                                      "The rival controlled by the console appears bigger, like a giant boss, and "
                                      "the camera widens to show it. Only its on-screen size changes. Not used "
                                      "online. (Above 200% the head can leave the frame in close combat: 200% is"
                                      " the largest size that frames well.)")));
    stage.items.push_back(Toggle(T::T("Escenarios repetidos", "Repeated stages"), "dbz3_escenarios_repetidos",
                                 T::T("Muestra en la pantalla de escenarios los añadidos que son el mismo lugar que uno "
                                      "del juego (p. ej. el Torneo de B2). Apagado de serie. Se aplica al reiniciar.",
                                      "Shows on the stage screen the added stages that are the same place as one of "
                                      "the game (e.g. the B2 Tournament). Off by default. Applies after a restart.")));
    stage.items.push_back(Toggle(T::T("Revancha aleatoria", "Random rematch"), "dbz3_escenarios_revancha_aleatoria",
                                 T::T("Con el escenario «?» del Duelo, cada revancha carga un escenario distinto de los "
                                      "visibles. Apagado de serie. No se usa en el online.",
                                      "With the random (?) stage of the Duelo, every rematch loads a different stage. "
                                      "Off by default. Not used online.")));
    stage.items.push_back(Toggle(T::T("El «?» entre todos", "Random stage among all"), "dbz3_esc_azar_todos",
                                 T::T("El escenario «?» del Duelo elige entre todos los visibles, no solo entre los que "
                                      "muestra la rueda. Apagado de serie.",
                                      "The random (?) stage of the Duelo picks among every visible stage, not only the "
                                      "ones on the wheel. Off by default.")));
    stage.items.push_back(Toggle(T::T("Intro de cine", "Cinematic intro"), "dbz3_cine_intro",
                                 T::T("Al empezar cada combate, 10 s de cámara de cine sobre el escenario y los "
                                      "luchadores; A la salta. No se usa en el online.",
                                      "When each fight starts, 10 s of cinematic camera over the stage and the "
                                      "fighters; A skips it. Not used online.")));
    stage.items.push_back(Toggle(
        Tagged(T::T("IK de suelo de B2 por ficha", "B2 floor IK per fighter"), kExperimental),
        "dbz3_b2_ik_escala_ficha",
        T::T("B2: escala los umbrales del IK de suelo por la altura del modelo HD de cada personaje "
             "(Cell 1,10; Ten/Yamhan 1,1025; el resto 1,00). Apagado de serie: A/B en pruebas.",
             "B2: scales the floor IK thresholds by each fighter's HD model height (Cell 1.10; "
             "Tien/Yamhan 1.1025; the rest 1.00). Off by default: A/B testing.")));
  }

  // 8. AVANZADO: herramientas de laboratorio y el launcher completo.
  {
    auto& advanced = menu.sections.emplace_back();
    advanced.title = Titled(T::T("AVANZADO", "ADVANCED"),
                            T::T("Para quien quiere más control.", "For those who want more control."));
    // F4 (solF4, 10/11): escenas de transicion de B2 aun sin cotejo visual. La Llanura sigue encendida de serie y
    // no tiene fila aqui.
    advanced.items.push_back(Toggle(T::T("B2: robot de Base (prueba)", "B2: Base robot (test)"), "dbz3_b2_transicion_base",
                                  T::T("Escena experimental. Apagada de serie; no se usa online.", "Experimental scene. Off by default; not used online.")));
    advanced.items.push_back(Toggle(T::T("B2: destruccion de Kami (prueba)", "B2: Kami destruction (test)"), "dbz3_b2_transicion_kami",
                                  T::T("Escena experimental. Apagada de serie; no se usa online.", "Experimental scene. Off by default; not used online.")));
    advanced.items.push_back(Toggle(T::T("B2: transicion de Nieve (prueba)", "B2: Snow transition (test)"), "dbz3_b2_transicion_nieve",
                                  T::T("Escena experimental. Apagada de serie; no se usa online.", "Experimental scene. Off by default; not used online.")));
    advanced.items.push_back(Toggle(T::T("B2: transicion de Ciudad (prueba)", "B2: City transition (test)"), "dbz3_b2_transicion_ciudad",
                                  T::T("Escena experimental. Apagada de serie; no se usa online.", "Experimental scene. Off by default; not used online.")));
    advanced.items.push_back(Toggle(T::T("B2: verificar inicio (prueba)", "B2: verify start (test)"), "dbz3_b2_validar_inicio",
                                  T::T("Comprueba recursos y reinicia el estado entre escenas. Apagado de serie.", "Checks resources and resets state between scenes. Off by default.")));
    advanced.items.push_back(Toggle(
        T::T("Sonda del laboratorio", "Lab probe"), "dbz3_combate_sonda",
        T::T("Guarda cada cuadro y cada golpe del combate en logs/combate_*.csv para medir los golpes (frame data).",
             "Saves every frame and hit of the fight to logs/combate_*.csv to measure moves (frame data).")));
    if (open_full_settings) {
      Item full;
      full.kind = Item::Kind::kAction;
      full.label = T::T("Todos los ajustes", "All settings");
      full.value_text = "F4";
      full.help = T::T("Abre el launcher completo encima del juego: mods, personajes, controles...",
                       "Opens the full launcher over the game: mods, characters, controls...");
      full.action = std::move(open_full_settings);
      advanced.items.push_back(std::move(full));
    }
  }
  REXLOG_INFO("dbz3: quick settings menu ready (F1 / {})", rex::cvar::GetFlagByName("quick_menu_buttons"));
}

void RefreshQuickMenuLanguage(rex::ui::QuickMenuConfig& menu, std::function<void()> open_full_settings) {
  if (dbz3::settings::Language() == g_menu_language) return;
  rex::ui::QuickMenuConfig fresh;
  ConfigureQuickMenu(fresh, std::move(open_full_settings));
  // Everything but the callbacks (can_open is the caller, still running).
  menu.title = std::move(fresh.title);
  menu.sections = std::move(fresh.sections);
  menu.quick_toggle_label = std::move(fresh.quick_toggle_label);
  menu.quick_toggle_cvar = std::move(fresh.quick_toggle_cvar);
  menu.subtitle = std::move(fresh.subtitle);
  menu.text_change = std::move(fresh.text_change);
  menu.text_close = std::move(fresh.text_close);
  menu.text_saved = std::move(fresh.text_saved);
  menu.text_restart = std::move(fresh.text_restart);
  menu.text_on = std::move(fresh.text_on);
  menu.text_off = std::move(fresh.text_off);
}

}  // namespace dbz3::ingame

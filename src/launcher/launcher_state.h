// dbz3 - Pre-game launcher screen (ImGui dialog).
// Dark modern style with Dragon Ball accent colors.

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

#include "mod_pipeline.h"
#include "settings.h"
#include "../mods.h"
#include "../native_mods.h"

namespace rex::ui {
class ImmediateDrawer;
class ImmediateTexture;
}  // namespace rex::ui

namespace dbz3::launcher {

// The app hands the launcher its immediate drawer so it can show image previews
// (new-character icon / name banner / portraits). Null = previews disabled.
void SetPreviewDrawer(rex::ui::ImmediateDrawer* drawer);
rex::ui::ImmediateDrawer* PreviewDrawer();
// Dialogo de Windows para elegir un archivo (tambien desde el juego). `out` en UTF-8; false si se cancela.
bool PickFileDialog(std::string& out, const char* filter_desc, const char* filter_ext);
std::string PathUtf8(const std::filesystem::path& p);   // para texturas propias (caras del Tag Team)
// Runs a function on the UI thread after the current paint (the app's
// CallInUIThreadDeferred). The launcher reads the controllers through it:
// reading them may pump window events, which must not happen mid-paint.
void SetUiDefer(std::function<void(std::function<void()>)> defer);
// Ejecuta fn en el hilo de la interfaz fuera del pintado (lectura de mandos de los paneles).
void DeferUi(std::function<void()> fn);
// A file dropped on the window (any thread). The launcher installs .zip mods and copies
// edited texture PNGs into "Mi pack de texturas" on its next frame.
void QueueDroppedFile(const std::filesystem::path& path);

class LauncherDialog : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_play);
  ~LauncherDialog() override;
  void HandleDroppedFiles();

  // Over a running game (F4): the game doesn't see the controller meanwhile.
  void SetInGame(bool in_game);

  // Apply the shared dark DBZ theme (also used by the in-game F4 menu).
  static void ApplyTheme();

 protected:
  void OnDraw(ImGuiIO& io) override;
  // Called when the dialog is dismissed (Play button or window close). Auto-saves
  // the user settings so choices made in the launcher are never lost, even if the
  // user forgets to press "Save settings".
  void OnClose() override;

 private:
  void DrawVideoTab();
  void DrawUpscaleTab();
  void DrawAudioTab();
  void DrawInputTab();
  void DrawModsTab();
  void DrawNativeModsTab();
  void DrawNovasModsTab();
  void DrawModelSwapTab();
  void DrawTexturesTab();
  void DrawNewCharactersTab();
  void DrawImporter();
  void ParseImporterOutput();
  void DrawDevTab();
  // "Novedades" (novedades.h): boton en la cabecera; se abre sola una vez tras actualizar.
  void DrawNews();
  bool news_pending_ = false;
  bool news_checked_ = false;
  // "Primeros pasos": 4 pasos para empezar (se abre solo la primera vez; boton en la cabecera).
  void DrawFirstSteps();
  bool first_steps_pending_ = false;
  bool first_steps_checked_ = false;
  double saved_flash_until_ = 0.0;   // "Guardado" en el boton de guardar durante 2 s
  // Controles: tecla que se esta asignando ("a", "lstick_up"...; vacio = ninguna) y frames de espera
  // (el clic que abre la escucha no cuenta como tecla).
  std::string kb_listen_;
  int kb_listen_wait_ = 0;
  // Ficha de un mod (al pulsar su nombre en la pestana Mods).
  void DrawModCard();
  std::string mod_card_;    // pedida este frame
  std::string card_mod_;    // la que esta abierta
  // Controller: feeds ImGui's gamepad navigation, LB/RB tabs, START = Play,
  // and remembers whether the last input came from the pad or the keyboard.
  void PollController(ImGuiIO& io);
  void DrawInputHints();

  struct AssetProbe {
    bool valid = false;
    double next_refresh = 0.0;
    std::filesystem::path game_root;
    std::string sel_region;
    bool root_ok = false;
    bool region_ok = false;
    bool us_ok = false;
    bool iso_mode = false;
    bool xex_present = false;
    bool xex_ok = false;
    bool xex_blocked = false;
    bool assets_ready = false;
    bool redirect_default_xex = false;
    std::string boot_note;
    dbz3::settings::XexStatus xex_status = dbz3::settings::XexStatus::kMissing;
  };
  void RefreshAssets();
  void InvalidateAssets() { assets_.valid = false; }
  AssetProbe assets_;

  std::function<void()> on_play_;

  // Model swap pipeline state.
  ModPipeline mod_pipeline_;
  int pipeline_src_idx_ = -1;
  int pipeline_dst_idx_ = -1;
  bool catalog_load_attempted_ = false;
  char output_buf_[8192] = {};
  // Custom data_cmn.afs path for the model swap (empty = auto-detect).
  bool afs_path_auto_ = true;
  char afs_path_buf_[1024] = {};

  // Texture mod pipeline state.
  int tex_src_idx_ = -1;
  int tex_dst_idx_ = -1;  // -1 = mismo bin que el origen (sin swap)
  char tex_mod_buf_[128] = {};
  char tex_dir_buf_[512] = {};  // carpeta de texturas (default = mods/<mod>/textures)
  // "Texturas faciles" (pestana Mods): conversion de capturas a PNG en curso y cuantas hay.
  // ISO + mods: copia de los .afs antes de jugar (settings::PrepareIsoData), en segundo plano. El hilo se queda
  // con su copia del estado (si se cierra el launcher, termina solo).
  struct IsoPrep {
    std::atomic<int> progress{0};
    std::atomic<bool> done{false};
    bool ok = false;
    bool then_play = false;   // pedida con JUGAR: al acabar, se juega
    std::string error;
  };
  void StartIsoPrep(bool then_play);
  void DrawPs2Box();
  std::string ps2_msg_;   // resultado de la ultima ISO de PS2 elegida
  // Pack de Infinite World (Nova's Mods): estado de cada receta de IW (importar.py oficiales) y casillas (true = la quiero)
  struct PackFila {
    std::string id, nombre;
    bool listo = false, instalado = false;
  };
  void DrawPackIW();
  void DrawRecetasNova();   // Nova's Mods: mods que se construyen con las ISO del jugador (nova_recetas.py)
  struct RecetaNova {
    std::string id, nombre_es, nombre_en, falta;
    bool instalado = false;
    std::string desc_es, desc_en;
  };
  std::vector<RecetaNova> nrec_filas_;
  int nrec_estado_ = 0;   // 0 sin leer, 1 leyendo la lista, 2 lista leida, 3 construyendo
  std::string nrec_msg_;
  void DrawVideosHd();   // Nova's Mods: opening y final HD (descarga opcional)
  void DrawMusicaT3();   // Nova's Mods: musica de Tenkaichi 3 creada con la ISO del jugador
  std::string t3_msg_;
  std::vector<PackFila> pack_filas_;
  std::map<std::string, bool> pack_quiero_;
  bool pack_consulta_ = false;
  int pack_gen_ = -1;
  std::string pack_msg_;
  int novas_tipo_ = 0;    // Nova's Mods: 0 todos, 1 personajes, 2 historias, 3 texturas
  void DrawIsoPrepBox();
  std::shared_ptr<IsoPrep> iso_prep_;
  std::string iso_prep_error_;   // la ultima copia fallo: aviso junto a JUGAR; el siguiente JUGAR juega sin mods
  int active_mods_ = 0;         // mods activos (aviso de modo ISO junto a JUGAR)
  int broken_mods_ = -1;        // mods activos con problemas (-1 = sin contar)
  bool tex_easy_job_ = false;
  int tex_easy_captures_ = -1;  // -1 = sin contar todavia
  std::string tex_easy_status_;

  // New characters tab (roster_build.py).
  struct CharacterSource {
    std::string folder;
    std::string name;
    bool costume = false;  // traje.toml: traje extra de un personaje existente (sin plaza propia)
    int target_id = -1;    // traje: [traje] personaje = N
    int donor = -1;
    int req_id = -1;      // plaza pedida (id = ...), -1 = automatica
    int after = -1;       // despues_de, -1 = el donante
    int icon_src = 0;     // 0 modelo, 1 imagen (ui/cara.png), 2 terminado (ui/icono.png)
    int portrait_src = 0;
    // Fuentes de "conservar las imagenes originales" (port con arte propio, p.ej.
    // Infinite World); -1 = el mod no trae esa imagen.
    int orig_icon_src = -1;
    int orig_portrait_src = -1;
    struct Capsule {
      std::string name;
      std::string kind;  // especial | definitiva | transformacion
      int form = 1;
    };
    std::vector<Capsule> capsules;   // [[capsula]] de personaje.toml
    bool iw_port = false;            // trae su propio moveset (camara.bin): port
    float icon_adj[4] = {1.0f, 0.0f, 0.0f, 3.0f};       // zoom, dx, dy, giro (= ICON_ADJ)
    float portrait_adj[4] = {1.0f, 0.0f, 0.0f, 35.0f};  // = PORTRAIT_ADJ
    bool enabled = true;
  };
  std::vector<CharacterSource> char_sources_;
  // Plaza (free character ID) each enabled source gets, same rule as roster_build.py.
  std::map<std::string, int> AssignSlots() const;
  void DrawSlotTable(const std::map<std::string, int>& slots);
  void DrawCharacterEditor(CharacterSource& cs, const std::map<std::string, int>& slots);
  void RunCharacterPreview(const std::string& mod, std::vector<std::string> args);
  struct PreviewImage {
    std::unique_ptr<rex::ui::ImmediateTexture> tex;
    int w = 0;
    int h = 0;
    std::filesystem::file_time_type stamp{};
  };
  // Loads (and reloads when the file changes) a raw preview written by
  // roster_build.py vista: u32 width, u32 height (LE) + RGBA8.
  const PreviewImage* Preview(const std::filesystem::path& rgba);
  void PreviewWidget(const std::filesystem::path& rgba, float scale);
  std::map<std::string, PreviewImage> previews_;
  std::string nc_selected_;          // installed character being edited
  std::string ed_loaded_for_;        // editor fields loaded for this folder
  char ed_name_buf_[64] = {};
  char ed_import_buf_[1024] = {};
  char ed_cap_name_buf_[64] = {};
  int ed_cap_kind_ = 0;              // 0 especial, 1 definitiva, 2 transformacion
  int ed_cap_game_ = 0;             // juego de origen al importar capsulas (0 = detectar)
  int ed_cap_form_ = 1;
  bool slots_expanded_ = false;
  // Cambio de pestana pedido desde otra (Mods <-> Personajes nuevos).
  int request_tab_ = 0;              // 0 ninguno, 1 Mods, 2 Personajes nuevos, 3 Nova's Mods, 4 Video
  std::vector<std::string> pending_preview_;   // queued while another job runs
  std::string pending_preview_mod_;
  int nc_slot_choice_ = -1;          // -1 = automatica, si no el ID de la plaza
  bool char_sources_loaded_ = false;
  int char_sources_gen_ = -1;
  std::string roster_manifest_;
  char nc_name_buf_[64] = {};
  char nc_mod_buf_[64] = {};
  char nc_models_buf_[4096] = {};   // una ruta por linea
  char nc_face_buf_[1024] = {};
  char nc_portrait_buf_[1024] = {};
  int nc_donor_idx_ = 21;           // Recoome
  int nc_after_idx_ = -1;
  int nc_forms_ = 1;

  // Controller state (read on the UI thread outside the paint, see SetUiDefer).
  struct PadShared {
    uint16_t buttons = 0;
    int16_t lx = 0, ly = 0;
    bool valid = false;
    bool pending = false;
  };
  std::shared_ptr<PadShared> pad_ = std::make_shared<PadShared>();
  uint16_t pad_last_buttons_ = 0;
  bool pad_mode_ = false;          // last input: true = controller, false = keyboard/mouse
  int tab_index_ = 0;              // tab drawn this frame
  int tab_request_ = -1;           // tab to select next frame (LB/RB, Ctrl+Tab)
  bool pad_play_ = false;          // START pressed
  bool launching_ = false;         // JUGAR pulsado: montando los personajes nuevos
  bool roster_queued_ = false;     // el montaje espera a que acabe otra tarea (instalar un zip)
  bool roster_ran_ = false;        // JUGAR con personajes que montar (su resultado decide si se juega)
  bool stages_pending_ = false;    // R8: falta el paso de escenarios de PS2 (ModPipeline::BuildStages)
  bool stages_running_ = false;    // R8: paso de escenarios en marcha (se juega al acabar, salga como salga)
  bool in_game_ = false;
  bool input_blocked_ = false;

  // Character importer (importar.py): games found on disk and their characters.
  struct ImportSource {
    std::string id, name, state, path;
  };
  struct ImportEntry {
    std::string key, name, kind, suggested;  // kind: b1 | trajes | formas
    int donor = -1;
    int count = 0;
    bool port = false;                       // community moveset port available
  };
  std::vector<ImportSource> imp_sources_;
  std::vector<ImportEntry> imp_entries_;
  std::string imp_source_;           // selected game
  std::string imp_message_;          // last result line
  int imp_pending_ = 0;              // 1 sources, 2 character list, 3 importing
  int imp_selected_ = -1;
  int imp_donor_idx_ = -1;           // -1 = the importer's suggestion
  int imp_preset_ = 0;               // indice en imp_presets_ (presets_importar.toml)
  std::vector<std::pair<std::string, std::string>> imp_presets_;   // nombre, descripcion (importar.py lista)
  bool imp_physics_ = false;         // fisica = "donante"
  bool imp_ground_ = true;           // altura (pies al suelo)
  bool imp_loaded_ = false;
  bool imp_query_failed_ = false;    // la ultima consulta salio con error (no es "lista vacia")
  char imp_search_[64] = {};
  char imp_name_[64] = {};

  // Mod manifest editing state.
  bool editing_mod_ = false;
  std::string edit_mod_name_;
  char edit_name_buf_[256] = {};
  char edit_desc_buf_[2048] = {};
  char edit_author_buf_[256] = {};
  char edit_version_buf_[128] = {};
  bool pending_manifest_reload_ = false;

  // Mods center (P4.1): zip install + profiles.
  std::string mods_status_;  // transient status line shown in the Mods tab
  std::string native_mods_status_;
  // Pestana Juego: guardados y catalogo leidos al abrirla y tras cada accion (no en cada frame).
  struct SaveFile {
    std::filesystem::path path;
    bool supported = false;
  };
  std::vector<SaveFile> saves_;
  std::vector<dbz3::NativeModInfo> native_catalog_;
  bool saves_loaded_ = false;
  bool save100_ok_ = false;
  std::string nova_last_status_;
  bool profile_name_dialog_ = false;
  char new_profile_buf_[64] = {};

  // Mods tab: cached mod list (avoids a recursive disk scan every frame) plus
  // a search filter. The cache is invalidated after toggles/installs/edits and
  // rebuilt lazily on the next draw.
  std::vector<dbz3::ModInfo> mods_cache_;
  bool mods_loaded_ = false;   // false = hay que volver a leer la lista (PollModsScan la pide)
  bool mods_ready_ = false;    // ya llego la primera lectura
  struct ModsScan {            // lectura en otro hilo: 1 = lista lista, 2 = tambien los conflictos
    std::atomic<int> stage{0};
    bool taken = false;        // la lista ya se paso a mods_cache_ (hilo de la interfaz)
    std::vector<dbz3::ModInfo> mods;
    std::vector<dbz3::ModConflict> conflicts;
  };
  std::shared_ptr<ModsScan> mods_scan_;
  void PollModsScan();
  char mods_search_buf_[128] = {};
  char profiles_search_[128] = {};
  char novas_search_[128] = {};
  char characters_search_[128] = {};
  // Last ModPipeline generation seen: when the async swap/texture build finishes
  // (counter bumps) the cached mod list is rebuilt so the new mod shows up.
  int last_pipeline_gen_ = 0;

  // Searchable character combos (Model Swap / Textures).
  char swap_src_search_buf_[128] = {};
  char swap_dst_search_buf_[128] = {};
  char tex_search_buf_[128] = {};

  // Game-data validation banner (P1): transient error shown when a folder the
  // user picked for "Carpeta extraida" is not a valid game dir.
  std::string banner_error_;

  // "Repair installation" (footer button or --dbz3_repair): the one-shot flag is
  // consumed once per session and the report is shown in a modal popup.
  bool repair_checked_ = false;
  bool repair_popup_ = false;
  bool session_checked_ = false;                 // ultima partida cerrada de golpe (una vez)
  bool kit_missing_ = false;                     // personajes nuevos sin kit: aviso junto a JUGAR
  bool roster_failed_ = false;                   // montaje fallido: el popup ofrece jugar con el anterior
  std::vector<dbz3::ModConflict> mods_conflicts_;  // A9: mods activos que se pisan (con mods_cache_)
  bool play_previous_roster_ = false;
  std::vector<std::string> session_suspects_;
  std::string repair_report_;
};

}  // namespace dbz3::launcher

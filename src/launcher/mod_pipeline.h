// dbz3 - Model swap pipeline integration (project-side, no SDK changes).
//
// Wraps the validated Python swap script (mod center hd/swap_b3.py) so the
// launcher can scan the B3 character catalog and run model swaps (B3 HD ->
// B3 HD) from the UI. Python is invoked asynchronously; output is captured.

#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dbz3::launcher {

// One character entry from the B3 catalog (catalog_b3.cat).
struct B3Char {
  int bin = 0;        // AFS entry (bin) of the model
  std::string label;  // e.g. "XGTN_BODY"
  std::string name;   // friendly name, e.g. "Goten"
  std::string variant;  // outfit/transformation ("" for main)
  bool playable = true;
  std::string note;

  std::string DisplayName() const {
    if (variant.empty()) return name;
    return name + " (" + variant + ")";
  }
};

class ModPipeline {
 public:
  // Reads the cached B3 catalog from disk. Returns false if missing.
  bool LoadCatalog();

  const std::vector<B3Char>& B3() const { return b3_; }
  bool CatalogLoaded() const { return loaded_; }

  // Asynchronous operation. Poll IsRunning() to know when done; read the
  // output via Output().
  void SwapB3ToB3(const B3Char& src, const B3Char& dst);

  // Texture mod pipeline (texture_b3.py): extract a character's textures as
  // editable PNGs into mods/<mod>/textures/, then rebuild the mod from the
  // edited PNGs. Both are asynchronous.
  void ExtractTextures(const B3Char& src, const std::string& mod_name,
                       const std::string& dir = "");
  void BuildTextures(const std::string& mod_name, int dest_slot = -1,
                     const std::string& dir = "");
  // "Texturas faciles": convierte las capturas del juego (DDS + index.jsonl) a PNG con el
  // nombre que reconoce un pack (<hash>_<WxH>_<formato>.png), por tamano, solo las nuevas.
  void ConvertTextureCaptures(const std::string& dump_dir, const std::string& out_dir);

  // New characters (mod center hd/roster_build.py): creates a source mod
  // (mods/<mod>/personaje.toml + models + face images) and builds the combined
  // generated mod "_roster" (IDs, appended data_cmn entries, select icons/names/
  // portraits, select framing) that the runtime reads. Both are asynchronous.
  struct NewCharacter {
    std::string mod;           // folder name under mods/
    std::string name;          // select name banner
    int donor = 21;            // character ID whose moveset/techniques it uses
    int after = -1;            // character ID after which its wheel cell goes (-1 = donor)
    int forms_per_costume = 1;
    int slot = -1;             // requested free ID (22-26, 31); -1 = first free
    std::vector<std::string> models;  // one per costume (x forms): PS2 #AMB, HD #AMB or LZX
    // Select images are rendered from the model by default; optional own art:
    std::string face;          // face art for the wheel icon (official ring/background added)
    std::string portrait;      // art for the P1/P2 portraits
  };
  void CreateCharacter(const NewCharacter& c);
  // Regenerates the select previews of a source mod (roster_build.py vista), with
  // extra args (adjustments, --guardar to persist them, --solo icono|retrato).
  void PreviewCharacter(const std::string& mod, const std::vector<std::string>& extra);
  void BuildRoster(bool force = false);
  // Escenarios nuevos desde los juegos de PS2 del jugador (campos de IW y escenarios de B1/B2): los que falten o
  // cuya ISO cambio, en mods/ (mod center hd/campos_iw.py auto). Se genera en su equipo: nada del juego viaja.
  void BuildStages();
  // Resultado del ultimo paso de escenarios (visible en la pestana Mods). `created` = escenarios completos en
  // mods/; `failed` = tareas que fallaron en la ultima pasada y `reason` = primer motivo (sin prefijo).
  struct StageReport {
    int created = 0;
    int failed = 0;
    bool ran = false;          // hay un resultado (de esta sesion o del registro guardado)
    bool up_to_date = false;   // la ultima pasada dijo "al dia"
    std::string reason;
  };
  StageReport StagesReport();
  // Registro de la ultima pasada (mods/_escenarios_ps2.txt): sobrevive al reinicio del launcher.
  static std::filesystem::path StagesRegistryPath();
  // Capsules (skills) of a source mod: roster_build.py capsulas (--anadir NOMBRE TIPO
  // [FORMA] | --quitar N | --renombrar N NOMBRE | --subir N); edits personaje.toml.
  void EditCapsules(const std::string& mod, const std::vector<std::string>& extra);
  // Character importer (mod center hd/importar.py). Queries print TAB lines in
  // Output(): "fuentes" -> "fuente	ID	NAME	STATE	PATH	NOTE", "lista ID" ->
  // "personaje	KEY	NAME	DONOR	NOTE". ImportCharacter creates the source mod.
  void ImporterQuery(const std::vector<std::string>& args);
  struct ImportRequest {
    std::string source;  // b1, b2, b3, iw...
    std::string key;     // character key from "lista"
    std::string mod;     // folder under mods/
    std::string name;    // select name banner
    int donor = -1;      // -1 = the importer's suggestion
    std::string preset;  // presets_importar.toml (original | solo_modelos)
    std::vector<std::string> options;  // --opcion clave=valor
  };
  void ImportCharacter(const ImportRequest& r);
  // Pack de Infinite World (Nova's Mods): crea con importar.py "oficial" las recetas de `install` (con la copia de
  // IW del jugador: no se reparte nada del juego) y aparta a mods_respaldo/ las de `remove` (activas o desactivadas;
  // nunca se borra). Con un trabajo en curso no hace nada.
  void RunOfficialPorts(const std::vector<std::string>& install, const std::vector<std::string>& remove);
  // Tag Team: equipo de 2-4 personajes de B3 (roster_build.py equipo -> mods/equipo_*).
  void CreateTeam(const std::vector<int>& members, const std::string& name);
  void ClearOutput();
  // The modding kit (mod center hd/ with the importer) is next to the game.
  // roster_build.py presente (kit de modding instalado).
  static bool RosterToolAvailable();
  static bool StagesToolAvailable();
  static bool ToolsInstalled();
  // Blocks until the current async run (if any) finishes.
  void Wait();
  // True if any enabled mod declares a new character (mods/*/personaje.toml).
  static bool HasCharacterSources();
  static std::filesystem::path ModsDir();

  // Path to the data_cmn.afs to operate on (the model source/destination).
  // Auto-detected by default; the launcher can override it if the user picks
  // a custom location.
  void SetAfsPath(const std::string& path);
  std::string AfsPath() const;

  // -1: pendiente o fallo de infraestructura; 0: proceso terminado correctamente.
  int ExitCode() const { return exit_code_.load(); }
  // A4: estado del ultimo trabajo, sin la ambiguedad de ExitCode()==-1. kRunning se fija antes
  // de lanzar el hilo; al terminar por cualquier camino queda kOk (exit 0) o kFailed.
  enum class JobState { kIdle, kRunning, kOk, kFailed };
  JobState State() const { return state_.load(); }
  // _roster publicado y completo (roster.toml, manifest.txt y build.hash); sin fuentes de
  // personaje activas no hace falta ninguno.
  static bool RosterArtifactsOk();
  bool IsRunning() const { return running_.load(); }
  std::string Output() const;
  // Last "@progreso k/n text" line printed by the running tool (roster_build): 0..1,
  // or -1 if none yet. `what` gets the text (the character being built).
  float Progress(std::string* what = nullptr) const;

  // Monotonic counter bumped each time an async run finishes; the launcher uses
  // it to refresh its cached mod list when a new mod appears on disk.
  int Generation() const { return generation_.load(); }

  // Joins the worker on destruction. A finished-but-unjoined std::thread is
  // still joinable, and destroying a joinable thread calls std::terminate();
  // this prevents a crash when the launcher closes right after a swap/build.
  ~ModPipeline();

 private:
  void RunAsync(const std::filesystem::path& script,
                const std::vector<std::string>& args);
  void AppendOutput(const std::string& text);
  // G11: aparta a mods_respaldo/ las carpetas de escenario auto-generadas (origen.txt) incompletas, para que
  // campos_iw.py las vuelva a crear en vez de darlas por buenas con el sello viejo.
  int PruneIncompleteStages();
  void SetStagesReport(int created, int failed, const std::string& reason);
  void ParseStagesOutputLocked();
  void WriteStagesRegistryLocked(const StageReport& report);
  void LoadStagesRegistryLocked();
  // us/ folder next to the selected data_cmn.afs ("" = let the script auto-detect).
  std::string UsDir() const;
  std::vector<std::string> SwapArgs(const B3Char& src, const B3Char& dst,
                                    const std::string& mod) const;
  std::vector<std::string> TextureArgs(const B3Char& src,
                                       const std::string& mod,
                                       const std::string& dir) const;
  std::vector<std::string> BuildTextureArgs(const std::string& mod,
                                          int dest_slot,
                                          const std::string& dir) const;

  std::vector<B3Char> b3_;
  bool loaded_ = false;
  std::atomic<int> exit_code_{-1};
  std::atomic<JobState> state_{JobState::kIdle};
  std::atomic<bool> running_{false};
  std::atomic<int> generation_{0};
  mutable std::mutex mutex_;
  std::string output_;
  std::string afs_path_;
  std::thread worker_;
  // G11: solo se interpreta la salida de un trabajo de escenarios (no la del roster/swap).
  bool stages_run_pending_ = false;
  bool last_job_stages_ = false;
  bool stages_parsed_ = false;
  bool stages_report_loaded_ = false;
  std::string stages_parsed_output_;
  StageReport stages_report_;
};

}  // namespace dbz3::launcher
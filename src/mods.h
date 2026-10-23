// dbz3 - Mod manager (project-side, no SDK changes).
//
// Manages the "mods" folder next to the project assets. A mod is a subfolder
// that replaces game files by providing whole-file copies (e.g. a repacked
// .afs). The launcher lists mods and toggles enable/disable via a ".disabled"
// marker. Whole-file overrides are resolved by the SDK's VFS layer.
//
// Formato (igual que el proyecto hermano DBZ Budokai HD):
//   mods/<name>/<region>/file...     -> override de assets
//   mods/<name>/manifest.txt         -> metadata key=value:
//        name, description, author, version, type, source, target

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dbz3 {

// Root folder where mods live (project root "mods", next to the assets).
std::filesystem::path ModsRoot();

// One mod as seen by the launcher. `enabled` reflects the effective state
// after normalizing both disable conventions (folder named "foo.disabled" OR
// a ".disabled" marker file inside the folder).
struct ModInfo {
  std::string name;
  bool enabled = true;
  // Optional metadata from a "manifest.txt" inside the mod folder (key=value
  // lines). Empty fields fall back to the folder name / inferred type.
  std::string display_name;
  std::string description;
  std::string author;
  std::string version;
  // D4: version del juego con la que se activo/instalo el mod (fichero .dbz3_version). Vacio si no
  // hay marca (mod antiguo). El launcher avisa si no coincide con la version que corre.
  std::string made_for;
  std::string type;     // e.g. "swap_b3", "port_b3", "audio", "moveset"
  std::string source;   // source character/bin (e.g. "Goten (bin 298)")
  std::string target;   // target slot (e.g. "Krillin (326)")
  // Human-readable file count inside the mod (0 when empty).
  int file_count = 0;
  // Problems a non-technical user can fix (the launcher words them): kind + detail.
  // kRoster: aviso del ultimo montaje de personajes/trajes (mods/roster_avisos.txt), texto ya redactado.
  enum Problem { kEmpty = 1, kNested, kMissingFile, kBadTextureName, kBadTextureSize, kBadToml, kRoster };
  std::vector<std::pair<int, std::string>> problems;
};

// A mod unzipped inside an extra folder (mods/X/X/...): moves the inner contents up one level.
bool FixNestedMod(const std::string& mod_name);

// List mod folders under the mods root. Enabled mods first, then disabled.
// `check_problems` = false skips ModInfo::problems: the game side (roster, stages, campaigns)
// never shows them, and the texture size check opens every image of a pack (12345 PNG of an
// HD pack just installed = ~70 s with the antivirus checking each one).
std::vector<ModInfo> ListMods(bool check_problems = true);

// Enable/disable a mod (creates/removes the ".disabled" marker).
void SetModEnabled(const std::string& mod_name, bool enable);

// D4: deja en mods/<mod>/.dbz3_version la version del juego con la que se activo o instalo.
void StampModVersion(const std::string& mod_name, const std::string& version);

// Reads the raw manifest key for a mod ("" if the key is missing). Keys:
// name, description, author, version, type, source, target.
std::string GetModManifestValue(const std::string& mod_name,
                                const std::string& key);

// Sets a manifest key for a mod, creating/updating manifest.txt in the mod
// folder. Empty value removes the line. Returns false if not writable.
bool SetModManifestValue(const std::string& mod_name, const std::string& key,
                         const std::string& value);

// Infer/return a human-readable label for a mod type.
const char* ModTypeLabel(const std::string& type);

// Return a color for a mod type badge (0xRGB packed, negative = dim gray).
int ModTypeColor(const std::string& type);

// Installs a mod from a .zip file: extracts it (native PowerShell
// Expand-Archive) and normalizes the layout into mods/<name>/. The name is
// derived from the zip file (sanitized, suffixed if it already exists).
// Returns true on success (out_name = final mod folder name).
bool InstallModFromZip(const std::string& zip_path_utf8, std::string& out_name,
                       std::string& out_error);

// --- Mod profiles ---------------------------------------------------------
// A profile is a named set of enabled mods. "vanilla" is the built-in profile
// (all mods disabled) and is not stored in profiles.txt.
std::vector<std::string> ListProfiles();
// Enabled mods listed in a stored profile (empty for "vanilla").
std::vector<std::string> ProfileEnabledMods(const std::string& profile);
// Persists/replaces a profile with the given enabled set.
bool SaveProfile(const std::string& name,
                 const std::vector<std::string>& enabled_mods);
// Removes a stored profile. Returns false for "vanilla".
bool DeleteProfile(const std::string& name);
// Enables exactly the mods in the profile (and disables the rest).
void ApplyProfile(const std::string& name);

// --- Choques entre mods (A9) ------------------------------------------------
// Dos mods activos que sustituyen lo mismo del juego (la misma entrada us/<afs>/<n> o el mismo
// archivo entero). Gana el primero en orden alfabetico de carpeta, igual que el runtime
// (ScanModDirs); el launcher lo ensena para que el jugador sepa cual se ve.
struct ModConflict {
  std::string winner, loser;
  int resources = 0;
};
std::vector<ModConflict> FindConflicts();

// --- Memoria de la ultima partida (launcher que se cura solo) ---------------
// Al JUGAR se apunta "en_marcha" y los mods activos; al cerrar bien, "ok" y el perfil
// kGoodProfile; el gestor de cierres apunta "cerrado_de_golpe". Si la anterior no acabo
// bien, SessionSuspects() da los mods activos que no estaban en la ultima partida buena.
inline constexpr const char* kGoodProfile = "ultima partida buena";
void SessionStart();
void SessionEnd(bool ok);   // ok=false: cierre inesperado (seguro llamarla desde el gestor)
std::vector<std::string> SessionSuspects();
void SessionForget();       // el jugador eligio seguir igual: no volver a preguntar

// --- Sesion vanilla (online sin mods, solo esta partida) -----------------------
// El juego se reinicia con DBZ3_VANILLA_SESSION=1: la raiz de mods pasa a una carpeta vacia ("mods_vanilla", ver
// AfsModsRoot) y los mods del jugador no se leen, mueven ni cambian. Al cerrar, el siguiente arranque normal vuelve a
// tener los suyos. `pending` (rol/puerto/codigo) se deja en un fichero temporal para reconectar solo al volver.
bool VanillaSession();
bool RelaunchVanilla(const std::string& pending, bool skip_launcher);   // no vuelve si va bien
bool TakeVanillaPending(std::string& out);                              // lee y borra el temporal una vez

}  // namespace dbz3
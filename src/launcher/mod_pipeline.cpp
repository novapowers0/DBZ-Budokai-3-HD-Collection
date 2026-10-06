// dbz3 - Model swap pipeline integration (project-side).

#include "mod_pipeline.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#if REX_PLATFORM_WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#if !REX_PLATFORM_WIN32
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace dbz3::launcher {

namespace {

// Project root = folder that contains "us"/"eu" (walk up from the exe).
// Supports both layouts: <root>/us and <root>/assets/us (standalone release).
std::filesystem::path ProjectRoot() {
  auto exe_dir = rex::filesystem::GetExecutableFolder();
  std::filesystem::path probe = exe_dir;
  for (int depth = 0; depth < 6; ++depth) {
    if (std::filesystem::is_directory(probe / "us") ||
        std::filesystem::is_directory(probe / "eu") ||
        std::filesystem::is_directory(probe / "assets" / "us") ||
        std::filesystem::is_directory(probe / "assets" / "eu")) {
      return probe;
    }
    probe = probe.parent_path();
  }
  return exe_dir;
}

std::filesystem::path PipelineScript() {
  return ProjectRoot() / "mod center hd" / "swap_b3.py";
}

std::filesystem::path TextureScript() {
  return ProjectRoot() / "mod center hd" / "texture_b3.py";
}

std::filesystem::path RosterScript() {
  return ProjectRoot() / "mod center hd" / "roster_build.py";
}

std::filesystem::path ImporterScript() {
  return ProjectRoot() / "mod center hd" / "importar.py";
}

std::filesystem::path CatalogFile() {
  return ProjectRoot() / "mod center hd" / "catalog_b3.cat";
}

std::filesystem::path ModsOutDir() {
  // The mods the runtime reads live next to the game data. In the release the
  // exe and mods/ share the same folder, but walk up from the executable
  // looking for a "mods" directory just in case.
  std::filesystem::path probe = rex::filesystem::GetExecutableFolder();
  std::error_code ec;
  for (int depth = 0; depth < 4; ++depth) {
    const std::filesystem::path candidate = probe / "mods";
    if (std::filesystem::is_directory(candidate, ec)) {
      return candidate;
    }
    probe = probe.parent_path();
  }
  return rex::filesystem::GetExecutableFolder() / "mods";
}

std::string Quote(const std::string& s) { return "\"" + s + "\""; }

// Candidate command prefixes for a Python 3 interpreter. The order matters:
// on Windows a bare "python" can resolve to the Microsoft Store *alias stub*
// (a zero-byte reparse point in ...\WindowsApps) which just prints "Python was
// not found..." and exits with code 9009 -- that was issue #13. The python.org
// launcher ("py -3") never picks that stub, so it is tried first, then the
// usual names. Every candidate is actually *probed* before use.
std::vector<std::string> PythonCandidates() {
  std::vector<std::string> v;
  if (const char* py = std::getenv("DBZ3_PYTHON"); py && *py) {
    v.push_back(Quote(py));
  }
#if REX_PLATFORM_WIN32
  // Python portatil que trae la release (python/, con Pillow, numpy y tkinter): nadie tiene
  // que instalar nada para el Kit ni para las texturas.
  for (const auto& dir : {rex::filesystem::GetExecutableFolder(), ProjectRoot()}) {
    std::error_code ec;
    const auto exe = dir / "python" / "python.exe";
    if (std::filesystem::is_regular_file(exe, ec)) {
      v.push_back(Quote(exe.string()));
      break;
    }
  }
  v.push_back("py -3");
  v.push_back("python");
  v.push_back("python3");
#else
  v.push_back("python3");
  v.push_back("python");
#endif
  return v;
}

std::string PythonMissingMessage() {
  return
      "============================================================\n"
      "No se encontro Python 3 en este equipo.\n"
      "This feature (model swap / texture mods) runs Python scripts.\n"
      "============================================================\n"
      "LO MAS FACIL: descarga el 'Kit de Modding' del release y descomprimelo\n"
      "junto a dbz3.exe. Trae Python listo (carpeta python): no instalas nada.\n"
      "EASIEST: download the 'Modding Kit' from the release and extract it next\n"
      "to dbz3.exe. It ships Python ready to use (python folder).\n"
      "\n"
      "O bien / Or:\n"
      "1) Instala Python 3 desde https://www.python.org/downloads/\n"
      "   (en Windows, marca \"Add python.exe to PATH\").\n"
      "2) Instala las dependencias:\n"
      "       py -3 -m pip install pillow numpy\n"
      "3) Si lo tienes en otra ruta, define la variable DBZ3_PYTHON.\n"
      "\n"
      "Nota: el alias de Microsoft Store (python.exe en WindowsApps) NO sirve:\n"
      "solo muestra un aviso y termina con el codigo 9009.\n"
      "Note: the Microsoft Store 'python.exe' alias is only a stub (exit 9009).\n"
      "============================================================\n";
}

std::string DependencyHintMessage() {
  return
      "\n------------------------------------------------------------\n"
      "Parece que falta una dependencia de Python (Pillow / numpy).\n"
      "A Python dependency looks missing (Pillow / numpy).\n"
      "Instalalas con / Install them with:\n"
      "       py -3 -m pip install pillow numpy\n"
      "------------------------------------------------------------\n";
}

#if REX_PLATFORM_WIN32
// Runs a command line and returns true only if it exits with code 0. Output is
// discarded (NUL). Used to validate each Python candidate cheaply. A 15 s cap
// avoids a hung interpreter blocking the pipeline forever.
bool ProbeCommandLine(const std::string& cmd) {
  int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, nullptr, 0);
  if (wlen <= 0) {
    return false;
  }
  std::vector<wchar_t> wcmd(wlen);
  MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, wcmd.data(), wlen);

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE hNul = CreateFileW(L"NUL", GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                            OPEN_EXISTING, 0, nullptr);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hNul;
  si.hStdError = hNul;
  si.hStdInput = hNul;

  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  if (hNul != INVALID_HANDLE_VALUE) {
    CloseHandle(hNul);
  }
  if (!ok) {
    return false;
  }
  if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 2000);
  }
  DWORD rc = 1;
  GetExitCodeProcess(pi.hProcess, &rc);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return rc == 0;
}
#else
// POSIX probe: run the command through /bin/sh with output to /dev/null.
bool ProbeCommandLine(const std::string& cmd) {
  const char* devnull = "/dev/null";
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, devnull,
                                   O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, devnull,
                                   O_WRONLY, 0);

  const std::string shell = "/bin/sh";
  std::vector<char*> argv = {
      const_cast<char*>(shell.c_str()), const_cast<char*>("-c"),
      const_cast<char*>(cmd.c_str()), nullptr};
  pid_t pid = -1;
  const int rc = posix_spawn(&pid, shell.c_str(), &actions, nullptr, argv.data(),
                             environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) {
    return false;
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

// Returns a usable Python command prefix (already quoted / argument-ready),
// or "" if no working Python 3 could be found.
std::string ResolvePython() {
  for (const std::string& cand : PythonCandidates()) {
#if REX_PLATFORM_WIN32
    if (ProbeCommandLine(cand + " -c \"import sys\"")) {
      return cand;
    }
#else
    if (ProbeCommandLine(cand + " -c 'import sys'")) {
      return cand;
    }
#endif
  }
  return "";
}

int ParseIntField(const std::string& s) {
  return s.empty() ? 0 : std::atoi(s.c_str());
}

}  // namespace

bool ModPipeline::LoadCatalog() {
  std::lock_guard<std::mutex> lock(mutex_);
  b3_.clear();
  loaded_ = false;

  std::ifstream in(CatalogFile());
  if (!in.is_open()) {
    REXLOG_WARN("dbz3: catalogo B3 no encontrado en {}", CatalogFile().string());
    return false;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    // bin|nombre|label|variante|jugable
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, '|')) {
      parts.push_back(part);
    }
    if (parts.size() < 3) {
      continue;
    }
    B3Char c;
    c.bin = ParseIntField(parts[0]);
    c.name = parts[1];
    c.label = parts[2];
    if (parts.size() > 3) c.variant = parts[3];
    if (parts.size() > 4) c.playable = ParseIntField(parts[4]) != 0;
    if (parts.size() > 5) c.note = parts[5];
    b3_.push_back(std::move(c));
  }
  loaded_ = true;
  REXLOG_INFO("dbz3: catalogo B3 cargado ({} personajes)", b3_.size());
  return true;
}

ModPipeline::~ModPipeline() {
  // A std::thread that finished but was never joined/detached is STILL
  // joinable; destroying a joinable thread calls std::terminate(). RunAsync
  // only joins at the start of the next run, so closing the launcher right
  // after a swap/build would crash without this.
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::string ModPipeline::Output() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return output_;
}

void ModPipeline::SetAfsPath(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  afs_path_ = path;
}

std::string ModPipeline::AfsPath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return afs_path_;
}

void ModPipeline::AppendOutput(const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  output_ += text;
}

void ModPipeline::RunAsync(const std::filesystem::path& script,
                           const std::vector<std::string>& args) {
  if (running_.exchange(true)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    output_.clear();
  }
  if (worker_.joinable()) {
    worker_.join();
  }

  // "tail" = quoted script + quoted args. The Python interpreter itself is
  // resolved inside the worker with a real probe, so a bare "python" that
  // resolves to the Microsoft Store alias stub is skipped instead of failing
  // with exit code 9009 (issue #13).
  std::string tail = Quote(script.string());
  for (const std::string& a : args) {
    // Escapar cada argumento: si contiene espacios, envolverlo en comillas
    // para que cmd.exe lo trate como un solo token (las rutas del proyecto
    // tienen espacios, p.ej. "...DBZ Budokai 3 HD Collection\...").
    if (a.find(' ') != std::string::npos || a.find('\t') != std::string::npos) {
      tail += " " + Quote(a);
    } else {
      tail += " " + a;
    }
  }

  REXLOG_INFO("dbz3: mod pipeline: {}", tail);
  worker_ = std::thread([this, tail]() {
    const std::string python = ResolvePython();
    if (python.empty()) {
      AppendOutput(PythonMissingMessage());
      running_.store(false);
      return;
    }
    const std::string cmd = python + " " + tail;
#if REX_PLATFORM_WIN32
    // Usamos CreateProcess en vez de _popen: _popen pasa el comando a
    // "cmd.exe /c", que falla al parsear comillas cuando el comando empieza
    // con '"' (p.ej. "\"python\" ...") con el error "sintaxis de la etiqueta
    // del volumen" (reproducido con un test _popen). CreateProcess lanza
    // python directamente sin cmd.exe, redirigiendo stdout+stderr a un pipe.
    HANDLE hOutRead = nullptr, hOutWrite = nullptr;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) {
      AppendOutput("ERROR: no se pudo crear el pipe para python.\n");
      running_.store(false);
      return;
    }
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hOutWrite;
    si.hStdError = hOutWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    // Convertir el comando a wide para CreateProcessW.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wcmd(wlen);
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, wcmd.data(), wlen);

    PROCESS_INFORMATION pi{};
    // Fíjate: el command line de CreateProcess NO debe empezar con comilla
    // alrededor de todo; pasamos el comando tal cual (python ya se cita).
    const BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hOutWrite);
    if (!ok) {
      AppendOutput("ERROR: CreateProcess fallo (WinError " +
                   std::to_string(GetLastError()) + ").\n");
      CloseHandle(hOutRead);
      running_.store(false);
      return;
    }

    // Leer la salida (stdout+stderr combinados) del pipe.
    char buf[4096];
    DWORD n = 0;
    // (no terminator: buf[n] with n == sizeof(buf) wrote past the buffer and
    // corrupted the worker's stack on outputs of 4 KB+, like the importer's lists)
    while (ReadFile(hOutRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
      AppendOutput(std::string(buf, n));
    }
    CloseHandle(hOutRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 0;
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (rc != 0) {
      AppendOutput("\n[exit code " + std::to_string(rc) + "]\n");
    }
    // 9009 = the Microsoft Store alias stub ("command not found"); a
    // "No module named ..." traceback means Pillow/numpy are missing.
    if (rc == 9009) {
      AppendOutput(PythonMissingMessage());
    } else if (Output().find("No module named") != std::string::npos) {
      AppendOutput(DependencyHintMessage());
    }
    REXLOG_INFO("dbz3: mod pipeline: done (exit {})", rc);
    generation_.fetch_add(1);
    running_.store(false);
#else  // !REX_PLATFORM_WIN32
    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
      AppendOutput("ERROR: no se pudo crear el pipe para python.\n");
      running_.store(false);
      return;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

    const std::string shell = "/bin/sh";
    std::vector<char*> argv = {
        const_cast<char*>(shell.c_str()), const_cast<char*>("-c"),
        const_cast<char*>(cmd.c_str()), nullptr};
    pid_t pid = -1;
    const int spawn_rc = posix_spawn(&pid, shell.c_str(), &actions, nullptr,
                                     argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fds[1]);
    if (spawn_rc != 0) {
      close(pipe_fds[0]);
      AppendOutput("ERROR: no se pudo ejecutar python (errno " +
                   std::to_string(spawn_rc) + ").\n");
      running_.store(false);
      return;
    }

    char buf[4096];
    ssize_t n = 0;
    while ((n = read(pipe_fds[0], buf, sizeof(buf))) > 0) {
      AppendOutput(std::string(buf, static_cast<size_t>(n)));
    }
    close(pipe_fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      AppendOutput("\n[exit code " +
                   std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) +
                   "]\n");
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
      AppendOutput(PythonMissingMessage());
    } else if (Output().find("No module named") != std::string::npos) {
      AppendOutput(DependencyHintMessage());
    }
    generation_.fetch_add(1);
    running_.store(false);
#endif  // REX_PLATFORM_WIN32
  });
}

void ModPipeline::SwapB3ToB3(const B3Char& src, const B3Char& dst) {
  if (src.bin == 0 || dst.bin == 0) {
    AppendOutput("ERROR: el personaje origen/destino no tiene bin asignado.\n");
    return;
  }
  if (src.bin == dst.bin) {
    AppendOutput(
        "ERROR: el origen y el destino son el mismo personaje (bin " +
        std::to_string(src.bin) + "). Elige dos distintos.\n");
    return;
  }
  const std::string mod = "swap_" + std::to_string(src.bin) + "_on_" +
                          std::to_string(dst.bin);
  RunAsync(PipelineScript(), SwapArgs(src, dst, mod));
}

std::vector<std::string> ModPipeline::SwapArgs(const B3Char& src,
                                               const B3Char& dst,
                                               const std::string& mod) const {
  std::vector<std::string> args = {"--origen", std::to_string(src.bin),
                                   "--dest", std::to_string(dst.bin),
                                   "--mod", mod,
                                   "--out", ModsOutDir().string()};
  std::string afs;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    afs = afs_path_;
  }
  if (!afs.empty()) {
    args.push_back("--afs");
    args.push_back(afs);
  }
  return args;
}

std::vector<std::string> ModPipeline::TextureArgs(
    const B3Char& src, const std::string& mod, const std::string& dir) const {
  std::vector<std::string> args = {"extract",
                                   "--bin", std::to_string(src.bin),
                                   "--mod", mod,
                                   "--out", ModsOutDir().string()};
  if (!dir.empty()) {
    args.push_back("--dir");
    args.push_back(dir);
  }
  std::string afs;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    afs = afs_path_;
  }
  if (!afs.empty()) {
    args.push_back("--afs");
    args.push_back(afs);
  }
  return args;
}

std::vector<std::string> ModPipeline::BuildTextureArgs(
    const std::string& mod, int dest_slot, const std::string& dir) const {
  std::vector<std::string> args = {"build",
                                   "--mod", mod,
                                   "--out", ModsOutDir().string()};
  if (dest_slot >= 0) {
    args.push_back("--slot");
    args.push_back(std::to_string(dest_slot));
  }
  if (!dir.empty()) {
    args.push_back("--dir");
    args.push_back(dir);
  }
  std::string afs;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    afs = afs_path_;
  }
  if (!afs.empty()) {
    args.push_back("--afs");
    args.push_back(afs);
  }
  return args;
}

void ModPipeline::ExtractTextures(const B3Char& src,
                                  const std::string& mod_name,
                                  const std::string& dir) {
  if (src.bin == 0) {
    AppendOutput("ERROR: el personaje no tiene bin asignado.\n");
    return;
  }
  RunAsync(TextureScript(), TextureArgs(src, mod_name, dir));
}

void ModPipeline::ConvertTextureCaptures(const std::string& dump_dir,
                                         const std::string& out_dir) {
  // texture_dump_import.py va en "mod center hd/" en la release y en awo_tools/ en el proyecto.
  std::filesystem::path script = ProjectRoot() / "mod center hd" / "texture_dump_import.py";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(script, ec)) {
    script = ProjectRoot() / "awo_tools" / "texture_dump_import.py";
  }
  RunAsync(script, {dump_dir, out_dir, "--no-match", "--pack-names", "--solo-reemplazables", "--solo-nuevos"});
}

void ModPipeline::BuildTextures(const std::string& mod_name, int dest_slot,
                                const std::string& dir) {
  RunAsync(TextureScript(), BuildTextureArgs(mod_name, dest_slot, dir));
}

void ModPipeline::CreateCharacter(const NewCharacter& c) {
  if (c.mod.empty() || c.name.empty() || c.models.empty()) {
    AppendOutput("ERROR: faltan el nombre, la carpeta del mod o los modelos.\n");
    return;
  }
  std::vector<std::string> args = {"nuevo", "--mods", ModsOutDir().string(), "--mod", c.mod,
                                   "--nombre", c.name, "--donante", std::to_string(c.donor)};
  if (c.forms_per_costume > 1) {
    args.push_back("--por-traje");
    args.push_back(std::to_string(c.forms_per_costume));
  }
  if (c.slot >= 0) {
    args.push_back("--id");
    args.push_back(std::to_string(c.slot));
  }
  for (const auto& m : c.models) {
    args.push_back("--modelo");
    args.push_back(m);
  }
  if (!c.face.empty()) {
    args.push_back("--cara");
    args.push_back(c.face);
  }
  if (!c.portrait.empty()) {
    args.push_back("--retrato");
    args.push_back(c.portrait);
  }
  if (c.after >= 0) {
    args.push_back("--despues-de");
    args.push_back(std::to_string(c.after));
  }
  const std::string us = UsDir();
  if (!us.empty()) {
    args.push_back("--us");
    args.push_back(us);
  }
  RunAsync(RosterScript(), args);
}

void ModPipeline::PreviewCharacter(const std::string& mod, const std::vector<std::string>& extra) {
  std::vector<std::string> args = {"vista", "--mods", ModsOutDir().string(), "--mod", mod};
  const std::string us = UsDir();
  if (!us.empty()) {
    args.push_back("--us");
    args.push_back(us);
  }
  args.insert(args.end(), extra.begin(), extra.end());
  RunAsync(RosterScript(), args);
}

std::string ModPipeline::UsDir() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return afs_path_.empty() ? std::string() : std::filesystem::path(afs_path_).parent_path().string();
}

void ModPipeline::EditCapsules(const std::string& mod, const std::vector<std::string>& extra) {
  std::vector<std::string> args = {"capsulas", "--mods", ModsOutDir().string(), "--mod", mod};
  args.insert(args.end(), extra.begin(), extra.end());
  RunAsync(RosterScript(), args);
}

void ModPipeline::ImporterQuery(const std::vector<std::string>& args) {
  RunAsync(ImporterScript(), args);
}

void ModPipeline::ImportCharacter(const ImportRequest& r) {
  std::vector<std::string> args = {"importar", r.source, r.key, "--mod", r.mod, "--nombre", r.name,
                                   "--mods", ModsOutDir().string()};
  if (r.donor >= 0) {
    args.push_back("--donante");
    args.push_back(std::to_string(r.donor));
  }
  const std::string us = UsDir();
  if (!us.empty()) {
    args.push_back("--us");
    args.push_back(us);
  }
  RunAsync(ImporterScript(), args);
}

bool ModPipeline::RosterToolAvailable() {
  std::error_code ec;
  return std::filesystem::exists(RosterScript(), ec);
}

bool ModPipeline::ToolsInstalled() {
  std::error_code ec;
  return std::filesystem::exists(ImporterScript(), ec) && std::filesystem::exists(RosterScript(), ec);
}

void ModPipeline::ClearOutput() {
  std::lock_guard<std::mutex> lock(mutex_);
  output_.clear();
}

void ModPipeline::BuildRoster(bool force) {
  std::vector<std::string> args = {"construir", "--mods", ModsOutDir().string()};
  const std::string us = UsDir();
  if (!us.empty()) {
    args.push_back("--us");
    args.push_back(us);
  }
  if (force) args.push_back("--force");
  RunAsync(RosterScript(), args);
}

void ModPipeline::Wait() {
  if (worker_.joinable()) worker_.join();
}

std::filesystem::path ModPipeline::ModsDir() { return ModsOutDir(); }

bool ModPipeline::HasCharacterSources() {
  std::error_code ec;
  const auto dir = ModsOutDir();
  if (!std::filesystem::is_directory(dir, ec)) return false;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (e.is_directory(ec) && std::filesystem::exists(e.path() / "personaje.toml", ec) &&
        !std::filesystem::exists(e.path() / ".disabled", ec)) {
      return true;
    }
  }
  // fuentes desactivadas pero _roster generado: hay que regenerarlo (o borrarlo)
  return std::filesystem::exists(dir / "_roster", ec);
}

}  // namespace dbz3::launcher

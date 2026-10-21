#include <cctype>
#include <chrono>
#include <algorithm>
// dbz3 - Model swap pipeline integration (project-side).

#include "mod_pipeline.h"
#include "settings.h"
#include "../region.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#if REX_PLATFORM_WIN32
#include <windows.h>
#endif

#include <cerrno>
#include <ctime>
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
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

// G11: los paths que se pasan a Python y se escriben en el registro van SIEMPRE en UTF-8. Con
// path::string() Windows usa la pagina ANSI: un usuario con "Jose" con tilde (o cualquier carpeta no
// ASCII) hacia que Python recibiera otra ruta y no encontrara ni mods/ ni ps2_games/.
std::string Utf8(const std::filesystem::path& p) { return rex::path_to_utf8(p); }

std::string TrimCopy(std::string s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.erase(0, 1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
  return s;
}

// Lee `entrada` y `menu` del stage.toml de un escenario (mismo formato que src/escenarios.cpp).
bool StageEntries(const std::filesystem::path& file, int& entrada, int& menu) {
  std::ifstream in(file);
  if (!in.is_open()) return false;
  entrada = menu = -1;
  std::string line;
  while (std::getline(in, line)) {
    line = TrimCopy(std::move(line));
    if (line.empty() || line.front() == '#' || line.front() == '[') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = TrimCopy(line.substr(0, eq));
    std::string value = TrimCopy(line.substr(eq + 1));
    if (!value.empty() && value.front() == '"') {
      const size_t end = value.find('"', 1);
      value = value.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    } else if (const size_t hash = value.find('#'); hash != std::string::npos) {
      value.resize(hash);
      value = TrimCopy(std::move(value));
    }
    if (key == "entrada") entrada = std::atoi(value.c_str());
    else if (key == "menu") menu = std::atoi(value.c_str());
  }
  if (entrada < 0) return false;
  if (menu <= 0) menu = entrada + 100;   // escenarios_convertir.instalar usa entrada + 100
  return true;
}

// Un escenario auto-generado esta completo si estan sus dos bins (escenario y menu) y su manifiesto.
// El sello origen.txt solo dice "ya se intento"; no garantiza que la carpeta se copiara entera.
bool StageFolderComplete(const std::filesystem::path& dir, std::string* why) {
  auto fail = [why](const std::string& text) {
    if (why) *why = text;
    return false;
  };
  std::error_code ec;
  if (!std::filesystem::is_regular_file(dir / "stage.toml", ec)) return fail("falta stage.toml");
  int entrada = -1, menu = -1;
  if (!StageEntries(dir / "stage.toml", entrada, menu)) return fail("stage.toml sin entrada valida");
  if (!std::filesystem::is_regular_file(dir / "manifest.txt", ec)) return fail("falta manifest.txt");
  for (const int n : {entrada, menu}) {
    const auto geom = dir / "us" / "data_cmn.afs" / std::to_string(n) / "geom.bin";
    if (!std::filesystem::is_regular_file(geom, ec)) return fail("falta geom.bin de la entrada " + std::to_string(n));
  }
  return true;
}

int CountCompleteStages() {
  std::error_code ec;
  int count = 0;
  const auto root = rex::filesystem::GetExecutableFolder() / "mods";
  for (auto [it, iec] = std::pair{std::filesystem::directory_iterator(root, ec), std::error_code{}};
       !iec && it != std::filesystem::directory_iterator(); it.increment(iec)) {
    if (!it->is_directory(ec)) continue;
    // solo las auto-generadas: una carpeta de escenario hecha a mano no lleva origen.txt
    if (!std::filesystem::is_regular_file(it->path() / "origen.txt", ec)) continue;
    if (StageFolderComplete(it->path(), nullptr)) ++count;
  }
  return count;
}

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

std::filesystem::path StagesScript() {
  return ProjectRoot() / "mod center hd" / "campos_iw.py";
}

std::filesystem::path ImporterScript() {
  return ProjectRoot() / "mod center hd" / "importar.py";
}

std::filesystem::path CatalogFile() {
  return ProjectRoot() / "mod center hd" / "catalog_b3.cat";
}

std::filesystem::path ModsOutDir() {
  return rex::filesystem::GetExecutableFolder() / "mods";
}

         std::string Quote(const std::string& s) {
         #if REX_PLATFORM_WIN32
           std::string out = "\"";
           size_t slashes = 0;
           for (char c : s) {
             if (c == '\\') { ++slashes; continue; }
             if (c == '"') {
               out.append(slashes * 2 + 1, '\\');
             } else {
               out.append(slashes, '\\');
             }
             slashes = 0;
             out += c;
           }
           out.append(slashes * 2, '\\');
           out += '"';
           return out;
         #else
           std::string out = "'";
           for (char c : s) out += c == '\'' ? "'\\''" : std::string(1, c);
           return out + "'";
         #endif
         }

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
      v.push_back(Quote(Utf8(exe)));
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
  pid_t waited;
  do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
  return waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
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
    REXLOG_WARN("dbz3: catalogo B3 no encontrado en {}", Utf8(CatalogFile()));
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

float ModPipeline::Progress(std::string* what) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t at = output_.rfind("@progreso ");
  if (at == std::string::npos) {
    return -1.0f;
  }
  const size_t eol = output_.find('\n', at);
  const std::string line = output_.substr(at + 10, eol == std::string::npos ? std::string::npos : eol - at - 10);
  int k = 0, n = 0, used = 0;
  if (std::sscanf(line.c_str(), "%d/%d %n", &k, &n, &used) < 2 || n <= 0) {
    return -1.0f;
  }
  if (what) {
    *what = line.substr(size_t(used));
    while (!what->empty() && (what->back() == '\r' || what->back() == ' ')) what->pop_back();
  }
  return std::clamp(float(k) / float(n), 0.0f, 1.0f);
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
    stages_run_pending_ = false;   // G11: no dejar marcado un paso de escenarios que no llego a arrancar
    return;
  }
  exit_code_.store(-1);
  state_.store(JobState::kRunning);   // antes del hilo: nadie ve el resultado del trabajo anterior
  try {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    output_.clear();
    last_job_stages_ = stages_run_pending_;
    stages_run_pending_ = false;
  }
  if (worker_.joinable()) {
    worker_.join();
  }

  // "tail" = quoted script + quoted args. The Python interpreter itself is
  // resolved inside the worker with a real probe, so a bare "python" that
  // resolves to the Microsoft Store alias stub is skipped instead of failing
  // with exit code 9009 (issue #13).
  std::string tail = Quote(Utf8(script));
  for (const std::string& a : args) {
    // Escapar cada argumento: si contiene espacios, envolverlo en comillas
    // para que cmd.exe lo trate como un solo token (las rutas del proyecto
    // tienen espacios, p.ej. "...DBZ Budokai 3 HD Collection\...").
    tail += " " + Quote(a);
  }

  REXLOG_INFO("dbz3: mod pipeline: {}", tail);
  worker_ = std::thread([this, tail]() {
    // todos los caminos de salida (return temprano, excepcion) pasan por aqui: sin exit 0
    // explicito el trabajo queda kFailed
    struct Completion {
      std::atomic<int>& generation;
      std::atomic<bool>& running;
      std::atomic<int>& exit_code;
      std::atomic<JobState>& state;
      ~Completion() {
        state.store(exit_code.load() == 0 ? JobState::kOk : JobState::kFailed);
        generation.fetch_add(1);
        running.store(false);
      }
    } completion{generation_, running_, exit_code_, state_};
    try {
    const std::string python = ResolvePython();
    if (python.empty()) {
      AppendOutput(PythonMissingMessage());
      return;
    }
    // -X utf8: la salida de las herramientas llega en UTF-8 (tildes del informe sin romper)
    const std::string cmd = python + " -X utf8 " + tail;
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

    const DWORD wait_result = WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = DWORD(-1);
    if (wait_result != WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess, &rc) ||
        rc == STILL_ACTIVE) rc = DWORD(-1);
    exit_code_.store(rc == DWORD(-1) ? -1 : static_cast<int>(rc));
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

#else  // !REX_PLATFORM_WIN32
    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
      AppendOutput("ERROR: no se pudo crear el pipe para python.\n");
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
      return;
    }

    char buf[4096];
    ssize_t n = 0;
    bool output_ok = true;
    for (;;) {
      n = read(pipe_fds[0], buf, sizeof(buf));
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { output_ok = n == 0; break; }
      AppendOutput(std::string(buf, static_cast<size_t>(n)));
    }
    close(pipe_fds[0]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    const bool exited = waited == pid && WIFEXITED(status);
    exit_code_.store(exited && output_ok ? WEXITSTATUS(status) : -1);
    if (!exited || !output_ok || WEXITSTATUS(status) != 0) {
      AppendOutput("\n[exit code " +
                   std::to_string(ExitCode()) +
                   "]\n");
    }
    if (exited && WEXITSTATUS(status) == 127) {
      AppendOutput(PythonMissingMessage());
    } else if (Output().find("No module named") != std::string::npos) {
      AppendOutput(DependencyHintMessage());
    }

#endif  // REX_PLATFORM_WIN32
    } catch (const std::exception&) {
      exit_code_.store(-1);
      try { AppendOutput("ERROR: excepcion del trabajo; revisa el registro.\n"); } catch (...) {}
    } catch (...) { exit_code_.store(-1); }
  });
  } catch (...) {
    exit_code_.store(-1);
    state_.store(JobState::kFailed);
    generation_.fetch_add(1);
    running_.store(false);
    try { AppendOutput("ERROR: no se pudo iniciar el trabajo.\n"); } catch (...) {}
  }
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
                                   "--out", Utf8(ModsOutDir())};
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
                                   "--out", Utf8(ModsOutDir())};
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
                                   "--out", Utf8(ModsOutDir())};
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
  std::vector<std::string> args = {"nuevo", "--mods", Utf8(ModsOutDir()), "--mod", c.mod,
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
  std::vector<std::string> args = {"vista", "--mods", Utf8(ModsOutDir()), "--mod", mod};
  const std::string us = UsDir();
  if (!us.empty()) {
    args.push_back("--us");
    args.push_back(us);
  }
  args.insert(args.end(), extra.begin(), extra.end());
  RunAsync(RosterScript(), args);
}

std::string ModPipeline::UsDir() const {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!afs_path_.empty()) return rex::path_to_utf8(std::filesystem::u8path(afs_path_).parent_path());
  }
  // automatico: la carpeta de datos que monta el juego (la elegida en el launcher y su region).
  // Antes no se pasaba --us y roster_build buscaba junto al exe: con los datos del juego en otra
  // carpeta el montaje de personajes fallaba siempre.
  const std::filesystem::path root = dbz3::HostDataRoot();   // desde la ISO: la copia de sus .afs
  if (root.empty()) return {};
  const std::filesystem::path dir = root / (dbz3::settings::Region() == "eu" ? "eu" : "us");
  std::error_code ec;
  return std::filesystem::exists(dir / "data_cmn.afs", ec) ? rex::path_to_utf8(dir) : std::string();
}

void ModPipeline::EditCapsules(const std::string& mod, const std::vector<std::string>& extra) {
  std::vector<std::string> args = {"capsulas", "--mods", Utf8(ModsOutDir()), "--mod", mod};
  args.insert(args.end(), extra.begin(), extra.end());
  RunAsync(RosterScript(), args);
}

void ModPipeline::ImporterQuery(const std::vector<std::string>& args) {
  RunAsync(ImporterScript(), args);
}

void ModPipeline::RunOfficialPorts(const std::vector<std::string>& install, const std::vector<std::string>& remove) {
  if (IsRunning()) return;
  auto valido = [](const std::string& id) {   // id de receta: letras, cifras y _ (nunca una opcion ni una ruta)
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char ch) { return std::isalnum(ch) || ch == '_'; });
  };
  for (const auto* lista : {&install, &remove})
    for (const std::string& id : *lista)
      if (!valido(id)) return;
  ClearOutput();
  std::error_code ec;
  const std::filesystem::path root = ModsOutDir(), respaldo = root.parent_path() / "mods_respaldo";
  const std::string marca = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::system_clock::now().time_since_epoch()).count());
  for (const std::string& id : remove) {
    for (const std::string& dir : {id, id + ".disabled"}) {
      if (!std::filesystem::is_directory(root / dir, ec)) continue;
      std::filesystem::create_directories(respaldo, ec);
      std::filesystem::rename(root / dir, respaldo / (dir + "_" + marca), ec);
      if (ec) {
        REXLOG_WARN("dbz3: pack IW: no se pudo apartar {} ({})", id, ec.message());
        AppendOutput("ERROR: " + id + ": " + ec.message() + "\n");
        generation_.fetch_add(1);
        return;
      }
      REXLOG_INFO("dbz3: pack IW: {} apartado en mods_respaldo", id);
    }
  }
  if (install.empty()) {
    generation_.fetch_add(1);   // sin herramienta: este es el fin del trabajo para el launcher
    return;
  }
  std::vector<std::string> args = {"oficial"};
  args.insert(args.end(), install.begin(), install.end());
  args.push_back("--mods");
  args.push_back(Utf8(root));
  if (const std::string us = UsDir(); !us.empty()) {
    args.push_back("--us");
    args.push_back(us);
  }
  RunAsync(ImporterScript(), args);
}

void ModPipeline::ImportCharacter(const ImportRequest& r) {
  std::vector<std::string> args = {"importar", r.source, r.key, "--mod", r.mod, "--nombre", r.name,
                                   "--mods", Utf8(ModsOutDir())};
  if (r.donor >= 0) {
    args.push_back("--donante");
    args.push_back(std::to_string(r.donor));
  }
  if (!r.preset.empty()) {
    args.push_back("--preset");
    args.push_back(r.preset);
  }
  for (const auto& o : r.options) {
    args.push_back("--opcion");
    args.push_back(o);
  }
  const std::string us = UsDir();
  if (!us.empty()) {
    args.push_back("--us");
    args.push_back(us);
  }
  RunAsync(ImporterScript(), args);
}

void ModPipeline::CreateTeam(const std::vector<int>& members, const std::string& name) {
  std::vector<std::string> args = {"equipo", "--mods", Utf8(ModsOutDir()), "--miembros"};
  for (int m : members) args.push_back(std::to_string(m));
  if (!name.empty()) {
    args.push_back("--nombre");
    args.push_back(name);
  }
  const std::string us = UsDir();
  if (!us.empty()) {
    args.push_back("--us");
    args.push_back(us);
  }
  RunAsync(RosterScript(), args);
}

bool ModPipeline::RosterToolAvailable() {
  std::error_code ec;
  return std::filesystem::exists(RosterScript(), ec);
}

bool ModPipeline::RosterArtifactsOk() {
  if (!HasCharacterSources()) return true;   // roster_build retira _roster y sale 0
  std::error_code ec;
  const auto r = ModsOutDir() / "_roster";
  for (const char* f : {"roster.toml", "manifest.txt", "build.hash"}) {   // = validate_roster
    if (!std::filesystem::is_regular_file(r / f, ec)) return false;
  }
  return std::filesystem::file_size(r / "build.hash", ec) > 0 && !ec;
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
  std::vector<std::string> args = {"construir", "--mods", Utf8(ModsOutDir())};
  const std::string us = UsDir();
  if (!us.empty()) {
    args.push_back("--us");
    args.push_back(us);
  }
  if (force) args.push_back("--force");
  RunAsync(RosterScript(), args);
}

bool ModPipeline::StagesToolAvailable() {
  std::error_code ec;
  return std::filesystem::exists(StagesScript(), ec);
}

std::filesystem::path ModPipeline::StagesRegistryPath() {
  return rex::filesystem::GetExecutableFolder() / "mods" / "_escenarios_ps2.txt";
}

void ModPipeline::SetStagesReport(int created, int failed, const std::string& reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  StageReport report;
  report.ran = true;
  report.created = created >= 0 ? created : CountCompleteStages();
  report.failed = failed;
  report.reason = reason;
  report.up_to_date = failed == 0;
  stages_report_ = report;
  stages_report_loaded_ = true;
  stages_parsed_ = true;
  stages_parsed_output_ = output_;   // que no se reinterpreta una salida vieja del roster/swap
  WriteStagesRegistryLocked(stages_report_);
}

void ModPipeline::WriteStagesRegistryLocked(const StageReport& report) {
  std::error_code ec;
  const auto path = StagesRegistryPath();
  std::filesystem::create_directories(path.parent_path(), ec);
  auto partial = path;
  partial += ".tmp";
  {
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "# dbz3: ultimo paso de escenarios de PS2 (lo escribe el launcher)\n";
    out << "version=1\n";
    out << "creados=" << report.created << "\n";
    out << "fallaron=" << report.failed << "\n";
    out << "al_dia=" << (report.up_to_date ? 1 : 0) << "\n";
    std::string reason = report.reason;
    for (char& c : reason)
      if (c == '\r' || c == '\n' || c == '=') c = ' ';
    out << "motivo=" << reason << "\n";
    out << "fecha=" << std::time(nullptr) << "\n";
  }
  std::filesystem::rename(partial, path, ec);
  if (ec) {   // otro proceso lo tiene abierto: se conserva el .tmp (nada se borra)
    REXLOG_WARN("dbz3: escenarios de PS2: no se pudo publicar el registro {} ({})", Utf8(path), ec.message());
    ec.clear();
  }
}

void ModPipeline::LoadStagesRegistryLocked() {
  std::ifstream in(StagesRegistryPath());
  if (!in.is_open()) return;
  StageReport report;
  std::string line;
  while (std::getline(in, line)) {
    line = TrimCopy(std::move(line));
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = TrimCopy(line.substr(0, eq));
    const std::string value = TrimCopy(line.substr(eq + 1));
    if (key == "creados") report.created = std::atoi(value.c_str());
    else if (key == "fallaron") report.failed = std::atoi(value.c_str());
    else if (key == "al_dia") report.up_to_date = value == "1";
    else if (key == "motivo") report.reason = value;
  }
  report.ran = true;
  report.created = CountCompleteStages();   // el disco manda: una carpeta borrada no se cuenta
  stages_report_ = std::move(report);
}

void ModPipeline::ParseStagesOutputLocked() {
  if (running_ || !last_job_stages_) return;
  if (stages_parsed_ && stages_parsed_output_ == output_) return;
  stages_parsed_ = true;
  stages_parsed_output_ = output_;
  StageReport report;
  report.ran = true;
  int made = 0;
  std::istringstream lines(output_);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    constexpr std::string_view prefix = "escenarios: ";
    if (line.compare(0, prefix.size(), prefix) != 0) continue;
    const std::string msg = line.substr(prefix.size());
    if (msg.rfind("creado ", 0) == 0) {
      ++made;
    } else if (msg.rfind("fallo ", 0) == 0 || msg.rfind("sin ", 0) == 0) {
      ++report.failed;
      if (report.reason.empty()) report.reason = msg;
    } else if (msg.rfind("al dia", 0) == 0) {
      report.up_to_date = true;
    }
  }
  report.created = CountCompleteStages();
  if (made > report.created) report.created = made;
  if (report.failed == 0 && state_.load() == JobState::kFailed) {
    report.failed = 1;
    report.reason = "el conversor termino con error (codigo " + std::to_string(exit_code_.load()) + ")";
  }
  if (report.failed == 0 && output_.empty()) {
    report.failed = 1;
    report.reason = "sin salida del conversor (revisa el registro)";
  }
  if (report.reason.size() > 180) report.reason.resize(180);
  stages_report_ = std::move(report);
  stages_report_loaded_ = true;
  WriteStagesRegistryLocked(stages_report_);
}

ModPipeline::StageReport ModPipeline::StagesReport() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stages_report_loaded_) LoadStagesRegistryLocked();
  if (!running_) ParseStagesOutputLocked();
  return stages_report_;
}

int ModPipeline::PruneIncompleteStages() {
  std::error_code ec;
  const auto root = ModsOutDir();
  const auto backup = root.parent_path() / "mods_respaldo";
  const std::string stamp = std::to_string(std::time(nullptr));
  int moved = 0;
  for (auto [it, iec] = std::pair{std::filesystem::directory_iterator(root, ec), std::error_code{}};
       !iec && it != std::filesystem::directory_iterator(); it.increment(iec)) {
    if (!it->is_directory(ec)) continue;
    const auto dir = it->path();
    if (!std::filesystem::is_regular_file(dir / "origen.txt", ec)) continue;   // solo auto-generados
    std::string why;
    if (StageFolderComplete(dir, &why)) continue;
    std::filesystem::create_directories(backup, ec);
    const auto dst = backup / (Utf8(dir.filename()) + "_medias_" + stamp);
    std::filesystem::rename(dir, dst, ec);
    if (ec) {
      REXLOG_WARN("dbz3: escenarios de PS2: no se pudo apartar la carpeta a medias {} ({})", Utf8(dir), ec.message());
      ec.clear();
      continue;
    }
    ++moved;
    REXLOG_INFO("dbz3: escenarios de PS2: carpeta a medias '{}' apartada a {} ({}); se volvera a crear",
                Utf8(dir.filename()), Utf8(dst), why);
  }
  return moved;
}

void ModPipeline::BuildStages() {
  // ps2_games como el juego (pasarela_b1.cpp BuscarIsoB1): DBZ3_PS2_GAMES (pruebas) o la carpeta de los ajustes.
  // Si los ajustes cayeron a Documentos (instalacion no escribible), tambien se mira junto al exe y en user_data:
  // antes solo se miraba UserSettingsPath().parent_path() y el paso se omitia en silencio.
  std::vector<std::filesystem::path> candidates;
  if (const char* env = std::getenv("DBZ3_PS2_GAMES"); env && *env) {
    candidates.push_back(std::filesystem::u8path(env));
  } else {
    candidates.push_back(dbz3::settings::UserSettingsPath().parent_path() / "ps2_games");
    candidates.push_back(rex::filesystem::GetExecutableFolder() / "ps2_games");
    candidates.push_back(dbz3::settings::UserDataRoot() / "ps2_games");
    candidates.push_back(ProjectRoot() / "ps2_games");
  }
  std::error_code ec;
  std::filesystem::path ps2;
  for (const auto& candidate : candidates) {
    if (std::filesystem::is_directory(candidate, ec)) {
      ps2 = candidate;
      break;
    }
  }
  const std::string us = UsDir();
  if (us.empty()) {
    const int created = CountCompleteStages();
    SetStagesReport(created, 1,
                    "no encuentro los datos de Budokai 3 HD (elige la carpeta/ISO del juego en la pestana Juego)");
    REXLOG_INFO("dbz3: escenarios de PS2: omitido (sin datos del juego)");
    return;
  }
  if (ps2.empty()) {
    std::string where;
    for (const auto& candidate : candidates) where += (where.empty() ? "" : ", ") + Utf8(candidate);
    const int created = CountCompleteStages();
    SetStagesReport(created, 1,
                    "no encuentro la carpeta ps2_games (elige las ISO de Budokai 2/Infinite World en la pestana Juego)");
    REXLOG_INFO("dbz3: escenarios de PS2: omitido (ps2_games no existe; buscado en {})", where);
    return;
  }
  if (!StagesToolAvailable()) {
    const int created = CountCompleteStages();
    SetStagesReport(created, 1, "falta 'mod center hd/campos_iw.py' (Kit de modding) junto al juego");
    REXLOG_INFO("dbz3: escenarios de PS2: omitido (sin campos_iw.py)");
    return;
  }
  if (IsRunning()) {
    SetStagesReport(-1, 1, "otra tarea de mods sigue en marcha");
    return;
  }
  const int pruned = PruneIncompleteStages();
  if (pruned > 0) REXLOG_INFO("dbz3: escenarios de PS2: {} carpetas a medias apartadas a mods_respaldo", pruned);
  stages_run_pending_ = true;
  RunAsync(StagesScript(), {"auto", "--mods", Utf8(ModsOutDir()), "--us", us, "--ps2", Utf8(ps2)});
}

void ModPipeline::Wait() {
  if (worker_.joinable()) worker_.join();
}

std::filesystem::path ModPipeline::ModsDir() { return ModsOutDir(); }

bool ModPipeline::HasCharacterSources() {
  std::error_code ec;
  const auto dir = ModsOutDir();
  if (!std::filesystem::is_directory(dir, ec)) return false;
  for (auto [it_1, iec_1] = std::pair{std::filesystem::directory_iterator(dir, ec), std::error_code{}}; !iec_1 && it_1 != std::filesystem::directory_iterator(); it_1.increment(iec_1)) {
    const auto& e = *it_1;
    if (e.is_directory(ec) &&
        (std::filesystem::exists(e.path() / "personaje.toml", ec) ||
         std::filesystem::exists(e.path() / "traje.toml", ec)) &&
        !std::filesystem::exists(e.path() / ".disabled", ec)) {
      return true;
    }
  }
  // fuentes desactivadas pero _roster generado: hay que regenerarlo (o borrarlo)
  return std::filesystem::exists(dir / "_roster", ec);
}

}  // namespace dbz3::launcher

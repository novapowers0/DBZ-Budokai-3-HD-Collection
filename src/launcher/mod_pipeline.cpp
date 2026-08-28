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

std::string PythonExecutable() {
  if (const char* py = std::getenv("DBZ3_PYTHON"); py && *py) {
    return py;
  }
  return "python";
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

  std::string cmd = Quote(PythonExecutable()) + " " + Quote(script.string());
  for (const std::string& a : args) {
    // Escapar cada argumento: si contiene espacios, envolverlo en comillas
    // para que cmd.exe lo trate como un solo token (las rutas del proyecto
    // tienen espacios, p.ej. "...DBZ Budokai 3 HD Collection\...").
    if (a.find(' ') != std::string::npos || a.find('\t') != std::string::npos) {
      cmd += " " + Quote(a);
    } else {
      cmd += " " + a;
    }
  }

  // Diagnostico temporal: volcar el comando a un log para depurar el error de
  // "sintaxis de la etiqueta del volumen".
  {
    std::ofstream dbg(rex::filesystem::GetExecutableFolder() / "pipeline_cmd.log",
                      std::ios::app);
    dbg << "CMD: " << cmd << "\n";
  }

  worker_ = std::thread([this, cmd]() {
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
    while (ReadFile(hOutRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
      buf[n] = '\0';
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
    running_.store(false);
#else  // !REX_PLATFORM_WIN32
    // Portable path not wired up yet: report a clear error instead of failing
    // to link (the SDK spawn helper for posix is a follow-up for the Linux port).
    AppendOutput("ERROR: ejecutar el pipeline de modding no esta soportado en "
                 "esta plataforma todavia.\n");
    running_.store(false);
#endif  // REX_PLATFORM_WIN32
  });
}

void ModPipeline::SwapB3ToB3(const B3Char& src, const B3Char& dst) {
  if (src.bin == 0 || dst.bin == 0) {
    AppendOutput("ERROR: el personaje origen/destino no tiene bin asignado.\n");
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

void ModPipeline::BuildTextures(const std::string& mod_name, int dest_slot,
                                const std::string& dir) {
  RunAsync(TextureScript(), BuildTextureArgs(mod_name, dest_slot, dir));
}

}  // namespace dbz3::launcher
#include "datos.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <toml++/toml.hpp>

namespace dbz3::datos {
namespace {

std::mutex g_m;
std::map<std::string, toml::table> g_ficheros;   // nombre sin extension -> tabla
std::map<std::string, std::filesystem::file_time_type> g_mtimes;
std::atomic<bool> g_iniciado{false};
std::atomic<bool> g_parar{false};
std::thread g_hilo;   // vigilante de pruebas (solo con DBZ3_NO_DIALOGS); Detener() lo para

std::filesystem::path Carpeta() { return rex::filesystem::GetExecutableFolder() / "res" / "datos"; }

const toml::node* Buscar(const toml::table& t, std::string ruta) {
  const toml::node* n = &t;
  size_t i = 0;
  while (n && i <= ruta.size()) {
    const size_t j = ruta.find('.', i);
    const std::string k = ruta.substr(i, j == std::string::npos ? std::string::npos : j - i);
    const toml::table* tab = n->as_table();
    if (!tab) return nullptr;
    n = tab->get(k);
    if (j == std::string::npos) break;
    i = j + 1;
  }
  return n;
}

// PRUEBA: DBZ3_TEST_DATOS="fichero:ruta[,fichero:ruta...]" escribe en el log el valor vigente tras cada carga, para
// comprobar sin imagen que un cambio en res/datos llega al juego (tools/pruebas.py escenario datos_recarga).
void LogPrueba() {
  const char* e = std::getenv("DBZ3_TEST_DATOS");
  if (!e) return;
  std::string s = e;
  for (size_t i = 0; i < s.size();) {
    const size_t j = s.find(',', i);
    const std::string par = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
    const size_t c = par.find(':');
    if (c != std::string::npos) {
      const std::string f = par.substr(0, c), r = par.substr(c + 1);
      auto it = g_ficheros.find(f);
      const toml::node* n = it == g_ficheros.end() ? nullptr : Buscar(it->second, r);
      if (n && n->is_number()) {
        REXLOG_INFO("dbz3 datos: prueba: {} {} = {}", f, r, n->value_or(0.0));
      } else if (n && n->is_string()) {
        REXLOG_INFO("dbz3 datos: prueba: {} {} = {}", f, r, n->value_or(std::string()));
      } else {
        REXLOG_INFO("dbz3 datos: prueba: {} {} = (sin valor)", f, r);
      }
    }
    if (j == std::string::npos) break;
    i = j + 1;
  }
}

int Cargar() {
  std::map<std::string, toml::table> nuevo;
  std::map<std::string, std::filesystem::file_time_type> mt;
  std::map<std::string, toml::table> anterior;
  {
    std::lock_guard<std::mutex> lk(g_m);   // copia bajo el candado: el fallback de un fichero roto no lee g_ficheros suelto
    anterior = g_ficheros;
  }
  std::error_code ec;
  int n = 0;
  for (const auto& e : std::filesystem::directory_iterator(Carpeta(), ec)) {
    if (e.path().extension() != ".toml") continue;
    mt[e.path().stem().string()] = e.last_write_time(ec);   // tambien si falla: no recargar en bucle un fichero roto
    try {
      nuevo[e.path().stem().string()] = toml::parse_file(e.path().string());
      ++n;
    } catch (const toml::parse_error& err) {
      REXLOG_WARN("dbz3 datos: {} no se pudo leer ({}): se queda el valor anterior", e.path().filename().string(),
                  std::string(err.description()));
      auto it = anterior.find(e.path().stem().string());
      if (it != anterior.end()) nuevo[e.path().stem().string()] = it->second;
    }
  }
  std::lock_guard<std::mutex> lk(g_m);
  g_ficheros = std::move(nuevo);
  g_mtimes = std::move(mt);
  REXLOG_INFO("dbz3 datos: {} ficheros cargados de {}", n, Carpeta().string());
  LogPrueba();
  return n;
}

bool Cambio() {
  std::error_code ec;
  std::map<std::string, std::filesystem::file_time_type> ahora;
  for (const auto& e : std::filesystem::directory_iterator(Carpeta(), ec)) {
    if (e.path().extension() == ".toml") ahora[e.path().stem().string()] = e.last_write_time(ec);
  }
  std::lock_guard<std::mutex> lk(g_m);
  return ahora != g_mtimes;
}

}  // namespace

double Num(const char* fichero, const char* ruta, double def) {
  std::lock_guard<std::mutex> lk(g_m);
  auto it = g_ficheros.find(fichero);
  if (it == g_ficheros.end()) return def;
  const toml::node* n = Buscar(it->second, ruta);
  return n && n->is_number() ? n->value_or(def) : def;
}

std::string Str(const char* fichero, const char* ruta, const char* def) {
  std::lock_guard<std::mutex> lk(g_m);
  auto it = g_ficheros.find(fichero);
  if (it == g_ficheros.end()) return def;
  const toml::node* n = Buscar(it->second, ruta);
  return n && n->is_string() ? n->value_or(std::string(def)) : std::string(def);
}

bool Existe(const char* fichero, const char* ruta) {
  std::lock_guard<std::mutex> lk(g_m);
  auto it = g_ficheros.find(fichero);
  return it != g_ficheros.end() && Buscar(it->second, ruta) != nullptr;
}

int Recargar() { return Cargar(); }

void Detener() {
  g_parar = true;
  if (g_hilo.joinable()) g_hilo.join();
}

void Iniciar() {
  if (g_iniciado.exchange(true)) return;
  Cargar();
  if (std::getenv("DBZ3_NO_DIALOGS")) {   // solo pruebas automaticas: recarga sola al cambiar un fichero
    std::atexit(Detener);
    g_hilo = std::thread([] {
      while (!g_parar) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::error_code ec;   // "F5" de pruebas: tools/pruebas.py escribe dbz3_datos.req junto al exe
        const auto req = rex::filesystem::GetExecutableFolder() / "dbz3_datos.req";
        if (std::filesystem::remove(req, ec)) {
          REXLOG_INFO("dbz3 datos: F5: {} ficheros recargados", Cargar());
          continue;
        }
        if (Cambio()) {
          REXLOG_INFO("dbz3 datos: cambio detectado en res/datos: recargando");
          Cargar();
        }
      }
    });
  }
}

}  // namespace dbz3::datos

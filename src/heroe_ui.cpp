// dbz3 - Modo Heroe: pantallas Create / Edit y Select Master al estilo Ultimate Tenkaichi (ver heroe_ui.h).
// Referencia: studies 7\10_heroe_rework_ut.md ("Referencia visual de UT": vista previa a la izquierda, lista a la
// derecha, Part 1-4 con Hue/Saturation/Brightness, Logo Mark, Scouter, botones Confirm/Back/Reset y la linea de ayuda;
// Select Master con retrato, "Training Completion %", modificadores y la regla del 60 %). Estilo: el de los menus de
// Budokai 3 (dbz3::ingame::style, marcos naranjas, letra turquesa), como el resto de pantallas del HUB.
#include "reloj_prueba.h"
#include "heroe_ui.h"
#include "lim3_limits.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
using DWORD = unsigned long;   // Linux: Lanzar va por std::system
#endif

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/input/input_system.h>   // HEROE-V05: gatillos LT/RT (girar) en el creador nuevo
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cac/cac.h"
#include "cac/arcosiano_ui.h"
#include "cac/cac_creador_util.h"
#include "cac/razas_b3_ui.h"
#include "escenarios.h"
#include "heroe_api.h"
#include "heroe_rutas.h"
#include "heroe_v2_casa.h"
#include "heroe_ui_v2.h"   // HEROE-V04: kit del aspecto nuevo (solo con dbz3_heroe_ui_nueva)
namespace dbz3::heroe { bool NaveEnMapa(); }   // heroe.cpp (PM-37: nave de Freezer en el mapa de Namek)

namespace dbz3::modos { void AsegurarPanel(); }   // modos.cpp (HEROE-V05)
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"
#include "modos.h"
#include "ingame/native_ui.h"
#include "mods.h"
#include "ps2_modelo.h"   // ps2::Escena: el pintor 3D con ImGui de los modelos de IW/B2 (toon + borde)

namespace dbz3::heroe { dbz3::modos::Reglas Reglas(int estilo, int extra, int traje); }   // heroe.cpp (HEROE-TEC: Probar)

namespace dbz3::heroe_ui {
void VistaTecnicaPedir(int cap);   // R23 HER-07 (al final del archivo)
namespace {
namespace S = dbz3::ingame::style;
namespace D = S::mode;
namespace H = dbz3::heroe;
namespace M = dbz3::modos;
namespace T = dbz3::i18n;
namespace C = dbz3::cac;
namespace fs = std::filesystem;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kLb = 0x0100, kRb = 0x0200,
                   kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;

REXCVAR_DEFINE_BOOL(dbz3_lim3_vista_malla, false, "DBZ3/Mods",
                    "LIM3: admitir hasta 400000 triangulos en HMV1 (solo lectura)");
REXCVAR_DEFINE_BOOL(dbz3_lim3_vista_vertices, false, "DBZ3/Mods",
                    "LIM3: hasta 480000 vertices de vista 3D con VtxOffset");

REXCVAR_DEFINE_BOOL(dbz3_lim_ani2, false, "DBZ3/Dev",
                    "R23 LIM: aceptar animaciones ANI2 en la vista previa del heroe");

// HER01 (1.5.7): boton Importar receta en el creador. Apagado de serie: se enciende en Avanzado (quick_settings).
REXCVAR_DEFINE_BOOL(dbz3_heroe_probar_aislada, false, "DBZ3/Mods",
                    "Hero v2: practice only the selected base attack, without automatic preview");

REXCVAR_DEFINE_BOOL(dbz3_heroe_importar, false, "DBZ3/Mods",
                    "Boton Importar en el creador del Heroe: rehace un personaje desde receta.toml (experimental)");
// HEROE-02 (11/11): tras exportar, la ayuda lista las tecnicas que no viajan (de otro estilo) con su duenyo. Apagado de serie.
REXCVAR_DEFINE_BOOL(dbz3_heroe_exportar_lista, false, "DBZ3/Mods",
                    "Exportar Heroe: lista las tecnicas de otro estilo que no viajan al personaje exportado (experimental)");
// HEROE-V04 (11/11): aspecto nuevo de las pantallas del Heroe (maquetas HEROE_VISUAL; kit heroe_ui_v2.h). Apagado de serie:
// apagado, se ejecuta exactamente el dibujo de antes.
REXCVAR_DEFINE_BOOL(dbz3_heroe_ui_nueva, true, "DBZ3/Mods",
                    "Modo Heroe: aspecto nuevo de las pantallas (personaje grande, un panel, ayuda unica) (experimental)");
// Rediseno del Modo Heroe (DISENO.md de heroe_rediseno): Habitacion (maqueta 2) y Evolucion por raza (maqueta 4). Apagados de serie.
REXCVAR_DEFINE_BOOL(dbz3_heroe_habitacion, true, "DBZ3/Mods",
                    "Modo Heroe: pantalla Habitacion (casa del heroe: aspecto, evolucion, maestros, tecnicas y ficha) (experimental)");
REXCVAR_DEFINE_BOOL(dbz3_heroe_evolucion, true, "DBZ3/Mods",
                    "Modo Heroe: Evolucion por raza (arbol de formas segun el camino elegido) (experimental)");
// CAC-15 (R23): via «Libre» del creador: 4 filas «Capa 1-4» tras Accesorio (parte entera de un luchador nativo, sobre el cuerpo).
REXCVAR_DEFINE_BOOL(dbz3_heroe_capas, true, "DBZ3/Mods",
                    "Creador del Heroe: filas Capa 1-4 (pelo, torso, armadura o accesorios enteros de otro luchador sobre el cuerpo) (experimental)");
REXCVAR_DEFINE_BOOL(dbz3_heroe_tecnicas_v2, true, "DBZ3/Mods",   // HEROE-TEC (maqueta 6): heroe_v2_tecnicas.inl + heroe_tecnicas_v2.inl
                    "Modo Heroe: Tecnicas v2 (huecos de golpes/especiales/definitiva/esquiva, aprender con los maestros, probar) (experimental)");

std::string Num(int n) { return std::to_string(n); }
// Rutas con acentos: std::filesystem::path::string() usa la pagina ANSI en Windows; u8string() es UTF-8.
std::string Utf8(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

// ---------------------------------------------------------------------------------------------------------------------
// Vista previa: tira de N giros (vista_heroe.py) -> textura. Se relee si el fichero cambia (regenerada al guardar).
// ---------------------------------------------------------------------------------------------------------------------
fs::path Juego() { return dbz3::ModsRoot().parent_path(); }
// Las vistas se reconstruyen; no son partidas. Una instalacion protegida no debe impedir ver el heroe.
fs::path Vistas() {
  static const fs::path ruta = [] {
    const fs::path normal = Juego() / "ingame_ui" / "heroe_vista";
    const fs::path usuario = dbz3::settings::UserDataRoot() / "heroe_vista";
    for (const auto& p : {normal, usuario}) {
      std::error_code ec;
      fs::create_directories(p, ec);
      std::ofstream prueba(p / "generacion.log", std::ios::app);
      if (!ec && prueba) return p;
    }
    return usuario;   // el fallo de escritura se explica en GenerarCon, sin excepcion en el hilo de dibujo
  }();
  return ruta;
}
fs::path VistaPng(int id, int traje) {
  return Vistas() / (Num(id) + "_" + Num(traje) + ".png");
}
struct Tira {
  std::unique_ptr<rex::ui::ImmediateTexture> tex;
  S::Pic pic;
  fs::file_time_type cuando{};
  double visto = -1e9;
};
std::map<std::string, Tira> g_tiras;
// HEROE-04 (P13): cada edicion de la ficha es una clave nueva (hash de los valores) con su tira o su malla (~20 MB de
// texturas, 101 en una ficha medida); sin podar, las vistas de todas las ediciones seguian vivas en la GPU hasta
// agotar la memoria de subida ("Failed to get a buffer" del SDK). Se sueltan las que nadie ha mirado en 5 s
// (las que se pintan refrescan `visto` cada segundo como mucho).
template <class Mapa>
void Podar(Mapa& mapa, double ahora) {
  for (auto it = mapa.begin(); it != mapa.end();) it = ahora - it->second.visto > 5.0 ? mapa.erase(it) : std::next(it);
}
std::string Vivas();   // contador para el registro: tiras, mallas y texturas de vista previa vivas
std::mutex g_gen_mu;
std::set<std::string> g_generando, g_fallidas;   // con g_gen_mu
enum class ErrorModelo { kPython, kPaquete, kDatos, kEscritura, kGeneracion, kTorsoIncompatible };
std::map<std::string, ErrorModelo> g_error_modelo;   // con g_gen_mu

bool Lanzar(const std::wstring& cmd, DWORD* code, DWORD limite_ms = 180000, const fs::path& registro = {}) {
#if defined(_WIN32)
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  HANDLE log = INVALID_HANDLE_VALUE;
  if (!registro.empty()) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    log = CreateFileW(registro.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return false;
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = log;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  const bool lanzado = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, log != INVALID_HANDLE_VALUE,
                                    CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
  if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
  if (!lanzado)
    return false;
  *code = 1;
  if (WaitForSingleObject(pi.hProcess, limite_ms) == WAIT_OBJECT_0) {
    GetExitCodeProcess(pi.hProcess, code);
  } else {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    *code = 1460;   // tiempo agotado
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
#else
  const std::wstring completa = registro.empty() ? cmd : cmd + L" > \"" + registro.wstring() + L"\" 2>&1";
  const int r = std::system(fs::path(completa).string().c_str());   // sin consola que ocultar
  *code = r == 0 ? 0 : 1;
  return r != -1;
#endif
}

// python + vista_heroe.py --solo id_traje en un hilo (sin consola). Interpretes: DBZ3_PYTHON, python/ junto al exe
// (el del Kit de Modding), py -3, python (como mod_pipeline.cpp; el alias de la Store da 9009 y se salta).
// args: argumentos de vista_heroe.py tras --juego; hecho: fichero que debe existir al acabar.
void GenerarCon(const std::string& clave, const std::wstring& args, const fs::path& hecho,
                const char* script_py = "vista_heroe.py", const std::string& ficha = {});
void Generar(int id, int traje) {
  if (H::CuerpoPropio(id)) return;   // G06a: MallaFicha genera el cuerpo UT con sus materiales (C4: y el de piezas XV2)
  const std::string clave = Num(id) + "_" + Num(traje);
  GenerarCon(clave, L"--solo " + fs::path(clave).wstring(), VistaPng(id, traje));
}
std::atomic<bool> g_montaje_fallo{false};   // el ultimo montaje del heroe (combate_*) fallo: aviso al jugador
std::mutex g_ut_mu, g_ut_turno;   // C4: ver GenerarCon
std::string g_ultimo_ut;
void GenerarCon(const std::string& clave, const std::wstring& args, const fs::path& hecho, const char* script_py, const std::string& ficha) {
  {
    std::lock_guard<std::mutex> lk(g_gen_mu);
    if (g_generando.count(clave) || g_fallidas.count(clave)) return;
    g_generando.insert(clave);
  }
  // C4: las vistas previas de la ficha (ut_*) se hacen de una en una y manda la ULTIMA pedida: al cambiar de pieza varias veces
  // seguidas, las intermedias se saltan (antes: un Python por cambio, 15-25 s cada uno y todos a la vez). El resultado queda
  // en cache por combinacion (el nombre del fichero lleva el hash de la ficha).
  const bool es_ficha = clave.rfind("ut_", 0) == 0;
  if (es_ficha) {
    std::lock_guard<std::mutex> lk(g_ut_mu);
    g_ultimo_ut = clave;
  }
  std::thread([clave, args, hecho, script_py, es_ficha, ficha] {
    std::unique_lock<std::mutex> turno(g_ut_turno, std::defer_lock);
    if (es_ficha) {
      turno.lock();   // una a la vez
      bool vigente;
      {
        std::lock_guard<std::mutex> lk(g_ut_mu);
        vigente = g_ultimo_ut == clave;
      }
      std::error_code ec0;
      if (!vigente || fs::exists(hecho, ec0)) {   // otra pedida despues, o ya hecha: se salta (sin marcarla como fallida)
        std::lock_guard<std::mutex> lk(g_gen_mu);
        g_generando.erase(clave);
        return;
      }
    }
    const fs::path script = Juego() / "mod center hd" / "cac" / script_py;
    std::error_code ec;
    bool ok = false;
    ErrorModelo error = ErrorModelo::kPython;
    const fs::path registro = Vistas() / (clave + ".log");
    auto leer_registro = [](const fs::path& ruta) {
      std::ifstream entrada(ruta, std::ios::binary | std::ios::ate);
      const auto longitud = entrada ? std::streamoff(entrada.tellg()) : 0;
      entrada.seekg(std::max(std::streamoff(0), longitud - 16384));
      return std::string((std::istreambuf_iterator<char>(entrada)), std::istreambuf_iterator<char>());
    };
    std::string diagnostico;
    std::ofstream prueba(registro, std::ios::app);
    prueba.close();
    bool escribible = !prueba.fail();
    const fs::path entrada = Vistas() / (clave + ".ficha.txt");
    if (escribible && !ficha.empty()) {
      std::ofstream f(entrada, std::ios::binary | std::ios::trunc);
      f.write(ficha.data(), std::streamsize(ficha.size()));
      f.close();
      escribible = !f.fail();
    }
    if (!escribible) {
      error = ErrorModelo::kEscritura;
    } else if (!fs::exists(script, ec)) {
      error = ErrorModelo::kPaquete;
      REXLOG_WARN("dbz3 heroe: vista previa {}: falta {} (se queda el retrato)", clave, script.string());
    } else {
      std::vector<std::wstring> pys;
      if (const char* e = std::getenv("DBZ3_PYTHON"); e && *e) pys.push_back(L"\"" + fs::u8path(e).wstring() + L"\"");
      const fs::path port = Juego() / "python" / "python.exe";
      if (fs::exists(port, ec)) pys.push_back(L"\"" + port.wstring() + L"\"");
#if defined(_WIN32)
      pys.push_back(L"py -3");
#else
      pys.push_back(L"python3");
#endif
      pys.push_back(L"python");
      if (const char* e = std::getenv("DBZ3_TEST_SIN_PYTHON"); e && *e == '1') pys.clear();   // prueba: PC sin Python
      const auto t0 = std::chrono::steady_clock::now();
      for (const std::wstring& py : pys) {
        const std::wstring cmd =
            py + L" -X utf8 \"" + script.wstring() + L"\" --juego \"" + Juego().wstring() + L"\" " + args +
            (!ficha.empty() ? L" --ficha-archivo \"" + entrada.wstring() + L"\"" : L"") +
            (std::string(script_py) == "vista_heroe.py" ? L" --salida \"" + Vistas().wstring() + L"\" --progreso \"" +
                                                              (Vistas() / (clave + ".progreso")).wstring() + L"\"" : L"");
        DWORD code = 1;
        // El montaje del heroe (cuerpo, 3 historias y rotulo) tarda 112 s medidos en un PC rapido y va en este mismo proceso: con el limite de
        // 180 s de las vistas previas, un PC algo mas lento lo mataba a medias y se quedaba sin historia o sin nombre.
        if (!Lanzar(cmd, &code, clave.rfind("combate_", 0) == 0 || clave.rfind("exportar_", 0) == 0 ||
                                     clave.rfind("importar_", 0) == 0 ? 1200000 : 180000, registro)) continue;
        if (code == 9009) continue;   // alias de la Microsoft Store
        ok = code == 0 && fs::exists(hecho, ec);
        std::string detalle = leer_registro(registro);
        if (!ok && detalle.empty() && (clave.rfind("combate_", 0) == 0 || clave.rfind("exportar_", 0) == 0 ||
                                       clave.rfind("importar_", 0) == 0))
          detalle = leer_registro(Vistas() / "montaje.log");
        if (!detalle.empty()) diagnostico = detalle;
        if (!ok) {
          error = ErrorModelo::kGeneracion;
          if (detalle.find("ModuleNotFoundError") != std::string::npos || detalle.find("ImportError") != std::string::npos ||
              detalle.find("SyntaxError") != std::string::npos) {
            error = ErrorModelo::kPython;
            continue;   // un Python del sistema incompleto no debe impedir probar el siguiente interprete
          }
          if (detalle.find("NoGameData") != std::string::npos) error = ErrorModelo::kDatos;
          if (detalle.find("xbdecompress fallo") != std::string::npos || detalle.find("xbcompress fallo") != std::string::npos)
            error = ErrorModelo::kPaquete;
          if (detalle.find("PermissionError") != std::string::npos) error = ErrorModelo::kEscritura;
          if (detalle.find("Original 3 (Placa pectoral) y pieza B3 de torso a la vez") != std::string::npos)
            error = ErrorModelo::kTorsoIncompatible;
          REXLOG_WARN("dbz3 heroe: generacion {} codigo {}: {} (registro {})", clave, code, detalle.substr(0, 1600), registro.string());
        }
        if (!ok && clave.rfind("combate_", 0) == 0)
          REXLOG_ERROR("dbz3 heroe: el montaje del heroe fallo (codigo {}): detalle en ingame_ui/heroe_vista/montaje.log", code);
        break;
      }
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
      if (ok) REXLOG_INFO("dbz3 heroe: vista previa {} generada ({} ms): {}", clave, ms, hecho.string());
      else REXLOG_WARN("dbz3 heroe: vista previa {} no se pudo generar (sin Python o sin data_cmn.afs): se queda la "
                       "anterior o el retrato", clave);
    }
    if (!ficha.empty()) fs::remove(entrada, ec);  // entrada temporal de esta petición, no ficha guardada
    if (clave.rfind("combate_", 0) == 0) g_montaje_fallo = !ok;   // el aviso en pantalla (MontajeFallido)
    std::lock_guard<std::mutex> lk(g_gen_mu);
    g_generando.erase(clave);
    if (!ok) {
      g_fallidas.insert(clave);
      g_error_modelo[clave] = error;
      REXLOG_WARN("dbz3 heroe: modelo no disponible {} motivo {} registro {}", clave, int(error), registro.string());
    }
    if (!ok && escribible) {
      std::ofstream salida(registro, std::ios::app);
      salida << "\nheroe: clave=" << clave << " motivo=" << int(error) << " script=" << script.string()
             << "\n" << diagnostico << "\n";
    }
  }).detach();
}

bool Fallida(const std::string& clave) {
  std::lock_guard<std::mutex> lk(g_gen_mu);
  return g_fallidas.count(clave) != 0;
}
bool Generando2(const std::string& clave) {
  std::lock_guard<std::mutex> lk(g_gen_mu);
  return g_generando.count(clave) != 0 || g_fallidas.count(clave) != 0;
}
bool HayGenerando() {   // K: alguna vista previa en marcha (el menu sigue vivo: se compone en otro hilo)
  std::lock_guard<std::mutex> lk(g_gen_mu);
  return !g_generando.empty();
}
bool Generando(int id, int traje) {
  std::lock_guard<std::mutex> lk(g_gen_mu);
  return g_generando.count(Num(id) + "_" + Num(traje)) != 0;
}

std::string AvisoModelo(const std::string& clave) {
  std::lock_guard<std::mutex> lk(g_gen_mu);
  const auto it = g_error_modelo.find(clave);
  if (it == g_error_modelo.end()) return {};
  switch (it->second) {
    case ErrorModelo::kPython:
      return T::T("Falta Python o sus módulos. Extrae también la carpeta python del paquete del juego.",
                  "Python or its modules are missing. Also extract the python folder from the game package.");
    case ErrorModelo::kPaquete:
      return T::T("Faltan herramientas del Héroe. Extrae el paquete completo del juego en esta carpeta.",
                  "Hero tools are missing. Extract the full game package into this folder.");
    case ErrorModelo::kDatos:
      return T::T("Faltan los datos de B3 HD. Elige su carpeta extraída en el launcher; una ISO no sirve para crear modelos.",
                  "B3 HD data is missing. Select its extracted folder in the launcher; an ISO cannot build models.");
    case ErrorModelo::kTorsoIncompatible:
      return T::T("Elige la placa pectoral o el torso de B3: no se pueden usar juntos.",
                  "Choose the chest plate or the B3 torso: they cannot be used together.");
    case ErrorModelo::kEscritura:
      return T::T("No se puede escribir. Mueve el juego a una carpeta con permisos y vuelve a abrir el Héroe.",
                  "Cannot write files. Move the game to a writable folder and reopen Hero mode.");
    default:
      return T::T("No se pudo crear el modelo. Consulta el registro del Héroe y vuelve a abrir esta pantalla para reintentar.",
                  "Could not build the model. Check the hero log and reopen this screen to retry.");
  }
}

void ReintentarVistas() {
  std::lock_guard<std::mutex> lk(g_gen_mu);
  // Solo al entrar por voluntad del jugador: nunca un bucle de Python por cada fotograma fallido.
  for (auto it = g_fallidas.begin(); it != g_fallidas.end();) {
    if (it->rfind("combate_", 0) == 0 || it->rfind("exportar_", 0) == 0) { ++it; continue; }
    g_error_modelo.erase(*it);
    it = g_fallidas.erase(it);
  }
}

// Hilo de dibujo. Mira el fichero como mucho una vez por segundo.
S::Pic Vista(int id, int traje, double ahora) {
  Podar(g_tiras, ahora);
  Tira& t = g_tiras[Num(id) + "_" + Num(traje)];
  if (ahora - t.visto < 1.0) return t.pic;
  t.visto = ahora;
  const fs::path f = VistaPng(id, traje);
  std::error_code ec;
  const auto cuando = fs::last_write_time(f, ec);
  if (ec) {
    if (!t.pic) Generar(id, traje);
    return t.pic;
  }
  if (t.pic && cuando == t.cuando) return t.pic;
  auto* drawer = dbz3::launcher::PreviewDrawer();
  if (!drawer) return t.pic;
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0;
  const std::vector<uint8_t> px = rex::ui::DecodeImageRGBA(data.data(), data.size(), w, h);
  if (px.empty()) return t.pic;
  t.tex = drawer->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false, px.data());
  t.pic = t.tex ? S::Pic{reinterpret_cast<ImTextureID>(t.tex.get()), float(w), float(h)} : S::Pic{};
  t.cuando = cuando;
  REXLOG_INFO("dbz3 heroe: vista previa {}_{} cargada ({}x{}); vivas: {}", id, traje, w, h, Vivas());
  return t.pic;
}

// ---------------------------------------------------------------------------------------------------------------------
// Vista previa 3D: la malla posada del cuerpo (vista_heroe.py, fichero .malla junto a la tira) pintada EN VIVO con
// ps2::Escena (toon de 3 tonos y borde negro, como los modelos de IW en los minijuegos), girando de verdad.
// ---------------------------------------------------------------------------------------------------------------------
struct Modelo3D {
  dbz3::ps2::Malla malla;
  std::vector<dbz3::ps2::Tex> tx;
  std::vector<std::vector<std::pair<float, int>>> rampa;   // 25/10: tramos de la rampa de B3 (Escena::Toon::rampa)
  float alto = 0;
  float ancho = 0;
  fs::file_time_type cuando{};
  double visto = -1e9;
  // POST3/HEROE-VISTAS: animaciones reales de B3 (bloque "ANI1" de vista_heroe.py bloque_anims): hueso de cada vertice
  // (t * 3 + c) y, por clip, cuadros x nhuesos matrices 3x4 que llevan el vertice de la malla al del cuadro.
  struct Anim {
    int tipo = 0, cuadros = 0;
    float fps = 60;
    bool bucle = false;
    std::vector<float> m;
  };
  int nhuesos = 0;
  std::vector<uint8_t> hueso;
  // ANI2 de vista previa: conserva el camino ANI1 y almacena huesos de 16 bits aparte.
  std::vector<uint16_t> hueso16;
  std::vector<Anim> anims;
  uint32_t Hueso(size_t i) const { return hueso16.empty() ? hueso[i] : hueso16[i]; }
  std::vector<uint8_t> tira;   // CAC-07: tira (draw del bin) de cada triangulo, bloque "DRW1" (255 = no se oculta)
  explicit operator bool() const { return alto > 0 && !malla.tris.empty(); }
};
std::map<std::string, Modelo3D> g_mallas;
std::string Vivas() {
  size_t tiras = 0, mallas = 0, tex = 0;
  for (const auto& [k, t] : g_tiras) tiras += t.tex != nullptr, tex += t.tex != nullptr;
  for (const auto& [k, m] : g_mallas) mallas += !m.tx.empty(), tex += m.tx.size();
  return std::to_string(tiras) + " tiras, " + std::to_string(mallas) + " mallas, " + std::to_string(tex) + " texturas";
}

bool LeerMalla(const fs::path& f, Modelo3D& m) {
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t o = 0;
  auto u32 = [&](uint32_t& v) {
    if (o + 4 > d.size()) return false;
    std::memcpy(&v, d.data() + o, 4);
    o += 4;
    return true;
  };
  auto f32 = [&](float& v) {
    if (o + 4 > d.size()) return false;
    std::memcpy(&v, d.data() + o, 4);
    o += 4;
    return true;
  };
  if (d.size() < 16 || std::memcmp(d.data(), "HMV1", 4) != 0) return false;
  o = 4;
  uint32_t ntex = 0, ntri = 0;
  float alto = 0;
  const bool expandida = REXCVAR_GET(dbz3_lim3_vista_malla);
  if (!u32(ntex) || !u32(ntri) || !f32(alto) || ntex > 256 || !lim3::PreviewMeshFits(expandida, ntri, d.size() - o))
    return false;
  std::vector<dbz3::pasarela::Imagen> imgs(ntex);
  for (auto& im : imgs) {
    uint32_t w = 0, h = 0;
    if (!u32(w) || !u32(h) || w > 4096 || h > 4096 || o + size_t(w) * h * 4 > d.size()) return false;
    im.w = int(w), im.h = int(h);
    im.rgba.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + size_t(w) * h * 4));
    o += size_t(w) * h * 4;
  }
  dbz3::ps2::Malla malla;
  if (expandida && !lim3::PreviewMeshFits(true, ntri, d.size() - o)) return false;
  malla.tris.resize(ntri);
  for (auto& t : malla.tris) {
    for (int c = 0; c < 3; ++c) {
      float v[8];
      for (float& x : v)
        if (!f32(x)) return false;
      t.p[c] = {v[0], v[1], v[2]};
      t.n[c] = {v[3], v[4], v[5]};
      t.uv[c] = ImVec2(v[6], v[7]);
    }
    uint32_t tex = 0, fl = 0;
    if (!u32(tex) || !u32(fl)) return false;
    t.tex = int(tex);
    t.trans = (fl & 1) != 0;
  }
  m.malla = std::move(malla);
  m.rampa.clear();   // 25/10: "RMP1" tras los triangulos: tramos de la rampa toon de B3 por textura (vista_heroe.py)
  uint32_t n_orig = 0;
  if (o + 8 <= d.size() && std::memcmp(d.data() + o, "RMP1", 4) == 0 && (o += 4, u32(n_orig)) && n_orig <= ntex) {
    m.rampa.resize(n_orig);
    bool bien = true;
    for (auto& fila : m.rampa) {
      uint32_t n = 0;
      if (!u32(n) || n > 8) { bien = false; break; }
      for (uint32_t k = 0; k < n && bien; ++k) {
        float l0 = 0;
        uint32_t ti = 0;
        if (!f32(l0) || !u32(ti) || ti >= ntex) bien = false;
        else fila.emplace_back(l0, int(ti));
      }
      if (!bien) break;
    }
    if (!bien) m.rampa.clear();
  }
  m.anims.clear(), m.hueso.clear(), m.nhuesos = 0;
  m.hueso16.clear();
  uint32_t nh = 0, nv = 0, nc = 0;
  if (o + 8 <= d.size() && std::memcmp(d.data() + o, "ANI2", 4) == 0) {
    // ANI2: u32 payload_bytes, u32 nh, u32 nv, u32 nc, nv*u16 LE (align 4),
    // then nc headers <tipo,n,fps,bucle> + matrices 3x4 densely packed as f32 LE.
    // Disabled or unrecognized content leaves the static model and still reads DRW1.
    static const bool activo = REXCVAR_GET(dbz3_lim_ani2);
    constexpr size_t max_ani2 = 64u * 1024u * 1024u;
    uint32_t bytes = 0;
    o += 4;
    if (!u32(bytes) || bytes > d.size() - o) bytes = 0, o = d.size();   // repaso dsREV: bloque roto = modelo estatico, no malla fallida
    const size_t fin = o + bytes;
    if (activo && bytes >= 12 && bytes <= max_ani2) {
      u32(nh); u32(nv); u32(nc);
      const size_t mapa = (size_t(nv) * 2 + 3) & ~size_t(3);
      if (nh > 0 && nh <= 512 && nv == ntri * 3 && nc <= 32 && mapa <= fin - o) {
        std::vector<uint16_t> hv;
        hv.reserve(nv);
        bool bien = true;
        for (size_t i = 0; i < nv; ++i) {
          const uint16_t b = uint16_t(d[o + i * 2]) | (uint16_t(d[o + i * 2 + 1]) << 8);
          if (b >= nh) { bien = false; break; }
          hv.push_back(b);
        }
        o += mapa;
        std::vector<Modelo3D::Anim> anims;
        for (uint32_t k = 0; k < nc && bien; ++k) {
          uint32_t tipo = 0, n = 0, bucle = 0;
          float fps = 0;
          if (fin - o < 16) { bien = false; break; }
          u32(tipo); u32(n); f32(fps); u32(bucle);
          if (tipo >= 32 || !n || n > 1200 || !std::isfinite(fps) || fps <= 0 || fps > 240 || bucle > 1 ||
              size_t(n) * nh * 48 > fin - o || std::any_of(anims.begin(), anims.end(), [&](const auto& a) {
                return uint32_t(a.tipo) == tipo;
              })) { bien = false; break; }
          Modelo3D::Anim a;
          a.tipo = int(tipo), a.cuadros = int(n), a.fps = fps, a.bucle = bucle != 0;
          a.m.resize(size_t(n) * nh * 12);
          std::memcpy(a.m.data(), d.data() + o, a.m.size() * sizeof(float));
          if (!std::all_of(a.m.begin(), a.m.end(), [](float x) { return std::isfinite(x); })) {
            bien = false; break;
          }
          o += a.m.size() * sizeof(float);
          anims.push_back(std::move(a));
        }
        if (bien && o == fin) {
          m.anims = std::move(anims), m.hueso16 = std::move(hv), m.nhuesos = int(nh);
          REXLOG_INFO("dbz3 lim: vista ANI2 clips={} huesos={} bytes={}", nc, nh, bytes);
        }
      }
    }
    o = fin;
  } else if (o + 16 <= d.size() && std::memcmp(d.data() + o, "ANI1", 4) == 0 && (o += 4, u32(nh)) && u32(nv) && nh <= 255 &&
      nv == ntri * 3 && o + nv <= d.size()) {
    std::vector<uint8_t> hv(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + nv));
    o += (nv + 3) & ~3u;
    bool bien = u32(nc) && nc <= 16 && std::all_of(hv.begin(), hv.end(), [&](uint8_t b) { return b < nh; });
    std::vector<Modelo3D::Anim> anims;
    for (uint32_t k = 0; k < nc && bien; ++k) {
      uint32_t tipo = 0, n = 0, bucle = 0;
      float fps = 0;
      if (!u32(tipo) || !u32(n) || !f32(fps) || !u32(bucle) || n == 0 || n > 600 || fps <= 0 ||
          o + size_t(n) * nh * 48 > d.size()) {
        bien = false;
        break;
      }
      Modelo3D::Anim a;
      a.tipo = int(tipo), a.cuadros = int(n), a.fps = fps, a.bucle = bucle != 0;
      a.m.resize(size_t(n) * nh * 12);
      std::memcpy(a.m.data(), d.data() + o, a.m.size() * 4);
      o += a.m.size() * 4;
      anims.push_back(std::move(a));
    }
    if (bien) m.anims = std::move(anims), m.hueso = std::move(hv), m.nhuesos = int(nh);
  }
  m.tira.clear();
  uint32_t nd = 0;
  if (o + 8 <= d.size() && std::memcmp(d.data() + o, "DRW1", 4) == 0 && (o += 4, u32(nd)) && nd == ntri &&
      o + nd <= d.size())
    m.tira.assign(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + nd));
  m.tx.clear();
  for (const auto& im : imgs) m.tx.push_back(dbz3::ps2::Crear(im, false));
  m.alto = alto;
  // Caja real en planta: la usa el encuadre para no recortar hombros ni brazos al girar.
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (const auto& t : m.malla.tris)
    for (const auto& p : t.p) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z),
                            z1 = std::max(z1, p.z);
  m.ancho = std::max(x1 - x0, z1 - z0);
  return true;
}

// Hilo de dibujo; mira el fichero como mucho una vez por segundo (se relee si se regenera al guardar).
// CAC-16: los mechones quitados de una capa (capaN_quitar=...) no rehacen la vista previa: la app los oculta (MallaConOcultas).
std::string ArgsVista(int id, int traje) {
  std::string s = H::ArgsFicha(id, traje);
  for (size_t k; (k = s.find("_quitar=")) != std::string::npos;) {
    const size_t ini = s.rfind(';', k), fin = s.find(';', k);
    s.erase(ini, (fin == std::string::npos ? s.size() : fin) - ini);
  }
  return s + ";tc=3";   // CAC-17: las vistas con lista de texturas (.texcol); invalida las cacheadas sin ella
}
Modelo3D* MallaClave(const std::string& clave, double ahora, double cada = 1.0) {
  Podar(g_mallas, ahora);
  Modelo3D& m = g_mallas[clave];
  if (ahora - m.visto >= cada && !Generando2(clave)) {   // mientras Python la escribe, ni se lee ni se aparta
    m.visto = ahora;
    const fs::path f = Vistas() / (clave + ".malla");
    std::error_code ec;
    const auto cuando = fs::last_write_time(f, ec);
    if (!ec && (!m || cuando != m.cuando)) {
      if (LeerMalla(f, m)) {
        m.cuando = cuando;
        REXLOG_INFO("dbz3 heroe: vista previa 3D {} cargada ({} triangulos, {} texturas, alto {:.2f}); vivas: {}", clave,
                    m.malla.tris.size(), m.tx.size(), m.alto, Vivas());
      } else {
        // Rota (corte de luz, disco lleno...): se aparta a heroe_vista/_respaldo (no se borra) y se regenera; antes se
        // quedaba en el retrato avisando cada segundo. Las de ficha (f_*) las vuelve a pedir MallaFicha al no estar.
        const fs::path resp = Vistas() / "_respaldo";
        fs::create_directories(resp, ec);
        const fs::path dst = resp / (clave + "_" + std::to_string(std::time(nullptr)) + ".malla");
        fs::rename(f, dst, ec);
        REXLOG_WARN("dbz3 heroe: vista previa 3D {}: {} no se puede leer; apartada a {} ({}) y se regenera", clave,
                    f.string(), dst.string(), ec ? ec.message() : "ok");
        m.cuando = cuando;
        if (!ec && clave.rfind("f_", 0) != 0)
          GenerarCon(clave, L"--solo " + fs::path(clave).wstring(), Vistas() / (clave + ".malla"));
      }
    }
  }
  return m ? &m : nullptr;
}
Modelo3D* Malla3D(int id, int traje, double ahora) { return MallaClave(Num(id) + "_" + Num(traje), ahora); }

// Vista de la FICHA (studies 8 fase 2, H2): colores de Parte 1-4 y del pelo, peinado UT, logo y scouter, aplicados
// por vista_heroe.py --ficha a la malla del cuerpo. Clave = f_<hash de los valores>: cada combinacion su fichero.
// (los valores: H::ArgsFicha / H::FichaNativa, heroe.cpp: el mismo texto que ficha.txt del traje de combate)
std::string ClaveFicha(const std::string& args) {
  uint32_t x = 2166136261u;
  for (unsigned char c : std::string_view("cac-t15")) x = (x ^ c) * 16777619u;
  for (unsigned char c : args) x = (x ^ c) * 16777619u;
  char b[16];
  std::snprintf(b, sizeof(b), "f_%08x", x);
  return b;
}
// CAC-13: lo que vista_heroe.py deja en <clave>.json (ventanas del montaje y tope) y <clave>.progreso (barra). Solo hilo de dibujo.
struct TopeInfo { int ventanas = 0, tope = 3261, ms = 0; bool excedido = false, cache = false; };
double NumJson(const std::string& t, const char* k, double def) {
  const size_t p = t.find(std::string("\"") + k + "\"");
  if (p == std::string::npos) return def;
  const size_t c = t.find(':', p);
  return c == std::string::npos ? def : std::atof(t.c_str() + c + 1);
}
std::string LeerTexto(const fs::path& f) {
  std::ifstream e(f, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(e)), std::istreambuf_iterator<char>());
}
const TopeInfo* TopeClave(const std::string& clave) {
  static std::map<std::string, TopeInfo> memo;
  if (const auto it = memo.find(clave); it != memo.end()) return &it->second;
  std::error_code ec;
  const fs::path f = Vistas() / (clave + ".json");
  if (!fs::exists(f, ec)) return nullptr;
  const std::string t = LeerTexto(f);
  if (t.empty()) return nullptr;
  TopeInfo i;
  i.ventanas = int(NumJson(t, "ventanas", 0));
  i.tope = int(NumJson(t, "tope", 3261));
  i.ms = int(NumJson(t, "ms", 0));
  i.excedido = t.find("\"excedido\": true") != std::string::npos;
  i.cache = t.find("\"anims_cache\": true") != std::string::npos;
  if (i.cache) REXLOG_INFO("dbz3 heroe: CAC-13: cache anims {} ({} ms)", clave, i.ms);
  if (i.excedido)
    REXLOG_WARN("dbz3 heroe: CAC-13: tope de ventanas {}/{} (la mezcla no cabe: alguna pieza volveria a Base)", i.ventanas, i.tope);
  return &(memo[clave] = i);
}
// Progreso {n,total,texto} de la vista en marcha; false si no hay fichero. Registra cada cambio.
bool ProgresoClave(const std::string& clave, int* n, int* total, std::string* texto) {
  std::error_code ec;
  const fs::path f = Vistas() / (clave + ".progreso");
  if (!fs::exists(f, ec)) return false;
  const std::string t = LeerTexto(f);
  if (t.empty()) return false;
  *n = int(NumJson(t, "n", 0));
  *total = std::max(1, int(NumJson(t, "total", 1)));
  const size_t p = t.find("\"texto\"");
  const size_t a = p == std::string::npos ? p : t.find('"', t.find(':', p) + 1);
  const size_t b = a == std::string::npos ? a : t.find('"', a + 1);
  *texto = b == std::string::npos ? std::string() : t.substr(a + 1, b - a - 1);
  static std::string ultimo;
  const std::string marca = clave + std::to_string(*n);
  if (marca != ultimo) {
    ultimo = marca;
    REXLOG_INFO("dbz3 heroe: CAC-13: progreso {}/{} {}", *n, *total, *texto);
  }
  return true;
}
bool EnMarcha(const std::string& clave) {
  std::lock_guard<std::mutex> lk(g_gen_mu);
  return g_generando.count(clave) != 0;
}
std::string g_ficha_clave;
double g_ficha_cambio = 0;
// Malla de la ficha si ya esta; si no, la pide (0,4 s despues del ultimo cambio, para no lanzar una por pulsacion).
Modelo3D* MallaFicha(int id, int traje, const C::Heroe& h, double ahora) {
  (void)h;
  if (H::CuerpoPropio(id)) return nullptr;   // G06a: tira 384/12 (la malla completa no cabe en el batch de ImGui)
  if (H::FichaNativa()) return nullptr;
  const std::string args = ArgsVista(id, traje), clave = ClaveFicha(args);
  if (clave != g_ficha_clave) {
    g_ficha_clave = clave;
    g_ficha_cambio = ahora;
  }
  static std::string ultima;   // mientras se genera la nueva, la ultima de la ficha (sin parpadeo al cuerpo nativo)
  static int u_id = -1, u_traje = -1;
  Modelo3D* m = MallaClave(clave, ahora, 0.25);
  if (m) ultima = clave, u_id = id, u_traje = traje;
  if (!m && ahora - g_ficha_cambio > 0.4) {
    // UT: VistaUT ya esta generando la misma ficha (PNG + .malla); no lanzar dos procesos.
    if (!Generando2(clave) && !Generando2("ut_" + clave)) {
      REXLOG_INFO("dbz3 heroe: vista de la ficha {}: {}", clave, args);
      GenerarCon(clave, L"--ficha \"" + fs::u8path(args).wstring() + L"\" --clave " + fs::path(clave).wstring(), Vistas() / (clave + ".malla"));
    }
  }
  if (!m && !ultima.empty() && u_id == id && u_traje == traje) {
    auto it = g_mallas.find(ultima);
    if (it != g_mallas.end() && it->second) {
      it->second.visto = ahora;   // se pinta mientras se genera la nueva: que Podar no la suelte
      return &it->second;
    }
  }
  return m;
}

bool CuerpoUT() { return H::CuerpoPropio(H::Cuerpo()); }   // UT o XV2: cuerpo propio (vista = tira de vista_heroe.py)
bool Xv2() { return H::CuerpoXv2(H::Cuerpo()); }

S::Pic VistaUT(int id, int traje, double ahora) {   // G06a: turntable del cuerpo completo, no una silueta sustituta
  // K: el donante (moveset elegido) decide el esqueleto y los accesorios del heroe (espada de Trunks...): tambien en la vista previa
  const std::string args = ArgsVista(id, traje) + (Xv2() ? ";donante=" + Num(H::DonanteCombate()) : std::string()), clave = ClaveFicha(args);
  Podar(g_tiras, ahora);
  Tira& t = g_tiras["ut_" + clave];
  if (ahora - t.visto < 1.0) return t.pic;
  t.visto = ahora;
  const fs::path f = Vistas() / (clave + ".png");
  std::error_code ec;
  const auto cuando = fs::last_write_time(f, ec);
  if (ec) {
    if (!t.pic && !Generando2("ut_" + clave))
      GenerarCon("ut_" + clave, L"--ficha \"" + fs::u8path(args).wstring() + L"\" --clave " + fs::path(clave).wstring(), f);
    return t.pic;
  }
  if (t.pic && cuando == t.cuando) return t.pic;
  auto* drawer = dbz3::launcher::PreviewDrawer();
  if (!drawer) return t.pic;
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0;
  const auto px = rex::ui::DecodeImageRGBA(data.data(), data.size(), w, h);
  if (px.empty()) return t.pic;
  t.tex = drawer->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false, px.data());
  t.cuando = cuando;
  t.pic = t.tex ? S::Pic{reinterpret_cast<ImTextureID>(t.tex.get()), float(w), float(h)} : S::Pic{};
  REXLOG_INFO("dbz3 heroe: vista UT {} cargada ({}x{}); vivas: {}", clave, w, h, Vivas());
  return t.pic;
}

// Camara de la vista previa (H06): presets medidos sobre la caja real de la malla. 0 cuerpo, 1 tres cuartos (defecto),
// 2 busto, 3 retrato. El zoom se guarda aparte para no perderlo al cambiar de preset.
struct CamPreview {
  const char* nombre;
  float pitch, dist, fov, target;
};
constexpr CamPreview kCams[4] = {
    {"cuerpo", 4.0f, 2.12f, 0.55f, 0.50f},
    {"tres cuartos", 8.0f, 2.12f, 0.55f, 0.52f},
    {"busto", 6.0f, 1.40f, 0.50f, 0.72f},
    {"retrato", 6.0f, 1.00f, 0.22f, 0.90f},
};
int g_cam_preset = 1;
float g_cam_zoom = 1.0f;
float g_cam_yaw = 25.0f;   // grados; LB/RB y el giro automatico mueven el modelo, no la camara
// Pinta el modelo en la caja p0..p1 (px de pantalla), girado `giro` radianes. La camara orbita el centro de la caja:
// antes estaba fija a la altura del pecho (pitch 0), no se podia ver la coronilla y el cuerpo flotaba.
bool VistaPesada(const Modelo3D& m) { return m.malla.tris.size() > 12000; }   // XV2 sin aligerar (~17000); la vista XV2 ~8500; B3 ~2600
bool Pintar3D(ImDrawList* dl, const Modelo3D& m, ImVec2 p0, ImVec2 p1, float giro, const dbz3::ps2::Malla* malla = nullptr) {
  if (const char* e = std::getenv("DBZ3_TEST_HEROE_CAM")) g_cam_preset = std::clamp(std::atoi(e), 0, 3);
  const CamPreview& cp = kCams[std::clamp(g_cam_preset, 0, 3)];
  dbz3::ps2::Camara cam;
  cam.ok = true;
  cam.fov = cp.fov;
  cam.c = ImVec2((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
  cam.h = ImVec2((p1.x - p0.x) * 0.5f, (p1.y - p0.y) * 0.5f);
  const float alto = std::max(m.alto, 0.01f);
  // El plano cercano del proyector es 1.0: no acercar la camara por debajo de 1.05 unidades.
  const float dist = std::max(cp.dist * alto * g_cam_zoom, 1.05f);
  const float ty = cp.target * alto;
  const float yaw = g_cam_yaw * 3.14159265f / 180.0f, pitch = cp.pitch * 3.14159265f / 180.0f;
  const dbz3::ps2::V3 ojo{std::sin(yaw) * std::cos(pitch) * dist, ty + std::sin(pitch) * dist,
                          std::cos(yaw) * std::cos(pitch) * dist};
  const dbz3::ps2::V3 obj{0, ty, 0};
  auto sub = [](const dbz3::ps2::V3& a, const dbz3::ps2::V3& b) { return dbz3::ps2::V3{a.x - b.x, a.y - b.y, a.z - b.z}; };
  auto dot = [](const dbz3::ps2::V3& a, const dbz3::ps2::V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
  auto norm = [&](const dbz3::ps2::V3& a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-6f ? dbz3::ps2::V3{a.x / l, a.y / l, a.z / l} : dbz3::ps2::V3{0, 0, -1};
  };
  auto cross = [](const dbz3::ps2::V3& a, const dbz3::ps2::V3& b) {
    return dbz3::ps2::V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  };
  const dbz3::ps2::V3 fwd = norm(sub(obj, ojo));
  const dbz3::ps2::V3 der = norm(cross(fwd, dbz3::ps2::V3{0, 1, 0}));
  const dbz3::ps2::V3 arr = cross(der, fwd);
  cam.m[0] = der.x, cam.m[1] = arr.x, cam.m[2] = -fwd.x, cam.m[3] = 0;
  cam.m[4] = der.y, cam.m[5] = arr.y, cam.m[6] = -fwd.y, cam.m[7] = 0;
  cam.m[8] = der.z, cam.m[9] = arr.z, cam.m[10] = -fwd.z, cam.m[11] = 0;
  cam.m[12] = -dot(der, ojo), cam.m[13] = -dot(arr, ojo), cam.m[14] = dot(fwd, ojo), cam.m[15] = 1;
  dbz3::ps2::Escena::Toon toon;
  toon.borde = 2.0f / std::max(1.0f, cam.h.y / std::tan(cam.fov * 0.5f) / dist);
  toon.borde_tex = true;
  toon.sin_traseras = m.malla.tris.size() > 3000;   // mallas XV2 (cerradas); las nativas de B3 se pintan como siempre
  if (!m.rampa.empty() && !std::getenv("DBZ3_FIG_TOON_B2")) {   // 25/10: la rampa toon de B3 y su borde fino
    toon.rampa = &m.rampa;
    toon.borde *= 0.6f;
  }
  // POST3/HEROE-XV2-CUERPO: el heroe XV2 trae ~17000 triangulos (B3: ~2600); con la rampa cortada y el borde suavizado
  // pasaba de 65535 vertices: en tandas (ventanas propias) lo tapaba el fondo del creador, y en la lista del creador los
  // textos que van detras daban la vuelta a los indices de 16 bits (manchas por toda la pantalla). Se quitan las caras de
  // espaldas a la camara (ninguna normal de sus vertices la mira: no se ven) y el borde toon (sale de esas caras): la mitad
  // de vertices. ponytail: el XV2 pierde el contorno negro solo en la vista previa (el combate lo conserva); para tenerlo
  // harian falta indices de 32 bits en el renderizador de ImGui del SDK.
  const dbz3::ps2::Malla* dibujo = malla ? malla : &m.malla;
  const bool offsets = (dl->Flags & ImDrawListFlags_AllowVtxOffset) != 0;
  if (VistaPesada(m) && !offsets) {
    toon.borde = 0, toon.ligero = true;
    static dbz3::ps2::Malla delante;
    delante.tris.clear();
    const float cg = std::cos(giro), sg = std::sin(giro);
    for (const auto& t : dibujo->tris) {
      bool ve = false;
      for (int c = 0; c < 3 && !ve; ++c) {
        const auto& p = t.p[c];
        const auto& n = t.n[c];
        const dbz3::ps2::V3 w{p.x * cg + p.z * sg, p.y, -p.x * sg + p.z * cg}, nw{n.x * cg + n.z * sg, n.y, -n.x * sg + n.z * cg};
        ve = (n.x == 0 && n.y == 0 && n.z == 0) || dot(nw, sub(ojo, w)) > 0;
      }
      if (ve) delante.tris.push_back(t);
    }
    dibujo = &delante;
  }
  static dbz3::ps2::Escena escena;
  escena.Empezar();
  escena.Poner(cam, *dibujo, m.tx, dbz3::ps2::V3{0, 0, 0}, giro, 1.0f, toon);
  // R18: la MISMA lista recibe despues los textos del creador. Reservar su margen y comprobar tambien la escena
  // ligera: quitar la rampa no garantiza que quepa el contorno. Sin AA de relleno TODOS los trozos cuestan como
  // maximo 3 vertices (AddTriangleFilled suavizado costaba 6). El AA de la interfaz se restaura tras el modelo.
  // Con offsets este presupuesto protege tiempo de cuadro y memoria, no el indice local.
  const bool presupuesto_ampliado = REXCVAR_GET(dbz3_lim3_vista_vertices);
  const size_t usados = size_t(dl->VtxBuffer.Size);
  const auto cabe = [&] { return lim3::PreviewDrawFits(presupuesto_ampliado, offsets, usados, escena.Cuantos()); };
  const size_t trozos_cortados = escena.Cuantos();
  if (!toon.ligero && !cabe()) {
    toon.ligero = true;
    escena.Empezar();
    escena.Poner(cam, *dibujo, m.tx, dbz3::ps2::V3{0, 0, 0}, giro, 1.0f, toon);
  }
  if (toon.borde > 0 && !cabe()) {
    toon.borde = 0;
    escena.Empezar();
    escena.Poner(cam, *dibujo, m.tx, dbz3::ps2::V3{0, 0, 0}, giro, 1.0f, toon);
  }
  // Una malla excepcionalmente grande no debe corromper el resto de la pantalla ni abrir ventanas por encima.
  if (!cabe()) return false;
  static size_t dicho = 0;
  if (dicho != m.malla.tris.size()) {
    dicho = m.malla.tris.size();
    REXLOG_INFO("dbz3 heroe: vista previa pintada: {} triangulos de malla, {} trozos ({} con la rampa cortada; lista en {}), {}",
                dicho, escena.Cuantos(), trozos_cortados, dl->_VtxCurrentIdx,
                toon.ligero ? "ligero (un tramo por triangulo)" : "rampa cortada");
  }
  dl->PushClipRect(p0, p1, true);
  const auto flags_relleno = dl->Flags;
  dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
  escena.Pintar(dl);
  dl->Flags = flags_relleno;
  dl->PopClipRect();
  if (std::getenv("DBZ3_TEST_VISTA_VERTICES"))
    REXLOG_INFO("dbz3 vista R18: frame={} modelo={} trozos={} vertices={} indice={} tandas={} borde={}",
                ImGui::GetFrameCount(), m.malla.tris.size(), escena.Cuantos(), dl->VtxBuffer.Size,
                dl->_VtxCurrentIdx, escena.Tandas(), toon.borde);
  return true;
}

// POST3/HEROE-VISTAS: la malla en el cuadro `t` (segundos desde que se eligio) de su animacion `tipo` (ANI1). Sin ese clip,
// la malla quieta. Los clips sin bucle (golpe, victoria, transformacion) esperan 1 s en su ultimo cuadro y vuelven a empezar.
const dbz3::ps2::Malla& MallaCuadro(const Modelo3D& m, int tipo, double t) {
  const auto a = std::find_if(m.anims.begin(), m.anims.end(), [&](const Modelo3D::Anim& x) { return x.tipo == tipo; });
  const size_t nv = m.hueso16.empty() ? m.hueso.size() : m.hueso16.size();
  if (a == m.anims.end() || nv != m.malla.tris.size() * 3) return m.malla;
  const int c = int(std::max(t, 0.0) * a->fps);
  const int f = a->bucle ? c % a->cuadros : std::min(c % (a->cuadros + int(a->fps)), a->cuadros - 1);
  const float* M = a->m.data() + size_t(f) * size_t(m.nhuesos) * 12;
  static dbz3::ps2::Malla tmp;
  tmp.tris = m.malla.tris;
  for (size_t i = 0; i < tmp.tris.size(); ++i)
    for (int k = 0; k < 3; ++k) {
      const float* r = M + size_t(m.Hueso(i * 3 + size_t(k))) * 12;
      auto& p = tmp.tris[i].p[k];
      auto& n = tmp.tris[i].n[k];
      const dbz3::ps2::V3 q = p, v = n;
      p = {r[0] * q.x + r[1] * q.y + r[2] * q.z + r[3], r[4] * q.x + r[5] * q.y + r[6] * q.z + r[7],
           r[8] * q.x + r[9] * q.y + r[10] * q.z + r[11]};
      n = {r[0] * v.x + r[1] * v.y + r[2] * v.z, r[4] * v.x + r[5] * v.y + r[6] * v.z, r[8] * v.x + r[9] * v.y + r[10] * v.z};
    }
  return tmp;
}

// ---------------------------------------------------------------------------------------------------------------------
// Piezas de dibujo comunes
// ---------------------------------------------------------------------------------------------------------------------
void Fondo(ImDrawList* dl, const S::Area& a, float x0, float y0, float x1, float y1, int alfa = 225) {
  dl->AddRectFilled(a.At(x0 + 10, y0 + 10), a.At(x1 - 10, y1 - 10), IM_COL32(10, 22, 52, alfa), 10 * a.s);
}
void Marco(ImDrawList* dl, const S::Area& a, float x0, float y0, float x1, float y1, int alfa = 225) {
  Fondo(dl, a, x0, y0, x1, y1, alfa);
  S::Frame(dl, a.At(x0, y0), a.At(x1, y1), a.s);
}
void Texto(ImDrawList* dl, const S::Area& a, float x, float y, float tam, const std::string& s, bool sel, bool off,
           float max_w = 0) {
  S::MenuTextB3(dl, a.At(x, y), tam * a.s, s.c_str(), sel, off, max_w * a.s);
}
void Cara(ImDrawList* dl, const S::Area& a, int id, float x, float y, float h, int alfa = 255) {
  if (const S::Pic p = S::Img(("caras/" + Num(id)).c_str())) {
    const float w = h * p.w / std::max(p.h, 1.0f);
    S::DrawPic(dl, p, a.At(x - w * 0.5f, y - h * 0.5f), a.At(x + w * 0.5f, y + h * 0.5f), ImVec4(0, 0, 0, 0),
               IM_COL32(255, 255, 255, alfa));
  }
}
// Color de la muestra: h tono, s y v de -100..100 alrededor de la mitad [H] (los rangos de UT no estan medidos).
ImU32 ColorHsv(const C::Hsv& c, int alfa = 255) {
  if (c.h == 0 && c.s == 0 && c.v == 0) return IM_COL32(120, 120, 128, alfa);   // "Original": el color del traje
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(float(c.h % 360) / 360.0f, std::clamp(0.55f + c.s / 222.0f, 0.0f, 1.0f),
                              std::clamp(0.6f + c.v / 250.0f, 0.0f, 1.0f), r, g, b);
  return IM_COL32(int(r * 255), int(g * 255), int(b * 255), alfa);
}
void Muestra(ImDrawList* dl, const S::Area& a, float x0, float y0, float x1, float y1, ImU32 col) {
  dl->AddRectFilled(a.At(x0, y0), a.At(x1, y1), col, 4 * a.s);
  dl->AddRect(a.At(x0, y0), a.At(x1, y1), IM_COL32(20, 10, 0, 255), 4 * a.s, 0, 2.5f * a.s);
  dl->AddRect(a.At(x0 - 2, y0 - 2), a.At(x1 + 2, y1 + 2), IM_COL32(255, 190, 70, 255), 5 * a.s, 0, 1.5f * a.s);
}

// ---------------------------------------------------------------------------------------------------------------------
// Create / Edit
// ---------------------------------------------------------------------------------------------------------------------
bool g_edit = false;
int g_fila = 0, g_boton = 0, g_panel = -1, g_canal = 0;
C::Hsv g_panel_antes;
C::Hsv g_panel_texcol;   // editar en el panel; confirmar de una vez para no cambiar la clave durante la edición
C::Heroe g_antes;
int g_traje_antes = 0;
float g_giro = 0;          // giro de la vista previa (en tiras: 0..N)
double g_giro_mano = -1e9;  // ultima vez que se giro a mano (el giro automatico espera 3 s)
double g_t_ant = 0;

const char* const kLogos[][2] = {{"Ninguno", "None"},           {"Tortuga (Kame)", "Turtle"},
                                 {"Rey Kaio", "King Kai"},       {"Capsule Corp.", "Capsule Corp."},
                                 {"Patrulla Roja", "Red Ribbon"}, {"Majin", "Majin"},
                                 {"Kaio-shin", "Supreme Kai"},   {"Patrulla Galáctica", "Galactic Patrol"}};
constexpr int kNumLogos = int(std::size(kLogos));
// donantes nativos sin XV2 (= PELOS_NATIVOS de mod center hd/cac/vista_heroe.py)
// "" = sin donante nativo perfecto: 1.5.5 solo ofrece los que comparten la cabeza de Goku (el resto flotaba o se deformaba, 24/10)
// 25/10: con la calva completa (vista_heroe._calva: craneo y cuello de Krilin) Bardock, Trunks y Goten ya no dejan ver la cara
// por la nuca ni huecos en el cuello: entran
// 25/10: + Yamcha (corto y largo) y Krilin con pelo (traje alternativo)
const char* const kPelosNativos[] = {"", "Goku", "Vegeta", "Bardock", "Gohan (Cell)", "Goku niño", "Trunks", "Goten",
                                     "Yamcha", "Yamcha (largo)", "Krilin"};   // 25/10: Krilin (10) vuelve: con la escala por los ojos encaja (medido 0,92-0,95)
// 25/10: tipos de rostro (= vista_heroe.py ROSTROS; Heroe::cara): la cara de un personaje de B3 en todas sus expresiones,
// o un rostro propio (ojos reales de B3 con otro gesto). Las cejas siguen al color del pelo; en Super Saiyan manda el juego
constexpr std::pair<int, const char*> kPelosCac2[] = {{75, "ANDROID No.17"}, {80, "ANDROID No.18"}};
bool PeloComunCac2() { return REXCVAR_GET(dbz3_heroe_ui_nueva) && H::PeloCac2(); }
bool DonantesPeloCac2() { return H::Cuerpo() == 0 && H::Traje() == 0; }
const char* const kRostros[][2] = {{"Del cuerpo", "Body's own"},          {"Goku", "Goku"},
                                   {"Vegeta", "Vegeta"},                   {"Gohan", "Gohan"},
                                   {"Gohan joven", "Young Gohan"},         {"Trunks", "Trunks"},
                                   {"Bardock", "Bardock"},                 {"Yamcha", "Yamcha"},
                                   {"Ten Shin Han", "Tien Shinhan"},       {"Tipo A: confiado", "Type A: confident"},
                                   {"Tipo B: desafiante", "Type B: defiant"}, {"Tipo C: sereno", "Type C: calm"},
                                   {"Tipo D: feroz", "Type D: fierce"},     {"Tipo E: burlón", "Type E: smug"}};
constexpr int kNumRostros = int(std::size(kRostros));
constexpr int kNumPelos = 11;   // pelos UT del catalogo (mod center hd/cac/piezas_cac.toml: hair_000_01..hair_008_01 + base)

struct DonanteB3 {
  int fid = -1;
  std::string nombre;
  std::string aprobada_en;   // CAC-05: los "cuerpo_traje" (ademas de 0_0) donde un juez aprobo la pieza (texto del array)
  bool aprobada = false;
};
constexpr const char* kB3ZonaEs[5] = {"Torso", "Piernas", "Guantes", "Botas", "Accesorio"};   // CAC-07a: + accesorio
constexpr const char* kB3ZonaEn[5] = {"Torso", "Legs", "Gloves", "Boots", "Accessory"};

std::string JsonCadena(const std::string& obj, const char* clave) {
  const std::string marca = std::string("\"") + clave + "\"";
  size_t p = obj.find(marca);
  if (p == std::string::npos || (p = obj.find(':', p + marca.size())) == std::string::npos) return {};
  while (++p < obj.size() && std::isspace(static_cast<unsigned char>(obj[p]))) {}
  if (p >= obj.size() || obj[p] != '"') return {};
  std::string out;
  for (++p; p < obj.size(); ++p) {
    if (obj[p] == '"') return out;
    if (obj[p] == '\\' && p + 1 < obj.size()) {
      const char c = obj[++p];
      out += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c;
    } else {
      out += obj[p];
    }
  }
  return {};
}
bool JsonVerdadero(const std::string& obj, const char* clave) {   // DS144: el valor exacto de "ok", no un ": true" posterior
  const std::string marca = std::string("\"") + clave + "\"";
  size_t p = obj.find(marca);
  if (p == std::string::npos || (p = obj.find(':', p + marca.size())) == std::string::npos) return false;
  while (++p < obj.size() && std::isspace(static_cast<unsigned char>(obj[p]))) {}
  return obj.compare(p, 4, "true") == 0;
}
const std::array<std::vector<DonanteB3>, 5>& CatalogoDonantesB3() {
  static const auto todos = [] {
    std::array<std::vector<DonanteB3>, 5> out;
    const fs::path path = Juego() / "mod center hd" / "cac" / "piezas_b3_base.json";
    std::ifstream f(path, std::ios::binary);
    if (!f) {
      REXLOG_WARN("dbz3 heroe: falta el catalogo de piezas B3 {} (se conserva Base)", path.string());
      return out;
    }
    const std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    constexpr const char* zonas[] = {"torso", "piernas", "guantes", "botas", "accesorios"};
    for (int z = 0; z < 5; ++z) {
      const std::string key = std::string("\"") + zonas[z] + "\"";
      size_t p = json.find(key);
      if (p == std::string::npos || (p = json.find('[', p + key.size())) == std::string::npos) continue;
      int depth = 0;
      bool quoted = false, escaped = false;
      size_t end = p;
      for (; end < json.size(); ++end) {
        const char c = json[end];
        if (quoted) {
          if (escaped) escaped = false;
          else if (c == '\\') escaped = true;
          else if (c == '"') quoted = false;
          continue;
        }
        if (c == '"') quoted = true;
        else if (c == '[') ++depth;
        else if (c == ']' && --depth == 0) break;
      }
      if (end >= json.size()) continue;
      const std::string arr = json.substr(p + 1, end - p - 1);
      size_t q = 0;
      while ((q = arr.find('{', q)) != std::string::npos) {
        const size_t start = q++;
        int obj_depth = 1;
        quoted = escaped = false;
        for (; q < arr.size() && obj_depth; ++q) {
          const char c = arr[q];
          if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
          } else if (c == '"') quoted = true;
          else if (c == '{') ++obj_depth;
          else if (c == '}') --obj_depth;
        }
        if (obj_depth) break;
        const std::string obj = arr.substr(start, q - start);
        // El catálogo de capas es libre; las piezas guiadas conservan su filtro de aprobación.
        const bool aprobada = z == 4 ? obj.find("\"aprobada_en\"") != std::string::npos
                                    : JsonCadena(obj, "validacion_visual") == "perfecto" && JsonVerdadero(obj, "ok");
        const std::string fid_txt = obj.substr(obj.find("\"fid\"") == std::string::npos ? obj.size() : obj.find("\"fid\""));
        const size_t colon = fid_txt.find(':');
        if (colon == std::string::npos) continue;
        try {
          const int fid = std::stoi(fid_txt.substr(colon + 1));
          const std::string name = JsonCadena(obj, "nombre");
          std::string en;   // CAC-05: "aprobada_en": ["7_0", ...] (sin el campo: solo Goku traje 1, como antes)
          if (size_t a = obj.find("\"aprobada_en\""); a != std::string::npos)
            if (size_t i = obj.find('[', a), j = obj.find(']', a); i != std::string::npos && j != std::string::npos && i < j)
              en = obj.substr(i, j - i + 1);
          if (fid >= 0 && !name.empty()) out[size_t(z)].push_back({fid, name, en, aprobada});
        } catch (...) {
        }
      }
    }
    return out;
  }();
  return todos;
}

std::vector<DonanteB3> LeerDonantesB3(const std::string& zona) {
  constexpr const char* zonas[] = {"torso", "piernas", "guantes", "botas", "accesorios"};
  for (int z = 0; z < 5; ++z) {
    if (zona != zonas[z]) continue;
    std::vector<DonanteB3> out;
    for (const DonanteB3& p : CatalogoDonantesB3()[size_t(z)])
      if (p.aprobada) out.push_back(p);
    return out;
  }
  return {};
}

// 1.5.5: torso/piernas/guantes/botas B3 ocultos hasta adaptarlos al cuerpo (salian negros, despegados o deformes: hojas 24/10)
// 25/10: injerto nuevo (huesos sin estirar, textura exclusiva, rampa cocida, bordes soldados) y catalogo revisado: visibles
constexpr bool kPiezasB3Listas = true;
bool PiezasB3UI() {
  // Las piezas propias del Arcosiano dependen del cuerpo elegido; tener XV2
  // habilitado para otros cuerpos no debe ocultar estos seis selectores.
  const bool raza_b3 = dbz3::razas_b3::Familia(H::Cuerpo()) != 0;
  return kPiezasB3Listas && !g_edit && !Xv2() && !CuerpoUT() && (raza_b3 || !H::Xv2PiezasActivas());
}
bool ArcosianoUI() { return PiezasB3UI() && (H::Cuerpo() == 27 || H::Cuerpo() == 38); }
int RazaB3UI() { return PiezasB3UI() ? dbz3::razas_b3::Familia(H::Cuerpo()) : 0; }
int NumPiezasB3() { return !PiezasB3UI() ? 0 : RazaB3UI() ? dbz3::razas_b3::Numero(RazaB3UI()) : 5 + (REXCVAR_GET(dbz3_heroe_capas) ? C::kNumCapas : 0); }   // 5.a: accesorio; 6-9: Capa 1-4 (CAC-15)
int ZonaB3Fila(int fila) { return fila >= 5 && fila < 5 + NumPiezasB3() ? fila - 5 : -1; }
// CAC-07: la fila "Quitar pieza" va tras las piezas (Create de los cuerpos de B3); Logo y Scouter bajan una
int FilaQuitar() { return !g_edit && !Xv2() && !CuerpoUT() ? 1 : 0; }
// CAC-16: «Mechon» tras «Quitar pieza»: recorre los mechones de las capas (los ids 64.. de la vista) y A quita o devuelve el elegido
int FilaMechon() { return FilaQuitar() && !RazaB3UI() && PiezasB3UI() && REXCVAR_GET(dbz3_heroe_capas) ? 1 : 0; }
// CAC-17: «Textura» y «Entrada» tras «Mechon»: el color de cada entrada de paleta de cada textura del traje (ficha texcol)
int FilaTc() { return FilaQuitar() && !RazaB3UI() && PiezasB3UI() ? 2 : 0; }
int InicioTc() { return 5 + NumPiezasB3() + FilaQuitar() + FilaMechon(); }
bool EsFilaTex(int f) { return FilaTc() && f == InicioTc(); }
bool EsFilaEnt(int f) { return FilaTc() && f == InicioTc() + 1; }
int FilaLogo() { return 5 + NumPiezasB3() + FilaQuitar() + FilaMechon() + FilaTc(); }
bool EsFilaQuitar(int f) { return FilaQuitar() && f == 5 + NumPiezasB3(); }
bool EsFilaMechon(int f) { return FilaMechon() && f == 6 + NumPiezasB3(); }
// CAC-05: antes solo Goku traje 1 (los donantes se midieron ahi). Ahora cada pieza vale en Goku traje 1 y en los
// "cuerpo_traje" de su "aprobada_en" (aprobados por juez en studies 5/ENTREGAS/R23/cac); el montaje aplica la misma regla.
std::vector<DonanteB3> OpcionesB3(int zona) {
  constexpr const char* zonas[] = {"torso", "piernas", "guantes", "botas", "accesorios"};
  const bool goku = H::Cuerpo() == 0 && H::Traje() == 0 && zona < 4;   // los accesorios, solo con aprobada_en
  const std::string clave = "\"" + std::to_string(H::Cuerpo()) + "_" + std::to_string(H::Traje()) + "\"";
  std::vector<DonanteB3> out;
  for (const DonanteB3& p : LeerDonantesB3(zonas[std::clamp(zona, 0, 4)]))
    if (goku || p.aprobada_en.find(clave) != std::string::npos) out.push_back(p);
  return out;
}
int& PiezaB3(C::Heroe& h, int zona) { return zona < 4 ? h.b3_piezas[zona] : h.b3_accesorio; }   // CAC-07a
// CAC-15 «Libre»: cada Capa = (luchador, parte) con izquierda/derecha; el luchador sale del catalogo de piezas (todos los
// modelos de cuerpo de B3) mas las formas Super Saiyan (sus pelos). La parte: 0 pelo, 1 torso, 2 armadura, 3 accesorios
// (= CAPA_PARTES de vista_heroe.py; la ficha lleva capaN=FID:PARTE).
constexpr const char* kCapaParteEs[4] = {"pelo", "torso", "armadura", "accesorios"};
constexpr const char* kCapaParteEn[4] = {"hair", "torso", "armor", "accessories"};
const std::vector<DonanteB3>& LuchadoresCapa() {
  static const std::vector<DonanteB3> lst = [] {
    std::vector<DonanteB3> v;
    std::set<int> vistos;
    const std::pair<int, const char*> extra[] = {{266, "Goku (SSJ)"},    {267, "Goku (SSJ2)"},  {268, "Goku (SSJ3)"},
                                                 {417, "Vegeta (SSJ)"}, {419, "Vegeta (SSJ2)"}, {391, "Trunks (SSJ)"},
                                                 {236, "Gohan (SSJ)"},  {237, "Gohan (SSJ2)"},  {299, "Goten (SSJ)"}};
    for (const auto& [fid, nombre] : extra) {
      DonanteB3 d;
      d.fid = fid, d.nombre = nombre;
      if (vistos.insert(fid).second) v.push_back(d);
    }
    for (const auto& zona : CatalogoDonantesB3())
      for (const DonanteB3& d : zona)
        if (vistos.insert(d.fid).second) v.push_back(d);
    return v;
  }();
  return lst;
}
std::string NombreCapa(const C::Capa& c) {
  if (c.fid < 0) return T::T("Ninguna", "None");
  std::string n = std::to_string(c.fid);
  for (const DonanteB3& d : LuchadoresCapa())
    if (d.fid == c.fid) n = d.nombre;
  const int p = std::clamp(c.parte, 0, 3);
  return n + " · " + T::T(kCapaParteEs[p], kCapaParteEn[p]);
}
void MoverCapa(C::Heroe& h, int i, int dir) {
  const auto& lst = LuchadoresCapa();
  const int n = 1 + int(lst.size()) * 4;
  C::Capa& c = h.capas[i];
  int cur = 0;
  for (size_t k = 0; k < lst.size(); ++k)
    if (lst[k].fid == c.fid) cur = 1 + int(k) * 4 + std::clamp(c.parte, 0, 3);
  cur = (cur + dir + n) % n;
  c = cur == 0 ? C::Capa{} : C::Capa{lst[size_t((cur - 1) / 4)].fid, (cur - 1) % 4};
  REXLOG_INFO("dbz3 heroe: Capa {} = {}", i + 1, NombreCapa(c));
}
std::string NombrePiezaB3(int zona, int fid) {
  if (fid < 0) return T::T("Base", "Base");
  for (const DonanteB3& p : OpcionesB3(zona))
    if (p.fid == fid) return p.nombre;
  return T::T("Base", "Base");
}
void MoverPiezaB3(C::Heroe& h, int zona, int dir) {
  if (zona >= 5 && !RazaB3UI()) return MoverCapa(h, zona - 5, dir);   // CAC-15: filas Capa 1-4
  if (const int raza = RazaB3UI()) {
    dbz3::razas_b3::Cambiar(raza, h.b3_piezas, zona, dir);
    REXLOG_INFO("dbz3 heroe: pieza B3 nativa {} = {}", dbz3::razas_b3::Etiqueta(raza, zona, false),
                dbz3::razas_b3::Nombre(raza, h.b3_piezas, zona));
    return;
  }
  const auto opts = OpcionesB3(zona);
  const int actual = PiezaB3(h, zona);
  int cur = 0;
  for (size_t i = 0; i < opts.size(); ++i)
    if (opts[i].fid == actual) cur = int(i) + 1;
  const int n = int(opts.size()) + 1;
  cur = (cur + dir + n) % n;
  PiezaB3(h, zona) = cur == 0 ? -1 : opts[size_t(cur - 1)].fid;
  REXLOG_INFO("dbz3 heroe: pieza B3 {} = {}", kB3ZonaEn[zona], NombrePiezaB3(zona, PiezaB3(h, zona)));
}

// Scouter nativo (1/11): draw propio con el material del modelo 281 (vista_heroe.py SCOUTER_NATIVO_ACTIVO). Es la ultima
// fila de Create; los botones y la navegacion comparten este recuento. Si se apaga alli, volver a 7 en Create.
REXCVAR_DEFINE_BOOL(dbz3_cac2_formas, true, "DBZ3/Mods", "CaC: browse mounted native forms with the controller");
bool FilaFormaActiva() { return REXCVAR_GET(dbz3_heroe_ui_nueva) && REXCVAR_GET(dbz3_cac2_formas) && !CuerpoUT(); }
int g_forma_cac2 = 0;
std::array<int, 8> g_caps_cac2{};  // -1: sin modelo/capsula anunciados; 0 = forma sin capsula
std::string g_estado_forma_cac2;
REXCVAR_DEFINE_BOOL(dbz3_cac2_reset_color, true, "DBZ3/Mods", "CaC: restore cosmetic colors without changing parts");
bool ResetColoresCac2() {
  return REXCVAR_GET(dbz3_heroe_ui_nueva) && REXCVAR_GET(dbz3_cac2_reset_color) && !CuerpoUT();
}
bool FilaFotoActiva() { return REXCVAR_GET(dbz3_heroe_ui_nueva) && H::FotoCac2(); }
int Filas() { return 3 + int(FilaFotoActiva()) + int(FilaFormaActiva()) + (Xv2() ? 14 : CuerpoUT() ? (g_edit ? 3 : 6) : 7 + NumPiezasB3() + FilaQuitar() + FilaMechon() + FilaTc()); }
// 8/11: la fila «Scouter» de Create esta OCULTA (era la 8.a) hasta que salga el cristal (TANDA3_SCOUTER.md); el codigo y la ficha se conservan.   // la ultima: la de los botones
REXCVAR_DEFINE_BOOL(dbz3_cac2_pestanas, true, "DBZ3/Mods", "CaC: category tabs in Create/Edit");
bool PestanasCac2() { return REXCVAR_GET(dbz3_heroe_ui_nueva) && REXCVAR_GET(dbz3_cac2_pestanas) && !CuerpoUT(); }
int g_tab_cac2 = 0;
constexpr const char* kTabsCac2[][2] = {{"Traje", "Costume"}, {"Colores", "Colors"}, {"Rostro", "Face"}, {"Recetas", "Recipes"}};
int FilaBotones() { return Filas() - 1; }
// CAC-10/20: filas comunes a Create/Edit; se añaden antes de los botones clásicos.
constexpr int kFilasReceta = 3;
int FilaReceta() { return FilaBotones() - kFilasReceta; }
int FilaUtilidadesCac2() { return FilaReceta() - int(FilaFotoActiva()) - int(FilaFormaActiva()); }
bool EsFilaFormaCac2(int f) { return FilaFormaActiva() && f == FilaUtilidadesCac2(); }
bool EsFilaFotoCac2(int f) { return FilaFotoActiva() && f == FilaReceta() - 1; }
std::vector<int> FilasPestanaCac2() {
  std::vector<int> filas;
  for (int f = 0; f < FilaBotones(); ++f) {
    const int tab = f >= FilaUtilidadesCac2() ? 3 : g_edit ? 2 :
                    ((f >= 1 && f <= 4) || EsFilaTex(f) || EsFilaEnt(f)) ? 1 : 0;
    if (tab == g_tab_cac2) filas.push_back(f);
  }
  return filas;
}
int g_receta_sel = 0, g_nom_receta = -1, g_receta_armada = -1;
bool g_codigo_pegar = false;
std::string g_codigo_clave, g_codigo_ficha, g_codigo_texto, g_receta_aviso;
std::string RecetaEtiqueta(int i) {
  return Num(i + 1) + "/" + Num(C::kMaxRecetas) + " · " + (H::RecetaLlena(i) ? H::RecetaNombre(i) : T::T("Vacío", "Empty"));
}
int NumBotones() { return REXCVAR_GET(dbz3_heroe_importar) ? 6 : 5; }   // Importar solo si el ajuste esta encendido
C::Heroe& Ficha() { return C::SlotEditable(C::SlotActivo()); }

// CAC-07 "Quitar pieza": una fila que recorre las tiras (draws) del traje. La elegida parpadea en la vista previa y A
// la oculta o la vuelve a mostrar; las ocultas no se pintan. Los bits valen para el cuerpo y traje con que se marcaron.
int g_parte = 0;
// Solo metadatos propios: la caché puede podar la malla entre dos cuadros.
struct TirasCreador {
  int base = 0;
  std::vector<int> mechones;
  int cuadro = -2;
  int ficha = -1, cuerpo = -1, traje = -1;
};
TirasCreador g_tiras_creador;
const TirasCreador& TirasActuales() {
  static const TirasCreador vacias;
  const auto& t = g_tiras_creador;
  return (t.cuadro >= ImGui::GetFrameCount() - 1 && t.ficha == C::SlotActivo() &&
          t.cuerpo == H::Cuerpo() && t.traje == H::Traje()) ? t : vacias;
}
bool g_parpadeo = false;                  // el cursor esta en la fila: la tira g_parte parpadea
int ClaveTraje() { return H::Cuerpo() * 100 + H::Traje(); }
int64_t Ocultas() {
  const C::Heroe& h = Ficha();
  return h.ocultar_de == ClaveTraje() ? h.ocultar : 0;
}
int NumTiras(const Modelo3D* m) {
  int n = 0;
  if (m)
    for (uint8_t t : m->tira)
      if (t < 64) n = std::max(n, t + 1);   // 64.. = mechones de capa (CAC-16), 255 = sin tira
  return n;
}
void AlternarParte() {
  C::Heroe& h = Ficha();
  if (h.ocultar_de != ClaveTraje()) h.ocultar = 0, h.ocultar_de = ClaveTraje();
  h.ocultar ^= int64_t{1} << g_parte;
  REXLOG_INFO("dbz3 heroe: Create: tira {} {}", g_parte, (h.ocultar >> g_parte & 1) ? "oculta" : "visible");
}
// CAC-16: mechones de capa. id de tira = 64 + 47 * (capa - 1) + mechon (vista_heroe.py TIRA_CAPA0 / TIRAS_POR_CAPA).
int g_parpadeo_mechon = -1;   // id de tira del mechon que parpadea (-1: ninguno)
int g_mechon = 0;   // posicion en la lista de mechones presentes en la vista
std::vector<int> Mechones(const Modelo3D* m) {
  std::vector<int> v;
  if (m)
    for (uint8_t t : m->tira)
      if (t >= 64 && t < 64 + 47 * C::kNumCapas) v.push_back(t);
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}
void GuardarTirasCreador(const Modelo3D* m) {
  g_tiras_creador = {NumTiras(m), Mechones(m), ImGui::GetFrameCount(), C::SlotActivo(), H::Cuerpo(), H::Traje()};
}
bool MechonQuitado(int id) {
  const int c = (id - 64) / 47, b = (id - 64) % 47;
  return c >= 0 && c < C::kNumCapas && (Ficha().capas[c].quitar >> b & 1);
}
void AlternarMechon() {
  const auto v = TirasActuales().mechones;
  if (v.empty()) return;
  const int id = v[size_t(std::min(g_mechon, int(v.size()) - 1))], c = (id - 64) / 47, b = (id - 64) % 47;
  Ficha().capas[c].quitar ^= int64_t{1} << b;
  REXLOG_INFO("dbz3 heroe: Create: mechon {} de la capa {} {}", b + 1, c + 1, MechonQuitado(id) ? "quitado" : "devuelto");
}
const dbz3::ps2::Malla& MallaConOcultas(const Modelo3D& m, const dbz3::ps2::Malla& base) {
  const int64_t ocultas = Ocultas();
  const bool parpadea = g_parpadeo && std::fmod(ImGui::GetTime(), 0.8) < 0.4;
  bool capas_q = false;   // CAC-16: algun mechon de capa quitado o parpadeando
  for (const C::Capa& c : Ficha().capas) capas_q |= c.fid >= 0 && c.quitar != 0;
  const int mech_parp = g_parpadeo_mechon >= 0 && std::fmod(ImGui::GetTime(), 0.8) < 0.4 ? g_parpadeo_mechon : -1;
  if ((!ocultas && !parpadea && !capas_q && mech_parp < 0) || m.tira.size() != base.tris.size()) return base;
  static dbz3::ps2::Malla tmp;
  tmp.tris.clear();
  for (size_t i = 0; i < base.tris.size(); ++i) {
    const int t = m.tira[i];
    if (t >= 64 && t != 255) {
      if (MechonQuitado(t) || t == mech_parp) continue;
    } else if (t != 255 && ((ocultas >> t & 1) || (parpadea && t == g_parte))) continue;
    tmp.tris.push_back(base.tris[i]);
  }
  return tmp;
}
// --- CAC-17: colores por entrada de textura ---------------------------------------------------------------------------
// vista_heroe.py escribe <clave>.texcol junto a la malla: una linea por textura "tex w h zona r,g,b;r,g,b;..." (6 entradas, de
// la mas oscura a la mas clara, color de serie 0..255). La ficha guarda texcol="tex,entrada,h,s,v|...": g_tc es su copia editable.
struct TcTex { int tex = 0, w = 0, h = 0; std::string zona; int rgb[6][3] = {}; };
std::vector<TcTex> g_tc_lista;
std::string g_tc_clave;
std::map<std::pair<int, int>, C::Hsv> g_tc;
std::string g_tc_txt;
int g_tc_tex = 0, g_tc_ent = 0;
std::string TcSerializar() {
  std::string s;
  for (const auto& [k, c] : g_tc)
    if (k.first >= 0 && k.second >= 0 && k.second < 6 && (c.h || c.s || c.v)) s += (s.empty() ? "" : "|") + Num(k.first) + "," + Num(k.second) + "," + Num(c.h) + "," + Num(c.s) + "," + Num(c.v);
  return s;
}
void TcSincronizar(C::Heroe& h) {   // la edicion (puntero de ColorDeFila) manda; un cambio de fuera (Reset, otra ficha) recarga
  const std::string ser = TcSerializar();
  if (ser != g_tc_txt) {
    h.texcol = ser;
    g_tc_txt = ser;
  } else if (h.texcol != g_tc_txt) {
    g_tc.clear();
    size_t i = 0;
    while (i < h.texcol.size()) {
      const size_t j = std::min(h.texcol.find('|', i), h.texcol.size());
      int t = 0, e = 0, a = 0, b = 0, c = 0;
      if (std::sscanf(h.texcol.substr(i, j - i).c_str(), "%d,%d,%d,%d,%d", &t, &e, &a, &b, &c) == 5) g_tc[{t, e}] = C::Hsv{a, b, c};
      i = j + 1;
    }
    g_tc_txt = h.texcol;
  }
}
const std::vector<TcTex>& TcLista() {
  const bool nativa = H::FichaNativa() || H::Materiales17();
  const std::string clave = nativa ? Num(H::Cuerpo()) + "_" + Num(H::Traje())
                                  : ClaveFicha(ArgsVista(H::Cuerpo(), H::Traje()));  // no espera a que MallaFicha dibuje
  if (clave != g_tc_clave) {
    g_tc_lista.clear();
    g_tc_clave = clave;
  }
  if (g_tc_lista.empty()) {
    std::ifstream f(Vistas() / (clave + ".texcol"));
    if (f) {
      std::vector<TcTex> v;
      for (std::string ln; std::getline(f, ln);) {
        TcTex t;
        std::istringstream fila(ln);
        std::string valores;
        if (!(fila >> t.tex >> t.w >> t.h >> t.zona >> valores) ||
            t.tex < 0 || t.w <= 0 || t.h <= 0) continue;
        const char* q = valores.c_str();
        bool completa = true;
        for (int e = 0; e < 6; ++e) {
          int n = 0;
          if (std::sscanf(q, "%3d,%3d,%3d%n", &t.rgb[e][0], &t.rgb[e][1], &t.rgb[e][2], &n) != 3 ||
              std::any_of(t.rgb[e], t.rgb[e] + 3, [](int c) { return c < 0 || c > 255; })) {
            completa = false;
            break;
          }
          q += n;
          if (e < 5 && *q == ';') ++q;
          else if (e < 5 || *q) { completa = false; break; }
        }
        if (completa) v.push_back(t);
      }
      g_tc_lista = std::move(v);  // vacía: volver a mirar; jamás devolver la lista de otra clave
    } else if (nativa) {   // vistas anteriores a CAC-17: regenerar solo el catálogo, con espera entre intentos
      using Reloj = std::chrono::steady_clock;
      static std::map<std::string, Reloj::time_point> proximas;
      const auto ahora = Reloj::now();
      const auto key = "tc_" + clave;
      auto it = proximas.try_emplace(key, ahora).first;
      if (ahora >= it->second && !EnMarcha(key) && !EnMarcha(clave) && !EnMarcha("anim_" + clave)) {
        it->second = ahora + std::chrono::seconds(30);
        {
          std::lock_guard<std::mutex> lk(g_gen_mu);
          g_fallidas.erase(key);   // GenerarCon también bloquea las fallidas
          g_error_modelo.erase(key);
        }
        GenerarCon(key, L"--texcol " + fs::path(clave).wstring(), Vistas() / (clave + ".texcol"));
      }
    }
  }
  return g_tc_lista;
}
C::Hsv* TcColorActual() {
  const auto& l = TcLista();
  if (l.empty()) return nullptr;
  return &g_tc[{l[size_t(std::min(g_tc_tex, int(l.size()) - 1))].tex, g_tc_ent}];
}
C::Hsv* ColorDeFila(int f) {
  C::Heroe& h = Ficha();
  if (EsFilaEnt(f)) return TcColorActual();
  if (Xv2()) return f >= 7 && f < 13 ? &h.parte[f - 7] : nullptr;   // C4: piel | pelo | torso | piernas | botas | guantes
  if (!g_edit && f >= 1 && f <= 4) return &h.parte[f - 1];
  if (g_edit && f == 1) return &h.pelo_c1;
  if (g_edit && f == 2 && !CuerpoUT()) return &h.pelo_c2;
  if (g_edit && f == 4 && !CuerpoUT()) return &h.ojos_c;
  if (g_edit && f == 5 && !CuerpoUT()) return &h.piel_c;
  return nullptr;
}
std::string NombreColor(int f) {
  if (EsFilaEnt(f)) return std::string(T::T("Entrada ", "Entry ")) + Num(g_tc_ent + 1);
  if (Xv2()) {
    static const char* es[6] = {"Color de piel", "Color de pelo", "Color de torso", "Color de piernas", "Color de botas", "Color de guantes"};
    static const char* en[6] = {"Skin color", "Hair color", "Torso color", "Legs color", "Boots color", "Gloves color"};
    return T::T(es[std::clamp(f - 7, 0, 5)], en[std::clamp(f - 7, 0, 5)]);
  }
  if (!g_edit && CuerpoUT()) {
    switch (f) {
      case 1: return T::T("Parte de arriba", "Top");
      case 2: return T::T("Parte de abajo", "Bottoms");
      case 3: return T::T("Cinturón", "Belt");
      case 4: return T::T("Calzado", "Footwear");
    }
  }
  if (!g_edit) return std::string(T::T("Parte ", "Part ")) + Num(f);
  if (f == 4) return T::T("Color de ojos", "Eye color");
  if (f == 5) return T::T("Color de piel", "Skin color");
  return f == 1 ? T::T("Color de pelo 1", "Hair color 1") : T::T("Color de pelo 2", "Hair color 2");
}
std::string HsvTxt(const C::Hsv& c) { return Num(c.h) + " / " + Num(c.s) + " / " + Num(c.v); }

void Restablecer() {
  C::Heroe& h = Ficha();
  if (g_edit) {
    h.pelo = h.cara = 0;
    if (PeloComunCac2()) h.pelo_fid_cac2 = -1;
    h.pelo_c1 = h.pelo_c2 = h.ojos_c = h.piel_c = C::Hsv{};
  } else {
    H::PonerTraje(0);
    for (C::Hsv& c : h.parte) c = C::Hsv{};
    for (int& x : h.xv2) x = 0;
    for (int& fid : h.b3_piezas) fid = -1;
    h.logo = h.scouter = 0;
    h.ocultar = 0, h.ocultar_de = -1;
    h.b3_accesorio = -1;
    h.texcol.clear();
    h.material17.clear();
    h.pintura17.clear();
    for (C::Capa& c : h.capas) c = C::Capa{};   // (incluye los mechones quitados)
  }
  REXLOG_INFO("dbz3 heroe: {}: reset (valores por defecto)", g_edit ? "Edit" : "Create");
}
void Descartar() {
  C::Heroe& h = Ficha();
  for (int i = 0; i < C::kNumPartes; ++i) h.parte[i] = g_antes.parte[i];
  for (int i = 0; i < 7; ++i) h.xv2[i] = g_antes.xv2[i];
  h.logo = g_antes.logo, h.scouter = g_antes.scouter, h.pelo = g_antes.pelo;
  h.pelo_c1 = g_antes.pelo_c1, h.pelo_c2 = g_antes.pelo_c2;
  h.cara = g_antes.cara, h.ojos_c = g_antes.ojos_c, h.piel_c = g_antes.piel_c;
  for (int i = 0; i < 4; ++i) h.b3_piezas[i] = g_antes.b3_piezas[i];
  h.ocultar = g_antes.ocultar, h.ocultar_de = g_antes.ocultar_de;
  h.b3_accesorio = g_antes.b3_accesorio;
  H::PonerTraje(g_traje_antes);
  for (int i = 0; i < C::kNumCapas; ++i) h.capas[i] = g_antes.capas[i];
  h.material17 = g_antes.material17;
  h.pintura17 = g_antes.pintura17;
  h.texcol = g_antes.texcol;   // CAC-17 (despues de PonerTraje, que la limpia si el traje cambia)
  std::copy_n(g_antes.forma_receta, C::kMaxFormasReceta, h.forma_receta);
  std::copy_n(g_antes.forma_estallido, C::kMaxFormasReceta, h.forma_estallido);
  h.pelo_fid_cac2 = g_antes.pelo_fid_cac2;
  h.foto_pose = g_antes.foto_pose;
  h.foto_yaw = g_antes.foto_yaw;
  h.nombre = g_antes.nombre;
  H::PonerNombre(g_antes.nombre);
  std::copy_n(g_antes.recetas, C::kMaxRecetas, h.recetas);  // X prepara el hueco; Confirmar lo persiste
}

REXCVAR_DEFINE_BOOL(dbz3_cac_deshacer, true, "DBZ3/Mods", "CaC: undo and redo appearance edits in the B3 creator");
std::optional<C::HistorialCreador> g_historial_creador;
std::optional<C::AspectoCreador> g_aspecto_antes_panel;

void RegistrarAspecto(std::optional<C::AspectoCreador>& antes, const C::Heroe& h) {
  if (antes && g_historial_creador)
    g_historial_creador->RegistrarSiCambio(std::move(*antes), h, H::Traje());
  antes.reset();
}

void CambiarHistorial(C::Heroe& h, bool deshacer) {
  auto aplicar = [&h](const C::AspectoCreador& destino) {
    H::PonerTraje(destino.Traje());  // limpia capas/texturas si cambia el traje: restaurarlas después
    destino.Restaurar(h);
    H::PonerNombre(h.nombre);
    g_tc.clear();
    g_tc_txt.clear();
    TcSincronizar(h);
    PedirVista(H::Cuerpo(), H::Traje());
  };
  auto& historial = *g_historial_creador;
  if (deshacer ? historial.Deshacer(h, H::Traje(), aplicar) : historial.Rehacer(h, H::Traje(), aplicar)) {
    g_codigo_ficha.clear();
    g_receta_armada = -1;
    g_receta_aviso.clear();
    REXLOG_INFO("dbz3 heroe: NCAC2 {} (traje {}, deshacer {}, rehacer {}, cambios {})",
                deshacer ? "deshacer" : "rehacer", H::Traje() + 1, historial.PendientesDeshacer(),
                historial.PendientesRehacer(), int(historial.HayCambiosSinGuardar(h, H::Traje())));
  }
}

// 1.5.6 Exportar como personaje: vista_heroe.py --exportar deja mods/heroe_<nombre>/ (mod normal, con receta.toml) y
// ingame_ui/heroe_vista/exportar.json con el resultado (ok | sin_id | fallo). Una peticion por pulsacion (clave nueva).
std::string g_exp_clave, g_exp_ultimo;
int g_exp_n = 0;
fs::path ExportarJson() { return Vistas() / "exportar.json"; }
void PedirExportar() {
  if (!g_exp_clave.empty() && Generando2(g_exp_clave) && !Fallida(g_exp_clave)) return;   // ya en marcha
  std::error_code ec;
  fs::remove(ExportarJson(), ec);
  g_exp_clave = "exportar_" + Num(++g_exp_n);
  g_exp_ultimo.clear();
  const std::string args = H::ArgsExportar();
  REXLOG_INFO("dbz3 heroe: exportar como personaje: {}", args);
  GenerarCon(g_exp_clave, L"--exportar 1", ExportarJson(), "vista_heroe.py", args);
}
std::string CampoJson(const std::string& j, const char* k) {   // "k": "valor" | "k": numero (el json lo escribe vista_heroe.py)
  const size_t p = j.find(std::string("\"") + k + "\": ");
  if (p == std::string::npos) return {};
  size_t i = p + std::strlen(k) + 4;
  if (i < j.size() && j[i] == '"') {
    const size_t e = j.find('"', i + 1);
    return e == std::string::npos ? std::string() : j.substr(i + 1, e - i - 1);
  }
  const size_t e = j.find_first_of(",}", i);
  return j.substr(i, e == std::string::npos ? std::string::npos : e - i);
}
std::vector<int> ListaJson(const std::string& j, const char* k) {   // "k": [1, 2, 3]
  std::vector<int> v;
  const size_t p = j.find(std::string("\"") + k + "\": [");
  if (p == std::string::npos) return v;
  const size_t e = j.find(']', p);
  std::istringstream s(j.substr(p + std::strlen(k) + 5, e == std::string::npos ? std::string::npos : e - p - std::strlen(k) - 5));
  for (std::string x; std::getline(s, x, ',');)
    if (const int n = std::atoi(x.c_str()); n > 0) v.push_back(n);
  return v;
}
int DonanteDe(int cap);   // mas abajo (catalogo de tecnicas)
std::string DuenoTecnica(int cap) {   // HEROE-02: nombre del duenyo de la tecnica (o su numero si no esta en el catalogo)
  const int d = DonanteDe(cap);
  return d >= 0 ? M::Nombre(d) : "#" + Num(cap);
}
// Texto de la ayuda del boton Exportar: antes de pulsar, el aviso legal; despues, el estado de la ultima exportacion.
std::string TextoExportar() {
  if (g_exp_clave.empty())
    return T::T("Crea un personaje aparte con este héroe. Se comparte solo su receta, nunca el modelo.",
                "Creates a separate character from this hero. Only its recipe is shared, never the model.");
  std::string r;
  if (Fallida(g_exp_clave)) {
    r = AvisoModelo(g_exp_clave);
  } else if (Generando2(g_exp_clave)) {
    return T::T("Exportando personaje... puede tardar un minuto.", "Exporting character... this may take a minute.");
  } else {
    std::ifstream f(ExportarJson());
    const std::string j((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string estado = CampoJson(j, "estado"), mod = CampoJson(j, "mod");
    if (estado == "ok") {
      r = std::string(T::T("Exportado en mods/", "Exported to mods/")) + mod +
          T::T(". Sale en la selección al pulsar JUGAR. Se comparte la receta, no el modelo.",
               ". It shows in character select after PLAY. Share the recipe, not the model.");
      const std::string aviso = CampoJson(j, "aviso");
      const std::vector<int> fuera = REXCVAR_GET(dbz3_heroe_exportar_lista) ? ListaJson(j, "tecnicas_excluidas") : std::vector<int>{};
      if (aviso.find("XV2") != std::string::npos || aviso.find("Xenoverse") != std::string::npos)
        r += std::string("  ") + T::T("Piezas XV2 desactivadas: héroe base de B3 (se activan en el launcher)",
                         "XV2 parts off: B3 base hero (enable them in the launcher)");
      else if (!aviso.empty() && fuera.empty())
        r += "  " + aviso;
      if (!fuera.empty()) {   // HEROE-02: traducido y con el duenyo de cada una, en vez del aviso en castellano con numeros
        r += std::string("  ") + T::T("No viajan (técnicas de otro estilo): ", "Not included (techniques of another style): ");
        for (size_t k = 0; k < fuera.size(); ++k) r += (k ? ", " : "") + DuenoTecnica(fuera[k]);
      }
    } else if (estado == "sin_id")
      r = T::T("No quedan plazas de personajes nuevos: desactiva otro y vuelve a exportar.",
               "No free new-character slots: disable another one and export again.");
    else
      r = T::T("No se pudo exportar el personaje (detalle en montaje.log).", "Could not export the character (details in montaje.log).");
    if (g_exp_ultimo != r) REXLOG_INFO("dbz3 heroe: exportar como personaje: {} (mod {}, id {})", estado, mod, CampoJson(j, "id_real"));
  }
  g_exp_ultimo = r;
  return r;
}

// 1.5.7 Importar receta: el boton Importar abre la lista de recetas que hay en la carpeta del juego y reconstruye
// una con las piezas de ESTE juego (vista_heroe.py --receta). Se buscan dos sitios:
//   - recetas_heroe/*.toml  (lo que se comparte: solo el .toml, nunca el modelo)
//   - mods/*/receta.toml    (heores exportados en esta misma instalacion)
fs::path ImportarJson() { return Vistas() / "importar.json"; }
bool g_imp_abierto = false;
int g_imp_sel = 0;
std::vector<fs::path> g_imp_lista;
std::string g_imp_clave, g_imp_ultimo;
int g_imp_n = 0;
bool EsReceta(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  const std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return s.find("[receta]") != std::string::npos && s.find("ficha") != std::string::npos;
}
std::vector<fs::path> RecetasDisponibles() {
  std::vector<fs::path> v;
  std::error_code ec;
  const fs::path compartidas = Juego() / "recetas_heroe";
  if (fs::is_directory(compartidas, ec))
    for (const auto& e : fs::directory_iterator(compartidas, ec))
      if (e.path().extension() == ".toml" && fs::is_regular_file(e.path(), ec)) v.push_back(e.path());
  for (const auto& e : fs::directory_iterator(dbz3::ModsRoot(), ec)) {
    if (fs::is_directory(e.path(), ec)) {
      const fs::path r = e.path() / "receta.toml";
      if (fs::is_regular_file(r, ec)) v.push_back(r);
    } else if (e.path().extension() == ".toml" && fs::is_regular_file(e.path(), ec)) {
      v.push_back(e.path());   // receta suelta dejada en mods/ (no es un mod: se filtra por contenido)
    }
  }
  v.erase(std::remove_if(v.begin(), v.end(), [](const fs::path& p) { return !EsReceta(p); }), v.end());
  std::sort(v.begin(), v.end(), [](const fs::path& a, const fs::path& b) {
    std::error_code ea, eb;
    const auto ta = fs::last_write_time(a, ea), tb = fs::last_write_time(b, eb);
    if (ea || eb || ta == tb) return a < b;
    return ta > tb;   // la receta recien exportada sale primera
  });
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}
void AbrirImportar() {
  g_imp_lista = RecetasDisponibles();
  g_imp_sel = 0;
  g_imp_abierto = true;
  g_imp_ultimo.clear();
  REXLOG_INFO("dbz3 heroe: importar receta: {} recetas encontradas (recetas_heroe/ y mods/*/receta.toml)", g_imp_lista.size());
}
void PedirImportar(const fs::path& receta) {
  std::error_code ec;
  fs::remove(ImportarJson(), ec);
  g_imp_clave = "importar_" + Num(++g_imp_n);   // clave nueva: un fallo anterior no bloquea reintentar
  g_imp_ultimo.clear();
  REXLOG_INFO("dbz3 heroe: importar receta: {}", Utf8(receta));
  GenerarCon(g_imp_clave, L"--receta \"" + receta.wstring() + L"\"", ImportarJson());
}
// Texto de la ayuda/picker de importacion. Los avisos reutilizan claves ya traducidas (test_i18n --lista 0).
std::string TextoImportar() {
  if (g_imp_abierto) {
    if (g_imp_lista.empty()) return T::T("Nothing found.", "Nothing found.");
    return Utf8(g_imp_lista[size_t(g_imp_sel)]) + "  [" + T::T("Importar", "Import") + "]";
  }
  if (g_imp_clave.empty()) return T::T("Import a character from another game", "Import a character from another game");
  std::string r;
  if (Fallida(g_imp_clave)) {
    r = T::T("Import failed. Open 'Technical details' to see why.", "Import failed. Open 'Technical details' to see why.");
  } else if (Generando2(g_imp_clave)) {
    return T::T("Importing... (a minute at most)", "Importing... (a minute at most)");
  } else {
    std::ifstream f(ImportarJson());
    const std::string j((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string estado = CampoJson(j, "estado"), mod = CampoJson(j, "mod");
    if (estado == "ok") {
      r = std::string(T::T("Imported", "Imported")) + ": mods/" + mod;
      const std::string id = CampoJson(j, "id_real");
      if (!id.empty()) r += " (#" + id + ")";
      const std::string aviso = CampoJson(j, "aviso");
      const std::vector<int> fuera = REXCVAR_GET(dbz3_heroe_exportar_lista) ? ListaJson(j, "tecnicas_excluidas") : std::vector<int>{};
      if (aviso.find("Xenoverse 2 no instalado") != std::string::npos)   // HEROE-03: receta con XV2 en un juego sin XV2
        r += std::string("  ") + T::T("Xenoverse 2 no instalado: héroe base de B3", "Xenoverse 2 not installed: B3 base hero");
      else if (aviso.find("XV2") != std::string::npos || aviso.find("Xenoverse") != std::string::npos)
        r += std::string("  ") + T::T("Piezas XV2 desactivadas: héroe base de B3 (se activan en el launcher)",
                         "XV2 parts off: B3 base hero (enable them in the launcher)");
      else if (!aviso.empty() && fuera.empty())
        r += "  " + aviso;
      if (!fuera.empty()) {
        r += std::string("  ") + T::T("No viajan (técnicas de otro estilo): ", "Not included (techniques of another style): ");
        for (size_t k = 0; k < fuera.size(); ++k) r += (k ? ", " : "") + DuenoTecnica(fuera[k]);
      }
    } else if (estado == "sin_id") {
      r = T::T("No quedan plazas de personajes nuevos: desactiva otro y vuelve a exportar.",
               "No free new-character slots: disable another one and export again.");
    } else {
      r = T::T("Import failed. Open 'Technical details' to see why.", "Import failed. Open 'Technical details' to see why.");
    }
    if (g_imp_ultimo != r)
      REXLOG_INFO("dbz3 heroe: importar receta: {} (mod {}, id {}) -> {}", estado, mod, CampoJson(j, "id_real"), r);
  }
  g_imp_ultimo = r;
  return r;
}

// Un fichero por petición; EnMarcha excluye las fallidas. Nunca se reutiliza un JSON previo.
void PedirCodigo(bool pegar) {
  if (!g_codigo_clave.empty()) return;
  std::string args;
  if (pegar) {
    const char* p = ImGui::GetClipboardText();
    std::string texto;
    for (; p && *p; ++p) {
      const unsigned char c = *p;
      if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
      if (texto.size() == 6144 || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) {
        g_receta_aviso = T::T("Código no válido.", "Invalid code.");
        return;
      }
      texto += char(c);
    }
    if (texto.empty()) {
      g_receta_aviso = T::T("No hay un código copiado.", "No code on the clipboard.");
      return;
    }
    args = H::ArgsPegarCodigo(texto);
  } else {
    args = H::ArgsCodigoReceta();  // ArgsReceta, no ArgsPropio (no exporta las formas)
  }
  static unsigned n = 0;
  const auto t = std::chrono::system_clock::now().time_since_epoch().count();
  std::string clave;
  do { clave = "codigo_" + std::to_string(t) + "_" + std::to_string(++n); }
  while (fs::exists(Vistas() / (clave + ".json")));
  g_codigo_pegar = pegar;
  g_codigo_clave = clave;
  g_receta_aviso = T::T("Procesando código…", "Processing code…");
  const std::string pedido = args + " --peticion " + clave;
  GenerarCon(clave, fs::path(std::u8string(pedido.begin(), pedido.end())).wstring() + L" --salida \"" + Vistas().wstring() + L"\"",
             Vistas() / (clave + ".json"), "recetas.py");
}

void LeerCodigo() {
  if (g_codigo_clave.empty() || EnMarcha(g_codigo_clave)) return;
  std::ifstream f(Vistas() / (g_codigo_clave + ".json"), std::ios::binary);
  g_codigo_clave.clear();
  const std::string j((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (CampoJson(j, "estado") == "ok") {
    if (g_codigo_pegar) {
      g_codigo_ficha = CampoJson(j, "ficha");
      if (!g_codigo_ficha.empty()) {
        g_receta_aviso = T::T("A otra vez: sustituye el aspecto de esta ficha.", "A again: replace this slot's appearance.");
        return;
      }
    } else {
      g_codigo_texto = CampoJson(j, "codigo");
      if (!g_codigo_texto.empty()) {
        ImGui::SetClipboardText(g_codigo_texto.c_str());
        g_receta_aviso = T::T("Código copiado.", "Code copied.");
        return;
      }
    }
  }
  const auto clave = CampoJson(j, "clave");
  if (clave == "otra_version") g_receta_aviso = T::T("Código de otra versión.", "Code from another version.");
  else if (clave == "grande") g_receta_aviso = T::T("Código demasiado grande.", "Code is too large.");
  else if (clave == "sin_cuerpo") g_receta_aviso = T::T("El código no lleva un cuerpo.", "The code has no body.");
  else if (clave == "crc" || clave == "roto") g_receta_aviso = T::T("Código incompleto o dañado.", "Incomplete or damaged code.");
  else if (clave == "no_codigo") g_receta_aviso = T::T("No es un código de guerrero.", "This is not a warrior code.");
  else g_receta_aviso = T::T("No se pudo leer el código.", "Could not read the code.");
}

std::string AyudaReceta() {
  if (!g_receta_aviso.empty()) return g_receta_aviso;
  if (g_fila == FilaReceta())
    return T::T("Izquierda/derecha: hueco. A: aplicar. X: guardar con nombre.", "Left/right: slot. A: apply. X: save with name.");
  return g_fila == FilaReceta() + 1 ? T::T("A: copiar el código.", "A: copy the code.")
                                    : T::T("A: leer el código copiado.", "A: read the copied code.");
}

bool FichaXv2NoDisponible(const std::string& ficha) {
  const int cuerpo = ficha.rfind("cuerpo=", 0) == 0 ? std::atoi(ficha.c_str() + 7) : -1;
  return H::CuerpoXv2(cuerpo) && !H::Xv2PiezasActivas();
}

std::string Ayuda() {
  if (g_panel < 0 && g_fila >= FilaReceta() && g_fila < FilaBotones()) return AyudaReceta();
  if (g_fila == FilaBotones() && g_boton == 4 && g_panel < 0) return TextoExportar();
  if (g_fila == FilaBotones() && g_boton == 5 && g_panel < 0 && REXCVAR_GET(dbz3_heroe_importar)) return TextoImportar();
  if (g_panel >= 0) {
    return g_canal == 0 ? T::T("Elige el tono.", "Select hue.")
         : g_canal == 1 ? T::T("Elige la saturación.", "Select saturation.")
                        : T::T("Elige el brillo.", "Select brightness.");
  }
  if (g_fila == FilaBotones()) {
    return g_boton == 0 ? T::T("Guarda los cambios en la ficha.", "Save the changes to the slot.")
         : g_boton == 1 ? T::T("Vuelve sin guardar los cambios.", "Go back without saving.")
         : g_boton == 3 ? T::T("Ponle nombre a tu héroe: sale en toda su historia.", "Name your hero: it appears across their story.")
                         : T::T("Deja los valores por defecto.", "Restore the default values.");
  }
  const int b3 = ZonaB3Fila(g_fila);
  if (b3 >= 0 && ArcosianoUI())
    return T::T("Izquierda/derecha: cambia el tipo original de esta pieza.", "Left/right: change this original part type.");
  if (b3 >= 0)
    return !OpcionesB3(b3).empty()
               ? T::T("Izquierda/derecha: elige Base o un donante compatible.",
                      "Choose Base or a compatible donor with left/right.")
               : T::T("Aún no hay piezas aprobadas para este cuerpo y traje.", "No approved parts for this body and costume yet.");
  if (g_edit) {
    return g_fila == 0 ? T::T("Elige el peinado.", "Select a hairstyle.")
         : g_fila == 1 ? T::T("Elige el color base del pelo (las cejas lo siguen).", "Select the base hair color (eyebrows follow it).")
         : g_fila == 2 ? T::T("Elige el color propio del pelo.", "Select the custom hair color.")
         : g_fila == 3 ? T::T("Elige el rostro (en todas sus expresiones).", "Select the face (in all its expressions).")
         : g_fila == 4 ? T::T("Elige el color de ojos (en Super Saiyan, turquesa).", "Select the eye color (teal as Super Saiyan).")
                        : T::T("Elige el tono de piel (cara, brazos y prendas a la vez).", "Select the skin tone (face, arms and clothes at once).");
  }
  if (Xv2() && g_fila < 7) return T::T("Elige la pieza (izquierda / derecha).", "Select the part (left / right).");
  if (EsFilaQuitar(g_fila))
    return TirasActuales().base ? T::T("Izquierda/derecha: elige una parte del traje (parpadea). A: la quita o la devuelve.",
                                         "Left/right: pick a costume part (it blinks). A: remove it or bring it back.")
                                  : T::T("Este traje no tiene partes que quitar.", "This costume has no parts to remove.");
  if (const int cz = ZonaB3Fila(g_fila); cz >= 5 && !RazaB3UI())
    return T::T("Izquierda/derecha: luchador y parte (pelo, torso, armadura, accesorios) puestos tal cual sobre el cuerpo.",
                "Left/right: fighter and part (hair, torso, armor, accessories) placed as they are on the body.");
  if (EsFilaMechon(g_fila))
    return T::T("Izquierda/derecha: elige un mechón de una capa (parpadea). A: lo quita o lo devuelve.",
                "Left/right: pick a lock of a layer (it blinks). A: remove it or bring it back.");
  if (EsFilaTex(g_fila)) return H::Materiales17() ?
      T::T("A: pintar y materiales. X: textura original.", "A: paint and materials. X: reset texture.") :
      T::T("Izquierda/derecha: elige una textura. X: original.", "Left/right: pick a texture. X: reset.");
  if (EsFilaEnt(g_fila)) return T::T("Izquierda/derecha: elige un color de la textura. A: lo cambia (recolorea todo lo que lo usa).", "Left/right: pick a color of the texture. A: change it (recolors everything that uses it).");
  if (g_fila == FilaLogo()) return T::T("Elige la marca del logo.", "Select a logo mark.");
  if (g_fila == FilaLogo() + 1) return T::T("Lleva o no el rastreador.", "Wear the scouter or not.");
  switch (g_fila) {
    case 0: return T::T("Elige el traje.", "Select a costume.");
    default: return T::T("Elige el color de la parte (A: abrir).", "Select the part color (A: open).");
  }
}

// Barra de un canal del panel HSV: tono = arcoiris; saturacion = gris -> color; brillo = negro -> color -> blanco.
void Barra(ImDrawList* dl, const S::Area& a, float x0, float y, float x1, int canal, const C::Hsv& c) {
  const float h = 13;
  const ImVec2 p0 = a.At(x0, y - h), p1 = a.At(x1, y + h);
  if (canal == 0) {
    const int n = 12;
    for (int i = 0; i < n; ++i) {
      float r0, g0, b0, r1, g1, b1;
      ImGui::ColorConvertHSVtoRGB(float(i) / n, 0.85f, 0.95f, r0, g0, b0);
      ImGui::ColorConvertHSVtoRGB(float(i + 1) / n, 0.85f, 0.95f, r1, g1, b1);
      const ImU32 ca = IM_COL32(int(r0 * 255), int(g0 * 255), int(b0 * 255), 255);
      const ImU32 cb = IM_COL32(int(r1 * 255), int(g1 * 255), int(b1 * 255), 255);
      const float xa = p0.x + (p1.x - p0.x) * i / n, xb = p0.x + (p1.x - p0.x) * (i + 1) / n;
      dl->AddRectFilledMultiColor(ImVec2(xa, p0.y), ImVec2(xb + 0.5f, p1.y), ca, cb, cb, ca);
    }
  } else if (canal == 1) {
    C::Hsv g = c, f = c;
    g.s = -100, f.s = 100;
    dl->AddRectFilledMultiColor(p0, p1, ColorHsv(g), ColorHsv(f), ColorHsv(f), ColorHsv(g));
  } else {
    C::Hsv m = c;
    m.v = 0;
    const float xm = (p0.x + p1.x) * 0.5f;
    const ImU32 neg = IM_COL32(0, 0, 0, 255), bla = IM_COL32(255, 255, 255, 255);
    dl->AddRectFilledMultiColor(p0, ImVec2(xm, p1.y), neg, ColorHsv(m), ColorHsv(m), neg);
    dl->AddRectFilledMultiColor(ImVec2(xm, p0.y), p1, ColorHsv(m), bla, bla, ColorHsv(m));
  }
  dl->AddRect(p0, p1, IM_COL32(20, 10, 0, 255), 0, 0, 2.5f * a.s);
  const float v = canal == 0 ? c.h / 360.0f : ((canal == 1 ? c.s : c.v) + 100) / 200.0f;
  const float xv = p0.x + (p1.x - p0.x) * std::clamp(v, 0.0f, 1.0f);
  dl->AddTriangleFilled(ImVec2(xv, p0.y + 2 * a.s), ImVec2(xv - 9 * a.s, p0.y - 12 * a.s), ImVec2(xv + 9 * a.s, p0.y - 12 * a.s),
                        IM_COL32(255, 255, 255, 255));
  dl->AddTriangle(ImVec2(xv, p0.y + 2 * a.s), ImVec2(xv - 9 * a.s, p0.y - 12 * a.s), ImVec2(xv + 9 * a.s, p0.y - 12 * a.s),
                  IM_COL32(20, 10, 0, 255), 2 * a.s);
  dl->AddLine(ImVec2(xv, p0.y), ImVec2(xv, p1.y), IM_COL32(255, 255, 255, 255), 2.5f * a.s);
}

// Columnas de la pantalla (px del area 1200x900)
constexpr float kPx0 = 40, kPx1 = 410, kPy0 = 172, kPy1 = 760;   // vista previa
constexpr float kLx0 = 440, kLx1 = 1130, kLy0 = 172;             // lista
float Paso() { return std::min(54.0f, 450.0f / float(FilaBotones())); }   // C4: 13 filas en XV2

// studies 8 fase 2 (H2): si el combate ya lleva los colores de la ficha (mira los ficheros 1 vez por segundo); "" = nada
// que decir. HEROE-V03: sacado de DibujarVista tal cual para compartirlo con el dibujo nuevo.
const char* TextoCombate() {
  static double visto = -1e9;
  static int st = 0;
  if (dbz3::RelojT(ImGui::GetTime()) - visto > 1.0) {
    visto = dbz3::RelojT(ImGui::GetTime());
    H::TrajeCombate(&st);
  }
  if (st <= 0) return "";
  return st == 1   ? T::T("En combate: con estos colores", "In battle: with these colors")
         : st == 2 ? T::T("En combate: al reiniciar el juego", "In battle: after restarting the game")
                   : T::T("En combate: al confirmar", "In battle: after confirming");
}

// HEROE-V03: que se pinta de la ficha (malla 3D, tira de giros o nada) y su aviso de error. Lo comparten el dibujo de
// siempre (DibujarVista) y el nuevo (heroe_v2_creador.inl); mismas llamadas y en el mismo orden que antes.
struct VistaElegida {
  const Modelo3D* m3 = nullptr;
  S::Pic p;
  std::string aviso, clave_error;
};
// POST3/HEROE-VISTAS (solo con el aspecto nuevo): una vista hecha antes de las animaciones (sin bloque ANI1) se rehace
// una vez en segundo plano con los mismos argumentos; mientras, se ve la de siempre, quieta.
std::set<std::string> g_anim_pedidas;
void PedirAnims(const Modelo3D* m, const std::string& clave, const std::wstring& args) {
  if (!m || !m->anims.empty() || Generando2(clave) || Generando2("ut_" + clave) || !g_anim_pedidas.insert(clave).second) return;
  REXLOG_INFO("dbz3 heroe: vista {} sin animaciones: se rehace", clave);
  GenerarCon("anim_" + clave, args, Vistas() / (clave + ".malla"));
}

VistaElegida ElegirVista() {
  VistaElegida v;
  const int id = H::Cuerpo(), traje = H::Traje();
  const bool ut = CuerpoUT(), nueva = REXCVAR_GET(dbz3_heroe_ui_nueva);
  const double ahora = dbz3::RelojT(ImGui::GetTime());
  const Modelo3D* mf = ut ? nullptr : MallaFicha(id, traje, Ficha(), ahora);   // colores/pelo/logo de la ficha
  v.m3 = ut ? nullptr : (mf ? mf : Malla3D(id, traje, ahora));
  v.p = v.m3 ? S::Pic{} : (ut ? VistaUT(id, traje, ahora) : Vista(id, traje, ahora));
  const std::string ficha_args = ArgsVista(id, traje) + (Xv2() ? ";donante=" + Num(H::DonanteCombate()) : std::string());
  if (nueva) {
    if (ut) {   // POST3: el cuerpo propio (XV2/UT) tambien en 3D, con vistas y animaciones (antes: un solo retrato de frente)
      const std::string k = ClaveFicha(ficha_args);
      Modelo3D* mu = MallaClave(k, ahora);
      if (mu && mu->malla.tris.size() <= 30000) v.m3 = mu;
      PedirAnims(v.m3, k, L"--ficha \"" + fs::u8path(ficha_args).wstring() + L"\" --clave " + fs::path(k).wstring());
    } else if (mf) {
      const std::string k = ClaveFicha(ArgsVista(id, traje));
      PedirAnims(mf, k, L"--ficha \"" + fs::u8path(ArgsVista(id, traje)).wstring() + L"\" --clave " + fs::path(k).wstring());
    } else if (v.m3) {
      PedirAnims(v.m3, Num(id) + "_" + Num(traje), L"--solo " + fs::path(Num(id) + "_" + Num(traje)).wstring());
    }
  }
  const std::string clave = ut ? "ut_" + ClaveFicha(ficha_args) :
                            H::FichaNativa() ? Num(id) + "_" + Num(traje) : ClaveFicha(ficha_args);
  v.aviso = AvisoModelo(clave);
  v.clave_error = clave;
  if (v.aviso.empty() && !ut && !v.m3 && !v.p) {
    v.clave_error = Num(id) + "_" + Num(traje);
    v.aviso = AvisoModelo(v.clave_error);
  }
  return v;
}
// Solo la fila de formas pide el bin montado; nunca confundir una carga/fallo con la Base.
REXCVAR_DEFINE_BOOL(dbz3_cac_cejas_ssj3, false, "DBZ3/Mods", "CaC: preview eyebrow correction for SSJ3");
std::string ArgsFormasCac2() {
  return H::ArgsPropio() + ";donante=" + Num(H::DonanteCombate()) +
         (REXCVAR_GET(dbz3_cac_cejas_ssj3) ? ";_cejas_ssj3=1" : "");
}
void LeerCatalogoFormasCac2(const std::string& args) {
  g_caps_cac2.fill(-1);
  const std::string clave = ClaveFicha(args + ";cac2_catalogo=1");
  const auto dst = Vistas() / (clave + ".formas");
  std::ifstream in(dst);
  if (in) {
    for (int f = 0, cap = 0; in >> f >> cap;) {
      if (f >= 0 && f < int(g_caps_cac2.size()) && cap >= 0 && cap <= 65535) g_caps_cac2[f] = cap;
    }
  } else {
    GenerarCon("cat_" + clave, L"--catalogo-cac2 --clave " + fs::path(clave).wstring(), dst, "vista_heroe.py", args);
  }
  if (g_caps_cac2[g_forma_cac2] < 0) g_forma_cac2 = 0;
  g_estado_forma_cac2 = g_caps_cac2[0] >= 0 ? "" : Fallida("cat_" + clave) ?
      T::T("Catálogo no disponible", "Catalog unavailable") : T::T("Buscando formas…", "Finding forms…");
}

VistaElegida VistaFormaCac2() {
  const std::string args = ArgsFormasCac2();
  const std::string clave = ClaveFicha(args + ";cac2_forma=" + Num(g_forma_cac2));
  const auto malla = Vistas() / (clave + ".malla");
  const auto ahora = dbz3::RelojT(ImGui::GetTime());
  static std::string reciente, puesta;
  static double cambio = 0;
  if (clave != reciente) reciente = clave, cambio = ahora;
  VistaElegida v;
  v.m3 = MallaClave(clave, ahora, 0.25);
  if (v.m3) {
    if (g_forma_cac2 && puesta != clave) REXLOG_INFO("dbz3 heroe: CAC2 forma {} lista", g_forma_cac2);
    puesta = clave;
  } else {
    const std::string trabajo = "ut_" + clave;
    if (ahora - cambio > 0.4) GenerarCon(trabajo, L"--vista-cac2 --forma-vista " + fs::path(Num(g_forma_cac2)).wstring() +
        L" --clave " + fs::path(clave).wstring(), malla, "vista_heroe.py", args);
    v.clave_error = trabajo;
    v.aviso = AvisoModelo(trabajo);
    if (v.aviso.empty()) v.aviso = T::T("Preparando esta forma…", "Preparing this form…");
  }
  return v;
}

// Giro actual de la tira `p` en la caja ix0..ix1 x iy0..iy1 (px del area), con su sombra de suelo. HEROE-V03: sacado de
// DibujarVista tal cual.
void PintarTira(ImDrawList* dl, const S::Area& a, const S::Pic& p, float ix0, float iy0, float ix1, float iy1) {
  const int giros = std::max(1, int(p.w / std::max(p.h, 1.0f) + 0.5f));
  const int k = (int(std::floor(g_giro)) % giros + giros) % giros;
  const float tw = p.w / giros;
  // recorte central de cada giro (el modelo de pie ocupa el centro): aspecto 0.6
  const float cw = tw * 0.6f, sx0 = tw * k + (tw - cw) * 0.5f;
  const float hh = iy1 - iy0, ww = std::min(ix1 - ix0, hh * cw / p.h);
  const float cx = (ix0 + ix1) * 0.5f;
  // suelo: sombra eliptica bajo los pies
  dl->AddEllipseFilled(a.At(cx, iy1 - 6), ImVec2(ww * 0.42f * a.s, 10 * a.s), IM_COL32(0, 0, 0, 110));
  S::DrawPic(dl, p, a.At(cx - ww * 0.5f, iy0), a.At(cx + ww * 0.5f, iy1), ImVec4(sx0, 0, sx0 + cw, p.h));
}

void DibujarVista(ImDrawList* dl, const S::Area& a, float t) {
  Marco(dl, a, kPx0, kPy0, kPx1, kPy1, 235);
  const int id = H::Cuerpo(), traje = H::Traje();
  const float ix0 = kPx0 + 34, ix1 = kPx1 - 34, iy0 = kPy0 + 30, iy1 = kPy1 - 104;
  const VistaElegida v = ElegirVista();
  const Modelo3D* m3 = v.m3;
  GuardarTirasCreador(m3);
  const S::Pic p = v.p;
  const std::string& aviso = v.aviso;
  const std::string& clave_error = v.clave_error;
  bool pintada = false;
  if (m3) {   // 3D en vivo
    const float cx = (ix0 + ix1) * 0.5f;
    if (g_cam_preset <= 1)   // cuerpo entero: sombra de contacto bajo los pies; en busto/retrato no hay suelo
      dl->AddEllipseFilled(a.At(cx, iy1 - 0.16f * (iy1 - iy0)), ImVec2(62 * a.s, 8 * a.s), IM_COL32(0, 0, 0, 110));
    static bool dicho = false;
    if (!dicho) {
      dicho = true;
      REXLOG_INFO("dbz3 heroe: vista previa 3D en vivo (ps2::Escena, {} triangulos)", m3->malla.tris.size());
    }
    pintada = Pintar3D(dl, *m3, a.At(ix0 - 20, iy0 - 10), a.At(ix1 + 20, iy1), g_giro * 6.2831853f / 8.0f,
                       &MallaConOcultas(*m3, m3->malla));
  }
  if (!pintada) {
    const S::Pic tira = p ? p : m3 && !CuerpoUT() ? Vista(id, traje, dbz3::RelojT(ImGui::GetTime())) : S::Pic{};
    if (tira) {
      PintarTira(dl, a, tira, ix0, iy0, ix1, iy1);
    } else {
      Cara(dl, a, CuerpoUT() ? H::EstiloId() : id, (ix0 + ix1) * 0.5f, (iy0 + iy1) * 0.5f - 40, 160);
      Texto(dl, a, (ix0 + ix1) * 0.5f, (iy0 + iy1) * 0.5f + 90, D::kSmall,
            HayGenerando() ? std::string(T::T("Preparando la vista previa", "Preparing the preview")) + std::string(size_t(int(ImGui::GetTime() * 3) % 4), '.')
                                 : T::T("Vista previa: retrato", "Preview: portrait"),
            false, true, ix1 - ix0);
    }
  }
  if (!aviso.empty()) {
    const float y = iy1 - 245;
    dl->AddRectFilled(a.At(kPx0 + 12, y), a.At(kPx1 - 12, iy1 + 8), IM_COL32(25, 16, 28, 245), 5 * a.s);
    const auto ruta_log = (Vistas() / (clave_error + ".log")).u8string();
    const std::string mensaje = aviso + "\n\n" + T::T("Registro: ", "Log: ") + std::string(ruta_log.begin(), ruta_log.end());
    dl->AddText(ImGui::GetFont(), 18 * a.s, a.At(kPx0 + 24, y + 12), IM_COL32(255, 220, 150, 255),
                mensaje.c_str(), nullptr, (kPx1 - kPx0 - 48) * a.s);
  }
  if (!clave_error.empty() && clave_error.rfind("f_", 0) == 0) {   // CAC-13: barra de progreso y aviso del tope de ventanas
    int pn = 0, pt = 1;
    std::string ptxt;
    if (EnMarcha(clave_error) && ProgresoClave(clave_error, &pn, &pt, &ptxt)) {
      const float x0 = kPx0 + 30, x1 = kPx1 - 30, y = kPy1 - 96;
      dl->AddRectFilled(a.At(x0, y), a.At(x1, y + 4), IM_COL32(255, 255, 255, 40), 2 * a.s);
      dl->AddRectFilled(a.At(x0, y), a.At(x0 + (x1 - x0) * std::min(pn, pt) / float(pt), y + 4), IM_COL32(255, 200, 90, 230), 2 * a.s);
    }
    if (const TopeInfo* ti = TopeClave(clave_error); ti && ti->excedido) {
      const std::string msg = std::string(T::T("Esta mezcla pasa del tope (", "This mix goes over the limit (")) + std::to_string(ti->ventanas) + "/" +
                              std::to_string(ti->tope) + T::T("): el cuerpo desaparecería en combate. Quita una pieza.",
                                                              "): the body would vanish in battle. Remove a piece.");
      dl->AddText(ImGui::GetFont(), 17 * a.s, a.At(kPx0 + 24, iy1 - 34), IM_COL32(255, 190, 60, 255), msg.c_str(), nullptr, (kPx1 - kPx0 - 48) * a.s);
    }
  }
  if (Xv2() && HayGenerando())   // K: aviso mientras se compone la pieza nueva (se ve el modelo anterior)
    Texto(dl, a, (kPx0 + kPx1) * 0.5f, kPy1 - 112, D::kSmall,
          std::string(T::T("Actualizando el modelo", "Updating the model")) + std::string(size_t(int(ImGui::GetTime() * 3) % 4), '.'),
          false, true, kPx1 - kPx0 - 60);   // bajo el nombre, fuera del marco; puntos animados
  // nombre del heroe, cuerpo + traje y el giro
  Texto(dl, a, (kPx0 + kPx1) * 0.5f, kPy1 - 82, D::kRow, H::NombreMostrado(), true, false, kPx1 - kPx0 - 60);
  Texto(dl, a, (kPx0 + kPx1) * 0.5f, kPy1 - 50, D::kSmall,
        H::NombreCuerpo() + "  -  " + T::T("Traje ", "Costume ") + Num(traje + 1) + "/" + Num(H::NumTrajes()), false, true,
        kPx1 - kPx0 - 60);
  const char* combate = TextoCombate();
  if (*combate) Texto(dl, a, (kPx0 + kPx1) * 0.5f, kPy1 - 22, D::kSmall * 0.85f, combate, false, true, kPx1 - kPx0 - 60);
  (void)t;
}

// HEROE-V03: textos de la fila f de Create/Edit (etiqueta, valor, si lleva flechas, icono del pack y color si es una
// parte de color). Sacado de DibujarLista tal cual; lo comparten el dibujo de siempre y el nuevo.
struct FilaCrear {
  std::string et, val;
  bool flechas = true;
  const char* icono = nullptr;   // pack Dragon HUB (opcional): ingame_ui/hub/heroe_icono_*.png
  const C::Hsv* color = nullptr;
  uint32_t orig = 0;   // CAC-17: color de serie de la entrada (ImU32) mientras su color propio es 0/0/0
};
FilaCrear TextosFila(int f) {
  C::Heroe& h = Ficha();
  FilaCrear r;
  std::string& et = r.et;
  std::string& val = r.val;
  const char*& icono = r.icono;
  // Se comparte la fila de forma, no un segundo selector que pueda apuntar a otra cápsula.
  if (EsFilaFormaCac2(f)) {
    r.et = T::T("Ver forma", "View form");
    r.val = g_forma_cac2 ? T::T("Forma ", "Form ") + Num(g_forma_cac2) : T::T("Base", "Base");
    if (g_caps_cac2[g_forma_cac2] > 0) r.val += T::T(" · Cápsula ", " · Capsule ") + Num(g_caps_cac2[g_forma_cac2]);
    if (g_forma_cac2 && H::RecetaFormaCac2()) {
      const int ri = H::FormaReceta(g_forma_cac2);
      r.val += ri < 0 ? T::T(" · Nativa", " · Native") : " · " + H::RecetaNombre(ri);
    }
    if (!g_estado_forma_cac2.empty()) r.val = g_estado_forma_cac2;
    return r;
  }
  if (EsFilaFotoCac2(f)) {
    r.et = T::T("Pose del retrato", "Portrait pose");
    r.val = h.foto_pose == 0 ? T::T("Automática", "Automatic") :
        std::string(h.foto_pose == 1 ? T::T("Brazos bajos", "Arms down") : T::T("Brazos abiertos", "Arms out")) +
        " · " + Num(h.foto_yaw) + "°";
    return r;
  }
  if (f >= FilaReceta() && f < FilaBotones()) {
    if (f == FilaReceta()) {
      et = T::T("Traje guardado", "Saved costume");
      val = RecetaEtiqueta(g_receta_sel);
    } else {
      r.flechas = false;
      et = f == FilaReceta() + 1 ? T::T("Compartir código", "Share code") : T::T("Pegar código", "Paste code");
      val = f == FilaReceta() + 1 && !g_codigo_texto.empty() ? g_codigo_texto : "A";
    }
    return r;
  }
  if (EsFilaTex(f)) {
    et = T::T("Textura", "Texture");
    const auto& l = TcLista();
    if (l.empty()) val = T::T("(se prepara la vista)", "(preparing preview)");
    else {
      const TcTex& t = l[size_t(std::min(g_tc_tex, int(l.size()) - 1))];
      val = std::string(T::T("Textura ", "Texture ")) + Num(std::min(g_tc_tex, int(l.size()) - 1) + 1) + " / " + Num(int(l.size())) + "  (" + t.zona + ")";
    }
    return r;
  }
  if (EsFilaEnt(f)) {
    const C::Hsv* c = TcColorActual();
    et = std::string(T::T("Entrada ", "Entry ")) + Num(g_tc_ent + 1) + " / 6";
    if (!c) { val = T::T("(se prepara la vista)", "(preparing preview)"); return r; }
    const auto& l = TcLista();
    const TcTex& t = l[size_t(std::min(g_tc_tex, int(l.size()) - 1))];
    r.orig = IM_COL32(t.rgb[g_tc_ent][0], t.rgb[g_tc_ent][1], t.rgb[g_tc_ent][2], 255);
    r.color = c;
    val = c->h == 0 && c->s == 0 && c->v == 0 ? std::string(T::T("Original", "Original")) : std::string(T::T("Color propio", "Custom color"));
    r.flechas = true;
    return r;
  }
  if (const C::Hsv* c = ColorDeFila(f)) {
    r.color = c;
    et = NombreColor(f);
    val = c->h == 0 && c->s == 0 && c->v == 0 ? std::string(T::T("Original", "Original"))
                                              : std::string(T::T("Color propio", "Custom color"));
    r.flechas = false;
    return r;
  }
  const int b3 = ZonaB3Fila(f);
  if (Xv2() && f < 7) {   // C4: piezas de XV2 (pelo, cara, torso, piernas, botas, guantes)
    et = H::NombreTipoXv2(f);
    const auto lst = H::PiezasXv2(H::Cuerpo(), f);
    int k = 0;
    for (size_t i = 0; i < lst.size(); ++i)
      if (lst[i] == h.xv2[f]) k = int(i);
    val = lst.empty() ? std::string(T::T("(no disponible)", "(not available)"))
                      : std::string(T::T("Pieza ", "Piece ")) + Num(k + 1) + " / " + Num(int(lst.size())) + "  (XV2 " + Num(lst[size_t(k)]) + ")";
  } else if (b3 >= 0) {
    if (const int raza = RazaB3UI()) {
      et = T::T(dbz3::razas_b3::Etiqueta(raza, b3, false), dbz3::razas_b3::Etiqueta(raza, b3, true));
      val = dbz3::razas_b3::Nombre(raza, h.b3_piezas, b3);
    } else if (b3 >= 5) {   // CAC-15: Capa 1-4
      et = std::string(T::T("Capa ", "Layer ")) + Num(b3 - 4);
      val = NombreCapa(h.capas[b3 - 5]);
    } else {
      et = T::T(kB3ZonaEs[b3], kB3ZonaEn[b3]);
      val = NombrePiezaB3(b3, PiezaB3(h, b3));
    }
  } else if (g_edit && f == 3) {
    et = T::T("Rostro", "Face");
    const int k = std::clamp(h.cara, 0, kNumRostros - 1);
    val = T::T(kRostros[k][0], kRostros[k][1]);
  } else if (g_edit) {
    et = T::T("Pelo", "Hair");
    // studies 8 fase 2: el 1 es el pelo propio del cuerpo; 2..10 = piezas UT (vista_heroe.py buscar_pelo)
    if (PeloComunCac2()) {
      const int legado = std::clamp(h.pelo, 0, kNumPelos - 1);
      val = legado ? kPelosNativos[legado] : T::T("Del cuerpo", "Body's own");
      if (DonantesPeloCac2()) for (auto [fid, nombre] : kPelosCac2)
        if (h.pelo_fid_cac2 == fid) val = std::string(nombre) + T::T(" · Base", " · Base");
      if (std::any_of(std::begin(h.capas), std::end(h.capas), [](const C::Capa& c) { return c.fid >= 0 && c.parte == 0; }))
        val += T::T(" (capa prioritaria)", " (layer takes priority)");
    } else val = h.pelo == 0 ? std::string(T::T("Peinado 1 (del cuerpo)", "Hairstyle 1 (body's own)")) + " / " + Num(kNumPelos)
                      : std::string(T::T("Peinado ", "Hairstyle ")) + Num(h.pelo + 1) + " (" + (H::Xv2PiezasActivas() ? "UT" : kPelosNativos[h.pelo]) + (H::Xv2PiezasActivas() ? "" : ", beta") + ") / " + Num(kNumPelos);
  } else if (f == 0) {
    et = T::T("Traje", "Costume");
    icono = "hub/heroe_icono_traje";
    val = std::string(T::T("Traje nativo ", "Native costume ")) + Num(H::Traje() + 1);
  } else if (EsFilaQuitar(f)) {
    et = T::T("Quitar pieza", "Remove part");
    const int n = TirasActuales().base;
    int quitadas = 0;
    for (int i = 0; i < 64; ++i) quitadas += int(Ocultas() >> i & 1);
    val = !n ? std::string(T::T("(no disponible)", "(not available)"))
             : std::string(T::T("Parte ", "Part ")) + Num(std::min(g_parte, n - 1) + 1) + " / " + Num(n) +
                   ((Ocultas() >> g_parte & 1) ? T::T(" (quitada)", " (removed)") : "") +
                   (quitadas ? "  ·  " + Num(quitadas) + T::T(" quitadas", " removed") : std::string());
  } else if (EsFilaMechon(f)) {
    et = T::T("Mechón de capa", "Layer lock");
    const auto v = TirasActuales().mechones;
    if (v.empty()) {
      val = T::T("(sin capa de pelo)", "(no hair layer)");
    } else {
      const int id = v[size_t(std::min(g_mechon, int(v.size()) - 1))];
      val = std::string(T::T("Capa ", "Layer ")) + Num((id - 64) / 47 + 1) + " · " + Num((id - 64) % 47 + 1) + " / " + Num(int(v.size())) +
            (MechonQuitado(id) ? T::T(" (quitado)", " (removed)") : "");
    }
  } else if (f == FilaLogo()) {
    et = T::T("Logo", "Logo mark");
    icono = S::Img("hub/heroe_icono_emblema_estrella") ? "hub/heroe_icono_emblema_estrella" : "hub/heroe_icono_logo";
    const int k = std::clamp(h.logo, 0, kNumLogos - 1);
    val = T::T(kLogos[k][0], kLogos[k][1]);
  } else {
    et = "Scouter";
    icono = "hub/heroe_icono_scouter";
    val = h.scouter ? "ON" : "OFF";
  }
  return r;
}

void DibujarLista(ImDrawList* dl, const S::Area& a, float t) {
  const int n = Filas() - 1;
  const float y1 = kLy0 + 2 * D::kPad + Paso() * n;
  Marco(dl, a, kLx0, kLy0, kLx1, y1);
  const float xm = 700, xv = (xm + 40 + kLx1 - D::kLine) * 0.5f;   // etiqueta | valor
  for (int f = 0; f < n; ++f) {
    const float y = kLy0 + D::kPad + Paso() * (f + 0.5f);
    const bool sel = f == g_fila && g_panel < 0;
    if (sel) D::Stripe(dl, a, kLx0, y - Paso() * 0.5f + 2, kLx1, y + Paso() * 0.5f - 2, t);
    const FilaCrear r = TextosFila(f);
    const std::string& et = r.et;
    const std::string& val = r.val;
    const bool flechas = r.flechas;
    const char* icono = r.icono;
    if (const C::Hsv* c = r.color) {
      Muestra(dl, a, xm + 40, y - 14, xm + 92, y + 14, ColorHsv(*c));
      Texto(dl, a, xv + 40, y, D::kRow, val, sel, false, kLx1 - xm - 160);
    } else {
      Texto(dl, a, xv, y, D::kRow, val, sel, false, kLx1 - xm - 120);
    }
    if (icono)
      if (const S::Pic ic = S::Img(icono)) S::DrawPic(dl, ic, a.At(kLx0 + 14, y - 24), a.At(kLx0 + 62, y + 24), ImVec4(0, 0, 0, 0), sel ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 190));
    Texto(dl, a, (kLx0 + xm) * 0.5f + 10, y, D::kRow, et, sel, !sel, xm - kLx0 - 30);
    if (sel && flechas) S::Arrows(dl, a.At(xm + 20, y - Paso() * 0.5f), a.At(kLx1 - 14, y + Paso() * 0.5f), 32 * a.s, t);
  }
  // botones Confirmar / Atras / Reset / Nombre / Exportar (/ Importar si dbz3_heroe_importar)
  const int nb_n = NumBotones();
  const float hueco = nb_n == 6 ? 12.0f : 20.0f;
  const float by0 = y1 + 18, by1 = by0 + 62, bw = (kLx1 - kLx0 - hueco * (nb_n - 1)) / nb_n;
  const char* nb[6] = {T::T("Confirmar", "Confirm"), T::T("Atrás", "Back"), T::T("Restablecer", "Reset"), T::T("Nombre", "Name"),
                       T::T("Exportar", "Export"), T::T("Importar", "Import")};
  for (int b = 0; b < nb_n; ++b) {
    const float x0 = kLx0 + (bw + hueco) * b, x1 = x0 + bw;
    const bool sel = g_fila == FilaBotones() && g_boton == b && g_panel < 0;
    Marco(dl, a, x0, by0, x1, by1);
    if (sel) D::Stripe(dl, a, x0, by0 + 8, x1, by1 - 8, t);
    Texto(dl, a, (x0 + x1) * 0.5f, (by0 + by1) * 0.5f, D::kRow, nb[b], sel, !sel, bw - 40);
  }
  // linea de ayuda (UT: "Select brightness.")
  const float hy0 = by1 + 16, hy1 = hy0 + 58;
  Marco(dl, a, kLx0, hy0, kLx1, hy1, 200);
  Texto(dl, a, (kLx0 + kLx1) * 0.5f, (hy0 + hy1) * 0.5f, D::kSmall, Ayuda(), false, false, kLx1 - kLx0 - 70);
}

void DibujarPanel(ImDrawList* dl, const S::Area& a, float t) {
  C::Hsv* c = ColorDeFila(g_panel);
  if (c && EsFilaEnt(g_panel)) c = &g_panel_texcol;
  if (!c) return;
  const int n = Filas() - 1;
  const float y1 = kLy0 + 2 * D::kPad + Paso() * n;
  Marco(dl, a, kLx0, kLy0, kLx1, y1, 245);
  const float cab = kLy0 + D::kPad + 30;
  Texto(dl, a, (kLx0 + kLx1) * 0.5f - 60, cab, D::kHead, NombreColor(g_panel) + ": " + T::T("color propio", "custom color"),
        true, false, kLx1 - kLx0 - 260);
  Muestra(dl, a, kLx1 - 150, cab - 22, kLx1 - 60, cab + 22, ColorHsv(*c));
  const char* nom[3] = {T::T("Tono", "Hue"), T::T("Saturación", "Saturation"), T::T("Brillo", "Brightness")};
  const int val[3] = {c->h, c->s, c->v};
  const float paso = std::min(90.0f, (y1 - cab - 60) / 3);
  for (int k = 0; k < 3; ++k) {
    const float y = cab + 56 + paso * (k + 0.5f);
    const bool sel = k == g_canal;
    if (sel) D::Stripe(dl, a, kLx0, y - paso * 0.5f + 4, kLx1, y + paso * 0.5f - 4, t);
    Texto(dl, a, kLx0 + 110, y, D::kRow, nom[k], sel, !sel, 160);
    Barra(dl, a, kLx0 + 210, y + 4, kLx1 - 140, k, *c);
    Texto(dl, a, kLx1 - 80, y, D::kRow, Num(val[k]), sel, false, 90);
    if (sel) S::Arrows(dl, a.At(kLx0 + 186, y - paso * 0.42f), a.At(kLx1 - 118, y + paso * 0.42f), 30 * a.s, t);
  }
}

// Lista de recetas del boton Importar: tapa la columna de la derecha (la de la vista previa se sigue viendo).
void DibujarImportar(ImDrawList* dl, const S::Area& a, float t) {
  if (!g_imp_abierto) return;
  const int filas = Filas() - 1;
  const float y1 = kLy0 + 2 * D::kPad + Paso() * filas;
  Marco(dl, a, kLx0, kLy0, kLx1, y1, 248);
  const float cab = kLy0 + D::kPad + 30;
  Texto(dl, a, (kLx0 + kLx1) * 0.5f, cab, D::kHead,
        T::T("Import a character from another game", "Import a character from another game"), true, false, kLx1 - kLx0 - 60);
  if (g_imp_lista.empty()) {
    Texto(dl, a, (kLx0 + kLx1) * 0.5f, (cab + y1) * 0.5f, D::kRow, T::T("Nothing found.", "Nothing found."), false, false,
          kLx1 - kLx0 - 60);
    return;
  }
  const int vis = std::max(1, int((y1 - cab - 40) / Paso()));
  const int n = int(g_imp_lista.size());
  const int first = std::clamp(g_imp_sel - vis / 2, 0, std::max(0, n - vis));
  const int last = std::min(n, first + vis);
  for (int i = first; i < last; ++i) {
    const fs::path& r = g_imp_lista[size_t(i)];
    const bool sel = i == g_imp_sel;
    const float y = cab + 32 + Paso() * (i - first + 0.5f);
    if (sel) D::Stripe(dl, a, kLx0, y - Paso() * 0.5f + 4, kLx1, y + Paso() * 0.5f - 4, t);
    const std::string et = Utf8(r.parent_path().filename()) + " / " + Utf8(r.filename());
    Texto(dl, a, (kLx0 + kLx1) * 0.5f, y, D::kRow, et, sel, !sel, kLx1 - kLx0 - 60);
  }
}

// Nombre del heroe: rejilla de letras con el mando (A escribe, X borra, Y acepta, B cancela). Hasta 16 bytes (PonerNombre).
bool g_nom_abierto = false;
std::string g_nom_txt;
int g_nom_cur = 0;
const std::vector<std::string>& LetrasNombre() {
  static const std::vector<std::string> v = [] {
    std::vector<std::string> r;
    for (char c = 'A'; c <= 'Z'; ++c) r.push_back(std::string(1, c));
    for (const char* s : {"\xC3\x91", "\xC3\x81", "\xC3\x89", "\xC3\x8D"}) r.push_back(s);
    for (char c = 'a'; c <= 'z'; ++c) r.push_back(std::string(1, c));
    for (const char* s : {"\xC3\xB1", "\xC3\xA1", "\xC3\xA9", "\xC3\xAD"}) r.push_back(s);
    for (const char* s : {"\xC3\x93", "\xC3\x9A", "\xC3\xB3", "\xC3\xBA", "\xC3\xBC", " ", "-", ".", "'", "!"}) r.push_back(s);
    for (char c = '0'; c <= '9'; ++c) r.push_back(std::string(1, c));
    return r;
  }();
  return v;
}
void BorrarLetra(std::string& s) {
  while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
  if (!s.empty()) s.pop_back();
}
void NombreEntrada(uint16_t p) {
  const int n = int(LetrasNombre().size());
  constexpr int kCols = 10;
  if (p & kLeft) g_nom_cur = (g_nom_cur + n - 1) % n;
  if (p & kRight) g_nom_cur = (g_nom_cur + 1) % n;
  if (p & kUp) g_nom_cur = (g_nom_cur + n - kCols) % n;
  if (p & kDown) g_nom_cur = (g_nom_cur + kCols) % n;
  if (p & kA) {
    const std::string& l = LetrasNombre()[size_t(g_nom_cur)];
    if (g_nom_txt.size() + l.size() <= (g_nom_receta >= 0 ? 24u : 16u)) g_nom_txt += l;
  }
  if (p & kX) BorrarLetra(g_nom_txt);
  if (p & kB) { g_nom_abierto = false; g_nom_receta = -1; return; }
  if ((p & kY) && !g_nom_txt.empty() && g_nom_txt.find_first_not_of(' ') != std::string::npos) {
    if (g_nom_receta >= 0) {
      const bool ok = H::RecetaGuardar(g_nom_receta, g_nom_txt, false);
      g_receta_aviso = ok ? T::T("Traje preparado. Confirma para guardar.", "Costume ready. Confirm to save.")
                          : T::T("No se pudo guardar el traje.", "Could not save the costume.");
      g_nom_receta = -1;
      g_nom_abierto = false;
      return;
    }
    H::PonerNombre(g_nom_txt);
    Ficha().nombre = H::Nombre();
    REXLOG_INFO("dbz3 heroe: nombre = {}", H::Nombre());
    g_nom_abierto = false;
  }
}
void NombreDibujo(ImDrawList* dl, const S::Area& a, float t) {
  constexpr int kCols = 10;
  const auto& v = LetrasNombre();
  const int filas = (int(v.size()) + kCols - 1) / kCols;
  const float y1 = kLy0 + 2 * D::kPad + Paso() * (Filas() - 1);
  Marco(dl, a, kLx0, kLy0, kLx1, y1, 245);
  Texto(dl, a, (kLx0 + kLx1) * 0.5f, kLy0 + 44, D::kHead, g_nom_txt + (std::fmod(t, 1.0f) < 0.5f ? "_" : " "), true, false, kLx1 - kLx0 - 60);
  const float gy0 = kLy0 + 90, cw = (kLx1 - kLx0 - 40) / kCols, ch = std::min(56.0f, (y1 - gy0 - 16) / filas);
  for (int i = 0; i < int(v.size()); ++i) {
    const float x = kLx0 + 20 + cw * (i % kCols), y = gy0 + ch * (i / kCols);
    const bool sel = i == g_nom_cur;
    if (sel) D::Stripe(dl, a, x + 2, y + 2, x + cw - 2, y + ch - 2, t);
    Texto(dl, a, x + cw * 0.5f, y + ch * 0.5f, D::kRow, v[size_t(i)], sel, !sel, cw);
  }
}

}  // namespace

bool EnEdit() { return g_edit; }

void PedirVista(int id, int traje) {
  ReintentarVistas();
  std::error_code ec;
  fs::path malla = VistaPng(id, traje);
  malla.replace_extension(".malla");
  if (!fs::exists(VistaPng(id, traje), ec) || !fs::exists(malla, ec)) Generar(id, traje);
}

// studies 9: monta mods/heroe_propio = el heroe de la ficha como PERSONAJE NUEVO (ID propio en el roster: nombre de la
// ficha, modelo con los colores, el logo y el peinado UT, moveset del estilo). El juego lo carga al arrancar (roster_build
// en JUGAR): hasta reiniciar, el combate sale como antes (estilo + cuerpo). Antes: traje extra heroe_fichaN (studies 8).
// Menu nativo de deseos en la historia del Heroe (campaigns.cpp avisa cada cuadro del menu): solo el registro de la fila
std::atomic<int> g_deseo_fila{-1};
std::atomic<int> g_deseo_filas{0};             // HEROE-V04 (08): filas del menu y cuando se aviso por ultima vez (ms),
std::atomic<long long> g_deseo_ms{-100000};    // para que el aspecto nuevo (heroe_v2_shenron.inl) sepa que esta en pantalla
long long MsDeseo() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void AvisarMenuDeseo(int fila, int filas) {
  if (static bool panel = false; !panel) panel = true, dbz3::modos::AsegurarPanel();   // HEROE-V05
  if (g_deseo_fila.exchange(fila) != fila) REXLOG_INFO("dbz3 heroe: menu de deseos: fila {} de {}", fila + 1, filas);
  g_deseo_filas = filas;
  g_deseo_ms = MsDeseo();
}
bool MenuDeseo(int* fila, int* filas) {
  if (MsDeseo() - g_deseo_ms.load() > 250) return false;
  *fila = g_deseo_fila.load();
  *filas = g_deseo_filas.load();
  if (H::DeseoHistoriaPendiente()) {
    // Este acceso se hace desde el dibujo de la pantalla nativa. Sus dos filas tienen
    // etiquetas propias: nunca se presenta una capsula como si reviviera a alguien.
    const S::Area q = v2::LienzoImagen();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const char* textos[] = {dbz3::i18n::T("Revivir a los caídos", "Revive the fallen"),
                            dbz3::i18n::T("Seguir sin revivir", "Continue without reviving")};
    for (int i = 0; i < 2; ++i)
      dl->AddText(ImGui::GetFont(), 30 * q.s, q.At(350, 671 + 72.0f * i),
                  i == *fila ? IM_COL32(200, 255, 255, 255) : IM_COL32(110, 200, 185, 255), textos[i]);
    dl->AddText(ImGui::GetFont(), 22 * q.s, q.At(350, 835), IM_COL32(210, 230, 245, 255),
                dbz3::i18n::T("B: Pensarlo (conserva las siete Esferas)", "B: Decide later (keep all seven Dragon Balls)"));
  }
  return true;
}
bool MontajeFallido() { return g_montaje_fallo; }
bool FaltaRotuloNombre() {
  std::error_code ec;
  return H::IdPropio() >= 0 && (!H::HistoriaMontada() || !fs::exists(dbz3::ModsRoot() / "heroe_nombres" / "nombres.json", ec));
}
void PedirCombate() {
  const std::string args = H::ArgsPropio();
  const fs::path hecho = dbz3::ModsRoot() / "heroe_propio" / "ficha.txt";
  std::error_code ec;
  const char* sin_py = std::getenv("DBZ3_TEST_SIN_PYTHON");   // prueba (pruebas.py): sin cache, para ver el aviso del montaje fallido
  if (!(sin_py && *sin_py == '1') && fs::exists(hecho, ec)) {
    std::ifstream f(hecho);
    std::string l;
    std::getline(f, l);
    while (!l.empty() && (l.back() == 13 || l.back() == ' ')) l.pop_back();
    if (l == args && H::HistoriaMontada() && fs::exists(dbz3::ModsRoot() / "heroe_nombres" / "nombres.json", ec))
      return;   // ya esta hecho con estos valores (y su historia de DU y el rotulo del nombre montados)
  }
  REXLOG_INFO("dbz3 heroe: el heroe propio de la ficha {}: se monta ({})", H::FichaNumero(), args);
  const std::string con_donante = args + ";donante=" + Num(H::DonanteCombate());
  GenerarCon("combate_" + ClaveFicha(args),
             // studies 9b: UTF-8 -> UTF-16 (fs::path(std::string) usa la pagina ANSI y rompe los acentos del nombre)
             L"--combate " + fs::path(Num(H::FichaNumero())).wstring(),
             hecho, "vista_heroe.py", con_donante);
}

void AbrirCrear(bool edit) {
  g_receta_sel = 0;
  g_nom_receta = g_receta_armada = -1;
  g_codigo_clave.clear(); g_codigo_ficha.clear(); g_codigo_texto.clear(); g_receta_aviso.clear();
  g_edit = edit;
  g_tab_cac2 = edit ? 2 : 0;
  g_fila = g_boton = g_canal = 0;
  g_panel = -1;
  g_imp_abierto = false;
  g_antes = Ficha();
  g_historial_creador.reset();
  g_aspecto_antes_panel.reset();
  g_forma_cac2 = 0;
  g_caps_cac2.fill(-1);
  g_estado_forma_cac2.clear();
  g_traje_antes = H::Traje();
  g_giro_mano = -1e9;
  PedirVista(H::Cuerpo(), H::Traje());
  REXLOG_INFO("dbz3 heroe: {} abierto (ficha {}, cuerpo {}, traje {}, logo {}, scouter {}, pelo {})", edit ? "Edit" : "Create",
              C::SlotActivo() + 1, H::Cuerpo(), H::Traje() + 1, g_antes.logo, g_antes.scouter, g_antes.pelo);
  if (ArcosianoUI()) REXLOG_INFO("dbz3 heroe: Arcosiano: seis selectores originales disponibles");
}

// Arte de GPT (encargo 3, res/heroe/gpt3/<rel>): sin el fichero devuelve Pic vacia y quien llama pone lo de siempre.
namespace {
S::Pic PicPng(Tira& t, const fs::path& f, double ahora, double cada);
S::Pic PicPng(const fs::path& f, double ahora);
}
static S::Pic ArteGpt3(const char* rel) { return PicPng(Juego() / "res" / "heroe" / "gpt3" / rel, dbz3::RelojT(ImGui::GetTime())); }
// Fondo opaco de pantalla completa de GPT (1920x1080) con el mismo velo suave y las tiras laterales que v2::Fondo.
static bool FondoGpt3(ImDrawList* dl, const S::Area& a, const char* rel) {
  const S::Pic p = ArteGpt3(rel);
  if (!p) return false;
  const S::Imagen g = S::GuestImage(ImGui::GetIO());
  S::DrawPic(dl, p, g.p0, g.p1, ImVec4(0, 0, p.w, p.h));
  dl->AddRectFilled(g.p0, g.p1, IM_COL32(0, 8, 28, 40));
  S::Sides(dl, a);
  return true;
}
// Fondo del Modo Heroe otra vez encima del anfitrion (lo dibuja modos.cpp antes): estas pantallas usan toda el area,
// como las de UT, y la vista previa / la lista no deben tener al anfitrion asomando por detras.
// `propio`: fondo opcional del pack Dragon HUB (ingame_ui/hub/heroe_fondo_*.png, 16:9). Sin el pack queda el de siempre.
// Con el: un velo oscuro suave (los fondos son claros) para que el texto se lea.
static void FondoSinAnfitrion(ImDrawList* dl, const S::Area& a, const char* propio = nullptr) {
  const bool con_pack = propio && S::Img(propio);
  const char* fondo = con_pack ? propio : S::Img("hub/fondo_heroe") ? "hub/fondo_heroe" : S::Img("hub/fondo") ? "hub/fondo" : "fondo_duelo";
  S::Backdrop(dl, a, fondo);
  if (con_pack) {
    const auto g = S::GuestImage(ImGui::GetIO());
    dl->AddRectFilled(g.p0, g.p1, IM_COL32(0, 8, 28, 110));
  }
  S::Sides(dl, a);
}

void DibujarAvatarUT(ImDrawList* dl, const S::Area& a) {   // G06a: selector con el cuerpo propio
  FondoSinAnfitrion(dl, a, "hub/heroe_fondo_creador");
  if (const auto p = VistaUT(H::Cuerpo(), H::Traje(), dbz3::RelojT(ImGui::GetTime()))) {
    const float frame = p.h;   // fotogramas cuadrados: 12 giros
    S::DrawPic(dl, p, a.At(55, 210), a.At(440, 825), ImVec4(0, 0, frame, p.h));
  }
}

// HEROE-V04: dibujo nuevo de Create/Edit (maquetas 01 y 02). Solo dibuja: la entrada sigue en Crear.
#include "heroe_v2_creador.inl"
#include "heroe_v2_ficha.inl"      // HEROE-V04: ficha del heroe (Principal, maqueta 03)
REXCVAR_DEFINE_BOOL(dbz3_heroe_motivo_maestro, true, "DBZ3/Mods",
                    "Hero: show the actual master selection restriction");

const char* AvisoAfinidadMaestro() {
  return T::T("Aún te falta una decisión o un reto de su ruta.", "A choice or challenge on this master's route is still missing.");
}
#include "heroe_v2_maestros.inl"   // HEROE-V04: Elige maestro (Maestros, maqueta 05)
#include "heroe_v2_mapa.inl"       // HEROE-V04: Mapa del mundo (MapaUI, maqueta 04)
REXCVAR_DEFINE_BOOL(dbz3_heroe_ayuda_esquiva, true, "DBZ3/Mods",
                    "Hero room: show the dodge slot when Techniques v2 is on");

#include "heroe_v2_habitacion.inl"   // Habitacion (maqueta 2, dbz3_heroe_habitacion)
#include "heroe_v2_evolucion.inl"     // Evolucion por raza (maqueta 4, dbz3_heroe_evolucion)

#include "heroe_cac17.inl"

Accion Crear(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  C::Heroe& h = Ficha();
  LeerCodigo();
  struct TcSync { C::Heroe& h; explicit TcSync(C::Heroe& x) : h(x) { TcSincronizar(h); } ~TcSync() { TcSincronizar(h); } } tc_sync(h);   // CAC-17
  const bool nueva = REXCVAR_GET(dbz3_heroe_ui_nueva);   // HEROE-V04: apagado = el dibujo de siempre
  if (nueva && REXCVAR_GET(dbz3_cac_deshacer) && !CuerpoUT() && !Xv2()) {
    if (!g_historial_creador) g_historial_creador.emplace(h, H::Traje());
  } else if (g_historial_creador) {
    g_historial_creador.reset();
    g_aspecto_antes_panel.reset();
  }
  if (g_p17.abierta && H::Materiales17()) {
    EditarMateriales17(h, p);
    if (!g_p17.abierta) RegistrarAspecto(g_aspecto_antes_panel, h);
    return Accion::kNada;
  }
  if (g_p17.abierta) {
    g_p17 = {};
    g_aspecto_antes_panel.reset();
  }
  g_boton = std::min(g_boton, NumBotones() - 1);   // si se apaga Importar con el cursor en el, no cae en Reset
  const bool fondo_cac2 = PestanasCac2();
  g_aro_creador = !fondo_cac2 && nueva && FondoGpt3(dl, a, "fondo_creador_domos.png");
  if (fondo_cac2) {
    if (!v2::Fondo(dl, a, v2::kFondoCreador)) {
      S::Backdrop(dl, a, "fondo_duelo");  // recurso nativo del Duelo, data_usi 2055
      S::Sides(dl, a);
    }
  } else if (!(g_aro_creador || (nueva && v2::Fondo(dl, a, v2::kFondoCreador)))) FondoSinAnfitrion(dl, a, "hub/heroe_fondo_creador");
  // giro de la vista previa: automatico (1,6 giros por segundo) salvo 3 s despues de girar a mano (LB/RB)
  const double ahora = dbz3::RelojT(ImGui::GetTime());
  const float dt = float(std::clamp(ahora - g_t_ant, 0.0, 0.1));
  g_t_ant = ahora;
  // H06: giro mas lento (30 grados/s), zoom con rueda, orbita con Q/E y reset con R; X cambia de preset.
  if (!nueva && ahora - g_giro_mano > 3.0) g_giro += 0.667f * dt;   // POST3: con el aspecto nuevo ya no gira solo
  if (!nueva && (p & (kLb | kRb))) {
    g_giro = std::floor(g_giro) + ((p & kRb) ? 1.0f : -1.0f) + 0.01f;
    g_giro_mano = ahora;
  }
  ImGuiIO& io = ImGui::GetIO();
  if (nueva) ControlesVistaV2(a, p, dt, ahora);   // POST3/HEROE-VISTAS: vistas, giro libre y animaciones
  if (io.MouseWheel != 0.0f) {
    g_cam_zoom = std::clamp(g_cam_zoom * std::pow(1.10f, io.MouseWheel), 0.62f, 3.20f);
    REXLOG_INFO("dbz3 heroe: camara zoom {:.2f}", g_cam_zoom);
  }
  if (!nueva && ImGui::IsKeyPressed(ImGuiKey_Q)) g_cam_yaw -= 5.0f;   // con el aspecto nuevo Q/E son LT/RT: vistas
  if (!nueva && ImGui::IsKeyPressed(ImGuiKey_E)) g_cam_yaw += 5.0f;
  if (ImGui::IsKeyPressed(ImGuiKey_R)) {
    g_cam_preset = 1, g_cam_zoom = 1.0f, g_cam_yaw = 25.0f;
    REXLOG_INFO("dbz3 heroe: camara reset (tres cuartos)");
  }
  if ((p & (nueva ? kRsV2 : kX)) && !g_nom_abierto && (nueva || (!EsFilaTex(g_fila) && !EsFilaEnt(g_fila)))) {   // HEROE-V05: con el aspecto nuevo, X es Nombre: camara a RS
    g_cam_preset = (g_cam_preset + 1) % 4;
    REXLOG_INFO("dbz3 heroe: camara preset={} ({}) zoom={:.2f}", g_cam_preset, kCams[g_cam_preset].nombre, g_cam_zoom);
  }
  if (g_nom_abierto) {
    std::optional<C::AspectoCreador> antes;
    if (g_historial_creador && (p & kY) && !(p & kB)) antes.emplace(h, H::Traje());
    NombreEntrada(p);
    if (antes && !g_nom_abierto) RegistrarAspecto(antes, h);
  } else if (C::Hsv* color = g_panel >= 0 ? ColorDeFila(g_panel) : nullptr; g_panel >= 0 && !color) {
    g_panel = -1;   // la entrada desapareció: cerrar y consumir la pulsación
    g_aspecto_antes_panel.reset();
  } else if (color) {
    if (ResetColoresCac2() && (p & kY) && !(p & kB)) {
      C::RestaurarColoresCac2(h);
      g_tc.clear();
      if (g_aspecto_antes_panel) TcSincronizar(h);
      RegistrarAspecto(g_aspecto_antes_panel, h);
      g_panel = -1;
      REXLOG_INFO("dbz3 heroe: CAC2 colores originales");
      DibujarCreadorV2(dl, a, t);
      return Accion::kNada;
    }
    const bool entrada = EsFilaEnt(g_panel);
    C::Hsv& c = entrada ? g_panel_texcol : *color;
    const int nc = nueva ? 4 : 3;   // HEROE-V05: 4.a fila "Colores rapidos" solo con el aspecto nuevo
    if (p & kUp) g_canal = (g_canal + nc - 1) % nc;
    if (p & kDown) g_canal = (g_canal + 1) % nc;
    const int d = ((p & kRight) ? 1 : 0) - ((p & kLeft) ? 1 : 0);
    if (d && g_canal == 3) {
      g_rapido_v2 = (g_rapido_v2 + d + kNumRapidosV2) % kNumRapidosV2;
    } else if (d) {
      if (g_canal == 0) c.h = ((c.h + 10 * d) % 360 + 360) % 360;
      else if (g_canal == 1) c.s = std::clamp(c.s + 10 * d, -100, 100);
      else c.v = std::clamp(c.v + 10 * d, -100, 100);
    }
    if ((nueva || EsFilaEnt(g_panel)) && (p & kX)) c = C::Hsv{};
    if (EsFilaEnt(g_panel) && ((p & kY) || ImGui::IsKeyPressed(ImGuiKey_Delete))) {
      g_tc.clear();
      if (g_aspecto_antes_panel) TcSincronizar(h);
      RegistrarAspecto(g_aspecto_antes_panel, h);
      g_panel = -1;
      return Accion::kNada;
    }   // X Original: el color de serie de la parte
    // A en los colores rapidos: pone ese color y vuelve a Tono para afinarlo (el panel sigue abierto; A otra vez acepta)
    const bool rapido = nueva && g_canal == 3 && (p & kA) && !(p & kB);
    if (rapido) {
      c = kRapidosV2[g_rapido_v2];
      g_canal = 0;
      REXLOG_INFO("dbz3 heroe: {}: color rapido {} = ({}, {}, {})", g_edit ? "Edit" : "Create", g_rapido_v2 + 1, c.h, c.s, c.v);
    }
    if (p & kB) c = g_panel_antes;
    if (!rapido && (p & (kA | kB))) {
      C::ClampHsv(c);
      if (entrada && !(p & kB)) *color = c;
      if (p & kB) g_aspecto_antes_panel.reset();
      if (g_aspecto_antes_panel) TcSincronizar(h);
      RegistrarAspecto(g_aspecto_antes_panel, h);
      REXLOG_INFO("dbz3 heroe: {}: {} = ({}, {}, {}){}", g_edit ? "Edit" : "Create", NombreColor(g_panel), c.h, c.s, c.v,
                  (p & kB) ? " (sin cambios)" : "");
      g_panel = -1;
    }
  } else if (g_imp_abierto) {   // lista de recetas: arriba/abajo elige, A reconstruye, B cierra
    const int n = int(g_imp_lista.size());
    if (n) {
      if (p & kUp) g_imp_sel = (g_imp_sel + n - 1) % n;
      if (p & kDown) g_imp_sel = (g_imp_sel + 1) % n;
      if (p & kA) {
        PedirImportar(g_imp_lista[size_t(g_imp_sel)]);
        g_imp_abierto = false;
      }
    }
    if (p & kB) g_imp_abierto = false;
  } else {
    // HEROE-V05: con el aspecto nuevo la fila de botones no esta en el panel (van en la barra de ayuda, maqueta 01)
    if (static bool probado = false; !probado) {   // PRUEBA (solo pruebas.py): DBZ3_TEST_CAPAS="FID:PARTE,FID:PARTE,.." pone las capas sin teclear
      probado = true;
      if (const char* e = std::getenv("DBZ3_TEST_CAPAS")) {
        int i = 0;
        for (const char* q = e; *q && i < C::kNumCapas; ++i) {
          int fid = -1, parte = 0, n_ = 0;
          if (std::sscanf(q, "%d:%d%n", &fid, &parte, &n_) < 2) break;
          h.capas[i] = C::Capa{fid, parte, 0};
          q += n_;
          if (*q == ',') ++q;
        }
      }
    }
    // Ctrl+Z comparte Z con BACK en el teclado. Consumirlo antes de cambiar de pestaña.
    if (g_historial_creador && !(p & kB)) {
      const bool ctrl_z = io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false);
      const bool ctrl_y = io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y, false);
      const bool ls = (p & kLsV2) && !REXCVAR_GET(dbz3_heroe_importar);
      if (ctrl_z || ctrl_y || ls) {
        CambiarHistorial(h, ctrl_z || ls);
        p = 0;
      }
    }
    const int n = nueva ? FilaBotones() : Filas();
    if (nueva) g_fila = std::clamp(g_fila, 0, n - 1);
    if (p & (kUp | kDown)) g_ayuda_v2 = 0;   // el resultado de Exportar / Importar se ve hasta moverse
    const bool pestanas = PestanasCac2();
    if (pestanas && (p & kBackV2) && !(p & kB)) {
      g_tab_cac2 = (g_tab_cac2 + 1) % int(std::size(kTabsCac2));
      g_edit = g_tab_cac2 == 2;
      g_fila = -1;
      g_ayuda_v2 = 0;
      REXLOG_INFO("dbz3 heroe: CAC2 pestana {}", g_tab_cac2);
      p = 0;  // no aplicar otra pulsacion sobre la primera fila de la nueva pestaña
      g_codigo_ficha.clear();
      g_receta_armada = -1;
      g_receta_aviso.clear();
    }
    const auto filas_cac2 = pestanas ? FilasPestanaCac2() : std::vector<int>{};
    if (!filas_cac2.empty()) {
      const auto i = std::find(filas_cac2.begin(), filas_cac2.end(), g_fila);
      const int m = int(filas_cac2.size()), d = int(bool(p & kDown)) - int(bool(p & kUp));
      g_fila = i == filas_cac2.end() ? filas_cac2.front() : filas_cac2[(i - filas_cac2.begin() + d + m) % m];
    } else {
      if (p & kUp) g_fila = (g_fila + n - 1) % n;
      if (p & kDown) g_fila = (g_fila + 1) % n;
    }
    if (p & (kUp | kDown | kB)) {
      g_codigo_ficha.clear();
      g_receta_armada = -1;
      g_receta_aviso.clear();
    }
    g_parpadeo = EsFilaQuitar(g_fila) && g_panel < 0;
    g_parpadeo_mechon = -1;
    if (EsFilaMechon(g_fila) && g_panel < 0) {
      const auto mv = TirasActuales().mechones;
      if (!mv.empty()) g_parpadeo_mechon = mv[size_t(std::min(g_mechon, int(mv.size()) - 1))];
    }
    std::optional<C::AspectoCreador> antes_lista;
    if (g_historial_creador && (p & (kA | kLeft | kRight | kX | kY | kStartV2)) && !(p & kB))
      antes_lista.emplace(h, H::Traje());
    const int d = ((p & kRight) ? 1 : 0) - ((p & kLeft) ? 1 : 0);
    if (EsFilaFormaCac2(g_fila) && g_forma_cac2 && H::RecetaFormaCac2() && !(p & kB) && (p & (kX | kY))) {
      constexpr const char* kEstallidos[] = {"", "#FFAA00", "#64B4FF", "#DF50FF", "#FFFFFF"};
      const int f = g_forma_cac2;
      int r = H::FormaReceta(f);
      std::string color = h.forma_estallido[f];
      if (p & kY) {
        do r = r + 1 >= C::kMaxRecetas ? -1 : r + 1; while (r >= 0 && !H::RecetaCompatibleForma(r));
      } else if (r >= 0) {
        const auto fin = std::end(kEstallidos);
        const auto i = std::find(std::begin(kEstallidos), fin, color);
        color = i == fin || i + 1 == fin ? kEstallidos[0] : *(i + 1);
      }
      if (H::PonerFormaReceta(f, r, color))
        REXLOG_INFO("dbz3 heroe: CAC2 forma {} receta {} estallido '{}'", f, r, h.forma_estallido[f]);
      p &= ~(kX | kY | kA | kLeft | kRight | kRb);
    }
    if (EsFilaFormaCac2(g_fila)) {
      LeerCatalogoFormasCac2(H::ArgsReceta() + ";donante=" + Num(H::DonanteCombate()));
      if (!(p & kB)) {
        const int paso = d ? d : (p & kA) ? 1 : 0;
        if (paso && g_caps_cac2[0] >= 0) {
          for (int i = 0; i < 8; ++i) {
            g_forma_cac2 = (g_forma_cac2 + paso + 8) % 8;
            if (g_caps_cac2[g_forma_cac2] >= 0) break;
          }
          REXLOG_INFO("dbz3 heroe: CAC2 forma {} capsula {}", g_forma_cac2, g_caps_cac2[g_forma_cac2]);
        }
        p &= ~(kA | kLeft | kRight);
      }
    } else g_forma_cac2 = 0;
    if (EsFilaFotoCac2(g_fila) && (p & (kA | kLeft | kRight)) && !(p & kB)) {
      if (p & kA) h.foto_pose = (h.foto_pose + 1) % 3;
      else if (h.foto_pose) h.foto_yaw = std::clamp(h.foto_yaw + d * 10, -40, 40);
      REXLOG_INFO("dbz3 heroe: CAC2 retrato pose {} giro {}", h.foto_pose, h.foto_yaw);
      p &= ~(kA | kLeft | kRight);
    }
    if (d && (p & (kLeft | kRight))) {
      const int b3 = ZonaB3Fila(g_fila);
      if (g_fila == FilaReceta()) {
        g_receta_sel = (g_receta_sel + d + C::kMaxRecetas) % C::kMaxRecetas;
        g_receta_armada = -1;
        g_receta_aviso.clear();
      } else if (g_fila >= FilaReceta() && g_fila < FilaBotones()) {
        // Acciones de código: no tienen flechas.
      } else if (b3 >= 0) {
        MoverPiezaB3(h, b3, d);
      } else
      if (g_fila == FilaBotones()) {
        const int nb_n = NumBotones();
        g_boton = (g_boton + d + nb_n) % nb_n;
      } else if (Xv2() && g_fila < 7) {   // C4: cambia de pieza (la 1.a vez genera el modelo: ~6-12 s)
        const auto lst = H::PiezasXv2(H::Cuerpo(), g_fila);
        if (!lst.empty()) {
          int k = 0;
          for (size_t i = 0; i < lst.size(); ++i)
            if (lst[i] == h.xv2[g_fila]) k = int(i);
          h.xv2[g_fila] = lst[size_t((k + d + int(lst.size())) % int(lst.size()))];
          REXLOG_INFO("dbz3 heroe: XV2: {} = modelo {}", H::NombreTipoXv2(g_fila), h.xv2[g_fila]);
        }
      } else if (g_edit && g_fila == 3 && !CuerpoUT()) {
        h.cara = (h.cara + d + kNumRostros) % kNumRostros;
        REXLOG_INFO("dbz3 heroe: Edit: rostro {} ({})", h.cara, kRostros[h.cara][1]);
      } else if (g_edit && g_fila == 0) {
        if (PeloComunCac2()) {
          const int np = kNumPelos + (DonantesPeloCac2() ? int(std::size(kPelosCac2)) : 0);
          int i = std::clamp(h.pelo, 0, kNumPelos - 1);
          if (DonantesPeloCac2()) for (int j = 0; j < int(std::size(kPelosCac2)); ++j)
            if (kPelosCac2[j].first == h.pelo_fid_cac2) i = kNumPelos + j;
          i = (i + d + np) % np;
          h.pelo = i < kNumPelos ? i : 0;
          h.pelo_fid_cac2 = i < kNumPelos ? -1 : kPelosCac2[i - kNumPelos].first;
          REXLOG_INFO("dbz3 heroe: CAC2 pelo legado {} fid {}", h.pelo, h.pelo_fid_cac2);
        } else {
        const int np = H::Xv2PiezasActivas() ? 10 : kNumPelos;   // piezas UT: 10 (los nativos 8-10 son solo B3)
        do h.pelo = (h.pelo + d + np) % np;
        while (h.pelo != 0 && !H::Xv2PiezasActivas() && !*kPelosNativos[h.pelo]);
        }
      } else if (!g_edit && g_fila == 0) {
        const int nt = std::max(1, H::NumTrajes());
        H::PonerTraje((H::Traje() + d + nt) % nt);
        PedirVista(H::Cuerpo(), H::Traje());
        REXLOG_INFO("dbz3 heroe: Create: traje {} / {}", H::Traje() + 1, nt);
      } else if (EsFilaQuitar(g_fila)) {
        if (const int nt = TirasActuales().base) g_parte = (std::min(g_parte, nt - 1) + d + nt) % nt;
      } else if (EsFilaMechon(g_fila)) {
        if (const int nm = int(TirasActuales().mechones.size())) g_mechon = (std::min(g_mechon, nm - 1) + d + nm) % nm;
      } else if (EsFilaTex(g_fila)) {
        if (const int nt = int(TcLista().size())) g_tc_tex = (std::min(g_tc_tex, nt - 1) + d + nt) % nt;
      } else if (EsFilaEnt(g_fila)) {
        g_tc_ent = (g_tc_ent + d + 6) % 6;
      } else if (!g_edit && g_fila == FilaLogo()) {
        h.logo = (h.logo + d + kNumLogos) % kNumLogos;
      } else if (!g_edit && g_fila == FilaLogo() + 1) {
        h.scouter = !h.scouter;
      }
    }
    if (!pestanas && (p & (nueva ? kBackV2 : kY)) && !Xv2()) {   // Create <-> Edit (UT: dos pantallas con la misma vista previa); se conserva lo cambiado (aspecto nuevo: BACK)
      g_edit = !g_edit;
      g_fila = g_boton = 0;
      REXLOG_INFO("dbz3 heroe: pasa a {}", g_edit ? "Edit" : "Create");
    }
    if (p & kB) {
      Descartar();
      REXLOG_INFO("dbz3 heroe: {}: atras (sin guardar)", g_edit ? "Edit" : "Create");
      return Accion::kVolver;
    }
    bool receta_consumida = false;
    if (g_fila == FilaReceta() && (p & kX)) {
      g_nom_receta = g_receta_sel;
      g_nom_abierto = true;
      g_nom_txt = H::RecetaLlena(g_receta_sel) ? H::RecetaNombre(g_receta_sel) : T::T("Traje", "Costume");
      g_nom_cur = 0;
      receta_consumida = true;
    } else if (g_fila >= FilaReceta() && g_fila < FilaBotones() && (p & kA)) {
      receta_consumida = true;
      bool aplicada = false;
      if (g_fila == FilaReceta()) {
        const auto& ficha = h.recetas[g_receta_sel].ficha;
        if (ficha.empty()) g_receta_aviso = T::T("Ese hueco está vacío.", "This slot is empty.");
        else if (FichaXv2NoDisponible(ficha)) g_receta_aviso = T::T("El traje necesita piezas XV2.", "This costume needs XV2 parts.");
        else if (g_receta_armada != g_receta_sel) {
          g_receta_armada = g_receta_sel;
          g_receta_aviso = T::T("A otra vez: aplica y guarda el traje.", "A again: apply and save the costume.");
        } else {
          aplicada = H::RecetaAplicar(g_receta_sel);
          g_receta_armada = -1;
          if (!aplicada) g_receta_aviso = T::T("No se puede aplicar ese cuerpo.", "That body cannot be applied.");
        }
      } else if (g_fila == FilaReceta() + 2 && !g_codigo_ficha.empty()) {
        const std::string ficha = std::move(g_codigo_ficha);
        g_codigo_ficha.clear();
        if (FichaXv2NoDisponible(ficha)) g_receta_aviso = T::T("El código necesita piezas XV2.", "This code needs XV2 parts.");
        else {
          aplicada = H::AplicarFicha(ficha);
          if (!aplicada) g_receta_aviso = T::T("No se puede aplicar ese cuerpo.", "That body cannot be applied.");
        }
      } else {
        PedirCodigo(g_fila == FilaReceta() + 2);
      }
      if (aplicada) {
        PedirVista(H::Cuerpo(), H::Traje());
        PedirCombate();
        return Accion::kConfirmar;
      }
    }
    int boton = -1;   // boton pulsado de la fila de botones (0 Confirmar, 1 Atras, 2 Reset, 3 Nombre, 4 Exportar, 5 Importar)
    if ((p & kX) && (EsFilaTex(g_fila) || EsFilaEnt(g_fila))) {
      const auto& lista = TcLista();
      if (!lista.empty()) {
        const int ti = lista[std::min(g_tc_tex, int(lista.size()) - 1)].tex;
        if (EsFilaTex(g_fila)) for (int i = 0; i < 6; ++i) g_tc.erase({ti, i});
        else g_tc.erase({ti, g_tc_ent});
      }
      p &= ~kX;
    }
    if (!receta_consumida && (p & kA)) {
      if (H::Materiales17() && EsFilaTex(g_fila)) {
        AbrirMateriales17(h);
      } else if (const C::Hsv* color = ColorDeFila(g_fila)) {
        g_panel = g_fila;
        g_canal = 0;
        g_panel_antes = g_panel_texcol = *color;
        REXLOG_INFO("dbz3 heroe: {}: panel de color de {}", g_edit ? "Edit" : "Create", NombreColor(g_fila));
      } else if (g_fila == FilaBotones()) {
        boton = g_boton;
      } else if (EsFilaQuitar(g_fila) && TirasActuales().base) {
        AlternarParte();
      } else if (EsFilaMechon(g_fila) && !TirasActuales().mechones.empty()) {
        AlternarMechon();
      } else {
        if (filas_cac2.empty()) g_fila = std::min(g_fila + 1, n - 1);
        else {
          const auto i = std::find(filas_cac2.begin(), filas_cac2.end(), g_fila);
          if (i != filas_cac2.end() && i + 1 != filas_cac2.end()) g_fila = *(i + 1);
        }   // n - 1 = FilaBotones() con el aspecto de siempre
      }
    }
    if (!receta_consumida && nueva && boton < 0 && !(p & kA)) {   // HEROE-V05: los botones, en el mando (barra de ayuda de la maqueta 01)
      if (p & kRb) boton = 0;
      else if (p & kStartV2) boton = 2;
      else if (p & kX) boton = 3;
      else if (p & kY) boton = 4;
      else if ((p & kLsV2) && REXCVAR_GET(dbz3_heroe_importar)) boton = 5;
      if (boton == 4 || boton == 5) g_ayuda_v2 = boton;   // su resultado va en la linea de ayuda
    }
    if (boton >= 0) {
      {
        if (boton == 0) {
          if (const TopeInfo* ti = TopeClave(ClaveFicha(ArgsVista(H::Cuerpo(), H::Traje()))); ti && ti->excedido) {
            REXLOG_WARN("dbz3 heroe: CAC-13: Confirmar bloqueado por el tope ({}/{})", ti->ventanas, ti->tope);
            return Accion::kNada;   // la mezcla no cabe: no se monta nada (el aviso ambar dice que quitar)
          }
          REXLOG_INFO("dbz3 heroe: {}: confirmar (traje {}, partes {} | {} | {} | {}, logo {}, scouter {}, pelo {}, "
                      "colores de pelo {} | {})",
                      g_edit ? "Edit" : "Create", H::Traje() + 1, HsvTxt(h.parte[0]), HsvTxt(h.parte[1]),
                      HsvTxt(h.parte[2]), HsvTxt(h.parte[3]), h.logo, h.scouter, h.pelo, HsvTxt(h.pelo_c1),
                      HsvTxt(h.pelo_c2));
          PedirVista(H::Cuerpo(), H::Traje());   // la vista previa del traje guardado (si aun no esta)
          PedirCombate();                        // studies 8 fase 2 (H2): el traje de combate con los colores
          return Accion::kConfirmar;
        }
        if (boton == 1) {
          Descartar();
          REXLOG_INFO("dbz3 heroe: {}: atras (sin guardar)", g_edit ? "Edit" : "Create");
          return Accion::kVolver;
        }
        if (boton == 4) {
          PedirExportar();
          return Accion::kNada;
        }
        if (boton == 5 && REXCVAR_GET(dbz3_heroe_importar)) {
          AbrirImportar();
          return Accion::kNada;
        }
        if (boton == 3) {
          g_nom_abierto = true;
          g_nom_txt = H::Nombre();
          g_nom_cur = 0;
          return Accion::kNada;
        }
        Restablecer();
      }
    }
    if (g_panel >= 0 || g_p17.abierta) {
      g_aspecto_antes_panel = std::move(antes_lista);
    } else if (antes_lista) {
      TcSincronizar(h);
      RegistrarAspecto(antes_lista, h);
    }
  }
  // --- dibujo ---
  if (nueva) {
    DibujarCreadorV2(dl, a, t);
    if (std::getenv("DBZ3_TEST_VISTA_VERTICES"))
      REXLOG_INFO("dbz3 vista R18: final frame={} vertices={} indice={}",
                  ImGui::GetFrameCount(), dl->VtxBuffer.Size, dl->_VtxCurrentIdx);
    return Accion::kNada;
  }
  S::BannerBeta(dl, a, g_edit ? T::T("Editar héroe", "Edit hero") : T::T("Crear héroe", "Create hero"));
  if (!g_edit && (!H::Xv2Instalado() || !H::Xv2PiezasActivas()))   // C8b: aviso claro; las piezas XV2 son opcionales
    Texto(dl, a, 578, 826, D::kSmall,   // bajo la ayuda: arriba lo tapaban el banner y los marcos
          H::Xv2Instalado()
              ? T::T("Piezas XV2 desactivadas: héroe base de B3 (se activan en el launcher)",
                     "XV2 parts off: B3 base hero (enable them in the launcher)")
              : T::T("Xenoverse 2 no instalado: héroe base de B3 (personajes nativos)",
                     "Xenoverse 2 not installed: B3 base hero (native characters)"),
          false, true, 1100);
  // esquina: ficha y nivel
  {
    constexpr float c0 = D::kCornerY0, c1 = D::kTop - 24;
    Marco(dl, a, D::kCornerX0 + 40, c0, kLx1, c1);
    const std::string s = std::string(T::T("Ficha ", "Slot ")) + Num(C::SlotActivo() + 1) + "/" + Num(C::NumSlots()) +
                          "   " + H::Nombre();
    Texto(dl, a, (D::kCornerX0 + 40 + kLx1) * 0.5f, (c0 + c1) * 0.5f, D::kRow, s, true, false, kLx1 - D::kCornerX0 - 100);
  }
  DibujarVista(dl, a, t);
  DibujarLista(dl, a, t);
  DibujarImportar(dl, a, t);
  if (g_panel >= 0) DibujarPanel(dl, a, t);
  if (g_nom_abierto) {
    NombreDibujo(dl, a, t);
    S::HelpLine(dl, a.At(600, 868), 38 * a.s,
                {{"A", T::T("Escribir", "Type")}, {"X", T::T("Borrar", "Delete")}, {"Y", T::T("Aceptar", "OK")}, {"B", T::T("Cancelar", "Cancel")}});
  } else if (g_panel >= 0) {
    S::HelpLine(dl, a.At(600, 868), 38 * a.s,
                {{"A", T::T("Aceptar", "OK")}, {"B", T::T("Cancelar", "Cancel")}});
  } else {
    S::HelpLine(dl, a.At(600, 868), 38 * a.s,
                {{"A", T::T("Elegir", "Select")},
                 {"B", T::T("Atrás", "Back")},
                 {"LB", T::T("Girar", "Rotate")},
                 {"RB", T::T("Girar", "Rotate")},
                 {"Y", g_edit ? T::T("Crear", "Create") : T::T("Editar (pelo)", "Edit (hair)")}});
  }
  return Accion::kNada;
}

// ---------------------------------------------------------------------------------------------------------------------
// Movesets (studies 8 fase 2): elegir de quien son los golpes sin salir del modo
// ---------------------------------------------------------------------------------------------------------------------
namespace {
std::vector<int> g_movs;
int g_mov_cur = 0, g_mov_scroll = 0, g_mov_antes = -1;
int g_mov_tab = 0;   // DS144: 0 = movesets (donantes), 1 = estilos de lucha
std::vector<int> g_estilos;
int g_est_cur = 0, g_est_scroll = 0;
constexpr int kMovCols = 5, kMovFilas = 4;
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Tecnicas (studies 5 C2): inventario del heroe con la carta de cada tecnica (tecnicas_cartas.py las arma sola con las
// piezas del juego: nombre, duenyo, descripcion, coste de ki y la cara del luchador). 4 huecos equipables.
// ---------------------------------------------------------------------------------------------------------------------
namespace {
int g_tab = 0;   // 0 moveset, 1 tecnicas
std::vector<int> g_tec_inv;
int g_tec_cur = 0, g_tec_scroll = 0, g_tec_hueco = 0;
bool g_tec_cambio = false;
int g_pregunta_cap = 0;   // C4 (decision C2 #3): tecnica de otro donante pendiente de "cambiar el moveset?" (0 = sin pregunta)
constexpr int kTecFilas = 6;
std::map<std::string, Tira> g_cartas;   // textura por carta (se relee si el fichero cambia)

fs::path CartaPng(int cap) { return Juego() / "ingame_ui" / "heroe_tecnicas" / ("cap_" + Num(cap) + ".png"); }
fs::path CartasListo() { return Juego() / "ingame_ui" / "heroe_tecnicas" / "listo.txt"; }
S::Pic PicPng(Tira& t, const fs::path& f, double ahora, double cada) {
  if (ahora - t.visto < cada) return t.pic;
  t.visto = ahora;
  std::error_code ec;
  const auto cuando = fs::last_write_time(f, ec);
  if (ec) return t.pic;
  if (t.pic && cuando == t.cuando) return t.pic;
  auto* drawer = dbz3::launcher::PreviewDrawer();
  if (!drawer) return t.pic;
  std::ifstream in(f, std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0;
  const std::vector<uint8_t> px = rex::ui::DecodeImageRGBA(data.data(), data.size(), w, h);
  if (px.empty()) return t.pic;
  t.tex = drawer->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false, px.data());
  t.pic = t.tex ? S::Pic{reinterpret_cast<ImTextureID>(t.tex.get()), float(w), float(h)} : S::Pic{};
  t.cuando = cuando;
  return t.pic;
}
S::Pic PicPng(const fs::path& f, double ahora) {
  return PicPng(g_cartas[f.string()], f, ahora, 1.0);
}
int DonanteDe(int cap) {
  for (const H::Tecnica& t : H::CatalogoTecnicas())
    if (t.cap == cap) {
      const int efectivo = H::DonanteEfectivo();
      return t.Dueno(efectivo) && H::TecnicaActiva(cap) ? efectivo : t.donante;
    }
  return -1;
}
void ReordenarTecnicas() {
  g_tec_inv = H::InventarioTecnicas();
  const int ef = H::DonanteEfectivo();
  std::stable_sort(g_tec_inv.begin(), g_tec_inv.end(), [ef](int x, int y) {
    const int dx = DonanteDe(x), dy = DonanteDe(y);
    if ((dx == ef) != (dy == ef)) return dx == ef;
    return dx != dy ? dx < dy : x < y;
  });
}
void AsegurarCartas() {
  std::error_code ec;
  // d: si un roster.toml de mods/ es mas nuevo que listo.txt (se montaron capsulas >= 596), se rehacen las cartas
  if (fs::exists(CartasListo(), ec)) {
    const auto listo = fs::last_write_time(CartasListo(), ec);
    bool viejo = false;
    for (const auto& m : fs::directory_iterator(Juego() / "mods", ec)) {
      const fs::path r = m.path() / "roster.toml";
      std::error_code e2;
      if (fs::exists(r, e2) && fs::last_write_time(r, e2) > listo) viejo = true;
    }
    if (!viejo) return;
  }
  const std::string caras = H::ArgsCartas();
  GenerarCon("cartas", L"--caras " + std::wstring(caras.begin(), caras.end()), CartasListo(), "tecnicas_cartas.py");
}
// Cabecera de la carta (cara + nombre + orbes de ki) = las primeras 104 filas de la imagen.
void CabeceraCarta(ImDrawList* dl, const S::Area& a, int cap, float x0, float y0, float w, double ahora, int alfa = 255) {
  const S::Pic pic = PicPng(CartaPng(cap), ahora);
  const float h = w * 104.0f / std::max(pic.w, 468.0f);   // la carta mide lo que miden sus tiras de texto (>= 584 px)
  if (pic) S::DrawPic(dl, pic, a.At(x0, y0), a.At(x0 + w, y0 + h), ImVec4(0, 0, pic.w, 104), IM_COL32(255, 255, 255, alfa));
  else dl->AddRectFilled(a.At(x0, y0), a.At(x0 + w, y0 + h), IM_COL32(28, 74, 88, alfa), 8 * a.s);
}
// Capa visual propia del Heroe: piezas nativas de los menus recoloreadas en marcha (heroe_arte.py, del data_usi del jugador).
fs::path ArteListo() { return Juego() / "ingame_ui" / "heroe_arte" / "listo.txt"; }
S::Pic Arte(const char* nombre, double ahora) {
  std::error_code ec;
  if (!fs::exists(ArteListo(), ec)) {
    GenerarCon("arte", L"", ArteListo(), "heroe_arte.py");
    return {};
  }
  return PicPng(Juego() / "ingame_ui" / "heroe_arte" / (std::string(nombre) + ".png"), ahora);
}
}  // namespace

#include "heroe_v2_tecnicas.inl"   // HEROE-TEC: Tecnicas v2 (maqueta 6, dbz3_heroe_tecnicas_v2)

Accion TecnicasUI(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  if (REXCVAR_GET(dbz3_heroe_tecnicas_v2)) return TecnicasV2UI(dl, a, t, p);
  const double ahora = dbz3::RelojT(ImGui::GetTime());
  static bool tipos_aplicados = false;
  if (!tipos_aplicados && H::TiposListos()) {   // C4: al llegar los tipos, la lista se rehace sin las tecnicas sin entrada
    tipos_aplicados = true;
    ReordenarTecnicas();
  }
  if (g_tec_inv.empty() && !H::InventarioTecnicas().empty()) ReordenarTecnicas();
  const int n = int(g_tec_inv.size());
  if (n > 0) {
    g_tec_cur = std::clamp(g_tec_cur, 0, n - 1);
    if (p & kUp) g_tec_cur = (g_tec_cur + n - 1) % n;
    if (p & kDown) g_tec_cur = (g_tec_cur + 1) % n;
  }
  if (g_tec_cur < g_tec_scroll) g_tec_scroll = g_tec_cur;
  if (g_tec_cur >= g_tec_scroll + kTecFilas) g_tec_scroll = g_tec_cur - kTecFilas + 1;
  if (p & (kLb | kLeft)) g_tec_hueco = (g_tec_hueco + 2) % 3;
  if (p & (kRb | kRight)) g_tec_hueco = (g_tec_hueco + 1) % 3;
  if (g_pregunta_cap > 0) {   // el aviso Si/No manda: A = cambia el moveset a ese donante, B = se queda marcada "no disponible"
    const int cap = g_pregunta_cap;
    if (p & (kA | kB)) {
      const int don = DonanteDe(cap);
      if (p & kA) {
        H::PonerMoveset(don == H::EstiloId() ? -1 : don);
        ReordenarTecnicas();
        REXLOG_INFO("dbz3 heroe: tecnica {}: moveset cambiado al donante {} (Si)", cap, don);
      } else {
        REXLOG_INFO("dbz3 heroe: tecnica {}: queda equipada pero no disponible con este estilo (No)", cap);
      }
      const int tipo = H::TipoTecnica(cap, H::DonanteEfectivo());
      if (tipo >= 0) H::Equipar(tipo, cap);
      g_tec_hueco = std::max(0, tipo);
      g_tec_cambio = true;
      g_pregunta_cap = 0;
    }
    p = 0;
  }
  if ((p & kA) && n > 0) {
    const int cap = g_tec_inv[size_t(g_tec_cur)];
    bool ya = false;
    for (int k = 0; k < 3; ++k) ya = ya || H::Equipada(k) == cap;
    if (ya) {   // A sobre una carta ya equipada la quita
      for (int k = 0; k < 3; ++k)
        if (H::Equipada(k) == cap) H::Equipar(k, 0);
    } else if (const int tipo = H::TipoTecnica(cap, H::DonanteEfectivo()); tipo >= 0) {
      if (DonanteDe(cap) != H::DonanteEfectivo()) {
        g_pregunta_cap = cap;   // C4: tecnica de otro donante: primero se pregunta (arriba)
      } else {
        H::Equipar(tipo, cap);   // va al hueco de su tipo (definitiva / E+derecha / E+izquierda); la anterior sale
        g_tec_hueco = tipo;
      }
    }
    g_tec_cambio = true;
  }
  if (p & kX) {
    H::Equipar(g_tec_hueco, 0);
    g_tec_cambio = true;
  }
  if (p & kY) {
    g_tab = 0;
    return Accion::kNada;
  }
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: tecnicas: atras ({})", g_tec_cambio ? "con cambios" : "sin cambios");
    return g_tec_cambio ? Accion::kConfirmar : Accion::kVolver;
  }
  AsegurarCartas();
  if (!H::TiposListos()) GenerarCon("tipos", L"", H::TiposRutaPublica(), "tecnicas_tipo.py");   // C4: tipo de cada tecnica
  FondoSinAnfitrion(dl, a);
  S::BannerBeta(dl, a, T::T("Técnicas", "Techniques"));
  const int ef = H::DonanteEfectivo();
  // lista (izquierda): cabecera de cada carta
  constexpr float lx0 = 40, lx1 = 420, ly0 = 172;
  const float rowh = 340.0f * 104.0f / 584.0f + 16;
  Marco(dl, a, lx0, ly0, lx1, ly0 + 24 + rowh * kTecFilas, 235);
  if (n == 0) {
    Texto(dl, a, (lx0 + lx1) * 0.5f, ly0 + 120, D::kRow, T::T("Sin técnicas todavía.", "No techniques yet."), false, true, 330);
    Texto(dl, a, (lx0 + lx1) * 0.5f, ly0 + 160, D::kSmall,
          T::T("Se abren con los estilos de lucha.", "They unlock with fighting styles."), false, true, 330);
  }
  for (int k = 0; k < kTecFilas; ++k) {
    const int i = g_tec_scroll + k;
    if (i >= n) break;
    const int cap = g_tec_inv[size_t(i)];
    const float y = ly0 + 12 + rowh * k;
    const bool sel = i == g_tec_cur;
    bool eq = false;
    for (int h = 0; h < 3; ++h) eq = eq || H::Equipada(h) == cap;
    if (sel) D::Stripe(dl, a, lx0 + 6, y - 2, lx1 - 6, y + rowh - 10, t);
    CabeceraCarta(dl, a, cap, lx0 + 20, y, 340, ahora, sel || eq ? 255 : 170);
    if (eq) Texto(dl, a, lx1 - 30, y + 14, D::kSmall, "E", true, false);
    if (DonanteDe(cap) != ef) Texto(dl, a, lx0 + 270, y + 60, 14, T::T("no disponible con este estilo", "not available with this style"), false, true, 110);
  }
  if (g_tec_scroll > 0) S::Arrows(dl, a.At(lx0 + 120, ly0 - 30), a.At(lx0 + 250, ly0 + 5), 24 * a.s, t);
  // carta elegida (centro/derecha)
  constexpr float cx0 = 450, cx1 = 1130;
  Marco(dl, a, cx0, ly0, cx1, ly0 + 24 + rowh * kTecFilas, 235);
  if (n > 0) {
    const int cap = g_tec_inv[size_t(g_tec_cur)];
    const S::Pic pic = PicPng(CartaPng(cap), ahora);
    if (pic) {
      const float maxw = 380, maxh = rowh * kTecFilas - 8;
      const float sc = std::min(maxw / pic.w, maxh / pic.h);
      S::DrawPic(dl, pic, a.At(cx0 + 20, ly0 + 14), a.At(cx0 + 20 + pic.w * sc, ly0 + 14 + pic.h * sc));
    } else {
      Texto(dl, a, cx0 + 210, ly0 + 140, D::kRow,
            Fallida("cartas") ? T::T("No se pudieron hacer las cartas (hace falta Python).", "The cards could not be made (Python is needed).")
            : Generando2("cartas") ? std::string(T::T("Generando las cartas", "Making the cards")) + std::string(size_t(int(ImGui::GetTime() * 3) % 4), '.')
                                   : T::T("Carta no disponible", "Card unavailable"),
            false, true, 380);
    }
    const int don = DonanteDe(cap);
    Cara(dl, a, don, cx0 + 540, ly0 + 80, 96);
    Texto(dl, a, cx0 + 540, ly0 + 150, D::kSmall, M::Nombre(don), true, false, 200);
    Texto(dl, a, cx0 + 540, ly0 + 200, D::kSmall,
          don == ef ? std::string(T::T("Usable con tu estilo", "Usable with your style"))
                    : std::string(T::T("Pide el estilo de ", "Needs the style of ")) + M::Nombre(don),
          false, don != ef, 200);
  }
  // huecos equipados (abajo)
  const float hy = ly0 + 24 + rowh * kTecFilas + 14;
  static const char* kNomHueco[3] = {"Definitiva", "E + →", "E + ←"};   // C4: definitiva, E+derecha, E+izquierda
  for (int k = 0; k < 3; ++k) {
    const float x0 = 40 + k * 370, x1 = x0 + 352;
    Marco(dl, a, x0, hy, x1, hy + 104, k == g_tec_hueco ? 240 : 190);
    if (k == g_tec_hueco) D::Stripe(dl, a, x0 + 4, hy + 6, x1 - 4, hy + 98, t);
    const int cap = H::Equipada(k);
    if (cap > 0) CabeceraCarta(dl, a, cap, x0 + 10, hy + 6, 332, ahora, H::TecnicaActiva(cap) ? 255 : 120);
    else
      Texto(dl, a, (x0 + x1) * 0.5f, hy + 52, D::kSmall,
            std::string(k == 0 ? T::T("Definitiva", "Ultimate") : kNomHueco[k]) + " - " + T::T("libre", "empty"), false, true, 330);
  }
  if (g_pregunta_cap > 0) {   // aviso Si/No (C2 #3)
    const int don = DonanteDe(g_pregunta_cap);
    Marco(dl, a, 300, 330, 900, 540, 250);
    Texto(dl, a, 600, 372, D::kRow, std::string(T::T("Esta técnica es de ", "This technique belongs to ")) + M::Nombre(don), true, false, 540);
    Texto(dl, a, 600, 420, D::kSmall, T::T("¿Cambiar tu moveset a ese estilo?", "Switch your moveset to that style?"), false, false, 540);
    Texto(dl, a, 600, 470, D::kSmall * 0.85f,
          T::T("Si no, queda equipada pero no disponible con este estilo.", "If not, it stays equipped but unavailable with this style."), false, true, 560);
    S::HelpLine(dl, a.At(600, 520), 38 * a.s, {{"A", T::T("Sí", "Yes")}, {"B", T::T("No", "No")}});
  }
  S::HelpLine(dl, a.At(600, 868), 38 * a.s,
              {{"A", T::T("Equipar/quitar", "Equip/remove")}, {"X", T::T("Vaciar", "Clear")},
               {"LB", T::T("Hueco", "Slot")}, {"Y", T::T("Técnicas", "Moveset")}, {"B", T::T("Atrás", "Back")}});
  return Accion::kNada;
}


// ---------------------------------------------------------------------------------------------------------------------
// C4: pantalla principal del Heroe (antes: lista centrada de 9 lineas + carrusel + bocadillo del Kaio). Misma estructura que
// Crear/Tecnicas (marcos naranja de B3, banner, franja de seleccion): vista previa del MODELO a la izquierda, tres secciones
// (Apariencia / Combate / Progreso) con etiqueta y valor a la derecha, y un panel de detalle legible debajo.
// ---------------------------------------------------------------------------------------------------------------------
namespace {
std::vector<std::string> Parrafo(const std::string& s, size_t ancho) {   // corte por palabras (la fuente de B3 mide ~13 px a tam. 26)
  std::vector<std::string> out;
  std::istringstream ls(s);
  std::string linea;
  while (std::getline(ls, linea)) {
    std::istringstream ws(linea);
    std::string w, cur;
    while (ws >> w) {
      if (!cur.empty() && cur.size() + 1 + w.size() > ancho) {
        out.push_back(cur);
        cur.clear();
      }
      cur += (cur.empty() ? "" : " ") + w;
    }
    out.push_back(cur);
  }
  return out;
}
}  // namespace

void Principal(ImDrawList* dl, const S::Area& a, float t, int cursor, int tab_sel, const std::vector<std::string>& filas,
               const std::string& detalle, const std::string& aviso, const std::string& ficha) {
  const bool nueva = REXCVAR_GET(dbz3_heroe_ui_nueva);   // HEROE-V04: apagado = el dibujo de siempre
  if (!nueva) {
    FondoSinAnfitrion(dl, a);
    S::Banner(dl, a, T::T("Tu héroe (beta)", "Your hero (beta)"), ficha.c_str());
  }
  // la vista previa gira sola; stick derecho / Q-E la giran a mano (como en Crear)
  {
    static double antes = 0;
    const double ahora = dbz3::RelojT(ImGui::GetTime());
    const float dt = float(std::clamp(ahora - antes, 0.0, 0.1));
    antes = ahora;
    float stick = 0.0f;   // ponytail: el stick derecho no llega por esta version de ImGui (solo Q/E)
    if (ImGui::IsKeyDown(ImGuiKey_E)) stick += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) stick -= 1.0f;
    g_giro += (std::abs(stick) > 0.15f ? stick * 2.0f : 0.667f) * dt;
  }
  if (nueva) {   // HEROE-V04: maqueta 03 (heroe_v2_ficha.inl); mismas filas, cursor y pestana
    DibujarFichaV2(dl, a, t, cursor, tab_sel, filas, detalle, aviso, ficha);
    return;
  }
  DibujarVista(dl, a, t);   // izquierda: el modelo del heroe girando, nombre, cuerpo y traje
  // --- pestanas (LB / RB): Apariencia / Combate / Progreso; cada una tiene 3 filas (0-2, 3-5, 6-8)
  static const char* kTabES[3] = {"APARIENCIA", "COMBATE", "PROGRESO"};
  static const char* kTabEN[3] = {"APPEARANCE", "COMBAT", "PROGRESS"};
  const int tab = std::clamp(tab_sel, 0, 2);
  constexpr float x0 = 440, x1 = 1130, ty0 = 150, ty1 = 196;
  const float tw = (x1 - x0) / 3.0f;
  for (int i = 0; i < 3; ++i) {
    const float a0 = x0 + tw * i + 4, a1 = x0 + tw * (i + 1) - 4;
    Fondo(dl, a, a0, ty0, a1, ty1, i == tab ? 240 : 150);
    if (i == tab) D::Stripe(dl, a, a0 + 2, ty0 + 3, a1 - 2, ty1 - 3, t);
    S::Frame(dl, a.At(a0, ty0), a.At(a1, ty1), a.s * 0.55f);
    Texto(dl, a, (a0 + a1) * 0.5f, (ty0 + ty1) * 0.5f, D::kRow * 0.9f, T::T(kTabES[i], kTabEN[i]), i == tab, i != tab, a1 - a0 - 20);
  }
  // --- filas de la pestana activa
  constexpr float y0 = 206, paso = 50;
  Marco(dl, a, x0, y0, x1, y0 + 5 * paso + 18, 235);
  static const int kFilasTab[3][4] = {{0, 1, 2, 10}, {3, 4, 5, 9}, {6, 7, -1, -1}};   // C4: la 9 = color del ki (Combate)
  for (int k = 0; k < 5; ++k) {
    const int f = k == 4 ? 8 : kFilasTab[tab][k];   // la 5.a ranura: "Empezar", fija en las tres pestanas
    if (f < 0 || f >= int(filas.size())) continue;
    const float cy = y0 + 9 + paso * (k + 0.5f);
    const bool sel = f == cursor;
    if (sel) D::Stripe(dl, a, x0 + 12, cy - paso * 0.5f + 4, x1 - 12, cy + paso * 0.5f - 4, t);
    std::string et = filas[size_t(f)], val;
    if (const size_t p = et.find(": "); p != std::string::npos) {
      val = et.substr(p + 2);
      et.resize(p);
    }
    if (f == 8) {   // Empezar: un solo texto grande
      Texto(dl, a, (x0 + x1) * 0.5f, cy, D::kHead, et + (val.empty() ? "" : ": " + val), sel, false, x1 - x0 - 60);
      continue;
    }
    const bool gris = f == 10 && !H::Xv2Instalado();   // piezas XV2: gris si no hay Xenoverse 2
    Texto(dl, a, x0 + 130, cy, D::kRow, et, sel, gris, 200);
    Texto(dl, a, x0 + 440, cy, D::kRow, val, sel, gris, 400);
    if (sel && !gris && f != 2 && f != 4 && f != 7) S::Arrows(dl, a.At(x0 + 236, cy - paso * 0.5f), a.At(x1 - 18, cy + paso * 0.5f), 30 * a.s, t);
  }
  // --- barras (como el panel de estadisticas de la pantalla de nivel de B3): vida, dano, resistencia, experiencia
  {
    const H::Barras b = H::Barras_();
    constexpr float by0 = 500, bh = 30, bx0 = x0 + 190, bx1 = x1 - 90;
    Marco(dl, a, x0, by0 - 14, x1, by0 + 4 * (bh + 10) + 6, 235);
    struct Fila { const char* es; const char* en; int v, m; std::string txt; };
    const Fila fs[4] = {{"VIDA", "HEALTH", b.vida, b.vida_max, Num(b.vida)},
                        {"DAÑO", "DAMAGE", b.dano, b.dano_max, Num(b.dano) + "%"},
                        {"GUARDIA", "GUARD", b.resist, b.resist_max, Num(b.resist) + "%"},
                        {"NIVEL", "LEVEL", b.xp, b.xp_max, std::string(T::T("Nv ", "Lv ")) + Num(b.nivel)}};
    for (int i = 0; i < 4; ++i) {
      const float cy = by0 + (bh + 10) * i + bh * 0.5f;
      Texto(dl, a, x0 + 100, cy, D::kSmall, T::T(fs[i].es, fs[i].en), false, false, 150);
      const ImVec2 p0 = a.At(bx0, cy - 11), p1 = a.At(bx1, cy + 11);
      dl->AddRectFilled(p0, p1, IM_COL32(8, 18, 40, 255), 4 * a.s);
      const float k = std::clamp(float(fs[i].v) / float(std::max(1, fs[i].m)), 0.0f, 1.0f);
      dl->AddRectFilled(p0, ImVec2(p0.x + (p1.x - p0.x) * k, p1.y), i == 3 ? IM_COL32(60, 200, 90, 255) : IM_COL32(40, 150, 235, 255), 4 * a.s);
      dl->AddRect(p0, p1, IM_COL32(232, 140, 32, 255), 4 * a.s, 0, 2 * a.s);
      Texto(dl, a, bx1 + 48, cy, D::kSmall * 0.9f, fs[i].txt, false, false, 90);
    }
  }
  // --- detalle (abajo): letra mas grande, blanca con perfil oscuro
  constexpr float dy0 = 768, dy1 = 852;
  Marco(dl, a, 40, dy0, x1, dy1, 240);
  const std::vector<std::string> ls = Parrafo(aviso.empty() ? detalle : aviso, 66);
  for (size_t i = 0; i < ls.size() && i < 3; ++i)
    Texto(dl, a, (40 + x1) * 0.5f, dy0 + 22 + 25.0f * float(i), D::kSmall * 0.82f, ls[i], !aviso.empty(), false, x1 - 90);
}

namespace {
std::vector<std::string> Parrafo(const std::string& s, size_t ancho);   // definida abajo (pantalla principal)
}

void AbrirMovesets(bool estilo) {
  g_tab = 0;
  g_mov_tab = estilo ? 1 : 0;   // DS144: A en la fila Estilo abre directo la pestana Estilo; LB/RB cambia
  g_tec_cambio = false;
  g_pregunta_cap = 0;
  if (const char* e = std::getenv("DBZ3_TEST_HEROE_TEC"); e && *e == '1') {   // pruebas: abre directo en Tecnicas
    g_tab = 1;
    g_tec_cur = g_tec_scroll = 0;
    ReordenarTecnicas();
  }
  g_movs = H::Movesets();
  g_mov_antes = H::MovesetEleccion();
  g_mov_cur = 0;
  for (size_t i = 0; i < g_movs.size(); ++i)
    if (g_movs[i] == g_mov_antes) g_mov_cur = int(i);
  g_mov_scroll = std::max(0, g_mov_cur / kMovCols - kMovFilas + 1);
  g_estilos = H::Estilos();
  g_est_cur = 0;
  for (size_t i = 0; i < g_estilos.size(); ++i)
    if (g_estilos[i] == H::EstiloId()) g_est_cur = int(i);
  g_est_scroll = std::max(0, g_est_cur / kMovCols - kMovFilas + 1);
  REXLOG_INFO("dbz3 heroe: pantalla de movesets abierta ({} movesets, {} estilos, actual {}, estilo {})", g_movs.size(),
              g_estilos.size(), g_mov_antes, H::EstiloId());
}

REXCVAR_DEFINE_BOOL(dbz3_heroe_aviso_formas_moveset, true, "DBZ3/Mods",
                    "Hero moveset: disclose form limits for swapped bodies");

Accion MovesetsUI(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  if (g_tab == 1) return TecnicasUI(dl, a, t, p);
  if (p & kY) {   // C2: Y pasa a la pestana Tecnicas (inventario y 4 huecos)
    g_tab = 1;
    g_tec_cur = g_tec_scroll = 0;
    g_tec_cambio = false;
    g_pregunta_cap = 0;
    ReordenarTecnicas();
    REXLOG_INFO("dbz3 heroe: tecnicas abiertas ({} en el inventario, donante {}, equipo {} {} {} {})", g_tec_inv.size(),
                H::DonanteEfectivo(), H::Equipada(0), H::Equipada(1), H::Equipada(2), H::Equipada(3));
    return Accion::kNada;
  }
  if (p & (kLb | kRb)) {   // DS144: LB/RB cambian entre la lista de movesets y la de estilos de lucha
    g_mov_tab = (g_mov_tab + 1) % 2;
    p &= uint16_t(~(kLb | kRb));
  }
  const bool est = g_mov_tab == 1;
  const std::vector<int>& lista = est ? g_estilos : g_movs;
  int& cur = est ? g_est_cur : g_mov_cur;
  int& scroll = est ? g_est_scroll : g_mov_scroll;
  const int n = std::max(1, int(lista.size()));
  cur = std::clamp(cur, 0, n - 1);
  if (p & kLeft) cur = (cur + n - 1) % n;
  if (p & kRight) cur = (cur + 1) % n;
  if (p & kUp) cur = std::max(0, cur - kMovCols);
  if (p & kDown) cur = std::min(n - 1, cur + kMovCols);
  const int fila = cur / kMovCols;
  if (fila < scroll) scroll = fila;
  if (fila >= scroll + kMovFilas) scroll = fila - kMovFilas + 1;
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: {}: atras (sigue estilo {}, moveset {})", est ? "estilos" : "movesets", H::EstiloId(),
                g_mov_antes);
    return Accion::kVolver;
  }
  if ((p & kA) && !lista.empty()) {
    const int id = lista[size_t(cur)];
    if (est) {
      H::PonerEstilo(id);
      REXLOG_INFO("dbz3 heroe: estilo {} ({}) elegido en la lista (combate siguiente; la ficha lo guarda al confirmar)",
                  id, M::Nombre(id));
    } else {
      H::PonerMoveset(id);
    }
    return Accion::kConfirmar;
  }
  FondoSinAnfitrion(dl, a);
  S::BannerBeta(dl, a, T::T("Elige estilo o moveset", "Choose style or moveset"));
  // pestanas (DS144): Moveset / Estilo; la franja marca la activa, como en la pantalla principal
  constexpr float tx0 = 40, tx1 = 760, ty0 = 140, ty1 = 180;
  const float tw = (tx1 - tx0) * 0.5f;
  static const char* const kTabEs[2] = {"MOVESET", "ESTILO"};
  static const char* const kTabEn[2] = {"MOVESET", "STYLE"};
  for (int i = 0; i < 2; ++i) {
    const float a0 = tx0 + tw * i + 4, a1 = tx0 + tw * (i + 1) - 4;
    Fondo(dl, a, a0, ty0, a1, ty1, i == g_mov_tab ? 240 : 150);
    if (i == g_mov_tab) D::Stripe(dl, a, a0 + 2, ty0 + 3, a1 - 2, ty1 - 3, t);
    S::Frame(dl, a.At(a0, ty0), a.At(a1, ty1), a.s * 0.55f);
    Texto(dl, a, (a0 + a1) * 0.5f, (ty0 + ty1) * 0.5f, D::kRow * 0.9f, T::T(kTabEs[i], kTabEn[i]), i == g_mov_tab,
          i != g_mov_tab, a1 - a0 - 20);
  }
  // rejilla (izquierda)
  constexpr float gx0 = 40, gx1 = 760, gy0 = 190, celda_w = (gx1 - gx0 - 2 * 24) / kMovCols, celda_h = 128;
  const float gy1 = gy0 + 2 * 24 + celda_h * kMovFilas;
  Marco(dl, a, gx0, gy0, gx1, gy1, 235);
  for (int k = 0; k < kMovFilas * kMovCols; ++k) {
    const int i = scroll * kMovCols + k;
    if (i >= int(lista.size())) break;
    const int id = lista[size_t(i)];
    const float cx = gx0 + 24 + celda_w * (k % kMovCols + 0.5f), cy = gy0 + 24 + celda_h * (k / kMovCols + 0.5f);
    const bool sel = i == cur, actual = est ? id == H::EstiloId() : id == g_mov_antes;
    if (sel) D::Stripe(dl, a, cx - celda_w * 0.5f + 2, cy - celda_h * 0.5f + 4, cx + celda_w * 0.5f - 2, cy + celda_h * 0.5f - 4, t);
    const int cara = est ? id : (id < 0 ? H::EstiloId() : H::MovesetRetrato(id));
    if (cara >= 0) Cara(dl, a, cara, cx, cy - 18, 64, sel || actual ? 255 : 170);
    std::string nombre = (!est && id < 0) ? std::string(T::T("Del estilo", "Style's own")) : H::MovesetNombre(id);
    if (est && H::EsMaestro(id)) nombre += " *";
    Texto(dl, a, cx, cy + 34, 18, nombre, sel, !sel, celda_w - 12);
    if (actual) Texto(dl, a, cx, cy - 56, 15, T::T("ACTUAL", "CURRENT"), true, false, celda_w - 10);
  }
  if (scroll > 0) S::Arrows(dl, a.At(gx0 + 300, gy0 - 30), a.At(gx0 + 420, gy0 + 10), 26 * a.s, t);
  // ficha del elegido (derecha)
  const int id = lista.empty() ? (est ? H::EstiloId() : -1) : lista[size_t(cur)];
  constexpr float px0 = 790, px1 = 1130, py0 = 190;
  const float py1 = gy1;
  Marco(dl, a, px0, py0, px1, py1, 235);
  const int cara = est ? id : (id < 0 ? H::EstiloId() : H::MovesetRetrato(id));
  if (cara >= 0) Cara(dl, a, cara, (px0 + px1) * 0.5f, py0 + 110, 130);
  std::string titulo = est ? std::string(M::Nombre(id)) : (id < 0 ? M::Nombre(H::EstiloId()) : H::MovesetNombre(id));
  if (est && H::EsMaestro(id)) titulo += T::T(" (maestro)", " (master)");
  Texto(dl, a, (px0 + px1) * 0.5f, py0 + 200, D::kHead, titulo, true, false, px1 - px0 - 60);
  if (est) {
    Texto(dl, a, (px0 + px1) * 0.5f, py0 + 240, D::kSmall, T::T("Estilo de lucha", "Fighting style"), false, false,
          px1 - px0 - 60);
    const std::vector<std::string> ls = Parrafo(H::DetalleEstilo(id), 34);
    float yy = py0 + 280;
    for (size_t i = 0; i < ls.size() && i < 7 && yy < py1 - 40; ++i, yy += 28)
      Texto(dl, a, (px0 + px1) * 0.5f, yy, D::kSmall * 0.85f, ls[i], false, false, px1 - px0 - 50);
    if (H::EsMaestro(id))
      Texto(dl, a, (px0 + px1) * 0.5f, py1 - 46, D::kSmall * 0.8f,
            T::T("Estilo maestro: da un bonus y su cápsula.", "Master style: gives a bonus and its capsule."), false,
            false, px1 - px0 - 50);
  } else {
    Texto(dl, a, (px0 + px1) * 0.5f, py0 + 240, D::kSmall,
          id < 0 ? std::string(T::T("(el moveset del estilo)", "(the style's own moveset)"))
                 : std::string(T::T("Cápsulas propias: ", "Own capsules: ")) + (cara < 0 ? std::string("—") : Num(H::CapsulasMoveset(cara))),
          false, false, px1 - px0 - 60);
    Texto(dl, a, (px0 + px1) * 0.5f, py0 + 290, D::kSmall, T::T("Golpes, combos y cámara:", "Moves, combos and camera:"),
          false, true, px1 - px0 - 60);
    Texto(dl, a, (px0 + px1) * 0.5f, py0 + 320, D::kSmall, T::T("los de este luchador.", "this fighter's."), false, true,
          px1 - px0 - 60);
    const bool aviso = REXCVAR_GET(dbz3_heroe_aviso_formas_moveset) && id >= 0 && id != H::EstiloId();
    Texto(dl, a, (px0 + px1) * 0.5f, py0 + 360, D::kSmall,
          aviso ? T::T("Vida y daño:", "Health and damage:") : T::T("Vida, daño y formas:", "Health, damage and forms:"),
          false, true, px1 - px0 - 60);
    Texto(dl, a, (px0 + px1) * 0.5f, py0 + 390, D::kSmall,
          std::string(T::T("los del estilo (", "the style's (")) + M::Nombre(H::EstiloId()) + ").", false, true,
          px1 - px0 - 60);
    if (aviso)
      Texto(dl, a, (px0 + px1) * 0.5f, py0 + 430, D::kSmall * 0.82f,
            T::T("Las formas dependen del cuerpo.\nSin montaje, un cuerpo distinto pelea en base.",
                 "Forms depend on the body.\nWithout a build, a different body fights in base."),
            false, true, px1 - px0 - 44);
  }
  Marco(dl, a, gx0, gy1 + 16, px1, gy1 + 74, 200);
  Texto(dl, a, (gx0 + px1) * 0.5f, gy1 + 45, D::kSmall,
        std::string(Num(cur + 1)) + " / " + Num(n) + "   " +
            T::T("Se aplica en el combate siguiente.", "Applies from the next fight."),
        false, false, px1 - gx0 - 70);
  S::HelpLine(dl, a.At(600, 868), 38 * a.s,
              {{"A", T::T("Elegir", "Select")}, {"LB", T::T("Cambiar lista", "Switch list")},
               {"RB", T::T("Cambiar lista", "Switch list")}, {"Y", T::T("Técnicas", "Techniques")},
               {"B", T::T("Atrás", "Back")}});
  return Accion::kNada;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fichas de heroe (K): lista de cac.toml, duplicar (X) y archivar (Y). A elige y vuelve a la creacion.
// ---------------------------------------------------------------------------------------------------------------------
namespace {
int g_slot_cur = 0;
}
void AbrirSlots() {
  g_slot_cur = C::SlotActivo();
  REXLOG_INFO("dbz3 heroe: fichas abiertas ({} activas, {} archivadas)", C::NumSlots(), C::NumArchivados());
}
Accion SlotsUI(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  // C2: selector propio de heroes: kMaxSlots (3) tarjetas; una vacia crea un heroe nuevo con A.
  const int n = std::max(1, C::NumSlots());
  const int total = C::kMaxSlots;
  g_slot_cur = std::clamp(g_slot_cur, 0, total - 1);
  if (p & (kLeft | kLb)) g_slot_cur = (g_slot_cur + total - 1) % total;
  if (p & (kRight | kRb)) g_slot_cur = (g_slot_cur + 1) % total;
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: fichas: atras");
    return Accion::kVolver;
  }
  if (p & kX) {
    if (g_slot_cur >= n) {
      REXLOG_INFO("dbz3 heroe: fichas: duplicar sobre una ficha vacia: nada");
    } else if (C::SlotsLlenos()) {
      REXLOG_WARN("dbz3 heroe: fichas: maximo {} heroes", C::kMaxSlots);
    } else {
      C::DuplicarSlot(g_slot_cur);
      g_slot_cur = C::NumSlots() - 1;
      REXLOG_INFO("dbz3 heroe: ficha duplicada: ahora {} heroes (activa {})", C::NumSlots(), g_slot_cur + 1);
    }
  }
  if (p & kY) {
    if (g_slot_cur >= n) {
      if (C::NumArchivados() > 0 && !C::SlotsLlenos()) {   // Y sobre una vacia recupera el ultimo archivado
        C::RestaurarArchivado(C::NumArchivados() - 1);
        g_slot_cur = C::NumSlots() - 1;
        REXLOG_INFO("dbz3 heroe: ficha restaurada: {} activas, {} archivadas", C::NumSlots(), C::NumArchivados());
      }
    } else if (C::NumSlots() <= 1) {
      REXLOG_WARN("dbz3 heroe: fichas: no se puede archivar el unico heroe");
    } else {
      C::ArchivarSlot(g_slot_cur);
      g_slot_cur = std::clamp(g_slot_cur, 0, C::NumSlots() - 1);
      REXLOG_INFO("dbz3 heroe: ficha archivada: quedan {} activas, {} archivadas", C::NumSlots(), C::NumArchivados());
    }
  }
  if (p & kA) {
    if (g_slot_cur >= C::NumSlots()) {
      const int nuevo = C::NuevoSlot();
      if (nuevo < 0) {
        REXLOG_WARN("dbz3 heroe: fichas: no se pudo crear (lleno)");
        return Accion::kNada;
      }
      g_slot_cur = nuevo;
      REXLOG_INFO("dbz3 heroe: heroe nuevo en la ficha {}/{}", g_slot_cur + 1, C::kMaxSlots);
      C::ElegirSlot(g_slot_cur);
      return Accion::kCrear;   // "Nueva partida": al crearlo se abre la creacion del heroe
    }
    C::ElegirSlot(g_slot_cur);
    REXLOG_INFO("dbz3 heroe: ficha elegida {}/{}", g_slot_cur + 1, C::NumSlots());
    return Accion::kConfirmar;
  }
  FondoSinAnfitrion(dl, a);
  S::BannerBeta(dl, a, T::T("Tus héroes", "Your heroes"));
  constexpr float gx0 = 40, gx1 = 1130, gy0 = 172, ch = 470;
  const float cw = (gx1 - gx0 - 2 * 20) / total;
  for (int i = 0; i < total; ++i) {
    const float x0 = gx0 + i * (cw + 20), y0 = gy0;
    const float x1 = x0 + cw, y1 = y0 + ch;
    const bool sel = i == g_slot_cur;
    if (i >= C::NumSlots()) {
      // tarjeta "Nueva partida": mismo aro dorado y franja verde (piezas nativas recoloreadas) con un + dentro
      Marco(dl, a, x0, y0, x1, y1, sel ? 235 : 150);
      if (sel) D::Stripe(dl, a, x0 + 2, y0 + 4, x1 - 2, y1 - 4, t);
      const double ahora = dbz3::RelojT(ImGui::GetTime());
      const float cx = (x0 + x1) * 0.5f, cy = y0 + 140;
      if (const S::Pic ag = Arte("agua", ahora))
        S::DrawPic(dl, ag, a.At(x0 + 24, y0 + 246), a.At(x1 - 24, y0 + 296), ImVec4(40, 20, 472, 108),
                   IM_COL32(255, 255, 255, sel ? 235 : 110));
      if (const S::Pic ar = Arte("anillo", ahora))
        S::DrawPic(dl, ar, a.At(cx - 160, cy - 160), a.At(cx + 160, cy + 160), ImVec4(0, 0, 0, 0),
                   IM_COL32(255, 255, 255, sel ? 255 : 120));
      Texto(dl, a, cx, cy, 120, "+", sel, !sel);
      Texto(dl, a, cx, y0 + 271, D::kHead, T::T("Nueva partida", "New game"), sel, !sel, cw - 30);
      Texto(dl, a, cx, y0 + 330, D::kSmall, T::T("Crea tu héroe", "Create your hero"), false, true, cw - 30);
      Texto(dl, a, cx, y0 + 384, D::kSmall, std::string(T::T("Ficha ", "Slot ")) + Num(i + 1) + "/" + Num(total), false, true,
            cw - 30);
      continue;
    }
    const bool act = i == C::SlotActivo();
    Marco(dl, a, x0, y0, x1, y1, sel ? 240 : 200);
    if (sel) D::Stripe(dl, a, x0 + 2, y0 + 4, x1 - 2, y1 - 4, t);
    const C::Heroe& h = C::Slot(i);
    {   // aro dorado (pieza nativa recoloreada) con la cara dentro; la franja verde de agua tras el nombre
      const double ahora = dbz3::RelojT(ImGui::GetTime());
      const float cx = (x0 + x1) * 0.5f, cy = y0 + 140;
      if (const S::Pic ag = Arte("agua", ahora))
        S::DrawPic(dl, ag, a.At(x0 + 24, y0 + 246), a.At(x1 - 24, y0 + 296), ImVec4(40, 20, 472, 108),
                   IM_COL32(255, 255, 255, sel ? 235 : 110));
      if (const S::Pic ar = Arte("anillo", ahora)) {
        const float r = 160;   // el aro visible mide ~64 % de la imagen
        S::DrawPic(dl, ar, a.At(cx - r, cy - r), a.At(cx + r, cy + r), ImVec4(0, 0, 0, 0),
                   IM_COL32(255, 255, 255, sel || act ? 255 : 130));
      }
      // retrato propio: la cara del luchador del cuerpo del heroe; con cuerpo UT (100..105) no hay cara nativa y vale la del
      // estilo (el luchador nativo de quien es su moveset). Nunca una cara fija.
      Cara(dl, a, H::CuerpoPropio(h.cuerpo) ? h.estilo : h.cuerpo, cx, cy, 104, sel || act ? 255 : 170);
    }
    Texto(dl, a, (x0 + x1) * 0.5f, y0 + 270, D::kHead, h.nombre, sel, !sel, cw - 30);
    Texto(dl, a, (x0 + x1) * 0.5f, y0 + 316, D::kSmall,
          std::string(T::T("Nivel ", "Level ")) + Num(h.nivel) + "  ·  " + std::string(T::T("Saga ", "Saga ")) + Num(h.saga + 1),
          false, true, cw - 30);
    Texto(dl, a, (x0 + x1) * 0.5f, y0 + 350, D::kSmall, M::Nombre(h.cuerpo) + "  ·  " + M::Nombre(h.estilo), false, true,
          cw - 30);
    Texto(dl, a, (x0 + x1) * 0.5f, y0 + 384, D::kSmall,
          std::string(T::T("Ficha ", "Slot ")) + Num(i + 1) + "/" + Num(total), false, true, cw - 30);
    if (act) Texto(dl, a, (x0 + x1) * 0.5f, y0 + 430, D::kSmall, T::T("ACTIVO", "CURRENT"), true, false, cw - 30);
  }
  if (C::ArchivadosPorLimite() > 0)
    Texto(dl, a, 600, 700, D::kSmall,
          std::string(T::T("Tu partida tenía más de 3 héroes: ", "Your save had more than 3 heroes: ")) + Num(C::ArchivadosPorLimite()) +
              T::T(" quedan archivados (Y sobre una ficha vacía los recupera).", " are archived (Y on an empty slot restores them)."),
          true, false, 1000);
  Texto(dl, a, 600, 668, D::kSmall,
        std::string(T::T("Héroes: ", "Heroes: ")) + Num(C::NumSlots()) + "/" + Num(C::kMaxSlots) + "   " +
            T::T("Archivados: ", "Archived: ") + Num(C::NumArchivados()),
        false, false);
  S::HelpLine(dl, a.At(600, 868), 38 * a.s,
              {{"A", T::T("Elegir/crear", "Select/create")}, {"X", T::T("Duplicar", "Duplicate")},
               {"Y", T::T("Archivar/recuperar", "Archive/restore")}, {"B", T::T("Atrás", "Back")}});
  return Accion::kNada;
}

// ---------------------------------------------------------------------------------------------------------------------
// Mapa del mundo (studies 8 fase 2, H5): las sagas como puntos de un mapa propio (dibujado por codigo, sin arte), el
// heroe en 3D (la malla de su ficha, ps2::Escena) vuela de un punto a otro y el radar cuenta las Esferas (una por saga
// completada). A en la saga siguiente la empieza. Es la "alternativa barata" de heroe_pasos.md H5 (no el mapa de DU).
// ---------------------------------------------------------------------------------------------------------------------
namespace {
int g_mapa_cur = 0;
float g_mapa_x = 0, g_mapa_y = 0;   // posicion del heroe (px del area), va hacia el punto del cursor
double g_mapa_t = 0;
std::string g_mapa_aviso;
constexpr float kMx0 = 40, kMx1 = 820, kMy0 = 172, kMy1 = 760;
// Puntos de las sagas en el mapa (fraccion del panel) [H]: un camino de oeste a este, la ultima en el centro.
constexpr float kPuntos[][2] = {{0.12f, 0.70f}, {0.26f, 0.42f}, {0.38f, 0.72f}, {0.50f, 0.36f}, {0.62f, 0.66f},
                                {0.74f, 0.30f}, {0.86f, 0.58f}, {0.55f, 0.15f}, {0.30f, 0.20f}, {0.80f, 0.85f}};
ImVec2 Punto(int i) {
  const int n = int(std::size(kPuntos));
  const float* p = kPuntos[std::clamp(i, 0, n - 1)];
  return ImVec2(kMx0 + 30 + (kMx1 - kMx0 - 60) * p[0], kMy0 + 30 + (kMy1 - kMy0 - 60) * p[1]);
}
void Tierra(ImDrawList* dl, const S::Area& a) {   // mar, islas y montes (formas fijas) [H]
  dl->AddRectFilledMultiColor(a.At(kMx0 + 12, kMy0 + 12), a.At(kMx1 - 12, kMy1 - 12), IM_COL32(30, 90, 170, 255),
                              IM_COL32(30, 90, 170, 255), IM_COL32(10, 40, 110, 255), IM_COL32(10, 40, 110, 255));
  static const float kIslas[][4] = {{0.20f, 0.60f, 0.20f, 0.30f}, {0.45f, 0.50f, 0.25f, 0.34f},
                                    {0.75f, 0.50f, 0.22f, 0.36f}, {0.40f, 0.18f, 0.24f, 0.13f},
                                    {0.82f, 0.86f, 0.10f, 0.08f}, {0.08f, 0.25f, 0.06f, 0.10f}};
  const float w = kMx1 - kMx0 - 60, h = kMy1 - kMy0 - 60;
  for (const auto& i : kIslas) {
    const ImVec2 c = a.At(kMx0 + 30 + w * i[0], kMy0 + 30 + h * i[1]);
    const ImVec2 r(w * i[2] * 0.5f * a.s, h * i[3] * 0.5f * a.s);
    dl->AddEllipseFilled(c, ImVec2(r.x + 6 * a.s, r.y + 6 * a.s), IM_COL32(225, 205, 140, 255), 0, 40);   // playa
    dl->AddEllipseFilled(c, r, IM_COL32(70, 150, 60, 255), 0, 40);
    dl->AddEllipseFilled(ImVec2(c.x - r.x * 0.2f, c.y - r.y * 0.15f), ImVec2(r.x * 0.5f, r.y * 0.45f),
                         IM_COL32(95, 170, 75, 255), 0, 30);
    for (int m = 0; m < 3; ++m) {   // montes
      const float mx = c.x + r.x * (-0.4f + 0.4f * m), my = c.y + r.y * (m == 1 ? -0.1f : 0.25f), s = 14 * a.s;
      dl->AddTriangleFilled(ImVec2(mx, my - s), ImVec2(mx - s, my + s * 0.6f), ImVec2(mx + s, my + s * 0.6f),
                            IM_COL32(130, 105, 70, 255));
      dl->AddTriangleFilled(ImVec2(mx, my - s), ImVec2(mx - s * 0.35f, my - s * 0.4f), ImVec2(mx + s * 0.35f, my - s * 0.4f),
                            IM_COL32(245, 245, 250, 255));
    }
  }
}
void Esfera(ImDrawList* dl, ImVec2 c, float r, int estrellas, int alfa = 255) {
  dl->AddCircleFilled(c, r, IM_COL32(250, 150, 30, alfa), 20);
  dl->AddCircleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.3f), r * 0.35f, IM_COL32(255, 230, 170, alfa), 12);
  dl->AddCircle(c, r, IM_COL32(120, 50, 0, alfa), 20, 1.5f);
  for (int k = 0; k < estrellas; ++k) {
    const float ang = 6.2831853f * k / std::max(estrellas, 1), d = estrellas > 1 ? r * 0.38f : 0;
    dl->AddCircleFilled(ImVec2(c.x + std::cos(ang) * d, c.y + std::sin(ang) * d), r * 0.16f, IM_COL32(200, 20, 20, alfa), 8);
  }
}
}  // namespace

void AbrirMapa() {
  const int total = std::max(1, std::min(H::SagasTotal(), int(std::size(kPuntos))));
  const H::rutas::EstadoMapa st = H::rutas::CalcularEstadoMapa(
      H::EtapaRuta(), total, H::VueltaRuta(), H::RutaCompletada(), H::Saga(),
      H::ColeccionVueltasOn() && H::Rutas());
  g_mapa_cur = st.cursor_inicial;   // HM-02 / NHER4: por etapa real de ruta cuando C2+Rutas ON
  const ImVec2 p = Punto(g_mapa_cur);
  g_mapa_x = p.x, g_mapa_y = p.y;
  g_mapa_t = 0;
  g_mapa_aviso.clear();
  REXLOG_INFO("dbz3 heroe: mapa abierto ({} sagas, completadas {}, la siguiente {})", H::SagasTotal(),
              std::min(H::Saga(), H::SagasTotal()), H::Saga() + 1);
}

Accion MapaUI(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const int total = std::max(1, std::min(H::SagasTotal(), int(std::size(kPuntos))));
  const H::rutas::EstadoMapa st = H::rutas::CalcularEstadoMapa(
      H::EtapaRuta(), total, H::VueltaRuta(), H::RutaCompletada(), H::Saga(),
      H::ColeccionVueltasOn() && H::Rutas());
  const int sig = st.sig;
  const int hechas = st.hechas;
  const int antes = g_mapa_cur;
  if (p & (kRight | kDown)) g_mapa_cur = std::min(g_mapa_cur + 1, total - 1);   // se ven todas; solo empieza la siguiente jugable
  if (p & (kLeft | kUp)) g_mapa_cur = std::max(g_mapa_cur - 1, 0);
  if (REXCVAR_GET(dbz3_heroe_ui_nueva) && (p & (kLb | kRb)))   // HEROE-V05-B: "LB Saga" (maqueta 04): LB la siguiente, RB la
    g_mapa_cur = (g_mapa_cur + ((p & kLb) ? 1 : total - 1)) % total;   // anterior, dando la vuelta
  if (g_mapa_cur != antes) {
    g_mapa_aviso.clear();
    REXLOG_INFO("dbz3 heroe: mapa: saga {} ({})", g_mapa_cur + 1, H::NombreSaga(g_mapa_cur));
  }
  if (p & kB) return Accion::kVolver;
  if (p & kA) {
    if (g_mapa_cur >= H::kSagasJugables) {
      g_mapa_aviso = T::T("En desarrollo: esta saga aun no se puede jugar.", "In development: this saga cannot be played yet.");
      REXLOG_INFO("dbz3 heroe: mapa: la saga {} esta en desarrollo (bloqueada)", g_mapa_cur + 1);
    } else if (g_mapa_cur == sig) {
      REXLOG_INFO("dbz3 heroe: mapa: empezar la saga {} ({}){}", g_mapa_cur + 1, H::NombreSaga(g_mapa_cur),
                  st.nueva_vuelta ? ", nueva vuelta " + std::to_string(H::VueltaRuta() + 1)
                  : st.vuelta_idx > 0 ? ", vuelta " + std::to_string(st.vuelta_idx + 1) : std::string());
      return Accion::kConfirmar;
    } else if (st.nueva_vuelta) {
      g_mapa_aviso = T::T("Ruta completada: pulsa A en la saga 1 para empezar una nueva vuelta.",
                          "Route completed: press A on saga 1 to start a new lap.");
    } else if (g_mapa_cur < sig) {
      g_mapa_aviso = T::T("Saga completada: ya tienes su Esfera.", "Saga cleared: you already have its Dragon Ball.");
      REXLOG_INFO("dbz3 heroe: mapa: la saga {} ya esta completada", g_mapa_cur + 1);
    } else {
      g_mapa_aviso = T::T("Bloqueada: completa antes la saga anterior.", "Locked: clear the previous saga first.");
    }
  }
  if (REXCVAR_GET(dbz3_heroe_ui_nueva)) {   // HEROE-V04/V05-B: maqueta 04 (heroe_v2_mapa.inl); misma entrada, arriba; sin el
    // heroe 3D volando (V05-B: la maqueta pone una marca sobre el combate actual)
    DibujarMapaV2(dl, a, t, total, hechas, g_mapa_cur, ImVec4(kMx0, kMy0, kMx1, kMy1), g_mapa_aviso);
    return Accion::kNada;
  }
  FondoSinAnfitrion(dl, a);
  const int vuelta = st.vuelta_idx + 1;
  std::string titulo = T::T("Mapa del mundo", "World map");
  if (vuelta > 1) titulo += std::string(T::T(" - Vuelta ", " - Lap ")) + std::to_string(vuelta);
  S::BannerBeta(dl, a, titulo.c_str());
  Marco(dl, a, kMx0, kMy0, kMx1, kMy1, 235);
  Tierra(dl, a);
  // camino entre sagas (completado: dorado; por hacer: discontinuo)
  for (int i = 0; i + 1 < total; ++i) {
    const ImVec2 p0 = a.At(Punto(i).x, Punto(i).y), p1 = a.At(Punto(i + 1).x, Punto(i + 1).y);
    if (i < hechas) {
      dl->AddLine(p0, p1, IM_COL32(255, 210, 80, 255), 4 * a.s);
    } else {
      for (int k = 0; k < 12; k += 2)
        dl->AddLine(ImVec2(p0.x + (p1.x - p0.x) * k / 12, p0.y + (p1.y - p0.y) * k / 12),
                    ImVec2(p0.x + (p1.x - p0.x) * (k + 1) / 12, p0.y + (p1.y - p0.y) * (k + 1) / 12),
                    IM_COL32(230, 230, 240, 160), 3 * a.s);
    }
  }
  for (int i = 0; i < total; ++i) {
    const ImVec2 c = a.At(Punto(i).x, Punto(i).y);
    const bool hecha = i < hechas, siguiente = i == sig, sel = i == g_mapa_cur;
    if (sel) dl->AddCircle(c, (26 + 4 * std::sin(t * 6)) * a.s, IM_COL32(255, 255, 255, 230), 32, 3 * a.s);
    if (hecha) {
      Esfera(dl, c, 15 * a.s, i % 7 + 1);
    } else {
      dl->AddCircleFilled(c, 13 * a.s, siguiente && i < H::kSagasJugables ? IM_COL32(220, 40, 40, 255) : IM_COL32(90, 90, 100, 230), 20);
      dl->AddCircle(c, 13 * a.s, IM_COL32(20, 10, 0, 255), 20, 2 * a.s);
    }
    Texto(dl, a, Punto(i).x, Punto(i).y + 30, D::kSmall * 0.8f, Num(i + 1), sel, !sel && !siguiente && !hecha, 60);
  }
  // el heroe vuela hacia el punto del cursor (3D, la malla de su ficha)
  const double ahora = dbz3::RelojT(ImGui::GetTime());
  const float dt = g_mapa_t > 0 ? float(std::min(ahora - g_mapa_t, 0.1)) : 0.0f;
  g_mapa_t = ahora;
  const ImVec2 dest = Punto(g_mapa_cur);
  const float k = 1.0f - std::exp(-4.0f * dt);
  const float dx = dest.x - g_mapa_x;
  g_mapa_x += (dest.x - g_mapa_x) * k;
  g_mapa_y += (dest.y - g_mapa_y) * k;
  const bool vuela = std::abs(dest.x - g_mapa_x) + std::abs(dest.y - g_mapa_y) > 3;
  const float alto = vuela ? 26.0f : 8.0f + 4.0f * std::sin(t * 3);
  const Modelo3D* mf = MallaFicha(H::Cuerpo(), H::Traje(), Ficha(), ahora);
  bool pintada = false;
  if (const Modelo3D* m3 = CuerpoUT() ? nullptr : (mf ? mf : Malla3D(H::Cuerpo(), H::Traje(), ahora))) {
    const float giro = vuela ? (dx >= 0 ? 1.5708f : -1.5708f) : 0.0f;
    pintada = Pintar3D(dl, *m3, a.At(g_mapa_x - 60, g_mapa_y - 150 - alto), a.At(g_mapa_x + 60, g_mapa_y - alto), giro);
    if (pintada) dl->AddEllipseFilled(a.At(g_mapa_x, g_mapa_y - 4), ImVec2(22 * a.s, 6 * a.s), IM_COL32(0, 0, 0, 90));
  }
  if (!pintada) {
    if (CuerpoUT()) {
      if (const auto p = VistaUT(H::Cuerpo(), H::Traje(), ahora)) {
        const float frame = p.h;
        const float kf = vuela ? (dx > 0 ? 3.0f : 9.0f) : 0.0f;   // 12 giros: 90/270 grados
        S::DrawPic(dl, p, a.At(g_mapa_x - 55, g_mapa_y - 130 - alto), a.At(g_mapa_x + 55, g_mapa_y - alto),
                   ImVec4(kf * frame, 0, (kf + 1) * frame, p.h));
        pintada = true;
      }
    }
    if (!pintada) Cara(dl, a, CuerpoUT() ? H::EstiloId() : H::Cuerpo(), g_mapa_x, g_mapa_y - 40 - alto, 64);
  }
  // radar (derecha): una Esfera por saga completada
  constexpr float rx0 = 840, rx1 = 1160;
  Marco(dl, a, rx0, kMy0, rx1, kMy1, 235);
  const ImVec2 rc = a.At((rx0 + rx1) * 0.5f, kMy0 + 130);
  const float rr = 95 * a.s;
  dl->AddCircleFilled(rc, rr, IM_COL32(20, 70, 30, 255), 48);
  for (int g = 1; g <= 3; ++g) dl->AddCircle(rc, rr * g / 3, IM_COL32(90, 220, 110, 160), 48, 1.5f * a.s);
  dl->AddLine(ImVec2(rc.x - rr, rc.y), ImVec2(rc.x + rr, rc.y), IM_COL32(90, 220, 110, 160), 1.5f * a.s);
  dl->AddLine(ImVec2(rc.x, rc.y - rr), ImVec2(rc.x, rc.y + rr), IM_COL32(90, 220, 110, 160), 1.5f * a.s);
  const float barrido = t * 2.0f;
  dl->AddLine(rc, ImVec2(rc.x + std::cos(barrido) * rr, rc.y + std::sin(barrido) * rr), IM_COL32(180, 255, 190, 220),
              2.5f * a.s);
  const int esferas = __builtin_popcount(unsigned(H::Esferas()));
  for (int i = 0; i < 7; ++i) {   // recogidas: en el centro; por recoger: puntos que parpadean donde estan sus sagas
    const float ang = 6.2831853f * i / 7 - 1.5708f;
    if (i < esferas) {
      Esfera(dl, ImVec2(rc.x + std::cos(ang) * rr * 0.32f, rc.y + std::sin(ang) * rr * 0.32f), 9 * a.s, i + 1);
    } else if (std::fmod(t * 2.0f + i * 0.3f, 2.0f) < 1.3f) {
      dl->AddCircleFilled(ImVec2(rc.x + std::cos(ang * 1.7f) * rr * 0.8f, rc.y + std::sin(ang * 1.7f) * rr * 0.8f),
                          5 * a.s, IM_COL32(255, 200, 60, 255), 10);
    }
  }
  Texto(dl, a, (rx0 + rx1) * 0.5f, kMy0 + 250, D::kSmall, std::string(T::T("Esferas: ", "Dragon Balls: ")) +
        Num(esferas) + " / 7", true, false, rx1 - rx0 - 40);
  // ficha de la saga del cursor
  const int s = g_mapa_cur;
  float y = kMy0 + 300;
  Texto(dl, a, (rx0 + rx1) * 0.5f, y, D::kRow, H::NombreSaga(s), true, false, rx1 - rx0 - 40);
  y += 40;
  Texto(dl, a, (rx0 + rx1) * 0.5f, y, D::kSmall,
        s < hechas ? T::T("Completada", "Cleared")
        : s >= H::kSagasJugables ? T::T("En desarrollo", "In development")
        : s == sig ? T::T("Siguiente: A para empezar", "Next: A to start") : T::T("Bloqueada", "Locked"), false,
        s < hechas, rx1 - rx0 - 40);
  y += 38;
  const auto combates = H::CombatesDeSaga(s);
  for (const auto& [rival, esc] : combates) {
    if (y > kMy1 - 70) break;
    Texto(dl, a, (rx0 + rx1) * 0.5f, y, D::kSmall * 0.85f,
          (rival >= 0 ? M::Nombre(rival) : std::string("?")) + "  -  " + dbz3::escenarios::Nombre(esc), false, true,
          rx1 - rx0 - 40);
    y += 28;
  }
  if (const int m = H::MaestroQueEntrena(s); m >= 0 && y <= kMy1 - 40)
    Texto(dl, a, (rx0 + rx1) * 0.5f, y + 6, D::kSmall * 0.85f, std::string(T::T("Maestro: ", "Master: ")) + M::Nombre(m),
          false, false, rx1 - rx0 - 40);
  if (!g_mapa_aviso.empty())
    Texto(dl, a, (kMx0 + kMx1) * 0.5f, kMy1 + 40, D::kSmall, g_mapa_aviso, true, false, kMx1 - kMx0);
  S::HelpLine(dl, a.At(600, 868), 38 * a.s,
              {{"A", T::T("Empezar", "Start")}, {"B", T::T("Atrás", "Back")}});
  return Accion::kNada;
}

// ---------------------------------------------------------------------------------------------------------------------
// Select Master (H4)
// ---------------------------------------------------------------------------------------------------------------------
namespace {
int g_mcur = 0;
std::string g_maviso;
std::string Pct(int v) { return (v > 0 ? "+" : "") + Num(v) + "%"; }
}  // namespace

void AbrirMaestros() {
  g_mcur = std::max(0, H::MaestroActual());
  g_maviso.clear();
  const int act = H::MaestroActual();
  REXLOG_INFO("dbz3 heroe: Select Master abierto (maestro actual {}, entrenamiento {}%)", act,
              act >= 0 ? H::Maestro(act).entreno : 0);
}

Accion Maestros(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  constexpr int n = H::kMaestros;
  if (p & (kUp | kDown)) g_maviso.clear();
  if (p & kUp) g_mcur = (g_mcur + n - 1) % n;
  if (p & kDown) g_mcur = (g_mcur + 1) % n;
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: Select Master: atras");
    return Accion::kVolver;
  }
  if (p & kA) {
    if (H::ElegirMaestro(g_mcur)) {
      return Accion::kConfirmar;
    }
    if (const H::InfoMaestro im = H::Maestro(g_mcur); !im.desbloqueado && im.saga >= 0)
      g_maviso = std::string(T::T("Se desbloquea al llegar a: ", "Unlocks when you reach: ")) + H::NombreSaga(im.saga);
    else if (REXCVAR_GET(dbz3_heroe_motivo_maestro) && im.desbloqueado)
      g_maviso = AvisoAfinidadMaestro();
    else g_maviso = T::T("No se pudo cambiar de maestro.", "The master could not be changed.");
  }
  const int act = H::MaestroActual();
  if (REXCVAR_GET(dbz3_heroe_ui_nueva)) {   // HEROE-V04: maqueta 05 (heroe_v2_maestros.inl); misma entrada, arriba
    DibujarMaestrosV2(dl, a, t, g_mcur, act, g_maviso);
    return Accion::kNada;
  }
  FondoSinAnfitrion(dl, a, "hub/heroe_fondo_maestros");
  S::BannerBeta(dl, a, T::T("Elige maestro", "Select Master"));
  // lista de maestros con su retrato (izquierda)
  constexpr float lx0 = 40, lx1 = 470, ly0 = 172, paso = 76;
  const float ly1 = ly0 + 2 * D::kPad + paso * n;
  Marco(dl, a, lx0, ly0, lx1, ly1, 235);
  for (int i = 0; i < n; ++i) {
    const H::InfoMaestro m = H::Maestro(i);
    const float y = ly0 + D::kPad + paso * (i + 0.5f);
    const bool sel = i == g_mcur;
    if (const S::Pic pm = S::Img("hub/heroe_panel_maestro")) {   // pack Dragon HUB (opcional): placa 1983x793, caja util (28,41)-(1951,706)
      const float y0 = y - paso * 0.5f + 3, y1p = y + paso * 0.5f - 3;
      const ImU32 tin = sel ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 150);
      const float k = (y1p - y0) / 665.0f, cap = 300 * k;   // escala por el alto; tapas izquierda y derecha, centro estirado
      const float x0 = lx0 + 4, x1 = lx1 - 4;
      S::DrawPic(dl, pm, a.At(x0, y0), a.At(x0 + cap, y1p), ImVec4(28, 41, 328, 706), tin);
      S::DrawPic(dl, pm, a.At(x0 + cap, y0), a.At(x1 - cap, y1p), ImVec4(328, 41, 1651, 706), tin);
      S::DrawPic(dl, pm, a.At(x1 - cap, y0), a.At(x1, y1p), ImVec4(1651, 41, 1951, 706), tin);
    } else if (sel) D::Stripe(dl, a, lx0, y - paso * 0.5f + 3, lx1, y + paso * 0.5f - 3, t);
    Cara(dl, a, m.id, lx0 + 78, y, 60, !m.desbloqueado ? 60 : sel ? 255 : 190);   // s9: bloqueado, en sombra
    Texto(dl, a, lx0 + 230, y - 12, D::kRow, m.desbloqueado ? M::Nombre(m.id) : std::string("???"), sel, !sel, 190);
    // barra fina de entrenamiento bajo el nombre
    const float bx0 = lx0 + 140, bx1 = lx0 + 320, by = y + 20;
    dl->AddRectFilled(a.At(bx0, by - 5), a.At(bx1, by + 5), IM_COL32(0, 0, 0, 160), 3 * a.s);
    dl->AddRectFilled(a.At(bx0, by - 5), a.At(bx0 + (bx1 - bx0) * m.entreno / 100.0f, by + 5),
                      m.entrenado ? IM_COL32(255, 200, 40, 255) : IM_COL32(60, 200, 255, 255), 3 * a.s);
    Texto(dl, a, lx1 - 70, y + (i == act ? 10 : 0), D::kSmall, Num(m.entreno) + "%", sel, !sel, 80);
    if (i == act) Texto(dl, a, lx1 - 70, y - 22, 16, T::T("ACTUAL", "CURRENT"), true, false, 90);
  }
  // panel MASTER SKILL (derecha)
  const H::InfoMaestro m = H::Maestro(g_mcur);
  constexpr float px0 = 500, px1 = 1130, py0 = 172, py1 = 700;
  Marco(dl, a, px0, py0, px1, py1, 235);
  Texto(dl, a, (px0 + px1) * 0.5f, py0 + 44, D::kHead, T::T("HABILIDAD DEL MAESTRO", "MASTER SKILL"), true, false, px1 - px0 - 80);
  Cara(dl, a, m.id, px0 + 110, py0 + 140, 120, m.desbloqueado ? 255 : 60);   // s9: bloqueado, sin desvelarlo
  Texto(dl, a, px0 + 380, py0 + 104, D::kHead, m.desbloqueado ? M::Nombre(m.id) : std::string("???"), true, false, 380);
  Texto(dl, a, px0 + 380, py0 + 146, D::kSmall,
        std::string(T::T("Entrenamiento completado: ", "Training Completion: ")) + Num(m.entreno) + "%", false, false, 400);
  {   // barra grande de entrenamiento con la marca del 60 %
    const float bx0 = px0 + 200, bx1 = px1 - 50, by = py0 + 186;
    dl->AddRectFilled(a.At(bx0, by - 9), a.At(bx1, by + 9), IM_COL32(0, 0, 0, 180), 4 * a.s);
    dl->AddRectFilled(a.At(bx0, by - 9), a.At(bx0 + (bx1 - bx0) * m.entreno / 100.0f, by + 9),
                      m.entrenado ? IM_COL32(255, 200, 40, 255) : IM_COL32(60, 200, 255, 255), 4 * a.s);
    const float x60 = bx0 + (bx1 - bx0) * H::EntrenoMinimo() / 100.0f;
    dl->AddLine(a.At(x60, by - 14), a.At(x60, by + 14), IM_COL32(255, 80, 60, 255), 3 * a.s);
    dl->AddRect(a.At(bx0, by - 9), a.At(bx1, by + 9), IM_COL32(255, 190, 70, 255), 4 * a.s, 0, 1.5f * a.s);
  }
  // modificadores (los de heroe.cpp kEstilos: los aplica heroe::Reglas)
  std::vector<std::pair<std::string, bool>> mods;   // texto, positivo
  if (m.vida) mods.push_back({std::string(T::T("Vida ", "Health ")) + (m.vida > 0 ? "+" : "") + Num(m.vida), m.vida > 0});
  if (m.dano != 100) mods.push_back({std::string(T::T("Ataque ", "Attack ")) + Pct(m.dano - 100), m.dano > 100});
  if (m.recibido != 100)
    mods.push_back({std::string(T::T("Daño recibido ", "Damage taken ")) + Pct(m.recibido - 100), m.recibido < 100});
  if (m.ki < 0) mods.push_back({T::T("El ki se recarga solo", "Ki recharges by itself"), true});
  float y = py0 + 236;
  Texto(dl, a, (px0 + px1) * 0.5f, y, D::kRow, T::T("Modificadores", "Modifiers"), true, false, px1 - px0 - 80);
  y += 38;
  for (const auto& [s, bueno] : mods) {
    Texto(dl, a, (px0 + px1) * 0.5f, y, D::kSmall, s, bueno, !bueno, px1 - px0 - 80);
    y += 32;
  }
  // tecnicas y capsula que desbloquea
  y += 10;
  Texto(dl, a, (px0 + px1) * 0.5f, y, D::kRow, T::T("Técnicas", "Techniques"), true, false, px1 - px0 - 80);
  y += 36;
  Texto(dl, a, (px0 + px1) * 0.5f, y, D::kSmall, m.tecnicas, false, false, px1 - px0 - 80);
  y += 40;
  if (const S::Pic cap = S::Img("capsulas")) {   // botella de capsula de la Skill Shop (data del juego)
    const float h = 50 * a.s, w = h * 31 / 83;
    const ImVec2 c = a.At(px0 + 90, y);
    S::DrawPic(dl, cap, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f),
               ImVec4(3, 7, 34, 90), m.entrenado ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 90));
  }
  Texto(dl, a, (px0 + px1) * 0.5f + 40, y, D::kSmall,
        std::string(T::T("Cápsula ", "Capsule ")) + m.capsula_n + ": " +
            (m.entrenado ? T::T("desbloqueada", "unlocked") : T::T("se desbloquea al 100 %", "unlocks at 100%")),
        m.entrenado, !m.entrenado, px1 - px0 - 200);
  // En la vista clasica tambien se ve el dato aunque el maestro ya este abierto.
  Marco(dl, a, px0, py1 + 16, px1, py1 + 78, 200);
  std::string entrena;
  for (int s = 0; s < std::min(H::SagasTotal(), H::kSagasJugables); ++s)
    if (H::MaestroQueEntrena(s) == m.id)
      entrena += std::string(entrena.empty() ? "" : ", ") + H::NombreSaga(s);
  Texto(dl, a, (px0 + px1) * 0.5f, py1 + 31, D::kSmall * 0.85f,
        entrena.empty() ? std::string(T::T("Dragon Universe: sin saga asignada", "Dragon Universe: no assigned saga"))
                        : std::string(T::T("Dragon Universe: ", "Dragon Universe: ")) + entrena,
        false, false, px1 - px0 - 70);
  // regla del 60 % (UT) o el aviso
  const bool puede = H::PuedeCambiarA(g_mcur);
  if (!m.desbloqueado && g_maviso.empty() && m.saga >= 0)   // s9: maestro aun no desbloqueado
    g_maviso = std::string(T::T("Se desbloquea al llegar a: ", "Unlocks when you reach: ")) + H::NombreSaga(m.saga);
  if (REXCVAR_GET(dbz3_heroe_motivo_maestro) && !puede && m.desbloqueado && g_maviso.empty())
    g_maviso = AvisoAfinidadMaestro();
  std::string regla = g_maviso.empty()
                          ? std::string(T::T("Cambia de maestro cuando quieras; a ", "Switch master any time; at ")) +
                                Num(H::EntrenoMinimo()) + T::T("% abres su técnica clave.", "% you unlock their key technique.")
                          : g_maviso;
  Texto(dl, a, (px0 + px1) * 0.5f, py1 + 60, D::kSmall * 0.85f, regla, !puede && !g_maviso.empty(), false, px1 - px0 - 70);
  // capsulas desbloqueadas de la ficha (cac.toml tecnicas[])
  {
    const C::Heroe& h = C::Slot(C::SlotActivo());
    std::string s = T::T("Desbloqueadas: ", "Unlocked: ");
    int k = 0;
    for (int i = 0; i < n; ++i) {
      const H::InfoMaestro x = H::Maestro(i);
      if (!x.entrenado) continue;
      s += std::string(k++ ? ", " : "") + x.capsula_n;
    }
    if (!k) s += T::T("ninguna", "none");
    (void)h;
    Marco(dl, a, lx0, ly1 + 16, lx1, ly1 + 74, 200);
    Texto(dl, a, (lx0 + lx1) * 0.5f, ly1 + 45, D::kSmall, s, false, false, lx1 - lx0 - 60);
  }
  S::HelpLine(dl, a.At(600, 868), 38 * a.s,
              {{"A", puede              ? T::T("Elegir maestro", "Choose master")
                     : !m.desbloqueado ? T::T("Bloqueado", "Locked")
                                       : REXCVAR_GET(dbz3_heroe_motivo_maestro) ? T::T("Falta afinidad", "Affinity required")
                                           : T::T("Bloqueado (60 %)", "Locked (60%)")},
               {"B", T::T("Atrás", "Back")}});
  return Accion::kNada;
}

// ---------------------------------------------------------------------------------------------------------------------
// Habitacion y Evolucion por raza (rediseno del Modo Heroe; dbz3_heroe_habitacion / dbz3_heroe_evolucion, apagados de serie).
// Entrada y cursor aqui; lo que abre cada entrada lo hace rpg.cpp (kHeroeHabitacion / kHeroeEvolucion) y vuelve.
// ---------------------------------------------------------------------------------------------------------------------
#include "heroe_v2_coleccion.inl"   // HEROE-REC: Coleccion (dbz3_heroe_recompensas); tras CabeceraCarta/AsegurarCartas

bool HabitacionActiva() { return REXCVAR_GET(dbz3_heroe_habitacion); }

void AbrirHabitacion(bool reentrada) {
  if (!reentrada) { g_hab_cur = 0; g_hab_receta = -1; }
  g_hab_aviso.clear();
  PedirVista(H::Cuerpo(), H::Traje());
  REXLOG_INFO("dbz3 heroe: Habitacion abierta (cuerpo {}, traje {}, fila {})", H::Cuerpo(), H::Traje() + 1, g_hab_cur + 1);
  REXLOG_INFO("dbz3 heroe: Habitacion abierta: casa {}", H::CasaActual() == H::CasaHabitacion::kKame ? "kame" : "capsule");
}

Hab Habitacion(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  const double ahora = dbz3::RelojT(ImGui::GetTime());
  static double antes = 0;
  const float dt = float(std::clamp(ahora - antes, 0.0, 0.1));
  antes = ahora;
  // En esta pantalla LB/RB pertenecen al selector de casa; LB no cambia ademas la animacion.
  ControlesVistaV2(a, uint16_t(p & ~(kLb | kRb)), dt, ahora);   // RS / LT-RT / raton: girar al heroe
  if (p & (kUp | kDown)) g_hab_aviso.clear();
  const int fila = g_hab_cur;
  g_hab_cur = std::min(g_hab_cur, NumFilasHab() - 1);   // HEROE-REC: la 6.a fila solo existe con el ajuste
  if (p & kUp) g_hab_cur = (g_hab_cur + NumFilasHab() - 1) % NumFilasHab();
  if (p & kDown) g_hab_cur = (g_hab_cur + 1) % NumFilasHab();
  if (g_hab_cur != fila) REXLOG_INFO("dbz3 heroe: Habitacion: fila {} de {}", g_hab_cur + 1, NumFilasHab());
  Hab r = Hab::kNada;
  if (bool(p & kLb) != bool(p & kRb) || PulsarCasaHabitacion(a)) {
    H::CambiarCasa();
    g_hab_aviso.clear();
    // Guardar antes de atender otra accion simultanea (confirmar o volver) en el siguiente cuadro.
    DibujarHabitacionV2(dl, a, t, p, ahora);
    return Hab::kCambio;
  }
  if (const int d = ((p & kRight) ? 1 : 0) - ((p & kLeft) ? 1 : 0)) {
    if (g_hab_cur == 0) {  // CAC-10 vía A: nativos seguidos de los huecos llenos
      const int n = std::max(1, H::NumTrajes());
      const int total = n + C::kMaxRecetas;
      int k = g_hab_receta >= 0 ? n + g_hab_receta : std::clamp(H::Traje(), 0, n - 1);
      do { k = (k + d + total) % total; } while (k >= n && !H::RecetaLlena(k - n));
      g_hab_receta = k < n ? -1 : k - n;
      g_hab_aviso.clear();
      if (k < n && k != H::Traje()) {
        H::PonerTraje(k);
        PedirVista(H::Cuerpo(), H::Traje());
        r = Hab::kCambio;
      }
    } else if (g_hab_cur == 2) {   // Maestros: el siguiente que se pueda elegir (regla del 60 %)
      const int n = H::kMaestros, act = std::max(0, H::MaestroActual());
      for (int k = 1; k < n; ++k) {
        const int j = ((act + d * k) % n + n) % n;
        if (H::Maestro(j).desbloqueado && H::PuedeCambiarA(j) && H::ElegirMaestro(j)) {
          REXLOG_INFO("dbz3 heroe: Habitacion: maestro {}", j);
          r = Hab::kCambio;
          break;
        }
      }
    }
  }
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: Habitacion: volver");
    return Hab::kVolver;
  }
  if (p & kA) {
    if (g_hab_cur == 0 && g_hab_receta >= 0) {
      if (H::RecetaLlena(g_hab_receta) && !FichaXv2NoDisponible(Ficha().recetas[g_hab_receta].ficha) && H::RecetaAplicar(g_hab_receta)) {
        g_hab_receta = -1;
        PedirVista(H::Cuerpo(), H::Traje());
        PedirCombate();
        return Hab::kCambio;
      }
      g_hab_aviso = T::T("No se puede aplicar este traje.", "This costume cannot be applied.");
      DibujarHabitacionV2(dl, a, t, p, ahora);
      return r;
    }
    static const Hab kDest[kHabFilas] = {Hab::kAspecto, Hab::kEvolucion, Hab::kMaestros, Hab::kTecnicas, Hab::kFicha, Hab::kColeccion};
    if (g_hab_cur == 1 && !H::EvolucionActiva())
      g_hab_aviso = T::T("Evolución está apagada (Ajustes, beta).", "Evolution is off (Settings, beta).");
    else {
      REXLOG_INFO("dbz3 heroe: Habitacion: entra en la fila {}", g_hab_cur + 1);
      return kDest[g_hab_cur];
    }
  }
  DibujarHabitacionV2(dl, a, t, p, ahora);
  return r;
}

void AbrirEvolucion() {
  g_evo_raza = H::RazaEvolucion();
  g_evo_cur = -1;   // se coloca en el nodo actual al primer dibujo
  PedirVista(H::Cuerpo(), H::Traje());
  // R23 CAC: la ficha entera en el registro (guion heroe_cac_razas.py: pestana = raza de la ficha)
  REXLOG_INFO("dbz3 heroe: Evolucion abierta (raza {}, {} formas) ficha raza {} cuerpo {} traje {} id propio {}", g_evo_raza,
              H::FormasDeRaza(g_evo_raza).size(), H::RazaHeroe(), H::Cuerpo(), H::Traje() + 1, H::IdPropio());
  for (const H::InfoForma& f : H::FormasDeRaza(g_evo_raza))
    REXLOG_INFO("dbz3 heroe: Evolucion: forma {} estado {} capsula {} combate {}", f.id, f.estado, f.capsula, f.combate);
}

Accion Evolucion(ImDrawList* dl, const S::Area& a, float t, uint16_t p) {
  if (p & kB) {
    REXLOG_INFO("dbz3 heroe: Evolucion: atras");
    return Accion::kVolver;
  }
  if (p & (kLb | kRb)) {
    g_evo_raza = (g_evo_raza + ((p & kRb) ? 1 : kRazasEvo - 1)) % kRazasEvo;
    g_evo_cur = -1;
    REXLOG_INFO("dbz3 heroe: Evolucion: raza {} ({} formas)", g_evo_raza, H::FormasDeRaza(g_evo_raza).size());
  }
  {
    const std::vector<H::InfoForma> fs = H::FormasDeRaza(g_evo_raza);
    const std::vector<NodoEvo> n = DisponerEvo(fs, g_evo_raza);
    g_evo_cur = AjustarEvo(n, g_evo_cur);
    const float dx = ((p & kRight) ? 1.0f : 0.0f) - ((p & kLeft) ? 1.0f : 0.0f), dy = ((p & kDown) ? 1.0f : 0.0f) - ((p & kUp) ? 1.0f : 0.0f);
    if (dx != 0.0f || dy != 0.0f) {
      if (const int j = VecinoEvo(n, g_evo_cur, dx, dy); j >= 0) {
        g_evo_cur = j;
        REXLOG_INFO("dbz3 heroe: Evolucion: nodo {} ({}, estado {}, combate {})", j, j ? fs[size_t(j - 1)].id : std::string("base"),
                    j ? fs[size_t(j - 1)].estado : 0, j ? fs[size_t(j - 1)].combate : 1);
      }
    }
    if (p & kA)
      REXLOG_INFO("dbz3 heroe: Evolucion: ver forma {} ({})", g_evo_cur, g_evo_cur ? fs[size_t(g_evo_cur - 1)].id : std::string("base"));
  }
  DibujarEvolucionV2(dl, a, t);
  return Accion::kNada;
}

// R23 HER-07: vista previa de Tecnicas = fotograma del propio juego. Al pulsar "Probar" se apunta la carta; cuando el jugador la
// lanza en el Entrenamiento (modos.cpp: gasto de ki de golpe del lado 1) se guarda la ventana 0,7 s despues, recortada a la imagen
// 16:9 del juego y reducida a 640x360, en ingame_ui/heroe_tecnicas/vista_<cap>.png. Nada se embarca: se hace en el PC del jugador.
namespace {
std::atomic<int> g_vista_cap{0};
}
void VistaTecnicaPedir(int cap) { g_vista_cap = cap; }
void VistaTecnicaLanzada() {
#if defined(_WIN32)
  const int cap = g_vista_cap.exchange(0);
  if (cap <= 0) return;
  std::thread([cap] {
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    struct Busca {
      DWORD pid;
      HWND h;
      long area;
    } b{GetCurrentProcessId(), nullptr, 0};
    EnumWindows(
        [](HWND h, LPARAM l) -> BOOL {   // la ventana visible mas grande de este proceso = la del juego
          auto* b = reinterpret_cast<Busca*>(l);
          DWORD pid = 0;
          GetWindowThreadProcessId(h, &pid);
          RECT r{};
          if (pid == b->pid && IsWindowVisible(h) && GetClientRect(h, &r) && long(r.right) * r.bottom > b->area) b->h = h, b->area = long(r.right) * r.bottom;
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&b));
    RECT rc{};
    if (!b.h || !GetClientRect(b.h, &rc) || rc.right <= 64 || rc.bottom <= 36) return;
    const int w = rc.right, h = rc.bottom;
    int cw = w, ch = w * 9 / 16;   // la imagen 16:9 centrada (sin franjas)
    if (ch > h) ch = h, cw = h * 16 / 9;
    constexpr int ow = 640, oh = 360;
    HDC pant = GetDC(nullptr), m1 = CreateCompatibleDC(pant), m2 = CreateCompatibleDC(pant);
    HBITMAP b1 = CreateCompatibleBitmap(pant, w, h), b2 = CreateCompatibleBitmap(pant, ow, oh);
    HGDIOBJ v1 = SelectObject(m1, b1), v2_ = SelectObject(m2, b2);
    bool ok = PrintWindow(b.h, m1, 0x3 /*PW_CLIENTONLY | PW_RENDERFULLCONTENT*/) != 0;
    SetStretchBltMode(m2, HALFTONE);
    // como la maqueta 14: la parte izquierda y baja (donde esta el jugador en el Entrenamiento), sin el marcador, 16:9
    const int sx = (w - cw) / 2, sy = (h - ch) / 2 + ch * 2 / 5, sw = cw * 55 / 100, sh = sw * 9 / 16;   // y 40-95 %: sin el marcador
    ok = ok && StretchBlt(m2, 0, 0, ow, oh, m1, sx, sy, sw, sh, SRCCOPY);
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof bi, bi.biWidth = ow, bi.biHeight = -oh, bi.biPlanes = 1, bi.biBitCount = 24, bi.biCompression = BI_RGB;
    std::vector<uint8_t> px(size_t(ow * 3) * oh);
    SelectObject(m2, v2_);
    ok = ok && GetDIBits(m2, b2, 0, oh, px.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS) == oh;
    SelectObject(m1, v1), DeleteObject(b1), DeleteObject(b2), DeleteDC(m1), DeleteDC(m2), ReleaseDC(nullptr, pant);
    std::error_code ec;
    const fs::path dir = Juego() / "ingame_ui" / "heroe_tecnicas";
    fs::create_directories(dir, ec);
    const fs::path f = dir / ("vista_" + std::to_string(cap) + ".png");
    if (ok) {   // PNG sin comprimir (bloques "stored" de deflate): el lector del juego (DecodeImageRGBA) lee PNG
      std::vector<uint8_t> raw;   // filas con su byte de filtro 0, RGB (GDI da BGR)
      raw.reserve(size_t(oh) * (ow * 3 + 1));
      for (int y = 0; y < oh; ++y) {
        raw.push_back(0);
        const uint8_t* r = px.data() + size_t(y) * ow * 3;
        for (int x = 0; x < ow; ++x) raw.insert(raw.end(), {r[x * 3 + 2], r[x * 3 + 1], r[x * 3]});
      }
      auto crc = [](const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
        for (size_t i = 0; i < n; ++i) {
          c ^= d[i];
          for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
        return c;
      };
      auto be = [](std::vector<uint8_t>& v, uint32_t x) { v.insert(v.end(), {uint8_t(x >> 24), uint8_t(x >> 16), uint8_t(x >> 8), uint8_t(x)}); };
      std::vector<uint8_t> z = {0x78, 0x01};
      uint32_t a1 = 1, a2 = 0;
      for (size_t i = 0; i < raw.size(); i += 65535) {
        const size_t n = std::min<size_t>(65535, raw.size() - i);
        z.insert(z.end(), {uint8_t(i + n == raw.size()), uint8_t(n), uint8_t(n >> 8), uint8_t(~n), uint8_t(~n >> 8)});
        z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
      }
      for (uint8_t c : raw) a1 = (a1 + c) % 65521, a2 = (a2 + a1) % 65521;
      be(z, (a2 << 16) | a1);
      std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
      auto trozo = [&](const char* tipo, const std::vector<uint8_t>& d) {
        be(png, uint32_t(d.size()));
        const size_t ini = png.size();
        png.insert(png.end(), tipo, tipo + 4);
        png.insert(png.end(), d.begin(), d.end());
        be(png, crc(png.data() + ini, png.size() - ini) ^ 0xFFFFFFFFu);
      };
      std::vector<uint8_t> ihdr;
      be(ihdr, ow), be(ihdr, oh);
      ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
      trozo("IHDR", ihdr), trozo("IDAT", z), trozo("IEND", {});
      std::ofstream o(f, std::ios::binary | std::ios::trunc);
      o.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
      ok = bool(o);
    }
    REXLOG_INFO("dbz3 heroe: tecnicas v2: vista previa de la capsula {} {} ({})", cap, ok ? "guardada" : "NO guardada", f.string());
  }).detach();
#endif
}

}  // namespace dbz3::heroe_ui

// R16: ejecutado por pruebas.py cac_guardado en un directorio NUEVO, nunca borra datos.
#include "src/cac/cac.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace C = dbz3::cac;
namespace fs = std::filesystem;
void Exigir(bool ok, const char* texto) { if (!ok) throw std::runtime_error(texto); }
void Igual(const C::Heroe& a, const C::Heroe& b) {
  Exigir(a.id == b.id && a.nombre == b.nombre && a.raza == b.raza && a.cuerpo == b.cuerpo, "identidad distinta");
  for (int i = 0; i < 7; ++i) Exigir(a.xv2[i] == b.xv2[i], "pieza perdida");
  for (int i = 0; i < C::kNumPartes; ++i)
    Exigir(a.parte[i].h == b.parte[i].h && a.parte[i].s == b.parte[i].s && a.parte[i].v == b.parte[i].v, "color perdido");
}
int main(int argc, char** argv) {
  try {
    Exigir(argc == 2 && !fs::exists(argv[1]), "se necesita un directorio nuevo");
    const fs::path dir = argv[1];
    fs::create_directories(dir);
    const char* razas[] = {"HUM", "HUF", "NMC", "FRI", "MAM", "MAF"};
    for (int r = 0; r < 6; ++r) {
      const std::string path = (dir / (std::string(razas[r]) + ".toml")).string();
      C::Heroe& h = C::SlotEditable(0);
      h = C::Heroe{};
      h.nombre = razas[r];
      h.raza = 7 + r; h.cuerpo = 110 + r;
      for (int k = 0; k < 7; ++k) h.xv2[k] = 301 + r * 10 + k;
      for (int k = 0; k < C::kNumPartes; ++k) h.parte[k] = {30 + r * 10 + k, -50 + k, 40 - k};
      Exigir(C::Guardar(path), "guardar inicial");
      const C::Heroe esperado = C::Slot(0);
      h = C::Heroe{};
      Exigir(C::Leer(path), "leer inicial");
      Igual(esperado, C::Slot(0));
      Exigir(C::Guardar(path), "guardar segunda vez");
      // Reproduce el hueco que dejaba el rename del principal a .bak antes del commit.
      fs::rename(path, path + ".principal_apartado");
      C::SlotEditable(0) = C::Heroe{};
      Exigir(C::Leer(path), "principal ausente: no se recupera .bak");
      Igual(esperado, C::Slot(0));
      std::cout << "PASS " << razas[r] << ": piezas/6 colores y recuperacion sin principal\n";
    }
    const std::string p = (dir / "texto.toml").string();
    C::SlotEditable(0).nombre = "Heroe \"XV2\"\\nombre\nsegunda linea\tfin";
    Exigir(C::Guardar(p), "guardar nombre especial");
    const C::Heroe texto = C::Slot(0);
    Exigir(C::Leer(p), "nombre con salto de linea rompe TOML");
    Igual(texto, C::Slot(0));
    std::cout << "PASS texto escapado\n";
    // Si no se puede preparar .bak, el guardado debe FALLAR conservando el principal intacto.
    const std::string q = (dir / "bloqueado.toml").string();
    Exigir(C::Guardar(q), "guardar antes del bloqueo");
    fs::create_directory(q + ".bak");
    std::ofstream(q + ".bak/ocupado") << "preservar";
    C::SlotEditable(0).nombre = "no debe reemplazar el principal";
    Exigir(!C::Guardar(q), "guardado dice exito aunque falla el respaldo");
    Exigir(C::Leer(q), "principal perdido con respaldo ocupado");
    Igual(texto, C::Slot(0));
    std::cout << "PASS fallo de respaldo conserva principal\n";
#ifdef _WIN32
    // Un proceso hijo viejo puede tener el temporal heredado sin FILE_SHARE_DELETE.
    // Un nuevo guardado no debe truncarlo ni depender de que ese hijo termine.
    const std::string heredado = (dir / "heredado.toml").string();
    HANDLE ocupado = CreateFileW(fs::path(heredado + ".tmp").c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Exigir(ocupado != INVALID_HANDLE_VALUE, "crear temporal heredado ocupado");
    const bool salvo = C::Guardar(heredado);
    CloseHandle(ocupado);
    Exigir(salvo, "un temporal heredado impide guardar");
    Exigir(C::Leer(heredado), "leer con temporal heredado");
    Igual(texto, C::Slot(0));
    std::cout << "PASS temporal heredado ocupado no bloquea guardado\n";
#endif
    const std::string paralelo = (dir / "paralelo.toml").string();
    std::atomic<bool> bien{true};
    auto guardar = [&] { for (int n = 0; n < 20; ++n) if (!C::Guardar(paralelo)) bien = false; };
    std::thread a(guardar), b(guardar);
    a.join(); b.join();
    Exigir(bien, "dos guardados simultaneos colisionan");
    Exigir(C::Leer(paralelo), "leer tras guardados simultaneos");
    Igual(texto, C::Slot(0));
    std::cout << "PASS 40 guardados desde dos hilos\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL cac_guardado: " << e.what() << '\n';
    return 1;
  }
}

// Datos medidos del original, leidos de <exe>/res/datos/*.toml (los escribe tools/extraer_datos.py; X0c/X0d de studies 5).
// Sirven para que una camara, un tiempo o un sonido se corrija editando un .toml, sin recompilar:
// en modo pruebas (DBZ3_NO_DIALOGS) el juego vigila los ficheros y recarga solo; F5 recarga a mano siempre.
//
//   double fov = dbz3::datos::Num("camaras", "iw.nb013.fov", 0.785);   // fichero camaras.toml, [iw.nb013] fov = ...
//   std::string s = dbz3::datos::Str("b2_ordenes", "orden.58.funcion", "");
//
// Cada llamada lee el valor vigente: no guardes el resultado entre cuadros si quieres que F5 se vea.
#pragma once
#include <string>

namespace dbz3::datos {

double Num(const char* fichero, const char* ruta, double por_defecto);
std::string Str(const char* fichero, const char* ruta, const char* por_defecto);
bool Existe(const char* fichero, const char* ruta);

// Relee todos los ficheros (F5). Devuelve el numero de ficheros leidos.
int Recargar();
// Arranque: primera lectura y, en pruebas, el hilo que vigila los cambios. Idempotente.
void Iniciar();
// Para el vigilante de pruebas (se llama sola al salir). Idempotente.
void Detener();

}  // namespace dbz3::datos

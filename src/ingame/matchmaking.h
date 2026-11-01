// dbz3 - Matchmaking del online sin servidor propio: logica pura (formato de los anuncios, eleccion de sala, regla de
// la carrera). Los anuncios viajan por un servicio publico tipo ntfy.sh (un topic por version del juego + mods que
// cambian la partida). Sin dependencias del juego: se prueba aparte (tools/test_matchmaking.cpp).
#pragma once

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace dbz3::mm {

// Un anuncio vale mientras su ULTIMO mensaje sea OPEN y siga en la ventana del sondeo (since=80s, la mide el servidor:
// no depende del reloj de este PC). Se renueva cada 30 s: aguanta dos renovaciones perdidas.
inline std::string Message(const char* kind, const std::string& code) { return "dbz3 1 " + std::string(kind) + " " + code; }

inline std::string Topic(const std::string& build_hex, const std::string& mods_key) {
  auto clean = [](std::string s, size_t n) {
    std::string o;
    for (char c : s)
      if (std::isalnum((unsigned char)c) && o.size() < n) o += char(std::tolower((unsigned char)c));
    return o;
  };
  return "dbz3hd-mm-" + clean(build_hex, 10) + "-" + (mods_key.empty() ? "v" : clean(mods_key, 8));
}

// `body` = lineas JSON de ntfy (`/topic/json?poll=1`). Devuelve los codigos vivos, sin repetir.
inline std::vector<std::string> LiveCodes(const std::string& body) {
  std::vector<std::string> order;   // orden de primera aparicion
  std::vector<bool> open;
  size_t pos = 0;
  while (pos < body.size()) {
    size_t e = body.find('\n', pos);
    if (e == std::string::npos) e = body.size();
    const std::string line = body.substr(pos, e - pos);
    pos = e + 1;
    if (line.find("\"event\":\"message\"") == std::string::npos) continue;
    const size_t m = line.find("\"message\":\"");
    if (m == std::string::npos) continue;
    const size_t b = m + 11, q = line.find('"', b);
    if (q == std::string::npos) continue;
    const std::string msg = line.substr(b, q - b);   // "dbz3 1 OPEN XXXXX-XXXXX": solo ASCII, sin escapes
    if (msg.rfind("dbz3 1 ", 0) != 0) continue;
    const bool is_open = msg.compare(7, 5, "OPEN ") == 0;
    if (!is_open && msg.compare(7, 4, "BYE ") != 0) continue;
    const std::string code = msg.substr(is_open ? 12 : 11);
    if (code.size() != 11 || code[5] != '-') continue;
    size_t i = std::find(order.begin(), order.end(), code) - order.begin();
    if (i == order.size()) {
      order.push_back(code);
      open.push_back(false);
    }
    open[i] = is_open;   // el mensaje mas nuevo manda
  }
  std::vector<std::string> out;
  for (size_t i = 0; i < order.size(); ++i)
    if (open[i]) out.push_back(order[i]);
  return out;
}

// Sala a la que unirse: la de codigo MENOR (todos eligen igual) que no sea la mia ni una ya fallida. Con `mine`
// anunciada solo vale una MENOR que la mia: si dos anuncian a la vez, la mayor cede y se une a la menor.
inline std::string PickJoin(const std::vector<std::string>& live, const std::string& mine,
                            const std::set<std::string>& bad) {
  std::string best;
  for (const auto& c : live) {
    if (c == mine || bad.count(c)) continue;
    if (!mine.empty() && !(c < mine)) continue;
    if (best.empty() || c < best) best = c;
  }
  return best;
}

}  // namespace dbz3::mm

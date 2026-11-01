// Dragon Tactics H3/H4: pruebas de la partida (economia, tienda, copias, sinergias, bots, torneo de 8 entrenadores, emparejamientos, eliminacion,
// clasificacion, guardar y continuar), sin juego ni graficos.
// Uso: tactics_partida_test [carpeta "mod center hd"] [carpeta temporal] [partidas]   (desde la raiz del repo; devuelve 0 si todo pasa)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "src/tactics/partida.h"

using namespace dbz3::tactics;

static int g_fallos = 0;
static long g_comprobaciones = 0;
#define CHECK(c) do { ++g_comprobaciones; if (!(c)) { if (g_fallos++ < 30) std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static Catalogo g_cat;
static Economia g_eco;

// Estado completo de lo que una accion puede tocar: una accion rechazada no debe cambiar nada.
static std::string Foto(const Partida& p) {
  std::string s;
  for (int i = 0; i < p.N(); ++i) {
    const Jugador& j = p.J(i);
    s += std::to_string(j.oro) + "," + std::to_string(j.exp) + "," + std::to_string(j.nivel) + "," + std::to_string(j.vida) + ";";
    for (int c : j.tienda) s += std::to_string(c) + ",";
    for (const Pieza& z : j.pz) s += std::to_string(z.unidad) + "." + std::to_string(z.estrellas) + "." + std::to_string(z.caps[0]) + "." + std::to_string(z.caps[1]) + ",";
    for (int c : j.inv) s += std::to_string(c) + ",";
  }
  for (size_t u = 0; u < g_eco.unidades.size(); ++u) s += std::to_string(p.Libres(int(u))) + ",";
  return s;
}

// Invariantes que se cumplen despues de CUALQUIER accion.
static void Auditar(const Partida& p) {
  for (size_t u = 0; u < g_eco.unidades.size(); ++u) {   // reserva compartida: ni se crean ni se pierden copias (los eliminados las devuelven)
    int total = p.Libres(int(u));
    CHECK(p.Libres(int(u)) >= 0);
    for (int i = 0; i < p.N(); ++i) {
      for (int c : p.J(i).tienda) total += c == int(u);
      for (const Pieza& z : p.J(i).pz) if (z.unidad == int(u)) total += p.Peso(z.estrellas);
    }
    CHECK(total == g_eco.unidades[u].copias);
  }
  int vivos = 0;
  for (int i = 0; i < p.N(); ++i) {
    const Jugador& j = p.J(i);
    vivos += j.vivo;
    CHECK(j.oro >= 0);
    CHECK(j.nivel >= 1 && j.nivel <= g_eco.NivelMax() && j.vida >= 0 && j.vida <= g_eco.vida_entrenador);
    if (!j.vivo) {   // un eliminado no se queda con nada
      CHECK(j.vida == 0 && j.puesto >= 2 && j.puesto <= p.N() && p.Valor(i) == 0 && j.caps_total == 0);
      for (int c : j.inv) CHECK(c == 0);
      for (int c : j.tienda) CHECK(c < 0);
      continue;
    }
    CHECK(j.vida > 0);
    {   // capsulas: ni se crean ni se pierden (inventario + equipadas = recibidas), ninguna en una pieza vacia, y las de las rondas siempre llegan
      int suma = 0;
      for (int c : j.inv) { CHECK(c >= 0); suma += c; }
      for (const Pieza& z : j.pz)
        for (int c : z.caps) {
          CHECK(c >= -1 && c < int(g_eco.capsulas.size()) && (c < 0 || z.hay()));
          suma += c >= 0;
        }
      CHECK(suma == j.caps_total);
      CHECK(g_eco.capsula_cada == 0 || j.caps_total >= (p.Ronda() - 1) / g_eco.capsula_cada);
    }
    CHECK(j.oro + p.Valor(i) == j.ganado - j.gastado);   // el oro solo entra por ingresos y solo se pierde en servicios: sin beneficio comprando y vendiendo
    CHECK(p.EnTablero(i) <= p.TopeTablero(i));
    for (int a = 0; a < kSlots; ++a) {
      const Pieza& z = j.pz[a];
      CHECK(!z.hay() || (z.estrellas >= 1 && z.estrellas <= kMaxEstrellas && z.unidad < int(g_eco.unidades.size())));
      if (!z.hay() || z.estrellas == kMaxEstrellas) continue;
      int iguales = 0;
      for (const Pieza& o : j.pz) iguales += o.unidad == z.unidad && o.estrellas == z.estrellas;
      CHECK(iguales < 3);   // tres iguales siempre se combinan
    }
  }
  CHECK(vivos == p.Vivos() && vivos >= 1);
}

struct Estadistica {
  long partidas = 0, rondas = 0, max_rondas = 0, gana0 = 0, dos_estrellas = 0, tres_estrellas = 0, cuatro_estrellas = 0, rechazadas = 0, aceptadas = 0;
  long duelos_8 = 0, repetidos = 0, repetidos_8 = 0, copias = 0, copias_esperadas = 0, eliminaciones = 0, humano_cae = 0;
};

// Los duelos de una ronda: cada vivo en exactamente uno (como a, o como b real), copias solo si el numero de vivos es impar y nunca el propio tablero.
static void AuditarDuelos(const Partida& p, std::vector<int>* previo, Estadistica* st) {
  std::vector<int> veces(size_t(p.N()), 0);
  int copias = 0, neutrales = 0;
  for (const Duelo& d : p.Duelos()) {
    if (d.b == kNeutral) {   // ronda neutral: cada vivo contra el tablero sin dueno
      CHECK(d.a >= 0 && d.a < p.N() && p.J(d.a).vivo && d.copia && p.Neutral());
      ++veces[size_t(d.a)], ++neutrales;
      continue;
    }
    CHECK(d.a >= 0 && d.a < p.N() && d.b >= 0 && d.b < p.N() && d.a != d.b && p.J(d.a).vivo && p.J(d.b).vivo);
    ++veces[size_t(d.a)];
    if (d.copia) ++copias;
    else {
      ++veces[size_t(d.b)];
      CHECK(d.a < d.b);
    }
  }
  for (int i = 0; i < p.N(); ++i) CHECK(veces[size_t(i)] == (p.J(i).vivo ? 1 : 0));
  CHECK(copias == (neutrales ? 0 : p.Vivos() % 2));
  CHECK((neutrales > 0) == p.NeutralEn(p.Ronda()) && (neutrales == 0 || neutrales == p.Vivos()));
  st->copias += copias, st->copias_esperadas += neutrales ? 0 : p.Vivos() % 2;
  CHECK(p.J(0).vivo == (p.DueloHumano() >= 0));
  // rival repetido: solo se mira cuando los dos jugaron ronda real (las copias no cuentan como rival que se repite)
  for (const Duelo& d : p.Duelos()) {
    if (d.copia || d.b == kNeutral) continue;
    const bool igual = (*previo)[size_t(d.a)] == d.b || (*previo)[size_t(d.b)] == d.a;
    if (p.Vivos() >= 4) st->repetidos += igual, ++st->duelos_8;
    if (p.Vivos() == p.N() && p.N() == 8) {   // con todos vivos y parejas siempre se puede evitar
      st->repetidos_8 += igual;
      CHECK(!igual);
    }
  }
  for (const Duelo& d : p.Duelos()) if (d.b != kNeutral) (*previo)[size_t(d.a)] = d.b, (*previo)[size_t(d.b)] = d.a;
  for (const Duelo& d : p.Duelos()) if (d.copia && d.b != kNeutral) (*previo)[size_t(d.a)] = d.b;
}

enum class Humano { Piloto, Caos, Pasivo };

// Una partida entera de `n` entrenadores. Humano::Caos: acciones al azar (validas e invalidas); Pasivo: no hace nada (cae pronto y el resto se juega solo).
// guarda_en: si >= 0, en esa ronda se guarda, se carga una copia y las dos siguen: tienen que dar exactamente lo mismo.
static uint32_t Jugar(uint64_t semilla, int n, Humano h, Estadistica* st, std::vector<std::string>* rondas = nullptr, int guarda_en = -1) {
  Partida p(g_cat, g_eco, semilla, n);
  std::mt19937_64 azar(semilla * 7919 + 1);
  uint32_t huella = 2166136261u;
  const auto mezcla = [&](uint32_t v) { huella = (huella ^ v) * 16777619u; };
  std::vector<int> previo(size_t(n), -1);
  Auditar(p);
  std::optional<Partida> copia;
  while (!p.Acabada() && p.Ronda() < 500) {
    AuditarDuelos(p, &previo, st);
    if (p.Ronda() == guarda_en) {   // guardar -> cargar: la copia es la misma partida, incluido el azar
      const std::string texto = p.Guardar();
      std::string err;
      CHECK(!texto.empty() && Partida::Cargar(texto, g_cat, g_eco, &copia, &err));
      if (copia) CHECK(copia->Huella() == p.Huella() && copia->Guardar() == texto);
    }
    const std::mt19937_64 azar0 = azar;
    for (Partida* q : {&p, copia ? &*copia : nullptr}) {
      if (!q) continue;
      std::mt19937_64 az2 = azar0;   // las dos reciben las mismas acciones al azar
      if (h == Humano::Caos) {
        for (int k = 0, m = int(az2() % 14); k < m; ++k) {
          const std::string antes = Foto(*q);
          const int a = int(az2() % 9), x = int(az2() % 7) - 1, y = int(az2() % (kSlots + 2)) - 1;
          const Res r = a == 0   ? q->Comprar(0, x)
                        : a == 1 ? q->Vender(0, y)
                        : a == 2 ? q->Mover(0, y, int(az2() % kSlots))
                        : a == 3 ? q->Actualizar(0)
                        : a == 4 ? q->ComprarExp(0)
                        : a == 5 ? q->ComprarCapsula(0)
                        : a == 6 ? q->Equipar(0, y, int(az2() % 3) - 1, int(az2() % 11) - 1)
                        : a == 8 ? q->Bloquear(0)
                                 : q->Quitar(0, y, int(az2() % 3) - 1);
          if (q == &p) {
            if (r == Res::Ok) ++st->aceptadas;
            else ++st->rechazadas;
          }
          if (r != Res::Ok) CHECK(Foto(*q) == antes);
          if (q == &p) Auditar(*q);
        }
      } else if (h == Humano::Piloto) {
        q->Jugar(0);
        if (q == &p) Auditar(*q);
      }
      if (q == &p) azar = az2;
    }
    const bool humano_vivo = p.J(0).vivo;
    for (Partida* q : {&p, copia ? &*copia : nullptr}) {
      if (!q) continue;
      q->Listo();
      CHECK(q->FaseActual() == Fase::Combate);
      CHECK(q->Comprar(0, 0) == Res::FaseIncorrecta);   // sin compras en pleno combate
      if (q->DueloHumano() >= 0) {
        auto& sim = q->Combate();
        while (!sim.Terminada()) sim.Paso();
      }
    }
    std::vector<int> vida_antes;
    for (int i = 0; i < n; ++i) vida_antes.push_back(p.J(i).vida);
    const int rival = humano_vivo ? p.Duelos()[size_t(p.DueloHumano())].b : -1, rival_copia = humano_vivo ? p.Duelos()[size_t(p.DueloHumano())].copia : 0;
    const uint32_t hu = humano_vivo ? p.Combate().Huella() : 0;
    for (Partida* q : {&p, copia ? &*copia : nullptr}) if (q) q->TerminarCombate();
    CHECK(p.FaseActual() == Fase::Resultado);
    if (copia) CHECK(copia->Huella() == p.Huella() || copia->FaseActual() == Fase::Resultado);
    mezcla(hu);
    if (rondas && humano_vivo) {   // lo que imprime el juego (tools/pruebas.py tactics_partida lo compara): ganador, huella, vida del humano y de su rival
      char b[96];
      std::snprintf(b, sizeof b, "ronda %d: ganador %d huella %08x vida %d / %d", p.Ronda(), p.GanadorRonda(), hu, p.J(0).vida, rival >= 0 && !rival_copia ? p.J(rival).vida : 0);
      rondas->push_back(b);
    }
    for (int i = 0; i < n; ++i) {
      CHECK(vida_antes[size_t(i)] - p.J(i).vida == std::min(vida_antes[size_t(i)], p.DanoRonda(i)));   // un duelo contra una copia solo hace dano a quien la enfrenta, nunca a su dueño
      mezcla(uint32_t(p.J(i).vida)), mezcla(uint32_t(p.J(i).oro));
      for (const Pieza& z : p.J(i).pz) {
        st->dos_estrellas += z.estrellas == 2 && z.hay(), st->tres_estrellas += z.estrellas == 3 && z.hay(), st->cuatro_estrellas += z.estrellas == 4 && z.hay();
        mezcla(uint32_t(z.unidad + 1) * 4 + uint32_t(z.estrellas));
      }
      if (!p.J(i).vivo && p.J(i).ronda_baja == p.Ronda()) ++st->eliminaciones;
    }
    if (humano_vivo) CHECK(p.GanadorRonda() >= -1 && p.GanadorRonda() <= 1);
    if (!p.J(0).vivo) ++st->humano_cae;
    Auditar(p);
    for (Partida* q : {&p, copia ? &*copia : nullptr}) if (q) q->SiguienteRonda();
    if (copia && p.Acabada()) CHECK(copia->Acabada() && copia->Huella() == p.Huella());
    if (copia && !p.Acabada()) {   // tras el combate las dos siguen en la misma preparacion
      CHECK(copia->Ronda() == p.Ronda() && copia->Huella() == p.Huella());
      for (int i = 0; i < n; ++i) CHECK(copia->J(i).oro == p.J(i).oro && copia->J(i).vida == p.J(i).vida);
    }
    Auditar(p);
  }
  CHECK(p.Acabada());   // la partida siempre termina
  CHECK(p.Vivos() == 1 || p.Vivos() == 0);
  const std::vector<int> cl = p.Clasificacion();
  CHECK(int(cl.size()) == n && cl[0] == p.Campeon() && p.J(p.Campeon()).puesto == 1);
  for (int i = 0; i < n; ++i) {   // puestos 1..n sin repetir, y el que cae antes queda peor
    CHECK(p.J(cl[size_t(i)]).puesto == i + 1);
    if (i > 0) CHECK(p.J(cl[size_t(i - 1)]).ronda_baja == 0 || p.J(cl[size_t(i - 1)]).ronda_baja >= p.J(cl[size_t(i)]).ronda_baja);
  }
  ++st->partidas, st->rondas += p.Ronda(), st->max_rondas = std::max<long>(st->max_rondas, p.Ronda());
  st->gana0 += p.Campeon() == 0;
  mezcla(uint32_t(p.Huella() & 0xffffffffu));
  return huella;
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::string datos = argc > 1 ? argv[1] : "mod center hd", tmp = argc > 2 ? argv[2] : "D:/tmp";
  const int partidas = argc > 3 ? std::atoi(argv[3]) : 300;
  std::string err;
  if (!CargarCatalogo(datos + "/tactics_units.toml", datos + "/tactics_manifest.toml", &g_cat, &err) ||
      !CargarEconomia(datos + "/tactics_economia.toml", g_cat, &g_eco, &err)) {
    std::printf("no se pudieron cargar los datos:\n%s", err.c_str());
    return 2;
  }
  std::printf("economia: %zu unidades, %zu sinergias, %d niveles, %d entrenadores\n", g_eco.unidades.size(), g_eco.sinergias.size(), g_eco.NivelMax(), g_eco.jugadores);
  CHECK(g_eco.unidades.size() == 34 && g_eco.jugadores == 8);
  CHECK(g_eco.capsulas.size() >= 8 && g_eco.capsulas.size() <= 10 && g_eco.capsula_cada > 0 && g_eco.precio_capsula > 0);
  std::filesystem::copy_file(datos + "/tactics_capsulas.toml", tmp + "/tactics_capsulas.toml", std::filesystem::copy_options::overwrite_existing);

  {   // H10: tienda bloqueada (se conserva entre rondas hasta actualizar) y oro de racha
    CHECK(Partida(g_cat, g_eco, 7).OroRacha(2) == 0 && Partida(g_cat, g_eco, 7).OroRacha(3) == 1 && Partida(g_cat, g_eco, 7).OroRacha(-5) == 2 && Partida(g_cat, g_eco, 7).OroRacha(99) == g_eco.racha_tope);
    Partida p(g_cat, g_eco, 11);
    int t0[kMaxTienda];
    CHECK(p.Bloquear(0) == Res::Ok && p.J(0).bloq);
    for (int i = 0; i < kMaxTienda; ++i) t0[i] = p.J(0).tienda[i];
    p.Jugar(0);   // compra y coloca (la tienda bloqueada tambien se puede comprar: lo comprado queda -1)
    for (int i = 0; i < kMaxTienda; ++i) t0[i] = p.J(0).tienda[i];
    p.Listo();
    while (!p.Combate().Terminada()) p.Combate().Paso();
    p.TerminarCombate();
    p.SiguienteRonda();
    bool igual = true;
    for (int i = 0; i < kMaxTienda; ++i) igual = igual && p.J(0).tienda[i] == t0[i];
    CHECK(!p.J(0).vivo || igual);
    CHECK(p.J(0).racha >= -1 && p.J(0).racha <= 1 && std::abs(p.J(0).racha) == (p.GanadorRonda() == -1 ? 0 : 1));
    Auditar(p);
  }
  {   // validacion del TOML: un umbral que las unidades distintas no alcanzan, y una unidad desconocida
    std::ifstream in(datos + "/tactics_economia.toml");
    std::string t((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t a = t.find("id = \"misticos\""), i = t.find("n = 3", a);   // androides: 4 unidades distintas
    CHECK(a != std::string::npos && i != std::string::npos);
    std::string malo = t;
    malo.replace(i, 5, "n = 6");
    std::ofstream(tmp + "/tactics_economia_mala.toml") << malo;
    Economia e;
    std::string e1;
    CHECK(!CargarEconomia(tmp + "/tactics_economia_mala.toml", g_cat, &e, &e1) && e1.find("misticos") != std::string::npos);
    malo = t;
    malo.replace(malo.find("id = \"cell\""), 11, "id = \"nadie\"");
    std::ofstream(tmp + "/tactics_economia_mala.toml") << malo;
    std::string e2;
    CHECK(!CargarEconomia(tmp + "/tactics_economia_mala.toml", g_cat, &e, &e2) && e2.find("nadie") != std::string::npos);
    malo = t;
    malo.replace(malo.find("calidad_bots = ["), 16, "calidad_bots = [1, 2, 3]#");
    std::ofstream(tmp + "/tactics_economia_mala.toml") << malo;
    std::string e3;
    CHECK(!CargarEconomia(tmp + "/tactics_economia_mala.toml", g_cat, &e, &e3) && e3.find("calidad_bots") != std::string::npos);
    std::ifstream ic(datos + "/tactics_capsulas.toml");   // capsulas: una que no hace nada, un id repetido y menos de 8
    const std::string tc((std::istreambuf_iterator<char>(ic)), std::istreambuf_iterator<char>());
    std::ofstream(tmp + "/tactics_economia_buena.toml") << t;
    std::string mc = tc;
    mc.replace(mc.find("vida = 25"), 9, "vida = 0");
    std::ofstream(tmp + "/tactics_capsulas.toml") << mc;
    std::string e4;
    CHECK(!CargarEconomia(tmp + "/tactics_economia_buena.toml", g_cat, &e, &e4) && e4.find("vigor") != std::string::npos && e4.find("no hace nada") != std::string::npos);
    mc = tc;
    mc.replace(mc.find("id = \"fuerza\""), 13, "id = \"vigor\"");
    std::ofstream(tmp + "/tactics_capsulas.toml") << mc;
    std::string e5;
    CHECK(!CargarEconomia(tmp + "/tactics_economia_buena.toml", g_cat, &e, &e5) && e5.find("repetido") != std::string::npos);
    mc = tc.substr(0, tc.find("[[capsula]]", tc.find("id = \"foco\"")));
    std::ofstream(tmp + "/tactics_capsulas.toml") << mc;
    std::string e6;
    CHECK(!CargarEconomia(tmp + "/tactics_economia_buena.toml", g_cat, &e, &e6) && e6.find("entre 8") != std::string::npos);
    std::ofstream(tmp + "/tactics_capsulas.toml") << tc;
    CHECK(CargarEconomia(tmp + "/tactics_economia_buena.toml", g_cat, &e, &e6) && e.capsulas.size() == g_eco.capsulas.size());
  }

  {   // datos: dos sinergias por unidad, distintas; cada umbral con suficientes distintas (ya validado al cargar); cada coste con unidades
    for (const UnidadEco& u : g_eco.unidades) CHECK(u.sin[0] >= 0 && u.sin[1] >= 0 && u.sin[0] != u.sin[1] && u.coste >= 1 && u.coste <= 4);
    for (int c = 1; c <= 4; ++c) {
      int n = 0;
      for (const UnidadEco& u : g_eco.unidades) n += u.coste == c;
      CHECK(n >= 3);
    }
    for (size_t k = 0; k < g_eco.sinergias.size(); ++k) {
      int miembros = 0;
      for (const UnidadEco& u : g_eco.unidades) miembros += u.sin[0] == int(k) || u.sin[1] == int(k);
      CHECK(miembros >= g_eco.sinergias[k].umbrales.back().n + 1 || miembros >= g_eco.sinergias[k].umbrales.back().n);
    }
  }

  {   // inicio y compra/venta sin beneficio
    Partida p(g_cat, g_eco, 7);
    const Jugador& j = p.J(0);
    CHECK(p.N() == 8 && p.Vivos() == 8 && j.oro == 8 && j.nivel == 1 && p.TopeTablero(0) == 3 && p.FaseActual() == Fase::Preparacion && p.Ronda() == 1);
    int cartas = 0;
    for (int c : j.tienda) cartas += c >= 0;
    CHECK(cartas == 5);
    int c = 0;
    while (j.tienda[c] < 0 || g_eco.unidades[size_t(j.tienda[c])].coste > j.oro) ++c;
    const int oro0 = j.oro, coste = g_eco.unidades[size_t(j.tienda[c])].coste;
    CHECK(p.Comprar(0, c) == Res::Ok && j.oro == oro0 - coste && j.pz[0].hay());
    CHECK(p.Comprar(0, c) == Res::Invalido);   // la carta ya se compro
    CHECK(p.Vender(0, 0) == Res::Ok && j.oro == oro0 && !j.pz[0].hay());
    CHECK(p.Vender(0, 0) == Res::Invalido);
    CHECK(p.Actualizar(0) == Res::Ok && j.oro == oro0 - 2);
    CHECK(p.ComprarExp(0) == Res::Ok && j.oro == oro0 - 6 && j.exp == 0 && j.nivel == 2 && p.TopeTablero(0) == 4);
    CHECK(p.ComprarExp(0) == Res::SinOro && j.oro == oro0 - 6);
    Auditar(p);
  }

  {   // capsulas: equipar, cambiar, quitar, vender, combinar (el exceso vuelve al inventario) y sus efectos en el combate
    Economia rica = g_eco;   // oro de sobra y capsulas a 1 para poder montar cada caso
    rica.oro_inicial = 1000, rica.precio_capsula = 1;
    const auto buscar = [&](Partida& p, int u) {   // refresca hasta que la tienda ofrece `u`
      for (int i = 0; i < 600; ++i) {
        for (int c = 0; c < 5; ++c) if (p.J(0).tienda[c] == u) return c;
        CHECK(p.Actualizar(0) == Res::Ok);
      }
      return -1;
    };
    const auto con_inventario = [&](const Jugador& j) { for (int c = 0; c < int(rica.capsulas.size()); ++c) if (j.inv[c] > 0) return c; return -1; };
    const auto total = [&](const Jugador& j) { int t = 0; for (int c : j.inv) t += c; for (const Pieza& z : j.pz) t += (z.caps[0] >= 0) + (z.caps[1] >= 0); return t; };
    int barata = 0;
    while (rica.unidades[size_t(barata)].coste != 1) ++barata;
    {
      Partida p(g_cat, rica, 31, 2);
      const Jugador& j = p.J(0);
      CHECK(p.Equipar(0, 0, 0, 0) == Res::Invalido && p.ComprarCapsula(0) == Res::Ok && p.ComprarCapsula(0) == Res::Ok && p.ComprarCapsula(0) == Res::Ok && total(j) == 3 && j.caps_total == 3);
      CHECK(p.Equipar(0, 0, 0, con_inventario(j)) == Res::Invalido);   // la pieza no existe
      const int c0 = buscar(p, barata);
      CHECK(c0 >= 0 && p.Comprar(0, c0) == Res::Ok && j.pz[0].unidad == barata);
      const int a = con_inventario(j);
      CHECK(p.Equipar(0, 0, 0, a) == Res::Ok && j.pz[0].caps[0] == a && total(j) == 3);
      const std::string antes = Foto(p);
      CHECK(p.Equipar(0, 0, 2, a) == Res::Invalido && p.Equipar(0, 0, 0, int(rica.capsulas.size())) == Res::Invalido && Foto(p) == antes);
      int b = -1;
      for (int c = 0; c < int(rica.capsulas.size()); ++c) if (j.inv[c] > 0 && c != a) b = c;
      if (b >= 0) CHECK(p.Equipar(0, 0, 0, b) == Res::Ok && j.pz[0].caps[0] == b && j.inv[a] >= 1 && total(j) == 3);   // equipar sobre otra devuelve la anterior
      CHECK(p.Quitar(0, 0, 1) == Res::Invalido && p.Quitar(0, 0, 0) == Res::Ok && j.pz[0].caps[0] < 0 && total(j) == 3);
      CHECK(p.Equipar(0, 0, 0, con_inventario(j)) == Res::Ok && p.Equipar(0, 0, 1, con_inventario(j)) == Res::Ok);
      const int oro = j.oro;
      CHECK(p.Vender(0, 0) == Res::Ok && j.oro == oro + 1 && total(j) == 3 && j.caps_total == 3);   // vender devuelve las capsulas; las capsulas nunca dan oro
      Auditar(p);
      p.Listo();
      CHECK(p.Equipar(0, 0, 0, 0) == Res::FaseIncorrecta && p.Quitar(0, 0, 0) == Res::FaseIncorrecta && p.ComprarCapsula(0) == Res::FaseIncorrecta);
    }
    for (int caso = 0; caso < 3; ++caso) {   // combinar 3 copias con capsulas: se queda la mejor pieza con 2 y el resto vuelve al inventario
      Partida p(g_cat, rica, uint64_t(40 + caso), 2);
      const Jugador& j = p.J(0);
      for (int i = 0; i < 4; ++i) CHECK(p.ComprarCapsula(0) == Res::Ok);
      for (int copia = 0; copia < 2; ++copia) {
        const int c = buscar(p, barata);
        CHECK(c >= 0 && p.Comprar(0, c) == Res::Ok);
        int s = 0;
        while (!(j.pz[s].unidad == barata && j.pz[s].caps[0] < 0 && j.pz[s].caps[1] < 0)) ++s;
        CHECK(p.Equipar(0, s, 0, con_inventario(j)) == Res::Ok && p.Equipar(0, s, 1, con_inventario(j)) == Res::Ok);
        if (caso == 1 && copia == 0) CHECK(p.Mover(0, s, kBanco + 5) == Res::Ok);   // la del tablero es la que se queda
        if (caso == 2 && copia == 1) CHECK(p.Quitar(0, s, 1) == Res::Ok);           // una con un hueco libre
      }
      CHECK(total(j) == 4);
      const int c = buscar(p, barata);
      CHECK(c >= 0 && p.Comprar(0, c) == Res::Ok);
      int piezas = 0, estrellas = 0, equipadas = 0;
      for (const Pieza& z : j.pz) if (z.hay()) ++piezas, estrellas = z.estrellas, equipadas += (z.caps[0] >= 0) + (z.caps[1] >= 0);
      CHECK(piezas == 1 && estrellas == 2 && equipadas == 2 && total(j) == 4 && j.caps_total == 4);   // la nueva (sin capsulas) no pierde nada
      if (caso == 1) CHECK(j.pz[kBanco + 5].estrellas == 2);
      Auditar(p);
    }
    {   // H9: 27 copias de una unidad barata = una pieza de 4 estrellas (la maxima); la reserva sigue cuadrando
      Partida p(g_cat, rica, 77, 2);
      const Jugador& j = p.J(0);
      for (int i = 0; i < 27; ++i) {
        const int c = buscar(p, barata);
        CHECK(c >= 0 && p.Comprar(0, c) == Res::Ok);
        Auditar(p);
      }
      int piezas = 0, estr = 0;
      for (const Pieza& z : j.pz) if (z.hay()) ++piezas, estr = z.estrellas;
      CHECK(piezas == 1 && estr == kMaxEstrellas && p.Peso(estr) == 27);
      CHECK(p.Vender(0, [&] { for (int s = 0; s < kSlots; ++s) if (j.pz[s].hay()) return s; return 0; }()) == Res::Ok);
      Auditar(p);
    }
    for (int k = 0; k < int(rica.capsulas.size()); ++k) {   // cada capsula hace lo que dice: estadisticas de la unidad en la simulacion
      Partida p(g_cat, rica, uint64_t(60 + k), 2);
      const Jugador& j = p.J(0);
      for (int i = 0; i < 400 && j.inv[k] == 0; ++i) CHECK(p.ComprarCapsula(0) == Res::Ok);
      CHECK(j.inv[k] > 0);
      const int c = buscar(p, barata);
      CHECK(c >= 0 && p.Comprar(0, c) == Res::Ok && p.Mover(0, 0, kBanco) == Res::Ok && p.Equipar(0, kBanco, 1, k) == Res::Ok);
      p.Listo();
      const sim::DefUnidad& d = p.Combate().Unidades()[0].def;
      const CapsulaDef& cp = rica.capsulas[size_t(k)];
      sim::DefUnidad b = g_cat.unidades[size_t(rica.unidades[size_t(barata)].cat)];
      CHECK(d.vida == b.vida * (100 + cp.vida) / 100 && d.ataque == b.ataque * (100 + cp.ataque) / 100 && d.defensa == b.defensa * (100 + cp.defensa) / 100);
      CHECK(d.velocidad == std::max(1, b.velocidad * (100 + cp.velocidad) / 100) && d.alcance == b.alcance + cp.alcance && d.ki_inicial == cp.ki_inicial);
      CHECK(d.ki_max == std::max(20, b.ki_max * (100 + cp.ki_max) / 100) && d.tecnica.dano == b.tecnica.dano * (100 + cp.tecnica) / 100);
      CHECK(p.Combate().Unidades()[0].ki == std::min(d.ki_max, d.ki_max * ((b.pasiva == sim::Pasiva::KiInicial ? b.pasiva_valor : 0) + cp.ki_inicial) / 100));
    }
  }

  {   // tope del tablero, intercambios y banquillo lleno que aun deja completar una mejora
    Partida p(g_cat, g_eco, 11);
    const Jugador& j = p.J(0);
    for (int c = 0; c < 5; ++c) p.Comprar(0, c);
    int tablero = 0;
    for (int s = 0; s < kBanco && tablero < 3; ++s) if (j.pz[s].hay()) { CHECK(p.Mover(0, s, kBanco + tablero) == Res::Ok); ++tablero; }
    Auditar(p);
    CHECK(p.EnTablero(0) <= 3);
    int bench = -1;
    for (int s = 0; s < kBanco; ++s) if (j.pz[s].hay()) bench = s;
    if (bench >= 0 && p.EnTablero(0) == 3) {
      CHECK(p.Mover(0, bench, kBanco + 17) == Res::Tope);   // el cuarto no cabe
      const int antes = j.pz[kBanco].unidad;
      CHECK(p.Mover(0, bench, kBanco) == Res::Ok && j.pz[bench].unidad == antes);   // pero intercambiar si
    }
    Auditar(p);
  }

  {   // sinergias: los duplicados y las estrellas no suman miembros
    int saiyan = -1;
    for (size_t k = 0; k < g_eco.sinergias.size(); ++k) if (g_eco.sinergias[k].id == "saiyan") saiyan = int(k);
    bool visto_uno = false, visto_dos = false;
    for (uint64_t semilla = 1; semilla < 400 && !(visto_uno && visto_dos); ++semilla) {
      Partida p(g_cat, g_eco, semilla, 2);
      const Jugador& j = p.J(0);
      for (int ronda = 0; ronda < 6; ++ronda) {
        for (int c = 0; c < 5; ++c) if (j.tienda[c] >= 0 && g_eco.unidades[size_t(j.tienda[c])].sin[0] == saiyan) p.Comprar(0, c);
        int n = 0;
        for (int s = 0; s < kBanco && n < 3; ++s) if (j.pz[s].hay()) p.Mover(0, s, kBanco + n++);
        int miembros = 0, vistos = 0;
        for (const SinActiva& a : p.Sinergias(0)) if (a.sinergia == saiyan) miembros = a.cuenta;
        std::vector<int> ids;
        for (int s = kBanco; s < kSlots; ++s) if (j.pz[s].hay() && g_eco.unidades[size_t(j.pz[s].unidad)].sin[0] == saiyan) {
          bool nuevo = true;
          for (int i : ids) nuevo = nuevo && i != j.pz[s].unidad;
          if (nuevo) ids.push_back(j.pz[s].unidad);
          ++vistos;
        }
        CHECK(miembros == int(ids.size()));
        visto_uno = visto_uno || (miembros == 1 && vistos >= 2);   // dos copias del mismo = un miembro
        visto_dos = visto_dos || miembros == 2;
        p.Listo();
        while (!p.Combate().Terminada()) p.Combate().Paso();
        p.TerminarCombate();
        p.SiguienteRonda();
        if (p.Acabada()) break;
      }
    }
    CHECK(visto_uno && visto_dos);
  }

  {   // los 7 bots no ven al humano: su tablero al abrir la preparacion es el mismo pase lo que pase en el lado del humano
    Partida a(g_cat, g_eco, 99), b(g_cat, g_eco, 99);
    b.Actualizar(0), b.Comprar(0, 1), b.Comprar(0, 2);
    for (int i = 1; i < 8; ++i) {
      for (int s = 0; s < kSlots; ++s) CHECK(a.J(i).pz[s].unidad == b.J(i).pz[s].unidad && a.J(i).pz[s].estrellas == b.J(i).pz[s].estrellas);
      CHECK(a.J(i).oro == b.J(i).oro);
    }
  }

  {   // dificultad: la calidad se nota (los bots fuertes ganan mas veces que los flojos) y el orden de compra es reproducible
    long vidas[8] = {};
    const int N = 120;
    for (int s = 0; s < N; ++s) {
      Partida p(g_cat, g_eco, uint64_t(5000 + s));
      for (int i = 1; i < 8; ++i) CHECK(p.J(i).calidad == g_eco.calidad_bots[size_t(i - 1)]);
      while (!p.Acabada() && p.Ronda() < 500) {
        p.Jugar(0);
        p.Listo();
        while (p.DueloHumano() >= 0 && !p.Combate().Terminada()) p.Combate().Paso();
        p.TerminarCombate();
        p.SiguienteRonda();
      }
      for (int i = 0; i < 8; ++i) vidas[i] += 9 - p.J(i).puesto;   // puntos por puesto
    }
    std::printf("puntos por puesto (8 = campeon) con %d torneos: tu(piloto) %ld, bots calidad 30/45/55/65/75/85/95: %ld %ld %ld %ld %ld %ld %ld\n", N, vidas[0], vidas[1], vidas[2],
                vidas[3], vidas[4], vidas[5], vidas[6], vidas[7]);
    CHECK(vidas[7] + vidas[6] > vidas[1] + vidas[2]);   // los dos mejores suman mas que los dos peores
    Partida a(g_cat, g_eco, 321), b(g_cat, g_eco, 321);
    for (int i = 0; i < 8; ++i) for (int s = 0; s < kSlots; ++s) CHECK(a.J(i).pz[s].unidad == b.J(i).pz[s].unidad);
  }

  Estadistica piloto, caos, pasivo, guardado;
  std::vector<std::string> rondas2024;
  const uint32_t h1 = Jugar(2024, 8, Humano::Piloto, &piloto, &rondas2024), h2 = Jugar(2024, 8, Humano::Piloto, &piloto), h3 = Jugar(2025, 8, Humano::Piloto, &piloto);
  CHECK(h1 == h2);   // misma semilla = misma partida
  CHECK(h1 != h3);
  CHECK(Jugar(2024, 8, Humano::Caos, &caos) == Jugar(2024, 8, Humano::Caos, &caos));
  const uint32_t sin_guardar = Jugar(77, 8, Humano::Piloto, &guardado);   // guardar -> cargar -> seguir = la misma partida que sin guardar
  for (int ronda : {1, 2, 3, 6, 9}) CHECK(Jugar(77, 8, Humano::Piloto, &guardado, nullptr, ronda) == sin_guardar);
  CHECK(Jugar(78, 8, Humano::Caos, &guardado, nullptr, 2) == Jugar(78, 8, Humano::Caos, &guardado));
  for (int i = 0; i < partidas; ++i) {
    const uint64_t semilla = uint64_t(1000 + i);
    const int n = i % 5 == 4 ? 2 + int(semilla % 7) : 8;   // casi todo con 8; de vez en cuando 2..8 (con pares e impares de vivos)
    Jugar(semilla, n, i % 3 == 0 ? Humano::Caos : i % 3 == 1 ? Humano::Piloto : Humano::Pasivo, i % 3 == 0 ? &caos : i % 3 == 1 ? &piloto : &pasivo, nullptr, i % 4 == 3 ? 1 + i % 7 : -1);
  }
  CHECK(piloto.dos_estrellas > 0 && caos.dos_estrellas > 0);
  CHECK(pasivo.humano_cae == pasivo.partidas && pasivo.partidas > 0);   // el humano pasivo siempre cae y aun asi se acaba el torneo con clasificacion
  CHECK(piloto.copias == piloto.copias_esperadas && piloto.copias > 0);
  CHECK(piloto.eliminaciones > 0);

  {   // guardado en disco: escritura segura, copia anterior, rotos y editados rechazados
    Partida p(g_cat, g_eco, 4242);
    p.Jugar(0);
    const std::string ruta = tmp + "/hub_tactics_prueba.toml";
    std::remove(ruta.c_str()), std::remove((ruta + ".bak").c_str()), std::remove((ruta + ".tmp").c_str());
    std::string e;
    CHECK(GuardarArchivo(ruta, p.Guardar(), &e));
    const uint64_t h_a = p.Huella();
    p.Listo();
    while (!p.Combate().Terminada()) p.Combate().Paso();
    p.TerminarCombate();
    p.SiguienteRonda();
    CHECK(p.Guardar() != "" && GuardarArchivo(ruta, p.Guardar(), &e));   // la anterior pasa a .bak
    std::optional<Partida> c;
    CHECK(CargarArchivo(ruta, g_cat, g_eco, &c, &e) && c->Huella() == p.Huella() && c->Ronda() == p.Ronda());
    { std::ifstream t(ruta + ".tmp"); CHECK(!t); }   // no queda temporal
    { std::ifstream bk(ruta + ".bak"); CHECK(bool(bk)); }
    std::ifstream in(ruta, std::ios::binary);
    const std::string bueno((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::string roto = bueno.substr(0, bueno.size() / 2);   // cortado: se rechaza y se usa la copia anterior (ronda 1)
    std::ofstream(ruta, std::ios::binary | std::ios::trunc) << roto;
    std::optional<Partida> d;
    CHECK(!Partida::Cargar(roto, g_cat, g_eco, &d, &e) && !d);
    CHECK(CargarArchivo(ruta, g_cat, g_eco, &d, &e) && d->Ronda() == 1 && d->Huella() == h_a);
    std::string editado = bueno;
    const size_t k = editado.find("huella = ");
    const size_t fin = editado.find('\n', k) - (editado[editado.find('\n', k) - 1] == '\r' ? 1 : 0);   // la huella puede ser negativa: se cambia su ultima cifra, no la primera
    editado[fin - 1] = editado[fin - 1] == '1' ? '2' : '1';
    CHECK(!Partida::Cargar(editado, g_cat, g_eco, &d, &e) && e.find("huella") != std::string::npos);
    std::string raro = bueno;   // un valor imposible dentro del estado (oro negativo) tambien se rechaza aunque se arregle la huella
    const size_t ie = raro.find("estado = [");
    raro.insert(ie + 10, "-5, ");
    CHECK(!Partida::Cargar(raro, g_cat, g_eco, &d, &e));
    std::optional<Partida> otro;
    CHECK(!CargarArchivo(tmp + "/no_existe.toml", g_cat, g_eco, &otro, &e) && !otro);
    p.Listo();
    CHECK(p.Guardar().empty());   // en pleno combate no se guarda
  }

  const auto linea = [](const char* n, const Estadistica& s) {
    std::printf("%s: %ld partidas, rondas %.1f de media (max %ld), gana el humano %ld, mejoras vistas: 2 estrellas %ld, 3 estrellas %ld, 4 estrellas %ld, acciones aceptadas %ld, rechazadas %ld, eliminaciones %ld, copias %ld, "
                "rival repetido en %ld de %ld duelos con 4+ vivos (%ld repetidos con 8 vivos)\n",
                n, s.partidas, s.partidas ? double(s.rondas) / double(s.partidas) : 0.0, s.max_rondas, s.gana0, s.dos_estrellas, s.tres_estrellas, s.cuatro_estrellas, s.aceptadas, s.rechazadas, s.eliminaciones, s.copias,
                s.repetidos, s.duelos_8, s.repetidos_8);
  };
  for (size_t i = 0; i < rondas2024.size() && i < 3; ++i) std::printf("semilla 2024, %s\n", rondas2024[i].c_str());
  linea("piloto en el torneo", piloto);
  linea("acciones al azar en el torneo", caos);
  linea("humano pasivo (cae y el torneo sigue solo)", pasivo);
  linea("guardar y continuar", guardado);
  CHECK(piloto.duelos_8 > 0 && double(piloto.repetidos) / double(piloto.duelos_8) < 0.05);
  std::printf("\n%ld comprobaciones, %d fallos\n", g_comprobaciones, g_fallos);
  return g_fallos ? 1 : 0;
}

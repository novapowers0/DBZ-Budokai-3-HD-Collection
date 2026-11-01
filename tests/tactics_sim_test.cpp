// Dragon Tactics H2/T8-T9: pruebas de la simulacion y del catalogo, sin juego ni graficos.
// Uso: tactics_sim_test [carpeta "mod center hd"] [carpeta temporal]   (desde la raiz del repo; devuelve 0 si todo pasa)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "src/tactics/audio.h"
#include "src/tactics/catalogo.h"
#include "src/tactics/sim.h"

using namespace dbz3::tactics;
using namespace dbz3::tactics::sim;

static int g_fallos = 0, g_comprobaciones = 0;
#define CHECK(c) do { ++g_comprobaciones; if (!(c)) { std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fallos; } } while (0)

static Catalogo g_cat;
static DefUnidad U(const char* id) { return *g_cat.Buscar(id); }
static DefUnidad Maniqui() {   // blanco quieto y blando: velocidad 1 = un paso cada 10 s
  DefUnidad d; d.id = "maniqui"; d.vida = 5000; d.ataque = 1; d.defensa = 0; d.velocidad = 1; d.ki_max = 1000;
  return d;
}
struct Pieza { DefUnidad def; int equipo, col, fila; };
static Sim Montar(uint64_t semilla, const std::vector<Pieza>& ps) {
  Sim s(semilla);
  for (const Pieza& p : ps) CHECK(s.Anadir(p.def, p.equipo, {p.col, p.fila}) >= 0);
  return s;
}
// Duelo de partida: 3 contra 3 y 6 contra 6 con 8 de las 16 unidades.
static const std::vector<Pieza>& Escenario(bool seis) {
  static std::vector<Pieza> e3, e6;
  if (e3.empty()) {
    e3 = {{U("goku"), 0, 2, 3}, {U("vegeta"), 0, 3, 4}, {U("piccolo"), 0, 1, 4},
          {U("freezer"), 1, 2, 2}, {U("cell"), 1, 3, 1}, {U("a18"), 1, 1, 0}};
    e6 = e3;
    e6.insert(e6.end(), {{U("krilin"), 0, 4, 5}, {U("a16"), 0, 0, 3}, {U("a18"), 0, 2, 5},
                         {U("a16"), 1, 4, 2}, {U("krilin"), 1, 0, 1}, {U("goku"), 1, 2, 0}});
  }
  return seis ? e6 : e3;
}

// Ronda completa con distintas formas de "pintar": 0 un paso por fotograma, 1 con lectura de eventos y azar cosmetico,
// 2/3 dos o cuatro pasos por fotograma (x2, x4), 4 fotogramas irregulares (FPS variable).
static void Correr(Sim& s, int modo) {
  Rng fps = Flujo(7, 9);
  size_t leidos = 0;
  while (!s.Terminada()) {
    int n = modo == 2 ? 2 : modo == 3 ? 4 : modo == 4 ? 1 + static_cast<int>(fps.Hasta(6)) : 1;
    for (int i = 0; i < n; ++i) s.Paso();
    if (modo == 1) { for (; leidos < s.Eventos().size(); ++leidos) s.Cosmetico().Siguiente(); s.Cosmetico().Siguiente(); }
  }
}

struct Datos { int fallos_alcance = 0, ticks = 0; unsigned min_hueco_tecnica = 1u << 30; };
// Ronda paso a paso comprobando las reglas en cada tick (alcance, ocupacion, objetivos, inmovilidad al atacar, hueco entre tecnicas).
static Datos Vigilar(Sim& s) {
  Datos d;
  std::map<int, uint32_t> ultima_tecnica;
  size_t leidos = 0;
  while (!s.Terminada() && s.Tick() < 100000) {
    std::vector<Unidad> antes = s.Unidades();
    s.Paso();
    const auto& us = s.Unidades();
    for (; leidos < s.Eventos().size(); ++leidos) {
      const Evento& e = s.Eventos()[leidos];
      if (e.tipo == Tipo::Impacto && e.unidad >= 0) {   // las posiciones de "antes" son las del impacto (las llegadas se resuelven despues)
        const DefUnidad& a = antes[e.unidad].def;
        int dist = Distancia({antes[e.unidad].casilla % kAncho, antes[e.unidad].casilla / kAncho}, {antes[e.objetivo].casilla % kAncho, antes[e.objetivo].casilla / kAncho});
        int tope = e.accion == Accion::Tecnica ? a.tecnica.alcance + a.tecnica.area : a.alcance;
        if (e.accion == Accion::Tecnica && a.tecnica.forma == Forma::Linea) tope = a.tecnica.area;
        if (dist > tope) ++d.fallos_alcance;
      }
      if (e.tipo == Tipo::Inicio && e.accion != Accion::Mover) {
        CHECK(us[e.objetivo].viva());   // nunca se empieza a pegar a quien ya ha caido
        if (e.accion == Accion::Tecnica) {
          CHECK(us[e.unidad].ki == 0);
          auto it = ultima_tecnica.find(e.unidad);
          if (it != ultima_tecnica.end()) d.min_hueco_tecnica = std::min(d.min_hueco_tecnica, e.tick - it->second);
          ultima_tecnica[e.unidad] = e.tick;
        }
      }
    }
    bool usada[kAncho * kAlto] = {};
    for (size_t i = 0; i < us.size(); ++i) {
      if (!us[i].viva()) continue;
      CHECK(!usada[us[i].casilla]); usada[us[i].casilla] = true;
      if (us[i].destino >= 0) { CHECK(!usada[us[i].destino]); usada[us[i].destino] = true; }
      bool ataca = antes[i].estado == Estado::Basico || antes[i].estado == Estado::Tecnica;
      if (ataca && us[i].viva()) CHECK(us[i].casilla == antes[i].casilla);
    }
  }
  d.ticks = static_cast<int>(s.Tick());
  return d;
}

static const Evento* Primero(const Sim& s, Tipo t, Accion a, int unidad) {
  for (const Evento& e : s.Eventos()) if (e.tipo == t && e.accion == a && e.unidad == unidad) return &e;
  return nullptr;
}

int main(int argc, char** argv) {
  std::string datos = argc > 1 ? argv[1] : "mod center hd", tmp = argc > 2 ? argv[2] : "D:/tmp";
  // ---- T9: catalogo
  std::string err;
  CHECK(CargarCatalogo(datos + "/tactics_units.toml", datos + "/tactics_manifest.toml", &g_cat, &err));
  if (!err.empty()) std::printf("%s", err.c_str());
  CHECK(g_cat.unidades.size() == 34);
  for (const char* id : {"goku", "vegeta", "piccolo", "krilin", "a16", "a18", "freezer", "cell", "gohan", "trunks", "goten", "superbuu", "broly", "bardock", "cooler", "recoome"}) CHECK(g_cat.Buscar(id));
  {
    std::ofstream(tmp + "/tactics_malo.toml") << "version = 1\n[[unidad]]\nid = \"x\"\nvida = 0\n[[unidad]]\nid = \"x\"\n";
    Catalogo c; std::string e2;
    CHECK(!CargarCatalogo(tmp + "/tactics_malo.toml", datos + "/tactics_manifest.toml", &c, &e2));
    CHECK(e2.find("id repetido") != std::string::npos && e2.find("'vida' fuera de rango") != std::string::npos && e2.find("no esta en el manifiesto") != std::string::npos);
    CHECK(!CargarCatalogo(tmp + "/no_existe.toml", "", &c, &e2));
  }
  if (g_cat.unidades.size() != 34) { std::printf("sin catalogo no se puede seguir\n"); return 1; }

  // ---- hex: vecinos y distancias
  CHECK(Distancia({0, 0}, {5, 5}) == 8 && Distancia({2, 2}, {2, 3}) == 1 && Distancia({2, 2}, {1, 3}) == 1 && Distancia({2, 2}, {3, 3}) == 2);
  for (int f = 0; f < kAlto; ++f) for (int c = 0; c < kAncho; ++c) for (int d = 0; d < 6; ++d) {
    int n = Vecino({c, f}, d);
    if (n >= 0) CHECK(Distancia({c, f}, {n % kAncho, n / kAncho}) == 1 && Vecino({n % kAncho, n / kAncho}, (d + 3) % 6) == f * kAncho + c);
  }
  CHECK(Vecino({0, 0}, 3) == -1 && Vecino({5, 5}, 0) == -1);

  // ---- (1) determinismo: misma semilla y ordenes = mismos eventos y resultado, con o sin "graficos", x1/x2/x4 y FPS variable
  for (bool seis : {false, true}) {
    Sim ref = Montar(2024, Escenario(seis));
    Correr(ref, 0);
    CHECK(ref.Terminada() && ref.Eventos().size() > 20);
    for (int modo = 0; modo <= 4; ++modo) {
      Sim s = Montar(2024, Escenario(seis));
      Correr(s, modo);
      CHECK(s.Huella() == ref.Huella() && s.Eventos().size() == ref.Eventos().size());
      CHECK(s.Resumen().ganador == ref.Resumen().ganador && s.Resumen().ticks == ref.Resumen().ticks);
      for (size_t i = 0; i < s.Unidades().size(); ++i) CHECK(s.Unidades()[i].vida == ref.Unidades()[i].vida && s.Unidades()[i].casilla == ref.Unidades()[i].casilla);
    }
    int distintas = 0;   // la semilla si importa (las decisiones empatadas se barajan con el azar de combate)
    for (uint64_t sd = 1; sd <= 20; ++sd) { Sim s = Montar(sd, Escenario(seis)); Correr(s, 0); distintas += s.Huella() != ref.Huella(); }
    CHECK(distintas > 0);
    std::printf("%s: ganador %d en %u ticks (%.1f s), %zu eventos, huella %08x\n", seis ? "6v6" : "3v3", ref.Resumen().ganador, ref.Resumen().ticks,
                ref.Resumen().ticks / 60.0, ref.Eventos().size(), ref.Huella());
  }
  {   // estado del azar serializable
    Rng a = Flujo(5, 0); a.Siguiente(); a.Siguiente();
    Rng b; b.estado = a.estado;
    CHECK(a.Siguiente() == b.Siguiente() && a.Hasta(100) == b.Hasta(100));
    CHECK(Flujo(5, 0).Siguiente() != Flujo(5, 1).Siguiente());
  }

  // ---- (2) cambiar un defensor de casilla cambia el resultado y se explica: Cell (cuerpo a cuerpo) pega al que tiene delante
  {
    auto duelo = [&](bool a16_delante) {
      return Montar(11, {{U("cell"), 1, 2, 2}, {U("a16"), 0, a16_delante ? 2 : 0, a16_delante ? 3 : 5}, {U("krilin"), 0, a16_delante ? 0 : 2, a16_delante ? 5 : 3}});
    };
    Sim a = duelo(true), b = duelo(false);
    Correr(a, 0); Correr(b, 0);
    auto primera_victima = [](const Sim& s) { const Evento* e = Primero(s, Tipo::Impacto, Accion::Basico, 0); return e ? e->objetivo : -1; };
    CHECK(primera_victima(a) == 1 && primera_victima(b) == 2);   // a16 es la unidad 1, krilin la 2; Cell siempre pega al mas cercano
    CHECK(a.Huella() != b.Huella());
    CHECK(Primero(a, Tipo::Impacto, Accion::Basico, 0)->valor < Primero(b, Tipo::Impacto, Accion::Basico, 0)->valor);   // y a16 (defensa 35) aguanta mas que krilin (12)
    std::printf("defensor: A-16 delante -> Cell pega a la unidad %d por %d; delante Krilin -> unidad %d por %d\n", primera_victima(a),
                Primero(a, Tipo::Impacto, Accion::Basico, 0)->valor, primera_victima(b), Primero(b, Tipo::Impacto, Accion::Basico, 0)->valor);
  }

  // ---- (3)(5) barrido: 400 rondas con composiciones y casillas al azar. Alcance, ocupacion, objetivos y fin de ronda en cada tick.
  {
    int terminadas = 0, fallos_alcance = 0, max_ticks = 0, empates = 0, por_equipo[2] = {0, 0};
    unsigned hueco = 1u << 30;
    for (uint64_t sd = 1; sd <= 400; ++sd) {
      Rng r = Flujo(sd, 3);
      Sim s(sd);
      for (int eq = 0; eq < 2; ++eq) {
        bool usada[kAncho * kAlto] = {};
        int n = 1 + static_cast<int>(r.Hasta(6));
        for (int i = 0; i < n; ++i) {
          int f = (eq == 0 ? 3 : 0) + static_cast<int>(r.Hasta(3)), c = static_cast<int>(r.Hasta(kAncho));
          if (usada[f * kAncho + c]) continue;
          usada[f * kAncho + c] = true;
          s.Anadir(g_cat.unidades[r.Hasta(8)], eq, {c, f});
        }
      }
      Datos d = Vigilar(s);
      terminadas += s.Terminada(); fallos_alcance += d.fallos_alcance; max_ticks = std::max(max_ticks, d.ticks);
      hueco = std::min(hueco, d.min_hueco_tecnica);
      Resultado res = s.Resumen();
      empates += res.ganador == -1; if (res.ganador >= 0) ++por_equipo[res.ganador];
      CHECK(res.vivas[0] == 0 || res.vivas[1] == 0 || d.ticks == kTopeTicks);   // o cae un bando o se agota el tiempo
    }
    CHECK(terminadas == 400 && fallos_alcance == 0 && max_ticks <= kTopeTicks);
    std::printf("barrido 400 rondas: todas terminan (max %d ticks de %d), golpes fuera de alcance %d, victorias %d/%d, empates %d, hueco minimo entre tecnicas %u ticks\n",
                max_ticks, kTopeTicks, fallos_alcance, por_equipo[0], por_equipo[1], empates, hueco);
  }

  // ---- H13 reglas de objetivo y movimiento: una unidad por casilla, pocos cambios de objetivo y casi nada de ir y volver (A -> B -> A)
  {
    long unidades = 0, cambios = 0, vaiven = 0, solapes = 0;
    for (uint64_t sd = 1; sd <= 200; ++sd) {
      Rng r = Flujo(sd, 8);
      Sim s(sd);
      for (int eq = 0; eq < 2; ++eq) {
        bool usada[kAncho * kAlto] = {};
        for (int i = 0; i < 6; ++i) {
          int f = (eq == 0 ? 3 : 0) + static_cast<int>(r.Hasta(3)), c = static_cast<int>(r.Hasta(kAncho));
          if (!usada[f * kAncho + c]) usada[f * kAncho + c] = true, s.Anadir(g_cat.unidades[r.Hasta(12)], eq, {c, f});
        }
      }
      const size_t n = s.Unidades().size();
      std::vector<int> obj(n, -1), ult(n), pen(n, -1);
      for (size_t i = 0; i < n; ++i) ult[i] = s.Unidades()[i].casilla;
      unidades += static_cast<long>(n);
      while (!s.Terminada()) {
        s.Paso();
        bool ocupada[kAncho * kAlto] = {};
        for (size_t i = 0; i < n; ++i) {
          const Unidad& u = s.Unidades()[i];
          if (!u.viva()) continue;
          for (int c : {u.casilla, u.destino}) if (c >= 0) { solapes += ocupada[c]; ocupada[c] = true; }
          if (u.objetivo != obj[i]) { cambios += obj[i] >= 0 && s.Unidades()[static_cast<size_t>(obj[i])].viva(); obj[i] = u.objetivo; }
          if (u.casilla != ult[i]) { vaiven += pen[i] == u.casilla; pen[i] = ult[i]; ult[i] = u.casilla; }
        }
      }
    }
    std::printf("movimiento (200 rondas 6v6): %ld unidades, solapes de casilla %ld, cambios de objetivo %.3f por unidad, ida y vuelta %.3f por unidad\n", unidades, solapes,
                double(cambios) / double(unidades), double(vaiven) / double(unidades));
    CHECK(solapes == 0);
    CHECK(double(cambios) / double(unidades) < 0.20);    // banco de estres con casillas al azar; en partidas reales: 0,14 antes de H13 y 0,03 ahora
    CHECK(double(vaiven) / double(unidades) < 0.08);     // en partidas reales: 0,036 antes de H13 y 0,02 ahora; tope holgado
  }
  // ---- H13 colocar importa: los mismos 6 personajes con a distancia delante pierden mucho frente al mismo rival que si van detras
  {
    double bien = 0, mal = 0; int duelos = 0;
    for (uint64_t sd = 1; sd <= 400; ++sd) {
      Rng r = Flujo(sd, 9);
      std::vector<int> a, b;
      for (std::vector<int>* t : {&a, &b}) while (t->size() < 6) { int u = static_cast<int>(r.Hasta(12)); if (std::find(t->begin(), t->end(), u) == t->end()) t->push_back(u); }
      const auto jugar = [&](bool a_bien) {
        Sim s(sd);
        const auto poner = [&](const std::vector<int>& us, int eq, bool bien_) {
          static const int centro[6] = {2, 3, 1, 4, 0, 5};
          std::vector<int> usadas;
          for (int pasada = 0; pasada < 2; ++pasada)
            for (int u : us) {
              if ((g_cat.unidades[static_cast<size_t>(u)].alcance <= 1) != (pasada == 0)) continue;
              const bool delante = (pasada == 0) == bien_;
              for (int k = 0; k < 3; ++k) {
                bool puesta = false;
                for (int c : centro) {
                  const int cu = (delante ? k : 2 - k) * 6 + c;
                  if (std::find(usadas.begin(), usadas.end(), cu) != usadas.end()) continue;
                  usadas.push_back(cu); puesta = true;
                  s.Anadir(g_cat.unidades[static_cast<size_t>(u)], eq, {eq == 0 ? cu % 6 : 5 - cu % 6, eq == 0 ? 3 + cu / 6 : 2 - cu / 6});
                  break;
                }
                if (puesta) break;
              }
            }
        };
        poner(a, 0, a_bien); poner(b, 1, true);
        while (!s.Terminada()) s.Paso();
        const int g = s.Resumen().ganador;
        return g < 0 ? 0.5 : g == 0 ? 1.0 : 0.0;
      };
      bien += jugar(true), mal += jugar(false), ++duelos;
    }
    std::printf("colocacion (%d duelos de 6): bien colocados ganan %.1f %%, a distancia delante %.1f %%\n", duelos, 100.0 * bien / duelos, 100.0 * mal / duelos);
    CHECK(bien - mal > 0.10 * duelos);    // colocar cambia el resultado al menos 10 puntos (medido: unos 25)
  }

  // ---- (3) bloqueada: Cell cercada por 4 aliados en el fondo; debe salir y actuar cuando le dejen paso
  {
    Sim s = Montar(3, {{U("cell"), 1, 2, 0}, {Maniqui(), 1, 1, 0}, {Maniqui(), 1, 3, 0}, {Maniqui(), 1, 1, 1}, {Maniqui(), 1, 2, 1},
                       {U("goku"), 0, 2, 4}});
    Datos d = Vigilar(s);
    CHECK(s.Terminada());
    const Evento* sale = nullptr;
    for (const Evento& e : s.Eventos()) if (e.tipo == Tipo::Inicio && e.unidad == 0) { sale = &e; break; }
    CHECK(sale != nullptr && sale->tick > 1);   // al principio no podia moverse, despues si
    std::printf("bloqueada: Cell cercada sale en el tick %u, ronda de %d ticks\n", sale ? sale->tick : 0, d.ticks);
  }
  // sin objetivo: al caer su objetivo cada atacante elige otro (Vigilar ya comprueba que nunca se pega a un caido); y sin enemigos la ronda acaba
  {
    Sim s(1); s.Anadir(U("goku"), 0, {2, 3}); s.Paso();
    CHECK(s.Terminada() && s.Resumen().ganador == 0 && s.Tick() == 1);
    Sim v(1); v.Paso();
    CHECK(v.Terminada() && v.Resumen().ganador == -1);
    Sim m = Montar(1, {{[] { DefUnidad d = U("goku"); d.vida = 1; return d; }(), 0, 2, 3}, {[] { DefUnidad d = U("goku"); d.vida = 1; return d; }(), 1, 2, 2}});
    Correr(m, 0);   // golpes simultaneos: caen los dos a la vez, nadie ataca primero
    CHECK(m.Resumen().ganador == -1 && m.Resumen().vivas[0] == 0 && m.Resumen().vivas[1] == 0);
  }

  // ---- (5) empate por tiempo: dos muros que no se pueden matar; a los 60 s el desgaste igual para todos decide por vida restante
  {
    DefUnidad muro = Maniqui(); muro.id = "muro"; muro.defensa = 200; muro.velocidad = 10;
    Sim s = Montar(4, {{muro, 0, 2, 3}, {muro, 1, 2, 2}});
    Correr(s, 0);
    Resultado r = s.Resumen();
    CHECK(r.terminada && r.ticks == kTopeTicks && r.vivas[0] == 1 && r.vivas[1] == 1);
    CHECK(r.vida[0] < 5000 && r.vida[1] < 5000);   // el desgaste ha golpeado a los dos
    CHECK(r.ganador == (r.vida[0] > r.vida[1] ? 0 : r.vida[1] > r.vida[0] ? 1 : -1));
  }
  // ningun bando ataca siempre primero: 3 contra 3 con las mismas unidades y semillas distintas; ni gana siempre uno ni cae siempre el mismo
  {
    int gana[2] = {0, 0}, empates = 0;
    for (uint64_t sd = 1; sd <= 200; ++sd) {
      Sim s = Montar(sd, {{U("goku"), 0, 1, 3}, {U("vegeta"), 0, 3, 3}, {U("piccolo"), 0, 2, 4},
                          {U("goku"), 1, 4, 2}, {U("vegeta"), 1, 2, 2}, {U("piccolo"), 1, 3, 1}});
      Correr(s, 0);
      if (s.Resumen().ganador >= 0) ++gana[s.Resumen().ganador]; else ++empates;
    }
    CHECK(std::abs(gana[0] - gana[1]) <= 10);   // la rejilla es simetrica (el tablero de arriba va girado 180 grados): ningun bando parte con ventaja
    std::printf("espejo 3v3, 200 semillas: victorias %d / %d, empates %d\n", gana[0], gana[1], empates);
    int mismo = 0, juegos = 0;   // dos equipos distintos: si cambian de lado (y de tablero) gana el mismo equipo
    for (uint64_t sd = 1; sd <= 100; ++sd) {
      Sim x = Montar(sd, {{U("goku"), 0, 1, 3}, {U("vegeta"), 0, 3, 3}, {U("piccolo"), 0, 2, 4}, {U("cell"), 1, 4, 2}, {U("freezer"), 1, 2, 2}, {U("a18"), 1, 3, 1}});
      Sim y = Montar(sd, {{U("cell"), 0, 1, 3}, {U("freezer"), 0, 3, 3}, {U("a18"), 0, 2, 4}, {U("goku"), 1, 4, 2}, {U("vegeta"), 1, 2, 2}, {U("piccolo"), 1, 3, 1}});
      Correr(x, 0);
      Correr(y, 0);
      const int gx = x.Resumen().ganador, gy = y.Resumen().ganador;
      if (gx >= 0 && gy >= 0) {
        ++juegos;
        mismo += gx == 1 - gy;
      }
    }
    CHECK(juegos > 50 && mismo * 10 >= juegos * 9);
    std::printf("cambio de lado: %d de %d duelos los gana el mismo equipo\n", mismo, juegos);
  }

  // ---- (4) ki y tecnicas sin bucles: ki_max minimo, acciones de 2 ticks y 200 de ki por segundo
  {
    DefUnidad loco = U("goku"); loco.id = "loco"; loco.ki_max = 20; loco.ataque_ticks = 2; loco.tecnica_ticks = 2;
    loco.pasiva = Pasiva::KiRegular; loco.pasiva_valor = 200; loco.vida = 5000; loco.defensa = 200;
    Sim s = Montar(5, {{loco, 0, 2, 3}, {loco, 1, 2, 2}, {loco, 0, 3, 3}, {loco, 1, 1, 2}});
    Datos d = Vigilar(s);
    CHECK(s.Terminada() && d.ticks <= kTopeTicks && s.Eventos().size() < 100000);
    // sin pasiva: la tecnica no sale hasta acumular ki (10 por golpe dado y 5 por golpe recibido) y deja el ki a 0
    DefUnidad n = U("a18"); n.pasiva = Pasiva::Ninguna; n.vida = 3000; n.defensa = 100; n.alcance = 1;
    Sim t = Montar(6, {{n, 0, 2, 3}, {n, 1, 2, 2}});
    Correr(t, 0);
    int ki = 0; bool lanzada = false;
    for (const Evento& e : t.Eventos()) {
      if (e.unidad != 0 && e.objetivo != 0) continue;
      if (e.tipo == Tipo::Impacto && e.accion == Accion::Basico) ki += e.unidad == 0 ? kKiPorGolpe : kKiPorRecibido;
      if (e.tipo == Tipo::Inicio && e.accion == Accion::Tecnica && e.unidad == 0 && !lanzada) { lanzada = true; CHECK(ki >= n.ki_max); }
    }
    CHECK(lanzada);
  }

  // ---- (6) comportamientos de teamfight-simulator que aplican: andar hasta el alcance, ki por golpe, tecnica al llenar, areas, pasivas
  {
    for (int alc : {1, 3}) {   // la unidad se detiene en cuanto el objetivo entra en alcance
      DefUnidad d = U("goku"); d.alcance = alc; d.pasiva = Pasiva::Ninguna;
      Sim s = Montar(8, {{d, 0, 2, 5}, {Maniqui(), 1, 2, 1}});
      while (!s.Terminada() && !Primero(s, Tipo::Inicio, Accion::Basico, 0)) s.Paso();
      const Unidad &a = s.Unidades()[0], &o = s.Unidades()[1];
      CHECK(Distancia({a.casilla % kAncho, a.casilla / kAncho}, {o.casilla % kAncho, o.casilla / kAncho}) == alc);
    }
    {   // ki: +10 al golpear, +5 al recibir
      DefUnidad d = U("goku"); d.pasiva = Pasiva::Ninguna;
      Sim s = Montar(9, {{d, 0, 2, 3}, {Maniqui(), 1, 2, 2}});
      while (!s.Terminada() && !Primero(s, Tipo::Impacto, Accion::Basico, 0)) s.Paso();
      CHECK(s.Unidades()[0].ki == kKiPorGolpe && s.Unidades()[1].ki == kKiPorRecibido);
    }
    {   // circulo: una tecnica toca a todo el grupo enemigo cercano al objetivo, nunca a los aliados
      DefUnidad f = U("freezer"); f.pasiva = Pasiva::KiInicial; f.pasiva_valor = 100;
      Sim s = Montar(10, {{f, 0, 2, 4}, {Maniqui(), 1, 2, 2}, {Maniqui(), 1, 3, 2}, {Maniqui(), 1, 2, 1}, {Maniqui(), 0, 1, 3}});
      while (!s.Terminada() && !Primero(s, Tipo::Impacto, Accion::Tecnica, 0)) s.Paso();
      int victimas = 0; for (const Evento& e : s.Eventos()) if (e.tipo == Tipo::Impacto && e.accion == Accion::Tecnica) { ++victimas; CHECK(s.Unidades()[e.objetivo].equipo == 1); }
      CHECK(victimas >= 2);
      // linea: atraviesa a los tres enemigos alineados al noroeste
      DefUnidad v = U("vegeta"); v.pasiva = Pasiva::KiInicial; v.pasiva_valor = 100;
      Sim l = Montar(10, {{v, 0, 2, 3}, {Maniqui(), 1, 2, 2}, {Maniqui(), 1, 1, 1}, {Maniqui(), 1, 1, 0}, {Maniqui(), 1, 4, 2}});
      while (!l.Terminada() && !Primero(l, Tipo::Impacto, Accion::Tecnica, 0)) l.Paso();
      int en_linea = 0; for (const Evento& e : l.Eventos()) if (e.tipo == Tipo::Impacto && e.accion == Accion::Tecnica) ++en_linea;
      CHECK(en_linea == 3);
      std::printf("areas: circulo toca %d, linea toca %d\n", victimas, en_linea);
    }
    {   // pasivas: perfora ignora media defensa; regenera con tope
      DefUnidad p = U("piccolo"), q = p; q.pasiva = Pasiva::Ninguna;
      DefUnidad muro = Maniqui(); muro.defensa = 40;
      Sim a = Montar(12, {{p, 0, 2, 3}, {muro, 1, 2, 2}}), b = Montar(12, {{q, 0, 2, 3}, {muro, 1, 2, 2}});
      Correr(a, 0); Correr(b, 0);
      CHECK(Primero(a, Tipo::Impacto, Accion::Basico, 0)->valor == p.ataque * 100 / 120 && Primero(b, Tipo::Impacto, Accion::Basico, 0)->valor == p.ataque * 100 / 140);
      DefUnidad c = U("cell"); c.vida = 1000; c.defensa = 0; c.pasiva_valor = 50;   // regenera el 50 % por segundo, pero con tope del 25 % por ronda
      DefUnidad golpeador = Maniqui(); golpeador.velocidad = 10; golpeador.ataque = 100;
      Sim r = Montar(13, {{c, 0, 2, 3}, {golpeador, 1, 2, 2}});
      Correr(r, 0);
      CHECK(r.Unidades()[0].regenerado == 250);
    }
  }

  {   // mezcla de sonido: maximo de voces a la vez, y las tecnicas y los KO siempre tienen sitio
    MezclaSe m;
    int suenan = 0;
    for (int i = 0; i < 200; ++i) suenan += m.Pedir(1.0 + i * 0.0005, PrioSe::Golpe, 0.3);   // 200 golpes en 0,1 s
    CHECK(suenan == MezclaSe::kTope[0] && m.Ocupadas(1.1) == MezclaSe::kTope[0] && m.Descartados(PrioSe::Golpe) == 200 - suenan);
    CHECK(m.Pedir(1.1, PrioSe::Tecnica, 0.9) && !m.Pedir(1.1, PrioSe::Tecnica, 0.9) && m.Pedir(1.1, PrioSe::Muerte, 0.9) && !m.Pedir(1.1, PrioSe::Muerte, 0.9));
    CHECK(m.Ocupadas(1.1) == MezclaSe::kVoces);
    CHECK(m.Ocupadas(1.45) == 2 && m.Pedir(1.45, PrioSe::Golpe, 0.3));   // los golpes acaban y las voces se liberan
    int tope = 0;   // una tormenta de 60 s con peticiones al azar: nunca mas de kVoces a la vez
    sim::Rng az = sim::Flujo(3, 9);
    MezclaSe t;
    for (double ahora = 0; ahora < 60.0; ahora += 0.004) {
      if (az.Hasta(4) == 0) t.Pedir(ahora, PrioSe(az.Hasta(3)), 0.2 + 0.1 * az.Hasta(8));
      tope = std::max(tope, t.Ocupadas(ahora));
    }
    CHECK(tope <= MezclaSe::kVoces && t.Sonados(PrioSe::Muerte) > 0 && t.Descartados(PrioSe::Golpe) >= t.Descartados(PrioSe::Muerte));
  }

  std::printf("\n%d comprobaciones, %d fallos\n", g_comprobaciones, g_fallos);
  return g_fallos ? 1 : 0;
}

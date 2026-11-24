// Dragon Budokai Survivors, paquete P1 (combate y espectaculo): pruebas de juego.cpp sin juego ni graficos. Devuelve 0 si todo pasa.
// Uso (raiz del repo): survivors_p1_test [res/survivors/survivors.toml]
//   a mano: clang++ -O2 -std=c++17 -I. -isystem rexglue/include tests/survivors_p1_test.cpp src/survivors/juego.cpp src/survivors/sim.cpp
//           src/survivors/tienda.cpp
// Cubre: A07 (kit real de jefe y 2 fases), A08 (ritmo), A06 (conductas), A05, A02, A09 (rareza, re-sorteo, saltar, desterrar, cofre por tiers, determinismo con semilla y que sin meta_v2 todo queda igual).
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include "src/survivors/juego.h"

using namespace dbz3::survivors;

namespace {
int fallos = 0;
void Comprueba(bool ok, const char* que) {
  if (!ok) ++fallos, std::printf("  FALLO: %s\n", que);
}

Config V2() {
  Config c;
  c.meta_v2 = true;
  return c;
}

// fuerza una subida de nivel: da la experiencia justa y un paso
void Sube(Juego& j) {
  j.jugador.exp = j.jugador.exp_sig;
  j.Paso({0, 0});
}

// bot de movimiento: huye de los enemigos cercanos, del jefe y de sus balas, y va a por la gema mas cercana (el de survivors_juego_test)
Entrada Mira(const Juego& j) {
  const float px = j.jugador.x, py = j.jugador.y;
  float fx = 0, fy = 0;
  for (int i = 0; i < j.NumEnemigos(); ++i) {
    const VistaEnemigo e = j.Enemigo(i);
    const float vx = px - e.x, vy = py - e.y, d2 = vx * vx + vy * vy;
    if (d2 < 25 && d2 > 1e-6f) fx += vx / d2, fy += vy / d2;
  }
  if (j.jefe.estado != EstadoJefe::Ausente && j.jefe.estado != EstadoJefe::Muerto) {
    const float vx = px - j.jefe.x, vy = py - j.jefe.y, d2 = vx * vx + vy * vy + 1e-3f;
    if (d2 < 100) fx += 4 * vx / d2, fy += 4 * vy / d2;
  }
  for (int k = 0; k < j.nEfectos; ++k) {
    const Efecto2D& e = j.efectos[k];
    if (!e.hostil || e.forma != Forma::Bola) continue;
    const float vx = px - e.x, vy = py - e.y, d2 = vx * vx + vy * vy + 1e-3f;
    if (d2 < 9) fx += vx / d2, fy += vy / d2;
  }
  int g = -1;
  float m = 15 * 15;
  for (int k = 0; k < j.nGemas; ++k) {
    const float d2 = (j.gx[k] - px) * (j.gx[k] - px) + (j.gy[k] - py) * (j.gy[k] - py);
    if (d2 < m) m = d2, g = k;
  }
  if (g >= 0) {
    const float dd = std::sqrt(m) + 1e-3f;
    fx += 0.35f * (j.gx[g] - px) / dd, fy += 0.35f * (j.gy[g] - py) / dd;
  }
  return {fx, fy};
}

// bot de decisiones: usa re-sorteo, saltar y desterrar con una regla fija (depende solo del estado: determinista)
uint64_t JugarDecisiones(const Datos& d, uint64_t semilla, const Config& c, int* usos /*3*/) {
  auto j = std::make_unique<Juego>();
  j->Iniciar(d, int(semilla % 3), semilla, 300, c);
  for (int p = 0; p < 100000 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
    if (j->estado == Estado::Eligiendo && !j->CofreAbierto()) {
      const uint32_t r = uint32_t(j->pasos / 7 + uint64_t(j->jugador.nivel) * 13u);
      if (r % 5 == 0 && j->Resortear()) ++usos[0];
      else if (r % 7 == 0 && j->Saltar()) ++usos[1];
      else if (r % 11 == 0 && j->Desterrar(int(r % uint32_t(j->nOpciones)))) ++usos[2];
      else j->Elegir(int(r % uint32_t(j->nOpciones)));
    } else if (j->estado == Estado::Eligiendo) {
      j->CerrarCofre();
    } else {
      j->Paso({std::sin(float(j->pasos) * 0.01f), std::cos(float(j->pasos) * 0.013f)});
    }
  }
  return j->Hash();
}
}  // namespace

int main(int argc, char** argv) {
  const std::string ruta = argc > 1 ? argv[1] : "res/survivors/survivors.toml";
  Datos d;
  std::string err;
  if (!CargarDatos(ruta, &d, &err)) {
    std::printf("no se pueden cargar los datos:\n%s", err.c_str());
    return 2;
  }

  std::printf("A09) subida v2:\n");
  {   // apagado: nada de decisiones y todo Comun
    auto j = std::make_unique<Juego>();
    j->Iniciar(d, 0, 5, 300);
    Sube(*j);
    Comprueba(j->estado == Estado::Eligiendo && j->nOpciones == kOpciones, "sin meta_v2 la subida da 3 opciones");
    bool comun = true;
    for (int i = 0; i < j->nOpciones; ++i) comun = comun && j->opciones[i].rareza == Rareza::Comun;
    Comprueba(comun, "sin meta_v2 todo es Comun");
    j->zeni = 1000;
    Comprueba(!j->Resortear() && !j->Saltar() && !j->Desterrar(0), "sin meta_v2 no hay re-sorteo, saltar ni desterrar");
    Comprueba(j->DesterrosRestantes() == 0 && j->zeni == 1000, "sin meta_v2 no cambia nada");
  }
  {   // rareza: aparecen todas, el nivel regalado respeta el tope y la suerte las mueve
    int cuenta[kRarezas] = {};
    int total = 0;
    bool tope = true, evoLeg = true;
    for (uint64_t s = 1; s <= 400; ++s) {
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, 0, s, 300, V2());
      Sube(*j);
      for (int i = 0; i < j->nOpciones; ++i) {
        const Opcion& o = j->opciones[i];
        ++cuenta[int(o.rareza)], ++total;
        tope = tope && o.nivel >= 1 && o.nivel <= kNivelMax;
        if (o.tipo == TipoOpcion::Evolucion) evoLeg = evoLeg && o.rareza == Rareza::Legendaria;
      }
    }
    std::printf("  rareza en %d opciones: comun %d poco %d rara %d epica %d legendaria %d\n", total, cuenta[0], cuenta[1], cuenta[2], cuenta[3],
                cuenta[4]);
    Comprueba(cuenta[0] > cuenta[1] && cuenta[1] > cuenta[2] && cuenta[2] > cuenta[3] && cuenta[3] > cuenta[4] && cuenta[4] > 0,
              "las 5 rarezas salen y de mas a menos comunes");
    Comprueba(tope, "el nivel con rareza queda entre 1 y 5");
    Comprueba(evoLeg, "las evoluciones son Legendaria");
    Config c = V2();
    c.mejoras[0] = 0;
    int raras_suerte = 0, raras_normal = cuenta[2] + cuenta[3] + cuenta[4];
    for (int k = 0; k < int(d.mejoras.size()); ++k) if (d.mejoras[size_t(k)].efecto == Efecto::Suerte) c.mejoras[k] = kNivelMax;
    for (uint64_t s = 1; s <= 400; ++s) {
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, 0, s, 300, c);
      Sube(*j);
      for (int i = 0; i < j->nOpciones; ++i) raras_suerte += j->opciones[i].rareza >= Rareza::Rara;
    }
    bool hay_suerte = false;
    for (const auto& m : d.mejoras) hay_suerte = hay_suerte || m.efecto == Efecto::Suerte;
    std::printf("  rara o mejor: sin suerte %d, con suerte al maximo %d\n", raras_normal, raras_suerte);
    if (hay_suerte) Comprueba(raras_suerte > raras_normal, "la suerte sube la rareza");
  }
  {   // re-sorteo: coste creciente, solo con zeni, no toca el nivel ni consume la subida
    auto j = std::make_unique<Juego>();
    j->Iniciar(d, 0, 7, 300, V2());
    Sube(*j);
    const int c0 = j->CosteResortear();
    Comprueba(c0 == d.subida.reroll_base && !j->PuedeResortear() && !j->Resortear(), "sin zeni no se puede re-sortear");
    j->zeni = 1000;
    Opcion antes[kOpciones];
    std::copy(j->opciones, j->opciones + kOpciones, antes);
    Comprueba(j->Resortear() && j->zeni == 1000 - c0, "re-sortear cobra el coste");
    Comprueba(j->CosteResortear() == c0 + d.subida.reroll_paso, "el coste sube con cada uso");
    Comprueba(j->estado == Estado::Eligiendo && j->nOpciones == kOpciones && j->jugador.nivel == 2, "tras re-sortear sigue la misma subida");
    bool distinto = false;
    for (int k = 0; k < 10 && !distinto; ++k) {
      j->zeni = 100000;
      j->Resortear();
      for (int i = 0; i < kOpciones; ++i) distinto = distinto || j->opciones[i].def != antes[i].def || j->opciones[i].tipo != antes[i].tipo;
    }
    Comprueba(distinto, "re-sortear cambia las opciones");
    Comprueba(j->resorteos >= 2 && j->CosteResortear() == c0 + d.subida.reroll_paso * j->resorteos, "cuenta los re-sorteos");
  }
  {   // saltar: da zeni y vida, y la subida pendiente pasa
    auto j = std::make_unique<Juego>();
    j->Iniciar(d, 0, 8, 300, V2());
    j->jugador.vida = j->jugador.vida_max * 0.5f;
    Sube(*j);
    const float vida0 = j->jugador.vida;
    const int nt = j->nTecnicas + j->nPasivas;
    Comprueba(j->Saltar(), "saltar funciona");
    Comprueba(j->estado == Estado::Jugando && j->zeni == d.subida.saltar_zeni, "saltar da zeni y sigue la partida");
    Comprueba(j->jugador.vida > vida0 && j->nTecnicas + j->nPasivas == nt, "saltar cura y no da mejora");
    Comprueba(!j->Saltar(), "no se puede saltar sin subida en curso");
  }
  {   // desterrar: la opcion no vuelve a salir; hay un tope
    auto j = std::make_unique<Juego>();
    j->Iniciar(d, 0, 9, 300, V2());
    Sube(*j);
    const int dest = d.subida.desterros;
    Opcion borrada;
    int hechos = 0;
    for (int k = 0; k < dest + 2; ++k) {
      if (j->nOpciones == 0) break;
      const Opcion o = j->opciones[0];
      if (o.tipo != TipoOpcion::Tecnica && o.tipo != TipoOpcion::Pasiva) break;
      if (j->Desterrar(0)) {
        ++hechos;
        if (k == 0) borrada = o;
        Comprueba(j->EsDesterrada(o), "la opcion queda desterrada");
      }
    }
    Comprueba(hechos == dest && j->DesterrosRestantes() == 0, "solo se puede desterrar el tope de veces");
    bool vuelve = false;
    for (int n = 0; n < 60 && !vuelve; ++n) {
      for (int i = 0; i < j->nOpciones; ++i) vuelve = vuelve || (j->opciones[i].def == borrada.def && j->opciones[i].tipo == borrada.tipo);
      j->Elegir(0);
      Sube(*j);
      if (j->estado != Estado::Eligiendo) break;
    }
    Comprueba(!vuelve && dest > 0, "una opcion desterrada no vuelve a salir");
  }
  {   // cofre por tiers: 1/3/5 premios aplicados; estado Eligiendo hasta cerrarlo
    int cuenta[3] = {};
    for (const int tier : {1, 3, 5}) {
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, 0, 11 + uint64_t(tier), 300, V2());
      const int antes = j->nTecnicas + j->nPasivas;
      const int nivel = j->jugador.nivel;
      j->objetos[j->nObjetos++] = {j->jugador.x, j->jugador.y, TipoObjeto::Cofre, uint8_t(tier)};
      j->Paso({0, 0});
      bool ev = false;
      for (int e = 0; e < j->nEventos; ++e) ev = ev || (j->eventos[e].tipo == TipoEvento::CofreAbre && j->eventos[e].a == tier);
      std::printf("  cofre tier %d: premios %d estado %d\n", tier, j->nPremios, int(j->estado));
      Comprueba(ev && j->CofreAbierto() && j->estado == Estado::Eligiendo && j->nPremios == tier, "el cofre da tier premios y avisa");
      Comprueba(j->nOpciones >= 1 && j->nOpciones <= kOpciones, "opciones[] sigue valido para pantallas antiguas");
      Comprueba(j->jugador.nivel == nivel && j->nTecnicas + j->nPasivas >= antes, "el cofre no sube de nivel");
      Comprueba(!j->Resortear() && !j->Saltar() && !j->Desterrar(0), "dentro del cofre no hay decisiones");
      j->Elegir(0);
      Comprueba(j->estado == Estado::Jugando && !j->CofreAbierto() && j->nPremios == 0, "Elegir cierra el cofre");
      ++cuenta[tier / 2];
    }
    {   // el cofre del jefe: tier 5 (probado en el juego completo mas abajo); sorteo de tier al caer
      int t[6] = {};
      for (uint64_t s = 1; s <= 300; ++s) {
        auto j = std::make_unique<Juego>();
        j->Iniciar(d, 0, s, 300, V2());
        j->objetos[j->nObjetos++] = {j->jugador.x, j->jugador.y, TipoObjeto::Cofre, 0};   // tier 0 = lo decide Recoger (1)
        j->Paso({0, 0});
        ++t[j->nPremios];
      }
      Comprueba(t[1] == 300, "un cofre sin tier da 1 premio");
    }
    {   // cofre con evolucion lista: la evolucion va primero
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, 0, 4, 300, V2());
      int base = -1, evo = -1;
      for (int e = 0; e < int(d.tecnicas.size()) && evo < 0; ++e)
        if (d.tecnicas[size_t(e)].evoluciona_de >= 0) evo = e, base = d.tecnicas[size_t(e)].evoluciona_de;
      j->Dar(TipoOpcion::Tecnica, base, kNivelMax);
      j->Dar(TipoOpcion::Pasiva, d.tecnicas[size_t(evo)].pasiva, 1);
      const int n = j->OpcionesCofre(3);
      Comprueba(n == 3 && j->premios[0].tipo == TipoOpcion::Evolucion && j->premios[0].def == evo, "el cofre evoluciona primero");
      Comprueba(j->NivelTecnica(evo) == kNivelMax && j->NivelTecnica(base) == 0, "la evolucion queda aplicada");
    }
  }
  {   // determinismo con decisiones: misma semilla, mismo hash; y con meta_v2 apagado el bot de decisiones no cambia nada
    int u1[3] = {}, u2[3] = {}, u3[3] = {};
    const uint64_t a = JugarDecisiones(d, 21, V2(), u1), b = JugarDecisiones(d, 21, V2(), u2), c = JugarDecisiones(d, 22, V2(), u3);
    std::printf("  decisiones: hash %016llx %016llx (otra semilla %016llx); re-sorteos %d saltos %d destierros %d\n", (unsigned long long)a,
                (unsigned long long)b, (unsigned long long)c, u1[0], u1[1], u1[2]);
    Comprueba(a == b && u1[0] == u2[0] && u1[1] == u2[1] && u1[2] == u2[2], "determinismo con semilla usando re-sorteo, saltar y desterrar");
    Comprueba(a != c, "otra semilla, otra partida");
    Comprueba(u1[0] + u1[1] + u1[2] > 0, "el bot uso las decisiones");
    int u4[3] = {};
    JugarDecisiones(d, 21, Config{}, u4);
    Comprueba(u4[0] + u4[1] + u4[2] == 0, "con meta_v2 apagado el bot no puede decidir");
  }
  {   // partida completa con meta_v2: llega al jefe y el cofre del jefe es de 5 premios
    auto j = std::make_unique<Juego>();
    Config c = V2();
    j->Iniciar(d, 0, 31, 120, c);
    int premios_jefe = 0;
    for (int p = 0; p < 200000 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
      if (j->estado == Estado::Eligiendo) { j->Elegir(0); continue; }
      j->jugador.vida = j->jugador.vida_max;   // inmortal: solo interesa el jefe
      if (j->jefe.estado == EstadoJefe::Persigue || j->jefe.estado == EstadoJefe::Carga) j->jefe.vida = std::min(j->jefe.vida, 1.0f);
      Entrada m{0, 0};   // el jefe cae donde le persigue el jugador (quieto); luego va a por su cofre
      for (int k = 0; k < j->nObjetos; ++k)
        if (j->objetos[k].tipo == TipoObjeto::Cofre) m = {j->objetos[k].x - j->jugador.x, j->objetos[k].y - j->jugador.y};
      j->Paso(m);
      for (int e = 0; e < j->nEventos; ++e)
        if (j->eventos[e].tipo == TipoEvento::CofreAbre) premios_jefe = std::max(premios_jefe, j->eventos[e].a);
    }
    std::printf("  partida con meta_v2: estado %d, mayor cofre %d premios\n", int(j->estado), premios_jefe);
    Comprueba(j->estado == Estado::Victoria && premios_jefe == 5, "el cofre del jefe da 5 premios");
  }

  std::printf("A02) impacto (jugo):\n");
  {
    struct R { uint64_t hash; int pausas, temblores, ev_pausa, ev_tem; float total, tmax_quieto; Estado fin; };
    auto jugar = [&](uint64_t semilla, bool jugo) {
      Config c;
      c.jugo = jugo;
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, int(semilla % 3), semilla, 300, c);
      R r{};
      float quieto = 0;
      for (int p = 0; p < 100000 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
        const float t0 = j->tiempo;
        const bool elige = j->estado == Estado::Eligiendo;   // elegir una evolucion tambien da una pausa (su evento sale de Elegir)
        if (elige) j->Elegir(0);
        else j->Paso({std::sin(float(j->pasos) * 0.01f), std::cos(float(j->pasos) * 0.013f)});
        if (!elige) quieto = j->tiempo == t0 ? quieto + 1.0f / 60.0f : 0.0f;
        r.tmax_quieto = std::max(r.tmax_quieto, quieto);
        for (int e = 0; e < j->nEventos; ++e) r.ev_pausa += j->eventos[e].tipo == TipoEvento::Pausa, r.ev_tem += j->eventos[e].tipo == TipoEvento::Temblor;
        j->jugador.vida = j->jugador.vida_max;   // inmortal: que llegue al jefe
      }
      r.hash = j->Hash(), r.pausas = j->pausas, r.temblores = j->temblores, r.total = j->pausa_total, r.fin = j->estado;
      return r;
    };
    const R a = jugar(41, true), b = jugar(41, true), off = jugar(41, false);
    std::printf("  con jugo: pausas %d (%.2f s en total, la mas larga %.2f s) temblores %d, final %d; sin jugo: pausas %d temblores %d\n", a.pausas, a.total, a.tmax_quieto,
                a.temblores, int(a.fin), off.pausas, off.temblores);
    Comprueba(a.hash == b.hash && a.pausas == b.pausas, "determinismo con jugo: misma semilla, misma partida (la pausa no lo rompe)");
    Comprueba(a.pausas > 0 && a.ev_pausa == a.pausas, "hay pausas de golpe y cada una avisa con su evento");
    Comprueba(a.tmax_quieto <= 0.2f, "ninguna pausa pasa de 0,2 s (6 cuadros + margen)");
    Comprueba(a.total < 0.06f * 300.0f, "las pausas suman menos del 6 % de la partida (topes)");
    Comprueba(a.temblores > 0 && a.ev_tem == a.temblores && a.temblores < 60, "hay temblores (jefe, bomba, elite) y con tope");
    Comprueba(off.pausas == 0 && off.temblores == 0 && off.ev_pausa == 0, "sin jugo no hay pausas ni temblores");
  }

  std::printf("A05) formas:\n");
  {
    Config c;
    c.formas = true;
    auto j = std::make_unique<Juego>();
    j->Iniciar(d, 0, 61, 600, c);
    Comprueba(j->forma == 0 && j->ki == 0, "empieza en la forma base con la barra vacia");
    const float vel0 = j->jugador.velocidad;
    j->DarKi(0.5f);
    Comprueba(j->forma == 0 && std::fabs(j->ki - 0.5f) < 1e-4f, "DarKi llena la barra");
    j->DarKi(0.6f);
    bool ev = false;
    for (int e = 0; e < j->nEventos; ++e) ev = ev || (j->eventos[e].tipo == TipoEvento::Forma && j->eventos[e].a == 1);
    Comprueba(j->forma == 1 && ev, "barra llena: se transforma (forma 1) y avisa con el evento");
    Comprueba(j->jugador.velocidad > vel0 * 1.05f, "transformado va mas rapido");
    const int pasos = int(d.formas.dura * 60.0f) + 2;
    bool vuelve = false;
    for (int p = 0; p < pasos && j->estado != Estado::Derrota;) {
      if (j->estado == Estado::Eligiendo) { j->Elegir(0); continue; }
      j->jugador.vida = j->jugador.vida_max;
      j->Paso({0, 0}), ++p;
      for (int e = 0; e < j->nEventos; ++e) vuelve = vuelve || (j->eventos[e].tipo == TipoEvento::Forma && j->eventos[e].a == 0);
    }
    std::printf("  forma tras %.0f s: %d, ki %.2f, velocidad %.2f (base %.2f)\n", d.formas.dura, j->forma, j->ki, j->jugador.velocidad, vel0);
    Comprueba(j->forma == 0 && vuelve && std::fabs(j->jugador.velocidad - vel0) < 1e-3f, "al acabar la duracion vuelve a la base y quita los bonos");
    // evolucion: transforma al instante
    auto k = std::make_unique<Juego>();
    k->Iniciar(d, 0, 62, 600, c);
    int base = -1, evo = -1;
    for (int e = 0; e < int(d.tecnicas.size()) && evo < 0; ++e)
      if (d.tecnicas[size_t(e)].evoluciona_de >= 0) evo = e, base = d.tecnicas[size_t(e)].evoluciona_de;
    k->Dar(TipoOpcion::Tecnica, base, kNivelMax);
    k->Dar(TipoOpcion::Pasiva, d.tecnicas[size_t(evo)].pasiva, 1);
    k->Dar(TipoOpcion::Evolucion, evo, kNivelMax);
    Comprueba(k->forma == 1, "evolucionar una tecnica transforma al luchador");
    k->Dar(TipoOpcion::Evolucion, evo, kNivelMax);
    Comprueba(k->forma == std::min(2, d.formas.max), "una segunda evolucion transformado sube de forma");
    // apagado: DarKi y la barra no hacen nada
    auto o = std::make_unique<Juego>();
    o->Iniciar(d, 0, 63, 600);
    o->DarKi(1);
    for (int p = 0; p < 600; ++p) { if (o->estado == Estado::Eligiendo) o->Elegir(0); else o->Paso({0, 0}); }
    Comprueba(o->forma == 0 && o->ki == 0, "sin Config::formas no hay barra ni formas");
    // llenado solo con derrotados y tiempo (partida completa del bot): se transforma alguna vez
    auto g = std::make_unique<Juego>();
    g->Iniciar(d, 1, 64, 300, c);
    int cambios = 0;
    for (int p = 0; p < 100000 && (g->estado == Estado::Jugando || g->estado == Estado::Eligiendo); ++p) {
      if (g->estado == Estado::Eligiendo) { g->Elegir(0); continue; }
      g->jugador.vida = g->jugador.vida_max;
      g->Paso({std::sin(float(g->pasos) * 0.01f), std::cos(float(g->pasos) * 0.013f)});
      for (int e = 0; e < g->nEventos; ++e) cambios += g->eventos[e].tipo == TipoEvento::Forma;
    }
    std::printf("  partida de 300 s: %d cambios de forma\n", cambios);
    Comprueba(cambios >= 2, "en una partida la barra se llena y el luchador se transforma varias veces");
  }

  std::printf("A08) ritmo (jefes_v2):\n");
  {
    Config c;
    c.jefes_v2 = true;
    Datos dd = d;
    for (DefJefe& jf : dd.jefes) jf.vida = 1e9f;   // inmortal: para ver la furia y que la partida siga pasados los 15 minutos
    auto j = std::make_unique<Juego>();
    j->Iniciar(dd, 0, 71, 0, c);
    float t_jefe = -1, t_furia = -1, t_fin = -1;
    float avisos[4] = {-1, -1, -1, -1}, hechos[4] = {-1, -1, -1, -1};
    int n_aviso = 0, n_hecho = 0, mini_cofres = 0;
    bool aviso_5s = true;
    float ultimo_aviso_t[4] = {-1, -1, -1, -1};
    for (int p = 0; p < 60 * 1300 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
      if (j->estado == Estado::Eligiendo) { j->Elegir(0); continue; }
      j->jugador.vida = j->jugador.vida_max;
      j->Paso({std::sin(float(j->pasos) * 0.01f), std::cos(float(j->pasos) * 0.013f)});
      for (int e = 0; e < j->nEventos; ++e) {
        const Evento& ev = j->eventos[e];
        if (ev.tipo == TipoEvento::JefeAparece && t_jefe < 0) t_jefe = j->tiempo;
        if (ev.tipo == TipoEvento::JefeFuria && t_furia < 0) t_furia = j->tiempo;
        if (ev.tipo == TipoEvento::AvisoEvento) ++n_aviso, ultimo_aviso_t[ev.a] = j->tiempo, avisos[ev.a] = j->tiempo;
        if (ev.tipo == TipoEvento::EventoOleada) {
          ++n_hecho, hechos[ev.a] = j->tiempo;
          if (ultimo_aviso_t[ev.a] < 0 || std::fabs(j->tiempo - ultimo_aviso_t[ev.a] - dd.ritmo.aviso) > 0.1f) aviso_5s = false;
        }
        if (ev.tipo == TipoEvento::Muerte && j->nEventos > 0) (void)0;
      }
      if (j->tiempo > 1000) break;
    }
    t_fin = j->tiempo;
    (void)mini_cofres;
    std::printf("  jefe a los %.0f s (12:00 = 720), furia a los %.0f s (15:00 = 900), fin a los %.0f s estado %d; avisos %d eventos %d minijefes %d\n", t_jefe, t_furia, t_fin,
                int(j->estado), n_aviso, n_hecho, j->minijefes_vistos);
    Comprueba(t_jefe >= 719 && t_jefe <= 722, "el jefe sale a las 12:00");
    Comprueba(t_furia >= 899 && t_furia <= 902, "si el jefe sigue vivo a las 15:00 entra en furia");
    Comprueba(t_fin > 905 && j->furia, "con el jefe vivo la partida sigue pasados los 15 minutos");
    Comprueba(j->minijefes_vistos == 2, "hay 2 minijefes (min 5 y 10)");
    Comprueba(n_aviso == n_hecho && n_hecho >= 7 && aviso_5s, "cada evento avisa 5 s antes (enjambre, cerco, meteoros y los 2 minijefes)");
    Comprueba(j->eventos_vistos >= 5, "los eventos ocurren (5 de serie)");
    (void)avisos, (void)hechos;
  }
  {   // sin jefes_v2: el jefe sigue a los 14 min y la partida acaba a los 15
    auto j = std::make_unique<Juego>();
    j->Iniciar(d, 0, 71, 0);
    float t_jefe = -1;
    for (int p = 0; p < 60 * 1000 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
      if (j->estado == Estado::Eligiendo) { j->Elegir(0); continue; }
      j->jugador.vida = j->jugador.vida_max;
      j->Paso({std::sin(float(j->pasos) * 0.01f), std::cos(float(j->pasos) * 0.013f)});
      for (int e = 0; e < j->nEventos; ++e) if (j->eventos[e].tipo == TipoEvento::JefeAparece) t_jefe = j->tiempo;
    }
    Comprueba(t_jefe >= 839 && t_jefe <= 842 && !j->furia && j->eventos_vistos == 0, "apagado: el jefe sale a los 14 min y no hay eventos ni furia");
  }
  {   // partidas con el bot: cuantas ven al jefe (a las 12:00 con jefes_v2) y cuanto dura la pelea (sin forzar su vida)
    int vistos = 0, caidos = 0, partidas = 12;
    float pelea = 0;
    for (int g = 0; g < partidas; ++g) {
      Config c;
      c.jefes_v2 = true;
      c.escenario = g % int(d.escenarios.size());
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, g % 3, 100 + uint64_t(g), 0, c);
      float t0 = -1, t1 = -1;
      for (int p = 0; p < 60 * 1800 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
        if (j->estado == Estado::Eligiendo) {   // SUR-05: el bot elige la opcion de mayor rareza (un jugador no coge siempre la primera)
          int mejor = 0;
          for (int i = 1; i < j->nOpciones; ++i) if (j->opciones[i].rareza > j->opciones[mejor].rareza) mejor = i;
          j->Elegir(mejor);
          continue;
        }
        j->Paso(Mira(*j));
        if (j->estado == Estado::Derrota) std::printf("    partida %d muere a los %.0f s (nivel %d)\n", g, j->tiempo, j->jugador.nivel);
        for (int e = 0; e < j->nEventos; ++e) {
          if (j->eventos[e].tipo == TipoEvento::JefeAparece) t0 = j->tiempo;
          if (j->eventos[e].tipo == TipoEvento::JefeMuere) t1 = j->tiempo;
        }
        if (t0 >= 0 && t1 >= 0) break;
      }
      if (t0 >= 0) ++vistos;
      if (t0 >= 0 && t1 >= 0) pelea += t1 - t0, ++caidos;
    }
    std::printf("  %d de %d partidas llegan al jefe y %d lo derrotan; pelea media (las que lo derrotan) %.0f s\n", vistos, partidas, caidos, pelea / float(std::max(1, caidos)));
    Comprueba(vistos >= 1, "alguna partida del bot llega al jefe de las 12:00");
  }

  std::printf("A07) kit de jefe y 2 fases (jefes_v2):\n");
  {
    // por jefe: ataques que salen en cada fase (por ID de AtaqueJefe), el cambio de fase (una vez, sin dano mientras dura) y que el jugador recibe dano
    struct Esp { const char* id; AtaqueJefe f1[3], f2[4]; };
    using A = AtaqueJefe;   // SUR-04: Cell Jr, Taiyoken, rocas y absorcion se suman al kit de B3
    const Esp esp[] = {{"cell", {A::Kamehameha, A::InvocaCellJr, A::Ninguno}, {A::Taiyoken, A::Kamehameha, A::CampoEnergia, A::BombaEspiritual}},
                       {"freezer", {A::RayoMortal, A::Rocas, A::Ninguno}, {A::Rocas, A::RayoMortal, A::OndaMortal, A::BolaMuerte}},
                       {"superbuu", {A::IllFlash, A::Ninguno, A::Ninguno}, {A::IllFlash, A::IllBola, A::Absorcion, A::Ninguno}},
                       {"cooler", {A::RayoDestructor, A::DiscoSauzer, A::Ninguno}, {A::RayoDestructor, A::DiscoSauzer, A::Supernova, A::Ninguno}}};
    for (const Esp& x : esp) {
      int esc = -1;
      for (int k = 0; k < int(d.escenarios.size()); ++k) if (d.jefes[size_t(d.escenarios[size_t(k)].jefe)].id == x.id) esc = k;
      if (esc < 0) { std::printf("  (sin escenario para %s)\n", x.id); continue; }
      Datos dd = d;
      for (DefJefe& jf : dd.jefes) jf.vida = 1e9f;   // inmortal: la fase 2 la forzamos con la vida
      auto correr = [&](uint64_t& hash, bool v2, int* fases, int* cambios, bool* sin_dano_transforma, int* dano_jugador, int* maxEfectos, bool vistos[17][2]) {
        Config c;
        c.jefes_v2 = v2, c.escenario = esc;
        auto j = std::make_unique<Juego>();
        j->Iniciar(dd, 0, 61, 60, c);
        float t_fase2 = -1;
        *fases = 1, *cambios = 0, *sin_dano_transforma = true, *dano_jugador = 0, *maxEfectos = 0;
        float vida_t = -1;
        for (int p = 0; p < 60 * 260 && j->estado != Estado::Derrota; ++p) {
          if (j->estado == Estado::Eligiendo) { j->Elegir(0); continue; }
          if (j->estado != Estado::Jugando) break;
          j->jugador.vida = j->jugador.vida_max;
          if (v2 && t_fase2 < 0 && j->jefe.estado == EstadoJefe::Persigue && j->tiempo - 48.0f > 100) j->jefe.vida = j->jefe.vida_max * 0.49f, t_fase2 = j->tiempo;   // a los 100 s de pelea
          const float vida_ant = j->jefe.vida;
          // el luchador se queda junto al jefe durante el cambio de fase (sus tecnicas le darian si pudiera recibir dano)
          if (j->jefe.estado == EstadoJefe::Transforma) j->jugador.x = j->jefe.x + 0.5f, j->jugador.y = j->jefe.y;
          j->Paso({std::sin(float(j->pasos) * 0.01f), std::cos(float(j->pasos) * 0.013f)});
          if (j->jefe.estado == EstadoJefe::Transforma && vida_t >= 0 && j->jefe.vida < vida_t - 1.0f) *sin_dano_transforma = false;
          vida_t = j->jefe.estado == EstadoJefe::Transforma ? j->jefe.vida : -1;
          (void)vida_ant;
          for (int e = 0; e < j->nEventos; ++e) {
            const Evento& ev = j->eventos[e];
            if (ev.tipo == TipoEvento::JefeAtaque && ev.a >= 0 && ev.a < 17) vistos[ev.a][j->jefe.fase >= 2 ? 1 : 0] = true;
            if (ev.tipo == TipoEvento::JefeFase) ++*cambios, *fases = ev.a;
            if (ev.tipo == TipoEvento::DanoJugador && j->jefe.estado != EstadoJefe::Ausente) ++*dano_jugador;
          }
          *maxEfectos = std::max(*maxEfectos, j->nEfectos);
        }
        hash = j->Hash();
        if (v2) Comprueba(j->jefe.fase == 2 && t_fase2 > 0, "el jefe llega a la fase 2");
      };
      bool vistos[17][2] = {}, vistos_off[17][2] = {};
      uint64_t h1 = 0, h2 = 0, h_off = 0;
      int f = 0, c1 = 0, c2 = 0, dj = 0, dj2 = 0, me = 0, me2 = 0, f_off = 0, c_off = 0, dj_off = 0, me_off = 0;
      bool sdt = true, sdt2 = true, sdt_off = true;
      bool v2b[17][2] = {};
      correr(h1, true, &f, &c1, &sdt, &dj, &me, vistos);
      correr(h2, true, &f, &c2, &sdt2, &dj2, &me2, v2b);
      correr(h_off, false, &f_off, &c_off, &sdt_off, &dj_off, &me_off, vistos_off);
      std::printf("  %-8s fases %d cambios %d dano al jugador %d, efectos como mucho %d; fase 1:", x.id, f, c1, dj, me);
      for (int a = 1; a < 17; ++a) if (vistos[a][0]) std::printf(" %s", NombreAtaque(AtaqueJefe(a)));
      std::printf(" | fase 2:");
      for (int a = 1; a < 17; ++a) if (vistos[a][1]) std::printf(" %s", NombreAtaque(AtaqueJefe(a)));
      std::printf("\n");
      bool ok1 = true, ok2 = true, ult1 = false;
      for (const AtaqueJefe a : x.f1) if (a != AtaqueJefe::Ninguno) ok1 = ok1 && vistos[int(a)][0];
      for (const AtaqueJefe a : x.f2) if (a != AtaqueJefe::Ninguno) ok2 = ok2 && vistos[int(a)][1];
      ult1 = vistos[int(AtaqueJefe::BombaEspiritual)][0] || vistos[int(AtaqueJefe::BolaMuerte)][0] || vistos[int(AtaqueJefe::Supernova)][0] || vistos[int(AtaqueJefe::CampoEnergia)][0] ||
             vistos[int(AtaqueJefe::IllBola)][0] ||
             vistos[int(AtaqueJefe::Taiyoken)][0] || vistos[int(AtaqueJefe::Absorcion)][0];
      Comprueba(ok1, "fase 1: salen los ataques de su kit real");
      Comprueba(ok2, "fase 2: salen tambien la definitiva y los de la forma final");
      Comprueba(!ult1, "las definitivas (y el Energy Field / Ill Ball) no salen en la fase 1");
      Comprueba(c1 == 1 && f == 2, "el cambio de fase ocurre una vez (JefeFase a=2)");
      Comprueba(sdt, "no recibe dano mientras cambia de fase");
      Comprueba(dj > 0, "los ataques del kit dan al jugador");
      Comprueba(me < kMaxEfectos - 8, "los efectos del jefe no desbordan la lista");
      Comprueba(h1 == h2 && c1 == c2, "determinista: misma semilla, mismo resultado");
      Comprueba(c_off == 0 && dj_off >= 0, "con jefes_v2 apagado no hay cambio de fase ni ataques de kit");
      bool kit_off = false;
      for (int a = 3; a < 17; ++a) kit_off = kit_off || vistos_off[a][0] || vistos_off[a][1];
      Comprueba(!kit_off, "con jefes_v2 apagado no sale ningun ataque del kit");
      (void)h_off, (void)dj2, (void)me2, (void)me_off;
    }
  }

  std::printf("A06) conductas (ia):\n");
  {
    Config c;
    c.ia = true;
    int tiros = 0, explos = 0, escudos = 0, minions_extra = 0, dano_enemigo = 0;
    auto j = std::make_unique<Juego>();
    j->Iniciar(d, 0, 81, 300, c);
    bool hay_cond[5] = {};
    for (const DefEnemigo& e : d.enemigos) hay_cond[int(e.conducta)] = true;
    Comprueba(hay_cond[1] && hay_cond[2] && hay_cond[3] && hay_cond[4], "el toml (o los de serie por id) define tirador, embestida, explota y elite");
    int ant_bolas = 0;
    for (int p = 0; p < 60 * 280 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
      if (j->estado == Estado::Eligiendo) { j->Elegir(0); continue; }
      j->jugador.vida = j->jugador.vida_max;
      j->Paso({std::sin(float(j->pasos) * 0.01f), std::cos(float(j->pasos) * 0.013f)});
      int bolas = 0;
      for (int k = 0; k < j->nEfectos; ++k) {
        const Efecto2D& e = j->efectos[k];
        if (e.hostil && e.forma == Forma::Bola && e.radio < 0.31f) ++bolas;
        if (e.hostil && e.forma == Forma::Explosion && e.vida > e.vida0 - 0.02f) ++explos;
      }
      if (bolas > ant_bolas) tiros += bolas - ant_bolas;
      ant_bolas = bolas;
      for (int i = 0; i < j->NumEnemigos(); ++i) {
        const VistaEnemigo v = j->Enemigo(i);
        if (v.escudo > 0) ++escudos;
      }
      for (int e = 0; e < j->nEventos; ++e) dano_enemigo += j->eventos[e].tipo == TipoEvento::DanoJugador;
    }
    (void)minions_extra;
    std::printf("  300 s con ia: disparos %d, explosiones (cuadros) %d, cuadros con elite escudado %d, golpes al jugador %d\n", tiros, explos, escudos, dano_enemigo);
    Comprueba(tiros > 0, "los tiradores disparan bolas de ki");
    Comprueba(explos > 0, "los Saibaman explotan");
    Comprueba(escudos > 0, "los elites tienen escudo");
  }
  {   // rendimiento: ms por paso con la horda llena, con y sin conductas (no debe subir mas de un 20 %)
    auto medir = [&](bool ia) {
      Config c;
      c.ia = ia;
      auto j = std::make_unique<Juego>();
      Datos dd = d;
      dd.tope_pantalla = 600;
      j->Iniciar(dd, 0, 91, 120, c);   // partida comprimida: la horda se llena enseguida
      double total = 0;
      int pasos = 0;
      for (int p = 0; p < 60 * 105; ++p) {
        if (j->estado == Estado::Eligiendo) { j->Elegir(0); continue; }
        if (j->estado != Estado::Jugando) break;
        j->jugador.vida = j->jugador.vida_max;
        const auto t0 = std::chrono::steady_clock::now();
        j->Paso({std::sin(float(j->pasos) * 0.01f), std::cos(float(j->pasos) * 0.013f)});
        if (j->NumEnemigos() > 150) total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), ++pasos;
      }
      return pasos > 0 ? total / pasos : 0.0;
    };
    medir(false), medir(true);   // calentamiento
    const double a = medir(false), b2 = medir(true);
    std::printf("  ms por paso con >150 enemigos: sin ia %.3f, con ia %.3f (x%.2f)\n", a, b2, a > 0 ? b2 / a : 0.0);
    Comprueba(a > 0 && b2 <= a * 1.2 + 0.02, "las conductas no suben el coste por paso mas de un 20 %");
  }

  std::printf(fallos ? "FALLOS: %d\n" : "TODO OK\n", fallos);
  return fallos ? 1 : 0;
}

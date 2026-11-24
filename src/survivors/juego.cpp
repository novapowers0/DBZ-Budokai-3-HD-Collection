// Dragon Budokai Survivors: partida completa (ver juego.h). La horda (persecucion, separacion y rejilla) la mueve Sim; aqui van los tipos,
// la vida, las tecnicas, el jefe y la progresion. Sin memoria dinamica por paso: todo son arrays fijos.
#include "juego.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <toml++/toml.hpp>

namespace dbz3::survivors {
namespace {
constexpr float kDt = Sim::kDt;
constexpr float kPi = 3.14159265f;
constexpr float kApertura = 0.6f;   // media apertura del cono (rad, ~35 grados)

struct Nombre { const char* texto; int valor; };
constexpr Nombre kArquetipos[] = {{"rayo", 0}, {"orbita", 1}, {"teledirigida", 2}, {"estallido", 3}, {"aura", 4}, {"rayo_fino", 5},
                                  {"cono", 6}, {"explosion", 7}, {"bumeran", 8}, {"rafaga", 9}, {"barrera", 10}, {"onda", 11}};
constexpr Nombre kEfectos[] = {{"vida_max", 0}, {"velocidad", 1}, {"recogida", 2}, {"recarga", 3}, {"dano", 4}, {"area", 5},
                               {"exp", 6},      {"regen", 7},     {"suerte", 8},   {"zeni", 9},    {"reanimar", 10},
                               {"armadura", 11}, {"duracion", 12}, {"cantidad", 13}, {"maldicion", 14}, {"crecimiento", 15},
                               {"resorteo", 16}, {"salto", 17}, {"desterro", 18}};
constexpr Nombre kAnillos[] = {{"", 0}, {"oro", 1}, {"rojo", 2}, {"azul", 3}};
template <size_t N>
int Buscar(const Nombre (&t)[N], const std::string& s) {
  for (const Nombre& n : t) if (s == n.texto) return n.valor;
  return -1;
}

struct Lector {
  std::string pre;   // "tecnica 'kamehameha'" ...
  std::string* err;
  float F(const toml::table& t, const char* k, float def) {
    if (!t.contains(k)) return def;
    if (const auto v = t[k].value<double>(); v && std::isfinite(*v) && std::abs(*v) <= std::numeric_limits<float>::max()) return float(*v);
    *err += pre + ": '" + k + "' no es un numero finito representable (uso " + std::to_string(def) + ")\n";
    return def;
  }
  int I(const toml::table& t, const char* k, int def) {
    if (!t.contains(k)) return def;
    const auto v = t[k].value<double>();
    if (v && std::isfinite(*v)) {
      const double n = std::round(*v);
      if (n >= std::numeric_limits<int>::min() && n <= std::numeric_limits<int>::max()) return int(n);
    }
    *err += pre + ": '" + k + "' fuera de rango entero (uso " + std::to_string(def) + ")\n";
    return def;
  }
  static float Finito(double v, float def) {
    return std::isfinite(v) && std::abs(v) <= std::numeric_limits<float>::max() ? float(v) : def;
  }
  std::string S(const toml::table& t, const char* k, const char* def = "") { return t[k].value_or<std::string>(def); }
  void Niveles(const toml::table& t, const char* k, float (&out)[kNivelMax], float def) {   // 5 valores o uno para todos
    if (const toml::array* a = t[k].as_array()) {
      if (a->size() != size_t(kNivelMax)) *err += pre + ": '" + k + "' debe tener 5 valores\n";
      for (int i = 0; i < kNivelMax; ++i)
        out[i] = i < int(a->size()) ? Finito(a->get(size_t(i))->value_or<double>(def), def) : (i ? out[i - 1] : def);
    } else {
      const float v = F(t, k, def);
      for (float& o : out) o = v;
    }
  }
  void Textos_(const toml::table& t, Textos* x) {
    x->es = S(t, "es"), x->en = S(t, "en", x->es.c_str());
    x->desc_es = S(t, "desc_es"), x->desc_en = S(t, "desc_en", x->desc_es.c_str());
    if (x->es.empty()) *err += pre + ": falta 'es' (nombre)\n";
  }
  std::vector<std::string> Lista(const toml::table& t, const char* k) {
    std::vector<std::string> v;
    if (const toml::array* a = t[k].as_array())
      for (size_t i = 0; i < a->size(); ++i) v.push_back(a->get(i)->value_or<std::string>(""));
    return v;
  }
  std::vector<float> Numeros(const toml::table& t, const char* k) {
    std::vector<float> v;
    if (const toml::array* a = t[k].as_array())
      for (size_t i = 0; i < a->size(); ++i) v.push_back(Finito(a->get(i)->value_or<double>(0), 0));
    return v;
  }
};

uint32_t Color(const std::string& s, uint32_t def) {
  if (s.size() != 6) return def;
  return uint32_t(std::strtoul(s.c_str(), nullptr, 16));
}

bool LeerTecnica(const toml::table& t, Lector& l, DefTecnica* d) {
  d->id = l.S(t, "id");
  l.pre = "tecnica '" + d->id + "'";
  d->icono = l.S(t, "icono", "ki_01");
  l.Textos_(t, &d->txt);
  const int a = Buscar(kArquetipos, l.S(t, "arquetipo"));
  if (a < 0) { *l.err += l.pre + ": arquetipo desconocido '" + l.S(t, "arquetipo") + "'\n"; return false; }
  d->arq = Arquetipo(a);
  d->color = Color(l.S(t, "color"), d->color);
  float cant[kNivelMax];
  l.Niveles(t, "dano", d->dano, 10), l.Niveles(t, "recarga", d->recarga, 1.5f), l.Niveles(t, "area", d->area, 0.4f);
  l.Niveles(t, "cantidad", cant, 1);
  for (int i = 0; i < kNivelMax; ++i) {
    d->cantidad[i] = int(std::fmin(std::fmax(cant[i], 1.0f), 64.0f));   // DS1/05: sin UB ni NaN   // auditoria #3: tope contra valores absurdos
    if (d->recarga[i] < 0.05f) d->recarga[i] = 0.05f;
  }
  d->velocidad = l.F(t, "velocidad", d->velocidad), d->duracion = l.F(t, "duracion", d->duracion);
  d->alcance = l.F(t, "alcance", d->alcance), d->empuje = l.F(t, "empuje", d->empuje), d->tick = l.F(t, "tick", d->tick);
  if (d->tick < 0 || double(d->tick * 60.0f) > double(UINT32_MAX)) {
    *l.err += l.pre + ": 'tick' fuera del rango de cuadros (uso 0.3)\n";
    d->tick = DefTecnica{}.tick;
  }
  d->perfora = l.I(t, "perfora", 0);
  return !d->id.empty();
}
}  // namespace

const char* NombreObjeto(TipoObjeto t) {
  static const char* const k[kTiposObjeto] = {"capsula", "iman", "bomba", "reloj", "zeni", "cofre"};
  return uint8_t(t) < kTiposObjeto ? k[uint8_t(t)] : "?";
}

bool CargarDatos(const std::string& ruta, Datos* out, std::string* error) {
  std::string err;
  Datos d;
  toml::table raiz;
  try {
    raiz = toml::parse_file(ruta);
  } catch (const toml::parse_error& e) {
    *error = ruta + ": " + std::string(e.description()) + "\n";
    return false;
  }
  Lector l{"partida", &err};
  bool ok = true;
  if (const toml::table* p = raiz["partida"].as_table()) {
    d.duracion = l.F(*p, "duracion", d.duracion), d.minuto_jefe = l.F(*p, "minuto_jefe", d.minuto_jefe);
    d.tope_pantalla = l.I(*p, "tope_pantalla", d.tope_pantalla);
    d.exp_base = l.F(*p, "exp_base", d.exp_base), d.exp_paso = l.F(*p, "exp_paso", d.exp_paso);
    d.exp_curva = l.F(*p, "exp_curva", d.exp_curva);
    d.invulnerable = l.F(*p, "invulnerable", d.invulnerable);
  }
  if (d.duracion < 10) err += "partida: duracion < 10 s, uso 900\n", d.duracion = 900;
  if (d.tope_pantalla < 1 || d.tope_pantalla > Sim::kMax) err += "partida: tope_pantalla fuera de 1..2048\n", d.tope_pantalla = 600;
  if (!std::isfinite(d.exp_base) || d.exp_base <= 0) err += "partida: exp_base debe ser positiva y finita, uso 10\n", d.exp_base = 10;
  if (!std::isfinite(d.exp_paso) || d.exp_paso < 0) err += "partida: exp_paso debe ser no negativa y finita, uso 5\n", d.exp_paso = 5;
  if (!std::isfinite(d.exp_curva) || d.exp_curva < 0) err += "partida: exp_curva debe ser no negativa y finita, uso 1.75\n", d.exp_curva = 1.75f;

  auto lista = [&](const char* k) -> const toml::array* {
    const toml::array* a = raiz[k].as_array();
    if (!a || a->empty()) err += std::string("falta la lista [[") + k + "]]\n", ok = false;
    return a;
  };
  auto tabla = [](const toml::array* a, size_t i) { return a->get(i)->as_table(); };

  if (const toml::array* a = lista("tecnica"))
    for (size_t i = 0; i < a->size(); ++i)
      if (const toml::table* t = tabla(a, i)) {
        DefTecnica x;
        if (LeerTecnica(*t, l, &x)) d.tecnicas.push_back(std::move(x)); else ok = false;
      }
  const int nBase = int(d.tecnicas.size());
  auto tecnica = [&](const std::string& id) { for (int i = 0; i < nBase; ++i) if (d.tecnicas[i].id == id) return i; return -1; };

  if (const toml::array* a = lista("pasiva"))
    for (size_t i = 0; i < a->size(); ++i)
      if (const toml::table* t = tabla(a, i)) {
        DefPasiva x;
        x.id = l.S(*t, "id"), l.pre = "pasiva '" + x.id + "'";
        x.icono = l.S(*t, "icono", "utilidad_01");
        l.Textos_(*t, &x.txt);
        const int e = Buscar(kEfectos, l.S(*t, "efecto"));
        if (e < 0) { err += l.pre + ": efecto desconocido '" + l.S(*t, "efecto") + "'\n", ok = false; continue; }
        x.efecto = Efecto(e), x.por_nivel = l.F(*t, "por_nivel", x.por_nivel);
        d.pasivas.push_back(std::move(x));
      }

  if (const toml::array* a = raiz["evolucion"].as_array())
    for (size_t i = 0; i < a->size(); ++i)
      if (const toml::table* t = tabla(a, i)) {
        DefTecnica x;
        if (!LeerTecnica(*t, l, &x)) { ok = false; continue; }
        x.evoluciona_de = tecnica(l.S(*t, "base"));
        for (int p = 0; p < int(d.pasivas.size()); ++p) if (d.pasivas[p].id == l.S(*t, "pasiva")) x.pasiva = p;
        if (x.evoluciona_de < 0 || x.pasiva < 0) { err += "evolucion '" + x.id + "': 'base' o 'pasiva' no existen\n", ok = false; continue; }
        d.tecnicas.push_back(std::move(x));
      }
  if (int(d.tecnicas.size()) > kMaxDefs) err += "como mucho 48 tecnicas + evoluciones\n", ok = false;

  if (const toml::array* a = lista("personaje"))
    for (size_t i = 0; i < a->size(); ++i)
      if (const toml::table* t = tabla(a, i)) {
        DefPersonaje x;
        x.id = l.S(*t, "id"), l.pre = "personaje '" + x.id + "'";
        l.Textos_(*t, &x.txt);
        x.fid = l.I(*t, "fid", x.fid), x.vida = l.F(*t, "vida", x.vida), x.velocidad = l.F(*t, "velocidad", x.velocidad);
        x.recogida = l.F(*t, "recogida", x.recogida);
        if (const std::string ps = l.S(*t, "pasiva"); !ps.empty()) {   // C01: pasiva propia (un efecto)
          x.pasiva = Buscar(kEfectos, ps), x.pasiva_valor = l.F(*t, "pasiva_valor", 0);
          if (x.pasiva < 0) err += l.pre + ": pasiva '" + ps + "' no es un efecto\n";
        }
        x.tecnica = tecnica(l.S(*t, "tecnica"));
        if (x.tecnica < 0) { err += l.pre + ": tecnica inicial '" + l.S(*t, "tecnica") + "' no existe\n", ok = false; continue; }
        d.personajes.push_back(std::move(x));
      }

  if (const toml::array* a = lista("enemigo"))
    for (size_t i = 0; i < a->size() && i < size_t(kMaxEnemigos); ++i)
      if (const toml::table* t = tabla(a, i)) {
        DefEnemigo x;
        x.id = l.S(*t, "id"), l.pre = "enemigo '" + x.id + "'";
        x.fid = l.I(*t, "fid", x.fid), x.tono = l.F(*t, "tono", 0), x.traje = l.I(*t, "traje", 0), x.vida = l.F(*t, "vida", x.vida);
        x.velocidad = l.F(*t, "velocidad", x.velocidad), x.dano = l.F(*t, "dano", x.dano), x.radio = l.F(*t, "radio", x.radio);
        x.exp = l.F(*t, "exp", x.exp), x.escala = l.F(*t, "escala", x.escala), x.cofre = l.F(*t, "cofre", 0);
        x.anillo = std::max(0, Buscar(kAnillos, l.S(*t, "anillo")));
        {   // A06: conducta (y sus numeros por tipo); sin 'conducta' en el toml, la de serie segun el id (lo que no se nombra persigue)
          static const Nombre kConductas[] = {{"persigue", 0}, {"tirador", 1}, {"dispara", 1}, {"embestida", 2}, {"explota", 3},
                                              {"elite", 4},    {"invoca", 5},  {"absorbe", 6}, {"escudo", 7}};
          int c = Buscar(kConductas, l.S(*t, "conducta"));
          if (c < 0) c = x.anillo > 0 ? 4 : x.id == "saibaman_rapido" ? 3 : x.id == "celljr" ? 1 : x.id == "celljr_oscuro" ? 2 : 0;
          x.conducta = Conducta(c);
          switch (x.conducta) {
            case Conducta::Tirador: x.cond_cada = 3.5f, x.cond_aviso = 0.5f, x.cond_dano = 6, x.cond_rango = 8; break;
            case Conducta::Embestida: x.cond_cada = 4, x.cond_aviso = 0.8f, x.cond_vel = 11, x.cond_rango = 9; break;
            case Conducta::Explota: x.cond_aviso = 0.9f, x.cond_dano = 12, x.cond_radio = 2.6f; break;
            case Conducta::Elite: x.cond_cada = 8, x.cond_aviso = 1.0f, x.cond_n = 3, x.cond_escudo = 0.35f; break;
            case Conducta::Invoca: x.cond_cada = 6, x.cond_aviso = 0.8f, x.cond_n = 2; break;
            case Conducta::Absorbe: x.cond_radio = 3.0f, x.cond_escudo = 0.5f; break;
            case Conducta::Frontal: x.cond_escudo = 0.85f; break;
            default: break;
          }
          x.cond_cada = std::fmax(0.3f, l.F(*t, "cond_cada", x.cond_cada)), x.cond_aviso = std::fmax(0.1f, l.F(*t, "cond_aviso", x.cond_aviso));
          x.cond_dano = l.F(*t, "cond_dano", x.cond_dano), x.cond_vel = l.F(*t, "cond_vel", x.cond_vel), x.cond_rango = l.F(*t, "cond_rango", x.cond_rango);
          x.cond_radio = l.F(*t, "cond_radio", x.cond_radio), x.cond_escudo = std::clamp(l.F(*t, "cond_escudo", x.cond_escudo), 0.0f, 3.0f);
          x.cond_n = std::clamp(l.I(*t, "cond_n", x.cond_n), 0, 8);
        }
        if (x.radio < 0.1f || x.radio > 2.0f) err += l.pre + ": radio fuera de 0.1..2\n", x.radio = 0.45f;
        d.enemigos.push_back(std::move(x));
      }
  while (!d.enemigos.empty() && d.enemigos.size() < size_t(kTiposEnemigo)) d.enemigos.push_back(d.enemigos.back());
  auto enemigo = [&](const std::string& id) { for (int i = 0; i < int(d.enemigos.size()); ++i) if (d.enemigos[size_t(i)].id == id) return i; return -1; };

  // [jefe] (uno) o [[jefe]] (uno por escenario)
  auto leer_jefe = [&](const toml::table& t) {
    DefJefe j;
    l.pre = "jefe";
    j.id = l.S(t, "id", "cell"), l.pre = "jefe '" + j.id + "'", l.Textos_(t, &j.txt);
    j.fid = l.I(t, "fid", j.fid), j.vida = l.F(t, "vida", j.vida), j.velocidad = l.F(t, "velocidad", j.velocidad);
    j.dano = l.F(t, "dano", j.dano), j.radio = l.F(t, "radio", j.radio), j.escala = l.F(t, "escala", j.escala);
    j.cada = l.F(t, "cada", j.cada), j.aviso = l.F(t, "aviso", j.aviso);
    j.carga_vel = l.F(t, "carga_vel", j.carga_vel), j.carga_dura = l.F(t, "carga_dura", j.carga_dura);
    j.carga_ancho = l.F(t, "carga_ancho", j.carga_ancho), j.carga_dano = l.F(t, "carga_dano", j.carga_dano);
    j.anillo_balas = std::clamp(l.I(t, "anillo_balas", j.anillo_balas), 0, 512), j.bala_vel = l.F(t, "bala_vel", j.bala_vel);
    j.bala_dano = l.F(t, "bala_dano", j.bala_dano), j.bala_radio = l.F(t, "bala_radio", j.bala_radio);
    j.lluvia_n = std::clamp(l.I(t, "lluvia_n", j.lluvia_n), 0, 24), j.lluvia_ancho = std::clamp(l.F(t, "lluvia_ancho", j.lluvia_ancho), 1.0f, 64.0f);
    j.lluvia_alto = std::clamp(l.F(t, "lluvia_alto", j.lluvia_alto), 1.0f, 64.0f), j.lluvia_radio = l.F(t, "lluvia_radio", j.lluvia_radio);
    j.lluvia_dano = l.F(t, "lluvia_dano", j.lluvia_dano), j.lluvia_aviso = std::max(0.3f, l.F(t, "lluvia_aviso", j.lluvia_aviso));
    if (const toml::array* a = t["fases_fid"].as_array())   // SUR-8: modelos de cada fase (fid del manifiesto) y su forma
      for (size_t k = 0; k < a->size() && k < 3; ++k) j.fases_fid.push_back(int(a->get(k)->value_or<int64_t>(j.fid)));
    if (const toml::array* a = t["fases_forma"].as_array())
      for (size_t k = 0; k < a->size() && k < 3; ++k) j.fases_forma.push_back(int(a->get(k)->value_or<int64_t>(0)));
    d.jefes.push_back(std::move(j));
  };
  if (const toml::table* t = raiz["jefe"].as_table()) leer_jefe(*t);
  else if (const toml::array* a = raiz["jefe"].as_array())
    for (size_t i = 0; i < a->size(); ++i)
      if (const toml::table* t = tabla(a, i)) leer_jefe(*t);
  if (d.jefes.empty()) err += "falta [jefe] (uso Cell por defecto)\n", d.jefes.emplace_back();

  if (const toml::table* t = raiz["recogibles"].as_table()) {   // sin la tabla: los valores por defecto de juego.h
    DefRecogibles& r = d.recogibles;
    l.pre = "recogibles";
    r.gema_verde = l.F(*t, "gema_verde", r.gema_verde), r.gema_roja = l.F(*t, "gema_roja", r.gema_roja);
    for (int k = 0; k < kTiposObjeto - 1; ++k) r.prob[k] = l.F(*t, NombreObjeto(TipoObjeto(k)), r.prob[k]);
    r.cura = l.F(*t, "cura", r.cura), r.iman_dura = l.F(*t, "iman_dura", r.iman_dura), r.iman_vel = l.F(*t, "iman_vel", r.iman_vel);
    r.bomba_dano = l.F(*t, "bomba_dano", r.bomba_dano), r.bomba_radio = l.F(*t, "bomba_radio", r.bomba_radio);
    r.reloj_dura = l.F(*t, "reloj_dura", r.reloj_dura), r.zeni_valor = l.I(*t, "zeni_valor", r.zeni_valor);
    r.victoria_tras = l.F(*t, "victoria_tras", r.victoria_tras);
    r.reloj_jefe = std::clamp(l.F(*t, "reloj_jefe", r.reloj_jefe), 0.0f, 1.0f);
    float s = 0;
    for (float p : r.prob) s += p;
    if (s > 1) err += "recogibles: las probabilidades suman mas de 1\n";
  }

  if (const toml::array* a = lista("oleada"))
    for (size_t i = 0; i < a->size(); ++i)
      if (const toml::table* t = tabla(a, i)) {
        Oleada o;
        l.pre = "oleada " + std::to_string(i + 1);
        o.minuto = l.F(*t, "minuto", o.minuto), o.ritmo = l.F(*t, "ritmo", o.ritmo), o.tope = l.I(*t, "tope", o.tope);
        o.vida = l.F(*t, "vida", o.vida);
        if (const toml::array* p = (*t)["tipos"].as_array())
          for (size_t k = 0; k < p->size() && k < size_t(kTiposEnemigo); ++k) o.peso[k] = float(p->get(k)->value_or<double>(0));
        float s = 0;
        for (float w : o.peso) s += w;
        if (s <= 0) err += l.pre + ": 'tipos' sin ningun peso\n", o.peso[0] = 1;
        if (!d.oleadas.empty() && o.minuto < d.oleadas.back().minuto) err += l.pre + ": minutos desordenados\n", ok = false;
        d.oleadas.push_back(o);
      }

  // ---- tanda 4: rompibles, escenarios, dificultad, tienda y racha
  if (const toml::table* t = raiz["rompibles"].as_table()) {
    l.pre = "rompibles";
    d.rompible_celda = std::max(3.0f, l.F(*t, "celda", d.rompible_celda)), d.rompible_densidad = l.F(*t, "densidad", d.rompible_densidad);
  }
  if (const toml::array* a = raiz["rompible"].as_array())
    for (size_t i = 0; i < a->size(); ++i)
      if (const toml::table* t = tabla(a, i)) {
        DefRompible x;
        x.id = l.S(*t, "id"), l.pre = "rompible '" + x.id + "'";
        x.vida = l.F(*t, "vida", x.vida), x.exp = l.F(*t, "exp", x.exp);
        const std::vector<float> p = l.Numeros(*t, "suelta");   // capsula, iman, bomba, reloj, zeni, cofre
        float s = 0;
        for (int k = 0; k < kTiposObjeto && k < int(p.size()); ++k) x.prob[k] = p[size_t(k)], s += p[size_t(k)];
        if (s > 1) err += l.pre + ": 'suelta' suma mas de 1\n";
        if (!x.id.empty() && d.rompibles.size() < size_t(kMaxRompDefs)) d.rompibles.push_back(std::move(x));
        else if (!x.id.empty()) err += "rompible '" + x.id + "': excede el límite de definiciones\n";
      }
  auto rompible = [&](const std::string& id) { for (int i = 0; i < int(d.rompibles.size()); ++i) if (d.rompibles[size_t(i)].id == id) return i; return -1; };
  auto jefe = [&](const std::string& id) { for (int i = 0; i < int(d.jefes.size()); ++i) if (d.jefes[size_t(i)].id == id) return i; return -1; };

  if (const toml::array* a = raiz["escenario"].as_array())
    for (size_t i = 0; i < a->size() && d.escenarios.size() < size_t(kMaxEscenarios); ++i)
      if (const toml::table* t = tabla(a, i)) {
        DefEscenario x;
        x.id = l.S(*t, "id"), l.pre = "escenario '" + x.id + "'";
        l.Textos_(*t, &x.txt);
        x.jefe = std::max(0, jefe(l.S(*t, "jefe")));
        if (jefe(l.S(*t, "jefe")) < 0) err += l.pre + ": jefe '" + l.S(*t, "jefe") + "' no existe (uso el primero)\n";
        const std::vector<std::string> en = l.Lista(*t, "enemigos");
        for (int k = 0; k < kTiposEnemigo; ++k) {
          const int e = k < int(en.size()) ? enemigo(en[size_t(k)]) : -1;
          if (e < 0) err += l.pre + ": enemigo " + std::to_string(k + 1) + " no existe (uso el de ese hueco)\n";
          x.enemigos[k] = e >= 0 ? e : std::min(k, int(d.enemigos.size()) - 1);
        }
        for (const std::string& r : l.Lista(*t, "rompibles"))
          if (rompible(r) >= 0) x.rompibles.push_back(rompible(r));
          else err += l.pre + ": rompible '" + r + "' no existe\n";
        x.ambiente = l.Lista(*t, "ambiente"), x.ambiente_alfa = l.Numeros(*t, "ambiente_alfa");
        x.ambiente_alfa.resize(x.ambiente.size(), 0.4f);
        x.vida = l.F(*t, "vida", x.vida), x.ritmo = l.F(*t, "ritmo", x.ritmo);
        x.rayo_cada = l.F(*t, "rayo_cada", 0), x.rayo_aviso = std::max(0.3f, l.F(*t, "rayo_aviso", x.rayo_aviso));
        x.rayo_radio = l.F(*t, "rayo_radio", x.rayo_radio), x.rayo_dano = l.F(*t, "rayo_dano", x.rayo_dano);
        x.rayo_dano_enemigos = l.F(*t, "rayo_dano_enemigos", x.rayo_dano_enemigos);
        if (const toml::table* dc = (*t)["decorado"].as_table()) {
          DefDecorado& g = x.deco;
          g.suelos = l.Lista(*dc, "suelos"), g.manchas = l.Lista(*dc, "manchas"), g.adornos = l.Lista(*dc, "adornos");
          g.reparto = l.Numeros(*dc, "reparto"), g.manchas_alfa = l.Numeros(*dc, "manchas_alfa"), g.manchas_tam = l.Numeros(*dc, "manchas_tam");
          g.anclas = l.Numeros(*dc, "anclas");
          for (float p : l.Numeros(*dc, "planos")) {
             if (double(p) >= double(std::numeric_limits<int>::min()) && double(p) <= double(std::numeric_limits<int>::max()))
               g.planos.push_back(int(p));
             else err += l.pre + ": plano fuera de rango; se omite\n";
           }
          g.reparto.resize(g.manchas.size(), 1.0f), g.manchas_alfa.resize(g.manchas.size(), 1.0f), g.manchas_tam.resize(g.manchas.size(), 1.0f);
          g.anclas.resize(g.adornos.size(), 250.0f);
          if (g.suelos.size() > 3) g.suelos.resize(3);
          if (g.manchas.size() > 8) err += l.pre + ": como mucho 8 manchas\n", g.manchas.resize(8);
          if (g.adornos.size() > 24) err += l.pre + ": como mucho 24 adornos\n", g.adornos.resize(24), g.anclas.resize(24);
          g.densidad_adornos = l.F(*dc, "densidad_adornos", g.densidad_adornos), g.densidad_manchas = l.F(*dc, "densidad_manchas", g.densidad_manchas);
          g.color = Color(l.S(*dc, "color"), g.color);
        }
        if (!x.id.empty()) d.escenarios.push_back(std::move(x));
      }
  if (d.escenarios.empty()) {   // toml de antes de la tanda 4: la llanura con los 6 primeros enemigos y el primer jefe
    DefEscenario x;
    x.id = "llanura", x.txt = {"Llanura", "Plains", "", ""};
    for (int k = 0; k < kTiposEnemigo; ++k) x.enemigos[k] = std::min(k, int(d.enemigos.size()) - 1);
    d.escenarios.push_back(std::move(x));
  }

  if (const toml::table* t = raiz["dificultad"].as_table()) {
    const char* nombres[kDificultades] = {"normal", "dificil", "extremo"};
    for (int k = 0; k < kDificultades; ++k)
      if (const toml::table* n = (*t)[nombres[k]].as_table()) {
        l.pre = std::string("dificultad ") + nombres[k];
        DefDificultad& f = d.dificultad[k];
        f.vida = l.F(*n, "vida", f.vida), f.dano = l.F(*n, "dano", f.dano), f.zeni = l.F(*n, "zeni", f.zeni);
      }
  }
  if (const toml::array* a = raiz["mejora_permanente"].as_array())
    for (size_t i = 0; i < a->size() && d.mejoras.size() < size_t(kMejorasPerm); ++i)
      if (const toml::table* t = tabla(a, i)) {
        DefMejoraPerm x;
        x.id = l.S(*t, "id"), l.pre = "mejora_permanente '" + x.id + "'";
        x.icono = l.S(*t, "icono"), l.Textos_(*t, &x.txt);
        const int e = Buscar(kEfectos, l.S(*t, "efecto"));
        if (e < 0) { err += l.pre + ": efecto desconocido '" + l.S(*t, "efecto") + "'\n"; continue; }
        x.efecto = Efecto(e), x.por_nivel = l.F(*t, "por_nivel", x.por_nivel);
        const toml::array* p = (*t)["precios"].as_array();
        if (!p || p->size() != size_t(kNivelMax)) err += l.pre + ": 'precios' debe tener 5 valores\n";
        for (size_t k = 0; p && k < p->size() && k < size_t(kNivelMax); ++k) {
          const auto v = p->get(k)->value<double>();
          if (v && std::isfinite(*v) && *v <= double(std::numeric_limits<int>::max()))
            x.precio[k] = int(std::max(1.0, *v));
          else err += l.pre + ": precio no representable; se usa el de serie\n";
        }
        d.mejoras.push_back(std::move(x));
      }
  if (const toml::table* t = raiz["subida"].as_table()) {   // A09; sin la tabla: los valores de juego.h
    l.pre = "subida";
    DefSubida& u = d.subida;
    const std::vector<float> pes = l.Numeros(*t, "pesos"), niv = l.Numeros(*t, "niveles"), cof = l.Numeros(*t, "cofre_pesos");
    if (!pes.empty() && pes.size() != size_t(kRarezas)) err += "subida: 'pesos' debe tener 5 valores\n";
    if (!niv.empty() && niv.size() != size_t(kRarezas)) err += "subida: 'niveles' debe tener 5 valores\n";
    if (!cof.empty() && cof.size() != 3) err += "subida: 'cofre_pesos' debe tener 3 valores\n";
    for (int k = 0; k < kRarezas; ++k) {
      if (k < int(pes.size())) u.peso[k] = std::fmax(0.0f, pes[size_t(k)]);
      if (k < int(niv.size())) u.niveles[k] = int(std::clamp(niv[size_t(k)], 0.0f, float(kNivelMax - 1)));
    }
    for (int k = 0; k < 3 && k < int(cof.size()); ++k) u.cofre_peso[k] = std::fmax(0.0f, cof[size_t(k)]);
    u.suerte_k = l.F(*t, "suerte_k", u.suerte_k);
    u.reroll_base = std::max(0, l.I(*t, "reroll_base", u.reroll_base)), u.reroll_paso = std::max(0, l.I(*t, "reroll_paso", u.reroll_paso));
    u.saltar_zeni = std::max(0, l.I(*t, "saltar_zeni", u.saltar_zeni)), u.saltar_vida = std::clamp(l.F(*t, "saltar_vida", u.saltar_vida), 0.0f, 1.0f);
    u.desterros = std::max(0, l.I(*t, "desterros", u.desterros));
  }
  if (const toml::table* t = raiz["formas"].as_table()) {   // A05; sin la tabla: los valores de juego.h
    l.pre = "formas";
    DefFormas& f = d.formas;
    f.dura = std::fmax(1.0f, l.F(*t, "dura", f.dura)), f.kills = std::fmax(1.0f, l.F(*t, "kills", f.kills)), f.tiempo = std::fmax(1.0f, l.F(*t, "tiempo", f.tiempo));
    f.dano = l.F(*t, "dano", f.dano), f.velocidad = l.F(*t, "velocidad", f.velocidad), f.max = std::clamp(l.I(*t, "max", f.max), 1, 3);
  }
  if (const toml::table* t = raiz["ritmo"].as_table()) {   // A08; sin la tabla: los valores de juego.h
    l.pre = "ritmo";
    DefRitmo& r = d.ritmo;
    const std::vector<float> mj = l.Numeros(*t, "minijefes");
    for (int k = 0; k < 2 && k < int(mj.size()); ++k) r.minijefe[k] = std::clamp(mj[size_t(k)], 0.5f, 15.0f);
    r.jefe = std::clamp(l.F(*t, "jefe", r.jefe), 1.0f, 15.0f), r.furia = std::clamp(l.F(*t, "furia", r.furia), 1.0f, 15.0f);
    r.aviso = std::clamp(l.F(*t, "aviso", r.aviso), 0.0f, 30.0f);
    r.mini_vida = std::fmax(1.0f, l.F(*t, "mini_vida", r.mini_vida)), r.mini_escala = std::clamp(l.F(*t, "mini_escala", r.mini_escala), 1.0f, 3.0f);
    r.furia_vel = std::fmax(1.0f, l.F(*t, "furia_vel", r.furia_vel)), r.furia_dano = std::fmax(1.0f, l.F(*t, "furia_dano", r.furia_dano));
  }
  if (const toml::array* a = raiz["evento"].as_array())
    for (size_t i = 0; i < a->size() && d.ritmo.eventos.size() < size_t(kMaxEventos2); ++i)
      if (const toml::table* t = tabla(a, i)) {
        static const Nombre kEventos[] = {{"enjambre", 0}, {"cerco", 1}, {"meteoros", 2}};
        DefEvento e;
        l.pre = "evento " + std::to_string(i + 1);
        e.minuto = std::clamp(l.F(*t, "minuto", e.minuto), 0.0f, 15.0f);
        e.tipo = Buscar(kEventos, l.S(*t, "tipo"));
        if (e.tipo < 0) { err += l.pre + ": 'tipo' debe ser enjambre, cerco o meteoros\n"; continue; }
        e.n = std::clamp(l.I(*t, "n", e.n), 1, 200);
        d.ritmo.eventos.push_back(e);
      }
  if (const toml::table* t = raiz["racha"].as_table()) {
    l.pre = "racha";
    d.racha.dura = l.F(*t, "dura", d.racha.dura), d.racha.exp = l.F(*t, "exp", d.racha.exp), d.racha.tope = l.F(*t, "tope", d.racha.tope);
  }

  if (d.personajes.empty() || d.tecnicas.empty() || d.oleadas.empty() || d.escenarios.empty() || d.jefes.empty())   // auditoria #1
    err += "datos incompletos: personaje/tecnica/oleada vacios (revisa que cada entrada sea una tabla)\n", ok = false;
  for (const DefJefe& j : d.jefes) {
    for (const DefDificultad& f : d.dificultad) {
      const double vida = double(j.vida) * double(f.vida);
      if (j.vida <= 0 || f.vida <= 0 || vida > std::numeric_limits<float>::max() || float(vida) * 0.33f <= 0) {
        err += "jefe '" + j.id + "': vida incompatible con los umbrales de fase\n";
        ok = false;
        break;
      }
    }
  }
  *error = err;
  if (!ok) return false;
  *out = std::move(d);
  return true;
}

// ---------------------------------------------------------------- partida
const Textos& NombreRareza(Rareza r) {
  static const Textos k[kRarezas] = {{"Común", "Common", "", ""},        {"Poco común", "Uncommon", "", ""}, {"Rara", "Rare", "", ""},
                                     {"Épica", "Epic", "", ""},          {"Legendaria", "Legendary", "", ""}};
  return k[int(r)];
}
uint32_t ColorRareza(Rareza r) {
  static const uint32_t k[kRarezas] = {0xB4B4B4, 0x4CD964, 0x4A90FF, 0xB45CFF, 0xFFB020};
  return k[int(r)];
}

uint32_t Juego::Azar() {   // xorshift64*
  rng_ ^= rng_ >> 12, rng_ ^= rng_ << 25, rng_ ^= rng_ >> 27;
  return uint32_t((rng_ * 0x2545F4914F6CDD1DULL) >> 32);
}

void Juego::Evento_(TipoEvento t, int a, float x, float y, float v) {
  if (nEventos < kMaxEventos) eventos[nEventos++] = {t, a, x, y, v};
}
void Juego::EventoFijo_(TipoEvento t, int a, float x, float y, float v) {
  if (nEventos >= kMaxEventos) {   // llena (una bomba sobre 300 enemigos): cede el ultimo que no sea tambien fijo (un golpe o una muerte)
    for (int k = kMaxEventos - 1; k >= 0; --k)
      if (eventos[k].tipo == TipoEvento::Golpe || eventos[k].tipo == TipoEvento::Muerte || eventos[k].tipo == TipoEvento::Recogida) {
        eventos[k] = {t, a, x, y, v};
        return;
      }
    return;
  }
  eventos[nEventos++] = {t, a, x, y, v};
}

// ---------------------------------------------------------------- objetos (capsula, iman, bomba, reloj, zeni, cofre)
void Juego::Poner(float x, float y, TipoObjeto t, int tier) {
  if (t == TipoObjeto::Cofre && config.meta_v2 && tier <= 0) {   // A09: el tier se sortea al caer, para poder dibujarlo distinto
    const DefSubida& sb = datos->subida;
    const float k = 1 + Mult(Efecto::Suerte) * sb.suerte_k;
    const float w[3] = {sb.cofre_peso[0], sb.cofre_peso[1] * k, sb.cofre_peso[2] * k};
    const float a = Azar01() * (w[0] + w[1] + w[2]);
    tier = a < w[0] ? 1 : (a - w[0] < w[1] ? 3 : 5);
  }
  int i = nObjetos < kMaxObjetos ? nObjetos++ : -1;
  if (i < 0) {   // lleno: ocupa el sitio del objeto (que no sea un cofre) mas lejano al jugador; los que quedan atras no se echan de menos
    float m = -1;
    for (int k = 0; k < kMaxObjetos; ++k) {
      const float vx = objetos[k].x - jugador.x, vy = objetos[k].y - jugador.y, d2 = vx * vx + vy * vy;
      if (objetos[k].tipo != TipoObjeto::Cofre && d2 > m) m = d2, i = k;
    }
  }
  if (i >= 0) objetos[i] = {x, y, t, uint8_t(t == TipoObjeto::Cofre && config.meta_v2 ? tier : 0)};
}

void Juego::Soltar(float x, float y, float cofre) {
  const float suerte = 1 + Mult(Efecto::Suerte);   // mejora permanente: mas caidas (no mas cofres de elite: ya los da su toml)
  if (cofre > 0 && Azar01() < cofre) return Poner(x, y, TipoObjeto::Cofre);
  const DefRecogibles& r = datos->recogibles;
  float a = Azar01();
  for (int k = 0; k < kTiposObjeto - 1; ++k) {
    if (a < r.prob[k] * suerte) return Poner(x, y, TipoObjeto(k));
    a -= r.prob[k] * suerte;
  }
}

void Juego::Recoger(const Objeto& o) {
  const DefRecogibles& r = datos->recogibles;
  Evento_(TipoEvento::Objeto, int(o.tipo), o.x, o.y);
  switch (o.tipo) {
    case TipoObjeto::Capsula: jugador.vida = std::fmin(jugador.vida_max, jugador.vida + r.cura * jugador.vida_max); break;
    case TipoObjeto::Iman: iman = r.iman_dura; break;
    case TipoObjeto::Bomba: {
      Jugo_(0, 1, 0.7f, jugador.x, jugador.y);
      const float r2 = r.bomba_radio * r.bomba_radio;
      for (int i = 0; i < horda_.n; ++i) {
        const float vx = horda_.x[i] - jugador.x, vy = horda_.y[i] - jugador.y;
        if (vx * vx + vy * vy < r2) Dano(i, r.bomba_dano, -1, 0, jugador.x, jugador.y);
      }
      if (JefeEn(jugador.x, jugador.y, r.bomba_radio)) DanoJefe(r.bomba_dano, -1);
      RomperEn(jugador.x, jugador.y, r.bomba_radio);
      break;
    }
    case TipoObjeto::Reloj:
      congelado = r.reloj_dura;
      for (int i = 0; i < horda_.n; ++i) horda_.vel[i] = 0;
      break;
    case TipoObjeto::Zeni: {   // con la dificultad y la mejora Zeni extra; la fraccion se guarda para la siguiente moneda
      zeni_resto_ += float(r.zeni_valor) * mzeni_ * (1 + Mult(Efecto::Zeni) + Mult(Efecto::Maldicion));
      const int n = int(zeni_resto_);
      zeni += n, zeni_resto_ -= float(n);
      break;
    }
    case TipoObjeto::Cofre:
      if (config.meta_v2) AbrirCofre_(o.tier > 0 ? o.tier : 1);   // A09: premios aplicados ya; la pantalla del cofre sale al final del paso
      else ++subidas_;
      break;   // mejora gratis: la eleccion sale al final del paso, como al subir de nivel
  }
}

float Juego::Recarga01(int s) const {
  if (s < 0 || s >= nTecnicas) return 0;
  const float total = datos->tecnicas[tecnicas[s].def].recarga[tecnicas[s].nivel - 1] * std::fmax(0.4f, 1 - Mult(Efecto::Recarga));
  return total > 0 ? std::fmin(1.0f, std::fmax(0.0f, recarga_[s] / total)) : 0;
}

Efecto2D* Juego::NuevoEfecto() {
  if (nEfectos >= kMaxEfectos) return nullptr;
  efectos[nEfectos] = Efecto2D{};
  efectos[nEfectos].id_visual = ++siguiente_fx_visual;
  return &efectos[nEfectos++];
}

float Juego::Mult(Efecto e) const {   // suma de la bonificacion de las pasivas con ese efecto y de las mejoras permanentes
  float s = perm_[int(e)];
  if (forma > 0 && e == Efecto::Dano) s += datos->formas.dano;   // A05: transformado
  const DefPersonaje& pj = datos->personajes[size_t(personaje)];
  if (pj.pasiva == int(e)) s += pj.pasiva_valor;                 // C01: pasiva propia del luchador
  if (e == Efecto::Dano) {                                       // C04: Crecimiento = dano de mas por nivel del luchador
    const float c = Mult(Efecto::Crecimiento);
    if (c > 0) s += c * float(jugador.nivel);
  }
  for (int i = 0; i < nPasivas; ++i)
    if (datos->pasivas[pasivas[i].def].efecto == e) s += datos->pasivas[pasivas[i].def].por_nivel * float(pasivas[i].nivel);
  return s;
}

int Juego::NivelPasiva(int def) const {
  for (int i = 0; i < nPasivas; ++i) if (pasivas[i].def == def) return pasivas[i].nivel;
  return 0;
}
int Juego::NivelTecnica(int def) const {
  for (int i = 0; i < nTecnicas; ++i) if (tecnicas[i].def == def) return tecnicas[i].nivel;
  return 0;
}

const char* Juego::Icono(const Opcion& o) const {
  if (o.tipo == TipoOpcion::Pasiva) return datos->pasivas[o.def].icono.c_str();
  if (o.tipo == TipoOpcion::Curar) return "utilidad_04";
  return datos->tecnicas[o.def].icono.c_str();
}
const Textos& Juego::Texto(const Opcion& o) const {
  static const Textos kCurar{"Semilla del ermitaño", "Senzu Bean", "Recupera un 30 % de la vida.", "Restores 30% health."};
  if (o.tipo == TipoOpcion::Pasiva) return datos->pasivas[o.def].txt;
  if (o.tipo == TipoOpcion::Curar) return kCurar;
  return datos->tecnicas[o.def].txt;
}

VistaEnemigo Juego::Enemigo(int i) const {
  const DefEnemigo& d = datos->enemigos[tipo_[i]];
  float f = horda_.fase[i] + tiempo * 1.2f * d.velocidad / 2.5f;
  VistaEnemigo v{horda_.x[i], horda_.y[i], f - std::floor(f), destello_[i], d.escala, int(tipo_[i]), d.anillo};
  if (mini_[i]) v.escala *= datos->ritmo.mini_escala, v.mini = true;
  if (config.ia) {
    if (cond_est_[i] == 1 && d.conducta != Conducta::Persigue) v.aviso = 1;
    if (escudo_[i] > 0) v.escudo = std::fmin(1.0f, escudo_[i] / std::fmax(1.0f, vida_[i] * d.cond_escudo + escudo_[i]));
    if (d.conducta == Conducta::Frontal) v.escudo = 1;
  }
  return v;
}

bool Juego::Iniciar(const Datos& d, int pj, uint64_t semilla, float dura, const Config& c) {
  if (d.personajes.empty() || d.tecnicas.empty() || d.enemigos.size() < size_t(kTiposEnemigo) || d.oleadas.empty() || d.escenarios.empty() ||
      d.jefes.empty())
    return false;
  datos = &d;
  personaje = pj < 0 || pj >= int(d.personajes.size()) ? 0 : pj;
  config = c;
  config.escenario = std::clamp(c.escenario, 0, int(d.escenarios.size()) - 1), config.dificultad = std::clamp(c.dificultad, 0, kDificultades - 1);
  std::memset(perm_, 0, sizeof perm_);
  for (int k = 0; k < kMejorasPerm && k < int(d.mejoras.size()); ++k) {
    config.mejoras[k] = std::clamp(c.mejoras[k], 0, kNivelMax);
    perm_[int(d.mejoras[size_t(k)].efecto)] += d.mejoras[size_t(k)].por_nivel * float(config.mejoras[k]);
  }
  const DefDificultad& dif = d.dificultad[config.dificultad];
  mdano_ = dif.dano, mvida_ = dif.vida * Escenario().vida, mzeni_ = dif.zeni, zeni_resto_ = 0;
  vidas_extra = perm_[int(Efecto::Reanimar)] > 0 ? 1 : 0, jefes_derrotados = 0;
  racha = racha_max = 0, racha_t = 0;
  nRompibles = rotos = nRotos_ = 0;
  rng_ = semilla ? semilla : 0x9E3779B97F4A7C15ULL;
  sal_ = uint32_t(rng_ ^ (rng_ >> 32)) | 1u;
  duracion = dura > 0 ? dura : d.duracion;
  escala_ = 900.0f / duracion;
  estado = Estado::Jugando;
  tiempo = 0, pasos = 0, derrotados = 0, nEfectos = nGemas = nEventos = nOpciones = 0, nTecnicas = nPasivas = 0;
  aparecer_ = regen_ = victoria_ = 0, subidas_ = 0;
  ultima_subida_ = -3;
  cofre_ = false, nPremios = tierCofre = resorteos = 0, desterr_tec_ = desterr_pas_ = 0;
  forma = 0, ki = forma_t = 0, derrotados_ki_ = 0;
  furia = false, eventos_vistos = minijefes_vistos = 0, nProg_ = 0;
  std::memset(mini_, 0, sizeof mini_), std::memset(cond_est_, 0, sizeof cond_est_);
  std::memset(cond_t_, 0, sizeof cond_t_), std::memset(escudo_, 0, sizeof escudo_);
  pausa_ = pausa_cd_ = temblor_cd_ = 0, pausas = temblores = 0, pausa_total = 0;
  desterros_ = config.meta_v2 ? d.subida.desterros + int(Mult(Efecto::Desterro) + 0.01f) : 0;   // SUR-10: +1 por nivel de la mejora
  nObjetos = zeni = 0, iman = congelado = cegado = 0;
  std::memset(dano_tecnica, 0, sizeof dano_tecnica), std::memset(usos_tecnica, 0, sizeof usos_tecnica);
  std::memset(ultimo_jefe_, 0, sizeof ultimo_jefe_), std::memset(recarga_, 0, sizeof recarga_);
  float rmax = 0.5f;
  for (const int k : Escenario().enemigos) rmax = d.enemigos[size_t(k)].radio > rmax ? d.enemigos[size_t(k)].radio : rmax;
  horda_.Iniciar(Azar() | 1, 0);
  horda_.celda = 2.0f * rmax;
  const DefPersonaje& p = d.personajes[personaje];
  jugador = Jugador{};
  jugador.vida = jugador.vida_max = p.vida, jugador.velocidad = p.velocidad, jugador.recogida = p.recogida;
  jugador.exp_sig = d.exp_base;
  jefe = Jefe{};
  Recalcular();   // mejoras permanentes: vida, velocidad, recogida
  jugador.vida = jugador.vida_max;
  const DefEscenario& e = Escenario();
  rayo_t_ = e.rayo_cada > 0 ? e.rayo_cada * (0.6f + 0.8f * Azar01()) : 0;
  Dar(TipoOpcion::Tecnica, p.tecnica, 1);
  PasoRompibles();
  if (config.jefes_v2) ProgramarRitmo_();
  return true;
}

void Juego::Recalcular() {
  const DefPersonaje& p = datos->personajes[personaje];
  const float vm = p.vida + Mult(Efecto::VidaMax);
  if (vm > jugador.vida_max) jugador.vida += vm - jugador.vida_max;
  jugador.vida_max = vm;
  jugador.velocidad = p.velocidad * (1 + Mult(Efecto::Velocidad) + (forma > 0 ? datos->formas.velocidad : 0.0f));
  jugador.recogida = p.recogida * (1 + Mult(Efecto::Recogida));
}

void Juego::Dar(TipoOpcion tipo, int def, int nivel) {
  Opcion o{tipo, def, nivel};
  if (tipo == TipoOpcion::Pasiva) {
    for (int i = 0; i < nPasivas; ++i) if (pasivas[i].def == def) { pasivas[i].nivel = nivel; Recalcular(); return; }
    if (nPasivas < kMaxPasivas) pasivas[nPasivas++] = {def, nivel}, Recalcular();
    return;
  }
  if (tipo == TipoOpcion::Evolucion) return Aplicar(o);
  for (int i = 0; i < nTecnicas; ++i) if (tecnicas[i].def == def) { tecnicas[i].nivel = nivel; return; }
  if (nTecnicas < kMaxTecnicas) recarga_[nTecnicas] = 0.3f, tecnicas[nTecnicas++] = {def, nivel};
}

void Juego::Aplicar(const Opcion& o) {
  switch (o.tipo) {
    case TipoOpcion::Tecnica:
    case TipoOpcion::Pasiva: Dar(o.tipo, o.def, o.nivel); break;
    case TipoOpcion::Evolucion: {
      const int base = datos->tecnicas[o.def].evoluciona_de;
      for (int i = 0; i < nTecnicas; ++i)
        if (tecnicas[i].def == base) tecnicas[i] = {o.def, kNivelMax}, recarga_[i] = 0.3f;
      Evento_(TipoEvento::Evolucion, o.def, jugador.x, jugador.y);
      Jugo_(4, 0, 0, jugador.x, jugador.y);
      if (config.formas) Transformar_(forma > 0 ? std::min(2, datos->formas.max) : 1);   // A05: evolucionar = transformarse
      break;
    }
    case TipoOpcion::Curar: jugador.vida = std::fmin(jugador.vida_max, jugador.vida + 0.3f * jugador.vida_max); break;
  }
}

bool Juego::EsDesterrada(const Opcion& o) const {
  if (o.def < 0 || o.def >= 64) return false;
  if (o.tipo == TipoOpcion::Pasiva) return (desterr_pas_ >> o.def) & 1;
  if (o.tipo == TipoOpcion::Tecnica) return (desterr_tec_ >> o.def) & 1;
  return false;
}

int Juego::Candidatas_(Opcion* c, int cap, Opcion* evo, int* nevo) const {
  int n = 0, ev = 0;
  const Datos& d = *datos;
  auto pon = [&](const Opcion& o) { if (n < cap && !EsDesterrada(o)) c[n++] = o; };
  // evoluciones listas: siempre salen
  for (int i = 0; i < nTecnicas; ++i)
    if (tecnicas[i].nivel >= kNivelMax && d.tecnicas[tecnicas[i].def].evoluciona_de < 0)
      for (int e = 0; e < int(d.tecnicas.size()); ++e)
        if (d.tecnicas[e].evoluciona_de == tecnicas[i].def && NivelPasiva(d.tecnicas[e].pasiva) > 0 && ev < kOpciones && evo)
          evo[ev++] = {TipoOpcion::Evolucion, e, kNivelMax, Rareza::Legendaria};
  for (int i = 0; i < nTecnicas; ++i)
    if (tecnicas[i].nivel < kNivelMax) pon({TipoOpcion::Tecnica, tecnicas[i].def, tecnicas[i].nivel + 1});
  if (nTecnicas < kMaxTecnicas)
    for (int t = 0; t < int(d.tecnicas.size()); ++t) {
      if (d.tecnicas[t].evoluciona_de >= 0 || NivelTecnica(t) > 0) continue;
      bool evolucionada = false;   // su evolucion ya esta
      for (int e = 0; e < int(d.tecnicas.size()); ++e) if (d.tecnicas[e].evoluciona_de == t && NivelTecnica(e) > 0) evolucionada = true;
      if (!evolucionada) pon({TipoOpcion::Tecnica, t, 1});
    }
  for (int i = 0; i < nPasivas; ++i)
    if (pasivas[i].nivel < kNivelMax) pon({TipoOpcion::Pasiva, pasivas[i].def, pasivas[i].nivel + 1});
  if (nPasivas < kMaxPasivas)
    for (int p = 0; p < int(d.pasivas.size()); ++p)
      if (!NivelPasiva(p)) pon({TipoOpcion::Pasiva, p, 1});
  if (nevo) *nevo = ev;
  return n;
}

Rareza Juego::TirarRareza_() {
  const DefSubida& sb = datos->subida;
  const float suerte = Mult(Efecto::Suerte) * sb.suerte_k;
  float w[kRarezas], tot = 0;
  for (int r = 0; r < kRarezas; ++r) tot += (w[r] = sb.peso[r] * (r ? 1 + suerte * float(r) : 1.0f));
  float a = Azar01() * tot;
  for (int r = 0; r < kRarezas - 1; ++r) {
    if (a < w[r]) return Rareza(r);
    a -= w[r];
  }
  return Rareza::Legendaria;
}

void Juego::Sazonar_(Opcion* o) {
  if (o->tipo != TipoOpcion::Tecnica && o->tipo != TipoOpcion::Pasiva) return;
  o->rareza = TirarRareza_();
  o->nivel = std::min(kNivelMax, o->nivel + datos->subida.niveles[int(o->rareza)]);
}

void Juego::Sortear() {
  Opcion c[64], evo[kOpciones];
  int ev = 0;
  const int n = Candidatas_(c, 64, evo, &ev);
  for (int k = 0; k < ev; ++k) opciones[k] = evo[k];
  nOpciones = ev;
  for (int k = 0; nOpciones < kOpciones && k < n; ++k) {   // Fisher-Yates parcial
    const int j = k + int(Azar() % uint32_t(n - k));
    const Opcion t = c[k];
    c[k] = c[j], c[j] = t;
    opciones[nOpciones++] = c[k];
  }
  if (config.meta_v2) for (int k = ev; k < nOpciones; ++k) Sazonar_(&opciones[k]);
  if (nOpciones == 0) opciones[nOpciones++] = {TipoOpcion::Curar, -1, 1};
}

// ---------------------------------------------------------------- A09: decisiones de la subida
void Juego::SiguienteEleccion_() {
  if (--subidas_ > 0) Sortear();
  else estado = Estado::Jugando, nOpciones = 0;
}

bool Juego::Resortear() {
  if (!PuedeResortear()) return false;
  zeni -= CosteResortear(), ++resorteos;
  nEventos = 0;
  Sortear();
  return true;
}

bool Juego::Saltar() {
  if (!PuedeDecidir_()) return false;
  nEventos = 0;
  const float ms = Mult(Efecto::Salto);   // SUR-10: la mejora Salto: mas zeni (x(1 + 4 por nivel))  y mas vida
  zeni += int(float(datos->subida.saltar_zeni) * (1 + 4 * ms) + 0.5f);
  jugador.vida = std::fmin(jugador.vida_max, jugador.vida + (datos->subida.saltar_vida + ms) * jugador.vida_max);
  SiguienteEleccion_();
  return true;
}

bool Juego::Desterrar(int i) {
  if (!PuedeDecidir_() || desterros_ <= 0 || i < 0 || i >= nOpciones) return false;
  const Opcion& o = opciones[i];
  if ((o.tipo != TipoOpcion::Tecnica && o.tipo != TipoOpcion::Pasiva) || o.def < 0 || o.def >= 64) return false;
  (o.tipo == TipoOpcion::Pasiva ? desterr_pas_ : desterr_tec_) |= 1ULL << o.def;
  --desterros_;
  nEventos = 0;
  Sortear();
  return true;
}

int Juego::OpcionesCofre(int tier) {
  tierCofre = std::clamp(tier, 1, kMaxPremios), nPremios = 0;
  Opcion evo[kOpciones], c[64];
  int ev = 0;
  Candidatas_(c, 0, evo, &ev);
  const auto guardar = [&](const Opcion& o) {
    nivel_previo_premio[nPremios] = o.tipo == TipoOpcion::Pasiva ? NivelPasiva(o.def)
        : o.tipo == TipoOpcion::Curar ? 0 : NivelTecnica(o.def);
    premios[nPremios++] = o;
    Aplicar(o);
  };
  for (int k = 0; k < ev && nPremios < tierCofre; ++k) guardar(evo[k]);
  while (nPremios < tierCofre) {
    const int n = Candidatas_(c, 64, nullptr, nullptr);   // despues de aplicar el anterior: el nivel ya subio
    Opcion o = n > 0 ? c[Azar() % uint32_t(n)] : Opcion{TipoOpcion::Curar, -1, 1};
    Sazonar_(&o);
    guardar(o);
  }
  return nPremios;
}

void Juego::AbrirCofre_(int tier) {
  OpcionesCofre(tier);
  cofre_ = true;
  EventoFijo_(TipoEvento::CofreAbre, nPremios, jugador.x, jugador.y);
}

void Juego::CerrarCofre() {
  if (!cofre_) return;
  cofre_ = false, nPremios = 0, nEventos = 0;
  if (subidas_ > 0) Sortear();
  else estado = Estado::Jugando, nOpciones = 0;
}

void Juego::Elegir(int i) {
  if (estado != Estado::Eligiendo) return;
  if (cofre_) return CerrarCofre();   // A09: en el cofre, "Hecho"
  if (i < 0 || i >= nOpciones) return;
  nEventos = 0;   // los del paso anterior ya se leyeron: sin esto, quien lee los eventos tras Elegir (modo.cpp, el test) los contaba dos veces
  Aplicar(opciones[i]);
  SiguienteEleccion_();
}

int Juego::Aparecer(int hueco, const float* pos, bool mini, int tipo_forzado) {   // hueco de la oleada (0..5) -> enemigo del escenario; pos = donde (si no, el anillo)
  const int i = horda_.n;
  if (i >= Sim::kMax || solo_jefe_) return -1;
  horda_.PonerN(i + 1);
  if (pos) horda_.x[i] = pos[0], horda_.y[i] = pos[1];
  const int tipo = tipo_forzado >= 0 ? tipo_forzado : Escenario().enemigos[hueco];
  const DefEnemigo& e = datos->enemigos[size_t(tipo)];
  const Oleada* o = &datos->oleadas[0];
  for (const Oleada& w : datos->oleadas) if (w.minuto <= Minuto()) o = &w;
  horda_.rad[i] = e.radio, horda_.vel[i] = congelado > 0 ? 0 : e.velocidad;
  tipo_[i] = uint8_t(tipo), vida_[i] = e.vida * o->vida * mvida_, destello_[i] = 0;
  std::memset(ultimo_[i], 0, sizeof ultimo_[i]);
  mini_[i] = 0, cond_est_[i] = 0, cond_t_[i] = 0, escudo_[i] = 0;
  if (mini) {   // A08: minijefe
    mini_[i] = 1, vida_[i] *= datos->ritmo.mini_vida, horda_.rad[i] *= datos->ritmo.mini_escala;
  }
  if (config.ia && e.conducta != Conducta::Persigue) {   // A06: desfase para que no actuen todos a la vez; el elite nace con su escudo
    cond_t_[i] = Azar01() * e.cond_cada;
    if (e.conducta == Conducta::Elite) escudo_[i] = vida_[i] * e.cond_escudo;
  }
  return i;
}

void Juego::Dano(int i, float d, int def, float empuje, float ox, float oy) {
  if (vida_[i] <= 0) return;
  if (config.ia && d < 1e8f) {   // A06: conductas que cambian el golpe recibido
    const DefEnemigo& de = datos->enemigos[tipo_[i]];
    if (de.conducta == Conducta::Frontal) {   // escudo de frente: lo que llega desde el lado del jugador pierde casi todo
      const float fx = jugador.x - horda_.x[i], fy = jugador.y - horda_.y[i], sx = ox - horda_.x[i], sy = oy - horda_.y[i];
      const float lf = std::sqrt(fx * fx + fy * fy), ls = std::sqrt(sx * sx + sy * sy);
      if (lf > 1e-3f && ls > 0.3f && fx * sx + fy * sy > 0.5f * lf * ls) d *= 1 - de.cond_escudo;
    } else if (de.conducta == Conducta::Absorbe) {   // lo que se le pega cura a los que tiene cerca (hasta la vida base de cada uno)
      const float cura = d * de.cond_escudo;
      EnCirculo(horda_.x[i], horda_.y[i], de.cond_radio, [&](int k) {
        const float tope = datos->enemigos[tipo_[k]].vida * mvida_;
        if (k != i && vida_[k] > 0 && vida_[k] < tope) vida_[k] = std::fmin(tope, vida_[k] + cura);
      });
    }
  }
  if (config.ia && escudo_[i] > 0 && d < 1e8f) {   // A06: el escudo del elite se come el dano primero
    const float a = std::fmin(d, escudo_[i]);
    escudo_[i] -= a, d -= a, destello_[i] = 1;
    if (def >= 0) dano_tecnica[def] += a;
    Evento_(TipoEvento::Golpe, def, horda_.x[i], horda_.y[i], a);
    if (d <= 0) return;
  }
  if (def >= 0) dano_tecnica[def] += d < vida_[i] ? d : vida_[i];   // def -1 = bomba (no es de ninguna tecnica)
  vida_[i] -= d, destello_[i] = 1;
  Evento_(TipoEvento::Golpe, def, horda_.x[i], horda_.y[i], d);
  if (config.jugo) {   // A02: retroceso corto de todo golpe (sin empuje propio) y pausa en el golpe fuerte
    if (empuje <= 0 && def >= 0) empuje = 0.12f;
    if (d >= kGolpeFuerte) Jugo_(3, 0, 0, horda_.x[i], horda_.y[i], false);
  }
  if (empuje > 0) {
    const float vx = horda_.x[i] - ox, vy = horda_.y[i] - oy, l = std::sqrt(vx * vx + vy * vy);
    if (l > 1e-4f) horda_.x[i] += vx / l * empuje, horda_.y[i] += vy / l * empuje;
  }
  if (vida_[i] <= 0) {
    ++derrotados;
    ++racha, racha_t = 0, racha_max = std::max(racha_max, racha);
    if (racha == 10 || racha == 25 || (racha >= 50 && racha % 50 == 0)) Evento_(TipoEvento::Racha, racha, jugador.x, jugador.y);
    const DefEnemigo& e = datos->enemigos[tipo_[i]];
    Evento_(TipoEvento::Muerte, tipo_[i], horda_.x[i], horda_.y[i]);
    if (e.anillo > 0) Jugo_(3, 2, 0.4f, horda_.x[i], horda_.y[i]);   // A02: muerte de elite
    if (nGemas < kMaxGemas) gx[nGemas] = horda_.x[i], gy[nGemas] = horda_.y[i], gvalor[nGemas++] = e.exp;
    else gvalor[pasos % kMaxGemas] += e.exp;   // ponytail: sin sitio se suma a una gema que ya existe
    Soltar(horda_.x[i] + 0.3f, horda_.y[i], mini_[i] ? 1.0f : e.cofre);   // al lado de la gema (el minijefe, cofre seguro)
  }
}

void Juego::DanoJefe(float d, int def) {
  if (jefe.estado == EstadoJefe::Ausente || jefe.estado == EstadoJefe::Muerto || jefe.estado == EstadoJefe::Transforma) return;
  if (jefe.escudo > 0) d *= 0.3f;   // A07: Energy Field
  if (def >= 0) {   // def -1 = bomba: sin tick ni cuenta por tecnica
    const uint32_t tick = uint32_t(datos->tecnicas[def].tick * 60.0f);
    if (ultimo_jefe_[def] && uint32_t(pasos) - ultimo_jefe_[def] < tick) return;
    ultimo_jefe_[def] = uint32_t(pasos);
  }
  const float vida_antes = jefe.vida;
  jefe.vida -= d, jefe.destello = 1;
  if (KitJefe_() && jefe.fase < NFases()) jefe.vida = std::fmax(jefe.vida, jefe.vida_max * UmbralFase_(jefe.fase));   // SUR-8: un golpe grande no se salta una fase
  if (def >= 0) dano_tecnica[def] += std::max(0.0f, vida_antes - std::max(0.0f, jefe.vida));   // DS1/03: solo el dano que entra tras el tope de fase
  Evento_(TipoEvento::Golpe, def, jefe.x, jefe.y, d);
  if (config.jugo && d >= kGolpeFuerte) Jugo_(3, 0, 0, jefe.x, jefe.y, false);
  if (jefe.vida <= 0) {
    jefe.estado = EstadoJefe::Muerto, ++jefes_derrotados;
    Evento_(TipoEvento::JefeMuere, 0, jefe.x, jefe.y);
    pausa_cd_ = 0, Jugo_(6, 3, 1.0f, jefe.x, jefe.y);   // A02: la caida del jefe manda sobre cualquier pausa en curso
    Poner(jefe.x, jefe.y, TipoObjeto::Cofre, 5);   // su cofre: hay victoria_tras segundos para cogerlo
    victoria_ = datos->recogibles.victoria_tras;
    if (victoria_ <= 0) estado = Estado::Victoria;
  }
}

bool Juego::JefeEn(float x, float y, float r) const {
  if (jefe.estado == EstadoJefe::Ausente || jefe.estado == EstadoJefe::Muerto) return false;
  const float vx = jefe.x - x, vy = jefe.y - y, rr = r + DJ().radio;
  return vx * vx + vy * vy < rr * rr;
}

int Juego::Cercano(float x, float y, float rmax) const {   // ponytail: recorrido lineal (600 enemigos ~ 1 us); rejilla si hiciera falta
  int mejor = -1;
  float m = rmax * rmax;
  for (int i = 0; i < horda_.n; ++i) {
    if (vida_[i] <= 0) continue;
    const float vx = horda_.x[i] - x, vy = horda_.y[i] - y, d2 = vx * vx + vy * vy;
    if (d2 < m) m = d2, mejor = i;
  }
  return mejor;
}

template <class F>
void Juego::EnCirculo(float x, float y, float r, F&& f) {
  horda_.Recorrer(x - r, y - r, x + r, y + r, [&](int i) {
    if (vida_[i] <= 0) return;
    const float vx = horda_.x[i] - x, vy = horda_.y[i] - y, rr = r + horda_.rad[i];
    if (vx * vx + vy * vy < rr * rr) f(i);
  });
}

template <class F>
void Juego::EnSegmento(float x0, float y0, float x1, float y1, float ancho, F&& f) {
  const float h = ancho * 0.5f, sx = x1 - x0, sy = y1 - y0, l2 = sx * sx + sy * sy + 1e-6f;
  horda_.Recorrer(std::fmin(x0, x1) - h, std::fmin(y0, y1) - h, std::fmax(x0, x1) + h, std::fmax(y0, y1) + h, [&](int i) {
    if (vida_[i] <= 0) return;
    float t = ((horda_.x[i] - x0) * sx + (horda_.y[i] - y0) * sy) / l2;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const float vx = horda_.x[i] - (x0 + sx * t), vy = horda_.y[i] - (y0 + sy * t), rr = h + horda_.rad[i];
    if (vx * vx + vy * vy < rr * rr) f(i);
  });
}

// Lanza la tecnica del hueco 'slot' (cuando se acaba su recarga)
void Juego::Disparar(int slot) {
  const int k = tecnicas[slot].def, L = tecnicas[slot].nivel - 1;
  const DefTecnica& d = datos->tecnicas[k];
  const float dano = d.dano[L] * (1 + Mult(Efecto::Dano)), area = d.area[L] * (1 + Mult(Efecto::Area));
  const int cant = d.cantidad[L] + int(Mult(Efecto::Cantidad));   // C04
  const float mdur = 1 + Mult(Efecto::Duracion);
  const float px = jugador.x, py = jugador.y;
  float mx = jugador.dx, my = jugador.dy;   // rayo y cono apuntan al mas cercano a su alcance (si no hay, hacia donde mira)
  ++usos_tecnica[k];
  auto efecto = [&](Forma f, float vida) -> Efecto2D* {
    Efecto2D* e = NuevoEfecto();
    if (!e) return nullptr;
    e->forma = f, e->def = uint8_t(k), e->color = d.color, e->x = px, e->y = py, e->vida = e->vida0 = vida * mdur;
    e->dano = dano, e->radio = area, e->empuje = d.empuje, e->perfora = d.perfora;
    return e;
  };
  // SUR-8 autoapuntado (config.autoapuntar): el jefe si esta a su alcance (a la vista), si no el enemigo mas cercano, si no hacia donde mira.
  // Apagado: siempre hacia donde mira. El movimiento nunca cambia.
  const bool jefe_vivo = jefe.estado != EstadoJefe::Ausente && jefe.estado != EstadoJefe::Muerto && jefe.estado != EstadoJefe::Transforma;
  auto objetivo = [&](float rmax, float* tx, float* ty) -> bool {   // posicion del objetivo; falso si no hay
    if (!config.autoapuntar) return false;
    if (jefe_vivo && JefeEn(px, py, rmax)) { *tx = jefe.x, *ty = jefe.y; return true; }
    const int c = Cercano(px, py, rmax);
    if (c < 0) return false;
    *tx = horda_.x[c], *ty = horda_.y[c];
    return true;
  };
  auto hacia = [&](float rmax, float* dx, float* dy) {   // direccion al objetivo (o hacia donde mira)
    float tx, ty;
    *dx = jugador.dx, *dy = jugador.dy;
    if (!objetivo(rmax, &tx, &ty)) return;
    const float vx = tx - px, vy = ty - py, l = std::sqrt(vx * vx + vy * vy);
    if (l > 1e-4f) *dx = vx / l, *dy = vy / l;
  };
  auto bola = [&](float dx, float dy, int objetivo) {
    if (Efecto2D* e = efecto(Forma::Bola, d.duracion)) e->vx = dx * d.velocidad, e->vy = dy * d.velocidad, e->objetivo = objetivo;
  };
  {   // para el dibujo (pose y giro del lanzador): solo lee, no toca el azar ni el estado
    float ex = mx, ey = my;
    hacia(d.arq == Arquetipo::Cono ? area + 1 : d.alcance, &ex, &ey);
    Evento_(TipoEvento::Tecnica, k, ex, ey);
  }

  switch (d.arq) {
    case Arquetipo::Rayo:   // sigue al jugador mientras dura (MoverEfectos), golpea cada tick
      hacia(d.alcance, &mx, &my);
      for (int c = 0; c < cant; ++c)
        if (Efecto2D* e = efecto(Forma::Rayo, d.duracion)) {
          const float s = c % 2 ? -1.0f : 1.0f;
          e->ancho = area, e->radio = d.alcance, e->ang = std::atan2(my * s, mx * s);
        }
      break;
    case Arquetipo::Orbita:
      for (int c = 0; c < cant; ++c)
        if (Efecto2D* e = efecto(Forma::Disco, d.duracion)) e->vy = 2 * kPi * float(c) / float(cant), e->radio = area;   // vy = desfase en la orbita
      break;
    case Arquetipo::Teledirigida:
      for (int c = 0; c < cant; ++c) {
        float dx, dy;
        hacia(d.alcance, &dx, &dy);
        const float a = std::atan2(dy, dx) + (float(c) - float(cant - 1) * 0.5f) * 0.5f;
        bola(std::cos(a), std::sin(a), config.autoapuntar ? Cercano(px, py, d.alcance) : -1);
      }
      break;
    case Arquetipo::Estallido: {
      float ox, oy;
      const bool apunta = objetivo(d.alcance, &ox, &oy);   // SUR-8: con objetivo, una de las bolas va hacia el; si no, el giro al azar de siempre
      const float a0 = apunta ? std::atan2(oy - py, ox - px) : Azar01() * 2 * kPi;
      for (int c = 0; c < cant; ++c) {
        const float a = a0 + 2 * kPi * float(c) / float(cant);
        bola(std::cos(a), std::sin(a), -1);
      }
      break;
    }
    case Arquetipo::Rafaga: {
      float dx, dy;
      hacia(d.alcance, &dx, &dy);
      const float a = std::atan2(dy, dx);
      for (int c = 0; c < cant; ++c) {
        const float b = a + (Azar01() - 0.5f) * 0.25f;
        bola(std::cos(b), std::sin(b), -1);
      }
      break;
    }
    case Arquetipo::Aura:
    case Arquetipo::Barrera: {   // efecto que dura justo hasta el siguiente: se ve continuo
      const float rec = d.recarga[L] * std::fmax(0.4f, 1 - Mult(Efecto::Recarga));
      if (Efecto2D* e = efecto(d.arq == Arquetipo::Aura ? Forma::Aura : Forma::Anillo, rec + kDt)) e->golpea = false, e->ancho = 0.15f;
      EnCirculo(px, py, area, [&](int i) {
        Dano(i, dano, k, 0, px, py);
        if (d.arq == Arquetipo::Barrera) {   // fuera del anillo
          const float vx = horda_.x[i] - px, vy = horda_.y[i] - py, l = std::sqrt(vx * vx + vy * vy) + 1e-4f;
          const float r = area + horda_.rad[i];
          horda_.x[i] = px + vx / l * r, horda_.y[i] = py + vy / l * r;
        }
      });
      if (JefeEn(px, py, area)) DanoJefe(dano, k);
      break;
    }
    case Arquetipo::RayoFino: {
      float dx, dy;
      hacia(d.alcance, &dx, &dy);
      const float a0 = std::atan2(dy, dx);
      for (int c = 0; c < cant; ++c) {
        const float a = a0 + 2 * kPi * float(c) / float(cant);
        const float ex = px + std::cos(a) * d.alcance, ey = py + std::sin(a) * d.alcance;
        if (Efecto2D* e = efecto(Forma::Rayo, d.duracion)) e->golpea = false, e->x2 = ex, e->y2 = ey, e->ancho = area;
        EnSegmento(px, py, ex, ey, area, [&](int i) { Dano(i, dano, k, 0, px, py); });
        // jefe: distancia de su centro al segmento
        if (jefe.estado != EstadoJefe::Ausente && jefe.estado != EstadoJefe::Muerto) {
          const float sx = ex - px, sy = ey - py;
          float t = ((jefe.x - px) * sx + (jefe.y - py) * sy) / (sx * sx + sy * sy);
          t = t < 0 ? 0 : t > 1 ? 1 : t;
          if (JefeEn(px + sx * t, py + sy * t, area * 0.5f)) DanoJefe(dano, k);
        }
      }
      break;
    }
    case Arquetipo::Cono:
      hacia(area + 1, &mx, &my);
      for (int c = 0; c < cant; ++c) {
        const float s = c % 2 ? -1.0f : 1.0f, ang = std::atan2(my * s, mx * s), cx = std::cos(ang), cy = std::sin(ang);
        if (Efecto2D* e = efecto(Forma::Cono, d.duracion)) e->golpea = false, e->ang = ang, e->apertura = kApertura;
        const float cosA = std::cos(kApertura);
        EnCirculo(px, py, area, [&](int i) {
          const float vx = horda_.x[i] - px, vy = horda_.y[i] - py, l = std::sqrt(vx * vx + vy * vy);
          if (l < horda_.rad[i] || (vx * cx + vy * cy) >= cosA * l) Dano(i, dano, k, d.empuje, px, py);
        });
        if (JefeEn(px, py, area)) {
          const float vx = jefe.x - px, vy = jefe.y - py, l = std::sqrt(vx * vx + vy * vy);
          if (l < DJ().radio || (vx * cx + vy * cy) >= cosA * l) DanoJefe(dano, k);
        }
      }
      break;
    case Arquetipo::Explosion:
      for (int c = 0; c < cant; ++c) {
        float ex = px + mx * 3, ey = py + my * 3;   // nadie cerca: delante
        float ox, oy;
        if (c == 0 && objetivo(d.alcance, &ox, &oy)) ex = ox, ey = oy;   // SUR-8: la primera, sobre el objetivo (jefe antes que horda)
        else if (config.autoapuntar && horda_.n > 0)
          for (int intento = 0; intento < 8; ++intento) {
            const int i = int(Azar() % uint32_t(horda_.n));
            const float vx = horda_.x[i] - px, vy = horda_.y[i] - py;
            if (vida_[i] > 0 && vx * vx + vy * vy < d.alcance * d.alcance) { ex = horda_.x[i], ey = horda_.y[i]; break; }
          }
        if (Efecto2D* e = efecto(Forma::Explosion, d.duracion)) e->golpea = false, e->x = ex, e->y = ey;
        if (d.id == "genkidama")   // SUR-01: la esfera de energia de B3 (la misma que Spirit Bomb/Supernova de los jefes) cae sobre el objetivo
          if (Efecto2D* e = efecto(Forma::Sombra, 0.9f)) e->golpea = false, e->x = ex, e->y = ey, e->color = 0x66DDFF;
        EnCirculo(ex, ey, area, [&](int i) { Dano(i, dano, k, d.empuje, ex, ey); });
        if (JefeEn(ex, ey, area)) DanoJefe(dano, k);
      }
      break;
    case Arquetipo::Bumeran:
      for (int c = 0; c < cant; ++c) {
        float dx, dy;
        hacia(d.alcance, &dx, &dy);
        const float a = std::atan2(dy, dx) + float(c) * 2 * kPi / float(cant);
        if (Efecto2D* e = efecto(Forma::Disco, d.duracion)) e->vx = std::cos(a) * d.velocidad, e->vy = std::sin(a) * d.velocidad;
      }
      break;
    case Arquetipo::Onda:
      if (Efecto2D* e = efecto(Forma::Anillo, d.duracion)) e->radio = 0, e->x2 = area, e->ancho = 0.6f;   // x2 = radio final
      break;
  }
}

void Juego::MoverEfectos() {
  const float px = jugador.x, py = jugador.y;
  for (int n = 0; n < nEfectos; ++n) {
    Efecto2D& e = efectos[n];
    e.vida -= kDt, e.t += kDt;
    if (e.hostil) {   // bala del jefe o aviso
      if (e.forma == Forma::Bola) {
        e.x += e.vx * kDt, e.y += e.vy * kDt;
        const float vx = e.x - px, vy = e.y - py, rr = e.radio + Sim::kRadioJugador;
        if (vx * vx + vy * vy < rr * rr) DanoJugador(e.dano), e.vida = 0;
      } else if (e.forma == Forma::Disco) {   // A07: Sauzer Blade: va hacia el jugador y a media vida vuelve al jefe; no desaparece al dar
        if (e.t > e.vida0 * 0.45f) {
          const float bx = jefe.x - e.x, by = jefe.y - e.y, bl = std::sqrt(bx * bx + by * by) + 1e-4f;
          e.vx = bx / bl * e.empuje, e.vy = by / bl * e.empuje;
          if (bl < 0.9f) e.vida = 0;
        }
        e.x += e.vx * kDt, e.y += e.vy * kDt, e.ang += 14 * kDt;
        const float vx = e.x - px, vy = e.y - py, rr = e.radio * 2.2f + Sim::kRadioJugador;   // el dibujo del disco es 2,2 veces su radio
        if (vx * vx + vy * vy < rr * rr) DanoJugador(e.dano);
      } else if ((e.forma == Forma::Sombra || e.forma == Forma::AvisoRayo) && e.vida <= 0) {
        Impacto(e);   // ojo: puede anadir efectos al final de la lista (los recorre este mismo bucle)
      }
      continue;
    }
    if (!e.golpea || e.vida <= 0) continue;
    const int k = e.def;
    const DefTecnica& d = datos->tecnicas[k];
    const uint32_t tick = uint32_t(d.tick * 60.0f);
    auto pegar = [&](int i) -> bool {
      if (ultimo_[i][k] && uint32_t(pasos) - ultimo_[i][k] < tick) return false;
      ultimo_[i][k] = uint32_t(pasos);
      Dano(i, e.dano, k, e.empuje, e.x, e.y);
      return true;
    };
    switch (d.arq) {
      case Arquetipo::Rayo: {   // pegado al jugador
        e.x = px, e.y = py, e.x2 = px + std::cos(e.ang) * e.radio, e.y2 = py + std::sin(e.ang) * e.radio;
        EnSegmento(e.x, e.y, e.x2, e.y2, e.ancho, pegar);
        const float sx = e.x2 - e.x, sy = e.y2 - e.y;
        float t = ((jefe.x - e.x) * sx + (jefe.y - e.y) * sy) / (sx * sx + sy * sy + 1e-6f);
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        if (JefeEn(e.x + sx * t, e.y + sy * t, e.ancho * 0.5f)) DanoJefe(e.dano, k);
        break;
      }
      default: break;
    }
    if (d.arq == Arquetipo::Orbita) {
      const float a = e.vy + e.t * d.velocidad;
      e.x = px + std::cos(a) * d.alcance, e.y = py + std::sin(a) * d.alcance, e.ang = a;
      EnCirculo(e.x, e.y, e.radio, pegar);
      if (JefeEn(e.x, e.y, e.radio)) DanoJefe(e.dano, k);
    } else if (d.arq == Arquetipo::Bumeran) {
      if (e.t > e.vida0 * 0.5f) {   // vuelve hacia el jugador (a media vida: con la pasiva Duracion llega mas lejos)
        const float vx = px - e.x, vy = py - e.y, l = std::sqrt(vx * vx + vy * vy) + 1e-4f;
        e.vx = vx / l * d.velocidad, e.vy = vy / l * d.velocidad;
        if (l < 0.6f) e.vida = 0;
      }
      e.x += e.vx * kDt, e.y += e.vy * kDt, e.ang += 12 * kDt;
      EnCirculo(e.x, e.y, e.radio, pegar);
      if (JefeEn(e.x, e.y, e.radio)) DanoJefe(e.dano, k);
    } else if (d.arq == Arquetipo::Onda) {
      const float r = e.x2 * (1 - e.Vida01());
      e.radio = r;
      EnCirculo(px, py, r + 0.3f, [&](int i) {
        const float vx = horda_.x[i] - px, vy = horda_.y[i] - py;
        if (vx * vx + vy * vy > (r - 0.8f) * (r - 0.8f) || r < 0.8f) pegar(i);
      });
      e.x = px, e.y = py;
      if (JefeEn(px, py, r + 0.3f)) DanoJefe(e.dano, k);
    } else if (e.forma == Forma::Bola) {
      if (e.objetivo >= 0 && e.objetivo < horda_.n && vida_[e.objetivo] > 0) {   // teledirigida: gira hacia su objetivo
        const float vx = horda_.x[e.objetivo] - e.x, vy = horda_.y[e.objetivo] - e.y, l = std::sqrt(vx * vx + vy * vy) + 1e-4f;
        e.vx = vx / l * d.velocidad, e.vy = vy / l * d.velocidad;
      }
      e.x += e.vx * kDt, e.y += e.vy * kDt;
      int golpes = 0;
      EnCirculo(e.x, e.y, e.radio, [&](int i) {
        if (e.vida <= 0) return;
        if (pegar(i)) ++golpes, e.vida = (e.perfora >= 0 && golpes > e.perfora) ? 0 : e.vida;
      });
      if (e.vida > 0 && JefeEn(e.x, e.y, e.radio)) {
        DanoJefe(e.dano, k);
        if (e.perfora >= 0) e.vida = 0;
      }
      if (golpes && e.perfora > 0) e.perfora -= golpes;
    }
  }
  int m = 0;   // fuera los acabados (conservando el orden)
  for (int n = 0; n < nEfectos; ++n)
    if (efectos[n].vida > 0) efectos[m++] = efectos[n];
  nEfectos = m;
}

// ---------------------------------------------------------------- A07: kit real de cada jefe y 2 fases (solo con Config::jefes_v2)
// Las tecnicas salen de las capsulas #SKC de B3 (carta de cada una: Frieza 100 Death Beam, 101 Death Wave, 102 Death Ball; Cell 117 Kamehameha, 118
// Energy Field, 119 Spirit Bomb; Super Buu 124 Ill Flash, 125 Ill Ball Attack; Cooler 134 Destructive Ray, 135 Sauzer Blade, 136 Supernova). Las
// definitivas ("Unusable without Perfect Form / Final Form") y el Energy Field de Cell (necesita haber absorbido al #17) solo salen en la 2a fase.
namespace {
struct DatosKit {
  AtaqueJefe id;
  const char* nombre;
  float aviso, dur, dano, ancho, vel;   // s de aviso, s del rayo, dano base, ancho del rayo / radio, velocidad del proyectil / s que tarda en caer
  uint32_t color;
};
constexpr DatosKit kKits[] = {
    {AtaqueJefe::Ninguno, "ninguno", 0, 0, 0, 0, 0, 0xFFFFFF},
    {AtaqueJefe::Carga, "carga", 1.2f, 0, 0, 0, 0, 0xFF3030},
    {AtaqueJefe::Kamehameha, "kamehameha", 1.3f, 0.5f, 28, 2.0f, 0, 0x3D76FF},
    {AtaqueJefe::CampoEnergia, "energy_field", 2.4f, 0, 26, 5.5f, 0, 0xFFE060},
    {AtaqueJefe::BombaEspiritual, "spirit_bomb", 1.0f, 0, 45, 5.5f, 3.0f, 0x70C0FF},
    {AtaqueJefe::RayoMortal, "death_beam", 0.6f, 0.25f, 16, 0.6f, 0, 0xFF58EA},
    {AtaqueJefe::OndaMortal, "death_wave", 1.1f, 0.5f, 24, 3.4f, 0, 0xFF58EA},
    {AtaqueJefe::BolaMuerte, "death_ball", 1.2f, 0, 40, 1.6f, 3.4f, 0x9040FF},
    {AtaqueJefe::IllFlash, "ill_flash", 0.9f, 0.4f, 20, 1.7f, 0, 0xFF80D0},
    {AtaqueJefe::IllBola, "ill_ball", 0.9f, 0, 18, 0.9f, 6.0f, 0xFF80D0},
    {AtaqueJefe::RayoDestructor, "destructive_ray", 1.0f, 0.4f, 22, 1.6f, 0, 0xFF58EA},
    {AtaqueJefe::DiscoSauzer, "sauzer_blade", 0.8f, 0, 18, 0.6f, 9.0f, 0xB070FF},
    {AtaqueJefe::Supernova, "supernova", 1.5f, 0, 55, 7.5f, 3.6f, 0xFF5020},
    // SUR-04: movimientos de combo (no son capsulas de B3). Taiyoken: ciega a quien este dentro de 'ancho' (huir en el aviso); Cell Jr: invoca 'dano' crias;
    // Rocas: 'dano' = golpe de cada roca, caen en una zona de 'ancho' de radio; Absorcion: tira del jugador hacia el jefe 'dur' s a 'vel' u/s
    {AtaqueJefe::Taiyoken, "taiyoken", 1.1f, 0, 0, 7.0f, 0, 0xFFFFE0},
    {AtaqueJefe::InvocaCellJr, "cell_jr", 1.0f, 0, 4, 2.2f, 0, 0x60FF60},
    {AtaqueJefe::Rocas, "rocas", 1.4f, 0, 14, 5.5f, 1.0f, 0xB0B0FF},
    {AtaqueJefe::Absorcion, "absorcion", 1.2f, 1.8f, 24, 12.0f, 3.6f, 0xFF80D0},
};
const DatosKit& DatosAt(AtaqueJefe a) { return kKits[size_t(a) < sizeof kKits / sizeof kKits[0] ? size_t(a) : 0]; }
constexpr float kLargoRayo = 26.0f;
// ataques por jefe: fase 1 (3 huecos, los repetidos pesan mas), fase 2 (3 huecos) y la definitiva (sale cada 3er ataque de la fase 2)
struct ListaKit { AtaqueJefe f1[3], f2[3], ult; };
constexpr ListaKit kListas[] = {
    {{}, {}, AtaqueJefe::Ninguno},
    {{AtaqueJefe::Carga, AtaqueJefe::Kamehameha, AtaqueJefe::InvocaCellJr}, {AtaqueJefe::Taiyoken, AtaqueJefe::Kamehameha, AtaqueJefe::CampoEnergia}, AtaqueJefe::BombaEspiritual},
    {{AtaqueJefe::Carga, AtaqueJefe::RayoMortal, AtaqueJefe::Rocas}, {AtaqueJefe::Rocas, AtaqueJefe::RayoMortal, AtaqueJefe::OndaMortal}, AtaqueJefe::BolaMuerte},
    {{AtaqueJefe::Carga, AtaqueJefe::IllFlash, AtaqueJefe::IllFlash}, {AtaqueJefe::IllBola, AtaqueJefe::IllFlash, AtaqueJefe::IllBola}, AtaqueJefe::Absorcion},
    {{AtaqueJefe::Carga, AtaqueJefe::RayoDestructor, AtaqueJefe::DiscoSauzer}, {AtaqueJefe::Carga, AtaqueJefe::RayoDestructor, AtaqueJefe::DiscoSauzer}, AtaqueJefe::Supernova},
};
float DistSegmento(float px, float py, float ax, float ay, float bx, float by) {
  const float sx = bx - ax, sy = by - ay;
  float t = ((px - ax) * sx + (py - ay) * sy) / (sx * sx + sy * sy + 1e-6f);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const float cx = ax + sx * t - px, cy = ay + sy * t - py;
  return std::sqrt(cx * cx + cy * cy);
}
}  // namespace

const char* NombreAtaque(AtaqueJefe a) { return DatosAt(a).nombre; }

int CartaJefe(AtaqueJefe a) {
  // R8: data_usi[4], fila 0x20 + 40*ID. Carga y eventos propios no son cartas.
  switch (a) {
    case AtaqueJefe::Kamehameha: return 117;
    case AtaqueJefe::CampoEnergia: return 118;
    case AtaqueJefe::BombaEspiritual: return 119;
    case AtaqueJefe::RayoMortal: return 100;
    case AtaqueJefe::OndaMortal: return 101;
    case AtaqueJefe::BolaMuerte: return 102;
    case AtaqueJefe::IllFlash: return 124;
    case AtaqueJefe::IllBola: return 125;
    case AtaqueJefe::RayoDestructor: return 134;
    case AtaqueJefe::DiscoSauzer: return 135;
    case AtaqueJefe::Supernova: return 136;
    default: return -1;
  }
}

int IndiceFormaJefe(const DefJefe& d, int fase, bool dos_fases) {
  const int n = int(d.fases_fid.size());
  if (n == 0) return -1;
  return dos_fases ? (fase >= 2 ? n - 1 : 0) : std::clamp(fase - 1, 0, n - 1);
}

int Juego::NFases() const {
  if (!KitJefe_()) return 1;
  return !config.jefes_dos_fases && DJ().fases_fid.size() >= 3 ? 3 : 2;
}
float Juego::UmbralFase_(int fase) const { return NFases() == 2 ? 0.5f : fase <= 1 ? 0.66f : 0.33f; }

int Juego::KitJefe_() const {
  if (!config.jefes_v2) return 0;
  const std::string& id = DJ().id;
  if (id == "cell" || id == "cell_perfecto") return 1;
  if (id == "freezer") return 2;
  if (id == "superbuu") return 3;
  if (id == "cooler") return 4;
  return 0;
}

AtaqueJefe Juego::ElegirAtaque_(int kit) {
  const ListaKit& l = kListas[kit];
  if (ataque_prueba != AtaqueJefe::Ninguno) return ataque_prueba;
  if (jefe.fase >= std::max(2, NFases()) && l.ult != AtaqueJefe::Ninguno && jefe.n_ataques % 3 == 2) return l.ult;
  return (jefe.fase >= 2 ? l.f2 : l.f1)[Azar() % 3u];
}

void Juego::RayosKit_(AtaqueJefe a, float* ang, int* n) const {
  const float base = std::atan2(jefe.dy, jefe.dx);
  const bool p2 = jefe.fase >= 2;
  *n = 1, ang[0] = base;
  if (a == AtaqueJefe::RayoMortal && p2) *n = 3, ang[0] = base - 0.3f, ang[1] = base, ang[2] = base + 0.3f;
  else if ((a == AtaqueJefe::RayoDestructor || a == AtaqueJefe::IllFlash) && p2) *n = 2, ang[0] = base - 0.2f, ang[1] = base + 0.2f;
  else if (a == AtaqueJefe::IllBola) *n = 3, ang[0] = base - 0.35f, ang[1] = base, ang[2] = base + 0.35f;
}

void Juego::IniciarKit_(AtaqueJefe a, float dano) {
  const DatosKit& k = DatosAt(a);
  jefe.estado = EstadoJefe::AvisoKit, jefe.ataque = a, jefe.t = 0;
  auto marca = [&](Forma f) -> Efecto2D* {
    Efecto2D* e = NuevoEfecto();
    if (e) e->forma = f, e->hostil = true, e->golpea = false, e->def = 255, e->color = 0xFF3030, e->x = jefe.x, e->y = jefe.y, e->vida = e->vida0 = k.aviso;
    return e;
  };
  switch (a) {
    case AtaqueJefe::CampoEnergia:   // el escudo es el aviso: la onda sale al acabar
      jefe.escudo = 1;
      if (Efecto2D* e = marca(Forma::AvisoAnillo)) e->radio = k.ancho;
      if (Efecto2D* e = marca(Forma::Aura)) e->color = k.color, e->radio = 2.2f;
      break;
    case AtaqueJefe::Taiyoken:   // anillo de la zona que ciega: hay que salir de el durante el aviso (SUR-04: blanco amarillento, el del destello)
      if (Efecto2D* e = marca(Forma::AvisoAnillo)) e->radio = k.ancho, e->color = k.color;
      break;
    case AtaqueJefe::InvocaCellJr:   // aura verde mientras las crias se forman
      if (Efecto2D* e = marca(Forma::Aura)) e->color = k.color, e->radio = k.ancho;
      break;
    case AtaqueJefe::Absorcion:   // aura rosa y el anillo de hasta donde llega el tiron
      if (Efecto2D* e = marca(Forma::AvisoAnillo)) e->radio = k.ancho;
      if (Efecto2D* e = marca(Forma::Aura)) e->color = k.color, e->radio = 2.4f;
      break;
    case AtaqueJefe::Rocas: {   // zona y una sombra por roca alrededor del jugador; caen escalonadas despues del aviso
      if (Efecto2D* e = marca(Forma::AvisoZona)) e->x = jugador.x, e->y = jugador.y, e->x2 = k.ancho, e->y2 = k.ancho, e->vida = e->vida0 = k.aviso + 0.12f * 5;
      for (int b = 0; b < 6; ++b)
        if (Efecto2D* e = marca(Forma::Sombra)) {
          e->x = b == 0 ? jugador.x : jugador.x + (Azar01() * 2 - 1) * k.ancho, e->y = b == 0 ? jugador.y : jugador.y + (Azar01() * 2 - 1) * k.ancho;
          e->color = k.color, e->radio = k.vel, e->dano = dano, e->vida = e->vida0 = k.aviso + 0.12f * float(b);
        }
      break;
    }
    case AtaqueJefe::BombaEspiritual:
    case AtaqueJefe::Supernova:   // la esfera cae sobre donde esta el jugador ahora (la sombra crece hasta el impacto)
      if (Efecto2D* e = marca(Forma::Sombra))
        e->color = k.color, e->x = jugador.x, e->y = jugador.y, e->radio = k.ancho, e->dano = dano, e->vida = e->vida0 = k.vel;
      break;
    default: {   // rayos, bolas y disco: franja de aviso por cada direccion (la bola y el disco, fina)
      float ang[3];
      int n;
      RayosKit_(a, ang, &n);
      for (int r = 0; r < n; ++r)
        if (Efecto2D* e = marca(Forma::AvisoLinea)) {
          e->x2 = jefe.x + std::cos(ang[r]) * kLargoRayo, e->y2 = jefe.y + std::sin(ang[r]) * kLargoRayo, e->radio = 4;
          e->ancho = k.dur > 0 || a == AtaqueJefe::BolaMuerte ? k.ancho : 0.5f;
        }
      break;
    }
  }
  Evento_(TipoEvento::JefeAtaque, int(a), jefe.x, jefe.y, k.aviso);
  Evento_(TipoEvento::AvisoJefe, 2, jefe.x, jefe.y);
}

void Juego::DispararKit_(AtaqueJefe a, float dano) {
  const DatosKit& k = DatosAt(a);
  auto nuevo = [&](Forma f, float vida) -> Efecto2D* {
    Efecto2D* e = NuevoEfecto();
    if (e) e->forma = f, e->hostil = true, e->def = 255, e->color = k.color, e->x = jefe.x, e->y = jefe.y, e->vida = e->vida0 = vida;
    return e;
  };
  const float vx = jugador.x - jefe.x, vy = jugador.y - jefe.y, l = std::sqrt(vx * vx + vy * vy) + 1e-4f;
  float ang[3];
  int n = 1;
  switch (a) {
    case AtaqueJefe::Kamehameha: case AtaqueJefe::RayoMortal: case AtaqueJefe::OndaMortal: case AtaqueJefe::IllFlash: case AtaqueJefe::RayoDestructor:
      RayosKit_(a, ang, &n);
      for (int r = 0; r < n; ++r)
        if (Efecto2D* e = nuevo(Forma::Rayo, k.dur)) {
          e->golpea = false, e->x2 = jefe.x + std::cos(ang[r]) * kLargoRayo, e->y2 = jefe.y + std::sin(ang[r]) * kLargoRayo, e->ancho = k.ancho, e->radio = kLargoRayo;
        }
      jefe.estado = EstadoJefe::Kit, jefe.t = 0;
      Jugo_(0, 3, 0.5f, jefe.x, jefe.y);
      return;
    case AtaqueJefe::IllBola: case AtaqueJefe::BolaMuerte:
      RayosKit_(a, ang, &n);
      for (int r = 0; r < n; ++r)
        if (Efecto2D* e = nuevo(Forma::Bola, 28.0f / k.vel)) {
          const float ax = a == AtaqueJefe::BolaMuerte ? vx / l : std::cos(ang[r]), ay = a == AtaqueJefe::BolaMuerte ? vy / l : std::sin(ang[r]);
          e->vx = ax * k.vel, e->vy = ay * k.vel, e->radio = k.ancho, e->dano = dano;
        }
      break;
    case AtaqueJefe::DiscoSauzer:
      if (Efecto2D* e = nuevo(Forma::Disco, 3.2f)) e->vx = vx / l * k.vel, e->vy = vy / l * k.vel, e->radio = k.ancho, e->dano = dano, e->empuje = k.vel;
      break;
    case AtaqueJefe::Taiyoken:   // destello: ciega 2,5 s (a medio paso) a quien siga dentro; sin dano
      if (l < k.ancho + Sim::kRadioJugador) cegado = 2.5f;
      if (Efecto2D* e = nuevo(Forma::Explosion, 0.45f)) e->golpea = false, e->radio = k.ancho * 1.3f;
      Jugo_(0, 3, 0.5f, jefe.x, jefe.y);
      break;
    case AtaqueJefe::InvocaCellJr: {   // crias alrededor del jefe (el hueco 'celljr' de la tabla de enemigos)
      int tipo = -1;
      for (size_t q = 0; q < datos->enemigos.size() && tipo < 0; ++q) if (datos->enemigos[q].id == "celljr") tipo = int(q);
      for (int b = 0; tipo >= 0 && b < int(k.dano); ++b) {
        const float a = 2 * kPi * float(b) / k.dano + Azar01(), pos[2] = {jefe.x + std::cos(a) * k.ancho, jefe.y + std::sin(a) * k.ancho};
        Aparecer(0, pos, false, tipo);
      }
      Jugo_(0, 3, 0.4f, jefe.x, jefe.y);
      break;
    }
    case AtaqueJefe::Absorcion:   // el tiron dura 'dur' s (estado Kit): ver PasoJefe
      jefe.estado = EstadoJefe::Kit, jefe.t = 0;
      Jugo_(0, 3, 0.4f, jefe.x, jefe.y);
      return;
    case AtaqueJefe::CampoEnergia: {   // la onda del escudo: da a quien este dentro del radio
      jefe.escudo = 0;
      if (l < k.ancho + Sim::kRadioJugador) DanoJugador(dano);
      if (Efecto2D* e = nuevo(Forma::Explosion, 0.6f)) e->golpea = false, e->radio = k.ancho;
      Jugo_(0, 3, 0.7f, jefe.x, jefe.y);
      break;
    }
    default: break;   // las esferas ya cayeron sobre su sombra (IniciarKit_)
  }
  jefe.estado = EstadoJefe::Persigue, jefe.t = 0;
}

void Juego::CambiarFase_() {   // a media vida: destello y pausa, el jefe cambia de forma y no recibe dano mientras dura (kTransforma)
  jefe.fase = std::min(jefe.fase + 1, NFases()), jefe.estado = EstadoJefe::Transforma, jefe.t = 0, jefe.escudo = 0, jefe.ataque = AtaqueJefe::Ninguno;
  int m = 0;   // fuera sus avisos y esferas pendientes: no se ataca desde la forma vieja
  for (int n = 0; n < nEfectos; ++n) {
    const Efecto2D& e = efectos[n];
    const bool aviso = e.hostil && (e.forma == Forma::AvisoLinea || e.forma == Forma::AvisoAnillo || e.forma == Forma::AvisoZona || e.forma == Forma::Sombra ||
                                    e.forma == Forma::Rayo || e.forma == Forma::Aura);
    if (!aviso) efectos[m++] = efectos[n];
  }
  nEfectos = m;
  if (Efecto2D* e = NuevoEfecto())   // el estallido del cambio (solo dibujo)
    e->forma = Forma::Explosion, e->hostil = true, e->golpea = false, e->def = 255, e->color = 0xFFE890, e->x = jefe.x, e->y = jefe.y, e->radio = 5.5f, e->vida = e->vida0 = 1.0f;
  EventoFijo_(TipoEvento::JefeFase, jefe.fase, jefe.x, jefe.y);
  Jugo_(24, 3, 0.9f, jefe.x, jefe.y);
}

void Juego::PasoJefe() {
  const DefJefe& d = DJ();
  if (jefe.estado == EstadoJefe::Ausente) {
    if (Minuto() < MinutoJefe_()) return;
    const float a = Azar01() * 2 * kPi;
    jefe.x = jugador.x + std::cos(a) * 16, jefe.y = jugador.y + std::sin(a) * 16;
    jefe.vida = jefe.vida_max = d.vida * datos->dificultad[config.dificultad].vida, jefe.estado = EstadoJefe::Persigue, jefe.t = 0;
    Evento_(TipoEvento::JefeAparece, 0, jefe.x, jefe.y);
    Jugo_(0, 3, 0.5f, jefe.x, jefe.y);
    return;
  }
  if (jefe.estado == EstadoJefe::Muerto) return;
  if (config.jefes_v2 && !furia && tiempo >= datos->ritmo.furia / 15.0f * duracion) {   // A08: sigue vivo al minuto de la furia
    furia = true;
    EventoFijo_(TipoEvento::JefeFuria, 0, jefe.x, jefe.y);
    Jugo_(0, 3, 0.8f, jefe.x, jefe.y);
  }
  const int kit = KitJefe_();   // A07: 0 = ataques de siempre
  if (kit && jefe.fase < NFases() && jefe.estado != EstadoJefe::Transforma && jefe.vida <= jefe.vida_max * UmbralFase_(jefe.fase)) CambiarFase_();   // SUR-8: 66/33 % (50 % con 2 fases)
  const bool p2 = kit && jefe.fase >= 2;
  const float dt = (congelado > 0 ? kDt * datos->recogibles.reloj_jefe : kDt) * (furia ? datos->ritmo.furia_vel : 1.0f);   // reloj: el jefe va mas despacio
  jefe.t += dt;
  jefe.destello = std::fmax(0.0f, jefe.destello - kDt * 6);
  const float vx = jugador.x - jefe.x, vy = jugador.y - jefe.y, l = std::sqrt(vx * vx + vy * vy) + 1e-4f;
  const float ult = kit && NFases() >= 3 && jefe.fase >= 3 ? 1.1f : 1.0f;   // SUR-8: la ultima fase de tres: algo mas de velocidad y dano
  float dano = d.dano * (furia ? datos->ritmo.furia_dano : 1.0f) * (p2 ? 1.15f : 1.0f) * ult;
  const float vel = d.velocidad * (p2 ? 1.15f : 1.0f) * ult, cada = d.cada * (p2 ? 0.8f : 1.0f);
  const float dano_kit = d.dano / 20.0f * (furia ? datos->ritmo.furia_dano : 1.0f) * (p2 ? 1.15f : 1.0f) * ult;   // los ataques del kit: su dano base x esto
  auto aviso = [&](Forma f) {
    if (Efecto2D* e = NuevoEfecto()) {
      e->forma = f, e->hostil = true, e->golpea = false, e->def = 255, e->color = 0xFF3030, e->x = jefe.x, e->y = jefe.y;
      e->x2 = jefe.x + jefe.dx * d.carga_vel * d.carga_dura, e->y2 = jefe.y + jefe.dy * d.carga_vel * d.carga_dura;
      e->ancho = d.carga_ancho, e->radio = 4, e->vida = e->vida0 = d.aviso;
    }
  };
  switch (jefe.estado) {
    case EstadoJefe::Persigue:
      jefe.dx = vx / l, jefe.dy = vy / l;
      jefe.x += jefe.dx * vel * dt, jefe.y += jefe.dy * vel * dt;
      if (jefe.t >= cada && kit) {   // A07: un ataque de su kit (la carga es su combo)
        const AtaqueJefe a = ElegirAtaque_(kit);
        jefe.t = 0, ++jefe.n_ataques;
        if (a == AtaqueJefe::Carga) {
          jefe.estado = EstadoJefe::AvisoCarga, jefe.ataque = a;
          aviso(Forma::AvisoLinea);
          Evento_(TipoEvento::AvisoJefe, 0, jefe.x, jefe.y);
        } else {
          IniciarKit_(a, DatosAt(a).dano * dano_kit);
        }
      } else if (jefe.t >= cada) {
        // 0 carga, 1 anillo, 2 lluvia (tanda 4: solo si el jefe la tiene)
        const int at = d.lluvia_n > 0 ? int(Azar() % 3u) : ((Azar() & 1) != 0 ? 0 : 1);
        jefe.t = 0;
        if (at < 2) {
          jefe.estado = at == 0 ? EstadoJefe::AvisoCarga : EstadoJefe::AvisoAnillo;
          aviso(at == 0 ? Forma::AvisoLinea : Forma::AvisoAnillo);
        } else {   // zona rectangular centrada en el jugador y una sombra por proyectil (la primera, a sus pies); caen escalonados
          jefe.estado = EstadoJefe::AvisoLluvia;
          const float cx = jugador.x, cy = jugador.y, hx = d.lluvia_ancho * 0.5f, hy = d.lluvia_alto * 0.5f;
          const float dura = d.lluvia_aviso + 0.12f * float(d.lluvia_n - 1);
          if (Efecto2D* e = NuevoEfecto())
            e->forma = Forma::AvisoZona, e->hostil = true, e->golpea = false, e->def = 255, e->color = 0xFF3030, e->x = cx, e->y = cy, e->x2 = hx, e->y2 = hy,
            e->vida = e->vida0 = dura;
          for (int b = 0; b < d.lluvia_n; ++b)
            if (Efecto2D* e = NuevoEfecto()) {
              e->forma = Forma::Sombra, e->hostil = true, e->golpea = false, e->def = 255, e->color = 0xFF4040;
              e->x = b == 0 ? cx : cx + (Azar01() * 2 - 1) * hx, e->y = b == 0 ? cy : cy + (Azar01() * 2 - 1) * hy;
              e->radio = d.lluvia_radio, e->dano = d.lluvia_dano, e->vida = e->vida0 = d.lluvia_aviso + 0.12f * float(b);
            }
        }
        Evento_(TipoEvento::AvisoJefe, at, jefe.x, jefe.y);
      }
      break;
    case EstadoJefe::AvisoLluvia:   // invoca la lluvia (pose de tecnica) mientras caen las primeras
      if (jefe.t >= d.aviso) jefe.estado = EstadoJefe::Persigue, jefe.t = 0;
      break;
    case EstadoJefe::AvisoCarga:
      if (jefe.t >= d.aviso) jefe.estado = EstadoJefe::Carga, jefe.t = 0, Jugo_(0, 3, 0.45f, jefe.x, jefe.y);
      break;
    case EstadoJefe::Carga:
      jefe.x += jefe.dx * d.carga_vel * dt, jefe.y += jefe.dy * d.carga_vel * dt, dano = d.carga_dano * (furia ? datos->ritmo.furia_dano : 1.0f);
      if (jefe.t >= d.carga_dura) jefe.estado = EstadoJefe::Persigue, jefe.t = 0;
      break;
    case EstadoJefe::AvisoKit:   // A07: avisa; al acabar el aviso sale el ataque (los rayos dejan el estado Kit unos segundos)
      if (jefe.t >= DatosAt(jefe.ataque).aviso) DispararKit_(jefe.ataque, DatosAt(jefe.ataque).dano * dano_kit);
      break;
    case EstadoJefe::Kit: {   // rayo en curso: da al jugador mientras lo toca (la invulnerabilidad tras el golpe evita repetirlo)
      const DatosKit& k = DatosAt(jefe.ataque);
      if (jefe.ataque == AtaqueJefe::Absorcion) {   // tira del jugador hacia el jefe; si llega hasta el, el golpe
        if (l < k.ancho) {
          const float f = std::fmin(l, k.vel * kDt) / l;
          jugador.x -= vx * f, jugador.y -= vy * f;
          if (l < d.radio + Sim::kRadioJugador + 0.5f) DanoJugador(k.dano * dano_kit);
        }
        if (jefe.t >= k.dur) jefe.estado = EstadoJefe::Persigue, jefe.t = 0;
        break;
      }
      float ang[3];
      int n;
      RayosKit_(jefe.ataque, ang, &n);
      for (int r = 0; r < n; ++r)
        if (DistSegmento(jugador.x, jugador.y, jefe.x, jefe.y, jefe.x + std::cos(ang[r]) * kLargoRayo, jefe.y + std::sin(ang[r]) * kLargoRayo) < k.ancho * 0.5f + Sim::kRadioJugador)
          DanoJugador(k.dano * dano_kit);
      if (jefe.t >= k.dur) jefe.estado = EstadoJefe::Persigue, jefe.t = 0;
      break;
    }
    case EstadoJefe::Transforma:
      if (jefe.t >= kTransforma) jefe.estado = EstadoJefe::Persigue, jefe.t = 0;
      break;
    case EstadoJefe::AvisoAnillo:
      if (jefe.t >= d.aviso) {
        for (int b = 0; b < d.anillo_balas; ++b)
          if (Efecto2D* e = NuevoEfecto()) {
            const float a = 2 * kPi * float(b) / float(d.anillo_balas);
            e->forma = Forma::Bola, e->hostil = true, e->def = 255, e->color = 0xFF4040, e->x = jefe.x, e->y = jefe.y;
            e->vx = std::cos(a) * d.bala_vel, e->vy = std::sin(a) * d.bala_vel, e->radio = d.bala_radio, e->dano = d.bala_dano;
            e->vida = e->vida0 = 30.0f / d.bala_vel;
          }
        jefe.estado = EstadoJefe::Persigue, jefe.t = 0;
        Jugo_(0, 3, 0.4f, jefe.x, jefe.y);
      }
      break;
    default: break;
  }
  const float rr = d.radio + Sim::kRadioJugador;
  if (vx * vx + vy * vy < rr * rr && jefe.estado != EstadoJefe::Transforma) DanoJugador(dano);
}

// ---------------------------------------------------------------- tanda 4: dano al jugador, rompibles, rayo y lluvia
void Juego::DanoJugador(float d) {
  if (jugador.invulnerable > 0 || d <= 0) return;
  d *= mdano_ * (1 - std::fmin(0.6f, Mult(Efecto::Armadura)));   // C04: armadura (tope 60 %)
  jugador.vida -= d, jugador.invulnerable = datos->invulnerable, jugador.destello = 1;
  Evento_(TipoEvento::DanoJugador, int(d + 0.5f), jugador.x, jugador.y);
}

void Juego::Impacto(const Efecto2D& e) {
  const float vx = jugador.x - e.x, vy = jugador.y - e.y, rr = e.radio + Sim::kRadioJugador;
  if (vx * vx + vy * vy < rr * rr) DanoJugador(e.dano);
  if (e.forma == Forma::AvisoRayo) {   // el rayo dana tambien a la horda (y al jefe) y rompe lo que pille
    const float de = Escenario().rayo_dano_enemigos;
    EnCirculo(e.x, e.y, e.radio, [&](int i) { Dano(i, de, -1, 0.8f, e.x, e.y); });
    if (JefeEn(e.x, e.y, e.radio)) DanoJefe(de, -1);
    RomperEn(e.x, e.y, e.radio);
    Evento_(TipoEvento::RayoCae, 0, e.x, e.y);
  }
  if (Efecto2D* f = NuevoEfecto()) {   // dibujo: el rayo cayendo o el estallido del proyectil
    f->forma = e.forma == Forma::AvisoRayo ? Forma::RayoCae : Forma::Explosion, f->hostil = true, f->golpea = false, f->def = 255;
    f->color = e.forma == Forma::AvisoRayo ? 0xBFE6FF : 0xFF6030, f->x = e.x, f->y = e.y, f->radio = e.radio;
    f->vida = f->vida0 = e.forma == Forma::AvisoRayo ? 0.5f : 0.35f;
  }
}

void Juego::PasoRayo() {
  const DefEscenario& s = Escenario();
  if (s.rayo_cada <= 0 || (rayo_t_ -= kDt) > 0) return;
  rayo_t_ = s.rayo_cada * (0.6f + 0.8f * Azar01());
  float x = jugador.x, y = jugador.y;
  if (Azar01() < 0.6f) {   // casi siempre cerca, no siempre encima
    const float a = Azar01() * 2 * kPi, r = 1.5f + Azar01() * 5.0f;
    x += std::cos(a) * r, y += std::sin(a) * r;
  }
  if (Efecto2D* e = NuevoEfecto())
    e->forma = Forma::AvisoRayo, e->hostil = true, e->golpea = false, e->def = 255, e->color = 0xBFE6FF, e->x = x, e->y = y, e->radio = s.rayo_radio,
    e->dano = s.rayo_dano, e->vida = e->vida0 = s.rayo_aviso;
}

// Rompibles: celdas de rompible_celda de lado; la celda (i, j) tiene uno si su ruido (con la sal de la semilla) cae bajo la densidad.
// Salen a menos de kCerca del jugador y se quitan pasado kLejos (los rotos quedan anotados: no vuelven). Sin memoria nueva.
namespace {
constexpr float kCerca = 24.0f, kLejos = 32.0f;
uint32_t Ruido(int i, int j, uint32_t sal) {
  uint32_t h = uint32_t(i) * 0x8DA6B343u ^ uint32_t(j) * 0xD8163841u ^ sal * 0xCB1AB31Fu;
  h ^= h >> 13, h *= 0x5BD1E995u, h ^= h >> 15;
  return h;
}
}  // namespace

void Juego::PasoRompibles() {
  const DefEscenario& s = Escenario();
  // fuera los lejanos (el roto se anota para que no vuelva)
  for (int k = 0; k < nRompibles;) {
    const Rompible& r = rompibles[k];
    const float vx = r.x - jugador.x, vy = r.y - jugador.y;
    if (vx * vx + vy * vy < kLejos * kLejos) { ++k; continue; }
    if (r.roto >= 0) rotos_[nRotos_ % 256][0] = r.ci, rotos_[nRotos_ % 256][1] = r.cj, ++nRotos_;
    rompibles[k] = rompibles[--nRompibles];
  }
  if (s.rompibles.empty()) return;
  const float c = datos->rompible_celda;
  const int i0 = int(std::floor((jugador.x - kCerca) / c)), i1 = int(std::floor((jugador.x + kCerca) / c));
  const int j0 = int(std::floor((jugador.y - kCerca) / c)), j1 = int(std::floor((jugador.y + kCerca) / c));
  for (int cj = j0; cj <= j1; ++cj)
    for (int ci = i0; ci <= i1 && nRompibles < kMaxRompibles; ++ci) {
      const uint32_t h = Ruido(ci, cj, sal_);
      if (float(h % 1000u) >= datos->rompible_densidad * 1000.0f) continue;
      const float x = (float(ci) + 0.2f + 0.6f * float((h >> 10) & 255u) / 255.0f) * c, y = (float(cj) + 0.2f + 0.6f * float((h >> 18) & 255u) / 255.0f) * c;
      const float vx = x - jugador.x, vy = y - jugador.y;
      if (vx * vx + vy * vy > kCerca * kCerca || (pasos == 0 && vx * vx + vy * vy < 9.0f)) continue;   // ni lejos ni encima al empezar
      bool esta = false;
      for (int k = 0; k < nRompibles && !esta; ++k) esta = rompibles[k].ci == ci && rompibles[k].cj == cj;
      for (int k = 0; k < std::min(nRotos_, 256) && !esta; ++k) esta = rotos_[k][0] == ci && rotos_[k][1] == cj;
      if (esta) continue;
      Rompible& r = rompibles[nRompibles++];
      r = Rompible{};
      r.x = x, r.y = y, r.ci = ci, r.cj = cj, r.tipo = s.rompibles[(h >> 26) % s.rompibles.size()];
      r.vida = datos->rompibles[size_t(r.tipo)].vida;
    }
}

void Juego::Romper(int i) {
  Rompible& r = rompibles[i];
  if (r.roto >= 0) return;
  const DefRompible& d = datos->rompibles[size_t(r.tipo)];
  r.roto = 0, r.vida = 0, ++rotos;
  Evento_(TipoEvento::Rompible, r.tipo, r.x, r.y);
  if (d.exp > 0) {
    if (nGemas < kMaxGemas) gx[nGemas] = r.x - 0.35f, gy[nGemas] = r.y - 0.2f, gvalor[nGemas++] = d.exp;
    else gvalor[pasos % kMaxGemas] += d.exp;
  }
  const float suerte = 1 + Mult(Efecto::Suerte);
  float a = Azar01();
  for (int k = 0; k < kTiposObjeto; ++k) {
    if (a < d.prob[k] * suerte) return Poner(r.x + 0.4f, r.y - 0.25f, TipoObjeto(k));
    a -= d.prob[k] * suerte;
  }
}

void Juego::RomperEn(float x, float y, float rad) {
  for (int i = 0; i < nRompibles; ++i) {
    const float vx = rompibles[i].x - x, vy = rompibles[i].y - y, rr = rad + 0.6f;
    if (vx * vx + vy * vy < rr * rr) Romper(i);
  }
}

// Los efectos del jugador que tocan un rompible entero le quitan su dano (como mucho un golpe cada 0,25 s). La forma de cada uno, como
// la de sus golpes a la horda; los que ya hicieron su dano al crearse (golpea = false: cono, explosion, rayo fino) cuentan mientras se ven.
void Juego::GolpesRompibles() {
  constexpr float kR = 0.6f;   // radio del objeto
  for (int i = 0; i < nRompibles; ++i) {
    Rompible& r = rompibles[i];
    if (r.roto >= 0) { r.roto += kDt; continue; }
    if (r.ultimo && uint32_t(pasos) - r.ultimo < 15u) continue;
    for (int n = 0; n < nEfectos; ++n) {
      const Efecto2D& e = efectos[n];
      if (e.hostil || e.vida <= 0) continue;
      const float vx = r.x - e.x, vy = r.y - e.y;
      bool toca = false;
      switch (e.forma) {
        case Forma::Rayo: {
          const float sx = e.x2 - e.x, sy = e.y2 - e.y;
          float t = (vx * sx + vy * sy) / (sx * sx + sy * sy + 1e-6f);
          t = t < 0 ? 0 : t > 1 ? 1 : t;
          const float dx = r.x - (e.x + sx * t), dy = r.y - (e.y + sy * t), h = e.ancho * 0.5f + kR;
          toca = dx * dx + dy * dy < h * h;
          break;
        }
        case Forma::Cono: {
          const float l = std::sqrt(vx * vx + vy * vy);
          toca = l < e.radio + kR && (l < kR || (vx * std::cos(e.ang) + vy * std::sin(e.ang)) >= std::cos(e.apertura) * l);
          break;
        }
        case Forma::Anillo:   // barrera y onda: el aro
          toca = vx * vx + vy * vy < (e.radio + kR + 0.3f) * (e.radio + kR + 0.3f);
          break;
        default: toca = vx * vx + vy * vy < (e.radio + kR) * (e.radio + kR); break;
      }
      if (!toca) continue;
      r.ultimo = uint32_t(pasos), r.vida -= std::max(1.0f, e.dano);
      if (r.vida <= 0) Romper(i);
      break;
    }
  }
}

// ---------------------------------------------------------------- A08: ritmo (minijefes y eventos avisados)
const char* Juego::NombreEvento(int tipo) {
  static const char* const k[] = {"enjambre", "cerco", "meteoros", "minijefe"};
  return tipo >= 0 && tipo < 4 ? k[tipo] : "?";
}

int Juego::HuecoOleada_() {   // un hueco (tipo de enemigo) con los pesos de la fila de oleada actual
  const Oleada* o = &datos->oleadas[0];
  for (const Oleada& w : datos->oleadas) if (w.minuto <= Minuto()) o = &w;
  float total = 0;
  for (float w : o->peso) total += w;
  float r = Azar01() * total;
  int t = 0;
  while (t < kTiposEnemigo - 1 && r >= o->peso[t]) r -= o->peso[t++];
  return t;
}

void Juego::ProgramarRitmo_() {   // el calendario de la partida (en segundos de juego; el aviso va 'aviso' s antes)
  const DefRitmo& r = datos->ritmo;
  nProg_ = 0;
  auto pon = [&](float minuto, int tipo, int n) {
    if (nProg_ < kMaxEventos2 + 2) prog_[nProg_++] = {minuto / 15.0f * duracion, tipo, n, false, false};
  };
  for (const float m : r.minijefe) pon(m, 3, 1);
  if (!r.eventos.empty()) for (const DefEvento& e : r.eventos) pon(e.minuto, e.tipo, e.n);
  else pon(3.0f, 0, 45), pon(7.0f, 1, 36), pon(8.5f, 2, 12), pon(11.0f, 0, 70), pon(13.0f, 1, 48);   // de serie
}

void Juego::AjustesTardios(bool jugo, bool formas, bool ia, bool jefes) {
  config.jugo = config.jugo || jugo, config.formas = config.formas || formas, config.ia = config.ia || ia;
  if (jefes && !config.jefes_v2) config.jefes_v2 = true, ProgramarRitmo_();
}

void Juego::PasoRitmo_() {
  const float aviso = datos->ritmo.aviso;
  for (int k = 0; k < nProg_; ++k) {
    Prog& p = prog_[k];
    if (!p.avisado && tiempo >= p.t - aviso) {
      p.avisado = true;
      EventoFijo_(TipoEvento::AvisoEvento, p.tipo, jugador.x, jugador.y, aviso);
    }
    if (!p.hecho && tiempo >= p.t) {
      p.hecho = true;
      Lanzar_(p.tipo, p.n, k);
    }
  }
}

void Juego::Lanzar_(int tipo, int n, int k) {
  (void)k;
  EventoFijo_(TipoEvento::EventoOleada, tipo, jugador.x, jugador.y, float(n));
  switch (tipo) {
    case 0: {   // enjambre: un grupo apretado que sale por un lado a la vez
      const float a0 = Azar01() * 2 * kPi;
      for (int b = 0; b < n; ++b) {
        const float a = a0 + (Azar01() - 0.5f) * 1.0f, r = 19.0f + Azar01() * 4.0f, pos[2] = {jugador.x + std::cos(a) * r, jugador.y + std::sin(a) * r};
        if (Aparecer(HuecoOleada_(), pos) < 0) break;
      }
      ++eventos_vistos;
      break;
    }
    case 1: {   // cerco: un anillo cerrado que se estrecha
      const float a0 = Azar01() * 2 * kPi;
      for (int b = 0; b < n; ++b) {
        const float a = a0 + 2 * kPi * float(b) / float(n), pos[2] = {jugador.x + std::cos(a) * 14.0f, jugador.y + std::sin(a) * 14.0f};
        if (Aparecer(HuecoOleada_(), pos) < 0) break;
      }
      ++eventos_vistos;
      break;
    }
    case 2: {   // lluvia de meteoros: marcas de rayo escalonadas alrededor del jugador
      const DefEscenario& s = Escenario();
      for (int b = 0; b < n; ++b)
        if (Efecto2D* e = NuevoEfecto()) {
          const float a = Azar01() * 2 * kPi, r = (b == 0 ? 0.0f : 1.0f + Azar01() * 8.0f);
          e->forma = Forma::AvisoRayo, e->hostil = true, e->golpea = false, e->def = 255, e->color = 0xFFA040;
          e->x = jugador.x + std::cos(a) * r, e->y = jugador.y + std::sin(a) * r, e->radio = s.rayo_radio, e->dano = s.rayo_dano;
          e->vida = e->vida0 = s.rayo_aviso + 0.25f * float(b);
        }
      ++eventos_vistos;
      break;
    }
    default: {   // minijefe: el elite del escenario, fuerte, con su cofre seguro
      int hueco = kTiposEnemigo - 1;
      for (int t = 0; t < kTiposEnemigo; ++t)
        if (datos->enemigos[size_t(Escenario().enemigos[t])].anillo > 0) hueco = t;
      const float a = Azar01() * 2 * kPi, pos[2] = {jugador.x + std::cos(a) * 20.0f, jugador.y + std::sin(a) * 20.0f};
      if (Aparecer(hueco, pos, true) >= 0) ++minijefes_vistos;
      break;
    }
  }
}

// ---------------------------------------------------------------- A06: conductas de los enemigos
void Juego::DispararEnemigo_(int i, const DefEnemigo& d) {
  const float vx = jugador.x - horda_.x[i], vy = jugador.y - horda_.y[i], l = std::sqrt(vx * vx + vy * vy) + 1e-4f;
  if (Efecto2D* e = NuevoEfecto()) {
    e->forma = Forma::Bola, e->hostil = true, e->def = 255, e->color = 0xFFC030, e->x = horda_.x[i], e->y = horda_.y[i];
    e->vx = vx / l * 9.0f, e->vy = vy / l * 9.0f, e->radio = 0.3f, e->dano = d.cond_dano, e->vida = e->vida0 = 2.6f;
  }
}

void Juego::PasoConductas_() {
  const Datos& D = *datos;
  for (int i = 0; i < horda_.n; ++i) {
    const DefEnemigo& d = D.enemigos[tipo_[i]];
    if (d.conducta == Conducta::Persigue || vida_[i] <= 0) continue;
    if (congelado > 0) continue;   // el reloj los para (Recoger ya les puso velocidad 0)
    float& t = cond_t_[i];
    uint8_t& est = cond_est_[i];
    const float vx = jugador.x - horda_.x[i], vy = jugador.y - horda_.y[i], l = std::sqrt(vx * vx + vy * vy) + 1e-4f;
    t += kDt;
    auto marca = [&](Forma f, float radio, float ancho, float dura) {   // aviso en el suelo (el mismo del jefe), sin dano propio
      if (Efecto2D* e = NuevoEfecto()) {
        e->forma = f, e->hostil = true, e->golpea = false, e->def = 255, e->color = 0xFF3030, e->x = horda_.x[i], e->y = horda_.y[i];
        e->x2 = horda_.x[i] + vx / l * d.cond_vel * 0.6f, e->y2 = horda_.y[i] + vy / l * d.cond_vel * 0.6f;
        e->ancho = ancho, e->radio = radio, e->vida = e->vida0 = dura;
      }
    };
    switch (d.conducta) {
      case Conducta::Tirador:
        if (est == 0) {
          horda_.vel[i] = d.velocidad;
          if (l < d.cond_rango && t >= d.cond_cada) est = 1, t = 0, horda_.vel[i] = 0;
        } else {
          horda_.vel[i] = 0;
          if (t >= d.cond_aviso) DispararEnemigo_(i, d), est = 0, t = 0, horda_.vel[i] = d.velocidad;
        }
        break;
      case Conducta::Embestida:
        if (est == 0) {
          horda_.vel[i] = d.velocidad;
          if (l < d.cond_rango && t >= d.cond_cada) est = 1, t = 0, horda_.vel[i] = 0, marca(Forma::AvisoLinea, 3.0f, 1.0f, d.cond_aviso);
        } else if (est == 1) {
          horda_.vel[i] = 0;
          if (t >= d.cond_aviso) est = 2, t = 0, horda_.vel[i] = d.cond_vel;
        } else if (t >= 0.6f) est = 0, t = 0, horda_.vel[i] = d.velocidad;
        break;
      case Conducta::Explota:
        if (est == 0) {
          if (l < 2.2f) est = 1, t = 0, horda_.vel[i] = 0, marca(Forma::AvisoAnillo, d.cond_radio, 0.2f, d.cond_aviso);
        } else if (t >= d.cond_aviso) {
          if (l < d.cond_radio + Sim::kRadioJugador) DanoJugador(d.cond_dano);
          if (Efecto2D* e = NuevoEfecto())   // dibujo: el estallido (como el del proyectil de la lluvia del jefe)
            e->forma = Forma::Explosion, e->hostil = true, e->golpea = false, e->def = 255, e->color = 0xFF6030, e->x = horda_.x[i], e->y = horda_.y[i],
            e->radio = d.cond_radio, e->vida = e->vida0 = 0.35f;
          Dano(i, vida_[i] + 1.0f, -1, 0, horda_.x[i], horda_.y[i]);   // se destruye (cuenta como derrotado: suelta su gema; la cifra = su vida)
          Jugo_(0, 2, 0.25f, horda_.x[i], horda_.y[i], false);
        }
        break;
      case Conducta::Elite:
      case Conducta::Invoca:
        if (est == 0) {
          horda_.vel[i] = d.velocidad;
          if (l < 18.0f && t >= d.cond_cada && horda_.n + d.cond_n < Sim::kMax) est = 1, t = 0, horda_.vel[i] = 0, marca(Forma::AvisoAnillo, 2.5f, 0.2f, d.cond_aviso);
        } else {
          horda_.vel[i] = 0;
          if (t >= d.cond_aviso) {
            const float px = horda_.x[i], py = horda_.y[i];
            for (int b = 0; b < d.cond_n; ++b) {
              const float a = 2 * kPi * float(b) / float(std::max(1, d.cond_n)) + Azar01(), pos[2] = {px + std::cos(a) * 1.8f, py + std::sin(a) * 1.8f};
              Aparecer(0, pos);   // el hueco 0 del escenario: el enemigo corriente
            }
            est = 0, t = 0;
            if (i < horda_.n) horda_.vel[i] = d.velocidad;
          }
        }
        break;
      default: break;
    }
  }
}

// A05: barra de ki y transformacion. Los numeros salen de DefFormas; sin Config::formas nada de esto corre.
void Juego::Transformar_(int f) {
  const DefFormas& d = datos->formas;
  f = std::clamp(f, 1, std::max(1, d.max));
  const bool nueva = f != forma;
  forma = f, ki = 1, forma_t = d.dura;
  Recalcular();
  if (nueva) EventoFijo_(TipoEvento::Forma, f, jugador.x, jugador.y, d.dura);
}

void Juego::DarKi(float f) {
  if (!config.formas || forma > 0) return;
  ki = std::fmin(1.0f, ki + f);
  if (ki >= 1) Transformar_(1);
}

void Juego::PasoForma_() {
  const DefFormas& d = datos->formas;
  if (forma > 0) {   // transformado: la barra se vacia; al llegar a 0 vuelve a la base y empieza a llenarse otra vez
    forma_t -= kDt, ki = std::fmax(0.0f, forma_t / d.dura);
    if (forma_t <= 0) {
      forma = 0, ki = forma_t = 0;
      Recalcular();
      EventoFijo_(TipoEvento::Forma, 0, jugador.x, jugador.y, 0);
    }
    derrotados_ki_ = derrotados;
    return;
  }
  const int nuevos = derrotados - derrotados_ki_;
  derrotados_ki_ = derrotados;
  DarKi(kDt / std::fmax(1.0f, d.tiempo) + float(nuevos) / std::fmax(1.0f, d.kills));
}

// A02: pausa de golpe (la simulacion se queda quieta unos cuadros) y temblor de camara (lo dibuja modo_mundo.cpp). Topes: una pausa cada
// 0,35 s y un temblor cada 0,3 s, para que ni la horda ni una tecnica que pega en cadena dejen el juego a tirones. Solo con Config::jugo.
void Juego::Jugo_(int cuadros, int temblor, float intensidad, float x, float y, bool especial) {
  if (!config.jugo) return;
  // 'especial' (elite, jefe, evolucion): solo espera a que acabe la pausa anterior; el golpe fuerte corriente, una cada 1,2 s
  if (cuadros > 0 && pausa_ <= 0 && (especial ? pausa_cd_ <= 0.85f : pausa_cd_ <= 0)) {
    pausa_ = float(cuadros) * kDt, pausa_cd_ = especial ? 0.35f : 1.2f, ++pausas;
    EventoFijo_(TipoEvento::Pausa, int(float(cuadros) * kDt * 1000.0f + 0.5f), x, y);
  }
  if (temblor > 0 && temblor_cd_ <= 0) temblor_cd_ = 0.3f, ++temblores, EventoFijo_(TipoEvento::Temblor, temblor, x, y, intensidad);
}

void Juego::Paso(const Entrada& in) {
  if (estado != Estado::Jugando) return;
  nEventos = 0;
  if (config.jugo) {
    pausa_cd_ = std::fmax(0.0f, pausa_cd_ - kDt), temblor_cd_ = std::fmax(0.0f, temblor_cd_ - kDt);
    if (pausa_ > 0) {   // pausa de golpe: ni el tiempo ni nada avanza
      pausa_ -= kDt, pausa_total += kDt;
      return;
    }
  }
  ++pasos;
  tiempo += kDt;
  const Datos& D = *datos;

  // jugador
  float dx = in.x, dy = in.y, l = std::sqrt(dx * dx + dy * dy);
  if (l > 1) dx /= l, dy /= l, l = 1;
  jugador.moviendo = l > 0.1f;
  if (jugador.moviendo) jugador.dx = dx / l, jugador.dy = dy / l;
  const float vj = jugador.velocidad * (cegado > 0 ? 0.45f : 1.0f);   // SUR-04: el Taiyoken lo deja a medio paso
  if (cegado > 0) cegado = std::fmax(0.0f, cegado - kDt);
  jugador.x += dx * vj * kDt, jugador.y += dy * vj * kDt;
  jugador.invulnerable = std::fmax(0.0f, jugador.invulnerable - kDt);
  jugador.destello = std::fmax(0.0f, jugador.destello - kDt * 4);

  if (config.formas) PasoForma_();

  // reloj: al acabar la horda vuelve a su velocidad
  if (congelado > 0 && (congelado -= kDt) <= 0) {
    congelado = 0;
    for (int i = 0; i < horda_.n; ++i) horda_.vel[i] = D.enemigos[tipo_[i]].velocidad;
  }
  iman = std::fmax(0.0f, iman - kDt);

  // horda (persecucion + rejilla + separacion)
  horda_.jx = jugador.x, horda_.jy = jugador.y;
  horda_.Paso(0, 0);
  for (int i = 0; i < horda_.n; ++i) destello_[i] = std::fmax(0.0f, destello_[i] - kDt * 7);

  // oleada
  const Oleada* o = &D.oleadas[0];
  for (const Oleada& w : D.oleadas) if (w.minuto <= Minuto()) o = &w;
  const int tope = o->tope < D.tope_pantalla ? o->tope : D.tope_pantalla;
  aparecer_ += o->ritmo * Escenario().ritmo * escala_ * kDt * (1 + Mult(Efecto::Maldicion));   // C04: maldicion = mas horda
  float total = 0;
  for (float w : o->peso) total += w;
  while (aparecer_ >= 1 && horda_.n < tope) {
    aparecer_ -= 1;
    float r = Azar01() * total;
    int t = 0;
    while (t < kTiposEnemigo - 1 && r >= o->peso[t]) r -= o->peso[t++];
    Aparecer(t);
  }
  if (aparecer_ > 1) aparecer_ = 1;
  if (config.jefes_v2) PasoRitmo_();
  if (config.ia) PasoConductas_();

  // contacto con la horda
  float toca = 0;
  for (int i = 0; i < horda_.n; ++i) {
    const float vx = horda_.x[i] - jugador.x, vy = horda_.y[i] - jugador.y, rr = horda_.rad[i] + Sim::kRadioJugador + 0.05f;
    if (vida_[i] > 0 && vx * vx + vy * vy < rr * rr && D.enemigos[tipo_[i]].dano > toca) toca = D.enemigos[tipo_[i]].dano;
  }
  if (toca > 0) DanoJugador(toca);

  // jefe, rayos, rompibles y tecnicas
  PasoJefe();
  PasoRayo();
  if (pasos % 15 == 0) PasoRompibles();
  const float rec = std::fmax(0.4f, 1 - Mult(Efecto::Recarga));
  for (int s = 0; s < nTecnicas && estado == Estado::Jugando; ++s) {
    recarga_[s] -= kDt;
    if (recarga_[s] <= 0) {
      Disparar(s);
      recarga_[s] += D.tecnicas[tecnicas[s].def].recarga[tecnicas[s].nivel - 1] * rec;
      if (recarga_[s] < kDt) recarga_[s] = kDt;
    }
  }
  MoverEfectos();
  GolpesRompibles();

  // racha: se apaga a los racha.dura s sin derrotar; mientras dure, un poco mas de experiencia
  if ((racha_t += kDt) > D.racha.dura) racha = 0;
  // gemas: iman y recogida
  const float iman2 = jugador.recogida * jugador.recogida;
  const float mexp = (1 + Mult(Efecto::Exp) + Mult(Efecto::Maldicion)) * escala_ * (1 + std::fmin(float(racha) * D.racha.exp, D.racha.tope));
  for (int g = 0; g < nGemas;) {
    const float vx = jugador.x - gx[g], vy = jugador.y - gy[g], d2 = vx * vx + vy * vy;
    if (d2 < 0.36f) {
      jugador.exp += gvalor[g] * mexp;
      Evento_(TipoEvento::Recogida, int(gvalor[g] + 0.5f), gx[g], gy[g]);
      --nGemas, gx[g] = gx[nGemas], gy[g] = gy[nGemas], gvalor[g] = gvalor[nGemas];
      continue;
    }
    if (d2 < iman2 || iman > 0) {   // con el iman, todas
      const float d = std::sqrt(d2), v = std::fmax(jugador.velocidad + 8, iman > 0 ? D.recogibles.iman_vel : 0.0f) * kDt;
      gx[g] += vx / d * v, gy[g] += vy / d * v;
    }
    ++g;
  }
  // objetos: se cogen al tocarlos (el que se coge deja su sitio al ultimo)
  for (int k = 0; k < nObjetos;) {
    const float vx = jugador.x - objetos[k].x, vy = jugador.y - objetos[k].y;
    if (vx * vx + vy * vy >= 0.81f) { ++k; continue; }
    // Un cofre abierto ya ocupa premios[]. Conserva los otros para el siguiente paso.
    if (cofre_ && objetos[k].tipo == TipoObjeto::Cofre) { ++k; continue; }
    const Objeto o = objetos[k];
    objetos[k] = objetos[--nObjetos];
    Recoger(o);
  }

  // fuera los muertos (el ultimo ocupa su hueco)
  for (int i = 0; i < horda_.n;) {
    if (vida_[i] > 0) { ++i; continue; }
    const int u = horda_.n - 1;
    tipo_[i] = tipo_[u], vida_[i] = vida_[u], destello_[i] = destello_[u];
    mini_[i] = mini_[u], cond_est_[i] = cond_est_[u], cond_t_[i] = cond_t_[u], escudo_[i] = escudo_[u];
    std::memcpy(ultimo_[i], ultimo_[u], sizeof ultimo_[i]);
    horda_.Quitar(i);
  }

  // vida, final y niveles
  const float regen = Mult(Efecto::Regen);
  if (regen > 0) jugador.vida = std::fmin(jugador.vida_max, jugador.vida + regen * kDt);
  if (jugador.vida <= 0 && vidas_extra > 0) {   // mejora permanente Reanimar: vuelve con una parte de la vida y un respiro
    --vidas_extra;
    jugador.vida = jugador.vida_max * std::clamp(Mult(Efecto::Reanimar), 0.1f, 1.0f), jugador.invulnerable = 2.5f;
    Evento_(TipoEvento::Reanimado, 0, jugador.x, jugador.y);
  }
  if (jugador.vida <= 0) {
    jugador.vida = 0, estado = Estado::Derrota;
    return;
  }
  if (estado == Estado::Victoria) return;
  if ((tiempo >= duracion && !(config.jefes_v2 && JefeVivo_())) || (victoria_ > 0 && (victoria_ -= kDt) <= 0)) {
    estado = Estado::Victoria;
    return;
  }
  // Una eleccion de XP cada 3 s de juego como minimo; el resto queda en la barra.
  if (jugador.exp >= jugador.exp_sig && tiempo - ultima_subida_ >= 3.0f) {
    jugador.exp -= jugador.exp_sig, ++jugador.nivel, ++subidas_;
    const float l = float(jugador.nivel - 1);
    const float curva = std::fmax(0.0f, l - 5);   // primeras mejoras sin freno cuadratico
    jugador.exp_sig = D.exp_base + D.exp_paso * l + D.exp_curva * curva * curva;
    ultima_subida_ = tiempo;
    Evento_(TipoEvento::Subida, jugador.nivel, jugador.x, jugador.y);
  }
  if (cofre_) {   // A09: la pantalla del cofre; opciones[] = los 3 primeros premios para pantallas que aun no la conocen
    estado = Estado::Eligiendo, nOpciones = std::min(nPremios, kOpciones);
    for (int k = 0; k < nOpciones; ++k) opciones[k] = premios[k];
  } else if (subidas_ > 0) estado = Estado::Eligiendo, Sortear();
}

uint64_t Juego::Hash() const {
  uint64_t h = horda_.Hash();
  auto mete = [&](const void* p, size_t b) {
    const auto* c = static_cast<const uint8_t*>(p);
    for (size_t k = 0; k < b; ++k) h = (h ^ c[k]) * 1099511628211ULL;
  };
  const float f[] = {jugador.x, jugador.y, jugador.vida, jugador.exp, jefe.x, jefe.y, jefe.vida, float(jugador.nivel), float(jefe.estado)};
  mete(f, sizeof f), mete(&tiempo, sizeof tiempo), mete(&derrotados, sizeof derrotados);
  mete(vida_, sizeof(float) * size_t(horda_.n)), mete(tipo_, size_t(horda_.n));
  mete(gx, sizeof(float) * size_t(nGemas)), mete(gy, sizeof(float) * size_t(nGemas));
  for (int k = 0; k < nObjetos; ++k) mete(&objetos[k].x, sizeof(float)), mete(&objetos[k].y, sizeof(float)), mete(&objetos[k].tipo, 1);   // sin el relleno
  mete(&zeni, sizeof zeni), mete(&congelado, sizeof congelado);
  for (int k = 0; k < nRompibles; ++k) mete(&rompibles[k].x, sizeof(float)), mete(&rompibles[k].roto, sizeof(float)), mete(&rompibles[k].tipo, sizeof(int));
  mete(&racha, sizeof racha), mete(&vidas_extra, sizeof vidas_extra);
  mete(tecnicas, sizeof tecnicas), mete(pasivas, sizeof pasivas), mete(&nEfectos, sizeof nEfectos), mete(&rng_, sizeof rng_);
  return h;
}

}  // namespace dbz3::survivors

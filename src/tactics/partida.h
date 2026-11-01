// Dragon Tactics H3/H4: estado de la partida (torneo de N entrenadores: fases, economia, tienda, copias, sinergias, bots, emparejamientos,
// eliminacion y clasificacion). Puro y determinista: sin graficos, sin reloj real y sin dependencias del juego (solo sim.h y catalogo.h);
// la presentacion (entrada.cpp) solo lee y pide acciones. El jugador humano es siempre el 0.
// Azar propio con flujos separados: tienda de cada jugador, decisiones de cada bot, emparejamientos y combate de cada duelo.
// Datos editables en tactics_economia.toml (ver CargarEconomia). Reserva compartida de copias: para cada unidad,
// libres + ofertas en tiendas + peso(estrellas) de lo que tiene cada jugador vivo = copias totales, siempre (los eliminados lo devuelven).
// Capsulas (H5): 2 por unidad, del inventario del jugador; se ganan una cada capsula_cada rondas o en la tienda; vender y combinar las devuelven al inventario.
// Guardar()/Cargar(): la partida se guarda en preparacion (antes de cerrarla) con todo el azar, y cargarla da la misma partida.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "src/tactics/catalogo.h"
#include "src/tactics/sim.h"

namespace dbz3::tactics {

constexpr int kMaxJug = 8, kNeutral = kMaxJug, kBanco = 8, kTablero = 18, kSlots = kBanco + kTablero, kMaxTienda = 5, kMaxCoste = 4, kMaxEstrellas = 4, kMaxCaps = 12, kHuecosCaps = 2;
// slot 0..7 = banquillo; 8..25 = tablero: (slot-8) = fila*6 + columna, fila 0 = la de delante (la mas cercana al rival).

// Capsula (tactics_capsulas.toml): mejoras en % (alcance en casillas) que se suman a las de las sinergias; ki_max negativo = la tecnica llega antes.
struct CapsulaDef { std::string id, es, en; int vida = 0, ataque = 0, defensa = 0, velocidad = 0, ki_inicial = 0, ki_max = 0, alcance = 0, tecnica = 0; };
struct Umbral { int n = 0, vida = 0, ataque = 0, defensa = 0; };   // con n miembros distintos: +% a los miembros
struct SinergiaDef { std::string id, es, en; std::vector<Umbral> umbrales; };
struct UnidadEco { std::string id; int coste = 1, copias = 0, cat = -1; int sin[2] = {-1, -1}; };   // cat: indice en el catalogo
struct Economia {
  int jugadores = 8;                                          // entrenadores del torneo (el humano + bots)
  std::vector<int> calidad_bots;                              // calidad de decision (0-100) de los bots 1..jugadores-1
  int oro_inicial = 8, ingreso_ronda = 5, ingreso_victoria = 1, interes_cada = 10, interes_tope = 3;
  int neutral_desde = 3, neutral_cada = 5, neutral_oro = 2, neutral_pct = 45, neutral_pct_ronda = 3;   // H10: ronda neutral (0 = nunca): todos contra personajes de coste 1, % de sus stats = neutral_pct + neutral_pct_ronda * ronda; ganar da oro y una capsula
  int racha_desde = 3, racha_cada = 2, racha_tope = 3;       // H10: de `racha_desde` victorias (o derrotas) seguidas, +1 oro, y +1 mas cada `racha_cada`, hasta `racha_tope`
  int tienda = 5, precio_actualizar = 2, precio_exp = 4, exp_compra = 4, exp_ronda = 2;
  int capsula_cada = 3, precio_capsula = 5;                   // una capsula gratis al acabar cada `capsula_cada` rondas (0 = nunca) y su precio en la tienda
  std::vector<CapsulaDef> capsulas;
  std::vector<int> exp_subir, tope_unidades;                  // por nivel (exp_subir: del nivel n al n+1); tope_unidades: unidades en el tablero
  std::vector<std::array<int, kMaxCoste>> peso_coste;         // por nivel: peso de que la oferta sea de coste 1..4
  int copias_coste[kMaxCoste] = {};
  int mult_estrella[kMaxEstrellas] = {100, 180, 324, 583};   // % de vida y ataque con 1, 2, 3 y 4 estrellas
  int vida_entrenador = 100, dano_base = 4, dano_cada_rondas = 2, dano_superviviente = 2, dano_tope = 25;
  std::vector<UnidadEco> unidades;
  std::vector<SinergiaDef> sinergias;
  int NivelMax() const { return int(tope_unidades.size()); }
};
// Carga y valida (rangos, ids del catalogo, sinergias existentes, cada umbral alcanzable con unidades distintas). Errores legibles, uno por linea.
bool CargarEconomia(const std::string& ruta, const Catalogo& cat, Economia* out, std::string* error);

struct Pieza { int unidad = -1, estrellas = 1, caps[kHuecosCaps] = {-1, -1}; bool hay() const { return unidad >= 0; } };   // unidad: indice en Economia::unidades; caps: capsulas equipadas o -1
struct Jugador {
  bool bot = false;
  int oro = 0, exp = 0, nivel = 1, vida = 0;
  int tienda[kMaxTienda] = {-1, -1, -1, -1, -1};   // unidad ofrecida o -1 (comprada)
  int inv[kMaxCaps] = {};                          // capsulas en el inventario, por tipo
  int caps_total = 0;                              // capsulas que ha recibido (rondas y tienda): inventario + equipadas, siempre
  Pieza pz[kSlots];
  int calidad = 100;                               // bots: calidad de decision 0-100 (el humano 100, solo para el piloto automatico)
  bool vivo = true;
  int puesto = 0, ronda_baja = 0;                  // puesto final (1 = campeon; 0 mientras sigue vivo) y ronda en que cayo
  int ultimo_rival = -1, copias_vistas = 0, gano = 0;   // ultimo rival (o equipo copiado), veces que lucho contra una copia, 1 si gano su ultimo duelo
  int racha = 0;                                   // H10: >0 victorias seguidas, <0 derrotas seguidas, 0 tras un empate
  bool bloq = false;                               // H10: tienda bloqueada: se conserva de una ronda a otra hasta desbloquearla o actualizarla
  int oro_cierre = 0;                              // oro al cerrar la preparacion (base del interes)
  int ganado = 0, gastado = 0;                     // ingresos acumulados y oro gastado en servicios (actualizar, EXP): para auditar la economia
};

enum class Fase { Preparacion, Combate, Resultado, Fin };
enum class Res { Ok, FaseIncorrecta, Invalido, SinOro, SinSitio, Tope };

// Un duelo de la ronda: el jugador `a` contra el tablero de `b`. Si `copia`, b solo presta su tablero (una copia que no consume reserva ni
// recibe dano); si no, b tambien juega y puede perder vida. `a` es siempre el de menor indice, asi que el humano (0) es el equipo 0 (abajo).
struct Duelo { int a, b; bool copia; };
struct SinActiva { int sinergia, cuenta, umbral; };   // umbral: indice del tramo activo (-1 ninguno); cuenta = miembros distintos en el tablero
struct Combatiente { int jugador, slot, unidad, estrellas; };   // por unidad de la simulacion del humano, en su orden; jugador = equipo (0 humano, 1 rival)

class Partida {
 public:
  Partida(const Catalogo& cat, const Economia& eco, uint64_t semilla, int jugadores = 0);   // 0 = eco.jugadores
  const Economia& Eco() const { return *eco_; }
  const Jugador& J(int p) const { return j_[p]; }
  Fase FaseActual() const { return fase_; }
  int Ronda() const { return ronda_; }
  bool Acabada() const { return fase_ == Fase::Fin; }
  int Campeon() const { return campeon_; }          // el entrenador que queda en pie (el de mejor puesto si caen varios a la vez); solo si Acabada()
  int N() const { return n_; }
  int Vivos() const;
  std::vector<int> Clasificacion() const;           // jugadores por puesto (1o primero); solo si Acabada()
  const std::vector<Duelo>& Duelos() const { return duelos_; }
  const std::vector<sim::Resultado>& Resultados() const { return res_; }   // por duelo; completo tras TerminarCombate()
  int DueloHumano() const { return duelo_h_; }      // indice en Duelos() del duelo del humano, -1 si ya cayo
  uint64_t Huella() const;                          // FNV-1a de todo el estado (para comparar partidas)
  int Libres(int unidad) const { return libres_[size_t(unidad)]; }
  bool Neutral() const { return neutral_; }          // H10: esta ronda todos luchan contra el rival neutral (duelo con b = kNeutral, sin dano a nadie)
  bool NeutralEn(int ronda) const { return EsNeutral(ronda); }
  int NeutralPct() const { return std::min(150, eco_->neutral_pct + eco_->neutral_pct_ronda * ronda_); }
  int Peso(int estrellas) const { return estrellas == 4 ? 27 : estrellas == 3 ? 9 : estrellas == 2 ? 3 : 1; }   // copias que vale (3 piezas de una estrella = 1 de la siguiente)
  int Valor(int p) const;                           // oro que devolveria vender todo lo que tiene el jugador
  int TopeTablero(int p) const { return eco_->tope_unidades[size_t(j_[p].nivel - 1)]; }
  int EnTablero(int p) const;
  std::vector<SinActiva> Sinergias(int p) const;

  // Acciones (solo en preparacion). Atomicas: o se hacen enteras o no cambian nada.
  Res Comprar(int p, int carta);                    // carta 0..tienda-1; combina solas 3 copias iguales
  Res Vender(int p, int slot);
  // SOLO PRUEBAS (pruebas.py): pone una pieza de `estrellas` en la primera casilla libre del tablero (-1 si no hay) y cambia sus estrellas
  // despues; las copias salen de la reserva, asi que el reparto de copias sigue cuadrando.
  int PonerPrueba(int p, int unidad, int estrellas);
  bool EstrellasPrueba(int p, int slot, int estrellas);
  Res Mover(int p, int de, int a);                  // banquillo/tablero; si a esta ocupado se intercambian
  Res Actualizar(int p);
  Res Bloquear(int p);                              // alterna el bloqueo de la tienda (gratis)
  int OroRacha(int racha) const;                    // oro extra por una racha (victorias o derrotas) de esa longitud
  Res ComprarExp(int p);
  Res ComprarCapsula(int p);                        // una capsula al azar del catalogo, al inventario
  Res Equipar(int p, int slot, int hueco, int capsula);   // del inventario al hueco 0/1 de la pieza; la que estaba vuelve al inventario
  Res Quitar(int p, int slot, int hueco);           // de la pieza al inventario
  void Listo();                                     // el jugador humano cierra la preparacion: nace el combate
  // Combate: quien presenta llama a Paso() de la simulacion; cuando Terminada(), TerminarCombate() aplica el resultado.
  sim::Sim& Combate() { return *sim_; }
  const std::vector<Combatiente>& Combatientes() const { return comb_; }
  void TerminarCombate();
  void SiguienteRonda();                            // tras el resultado: ingresos y nueva preparacion (o fin)
  int GanadorRonda() const { return ganador_ronda_; }   // del duelo del humano: 0 el humano, 1 el rival, -1 empate
  int DanoRonda(int p) const { return dano_[p]; }       // vida que perdio p en la ultima ronda
  int DanoRival() const { return dano_rival_; }         // dano que recibe el equipo 1 del duelo del humano (aunque sea una copia y no lo pierda nadie)

  // Guardado: solo en preparacion. Cargar valida todo (rangos, reserva, tamanos) y devuelve false con el motivo; no toca `*out` si falla.
  std::string Guardar() const;
  static bool Cargar(const std::string& texto, const Catalogo& cat, const Economia& eco, std::optional<Partida>* out, std::string* error);

  // Bot (mismas acciones y reglas que el jugador; solo ve su propio estado y la tienda). Tambien sirve de piloto automatico del humano en pruebas.
  void Jugar(int p);
  // SOLO PRUEBAS (tactics_equilibrio): fuerza el oro que el bot guarda cada ronda (-1 = lo decide el bot) y le hace colocar al reves. No se guarda ni cambia las reglas.
  void PruebaBot(int p, int reserva, bool colocar_mal) { reserva_prueba_[p] = reserva, colocar_mal_[p] = colocar_mal; }

 private:
  void Surtir(int p);
  void Emparejar();
  void EmparejarNeutral();
  bool EsNeutral(int ronda) const { return eco_->neutral_cada > 0 && ronda >= eco_->neutral_desde && (ronda - eco_->neutral_desde) % eco_->neutral_cada == 0; }
  void Retirar(int p);
  void Ingresos();
  std::vector<int64_t> Estado() const;
  bool Restaurar(const std::vector<int64_t>& v, std::string* error);
  void DevolverTienda(int p);
  void IniciarPreparacion();
  void Combinar(int p);
  void SubirNivel(Jugador& j);
  int Sitio(int p, int u) const;                    // slot libre del banquillo o -1
  void DarCapsula(int p);
  void Devolver(Jugador& j, int slot);              // todas las capsulas de una pieza al inventario
  void Absorber(Jugador& j, int queda, int se_va);  // las capsulas de `se_va` pasan a los huecos libres de `queda` y el resto al inventario
  int Puntua(int p, int u) const;                   // utilidad de una unidad para el bot
  sim::DefUnidad Def(int p, const Pieza& pz, const std::vector<SinActiva>& sin) const;

  const Catalogo* cat_;
  const Economia* eco_;
  uint64_t semilla_;
  int n_ = 2;
  Jugador j_[kMaxJug + 1];   // el ultimo (kNeutral) es el rival neutral de las rondas neutrales: solo su tablero
  bool neutral_ = false;
  std::vector<int> libres_;
  sim::Rng tienda_[kMaxJug], bot_[kMaxJug], azar_;
  int reserva_prueba_[kMaxJug] = {-1, -1, -1, -1, -1, -1, -1, -1};
  bool colocar_mal_[kMaxJug] = {};
  Fase fase_ = Fase::Preparacion;
  int ronda_ = 1, campeon_ = -1, ganador_ronda_ = -1, dano_[kMaxJug] = {}, dano_rival_ = 0, duelo_h_ = -1;
  std::vector<Duelo> duelos_;
  std::vector<sim::Resultado> res_;   // por duelo, tras Listo(): los del humano se rellenan al acabar su combate
  std::optional<sim::Sim> sim_;
  std::vector<Combatiente> comb_;
};

// Guardado en disco (ver Partida::Guardar): temporal + reemplazo con copia anterior (.bak); al leer, si la principal esta rota se prueba la .bak.
bool GuardarArchivo(const std::string& ruta, const std::string& texto, std::string* error);
bool CargarArchivo(const std::string& ruta, const Catalogo& cat, const Economia& eco, std::optional<Partida>* out, std::string* error);

}  // namespace dbz3::tactics

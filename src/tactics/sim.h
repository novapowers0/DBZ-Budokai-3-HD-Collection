// Dragon Tactics H2/T8: simulacion de combate pura. Sin graficos, sin reloj real y sin dependencias del juego: la presentacion
// (H1) solo lee los eventos y las unidades. Pasos fijos de 60 Hz; todo en enteros, asi que misma semilla + mismas ordenes = mismo
// resultado en cualquier maquina y a cualquier velocidad (x2/x4 = llamar a Paso() mas veces por fotograma).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dbz3::tactics::sim {

constexpr int kAncho = 6, kAlto = 6;             // tablero hexagonal 6x6 en coordenadas "odd-r" (las filas impares van medio hueco a la derecha)
constexpr int kHz = 60;
constexpr int kTopeTicks = 60 * kHz;             // 60 s logicos
constexpr int kInicioDesgaste = 45 * kHz;        // desde aqui TODAS las unidades pierden kDesgastePct de su vida maxima por segundo
constexpr int kDesgastePct = 2;
constexpr int kKiPorGolpe = 10, kKiPorRecibido = 5;
constexpr int kHisteresis = 1;                   // H13: el objetivo actual se conserva mientras ningun otro enemigo este mas de 1 casilla mas cerca
constexpr int kEsperaRetroceso = 15;             // H13: ticks que espera una unidad antes de deshacer el paso que acaba de dar
constexpr int kEsperaBloqueo = 6;                // ticks que espera una unidad sin camino antes de volver a pensar

// Hexagonos: casilla (col, fila) <-> cubo; vecinos en orden fijo; distancia en pasos.
struct Casilla { int col, fila; };
bool Dentro(Casilla c);
int Distancia(Casilla a, Casilla b);
int Vecino(Casilla c, int dir);                  // indice de casilla (fila*6+col) del vecino 0..5 o -1 si cae fuera

enum class Forma { Objetivo, Linea, Circulo };   // Linea: area casillas en la direccion hex del objetivo; Circulo: radio area alrededor del objetivo
enum class Pasiva { Ninguna, KiInicial, Furia, Perfora, Resistentes, Coraza, KiRegular, Remate, Regenera };

struct Tecnica { int dano = 200; int alcance = 2; Forma forma = Forma::Objetivo; int area = 0; };   // dano: % del ataque
struct DefUnidad {
  std::string id;
  int vida = 500, ataque = 40, defensa = 10, velocidad = 10;   // velocidad: casillas cada 10 s (paso = 600/velocidad ticks)
  int alcance = 1, ki_max = 100;
  int ki_inicial = 0;                                          // % extra de ki al empezar (capsulas), ademas de la pasiva KiInicial
  int ataque_ticks = 48, tecnica_ticks = 120;                  // duracion total de cada accion; el impacto cae al 40 % / 50 %
  Tecnica tecnica;
  Pasiva pasiva = Pasiva::Ninguna; int pasiva_valor = 0;       // porcentaje (KiRegular: ki por segundo)
  int voz_banco = 4, voz_ataque[3] = {42, 43, 50}, voz_ko[3] = {19, 20, 25};   // gritos (gestor de efectos del juego); solo sonido, no entra en la simulacion
};

// Azar propio con estado serializable (SplitMix64). Flujo(semilla, n) da flujos independientes: 0 combate, 1 cosmetico, 2.. tiendas/bots.
struct Rng {
  uint64_t estado = 0;
  uint64_t Siguiente();
  uint32_t Hasta(uint32_t n);                    // 0..n-1
};
Rng Flujo(uint64_t semilla, uint32_t n);

enum class Accion : uint8_t { Ninguna, Mover, Basico, Tecnica };
enum class Estado : uint8_t { Espera, Moviendo, Basico, Tecnica, Muerta };
enum class Tipo : uint8_t {
  Inicio,        // arranca la accion (Mover: unidad -> objetivo = casilla destino)
  Impacto,       // el golpe cae; una por victima, valor = dano (Mover no tiene Impacto)
  Fallo,         // el golpe cae donde ya no hay nadie a su alcance
  Recuperacion,  // la accion termino y la unidad vuelve a estar libre (Mover: llegada a la casilla)
  Muerte,        // unidad = la caida
  FinRonda       // valor = ganador (-1 empate)
};
struct Evento { uint32_t tick; Tipo tipo; Accion accion; int16_t unidad, objetivo; int32_t valor; };

struct Unidad {
  DefUnidad def; int equipo = 0;
  int casilla = 0, destino = -1;                 // destino: casilla reservada mientras se mueve
  int vida = 0, ki = 0, regenerado = 0;
  Estado estado = Estado::Espera;
  int objetivo = -1;
  uint32_t t_impacto = 0, t_fin = 0;             // ticks absolutos de la accion en curso (t_impacto 0 = ya cayo)
  bool viva() const { return estado != Estado::Muerta; }
};

struct Resultado { bool terminada = false; int ganador = -1; uint32_t ticks = 0; int vivas[2] = {0, 0}; int vida[2] = {0, 0}; };

class Sim {
 public:
  explicit Sim(uint64_t semilla);
  int Anadir(const DefUnidad& d, int equipo, Casilla c);   // equipo 0 abajo (filas 3-5), equipo 1 arriba (filas 0-2); -1 si no cabe
  void Paso();                                             // un tick de 1/60 s; no hace nada tras terminar
  bool Terminada() const { return fin_; }
  Resultado Resumen() const;
  uint32_t Tick() const { return tick_; }
  const std::vector<Unidad>& Unidades() const { return u_; }
  const std::vector<Evento>& Eventos() const { return ev_; }     // todos, en orden; la presentacion guarda cuantos ya leyo
  uint32_t Huella() const;                                       // FNV-1a de los eventos: dos rondas iguales dan la misma huella
  Rng& Combate() { return combate_; }
  Rng& Cosmetico() { return cosmetico_; }                        // para efectos visuales: nunca influye en el combate

 private:
  struct Golpe { int atacante, victima; bool tecnica; };
  void Emitir(Tipo t, Accion a, int u, int o, int v);
  int Elegir(int u, int* paso);                                  // objetivo y primer paso (casilla) hacia el; paso -1 = ya en alcance
  void Decidir(int u);
  void Empezar(int u, Accion a, int obj);
  void Golpear(const std::vector<Golpe>& lote);
  int Calcular(const Unidad& a, const Unidad& v, bool tecnica) const;
  void Morir(int u);
  void Comprobar();

  std::vector<Unidad> u_;
  std::vector<Evento> ev_;
  Rng combate_, cosmetico_;
  uint32_t tick_ = 0;
  bool fin_ = false;
  int ocupa_[kAncho * kAlto];                                    // id de unidad que ocupa o reserva la casilla, -1 libre
  std::vector<bool> retro_;                                      // por unidad: ya espero antes de volver atras
  std::vector<int> ant_;                                         // por unidad: casilla que acaba de dejar (para no volver atras)
  std::vector<int> pienso_hasta_;                                // por unidad: tick antes del que no vuelve a pensar (bloqueo)
};

}  // namespace dbz3::tactics::sim

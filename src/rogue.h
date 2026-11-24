// dbz3 - Dragon Rogue (ROG-02): la pausa "elige 1 de 3". Diseno: studies 5/ENTREGAS/ROGUE_DISENO.md 1.1-1.3.
// La partida (ROG-01) sigue declarada en modos.h; aqui solo lo de las cartas. Las cartas elegidas viven en rogue.cpp y
// se guardan en rogue.toml ([partida] cartas / cambios); `semilla` es la de la partida (Run.semilla de rpg.cpp): con
// otra semilla la lista se vacia sola (partida nueva).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include "ingame/game_style.h"
#include "modos.h"

namespace dbz3::rogue {
// Reglas del combate siguiente: la suma de las cartas elegidas y de las sinergias, con topes (dano x3, recibido x0,4,
// 7 capsulas).
// ROG-03: si el combate `ganados` (desde 0) es de jefe, lleva ademas su armadura y su dano; y cada tramo superado
// sube el dano recibido.
dbz3::modos::Reglas ReglasPartida(uint32_t semilla, int jugador, int ganados);
// Vida extra al ganar el combate numero `ganados` (cartas "recupera" y sinergia Apoyo x4; tras un jefe, toda), ademas
// de la de serie de rpg.cpp.
int VidaAlGanar(uint32_t semilla, int ganados);
// ROG-03: aviso de jefe antes del combate `ganados` (desde 0). true = es un jefe y la pantalla ya esta dibujada.
bool AvisoJefe(ImDrawList* dl, const dbz3::ingame::style::Area& a, uint32_t semilla, int jugador, int ganados);
// Pantalla de la eleccion tras ganar el piso `ganados` (hilo de dibujo). `pulsado` = botones recien pulsados (mascara
// del mando, como rpg.cpp). Devuelve 0 = nada que elegir (sigue la pantalla de siempre), 1 = eligiendo (ya dibujada),
// 2 = algo cambio (carta, salto o cambio de cartas; *vida / *vida_max al dia): hay que guardar la partida.
int Eleccion(ImDrawList* dl, const dbz3::ingame::style::Area& a, uint16_t pulsado, uint32_t semilla, int ganados,
             int* vida, int* vida_max);
// ROG-06/07 (rogue_resultado.cpp): Zenie del final, dia UTC (DBZ3_TEST_DIA), semilla y puntuacion del reto diario.
int ZeniFin(int ganados, int jefes, bool completo);
int DiaUtc();
uint32_t SemillaDiaria(int dia);
int PuntosDiario(int ganados, int jefes, int vida, int segundos);

// ROG-05 (rogue_caminos.cpp): los 7 caminos de res/datos/rogue_caminos.toml, en el orden de la pantalla 01.
struct Camino {
  std::string id, arte, nombre[5], texto[5], clasico, clasico_ii, vida, rivales, pausa, maldicion;   // arte: emblema camino_<arte>.png (por defecto, el id)   // nombre/texto: es en fr de it
  std::vector<int> escalera;
  int combates = 0, cada_jefe = 5, cura = 0, cura_jefe = 9999, tiempo = 0, regen = 0, premio = 0;   // combates 0 = sin fin
  int vida0 = 2000, vida_paso = 250, abre = 0;
  bool abierto = false;
  bool en_orden = false;   // POST3 (Kumite II): las reglas de los jefes van en su orden (1.o, 2.o, 3.o), sin el arranque por semilla
  std::vector<std::string> reglas_tramo;   // ROG-05 (r23): ids de [maldiciones]: el tramo 1 (combates 6-10) lleva la 1.a, el 2 la 2.a... en ciclo
};
std::vector<Camino> Caminos();
Camino CaminoDe(const std::string& id);   // el de ese id (o El Abismo si no existe)
// r23: combates que faltan para el proximo jefe (1 = el siguiente combate es jefe; 0 = no hay mas) y la linea del HUD de rpg.cpp
// ("Piso 4 - jefe en 1"). `ganados` = combates ya ganados.
int ProximoJefe(int ganados);
std::string TextoHud(int ganados);
// Mejor marca del modo antiguo `clasico` (tipo de hub_modos.toml) para ese luchador; 0 si no hay. Solo lee.
int RecordClasico(const std::string& clasico, int jugador);
// Torre de la semana: la maldicion de esta semana UTC (DBZ3_TEST_SEMANA), de [maldiciones] de rogue_caminos.toml.
struct Maldicion {
  std::string id, nombre[2];   // es, en
  std::vector<int> rivales;
  int tiempo = 0, regen = 0, ki = 0, recibido = 100, armadura = 0;
};
Maldicion MaldicionSemana();
Maldicion MaldicionDe(const std::string& id);   // r23: una de [maldiciones] por su id (la regla por tramo de El Abismo)
// Pantalla 01 (maqueta ROGUE_VISUAL/01_elegir_camino.png), en el selector de rpg.cpp: caminos, record Rogue y clasico,
// luchador y Zenie. `a_medias` = hay una partida de Rogue (solo se sigue la de su camino). Devuelve 0 = nada,
// 1 = empezar o seguir (el camino ya esta puesto con SetCamino), 2 = salir, 3 = Retos clasicos.
int PantallaCaminos(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t pulsado, int jugador, bool a_medias,
                    int zeni, const char* aviso);
// rogue.cpp: camino de la partida (el de la partida a medias, o el elegido para la siguiente) y records por camino.
void SetCamino(const std::string& id);
std::string CaminoActual();
int RecordCamino(const std::string& camino, int jugador);   // jugador -1 = el mejor de todos los luchadores
// PM-24: dificultad I/II/III (0..2) de la partida siguiente (o la de la partida a medias): vida del rival +500 y dano
// recibido +20 % por nivel, premio +50 %. II y III se abren venciendo el piso 10 del nivel anterior (o con DBZ3_TEST_ROGUE).
void SetDificultad(int d);
int Dificultad();
bool DificultadAbierta(const std::string& camino, int d);   // d = 1 o 2
bool CaminoAbierto(const Camino& c);                        // abierto de salida, o `abre` pisos de record (o pruebas)
// ROG-04 (rogue_evoluciones.cpp): tabla de formas real del juego (char372). Transformaciones = cuantas tiene el
// luchador; FichaFusion = su ficha de fusion nativa (64-104) y su forma fusionada, o -1; EvolucionDe = forma inicial,
// luchador (fusion; -1 = el suyo) y capsulas que exige, para `paso` transformaciones de las cartas.
struct Evolucion {
  int id = -1, forma = 0;
  std::vector<int> caps;
};
int Transformaciones(int jugador);
int FichaFusion(int jugador, int* forma);
Evolucion EvolucionDe(int jugador, int paso, bool fusion);

// ---- ROG-06/07 (rogue_resultado.cpp): pantalla 05 (resultado), 06 (Entrenamiento de Kami), 07 (reto diario) y "ver mis
// mejoras". Compras, hitos y reto diario van en rogue.toml ([kami], [diario], [[dia]]) por los enganches de rogue.cpp.
int PisosDePrueba();   // rogue.cpp: DBZ3_TEST_ROGUE (0 = juego normal)
enum class Accion { kNada, kDibujada, kZenie, kJugarDiario, kCerrar, kOtraVez, kKami };
void AbrirKami();
void AbrirDiario();
void AbrirMejoras(uint32_t semilla, int jugador, int ganados);
// Pantalla abierta con Abrir* (hilo de dibujo, rpg::Dibujar): kNada = ninguna abierta; kZenie = se gasto Zenie de *zeni
// (guardar rpg.toml); kJugarDiario = empezar la partida del dia (rpg.cpp: Empezar(kRogue), que llama a PartidaNueva).
Accion Pantalla(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t pulsado, int* zeni);
// Pantalla 05 tras Fin: kDibujada; kCerrar (A/B), kOtraVez (X), kKami (Y); kNada = sin datos (sale la de siempre).
Accion Resultado(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t pulsado);
// Partida nueva (rpg.cpp Empezar): la del dia si se pidio (semilla y luchador del dia); si no, las ventajas de Kami.
void PartidaNueva(uint32_t* semilla, int* jugador, int* vida, int* vida_max);
int JugadorDiario(int dia);
bool CaminoComprado(const std::string& id);   // comprado en Kami (ROG-05: sumarlo a CaminoAbierto)
struct FilaRanking {
  std::string pos, nombre, valor, extra;
  int medalla = 0;
};
std::vector<FilaRanking> RankingRogue(std::string* dice);   // pestana "Dragon Rogue" del Ranking (modos.cpp)
// Enganches que llama rogue.cpp con su candado puesto.
struct DatosFin {
  int jugador = 0, ganados = 0, jefes = 0, record_antes = 0, vida = 0, zeni = 0;
  bool completo = false;
  int sellos[4] = {};
  uint32_t semilla = 0;
  std::string camino;   // PM-24: camino y dificultad de la partida (para abrir el nivel siguiente)
  int dif = 0;
};
void AlTerminar(const DatosFin& d);
std::string TextoExtra();
void CargarExtra(const std::string& texto);
bool HayExtra();
bool CartaBloqueada(const std::string& id);   // carta con precio/hito aun sin abrir: no sale en la oferta
int CambiosExtra();
bool CartaInicialActiva(uint32_t semilla);   // PM-24: ventaja de Kami "empieza con 1 carta comun" (no vale en el reto diario)
int SaltoExtra();   // % de vida extra al saltar (ventaja de Kami)
// rogue.cpp, sin candado puesto: cartas de la partida de *semilla (nullptr = toda la baraja, veces 0) y guardar ya.
struct CartaVista {
  std::string id, nombre, texto;
  int cat = 0, estrellas = 1, max = 1, veces = 0;
};
std::vector<CartaVista> VerCartas(const uint32_t* semilla);
void GuardarYa();
void PruebaRareza();   // DBZ3_TEST_ROGUE_RAREZA: reparte 3000 ofertas con y sin peso y lo dice en el registro
}  // namespace dbz3::rogue

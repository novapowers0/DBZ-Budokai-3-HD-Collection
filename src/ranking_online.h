// dbz3 - RANK-ONLINE: ranking mundial de Dragon Budokai Survivors y del reto diario de Dragon Rogue (BETA, apagado de serie:
// dbz3_ranking_online). Estudio y maquetas: studies 5/ENTREGAS/RANKING_ONLINE.md. El juego publica la partida firmada
// (HMAC-SHA256) en un tema de ntfy con un GET (HttpGetUrl, como el matchmaking); una GitHub Action
// (tools/ranking_online/) la valida y escribe los JSON que el juego descarga de raw.githubusercontent.com con cache
// (user_data/dbz3/ranking_cache/). Sin red, la partida queda en user_data/dbz3/ranking_pendientes.txt y se reenvia
// sola hasta verla en recibidos.json. Todo se dibuja en el hilo de dibujo; la red va en un hilo propio.
#pragma once

#include <cstdint>
#include <string>

#include <imgui.h>

#include "ingame/game_style.h"

namespace dbz3::rank {

bool Activo();   // dbz3_ranking_online

// Partida terminada que se puede enviar.
struct Partida {
  char modo = 0;   // 'r' = reto diario de Dragon Rogue (solo el intento puntuable), 's' = Dragon Budokai Survivors; 0 = ninguna
  int jugador = 0, segundos = 0, puntos = 0;
  int dia = 0, ganados = 0, jefes = 0, vida = 0;   // 'r'
  uint32_t semilla = 0;
  int bajas = 0, nivel = 0, dificultad = 0;   // 's'
  bool victoria = false;
  std::string escenario;   // 's': id del escenario
  std::string lugar;       // texto de la barra: el camino o el escenario
};
int PuntosSurvivors(int segundos, int bajas, bool victoria);   // = puntos_surv de tools/ranking_online/validar.py

constexpr uint16_t kBotonX = 0x4000, kBotonY = 0x8000;   // mascara del mando (XInput)
// Pantalla final (Fin del camino / resultados de Survivors), antes de mirar los botones: quita los que usa el bloque
// (eligiendo alias: A, B, izquierda y derecha; si no, `boton` = enviar / reintentar / ver ranking).
uint16_t Entrada(uint16_t pulsado, const Partida& p, uint16_t boton);
// Bloque "Ranking mundial" (maquetas 04*): p0 = esquina de arriba a la izquierda, w = ancho, en px de la maqueta
// 1280x720; u = px de pantalla por px de maqueta. ~125 de alto.
void Bloque(ImDrawList* dl, ImVec2 p0, float w, float u, const Partida& p);
// Ayuda de la pantalla final: texto del boton de enviar (nullptr = sin boton).
const char* EtiquetaBoton();
bool EligiendoAlias();   // cuadro del alias abierto (A elige, B pasa)
bool Consultando();      // aviso de privacidad abierto (A = Si, B = No)
// Pantalla abierta desde el bloque ("Ver ranking") o desde una tabla (ficha de una partida): true = dibujada encima.
bool Encima(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t pulsado);
// Paginas del Ranking (modos.cpp): 0 = Dragon Budokai Survivors mundial, 1 = Dragon Rogue diario mundial. Dibuja todo menos el
// anfitrion. Devuelve true si se quedo con B (cerraba una ficha). Titulo de la pagina en TituloTabla.
bool Tabla(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t pulsado, int cual);
const char* TituloTabla(int cual);
// Reto diario (pantalla 07 de rogue_resultado.cpp): panel derecho "Mundial - hoy" en p0..p1 (false = el jugador pidio
// "Mis dias" con Y: va el panel de siempre) y la caja de abajo a la izquierda (estado del envio y puesto de hoy).
bool PanelDiario(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float u, uint16_t pulsado, int dia, int jugador, const std::string& camino);
void CajaDiario(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float u, int dia, const std::string& codigo);

}  // namespace dbz3::rank

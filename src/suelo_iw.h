// dbz3 - Suelo real de los campos de Infinite World (src/suelo_iw.cpp; frente R11, 2026-10-16).
// Con un campo de IW de tools/campos_iw.py cargado (alta 5060+n en el registro anfitrion), suelo y paredes salen de la
// colision #WCF del campo, como en los minijuegos a pie de IW (rayo de suelo desde la cabeza y barrido contra paredes).
// DBZ3_SUELO_PAREDES=0 quita las paredes. US y EU.
#pragma once

#include <cstdint>
struct PPCContext;

namespace dbz3::suelo_iw {

// Altura del suelo mas alto del campo de IW cargado en (x, z) (coordenadas y unidades de IW = las del combate);
// `sin_suelo` si no hay campo de IW cargado, aun se esta leyendo o en (x, z) no hay suelo.
float Altura(float x, float z, float sin_suelo);
// EU299A70 + ObjectsCountEffect: conservar el desfase del punto seguido
// cuando jugador/referencia quedan en copias distintas de la misma rejilla.
// Solo cambia la representacion periodica; no cambia coeficientes de camara.
void EnvolverReferencia(float& x, float& z, float jugador_x, float jugador_z);
// Periodos del campo ACTUAL; cero fuera de MSNMW o sin el candidato activo.
void Periodos(uint8_t* base, float& x, float& z);
// Enganche del dibujante AWO: copias en el MISMO pase nativo (antes del resolve).
bool DibujarRejillaNativa(PPCContext& ctx, uint8_t* base, void (*dibujar)(PPCContext&,uint8_t*));
// Tipo de superficie del #WCF bajo (x, z), la cara mas alta (u16 en +6 del grupo; EU Collision_RaycastVsModel lo deja en
// +0x25A y MiniGame_StoreAnimFrame en +0x1E8). 1 suelo, 2 pared, 3 y 4 los que queman en Namek (Char_CheckActionAllowed2 /
// Char_CheckActionAllowed). 0 sin campo.
int TipoSuelo(float x, float z, float tope = 1e30f);
// Tipo de la cara mas cercana que toca la esfera de centro (x, y, z) y radio r; 0 si no toca nada (EU 0x296A70, +0x1F0).
int ToqueEsfera(float x, float y, float z, float r);
// Rayo de `desde` a `hasta` contra la colision del campo (mascara de materiales del #WCF: 1 la de la camara de IW,
// Camera_UpdateFollow; 7 la del luchador): true y el punto de corte mas cercano a `desde`. Para la camara del
// minijuego (P4), como IW: rayo del jugador a la camara y, si corta, la camara en el corte:
//   float corte[3]; if (dbz3::suelo_iw::Rayo(obj, ojo, 1, corte)) std::copy(corte, corte + 3, ojo);
bool Rayo(const float desde[3], const float hasta[3], uint32_t mascara, float corte[3]);
// Paso como Collision_MoveSphere de IW (minijuegos a pie): de `desde` a `hasta` (pies, coordenadas de IW; solo cuenta el
// paso en x/z), la esfera de radio 4 x escala (calibrado; IW: H - 1) 8 por encima de los pies desliza por las paredes del
// #WCF (mascara 2); H (la cabeza del luchador de B3 que esta en `desde`) da el inicio del rayo de suelo. `salida` = sitio final, con la y del suelo bajo el (rayo desde la
// cabeza). escala: 1 al andar; 0,5 en el scatto (estados 0x14-0x16 de IW), 1,8 cayendo (Char_GetActionSpeedScale).
// false (y salida = hasta) sin campo de IW cargado. Para P4 (modo Piccolo, sitio llevado a mano):
//   float s[3]; dbz3::suelo_iw::Mover(a0, a1, s); propio = {s[0], 0, s[2]};
bool Mover(const float desde[3], const float hasta[3], float salida[3], float escala = 1.0f);

// Suelo fijo (frente R12, vuelo de IW en los campos 883/881): mientras on, la consulta de suelo del juego (sub_820CB0A0)
// responde `altura` en todo el escenario, asi el luchador de B3 se queda a esa altura "en el suelo" (IW vuela a 400).
// Cualquier hilo; el que lo pone lo quita al acabar.
void SueloFijo(float altura, bool on);

// Adaptacion de los dos actores aereos de MSN009: cada consulta conserva la altura
// del actor mas cercano en XZ; no impone a Krilin la altura cambiante de Freezer.
void SueloActores(const float a[3], const float b[3], bool on);

}  // namespace dbz3::suelo_iw

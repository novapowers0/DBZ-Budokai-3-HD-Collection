// dbz3 - Dibujo con el aspecto de los menus de Budokai 3 (marco naranja, filas con letras
// turquesa perfiladas, cinta del titulo, bocadillo). Lo usan las pantallas propias que se
// meten dentro del juego (ONLINE en el Duelo, TAG TEAM en el menu principal).
// Piezas: ingame_ui/<nombre>.png, que roster_build.ui_native saca de los AFS del jugador (marco,
// bocadillo, botones, fondos, anfitriones) y genera (titulos con la letra del juego). Un mod puede
// cambiarlas (mods/<mod>/ui_ingame/). Sin ellas se dibujan sustitutos.
#pragma once

#include <imgui.h>

#include <string>
#include <utility>
#include <vector>

namespace dbz3::ingame::style {

// Area 4:3 donde dibuja el juego (centrada en la imagen del juego) y escala respecto a 1600x900.
struct Area {
  ImVec2 p0, p1;
  float s = 1.0f;   // 1.0 = area de 900 px de alto
  ImVec2 At(float x, float y) const { return ImVec2(p0.x + x * s, p0.y + y * s); }   // en px de 1200x900
};
Area GameArea(const ImGuiIO& io);
// S1-F06: rectangulo de la imagen del juego (16:9) en la ventana, como la coloca el presentador del SDK
// (rexglue-sdk ui/presenter.cpp, present_letterbox = dbz3_aspect "keep"): cabe entera, con bandas arriba y abajo en
// una ventana mas alta que 16:9 (4:3, 16:10, Steam Deck) y a los lados en una mas ancha; con "stretch" llena la
// ventana. Antes el area 4:3 se calculaba con el alto de la VENTANA: en 4:3 todo el HUD propio salia un 33 % mas
// grande y fuera de la imagen. Lo que proyecta el 3D (ps2::CamaraCombate) o centra en pantalla debe usar esto.
struct Imagen {
  ImVec2 p0, p1;
  ImVec2 Size() const { return ImVec2(p1.x - p0.x, p1.y - p0.y); }
};
Imagen GuestImage(const ImGuiIO& io);

// Imagen <nombre>.png: primero la de un mod activo (mods/*/ui_ingame/), si no la de serie
// (ingame_ui/). Sin ninguna, w = 0. Hilo de dibujo.
struct Pic {
  ImTextureID tex{};
  float w = 0, h = 0;
  explicit operator bool() const { return w > 0; }
};
Pic Img(const char* name);
// Recorte `src` (px de la textura: x0, y0, x1, y1; todo ceros = entera) en p0..p1.
void DrawPic(ImDrawList* dl, const Pic& p, ImVec2 p0, ImVec2 p1, ImVec4 src = ImVec4(0, 0, 0, 0),
             ImU32 tint = IM_COL32_WHITE, bool mirror = false);

void Frame(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float s);                 // marco de las listas
void Highlight(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float t);             // franja de la fila elegida
// max_w > 0: la letra se reduce hasta que el texto quepa en ese ancho (px de pantalla).
void MenuText(ImDrawList* dl, ImVec2 center, float size, const char* text, bool selected, bool disabled,
              float max_w = 0);
// C4: texto como la bandeja "Custom Tray" / Edit Skills del juego (medido 22/10: relleno blanco 255,255,255 y perfil casi negro
// 17,12,8); elegido = amarillo, apagado = gris. El turquesa de MenuText es el de las filas del selector.
void MenuTextB3(ImDrawList* dl, ImVec2 center, float size, const char* text, bool selected, bool disabled, float max_w = 0);
// Cinta del titulo arriba a la izquierda, como la del Duelo; `title` = imagen del titulo
// (ingame_ui) o, sin ella, `text` con la fuente.
void Banner(ImDrawList* dl, const Area& a, const char* text, const char* title = nullptr);
// Titulo en letras doradas con perfil (las del Banner), centrado en (cx, cy) del area 1200x900 y encogido para que
// quepa en `ancho` unidades: sobre el papel de una cinta que ya dibuja el juego. `alpha` 0..1. X1: sustituye al logo/PNG.
void TitleText(ImDrawList* dl, const Area& a, float cx, float cy, float ancho, const char* text, float escala = 1.0f);
// Bocadillo; tail_x = donde sale el pico (0..1 del ancho, hacia arriba) o < 0 sin pico.
void Speech(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float s, const char* text, float tail_x = 0.75f);
// Opcion "Bordes dorados" (dbz3_ui_side_borders): con ella, tiras y fondos 4:3 como el juego.
bool Borders();
// Fondo: dibujo del juego `image` (ingame_ui) o degradado. Sin bordes o con una imagen ancha, a toda la ventana.
void Backdrop(ImDrawList* dl, const Area& a, const char* image = nullptr);
// Anfitrion que habla (como los Kaio del Duelo): esquina de arriba en (x, y) px de 1200x900, al
// tamano del juego; lo que sale del area se corta. Despues va Sides (los tapa, como en el juego).
void Host(ImDrawList* dl, const Area& a, const char* image, float x, float y, bool mirror);
// Tiras doradas de los lados del area (las de los menus del juego).
void Sides(ImDrawList* dl, const Area& a);
// Boton del mando con el dibujo del juego: "A", "B", "X", "Y", "LB", "LT", "RB", "RT", "BACK"
// (= SELECT), "START". pos = borde izquierdo, centro vertical. Devuelve el ancho usado.
float Glyph(ImDrawList* dl, const char* button, ImVec2 pos, float h);
// Linea de ayuda centrada: boton + texto, ... (como "BACK : Help" de los menus).
void HelpLine(ImDrawList* dl, ImVec2 center, float h, const std::vector<std::pair<const char*, const char*>>& items);
// Flechas izquierda/derecha del juego a los lados de p0..p1 (selector de formato).
void Arrows(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float h, float t);

// Esquema comun de las pantallas de modo (retos del HUB, tienda, ranking, final...), en px del area 1200x900, como
// el Duelo del juego: cinta del titulo arriba a la izquierda (Banner), anfitrion abajo a la izquierda en (kHostX,
// kHostY) con la cara hasta x ~330, el contenido en la columna kX0..kX1 entre kTop y kBottom (no le tapa la cara),
// el bocadillo abajo a su derecha con el pico hacia el (Bubble) y la ayuda de botones abajo centrada (Help).
// Entre dos marcos, kGap (el marco del juego sobresale 17 px). Letras: kRow en las listas, kHead en la cabecera
// de un marco, kSmall en las tablas. La esquina de arriba a la derecha (kCorner*) queda para la caja de Zenie o
// el selector de pagina.
namespace mode {
constexpr float kHostX = -68, kHostY = 319;   // el Kaio del Duelo (437 px de alto a 1.33) pegado abajo
constexpr float kX0 = 390, kX1 = 1090, kCx = (kX0 + kX1) * 0.5f, kTop = 172, kBottom = 600, kGap = 46;
constexpr float kRow = 30, kHead = 34, kSmall = 26;
constexpr float kCornerX0 = 650, kCornerY0 = 40, kCornerY1 = 120;
// Dentro de un marco (Frame p0..p1): el filo de dentro de su linea queda kLine dentro (medido a 1600x900); las filas
// empiezan kPad dentro por arriba y por abajo (como el Duelo: la franja no pisa la linea) y el texto cabe en
// (x1 - x0) - 2 * kText.
constexpr float kLine = 18, kPad = 22, kText = 34;
// HUD de los modos en combate: un solo bloque centrado en x 600 ABAJO (pedido del usuario: lejos del reloj y del
// contador nativo de rivales, que van arriba). El marcador del modo (kHudY, alto kHudH) pegado al borde de abajo y
// sus lineas de reglas (como mucho 2: "¡Aguanta!"/modificadores y las de rpg::Hud) encima, cada kHudLine desde
// kHudInfoY.
constexpr float kHudH = 58, kHudLine = 28, kHudY = 900 - kHudH - 36, kHudInfoY = kHudY - 16 - 2 * kHudLine;
// Franja de la fila elegida dentro del marco x0..x1 (px del area): sin pisar su linea, como la del juego.
void Stripe(ImDrawList* dl, const Area& a, float x0, float y0, float x1, float y1, float t);
void Bubble(ImDrawList* dl, const Area& a, const char* text);
void Help(ImDrawList* dl, const Area& a, const std::vector<std::pair<const char*, const char*>>& items);
}  // namespace mode

// Tira de un equipo sobre la rueda del select (Tenkaichi 3): `slots` casillas numeradas, las
// elegidas con la cara y el nombre. mirror = lado derecho (jugador 2). active = eligiendo.
void TeamStrip(ImDrawList* dl, ImVec2 p0, float s, bool mirror, int slots, const std::vector<std::string>& names,
               const std::vector<ImTextureID>& faces, bool active, bool done, float t);
// Ficha de un compañero del Tag Team bajo el marcador del juego (estilo FighterZ/Tenkaichi 3):
// cara (la del marcador del juego; sin ella, la inicial), nombre y barra de vida. mirror = lado del jugador 2 (crece hacia la izquierda).
// button = boton que lo hace entrar ("BACK" o "BACK+LB") o nullptr; switching = parpadeo del relevo.
void PartnerCard(ImDrawList* dl, ImVec2 p0, float s, bool mirror, const char* name, ImTextureID face, float hp,
                 bool down, const char* button, bool switching, float t);

}  // namespace dbz3::ingame::style

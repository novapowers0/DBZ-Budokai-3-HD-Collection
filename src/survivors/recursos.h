// Dragon Budokai Survivors: arte propio (PNG de GPT, alfa recto) cargado a textura y ayudas de dibujo sobre ImDrawList.
// Carpetas junto al exe: res/mejoras/ (biblioteca comun con Dragon Rogue: iconos/<id>.png, ui/<pieza>.png; solo se lee) y
// res/survivors/ (03_fondos/<escenario>/fondo_<escenario>_{suelo,medio,lejano}.png y survivors.toml).
// Peticiones y texturas GPU en el HILO DE DIBUJO; lectura/decodificacion en dos trabajadores.
// Cache por ruta; mientras se prepara devuelve nullptr; lo que falta se registra una vez.
#pragma once

#include <filesystem>

#include <imgui.h>

#include "../ps2_modelo.h"

namespace dbz3::survivors {

std::filesystem::path RutaRecursos();   // <exe>/res/survivors (registro de error una vez si falta)
std::filesystem::path RutaMejoras();    // <exe>/res/mejoras

const ps2::Tex* Textura(const std::filesystem::path& png);   // nullptr si pendiente, ausente o invalido; reintentar en el siguiente dibujo
const ps2::Tex* Icono(const char* id);                        // res/mejoras/iconos/<id>.png (ki_01, fisica_03, utilidad_12, evolucion_05...)
const ps2::Tex* Ui(const char* pieza);                        // res/mejoras/ui/<pieza>.png (ui_tarjeta_normal, ui_casilla_mejora...)
const ps2::Tex* Fondo(const char* escenario, const char* capa);   // ("llanura", "suelo" | "medio" | "lejano")
const ps2::Tex* Arte(const char* rel);                        // res/survivors/<rel>: arte de la tanda 3 ("01_recogibles/recogible_iman.png"...)
const ps2::Tex* ArteMejoras(const char* rel);                 // res/mejoras/<rel> (solo lectura: "rogue/ui_candado.png")
void LiberarTexturas();                                       // al cerrar el modo (~25 MB de GPU por escenario: 3 capas de 1920x1080 RGBA)

// ---- tanda 3 (metadatos_tanda3.json): hojas de cuadros, piezas giradas y cifras
// Cuadro `cuadro` de una hoja de `cuadros` celdas en una fila (la imagen entera con cuadros = 1) con su punto `ancla` (px de la celda,
// el mismo en todos los cuadros: no se centra cada uno) en `p`; `escala` = px de pantalla por px del PNG; espejo = volteo sobre el ancla.
void DibujarCuadro(ImDrawList* dl, const ps2::Tex* t, int cuadro, int cuadros, ImVec2 ancla, ImVec2 p, float escala, ImU32 col = IM_COL32_WHITE,
                   bool espejo = false);
// Lienzo entero girado `ang` rad (sentido horario en pantalla) alrededor de su centro c, con medias medidas `semi`; despues se multiplica la
// y por `aplastar` (proyeccion al suelo: sin(picado); 1 = pieza de pantalla). Para el octavo de anillo y las flechas del borde.
void DibujarGirada(ImDrawList* dl, const ps2::Tex* t, ImVec2 c, ImVec2 semi, float ang, float aplastar, ImU32 col = IM_COL32_WHITE);
// Numero con las cifras de ui_dano_cifras_{blancas,amarillas} (celdas 64x64, cada una con su ancho natural): centrado en x con la linea
// base (y = 60 del PNG) en `base`; `alto` = alto de la tinta en pantalla (56 px del PNG). Devuelve el ancho dibujado.
float DibujarCifras(ImDrawList* dl, bool amarillas, int valor, ImVec2 base, float alto, ImU32 col = IM_COL32_WHITE);

// Imagen estirada a [a, b] / escalada proporcional y centrada dentro de [a, b] (tarjetas, retratos, medallas, iconos).
void DibujarImagen(ImDrawList* dl, const ps2::Tex* t, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE);
void DibujarAjustada(ImDrawList* dl, const ps2::Tex* t, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE);
// Panel de nueve secciones (ui_boton_*, ui_ventana_dialogo, ui_panel_resultados, ui_tooltip) con los margenes de
// metadatos_ui.json; `escala` = px de pantalla por px del PNG en las esquinas. false si la pieza no tiene margenes o falta.
bool DibujarNueve(ImDrawList* dl, const char* pieza, ImVec2 a, ImVec2 b, float escala = 1.0f, ImU32 col = IM_COL32_WHITE);
// Lo mismo con una textura cualquiera y sus margenes (izquierda, arriba, derecha, abajo; px del PNG entero): piezas de la tanda 4
bool DibujarNueveTex(ImDrawList* dl, const ps2::Tex* t, float l, float tp, float r, float bt, ImVec2 a, ImVec2 b, float escala = 1.0f,
                     ImU32 col = IM_COL32_WHITE);
// Barra "vida" | "ki" | "exp": relleno recortado al porcentaje dentro de rect_interior y el marco encima (misma caja [a, b]).
void DibujarBarra(ImDrawList* dl, const char* tipo, ImVec2 a, ImVec2 b, float pct, ImU32 col = IM_COL32_WHITE);
// Suelo repetible en x e y que cubre [a, b]: `camara` = posicion de la camara en px de pantalla (mundo * px por unidad), que lo
// desplaza; `tesela` = lado en px de pantalla de una repeticion del PNG (1920 x 1080 -> tesela x tesela*1080/1920).
void DibujarSuelo(ImDrawList* dl, const char* escenario, ImVec2 a, ImVec2 b, ImVec2 camara, float tesela);
// Capa de adorno "medio" o "lejano" (centro transparente) cubriendo [a, b] sin deformar (recorta lo que sobre). Orden de
// metadatos_ui.json: suelo, lejano, medio.
void DibujarCapa(ImDrawList* dl, const char* escenario, const char* capa, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE);

}  // namespace dbz3::survivors

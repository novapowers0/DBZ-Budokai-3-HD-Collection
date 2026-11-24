// dbz3 - Modo Heroe, aspecto nuevo (HEROE-V04): menu de deseos de Shenron en la historia del Heroe (maqueta 08_shenron).
// El menu (marco, filas, cursor, A) es el NATIVO de B3 (pantalla 11; campaigns.cpp lo vigila y avisa cada cuadro a
// heroe_ui::AvisarMenuDeseo). Nada lo tapa ni lo sustituye: ImGui pinta siempre ENCIMA de la imagen del juego, asi que el
// panel solido de la maqueta (detras de las filas) no se puede poner desde aqui. Solo se anade, alrededor del marco nativo
// (que va abajo en el centro, x 400..1180 del lienzo): titulo "Pide un deseo" con BETA y las 7 Esferas arriba, la linea
// "Detalles" de la fila elegida bajo el titulo y la ayuda abajo a la derecha. La cabeza de Shenron (x 670..1100, arriba)
// queda libre.
// Se incluye en modos.cpp (fuera de sus espacios de nombres); lo llama ModosPanel::OnDraw cada cuadro. Kit: heroe_ui_v2.h.
#pragma once

#include "heroe_ui.h"
#include "heroe_ui_v2.h"
#include "launcher/i18n.h"

namespace dbz3::heroe_ui::v2 {

// HEROE-V05: el 12/11 el menu de deseos se vio sin estos anadidos; estas dos lineas de registro (una vez cada una)
// dicen si es porque el panel de modos estaba "tapado" (GameDimmed / OverlayOpen) o porque no se llega a dibujar.
inline void DeseosTapados() {
  int fila = 0, filas = 0;
  static bool dicho = false;
  if (!dicho && MenuDeseo(&fila, &filas) && Activa()) {
    dicho = true;
    REXLOG_INFO("dbz3 heroe: menu de deseos v2: NO se dibuja (panel tapado)");
  }
}

inline void DibujarDeseos(ImDrawList* dl) {
  int fila = 0, filas = 0;
  if (!MenuDeseo(&fila, &filas) || !Activa()) return;   // apagado o sin menu: nada (ni se lee el ajuste)
  if (static bool dicho = false; !dicho) {
    dicho = true;
    REXLOG_INFO("dbz3 heroe: menu de deseos v2: dibujado");
  }
  namespace T = dbz3::i18n;
  const S::Area q = LienzoImagen();
  // HEROE-V05: como la maqueta, el titulo y las 7 Esferas van en una banda PEGADA a la parte de arriba del marco nativo (que
  // se ve en x ~410..1170, y ~580..850 del lienzo), no como pergamino arriba a la izquierda; sin el cuadro "Detalles" (la
  // maqueta no lo tiene: lo que hace cada fila ya lo dice su nombre).
  // POST 12/11: cajas medidas en la maqueta 08_shenron (lienzo 1600x900): panel 226..1376 x 534..876, titulo (272,578),
  // Esferas 1044..1316 cada 45 en y 578, filas en y 650/722/794 con su esfera en x 300, franja de la elegida 260..1360,
  // ayuda A en (994,838). El marco nativo (410..1170 x 580..850) queda DENTRO: su borde se tapa con el interior oscuro del
  // panel y su relleno claro se oscurece a medias (las filas nativas siguen leyendose).
  // POST2: el marco nativo se estira a este panel (campaigns.cpp, hook de sub_821613A8) y las filas nativas van a la
  // izquierda en cian/turquesa: aqui solo un velo uniforme sobre su interior (oscurece sin apagar el texto nativo).
  // franja de la elegida: azul translucido (la pieza "agua" es opaca y taparia el texto nativo de la fila)
  const float yf = 722 + 72 * (float(fila) - float(std::max(filas, 1) - 1) * 0.5f);
  // ronda 3: el velo NO pasa por la franja de la elegida (su letra cian queda viva, como la maqueta)
  const ImU32 velo = IM_COL32(4, 14, 30, 70);   // 21/11: la maqueta es azul marino oscuro; el panel nativo salia azul claro
  if (fila >= 0) {
    dl->AddRectFilled(q.At(226, 534), q.At(1376, yf - 32), velo);
    dl->AddRectFilled(q.At(226, yf + 32), q.At(1376, 876), velo);
    dl->AddRectFilled(q.At(226, yf - 32), q.At(262, yf + 32), velo);
    dl->AddRectFilled(q.At(1358, yf - 32), q.At(1376, yf + 32), velo);
  } else {
    dl->AddRectFilled(q.At(226, 534), q.At(1376, 876), velo);
  }
  if (fila >= 0) {   // POST2: la pieza "agua" de la maqueta, translucida (opaca taparia el texto nativo), sin contorno
    if (const S::Pic agua = S::Img("agua")) S::DrawPic(dl, agua, q.At(262, yf - 32), q.At(1358, yf + 32), ImVec4(0, 0, 0, 0), IM_COL32(255, 255, 255, 95));
    else dl->AddRectFilledMultiColor(q.At(262, yf - 30), q.At(1358, yf + 30), IM_COL32(60, 150, 230, 95), IM_COL32(60, 150, 230, 25),
                                     IM_COL32(40, 110, 200, 25), IM_COL32(40, 110, 200, 95));
  }
  S::Frame(dl, q.At(226, 534), q.At(1376, 876), q.s, false);   // marco del juego sin su interior (taparia las filas nativas)
  Texto(dl, q, 272, 578, kLetraFila * 1.12f, T::T("Pide un deseo", "Make a wish"), 0, 2, false, false, 600);
  const S::Pic bola = S::Img("bola_estrella");
  for (int i = 0; i < 7; ++i) {
    const float bx = 1044 + 45.0f * float(i);
    if (bola) S::DrawPic(dl, bola, q.At(bx - 17, 561), q.At(bx + 17, 595));
    else Orbe(dl, q, bx, 578, 16, IM_COL32(245, 150, 30, 255));
  }
  for (int i = 0; i < filas; ++i) {   // esfera de 1 estrella delante de cada fila
    const float yi = 722 + 72 * (float(i) - float(filas - 1) * 0.5f);
    if (bola) S::DrawPic(dl, bola, q.At(282, yi - 18), q.At(318, yi + 18));
    else Orbe(dl, q, 300, yi, 17, IM_COL32(245, 150, 30, 255));
  }
  // HEROE-MAQ/shenron: ayuda DENTRO del panel, abajo a la derecha, solo [A] Pedir. "B Pensarlo" NO se muestra: el menu
  // original no lee B y forzarlo concedia el deseo (NOTAS.md de la maqueta 3); el hook de campaigns.cpp sigue ahi sin probar.
  // 21/11 (HER-01): B Pensarlo ya cierra el menu sin deseo (her01_shenron: "B Pensarlo: se cierra el menu sin deseo"), como la maqueta.
  S::HelpLine(dl, q.At(1200, 838), 40 * q.s, {{"A", T::T("Pedir", "Wish")}, {"B", T::T("Pensarlo", "Think")}});
}

}  // namespace dbz3::heroe_ui::v2

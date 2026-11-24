// Dragon Budokai Survivors: capa de ambiente (ambiente.h).
#include "ambiente.h"

#include <cmath>
#include <cstring>
#include <string>

#include "recursos.h"

namespace dbz3::survivors {
namespace {

// fps de inventario.csv (tanda 4); tesela = lado de la celda en px a 1080; paso = distancia entre casillas (en teselas); densidad = parte de
// las casillas con dibujo (cada una con su cuadro de salida, su volteo y un desplazamiento propio: no se ve rejilla); deriva = px/s a 1080
// (hacia donde se mueve el emisor); paralaje = cuanto sigue al suelo (1 = pegada al suelo, como la niebla baja; 0 = quieta en la pantalla).
// Medido con un montaje sobre los cuatro suelos (las hojas de la tanda 4 juntas por la tinta tapaban el suelo).
struct Capa { const char* nombre; float fps, tesela, paso, densidad, dx, dy, paralaje; };
constexpr Capa kCapas[] = {
    {"lluvia", 10, 300, 0.8f, 0.9f, -70, 460, 0.6f}, {"hojas", 8, 300, 1.5f, 0.35f, 150, 55, 0.7f},   {"arena", 8, 380, 1.0f, 0.6f, 520, 35, 0.6f},
    {"niebla", 6, 560, 0.75f, 0.85f, 22, 0, 1.0f},  {"chispas", 10, 260, 1.4f, 0.4f, 12, -55, 0.8f}, {"nieve", 8, 300, 1.0f, 0.75f, -35, 95, 0.6f},
};

uint32_t Ruido(int i, int j) {
  uint32_t h = uint32_t(i) * 0x8DA6B343u ^ uint32_t(j) * 0xD8163841u ^ 0x2C1B3C6Du;
  h ^= h >> 13, h *= 0x5BD1E995u, h ^= h >> 15;
  return h;
}

}  // namespace

bool DibujarAmbiente(ImDrawList* dl, const char* nombre, float alfa, float W, float H, ImVec2 camara, float t, float escala) {
  const Capa* c = nullptr;
  for (const Capa& q : kCapas)
    if (std::strcmp(q.nombre, nombre) == 0) c = &q;
  if (!dl || !c || alfa <= 0.0f) return false;
  const ps2::Tex* tx = Arte((std::string("tanda4/03_ambiente/ambiente_") + nombre + ".png").c_str());
  if (!tx || tx->w <= 0 || tx->h <= 0) return false;
  const float lado = c->tesela * escala;
  if (lado < 16.0f) return false;
  // desplazamiento del dibujo: el emisor deriva con el tiempo y la capa sigue a medias al suelo
  const float k = lado / 256.0f, p = lado * c->paso;
  const float sx = c->dx * escala * t - camara.x * c->paralaje, sy = c->dy * escala * t - camara.y * c->paralaje;
  const int i0 = int(std::floor(-sx / p)) - 2, i1 = int(std::floor((W - sx) / p)) + 1, j0 = int(std::floor(-sy / p)) - 2, j1 = int(std::floor((H - sy) / p)) + 1;
  const int cuadro = int(t * c->fps);
  const ImU32 col = IM_COL32(255, 255, 255, int(255.0f * (alfa > 1 ? 1 : alfa)));
  const uint32_t umbral = uint32_t(c->densidad * 1000.0f);
  for (int j = j0; j <= j1; ++j)
    for (int i = i0; i <= i1; ++i) {
      const uint32_t h = Ruido(i, j);
      if ((h >> 20) % 1000u >= umbral) continue;
      const float jx = (float((h >> 9) & 255u) / 255.0f - 0.5f) * 0.5f, jy = (float((h >> 17) & 7u) / 7.0f - 0.5f) * 0.5f;
      DibujarCuadro(dl, tx, int((uint32_t(cuadro) + h) % 8u), 8, ImVec2(128, 128), ImVec2(sx + (float(i) + jx) * p + lado * 0.5f, sy + (float(j) + jy) * p + lado * 0.5f), k,
                    col, (h >> 8) & 1u);
    }
  return true;
}

}  // namespace dbz3::survivors

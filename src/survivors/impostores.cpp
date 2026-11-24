// Dragon Budokai Survivors: gestor de impostores y figura 3D en un rectangulo (impostores.h).
#include "impostores.h"
#include "medidas.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <thread>

#include <rex/logging.h>

namespace dbz3::pasarela::nave {   // pasarela_nave.cpp
int EstadoFiguraB3(int b3, int forma, int fid);
float AltoRelFiguraB3(int b3, int forma, int fid);
bool DibujarFiguraB3En(dbz3::ps2::Escena* esc, const dbz3::ps2::Camara& cam, int b3, int forma, dbz3::ps2::V3 suelo, float alto, float giro, int clip, float t, int fid, float dura = 0.0f,
                       float ciclo = -1.0f, int clip_prev = -1, float t_prev = 0.0f, float mezcla = 0.0f);
int HornearFiguraB3(int b3, int forma, int fid, int clip, int cuadros, int dirs, int px, float picado, pasarela::Imagen* atlas, float* ancla,
                     float tono);   // survivors/horno.inl
std::function<int(pasarela::Imagen*, float*)> PrepararHornoB3(int b3, int forma, int fid, int clip, int cuadros, int dirs, int px, float picado, float tono);
}  // namespace dbz3::pasarela::nave

namespace dbz3::survivors {

void Impostores::Definir(const std::vector<TipoImpostor>& tipos) {
  tipos_.clear();
  tipos_.resize(tipos.size());
  for (size_t i = 0; i < tipos.size(); ++i) tipos_[i].def = tipos[i];
}

// Dos hornos como maximo: no ocupar todos los nucleos mientras el dibujante sigue trabajando.
void Impostores::Avanzar() {
  medidas::Tramo tramo{medidas::Atlas};
  if (espera_) return;
  // El modelo y la rasterizacion se preparan fuera del dibujo. Publicar un atlas son 2-4 MB:
  // una sola subida por llamada, con presupuesto adicional de 10 ms para recorrer los pendientes.
  const auto t0 = std::chrono::steady_clock::now();
  int activos = 0;
  for (auto& u : tipos_) if (u.trabajo.valid()) ++activos;
  for (size_t i = 0; i < tipos_.size(); ++i) {
    Uno& u = tipos_[i];
    if (u.estado != 1) continue;
    if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(10)) return;
    const TipoImpostor& d = u.def;
    if (!u.listo && !u.trabajo.valid()) {
      if (activos >= 2) continue;
      const int e = pasarela::nave::HornearFiguraB3(d.b3, d.forma, d.fid, d.clip, kCuadros, kDirs, std::clamp(d.px, 16, 256), kPicado, nullptr,
                                                    nullptr, d.tono);
      if (e == 0) {
        u.estado = 0;
        REXLOG_ERROR("dbz3 survivors: impostor {} (b3 {} forma {} fid {} clip {}) imposible: falta B3 HD o el clip", i, d.b3, d.forma, d.fid, d.clip);
        continue;
      }
      if (e != 2) continue;   // el modelo aun se prepara: mira el siguiente
      auto hornear = pasarela::nave::PrepararHornoB3(d.b3, d.forma, d.fid, d.clip, kCuadros, kDirs, std::clamp(d.px, 16, 256), kPicado, d.tono);
      if (!hornear) continue;
      // Un future de std::async esperaria al destruirse al cancelar la carga. El trabajo
      // posee su resultado y solo lee modelos publicados con vida de proceso.
      std::packaged_task<Horneado()> trabajo([hornear = std::move(hornear)] {
        Horneado h;
        h.e = hornear(&h.atlas, h.ancla);
        return h;
      });
      u.trabajo = trabajo.get_future();
      std::thread(std::move(trabajo)).detach();
      ++activos;
      continue;
    }
    if (!u.listo) {
      if (u.trabajo.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
      Horneado h = u.trabajo.get();
      --activos;
      if (h.e != 2) {
        u.estado = 0;
        REXLOG_ERROR("dbz3 survivors: impostor {} (fid {} clip {}) no se pudo hornear", i, d.fid, d.clip);
        continue;
      }
      u.atlas = std::move(h.atlas);
      std::copy(std::begin(h.ancla), std::end(h.ancla), u.ancla);
      u.listo = true;
    }
    u.tex = ps2::Crear(u.atlas);
    if (!u.tex) return;   // sin dibujante todavia: el cuadro siguiente
    u.atlas = {};         // la copia en CPU ya no hace falta
    u.estado = 2;
    return;   // como mucho una subida de atlas (2-4 MB) por cuadro
  }
}

int Impostores::Estado(int tipo) const { return tipo >= 0 && size_t(tipo) < tipos_.size() ? tipos_[size_t(tipo)].estado : 0; }

bool Impostores::Listos() const {
  return std::none_of(tipos_.begin(), tipos_.end(), [](const Uno& u) { return u.estado == 1; });
}

float Impostores::Vuelta(int tipo) const { return Estado(tipo) == 2 ? std::max(tipos_[size_t(tipo)].ancla[3], 0.1f) : 1.0f; }

bool Impostores::Cuadro(int tipo, float giro, float fase, ImVec2 pies, float px_alto, Sprite* out) const {
  if (!out || Estado(tipo) != 2) return false;
  const Uno& u = tipos_[size_t(tipo)];
  const float fpx = px_alto / std::max(u.ancla[2], 1e-3f), celda = float(u.def.px) * fpx;   // px de pantalla por px del atlas
  const int d = ((int(std::floor(giro / (6.2831853f / kDirs) + 0.5f)) % kDirs) + kDirs) % kDirs;
  const int c = std::clamp(int((fase - std::floor(fase)) * kCuadros), 0, kCuadros - 1);
  out->tex = u.tex.id();
  out->a = ImVec2(pies.x - u.ancla[0] * fpx, pies.y - u.ancla[1] * fpx);
  out->b = ImVec2(out->a.x + celda, out->a.y + celda);
  out->uv0 = ImVec2(float(c) / kCuadros, float(d) / kDirs);
  out->uv1 = ImVec2(float(c + 1) / kCuadros, float(d + 1) / kDirs);
  return true;
}

bool DibujarFiguraEnRect(ImDrawList* dl, ImVec2 a, ImVec2 b, int b3, int forma, int fid, float giro, int clip, float t) {
  const float rel = pasarela::nave::AltoRelFiguraB3(b3, forma, fid);   // 0 = aun no esta (la pide)
  if (!dl || rel <= 0 || b.x - a.x < 4 || b.y - a.y < 4) return false;
  constexpr float kAlto = 10.0f;   // unidades de mundo de un luchador normal (lejos del plano cercano)
  const float hm = kAlto * rel, rw = b.x - a.x, rh = b.y - a.y;
  ps2::Camara cam;
  cam.fov = 0.35f, cam.c = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), cam.h = ImVec2(rw * 0.5f, rh * 0.5f), cam.ok = true, cam.za = cam.zb = 0;
  const float f = cam.Foco();
  const float D = std::max(f * hm / (0.9f * rh), f * 0.8f * hm / (0.95f * rw));   // entera de alto (90 %) y de ancho (brazos ~0,8 del alto)
  cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1.0f, cam.m[13] = -hm * 0.5f, cam.m[14] = -D;   // ojo en (0, hm/2, D) mirando a -z
  static ps2::Escena esc;   // hilo de dibujo; se vacia y se pinta en cada llamada
  esc.Empezar();
  if (!pasarela::nave::DibujarFiguraB3En(&esc, cam, b3, forma, {0, 0, 0}, kAlto, giro, clip, t, fid)) return false;
  dl->PushClipRect(a, b, true);
  esc.Pintar(dl);
  dl->PopClipRect();
  return true;
}

}  // namespace dbz3::survivors

// solIWX: observacion por paso del guest, junto a combate_sonda.csv.
// Solo se activa con DBZ3_TEST_INTRO_SONDA=1 y la sonda de combate.
// No escribe memoria del juego. Los offsets son del gestor de camara B3:
// 820F4488 avanza +476; 820F47E0 selecciona +608/+612; freecam.cpp documenta la pose.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dbz3::intro_sonda {
inline bool Activa() {
  static const bool activa = [] {
    const char* p = std::getenv("DBZ3_TEST_INTRO_SONDA");
    return p && std::strcmp(p, "1") == 0;
  }();
  return activa;
}

inline const char* Cabecera() {
  return ",cam_reloj,cam_paso,cam_limite,cam_modo,cam_clip,cam_acc,cam_elegida,cam_dibujo,"
         "cam_ojo_x,cam_ojo_y,cam_ojo_z,cam_obj_x,cam_obj_y,cam_obj_z,cam_roll,cam_fov";
}

inline float Fl(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, sizeof(f));
  return f;
}

inline void Fila(uint8_t* base, std::FILE* f, uint32_t mgr) {
  const uint32_t elegida = REX_LOAD_U32(mgr + 608), dibujo = REX_LOAD_U32(mgr + 612);
  std::fprintf(f, ",%.9g,%.9g,%.9g,%u,%u,%u,%u,%u", Fl(base, mgr + 476),
               Fl(base, mgr + 464), Fl(base, mgr + 576), REX_LOAD_U32(mgr + 548),
               REX_LOAD_U32(mgr + 560), REX_LOAD_U32(mgr + 564), elegida, dibujo);
  // Las poses nativas viven en el propio gestor. No se desreferencia un puntero
  // exterior: si no es una pose local, se deja constancia mediante campos vacios.
  if (dibujo < mgr || uint64_t(dibujo) + 64 > uint64_t(mgr) + 36000) {
    std::fputs(",,,,,,,,", f);
    return;
  }
  for (uint32_t o : {0u, 4u, 8u, 16u, 20u, 24u, 48u, 52u})
    std::fprintf(f, ",%.9g", Fl(base, dibujo + o));
}
}  // namespace dbz3::intro_sonda

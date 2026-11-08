// dbz3 - Pantalla y VRAM para el aviso de la escala interna (dsPHIRES).
// Solo lectura: tamano del escritorio primario y VRAM dedicada del adaptador de
// mayor memoria. En Linux devuelve 0 y la interfaz no muestra el aviso.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <rex/platform.h>

#if REX_PLATFORM_WIN32
#include <windows.h>
#include <dxgi.h>
#endif

namespace dbz3::launcher {

struct PantallaInfo {
  int width = 0;
  int height = 0;
  int vram_mb = 0;   // memoria dedicada del adaptador (0 = desconocida)
};

inline PantallaInfo PantallaActual() {
  PantallaInfo info;
#if REX_PLATFORM_WIN32
  info.width = int(GetSystemMetrics(SM_CXSCREEN));
  info.height = int(GetSystemMetrics(SM_CYSCREEN));
  // Solo para pruebas de UI (como PGPU_TEST_*): simula una pantalla >4K.
  if (const char* forced = std::getenv("PGPU_TEST_SCREEN")) {
    int w = 0, h = 0;
    if (std::sscanf(forced, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
      info.width = w;
      info.height = h;
    }
  }
  IDXGIFactory1* factory = nullptr;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && factory) {
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
      DXGI_ADAPTER_DESC1 desc = {};
      if (SUCCEEDED(adapter->GetDesc1(&desc))) {
        const int mb = int(desc.DedicatedVideoMemory / (1024 * 1024));
        if (mb > info.vram_mb) info.vram_mb = mb;
      }
      adapter->Release();
    }
    factory->Release();
  }
#endif
  return info;
}

// Escala entera a la que el framebuffer de 720p del juego coincide (o casi) con
// la pantalla. Por encima de ella solo hay supersampling: mas coste, no mas
// detalle. Devuelve 0 si no se conoce la pantalla.
inline int EscalaNativaDePantalla(int width, int height) {
  if (width <= 0 || height <= 0) return 0;
  int scale = std::min(width / 1280, height / 720);
  if (scale < 1) scale = 1;
  if (scale > 6) scale = 6;
  return scale;
}

// En pantallas de mas de 4K la escala nativa real (4x en 5K, 6x en 8K) no sostiene
// los 60 Hz: medido, 4x ~50 FPS y 6x ~18 FPS. La escala que se PROPONE al jugador
// se limita a 3x (59,8 FPS medidos a 5K); el presentador escala el resto.
inline bool PantallaSupera4K(int width, int height) {
  return width > 3840 || height > 2160;
}

inline int EscalaNativaPropuesta(int width, int height) {
  const int nativa = EscalaNativaDePantalla(width, height);
  if (nativa > 3 && PantallaSupera4K(width, height)) return 3;
  return nativa;
}

// VRAM estimada en combate (medida en este proyecto, salida 720p, HD off):
// 1x 432 MB, 2x 1162, 3x 2056, 4x 3287, 6x 6157. La tabla es lo medido; antes
// la formula sumaba 460 + 160*escala^2 y a 1x daba 620 MB (casi el doble de los
// 432 reales) y a 4K 952 MB frente a 511. La salida suma ~12 MB por megapixel
// por encima de 720p (medido a 1x: 1080p 445, 4K 511, 5400x5400 770 MB).
// nGPU: la mejora de texturas suma VRAM real (medido a 1x: +177 MB a x2 y
// +269..428 MB a x3; se toma 200 por paso, conservador para avisar antes).
inline int VramEstimadaMb(int scale, int out_width, int out_height, bool fsr_temporal,
                          int hd_texture_factor = 1) {
  static const int kBaseMb[7] = {0, 432, 1162, 2056, 3287, 4722, 6157};
  const int s = std::clamp(scale, 1, 6);
  int mb = kBaseMb[s];
  const double mp = double(out_width) * double(out_height) / 1.0e6;
  if (mp > 0.9216) mb += int((mp - 0.9216) * 12.0);   // 720p ya esta en la tabla
  if (fsr_temporal) mb += int(mp * 67.0);   // FSR3: +~556 MB medidos a 4K
  const int hd = hd_texture_factor < 1 ? 1 : hd_texture_factor;
  if (hd > 1) mb += 200 * (hd - 1);
  return mb;
}

}  // namespace dbz3::launcher

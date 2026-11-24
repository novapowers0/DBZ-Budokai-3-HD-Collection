// Dragon Budokai Survivors: enemigos como IMPOSTORES (atlas 2D horneados una vez de un modelo de B3 con HornearFiguraB3, horno.inl) y la
// figura 3D completa dentro de un rectangulo de pantalla (seleccion de personaje, jefe).
// El dibujo gestiona estados y GPU; dos trabajadores hornean datos CPU inmutables.
//
// Memoria por tipo (8 cuadros x 8 direcciones, celda px): atlas (8 px)^2 RGBA = 2,25 MB de GPU con px 96 (4 MB con 128);
// la copia en CPU se suelta al crear la textura. Hornear cuesta ~80 ms por tipo (Saibaman, 96 px; mas con modelos grandes o px
// mayor): Avanzar() publica como mucho UNA textura por cuadro; Carga espera a que esten todos.
#pragma once

#include <future>
#include <vector>

#include <imgui.h>

#include "../pasarela.h"
#include "../ps2_modelo.h"

namespace dbz3::survivors {

struct TipoImpostor {
  int b3 = 0, forma = 0, fid = -1;   // modelo (fid de mod center hd/tactics_manifest.toml)
  float tono = 0.0f;                 // giro de tono en grados al hornear (0 = colores de B3; ~-120 = Saibaman rojo)
  int px = 96;                       // lado de la celda del atlas
  int clip = 1;                      // 0 espera, 1 andar, 2 ataque, 3 tecnica, 4 dano, 5 derrota... (otro clip = otro tipo)
};

struct Sprite {   // un rectangulo con su trozo del atlas: dl->AddImage(tex, a, b, uv0, uv1) o PrimRectUV agrupando por tex
  ImTextureID tex = {};
  ImVec2 a, b, uv0, uv1;
};

class Impostores {
 public:
  static constexpr int kCuadros = 8, kDirs = 8;
  static constexpr float kPicado = 0.95993f;   // 55 grados sobre el horizonte: la camara del modo tiene que usar el mismo

  void Definir(const std::vector<TipoImpostor>& tipos);   // descarta lo horneado; el indice en `tipos` es el `tipo` de abajo
  void Avanzar();                                         // cada cuadro hasta Listos(): con tope de ~10 ms por llamada (carga perezosa, 13/11)
  void Esperar(bool si) { espera_ = si; }                 // true: Avanzar no hace nada (los luchadores van primero; luego false)
  int Estado(int tipo) const;                             // 0 falta B3 HD o el clip (no se hara), 1 generandose, 2 listo
  bool Listos() const;                                    // ninguno en 1
  float Vuelta(int tipo) const;                           // segundos de una vuelta del clip
  // Cuadro de `tipo` mirando a `giro` (radianes, 0 = de cara a +z, el de DibujarFiguraB3En) en la `fase` de su clip (vueltas;
  // se usa la parte decimal: fase = t / Vuelta(tipo) + desfase propio), con los PIES en `pies` (pantalla) y `px_alto` = px de
  // pantalla de una figura de alto 1 (px por unidad de mundo * alto). false si el tipo aun no esta listo.
  bool Cuadro(int tipo, float giro, float fase, ImVec2 pies, float px_alto, Sprite* out) const;

 private:
  struct Horneado {
    int e = 0;
    pasarela::Imagen atlas;
    float ancla[4] = {0, 0, 1, 1};
  };
  struct Uno {
    TipoImpostor def;
    int estado = 1;
    bool listo = false;       // horneado recogido del hilo (atlas lleno)
    pasarela::Imagen atlas;   // solo hasta crear la textura
    ps2::Tex tex;
    float ancla[4] = {0, 0, 1, 1};
    std::future<Horneado> trabajo;   // packaged_task posee su resultado: cancelar no espera al horno
  };
  std::vector<Uno> tipos_;
  bool espera_ = false;
};

// Figura 3D de B3 completa (DibujarFiguraB3En) de pie, centrada y entera dentro de [a, b] (recortada a la caja), de cara a la
// pantalla con giro 0; clip/t como DibujarFiguraB3En (0 espera, t en segundos). false si el modelo aun no esta listo (lo pide).
bool DibujarFiguraEnRect(ImDrawList* dl, ImVec2 a, ImVec2 b, int b3, int forma, int fid, float giro = 0.0f, int clip = 0, float t = 0.0f);

}  // namespace dbz3::survivors

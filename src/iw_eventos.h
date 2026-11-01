// Ventana de eventos #BSK de IW. EU23F520: anterior < cuadro <= actual,
// colas de siete entradas (una para voz/ki/boca) por evaluacion.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace dbz3::anim::eventos {
struct Registro {
  float cuadro;
  uint32_t tipo, valor;
};
struct Lote {
  std::array<std::array<Registro,7>,10> colas{};
  std::array<uint8_t,10> usados{};
};
// Primero23F520 llena colas en orden de archivo; DESPUES207180 las consume
// por tipos. No ordenar el archivo por cuadro ni ejecutar efectos al encolarlos.
inline Lote Evaluar(const std::vector<Registro>& registros,float& anterior,float actual,float duracion) {
  Lote lote;
  auto tramo = [&](float de, float a) {
    for (const auto& e : registros) {
      if (e.tipo > 9 || e.cuadro <= de || e.cuadro > a) continue;
      const int tope = e.tipo == 3 || e.tipo == 7 || e.tipo == 8 ? 1 : 7;
      auto& n=lote.usados[e.tipo];
      if(e.tipo==9){lote.colas[9][0]=e;n=1;continue;} // ultima escritura de244
      if(n>=tope)continue;
      lote.colas[e.tipo][n++]=e;
    }
  };
  if (actual < anterior) {
    tramo(anterior, duracion);
    anterior = -1.0f;
  }
  tramo(anterior, actual);
  anterior = actual;
  return lote;
}
template<class Emitir>
void Despachar(const Lote& lote,Emitir emitir) {
  //5 NO tiene consumidor en Animations2: se notifica al lector externo despues
  // de los tipos sincronos.9 es una escritura de estado, no N efectos distintos.
  constexpr uint8_t orden[]={0,1,2,3,7,4,6,8,5,9};
  for(const auto tipo:orden) {
    const unsigned n=tipo==6?(lote.usados[6]?1u:0u):lote.usados[tipo];
    for(unsigned i=0;i<n;++i)emitir(lote.colas[tipo][i]);
  }
}
template<class Emitir>
void Avanzar(const std::vector<Registro>& registros,float& anterior,float actual,float duracion,Emitir emitir) {
  Despachar(Evaluar(registros,anterior,actual,duracion),emitir);
}
} // namespace dbz3::anim::eventos

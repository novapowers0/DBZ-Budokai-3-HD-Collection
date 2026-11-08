// Replay original acotado y guardas del adaptador asincrono de reaccion.
#include "../src/iw_piccolo_reaccion.h"
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace dbz3::iwpiccolo;
unsigned bits(float x) { unsigned u; std::memcpy(&u, &x, 4); return u; }
void le(std::ostringstream& s, unsigned v, unsigned n) {
  for (unsigned i=0;i<n;++i) s<<std::hex<<std::setw(2)<<std::setfill('0')<<((v>>(8*i))&255u);
}

int main() {
  std::ostringstream tabla,giros;
  for (auto h:kHuesos) {
    le(tabla,h.hueso,1);le(tabla,h.grupo,1);le(tabla,h.campo02,2);le(tabla,h.codigo,4);
    le(tabla,bits(h.radio_mundo),4);le(tabla,bits(h.radio_pantalla),4);
  }
  for (auto g:kGiros) {le(giros,bits(g.horizontal),4);le(giros,bits(g.vertical),4);}
  std::cout<<"{\"tabla\":\""<<tabla.str()<<"\",\"giros\":\""<<giros.str()<<"\"}\n";
  unsigned casos=0,fallos=0;
  auto exigir=[&](bool b){++casos;if(!b)++fallos;};
  for (int rol=0;rol<22;++rol) {
    ReaccionEnMotor r;
    const auto hit=r.Golpe(rol,900,900);
    exigir(hit.error==Error::Ninguno && hit.orden.reproducir && hit.estado.pose_estado==3);
    const auto alias=AliasReaccion(kHuesos[rol].codigo);
    // El terminal de la accion anterior nunca debe terminar una orden encolada.
    exigir(r.Paso({0xFC0,35,1,34},900,900,0).activa);
    exigir(r.Paso({alias,30,0,29},900,900,0).activa);
    exigir(r.Ver().pose_estado==3);
    exigir(r.Paso({alias,30,0,0},900,900,0).observado);
    exigir(r.Paso({alias,30,0,28},900,900,0).activa);
    const auto fin=r.Paso({alias,30,0,29},900,900,0);
    exigir(fin.regreso && !fin.activa && r.Ver().codigo==0x3E8);
    const auto otra=r.Golpe(rol,900,900);
    exigir(otra.estado.generacion>hit.estado.generacion);
    exigir(r.Paso({alias,30,0,29},900,900,0).activa);
    exigir(r.Paso({alias,30,0,29},900,900,0).activa);
    exigir(r.Paso({alias,30,0,0},900,900,0).observado);
  }
  ReaccionEnMotor r;
  exigir(r.Golpe(22,900,900).error==Error::HuesoNoResuelto && r.Ver().pose_estado==1);
  exigir(r.Golpe(-1,900,900).impacto==false && r.Ver().pose_estado==1);
  exigir(r.Golpe(4,0,900).orden.reproducir==false && r.Ver().pose_estado==1);
  auto repetida=r.Golpe(4,900,900);repetida=r.Golpe(4,900,900);
  exigir(repetida.estado.repeticion_cc0==1);
  exigir(DespacharFinReaccion(r.Ver(),{r.Ver().generacion-1,true,30}).error==Error::ObservacionDeOtroClip);
  exigir(AliasReaccion(0x405)==0 && AliasReaccion(0x40C)==0);
  std::cout<<"{\"guardas\":"<<casos<<",\"fallos\":"<<fallos<<"}\n";

  int rol;float vida,maxima,az,el,contador;int fin;
  unsigned replay=0;
  std::cout<<std::setprecision(10);
  while(std::cin>>rol>>vida>>maxima>>az>>el>>contador>>fin) {
    Estado e;e.pose_estado=3;e.ultimo_hueso=rol;
    Entrada in;in.salud=vida;in.salud_maxima=maxima;in.inclinacion=el;
    in.clip={e.generacion,false,contador};
    const auto paso=ActualizarRotacion(e,in);
    const auto salida=DespacharFinReaccion(paso.estado,{e.generacion,fin!=0,contador});
    std::cout<<"{\"azimut\":"<<az+paso.delta_azimut<<",\"inclinacion\":"<<paso.inclinacion
        <<",\"estado\":"<<salida.estado.pose_estado<<",\"error\":"<<int(paso.error)<<"}\n";
    ++replay;
  }
  return fallos || !std::cin.eof() || !replay;
}

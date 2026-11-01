// AME del consumidor frente a RAM real PCSX2 (no frente al interprete EE).
#include "../src/iw_capsulas_render.h"
#include <fstream>
#include <iostream>
#include <iterator>
namespace R=dbz3::iw::capsrender;
struct DatosF5 {R::Bytes b;size_t p=0;uint32_t U(){auto v=dbz3::iw::spx::capsulas::U(b,p);p+=4;return v;}float F(){return R::F(U());}R::Bytes Blob(){const auto n=U();R::Bytes out(b.begin()+p,b.begin()+p+n);p+=n;return out;}};
int main(int argc,char** argv) {
  try {
    if(argc!=2)throw std::runtime_error("fixture");std::ifstream f(argv[1],std::ios::binary);DatosF5 d{R::Bytes(std::istreambuf_iterator<char>(f),{})};if(d.U()!=0x46355254)throw std::runtime_error("firma");
    const auto raw=d.Blob();const auto inicial=d.U(),campos=d.U(),primero=d.U();const float paso=d.F(),dt=d.F();const auto inicio=d.p;
    unsigned coinciden=0;bool antes_ganador=false;size_t comprobaciones=0;
    for(bool antes:{false,true}) {
      d.p=inicio;R::Ame ame(raw);uint32_t seed=inicial;
      for(unsigned frame=1;frame<=campos;++frame){if(antes)(void)R::Ame::Memory(seed,32);if(frame>=primero)ame.Cuadro(seed,paso,dt);if(!antes)(void)R::Ame::Memory(seed,32);}
      size_t errores=0,checks=0;std::string primer_error;std::map<std::string,unsigned> categorias;
      auto ver=[&](bool v,const std::string& texto){++checks;if(!v){++errores;if(primer_error.empty())primer_error=texto;}};
      auto fl=[&](float v,const std::string& texto){const auto e=d.F();if(std::abs(v-e)>3.e-6f)++categorias[texto];ver(std::abs(v-e)<=3.e-6f,texto+" host="+std::to_string(v)+" original="+std::to_string(e));};
      ver(uint32_t(ame.Nodos())==d.U(),"nodos");ver(seed==d.U(),"RNG final");std::vector<const R::NodoAme*> activos;for(const auto& n:ame.nodos)if(!n.retirado)activos.push_back(&n);
      const auto nn=d.U();ver(activos.size()==nn,"numero de nodos activos");if(activos.size()!=nn)throw std::runtime_error("cantidad de nodos impide comparar RAM");
      for(const auto* n:activos) {
        const auto& def=ame.defs[n->def];ver(def.offset==d.U(),"orden/offset");ver(def.tipo==d.U(),"tipo");const auto np=d.U();ver(n->particulas.size()==np,"conteo particulas");if(n->particulas.size()!=np)throw std::runtime_error("cantidad de particulas impide comparar RAM");
        for(const auto& p:n->particulas) {
          fl(p.edad,"edad");ver(p.flags==d.U(),"flags");for(float v:{p.p.x,p.p.y,p.p.z})fl(v,"posicion");for(float v:{p.v.x,p.v.y,p.v.z})fl(v,"velocidad");
          for(float v:p.color)fl(v,"color");for(float v:p.uv)fl(v,"UV");fl(p.tamano,"tamano");fl(p.escala,"escala");
          if(!antes && std::abs(p.angulo-R::F(dbz3::iw::spx::capsulas::U(d.b,d.p)))>3.e-6f)std::cout<<"ANGULO nodo="<<def.offset<<" base="<<def.f[8]<<" rango="<<def.f[9]<<" velocidad="<<def.f[11]<<" host="<<p.angulo<<" native="<<R::F(dbz3::iw::spx::capsulas::U(d.b,d.p))<<'\n';
          fl(p.angulo,"angulo");for(float v:p.quat)fl(v,"quaternion");
        }
      }
      std::cout<<"RNG32 "<<(antes?"ANTES":"DESPUES")<<" D5: errores="<<errores<<" comprobaciones="<<checks<<" primero="<<primer_error<<'\n';
      for(const auto& [nombre,n]:categorias)std::cout<<"  "<<nombre<<"="<<n<<'\n';
      if(!errores){++coinciden;antes_ganador=antes;comprobaciones=checks;}
    }
    if(coinciden!=1)throw std::runtime_error("la RAM real no identifica una unica secuencia verificada");
    std::cout<<"PASS "<<comprobaciones<<" RAM PCSX2 real; RNG32 "<<(antes_ganador?"ANTES":"DESPUES")<<" de D5; campos="<<campos<<" primer_D5="<<primero<<'\n';return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL RAM real "<<e.what()<<'\n';return 1;}
}

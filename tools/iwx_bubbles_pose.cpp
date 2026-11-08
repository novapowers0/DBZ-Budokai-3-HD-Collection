// Contraste del codigo de produccion con TARGET/LIVE externos de PCSX2.
#include "iw_bubbles_pose.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
namespace p=dbz3::iwbubbles::poses;
std::vector<uint8_t> Leer(const std::filesystem::path& path){
  std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error(path.string());
  return {std::istreambuf_iterator<char>(f),{}};
}
float Error(float a,float b){return std::isfinite(a)&&std::isfinite(b)?std::fabs(a-b):std::numeric_limits<float>::infinity();}
template<size_t N>float Qps(const std::array<p::Hueso,N>& live,const std::vector<uint8_t>& raw){
  if(raw.size()!=N*128)throw std::runtime_error("LIVE incompatible");
  const p::Bytes b{raw};float maximo=0;
  for(size_t i=0;i<N;++i){const auto h=b.Leer<p::Hueso>(128*i);
    for(size_t k=0;k<4;++k)maximo=std::max(maximo,Error(live[i].q[k],h.q[k]));
    for(size_t k=0;k<3;++k){maximo=std::max(maximo,Error(live[i].p[k],h.p[k]));maximo=std::max(maximo,Error(live[i].s[k],h.s[k]));}
  }
  return maximo;
}
int main(int argc,char** argv){try{
  if(argc!=4)throw std::runtime_error("Usar: recursos fixtures_staged entrada_facial");
  const std::filesystem::path recursos=argv[1],fixtures=argv[2],entrada=argv[3];
  p::Recursos banco;banco.Iniciar(Leer(recursos/"amo.bin"),Leer(recursos/"amm.bin"),Leer(recursos/"bsk.bin"),
    Leer(recursos/"cara_amo.bin"),Leer(recursos/"cara_amm.bin"),Leer(recursos/"elf.bin"));
  std::cout<<std::setprecision(9);
  const auto lut=Leer(fixtures/"lut.bin");
  if(lut.size()!=banco.lut.size()*4||std::memcmp(lut.data(),banco.lut.data(),lut.size()))throw std::runtime_error("LUT no es la original completa");
  std::cout<<"LUT iguales=16385/16385 fuente=ELF\n";
  float target_max=0;
  for(unsigned j=0;j<5;++j){
    const auto raw=Leer(fixtures/("target"+std::to_string(j)+".bin"));const p::Bytes b{raw};
    const auto got=banco.cuerpo.Muestrear(b.U(0)&65535,b.Leer<float>(4),banco.lut);float q=0,pos=0,esc=0;
    for(unsigned i=0;i<47;++i){const auto h=b.Leer<p::Target>(102+64*i);
      if(banco.cuerpo.mapa[i]!=b.Leer<int16_t>(8+2*i))throw std::runtime_error("Mapa por nombres no es el original");
      for(unsigned k=0;k<4;++k)q=std::max(q,Error(got[i].q[k],h.q[k]));
      for(unsigned k=0;k<3;++k){pos=std::max(pos,Error(got[i].p[k],h.p[k]));esc=std::max(esc,Error(got[i].s[k],h.s[k]));}
    }
    target_max=std::max({target_max,q,pos,esc});
    std::cout<<"TARGET "<<j<<" q="<<q<<" p="<<pos<<" s="<<esc<<'\n';
  }
  p::Instancia<47> mezcla;const auto old=Leer(fixtures/"oldlive.bin");
  if(old.size()!=sizeof mezcla.live)throw std::runtime_error("oldlive incompatible");
  std::memcpy(mezcla.live.data(),old.data(),old.size());mezcla.iniciada=true;mezcla.u=.005f;mezcla.rate=.03f;
  mezcla.Aplicar(banco.Muestrear(0xD5,0),1.2f);
  const float transicion=Qps(mezcla.live,Leer(fixtures/"expectedlive.bin"));
  std::cout<<"DD_D5 qps="<<transicion<<'\n';
  const auto meta_raw=Leer(entrada/"meta.bin");const p::Bytes meta{meta_raw};
  const auto antes=Leer(entrada/"antes_live.bin"),despues=Leer(entrada/"despues_live.bin");
  std::memcpy(mezcla.live.data(),antes.data(),antes.size());mezcla.u=.005f;mezcla.rate=meta.Leer<float>(12);
  mezcla.Aplicar(banco.Muestrear(0xD2,meta.Leer<float>(20)),1.2f);
  p::Instancia<8> cara;cara.Iniciar(banco.cara.Reposo());
  const auto neutral=banco.cara.Muestrear(0,1,banco.lut);
  // El original conserva el hemisferio de LIVE durante la relajacion facial .6.
  // No inicializar directamente desde TARGET: q y -q representan el mismo giro,
  // pero la copia de128B y la proxima mezcla exigen conservar ese historial.
  for(unsigned i=0;i<200;++i)cara.AplicarFactor(neutral,.6f);
  banco.PublicarCara(mezcla,cara);
  const auto facial=Leer(entrada/"facial.bin");const p::Bytes fb{facial};float facial_max=0;
  for(unsigned j=0;j<fb.U(0);++j){
    const auto dst=fb.Leer<int32_t>(16+136*j),src=fb.Leer<int32_t>(20+136*j);
    bool hallado=false;for(const auto& par:banco.boca)hallado|=par.first==unsigned(dst)&&par.second==unsigned(src);
    if(!hallado)throw std::runtime_error("mapa facial por nombres no coincide con RAM");
    const auto original=fb.Leer<p::Hueso>(24+136*j);const auto& got=cara.live[size_t(src)];float d=0;
    for(unsigned k=0;k<4;++k)d=std::max(d,Error(got.q[k],original.q[k]));
    for(unsigned k=0;k<3;++k){d=std::max(d,Error(got.p[k],original.p[k]));d=std::max(d,Error(got.s[k],original.s[k]));}
    facial_max=std::max(facial_max,d);std::cout<<"FACIAL src="<<src<<" dst="<<dst<<" qps="<<d<<'\n';
    if(d>.1f)std::cout<<"  q_original="<<original.q.x<<','<<original.q.y<<','<<original.q.z<<','<<original.q.w
      <<" q_recurso="<<got.q.x<<','<<got.q.y<<','<<got.q.z<<','<<got.q.w<<'\n';
  }
  std::cout<<"ENTRADA recursos_faciales=120/0+1 qps="<<Qps(mezcla.live,despues)<<" max_facial="<<facial_max<<" visual=false\n";
  // Mismos limites de los contrastes anteriores; la entrada conserva su error medido.
  if(target_max>9.53674316e-7f||transicion>1.1920929e-7f||facial_max>1.1920929e-7f)return 2;
  return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

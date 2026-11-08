// Reproduce la orden/tiempo BBR contra RAM retenida y prueba PCM sin altavoces.
#include "iw_bubbles_audio.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace dbz3;
std::vector<uint8_t> Leer(const std::filesystem::path& p){
  std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error(p.string());
  return {std::istreambuf_iterator<char>(f),{}};
}
void Exigir(bool bien,const char* que){if(!bien)throw std::runtime_error(que);}
int main(int argc,char** argv){try{
  Exigir(argc==3,"Usar: recursos_externos ELF");const std::filesystem::path dir=argv[1];
  const auto elf=Leer(argv[2]);
  const auto voz=anim::apev::ResolverAdx(elf,912,0,0);
  Exigir(voz.ruta==anim::apev::Ruta::Adx && voz.banco==927,"BBR perfil16 linea0 no es ADX927");
  Exigir(anim::apev::BancoBoca(elf,912)==1459,"Banco de boca BBR distinto");
  Exigir(anim::apev::ResolverAdx(elf,912,8,0).ruta==anim::apev::Ruta::SilencioOriginal,"No rechaza linea8");
  Exigir(anim::apev::ResolverAdx({},912,0,0).ruta==anim::apev::Ruta::Pendiente,"ELF vacio usa una voz");
  anim::apev::boca::BancoAMK banco;Exigir(banco.Cargar(Leer(dir/"boca_usa.amk")),"AMK invalido");
  const auto raw=Leer(dir/"campos.bin");Exigir(raw.size()%24==0 && !raw.empty(),"Fixture incompleta");
  iwbubbles::audio::Intro intro;unsigned inicios=0,anter=0;float maximo=0;
  for(size_t p=0;p<raw.size();p+=24){
    uint32_t u[6];std::memcpy(u,raw.data()+p,24);float frame,boca;
    std::memcpy(&frame,u+3,4);std::memcpy(&boca,u+5,4);
    Exigir(!p || u[0]==anter+1,"Fixture no consecutiva");anter=u[0];
    const bool inicio=intro.Paso(u[1]==10?1:u[1]==0?2:0,u[2],frame,banco);inicios+=inicio;
    if(inicio)Exigir(u[0]==17527 && frame==1.2f,"Inicio distinto del original");
    if(intro.boca.id!=u[4]){
      std::cerr<<"RAM n="<<u[0]<<" esperado_id="<<std::hex<<u[4]<<" actual_id="<<intro.boca.id
        <<std::dec<<" esperado_frame="<<boca<<" actual_frame="<<intro.boca.cuadro<<'\n';
      throw std::runtime_error("Selector de boca distinto de RAM");
    }
    maximo=std::max(maximo,std::abs(intro.boca.cuadro-boca));
  }
  Exigir(inicios==1 && maximo==0,"Reloj AMK o numero de voces distinto");
  Exigir(!intro.Paso(1,0x11D,0,banco),"Se repite voz en la misma sesion");
  intro={};Exigir(!intro.Paso(1,0x11D,NAN,banco),"NaN inicia voz");
  Exigir(!intro.Paso(0,0x11D,0,banco),"Fase distinta inicia voz");
  Exigir(intro.Paso(1,0x11D,1.2f,banco),"Nueva sesion no inicia voz");
  for(const char* idioma:{"usa","jpn"}){
    const auto adx=Leer(dir/(std::string("voz_")+idioma+".adx"));const auto pcm=iwadx::Leer(adx);
    Exigir(pcm.hz && !pcm.muestras.empty(),"ADX vacio");
    Exigir(std::any_of(pcm.muestras.begin(),pcm.muestras.end(),[](auto x){return x!=0;}),"PCM es silencio");
    iwsonido::Mezclador m;auto canal=std::make_shared<iwsonido::Canal>();iwsonido::Opciones o;o.canal=canal;
    const auto h=m.Iniciar(std::make_shared<const std::vector<int16_t>>(pcm.muestras),pcm.hz,o);Exigir(h!=0,"Sin handle PCM");
    std::vector<int16_t> salida(48000*2);m.Renderizar(salida.data(),48000);
    Exigir(std::any_of(salida.begin(),salida.end(),[](auto x){return x!=0;}),"Mezclador no emite voz");
    m.Maestro(1,true);m.Iniciar(std::make_shared<const std::vector<int16_t>>(pcm.muestras),pcm.hz,o);m.Renderizar(salida.data(),48000);
    Exigir(std::all_of(salida.begin(),salida.end(),[](auto x){return x==0;}),"Silenciar deja pasar la voz");
    std::cout<<"PCM "<<idioma<<" hz="<<pcm.hz<<" cuadros="<<pcm.muestras.size()/2<<" mezcla=PASS silencio=PASS\n";
  }
  std::cout<<"RAM campos="<<raw.size()/24<<" voces="<<inicios<<" inicio=17527 AMK_error="<<maximo<<" PASS\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

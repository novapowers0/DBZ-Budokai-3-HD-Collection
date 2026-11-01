// API de produccion REAL: no sustituye Posicion/MVOL/registroADX ni callbacks.
#include "../src/iw_midi.h"
#include "../src/iw_adx.h"
#include "../src/iw_azar.h"
#include "../src/iw_capsulas_render.h"
#define main iw_capsulas_F2_historico_no_ejecutado
#include "iw_capsulas_F2_motor.cpp"
#undef main
namespace dbz3::settings {double MasterVolume(){return 1;}bool AudioMute(){return false;}}
bool NoCeroF5(const std::vector<int16_t>& b){return std::any_of(b.begin(),b.end(),[](auto x){return x!=0;});}
std::vector<int16_t> ConstanteF5(int l,int r,size_t n=96000){std::vector<int16_t> b(n*2);for(size_t i=0;i<n;++i){b[2*i]=int16_t(l);b[2*i+1]=int16_t(r);}return b;}
int main(int argc,char** argv) {
  try {
    Ver(argc==2,"fixture original F3");std::ifstream f(argv[1],std::ios::binary);C::Bytes b(std::istreambuf_iterator<char>(f),{});size_t p=0;
    auto word=[&](){auto v=C::U(b,p);p+=4;return v;};auto blob=[&](){auto n=word();C::Bytes o(b.begin()+p,b.begin()+p+n);p+=n;return o;};
    dbz3::pasarela::elf_fixture=blob();const auto nb=word();for(unsigned i=0;i<nb;++i){const auto id=word();dbz3::pasarela::bancos_fixture[id]=blob();}
    std::map<uint32_t,uint16_t> original;const auto nv=word();Ver(nv==9,"nueve MVOL originales");
    for(unsigned i=0;i<nv;++i){const auto valor=word(),reg=word(),nr=word();Ver(nr==4,"cuatro registros originales");original[valor]=uint16_t(reg);
      std::vector<uint32_t> direcciones;for(unsigned k=0;k<nr;++k){direcciones.push_back(word());Ver(word()==reg,"mismo MVOL en ambos canales/cores");}
      Ver(direcciones==std::vector<uint32_t>{0x980,0xa80,0x981,0xa81},"AMSOUND escribe MVOL real de ambos cores");}
    auto& salida=dbz3::iwsonido::Salida();auto& mezcla=salida.Mezcla();Ver(salida.DispositivoAutomatico(false),"sin altavoces");
    for(int id:{993,994}) {
      dbz3::pasarela::PruebaCapsulasSpx e("fixture",id);Ver(e.carga,"carga original de pantalla");
      Ver(bool(e.volumen_spu2)&&bool(e.detener_adx_anteriores),"callbacks REALES conectados desde constructor");
      Ver(!e.midi.posicion_audio,"sin callback de cursor aportado por el test");
      Ver(e.motor.Tick(0)&&e.midi.Handle()!=0,"RPC13 real inicia MIDI");
      auto h=e.midi.Handle();uint64_t cursor=999;Ver(mezcla.Posicion(h,cursor)&&cursor==0,"cursor inicial real");
      std::vector<int16_t> pcm(96000);salida.Procesar(pcm.data(),48000);
      Ver(NoCeroF5(pcm)&&mezcla.Posicion(h,cursor)&&cursor==48000,"cursor cuenta exactamente salida consumida");
      Ver(mezcla.Pausar(h,true),"pausar handle real");salida.Procesar(pcm.data(),37);
      Ver(mezcla.Posicion(h,cursor)&&cursor==48000,"pausa conserva cursor real");Ver(mezcla.Pausar(h,false),"reanudar");
      Ver(e.RpcCapsulas(0x14,0)&&e.midi.Error().empty(),"RPC14 resuelve Posicion del mezclador automaticamente");
      Ver(!mezcla.Posicion(h,cursor),"RPC14 retira el handle anterior");
      std::vector<int16_t> cola(48000);salida.Procesar(cola.data(),24000);
      Ver(NoCeroF5(cola)&&cola.back()==0,"cola ADSR audible en PCM y despues silencio");e.midi.Abortar();
      std::cout<<"SPX"<<id<<" API Posicion/RPC13-14 real PASS\n";
    }
    // Fuentes a24k verifican que Posicion son cuadros de la fuente, no de UI.
    salida.UsarProveedor(nullptr);mezcla.Maestro(1);dbz3::iwsonido::Opciones seca;seca.rutas=3;
    auto resample=salida.Enviar(ConstanteF5(4,8),24000,seca);std::vector<int16_t> pequeno(256);salida.Procesar(pequeno.data(),100);
    uint64_t cursor=0;Ver(mezcla.Posicion(resample,cursor)&&cursor==50,"cursor fuente24k/consumo48k");mezcla.Parar(resample);
    {
      dbz3::pasarela::PruebaCapsulasSpx e("fixture",994);Ver(e.motor.Tick(0)&&e.midi.Handle(),"MIDI ajeno al registro ADX permanece vivo");
      const auto midi=e.midi.Handle();Ver(mezcla.Pausar(midi,true),"MIDI pausado para aislar PCM de MVOL");
      e.motor.vm.Parar();C::Bytes quieto(36);std::memcpy(quieto.data(),"#SPX",4);quieto[20]=36;quieto[24]=1;quieto.insert(quieto.end(),128,0x92);
      Ver(e.motor.vm.Cargar(quieto,dbz3::iw::spx::kCapsulasEU)&&e.motor.vm.Iniciar()==0,"task quieto durante transicion");
      salida.UsarProveedor(nullptr);mezcla.Maestro(.25f);
      dbz3::iwadx::Pcm a0{ConstanteF5(1000,2000),48000},a1{ConstanteF5(3000,4000),48000};
      dbz3::iwsonido::Opciones o0;o0.core=0;o0.rutas=3;auto o1=o0;o1.core=1;
      const auto h0=dbz3::iwadx::Oir(a0,o0),h1=dbz3::iwadx::Oir(a1,o1),otro=salida.Enviar(ConstanteF5(7,11),48000,o1);
      Ver(h0&&h1&&otro,"dos ADX registrados y actor noADX reales");
      Ver(e.volumen_spu2(original.at(dbz3::iw::spx::Bits(1.f))),"MVOL inicial nativo");
      e.fundido.valor=0;e.fundido.velocidad=.125f;Ver(e.motor.host.transicion(10),"transicion10 acepta callbacks reales");e.fundido.velocidad=.125f;
      for(int i=0;i<=8;++i) {
        // RAM PCSX2 real19653->19654, aun sin particulas: el kernel consume
        // Memory32 al final de cada campo. No probarlo con otra formula clonada.
        if(i==1)dbz3::iwazar::Global().Cambiar(2626465793u);
        if(i)Ver(e.PasoCapsulas(0),"transicion10 con API real");
        if(i==1)Ver(dbz3::iwazar::Global().Estado()==2416760500u,"kernel RNG coincide con DOS estados reales PCSX2 consecutivos");
        const auto reg=original.at(dbz3::iw::spx::Bits(1-e.fundido.valor));salida.Procesar(pequeno.data(),64);
        const auto l=int16_t(((1000*int(reg)>>14)+(3007*int(reg)>>14))*.25f);
        const auto r=int16_t(((2000*int(reg)>>14)+(4011*int(reg)>>14))*.25f);
        Ver(pequeno[0]==l&&pequeno[1]==r,"MVOL ambos cores, ambos canales y Maestro independiente coinciden con registros originales");
      }
      Ver(e.fundido.valor==1,"fin real del fundido");
      Ver(!mezcla.Posicion(h0,cursor)&&!mezcla.Posicion(h1,cursor),"ToDefine detuvo TODOS los ADX registrados");
      Ver(mezcla.Posicion(midi,cursor)&&mezcla.Posicion(otro,cursor),"ToDefine conserva MIDI y actor noADX");
      Ver(e.detener_adx_anteriores(),"PararTodos real idempotente");
      Ver(e.volumen_spu2(0x3fff),"restaurar MVOL nativo");salida.Procesar(pequeno.data(),64);
      Ver(pequeno[0]==1&&pequeno[1]==2,"ganancia host0.25 no sobrescrita por MVOL ni por ToDefine");
      const auto nuevo=dbz3::iwadx::Oir(a0,o0);Ver(nuevo&&e.detener_adx_anteriores()&&!mezcla.Posicion(nuevo,cursor),"registro admite y retira ADX posteriores");
      Ver(mezcla.Posicion(midi,cursor)&&mezcla.Posicion(otro,cursor),"actores conservados despues de segunda retirada");e.midi.Abortar();mezcla.Parar(otro);
      std::cout<<"RPC1D MVOL real ambos cores+canales, Maestro0.25 y ToDefine registro ADX selectivo PASS\n";
    }
    Ver(mezcla.Voces()==0,"sin voces residuales");std::cout<<"PASS "<<n<<" API audio REAL CapsulePick\n";return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL audio real "<<n<<" "<<e.what()<<'\n';return 1;}
}

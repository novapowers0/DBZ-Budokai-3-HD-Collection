// Reutiliza el soporte del arnes anterior, NO su main ni sus expectativas de
// bloqueo RPC13. El bloque CapsulePick incluido es el de produccion actual.
#include "../src/iw_midi.h"
#define main iw_capsulas_F2_main_no_ejecutado
#include "iw_capsulas_F2_motor.cpp"
#undef main
namespace dbz3::settings {double MasterVolume(){return 1;}bool AudioMute(){return false;}}
bool NoCeroMidi(const std::vector<int16_t>& b){for(auto x:b)if(x)return true;return false;}
int main(int argc,char** argv) {
  try {
    Ver(argc==2,"fixture requerido");std::ifstream f(argv[1],std::ios::binary);C::Bytes bytes(std::istreambuf_iterator<char>(f),{});size_t p=0;
    auto word=[&](){auto v=C::U(bytes,p);p+=4;return v;};
    auto blob=[&](){const auto k=word();C::Bytes b(bytes.begin()+p,bytes.begin()+p+k);p+=k;return b;};
    dbz3::pasarela::elf_fixture=blob();const auto nb=word();for(uint32_t i=0;i<nb;++i){const auto id=word();dbz3::pasarela::bancos_fixture[id]=blob();}
    std::map<uint32_t,uint16_t> mvol;const auto nvol=word();
    for(uint32_t i=0;i<nvol;++i) {
      const auto valor=word(),registro=word(),nr=word();mvol[valor]=uint16_t(registro);Ver(nr==4,"RPC1D escribe cuatro registros MVOL");
      std::vector<uint32_t> entries;for(uint32_t j=0;j<nr;++j){entries.push_back(word());Ver(word()==registro,"MVOL valor identico en ambos cores");}
      Ver(entries==std::vector<uint32_t>{0x980,0xa80,0x981,0xa81},"MVOL L/R de core0 y core1 original");
    }
    auto& salida=dbz3::iwsonido::Salida();Ver(salida.DispositivoAutomatico(false),"ningun altavoz abierto");
    auto* ctx=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={640,448};io.DeltaTime=1.f/60;
    unsigned char* px;int w,h;io.Fonts->GetTexDataAsRGBA32(&px,&w,&h);io.Fonts->SetTexID(ImTextureID(1));
    ImGui::NewFrame();ImGui::Begin("##midi_F3");auto* dl=ImGui::GetWindowDrawList();
    for(int id:{993,994}) {
      dbz3::pasarela::PruebaCapsulasSpx escena("fixture",id);Ver(escena.carga,"CapsulePick carga original45");
      escena.dl=dl;escena.cam.c={320,224};escena.cam.h={320,224};
      escena.motor.Tick(0);Ver(escena.motor.llamadas[0x75]>0,"el guion original ejecuto builtin75/RPC13");
      Ver(escena.midi.Preparada()&&escena.midi.Handle()!=0,"puente real RPC13 entrega handle del mezclador");
      std::vector<int16_t> pcm(96000);salida.Procesar(pcm.data(),48000);Ver(NoCeroMidi(pcm),"PCM real del puente CapsulePick/Salida.Procesar");
      escena.midi.posicion_audio=[&](dbz3::iwsonido::Handle x,uint64_t& cursor){if(x!=escena.midi.Handle())return false;cursor=48000;return true;};
      Ver(escena.RpcCapsulas(0x14,0),"puente RPC14 real conserva KeyOff");std::vector<int16_t> tail(48000);salida.Procesar(tail.data(),24000);
      Ver(NoCeroMidi(tail)&&tail.back()==0,"puente14 emite cola y despues se corta realmente");
      escena.motor.Dibujar();const auto error=escena.motor.vm.Error();
      Ver(error.find("RPC13")==std::string::npos,"RPC13 ya no bloquea");
      Ver(escena.motor.vm.Bloqueada()&&(error.find("AME972")!=std::string::npos||error.find("GS")!=std::string::npos),"se conserva bloqueo AME/GS especifico");
      std::cout<<"SPX"<<id<<" RPC13+14 salida PCM PASS; bloqueo restante: "<<error<<'\n';escena.midi.Abortar();
    }
    // Host de transicion: sin MIDI activo, con UN ADX de estimulo registrado.
    // El callback retira ese handle real; no simula un exito ni limpia otros.
    {
      dbz3::pasarela::PruebaCapsulasSpx escena("fixture",994);escena.dl=dl;escena.cam.c={320,224};escena.cam.h={320,224};escena.motor.vm.Parar();
      C::Bytes spx(36);std::memcpy(spx.data(),"#SPX",4);spx[20]=36;spx[24]=1;spx.insert(spx.end(),128,0x92);
      Ver(escena.motor.vm.Cargar(spx,dbz3::iw::spx::kCapsulasEU)&&escena.motor.vm.Iniciar()==0,"task quieto de estimulo transicion");
      // Solo core1 en este escenario: MVOL equivale a este factor. En la UI
      // real el puente debe implementar ambos cores y conservar user-volume.
      salida.UsarProveedor(nullptr);salida.Mezcla().Maestro(1);
      std::vector<int16_t> adx(96000,1000);dbz3::iwsonido::Opciones opciones;opciones.core=1;opciones.rutas=3;
      const auto handle_adx=salida.Enviar(adx,48000,opciones);Ver(handle_adx!=0,"ADX de estimulo con handle real");
      uint16_t ultimo_mvol=0;
      escena.volumen_spu2=[&](uint16_t registro){
        const auto esperado=mvol.at(dbz3::iw::spx::Bits(1-escena.fundido.valor));Ver(registro==esperado,"host RPC1D coincide con instrucciones EE/IOP originales");
        ultimo_mvol=registro;salida.Mezcla().Maestro(float(registro)/16384);return true;
      };
      int paradas=0;escena.detener_adx_anteriores=[&]{++paradas;return salida.Mezcla().Parar(handle_adx);};
      escena.fundido.valor=.25f;Ver(escena.motor.host.transicion(10),"host10 conserva progreso y acepta registro ADX real");escena.fundido.velocidad=.125f;
      for(int i=0;i<8;++i) {
        Ver(escena.PasoCapsulas(0),"host10 progresa sin anular VM");std::vector<int16_t> bloque(128);salida.Procesar(bloque.data(),64);
        Ver(bloque[0]==int16_t((int64_t(1000)*ultimo_mvol)>>14),"fundido cambia salida ADX real segun MVOL original");
      }
      Ver(escena.fundido.valor==1&&paradas==1,"ADX detenido UNA vez al cruzar1");
      Ver(salida.Mezcla().Voces()==0,"ToDefine retira la voz, no solo la silencia");salida.Mezcla().Maestro(1);
      std::vector<int16_t> despues(512);salida.Procesar(despues.data(),256);Ver(!NoCeroMidi(despues),"ToDefine host corto el ADX real");
      const int inicio=dl->VtxBuffer.Size;escena.Cuadro(dl,io,0,0);int negros=0;
      for(int i=inicio;i+3<dl->VtxBuffer.Size;++i)if(dl->VtxBuffer[i].col==IM_COL32_BLACK&&dl->VtxBuffer[i+1].col==IM_COL32_BLACK&&
          dl->VtxBuffer[i+2].col==IM_COL32_BLACK&&dl->VtxBuffer[i+3].col==IM_COL32_BLACK&&
          dl->VtxBuffer[i].pos.x==0&&dl->VtxBuffer[i].pos.y==0&&dl->VtxBuffer[i+2].pos.x==640&&dl->VtxBuffer[i+2].pos.y==448)++negros;
      Ver(negros>=2,"host emite fondo y quad negro de transicion, no solo valor guardado");
      std::cout<<"HOST transicion10: MVOL EE+IOP/PCM ADX/quad/stop real PASS; puentes MVOL ambos cores y ADX heredado UI pendientes\n";
    }
    ImGui::End();ImGui::Render();ImGui::DestroyContext(ctx);std::cout<<"PASS "<<n<<" comprobaciones puente MIDI/host10\n";return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL puente MIDI "<<n<<" "<<e.what()<<'\n';return 1;}
}

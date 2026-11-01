// Compila los cuerpos ACTUALES de CapsulePick, sin abrir el juego ni altavoces.
#include "../src/iw_midi.h"
#include "../src/iw_adx.h"
#include "../src/iw_azar.h"
#include "../src/iw_capsulas_render.h"
#define main iw_capsulas_F2_historico_no_ejecutado
#include "iw_capsulas_F2_motor.cpp"
#undef main
namespace dbz3::settings {double MasterVolume(){return 1;}bool AudioMute(){return false;}}
int main(int argc,char** argv) {
  try {
    Ver(argc==2,"bancos de fixture");std::ifstream f(argv[1],std::ios::binary);C::Bytes b(std::istreambuf_iterator<char>(f),{});size_t p=0;
    auto word=[&](){auto v=C::U(b,p);p+=4;return v;};auto blob=[&](){auto n=word();C::Bytes out(b.begin()+p,b.begin()+p+n);p+=n;return out;};
    dbz3::pasarela::elf_fixture=blob();const auto nb=word();for(unsigned i=0;i<nb;++i){const auto id=word();dbz3::pasarela::bancos_fixture[id]=blob();}
    Ver(dbz3::iwsonido::Salida().DispositivoAutomatico(false),"no se abre dispositivo de sonido");
    auto& azar=dbz3::iwazar::Global();azar.Cambiar(0x87654321);
    dbz3::pasarela::PruebaCapsulasSpx escena("fixture",993);Ver(escena.carga,"SPX993 carga sus bancos originales");
    auto objeto=std::dynamic_pointer_cast<dbz3::iw::capsrender::ObjetoAme>(escena.efectos.slots[0]);
    Ver(bool(objeto)&&objeto->Nodos()==101,"la UI instala el consumidor AME nativo");
    Ver(azar.Estado()==0x87654321,"la carga no reinicia ni consume RNG global");
    Ver(escena.efectos.Consumir(0xD6,0,false),"D6 captura estado inicial");
    Ver(objeto->estado.cuadros==0&&azar.Estado()==0x87654321,"dibujar no avanza ni usa azar");
    Ver(escena.efectos.Consumir(0xD5,0,false),"D5 avanza motor nativo");
    Ver(objeto->estado.cuadros==1&&azar.Estado()==0xd98ee273,"primer cuadro/RNG coincide con EE original");
    Ver(escena.efectos.Consumir(0xD5,0,true)&&objeto->estado.cuadros==1,"pausa no actualiza AME");
    while(objeto->estado.cuadros<310)Ver(escena.efectos.Consumir(0xD5,0,false),"D5 encadenado");
    Ver(objeto->Nodos()==79&&objeto->estado.nacidas==65&&azar.Estado()==0x3176472e,"cadena real integrada hasta cuadro310");
    Ver(escena.efectos.Consumir(0xD6,0,false)&&escena.motor.pases.size()==2,"D6 encola snapshot sin avanzar");
    Ver(escena.efectos.Consumir(0xD4,0,true)&&!escena.efectos.slots[0],"D4 retira incluso en pausa");
    Ver(objeto->Nodos()==-1,"D4 deja objeto retirado");objeto.reset();
    Ver(escena.motor.pases[0](),"snapshot vacio conservado tras D4");
    Ver(!escena.motor.pases[1](),"el snapshot vivo no acepta un renderer sin contraste");
    Ver(escena.motor.vm.Error().find("falta dibujo GS")!=std::string::npos,"bloqueo restante es dibujo, no cadenas omitidas");
    Ver(azar.Estado()==0x3176472e,"presentar snapshots no toca RNG");
    dbz3::pasarela::PruebaCapsulasSpx siguiente("fixture",994);
    Ver(siguiente.carga&&azar.Estado()==0x3176472e,"la siguiente pantalla conserva el RNG heredado");
    auto& banco=dbz3::pasarela::bancos_fixture.at(972);const auto version=banco[4];banco[4]=3;
    dbz3::pasarela::PruebaCapsulasSpx invalida("fixture",993);banco[4]=version;
    Ver(!invalida.carga&&invalida.motor.vm.Bloqueada(),"AME fuera del formato probado bloquea la UI sin excepcion sin capturar");
    std::cout<<"PASS "<<n<<" puente AME nativo/D4-D6/capturas/RNG compartido; GS completo pendiente\n";return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL puente AME "<<n<<" "<<e.what()<<'\n';return 1;}
}

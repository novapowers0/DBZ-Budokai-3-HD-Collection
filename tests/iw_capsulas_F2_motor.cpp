// Bloque CapsulePick extraido sin cambios por el arnes. AMO/AMM/AMT y
// generacion de vertices ImGui reales; frontera GPU/logging sustituida.
#include "../src/iw_spx_capsulas.h"
#include "../src/iw_ame.h"
#include "../src/iw_iop.h"
#include <fstream>
#include <iostream>
#include <iterator>
namespace C=dbz3::iw::spx::capsulas;
namespace dbz3::pasarela {
static C::Bytes elf_fixture;
static std::map<uint32_t,C::Bytes> bancos_fixture;
struct AfsPs2::Impl { bool local=false; };
bool AfsPs2::Abrir(const std::filesystem::path&,const std::string& afs) {
  d_=std::make_shared<Impl>();d_->local=afs=="DATA_USA.AFS";return !bancos_fixture.empty();
}
std::vector<uint8_t> AfsPs2::Entrada(size_t i,uint32_t) {return bancos_fixture.at(uint32_t(i)|(d_->local?65536:0));}
static C::Bytes LeerElf(const std::filesystem::path&) {return elf_fixture;}
namespace S=dbz3::ingame::style;
constexpr uint16_t kStart=8,kSelect=1;
}
namespace dbz3::ingame::style {
Area GameArea(const ImGuiIO& io){const float s=io.DisplaySize.y/900;return {{0,0},io.DisplaySize,s};}
}
namespace rex::cvar {
inline std::string GetFlagByName(const std::string&){return "0";}
inline bool SetFlagByName(const std::string&,const std::string&){return true;}
}
namespace dbz3::ps2 {
int EstadoProfundidad(){return 0;}
struct TexturaCPU:rex::ui::ImmediateTexture {TexturaCPU(int w,int h):ImmediateTexture(uint32_t(w),uint32_t(h)) {}};
Tex Crear(const pasarela::Imagen& im,bool){Tex t;t.t=std::make_unique<TexturaCPU>(im.w,im.h);t.w=float(im.w);t.h=float(im.h);return t;}
std::vector<Tex> Texturas(const Modelo& m){std::vector<Tex> out;for(const auto& im:m.tex)out.push_back(Crear(im,false));return out;}
}
#include DBZ3_CAPSULAS_F2_CUERPOS
size_t n=0;
void Ver(bool b,const char* m){++n;if(!b)throw std::runtime_error(m);}
void Poner(C::Bytes& b,size_t p,uint32_t v){for(unsigned j=0;j<4;++j)b.at(p+j)=uint8_t(v>>(8*j));}
int main(int argc,char** argv) {
  try {
    Ver(argc==2,"fixture requerido");std::ifstream file(argv[1],std::ios::binary);C::Bytes bytes(std::istreambuf_iterator<char>(file),{});size_t p=0;
    auto word=[&](){const auto v=C::U(bytes,p);p+=4;return v;};
    auto blob=[&](){const auto tam=word();Ver(p+tam<=bytes.size(),"fixture truncado");C::Bytes b(bytes.begin()+p,bytes.begin()+p+tam);p+=tam;return b;};
    dbz3::pasarela::elf_fixture=blob();const auto bancos=word();for(uint32_t i=0;i<bancos;++i){const auto id=word();dbz3::pasarela::bancos_fixture[id]=blob();}
    auto* ctx=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize=ImVec2(640,448);io.DeltaTime=1.f/60;unsigned char* pix;int w,h;io.Fonts->GetTexDataAsRGBA32(&pix,&w,&h);io.Fonts->SetTexID(ImTextureID(1));
    ImGui::NewFrame();ImGui::Begin("##capsulas_F2");auto* dl=ImGui::GetWindowDrawList();
    for(int id:{993,994}) {
      dbz3::pasarela::PruebaCapsulasSpx prueba("fixture",id);Ver(prueba.carga,"constructor real CapsulePick");
      prueba.Cuadro(dl,io,0,0);
      std::cout<<"SPX"<<id<<" primer limite REAL integrado: "<<prueba.motor.vm.Error()<<" cuadro="<<prueba.motor.vm.Cuadro()<<" ultimo="<<std::hex<<prueba.motor.ultimo_id<<std::dec<<" capturas="<<prueba.motor.pases.size()<<'\n';
      Ver(prueba.motor.vm.Bloqueada(),"no se habilita una pantalla incompleta");
      Ver(prueba.motor.vm.Error().find("RPC13 inicia MIDI")!=std::string::npos || prueba.motor.vm.Error().find("AME972")!=std::string::npos,
          "AMM/AML/AMP ya no son el primer bloqueo del Tick integrado");
      // El mismo consumidor que usa el juego produce vertices para un pase
      // simple admitido; la captura no vuelve a leer la pose al dibujar.
      prueba.motor.vm.Parar();prueba.motor.pases.clear();
      Ver(prueba.Modelo(1),"AMO/AMT real");auto& actor=prueba.motor.actores[1];
      Ver(prueba.motor.host.pose(prueba.motor,1,0x10000,0,false),"AMM real aplicada por bloque integrado");
      C::Trabajo trabajo;trabajo.sin_niebla=true;trabajo.u=.25f;trabajo.v=.125f;
      auto a=actor;a.niebla=0;a.param_c8=a.param_cc=0;
      const auto triangulos=prueba.modelos[1].pose.tris.size();
      auto pase=prueba.motor.host.capturar_actor(prueba.motor,1,a,trabajo);
      const int antes=dl->VtxBuffer.Size;Ver(pase(),"renderer simple con UV admitido");
      Ver(dl->VtxBuffer.Size>antes,"consumidor real entrega vertices ImGui");
      std::cout<<"SPX"<<id<<" AMM+UV: "<<triangulos<<" triangulos fuente, "<<dl->VtxBuffer.Size-antes<<" vertices emitidos\n";
      trabajo.sin_niebla=false;auto fog=prueba.motor.host.capturar_actor(prueba.motor,1,a,trabajo);
      Ver(!fog(),"doble contexto GS no se simula con dos copias");
      trabajo.sin_niebla=true;a.niebla=1;auto light=prueba.motor.host.capturar_actor(prueba.motor,1,a,trabajo);
      Ver(!light(),"sin shader VU de luz, no devolver exito por guardar matrices");
    }
    const auto raw=dbz3::pasarela::bancos_fixture.at(972);
    uint32_t sem=0;Ver(dbz3::iwiop::LeerSemillaAzar(dbz3::pasarela::elf_fixture,sem),"semilla ELF, no constante inventada");
    dbz3::pasarela::AmeCapsulasSpx completo(raw,sem);Ver(!completo.Regular(),"motor reducido rechaza cadena original972");
    // Subconjunto ORIGINAL de972: solo el primer emisor y sus dos luces
    // infinitas. No es una afirmacion sobre las otras siete cadenas del banco.
    auto subset=raw;Poner(subset,C::U(raw,12)+0x28,0);
    auto reducido=std::make_shared<dbz3::pasarela::AmeCapsulasSpx>(subset,sem);Ver(reducido->limite.empty(),"subarbol sin cadena soportado");
    C::CicloAme ciclo;Ver(ciclo.Instalar(0,reducido),"slot AME real");
    size_t dibujados=0;reducido->capturar=[&](const auto& in){for(const auto& lista:in.ps)dibujados+=lista.size();return true;};
    for(int i=0;i<120;++i){Ver(ciclo.Consumir(0xD5,0,false),"D5 avanza motor actual");Ver(ciclo.Consumir(0xD6,0,false),"D6 dibuja sin avanzar");}
    Ver(reducido->estado.edad==120&&reducido->estado.vivo,"vida negativa no caduca a60");
    const auto edad=reducido->estado.edad;Ver(ciclo.Consumir(0xD5,0,true)&&reducido->estado.edad==edad,"pausa real");
    Ver(ciclo.Consumir(0xD4,0,true)&&!ciclo.slots[0]&&!reducido->estado.vivo,"D4 retira incluso en pausa");
    Ver(dibujados>120,"particulas originales conservadas mas alla de60");
    dbz3::pasarela::PruebaCapsulasSpx lease("fixture",994);lease.dl=dl;lease.cam.c={320,224};lease.cam.h={320,224};
    auto objeto=std::make_shared<dbz3::pasarela::AmeCapsulasSpx>(subset,lease.azar);
    const std::weak_ptr<dbz3::pasarela::AmeCapsulasSpx> debil=objeto;
    objeto->capturar=[&lease,debil](const auto& in){return lease.CapturarAme(debil,in);};
    Ver(lease.efectos.Instalar(0,objeto)&&lease.efectos.Consumir(0xD5,0,false)&&lease.efectos.Consumir(0xD6,0,false),"captura AME real con atlas973 independiente de AMO974");
    Ver(lease.atlas_ame.size()==size_t(dbz3::pasarela::CuantasAmt(dbz3::pasarela::bancos_fixture.at(973))),"atlas completo del original");
    Ver(lease.efectos.Consumir(0xD4,0,false),"retirar despues de capturar");objeto.reset();
    Ver(!debil.expired()&&lease.motor.pases.size()==1,"D4 no invalida las definiciones de una captura pendiente");
    Ver(lease.motor.pases[0](),"captura AME se dibuja despues de retirar el slot");lease.motor.pases.clear();
    Ver(debil.expired(),"liberacion real sin ciclo de shared_ptr");
    ImGui::End();ImGui::Render();ImGui::DestroyContext(ctx);
    std::cout<<"PASS motor F2 "<<n<<" comprobaciones; GPU y salida sonora NO ejecutadas\n";return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL motor F2 "<<n<<" "<<e.what()<<'\n';return 1;}
}

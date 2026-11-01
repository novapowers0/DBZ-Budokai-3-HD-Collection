// Integracion REAL extraida de anim_iw/iw_minijuegos; sin SDK ni dispositivos.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../src/iw_ap_consumidores.h"

void Exigir(bool v,const char* m){if(!v)throw std::runtime_error(m);}
std::vector<uint8_t> g_elf;
#include IW_AP_F_AZAR
std::map<uint32_t,uint8_t> ram;
uint32_t R(uint32_t a,unsigned n=4){uint32_t r=0;for(unsigned i=0;i<n;++i)r=(r<<8)|ram[a+i];return r;}
void W(uint32_t a,uint32_t v,unsigned n=4){for(unsigned i=0;i<n;++i)ram[a+n-1-i]=uint8_t(v>>(8*i));}
void WF(uint32_t a,float v){W(a,std::bit_cast<uint32_t>(v));}
#define REX_LOAD_U32(a) R(a)
#define REX_LOAD_U16(a) uint16_t(R(a,2))
#define REX_LOAD_U8(a) uint8_t(R(a,1))
#define REX_STORE_U32(a,v) W(a,v)
std::string logs;
template<class... T>void Log(const char* fmt,T&&... args){std::ostringstream s;s<<fmt;((s<<' '<<args),...);logs+=s.str()+'\n';}
#define REXLOG_INFO(...) Log(__VA_ARGS__)
#define REXLOG_WARN(...) Log(__VA_ARGS__)
#define IM_COL32(r,g,b,a) (uint32_t(r)|(uint32_t(g)<<8)|(uint32_t(b)<<16)|(uint32_t(a)<<24))
using ImU32=uint32_t;using ImDrawIdx=uint32_t;using ImTextureID=uint64_t;
struct ImVec2 {float x=0,y=0;ImVec2()=default;ImVec2(float a,float b):x(a),y(b){}};
struct ImDrawList {
  unsigned _VtxCurrentIdx=0,indices=0,callbacks=0;
  void AddCallback(void(*)(const void*,const void*),void*){++callbacks;}
  void PushTextureID(ImTextureID){}void PopTextureID(){}void PrimReserve(int,int){}
  void PrimWriteIdx(ImDrawIdx){++indices;}
  void PrimWriteVtx(ImVec2 p,ImVec2,ImU32){Exigir(std::isfinite(p.x)&&std::isfinite(p.y),"vertice no finito");++_VtxCurrentIdx;}
  void AddImageQuad(ImTextureID,ImVec2,ImVec2,ImVec2,ImVec2,ImVec2,ImVec2,ImVec2,ImVec2,ImU32){_VtxCurrentIdx+=4;indices+=6;}
};
namespace ImGui {struct IO {ImVec2 DisplaySize{960,720};};IO& GetIO(){static IO i;return i;}}
namespace rex::ui {void Blend(const void*,const void*){}inline auto kImDrawBlendAdditive=Blend,kImDrawBlendAlpha=Blend;}
namespace dbz3::iwgs {void Resta(const void*,const void*){}void Inversa(const void*,const void*){}}
namespace dbz3 {struct GA {uint32_t a;constexpr GA(uint32_t v):a(v){}operator uint32_t()const{return a;}};}
namespace dbz3::ps2 {
struct V3 {float x=0,y=0,z=0;};
struct Tex {bool valido=true;explicit operator bool()const{return valido;}ImTextureID id()const{return 1;}};
struct Camara {
  float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};ImVec2 c{480,360};bool ok=true;
  float Foco()const{return c.y/std::tan(.4f);}
  bool P(V3 p,ImVec2& xy,float& k,float foco)const {
    if(p.z>=-1)return false;k=foco/-p.z;xy={c.x+p.x*k,c.y-p.y*k};return std::isfinite(k);
  }
};
}
#include IW_AP_F_AME
namespace dbz3::iwame::ame {
template<class T>size_t PintarModelos(const Instancia&,const ps2::Camara&,const T&,const std::vector<ps2::Tex>&,ImDrawList*){throw std::runtime_error("rama de modelos ajena al ensayo");}
}
std::vector<uint8_t> File(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);Exigir(bool(f),"archivo");const auto n=f.tellg();std::vector<uint8_t>b(size_t(n),0);f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);return b;}
std::filesystem::path g_game;
namespace dbz3::pasarela {
using namespace anim::apev;
std::filesystem::path BuscarIW(){return g_game;}
std::vector<uint8_t> LeerElf(const std::filesystem::path&){return g_elf;}
struct AfsPs2 {
  std::filesystem::path path;
  bool Abrir(const std::filesystem::path& p,const char* name){path=p/"USR"/name;return std::filesystem::exists(path);}
  std::vector<uint8_t> Entrada(size_t id) {
    std::ifstream f(path,std::ios::binary);uint32_t n;f.seekg(4);f.read(reinterpret_cast<char*>(&n),4);Exigir(id<n,"AFS indice");
    uint32_t p,size;f.seekg(8+id*8);f.read(reinterpret_cast<char*>(&p),4);f.read(reinterpret_cast<char*>(&size),4);
    std::vector<uint8_t>b(size);f.seekg(p);f.read(reinterpret_cast<char*>(b.data()),size);Exigir(bool(f),"AFS lectura");return b;
  }
};
std::vector<uint8_t> HijoAmb(const std::vector<uint8_t>& b,unsigned k){if(U32(b,0)!=0x424d4123 || k>=U32(b,16))return {};size_t t=U32(b,20)+16*k,p=U32(b,t),n=U32(b,t+4);return Rango(b,p,n)?std::vector<uint8_t>(b.begin()+p,b.begin()+p+n):std::vector<uint8_t>{};}
}
namespace dbz3::settings {bool mute=false;bool AudioMute(){return mute;}float MasterVolume(){return .5f;}}
namespace dbz3::iwsonido {
struct Canal {bool pausa=false;};struct Opciones {std::shared_ptr<Canal> canal;};
struct Mixer {std::shared_ptr<Canal> Slot(uint64_t){return std::make_shared<Canal>();}};
struct Output {Mixer m;Mixer& Mezcla(){return m;}};Output& Salida(){static Output o;return o;}
}
namespace dbz3::iwiop {
struct Voz {std::vector<int16_t> pcm;uint32_t muestra=1;bool silencio=false,secuencia_ausente=false;};
int oidas=0;Voz Leer(const std::vector<uint8_t>&,int s){Voz v;if(s==88)v.silencio=v.secuencia_ausente=true;else v.pcm={1,1};return v;}
void Oir(const Voz&,iwsonido::Opciones){++oidas;}
}
namespace dbz3::iwadx {
struct Canal{bool pausa=false;};struct Pcm {std::vector<int16_t>muestras;uint32_t hz=48000;};Pcm Leer(const std::vector<uint8_t>&){return {};}
void Oir(const Pcm&,const std::shared_ptr<Canal>&,float){}
}
struct PPCContext{};
namespace dbz3::iwmini {
using namespace ps2;namespace P=pasarela;
constexpr uint32_t kMgrPtr=0x823d6cf4,kX0=2096,kXPaso=8240;
struct Datos {std::array<iwame::ame::Efecto,2>impactos_ap;std::vector<int>atlas_impactos_ap;std::array<iwame::ame::Efecto,4>pasos_ap;std::array<iwame::ame::Efecto,3>kami_fx;std::vector<int>kami_modelos;float escala=1;bool cuerpo_iw=false;int indice=0;};
std::shared_ptr<Datos> datos;std::shared_ptr<Datos> Actual(){return datos;}
std::atomic<int>g_estado{2};std::atomic<bool>g_modulo{false};
bool CamaraDelRenderIw(float* m,float* f){for(int i=0;i<16;++i)m[i]=i%5==0?1.0f:0.0f;*f=.8f;return true;}
struct PasoAp{int hijo;V3 p;};std::mutex g_pasos_ap_mu;std::vector<PasoAp>g_pasos_ap;
struct ImpactoAp{int tipo;V3 p;std::array<float,4>q;const Datos* campo;};std::mutex g_impactos_ap_mu;std::vector<ImpactoAp>g_impactos_ap;
int EscenaApCampo();bool EventoApCampo(uint8_t*,uint32_t,int,int&);bool EventoApImpacto(uint8_t*,uint32_t,int,const float*);
bool PuntoCuerpo(uint8_t*,uint32_t x,int,ps2::V3& p){p={0,0,-40};return x!=0;}
namespace suelo_stub {int TipoSuelo(float,float){return 0;}float Altura(float,float,float){return 0;}}
}
namespace dbz3::suelo_iw {int TipoSuelo(float x,float z,float=1e30f){return iwmini::suelo_stub::TipoSuelo(x,z);}float Altura(float x,float z,float f){return iwmini::suelo_stub::Altura(x,z,f);}}
namespace dbz3::anim {
namespace P=pasarela;
constexpr uint32_t kAnimObj=0x650,kModelo=0x538,kMgrPtr=0x823d6cf4,kX0=2096,kXPaso=8240;
struct Anim{using Evento=eventos::Registro;uint32_t cmn=915,cuadros=100;std::vector<Evento> eventos;};
std::map<uint32_t,Anim>animaciones;const Anim* AnimDe(uint32_t c){auto it=animaciones.find(c);return it==animaciones.end()?nullptr:&it->second;}
uint32_t Abs(uint32_t v,uint32_t p){return v&&v<0x01000000u?p+v:v;}
void MarcarEventoSonoro(uint32_t){}
float VelocidadAP(uint32_t,float,float = 1.0f,float = 1.0f);
}
#include IW_AP_F_LOGICA
#include IW_AP_F_PANEL

uint32_t Seed(){uint32_t s=0;dbz3::iwazar::Global().ConEstado([&](uint32_t& v){s=v;});return s;}
uint32_t Memory(uint32_t& s,int n){while(n-->0)s=(s<<1)|(((s>>31)^(s>>4))&1);return s-1;}
void Rig(uint32_t x,uint32_t model,uint32_t h,uint32_t names,uint32_t mats,uint32_t desc,const std::string& wanted) {
  W(x+0x650+0x538,model);W(model+4,h);W(h,0x2141574f);W(h+0x20,3);W(h+0x24,names);W(model+0x408,mats);W(x+0x650+0x53c,desc);W(desc,2,2);
  for(unsigned i=0;i<3;++i){const auto s=i==0?"GOK_"+wanted:i==1?"OTHER":"ROOT";for(size_t k=0;k<s.size();++k)ram[names+32*i+uint32_t(k)]=uint8_t(s[k]);}
  for(unsigned i=0;i<3;++i){WF(mats+i*64+48,float(i+1));WF(mats+i*64+52,float(i+2));WF(mats+i*64+56,float(i+3));}
  for(unsigned i=0;i<16;++i)WF(x+i*4,i%5==0?1.0f:0.0f);WF(x+56,-40);
  W(x+7420,0);
}
int main(int argc,char** argv) {
  try {
    Exigir(argc==2,"ruta IW");g_game=argv[1];g_elf=File(g_game/"SLUS_218.42");using namespace dbz3;
    iwazar::Global().Iniciar(g_elf);iwazar::Global().Cambiar(0x12345678);iwazar::Global().Iniciar(g_elf);
    Exigir(Seed()==0x12345678,"Iniciar repetido resetea RNG");
    _putenv_s("DBZ3_IW_EVENTOS_LOG","1");_putenv_s("DBZ3_IW_EVENTOS_OFF","");
    const uint32_t mgr=0x50000000,x=mgr+2096;W(0x823d6cf4,mgr);
    anim::animaciones[42].eventos={{0,9,6}};float anterior=-1;PPCContext ctx;
    anim::AvanzarEventos(ctx,nullptr,x,42,anterior,0);
    Exigir(std::fabs(iwmini::VelocidadCampoAp(x,35,.4f)-21)<1e-5f,"AP9 no llega al scatto real");
    Exigir(std::fabs(iwmini::VelocidadCampoAp(x,1.8f,.4f)-1.08f)<1e-6f,"AP9 no llega al andar real");
    anim::PrepararContextoAp(x,42);Exigir(std::fabs(anim::VelocidadAP(x,4.5f,1,1)-2.7f)<1e-6f,"re-arme borra AP9/NPC");
    anim::animaciones[43];anterior=-1;anim::AvanzarEventos(ctx,nullptr,x,43,anterior,0);
    Exigir(anim::VelocidadAP(x,35,1,1)==14,"animacion nueva no reinicia AP9");
    anim::animaciones[45].eventos={{5,9,6}};anterior=-1;anim::AvanzarEventos(ctx,nullptr,x,45,anterior,6);
    Exigir(std::fabs(anim::VelocidadAP(x,35,1,1)-21)<1e-5f,"AP9 antes de envolver");
    anim::AvanzarEventos(ctx,nullptr,x,45,anterior,1);Exigir(anim::VelocidadAP(x,35,1,1)==14,"vuelta sin9 conserva factor viejo");
    logs.clear();anim::DispararEvento(nullptr,x,43,{0,0,200});Exigir(logs.find("suprimido por escena IW0")!=std::string::npos,"AP0 campo usa escena B3");
    uint32_t s=Seed();Memory(s,32);anim::DispararEvento(nullptr,x,43,{0,3,101});Exigir(Seed()==s,"voz no usa Memory32 compartida");
    logs.clear();anim::DispararEvento(nullptr,x,43,{0,1,88});Exigir(logs.find("sin secuencia para ese id")!=std::string::npos && logs.find("volumen/velocidad cero")==std::string::npos,"mensaje secuencia ausente");
    iwmini::datos=std::make_shared<iwmini::Datos>();auto& d=*iwmini::datos;
    pasarela::AfsPs2 afs;Exigir(afs.Abrir(g_game,"DATA_CMN.AFS"),"CMN");auto bundle=afs.Entrada(813);
    d.impactos_ap[0]=iwame::ame::Leer(pasarela::HijoAmb(bundle,1),true);d.impactos_ap[1]=iwame::ame::Leer(pasarela::HijoAmb(bundle,6),true);
    const auto atlas=pasarela::HijoAmb(bundle,0);const auto texturas=anim::apev::U32(atlas,16);Exigir(texturas>0,"atlas813");d.atlas_impactos_ap.resize(texturas);
    const auto off=anim::apev::ElfOff(g_elf,0x4586d4+9*4,4),name=anim::apev::ElfOff(g_elf,anim::apev::U32(g_elf,off),1);
    Exigir(name<g_elf.size(),"rol10 ELF");std::string wanted(reinterpret_cast<char*>(g_elf.data()+name));
    Rig(x,0x60000000,0x60001000,0x60002000,0x60003000,0x60004000,wanted);
    Rig(x+8240,0x60100000,0x60101000,0x60102000,0x60103000,0x60104000,wanted);
    anim::apev::V3 p;Exigir(anim::AnclaImpactoAp(nullptr,x,10,g_elf,p)&&p.x==1&&p.y==2&&p.z==-37,"ancla10 no se enlaza por nombre");
    anim::animaciones[44].eventos={{0,5,1100},{1,5,1101},{2,5,1902}};anterior=-1;
    anim::AvanzarEventos(ctx,nullptr,x,44,anterior,0);Exigir(iwmini::g_impactos_ap.empty(),"AP5 se genera durante el mando");
    WF(0x60003000+48,7);anim::ConsumirImpactosAP(nullptr);Exigir(iwmini::g_impactos_ap.size()==1 && iwmini::g_impactos_ap[0].p.x==7,"AP5 no usa ancla post-logica actual");
    anim::ConsumirImpactosAP(nullptr);Exigir(iwmini::g_impactos_ap.size()==1,"AP5 duplicado");
    iwmini::PanelPrueba panel;panel.tx_impactos_ap_.resize(texturas);panel.tx_pasos_ap_.resize(64);ImDrawList dl;ps2::Camara cam;
    unsigned vertices0=0;for(int f=0;f<24;++f){panel.ImpactosAp(&dl,cam,d);vertices0=dl._VtxCurrentIdx;}
    Exigir(vertices0>0 && dl.indices>0,"AME813/1 no dibuja primitivas reales");
    anim::AvanzarEventos(ctx,nullptr,x,44,anterior,1);anim::ConsumirImpactosAP(nullptr);
    const auto antes=dl._VtxCurrentIdx;for(int f=0;f<24;++f)panel.ImpactosAp(&dl,cam,d);
    Exigir(dl._VtxCurrentIdx>antes,"AME813/6 no dibuja primitivas reales");
    anim::AvanzarEventos(ctx,nullptr,x,44,anterior,2);logs.clear();anim::ConsumirImpactosAP(nullptr);
    Exigir(iwmini::g_impactos_ap.empty() && logs.find("pendiente")!=std::string::npos,"subtipo2 simula consumo");
    // AP6 real: exactamente el mismo emisor con semilla local de referencia;
    // comprueba el CABLEADO compartido, no reescribe Cuadro ni sus formulas.
    auto foot=pasarela::HijoAmb(afs.Entrada(905),3);d.pasos_ap[0]=iwame::ame::Leer(foot,true);
    Exigir(bool(d.pasos_ap[0]),"paso905/3");iwame::ame::Instancia referencia;referencia.ef=&d.pasos_ap[0];referencia.origen={0,0,-40};referencia.mezcla_gs=true;
    uint32_t esperado=Seed();int sonido;Exigir(iwmini::EventoApCampo(nullptr,x,1,sonido),"consumidor AP6");
    for(int f=0;f<12;++f){iwame::ame::Cuadro(referencia,esperado);panel.PasosAp(&dl,cam,d);Exigir(Seed()==esperado,"AP6 usa semilla privada");}
    const auto despues6=Seed();Memory(esperado,32);anim::DispararEvento(nullptr,x,43,{0,3,101});Exigir(Seed()==esperado,"voz no continua despues de AP6");
    anim::QuitarContextoAp(x);Exigir(anim::VelocidadAP(x,35,1,1)==14,"contexto AP no liberado");
    std::cout<<"PASS consumidores integrados: AP9 andar/scatto/NPC y reset; AP0 escena0; voces/AP6 RNG comun; secuencia ausente; AP5 813/1+6 vertices "<<dl._VtxCurrentIdx<<" indices "<<dl.indices<<" semillaAP6 "<<despues6<<'\n';
    return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

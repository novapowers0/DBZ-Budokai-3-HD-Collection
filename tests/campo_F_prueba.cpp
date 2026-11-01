// Solo prueba offline: funciones REALES extraidas, RAM guest simulada.
#include "../src/iw_minijuegos.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace dbz3 {bool g_guest_eu=false;}
struct PPCContext {struct {uint32_t u32=0;}r3;uint64_t lr=0;};
std::map<uint32_t,uint32_t> ram;
uint32_t Leer(uint32_t ad){return ram[ad];}
void Escribir(uint32_t ad,uint32_t valor){ram[ad]=valor;}
#define REX_LOAD_U32(ad) Leer(ad)
#define REX_STORE_U32(ad,v) Escribir(ad,v)
#define REXLOG_INFO(...) ((void)0)
#define DBZ3_HOOK(us,eu) void Gancho(PPCContext& ctx,uint8_t* base)
constexpr uint32_t kDireccion=0x8234D3D0u,kSombra=0x60000000u;
constexpr uint32_t kVieja[4]={0x3F147AE1,0xBF147AE1,0xBF147AE1,0};
constexpr uint32_t kAbajo[4]={0,0xBF800000,0,0x3F800000};
std::array<uint32_t,4> observado{};uint32_t alfa_observado=0;int llamadas=0;
void Exigir(bool ok,const char* mensaje){if(!ok)throw std::runtime_error(mensaje);}
void orig(PPCContext& ctx,uint8_t*) {
  ++llamadas;for(size_t i=0;i<4;++i)observado[i]=Leer(kDireccion+uint32_t(i)*4);
  alfa_observado=Leer(ctx.r3.u32+0xec);
  Escribir(kSombra+0xf4,0x12345678); // un efecto legitimo del consumidor NO se restaura
}
namespace dbz3::iwmini {
std::atomic<int> g_estado{2};std::atomic<bool> g_modulo{false};bool activo=true;
const int* Actual(){static int valor=0;return activo?&valor:nullptr;}
constexpr uint32_t kMgrPtr=0x110000,kFov=0x1c4;
constexpr float kGrados=3.14159265f/180.0f;
std::atomic<float> g_fov_campo{0};
float LoadF(uint8_t*,uint32_t ad){auto v=Leer(ad);float f;std::memcpy(&f,&v,4);return f;}
void StoreF(uint8_t*,uint32_t ad,float f){uint32_t v;std::memcpy(&v,&f,4);Escribir(ad,v);}
struct V3 {float x,y,z;};
struct ClipCamIa {int nf=5;std::vector<float> ojo,obj,fov;};
struct Datos {std::map<int,ClipCamIa> camaras;};
void MuestraPistaCam(const std::vector<float>& p,float,float* destino) {
  for(size_t i=0;i<p.size();++i)destino[i]=p[i];
}
}
#include CAMPO_F_CORE

int main() {
  try {
    using namespace dbz3::iwmini;
    std::cout<<std::setprecision(12);
    uint8_t* base=nullptr;const uint32_t mgr=0x120000;Escribir(kMgrPtr,mgr);
    StoreF(base,mgr+kFov,0.7853981852531433f);const auto anterior=Leer(mgr+kFov);
    for(int grados=5;grados<=85;++grados) {
      FovIW(base,float(grados));
      const auto f=LoadF(base,mgr+kFov);Exigir(f==g_fov_campo.load(),"mgr y camara activa discrepan");
      std::cout<<"FOV "<<grados<<' '<<f<<'\n';
    }
    FovIW(base,0);Exigir(Leer(mgr+kFov)==anterior,"FOV nativo no restaurado");
    Datos d;d.camaras[7]={5,{1,2,3},{4,5,6},{40*kGrados}};
    float ojo[3],obj[3];
    Exigir(CamaraIw(base,mgr,d,7,4,true,{10,20,30},ojo,obj),"clip no consumido");
    Exigir(ojo[0]==11 && ojo[1]==22 && ojo[2]==33 && obj[0]==14 && obj[1]==25 && obj[2]==36,"se modifico ojo/objetivo");
    std::cout<<"CLIP "<<g_fov_campo.load()<<'\n';
    const auto clip=Leer(mgr+kFov);
    Exigir(!CamaraIw(base,mgr,d,99,4,false,{0,0,0},ojo,obj) && Leer(mgr+kFov)==clip,"clip ausente altera FOV");
    _putenv_s("DBZ3_TEST_IWMINI","");_putenv_s("DBZ3_IW_SOMBRA_COMBATE","");
    int casos=0;
    for(bool eu:{false,true})for(int variante=0;variante<6;++variante) {
      dbz3::g_guest_eu=eu;activo=true;g_estado=2;g_modulo=false;
      PPCContext ctx;ctx.r3.u32=kSombra;ctx.lr=eu?0x82101724:0x82101624;
      if(variante==1)activo=false;
      if(variante==2)g_estado=0;
      if(variante==3)g_modulo=true; // vuelo y modulos ajenos: fuera del parche
      if(variante==4)ctx.lr=0x12345678;
      if(variante==5)_putenv_s("DBZ3_IW_SOMBRA_COMBATE","1");
      for(uint32_t i=0;i<4;++i)Escribir(kDireccion+4*i,kVieja[i]);
      StoreF(base,kSombra+0xec,.498488f);const auto alfa_antes=Leer(kSombra+0xec);llamadas=0;
      Gancho(ctx,base);Exigir(llamadas==1,"consumidor nativo no ejecutado exactamente una vez");
      if(variante==0) {
        uint32_t opacidad;float a=.3f;std::memcpy(&opacidad,&a,4);
        Exigir(alfa_observado==opacidad,"opacidad original incorrecta");
#ifndef CAMPO_F_ANTES
        Exigir(std::equal(observado.begin(),observado.end(),kAbajo),"direccion de sombra no vertical en el consumidor");
#endif
        std::cout<<"SOMBRA "<<eu;
        for(auto v:observado)std::cout<<' '<<v;std::cout<<'\n';
      } else {
        Exigir(alfa_observado==alfa_antes && std::equal(observado.begin(),observado.end(),kVieja),"parche escapa de campo/contexto");
      }
      Exigir(Leer(kSombra+0xec)==alfa_antes,"opacidad no restaurada");
      for(uint32_t i=0;i<4;++i)Exigir(Leer(kDireccion+4*i)==kVieja[i],"direccion no restaurada");
      Exigir(Leer(kSombra+0xf4)==0x12345678,"se deshizo un efecto del consumidor");
      _putenv_s("DBZ3_IW_SOMBRA_COMBATE","");++casos;
    }
    std::cout<<"PASS 81 FOV, clips sin mover ojo/objetivo, restauracion y "<<casos<<" contextos US/EU de sombra\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

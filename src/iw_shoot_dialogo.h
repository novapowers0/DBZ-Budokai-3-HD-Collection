// SHOOT: mensajes19/1A de Kaio. EU3437F0/3437A0 -> s_MESSAGE342F90
// -> WaitAnimEnd346520 -> ShowAndLoadNext3462B0 -> PopFinishedQueue.
#pragma once
#include <rex/logging.h>
#include "anim_iw.h"
#include "ps2_modelo.h"
#include "iw_ap_eventos.h"
#include "iw_adx.h"
#include "launcher/settings.h"
#include "ingame/game_style.h"

namespace dbz3::iwdisparo::mensaje {
namespace P=dbz3::pasarela;
namespace A=dbz3::anim::ama;
struct Datos {
  A::Obra obra;
  std::vector<P::Imagen> atlas;
  P::Imagen textos[2];
  int voces[2]={-1,-1};
  float dur_voz[2]={0,0};
  iwadx::Pcm pcm[2];
  float centro[2]={195,312},marco[2]={0,0};
  bool japones=false;
  bool Valido() const {return obra.Valida() && obra.Objeto(3)>=0 && obra.Objeto(4)>=0 && !atlas.empty();}
  float Duracion(int id) const {
    const int n=obra.Objeto(uint32_t(id));
    return n<0?0:A::MaxCuadros(A::CrearArbol(obra,n));
  }
};
inline Datos Cargar(P::AfsPs2& cmn,P::AfsPs2& usa,bool japones) {
  Datos d;d.japones=japones;
  const auto paquete=cmn.Entrada(936),b=P::HijoAmb(paquete,0);
  d.obra.Cargar(b);
  const auto atlas=P::HijoAmb(paquete,1),texto=P::HijoAmb(usa.Entrada(5),11);
  for(int i=0,n=P::CuantasAmt(atlas);i<n;++i)d.atlas.push_back(P::TexturaAmt(atlas,i));
  for(int i=0;i<2;++i)d.textos[i]=P::TexturaAmt(texto,0x19+i);
  if(const int n=d.obra.Objeto(0);n>=0) {
    const int seg=d.obra.Nodo(n).anim;
    if(seg>=0) {int clave=-1;float pos[4]={};A::MuestrearPosicion(d.obra.Seg(seg).a.c2,0,clave,pos);d.marco[0]=pos[0];d.marco[1]=pos[1];}
  }
  auto u=[&](size_t o) {uint32_t v=0;if(o+4<=b.size())std::memcpy(&v,b.data()+o,4);return v;};
  const size_t tabla=u(0x1c);
  if(tabla+35*4<=b.size()) {
    const size_t seg=u(tabla+34*4),pista=u(seg+4),claves=u(pista+0x1c);
    if(claves+8<=b.size())std::memcpy(d.centro,b.data()+claves,8);
  }
  const auto fuente=P::BuscarIW();
  const auto elf=P::LeerElf(fuente);
  P::AfsPs2 adx;
  const bool hay=adx.Abrir(fuente,japones?"ADX_JPN.AFS":"ADX_USA.AFS");
  for(int i=0;i<2;++i) {
    d.voces[i]=anim::apev::EntradaAdxPerfil(elf,0x13,8+i);
    if(!hay || d.voces[i]<0)continue;
    const auto b=adx.Entrada(size_t(d.voces[i]));
    if(b.size()<16)continue;
    auto be=[&](size_t o) {return uint32_t(b[o])<<24|uint32_t(b[o+1])<<16|uint32_t(b[o+2])<<8|b[o+3];};
    if(const auto hz=be(8);hz)d.dur_voz[i]=float(be(12))*60.0f/float(hz);
    d.pcm[i]=iwadx::Leer(b);
  }
  return d;
}
inline void DibujarObra(ImGuiIO& io,const A::Obra& obra,const std::vector<ps2::Tex>& atlas,int id,float t,float dx=0,float dy=0) {
  const int n=obra.Objeto(uint32_t(id));if(n<0)return;
  auto arbol=A::CrearArbol(obra,n,t);A::MarcarArbol(arbol,t);
  A::Contexto c;c.pos[0]=dx;c.pos[1]=dy;std::vector<A::Entrada> es;
  A::ActualizarNodo(arbol,arbol.raiz,c,es);
  const auto area=ingame::style::GameArea(io);
  auto at=[&](float x,float y){return area.At(x*2,y*2);};
  auto* dl=ImGui::GetForegroundDrawList();
  for(const auto& e:es) {
    if(e.SinTextura() || e.textura>=atlas.size())continue;
    const auto& tx=atlas[e.textura];if(!tx)continue;
    A::Quad q;A::DibujarEntrada(e,q);
    auto color=[&](int i){return std::clamp(int(e.tinte[i]*255+e.rgba[i]+.5f),0,255);};
    dl->AddImageQuad(tx.id(),at(q.xy[0][0],q.xy[0][1]),at(q.xy[1][0],q.xy[1][1]),at(q.xy[2][0],q.xy[2][1]),at(q.xy[3][0],q.xy[3][1]),
                    ImVec2(q.uv[0][0]/tx.w,q.uv[0][1]/tx.h),ImVec2(q.uv[1][0]/tx.w,q.uv[1][1]/tx.h),
                    ImVec2(q.uv[2][0]/tx.w,q.uv[2][1]/tx.h),ImVec2(q.uv[3][0]/tx.w,q.uv[3][1]/tx.h),
                    IM_COL32(color(0),color(1),color(2),color(3)));
  }
}
class Reproductor {
 public:
  void Reiniciar(){if(canal_)++canal_->generacion;iwsonido::Salida().Mezcla().OlvidarFin(voz_);voz_=0;reanudada_=false;id_=-1;tiempo_=0;salida_=-1;}
  bool Activo() const{return id_>=0;}
  int Id() const{return id_;}
  float Tiempo() const{return tiempo_;}
  int64_t PosicionAudio() const {uint64_t p=0;return voz_&&iwsonido::Salida().Mezcla().Posicion(voz_,p)?int64_t(p):-1;}
  bool VozReanudada() const{return reanudada_;}
  void Iniciar(const Datos& d,int id) {
    Reiniciar();id_=id;
    if(id>=0 && id<2 && !d.pcm[id].muestras.empty()) {
      // s_MESSAGE pausa el canal2 durante la entrada;346520 lo reanuda.
      // El silencio del jugador no debe adelantar la terminacion del audio.
      canal_=std::make_shared<iwsonido::Canal>();canal_->pausa=true;
      auto& mezcla=iwsonido::Salida().Mezcla();
      mezcla.Maestro(float(settings::MasterVolume()),settings::AudioMute());
      iwsonido::Opciones o;o.canal=canal_;o.sustituir=false;o.notificar_fin=true;
      voz_=iwadx::Oir(d.pcm[id],std::move(o));
      if(std::getenv("DBZ3_TEST_IWMINI"))REXLOG_INFO("dbz3 SHOOT voz: {} japones={} hz={} muestras={} entrada={} salida={}",
          d.voces[id],d.japones,d.pcm[id].hz,d.pcm[id].muestras.size()/2,d.Duracion(3),d.Duracion(4));
    }
  }
  void Paso(const Datos& d,float dt=1) {
    if(!Activo())return;
    tiempo_+=dt;
    const float entrada=d.Duracion(3);
    if(!reanudada_ && tiempo_>=entrada) {
      reanudada_=true;inicio_voz_=tiempo_;if(canal_)canal_->pausa=false;return;
    }
    const bool fin_audio=iwsonido::Salida().Terminado(voz_);
    if(salida_<0 && reanudada_ && (fin_audio || tiempo_-inicio_voz_>=600))salida_=tiempo_;
    if(salida_>=0 && tiempo_-salida_>=d.Duracion(4))id_=-1;
  }
  void Dibujar(ImGuiIO& io,const Datos& d) {
    if(!Activo() || !d.Valido())return;
    if(atlas_.empty()) {
      for(const auto& im:d.atlas)atlas_.push_back(ps2::Crear(im));
      for(int i=0;i<2;++i)textos_[i]=ps2::Crear(d.textos[i]);
    }
    const auto area=ingame::style::GameArea(io);
    auto at=[&](float x,float y){return area.At(x*2,y*2);};
    auto* dl=ImGui::GetForegroundDrawList();
    DibujarObra(io,d.obra,atlas_,0,tiempo_); // marco absoluto, sin sumar otra vez175
    DibujarObra(io,d.obra,atlas_,salida_<0?3:4,salida_<0?tiempo_:tiempo_-salida_);
    const auto& tx=textos_[id_];
    if(tx && tiempo_>=d.Duracion(3) && salida_<0)
      dl->AddImage(tx.id(),at(d.centro[0]-tx.w*.5f,d.centro[1]-tx.h*.5f),at(d.centro[0]+tx.w*.5f,d.centro[1]+tx.h*.5f));
  }
 private:
  int id_=-1;
  float tiempo_=0,salida_=-1;
  float inicio_voz_=0;
  bool reanudada_=false;
  iwsonido::Handle voz_=0;
  std::shared_ptr<iwsonido::Canal> canal_;
  std::vector<ps2::Tex> atlas_;
  ps2::Tex textos_[2];
};
} // namespace dbz3::iwdisparo::mensaje

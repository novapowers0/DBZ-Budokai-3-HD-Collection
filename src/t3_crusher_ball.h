// solCB: consumidor de datos ORIGINALES T3. No usa bolas/texturas de un donante.
// Experimental y apagado: requiere mod activo + crusher_ball.activo, o entorno de pruebas.
#pragma once
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>
#include "ps2_modelo.h"
#include "cinematica.h"
#include "iw_minijuegos.h"
#if defined(DBZ3_DUAL_REGION)
REX_EXTERN(dbz3eu_sub_821150B8);
REX_EXTERN(dbz3eu_sub_82103A78);
#endif

namespace dbz3::t3cb {
using V=ps2::V3;
inline V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V mul(V a,float k){return {a.x*k,a.y*k,a.z*k};}
inline float len(V a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
struct Blob {
 std::vector<uint8_t> b;
 uint32_t U(size_t p)const{uint32_t v=0;if(p+4<=b.size())std::memcpy(&v,b.data()+p,4);return v;}
 int H(size_t p)const{int16_t v=0;if(p+2<=b.size())std::memcpy(&v,b.data()+p,2);return v;}
 float F(size_t p)const{float v=0;if(p+4<=b.size())std::memcpy(&v,b.data()+p,4);return std::isfinite(v)?v:0;}
 int B(size_t p)const{return p<b.size()?b[p]:0;}
};
struct Part {int kind=0;Blob def,a,b;std::vector<int> tex;};
struct Data {std::vector<Part> parts;std::vector<pasarela::Imagen> images;std::filesystem::path path;bool valid=false;int ray_tex=-1;};
inline Data g_data;
inline std::mutex g_mutex;
struct State {bool active=false;uint32_t x=0,rival=0,frame=0,start=0;int phase=-1,hits=0;V hand,ball,target,origin;float hp0=0;};
inline State g_state,g_draw;
inline uint8_t* g_base=nullptr;
inline uint32_t g_clock=0;
inline std::vector<ps2::Tex> g_textures;
inline bool Test(){static bool t=std::getenv("DBZ3_TEST_T3_CRUSHER")!=nullptr;return t;}
inline bool Frozen(){static unsigned f=std::getenv("DBZ3_TEST_T3CB_FREEZE")?unsigned(std::atoi(std::getenv("DBZ3_TEST_T3CB_FREEZE"))):0;return Test()&&f&&g_state.active&&g_state.frame>=f;}
inline float RF(uint8_t* base,uint32_t p){uint32_t v=REX_LOAD_U32(p);float f;std::memcpy(&f,&v,4);return f;}
inline void WF(uint8_t* base,uint32_t p,float f){uint32_t v;std::memcpy(&v,&f,4);REX_STORE_U32(p,v);}
inline V Point(uint8_t* base,uint32_t x,int n){return {RF(base,x+1616+16*n),RF(base,x+1620+16*n),RF(base,x+1624+16*n)};}
inline bool Load(){
 if(g_data.valid)return true;
 static bool sin_banco=false;if(sin_banco)return false;   // sin mod activo no se vuelve a mirar el disco
 std::error_code ec;
 for(const auto& e:std::filesystem::directory_iterator("mods",ec)){
  if(!e.is_directory()||std::filesystem::exists(e.path()/".disabled"))continue;
  auto path=e.path()/"crusher_ball.tcb2";
  if(!std::filesystem::exists(path)||(!Test()&&!std::filesystem::exists(e.path()/"crusher_ball.activo")))continue;
  std::ifstream f(path,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
  if(bytes.size()<20||bytes.size()>16*1024*1024||std::memcmp(bytes.data(),"TCB2",4))continue;
  size_t at=4;bool ok=true;
  auto u=[&](){uint32_t v=0;if(at+4>bytes.size()){ok=false;return v;}std::memcpy(&v,bytes.data()+at,4);at+=4;return v;};
  auto blob=[&](){Blob b;size_t n=u();if(n>bytes.size()-std::min(at,bytes.size())){ok=false;return b;}b.b.assign(bytes.begin()+at,bytes.begin()+at+n);at+=n;return b;};
  uint32_t ng=u(),np=u(),nt=u(),mask=u();if(ng!=5||np!=13||nt>64||mask!=0x14221)continue;
  Data data;data.path=path;
  for(uint32_t i=0;i<ng&&ok;++i){
   int kind=int(u());uint32_t nr=u(),nparts=u();if(nr>32||nparts>40){ok=false;break;}
   std::vector<std::vector<int>> resources;
   for(uint32_t j=0;j<nr;++j){uint32_t n=u();if(n>32){ok=false;break;}std::vector<int> ids;for(uint32_t k=0;k<n;++k)ids.push_back(int(u()));resources.push_back(ids);}
   for(uint32_t j=0;j<nparts&&ok;++j){Part p;p.kind=kind;p.def=blob();uint32_t n=u();if(n>2||p.def.b.size()!=64){ok=false;break;}if(n)p.a=blob();if(n>1)p.b=blob();
    int ti=p.def.B(1);if(kind&&ti<int(resources.size()))p.tex=resources[ti];data.parts.push_back(std::move(p));}
  }
  for(uint32_t i=0;i<nt&&ok;++i){pasarela::Imagen im;im.w=int(u());im.h=int(u());uint32_t entry=u();u();if(entry==0xffffffffu&&data.ray_tex<0)data.ray_tex=int(i);Blob b=blob();if(im.w<=0||im.h<=0||im.w>1024||im.h>1024||b.b.size()!=size_t(im.w)*im.h*4){ok=false;break;}im.rgba=std::move(b.b);data.images.push_back(std::move(im));}
  if(ok&&at==bytes.size()&&data.parts.size()==13){data.valid=true;g_data=std::move(data);REXLOG_INFO("dbz3 t3cb: banco ORIGINAL {}: 5 familias, 13 partes, {} texturas; experimental",path.string(),nt);REXLOG_WARN("dbz3 t3cb: NO fiel/NO activar: semillas, emisores5/9, billboards16, oclusion, camara y reacciones requieren contraste");return true;}
 }
 sin_banco=true;
 return false;
}
inline void StartAnim(PPCContext& ctx,uint8_t* base,uint32_t x,uint32_t code){
 REX_STORE_U16(x+3246,REX_LOAD_U16(x+3244));REX_STORE_U16(x+3244,15);REX_STORE_U16(x+3248,0);
 PPCContext c=ctx;c.r3.u64=x;c.r4.u64=0;DBZ3_CALL2(sub_82103978,dbz3eu_sub_82103A78)(c,base);
 REX_STORE_U32(x+2928,code);c=ctx;c.r3.u64=x;DBZ3_CALL2(sub_82114FB8,dbz3eu_sub_821150B8)(c,base);
}
inline void Tick(PPCContext& ctx,uint8_t* base){
 if(dbz3::net::Session::Get().active())return;   // apagado en online: el estado no va en las sumas
 g_base=base;if(Frozen())return;++g_clock;
 const uint32_t mgr=REX_LOAD_U32(dbz3::GA(0x823D6CF4));if(!mgr){g_state.active=false;std::lock_guard<std::mutex> lock(g_mutex);g_draw=g_state;return;}
 const uint32_t battle=REX_LOAD_U32(mgr+35128);
 if(battle!=4){g_state.active=false;std::lock_guard<std::mutex> lock(g_mutex);g_draw=g_state;return;}
 const uint32_t x=mgr+2096,other=x+8240,code=REX_LOAD_U32(x+2928);
 if(!g_state.active&&(code==0x600||code==0x700)&&Load()){
  g_state={};g_state.active=true;g_state.x=x;g_state.rival=other;g_state.start=g_clock;g_state.hp0=RF(base,other+6080);
  g_state.hand=Point(base,x,11);g_state.origin=g_state.hand;
  // Este CSK no ejecuta AP de energia nativa: la prueba confirmó que el CCM
  // comprueba el coste pero no lo cobra. T3 60000/20000 = 3 barras B3.
  WF(base,x+6100,std::max(0.0f,RF(base,x+6100)-3000.0f));
  REXLOG_INFO("dbz3 t3cb: empieza cuadro {} ki {:.0f} HP rival {:.0f}; daño en unidades B3 (T3/10)",g_clock,RF(base,x+6100),g_state.hp0);
 }
 if(!g_state.active)return;
 auto& s=g_state;s.frame=g_clock-s.start;s.hand=Point(base,x,11);s.target=Point(base,other,1);
 int phase=s.frame<12?0:s.frame<60?1:s.frame<72?2:s.frame<103?3:4;
 if(phase!=s.phase){s.phase=phase;StartAnim(ctx,base,x,0x600+uint32_t(phase));REXLOG_INFO("dbz3 t3cb: fase {} cuadro {} codigo {:X}",phase,s.frame,0x600+phase);}
 else if(phase<4&&REX_LOAD_U16(x+3244)!=15&&s.frame<118)StartAnim(ctx,base,x,0x600+uint32_t(phase));
 if(s.frame<71){s.ball=s.hand;s.origin=s.hand;}
 else {V d=add(s.target,mul(s.origin,-1));float distance=len(d);float advance=(s.frame-71)/60.0f*(400.0f*10000.0f/3600.0f);s.ball=add(s.origin,mul(d,distance>0?std::min(1.0f,advance/distance):0));}
 // La referencia PAL tiene 4 contactos a 1,20/1,32/1,44/1,56 s. La unidad de HP de B3 es 1/10.
 // No se golpea si el proyectil no esta en contacto con el objetivo.
 constexpr unsigned hitFrames[]={72,79,86,94};
 if(s.hits<4&&s.frame>=hitFrames[s.hits]&&len(add(s.ball,mul(s.target,-1)))<3.0f){
  const float hp=RF(base,other+6080);const float raw=std::ceil((std::ceil((7270.0f/4)/10)*10)*1.1f/10)*10;
  const float damage=raw/10;WF(base,other+6080,std::max(0.0f,hp-damage));++s.hits;
  REXLOG_INFO("dbz3 t3cb: impacto {} cuadro {} daño_B3 {:.0f} equivalente_T3 {:.0f} HP {:.0f}->{:.0f}",s.hits,s.frame,damage,raw,hp,std::max(0.0f,hp-damage));
 }
 if(s.frame>=118){s.active=false;REX_STORE_U16(x+3246,REX_LOAD_U16(x+3244));REX_STORE_U16(x+3244,0);REX_STORE_U32(x+2928,0);PPCContext c=ctx;c.r3.u64=x;c.r4.u64=0;DBZ3_CALL2(sub_82103978,dbz3eu_sub_82103A78)(c,base);REXLOG_INFO("dbz3 t3cb: fin cuadros {} segundos {:.4f} impactos {} daño_B3 {:.0f} equivalente_T3 {:.0f}; fidelidad visual pendiente",s.frame,s.frame/60.0f,s.hits,s.hp0-RF(base,other+6080),(s.hp0-RF(base,other+6080))*10);}
 std::lock_guard<std::mutex> lock(g_mutex);g_draw=s;
}
inline float key3(const Blob& b,size_t at,float t,float total,float split){
 float a=b.F(at),m=b.F(at+4),z=b.F(at+8);if(total<=0)return z;
 float q=std::clamp(t/total,0.0f,1.0f);split=std::clamp(split,.001f,.999f);
 return q<split?a+(m-a)*q/split:m+(z-m)*(q-split)/(1-split);
}
inline float random01(uint32_t n){n^=n<<13;n^=n>>17;n^=n<<5;return (n&65535)/65535.0f;}
inline std::array<float,4> color(const Blob& b,size_t at,float key){
 std::array<float,4> c{};int a=int(key);float f=key-a;for(int i=0;i<4;++i)c[i]=b.F(at+16*a+4*i)+(b.F(at+16*std::min(a+1,2)+4*i)-b.F(at+16*a+4*i))*f;return c;
}
inline void Quad(ImDrawList* dl,const ps2::Camara& cam,int tex,V pos,float sx,float sy,float angle,const std::array<float,4>& col,
                 ImVec2 uv0={0,0},ImVec2 uv1={1,1},bool additive=true){
 if(tex<0||tex>=int(g_textures.size())||!g_textures[tex]||sx<=0||sy<=0)return;
 ImVec2 p;float k;if(!cam.P(pos,p,k))return;float x=sx*k*.5f,y=sy*k*.5f;
 if(!std::isfinite(x)||!std::isfinite(y)||x>20000||y>20000)return;
 float ca=std::cos(angle),sa=std::sin(angle);auto pt=[&](float a,float b){return ImVec2(p.x+ca*a-sa*b,p.y+sa*a+ca*b);};
 if(!additive){
  dl->AddCallback(rex::ui::kImDrawBlendAlpha,nullptr);
  ImU32 c=IM_COL32(int(std::clamp(col[0]/128,0.0f,1.0f)*255),int(std::clamp(col[1]/128,0.0f,1.0f)*255),int(std::clamp(col[2]/128,0.0f,1.0f)*255),int(std::clamp(col[3]/128,0.0f,1.0f)*255));
  dl->AddImageQuad(g_textures[tex].id(),pt(-x,-y),pt(x,-y),pt(x,y),pt(-x,y),uv0,ImVec2(uv1.x,uv0.y),uv1,ImVec2(uv0.x,uv1.y),c);
  dl->AddCallback(rex::ui::kImDrawBlendAdditive,nullptr);
  return;
 }
 // GS: RGB y alfa 128 son neutros. Descomponer el producto permite sobrebrillo aditivo sin recortarlo.
 for(int pass=0;pass<(additive?4:1);++pass){
  float a=std::clamp(col[3]/128.0f,0.0f,2.0f);int rgb[3];for(int j=0;j<3;++j)rgb[j]=int(std::clamp(col[j]/128*a-pass,0.0f,1.0f)*255);
  if(!(rgb[0]|rgb[1]|rgb[2]))continue;
  ImU32 c=IM_COL32(rgb[0],rgb[1],rgb[2],additive?255:int(std::min(a,1.0f)*255));
  dl->AddImageQuad(g_textures[tex].id(),pt(-x,-y),pt(x,-y),pt(x,y),pt(-x,y),uv0,ImVec2(uv1.x,uv0.y),uv1,ImVec2(uv0.x,uv1.y),c);
 }
}
inline float phaseTime(int p){constexpr float times[]={8.0f/60,71.0f/60,104.0f/60,72.0f/60,72.0f/60,67.0f/60,118.0f/60};return times[std::clamp(p,0,6)];}
inline void Rays(ImDrawList* dl,const ps2::Camara& cam,const Part& p,V pos,float age,float scale){
 if(g_data.ray_tex<0)return;ImVec2 c;float k;if(!cam.P(pos,c,k))return;
 const int count=std::clamp(p.def.B(3),1,48);const float inner=p.def.F(16)*scale,jitter=p.def.F(20)*scale;
 // EftRay mode1: angulos uniformes +0..4 grados; giro y alfa se sortean en cada paso de 30 Hz.
 const uint32_t tick=uint32_t(age*30);float turn=random01(tick+7919)*6.28318530718f;
 for(int i=0;i<count;++i){uint32_t seed=uint32_t(i)*1031+17;
  float angle=6.28318530718f*i/count+float(int(random01(seed)*5))*.01745329252f+turn;
  float length=100*scale+random01(seed+1)*20,width=std::max(.4f,scale*random01(seed+2)*5);
  float off=inner+random01(seed+3)*jitter;float a=float(int(random01(seed+tick*97)*112));
  float radius=(off+length*.5f)*k;ImVec2 center(c.x+std::sin(angle)*radius,c.y+std::cos(angle)*radius);
  // Quad usa la misma proyeccion; desplazar en los ejes de la vista conserva el origen 3D.
  V right{cam.m[0],cam.m[4],cam.m[8]},up{cam.m[1],cam.m[5],cam.m[9]};
  V q=add(pos,add(mul(right,std::sin(angle)*(off+length*.5f)),mul(up,-std::cos(angle)*(off+length*.5f))));
  Quad(dl,cam,g_data.ray_tex,q,width*2,length,angle,{192,192,192,a});
 }
}
inline void V000(ImDrawList* dl,const ps2::Camara& cam,const Part& p,V pos,float age,float scale){
 const Blob& b=p.a;if(b.b.size()<32||b.U(0)!=0x30303056)return;size_t keys=b.U(8);
 for(int l=0;l<b.B(4);++l){size_t at=32+32*l;int count=b.B(at+4),texture=b.B(at);uint32_t flags=b.U(at+16);size_t first=keys;keys+=64*count;
  if(!count||keys>b.b.size()||texture>=int(p.tex.size()))continue;
  float f=age*60*b.B(at+3);int end=b.H(at+8);
  int ls=b.B(at+6),le=b.B(at+5),loops=int(int8_t(b.B(at+7)));
  if((flags&1)&&ls<count&&le<count&&le>ls){float a=b.F(0);(void)a;int from=int(b.U(first+64*ls+52)),to=int(b.U(first+64*le+52));if(f>to&&(loops<0||f<to+(to-from)*loops))f=from+std::fmod(f-from,float(std::max(1,to-from)));}
  if(f>end&&!(b.B(5))){continue;}if(b.B(5)&&end>0)f=std::fmod(f,float(end));
  int a=0;while(a+1<count&&float(b.U(first+64*(a+1)+52))<=f)++a;int c=std::min(a+1,count-1);
  float fa=float(b.U(first+64*a+52)),fc=float(b.U(first+64*c+52)),t=fc>fa?std::clamp((f-fa)/(fc-fa),0.0f,1.0f):0;
  auto val=[&](int o){float x=b.F(first+64*a+o);return x+(b.F(first+64*c+o)-x)*t;};
  std::array<float,4> col{};for(int j=0;j<4;++j)col[j]=b.B(first+64*a+48+j)+(b.B(first+64*c+48+j)-b.B(first+64*a+48+j))*t;
  float angle=(flags&64)?b.F(first+16+8)*age*30:val(24);angle*=.01745329252f;
  V q=add(pos,{ -val(0)*scale,-val(4)*scale,val(8)*scale});
  if(flags&0x100){float jitter=(random01(uint32_t(age*60)+l*31)-.5f);q.x+=b.H(at+28)/1000.0f*jitter;q.y+=b.H(at+30)/1000.0f*jitter;}
  ImVec2 uv0(0,0),uv1(1,1);if(flags&16){int grid=b.B(at+12),nx=grid==0?2:grid==3?3:4,ny=grid==1?2:nx;int frame=int(f/std::max(1,b.B(at+15)))%(nx*ny);uv0=ImVec2(float(frame%nx)/nx,float(frame/nx)/ny);uv1=ImVec2(uv0.x+1.0f/nx,uv0.y+1.0f/ny);}
  Quad(dl,cam,p.tex[texture],q,std::abs(val(32))*scale,std::abs(val(36))*scale,angle,col,uv0,uv1,b.B(at+2)==1);
 }
}
inline void Draw(ImGuiIO& io){
 State s;{std::lock_guard<std::mutex> lock(g_mutex);s=g_draw;}if(!s.active||!g_data.valid||!g_base)return;
 if(g_textures.empty())for(const auto& im:g_data.images)g_textures.push_back(ps2::Crear(im));
 ps2::Camara cam=ps2::CamaraCombate(g_base,io.DisplaySize);if(!cam.ok)return;
 ImDrawList* dl=ImGui::GetBackgroundDrawList();dl->AddCallback(rex::ui::kImDrawBlendAdditive,nullptr);
 float time=s.frame/60.0f;unsigned part_index=0;
 for(const Part& p:g_data.parts){
  ++part_index;float begin=phaseTime(p.def.B(8)),end=phaseTime(p.def.B(9));
  if(end<=begin)end=118.0f/60;float age=time-begin;if(age<0||time>=end)continue;
  V pos=p.def.B(10)==6?s.target:s.ball;float scale=p.def.F(28);if(scale==0)scale=1;
  if(p.def.F(40)>0)scale=key3(p.def,28,age,p.def.F(40),p.def.F(44));
  if(p.kind==0){
   Rays(dl,cam,p,pos,age,scale);continue;
  }
  if(p.kind==14){V000(dl,cam,p,pos,age,scale);continue;}
  if(p.tex.empty())continue;
  if(p.kind==16){
   float key=(p.a.U(0)&1)&&p.a.F(20)>0?std::clamp(age/p.a.F(20),0.0f,1.0f)*2:2;
   auto c=color(p.b,0,key);float w=key3(p.a,0x34,age,p.a.F(20),.5f),h=key3(p.a,0x40,age,p.a.F(20),.5f);
   float fade=1;if(p.a.F(12)>0)fade=std::min(1.0f,age/p.a.F(12));c[3]*=fade;
   Quad(dl,cam,p.tex[std::min(size_t(p.def.B(2)),p.tex.size()-1)],pos,std::abs(w)*scale,std::abs(h)*scale,p.a.F(0x88)*age,c);continue;
  }
  if(p.kind==5||p.kind==9){
   const Blob& a=p.a;float anim=p.kind==5?a.F(0x14):a.F(0x210),split=p.kind==5?a.F(0x18):a.F(0x214);
   float key=anim>0?std::clamp(age/anim,0.0f,1.0f)*2:2;int ki=std::min(2,int(key));
   float life=p.kind==5?key3(a,0x34,age,anim,split):key3(a,0x1ec,age,anim,split);if(life<=0)continue;
   float interval=p.kind==5?key3(a,0x28,age,anim,split):a.B(0x238+ki)/30.0f;interval=std::max(interval,1.0f/30);
   int count=p.kind==5?a.H(0x1c+2*ki):a.B(0x23b+ki);count=std::clamp(count,0,64);
   int last=int(age/interval),first=std::max(0,int((age-life)/interval));
   for(int birth=first;birth<=last;++birth)for(int j=0;j<count;++j){
    float pa=age-birth*interval;if(pa<0||pa>life)continue;float t=pa/life;uint32_t seed=part_index*65537+birth*193+j*41+1;
    float rx=random01(seed),ry=random01(seed+7),rz=random01(seed+13);
    V q=pos;float w=0,h=0;std::array<float,4> col=color(p.b,0,key),ce=color(p.b,0x60,key);
    for(int c=0;c<4;++c)col[c]+=(ce[c]-col[c])*t;
    if(p.kind==5){
     float speed=a.F(0x58+ki*12);float radius=key3(a,0x190,age,anim,split);
     float direction=rx*6.28318530718f,vertical=ry*2-1;
     q=add(q,{std::cos(direction)*(radius+speed*pa),vertical*(radius+speed*pa),std::sin(direction)*(radius+speed*pa)});
     float mid=std::clamp(a.F(0x4c+4*ki),.001f,.999f);float v=t<mid?t/mid:(t-mid)/(1-mid);int stage=t<mid?0:1;
     w=a.F(0xa0+ki*12+stage*4)+(a.F(0xa0+ki*12+(stage+1)*4)-a.F(0xa0+ki*12+stage*4))*v;
     h=a.F(0xe8+ki*12+stage*4)+(a.F(0xe8+ki*12+(stage+1)*4)-a.F(0xe8+ki*12+stage*4))*v;
     // EftPtcl_Update: el x16 es SOLO para shape1/2 (coordenadas de sprite).
     // Shape0 usa quads en el mundo; aplicarlo aqui inflaba el efecto x16.
     if(a.B(5)!=0&&!(a.U(0)&2)){w*=16;h*=16;}
    }else{
     q=add(q,{key3(a,0x1a4,age,anim,split)+rx*key3(a,0x1b0,age,anim,split),
                -key3(a,0x1bc,age,anim,split)-ry*key3(a,0x1c8,age,anim,split),key3(a,0x1d4,age,anim,split)+rz*key3(a,0x1e0,age,anim,split)});
     float mid=std::clamp(a.F(0x220),.001f,.999f);int st=t<mid?0:1;float v=t<mid?t/mid:(t-mid)/(1-mid);
     w=a.F(ki*12+st*4)+(a.F(ki*12+(st+1)*4)-a.F(ki*12+st*4))*v;
     h=a.F(0x48+ki*12+st*4)+(a.F(0x48+ki*12+(st+1)*4)-a.F(0x48+ki*12+st*4))*v;
    }
    float angle=p.kind==5?a.F(0x238+ki*4)*3.14159265f+a.F(0x250+ki*4)*pa*30*3.14159265f:rx*6.28318530718f;
    Quad(dl,cam,p.tex[std::min(size_t(p.def.B(2)),p.tex.size()-1)],q,std::abs(w)*scale,std::abs(h)*scale,angle,col);
   }
  }
 }
 dl->AddCallback(rex::ui::kImDrawBlendAlpha,nullptr);
}
} // namespace dbz3::t3cb

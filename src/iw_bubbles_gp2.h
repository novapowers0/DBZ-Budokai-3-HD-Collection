// GP2 Bubbles IW EU: maquina propia del modelo BBR, sin escribir cuerpo B3.
// Evidencia ELF afe8c7b9...: D:/tmp/solGP2/bubbles/ELF_RECUPERADO.md.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <cstdio>
#include "iw_bubbles_root.h"
#include "iw_bubbles_wcf.h"

namespace dbz3::iwbubbles {
inline constexpr bool kBubblesProbado = false; // Candidato completo; oraculo C++/runtime/fin9 pendientes.
inline bool Prueba(int clave) {
  const char* m = std::getenv("DBZ3_TEST_IWMINI");
  const char* b = std::getenv("DBZ3_TEST_GP2_BUBBLES");
  return clave == 0x7D1 && (kBubblesProbado ||
      (m && std::strtoul(m,nullptr,0)==0x7D1 && b && b[0]=='1' && b[1]==0));
}
inline constexpr std::array<unsigned,11> kCodigos = {0xD2,0xD3,0xD4,0xDD,0xDF,0xE0,0xE1,0xD5,0xD6,0xE2,0xD8};
using V = noche::V;
inline V Suma(V a,V b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline V Resta(V a,V b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline V Escala(V a,float k) { return {a.x*k,a.y*k,a.z*k}; }
inline float Largo(V a) { return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z); }
inline V Normal(V a) { const float l=Largo(a); return l>0?Escala(a,1/l):V{}; }
// World-space del modelo propio; el adaptador no confunde modo de tarea y estado.
struct Entrada {
  V jugador; //ancla de cintura publicada del jugador0, no pies ni pos del rival oculto.
  V camaraC0;
  int estado_jugador=1,dificultad=2;
  float paso=1.2f, factor_velocidad=.48f;
  bool bloqueo248=false, objetivo180=false,c0_valida=false;
};
struct Evento { int tipo=0; float desvio=0; V p; };
struct Cambio {
  int desde=0,hasta=0;unsigned codigo=0;float cuadro=0;bool f210=false,f214=false,f218=false;
  V p,raiz,raiz_antes;unsigned pose_codigo=0,flags_raiz=0;float pose_cuadro=0;std::uint64_t tarea=0;wcf::Contact contacto;
};
struct Tarea { int modo=0,fase=0;float tiempo=0,duracion=0;V vel,centro;bool activa=true;std::uint64_t id=0; };
enum Cond { Siempre, Timeout, Fin, FinLibre, Distinto, SaleMovimiento, TareaActiva,
            CercaFrente, Evento7, Evento8, Evento9 };
enum Acc { Seleccion, Entrar1, Entrar2, EntrarElegido, EntrarElegidoFin,
           EntrarDistinto, FrenarE0, SalirMovimiento, Evadir4, Interrumpir4,
           Volver0, Emitir7, Emitir8, Emitir9 };
struct Regla { unsigned estados; Cond condicion; Acc accion; };
struct Maquina {
  int estado=0, anterior=0, modo=0, fase=0, elegido=0;
  unsigned codigo=0xD2;
  float cuadro=0, reloj=0, tiempo=0, duracion=0, stick_x=0,stick_y=0;
  bool flag200=false,flag210=false,flag214=false,flag218=false;
  std::int16_t cc4=0;
  bool fin=false,latch224=false,tarea=false;
  V p, frente{0,0,1},vel,centro;
  float icono=0;
  std::uint64_t tick=0,revision=0;
  std::uint64_t tarea_actual=0,siguiente_tarea=0;
  noche::Root raiz;
  noche::Anclas anclas;
  noche::Work work;
  noche::Grid grid{-900,900,-900,900,3};
  wcf::Result barrido,rayo;
  wcf::Contact contacto_tarea5;
  V ultima_raiz;
  unsigned flags_consumidos=0,pose_codigo=0xD2;
  float pose_cuadro=0,cuadro_previo_fin=0;
  bool nueva_tarea5=false,bloqueado_c0=false;
  noche::MezclaEntrada mezcla;
  std::array<float,0xE3> largos{};
  std::array<Evento,4> eventos{};
  std::vector<Tarea> tareas; // mismo52FF: conservar orden de creacion, incluso tareas solapadas.
  int n_eventos=0;
  std::array<Cambio,8> cambios{};
  int n_cambios=0;
  void Iniciar(V posicion,const std::array<float,0xE3>& clips) { *this=Maquina{};p=posicion;largos=clips;raiz.posicion40=p; }
  void Entrar(int nuevo);
  int Selector() const;
  float Distancia(const Entrada& e) const { return Largo(Resta(e.jugador,anclas.actual640)); }
  float Frente(const Entrada& e) const { const V d=Normal(Resta(e.jugador,anclas.actual640));return d.x*frente.x+d.z*frente.z; }
  template<class Rng> void Crear(const Entrada& e,Rng& rng,bool lento,int forzado);
  template<class Rng> void IA(const Entrada& e,Rng& rng);
  template<class Rng> void Trayectoria(const Entrada& e,Rng& rng);
  template<class Pose> void EvaluarPose(Pose& pose,float frame);
  template<class Pose> void RootAndBones(Pose& pose,unsigned flags=0);
  template<class Rng,class Pose> void Paso(const Entrada& e,Rng& rng,Pose& pose,wcf::Backend& colision);
  template<class Rng,class Pose> void Tick(const Entrada& e,Rng& rng,Pose& pose,wcf::Backend& colision);
  void ActualizarAncla();
  void Emitir(int tipo,const Entrada& e);
  void Anotar(int desde) {
    if(n_cambios<8){
      const auto contacto=rayo.hit?rayo.contact:barrido.hit?barrido.contact:wcf::Contact{};
      cambios[n_cambios++]={desde,estado,codigo,cuadro,flag210,flag214,flag218,p,raiz.local50,ultima_raiz,pose_codigo,flags_consumidos,pose_cuadro,tarea_actual,contacto};
    }
  }
};
struct Golden {
  const char* nombre;bool ray;wcf::V desde,hasta,punto,normal;bool hit;unsigned material,tipo;
};
inline constexpr std::array<Golden,6> kGoldens = {{
  {"suelo100",true,{100,4,100},{100,-500,100},{100,0,100},{0,1,0},true,7,1},
  {"sin_choque",false,{100,1.7f,100},{100,1.7f,101},{100,1.7f,101},{},false,0,0},
  {"contacto_suelo",false,{100,1,100},{100,.2f,100},{100,.5f,100},{0,1,0},true,7,1},
  {"costuraX",false,{898,2,0},{899,2,0},{899,2,0},{},false,0,0},
  {"costuraXZ",false,{-898,2,-898},{-899,2,-899},{-899,2,-899},{},false,0,0},
  {"pared_real",false,{-515.3466796875f,16.4260177612f,-537.599182129f},
    {-515.650085449f,16.2442474365f,-536.881591797f},
    {-515.536315918f,16.3124084473f,-537.150695801f},
    {.3792309463f,.2272143215f,-.8969713449f},true,6,2}
}};
struct ResultadoGolden {bool pasa=false;float error_punto=0,error_normal=0;wcf::Result actual;std::string error;};
inline std::array<ResultadoGolden,6> SelfTestWcf(const std::vector<std::uint8_t>& datos);
struct ResultadoClock {
  unsigned total=0,pasadas=0,primerEE=0;std::uint32_t esperado=0,actual=0;std::string error;
};
inline ResultadoClock SelfTestClock(const char* archivo);
} // namespace dbz3::iwbubbles
#include "iw_bubbles_gp2.inl"

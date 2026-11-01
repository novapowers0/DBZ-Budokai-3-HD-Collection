// Prueba opt-in: reloj de CAMARA evaluada + dibujo; nunca usa reloj host/actor.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace dbz3::gp2cam {
// EU299A70 selecciona actor+40+altura o ancla640 segun work+1C8.
// Los estados que no escriben1C8 conservan el valor (caida0E incluida).
inline bool ModoAnclaOriginal(int e,bool anterior) {
  switch(e) {
    case 0:case 1:case 2:case 3:case 5:case 7:case 9:case 10:case 11:
    case 16:case 17:case 26:case 34:case 36:case 39:case 40:case 41:case 42:case 44:case 45:return false;
    case 4:case 12:case 18:case 19:case 20:case 21:case 22:case 23:case 24:
    case 25:case 28:case 29:case 30:case 31:case 32:case 33:return true;
    default:return anterior;
  }
}
inline bool Activo() {
  static const bool on = [] {
    const char* p = std::getenv("DBZ3_TEST_GP2_CAMARAS");
    return p && *p && std::getenv("DBZ3_TEST_IWMINI");
  }();
  return on;
}
struct Evaluacion {
  const char* familia = "desconocida";
  int cmn = -1, hijo = -1, banco = -1, clip = -1, clave_interna = -1;
  int fase = -1, paso = -1, duracion = -1;
  float cuadro = -1, ojo[3]{}, objetivo[3]{}, fov = -1, roll = 0;
};
struct Estado {
  std::mutex mu;
  Evaluacion e;
  bool hay = false;
  uint64_t evaluacion = 0, generacion = 0, dibujo = 0, dibujo_evaluado = 0;
  std::thread::id hilo_eval;
  FILE* archivo = nullptr;
  bool abrio = false;
  ~Estado() { if (archivo) std::fclose(archivo); }
};
inline Estado& Datos() { static Estado s; return s; }
inline void Evaluada(const char* familia, int cmn, int hijo, int banco, int clip,
    float cuadro, int duracion, int fase, int paso, const float* ojo,
    const float* objetivo, float fov, float roll = 0, int clave_interna = -1) {
  if (!Activo()) return; // Fuera de los dos env: no estado, mutex, archivo ni log.
  auto& s = Datos();
  std::lock_guard<std::mutex> lk(s.mu);
  if (!s.hay || std::strcmp(s.e.familia, familia) || s.e.cmn != cmn ||
      s.e.hijo != hijo || s.e.banco != banco || s.e.clip != clip ||
      s.e.fase != fase || s.e.paso != paso || cuadro < s.e.cuadro) ++s.generacion;
  s.e = Evaluacion{};
  s.e.familia=familia; s.e.cmn=cmn; s.e.hijo=hijo; s.e.banco=banco;
  s.e.clip=clip; s.e.clave_interna=clave_interna; s.e.cuadro=cuadro;
  s.e.duracion=duracion; s.e.fase=fase; s.e.paso=paso; s.e.fov=fov; s.e.roll=roll;
  for (int i=0;i<3;++i) {s.e.ojo[i]=ojo[i];s.e.objetivo[i]=objetivo[i];}
  s.hay=true; ++s.evaluacion; s.hilo_eval=std::this_thread::get_id();
}
// Disparo evalua en su panel: publicar en Fotograma el registro elegido, SIN
// inventar otro cuadro. La fila indica si el ultimo sampler vino de otro hilo.
inline void AplicadaDisparo(int cine) {
  if (!Activo()) return;
  auto& s=Datos(); std::lock_guard<std::mutex> lk(s.mu);
  if (!s.hay || std::strcmp(s.e.familia,"disparo")) return;
  s.e.paso=cine; // Conservar OE/FOV del sampler; no sustituirlos por la entrada transformada.
}
inline void Numero(std::ostream& o, float v) { if(std::isfinite(v)) o<<v; else o<<"null"; }
inline void Vector(std::ostream& o,const float* v,int n) {
  o<<'[';for(int i=0;i<n;++i){if(i)o<<',';Numero(o,v[i]);}o<<']';
}
inline void Dibujo(const float* vista,float fov_render) {
  if (!Activo()) return;
  auto& s=Datos(); std::lock_guard<std::mutex> lk(s.mu);
  ++s.dibujo;
  if(!s.abrio) {
    s.abrio=true;s.archivo=std::fopen(std::getenv("DBZ3_TEST_GP2_CAMARAS"),"wx");
    if(s.archivo) {
      const char* header="{\"tipo\":\"cabecera\",\"schema\":\"gp2_camaras_v1\",\"reloj\":\"cuadro_del_sampler_camara\",\"dibujo\":\"secuencia_PublicarCamaraRender\",\"matrices\":\"raw_sin_certificacion_visual\"}\n";
      std::fwrite(header,1,std::strlen(header),s.archivo);
    }
  }
  if(!s.archivo) return;
  std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(9);
  o<<"{\"tipo\":\"cuadro\",\"dibujo\":"<<s.dibujo<<",\"clave_prueba\":"
   <<std::strtoul(std::getenv("DBZ3_TEST_IWMINI"),nullptr,0)
   <<",\"hay_evaluacion\":"<<(s.hay?"true":"false")
   <<",\"evaluacion\":"<<s.evaluacion<<",\"generacion\":"<<s.generacion
   <<",\"evaluacion_nueva\":"<<(s.evaluacion!=s.dibujo_evaluado?"true":"false")
   <<",\"mismo_hilo_sampler\":"<<(s.hilo_eval==std::this_thread::get_id()?"true":"false")
   <<",\"evaluada\":{\"familia\":\""<<s.e.familia<<"\",\"cmn\":"<<s.e.cmn
   <<",\"hijo\":"<<s.e.hijo<<",\"banco\":"<<s.e.banco<<",\"clip\":"<<s.e.clip
   <<",\"clave_interna\":"<<s.e.clave_interna<<",\"fase\":"<<s.e.fase<<",\"paso\":"<<s.e.paso
   <<",\"duracion\":"<<s.e.duracion<<",\"cuadro\":";Numero(o,s.e.cuadro);
  o<<",\"ojo\":";Vector(o,s.e.ojo,3);o<<",\"objetivo\":";Vector(o,s.e.objetivo,3);
  o<<",\"fov\":";Numero(o,s.e.fov);o<<",\"roll\":";Numero(o,s.e.roll);
  o<<"},\"render\":{\"vista\":";Vector(o,vista,16);o<<",\"fov\":";Numero(o,fov_render);
  o<<"},\"certificacion_temporal\":false}\n";
  const auto line=o.str();std::fwrite(line.data(),1,line.size(),s.archivo);std::fflush(s.archivo);
  s.dibujo_evaluado=s.evaluacion;
}
} // namespace dbz3::gp2cam

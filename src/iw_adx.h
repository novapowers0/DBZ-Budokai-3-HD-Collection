// ADX de IW (AP7): decodificacion CRI4bits y slots independientes del actor.
// La seleccion de entrada viene SIEMPRE del ELF en iw_ap_eventos.h.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>
#include "iw_sonido.h"
#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#endif

namespace dbz3::iwadx {
struct Pcm { std::vector<int16_t> muestras; uint32_t hz=0; };
inline Pcm Leer(const std::vector<uint8_t>& b) {
  Pcm p;
  if(b.size()<32 || b[0]!=0x80 || b[1]!=0 || b[4]!=3 || b[6]!=4)return p;
  auto u=[&](size_t o){return uint32_t(b[o])<<24|uint32_t(b[o+1])<<16|uint32_t(b[o+2])<<8|b[o+3];};
  const size_t inicio=size_t(b[2])<<8|b[3],bloque=b[5],canales=b[7];
  const uint32_t hz=u(8),total=u(12),corte=uint32_t(b[16])<<8|b[17];
  if(inicio+4>b.size() || bloque<3 || canales<1 || canales>2 || !hz || hz>192000 || corte>hz/2)return p;
  const size_t por_bloque=(bloque-2)*2;
  if(total>((b.size()-inicio-4)/(bloque*canales))*por_bloque)return p;
  const double z=std::cos(2*3.14159265358979323846*corte/hz),a=std::sqrt(2.0)-z,t=std::sqrt(2.0)-1;
  const double c=(a-std::sqrt((a+t)*(a-t)))/t;
  const int c1=int(std::lrint(float(c*8192))),c2=int(std::lrint(float(c*c*-4096)));
  int hist[2][2]={};std::vector<int16_t> tmp(por_bloque*2);
  p.hz=hz;p.muestras.reserve(size_t(total)*2);
  for(size_t off=inicio+4;p.muestras.size()<size_t(total)*2;off+=bloque*canales) {
    for(size_t k=0;k<canales;++k) {
      const auto* fr=b.data()+off+k*bloque;
      const int escala=int(fr[0])<<8|fr[1];
      if(escala&0x8000)return {};
      for(size_t i=0;i<por_bloque;++i) {
        int v=i&1?fr[2+i/2]&15:fr[2+i/2]>>4;if(v>=8)v-=16;
        v=std::clamp(v*escala+((c1*hist[k][0]+c2*hist[k][1])>>12),-32768,32767);
        hist[k][1]=hist[k][0];hist[k][0]=v;tmp[k*por_bloque+i]=int16_t(v);
      }
    }
    for(size_t i=0;i<por_bloque && p.muestras.size()<size_t(total)*2;++i) {
      p.muestras.push_back(tmp[i]);p.muestras.push_back(canales==2?tmp[por_bloque+i]:tmp[i]);
    }
  }
  return p;
}
using Canal=iwsonido::Canal;
inline std::mutex g_registro_mu;
inline std::vector<iwsonido::Handle> g_registro;
inline iwsonido::Handle Registrar(iwsonido::Handle handle) {
  if(!handle)return 0;
  std::lock_guard lock(g_registro_mu);
  std::erase_if(g_registro,[](auto h){uint64_t frame=0;return !iwsonido::Salida().Mezcla().Posicion(h,frame);});
  g_registro.push_back(handle);return handle;
}
// ToDefine(-1): solo los ADX que ha iniciado este port; nunca borrar el
// mezclador completo, sus instrumentos MIDI ni los efectos de otros actores.
inline bool PararTodos() {
  std::vector<iwsonido::Handle> handles;
  {std::lock_guard lock(g_registro_mu);handles.swap(g_registro);}
  for(auto h:handles)iwsonido::Salida().Mezcla().Parar(h);
  return true;
}
inline iwsonido::Handle Oir(const Pcm& pcm,iwsonido::Opciones opciones) {
  return Registrar(iwsonido::Salida().Enviar(pcm.muestras,pcm.hz,std::move(opciones)));
}
// Compatibilidad con el llamador AP7: volumen era MasterVolume, no ganancia
// adicional por voz. IOP y ADX comparten la misma etapa final y dispositivo.
inline iwsonido::Handle Oir(const Pcm& pcm,const std::shared_ptr<Canal>& canal,float volumen) {
  if(pcm.muestras.empty() || volumen<=0){if(canal){++canal->generacion;canal->pausa=false;}return 0;}
  auto& s=iwsonido::Salida();s.Mezcla().Maestro(volumen);
  iwsonido::Opciones o;o.canal=canal;return Registrar(s.Enviar(pcm.muestras,pcm.hz,std::move(o)));
}
} // namespace dbz3::iwadx

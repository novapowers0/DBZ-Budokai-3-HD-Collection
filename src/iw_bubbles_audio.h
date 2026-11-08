// BBR EU318410: primer UPDATE de11D solicita voz0 y AMK48 una sola vez.
// El reloj AMK avanza, pero el estado10 no publica25FA90 sobre el cuerpo.
#pragma once
#include <array>
#include "iw_ap_boca.h"
#include "iw_adx.h"
#include "iw_bubbles_root.h"

namespace dbz3::iwbubbles::audio {
struct Recurso {
  int entrada=-1;
  iwadx::Pcm pcm;
  anim::apev::boca::BancoAMK boca;
  // Lineas 1..6 del mismo perfil (estados 1/2/6 y ramas de la IA); [0] queda vacia (es pcm).
  std::array<iwadx::Pcm,7> lineas;
};
struct Intro {
  bool solicitada=false;
  anim::apev::boca::Estado boca;
  // Se llama una vez por pasoPAL, despues de AnimationFramesLink. Al cambiar
  // de mision el propietario reinicia Intro; dibujar dos veces no la reinicia.
  bool Paso(int fase,unsigned codigo,float cuadro,const anim::apev::boca::BancoAMK& banco) {
    if(!std::isfinite(cuadro) || cuadro<0)return false;
    if(int32_t(boca.id)>=0){
      const auto* clip=banco.Obtener(boca.id&65535);
      if(!clip)boca.Reiniciar();
      else{
        //206E20 ADD.S: EE trunca hacia0. Al cercano terminaba un campo antes
        // (n17572 en vez de17573) en el AMK48 real de Bubbles.
        boca.cuadro=noche::ClockAdd(boca.cuadro,1.2f);
        if(boca.cuadro>=float(clip->duracion-1))boca.Reiniciar();
      }
    }
    if(solicitada || fase!=1 || codigo!=0x11D)return false;
    solicitada=true;boca.IniciarAP8(banco,0x48);
    return true;
  }
};
} // namespace dbz3::iwbubbles::audio

// Ejecuta la salida SDL REAL de iw_sonido.h con un receptor, sin altavoces.
// Tambien se ejecuta en Windows; no certifica ALSA/Pulse/PipeWire ni Linux.
#define DBZ3_IW_SDL_AUDIO
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace receptor {
std::mutex mu;std::condition_variable cv;
int refs=1,abiertos=0,cerrados=0,pendientes=0,max_cola=0,consultas=0;
bool liberar=false;std::string fallo,ultimo_error;
SDL_AudioSpec formato{};std::vector<std::vector<int16_t>> bloques;
void Exigir(bool v,const char* s){if(!v)throw std::runtime_error(s);}
void Esperar(auto pred){std::unique_lock l(mu);Exigir(cv.wait_for(l,std::chrono::seconds(3),pred),"timeout receptor");}
bool SDLCALL Init(SDL_InitFlags){std::lock_guard l(mu);if(fallo=="init")return false;++refs;return true;}
void SDLCALL Quit(SDL_InitFlags){std::lock_guard l(mu);--refs;cv.notify_all();}
SDL_AudioStream* SDLCALL Abrir(SDL_AudioDeviceID,const SDL_AudioSpec* spec,SDL_AudioStreamCallback,void*) {
  std::unique_lock l(mu);++abiertos;formato=*spec;cv.notify_all();cv.wait(l,[]{return liberar;});
  return fallo=="open"?nullptr:reinterpret_cast<SDL_AudioStream*>(1);
}
SDL_AudioDeviceID SDLCALL Dispositivo(SDL_AudioStream*){return 1;}
bool SDLCALL Formato(SDL_AudioDeviceID,SDL_AudioSpec* spec,int* periodo) {
  std::lock_guard l(mu);*spec=formato;*periodo=1024;return fallo!="format";
}
bool SDLCALL Resume(SDL_AudioStream*){std::lock_guard l(mu);return fallo!="resume";}
int SDLCALL Cola(SDL_AudioStream*) {
  std::lock_guard l(mu);++consultas;cv.notify_all();return fallo=="queued"?-1:pendientes;
}
bool SDLCALL Put(SDL_AudioStream*,const void* p,int bytes) {
  std::lock_guard l(mu);if(fallo=="put")return false;
  const auto* a=static_cast<const int16_t*>(p);bloques.emplace_back(a,a+bytes/2);
  pendientes+=bytes;max_cola=std::max(max_cola,pendientes);cv.notify_all();return true;
}
void SDLCALL Destruir(SDL_AudioStream*){std::lock_guard l(mu);++cerrados;pendientes=0;cv.notify_all();}
const char* SDLCALL Error(){return "error simulado";}
void SDLCALL Log(int,const char* formato,...) {
  // FalloSDL pasa (categoria, formato, paso, SDL_GetError()).
  va_list args;va_start(args,formato);const char* paso=va_arg(args,const char*);va_end(args);
  std::lock_guard l(mu);ultimo_error=paso;cv.notify_all();
}
static_assert(std::is_same_v<decltype(&Abrir),decltype(&SDL_OpenAudioDeviceStream)>);
static_assert(std::is_same_v<decltype(&Put),decltype(&SDL_PutAudioStreamData)>);
void Consumir(){std::lock_guard l(mu);Exigir(pendientes>=4096,"cola vacia");pendientes-=4096;}
void Bloque(size_t i,int valor) {
  std::lock_guard l(mu);Exigir(i<bloques.size(),"falta bloque");Exigir(bloques[i].size()==2048,"formato de bloque");
  for(auto x:bloques[i])Exigir(x==valor,"PCM/mezcla/volumen/pausa incorrecto");
}
std::atomic<float> volumen{.5f};std::atomic<bool> silencio{false};
}
#define SDL_InitSubSystem receptor::Init
#define SDL_QuitSubSystem receptor::Quit
#define SDL_OpenAudioDeviceStream receptor::Abrir
#define SDL_GetAudioStreamDevice receptor::Dispositivo
#define SDL_GetAudioDeviceFormat receptor::Formato
#define SDL_ResumeAudioStreamDevice receptor::Resume
#define SDL_GetAudioStreamQueued receptor::Cola
#define SDL_PutAudioStreamData receptor::Put
#define SDL_DestroyAudioStream receptor::Destruir
#define SDL_GetError receptor::Error
#define SDL_LogError receptor::Log
#include "../src/iw_sonido.h"

dbz3::iwsonido::Ajustes LeerAjustes(){return {receptor::volumen.load(),receptor::silencio.load()};}
int main() {
  using namespace receptor;using namespace dbz3::iwsonido;
  Reproductor r;
  try {
    r.UsarProveedor(LeerAjustes);
    const std::vector<int16_t> a(65536,1000),b(65536,2000),c(65536,4000);
    const auto ha=r.Enviar(a,48000);Esperar([]{return abiertos==1;});
    Opciones o;o.canal=r.Mezcla().Slot(7);o.ganancia=.5f;
    Exigir(r.Enviar(b,48000,o)!=0,"voz SDL descartada");
    {std::lock_guard l(mu);liberar=true;cv.notify_all();}
    Esperar([]{return bloques.size()==3;});
    Exigir(ha && formato.freq==48000 && formato.channels==2 && formato.format==SDL_AUDIO_S16,"formato salida");
    for(size_t i=0;i<3;++i)Bloque(i,1000);
    Exigir(!r.DispositivoAutomatico(false),"cambio de dispositivo durante reproduccion");
    volumen=.25f;Consumir();Esperar([]{return bloques.size()==4;});Bloque(3,500);
    silencio=true;Consumir();Esperar([]{return bloques.size()==5;});Bloque(4,0);
    silencio=false;o.canal->pausa=true;Consumir();Esperar([]{return bloques.size()==6;});Bloque(5,250);
    o.canal->pausa=false;Consumir();Esperar([]{return bloques.size()==7;});Bloque(6,500);
    r.Enviar(c,48000,o);Consumir();Esperar([]{return bloques.size()==8;});Bloque(7,750);
    r.Mezcla().PararSlot(7);Consumir();Esperar([]{return bloques.size()==9;});Bloque(8,250);
    Exigir(r.Mezcla().Parar(ha),"corte voz");Consumir();Esperar([]{return bloques.size()==10;});Bloque(9,0);
    r.Cerrar();Exigir(refs==1 && cerrados==1 && abiertos==1 && max_cola<=12288,"fuga o cola sin limite");
    // El mismo objeto puede reabrir. Fin no se anuncia al mezclar la ultima muestra.
    {std::lock_guard l(mu);bloques.clear();}
    Opciones fin;fin.notificar_fin=true;
    const auto hfin=r.Enviar({1000,1000},48000,fin);Esperar([]{return bloques.size()==3;});
    Exigir(!r.Terminado(hfin),"fin antes de consumir la cola SDL");
    Consumir();Esperar([]{return bloques.size()==4;});
    Exigir(!r.Terminado(hfin),"fin sin reservar periodo del dispositivo");
    Consumir();Esperar([]{return bloques.size()==5;});Exigir(r.Terminado(hfin),"fin no avanza con el dispositivo");
    r.Cerrar();Exigir(refs==1 && cerrados==2,"reapertura no limpia");
    Exigir(r.DispositivoAutomatico(false),"seleccion callback externo");
    const int antes=abiertos;const auto h=r.Enviar({1000,1000},48000);
    int16_t pcm[2]{};r.Procesar(pcm,1);Exigir(h && pcm[0]==250 && abiertos==antes,"callback abre SDL");r.Cerrar();
    // Todas las salidas fallidas liberan solo nuestra referencia de SDL.
    for(const char* paso:{"init","open","format","resume","queued","put"}) {
      {std::lock_guard l(mu);fallo=paso;ultimo_error.clear();}
      Reproductor e;const auto he=e.Enviar({1,1},48000);
      Esperar([]{return !ultimo_error.empty();});e.Cerrar();
      Exigir(he && e.ErrorSalida()!=0 && e.Terminado(he),"error no visible al llamador");
      Exigir(refs==1,"error quita referencia SDL del runtime");
      Exigir(ultimo_error==paso,"diagnostico no identifica etapa fallida");
    }
    std::cout<<"PASS SDL: PCM 48k estereo; mezcla/volumen/mute/pausa/sustitucion/corte; cola <=64ms; fin tras cola+periodo; reapertura/callback; errores init/open/format/resume/queued/put sin fugas. Receptor, no Linux real.\n";
    return 0;
  }catch(const std::exception& e) {
    {std::lock_guard l(mu);liberar=true;cv.notify_all();}
    r.Cerrar();std::cerr<<"FAIL "<<e.what()<<'\n';return 1;
  }
}

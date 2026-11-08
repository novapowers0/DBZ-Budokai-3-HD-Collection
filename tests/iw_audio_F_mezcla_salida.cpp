// Salida comun REAL con receptor waveOut: no dispositivo ni audio audible.
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace receptor {
std::mutex mu;std::condition_variable cv;
bool liberar=false;int abri=0,cerre=0,prepare=0,unprepare=0,escribi=0,resets=0;
bool fallar_open=false,fallar_write=false;
WAVEFORMATEX formato{};std::vector<WAVEHDR*> pendientes;
std::vector<std::vector<int16_t>> bloques;
void Exigir(bool v,const char* msg){if(!v)throw std::runtime_error(msg);}
void Esperar(auto pred) {std::unique_lock lock(mu);Exigir(cv.wait_for(lock,std::chrono::seconds(3),pred),"timeout receptor");}
MMRESULT WINAPI Abrir(LPHWAVEOUT wo,UINT,LPCWAVEFORMATEX f,DWORD_PTR,DWORD_PTR,DWORD) {
  std::unique_lock lock(mu);++abri;formato=*f;cv.notify_all();
  cv.wait(lock,[]{return liberar;});if(fallar_open)return MMSYSERR_ALLOCATED;
  *wo=reinterpret_cast<HWAVEOUT>(1);return MMSYSERR_NOERROR;
}
MMRESULT WINAPI Preparar(HWAVEOUT,LPWAVEHDR,UINT){std::lock_guard lock(mu);++prepare;return MMSYSERR_NOERROR;}
MMRESULT WINAPI Despreparar(HWAVEOUT,LPWAVEHDR,UINT){std::lock_guard lock(mu);++unprepare;return MMSYSERR_NOERROR;}
MMRESULT WINAPI Escribir(HWAVEOUT,LPWAVEHDR h,UINT) {
  std::lock_guard lock(mu);if(fallar_write){++escribi;cv.notify_all();return MMSYSERR_ERROR;}
  h->dwFlags&=~WHDR_DONE;
  bloques.emplace_back(reinterpret_cast<int16_t*>(h->lpData),reinterpret_cast<int16_t*>(h->lpData+h->dwBufferLength));
  pendientes.push_back(h);++escribi;cv.notify_all();return MMSYSERR_NOERROR;
}
MMRESULT WINAPI Reset(HWAVEOUT) {
  std::lock_guard lock(mu);++resets;for(auto* h:pendientes)h->dwFlags|=WHDR_DONE;
  pendientes.clear();cv.notify_all();return MMSYSERR_NOERROR;
}
MMRESULT WINAPI Cerrar(HWAVEOUT){std::lock_guard lock(mu);++cerre;cv.notify_all();return MMSYSERR_NOERROR;}
void Completar() {
  std::lock_guard lock(mu);Exigir(!pendientes.empty(),"ningun buffer para completar");
  pendientes.front()->dwFlags|=WHDR_DONE;pendientes.erase(pendientes.begin());
}
void ComprobarBloque(size_t n,int valor) {
  std::lock_guard lock(mu);Exigir(n<bloques.size(),"bloque no enviado");
    Exigir(bloques[n].size()==2048,"longitud de bloque48k (1024 cuadros estereo)");
  Exigir(std::all_of(bloques[n].begin(),bloques[n].end(),[&](int x){return x==valor;}),"mezcla/volumen/pausa incorrectos");
}
}
#define waveOutOpen receptor::Abrir
#define waveOutPrepareHeader receptor::Preparar
#define waveOutUnprepareHeader receptor::Despreparar
#define waveOutWrite receptor::Escribir
#define waveOutReset receptor::Reset
#define waveOutClose receptor::Cerrar
#include "../src/iw_sonido.h"
namespace dbz3::settings {
std::atomic<float> volumen{.5f};std::atomic<bool> silencio{false};
double MasterVolume(){return volumen.load();}bool AudioMute(){return silencio.load();}
}
namespace dbz3::pasarela {std::vector<uint8_t> HijoAmb(const std::vector<uint8_t>&,uint32_t){return {};}}
#define REXLOG_INFO(...) ((void)0)
#include DBZ3_IW_IOP_CORE
#include DBZ3_IW_ADX_CORE

int main(int argc,char** argv) {
  try {
    using namespace receptor;using namespace dbz3;
    iwiop::Voz voz;voz.pcm.assign(32768,1000);
    const auto hi=iwiop::Oir(voz);Esperar([]{return abri==1;});
    auto canal=std::make_shared<iwadx::Canal>();
    iwadx::Pcm adx;adx.hz=48000;adx.muestras.assign(32768,9000);
    iwadx::Oir(adx,canal,.5f);adx.muestras.assign(32768,2000);
    const auto ha=iwadx::Oir(adx,canal,.5f);Exigir(hi && ha,"handles IOP/ADX");
    {std::lock_guard lock(mu);liberar=true;cv.notify_all();}
    Esperar([]{return escribi==3;});
    Exigir(formato.nSamplesPerSec==48000 && formato.nChannels==2 && formato.nBlockAlign==4 && formato.wBitsPerSample==16,"formato comun");
    Exigir(abri==1 && prepare==3,"mas de un dispositivo o cola no acotada");
    for(size_t n=0;n<3;++n)ComprobarBloque(n,1500); // (1000+2000)*.5, no .5 dos veces
    settings::volumen=.25f;Completar();Esperar([]{return escribi==4;});ComprobarBloque(3,750);
    settings::silencio=true;Completar();Esperar([]{return escribi==5;});ComprobarBloque(4,0);
    settings::silencio=false;canal->pausa=true;Completar();Esperar([]{return escribi==6;});ComprobarBloque(5,250);
    canal->pausa=false;Completar();Esperar([]{return escribi==7;});ComprobarBloque(6,750);
    iwadx::Oir({},canal,0);Completar();Esperar([]{return escribi==8;});ComprobarBloque(7,250);
    Exigir(iwsonido::Salida().Mezcla().Parar(hi),"corte por handle IOP");
    Completar();Esperar([]{return escribi==9;});ComprobarBloque(8,0);
    iwsonido::Salida().Cerrar();Exigir(cerre==1 && unprepare==3 && resets==1,"cierre y liberacion de buffers");
    // API explicita: volumen final .25, ganancia de fuente .5; el launcher
    // vale .9 y NO se debe volver a multiplicar. Mismo DSP por callback externo.
    Exigir(iwsonido::Salida().DispositivoAutomatico(false),"seleccionar callback externo");
    settings::volumen=.9f;iwsonido::Opciones o;o.ganancia=.5f;
    const auto he=iwiop::Oir(voz,.25f,o);int16_t muestra[2];iwsonido::Salida().Procesar(muestra,1);
    Exigir(he && muestra[0]==125 && muestra[1]==125,"volumen explicito duplicado por MasterVolume");
    iwsonido::Salida().Cerrar();Exigir(abri==1,"callback externo abrio dispositivo");
    o.ganancia=1;o.canal=iwsonido::Salida().Mezcla().Slot(88);
    iwiop::Oir(voz,o);o.sustituir=true;iwadx::Oir(adx,o);
    iwsonido::Salida().Procesar(muestra,1);
    Exigir(muestra[0]==1800 && muestra[1]==1800,"opciones/slot compartido entre IOP y ADX");
    iwsonido::Salida().Cerrar();
    Exigir(argc==2,"falta referencia de impulso");
    std::ifstream f(std::filesystem::path(argv[1])/"impulso_0_cola.s16",std::ios::binary|std::ios::ate);
    Exigir(bool(f),"falta salida contrastada con IRX/especificacion");
    const auto bytes=f.tellg();std::vector<int16_t> referencia(size_t(bytes)/2),salida(referencia.size());
    f.seekg(0);f.read(reinterpret_cast<char*>(referencia.data()),bytes);
    iwiop::Voz impulso;impulso.pcm={32767,0};impulso.rutas=12;settings::volumen=1;
    iwiop::Oir(impulso);iwsonido::Salida().Procesar(salida.data(),salida.size()/2);
    Exigir(salida==referencia,"Oir no hereda routing/reverb ni conserva cola tras terminar la voz");
    iwsonido::Salida().Cerrar();Exigir(abri==1,"DSP por callback abrio segundo dispositivo");
    // Error de dispositivo verificable por el llamador; no bucle de reintento.
    fallar_open=true;{
      iwsonido::Reproductor r;r.Enviar({1,1},48000);Esperar([]{return abri==2;});r.Cerrar();
      Exigir(r.ErrorSalida()==MMSYSERR_ALLOCATED,"error de Open perdido");
    }
    fallar_open=false;fallar_write=true;{
      iwsonido::Reproductor r;r.Enviar({1,1},48000);Esperar([]{return cerre==2;});r.Cerrar();
      Exigir(r.ErrorSalida()==MMSYSERR_ERROR,"error de Write perdido");
    }
    Exigir(prepare==6 && unprepare==6,"fuga tras Write fallido");
    std::cout<<"PASS IOP+ADX: un dispositivo/3buffers; MasterVolume UNA vez y en vivo; mute, pausa, reanudacion, sustitucion y corte; API explicita/callback; Oir hereda routing y cola original; errores Open/Write sin fugas\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"FAIL "<<e.what()<<'\n';
    {std::lock_guard lock(receptor::mu);receptor::liberar=true;receptor::cv.notify_all();}
    dbz3::iwsonido::Salida().Cerrar();return 1;
  }
}

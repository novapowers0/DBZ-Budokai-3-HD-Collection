// Ejecuta Oir REAL con waveOut sustituido SOLO en esta unidad de prueba.
// Captura exactamente WAVEFORMATEX y los bytes enviados; nunca abre un altavoz.
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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
#include "../src/iw_ap_eventos.h"

namespace receptor {
std::mutex mutex;
std::condition_variable cv;
WAVEFORMATEX formato{};
std::vector<int16_t> datos;
int abri=0,escribi=0,cerre=0,pausas=0,reinicios=0,cortes=0;
bool bloquear=false,liberar=false,terminar=true;
void Exigir(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
void Esperar(auto condicion) {
  std::unique_lock lock(mutex);
  Exigir(cv.wait_for(lock,std::chrono::seconds(3),condicion),"timeout del receptor");
}
MMRESULT WINAPI Abrir(LPHWAVEOUT h,UINT,LPCWAVEFORMATEX f,DWORD_PTR,DWORD_PTR,DWORD) {
  std::unique_lock lock(mutex);formato=*f;++abri;*h=reinterpret_cast<HWAVEOUT>(1);cv.notify_all();
  if(bloquear)cv.wait(lock,[]{return liberar;});
  return MMSYSERR_NOERROR;
}
MMRESULT WINAPI Preparar(HWAVEOUT,LPWAVEHDR,UINT){return MMSYSERR_NOERROR;}
MMRESULT WINAPI Escribir(HWAVEOUT,LPWAVEHDR h,UINT) {
  std::lock_guard lock(mutex);
  datos.assign(reinterpret_cast<int16_t*>(h->lpData),reinterpret_cast<int16_t*>(h->lpData+h->dwBufferLength));
  ++escribi;if(terminar)h->dwFlags|=WHDR_DONE;cv.notify_all();return MMSYSERR_NOERROR;
}
MMRESULT WINAPI Cerrar(HWAVEOUT){std::lock_guard lock(mutex);++cerre;cv.notify_all();return MMSYSERR_NOERROR;}
MMRESULT WINAPI Pausar(HWAVEOUT){std::lock_guard lock(mutex);++pausas;cv.notify_all();return MMSYSERR_NOERROR;}
MMRESULT WINAPI Reiniciar(HWAVEOUT){std::lock_guard lock(mutex);++reinicios;cv.notify_all();return MMSYSERR_NOERROR;}
MMRESULT WINAPI Cortar(HWAVEOUT){std::lock_guard lock(mutex);++cortes;cv.notify_all();return MMSYSERR_NOERROR;}
}
#define waveOutOpen receptor::Abrir
#define waveOutPrepareHeader receptor::Preparar
#define waveOutUnprepareHeader receptor::Preparar
#define waveOutWrite receptor::Escribir
#define waveOutClose receptor::Cerrar
#define waveOutPause receptor::Pausar
#define waveOutRestart receptor::Reiniciar
#define waveOutReset receptor::Cortar
namespace dbz3::pasarela {
std::vector<uint8_t> HijoAmb(const std::vector<uint8_t>& b,uint32_t k) {
  using namespace anim::apev;
  if(U32(b,0)!=0x424d4123 || k>=U32(b,16))return {};
  const size_t t=U32(b,20)+k*16,o=U32(b,t),n=U32(b,t+4);
  if(!Rango(b,o,n))return {};return {b.begin()+o,b.begin()+o+n};
}
}
namespace dbz3::settings {bool mute=false;bool AudioMute(){return mute;}double MasterVolume(){return .5;}}
#define REXLOG_INFO(...) ((void)0)
#include DBZ3_IW_IOP_CORE
#include DBZ3_IW_ADX_CORE

std::vector<int16_t> Pcm(const std::filesystem::path& path) {
  std::ifstream f(path,std::ios::binary|std::ios::ate);receptor::Exigir(bool(f),"PCM inexistente");
  const auto n=f.tellg();std::vector<int16_t> b(size_t(n)/2);f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);return b;
}
void Validar(const std::vector<int16_t>& fuente,int hz,const std::filesystem::path& archivo) {
  receptor::Exigir(receptor::formato.nSamplesPerSec==uint32_t(hz) && receptor::formato.nChannels==2 &&
    receptor::formato.nBlockAlign==4 && receptor::formato.wBitsPerSample==16 &&
    receptor::formato.nAvgBytesPerSec==uint32_t(hz)*4,"formato de salida");
  receptor::Exigir(receptor::datos.size()==fuente.size(),"longitud de salida");
  for(size_t i=0;i<fuente.size();++i)receptor::Exigir(receptor::datos[i]==int(fuente[i]*.5f),"volumen/orden de salida");
  std::ofstream f(archivo,std::ios::binary);f.write(reinterpret_cast<const char*>(receptor::datos.data()),receptor::datos.size()*2);
}
int main(int argc,char** argv) {
  try {
    using namespace receptor;
    Exigir(argc==2,"uso: salida <tmp existente>");const std::filesystem::path out=argv[1];
    dbz3::iwiop::Voz voz;voz.pcm=Pcm(out/"despues/954_58.s16");
    dbz3::iwiop::Oir(voz);Esperar([]{return cerre==1;});
    Validar(voz.pcm,48000,out/"salida_iop.s16");
    std::ifstream raw(out/"original.adx",std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(raw),{}};
    auto adx=dbz3::iwadx::Leer(bytes);
    Exigir(adx.muestras==Pcm(out/"adx_original.s16"),"ADX vs FFmpeg antes de salir");
    auto canal=std::make_shared<dbz3::iwadx::Canal>();
    dbz3::iwadx::Oir(adx,canal,.5);Esperar([]{return cerre==2;});
    Validar(adx.muestras,int(adx.hz),out/"salida_adx.s16");
    // Reproduce la carrera real: nuevo evento invalida el slot durante Open.
    {std::lock_guard lock(mutex);bloquear=true;liberar=false;}
    dbz3::iwadx::Oir(adx,canal,.5);Esperar([]{return abri==3;});
    ++canal->generacion;
    {std::lock_guard lock(mutex);liberar=true;cv.notify_all();}
    Esperar([]{return cerre==3;});Exigir(escribi==2,"slot sustituido emitio audio obsoleto");
    // Pausa pedida antes del primer Write debe existir ya al encolar el buffer.
    {std::lock_guard lock(mutex);liberar=false;terminar=false;}
    dbz3::iwadx::Oir(adx,canal,.5);Esperar([]{return abri==4;});canal->pausa=true;
    {std::lock_guard lock(mutex);liberar=true;cv.notify_all();}
    Esperar([]{return escribi==3;});Exigir(pausas==1,"pausa inicial perdida");
    canal->pausa=false;Esperar([]{return reinicios==1;});
    dbz3::iwadx::Oir({},canal,0);Esperar([]{return cerre==4;});Exigir(cortes==1,"corte de slot no aplicado");
    // El silencio de ajustes no debe abrir otro dispositivo.
    dbz3::settings::mute=true;dbz3::iwiop::Oir(voz);Exigir(abri==4,"mute ignorado");
    std::cout<<"PASS salida IOP/ADX bytes+formato+volumen; slot sustituido, pausa previa, reanudacion, corte y mute; receptor sin dispositivo\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

// IW: mezcla persistente de IOP/ADX y reverb SPU2. Sin memoria guest ni SDK.
// DSP segun psx-spx; efecto3 verificado ejecutando LIBSD.IRX, export23.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#endif

namespace dbz3::iwsonido {
struct Estereo {int32_t l=0,r=0;};
inline int32_t Saturar(int64_t v) {return int32_t(std::clamp<int64_t>(v,-32768,32767));}
inline int32_t Producto(int64_t a,int32_t b) {return int32_t((a*b)>>15);}
inline float Ganancia(float v) {return std::isfinite(v)?std::clamp(v,0.0f,1.0f):0.0f;}

// Preset hardware Studio Medium. Los offsets del SPU original son unidades8B;
// LIBSD los escribe multiplicados por4 en registros SPU2 (palabras de16bits).
inline constexpr std::array<uint16_t,32> kEfecto3={
  0x00b1,0x007f,0x70f0,0x4fa8,0xbce0,0x4510,0xbef0,0xb4c0,
  0x5280,0x4ec0,0x0904,0x076b,0x0824,0x065f,0x07a2,0x0616,
  0x076c,0x05ed,0x05ec,0x042e,0x050f,0x0305,0x0462,0x02b7,
  0x042f,0x0265,0x0264,0x01b2,0x0100,0x0080,0x8000,0x8000
};
inline constexpr std::array<int16_t,39> kFirReverb={
  -1,0,2,0,-10,0,35,0,-103,0,266,0,-616,0,1332,0,-2960,0,10246,
  16384,10246,0,-2960,0,1332,0,-616,0,266,0,-103,0,35,0,-10,0,2,0,-1
};
class Reverb {
 public:
  static constexpr size_t kPalabras=0x4840/2;
  void Configurar3(int16_t izquierda=0x050a,int16_t derecha=0x050a) {
    ram_.fill(0);for(auto& x:entrada_)x.fill(0);for(auto& x:salida_)x.fill(0);
    reloj_=0;profundidad_={izquierda,derecha};activo_=true;
  }
  void Habilitar(bool activo) {activo_=activo;}
  bool Activo() const {return activo_;}
  void Limpiar() {*this=Reverb{};}
  Estereo Paso(Estereo fuente) {
    const size_t p=size_t(reloj_&63),lado=size_t(reloj_&1);
    entrada_[0][p]=int16_t(Saturar(fuente.l));entrada_[1][p]=int16_t(Saturar(fuente.r));
    int64_t acumulado=0;
    for(size_t k=0;k<kFirReverb.size();++k)
      acumulado+=int64_t(entrada_[lado][(p+64-39+k)&63])*kFirReverb[k];
    const int32_t in=Producto(Saturar(acumulado>>15),int16_t(kEfecto3[30+lado]));
    auto leer=[&](int64_t offset) {return int32_t(ram_[Indice(offset)]);};
    auto direccion=[&](size_t registro) {return int64_t(kEfecto3[registro])*4;};
    const auto same=direccion(10+lado),diff=direccion(18+lado);
    const int32_t anterior_same=leer(same-1),anterior_diff=leer(diff-1);
    const int32_t mismo=Producto(in+Producto(leer(direccion(16+lado)),int16_t(kEfecto3[7]))-anterior_same,
                                int16_t(kEfecto3[2]))+anterior_same;
    const int32_t cruzado=Producto(in+Producto(leer(direccion(24+(1-lado))),int16_t(kEfecto3[7]))-anterior_diff,
                                  int16_t(kEfecto3[2]))+anterior_diff;
    int32_t comb=0;
    for(size_t k=0;k<4;++k) {
      const size_t registro=(k<2?12+2*k:20+2*(k-2))+lado;
      comb+=Producto(leer(direccion(registro)),int16_t(kEfecto3[3+k]));
    }
    const auto apf1=direccion(26+lado),apf2=direccion(28+lado);
    const int32_t viejo1=leer(apf1-direccion(0)),viejo2=leer(apf2-direccion(1));
    const int32_t escribir1=comb-Producto(viejo1,int16_t(kEfecto3[8]));
    const int32_t intermedio=viejo1+Producto(escribir1,int16_t(kEfecto3[8]));
    const int32_t escribir2=intermedio-Producto(viejo2,int16_t(kEfecto3[9]));
    const int32_t wet=Saturar(viejo2+Producto(escribir2,int16_t(kEfecto3[9])));
    // Lecturas completas antes de escribir: evita depender de alias entre taps.
    if(activo_) {
      ram_[Indice(same)]=int16_t(Saturar(mismo));ram_[Indice(diff)]=int16_t(Saturar(cruzado));
      ram_[Indice(apf1)]=int16_t(Saturar(escribir1));ram_[Indice(apf2)]=int16_t(Saturar(escribir2));
    }
    salida_[lado][p]=activo_?int16_t(wet):0;salida_[1-lado][p]=0;
    ++reloj_;Estereo out;int32_t* canales[]={&out.l,&out.r};
    for(size_t ch=0;ch<2;++ch) {
      int64_t total=0;
      for(size_t k=0;k<kFirReverb.size();++k) {
        const int coef=std::clamp(int(kFirReverb[k])*2,-32768,32767);
        total+=int64_t(salida_[ch][(p+1+64-39+k)&63])*coef;
      }
      *canales[ch]=activo_?Producto(Saturar(total>>15),profundidad_[ch]):0;
    }
    return out;
  }
 private:
  size_t Indice(int64_t offset) const {
    int64_t p=int64_t((reloj_/2)%kPalabras)+offset;p%=int64_t(kPalabras);
    if(p<0)p+=kPalabras;return size_t(p);
  }
  std::array<int16_t,kPalabras> ram_{};
  std::array<std::array<int16_t,64>,2> entrada_{},salida_{};
  std::array<int16_t,2> profundidad_{};
  uint64_t reloj_=0;
  bool activo_=false;
};

struct Canal {std::atomic<uint32_t> generacion{0};std::atomic<bool> pausa{false};};
using Handle=uint64_t;
// Rutas: bit0/1 secoL/R; bit2/3 envio a reverbL/R (VMIXL/R/EL/ER).
// NO inferir envio a reverb solo porque el core tiene un efecto configurado.
struct Opciones {
  unsigned core=0;
  uint8_t rutas=0xff; // heredar del decoder; sin metadatos (ADX/PCM), secoL/R
  float ganancia=1.0f; // fuente, antes de la reverb; no MasterVolume
  std::shared_ptr<Canal> canal;
  bool sustituir=true;
  bool notificar_fin=false; // consumidores que esperan fin audible, no solo fin de mezcla
};
class Mezclador {
 public:
  Handle Iniciar(std::shared_ptr<const std::vector<int16_t>> pcm,uint32_t hz,Opciones o={}) {
    std::lock_guard lock(mutex_);
    uint32_t gen=0;
    if(o.canal) {gen=o.sustituir?++o.canal->generacion:o.canal->generacion.load();if(o.sustituir)o.canal->pausa=false;}
    if(!pcm || pcm->empty() || pcm->size()%2 || !hz || hz>192000 || o.core>1)return 0;
    o.rutas=o.rutas==0xff?3:uint8_t(o.rutas&15);
    o.ganancia=Ganancia(o.ganancia);
    const Handle h=++siguiente_;if(o.notificar_fin)finales_[h]=~uint64_t(0);
    voces_.emplace(h,Voz{std::move(pcm),hz,0,std::move(o),gen});return h;
  }
  Handle Iniciar(const std::vector<int16_t>& pcm,uint32_t hz,Opciones o={}) {
    return Iniciar(std::make_shared<const std::vector<int16_t>>(pcm),hz,std::move(o));
  }
  std::shared_ptr<Canal> Slot(uint64_t id) {
    std::lock_guard lock(mutex_);auto& s=slots_[id];if(!s)s=std::make_shared<Canal>();return s;
  }
  void PararSlot(uint64_t id) {const auto s=Slot(id);++s->generacion;s->pausa=false;}
  void PausarSlot(uint64_t id,bool pausa) {Slot(id)->pausa=pausa;}
  bool Parar(Handle id) {std::lock_guard lock(mutex_);const bool habia=voces_.erase(id)!=0;
    if(habia)if(auto it=finales_.find(id);it!=finales_.end())it->second=cuadros_renderizados_;return habia;}
  bool FinalMezclado(Handle id,uint64_t& cuadro) const {
    std::lock_guard lock(mutex_);const auto it=finales_.find(id);if(it==finales_.end())return false;
    cuadro=it->second;return true;
  }
  void OlvidarFin(Handle id){std::lock_guard lock(mutex_);finales_.erase(id);}
  uint64_t CuadrosRenderizados()const{std::lock_guard lock(mutex_);return cuadros_renderizados_;}
  bool Pausar(Handle id,bool pausa) {
    std::lock_guard lock(mutex_);auto it=voces_.find(id);if(it==voces_.end())return false;
    it->second.pausa=pausa;return true;
  }
  // Cursor de la fuente realmente consumida por Renderizar (no tiempo UI).
  // La cola de waveOut es posterior: no incluir muestras aun no mezcladas.
  bool Posicion(Handle id,uint64_t& cuadros) const {
    std::lock_guard lock(mutex_);const auto it=voces_.find(id);
    if(it==voces_.end() || (it->second.o.canal &&
       it->second.o.canal->generacion.load()!=it->second.generacion))return false;
    cuadros=it->second.posicion/48000;return true;
  }
  // MVOL SPU2 fijo: bit14 signo, bit15 barrido (requiere otro consumidor).
  // Separado de la ganancia/silencio que el jugador elige en el launcher.
  bool VolumenSpu2(unsigned core,uint16_t izquierda,uint16_t derecha) {
    if(core>1 || ((izquierda|derecha)&0x8000))return false;
    std::lock_guard lock(mutex_);
    mvol_[core]={int16_t(uint16_t(izquierda<<1)),int16_t(uint16_t(derecha<<1))};return true;
  }
  void ConfigurarEfecto3(unsigned core,int16_t l=0x050a,int16_t r=0x050a) {
    std::lock_guard lock(mutex_);if(core<2)reverb_[core].Configurar3(l,r);
  }
  void AsegurarEfecto3() {
    std::lock_guard lock(mutex_);for(auto& r:reverb_)if(!r.Activo())r.Configurar3();
  }
  void Maestro(float volumen,bool silencio=false) {
    std::lock_guard lock(mutex_);maestro_=Ganancia(volumen);silencio_=silencio;
  }
  void Limpiar() {
    std::lock_guard lock(mutex_);voces_.clear();finales_.clear();for(auto& r:reverb_)r.Limpiar();
    for(auto& v:mvol_)v={32768,32768};
    for(auto& [id,c]:slots_){(void)id;++c->generacion;c->pausa=false;}
  }
  size_t Voces() const {std::lock_guard lock(mutex_);return voces_.size();}
  // Produce exactamente cuadros48k. Sin dispositivo, utilizable por cualquier
  // callback host. Silenciar NO detiene voces/reloj/cola de reverberacion.
  void Renderizar(int16_t* destino,size_t cuadros) {
    std::lock_guard lock(mutex_);
    for(size_t f=0;f<cuadros;++f) {
      Estereo seco[2]{},wet[2]{};
      for(auto it=voces_.begin();it!=voces_.end();) {
        auto& v=it->second;const size_t i=size_t(v.posicion/48000),n=v.pcm->size()/2;
        if(i>=n || (v.o.canal && v.o.canal->generacion.load()!=v.generacion)){
          if(auto fin=finales_.find(it->first);fin!=finales_.end())fin->second=cuadros_renderizados_+f;
          it=voces_.erase(it);continue;
        }
        if(!v.pausa && !(v.o.canal && v.o.canal->pausa.load())) {
          int32_t valor[2];
          for(size_t ch=0;ch<2;++ch) {
            const int a=(*v.pcm)[i*2+ch],b=(*v.pcm)[std::min(i+1,n-1)*2+ch];
            const int muestra=a+int(int64_t(b-a)*int64_t(v.posicion%48000)/48000);
            valor[ch]=int32_t(muestra*v.o.ganancia);
          }
          auto& s=seco[v.o.core];auto& w=wet[v.o.core];
          if(v.o.rutas&1)s.l+=valor[0];if(v.o.rutas&2)s.r+=valor[1];
          if(v.o.rutas&4)w.l+=valor[0];if(v.o.rutas&8)w.r+=valor[1];v.posicion+=v.hz;
        }
        if(v.posicion/48000>=n){
          if(auto fin=finales_.find(it->first);fin!=finales_.end())fin->second=cuadros_renderizados_+f+1;
          it=voces_.erase(it);
        }else ++it;
      }
      Estereo salida[2];
      for(size_t core=0;core<2;++core) {
        const auto eco=reverb_[core].Paso({Saturar(wet[core].l),Saturar(wet[core].r)});
        salida[core]={Producto(Saturar(Saturar(seco[core].l)+eco.l),mvol_[core][0]),
                      Producto(Saturar(Saturar(seco[core].r)+eco.r),mvol_[core][1])};
      }
      // MMIX0FC0/0FCC: core0 llega SECO al externo de core1; no atraviesa una
      // segunda reverb. Ganancia maestra host UNA vez, despues de ambos buses.
      destino[f*2]=silencio_?0:int16_t(Saturar(salida[0].l+salida[1].l)*maestro_);
      destino[f*2+1]=silencio_?0:int16_t(Saturar(salida[0].r+salida[1].r)*maestro_);
    }
    cuadros_renderizados_+=cuadros;
  }
 private:
  struct Voz {
    std::shared_ptr<const std::vector<int16_t>> pcm;uint32_t hz;uint64_t posicion;
    Opciones o;uint32_t generacion;bool pausa=false;
  };
  mutable std::mutex mutex_;
  std::map<Handle,Voz> voces_;
  std::map<Handle,uint64_t> finales_;
  uint64_t cuadros_renderizados_=0;
  std::map<uint64_t,std::shared_ptr<Canal>> slots_;
  Reverb reverb_[2];Handle siguiente_=0;float maestro_=1;bool silencio_=false;
  std::array<std::array<int32_t,2>,2> mvol_{{{{32768,32768}},{{32768,32768}}}};
};

struct Ajustes {float volumen=1;bool silencio=false;};
using Proveedor=Ajustes(*)();
// Una unica salida48k para ambos decodificadores. Tres bloques1024cuadros
// dan64ms de cola (16/32ms se agotaban en WinMM bajo carga); ningun hilo/dispositivo por voz. El DSP es el
// mismo que usa Renderizar(), por lo que se puede probar sin altavoces.
class Reproductor {
 public:
  ~Reproductor(){Cerrar();}
  Mezclador& Mezcla(){return mezcla_;}
  bool Terminado(Handle h)const {
    if(!h || error_.load())return true;
    uint64_t fin=0;
    if(!mezcla_.FinalMezclado(h,fin)){uint64_t p=0;return !mezcla_.Posicion(h,p);}
    return fin!=~uint64_t(0) && (!automatico_ || reproducidos_.load()>=fin);
  }
  void UsarProveedor(Proveedor p){std::lock_guard lock(ajustes_);proveedor_.store(p);}
  bool DispositivoAutomatico(bool activo) {
    std::lock_guard lock(inicio_);if(hilo_.joinable())return false;
    automatico_=activo;return true; // elegir antes de enviar; callback externo usa Procesar
  }
  void Procesar(int16_t* salida,size_t cuadros) {
    {
      // Al pasar de launcher a volumen explicito, ninguna lectura anterior del
      // proveedor puede sobrescribir despues la nueva ganancia maestra.
      std::lock_guard lock(ajustes_);
      if(const auto p=proveedor_.load()){const auto a=p();mezcla_.Maestro(a.volumen,a.silencio);}
    }
    mezcla_.Renderizar(salida,cuadros);
  }
  Handle Enviar(const std::vector<int16_t>& pcm,uint32_t hz,Opciones o={}) {
    std::lock_guard lock(inicio_);const auto h=mezcla_.Iniciar(pcm,hz,std::move(o));
#if defined(_WIN32)
    if(h && automatico_ && !hilo_.joinable()) {
      error_=0;hilo_=std::jthread([this](std::stop_token stop){Trabajar(stop);});
    }
#else
    if(h && automatico_){mezcla_.Parar(h);error_=0xffffffffu;return 0;}
#endif
    return h;
  }
  void Cerrar() {
    std::lock_guard lock(inicio_);
    if(hilo_.joinable()){hilo_.request_stop();hilo_.join();}
    mezcla_.Limpiar();
  }
  uint32_t ErrorSalida()const{return error_.load();}
 private:
#if defined(_WIN32)
  void Trabajar(std::stop_token stop) {
    WAVEFORMATEX formato{};formato.wFormatTag=WAVE_FORMAT_PCM;formato.nChannels=2;
    formato.nSamplesPerSec=48000;formato.wBitsPerSample=16;formato.nBlockAlign=4;formato.nAvgBytesPerSec=192000;
    // Medido: la cola de16ms se vaciaba (24k avanzaba a9624muestras/s).
    // La sonda silenciosa confirma la falta de margen incluso con evento;
    // usar64ms y despertar por el dispositivo, sin sondeo Sleep(2).
    HANDLE disponible=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!disponible){error_=GetLastError();return;}
    HWAVEOUT wo=nullptr;MMRESULT error=waveOutOpen(&wo,WAVE_MAPPER,&formato,reinterpret_cast<DWORD_PTR>(disponible),0,CALLBACK_EVENT);
    if(error!=MMSYSERR_NOERROR){CloseHandle(disponible);error_=error;return;}
    const uint64_t base_salida=mezcla_.CuadrosRenderizados();reproducidos_=base_salida;
    uint32_t pos_anterior=0;uint64_t vueltas=0;
    struct Bloque {std::array<int16_t,2048> pcm{};WAVEHDR cabecera{};bool preparado=false,enviado=false;};
    std::array<Bloque,3> bloques;
    for(auto& b:bloques) {
      b.cabecera.lpData=reinterpret_cast<LPSTR>(b.pcm.data());b.cabecera.dwBufferLength=DWORD(b.pcm.size()*2);
      error=waveOutPrepareHeader(wo,&b.cabecera,sizeof(WAVEHDR));if(error)break;b.preparado=true;
    }
    while(!error && !stop.stop_requested()) {
      bool trabajo=false;
      for(auto& b:bloques) {
        if(b.enviado && !(b.cabecera.dwFlags&WHDR_DONE))continue;
        Procesar(b.pcm.data(),b.pcm.size()/2);
        if(stop.stop_requested())break;
        error=waveOutWrite(wo,&b.cabecera,sizeof(WAVEHDR));if(error)break;
        b.enviado=true;trabajo=true;
      }
      // La notificacion de fin debe esperar a que la cola llegue al
      // dispositivo. Mezclar el ultimo sample no significa haberlo oido.
      MMTIME t{};t.wType=TIME_SAMPLES;
      if(waveOutGetPosition(wo,&t,sizeof(t))==MMSYSERR_NOERROR) {
        const uint32_t p=t.wType==TIME_SAMPLES?t.u.sample:t.wType==TIME_BYTES?t.u.cb/4:pos_anterior;
        if(p<pos_anterior)vueltas+=uint64_t(1)<<(t.wType==TIME_BYTES?30:32);
        pos_anterior=p;reproducidos_=base_salida+vueltas+p;
      }
      if(!trabajo && !error)WaitForSingleObject(disponible,20); // plazo solo para atender stop, no reloj de audio
    }
    waveOutReset(wo);
    for(auto& b:bloques)if(b.preparado)waveOutUnprepareHeader(wo,&b.cabecera,sizeof(WAVEHDR));
    waveOutClose(wo);CloseHandle(disponible);error_=error;
  }
#endif
  Mezclador mezcla_;std::atomic<Proveedor> proveedor_{nullptr};
  std::atomic<uint64_t> reproducidos_{0};
  std::mutex inicio_,ajustes_;std::jthread hilo_;std::atomic<uint32_t> error_{0};bool automatico_=true;
};
inline Reproductor& Salida(){static Reproductor s;return s;}
} // namespace dbz3::iwsonido

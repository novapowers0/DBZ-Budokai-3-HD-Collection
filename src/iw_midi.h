// CapsulePick IW: IECSidiM + banco HSYN sin comprimir (gorP/tesS/lpmS/igaV).
// Fuentes: MODMIDI1260/C90/C20/1860/1D50, MODHSYN6C8C/41CC/458C/4A24.
// Ningun preset/muestra del juego se almacena aqui. C++23 por iw_sonido.
#pragma once
#include "iw_iop.h"
#include <array>
#include <concepts>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace dbz3::iwmidi {
using Bytes=std::vector<uint8_t>;
inline constexpr uint32_t kPulsoIOPUs=0x1047; // AMSOUND31F8 -> MODMIDI.Init(...,4167), ~240Hz.
inline void Rango(const Bytes& b,size_t p,size_t n) {
  if(p>b.size() || n>b.size()-p)throw std::runtime_error("MIDI IW: datos truncados");
}
inline uint32_t U(const Bytes& b,size_t p) {Rango(b,p,4);return iwiop::U(b,p);}
inline uint16_t H(const Bytes& b,size_t p) {Rango(b,p,2);return uint16_t(b[p]|uint16_t(b[p+1])<<8);}
inline size_t Chunk(const Bytes& b,const char* s) {
  const auto p=iwiop::Chunk(b,s);if(p==b.size())throw std::runtime_error(std::string("MIDI IW: falta ")+s);
  Rango(b,p,U(b,p+8));return p;
}
struct Evento {
  uint64_t muestra=0;uint32_t tick=0;Bytes bytes;
};
struct Partitura {
  std::vector<Evento> eventos;
  uint64_t fin=0;
  uint32_t division=0;
};
// El bit80 de un dato marca delta cero. Los note-off SOLO llevan tecla.
// El running status tampoco consume un delta extra (MODMIDI1260/C90).
inline Partitura Leer(const Bytes& b,uint32_t pista=0) {
  const size_t seq=Chunk(b,"IECSuqeS"),midi=Chunk(b,"IECSidiM");
  if(U(b,seq+20)!=midi || pista>U(b,midi+12))throw std::runtime_error("MIDI IW: pista fuera del banco");
  const uint32_t rel=U(b,midi+16+4*size_t(pista));
  if(rel==0xffffffff)throw std::runtime_error("MIDI IW: pista ausente");
  const size_t header=midi+rel;
  if(U(b,header)!=6)throw std::runtime_error("MIDI IW: tabla MIDI de referencias aun no soportada");
  Partitura out;out.division=H(b,header+4);
  if(!out.division || (out.division&0x8000))throw std::runtime_error("MIDI IW: division no PPQ");
  const size_t fin=midi+U(b,midi+8);size_t pos=header+6;
  uint8_t status=0;bool sin_delta=false,terminado=false;
  uint32_t ticks=0,tempo=500000,periodo=((tempo<<7)/out.division);
  uint64_t reloj=0;uint16_t canales=0;
  auto byte=[&]() {if(pos>=fin)throw std::runtime_error("MIDI IW: flujo sin fin de pista");return b[pos++];};
  auto variable=[&]() {uint32_t n=0;for(unsigned i=0;i<4;++i){const auto v=byte();n=(n<<7)|(v&127);if(!(v&128))return n;}throw std::runtime_error("MIDI IW: VLQ excesivo");};
  auto agregar=[&](Bytes datos) {
    // Periodos us<<7.1860 procesa ANTES de avanzar el reloj. Origen del
    // primer pulso=0; no redondear el IOP a1ms ni los4167us a200 muestras.
    const uint64_t q=uint64_t(kPulsoIOPUs)<<7;
    const uint64_t pulso=reloj ? (reloj+q-1)/q-1 : 0;
    out.eventos.push_back({pulso*kPulsoIOPUs*48000/1000000,ticks,std::move(datos)});
  };
  while(!terminado) {
    const uint32_t delta=sin_delta?0:variable();sin_delta=false;
    if(uint64_t(ticks)+delta>0xffffffffull)throw std::runtime_error("MIDI IW: reloj de pista desbordado");
    ticks+=delta;reloj+=uint64_t(delta)*periodo;
    uint8_t op=byte(),a=0;
    if(op&128) {if(op<0xf0){status=op;a=byte();}}
    else {if(!status)throw std::runtime_error("MIDI IW: running status sin inicializar");a=op;op=status;}
    if(op==0xff) {
      const uint8_t meta=byte();const uint32_t n=variable();
      if(n>fin-pos)throw std::runtime_error("MIDI IW: meta truncado");
      if(meta==0x51) {
        if(n!=3)throw std::runtime_error("MIDI IW: tempo invalido");
        tempo=uint32_t(b[pos])<<16|uint32_t(b[pos+1])<<8|b[pos+2];periodo=(tempo<<7)/out.division;
        if(!periodo)throw std::runtime_error("MIDI IW: periodo cero");
      }
      if(meta==0x2f){if(n)throw std::runtime_error("MIDI IW: fin invalido");terminado=true;}
      pos+=n;
    } else if(op<0xf0) {
      const uint8_t tipo=op&0xf0;uint8_t v=0;
      if(tipo!=0x80 && tipo!=0xc0 && tipo!=0xd0)v=byte();
      sin_delta=((a|v)&128)!=0;canales|=uint16_t(1u<<(op&15));
      if(tipo==0x80 || tipo==0xc0 || tipo==0xd0)agregar({op,uint8_t(a&127)});
      else if(tipo==0x90 || tipo==0xa0 || tipo==0xb0 || tipo==0xe0)agregar({op,uint8_t(a&127),uint8_t(v&127)});
      else throw std::runtime_error("MIDI IW: evento sin consumidor");
    } else throw std::runtime_error("MIDI IW: SysEx no soportado por CapsulePick");
  }
  // MODMIDI17AC: pedal OFF y all-notes-off para cada canal usado al acabar.
  for(uint8_t c=0;c<16;++c)if(canales&(1u<<c)){agregar({uint8_t(0xb0|c),64,0});agregar({uint8_t(0xb0|c),123,0});}
  out.fin=out.eventos.empty()?0:out.eventos.back().muestra;
  return out;
}

struct Onda {std::vector<int16_t> mono;uint32_t factor=0;};
struct Nota {
  size_t onda=0;uint32_t pitch=0;uint16_t adsr1=0,adsr2=0,volumen=0;
  uint8_t rutas=0;unsigned core=0;
};
class Banco {
 public:
  Bytes indice;
  std::vector<Onda> ondas;
  void Cargar(const Bytes& body,const Bytes& ind) {
    const auto vi=Chunk(ind,"IECSigaV");
    Chunk(ind,"IECSlpmS");Chunk(ind,"IECStesS");Chunk(ind,"IECSgorP");
    std::vector<Onda> nuevas;
    const auto max=U(ind,vi+12);if(max>127)throw std::runtime_error("MIDI IW: banco VAG fuera del formato");
    for(uint32_t i=0;i<=max;++i) {
      const size_t rec=vi+U(ind,vi+16+4*size_t(i));const size_t inicio=U(ind,rec);const uint32_t hz=H(ind,rec+4);
      if(!hz)throw std::runtime_error("MIDI IW: VAG sin frecuencia");
      Onda o;o.factor=uint32_t(uint64_t(hz)*65536/48000);
      int s1=0,s2=0;bool fin=false;
      static constexpr int k[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
      for(size_t p=inicio;p+16<=body.size();p+=16) {
        const unsigned filtro=body[p]>>4,shift=body[p]&15;
        if(filtro>4 || shift>12 || (body[p+1]&6))throw std::runtime_error("MIDI IW: predictor o bucle VAG no soportado");
        for(size_t j=2;j<16;++j)for(int n:{body[p+j]&15,body[p+j]>>4}) {
          const int signed_n=n<8?n:n-16;
          int x=(signed_n*4096)>>shift;x=std::clamp(x+((s1*k[filtro][0]+s2*k[filtro][1]+32)>>6),-32768,32767);
          s2=s1;s1=x;o.mono.push_back(int16_t(x));
        }
        if(body[p+1]&1){fin=true;break;}
      }
      if(!fin)throw std::runtime_error("MIDI IW: VAG truncado");nuevas.push_back(std::move(o));
    }
    indice=ind;ondas=std::move(nuevas);
  }
  Nota Resolver(unsigned programa,unsigned tecla,unsigned velocidad) const {
    const auto pr=Chunk(indice,"IECSgorP"),ss=Chunk(indice,"IECStesS"),sp=Chunk(indice,"IECSlpmS");
    auto registro=[&](size_t c,unsigned id){if(id>U(indice,c+12))throw std::runtime_error("MIDI IW: indice HSYN fuera de banco");const auto r=U(indice,c+16+4*size_t(id));if(r==0xffffffff)throw std::runtime_error("MIDI IW: registro HSYN ausente");return c+r;};
    const size_t p=registro(pr,programa);Rango(indice,p,36);
    // Este consumidor cubre las capas/curvas del banco de CapsulePick. Otra
    // capa o modulacion se rechaza, no se aproxima con otro instrumento.
    if(indice[p+4]!=1 || indice[p+5]!=20)throw std::runtime_error("MIDI IW: programa multicapa no soportado");
    const size_t t=p+U(indice,p);Rango(indice,t,20);
    if(tecla<(indice[t+2]&127) || tecla>(indice[t+4]&127))throw std::runtime_error("MIDI IW: tecla fuera de capa");
    const size_t set=registro(ss,H(indice,t));Rango(indice,set,6);
    if(indice[set] || velocidad<indice[set+1] || velocidad>indice[set+2] || indice[set+3]!=1)
      throw std::runtime_error("MIDI IW: curva o sampleset no soportado");
    const size_t s=registro(sp,H(indice,set+4));Rango(indice,s,42);
    if(tecla!=(indice[s+11]&127) || indice[s+12] || indice[p+8] || indice[p+9] || indice[t+18] || indice[t+19] || indice[s+40])
      throw std::runtime_error("MIDI IW: afinacion o modulacion no soportada");
    if(indice[p+7]!=64 || indice[t+17]!=64 || indice[s+13]!=64)throw std::runtime_error("MIDI IW: panorama de banco no central");
    Nota n;n.onda=H(indice,s);if(n.onda>=ondas.size())throw std::runtime_error("MIDI IW: VAG ausente");
    n.pitch=std::min(0x3fffu,uint32_t(uint64_t(4096)*ondas[n.onda].factor>>16));
    n.adsr1=H(indice,s+18);n.adsr2=H(indice,s+20);
    // HSYN41CC/43B4..440C: curvas identidad del banco, LFO unidad52D0.
    n.volumen=uint16_t((uint32_t(indice[p+6])*indice[t+16]*indice[s+16]*velocidad)>>14);
    n.rutas=indice[s+41]&15;n.core=0; // las <=5 voces caben en core0 (IRX725C).
    if(!n.pitch || !n.rutas)throw std::runtime_error("MIDI IW: voz sin salida valida");return n;
  }
};

struct Voz {
  Nota nota;uint8_t canal=0,tecla=0;uint64_t fase=0;iwiop::Envolvente env;bool viva=true;
};
class Sintetizador {
 public:
  const Banco* banco=nullptr;std::vector<Voz> voces;std::array<uint8_t,16> programas{};
  void EventoMidi(const Bytes& b) {
    if(!banco || b.size()<2)throw std::runtime_error("MIDI IW: sintetizador no preparado");
    const uint8_t tipo=b[0]&0xf0,c=b[0]&15;
    if(tipo==0xc0){programas[c]=b[1];return;}
    if(tipo==0x90 && b.size()==3 && b[2]) {voces.push_back({banco->Resolver(programas[c],b[1],b[2]),c,b[1]});return;}
    if(tipo==0x80 || (tipo==0x90 && b.size()==3 && !b[2])) {for(auto& v:voces)if(v.canal==c && v.tecla==b[1])v.env.Apagar();return;}
    if(tipo==0xb0 && b.size()==3) {
      if((b[1]==7 && b[2]==127)||(b[1]==10 && b[2]==64)||(b[1]==64 && !b[2]))return;
      if(b[1]==123 && !b[2]){for(auto& v:voces)if(v.canal==c)v.env.Apagar();return;}
    }
    if(tipo==0xe0 && b.size()==3 && b[1]==0 && b[2]==64)return;
    throw std::runtime_error("MIDI IW: controlador no soportado en CapsulePick");
  }
  void Apagar(){for(auto& v:voces)v.env.Apagar();}
  std::array<int16_t,2> Muestra() {
    int32_t suma=0;
    for(auto& v:voces)if(v.viva) {
      const auto& o=banco->ondas[v.nota.onda];
      if(v.fase/4096>=o.mono.size()){v.viva=false;continue;}
      v.env.Paso(v.nota.adsr1,v.nota.adsr2);
      const int muestra=iwiop::Interpolar(o.mono,v.fase);
      const int adsr=int((int64_t(muestra)*v.env.nivel)>>15);
      suma+=int((int64_t(adsr)*v.nota.volumen)>>14);v.fase+=v.nota.pitch;
      if(v.env.etapa==3 && v.env.nivel==0)v.viva=false;
    }
    const auto sample=int16_t(std::clamp(suma,-32768,32767));return {sample,sample};
  }
  bool Viva()const {for(const auto& v:voces)if(v.viva)return true;return false;}
};
struct Audio {
  std::vector<int16_t> pcm;Partitura plan;uint8_t rutas=0;unsigned core=0;
};
inline Audio Renderizar(const Banco& banco,Partitura plan,uint64_t corte=std::numeric_limits<uint64_t>::max()) {
  Sintetizador sin;sin.banco=&banco;Audio out;out.plan=std::move(plan);size_t i=0;
  uint64_t frame=0;
  for(;;++frame) {
    if(frame==corte){sin.Apagar();i=out.plan.eventos.size();}
    while(i<out.plan.eventos.size() && out.plan.eventos[i].muestra<=frame)sin.EventoMidi(out.plan.eventos[i++].bytes);
    if(i==out.plan.eventos.size() && !sin.Viva())break;
    for(const auto& v:sin.voces) {
      if(out.rutas && (out.rutas!=v.nota.rutas || out.core!=v.nota.core))throw std::runtime_error("MIDI IW: rutas heterogeneas requieren buses separados");
      out.rutas=v.nota.rutas;out.core=v.nota.core;
    }
    if(frame>uint64_t(out.pcm.max_size()/2)-1)throw std::runtime_error("MIDI IW: salida demasiado grande");
    const auto muestra=sin.Muestra();out.pcm.push_back(muestra[0]);out.pcm.push_back(muestra[1]);
  }
  if(corte==std::numeric_limits<uint64_t>::max() && (out.pcm.empty() || !out.rutas))throw std::runtime_error("MIDI IW: secuencia sin voz consumible");
  return out;
}

// El Reproductor debe vivir mas que esta sesion. Solo se cancelan handles y
// generaciones propios; nunca Limpiar()/Maestro() del mezclador compartido.
class Sesion {
 public:
  explicit Sesion(iwsonido::Reproductor& salida=iwsonido::Salida(),uint64_t slot=0x49574d4944490000ull)
      : salida_(salida),slot_(slot) {}
  ~Sesion(){Abortar();}
  Sesion(const Sesion&)=delete;Sesion& operator=(const Sesion&)=delete;
  const std::string& Error()const{return error_;}
  bool Preparada()const{return preparada_;}
  iwsonido::Handle Handle()const{return handle_;}
  const Audio& Datos()const{return audio_;}
  const Banco& Instrumentos()const{return banco_;}
  std::function<bool(iwsonido::Handle,uint64_t&)> posicion_audio;
  bool Preparar(const Bytes& cuerpo,const Bytes& indice,const Bytes& secuencia) {
    try {
      Banco b;b.Cargar(cuerpo,indice);auto audio=Renderizar(b,Leer(secuencia));
      Abortar();banco_=std::move(b);audio_=std::move(audio);preparada_=true;error_.clear();return true;
    }catch(const std::exception& e){error_=e.what();return false;}
  }
  bool Rpc13(uint32_t id) {
    if(!preparada_ || id!=0)return Fallar("RPC13: banco/pista de capsulas no preparado");
    iwsonido::Opciones o;o.canal=salida_.Mezcla().Slot(slot_);o.sustituir=true;o.rutas=audio_.rutas;o.core=audio_.core;
    const auto nuevo=salida_.Enviar(audio_.pcm,48000,o);
    if(!nuevo)return Fallar("RPC13: el mezclador rechazo la salida PCM");
    if(handle_)salida_.Mezcla().Parar(handle_);
    handle_=nuevo;tocando_=true;error_.clear();return true;
  }
  // Corte con un cursor de AUDIO conocido. Replay hasta ese punto conserva
  // fase/ADSR y produce la cola KeyOff original. Nunca usar reloj de ventana.
  bool Rpc14En(uint32_t id,uint64_t frame) {
    if(id!=0 && id!=0xffffffff)return Fallar("RPC14: slot ajeno a CapsulePick");
    if(!handle_ || !tocando_)return true;
    try {
      std::vector<int16_t> cola;
      if(frame<audio_.pcm.size()/2) {
        auto cortado=Renderizar(banco_,audio_.plan,frame);
        const auto inicio=std::min<uint64_t>(frame*2,cortado.pcm.size());
        cola.assign(cortado.pcm.begin()+size_t(inicio),cortado.pcm.end());
      }
      Abortar();
      if(!cola.empty()) {
        iwsonido::Opciones o;o.canal=salida_.Mezcla().Slot(slot_);o.rutas=audio_.rutas;o.core=audio_.core;
        handle_=salida_.Enviar(cola,48000,o);
        if(!handle_)return Fallar("RPC14: mezclador rechazo cola ADSR");
      }
      error_.clear();return true;
    }catch(const std::exception& e){return Fallar(e.what());}
  }
  bool Rpc14(uint32_t id) {
    if(id!=0 && id!=0xffffffff)return Fallar("RPC14: slot ajeno a CapsulePick");
    if(!handle_ || !tocando_)return id==0 || id==0xffffffff;
    uint64_t frame=0;
    const bool cursor=posicion_audio?posicion_audio(handle_,frame):Cursor(salida_.Mezcla(),handle_,frame);
    if(!cursor && !posicion_audio && TieneCursor<iwsonido::Mezclador>()) {
      // El handle es nuestro y fue aceptado. Una API presente que ya no lo
      // encuentra indica voz finalizada/cancelada: el stop nativo es no-op.
      Abortar();error_.clear();return true;
    }
    if(!cursor)return Fallar("RPC14 necesita API Posicion(handle,frames) del mezclador; no se inventa cursor");
    return Rpc14En(id,frame);
  }
  void Abortar() {
    if(handle_){salida_.Mezcla().Parar(handle_);handle_=0;}tocando_=false;
  }
 private:
  template<class M>static bool Cursor(M& m,iwsonido::Handle h,uint64_t& frame) {
    if constexpr(requires{{m.Posicion(h,frame)}->std::same_as<bool>;})return m.Posicion(h,frame);
    else return false;
  }
  template<class M>static constexpr bool TieneCursor() {return requires(M& m,iwsonido::Handle h,uint64_t& p){{m.Posicion(h,p)}->std::same_as<bool>;};}
  bool Fallar(const std::string& e){error_=e;return false;}
  iwsonido::Reproductor& salida_;uint64_t slot_;iwsonido::Handle handle_=0;
  Banco banco_;Audio audio_;bool preparada_=false,tocando_=false;std::string error_;
};
} // namespace dbz3::iwmidi

// Banco de efectos IOP de IW. MODSESQ/MODHSYN: resolver secuencia/programa/
// tecla ANTES de seleccionar el VAG; el id de sonido no es el id de muestra.
#pragma once
#include "pasarela.h"
#include "launcher/settings.h"
#include <rex/logging.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>
#include "iw_sonido.h"
#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#endif
namespace dbz3::iwiop {
// Memory(n), EE EU/USA127CA0. El propietario del contexto debe compartir ESTA
// instancia con AME/juego y restaurar la semilla al cargar estado; no hay semilla
// privada de audio ni inicializacion supuesta a1. El integrador aporta el estado.
struct Azar {
  uint32_t estado;
  explicit Azar(uint32_t semilla):estado(semilla) {}
  uint32_t Cambiar(uint32_t semilla) {const auto anterior=estado;estado=semilla;return anterior;}
  uint32_t Memory(int bits) {
    for(int i=0;i<bits;++i)estado=(estado<<1)|(((estado>>31)^(estado>>4))&1);
    return estado-1;
  }
};
// ROM de interpolacion del SPU/SPU2 (datos de hardware, psx-spx, seccion
// "4-Point Gaussian Interpolation"). No sustituir por una curva gaussiana
// analitica: sus redondeos y suma7F80 son parte del filtro original.
inline constexpr int16_t kGauss[512]={
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 0,0,0,0,0,0,0,1,1,1,1,2,2,2,3,3,
 3,4,4,5,5,6,7,7,8,9,9,10,11,12,13,14,
 15,16,17,18,19,21,22,24,25,27,28,30,32,33,35,37,
 0x27,0x29,0x2c,0x2e,0x30,0x33,0x35,0x38,0x3a,0x3d,0x40,0x43,0x46,0x49,0x4d,0x50,
 0x54,0x57,0x5b,0x5f,0x63,0x67,0x6b,0x6f,0x74,0x78,0x7d,0x82,0x87,0x8c,0x91,0x96,
 0x9c,0xa1,0xa7,0xad,0xb3,0xba,0xc0,0xc7,0xcd,0xd4,0xdb,0xe3,0xea,0xf2,0xfa,0x101,
 0x10a,0x112,0x11b,0x123,0x12c,0x135,0x13f,0x148,0x152,0x15c,0x166,0x171,0x17b,0x186,0x191,0x19c,
 0x1a8,0x1b4,0x1c0,0x1cc,0x1d9,0x1e5,0x1f2,0x200,0x20d,0x21b,0x229,0x237,0x246,0x255,0x264,0x273,
 0x283,0x293,0x2a3,0x2b4,0x2c4,0x2d6,0x2e7,0x2f9,0x30b,0x31d,0x330,0x343,0x356,0x36a,0x37e,0x392,
 0x3a7,0x3bc,0x3d1,0x3e7,0x3fc,0x413,0x42a,0x441,0x458,0x470,0x488,0x4a0,0x4b9,0x4d2,0x4ec,0x506,
 0x520,0x53b,0x556,0x572,0x58e,0x5aa,0x5c7,0x5e4,0x601,0x61f,0x63e,0x65c,0x67c,0x69b,0x6bb,0x6dc,
 0x6fd,0x71e,0x740,0x762,0x784,0x7a7,0x7cb,0x7ef,0x813,0x838,0x85d,0x883,0x8a9,0x8d0,0x8f7,0x91e,
 0x946,0x96f,0x998,0x9c1,0x9eb,0xa16,0xa40,0xa6c,0xa98,0xac4,0xaf1,0xb1e,0xb4c,0xb7a,0xba9,0xbd8,
 0xc07,0xc38,0xc68,0xc99,0xccb,0xcfd,0xd30,0xd63,0xd97,0xdcb,0xe00,0xe35,0xe6b,0xea1,0xed7,0xf0f,
 0xf46,0xf7f,0xfb7,0xff1,0x102a,0x1065,0x109f,0x10db,0x1116,0x1153,0x118f,0x11cd,0x120b,0x1249,0x1288,0x12c7,
 0x1307,0x1347,0x1388,0x13c9,0x140b,0x144d,0x1490,0x14d4,0x1517,0x155c,0x15a0,0x15e6,0x162c,0x1672,0x16b9,0x1700,
 0x1747,0x1790,0x17d8,0x1821,0x186b,0x18b5,0x1900,0x194b,0x1996,0x19e2,0x1a2e,0x1a7b,0x1ac8,0x1b16,0x1b64,0x1bb3,
 0x1c02,0x1c51,0x1ca1,0x1cf1,0x1d42,0x1d93,0x1de5,0x1e37,0x1e89,0x1edc,0x1f2f,0x1f82,0x1fd6,0x202a,0x207f,0x20d4,
 0x2129,0x217f,0x21d5,0x222c,0x2282,0x22da,0x2331,0x2389,0x23e1,0x2439,0x2492,0x24eb,0x2545,0x259e,0x25f8,0x2653,
 0x26ad,0x2708,0x2763,0x27be,0x281a,0x2876,0x28d2,0x292e,0x298b,0x29e7,0x2a44,0x2aa1,0x2aff,0x2b5c,0x2bba,0x2c18,
 0x2c76,0x2cd4,0x2d33,0x2d91,0x2df0,0x2e4f,0x2eae,0x2f0d,0x2f6c,0x2fcc,0x302b,0x308b,0x30ea,0x314a,0x31aa,0x3209,
 0x3269,0x32c9,0x3329,0x3389,0x33e9,0x3449,0x34a9,0x3509,0x3569,0x35c9,0x3629,0x3689,0x36e8,0x3748,0x37a8,0x3807,
 0x3867,0x38c6,0x3926,0x3985,0x39e4,0x3a43,0x3aa2,0x3b00,0x3b5f,0x3bbd,0x3c1b,0x3c79,0x3cd7,0x3d35,0x3d92,0x3def,
 0x3e4c,0x3ea9,0x3f05,0x3f62,0x3fbd,0x4019,0x4074,0x40d0,0x412a,0x4185,0x41df,0x4239,0x4292,0x42eb,0x4344,0x439c,
 0x43f4,0x444c,0x44a3,0x44fa,0x4550,0x45a6,0x45fc,0x4651,0x46a6,0x46fa,0x474e,0x47a1,0x47f4,0x4846,0x4898,0x48e9,
 0x493a,0x498a,0x49d9,0x4a29,0x4a77,0x4ac5,0x4b13,0x4b5f,0x4bac,0x4bf7,0x4c42,0x4c8d,0x4cd7,0x4d20,0x4d68,0x4db0,
 0x4df7,0x4e3e,0x4e84,0x4ec9,0x4f0e,0x4f52,0x4f95,0x4fd7,0x5019,0x505a,0x509a,0x50da,0x5118,0x5156,0x5194,0x51d0,
 0x520c,0x5247,0x5281,0x52ba,0x52f3,0x532a,0x5361,0x5397,0x53cc,0x5401,0x5434,0x5467,0x5499,0x54ca,0x54fa,0x5529,
 0x5558,0x5585,0x55b2,0x55de,0x5609,0x5632,0x565b,0x5684,0x56ab,0x56d1,0x56f6,0x571b,0x573e,0x5761,0x5782,0x57a3,
 0x57c3,0x57e2,0x57ff,0x581c,0x5838,0x5853,0x586d,0x5886,0x589e,0x58b5,0x58cb,0x58e0,0x58f4,0x5907,0x5919,0x592a,
 0x593a,0x5949,0x5958,0x5965,0x5971,0x597c,0x5986,0x598f,0x5997,0x599e,0x59a4,0x59a9,0x59ad,0x59b0,0x59b2,0x59b3
};
inline int Interpolar(const std::vector<int16_t>& mono,uint64_t fase) {
  const size_t i=size_t(fase>>12);const unsigned f=unsigned(fase>>4)&255;
  auto m=[&](size_t retraso){return i>=retraso&&i-retraso<mono.size()?int(mono[i-retraso]):0;};
  return std::clamp(((kGauss[255-f]*m(3))>>15)+((kGauss[511-f]*m(2))>>15)+
                    ((kGauss[256+f]*m(1))>>15)+((kGauss[f]*m(0))>>15),-32768,32767);
}
inline uint32_t U(const std::vector<uint8_t>& b,size_t o) {
  uint32_t v=0;if(o+4<=b.size()) memcpy(&v,b.data()+o,4);return v;
}
// La semilla inicial es dato del ELF, NO1. USA: .reginfo GP4A35F0,
// GP-7FE8=49B608 contiene87654321. EU usa otro GP; no trasladar la direccion.
inline bool LeerSemillaAzar(const std::vector<uint8_t>& elf,uint32_t& semilla) {
  auto rango=[&](size_t p,size_t n){return p<=elf.size() && n<=elf.size()-p;};
  if(!rango(0,52) || U(elf,0)!=0x464C457F || elf[4]!=1 || elf[5]!=1)return false;
  const size_t sh=U(elf,32),ss=U(elf,44)>>16,sn=U(elf,48)&65535;
  if(ss<40 || !rango(sh,ss*sn))return false;
  uint32_t gp=0;
  for(size_t i=0;i<sn;++i) {
    const size_t p=sh+i*ss;
    if(U(elf,p+4)!=0x70000006)continue; // SHT_MIPS_REGINFO
    const size_t off=U(elf,p+16);
    if(U(elf,p+20)<24 || !rango(off,24))return false;
    gp=U(elf,off+20);break;
  }
  if(gp<0x7FE8)return false;
  const uint32_t direccion=gp-0x7FE8;
  const size_t ph=U(elf,28),ps=U(elf,40)>>16,pn=U(elf,44)&65535;
  if(ps<32 || !rango(ph,ps*pn))return false;
  for(size_t i=0;i<pn;++i) {
    const size_t p=ph+i*ps;
    if(U(elf,p)!=1 || direccion<U(elf,p+8))continue;
    const size_t delta=size_t(direccion-U(elf,p+8)),bytes=U(elf,p+16);
    if(delta>bytes || bytes-delta<4)continue;
    const size_t off=size_t(U(elf,p+4))+delta;
    if(!rango(off,4))return false;
    semilla=U(elf,off);return true;
  }
  return false;
}
inline size_t Chunk(const std::vector<uint8_t>& b,const char* nombre) {
  for(size_t i=0;i+16<=b.size();i+=4) if(!memcmp(b.data()+i,nombre,8)) return i;
  return b.size();
}
struct Voz {
  std::vector<int16_t> pcm;
  uint32_t muestra=0,pitch=0,adsr1=0,adsr2=0;
  float ganancia=0;
  uint8_t rutas=3; // tono+0D: secoL/R y envioL/R, MODHSYN5D5C..5F2C
  bool silencio=false; // cero original, distinto de fallo de lectura/formato
  bool secuencia_ausente=false; // MODSESQ no emite si id>max o punteroFFFFFFFF
  uint32_t apagado=0; // cuadro de audio48k del A0 velocidad0;0=sin note-off
};
// Registros ADSR SPU2: nivel15bits y acumulador de velocidad15bits a48k.
struct Envolvente {
  int etapa=0,nivel=0;
  uint32_t reloj=0;
  void Apagar() {if(etapa<3){etapa=3;reloj=0;}}
  float Paso(uint16_t a,uint16_t b) {
    int shift=0,paso=0;bool baja=false,exp=false;
    if(etapa==0) {shift=(a>>10)&31;paso=7-((a>>8)&3);exp=(a&0x8000)!=0;}
    else if(etapa==1) {shift=(a>>4)&15;paso=-8;baja=exp=true;}
    else if(etapa==2) {
      shift=(b>>8)&31;baja=(b&0x4000)!=0;exp=(b&0x8000)!=0;
      paso=7-((b>>6)&3);if(baja) paso=-(paso+1);
    } else {
      shift=b&31;paso=-8;baja=true;exp=(b&32)!=0;
    }
    uint32_t avance=0x8000u>>std::max(0,shift-11);
    int salto=paso*(1<<std::max(0,11-shift));
    const bool fijo=(etapa==0 && ((a>>8)&127)==127) ||
                    (etapa==2 && ((b>>6)&127)==127) || (etapa==3 && (b&31)==31);
    if(exp&&baja) salto=int16_t((int64_t(salto)*nivel)>>15);
    else if(exp&&nivel>0x6000) {
      if(shift<10)salto>>=2;
      else if(shift>=11)avance>>=2;
      else {salto>>=1;avance>>=1;}
    }
    reloj+=fijo?avance:std::max(1u,avance);
    if(reloj>=0x8000) {reloj=0;nivel=std::clamp(nivel+salto,0,32767);}
    if(etapa==0&&nivel==32767) etapa=1;
    else if(etapa==1&&nivel<=((a&15)+1)*2048) etapa=2;
    return float(nivel)/32768;
  }
};
inline Voz Leer(const std::vector<uint8_t>& amb,int sonido) {
  Voz v;
  const auto body=pasarela::HijoAmb(amb,0),indice=pasarela::HijoAmb(amb,1),seq=pasarela::HijoAmb(amb,2);
  const size_t sc=Chunk(seq,"IECSqseS"),vi=Chunk(indice,"IECSigaV"),sb=Chunk(indice,"IECSbteS");
  if(sc==seq.size()||vi==indice.size()||sb==indice.size()||sonido<0) return v;
  const size_t grupo=sc+U(seq,sc+0x20),tabla=grupo+4+U(seq,grupo+4);
  // Validar la tabla ANTES de distinguir ausencia original de banco truncado.
  if(grupo+16>seq.size() || U(seq,grupo)>255 || tabla>seq.size() ||
     (size_t(U(seq,grupo))+1)*4>seq.size()-tabla) return v;
  // MODSESQ21F8: 2308..2318 y2334..2350; devuelve-1 sin emitir.
  if(uint32_t(sonido)>U(seq,grupo) || U(seq,tabla+4*size_t(sonido))==0xFFFFFFFFu) {
    v.silencio=v.secuencia_ausente=true;return v;
  }
  const size_t s=sc+U(seq,tabla+4*size_t(sonido)),flujo=s+U(seq,s);
  if(s+16>seq.size() || U(seq,s)==0xFFFFFFFFu) return v;
  const size_t bytes=U(seq,s+8);
  if((bytes!=7 && bytes!=13) || flujo>seq.size() || bytes>seq.size()-flujo) return v;
  // Solo una nota a delta0, opcional A0(prog,key,vel0) y finFF. MODHSYN2D38/
  // 2E40 convierte A0 velocidad0 en key-off, NO en otra muestra one-shot.
  const uint8_t* ev=seq.data()+flujo;
  if(ev[0]||ev[1]!=0x90) return v;
  if(bytes==7) {if(ev[5]||ev[6]!=0xFF)return v;}
  else {
    if(!(ev[5]&128) || (ev[6]&128) || ev[7]!=0xA0 || ev[8]!=ev[2] || ev[9]!=ev[3] ||
       ev[10] || ev[11] || ev[12]!=0xFF)return v;
    // MODSESQ7F4: seis factores1000 ->128000000/1000=periodo128000,
    // acumulador en microsegundos<<7: un tick=1000us, no negra MIDI.
    if((U(seq,sc+20)>>16)!=1000 || (U(seq,grupo+8)>>16)!=1000 || (U(seq,s+4)>>16)!=1000)return v;
    v.apagado=uint32_t(((ev[5]&127)<<7)|ev[6])*48;
    if(!v.apagado)return v;
  }
  const int programa=ev[2],tecla=ev[3],velocidad=ev[4];
  if(velocidad==0 || seq[s+4]==0) {v.silencio=true;return v;}
  const size_t banco=sb+U(indice,sb+0x10),prog=sb+U(indice,banco+4+4*size_t(programa));
  if(prog+8>indice.size()||tecla<indice[prog+5]||tecla>indice[prog+6]) return v;
  const size_t tono=prog+U(indice,prog)+size_t(indice[prog+4])*(tecla-indice[prog+5]);
  if(tono+50>indice.size()) return v;
  // Solo la curva identidad y el centro afinado estan resueltos en este banco.
  if(indice[tono+2]||indice[tono+4]!=64||indice[tono+5]||indice[tono+6]||seq[s+4]!=127||seq[s+5]!=64) return v;
  v.muestra=U(indice,tono)&0xFFFF;
  if(v.muestra>U(indice,vi+12)||vi+16+4*v.muestra+4>indice.size()) return Voz{};
  const size_t rec=vi+U(indice,vi+16+4*v.muestra),inicio=U(indice,rec);
  const uint32_t hz=U(indice,rec+4)&0xFFFF;
  if(inicio>=body.size()||!hz) return Voz{};
  v.pitch=uint32_t((4096ull*uint32_t(double(hz)*65536/48000))>>16);
  v.pitch=std::min(v.pitch,0x3FFFu); // limite del registro SPU2, MODHSYN4BE8
  v.ganancia=float(velocidad*int(indice[tono+3]))/16384;
  v.rutas=indice[tono+13]&15;
  v.adsr1=U(indice,tono+14)&0xFFFF;v.adsr2=U(indice,tono+16)&0xFFFF;
  static constexpr int K[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
  int s1=0,s2=0;
  std::vector<int16_t> mono;
  for(size_t o=inicio;o+16<=body.size();o+=16) {
    const int filtro=body[o]>>4,shift=body[o]&15;
    if(filtro>4||shift>12) return Voz{};
    for(size_t j=2;j<16;++j) for(int nib:{body[o+j]&15,body[o+j]>>4}) {
      int x=int16_t(nib<<12)>>shift;
      x=std::clamp(x+((s1*K[filtro][0]+s2*K[filtro][1]+32)>>6),-32768,32767);
      s2=s1;s1=x;mono.push_back(int16_t(x));
    }
    if(body[o+1]&1) break;
  }
  // Salida seca48k, filtro ROM gaussiano de4puntos; reverb global pendiente.
  if(!v.pitch||mono.empty()) return Voz{};
  uint64_t fase=0;Envolvente envolvente;
  for(;fase/4096<mono.size();fase+=v.pitch) {
    if(v.apagado && v.pcm.size()/2>=v.apagado)envolvente.Apagar();
    const int seco=Interpolar(mono,fase);
    const float adsr=envolvente.Paso(uint16_t(v.adsr1),uint16_t(v.adsr2));
    const auto muestra=int16_t(std::clamp(int(seco*v.ganancia*adsr),-32768,32767));
    v.pcm.push_back(muestra);v.pcm.push_back(muestra); // pan64: misma gananciaL/R, sin upmix implicito
    if(envolvente.etapa==3 && !envolvente.nivel)break;
  }
  REXLOG_INFO("dbz3 iw IOP F: sonido {:#x} programa {} tecla {} -> VAG {:#x}, {} muestras fuente, pitch {:#x}, ganancia {:.9f}, ADSR {:04x}/{:04x} aplicado; PCM seco ROM4p; reverb depende del envio del llamador",
              sonido,programa,tecla,v.muestra,mono.size(),v.pitch,v.ganancia,v.adsr1,v.adsr2);
  return v;
}
// Los arneses historicos extraen solo el namespace del decoder (sin includes
// host). El arnes de salida incluye iw_sonido y habilita tambien esta interfaz.
#if !defined(DBZ3_IW_IOP_CORE) || defined(DBZ3_IW_AUDIO_F_SALIDA)
inline void ConfigurarSalidaLauncher() {
  iwsonido::Salida().UsarProveedor(+[]()->iwsonido::Ajustes {
    return {float(settings::MasterVolume()),settings::AudioMute()};
  });
}
inline iwsonido::Opciones OpcionesVoz(const Voz& voz,iwsonido::Opciones o) {
  if(o.rutas==0xff)o.rutas=voz.rutas;
  // Battle_InitSoundRegulator/MinigameMovements configuran3/050A en ambos
  // cores. Asegurar no reinicia una reverb ya viva al empezar otro sonido.
  if(o.rutas&12)iwsonido::Salida().Mezcla().AsegurarEfecto3();
  return o;
}
// API de integracion: volumen_maestro final (0..1), ganancia/rutas/core/slot de
// la fuente en Opciones. Devuelve handle individual; no se vuelve a multiplicar
// por MasterVolume. El llamador puede conservar el slot con Mezcla().Slot(actor).
inline iwsonido::Handle Oir(const Voz& voz,float volumen_maestro,iwsonido::Opciones o={}) {
  auto& s=iwsonido::Salida();s.UsarProveedor(nullptr);s.Mezcla().Maestro(volumen_maestro);
  return s.Enviar(voz.pcm,48000,OpcionesVoz(voz,std::move(o)));
}
// Compatibilidad: ya aplicaba MasterVolume antes de esta ronda. Ahora se lee
// por bloque, UNA vez tras la mezcla, tambien para la cola y para mute en vivo.
inline iwsonido::Handle Oir(const Voz& voz,iwsonido::Opciones opciones) {
  ConfigurarSalidaLauncher();return iwsonido::Salida().Enviar(voz.pcm,48000,OpcionesVoz(voz,std::move(opciones)));
}
inline iwsonido::Handle Oir(const Voz& voz) {
  ConfigurarSalidaLauncher();return iwsonido::Salida().Enviar(voz.pcm,48000,OpcionesVoz(voz,{}));
}
#endif
}

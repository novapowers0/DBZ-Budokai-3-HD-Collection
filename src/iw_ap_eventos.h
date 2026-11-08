// AP7 IW: contrato original, sin equivalencias numericas con B3.
// EU: Anim_ProcessEventTrack 23F520, Animations2 207180; USA: wrapper22FAE0,
// voces22FD30, Char_HandleAnimEvent2A84C0, MinigameMovementsAudioEffects302C30.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dbz3::anim::apev {
struct Consumidor {
  uint32_t funcion;
  const char* nombre;
  const char* efecto_sonido;
  const char* falta;
};
// Tabla COMPLETA del switch0..9; tabla completa no significa consumidores
// completos. El tipo10 presente en los datos NO tiene case en23F520.
inline constexpr std::array<Consumidor, 10> kConsumidores = {{
    {0x205660, "PlayerVsCpuGeneralScene3", "valor s16; solo escenas2/3/4/7; estados puros en Consumir0", "otros efectos requieren contexto/generador IW; no equivalencias B3"},
    {0x22FC50, "Audio_PlayHitSoundWrapper", "slot1; secuencia=valor s16 (>=1000: handle, -1000)", "secuenciador/handles si no es one-shot"},
    {0x22FC50, "Audio_PlayHitSoundWrapper", "slot=lado+2; banco de gritos del actor; valor s16", "actor requerido en clips comunes; handles"},
    {0x22FEA0, "GenericCharacterVoiceGruntingMidBattle", "slot=lado+2; filtro idioma, azar y forma; NO ADX directo", "azar/forma especial; boca y corte de la voz anterior"},
    {0x1BF610, "Super_Ult_Ki_AttackEffects", "AP4 pasa {actor,0}; Preparar4 conserva destino/registro y ruta IW", "ejecutar recursos de tecnicas/capsulas; pedido no equivale a efecto dibujado"},
    {0x22F090, "Battle_UpdateGroundHeightAndFilter", "modelo4DC=personajeB1C; siete u32 ->1FBE00 geometria/dispatch decimal", "generadores finales de la tabla IW; no usar el subtipo como AME/B3"},
    {0x2A8940, "Char_HandleAnimEvent", "primero de cola; lado0; material del suelo; valores1..4 visual,5/6 audio", "consumidor de campo conectado; resto de escenas pendiente"},
    {0x22FD20, "PlayHit_KiBlastsAudios3", "s16: <1000 ADX del perfil;1000..1999 pausa;>=2000 inicia(valor-2000) sin cortar", "transicion de forma pendiente; PCM JP/USA contrastado con FFmpeg; boca aparte"},
    {0x207180, "MouthAnimations", "BancoAMK/Estado/Motor: selectores reales y nueve registros128B", "cargar poses AMM reales y enlazar huesos del cuerpo; no indices de pose B3"},
    {0x299EE0, "Minigame_MovePlayerByStick", "modelo514=personajeB54; float(int32(valor))/10;0 usa0.4 en campo", "conectar EstadoExterno.VelocidadCampo al movimiento; no escribir offsets B3"},
}};
struct Campo {
  int valor, despacho;
  const char* ancla;
};
// EU2A8940,302390,3022D0: fase0/2(mini0) -> AMO3/4;
// fase1/2(otro mini) -> AMO9/A; fase3 no genera visual.
inline constexpr Campo kCampo[] = {{1,0,"X+700, y=suelo18D0"}, {2,0,"X+710, y=suelo18D0"},
                                   {3,1,"X+700, y=suelo18D0"}, {4,1,"X+710, y=suelo18D0"},
                                   {5,-1,"slot0 CMN938: sonido segun mini/superficie"}, {6,-1,"slot0 CMN938: sonido segun mini/superficie"}};

inline bool Rango(const std::vector<uint8_t>& b, size_t p, size_t n) {
  return p <= b.size() && n <= b.size()-p;
}
inline uint32_t U32(const std::vector<uint8_t>& b, size_t p) {
  if (!Rango(b,p,4)) return 0;
  return uint32_t(b[p]) | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24;
}
// Leer la tabla del ELF DEL JUGADOR, no repartirla. GroundMinigameCharacterProperties:
// USA2F52B0 usa473880, filas40B: 8CMN + banco SE + banco boca. Se localiza por
// sus primeras tres filas y la fila comun previa, para no asumir un delta EU.
inline int BancoActor(const std::vector<uint8_t>& elf, uint32_t cmn) {
  if (!Rango(elf,0,52) || U32(elf,0)!=0x464C457F || !cmn || cmn==204 || cmn==915) return -1;
  for (size_t p=32; Rango(elf,p,17*40); p+=4) {
    if (U32(elf,p-32)!=915 || U32(elf,p)!=922 || U32(elf,p+40)!=921 || U32(elf,p+80)!=920) continue;
    bool valida=true;
    for (size_t i=0;i<17;++i) {
      const uint32_t b=U32(elf,p+i*40+32), boca=U32(elf,p+i*40+36);
      if (b<1400 || b>1600 || boca+1!=b) { valida=false;break; }
    }
    if (!valida) continue;
    for (size_t i=0;i<17;++i)
      // Los almacenes secundarios (913/917) pertenecen al mismo actor.
      for (size_t a=0;a<8;++a)
        if (U32(elf,p+i*40+4*a)==cmn) return int(U32(elf,p+i*40+32));
    return -1;
  }
  return -1;
}
inline int BancoBoca(const std::vector<uint8_t>& elf,uint32_t cmn) {
  // BancoActor ya valida la columna boca+1==SE de todas las filas del ELF.
  const int se=BancoActor(elf,cmn);return se<0?-1:se-1;
}
enum class Ruta { Pendiente, SilencioOriginal, Cmn, Voz, Adx, PausaAdx };
struct Audio {
  Ruta ruta=Ruta::Pendiente;
  int banco=-1, sonido=-1;
  const char* estado="sin consumidor verificado";
  bool solapar=false;
};
inline Audio Resolver(uint32_t tipo, uint32_t valor, int banco_actor, bool japones) {
  int v=int16_t(uint16_t(valor)); // colas originales1/2/3/7/8 son s16
  if (tipo==1 || tipo==2) {
    if (v<0) return {Ruta::SilencioOriginal,-1,v,"id negativo: original no reproduce"};
    if (v>=1000) return {Ruta::Pendiente,-1,v-1000,"requiere handle/control original"};
    if (tipo==1) return {Ruta::Cmn,954,v,"secuencia del banco comun de minijuegos"};
    if (banco_actor>=0) return {Ruta::Voz,banco_actor,v,"secuencia del banco SE del actor"};
    return {Ruta::Pendiente,-1,v,"clip comun: falta identidad IW del actor"};
  }
  if (tipo==3) {
    if (v>=2000) {
      if (japones) return {Ruta::SilencioOriginal,-1,v,"voz solo inglesa"};
      v-=2000;
    } else if (v>=1000) {
      if (!japones) return {Ruta::SilencioOriginal,-1,v,"voz solo japonesa"};
      v-=1000;
    }
    if (v==400) return {Ruta::SilencioOriginal,-1,v,"centinela400: original no reproduce"};
    if (v<0 || v>=100) return {Ruta::Pendiente,-1,v,"seleccion aleatoria original pendiente (no elegir voz fija)"};
    if (banco_actor<0) return {Ruta::Pendiente,-1,v,"clip comun: falta identidad IW del actor"};
    return {Ruta::Voz,banco_actor,v,"grito original; sin sustitucion B3"};
  }
  return {Ruta::Pendiente,-1,v,tipo<kConsumidores.size()?kConsumidores[tipo].falta:"tipo fuera del switch original"};
}
// Identidad del CUERPO anfitrion, no del clip compartido 204/915. IDs B3 ya
// usados por los selectores de minijuegos; el banco SE sigue leyendose del ELF.
inline uint32_t CmnActor(uint32_t id) {
  switch(id) {
    case 0:return 922;case 2:return 921;case 3:return 920;case 4:return 919;case 6:return 923;case 7:return 931;
    case 8:return 928;case 9:return 927;case 10:return 924;case 11:return 925;
    case 12:return 929;case 13:return 932;case 14:return 926;case 15:return 930;
    case 33:return 914;
    default:return 0;
  }
}
inline int PerfilAdx(uint32_t cmn) {
  // EU298380/USA2980F0, mismos indices que los bancos de minijuegos.
  switch(cmn) {
    case 922:return 0;case 921:return 1;case 920:return 2;case 919:return 3;
    case 923:return 4;case 931:return 5;case 928:return 6;case 927:return 7;
    case 924:return 8;case 925:return 9;case 929:return 10;case 932:return 11;
    case 926:return 12;case 930:return 13;case 914:return 15;
    case 912:return 16; // BBR actor+B8C=39, EU298380 -> filtro16
    default:return -1;
  }
}
inline size_t ElfOff(const std::vector<uint8_t>& elf,uint32_t direccion,size_t n) {
  if(!Rango(elf,0,52) || U32(elf,0)!=0x464C457F)return elf.size();
  const size_t tabla=U32(elf,28),paso=uint16_t(U32(elf,42)),cuenta=uint16_t(U32(elf,44));
  if(paso<32 || tabla>elf.size() || cuenta>(elf.size()-tabla)/paso)return elf.size();
  for(size_t i=0;i<cuenta;++i) {
    const size_t p=tabla+i*paso;const uint32_t va=U32(elf,p+8),tam=U32(elf,p+16);
    if(U32(elf,p)!=1 || direccion<va || uint64_t(direccion)-va+n>tam)continue;
    const size_t off=size_t(U32(elf,p+4))+direccion-va;
    return Rango(elf,off,n)?off:elf.size();
  }
  return elf.size();
}
// AP7 -> USA31B0D0/31B120 (EU31B380/31B410): tabla filtrada del
// minijuego, no el banco SESQ ni el indice ADX crudo del evento.
inline Audio ResolverAdx(const std::vector<uint8_t>& elf,uint32_t cmn,uint32_t valor,uint32_t cambiando_forma) {
  int v=int16_t(uint16_t(valor));
  if(v<0)return {Ruta::SilencioOriginal,-1,v,"indice negativo: AudioFiles no reproduce"};
  const bool solapar=v>=2000;
  if(solapar)v-=2000; // USA22FC78..88: AudioFiles(slot,perfil,v-2000,-1)
  else if(v>=1000)return {Ruta::PausaAdx,-1,v,"pausa del slot de voz del actor"};
  if(cambiando_forma)return {Ruta::Pendiente,-1,v,"tabla de transicion de forma ADX pendiente; sin voz de otra forma"};
  const int perfil=PerfilAdx(cmn);
  if(perfil<0)return {Ruta::Pendiente,-1,v,"identidad IW sin perfil ADX verificado"};
  const uint32_t tabla=elf.size()==3784752?0x47FC00u:elf.size()==3786800?0x4803B0u:0;
  if(!tabla)return {Ruta::Pendiente,-1,v,"version ELF no reconocida para ADX"};
  const size_t cnt=ElfOff(elf,tabla+0x50+4*uint32_t(perfil),4),ptr=ElfOff(elf,tabla+4*uint32_t(perfil),4);
  if(cnt==elf.size() || ptr==elf.size())return {Ruta::Pendiente,-1,v,"tabla ADX truncada"};
  if(uint32_t(v)>=U32(elf,cnt))return {Ruta::SilencioOriginal,-1,v,"indice fuera del perfil: original no reproduce"};
  const size_t at=ElfOff(elf,U32(elf,ptr)+4*uint32_t(v),4);
  if(at==elf.size())return {Ruta::Pendiente,-1,v,"entrada ADX truncada"};
  const int entrada=int32_t(U32(elf,at));
  if(entrada==-1)return {Ruta::SilencioOriginal,-1,v,"entrada ADX nula original"};
  if(entrada<0)return {Ruta::Pendiente,-1,v,"entrada ADX invalida"};
  return {Ruta::Adx,entrada,v,"tabla filtrada de voz del minijuego IW",solapar};
}
// Avisos de progreso de los campos a pie (s_PLY_CUTIN EU 0x2A53C0): linea `v` del perfil ADX `perfil`
// (MinigameAudioTriggerFilter EU 0x298380: 0x0B Piccolo -> 9, 0x4D -> 0x11, 0x6C Kaio -> 0x13). Mismo recorrido
// que Audio_GetFilteredTableEntry; devuelve la entrada del ADX_*.AFS o -1.
inline int EntradaAdxPerfil(const std::vector<uint8_t>& elf,int perfil,int v) {
  // EU31B3C0: punteros4803B0;480400 son los20conteos, NO los punteros.
  const uint32_t tabla=elf.size()==3784752?0x47FC00u:elf.size()==3786800?0x4803B0u:0;
  if(!tabla || perfil<0 || perfil>=20 || v<0)return -1;
  const size_t cnt=ElfOff(elf,tabla+0x50+4*uint32_t(perfil),4),ptr=ElfOff(elf,tabla+4*uint32_t(perfil),4);
  if(cnt==elf.size() || ptr==elf.size() || uint32_t(v)>=U32(elf,cnt))return -1;
  const size_t at=ElfOff(elf,U32(elf,ptr)+4*uint32_t(v),4);
  return at==elf.size() ? -1 : int32_t(U32(elf,at));
}
struct AzarVoz {
  struct Fila { int base=0,cuenta=0,probabilidad=0; };
  std::array<Fila,14> filas{};
  bool valida=false;
};
// EU457D50/USA457550, 14 registros {s16 base,s8 cuenta,s8 porcentaje}.
// Solo la firma de localizacion: todos los valores se leen del ELF del jugador.
inline AzarVoz LeerAzarVoz(const std::vector<uint8_t>& elf) {
  AzarVoz a;
  for(size_t p=0;Rango(elf,p,56);p+=4) {
    if(U32(elf,p)!=0x64080000 || U32(elf,p+4)!=0x64040008 || U32(elf,p+8)!=0x6406000C) continue;
    for(size_t i=0;i<a.filas.size();++i) {
      const auto v=U32(elf,p+4*i);
      a.filas[i]={int16_t(uint16_t(v)),int8_t(v>>16),int8_t(v>>24)};
      if(a.filas[i].base<0 || a.filas[i].cuenta<1 || a.filas[i].probabilidad<0 || a.filas[i].probabilidad>100) return {};
    }
    a.valida=true;return a;
  }
  return a;
}
// EU22FEA0/USA22FD30 llama Memory(32), no Memory(8) del emisor AME.
// El callback facilita el contraste ejecutando instrucciones retail con el
// MISMO estado RNG. La semilla global de IW no existe en el anfitrion.
template<class Memoria>
inline Audio ResolverVoz(uint32_t valor,int banco,bool japones,const AzarVoz& tabla,Memoria memoria) {
  int v=int16_t(uint16_t(valor));
  if(v>=2000) {if(japones)return {Ruta::SilencioOriginal,-1,v,"voz solo inglesa"};v-=2000;}
  else if(v>=1000) {if(!japones)return {Ruta::SilencioOriginal,-1,v,"voz solo japonesa"};v-=1000;}
  if(v==400)return {Ruta::SilencioOriginal,-1,v,"centinela400: original no reproduce"};
  if(v>=300) {
    const uint32_t porcentaje=v>=600?60:30;
    if(uint32_t(memoria())%100+1>porcentaje)
      return {Ruta::SilencioOriginal,-1,v,"filtro probabilistico original"};
    v-=v>=600?600:300;
  } else if(v>=100) {
    const int fila=v-100;
    if(!tabla.valida || fila>=int(tabla.filas.size()))return {Ruta::Pendiente,-1,v,"tabla aleatoria original ausente/fuera de rango"};
    const auto& f=tabla.filas[size_t(fila)];
    if(f.probabilidad<100 && uint32_t(memoria())%100+1>uint32_t(f.probabilidad))
      return {Ruta::SilencioOriginal,-1,v,"filtro probabilistico original"};
    v=f.base+int(uint32_t(memoria())%uint32_t(f.cuenta));
  }
  if(v<0 || banco<0)return {Ruta::Pendiente,-1,v,"identidad IW del actor ausente"};
  return {Ruta::Voz,banco,v,"grito original tras selector probabilistico"};
}
// Tabla de argumentos REALES de los sonidos del tipo6 (USA2A866C..2A8970).
// Sin indice de minijuego no se puede escoger este banco/sonido desde cmn915.
// +1E8 es el MATERIAL de Collision_Raycast (EU296A00), NO el actor.
inline int SonidoCampo(int valor,int mini,int superficie,int lado) {
  if (lado!=0 || (valor!=5 && valor!=6)) return -1;
  if (superficie==3 || superficie==4) return valor==5?4:3;
  if (mini==2) return valor==5?8:7;
  if (mini==10 || mini==16) return valor==5?6:5;
  // USA2A87D0 conserva a1=2 (NO6); USA2A8960 escribe a1=1.
  if (mini==14) return valor==5?(superficie==6?8:2):(superficie==6?7:1);
  if (mini==0 || mini==1) return valor==5?(superficie==6?6:2):(superficie==6?5:1);
  return -1;
}
inline int EfectoCampo(int valor,int mini,int superficie,int lado) {
  if(lado!=0 || valor<1 || valor>4)return -1;
  const int fase=superficie==3 || superficie==4?1:superficie==6?2:superficie==7?3:0;
  if(fase==3)return -1;
  return (valor<=2?3:4)+(fase==1 || (fase==2 && mini!=0)?6:0);
}
} // namespace dbz3::anim::apev

// Regresion del motor real, activada solo por DBZ3_IW_AMA_PRUEBA.
// Oraculo: IW EU 1B69B0 recalcula cada indice; 1B3D50 hereda con flags 08/10/20 sin claves FX.
#pragma once
#include "../src/anim_iw.h"
#include "../src/iw_eventos.h"
#include <rex/logging.h>
#include <cmath>
#include <cstring>

namespace dbz3::anim::ama::prueba {
inline bool Ejecutar() {
  int bien = 0, total = 0;
  auto comprobar = [&](bool ok, const char* que) {
    ++total;
    if (ok) ++bien;
    else REXLOG_WARN("dbz3 solIWA AMA: FAIL {}", que);
  };
  auto u32 = [](std::vector<uint8_t>& b, size_t o, uint32_t v) { std::memcpy(b.data()+o,&v,4); };
  Canal c;
  c.n=3;c.dur=30;c.bucle=true;
  c.tiempos={{0,kEscalon},{10,kEscalon},{20,kEscalon}};
  c.claves.resize(3*32);
  for (size_t k=0;k<3;++k) {
    const float f=float(k+1);
    uint32_t v;std::memcpy(&v,&f,4);
    for (size_t j=0;j<8;++j) u32(c.claves,32*k+4*j,v);
  }
  int ce=-1,cp=-1,cc=-1,ct=-1;
  // Avance, salto de varias claves, vuelta de bucle y rebobinado; la cache
  // pertenece al consumidor, como ocurre con un arbol vivo del mapa/HUD.
  const float tiempos[]={0,11,21,31,21,5};
  const float esperado[]={1,2,3,1,3,1};
  for (int k=0;k<6;++k) {
    float escala[8]={},pos[4]={};MuestraColor color;MuestraCuadro cuadro;
    MuestrearEscala(c,tiempos[k],ce,escala);
    MuestrearPosicion(c,tiempos[k],cp,pos);
    MuestrearColor(c,tiempos[k],cc,color);
    MuestrearCuadro(c,tiempos[k],ct,cuadro);
    comprobar(escala[0]==esperado[k],"escala/cache");
    comprobar(pos[0]==esperado[k],"posicion/cache");
    comprobar(color.tinte[0]==esperado[k],"color/cache");
    uint32_t bits;std::memcpy(&bits,&esperado[k],4);
    comprobar(cuadro.crudo==bits,"textura/cache");
  }
  // Archivo minimo valido: un nodo y un segmento, FX sin canales propios.
  // El lector ha de conservar los tres bits de herencia, no inventar claves.
  std::vector<uint8_t> b(0xC0);
  std::memcpy(b.data(),"#AMA",4);u32(b,0x10,1);u32(b,0x14,0x20);
  u32(b,0x20,0x30);u32(b,0x40,0x50);u32(b,0x5C,0x90);u32(b,0x6C,30);
  u32(b,0x90,0x38);
  Obra o;comprobar(o.Cargar(b),"lector AMA");
  if (o.Valida()) {
    const Segmento& s=o.Seg(o.Nodo(o.Raiz()).anim);
    comprobar(s.tiene_fx,"FX heredados sin claves");
    Muestras m;m.pos[0]=7;m.pos[1]=9;m.escala[0]=2;m.escala[1]=3;m.escala[2]=45;
    m.color.tinte[3]=0.25f;
    Contexto ctx;AplicarFx(s,0,m,ctx);
    comprobar(ctx.pos[0]==7&&ctx.pos[1]==9,"herencia posicion");
    comprobar(ctx.escala_tam[0]==2&&ctx.escala_tam[1]==3&&ctx.giro==45,"herencia escala/giro");
    comprobar(ctx.tinte[3]==0.25f,"herencia alfa");
  }
  REXLOG_INFO("dbz3 solIWA AMA: {}/{} {}",bien,total,bien==total ? "PASS" : "FAIL");
  const bool ama_ok=bien==total;
  bien=total=0;
  std::vector<eventos::Registro> registros={{10,1,123}};
  float anterior=-1;int sonidos=0;
  auto emitir=[&](const eventos::Registro&) { ++sonidos; };
  eventos::Avanzar(registros,anterior,9.5f,20,emitir);
  comprobar(sonidos==0,"AP no anticipar medio cuadro");
  eventos::Avanzar(registros,anterior,9.5f,20,emitir);
  comprobar(sonidos==0,"AP consulta repetida antes");
  eventos::Avanzar(registros,anterior,10,20,emitir);
  comprobar(sonidos==1,"AP cruzar cuadro exacto");
  eventos::Avanzar(registros,anterior,10,20,emitir);
  comprobar(sonidos==1,"AP consulta repetida despues");
  registros={{19,1,1},{0,1,2}};anterior=18;sonidos=0;
  eventos::Avanzar(registros,anterior,1,20,emitir);
  comprobar(sonidos==2,"AP cola final y principio al envolver");
  registros.assign(8,{5,1,1});anterior=0;sonidos=0;
  eventos::Avanzar(registros,anterior,10,20,emitir);
  comprobar(sonidos==7,"AP capacidad siete impactos");
  registros.assign(2,{5,3,1});anterior=0;sonidos=0;
  eventos::Avanzar(registros,anterior,10,20,emitir);
  comprobar(sonidos==1,"AP capacidad una voz");
  registros={{1,1,7},{4,1,8},{10,1,9}};anterior=-1;sonidos=0;
  eventos::Avanzar(registros,anterior,5,20,emitir);
  comprobar(sonidos==2,"AP destino durante fundido");
  eventos::Avanzar(registros,anterior,5,20,emitir);
  comprobar(sonidos==2,"AP fin fundido sin repeticion");
  REXLOG_INFO("dbz3 solIWA AP: {}/{} {}",bien,total,bien==total ? "PASS" : "FAIL");
  return ama_ok&&bien==total;
}
} // namespace dbz3::anim::ama::prueba

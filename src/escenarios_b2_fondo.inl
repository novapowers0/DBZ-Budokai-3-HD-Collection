// Consumidor de raíces B2 sobre los controladores reales HD (registro32B).
// BE1D0/BDFA8: +8 controlador, [0]modelo,[1]tiempo,[6]modo,[7]mezcla.
#include "escenarios_b2_tareas.inl"
struct B2FondoHD {
  struct Raiz { uint32_t amm=0,registro=0,control=0,uv=0,modelo=0,ultimo_fx=~0u; bool pistas=false,por_guion=false; std::array<uint8_t,32> antes{},antes_uv{}; };
  struct Copia {uint32_t p=0;std::vector<uint8_t> bytes;};
  std::vector<Raiz> raices;
  std::vector<Copia> copias;
  uint32_t zdd=0;
  uint32_t banco_fx=0,n_fx=0;
  int grupo_fx=-1;
  uint32_t dibujos_fx=0;
  std::vector<uint32_t> instancias_fx;
  struct AnclajeFx {uint32_t fx=0,raiz=0,hueso=0;bool seguir=false,anclado=false;};
  std::map<uint32_t,AnclajeFx> tareas_fx;
#include "escenarios_b2_robot.inl"
  bool CargarFx(PPCContext& ctx,uint8_t* base,const b2::Recurso& r) {
    if(banco_fx)return true;
    const auto v=r.Bloque(10);
    if(!v.Rango(0,32)||std::memcmp(v.p,"#AMB",4)||!v.U(16,true)||v.U(16,true)>640)return false;
    const uint32_t datos=g_mem?g_mem->SystemHeapAlloc(uint32_t(v.n),32):0;
    if(!datos)return false;std::memcpy(base+datos,v.p,v.n);
    // El contenedor genérico no reubica los enlaces del árbol ACE. Su
    // inicializador específico sí (F2E0), antes de que el AMB marque los hijos.
    const uint32_t tabla=v.U(20,true);
    for(uint32_t i=0;i<v.U(16,true);++i) {
      const uint32_t p=v.U(tabla+16*i,true);
      if(v.Rango(p,16)&&!std::memcmp(v.p+p,"#ACE",4)) {
        PPCContext c=ctx;c.r3.u64=datos+p;sub_821546F8(c,base);
      } else if(v.Rango(p,16)&&v.U(tabla+16*i+4,true)) {
        PPCContext c=ctx;c.r3.u64=datos+p;sub_820800A8(c,base);
      }
    }
    PPCContext c=ctx;c.r3.u64=datos;sub_820800A8(c,base);
    n_fx=REX_LOAD_U32(datos+16);
    const uint32_t t=REX_LOAD_U32(datos+20);
    if(t<0x10000000u||t>=0xf0000000u)return false;
    banco_fx=g_mem->SystemHeapAlloc(32+641*16,32);if(!banco_fx)return false;
    std::memset(base+banco_fx,0,32+641*16);std::memcpy(base+banco_fx,base+datos,32);
    REX_STORE_U32(banco_fx+16,641);REX_STORE_U32(banco_fx+20,banco_fx+32);
    std::memcpy(base+banco_fx+32,base+t,16*n_fx);
    // HD busca el atlas de escenario en640; B2 lo guarda en0. Alias de la
    // misma textura reubicada, sin copiar ni inicializarla dos veces.
    std::memcpy(base+banco_fx+32+640*16,base+t,16);
    REXLOG_INFO("dbz3 B2 FX: banco original convertido/reubicado {} entradas, atlas0->640, {:08X}",n_fx,banco_fx);
    return true;
  }
  bool Cargar(uint8_t* base,const b2::Recurso& r) {
    raices.clear();copias.clear();zdd=REX_LOAD_U32(kG+32);
    instancias_fx.clear();
    tareas_fx.clear();
    dibujos_fx=0;
    if(grupo_fx!=int(r.grupo)){banco_fx=n_fx=0;grupo_fx=int(r.grupo);}
    const auto m=r.Bloque(3);if(!m.Rango(0,40)||m.U(8)>64)return false;
    const uint32_t tabla=m.U(36),n=m.U(8);
    if(!m.Rango(tabla,4u*n))return false;
    for(uint32_t i=0;i<n;++i) {
      const uint32_t s=m.U(tabla+4*i);if(!m.Rango(s,32))return false;
      Raiz a;
      if(m.U(s)) {
        const uint32_t t=m.U(s+12);if(!m.Rango(t,16))return false;
        a.amm=m.U(t+4);
      }
      raices.push_back(a);
    }
    return true;
  }
  bool Vincular(uint8_t* base,unsigned i) {
    if(i>=raices.size())return false;
    auto& a=raices[i];if(a.registro)return true;
    const uint32_t n=REX_LOAD_U32(kG+56),p=REX_LOAD_U32(kG+60);
    if(n!=raices.size()||p<0x10000000u||p>=0xf0000000u) {
      REXLOG_WARN("dbz3 B2 raices: original={} HD={}; reconstruir pack con todas las raices",raices.size(),n);
      return false;
    }
    const uint32_t registro=p+32*i,control=REX_LOAD_U32(registro+8),uv=REX_LOAD_U32(registro+12);
    if((control&&(control<0x10000000u||control>=0xf0000000u))||
       (uv&&(uv<0x10000000u||uv>=0xf0000000u)))return false;
    const uint32_t modelo=control?REX_LOAD_U32(control):0;
    if(control&&(modelo<0x10000000u||modelo>=0xf0000000u))return false;
    a.registro=registro;a.control=control;a.uv=uv;a.modelo=modelo;
    if(uv)std::memcpy(a.antes_uv.data(),base+uv,32);
    if(control) {
      std::memcpy(a.antes.data(),base+control,32);
      REXLOG_INFO("dbz3 B2 raiz: id={} registro={:08X} control={:08X} modelo={:08X} modo={} mezcla={:.3f}",
                  i,a.registro,a.control,a.modelo,REX_LOAD_U32(a.control+24),LeeFl(a.control+28));
    }
    return true;
  }
  bool Pistas(uint8_t* base,const b2::Recurso& r,Raiz& a) {
    if(a.pistas)return true;
    if(!a.control)return !a.amm;
    const auto m=r.Bloque(3);const uint32_t s=a.amm,acm=REX_LOAD_U32(a.modelo+0x46c);
    if(!m.Rango(s,32)||std::memcmp(m.p+s,"#AMM",4)||acm<0x10000000u||acm>=0xf0000000u||
       (REX_LOAD_U32(acm)&0xffffffu)!=0x41434du)return false;
    const uint32_t n=m.U(s+16),nb=m.U(s+24);
    if(n!=REX_LOAD_U32(acm+16)||nb!=REX_LOAD_U32(acm+24))return false;
    const uint32_t st=s+m.U(s+20),dt=B2AnimHD::Ptr(acm,REX_LOAD_U32(acm+20));
    for(uint32_t clip=0;clip<n;++clip) {
      const uint32_t src=st+16*clip,dst=dt+16*clip;
      if(!m.Rango(src,16))return false;
      const uint32_t sp=m.U(src)&16?3:2,dp=REX_LOAD_U32(dst)&16?3:2;
      const uint32_t sb=s+m.U(src+12),db=B2AnimHD::Ptr(acm,REX_LOAD_U32(dst+12));
      if(!m.Rango(sb,nb*sp*4))return false;
      for(uint32_t bone=0;bone<nb;++bone)for(uint32_t k=0;k<std::min(sp,dp);++k) {
        const uint32_t rel=m.U(sb+4*(bone*sp+k)),p=B2AnimHD::Ptr(acm,REX_LOAD_U32(db+4*(bone*dp+k)));
        if(!rel&&!p)continue;if(!rel||p<0x10000000u||p>=0xf0000000u)return false;
        const uint32_t q=s+rel,keys=m.U(q+8),step=k?16:8;
        if(!m.Rango(q,12)||keys!=REX_LOAD_U32(p+8)||!m.Rango(q+12,size_t(keys)*step))return false;
        copias.push_back({p+12,std::vector<uint8_t>(base+p+12,base+p+12+keys*step)});
        for(uint32_t j=0;j<keys;++j) {
          const uint32_t x=q+12+j*step,y=p+12+j*step;
          if(step==8)for(unsigned v=0;v<4;++v)REX_STORE_U16(y+2*v,uint16_t(m.p[x+2*v]|uint16_t(m.p[x+2*v+1])<<8));
          else for(unsigned v=0;v<4;++v)REX_STORE_U32(y+4*v,m.U(x+4*v));
        }
      }
    }
    a.pistas=true;REXLOG_INFO("dbz3 B2 raiz: pistas originales restituidas modelo={:08X} clips={} huesos={}",a.modelo,n,nb);
    return true;
  }
  bool Controlar(PPCContext& ctx,uint8_t* base,const b2::Recurso& r,const b2::Orden& o) {
    const auto& p=o.argumentos;const unsigned i=p[3];if(!Vincular(base,i))return false;
    auto& a=raices[i];const int modo=b2vm::Signo(p[2]);
    a.por_guion=true;
    PPCContext c=ctx;
    if(modo==-1) {
      unsigned eliminadas=0;
      if(!B2TareasHD::Eliminar(a.registro,
          [&](uint32_t p){return REX_LOAD_U32(p);},
          [&](uint32_t t){PPCContext q=ctx;q.r3.u64=t;sub_820BD570(q,base);},eliminadas))return false;
      REXLOG_INFO("dbz3 B2 raiz: eliminacion PAL id={} tareas={}",i,eliminadas);
    }
    else {c.r3.u64=i;sub_820C1390(c,base);}
    if(a.control) {
      if(modo>0&&!Pistas(base,r,a))return false;
      const float tiempo=b2vm::Float(p[1]);
      // PAL1B908C hace SWC1: conserva los bits del tiempo, sin pasar por entero.
      if(tiempo>=0)REX_STORE_U32(a.control+4,p[1]);
      c=ctx;c.r3.u64=a.registro;sub_820BE1D0(c,base);
      REX_STORE_U32(a.control+24,modo>0?uint32_t(modo)&3:0);
    }
    // El controlador UV es independiente, tal como1B8FF0 (no confundirlo con AMM).
    const uint32_t uv=REX_LOAD_U32(a.registro+12);
    if(uv) {
      if(uv<0x10000000u||uv>=0xf0000000u)return false;
      if(b2vm::Float(p[0])>=0)REX_STORE_U32(uv+4,p[0]);
      c=ctx;c.r3.u64=a.registro;sub_820BD628(c,base);
      REX_STORE_U32(uv+24,modo>0?uint32_t(modo)&5:0);
    }
    return true;
  }
  bool CrearFx(PPCContext& ctx,uint8_t* base,const b2::Recurso& r,const b2::Orden& o) {
    const auto& p=o.argumentos;
    if(!Vincular(base,p[4])||!CargarFx(ctx,base,r)||!p[2]||p[2]>=n_fx)return false;
    auto& a=raices[p[4]];const float tiempo=b2vm::Float(p[0]);
    if(a.control&&!Pistas(base,r,a))return false;
    if(a.control&&tiempo>=0) {
      const uint32_t acm=REX_LOAD_U32(a.modelo+0x46c),t=B2AnimHD::Ptr(acm,REX_LOAD_U32(acm+20));
      const uint32_t clip=REX_LOAD_U32(a.control+16)&0xffff;
      float f=tiempo+1;
      if(clip>=REX_LOAD_U32(acm+16))return false;
      if(f>=float(REX_LOAD_U32(t+clip*16+8)))f=0;
      REX_STORE_U32(a.control+4,b2vm::Bits(f));
    }
    const uint32_t antes=REX_LOAD_U32(0x823ee81c),prev=REX_LOAD_U32(kG+44);
    REX_STORE_U32(kG+44,banco_fx);
    PPCContext c=ctx;c.r3.u64=a.registro;c.r4.u64=p[3];c.r5.u64=p[2];c.f1.f64=tiempo;
    sub_820C1098(c,base); // emisor y anclaje de hueso del motor; no sprites host aproximados
    REX_STORE_U32(kG+44,prev);
    if(antes==REX_LOAD_U32(0x823ee81c))return false;
    const uint32_t fx=REX_LOAD_U32(0x824370b0+4*(antes&127));
    REX_STORE_U32(fx+8,REX_LOAD_U32(fx+8)|2); // grupo02000002 del guion B2
    instancias_fx.push_back(fx);
    uint32_t tarea=0;
    for(uint32_t t=0x82398890,n=0;t!=0x823987e0&&n<512;++n) {
      if(t<0x10000000u||t>=0xf0000000u)break;
      if(REX_LOAD_U32(t+16)==a.registro&&REX_LOAD_U32(t+56)==fx){tarea=t;break;}
      t=REX_LOAD_U32(t+36);
    }
    if(!tarea)return false;
    tareas_fx[tarea]={fx,p[4],p[3],p[1]!=0,false};
    REXLOG_INFO("dbz3 B2 FX: recurso={} raiz={} hueso={} seguir={} demora={:.1f} instancia={:08X} tarea={:08X}",p[2],p[4],p[3],p[1],tiempo,fx,tarea);
    return true;
  }
  bool ActualizarFx(PPCContext& ctx,uint8_t* base) {
    const uint32_t t=ctx.r3.u32;auto it=tareas_fx.find(t);if(it==tareas_fx.end())return false;
    auto& e=it->second;
    if(REX_LOAD_U32(t+56)!=e.fx){tareas_fx.erase(it);return false;}
    const float edad=LeeFl(t+132)+LeeFl(kG+68);REX_STORE_U32(t+132,b2vm::Bits(edad));
    if(edad<LeeFl(t+128))return true;
    if(int32_t(REX_LOAD_U32(e.fx+56))<0) {
      PPCContext c=ctx;c.r3.u64=t;sub_820BD570(c,base);tareas_fx.erase(it);return true;
    }
    auto& a=raices[e.raiz];
    // 1AC9F0 actualiza también raíces auxiliares SIN tarea de dibujo. HD solo
    // avanza automáticamente las raíces visibles: NADARE quedaba en cuadro1.
    const uint32_t guest=REX_LOAD_U32(0x8238D6B4);
    if(a.control&&!a.por_guion&&a.ultimo_fx!=guest) {
      a.ultimo_fx=guest;PPCContext c=ctx;c.r3.u64=a.registro;sub_820BDFA8(c,base);
    }
    // 1AC9F0: argumento1de96 = seguimiento del ancla, NO repetir el emisor.
    if(a.control&&(e.seguir||!e.anclado)) {
      const uint32_t mat=REX_LOAD_U32(a.modelo+1032);
      PPCContext c=ctx;c.r3.u64=t+64;c.r4.u64=mat+64*e.hueso;sub_8208A0C8(c,base);e.anclado=true;
    }
    PPCContext c=ctx;c.r3.u64=e.fx;sub_821547D8(c,base);
    return true;
  }
  void Devolver(PPCContext& ctx,uint8_t* base) {
    if(zdd!=REX_LOAD_U32(kG+32))return;
    for(const auto& b:copias)std::memcpy(base+b.p,b.bytes.data(),b.bytes.size());
    for(const auto& a:raices)if(a.control&&REX_LOAD_U32(a.registro+8)==a.control) {
      std::memcpy(base+a.control,a.antes.data(),32);
      PPCContext c=ctx;c.r3.u64=a.registro;sub_820BE1D0(c,base);
      std::memcpy(base+a.control,a.antes.data(),32);
    }
    for(const auto& a:raices)if(a.uv&&REX_LOAD_U32(a.registro+12)==a.uv) {
      std::memcpy(base+a.uv,a.antes_uv.data(),32);
      PPCContext c=ctx;c.r3.u64=a.registro;sub_820BD628(c,base);
      std::memcpy(base+a.uv,a.antes_uv.data(),32);
    }
    copias.clear();
    for(uint32_t p:instancias_fx)if((REX_LOAD_U32(p+8)&0x02000002)==0x02000002) {
      PPCContext c=ctx;c.r3.u64=p;sub_8214F928(c,base);
    }
    instancias_fx.clear();
  }
};

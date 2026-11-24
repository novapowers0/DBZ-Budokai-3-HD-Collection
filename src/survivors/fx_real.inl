// Incluido por combate.cpp: todas las llamadas PPC ocurren en su hilo y contexto.
// Spawn 82155990 -> callbacks originales; tick 821547D8. El adaptador de dibujo
// lee las particulas producidas por ellos; no calcula nacimientos ni curvas ACE.
#include "fx_real.h"
#include <mutex>
#include <string>
#include <cstdlib>

REXCVAR_DEFINE_BOOL(dbz3_survivors_motor_fx, true, "DBZ3/Mods",
                    "Survivors: motor original de particulas B3 (experimental)");

namespace dbz3::survivors::fxreal {
namespace {
std::mutex mu;
std::vector<Pedido> pendientes;
std::vector<Cuadro> publicados;
uint64_t paso_ui = 0;
std::chrono::steady_clock::time_point recibido{};
struct Instancia {
  uint64_t id;
  uint32_t raiz=0, marca=0, ace=0;
  std::vector<uint8_t> gestor;
  bool suspendida=false;
};
std::vector<Instancia> instancias;
// 8214F0C8 inicializa esta region US: indices/listas, 2048 particulas,
// 256 componentes/enlaces, 128 raices y sus tablas; termina en control+112.
// El combate oculto usa las mismas direcciones. Intercambiar su estado SOLO
// dentro de Paso evita que su tick/recolector actualice o libere nuestros nodos.
// Todos los callbacks se ejecutan sincronos en este mismo hilo de combate.
struct FotoCombate {
  static constexpr uint32_t inicio=0x823EE7F0u, fin=0x82439320u;
  static constexpr size_t bytes=fin-inicio;
  uint8_t* base;
  std::vector<uint8_t>& combate;
  bool tomada=false;
  void Tomar() {
    if(tomada)return;
    combate.resize(bytes);
    std::memcpy(combate.data(),base+inicio,bytes);
    tomada=true;
  }
  void Restaurar() const { std::memcpy(base+inicio,combate.data(),bytes); }
};
struct TurnoPrivado {
  static constexpr auto inicio=FotoCombate::inicio;
  uint8_t* base;
  std::vector<uint8_t>& propio;
  FotoCombate& foto;
  // Todos los turnos de Paso restauran la misma foto: fuera de ellos solo se consultan tablas.
  TurnoPrivado(PPCContext& ctx,FotoCombate& f,std::vector<uint8_t>& imagen):base(f.base),propio(imagen),foto(f) {
    foto.Tomar();
    if(!propio.empty())std::memcpy(base+inicio,propio.data(),propio.size());
    else {
      propio.resize(FotoCombate::bytes);
      PPCContext c=ctx;sub_8214F0C8(c,base);
      static bool dicho=false;
      if(!dicho) {dicho=true;REXLOG_INFO("dbz3 survivors: motor FX con gestor privado por efecto US ({} bytes)",propio.size());}
    }
  }
  ~TurnoPrivado() {
    std::memcpy(propio.data(),base+inicio,propio.size());
    foto.Restaurar();
  }
};
bool Presupuesto(uint8_t* base) {
  // Indices consumir/devolver de 8214F0C8, 821527C0 y 82152C28.
  // Estos anillos NO comprueban si dan la vuelta sobre un objeto aun vivo.
  // Reservar margen para la emision recursiva de un tick. Si se acerca al
  // limite se suspende SOLO esta instancia hasta que desaparezca su pedido.
  // No reiniciar todas las demas ni repetir el mismo ACE peligroso cada frame.
  const auto usados=[&](uint32_t a,uint32_t b,uint32_t n) {
    return (REX_LOAD_U32(a)-REX_LOAD_U32(b))&(n-1);
  };
  const uint32_t particulas=usados(0x823EE804,0x823EE800,2048);
  const uint32_t componentes=usados(0x823EE80C,0x823EE808,256);
  if(particulas<768 && componentes<96)return true;
  static unsigned avisos=0;
  if(avisos++<8)REXLOG_WARN("dbz3 survivors: FX presupuesto particulas={} componentes={}; instancia suspendida, respaldo visual",particulas,componentes);
  return false;
}
bool Ptr(uint32_t p) { return p >= 0x10000000u && p < 0xE0000000u; }
int Lado(const Pedido& p) { return (p.id & (1ull<<60)) != 0; }
bool LuchadorCargado(uint8_t* base,const Pedido& p) {
  // Fighter()/kId de combate.cpp y tablas de carga 82128338/820B8F78 (US).
  if(p.b3<0)return p.banco==7 && p.bsp==504;
  if(p.b3>=64)return false;
  const uint32_t m=REX_LOAD_U32(0x823D6CF4u);
  if(!Ptr(m) || !Ptr(m+35128))return false;
  const uint32_t estado=REX_LOAD_U32(m+35128);
  if(estado<3 || estado>7)return false;
  if(REX_LOAD_U32(m+2096+8240*Lado(p)+7420)!=uint32_t(p.b3))return false;
  const uint32_t tabla=p.banco==6 ? 0x82322950u : 0x82333208u;
  return REX_LOAD_U32(tabla+4*p.b3)==uint32_t(p.bsp);
}
uint32_t Recurso(PPCContext& ctx,uint8_t* base,const Pedido& p,int banco,int indice) {
  PPCContext c=ctx;c.r3.u64=Lado(p);c.r4.u64=banco;c.r5.u64=indice;
  sub_820CF580(c,base);
  return c.r3.u32;
}
bool TecnicaCargada(uint8_t* base,const Pedido& p) {
  const uint32_t tec=REX_LOAD_U32(0x82333994u);
  if(!Ptr(tec))return false;
  const uint32_t slot=tec+1152*Lado(p)+80;
  return Ptr(slot+544) && REX_LOAD_U32(slot+20)==1;
}
uint32_t FilaRayo(PPCContext& ctx,uint8_t* base,const Pedido& p) {
  if(!TecnicaCargada(base,p))return 0;
  // SUR-8 (repaso dsREV): el CST esta en el banco 2 o, en algunos luchadores (Cell, Freezer, Cooler), en el 1
  uint32_t cst=0;
  for(const int banco:{2,1}) {
    const uint32_t c=Recurso(ctx,base,p,banco,0);
    if(Ptr(c+16) && Ptr(c) && (REX_LOAD_U32(c)&0xffffffu)==0x435354u) {cst=c;break;}
  }
  if(!cst)return 0;
  const uint32_t n=REX_LOAD_U32(cst+8), off=REX_LOAD_U32(cst+12);
  if(n>128 || off<16 || off>0x10000 || !Ptr(cst+off+n*0xF0))return 0;
  // Cooler tiene dos filas tipo 0, codigo 0; en B3 gana la primera.
  for(uint32_t k=0;k<n;++k) {
    const uint32_t r=cst+off+k*0xF0;
    const auto tipo=REX_LOAD_U16(r+16);
    if(tipo<=1 && REX_LOAD_U16(r+18)==p.cst_codigo)return r;
  }
  return 0;
}
float Fl(uint8_t* base, uint32_t p) {
  uint32_t u = REX_LOAD_U32(p); float f; std::memcpy(&f, &u, 4); return f;
}
bool Propia(uint8_t* base, const Instancia& i) {
  // La cabeza +48 CAMBIA cuando el emisor crea/elimina componentes. No es
  // identidad: usarla reiniciaba el mismo proyectil cada cuadro y agotaba el pool.
  if (!Ptr(i.raiz) || REX_LOAD_U32(i.raiz + 8) != 0 || REX_LOAD_U32(i.raiz + 12) != i.marca) return false;
  // 821557F8 tambien recoge raices terminadas. Una marca que aun no se ha
  // sobrescrito no demuestra que sigan enlazadas: evitar una segunda liberacion.
  const uint32_t nx=REX_LOAD_U32(i.raiz), pv=REX_LOAD_U32(i.raiz+4);
  return Ptr(nx) && Ptr(pv) && REX_LOAD_U32(pv)==i.raiz && REX_LOAD_U32(nx+4)==i.raiz;
}
void Quitar(PPCContext& ctx, uint8_t* base, const Instancia& i) {
  if (Propia(base, i)) {
    // 8214F928 solo escribe -1 en +56; no devuelve componentes ni particulas.
    // 82154748 es la liberacion original usada por 821557F8: devuelve cada
    // componente (82152C28), su enlace (8214FB10) y la raiz a los pools de B3.
    PPCContext c=ctx; c.r3.u64=i.raiz; sub_82154748(c,base);
    REX_STORE_U32(i.raiz+12,0);
  }
}
Vertice Sumar(Vertice a, Vertice b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vertice Por(Vertice a, float s) { return {a.x*s,a.y*s,a.z*s}; }
Vertice Normal(Vertice a) {
  const float l=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
  return l>1e-6f ? Por(a,1/l) : Vertice{};
}
Vertice Vector(uint8_t* base,uint32_t p) { return {Fl(base,p),Fl(base,p+4),Fl(base,p+8)}; }
void CuerpoRayo(uint8_t* base,const Pedido& p,uint32_t d,std::vector<Cuadro>& salida) {
  if(!d || REX_LOAD_U16(d+16)!=0 || !(p.largo_rayo>0) || !std::isfinite(p.largo_rayo))return;
  // 8212C2F8 usa CST +0x20/+0x22 para el recurso de dibujo, no son el
  // selector del ACE de punta. Banco 1 es habitual (Cell, Freezer, Cooler).
  const int textura=int16_t(REX_LOAD_U16(d+74));
  const float ancho=Fl(base,d+76), repeticion=Fl(base,d+84);
  if(textura<0 || textura>=512 || !std::isfinite(ancho) || ancho<=0 || ancho>1000 ||
     !std::isfinite(repeticion) || repeticion<=0 || repeticion>1000)return;
  // 8212C54C..8212C620: normal(cross(direccion, origen-ojo))*CST[0x4C].
  // La camara es la del modo. Dos caras de la funcion original coinciden en
  // este plano: una sola evita sumar dos veces el brillo al aplanarlo en la UI.
  const Vertice origen{0,0,-p.largo_rayo};
  const auto vista=Sumar(origen,Por(p.ojo,-1));
  const auto lado=Por(Normal(Vertice{-vista.y,vista.x,0}),ancho);
  if(lado.x*lado.x+lado.y*lado.y<1e-8f)return;
  // U_fin-U_inicio = largo/(CST[0x54]*CST[0x4C]*2), 8212C550..8212C5B0.
  // Cortar en periodos mantiene el wrap original con el sampler clamp de ImGui.
  const float periodos=p.largo_rayo/(repeticion*ancho*2);
  if(!std::isfinite(periodos) || periodos<=0 || periodos>128)return;
  const int segmentos=int(std::ceil(periodos));
  if(salida.size()+segmentos>2048)return;
  for(int k=0;k<segmentos;++k) {
    const float u0=float(k),u1=std::min(float(k+1),periodos);
    const Vertice a{0,0,-p.largo_rayo+p.largo_rayo*u0/periodos};
    const Vertice b{0,0,-p.largo_rayo+p.largo_rayo*u1/periodos};
    Cuadro q{};q.id=p.id;q.bsp=p.bsp;q.textura=textura;q.aditiva=true;q.cuerpo_rayo=true;
    q.v[0]=Sumar(a,lado);q.v[1]=Sumar(b,lado);
    q.v[2]=Sumar(b,Por(lado,-1));q.v[3]=Sumar(a,Por(lado,-1));
    q.uv[0]=0;q.uv[1]=0;q.uv[2]=u1-u0;q.uv[3]=1;
    for(auto& color:q.rgba)for(float& c:color)c=1;
    salida.push_back(q);
  }
}
void Recoger(PPCContext& ctx, uint8_t* base, const Instancia& i, const Pedido& pedido, std::vector<Cuadro>& salida) {
  // Lista del gestor: nodo+8 = componente; componente+20 = definicion ACE.
  for (uint32_t n = REX_LOAD_U32(i.raiz + 48), pasos = 0; Ptr(n) && pasos++ < 256;) {
    const uint32_t o = REX_LOAD_U32(n + 8);
    if (!Ptr(o)) break;
    const uint32_t d = REX_LOAD_U32(o + 20);
    if (!Ptr(d)) break;
    const uint32_t tipo = REX_LOAD_U32(d + 4);
    // Tabla US 8234D8A0 (pasos de 16): 20000 -> 8214FE48 (sprite), 20001 -> 821532C8 (billboard),
    // 20002 -> 82150500 (estela), 20003 -> 82150C00 (plano con matriz).
    // Confundir 20001 con 20003 leia color como escala y siempre daba tam=0.
    if (tipo >= 0x20000u && tipo <= 0x20003u) {
      const uint32_t fin = o + 68;
      for (uint32_t p = REX_LOAD_U32(o + 60), k = 0; Ptr(p) && p != fin && k++ < 256; p = REX_LOAD_U32(p)) {
        if (REX_LOAD_U32(p + 12) & 1) continue;
        const bool estela=tipo==0x20002u, simple=tipo==0x20000u;
        const bool frontal=tipo==0x20001u || simple;
        const float tam=estela ? Fl(base,p+96) : simple ? Fl(base,p+80)*.5f :
            Fl(base,p+(frontal ? 80 : 112))*Fl(base,p+(frontal ? 84 : 116));
        if (!std::isfinite(tam) || tam <= 0 || tam > 10000) continue;
        // Misma matriz de 82150C00, con destino explicito para no cambiar la
        // pila de matrices del combate. Salida fuera del marco de la llamada.
        const uint32_t m = ctx.r1.u32 - 128;
        if (!frontal && !estela) {
          PPCContext c = ctx; c.r1.u32 -= 512;
          c.r3.u64 = m; c.r4.u64 = p + 48; c.r5.u64 = p + 16;
          sub_82090730(c, base);
        }
        Cuadro q{}; q.id=i.id; q.bsp=pedido.bsp; q.banco=pedido.banco; q.ace=pedido.ace; q.frontal=frontal; q.centro=Vector(base,p+16);
        const bool texturada=(REX_LOAD_U32(d+8)&0x1000u)!=0;
        q.textura=texturada ? int(REX_LOAD_U32(d+(estela ? 196 : 164))) : -1;
        q.aditiva=REX_LOAD_U32(d+(estela ? 192 : 160))==1;
        const float xy[4][2] = {{-tam,tam},{tam,tam},{tam,-tam},{-tam,-tam}};
        const float ang = frontal && !simple ? Fl(base,p+88)*0.01745329252f : 0;
        const float ca=std::cos(ang), sa=std::sin(ang);
        const float aspecto=simple ? Fl(base,p+84)*.5f/tam : frontal ? 1+Fl(base,d+100) : 1;
        if(estela) {
          // 82150610..821506D0: cabeza = posicion + direccion normalizada*largo;
          // perpendicular = normalize(cross(direccion, posicion-ojo)); dos anchos.
          const auto dir=Normal(Vector(base,p+32));
          const auto vista=Sumar(q.centro,Por(pedido.ojo,-1));
          const auto cruz=Vertice{dir.y*vista.z-dir.z*vista.y,dir.z*vista.x-dir.x*vista.z,dir.x*vista.y-dir.y*vista.x};
          const auto lado=Normal(cruz), cabeza=Sumar(q.centro,Por(dir,tam));
          const auto a=Por(lado,Fl(base,p+104)), b=Por(lado,Fl(base,p+100));
          q.v[0]=Sumar(cabeza,a); q.v[1]=Sumar(cabeza,Por(a,-1));
          q.v[2]=Sumar(q.centro,Por(b,-1)); q.v[3]=Sumar(q.centro,b);
        }
        for (int v = 0; v < 4; ++v) {
          if(estela)continue;
          if(frontal) {
            // 821533D0..821534CC: ejes de camara. Aqui se usan los del modo,
            // nunca la camara del combate oculto que continua moviendose.
            q.v[v]={ca*xy[v][0]-sa*xy[v][1]*aspecto,
                    sa*xy[v][0]+ca*xy[v][1]*aspecto,0};
          } else {
            q.v[v] = {Fl(base,m)*xy[v][0]+Fl(base,m+16)*xy[v][1]+Fl(base,m+48),
                      Fl(base,m+4)*xy[v][0]+Fl(base,m+20)*xy[v][1]+Fl(base,m+52),
                      Fl(base,m+8)*xy[v][0]+Fl(base,m+24)*xy[v][1]+Fl(base,m+56)};
          }
        }
        bool valido=q.textura>=-1 && q.textura<512;
        bool visible=false;
        for (int v=0;v<4;++v) {
          const uint32_t color=p+(estela ? (v<2 ? 64 : 48) : frontal ? 48 : 80);
          for(int c=0;c<4;++c) { q.rgba[v][c]=Fl(base,color+4*c); valido &= std::isfinite(q.rgba[v][c]); }
          visible |= q.rgba[v][3]>0;
          valido &= std::isfinite(q.v[v].x) && std::isfinite(q.v[v].y) && std::isfinite(q.v[v].z);
        }
        const uint32_t uv=p+(estela ? 80 : frontal ? 64 : 96);
        q.uv[0]=Fl(base,uv); q.uv[1]=Fl(base,uv+4);
        // 82150D08..82150DC0 usa +104/+108 como extremos, no anchura/alto.
        q.uv[2]=Fl(base,uv+8); q.uv[3]=Fl(base,uv+12);
        for (float u:q.uv) valido &= std::isfinite(u);
        valido &= std::isfinite(q.centro.x) && std::isfinite(q.centro.y) && std::isfinite(q.centro.z);
        if (valido && visible && salida.size()<2048) {
          static unsigned diag[4]{};
          if(diag[tipo-0x20000u]++<2)REXLOG_INFO("dbz3 survivors: FX cuadro tipo={:08X} tam={:.3f} textura={} bsp={} aditiva={} color={:.3f},{:.3f},{:.3f},{:.3f}",tipo,tam,q.textura,q.bsp,q.aditiva,q.rgba[0][0],q.rgba[0][1],q.rgba[0][2],q.rgba[0][3]);
          salida.push_back(q);
        }
      }
    }
    const uint32_t siguiente=REX_LOAD_U32(n); if(siguiente==n)break; n=siguiente;
  }
}
}  // namespace

bool Activo() {
  static const bool prueba=[] { const char* p=std::getenv("DBZ3_TEST_SURVIVORS_FX_REAL");return p && *p=='1'; }();
  return !dbz3::g_guest_eu && (prueba || REXCVAR_GET(dbz3_survivors_motor_fx));
}
void Publicar(uint64_t paso,const std::vector<Pedido>& pedidos) {
  std::lock_guard<std::mutex> l(mu); paso_ui=paso; pendientes=pedidos; recibido=std::chrono::steady_clock::now();
}
std::vector<Cuadro> Leer() { std::lock_guard<std::mutex> l(mu); return publicados; }

void Paso(PPCContext& ctx,uint8_t* base) {
  if(dbz3::g_guest_eu)return; // Direcciones US verificadas; no adivinar parejas.
  std::vector<Pedido> pedidos; uint64_t paso;
  {
    std::lock_guard<std::mutex> l(mu); paso=paso_ui;
    if(Activo() && std::chrono::steady_clock::now()-recibido<std::chrono::milliseconds(500)) pedidos=pendientes;
  }
  pedidos.erase(std::remove_if(pedidos.begin(),pedidos.end(),[&](const Pedido& p) {
    return !LuchadorCargado(base,p);
  }),pedidos.end());
  std::vector<Cuadro> cuadros;
  // El cuerpo no necesita que el emisor cree particulas ni ocupa plaza ACE.
  for(auto& p:pedidos)if(p.cst_codigo>=0) {
    const uint32_t d=FilaRayo(ctx,base,p);
    p.ace=-1;
    if(!d)continue;
    CuerpoRayo(base,p,d,cuadros);
    const int banco=int16_t(REX_LOAD_U16(d+0x58)), ace=int16_t(REX_LOAD_U16(d+0x5e));
    if(banco>=0 && banco<=2 && ace>=2 && ace<48)p.banco=banco,p.ace=ace;
  }
  const auto ace_de=[&](const Pedido& p) -> uint32_t {
    if(p.ace<0)return 0;
    if(p.banco==6) {
      const uint32_t g=REX_LOAD_U32(0x82322AECu);
      if(!Ptr(g) || !Ptr(g+976) || REX_LOAD_U16(g+976)!=1)return 0;
    } else if(p.b3>=0 && p.cst_codigo<0 && !TecnicaCargada(base,p))return 0;
    const uint32_t ace=Recurso(ctx,base,p,p.banco,p.ace);
    return Ptr(ace) && (REX_LOAD_U32(ace)&0xffffffu)==0x414345u ? ace : 0;
  };
  // SUR-8 (sonda): inventario del lado 0. No identifica por si solo una tecnica.
  static bool sondado=false;
  if(!sondado && !pedidos.empty() && std::getenv("DBZ3_TEST_SURVIVORS_SONDA_FX")) {
    sondado=true;
    for(int banco=0;banco<4;++banco) {   // 4..15: acceso invalido (medido)
      std::string lista;
      for(int ace=0;ace<64;++ace) {
        PPCContext c=ctx;c.r3.u64=0;c.r4.u64=uint64_t(banco);c.r5.u64=uint64_t(ace);sub_820CF580(c,base);
        const uint32_t a=c.r3.u32;
        if(Ptr(a)&&(REX_LOAD_U32(a)&0xffffffu)==0x414345u) lista+=std::to_string(ace)+" ";
      }
      REXLOG_INFO("dbz3 survivors: sonda FX banco={} ACE validos: {}",banco,lista);
    }
  }
  if(pedidos.empty() && instancias.empty()) {
    std::lock_guard<std::mutex> l(mu);publicados.clear();return;
  }
  static std::vector<uint8_t> imagen_combate;
  FotoCombate foto{base,imagen_combate};
  static uint64_t anterior=~0ULL;
  if(paso<anterior && anterior!=~0ULL) {
    for(auto& i:instancias)if(!i.suspendida) {
      TurnoPrivado turno(ctx,foto,i.gestor);Quitar(ctx,base,i);
    }
    instancias.clear();
  }
  const bool avanza=paso!=anterior; anterior=paso;
  for(auto it=instancias.begin();it!=instancias.end();) {
    const auto p=std::find_if(pedidos.begin(),pedidos.end(),[&](const Pedido& p){return p.id==it->id;});
    // 821547D8 convierte el ultimo componente terminado en -1, y 821557F8
    // recoge cualquier contador <=0. Comparar solo con cero dejaba las bolas
    // y auras repetibles muertas ocupando plaza hasta desaparecer su pedido.
    bool retirar=p==pedidos.end() || (p->b3>=0 && ace_de(*p)!=it->ace);
    if(!it->suspendida && (retirar || (p!=pedidos.end() && p->repetir))) {   // auditoria #12: sin turno (2x306 KB) si no hay nada que comprobar
      TurnoPrivado turno(ctx,foto,it->gestor);
      retirar |= p!=pedidos.end() && p->repetir &&
          (!Propia(base,*it) || int32_t(REX_LOAD_U32(it->raiz+56))<=0);
      if(retirar)Quitar(ctx,base,*it);
    } else if (retirar) {
      // La instancia suspendida conserva raiz y componentes; al desaparecer su
      // pedido ya no se readmite, asi que se devuelven a los pools del gestor.
      TurnoPrivado turno(ctx,foto,it->gestor);
      Quitar(ctx,base,*it);
    }
    // Destruir el turno ANTES de mover/borrar la imagen a la que hace referencia.
    if(retirar)it=instancias.erase(it); else ++it;
  }
  // Con ocho plazas, las rafagas no deben impedir para siempre la admision
  // de la cabeza del rayo, aura, explosion o impacto publicados despues.
  std::stable_sort(pedidos.begin(),pedidos.end(),[](const Pedido& a,const Pedido& b) {
    return (a.banco==7 && a.ace==9) < (b.banco==7 && b.ace==9);
  });
  for(const auto& p:pedidos) {
    if(instancias.size()>=8)break;
    if(std::any_of(instancias.begin(),instancias.end(),[&](const Instancia& i){return i.id==p.id;}))continue;
    const uint32_t ace=ace_de(p);
    if(!ace)continue;
    Instancia nueva{p.id};
    nueva.ace=ace;
    {
      TurnoPrivado turno(ctx,foto,nueva.gestor);
      PPCContext c=ctx;c.r3.u64=ace;c.r4.u64=0;c.r5.u64=0;sub_82155990(c,base);
      nueva.raiz=c.r3.u32;
      // La prioridad original sigue a cero: el aislamiento ya da identidad y
      // ningun recolector del combate puede reutilizar esta imagen privada.
      nueva.suspendida=!Ptr(nueva.raiz) || !Presupuesto(base);
    }
    const uint32_t raiz=nueva.raiz;
    instancias.push_back(std::move(nueva));
    static std::vector<uint32_t> fuentes;
    const int lado=Lado(p);
    const uint32_t fuente=(uint32_t(p.bsp)<<14)|(uint32_t(p.banco)<<10)|(uint32_t(lado)<<9)|uint32_t(p.ace);
    if(Ptr(raiz) && std::find(fuentes.begin(),fuentes.end(),fuente)==fuentes.end()) {
      fuentes.push_back(fuente);
      REXLOG_INFO("dbz3 survivors: motor FX original spawn id={} banco={} indice={} bsp={} ACE={:08X} raiz={:08X} lado={}",p.id,p.banco,p.ace,p.bsp,ace,raiz,lado);
    }
  }
  for(auto& i:instancias) {
    if(i.suspendida)continue;
    TurnoPrivado turno(ctx,foto,i.gestor);
    if(!Propia(base,i))continue;
    if(avanza) { PPCContext c=ctx;c.r3.u64=i.raiz;sub_821547D8(c,base); }
    if(!Presupuesto(base)) {i.suspendida=true;continue;}
    const auto p=std::find_if(pedidos.begin(),pedidos.end(),[&](const Pedido& p){return p.id==i.id;});
    if(p!=pedidos.end())Recoger(ctx,base,i,*p,cuadros);
  }
  static bool dicho=false;
  if(!cuadros.empty()&&!dicho) {dicho=true;REXLOG_INFO("dbz3 survivors: motor FX original particulas={} textura={} color={:.3f},{:.3f},{:.3f}",cuadros.size(),cuadros[0].textura,cuadros[0].rgba[0][0],cuadros[0].rgba[0][1],cuadros[0].rgba[0][2]);}
  std::lock_guard<std::mutex> l(mu);publicados=std::move(cuadros);
}
}  // namespace dbz3::survivors::fxreal

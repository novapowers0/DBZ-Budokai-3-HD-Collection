// CapsulePick IW, portable C++17. EU484930; exclusivamente los 33 IDs literales
// de CMN993/994. NO usar la tabla de combate/B2. Fuente: iw_sles55347.c,
// 280500/2806E0 (bancos), 281DE0/472F40 (45 recursos), 282590..284E60,
// 279560..2796B0 y364ED0..364EF0. Los cuerpos omitidos por Ghidra se
// contrastaron en las instrucciones del ELF EU (direcciones en kFunciones).
// No hay #AMA entre esos45 recursos: AMC/AML/AMM/AMO/AMT/AMP/AME/IECS.
// Dibujo/skin/material/IOP requieren consumidores host: ausencia = bloqueo.
#pragma once
#include "iw_spx.h"
#include <memory>
#include <sstream>

namespace dbz3::iw::spx::capsulas {
using Bytes = std::vector<uint8_t>;
inline uint32_t U(const Bytes& b, size_t p) {
  if (p > b.size() || b.size() - p < 4) throw std::runtime_error("recurso IW truncado");
  return uint32_t(b[p]) | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24;
}
inline float F(const Bytes& b, size_t p) { return Float(U(b,p)); }
inline bool Magic(const Bytes& b, const char* s) { return b.size() >= 4 && !std::memcmp(b.data(),s,4); }
inline int16_t S16(const Bytes& b, size_t p) {
  if(p>b.size() || b.size()-p<2) throw std::runtime_error("short IW truncado");
  const uint16_t u=uint16_t(b[p]) | uint16_t(b[p+1])<<8;
  int16_t s; std::memcpy(&s,&u,2); return s;
}
inline std::string Etiqueta(const Bytes& b,size_t p) {
  if(p>b.size() || b.size()-p<32) throw std::runtime_error("etiqueta IW truncada");
  size_t n=0; while(n<32 && b[p+n]) ++n;
  if(n==32) throw std::runtime_error("etiqueta IW sin terminador");
  return std::string(reinterpret_cast<const char*>(b.data()+p),n);
}
// EU1372F0 compara desde el final hasta el separador '_' de ambos nombres.
// Los prefijos de los bancos pueden diferir; no casar por indice de hueso.
inline bool MismaEtiqueta(const std::string& a,const std::string& b) {
  auto i=a.size(),j=b.size();
  while(i && j) {
    if(a[--i]!=b[--j]) return false;
    if(i && j && a[i-1]=='_' && b[j-1]=='_') return true;
    if(i && !j && a[i-1]=='_') return true;
    if(j && !i && b[j-1]=='_') return true;
  }
  return i==j;
}
// AMP = selector de VARIANTE/visibilidad de cada nodo AMO (EU13BCF0/13BDF0),
// NO indices de textura. -1 oculta la geometria de ese nodo (13A1F0), sin
// eliminar su transform ni sus hijos. Canales sin etiqueta conservan estado.
struct SelectorAmp {
  std::vector<int> canales, seleccion;
  void Enlazar(const Bytes& amo,const Bytes& amp) {
    if(!Magic(amo,"#AMO") || !Magic(amp,"#AMP")) throw std::runtime_error("AMO/AMP invalido");
    const uint32_t n=U(amo,16),m=U(amp,24);
    if(n>256 || m>256 || uint64_t(U(amo,36))+32ull*n>amo.size() || uint64_t(U(amp,28))+32ull*m>amp.size())
      throw std::runtime_error("tabla de etiquetas AMP invalida");
    canales.assign(n,-1); seleccion.assign(n,0); // EU1386D0: seleccion inicial0.
    for(size_t i=0;i<n;++i) for(size_t j=0;j<m;++j)
      if(MismaEtiqueta(Etiqueta(amo,U(amo,36)+32*i),Etiqueta(amp,U(amp,28)+32*j))) { canales[i]=int(j); break; }
  }
  void Muestrear(const Bytes& amp,uint32_t clip,float cuadro) {
    if(!std::isfinite(cuadro) || clip>=U(amp,16) || canales.size()!=seleccion.size()) throw std::runtime_error("muestra AMP invalida");
    const size_t tabla=U(amp,U(amp,20)+16*size_t(clip)+12);
    for(size_t h=0;h<canales.size();++h) if(canales[h]>=0) {
      const size_t p=U(amp,tabla+4*size_t(canales[h]));
      const uint32_t n=U(amp,p+8);
      if(!p || !n || uint64_t(p)+12+4ull*n>amp.size()) throw std::runtime_error("pista AMP invalida");
      // Misma busqueda binaria EU1370A0 (incluye su comportamiento antes de1).
      int i=0;
      if(cuadro>=1) {
        i=int(n>>1); uint32_t rango=n;
        while(((rango+1)>>1)>=2) {
          const uint32_t paso=(rango+1)>>2; rango=(rango+1)>>1;
          if(i<0 || uint32_t(i)>=n) throw std::runtime_error("indice AMP invalido");
          i+=cuadro<float(S16(amp,p+12+4*size_t(i))) ? -int(paso) : int(paso);
        }
        if(i<0 || uint32_t(i)>=n) throw std::runtime_error("indice AMP invalido");
        if(cuadro<float(S16(amp,p+12+4*size_t(i)))) --i;
      }
      if(i<0 || uint32_t(i)>=n) throw std::runtime_error("cuadro AMP anterior a primera clave");
      seleccion[h]=S16(amp,p+14+4*size_t(i));
    }
  }
  // Consumidor de geometria del corpus993/994: todas sus selecciones son0/-1.
  // Usa la malla BASE de la pose, nunca la ya filtrada del cuadro anterior.
  // Una variante >0 exige cargar esa variante AMO: no fingir que es textura.
  template<class Malla> bool Aplicar(const Malla& base,Malla& salida) const {
    if(&base==&salida) return false;
    Malla nueva;
    for(const auto& t:base.tris) {
      const size_t h=t.hueso[0]; if(h>=seleccion.size()) return false;
      if(seleccion[h]>0) return false;
      if(seleccion[h]==0) nueva.tris.push_back(t);
    }
    salida=std::move(nueva); return true;
  }
};
struct Enlace { uint32_t id, funcion; };
inline constexpr std::array<Enlace,33> kFunciones = {{{1,0x284E60},{0x11,0x284E10},
  {0x40,0x284D60},{0x42,0x2843C0},{0x44,0x2841E0},{0x45,0x2841A0},
  {0x46,0x2834C0},{0x47,0x2832F0},{0x48,0x284910},{0x50,0x283DA0},
  {0x60,0x2838A0},{0x62,0x283670},{0x63,0x283610},{0x67,0x2835D0},
  {0x71,0x2830B0},{0x72,0x283000},{0x73,0x282FD0},{0x75,0x283130},
  {0x90,0x282F80},{0x93,0x282C60},{0x94,0x282B90},{0x95,0x282AB0},
  {0x96,0x282930},{0x97,0x282880},{0xA0,0x2826D0},{0xA1,0x282670},
  {0xA3,0x2825C0},{0xD4,0x2796B0},{0xD5,0x279630},{0xD6,0x279560},
  {0x116,0x364EF0},{0x118,0x281E80},{0x119,0x364ED0}}};

struct Recurso { uint32_t id = 0; Bytes bytes; };
struct Actor {
  int modelo = -1, texturas = -1;
  std::array<int,2> movimiento{{-1,-1}}, material{{-1,-1}};
  uint32_t param_c8 = 0, param_cc = 0, clip = 0;
  float cuadro = 0, niebla = 0;
  SelectorAmp amp;
  uint32_t clip_amp = 0; float cuadro_amp = 0;
};
struct Luz {
  int recurso = -1; uint32_t flags = 0, clip = 0; float cuadro = 0, velocidad = 1;
  std::array<float,4> direccion{}, color{};
  bool valida = false;
};
// Matrices enviadas al GS/VU por133F10,12C170,12C070: tres luces como
// maximo (CapsulePick solo enlaza dos), mas ambiente. Los colores no se
// convierten a tonos B3 ni se aplica un multiplicador empirico al modelo.
struct MatricesLuz {
  std::array<std::array<float,4>,4> direcciones{},colores{};
};
inline MatricesLuz PrepararLuces(const std::array<Luz,2>& luces,const std::array<float,4>& ambiente) {
  MatricesLuz m; std::vector<const Luz*> activas;
  for(const auto& l:luces) if(l.flags&3) {
    if(!l.valida) throw std::runtime_error("AML activada sin muestra");
    activas.push_back(&l);
  }
  std::stable_sort(activas.begin(),activas.end(),[](const Luz* a,const Luz* b){return a->color[3]>b->color[3];});
  for(size_t i=0;i<activas.size();++i) {
    for(size_t j=0;j<3;++j) m.direcciones[i][j]=-activas[i]->direccion[j]; //12C070
    m.colores[i]=activas[i]->color;
  }
  m.direcciones[3][3]=1; m.colores[3]=ambiente;
  return m;
}
struct EstadoGS {
  std::array<uint32_t,2> mascara{{~0u,~0u}},valor{}; //12C3F0/ELF EU49BE20/24
  std::array<uint64_t,2> alpha{{0x44,0x44}};
  std::array<std::array<float,4>,2> color{{{{1,1,1,1}},{{1,1,1,1}}}};
  float expansion = .1f; //263880 y valor inicial EU49BE7C
  void Cambiar(uint32_t quitar,uint32_t poner,uint32_t xor1,uint32_t poner2,uint32_t xor2) {
    for(size_t i=0;i<2;++i) {
      mascara[i]=((mascara[i]&~quitar)|poner)^xor1;
      valor[i]=((valor[i]&~quitar)|poner2)^xor2;
      mascara[i]|=7; valor[i]&=~7u; //12C4E0
    }
  }
};
// Secuencia REAL284520 con globals de inicio263880 (flags49CB28=0).
// Primer draw usa DOS contextos; el segundo lleva el color negro de458EB0 y
// expansion0.025. No equivale a repetir el modelo con alfa ni a niebla lineal.
inline std::array<EstadoGS,2> PasesNieblaGS(EstadoGS s) {
  s.Cambiar(0x20000000,0,0,0x20000000,0); s.alpha={0x44,0x42};
  const auto primero=s;
  s.Cambiar(0,0x20000000,0,0,0); s.alpha[1]=0x44;
  s.color={{{{1,1,1,1}},{{1,1,1,1}}}}; //263230
  s.expansion=.025f; s.color={{{{0,0,0,1}},{{0,0,0,1}}}}; //263460
  s.Cambiar(0x13c00720,0,0,0x10400200,0);
  return {primero,s};
}
struct Camara {
  int recurso = -1; uint32_t clip = 0; float cuadro = 0, velocidad = 1;
  std::array<float,3> ojo{}, objetivo{}; float giro = 0, fov = 0; bool valida = false;
};
struct Rectangulo {
  std::array<float,4> color{}, destino{};
  std::array<float,2> tam{}, centro{};
  std::array<float,4> rect{};
  float z = 0, demora = 0;
  uint32_t flags = 0, alpha = 0x44, test = 0;
};
// Un RPC aceptado debe PREPARAR los bancos reales; true no es un temporizador
// inventado. Ocupado sondea al consumidor IOP. No se supone que71 sea camara.
struct BancoIOP { int cuerpo = -1, indice = -1, secuencia = -1; bool preparado = false; };
struct BancoParticulas {
  int modelo = 33, texturas = 34, definicion = 35; //CMN974/973/972 de la tabla45.
  bool arranque_solicitado = false;  //2806E0: Effect_SetSlotCallbacks/SpawnAuraDash.
  size_t operaciones_consumidas = 0;
};
// EU279560=DIBUJO (277630), 279630=UPDATE (277830), 2796B0=RETIRAR.
// El nombre historico SetTimerInfinite es enganoso: +48=-1 deja de dibujar
// y permite devolver el objeto al pool; NO mantiene particulas infinitas.
struct ObjetoAme {
  virtual ~ObjetoAme() = default;
  virtual bool Dibujar() = 0;
  virtual bool Regular() = 0;
  virtual bool Retirar() = 0;
  virtual int Nodos() const = 0;
};
struct CicloAme {
  std::array<std::shared_ptr<ObjetoAme>,16> slots;
  bool Instalar(int slot,std::shared_ptr<ObjetoAme> objeto) {
    if(slot<0 || slot>=16 || !objeto) return false;
    auto& anterior=slots[size_t(slot)];
    if(anterior && !anterior->Retirar()) return false;
    anterior=std::move(objeto); return true;
  }
  bool Consumir(uint32_t id,int slot,bool pausa) {
    if(slot<0 || slot>=16 || (id!=0xD4 && id!=0xD5 && id!=0xD6)) return false;
    auto& a=slots[size_t(slot)];
    if(id==0xD4) { if(a && !a->Retirar()) return false; a.reset(); return true; }
    if(pausa || !a) return true; //49CB70, no espera inventada ni spawn implicito.
    if(id==0xD6) return a->Dibujar();
    if(!a->Regular()) return false;
    if(a->Nodos()<0) { if(!a->Retirar()) return false; a.reset(); }
    return true;
  }
};

// El motor reducido iw_ame no implementa la cadena de particulas +24 de972.
// Comprobar los DATOS antes de aceptarlo como consumidor fiel. El adaptador
// puede corregir la vida negativa a FLT_MAX; no puede ignorar los hijos20000.
inline std::string LimiteMotorAme(const Bytes& b) {
  if(!Magic(b,"#AME") || U(b,4)!=2) return "AME CapsulePick no valido";
  std::map<uint32_t,bool> vistos;
  std::function<std::string(uint32_t)> recorrer=[&](uint32_t q) -> std::string {
    while(q) {
      if(vistos[q]) return "ciclo en AME";
      vistos[q]=true;
      if(uint64_t(q)+0x60>b.size()) return "nodo AME truncado";
      const uint32_t tipo=U(b,q+4),clase=tipo>>16,hijo=U(b,q+0x24);
      if((clase==1 && (tipo&65535)>3) || (clase==2 && (tipo&65535)>3) || clase>3) return "tipo AME sin consumidor";
      if(clase==2 && hijo && (U(b,size_t(hijo)+4)>>16)==2)
        return "AME972: cadena de particulas20000/+24 no soportada por iw_ame";
      if(clase==2 && (U(b,q+8)&0x80000)) return "AME972: cuadro de atlas inicial aleatorio sin consumidor";
      if(hijo) { const auto error=recorrer(hijo); if(!error.empty()) return error; }
      q=U(b,q+0x28);
    }
    return {};
  };
  return recorrer(U(b,12));
}
struct Trabajo {
  uint64_t generacion = 0;
  float velocidad = 0, cuadro = 0, fin = 0;
  int vueltas = 0, slot = 0, clase = 0; uint32_t entrada = 0;
  int dibujar_actor = -1, dibujar_rect = -1;
  bool sin_niebla = false, auxiliar = false, comun = false;
  float u = 0, v = 0, du = 0, dv = 0; int uv_restante = 0;
};
class Escena;
struct Consumidores {
  std::function<void(const std::string&)> log;
  std::function<bool(const Escena&, bool)> preparar_iop; // false=C/0x11; true=AB/0x17
  std::function<bool()> iop_ocupado;
  std::function<bool(uint32_t)> parar_iop; // nombre antiguo: RPC13 INICIA secuencia (AMSOUND3690).
  std::function<bool(const Escena&, int, uint32_t, float, bool)> pose; // bool=AMP
  std::function<bool(const Escena&, int, const Luz&)> luz;
  std::function<bool(const Escena&, int, const Actor&, const Trabajo&)> actor;
  std::function<bool(const Escena&, int, const Rectangulo&)> rectangulo;
  std::function<bool(const Escena&, uint32_t, int)> particulas; //279560/630/6B0
  std::function<bool(int)> transicion;
  // Captura en el callback posterior ORIGINAL de cada task. Las clausuras
  // contienen pose/camara/material de ESE instante, no el estado al fin del VM.
  using Pase = std::function<bool()>;
  std::function<Pase(const Escena&,int,const Actor&,const Trabajo&)> capturar_actor;
  std::function<Pase(const Escena&,int,const Rectangulo&)> capturar_rectangulo;
};

// Muestras LINEALES de AMC/AML: 127280 y1342B0 interpolan entre registros
// {u32 cuadro, float[dim]}; sin inventar trayectorias o encuadres.
inline std::vector<float> Pista(const Bytes& b, uint32_t p, int dim, float cuadro) {
  const uint32_t n=U(b,p+8);
  if (!n || uint64_t(p)+12+uint64_t(n)*4*(dim+1)>b.size()) throw std::runtime_error("pista IW invalida");
  uint32_t i=0;
  const size_t paso=4*size_t(dim+1);
  while(i+1<n && cuadro>=float(U(b,p+12+paso*(i+1)))) ++i;
  const size_t a=p+12+paso*i;
  float t=0;
  if(i+1<n) { const float dt=float(U(b,a+paso))-float(U(b,a)); if(dt<=0) throw std::runtime_error("tiempos IW no crecientes"); t=std::clamp((cuadro-float(U(b,a)))/dt,0.f,1.f); }
  std::vector<float> out(size_t(dim),0.f);
  for(int j=0;j<dim;++j) out[size_t(j)]=F(b,a+4+4*size_t(j))*(1-t)+(t ? F(b,a+paso+4+4*size_t(j))*t : 0);
  return out;
}

// EU134230 ->1342B0, AML tipo10 de los cuatro bancos989..992. El GS usa
// direccion NORMALIZADA y luminancia en color.w, no el vector crudo del AML.
inline void MuestrearLuz(const Bytes& b,Luz& l) {
  if(!Magic(b,"#AML") || l.clip>=U(b,16) || !std::isfinite(l.cuadro)) throw std::runtime_error("AML invalido");
  const size_t desc=U(b,20)+16*size_t(l.clip);
  if(U(b,desc)!=10 || !U(b,desc+8)) throw std::runtime_error("tipo AML no soportado");
  const size_t pistas=U(b,desc+12);
  const float f=std::min(l.cuadro,float(U(b,desc+8)-1));
  const auto d=Pista(b,U(b,pistas),3,f),c=Pista(b,U(b,pistas+4),3,f);
  const float largo=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
  if(!std::isfinite(largo) || largo==0) throw std::runtime_error("direccion AML degenerada");
  l.direccion={d[0]/largo,d[1]/largo,d[2]/largo,1/largo};
  l.color={c[0],c[1],c[2],c[0]*.299f+c[1]*.587f+c[2]*.114f}; l.valida=true;
}

// Transporte EU13FA20 -> AMSOUND RPC13 (3690 -> MODSESQ Start, argumento1).
// RPC14/3838 es stop. Conservar Parar solo como alias de la API antigua;
// no conectarlo a la cancelacion de un canal ADX: RPC13 INICIA la secuencia.
struct RpcIOP {
  std::function<int(uint32_t,uint32_t,const Bytes&)> enviar;
  bool Iniciar(uint32_t secuencia) const {
    if(!enviar) return false;
    Bytes b(4); for(unsigned i=0;i<4;++i) b[i]=uint8_t(secuencia>>(8*i));
    return enviar(0x13,3,b)>=0;
  }
  bool Parar(uint32_t secuencia) const { return Iniciar(secuencia); } // compatibilidad; nombre obsoleto.
};

// EU281E80/27DA10/27D700/27B5C0. Conserva el progreso heredado al iniciar;
// tipo10 es un quad NEGRO alpha0x44, no un cambio inmediato de pantalla.
// El consumidor externo debe dibujar color (floats0..1), aplicar RPC volumen
// y detener los ADX heredados (ToDefine126880) al cruzar1. No mata el VM.
struct Fundido10 {
  float valor = 0, velocidad = 0;
  bool iniciado = false, avanzando = false;
  struct Paso {
    std::array<float,4> color{};
    uint32_t alpha = 0x44, flags = 0x900;
    bool liberar_anterior = false, enviar_volumen = false;
    float volumen = 0;
  };
  bool Iniciar() {
    if(!std::isfinite(valor) || !std::isfinite(velocidad) || valor<0 || valor>1 || velocidad<0) return false;
    iniciado=avanzando=true; return true;
  }
  Paso Actualizar() {
    if(!iniciado) throw std::runtime_error("transicion10 no iniciada");
    Paso p;
    if(avanzando) {
      p.enviar_volumen=true;
      const float siguiente=valor+velocidad;
      if(siguiente>=1) { p.liberar_anterior=valor<1; valor=1; avanzando=false; }
      else valor=siguiente;
    }
    p.color={0,0,0,valor}; p.volumen=1-valor; return p;
  }
};

// Desde una pose/seleccion sin modificar: GS REPEAT se aplica al muestrear,
// no a cada vertice (envolver vertices rompe la interpolacion del triangulo).
template<class Malla> void AplicarUV(const Malla& base,Malla& salida,float u,float v) {
  if(&base==&salida) throw std::runtime_error("UV requiere una pose base distinta");
  if(!std::isfinite(u) || !std::isfinite(v)) throw std::runtime_error("UV no finito");
  salida=base;
  for(auto& t:salida.tris) for(auto& q:t.uv) { q.x+=u; q.y+=v; }
}

class Escena {
 public:
  Instancia vm;
  Consumidores host;
  std::array<Recurso,45> recursos;
  std::array<Actor,4> actores;
  std::array<Luz,2> luces;
  Camara camara;
  std::array<Rectangulo,16> rects;
  std::array<Trabajo,32> trabajos;
  BancoIOP audio;
  BancoParticulas efectos;
  std::array<float,4> ambiente{};
  float reloj = 0, paso = 1;
  std::array<float,2> uv_global{}; // EU4C4E38/78, comun a TODOS los draws.
  uint16_t botones = 0;
  bool listas = false, salida = false;
  int guion = 0;
  uint32_t ultimo_id = 0, ultima_funcion = 0;
  std::map<uint32_t,size_t> llamadas;
  size_t dibujos = 0;
  bool bloqueo_informado = false;
  bool capturar_pases = false;
  std::vector<Consumidores::Pase> pases;

  static Configuracion ConfigIW() {
    Configuracion c; c.ranuras=32;
    c.bytes_compartida=c.bytes_locales=c.bytes_temporal=0x80; c.bytes_pila=0x100;
    return c;  // iw_sles55347.c:330693, READ_GameFolders; argumentos originales.
  }
  Escena() : vm(ConfigIW()) {
    vm.ganchos.crear=[this](Instancia&,Hilo& h,const Hilo*) {
      trabajos[size_t(h.ranura)]={}; trabajos[size_t(h.ranura)].generacion=h.generacion;
      h.posterior=[this](Instancia&,Hilo& t) { if(capturar_pases) CapturarTrabajo(t); };
    };
    vm.traza=[this](const Traza& t) {
      if(t.fase==Traza::Fase::Error && !bloqueo_informado) {
        bloqueo_informado=true;
        Informar("BLOQUEO cuadro="+std::to_string(t.cuadro)+" pc="+Hex(t.pc)+" builtin="+Hex(ultimo_id)+" funcion="+Hex(ultima_funcion)+": "+vm.Error());
      }
    };
    for(const auto e:kFunciones) vm.Registrar(kCapsulasEU,e.id,[this,e](Instancia&,Hilo& h) {
      Identificar(e.id); ++llamadas[e.id]; return Ejecutar(e.id,h);
    });
  }
  const Bytes& Banco(int indice) const {
    if(indice<0 || indice>=45 || recursos[size_t(indice)].bytes.empty()) throw std::runtime_error("banco CapsulePick ausente");
    return recursos[size_t(indice)].bytes;
  }
  // Lee la tabla DEL ELF, no una copia de sus45 archivos. El patron incluye
  // las dos SPX, ambos AMC y cuatro AML; un candidato ambiguo se rechaza.
  bool Cargar(const Bytes& elf, const std::function<Bytes(uint32_t)>& leer, int id) {
    try {
      if(id!=993 && id!=994) throw std::runtime_error("solo SPX993/994");
      size_t tabla=elf.size();
      for(size_t p=0;p+45*4<=elf.size();p+=4) {
        if(U(elf,p)!=993 || U(elf,p+4)!=994 || U(elf,p+8)!=975 || U(elf,p+12)!=976 || U(elf,p+16)!=989 || U(elf,p+28)!=992) continue;
        if(tabla!=elf.size()) throw std::runtime_error("tabla45 ambigua");
        tabla=p;
      }
      if(tabla==elf.size()) throw std::runtime_error("tabla45 no encontrada");
      for(size_t i=0;i<45;++i) {
        recursos[i]={U(elf,tabla+i*4),{}};
        recursos[i].bytes=leer(recursos[i].id);
        if(recursos[i].bytes.empty()) throw std::runtime_error("recurso45 ausente: "+std::to_string(recursos[i].id));
      }
      guion=id;
      //2806E0 ->993 (d8); 280500 ->994 (dc). Offsets respecto al trabajo+24.
      auto actor=[&](int s,int amo,int amt,int amm,int amp=-1) { actores[size_t(s)].modelo=(amo-0x24)/4; actores[size_t(s)].texturas=(amt-0x24)/4; actores[size_t(s)].movimiento[0]=(amm-0x24)/4; if(amp>=0) actores[size_t(s)].material[0]=(amp-0x24)/4; };
      if(id==993) {
        actor(0,0x60,0x64,0x68); actor(1,0x48,0x4C,0x50); actor(2,0x78,0x7C,0x80,0x84);
        camara.recurso=2; luces[0].recurso=4; luces[1].recurso=5; audio={39,40,41,false};
        efectos.arranque_solicitado=true;
      } else {
        actor(0,0x6C,0x70,0x74); actor(1,0x54,0x58,0x5C); actor(2,0x88,0x8C,0x90,0x94); actor(3,0x98,0x9C,0xA0,0xA4);
        camara.recurso=3; luces[0].recurso=6; luces[1].recurso=7; audio={42,43,44,false};
      }
      // Crear los bancos del renderer, incluso antes de que un hilo los anime.
      for(const auto& a:actores) if(a.modelo>=0 && (!Magic(Banco(a.modelo),"#AMO") || !Magic(Banco(a.texturas),"#AMT"))) throw std::runtime_error("AMO/AMT no validos");
      for(auto& a:actores) if(a.modelo>=0 && a.material[0]>=0) a.amp.Enlazar(Banco(a.modelo),Banco(a.material[0]));
      // Char_SetEffectFileForSlot ->133DF0 crea la luz ANTES del primer
      // AML60: objeto a cero salvo posicion.w/direccion.w=1. El SPX puede
      // habilitar63 y dibujar antes de que el task60 tome su primera muestra.
      for(auto& l:luces) { l.direccion={0,0,0,1};l.color={0,0,0,0};l.valida=true; }
      MuestrearCamara(0,0);
      Informar("SPX"+std::to_string(id)+":45/45 recursos, tabla ELF="+std::to_string(tabla)+"; AMC/AML/AMM, sin AMA; 33 builtins registrados");
      return vm.Cargar(Banco(id==993?0:1),kCapsulasEU) && vm.Iniciar()==0;
    } catch(const std::exception& e) { Bloquear(e.what()); return false; }
  }
  bool Tick(uint16_t entrada, float delta=1) {
    botones=entrada; paso=delta;
    if(!std::isfinite(paso) || paso<0) { Bloquear("paso IW invalido"); return false; }
    if(capturar_pases) pases.clear();
    return vm.Tick();
  }
  void EncolarPase(Consumidores::Pase pase) {
    Necesita(bool(pase),"captura GS/AME sin consumidor"); pases.push_back(std::move(pase));
  }
  // Dibujo real desde los tasks que los nativos activaron; no recorrer el VM
  // offline ni dibujar una escena prefabricada en funcion del tiempo.
  bool Dibujar() {
    try {
      if(capturar_pases) {
        for(const auto& p:pases) { Necesita(p(),"pase GS/AME capturado no soportado"); ++dibujos; }
        return true;
      }
      for(size_t i:vm.OrdenVivo()) {
        const auto& w=trabajos[i];
        if(w.generacion!=vm.HiloEn(i).generacion || !vm.HiloEn(i).activo) continue;
        if(w.dibujar_actor>=0) {
          Necesita(bool(host.actor),"consumidor AMO/AMM de dibujo ausente");
          auto pase=w; pase.u=uv_global[0]; pase.v=uv_global[1];
          Necesita(host.actor(*this,w.dibujar_actor,actores.at(size_t(w.dibujar_actor)),pase),"consumidor AMO: pase/niebla/material no soportado"); ++dibujos;
        }
        if(w.dibujar_rect>=0) {
          Necesita(bool(host.rectangulo),"consumidor de rectangulo ausente");
          Necesita(host.rectangulo(*this,w.dibujar_rect,rects.at(size_t(w.dibujar_rect))),"modo GS de rectangulo no soportado"); ++dibujos;
        }
      }
      return true;
    } catch(const std::exception& e) { Bloquear(e.what()); return false; }
  }
  void Bloquear(const std::string& e) {
    if(!vm.Bloqueada()) {
      try { vm.Fallar(e); } catch(const std::exception&) {}
    }
    if(!bloqueo_informado) { bloqueo_informado=true; Informar("BLOQUEO SPX"+std::to_string(guion)+" cuadro="+std::to_string(vm.Cuadro())+" builtin="+Hex(ultimo_id)+" funcion="+Hex(ultima_funcion)+": "+e); }
  }

 private:
  void CapturarTrabajo(const Hilo& h) {
    const auto& w=trabajos.at(size_t(h.ranura));
    if(w.generacion!=h.generacion) return;
    if(w.dibujar_actor>=0) {
      Necesita(bool(host.capturar_actor),"captura AMO/AMM ausente");
      auto pase=w; pase.u=uv_global[0]; pase.v=uv_global[1];
      EncolarPase(host.capturar_actor(*this,w.dibujar_actor,actores.at(size_t(w.dibujar_actor)),pase));
    }
    if(w.dibujar_rect>=0) {
      Necesita(bool(host.capturar_rectangulo),"captura rectangulo GS ausente");
      EncolarPase(host.capturar_rectangulo(*this,w.dibujar_rect,rects.at(size_t(w.dibujar_rect))));
    }
  }
  static std::string Hex(uint32_t a) { std::ostringstream s; s<<"0x"<<std::hex<<a; return s.str(); }
  void Informar(const std::string& e) const { if(host.log) host.log(e); }
  void Necesita(bool ok,const char* e) {
    if(!ok) {
      if(vm.Bloqueada()) throw std::runtime_error(vm.Error());
      vm.Fallar(e);
    }
  }
  void Identificar(uint32_t id) {
    ultimo_id=id;
    for(const auto e:kFunciones) if(e.id==id) { ultima_funcion=e.funcion; return; }
  }
  uint32_t A(Hilo& h,int i) { return vm.Arg(h,uint32_t(i)); }
  float AF(Hilo& h,int i) { const float f=Float(A(h,i)); Necesita(std::isfinite(f),"float de builtin no finito"); return f; }
  int Indice(Hilo& h,int arg,int max) { const auto n=A(h,arg); Necesita(n<uint32_t(max),"indice nativo fuera de banco"); return int(n); }
  Trabajo& W(Hilo& h) { return trabajos.at(size_t(h.ranura)); }
  uint32_t Duracion(int recurso,uint32_t clip) {
    const auto& b=Banco(recurso); Necesita(clip<U(b,16),"clip fuera de banco");
    const uint32_t n=U(b,U(b,20)+16*size_t(clip)+8); Necesita(n>0,"clip sin duracion"); return n;
  }
  void Acumular(Hilo& h,int n) {
    const uint32_t p=h.r[14]+0x68;
    const int inicio=vm.Leer32(p+4) ? Signo(vm.Leer32(p)) : int(reloj);
    vm.Escribir32(p,uint32_t(inicio)+uint32_t(n)); vm.Escribir32(p+4,1);
  }
  void MuestrearCamara(uint32_t clip,float f) {
    const auto& b=Banco(camara.recurso); const float fin=float(Duracion(camara.recurso,clip)-1);
    const uint32_t p=U(b,U(b,20)+16*size_t(clip)+12);
    const auto o=Pista(b,U(b,p),3,std::min(f,fin)), d=Pista(b,U(b,p+4),3,std::min(f,fin));
    std::copy(o.begin(),o.end(),camara.ojo.begin()); std::copy(d.begin(),d.end(),camara.objetivo.begin());
    camara.giro=Pista(b,U(b,p+8),1,std::min(f,fin))[0];
    camara.fov=std::clamp(Pista(b,U(b,p+12),1,std::min(f,fin))[0],0.08726647f,3.0543263f);
    camara.clip=clip; camara.cuadro=f; camara.valida=true;
  }
  // Despues del sondeo de ultima vuelta, el original retorna1 ANTES de aplicar
  // otra pose. Los contadores -1 y el caso vueltas==1 tienen reglas distintas.
  bool Avanzar(Trabajo& w,bool cam_o_luz) {
    if(vm.Estado()==3) return false;
    w.cuadro+=w.clase==0x50?camara.velocidad:w.clase==0x60?luces[size_t(w.slot)].velocidad:w.velocidad;
    if(w.vueltas==1) {
      const bool fin=cam_o_luz ? w.cuadro>w.fin-1 || w.cuadro<0 : w.cuadro>w.fin-1;
      if(fin) { w.cuadro+=w.cuadro<0 ? w.fin-1 : -(w.fin-1); --w.vueltas; return true; }
    } else if(cam_o_luz ? w.cuadro>w.fin-1 : w.cuadro>=w.fin) {
      w.cuadro-=w.fin;
      if(cam_o_luz && w.cuadro<0) w.cuadro=0;
      if(w.vueltas!=-1 && --w.vueltas<1) return true;
    } else if(cam_o_luz && w.cuadro<0) {
      w.cuadro=std::min(w.fin-1,w.cuadro+w.fin);
      if(w.vueltas!=-1 && --w.vueltas<1) return true;
    }
    return false;
  }
  uint32_t SondearAnim(Hilo& h) {
    auto& w=W(h);
    Identificar(uint32_t(w.clase));
    if(vm.Estado()==3 && (w.clase==0x42 || w.clase==0x46)) return 3;
    if(Avanzar(w,w.clase==0x50 || w.clase==0x60)) { h.sondeo={}; return 1; }
    if(w.clase==0x50) MuestrearCamara(w.entrada,w.cuadro);
    else if(w.clase==0x60) {
      auto& l=luces[size_t(w.slot)]; l.clip=w.entrada; l.cuadro=w.cuadro;
      MuestrearLuz(Banco(l.recurso),l);
      Necesita(bool(host.luz),"consumidor AML134230 ausente"); Necesita(host.luz(*this,w.slot,l),"AML no aplicado por el host");
    } else {
      auto& a=actores[size_t(w.slot)];
      if(w.clase==0x46) {
        a.clip_amp=w.entrada; a.cuadro_amp=w.cuadro;
        a.amp.Muestrear(Banco(a.material.at(size_t((w.entrada>>16)-1))),w.entrada&65535,w.cuadro);
      } else { a.clip=w.entrada; a.cuadro=w.cuadro; }
      Necesita(bool(host.pose),"consumidor de pose AMM/AMP ausente"); Necesita(host.pose(*this,w.slot,w.entrada,w.cuadro,w.clase==0x46),"pose/material no aplicado por el host");
    }
    return 3;
  }
  uint32_t EmpezarAnim(uint32_t id,Hilo& h) {
    auto& w=W(h); w.clase=int(id); w.vueltas=Signo(A(h,0)); w.entrada=A(h,1);
    if(id==0x42 || id==0x46) {
      w.slot=Indice(h,3,4); const int banco=Indice(h,2,2);
      const int r=id==0x42?actores[size_t(w.slot)].movimiento[size_t(banco)]:actores[size_t(w.slot)].material[size_t(banco)];
      w.fin=float(Duracion(r,w.entrada)); w.entrada|=uint32_t(banco+1)<<16; w.cuadro=-w.velocidad;
      if(id==0x42 || id==0x46) w.vueltas=int(int8_t(uint8_t(w.vueltas)));
    } else {
      w.slot=Indice(h,2,id==0x50?1:2);
      const int r=id==0x50?camara.recurso:luces[size_t(w.slot)].recurso;
      w.fin=float(Duracion(r,w.entrada));
      const float v=id==0x50?camara.velocidad:luces[size_t(w.slot)].velocidad;
      w.cuadro=v>=0?0:w.fin-1;
    }
    Acumular(h,int(w.fin));
    h.sondeo=[this](Instancia&,Hilo& t) { return SondearAnim(t); };
    if(id==0x50) MuestrearCamara(w.entrada,w.cuadro);
    if(id==0x60) {
      auto& l=luces[size_t(w.slot)]; l.clip=w.entrada; l.cuadro=w.cuadro;
      MuestrearLuz(Banco(l.recurso),l);
      Necesita(bool(host.luz),"consumidor AML134230 ausente"); Necesita(host.luz(*this,w.slot,l),"AML no aplicado por el host");
    }
    return (id==0x50 || id==0x60)?3:SondearAnim(h);
  }
  void RectPos(Rectangulo& r,float x,float y,float z,bool redimensionar,bool situar=true) {
    r.centro={x,y}; r.z=z;
    for(int i=0;i<2;++i) {
      const bool c=(r.flags&(1u<<i))!=0;
      const float centro=r.centro[size_t(i)],tam=r.tam[size_t(i)];
      if(c) { const float mitad=0.5f*(tam+(redimensionar?0.f:1.f)); r.rect[size_t(i)]=centro-mitad; r.rect[size_t(i+2)]=centro+mitad; }
      else { if(situar) r.rect[size_t(i)]=centro; r.rect[size_t(i+2)]=r.rect[size_t(i)]+tam; }
    }
  }
  uint32_t SondearUV(Hilo& h) {
    auto& q=W(h);
    if(!q.uv_restante) { h.sondeo={}; return 1; }
    if(vm.Estado()!=3) {
      // EU283160 suma/resta1 en float; floor produce otros redondeos para
      // velocidades negativas pequenas. No usar fmod ni multiplicar por delta.
      auto wrap=[](float x) {
        if(!std::isfinite(x) || std::abs(x)>65536) throw std::runtime_error("paso UV fuera del rango verificado");
        while(x<0) x+=1; while(x>=1) x-=1; return x;
      };
      q.u=wrap(q.u+q.du); q.v=wrap(q.v+q.dv);
      if(q.uv_restante>0) --q.uv_restante;
      vm.Escribir32(h.r[14]+0x70,Bits(q.u)); vm.Escribir32(h.r[14]+0x74,Bits(q.v));
    }
    uv_global={q.u,q.v}; return 3;
  }
  uint32_t Ejecutar(uint32_t id,Hilo& h) {
    auto& w=W(h);
    switch(id) {
      case 1: return 1;  //284E60: return1 EXISTE en el original, no fallback.
      case 0x11: w.comun=true; return 1; // instala draw284DF0 ->1281D0.
      case 0x40: case 0x48: {
        const int s=Indice(h,2,4); const bool activar=A(h,1)!=0;
        if(activar) { w.dibujar_actor=s; w.sin_niebla=id==0x40; w.auxiliar=A(h,0)!=0; }
        else if(w.dibujar_actor==s) w.dibujar_actor=-1;
        return 1;
      }
      case 0x44: w.velocidad=AF(h,0)*paso; return 1;
      case 0x45: { auto& a=actores[size_t(Indice(h,2,4))]; a.param_c8=A(h,1); a.param_cc=A(h,0); return 1; }
      case 0x42: case 0x46: case 0x50: case 0x60: return EmpezarAnim(id,h);
      case 0x62: ambiente={AF(h,2),AF(h,1),AF(h,0),0}; return 1;
      case 0x63: { auto& l=luces[size_t(Indice(h,1,2))]; const uint32_t flags=A(h,0)&0xFFFF; if(flags&0x30) l.flags&=~0x30u; l.flags|=flags; return 1; }
      case 0x67: actores[size_t(Indice(h,1,4))].niebla=AF(h,0); return 1;
      case 0x47: {
        if(A(h,2)) {
          w.clase=0x47; w.uv_restante=Signo(A(h,2)); w.u=w.v=0; w.du=AF(h,1); w.dv=AF(h,0);
          vm.Escribir32(h.r[14]+0x70,0); vm.Escribir32(h.r[14]+0x74,0);
          vm.Escribir32(h.r[14]+0x78,Bits(w.du)); vm.Escribir32(h.r[14]+0x7C,Bits(w.dv));
          h.sondeo=[this](Instancia&,Hilo& t) { return SondearUV(t); };
          SondearUV(h); //283340 y28336C: DOS llamadas al arrancar (ELF EU).
        } else if(w.clase==0x47) h.sondeo={};
        return SondearUV(h);
      }
      case 0x71: case 0x72: {
        Indice(h,0,16);
        Necesita(bool(host.preparar_iop),"consumidor de carga IOP13EFA0/13ED70 ausente");
        if(host.preparar_iop(*this,id==0x72)) { audio.preparado=true; h.sondeo={}; return 1; }
        h.sondeo=[this,id](Instancia&,Hilo& t) { return Ejecutar(id,t); }; return 3;
      }
      case 0x73:
        Necesita(bool(host.iop_ocupado),"sondeo IOP49C748 ausente");
        if(host.iop_ocupado()) { h.sondeo=[this](Instancia&,Hilo& t) { return Ejecutar(0x73,t); }; return 3; }
        h.sondeo={}; return 1;
      case 0x75:
        Necesita(bool(host.parar_iop),"consumidor RPC13FA20/0x13 ausente"); Necesita(host.parar_iop(A(h,0)),"RPC13FA20 rechazado"); return 1;
      case 0x90: if(A(h,0)) w.dibujar_rect=Indice(h,1,16); else w.dibujar_rect=-1; return 1;
      case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        const int n=Indice(h,id==0x93?4:id==0x94?2:id==0x95?3:id==0x96?1:5,16);
        auto& r=rects[size_t(n)];
        if(id==0x93) r.color={AF(h,3),AF(h,2),AF(h,1),AF(h,0)};
        if(id==0x94) { r.tam={AF(h,1),AF(h,0)}; RectPos(r,r.centro[0],r.centro[1],r.z,true,false); }
        if(id==0x95) RectPos(r,AF(h,2),AF(h,1),AF(h,0),true);
        if(id==0x96) { r.flags=A(h,0); r.alpha=(r.flags&16)?0x48:0x44; r.test=(r.flags&32)?5:0; RectPos(r,r.centro[0],r.centro[1],r.z,false,false); }
        if(id==0x97) {
          r.destino={AF(h,3),AF(h,2),AF(h,1),AF(h,0)}; r.demora=AF(h,4); Acumular(h,int(r.demora)); w.slot=n;
          h.sondeo=[this](Instancia&,Hilo& t) {
            auto& a=rects[size_t(W(t).slot)]; if(a.demora<=0) { t.sondeo={}; return 1u; }
            const float k=vm.Estado()==3?0.f:std::min(1.f,1.f/a.demora);
            if(vm.Estado()!=3) a.demora-=paso;
            for(size_t j=0;j<4;++j) a.color[j]=a.destino[j]*k+a.color[j]*(1-k);
            return 3u;
          };
          auto fn=h.sondeo; return fn(vm,h);
        }
        return 1;
      }
      case 0xA0:
        reloj=0; h.sondeo=[this](Instancia&,Hilo&) { if(vm.Estado()!=3) reloj+=paso; return 3u; };
        if(vm.Estado()!=3) reloj+=paso; return 3;
      case 0xA1: h.r[0]=uint32_t(int(reloj+0.5f)); return 1;
      case 0xA3:
        if(int(AF(h,0))<=0) return 1;
        h.sondeo=[this](Instancia&,Hilo& t) { if(reloj<=AF(t,0)) return 3u; t.sondeo={}; return 1u; }; return 3;
      case 0xD4: case 0xD5: case 0xD6:
        Necesita(bool(host.particulas),"consumidor AME279560/630/6B0 ausente"); Necesita(host.particulas(*this,id,Indice(h,0,16)),"AME no aplicado por el host"); ++efectos.operaciones_consumidas; return 1;
      case 0x116: { const int k=Indice(h,0,2); h.r[0]=(botones&(k?1:8))!=0; return 1; }
      case 0x118: Necesita(bool(host.transicion),"consumidor transicion281E80 ausente"); Necesita(host.transicion(10),"transicion10 rechazada"); salida=true; return 1;
      case 0x119: listas=true; return 1;
      default: vm.Fallar("builtin fuera del inventario CapsulePick");
    }
  }
};
}  // namespace dbz3::iw::spx::capsulas

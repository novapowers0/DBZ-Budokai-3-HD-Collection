// Standalone C++17. Fixtures externos de instrucciones/datos retail;
// geometria y callbacks de estimulo no equivalen a una partida ni a audio SPU2.
#include "../src/iw_spx_capsulas.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
namespace C=dbz3::iw::spx::capsulas;
namespace S=dbz3::iw::spx;
namespace {
size_t checks=0,amp_frames=0,amp_values=0,mesh_before=0,mesh_after=0;
void Ver(bool ok,const std::string& why) { ++checks; if(!ok) throw std::runtime_error(why); }
C::Bytes Leer(const std::string& p) {
  std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("no se abre "+p);
  return C::Bytes(std::istreambuf_iterator<char>(f),{});
}
struct Datos {
  C::Bytes b; size_t p=0;
  uint32_t U() { const auto v=C::U(b,p);p+=4;return v; }
  C::Bytes Blob() { const auto n=U();Ver(p<=b.size() && n<=b.size()-p,"fixture truncado"); C::Bytes r(b.begin()+p,b.begin()+p+n);p+=n;return r; }
};
struct UV { float x=0,y=0; };
struct Tri { uint8_t hueso[3]{}; UV uv[3]; };
struct Malla { std::vector<Tri> tris; };
// Topologia y UV VIF del primer AMG: no emula pose/VU/GS. Sirve para probar
// que el consumidor elimina/restaura triangulos REALES, sin cambiar sus UV.
Malla Geometria(const C::Bytes& b) {
  Malla m; const size_t a=C::U(b,C::U(b,28)),ejes=a+C::U(b,a+20);
  for(uint32_t i=0;i<C::U(b,a+16);++i) {
    const size_t arm=C::U(b,ejes+80*i+0x34); if(!arm) continue;
    const size_t grupo=C::U(b,a+arm+4); if(!grupo) continue;
    const size_t g=a+grupo,tabla=g+C::U(b,g+4);
    for(uint32_t k=0;k<C::U(b,g);++k) {
      const size_t po=g+C::U(b,tabla+4*k); const uint32_t tipo=C::U(b,po),tam=C::U(b,po+0x90);
      if((tipo&65535)==65535 || tam<0x60000000 || tam>=0x70000000) continue;
      const size_t fin=po+0xA0+size_t(tam-0x60000000)*16;
      for(size_t p=po+0xA0;p+32<=fin;) {
        const uint32_t vif=C::U(b,p+12),ft=C::U(b,p+16),n=C::U(b,p+20),num=(vif>>16)&255;
        if(!n || num<1 || ((vif>>24)&127)!=0x6c) break;
        const size_t stride=size_t(num-1)*16/n;
        Ver(stride==16+(tipo&1?16:0)+(tipo&2?16:0)+(tipo&4?16:0),"stride VIF original");
        auto vert=[&](size_t index) {
          const size_t q=p+32+index*stride+16+(tipo&1?16:0)+(tipo&2?16:0);
          return (tipo&4)?UV{C::F(b,q),C::F(b,q+4)}:UV{};
        };
        auto tri=[&](size_t x,size_t y,size_t z) { Tri t; for(auto& h:t.hueso) h=uint8_t(i);t.uv[0]=vert(x);t.uv[1]=vert(y);t.uv[2]=vert(z);m.tris.push_back(t); };
        if(ft==1) for(size_t x=0;x+2<n;++x) tri(x,x+1,x+2);
        else for(size_t x=0;x+2<n;x+=3) tri(x,x+1,x+2);
        p+=32+size_t(n)*stride;
      }
    }
  }
  Ver(!m.tris.empty(),"geometria VIF no vacia"); return m;
}
void Amp(Datos& d) {
  const uint32_t cases=d.U();
  for(uint32_t c=0;c<cases;++c) {
    const auto amo=d.Blob(),amp=d.Blob();C::SelectorAmp a;a.Enlazar(amo,amp);
    const uint32_t n=d.U();Ver(a.canales.size()==n,"tamano bind AMP");
    for(size_t h=0;h<n;++h) Ver(a.canales[h]==S::Signo(d.U()),"bind AMP vs13BCF0");
    const auto base=Geometria(amo);const uint32_t frames=d.U();std::set<size_t> visibles;
    for(uint32_t f=0;f<frames;++f) {
      const float frame=S::Float(d.U());a.Muestrear(amp,0,frame);std::vector<int> expected(n);
      for(size_t h=0;h<n;++h) {
        const int raw=S::Signo(d.U());expected[h]=a.canales[h]<0?0:raw;
        Ver(a.seleccion[h]==expected[h],"seleccion AMP vs13BDF0 frame="+std::to_string(frame)+" nodo="+std::to_string(h));++amp_values;
      }
      Malla out;Ver(a.Aplicar(base,out),"consumidor AMP real admite corpus0/-1");
      size_t kept=0;for(const auto& t:base.tris) if(expected[t.hueso[0]]>=0) ++kept;
      Ver(out.tris.size()==kept,"AMP altera topologia visible segun resultado retail");
      visibles.insert(kept);mesh_before+=base.tris.size();mesh_after+=kept;
      for(const auto& t:out.tris) Ver(expected[t.hueso[0]]==0,"ningun triangulo oculto llega al pase");
      ++amp_frames;
    }
    Ver(visibles.size()>1,"AMP restaura geometria: no filtrado acumulativo");
    std::cout<<"AMP"<<c<<" muestras="<<frames<<" nodos="<<n<<" topologias="<<visibles.size()<<'\n';
  }
}
struct Codigo {
  C::Bytes b;
  void I(uint32_t v) { b.push_back(0x30);for(unsigned j=0;j<4;++j)b.push_back(uint8_t(v>>(8*j))); }
  void N(uint32_t id,std::initializer_list<uint32_t> args={}) {
    for(auto it=args.end();it!=args.begin();) {b.push_back(8);I(*--it);}
    b.push_back(0x80);I(id);
    if(args.size()) {b.push_back(0x12);b.push_back(0x10);b.push_back(uint8_t(args.size()*4));b.push_back(0x0b);}
  }
  C::Bytes Guion() { C::Bytes r(36);std::memcpy(r.data(),"#SPX",4);r[20]=36;r[24]=1;r.insert(r.end(),b.begin(),b.end());return r; }
};
void Uv(Datos& d) {
  const auto cases=d.U();
  for(uint32_t c=0;c<cases;++c) {
    const auto ticks=d.U(),du=d.U(),dv=d.U();C::Escena e;Codigo code;
    code.b.push_back(0x98);code.I(0);code.I(1); // hijo posterior para dibujar.
    code.N(0x47,{dv,du,ticks});code.b.insert(code.b.end(),16,0x92);
    const auto child=uint32_t(code.b.size());for(unsigned j=0;j<4;++j)code.b[2+j]=uint8_t(child>>(8*j));
    code.N(0x40,{0,1,0});code.b.insert(code.b.end(),16,0x92);
    uint32_t ret=0;e.vm.traza=[&](const S::Traza& t){if(t.ranura==0&&((t.fase==S::Traza::Fase::Nativo&&t.codigo==0x47)||t.fase==S::Traza::Fase::Sondeo))ret=t.retorno;};
    Ver(e.vm.Cargar(code.Guion(),S::kCapsulasEU) && e.vm.Iniciar()==0,"crear estimulo UV");
    for(int i=0;i<4;++i) {
      e.vm.Pausar(i==2);Ver(e.Tick(0),"UV tick");
      const auto eret=d.U(),left=d.U();Ver(ret==eret,"retorno UV original");Ver(uint32_t(e.trabajos[0].uv_restante)==left,"contador UV original");
      for(size_t k=0;k<2;++k) Ver(e.vm.Leer32(e.vm.HiloEn(0).r[14]+0x70+uint32_t(k)*4)==d.U(),"memoria temporal UV original");
      for(size_t k=0;k<8;++k) Ver(S::Bits(e.uv_global[k/4])==d.U(),"UV global vector GS original");
      Ver(bool(e.vm.HiloEn(0).sondeo)==bool(d.U()),"sondeo UV original");
    }
    // Dibujo en OTRO trabajo: debe recibir el UV global, no su temporal vacio.
    Ver(e.trabajos[1].dibujar_actor==0 && e.trabajos[1].u==0,"dibuja el hijo, sin temporal UV propio");bool recibio=false;
    e.host.actor=[&](const C::Escena&,int,const C::Actor&,const C::Trabajo& w){recibio=w.u==e.uv_global[0]&&w.v==e.uv_global[1];return true;};
    Ver(e.Dibujar()&&recibio,"consumidor recibe UV global");
    Malla base;Tri t;t.uv[0]={.99f,.1f};t.uv[1]={1.01f,.2f};base.tris.push_back(t);Malla out;
    C::AplicarUV(base,out,e.uv_global[0],e.uv_global[1]);
    Ver(out.tris[0].uv[1].x==1.01f+e.uv_global[0] && base.tris[0].uv[0].x==.99f,"UV aplicado a vertices sin romper REPEAT ni acumular");
  }
}
void Rpc(Datos& d) {
  const auto n=d.U();C::RpcIOP rpc;Ver(!rpc.Parar(0),"sin transporte RPC falla");
  for(uint32_t i=0;i<n;++i) {
    const auto value=d.U(),status=d.U(),ret=d.U(),command=d.U(),mode=d.U(),size=d.U(),payload=d.U();bool enviado=false;
    rpc.enviar=[&](uint32_t c,uint32_t m,const C::Bytes& b) {Ver(c==command&&m==mode&&b.size()==size&&C::U(b,0)==payload,"RPC13 peticion coincide con instrucciones originales");enviado=true;return S::Signo(status);};
    Ver(rpc.Parar(value)==(ret==0) && enviado,"RPC13 resultado original, sin exito ficticio");
  }
}
void Transicion(Datos& d) {
  C::Fundido10 f;f.valor=S::Float(d.U());f.velocidad=S::Float(d.U());Ver(f.Iniciar(),"iniciar transicion10");
  Ver(S::Bits(f.valor)==d.U(),"27DA10 conserva progreso heredado");const auto n=d.U();
  for(uint32_t i=0;i<n;++i) {
    const auto p=f.Actualizar();Ver(S::Bits(f.valor)==d.U(),"27D700 progreso");
    for(float v:p.color)Ver(S::Bits(v)==d.U(),"27B5C0 color negro/alpha normalizado original");
    Ver(p.flags==d.U()&&p.alpha==d.U(),"GS flags900 y ALPHA44 originales");
    Ver(p.enviar_volumen==(i<6)&&p.liberar_anterior==(i==5),"transicion libera una vez, audio solo en avance");
  }
}
void Luces(Datos& d) {
  const auto n=d.U();
  for(uint32_t i=0;i<n;++i) {
    const auto b=d.Blob();C::Luz l;l.clip=d.U();l.cuadro=S::Float(d.U());C::MuestrearLuz(b,l);
    for(float v:l.direccion) Ver(std::abs(v-S::Float(d.U()))<2.e-6f,"AML direccion normalizada original");
    for(float v:l.color) Ver(std::abs(v-S::Float(d.U()))<2.e-6f,"AML color/luminancia original");
  }
}
void OrdenDibujo() {
  C::Escena e;Codigo c;c.b.push_back(0x98);c.I(0);c.I(0xffffffff);
  c.N(0x90,{1,0});c.b.insert(c.b.end(),8,0x92);
  const auto child=uint32_t(c.b.size());for(unsigned j=0;j<4;++j)c.b[2+j]=uint8_t(child>>(8*j));
  c.N(0x90,{1,1});c.b.insert(c.b.end(),8,0x92);
  Ver(e.vm.Cargar(c.Guion(),S::kCapsulasEU)&&e.vm.Iniciar()==0&&e.Tick(0)&&e.Tick(0),"dos tasks de dibujo");
  Ver(e.vm.OrdenVivo()==std::vector<size_t>{1,0},"orden140200 no coincide con las ranuras");
  std::vector<int> orden;e.host.rectangulo=[&](const C::Escena&,int s,const C::Rectangulo&){orden.push_back(s);return true;};
  Ver(e.Dibujar()&&orden==std::vector<int>{1,0},"GS recibe los pases en orden de prioridad/insercion");
  e.vm.Parar();Ver(e.vm.OrdenVivo().empty(),"tasks muertos no repiten draws");
}
void Bloqueos(Datos& d) {
  const auto elf=d.Blob();std::map<uint32_t,C::Bytes> bancos;const auto n=d.U();
  for(uint32_t i=0;i<n;++i) {const auto id=d.U();bancos[id]=d.Blob();}
  for(int id:{993,994}) {
    C::Escena sin;Ver(sin.Cargar(elf,[&](uint32_t k){return bancos.at(k);},id),"SPX real45 carga");
    Ver(!sin.Tick(0)&&sin.ultimo_id==0x71,"sin IOP no habilitar pantalla");
    C::Escena rpc;std::array<C::Bytes,3> copia;
    rpc.host.preparar_iop=[&](const C::Escena& e,bool ab){if(ab){copia[0]=e.Banco(e.audio.cuerpo);copia[1]=e.Banco(e.audio.indice);}else copia[2]=e.Banco(e.audio.secuencia);return true;};
    rpc.host.iop_ocupado=[](){return false;};
    Ver(rpc.Cargar(elf,[&](uint32_t k){return bancos.at(k);},id),"SPX real con copia durable");
    for(int f=0;f<1000 && !rpc.vm.Bloqueada();++f) rpc.Tick(0);
    std::cout<<"SPX"<<id<<" primer limite: builtin="<<std::hex<<rpc.ultimo_id<<std::dec<<" "<<rpc.vm.Error()<<'\n';
    Ver(rpc.vm.Bloqueada() && (rpc.ultimo_id==0x42||rpc.ultimo_id==0x46||rpc.ultimo_id==0x60),
        "guion real exige consumidor de pose/luz antes de continuar");
    Ver(!copia[0].empty()&&!copia[1].empty()&&!copia[2].empty(),"tres bancos originales cargados antes de RPC");
    std::cout<<"SPX"<<id<<" runtime pendiente: cuadro="<<rpc.vm.Cuadro()<<" "<<rpc.vm.Error()<<'\n';
  }
  C::Escena sin_rpc;Codigo code;code.N(0x75,{0});code.b.push_back(0x92);
  Ver(sin_rpc.vm.Cargar(code.Guion(),S::kCapsulasEU)&&sin_rpc.vm.Iniciar()==0,"estimulo75");
  Ver(!sin_rpc.Tick(0)&&sin_rpc.ultimo_id==0x75&&sin_rpc.vm.Error().find("RPC")!=std::string::npos,"75 sin backend bloquea, no opera sobre AME");
  C::Escena amp;Ver(amp.Cargar(elf,[&](uint32_t k){return bancos.at(k);},993),"recursos AMP para VM");amp.vm.Parar();
  Codigo anim;anim.N(0x44,{S::Bits(1)});anim.N(0x46,{1,0,0,2});anim.b.push_back(0x92);
  const auto base=Geometria(amp.Banco(amp.actores[2].modelo));Malla out;int poses=0;
  amp.host.pose=[&](const C::Escena& e,int slot,uint32_t entrada,float frame,bool material) {
    Ver(slot==2&&entrada==0x10000&&material,"46 entrega AMP, no AMM");
    Ver(e.actores[2].cuadro_amp==frame&&e.actores[2].cuadro==0,"relojes AMP/AMM separados");
    ++poses;return e.actores[2].amp.Aplicar(base,out);
  };
  Ver(amp.vm.Cargar(anim.Guion(),S::kCapsulasEU)&&amp.vm.Iniciar()==0&&amp.Tick(0),"46 consume geometria original en VM");
  const int antes=poses;const auto visible=out.tris.size();amp.vm.Pausar(true);
  Ver(amp.Tick(0)&&poses==antes&&out.tris.size()==visible,"46 en pausa no aplica ni avanza AMP");
  amp.vm.Pausar(false);Ver(amp.Tick(0)&&poses==antes+1&&amp.actores[2].cuadro_amp==1,"46 reanuda sin perder cuadro");
}
}
int main(int argc,char** argv) {
  try {
    if(argc!=2)throw std::runtime_error("uso: iw_capsulas_F_prueba fixture.bin");Datos d{Leer(argv[1])};Ver(d.U()==0x43465031,"firma fixture");
    Amp(d);Uv(d);Rpc(d);Transicion(d);Luces(d);OrdenDibujo();Bloqueos(d);Ver(d.p==d.b.size(),"fixture consumido entero");
    std::cout<<"PASS checks="<<checks<<" amp_frames="<<amp_frames<<" amp_values="<<amp_values<<" triangulos_antes="<<mesh_before<<" despues="<<mesh_after<<'\n';return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL checks="<<checks<<": "<<e.what()<<'\n';return 1;}
}

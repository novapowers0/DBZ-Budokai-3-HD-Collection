// AP: lote, lectores externos, AMK y matrices. Sin juego/SDK/dispositivos.
#include "../src/iw_ap_consumidores.h"
#include "../src/iw_ap_boca.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace dbz3::anim::apev;
namespace eventos=dbz3::anim::eventos;
void Exigir(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
std::vector<uint8_t> File(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary|std::ios::ate);Exigir(bool(f),"abrir recurso");const auto n=f.tellg();
  std::vector<uint8_t>b{std::vector<uint8_t>(size_t(n))};f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);return b;
}
std::vector<uint8_t> Entrada(const std::filesystem::path& p,unsigned id) {
  std::ifstream f(p,std::ios::binary);uint32_t magic=0,n=0;f.read(reinterpret_cast<char*>(&magic),4);f.read(reinterpret_cast<char*>(&n),4);
  Exigir(bool(f)&&magic==0x00534641 && id<n,"AFS/indice");uint32_t off=0,size=0;
  f.seekg(8+id*8);f.read(reinterpret_cast<char*>(&off),4);f.read(reinterpret_cast<char*>(&size),4);
  std::vector<uint8_t>b(size);f.seekg(off);f.read(reinterpret_cast<char*>(b.data()),size);Exigir(bool(f),"lectura AFS");return b;
}
std::vector<uint8_t> Hijo(const std::vector<uint8_t>& b,unsigned k) {
  if(U32(b,0)!=0x424d4123 || k>=U32(b,16))return {};
  size_t t=U32(b,20)+k*16,o=U32(b,t),n=U32(b,t+4);return Rango(b,o,n)?std::vector<uint8_t>(b.begin()+o,b.begin()+o+n):std::vector<uint8_t>{};
}
void W(std::ostream& f,uint32_t v){f.write(reinterpret_cast<const char*>(&v),4);}
void Lote(std::ostream& f,const std::vector<eventos::Registro>& r) {
  W(f,uint32_t(r.size()));for(const auto& e:r){W(f,std::bit_cast<uint32_t>(e.cuadro));W(f,e.tipo);W(f,e.valor);}
  float anterior=-1;const auto l=eventos::Evaluar(r,anterior,100000,100000);
  for(size_t t=0;t<10;++t) {W(f,l.usados[t]);for(size_t i=0;i<l.usados[t];++i)W(f,l.colas[t][i].valor);}
}
#include IW_AP_F_PREVIAS
int main(int argc,char** argv) {
  try {
    Exigir(argc==3,"uso: ap_F <IW USA> <tmp>");const std::filesystem::path game=argv[1],out=argv[2];
    Exigir(PruebasPrevias(),"regresion eventos existente");std::cout<<std::setprecision(10);
    std::vector<eventos::Registro> mezcla;
    for(unsigned i=0;i<9;++i)for(unsigned t:{8u,4u,7u,1u,9u,6u,0u,5u,3u,2u})
      mezcla.push_back({2,t,t==8?1:t==6?(i?5u:7u):t==9?i:t*100+i});
    {std::ofstream f(out/"sintetico.bin",std::ios::binary);Lote(f,mezcla);}
    float old=-1;const auto lote=eventos::Evaluar(mezcla,old,2,4);EstadoExterno ex;ex.Actualizar(lote);
    eventos::Despachar(lote,[](const auto& e){std::cout<<"ORDEN "<<e.tipo<<' '<<e.valor<<'\n';});
    Exigir(lote.usados[6]==7 && lote.colas[6][0].valor==7,"cola6 debe conservar primera y capacidad7");
    Exigir(ex.cuenta5==7 && ex.valor244==.8f,"5/9 encolado");
    int n=0;auto leer5=[&](uint32_t v){++n;std::cout<<"LECTOR5 "<<v<<'\n';};
    ex.Leer5(leer5);ex.Leer5(leer5);Exigir(n==14,"lector5 no debe vaciar su cola");
    const auto vacio=eventos::Evaluar(mezcla,old,2,4);ex.Actualizar(vacio);Exigir(!ex.cuenta5 && ex.valor244==.8f,"9 persiste,5 se renueva");
    ex.Reiniciar();Exigir(ex.VelocidadCampo(20,1,1)==8,"9 cero usa0.4");
    {Estado0 s;Exigir(Consumir0(-1,0x29,s)==Consumo::Pendiente && s.cuenta29==0,"escena desconocida no es silencio demostrado");}
    for(int32_t v:{0,5,20,-1,-100,2147483647,int32_t(-2147483647-1)}) {
      eventos::Lote l;l.usados[9]=1;l.colas[9][0]={0,9,uint32_t(v)};ex.Actualizar(l);
      std::cout<<"VELOCIDAD "<<v<<' '<<std::bit_cast<uint32_t>(ex.valor244)<<' '<<std::bit_cast<uint32_t>(ex.VelocidadCampo(20,.5f,1.25f))<<'\n';
    }
    for(int escena=0;escena<9;++escena)for(int v:{0x29,0x2b,0x3c,0x3d,0x50,0x51,0x47}) {
      Estado0 s;s.cuenta29=2;s.cuenta2b=3;s.cuadro_objetivo18e4=7.5;
      const auto r=Consumir0(escena,uint32_t(v),s);
      std::cout<<"AP0 "<<escena<<' '<<v<<' '<<int(r)<<' '<<s.cuenta29<<' '<<s.cuenta2b<<' '<<s.flag206e<<' '<<s.oculto_ccd<<' '
               <<std::bit_cast<uint32_t>(s.cuadro_c68)<<' '<<std::bit_cast<uint32_t>(s.velocidad_cb4)<<'\n';
    }
    for(int id:{-1,0,1,3,4,5,6,7,199,200,299,300,303,304,349,350,399,400,499,500}) {
      RegistroEfectos4 a{111,-1,-1},b{222,-1,-1};const auto p=Preparar4(uint32_t(id),a,b);
      std::cout<<"AP4 "<<id<<' '<<p.contrario<<' '<<int(p.ruta)<<' '<<a.primero24<<' '<<a.comando48<<' '<<b.primero24<<' '<<b.comando48<<' '<<p.paquete_actor<<'\n';
    }
    std::array<V3,100>a{},b{};for(size_t i=0;i<a.size();++i){a[i]={float(i),float(i*2),float(i*3)};b[i]={float(i+20),float(i*4+10),float(i*5+30)};}
    for(int32_t v:{0,1,19,20,503,1010203,2010203,999999,-1,123456}) {
      Impacto5 r;bool ok=Preparar5(uint32_t(v),a,b,1,r);
      std::cout<<"AP5 "<<v<<' '<<ok<<' '<<r.tipo<<' '<<r.ancla_propia<<' '<<r.ancla_rival<<' '<<r.invertir<<' '
               <<r.posicion.x<<' '<<r.posicion.y<<' '<<r.posicion.z<<' '<<r.direccion.x<<' '<<r.direccion.y<<' '<<r.direccion.z<<'\n';
    }
    const auto elf=File(game/"SLUS_218.42");Exigir(BancoBoca(elf,922)==1471 && BancoBoca(elf,924)==1475,"banco boca desde ELF");
    for(int tipo=0;tipo<20;++tipo)std::cout<<"FUNC5 "<<tipo<<' '<<FuncionImpacto5(elf,tipo)<<'\n';
    size_t amk_casos=0;
    for(const char* afs:{"ADX_USA.AFS","ADX_JPN.AFS"})for(unsigned bank:{1471u,1475u}) {
      const auto raw=Entrada(game/"USR"/afs,bank);boca::BancoAMK amk;Exigir(amk.Cargar(raw),"AMK retail");
      for(uint32_t id=0;id<amk.Cantidad();++id)if(const auto c=amk.Obtener(id)) {
        boca::Estado s;Exigir(s.IniciarAP8(amk,id),"inicio AMK valido");
        std::vector<float> tiempos={0,float(c->duracion-1)};
        for(const auto& k:c->claves){tiempos.push_back(float(k.cuadro)-1);tiempos.push_back(float(k.cuadro)-.5f);tiempos.push_back(float(k.cuadro));}
        for(float t:tiempos) {
          s.cuadro=t;boca::Seleccion sel;Exigir(s.Seleccionar(amk,12,sel),"seleccionar AMK");
          std::cout<<"AMK "<<afs<<' '<<bank<<' '<<id<<' '<<t<<' '<<int(sel.visema)<<' '<<sel.variante<<' '<<sel.pose<<'\n';++amk_casos;
        }
      }
      boca::Estado s;Exigir(s.IniciarAP8(amk,1),"clip1 retail");s.cuadro=3;
      Exigir(!s.IniciarAP8(amk,2,false,true) && s.cuadro==3,"AP8 bloqueado debe conservar estado");
      s.Avanzar(amk,1,true);Exigir(s.cuadro==3,"pausa boca");s.ignorar_pausa=true;s.Avanzar(amk,1,true);
      Exigir(s.cuadro==4,"override de pausa boca");s.Avanzar(amk,100000,false);Exigir(s.id==0xffffffffu && s.cuadro==0,"fin boca");
      Exigir(!s.IniciarAP8(amk,uint32_t(-1)),"idAP8 firmado");
      const auto n=amk.Cantidad();Exigir(!amk.Cargar({1,2}) && amk.Cantidad()==n,"AMK transaccional");
      if(std::string(afs)=="ADX_USA.AFS" && bank==1471) {
        for(int caso=0;caso<8;++caso) {
          boca::Estado reloj;reloj.IniciarAP8(amk,1);
          const float frame=caso==4?float(amk.Obtener(1)->duracion-2):caso==5?float(amk.Obtener(1)->duracion-1):0.0f;
          const float paso=caso==4?2.0f:caso==5?0.0f:caso==6?1.25f:1.0f;
          const bool pausa=caso==1 || caso==2 || caso==7,forzar=caso==2 || caso==7,congelado=caso==3 || caso==7;
          reloj.cuadro=frame;reloj.ignorar_pausa=forzar;reloj.Avanzar(amk,paso,pausa,congelado);
          std::cout<<"RELOJ8 "<<frame<<' '<<paso<<' '<<pausa<<' '<<forzar<<' '<<congelado<<' '<<reloj.id<<' '
                   <<std::bit_cast<uint32_t>(reloj.cuadro)<<' '<<reloj.ignorar_pausa<<'\n';
        }
      }
    }
    // Nueve matrices completas, indices distintos/negativos; comparar salida con25FA90.
    for(unsigned modo=0;modo<3;++modo) {
      std::array<boca::MatrizHueso,20> src{},dst{};
      for(size_t i=0;i<src.size();++i)for(size_t k=0;k<32;++k){src[i][k]=uint32_t(0x12300000+i*256+k);dst[i][k]=uint32_t(0x45600000+i*256+k);}
      boca::Indices ia={0,2,4,-1,8,10,12,14,16},ib={17,15,13,11,9,-1,5,3,1};
      if(modo==2){ia={0,1,2,3,4,5,6,7,8};ib={9,10,11,12,13,14,15,16,17};}
      std::ofstream f(out/("matrices_"+std::to_string(modo)+".bin"),std::ios::binary);
      if(modo==1){Exigir(boca::Copiar9(dst,ib,src,ia)==7,"copia inversa");f.write(reinterpret_cast<const char*>(src.data()),sizeof(src));}
      else{Exigir(boca::Copiar9(src,ia,dst,ib)==(modo==2?9:7),"copiar9 cuenta");f.write(reinterpret_cast<const char*>(dst.data()),sizeof(dst));}
    }
    boca::MatrizHueso q{},target{};
    for(auto* p:{&q,&target}){(*p)[19]=std::bit_cast<uint32_t>(1.0f);(*p)[23]=std::bit_cast<uint32_t>(1.0f);for(size_t k=24;k<28;++k)(*p)[k]=std::bit_cast<uint32_t>(1.0f);}
    target[17]=std::bit_cast<uint32_t>(.6f);target[19]=std::bit_cast<uint32_t>(.8f);target[20]=std::bit_cast<uint32_t>(10.0f);q[28]=0xaabbccdd;
    boca::MezclarMatriz(q,target,.75f);Exigir(q[28]==0xaabbccdd,"mezcla boca altera flags");
    {std::ofstream f(out/"mezcla_matriz.bin",std::ios::binary);f.write(reinterpret_cast<const char*>(q.data()),sizeof(q));}
    {
      boca::BancoAMK banco;Exigir(banco.Cargar(Entrada(game/"USR/ADX_USA.AFS",1471)),"banco motor");
      std::array<boca::MatrizHueso,20> cuerpo{};std::array<boca::NueveMatrices,12> poses{};
      auto identidad=[](auto& m){m[19]=std::bit_cast<uint32_t>(1.0f);m[23]=std::bit_cast<uint32_t>(1.0f);for(size_t k=24;k<28;++k)m[k]=std::bit_cast<uint32_t>(1.0f);};
      for(size_t i=0;i<cuerpo.size();++i){identidad(cuerpo[i]);cuerpo[i][20]=std::bit_cast<uint32_t>(float(i));cuerpo[i][28]=uint32_t(0x12340000+i);}
      for(size_t p=0;p<poses.size();++p)for(size_t i=0;i<9;++i){identidad(poses[p][i]);poses[p][i][20]=std::bit_cast<uint32_t>(float(p*10+i));}
      const boca::Indices indices={17,15,13,11,9,7,5,3,1};boca::Motor motor;
      Exigir(motor.Iniciar(banco,1,cuerpo,indices),"motor inicio/snapshot");
      Exigir(motor.respaldo[0]==cuerpo[17],"snapshot9 debe venir del cuerpo");
      const auto original=cuerpo;Exigir(motor.Aplicar(banco,poses,cuerpo,indices,true)==0 && cuerpo==original,"boca congelada escribe matrices");
      for(int paso=0;paso<3;++paso) {
        if(paso==2){const auto c=banco.Obtener(1);motor.estado.cuadro=float(c->claves.size()>1?c->claves[1].cuadro:0);}
        Exigir(motor.Aplicar(banco,poses,cuerpo,indices)==9,"motor no aplica matrices");
        std::ofstream f(out/("motor_"+std::to_string(paso)+".bin"),std::ios::binary);f.write(reinterpret_cast<const char*>(cuerpo.data()),sizeof(cuerpo));
      }
    }
    std::ofstream corpus(out/"corpus.bin",std::ios::binary);size_t clips=0,eventos_n=0;
    for(unsigned cmn:{204u,912u,913u,914u,915u,916u,917u,918u,919u,920u,921u,922u,923u,924u,925u,926u,927u,928u,929u,930u,931u,932u}) {
      const auto amb=Entrada(game/"USR/DATA_CMN.AFS",cmn);
      for(unsigned h=0;h<U32(amb,16);++h) {
        const auto bsk=Hijo(amb,h);if(U32(bsk,0)!=0x4b534223)continue;
        for(unsigned cod=0;cod<U32(bsk,16);++cod) {
          const size_t blk=U32(bsk,U32(bsk,20)+4*cod);if(!blk)continue;std::vector<eventos::Registro> records;
          for(unsigned j=0;j<U32(bsk,blk+40);++j) {
            const size_t l=U32(bsk,blk+44)+8*j;if((U32(bsk,l)&65535)!=7)continue;
            for(unsigned k=0;k<U32(bsk,l)>>16;++k){const size_t p=U32(bsk,l+4)+16*k;records.push_back({float(uint16_t(U32(bsk,p))),U32(bsk,p+4),U32(bsk,p+8)});}
          }
          if(records.empty())continue;W(corpus,cmn);W(corpus,h);W(corpus,cod);Lote(corpus,records);++clips;eventos_n+=records.size();
        }
      }
    }
    std::cout<<"PASS AP: regresion previa, orden/colas, lectores5/9, contexto0/4; AMK "<<amk_casos<<" muestras; corpus "<<clips<<' '<<eventos_n<<'\n';
    return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

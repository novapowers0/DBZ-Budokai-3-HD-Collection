#include "../src/iw_capsulas_render.h"
#include <fstream>
#include <iostream>
#include <iterator>
namespace R=dbz3::iw::capsrender;
namespace C=dbz3::iw::spx::capsulas;
size_t checks=0;
void Ver(bool b,const std::string& s){++checks;if(!b)throw std::runtime_error(s);}
struct Datos {
  R::Bytes b;size_t p=0;
  uint32_t U(){auto v=C::U(b,p);p+=4;return v;}
  R::Bytes Blob(){const auto n=U();R::Bytes o(b.begin()+p,b.begin()+p+n);p+=n;return o;}
  void F(float v,float eps,const std::string& why){const float e=R::F(U());Ver(std::abs(v-e)<=eps,why+" host="+std::to_string(v)+" original="+std::to_string(e));}
};
R::Qword Floats(float a,float b,float c,float d){return {R::B(a),R::B(b),R::B(c),R::B(d)};}
int main(int argc,char** argv) {
  try {
    Ver(argc>=2&&argc<=4,"fixture");std::ifstream f(argv[1],std::ios::binary);Datos d{R::Bytes(std::istreambuf_iterator<char>(f),{})};Ver(d.U()==0x46345249,"firmaF4");
    auto elf=d.Blob();R::Ame ame(d.Blob());const float paso=R::F(d.U()),dt=R::F(d.U());uint32_t seed=0x87654321;uint64_t frame=0;
    if(argc<3||std::string(argv[2])!="--solo-vu") {
    const auto ns=d.U();
    for(size_t s=0;s<ns;++s) {
      const auto target=d.U();while(frame<target){ame.Cuadro(seed,paso,dt);++frame;}
      Ver(uint32_t(ame.Nodos())==d.U(),"AME conteo nodos frame="+std::to_string(frame));Ver(seed==d.U(),"AME RNG exacto frame="+std::to_string(frame));
      const auto n=d.U();std::vector<size_t> activos;for(size_t i=0;i<ame.nodos.size();++i)if(!ame.nodos[i].retirado)activos.push_back(i);
      Ver(activos.size()==n,"AME lista activa");
      for(size_t j=0;j<n;++j) {
        const auto& node=ame.nodos[activos[j]];const auto& def=ame.defs[node.def];
        Ver(def.offset==d.U()&&def.tipo==d.U(),"AME orden original277150/26BB0");const auto count=d.U();
        Ver(node.particulas.size()==count,"AME particulas frame="+std::to_string(frame)+" nodo="+std::to_string(def.offset));
        for(const auto& p:node.particulas) {
          d.F(p.edad,0,"AME edad");Ver(p.flags==d.U(),"AME flags");
          for(float v:{p.p.x,p.p.y,p.p.z})d.F(v,3.e-6f,"AME posicion");
          for(float v:{p.v.x,p.v.y,p.v.z})d.F(v,3.e-6f,"AME velocidad");
          for(float v:p.color)d.F(v,3.e-6f,"AME RGBA");for(float v:p.uv)d.F(v,3.e-6f,"AME UV");
          d.F(p.tamano,3.e-6f,"AME tamano");d.F(p.escala,3.e-6f,"AME escala");d.F(p.angulo,3.e-6f,"AME angulo");for(float v:p.quat)d.F(v,3.e-6f,"AME quaternion");
        }
      }
      std::cout<<"AME original frame="<<frame<<" nodos="<<ame.Nodos()<<" nacidas="<<ame.nacidas<<" RNG="<<std::hex<<seed<<std::dec<<" PASS\n";
    }
    }
    if(argc==3&&std::string(argv[2])=="--solo-ame"){std::cout<<"PASS AME F4 "<<checks<<'\n';return 0;}
    const unsigned indice=argc==4?unsigned(std::stoul(argv[3])):5;
    R::Programas prog(std::move(elf));R::VU vu;vu.code=prog.Cargar(7,indice);
    for(size_t i=0;i<28;++i)for(size_t j=0;j<4;++j)vu.mem[i][j]=prog.Leer(uint32_t(0x485f10+16*i+4*j));
    for(size_t i=0;i<4;++i) {
      vu.mem[i]={0,0,0,0};vu.mem[i][i]=R::B(1);
      vu.mem[12+i]=vu.mem[i];
    }
    vu.mem[8]=Floats(80,0,0,0);vu.mem[9]=Floats(0,80,0,0);vu.mem[10]=Floats(0,0,10,0);vu.mem[11]=Floats(2048,2048,1000,1);
    // Bootstrap original: q4..7=normal->luces; q16..19=colores/ambiente.
    vu.mem[4]=Floats(0,0,0,0);vu.mem[5]=Floats(0,0,0,0);vu.mem[6]=Floats(1,0,0,0);vu.mem[7]=Floats(0,0,0,0);
    vu.mem[16]=Floats(1,0,0,0);vu.mem[17]=Floats(0,0,0,0);vu.mem[18]=Floats(0,0,0,0);vu.mem[19]=Floats(0,0,0,1);
    const uint32_t prim=indice>=0x30?0x38:0x18,base=0x30004000|prim|(prim<<15);
    vu.mem[48]={0x8000,base|0x18000,0x412,R::B(1)};vu.mem[49]={0x8000,base|0x20000,0x412,R::B(1.f/3)};vu.mem[50]={0x8000,base|0x28000,0x412,R::B(.5f)};
    vu.mem[51]=Floats(128,128,128,128);vu.mem[52]=Floats(255,0,1,128.5f);vu.mem[54]=Floats(128,128,128,128);vu.mem[66]=Floats(128,128,128,128);
    vu.Ejecutar(0);Ver(vu.pc==8,"VU bootstrap termina en8");vu.top=0x100;vu.mem[0x100]={2,3,0,0};
    for(size_t i=0;i<3;++i){vu.mem[0x101+3*i]=Floats(i==0?-.5f:i==1?.5f:0,i==2?.5f:-.5f,0,1);vu.mem[0x102+3*i]=Floats(0,0,1,1);vu.mem[0x103+3*i]=Floats(i==0?0:i==1?1:.5f,i==2?1:0,1,0);}
    vu.Ejecutar(8);Ver(!vu.gif.empty(),"VU original emite XGKICK real");
    R::Lienzo img;R::Imagen white{1,1,{255,255,255,255},0};for(const auto& g:vu.gif)img.Gif(g,&white,{});
    Ver(img.pixeles>0&&img.triangulos>0,"GS rasteriza salida original VU");
    const auto centro=(224*640+320)*4;const auto esperado=indice>=0x30?127:255;
    Ver(std::abs(int(img.rgba[centro])-esperado)<=1,"rojo proyectado+niebla esperado actual="+std::to_string(img.rgba[centro]));
    Ver(std::abs(int(img.rgba[centro+1])-(indice==61?0:esperado))<=1,"verde de luz direccional esperado");
    Ver(std::abs(int(img.rgba[centro+2])-(indice==61?0:esperado))<=1,"azul de luz direccional esperado");
    const auto& gif=vu.gif[0];Ver(C::U(gif,0)%32768==3,"GIF original tres vertices");
    for(size_t i=0;i<3;++i) {
      const auto p=16+48*i;const unsigned x=i==0?32128:i==1?33408:32768,y=i==2?33408:32128;
      Ver(C::U(gif,p+32)==x&&C::U(gif,p+36)==y,"XYZF fijo4 coincide con proyeccion exacta");
      Ver(C::U(gif,p+40)==16000,"Z entero original");
      Ver((C::U(gif,p+44)>>4&255)==(indice>=0x30?127:0),"FOG original antes del rasterizado");
      Ver(C::U(gif,p+16)==128&&C::U(gif,p+20)==(indice==61?0:128)&&C::U(gif,p+24)==(indice==61?0:128),"RGBA antes del rasterizado");
    }
    std::cout<<"VU original indice="<<indice<<" code="<<vu.code.size()<<" pares ejecutados="<<vu.ejecutadas<<" GIF="<<vu.gif.size()<<" GS triangulos="<<img.triangulos<<" pixeles="<<img.pixeles<<" PASS\n";
    std::cout<<"PASS F4 "<<checks<<"\n";return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL F4 "<<checks<<" "<<e.what()<<'\n';return 1;}
}

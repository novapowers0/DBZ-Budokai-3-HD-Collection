#include "../src/iw_capsulas_render.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
namespace R=dbz3::iw::capsrender;
int main(int argc,char** argv) {
  size_t checks=0;
  try {
    if(argc!=2)throw std::runtime_error("fixture");std::ifstream f(argv[1],std::ios::binary);R::Bytes b(std::istreambuf_iterator<char>(f),{});size_t p=0;
    auto u=[&](){const auto x=dbz3::iw::spx::capsulas::U(b,p);p+=4;return x;};
    auto ver=[&](bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);};std::map<uint32_t,bool> cargados;
    ver(u()==0x46355348,"firma");const auto n=u();R::Programas prog(R::Bytes(b.begin()+p,b.begin()+p+n));p+=n;const auto count=u();size_t omitidos=0,con_callback=0,nop_mpg=0;
    for(unsigned i=0;i<count;++i) {
      const auto flags=u(),modo=u(),dibujar=u(),clave=u(),callback=u(),unico=u();std::array<uint32_t,4> record;for(auto& w:record)w=u();
      const auto e=prog.Elegir(flags,modo);const auto id=" caso="+std::to_string(i)+" flags="+std::to_string(flags);
      ver(e.dibujar==bool(dibujar),"seleccion de omision original"+id);ver(uint32_t(e.clave)==clave,"clave original"+id);ver(e.callback==callback,"callback original"+id);
      ver(e.contexto_unico==bool(unico),"omision original de segundo contexto"+id);
      if(e.dibujar){for(unsigned k=0;k<4;++k)ver(prog.Leer(e.registro+4*k)==record[k],"registro DMA original"+id);}else ++omitidos;
      con_callback+=e.callback!=0;
      if(e.dibujar && (record[1]&0x0fffffff)>=0x100000 && !cargados.contains(e.registro)) {
        const auto tag=prog.Leer(record[1]&0x0fffffff);
        if(tag==0x60000000) {
          bool rechazado=false;try{(void)prog.CargarRegistro(e.registro);}catch(const std::runtime_error& error){rechazado=std::string(error.what()).find("microprograma vacio")!=std::string::npos;}
          ver(rechazado,"RET sin MPG no se proclama un shader nuevo"+id);++nop_mpg;cargados[e.registro]=false;continue;
        }
        const auto& micro=prog.CargarRegistro(e.registro);ver(!micro.empty(),"microprograma del registro seleccionado"+id);cargados[e.registro]=true;
        const auto path=std::filesystem::path(argv[1]).parent_path()/("micro_"+std::to_string(e.registro)+".bin");std::ofstream file(path,std::ios::binary);
        for(const auto& par:micro)file.write(reinterpret_cast<const char*>(par.data()),8);
      }
    }
    const auto ncb=p<b.size()?u():0;
    for(unsigned i=0;i<ncb;++i) {
      const auto cb=u(),ng=u();std::map<uint32_t,uint32_t> globals;
      for(unsigned k=0;k<ng;++k){const auto addr=u(),value=u();globals[addr]=value;}
      const auto nbytes=u();R::Bytes bloque(b.begin()+p,b.begin()+p+nbytes);p+=nbytes;
      R::Bytes esperado(b.begin()+p,b.begin()+p+nbytes);p+=nbytes;
      prog.AplicarCallback(cb,bloque,[&](uint32_t addr){return globals.at(addr);});
      for(size_t k=0;k<bloque.size();++k)ver(bloque[k]==esperado[k],"callback original="+std::to_string(cb)+" byte="+std::to_string(k));
    }
    if(ncb)std::cout<<"Callbacks originales casos="<<ncb<<" bloques320B exactos PASS\n";
    const auto validos=std::count_if(cargados.begin(),cargados.end(),[](const auto& x){return x.second;});
    std::cout<<"PASS "<<checks<<" selector1B1130/133770 original; casos="<<count<<" omite="<<omitidos<<" callbacks="<<con_callback<<" microprogramas="<<validos<<" RET_sin_MPG="<<nop_mpg<<'\n';return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL selector "<<checks<<" "<<e.what()<<'\n';return 1;}
}

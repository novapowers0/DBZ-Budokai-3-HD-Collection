// Salida real del mezclador CPU, sin dispositivo/ventanas. No es una escucha
// certificada: guarda el PCM que recibiria la salida48k del host.
#include "../src/iw_midi.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
namespace M=dbz3::iwmidi;
namespace A=dbz3::iwsonido;
size_t comprobaciones=0;
void Ver(bool b,const char* s){++comprobaciones;if(!b)throw std::runtime_error(s);}
struct Datos {
  M::Bytes b;size_t p=0;
  uint32_t U(){const auto v=M::U(b,p);p+=4;return v;}
  uint64_t Q(){const auto l=U(),h=U();return uint64_t(l)|uint64_t(h)<<32;}
  M::Bytes Blob(){const auto n=U();M::Rango(b,p,n);M::Bytes o(b.begin()+p,b.begin()+p+n);p+=n;return o;}
};
void Archivo(const std::string& path,const std::vector<int16_t>& pcm){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(pcm.data()),std::streamsize(pcm.size()*2));if(!f)throw std::runtime_error("no se escribe salida");}
bool NoCero(const std::vector<int16_t>& x){for(auto s:x)if(s)return true;return false;}
int main(int argc,char** argv) {
  try {
    Ver(argc==3,"fixture y salida requeridos");std::ifstream f(argv[1],std::ios::binary);
    Datos d{M::Bytes(std::istreambuf_iterator<char>(f),{})};Ver(d.U()==0x4d494433,"firma MIDI F3");
    const auto cases=d.U();
    for(uint32_t c=0;c<cases;++c) {
      const auto id=d.U();const auto body=d.Blob(),index=d.Blob(),seq=d.Blob();
      M::Banco banco;banco.Cargar(body,index);const auto plan=M::Leer(seq);
      std::map<uint64_t,M::Bytes> grupos;
      for(const auto& e:plan.eventos)grupos[e.muestra].insert(grupos[e.muestra].end(),e.bytes.begin(),e.bytes.end());
      const auto ne=d.U();Ver(grupos.size()==ne,"numero de rafagas MODMIDI retail");
      for(uint32_t i=0;i<ne;++i){const auto frame=d.Q();const auto expected=d.Blob();Ver(grupos.at(frame)==expected,"bytes y tiempo de todos los eventos retail");}
      const auto nn=d.U();
      for(uint32_t i=0;i<nn;++i) {
        const auto programa=d.U(),vel=d.U();const auto n=banco.Resolver(programa,60,vel);
        Ver(n.onda==d.U(),"onda y direccion de inicio SPU HSYN retail");
        Ver(n.pitch==d.U(),"pitch HSYN+LIBSD retail");Ver(n.adsr1==d.U()&&n.adsr2==d.U(),"ADSR HSYN retail");
        Ver(n.volumen==d.U(),"volumen HSYN41CC retail");Ver(n.rutas==d.U()&&n.core==d.U(),"core/rutas HSYN retail");
      }
      A::Reproductor salida;Ver(salida.DispositivoAutomatico(false),"salida host sin altavoces");
      M::Sesion sesion(salida,0x4d49444900000000ull|id);
      const bool preparada=sesion.Preparar(body,index,seq);Ver(preparada,sesion.Error().c_str());
      Ver(sesion.Rpc13(0)&&sesion.Handle()!=0,"RPC13 aceptado por Enviar, con handle REAL");
      std::vector<int16_t> pcm(48000*2);salida.Procesar(pcm.data(),48000);
      Ver(NoCero(pcm),"RPC13 produce salida del mezclador, no solo PCM decodificado");
      const auto esperado=d.Blob();Ver(esperado.size()==pcm.size()*2,"tamano oraculo PCM");
      Ver(!std::memcmp(esperado.data(),pcm.data(),esperado.size()),"PCM48k completo1s vs banco/registros/ROM independientes");
      Archivo(std::string(argv[2])+"/midi_"+std::to_string(id)+"_inicio.s16",pcm);
      // API sin cursor: NO decir que RPC14 conserva la ADSR usando un reloj UI.
      sesion.posicion_audio=[](A::Handle,uint64_t&){return false;};
      Ver(!sesion.Rpc14(0)&&sesion.Error().find("Posicion")!=std::string::npos,"sin cursor bloquea explicitamente RPC14");
      uint64_t consumidas=48000;
      sesion.posicion_audio=[&](A::Handle h,uint64_t& cursor){if(h!=sesion.Handle())return false;cursor=consumidas;return true;};
      // El cursor procede de las muestras que acabamos de consumir con la API
      // real, no de tiempo supuesto. Un segundo actor ajeno debe continuar.
      std::vector<int16_t> ajeno(24000*2,1);A::Opciones o;o.canal=salida.Mezcla().Slot(123);o.rutas=3;
      const auto h_ajeno=salida.Enviar(ajeno,48000,o);Ver(h_ajeno!=0,"segundo actor real");
      Ver(sesion.Rpc14(0),sesion.Error().c_str());std::vector<int16_t> fin(24000*2);salida.Procesar(fin.data(),24000);
      const auto expected_fin=d.Blob();Ver(expected_fin.size()==fin.size()*2&&!std::memcmp(expected_fin.data(),fin.data(),expected_fin.size()),"RPC14 cola KeyOff original y actor ajeno conservado");
      Archivo(std::string(argv[2])+"/midi_"+std::to_string(id)+"_corte.s16",fin);
      Ver(fin.back()==1,"RPC14 ya no produce nota; otro actor sigue audible");
      Ver(sesion.Rpc14(0),"stop repetido de MIDI parado es no-op original");
      size_t saturadas=0;int pico=0;for(int x:sesion.Datos().pcm){pico=std::max(pico,std::abs(x));saturadas+=x==32767||x==-32768;}
      std::cout<<"PASS MIDI"<<id<<" eventos="<<plan.eventos.size()<<" rafagas="<<grupos.size()<<" PCM_total="<<sesion.Datos().pcm.size()/2<<" pico="<<pico<<" saturadas="<<saturadas<<" RPC13 salida no nula, RPC14 KeyOff/cola/corte\n";
      auto mala=seq;mala.resize(60);Ver(!sesion.Preparar(body,index,mala),"MIDI truncado no se acepta");
    }
    Ver(d.p==d.b.size(),"fixture consumido entero");std::cout<<"PASS "<<comprobaciones<<" comprobaciones MIDI/salida real\n";return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL MIDI "<<comprobaciones<<" "<<e.what()<<'\n';return 1;}
}

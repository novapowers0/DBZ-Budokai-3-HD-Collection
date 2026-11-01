// PCM de los AP7 reales contra FFmpeg; ningun dispositivo de audio.
#define main PruebaAnteriorV
#include "iw_ap_audio_prueba.cpp"
#undef main
#include "../src/iw_adx.h"

int main(int argc,char** argv) {
  try {
    Comprobar(argc==2,"uso: prueba <IW USA>");
    const std::filesystem::path game=argv[1];const auto elf=Fichero(game/"SLUS_218.42");
    for(uint32_t cmn:{922u,921u,920u,919u,923u,931u,928u,927u,924u,925u,929u,932u,926u,930u,914u})
      std::cout<<"PERFIL "<<cmn<<' '<<PerfilAdx(cmn)<<'\n';
    Comprobar(ResolverAdx(elf,924,10,1).ruta==Ruta::Pendiente,"transicion de forma no inventada");
    Comprobar(ResolverAdx(elf,924,1000,0).ruta==Ruta::PausaAdx,"control pausa");
    Comprobar(ResolverAdx(elf,924,2000,0).ruta==Ruta::Adx && ResolverAdx(elf,924,2000,0).solapar &&
              ResolverAdx(elf,924,2000,0).banco==1134,"2000: nuevo ADX sin cortar el anterior");
    Comprobar(ResolverAdx(elf,924,2100,0).ruta==Ruta::SilencioOriginal,"2100: limites originales del perfil");
    Comprobar(ResolverAdx({},924,10,0).ruta==Ruta::Pendiente,"ELF ausente");
    int total=0,pcm=0,silencio=0;
    for(int cmn=912;cmn<=932;++cmn) {
      const auto amb=Entrada(game/"USR/DATA_CMN.AFS",cmn);
      for(uint32_t hi=0;hi<U32(amb,16);++hi) {
        const auto b=dbz3::pasarela::HijoAmb(amb,hi);if(U32(b,0)!=0x4b534223)continue;
        for(uint32_t cod=0;cod<U32(b,16);++cod) {
          const size_t blk=U32(b,U32(b,20)+4*cod);if(!blk)continue;
          for(uint32_t ln=0;ln<U32(b,blk+40);++ln) {
            const size_t l=U32(b,blk+44)+8*ln;if((U32(b,l)&65535)!=7)continue;
            for(uint32_t i=0;i<U32(b,l)>>16;++i) {
              const size_t e=U32(b,l+4)+16*i;if(U32(b,e+4)!=7)continue;
              const auto v=U32(b,e+8);const auto r=ResolverAdx(elf,uint32_t(cmn),v,0);++total;
              if(r.ruta==Ruta::SilencioOriginal){++silencio;continue;}
              Comprobar(r.ruta==Ruta::Adx,"AP7 sin resolver");
              for(const char* afs:{"ADX_USA.AFS","ADX_JPN.AFS"}) {
                const auto datos=Entrada(game/"USR"/afs,r.banco);
                const auto p=dbz3::iwadx::Leer(datos);
                Comprobar(!p.muestras.empty(),"ADX real no decodificado");++pcm;
                std::cout<<"ADX "<<cmn<<' '<<v<<' '<<afs<<' '<<r.banco<<' '<<p.hz<<' '<<p.muestras.size()<<' '<<HashPcm(p.muestras)<<'\n';
                for(size_t n:{size_t(0),size_t(31),size_t(33)}) {
                  if(n<datos.size())Comprobar(dbz3::iwadx::Leer({datos.begin(),datos.begin()+n}).muestras.empty(),"ADX truncado aceptado");
                }
              }
            }
          }
        }
      }
    }
    std::cout<<"CORPUS_ADX "<<total<<' '<<pcm<<' '<<silencio<<'\n';return 0;
  } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

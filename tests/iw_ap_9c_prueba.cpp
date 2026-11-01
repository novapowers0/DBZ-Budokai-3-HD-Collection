// Extiende el corpus retail del oraculo V: sin ventana ni salida sonora.
#define main PruebaAnteriorV
#include "iw_ap_audio_prueba.cpp"
#undef main

int main(int argc,char** argv) {
  try {
    Comprobar(argc==2,"uso: prueba <IW USA>");
    const std::filesystem::path game=argv[1];
    const auto elf=Fichero(game/"SLUS_218.42");
    const auto azar=LeerAzarVoz(elf);Comprobar(azar.valida,"tabla azar retail");
    for(uint32_t seed:{1u,0x12345678u,0x80000000u,0xffffffffu})
      for(bool jp:{false,true})for(int v=0;v<2700;++v) {
        // Valores fuera del dominio de la tabla original no se ejecutan en el
        // oraculo EE: el retail leeria fuera de sus14 filas.
        int n=v>=2000?v-2000:v>=1000?v-1000:v;
        if(n>=114 && n<300)continue;
        if(n>=700)continue;
        uint32_t s=seed;
        auto mem=[&](){for(int k=0;k<32;++k)s=(s<<1)|(((s>>31)^(s>>4))&1);return s-1;};
        const auto r=ResolverVoz(uint32_t(v),1472,jp,azar,mem);
        std::cout<<"RNG "<<seed<<' '<<jp<<' '<<v<<' '<<int(r.ruta)<<' '<<r.sonido<<' '<<s<<'\n';
      }
    std::map<std::array<int,3>,dbz3::iwiop::Voz> cache;
    int total=0,pcm=0,silencio=0,pendiente=0;
    uint32_t semilla=0x12345678;
    auto mem=[&](){for(int k=0;k<32;++k)semilla=(semilla<<1)|(((semilla>>31)^(semilla>>4))&1);return semilla-1;};
    for(int cmn:{204,912,913,914,915,916,917,918,919,920,921,922,923,924,925,926,927,928,929,930,931,932}) {
      const auto amb=Entrada(game/"USR/DATA_CMN.AFS",cmn);
      const int banco=BancoActor(elf,cmn==204 || cmn==915?CmnActor(0):uint32_t(cmn));
      for(uint32_t hi=0;hi<U32(amb,16);++hi) {
        const auto b=dbz3::pasarela::HijoAmb(amb,hi);if(U32(b,0)!=0x4b534223)continue;
        for(uint32_t cod=0;cod<U32(b,16);++cod) {
          const size_t blk=U32(b,U32(b,20)+4*cod);if(!blk)continue;
          for(uint32_t ln=0;ln<U32(b,blk+40);++ln) {
            const size_t l=U32(b,blk+44)+8*ln;if((U32(b,l)&65535)!=7)continue;
            for(uint32_t i=0;i<U32(b,l)>>16;++i) {
              const size_t e=U32(b,l+4)+16*i;const auto t=U32(b,e+4),v=U32(b,e+8);
              if(t!=1 && t!=2 && t!=3)continue;++total;
              const auto a=t==3?ResolverVoz(v,banco,false,azar,mem):Resolver(t,v,banco,false);
              if(a.ruta==Ruta::SilencioOriginal){++silencio;continue;}
              if(a.ruta!=Ruta::Cmn && a.ruta!=Ruta::Voz){++pendiente;continue;}
              const std::array<int,3> key={int(a.ruta),a.banco,a.sonido};
              auto [it,nueva]=cache.try_emplace(key);
              if(nueva) {
                const char* afs=a.ruta==Ruta::Cmn?"DATA_CMN.AFS":"ADX_USA.AFS";
                it->second=dbz3::iwiop::Leer(Entrada(game/"USR"/afs,a.banco),a.sonido);
                const auto& voz=it->second;
                std::cout<<"PCM9C "<<afs<<' '<<a.banco<<' '<<a.sonido<<' '<<voz.silencio<<' '
                         <<voz.muestra<<' '<<voz.pitch<<' '<<voz.pcm.size()<<' '<<HashPcm(voz.pcm)<<'\n';
              }
              if(it->second.silencio)++silencio;
              else if(!it->second.pcm.empty())++pcm;
              else {++pendiente;std::cout<<"PENDIENTE "<<cmn<<' '<<cod<<' '<<t<<' '<<v<<'\n';}
            }
          }
        }
      }
    }
    // Campo938 se compara completo contra decoder independiente.
    for(int sonido=1;sonido<=8;++sonido) {
      const auto v=dbz3::iwiop::Leer(Entrada(game/"USR/DATA_CMN.AFS",938),sonido);
      Comprobar(!v.pcm.empty(),"paso938 PCM");
      std::cout<<"PCM9C DATA_CMN.AFS 938 "<<sonido<<" 0 "<<v.muestra<<' '<<v.pitch<<' '<<v.pcm.size()<<' '<<HashPcm(v.pcm)<<'\n';
    }
    std::cout<<"CORPUS9C "<<total<<' '<<pcm<<' '<<silencio<<' '<<pendiente<<'\n';
    return 0;
  } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

// Corpus original y salida seca real del lector; no abre dispositivos.
#include <filesystem>
#undef _WIN32
#include "../src/iw_sonido.h"
#define main AyudantesAudioV
#include "iw_ap_audio_prueba.cpp"
#undef main

void Guardar(const std::filesystem::path& p,const std::vector<int16_t>& pcm) {
  std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(pcm.data()),pcm.size()*2);
  Comprobar(bool(f),"guardar salida PCM");
}
int main(int argc,char** argv) {
  try {
    Comprobar(argc==3 || argc==4,"uso: prueba <IW USA> <salida existente> [ELF EU]");
    const std::filesystem::path game=argv[1],out=argv[2];
    const auto elf=Fichero(game/"SLUS_218.42");const auto azar=LeerAzarVoz(elf);
    uint32_t seed=0x12345678;
    auto memoria=[&](){for(int n=0;n<32;++n)seed=(seed<<1)|(((seed>>31)^(seed>>4))&1);return seed-1;};
    std::map<std::array<int,3>,dbz3::iwiop::Voz> cache;
    int total=0,pcm=0,silencio=0,pendiente=0;
    for(int cmn:{204,912,913,914,915,916,917,918,919,920,921,922,923,924,925,926,927,928,929,930,931,932}) {
      const auto amb=Entrada(game/"USR/DATA_CMN.AFS",cmn);
      const int banco=BancoActor(elf,cmn==204 || cmn==915?CmnActor(0):uint32_t(cmn));
      for(uint32_t hi=0;hi<U32(amb,16);++hi) {
        const auto b=dbz3::pasarela::HijoAmb(amb,hi);if(U32(b,0)!=0x4b534223)continue;
        for(uint32_t cod=0;cod<U32(b,16);++cod) {
          const auto blk=U32(b,U32(b,20)+4*cod);if(!blk)continue;
          for(uint32_t ln=0;ln<U32(b,blk+40);++ln) {
            const auto l=U32(b,blk+44)+8*ln;if((U32(b,l)&65535)!=7)continue;
            for(uint32_t i=0;i<U32(b,l)>>16;++i) {
              const auto e=U32(b,l+4)+16*i,t=U32(b,e+4),v=U32(b,e+8);
              if(t!=1 && t!=2 && t!=3)continue;
              ++total;const auto a=t==3?ResolverVoz(v,banco,false,azar,memoria):Resolver(t,v,banco,false);
              if(a.ruta==Ruta::SilencioOriginal){++silencio;continue;}
              if(a.ruta!=Ruta::Cmn && a.ruta!=Ruta::Voz){++pendiente;continue;}
              const std::array<int,3> key={int(a.ruta),a.banco,a.sonido};
              auto [it,nueva]=cache.try_emplace(key);
              if(nueva) {
                const char* afs=a.ruta==Ruta::Cmn?"DATA_CMN.AFS":"ADX_USA.AFS";
                it->second=dbz3::iwiop::Leer(Entrada(game/"USR"/afs,a.banco),a.sonido);
                const auto& voz=it->second;
#ifdef DBZ3_IW_AUDIO_F_NUEVO
                if(!voz.pcm.empty())std::cout<<"RUTA "<<a.banco<<' '<<a.sonido<<' '<<int(voz.rutas)<<'\n';
#endif
                const std::string nombre=std::to_string(a.banco)+"_"+std::to_string(a.sonido)+".s16";
                Guardar(out/nombre,voz.pcm);
                std::cout<<"PCM "<<afs<<' '<<a.banco<<' '<<a.sonido<<' '<<voz.silencio<<' '
                         <<voz.muestra<<' '<<voz.pitch<<' '<<voz.adsr1<<' '<<voz.adsr2<<' '
                         <<voz.pcm.size()<<' '<<HashPcm(voz.pcm)<<' '<<nombre<<'\n';
              }
              const auto& voz=it->second;
              const int estado=voz.silencio?1:voz.pcm.empty()?0:2;
              if(estado==1)++silencio;else if(estado==2)++pcm;else ++pendiente;
              std::cout<<"EVENTO "<<cmn<<' '<<cod<<' '<<t<<' '<<v<<' '<<a.banco<<' '<<a.sonido<<' '<<estado<<'\n';
            }
          }
        }
      }
    }
    std::cout<<"CORPUS "<<total<<' '<<pcm<<' '<<silencio<<' '<<pendiente<<'\n';
#ifdef DBZ3_IW_AUDIO_F_NUEVO
    uint32_t seed_original=0;
    Comprobar(dbz3::iwiop::LeerSemillaAzar(elf,seed_original),"semilla del ELF original");
    std::cout<<"SEMILLA "<<seed_original<<'\n';
    if(argc==4) {
      uint32_t eu=0;Comprobar(dbz3::iwiop::LeerSemillaAzar(Fichero(argv[3]),eu),"semilla ELF EU");
      std::cout<<"SEMILLA_EU "<<eu<<'\n';
    }
    for(size_t len:{size_t(0),size_t(32),size_t(52),elf.size()/2}) {
      uint32_t untouched=99;
      Comprobar(!dbz3::iwiop::LeerSemillaAzar({elf.begin(),elf.begin()+len},untouched) && untouched==99,
                "ELF truncado no inventa semilla");
    }
    // Todos los pasos Memory(n) se imprimen para compararlos ejecutando EE,
    // mezclando consumidores de8/32bits y cambio/restauracion de semilla.
    for(uint32_t initial:{0u,1u,seed_original,0x12345678u,0x80000000u,0xffffffffu}) {
      dbz3::iwiop::Azar rng(initial);
      for(int bits:{0,8,32,8,1,31,32,-1}) {
        const auto antes=rng.estado;const auto value=rng.Memory(bits);
        std::cout<<"MEMORY "<<antes<<' '<<bits<<' '<<value<<' '<<rng.estado<<'\n';
      }
      const auto saved=rng.Cambiar(0xaabbccdd);rng.Memory(8);
      Comprobar(rng.Cambiar(saved)!=saved && rng.estado==saved,"seed swap/restaurar");
    }
    // Impulso, extremos y barrido de TODAS las fases, independiente del corpus.
    for(int i=0;i<256;++i) {
      for(int n=0;n<4;++n) {
        std::vector<int16_t> v(4);v[n]=32767;
        std::cout<<"FILTRO "<<i<<' '<<n<<' '<<dbz3::iwiop::Interpolar(v,(3ull<<12)|(i<<4))<<'\n';
      }
    }
    auto bank=Entrada(game/"USR/DATA_CMN.AFS",954);
    Comprobar(dbz3::iwiop::Leer(bank,88).secuencia_ausente,"rechazo retail fuera de banco");
    for(size_t len:{size_t(0),size_t(16),size_t(64),size_t(200)}) {
      auto bad=bank;bad.resize(len);
      auto v=dbz3::iwiop::Leer(bad,88);
      Comprobar(v.pcm.empty() && !v.silencio,"truncado NO es silencio original");
    }
    std::cout<<"PASS fronteras, filtro, routing y RNG; salida/reverb se verifican en el arnes separado\n";
#endif
    return 0;
  } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

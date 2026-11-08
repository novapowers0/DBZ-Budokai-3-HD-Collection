// DSP y mezcla puros: ninguna ventana/dispositivo ni runtime del juego.
#define NOMINMAX
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include "../src/iw_sonido.h"
using namespace dbz3::iwsonido;
void Exigir(bool ok,const char* mensaje){if(!ok)throw std::runtime_error(mensaje);}
void Guardar(const std::filesystem::path& p,const std::vector<int16_t>& b) {
  std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size()*2);Exigir(bool(f),"escritura PCM");
}
std::vector<int16_t> Render(Mezclador& m,size_t n) {std::vector<int16_t>b(n*2);m.Renderizar(b.data(),n);return b;}
int main(int argc,char** argv) {
  try {
    Exigir(argc==2,"uso: reverb <directorio existente>");const std::filesystem::path out=argv[1];
    for(size_t i=0;i<kEfecto3.size();++i)std::cout<<"REG "<<i<<' '<<kEfecto3[i]<<'\n';
    constexpr size_t n=96000;
    for(int caso=0;caso<3;++caso) {
      Reverb r;r.Configurar3();std::vector<int16_t> b(n*2),entrada(n*2);
      for(size_t i=0;i<n;++i) {
        Estereo in;
        if(caso==0 && !i)in.l=32767;
        if(caso==1 && !i)in.r=-32768;
        if(caso==2 && i<128)in={i&1?32767:-32768,i%3?32767:-32768};
        entrada[i*2]=int16_t(in.l);entrada[i*2+1]=int16_t(in.r);
        const auto wet=r.Paso(in);b[i*2]=int16_t(wet.l);b[i*2+1]=int16_t(wet.r);
      }
      Guardar(out/("impulso_"+std::to_string(caso)+"_entrada.s16"),entrada);
      Guardar(out/("impulso_"+std::to_string(caso)+"_cola.s16"),b);
      int pico=0;int64_t cola=0;size_t primero=n;
      for(size_t i=0;i<n;++i) {
        const int a=std::abs(int(b[i*2]))+std::abs(int(b[i*2+1]));
        if(a && primero==n)primero=i;pico=std::max(pico,a);if(i>=48000)cola+=a;
      }
      Exigir(pico>0 && cola>0,"impulso sin cola persistente");
      std::cout<<"IMPULSO "<<caso<<' '<<primero<<' '<<pico<<' '<<cola<<'\n';
    }
    {
      Mezclador m;m.Maestro(.5);m.Iniciar(std::vector<int16_t>(10,30000),48000);m.Iniciar(std::vector<int16_t>(10,30000),48000);
      const auto b=Render(m,5);Exigir(std::all_of(b.begin(),b.end(),[](int v){return v==16383;}),"MasterVolume debe ir despues de saturar la mezcla");
      Exigir(!m.Voces(),"voz acabada no retirada");
    }
    {
      Mezclador m;Opciones o;o.canal=m.Slot(77);
      const auto viejo=m.Iniciar(std::vector<int16_t>(20,9000),48000,o);
      const auto nuevo=m.Iniciar(std::vector<int16_t>(20,2000),48000,o);
      auto b=Render(m,1);Exigir(b[0]==2000,"slot sustituido emite voz vieja");
      Exigir(!m.Parar(viejo) && m.Voces()==1,"handle viejo afecta nueva voz");
      m.PausarSlot(77,true);b=Render(m,1);Exigir(b[0]==0,"pausa de slot");
      m.PausarSlot(77,false);b=Render(m,1);Exigir(b[0]==2000,"reanudar slot");
      m.PararSlot(77);b=Render(m,1);Exigir(b[0]==0 && !m.Parar(nuevo),"parar slot");
    }
    {
      Mezclador m;const auto h=m.Iniciar({1000,1000,2000,2000},48000);
      Exigir(m.Pausar(h,true),"pausar handle");Exigir(Render(m,1)[0]==0,"pausa avanza");
      m.Pausar(h,false);m.Maestro(1,true);Exigir(Render(m,1)[0]==0,"mute");
      m.Maestro(1,false);Exigir(Render(m,1)[0]==2000,"mute debe avanzar sin reiniciar");
    }
    {
      Mezclador m;m.Iniciar({0,0,1000,1000,2000,2000,3000,3000},32000);
      const auto b=Render(m,6);const int esperado[]={0,666,1333,2000,2666,3000};
      for(size_t i=0;i<6;++i)Exigir(b[i*2]==esperado[i] && b[i*2+1]==esperado[i],"remuestreo ADX estereo");
    }
    {
      Mezclador a,b;Opciones wet;wet.rutas=12;
      a.ConfigurarEfecto3(0);b.ConfigurarEfecto3(0);
      a.Iniciar({32767,0},48000,wet);b.Iniciar({32767,0},48000,wet);
      auto entero=Render(a,n),parcial=Render(b,8000);
      // Otro evento seco no reinicia ni modifica la cola anterior.
      b.Iniciar(std::vector<int16_t>(200,0),48000);
      const auto resto=Render(b,n-8000);parcial.insert(parcial.end(),resto.begin(),resto.end());
      Exigir(entero==parcial,"cola depende de fronteras/eventos");Guardar(out/"mezcla_cola.s16",entero);
      Mezclador dos;wet.core=1;dos.ConfigurarEfecto3(1);dos.Iniciar({32767,0},48000,wet);
      Exigir(Render(dos,n)==entero,"core1 procesa de nuevo core0 o difiere de core0 aislado");
    }
    {
      Mezclador m;Exigir(!m.Iniciar({1},48000),"PCM impar aceptado");
      Exigir(!m.Iniciar({1,1},0),"frecuencia cero aceptada");
      Opciones o;o.core=2;Exigir(!m.Iniciar({1,1},48000,o),"core invalido");
      m.Maestro(std::nanf(""));m.Iniciar({32767,32767},48000);Exigir(Render(m,1)[0]==0,"volumen NaN");
    }
    {
      // La salida automatica SDL tiene su propio receptor. Aqui solo DSP:
      // elegir callback antes de Enviar impide abrir un dispositivo real.
      Reproductor r;Exigir(r.DispositivoAutomatico(false),"seleccionar callback externo");
      Exigir(r.Enviar({1500,1500},48000)!=0,"callback externo no acepta PCM");
      int16_t b[2];r.Procesar(b,1);Exigir(b[0]==1500 && b[1]==1500,"callback externo sin dispositivo");
    }
    std::cout<<"PASS volumen final, slots/handles, pausa/mute, remuestreo, cola por bloques y dos cores\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}

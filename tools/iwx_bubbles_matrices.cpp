// Diagnostico del puente LIVE47 -> matrices; usa solo fixtures externos.
// No sustituye matrices originales ni activa ningun camino del renderer.
#include "pose47.h"
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

std::vector<std::uint8_t> leer(const char* nombre) {
  std::ifstream f(nombre, std::ios::binary);
  if (!f) throw std::runtime_error(nombre);
  return {std::istreambuf_iterator<char>(f), {}};
}

float diferencia(float a, float b) {
  return std::isfinite(a) && std::isfinite(b) ? std::fabs(a-b)
      : std::numeric_limits<float>::infinity();
}

void comparar(const char* nombre, const bb47::Live& got,
              const std::vector<std::uint8_t>& original) {
  if (original.size() != sizeof(bb47::Live)) throw std::runtime_error("LIVE truncado");
  float maximo = 0;
  unsigned distintos = 0;
  for (unsigned i=0; i<47; ++i) {
    bb47::Live128 esperado;
    std::memcpy(&esperado, original.data()+128*i, 128);
    float rotacion=0, traslacion=0, qps=0;
    unsigned peor=0;
    float error=0;
    for (unsigned k=0; k<16; ++k) {
      const float d=diferencia(got[i].matrix[k], esperado.matrix[k]);
      if (d>error) { error=d; peor=k; }
      if (k>=12 && k<=14) traslacion=std::max(traslacion,d);
      else rotacion=std::max(rotacion,d);
    }
    for (unsigned k=0; k<4; ++k) qps=std::max(qps,diferencia(got[i].q[k],esperado.q[k]));
    for (unsigned k=0; k<3; ++k) {
      qps=std::max(qps,diferencia(got[i].p[k],esperado.p[k]));
      qps=std::max(qps,diferencia(got[i].s[k],esperado.s[k]));
    }
    maximo=std::max(maximo,error);
    distintos+=error!=0;
    std::cout << "MATRIZ " << nombre << " hueso=" << i << " error=" << error
      << " rotacion=" << rotacion << " traslacion=" << traslacion << " qps=" << qps
      << " componente=" << peor << " original=" << esperado.matrix[peor]
      << " candidato=" << got[i].matrix[peor] << '\n';
  }
  std::cout << "RESUMEN " << nombre << " matrices_distintas=" << distintos
    << "/47 error_max=" << maximo << " normal=false paridad_visual=false\n";
}

void target(const char* nombre,const bb47::Pose& got,const std::vector<std::uint8_t>& raw){
  if(raw.size()!=sizeof(bb47::Pose))throw std::runtime_error("TARGET truncado");
  float q=0,p=0,s=0;
  for(unsigned i=0;i<47;++i){
    bb47::Target64 esperado;
    std::memcpy(&esperado,raw.data()+64*i,64);
    for(unsigned k=0;k<4;++k)q=std::max(q,diferencia(got[i].q[k],esperado.q[k]));
    for(unsigned k=0;k<3;++k){
      p=std::max(p,diferencia(got[i].p[k],esperado.p[k]));
      s=std::max(s,diferencia(got[i].s[k],esperado.s[k]));
    }
  }
  std::cout<<"TARGET "<<nombre<<" quat="<<q<<" pos="<<p<<" scale="<<s<<'\n';
}

void entrada(const std::filesystem::path& dir,const bb47::Decoder& decoder){
  auto archivo=[&](const char* name){return leer((dir/name).string().c_str());};
  const auto antes=archivo("antes_live.bin"),despues=archivo("despues_live.bin");
  const auto meta_raw=archivo("meta.bin"),mapa0=archivo("antes_mapa.bin"),mapa1=archivo("mapa.bin");
  if(meta_raw.size()!=32||mapa0.size()!=94||mapa1.size()!=94)throw std::runtime_error("Entrada truncada");
  bb47::Bytes meta{meta_raw};
  if(meta.at<float>(0)!=1||meta.u(24)!=0x4000f||meta.u(28)!=0x40000)
    throw std::runtime_error("No es la entrada natural11D->D2 observada");
  std::array<std::int16_t,47> m0,m1;
  std::memcpy(m0.data(),mapa0.data(),94);std::memcpy(m1.data(),mapa1.data(),94);
  const auto t0=decoder.sample(meta.u(24),meta.at<float>(16),m0);
  const auto t1=decoder.sample(meta.u(28),meta.at<float>(20),m1);
  target("INTRO_11D",t0,archivo("antes_target.bin"));
  target("ENTRADA_D2",t1,archivo("target.bin"));
  bb47::Instance medido;
  if(!medido.load(antes.data(),antes.size()))throw std::runtime_error("LIVE anterior invalido");
  medido.enter(.005f,meta.at<float>(12));
  const float beta=medido.apply(t1,1.2f);
  comparar("ENTRADA_DESDE_LIVE",medido.live,despues);
  bb47::Live fuente_facial{};std::array<bool,47> mascara_facial{};
  if(std::filesystem::exists(dir/"facial.bin")){
    // 25FA90, param3=0: copia los 128B del modelo facial independiente al
    // cuerpo. La fuente es RAM separada, no el LIVE corporal que comparamos.
    const auto raw=archivo("facial.bin");bb47::Bytes facial{raw};
    const unsigned n=facial.u(0),cuenta=facial.u(4);
    if(n>9||cuenta<1||cuenta>128||raw.size()!=16+136*n||
       facial.at<std::int32_t>(8)==-2||facial.u(12)!=0)
      throw std::runtime_error("Contrato facial25FA90 invalido/inactivo");
    auto publicado=medido.live;
    unsigned iguales=0;std::array<bool,47> usado{};
    for(unsigned j=0;j<n;++j){
      const auto dst=facial.at<std::int32_t>(16+136*j);
      const auto src=facial.at<std::int32_t>(20+136*j);
      if(dst<0||dst>=47||src<0||unsigned(src)>=cuenta||usado[dst])
        throw std::runtime_error("Remapeo facial invalido");
      usado[dst]=true;
      std::memcpy(&publicado[dst],raw.data()+24+136*j,128);
      fuente_facial[dst]=publicado[dst];mascara_facial[dst]=true;
      const bool igual=std::memcmp(&publicado[dst],despues.data()+128*dst,128)==0;
      iguales+=igual;
      std::cout<<"FACIAL fuente="<<src<<" destino="<<dst<<" igual128="<<igual<<'\n';
    }
    comparar("ENTRADA_CON_FACIAL_ORIGINAL",publicado,despues);
    std::cout<<"FACIAL_RESUMEN copias="<<n<<" iguales128="<<iguales
      <<" fuente=RAM_facial_independiente normal=false paridad_visual=false\n";
    if(iguales!=n)throw std::runtime_error("La fuente facial no coincide con la publicacion observada");
  }

  // Diagnostico: beta1 sobrescribe Q/P/S y matrices. No usar este estado
  // artificial como pose del port; aqui demuestra la independencia del historial.
  bb47::Live vacio{};bb47::Instance terminal;
  terminal.load(vacio.data(),sizeof vacio);terminal.enter(1,0);terminal.apply(t0,1.2f);
  comparar("INTRO_11D_TERMINAL",terminal.live,antes);
  terminal.enter(.005f,meta.at<float>(12));terminal.apply(t1,1.2f);
  comparar("ENTRADA_DESDE_11D",terminal.live,despues);
  if(std::filesystem::exists(dir/"facial.bin")){
    for(unsigned i=0;i<47;++i)if(mascara_facial[i])terminal.live[i]=fuente_facial[i];
    comparar("ENTRADA_11D_Y_FACIAL_ORIGINAL",terminal.live,despues);
  }

  bb47::Instance sin_mezcla;
  sin_mezcla.load(vacio.data(),sizeof vacio);sin_mezcla.enter(1,0);sin_mezcla.apply(t1,1.2f);
  comparar("D2_SIN_MEZCLA",sin_mezcla.live,despues);
  std::cout<<"ENTRADA beta="<<beta<<" cb4="<<medido.u<<" cb4_original="<<meta.at<float>(8)
    <<" normal=false paridad_visual=false\n";
}

int main(int argc,char** argv) {
  try {
    std::cout << std::setprecision(9);
    const auto amo=leer("amo.bin"), amm=leer("amm.bin"), seno=leer("lut.bin");
    const auto mapa_raw=leer("target0.bin"), old=leer("oldlive.bin"), next=leer("expectedlive.bin");
    if (seno.size()!=16385*4 || mapa_raw.size()!=102+47*64)
      throw std::runtime_error("LUT/TARGET invalidos");
    std::vector<float> lut(seno.size()/4);
    std::memcpy(lut.data(),seno.data(),seno.size());
    bb47::Decoder decoder(amo,amm,lut);
    if(argc==2){entrada(std::filesystem::path(argv[1]),decoder);return 0;}
    if(argc!=1)throw std::runtime_error("Usar sin argumentos o con carpeta de entrada");
    std::array<std::int16_t,47> mapa;
    std::memcpy(mapa.data(),mapa_raw.data()+8,94);
    bb47::Instance mezcla;
    if (!mezcla.load(old.data(),old.size())) throw std::runtime_error("LIVE inicial invalido");
    mezcla.enter(.005f,.03f);
    mezcla.apply(decoder.sample(0x40003,0,mapa),1.2f);
    comparar("DD_D5",mezcla.live,next);

    const auto antes=leer("fin_before.bin"), despues=leer("fin_after.bin"), meta_raw=leer("fin_meta.bin");
    if (meta_raw.size()!=512) throw std::runtime_error("metadatos fin9 invalidos");
    const bb47::Bytes meta{meta_raw};
    bb47::Instance fin;
    if (!fin.load(antes.data(),antes.size())) throw std::runtime_error("LIVE fin9 invalido");
    auto flags=meta.u(44);
    fin.consume(1,flags,meta.v(48),meta.v(96),0);
    comparar("FIN9",fin.live,despues);
    // Es un diagnostico: se conserva toda discrepancia, sin umbral de aceptacion.
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}

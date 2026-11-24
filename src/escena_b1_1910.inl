// Reproductor de la escena PAL 1910 antes del titulo/VS. No usa los dos slots
// del combate: las matrices y la camara comparten el mundo de B1.
#include "ps2_modelo.h"
#include <rex/platform.h>
#include <toml++/toml.hpp>
#include <map>
#include <bit>
#include <stdexcept>
#include <tuple>
#if REX_PLATFORM_WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

namespace dbz3::pasarela::nave {
bool LeerFigura1910(const std::filesystem::path&, ps2::Modelo&,
                    std::vector<std::vector<std::pair<float, int>>>&, std::vector<std::string>&);
}
namespace dbz3::pasarela {
std::vector<int16_t> LeerVoz1910(AfsPs2&, int, int, int);
}

namespace dbz3::pasarela::cine1910 {
namespace {
constexpr double kHz = 24000, kRate = 60;
using Bytes = std::vector<uint8_t>;
void Exigir(bool ok, const char* campo) {
  if (!ok) throw std::runtime_error(campo);
}
Bytes Leer(const std::filesystem::path& dir, const std::string& nombre, size_t tope = 32 << 20) {
  Exigir(!nombre.empty() && nombre.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.") == std::string::npos &&
             nombre != "." && nombre != "..", "nombre de recurso");
  std::ifstream f(dir / nombre, std::ios::binary | std::ios::ate);
  const auto n = f.tellg();
  Exigir(n > 0 && uint64_t(n) <= tope, "archivo ausente o demasiado grande");
  Bytes out(static_cast<size_t>(n));
  f.seekg(0);
  Exigir(bool(f.read(reinterpret_cast<char*>(out.data()), n)), "lectura incompleta");
  return out;
}
uint32_t U32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
// Misma seleccion PAL que las laminas del capitulo; se toma del ajuste del jugador.
const char* BancoPal(int idioma) {
  switch (idioma) {
    case 3: return "DATA_GE.AFS";
    case 4: return "DATA_FR.AFS";
    case 5: return "DATA_SP.AFS";
    case 6: return "DATA_IT.AFS";
    default: return "DATA_EN.AFS";
  }
}
uint32_t U32(const Bytes& b, size_t p) {
  Exigir(p <= b.size() && b.size()-p >= 4,"puntero de recurso PAL");
  return U32(b.data()+p);
}
int16_t S16(const Bytes& b, size_t p) {
  Exigir(p <= b.size() && b.size()-p >= 2,"clave AMP truncada");
  return std::bit_cast<int16_t>(uint16_t(unsigned(b[p]) | unsigned(b[p+1])<<8));
}
int I(const toml::table& t, const char* k, int lo, int hi) {
  const auto v = t[k].value_exact<int64_t>();
  Exigir(v && *v >= lo && *v <= hi, k);
  return int(*v);
}
double D(const toml::table& t, const char* k, double lo, double hi) {
  const auto v = t[k].value<double>();
  Exigir(v && *v >= lo && *v <= hi, k);
  return *v;
}
const toml::array& Lista(const toml::table& t, const char* k, size_t max, size_t min = 1) {
  const auto* a = t[k].as_array();
  Exigir(a && a->size() >= min && a->size() <= max, k);
  return *a;
}
const toml::table& Tabla(const toml::node& n) {
  auto* t = n.as_table();
  Exigir(t != nullptr, "se esperaba una tabla");
  return *t;
}
template<size_t N>
void Pista(const toml::table& t, const char* k, int cuadros, std::vector<std::array<float,N>>& dst) {
  for (const auto& node : Lista(t, k, size_t(cuadros))) {
    const auto* a = node.as_array();
    Exigir(a && a->size() == N, k);
    std::array<float,N> p{};
    for (size_t i = 0; i < N; ++i) {
      const auto v = (*a)[i].value<double>();
      Exigir(v && std::isfinite(*v) && std::abs(*v) <= 1e6, k);
      p[i] = float(*v);
    }
    Exigir(p[0] >= 0 && p[0] < cuadros && (dst.empty() || p[0] > dst.back()[0]), k);
    dst.push_back(p);
  }
}
template<size_t N>
std::array<float,N-1> Muestra(const std::vector<std::array<float,N>>& p, float f) {
  auto it = std::upper_bound(p.begin(), p.end(), f, [](float v, const auto& k) { return v < k[0]; });
  const auto& a = it == p.begin() ? *it : *std::prev(it);
  const auto& b = it == p.end() ? a : *it;
  std::array<float,N-1> out{};
  const float u = b[0] > a[0] ? std::clamp((f-a[0])/(b[0]-a[0]), 0.f, 1.f) : 0.f;
  for (size_t i = 1; i < N; ++i) out[i-1] = a[i] + (b[i]-a[i])*u;
  return out;
}

using Punto = std::array<float,3>;
Punto Cruz(Punto a, Punto b) {
  return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
float Dot(Punto a, Punto b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
bool Unit(Punto& p) {
  const float l = std::sqrt(Dot(p,p));
  if (!(l > 1e-6f)) return false;
  for (float& x : p) x /= l;
  return true;
}
// Pistas de AMC/AML: n en +8, claves (u32 cuadro, floats) desde +12.
template<size_t N>
std::vector<std::array<float,N>> Claves(const Bytes& b, size_t off, int cuadros) {
  Exigir(off!=0,"pista PAL ausente");
  const size_t n=U32(b,off+8);
  Exigir(n>0 && n<=size_t(cuadros),"numero de claves PAL");
  std::vector<std::array<float,N>> out;
  for (size_t i=0;i<n;++i) {
    const size_t p=off+12+4*N*i;
    std::array<float,N> key{};
    const auto f=U32(b,p);
    Exigir(f<uint32_t(cuadros) && (out.empty() || float(f)>out.back()[0]),"cuadro de pista PAL");
    key[0]=float(f);
    for (size_t j=1;j<N;++j) {
      key[j]=std::bit_cast<float>(U32(b,p+4*j));
      Exigir(std::isfinite(key[j]) && std::abs(key[j])<=1e6f,"clave de pista PAL");
    }
    out.push_back(key);
  }
  return out;
}
struct LuzClip { int cuadros=0; std::vector<std::array<float,4>> dir,rgb; };
std::array<LuzClip,14> LeerLuz(const Bytes& aml) {
  Exigir(U32(aml,0)==0x4c4d4123 && U32(aml,16)==14,"#AML 1909");
  const size_t tabla=U32(aml,20);
  std::array<LuzClip,14> out;
  for (size_t c=0;c<out.size();++c) {
    const size_t fila=tabla+16*c, pistas=U32(aml,fila+12);
    const uint32_t nf=U32(aml,fila+8);
    Exigir(U32(aml,fila)==10 && nf==2,"tipo de luz PAL 1910");
    auto& l=out[c]; l.cuadros=int(nf);
    l.dir=Claves<4>(aml,U32(aml,pistas),l.cuadros);
    l.rgb=Claves<4>(aml,U32(aml,pistas+4),l.cuadros);
  }
  return out;
}

#include "escena_b1_raster.inl"

struct Toma { double t; cinematica::Paso paso; };
ps2::Camara Camara(const Toma& toma, double u, ImVec2 centro, ImVec2 mitad) {
  const auto& p = toma.paso;
  const float f = float(std::clamp(u-toma.t, 0., double(p.cuadros-1)));
  const auto o = Muestra(p.cam_ojo,f), b = Muestra(p.cam_obj,f);
  Punto z{o[0]-b[0],o[1]-b[1],o[2]-b[2]};
  ps2::Camara cam;
  if (!Unit(z)) return cam;
  auto x = Cruz({0,1,0},z);
  if (!Unit(x)) return cam;
  auto y = Cruz(z,x);
  if (!p.cam_roll.empty()) {
    const float r = -Muestra(p.cam_roll,f)[0], c = std::cos(r), s = std::sin(r);
    for (int i=0;i<3;++i) { const float v=x[i]; x[i]=c*v+s*y[i]; y[i]=c*y[i]-s*v; }
  }
  const Punto axes[] = {x,y,z};
  for (int i=0;i<3;++i) {
    for (int j=0;j<3;++j) cam.m[4*j+i] = axes[i][j];
    cam.m[12+i] = -Dot(axes[i],o);
  }
  cam.m[15]=1;
  cam.c=centro; cam.h=mitad;
  cam.fov=std::clamp(Muestra(p.cam_fov,f)[0],0.08726646f,3.054326f);
  // FUN_001373d0(480): focal GS 240/tan(fov/2). 0x335e50: escalas 1 y 16/15,
  // la vertical se divide entre dos en FUN_0013d5d0. FUN_00144bf0 situa
  // los sprites con y * (9.142858/16): 448 unidades de UI son 256 del GS.
  cam.foco_x=mitad.x*(240.f/320.f);
  cam.foco_y=mitad.y; // 128 GS * (448/256) = 224
  cam.corte_cercano=raster::Cerca;
  cam.ok=true;
  return cam;
}

struct Clip {
  int cuadros = 0;
  std::vector<ps2::M34> hd;
  ps2::Anim ps;
};
struct MallaActor {
  bool hd = false;
  ps2::Modelo m;
  std::vector<std::string> huesos_ps2;
  int cabeza=-1, cara=-1;
  std::vector<std::pair<size_t,ps2::M34>> pelo;
  std::array<int,6> faldon{-1,-1,-1,-1,-1,-1};
  std::vector<std::vector<std::pair<float,int>>> rampa;
};
std::vector<std::string> Nombres(const Bytes& b, size_t n, size_t o) {
  Exigir(n > 0 && n <= 255 && o <= b.size() && n*32 <= b.size()-o,"nombres AMO/AMM");
  std::vector<std::string> out;
  for (size_t i=0;i<n;++i) {
    const auto* s = reinterpret_cast<const char*>(b.data()+o+i*32);
    out.emplace_back(s,std::find(s,s+32,'\0'));
  }
  return out;
}
ps2::Anim LeerClipPS(const Bytes& amm, int id, const std::vector<std::string>& huesos) {
  Exigir(amm.size() >= 32 && !std::memcmp(amm.data(),"#AMM",4),"cabecera AMM");
  const auto nombres=Nombres(amm,U32(amm.data()+24),U32(amm.data()+28));
  auto origen=ps2::LeerAnim(amm,id,nombres.size());
  Exigir(bool(origen),"clip AMM no legible");
  ps2::Anim salida;
  salida.cuadros=origen.cuadros;
  salida.ang.resize(huesos.size()); salida.pos.resize(huesos.size());
  for (size_t i=0;i<huesos.size();++i) {
    const auto it=std::find(nombres.begin(),nombres.end(),huesos[i]);
    if (it == nombres.end()) continue;  // sin pareja probada: reposo
    const size_t j=it-nombres.begin();
    salida.ang[i]=origen.ang[j]; salida.pos[i]=origen.pos[j];
  }
  return salida;
}
// FUN_00150cc0 casa nombres de nodos de AMO y AMP. FUN_00150dc0
// aplica la ultima clave s16/s16; -1 no emite malla (FUN_0014e9d0).
struct Partes {
  int cuadros=0;
  std::vector<std::vector<std::pair<int,int>>> nodo;
};
Partes LeerPartes(const Bytes& amp, int clip, int cuadros, const std::vector<std::string>& huesos) {
  Exigir(U32(amp,0)==0x504d4123 && U32(amp,16)==1 && clip==0,"banco #AMP 1910");
  const size_t fila=U32(amp,20), pistas=U32(amp,fila+12), n=U32(amp,24);
  Exigir(U32(amp,fila)==1 && U32(amp,fila+8)==uint32_t(cuadros),"clip #AMP 1910");
  const auto nombres=Nombres(amp,n,U32(amp,28));
  Partes out{cuadros,{}};
  out.nodo.resize(huesos.size());
  size_t leidas=0;
  for (size_t b=0;b<huesos.size();++b) {
    if (huesos[b].empty()) continue;
    const auto it=std::find(nombres.begin(),nombres.end(),huesos[b]);
    if (it==nombres.end()) continue;
    const size_t p=U32(amp,pistas+4*size_t(it-nombres.begin()));
    if (!p) continue;
    const size_t nc=U32(amp,p+8);
    Exigir(nc>0 && nc<=size_t(cuadros),"claves #AMP");
    auto& dest=out.nodo[b];
    for (size_t k=0;k<nc;++k) {
      const int f=S16(amp,p+12+4*k), v=S16(amp,p+14+4*k);
      Exigir(f>=0 && f<cuadros && (dest.empty() || f>dest.back().first) && (v==-1 || v==0),"selector #AMP");
      dest.emplace_back(f,v);
    }
    ++leidas;
  }
  Exigir(leidas>0,"#AMP sin nodos vinculados");
  return out;
}
bool ParteVisible(const Partes& p, size_t hueso, float f) {
  const auto& k=p.nodo[hueso];
  if (k.empty()) return true;
  const auto it=std::upper_bound(k.begin(),k.end(),f,[](float v,const auto& a){return v<a.first;});
  return (it==k.begin() ? *it : *std::prev(it)).second>=0;
}
void FiltrarPartes(const Partes& p, float f, const ps2::Malla& m, ps2::Malla& out) {
  std::array<bool,256> visible{};
  for (size_t b=0;b<p.nodo.size();++b) visible[b]=ParteVisible(p,b,f);
  out.tris.clear();
  for (const auto& tr:m.tris) if (visible[tr.hueso[0]]) out.tris.push_back(tr);
}

// HMS3 solo conserva un hueso por vertice. El pelo HD de Raditz comparte
// triangulos entre HEAD y HAIRn: al perder los pesos aparecen estiramientos.
// Conservar su forma HD de reposo en el espacio de la cabeza (solo slot 10).
void PrepararPelo(MallaActor& m, const std::vector<std::string>& nombres) {
  const auto& sk=*m.m.esq;
  const auto it=std::find(nombres.begin(),nombres.end(),"HEAD");
  Exigir(it!=nombres.end() && nombres.size()==sk.padre.size(),"esqueleto Raditz HMS3");
  m.cabeza=int(it-nombres.begin());
  const auto cara=std::find(nombres.begin(),nombres.end(),"L00_FACE");
  if (cara!=nombres.end()) m.cara=int(cara-nombres.begin());
  std::vector<ps2::M34> w(nombres.size());
  for (size_t b=0;b<w.size();++b) {
    const int p=sk.padre[b]; w[b]=p<0 ? sk.local[b] : w[p]*sk.local[b];
  }
  const auto inv=w[m.cabeza].Inversa();
  for (size_t b=0;b<w.size();++b) if (nombres[b].starts_with("HAIR")) {
    int p=sk.padre[b];
    while (p>=0 && p!=m.cabeza) p=sk.padre[p];
    Exigir(p==m.cabeza,"hueso de pelo fuera de HEAD");
    m.pelo.push_back({b,inv*w[b]});
  }
  Exigir(!m.pelo.empty(),"pelo Raditz HMS3 ausente");
  auto idx=[&](std::string_view s) {
    const auto i=std::find(nombres.begin(),nombres.end(),s);
    return i!=nombres.end() ? int(i-nombres.begin()) : -1;
  };
  m.faldon={idx("WAIST"),idx("FWPAT"),idx("LWPAT1"),idx("RWPAT1"),idx("LLEG2"),idx("RLEG2")};
  for (auto& tr:m.m.malla.tris) {
    for (int j=0;j<3;++j) {
      const size_t b=tr.hueso[j];
      if (tr.tex==3 && nombres[b]=="LARMROT" && sk.padre[b]>=0) {
        const auto& l=sk.local[b];
        tr.p[j]=l.por(tr.p[j]); tr.n[j]=l.gira(tr.n[j]);
        tr.hueso[j]=uint8_t(sk.padre[b]);
      } else if (nombres[b].starts_with("HAIR")) {
        const auto rel=inv*w[b];
        tr.p[j]=rel.por(tr.p[j]); tr.n[j]=rel.gira(tr.n[j]);
        tr.hueso[j]=uint8_t(m.cabeza);
      }
    }
  }
}
void FijarPelo(const MallaActor& m,std::vector<ps2::M34>& mundo) {
  for (const auto& [b,local]:m.pelo) mundo[b]=mundo[m.cabeza]*local;
  if (m.faldon[0]<0 || m.faldon[4]<0 || m.faldon[5]<0) return;
  const auto& sk=*m.m.esq;
  const auto inv_w=mundo[m.faldon[0]].Inversa();
  const auto lk=(inv_w*mundo[m.faldon[4]]).por({}), rk=(inv_w*mundo[m.faldon[5]]).por({});
  const float wy=mundo[m.faldon[0]].m[7];
  const float ldy=wy-mundo[m.faldon[4]].m[7], rdy=wy-mundo[m.faldon[5]].m[7];
  auto poner=[&](int b,float reach,float dy) {
    if (b<0) return;
    const float a=std::clamp(std::atan2(dy,std::max(.5f,reach))-.2f,.15f,1.13f);
    const float c=std::cos(a), s=std::sin(a);
    const ps2::M34 rx{{1,0,0,0, 0,c,-s,0, 0,s,c,.75f}};
    const int p=sk.padre[b];
    mundo[b]=(p>=0 ? mundo[p] : ps2::M34{})*(sk.local[b]*rx);
  };
  poner(m.faldon[1],std::max(lk.z,rk.z),rk.z>lk.z ? rdy : ldy);
  poner(m.faldon[2],lk.x,ldy);
  poner(m.faldon[3],-rk.x,rdy);
}

struct Actor {
  std::shared_ptr<MallaActor> modelo;
  const Clip* clip = nullptr;
  double inicio = 0;
  bool dibujo = false, dibujo_b = false;
  const Partes* partes = nullptr;
  double inicio_partes = 0;
};
enum Tipo { Dibujo, DibujoB, Anim, Liberar, SubImagen, SubVisible, SubPos, Voz, Corte,
            Luz, Ambiente, Niebla, NieblaRGB, NieblaDensidad, NieblaDistancia, AnimPartes };
struct Evento { double t; Tipo tipo; int slot = 0, v = 0; const Clip* clip = nullptr; ImVec2 xy{}; Punto rgb{}; const Partes* partes = nullptr; };

struct Player {
  std::vector<Toma> tomas;
  std::array<Actor,26> actores;
  std::map<std::tuple<int,int,int>, Clip> clips;
  std::vector<Evento> eventos;
  std::vector<int16_t> pcm;
  std::array<std::array<LuzClip,14>,2> luces;
  std::array<int,2> luz_actual{-1,-1};
  std::array<double,2> inicio_luz{};
  std::map<std::tuple<int,int,int>,Partes> partes;
  ps2::Escena::Entorno entorno;
  ps2::Malla filtrada;
  size_t idx = 0, toma = 0;
  bool subtitular = false;
  int subtitulo = -1;
  ImVec2 pos_sub{};
  double fin = 0, salida = 0, fade = 0;
  raster::Superficie dibujo;
  ps2::Tex cuadro_3d;
  Imagen cuadro_cpu;
  double ultimo_cuadro=-1;
  std::vector<Imagen> subs;
  std::vector<ps2::Tex> subs_tex;
  ps2::Malla posa;
  std::vector<ps2::M34> mundo;
  std::chrono::steady_clock::time_point t0{};
  double reloj = 0;
  bool preparado = false;
#if REX_PLATFORM_WIN32
  HWAVEOUT dispositivo = nullptr;
  WAVEHDR cab{};
  bool en_pausa = false;
#endif

  ~Player() {
#if REX_PLATFORM_WIN32
    if (dispositivo) {
      waveOutReset(dispositivo);
      if (cab.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(dispositivo,&cab,sizeof(cab));
      waveOutClose(dispositivo);
    }
#endif
  }

  bool Preparar(float volumen) {
    subs_tex.reserve(subs.size());
    for (const auto& im:subs) {
      auto tx=ps2::Crear(im,false);
      if (!tx) return false;
      subs_tex.push_back(std::move(tx));
    }
    subs.clear();
    cuadro_cpu.w=raster::W; cuadro_cpu.h=raster::H;
#if REX_PLATFORM_WIN32
    WAVEFORMATEX fmt{};
    fmt.wFormatTag=WAVE_FORMAT_PCM; fmt.nChannels=1; fmt.wBitsPerSample=16;
    fmt.nSamplesPerSec=DWORD(kHz); fmt.nBlockAlign=2; fmt.nAvgBytesPerSec=2*DWORD(kHz);
    for (auto& s : pcm) s = int16_t(std::clamp(float(s)*volumen, -32768.f, 32767.f));
    if (waveOutOpen(&dispositivo,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL) == MMSYSERR_NOERROR) {
      cab.lpData=reinterpret_cast<LPSTR>(pcm.data()); cab.dwBufferLength=DWORD(pcm.size()*2);
      if (waveOutPrepareHeader(dispositivo,&cab,sizeof(cab)) != MMSYSERR_NOERROR ||
          waveOutWrite(dispositivo,&cab,sizeof(cab)) != MMSYSERR_NOERROR) return false;
    } else { dispositivo=nullptr; return false; }
#else
    (void)volumen;
    return false;
#endif
    t0=std::chrono::steady_clock::now();
    return preparado=true;
  }

  void Pausa(bool si) {
#if REX_PLATFORM_WIN32
    if (dispositivo && si != en_pausa) {
      if (si) waveOutPause(dispositivo);
      else { waveOutRestart(dispositivo); t0=std::chrono::steady_clock::now(); }
      en_pausa = si;
    }
#else
    (void)si;
#endif
  }

  double Reloj() {
#if REX_PLATFORM_WIN32
    // Falla abierto si un driver deja de avanzar sin devolver WHDR_DONE.
    if (!en_pausa && std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count() > salida/kRate+5)
      return salida;
    MMTIME pos{}; pos.wType=TIME_SAMPLES;
    if (cab.dwFlags & WHDR_DONE) return salida;
    if (waveOutGetPosition(dispositivo,&pos,sizeof(pos)) != MMSYSERR_NOERROR) return salida;
    if (pos.wType == TIME_SAMPLES)
      reloj = std::max(reloj,double(pos.u.sample)*kRate/kHz);
    else if (pos.wType == TIME_BYTES)
      reloj = std::max(reloj,double(pos.u.cb)*kRate/(kHz*2));
    else if (pos.wType == TIME_MS)
      reloj = std::max(reloj,double(pos.u.ms)*kRate/1000.);
    else return salida;  // dispositivo perdido: continuar al titulo
#endif
    return reloj;
  }

  void Avanzar(double u) {
    while (idx < eventos.size() && eventos[idx].t <= std::min(u,fin)) {
      const auto& e = eventos[idx++];
      auto& a = actores[e.slot];
      switch (e.tipo) {
        case Dibujo: a.dibujo = e.v != 0; break;
        case DibujoB: a.dibujo_b = e.v != 0; break;
        case Anim: a.clip = e.clip; a.inicio = e.t; break;
        case Liberar: a.dibujo = a.dibujo_b = false; a.clip = nullptr; a.partes = nullptr; break;
        case SubImagen: subtitulo = e.v; break;
        case SubVisible: subtitular = e.v != 0; break;
        case SubPos: pos_sub = e.xy; break;
        case Voz:
          REXLOG_INFO("dbz3 B1 1910: voz {} cuadro {:.2f} reloj {:.2f} termina {:.2f}",e.v,e.t,u,e.rgb[0]);
          break;
        case Corte:
          toma=size_t(e.v);
          REXLOG_INFO("dbz3 B1 1910: camara {} cuadro {:.2f} reloj {:.2f}",e.v,e.t,u);
          break;
        case Luz: luz_actual[e.slot]=e.v; inicio_luz[e.slot]=e.t; break;
        case Ambiente: entorno.ambiente=e.rgb; break;
        case Niebla: entorno.niebla=e.v!=0; break;
        case NieblaRGB: entorno.color=e.rgb; break;
        case NieblaDensidad: entorno.densidad={e.xy.x,e.xy.y}; break;
        case NieblaDistancia: entorno.distancia={e.xy.x,e.xy.y}; break;
        case AnimPartes: a.partes=e.partes; a.inicio_partes=e.t; break;
      }
    }
    for (size_t i=0;i<luces.size();++i) if (luz_actual[i]>=0) {
      const auto& l=luces[i][luz_actual[i]];
      const float f=float(std::clamp(u-inicio_luz[i],0.,double(l.cuadros-1)));
      entorno.dir[i]=Muestra(l.dir,f);
      entorno.luz[i]=Muestra(l.rgb,f);
    }
  }

  void PosarHD(const Actor& a,double u) {
    const auto& m=*a.modelo;
    const float f=a.clip ? float(std::clamp(u-a.inicio,0.,double(a.clip->cuadros-1))) : 0.f;
    const auto& sk = *m.m.esq;
    const auto* local = a.clip ? a.clip->hd.data()+size_t(f)*sk.padre.size() : sk.local.data();
    mundo.resize(sk.padre.size());
    for (size_t b=0;b<mundo.size();++b) {
      const int p = sk.padre[b];
      mundo[b] = p >= 0 ? mundo[p]*local[b] : local[b];
    }
    FijarPelo(m,mundo);
    posa = m.m.malla;
    const bool caida_pelo = m.cabeza >= 0 && m.faldon[0] >= 0;
    const ps2::V3 piv_l{0.f,-.7f,-.7f};
    const ps2::V3 piv_w = caida_pelo ? mundo[m.cabeza].por(piv_l) : ps2::V3{};
    for (auto& tr : posa.tris) for (int j=0;j<3;++j) {
      const auto& w = mundo[tr.hueso[j]];
      if (caida_pelo && tr.tex == 5 && tr.hueso[j] == m.cabeza && tr.p[j].y < piv_l.y) {
        const ps2::V3 d{tr.p[j].x-piv_l.x, tr.p[j].y-piv_l.y, tr.p[j].z-piv_l.z};
        const auto vh = w.gira(d);
        const float lift = std::max(0.f, vh.y - d.y*.88f);
        const float k = std::clamp(-d.y*.09f, 0.f, .75f);
        ps2::V3 v{vh.x, vh.y - lift*k, vh.z};
        const float l2 = d.x*d.x+d.y*d.y+d.z*d.z, lv2 = v.x*v.x+v.y*v.y+v.z*v.z;
        if (lv2 > 1e-6f) { const float s = std::sqrt(l2/lv2); v.x*=s; v.y*=s; v.z*=s; }
        tr.p[j] = {piv_w.x+v.x, piv_w.y+v.y, piv_w.z+v.z};
        tr.n[j] = w.gira(tr.n[j]);
      } else {
        tr.p[j] = w.por(tr.p[j]); tr.n[j] = w.gira(tr.n[j]);
      }
    }
  }

  bool Render3D(double u) {
    auto cam=Camara(tomas[toma],u,{320,224},{320,224});
    if (u<tomas.front().t || !cam.ok) return false;
    // Keep the authored #AMC eye/target/roll. A tighter B1 lens can cut the
    // larger HD face; derive the minimum wider lens from the posed face.
    if ((toma==8 || toma==11) && actores[10].dibujo) {
      const auto& a=actores[10];
      PosarHD(a,u);
      float tangente=std::tan(cam.fov*.5f);
      for (const auto& tr:posa.tris) {
        if (tr.tex==5) continue; // hair material, not the face
        for (int i=0;i<3;++i) if (tr.hueso[i]==a.modelo->cabeza || tr.hueso[i]==a.modelo->cara) {
          const auto& p=tr.p[i];
          const float d=raster::Prof(cam,p);
          if (d<=raster::Cerca) continue;
          const float y=cam.m[1]*p.x+cam.m[5]*p.y+cam.m[9]*p.z+cam.m[13];
          tangente=std::max(tangente,std::abs(y)/d);
        }
      }
      cam.fov=std::min(3.054326f,2.f*std::atan(tangente));
    }
      dibujo.Empezar();
      auto e = entorno;
      for (size_t slot=0;slot<actores.size();++slot) {
        const auto& a=actores[slot];
        if (!a.dibujo && !a.dibujo_b) continue;
        e.fondo=slot==4;  // SKY00; no dejar que el promedio de sus triangulos tape los actores
        e.sin_luz=e.fondo;
        const auto& m = *a.modelo;
        const float f = a.clip ? float(std::clamp(std::min(u,fin)-a.inicio,0.,double(a.clip->cuadros-1))) : 0.f;
        if (m.hd) {
          PosarHD(a,u);
          dibujo.Poner(cam,posa,m.m.tex,e,true);
        } else {
          const auto& sk = *m.m.esq;
          if (a.clip) ps2::Posar(sk,a.clip->ps,f/60.f,posa);
          const auto& malla=a.clip ? posa : sk.crudo;
          if (a.partes) {
            const float reloj_partes=float(std::clamp(u-a.inicio_partes,0.,double(a.partes->cuadros-1)));
            FiltrarPartes(*a.partes,reloj_partes,malla,filtrada);
            dibujo.Poner(cam,filtrada,m.m.tex,e);
          } else dibujo.Poner(cam,malla,m.m.tex,e);
        }
      }
    dibujo.Pintar(entorno);
    return true;
  }

  void Pintar(ImDrawList* fg, double u, ImVec2 pantalla) {
    Avanzar(u);
    const float s=std::min(pantalla.x/640.f,pantalla.y/448.f);
    const ImVec2 c(pantalla.x*.5f,pantalla.y*.5f), h(320*s,224*s);
    const ImVec2 p0(c.x-h.x,c.y-h.y), p1(c.x+h.x,c.y+h.y);
    fg->AddRectFilled({},pantalla,IM_COL32_BLACK);
    const double f=std::floor(std::min(u,fin));
    if (f!=ultimo_cuadro && Render3D(std::min(u,fin))) {
      cuadro_cpu.rgba.resize(raster::W*raster::H*4);
      std::memcpy(cuadro_cpu.rgba.data(),dibujo.rgba.data(),cuadro_cpu.rgba.size());
      if (auto tx=ps2::Crear(cuadro_cpu,false)) {
        cuadro_3d=std::move(tx);
        ultimo_cuadro=f;
      }
    }
    if (cuadro_3d) fg->AddImage(cuadro_3d.id(),p0,p1);
  }
};
}  // namespace

std::vector<Imagen> CargarSubtitulos(const std::filesystem::path& iso,int idioma) {
  for (const char* banco:{BancoPal(idioma),BancoPal(1)}) {
    AfsPs2 afs;
    if (!afs.Abrir(iso,banco)) continue;
    const auto amt=HijoAmb(afs.Entrada(1909),58);
    if (CuantasAmt(amt)<8) continue;
    std::vector<Imagen> imgs;
    for(int i=0;i<8;++i) imgs.push_back(TexturaAmt(amt,i));
    if (std::all_of(imgs.begin(),imgs.end(),[](const Imagen& im){return bool(im);})) return imgs;
  }
  return {};
}

std::shared_ptr<Player> Cargar(const std::filesystem::path& dir, const std::filesystem::path& iso,int idioma) {
#if !REX_PLATFORM_WIN32
  (void)dir; (void)iso; (void)idioma;
  return {};
#else
  try {
    const auto datos = Leer(dir,"escena_1910.toml",2 << 20);
    const auto doc = toml::parse(std::string_view(reinterpret_cast<const char*>(datos.data()),datos.size()));
    I(doc,"schema",1,1); I(doc,"spx",1910,1910); I(doc,"amb",1909,1909);
    I(doc,"units_per_second",60,60);
    const auto size = doc["iso_size"].value_exact<int64_t>();
    Exigir(size && *size > 0 && uint64_t(*size) == std::filesystem::file_size(iso), "copia PAL diferente de la cache");
    auto p = std::make_shared<Player>();
    p->subs=CargarSubtitulos(iso,idioma);
    Exigir(p->subs.size()==8,"subtitulos PAL");
    p->fin = D(doc,"duration_units",1,6000);
    AfsPs2 banco;
    Exigir(banco.Abrir(iso,"DATA_EN.AFS"),"banco de recursos AMB 1909");
    const auto amb=banco.Entrada(1909);
    const auto amc=HijoAmb(amb,55);
    Exigir(U32(amc,0)==0x434d4123 && U32(amc,16)==14,"#AMC 1909");
    p->luces[0]=LeerLuz(HijoAmb(amb,56));
    p->luces[1]=LeerLuz(HijoAmb(amb,57));
    for (const auto& n : Lista(doc,"camaras",14,14)) {
      const auto& t = Tabla(n);
      const int clip=I(t,"clip",int(p->tomas.size()),int(p->tomas.size()));
      Toma s{D(t,"t",0,p->fin),{}};
      Exigir(p->tomas.empty() || s.t > p->tomas.back().t,"orden de camaras");
      auto& paso = s.paso;
      paso.estilo_b1 = true;
      paso.cuadros = I(t,"frames",1,4096);
      const size_t fila=size_t(U32(amc,20))+16*clip;
      Exigir(U32(amc,fila)&0x10,"FOV #AMC");
      Exigir(U32(amc,fila+8)==uint32_t(paso.cuadros),"duracion #AMC");
      const size_t pistas=U32(amc,fila+12);
      paso.cam_ojo = Claves<4>(amc,U32(amc,pistas),paso.cuadros);
      paso.cam_obj = Claves<4>(amc,U32(amc,pistas+4),paso.cuadros);
      paso.cam_roll = Claves<2>(amc,U32(amc,pistas+8),paso.cuadros);
      paso.cam_fov = Claves<2>(amc,U32(amc,pistas+12),paso.cuadros);
      for (int f=0;f<paso.cuadros;++f) Exigir(Camara(s,s.t+f,{}, {320,224}).ok,"camara degenerada");
      p->tomas.push_back(std::move(s));
    }

    // Las 8 ranuras de personaje HD apuntan a 6 mallas compartidas. Las otras
    // llevan la geometria original (Roshi, escenario y atrezo).
    constexpr int hd[] = {264,352,264,246,360,352,327,110};
    std::map<int,std::shared_ptr<MallaActor>> modelos_hd;
    for (const auto& n : Lista(doc,"actores",25,25)) {
      const auto& t = Tabla(n);
      const int slot = I(t,"slot",0,25);
      auto& a = p->actores[slot];
      Exigir(!a.modelo && slot != 23,"ranura duplicada o desconocida");
      if (slot >= 6 && slot <= 13) {
        const int bin = I(t,"hd_bin",hd[slot-6],hd[slot-6]);
        const auto nombre = "sk_"+std::to_string(bin)+".sk";
        Exigir(t["hd_skin"].value_or(std::string()) == nombre,"modelo HD incorrecto");
        auto& m = modelos_hd[bin];
        if (!m) {
          m = std::make_shared<MallaActor>(); m->hd = true;
          std::vector<std::string> nombres;
          Exigir(nave::LeerFigura1910(dir/nombre,m->m,m->rampa,nombres),"HMS3 no legible");
          if (slot==10) {
            PrepararPelo(*m,nombres);   // GEM2: quitado el prestamo de la textura de pelo de Goku (264): Raditz queda gris, el original es negro
          }
        }
        a.modelo = m;
      } else {
        a.modelo = std::make_shared<MallaActor>();
        const auto amo = Leer(dir,t["model"].value_or(std::string()));
        const auto amt = Leer(dir,t["texture"].value_or(std::string()));
        auto& m = a.modelo->m;
        m=ps2::CargarSuelto(amo,amt,true,true,false,false,true);
        Exigir(m.esq && m.malla && m.esq->crudo && m.esq->padre.size() == m.esq->inv_mundo.size(),"AMO/AMT no legible");
        for (size_t b=0;b<m.esq->padre.size();++b)
          Exigir(m.esq->padre[b] >= -1 && m.esq->padre[b] < int(b),"jerarquia AMO");
        a.modelo->huesos_ps2=Nombres(amo,m.esq->padre.size(),U32(amo.data()+36));
        m.esq->centro = {};  // mantener el origen del guion, no recentrar como los modelos de combate
      }
    }

    for (const auto& n : Lista(doc,"retarget",26,26)) {
      const auto& t = Tabla(n);
      const int a = I(t,"actor",6,13), b = I(t,"bank",0,255), c = I(t,"source_clip",0,255);
      I(t,"hd_bin",hd[a-6],hd[a-6]);
      const int nf = I(t,"frames",1,4096), nb = I(t,"bones",1,256);
      Exigir(size_t(nb) == p->actores[a].modelo->m.esq->padre.size(),"esqueleto/clip HD");
      auto bytes = Leer(dir,t["matrices"].value_or(std::string()));
      Exigir(bytes.size() == 12+size_t(nf)*nb*48 && !std::memcmp(bytes.data(),"B1M1",4) &&
             U32(bytes.data()+4) == uint32_t(nf) && U32(bytes.data()+8) == uint32_t(nb),"cabecera B1M1");
      auto [it, nuevo] = p->clips.try_emplace({a,b,c});
      Exigir(nuevo,"clip HD duplicado");
      auto& clip = it->second;
      clip.cuadros=nf; clip.hd.resize(size_t(nf)*nb);
      size_t off = 12;
      for (auto& m : clip.hd) for (float& v : m.m) {
        const uint32_t bits=U32(bytes.data()+off); off+=4;
        std::memcpy(&v,&bits,4);
        Exigir(std::isfinite(v),"matriz HD no finita");
      }
    }

    std::map<std::pair<int,int>,std::string> bancos, bancos_amp;
    for (const auto& n : Lista(doc,"clips",64)) {
      const auto& t = Tabla(n);
      const auto formato=t["kind"].value_or(std::string());
      if (formato != "amm" && formato != "amp") continue;
      (formato=="amm" ? bancos : bancos_amp).emplace(std::make_pair(I(t,"actor",0,25),I(t,"bank",0,255)),t["file"].value_or(std::string()));
    }
    int lineas=0, fin_eventos=0, cortes=0;
    std::vector<Evento> voces;
    struct HiloSpx { double fin=0; int actor=-1; bool dib=false, dib_b=false, sub=false; };
    std::map<int,HiloSpx> hilos;
    auto rgb=[](const toml::table& t) {
      Punto out{};
      const auto& a=Lista(t,"rgb",3,3);
      for (int i=0;i<3;++i) {
        const auto v=a[i].value<double>();
        Exigir(v && *v>=0. && *v<=1.,"color SPX"); out[i]=float(*v);
      }
      return out;
    };
    for (const auto& n : Lista(doc,"eventos",1024)) {
      const auto& t = Tabla(n);
      const auto tipo = t["tipo"].value_or(std::string());
      Evento ev{D(t,"t",0,p->fin),Dibujo};
      auto& h = hilos[int(t["hilo"].value_or(-1))];
      h.fin = std::max(h.fin, ev.t);
      if (tipo == "espera") h.fin = std::max(h.fin, ev.t + double(t["unidades"].value_or(0.0)));
      else if (tipo == "texto_fundido") h.fin = std::max(h.fin, ev.t + double(t["cuadros"].value_or(0.0)));
      if (tipo == "actor_dibujo" || tipo == "actor_dibujo_b" || tipo == "liberar_actor" || tipo == "animacion") {
        ev.slot=I(t,"actor",0,25);
        const auto& a=p->actores[ev.slot];
        Exigir(bool(a.modelo),"actor sin modelo");
        if (tipo == "liberar_actor") ev.tipo=Liberar;
        else if (tipo == "animacion") {
          ev.tipo=Anim;
          const int b=I(t,"banco",0,255), c=I(t,"clip",0,255), nf=I(t,"largo",1,4096);
          I(t,"vueltas",1,1);
          const auto k=std::make_tuple(ev.slot,b,c);
          auto it=p->clips.find(k);
          if (it == p->clips.end() && !a.modelo->hd) {
            Clip clip;
            const auto d=Leer(dir,bancos.at({ev.slot,b}));
            clip.ps=LeerClipPS(d,c,a.modelo->huesos_ps2);
            clip.cuadros=clip.ps.cuadros;
            it=p->clips.emplace(k,std::move(clip)).first;
          }
          Exigir(it != p->clips.end() && it->second.cuadros == nf,"duracion de clip");
          ev.clip=&it->second;
        } else {
          ev.tipo=tipo == "actor_dibujo" ? Dibujo : DibujoB;
          ev.v=I(t,"activo",0,1);
        }
      } else if (tipo == "texto_imagen" || tipo == "texto_visible" || tipo == "texto_posicion") {
        if (I(t,"sprite",0,1) != 0) continue;
        if (tipo == "texto_imagen") {
          ev.tipo=SubImagen; ev.v=I(t,"imagen",lineas,lineas); ++lineas;
        } else if (tipo == "texto_visible") {
          ev.tipo=SubVisible; ev.v=I(t,"activo",0,1);
        } else {
          ev.tipo=SubPos; ev.xy={float(D(t,"x",0,640)),float(D(t,"y",0,448))};
        }
      } else if (tipo == "camara") {
        ev.tipo=Corte; ev.v=I(t,"clip",cortes,cortes);
        Exigir(cortes<int(p->tomas.size()) && p->tomas[cortes].t==ev.t &&
               p->tomas[cortes].paso.cuadros==I(t,"largo",1,4096),"corte SPX/#AMC");
        ++cortes;
      } else if (tipo == "voz") {
        ev.tipo=Voz; ev.v=I(t,"adx_jp",699,705); ev.slot=I(t,"canal",1,2);
        ev.rgb[0]=float(D(t,"segundos",0,30));
        voces.push_back(ev);
        continue; // insertar usando el indice de muestra real, una vez leido el ADX
      } else if (tipo == "luz") {
        ev.tipo=Luz; ev.slot=I(t,"canal",0,1); ev.v=I(t,"clip",0,13);
        I(t,"vueltas",1,1);
      } else if (tipo == "color_global" || tipo == "niebla_color") {
        ev.tipo=tipo == "color_global" ? Ambiente : NieblaRGB; ev.rgb=rgb(t);
      } else if (tipo == "niebla") {
        ev.tipo=Niebla; ev.v=I(t,"activa",0,1);
      } else if (tipo == "niebla_distancia") {
        ev.tipo=NieblaDistancia; ev.xy={float(D(t,"cerca",0,65535)),float(D(t,"lejos",0,65535))};
        Exigir(ev.xy.x<ev.xy.y,"distancias de niebla");
      } else if (tipo == "niebla_densidad") {
        ev.tipo=NieblaDensidad; ev.xy={float(D(t,"a",0,255)),float(D(t,"b",0,255))};
      } else if (tipo == "animacion_amp") {
        ev.tipo=AnimPartes; ev.slot=I(t,"actor",19,21);
        const int b=I(t,"banco",0,0), c=I(t,"clip",0,0), nf=I(t,"largo",1,4096);
        I(t,"vueltas",1,1);
        auto [it,nuevo]=p->partes.try_emplace({ev.slot,b,c});
        if (nuevo) it->second=LeerPartes(Leer(dir,bancos_amp.at({ev.slot,b})),c,nf,p->actores[ev.slot].modelo->huesos_ps2);
        ev.partes=&it->second;
      } else if (tipo == "fin_escena") {
        Exigir(std::abs(ev.t-p->fin) < 0.01,"fin de escena");
        p->fade=I(t,"fundido",1,120); ++fin_eventos;
        continue;
      } else continue;
      if (ev.tipo == Anim || ev.tipo == AnimPartes) {
        const int v = int(t["vueltas"].value_or(1));
        const int l = int(t["largo"].value_or(0));
        h.fin = v < 0 ? p->fin : std::max(h.fin, ev.t + double(l) * v);
      } else if (ev.tipo == Dibujo) { h.actor = ev.slot; h.dib = ev.v != 0; }
      else if (ev.tipo == DibujoB) { h.actor = ev.slot; h.dib_b = ev.v != 0; }
      else if (ev.tipo == Liberar) { h.dib = h.dib_b = false; }
      else if (ev.tipo == SubVisible) { h.sub = ev.v != 0; }
      p->eventos.push_back(ev);
    }
    for (auto& [id, h] : hilos) {
      if (id < 0) continue;
      for (const auto& s : p->tomas) if (std::abs(h.fin - s.t) <= 1.0) h.fin = s.t;
      if (h.fin >= p->fin) continue;
      if (h.dib && h.actor >= 0) p->eventos.push_back({h.fin, Dibujo, h.actor, 0});
      if (h.dib_b && h.actor >= 0) p->eventos.push_back({h.fin, DibujoB, h.actor, 0});
      if (h.sub) p->eventos.push_back({h.fin, SubVisible, 0, 0});
    }
    Exigir(lineas==8 && fin_eventos==1 && p->clips.size()==40 && p->partes.size()==3 &&
           cortes==14 && voces.size()==7,"inventario incompleto");

    // Una sola pista mono: las pausas son muestras cero, no temporizadores.
    // La voz final continua durante el fundido. El dispositivo da el reloj
    // de TODAS las pistas, incluido el primer plano, aun con FPS irregulares.
    AfsPs2 afs;
    Exigir(afs.Abrir(iso,"ADX_JP.AFS"),"banco ADX_JP.AFS");
    size_t ultimo=0;
    int entrada=699;
    for (const auto& n : Lista(doc,"voces",7,7)) {
      const auto& t=Tabla(n);
      I(t,"entry",entrada,entrada); I(t,"sample_rate",int(kHz),int(kHz));
      const int count=I(t,"samples",1,int(kHz*30));
      const auto start=size_t(std::llround(D(t,"t",0,p->fin)*kHz/kRate));
      Exigir(start >= ultimo && std::abs(D(t,"seconds",0,30)*kHz-count) <= kHz/kRate,"reloj ADX");
      auto ev=voces.at(size_t(entrada-699));
      Exigir(ev.v==entrada && ev.slot==I(t,"channel",1,2) &&
             std::abs(ev.t-double(start)*kRate/kHz)<=0.5*kRate/kHz+1e-6 &&
             std::abs(ev.rgb[0]*kHz-count)<=kHz/kRate,"voz SPX/ADX");
      ev.t=double(start)*kRate/kHz;
      ev.rgb[0]=float(double(start+count)*kRate/kHz);
      p->eventos.push_back(ev);
      auto samples=LeerVoz1910(afs,entrada++,int(kHz),count);
      Exigir(samples.size() == size_t(count),"muestras ADX no coinciden");
      p->pcm.resize(start);
      p->pcm.insert(p->pcm.end(),samples.begin(),samples.end());
      ultimo=p->pcm.size();
    }
    p->pcm.resize(std::max(ultimo,size_t(std::ceil((p->fin+p->fade)*kHz/kRate))));
    p->salida=double(p->pcm.size())*kRate/kHz;
    std::stable_sort(p->eventos.begin(),p->eventos.end(),[](const auto& a,const auto& b){return a.t<b.t;});
    return p;
  } catch (const std::exception& e) {
    REXLOG_WARN("dbz3 historia B1: cache 1910 rechazada: {}",e.what());
    return {};
  }
#endif
}

inline void ExpandirTituloCap1Es(Imagen& im76, const Imagen& im78) {
  if (im76.w != 640 || im76.h != 448 || im78.w != 640 || im78.h != 448) return;
  constexpr int kY0 = 190, kY1 = 250, kH = kY1 - kY0, kW = 640;
  const auto orig76 = im76.rgba;
  std::vector<uint8_t> bg(kH * kW * 3), bad0(kH * kW, 0), bad(kH * kW, 0);
  for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
    const size_t o = (size_t(kY0 + y) * kW + x) * 4, b = (size_t(y) * kW + x) * 3;
    const uint8_t* p = im78.rgba[o + 1] < orig76[o + 1] ? &im78.rgba[o] : &orig76[o];
    bg[b] = p[0]; bg[b + 1] = p[1]; bg[b + 2] = p[2];
    if (int(p[0]) - int(p[1]) < 44 || p[1] > 122) bad0[y * kW + x] = 1;
  }
  for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
      const int ny = std::clamp(y + dy, 0, kH - 1), nx = std::clamp(x + dx, 0, kW - 1);
      if (bad0[ny * kW + nx]) bad[y * kW + x] = 1;
    }
  }
  std::vector<float> bg_v(bg.begin(), bg.end()), bg_h(bg.begin(), bg.end());
  for (int x = 0; x < kW; ++x) {
    for (int y = 0; y < kH; ++y) if (bad[y * kW + x]) {
      int y0 = y - 1, y1 = y + 1;
      while (y0 >= 0 && bad[y0 * kW + x]) --y0;
      while (y1 < kH && bad[y1 * kW + x]) ++y1;
      if (y0 < 0 && y1 >= kH) continue;
      if (y0 < 0) y0 = y1; else if (y1 >= kH) y1 = y0;
      const float t = y1 == y0 ? 0.f : float(y - y0) / float(y1 - y0);
      for (int c = 0; c < 3; ++c)
        bg_v[(y * kW + x) * 3 + c] = std::lerp(float(bg[(y0 * kW + x) * 3 + c]), float(bg[(y1 * kW + x) * 3 + c]), t);
    }
  }
  for (int y = 0; y < kH; ++y) {
    for (int x = 0; x < kW; ++x) if (bad[y * kW + x]) {
      int x0 = x - 1, x1 = x + 1;
      while (x0 >= 0 && bad[y * kW + x0]) --x0;
      while (x1 < kW && bad[y * kW + x1]) ++x1;
      if (x0 < 0 && x1 >= kW) continue;
      if (x0 < 0) x0 = x1; else if (x1 >= kW) x1 = x0;
      const float t = x1 == x0 ? 0.f : float(x - x0) / float(x1 - x0);
      for (int c = 0; c < 3; ++c)
        bg_h[(y * kW + x) * 3 + c] = std::lerp(float(bg[(y * kW + x0) * 3 + c]), float(bg[(y * kW + x1) * 3 + c]), t);
    }
  }
  for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
    const size_t o = (size_t(kY0 + y) * kW + x) * 4, b = (size_t(y) * kW + x) * 3;
    for (int c = 0; c < 3; ++c) {
      bg[b + c] = uint8_t(std::clamp(int(std::lround(.5f * (bg_v[b + c] + bg_h[b + c]))), 0, 255));
      im76.rgba[o + c] = bg[b + c];
    }
  }
  auto pegar = [&](const std::vector<uint8_t>& src, int sx0, int sx1, int dx0) {
    const int w = sx1 - sx0 + 1;
    for (int y = 0; y < kH; ++y) for (int i = 0; i < w; ++i) {
      const size_t so = (size_t(kY0 + y) * kW + sx0 + i) * 4, sb = (size_t(y) * kW + sx0 + i) * 3;
      const size_t doff = (size_t(kY0 + y) * kW + dx0 + i) * 4, db = (size_t(y) * kW + dx0 + i) * 3;
      const float ag = (float(src[so + 1]) - float(bg[sb + 1])) / std::max(238.f - float(bg[sb + 1]), 1.f);
      const float ab = (float(src[so + 2]) - float(bg[sb + 2])) / std::max(238.f - float(bg[sb + 2]), 1.f);
      float a = std::clamp(.5f * (ag + ab), 0.f, 1.f);
      if (a < .08f) continue;
      for (int c = 0; c < 3; ++c)
        im76.rgba[doff + c] = uint8_t(std::clamp(int(std::lround(float(bg[db + c]) * (1.f - a) + 238.f * a)), 0, 255));
    }
  };
  pegar(orig76, 70, 334, 17);
  pegar(im78.rgba, 142, 316, 17 + 265 + 14);
  pegar(orig76, 436, 574, 17 + 265 + 14 + 175 + 14);
}

inline void ExpandirContentsCap1Es(Imagen& im) {
  if (im.w != 272 || im.h != 224) return;
  const auto orig = im.rgba;
  constexpr int kW = 272, kStripW = 275, kDstW = 270;
  std::vector<uint8_t> strip(32 * kStripW * 4, 0);
  auto copiar = [&](int sy0, int sx0, int sx1, int dx0) {
    const int w = sx1 - sx0 + 1;
    for (int y = 0; y < 32; ++y)
      std::memcpy(&strip[(y * kStripW + dx0) * 4], &orig[((sy0 + y) * kW + sx0) * 4], size_t(w) * 4);
    return dx0 + w;
  };
  int x = copiar(0, 3, 17, 0);
  x = copiar(0, 21, 127, x) + 3;
  x = copiar(64, 20, 99, x) + 3;
  x = copiar(0, 172, 223, x);
  copiar(0, 226, 240, x);
  for (int y = 0; y < 32; ++y) {
    std::memset(&im.rgba[y * kW * 4], 0, size_t(kW) * 4);
    for (int dx = 0; dx < kDstW; ++dx) {
      const int sx = (dx * kStripW) / kDstW;
      std::memcpy(&im.rgba[(y * kW + 1 + dx) * 4], &strip[(y * kStripW + sx) * 4], 4);
    }
  }
}
}  // namespace dbz3::pasarela::cine1910

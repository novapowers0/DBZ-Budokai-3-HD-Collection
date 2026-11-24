// Servicios aislados para compilar los bloques literales insertados por nb2j.py.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#define REXLOG_INFO(...) ((void)0)
#define REXLOG_WARN(...) ((void)0)
#define REXCVAR_GET(k) k
bool dbz3_b2_dende_vida_pal = false;
constexpr bool dbz3_b2_fx_nativo = false;
constexpr int dbz3_b2_vineta_demora_cuadros = 0;
constexpr int kB2Personajes = 60, kJuego = 0, kSaibaman = 42;
constexpr float kAnda = 0.1f, kMira = 0.8f, kVelGiro = 1;
constexpr uint16_t kA = 1, kStart = 2;
struct ImVec2 { float x=0, y=0; };
using ImU32 = uint32_t;
struct ImDrawList { void AddRectFilled(ImVec2, ImVec2, ImU32) {} };
namespace ImGui { struct IO { ImVec2 DisplaySize; }; IO GetIO() {return {}; } }
#define IM_COL32(...) (0u)
namespace S { struct Area {}; }
struct Lienzo {};
Lienzo LienzoDe(const S::Area&) { return {}; }
namespace T { const char* T(const char* a, const char*) { return a; } }
namespace dbz3::fusion { int IdBuAbsorbido(int) { return -1; } }
namespace dbz3::escenarios { int PorMod(const char*) { return -1; } }
namespace dbz3::opcional { bool Desactivado(const char*) { return true; } }
bool SpxAvanzar(int,int) { std::abort(); }
constexpr int kMapaB3[]{99};
constexpr const char* kEscenarioB2[13]{};
// @TIPOS@
struct Casilla { float x=0, y=0; int condicion=0, escenario=-1; };
struct Datos {
  struct Sprite { float dur=2; int tex=1, py=32; struct R { uint32_t col; }; std::vector<R> recortes; };
  int fase=0;
  std::vector<Casilla> casillas=std::vector<Casilla>(64);
  std::vector<Ficha> piezas;
  std::vector<std::array<int,8>> t4{{}};
  std::vector<std::vector<std::array<int,6>>> guiones;
  std::vector<Sprite> hud_ama=std::vector<Sprite>(75), textos=std::vector<Sprite>(10);
} g_d;
std::vector<Ficha> g_f(3), g_snap_piezas;
std::vector<Hilo> g_hilos;
std::vector<std::array<int,2>> g_mensajes;
std::map<std::string,int> progreso;
float g_ahora=0, g_fundido_campo=0, g_marca_t=0, g_pend_t=0, g_rival_t=0, g_rival_espera=0;
float g_mensaje_t=-10, g_msg_cierre=-10, g_cont_t=0;
int g_turno=1, g_sel=1, g_suceso=-1, g_suceso_sel=-1, g_ult_mov=-1, g_pend_mov=-1, g_dest=-1;
int g_pend_j=-1, g_pend_r=-1, g_marca=-1, g_marca_icono=-1, g_rival_paso=1, g_cam_rival=0;
int g_rival_i=2, g_rival_lucha=-1, g_cont_j=-1, g_cont_r=-1;
int g_estado=kJuego, g_elegir=0, g_premio_id=0, g_tloc=0, marcador=0;
int g_com_rival_b2=0, g_ataque_rival_b2=0, g_combate_fallos=0, consulta=-1, tarjeta_cond=-1, candidato=-1;
bool g_msg_cerrando=false, g_espera_x=false, g_luchando=false, g_combate_forzado=false, g_t4_pend=false;
std::vector<int> dib_hud, dib_texto;
int LeerProgreso(const std::string& k, int respaldo) { return progreso.contains(k) ? progreso.at(k) : respaldo; }
float GiroDeOrden(const Ficha&, float) { return 0; }
float Dif(float a, float b) { return std::remainder(a-b, 6.2831853f); }
ImVec2 PosFicha(const Ficha& f) { const auto& c=g_d.casillas.at(f.casilla); return {c.x,c.y}; }
void MaquinaObservar(const char*) {}
void VolcarEstado(const char*) {}
void SonidoEventoB2(const char*, int) {}
void Aviso(const std::string&, int = -1) {}
bool HilosOcupados() { return !g_hilos.empty(); }
bool EscenaAbierta() {return false; }
std::string Nombre(const Ficha&) {return "ficha"; }
int B2aB3(int c) { return c; } // identidad de prueba: solo interesa el ordinal de ficha, no el remapeo
int FormaTableroB3Impl(int, int) {return 0; }
int TrajeAbsorcionB2(int) {return -1; }
int TrajeMajinB2(int,int) {return -1; }
int TrajeSaibamanB2(int) {return -1; }
void RankContar(const Ficha&, const Ficha&) {}
int PasoRival(const Ficha&) {return candidato; }
bool CasillaLlena(const Ficha&, int) {return false; }
void SucesosDeCasilla(const Ficha&) { std::abort(); }
template<class C> bool Condiciones(const C&, int) { std::abort(); }
void Guion(int,int) { std::abort(); } // T4 debe quedar vacío
template<class A> bool DibTarjeta(ImDrawList*,const A&,float,float,int cond,const Ficha&) { tarjeta_cond=cond; return true; }
bool BuscarCombateB2(int,int casilla,int,CombateB2&) { consulta=casilla; return false; }
void AplicarIdentidadTabla(const CombateB2&, int*) { std::abort(); }
void DibHudSpB(ImDrawList*,int id,const Lienzo&,float) { dib_hud.push_back(id); }
void DibSp(ImDrawList*,const std::vector<Datos::Sprite>&,int,int id,const Lienzo&,float) { dib_texto.push_back(id); }
ImU32 TintB(ImU32,const Datos::Sprite&,float) {return 0; }
void PulB2Log(int,const char*) {}
// @LOGICA@

int main(int argc, char** argv) {
  if (argc < 2) return 3;
  const std::string modo=argv[1];
  for (int i=0; i<3; ++i) { g_f[i].n=i; g_f[i].c=i; g_f[i].oculta=(i==0); }
  g_f[1].jugador=true; g_f[2].rival=true;
  if (modo == "dende" && argc == 8) {
    int barras=std::stoi(argv[2]), hp=std::stoi(argv[3]), cond=std::stoi(argv[4]),
        vida_max=std::stoi(argv[5]), override_hp=std::stoi(argv[6]), nuevo_hp=std::stoi(argv[7]);
    for (int sw=0; sw<2; ++sw) {
      dbz3_b2_dende_vida_pal=sw;
      for (int n=1;n<3;++n) {
        auto& f=g_f[n]; f.barras=barras; f.vida=hp; f.vida_b2=override_hp ? hp : -1;
        f.vida_max=vida_max;
      }
      for (int n=1;n<3;++n) {
        auto& f=g_f[n]; CurarDende(f);
        int out[14]{};
        ComponerVidaKi(g_f[1],g_f[2],cond,out);
        const int inicial=n==1 ? out[3] : -out[4];
        const int short_port=f.vida_b2;
        std::vector<std::pair<std::string,int>> campos;
        GuardarPiezasFase(campos);
        progreso.clear(); progreso.insert(campos.begin(), campos.end());
        progreso["tablero_b2_snap_version"]=1;
        assert(LeerPiezasFase());
        assert(g_snap_piezas.at(n).vida_b2==short_port && g_snap_piezas.at(n).vida==f.vida);
        const int banda_recargada=BandaDende(g_snap_piezas.at(n));
        progreso.erase("tablero_b2_snap_p" + std::to_string(n) + "_banda_dende");
        assert(LeerPiezasFase());
        auto recarga=g_snap_piezas.at(n);
        assert(BandaDende(recarga)==-1);
        f.vida_b2=nuevo_hp; // orden 54 posterior: PAL no recalcula su banda aquí
        ComponerVidaKi(g_f[1],g_f[2],cond,out);
        std::cout << sw << ' ' << n << ' ' << f.barras << ' ' << f.vida << ' ' << short_port << ' '
                  << banda_recargada << ' ' << inicial << ' ' << (n==1?out[3]:-out[4]) << ' ' << BandaDende(f) << '\n';
      }
    }
  } else if (modo=="combate" && argc==7) {
    const bool rival_mueve=std::stoi(argv[2])!=0;
    const int origen=std::stoi(argv[3]), destino=std::stoi(argv[4]);
    g_d.casillas.at(origen)={0,0,std::stoi(argv[6]),-1};
    g_d.casillas.at(destino)={200,0,std::stoi(argv[5]),-1};
    Ficha& mov=g_f[rival_mueve?2:1], &quieta=g_f[rival_mueve?1:2];
    mov.casilla=mov.desde=origen; quieta.casilla=quieta.desde=destino;
    g_f[1].vida_b2=g_f[2].vida_b2=2500;
    g_f[1].vida_max=g_f[2].vida_max=2000;
    g_f[1].ki_b2=g_f[2].ki_b2=300;
    g_ahora=1;
    if (rival_mueve) {
      candidato=destino;
      PasoRivalContacto();
      assert(g_pend_j<0 && g_rival_paso==2 && g_rival_lucha==1);
      g_ahora=g_rival_t+std::max(kAnda,g_rival_espera)+0.01f; PasoRivalContacto();
    } else {
      PasoJugadorContacto(1,destino);
      assert(g_cont_j==1 && g_pend_j<0);
      g_ahora=g_cont_t; LlegadaJugadorContacto();
    }
    const int ficha_j=g_pend_j, ficha_r=g_pend_r;
    int out[14]{};
    assert(Despacho(g_ahora,0,out)==0 && consulta==-1);
    assert(Despacho(g_ahora+1,0,out)==0);
    const int rc=Despacho(g_ahora+3,kA,out);
    std::cout << ficha_j << ' ' << ficha_r << ' ' << consulta << ' ' << out[3] << ' ' << -out[4] << ' '
              << out[12] << ' ' << out[13] << ' ' << rc << ' ' << mov.casilla << ' ' << quieta.casilla << ' '
              << tarjeta_cond << ' ' << out[5] << ' ' << g_suceso;
  } else if (modo=="dialogo" && (argc==3 || argc==21)) {
    const int variante=std::stoi(argv[2]);
    g_d.guiones={{{1,38,5,9,0,0},{2,35,1,10,0,0}}};
    if (variante==3) {
      assert(argc==21);
      g_d.guiones[0].clear();
      g_d.textos.resize(256);
      for (int i=3;i<argc;i+=6) {
        std::array<int,6> row;
        for (int j=0;j<6;++j) row[j]=std::stoi(argv[i+j]);
        g_d.guiones[0].push_back(row);
      }
    } else if (variante!=0) {
      g_d.guiones[0].insert(g_d.guiones[0].begin(), {1,38,1,2,0,0});
      if (variante==2) g_d.guiones[0][1][0]=2;
      g_d.guiones[0][2][0]=3;
    }
    g_hilos.push_back({0});
    const auto tick = [&](float t, uint16_t pad) {
      g_ahora=t;
      dib_hud.clear(); dib_texto.clear();
      CorrerHilos();
      PanelPort(t,pad);
      std::cout << (dib_hud.empty()?0:dib_hud.front()) << ' ' << g_mensajes.size() << ' ' << g_msg_cerrando << ' '
                << marcador << ' ' << (g_hilos.empty()?-1:int(g_hilos.front().i)) << ' '
                << (dib_texto.empty()?-1:dib_texto[0]) << ' ' << (dib_texto.empty()?-1:dib_texto[1]) << '\n';
    };
    tick(1,0); tick(1.017f,0); tick(1.05f,0); tick(1.1f,0); tick(1.2f,kA);
    tick(1.22f,0); tick(1.25f,0); tick(1.5f,0); tick(1.6f,0);
  } else return 3;
}

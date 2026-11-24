"""Compila y ejecuta fragmentos literales del port, con servicios aislados."""
import hashlib
import re
import subprocess
import tempfile
from pathlib import Path

SRC = Path('src/pasarela_b2.cpp')
FLAG = 'dbz3_b2_popo_origen_pal'
HUECO = 'dbz3_b2_popo_hueco_pal'
DENDE_PAL = 'dbz3_b2_dende_vida_pal'


def run(cmd, cwd):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=90)
    if p.returncode != 0:
        raise RuntimeError(f'{cmd[0]}: código {p.returncode}\n{p.stdout}{p.stderr}')
    return p.stdout


def fragment(text, signature):
    a = text.index('\n' + signature) + 1
    # Las funciones completas del fichero cierran en columna 0.
    end = text.index('\n}', a) + 2
    return text[a:end]


PRELUDE = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#define REXLOG_INFO(...) ((void)0)
#define REXCVAR_GET(name) (name)
struct Ficha { int vida=0, vida_b2=-1, banda_dende=-1, barras=4, casilla=0, desde=0, slot=0,
    slot_ant=0, anim_b2=0, anim_estado=1, vida_max=3000, ki_b2=-1, com_b2=-1, ataque=100;
    bool fuera=false, oculta=false, rival=false; };
struct Alfombra { int i=-1, dest=0; float t_mueve=0, t_fin=0; bool dende=false; };
struct CombateB2 { int condicion=-1, vida_j=0, vida_r=0, ki_j=-1, ki_r=-1, com=-1, ataque_r=-1; };
Alfombra g_alf;
std::vector<Ficha> g_f(1);
float g_ahora=0;
int g_com_rival_b2=0, g_ataque_rival_b2=0, sucesos=0, fin=0;
void RecalcularSlots() { g_f[0].slot = g_f[0].slot_ant = 0; } // una ficha en el banco
void SucesosDeCasilla(const Ficha&) { ++sucesos; }
void FinDeAccion() { ++fin; }
constexpr int kFases = 9;
enum class CierreEscena { Inicial, kEscena9=9, kEscena10Dato0=10, kEscena11=11 };
enum { kJuego, kGanada, kPerdida };
struct Texto { int tex=1, py=32; };
struct { int fase=8; std::vector<Texto> textos = std::vector<Texto>(10);
    std::vector<int> casillas = std::vector<int>(256); } g_d;
std::vector<std::array<int,2>> g_mensajes;
float g_mensaje_t=0, g_msg_cierre=0;
bool g_msg_cerrando=false;
int g_estado=kJuego, g_fase_sig=-1, g_cierre_paso=-1, progreso=-1, peticiones_save=0;
bool g_espera_x=true, borra=false, snapshot_borrado=false;
CierreEscena g_cierre_esc = CierreEscena::Inicial;
void BorrarEstadoFase() { snapshot_borrado = true; }
void GuardarProgreso(const char*, int f) { progreso = f; }
void GuardarBanderas(bool p) { ++peticiones_save; borra = p; }
void VolcarEstado(const char*) {}
int stock, maximo;
struct Premio { int id; std::string nombre; };
int Tienes(int) { return stock; }
int Maximo(int) { return maximo; }
void Dar(const Premio&, int cantidad) { stock += cantidad; }
'''

MAIN = r'''
int main(int argc, char** argv) {
  if (argc < 5) return 5;
  const std::string tipo = argv[1];
  const int a = std::stoi(argv[2]), b = std::stoi(argv[3]), offset = std::stoi(argv[4]);
  if (tipo == "popo") {
    Ficha& f = g_f[0]; f.casilla = a; f.desde = a-1;
    g_alf = Alfombra{0, b, 2, 3, false};
    g_ahora = 1; ActualizarAlfombra();
    if (f.casilla != a || sucesos || fin) return 6;
    g_ahora = 2; ActualizarAlfombra();
    if (f.casilla != b || sucesos || fin) return 7;
    const int desde_aterrizaje = f.desde;
    g_ahora = 3; ActualizarAlfombra(); ActualizarAlfombra();
    if (sucesos != 1 || fin != 1 || f.desde != desde_aterrizaje) return 8;
    std::cout << f.casilla << ' ' << f.desde << ' ' << f.slot << ' ' << f.slot_ant;
  } else if (tipo == "dende") {
    Ficha f; f.barras = a; f.vida = f.vida_b2 = 2501;
    CurarDende(f);
    std::cout << f.barras;
  } else if (tipo == "combate") {
    Ficha j, r; j.vida_b2=2501; r.vida_b2=b; j.ki_b2=321; r.ki_b2=654;
    r.vida_max=offset;
    int out[14] = {};
    ComponerVidaKi(j, r, a, out);
    if (out[4] >= 0) return 9; // esta sonda solo cubre HP inicial explícito
    std::cout << out[3] << ' ' << -out[4] << ' ' << out[12] << ' ' << out[13];
  } else if (tipo == "kid") {
    Ficha f; f.ataque = a;
    std::cout << f.ataque;
    for (int i=5; i<argc; ++i) {
      std::array<int, 6> o{0,35,17,std::stoi(argv[i]),0,0};
      ActualizarAtaque(f, o);
      std::cout << ' ' << f.ataque;
    }
  } else if (tipo == "premio") {
    stock=b; maximo=offset;
    // Llamada a la frontera de concesión, no se ejecuta la ventana real.
    if (a > 0) GanarCapsula(a);
    std::cout << stock;
  } else if (tipo == "cierre") {
    g_d.fase=offset;
    Cerrar({0, a, b, 0, 0, 0});
    if (!snapshot_borrado || g_cierre_paso != 0 || g_estado == kJuego || peticiones_save > 1) return 11;
    std::cout << (borra ? 255 : progreso >= 0 ? progreso : g_d.fase) << ' '
              << int(g_cierre_esc) << ' ' << peticiones_save;
  } else if (tipo == "dialogo") {
    Dialogo({0,38,a,b,0,0});
    for (const auto& m : g_mensajes) std::cout << m[0] << ' ' << m[1] << ' ';
  } else return 10;
}
'''


def collect(root, rows, ajuste, compiler, workdir=None):
    raw = (root / SRC).read_bytes()
    text = raw.decode('utf-8-sig')
    (default,) = re.findall(rf'REXCVAR_DEFINE_BOOL\(\s*{FLAG}\s*,\s*(true|false)\s*,', text)
    enabled = {'serie': default == 'true', 'on': True, 'off': False}[ajuste]
    tmp = Path(tempfile.mkdtemp(prefix=f'b2-nb2c-{ajuste}-', dir=workdir))
    # No se reescriben reglas en Python. El ejecutable consume el mismo contrato que A.
    src_nave = (root / 'src/pasarela_nave.cpp').read_bytes()
    aug = '\n'.join(fragment(text, s) for s in ('void CurarDende(Ficha& f) {',
                    'void ActualizarAlfombra() {', 'void ComponerVidaKi(const Ficha& j,'))
    if f'REXCVAR_GET({HUECO})' in aug:
        (hueco,) = re.findall(rf'REXCVAR_DEFINE_BOOL\(\s*{HUECO}\s*,\s*(true|false)\s*,', text)
        # NB2C solo conmuta el origen; el hueco usa su valor de serie. No hay ocupantes en este banco.
        aug = (f'constexpr bool {HUECO} = {hueco};\n'
               + fragment(text, 'int HuecoLibre(const Ficha& f, int casilla) {') + '\n' + aug)
    if f'REXCVAR_GET({DENDE_PAL})' in aug:
        (dende_pal,) = re.findall(rf'REXCVAR_DEFINE_BOOL\(\s*{DENDE_PAL}\s*,\s*(true|false)\s*,', text)
        # NB2C solo conmuta el origen; Dende PAL usa su valor de serie.
        aug = f'constexpr bool {DENDE_PAL} = {dende_pal};\n' + aug
    # Aislar bloques y sentencia literales dentro del despachador.
    for name, cond in [('Cerrar', '41 || o[1] == 42 || o[1] == 43'), ('Dialogo', '38')]:
        start = text.index(f'    if (o[1] == {cond}) {{')
        end = text.index('\n    }', start) + len('\n    }')
        block = text[start:end]
        aug += f'\nfloat {name}(const std::array<int,6>& o) {{\n float espera=0;\n' + block + '\n return -1;\n}'
    linea, = [line for line in text.splitlines() if 'if (o[1] == 35) f.ataque =' in line]
    aug += '\nvoid ActualizarAtaque(Ficha& f, const std::array<int,6>& o) {\n' + linea + '\n}'
    aug += '\n' + fragment(src_nave.decode('utf-8-sig'), 'void GanarCapsula(int id_b2) {')
    source = PRELUDE + f'\nconstexpr bool {FLAG} = {str(enabled).lower()};\n' + aug + MAIN
    (tmp / 'sonda.cpp').write_text(source, encoding='utf-8')
    run([compiler, '-std=c++20', 'sonda.cpp', '-o', 'sonda.exe'], tmp)
    out = {}
    for case, row in rows.items():
        family = case.split('/')[0]
        kw = dict(row['entrada'])
        if family == 'popo':
            args = kw['llegada'], kw['destino'], 0
        elif family == 'dende':
            args = kw['barras'], 0, 0
        elif family == 'combate' and 'condicion' in kw:
            # +0x1F inyectado = 2. C++ deriva este valor de vida_max/1000.
            args = kw['condicion'], kw['vida_r'], 2000
        elif family == 'kid':
            # Usar los operandos del contrato observado en T6, no el resultado A.
            t6 = row['traza']['t6']
            args = t6['ataque_inicial'], 0, 0, *t6['incrementos']
        elif family == 'premio':
            args = kw['elegido'], kw['stock'], kw['maximo']
        elif family == 'cierre':
            args = kw['order'], kw['following'], kw['current']
        elif family == 'dialogo' and kw['abierta'] is False:
            args = *kw['textos'], 0
        else:
            continue
        values = list(map(int, run([str(tmp / 'sonda.exe'), family, *map(str, args)], tmp).split()))
        if family == 'combate':
            value = dict(vida=values[:2], ki=values[2:])
        elif family == 'kid':
            value = dict(ataque=values)
        elif family == 'premio':
            value = dict(stock_final=values[0])
        elif family == 'cierre':
            fase, modo, peticiones = values
            value = dict(fase_byte=fase, modo=modo, guardado_solicitado=peticiones == 1)
        elif family == 'dialogo':
            value = dict(textos=[list(p) for p in zip(values[::2], values[1::2], strict=True)])
        else:
            keys = ('barras',) if family == 'dende' else ('casilla', 'desde', 'slot', 'slot_anterior')
            value = dict(zip(keys, values, strict=True))
        out[case] = dict(entrada=kw, valor=value)
    return dict(formato='b2-eventos-nb2c-v1', lado='port', metodo='fragmentos_cpp',
                 identidad=dict(src_sha256=hashlib.sha256(raw).hexdigest(),
                                nave_sha256=hashlib.sha256(src_nave).hexdigest(),
                                ajuste=ajuste, popo_origen_pal=enabled,
                               sonda=str(tmp / 'sonda.cpp')), casos=out)

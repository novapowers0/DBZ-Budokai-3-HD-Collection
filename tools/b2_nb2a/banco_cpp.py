"""Compila rutas extraídas del C++ del port; observa entradas a la capa de dibujo.

El C++ conserva su selector, cvar y respaldos. Se simulan la disponibilidad de
texturas, un lienzo identidad y los sumideros de ImGui. No se compara el GS.
"""
from collections import Counter
import hashlib
from pathlib import Path
import re
import subprocess


CPP = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <vector>
using std::size_t;
using ImU32 = unsigned;
#define IM_COL32_WHITE 0xffffffffu
#define IM_COL32(...) 0xffffffffu
bool dbz3_b2_capas_tablero_pal, dbz3_b2_presentacion_pal;
#define REXCVAR_GET(c) (c)
struct ImVec2 { float x=0, y=0; ImVec2(float x=0,float y=0):x(x),y(y){} };
struct ImVec4 { ImVec4(float,float,float,float){} };
struct Lienzo { ImVec2 o; float s; };
namespace rex::ui { constexpr int kImDrawBlendAdditive=1, kImDrawBlendAlpha=0; }
struct ImDrawList { int mezcla=0; void AddCallback(int v, void*) { mezcla=v; } };
struct Corte { float cuadro; int tex; unsigned gs; };
struct Sprite { float dur; std::vector<Corte> recortes; };
struct Datos { std::vector<Sprite> life_ama; } g_d;
std::vector<bool> g_ticonos;
struct Casilla { int tipo, condicion; bool lucha; };
int BolaEn(int casilla) { return casilla==1 ? 0 : -1; }
std::vector<std::array<int,2>> g_bolas_fase(1);
float g_ahora;
ImU32 TintB(ImU32 col,const Sprite&,float) { return col; }
void PulB2Log(int,const char*) {}
std::ostringstream salida;
bool DibNativa01(ImDrawList* dl, const std::vector<Sprite>& v, const std::vector<bool>& tx,
                 int anim,const Lienzo&, float reloj, ImU32=IM_COL32_WHITE, float=0, int img=-1) {
  if (img<0) img=anim;
  const auto& rr=v.at(img).recortes;
  if (rr.empty()) return false;
  auto* r=&rr.front();
  for (const auto& j:rr) { if (j.cuadro>reloj) break; r=&j; }
  if (r->tex<0 || size_t(r->tex)>=tx.size() || !tx[r->tex]) return false;
  salida << " A:" << anim << ':' << img << ':' << r->tex << ':' << dl->mezcla;
  return true;
}
void DibIcono(ImDrawList*, int i, ImVec2,ImVec2,ImVec4,ImU32=IM_COL32_WHITE) { salida << " T:" << i; }
'''


def muestra(cpp, a, b):
    start = cpp.index(a)
    end = cpp.index(b, start+len(a))
    return cpp[start:end]


def programa(cpp, data):
    fim = '  // objetos a la vista y piezas, de atras adelante'
    cs = muestra(cpp, 'if (c.tipo == 2) {   // solP4:', fim)
    # Incluye la puerta real del nuevo camino, en la misma iteración que el respaldo.
    if 'if (DibCasillaBasePal(dl' in cpp:
        tail = cpp[cpp.index('const bool buscada = !prepara'):]
        selector = re.search(r'if \((?:REXCVAR_GET\(dbz3_b2_presentacion_pal\) \|\| )?'
                             r'REXCVAR_GET\(dbz3_b2_capas_tablero_pal\)\)', tail[:tail.index(fim)])
        if selector is None:
            raise ValueError('No se encuentra el selector de capas')
        cs = muestra(tail[selector.start():], selector.group(), fim)
    cs = cs[:cs.rindex('}')]  # cierre del bucle externo original, sustituido por el nuestro
    bs = muestra(cpp, 'if (o.tipo == 0) {\n        const int b = BolaEn', '      // #AMA 397, registro 0x43')
    funciones = muestra(cpp, 'bool DibCasilla01(', '// Orden real del panel')
    datos = []
    for row in data['registros_ama397']:
        cortes = ','.join('{%s.0f,%d,%du}' % (r['cuadro'], (r['tex']+2**31)%2**32-2**31, r['gs']) for r in row['claves'])
        datos.append('{%sf,{%s}}' % (repr(float(row['dur'])), cortes))
    return CPP + funciones + '\n' + '''
int main() {
  g_d.life_ama={''' + ',\n'.join(datos) + '''};
  int kind, k, cond, falta, enable;
  float reloj;
  while (std::cin >> kind >> k >> cond >> enable >> falta >> reloj) {
    g_ticonos={''' + ','.join('true' if x else 'false' for x in data['recursos']['DATA_CMN.AFS/398']['texturas']) + '''};
    if (falta>=0) g_ticonos.at(falta)=false;
    dbz3_b2_capas_tablero_pal=enable==1;
    dbz3_b2_presentacion_pal=enable==2;
    ImDrawList ctx, *dl=&ctx;
    ImVec2 p;
    float e=1, t=reloj/60.0f;
    ImU32 col=IM_COL32_WHITE;
    g_ahora=t;
    salida.str(""); salida.clear();
    if (kind==1) {
      const Casilla c{k,cond,cond!=0};
      for (int i=0;i<1;++i) {
''' + cs + '''
      }
    } else {
      const struct {int tipo,casilla;} o{0,1};
      g_bolas_fase[0][1]=k;
      for (int i=0;i<1;++i) {
''' + bs + '''
      }
    }
    std::cout << ctx.mezcla << salida.str() << '\\n';
  }
}
'''


def ejecutar(src, data, work, etiqueta, compilador):
    raw = src.read_bytes()
    cpp = raw.decode('utf-8-sig').replace('\r\n','\n')
    if hashlib.sha256(raw).hexdigest() != data['fuente_sha256']:
        raise ValueError('La fuente C++ cambió entre censo y sonda')
    req = {}
    for fase, f in data['fases'].items():
        for r in f['casillas']:
            req[(fase,'T1',r['casilla'],r['tipo'],r['condicion'])] = r['capas']
        for r in f['objetos']:
            if r['bola'] is not None:
                req[(fase,'T8',r['registro'],r['bola'],0)] = r['capas']
    controles = [(1,2,0,1,13,37.5), (1,3,0,1,18,37.5), (1,4,0,1,17,37.5), (2,0,0,1,2,37.5),
                 (1,2,0,1,-1,0.), (1,3,0,1,-1,42.), (2,1,0,1,-1,141.)]
    def clave(r, on):
        _, tipo, _, k, cond = r
        return (1 if tipo=='T1' else 2, k if tipo=='T1' else k-1, cond, on,-1,37.5)
    todos = list(dict.fromkeys([clave(k,on) for k in req for on in (0,1)] + controles))
    # Ejecutar también las mismas capas con solo el perfil conjunto activo.
    perfiles = {k: (*k[:3],2,*k[4:]) for k in todos if k[3] == 1} if 'REXCVAR_GET(dbz3_b2_presentacion_pal)' in cpp else {}
    todos.extend(perfiles.values())
    cpp_path = work / (etiqueta+'.cpp')
    exe = work / (etiqueta+'.exe')
    cpp_path.write_text(programa(cpp,data), encoding='utf-8')
    subprocess.run([str(compilador), '-std=c++23', str(cpp_path), '-o', str(exe)], check=True, timeout=90, cwd=work)
    medicion = subprocess.run([str(exe)], input=''.join(' '.join(map(str,k))+'\n' for k in todos),
                               text=True, capture_output=True, check=True, timeout=15, cwd=work).stdout.splitlines()
    if len(medicion) != len(todos):
        raise ValueError('El arnés C++ devolvió diferente número de casos')
    def decode(l):
        m,*entradas = l.split()
        if m != '0':
            raise ValueError('Estado de mezcla quedó activo tras dibujar')
        return [('TEX',int(v[1])) if v[0]=='T' else ('AMA',*map(int,v[1:]))
                for s in entradas for v in [s.split(':')]]
    resultados = {k:decode(l) for k,l in zip(todos,medicion)}
    if any(resultados[k] != resultados[v] for k,v in perfiles.items()):
        raise ValueError('El perfil conjunto y el interruptor de capas no dibujan lo mismo')
    return req, {r: [resultados[clave(r,v)] for v in (0,1)] for r in req}, [resultados[k] for k in controles]


def capa_original(q, registros):
    r = registros[q['anim']]
    i = registros[q['imagen']]
    c = int(q['reloj'] % max(1,r['dur']))
    if not i['claves']:
        raise ValueError('Capa sin recorte B')
    corte = next((k for k in reversed(i['claves']) if k['cuadro']<=c), i['claves'][0])
    return ('AMA',q['anim'],q['imagen'],corte['tex'])


def evaluar(req, trazas, metadata, comparar):
    filas = []
    for k, original in req.items():
        fase, tabla, i, tipo, cond = k
        ref = [capa_original(q, metadata) for q in original]
        off, on = trazas[k]
        fila = dict(fase=int(fase), tabla=tabla, indice=i, tipo_o_bola=tipo, condicion=cond, original=ref)
        for config, log in [('off',off),('on',on)]:
            fila[config] = dict(dibujo=log, **comparar(ref,[r[:4] for r in log]))
        filas.append(fila)
    return filas, {m: dict(Counter(r[m]['estado'] for r in filas)) for m in ('off','on')}

# SPDX-License-Identifier: MIT
"""Compila únicamente los fragmentos C++ medidos (la copia conserva los bytes)."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile

FUENTES = ('iw_mini_botones.cpp', 'iw_mini_extra.cpp', 'iw_minijuegos.cpp')
RANGO = 'dbz3_iw_7d5_rango_pal'
CAPTURA = 'dbz3_iw_7dd_captura_derrota'


def run(args, **kw):
    r = subprocess.run(args, encoding='utf-8', errors='replace', capture_output=True, timeout=90, **kw)
    if r.returncode:
        raise RuntimeError(f'{args[0]} rc={r.returncode}: {r.stderr.strip() or r.stdout.strip()}')
    return r.stdout


def trozo(texto, inicio, fin):
    if texto.count(inicio) != 1:
        raise ValueError(f'ancla C++ ausente o ambigua: {inicio}')
    i = texto.index(inicio)
    return texto[i:texto.index(fin, i + len(inicio))]


def defecto(texto, cvar):
    valores = re.findall(r'^REXCVAR_DEFINE_BOOL\(\s*' + re.escape(cvar) + r'\s*,\s*(true|false)\s*,', texto, re.M)
    if len(valores) != 1:
        raise ValueError(f'cvar C++ ausente o ambigua: {cvar}')
    return valores[0]


class Sonda:
    def __init__(self, directorio, fuentes, cxx):
        self.directorio, self.fuentes, self.cxx = directorio, fuentes, cxx
        self.exe = directorio / 'sonda.exe'
        self.sha = {n: hashlib.sha256((fuentes / n).read_bytes()).hexdigest() for n in FUENTES}

    def compilar(self):
        b, e, m = ((self.fuentes / n).read_text(encoding='utf-8') for n in FUENTES)
        self.defectos = {RANGO: defecto(b, RANGO), CAPTURA: defecto(m, CAPTURA)}
        pal = trozo(b, 'int RangoNB007Pal(', '\n}\n') + '\n}'
        cuerpo = trozo(b, 'void PasoTeclear(', '\n}\n')
        rango = trozo(cuerpo, '  float total = 0;', '  const float v =')
        constantes = [re.search(r'^constexpr float ' + n + r'.*?;', b, re.M).group()
                      for n in ('kUmbralT', 'kLimiteTeclear')]
        boton = trozo(b, 'int BotonPulsado(', '\n}\n') + '\n}'
        eje = trozo(e, 'float Eje(int v)', '\n')
        pad = trozo(e, 'void PadGetStickAxes(', '\n}\n') + '\n}'
        terminar = trozo(m, '  void Acabar(uint8_t* base, uint32_t mgr, const Datos& d, bool meta)', '\n  }\n')
        resultado = [re.search(r'^\s*' + re.escape(n) + r' = .*?;', terminar, re.M).group()
                     for n in ('g_final', 'res.superado')]
        captura = trozo(m, '    if (!Captura7DD(d) && !Original7D1(d) && DistH(yo, npc_p_) <', '\n    }\n') + '\n    }'
        fuente = '\n'.join(['''#include <iostream>
#include <cstdint>
#include <cmath>
#define REXCVAR_GET(name) (ajuste < 0 ? valor_de_serie_##name : ajuste != 0)
#define REXLOG_INFO(...) ((void)0)
struct JuegoB { float nota[10]{}; int total{}, rango{}; };
struct Datos { int npc; };
struct V3 { float x, y, z; };
static bool capt, bub;
int g_final;
struct Resultado { bool superado; };
bool Captura7DD(const Datos&) { return capt; }
bool Original7D1(const Datos&) { return bub; }
float DistH(V3 a, V3 b) { return std::hypot(a.x-b.x, a.z-b.z); }''',
            *(f'constexpr bool valor_de_serie_{n} = {v};' for n, v in self.defectos.items()), '''
void Acabar(uint8_t*, uint32_t, const Datos&, bool meta) {
  Resultado res{};''', *resultado, '''
  std::cout << g_final << ' ' << int(res.superado);
}
int contacto(int ajuste, int npc, V3 yo, V3 npc_p_) {
  Datos d{npc}; uint8_t* base=nullptr; uint32_t mgr=0; g_final=0;''', captura, '''
  if (!g_final) std::cout << "0 -1";
  return 0;
}''', *constantes, pal, boton, eje, pad, '''
int rangoNB(JuegoB& j, int ajuste) {''', rango, '''
  return j.rango;
}
int main() {
  char modo;
  while (std::cin >> modo) {
    int a,b,c,d; float x,y;
    if (modo=='q' && std::cin >> a >> b) std::cout << BotonPulsado(uint16_t(a & b));
    else if (modo=='v' && std::cin >> a >> b >> c) {
      PadGetStickAxes(a,b,c,x,y); std::cout << x << ' ' << y;
    } else if (modo=='p' && std::cin >> a >> b >> c >> d >> x >> y) {
      capt=c; bub=d; contacto(a,b,{0,0,0},{x,y,0});
    } else if (modo=='r' && std::cin >> a) {
      JuegoB j; for (auto &t:j.nota) if (!(std::cin >> t)) return 2;
      std::cout << rangoNB(j,a);
    } else return 2;
    std::cout << '\\n';
  }
}'''])
        path = self.directorio / 'sonda.cpp'
        path.write_text(fuente, encoding='utf-8')
        self.codigo_sha256 = hashlib.sha256(fuente.encode()).hexdigest()
        run([self.cxx, '-std=c++17', '-O2', '-ffp-contract=off', '-o', str(self.exe), str(path)])

    def medir(self, modo, entradas, tipos):
        datos = ''.join(modo + ' ' + ' '.join(map(str, r)) + '\n' for r in entradas)
        filas = run([str(self.exe)], input=datos, env={**os.environ, 'LC_ALL': 'C'}).splitlines()
        if len(filas) != len(entradas) or any(len(f.split()) != len(tipos) for f in filas):
            raise RuntimeError('sonda C++ devolvió un número incorrecto de filas o columnas')
        return [tuple(t(v) for t, v in zip(tipos, fila.split())) for fila in filas]


def abrir(raiz, cxx, directorios):
    directorio = Path(directorios.enter_context(tempfile.TemporaryDirectory(prefix='iw-niwb-')))
    copia = directorio / 'src'
    copia.mkdir()
    for n in FUENTES:
        (copia / n).write_bytes((raiz / 'src' / n).read_bytes())
    sonda = Sonda(directorio, copia, cxx)
    sonda.compilar()
    return sonda

"""Banco de rango 7D7: vectores de disparo_banco, respaldo C++ real."""
import contextlib
import io
import re
from types import SimpleNamespace


def rango_original(pal, usa):
    from disparo_banco import ELF_HASH, rangos, sha256
    for clave, path in [('eu', pal), ('us', usa)]:
        if sha256(path) != ELF_HASH[clave]:
            raise ValueError(f'ELF {clave} no corresponde al banco de rangos: {path}')
    with contextlib.redirect_stdout(io.StringIO()):
        r = rangos(SimpleNamespace(elf_eu=pal, elf_us=usa))
    resultados = [p for p in r['perfiles'] if p['clave'] == '7D7']
    if len(resultados) != 2 or any(p['iguales'] != p['total'] for p in resultados):
        raise RuntimeError('banco original sin dos perfiles conformes')
    return resultados


def comparar_rango(original, cpp, cxx, d, ajuste):
    from ._limites import BANCOS, compilar
    from iw_niwb._sonda_cpp import trozo, defecto

    acum = trozo(cpp, '    int idx = -1;   // Menu_ClassifyValueByThreshold / Effect_ClassifyRange', '\n    if (d.juego == 3)')
    cierre = trozo(cpp, '    if (d.juego == 15) {   // MgShoot_GetRankFromTime con p', '\n    if (d.juego == 9)   // PAQ_3: medir el cierre')
    tabla = re.search(r'^constexpr\b[^\n]*\bkUmbral\s*\[.*?;', cpp, re.M)
    if tabla is None:
        raise ValueError('sin declaración de kUmbral de rango')
    cvar_cont = 'dbz3_iw_disparo_continuo_original'
    serie = defecto(cpp, cvar_cont)
    cvar_niwd = BANCOS['rango-7d7'][2]
    nuevo = defecto(cpp, cvar_niwd)
    sonda = r'''#include <iostream>
namespace iwmips::disparo { bool (*g_rango)(unsigned, int, int, float, int&) = nullptr; }
struct Datos { int juego, dif; };
''' + tabla[0] + r'''
int main() {
  Datos d;
  int reloj, ajuste, aciertos_, vida_;
  float jugado;
  #define REXCVAR_GET(name) (name)
  while (std::cin >> ajuste >> d.juego >> d.dif >> aciertos_ >> vida_ >> reloj >> jugado) {
    const bool ''' + cvar_cont + ' = reloj < 0 ? ' + serie + r''' : reloj != 0;
    const bool ''' + cvar_niwd + ' = ajuste < 0 ? ' + nuevo + r''' : ajuste != 0;
    const float p15_ = jugado;
''' + acum + '\n' + cierre + r'''
    std::cout << idx << '\n';
  }
}'''
    entradas = []
    for perfil in original:
        for continuo in (-1, 0, 1):
            entradas.extend((8, v['dificultad'], v['aciertos'], 0, continuo, v['valor_ee']) for v in perfil['casos'])
    # 7D8 sigue condicionado por la opción y por la vida; 7DE conserva su umbral.
    controles = [(9,0,0,1,0,30), (9,0,0,1,1,30), (9,0,0,0,1,30), (15,0,0,1,1,.05)]
    filas = compilar(sonda, cxx, d, ''.join(' '.join(map(str, (ajuste, *f)))+'\n' for f in [*entradas, *controles]))
    if any(len(f) != 1 for f in filas):
        raise RuntimeError('salida de rango incompleta')
    idx = iter(f[0] for f in filas)
    resultados = []
    for perfil in original:
        for modo in ('serie', 'off', 'on'):
            casos = [dict(dificultad=v['dificultad'], aciertos=v['aciertos'], segundos=v['valor_ee'],
                          original=v['original'], cpp=next(idx)) for v in perfil['casos']]
            diffs = [f for f in casos if f['original'] != f['cpp']]
            resultados.append(dict(region=perfil['region'], reloj=modo, total=len(casos), iguales=len(casos)-len(diffs),
                                   primera_diferencia=diffs[0] if diffs else None))
    checks = [dict(entrada=f, port=next(idx), esperado=v) for f,v in zip(controles, (4,-1,4,3))]
    return dict(perfiles=resultados, controles=checks, coincide=all(f['port'] == f['esperado'] for f in checks)
                and all(f['iguales'] == f['total'] for f in resultados))

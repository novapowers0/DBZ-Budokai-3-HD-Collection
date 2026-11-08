"""Genera el .rsp y el comando de enlace con /MAP para perfilar el exe sin PDB.

  python tools\\cpu_perfil\\mkrsp.py <build.ninja> <salida.rsp> <enlace.cmd> <exe> <mapa>

IMPORTANTE: el enlace debe escribir en una COPIA PROPIA del juego (por ejemplo
D:\\tmp\\cpu\\game\\dbz3.exe), nunca en `out/build/.../dbz3.exe`, que es el arbol compartido.
Este script no enlaza: solo deja el .rsp y el .cmd listos para ejecutarlos a mano desde el
directorio del build (los objetos son rutas relativas). El mapa solo vale para el exe que
genere ese mismo enlace.
"""
import re
import sys


def unesc(s):
    return s.replace('$ ', ' ').replace('$:', ':').replace('$$', '$')


def main():
    if len(sys.argv) < 6:
        print(__doc__)
        return 2
    build_ninja, rsp_out, cmd_out, exe_out, map_out = sys.argv[1:6]
    lines = open(build_ninja, encoding='utf-8', errors='replace').read().splitlines()
    i = [k for k, l in enumerate(lines) if l.startswith('build dbz3.exe:')][0]
    head = lines[i][len('build dbz3.exe:'):].split(' | ')[0]
    objs = [unesc(x) for x in head.split() if x.endswith('.obj') or x.endswith('.res')]
    vars_ = {}
    for l in lines[i + 1:]:
        if not l.startswith('  '):
            break
        m = re.match(r'  ([A-Z_]+) = (.*)', l)
        if m:
            vars_[m.group(1)] = m.group(2)
    rsp = ' '.join('"%s"' % o for o in objs) + ' ' + vars_.get('LINK_LIBRARIES', '').replace('  ', ' ')
    open(rsp_out, 'w', encoding='utf-8').write(rsp)
    cmd = ('"C:/Program Files/LLVM/bin/clang++.exe" -nostartfiles -nostdlib %s %s "@%s" -o "%s" '
           '-Xlinker /MANIFEST:EMBED -Xlinker "/implib:%s.lib" -Xlinker /version:0.0 %s '
           '-Xlinker "/MAP:%s"' % (
               vars_.get('FLAGS', ''), vars_.get('LINK_FLAGS', ''), rsp_out, exe_out, exe_out,
               vars_.get('MANIFESTS', ''), map_out))
    open(cmd_out, 'w', encoding='utf-8').write('@echo off\r\n' + cmd + '\r\n')
    print('objetos: %d -> %s' % (len(objs), rsp_out))
    print('recuerda: el -o debe ser una copia propia, no el exe canonico del build')
    return 0


if __name__ == '__main__':
    sys.exit(main())

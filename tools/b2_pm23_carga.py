"""PM-23: cotejo offline de la carga de las nueve fases, con el ELF PAL y C++ del port."""
import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import b2_juez
from b2_oraculo import original as orig

TOOLS = Path(__file__).resolve().parent
ESTADOS = Path('E:/DBZ3HD_trabajo/A2/sstates')


def tramo(src, inicio, fin):
    if src.count(inicio) != 1:
        raise ValueError(f'ancla no única: {inicio}')
    a = src.index(inicio)
    return src[a:src.index(fin, a + len(inicio))]


def arnes(src):
    """Extrae código real; si cambia la estructura preferimos error antes que emularla."""
    constantes = tramo(src, 'constexpr int kGoku = ', ';') + ';'
    ficha = tramo(src, 'struct Ficha {', '\n};') + '\n};'
    recalc = tramo(src, 'void RecalcularSlots() {', '\n}') + '\n}'
    activa = tramo(src, 'bool Activa(const Ficha& f) {', '\n')
    t2 = tramo(src, '  for (uint32_t p = 1; p < n2; ++p) {', '\n  }\n') + '\n  }'
    empezar = tramo(src, 'void Empezar() {', '\n}')
    inicio = tramo(empezar, '  g_f = g_d.piezas;', '  g_sel = 0;')
    (inicio, n) = re.subn(r'^  g_buscada\.assign\(.+?\);\n', '', inicio, flags=re.M)
    if n != 1:
        raise ValueError('cambió el contrato del arnés: g_buscada')
    serial = tramo(src, '  j += ",\\\"piezas\\\":[";', '  j += "],\\\"objetos\\\":[";')
    return '''#define _CRT_SECURE_NO_WARNINGS
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#define REXLOG_WARN(...)
#define REXCVAR_GET(name) name
''' + '\n'.join((constantes, ficha, 'std::vector<Ficha> g_f;', recalc, activa)) + '''
int main(int argc, char** argv) {
  if (argc != 3 || (std::string(argv[2]) != "0" && std::string(argv[2]) != "1")) return 2;
  const bool dbz3_b2_pm23_slot_inicial_pal = argv[2][0] == '1';
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> y((std::istreambuf_iterator<char>(input)), {});
  const auto u16 = [&](size_t o) -> int {return y.at(o) | (int(y.at(o+1)) << 8);};
  const uint32_t n1 = u16(0), n2 = u16(2), o2 = 4;
  if (y.size() != o2 + 40*n2) return 2;
  struct { std::vector<Ficha> piezas; int objetos = 0; } d;
  auto& g_d = d;
  int g_obj = 0, g_jefe = -1;
''' + t2 + '\n' + inicio + '''
  const auto N = [](int v) { return std::to_string(v); };
  const auto B = [](bool v) { return v ? "true" : "false"; };
  std::string j;
''' + serial + '''
  std::cout << '{' << j.substr(1) << "]}\\n";
}
'''


def pal(base):
    cpu = base.cpu({0x2791A0})
    cpu.hooks[0x123C18] = orig.memset
    board = cpu.r32(0x3A45A8)
    n1, n2, o2 = (cpu.r32(board + x) for x in (0x20, 0x28, 0x2C))
    if not (1 < n1 <= 512 and 1 < n2 <= 32 and 0 < o2 <= len(cpu.m) - n2 * 40):
        raise ValueError('tabla T2 fuera del contrato PAL conocido')
    t2 = struct.pack('<HH', n1, n2) + bytes(cpu.m[o2:o2 + n2 * 40])
    orig.invoke(cpu, 0x2791A0)
    piezas = []
    for i in range(1, n2):
        row, rec = o2 + 40*i, 0x69B7A0 + 0x24*i
        cas = cpu.r16(rec + 8)
        if not 0 < cas < n1:
            continue
        flags = cpu.r16(rec + 6) & 15
        piezas.append(dict(n=i, c=cpu.r16(rec+4), bando=cpu.r8(row+2), barras=cpu.r8(rec+1),
                           barras_max=cpu.r8(row+0xB), casilla=cas, slot=cpu.r8(rec+0xC),
                           fuera=flags == 0, en_juego=flags == 2, estado0=0))
    return t2, piezas, orig.trace(cpu)


def doc(fase, piezas, fuente):
    return dict(formato='b2-state-v1', carril='original' if fuente == 'elf_pal' else 'nativo',
                fuente=fuente, entrada_id=f'S{fase}', evento=1, fase=fase, hito='carga', piezas=piezas,
                turno=None, fase_actual=fase-1, fase_rival=None, script={'codigo': None, 'guion_siguiente': None})


def guardar(path, data):
    with path.open('x', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(data, ensure_ascii=True) + '\n')


def medir(a):
    out = a.salida.resolve()
    out.mkdir(parents=True)
    for p in ('A_pal', 'B_arnes', 'juez'):
        (out / p).mkdir()
    fuente = a.src.resolve()
    src = fuente.read_text(encoding='utf-8')
    if a.slot_pal and 'REXCVAR_GET(dbz3_b2_pm23_slot_inicial_pal)' not in src:
        raise ValueError('--slot-pal requiere una fuente que use dbz3_b2_pm23_slot_inicial_pal')
    cpp = out / 'sonda_carga.cpp'
    cpp.write_text(arnes(src), encoding='utf-8')
    exe = out / ('sonda_carga.exe' if sys.platform == 'win32' else 'sonda_carga')
    subprocess.run([a.cxx, '-std=c++17', str(cpp), '-o', str(exe)], cwd=out, check=True, timeout=90)
    meta = []
    for fase in range(1, 10):
        base = orig.Original(a.sstates / f'fase{fase}.p2s')
        raw, piezas, traza = pal(base)
        path = out / f't2_fase{fase}.bin'
        path.write_bytes(raw)
        b = json.loads(subprocess.check_output([str(exe), str(path), '1' if a.slot_pal else '0'], cwd=out, timeout=10))
        da, db = doc(fase, piezas, 'elf_pal'), doc(fase, b['piezas'], 'arnes_cpp')
        guardar(out / 'A_pal' / f'baseline_estado_fase{fase}.jsonl', da)
        guardar(out / 'B_arnes' / f'actual_fase{fase}.jsonl', db)
        # Inventario de TODOS los campos distintos; el juez solo muestra el primero por hito.
        pa, pb = (b2_juez._piezas(d) for d in (da, db))
        difs = [dict(n=n, campo=k, a=pa[n].get(k), b=pb[n].get(k))
                for n in sorted(pa.keys() & pb.keys()) for k in b2_juez.CAMPOS_PIEZA if pa[n].get(k) != pb[n].get(k)]
        meta.append(dict(fase=fase, **base.identity, traza=traza, diferencias=difs,
                         piezas_solo_a=sorted(pa.keys()-pb.keys()), piezas_solo_b=sorted(pb.keys()-pa.keys())))
    bat = subprocess.run([sys.executable, '-B', str(TOOLS / 'b2_juez_bateria.py'),
                          '--a', str(out/'A_pal'), '--b', str(out/'B_arnes'), '--salida', str(out/'juez')],
                         capture_output=True, encoding='utf-8', errors='replace', timeout=60)
    (out/'bateria.log').write_text(bat.stdout + bat.stderr, encoding='utf-8')
    if len(list((out/'juez').glob('juez_fase*.json'))) != 9 or bat.returncode not in (0, 1, 2):
        raise RuntimeError('batería incompleta: ver bateria.log')
    guardar(out/'medicion.json', dict(fuente=str(fuente), fuente_sha256=orig.sha(fuente), arnes_sha256=orig.sha(cpp),
                                      slot_pal=a.slot_pal, codigo_juez=bat.returncode, fases=meta))
    print(bat.stdout, end='')
    return bat.returncode


def selfcheck():
    """Controles offline de extraccion C++ y contrato b2-state-v1 sin ISO, ELF ni compilador."""
    if tramo('a[UNO]b', '[', ']') != '[UNO':
        raise ValueError('tramo() fallo en extraccion basica')
    for bad in ('sin ancla', '[A][B]'):
        try:
            tramo(bad, '[', ']')
        except ValueError:
            pass
        else:
            raise ValueError(f'tramo() debia rechazar: {bad}')
    fake_src = (
        'constexpr int kGoku = 0;\n'
        'struct Ficha {\n'
        '  int n;\n'
        '};\n'
        'void RecalcularSlots() {\n'
        '}\n'
        'bool Activa(const Ficha& f) { return true; }\n'
        'void Cargar() {\n'
        '  for (uint32_t p = 1; p < n2; ++p) {\n'
        '    g_d.piezas.push_back(Ficha{});\n'
        '  }\n'
        '}\n'
        'void Empezar() {\n'
        '  g_f = g_d.piezas;\n'
        '  g_buscada.assign(1, false);\n'
        '  if (!REXCVAR_GET(dbz3_b2_pm23_slot_inicial_pal)) RecalcularSlots();\n'
        '  g_sel = 0;\n'
        '}\n'
        'void Volcar() {\n'
        '  j += ",\\"piezas\\":[";\n'
        '  j += "],\\"objetos\\":[";\n'
        '}\n'
    )
    cpp = arnes(fake_src)
    for token in ('dbz3_b2_pm23_slot_inicial_pal', 'struct Ficha', 'RecalcularSlots'):
        if token not in cpp:
            raise ValueError(f'arnes() omitio {token}')
    d = doc(7, [dict(n=1, c=0, bando=0, barras=5, barras_max=5, casilla=1, slot=0, fuera=False, en_juego=True, estado0=0)], 'elf_pal')
    if b2_juez.validar_volcado(d) or b2_juez.autoprueba() != 0:
        raise ValueError('contrato b2_juez invalido en selfcheck')
    print('b2_pm23_carga autoprueba: OK (tramo, arnes sintetico, doc y b2_juez)')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, epilog='0 OK, 1 FAIL, 2 PENDIENTE_DATO (carga parcial) o error de uso, 3 error de ejecución.')
    p.add_argument('--self-check', '--autoprueba', dest='self_check', action='store_true',
                   help='controles offline sin savestates, ELF ni compilador')
    p.add_argument('--salida', type=Path, help='directorio nuevo; guarda los pares y el resultado')
    p.add_argument('--sstates', type=Path, default=ESTADOS, help='directorio de fase1.p2s ... fase9.p2s')
    p.add_argument('--src', type=Path, default=TOOLS.parent/'src/pasarela_b2.cpp', help='fuente del port, admite copia parcheada')
    p.add_argument('--slot-pal', action='store_true', help='activa dbz3_b2_pm23_slot_inicial_pal solo en el arnés (apagada de serie)')
    llvm = Path('C:/Program Files/LLVM/bin/clang++.exe')
    p.add_argument('--cxx', default=str(llvm) if llvm.is_file() else shutil.which('clang++') or 'c++', help='compilador C++17')
    a = p.parse_args(argv)
    try:
        if a.self_check:
            selfcheck()
            return 0
        if a.salida is None:
            p.error('the following arguments are required: --salida')
        return medir(a)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as e:
        print(f'ERROR: {e}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())

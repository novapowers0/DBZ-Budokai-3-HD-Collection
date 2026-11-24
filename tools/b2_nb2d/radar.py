"""Banco de solicitudes del radar: D_RADAR del ELF vs. bloque literal de DibHud."""
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

SRC = Path('src/pasarela_b2.cpp')
FLAG = 'dbz3_b2_radar_indicadores_pal'


def call(args, cwd, data=None):
    p = subprocess.run(args, cwd=cwd, input=data, encoding='utf-8', errors='replace',
                       capture_output=True, timeout=90, check=False)
    if p.returncode:
        raise RuntimeError(f'{args}: {p.returncode}\n{p.stdout}{p.stderr}')
    return p.stdout


def cases():
    # Fichas/estado inyectados. La espera tiene como actor PAL a la última ficha
    # del jugador. El port conserva g_sel incluso durante la acción del rival.
    actors = [('jugador', 0x12, True, False, 0),
              ('aliado', 0x12, True, False, 0),
              ('inicio-rival', 0x12, True, True, -1),
              ('rival-elige', 0x22, True, True, 0),
              ('rival-camina', 0x22, True, True, 2),
              ('neutral', 0x02, False, False, 0)]
    layouts = [('vacio', []), ('cerca', [(299, 0)]), ('borde', [(0, -300)]),
               ('lejos', [(-301, 0)]), ('tres', [(299, 0), (0, -300), (-301, 0)])]
    for actor, flags, selected, phase, step in actors:
        for layout, balls in layouts:
            for field in (0, 50, 100):
                yield dict(id=f'{actor}/{layout}/{field}', ficha=4 if actor == 'aliado' else 2,
                           flags=flags, seleccion_jugador=selected, fase_rival=phase,
                           paso_rival=step, campo=field, bolas=balls)


def pal(o, base, row):
    c = base.cpu({0x278B00, 0x27C230, 0x278E20, 0x2732E0})
    tarea, board, quad = o.TASK, o.CTX, o.HEAP
    t1, t8, records = board + 0x100, board + 0x300, board + 0x400
    delta = 1.2
    def f(addr, value):
        struct.pack_into('<f', c.m, addr, value)
    c.m[tarea:tarea+0x50] = bytes(0x50)
    c.w32(c.gp - 0x732C, tarea)
    c.w32(c.gp - 0x7748, board)
    c.w32(c.gp - 0x772C, row['ficha'])
    c.w16(0x69B7A6 + row['ficha']*36, row['flags'])
    c.w16(0x69B7A8 + row['ficha']*36, 1)
    c.w8(0x6992DC, 1)
    c.w32(tarea + 0x38, 17)
    c.w32(tarea + 0x3C, 3)
    c.w32(tarea + 0x4C, 2)  # fuera del alcance del sonido: sin repetir pitidos
    f(tarea + 0x34, 121)
    f(tarea + 0x44, row['campo'] * delta)
    f(c.gp - 0x7994, delta)
    c.w32(board + 0x24, t1)
    c.w32(board + 0x5C, t8)
    c.w32(board + 0x58, len(row['bolas']))
    f(t1 + 52, 0)
    f(t1 + 56, 0)
    c.m[0x3A4A28:0x3A4A2E] = b'\xff\x00'*3
    for i, (x, y) in enumerate(row['bolas']):
        f(t1 + (i+2)*52, x)
        f(t1 + (i+2)*52 + 4, y)
        c.w32(t8 + i*4, records + i*16)
        c.w16(records + i*16, i+2)
        c.w8(records + i*16 + 4, 0)
        c.w8(0x3A4A28 + i*2, i)
    puentes, draws = [], []
    o.service(c, 0x148EF0, 'selección de banco', puentes)
    o.service(c, 0x245C00, 'duración AMA dirigida (#59 = 121)', puentes, result=121)
    def draw(cpu):
        # a1 es el índice: el primer argumento flotante llega por f12.
        idx = cpu.r[5] & 0xFFFFFFFF
        addr = quad + len(draws)*64
        cpu.m[addr:addr+64] = bytes(64)
        draws.append((idx, addr, o.ee.fv(cpu.f[12])))
        cpu.r[2] = addr
    def atan(cpu):
        # Solo se comparan selección y reloj, no el ángulo generado por esta llamada.
        y, x = (o.ee.fv(cpu.f[i]) for i in (12, 13))
        puentes.append(dict(servicio='atan2 (ángulo excluido del juicio)', direccion='0x11d9a0'))
        cpu.f[0] = o.ee.fb(math.atan2(y, x))
    c.hooks[0x1A8D10] = draw
    c.hooks[0x11D9A0] = atan
    o.invoke(c, 0x26F940)
    trace = o.trace(c, puentes)
    trace['draw_hook'] = '0x1a8d10'
    return [[k, 0. if k == 17 else clock] for k, _, clock in draws], trace


def literal_radar(text):
    a = text.index('      if (g_radar && g_sel < int(g_f.size())) {', text.index('void DibHud('))
    b = text.index('\n      }\n', a) + len('\n      }')
    return text[a:b]


def port(src, out, cxx, rows, modes):
    block = literal_radar(src.read_text(encoding='utf-8-sig'))
    here = Path(__file__).resolve().parent
    path = out / (src.parent.parent.name + '-radar.cpp')
    program = (here / 'radar.cpp.in').read_text(encoding='utf-8').replace('@RADAR@', block)
    with path.open('x', encoding='utf-8', newline='\n') as f:
        f.write(program)
    exe = path.with_suffix('.exe')
    call([cxx, '-std=c++20', '-ffp-contract=off', str(path), '-o', str(exe)], out)
    inputs = [[mode, int(r['seleccion_jugador']), int(r['fase_rival']), r['paso_rival'], r['campo'],
               len(r['bolas']), *(v for xy in r['bolas'] for v in xy)] for mode in modes for r in rows]
    obs = [json.loads(line) for line in call([str(exe)], out, ''.join(' '.join(map(str, r))+'\n'
                                                                  for r in inputs)).splitlines()]
    if len(obs) != len(inputs):
        raise ValueError('Faltan filas en la sonda C++')
    return dict(zip(modes, (obs[i:i+len(rows)] for i in range(0,len(obs),len(rows))), strict=True)), digest(path)


def equal(a, b):
    return len(a) == len(b) and all(i == j and abs(t-u) <= .002 for (i,t),(j,u) in zip(a,b,strict=True))


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def measure(repo, state, out, cxx):
    from b2_oraculo import original as o
    candidate = repo / SRC
    if f'REXCVAR_GET({FLAG})' not in candidate.read_text(encoding='utf-8-sig'):
        raise ValueError('el ajuste de radar no está integrado')
    rows = list(cases())
    base = o.Original(state)
    b, sha_b = port(candidate, out, cxx, rows, (0, 1))
    for i, r in enumerate(rows):
        ref, trace = pal(o, base, r)
        vals = dict(OFF=b[0][i], ON=b[1][i])
        r.update(PAL=ref, **vals, traza=trace, igual={k: equal(ref, v) for k, v in vals.items()})
    summary = {k: sum(not r['igual'][k] for r in rows) for k in rows[0]['igual']}
    result = dict(formato='nb2d-radar-v1', alcance='Selección ordenada y reloj inicial de sprites de D_RADAR abierto; no ángulo, GS, flujo del turno ni audio.',
                  identidad=dict(PAL=base.identity, cpp_candidato=digest(candidate), sonda_candidato=sha_b),
                  casos=rows, diferencias=summary)
    return result

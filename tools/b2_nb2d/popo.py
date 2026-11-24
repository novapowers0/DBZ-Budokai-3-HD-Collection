"""Coteja los huecos de aterrizaje de Popo en la ruta T1 real 5 → 24 (IX)."""
import json
from pathlib import Path

from radar import SRC, call, digest

FLAG = 'dbz3_b2_popo_hueco_pal'


def cases():
    for actor, ally in ((1, 4), (4, 1)):
        for end in (False, True):
            for start in (False, True):
                # En el origen, el viajero ocupa 0 y su compañero 1. En el
                # destino, el compañero ya estaba en 0 antes de la llegada.
                yield dict(id=f'ficha-{actor}/destino-{int(end)}/origen-{int(start)}', actor=actor,
                           piezas={actor:[5,0], **({ally:[24,0]} if end else {}),
                                   **({2:[5,1]} if start else {})})


def pal(p, base, row):
    o = p.o
    c, _ = p.setup(base, {0x27A2B0, 0x278A30, 0x273950})
    tiles, nt = p.board(c)
    n = c.r32(c.r32(c.gp - 0x7748) + 0x28)
    if not 4 < n <= 32 or nt <= 24 or (c.r32(tiles+52*5+16), c.r32(tiles+52*5+20)) != (3,24):
        raise ValueError('el estado no contiene la ruta T1 real de IX 5→24')
    for i in range(1, n):
        c.w16(p.piece(i) + 6, 0)
    for i, (tile, slot) in row['piezas'].items():
        addr = p.piece(i)
        c.w16(addr+6, 0x12)
        c.w16(addr+8, tile)
        c.w16(addr+10, tile)
        c.w8(addr+12, slot)
        c.w8(addr+13, slot)
    c.w32(c.gp - 0x72D0, o.HEAP)
    o.invoke(c, 0x27A370, row['actor'], 24)
    return [[n, c.r16(p.piece(n)+8), c.r16(p.piece(n)+10), c.r8(p.piece(n)+12), c.r8(p.piece(n)+13)]
            for n in sorted(row['piezas'])], o.trace(c)


def port(src, out, compiler, rows, modes, name):
    from cpp import fragment
    text = src.read_text(encoding='utf-8-sig')
    block = '\n'.join(fragment(text, s) for s in ('int HuecoLibre(const Ficha& f, int casilla) {',
                    'void RecalcularSlots() {', 'void CurarDende(Ficha& f) {', 'void ActualizarAlfombra() {'))
    template = Path(__file__).with_name('popo.cpp.in').read_text(encoding='utf-8')
    path = out / f'popo-{name}.cpp'
    with path.open('x', encoding='utf-8', newline='\n') as f:
        f.write(template.replace('@FUNCIONES@', block))
    exe = path.with_suffix('.exe')
    call([compiler, '-std=c++20', str(path), '-o', str(exe)], out)
    commands = [[mode, r['actor'], len(r['piezas']), *(v for i,p in sorted(r['piezas'].items()) for v in (i,*p))]
                for mode in modes for r in rows]
    stdout = call([str(exe)], out, ''.join(' '.join(map(str,r))+'\n' for r in commands))
    obs = [json.loads(line) for line in stdout.splitlines()]
    if len(obs) != len(commands):
        raise ValueError('salida de aterrizaje incompleta')
    return {k:obs[i*len(rows):(i+1)*len(rows)] for i,k in enumerate(modes)}, digest(path)


def measure(repo, state, out, cxx):
    import pal as p
    candidate = repo / SRC
    if f'REXCVAR_GET({FLAG})' not in candidate.read_text(encoding='utf-8-sig'):
        raise ValueError('el ajuste de Popo no está integrado')
    base = p.o.Original(state)
    rows = list(cases())
    b, sha_b = port(candidate, out, cxx, rows, (0,1), 'candidato')
    for i,r in enumerate(rows):
        ref, trace = pal(p, base, r)
        vals = dict(OFF=b[0][i], ON=b[1][i])
        r.update(PAL=ref, **vals, traza=trace, igual={k:ref == v for k,v in vals.items()})
    return dict(formato='nb2d-popo-v1', alcance='Aterrizaje 5→24: destino vacío o con un aliado en hueco 0; origen solo o compartido. No el desvío por destino bloqueado.',
                identidad=dict(PAL=base.identity, cpp_candidato=digest(candidate), sonda_candidato=sha_b),
                diferencias={k:sum(not r['igual'][k] for r in rows) for k in rows[0]['igual']}, casos=rows)

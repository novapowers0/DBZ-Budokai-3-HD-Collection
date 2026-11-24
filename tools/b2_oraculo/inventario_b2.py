# SPDX-License-Identifier: MIT
"""B2-3: INVENTARIO.md del volcado de B2 (volcar_b2.py) con el cruce contra lo recreado a mano en src/pasarela_b2.cpp.
El cruce es por ENTRADA DEL AFS: el original la lee con FUN_0012D100(afs << 16 | entrada) (spx.jsonl) y el port con
cmn.Entrada(N) / leer_ama(N) / CmnTex(N) / loc.Entrada(N) (se sacan del fuente con su linea).

  python tools/b2_oraculo/inventario_b2.py NOMBRE [NOMBRE...]   -> E:/DBZ3HD_trabajo/b2_volcado/INVENTARIO.md
"""
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

SAL = Path(r'E:\DBZ3HD_trabajo\b2_volcado')
PORT = Path(__file__).parent.parent.parent / 'src' / 'pasarela_b2.cpp'
AFS = {0: 'DATA_CMN', 1: 'DATA_<idioma>'}
FUNC = {'0x12d100': 'lectura AFS', '0x224c80': 'builtin 0x150', '0x1e8dc0': 'builtin 0x151', '0x1e7ae0': 'builtin 0x152',
        '0x224bb0': 'builtin 0x153', '0x224b50': 'builtin 0x154',
        '0x23a050': 'figura 3D del jugador (lista de dibujo, tipo 4)', '0x23a2e0': 'figura 3D (tipo 3)',
        '0x23a620': 'pieza animada por claves (tipos 2/5)'}
FASES = range(1, 10)


def port():
    """{(afs, entrada): [lineas]} de lo que lee el port. Formulas con `fase` se expanden a las 9 fases."""
    usa = defaultdict(list)
    pat = re.compile(r'\b(cmn|panel_cmn|loc|afs|adx)?\.?(Entrada|leer_ama|CmnTex)\(')

    def primero(t):          # primer argumento (hasta la coma o el parentesis de cierre, a nivel 0)
        n = 0
        for i, ch in enumerate(t):
            if ch == '(':
                n += 1
            elif ch == ')' and n == 0 or ch == ',' and n == 0:
                return t[:i]
            elif ch == ')':
                n -= 1
        return t
    for n, l in enumerate(PORT.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
        for m in pat.finditer(l):
            quien, expr = m.group(1) or '', primero(l[m.end():]).replace('size_t', '').replace('uint32_t', '')
            if quien == 'adx' or 'CeldaEst' in l:
                continue
            afs = 1 if quien == 'loc' else 0
            try:
                if 'fase' in expr:
                    for f in FASES:
                        usa[(afs, int(eval(expr, {'fase': f - 1, '__builtins__': {}})))].append(f'{n} (fase {f})')
                else:
                    usa[(afs, int(eval(expr, {'__builtins__': {}})))].append(str(n))
            except Exception:
                pass
    return usa


def afs_del_disco():
    """[(nombre, lsn, tam, [desplazamientos de entrada])] de los AFS de /USR de la ISO (para pasar de LSN a entrada)."""
    import bisect
    import struct
    sys.path.insert(0, str(Path(__file__).parent))
    sys.path.insert(0, str(Path(__file__).parent.parent / 'iw_mips'))
    import oraculo_b2
    o = oraculo_b2.o
    cd = o.Cd.__new__(o.Cd)
    cd.f = o.ISO.open('rb')
    out = []
    for nom in ('DATA_CMN', 'DATA_SP', 'DATA_UK', 'DATA_FR', 'DATA_IT', 'DATA_DU', 'ADX_US'):
        h = cd.iso_buscar(f'/USR/{nom}.AFS')
        if not h:
            continue
        cab = cd.sector(h[0], 1)
        n = struct.unpack_from('<I', cab, 4)[0]
        cab = cd.sector(h[0], (8 + 8 * n + 2047) // 2048)
        out.append((nom, h[0], h[1], [struct.unpack_from('<I', cab, 8 + 8 * i)[0] for i in range(n)]))

    def resolver(lsn, cuantos):
        for nom, l0, tam, offs in out:
            if l0 <= lsn < l0 + (tam + 2047) // 2048:
                a, b = (lsn - l0) * 2048, (lsn - l0 + cuantos) * 2048 - 1
                i, j = bisect.bisect_right(offs, a) - 1, bisect.bisect_right(offs, b) - 1
                return nom, list(range(max(i, 0), j + 1)) if i >= 0 else []
        return None, []
    return resolver


def uno(nombre):
    base = SAL / nombre
    r = Counter()
    ns = []
    for l in (base / 'gs.jsonl').open(encoding='utf-8'):
        f = json.loads(l)
        ns.append(f['n'])
        prim = 0
        for o in f['ordenes']:
            if o[0] == 'G':
                if o[1] == 0:
                    prim = o[2] & 7
                elif o[1] in (5, 0xD):        # XYZ2 con dibujo
                    r['sprites 2D (vertices/2)' if prim == 6 else 'vertices de triangulo/linea por GIF'] += 0.5 if prim == 6 else 1
            else:
                r[{'I': 'subidas IMAGE', 'U': 'UNPACK (VU1)', 'M': 'MSCAL (VU1, 3D)', 'P': 'MPG', 'V': 'VIF otros'}[o[0]]] += 1
    lect, bi = [], Counter()
    for l in (base / 'spx.jsonl').read_text(encoding='utf-8').splitlines():
        if not l.strip():
            continue
        d = json.loads(l)
        if d['f'] == '0x12d100':
            a = int(d['a'][0], 16)
            lect.append((d['n'], a >> 16, a & 0xFFFF, d['ra']))
        else:
            bi[(FUNC.get(d['f'], d['f']), d['ra'])] += 1
    cd = {}
    if (base / 'cd.json').exists():          # lecturas de CD -> entradas (lo que de verdad lee, lo cargue quien lo cargue)
        res = afs_del_disco()
        for n, lsn, cuantos in json.load((base / 'cd.json').open()):
            nom, ents = res(lsn, cuantos)
            for e in ents:
                cd.setdefault((nom, e), n)
    son = Counter()
    for l in (base / 'sonido.jsonl').read_text(encoding='utf-8').splitlines():
        d = json.loads(l)
        son[(d['sid'], d['fno'])] += 1
    tex = json.load((base / 'texturas.json').open()) if (base / 'texturas.json').exists() else {}
    return dict(nombre=nombre, ns=ns, r=r, lect=lect, bi=bi, son=son, tex=tex, cd=cd,
                res=json.load((base / 'resumen.json').open()))


def main(nombres):
    usa = port()
    vs = [uno(n) for n in nombres]
    t = ['# Volcado del Budokai 2 original con el oráculo (B2 PAL, SLES-51839)', '',
         'Generado por `tools/b2_oraculo/inventario_b2.py` desde `volcar_b2.py` (oráculo `oraculo_b2.py`: el juego entero '
         'desde la entrada del ELF, sin emulador ni savestates; mando guionizado). Idioma: español.',
         'Texturas: PNG en `<nombre>/tex/`, nombre = XXH3-64 de los texeles crudos + XXH3-64 de la CLUT (el identificador '
         'de las capas y del pack HD). `ver_<vblank>.png` = cuadro pintado solo para comprobar (no es el traductor del juego).',
         'El juego dibuja en un búfer de 512x512 (FBP 0x40) y lo copia a la pantalla en dos medias copias de 512x256: el '
         'traductor del juego tiene que saltarse esa copia (textura = el propio búfer, TBP 0x800).', '',
         '| Volcado | Vblanks | Cuadros | Sprites 2D | Vértices tri/línea (GIF) | Subidas | MSCAL (3D) | Texturas 2D | Lecturas AFS | Órdenes de sonido | Fin |',
         '|---|---|---|---|---|---|---|---|---|---|---|']
    for v in vs:
        r = v['r']
        t.append(f"| {v['nombre']} | {v['ns'][0] if v['ns'] else '-'}-{v['ns'][-1] if v['ns'] else '-'} | {len(v['ns'])} | "
                 f"{int(r['sprites 2D (vertices/2)'])} | {r['vertices de triangulo/linea por GIF']} | {r['subidas IMAGE']} | "
                 f"{r['MSCAL (VU1, 3D)']} | {len(v['tex'])} | {len(v['lect'])} | {sum(v['son'].values())} | {v['res']['fin'][:60]} |")
    t += ['', '## Cruce original ↔ port por entrada del AFS', '',
          'Original: `FUN_0012D100(afs << 16 | entrada)`. Port: lecturas de `src/pasarela_b2.cpp` (línea). '
          '«solo original» = lo que el port no lee con un número fijo (lo recrea con otra cosa, lo lee por tabla o no lo tiene); '
          'esas son las candidatas a salir traducidas. «las dos» = solape: con el traducido encendido solo debe quedar una fuente.', '']
    for v in vs:
        t += [f"### {v['nombre']}", '', '| Vblank | AFS | Entrada | Llamador | Port (línea de pasarela_b2.cpp) |', '|---|---|---|---|---|']
        vistos = set()
        for n, afs, ent, ra in v['lect']:
            if (afs, ent) in vistos:
                continue
            vistos.add((afs, ent))
            p = usa.get((afs, ent))
            t.append(f"| {n} | {AFS.get(afs, afs)} | {ent} | {ra} | {'las dos: ' + ', '.join(p[:4]) if p else 'solo original'} |")
        dos = sum(1 for k in vistos if k in usa)
        t += ['', f'Entradas distintas: {len(vistos)}; las lee también el port: {dos}; solo el original: {len(vistos) - dos}.', '']
        if v['cd']:
            t += ['Lecturas de CD de este tramo resueltas a entrada (LSN → AFS; incluye lo que no pasa por `FUN_0012D100`):', '',
                  '| Vblank | AFS | Entrada | Port (línea de pasarela_b2.cpp) |', '|---|---|---|---|']
            idi = {'DATA_SP': 1}
            for (nom, e), n in sorted(v['cd'].items(), key=lambda x: (x[1], x[0])):
                if nom == 'ADX_US':
                    continue
                p = usa.get((idi.get(nom, 0), e)) if nom in ('DATA_CMN', 'DATA_SP') else None
                t.append(f"| {n} | {nom} | {e} | {'las dos: ' + ', '.join(p[:4]) if p else 'solo original'} |")
            adx = sorted(e for (nom, e) in v['cd'] if nom == 'ADX_US')
            t += ['', f"Audio (ADX_US, entradas): {', '.join(map(str, adx)) or 'ninguna'}.", '']
        if v['bi']:
            t += ['Órdenes de actor (van como ORDEN, no como triángulos; cada una con su dato en `spx.jsonl`: posición, '
                  'animación, cuadro) y builtins #SPX de figura 0x150-0x154: '
                  + ', '.join(f'{k[0]} ×{n}' for k, n in v['bi'].most_common()) + '. Destino en el port: modelos B3 HD y '
                  'escenarios portados según `studies 5/ENTREGAS/B2_SPX_MODELOS.md` (§2 personajes, §4 escenarios).', '']
            if not any(k[0].startswith('builtin') for k in v['bi']):
                t += ['Builtins #SPX 0x150-0x154 (viñeta del combate): ninguna llamada en este tramo (no hubo contacto).', '']
        else:
            t += ['Órdenes de actor y builtins #SPX: no se apuntaron en este tramo.', '']
        if v['son']:
            t += ['Sonido (RPC sid/fno ×veces): ' + ', '.join(f'{k[0]}/{k[1]} ×{n}' for k, n in v['son'].most_common(12)), '']
        if v['tex']:
            t += ['| Textura (PNG) | PSM | Sprites | Tiras | Vblanks | Rectángulo en pantalla |', '|---|---|---|---|---|---|']
            for nom, u in sorted(v['tex'].items(), key=lambda x: -(x[1]['sprites'] + x[1].get('tiras', 0)))[:60]:
                t.append(f"| {nom[:-4]} | {u['psm']} | {u['sprites']} | {u.get('tiras', 0)} | {u['primero']}-{u['ultimo']} | {u['rect']} |")
            t.append('')
    solo_port = sorted(k for k in usa if all(k not in {(a, e) for _, a, e, _ in v['lect']} for v in vs))
    t += ['## Lo que lee el port y el original no ha leído en estos tramos', '',
          ', '.join(f"{AFS.get(a, a)} {e}" for a, e in solo_port) or '(nada)', '',
          '(Son entradas de otras fases, de pantallas a las que el guion no llega, o de cosas añadidas por el port.)', '']
    (SAL / 'INVENTARIO.md').write_text('\n'.join(t), encoding='utf-8')
    print('INVENTARIO.md:', len(t), 'lineas;', len(usa), 'entradas del port')


if __name__ == '__main__':
    main(sys.argv[1:])

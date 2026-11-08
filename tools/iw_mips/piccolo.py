# SPDX-License-Identifier: MIT
"""dia1E4: esquiva ORIGINAL de Piccolo (IW EU 0x7D8, Viewer_UpdatePoseRotationInput 0x32DC80) ejecutada del ELF con ee.py.

Ejecuta tal cual: la funcion, Viewer_PoseScheduler 0x32D6B0, Viewer_PickRandomDirection 0x32D540, Viewer_PoseEnterState1/2,
Viewer_IsBelowThreshold(9878), Viewer_GetParamBC, Memory, la doble precision por software y la matematica VU0. Las tablas
0x5D98xx las pone Viewer_InitParamTables 0x3279F0 (tambien del ELF). Puentes (los de freezer.py): BonesMovement (anota el
clip), Hits_TransformationSounds, Char_GetBoneWorldPos y Math_WorldToScreen (su resultado no se usa).
Mismo espejo que src/iw_mips.cpp (piccolo::Esquiva). Nada del juego se copia al repo.
Uso: python tools/iw_mips/piccolo.py dorado              (capturas solIWX de PCSX2: llamadas en el estado 2, fila n-1 -> n)
     python tools/iw_mips/piccolo.py juego --log <dbz3.log> (llamadas del juego C++ reejecutadas en ee.py)
"""
import argparse
import glob
import json
import math
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import freezer  # noqa: E402
from bubbles import SEMILLA, f2b  # noqa: E402
from freezer import b2f, r32f  # noqa: E402

CAPS = 'E:/DBZ3HD_trabajo/iw_mips/solIWX/original_cuadros_17937*/7D8/cuadros.jsonl'   # capturas solIWX: borradas con D:/tmp el 7/11
CAP1 = Path('E:/DBZ3HD_trabajo/dorados_noche/7D8/llamadas.jsonl')   # N2: sonda cap_7d8.py, con semilla (estados 1 y 2)
ROT, INIT = 0x32DC80, 0x3279F0
X = freezer.X                            # Piccolo = *(0x49CA70) + 0x28D0 (mismo hueco que Freezer en el espejo)
CAM, VCTX = freezer.CAM, 0x1038000       # *(0x49CD6C) camara (+0x80 ojo, +0x90 objetivo); *(0x49CDF0) visor (+4 vida, +0x28 dif)
P_CAM, P_VCTX, PASO = 0x49CD6C, 0x49CDF0, 0x49C7DC
PASO_JUEGO = [0x3F99999A]   # N2: 1,2 PAL; 1,0 si el log es del ELF USA (iw_mips.cpp kPasoCampo, NTSC)
G = 0x5D9940                             # az, elev, golpe 948, modo, apunta90, giro, rumbo, rumbo_prev, contador, armada
GLOB = [G + 4 * i for i in range(10)]


class Espejo(freezer.Espejo):
    def __init__(s):
        super().__init__()
        s.cpu.hooks[freezer.PANTALLA] = lambda c: c.r.__setitem__(2, 1)   # = C++: PickRandomDirection no usa el resultado
        s.mem[:] = s.base
        s.cpu.call(INIT)                 # Viewer_InitParamTables: vida/tiempo/BC por dificultad y umbrales 0,25/0,4
        s.base[:] = s.mem

    def llamar(s, e):
        """e: seed, glob (10 palabras), estado, c74, dif, vida (bits), pos (bits), ojo, obj (bits)."""
        s.mem[:] = s.base
        c, w32 = s.cpu, s.cpu.w32
        s.ev, s.scr_usado, s.scr = [], [], []
        w32(SEMILLA, e['seed']); w32(P_CAM, CAM); w32(P_VCTX, VCTX); w32(PASO, PASO_JUEGO[0])
        w32(VCTX + 4, e['vida']); w32(VCTX + 0x28, e['dif'])
        for a, v in zip(GLOB, e['glob']):
            w32(a, v)
        c.w16(X + 0xCBC, e['estado'] & 0xFFFF); w32(X + 0xC74, e['c74'])
        for i in range(3):
            w32(X + 0x40 + 4 * i, e['pos'][i]); w32(CAM + 0x80 + 4 * i, e['ojo'][i]); w32(CAM + 0x90 + 4 * i, e['obj'][i])
        w32(X + 0x4C, 0x3F800000); w32(CAM + 0x8C, 0x3F800000); w32(CAM + 0x9C, 0x3F800000)
        freezer.ee.FPU_SUMA_PS2 = True   # = Ee::suma_ps2 del C++ (la resta del giro sube 1 ulp como el R5900)
        try:
            c.call(ROT, X)
        finally:
            freezer.ee.FPU_SUMA_PS2 = False
        s16 = lambda v: v - 0x10000 if v & 0x8000 else v
        return dict(seed=c.r32(SEMILLA), glob=[c.r32(a) for a in GLOB], estado=s16(c.r16(X + 0xCBC)),
                    b60=c.r32(X + 0xB60), ev=list(s.ev))


def angulos(pos, ojo):
    d = [pos[i] - ojo[i] for i in range(3)]
    r = math.sqrt(sum(x * x for x in d))
    return math.atan2(d[2], d[0]), math.asin(max(-1.0, min(1.0, d[1] / r))), r


def camara(fila):
    """Ojo y objetivo de la matriz de vista de la sonda: ojo = -t R^T, vista = -columna 2 (= objetivo - ojo del piloto)."""
    M = fila['camara']['matriz_comun']
    ojo = [-sum(M[12 + i] * M[4 * j + i] for i in range(3)) for j in range(3)]
    return ojo, [ojo[j] - M[4 * j + 2] for j in range(3)]


def prueba_dorado(caps=CAPS):
    """Piccolo en el estado 2 (sin azar: el planificador no corre) en las capturas solIWX, llamada del campo b = a+1."""
    esp, tot, malas = Espejo(), Counter(), []
    for p in sorted(glob.glob(caps)):
        filas = [json.loads(x) for x in open(p, encoding='utf-8')]
        for a, b, c in zip(filas, filas[1:], filas[2:]):
            pa, pb = a.get('piccolo'), b.get('piccolo')
            if not pa or not pb or b['n'] != a['n'] + 1 or c['n'] != b['n'] + 1 or pa['estado'] != 2:
                continue
            ga, gb = pa['gestor_pose'], pb['gestor_pose']
            if ga['modo'] not in (0, 1) or gb['modo'] not in (0, 1):   # la primera captura (…405100) leia otro campo en 100 filas
                tot['descartadas_modo_ilegible'] += 1
                continue
            if ga['modo'] != gb['modo'] or pa['seleccionado'] or a['piccolo']['temblor']['activo']:
                continue
            ojo, obj = camara(b)
            az, el, _ = angulos(b['actores'][1]['pos'], ojo)
            ac = a['actores'][1]
            c74 = int(pa['fin'] or (ac['codigo'] == pa['codigo'] and ac['f'] + 1.2 >= ac['dur']))
            vida = 1000.0 if ga['modo'] == 0 else 300.0   # dif 2 (vida 1000): modo = vida/1000 < 0,4
            glob_in = [f2b(az), f2b(el), 0, ga['modo'], 0, f2b(ga['giro']), ga['rumbo'], 0, f2b(ga['contador']), 0]
            e = dict(seed=0x12345678, glob=glob_in, estado=2, c74=c74, dif=2, vida=f2b(vida),
                     pos=[f2b(v) for v in b['actores'][1]['pos']], ojo=[f2b(v) for v in ojo], obj=[f2b(v) for v in obj])
            if pb['estado'] == 3:   # golpe: lo pone Viewer_HitTestNearestBone (el rayo del jugador), no esta funcion
                tot['golpes_externos'] += 1
                continue
            o = esp.llamar(e)
            quiere = [f2b(gb['giro']), f2b(gb['contador']), gb['rumbo'], gb['modo'], pb['estado']]
            sale = [o['glob'][5], o['glob'][8], o['glob'][6], o['glob'][3], o['estado']]
            az2, el2, _ = angulos(c['actores'][1]['pos'], camara(c)[0])
            dang = max(abs(b2f(o['glob'][0]) - az2), abs(b2f(o['glob'][1]) - el2))
            tot['llamadas'] += 1
            ok = quiere == sale and dang < 2e-5
            tot['iguales'] += ok
            tot['palabras_iguales'] += quiere == sale
            tot['semilla_sin_tocar'] += o['seed'] == 0x12345678
            if pb['estado'] != 2:
                tot['salidas_a_1'] += 1
                tot['salidas_a_1_iguales'] += ok
            tot['max_dang_e6'] = max(tot['max_dang_e6'], int(dang * 1e6))
            if not ok:
                malas.append((Path(p).parent.parent.name[-6:], b['n'], quiere, sale, dang))
    return tot, malas


def prueba_dorado1(cap=CAP1):
    """N2: cada llamada de la sonda cap_7d8 (PCSX2, con semilla y tiradas), entrada -> salida, palabra a palabra."""
    esp, tot, malas = Espejo(), Counter(), []
    PASO_JUEGO[0] = 0x3F99999A
    s16 = lambda v: v - 0x10000 if v & 0x8000 else v
    for linea in Path(cap).open(encoding='utf-8'):
        r = json.loads(linea)
        e = dict(seed=r['seed_in'], glob=[r['g%d' % k] for k in range(10)], estado=s16(r['estado'] & 0xFFFF), c74=r['c74'],
                 dif=r['dif'], vida=r['vida'], pos=[r['px'], r['py'], r['pz']], ojo=[r['ox'], r['oy'], r['oz']],
                 obj=[r['tx'], r['ty'], r['tz']])
        o = esp.llamar(e)
        quiere = [r['seed_out']] + [r['go%d' % k] for k in range(10)] + [s16(r['estado_out'] & 0xFFFF)]
        sale = [o['seed']] + o['glob'] + [o['estado']]
        clip_ok = (o['ev'][-1] == r['b60']) if o['ev'] else True
        ok = quiere == sale and clip_ok
        tot['llamadas'] += 1
        tot['iguales'] += ok
        tot['estado%d' % e['estado']] += 1
        tot['tiradas'] += r['seed_in'] != r['seed_out']
        tot['cambios_estado'] += e['estado'] != quiere[-1]
        if not ok:
            malas.append((r['i'], r['cnt'], quiere, sale, o['ev'], r['b60']))
    return tot, malas


RX = re.compile(r'dbz3 iwmips piccolo t=(\d+) seed_in=([0-9A-F]+) seed_out=([0-9A-F]+) in=(\S+) cam=(\S+) out=(\S+) ev=(\S+)')


def prueba_juego(log):
    if 'ELF USA' in Path(log).read_text(encoding='utf-8', errors='replace'):
        PASO_JUEGO[0] = 0x3F800000
    esp, tot, malas = Espejo(), Counter(), []
    H = lambda t: [int(x, 16) for x in t.split(',') if x]
    for linea in Path(log).read_text(encoding='utf-8', errors='replace').splitlines():
        m = RX.search(linea)
        if not m:
            continue
        t, si, so, ins, cam, out, ev = m.groups()
        w, k = H(ins), H(cam)
        e = dict(seed=int(si, 16), glob=w[:10], estado=w[10], c74=w[11], dif=w[12], vida=w[13], pos=k[0:3], ojo=k[3:6], obj=k[6:9])
        o = esp.llamar(e)
        sale = [o['seed']] + o['glob'] + [o['estado'] & 0xFFFF, o['b60']]
        quiere = [int(so, 16)] + H(out)
        evs = ','.join('%X' % v for v in o['ev']) or '-'
        tot['llamadas'] += 1
        ok = sale == quiere and evs == ev
        tot['iguales'] += ok
        tot['estado%d' % w[10]] += 1
        tot['entradas'] += evs != '-'
        tot['tiradas'] += int(si, 16) != int(so, 16)
        if not ok:
            malas.append((t, quiere, sale, ev, evs))
    return tot, malas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('que', choices=['dorado', 'dorado1', 'juego'])
    ap.add_argument('--log')
    ap.add_argument('--cap', help='dorado1: llamadas.jsonl de cap_7d8.py')
    a = ap.parse_args()
    if a.que == 'dorado' and not glob.glob(CAPS):   # N2: las capturas viejas ya no estan: el dorado nuevo de E:
        print('capturas solIWX ausentes (D:/tmp borrado el 7/11): uso', CAP1)
        a.que = 'dorado1'
    tot, malas = (prueba_dorado() if a.que == 'dorado' else prueba_dorado1(Path(a.cap) if a.cap else CAP1)
                  if a.que == 'dorado1' else prueba_juego(a.log))
    print(json.dumps(tot))
    for m in malas[:8]:
        print('DISTINTA', m)
    sys.exit(0 if tot['llamadas'] and tot['iguales'] == tot['llamadas'] else 1)


if __name__ == '__main__':
    main()

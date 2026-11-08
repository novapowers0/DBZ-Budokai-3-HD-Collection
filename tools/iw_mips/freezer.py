# SPDX-License-Identifier: MIT
"""Esquiva ORIGINAL de Freezer (IW EU 0x7D7, Char_PoseInputTransition 0x35DF50) ejecutada desde el ELF con ee.py.

Mismo espejo que src/iw_mips.cpp (freezer::Esquiva). Nada del juego se copia al repo.
Uso: python tools/iw_mips/freezer.py dorado            (capture eIW6 de PCSX2: llamadas sin azar = estado 2, palabra a palabra)
     python tools/iw_mips/freezer.py juego --log <dbz3.log>  (llamadas del juego C++ reejecutadas en ee.py)
"""
import argparse
import json
import math
import re
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import ee  # noqa: E402
from bubbles import ELF, GP, SEMILLA, f2b  # noqa: E402

CAP1 = Path('E:/DBZ3HD_trabajo/dorados_noche/7D7/llamadas.jsonl')   # N2: dorado con semilla (cap_7d7.py)
CAP = Path('E:/DBZ3HD_trabajo/iw_mips/solIWX/original_cuadros_1793936531407562500/7D7/cuadros.jsonl')
TRANS = 0x35DF50                         # Char_PoseInputTransition(freezer, krillin)
MGR = 0x1000000
X, J = MGR + 0x28D0, MGR + 0x800         # Freezer y Krillin (mismos huecos que el espejo de Bubbles)
CAM, VIEW = 0x1036000, 0x1037000         # *(0x49CD6C) controlador de camara, *(0x49CE78) visor (+0x14 ronda 0)
P_CAM, P_VIEW, PASO, ANCHO, ALTO = 0x49CD6C, 0x49CE78, 0x49C7DC, 0x49C84C, 0x49C850
TABLA = 0x5D9B48                         # ronda 0: B48 (float), B4C, B50
G = 0x5D9D60                             # az, elev, -, larga, apunta95, vel, dir, dir_prev, t, preparada, tirada
GLOB = [G, G + 4, G + 0xC, G + 0x10, G + 0x14, G + 0x18, G + 0x1C, G + 0x20, G + 0x24, G + 0x28]
BONES, SONIDO, HUESO, PANTALLA = 0x238670, 0x240740, 0x2EF6A0, 0x2EF870
PASO_JUEGO = [0x3F99999A]   # N2: 1,2 PAL; 1,0 si el log es del ELF USA (NTSC)
KB48 = [0.021, 0.021, 0.022, 0.022, 0.023, 0.023]
KB4C = [65, 65, 70, 70, 85, 85]
KB50 = [45, 50, 60, 65, 70, 75]


def b2f(b): return struct.unpack('<f', struct.pack('<I', b & ee.M32))[0]
def r32f(x): return b2f(f2b(x))


class Espejo:
    def __init__(s, elf=ELF):
        d = elf.read_bytes()
        s.base = bytearray(ee.TOTAL)
        phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
        for i in range(phnum):
            tipo, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
            if tipo == 1 and fsz:
                s.base[va:va + fsz] = d[off:off + fsz]
        s.mem = bytearray(s.base)
        c = s.cpu = ee.Ee(s.mem, GP)
        s.scr = []      # resultados del puente de pantalla a reponer (registro del juego); vacio = calcularlos

        def bones(c): s.ev.append(c.r32((c.r[4] & ee.M32) + 0xB60))

        def pantalla(c):  # Math_WorldToScreen(sale a0, pos a1, ojo a2, matriz a3) -> el port (iw_mini_disparo preparar)
            a = c.r[4] & ee.M32
            if s.scr:
                sx, sy = s.scr.pop(0)
            else:
                azp, el = b2f(c.r32(G)) - 1.5707964, b2f(c.r32(G + 4))
                fx_, fy_, fz_ = (b2f(c.r32(CAM + 0x90 + 4 * i)) for i in range(3))
                caz = r32f(math.atan2(-fx_, fz_)); cel = r32f(math.asin(max(-1.0, min(1.0, fy_))))
                sx = f2b(r32f(320.0 + r32f(r32f(azp - caz) * r32f(math.cos(el)))))
                sy = f2b(r32f(224.0 + r32f(-(el - cel))))
            c.w32(a, sx); c.w32(a + 4, sy); c.w32(a + 8, 0); c.w32(a + 12, 0x3F800000)
            s.scr_usado.append((sx, sy))
            c.r[2] = 1
        c.hooks.update({BONES: bones, SONIDO: lambda c: None, HUESO: lambda c: None, PANTALLA: pantalla})

    def llamar(s, e):
        """e: semilla, glob (10 palabras), estado, c74, krillin, dif, f (dir de camara, bits), pos (bits)."""
        s.mem[:] = s.base
        c, w32 = s.cpu, s.cpu.w32
        s.ev, s.scr_usado = [], []
        w32(SEMILLA, e['seed']); w32(P_CAM, CAM); w32(P_VIEW, VIEW); w32(VIEW + 0x14, 0)
        w32(PASO, PASO_JUEGO[0]); w32(ANCHO, f2b(640.0)); w32(ALTO, f2b(448.0))
        dif = e['dif']
        w32(TABLA, f2b(KB48[dif])); w32(TABLA + 4, KB4C[dif]); w32(TABLA + 8, KB50[dif])
        for a, v in zip(GLOB, e['glob']):
            w32(a, v)
        c.w16(X + 0xCBC, e['estado'] & 0xFFFF); w32(X + 0xC74, e['c74']); c.w16(J + 0xCBC, e['krillin'] & 0xFFFF)
        for i in range(3):
            w32(CAM + 0x90 + 4 * i, e['f'][i]); w32(X + 0x40 + 4 * i, e['pos'][i])
        w32(CAM + 0x8C, 0x3F800000); w32(CAM + 0x9C, 0x3F800000); w32(X + 0x4C, 0x3F800000)
        if 'cam8' in e:   # N2: dorado con semilla (cap_7d7): camara +0x80..+0x9C, posicion, tabla, ronda y paso leidos
            for k in range(8):
                w32(CAM + 0x80 + 4 * k, e['cam8'][k])
            for k in range(4):
                w32(X + 0x40 + 4 * k, e['pos4'][k])
            w32(ANCHO, f2b(600.0)); w32(ALTO, f2b(450.0))   # pantalla real del original (savestate 37: 600 x 450)
            w32(PASO, e['paso'])   # la fila de la tabla 0x5D9B48 (paso 0x18) es *(visor+0x14) = dificultad: KB* del port
        ee.FPU_SUMA_PS2 = True   # = Ee::suma_ps2 del C++ (solo en esta llamada)
        try:
            c.call(TRANS, X, J)
        finally:
            ee.FPU_SUMA_PS2 = False
        s16 = lambda v: v - 0x10000 if v & 0x8000 else v
        return dict(seed=c.r32(SEMILLA), glob=[c.r32(a) for a in GLOB], estado=s16(c.r16(X + 0xCBC)),
                    b60=c.r32(X + 0xB60), ev=list(s.ev), scr=list(s.scr_usado))


def pos_de(az, el):  # Char_PoseModeUpdateAll: (cos el cos az, sin el, cos el sin az) * 150, ojo en el origen
    ce = r32f(math.cos(el))
    return [f2b(r32f(r32f(math.cos(az) * 150.0) * ce)), f2b(r32f(math.sin(el) * 150.0)), f2b(r32f(r32f(math.sin(az) * 150.0) * ce))]


def glob_fila(fz):
    return [f2b(fz['az']), f2b(fz['elev']), fz['larga'], fz['apunta95'], f2b(fz['vel']), fz['dir'], fz['dir_prev'],
            f2b(fz['tiempo']), fz['preparada'], fz['tirada']]


def prueba_dorado(cap=CAP, dif=2):
    """Llamadas sin azar (Freezer en el estado 2: B4C no tira, el tracker no corre) de la captura eIW6, n-1 -> n."""
    filas = [json.loads(x) for x in cap.open()]
    esp, tot, malas = Espejo(), Counter(), []
    for a, b in zip(filas, filas[1:]):
        fa, fb_ = a.get('freezer'), b.get('freezer')
        if not fa or not fb_ or b['n'] != a['n'] + 1 or fa['freezer']['estado'] != 2:
            continue
        M = (b.get('camara') or {}).get('matriz_comun')
        if not M:
            continue
        f = [f2b(-M[8]), f2b(-M[9]), f2b(-M[10])]   # vista = -fila 2 de la matriz (488/489 con apunta95)
        # c74 lo pone AllAnimations DESPUES de la sonda: el clip en su ultimo campo (f + 1,2 >= duracion) = fin
        ac = a['actores'][1]
        c74 = int(fa['freezer']['c74'] or (ac['codigo'] == fa['freezer']['codigo'] and ac['f'] + 1.2 >= ac['dur']))
        e = dict(seed=0x12345678, glob=glob_fila(fa), estado=2, c74=c74,
                 krillin=fa['krillin']['estado'], dif=dif, f=f, pos=pos_de(fa['az'], fa['elev']))
        o = esp.llamar(e)
        g = b['freezer']
        quiere = [f2b(g['az']), f2b(g['elev']), f2b(g['vel']), f2b(g['tiempo']), g['dir'], g['freezer']['estado']]
        sale = [o['glob'][0], o['glob'][1], o['glob'][4], o['glob'][7], o['glob'][5], o['estado']]
        tot['llamadas'] += 1
        tot['iguales'] += quiere == sale
        tot['semilla_sin_tocar'] += o['seed'] == 0x12345678
        if g['freezer']['estado'] != 2:
            tot['salidas_a_1'] += 1
            tot['salidas_a_1_iguales'] += quiere == sale
        if quiere != sale:
            malas.append((b['n'], quiere, sale))
    return tot, malas


RX = re.compile(r'dbz3 iwmips freezer t=(\d+) seed_in=([0-9A-F]+) seed_out=([0-9A-F]+) in=(\S+) f=(\S+) pos=(\S+) '
                r'dif=(\d) scr=(\S+) out=(\S+) ev=(\S+)')


def prueba_juego(log):
    if 'ELF USA' in Path(log).read_text(encoding='utf-8', errors='replace'):
        PASO_JUEGO[0] = 0x3F800000
    esp, tot, malas = Espejo(), Counter(), []
    H = lambda t: [int(x, 16) for x in t.split(',') if x]
    for linea in Path(log).read_text(encoding='utf-8', errors='replace').splitlines():
        m = RX.search(linea)
        if not m:
            continue
        t, si, so, ins, f, pos, dif, scr, out, ev = m.groups()
        w = H(ins)
        sc = H(scr) if scr != '-' else []
        esp.scr = [(sc[i], sc[i + 1]) for i in range(0, len(sc), 2)]
        e = dict(seed=int(si, 16), glob=w[:10], estado=w[10], c74=w[11], krillin=w[12], dif=int(dif), f=H(f), pos=H(pos))
        o = esp.llamar(e)
        sale = [o['seed']] + o['glob'] + [o['estado'] & 0xFFFF, o['b60']]
        quiere = [int(so, 16)] + H(out)
        evs = ','.join('%X' % v for v in o['ev']) or '-'
        tot['llamadas'] += 1
        ok = sale == quiere and evs == ev
        tot['iguales'] += ok
        tot['estado2'] += w[10] == 2
        tot['entradas'] += evs != '-'
        if not ok:
            malas.append((t, quiere, sale, ev, evs))
    return tot, malas


def prueba_dorado1(cap):
    """N2: llamadas de la sonda cap_7d7 (PCSX2 con semilla; estados 1 y 2, con tiradas), entrada -> salida."""
    esp, tot, malas = Espejo(), Counter(), []
    s16 = lambda v: v - 0x10000 if v & 0x8000 else v
    for linea in Path(cap).open(encoding='utf-8'):
        r = json.loads(linea)
        if r['tabla'] != [f2b(KB48[0]), KB4C[0], KB50[0]]:   # la fila 0 leida en RAM = la del port (dificultad 0)
            tot['tabla_fila0_distinta'] += 1
        if r['nscr'] > 3:
            tot['fuera_mas_de_3_pantallas'] += 1
            continue
        gi = r['glob_in']
        e = dict(seed=r['seed_in'], glob=[gi[0], gi[1]] + gi[3:11], estado=r['estado'], c74=r['c74'], krillin=r['krillin'],
                 dif=r['ronda'], f=r['cam'][4:7], pos=r['pos'][:3], cam8=r['cam'], pos4=r['pos'], paso=r['paso'])
        esp.scr = [(r['scr'][2 * k], r['scr'][2 * k + 1]) for k in range(r['nscr'])]
        o = esp.llamar(e)
        go = r['glob_out']
        quiere = [r['seed_out']] + [go[0], go[1]] + go[3:11] + [s16(r['estado_out'] & 0xFFFF)]
        sale = [o['seed']] + o['glob'] + [o['estado']]
        clip_ok = (o['ev'][-1] == r['b60']) if o['ev'] else True
        ok = quiere == sale and clip_ok and len(o['scr']) == r['nscr']
        tot['llamadas'] += 1
        tot['iguales'] += ok
        tot['estado%d' % r['estado']] += 1
        tot['con_tirada'] += r['seed_in'] != r['seed_out']
        tot['cambios_estado'] += r['estado'] != s16(r['estado_out'] & 0xFFFF)
        tot['con_pantalla'] += r['nscr'] > 0
        if not ok:
            malas.append((r['i'], r['cnt'], quiere, sale, o['ev'], r['b60']))
    return tot, malas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('que', choices=['dorado', 'dorado1', 'juego'])
    ap.add_argument('--log')
    ap.add_argument('--cap', help='dorado1: llamadas.jsonl de cap_7d7.py')
    a = ap.parse_args()
    if a.que == 'dorado' and not CAP.exists():   # N2: captura eIW6 borrada con D:/tmp; dorado1 la cubre (estados 1 y 2)
        print('captura eIW6 ausente (D:/tmp borrado el 7/11): uso', CAP1)
        a.que = 'dorado1'
    tot, malas = (prueba_dorado() if a.que == 'dorado' else prueba_dorado1(a.cap or CAP1) if a.que == 'dorado1'
                  else prueba_juego(a.log))
    print(json.dumps(tot))
    for m in malas[:8]:
        print('DISTINTA', m)
    sys.exit(0 if tot['llamadas'] and tot['iguales'] == tot['llamadas'] else 1)


if __name__ == '__main__':
    main()

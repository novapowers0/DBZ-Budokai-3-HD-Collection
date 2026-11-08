# SPDX-License-Identifier: MIT
"""Fase 1 (7/11): lee en el ORIGINAL (PCSX2 del pool, solo lectura por PINE) el estado de la meta de los minijuegos de
campo (MSNMW): objeto oculto +0x16A4/+0x16A6, totales +0x165A/+0x165C, contadores A/B/C/D, meta +0x1630 frente a los
puntos 0 y 1 del #ITM, disparador +0x16BC y la mascara de bolas 0x5C1D78. G = *(0x49CDE8) (work de s_SPACE 0x31F280).
Uso: python tools/iw_mips/sonda_meta.py 0x7D0 [0x7D2 ...] [--pool 3]
"""
import argparse
import json
import struct
import subprocess
import sys
import time
from pathlib import Path

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI.parent))
import dif_grabar_pool as G  # noqa: E402

P_G, MASCARA, CNT = 0x49CDE8, 0x5C1D78, 0x4A4D40


def f32(w): return struct.unpack('<f', struct.pack('<I', w & 0xFFFFFFFF))[0]


def leer(p, g):
    rd = lambda a, n: p.leer(a, n)   # noqa: E731
    b = rd(g + 0x1620, 0xB0)
    o = lambda off: off - 0x1620   # noqa: E731
    u8 = lambda off: b[o(off)]   # noqa: E731
    s16 = lambda off: struct.unpack_from('<h', b, o(off))[0]   # noqa: E731
    v3 = lambda off: [round(x, 1) for x in struct.unpack_from('<3f', b, o(off))]   # noqa: E731
    itm = p.u32(g + 0x148)
    pt = lambda k: [round(x, 1) for x in struct.unpack('<3f', p.leer(itm + 0x20 + 16 * k, 12))]   # noqa: E731
    slot = u8(0x16A6)
    return dict(total_165A=s16(0x165A), cogidos_165C=s16(0x165C), oculto_16A4=u8(0x16A4), slot_16A6=slot,
                A=u8(0x166F), B=u8(0x1691), C=u8(0x16A3), D=u8(0x16B9), aros_166E=u8(0x166E), disparador_16BC=u8(0x16BC),
                meta_16BA=u8(0x16BA), meta_1630=v3(0x1630), punto0=pt(0), punto1=pt(1), oculto_pos=pt(90 + slot),
                mascara=p.u32(MASCARA) & 0xFF, itm_cab=[p.u32(itm + 4 * i) for i in range(1, 5)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('claves', nargs='+')
    ap.add_argument('--pool', type=int, default=3)
    ap.add_argument('--solo-ancho', action='store_true', help='solo el ancho de la UI (minijuegos de botones)')
    ap.add_argument('--sal', default=r'E:\DBZ3HD_trabajo\pendientes_0711\P1\sonda_meta.jsonl')
    a = ap.parse_args()
    pool = [sys.executable, str(AQUI.parent / 'pcsx2_pool.py')]
    import pcsx2_pool as PP
    import pcsx2_sonda as S
    for clave in a.claves:
        est = G.estado_por_defecto(clave)
        subprocess.run(pool + ['preparar', str(a.pool), '--estado', str(est)], check=True, timeout=120, capture_output=True)
        subprocess.run(pool + ['lanzar', str(a.pool), '--iso', G.ISO_EU, '--estado', str(est), '--ejecutar'], check=True,
                       timeout=120, capture_output=True)
        pid = G.pid_del_pool(a.pool)
        r = dict(clave=clave, estado=str(est))
        try:
            PP.registrar_en_sonda(S, a.pool)
            p, t = None, time.time()
            while time.time() - t < 300:
                try:
                    p = S.Pine(); x = p.u32(CNT); time.sleep(2)
                    if p.u32(CNT) != x:
                        break
                except Exception:   # noqa: BLE001
                    p = None; time.sleep(4)
            p.s.settimeout(30)
            muestras, t = [], time.time()
            while time.time() - t < 240:   # hasta que s_SPACE cargue los totales (+0x165A > 0) y unos segundos mas
                g = p.u32(P_G)
                if a.solo_ancho:
                    time.sleep(20)
                    break
                if 0x100000 <= g < 0x2000000 and struct.unpack('<h', p.leer(g + 0x165A, 2))[0] > 0:
                    muestras.append(leer(p, g))
                    if len(muestras) >= 3:
                        break
                    time.sleep(8)
                else:
                    time.sleep(1)
            r['muestras'] = muestras
            r['ancho_49C85C'] = f32(p.u32(0x49C85C))   # fRam0049c85c: ancho de la UI (ventanas de acierto de NB013/NB014)
            r['ancho_49C84C'] = f32(p.u32(0x49C84C))
        except Exception as ex:   # noqa: BLE001
            r['error'] = repr(ex)[:150]
        finally:
            if pid:
                subprocess.run(['taskkill', '/F', '/PID', str(pid)], capture_output=True); time.sleep(2)
            subprocess.run(pool + ['liberar', str(a.pool)], timeout=60, capture_output=True)
        print(json.dumps(r), flush=True)
        with open(a.sal, 'a', encoding='utf-8') as fo:
            fo.write(json.dumps(r) + chr(10))


if __name__ == '__main__':
    main()

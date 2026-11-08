# SPDX-License-Identifier: MIT
"""N2 (noche 6-7/11): sonda en anillo de la IA de persecucion s_len_3_2f (EU 0x31A940) en PCSX2 (plaza ya lanzada).

Misma IA que Bubbles (0x7D1); aqui sirve para cualquier minijuego que la use (0x7DD Piccolo persigue, ...).
La llamada 0x31B144 (jal s_len_3_2f, en Char_ComputePoseStateFromInput) pasa a un trampolin en 0xFA000 que guarda
los mismos campos que tools/iwx_bubbles_ia.py (semilla, estado, jugador, tarea, posiciones, mando del NPC) mas el
contador de campo, el actor y su indice, en un anillo de 128 registros de 128 B en 0xF0100 (contador en 0xF0000).
Ademas guarda el savestate al principio (para el espejo de bubbles.py, que necesita la RAM entera).
Uso: python tools/iw_mips/cap_ia.py N --salida <carpeta> [--seg 240] [--slot-estado 1] [--cargar 43]
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import cap_7d7 as K  # noqa: E402
from cap_7d7 import lw, lh, sw, sd, ld, addiu, andi, lui, sll, addu, jal, jr, cargar_abs, zero, a0, t0, t1, t2, t3, sp, ra  # noqa: E402

SITIO, IA = 0x31B144, 0x31A940
MGR, OBJ = 0x49CA70, 0x318400
s0 = 16
CAMPOS = ['seed_in', 'seed_out', 'estado', 'jugador', 'tarea_in', 'bx', 'by', 'bz', 'px', 'py', 'pz', 'dx', 'dz',
          'f200', 'f210', 'f214', 'f218', 'cnt', 'actor', 'idx', 'f318', 'f248', 'f180', 'f470', 'estado_out', 'f1f4', 'f1f8',
          'f200_in', 'f210_in', 'f214_in', 'f218_in', 'b248', 'b180']   # b*: la tabla base *(0x49CBC0) (s_len_3_2f +0x248/+0x180)   # N2: los indicadores antes de la llamada (quedan puestos de antes)
TREC = 0x100


def codigo():
    """Registro (palabras): ver CAMPOS. s0 = work del NPC (como en iwx_bubbles_ia), a0 = actor."""
    P = {k: 4 * i for i, k in enumerate(CAMPOS)}
    c = [addiu(sp, sp, -48), sd(ra, 0, sp), sd(a0, 8, sp),
         lui(t3, K.BASE >> 16), lw(t0, 0, t3), andi(t1, t0, 127), sll(t1, t1, 8), addu(t2, t1, t3),
         addiu(t2, t2, K.REC0 - K.BASE), sw(t2, 24, sp)]
    c += cargar_abs(t0, K.SEMILLA) + [sw(t0, P['seed_in'], t2)]
    c += [lh(t0, 0xCBC, a0), sw(t0, P['estado'], t2)]
    c += cargar_abs(t1, MGR) + [lh(t0, 0x14BC, t1), sw(t0, P['jugador'], t2), lw(t0, 0xB8, s0), sw(t0, P['tarea_in'], t2)]
    for k, (reg, off) in enumerate([(a0, 0x640), (a0, 0x644), (a0, 0x648), (t1, 0xE40), (t1, 0xE44), (t1, 0xE48),
                                    (s0, 0x50), (s0, 0x58)]):
        c += [lw(t0, off, reg), sw(t0, P['bx'] + 4 * k, t2)]
    c += cargar_abs(t0, K.CNT) + [sw(t0, P['cnt'], t2), sw(a0, P['actor'], t2), lw(t0, 0x1380, a0), sw(t0, P['idx'], t2)]
    for k, off in (('f318', 0x318), ('f248', 0x248), ('f180', 0x180), ('f200_in', 0x200), ('f210_in', 0x210),
                   ('f214_in', 0x214), ('f218_in', 0x218)):
        c += [lw(t0, off, s0), sw(t0, P[k], t2)]
    c += cargar_abs(t1, 0x49CBC0) + [lw(t0, 0x248, t1), sw(t0, P['b248'], t2), lw(t0, 0x180, t1), sw(t0, P['b180'], t2)]
    c += [jal(IA), 0, ld(a0, 8, sp), lw(t2, 24, sp)]
    c += cargar_abs(t0, K.SEMILLA) + [sw(t0, P['seed_out'], t2)]
    for k, off in (('f200', 0x200), ('f210', 0x210), ('f214', 0x214), ('f218', 0x218), ('f470', 0x470),
                   ('f1f4', 0x1F4), ('f1f8', 0x1F8)):
        c += [lw(t0, off, s0), sw(t0, P[k], t2)]
    c += [lh(t0, 0xCBC, a0), sw(t0, P['estado_out'], t2),
          lui(t3, K.BASE >> 16), lw(t0, 0, t3), addiu(t0, t0, 1), sw(t0, 0, t3),
          ld(ra, 0, sp), ld(a0, 8, sp), jr(ra), addiu(sp, sp, 48)]
    assert len(CAMPOS) * 4 <= TREC
    return c


# ---- 0x7DD: Piccolo_sCPUScript (EU 0x3638C0), llamada unica 0x363D44 dentro de MinigameHideNSeekPiccolo_sCPU
SITIO_7DD, IA_7DD, POBJ = 0x363D44, 0x3638C0, 0x49CBC0
CAMPOS_7DD = ['seed_in', 'seed_out', 'estado', 'jugador', 'cnt', 'actor', 'idx', 'ax', 'ay', 'az', 'tx', 'ty', 'tz',
              'w318', 'w404', 'w17c', 'w180', 'w4a0', 'w4ac', 'wb8', 'w1f4', 'w1f8', 'w4a0_out', 'w4ac_out', 'wb8_out',
              'w1f4_out', 'w1f8_out', 'w404_out', 'estado_t', 'work']
t5 = 13


def codigo_7dd():
    P = {k: 4 * i for i, k in enumerate(CAMPOS_7DD)}
    t4 = 12
    c = [addiu(sp, sp, -48), sd(ra, 0, sp), sd(a0, 8, sp),
         lui(t3, K.BASE >> 16), lw(t0, 0, t3), andi(t1, t0, 127), sll(t1, t1, 8), addu(t2, t1, t3),
         addiu(t2, t2, K.REC0 - K.BASE), sw(t2, 24, sp)]
    c += cargar_abs(t0, K.SEMILLA) + [sw(t0, P['seed_in'], t2), lh(t0, 0xCBC, a0), sw(t0, P['estado'], t2)]
    c += cargar_abs(t1, MGR) + [lh(t0, 0x14BC, t1), sw(t0, P['jugador'], t2)]
    c += cargar_abs(t0, K.CNT) + [sw(t0, P['cnt'], t2), sw(a0, P['actor'], t2), lw(t0, 0x1380, a0), sw(t0, P['idx'], t2)]
    c += [sll(t4, t0, 9), sll(t5, t0, 6), addu(t4, t4, t5), sll(t5, t0, 5), addu(t4, t4, t5)]
    c += cargar_abs(t1, POBJ) + [addu(t1, t1, t4), sw(t1, 32, sp), sw(t1, P['work'], t2)]
    for k, off in enumerate((0x640, 0x644, 0x648)):
        c += [lw(t0, off, a0), sw(t0, P['ax'] + 4 * k, t2)]
    c += [lw(t4, 0x1384, a0)]
    for k, off in enumerate((0x640, 0x644, 0x648)):
        c += [lw(t0, off, t4), sw(t0, P['tx'] + 4 * k, t2)]
    c += [lh(t0, 0xCBC, t4), sw(t0, P['estado_t'], t2)]
    for k in ('w318', 'w404', 'w17c', 'w180', 'w4a0', 'w4ac', 'wb8', 'w1f4', 'w1f8'):
        c += [lw(t0, int(k[1:], 16), t1), sw(t0, P[k], t2)]
    c += [jal(IA_7DD), 0, ld(a0, 8, sp), lw(t2, 24, sp), lw(t1, 32, sp)]
    c += cargar_abs(t0, K.SEMILLA) + [sw(t0, P['seed_out'], t2)]
    for k in ('w4a0', 'w4ac', 'wb8', 'w1f4', 'w1f8', 'w404'):
        c += [lw(t0, int(k[1:], 16), t1), sw(t0, P[k + '_out'], t2)]
    c += [lui(t3, K.BASE >> 16), lw(t0, 0, t3), addiu(t0, t0, 1), sw(t0, 0, t3),
          ld(ra, 0, sp), ld(a0, 8, sp), jr(ra), addiu(sp, sp, 48)]
    assert len(CAMPOS_7DD) * 4 <= TREC
    return c


def instalar(p, modo='bubbles'):
    sitio, ia = (SITIO_7DD, IA_7DD) if modo == '7dd' else (SITIO, IA)
    if p.u32(sitio) != jal(ia):
        raise RuntimeError('codigo EU distinto: %08X' % p.u32(sitio))
    if any(p.leer(K.BASE, 0x400)) or any(p.leer(K.CODE1, 0x800)):
        raise RuntimeError('el hueco 0xF0000/0xFA000 no esta a cero')
    c = codigo_7dd() if modo == '7dd' else codigo()
    p.escribir(K.BASE, bytes(K.REC0 - K.BASE + 128 * TREC))
    p.escribir(K.CODE1, struct.pack(f'<{len(c)}I', *c))
    p.escribir(sitio, struct.pack('<I', jal(K.CODE1)))
    return len(c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', type=int)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--seg', type=float, default=240)
    ap.add_argument('--cargar', type=int, help='slot de savestate a cargar antes (pool sstates)')
    ap.add_argument('--slot-estado', type=int, default=9, help='slot donde guardar la RAM al empezar')
    ap.add_argument('--seco', action='store_true')
    ap.add_argument('--modo', default='bubbles', choices=['bubbles', '7dd'])
    ap.add_argument('--guardar-tras', type=int, default=50, help='llamadas antes de guardar el savestate')
    a = ap.parse_args()
    if a.seco:
        print(len(codigo()), len(codigo_7dd()), 'palabras'); return
    import importlib.util
    import pcsx2_pool as PP
    out = Path(a.salida); out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('_sonda_n2b', K.R / 'pcsx2_sonda.py')
    s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
    PP.registrar_en_sonda(s, a.n)
    p = s.Pine(PP.puerto(a.n), espera=200)
    t1 = time.time()
    while time.time() - t1 < 150:
        try:
            if p.status() == 0 and p.u32(K.CNT):
                break
        except OSError as e:
            print('pine', e, flush=True)
        time.sleep(2)
    if a.cargar is not None:
        time.sleep(3); p.cargar(a.cargar); time.sleep(3)
        print('cargado', a.cargar, hex(p.u32(0x5C0DC8)), flush=True)
    time.sleep(2)
    campos = CAMPOS_7DD if a.modo == '7dd' else CAMPOS
    sitio, ia = (SITIO_7DD, IA_7DD) if a.modo == '7dd' else (SITIO, IA)
    t2 = time.time()
    while time.time() - t2 < 300:   # esperar a que el minijuego este cargado (la RAM del ELF de 0x7DD)
        try:
            if p.u32(MGR) and p.u32(sitio) == jal(ia):
                break
        except OSError:
            pass
        time.sleep(1)
    print('instalado', instalar(p, a.modo), flush=True)
    f = (out / 'ia.jsonl').open('w', encoding='utf-8')
    visto, t0, ultimo, guardado = 0, time.time(), 0, False
    try:
        while time.time() - t0 < a.seg:
            try:
                n = p.u32(K.BASE)
            except OSError:
                time.sleep(0.05); continue
            visto = max(visto, n - 120)
            while visto < n:
                b = p.leer(K.REC0 + (visto % 128) * TREC, 4 * len(campos))
                r = dict(zip(campos, struct.unpack(f'<{len(campos)}I', b)))
                r['i'] = visto
                f.write(json.dumps(r) + '\n')
                visto += 1
            if not guardado and n > a.guardar_tras:   # RAM entera con la sonda puesta (espejo de bubbles.py)
                p.guardar(a.slot_estado); guardado = True
                print('savestate', a.slot_estado, 'en la llamada', n, flush=True)
            if time.time() - ultimo > 10:
                ultimo = time.time(); f.flush(); print('t', round(time.time() - t0), 'llamadas', n, flush=True)
            time.sleep(0.1)
    finally:
        f.close()
        try:
            p.escribir(sitio, struct.pack('<I', jal(ia)))
        except OSError:
            pass
        print('fin', visto, flush=True)


if __name__ == '__main__':
    main()

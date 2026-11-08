# SPDX-License-Identifier: MIT
"""N2 (noche 6-7/11): dorado CON SEMILLA de la esquiva de Piccolo (IW EU 0x7D8, Viewer_UpdatePoseRotationInput 0x32DC80).

Sustituye a las capturas solIWX (D:/tmp, borradas el 7/11). Sonda en RAM como cap_7d7: la llamada 0x32D93C pasa a un
trampolin que copia la semilla, las 10 globales 0x5D9940.., estado, +0xC74, vida y dificultad del visor (*(0x49CDF0)
+4/+0x28), la posicion de Piccolo, el ojo y el objetivo de la camara, llama a la funcion original y copia la salida.
Anillo de 128 registros de 256 B en 0xF0100. Si el codigo del minijuego se carga encima, vuelve a poner el gancho.
Uso: python tools/iw_mips/cap_7d8.py N --salida <carpeta> [--seg 300] [--cargar 38]
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

SITIO, ROT = 0x32D93C, 0x32DC80
G, PVCTX = 0x5D9940, 0x49CDF0
CAMPOS = (['seed_in'] + ['g%d' % k for k in range(10)] + ['estado', 'c74', 'vida', 'dif', 'px', 'py', 'pz',
          'ox', 'oy', 'oz', 'tx', 'ty', 'tz', 'cnt', 'x'] + ['seed_out'] + ['go%d' % k for k in range(10)] +
          ['estado_out', 'b60'])


def codigo():
    P = {k: 4 * i for i, k in enumerate(CAMPOS)}
    c = [addiu(sp, sp, -48), sd(ra, 0, sp), sd(a0, 8, sp),
         lui(t3, K.BASE >> 16), lw(t0, 0, t3), andi(t1, t0, 127), sll(t1, t1, 8), addu(t2, t1, t3),
         addiu(t2, t2, K.REC0 - K.BASE), sw(t2, 24, sp)]
    c += cargar_abs(t0, K.SEMILLA) + [sw(t0, P['seed_in'], t2)]
    for k in range(10):
        c += cargar_abs(t0, G + 4 * k) + [sw(t0, P['g%d' % k], t2)]
    c += [lh(t0, 0xCBC, a0), sw(t0, P['estado'], t2), lw(t0, 0xC74, a0), sw(t0, P['c74'], t2)]
    c += cargar_abs(t1, PVCTX) + [lw(t0, 4, t1), sw(t0, P['vida'], t2), lw(t0, 0x28, t1), sw(t0, P['dif'], t2)]
    for k, nm in enumerate(('px', 'py', 'pz')):
        c += [lw(t0, 0x40 + 4 * k, a0), sw(t0, P[nm], t2)]
    c += cargar_abs(t1, K.PCAM)
    for k, nm in enumerate(('ox', 'oy', 'oz')):
        c += [lw(t0, 0x80 + 4 * k, t1), sw(t0, P[nm], t2)]
    for k, nm in enumerate(('tx', 'ty', 'tz')):
        c += [lw(t0, 0x90 + 4 * k, t1), sw(t0, P[nm], t2)]
    c += cargar_abs(t0, K.CNT) + [sw(t0, P['cnt'], t2), sw(a0, P['x'], t2)]
    c += [jal(ROT), 0, ld(a0, 8, sp), lw(t2, 24, sp)]
    c += cargar_abs(t0, K.SEMILLA) + [sw(t0, P['seed_out'], t2)]
    for k in range(10):
        c += cargar_abs(t0, G + 4 * k) + [sw(t0, P['go%d' % k], t2)]
    c += [lh(t0, 0xCBC, a0), sw(t0, P['estado_out'], t2), lw(t0, 0xB60, a0), sw(t0, P['b60'], t2),
          lui(t3, K.BASE >> 16), lw(t0, 0, t3), addiu(t0, t0, 1), sw(t0, 0, t3),
          ld(ra, 0, sp), ld(a0, 8, sp), jr(ra), addiu(sp, sp, 48)]
    assert len(CAMPOS) * 4 <= 0x100
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', type=int)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--seg', type=float, default=300)
    ap.add_argument('--cargar', type=int)
    ap.add_argument('--seco', action='store_true')
    a = ap.parse_args()
    c_ = codigo()
    if a.seco:
        print(len(c_), 'palabras'); return
    import importlib.util
    import pcsx2_pool as PP
    out = Path(a.salida); out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('_sonda_n2e', K.R / 'pcsx2_sonda.py')
    s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
    PP.registrar_en_sonda(s, a.n)
    p = s.Pine(PP.puerto(a.n), espera=200)
    t1_ = time.time()
    while time.time() - t1_ < 150:
        try:
            if p.status() == 0 and p.u32(K.CNT):
                break
        except OSError:
            pass
        time.sleep(2)
    if a.cargar is not None:
        time.sleep(3); p.cargar(a.cargar); time.sleep(3)
    if any(p.leer(K.BASE, 0x400)) or any(p.leer(K.CODE1, 0x400)):
        raise SystemExit('el hueco no esta a cero')
    p.escribir(K.BASE, bytes(K.REC0 - K.BASE + 128 * 0x100))
    print('hueco listo', flush=True)
    f = (out / 'llamadas.jsonl').open('w', encoding='utf-8')
    visto, t0_, ultimo, reparches = 0, time.time(), 0, 0
    try:
        while time.time() - t0_ < a.seg:
            try:
                if p.u32(SITIO) == jal(ROT):   # el gancho, cuando aparece el codigo original (overlay del minijuego)
                    if p.u32(K.CODE1) != c_[0]:
                        p.escribir(K.CODE1, struct.pack(f'<{len(c_)}I', *c_))
                    p.escribir(SITIO, struct.pack('<I', jal(K.CODE1))); reparches += 1
                n = p.u32(K.BASE)
            except OSError:
                time.sleep(0.05); continue
            if n - visto > 120:
                visto = n - 120
            while visto < n:
                r = dict(zip(CAMPOS, struct.unpack(f'<{len(CAMPOS)}I', p.leer(K.REC0 + (visto % 128) * 0x100, 4 * len(CAMPOS)))))
                r['i'] = visto
                f.write(json.dumps(r) + '\n')
                visto += 1
            if time.time() - ultimo > 10:
                ultimo = time.time(); f.flush(); print('t', round(time.time() - t0_), 'llamadas', n, 'reparches', reparches, flush=True)
            time.sleep(0.05)
    finally:
        f.close()
        try:
            p.escribir(SITIO, struct.pack('<I', jal(ROT)))
        except OSError:
            pass
        print('fin', visto, flush=True)


if __name__ == '__main__':
    main()

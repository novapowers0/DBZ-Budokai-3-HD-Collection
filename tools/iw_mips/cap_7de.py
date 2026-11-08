# SPDX-License-Identifier: MIT
"""N2 (noche 6-7/11): dorado de las marcas de Cell del 0x7DE (EU Char_InitTrailMarkerRandom 0x31D8F0) en PCSX2.

Las dos llamadas (0x31C960 en Char_EnterPoseState1Alt/DashTrail y 0x31E150 en el arranque) pasan a un trampolin en
0xFA000 que guarda la semilla de Memory antes y despues, el contador de campo, la direccion de retorno y la marca
entera (0x30 B) que deja la funcion original, en un anillo de 128 registros de 0x60 B en 0xF0100 (contador 0xF0000).
Solo copia valores: no cambia ninguna decision del juego.
Uso: python tools/iw_mips/cap_7de.py N --salida <carpeta> [--seg 300] [--cargar 44]
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import cap_7d7 as K  # noqa: E402
from cap_7d7 import lw, sw, sd, ld, addiu, andi, lui, sll, addu, jal, jr, cargar_abs, a0, t0, t1, t2, t3, sp, ra  # noqa: E402

SITIOS, FN = (0x31C960, 0x31E150), 0x31D8F0
TREC = 0x60
CAMPOS = ['seed_in', 'seed_out', 'cnt', 'ra', 'a0'] + ['m%d' % k for k in range(12)]


def codigo():
    c = [addiu(sp, sp, -48), sd(ra, 0, sp), sd(a0, 8, sp),
         lui(t3, K.BASE >> 16), lw(t0, 0, t3), andi(t1, t0, 127), sll(t1, t1, 5), addu(t2, t1, t1), addu(t2, t2, t1),
         addu(t2, t2, t3), addiu(t2, t2, K.REC0 - K.BASE), sw(t2, 24, sp)]      # t2 = REC0 + idx*0x60
    c += cargar_abs(t0, K.SEMILLA) + [sw(t0, 0, t2)] + cargar_abs(t0, K.CNT) + [sw(t0, 8, t2), sw(ra, 12, t2), sw(a0, 16, t2)]
    c += [jal(FN), 0, ld(a0, 8, sp), lw(t2, 24, sp)]
    c += cargar_abs(t0, K.SEMILLA) + [sw(t0, 4, t2)]
    for k in range(12):
        c += [lw(t0, 4 * k, a0), sw(t0, 20 + 4 * k, t2)]
    c += [lui(t3, K.BASE >> 16), lw(t0, 0, t3), addiu(t0, t0, 1), sw(t0, 0, t3),
          ld(ra, 0, sp), ld(a0, 8, sp), jr(ra), addiu(sp, sp, 48)]
    return c


def instalar(p):
    for s_ in SITIOS:
        if p.u32(s_) != jal(FN):
            raise RuntimeError('codigo EU distinto en %X: %08X' % (s_, p.u32(s_)))
    if any(p.leer(K.BASE, 0x400)) or any(p.leer(K.CODE1, 0x400)):
        raise RuntimeError('el hueco no esta a cero')
    c = codigo()
    p.escribir(K.BASE, bytes(K.REC0 - K.BASE + 128 * TREC))
    p.escribir(K.CODE1, struct.pack(f'<{len(c)}I', *c))
    for s_ in SITIOS:
        p.escribir(s_, struct.pack('<I', jal(K.CODE1)))
    return len(c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', type=int)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--seg', type=float, default=300)
    ap.add_argument('--cargar', type=int)
    ap.add_argument('--seco', action='store_true')
    ap.add_argument('--semilla-azar', action='store_true', help='tras cada recarga, semilla de Memory al azar')
    ap.add_argument('--recargar', type=int, default=0, help='volver a cargar el savestate tras cada tanda de 5 marcas')
    a = ap.parse_args()
    if a.seco:
        print(len(codigo()), 'palabras'); return
    import importlib.util
    import pcsx2_pool as PP
    out = Path(a.salida); out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('_sonda_n2d', K.R / 'pcsx2_sonda.py')
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
    p.escribir(K.BASE, bytes(K.REC0 - K.BASE + 128 * TREC))   # el gancho lo pone el bucle cuando aparece el jal original
    print('hueco listo; esperando el codigo del minijuego', flush=True)
    c_ = codigo()
    f = (out / 'marcas.jsonl').open('w', encoding='utf-8')
    visto, t0_, ultimo, reparches, recargas, tanda, t_nuevo = 0, time.time(), 0, 0, 0, 0, time.time()
    try:
        while time.time() - t0_ < a.seg:
            try:
                n = p.u32(K.BASE)
                # el minijuego carga su codigo encima (overlay): volver a poner el gancho en cuanto aparece el jal original
                for s_ in SITIOS:
                    if p.u32(s_) == jal(FN):
                        if p.u32(K.CODE1) != c_[0]:
                            p.escribir(K.CODE1, struct.pack(f'<{len(c_)}I', *c_))
                        p.escribir(s_, struct.pack('<I', jal(K.CODE1))); reparches += 1
                if a.recargar and n == tanda and tanda >= 5 and time.time() - t_nuevo > 15 and recargas < a.recargar:
                    p.cargar(a.cargar if a.cargar is not None else 44); recargas += 1   # la RAM vuelve al savestate: hueco a 0
                    print('recarga', recargas, 'marcas', visto, flush=True)
                    tanda, t_nuevo = 0, time.time()
                    time.sleep(1)
                    if a.semilla_azar:   # solo para variar el dorado: cada llamada se coteja entrada -> salida
                        import random
                        p.escribir(K.SEMILLA, struct.pack('<I', random.getrandbits(32) | 1))
                    continue
            except OSError:
                time.sleep(0.05); continue
            while tanda < n:
                b = p.leer(K.REC0 + (tanda % 128) * TREC, 4 * len(CAMPOS))
                tanda += 1; t_nuevo = time.time()
                r = dict(zip(CAMPOS, struct.unpack(f'<{len(CAMPOS)}I', b)))
                r['i'] = visto
                f.write(json.dumps(r) + '\n')
                visto += 1
            if time.time() - ultimo > 10:
                ultimo = time.time(); f.flush(); print('t', round(time.time() - t0_), 'marcas', n, 'reparches', reparches, flush=True)
            time.sleep(0.005)
    finally:
        f.close()
        try:
            for s_ in SITIOS:
                p.escribir(s_, struct.pack('<I', jal(FN)))
        except OSError:
            pass
        print('fin', visto, flush=True)


if __name__ == '__main__':
    main()

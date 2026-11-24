# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (8/11; region/cobertura 10/11): reejecuta en ee.py las parejas A->B que captura cap_mapa.py en PCSX2
(mandos del mapa GeneralMapScripts 0x2FE270 o Story_MapCameraUpdate 0x2F9280) y compara los campos de la logica (Goku,
camara, velocidad, direccion, reloj de reposo, casilla elegida, ficha de mision, cola de ordenes, estado de capsulas).
Cifra: parejas iguales / total, mas la COBERTURA de lo que las parejas ejercitan (A, L1, R1, correr, comandos de la cola
y Transition_SetupWithParams). Un 200/200 sin comandos 2/5/0x10 es un 200/200 que NO cubre el cambio de saga ni la esfera.

Region explicita (S3, 10/11): --region eu|usa. Si parejas.json trae "region" y no coincide, se rechaza la captura; una
captura EU no vale como USA solo por cambiar la region. Direcciones/ELF/estado se pueden sustituir con --elf, --p2s,
--pad y --fn (las que usa el savestate USA son las de MapaUsaIm). Uso:
  python mapa_pares.py <carpeta> [--region eu|usa] [--fn 2FE270] [--elf <SLES/SLUS>] [--p2s <savestate>] [--verbose]"""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import mapa_banco as MB  # noqa: E402

CAMPOS = [(0x1D0, 8), (0x29C, 8), (0x2A4, 8), (0x22C, 4), (0x234, 4), (0x238, 4), (0x330, 4), (0x10, 0x14), (0x28, 0x18)]
# Direcciones de las globales por region (las mismas de src/iw_mips_mapa.inl: MapaUsaIm).
REGION = {
    'eu': dict(fn=0x2FE270, M=0x49CDAC, ctx=0x49CDDC, paso=0x49C7DC, pad=0x4ADECC, cola=0x5D9220, cola_n=0x5D92A0,
               cola_arg=0x5D9260, estado=0x5C1554, guardado=0x5C1D7A),
    'usa': dict(fn=0x2FE060, M=0x49C58C, ctx=0x49C5BC, paso=0x49BFDC, pad=0x4AD6CC, cola=0x5D8A20, cola_n=0x5D8AA0,
                cola_arg=0x5D8A60, estado=0x5C0D54, guardado=0x5C157A),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('carpeta')
    ap.add_argument('--region', choices=('eu', 'usa'), default='eu')
    ap.add_argument('--fn')
    ap.add_argument('--elf')
    ap.add_argument('--p2s')
    ap.add_argument('--pad')
    ap.add_argument('--gp', help='GP del ELF (por defecto el EU 0x4A3DF0); con USA hace falta el suyo')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    if a.region == 'usa' and not (a.elf and a.p2s and a.gp):
        print('region usa: BLOQUEADO. No hay dorado USA del mapa (dsB2 lo dejo pendiente); una captura EU no vale.')
        print('  Para cuando exista: --region usa --elf <SLUS_218.42> --p2s <savestate USA> --gp <GP USA> '
              '[--pad 0x4AD6CC]')
        return 3
    reg = REGION[a.region]
    carp = Path(a.carpeta)
    meta = json.loads((carp / 'parejas.json').read_text())
    if meta.get('region') and meta['region'] != a.region:
        print('CAPTURA DE OTRA REGION: parejas.json dice %r y se pide %r; no se compara' % (meta['region'], a.region))
        return 2
    fn = int(a.fn, 16) if a.fn else int(meta.get('fn', reg['fn']))
    bl = {k: [tuple(x) for x in v] for k, v in meta['bloques'].items()}
    base = meta['base']
    M = int(meta.get('M', reg['M']))
    data = (carp / 'parejas.bin').read_bytes()
    if a.gp:
        MB.B.GP = int(a.gp, 16)
    if a.elf:
        MB.B.ELF = Path(a.elf)
    m = MB.Mapa(Path(a.p2s) if a.p2s else MB.P2S, elf=a.elf, pad=int(a.pad, 16) if a.pad else int(meta.get('pad', reg['pad'])))
    c = m.c
    m.M = M
    m.ctx = int(meta.get('ctx', reg['ctx']))
    m.pad_addr = int(a.pad, 16) if a.pad else int(meta.get('pad', reg['pad']))
    cola, cola_n, cola_arg = reg['cola'], reg['cola_n'], reg['cola_arg']
    tam = bl['B'][-1][2] + bl['B'][-1][1] - base
    ok = 0
    dif = []
    cob = {'a': 0, 'l1': 0, 'r1': 0, 'correr': 0, 'salida': 0}
    cmd = {}
    trans = []
    for k, pr in enumerate(meta['parejas']):
        d = data[pr['off']:pr['off'] + tam]
        for x, n, off in bl['A']:
            c.m[x:x + n] = d[off - base:off - base + n]
        m.ev.clear()
        c.call(fn, limite=5_000_000)
        pulsa = pr.get('pulsa', 0)
        cob['a'] += bool(pulsa & 0x4000)
        cob['l1'] += bool(pulsa & 0x400)
        cob['r1'] += bool(pulsa & 0x800)
        cob['correr'] += bool(pr.get('mant', 0) & 0x8000)
        n_cola = c.r32(cola_n)
        for j in range(min(n_cola, 16)):
            v = c.r32(cola + 4 * j)
            cmd[v] = cmd.get(v, 0) + 1
        for e in m.ev:
            if e[0].startswith('Transition_'):
                trans.append((k, e[1], e[2]))
        malo = []

        def cmp(addr, n, nombre):
            for x, nn, off in bl['B']:
                if x <= addr and addr + n <= x + nn:
                    cap = d[off - base + addr - x:off - base + addr - x + n]
                    mine = bytes(c.m[addr:addr + n])
                    if cap != mine:
                        malo.append((nombre, hex(addr), cap.hex(), mine.hex()))
                    return
        for o, n in CAMPOS:
            cmp(M + o, n, 'M+%x' % o)
        cmp(cola, 0x80, 'cola')
        cmp(cola_n, 4, 'cola_n')
        cmp(reg['estado'], 0x580, 'estado')
        cmp(reg['guardado'], 12, 'guardado')
        if malo:
            dif.append((k, pr['mant'], pr['lx'], malo[:3]))
        else:
            ok += 1
    total = len(meta['parejas'])
    print('region %s: parejas iguales %d/%d (fn %#x)' % (a.region, ok, total, fn))
    print('cobertura: A %d, L1 %d, R1 %d, correr %d; comandos %s; transiciones %d' %
          (cob['a'], cob['l1'], cob['r1'], cob['correr'], {hex(k): v for k, v in sorted(cmd.items())}, len(trans)))
    if cob['l1'] and 2 not in cmd and 5 not in cmd:
        print('  AVISO: L1 sin efecto (el estado de la captura no tiene otra area abierta: borde del rango)')
    if cob['r1'] and 2 not in cmd and 5 not in cmd:
        print('  AVISO: R1 sin efecto (el estado de la captura no tiene otra area abierta: borde del rango)')
    if cob['a'] and 9 not in cmd and 0x10 not in cmd:
        print('  AVISO: A sin efecto (ninguna pareja confirma una casilla)')
    if not cmd.get(0x10):
        print('  AVISO: sin esfera (comando 0x10): el dorado no cubre el flujo de la esfera; ver mapa_casos.py')
    for x in dif[:8]:
        print(' DIF', x)
    if a.verbose:
        for x in trans[:20]:
            print('  trans', x)
    return 0 if ok == total else 1


if __name__ == '__main__':
    sys.exit(main())

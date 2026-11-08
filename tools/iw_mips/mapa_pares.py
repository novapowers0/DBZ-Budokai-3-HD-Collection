# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (8/11): reejecuta en ee.py las parejas A->B que captura cap_mapa.py en PCSX2 (EU) para GeneralMapScripts 0x2FE270
y compara los campos de la logica (Goku, camara, velocidad, direccion, reloj de reposo, casilla elegida, ficha de mision,
cola de ordenes, estado de capsulas). Cifra: parejas iguales / total. Uso: python mapa_pares.py <carpeta> [--fn 2FE270]"""
import json, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import mapa_banco as MB

CAMPOS = [(0x1D0, 8), (0x29C, 8), (0x2A4, 8), (0x22C, 4), (0x234, 4), (0x238, 4), (0x330, 4), (0x10, 0x14), (0x28, 0x18)]

def main():
    carp = Path(sys.argv[1]); fn = int(sys.argv[sys.argv.index('--fn') + 1], 16) if '--fn' in sys.argv else MB.MANDOS
    meta = json.loads((carp / 'parejas.json').read_text())
    bl = {k: [tuple(x) for x in v] for k, v in meta['bloques'].items()}
    base = meta['base']; M = meta['M']
    data = (carp / 'parejas.bin').read_bytes()
    m = MB.Mapa(); c = m.c
    tam = bl['B'][-1][2] + bl['B'][-1][1] - base
    ok = 0; dif = []
    for k, pr in enumerate(meta['parejas']):
        d = data[pr['off']:pr['off'] + tam]
        for a, n, off in bl['A']:
            c.m[a:a + n] = d[off - base:off - base + n]
        m.ev.clear()
        c.call(fn, limite=5_000_000)
        malo = []
        def cmp(addr, n, nombre):
            for a, nn, off in bl['B']:
                if a <= addr and addr + n <= a + nn:
                    cap = d[off - base + addr - a:off - base + addr - a + n]
                    mine = bytes(c.m[addr:addr + n])
                    if cap != mine: malo.append((nombre, hex(addr), cap.hex(), mine.hex()))
                    return
        for o, n in CAMPOS: cmp(M + o, n, 'M+%x' % o)
        cmp(0x5D9220, 0x80, 'cola'); cmp(0x5D92A0, 4, 'cola_n'); cmp(0x5C1554, 0x580, 'estado'); cmp(0x5C1D7A, 12, 'guardado')
        if malo: dif.append((k, pr['mant'], pr['lx'], malo[:3]))
        else: ok += 1
    print('parejas iguales %d/%d (fn %#x)' % (ok, len(meta['parejas']), fn))
    for x in dif[:6]: print(' DIF', x)
    return 0 if ok == len(meta['parejas']) else 1
if __name__ == '__main__': sys.exit(main())

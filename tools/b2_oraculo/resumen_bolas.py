"""PM-21: grupo de capsula del resumen con <7 Bolas. ELF original (FUN_0029EDD0 en ee.py) vs el modelo del port (RankLider/RankGrupo)."""
import random, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mips_perfil import cargar
import ee

PERS = (0, 6, 4, 7, 28, 26, 27, 3, 5, 35, 24, 33, 21, 8)
CLASE = ((0, 4, 5, 10), (7, 6, 8, 1, 9, 14), (2, 3, 13, 12, 11))


def modelo(bolas, mask, a, b):
    m = mask | 1
    h = e0 = sh = None
    for i in range(14):
        if not m >> i & 1:
            continue
        s = a[i] + b[i]
        if h is None:
            h, e0, sh = i, a[i], s
        elif sh < s:
            h, sh = i, s
        elif sh == s and e0 < a[i]:
            h = i
    n = bin(bolas & 0x7F).count('1')
    clase = next((k for k, l in enumerate(CLASE) if h in l), -1)
    if clase < 0 or n < 2:
        return 1
    return clase + 0x21 if n < 5 else clase + 0x24 if n < 7 else 1


def original(c0, bolas, mask, a, b):
    c = ee.Ee(bytearray(c0.m0), c0.gp)
    got = []
    c.w8(0x6992DD, bolas); c.w16(0x6992DE, mask)
    for i in range(14):
        c.w8(0x6992E0 + i, a[i]); c.w8(0x6992EE + i, b[i])
    c.w32(c.gp - 0x7118, 1)   # iGpffff8ee8 != 0
    def mem(cpu):
        x, n = cpu.r[5] & 0xFF, cpu.r[6] & ee.M32
        s = cpu.r[4] & ee.M32; cpu.m[s:s + n] = bytes([x]) * n; cpu.r[2] = s
    c.hooks[0x123C18] = mem
    for ad in (0x245DD0, 0x2A44C0, 0x232E10):
        c.hooks[ad] = lambda cpu: setattr_r2(cpu, 0)
    def sorteo(cpu):
        got.append(cpu.r[4] & ee.M32); cpu.r[2] = 0
    c.hooks[0x2835F0] = sorteo
    c.r[4] = 0x1F00000
    c.r[28], c.r[29], c.r[31] = c.gp, ee.PILA + 0xFF000, ee.RETORNO
    c.run(0x29EDD0, 400000)
    return got[0] if got else None


def setattr_r2(cpu, v):
    cpu.r[2] = v


if __name__ == '__main__':
    c, _ = cargar('b2', None)
    c.m0 = bytes(c.m)
    rnd = random.Random(2211)
    malos = ok = 0
    for k in range(400):
        bolas = rnd.choice([x for x in range(0x7F)])
        mask = rnd.getrandbits(14)
        a = [rnd.choice([0, 0, 1, 2, 3, 5]) for _ in range(14)]
        b = [rnd.choice([0, 0, 1, 2]) for _ in range(14)]
        o, m = original(c, bolas, mask, a, b), modelo(bolas, mask, a, b)
        if o == m:
            ok += 1
        else:
            malos += 1
            if malos < 6:
                print('DIFIERE', hex(bolas), hex(mask), a, b, 'original', o, 'modelo', m)
    print(f'{ok} iguales, {malos} distintos de 400')
    sys.exit(1 if malos else 0)

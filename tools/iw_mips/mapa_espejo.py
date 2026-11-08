# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (8/11): espejo DESDE CERO del mapa de Dragon Mission (como lo hara src/iw_mips_mapa.inl): imagen = ELF +
#DMD (DATA_CMN 1093, hijo '#DMD' del #AMB) reubicado + #SCD de capsulas (DATA_CMN 1127) + punteros globales. Sin savestate.
`python mapa_espejo.py` compara la aparicion de casillas y el movimiento con el mismo escenario sobre el savestate del
PCSX2 (mapa_banco.Mapa)."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import bubbles as B  # noqa: E402
import ee  # noqa: E402
import mapa_banco as MB  # noqa: E402

CMN = Path(__file__).resolve().parents[2] / 'ps2_games/Infinite World (USA)/USR/DATA_CMN.AFS'
# espejo propio (por encima de los de los minijuegos, que acaban en 0x1190000+0x2000)
M_ = 0x11A0000        # gestor del mapa (0x600 B)
OBJ0 = 0x11A1000      # objeto de estado M[0] (+0x30 contador, +0x34 acumulador, +0x1C manejador)
ASSET = 0x11A2000     # recurso de capsulas (+0x54 = datos #SCD)
CTX = 0x11A4000       # 0x80 nodos de 0x30 B + zona de parametros de sprites (hasta +0x1820)
DMD_ = 0x11B0000
SCD_ = 0x11E0000
CAPS = 0x5CB830       # _RAMAllCapsules (EU)
ESTADO = 0x5C1490 + 0xC4   # bytes de estado por capsula (0x580)
# camara del mapa (M+0x188 = 45 grados es la que usa la proyeccion de Story_PickObjectAtCursor) y velocidad de Goku
CAMARA_INI = {0x188: 0x42340000, 0x18C: 0x3F800000, 0x19C: 0x3F800000, 0x1AC: 0x3F800000, 0x1B4: 0x3F1C61AB, 0x224: 0x3F800000,
              0x228: 0x3E23D70A, 0x230: 0x3F800000, 0x234: 0x40000000}
# parametros de pantalla que deja el arranque (EU, medidos en el savestate del mapa): 512x448 reales y 600x450 virtuales
PANTALLA = {0x49C824: 1, 0x49C82C: 0x44000000, 0x49C830: 0x43E00000, 0x49C834: 0x200, 0x49C838: 0x1C0, 0x49C83C: 0x200,
            0x49C840: 0x200, 0x49C844: 0x200, 0x49C848: 0x200, 0x49C84C: 0x44160000, 0x49C850: 0x43E10000,
            0x49C854: 0x3F5A740E, 0x49C858: 0x3F7EDCBB, 0x49C85C: 0x44160000, 0x49C860: 0x43E10000}


def entrada_afs(i, afs=CMN):
    with open(afs, 'rb') as f:
        n = struct.unpack('<I', f.read(8)[4:8])[0]
        f.seek(8 + 8 * i)
        o, s = struct.unpack('<II', f.read(8))
        f.seek(o)
        return f.read(s)


def hijo_amb(amb, c):
    if amb[:4] != b'#AMB' or c >= struct.unpack_from('<I', amb, 0x10)[0]:
        return b''
    t = struct.unpack_from('<I', amb, 0x14)[0]
    o, sz = struct.unpack_from('<II', amb, t + 16 * c)
    return amb[o:o + sz]


def dmd_de(entrada):
    for c in range(32):
        h = hijo_amb(entrada, c)
        if h[:4] == b'#DMD':
            return h
    return b''


def imagen_vacia():
    d = B.ELF.read_bytes()
    mem = bytearray(ee.TOTAL)
    phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
    for i in range(phnum):
        tipo, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
        if tipo == 1 and fsz:
            mem[va:va + fsz] = d[off:off + fsz]
    return mem


class Espejo(MB.Mapa):
    def __init__(s, saga=0, hechas=(), road=False, goku=None, dmd=None, scd=None):
        s.nom = B.leer_simbolos()
        mem = imagen_vacia()
        s.base = mem
        s.c = ee.Ee(mem, B.GP, entradas=s.nom.keys(), nombres=s.nom)
        s.ev = []
        for t, n in s.nom.items():
            if n.startswith(MB.PUL):
                s.c.hooks[t] = (lambda n: lambda cpu: (s.ev.append((n, cpu.r[4] & 0xFFFFFFFF, cpu.r[5] & 0xFFFFFFFF)),
                                                       cpu.r.__setitem__(2, 0)))(n)
        s.poner_mudos()
        c = s.c
        s.M, s.ctx = M_, CTX
        dmd = dmd or dmd_de(entrada_afs(1092 if road else 1093))
        scd = scd or entrada_afs(1127)
        # #DMD reubicado: '#' -> '!' y la tabla de punteros (+0x10) pasa a absoluta
        b = bytearray(dmd)
        b[0] = 0x21
        for k in range(struct.unpack_from('<I', b, 8)[0]):
            struct.pack_into('<I', b, 0x10 + 4 * k, struct.unpack_from('<I', b, 0x10 + 4 * k)[0] + DMD_)
        mem[DMD_:DMD_ + len(b)] = b
        mem[SCD_:SCD_ + len(scd)] = scd
        c.w32(MB.G_M, M_); c.w32(MB.G_CTX, CTX); c.w32(MB.G_PASO, 0x3F99999A)
        for a, v in PANTALLA.items(): c.w32(a, v)
        c.w32(M_, OBJ0)
        for a, v in CAMARA_INI.items(): c.w32(M_ + a, v)
        c.w32(M_ + (0x128 if road else 0x118), DMD_)
        c.w32(M_ + 0x4AC, 1 if road else 0)
        c.w32(M_ + 0x398, saga); c.w32(M_ + 0x394, saga)
        c.w32(M_ + 0x20, 0xFFFFFFFF); c.w32(M_ + 0x24, 0xFFFFFFFF)
        # capsulas: Capsule_LoadAndInitData(recurso) con las tablas globales como las deja el arranque
        for k, v in enumerate((1, 0x5C5510, 0x5C6010, 0x5C6030)): c.w32(CAPS + 4 * k, v)
        c.w32(ASSET + 0x54, SCD_)
        c.call(0x2AB8E0, ASSET, limite=5_000_000)
        s.hechas(hechas)
        if goku:
            s.poner_goku(*goku)

    def clave_idx(s, clave):
        return s.c.call(0x2AB730, clave, limite=1_000_000) & 0xFFFF

    def marca(s, clave, mascara):
        """byte de estado de la capsula `clave` |= mascara (el port traduce su progreso a estas marcas)."""
        s.c.m[ESTADO + s.clave_idx(clave)] |= mascara

    def hechas(s, claves):
        """Claves de capsula (DMD +0x20) completadas: |= 0xE0 y Capsule_UpdateUnlockFlagsAt0xC4(0x5C1490)."""
        c = s.c
        for k in claves:
            i = s.clave_idx(k)
            c.m[ESTADO + i] |= 0xE0
        c.call(0x2AB590, 0x5C1490, limite=5_000_000)

    def poner_goku(s, x, y):
        s.c.m[s.M + 0x1D0:s.M + 0x1D8] = struct.pack('<ff', x, y)
        s.c.m[s.M + 0x29C:s.M + 0x2A4] = struct.pack('<ff', x, y)


def comparar():
    import mapa_banco
    oro = mapa_banco.Mapa()
    k0 = oro.aparecer()
    sp = Espejo(goku=struct.unpack_from('<ff', oro.base, oro.M + 0x1D0))
    k1 = sp.aparecer()
    a, b = oro.nodos(), sp.nodos()
    print('casillas oro %d espejo %d, cuadros %d/%d' % (len(a), len(b), k0, k1))
    for g, h in zip(a, b):
        if g != h:
            print('G', g, '\nH', h)
    return a == b


if __name__ == '__main__':
    print('IGUAL' if comparar() else 'DISTINTO')



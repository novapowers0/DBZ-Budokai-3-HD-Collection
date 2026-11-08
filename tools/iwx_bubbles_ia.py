"""eIW6: sonda de la IA de Bubbles (EU s_len_3_2f 0x31A940) en el PCSX2 original.

Solo para capturas con --bubbles-ia. Reescribe en RAM (no en la ISO) el cuerpo de
FUN_00127DE0 (printf de depuracion; sus dos llamadas se anulan)
con un trampolin que guarda, en 0xF0000, la semilla del LFSR y
las entradas de la IA al entrar, llama a la IA y guarda semilla y flags al salir.
La llamada 0x31B144 (Char_ComputePoseStateFromInput) pasa a ir al trampolin.
No cambia ninguna decision del juego: solo copia valores y se salta un printf.
"""
import struct

B = 0xF0000  # hueco entre kernel y ELF (0x100000); la rejilla 0x4A9190 se borra cada cuadro (probado 6/11)
TRAMPOLIN = 0x127DE0
LLAMADA = 0x31B144
PRINTF = (0x2AA714, 0x31AA84)
IA = 0x31A940
ORIGINAL_LLAMADA = (3 << 26) | (IA >> 2)
ORIGINAL_PRINTF = (3 << 26) | (TRAMPOLIN >> 2)
PROLOGO = 0x27BDFE70  # addiu sp,sp,-0x190 de FUN_00127DE0

zero, t0, t1, t2, s0, gp, sp, ra, a0 = 0, 8, 9, 10, 16, 28, 29, 31, 4


def i(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def lw(rt, off, rs): return i(0x23, rs, rt, off)
def lh(rt, off, rs): return i(0x21, rs, rt, off)
def sw(rt, off, rs): return i(0x2B, rs, rt, off)
def sd(rt, off, rs): return i(0x3F, rs, rt, off)
def ld(rt, off, rs): return i(0x37, rs, rt, off)
def addiu(rt, rs, imm): return i(9, rs, rt, imm)
def lui(rt, imm): return i(0xF, 0, rt, imm)


def campo(k):  # desplazamiento del campo k (palabra) respecto a lui 0x4B
    return (B & 0xFFFF) + 4 * k


CAMPOS = ['seed_in', 'seed_out', 'cuenta', 'estado', 'jugador', 'tarea_in',
          'bx', 'by', 'bz', 'px', 'py', 'pz', 'dx', 'dz',
          'f200', 'f210', 'f214', 'f218']
SEGUNDO = 0x127E88  # FUN_00127E80 es un printf vacio: queda en jr ra; nop y su resto aloja la cola


def codigo():
    c = [addiu(sp, sp, -16), sd(ra, 0, sp), lui(t1, B >> 16),
         lw(t0, -0x7FE8, gp), sw(t0, campo(0), t1),
         lh(t0, 0xCBC, a0), sw(t0, campo(3), t1),
         lw(t2, -0x7380, gp), lh(t0, 0x14BC, t2), sw(t0, campo(4), t1),
         lw(t0, 0xB8, s0), sw(t0, campo(5), t1)]
    for k, (reg, off) in enumerate([(a0, 0x640), (a0, 0x644), (a0, 0x648), (t2, 0xE40), (t2, 0xE44), (t2, 0xE48),
                                    (s0, 0x50), (s0, 0x58)]):
        c += [lw(t0, off, reg), sw(t0, campo(6 + k), t1)]
    c += [(3 << 26) | (IA >> 2), 0, lui(t1, B >> 16), lw(t0, -0x7FE8, gp), sw(t0, campo(1), t1)]
    for k, off in enumerate([0x200, 0x210, 0x214, 0x218]):
        c += [lw(t0, off, s0), sw(t0, campo(14 + k), t1)]
    c += [lw(t0, campo(2), t1), addiu(t0, t0, 1), sw(t0, campo(2), t1), ld(ra, 0, sp), (ra << 21) | 8, addiu(sp, sp, 16)]
    return c


def bloques():
    c = codigo()
    a = c[:38] + [(2 << 26) | (SEGUNDO >> 2), 0]
    b = c[38:]
    if len(b) > (0x127EB0 - SEGUNDO) // 4:
        raise RuntimeError('la cola del trampolin no cabe')
    return a, b


def instalar(p):
    """p: cliente PINE (u32/leer/escribir). Comprueba los bytes originales antes de escribir."""
    a, b = bloques()
    if p.u32(LLAMADA) != ORIGINAL_LLAMADA or any(p.u32(x) != ORIGINAL_PRINTF for x in PRINTF) or             p.u32(TRAMPOLIN) != PROLOGO or p.u32(0x127E80) != 0x27BDFF80 or p.u32(0x127EA4) != 0x03E00008:
        raise RuntimeError('codigo EU distinto del esperado: no se parchea')
    if any(p.leer(B, 0x400)):
        raise RuntimeError('el hueco 0xF0000 no esta a cero: no se usa')
    p.escribir(B, bytes(4 * len(CAMPOS)))
    p.escribir(0x127E80, struct.pack('<2I', 0x03E00008, 0))
    p.escribir(SEGUNDO, struct.pack(f'<{len(b)}I', *b))
    p.escribir(TRAMPOLIN, struct.pack(f'<{len(a)}I', *a))
    for x in PRINTF:
        p.escribir(x, struct.pack('<I', 0))
    p.escribir(LLAMADA, struct.pack('<I', (3 << 26) | (TRAMPOLIN >> 2)))
    return len(a) + len(b)


def leer(read):
    v = struct.unpack(f'<{len(CAMPOS)}I', read(B, 4 * len(CAMPOS)))
    d = dict(zip(CAMPOS, v))
    d['ajeno'] = any(read(B + 4 * len(CAMPOS), 0x100))  # otro escritor en el hueco: invalida la fila
    for k in ('bx', 'by', 'bz', 'px', 'py', 'pz', 'dx', 'dz'):
        d[k] = struct.unpack('<f', struct.pack('<I', d[k]))[0]
    for k in ('estado', 'jugador'):
        d[k] = struct.unpack('<i', struct.pack('<I', d[k]))[0]
    return d


if __name__ == '__main__':
    a, b = bloques()
    print(len(a), len(b), 'palabras')
    assert len(a) == 40 and len(b) <= 10
    assert a[0] == 0x27BDFFF0 and b[-3] == 0xDFBF0000 and b[-2] == 0x03E00008

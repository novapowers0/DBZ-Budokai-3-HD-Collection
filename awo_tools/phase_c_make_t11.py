#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_c_make_t11.py - Test T11: permutar las VENTANAS del GPU vertex buffer.

Hecho verificado (docs/07_ports/SESION_GPU_DRAW_2026-09-11.md §6-7):
  - El GPU vertex buffer es una copia VERBATIM de una region contigua del AWO:
        vb0 .. ib  (N ventanas de 44 B),  vb0 = ib - N*44,  N = max(IB)+1
  - Cada ventana w es AUTOCONTENIDA para el shader:
        pos.xyz @+0 | w @+12 | bone @+16 | nrm.xyz @+20 | FFFFFFFF @+32 | uv @+36
  - El IB del fichero referencia las ventanas (indices 0..N-1).
  => Permutar las VENTANAS + remapear el IB = IDENTIDAD (geometria + textura).

El error de T10 fue permutar los "registros del tool" (rejilla `sec+2`, desalineada
+428 B respecto a las ventanas), lo que desplazaba el UV. T11 trabaja en la rejilla
correcta del GPU.

Permutacion por defecto: REVERSE de todas las ventanas (maxima). Tambien soporta
--swap I J.

Uso: python phase_c_make_t11.py <in.bin> <out.bin> [--swap I J]
"""
import struct
import sys

def be32(b, o): return struct.unpack(">I", b[o:o + 4])[0]
def be16(b, o): return struct.unpack(">H", b[o:o + 2])[0]

def main():
    path, out = sys.argv[1], sys.argv[2]
    b = bytearray(open(path, "rb").read())
    awo = 0x40
    tbl = awo + be32(b, awo + 0x1C)
    awg0 = awo + be32(b, tbl)
    def g(o): return be32(b, awg0 + o)
    ib_a = awg0 + g(0x30)
    n_ib = (g(0x38) - g(0x30)) // 2
    IB = [be16(b, ib_a + k * 2) for k in range(n_ib)]
    maxv = max(v for v in IB if v != 0xFFFF)
    N = maxv + 1
    vb0 = ib_a - N * 44
    print("N ventanas=%d  vb0=%d (awg0+%d)  ib=%d" % (N, vb0, vb0 - awg0, ib_a))

    orig = bytes(b)
    # comprobar que vb0 apunta a un marker (consistencia)
    if orig[vb0 + 32:vb0 + 36] != b"\xff\xff\xff\xff":
        print("AVISO: vb0+32 no es FFFFFFFF (inyeccion de marker de ventana)")
    if orig[vb0:vb0 + 4] == b"\xff\xff\xff\xff":
        print("AVISO: vb0+0 es FFFFFFFF")

    if "--swap" in sys.argv:
        i = sys.argv.index("--swap")
        a, c = int(sys.argv[i + 1]), int(sys.argv[i + 2])
        perm = list(range(N)); perm[a], perm[c] = c, a
        kind = "SWAP windows %d<->%d" % (a, c)
    else:
        perm = [N - 1 - w for w in range(N)]
        kind = "REVERSE windows"

    # escribir ventanas permutadas (pos/w/bone/nrm/marker/uv viajan juntas)
    new = bytearray(N * 44)
    for w in range(N):
        s = vb0 + perm[w] * 44
        new[w * 44:w * 44 + 44] = orig[s:s + 44]
    b[vb0:vb0 + N * 44] = new

    # remapear IB en el espacio de ventanas
    rem = 0
    for k in range(n_ib):
        v = be16(b, ib_a + k * 2)
        if v == 0xFFFF:
            continue
        struct.pack_into(">H", b, ib_a + k * 2, perm[v])
        rem += 1
    open(out, "wb").write(bytes(b))
    print("T11 %s: IB remapeados=%d" % (kind, rem))

    # validacion: cada ventana resuelta por el IB debe ser identica a la original
    b2 = open(out, "rb").read()
    same = True; checked = 0
    for k in range(n_ib):
        v1 = be16(orig, ib_a + k * 2)
        v2 = be16(b2, ib_a + k * 2)
        if v1 == 0xFFFF or v2 == 0xFFFF:
            continue
        checked += 1
        if orig[vb0 + v1 * 44:vb0 + v1 * 44 + 44] != b2[vb0 + v2 * 44:vb0 + v2 * 44 + 44]:
            same = False; break
    print("validacion (ventana resuelta identica):", "OK" if same else "FALLO",
          "(revisados %d)" % checked)
    print("escrito:", out)
    return 0 if same else 1

if __name__ == "__main__":
    sys.exit(main())

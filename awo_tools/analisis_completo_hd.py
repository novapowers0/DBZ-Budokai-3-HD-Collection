"""analisis_completo_hd.py - Mapeo exhaustivo del bin HD B3 (#AMB -> #AWO + #AZT).

Documenta TODA la estructura del bin para poder reconstruirlo desde un PS2.
Uso: python analisis_completo_hd.py <bin_hd>
"""
import struct
import sys

d = open(sys.argv[1], 'rb').read()


def be32(off):
    return struct.unpack('>I', d[off:off + 4])[0]


def be16(off):
    return struct.unpack('>H', d[off:off + 2])[0]


def fbe(off):
    return struct.unpack('>f', d[off:off + 4])[0]


print('=' * 70)
print('ANALISIS COMPLETO BIN HD B3: %s (%d bytes)' % (sys.argv[1], len(d)))
print('=' * 70)

# ---- Contenedor #AMB ----
print('\n[1] CONTENEDOR #AMB')
print('  magic[0:5]:', d[0:5])
# header AMB
print('  +8 count? :', struct.unpack('>I', d[8:12])[0])
print('  +0x10:     :', hex(struct.unpack('>I', d[0x10:0x14])[0]))
print('  +0x14:     :', hex(struct.unpack('>I', d[0x14:0x18])[0]))
print('  +0x18:     :', hex(struct.unpack('>I', d[0x18:0x1C])[0]))
print('  +0x20 tabla:', hex(struct.unpack('>I', d[0x20:0x24])[0]))

# localizar #AWO y #AZT
awo = az = None
for i in range(len(d) - 4):
    if d[i:i + 4] == b'#AWO' and awo is None:
        awo = i
    if d[i:i + 4] == b'#AZT' and az is None:
        az = i
print('  #AWO @ 0x%X  #AZT @ 0x%X' % (awo, az))
if az:
    print('  #AZT size: %d' % (len(d) - az))

# ---- AWO header ----
print('\n[2] AWO HEADER (@0x%X, rel al AWO)' % awo)


def ar(rel):
    return struct.unpack('>I', d[awo + rel:awo + rel + 4])[0]


print('  magic:', d[awo:awo + 8])
for off in [0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34]:
    print('  +%02X: %08X' % (off, ar(off)))
n_awg = ar(0x18)
awg_tbl = ar(0x1C)
labels_off = ar(0x24)
bones_tbl = ar(0x34)
print('  n_awg=%d, awg_tbl=0x%X, labels=0x%X, bones_tbl=0x%X' % (n_awg, awg_tbl, labels_off, bones_tbl))

# labels del AWO
print('\n[3] LABELS AWO (@0x%X)' % labels_off)
for bi in range(min(8, n_awg)):
    s = d[awo + labels_off + bi * 16:awo + labels_off + bi * 16 + 16].split(b'\x00')[0]
    print('  bone %2d: %s' % (bi, s))

# ---- Tabla AWG ----
print('\n[4] TABLA AWG (@0x%X, %d entradas)' % (awg_tbl, n_awg))
awg_offs = [ar(awg_tbl + i * 4) for i in range(n_awg)]
for i, rel in enumerate(awg_offs):
    abs_ = awo + rel
    print('  AWG%-2d rel 0x%X abs 0x%X magic %s' % (i, rel, abs_, d[abs_:abs_ + 5]))

# ---- Analizar cada AWG ----
print('\n[5] ESTRUCTURA DE CADA AWG')
for gi, rel in enumerate(awg_offs):
    AWG = awo + rel
    if d[AWG:AWG + 4] != b'#AWG':
        continue
    n_bones = be32(AWG + 0x10)
    axes_rel = be32(AWG + 0x14)
    n_groups = be32(AWG + 0x18)
    sec_rel = be32(AWG + 0x34)
    vb2_rel = be32(AWG + 0x2C)
    ib_rel = be32(AWG + 0x30)
    end_rel = be32(AWG + 0x38)
    sec_size = vb2_rel - sec_rel
    ib_size = end_rel - ib_rel
    vb2_size = ib_rel - vb2_rel
    print('  AWG%d @0x%X: n_bones=%d axes@0x%X groups=%d' % (gi, AWG, n_bones, axes_rel, n_groups))
    print('    sec34@0x%X(%d verts) vb2@0x%X(%d) ib@0x%X(%d idx) end@0x%X' % (
        AWG + sec_rel, sec_size // 44, AWG + vb2_rel, vb2_size // 44, AWG + ib_rel, ib_size // 2, AWG + end_rel))
    # labels del AWG
    lbl = d[AWG + 0x40:AWG + 0x40 + 16].split(b'\x00')[0]
    print('    label[0]: %s' % lbl)

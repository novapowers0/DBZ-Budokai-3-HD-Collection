# -*- coding: utf-8 -*-
"""
analyze_bin_hd.py — Analizar un bin HD (#AWO/#AWG) con la template
B3_AMB_PS3.bt (010 Editor, del Discord de la comunidad).

Usa los offsets REALES de la template:
AWO:  +0x10 numberOfBones, +0x14 ptrtoConnections, +0x18 numberOfAWGs,
      +0x1C pointerAWGoffsets, +0x24 ptrBoneNames, +0x30 AWOunk[bones](32B)
      + tabla AWGptr[awgs](4B) + BoneNames[bones](32B)
AWG:  +0x10 numberOfBones, +0x14 rigging_data_ptr, +0x1C ptrBones,
      +0x24 unk_Count(80B blocks), +0x28 ptrVertexBlock, +0x2C VertexBlockSize,
      +0x30 ptrFaceData, +0x34 FaceDataSize, +0x38 unk_ptr_28, +0x3C sizeOfunk_ptr_28

Uso:
  python analyze_bin_hd.py <bin> [--dump] [--awg N] [--sec34]
"""

import sys, io, struct
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

def u32be(b, o): return struct.unpack('>I', b[o:o+4])[0]
def u16be(b, o): return struct.unpack('>H', b[o:o+2])[0]
def f32be(b, o): return struct.unpack('>f', b[o:o+4])[0]

def find_magic(d, magic):
    i = d.find(magic)
    return i

def analyze(d):
    awo = d.find(b'#AWO')
    if awo < 0:
        print('Sin #AWO. magics:', [m.decode() for m in __import__('re').findall(rb'#[A-Z0-9]{3}', d[:0x400])])
        return None
    print('=== AMB ===')
    if d[:4] == b'#AMB':
        n = u32be(d, 0x0C)
        print('  AMB entradas: %d' % n)
        for i in range(n):
            loc, sz = u32be(d, 0x20+i*16), u32be(d, 0x24+i*16)
            print('    entry%d: loc=0x%X size=%d magic=%s' % (i, loc, sz, d[loc:loc+4]))
    print('=== AWO @0x%X ===' % awo)
    nb = u32be(d, awo+0x10)
    n_awg = u32be(d, awo+0x18)
    tbl = u32be(d, awo+0x1C)
    lbl = u32be(d, awo+0x24)
    conn = u32be(d, awo+0x14)
    print('  bones=%d awgs=%d tabla=0x%X labels=0x%X conn=0x%X' % (nb, n_awg, tbl, lbl, conn))
    # labels
    labels = {}
    for bi in range(min(nb, 70)):
        s = d[awo+lbl+bi*32:awo+lbl+bi*32+32]
        s = s.split(b'\x00')[0].decode('latin1', errors='replace')
        if s: labels[bi] = s
    print('  labels[0..5]:', [labels.get(i,'?') for i in range(min(6,nb))])
    # AWG offsets
    awg_offs = [u32be(d, awo+tbl+i*4) for i in range(n_awg)]
    print('  AWG offsets:', ['0x%X'%o for o in awg_offs[:6]], '...' if n_awg>6 else '')
    return awo, awg_offs, labels

def analyze_awg(d, awo, off):
    h = awo + off
    magic = d[h:h+4]
    nb = u32be(d, h+0x10)
    rgd = u32be(d, h+0x14)
    pbn = u32be(d, h+0x1C)
    nk = u32be(d, h+0x24)
    pvb = u32be(d, h+0x28); vbs = u32be(d, h+0x2C)
    pfc = u32be(d, h+0x30); fcs = u32be(d, h+0x34)
    pib = u32be(d, h+0x38); ibc = u32be(d, h+0x3C)
    lbl = d[h+pbn:h+pbn+32].split(b'\x00')[0].decode('latin1', errors='replace')
    n_vb = vbs//44; n_face = fcs//44
    print('  AWG @0x%X magic=%s label=%r nb=%d nk=%d vb=%d face=%d ib_cnt=%d' % (
        off, magic, lbl, nb, nk, n_vb, n_face, ibc))
    return (h, pvb, vbs, n_vb, pfc, fcs, n_face, pib, ibc, lbl)

def dump_sec34(d, h, pfc, fcs):
    base = h + pfc
    print('  --- sec34 (faceData) primeros verts (stride 44) ---')
    n = fcs//44
    skinned = static = zero = 0
    for i in range(n):
        o = base + i*44
        vals = [f32be(d, o+j*4) for j in range(11)]
        bone = u32be(d, o+28)
        if bone == 0xFFFFFFFF: static += 1
        elif bone < 100: skinned += 1
        else: zero += 1
        if i < 4:
            tag = 'nan' if vals[0]!=vals[0] else '%.3f'%vals[0]
            print('    v%d [%s] u=%.3f v=%.3f z=%.3f x=%.3f y=%.3f w=%.3f bone=%d' % (
                i, tag, vals[1],vals[2],vals[3],vals[4],vals[5],vals[6],bone))
    print('  skinned=%d static=%d otro=%d' % (skinned, static, zero))

def dump_ib(d, h, pib, ibc):
    base = h + pib
    vals = [u16be(d, base+i*2) for i in range(ibc)]
    print('  IB[%d]: %s...' % (ibc, vals[:12]))

def main():
    fn = sys.argv[1]
    d = open(fn, 'rb').read()
    r = analyze(d)
    if not r: return
    awo, awg_offs, labels = r
    dump_all = '--dump' in sys.argv
    sel = None
    if '--awg' in sys.argv:
        sel = int(sys.argv[sys.argv.index('--awg')+1])
    for i, off in enumerate(awg_offs):
        if sel is not None and i != sel: continue
        info = analyze_awg(d, awo, off)
        if not info: continue
        h, pvb, vbs, n_vb, pfc, fcs, n_face, pib, ibc, lbl = info
        if dump_all or sel is not None:
            dump_sec34(d, h, pfc, fcs)
            dump_ib(d, h, pib, ibc)

if __name__ == '__main__':
    main()

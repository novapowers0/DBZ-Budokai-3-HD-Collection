#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""awg_cara_export.py - Exportar AWGs de cara/mano (nb=1) de un bin HD B3 a OBJ.

Layout del vertice (44B, marker 0xFFFFFFFF en +0):
  +0 u32 0xFFFFFFFF | +4 f32 u | +8 f32 v | +12 f32 x
  +16 f32 y | +20 f32 z | +24 f32 weight(=1.0) | +28 f32 0.0
  +32 f32 nx | +36 f32 ny | +40 f32 nz

Estructura del AWG (offsets relativos al header #AWG, h):
  +0x10 n_bones (=1) | +0x2C = TAMANO del buffer de vertices (n*44)
  +0x30 ib_rel = offset IB | +0x34 sec_rel = TAMANO del IB en bytes
  +0x38 end_rel = offset fin IB
  Buffer de vertices SIEMPRE en h+0x1F0. Descriptor en h+0x180:
    +0x1C = n_verts, +0x24 = n_tris.  IB = lista de triangulos (quad strip).

Uso:
  python awg_cara_export.py <bin_amb_hd> <awg_index> [out.obj]
"""
import struct, sys, os, math

def u32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def u16(b, o): return struct.unpack('>H', b[o:o+2])[0]
def f32(b, o): return struct.unpack('>f', b[o:o+4])[0]


def export_awg_cara(binpath, awg_index, outpath=None):
    data = open(binpath, 'rb').read()
    awo = 0x40 if data[:4] == b'#AMB' else data.find(b'#AWO')
    tbl = u32(data, awo + 0x1C)
    h = awo + u32(data, awo + tbl + awg_index * 4)
    if data[h:h+4] != b'#AWG':
        raise ValueError('AWG%d no es #AWG' % awg_index)
    vb2_size = u32(data, h + 0x2C)   # tamano buffer vertices (bytes)
    ib_rel = u32(data, h + 0x30)     # offset IB
    ib_size = u32(data, h + 0x34)    # tamano IB (bytes)
    desc = h + 0x180
    n_verts = u32(data, desc + 0x1C)
    n_tris = u32(data, desc + 0x24)
    if n_verts == 0 or n_verts > 10000:
        raise ValueError('conteo de vertices invalido: %d' % n_verts)

    buf = h + 0x1F0
    verts = []
    for v in range(n_verts):
        o = buf + v * 44
        u, vv = f32(data, o + 4), f32(data, o + 8)
        x, y, z = f32(data, o + 12), f32(data, o + 16), f32(data, o + 20)
        w = f32(data, o + 24)
        nx, ny, nz = f32(data, o + 32), f32(data, o + 36), f32(data, o + 40)
        verts.append((x, y, z, u, vv, w, nx, ny, nz))

    ib_abs = h + ib_rel
    ib = [u16(data, ib_abs + k * 2) for k in range(ib_size // 2)]
    tris, skipped = [], 0
    for k in range(0, len(ib) - 2, 3):
        a, b, c = ib[k], ib[k + 1], ib[k + 2]
        if a >= n_verts or b >= n_verts or c >= n_verts or a == b or b == c or a == c:
            skipped += 1
            continue
        tris.append((a, b, c))

    if outpath:
        with open(outpath, 'w') as f:
            for x, y, z, u, vv, w, nx, ny, nz in verts:
                f.write('v %.6f %.6f %.6f\n' % (x, y, z))
                f.write('vt %.6f %.6f\n' % (u, vv))
            for a, b, c in tris:
                f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (
                    a + 1, a + 1, a + 1, b + 1, b + 1, b + 1, c + 1, c + 1, c + 1))
    xs = [v[0] for v in verts]; ys = [v[1] for v in verts]; zs = [v[2] for v in verts]
    return {'n_verts': n_verts, 'n_tris': n_tris, 'tris_emit': len(tris), 'skipped': skipped,
            'bounds': (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)),
            'nans': sum(1 for v in verts for val in v if math.isnan(val))}


def main():
    if len(sys.argv) < 3:
        print('Uso: python awg_cara_export.py <bin_amb_hd> <awg_index> [out.obj]')
        return
    awg_index = int(sys.argv[2])
    out = sys.argv[3] if len(sys.argv) > 3 else 'awg%d.obj' % awg_index
    r = export_awg_cara(sys.argv[1], awg_index, out)
    print('AWG%d: %d verts, %d/%d tris, %d skipped, bounds %s, NaN=%d -> %s' % (
        awg_index, r['n_verts'], r['tris_emit'], r['n_tris'], r['skipped'],
        ('[%.2f..%.2f][%.2f..%.2f][%.2f..%.2f]' % r['bounds']), r['nans'], out))


if __name__ == '__main__':
    main()
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""awg0_export.py - Exportar el AWG0 de un bin HD B3 a OBJ, autodetectando el
formato de vertice (A: marcador FFFF en +0/+2; C: marcador FFFF en +12).

Layout FORMATO A (Krillin 327, Cell F2 147), stride 44, sec_abs=+2:
  +0 FFFFFFFF | +4 u | +8 v | +12 z_local | +16 x_local | +20 y_local
  +24 peso | +28 BONE | +32 nz | +36 -ny | +40 nx

Layout FORMATO C (Goku 264, Vegeta 424, Babidi, Goten, Krillin armadura 329),
stride 44, sec_abs SIN +2:
  +0 x | +4 y | +8 z | +12 FFFFFFFF | +16 u | +20 v | +24 n.x | +28 n.y
  +32 n.z | +36 weight | +40 BONE

El IB es triangle STRIP (no lista): indices consecutivos con winding alternado y
triangulos degenerados como saltos entre parches. Se dibuja por los rangos A/B de
los descriptores del mesh group (bloques de 0x60, label en +0, rango A en +0x50/+0x54,
rango B en +0x58/+0x5C, todos <<8).

Uso:
  python awg0_export.py <bin_amb_hd> [out.obj] [--no-b]
"""
import struct, sys, os, re

def u32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def u16(b, o): return struct.unpack('>H', b[o:o+2])[0]
def f32(b, o): return struct.unpack('>f', b[o:o+4])[0]


def parse_awg0(d, awo):
    tbl = u32(d, awo + 0x1C)
    awg0 = awo + u32(d, awo + tbl)
    return {
        'awg0': awg0,
        'sec_rel': u32(d, awg0 + 0x34),
        'ib_abs': awg0 + u32(d, awg0 + 0x30),
        'end_abs': awg0 + u32(d, awg0 + 0x38),
        'mg': awg0 + u32(d, awg0 + 0x20),
        'mg_size': u32(d, awg0 + 0x28),
        'vb2_rel': u32(d, awg0 + 0x2C),
    }


def detect_format(d, st):
    """Detecta formato A (marcador en +0/+2) vs C (marcador en +12/+16/...).

    El buffer de vertices del AWG0 puede empezar en varias ubicaciones segun el
    bin: sec_rel, sec_rel+2 (align), o el final del mesh group (mg+mg_size).
    Se prueban todas con distintos offsets de marker para localizar el stream.
    """
    ib = st['ib_abs']
    candidates = {
        'sec_rel+2': st['awg0'] + st['sec_rel'] + 2,
        'sec_rel':   st['awg0'] + st['sec_rel'],
        'fin_mg':    st['mg'] + st['mg_size'],
    }
    all_offsets = (0, 2, 12, 16, 24, 38, 40)
    best = None
    for label, sec_abs in candidates.items():
        if sec_abs >= ib or sec_abs < 0:
            continue
        region = d[sec_abs:ib]
        n = len(region)
        total = n // 44
        for off in all_offsets:
            cnt = 0
            for i in range(0, n - 44, 44):
                if region[i + off:i + off + 4] == b'\xff\xff\xff\xff':
                    cnt += 1
            if cnt > total * 0.5:
                fmt = {'fmt': 'A' if off in (0, 2) else 'C',
                       'marker_off': off, 'sec_abs': sec_abs, 'n_sec': total}
                if best is None or fmt['n_sec'] > best['n_sec']:
                    best = fmt
    if best is None:
        raise ValueError('formato no detectado para este bin')
    return best


def read_verts(d, st, fmt):
    """Devuelve [(x,y,z,u,v,nx,ny,nz,w,bone), ...] del stream sec34."""
    n = fmt['n_sec']
    o0 = fmt['sec_abs']
    verts = []
    for i in range(n):
        o = o0 + i * 44
        if fmt['fmt'] == 'A':
            x, y, z = f32(d, o + 16), f32(d, o + 20), f32(d, o + 12)
            u, v = f32(d, o + 4), f32(d, o + 8)
            nx, ny, nz = f32(d, o + 40), -f32(d, o + 36), f32(d, o + 32)
            w, bone = f32(d, o + 24), u32(d, o + 28)
        else:  # formato C: posiciones en +0, nan en +12, bone en +40
            x, y, z = f32(d, o), f32(d, o + 4), f32(d, o + 8)
            u, v = f32(d, o + 16), f32(d, o + 20)
            nx, ny, nz = f32(d, o + 24), f32(d, o + 28), f32(d, o + 32)
            w, bone = f32(d, o + 36), u32(d, o + 40)
        verts.append((x, y, z, u, v, nx, ny, nz, w, bone))
    return verts


def read_descriptors(d, st, n_ib):
    """Bloques de 0x60 con label; rangos A=+0x50/0x54, B=+0x58/0x5C (<<8)."""
    mg, msize = st['mg'], st['mg_size']
    cands = set()
    for m in re.finditer(rb'[A-Z][A-Z0-9_]{4,25}', d[mg:mg + msize]):
        s = m.start()
        a0 = u32(d, mg + s + 0x50) >> 8; an = u32(d, mg + s + 0x54) >> 8
        b0 = u32(d, mg + s + 0x58) >> 8; bn = u32(d, mg + s + 0x5C) >> 8
        if (0 < an and 0 <= a0 < n_ib and a0 + an <= n_ib) or \
           (0 < bn and 0 <= b0 < n_ib and b0 + bn <= n_ib):
            cands.add(s)
    descs = []
    for s in sorted(cands):
        a0 = u32(d, mg + s + 0x50) >> 8; an = u32(d, mg + s + 0x54) >> 8
        b0 = u32(d, mg + s + 0x58) >> 8; bn = u32(d, mg + s + 0x5C) >> 8
        descs.append({'label': d[mg + s:mg + s + 20].split(b'\x00')[0].decode(errors='replace'),
                      'A': (a0, an), 'B': (b0, bn)})
    return descs


def strip_to_tris(ib, s, n, n_sec):
    tris, oob = [], 0
    for i in range(max(0, s), min(s + n, len(ib)) - 2):
        a, b, c = ib[i], ib[i + 1], ib[i + 2]
        if a == b or b == c or a == c:
            continue
        if a >= n_sec or b >= n_sec or c >= n_sec:
            oob += 1
            continue
        tris.append((a, b, c) if (i - s) % 2 == 0 else (b, a, c))
    return tris, oob


def export_awg0(binpath, outpath, use_b=True):
    d = open(binpath, 'rb').read()
    awo = 0x40 if d[:4] == b'#AMB' else d.find(b'#AWO')
    st = parse_awg0(d, awo)
    fmt = detect_format(d, st)
    n_ib = (st['end_abs'] - st['ib_abs']) // 2
    ib = [u16(d, st['ib_abs'] + i * 2) for i in range(n_ib)]
    verts = read_verts(d, st, fmt)
    tris, oob = [], 0
    for desc in read_descriptors(d, st, n_ib):
        for which, (s, n) in (('A', desc['A']), ('B', desc['B'])):
            if which == 'B' and not use_b:
                continue
            if n == 0 or not (0 <= s < n_ib and s + n <= n_ib):
                continue
            t, o = strip_to_tris(ib, s, n, len(verts))
            tris.extend(t)
            oob += o
    tris = list(dict.fromkeys(tris))
    with open(outpath, 'w') as f:
        for x, y, z, u, v, nx, ny, nz, w, bone in verts:
            f.write('v %.6f %.6f %.6f\n' % (x, y, z))
            f.write('vt %.6f %.6f\n' % (u, v))
        for a, b, c in tris:
            f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (
                a + 1, a + 1, a + 1, b + 1, b + 1, b + 1, c + 1, c + 1, c + 1))
    return fmt, verts, tris, oob


def main():
    if len(sys.argv) < 2:
        print('Uso: python awg0_export.py <bin_amb_hd> [out.obj] [--no-b]')
        return
    use_b = '--no-b' not in sys.argv
    out = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--') \
        else os.path.splitext(sys.argv[1])[0] + '.obj'
    fmt, verts, tris, oob = export_awg0(sys.argv[1], out, use_b)
    print('OK %s: fmt=%s n_sec=%d verts=%d tris=%d oob=%d -> %s' % (
        os.path.basename(sys.argv[1]), fmt['fmt'], fmt['n_sec'], len(verts),
        len(tris), oob, out))


if __name__ == '__main__':
    main()
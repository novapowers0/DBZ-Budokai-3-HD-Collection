#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""build_awo_autocontenido.py - Construir un bin HD #AWO autocontenido.

Paso 3 del conversor universal. Construye el AWG HD de un personaje desde su
#AMO0 PS2 (esqueleto JNB) + geometria HD (sec34/vb2/IB) + textura #AZT.

La vía correcta (AGENTS 12): construir un bin HD AUTOCONTENIDO (como
Bulma/Babidi), NO inyectar en una plantilla de Krillin.

Estructura del AWG0 HD (relativo al magic #AWG):
  +0x00 #AWG | +0x04 header_len(0x40) | +0x08 | +0x0C
  +0x10 n_bones | +0x14 axes | +0x18 groups | +0x1C 0x40
  +0x20 mg_off | +0x24 | +0x28 mg_size | +0x2C vb2 | +0x30 ib
  +0x34 sec34 | +0x38 end | +0x3C
  labels (16B x bones*2) en +0x40
  ejes (80B x bones)
  mesh group (mesh parts + arms + descriptores)
  sec34 (stride 44, align +2) | vb2 (stride 44) | IB (u16)

Layout vertice sec34 HD (44B, BE): [0xFFFFFFFF, u, v, z, x, y, peso, BONE, nz,-ny,nx]
Layout vertice vb2 HD (44B, BE):   [x, y, z, 0, 0, 0, 0, 0xFFFFFFFF, nx, ny, nz]

Uso:
  python build_awo_autocontenido.py <janemba.amb> <hd_geometry.json> <salida.bin>
"""
import struct
import sys
import json

def le32(b, o): return struct.unpack('<I', b[o:o+4])[0]
def lef(b, o): return struct.unpack('<f', b[o:o+4])[0]
def be_u32(v): return struct.pack('>I', v & 0xFFFFFFFF)
def be_u16(v): return struct.pack('>H', v & 0xFFFF)
def be_f32(v): return struct.pack('>f', v)


def build_awo(ps2, geom):
    """Construye el bin #AWO HD de Janemba.
    ps2 = #AMO0 PS2 (Janemba.amb), geom = dict con sec34/vb2/ib hex."""
    sec34 = bytes.fromhex(geom['sec34'])
    vb2 = bytes.fromhex(geom['vb2'])
    ib = bytes.fromhex(geom['ib'])
    n_sec = geom['n_sec']
    n_vb2 = geom['n_vb2']
    n_ib = geom['n_ib']

    # --- esqueleto JNB: ejes del AMG0 PS2 (en 0x84C0) ---
    amg0 = 0x84C0
    n_bones = le32(ps2, amg0 + 0x10)
    axes_rel = le32(ps2, amg0 + 0x14)
    axes_abs = amg0 + axes_rel
    # labels JNB: en AMG0 + labels_off? Para PS2 labels al final. Usar nombre generico.
    labels = []
    # Leer los 80B de cada eje, convertir LE->BE (campos floats y u32)
    ejes = b''
    for i in range(n_bones):
        e = axes_abs + i * 80
        raw = ps2[e:e+80]
        # convertir cada float LE->BE (los primeros 0x30 bytes son 12 floats)
        floats = [lef(ps2, e + j*4) for j in range(12)]
        # el resto: sello + offsets son u32 LE
        sello = le32(ps2, e + 0x30)
        p34 = le32(ps2, e + 0x34)
        p38 = le32(ps2, e + 0x38)
        # resto de bytes (0x3C..0x50) = 5 u32 LE
        rest = [le32(ps2, e + 0x3C + j*4) for j in range(5)]
        eje = b''.join(be_f32(f) for f in floats)
        eje += be_u32(sello) + be_u32(0) + be_u32(0)  # arm_ptr/p38 se rellenan luego
        eje += b''.join(be_u32(r) for r in rest)
        ejes += eje

    # --- labels (nombres de huesos, 16B) ---
    # Para Janemba, usar nombres genericos JNB_BONE_n (no tenemos la tabla de labels
    # del PS2 facilmente; el runtime no exige labels exactos para el skinning).
    label_bytes = b''
    for i in range(n_bones * 2):
        name = ('JNB_BONE_%d' % i).encode('ascii')
        label_bytes += name[:15].ljust(16, b'\x00')

    # --- geometria (sec34 con align +2) ---
    sec34_aligned = b'\x00\x00' + sec34  # align +2

    # --- ensamblar AWG0 ---
    # layout: header 0x40 + labels + ejes + mesh group + sec34 + vb2 + ib
    labels_off = 0x40
    axes_off = labels_off + len(label_bytes)
    mg_off = axes_off + len(ejes)
    # mesh group minimo: un descriptor que dibuje toda la geometria
    # (descriptor 0x60: label + stride44 + rango A=sec34 + rango B=ib)
    desc = b''
    desc += b'JNB_BODY'.ljust(16, b'\x00')           # label
    desc += be_u32(0x09000000) + be_u32(0x0F000000)  # consts
    desc += b'max 3832 m'.ljust(16, b'\x00')          # debug
    desc += be_f32(0)*3 + be_u32(0)                    # +0x20
    desc += be_u32(0x44) + be_u32(0)                   # +0x30
    desc += be_u32(0x1100) + be_u32(0x2C00) + be_u32(0x500) + be_u32(0)  # +0x38..+0x4C
    desc += be_u32(0 << 8) + be_u32(n_sec << 8)       # A_start, A_size (sec34)
    desc += be_u32(0 << 8) + be_u32(n_ib << 8 | 1)    # B_start, B_size (IB)
    mg = desc

    sec_off = mg_off + len(mg)
    # alinear sec34 a 0x800? El bin plantilla usa offsets exactos. Dejamos sec34 con align+2.
    vb2_off = sec_off + len(sec34_aligned)
    ib_off = vb2_off + len(vb2)
    end_off = ib_off + len(ib)

    # --- AWG header ---
    awg = b'#AWG'
    awg += be_u32(0x40) + be_u32(0) + be_u32(4)
    awg += be_u32(n_bones) + be_u32(axes_off) + be_u32(1) + be_u32(0x40)
    awg += be_u32(mg_off) + be_u32(1) + be_u32(len(mg))
    awg += be_u32(vb2_off) + be_u32(ib_off) + be_u32(sec_off) + be_u32(end_off)
    awg += be_u32(0x24)

    awg_body = awg + label_bytes + ejes + mg + sec34_aligned + vb2 + ib

    # --- AWO header (rel 0x40 del AMB), estructura verificada de Krillin ---
    # +0x00 #AWO | +0x04 0x30 | +0x08 0 | +0x0C 4
    # +0x10 n_bones | +0x14 0x30 | +0x18 n_awg | +0x1C awg_tbl
    # +0x20 0x18 | +0x24 labels | +0x28 0 | +0x2C 0
    # +0x30 0 | +0x34 bones_tbl | +0x38 0 | +0x3C 0
    n_awg = 1
    awg_off_in_awo = 0x40            # el AWG va en 0x40 rel AWO
    label_off = 0x24                 # labels (placeholder; no usados por el runtime)
    bones_tbl = 0x50                 # tabla de bones (placeholder)
    awo_header = bytearray(0x40)
    awo_header[0:4] = b'#AWO'
    struct.pack_into('>I', awo_header, 0x04, 0x30)
    struct.pack_into('>I', awo_header, 0x0C, 4)
    struct.pack_into('>I', awo_header, 0x10, n_bones)
    struct.pack_into('>I', awo_header, 0x14, 0x30)
    struct.pack_into('>I', awo_header, 0x18, n_awg)
    struct.pack_into('>I', awo_header, 0x1C, 0x30)   # awg_tbl en 0x30
    struct.pack_into('>I', awo_header, 0x20, 0x18)
    struct.pack_into('>I', awo_header, 0x24, label_off)
    struct.pack_into('>I', awo_header, 0x34, bones_tbl)
    awo_tbl = struct.pack('>I', awg_off_in_awo)
    # AWO = header 0x40 + tabla (4B en 0x30) + AWG en 0x40
    awo_body = bytes(awo_header[:0x30]) + awo_tbl
    awo_body = awo_body[:0x40].ljust(0x40, b'\x00')
    awo_body += awg_body

    return awo_body


def main():
    ps2 = open(sys.argv[1], 'rb').read()
    geom = json.load(open(sys.argv[2]))
    awo = build_awo(ps2, geom)
    out = sys.argv[3] if len(sys.argv) > 3 else 'janemba_autocontenido.bin'
    open(out, 'wb').write(awo)
    print('AWO HD generado: %d bytes -> %s' % (len(awo), out))


if __name__ == '__main__':
    main()

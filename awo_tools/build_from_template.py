#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""build_from_template.py - Construir bin HD de Janemba usando Cell Forma 2
(bin 147, 48 bones) como plantilla estructural completa.

La plantilla (Cell Forma 2) ya tiene la estructura de dibujo HD VALIDA
(mesh-ref blocks + ejes + arms + descriptores) con AWG0 de 48 bones, igual que
Janemba. Este generador reemplaza en el bin de la plantilla:
  1. La geometria (sec34/vb2/IB) con la de Janemba (ps2_to_hd_geometry).
  2. Los ejes JNB (quat/pos del esqueleto de Janemba).
  3. Los descriptores del cuerpo con los rangos de la geometria de Janemba.

Ventaja sobre el intento anterior: la plantilla YA tiene 48 bones y el mesh
group completo valido (no un descriptor minimo).

Uso:
  python build_from_template.py <bin_147_cell.bin> <janemba.amb> <hd_geometry.json> <salida.amb>
"""
import struct
import sys
import json

def be32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def le32(b, o): return struct.unpack('<I', b[o:o+4])[0]
def lef(b, o): return struct.unpack('<f', b[o:o+4])[0]
def be_u32(v): return struct.pack('>I', v & 0xFFFFFFFF)
def be_f32(v): return struct.pack('>f', v)


def extract_axes_jnb(ps2, amg0, n_bones):
    """Extrae los ejes JNB del AMG0 PS2 y los convierte a BE (mantiene arm_ptr
    y p38 como 0, se rellenan con los de la plantilla)."""
    axes_rel = le32(ps2, amg0 + 0x14)
    axes_abs = amg0 + axes_rel
    out = b''
    for i in range(n_bones):
        e = axes_abs + i * 80
        floats = [lef(ps2, e + j*4) for j in range(12)]
        sello = le32(ps2, e + 0x30)
        rest = [le32(ps2, e + 0x3C + j*4) for j in range(5)]
        eje = b''.join(be_f32(f) for f in floats)
        eje += be_u32(sello) + be_u32(0) + be_u32(0)  # arm_ptr/p38 (se rellenan)
        eje += b''.join(be_u32(r) for r in rest)
        out += eje
    return out


def main():
    plant = open(sys.argv[1], 'rb').read()   # bin 147 Cell Forma 2
    ps2 = open(sys.argv[2], 'rb').read()     # Janemba.amb
    geom = json.load(open(sys.argv[3]))
    sec34 = bytes.fromhex(geom['sec34'])
    vb2 = bytes.fromhex(geom['vb2'])
    ib = bytes.fromhex(geom['ib'])
    n_sec = geom['n_sec']
    n_vb2 = geom['n_vb2']
    n_ib = geom['n_ib']

    # Plantilla es #AMB (AWO en 0x40)
    awo = 0x40
    n_awg = be32(plant, awo + 0x18)
    awg_tbl = awo + be32(plant, awo + 0x1C)
    AWG0 = awo + be32(plant, awg_tbl)
    n_bones = be32(plant, AWG0 + 0x10)
    sec_rel = be32(plant, AWG0 + 0x34)
    vb2_rel = be32(plant, AWG0 + 0x2C)
    ib_rel = be32(plant, AWG0 + 0x30)
    end_rel = be32(plant, AWG0 + 0x38)
    axes_rel = be32(plant, AWG0 + 0x14)

    # Capacidad de los buffers de la plantilla
    buf_sec = (AWG0 + vb2_rel) - (AWG0 + sec_rel) - 2
    buf_vb2 = (AWG0 + ib_rel) - (AWG0 + vb2_rel)
    buf_ib = (AWG0 + end_rel) - (AWG0 + ib_rel)
    cap_sec = buf_sec // 44
    cap_vb2 = buf_vb2 // 44
    cap_ib = buf_ib // 2
    print('Plantilla Cell F2: n_bones=%d sec_cap=%d vb2_cap=%d ib_cap=%d' % (n_bones, cap_sec, cap_vb2, cap_ib))
    print('Janemba: sec=%d vb2=%d ib=%d' % (n_sec, n_vb2, n_ib))
    # --- MID-INSERT INTERNO: agrandar buffers sec34 y vb2 in-place ---
    # Mismo principio que el mid-insert de Goten en el runtime, pero dentro del bin.
    delta_sec = max(0, n_sec*44 - buf_sec)
    delta_vb2 = max(0, n_vb2*44 - buf_vb2)
    delta_ib = max(0, n_ib*2 - buf_ib)
    total_delta = delta_sec + delta_vb2 + delta_ib
    if total_delta > 0:
        print('mid-insert interno: +sec34=%d +vb2=%d +ib=%d bytes' % (delta_sec, delta_vb2, delta_ib))

    out = bytearray(plant)

    # Zonas originales
    sec_real_start = AWG0 + sec_rel + 2
    vb2_abs = AWG0 + vb2_rel
    ib_abs = AWG0 + ib_rel
    end_abs = AWG0 + end_rel

    if total_delta > 0:
        # Extraer ib original
        ib_old = bytes(out[ib_abs:end_abs])
        # Reconstruir: prefix + sec34_nuevo + vb2_nuevo + ib_original + suffix
        prefix = bytes(out[:sec_real_start])
        suffix = bytes(out[end_abs:])
        new_sec = sec34.ljust(buf_sec + delta_sec, b'\x00')
        new_vb2 = vb2.ljust(buf_vb2 + delta_vb2, b'\x00')
        out = bytearray(prefix + new_sec + new_vb2 + ib_old + suffix)
        # Recalcular posiciones
        vb2_abs = sec_real_start + len(new_sec)
        ib_abs = vb2_abs + len(new_vb2)
        end_abs = ib_abs + len(ib_old)
        # Actualizar offsets en el AWG header
        struct.pack_into('>I', out, AWG0 + 0x2C, vb2_abs - AWG0)
        struct.pack_into('>I', out, AWG0 + 0x30, ib_abs - AWG0)
        struct.pack_into('>I', out, AWG0 + 0x38, end_abs - AWG0)
        # ACTUALIZAR la tabla de offsets del AWO para los AWGs posteriores (1..N-1)
        # y el offset del AZT en el header AMB, ya que todo lo que sigue al AWG0
        # se desplaza por total_delta.
        awo = 0x40
        n_awg = be32(out, awo + 0x18)
        awg_tbl = awo + be32(out, awo + 0x1C)
        AWG0_off = be32(out, awg_tbl)  # offset del primer AWG (rel AWO)
        for i in range(1, n_awg):
            off = awg_tbl + i*4
            # solo si el AWG esta despues del AWG0 (lo normal)
            cur = be32(out, off)
            if cur > AWG0_off:
                struct.pack_into('>I', out, off, cur + total_delta)
        # Header AMB: actualizar offset del AZT (+0x30) y size del AWO (+0x24)
        # El AZT esta despues del AWO (que crecio). Sumar total_delta.
        if out[:4] == b'#AMB':
            az_off = be32(out, 0x30)
            if az_off != 0:
                struct.pack_into('>I', out, 0x30, az_off + total_delta)
            # AWO size real = AZT_off - 0x40 (el AWO va de 0x40 al AZT)
            awo_size = (az_off + total_delta) - 0x40
            struct.pack_into('>I', out, 0x24, awo_size)
        print('AWG header + tabla AWO + AZT offset actualizados (delta=%d)' % total_delta)

    # Rellenar geometria de Janemba
    out[sec_real_start:sec_real_start + n_sec*44] = sec34
    out[vb2_abs:vb2_abs + n_vb2*44] = vb2
    ib_be = b''
    for i in range(n_ib):
        ib_be += struct.pack('>H', int.from_bytes(ib[i*2:i*2+2], 'big'))
    out[ib_abs:ib_abs + n_ib*2] = ib_be
    # Llenar el resto del IB (cap_ib) con 0xFFFF (restart marker) para que el
    # runtime detenga el dibujo en n_ib y no lea indices residuales de Cell F2
    for i in range(n_ib, cap_ib):
        off = ib_abs + i*2
        out[off:off+2] = b'\xff\xff'

    # 4. Reemplazar ejes JNB (48 bones), manteniendo arm_ptr/p38 de la plantilla
    axes_abs = AWG0 + axes_rel
    axes_jnb = extract_axes_jnb(ps2, 0x84C0, n_bones)
    # los ejes JNB son 80B x 48. Reemplazar en la zona de ejes de la plantilla
    for i in range(n_bones):
        # ejes JNB (con arm_ptr/p38 = 0) + copiar arm_ptr/p38 de la plantilla
        eje_plant_off = axes_abs + i*80
        arm_ptr = be32(plant, eje_plant_off + 0x34)
        p38 = be32(plant, eje_plant_off + 0x38)
        eje = axes_jnb[i*80:(i+1)*80]
        # reescribir arm_ptr y p38 en el eje JNB
        eje = eje[:0x34] + be_u32(arm_ptr) + be_u32(p38) + eje[0x3C:]
        out[eje_plant_off:eje_plant_off+80] = eje

    # 5. Regenerar TODOS los descriptores REALES (stride 0x2C00) repartiendo
    #    TODA la geometria de Janemba (sec34+vb2 combinados + IB) uniformemente.
    #    Este es el patron de amo0_to_awo.py del B1: los rangos A/B contiguos
    #    cubren los buffers regenerados (clave: no dejar rangos OOB -> crash).
    import re
    zone_start = AWG0 + be32(plant, AWG0 + 0x20)  # mg_off
    zone_end = AWG0 + sec_rel
    zone = bytes(out[zone_start:zone_end])
    anchors = [m.start() for m in re.finditer(rb'max \d+ m', zone)]
    # Identificar SOLO descriptores reales: stride 0x2C00 y offset buffer != 0
    real_descs = []
    for an in anchors:
        dd = zone_start + (an - 0x18)
        try:
            if be32(bytes(out), dd + 0x44) == 0x2C00 and be32(bytes(out), dd + 0x40) != 0:
                real_descs.append(dd)
        except Exception:
            pass
    n_desc = len(real_descs)
    print('descriptores reales (stride 0x2C00):', n_desc)
    total_verts = n_sec + n_vb2  # stream combinado sec34+vb2
    total_idx = n_ib
    for k, dd in enumerate(real_descs):
        a_start = (total_verts * k) // n_desc
        a_size = (total_verts * (k+1)) // n_desc - a_start
        b_start = (total_idx * k) // n_desc
        b_size = (total_idx * (k+1)) // n_desc - b_start
        struct.pack_into('>I', out, dd + 0x50, a_start << 8)
        struct.pack_into('>I', out, dd + 0x54, a_size << 8)
        struct.pack_into('>I', out, dd + 0x58, b_start << 8)
        struct.pack_into('>I', out, dd + 0x5C, (b_size << 8) | 1)

    open(sys.argv[4], 'wb').write(bytes(out))
    print('Bin generado: %s (%d bytes)' % (sys.argv[4], len(out)))


if __name__ == '__main__':
    main()

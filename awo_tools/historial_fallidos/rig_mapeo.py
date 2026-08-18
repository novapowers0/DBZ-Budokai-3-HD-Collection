"""
Re-rigging JNB->KLL por mapeo de bone indices.

Descubrimiento clave (2026-08-14 sesion 3): el vertice HD del B3 lleva el
BONE INDEX en +28 (u32) y el peso en +24. El guest skinea cada vertice con
la matriz del hueso (eje) de ese indice. En el bin v6 los vertices de
Janemba tenian bone index 0 (bug de build_vertex_hd) -> masa deforme.

Fix: escribir el bone index correcto en +28 de cada vertice. Pero los bone
indices de Janemba (0-46) son del esqueleto JNB; el bin usa el esqueleto de
Krillin (KLL, 0-50). Hay que mapear bone_jnb -> label JNB -> label KLL ->
bone_kll.

Layout del vertice HD (B3, stride 44):
  +00: nan (flag)  +04: u  +08: v
  +12: pos.z_local +16: pos.x_local +20: pos.y_local
  +24: peso (float)
  +28: BONE INDEX (u32)
  +32: normal.z +36: normal.y +40: normal.x

Labels:
  Krillin: AWO +0x24, 16 bytes/bone, label de bone_idx en +bone_idx*16.
  Janemba: AMG +0x1C (labels_off), 16 bytes/bone, label de bone_idx en
           +bone_idx*16 (solo los bones pares tienen label en el AMG0).

Uso:
  python rig_mapeo.py <verts_skinned_jnb.bin> <output_verts_kll.bin>
"""

import os
import struct
import sys


def u32be(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


def u32le(data, off):
    return struct.unpack('<I', data[off:off + 4])[0]


def read_labels_hd(bin_hd):
    """Krillin HD: labels de hueso. Devuelve {bone_idx: label}.

    Layout: AWO +0x24 apunta a una tabla de 102 bloques de 16 bytes.
    El label del bone_idx N esta en el bloque N*2 (indices pares 0..100).
    """
    AWO = bin_hd[0x40:]
    labels_off = u32be(AWO, 0x24)
    labels = {}
    for bi in range(51):
        idx = bi * 2
        s = AWO[labels_off + idx * 16:labels_off + idx * 16 + 16]
        s = s.split(b'\x00')[0].decode('utf-8', errors='replace')
        if s:
            labels[bi] = s
    return labels


def read_labels_ps2(bin_ps2, amg_base):
    """Janemba PS2: labels de hueso del AMG0. Devuelve {bone_idx: label}."""
    labels_off = u32le(bin_ps2, amg_base + 0x1C)
    labels = {}
    for bi in range(48):
        s = bin_ps2[amg_base + labels_off + bi * 16:
                    amg_base + labels_off + bi * 16 + 16]
        s = s.split(b'\x00')[0].decode('utf-8', errors='replace')
        if s:
            labels[bi] = s
    return labels


def build_mapping(jnb_labels, kll_labels):
    """Mapea bone_idx JNB -> bone_idx KLL por label."""
    # label -> bone_idx KLL
    kll_by_label = {}
    for kll_idx, label in kll_labels.items():
        kll_by_label[label] = kll_idx
    # bone_jnb -> label_jnb -> label_kll -> bone_kll
    mapping = {}
    for jnb_idx, label in jnb_labels.items():
        # El label JNB: XJNB_BODY -> XKLL_BODY, JNB_WAIST -> KLL_WAIST
        kll_label = label.replace('XJNB_', 'XKLL_').replace('JNB_', 'KLL_')
        if kll_label in kll_by_label:
            mapping[jnb_idx] = kll_by_label[kll_label]
        else:
            mapping[jnb_idx] = -1  # sin mapeo
    return mapping


def remap_verts(verts, mapping, fallback=0):
    """Reescribe el bone index (+28) de cada vertice segun el mapeo."""
    out = bytearray(verts)
    n = len(verts) // 44
    mapped = {v: 0 for v in set(mapping.values()) if v >= 0}
    unmapped = 0
    for i in range(n):
        off = i * 44
        bi = u32be(out, off + 28)
        target = mapping.get(bi, -1)
        if target >= 0:
            struct.pack_into('>I', out, off + 28, target)
            mapped[target] += 1
        else:
            struct.pack_into('>I', out, off + 28, fallback)
            unmapped += 1
    return bytes(out), mapped, unmapped


def main():
    if len(sys.argv) < 3:
        print('Uso: rig_mapeo.py <verts_jnb> <out_verts_kll>')
        return
    verts = open(sys.argv[1], 'rb').read()
    out = sys.argv[2]

    # Cargar labels
    krillin = open(os.path.join(os.environ.get('TEMP', ''), 'opencode', 'b327_hd.bin'), 'rb').read()
    kll_labels = read_labels_hd(krillin)
    janemba = open(os.path.join(os.environ.get('TEMP', ''), 'opencode', 'janemba_541.bin'), 'rb').read()
    # amg_base del AMG0 de Janemba
    import sys as _s
    _s.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from extract_geometry import PS2Model
    model = PS2Model(janemba)
    amg_base = model.amo0 + model.amg_offsets()[0]
    jnb_labels = read_labels_ps2(janemba, amg_base)

    print('Krillin HD: %d labels' % len(kll_labels))
    print('Janemba AMG0: %d labels' % len(jnb_labels))

    mapping = build_mapping(jnb_labels, kll_labels)

    # Mapeos manuales adicionales (huesos de dedos/caras en AMGs separados).
    # Krillin solo tiene L00_LHAND(18)/L00_RHAND(25)/L00_FACE(36); los demas
    # dedos/faces de Janemba se asignan a esos equivalentes.
    manual = {
        1: 18,    # dedos mano izquierda -> KLL_L00_LHAND
        3: 18,
        5: 18,
        9: 18,
        11: 18,
        17: 18,
        19: 25,   # dedos mano derecha -> KLL_L00_RHAND
        23: 25,
        25: 25,
        27: 25,
        31: 25,
        35: 25,
        37: 36,   # caras -> KLL_L00_FACE
        41: 36,
        43: 36,
        45: 36,
    }
    for bi, target in manual.items():
        if bi not in mapping or mapping[bi] < 0:
            mapping[bi] = target
    print('\nMapeo JNB -> KLL:')
    for jnb_idx in sorted(mapping):
        jnb_label = jnb_labels.get(jnb_idx, '?')
        kll_idx = mapping[jnb_idx]
        kll_label = kll_labels.get(kll_idx, '?') if kll_idx >= 0 else '-'
        print('  bone %2d (%s) -> bone %2d (%s)%s' % (
            jnb_idx, jnb_label, kll_idx, kll_label,
            '' if kll_idx >= 0 else '  [SIN MAPEO]'))

    out_data, mapped, unmapped = remap_verts(verts, mapping)
    with open(out, 'wb') as f:
        f.write(out_data)
    print('\nVertices remapeados: %d | sin mapeo: %d' % (
        sum(mapped.values()), unmapped))
    print('Guardado: %s (%d bytes)' % (out, len(out_data)))


if __name__ == '__main__':
    main()

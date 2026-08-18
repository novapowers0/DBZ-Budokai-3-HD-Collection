"""
Extractor de geometría de un #AMO0/#AMG (PS2) — vértices y triángulos por mesh part.

Parsea el modelo PS2 a una representación intermedia:
  - esqueleto (huesos, jerarquía, labels)
  - mesh parts (por hueso): vértices B5 (V+VN+VT en 48B) y los triángulos
    (cada part PS2 expande triángulos: 3 vértices de 48B por triángulo)

Basado en AMO Decompiler.py (Nexus-sama) y la RE propia.

Uso:
  python extract_geometry.py <bin_amb_ps2> [--json]
"""

import struct
import sys
import json


def read_u32(b, off):
    return struct.unpack('<I', b[off:off + 4])[0]


def read_f32(b, off):
    return struct.unpack('<f', b[off:off + 4])[0]


class PS2Model:
    def __init__(self, b):
        self.b = b
        # AMO0 en 0x40 (o buscar)
        self.amo0 = 0x40
        if b[0:4] != b'#AMB':
            self.amo0 = 0
        # verificar magic
        m = b[self.amo0:self.amo0 + 4]
        if m not in (b'#AMO0', b'#AMO'):
            raise ValueError('No es #AMO0 en 0x%X: %s' % (self.amo0, m))
        self.bone_am = read_u32(b, self.amo0 + 0x10)
        self.bone_loc = read_u32(b, self.amo0 + 0x14)
        self.amg_am = read_u32(b, self.amo0 + 0x18)
        self.amg_loc = read_u32(b, self.amo0 + 0x30)

    def labels(self, amg_base):
        """Labels del primer AMG."""
        amg = amg_base
        bone_am = read_u32(self.b, amg + 0x10)
        label_loc = read_u32(self.b, amg + 0x1C)
        labels = []
        for i in range(bone_am):
            o = amg + label_loc + i * 32
            raw = self.b[o:o + 32]
            labels.append(raw.split(b'\x00')[0].decode('ascii', 'replace'))
        return labels

    def amg_offsets(self):
        """Los offsets de AMGs desde el header AMO0 (0x34+)."""
        offs = [self.amg_loc]
        for i in range(1, self.amg_am):
            o = self.amo0 + 0x34 + (i - 1) * 4
            v = read_u32(self.b, o)
            if v == 0:
                break
            offs.append(v)
        return offs

    def bones(self):
        """Relaciones de hueso desde bone_loc."""
        bones = []
        for i in range(self.bone_am):
            o = self.amo0 + self.bone_loc + i * 32
            bones.append({
                'index': read_u32(self.b, o),
                'axes': read_u32(self.b, o + 4),
                'child': read_u32(self.b, o + 8),
                'sibling': read_u32(self.b, o + 12),
                'parent': read_u32(self.b, o + 16),
            })
        return bones

    def mesh_parts(self, amg_base):
        """
        Extrae los mesh parts del AMG. Cada hueso con mesh group tiene parts.
        Cada part PS2: header 0xA0 + mesh data (face blocks).
        Devuelve lista de parts con sus vértices y triángulos.
        """
        amg = amg_base
        bone_am = read_u32(self.b, amg + 0x10)
        axes_loc = read_u32(self.b, amg + 0x14)
        parts = []
        for bi in range(bone_am):
            e0 = amg + axes_loc + bi * 80
            p34 = read_u32(self.b, e0 + 0x34)  # ptr armature
            if p34 == 0:
                continue
            arm = amg + p34
            arm_idx = read_u32(self.b, arm)
            mesh_hdr = read_u32(self.b, arm + 4)
            if mesh_hdr == 0:
                continue
            mg = amg + mesh_hdr
            mp_amnt = read_u32(self.b, mg)
            if mp_amnt == 0 or mp_amnt > 64:
                continue
            part_offs = [read_u32(self.b, mg + 16 + i * 4) for i in range(mp_amnt)]
            for pi, rel in enumerate(part_offs):
                po = mg + rel
                parts.append(self._parse_part(po, bi, arm_idx))
        return parts

    def _parse_part(self, po, bone_idx, arm_idx):
        """Parsea un mesh part PS2: header 0xA0 + mesh data."""
        b = self.b
        type1 = read_u32(b, po)
        type2 = read_u32(b, po + 4)
        tex = read_u32(b, po + 8)
        shader = read_u32(b, po + 0xC)
        size_field = read_u32(b, po + 0x90)
        mesh_size = (size_field - 0x60000000) * 16 if size_field >= 0x60000000 else 0
        mesh_data = b[po + 0xA0: po + 0xA0 + mesh_size]
        # interpretar mesh_data: header + vértices B5 de 48B
        # El part0 PS2 tenia: 16B header + 16B? -> los vertices empezaban en +0x20 (0x64D0)
        # Del analisis: mesh_data[0]=00*16, mesh_data[16]=01 00 00 00 0E 00 00 00...
        # Los vertices B5 empiezan en mesh_data[32] (el part0 vert0 en 0x64D0 = po+0xA0+0x20)
        stride = {0x01B5: 48, 0x01B4: 32, 0x0190: 16}.get(type1, 48)
        n_verts = 0
        verts = []
        if len(mesh_data) >= 32:
            body = mesh_data[32:]
            n_verts = len(body) // stride
            for i in range(n_verts):
                vo = body[i * stride: i * stride + stride]
                if stride == 48:
                    x, y, z = struct.unpack('<fff', vo[0:12])
                    nx, ny, nz = struct.unpack('<fff', vo[16:28])
                    u, v = struct.unpack('<ff', vo[32:40])
                    verts.append({'pos': [x, y, z], 'nrm': [nx, ny, nz], 'uv': [u, v]})
                elif stride == 32:
                    x, y, z = struct.unpack('<fff', vo[0:12])
                    u, v = struct.unpack('<ff', vo[16:24])
                    verts.append({'pos': [x, y, z], 'nrm': [0, 0, 0], 'uv': [u, v]})
                else:
                    x, y, z = struct.unpack('<fff', vo[0:12])
                    verts.append({'pos': [x, y, z], 'nrm': [0, 0, 0], 'uv': [0, 0]})
        return {
            'bone_idx': bone_idx,
            'arm_idx': arm_idx,
            'type1': type1,
            'tex': tex,
            'shader': shader,
            'stride': stride,
            'n_verts': n_verts,
            'verts': verts,
            'po': po,
        }


def main():
    if len(sys.argv) < 2:
        print('Uso: python extract_geometry.py <bin_amb_ps2>')
        return
    with open(sys.argv[1], 'rb') as f:
        b = f.read()
    model = PS2Model(b)
    print('AMO0 @0x%X: bone_am=%d amg_am=%d amg_loc=0x%X' % (
        model.amo0, model.bone_am, model.amg_am, model.amg_loc))
    amgs = model.amg_offsets()
    print('AMG offsets:', [hex(o) for o in amgs])
    total_parts = 0
    total_verts = 0
    for i, amg_off in enumerate(amgs):
        amg_base = model.amo0 + amg_off
        parts = model.mesh_parts(amg_base)
        nv = sum(p['n_verts'] for p in parts)
        print('  AMG%d @0x%X: %d mesh parts, %d vértices totales' % (i, amg_off, len(parts), nv))
        for p in parts[:5]:
            print('    bone=%d type=0x%04X tex=%d shader=%d stride=%d verts=%d' % (
                p['bone_idx'], p['type1'], p['tex'], p['shader'], p['stride'], p['n_verts']))
            if p['verts']:
                v0 = p['verts'][0]
                print('      vert0: pos=%s nrm=%s uv=%s' % (
                    [round(x, 4) for x in v0['pos']],
                    [round(x, 4) for x in v0['nrm']],
                    [round(x, 4) for x in v0['uv']]))
        total_parts += len(parts)
        total_verts += nv
    print('TOTAL: %d mesh parts, %d vértices' % (total_parts, total_verts))


if __name__ == '__main__':
    main()

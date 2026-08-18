# -*- coding: utf-8 -*-
"""
swap_cuerpo_hd.py — Inyectar la geometria del CUERPO de Goten en el AWG0
de Krillin (slot 327), manteniendo huesos/mesh group/IB de Krillin.

HD->HD interno: misma pose, misma escala, mismo formato. Aisla la variable
de "el bin completo de otro personaje no es compatible con el slot".

El AWG0 de Krillin: vb=2190 slots (stride 44, bone@+28). Los vertices de
Goten estan en su AWG0 vb=2035. La clave: la geometria de Goten ya esta en
espacio local del esqueleto GOT, y las matrices world de GOT vs KLL difieren
(skeleton distinto). Pero como ambos son humanoides con misma pose base,
podemos mapear por posicion aproximada (cada vertice KLL del cuerpo recibe
el vertice GOT mas cercano en world), o transformar local->world->local.

Uso:
  python swap_cuerpo_hd.py <goten.bin> <krillin.bin> <out>
"""

import sys, io, struct
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

def u32be(b, o): return struct.unpack('>I', b[o:o+4])[0]
def u32le(b, o): return struct.unpack('<I', b[o:o+4])[0]
def rf(b, o): return struct.unpack('>f', b[o:o+4])[0]

def get_awg0(d, awo):
    tbl = u32be(d, awo+0x1C)
    off0 = u32be(d, awo+tbl)
    h = awo + off0
    pvb = u32be(d, h+0x28); vbs = u32be(d, h+0x2C)
    pfc = u32be(d, h+0x30); fcs = u32be(d, h+0x34)
    pib = u32be(d, h+0x38)
    n_slots = vbs//44
    return h, pvb, vbs, n_slots, pfc, fcs, pib

def read_vb(d, base, idx):
    o = base + idx*44
    vals = [rf(d, o+j*4) for j in range(11)]
    bone = u32be(d, o+28)
    return vals, bone

def write_vb(d, base, idx, vals, bone):
    o = base + idx*44
    for j in range(11):
        struct.pack_into('>f', d, o+j*4, vals[j])
    struct.pack_into('>I', d, o+28, bone)

def main():
    goten = open(sys.argv[1], 'rb').read()
    krillin = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    # AWG0 de cada uno
    awo_g = goten.find(b'#AWO')
    awo_k = 0x40  # Krillin AMB
    hg, pvb_g, vbs_g, n_g, pfc_g, fcs_g, pib_g = get_awg0(goten, awo_g)
    hk, pvb_k, vbs_k, n_k, pfc_k, fcs_k, pib_k = get_awg0(krillin, awo_k)
    print('Goten AWG0: vb=%d slots | Krillin AWG0: vb=%d slots' % (n_g, n_k))
    print('Goten sec34(face)=%d | Krillin sec34(face)=%d' % (fcs_g//44, fcs_k//44))

    # Transformar los verts de Goten: estan en local GOT. Para mapearlos a KLL
    # sin matrices, usamos el IB: el vertice local de Goten del hueso b se
    # transforma a world con mat_GOT[b], luego a local KLL con mat_KLL[b_mapped].
    # PERO no tenemos mat world facil aqui. Alternativa simplificada:
    # Copiar los verts de Goten tal cual a los slots de Krillin, manteniendo
    # su bone local (GOT bone index). El guest skinea con las matrices del
    # esqueleto KLL por indice -> si GOT bone != KLL bone, se deforma.
    # Para el swap HD->HD con MISMO tipo de cuerpo, el bone index coincide
    # aprox (BODY=0, WAIST=1...). Probamos copia directa con mapeo de bone.

    # Mapeo de bones GOT->KLL por label (simplificado: BODY=0)
    # Leer labels de ambos
    def read_labels(d, awo):
        lbl_off = u32be(d, awo+0x24)
        labels = {}
        for bi in range(60):
            s = d[awo+lbl_off+bi*32:awo+lbl_off+bi*32+32]
            s = s.split(b'\x00')[0].decode('latin1', errors='replace')
            if s: labels[bi] = s
        return labels
    lbl_g = read_labels(goten, awo_g)
    lbl_k = read_labels(krillin, awo_k)
    kll_by_label = {l: i for i, l in lbl_k.items()}
    mapping = {}
    for bi, l in lbl_g.items():
        kl = l.replace('XGTN_','XKLL_').replace('GTN_','KLL_')
        mapping[bi] = kll_by_label.get(kl, -1)
    print('Bones GOT mapeados a KLL:', sum(1 for v in mapping.values() if v>=0), '/', len(lbl_g))

    # Copiar verts de Goten a Krillin
    new_bin = bytearray(krillin)
    base_g = awo_g + pvb_g
    base_k = awo_k + pvb_k
    used = 0
    for i in range(min(n_g, n_k)):
        vals, bone_g = read_vb(goten, base_g, i)
        bone_k = mapping.get(bone_g, 0)
        # no copiar padding/zeros
        if vals[3]==0 and vals[4]==0 and vals[5]==0 and vals[10]==0 and vals[8]==0 and vals[9]==0:
            continue
        vals[7] = 1.0  # weight
        write_vb(new_bin, base_k, used, vals, bone_k)
        used += 1
    # rellenar resto con bone 0
    pad = (struct.pack('>f', float('nan')) + struct.pack('>f',0)*2 +
           struct.pack('>f',0)*3 + struct.pack('>f',1.0) +
           struct.pack('>I', 0) + struct.pack('>f',0)*3)
    for i in range(used, n_k):
        base = base_k + i*44
        new_bin[base:base+44] = pad
    print('Copiados %d verts de Goten -> %d slots de Krillin' % (used, n_k))

    with open(out, 'wb') as f:
        f.write(bytes(new_bin))
    print('Guardado: %s' % out)

if __name__ == '__main__':
    main()

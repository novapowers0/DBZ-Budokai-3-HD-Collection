#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""port_ps2_b3_draw.py - Paso 3 del pipeline port PS2 -> B3 HD.

Convierte los GRUPOS de port_ps2_b3_geometry.py (rango A=vertices en el pool,
rango B=segmento del IB) en la ESTRUCTURA DE DIBUJO HD:

  - descriptores (bloques de 0x60): label + 'max N m' + rango A + rango B.
  - mesh-ref blocks (mesh parts): tipo de vertice (B5/B4) + textura/shader.

Si hay mas grupos que descriptores en la plantilla, se FUSIONAN los grupos
adyacentes en el IB (misma textura/vtype) hasta ajustarse a la cuenta de la
plantilla. Esto corrige el reparto UNIFORME de build_from_template.py (que
dividia los buffers por igual sin respetar los mesh parts reales).

Uso:
  python port_ps2_b3_draw.py <geometry.json> <n_desc_plantilla> <salida_draw.json>
"""
import sys
import json
import re
import struct


def be32(b, o): return struct.unpack('>I', b[o:o+4])[0]


def template_descriptor_count(plant_bin):
    """Cuenta los descriptores REALES de la plantilla (stride 0x2C00 y offset
    buffer != 0) en su AWG0. Solo AWG0 (el cuerpo)."""
    b = open(plant_bin, 'rb').read()
    awo = 0x40
    tbl = be32(b, awo + 0x1C)
    AWG0 = awo + be32(b, awo + tbl)
    mg_start = AWG0 + be32(b, AWG0 + 0x20)
    zone_end = AWG0 + be32(b, AWG0 + 0x34)
    zone = b[mg_start:zone_end]
    anchors = [m.start() for m in re.finditer(rb'max \d+ m', zone)]
    n = 0
    for an in anchors:
        dd = mg_start + (an - 0x18)
        try:
            if be32(b, dd + 0x44) == 0x2C00 and be32(b, dd + 0x40) != 0:
                n += 1
        except Exception:
            pass
    return n


def merge_to_count(groups, target):
    """Fusiona grupos adyacentes en el IB hasta quedar 'target' grupos.
    Prioriza fusionar grupos contiguos en el IB con el mismo (tex, vtype)."""
    gs = [dict(g) for g in groups]
    while len(gs) > target:
        best = None
        for i in range(len(gs) - 1):
            a, bb = gs[i], gs[i + 1]
            if a['B'][0] + a['B'][1] != bb['B'][0]:
                continue                      # no adyacentes en el IB
            same = (a['tex'] == bb['tex'] and a['vtype'] == bb['vtype'])
            # puntuacion: preferir fusion de parts del MISMO material contiguas
            score = (a['B'][1] + bb['B'][1]) if same else (a['B'][1] + bb['B'][1]) * 10
            if best is None or score < best[0]:
                best = (score, i, same)
        if best is None:
            # sin pares adyacentes: fusionar las dos mas pequenas consecutivas
            best = (float('inf'), 0, False)
            for i in range(len(gs) - 1):
                s = gs[i]['B'][1] + gs[i + 1]['B'][1]
                if s < best[0]:
                    best = (s, i, False)
        _, i, _ = best
        a, bb = gs[i], gs[i + 1]
        gs[i] = {'bone': a['bone'], 'tex': a['tex'], 'vtype': a['vtype'],
                 'A': [min(a['A'][0], bb['A'][0]),
                       a['A'][1] + bb['A'][1]],
                 'B': [a['B'][0], a['B'][1] + bb['B'][1]]}
        del gs[i + 1]
    return gs


def main():
    if len(sys.argv) < 4:
        print('Uso: port_ps2_b3_draw.py <geometry.json> <plantilla.bin> <salida_draw.json>')
        return
    geom = json.load(open(sys.argv[1]))
    target = template_descriptor_count(sys.argv[2])
    groups = geom['groups']
    n_desc = min(len(groups), target)
    if len(groups) > target:
        groups = merge_to_count(groups, target)
        n_desc = target
    print('grupos=%d -> descriptores=%d' % (len(geom['groups']), n_desc))
    descriptors = []
    mesh_ref = []
    seen_ref = set()
    for k, g in enumerate(groups):
        label = 'PORT_%02d' % k
        descriptors.append({'label': label, 'A': g['A'], 'B': g['B']})
        key = (g['vtype'], g['tex'])
        if key not in seen_ref:
            seen_ref.add(key)
            mesh_ref.append({'vtype': g['vtype'], 'tex': g['tex']})
    for d in descriptors:
        print('  desc: A=(%d,%d) B=(%d,%d)' % (d['A'][0], d['A'][1], d['B'][0], d['B'][1]))
    print('mesh-ref blocks: %d' % len(mesh_ref))
    json.dump({'descriptors': descriptors, 'mesh_ref': mesh_ref,
               'n_sec': geom['n_sec'], 'n_vb2': geom['n_vb2'], 'n_ib': geom['n_ib']},
              open(sys.argv[3], 'w'))
    print('guardado:', sys.argv[3])


if __name__ == '__main__':
    main()
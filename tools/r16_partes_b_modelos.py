#!/usr/bin/env python3
"""Pruebas de las piezas B contra los modelos locales; no distribuye arte."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys
import tempfile

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'mod center hd' / 'cac'), str(ROOT / 'mod center hd'),
               str(ROOT / 'awo_tools')]
import vista_heroe as vh
import model_render as mr
import partes_namek_majin as pb
from injertar_pelo import triangulos_por_draw
from skin_slots import SkinSlots
from b3_gateway import WINDOW_BUDGET
import texture_b3 as tb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--inspeccionar', action='store_true')
    ap.add_argument('--salida', type=Path)
    ap.add_argument('--fid', type=int, action='append')
    ap.add_argument('--clave')
    ap.add_argument('--render', type=Path)
    args = ap.parse_args()
    if args.salida and not args.salida.parent.is_dir():
        ap.error('el directorio de salida debe existir antes de comprobar los modelos')
    work = tempfile.mkdtemp(prefix='r16_partes_b_')
    afs = vh.rb.Afs(str(ROOT / 'us' / 'data_cmn.afs'), work)
    resultados = []
    for fid in args.fid or (350, 351, 352, 353, 128, 129, 133, 134, 141, 142):
        m = mr.Model(afs.entry(fid))
        td = triangulos_por_draw(m)
        if args.inspeccionar:
            print('FID', fid, 'ventanas', len(m.pos))
            for i, d in enumerate(m.table.draws):
                tri = [t for di, t in td if di == i]
                head = sum(np.any(m.vbone[list(t)] == m.bone('HEAD')) for t in tri)
                mat = m.mats[d['material']]
                if head or d['group_bone'] == 0 or 'MANT' in m.labels[d['group_bone']]:
                    print(i, d['prim'], m.labels[d['group_bone']], 'mat', d['material'],
                          'tex', mat['tex'], 'ramp', mat['shader'], 'tris', len(tri), 'HEAD', head)
                if head and mat['tex'] < len(m.tex) and mat['shader'] != 0xFFFFFFFF:
                    tex = m.tex[mat['tex']]
                    region = 'piel' if fid >= 350 else 'piel_majin'
                    u, v = pb._uv_region(tex, region)
                    muestra = tex[int(v*tex.shape[0]), int(u*tex.shape[1])]
                    print('  muestra', region, (u, v), muestra, 'rampa media',
                          m.tex[mat['shader']][..., :3].mean(axis=(0, 1)))
            print('huesos', {label: count for label, count in
                             Counter(m.labels[b] for b in m.vbone).items()
                             if 'HAIR' in label or 'MANT' in label})
            if fid == 352:
                for di, d in enumerate(m.table.draws):
                    tt = np.asarray([t for j, t in td if j == di], int)
                    print('huesos draw', di, Counter(m.labels[b] for b in m.vbone[np.unique(tt)]))
        else:
            cuerpo = 11 if fid >= 350 else (34 if fid < 133 else (35 if fid < 141 else 36))
            opciones = [x for x in pb._B3 if cuerpo in x[3] and (not args.clave or x[2] == args.clave)]
            assert opciones, 'sin opciones para esta base'
            casos = [[x] for x in opciones]
            if not args.clave:
                casos.append([x for x in opciones if x[0] in (3101, 3121, 3140, 3160, 3203, 3221, 3242)])
            for seleccion in casos:
                ficha = dict(cuerpo=cuerpo, donante=cuerpo)
                for i, z, clave, _ in seleccion:
                    ficha['b3_'+pb._ZONAS[z]] = i
                result, info = pb.aplicar_b3(m.data, ficha)
                assert result != m.data and info['ids'] == [x[0] for x in seleccion]
                nuevo = mr.Model(result)
                assert nuevo.labels == m.labels
                assert nuevo.avb.bind_worlds() == m.avb.bind_worlds()
                oldss, newss = SkinSlots(m.avb), SkinSlots(nuevo.avb)
                assert oldss.table == newss.table and oldss.slot_of == newss.slot_of
                assert len(nuevo.pos) <= max(WINDOW_BUDGET, len(m.pos))
                assert np.isfinite(nuevo.pos).all() and np.isfinite(nuevo.nrm).all()
                assert nuevo.table.materials == m.table.materials, 'cambio de toon/material'
                assert result[result.find(b'#AZT'):] == m.data[m.data.find(b'#AZT'):], 'texturas alteradas'
                assert np.max(np.abs(nuevo.posed()[0])) < 1000, 'geometría disparada'
                for awg in range(1, m.avb.n_awg):
                    a, b = mr.Model(m.data, awg=awg), mr.Model(result, awg=awg)
                    assert a.avb.indices() == b.avb.indices()
                    if any(x[2].startswith('mj_cuerpo_') for x in seleccion):
                        for va, vb in zip(a.avb.vertices(), b.avb.vertices()):
                            assert all(va[k] == vb[k] for k in ('bone', 'w', 'uv'))
                            assert np.isfinite(vb['pos']).all() and np.isfinite(vb['nrm']).all()
                    else:
                        assert a.avb.vertices() == b.avb.vertices(), 'expresiones/manos modificadas'
                resumen = dict(fid=fid, ids=info['ids'], ventanas=len(nuevo.pos),
                               triangulos=len(nuevo.tris), **info.get('ventanas', {}))
                resultados.append(resumen)
                print('PASS B3', resumen, flush=True)
                if args.render:
                    assert args.render.is_dir(), 'el directorio de evidencia debe existir'
                    nombre = '%d_%s' % (fid, '_'.join(str(x) for x in info['ids']))
                    (args.render / (nombre+'.bin')).write_bytes(result)
            print('PASS base %d: %d selecciones, DDS/materiales/paleta/expresiones intactos' % (fid, len(casos)), flush=True)
    if resultados:
        assert pb.aplicar_b3(m.data, dict(cuerpo=0, b3_torso=3100)) == (m.data, {})
        assert pb.aplicar_b3(m.data, dict(cuerpo=11, b3_piernas=3100)) == (m.data, {})
        print('PASS B3 partes B: %d casos; selecciones ajenas no modifican el modelo' % len(resultados))
        if args.salida:
            assert args.salida.parent.is_dir()
            args.salida.write_text(json.dumps(resultados, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

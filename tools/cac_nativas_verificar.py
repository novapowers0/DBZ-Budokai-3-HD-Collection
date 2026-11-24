"""Comprobaciones de recursos propios y montaje local; no inicia el juego."""
from __future__ import annotations
import argparse
import json
import sys
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'mod center hd/cac'), str(ROOT/'mod center hd'), str(ROOT/'awo_tools')]


def main():
    import partes_nativas as pn
    parser = argparse.ArgumentParser()
    parser.add_argument('--salida', type=Path, required=True)
    parser.add_argument('--cuerpo', type=int, action='append')
    parser.add_argument('--todas', action='store_true')
    args = parser.parse_args()
    if args.salida.exists() or not args.salida.parent.is_dir():
        raise ValueError('Salida nueva con padre existente requerida')
    args.salida.mkdir()
    catalog = pn.catalogo_b3()
    if len(catalog) != 36 or len({p['id'] for p in catalog}) != 36:
        raise ValueError('Catálogo nativo incompleto o duplicado')
    for row in catalog:
        mesh = pn.recurso(row['id'])
        p, n, t = (np.asarray(mesh[k]) for k in ('P', 'N', 'tris'))
        assert p.shape == n.shape and p.shape[1] == 3
        assert np.isfinite(p).all() and np.isfinite(n).all()
        assert t.min() >= 0 and t.max() < len(p)
        assert np.all(np.abs(np.linalg.norm(n, axis=1)-1) < .002)
        assert np.all(np.linalg.norm(np.cross(p[t[:, 1]]-p[t[:, 0]], p[t[:, 2]]-p[t[:, 0]]), axis=1) > 1e-9)
    print('PASS recursos: 24 cabezas y 12 cuerpos originales, índices, normales y finitud', flush=True)
    import vista_heroe as vh
    import model_render as mr
    afs = vh.rb.Afs(str(ROOT/'us/data_cmn.afs'), str(args.salida/'afs'))
    reports = []
    for body in args.cuerpo or [27, 11, 35]:
        race = pn.RAZA[body]
        fid = vh.rb.IDS[body]['models'][0]
        raw = afs.entry(fid)
        model = mr.Model(raw)
        skeleton = {n: np.asarray(model.worlds())[i, :3, 3].tolist() for i, n in enumerate(model.labels)}
        (args.salida/('rig_%d.json'%body)).write_text(json.dumps(skeleton, indent=2), encoding='utf-8')
        combinations = [(h, b) for h in range(8) for b in range(4)] if args.todas else [(0, 0)]
        for h, b in combinations:
            base = pn.BASE[race]
            ficha = dict(cuerpo=body, b3_torso=base+h, b3_guantes=base+40+b)
            data, summary = pn.aplicar_b3(raw, ficha)
            edited = mr.Model(data)
            p, n = edited.posed()
            used = np.unique(edited.tris)
            assert len(edited.tris) and np.isfinite(p[used]).all()
            assert np.isfinite(n[used]).all()
            assert summary['ventanas']['total'] <= max(3261, model.avb.n)
            path = args.salida/('%s_%d_%d.bin'%(race, h, b))
            path.write_bytes(data)
            reports.append(dict(cuerpo=body, cabeza=base+h, cuerpo_pieza=base+40+b,
                                fid=fid, **summary))
            print('PASS injerto %s %d/%d: %d ventanas, %d triángulos'%(race, h, b,
                  summary['ventanas']['total'], len(edited.tris)), flush=True)
    (args.salida/'resultados.json').write_text(json.dumps(reports, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

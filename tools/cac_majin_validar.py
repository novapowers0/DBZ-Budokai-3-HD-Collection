"""Auditoría reproducible de recursos propios y montaje binario Majin R16.

No ejecuta el juego. Los AMB del jugador se usan solo como anfitrión en una
carpeta de evidencia nueva; nunca se incorporan a res/ ni a las fuentes Blender.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path


def validar(salida, modelos=True):
    import numpy as np
    root = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(root/'mod center hd/cac'), str(root/'mod center hd'), str(root/'awo_tools')]
    import partes_majin as maj
    from variantes_formas import formas_raza
    if salida.exists() or not salida.parent.is_dir():
        raise ValueError('La evidencia requiere una carpeta nueva con padre existente')
    salida.mkdir()
    resource = root/'res/heroe/partes_majin'
    manifest = json.loads((resource/'catalogo.json').read_text(encoding='utf-8'))
    files = manifest['files']
    assert len(files) == 135 and len({r['file'] for r in files}) == 135
    assert len(maj.catalogo_b3()) == 19
    report = dict(recursos=0, formas=list(formas_raza(4)), montajes=[], fuentes=[])
    for form in formas_raza(4):
        geometries = set()
        for row in maj.catalogo_b3():
            for ctype in (range(3) if row['id'] >= 4060 else (0,)):
                mesh = maj.recurso(row['id'], form, cuerpo_tipo=ctype)
                p, n, uv = (np.asarray(mesh[k], float) for k in ('P', 'N', 'UV'))
                t = np.asarray(mesh['tris'], int)
                assert p.shape[1:] == (3,) and p.shape == n.shape
                assert len(uv) == len(p) == len(mesh['huesos']) == len(mesh['bindings'])
                assert np.isfinite(p).all() and np.isfinite(n).all() and np.isfinite(uv).all()
                assert t.min() >= 0 and t.max() < len(p)
                assert np.all(np.linalg.norm(n, axis=1) > .5)
                digest = hashlib.sha256(p.astype('<f8').tobytes()+t.astype('<i4').tobytes()).hexdigest()
                if ctype == 0:
                    assert digest not in geometries, 'Dos tipos repiten geometría'
                    geometries.add(digest)
                report['recursos'] += 1
        for body in range(4040, 4043):
            b = maj.recurso(body, form)
            bp = np.asarray(b['P'])[b['costuras']['cuello_cuerpo']]
            bn = np.asarray(b['N'])[b['costuras']['cuello_cuerpo']]
            for head in range(4020, 4024):
                h = maj.recurso(head, form)
                hp = np.asarray(h['P'])[h['costuras']['cuello_cabeza']]
                hn = np.asarray(h['N'])[h['costuras']['cuello_cabeza']]
                assert np.max(np.abs(bp-hp)) < 1e-6
                assert np.max(np.abs(bn-hn)) < 1e-6
        check = manifest['controles'][form]
        assert check['cuello_gap'] < 1e-6 and all(check['rig_vertices_movidos'].values())
        blend = root/'studies 5/ENTREGAS/R16/MAJ_BLENDER'/(form+'.blend')
        assert blend.is_file() and blend.stat().st_size > 10000
        report['fuentes'].append(str(blend.relative_to(root)))
    # Los otros catálogos siguen pudiendo pasar por este despacho sin activarlo.
    assert maj.seleccion_b3(dict(cuerpo=11, b3_torso=3100)) == {}
    for bad in (dict(cuerpo=11, b3_torso=4000), dict(cuerpo=35, b3_torso=4020),
                dict(cuerpo=35, b3_torso=4000, b3_botas=3700)):
        try:
            maj.seleccion_b3(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('Aceptó selección incompatible')
    print('PASS Majin recursos: 135 hashes, 19 diseños distintos, 5 fuentes Blender, 60 uniones de cuello', flush=True)
    if modelos:
        import model_render as mr
        import roster_build as rb
        import draw_oracle
        import partes_nativas as pn
        from awg_vertex_buffer import AwgVertexBuffer
        from b3_gateway import DrawTable
        work = salida/'afs'
        work.mkdir()
        afs = rb.Afs(str(root/'us/data_cmn.afs'), str(work))
        raw_by_body = {}
        for body in maj.CUERPOS:
            raw_by_body[body] = afs.entry(rb.IDS[body]['models'][0])
        # Todas las piezas se recalzan a los tres esqueletos reales, sin escribirlos
        # como recursos distribuibles. Prueba radios, normales y correspondencias.
        recalzadas = 0
        for body, raw in raw_by_body.items():
            model = mr.Model(raw)
            for record in files:
                mesh = json.loads((resource/record['file']).read_text(encoding='utf-8'))
                p, n, bones = pn.retarget(mesh, model)
                assert np.isfinite(p).all() and np.isfinite(n).all()
                assert min(bones) >= 0 and max(bones) < len(model.labels)
                recalzadas += 1
        report['recalzadas'] = recalzadas
        print('PASS Majin retarget: 405 piezas sobre los tres esqueletos nativos', flush=True)
        cases = [(body, raw, 'f_maj_super', [4000, 4020, 4040+i, 4060+i])
                 for i, (body, raw) in enumerate(raw_by_body.items())]
        for i, form in ((1, 'f_maj_absorcion'), (2, 'f_maj_absorbida')):
            cid, native = rb.IDS[35]['form_models'][i]
            fid = maj.modelo_forma(cid, native)
            cases.append((35, afs.entry(fid), form, [4007, 4023, 4042, 4063]))
        for index, (body, raw, form, ids) in enumerate(cases):
            ficha = dict(cuerpo=body, _majin_forma=form, partes=[])
            ficha.update({'b3_'+z: ident for z, ident in zip(maj.ZONAS, ids)})
            data, summary = maj.aplicar_b3(raw, ficha, root)
            dest = salida/('montaje_%d_%d.bin'%(index, body))
            dest.write_bytes(data)
            assert draw_oracle.analyse(str(dest), str(salida)), 'Montaje inválido para el guest'
            rendered = mr.Model(data)
            assert len(rendered.tris) > 100
            assert np.isfinite(rendered.posed()[0]).all()
            # Regresión del Majin negro: tras comprimir/leer el atlas, la banda
            # alfa cero debe conservar RGB coral y no convertirse en negro.
            for mi in np.unique(rendered.tri_mat):
                mat = rendered.mats[mi]
                tex = rendered.tex[mat['tex']]
                h, w = tex.shape[:2]
                skin = tex[int(.125*h), int(.125*w)]
                assert skin[0] > .75 and skin[1] > .35, 'Atlas de banda cero perdió su color'
                assert skin[3] < .1, 'Se perdió la banda toon cero'
                ramp = rendered.tex[mat['shader']]
                assert np.max(ramp[:, -1, :3]) < .04, 'La luz plena no conserva el color'
            for awg in range(1, rendered.avb.n_awg):
                table = DrawTable(AwgVertexBuffer._parse(data, awg=awg))
                assert all(d['B'][1] == 0 for d in table.draws), 'Arte auxiliar nativo aún dibujándose'
            report['montajes'].append(dict(cuerpo=body, forma=form, ids=ids, resumen=summary,
                                           sha256=hashlib.sha256(data).hexdigest()))
        print('PASS Majin montaje: tres cuerpos y dos formas, paleta e índices válidos; auxiliares nativos vaciados', flush=True)
    (salida/'validacion.json').write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str), encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--salida', type=Path, required=True)
    parser.add_argument('--solo-recursos', action='store_true')
    args = parser.parse_args()
    validar(args.salida, not args.solo_recursos)

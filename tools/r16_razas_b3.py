"""Laboratorio y respaldos del carril solRAZAS_B3 (solo datos locales)."""
from pathlib import Path
import argparse
import shutil
import sys
import json
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BACKUP = Path('D:/_PARA_BORRAR_CLAUDE/r16_solRAZAS_B3')
sys.path[:0] = [str(ROOT/'mod center hd/cac'), str(ROOT/'mod center hd'), str(ROOT/'awo_tools')]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('accion', choices=('respaldo', 'inventario', 'validar', 'diagnostico', 'comparar', 'presupuesto', 'apartar', 'limpiar'))
    ap.add_argument('archivos', nargs='*')
    a = ap.parse_args()
    if a.accion == 'respaldo':
        if not BACKUP.parent.is_dir():
            raise ValueError('Falta la carpeta padre del respaldo')
        for name in a.archivos:
            src, dest = ROOT/name, BACKUP/'original'/name
            if dest.exists():
                print('Respaldo conservado:', name)
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dest)
            print('Respaldado:', name)
        return
    BACKUP.mkdir(exist_ok=True)
    import vista_heroe as vh
    import model_render as mr
    import numpy as np
    (BACKUP/'afs').mkdir(exist_ok=True)
    work = tempfile.mkdtemp(prefix=a.accion+'_', dir=BACKUP/'afs')
    afs = vh.rb.Afs(str(ROOT/'us/data_cmn.afs'), work)
    if a.accion == 'inventario':
        rows = []
        for body in (11, 27, 38, 34, 35, 36):
            row = vh.rb.IDS[body]
            print('ROSTER', body, row, flush=True)
            for fid in dict.fromkeys(row['models']):
                m = mr.Model(afs.entry(fid))
                P, N = m.posed()
                import injertar_pelo as ip
                td = np.asarray([d for d, _ in ip.triangulos_por_draw(m)])
                draws = []
                for di, d in enumerate(m.table.draws):
                    ti = np.flatnonzero(td == di)
                    vv = np.unique(m.tris[ti]) if len(ti) else []
                    draws.append(dict(indice=di, **d, huesos=sorted({m.labels[m.vbone[v]] for v in vv}),
                                      triangulos=len(ti), material_info={k: val.tolist() if hasattr(val, 'tolist') else val
                                      for k, val in m.mats[d['material']].items()}))
                rows.append(dict(cuerpo=body, fid=fid, huesos=m.labels, draws=draws,
                                 variantes=m.variants(), texturas=[list(t.shape) for t in m.tex]))
                print('MODELO', fid, 'awgs', m.avb.n_awg, 'verts', len(P), 'tris', len(m.tris),
                      'draws', len(m.table.draws), 'mats', len(m.mats), 'tex', len(m.tex), flush=True)
        (BACKUP/'inventario.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
        print('Inventario completo:', BACKUP/'inventario.json')
        return
    if a.accion == 'apartar':
        for name in a.archivos:
            src = ROOT/'res/heroe'/name
            dest = BACKUP/'res_sobrantes'/name
            if src.is_dir():
                if dest.exists():
                    raise ValueError('Ya existe el destino: '+str(dest))
                dest.parent.mkdir(exist_ok=True)
                shutil.move(str(src), str(dest))
                print('Apartado:', src, '->', dest)
        return
    if a.accion == 'limpiar':
        # Los JSON siguen siendo necesarios para fichas del proveedor antiguo.
        # Blender y las laminas no los leen los proveedores de ejecucion.
        for name in ('arcosiano_r16', 'namek_original', 'partes_majin', 'partes_nativas', 'partes_nativas_freezer'):
            for src in (ROOT/'res/heroe'/name).rglob('*'):
                if src.suffix.lower() not in ('.blend', '.blend1', '.png'):
                    continue
                dest = BACKUP/'res_sobrantes'/src.relative_to(ROOT/'res/heroe')
                if dest.exists():
                    raise ValueError('Destino de respaldo ya existente: '+str(dest))
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(src), str(dest))
                print('Apartado:', src.relative_to(ROOT))
        return
    import razas_b3 as rb3
    if a.accion == 'comparar':
        from PIL import Image, ImageDraw
        hoja = Image.new('RGB', (960, 960), '#30343a')
        for fila, fid in enumerate((350, 352, 353)):
            raw = afs.entry(fid)
            original = mr.Model(raw)
            expresion = mr.Model(raw)
            lab = next(l for l in expresion.variants() if 'FACE' in l)
            expresion.use_variant(lab)
            ficha = rb3.ficha_prueba(11, fila if fila == 0 else fila+1, 0)
            ficha['b3_botas'] = 4081
            data, _ = rb3.aplicar(afs.entry(350), ficha, ROOT, afs)
            for col, model in enumerate((original, expresion, mr.Model(data))):
                P, _ = model.posed()
                vv = np.unique(model.tris)
                P = P[vv]
                center = (P.min(0)+P.max(0))/2
                scale = 285/max(np.ptp(P, axis=0)[:2])
                rgba = mr.render(model, size=(320,320), center=center, scale=scale, ss=1)
                im = Image.fromarray(rgba)
                hoja.paste(im, (col*320, fila*320), im)
                ImageDraw.Draw(hoja).text((col*320+8, fila*320+8), '%s %s' % (fid, ('nativo','expresion','injerto')[col]), fill='white')
        hoja.save(BACKUP/'comparar.png')
        return
    if a.accion == 'presupuesto':
        def contar(amb, host, grupos, n_tex, awg=0):
            for dec in (None, 6, 5, 4, 3):
                usados = set()
                for g in grupos:
                    for v in np.unique(g['tris']):
                        datos = np.r_[g['P'][v], g['N'][v], g['UV'][v]]
                        if dec is not None:
                            datos = np.round(datos, dec)
                        usados.add((g['rigido'], int(g['B'][v]), *datos))
                print('UNIQUE', dec, len(usados), flush=True)
            for g in grupos:
                print('GRUPO', g['region'], len(np.unique(g['tris'])), len(g['tris']), g['rigido'])
            raise SystemExit(0)
        rb3._emitir = contar
        ficha = rb3.ficha_prueba(27)
        ficha.update({ 'b3_'+z: 4000+v for z, v in zip(rb3.ZONAS, (9,18,3,2)) })
        rb3.aplicar(afs.entry(181), ficha, ROOT, afs)
        return
    if a.accion == 'diagnostico':
        from PIL import Image, ImageDraw
        casos = [(27, (0, 0, 0, 0)), (27, (9, 18, 3, 2)), (27, (45, 4, 1, 3)),
                 (11, (0, 0, 0, 0)), (11, (6, 2, 2, 1)), (11, (7, 3, 3, 2)),
                 (35, (0, 0, 0, 0)), (35, (3, 2, 1, 2)), (35, (6, 3, 2, 1))]
        hoja = Image.new('RGB', (960, 960), '#30343a')
        reportes = []
        for k, (body, valores) in enumerate(casos):
            ficha = rb3.ficha_prueba(body)
            ficha.update({'b3_'+z: b+v for z, b, v in zip(rb3.ZONAS, rb3.INICIOS[rb3.FAMILIAS[body]], valores)})
            if k == 1:
                ficha['colores_regiones'] = {'piel': [110, 155, 190], 'gemas': [235, 170, 45]}
            raw = afs.entry(vh.rb.IDS[body]['models'][0])
            try:
                data, report = rb3.aplicar(raw, ficha, ROOT, afs)
            except ValueError as exc:
                reportes.append(dict(host=body, valores=valores, error=str(exc)))
                print('FAIL rasgos', body, valores, exc, flush=True)
                continue
            model = mr.Model(data)
            P, N = model.posed()
            assert np.isfinite(P).all() and np.isfinite(N).all()
            center = (P.min(0)+P.max(0))/2
            scale = 285/max(np.ptp(P, axis=0)[:2])
            rgba = mr.render(model, size=(320, 320), center=center, scale=scale, ss=1)
            im = Image.fromarray(rgba)
            hoja.paste(im, ((k%3)*320, (k//3)*320), im)
            ImageDraw.Draw(hoja).text(((k%3)*320+8, (k//3)*320+6), '%s %s' % (body, valores), fill='white')
            reportes.append(dict(host=body, valores=valores, **report))
            print('PASS rasgos', body, valores, report['ventanas'], report['costura'], flush=True)
        hoja.save(BACKUP/'diagnostico.png')
        (BACKUP/'diagnostico.json').write_text(json.dumps(reportes, indent=2), encoding='utf-8')
        return
    reports = []
    for body in (27, 11, 35):
        raw = afs.entry(vh.rb.IDS[body]['models'][0])
        for h in range(8):
            ficha = rb3.ficha_prueba(body, h, h % 4)
            data, report = rb3.aplicar(raw, ficha, ROOT, afs)
            m = mr.Model(data)
            P, N = m.posed()
            assert len(m.tris) and np.isfinite(P).all() and np.isfinite(N).all()
            assert m.labels == mr.Model(raw).labels
            path = BACKUP/f'{body}_{h}.bin'
            path.write_bytes(data)
            reports.append(dict(cuerpo_host=body, cabeza_tipo=h, **report))
            print('PASS injerto', body, h, 'ventanas', report['ventanas'],
                  'tris', report['triangulos'], 'variantes', len(report['variantes']), flush=True)
    (BACKUP/'validacion.json').write_text(json.dumps(reports, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

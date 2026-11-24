"""Valida 8 cabezas x 4 cuerpos por raza desde el AFS del jugador.

Las laminas de este laboratorio son offline; las capturas en juego se obtienen
con pruebas.py correr smoke_e razas_b3 --par raza=... --par piezas=h,a,b,c.
"""
from pathlib import Path
import argparse
import hashlib
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'mod center hd/cac'), str(ROOT / 'mod center hd'), str(ROOT / 'awo_tools')]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--raza', choices=('freezer', 'namek', 'majin'))
    ap.add_argument('--muestra', action='store_true', help='12 muestras, en vez de las 32 combinaciones por raza')
    args = ap.parse_args()
    if not args.salida.parent.is_dir() or args.salida.exists():
        ap.error('la salida debe ser nueva y su carpeta padre debe existir')
    args.salida.mkdir()
    import numpy as np
    from PIL import Image, ImageDraw
    import vista_heroe as vh
    import model_render as mr
    import razas_b3 as rb
    afs = vh.rb.Afs(str(ROOT / 'us/data_cmn.afs'), str(args.salida))
    reports = []
    for body in (27, 11, 35):
        race = rb.FAMILIAS[body]
        if args.raza and args.raza != race:
            continue
        raw = afs.entry(vh.rb.IDS[body]['models'][0])
        cells = [(h, h % 4) for h in range(8)] + [(0, b) for b in range(4)]
        cases = cells if args.muestra else [(h, b) for h in range(8) for b in range(4)]
        sheet = Image.new('RGB', (6 * 256, 4 * 280), '#30343a')
        for h, b in cases:
            data, report = rb.aplicar(raw, rb.ficha_prueba(body, h, b), ROOT, afs)
            model = mr.Model(data)
            P, N = model.posed()
            used = np.unique(model.tris)
            assert len(used) and np.isfinite(P).all() and np.isfinite(N).all()
            assert model.labels == mr.Model(raw).labels
            assert report['ventanas'] <= 3261
            # Las expresiones no pueden llevarse la mandibula ni los dientes.
            jaw = [i for i, lab in enumerate(model.labels) if rb._clave(lab) == 'M_JAW']
            source = mr.Model(afs.entry(rb.CABEZAS[race][h]))
            source_jaw = [i for i, lab in enumerate(source.labels) if rb._clave(lab) == 'M_JAW']
            if np.isin(source.vbone[np.unique(source.tris)], source_jaw).any():
                assert jaw and np.isin(model.vbone[used], jaw).any(), (race, h, b, 'mandibula ausente')
            digest = hashlib.sha256(data).hexdigest()
            reports.append(dict(host=body, h=h, b=b, sha256=digest, **report))
            print('PASS receta', race, 'cabeza', h + 1, 'cuerpo', b + 1,
                  'ventanas', report['ventanas'], flush=True)
            for cell, pair in enumerate(cells):
                if pair != (h, b):
                    continue
                extent = P[used]
                center = (extent.min(0) + extent.max(0)) / 2
                scale = 220 / max(np.ptp(extent, axis=0)[:2])
                for side, yaw in enumerate((0, 90)):
                    rgba = mr.render(model, size=(256, 256), center=center, scale=scale, view=mr.rot_y(yaw), ss=1)
                    image = Image.fromarray(rgba)
                    x, y = (cell % 3) * 512 + side * 256, (cell // 3) * 280
                    sheet.paste(image, (x, y + 24), image)
                    ImageDraw.Draw(sheet).text((x + 5, y + 5), f'{race} H{h+1} B{b+1} / {yaw}', fill='white')
        sheet.save(args.salida / (race + '_catalogo.png'))
        sheet.resize((sheet.width // 2, sheet.height // 2), Image.Resampling.LANCZOS).save(
            args.salida / (race + '_catalogo_05.png'))
        rows = [r for r in reports if r['raza'] == race]
        print('Identidades binarias distintas:', race, len({r['sha256'] for r in rows}),
              '/', len(rows), '(no demuestra diferencia visual)', flush=True)
    (args.salida / 'validacion.json').write_text(json.dumps(reports, indent=2), encoding='utf-8')
    print('PASS laboratorio:', len(reports), 'recetas; laminas OFFLINE', flush=True)


if __name__ == '__main__':
    main()

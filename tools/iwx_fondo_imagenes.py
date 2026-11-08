"""Extrae imagen y reloj/camara del MISMO estado IW retenido; no inventa pares HD.

La fase de captura del port se mantiene separada de esta autoridad original.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import struct
import zipfile

from iwx_reloj_original import estado as reloj_estado


def sha(b):
    return hashlib.sha256(b).hexdigest()


def originales(referencia, salida, extras=()):
    ref = json.loads(referencia.read_text(encoding='utf-8'))
    salida.mkdir(exist_ok=False)
    result = []
    rows = list(ref['instantaneas'])
    for path in extras:
        rows.append(dict(estado=str(path), sha256=sha(path.read_bytes())))
    for r in rows:
        path = Path(r['estado'])
        raw = path.read_bytes()
        if sha(raw) != r['sha256']:
            raise ValueError('Estado original cambiado')
        with zipfile.ZipFile(path) as z:
            ram = z.read('eeMemory.bin')
            png = z.read('Screenshot.png')
        def u(a):
            return struct.unpack_from('<I', ram, a)[0]
        def f(a, n=1):
            return list(struct.unpack_from('<'+str(n)+'f', ram, a))
        mgr, campo = u(0x49CA70), u(0x49CBBC)
        n = u(0x4A4D40)
        if n != r.get('n', n) or u(0x5C0DC8) != 0x7D2:
            raise ValueError('Estado no corresponde a Serpiente')
        image = salida/f'original_{n}.png'
        image.write_bytes(png)
        stage = u(0x49CDB8)
        # El original publica la pose antes de avanzar el reloj. La fila anterior
        # se conserva como inferencia contrastable, nunca como contador VSync.
        series = path.parents[2]/'7D2'/'cuadros.jsonl'
        previous = None
        if series.exists():
            for line in series.read_text(encoding='utf-8').splitlines():
                candidate = json.loads(line)
                if candidate['n'] == n-1:
                    previous = candidate['camara']['f']
                    break
        if previous is None:
            previous = next((x['cam_reloj'] for x in result if x['n'] == n-1), None)
        row = dict(n=n, estado=str(path), estado_sha256=sha(raw), imagen=str(image),
            imagen_sha256=sha(png), cam_clip=u(campo+0x114), cam_reloj=f(campo+0x118)[0],
            tiempo_evaluado_inferido=previous,
            ojo=f(campo+0x80, 3), objetivo=f(campo+0x90, 3),
            pose_dibujo=f(u(mgr+0x244), 16), uv=[f(stage+0x5F0+i*8, 2) for i in range(8)],
            reloj_original=reloj_estado(path),
            sincronizacion='Imagen GS, RAM y reloj serializados en el mismo archivo; esto NO garantiza fase EE/GS ni ausencia de imagen obsoleta')
        result.append(row)
        print(json.dumps({k: row[k] for k in ('n', 'cam_clip', 'cam_reloj', 'ojo', 'objetivo', 'uv')}, ensure_ascii=False))
    report = dict(originales=result, paridad_port=False, activado_normal=False)
    with (salida/'originales.json').open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, allow_nan=False)
    return report


def comparar(captura, salida):
    from PIL import Image, ImageDraw, ImageStat
    salida.mkdir(exist_ok=False)
    raw = captura.read_bytes()
    report = json.loads(raw)
    replay = report['meta']['reproduccion_camara']
    pares, descartadas = [], []
    for pair in replay['pares']:
        original = Path(pair['original_imagen']).read_bytes()
        port = Path(pair['port_imagen']).read_bytes()
        if sha(original) != pair['original_sha256'] or sha(port) != pair['port_sha256']:
            raise ValueError('Imagen modificada despues de la captura')
        src = Image.open(io.BytesIO(original)).convert('RGB')
        dst = Image.open(io.BytesIO(port)).convert('RGB')
        # El contenedor puede traer un borde fino distinto alrededor del color
        # plano. Examinar el interior evita confundir ese borde con la escena.
        bx, by = max(1, src.width//20), max(1, src.height//20)
        std = max(ImageStat.Stat(src.crop((bx, by, src.width-bx, src.height-by))).stddev)
        if std < .5:
            descartadas.append(dict(n=pair['original_n'], motivo='Imagen original uniforme: no muestra el escenario', desviacion_max=std))
            continue
        if not pair.get('camara_coincide') or not pair.get('uv_retenida_estable'):
            raise ValueError('La captura no retiene camara y UV contrastadas')
        row = dict(n=pair['original_n'], error_camara=pair['error_ojo_objetivo_max'],
            uv_retenida_estable=True, original_imagen=pair['original_imagen'],
            port_imagen=pair['port_imagen'], tam_original=src.size, tam_port=dst.size)
        for name, im in (('original', src), ('port', dst)):
            # Estadistica del cielo, no un diff de pixeles entre aspectos distintos.
            sky = im.crop((0, 0, im.width, im.height//4))
            row['rgb_medio_cielo_'+name] = ImageStat.Stat(sky).mean
        canvas = Image.new('RGB', (1280, 520), '#161616')
        draw = ImageDraw.Draw(canvas)
        draw.text((12, 8), f"Original IW EU - estado {pair['original_n']}", fill='white')
        draw.text((652, 8), 'Candidato UV - camara/UV reproducidas', fill='white')
        src.thumbnail((640, 480))
        dst.thumbnail((640, 480))
        canvas.paste(src, ((640-src.width)//2, 40+(480-src.height)//2))
        canvas.paste(dst, (640+(640-dst.width)//2, 40+(480-dst.height)//2))
        picture = salida/f"comparacion_{pair['original_n']}.png"
        canvas.save(picture)
        row['comparacion'] = str(picture)
        pares.append(row)
    if not pares:
        raise ValueError('Ninguna pareja visual util')
    root = Path(__file__).resolve().parents[1]
    files = ('tools/iwx_fondo_imagenes.py', 'tools/iwx_fondo_uv_focal.py',
        'mod center hd/campos_iw.py', 'out/build/win-amd64-dual/dbz3.exe',
        'out/build/win-amd64-release/dbz3.exe')
    result = dict(captura=str(captura), captura_sha256=sha(raw), pares=pares,
        referencias_descartadas=descartadas, stage_restaurado=report['stage_restaurado'],
        actualizador_natural_validado=False, fase_ee_gs_original_acreditada=False,
        activado_normal=False, aceptacion_visual=False,
        dictamen='Comparacion preparada para revision visual manual; no acredita sincronismo natural ni FOV/aspecto',
        limites=['Reproduccion controlada de estado, no tasa de actualizacion natural',
                 'Diferencia de aspecto 4:3/16:9 conservada; sin diff global de pixeles',
                 f'{len(descartadas)} imagenes originales uniformes excluidas; no cuentan como pares',
                 'No atribuye diferencias de dibujo exclusivamente al reparto UV',
                 'No compara audio ni ejecuta bateria general'],
        claves={p:sha((root/p).read_bytes()) for p in files})
    with (salida/'comparacion.json').open('x', encoding='utf-8') as f:
        json.dump(result, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(result, ensure_ascii=False, indent=1))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--referencia', type=Path)
    p.add_argument('--salida', type=Path, required=True)
    p.add_argument('--estado', type=Path, action='append', default=[])
    p.add_argument('--csv', type=Path)
    p.add_argument('--comparar-captura', type=Path)
    a = p.parse_args()
    if a.comparar_captura:
        if a.referencia or a.estado or a.csv:
            p.error('--comparar-captura no se mezcla con extraccion de originales')
        comparar(a.comparar_captura, a.salida)
        return
    if not a.referencia:
        p.error('La extraccion exige --referencia')
    report = originales(a.referencia, a.salida, a.estado)
    if a.csv:
        rows = list(csv.DictReader(a.csv.open(encoding='utf-8')))
        for clip in sorted({r['cam_clip'] for r in rows if r['combate'] == '1'}):
            group = [r for r in rows if r['combate'] == '1' and r['cam_clip'] == clip and r['cam_modo'] == '7']
            if group:
                print('PORT_CLIP', clip, min(float(r['cam_reloj']) for r in group), max(float(r['cam_reloj']) for r in group))
        for orig in report['originales']:
            group = [r for r in rows if r['combate'] == '1' and int(r['cam_clip']) == orig['cam_clip'] and r['cam_modo'] == '7']
            if group:
                best = min(group, key=lambda r: abs(float(r['cam_reloj'])-orig['cam_reloj']))
                print('PORT_CERCANO', orig['n'], {k:best[k] for k in ('cam_clip','cam_reloj','cam_ojo_x','cam_ojo_y','cam_ojo_z','cam_obj_x','cam_obj_y','cam_obj_z')})


if __name__ == '__main__':
    main()

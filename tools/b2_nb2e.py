"""Ronda 3 de B2: diagnóstico del juez y láminas de capturas archivadas, sin arrancar juegos."""
from __future__ import annotations

import argparse
from collections import Counter
from hashlib import file_digest, sha256
from io import BytesIO
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap

REPO = Path(__file__).resolve().parents[1]
TRABAJO = Path(os.environ.get('DBZ3_TRABAJO', r'E:\DBZ3HD_trabajo'))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def digest(path):
    with path.open('rb') as stream:
        return file_digest(stream, 'sha256').hexdigest()


def run(cmd, cwd, log, accepted=(0,)):
    result = subprocess.run(list(map(str, cmd)), cwd=cwd, check=False, timeout=180,
                            env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'},
                            capture_output=True, text=True, encoding='utf-8', errors='replace')
    message = result.stdout + result.stderr
    with log.open('x', encoding='utf-8') as stream:
        stream.write(json.dumps(list(map(str, cmd)), ensure_ascii=False) + f'\nrc={result.returncode}\n' + message)
    require(result.returncode in accepted, f'rc={result.returncode}: {log}\n{message[-1800:]}')
    return result.returncode, message


def command(repo, *args):
    return [sys.executable, '-B', '-X', 'utf8', repo / 'tools' / args[0], *args[1:]]


def current_audit(repo, trabajo, work, estados=None, iso=None, cxx=None):
    fabrica = trabajo/'fabrica'
    latest = fabrica/'AP_HB2B/verificacion_n0zopu1l/juego_carga_fase7.jsonl'
    archive = fabrica/'HB2/salida/pm23'
    require(latest.is_file() and all((archive/n).is_dir() for n in ('A_original_pal', 'B_port_real_r20')),
            f'faltan los archivos históricos de AP_HB2B/HB2 en {fabrica}; revisa --trabajo')
    extra = (['--estados', estados] if estados else []) + (['--iso', iso] if iso else [])
    compiler = ['--cxx', cxx] if cxx else []
    nb2d = ('b2_nb2d/b2.py', '--repo', repo)
    code, _ = run(command(repo, *nb2d, 'bateria', '--salida', work/'bancos', *extra, *compiler),
                  repo, work/'bancos.log', (0, 1, 2))
    banks = read(work/'bancos/resultado.json')['bancos']
    events_path = work/'bancos/eventos/eventos.json'
    pending_events = [dict(campo=k, motivo=reason) for k, ok, reason in read(events_path)['filas']['serie'] if ok is None]
    off_path = work/'animacion_off'
    run(command(repo, *nb2d, 'animacion', '--anim-off', '--salida', off_path, *extra, *compiler),
        repo, work/'animacion_off.log', (0, 1, 2))
    load = {}
    for name, flags in [('serie', ()), ('slot_pal', ('--slot-pal',))]:
        cmd = command(repo, 'b2_pm23_carga.py', '--salida', work/name, *flags, *compiler,
                      *(['--sstates', estados] if estados else []))
        run(cmd, repo, work/f'carga-{name}.log', (0, 1, 2))
        load[name] = read(work/name/'medicion.json')
    ref = work/'slot_pal/A_pal/baseline_estado_fase7.jsonl'
    archived = work/'juez-f7.json'
    run(command(repo, 'b2_juez.py', ref, latest, '--salida', archived),
        repo, work/'juez-f7.log', (0, 1, 2))
    historic = work/'juez-r20'
    run(command(repo, 'b2_juez_bateria.py', '--a', archive/'A_original_pal', '--b', archive/'B_port_real_r20',
                '--salida', historic), repo, work/'juez-r20.log', (0, 1, 2))
    f7 = read(archived)['filas']
    outdated = {p.stem: dict(Counter('igual' if r['ok'] is True else 'distinto' if r['ok'] is False else 'pendiente'
                                    for r in read(p)['filas'])) for p in sorted(historic.glob('juez_fase*.json'))}
    result = dict(decision='MANTENER_OFF', bateria_rc=code, bancos=banks,
                  animacion_off=read(off_path/'resultado.json')['bancos']['animacion']['resumen'],
                  eventos_pendientes=pending_events,
                  carga={k: {str(f['fase']): {n:f[n] for n in ('diferencias', 'piezas_solo_a', 'piezas_solo_b')}
                             for f in v['fases']} for k,v in load.items()},
                  carga_codigos={k: v['codigo_juez'] for k,v in load.items()},
                  f7_archivado=dict(referencia=str(ref), port=str(latest), port_sha256=digest(latest),
                                   filas_juez=f7,
                                   limite='Si A no midió turno, un FAIL bruto null vs 1 no es una diferencia observada.'),
                  r20_historico=dict(fases=outdated,
                                    limite='PAL de carga repetido después de órdenes más cierre aislado. R20 no acredita el árbol actual.'))
    save(work/'decision.json', result)
    print(f'Bancos rc={code}; PM-23 carga serie/PAL: {result["carga_codigos"]}; '
          f'{len(pending_events)} campos de eventos pendientes. Propuesta: MANTENER_OFF.')
    return result


def source_path(value, manifest, repo, trabajo):
    if value.startswith('repo:'):
        return repo / value.removeprefix('repo:')
    if value.startswith('trabajo:'):
        return trabajo / value.removeprefix('trabajo:')
    return manifest.parent / value


def sheets(manifest, repo, trabajo, work):
    from PIL import Image, ImageDraw, ImageFont

    cases = read(manifest)
    require(isinstance(cases, list) and cases, 'manifiesto de capturas vacío o inválido')
    used = set()
    results = []
    windows_font = Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts/segoeui.ttf'
    def sized(size):
        for name in (str(windows_font), 'DejaVuSans.ttf'):
            try:
                return ImageFont.truetype(name, size)
            except OSError:
                pass
        return ImageFont.load_default()
    font, big = sized(19), sized(25)
    for case in cases:
        key = case['id']
        require(re.fullmatch(r'[a-z0-9-]+', key) and key not in used, f'id no válido o repetido: {key}')
        used.add(key)
        title, note = case['titulo'], case['limite']
        panels = case['capturas']
        hashes = case.get('sha256', {})
        require(2 <= len(panels) <= 4 and hashes.keys() <= panels.keys(), f'{key}: paneles o huellas inválidos')
        width, height, gap = 480, 300, 12
        canvas_width = (width + gap)*len(panels) + gap
        lines = textwrap.wrap(note, width=max(72, canvas_width//11))
        top = 48 + len(lines)*24
        canvas = Image.new('RGB', (canvas_width, top+height+54), '#141922')
        draw = ImageDraw.Draw(canvas)
        draw.text((gap, 9), title, fill='#f5f7fa', font=big)
        for j, line in enumerate(lines):
            draw.text((gap, 43+24*j), line, fill='#ffd98c', font=font)
        meta = []
        for i, (label, value) in enumerate(panels.items()):
            x = (width + gap)*i + gap
            draw.text((x, top+6), label, fill='#f5f7fa', font=font)
            y = top+32
            draw.rectangle((x, y, x+width-1, y+height-1), fill='#242e3b')
            if value is None:
                require(label not in hashes, f'{key}: huella sin archivo')
                draw.text((x+20, y+height//2-14), 'No hay captura comparable', fill='#ffd98c', font=big)
                meta.append(dict(etiqueta=label, estado='PENDIENTE', archivo=None))
                continue
            path = source_path(value, manifest, repo, trabajo).resolve()
            if not path.is_file():
                draw.text((x+20, y+height//2-14), 'Archivo no disponible', fill='#ffd98c', font=big)
                meta.append(dict(etiqueta=label, estado='PENDIENTE', archivo=str(path)))
                continue
            raw = path.read_bytes()
            h = sha256(raw).hexdigest()
            require(label not in hashes or h == hashes[label], f'{key}: huella incorrecta de {path}')
            with Image.open(BytesIO(raw)) as image:
                dims = image.size
                shown = image.convert('RGB')
            shown.thumbnail((width,height), Image.Resampling.LANCZOS)
            canvas.paste(shown, (x+(width-shown.width)//2, y+(height-shown.height)//2))
            meta.append(dict(etiqueta=label, estado='ARCHIVO_VALIDO', archivo=str(path), sha256=h,
                             dimensiones=dims, dimensiones_lamina=shown.size))
        output = work/f'{key}.png'
        with output.open('xb') as stream:
            canvas.save(stream, 'PNG')
        results.append(dict(id=key, titulo=title, limite=note, juicio_visual='PENDIENTE_REVISION',
                            lamina=output.name, sha256=digest(output), capturas=meta))
    save(work/'capturas.json', results)
    return results


def selfcheck():
    from PIL import Image

    with tempfile.TemporaryDirectory(prefix='nb2e-selfcheck-') as base:
        root = Path(base)
        original = root/'a.png'
        Image.new('RGB', (4, 3), (23, 45, 67)).save(original)
        fixture = dict(id='test', titulo='Prueba', limite='Sin port', capturas={'PAL': 'a.png', 'Port': None},
                       sha256={'PAL': digest(original)})
        manifest = root/'entrada.json'
        save(manifest, [fixture])
        results = sheets(manifest, root, root, root)
        require([x['estado'] for x in results[0]['capturas']] == ['ARCHIVO_VALIDO', 'PENDIENTE'], 'falso PASS sin port')
        require(results[0]['juicio_visual'] == 'PENDIENTE_REVISION', 'no se debe declarar paridad visual')
        with Image.open(root/'test.png') as panel:
            require(any(rgb == (23, 45, 67) for _, rgb in panel.getcolors(panel.width * panel.height)),
                    'lámina no contiene píxeles de la referencia')
        fixture['sha256']['PAL'] = '0'*64
        control = root/'control.json'
        save(control, [fixture])
        try:
            sheets(control, root, root, root)
        except ValueError as exc:
            require('huella incorrecta' in str(exc), exc)
        else:
            raise ValueError('debía rechazar la huella alterada')
    print('Autoprueba OK: lámina real, hueco sin port y rechazo de huella alterada.')


def parser():
    p = argparse.ArgumentParser(description=__doc__, epilog='0: salida generada; 1/2: el banco conserva diferencias/pendientes; 3: error.')
    p.add_argument('--repo', type=Path, default=REPO, help='raíz del proyecto (antes del subcomando)')
    p.add_argument('--self-check', '--autoprueba', dest='self_check', action='store_true', help='prueba sin ELF, ISO, juego ni compilador')
    sub = p.add_subparsers(dest='modo')
    a = sub.add_parser('diagnostico', help='repite los bancos instalados y conserva los pendientes originales')
    a.add_argument('--estados', type=Path, help='carpeta con fase1.p2s a fase9.p2s')
    a.add_argument('--iso', type=Path, help='ISO PAL B2; por defecto la configurada en NB2D')
    a.add_argument('--cxx', help='compilador; por defecto el seleccionado por cada banco')
    v = sub.add_parser('capturas', help='genera láminas y procedencia; no da PASS visual por tener imágenes')
    v.add_argument('--manifiesto', type=Path, help='JSON de paneles; por defecto el manifiesto archivado de NB2E')
    for s in (a, v):
        s.add_argument('--trabajo', type=Path, default=TRABAJO, help='raíz del archivo local de evidencias')
        s.add_argument('--salida', type=Path, help='carpeta nueva con padre existente (temporal si se omite)')
    return p


def output_dir(requested):
    if requested is None:
        return Path(tempfile.mkdtemp(prefix='nb2e-'))
    path = requested.resolve()
    path.mkdir()
    return path


def main(argv=None):
    p = parser()
    a = p.parse_args(argv)
    if a.self_check and a.modo:
        p.error('--self-check no se combina con subcomandos')
    try:
        if a.self_check:
            selfcheck()
            return 0
        if not a.modo:
            p.print_help()
            return 0
        work = output_dir(a.salida)
        print(f'Evidencia: {work}', flush=True)
        repo, trabajo = a.repo.resolve(), a.trabajo.resolve()
        if a.modo == 'capturas':
            manifest = a.manifiesto or trabajo/'fabrica/NB2E/casos_visuales.json'
            print(f'{len(sheets(manifest.resolve(), repo, trabajo, work))} láminas; juicio visual pendiente.')
            return 0
        result = current_audit(repo, trabajo, work, a.estados.resolve() if a.estados else None,
                               a.iso.resolve() if a.iso else None, a.cxx)
        codes = (result['bateria_rc'], *result['carga_codigos'].values())
        return next((n for n in (1, 2) if n in codes), 0)
    except (OSError, ValueError, KeyError, TypeError, ImportError, subprocess.SubprocessError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())

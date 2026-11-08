"""Una intro de escenario: observa UV/posicion del fondo y banco ambiental en RAM.

No escribe memoria, mods ni guiones. Muestreo asincrono, NO parejas PS2 ni audio
reproducido. --referencia reutiliza solo un offset relativo autenticado por las
firmas inmutables y el SHA del stage.toml, nunca una direccion de otra partida.
"""
import argparse
from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import threading
import time

import pruebas as P
import solcam_pruebas as SC
from iwx_bubbles_focal import HOME, pantalla2


def descripcion(stage):
    raw = stage.read_bytes()
    text = raw.decode('utf-8')
    value, = re.findall(r'^nubes\s*=\s*"([^"]+)"', text, re.M)[:1]
    fields = value.split(',')
    size = int(fields[4])
    if not 132 <= size <= 16*1024*1024:
        raise ValueError('tamano de busqueda de #ZDD fuera de limites')
    return dict(stage=str(stage), stage_sha256=hashlib.sha256(raw).hexdigest(),
                firma=fields[0], tam=size,
                id_se=int(re.search(r'^id_se\s*=\s*(\d+)', text, re.M)[1]))


def inmutable(raw):
    # PasoCapas solo mueve X/Z y UV: comprobar Y/W/normales/color de tres ventanas.
    return b''.join(raw[o+4:o+8]+raw[o+12:o+36] for o in (0, 44, 88))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--indice', type=int, required=True)
    ap.add_argument('--mod', default='Campo IW 884')
    ap.add_argument('--esperar', choices=('uv-activo-xz-fijo', 'uv-y-xz-activos'), required=True)
    ap.add_argument('--referencia', type=Path)
    ap.add_argument('--candidato', action='store_true', help='activa solo en esta prueba la camara local de intro')
    args = ap.parse_args()
    os.environ['DBZ3_AGENTE'] = 'solIWX'
    out = HOME/f'fondo_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA', out, flush=True)
    pantalla2()
    previous = json.loads(args.referencia.read_text(encoding='utf-8')) if args.referencia else None
    rows, errors, meta = [], [], {}

    def env(_):
        values = dict(DBZ3_TEST_BATTLE=f'0,7,{args.indice}', DBZ3_COMBATE_MUNECO='quieto',
                    DBZ3_COMBATE_KI='7000', DBZ3_COMBATE_SONDA='1',
                    DBZ3_TEST_INTRO_SONDA='1', DBZ3_HUB_MODE='1',
                    DBZ3_TEST_IWX_FONDO_CAMARA='0')
        if args.candidato:
            values['DBZ3_TEST_IWX_FONDO_CAMARA'] = '1'
        return values

    def jugar(j, _):
        meta.update(descripcion(j.dest/'mods'/args.mod/'stage.toml'))
        meta['exe_sha256'] = hashlib.sha256((j.dest/'dbz3.exe').read_bytes()).hexdigest()
        meta['candidato'] = args.candidato
        if previous and previous['meta']['stage_sha256'] != meta['stage_sha256']:
            raise P.Fallo('stage.toml distinto de la referencia; no reutilizar offset')
        signature = bytes.fromhex(meta['firma'])
        stopped = threading.Event()
        origin = time.monotonic()

        def sample():
            offset = previous['meta']['offset'] if previous else None
            found_zdd = None
            try:
                while not stopped.wait(.008):  # observacion finita, no espera de trabajo externo
                    mgr_raw = P.leer(j.h, P.GBASE+0x823D6CF4, 4)
                    if not mgr_raw:
                        continue
                    mgr = int.from_bytes(mgr_raw, 'big')
                    data = P.leer(j.h, P.GBASE+mgr, 36000) if mgr else None
                    zraw = P.leer(j.h, P.GBASE+0x823A3DA0, 4)
                    if not data or len(data) != 36000 or not zraw:
                        continue
                    state = struct.unpack_from('>I', data, 35128)[0]
                    zdd = int.from_bytes(zraw, 'big')
                    if state not in (1, 3, 4, 5, 8) or not 0x40000000 <= zdd < 0x50000000:
                        continue
                    if found_zdd != zdd:
                        if offset is None:
                            block = P.leer(j.h, P.GBASE+zdd, meta['tam'])
                            if not block:
                                continue
                            offset = block.find(signature)
                            if offset < 0 or block.find(signature, offset+1) >= 0:
                                raise ValueError('firma del fondo ausente o ambigua')
                        vertex = P.leer(j.h, P.GBASE+zdd+offset, 132)
                        if not vertex or inmutable(vertex) != inmutable(signature):
                            raise ValueError('offset relativo no autentica las tres ventanas')
                        found_zdd = zdd
                        meta.update(offset=offset, zdd=zdd)
                    vertex = P.leer(j.h, P.GBASE+zdd+offset, 44)
                    ambient = P.leer(j.h, P.GBASE+0x823A3E40, 4)
                    if not vertex or not ambient:
                        continue
                    camera = struct.unpack_from('>I', data, 612)[0]
                    pose = P.leer(j.h, P.GBASE+camera, 32) if camera else None
                    row = dict(t=round(time.monotonic()-origin, 6), estado=state,
                               reloj=struct.unpack_from('>f', data, 476)[0],
                               ambiente=int.from_bytes(ambient, 'big'), zdd=zdd,
                               cache_x=struct.unpack_from('>f', data, 0x190)[0],
                               cache_z=struct.unpack_from('>f', data, 0x198)[0])
                    for name, o in (('x', 0), ('y', 4), ('z', 8), ('u', 36), ('v', 40)):
                        row[name] = struct.unpack_from('>f', vertex, o)[0]
                    if pose and len(pose) == 32:
                        row.update(ojo_x=struct.unpack_from('>f', pose, 0)[0],
                                   ojo_z=struct.unpack_from('>f', pose, 8)[0])
                    rows.append(row)
            except Exception as exc:
                errors.append(f'{type(exc).__name__}: {exc}')

        thread = threading.Thread(target=sample)
        thread.start()
        try:
            j.log('tag: empieza un combate', 90)
            P._esperar_pelea(j, 90)
            P.dormir(2)
        finally:
            stopped.set()
            thread.join()
            source = j.dest/'logs'/'combate_sonda.csv'
            if source.is_file():
                shutil.copy2(source, out/'combate_sonda.csv')
            if rows:
                fields = list(dict.fromkeys(k for row in rows for k in row))
                with (out/'fondo.csv').open('x', newline='', encoding='utf-8') as f:
                    writer = csv.DictWriter(f, fieldnames=fields)
                    writer.writeheader()
                    writer.writerows(rows)
        if errors:
            raise P.Fallo('; '.join(errors))

    key = 'solIWX_fondo_intro'
    P.ESCENARIOS[key] = ('IW: UV y cielo durante una intro, observacion RAM', env, jugar)
    result = P.correr('smoke_b', key, [], None, {'timeout':'240'}, True)
    intro = [r for r in rows if r['estado'] == 1]
    fight = [r for r in rows if r['estado'] == 4]
    summary = dict(meta=meta, muestras=len(rows), estados=dict(Counter(r['estado'] for r in rows)),
                   intro_muestras=len(intro), pelea_muestras=len(fight), errores=errors,
                   intro_uv=len({(r['u'],r['v']) for r in intro}),
                   intro_xz=len({(r['x'],r['z']) for r in intro}),
                   intro_cache_xz=len({(r['cache_x'],r['cache_z']) for r in intro}),
                   intro_ojo_xz=len({(r['ojo_x'],r['ojo_z']) for r in intro if 'ojo_x' in r}),
                   pelea_uv=len({(r['u'],r['v']) for r in fight}),
                   ambiente_ids=sorted({r['ambiente'] for r in rows}),
                   paridad_ps2=False, sonido_reproducido_comprobado=False,
                   cobertura='RAM asincrona: primer vertice de una parte. No sincroniza PS2 ni prueba sonido audible.')
    reasons = []
    if len(intro) < 20 or len(fight) < 20:
        reasons.append('cobertura insuficiente de intro/pelea')
    if summary['pelea_uv'] < 2:
        reasons.append('control positivo UV de pelea ausente')
    moving = summary['intro_xz'] > 1
    if summary['intro_uv'] < 2 or summary['intro_ojo_xz'] < 20:
        reasons.append('control positivo UV/camara de intro ausente')
    if (args.esperar == 'uv-y-xz-activos') != moving:
        reasons.append(f"intro no cumple --esperar {args.esperar}")
    if summary['ambiente_ids'] != [meta.get('id_se')]:
        reasons.append('banco ambiental observado no coincide con stage.toml')
    if reasons:
        result['ok'] = False
        result['msg'] = '; '.join([result.get('msg', ''), *reasons])
    for name, value in (('medida.json', summary), ('resultado.json', result)):
        with (out/name).open('x', encoding='utf-8') as f:
            json.dump(value, f, ensure_ascii=False, indent=1, default=str, allow_nan=False)
    print(P.tabla([result]), flush=True)
    print(json.dumps(summary, ensure_ascii=False, indent=1), flush=True)
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

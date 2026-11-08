"""Censo offline de las intros instaladas; salidas y bytes solo fuera del repo.

Uso: python -B tools/iwx_escenarios.py --mods RUTA
La continuidad visual y el reloj de ejecución requieren además la prueba viva.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from datetime import datetime

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'mod center hd'), str(ROOT/'awo_tools')]
import campos_iw as CI
import escenarios_convertir as EC


def word(b, p):
    return struct.unpack_from('>I', b, p)[0]


def camera_bank(b, start=0, endian='>'):
    u = lambda p: struct.unpack_from(endian+'I', b, p)[0]
    assert b[start:start+4] == (b'#ACC' if endian == '>' else b'#AMC')
    n, table = u(start+16), u(start+20)
    clips = []
    for i in range(n):
        typ, var, frames, data = struct.unpack_from(endian+'4I', b, start+table+i*16)
        tracks = []
        for k in range(4 if typ == 0x1D else 3):
            p = u(start+data+4*k)
            if not p:
                tracks.append([])
                continue
            p += start
            count = u(p+8)
            assert count < 100000
            width = 4 if k < 2 else 2
            keys = []
            for j in range(count):
                q = p+12+j*width*4
                keys.append([u(q), *struct.unpack_from(endian+str(width-1)+'f', b, q+4)])
            tracks.append(keys)
        clips.append(dict(clip=i, tipo=typ, variante=var, cuadros=frames, pistas=tracks,
                          ultima_clave=[t[-1][0] if t else None for t in tracks]))
    return clips


def cameras(zdd):
    ob = word(zdd, word(zdd, 0x20))
    return [camera_bank(zdd, word(zdd, word(zdd, ob+0xD8)+bank*4))
             for bank in range(word(zdd, ob+0xD4))]


def camera_difference(source, port):
    """Diferencias exactas de datos, sin atribuirles una causa de ejecucion."""
    counts, examples, maximum = {}, [], {}

    def walk(a, b, path=()):
        if type(a) is not type(b):
            changed(a, b, path)
        elif isinstance(a, list):
            if len(a) != len(b):
                changed(len(a), len(b), path+('longitud',))
            for i, (left, right) in enumerate(zip(a, b)):
                walk(left, right, path+(i,))
        elif isinstance(a, dict):
            for key in sorted(a.keys() | b.keys()):
                if key not in a or key not in b:
                    changed(a.get(key), b.get(key), path+(key,))
                else:
                    walk(a[key], b[key], path+(key,))
        elif a != b:
            changed(a, b, path)

    def changed(a, b, path):
        # /clip/pistas/pista/clave/componente; componente 0 = cuadro de clave.
        field = str(path[1]) if len(path) > 1 else 'banco'
        if len(path) >= 5 and path[1] == 'pistas':
            field = 'pista_%s_%s' % (path[2], 'cuadro' if path[4] == 0 else 'valor')
        counts[field] = counts.get(field, 0)+1
        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
            maximum[field] = max(maximum.get(field, 0), abs(a-b))
        if len(examples) < 12:
            examples.append(dict(ruta='/'+ '/'.join(map(str, path)), original=a, instalado=b))

    walk(source, port)
    extras = []
    for i in range(len(source), len(port)):
        clip = {k: v for k, v in port[i].items() if k != 'clip'}
        originals = [j for j, other in enumerate(source)
                     if clip == {k: v for k, v in other.items() if k != 'clip'}]
        extras.append(dict(clip_instalado=i, identico_a_original=originals))
    return dict(diferencias_por_campo=counts, max_abs_por_campo=maximum,
                ejemplos=examples, clips_adicionales=extras)


def original_iw(mods, out, fields):
    from assets_censo import Game
    from assets_censo_iw import EU_DEFAULT
    from iwx_censo import entry
    game = Game('iw_eu', EU_DEFAULT)
    cmn = next(p for p in game.files if Path(p).name.upper() == 'DATA_CMN.AFS')
    records = []
    for field in fields:
        pack = entry(game, cmn, field+1)
        amc = next((h for h in CI.hijos(pack) if h[:4] == b'#AMC'), None)
        record = dict(campo=field, entrada_original=field+1,
                      sha256_paquete=hashlib.sha256(pack).hexdigest())
        if amc is None:
            record['estado'] = 'sin AMC propio en entrada campo+1; comprobar donante'
        else:
            source = camera_bank(amc, endian='<')
            record['sha256_amc'] = hashlib.sha256(amc).hexdigest()
            record['original'] = source
            index = CI.ENTRADAS[field]
            raw = mods/('Campo IW '+str(field))/'us/data_cmn.afs'/str(index)
            path = raw if raw.is_file() else next(f for f in raw.iterdir() if f.is_file())
            port = cameras(split(unpack(path.read_bytes(), out, 'stage'+str(index)))[0])[0]
            record['sha256_instalado'] = hashlib.sha256(path.read_bytes()).hexdigest()
            record['iguales_claves_y_descriptores'] = source == port
            record['estado'] = 'claves y descriptores iguales' if source == port else 'difieren'
            if source != port:
                record['instalado'] = port
                record['diferencias'] = camera_difference(source, port)
        records.append(record)
    result = dict(fuente=game.source, mods=str(mods), campos=records,
                  cobertura='Comparacion estatica AMC PS2 EU / ACC instalado; no prueba de reproduccion PS2.')
    (out/'camaras_originales.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(salida=str(out), campos=[{k: r[k] for k in ('campo', 'estado')} for r in records]), indent=2))


def unpack(b, out, name):
    if b[:4] == b'#AMB':
        return b
    source, dest = out/(name+'.lzx'), out/(name+'.amb')
    source.write_bytes(b)
    subprocess.run([str(EC.XBD), str(source), str(dest)],
                   check=True, capture_output=True, timeout=120)
    return dest.read_bytes()


def split(b):
    return [b[p:p+n] for p, n, _, _ in EC.hijos_amb(b, '>')]


def inspect(mods, out):
    native = {i: split(unpack(EC.entrada_afs(ROOT/'us/data_cmn.afs', i), out, 'native'+str(i)))
              for i in (44, 48, 54)}
    donor_hash = {hashlib.sha256(v[3]).hexdigest(): i for i, v in native.items()}
    rows, details = [], {}
    expected = [(folder, game, group, title, EC.ENTRADA_BASE+(20 if game == 'b1' else 0)+group)
                for game, group, folder, title, _ in CI.B12]
    expected += [('Campo IW '+str(k), 'iw', k, v[0], CI.ENTRADAS[k]) for k, v in CI.CAMPOS.items()]
    known = {v[0] for v in expected}
    # Incluir altas instaladas adicionales (por ejemplo PSP), sin atribuirles origen.
    for p in sorted(mods.glob('*/stage.toml')):
        if p.parent.name not in known and not p.parent.name.startswith('_'):
            expected.append((p.parent.name, 'origen por comprobar', None, p.parent.name, None))
    for folder, game, group, title, index in expected:
        path = mods/folder
        if index is None and (path/'stage.toml').exists():
            import re
            m = re.search(r'^entrada\s*=\s*(\d+)', (path/'stage.toml').read_text(encoding='utf-8'), re.M)
            index = int(m[1]) if m else None
        files = list((path/'us/data_cmn.afs'/str(index)).glob('*'))
        raw = path/'us/data_cmn.afs'/str(index)
        source = raw if raw.is_file() else next((f for f in files if f.is_file()), None)
        row = dict(carpeta=folder, origen=game, grupo=group, titulo=title, entrada=index,
                   instalada=source is not None, activa=source is not None and not (path/'.disabled').exists(),
                   intro='sin medir en ejecucion', camara='sin banco', fondo='sin comprobar',
                   sonido='sin comprobar', otros='sin comprobar')
        if source:
            b = source.read_bytes()
            parts = split(unpack(b, out, 'stage'+str(index)))
            cams = cameras(parts[0])
            sh = hashlib.sha256(parts[3]).hexdigest()
            donor = donor_hash.get(sh)
            details[folder] = dict(sha256_archivo=hashlib.sha256(b).hexdigest(), spx_sha256=sh,
                                   spx_donante=donor, camaras=cams)
            row['camara'] = '; '.join(','.join(str(c['cuadros']) for c in bank) for bank in cams)
            row['otros'] = 'SPX del donante '+str(donor) if donor else 'SPX sin donante identificado'
        rows.append(row)
    for i, parts in native.items():
        details['nativo '+str(i)] = dict(spx_sha256=hashlib.sha256(parts[3]).hexdigest(), camaras=cameras(parts[0]))
    (out/'censo.json').write_text(json.dumps(dict(mods=str(mods), filas=rows, detalle=details), ensure_ascii=False, indent=2), encoding='utf-8')
    with (out/'censo.csv').open('w', encoding='utf-8-sig', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    print(json.dumps(dict(salida=str(out), filas=rows), ensure_ascii=False, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mods', type=Path, default=ROOT/'out/build/win-amd64-release/mods')
    p.add_argument('--original-iw', type=int, nargs='+', help='contrastar campos IW contra AMC original EU')
    args = p.parse_args()
    out = Path('D:/tmp/solIWX')/('escenarios_'+datetime.now().strftime('%Y%m%d_%H%M%S'))
    out.mkdir(parents=True)
    # hd_nativo/compresores auxiliares heredan el temporal propio.
    os.environ['TMP'] = os.environ['TEMP'] = str(out)
    if args.original_iw:
        original_iw(args.mods, out, args.original_iw)
    else:
        inspect(args.mods, out)


if __name__ == '__main__':
    main()

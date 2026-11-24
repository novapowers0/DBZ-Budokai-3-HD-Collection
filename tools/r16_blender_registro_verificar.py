"""Comprueba registro/actualizacion local y cobertura completa de trajes."""
import argparse
import json
from pathlib import Path
import sys
import tomllib

import blender_partes_b3 as bp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--formas', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    args = ap.parse_args()
    if args.salida.exists() or not args.salida.parent.is_dir():
        raise ValueError('La salida debe ser nueva y su carpeta padre debe existir')
    cat = args.salida / 'mod center hd/cac/piezas_cac.toml'
    cat.parent.mkdir(parents=True)
    unrelated = '[[pieza]]\ntipo = "ajena"\nid = 42\nnombre = "Conservar"\nidx_afs = -1\narchivo = "otro"\n'
    cat.write_text(unrelated, encoding='utf-8')
    exports = sorted(args.formas.glob('*/editado.json'))
    assert len(exports) == 6
    for name in ('Conjunto inicial', 'Conjunto actualizado'):
        bp.registrar(exports, args.salida, 3080, name)
        rows = tomllib.loads(cat.read_text(encoding='utf-8'))['pieza']
        assert len(rows) == 2 and rows[0]['id'] == 42
        item = rows[1]
        assert item['id'] == 3080 and item['nombre'] == name and item['archivo'] == 'local:27:0'
        reg = json.loads((args.salida / 'user_data/dbz3/partes_blender/registro.json').read_text(encoding='utf-8'))
        assert len(reg['piezas']) == 1 and reg['piezas'][0]['trajes'] == [0]
    po, vh = bp._modulos()
    import tempfile
    afs = vh.rb.Afs(str(bp.ROOT / 'us/data_cmn.afs'), tempfile.mkdtemp(prefix='registro_formas_'))
    ficha = dict(cuerpo=27, b3_torso=3080)
    for fid in range(181, 187):
        data, info = po.aplicar(afs.entry(fid), ficha, args.salida)
        assert info['blender_local'] and len(data) > 0
    try:
        po.aplicar(afs.entry(187), ficha, args.salida)
    except ValueError as exc:
        assert 'sin modelo para esta forma/traje' in str(exc)
    else:
        raise AssertionError('Se acepto un traje no exportado')
    assert cat.read_text(encoding='utf-8').startswith(unrelated)
    print('PASS registro Blender: seis formas, solo traje 0, actualizacion sin duplicados y catalogo ajeno intacto')


if __name__ == '__main__':
    main()

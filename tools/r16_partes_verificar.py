"""Verificacion del contrato binario con los modelos locales del jugador."""
import argparse
import json
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'mod center hd' / 'cac'), str(ROOT / 'mod center hd'), str(ROOT / 'awo_tools')]
import vista_heroe as vh
import partes_originales as po
from awg_vertex_buffer import AwgVertexBuffer


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--salida', type=Path)
    ap.add_argument('--todos-trajes', action='store_true')
    args = ap.parse_args()
    afs = vh.rb.Afs(str(ROOT / 'us' / 'data_cmn.afs'), tempfile.mkdtemp(prefix='partes_verificar_'))
    casos = []
    modelos = ({fid: body for body in po.CUERPOS for fid in vh.rb.IDS[body]['models']}
               if args.todos_trajes else
               {fid: 27 if fid >= 181 else 38 for fid in (181, 182, 183, 184, 185, 186, 166, 167, 168)})
    for fid, body in modelos.items():
        data = afs.entry(fid)
        for item in [((i, z),) for i, z, _, _ in po.CATALOGO] + [((3000, 0), (3020, 1), (3040, 2), (3061, 3))]:
            ficha = dict(cuerpo=body)
            for i, z in item:
                ficha['b3_' + po.ZONAS[z]] = i
            result, info = po.aplicar(data, ficha)
            assert len(result) == len(data)
            a = AwgVertexBuffer._parse(data)
            allowed = np.zeros(len(data), bool)
            for awg in range(a.n_awg):
                aa = AwgVertexBuffer._parse(data, awg)
                bb = AwgVertexBuffer._parse(result, awg)
                assert aa.indices() == bb.indices()
                assert aa.bone_labels() == bb.bone_labels()
                assert aa.bind_worlds() == bb.bind_worlds()
                for v in range(aa.n):
                    at = aa.vb0 + 44 * v
                    allowed[at:at + 12] = True
                    allowed[at + 20:at + 32] = True
                for va, vb in zip(aa.vertices(), bb.vertices()):
                    assert va['bone'] == vb['bone'] and va['w'] == vb['w'] and va['uv'] == vb['uv']
                    assert np.isfinite(vb['pos']).all() and np.isfinite(vb['nrm']).all()
            old = np.frombuffer(data, np.uint8)
            new = np.frombuffer(result, np.uint8)
            assert np.array_equal(old[~allowed], new[~allowed]), 'Cambio fuera de pos/nrm'
            assert sum(info['ventanas_por_awg']) > 0, (fid, item, 'variante sin efecto')
            casos.append(dict(fid=fid, ids=[i for i, _ in item], **info))
        print('PASS fid %d: 13 piezas y combinacion; indices, huesos, pesos, UV, materiales y texturas intactos' % fid, flush=True)
    assert po.aplicar(data, dict(cuerpo=0, b3_torso=3000))[0] == data
    print('PASS originales: %d casos, %d modelos y todas sus expresiones' % (len(casos), len(modelos)))
    if args.salida:
        args.salida.parent.mkdir(parents=True, exist_ok=True)
        args.salida.write_text(json.dumps(casos, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

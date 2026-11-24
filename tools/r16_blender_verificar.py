"""Prueba local completa de Blender para las seis formas Freezer, sin ejecutar juego."""
import argparse
import json
import subprocess
from pathlib import Path
import blender_partes_b3 as bp


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--blender', type=Path, default=Path('C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'))
    args = ap.parse_args()
    if args.salida.exists() or not args.salida.parent.is_dir():
        raise ValueError('La salida debe ser nueva y su padre debe existir')
    args.salida.mkdir()
    root = Path(__file__).resolve().parents[1]
    receta = root / 'tools/blender_partes_ejemplos/freezer_original.toml'
    for fid in range(181, 187):
        d = args.salida / str(fid)
        d.mkdir()
        bp.preparar(receta, root / 'us', d / 'fuente', fid)
        cmds = [
            [str(args.blender), '--background', '--factory-startup', '--python-exit-code', '1', '--python', str(root / 'tools/blender_partes.py'), '--', str(d / 'fuente/blender.toml'), '--salida', str(d / 'rig'), '--sin-render'],
            [str(args.blender), '--background', str(d / 'rig/pieza.blend'), '--python-exit-code', '1', '--python', str(root / 'tools/r16_blender_editar.py'), '--', str(d / 'editado.json')],
        ]
        for i, cmd in enumerate(cmds):
            r = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=180)
            (d / ('blender%d.log' % i)).write_text(r.stdout + r.stderr, encoding='utf-8')
            if r.returncode:
                raise RuntimeError((r.stdout + r.stderr)[-1500:])
        original = json.loads((d / 'rig/pieza.b3mesh.json').read_text(encoding='utf-8'))
        edited = json.loads((d / 'editado.json').read_text(encoding='utf-8'))
        if original['meshes'] == edited['meshes']:
            raise RuntimeError('La edicion de Blender no cambio el modelo')
        bp.integrar(d / 'editado.json', d / 'editado.bin')
        before = (d / 'fuente/fuente.amb').read_bytes()
        after = (d / 'editado.bin').read_bytes()
        po, vh = bp._modulos()
        allowed = bytearray(len(before))
        m = vh.mr.Model(before)
        for awg in range(m.avb.n_awg):
            n = vh.mr.Model(before, awg=awg)
            for i in range(len(n.pos)):
                at = n.avb.vb0 + i * 44
                allowed[at:at+12] = b'\1' * 12
                allowed[at+20:at+32] = b'\1' * 12
        if len(before) != len(after) or any(a != b and not ok for a, b, ok in zip(before, after, allowed)):
            raise RuntimeError('La edicion cambio datos ajenos a posiciones/normales')
        print('PASS Blender forma %d: edicion real, rig, expresiones, skin, UV y materiales' % fid, flush=True)
    print('PASS Blender completo: seis formas editadas y reimportadas; sin arte distribuido')


if __name__ == '__main__':
    main()

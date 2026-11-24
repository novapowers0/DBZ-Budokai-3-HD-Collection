"""Contratos visuales del importador, sin arrancar Blender ni el juego."""
import ast
import io
import json
import math
from pathlib import Path
import sys
from types import SimpleNamespace as NS

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent


def main():
    # Cargar las funciones puras del worker sin simular el módulo bpy.
    tree = ast.parse((ROOT / 'mod center hd/obj_blender.py').read_text(encoding='utf-8'))
    functions = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name != 'main']
    scope = dict(np=np, math=math)
    exec(compile(ast.Module(body=functions, type_ignores=[]), '<worker_puro>', 'exec'), scope)
    base = np.array([[.8, .5, .3], [.16, .22, .9], [.025, .025, .03], [.92, .92, .92]])
    shades = np.concatenate([color[None] * np.linspace(.72, 1, 60)[:, None] for color in base])
    cleaned, palette = scope['paleta_cel'](shades)
    assert np.isfinite(cleaned).all()
    assert len(np.unique(cleaned[:60], axis=0)) == 1, 'la piel conserva iluminación pintada'
    assert len(np.unique(cleaned[60:120], axis=0)) == 1, 'el azul conserva iluminación pintada'
    assert not np.allclose(cleaned[0], cleaned[60]), 'se han mezclado dos colores'
    assert np.max(cleaned[120:180]) < .04, 'las líneas negras se han vuelto grises'
    assert np.min(cleaned[180:]) > .3, 'el blanco del ojo se ha oscurecido'
    assert len(palette) <= 9
    print('PASS paleta: luz eliminada, colores separados, negro y blanco conservados')
    ink = np.ones((9*9, 3), dtype=float)
    ink[4*9+4] = [.2,.2,.2]
    assert scope['afinar_tinta'](ink, 9) == 1
    assert ink[4*9+4].max() < .1 and np.all(ink[0] == 1), 'la tinta engordó o cambió el fondo'
    print('PASS tinta: línea oscurecida, sin engordarla ni alterar su fondo')

    uv = [NS(uv=(4.5/16, 4.5/16)), NS(uv=(10.5/16, 4.5/16)), NS(uv=(4.5/16, 10.5/16))]
    mesh = NS(loop_triangles=[NS(loops=[0, 1, 2])], uv_layers=NS(active=NS(data=uv)),
              calc_loop_triangles=lambda: None)
    pixels = np.zeros((16*16, 4), dtype=np.float32)
    pixels[:, :3] = [0, 0, 1]
    x, y = np.meshgrid(np.arange(16), np.arange(16))
    occupied = (x >= 4) & (y >= 4) & (x+y <= 14)
    pixels.reshape(16, 16, 4)[occupied, :3] = [1, 0, 0]
    before = pixels.copy()
    count = scope['dilatar_islas'](pixels, mesh, 16, margin=3)
    assert count > 0
    assert np.array_equal(before.reshape(16,16,4)[occupied], pixels.reshape(16,16,4)[occupied])
    assert np.array_equal(pixels.reshape(16,16,4)[5,3,:3], [1,0,0]), 'no extendió el color de la isla'
    assert len(np.unique(pixels[:, :3], axis=0)) == 2, 'el padding creó colores mezclados'
    print('PASS padding: relleno UV sin mezcla ni cambios en islas ocupadas')

    positions = [(0,0,0), (1,0,0), (0,1,0), (0,0,0), (0,0,1), (1,0,0)]
    mesh = NS(vertices=[NS(co=np.array(p)) for p in positions],
              loops=[NS(normal=np.array(n)) for n in [(0,0,1)]*3 + [(0,1,0)]*3],
              loop_triangles=[NS(index=0, vertices=[0,1,2], loops=[0,1,2]),
                              NS(index=1, vertices=[3,4,5], loops=[3,4,5])])
    normals = scope['normales_suaves'](mesh)
    assert np.allclose(normals[0], [0,0,1]) and np.allclose(normals[3], [0,1,0])
    mesh.vertices[4].co = np.array([0,-1.,0])
    mesh.loops[3].normal = mesh.loops[4].normal = mesh.loops[5].normal = np.array([0,0,1])
    normals = scope['normales_suaves'](mesh)
    assert np.allclose(normals[0], normals[3]), 'costura UV en superficie coplanar'
    print('PASS normales: costuras UV soldadas y aristas de 90 grados conservadas')

    if len(sys.argv) > 1:
        report = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
        work = Path(report['trabajo'])
        mesh = json.loads((work/'malla.json').read_text(encoding='utf-8'))
        normals = np.array([v['normal'] for v in mesh['vertices']])
        assert np.max(np.abs(np.linalg.norm(normals, axis=1)-1)) < 1e-5
        assert report['modelo']['verificacion'] == 'PASS'
        assert report['blender']['padding_texeles'] > 0
        assert len(report['blender']['paleta_srgb']) <= 12
        # Comprobar el DDS realmente empaquetado; PNG y retratos siguen opacos.
        sys.path.insert(0, str(ROOT/'awo_tools'))
        import sdbh_model as S
        tpl = S.Template(str(work/'personaje.amb'))
        atlas = np.asarray(Image.open(io.BytesIO(tpl.textures[0][3])).convert('RGBA'))
        masked = any(v.get('sin_rim', False) for v in mesh['vertices'])
        if masked:
            assert set(np.unique(atlas[..., 3])) == {0, 255}, 'mascara alfa no binaria'
            # Lejos de bordes DXT, la piel conserva toon y las telas oscuras su base.
            skin = (atlas[..., 0] > 180) & (atlas[..., 1] > 100) & (atlas[..., 2] < 160)
            dark = atlas[..., :3].max(2) < 60
            assert np.mean(atlas[..., 3][skin] == 0) > .97, 'la piel perdio toon'
            assert np.mean(atlas[..., 3][dark] == 255) > .97, 'la tela se ennegrece con N=0'
        else:
            assert np.max(atlas[..., 3]) == 0, 'el DDS vuelve a omitir el toon'
        assert np.max(atlas[..., :3]) > 180, 'se ha perdido el RGB al quitar alfa'
        png = np.asarray(Image.open(work/'atlas.png').convert('RGBA'))
        assert np.min(png[..., 3]) == 255, 'la vista previa se ha hecho transparente'
        assert tpl.textures[0][:2] == (1024, 1024)
        import tomllib
        config = tomllib.loads((Path(report['mod'])/'personaje.toml').read_text(encoding='utf-8'))
        assert config['personaje']['obj_cel'] is True, 'se perdió el acabado opcional del mod'
        assert report['modelo']['vertices_sin_brillo'] > 0
        assert any(int.from_bytes(m[0x30:0x34], 'big') == 2 for m in tpl.materials), 'el pelo aún depende de bordes UV'
        black = np.asarray(Image.open(io.BytesIO(tpl.textures[2][3])).convert('RGBA'))
        if masked:
            assert black[..., :3].max() < 40 and np.min(black[..., 3]) == 255
            assert 2 <= len(np.unique(black.reshape(-1, 4), axis=0)) <= 8, 'planos del pelo perdidos'
            from awg_vertex_buffer import AwgVertexBuffer
            written = AwgVertexBuffer.load(str(work/'personaje.amb')).vertices()
            lengths = np.linalg.norm(np.array([v['nrm'] for v in written]), axis=1)
            assert np.any(lengths == 0) and np.any(lengths > .99), 'mascara global o ausente'
            assert np.all((lengths < 1e-6) | (np.abs(lengths-1) < 1e-5))
            assert report['modelo']['bind_error'] < 1e-5
            print('PASS mascara individual: normales nulas y unitarias, piel toon, telas sin rim y pelo en planos oscuros')
            if any('color_sin_rim' in v for v in mesh['vertices']):
                cloth = np.asarray(Image.open(io.BytesIO(tpl.textures[3][3])).convert('RGBA'))
                assert tpl.textures[3][:2] == (64, 64)
                assert np.min(cloth[..., 3]) == 255 and cloth[..., :3].max() < 64
                assert 3 <= len(np.unique(cloth.reshape(-1, 4), axis=0)) <= 65
                assert any(int.from_bytes(m[0x30:0x34], 'big') == 3 for m in tpl.materials)
                print('PASS ropa: planos cel oscuros empaquetados, sin blanco y sin depender del rim global')
        else:
            assert black[..., :3].max() < 16 and len(np.unique(black.reshape(-1,4), axis=0)) == 1
        print('PASS modelo real: normales unitarias, atlas 1024, padding, DDS toon y RGB intactos')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

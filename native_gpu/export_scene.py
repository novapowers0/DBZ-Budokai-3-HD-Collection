"""Convert the player's AFS model to host vertex/index buffers and RGBA textures.

No game files are shipped with this tool. AWO parsing comes from this project's
existing model_render.py; the new renderer consumes a small, endian-neutral
scene format rather than Xbox GPU commands or shaders.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
DECOMPRESSOR = ROOT / 'tools' / 'xbdecompress.exe'
sys.path.insert(0, str(ROOT / 'mod center hd'))
from model_render import Model, native_accessory  # noqa: E402


def extract(afs, index, scratch):
    with afs.open('rb') as stream:
        header = stream.read(8)
        if header[:4] != b'AFS\0':
            raise ValueError('Not an AFS archive')
        count = struct.unpack_from('<I', header, 4)[0]
        if not 0 <= index < count:
            raise ValueError(f'AFS entry {index} outside {count} entries')
        stream.seek(8 + index * 8)
        offset, size = struct.unpack('<II', stream.read(8))
        if offset + size > afs.stat().st_size:
            raise ValueError('Truncated AFS entry')
        stream.seek(offset)
        compressed = stream.read(size)
    if compressed.startswith(b'#AMB'):
        return compressed
    source, target = scratch / f'{index}.lzx', scratch / f'{index}.bin'
    source.write_bytes(compressed)
    result = subprocess.run([str(DECOMPRESSOR), '/Y', str(source), str(target)],
                            capture_output=True, text=True)
    if result.returncode or not target.exists():
        raise RuntimeError(f'LZX decompression failed: {result.stdout} {result.stderr}')
    data = target.read_bytes()
    if not data.startswith(b'#AMB'):
        raise ValueError('Decoded entry is not an AMB model')
    return data


def export(afs, entry, output):
    with tempfile.TemporaryDirectory(prefix='dbz3-native-') as temporary:
        scratch = Path(temporary)
        data = extract(afs, entry, scratch)
        model = Model(data)
        accessory = native_accessory(model)
        if accessory:
            idx, bone = accessory
            model.attach(extract(afs, idx, scratch), bone)
        pose = model.arms_down()
        pose.update(model.mouth_closed())
        positions, normals = model.posed(pose)
        vertices = np.column_stack((positions, normals, model.uv)).astype('<f4')
        if not np.isfinite(vertices).all():
            raise ValueError('Nonfinite vertices')
        indices, draws = [], []
        for material in sorted(set(model.tri_mat.tolist()), key=lambda k: model.mats[k]['blend']):
            triangles = model.tris[model.tri_mat == material]
            if triangles.min() < 0 or triangles.max() >= len(vertices):
                raise ValueError('Index outside vertex buffer')
            start = len(indices)
            indices.extend(triangles.ravel().tolist())
            info = model.mats[material]
            # Invalid / absent texture is represented explicitly, never an accidental SRV index.
            base = info['tex'] if info['tex'] < len(model.tex) else 0xFFFFFFFF
            ramp = info['shader'] if info['shader'] < len(model.tex) else 0xFFFFFFFF
            draws.append((start, len(indices) - start, base, ramp, int(info['blend']),
                          *info['diffuse'].tolist()))
        textures = [(np.clip(tex * 255, 0, 255).round().astype('uint8')) for tex in model.tex]
        bounds = np.concatenate((positions.min(0), positions.max(0))).astype('<f4')
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open('wb') as stream:
            stream.write(struct.pack('<8s4I6f', b'DBZNGPU1', len(vertices), len(indices),
                                     len(draws), len(textures), *bounds))
            stream.write(vertices.tobytes())
            stream.write(np.asarray(indices, dtype='<u4').tobytes())
            for draw in draws:
                stream.write(struct.pack('<5I3f', *draw))
            for texture in textures:
                height, width, channels = texture.shape
                assert channels == 4
                stream.write(struct.pack('<3I', width, height, texture.nbytes))
                stream.write(texture.tobytes())
        metadata = {'entry': entry, 'vertices': len(vertices), 'triangles': len(indices) // 3,
                    'draws': len(draws), 'textures': len(textures), 'bounds': bounds.tolist(),
                    'source_model_sha256': hashlib.sha256(data).hexdigest(),
                    'scene_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
                    'pose': 'Arms down in bind hierarchy; no gameplay animation captured',
                    'parser': 'Upstream model_render.Model / SkinSlots / DrawTable',
                    'renderer': 'Direct3D 12, custom HLSL, no rexgpu or Xenia dependency'}
        output.with_suffix('.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
        print(json.dumps(metadata, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--afs', type=Path, required=True)
    parser.add_argument('--entry', type=int, default=264)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--decompressor', type=Path, default=DECOMPRESSOR)
    args = parser.parse_args()
    DECOMPRESSOR = args.decompressor.resolve()
    export(args.afs.resolve(), args.entry, args.output.resolve())

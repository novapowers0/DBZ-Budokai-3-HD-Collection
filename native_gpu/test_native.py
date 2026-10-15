"""Run real hardware render tests, C ABI frame updates, and malformed scene tests."""
import argparse
import ctypes as ct
import json
import math
from pathlib import Path
import struct
import subprocess
import time

import numpy as np
from PIL import Image


def pixels(path):
    image = np.asarray(Image.open(path).convert('RGB'))
    background = image[0, 0]
    mask = np.max(np.abs(image.astype(int) - background.astype(int)), axis=2) > 15
    color = (image.max(2) - image.min(2) > 40) & mask
    assert mask.sum() > 100000, f'Empty or incomplete geometry in {path}'
    assert color.sum() > 20000, f'Materials missing in {path}'
    return image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    args = parser.parse_args()
    root, scratch = args.directory.resolve(), args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    executable = root / 'dbz3-native-gpu.exe'
    reports = []
    for model in ('goku', 'vegeta', 'krillin'):
        for yaw in (0.0, 0.5, -0.5, math.pi):
            target = scratch / f'{model}-{yaw:.2f}.ppm'
            result = subprocess.run([str(executable), '--model', str(root / 'assets' / f'{model}.dbzgpu'),
                '--shaders', str(root / 'materials.hlsl'), '--capture', str(target), '--yaw', str(yaw)],
                capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            assert 'NVIDIA GeForce RTX 5090' in result.stdout, result.stdout
            image = pixels(target)
            reports.append({'model': model, 'yaw': yaw, 'colored_pixels': int((image.max(2)-image.min(2)>40).sum())})
    # Scene rejection must happen before touching the GPU, with explicit errors.
    original = bytearray((root / 'assets/goku.dbzgpu').read_bytes())
    mutations = {'truncated': original[:100], 'bad-magic': b'WRONG!!!' + original[8:]}
    counts = struct.unpack_from('<4I', original, 8)
    bad_index = original.copy()
    struct.pack_into('<I', bad_index, 48 + counts[0] * 32, counts[0])
    mutations['bad-index'] = bad_index
    nan_vertex = original.copy()
    struct.pack_into('<f', nan_vertex, 48, float('nan'))
    mutations['nan-vertex'] = nan_vertex
    for name, data in mutations.items():
        model = scratch / f'{name}.dbzgpu'
        model.write_bytes(data)
        result = subprocess.run([str(executable), '--model', str(model), '--validate'], capture_output=True, timeout=10)
        assert result.returncode == 1, f'{name} was accepted'
    # Exercise the C ABI used by a future gameplay bridge with world-space data.
    native = ct.CDLL(str(root / 'dbz3-native-renderer.dll'))
    native.dbz_native_last_error.restype = ct.c_char_p
    native.dbz_native_create.argtypes = [ct.c_void_p, ct.c_char_p, ct.c_char_p]
    native.dbz_native_create.restype = ct.c_void_p
    native.dbz_native_destroy.argtypes = [ct.c_void_p]
    native.dbz_native_update_vertices.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_uint32]
    native.dbz_native_set_camera.argtypes = [ct.c_void_p, ct.POINTER(ct.c_float), ct.POINTER(ct.c_float)]
    native.dbz_native_render.argtypes = [ct.c_void_p]
    native.dbz_native_capture.argtypes = [ct.c_void_p, ct.c_char_p]
    def ok(result):
        assert result, native.dbz_native_last_error().decode()
    scene_path = root / 'assets/goku.dbzgpu'
    handle = native.dbz_native_create(None, str(scene_path).encode(), str(root / 'materials.hlsl').encode())
    ok(handle)
    try:
        bounds = np.asarray(struct.unpack_from('<6f', original, 24))
        center = (bounds[:3] + bounds[3:]) / 2
        scale = 1.8 / (bounds[3:] - bounds[:3]).max()
        matrix = np.eye(4, dtype='float32')
        matrix[0,0] = matrix[1,1] = scale
        matrix[2,2] = -scale * 0.08
        matrix[0,3] = -center[0] * scale
        matrix[1,3] = -center[1] * scale
        matrix[2,3] = 0.5 + center[2] * scale * 0.08
        eye = np.asarray(center + [0,0,100], dtype='float32')
        ok(native.dbz_native_set_camera(handle, matrix.ctypes.data_as(ct.POINTER(ct.c_float)),
            eye.ctypes.data_as(ct.POINTER(ct.c_float))))
        ok(native.dbz_native_render(handle))
        before_path, after_path = scratch / 'live-before.ppm', scratch / 'live-after.ppm'
        ok(native.dbz_native_capture(handle, str(before_path).encode()))
        before = pixels(before_path)
        vertices = np.frombuffer(original, dtype='<f4', count=counts[0]*8, offset=48).copy().reshape(-1,8)
        vertices[:,0] += 1.0
        ok(native.dbz_native_update_vertices(handle, vertices.ctypes.data, counts[0]))
        ok(native.dbz_native_render(handle))
        ok(native.dbz_native_capture(handle, str(after_path).encode()))
        after = pixels(after_path)
        changed = int((np.abs(before.astype(int)-after.astype(int)).max(2) > 15).sum())
        assert changed > 10000, f'Live vertex/camera path has no visible effect: {changed}'
        assert not native.dbz_native_update_vertices(handle, vertices.ctypes.data, counts[0]-1)
        assert b'count' in native.dbz_native_last_error()
        vertices[0,0] = float('nan')
        assert not native.dbz_native_update_vertices(handle, vertices.ctypes.data, counts[0])
        assert b'Nonfinite' in native.dbz_native_last_error()
        started = time.perf_counter()
        for _ in range(120):
            ok(native.dbz_native_render(handle))
        elapsed = time.perf_counter() - started
    finally:
        native.dbz_native_destroy(handle)
    # Enumerate this process to prove which libraries the hardware test loaded.
    kernel, psapi = ct.WinDLL('kernel32'), ct.WinDLL('psapi')
    kernel.GetCurrentProcess.restype = ct.c_void_p
    process = kernel.GetCurrentProcess()
    modules = (ct.c_void_p * 1024)()
    needed = ct.c_ulong()
    psapi.EnumProcessModules.argtypes = [ct.c_void_p,ct.c_void_p,ct.c_ulong,ct.POINTER(ct.c_ulong)]
    assert psapi.EnumProcessModules(process,modules,ct.sizeof(modules),ct.byref(needed))
    psapi.GetModuleFileNameExW.argtypes = [ct.c_void_p,ct.c_void_p,ct.c_wchar_p,ct.c_ulong]
    loaded = []
    for module in modules[:needed.value // ct.sizeof(ct.c_void_p)]:
        path = ct.create_unicode_buffer(4096)
        psapi.GetModuleFileNameExW(process,module,path,4096)
        loaded.append(Path(path.value).name)
    assert not any('rexgpu' in name.lower() or 'xenia' in name.lower() for name in loaded), loaded
    assert 'd3d12.dll' in [name.lower() for name in loaded], loaded
    result = {'hardware': 'NVIDIA GeForce RTX 5090', 'backend': 'Direct3D 12',
        'model_views_passed': len(reports), 'malformed_scenes_rejected': list(mutations),
        'live_vertex_camera_path_passed': True, 'live_update_changed_pixels': changed,
        'invalid_live_updates_rejected': ['wrong vertex count','NaN'],
        'offscreen_120_frames_wall_seconds': elapsed,
        'offscreen_average_frame_ms': elapsed * 1000 / 120,
        'performance_scope': 'Single model, 900x900, synchronous CPU/GPU; not gameplay FPS',
        'rexgpu_xenia_modules_loaded': False, 'loaded_modules': sorted(loaded), 'views': reports,
        'scope': 'Standalone host renderer and C ABI verified; this test process does not load the live game hooks'}
    (root / 'test-results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({key:value for key,value in result.items() if key not in ('loaded_modules','views')}, indent=2))


if __name__ == '__main__':
    main()

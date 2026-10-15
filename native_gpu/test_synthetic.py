"""Run asset-free hardware regressions; no game or original shaders needed."""
import argparse
import json
from pathlib import Path
import subprocess
import sys


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--bridge', type=Path, help='Optional indexed-replay.dll for CPU RGBA4 tests')
    args = parser.parse_args()
    directory, scratch = args.directory.resolve(), args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    scripts = ('image_samplers', 'frame_operations', 'dof_depth_filter',
               'depth_resample', 'blend_and_shadow_sampler', 'scene_snapshot')
    for name in scripts:
        subprocess.run([sys.executable, str(Path(__file__).with_name(f'test_{name}.py')),
                        '--directory', str(directory), '--scratch', str(scratch / name)], check=True)
    if args.bridge:
        subprocess.run([sys.executable, str(Path(__file__).with_name('test_source_rgba4.py')),
                        '--bridge', str(args.bridge.resolve()), '--output', str(scratch / 'rgba4.json')], check=True)
    result = dict(scope='Synthetic D3D12/CPU regressions only; not complete game fidelity.',
                  hardware_suites=list(scripts), rgba4_cpu_suite=bool(args.bridge), passed=True)
    (scratch / 'synthetic-tests.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Synthetic native graphics tests passed')

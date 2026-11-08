"""Paso SDK optativo, siempre dentro del turno de tools/compilar.py."""
import os
import shutil
import subprocess
import time
from pathlib import Path


def compilar(root):
    sdk = root / 'rexglue-sdk-0.10'
    build = sdk / 'out/build-win-vulkan-baseline'
    backup = Path('D:/tmp/solB2X') / ('sdk_solar_z_' + time.strftime('%Y%m%d_%H%M%S'))
    backup.mkdir()
    for name in ('rexruntime.dll', 'rexruntime.lib'):
        shutil.copy2(sdk / 'out/win-amd64-baseline' / name, backup / name)
    shaders = sdk / 'src/ui/shaders'
    destino = shaders / 'bytecode/d3d12_5_1/dbz3_solar_depth_ps.h'
    fxc = Path(os.environ.get('DBZ3_FXC', 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe'))
    commands = [
        [str(fxc), '/nologo', '/T', 'ps_5_1', '/E', 'main', '/O3', '/D', 'DBZ3_SOLAR_DEPTH=1',
         '/Vn', 'dbz3_solar_depth_ps', '/Fh', str(destino), str(shaders / 'dbz3_ui_depth.ps.hlsl')],
        ['cmake', '--build', str(build), '--target', 'rexruntime'],
    ]
    for i, cmd in enumerate(commands):
        p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
        (backup / f'paso_{i}.log').write_text(p.stdout + p.stderr, encoding='utf-8')
        print(f'solar Z SDK paso{i}: codigo{p.returncode}; registro {backup}', flush=True)
        if p.returncode:
            print((p.stdout + p.stderr)[-6000:], flush=True)
            return p.returncode
    return 0

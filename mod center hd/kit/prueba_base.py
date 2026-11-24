"""Prueba de kit/base.py con ficheros reales del juego (us/ del proyecto). python "mod center hd/kit/prueba_base.py"

- LZX: entrada 147 de data_cmn.afs -> descomprimir -> comprimir (/N:2048) -> descomprimir = igual.
- Indice AFS: leer data_yah.afs y reescribirlo con una entrada sin cambios = el mismo fichero, byte a byte.
- us/: base.find_us, swap_b3.find_us, la del Mod Kit (Env._find_us) y studio_core.us_dir dan la misma carpeta.
Temporales en TRABAJO\\tmp (tools/infra.py; E:\\DBZ3HD_trabajo), nunca en C:.
"""
import hashlib
import os
import sys
import tempfile

KIT = os.path.dirname(os.path.abspath(__file__))
HD = os.path.dirname(KIT)
sys.path[:0] = [HD, os.path.join(HD, 'studio'), os.path.join(os.path.dirname(HD), 'awo_tools')]
from kit import base  # noqa: E402

try:
    import infra  # noqa: E402  (base.py ya puso tools/ en sys.path)
    WORK = str(infra.TRABAJO / 'tmp')
except ImportError:
    WORK = None
if WORK:
    os.makedirs(WORK, exist_ok=True)


def main():
    us = base.find_us()
    assert us, 'sin us/ del Budokai 3 HD'
    cmn = os.path.join(us, 'data_cmn.afs')

    # índice AFS: lectura
    idx = base.read_afs_index(cmn)
    assert len(idx) >= base.CMN_ENTRIES and idx[0][0] > 8 + 8 * len(idx) - 1, idx[:2]
    raw = base.extract_afs_entry(cmn, 147)
    assert len(raw) == idx[147][1] and raw[:4] == base.LZX_MAGIC

    # LZX ida y vuelta
    plano = base.lzx_bytes(raw, False, WORK)
    assert plano[:4] == b'#AMB', plano[:4]
    comp = base.lzx_bytes(plano, True, WORK)
    assert comp[:4] == base.LZX_MAGIC and base.lzx_bytes(comp, False, WORK) == plano
    print('LZX: entrada 147 %d -> %d -> %d bytes, ida y vuelta igual' % (len(raw), len(plano), len(comp)))

    # índice AFS: reescritura con la misma entrada = el mismo fichero
    yah = os.path.join(us, 'data_yah.afs')
    t = base.read_afs_index(yah)
    k = max(i for i, (a, s) in enumerate(t) if s)
    with tempfile.TemporaryDirectory(dir=WORK) as d:
        out = os.path.join(d, 'yah.afs')
        base.build_afs(yah, k, base.extract_afs_entry(yah, k), out)
        h = [hashlib.sha256(open(p, 'rb').read()).hexdigest() for p in (yah, out)]
        assert h[0] == h[1], 'build_afs cambio el AFS'
        assert base.read_afs_index(out) == t
    print('AFS: data_yah.afs (%d entradas) reescrito igual, sha256 %s' % (len(t), h[0][:12]))

    # us/: las tres busquedas
    import swap_b3  # noqa: PLC0415
    import studio_core  # noqa: PLC0415
    import modkit_gui  # noqa: PLC0415
    got = {'base.find_us': us, 'swap_b3.find_us': swap_b3.find_us(),
           'modkit_gui.Env._find_us': modkit_gui.Env({})._find_us(),
           'studio_core.us_dir': os.path.abspath(studio_core.us_dir()),
           'swap_b3.DEFAULT_AFS': os.path.dirname(swap_b3.DEFAULT_AFS)}
    assert len(set(got.values())) == 1, got
    assert swap_b3.XBCOMPRESS == base.XBCOMPRESS and swap_b3.read_afs_index is base.read_afs_index
    print('us/: las 3 busquedas (+DEFAULT_AFS) = %s' % us)
    print('prueba_base: OK')


if __name__ == '__main__':
    main()

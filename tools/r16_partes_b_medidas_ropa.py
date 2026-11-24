"""Medidas locales de ropa B: evidencia de ajuste, sin guardar arte del juego."""
import sys
import tempfile
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'mod center hd/cac'), str(ROOT / 'mod center hd'), str(ROOT / 'awo_tools')]
import model_render as mr
import vista_heroe as vh
import partes_namek_majin as pb

afs = vh.rb.Afs(str(ROOT / 'us/data_cmn.afs'), tempfile.mkdtemp(prefix='r16_b_ropa_'))
for fid in (350, 352, 353):
    m = mr.Model(afs.entry(fid))
    W = np.asarray(m.worlds())
    inv = np.linalg.inv(W[m.bone('HEAD')])
    to_local = lambda P: P @ inv[:3, :3].T + inv[:3, 3]
    local = to_local(m.posed()[0])
    print('FID', fid)
    for sufijo in ('HEAD', 'TA', 'CHEST', 'LSPAT1', 'RSPAT1'):
        b = m.bone(sufijo)
        q = local[m.vbone == b] if b is not None else []
        if len(q):
            print('base', sufijo, len(q), q.min(0), q.max(0))
    for clave in ('nm_capa', 'nm_turbante'):
        p = pb.ajustar(clave, m)
        q = to_local(p['P'])
        print('pieza', clave, 'escala', p['escala'], q.min(0), q.max(0))

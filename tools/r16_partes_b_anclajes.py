"""Medidas locales para revisar las uniones de las piezas B; no guarda arte."""
import sys
import tempfile
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'mod center hd/cac'), str(ROOT / 'mod center hd'), str(ROOT / 'awo_tools')]
import model_render as mr
import vista_heroe as vh
import partes_namek_majin as pb
from r16_partes_b_comprobar import comprobar_malla

afs = vh.rb.Afs(str(ROOT / 'us/data_cmn.afs'), tempfile.mkdtemp(prefix='r16_b_anclajes_'))
for fid in (350, 352, 353, 128, 129, 133, 134, 141, 142):
    m = mr.Model(afs.entry(fid))
    h = m.bone('HEAD')
    W = np.asarray(m.worlds())
    inv = np.linalg.inv(W[h])
    P = m.posed()[0] @ inv[:3, :3].T + inv[:3, 3]
    q = P[m.vbone == h]
    print('FID', fid, 'HEAD min/max', q.min(0), q.max(0))
    for i, s in enumerate(m.labels):
        if 'HAIR' in s:
            root = inv[:3, :3] @ W[i, :3, 3] + inv[:3, 3]
            v = P[m.vbone == i]
            print(s, 'root', root, 'verts', len(v), 'bounds', (v.min(0), v.max(0)) if len(v) else None)
    p = pb.ajustar('nm_cabeza_02' if fid >= 350 else 'mj_antena_04', m)
    local = p['P'] @ inv[:3, :3].T + inv[:3, 3]
    print('PIEZA escala', p['escala'], 'primeros puntos', local[:8])
    if fid < 350:
        for k in range(1, 7):
            pieza = pb.ajustar('mj_antena_%02d' % k, m)
            # Cada extremo de la raíz sigue HEAD aunque la cadena HAIR gire.
            receta = pb.generar('mj_antena_%02d' % k)
            raices = np.array([s == 'HEAD' for s in receta['huesos']])
            assert raices.sum() >= 8
            assert np.all(pieza['vbone'][raices] == h)
            cambios = {i: mr.rot_x(40) for i, s in enumerate(m.labels) if 'HAIR' in s}
            pp = pieza['P'][raices]
            local_r = (pp-W[h, :3, 3]) @ W[h, :3, :3]
            anim = np.asarray(m.worlds(cambios))[h]
            assert np.allclose(local_r @ anim[:3, :3].T + anim[:3, 3], pp)
        print('PASS raices majin: seis tentaculos unidos a HEAD con HAIR inclinado 40 grados')
        for k in range(1, 4):
            rostro = pb.ajustar('mj_rostro_%02d' % k, m)
            comprobar_malla(rostro)
            assert np.all(rostro['vbone'] == h)
        print('PASS rostros majin: tres relieves cerrados, orientados y asentados en la cabeza local')

"""Sonda local del pipeline original: solo lee los modelos del jugador."""
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'mod center hd' / 'cac'), str(ROOT / 'mod center hd'), str(ROOT / 'awo_tools')]
import vista_heroe as vh


def main():
    afs = vh.rb.Afs(str(ROOT / 'us' / 'data_cmn.afs'), tempfile.mkdtemp(prefix='partes_sonda_'))
    for fid in (map(int, sys.argv[1:]) if len(sys.argv) > 1 else (181, 184)):
        m = vh.mr.Model(afs.entry(fid))
        p, _ = m.posed()
        w = m.worlds()
        print('FID', fid, 'ventanas', m.avb.n, 'tris', len(m.tris), 'bounds', p.min(0).round(3), p.max(0).round(3))
        print('bones', [(i, n, w[i][:3, 3].round(3).tolist()) for i, n in enumerate(m.labels) if any(s in n for s in ('HEAD', 'CHEST', 'TAIL', 'STMC', 'SHIPPO', 'SIPO'))])
        for i, n in enumerate(m.labels):
            if n.endswith('_HEAD') or 'TAIL' in n:
                v = p[m.vbone == i]
                if len(v): print(n, 'geom', v.min(0).round(2), v.max(0).round(2), len(v))
        print('draws', [(i, d['prim'], m.labels[d['group_bone']], d['B'][1], d['material'], m.mats[d['material']]['tex'], m.mats[d['material']]['env']) for i, d in enumerate(m.table.draws) if d['B'][1]])
    print('roster', json.dumps({k: vh.rb.IDS[k] for k in (27, 38)}, indent=2))


if __name__ == '__main__':
    main()

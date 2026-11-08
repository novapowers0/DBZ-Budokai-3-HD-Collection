"""eB2d: compara pose HD contra PAL hueso a hueso con el ORDEN REAL de cada matriz.
El orden de las matrices (PAL 43 y HD 46) NO es el de los nombres del ACM; se deduce de las pistas:
la matriz i es el hueso N si (mundo_padre^-1 * mundo_i) == pista de N (cualquier padre j<i).
uso: python tools/solb2x_huesos_cmp3.py <carpeta_hd> <idx_PAL> [clip=118] [cuadro_pista=9]"""
import json, struct, sys, numpy as np
sys.path[:0] = ['tools', 'mod center hd']
import solb2x_acm as acm, fk_b1 as K
B2T = 'D:/DBZ3HD/smoke_d/mods/Calle de la ciudad B2/transicion_b2.b2t'
PAL = 'D:/tmp/solB2X/original_g8_e0_20261105_185240/traza.json'
def dd(A, B): return float(np.degrees(np.arccos(np.clip((np.trace(A @ B.T) - 1) / 2, -1, 1))))
def pistas(clip, f):
    _, _, nf, rot, _ = acm.clip(acm.load(B2T, 8), clip)
    return {n: K.rot(*K.interp_ang(ks, f)) for n, ks in rot.items()}
def mapa(R, TR):
    """R: lista de rotaciones mundo (convencion columna). Devuelve nombre->indice (solo si es unico)."""
    h = {}
    for i in range(1, len(R)):
        for j in range(i):
            L = np.linalg.inv(R[j]) @ R[i]
            for n, T in TR.items():
                if dd(L, T) < 1.5 and (i == 1 or np.linalg.norm(L - np.eye(3)) > .05): h.setdefault(n, set()).add(i)
    return {n: min(s) for n, s in h.items() if len(s) == 1}
def pal_mats(rec, k=1):
    raw = bytes.fromhex(rec['actores'][k]['modelo_raw'])
    return np.array([struct.unpack_from('<16f', raw, 0x1480 + 0x40 * j) for j in range(43)]).reshape(43, 4, 4)
def hd_mats(a):
    n = len(a['acm_nombres']); w = a['modelo_raw']; b = struct.pack('<%dI' % len(w), *w)
    return np.array([struct.unpack_from('<16f', b, 0x24b0 + 0x40 * j) for j in range(n)]).reshape(-1, 4, 4)
def hd_pasos(d):
    out = []
    for l in open(d + '/traza_hd.jsonl'):
        try: x = json.loads(l)
        except Exception: continue
        if x.get('tipo') == 'paso' and all('modelo_raw' in a for a in x['actores']): out.append(x)
    return out
def comparar(Ph, Hh, TR):
    Rp = [m[:3, :3].T for m in Ph]; Rh = [m[:3, :3].T for m in Hh]
    mp, mh = mapa(Rp, TR), mapa(Rh, TR)
    e = {n: dd(Rp[mp[n]], Rh[mh[n]]) for n in mp if n in mh}
    return e, len(mp), len(mh)
if __name__ == '__main__':
    d, ip = sys.argv[1], int(sys.argv[2]); clip = int(sys.argv[3]) if len(sys.argv) > 3 else 118
    P = [r for r in json.load(open(PAL)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
    TR = pistas(clip, int(sys.argv[4]) if len(sys.argv) > 4 else 9)
    Ph = pal_mats(P[ip])
    for x in hd_pasos(d)[::int(sys.argv[5]) if len(sys.argv) > 5 else 15]:
        a = x['actores'][1]; e, np_, nh = comparar(Ph, hd_mats(a), TR); v = np.array(list(e.values()))
        print(x['cuadro'], hex(a['clip_modelo']), round(a['tiempo_modelo'], 1), 'mapeados PAL/HD', np_, nh, 'comunes', len(v),
              'media', round(v.mean(), 1) if len(v) else None, 'max', round(v.max(), 1) if len(v) else None, '<=5:', int((v <= 5).sum()))

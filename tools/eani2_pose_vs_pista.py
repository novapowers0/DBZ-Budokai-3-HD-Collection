"""eANI2: pose dibujada contra SU PROPIA pista, para cualquier luchador (combate, minijuego, personaje importado).
Lee el volcado de DBZ3_TEST_IW_HUESOS (lineas de pose + una linea "acm" por codigo con mapas y pistas del clip).
Para cada hueso con pista: giro local (padre^-1 * hijo) frente a la pista (Euler u16 R = Rz Ry Rx, interpolacion lineal
corta como el motor). Diferencias = algo pisa la pista (IK, mezcla, capas) o el hueso no tiene ranura en el mapa.
Se saltan los cuadros de reloj < desde (defecto 5): ahi aun entra la pose anterior (mezcla o cambio de clip).
uso: python tools/eani2_pose_vs_pista.py <volcado.jsonl> [umbral_grados=2] [cada=1] [desde=5]"""
import json, sys
import numpy as np
PAR = {'WAIST': 'BODY', 'STMC': 'WAIST', 'CHEST': 'STMC', 'NECK': 'CHEST', 'HEAD': 'NECK',
       'RLEGROT': 'WAIST', 'RLEG1': 'RLEGROT', 'RLEG2': 'RLEG1', 'RFOOT1': 'RLEG2', 'RFOOT2': 'RFOOT1',
       'LLEGROT': 'WAIST', 'LLEG1': 'LLEGROT', 'LLEG2': 'LLEG1', 'LFOOT1': 'LLEG2', 'LFOOT2': 'LFOOT1',
       'RCHN': 'CHEST', 'RARMROT': 'RCHN', 'RARM1': 'RARMROT', 'RARM2': 'RARM1', 'RHANDROT': 'RARM2',
       'LCHN': 'CHEST', 'LARMROT': 'LCHN', 'LARM1': 'LARMROT', 'LARM2': 'LARM1', 'LHANDROT': 'LARM2',
       'T_TAIL2': 'T_TAIL1', 'T_TAIL3': 'T_TAIL2', 'T_TAIL4': 'T_TAIL3', 'T_TAIL5': 'T_TAIL4'}
def suf(n): return n.split('_', 1)[1] if '_' in n else n
def rot(x, y, z):
    cx, sx, cy, sy, cz, sz = np.cos(x), np.sin(x), np.cos(y), np.sin(y), np.cos(z), np.sin(z)
    return np.array([[cz*cy, cz*sy*sx - sz*cx, cz*sy*cx + sz*sx], [sz*cy, sz*sy*sx + cz*cx, sz*sy*cx - cz*sx], [-sy, cy*sx, cy*cx]])
def interp(ks, f):
    ia = 0
    while ia + 1 < len(ks) and ks[ia + 1][0] <= f: ia += 1
    ib = min(ia + 1, len(ks) - 1)
    u = 0.0 if ks[ib][0] <= ks[ia][0] else min(max((f - ks[ia][0]) / (ks[ib][0] - ks[ia][0]), 0), 1)
    return rot(*[(ks[ia][e] + u * ((((ks[ib][e] - ks[ia][e] + 32768) & 0xFFFF) - 32768))) * 9.5873799e-5 for e in (1, 2, 3)])
def dd(A, B): return float(np.degrees(np.arccos(np.clip((np.trace(A @ B.T) - 1) / 2, -1, 1))))
def analizar(ruta, umbral=2.0, cada=1, desde=5.0):
    clips, res = {}, {}
    for k, l in enumerate(open(ruta)):
        try: d = json.loads(l)
        except ValueError: continue
        if 'acm' in d:
            if not d.get('pistas'): continue
            alm = (d['codigo'] >> 16) - 1
            clips[(d['i'], d['codigo'])] = (d['mapas'][256 * alm:256 * alm + 256],
                                            [[p[q:q + 4] for q in range(0, len(p), 4)] for p in d['pistas']])
            continue
        if k % cada or (d['i'], d['codigo']) not in clips or d['reloj'] < desde: continue
        mapa, pistas = clips[(d['i'], d['codigo'])]
        m = np.array(d['m']).reshape(-1, 4, 4); R = {suf(n): m[j][:3, :3].T for j, n in enumerate(d['nombres'])}
        idx = {suf(n): j for j, n in enumerate(d['nombres'])}
        r = res.setdefault((d['i'], d['codigo']), {})
        for b, p in PAR.items():
            if b not in idx or p not in R: continue
            s = mapa[idx[b]]
            if s < 0 or s >= len(pistas) or not pistas[s]:
                r.setdefault(b, [0.0, 'sin pista'])
                continue
            e = dd(R[p].T @ R[b], interp(pistas[s], d['reloj']))
            if e > r.get(b, [0.0])[0]: r[b] = [e, d['reloj']]
    for (i, c), r in sorted(res.items()):
        if not r: continue
        malos = {b: (round(v[0], 1), v[1]) for b, v in r.items() if v[0] > umbral or v[1] == 'sin pista'}
        print('actor %d clip 0x%X: %d huesos, peor %.1f grados%s' % (i, c, len(r), max(v[0] for v in r.values()),
              ('  -> ' + str(malos)) if malos else ''))
if __name__ == '__main__':
    analizar(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 2.0, int(sys.argv[3]) if len(sys.argv) > 3 else 1,
             float(sys.argv[4]) if len(sys.argv) > 4 else 5.0)

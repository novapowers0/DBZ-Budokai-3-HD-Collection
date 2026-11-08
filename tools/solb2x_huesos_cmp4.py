"""eB2d: pose HD vs PAL por tramos con el orden real de matrices (ver solb2x_huesos_cmp3).
uso: python tools/solb2x_huesos_cmp4.py <traza_pal.json> <carpeta_hd> [actor=1]
Alinea cuadro HD = reloj PAL + desfase (se busca) y da, por tramo de clip PAL, error medio/maximo de los huesos comunes."""
import json, sys, numpy as np
sys.path[:0] = ['tools']
import solb2x_huesos_cmp3 as C
PAL_IDX = {'GOK_WAIST': 1, 'GOK_STMC': 2, 'GOK_CHEST': 3, 'GOK_RCHN': 4, 'GOK_RARMROT': 5, 'GOK_RARM1': 6, 'GOK_RARM2': 7, 'GOK_RHANDROT': 8, 'GOK_L00_RHAND': 9, 'GOK_NECK': 11, 'GOK_LCHN': 22, 'GOK_LARMROT': 23, 'GOK_LARM1': 24, 'GOK_LARM2': 25, 'GOK_LLEGROT': 29, 'GOK_LLEG1': 30, 'GOK_LLEG2': 31, 'GOK_RLEGROT': 35, 'GOK_RLEG1': 36, 'GOK_RLEG2': 37}
HD_IDX = {'GOK_WAIST': 1, 'GOK_RLEGROT': 2, 'GOK_RLEG1': 3, 'GOK_RLEG2': 4, 'GOK_CHEST': 18, 'GOK_RCHN': 19, 'GOK_RARMROT': 20, 'GOK_RARM1': 21, 'GOK_RARM2': 22, 'GOK_RHANDROT': 23, 'GOK_L00_RHAND': 24, 'GOK_NECK': 26, 'GOK_LCHN': 41, 'GOK_LARMROT': 42, 'GOK_LARM1': 43, 'GOK_LARM2': 44}
def main(pal, hd, k=1, fijo=False):
    P = [r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
    H = C.hd_pasos(hd)
    # el mapa de indices se deduce de la pose de vuelo (clip 118 estatico) en la ultima muestra de vuelo
    TR = C.pistas(118, 9)
    ip = max(i for i, r in enumerate(P) if r['actores'][k]['clip'] & 0xffff == 118)
    mp = C.mapa([m[:3, :3].T for m in C.pal_mats(P[ip], k)], TR); mp['GOK_WAIST'] = 1
    ih = next(x for x in H if x['actores'][k]['clip_modelo'] & 0xffff == 0x170)
    mh = C.mapa([m[:3, :3].T for m in C.hd_mats(ih['actores'][k])], TR); mh['GOK_WAIST'] = 1
    com = [n for n in mp if n in mh]
    pt = np.array([r['reloj'] for r in P])
    py = np.array([r['actores'][k]['pos'][1] for r in P])
    ht = np.array([x['cuadro'] for x in H]); hy = np.array([x['actores'][k]['pos'][1] for x in H])
    sel = ht > 100
    best = min(((np.abs(np.interp(ht[sel] * s + o, pt, py) - hy[sel]).mean(), s, o) for s in np.arange(.8, 1.6, .02) for o in np.arange(-30, 30, 1.0)))
    print('huesos comunes', len(com), 'reloj HD->PAL: escala %.2f desfase %.0f (error y %.2f)' % (best[1], best[2], best[0]))
    _, s, o = best
    por = {}
    for x in H:
        t = x['cuadro'] * s + o; i = int(np.argmin(np.abs(pt - t)))
        if abs(pt[i] - t) > 3: continue
        Ph = C.pal_mats(P[i], k); Hh = C.hd_mats(x['actores'][k])
        Rp = [m[:3, :3].T for m in Ph]; Rh = [m[:3, :3].T for m in Hh]
        e = [C.dd(Rp[mp[n]], Rh[mh[n]]) for n in com]
        por.setdefault((hex(P[i]['actores'][k]['clip']), hex(x['actores'][k]['clip_modelo'])), []).append((np.mean(e), np.max(e)))
    for kk, v in por.items():
        v = np.array(v); print('PAL', kk[0], 'HD', kk[1], 'n', len(v), 'media %.1f max %.1f' % (v[:, 0].mean(), v[:, 1].max()))
if __name__ == '__main__' and not (len(sys.argv) > 3 and sys.argv[3] == 'escena'):
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1)

def comparar_escena(pal, hd, off=None):
    """todas las escenas: mapas fijos; ambos actores; desfase por la trayectoria de la victima si no se da."""
    P = [r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
    H = C.hd_pasos(hd)
    com = [n for n in PAL_IDX if n in HD_IDX]
    pt = np.array([r['reloj'] for r in P]); ht = np.array([x['cuadro'] for x in H])
    if off is None:
        py = np.array([r['actores'][1]['pos'][2] for r in P]); hy = np.array([x['actores'][1]['pos'][2] for x in H])
        sel = ht > 20
        off = min(((np.abs(np.interp(ht[sel] - o, pt, py) - hy[sel]).mean(), o) for o in np.arange(-30, 30, .5)))[1]
    print('desfase HD-PAL', off, 'muestras PAL', len(P), 'HD', len(H), 'huesos comunes', len(com))
    for k in (0, 1):
        por = {}
        for x in H:
            t = x['cuadro'] - off; i = int(np.argmin(np.abs(pt - t)))
            if abs(pt[i] - t) > 2 or not (0 <= i < len(P)): continue
            Rp = [m[:3, :3].T for m in C.pal_mats(P[i], k)]; Rh = [m[:3, :3].T for m in C.hd_mats(x['actores'][k])]
            e = [C.dd(Rp[PAL_IDX[n]], Rh[HD_IDX[n]]) for n in com]
            por.setdefault((hex(P[i]['actores'][k]['clip']), hex(x['actores'][k]['clip_modelo']), P[i]['actores'][k]['codigo'], x['actores'][k]['codigo']), []).append((np.mean(e), np.max(e)))
        for kk, v in por.items():
            v = np.array(v); print('actor', k, 'PAL clip %s cod %d | HD clip %s cod %d' % (kk[0], kk[2], kk[1], kk[3]), 'n', len(v), 'media %.1f max %.1f' % (v[:, 0].mean(), v[:, 1].max()))
if __name__ == '__main__' and len(sys.argv) > 3 and sys.argv[3] == 'escena':
    comparar_escena(sys.argv[1], sys.argv[2])

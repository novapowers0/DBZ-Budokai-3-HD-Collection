"""eB2f: pose del ATACANTE (actor 0) HD vs PAL hueso a hueso, y de la victima con el mapa de cmp4.
Atacante: indice PAL = posicion del nombre en el AMM B2 (banco 1); indice HD = posicion del nombre en acm_nombres
(la fila del esqueleto del atacante sigue ese orden; ver INFORME_eB2d/eB2f).
uso: python tools/solb2x_huesos_cmp5.py <traza_pal.json> <carpeta_hd> [desfase]"""
import json, sys, numpy as np
sys.path[:0] = ['tools']
import solb2x_huesos_cmp3 as C
import solb2x_huesos_cmp4 as M
import solb2x_acm as acm

def nombres_pal():
    return acm.info(acm.load(C.B2T, 9))[3]

def mapa_atacante(hd_nombres):
    pn = nombres_pal()
    m = {}
    for i, n in enumerate(pn):
        if n in hd_nombres and n.startswith('GOK_') and n not in ('GOK_BODY',):
            m[n] = (i, hd_nombres.index(n))
    return m

def main(pal, hd, off=None):
    P = [r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
    H = C.hd_pasos(hd)
    pt = np.array([r['reloj'] for r in P]); ht = np.array([x['cuadro'] for x in H])
    if off is None:
        py = np.array([r['actores'][1]['pos'][2] for r in P]); hy = np.array([x['actores'][1]['pos'][2] for x in H])
        sel = ht > 20
        off = min(((np.abs(np.interp(ht[sel] - o, pt, py) - hy[sel]).mean(), o) for o in np.arange(-30, 30, .5)))[1]
    mp = mapa_atacante(H[0]['actores'][0]['acm_nombres'])
    print('desfase HD-PAL', off, 'huesos atacante comunes', len(mp))
    por = {}
    for x in H:
        t = x['cuadro'] - off; i = int(np.argmin(np.abs(pt - t)))
        if abs(pt[i] - t) > 2: continue
        Rp = [m[:3, :3].T for m in C.pal_mats(P[i], 0)]; Rh = [m[:3, :3].T for m in C.hd_mats(x['actores'][0])]
        e = {n: C.dd(Rp[a], Rh[b]) for n, (a, b) in mp.items()}
        key = (hex(P[i]['actores'][0]['clip']), hex(x['actores'][0]['clip_modelo']))
        por.setdefault(key, []).append(e)
    for k, v in por.items():
        med = {n: np.mean([d[n] for d in v]) for n in v[0]}
        arr = np.array(list(med.values()))
        print('atacante PAL clip %s | HD clip %s n %d: media %.1f max-hueso %.1f  (<=5 grados: %d de %d)  peores %s' % (
            k[0], k[1], len(v), arr.mean(), arr.max(), int((arr <= 5).sum()), len(arr),
            sorted(med.items(), key=lambda t: -t[1])[:4]))
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else None)

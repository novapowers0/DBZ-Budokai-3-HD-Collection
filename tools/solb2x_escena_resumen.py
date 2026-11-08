"""eB2g: resumen por escena HD frente a PAL: victima (error medio <=5 grados) y atacante (>=25 de 27 huesos <=5).
Misma metrica que INFORME_eB2f. Alineacion: desfase fijo HD-PAL (por defecto 0,5; Kami no tiene movimiento para
deducirlo, ver INFORME_eB2g) o automatico por la trayectoria de la victima si se pasa 'auto'.
uso: python tools/solb2x_escena_resumen.py <traza_pal.json> <carpeta_hd> [desfase|auto] [i = interpola PAL]"""
import json, sys
import numpy as np
from scipy.spatial.transform import Rotation as Rot, Slerp
sys.path[:0] = ['tools']
import solb2x_huesos_cmp3 as C
import solb2x_huesos_cmp4 as M
import solb2x_huesos_cmp5 as C5

def interp(P, pt, t, k):
    """eB2h: rotaciones PAL del actor k en el reloj t por slerp entre las dos muestras vecinas (PAL se muestrea cada 1,2 y HD cada 1,0:
    comparar con la muestra mas cercana mete hasta 14 grados de error de cuantizacion en brazos rapidos)."""
    j = int(np.clip(np.searchsorted(pt, t) - 1, 0, len(pt) - 2)); a, b = C.pal_mats(P[j], k), C.pal_mats(P[j + 1], k)
    u = float(np.clip((t - pt[j]) / (pt[j + 1] - pt[j]), 0, 1)); out = []
    if P[j]['actores'][k]['codigo'] != P[j + 1]['actores'][k]['codigo'] or P[j]['actores'][k]['clip'] != P[j + 1]['actores'][k]['clip']:
        u = float(round(u))   # cambio de estado/clip entre las dos muestras PAL: no se mezcla (la mezcla seria un artefacto), se toma la mas cercana
    for ma, mb in zip(a, b):
        Ra, Rb = Rot.from_matrix(ma[:3, :3].T), Rot.from_matrix(mb[:3, :3].T)
        out.append(Slerp([0, 1], Rot.concatenate([Ra, Rb]))(u).as_matrix())
    return out

def main(pal, hd, off='0.5', interpolar=False):
    P = [r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
    H = C.hd_pasos(hd)
    pt = np.array([r['reloj'] for r in P]); ht = np.array([x['cuadro'] for x in H])
    if off == 'auto':
        py = np.array([r['actores'][1]['pos'][2] for r in P]); hy = np.array([x['actores'][1]['pos'][2] for x in H])
        sel = ht > 20
        off = min(((np.abs(np.interp(ht[sel] - o, pt, py) - hy[sel]).mean(), o) for o in np.arange(-30, 30, .5)))[1]
    off = float(off)
    mp = C5.mapa_atacante(H[0]['actores'][0]['acm_nombres'])
    com = [n for n in M.PAL_IDX if n in M.HD_IDX]
    vic = []; ata = []; mal = []
    for x in H:
        t = x['cuadro'] - off; i = int(np.argmin(np.abs(pt - t)))
        if abs(pt[i] - t) > 2: continue
        if interpolar and not (pt[0] <= t <= pt[-1]): continue
        Rp1 = interp(P, pt, t, 1) if interpolar else [m[:3, :3].T for m in C.pal_mats(P[i], 1)]; Rh1 = [m[:3, :3].T for m in C.hd_mats(x['actores'][1])]
        vic.append(np.mean([C.dd(Rp1[M.PAL_IDX[n]], Rh1[M.HD_IDX[n]]) for n in com]))
        Rp = interp(P, pt, t, 0) if interpolar else [m[:3, :3].T for m in C.pal_mats(P[i], 0)]; Rh = [m[:3, :3].T for m in C.hd_mats(x['actores'][0])]
        e = [C.dd(Rp[a], Rh[b]) for n, (a, b) in mp.items()]
        ata.append(int(sum(v <= 5 for v in e)))
        if sum(v <= 5 for v in e) < 25: mal.append((round(x['cuadro']), hex(P[i]['actores'][0]['clip']), hex(x['actores'][0]['clip_modelo']), sum(v <= 5 for v in e)))
    vic = np.array(vic); ata = np.array(ata)
    print('desfase %.1f muestras %d | victima error medio <=5: %d de %d (media %.1f max %.1f) | atacante >=25/27 huesos <=5: %d de %d' % (
        off, len(vic), int((vic <= 5).sum()), len(vic), vic.mean(), vic.max(), int((ata >= 25).sum()), len(ata)))
    if mal:
        # tramos de muestras malas
        tr = []
        for c, p, h, k in mal:
            if tr and c - tr[-1][1] <= 3 and tr[-1][2:] == (p, h): tr[-1][1] = c
            else: tr.append([c, c, p, h])
        print('atacante fuera de umbral por tramos (cuadro HD, PAL clip, HD clip):', [tuple(t) for t in tr][:8])
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else '0.5', len(sys.argv) > 4 and sys.argv[4] == 'i')

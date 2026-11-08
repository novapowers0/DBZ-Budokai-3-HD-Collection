"""Evaluador de huesos B2 (HD vs original PAL), reconstruido el 7/11 tras perder D:/tmp/dia1B.

Metodo (el de INFORME_dia1B): para cada pareja se alinea el reloj HD con el PAL por la trayectoria de la victima, se empareja
cada hueso PAL con el hueso HD de rotacion mundo mas parecida (mediana en el tiempo, tope 15 grados, uno a uno) usando UNA corrida
de referencia ("todo encendido") y se mide el error angular medio por cuadro de esas parejas en cada corrida.
d = error(config) - error(referencia): d > 0 = la config esta MAS lejos del original, es decir, el token quitado ayudaba.
Ruido entre corridas ~0,4 grados. El emparejamiento favorece algo a la referencia (limite conocido).

uso:
  python tools/b2_huesos.py capturar --slot 90 --grupo 4 [--salida DIR]   # PCSX2 plaza 2 ya lanzada con --estado (lanzar 2 ...)
  python tools/b2_huesos.py comparar --pal traza.json --ref CARPETA_HD_ON CARPETA_HD [CARPETA_HD ...]
  python tools/b2_huesos.py --autoprueba
La captura se guarda en E:/DBZ3HD_trabajo/dorados_noche/b2/huesos/ (nunca .p2s ni volcados de RAM)."""
import argparse, json, os, struct, subprocess, sys
from pathlib import Path
import numpy as np

SALIDA = Path('E:/DBZ3HD_trabajo/dorados_noche/b2/huesos')
TOPE = 15.0
N_PAL = 43


def cargar_pal(ruta):
    """lista de (reloj, [pos_actor0, pos_actor1], [R_actor0(43,3,3), R_actor1])"""
    out = []
    for r in json.load(open(ruta)):
        if not r.get('evento') or not all(a.get('modelo_raw') for a in r['actores']):
            continue
        Rs = []
        for a in r['actores']:
            raw = bytes.fromhex(a['modelo_raw'])
            m = np.array([struct.unpack_from('<16f', raw, 0x1480 + 0x40 * j) for j in range(N_PAL)]).reshape(N_PAL, 4, 4)
            Rs.append(m[:, :3, :3].transpose(0, 2, 1))
        out.append((r['reloj'], [a['pos'] for a in r['actores']], Rs))
    return out


def cargar_hd(carpeta):
    out = []
    for l in open(Path(carpeta) / 'traza_hd.jsonl'):
        if '"tipo":"paso"' not in l[:20]:
            continue
        try:
            x = json.loads(l)
        except Exception:
            continue
        if not all('modelo_raw' in a and a.get('acm_nombres') for a in x['actores']):
            continue
        Rs = []
        for a in x['actores']:
            n = len(a['acm_nombres'])
            b = struct.pack('<%dI' % len(a['modelo_raw']), *a['modelo_raw'])
            m = np.array([struct.unpack_from('<16f', b, 0x24b0 + 0x40 * j) for j in range(n)]).reshape(n, 4, 4)
            Rs.append(m[:, :3, :3].transpose(0, 2, 1))
        out.append((x['cuadro'], [a['pos'] for a in x['actores']], Rs))
    return out


def desfase(pal, hd):
    """cuadro HD = reloj PAL + off, minimizando la distancia de la victima (actor 1)."""
    pt = np.array([p[0] for p in pal]); pp = np.array([p[1][1] for p in pal])
    ht = np.array([h[0] for h in hd]); hp = np.array([h[1][1] for h in hd])
    def costo(o):
        idx = np.clip(np.searchsorted(pt, ht - o), 1, len(pt) - 1)
        i = np.where(np.abs(pt[idx - 1] - (ht - o)) < np.abs(pt[idx] - (ht - o)), idx - 1, idx)
        return np.linalg.norm(pp[i] - hp, axis=1).mean()
    return min((costo(o), o) for o in np.arange(-40, 40.5, .5))[1]


def pares(pal, hd, off):
    """indices (i_pal, j_hd) con |reloj - (cuadro-off)| <= 2"""
    pt = np.array([p[0] for p in pal])
    out = []
    for j, h in enumerate(hd):
        t = h[0] - off; i = int(np.argmin(np.abs(pt - t)))
        if abs(pt[i] - t) <= 2:
            out.append((i, j))
    return out


def angulos(A, B):
    """A (n,3,3), B (m,3,3) -> (n,m) grados"""
    tr = np.einsum('iab,jab->ij', A, B)
    return np.degrees(np.arccos(np.clip((tr - 1) / 2, -1, 1)))


def emparejar(pal, hd, ps, k):
    """mapa {hueso_pal: hueso_hd} del actor k por mediana temporal de error angular (uno a uno, tope TOPE)"""
    E = np.stack([angulos(pal[i][2][k], hd[j][2][k]) for i, j in ps[::2]])
    med = np.median(E, axis=0)
    cand = sorted((med[a, b], a, b) for a in range(med.shape[0]) for b in range(med.shape[1]) if med[a, b] <= TOPE)
    ua, ub, m = set(), set(), {}
    for c, a, b in cand:
        if a not in ua and b not in ub:
            m[a] = b; ua.add(a); ub.add(b)
    return m


def error(pal, hd, ps, k, mapa):
    """error medio (grados) por cuadro HD de los huesos emparejados; devuelve (media, serie)"""
    a = np.array(list(mapa)); b = np.array([mapa[x] for x in a])
    serie = []
    for i, j in ps:
        tr = np.einsum('nab,nab->n', pal[i][2][k][a], hd[j][2][k][b])
        serie.append(float(np.degrees(np.arccos(np.clip((tr - 1) / 2, -1, 1))).mean()))
    return float(np.mean(serie)), serie


def evaluar(pal, ref, otros):
    """pal: salida de cargar_pal; ref y otros: salidas de cargar_hd. Devuelve dict por corrida con error atacante/victima."""
    off = desfase(pal, ref)
    ps = pares(pal, ref, off)
    mapas = [emparejar(pal, ref, ps, k) for k in (0, 1)]
    res = {}
    for nom, hd in [('ref', ref)] + list(otros.items()):
        o = desfase(pal, hd)
        p2 = pares(pal, hd, o)
        res[nom] = dict(desfase=o, cuadros=len(p2),
                        **{('atacante', 'victima')[k]: error(pal, hd, p2, k, mapas[k])[0] for k in (0, 1)})
    res['_huesos'] = [len(m) for m in mapas]
    return res


def capturar(a):
    raiz = Path(__file__).resolve().parent.parent
    SALIDA.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, DBZ3_AGENTE='dia1B', DBZ3_SALIDA_PAL=str(a.salida))
    cmd = [sys.executable, str(raiz / 'tools/solb2x_original.py'), '--pool', '2', '--slot-libre', str(a.slot),
           '--grupo', str(a.grupo), '--huesos', '--campos', str(a.campos)]
    r = subprocess.run(cmd, env=env, cwd=raiz)
    return r.returncode


def autoprueba():
    rng = np.random.default_rng(1)
    def rot(v):
        v = np.radians(v); c, s = np.cos(v), np.sin(v)
        return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    n = 20
    pal = []; hd = []
    for t in range(30):
        R = [np.stack([rot(5 * b + 3 * t) for b in range(N_PAL)]) for _ in range(2)]
        pos = [[t, 0, 0], [t * 2.0, 1, 0]]
        pal.append((float(t + 10), pos, R))
    def mk(err):
        out = []
        for t in range(30):
            R = [np.stack([rot(5 * b + 3 * t + err) for b in range(N_PAL)]) for _ in range(2)]
            out.append((float(t), [[t, 0, 0], [t * 2.0, 1, 0]], R))
        return out
    ref = mk(0.0)
    res = evaluar(pal, ref, {'peor': mk(7.0)})
    assert abs(desfase(pal, ref) - (-10)) < 1e-6, desfase(pal, ref)
    assert res['ref']['victima'] < 0.01 and res['ref']['atacante'] < 0.01, res
    assert abs(res['peor']['victima'] - 7.0) < 0.1, res
    assert res['_huesos'][1] >= 30, res
    print('autoprueba OK', json.dumps(res))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--autoprueba', action='store_true')
    sp = ap.add_subparsers(dest='cmd')
    c = sp.add_parser('capturar'); c.add_argument('--slot', type=int, required=True); c.add_argument('--grupo', type=int, required=True)
    c.add_argument('--campos', type=int, default=850); c.add_argument('--salida', type=Path, default=SALIDA)
    m = sp.add_parser('comparar'); m.add_argument('--pal', required=True); m.add_argument('--ref', required=True)
    m.add_argument('hd', nargs='*'); m.add_argument('--nombres', help='etiquetas separadas por coma para las carpetas hd')
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    if a.cmd == 'capturar':
        return capturar(a)
    if a.cmd == 'comparar':
        pal = cargar_pal(a.pal); ref = cargar_hd(a.ref)
        nom = a.nombres.split(',') if a.nombres else [Path(h).name for h in a.hd]
        res = evaluar(pal, ref, {n: cargar_hd(h) for n, h in zip(nom, a.hd)})
        print('huesos emparejados (atacante, victima):', res.pop('_huesos'))
        r0 = res['ref']
        for n, r in res.items():
            print('%-28s desfase %5.1f cuadros %4d  atac %6.2f  vict %6.2f  d_atac %+6.2f  d_vict %+6.2f' % (
                n, r['desfase'], r['cuadros'], r['atacante'], r['victima'], r['atacante'] - r0['atacante'], r['victima'] - r0['victima']))
        return 0
    ap.print_help()


if __name__ == '__main__':
    sys.exit(main())

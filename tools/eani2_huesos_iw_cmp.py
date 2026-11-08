"""eANI2: pose hueso a hueso de un minijuego de IW, port contra PCSX2 (savestate), mismo clip y cuadro.
Port: volcado de DBZ3_TEST_IW_HUESOS=<ruta.jsonl> (src/anim_iw.cpp VolcarHuesos: nombres + matrices 64 B del modelo B3).
Original: eeMemory del .p2s: actor -> modelo (+0xb78), nombres del #AMG/#AMO (+4), matrices (+0x408), mismo formato.
Se compara en el marco de la cintura (rotacion en grados, posicion en unidades del modelo), por NOMBRE (sufijo).
uso: python tools/eani2_huesos_iw_cmp.py <port.jsonl> <actor 0|1> <codigo_port> <estado.p2s> <modelo_ps2> <cuadro>
     python tools/eani2_huesos_iw_cmp.py --prueba   (comprobacion propia de la matematica)"""
import json, struct, sys, zipfile
import numpy as np

def suf(n): return n.split('_', 1)[1] if '_' in n else n
def dd(A, B): return float(np.degrees(np.arccos(np.clip((np.trace(A @ B.T) - 1) / 2, -1, 1))))
def rel(M, b):
    W = M['WAIST']; Rw = W[:3, :3].T
    return Rw.T @ (M[b][3, :3] - W[3, :3]), Rw.T @ M[b][:3, :3].T
def comparar(P, O, huesos=None):
    out = {}
    for b in huesos or [b for b in P if b in O and b != 'BODY']:
        pp, rp = rel(P, b); po, ro = rel(O, b)
        out[b] = (dd(rp, ro), float(np.linalg.norm(pp - po)))
    return out
def port(ruta, actor, codigo, cuadro):
    best = None
    for l in open(ruta):
        if not l.startswith('{"n"'): continue
        try: d = json.loads(l)
        except ValueError: continue   # una linea con nan
        if d['i'] != actor or d['codigo'] != codigo: continue
        if best is None or abs(d['reloj'] - cuadro) < abs(best['reloj'] - cuadro): best = d
    m = np.array(best['m']).reshape(-1, 4, 4)
    return best['reloj'], {suf(n): m[i] for i, n in enumerate(best['nombres'])}
def original(p2s, modelo):
    with zipfile.ZipFile(p2s) as z: ram = z.read('eeMemory.bin')
    u = lambda a: struct.unpack_from('<I', ram, a)[0]
    h = u(modelo + 4)
    n, no = (u(h + 0x10), u(h + 0x1C)) if ram[h + 1:h + 4] in (b'AMG', b'AWG') else (u(h + 0x20), u(h + 0x24))
    if no < 0x100000: no += h
    m = np.array(struct.unpack_from('<%df' % (16 * n), ram, u(modelo + 0x408))).reshape(n, 4, 4)
    return {suf(ram[no + 32 * i:no + 32 * i + 32].split(b'\0')[0].decode()): m[i] for i in range(n)}
def prueba():
    rng = np.random.default_rng(1)
    def rot(a): c, s = np.cos(a), np.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    M = {}
    for b, a in (('WAIST', .3), ('RLEG2', .9)):
        m = np.eye(4); m[:3, :3] = rot(a).T; m[3, :3] = rng.normal(size=3); M[b] = m
    N = {k: v.copy() for k, v in M.items()}; N['RLEG2'][:3, :3] = rot(.9 + np.radians(30)).T
    e = comparar(M, N)
    assert abs(e['RLEG2'][0] - 30) < 1e-4 and e['WAIST'][0] < 1e-3, e
    print('prueba OK')
if __name__ == '__main__':
    if sys.argv[1:] == ['--prueba']: prueba(); sys.exit()
    ruta, actor, codigo, p2s, modelo, cuadro = sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 0), sys.argv[4], int(sys.argv[5], 0), float(sys.argv[6])
    f, P = port(ruta, actor, codigo, cuadro); O = original(p2s, modelo)
    e = comparar(P, O); v = np.array([x[0] for x in e.values()])
    print('cuadro port %.2f / original %.2f: %d huesos, rot media %.2f max %.2f grados' % (f, cuadro, len(v), v.mean(), v.max()))
    for b, (r, p) in sorted(e.items(), key=lambda kv: -kv[1][0])[:8]: print('  %-10s rot %6.2f  pos %.3f' % (b, r, p))

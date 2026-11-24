"""Evaluador de huesos B2 (HD vs original PAL), reconstruido el 7/11 tras perder D:/tmp/dia1B.

Metodo (el de INFORME_dia1B): para cada pareja se alinea el reloj HD con el PAL por la trayectoria de la victima, se empareja
cada hueso PAL con el hueso HD de rotacion mundo mas parecida (mediana en el tiempo, tope 15 grados, uno a uno) usando UNA corrida
de referencia ("todo encendido") y se mide el error angular medio por cuadro de esas parejas en cada corrida.
d = error(config) - error(referencia): d > 0 = la config esta MAS lejos del original, es decir, el token quitado ayudaba.
Ruido entre corridas ~0,4 grados. El emparejamiento favorece algo a la referencia (limite conocido).

uso:
  python tools/b2_huesos.py capturar --slot 90 --grupo 4 [--salida DIR]   # PCSX2 plaza 2 ya lanzada con --estado (lanzar 2 ...)
  python tools/b2_huesos.py comparar --pal traza.json --ref CARPETA_HD_ON CARPETA_HD [CARPETA_HD ...]
  python tools/b2_huesos.py escala [--escribir]      # B2F: razon HD/B2 por ficha (ISO de B2 + bins HD)
  python tools/b2_huesos.py modelo --pal traza.json --hd CARPETA_HD   # control: escala HD/PAL en trazas
  python tools/b2_huesos.py parejas   # 29 parejas de la rueda con tools/solb2x_lanzamiento.py (1 ventana)
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


# ---------------------------------------------------------------- escala por ficha (B2F, 11/11)
# El IK de suelo compara alturas del hueso (modelo) con umbrales en unidades de mundo: si el modelo
# que dibuja el port (HD) mide distinto que el de B2, los umbrales hay que escalarlos por esa razon.
# La razon NO se mide en el emulador: se lee de los dos esqueletos en reposo (pierna cadera->tobillo
# y cuello cintura->cuello). Fuentes, todas en disco:
#   - B2: ELF SLES_518.39 (tabla de personajes, 53 filas de 0x2C8 en 0x2895C0; +0x74 = entrada AFS del
#     cuerpo base) y DATA_CMN.AFS de su ISO (entrada -> #AMB -> #AMO0).
#   - HD: us/data_cmn.afs (entrada del modelo base de cada ID de B3, via roster_db.json).
#   - ficha -> ID de B3: kB2aB3 de src/pasarela_b2.cpp (unica fuente).
# Medido 11/11: 35 de 53 dan 1,000000 exacto (el esqueleto HD es el mismo de B2), Cell 1,1000 y
# Ten/Yamhan 1,1025; 15 fichas no tienen cuerpo propio en el ELF (SoloB2/donante), su ID no tiene
# modelo base o la escala no es uniforme -> se quedan en 1,0 (regla de ficha desconocida).
ESCALA_LEEME = (
    'Escala del modelo HD/B2 por ficha para el IK de suelo (B2F, 11/11). Metodo: distancia cadera->tobillo '
    'y cintura->cuello del esqueleto EN REPOSO del cuerpo base de B2 (ELF SLES_518.39 0x2895C0 fila*0x2C8, '
    '+0x74 -> DATA_CMN.AFS del ISO de B2) y del modelo base de B3 (roster_db.json -> us/data_cmn.afs). '
    'ficha con cuerpo 0 en el ELF, sin modelo base de B3 o escala no uniforme = 1,0 (ficha desconocida). Medido: '
    '35/53 = 1,000000; Cell 1,1000; Ten/Yamhan 1,1025; 15 sin dato. No confundir con la escala 1,24 de eB2h: esa es del estado/suelo '
    '(medida con Goku, cuyo modelo da 1,000000), no del modelo. Es la ficha base: una transformacion usa otro modelo B3 y necesitaria su propia razon. Reproducir: python tools/b2_huesos.py escala')


def _radio(p):
    return Path(p).resolve()


def _iso_b2(ruta=None):
    """Ruta del ISO de B2: la dada, la de DBZ3_PS2_GAMES o ps2_games/*Budokai 2*.iso."""
    if ruta:
        return Path(ruta)
    base = os.environ.get('DBZ3_PS2_GAMES')
    candidatos = []
    if base:
        candidatos.append(Path(base))
    candidatos.append(Path(__file__).resolve().parent.parent / 'ps2_games')
    for d in candidatos:
        for p in sorted(d.glob('*Budokai 2*.iso')):
            return _radio(p)
    return None


def _importar_analisis():
    """iso.py (lector ISO del kit), altura.py (esqueletos B2/HD) y afs_pair.py (AFS HD con cache)."""
    raiz = Path(__file__).resolve().parent.parent
    for d in (raiz / 'mod center hd', raiz / 'awo_tools'):
        if str(d) not in sys.path:
            sys.path.insert(0, str(d))
    import iso  # noqa: PLC0415
    import altura  # noqa: PLC0415
    import afs_pair  # noqa: PLC0415
    return iso, altura, afs_pair


def _tabla_b2a_b3():
    """kB2aB3 de src/pasarela_b2.cpp (53 enteros). Falla si el fichero cambia de forma."""
    import re  # noqa: PLC0415
    txt = (Path(__file__).resolve().parent.parent / 'src/pasarela_b2.cpp').read_text(encoding='utf-8')
    m = re.search(r'kB2aB3\[kB2Personajes\]\s*=\s*\{(.*?)\};', txt, re.S)
    if not m:
        raise RuntimeError('no encontrado kB2aB3 en src/pasarela_b2.cpp')
    cuerpo = re.sub(r'//[^\n]*', '', m.group(1))
    vals = [int(x) for x in re.findall(r'-?\d+', cuerpo)]
    if len(vals) != 53:
        raise RuntimeError('kB2aB3 tiene %d valores (se esperan 53)' % len(vals))
    return vals


def _elf_b2_desde_iso(img, inner):
    """ELF del juego dentro del ISO (SLES_518.39): evita depender de E:\\DBZ3HD_trabajo."""
    for ruta, (lba, tam) in img.files().items():
        if ruta.upper().endswith('SLES_518.39'):
            return img.read(ruta, 0, tam)
    return None


def _entrada_iso(img, inner, tab, n):
    off, tam = tab[n]
    return img.read(inner, off, tam)


def _medidas_esqueleto(sk, altura):
    """(pierna, cuello, altura) del esqueleto en reposo; None si falta un hueso."""
    P = altura.pose(sk, None)
    tobillos = [P[k][1] for k in ('LFOOT1', 'RFOOT1') if k in P]
    pierna = altura.leg(sk)
    cuello = P['NECK'][1] - P['WAIST'][1] if 'NECK' in P and 'WAIST' in P else None
    alto = P['HEAD'][1] - min(tobillos) if 'HEAD' in P and tobillos else None
    return pierna, cuello, alto


def escala_tabla(iso_b2=None, escribir=False, ruta_json=None):
    """Lista de 53 razones HD/B2 (1,0 = sin dato) y salida por pantalla con numerador/denominador."""
    import json as _json  # noqa: PLC0415
    import struct  # noqa: PLC0415
    iso, altura, afs_pair = _importar_analisis()
    raiz = Path(__file__).resolve().parent.parent
    ruta_json = Path(ruta_json) if ruta_json else raiz / 'tools/dorados/b2_ik_tabla_pal.json'
    iso_b2 = _iso_b2(iso_b2)
    if iso_b2 is None or not iso_b2.is_file():
        raise SystemExit('BLOQUEADO: falta el ISO de B2 (--iso-b2 o DBZ3_PS2_GAMES)')
    img = iso.Iso(str(iso_b2))
    inner = next((k for k in img.files() if k.upper().endswith('DATA_CMN.AFS')), None)
    if inner is None:
        raise SystemExit('BLOQUEADO: el ISO de B2 no tiene DATA_CMN.AFS: %s' % iso_b2)
    cnt = struct.unpack('<I', img.read(inner, 0, 8)[4:8])[0]
    tab = [struct.unpack_from('<II', img.read(inner, 8, 8 * cnt), 8 * k) for k in range(cnt)]
    elf = _elf_b2_desde_iso(img, inner)
    if elf is None:   # respaldo: el ELF del proyecto de Ghidra
        p = Path('E:/DBZ3HD_trabajo/ghidra/elfs/b2.elf')
        elf = p.read_bytes() if p.is_file() else None
    if elf is None:
        raise SystemExit('BLOQUEADO: falta el ELF de B2 (SLES_518.39) en el ISO y en ghidra/elfs')

    rdb = _json.loads((raiz / 'mod center hd/roster_db.json').read_text(encoding='utf-8'))
    ids = {int(e['id']): e for e in rdb['ids']}
    formas = {int(k): v for k, v in rdb['form_ids'].items()}

    def bin_hd(b3):
        e = ids.get(b3) or formas.get(b3)
        if e:
            return int(e['models'][0])
        return {72: 438, 64: 305}.get(b3)   # fusiones: ID de forma sin entrada propia en roster_db

    a_b3 = _tabla_b2a_b3()
    razones, filas = [], []
    for c in range(53):
        rec = 0x2895C0 + c * 0x2C8
        nom = elf[rec:rec + 0x20].split(b'\0')[0].decode('latin1')
        cuerpo = struct.unpack_from('<I', elf, rec + 0x74)[0]
        bin3 = bin_hd(a_b3[c])
        m2 = m3 = (None, None, None)
        if 0 < cuerpo < len(tab):
            try:
                m2 = _medidas_esqueleto(altura.skeleton(_entrada_iso(img, inner, tab, cuerpo)), altura)
            except Exception:
                m2 = (None, None, None)
        if bin3:
            try:
                m3 = _medidas_esqueleto(altura.skeleton(afs_pair.hd(bin3)), altura)
            except Exception:
                m3 = (None, None, None)
        r = None
        if m2[0] and m3[0]:
            r = m3[0] / m2[0]
            # Escala uniforme o no hay dato: si la razon de altura no acompana a la de pierna, el
            # modelo HD no es el mismo cuerpo escalado (Gotenks: el ELF apunta al cuerpo de Goten).
            ra = (m3[2] / m2[2]) if (m2[2] and m3[2]) else None
            if ra and abs(ra - r) > 0.01:
                r = None
        razones.append(round(r, 6) if r else 1.0)
        filas.append((c, nom, cuerpo, bin3, m2, m3, r))
    print('ficha nombre             b2   hd   pierna B2  pierna HD  razon  cuello B2/HD  raz_cuello')
    for c, nom, cuerpo, bin3, m2, m3, r in filas:
        def f(x):
            return ('%9.4f' % x) if x else '       --'
        rc = (m3[1] / m2[1]) if (m2[1] and m3[1]) else None
        ra = (m3[2] / m2[2]) if (m2[2] and m3[2]) else None
        print('%5d %-18s %4d %4s %s %s  %s  %s/%s %s' % (
            c, nom, cuerpo, bin3, f(m2[0]), f(m3[0]), ('%.6f' % r) if r else '  sin dato',
            f(m2[1]), f(m3[1]), ('%.4f' % rc) if rc else '--'))
        if ra and r and abs(ra - r) > 0.01:
            print('      aviso: alto/pierna no coinciden (%.4f vs %.4f): modelo distinto, no escala uniforme' % (ra, r))
    distintos = [(c, n, r) for c, n, _, _, _, _, r in filas if r and abs(r - 1.0) > 1e-4]
    print('resumen: %d fichas con modelo identico (1,000000), %d distintas, %d sin dato (1,0)' % (
        53 - len(distintos) - sum(1 for f in filas if not f[6]), len(distintos), sum(1 for f in filas if not f[6])))
    print('distintas de 1:', [(c, n, round(r, 4)) for c, n, r in distintos])
    goku = filas[0]
    print('control Goku: %.6f (esperado 1,000000)' % (goku[6] or 0.0))

    actual = None
    if ruta_json.is_file():
        actual = _json.loads(ruta_json.read_text(encoding='utf-8')).get('escala')
    if actual is not None:
        n = min(len(actual), len(razones))
        dif = [(i, actual[i], razones[i]) for i in range(n) if abs(float(actual[i]) - razones[i]) > 1e-6]
        print('json: %s (%d valores); diferencias con lo medido: %s' % (
            ruta_json, len(actual), dif if dif else 'ninguna'))
    # Guardia contra deriva: la tabla C++ (kIkEscala) debe ser la misma que la del json.
    import re as _re  # noqa: PLC0415
    inl = raiz / 'src/escenarios_b2_ik.inl'
    if inl.is_file():
        m = _re.search(r'kIkEscala\[53\]\s*=\s*\{(.*?)\};', inl.read_text(encoding='utf-8'), _re.S)
        if m:
            cpp = [float(x) for x in m.group(1).split(',')]
            dif = [(i, cpp[i], razones[i]) for i in range(min(len(cpp), len(razones))) if abs(cpp[i] - razones[i]) > 1e-6]
            print('inl: %s (%d valores); diferencias con lo medido: %s' % (inl.name, len(cpp), dif if dif else 'ninguna'))
        else:
            print('inl: no encontrado kIkEscala[53] (revisar a mano)')
    if escribir:
        d = _json.loads(ruta_json.read_text(encoding='utf-8'))
        d['escala'] = razones
        d['_escala_leeme'] = ESCALA_LEEME
        ruta_json.write_text(_json.dumps(d, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
        print('escrito:', ruta_json)
    return razones


def modelo_traza(pal_ruta, hd_carpeta, actor=None):
    """Razon de escala HD/PAL medida en las trazas (no en el juego): distancias entre huesos
    emparejados por rotacion, en el mismo cuadro. Numerador HD, denominador PAL."""
    import json as _json  # noqa: PLC0415
    import struct  # noqa: PLC0415

    def pos_de(m, n, off):
        return m[:, 3, :3]

    def carga_pal(ruta):
        out = []
        for r in _json.load(open(ruta)):
            if not r.get('evento') or not all(a.get('modelo_raw') for a in r['actores']):
                continue
            Rs, Ps = [], []
            for a in r['actores']:
                raw = bytes.fromhex(a['modelo_raw'])
                m = np.array([struct.unpack_from('<16f', raw, 0x1480 + 0x40 * j) for j in range(N_PAL)]).reshape(N_PAL, 4, 4)
                Rs.append(m[:, :3, :3].transpose(0, 2, 1)); Ps.append(pos_de(m, N_PAL, 0))
            out.append((r['reloj'], [a['pos'] for a in r['actores']], Rs, Ps))
        return out

    def carga_hd(carpeta):
        out = []
        for l in open(Path(carpeta) / 'traza_hd.jsonl'):
            if '"tipo":"paso"' not in l[:20]:
                continue
            try:
                x = _json.loads(l)
            except Exception:
                continue
            if not all('modelo_raw' in a and a.get('acm_nombres') for a in x['actores']):
                continue
            Rs, Ps = [], []
            for a in x['actores']:
                n = len(a['acm_nombres'])
                b = struct.pack('<%dI' % len(a['modelo_raw']), *a['modelo_raw'])
                m = np.array([struct.unpack_from('<16f', b, 0x24b0 + 0x40 * j) for j in range(n)]).reshape(n, 4, 4)
                Rs.append(m[:, :3, :3].transpose(0, 2, 1)); Ps.append(pos_de(m, n, 0))
            out.append((x['cuadro'], [a['pos'] for a in x['actores']], Rs, Ps))
        return out

    pal, hd = carga_pal(pal_ruta), carga_hd(hd_carpeta)
    off = desfase(pal, hd)
    ps = pares(pal, hd, off)
    actores = (0, 1) if actor is None else (actor,)
    print('desfase %.1f  cuadros emparejados %d' % (off, len(ps)))
    for k in actores:
        mapa = emparejar(pal, hd, ps, k)
        a = np.array(list(mapa)); b = np.array([mapa[x] for x in a]); n = len(a)
        rr = []
        for i, j in ps:
            A, B = pal[i][3][k][a], hd[j][3][k][b]
            dA = np.linalg.norm(A[:, None, :] - A[None, :, :], axis=2)
            dB = np.linalg.norm(B[:, None, :] - B[None, :, :], axis=2)
            iu = np.triu_indices(n, 1)
            sel = dA[iu] > 1.0
            rr.append(dB[iu][sel] / dA[iu][sel])
        r = np.concatenate(rr)
        print('actor %d: huesos %d  pares %d  mediana %.4f  p10 %.4f  p90 %.4f  (HD/PAL)' % (
            k, n, len(r), float(np.median(r)), float(np.percentile(r, 10)), float(np.percentile(r, 90))))


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


# ---------------------------------------------------------------- parejas de la rueda (ronda 5)
# El .toml de pruebas no puede colocar a los luchadores junto a la pared, que es lo que dispara el motor
# B2 (B2ContactoReal con una superficie valida). El arnes tools/solb2x_lanzamiento.py SI escribe posiciones,
# pulsa L+P y comprueba "inicio por colision real". Esta orden lo corre para las 29 parejas de la rueda
# (cada personaje con el siguiente) y deja un JSON con el resultado de cada una.
RUEDA = ["YAMCHA", "TENSHINHAN", "Mr.SATAN", "KAIO-SHIN", "DABURA", "BUU L", "BUU M", "BUU S", "Dr.GERO",
         "CELL", "ANDROID No.18", "ANDROID No.17", "ANDROID No.16", "FREEZA", "GINEW", "RECOOM", "RADITZ",
         "NAPPA", "VEGETA", "TRUNKS L", "TRUNKS S", "GOTEN", "GOKOU", "GOHAN S", "GOHAN L", "GREAT SAIYAMAN",
         "VIEDL", "PICCOLO", "KULILIN"]
# Tres parejas de la rueda no arrancan el motor con el atacante natural (Buu M/Buu S y Piccolo/Krillin lo
# arrancan al reves; Videl-Piccolo no con ninguno de los dos, y Videl queda cubierta con Goku de atacante).
# Se cambia solo el ORDEN o el atacante; la pareja reportada sigue siendo la de la rueda.
PAR_OVERRIDE = {6: (36, 35), 26: (0, 15), 27: (10, 11)}


def _wrapper_lanz():
    """Envoltorio que corrige el alta() del arnes (la lista de mods crecio y su calculo se quedo corto)."""
    raiz = Path(__file__).resolve().parent.parent
    d = Path(os.environ.get('DBZ3_TMP', 'E:/DBZ3HD_trabajo/S5'))
    d.mkdir(parents=True, exist_ok=True)
    w = d / 'b2_run_lanz.py'
    w.write_text(
        'import sys\n'
        'sys.path.insert(0, r"%s")\n'
        'import solb2x_lanzamiento as S\n'
        '_alta = int(sys.argv[1])\n'
        'sys.argv = ["solb2x_lanzamiento.py"] + sys.argv[2:]\n'
        'S.alta = lambda mods, entrada: _alta\n'
        'raise SystemExit(S.main())\n' % (raiz / 'tools'), encoding='utf-8')
    return w


def parejas_rueda(copia='smoke_c', grupo=4, alta=32, desde=0, hasta=None, salida=None):
    import json as _json  # noqa: PLC0415
    import subprocess  # noqa: PLC0415
    import time  # noqa: PLC0415
    raiz = Path(__file__).resolve().parent.parent
    fichas = _json.loads((raiz / 'tools/dorados/b2_ik_tabla_pal.json').read_text(encoding='utf-8'))['fichas']
    a_b3 = _tabla_b2a_b3()
    pares = []
    for i in range(len(RUEDA)):
        pares.append((RUEDA[i], RUEDA[(i + 1) % len(RUEDA)]))
    pares = pares[int(desde):int(hasta) if hasta is not None else None]
    w = _wrapper_lanz()
    env = dict(os.environ, DBZ3_AGENTE='solB2X')
    filas = []
    for n, (na, nb) in enumerate(pares, start=int(desde)):
        fa, fb = fichas[na], fichas[nb]
        ids = ('%d,%d' % PAR_OVERRIDE[n]) if n in PAR_OVERRIDE else ('%d,%d' % (a_b3[fa], a_b3[fb]))
        t0 = time.monotonic()
        cmd = [sys.executable, str(w), str(alta), '--copia', copia, '--grupo', str(grupo),
               '--ids', ids, '--ik-suelo', '--ik-tabla', '--finalizar']
        try:
            r = subprocess.run(cmd, cwd=str(raiz), env=env, capture_output=True, text=True, timeout=600)
            out = r.stdout
            rc = r.returncode
        except subprocess.TimeoutExpired as e:
            out, rc = (e.stdout or ''), -9
        ev = None
        for l in out.splitlines():
            if l.startswith('evidencia '):
                ev = Path(l[len('evidencia '):].strip())
        ini, lados, suelo = False, [], []
        if ev and (ev / 'medida.json').is_file():
            try:
                ini = bool(_json.loads((ev / 'medida.json').read_text(encoding='utf-8')).get('inicio_real'))
            except Exception:
                ini = False
        if ev and (ev / 'dbz3.log').is_file():
            txt = (ev / 'dbz3.log').read_text(encoding='utf-8', errors='replace')
            lados = sorted({l.split('] ', 1)[-1] for l in txt.splitlines() if 'dbz3 B2 IK: lado ' in l})
            suelo = [l.split('] ', 1)[-1] for l in txt.splitlines() if 'dbz3 B2 IK suelo: actor=' in l][:1]
        ok = bool(ini) and len(lados) >= 2 and bool(suelo)
        fila = dict(n=n, a=na, b=nb, ficha_a=fa, ficha_b=fb, ids=ids, ids_reales=ids, ok=ok, rc=rc,
                    inicio_real=ini, lados=lados, suelo=suelo, evidencia=str(ev) if ev else None,
                    segundos=round(time.monotonic() - t0, 1))
        filas.append(fila)
        print('%2d %-14s-%-14s ids %-6s %s rc=%s %.0fs' % (n, na, nb, ids,
              'PASS' if ok else 'FAIL', rc if rc is not None else '?', fila['segundos']), flush=True)
        if salida:
            Path(salida).parent.mkdir(parents=True, exist_ok=True)
            Path(salida).write_text(_json.dumps(dict(copia=copia, grupo=grupo, alta=alta, filas=filas),
                                                ensure_ascii=False, indent=1), encoding='utf-8')
    buenas = sum(1 for f in filas if f['ok'])
    print('parejas: %d/%d PASS' % (buenas, len(filas)))
    return 0 if buenas == len(filas) else 1



def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--autoprueba', action='store_true')
    sp = ap.add_subparsers(dest='cmd')
    c = sp.add_parser('capturar'); c.add_argument('--slot', type=int, required=True); c.add_argument('--grupo', type=int, required=True)
    c.add_argument('--campos', type=int, default=850); c.add_argument('--salida', type=Path, default=SALIDA)
    m = sp.add_parser('comparar'); m.add_argument('--pal', required=True); m.add_argument('--ref', required=True)
    m.add_argument('hd', nargs='*'); m.add_argument('--nombres', help='etiquetas separadas por coma para las carpetas hd')
    e = sp.add_parser('escala', help='escala HD/B2 por ficha (B2F): 53 razones desde el ISO de B2 y los bins HD')
    e.add_argument('--iso-b2', type=Path, help='ISO de Budokai 2 (si no, DBZ3_PS2_GAMES/ps2_games)')
    e.add_argument('--json', type=Path, default=Path(__file__).resolve().parent / 'dorados/b2_ik_tabla_pal.json')
    e.add_argument('--escribir', action='store_true', help='actualiza la clave "escala" del json')
    mo = sp.add_parser('modelo', help='razon de escala HD/PAL medida en dos trazas (control del banco)')
    mo.add_argument('--pal', required=True); mo.add_argument('--hd', required=True); mo.add_argument('--actor', type=int, choices=(0, 1))
    pp = sp.add_parser('parejas', help='29 parejas de la rueda con el arnes solb2x_lanzamiento (1 ventana)')
    pp.add_argument('--copia', default='smoke_c', choices=('smoke_c', 'smoke_d', 'smoke_e', 'smoke_f'))
    pp.add_argument('--grupo', type=int, default=4, choices=(0, 2, 4, 7, 8))
    pp.add_argument('--alta', type=int, default=32, help='alta del grupo en la copia (log: "escenarios: alta N ... entrada 5004")')
    pp.add_argument('--desde', type=int, default=0); pp.add_argument('--hasta', type=int)
    pp.add_argument('--salida', type=Path, default=Path('E:/DBZ3HD_trabajo/S5/parejas_29.json'))
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    if a.cmd == 'capturar':
        return capturar(a)
    if a.cmd == 'escala':
        escala_tabla(a.iso_b2, a.escribir, a.json)
        return 0
    if a.cmd == 'modelo':
        modelo_traza(a.pal, a.hd, a.actor)
        return 0
    if a.cmd == 'parejas':
        return parejas_rueda(a.copia, a.grupo, a.alta, a.desde, a.hasta, a.salida)
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

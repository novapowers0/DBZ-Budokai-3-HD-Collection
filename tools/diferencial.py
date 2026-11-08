"""diferencial.py v0 (fase0C, 6/11): comparador original (PCSX2) contra port, Python puro (solo biblioteca estandar).

Formato unico de traza (JSONL): una fila por dato  {"t": reloj comun, "canal": str, "clave": str, "valor": ...}
  reloj comun: 1 unidad = 1 cuadro del port; PAL: t = campo*1,2 (o el 'reloj' de la traza B2). Port B2 HD: t = cuadro - desfase.
  canales continuos : hueso (clave 'a0/NOMBRE', valor [w,x,y,z]), camara (ojo/objetivo, [x,y,z]), fov (escalar)
  canales de ESTADO : estado (clave actor, se detectan sus CAMBIOS como eventos), clip (solo corta la interpolacion)
  canales de evento : decision, tarea, qte, cartel, sonido, orden (cada fila es un evento)
Algoritmo (plan_un_dia seccion 4 / audit_comparador): eventos -> SequenceMatcher -> anclas -> tiempo deformado por tramos ->
continuos con slerp (sin mezclar al cambiar el clip) -> brechas ordenadas -> brechas.{md,json}. Lo que va detras de una
decision distinta se marca como CONSECUENCIA y no cuenta.

uso:
  python tools/diferencial.py --autoprueba
  python tools/diferencial.py bubbles --orig <carpeta 7D1 con cuadros.jsonl> [--csv bb.csv] [--out DIR]
  python tools/diferencial.py b2 --pal <traza.json PAL> --hd <carpeta con traza_hd.jsonl> [--desfase 1.0] [--out DIR]
"""
import argparse
import bisect
import csv
import difflib
import json
import math
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dif_mapa_b2_pal as MB2

RELOJ_PAL = 1.2
UMBRAL = dict(hueso=5.0,        # grados por hueso (plan: <=5)
              huesos_ok=25,     # actor con >=27 huesos: bien si >=25 <=5 (metrica eB2f); con menos: error medio <=5
              camara=0.01,      # fraccion de la distancia ojo-objetivo
              fov=0.5,          # grados
              ancla=1.0,        # deriva maxima de un ancla (cuadros)
              arranque=2.5)     # los eventos de los primeros 2,5 relojes del solape no cuentan (el motor tarda en aplicar el clip)
EVENTOS_CAMBIO = {'estado'}                       # canales muestreados: el evento es el cambio de valor
EVENTOS_FILA = {'decision', 'tarea', 'qte', 'cartel', 'sonido', 'orden'}
GRAVEDAD = {'evento': 0, 'decision': 1, 'tiempo': 2, 'pose': 3, 'camara': 4, 'sonido': 5}


# ---------------------------------------------------------------- trazas
def fila(t, canal, clave, valor):
    return dict(t=round(float(t), 4), canal=canal, clave=str(clave), valor=valor)


def escribir(filas, ruta):
    Path(ruta).parent.mkdir(parents=True, exist_ok=True)
    with open(ruta, 'w', encoding='utf-8') as f:
        for r in filas:
            f.write(json.dumps(r, ensure_ascii=False, separators=(',', ':')) + '\n')


def leer(ruta):
    return [json.loads(l) for l in open(ruta, encoding='utf-8') if l.strip()]


# ---------------------------------------------------------------- cuaternios (puro Python)
def quat16(m):
    """m: 16 floats fila a fila (4x4 del juego). Rotacion R = parte 3x3 traspuesta (convencion columna de eB2d). -> [w,x,y,z]."""
    r = [[m[4 * j + i] for j in range(3)] for i in range(3)]
    tr = r[0][0] + r[1][1] + r[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = [s / 4, (r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s]
    elif r[0][0] >= r[1][1] and r[0][0] >= r[2][2]:
        s = math.sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2
        q = [(r[2][1] - r[1][2]) / s, s / 4, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s]
    elif r[1][1] >= r[2][2]:
        s = math.sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2
        q = [(r[0][2] - r[2][0]) / s, (r[0][1] + r[1][0]) / s, s / 4, (r[1][2] + r[2][1]) / s]
    else:
        s = math.sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2
        q = [(r[1][0] - r[0][1]) / s, (r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, s / 4]
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return [c / n for c in q]


def angulo(a, b):
    d = min(1.0, abs(sum(x * y for x, y in zip(a, b))))
    return 2 * math.degrees(math.acos(d))


def slerp(a, b, u):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0:
        b, d = [-c for c in b], -d
    if d > 0.9995:
        q = [x + u * (y - x) for x, y in zip(a, b)]
    else:
        th = math.acos(d)
        s0, s1 = math.sin((1 - u) * th) / math.sin(th), math.sin(u * th) / math.sin(th)
        q = [s0 * x + s1 * y for x, y in zip(a, b)]
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return [c / n for c in q]


# ---------------------------------------------------------------- adaptadores: Bubbles (IW)
def _ordinal(seq):
    """[(t, codigo)] -> [(t, 'S<n>')]: n = orden de aparicion de cada tramo (los ids de PAL y del port no son el mismo sistema)."""
    out, ult, n = [], object(), -1
    for t, c in seq:
        if c != ult:
            n, ult = n + 1, c
        out.append((t, 'S%d' % n))
    return out


def bubbles_original(carpeta):
    """carpeta/cuadros.jsonl de tools/iwx_original_cuadros.py (7D1). t = (campo-primer_campo)*1,2."""
    filas_in = leer(Path(carpeta) / 'cuadros.jsonl')
    n0 = filas_in[0]['n']
    out, prev = [], None
    for r in filas_in:
        b = r.get('bubbles') or {}
        npc = b.get('npc') or {}
        t = (r['n'] - n0) * RELOJ_PAL
        if 'estado' in npc:
            out.append(fila(t, 'estado', 'npc', npc['estado']))
            out.append(fila(t, 'clip', 'npc', npc.get('codigo')))
        ta = b.get('tarea')
        clave = ta['p'] if ta else None
        if ta and clave != prev:
            out.append(fila(t, 'decision', 'tarea', [ta['modo'], ta['fase'], round(ta['duracion'], 3)]))
        prev = clave
    return out


def bubbles_csv(ruta):
    """CSV del port (TEST_GP2_BB_CSV): una fila por tick; tick == un campo (t = tick*1,2, igual que el original)."""
    out = []
    for r in csv.DictReader(open(ruta, encoding='utf-8')):
        t = (int(r['tick']) - 1) * RELOJ_PAL
        out.append(fila(t, 'estado', 'npc', int(r['estado'])))
        out.append(fila(t, 'clip', 'npc', int(r['codigo'])))
    return out


# receta del port (Maquina::Crear) tal como la valido opusBB (E:/DBZ3HD_trabajo/tmp/opusBB/decisiones.py): copiada aqui, ya no depende de E:/DBZ3HD_trabajo/tmp
_M = 0xFFFFFFFF


def _lfsr(s):
    return ((s << 1) | (((s >> 31) ^ (s >> 4)) & 1)) & _M


def _f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def receta_bubbles(estado, dific=2, forzado=0):
    """Devuelve (modo, fase, duracion) de la receta del port a partir de una semilla (modulo sin signo, 32 bits en el 1o)."""
    st = [estado]

    def mem(n):
        for _ in range(n):
            st[0] = _lfsr(st[0])
        return (st[0] - 1) & _M
    modo = mem(32) % 3 + 1
    if forzado:
        modo = forzado
    lim = dific * 20 + 100
    fase = 0
    if modo == 2:
        fase = mem(32) & 1
        dur = mem(32) % 100 + 100
    elif modo == 4:
        dur = mem(32) % 50 + 100
    elif modo == 5:
        dur = mem(32) % (200 - dific * 20)
    else:
        dur = mem(32) % lim + (35 if modo == 1 else 60)
    return modo, fase, _f32(float(dur))


def bubbles_port_receta(carpeta, dific=2):
    """Decisiones que tomaria el port: para cada tarea nueva del original se busca, entre la semilla del cuadro anterior y la
    del actual (pasos del LFSR de ese cuadro), un desfase donde la receta del port da el mismo modo/fase/duracion (metodo opusBB).
    Si no hay ninguno, la decision del port es 'sin_receta'."""
    filas_in = leer(Path(carpeta) / 'cuadros.jsonl')
    n0 = filas_in[0]['n']
    out, prev = [], None
    for a, b in zip(filas_in, filas_in[1:]):
        ta = b['bubbles'].get('tarea')
        t0 = a['bubbles'].get('tarea')
        if not ta or (t0 and t0['p'] == ta['p']):
            continue
        s0, s1 = a['bubbles']['seed'], b['bubbles']['seed']
        objetivo = (ta['modo'], ta['fase'], _f32(ta['duracion']))
        st, hit = s0, None
        for _ in range(4000):
            if st == s1:
                break
            if receta_bubbles(st, dific, ta['modo'] if ta['modo'] in (4, 5) else 0) == objetivo:
                hit = receta_bubbles(st, dific, ta['modo'] if ta['modo'] in (4, 5) else 0)
                break
            st = _lfsr(st)
        t = (b['n'] - n0) * RELOJ_PAL
        out.append(fila(t, 'decision', 'tarea', [hit[0], hit[1], round(hit[2], 3)] if hit else 'sin_receta'))
    return out


# ---------------------------------------------------------------- adaptadores: B2 (PAL vs port HD)
def _mats_pal(rec, k):
    raw = bytes.fromhex(rec['actores'][k]['modelo_raw'])
    return [struct.unpack_from('<16f', raw, MB2.PAL_MATS_OFFSET + MB2.PAL_MATS_PASO * j) for j in range(MB2.PAL_N)]


def _mats_hd(a):
    n = len(a['acm_nombres'])
    w = a['modelo_raw']
    b = struct.pack('<%dI' % len(w), *w)
    return [struct.unpack_from('<16f', b, MB2.HD_MATS_OFFSET + MB2.HD_MATS_PASO * j) for j in range(n)]


def _mapas_b2(hd_primer_paso):
    """nombre -> (indice PAL, indice HD) por actor: victima (a1) con PAL_IDX/HD_IDX, atacante (a0) con mapa_atacante (cmp5)."""
    m = {1: {n: (MB2.PAL_IDX[n], MB2.HD_IDX[n]) for n in MB2.PAL_IDX if n in MB2.HD_IDX}}
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mod center hd'))   # fk_b1 (lo pide solb2x_acm)
        import solb2x_huesos_cmp5 as C5      # importa numpy/scipy y lee el .b2t del mod de la Ciudad
        m[0] = C5.mapa_atacante(hd_primer_paso['actores'][0]['acm_nombres'])
    except Exception as e:                   # sin atacante: se avisa, no se inventa
        print('AVISO: sin atacante (a0):', e, file=sys.stderr)
    return m


def hd_pasos(carpeta):
    out = []
    for l in open(Path(carpeta) / 'traza_hd.jsonl', encoding='utf-8'):
        try:
            x = json.loads(l)
        except ValueError:
            continue
        if x.get('tipo') == 'paso' and all('modelo_raw' in a for a in x['actores']):
            out.append(x)
    return out


def b2_pal(ruta, mapas):
    P = [r for r in json.load(open(ruta, encoding='utf-8')) if r.get('evento') and r['actores'][0].get('modelo_raw')]
    out = []
    for k in mapas:
        est = _ordinal([(r['reloj'], r['actores'][k]['codigo']) for r in P])
        for r, (_, e) in zip(P, est):
            t = r['reloj']
            out.append(fila(t, 'estado', 'a%d' % k, e))
            out.append(fila(t, 'clip', 'a%d' % k, r['actores'][k]['clip']))
            mats = _mats_pal(r, k)
            for n, (ip, _) in mapas[k].items():
                out.append(fila(t, 'hueso', 'a%d/%s' % (k, n), quat16(mats[ip])))
    return out


def b2_hd(carpeta, mapas, desfase=MB2.DESFASE_HD):
    H = hd_pasos(carpeta)
    out = []
    for k in mapas:
        est = _ordinal([(x['cuadro'] - desfase, x['actores'][k]['accion']) for x in H])
        for x, (_, e) in zip(H, est):
            t = x['cuadro'] - desfase
            out.append(fila(t, 'estado', 'a%d' % k, e))
            out.append(fila(t, 'clip', 'a%d' % k, x['actores'][k]['clip_modelo']))
            mats = _mats_hd(x['actores'][k])
            for n, (_, ih) in mapas[k].items():
                out.append(fila(t, 'hueso', 'a%d/%s' % (k, n), quat16(mats[ih])))
    return out


# ---------------------------------------------------------------- comparacion
def eventos(filas, t0, t1, arranque):
    """Eventos ordenados por t dentro del solape [t0+arranque, t1]."""
    ev, ult = [], {}
    for r in sorted((r for r in filas if r['canal'] in EVENTOS_CAMBIO or r['canal'] in EVENTOS_FILA), key=lambda r: r['t']):
        if r['canal'] in EVENTOS_CAMBIO:
            k = (r['canal'], r['clave'])
            cambia = k in ult and ult[k] != r['valor']
            ult[k] = r['valor']
            if not cambia:
                continue
        if t0 + arranque <= r['t'] <= t1:
            ev.append(r)
    return ev


def _firma(e):
    return (e['canal'], e['clave'], json.dumps(e['valor'], sort_keys=True))


def _deformar(anclas):
    """t_test -> t_ref lineal por tramos entre anclas [(t_test, t_ref)]; fuera de ellas, el desplazamiento del ancla mas cercana."""
    a = sorted(anclas)
    xs = [p[0] for p in a]

    def f(t):
        if not a:
            return t
        if t <= xs[0]:
            return t + a[0][1] - a[0][0]
        if t >= xs[-1]:
            return t + a[-1][1] - a[-1][0]
        j = bisect.bisect_right(xs, t) - 1
        (x0, y0), (x1, y1) = a[j], a[j + 1]
        return y0 if x1 == x0 else y0 + (t - x0) * (y1 - y0) / (x1 - x0)
    return f


class Serie:
    def __init__(self):
        self.t, self.v = [], []

    def add(self, t, v):
        self.t.append(t)
        self.v.append(v)


def _series(filas, canales):
    s = {}
    for r in sorted(filas, key=lambda r: r['t']):
        if r['canal'] in canales:
            s.setdefault((r['canal'], r['clave']), Serie()).add(r['t'], r['valor'])
    return s


def _muestra(serie, t, clip, interp):
    """Valor de la serie de referencia en t. Cuaternio->slerp, vector/escalar->lineal. Si el clip cambia entre las dos
    muestras vecinas no se mezcla (la mezcla seria un artefacto, eB2h): se toma la mas cercana. interp=False: siempre la mas cercana."""
    ts = serie.t
    if not ts or t < ts[0] - 1e-9 or t > ts[-1] + 1e-9:
        return None
    j = min(max(bisect.bisect_right(ts, t) - 1, 0), max(len(ts) - 2, 0))
    if len(ts) == 1:
        return serie.v[0]
    a, b = serie.v[j], serie.v[j + 1]
    u = 0.0 if ts[j + 1] == ts[j] else min(1.0, max(0.0, (t - ts[j]) / (ts[j + 1] - ts[j])))
    if not interp or (clip is not None and clip(ts[j]) != clip(ts[j + 1])):
        u = float(round(u))
    if isinstance(a, list) and len(a) == 4 and isinstance(a[0], float) and abs(sum(c * c for c in a) - 1) < 1e-3:
        return slerp(a, b, u)
    if isinstance(a, list):
        return [x + u * (y - x) for x, y in zip(a, b)]
    return a + u * (b - a)


def comparar(ref, test, interp=True, umbral=UMBRAL, canales=None, solape=None):
    """ref = original, test = port (misma unidad de reloj). Devuelve dict(brechas, resumen, anclas)."""
    ts = [r['t'] for r in ref]
    tt = [r['t'] for r in test]
    t0, t1 = solape or (max(min(ts), min(tt)), min(max(ts), max(tt)))
    if canales:
        ref = [r for r in ref if r['canal'] in canales or r['canal'] == 'clip']
        test = [r for r in test if r['canal'] in canales or r['canal'] == 'clip']
    ev_r, ev_t = eventos(ref, t0, t1, umbral['arranque']), eventos(test, t0, t1, umbral['arranque'])
    brechas, anclas = [], []
    sm = difflib.SequenceMatcher(None, [_firma(e) for e in ev_r], [_firma(e) for e in ev_t], autojunk=False)

    def b(tipo, canal, clave, t_ini, t_fin, detalle, puntos):
        brechas.append(dict(tipo=tipo, canal=canal, clave=clave, t_ini=round(t_ini, 2), t_fin=round(t_fin, 2),
                            detalle=detalle, puntos=round(puntos, 2), consecuencia=False))
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for er, et in zip(ev_r[i1:i2], ev_t[j1:j2]):
                anclas.append((et['t'], er['t']))
                if abs(et['t'] - er['t']) > umbral['ancla']:
                    b('tiempo', er['canal'], er['clave'], er['t'], et['t'],
                      'ancla %s %s deriva %.2f cuadros (original t=%.2f, port t=%.2f)' % (er['canal'], er['clave'], et['t'] - er['t'], er['t'], et['t']),
                      abs(et['t'] - er['t']) - umbral['ancla'])
            continue
        er_, et_ = list(ev_r[i1:i2]), list(ev_t[j1:j2])
        while er_ and et_ and (er_[0]['canal'], er_[0]['clave']) == (et_[0]['canal'], et_[0]['clave']):
            x, y = er_.pop(0), et_.pop(0)
            b('decision' if x['canal'] == 'decision' else 'evento', x['canal'], x['clave'], x['t'], y['t'],
              'valor distinto en %s %s: original %s, port %s' % (x['canal'], x['clave'], x['valor'], y['valor']), 1)
        for x in er_:
            b('decision' if x['canal'] == 'decision' else 'evento', x['canal'], x['clave'], x['t'], x['t'],
              'FALTA en el port: %s %s = %s' % (x['canal'], x['clave'], x['valor']), 1)
        for y in et_:
            b('decision' if y['canal'] == 'decision' else 'evento', y['canal'], y['clave'], y['t'], y['t'],
              'SOBRA en el port: %s %s = %s' % (y['canal'], y['clave'], y['valor']), 1)
    warp = _deformar(anclas)
    # --- continuos
    sr = _series(ref, {'hueso', 'camara', 'fov', 'clip'})
    clips = {k[1]: sr[k] for k in sr if k[0] == 'clip'}

    def clipf(actor):
        s = clips.get(actor)
        return None if s is None else (lambda t, s=s: s.v[max(bisect.bisect_right(s.t, t + 1e-9) - 1, 0)])
    err_t = {}            # (t, actor) -> [(clave, error)]
    for r in test:
        if r['canal'] == 'hueso':
            actor = r['clave'].split('/')[0]
            s = sr.get(('hueso', r['clave']))
            if s is None:
                continue
            q = _muestra(s, warp(r['t']), clipf(actor), interp)
            if q is not None:
                err_t.setdefault((r['t'], actor), []).append((r['clave'], angulo(q, r['valor'])))
        elif r['canal'] in ('camara', 'fov'):
            s = sr.get((r['canal'], r['clave']))
            q = s and _muestra(s, warp(r['t']), None, interp)
            if q is None:
                continue
            if r['canal'] == 'fov':
                e, lim = abs(q - r['valor']), umbral['fov']
            else:
                so = sr.get(('camara', 'objetivo'))
                ob = so and _muestra(so, warp(r['t']), None, interp)
                d = math.dist(q, ob) if ob else 1.0
                e, lim = math.dist(q, r['valor']) / (d or 1.0), umbral['camara']
            if e > lim:
                b('camara', r['canal'], r['clave'], r['t'], r['t'], '%s %s error %.4g (umbral %.4g)' % (r['canal'], r['clave'], e, lim), e / lim - 1)
    resumen = {}
    malos = {}
    for (t, actor), lst in sorted(err_t.items()):
        errs = [e for _, e in lst]
        if len(errs) >= umbral['huesos_ok'] + 2:
            ok = sum(e <= umbral['hueso'] for e in errs) >= umbral['huesos_ok']
        else:
            ok = sum(errs) / len(errs) <= umbral['hueso']
        rs = resumen.setdefault(actor, [0, 0, 0.0])
        rs[1] += 1
        rs[0] += ok
        if not ok:
            mal = sorted(lst, key=lambda x: -x[1])
            malos.setdefault(actor, []).append((t, sum(e for _, e in lst) / len(lst), mal[:3]))
        rs[2] = max(rs[2], max(errs))
    for actor, lst in malos.items():
        paso = sorted({t for (t, a) in err_t if a == actor})
        pos = {t: i for i, t in enumerate(paso)}
        tr = []
        for t, m, mal in lst:
            if tr and pos[t] - pos[tr[-1][1]] == 1:
                tr[-1][1] = t
                tr[-1][2].append(m)
                tr[-1][3] = max(tr[-1][3], mal, key=lambda l: l[0][1])
            else:
                tr.append([t, t, [m], mal])
        for ini, fin, ms, mal in tr:
            m = sum(ms) / len(ms)
            b('pose', 'hueso', actor, ini, fin, '%s: %d muestras fuera de umbral, error medio %.1f grados, peores %s' % (
                actor, len(ms), m, ', '.join('%s %.0f' % (c.split('/')[1], e) for c, e in mal)), (fin - ini + 1) * max(0.0, m - umbral['hueso']))
    # --- consecuencias: todo lo posterior a la 1a decision distinta no cuenta aparte
    dec = [x['t_ini'] for x in brechas if x['tipo'] == 'decision']
    if dec:
        for x in brechas:
            if x['tipo'] != 'decision' and x['t_ini'] > min(dec):
                x['consecuencia'] = True
    reales = [x for x in brechas if not x['consecuencia']]
    reales.sort(key=lambda x: (x['t_ini'], GRAVEDAD[x['tipo']], -x['puntos']))
    if reales:       # la que aparece antes primero; el resto por gravedad y puntos
        primero, resto = reales[0], sorted(reales[1:], key=lambda x: (GRAVEDAD[x['tipo']], -x['puntos'], x['t_ini']))
        reales = [primero] + resto
    cons = [x for x in brechas if x['consecuencia']]
    return dict(brechas=reales + cons, reales=len(reales), consecuencias=len(cons),
                resumen={a: dict(buenas=v[0], muestras=v[1], error_max=round(v[2], 1)) for a, v in resumen.items()},
                eventos=dict(original=len(ev_r), port=len(ev_t), anclas=len(anclas)), solape=[round(t0, 2), round(t1, 2)])


def informe(nombre, res, salida, notas=''):
    """Escribe brechas.md y brechas.json en salida y devuelve la linea de una frase de la escena."""
    salida = Path(salida)
    salida.mkdir(parents=True, exist_ok=True)
    linea = '%s: %s' % (nombre, 'IGUAL' if not res['reales'] else '%d brechas' % res['reales'])
    if res['consecuencias']:
        linea += ' (+%d consecuencias)' % res['consecuencias']
    md = ['# %s' % linea, '', notas, '', 'solape t=%s..%s; eventos %s' % (res['solape'][0], res['solape'][1], res['eventos']), '']
    for a, v in sorted(res['resumen'].items()):
        md.append('- %s: %d de %d muestras dentro de umbral (error maximo %.1f grados)' % (a, v['buenas'], v['muestras'], v['error_max']))
    md += ['', '## Brechas (la mas temprana primero, luego gravedad y puntos)', '']
    for i, x in enumerate(res['brechas'], 1):
        md.append('%d. [%s]%s t=%.1f..%.1f pts=%.1f: %s' % (i, x['tipo'], ' (consecuencia)' if x['consecuencia'] else '', x['t_ini'], x['t_fin'], x['puntos'], x['detalle']))
    if not res['brechas']:
        md.append('Ninguna.')
    (salida / 'brechas.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    (salida / 'brechas.json').write_text(json.dumps(dict(escena=nombre, linea=linea, **res), indent=1, ensure_ascii=False), encoding='utf-8')
    return linea


# ---------------------------------------------------------------- escenas
def escena_bubbles(orig, csv_port, out):
    ref = bubbles_original(orig)
    # 1) decisiones: original contra la receta del port (metodo opusBB)
    r1 = comparar(ref, bubbles_port_receta(orig), canales={'decision'}, solape=(min(r['t'] for r in ref), max(r['t'] for r in ref)))
    out = Path(out)
    l1 = informe('bubbles/decisiones', r1, out / 'decisiones', 'port = receta Maquina::Crear buscada en la ventana de semillas de cada cuadro (opusBB).')
    n_dec = sum(1 for r in ref if r['canal'] == 'decision')
    print('%s  [decisiones del original: %d]' % (l1, n_dec))
    res = {'decisiones': (r1, n_dec)}
    if csv_port:
        r2 = comparar(ref, bubbles_csv(csv_port), canales={'estado'})
        print(informe('bubbles/trayectoria_csv', r2, out / 'trayectoria_csv',
                      'CSV del port sin semilla ni entrada por paso: la trayectoria NO es la del original; informativo, no certifica nada.'))
        res['trayectoria'] = (r2, None)
    return res


def escena_b2(pal, hd, out, desfase=MB2.DESFASE_HD, interp=True, nombre='b2'):
    H = hd_pasos(hd)
    mapas = _mapas_b2(H[0])
    res = comparar(b2_pal(pal, mapas), b2_hd(hd, mapas, desfase), interp=interp)
    print(informe(nombre, res, out, 'PAL interpolado (slerp, sin mezclar cambios de clip) a los tiempos del port; desfase %.1f.' % desfase))
    return res


# ---------------------------------------------------------------- camara IW relativa al jugador y por tramo (P2, 7/11)
# Original: muestras del dorado (t, eye, tgt, j, amc). Port: lineas 'iwmini: jugador (..) [clip 0x..] ojo (..) [objetivo (..)]'.
# Tramo original = racha de #AMC igual (un minijuego de botones usa #AMC 1 unos segundos y luego #AMC 0). Tramo port = racha
# del mismo clip (el port no escribe el #AMC del original). Mediana por tramo de: distancia ojo-jugador, altura del ojo sobre
# el jugador y yaw del ojo RESPECTO a la linea jugador->objetivo (si falta el objetivo en un lado: yaw del ojo alrededor del
# jugador, que solo vale si los dos juegan en el espacio del clip, jugador en el origen).
def _med(v):
    v = sorted(v)
    n = len(v)
    return v[n // 2] if n % 2 else (v[n // 2 - 1] + v[n // 2]) / 2


def _giro(a, b):
    return abs((a - b + 180) % 360 - 180)


def _rel(f):
    """(dist, altura, yaw, con_objetivo) de una fila respecto al jugador; yaw respecto a jugador->objetivo si hay objetivo."""
    e, j, t = f['eye'], f['j'], f.get('tgt') or (0, 0, 0)
    if not any(abs(x) > 1e-6 for x in e):
        return None
    yaw = yabs = math.degrees(math.atan2(e[0] - j[0], e[2] - j[2]))
    con = any(abs(x) > 1e-6 for x in t)
    if con and math.hypot(t[0] - j[0], t[2] - j[2]) > 1.0:   # objetivo encima del jugador (campo: j + (0,14,0)): sin direccion
        yaw = (yaw - math.degrees(math.atan2(t[0] - j[0], t[2] - j[2])) + 180) % 360 - 180
    return math.dist(e, j), e[1] - j[1], yaw, con, yabs


def tramos(filas, clave):
    """Lista de dicts {k, n, dist, altura, yaw, con_obj} por racha consecutiva de clave(fila); descarta filas sin ojo."""
    out, cur = [], None
    for f in filas:
        r = _rel(f)
        if r is None:
            cur = None
            continue
        k = clave(f)
        if cur is None or cur['k'] != k:
            cur = {'k': k, 'v': [], 'f': []}
            out.append(cur)
        cur['v'].append(r)
        cur['f'].append(f)
        cur['clip'] = bool(f.get('clip'))
    res = []
    for c in out:
        ej = [tuple(f['eye']) for f in c['f']]
        med = tuple(_med([e[i] for e in ej]) for i in range(3))
        dd_, hh_ = sorted(x[0] for x in c['v']), sorted(x[1] for x in c['v'])
        pc = lambda v, q: v[min(len(v) - 1, int(q * (len(v) - 1) + 0.5))]   # noqa: E731
        res.append({'d10': pc(dd_, .1), 'd90': pc(dd_, .9), 'h10': pc(hh_, .1), 'h90': pc(hh_, .9), 'k': c['k'], 'n': len(c['v']), 'dist': _med([x[0] for x in c['v']]), 'altura': _med([x[1] for x in c['v']]),
                    'yaw': _med([x[2] for x in c['v']]), 'yaw_abs': _med([x[4] for x in c['v']]), 'con_obj': all(x[3] for x in c['v']), 'clip': c.get('clip', False),
                    't_dur': (c['f'][-1].get('t', 0) - c['f'][0].get('t', 0)) if 't' in c['f'][0] else 0.0,
                    'eye': med, 'span': max(max(e[i] for e in ej) - min(e[i] for e in ej) for i in range(3))})
    return res


def serie_port_iw(texto):
    """Filas del port con tgt y clip (clip None = linea 'marcha' de la camara de partida)."""
    rx = re.compile(r"iwmini: jugador \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)(?: clip (0x[0-9A-Fa-f]+))?.*?ojo \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)"
                    r"(?: objetivo \(([-\d.]+), ([-\d.]+), ([-\d.]+)\))?")
    filas = []
    for l in texto.splitlines():
        m = rx.search(l)
        if not m:
            continue
        g = m.groups()
        filas.append({'clip': g[3], 'j': tuple(float(x) for x in g[0:3]), 'eye': tuple(float(x) for x in g[4:7]),
                      'tgt': tuple(float(x) for x in g[7:10]) if g[7] else (0.0, 0.0, 0.0)})
    return filas          # las lineas sin clip ('marcha': camara de partida, p. ej. 0x7E1 en juego) y las de clip son candidatos distintos


def comparar_camara_iw(muestras_oro, filas_port, tol=0.15):
    """Lista (nombre, ok, detalle), una por tramo del original (el mas largo y los siguientes). Un tramo esta CUBIERTO si algun
    tramo del port coincide:
      a) relativo al jugador: distancia y altura del ojo (|dif| <= 15 % de la mediana original, minimo 1.0) y giro <= 15 grados
         (el giro solo cuenta si ambos lo miden respecto a jugador->objetivo o el port esta en el espacio del clip);
      b) o ojo FIJO en absoluto: los dos tramos con el ojo quieto (extension <= 3 %) y a menos de 3 % (minimo 1.0) uno del otro.
         Una camara fija no depende de donde juegue cada partida (p. ej. 0x7D8: ojo (0,150,37) mientras Piccolo se mueve)."""
    filas_port_todas = filas_port
    jo_oro = [f['j'] for f in muestras_oro if any(abs(x) > 1e-6 for x in f.get('eye', (0, 0, 0)))]
    en_origen = bool(jo_oro) and _med([math.hypot(*j) for j in jo_oro]) < 1.0   # 0x7D1/0x7DD/0x7DF: el jugador SALE en (0,0,0)
    if not en_origen and any(any(abs(x) > 1e-6 for x in f['j']) for f in filas_port):   # el port escribe j=(0,0,0) fuera de la partida: no es el jugador
        filas_port = [f for f in filas_port if any(abs(x) > 1e-6 for x in f['j'])]
    to = tramos(muestras_oro, lambda f: f.get('amc', 0))
    to = [t for t in to if t['n'] >= 3] or to          # un tramo de 1-2 muestras es el arranque del clip, no una camara
    # El dorado empieza en la presentacion (0x7D8: #AMC 1 los primeros 16 s, luego #AMC 0 = el juego) y puede acabar en el plano
    # de resultado: cuenta el tramo de mayor duracion; los demas salen como informacion (ok None).
    n_cuenta = max(range(len(to)), key=lambda k: (to[k]['t_dur'] or to[k]['n'], k)) if to else 0   # el tramo de MAYOR DURACION (el juego); empate: el ultimo
    tp = tramos(filas_port, lambda f: f.get('clip'))
    if filas_port is not filas_port_todas:               # tambien los tramos con j=(0,0,0): camaras fijas de partida (0x7D8: ojo (0,150,37) con j=0)
        sin_j = [f for f in filas_port_todas if not any(abs(x) > 1e-6 for x in f['j'])]
        tp += [t for t in tramos(sin_j, lambda f: f.get('clip')) if t['span'] <= max(0.03 * math.hypot(*t['eye']), 0.5)]
    if not to:
        return [('camara por tramo', None, 'el dorado no tiene ojo medido [H]')]
    if not tp:
        return [('camara por tramo', False, 'sin muestras de ojo del port en el log')]
    fijo = lambda t: t['span'] <= max(0.03 * math.hypot(*t['eye']), 0.5)   # noqa: E731
    out = []
    for i, o in enumerate(to):
        if i != n_cuenta:
            out.append((f"camara tramo {i + 1}/{len(to)} (#AMC {o['k']}, {o['n']} muestras, fuera del juego: no cuenta)", None,
                        f"original dist {o['dist']:.1f} alt {o['altura']:.1f}; solo informativo (el juego es el tramo de mayor duracion)"))
            continue
        best = None
        for p in tp:
            gir = o['con_obj'] and p['con_obj']
            comp = gir or p['clip']
            dd, dh = abs(o['dist'] - p['dist']), abs(o['altura'] - p['altura'])
            dy = _giro(o['yaw'], p['yaw']) if gir else _giro(o['yaw_abs'], p['yaw_abs'])   # sin objetivo en alguno: giro absoluto de los dos
            movil = not fijo(o)    # camara que se mueve en el original: el port solo da un ojo por clip, asi que giro no cuenta y distancia/altura valen si caen en la banda p10-p90 del original (+15 %)
            comp = comp and not movil
            en_banda = movil and o['d10'] * (1 - tol) - 1.0 <= p['dist'] <= o['d90'] * (1 + tol) + 1.0 and o['h10'] * (1 - tol) - 1.0 <= p['altura'] <= o['h90'] * (1 + tol) + 1.0
            ok_rel = ((dd <= max(abs(o['dist']) * tol, 1.0) and dh <= max(abs(o['altura']) * tol, 1.0)) or en_banda) and (dy <= 15 or not comp)
            de = math.dist(o['eye'], p['eye'])
            ok_abs = fijo(o) and fijo(p) and de <= max(0.03 * math.hypot(*o['eye']), 1.0)
            sc = min(dd / max(abs(o['dist']), 1) + dh / max(abs(o['altura']), 1) + dy / 90, de / max(math.hypot(*o['eye']), 1) if fijo(o) and fijo(p) else 9e9)
            if best is None or (ok_rel or ok_abs, -sc) > (best[0], -best[1]):
                best = (ok_rel or ok_abs, sc, p, dd, dh, dy, gir, comp, ok_rel, ok_abs, de, movil, en_banda)
        ok, sc, p, dd, dh, dy, gir, comp, ok_rel, ok_abs, de, movil, en_banda = best
        base = 'giro respecto a jugador->objetivo' if gir else 'giro absoluto en el espacio del clip' if comp else 'giro NO comparable (el port no escribe el objetivo y su mundo no es el del clip): no cuenta'
        if movil and en_banda:
            base = 'camara en movimiento en el original: el ojo del port cae en la banda p10-p90 del original (giro no cuenta)'
        if ok_abs and not ok_rel:
            base = f'IGUAL por ojo fijo en absoluto (dif {de:.1f}); relativo al jugador distinto porque el jugador no esta en el mismo sitio'
        # T1B mejora 4: PASS que ignora el giro (56, 135 grados...) no es una prueba de igualdad: pasa a [H] clase medida
        if ok and not ok_abs and dy > 15 and (gir or (movil and en_banda)):   # giro medible pero ignorado (el 'no comparable' del mundo girado se respeta)
            ok, base = None, f"{base}; el giro difiere {dy:.0f} grados y esta fila lo ignoraba: no cuenta como igual (medida)"
        out.append((f"camara tramo {i + 1}/{len(to)} (#AMC {o['k']}, {o['n']} muestras)", ok,
                    f"original dist {o['dist']:.1f} alt {o['altura']:.1f} giro {(o['yaw'] if gir else o['yaw_abs']):.1f}; port mas cercano (clip {p['k']}, {p['n']} muestras) "
                    f"dist {p['dist']:.1f} alt {p['altura']:.1f} giro {(p['yaw'] if gir else p['yaw_abs']):.1f}; dif dist {dd:.1f} alt {dh:.1f} giro {dy:.1f} [{base}]"))
    return out


def giro_reposo_iw(muestras_oro, filas_port, tol=15.0):
    """Fase 1 (7/11): giro de la camara de CAMPO (seguimiento) alrededor del jugador, y hacia donde mira (objetivo -
    jugador). La camara de seguimiento es un desplazamiento fijo respecto al jugador (Camera_UpdateFollow): su giro
    absoluto no depende de donde este el jugador (las pruebas lo recolocan), asi que se compara el giro de las filas del
    port cuya distancia horizontal y altura caen en la banda del original (+-15 %, min 1 u): son la camara de juego, no
    la entrada. Original: ultimo cuarto del tramo de juego (jugador quieto). [] si no aplica."""
    vivo = lambda f: any(abs(x) > 1e-6 for x in f['eye'])   # noqa: E731
    runs, cur = [], None
    for f in muestras_oro:
        if not vivo(f):
            cur = None
            continue
        if cur is None or cur[0] != f.get('amc', 0):
            cur = (f.get('amc', 0), [])
            runs.append(cur)
        cur[1].append(f)
    runs = [r for r in runs if len(r[1]) >= 3]
    campo = [f for f in filas_port if f.get('clip') is None and vivo(f)]
    if not runs or not campo:
        return []
    dur = lambda r: (r[1][-1].get('t', 0) - r[1][0].get('t', 0)) or len(r[1])   # noqa: E731
    o = max(reversed(runs), key=dur)[1]
    o = o[-max(3, len(o) // 4):]
    hz = lambda f: math.hypot(f['eye'][0] - f['j'][0], f['eye'][2] - f['j'][2])   # noqa: E731
    al = lambda f: f['eye'][1] - f['j'][1]   # noqa: E731
    yaw = lambda f: math.degrees(math.atan2(f['eye'][0] - f['j'][0], f['eye'][2] - f['j'][2]))   # noqa: E731
    ho, ao = _med([hz(f) for f in o]), _med([al(f) for f in o])
    p = [f for f in campo if abs(hz(f) - ho) <= max(0.15 * ho, 1.0) and abs(al(f) - ao) <= max(0.15 * abs(ao), 1.0)]
    if not p:
        return [('giro de la camara de juego', False, f'ninguna fila del port a distancia {ho:.1f} / altura {ao:.1f} del jugador')]
    yo = _med([yaw(f) for f in o])
    yp = _med([yaw(f) for f in p])
    dy = _giro(yo, yp)
    fuera = sum(_giro(yaw(f), yo) > tol for f in p)
    out = [('giro de la camara de juego', dy <= tol and fuera == 0,
            f'original {yo:.1f} port {yp:.1f} grados alrededor del jugador (dif {dy:.1f}, tolerancia {tol:.0f}); '
            f'{len(p)} filas del port, {fuera} fuera de tolerancia')]
    pt = [f for f in p if any(abs(x) > 1e-6 for x in f.get('tgt', (0, 0, 0)))]
    if pt:   # hacia donde mira: objetivo relativo al jugador (el original de campo: (0, 14, 0), encima de la cabeza)
        to = tuple(_med([f['tgt'][i] - f['j'][i] for f in o]) for i in range(3))
        tp = tuple(_med([f['tgt'][i] - f['j'][i] for f in pt]) for i in range(3))
        dm = math.dist(to, tp)
        out.append(('hacia donde mira la camara', dm <= 1.0, f'objetivo - jugador: original {tuple(round(x, 2) for x in to)} '
                    f'port {tuple(round(x, 2) for x in tp)} (dif {dm:.2f} u, tolerancia 1)'))
    return out


def planos_iw(muestras_oro, filas_port, tol=1.0):
    """Fase 1 (7/11): camaras que CORTAN entre planos fijos (7D5, 7DA: las de botones). El tramo de juego del original es
    uno solo (#AMC 0) con el ojo saltando entre planos; el port escribe una fila por clip. Se compara el CONJUNTO de planos
    sostenidos del original (ojo quieto >= 3 muestras) con todos los ojos del port (filas de clip y de seguimiento: la de
    clip se escribe en el cuadro 0, a veces con el ojo del plano anterior). El orden no se compara. [] si no aplica."""
    vivo = lambda f: any(abs(x) > 1e-6 for x in f['eye'])   # noqa: E731
    planos, cur = [], []
    for f in muestras_oro:
        if not vivo(f):
            continue
        if cur and math.dist(f['eye'], cur[-1]['eye']) < 0.5:
            cur.append(f)
            continue
        if len(cur) >= 3:
            planos.append(cur[0]['eye'])
        cur = [f]
    if len(cur) >= 3:
        planos.append(cur[0]['eye'])
    unicos = []
    for e in planos:
        if all(math.dist(e, u) >= tol for u in unicos):
            unicos.append(e)
    ojos = [f['eye'] for f in filas_port if vivo(f)]
    if len(unicos) < 4 or not ojos:   # 1-3 planos = entrada y camara de seguimiento (campo): eso lo mide giro_reposo_iw
        return []
    faltan = [u for u in unicos if min(math.dist(u, e) for e in ojos) > tol]
    return [('planos de camara', not faltan, f'{len(unicos) - len(faltan)}/{len(unicos)} planos del original con un ojo del port a '
             f'<= {tol} u' + (f'; faltan {[tuple(round(x, 1) for x in u) for u in faltan]}' if faltan else ''))]


def _auto_camara_iw():
    f = lambda e, j, t, a: {'eye': e, 'j': j, 'tgt': t, 'amc': a}   # noqa: E731
    # original con el jugador desplazado (100,0,0): lo relativo no depende de donde este el mundo
    oro = [f((100, 36, 80), (100, 0, 0), (100, 0, 1), 1)] * 25 + [f((100, 10, -30), (100, 0, 0), (100, 0, -1), 0)] * 25
    tr = tramos(oro, lambda x: x['amc'])
    assert [round(t['dist']) for t in tr] == [88, 32] and abs(tr[0]['yaw']) < 1e-6, tr
    # port en otro mundo (jugador en el origen), misma camara relativa -> los dos tramos CUBIERTOS
    port = [{'clip': '0xA', 'eye': (7, 36, 83), 'j': (7, 0, 3), 'tgt': (7, 0, 4)}] * 3 + \
           [{'clip': '0xB', 'eye': (7, 10, -27), 'j': (7, 0, 3), 'tgt': (7, 0, 2)}] * 3
    assert [r[1] for r in comparar_camara_iw(oro, port)] == [None, True], comparar_camara_iw(oro, port)   # el primer tramo es presentacion
    # port de campo (sin clip ni objetivo) con el mundo girado 180: distancia y altura iguales -> IGUAL (el giro no cuenta)
    campo = [{'clip': None, 'eye': (0, 36, -80), 'j': (0, 0, 0), 'tgt': (0, 0, 0)}] * 3
    r = comparar_camara_iw([f((0, 36, 80), (0, 0, 0), (0, 0, 0), 0)] * 4, campo)
    assert r[0][1] is True and 'NO comparable' in r[0][2], r
    # original con jugador fuera del origen y port con j=(0,0,0): ojos en absoluto (caso 0x7D8)
    o8 = [f((0, 150, 37), (0, 104, 0), (0, 150, -1), 0)] * 4
    p8 = [{'clip': None, 'eye': (0, 150, 37), 'j': (0, 0, 0), 'tgt': (0, 0, 0)}] * 3
    r = comparar_camara_iw(o8, p8)
    assert r[0][1] is True and 'ojo fijo' in r[0][2], r
    # el port con filas j=(0,0,0) intercaladas (fuera de partida): se descartan
    p8b = p8 + [{'clip': None, 'eye': (0, 150, 37), 'j': (0, 50, 0), 'tgt': (0, 0, 0)}] * 3
    assert comparar_camara_iw(o8, p8b)[0][1] is True
    # presentacion previa: solo cuenta el ultimo tramo largo; los anteriores salen con ok None
    intro = [f((900, 50, 900), (100, 0, 0), (100, 0, 1), 7)] * 3
    assert [x[1] for x in comparar_camara_iw(intro + oro, port)] == [None, None, True]
    assert [x[1] for x in comparar_camara_iw(oro, port[:3])] == [None, False]      # el port solo tiene la camara alta
    assert [x[1] for x in comparar_camara_iw(oro, port[3:])] == [None, True]
    t = ("x iwmini: jugador (0.00, 0.00, 0.00) clip 0xB4 cuadro 29 ojo (-46.87, 7.67, 3.16) fov 1 #AMC 915\n"
         "y iwmini: jugador (0.0, 0.0, 0.0) marcha (0.00, 0.00) anda 0 ojo (-46.9, 7.7, 3.2)")
    assert len(serie_port_iw(t)) == 2 and serie_port_iw(t)[0]['clip'] == '0xB4' and serie_port_iw(t)[1]['clip'] is None
    # giro en reposo: mismo jugador y mismo giro -> True; girado 90 -> False; objetivo encima de la cabeza
    g = lambda e, t=(0, 14, 0): {'eye': e, 'j': (0, 0, 0), 'tgt': t, 'amc': 5, 'clip': None}   # noqa: E731
    r = giro_reposo_iw([g((47.7, 24, 51.2))] * 8, [g((47.6, 24, 51.3))] * 8)
    assert [x[1] for x in r] == [True, True], r
    assert giro_reposo_iw([g((47.7, 24, 51.2))] * 8, [g((51.2, 24, -47.7))] * 8)[0][1] is False
    pl = [g((-46.9, 7.7, 3.2))] * 4 + [g((17.0, 8.2, 19.6))] * 4 + [g((5, 5, 5))] * 4 + [g((9, 9, 9))] * 4
    port = [g((17.0, 8.17, 19.56)), g((-46.87, 7.67, 3.16)), g((5, 5, 5)), g((9, 9, 9))]
    assert planos_iw(pl, port)[0][1] is True
    assert planos_iw(pl, port[1:])[0][1] is False
    assert planos_iw(pl[:8], port) == []


# ---------------------------------------------------------------- autoprueba
def autoprueba():
    import random
    _auto_camara_iw()
    # 1) cuaternios
    def rz(deg):
        c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        return [c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    qa, qb = quat16(rz(0)), quat16(rz(30))
    assert abs(angulo(qa, qb) - 30) < 1e-6, angulo(qa, qb)
    assert abs(angulo(qa, slerp(qa, qb, 0.5)) - 15) < 1e-6
    for d in (0, 90, 179, 200, 300):
        assert abs(angulo(quat16(rz(0)), quat16(rz(d))) - min(d, 360 - d)) < 1e-6, d
    # 2) el falso error de cuantizacion: PAL cada 1,2, port cada 1,0, brazo rapido (20 grados/cuadro)
    ref = [fila(i * 1.2, 'hueso', 'a0/BRAZO', quat16(rz(20 * i * 1.2))) for i in range(0, 60)]
    ref += [fila(i * 1.2, 'clip', 'a0', 7) for i in range(0, 60)]
    test = [fila(t, 'hueso', 'a0/BRAZO', quat16(rz(20 * t))) for t in range(1, 60)]
    sin_interp = comparar(ref, test, interp=False)
    con_interp = comparar(ref, test, interp=True)
    assert sin_interp['reales'] >= 1 and sin_interp['resumen']['a0']['buenas'] < sin_interp['resumen']['a0']['muestras'], 'el mas-cercano deberia dar errores falsos'
    assert con_interp['reales'] == 0 and con_interp['resumen']['a0']['buenas'] == con_interp['resumen']['a0']['muestras'], con_interp
    print('  cuantizacion PAL 1,2 vs port 1,0: mas-cercano %d brechas, slerp %d' % (sin_interp['reales'], con_interp['reales']))
    # 3) no se mezcla al cambiar el clip: salto de 90 grados entre dos muestras PAL con clip distinto
    ref = [fila(0, 'hueso', 'a0/X', quat16(rz(0))), fila(1.2, 'hueso', 'a0/X', quat16(rz(90))), fila(2.4, 'hueso', 'a0/X', quat16(rz(90))),
           fila(0, 'clip', 'a0', 1), fila(1.2, 'clip', 'a0', 2), fila(2.4, 'clip', 'a0', 2)]
    test = [fila(1.2, 'hueso', 'a0/X', quat16(rz(90)))]
    assert comparar(ref, test)['reales'] == 0
    # 4) eventos: un ancla con deriva, un evento que falta y una decision distinta con su consecuencia
    ev = lambda lst: [fila(t, 'estado', 'n', v) for t, v in lst]
    base = ev([(0, 'S0'), (10, 'S1'), (20, 'S2'), (30, 'S3'), (40, 'S3')])
    der = ev([(0, 'S0'), (10, 'S1'), (23, 'S2'), (30, 'S3')])
    r = comparar(base, der)
    assert r['reales'] == 1 and r['brechas'][0]['tipo'] == 'tiempo', r['brechas']
    dec_o = [fila(5, 'decision', 'tarea', [1, 0, 136.0]), fila(50, 'decision', 'tarea', [3, 0, 80.0])] + ev([(0, 'S0'), (60, 'S0'), (100, 'S1'), (101, 'S2')])
    dec_p = [fila(5, 'decision', 'tarea', [1, 0, 136.0]), fila(50, 'decision', 'tarea', [2, 0, 80.0])] + ev([(0, 'S0'), (60, 'S0'), (100, 'S1'), (130, 'S2')])
    r = comparar(dec_o, dec_p)
    assert r['reales'] == 1 and r['brechas'][0]['tipo'] == 'decision' and r['consecuencias'] >= 1, r['brechas']
    assert comparar(dec_o, dec_o)['reales'] == 0
    print('  eventos/anclas/consecuencias OK')
    # 5) datos reales (si existen en este PC)
    orig = Path('E:/DBZ3HD_trabajo/tmp/solIWX/original_cuadros_1793899772650782500/7D1')
    if (orig / 'cuadros.jsonl').exists():
        ref = bubbles_original(orig)
        r = comparar(ref, bubbles_port_receta(orig), canales={'decision'}, solape=(0, max(x['t'] for x in ref)))
        nd = sum(1 for x in ref if x['canal'] == 'decision')
        assert nd == 4 and r['reales'] == 0 and r['eventos']['anclas'] == 4, (nd, r)
        print('  Bubbles 7D1: %d/4 decisiones IGUAL (original contra la receta del port)' % r['eventos']['anclas'])
    else:
        print('  (sin captura Bubbles en este PC: se omite)')
    pal = Path('E:/DBZ3HD_trabajo/tmp/solB2X/original_g2_e0_20261105_204406/traza.json')
    hd = Path('D:/DBZ3HD/_pruebas/20261106-043914_solB2X_lanzamientos_grupo2')
    if pal.exists() and (hd / 'traza_hd.jsonl').exists():
        a = escena_b2(pal, hd, 'E:/DBZ3HD_trabajo/tmp/fase0C/kami_interp', interp=True, nombre='kami (slerp)')
        n = escena_b2(pal, hd, 'E:/DBZ3HD_trabajo/tmp/fase0C/kami_cercana', interp=False, nombre='kami (mas cercana, metodo viejo)')
        for k, v in a['resumen'].items():
            print('  Kami %s: slerp %d/%d (error max %.1f) | mas-cercana %d/%d (error max %.1f)' % (
                k, v['buenas'], v['muestras'], v['error_max'], n['resumen'][k]['buenas'], n['resumen'][k]['muestras'], n['resumen'][k]['error_max']))
        sa = a['resumen']['a0']
        sn = n['resumen']['a0']
        assert sa['buenas'] >= 245 and sa['buenas'] > sn['buenas'], (sa, sn)
        falsos = [x for x in a['brechas'] if x['tipo'] == 'pose' and x['t_ini'] > 3 and 'error medio' in x['detalle']
                  and float(x['detalle'].split('error medio ')[1].split()[0]) >= 10]
        assert not falsos, falsos
    else:
        print('  (sin trazas Kami en este PC: se omite)')
    print('AUTOPRUEBA OK')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--autoprueba', action='store_true')
    sub = ap.add_subparsers(dest='cmd')
    p = sub.add_parser('bubbles')
    p.add_argument('--orig', required=True)
    p.add_argument('--csv')
    p.add_argument('--out', default='E:/DBZ3HD_trabajo/tmp/dif/bubbles')
    p = sub.add_parser('b2')
    p.add_argument('--pal', required=True)
    p.add_argument('--hd', required=True)
    p.add_argument('--desfase', type=float, default=MB2.DESFASE_HD)
    p.add_argument('--out', default='E:/DBZ3HD_trabajo/tmp/dif/b2')
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    if a.cmd == 'bubbles':
        escena_bubbles(a.orig, a.csv, a.out)
    elif a.cmd == 'b2':
        escena_b2(a.pal, a.hd, a.out, a.desfase)
    else:
        ap.print_help()


if __name__ == '__main__':
    main()

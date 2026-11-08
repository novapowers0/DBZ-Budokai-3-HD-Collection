# SPDX-License-Identifier: MIT
"""Fase 0E: ejecuta la logica ORIGINAL de Bubbles (IW EU) desde el ELF del jugador con ee.py.

Entradas (solo lectura, nada se copia al repo):
  --elf  SLES_553.47 extraido de la ISO EU del jugador
  --p2s  savestate de PCSX2 de la captura con sonda (eeMemory.bin + Scratchpad.bin)
  --cap  cuadros.jsonl de esa captura (filas `bubbles_ia` de tools/iwx_bubbles_ia.py)
  --cap4 cuadros.jsonl de la captura de las 4 decisiones (opusBB)
  --ghidra C exportada de Ghidra (solo nombres de funciones para el censo)
Uso: python tools/iw_mips/bubbles.py [comprobar|todo|memory|ia|estados|tareas|velocidad]
     python tools/iw_mips/bubbles.py juego --log <dbz3.log>   (llamadas del juego C++ reejecutadas en ee.py)
"""
import argparse
import importlib.util
import json
import math
import re
import struct
import sys
import time
import zipfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import ee  # noqa: E402

ELF = Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47')
CAP = Path('E:/DBZ3HD_trabajo/iw_mips/solIWX/original_cuadros_1793934776767488700')
P2S = CAP / 'respaldos/captura_estados/1793934913425042200_SLES-55347 (335A5A1F).96.p2s'  # inicio.png, n 17647
CAP4 = Path('E:/DBZ3HD_trabajo/iw_mips/solIWX/original_cuadros_1793899772650782500/7D1/cuadros.jsonl')
GHIDRA = Path('E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c')
DECISIONES = Path('E:/DBZ3HD_trabajo/iw_mips/opusBB/decisiones.py')

# EU SLES_553.47 (medido: decomp + sonda eIW6)
GP = 0x4A3DF0             # crt0: gp; semilla del LFSR en gp-0x7FE8 = 0x49BE08
SEMILLA = GP - 0x7FE8
MGR = GP - 0x7380         # 0x49CA70 -> gestor del minijuego (jugador en +0x800, Bubbles en +0x28D0)
MEMORY = 0x127CA0
IA = 0x31A940             # s_len_3_2f (IA de Bubbles, 1 llamada/cuadro desde Char_ComputePoseStateFromInput)
CREAR = 0x3172E0          # s_BBR_POST_EXEC (crea la tarea)
OBJ = 0x318400            # LoadWaitMinigameObjectsFlag (tabla work, 0x260 por actor)
FIN_TEXTO = 0x37E000      # ultima funcion de minijuegos ~0x37D360; el resto del segmento son datos vivos

# Puentes: lo que NO decide la IA (sonido, boca, printf). Todo lo demas se ejecuta del ELF.
PRINTF, VOZ, BOCA, ICONO = 0x127DE0, 0x22FD20, 0x207080, 0x317B70


def leer_simbolos(p=GHIDRA):
    nombres = {}
    if p.exists():
        for m in re.finditer(rb'^// ==== ([0-9a-f]{8}) (\S+)', p.read_bytes(), re.M):
            nombres[int(m.group(1), 16)] = m.group(2).decode()
    return nombres


def cargar(elf=ELF, p2s=P2S):
    """RAM de la instantanea + codigo del ELF del jugador encima (0x100000..FIN_TEXTO)."""
    mem = bytearray(ee.TOTAL)
    with zipfile.ZipFile(p2s) as z:
        mem[:ee.RAM] = z.read('eeMemory.bin')
        mem[ee.SPR:ee.SPR + 0x4000] = z.read('Scratchpad.bin')
    d = elf.read_bytes()
    assert d[:4] == b'\x7fELF'
    phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
    texto = None
    for i in range(phnum):
        tipo, off, va, _, fsz, msz, fl, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
        if tipo == 1 and fsz:
            texto = (off, va, fsz)
    off, va, fsz = texto
    n = FIN_TEXTO - va
    elf_txt = d[off:off + n]
    dif = [va + i for i in range(0, n, 4) if elf_txt[i:i + 4] != mem[va + i:va + i + 4]]
    mem[va:va + n] = elf_txt
    return mem, dif


def lfsr_python():
    spec = importlib.util.spec_from_file_location('decisiones', DECISIONES)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def nueva_cpu(mem, nombres):
    cpu = ee.Ee(mem, GP, entradas=nombres.keys(), nombres=nombres)
    cpu.voces, cpu.bocas, cpu.iconos = [], [], []

    def printf(c): c.r[2] = 0
    def voz(c): c.voces.append(c.r[5] & 0xFFFF)
    def boca(c): c.bocas.append(c.r[5] & 0xFFFF)
    cpu.hooks.update({PRINTF: printf, VOZ: voz, BOCA: boca})
    return cpu


# ---------------------------------------------------------------- 1. Memory
def prueba_memory(mem, nombres, pasos=10_000):
    dec = lfsr_python()
    cpu = nueva_cpu(mem, nombres)
    cpu.whitelist = {MEMORY}
    cpu.estricto = True
    st = cpu.r32(SEMILLA)
    r = dec.R(st)
    t = time.perf_counter()
    n0 = cpu.n
    total = 0
    for k in range(pasos):  # 10.000 llamadas Memory(1) = 10.000 pasos del LFSR
        v = cpu.call(MEMORY, 1) & ee.M32
        assert v == r.mem(1) and cpu.r32(SEMILLA) == r.st, k
        total += 1
    for k, n in enumerate([32, 255, 3, 100, 1000] * 200):  # y 1.000 llamadas con n variado
        v = cpu.call(MEMORY, n) & ee.M32
        assert v == r.mem(n) and cpu.r32(SEMILLA) == r.st, (k, n)
        total += n
    dt = time.perf_counter() - t
    return dict(llamadas=pasos + 1000, pasos_lfsr=total, instrucciones=cpu.n - n0, s=round(dt, 3),
                ips=int((cpu.n - n0) / dt))


# ---------------------------------------------------------------- 2. IA (sonda)
def filas_ia(cap):
    prev = None
    for l in open(cap, encoding='utf-8'):
        f = json.loads(l)
        s = f.get('bubbles_ia')
        if not s:
            continue
        nueva = prev is None or s['cuenta'] != prev['cuenta']
        prev = s
        if nueva and s['cuenta']:
            yield f, s


def f2b(x): return struct.unpack('<I', struct.pack('<f', x))[0]


def paso(s): return ((s << 1) | (((s >> 31) ^ (s >> 4)) & 1)) & ee.M32  # un paso del LFSR (= decisiones.py)


class Escena:
    """Instantanea + punteros del minijuego medidos en la propia RAM."""
    def __init__(s, mem, nombres):
        s.base = bytes(mem)
        s.mem = mem
        s.nombres = nombres
        c = nueva_cpu(mem, nombres)
        s.mgr = c.r32(MGR)
        s.bub = s.mgr + 0x28D0
        s.jug = s.mgr + 0x800
        s.objs = c.call(OBJ) & ee.M32
        s.idx = c.r32(s.bub + 0x1380)
        s.work = s.objs + s.idx * 0x260
        assert c.r32(s.bub + 0x1384) == s.jug, 'Bubbles+0x1384 no apunta al jugador'
        s.mem[:] = s.base

    def preparar(s, e, gates=0):
        """Copia la instantanea limpia e inyecta las entradas que la sonda guardo antes de la llamada."""
        s.mem[:] = s.base
        c = s.cpu = getattr(s, 'cpu', None) or nueva_cpu(s.mem, s.nombres)  # misma CPU: cache de decodificacion
        c.voces.clear(); c.bocas.clear(); c.iconos.clear(); c.llamadas.clear()
        c.hooks.pop(ICONO, None)
        c.w32(SEMILLA, e['seed_in'])
        c.w16(s.bub + 0xCBC, e['estado'])
        c.w16(s.mgr + 0x14BC, e['jugador'])
        c.w32(s.work + 0xB8, e['tarea_in'])
        for k, off in zip(('bx', 'by', 'bz'), (0x640, 0x644, 0x648)):
            c.w32(s.bub + off, f2b(e[k]))
        for k, off in zip(('px', 'py', 'pz'), (0xE40, 0xE44, 0xE48)):
            c.w32(s.mgr + off, f2b(e[k]))
        c.w32(s.work + 0x50, f2b(e['dx']))
        c.w32(s.work + 0x58, f2b(e['dz']))
        for off in (0x200, 0x210, 0x214, 0x218):
            c.w32(s.work + off, 0)
        if gates is not None:
            c.w32(s.objs + 0x248, gates)
            c.w32(s.objs + 0x180, gates)
        return c

    def tarea(s, c):
        t = c.r32(s.work + 0xB8)
        if not t:
            return None
        return dict(p=t, modo=c.r16(t + 0x60), fase=c.r16(t + 0x62), duracion=c.rf(t + 0x54),
                    v40=[c.rf(t + 0x40), c.rf(t + 0x44), c.rf(t + 0x48)])


def prueba_ia(mem, nombres, cap, icono=True, censo=None):
    """Llama a la IA original con las entradas de cada llamada de la sonda y compara semilla y flags."""
    esc = Escena(mem, nombres)
    tot = Counter()
    fallos, tareas = [], []
    filas = {}
    for l in open(cap, encoding='utf-8'):
        f = json.loads(l)
        filas[f['n']] = f
    t0 = time.perf_counter()
    ninstr = 0
    for f, e in filas_ia(cap):
        previo = e['jugador'] == 0  # antes del READY: la instantanea (n 17647) es de esa fase -> sus +0x248/+0x180
        c = esc.preparar(e, gates=None if previo else 0)
        if not icono:
            c.hooks[ICONO] = lambda c: c.iconos.append(1)
        n0 = c.n
        c.call(IA, esc.bub)
        ninstr += c.n - n0
        if censo is not None:
            censo.update(c.llamadas)
        seed = c.r32(SEMILLA)
        fl = {k: c.r32(esc.work + off) for k, off in
              (('f200', 0x200), ('f210', 0x210), ('f214', 0x214), ('f218', 0x218))}
        orig_fl = {k: 1 if e[k] else 0 for k in fl}
        mio_fl = {k: 1 if v else 0 for k, v in fl.items()}
        tot['llamadas'] += 1
        tot['antes_ready' if previo else 'en_juego'] += 1
        ok_s = seed == e['seed_out']
        ok_f = mio_fl == orig_fl
        tot[f'semilla_ok={ok_s}'] += 1
        tot[f'flags_ok={ok_f}'] += 1
        for k in fl:
            tot['puesto_' + k] += mio_fl[k]
            tot['orig_' + k] += orig_fl[k]
        if c.voces:
            tot['voces'] += len(c.voces)
        t = esc.tarea(c)
        if e['tarea_in'] == 0 and t:
            o = filas[f['n']]['bubbles'].get('tarea')  # la fila n se lee al acabar el cuadro n
            exacta = bool(o) and (o['modo'], o['fase'], o['duracion']) == (t['modo'], t['fase'], t['duracion'])
            ulps = [abs(f2b(a) - f2b(b)) for a, b in zip(t['v40'], o['v40'])] if o else None
            tareas.append(dict(n=f['n'], mio=t, original=o, voces=list(c.voces), decision_exacta=exacta,
                               v40_ulps=ulps))
        if not (ok_s and ok_f):
            pasos, st = None, e['seed_in']
            for k in range(4000):
                if st == e['seed_out']:
                    pasos = k
                    break
                st = paso(st)
            fallos.append(dict(n=f['n'], estado=e['estado'], jugador=e['jugador'], seed_mio=seed,
                               pasos_original=pasos, flags_mio=mio_fl, flags_orig=orig_fl, iconos=len(c.iconos)))
    dt = time.perf_counter() - t0
    esc.mem[:] = esc.base  # deja la instantanea limpia para la siguiente prueba
    return dict(resumen=dict(tot), fallos=fallos, tareas=tareas, s=round(dt, 2), instrucciones=ninstr,
                ips=int(ninstr / dt) if dt else 0)


# ---------------------------------------------------------------- 2b. 4 decisiones (captura opusBB)
def prueba_tareas(mem, nombres, cap4=CAP4, dific=2):
    """Para cada tarea nueva de la captura de opusBB: busca el desfase del LFSR dentro del cuadro con el
    que s_BBR_POST_EXEC ORIGINAL da el mismo modo/fase/duracion, y mide el angulo de v40."""
    esc = Escena(mem, nombres)
    filas = {}
    for l in open(cap4, encoding='utf-8'):
        f = json.loads(l)
        filas[f['n']] = f
    ns = sorted(filas)
    out = []
    for n0, n in zip(ns, ns[1:]):
        t = filas[n]['bubbles'].get('tarea')
        t0 = filas[n0]['bubbles'].get('tarea')
        if not t or (t0 and t0['p'] == t['p']):  # misma tarea (avance de fase): no es una creacion
            continue
        b0 = filas[n0]['bubbles']
        s0, s1 = b0['seed'], filas[n]['bubbles']['seed']
        npc, jug = b0['npc'], b0['jugador']
        v = t['v40']
        lv = math.hypot(v[0], v[2])
        obj = (v[0] / lv, v[2] / lv)
        forzados = [0, 1] if t['modo'] == 1 else [0] if t['modo'] in (2, 3) else [t['modo']]
        st, hits = s0, []
        for o in range(4000):
            if st == s1:
                break
            for forz in forzados:
                c = esc.preparar(dict(seed_in=st, estado=npc['estado'], jugador=jug['estado'], tarea_in=0,
                                      bx=npc['pos'][0], by=npc['pos'][1], bz=npc['pos'][2],
                                      px=jug['pos'][0], py=jug['pos'][1], pz=jug['pos'][2],
                                      dx=0.0, dz=0.0, f200=0, f210=0, f214=0, f218=0), gates=None)
                c.call(CREAR, esc.bub, 1 if forz == 4 else 0, forz)
                m = esc.tarea(c)
                if (m['modo'], m['fase'], m['duracion']) == (t['modo'], t['fase'], ee.fv(f2b(t['duracion']))):
                    mv = m['v40']
                    lm = math.hypot(mv[0], mv[2]) or 1
                    ang = math.degrees(math.acos(max(-1, min(1, (mv[0] * obj[0] + mv[2] * obj[1]) / lm))))
                    hits.append(dict(o=o, forzado=forz, ang=round(ang, 2), v40=[round(x, 3) for x in mv],
                                     v40_orig=[round(x, 3) for x in v]))
            st = paso(st)
        hits.sort(key=lambda h: h['ang'])
        out.append(dict(n=n, modo=t['modo'], fase=t['fase'], duracion=t['duracion'], pasos_cuadro=o,
                        mejor=hits[:1], candidatos=len(hits), ok=bool(hits) and hits[0]['ang'] < 3))
    return out


# ---------------------------------------------------------------- 2c. maquina de estados (tabla 0x4995A0)
UPDATE, TICK, TRAYECTORIA, PASO_T = 0x31B260, 0x31B240, 0x31A570, 0x49C7DC


def prueba_estados(mem, nombres, cap, mover=False):
    """Cada cuadro n-1 -> n: inyecta el estado del actor de la fila n-1 (+ entradas de la IA del cuadro n si las hay)
    y ejecuta Char_PoseUpdateDispatchTable ORIGINAL; compara el estado resultante con la fila n.
    mover=True: ademas MgObj_UpdateTrajectory (callback de la tarea) + Char_PoseTickDispatch y mide la posicion."""
    esc = Escena(mem, nombres)
    filas = [json.loads(l) for l in open(cap, encoding='utf-8')]
    creada = {}
    for f in filas:
        ta = f['bubbles'].get('tarea')
        if ta and ta['p'] not in creada:
            creada[ta['p']] = f['n']
    tot, trans, trans_ok, difs, errores, voces = Counter(), Counter(), Counter(), [], [], []
    ult = None
    for a, b in zip(filas, filas[1:]):
        ia = a.get('bubbles_ia')
        ult = ia or ult
        if b['n'] != a['n'] + 1 or ult is None:
            continue
        npc, ib = a['bubbles']['npc'], b.get('bubbles_ia')
        nueva = ib and ib['cuenta'] != (ia or {}).get('cuenta')
        e = dict(ib) if nueva else dict(ult, seed_in=a['bubbles']['seed'], estado=npc['estado'],
                                        jugador=a['bubbles']['jugador']['estado'], tarea_in=npc['tareaB8'])
        c = esc.preparar(e, gates=None if e['jugador'] == 0 else 0)
        x, w = esc.bub, esc.work
        c.w16(x + 0xCBC, npc['estado']); c.w16(x + 0xCBE, npc['previo']); c.w16(x + 0xCC4, npc['cc4'])
        c.w32(x + 0xB60, npc['codigo']); c.w32(x + 0xC68, f2b(npc['cuadro'])); c.w32(x + 0xC6C, npc['duracion'])
        c.w32(x + 0xC74, npc['fin']); c.w32(x + 0x18E8, npc['flags18e8'])
        for i in range(3):
            c.w32(x + 0x40 + 4 * i, f2b(npc['pos'][i]))
        for v, off in zip(npc['flags'], (0x210, 0x214, 0x218)):
            c.w32(w + off, v)
        c.w32(w + 0x1F4, f2b(npc['stick'][0])); c.w32(w + 0x1F8, f2b(npc['stick'][1]))
        c.w32(w + 0x1A0, f2b(npc['reloj'])); c.w32(w + 0xB8, npc['tareaB8'])
        ta = a['bubbles'].get('tarea')
        n0 = c.n
        try:
            if ta and npc['tareaB8'] == ta['p']:  # la instantanea es de antes de crear la tarea: su contenido se inyecta
                p = ta['p']
                for i in range(3):
                    c.w32(p + 0x30 + 4 * i, f2b(ta['v30'][i])); c.w32(p + 0x40 + 4 * i, f2b(ta['v40'][i]))
                c.w32(p + 0x54, f2b(ta['duracion'])); c.w32(p + 0x58, ta['u58'])
                c.w16(p + 0x60, ta['modo']); c.w16(p + 0x62, ta['fase'])
                # ponytail: tiempo de la tarea estimado (cuadros desde que aparece x paso); la captura no lo guarda
                c.w32(p + 0x50, f2b(ta['tiempo'] if 'tiempo' in ta else (a['n'] - creada[p]) * c.rf(PASO_T)))  # sonda nueva: tiempo real
            c.call(UPDATE, x)  # orden medido: la IA crea la tarea en el update y la tarea mueve en el mismo cuadro
            if mover:
                p = c.r32(w + 0xB8)
                if p:
                    c.call(TRAYECTORIA, p)
                c.call(TICK, x)
        except (RuntimeError, NotImplementedError) as ex:
            errores.append((b['n'], str(ex)))
            continue
        if c.voces:
            voces.append((b['n'], list(c.voces)))
        tot['instrucciones'] += c.n - n0
        tot['instr_max'] = max(tot['instr_max'], c.n - n0)
        s0, s1 = npc['estado'], b['bubbles']['npc']['estado']
        mio = c.r16(x + 0xCBC)
        mio = mio - 0x10000 if mio & 0x8000 else mio
        tot['cuadros'] += 1
        tot['igual'] += mio == s1
        if s0 != s1:
            trans[f'{s0}->{s1}'] += 1
            trans_ok[f'{s0}->{s1}'] += mio == s1
        if mio != s1 and len(difs) < 20:
            difs.append(dict(n=b['n'], antes=s0, original=s1, mio=mio))
        if mover:
            p40 = [c.rf(x + 0x40 + 4 * i) for i in range(3)]
            err = max(abs(u - v) for u, v in zip(p40, b['bubbles']['npc']['pos']))
            tot['pos_err<1e-3'] += err < 1e-3
            tot['pos_err<0.05'] += err < 0.05
    esc.mem[:] = esc.base
    return dict(resumen=dict(tot), transiciones={k: f'{trans_ok[k]}/{v}' for k, v in trans.items()},
                diferencias=difs, errores=errores[:5], voces=voces)


# ---------------------------------------------------------------- 3. velocidad
def prueba_velocidad(mem, nombres, cap):
    """Solo el tiempo de c.call (sin copiar la instantanea); pasada 1 en frio (decodifica), pasada 2 en caliente."""
    esc = Escena(mem, nombres)
    filas = list(filas_ia(cap))
    out = {}
    for pasada in ('frio', 'caliente'):
        n_tot, t_tot, mx = 0, 0.0, 0
        for f, e in filas:
            c = esc.preparar(e, gates=None if e['jugador'] == 0 else 0)
            n0 = c.n
            t = time.perf_counter()
            c.call(IA, esc.bub)
            t_tot += time.perf_counter() - t
            n_tot += c.n - n0
            mx = max(mx, c.n - n0)
        out[pasada] = dict(llamadas=len(filas), instrucciones=n_tot, media=round(n_tot / len(filas)), maximo=mx,
                           ips=int(n_tot / t_tot), us_por_llamada=round(1e6 * t_tot / len(filas), 1))
    out['decodificadas'] = len(esc.cpu.code)
    esc.mem[:] = esc.base
    return out


# ---------------------------------------------------------------- 4. espejo del juego (= src/iw_mips.cpp)
# En el juego no hay instantanea: RAM = PT_LOAD del ELF + bss a cero + estos bloques propios + puentes.
EMGR, EOBJS, ECTX, ETAREAS, EACTIVA = 0x1000000, 0x1010000, 0x1012000, 0x1020000, 0x1030000
EBUB, EJUG, EWORK = EMGR + 0x28D0, EMGR + 0x800, EOBJS + 0x260


class Espejo:
    """Mismo montaje que src/iw_mips.cpp (bubbles::Llamada): IA 0x31A940 o tabla de updates 0x31B260."""
    BONES, RAIZ, PLATANO, HITBOX, CLIP = 0x238670, 0x2F5DA0, 0x3177F0, 0x2403F0, 0x1031000
    GETWORK, BORRAR, MGW, PASO_T = 0x296F20, 0x140460, 0x1032000, 0x49C7DC
    # tick (= iw_mips.cpp): colisiones y huesos por puente; globales gp-0x7238/-0x7230/-0x7000; rejilla propia
    SWEEP, RAY, CAMLOC, GRID, B84, COLCTX = 0x2E7640, 0x2E77D0, 0x208390, 0x1033000, 0x1034000, 0x1035000
    GUARDA = [EOBJS + 0x260 + o for o in (0xB4, 0x40, 0x44, 0x48, 0x4C, 0x60, 0x64, 0x68, 0x6C, 0x1CC)] + \
             [0x1032000 + 0x238 + 4 * i for i in range(10)]

    def __init__(s, elf=ELF):
        d = elf.read_bytes()
        s.base = bytearray(ee.TOTAL)
        phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
        for i in range(phnum):
            tipo, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
            if tipo == 1 and fsz:
                s.base[va:va + fsz] = d[off:off + fsz]
        s.mem = bytearray(s.base)
        c = s.cpu = ee.Ee(s.mem, GP)
        s.out, s.largos = None, {}

        def v0(v): return lambda c: c.r.__setitem__(2, v)

        def crear(c):
            s.out['tareas'].append(ETAREAS + 0x100 * len(s.out['tareas']))
            c.r[2] = s.out['tareas'][-1]

        def icono(c):
            st = c.r32(SEMILLA)
            for _ in range(62):  # efecto 0xB del icono: 7 x Memory(8) + 6 x Memory(1)
                st = paso(st)
            c.w32(SEMILLA, st); c.w32(EWORK + 0x228, 1); s.out['icono'] = 1

        def bones(c):  # BonesMovement -> AnimationsStatic(actor+0x640) con la duracion del clip del port
            x = c.r[4] & ee.M32
            cod = c.r32(x + 0xB60)
            L = int(s.largos.get(cod, 0))
            c.w32(x + 0xC68, 0); c.w32(x + 0xC6C, L); c.w32(x + 0xC70, f2b(float(L))); c.w32(x + 0xC74, 0)
            for off in (0xC88, 0xC90, 0xC9C, 0xCB0):
                c.w32(x + off, 0)
            c.w32(x + 0xAC4, 0x3F800000)
            s.out['ev'].append(('M', cod, cc4(c), 0.0))

        def cc4(c):
            v = c.r16(EBUB + 0xCC4)
            return v - 0x10000 if v & 0x8000 else v

        def observador(n, t):  # entra en el estado n: anota y ejecuta la funcion original
            def h(c):
                s.out['ev'].append(('E', n, cc4(c), 0.0))
                ra = c.r[31]; c.r[31] = ee.RETORNO; c.run(t, 10 ** 6); c.r[31] = ra
            return h

        c.hooks.update({0x29A5D0: v0(EOBJS), OBJ: v0(EOBJS), 0x28A9D0: v0(ECTX), 0x140300: crear, ICONO: icono,
                        VOZ: lambda c: s.out.__setitem__('voz', c.r[5] & 0xFFFF), BOCA: lambda c: None,
                        PRINTF: lambda c: None, s.HITBOX: lambda c: None, s.BONES: bones,
                        s.GETWORK: v0(s.MGW), s.BORRAR: lambda c: None,
                        s.RAIZ: lambda c: s.out['ev'].append(('R', 0, cc4(c), 0.0)),
                        s.PLATANO: lambda c: s.out['ev'].append(('B', c.r[5] & 0xFFFF, 0, ee.fv(c.f[12])))})
        for n in range(10):
            t = struct.unpack_from('<I', s.base, 0x499560 + 4 * n)[0]
            c.hooks[t] = observador(n, t)

        # Tick: los puentes reponen lo que dio el port en el juego (s.col, de la linea del registro) o, sin registro,
        # `s.colision(rayo, desde, hasta, mascara)` -> (hit, punto, normal, d, material, tipo) (None = sin choque).
        def choque(rayo):
            def h(c):
                a, b, pt, ct = (c.r[i] & ee.M32 for i in (5, 6, 7, 8))
                hasta = [c.r32(b + 4 * i) for i in range(3)]
                if s.col:
                    k, hit, punto, nrm, d, mat, tipo = s.col.pop(0)
                    assert k == ('Y' if rayo else 'S'), (k, rayo)
                else:
                    r = s.colision(rayo, [c.rf(a + 4 * i) for i in range(3)], [ee.fv(x) for x in hasta],
                                   c.r[10 if rayo else 9]) if s.colision else None
                    hit, punto, nrm, d, mat, tipo = (1, *[[f2b(x) for x in v] for v in r[1:3]], f2b(r[3]), r[4], r[5]) \
                        if r and r[0] else (0, hasta, [0] * 3, 0, 0, 0)
                if not hit:
                    punto = hasta
                for i in range(3):
                    c.w32(pt + 4 * i, punto[i])
                c.w32(pt + 12, c.r32(b + 12)); c.w32(ct + 0x10, 1 if hit else 0)
                if hit:
                    for i in range(3):
                        c.w32(ct + 4 * i, nrm[i])
                    c.w32(ct + 0xC, d); c.w16(ct + 0x20, mat); c.w16(ct + 0x22, tipo)
                c.r[2] = 1 if hit else 0
            return h

        def camloc(c):  # hueso 0 = raiz + desplazamiento de la pose girado por el frente (en el juego lo calcula el C++)
            x = (c.r[4] & ee.M32) - 0x640
            if s.col and s.col[0][0] == 'C':
                a = s.col.pop(0)[1]
            else:
                fx, fz = c.rf(EWORK + 0x50), c.rf(EWORK + 0x58)
                yaw = math.atan2(fx, fz); co, si = math.cos(yaw), math.sin(yaw); l = s.l50
                a = [f2b(c.rf(x + 0x40) + co * l[0] + si * l[2]), f2b(c.rf(x + 0x44) + l[1]), f2b(c.rf(x + 0x48) - si * l[0] + co * l[2])]
            for i in range(3):
                c.w32(x + 0x640 + 4 * i, a[i])
            c.w32(x + 0x64C, 0x3F800000)
        c.hooks.update({s.SWEEP: choque(False), s.RAY: choque(True), s.CAMLOC: camloc,
                        s.BORRAR: lambda c: s.out.__setitem__('tarea5', 1)})
        s.col, s.colision, s.l50 = [], None, [0.0] * 3

    def ia(s, e, que='ia'):
        """e: dict con las mismas entradas que escribe iw_mips.cpp. Devuelve semilla, flags, tareas, voz, icono, estado."""
        s.mem[:] = s.base
        c, w32, wf = s.cpu, s.cpu.w32, lambda a, x: s.cpu.w32(a, f2b(x))
        s.out = dict(tareas=[], voz=-1, icono=0, ev=[], tarea5=0)
        X, J, W = EBUB, EJUG, EWORK
        w32(MGR, EMGR); wf(EMGR + 0x8B80, ee.fv(0x3F99999A)); w32(X + 0x1380, 1); w32(X + 0x1384, J)
        wf(X + 0xCAC, 1.0); c.w16(ECTX + 0xC, e['dif'])
        w32(SEMILLA, e['seed_in']); c.w16(X + 0xCBC, e['estado'] & 0xFFFF); c.w16(X + 0xCBE, e['anterior'] & 0xFFFF)
        c.w16(X + 0xCC4, e.get('cc4', 0) & 0xFFFF); c.w16(J + 0xCBC, e['jugador'] & 0xFFFF)
        p = e.get('p', [0.0, 0.0, 0.0])
        for i in range(3):
            wf(X + 0x640 + 4 * i, e['bub'][i]); wf(J + 0x640 + 4 * i, e['jug'][i])
            wf(X + 0x40 + 4 * i, p[i]); wf(J + 0x40 + 4 * i, e['jug'][i])
        for a in (X + 0x64C, J + 0x64C, X + 0x4C, J + 0x4C):
            wf(a, 1.0)
        w32(X + 0xBC0, X); w32(X + 0xB64, s.CLIP); wf(X + 0xAC4, 1.0); wf(X + 0x18D8, 1.0)
        w32(X + 0xC84, ee.M32); w32(X + 0xC8C, ee.M32)
        w32(X + 0xB60, e.get('codigo', 0xD2)); wf(X + 0xC68, e.get('cuadro', 0.0)); w32(X + 0xC6C, e.get('dur', 0))
        w32(X + 0xC74, e.get('fin', 0)); w32(X + 0x18E8, e.get('f18e8', 0))
        w32(W + 0xB8, EACTIVA if e['tarea'] else 0)
        wf(W + 0x50, e['frente'][0]); wf(W + 0x58, e['frente'][1])
        wf(W + 0x1F4, e['stick'][0]); wf(W + 0x1F8, e['stick'][1]); wf(W + 0x1A0, e.get('reloj', 0.0))
        w32(W + 0x218, e.get('f218in', 0)); w32(W + 0x224, e.get('latch', 0))
        for off, k in ((0x200, 'f200in'), (0x210, 'f210in'), (0x214, 'f214in')):   # N2: solo si la sonda los trae
            if k in e:
                w32(W + off, e[k])
        w32(EOBJS + 0x248, e['gates'][0]); w32(EOBJS + 0x180, e['gates'][1]); w32(W + 0x228, e['icono_in'])
        wf(s.PASO_T, ee.fv(0x3F99999A))
        tm = e.get('tm', (0, 0, 0.0, 0.0, [0.0] * 3, [0.0] * 3))
        for i in range(3):
            wf(EACTIVA + 0x30 + 4 * i, tm[5][i]); wf(EACTIVA + 0x40 + 4 * i, tm[4][i]); wf(s.MGW + 0x238 + 4 * i, e.get('n238', [0.0] * 3)[i])
        wf(EACTIVA + 0x50, tm[3]); wf(EACTIVA + 0x54, tm[2]); w32(EACTIVA + 0x58, 1)
        c.w16(EACTIVA + 0x60, tm[0] & 0xFFFF); c.w16(EACTIVA + 0x62, tm[1] & 0xFFFF)
        # tick (todas las llamadas, como el C++): rejilla, globales, B84, aire, +0x50, work+0x90, suelo, guarda
        G = s.GRID
        w32(0x49CDB8, G); w32(0x49CBB8, s.MGW); w32(0x49CBC0, EOBJS); w32(s.MGW + 0x1E0, s.COLCTX)
        w32(G + 0x630, 7); wf(G + 0xAD4, 1800.0); wf(G + 0xAD8, 1800.0); w32(G + 0xADC, 6); w32(G + 0xAE0, 6)
        wf(G + 0xAF0, -750.0); wf(G + 0xAF8, -750.0)
        w32(X + 0xB84, s.B84); wf(s.B84, e.get('b84', 0.0)); w32(X + 0x2060, 0x100 if e.get('aire') else 0)
        wf(W + 0x38, 1.0); wf(W + 0x3C, 1.0); wf(X + 0x18D0, e.get('suelo_in', 0.0))
        for i in range(3):
            wf(X + 0x50 + 4 * i, e.get('pprev', [0.0] * 3)[i]); wf(W + 0x90 + 4 * i, e.get('a90in', [0.0] * 3)[i])
        wf(X + 0x5C, 1.0); wf(W + 0x9C, 1.0)
        for a, v in zip(s.GUARDA, e.get('gin') or []):
            w32(a, v)
        s.l50 = e.get('l50', [0.0] * 3)
        w50, w58 = c.r32(W + 0x50), c.r32(W + 0x58)
        if que == 'tick':
            c.call(TICK, X)
        elif que == 'tray':
            c.call(TRAYECTORIA, EACTIVA)
        elif que == 'tarea5':
            c.call(CREAR, X, 1, 5)
        else:
            c.call(IA if que == 'ia' else UPDATE, X)
        o = s.out
        o['seed_out'] = c.r32(SEMILLA)
        o['flags'] = ''.join('1' if c.r32(W + off) else '0' for off in (0x200, 0x210, 0x214, 0x218))
        o['tareas'] = [(c.r16(t + 0x60), c.r16(t + 0x62), f2b(c.rf(t + 0x54))) for t in o['tareas']]
        s16 = lambda v: v - 0x10000 if v & 0x8000 else v
        o['sale'] = (s16(c.r16(X + 0xCBC)), s16(c.r16(X + 0xCBE)), c.r32(X + 0xB60), c.r32(X + 0xC68),
                     int(c.r32(X + 0xC74) != 0), s16(c.r16(X + 0xCC4)), c.r32(W + 0x1A0), int(c.r32(W + 0x224) != 0))
        o['frente_cambia'] = (c.r32(W + 0x50), c.r32(W + 0x58)) != (w50, w58)
        o['pout'] = [c.r32(X + 0x40 + 4 * i) for i in range(3)]
        o['tout'] = (c.r16(EACTIVA + 0x62), c.r32(EACTIVA + 0x50), [c.r32(EACTIVA + 0x40 + 4 * i) for i in range(3)],
                     int(c.r32(W + 0xB8) == 0))
        o['tick'] = ([c.r32(W + 0x90 + 4 * i) for i in range(3)], c.r32(X + 0x18D0), c.r32(X + 0x184C),
                     c.r32(s.MGW + 0x1F4), c.r32(s.MGW + 0x1EC), o.get('tarea5', 0), c.r32(W + 0x50), c.r32(W + 0x58))
        o['gout'] = [c.r32(a) for a in s.GUARDA]
        return o


def prueba_espejo(cap):
    """El espejo del juego frente a la sonda (las 350 llamadas): mismo resultado que con la instantanea."""
    esp, tot = Espejo(), Counter()
    for f, e in filas_ia(cap):
        o = esp.ia(dict(seed_in=e['seed_in'], estado=e['estado'], anterior=0, jugador=e['jugador'], dif=2,
                        tarea=bool(e['tarea_in']), bub=[e['bx'], e['by'], e['bz']], jug=[e['px'], e['py'], e['pz']],
                        frente=[e['dx'], e['dz']], stick=[0.0, 0.0], gates=[0, 1 if e['jugador'] == 0 else 0], icono_in=0))
        tot['llamadas'] += 1
        tot['iguales'] += o['seed_out'] == e['seed_out'] and o['flags'] == ''.join(
            '1' if e[k] else '0' for k in ('f200', 'f210', 'f214', 'f218'))
    return dict(tot)


def prueba_espejo_estados(cap):
    """Tabla de updates ORIGINAL con el espejo del juego, cuadro a cuadro frente a la sonda (como prueba_estados).
    Semantica del port: f210/f214 a 0 al empezar la pasada, f218 se conserva."""
    esp, tot = Espejo(), Counter()
    filas = [json.loads(l) for l in open(cap, encoding='utf-8')]
    for f in filas:
        n = f['bubbles']['npc']
        esp.largos.setdefault(n['codigo'], n['duracion'])
    ult = None
    for a, b in zip(filas, filas[1:]):
        ia = a.get('bubbles_ia'); ult = ia or ult
        if b['n'] != a['n'] + 1 or ult is None:
            continue
        npc, ib = a['bubbles']['npc'], b.get('bubbles_ia')
        nueva = ib and ib['cuenta'] != (ia or {}).get('cuenta')
        e = dict(ib) if nueva else dict(ult, seed_in=a['bubbles']['seed'], jugador=a['bubbles']['jugador']['estado'])
        o = esp.ia(dict(seed_in=e['seed_in'], estado=npc['estado'], anterior=npc['previo'], jugador=e['jugador'], dif=2,
                        tarea=bool(npc['tareaB8']), bub=[e['bx'], e['by'], e['bz']], jug=[e['px'], e['py'], e['pz']],
                        frente=[e['dx'], e['dz']], stick=npc['stick'], gates=[0, 1 if e['jugador'] == 0 else 0],
                        icono_in=0, p=npc['pos'], cc4=npc['cc4'], codigo=npc['codigo'], cuadro=npc['cuadro'],
                        dur=npc['duracion'], fin=npc['fin'], f18e8=npc['flags18e8'], reloj=npc['reloj'],
                        f218in=npc['flags'][2], latch=0), que='upd')
        nb = b['bubbles']['npc']
        est, _, cod, cua, fin, c4, _, _ = o['sale']
        tot['cuadros'] += 1; tot['estado'] += est == nb['estado']; tot['codigo'] += cod == nb['codigo']
        tot['cuadro'] += abs(ee.fv(cua) - nb['cuadro']) < 1e-4; tot['fin'] += fin == nb['fin']
        tot['cc4'] += c4 == nb['cc4']; tot['transiciones'] += npc['estado'] != nb['estado']
    return dict(tot)


def prueba_espejo_tray(caps):
    """MgObj_UpdateTrajectory ORIGINAL con el espejo, por cuadro frente a las capturas: movimiento xz, fase, velocidad y fin.
    Con capturas viejas el tiempo de la tarea (+0x50) se estima; con la sonda nueva (tarea entera) es el real."""
    esp = Espejo()
    tot = Counter()
    for cap in caps:
        filas = [json.loads(l) for l in open(cap, encoding='utf-8')]
        creada = {}
        for f in filas:
            ta = f['bubbles'].get('tarea')
            if ta and ta['p'] not in creada: creada[ta['p']] = f['n']
        for a, b in zip(filas, filas[1:]):
            ta, tb = a['bubbles'].get('tarea'), b['bubbles'].get('tarea')
            npc, nb = a['bubbles']['npc'], b['bubbles']['npc']
            if b['n'] != a['n'] + 1 or not ta or npc['tareaB8'] != ta['p']: continue
            tiempo = ta.get('tiempo', (a['n'] - creada[ta['p']]) * 1.2)
            jug = a['bubbles']['jugador']
            o = esp.ia(dict(seed_in=a['bubbles']['seed'], estado=npc['estado'], anterior=npc['previo'], jugador=jug['estado'], dif=2,
                            tarea=True, bub=npc['ancla'][:3], jug=jug.get('ancla', jug['pos'])[:3], frente=[0.0, 1.0], stick=npc['stick'],
                            gates=[0, 0], icono_in=0, p=npc['pos'], tm=(ta['modo'], ta['fase'], ta['duracion'], tiempo, ta['v40'], ta['v30'])), 'tray')
            d = [ee.fv(o['pout'][i]) - npc['pos'][i] for i in (0, 2)]
            dc = [nb['pos'][i] - npc['pos'][i] for i in (0, 2)]
            err = max(abs(x - y) for x, y in zip(d, dc))
            tot[f'modo{ta["modo"]}'] += 1
            tot[f'modo{ta["modo"]}_mov<1e-3'] += err < 1e-3
            if tb and tb['p'] == ta['p']:
                tot['fase_ok'] += o['tout'][0] == tb['fase']; tot['fase_n'] += 1
                ev = max(abs(ee.fv(o['tout'][2][i]) - tb['v40'][i]) for i in range(3))
                tot['vel<1e-4'] += ev < 1e-4
            tot['acaba'] += o['tout'][3]; tot['acaba_cap'] += (not tb) or tb['p'] != ta['p']
    return dict(tot)


def colision_ram(nombres):
    """Puentes de colision del espejo respondidos por el codigo ORIGINAL (Collision_MoveSphere/Raycast) sobre la RAM de
    la captura, que tiene el WCF del campo cargado. Devuelve f(rayo, desde, hasta, mascara) -> (hit, punto, normal, d, mat, tipo)."""
    mem, _ = cargar()
    c = nueva_cpu(mem, nombres)
    ctx = c.r32(c.r32(0x49CBB8) + 0x1E0)
    A = ee.PILA + 0x10000

    def f(rayo, desde, hasta, mascara):
        for i in range(3):
            c.w32(A + 4 * i, f2b(desde[i])); c.w32(A + 0x10 + 4 * i, f2b(hasta[i]))
        c.w32(A + 0xC, 0x3F800000); c.w32(A + 0x1C, 0x3F800000)
        for i in range(0x30 // 4):
            c.w32(A + 0x40 + 4 * i, 0)
        if rayo:
            hit = c.call(0x2E77D0, ctx, A, A + 0x10, A + 0x20, A + 0x40, A + 0x80, mascara)
        else:
            hit = c.call(0x2E7640, ctx, A, A + 0x10, A + 0x20, A + 0x40, mascara, fargs=(0.5,))
        return (int(hit != 0), [c.rf(A + 0x20 + 4 * i) for i in range(3)], [c.rf(A + 0x40 + 4 * i) for i in range(3)],
                c.rf(A + 0x4C), c.r16(A + 0x60), c.r16(A + 0x62))
    return f


def prueba_espejo_tick(cap, nombres, paso_filas=1, solo_tarea=False):
    """Tabla de ticks ORIGINAL con el espejo del juego (+ guarda) frente a la misma tick con la RAM entera de la captura,
    con las mismas entradas (fila n-1) y las mismas colisiones originales. Debe salir igual palabra a palabra: prueba que
    el espejo y la guarda bastan. Despues, contra la fila n de la captura (sin tarea ni cambio de estado): suelo, work+0x90
    y tipo de contacto (lo que la captura guarda)."""
    esp, col = Espejo(), colision_ram(nombres)
    esp.colision = col
    mem, _ = cargar()
    esc = Escena(mem, nombres)
    filas = [json.loads(l) for l in open(cap, encoding='utf-8')]
    tot, difs = Counter(), []
    ult = None
    for a, b in list(zip(filas, filas[1:]))[::paso_filas]:
        npc, nb = a['bubbles']['npc'], b['bubbles']['npc']
        jug = a['bubbles']['jugador']
        if not a.get('bubbles_ia') and ult is None:  # captura sin sonda de la IA (opusBB): jugador de la fila, frente +z
            ult = dict(zip(('px', 'py', 'pz'), jug.get('ancla', jug['pos'])), dx=0.0, dz=1.0)
        ult = a.get('bubbles_ia') or ult
        if b['n'] != a['n'] + 1 or (solo_tarea and not npc['tareaB8']):
            continue
        x, w = esc.bub, esc.work
        tarea = bool(npc['tareaB8'])  # con tarea: si el barrido choca (tipo 2/5) se crea la tarea 5 en la tick
        c = esc.preparar(dict(ult, seed_in=a['bubbles']['seed'], estado=npc['estado'], jugador=jug['estado'], tarea_in=int(tarea),
                              bx=npc['ancla'][0], by=npc['ancla'][1], bz=npc['ancla'][2]), gates=None)
        borrada = []
        c.hooks[Espejo.BORRAR] = lambda cc: borrada.append(1)  # READ_BattleMain: B8 es ficticio (1) en esta prueba
        mgw = c.r32(0x49CBB8)
        # con tarea, la trayectoria (52FF) mueve la raiz antes de la tick: p += v40 (sin los giros de los modos 2/3)
        ta = a['bubbles'].get('tarea')
        pin = [u + v for u, v in zip(npc['pos'], ta['v40'])] if tarea and ta else list(npc['pos'])
        for i in range(3):
            c.w32(x + 0x40 + 4 * i, f2b(pin[i])); c.w32(x + 0x50 + 4 * i, f2b(npc['pos'][i]))
            c.w32(w + 0x90 + 4 * i, f2b(npc['work90'][i]))
        c.w32(w + 0x1F4, f2b(npc['stick'][0])); c.w32(w + 0x1F8, f2b(npc['stick'][1])); c.w32(x + 0x18E8, 0)
        l50 = npc['rootlocal']
        guarda_ram = [c.r32(esc.objs + (g - EOBJS)) if g < esp.MGW else c.r32(mgw + (g - esp.MGW)) for g in esp.GUARDA]
        b84 = c.rf(c.r32(x + 0xB84))
        aire = c.r8(x + 0x2061)

        def camloc(cc, l50=l50):  # mismo puente que el espejo (la pose la pone el port)
            xx = (cc.r[4] & ee.M32) - 0x640
            yaw = math.atan2(cc.rf(w + 0x50), cc.rf(w + 0x58)); co, si = math.cos(yaw), math.sin(yaw)
            for i, v in enumerate((cc.rf(xx + 0x40) + co * l50[0] + si * l50[2], cc.rf(xx + 0x44) + l50[1],
                                   cc.rf(xx + 0x48) - si * l50[0] + co * l50[2])):
                cc.w32(xx + 0x640 + 4 * i, f2b(v))
        c.hooks[Espejo.CAMLOC] = camloc
        c.hooks[Espejo.RAIZ] = lambda cc: None  # como el espejo: la raiz la consume el port
        try:
            c.call(TICK, x)
        except (RuntimeError, NotImplementedError) as ex:
            tot['error_ram'] += 1; difs.append(dict(n=b['n'], error=str(ex)[:80])); continue
        t5 = esc.tarea(c) if borrada else None
        ram = dict(pout=[c.r32(x + 0x40 + 4 * i) for i in range(3)],
                   tick=([c.r32(w + 0x90 + 4 * i) for i in range(3)], c.r32(x + 0x18D0), c.r32(x + 0x184C),
                         c.r32(mgw + 0x1F0 + 4 * esc.idx), c.r32(mgw + 0x1E8 + 4 * esc.idx), int(bool(borrada)),
                         c.r32(w + 0x50), c.r32(w + 0x58)),
                   tareas=[(t5['modo'], t5['fase'], f2b(t5['duracion']))] if t5 else [],
                   gout=[c.r32(esc.objs + (g - EOBJS)) if g < esp.MGW else c.r32(mgw + (g - esp.MGW)) for g in esp.GUARDA])
        o = esp.ia(dict(seed_in=a['bubbles']['seed'], estado=npc['estado'], anterior=npc['previo'], jugador=jug['estado'], dif=2,
                        tarea=tarea, bub=npc['ancla'][:3], jug=jug.get('ancla', jug['pos'])[:3], frente=[ult['dx'], ult['dz']],
                        stick=npc['stick'], gates=[0, 0], icono_in=0, p=pin, pprev=npc['pos'], a90in=npc['work90'],
                        b84=b84, aire=aire, l50=l50, gin=guarda_ram), 'tick')
        tot['cuadros'] += 1; tot[f'estado{npc["estado"]}'] += 1
        # MGW+0x248..+0x254 son punteros al WCF del invitado: el puente solo pone nodo != 0 (lo unico que se mira)
        g_o, g_r = list(o['gout']), list(ram['gout'])
        g_o[14], g_r[14] = int(g_o[14] != 0), int(g_r[14] != 0)
        g_o[15:18] = g_r[15:18] = [0, 0, 0]
        igual = o['pout'] == ram['pout'] and o['tick'] == ram['tick'] and g_o == g_r and o['tareas'] == ram['tareas'] and \
            o['seed_out'] == c.r32(SEMILLA)
        tot['con_tarea'] += tarea; tot['tarea5'] += ram['tick'][5]; tot['tarea5_iguales'] += ram['tick'][5] and igual
        tot['espejo=ram'] += igual
        if not igual and len(difs) < 8:
            difs.append(dict(n=b['n'], estado=npc['estado'], espejo=dict(pout=o['pout'], tick=o['tick']), ram=ram))
        if nb['estado'] == npc['estado']:  # contra la captura: lo que guarda la fila siguiente
            tot['captura_n'] += 1
            tot['captura_suelo<1e-3'] += abs(ee.fv(o['tick'][1]) - nb['pos'][1]) < 1e-3 or bool(aire)
            tot['captura_work90<1e-3'] += max(abs(ee.fv(u) - v) for u, v in zip(o['tick'][0], nb['work90'])) < 1e-3
            tot['captura_tipo'] += o['gout'][18] >> 16 == b['bubbles']['contacto238']['tipo']
            tot['captura_xz<1e-3'] += max(abs(ee.fv(o['pout'][i]) - nb['pos'][i]) for i in (0, 2)) < 1e-3
    esc.mem[:] = esc.base
    return dict(resumen=dict(tot), diferencias=difs)


def prueba_juego(log):
    """Lineas `dbz3 iwmips bubbles ia|upd` del juego (C++) reejecutadas en el oraculo Python con el mismo espejo."""
    esp, tot, difs = Espejo(), Counter(), []
    fl = lambda s: [float(x) for x in s.split(',')]
    for l in open(log, encoding='utf-8', errors='replace'):
        if 'dbz3 iwmips bubbles largos=' in l:
            for par in l.split('largos=', 1)[1].strip().strip(';').split(';'):
                k, v = par.split(':'); esp.largos[int(k, 16)] = float(v)
            continue
        m = re.search(r'dbz3 iwmips bubbles (ia|upd|tray|tarea5|tick) ', l)
        if not m:
            continue
        que = m.group(1)
        kv = dict(re.findall(r'(\w+)=(\S+)', l[m.end():]))
        e = dict(seed_in=int(kv['seed_in'], 16), estado=int(kv['estado']), anterior=int(kv['anterior']),
                 jugador=int(kv['jugador']), dif=int(kv['dif']), tarea=kv['tarea'] == '1', bub=fl(kv['bub']),
                 jug=fl(kv['jug']), frente=fl(kv['frente']), stick=fl(kv['stick']),
                 gates=[int(x) for x in kv['gates'].split('/')], icono_in=int(kv['icono_in']))
        if 'p' in kv:
            e.update(p=fl(kv['p']), cc4=int(kv['cc4']), codigo=int(kv['codigo'], 16), cuadro=float(kv['cuadro']),
                     dur=int(kv['dur']), fin=int(kv['fin']), f18e8=int(kv['f18e8']), reloj=float(kv['reloj']),
                     f218in=int(kv['f218in']), latch=int(kv['latch']))
        if 'tm' in kv:
            tm = kv['tm'].split('/')
            e.update(tm=(int(tm[0]), int(tm[1]), float(tm[2]), float(tm[3]), fl(tm[4]), fl(tm[5])), n238=fl(kv['n238']))
        if 'pprev' in kv:  # campos de la tick (6/11 E2), en todas las llamadas
            e.update(pprev=fl(kv['pprev']), a90in=fl(kv['a90in']), suelo_in=float(kv['suelo_in']), b84=float(kv['b84']),
                     aire=int(kv['aire']), l50=fl(kv['l50']), gin=[int(x, 16) for x in kv['gin'].strip(',').split(',')] if 'gin' in kv else None)
            esp.col = []
            for x in ([] if kv['col'] == '-' else kv['col'].strip(';').split(';')):
                p = x[1:].split('/')
                if x[0] == 'C':
                    esp.col.append(('C', [f2b(v) for v in fl(p[1])]))
                else:
                    esp.col.append((x[0], int(p[0]), [f2b(v) for v in fl(p[1])], [f2b(v) for v in fl(p[2])], f2b(float(p[3])),
                                    int(p[4]), int(p[5])))
        o = esp.ia(e, que)
        if esp.col:
            tot['puentes_sobrantes'] += 1
        tj = [] if kv['tareas'] == '-' else [tuple(t.split('/')[:3]) for t in kv['tareas'].strip(';').split(';')]
        tj = [(int(m_), int(fa), f2b(float(d))) for m_, fa, d in tj]
        ok = (o['seed_out'] == int(kv['seed_out'], 16) and o['flags'] == kv['flags'] and o['tareas'] == tj
              and o['voz'] == int(kv['voz']) and o['icono'] == int(kv['icono']))
        if que == 'upd':
            sj = kv['sale'].split('/')
            sale_j = (int(sj[0]), int(sj[1]), int(sj[2], 16), f2b(float(sj[3])), int(sj[4]), int(sj[5]), f2b(float(sj[6])),
                      int(sj[7]))
            evj = [] if kv['ev'] == '-' else [(x[0], int(x[1:].split('/')[0]), int(x.split('/')[1]), f2b(float(x.split('/')[2])))
                                               for x in kv['ev'].strip(';').split(';')]
            evo = [(t, n, c4, f2b(d)) for t, n, c4, d in o['ev']]
            ok = ok and o['sale'] == sale_j and evo == evj
            tot['transiciones'] += int(sj[0]) != e['estado']
            tot['platanos'] += sum(1 for x in evj if x[0] == 'B')
        if que == 'tray':
            to = kv['tout'].split('/')
            ok = ok and o['pout'] == [f2b(x) for x in fl(kv['pout'])] and o['tout'] == (
                int(to[0]) & 0xFFFF, f2b(float(to[1])), [f2b(x) for x in fl(to[2])], int(to[3]))
            tot['tareas_acabadas'] += int(to[3])
        if que == 'tick':
            fo = fl(kv['fout'])
            tj_ = ([f2b(x) for x in fl(kv['a90'])], f2b(float(kv['suelo'])), f2b(float(kv['f184c'])), int(kv['t1f0']),
                   int(kv['t1e8']), int(kv['t5']), f2b(fo[0]), f2b(fo[1]))
            ok = ok and o['pout'] == [f2b(x) for x in fl(kv['pout'])] and o['tick'] == tj_ and \
                o['gout'] == [int(x, 16) for x in kv['gout'].strip(',').split(',')]
            tot['tick_choques'] += sum(1 for x in kv['col'].split(';') if x[:2] in ('S1', 'Y1'))
            tot['tick_barridos_con_choque'] += sum(1 for x in kv['col'].split(';') if x[:2] == 'S1')
            tot['tick_tarea5'] += int(kv['t5'])
            tot[f"tick_estado{kv['estado']}"] += 1
        tot[que] += 1; tot[que + '_iguales'] += ok; tot['tareas'] += len(tj); tot['voces'] += int(kv['voz']) >= 0
        tot['us_suma'] += float(kv['us']); tot['us_max'] = max(tot['us_max'], float(kv['us']))
        if not ok and len(difs) < 10:
            difs.append(dict(que=que, tick=kv['tick'], juego=kv, oraculo={k: v for k, v in o.items()}))
    n = tot['ia'] + tot['upd'] + tot['tray'] + tot['tarea5'] + tot['tick']
    if n:
        tot['us_media'] = round(tot.pop('us_suma') / n, 2)
    return dict(resumen=dict(tot), diferencias=difs)


CAP_IA = Path('E:/DBZ3HD_trabajo/dorados_noche/7D1c/ia.jsonl')   # N2: sonda cap_ia.py (anillo, cada llamada), en E:


def prueba_espejo_ia(cap=CAP_IA):
    """N2: el espejo del juego frente a cada llamada de la sonda en anillo cap_ia.py (mismos campos que iwx_bubbles_ia)."""
    esp, tot, malas = Espejo(), Counter(), []
    fl = lambda v: struct.unpack('<f', struct.pack('<I', v))[0]
    s16 = lambda v: (v & 0xFFFF) - 0x10000 if v & 0x8000 else v & 0xFFFF
    for linea in open(cap, encoding='utf-8'):
        e = json.loads(linea)
        jug = s16(e['jugador'])
        o = esp.ia(dict(seed_in=e['seed_in'], estado=s16(e['estado']), anterior=0, jugador=jug, dif=2,
                        tarea=bool(e['tarea_in']), bub=[fl(e['bx']), fl(e['by']), fl(e['bz'])],
                        jug=[fl(e['px']), fl(e['py']), fl(e['pz'])], frente=[fl(e['dx']), fl(e['dz'])], stick=[0.0, 0.0],
                        gates=[e['b248'], e['b180']] if 'b248' in e else [0, 1 if jug == 0 else 0], icono_in=0,
                        **({'f200in': e['f200_in'], 'f210in': e['f210_in'], 'f214in': e['f214_in'], 'f218in': e['f218_in']}
                           if 'f218_in' in e else {})))
        quiere = (e['seed_out'], ''.join('1' if e[k] else '0' for k in ('f200', 'f210', 'f214', 'f218')))
        tot['llamadas'] += 1
        tot['tiradas'] += e['seed_in'] != e['seed_out']
        ok = (o['seed_out'], o['flags']) == quiere
        tot['iguales'] += ok
        if not ok and len(malas) < 8:
            malas.append((e['i'], e['cnt'], [hex(quiere[0]), quiere[1]], [hex(o['seed_out']), o['flags']]))
    return dict(tot), malas


def comprobar(res):
    """Autocomprobacion con los datos del jugador: falla si el interprete deja de reproducir lo medido el 6/11."""
    ia = res['ia']['resumen']
    assert ia['llamadas'] == 350 and ia.get('semilla_ok=True') == 350 and ia.get('flags_ok=True') == 350, ia
    assert res['ia_sin_icono']['resumen'].get('semilla_ok=True') == 349  # sin el efecto del icono: como el port
    assert res['espejo'] == dict(llamadas=350, iguales=350), res['espejo']  # espejo de src/iw_mips.cpp
    ee_ = res['espejo_estados']
    assert ee_['cuadros'] == ee_['estado'] == ee_['codigo'] == ee_['cuadro'] == ee_['fin'] == 1798, ee_
    t3 = res['ia']['tareas']
    assert len(t3) == 3 and all(x['decision_exacta'] and max(x['v40_ulps']) <= 1 for x in t3), t3
    assert len(res['tareas4']) == 4 and all(x['ok'] and x['mejor'][0]['ang'] < 1 for x in res['tareas4'])
    e = res['estados']['resumen']
    assert e['cuadros'] == e['igual'] == 1798, e
    # voces del original (bubbles_audio: linea por cuadro): 18078=4, 18159=4, 18256=3 (entra en 2), 18257=4
    assert res['estados']['voces'] == [(18078, [4]), (18159, [4]), (18256, [3]), (18257, [4])], res['estados']['voces']
    t = res['espejo_tick']['resumen']  # tabla de ticks: espejo + guarda = RAM entera de la captura, palabra a palabra
    assert t['cuadros'] > 400 and t['espejo=ram'] == t['cuadros'] and t['captura_work90<1e-3'] == t['captura_n'], t
    t4 = res['espejo_tick4']['resumen']
    assert t4['espejo=ram'] == t4['cuadros'] and t4['tarea5_iguales'] == t4['tarea5'], t4
    print('bubbles.py autocomprobacion OK (memory, IA 350/350, tareas 3/3 + 4/4, estados 1798/1798, voces 4/4, '
          f'ticks {t["espejo=ram"]}/{t["cuadros"]})')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('que', nargs='?', default='todo')
    ap.add_argument('--salida', type=Path, default=Path('E:/DBZ3HD_trabajo/noche_0711/N2/bubbles_resultado.json'))
    ap.add_argument('--log', type=Path, help='juego: dbz3.log de una partida con dbz3_iw_logica_original y DBZ3_TEST_IW_SEMILLA')
    a = ap.parse_args()
    if a.que == 'espejo_ia' or (a.que == 'comprobar' and not CAP.exists()):
        if a.que == 'comprobar':   # N2: la captura solIWX (y el savestate, opusBB, fase0E) se borraron con D:/tmp el 7/11
            print('captura solIWX ausente (D:/tmp borrado el 7/11): solo el espejo de la IA contra', CAP_IA)
        tot, malas = prueba_espejo_ia()
        print('IA con el espejo del juego frente a la sonda cap_ia:', tot)
        for m in malas:
            print('  DISTINTA', m)
        if not (tot.get('llamadas') and tot['iguales'] == tot['llamadas']):
            raise SystemExit(1)
        return tot
    if a.que == 'juego':
        r = prueba_juego(a.log)
        print('juego (C++) frente al oraculo Python:', r['resumen'])
        for x in r['diferencias']:
            print('  DIF', x)
        return r
    nombres = leer_simbolos()
    mem, dif = cargar()
    res = dict(codigo_distinto_ram_vs_elf=[hex(x) for x in dif])
    print('palabras de codigo distintas RAM/ELF (parches de la sonda):', len(dif), [hex(x) for x in dif[:8]])
    todo = a.que in ('todo', 'comprobar')
    if todo or a.que == 'memory':
        res['memory'] = prueba_memory(mem, nombres)
        print('Memory:', res['memory'])
        mem, _ = cargar()
    if todo or a.que == 'ia':
        censo = Counter()
        res['ia'] = prueba_ia(mem, nombres, CAP / '7D1/cuadros.jsonl', censo=censo)
        res['ia_censo'] = {f'{k:#x} {nombres.get(k, "?")}': v for k, v in censo.most_common()}
        print('IA:', res['ia']['resumen'], 'ips', res['ia']['ips'])
        for x in res['ia']['fallos'][:10]:
            print('  FALLO', x)
        for x in res['ia']['tareas']:
            print('  TAREA', x)
        res['ia_sin_icono'] = prueba_ia(mem, nombres, CAP / '7D1/cuadros.jsonl', icono=False)
        print('IA (icono como puente):', res['ia_sin_icono']['resumen'])
        res['espejo'] = prueba_espejo(CAP / '7D1/cuadros.jsonl')
        print('IA con el espejo del juego (solo ELF + puentes):', res['espejo'])
        res['espejo_estados'] = prueba_espejo_estados(CAP / '7D1/cuadros.jsonl')
        print('tabla de estados con el espejo del juego:', res['espejo_estados'])
        res['espejo_tray'] = prueba_espejo_tray([CAP / '7D1/cuadros.jsonl', CAP4])
        print('trayectoria con el espejo (tiempo estimado en capturas viejas):', res['espejo_tray'])
    if todo or a.que == 'estados':
        res['estados'] = prueba_estados(mem, nombres, CAP / '7D1/cuadros.jsonl')
        print('estados:', res['estados']['resumen'], res['estados']['transiciones'], 'voces', res['estados']['voces'])
        res['movimiento'] = prueba_estados(mem, nombres, CAP / '7D1/cuadros.jsonl', mover=True)
        print('movimiento (aprox.):', res['movimiento']['resumen'])
    if todo or a.que == 'tick':
        res['espejo_tick'] = prueba_espejo_tick(CAP / '7D1/cuadros.jsonl', nombres, paso_filas=3)
        print('tabla de ticks: espejo + guarda frente a la RAM de la captura, y contra la fila siguiente:',
              res['espejo_tick']['resumen'])
        for x in res['espejo_tick']['diferencias'][:4]:
            print('  DIF', x)
        res['espejo_tick4'] = prueba_espejo_tick(CAP / '7D1/cuadros.jsonl', nombres, solo_tarea=True)  # + tarea 5 por choque
        print('tabla de ticks, todos los cuadros con tarea:', res['espejo_tick4']['resumen'])
        for x in res['espejo_tick4']['diferencias'][:4]:
            print('  DIF', x)
    if todo or a.que == 'velocidad':
        res['velocidad'] = prueba_velocidad(mem, nombres, CAP / '7D1/cuadros.jsonl')
        print('velocidad:', res['velocidad'])
    if todo or a.que == 'tareas':
        res['tareas4'] = prueba_tareas(mem, nombres)
        for x in res['tareas4']:
            print('  DECISION', x)
    if a.que == 'comprobar':
        comprobar(res)
    a.salida.parent.mkdir(parents=True, exist_ok=True)
    a.salida.write_text(json.dumps(res, indent=1, default=str), encoding='utf-8')
    return res


if __name__ == '__main__':
    main()

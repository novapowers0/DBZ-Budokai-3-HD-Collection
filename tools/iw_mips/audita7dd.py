# SPDX-License-Identifier: MIT
"""Diagnostico F2 offline: no conecta con PCSX2 ni lanza procesos.

`muestra` reejecuta una muestra de IA y conserva las ultimas operaciones COP1
antes de escribir work+0x264. `contrato` lee las parejas A/B/C existentes.
Los resultados son observaciones locales; no certifican una partida completa.
"""
import argparse
import hashlib
import json
import math
import statistics
import struct
import time
from collections import Counter, deque
from itertools import product
from pathlib import Path

import persecucion as P

B, ee = P.B, P.ee


def identidad(path):
    path = Path(path)
    return dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def muestra(indice, perfil):
    meta = json.loads((P.CAP / 'muestras.json').read_text(encoding='utf-8'))
    datos = (P.CAP / 'muestras.bin').read_bytes()
    mu = meta['muestras'][indice]
    n = sum(r[2] for r in meta['pre']) + sum(r[2] for r in meta['post'])
    d, b0 = datos[mu['off']:mu['off'] + n], meta['base']
    mem, _ = B.cargar(B.ELF, P.P2S)
    mem[0xF0000:0x100000] = bytes(0x10000)
    sitios = {}
    for nombre, direccion, largo, off in meta['pre']:
        src = P.u32(mem, direccion) if nombre else direccion
        if nombre:
            g = next((r for r in meta['pre'] if r[0] is None and r[1] <= direccion < r[1] + r[2]), None)
            if g:
                src = P.u32(d, g[3] - b0 + direccion - g[1])
        sitios[nombre or hex(direccion)] = src
        mem[src:src + largo] = d[off - b0:off - b0 + largo]
    struct.pack_into('<I', mem, B.SEMILLA, mu['seed_in'])
    objetivo = sitios['work'] + 0x264
    ultimas, escrituras, rsqrts = deque(maxlen=24), [], []

    class Traza(ee.Ee):
        pc_actual = 0

        def w32(s, a, v):
            if a == objetivo:
                escrituras.append(dict(pc=hex(s.pc_actual), bits=hex(v), cop1=list(ultimas)))
            super().w32(a, v)

        def decodificar(s, pc):
            fn = super().decodificar(pc)
            w = s.r32(pc)
            cop1 = w >> 26 == 0x11
            texto = ee.RB.Instruction(w, pc, ee.RB.InstrCategory.R5900).disassemble() if cop1 else ''

            def paso():
                s.pc_actual = pc
                if cop1:
                    antes = tuple(s.f)
                    r = fn()
                    cambios = {str(i): [hex(x), hex(y)] for i, (x, y) in enumerate(zip(antes, s.f)) if x != y}
                    ultimas.append(dict(pc=hex(pc), instruccion=texto, cambios=cambios))
                    if w & 63 == 0x16 and (w >> 21) & 31 == 16:  # rsqrt.s
                        fs, ft, fd = (w >> 11) & 31, (w >> 16) & 31, (w >> 6) & 31
                        rsqrts.append(dict(pc=hex(pc), fs=hex(antes[fs]), ft=hex(antes[ft]), fd=hex(s.f[fd])))
                    return r
                return fn()

            s.code[pc] = paso
            return paso

    original = set(ee.FPU_HW)
    try:
        if perfil == 'sin-mul':
            ee.FPU_HW = original - {'mul'}
        elif perfil == 'sin-hw':
            ee.FPU_HW = set()
        c = Traza(mem, B.GP)
        c.hooks.update({B.PRINTF: lambda c: None, B.VOZ: lambda c: None, B.BOCA: lambda c: None})
        c.call(P.IA, mu['actor'])
        diferencias = []
        for nombre, direccion, largo, off in meta['post']:
            src = sitios[nombre or hex(direccion)]
            q = d[off - b0:off - b0 + largo]
            for o in range(0, largo, 4):
                obtenido = bytes(mem[src + o:src + o + 4])
                if q[o:o + 4] != obtenido:
                    diferencias.append([nombre or hex(direccion), hex(o), q[o:o + 4].hex(), obtenido.hex()])
        return dict(indice=indice, contador=mu['cnt'], perfil=perfil, semilla_igual=c.r32(B.SEMILLA) == mu['seed_out'],
                    diferencias=diferencias, escrituras=escrituras, rsqrts=rsqrts, instrucciones=c.n,
                    entrada=identidad(P.CAP / 'muestras.json'))
    finally:
        ee.FPU_HW = original


def perfiles():
    """Ablacion acotada: un perfil que arregla una muestra puede empeorar las otras."""
    original, filas = set(ee.FPU_HW), []
    try:
        for mul, rsqrt, sqrt in product((True, False), repeat=3):
            quitadas = {n for n, activo in (('mul', mul), ('rsqrt', rsqrt), ('sqrt', sqrt)) if not activo}
            ee.FPU_HW = original - quitadas
            t = time.perf_counter()
            tot, malas, campos = P.prueba_dorado(P.CAP, P.P2S)
            filas.append(dict(fpu_hw=sorted(ee.FPU_HW), totales=dict(tot), primera_diferencia=malas[:1],
                              diferencias=malas, campos=[(str(k), v) for k, v in campos.items()],
                              duracion_s=round(time.perf_counter() - t, 3)))
        return dict(entradas=[identidad(P.CAP / 'muestras.json'), identidad(P.CAP / 'muestras.bin'),
                              identidad(B.ELF), identidad(P.P2S), identidad(Path(ee.__file__))],
                    perfiles=filas, alcance='IA independiente A->B, no hardware ni partida continua')
    finally:
        ee.FPU_HW = original


def contrato():
    meta = json.loads((P.CAP_TICK / 'parejas.json').read_text(encoding='utf-8'))
    datos = (P.CAP_TICK / 'parejas.bin').read_bytes()
    b0 = meta['base']
    tam = max(o + n for bl in meta['bloques'].values() for _, _, n, o in bl) - b0
    mem, _ = B.cargar(B.ELF, P.P2S)
    base = bytes(mem)
    filas, distancias, diferencias_cabeza, pasos_entre = [], [], [], []
    for k, par in enumerate(meta['parejas']):
        mem[:] = base
        d = datos[par['off']:par['off'] + tam]
        for fase in ('A', 'B', 'C'):
            sitios = P._bloques(meta, d, b0, mem, fase)
            if fase == 'A':
                cobertura = []
            cobertura.extend((sitios[(str(tipo), direccion)], largo)
                             for tipo, direccion, largo, _ in meta['bloques'][fase])
            cubierto = lambda a, n: any(p and p <= a and a + n <= p + largo for p, largo in cobertura)
            mgr = P.u32(mem, 0x49CA70)
            jug, pic = mgr + 0x800, mgr + 0x28D0
            js = struct.unpack_from('<h', mem, jug + 0xCBC)[0]
            ps = struct.unpack_from('<h', mem, pic + 0xCBC)[0]
            xyz = lambda p, o: struct.unpack_from('<3f', mem, p + o)
            j, p = xyz(jug, 0x40), xyz(pic, 0x40)
            dh = math.hypot(j[0] - p[0], j[2] - p[2])
            d3 = math.dist(j, p)
            cabeza, ancla = xyz(pic, 0x770), xyz(pic, 0x640)
            distancia_anclas = math.dist(xyz(jug, 0x640), ancla)
            ctx = P.u32(mem, 0x49CDC0)
            distancia_ctx = struct.unpack_from('<f', mem, ctx + 0x75C)[0] if cubierto(ctx + 0x75C, 4) else None
            err = math.dist(cabeza, (ancla[0], ancla[1] + 5.5, ancla[2]))
            diferencias_cabeza.append(err)
            distancias.append((js == 9, dh < 14, d3 < 14))
            filas.append(dict(indice=k, contador=par['cnt'], fase=fase, jugador=js, piccolo=ps,
                              horizontal=dh, distancia3d=d3, cabeza_error=err,
                               clip_jugador=hex(P.u32(mem, jug + 0xB60)),
                               accion_pendiente=P.u32(mem, P.u32(mem, 0x49CBC0) + 0x1A8),
                               distancia_anclas=distancia_anclas, distancia_ctx_capturada=distancia_ctx,
                               hipotesis_anclas8=distancia_anclas <= 8 and ps != 11 and ps < 18))
            if fase == 'B':
                sem_b = P.u32(mem, B.SEMILLA)
            elif fase == 'C':
                sem_c, s = P.u32(mem, B.SEMILLA), sem_b
                for n in range(4097):
                    if s == sem_c:
                        pasos_entre.append(n)
                        break
                    s = P.lfsr(s, 1)
                else:
                    pasos_entre.append(None)
    contadores = [p['cnt'] for p in meta['parejas']]
    capturas = [f for f in filas if f['jugador'] == 9]
    falsas_h = [f for f in filas if f['jugador'] != 9 and f['horizontal'] < 14]
    falsas_3 = [f for f in filas if f['jugador'] != 9 and f['distancia3d'] < 14]
    inicio = next((f['indice'] for f in filas if f['piccolo'] == 13), None)
    activas = [f for f in filas if inicio is not None and f['indice'] >= inicio]

    def censo(ff):
        return dict(observaciones=len(ff), capturas=sum(f['jugador'] == 9 for f in ff),
                    horizontal_igual=sum((f['jugador'] == 9) == (f['horizontal'] < 14) for f in ff),
                    horizontal_falsa_positiva=sum(f['jugador'] != 9 and f['horizontal'] < 14 for f in ff),
                    horizontal_falsa_negativa=sum(f['jugador'] == 9 and f['horizontal'] >= 14 for f in ff),
                    distancia3d_igual=sum((f['jugador'] == 9) == (f['distancia3d'] < 14) for f in ff),
                    primera_diferencia_horizontal=next((f for f in ff if (f['jugador'] == 9) != (f['horizontal'] < 14)), None))
    alcance = Path('E:/DBZ3HD_trabajo/pendientes_0711/P1/alcance7dd.jsonl')
    partidas = [json.loads(l) for l in alcance.read_text(encoding='utf-8').splitlines() if l.strip()]
    grupos = {}
    for modo in (False, True):
        buenas = [p for p in partidas if p.get('alcanza') and p.get('semilla_savestate') == modo]
        ticks = [p['cuadros'] for p in buenas]
        grupos[str(modo)] = dict(partidas=len(ticks), ticks=ticks,
                                  minimo=min(ticks) if ticks else None, maximo=max(ticks) if ticks else None,
                                  mediana=statistics.median(ticks) if ticks else None)
    return dict(entradas=[identidad(P.CAP_TICK / 'parejas.json'), identidad(P.CAP_TICK / 'parejas.bin'), identidad(alcance)],
                parejas=len(contadores), observaciones=len(filas), fases='A/B/C; correlacionadas, no partidas independientes',
                delta_contador=dict(Counter(b - a for a, b in zip(contadores, contadores[1:]))),
                captura_estado9=len(capturas), primera_captura=capturas[:1],
                horizontal_igual=sum(a == b for a, b, c in distancias), horizontal_falsa_positiva=len(falsas_h),
                primera_falsa_horizontal=falsas_h[:1], distancia3d_igual=sum(a == c for a, b, c in distancias),
                distancia3d_falsa_positiva=len(falsas_3), primera_falsa_3d=falsas_3[:1],
                cabeza_fija_error_max=max(diferencias_cabeza), cabeza_fija_error_mediana=statistics.median(diferencias_cabeza),
                partidas_registradas=len(partidas), errores=sum('error' in p for p in partidas),
                 partidas_por_semilla_savestate=grupos, desde_primer_estado13=censo(activas),
                 contexto_distancia_capturado=sum(f['distancia_ctx_capturada'] is not None for f in filas),
                 anclas8_desde_estado13=dict(observaciones=len(activas),
                     iguales=sum((f['jugador'] == 9) == f['hipotesis_anclas8'] for f in activas),
                     primera_diferencia=next((f for f in activas if (f['jugador'] == 9) != f['hipotesis_anclas8']), None),
                     alcance='Hipotesis instantanea; contexto +0x75C puede ser del render anterior; estado9 persiste'),
                 entorno_captura=[f for f in filas if 580 <= f['indice'] <= 584],
                pasos_lfsr_B_C={str(k): v for k, v in Counter(pasos_entre).items()},
                 lfsr_resueltos=sum(n is not None for n in pasos_entre), lfsr_limite=4096)


def captura():
    """Predicado del ELF EU aislado. Las llamadas de resultado/sonido son observadas, no ejecutadas."""
    mem, _ = B.cargar(B.ELF, P.P2S)
    c = ee.Ee(mem, B.GP)
    for a, v in ((0x49CA70, P.MGR), (0x49CBC0, P.OBJS), (0x49CBA4, P.CTXD),
                 (0x49CBB8, P.MGW), (0x49CDC0, P.CTXM)):
        c.w32(a, v)
    eventos = []
    c.hooks[0x299900] = lambda c: eventos.append(('objetivo', c.r[4] & ee.M32, c.r[5] & ee.M32))
    c.hooks[B.MEMORY] = lambda c: eventos.append(('memory',))
    c.hooks[P.HITS_SON] = lambda c: eventos.append(('sonido',))
    filas, fallos = [], []
    # Limites exactos float32, incluidos los dos vecinos de 8; estados firmados.
    for tipo, estado, bits, vida, restante, gate in product(
            (0, 14), (-1, 0, 10, 11, 12, 15, 17, 18),
            (0, 0x40FFFFFF, 0x41000000, 0x41000001, 0x447A0000),
            (0.0, 1.0), (-1.0, 0.0, 1.0), (0, 1, 2)):
        c.w32(P.CTXD + 4, tipo)
        c.w16(P.MGR + 0x358C, estado & 0xFFFF)
        c.w32(P.CTXM + 0x75C, bits)
        c.w32(P.MGR + 0x2050, B.f2b(vida))
        c.w32(P.MGR + 0x7CC, 100)
        c.w32(P.MGR + 0x8B9C, B.f2b(100 - restante))
        c.w32(P.OBJS + 0x1B0, gate)
        c.w32(P.OBJS + 0x1A8, 0)
        eventos.clear()
        c.call(0x2A6260)
        distancia = B.b2f(bits) if hasattr(B, 'b2f') else P.b2f(bits)
        contacto = tipo == 14 and distancia <= 8 and estado != 11 and estado < 18
        esperado = gate == 0 and (restante <= 0 or vida <= 0 or contacto)
        esperado_eventos = [('objetivo', P.JUG, 2), ('memory',), ('sonido',)] if esperado else []
        ok = (c.r32(P.OBJS + 0x1A8) == (48 if esperado else 0)
              and c.r32(P.OBJS + 0x1B0) == (1 if esperado else gate)
              and eventos == esperado_eventos)
        fila = dict(tipo=tipo, estado=estado, distancia_bits=hex(bits), vida=vida, restante=restante,
                    gate=gate, activa_esperada=esperado, accion=c.r32(P.OBJS + 0x1A8),
                    gate_final=c.r32(P.OBJS + 0x1B0), eventos=list(eventos), igual=ok)
        filas.append(fila)
        if not ok:
            fallos.append(fila)
    return dict(entradas=[identidad(B.ELF), identidad(P.P2S), identidad(Path(ee.__file__))],
                funcion='EU 0x2A6260', casos=len(filas), iguales=sum(f['igual'] for f in filas),
                primera_diferencia=fallos[:1], diferencias=fallos, casos_detalle=filas,
                alcance='ELF EU frente al contrato inferido; entradas sinteticas, no dorado ni captura completa',
                 dependencias_sustituidas=['0x299900 objetivo', hex(B.MEMORY) + ' RNG', hex(P.HITS_SON) + ' sonido'])


def transicion():
    """Tarea de objetivo tipo2 -> despacho9 -> entrada original de pose. Efectos fuera del contrato."""
    mem, _ = B.cargar(B.ELF, P.P2S)
    c = ee.Ee(mem, B.GP)
    for a, v in ((0x49CA70, P.MGR), (0x49CBC0, P.OBJS), (0x49CBA4, P.CTXD), (0x49CBB8, P.MGW)):
        c.w32(a, v)
    # Se ejecutan 0x299000, 0x2AAF60 y 0x2A30B0; solo se sustituyen sus efectos ajenos a la pose.
    mudos = (0x238670, 0x2A54C0, 0x2AAA60, 0x1D55A0, 0x11DEF8, 0x302570, 0x2E8AE0, 0x13FEB0)
    eventos = []
    for pc in mudos:
        c.hooks[pc] = lambda c, pc=pc: eventos.append(hex(pc))
    filas, fallos = [], []
    for estado, aire, procesada, gate, pausa in product((1, 9, 16), (0, 1), (0, 1), (0, 1, 2), (0, 1)):
        mem[P.JUG:P.JUG + 0x20D0] = bytes(0x20D0)
        mem[P.OBJS:P.OBJS + 0x260] = bytes(0x260)
        mem[P.TAREA:P.TAREA + 0x100] = bytes(0x100)
        c.w32(P.CTXD + 4, 14)
        c.w32(P.MGR + 0x8B7C, pausa)
        c.w32(0x49C7DC, B.f2b(1.2))
        c.w16(P.JUG + 0xCBC, estado)
        c.w16(P.JUG + 0xCBE, 77)
        c.w32(P.JUG + 0xB60, 0xB)
        c.w8(P.JUG + 0x2061, aire)
        c.w32(P.OBJS + 0x1B0, gate)
        c.w32(P.OBJS + 0x1A8, 48)
        c.w32(P.OBJS + 0x180, P.TAREA)
        c.w32(P.TAREA + 0x58, 0)
        c.w16(P.TAREA + 0x6C, 2)
        c.w8(P.TAREA + 0x6E, procesada)
        c.w32(P.TAREA + 0x54, B.f2b(150))
        eventos.clear()
        c.call(0x299000, P.TAREA)
        solicita = estado != 16 and not aire and not procesada
        entra = solicita and gate < 2
        ok = (c.r16(P.JUG + 0xCBC) == (9 if entra else estado)
              and c.r16(P.JUG + 0xCBE) == (estado if entra else 77)
              and c.r32(P.JUG + 0xB60) == (5 if entra else 0xB)
              and c.r8(P.TAREA + 0x6E) == (1 if solicita else procesada)
              and c.r32(P.OBJS + 0x1A8) == 48
              and c.r32(P.TAREA + 0x50) == (0 if pausa else B.f2b(1.2))
              and bool(eventos) == entra)
        fila = dict(estado=estado, aire=aire, procesada=procesada, gate=gate, pausa=pausa,
                    entrada9_esperada=entra, estado_final=c.r16(P.JUG + 0xCBC),
                    clip=c.r32(P.JUG + 0xB60), accion_pendiente=c.r32(P.OBJS + 0x1A8),
                    efectos=list(eventos), igual=ok)
        filas.append(fila)
        if not ok:
            fallos.append(fila)
    return dict(entradas=[identidad(B.ELF), identidad(P.P2S), identidad(Path(ee.__file__))],
                funciones=['EU 0x299000 tarea', 'EU 0x2AAF60 despacho', 'EU 0x2A30B0 entrada9'],
                casos=len(filas), iguales=sum(f['igual'] for f in filas), primera_diferencia=fallos[:1],
                diferencias=fallos, casos_detalle=filas, dependencias_sustituidas=[hex(pc) for pc in mudos],
                alcance='Tarea sintetica tipo2 sin caducar; no bucle completo, render, sonido ni integracion C++')


def cadena():
    """Disparador -> fabrica original -> callback de tarea -> pose9, con asignador y efectos sustituidos."""
    mem, _ = B.cargar(B.ELF, P.P2S)
    c = ee.Ee(mem, B.GP)
    for a, v in ((0x49CA70, P.MGR), (0x49CBC0, P.OBJS), (0x49CBA4, P.CTXD),
                 (0x49CBB8, P.MGW), (0x49CDC0, P.CTXM)):
        c.w32(a, v)
    mudos = (0x238670, 0x2A54C0, 0x1D55A0, 0x11DEF8, 0x302570, 0x2E8AE0, 0x13FEB0,
             0x3389D0, 0x261BD0, B.MEMORY, P.HITS_SON)
    for pc in mudos:
        c.hooks[pc] = lambda c: None
    creadas = []

    def crear(c):
        callback = c.r[4] & ee.M32
        creadas.append(hex(callback))
        c.w32(P.TAREA + 0x1C, callback)
        c.r[2] = P.TAREA
    c.hooks[0x140300] = crear
    filas, fallos = [], []
    for bits, npc, estado, aire, gate in product(
            (0x40FFFFFF, 0x41000000, 0x41000001), (11, 15, 18), (1, 16), (0, 1), (0, 2)):
        mem[P.MGR:P.MGR + 0x9000] = bytes(0x9000)
        mem[P.OBJS:P.OBJS + 0x260] = bytes(0x260)
        mem[P.TAREA:P.TAREA + 0x100] = bytes(0x100)
        c.w32(P.CTXD + 4, 14)
        c.w32(P.MGR + 0x2050, B.f2b(100))
        c.w32(P.MGR + 0x7CC, 100)
        c.w16(P.JUG + 0xCBC, estado)
        c.w8(P.JUG + 0x2061, aire)
        c.w16(P.MGR + 0x358C, npc)
        c.w32(P.CTXM + 0x75C, bits)
        c.w32(P.OBJS + 0x1B0, gate)
        c.w32(0x49C7DC, B.f2b(1.2))
        creadas.clear()
        c.call(0x2A6260)
        dispara = gate == 0 and P.b2f(bits) <= 8 and npc != 11 and npc < 18
        callback = c.r32(P.TAREA + 0x1C)
        if dispara:
            c.call(callback, P.TAREA)
        entra = dispara and estado != 16 and not aire
        ok = (creadas == (['0x299000'] if dispara else [])
              and c.r16(P.JUG + 0xCBC) == (9 if entra else estado)
              and c.r32(P.OBJS + 0x1A8) == (48 if dispara else 0)
              and c.r32(P.OBJS + 0x1B0) == (1 if dispara else gate)
              and c.r32(P.OBJS + 0x180) == (P.TAREA if dispara else 0)
              and (not dispara or c.r16(P.TAREA + 0x6C) == 2)
              and (not entra or c.r32(P.JUG + 0xB60) == 5))
        fila = dict(distancia_bits=hex(bits), piccolo=npc, jugador=estado, aire=aire, gate=gate,
                    dispara_esperado=dispara, entra9_esperado=entra, tareas=list(creadas),
                    estado_final=c.r16(P.JUG + 0xCBC), accion=c.r32(P.OBJS + 0x1A8), igual=ok)
        filas.append(fila)
        if not ok:
            fallos.append(fila)
    return dict(entradas=[identidad(B.ELF), identidad(P.P2S), identidad(Path(ee.__file__))],
                funciones=['EU 0x2A6260', 'EU 0x299900', 'EU 0x299000', 'EU 0x2AAF60', 'EU 0x2A30B0'],
                casos=len(filas), iguales=sum(f['igual'] for f in filas), primera_diferencia=fallos[:1],
                diferencias=fallos, casos_detalle=filas, dependencias_sustituidas=[hex(pc) for pc in mudos] + ['0x140300 asignador'],
                alcance='Cadena local sintetica con vida/tiempo positivos; sin planificador, render ni partida continua')


def imagen_elf(path):
    raw = Path(path).read_bytes()
    mem = bytearray(ee.TOTAL)
    phoff, phnum = struct.unpack_from('<I', raw, 28)[0], struct.unpack_from('<H', raw, 44)[0]
    for k in range(phnum):
        tipo, off, va, _, n, _, _, _ = struct.unpack_from('<8I', raw, phoff + 32 * k)
        if tipo == 1:
            mem[va:va + n] = raw[off:off + n]
    return mem


def objetivo_continuo(usa):
    """Fabrica/callback originales persistentes, sin savestate ni CPU/colision de Piccolo."""
    A = P.Dir(usa)
    disparador, fabrica, callback, entrada9 = ((0x2A5DE0, 0x299670, 0x298D70, 0x2A2D10) if usa else
                                             (0x2A6260, 0x299900, 0x299000, 0x2A30B0))
    efectos = ((0x2A5090, 0x302330, 0x2E88D0, 0x13FEB0, 0x3384F0, 0x2619C0) if usa else
               (0x2A54C0, 0x302570, 0x2E8AE0, 0x13FEB0, 0x3389D0, 0x261BD0))
    elf = P.ELF_USA if usa else B.ELF
    mem = imagen_elf(elf)
    c = ee.Ee(mem, A.GP)
    tarea = P.TAREA + 0x400
    creadas, borradas = [], []

    def crear(c):
        f = c.r[4] & ee.M32
        if f != callback:
            raise RuntimeError('callback inesperado: ' + hex(f))
        mem[tarea:tarea + 0x100] = bytes(0x100)
        c.w32(tarea + 0x1C, f)
        creadas.append(f)
        c.r[2] = tarea

    c.hooks[A.CREAR] = crear
    c.hooks[A.BORRAR] = lambda c: borradas.append(c.r[4] & ee.M32)
    for pc in efectos + (A.BONES, A.AURA, A.GUION, A.SONIDO):
        c.hooks[pc] = lambda c: None
    # Sin whitelist: ejecutar TODAS las llamadas no sustituidas, incluido el RNG original.
    filas, fallos = [], []
    # Cada caso dura ocho campos: contacto, espera/pausa, retirada del contacto y persistencia de CBC9.
    for bits, npc, estado, aire, pausa, causa in product(
            (0x41000000, 0x41000001), (11, 15, 18), (1, 16), (0, 1), (0, 1), ('contacto', 'tiempo', 'vida')):
        mem[P.MGR:P.MGR + 0x9000] = bytes(0x9000)
        mem[P.OBJS:P.OBJS + 0x800] = bytes(0x800)
        mem[tarea:tarea + 0x100] = bytes(0x100)
        for p, v in ((A.P_MGR, P.MGR), (A.P_OBJS, P.OBJS), (A.P_CTXD, P.CTXD),
                     (A.P_MGW, P.MGW), (A.P_CTXM, P.CTXM)):
            c.w32(p, v)
        c.w32(P.CTXD + 4, 14)
        c.w32(A.PASO, A.PASO_BITS)
        c.w32(A.SEMILLA, 0x12345678)
        c.w32(P.MGR + 0x7CC, 100)
        c.w32(P.MGR + 0x8B9C, B.f2b(100 if causa == 'tiempo' else 0))
        c.w32(P.MGR + 0x2050, B.f2b(0 if causa == 'vida' else 100))
        c.w16(P.JUG + 0xCBC, estado)
        c.w16(P.JUG + 0xCBE, 77)
        c.w32(P.JUG + 0xB60, 0xB)
        creadas.clear(); borradas.clear()
        dispara = causa != 'contacto' or (P.b2f(bits) <= 8 and npc != 11 and npc < 18)
        historial = []
        for t in range(8):
            # El host solo aporta la pose hasta entrar en9; despues conserva la pose escrita por el ELF.
            if c.r16(P.JUG + 0xCBC) != 9:
                c.w16(P.JUG + 0xCBC, estado if t < 3 else 1)
            c.w8(P.JUG + 0x2061, aire if t < 3 else 0)
            c.w32(P.MGR + 0x8B7C, pausa if t < 3 else 0)
            c.w16(P.MGR + 0x358C, npc)
            c.w32(P.CTXM + 0x75C, bits if t == 0 else B.f2b(100))
            c.call(disparador)
            if c.r32(P.OBJS + 0x180):
                c.call(c.r32(tarea + 0x1C), tarea)
            entra = dispara and ((estado != 16 and not aire) or t >= 3)
            esperado = 9 if entra else (estado if t < 3 else 1)
            reloj = P.b2f(c.r32(tarea + 0x50))
            ok = (c.r16(P.JUG + 0xCBC) == esperado
                  and len(creadas) == int(dispara)
                  and c.r32(P.OBJS + 0x1A8) == (48 if dispara else 0)
                  and c.r32(P.OBJS + 0x1B0) == int(dispara)
                  and c.r32(P.OBJS + 0x248) == int(dispara)
                  and (not dispara or c.r32(P.OBJS + 0x180) == tarea)
                  and (not dispara or c.r8(tarea + 0x6E) == int(entra))
                  and (not entra or c.r32(P.JUG + 0xB60) == 5)
                  and (not dispara or not (pausa and t < 3) or reloj == 0))
            h = dict(t=t, estado=c.r16(P.JUG + 0xCBC), esperado=esperado,
                     reloj=reloj, procesada=c.r8(tarea + 0x6E), igual=ok)
            historial.append(h)
        fila = dict(distancia_bits=hex(bits), piccolo=npc, jugador=estado, aire=aire, pausa=pausa,
                    causa=causa, cuadros=historial, igual=all(h['igual'] for h in historial))
        filas.append(fila)
        if not fila['igual']:
            fallos.append(fila)
    return dict(entradas=[identidad(elf), identidad(Path(ee.__file__))], region='USA' if usa else 'EU',
                funciones=[hex(x) for x in (disparador, fabrica, callback, entrada9)],
                casos=len(filas), iguales=sum(f['igual'] for f in filas), campos=len(filas) * 8,
                primera_diferencia=fallos[:1], diferencias=fallos, casos_detalle=filas,
                dependencias_sustituidas=[hex(x) for x in efectos + (A.CREAR, A.BORRAR, A.BONES, A.AURA, A.GUION, A.SONIDO)],
                alcance='Cadena de objetivo persistente sintetica. RNG ejecutado; asignador y efectos sustituidos. '
                        'No CPU/colision, distribucion de nueve partidas ni aceptacion en juego.')


def mapa_objetivo():
    """Candidatos USA por instrucciones, no por un delta universal. No certifica ejecucion."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('dif_mapa_iw_usa', Path(__file__).parents[1] / 'dif_mapa_iw_usa.py')
    M = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(M)

    eu, us = imagen_elf(B.ELF), imagen_elf(P.ELF_USA)

    def firma(mem, pc, n):
        palabras = struct.unpack_from('<%dI' % n, mem, pc)
        sal = []
        for k, w in enumerate(palabras):
            op, rs = w >> 26, (w >> 21) & 31
            previo_lui = (k > 0 and palabras[k - 1] >> 26 == 15
                          and (palabras[k - 1] >> 16) & 31 == rs)
            if op in (2, 3):
                w &= 0xFC000000
            elif op in (9, 13, 15, 35, 43, 49, 57) or rs == 28 or previo_lui:
                w &= 0xFFFF0000
            sal.append(w)
        return tuple(sal)

    filas = []
    for pc, n in ((0x2A6260, 40), (0x299900, 40), (0x299000, 40), (0x2AAF60, 28),
                  (0x2A30B0, 32), (0x2A54C0, 16), (0x302570, 24), (0x2E8AE0, 24),
                  (0x13FEB0, 16), (0x3389D0, 16), (0x261BD0, 24)):
        centro = M.pc_eu_a_usa(pc)
        f = firma(eu, pc, n)
        candidatos = [hex(a) for a in range(centro - 0x100, centro + 0x104, 4) if firma(us, a, n) == f]
        filas.append(dict(eu=hex(pc), prediccion=hex(centro), instrucciones=n, candidatos=candidatos))
    return dict(entradas=[identidad(B.ELF), identidad(P.ELF_USA)], funciones=filas,
                alcance='Emparejado estructural local: inmediatos de direcciones enmascarados; requiere banco de ejecucion')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('que', choices=('muestra', 'contrato', 'perfiles', 'captura', 'transicion', 'cadena', 'mapa-objetivo', 'objetivo-continuo'))
    ap.add_argument('--usa', action='store_true', help='USA en objetivo-continuo; las auditorias antiguas siguen siendo EU')
    ap.add_argument('--indice', type=int, default=285)
    ap.add_argument('--perfil', choices=('serie', 'sin-mul', 'sin-hw'), default='sin-mul')
    ap.add_argument('--cap', type=Path, default=P.CAP)
    ap.add_argument('--p2s', type=Path, default=P.P2S)
    ap.add_argument('--salida', type=Path, help='JSON nuevo en carpeta existente; no sobrescribe evidencia')
    a = ap.parse_args()
    if a.salida and (a.salida.exists() or not a.salida.parent.is_dir()):
        ap.error('--salida debe estar ausente y su carpeta debe existir')
    P.CAP, P.P2S = a.cap, a.p2s
    t = time.perf_counter()
    r = (muestra(a.indice, a.perfil) if a.que == 'muestra' else perfiles() if a.que == 'perfiles'
          else captura() if a.que == 'captura' else transicion() if a.que == 'transicion'
           else cadena() if a.que == 'cadena' else mapa_objetivo() if a.que == 'mapa-objetivo'
           else objetivo_continuo(a.usa) if a.que == 'objetivo-continuo' else contrato())
    r['duracion_s'] = round(time.perf_counter() - t, 3)
    texto = json.dumps(r, ensure_ascii=False, indent=2)
    if a.salida:
        with a.salida.open('x', encoding='utf-8') as f:
            f.write(texto + '\n')
        print(json.dumps(dict(salida=str(a.salida), duracion_s=r['duracion_s'])))
    else:
        print(texto)
    # Un diagnostico con DIF no debe parecer un banco de igualdad aprobado.
    return int((a.que == 'muestra' and (r['diferencias'] or not r['semilla_igual']))
               or (a.que in ('captura', 'transicion', 'cadena', 'objetivo-continuo') and r['iguales'] != r['casos'])
               or (a.que == 'mapa-objetivo' and any(len(f['candidatos']) != 1 for f in r['funciones'])))


if __name__ == '__main__':
    raise SystemExit(main())

# SPDX-License-Identifier: MIT
"""F1: planificador/azar Viewer EU/USA y consumidor de pose con RNG original.

Memory, scheduler, selector y helpers originales; referencia propia persistente.
Hueso/proyeccion son servicios artificiales; huesos/audio solo peticiones. No
enlace con Input/Update, colision, hardware, dorado completo ni integracion C++.
"""
import argparse
from collections import Counter
from fractions import Fraction
import itertools
import json
from pathlib import Path
import time

import disparo_viewer_pose as pose
import disparo_marcas as marcas
import ee

base, arit, banco = pose.base, pose.arit, pose.banco
bits, leer, escribir = pose.bits, pose.leer, pose.escribir
MEMORY, LIMITE_SELECTOR = 0x127CA0, 0x3F060A92
PERFILES = {
    'eu': dict(pick=0x32D540, hueso=0x2EF6A0, proyeccion=0x2EF870),
    'us': dict(pick=0x32D210, hueso=0x2EF490, proyeccion=0x2EF660)}
MATH = ((0x119A70, 0x119B28, 'empaquetar_float_cuerpo'),
        (0x119B28, 0x119B80, 'desempaquetar_float'),
        (0x119B80, 0x119BB0, 'empaquetar_float'),
        (0x119BB0, 0x119BF0, 'extender'),
        (0x119BF0, 0x119CC8, 'empaquetar_double_cuerpo'),
        (0x119CC8, 0x119D58, 'desempaquetar_double'),
        (0x11A290, 0x11A2E8, 'dividir_double'),
        (0x11A2E8, 0x11A3E0, 'dividir_double_cuerpo'),
        (0x11A3E0, 0x11A4E0, 'comparar_double_cuerpo'),
        (0x11A4E0, 0x11A530, 'comparar_double'),
        (0x11A680, 0x11A6B0, 'empaquetar_double'),
        (0x11A6B0, 0x11A708, 'truncar'),
        (0x1002B0, 0x1002D0, 'menor_double'))
MODOS, FLAGS, PREVIOS = (0, 1, 2, ee.M32), (0, 1, 2, ee.M32), (0, 1, 2, 3, ee.M32)
ELEVACIONES = (0, bits(1), bits(-1), LIMITE_SELECTOR - 1, LIMITE_SELECTOR,
               LIMITE_SELECTOR + 1, (LIMITE_SELECTOR - 1) | 0x80000000,
               LIMITE_SELECTOR | 0x80000000, (LIMITE_SELECTOR + 1) | 0x80000000)
SEMILLAS = tuple(sorted({1, 0x12345678, 0x7FFFFFFF, 0x80000000, ee.M32,
                        *(1 << i for i in range(32)),
                        *(ee.M32 ^ (1 << i) for i in range(32))}))


def huella(ref, p):
    return pose.huella(lambda a: leer(ref, a), lambda a: pose.leer_h(ref, a), p)


def vector_hueso(variante):
    return [bits(10 + variante), bits(20 - variante), bits(30), bits(1)]


def vector_pantalla(variante):
    return [bits(-200 if variante else 100), bits(300), bits(2), bits(1)]


class PlanificadorViewer(pose.PoseViewer):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        self.p = p = dict(self.p, **PERFILES[region])
        c = self.cpu
        scheduler = (p['parambc'], MEMORY, p['parambc'], 0x119BB0, 0x11A290,
                     0x11A6B0, MEMORY, MEMORY, p['pick'], p['pick'])
        pick = (MEMORY, p['hueso'], p['camget'], p['camget'], p['proyeccion'],
                MEMORY, 0x119BB0, 0x1002B0, MEMORY, 0x119BB0, 0x1002B0, MEMORY)
        if (base.llamadas(c.m, p['scheduler'], p['scheduler'] + 0x170) != scheduler
                or base.llamadas(c.m, p['pick'], p['pick'] + 0x170) != pick):
            raise ValueError('JAL de planificador/selector fuera del perfil regional')
        c.hooks.pop(p['scheduler'])
        for a, b, rol in (*MATH, (p['scheduler'], p['scheduler'] + 0x170, 'scheduler'),
                          (p['pick'], p['pick'] + 0x170, 'pick'), (MEMORY, MEMORY + 0x40, 'memory')):
            c.whitelist.add(a); self.roles[a] = rol
            if (a, b) not in self.intervalos:
                self.intervalos.append((a, b))
        for rol in ('hueso', 'proyeccion'):
            self.roles[p[rol]] = rol
            c.hooks[p[rol]] = self._servicio(rol)
        self.hashes = [base.huella(c.m[a:b]) for a, b in self.intervalos]
        self.entrar_original = c.entrar
        c.entrar = self._entrar
        # entrar() solo DESPACHA: el cuerpo corre despues en el bucle EE.
        # Observar jr tras sw/--v0 conserva las instrucciones originales.
        retorno_memory = c.decodificar(MEMORY + 0x34)
        def observar_retorno_memory():
            self.evento_memory.update(despues=c.r32(p['semillap']), valor=c.r[2] & ee.M32)
            return retorno_memory()
        c.code[MEMORY + 0x34] = observar_retorno_memory
        self.variante = 0

    def _inicio_memory(self):
        c = self.cpu
        self.evento_memory = dict(tipo='memory', pasos=c.r[4] & ee.M32,
                                  antes=c.r32(self.p['semillap']))
        self.eventos.append(self.evento_memory)

    def _entrar(self, destino):
        c, p = self.cpu, self.p
        if destino == MEMORY:
            self._inicio_memory()
        if destino == p['scheduler']:
            self.eventos.append(dict(tipo='scheduler', args=[c.r[i] & ee.M32 for i in (4, 5)],
                                     antes=pose.huella(c.r32, c.r16, p)))
        return self.entrar_original(destino)

    def _servicio(self, rol):
        def hook(c):
            dst = c.r[4] & ee.M32
            if dst % 16 or not ee.PILA <= dst <= ee.PILA + 0x100000 - 16:
                raise RuntimeError('salida de servicio fuera de pila propia alineada')
            antes = pose.huella(c.r32, c.r16, self.p)
            if rol == 'hueso':
                args = [c.r[i] & ee.M32 for i in (5, 6)]
                if args != [pose.ACTOR, 1]:
                    raise RuntimeError('actor/hueso ajeno al fixture')
                v = vector_hueso(self.variante)
                ev = dict(tipo=rol, args=args, antes=antes, salida_bits=v)
                valor = 0
            else:
                origen = c.r[5] & ee.M32
                if origen % 16 or not ee.PILA <= origen <= ee.PILA + 0x100000 - 16:
                    raise RuntimeError('vector de entrada fuera de pila propia')
                args = [c.r[i] & ee.M32 for i in (6, 7)]
                if args != [base.CAM + 0x80, base.CAM + 0x100]:
                    raise RuntimeError('camara ajena al fixture')
                v, valor = vector_pantalla(self.variante), ee.M32 if self.variante else 0
                ev = dict(tipo=rol, args=args, antes=antes,
                          origen_bits=[c.r32(origen + 4 * i) for i in range(4)],
                          salida_bits=v, retorno=valor)
            for i, b in enumerate(v):
                c.w32(dst + 4 * i, b)
            self.eventos.append(ev)
            c.r[2] = valor
        return hook

    def preparar_plan(self, semilla=1, modo=0, apunta=0, previo=0, variante=0, **kw):
        if (type(semilla) is not int or not 1 <= semilla <= ee.M32
                or any(type(v) is not int for v in (modo, apunta, previo, variante))
                or modo not in MODOS or apunta not in FLAGS or previo not in PREVIOS
                or variante not in (0, 1)):
            raise ValueError('semilla no nula/flags fuera del contrato del planificador')
        ref = self.preparar_pose(**kw)
        c, p = self.cpu, self.p
        ref.pop(p['semillap'])
        ref[p['semillap'] - 16] = bytearray(c.m[p['semillap'] - 16:p['semillap'] + 20])
        for a, v in ((p['globales'] + 12, modo), (p['globales'] + 16, apunta),
                      (p['globales'] + 24, previo), (p['semillap'], semilla)):
            c.w32(a, v); escribir(ref, a, v)
        self.variante = variante
        return ref

    def paso_plan(self, operacion='scheduler', proyectar=0, evitar=0, nulo=False, pasos=32):
        c, p = self.cpu, self.p
        if (not self.preparado or operacion not in ('scheduler', 'pick', 'pose', 'memory')
                or type(proyectar) is not int or proyectar not in FLAGS
                or type(evitar) is not int or evitar not in PREVIOS
                or type(nulo) is not bool or type(pasos) is not int or pasos not in (0, 1, 8, 31, 32, 33)
                or c.r32(p['ctxp']) != base.CTX or c.r32(p['camp']) != base.CAM
                or c.r32(base.CTX + 0x28) > 5 or c.r32(p['pasop']) not in pose.PASOS
                or c.r32(p['globales'] + 8) not in (0, pose.CONTROL)):
            raise ValueError('operacion/contexto fuera del perfil de planificador')
        self.eventos = []
        c.io.clear(); c.sin_puente.clear(); c.llamadas.clear()
        if operacion == 'pick':
            c.f[12] = c.r32(p['globales'] + 4)
            valor = c.call(p['pick'], proyectar, pose.ACTOR, evitar, limite=20000)
        elif operacion == 'memory':
            c.r[4] = pasos
            self._inicio_memory()
            valor = c.call(MEMORY, pasos, limite=20000)
        elif operacion == 'scheduler':
            c.call(p['scheduler'], 0 if nulo else p['globales'] + 12, pose.ACTOR, limite=40000)
        else:
            c.call(p['pose'], pose.ACTOR, limite=50000)
        if c.io or c.sin_puente or any(a not in self.roles for a in c.llamadas):
            raise RuntimeError('E/S o llamada ajena al perfil de planificador')
        obs = dict(pose.huella(c.r32, c.r16, p), eventos=self.eventos.copy(),
                   llamadas={self.roles[a]: n for a, n in c.llamadas.items()})
        if operacion in ('pick', 'memory'):
            obs['retorno'] = valor & ee.M32
        return obs


def modelo(s, ref, operacion='scheduler', proyectar=0, evitar=0, nulo=False, pasos=32):
    r, p = {a: bytearray(b) for a, b in ref.items()}, s.p
    calls, eventos = Counter(), []
    g = p['globales']
    L, W = lambda a: leer(r, a), lambda a, v: escribir(r, a, v)
    def azar(n=32, raiz=False):
        antes = L(p['semillap'])
        despues, valor = marcas.avanzar(antes, n)
        W(p['semillap'], despues)
        eventos.append(dict(tipo='memory', pasos=n, antes=antes, despues=despues, valor=valor))
        calls['memory'] += int(not raiz)
        return valor
    def extender():
        calls.update(extender=1, desempaquetar_float=1, empaquetar_double=1, empaquetar_double_cuerpo=1)
    def seleccionar(proy, anterior, raiz=False):
        calls['pick'] += int(not raiz)
        azar()  # Tirada inicial descartada, tambien sin proyeccion.
        if proy:
            calls.update(hueso=1, camget=2, proyeccion=1)
            antes = huella(r, p)
            eventos.append(dict(tipo='hueso', args=[pose.ACTOR, 1], antes=antes,
                                 salida_bits=vector_hueso(s.variante)))
            eventos.append(dict(tipo='proyeccion', args=[base.CAM + 0x80, base.CAM + 0x100],
                                 antes=antes, origen_bits=vector_hueso(s.variante),
                                 salida_bits=vector_pantalla(s.variante), retorno=ee.M32 if s.variante else 0))
        for _ in range(64):
            rumbo = azar() & 3
            if rumbo != anterior:
                break
        else:
            raise ValueError('selector no termina dentro del dominio de64 tiradas')
        if rumbo in (2, 3):
            extender()
            calls.update(menor_double=1, comparar_double=1, comparar_double_cuerpo=1,
                         desempaquetar_double=2)
            elev, limite = arit.racional(L(g + 4)), arit.racional(LIMITE_SELECTOR)
            bloqueado = elev > limite if rumbo == 2 else elev < -limite
            if bloqueado:
                prohibido = rumbo
                for _ in range(64):
                    rumbo = azar() & 3
                    if rumbo != prohibido:
                        break  # NO vuelve a excluir la direccion anterior.
                else:
                    raise ValueError('selector angular no termina dentro de64 tiradas')
        return rumbo
    def planificar():
        modo, flag = L(g + 12), L(g + 16)
        if modo in (0, 1):
            calls['parambc'] += 1
            producto = arit.producto(L(p['pasop']), L(p['maximos'] + 4 + 16 * L(base.CTX + 0x28)), cop1=True)
            if modo == 1:
                extender()
                calls.update(dividir_double=1, dividir_double_cuerpo=1,
                             desempaquetar_double=3, empaquetar_double_cuerpo=1,
                             truncar=1, empaquetar_float=1, empaquetar_float_cuerpo=1)
                producto = arit.truncar(arit.racional(producto) * Fraction(2, 3))
            W(g + 20, producto)
            if modo == 0:
                W(g + 32, 0)
                W(g + 36, azar() % 10)
            else:
                azar()  # El calculo aleatorio %120 se SOBRESCRIBE con120.0.
                W(g + 32, bits(120))
                W(g + 36, azar() % 5)
        if flag in (0, 1):
            W(g + 28, L(g + 24))
            W(g + 24, seleccionar(flag, L(g + 24)))
            if flag == 1:
                W(g + 36, 1)
    if operacion == 'pose':
        def callback(rr, cc, ev, args):
            nonlocal r, calls, eventos
            r, calls, eventos = rr, cc, ev
            planificar()
        return pose.modelo(s, r, planificador=callback)
    if operacion == 'scheduler':
        if not nulo:
            planificar()
    elif operacion == 'pick':
        valor = seleccionar(proyectar, evitar, raiz=True)
    elif operacion == 'memory':
        valor = azar(pasos, raiz=True)
    else:
        raise ValueError('operacion esperada fuera del perfil')
    obs = dict(huella(r, p), eventos=eventos, llamadas={k: n for k, n in calls.items() if n})
    if operacion in ('pick', 'memory'):
        obs['retorno'] = valor
    return r, obs


def comprobar(region, elf):
    s = PlanificadorViewer(region, elf)
    filas, muestras, continuos, cobertura = [], {}, [], Counter()
    seleccionados, tiradas, operaciones = Counter(), Counter(), Counter()
    def revisar(nombre, want, got):
        f = dict(caso=nombre, igual=want == got)
        if not f['igual']:
            f.update(esperado=want, obtenido=got)
            if all(x['igual'] for x in filas): print('Primer DIF', region, f)
        filas.append(f)
    def ejecutar(nombre, ref, operacion='scheduler', **kw):
        ref, want = modelo(s, ref, operacion, **kw)
        got = s.paso_plan(operacion, **kw)
        revisar(nombre, want, got)
        revisar(nombre + ':memoria_abi_guardias', True, s.integridad(ref))
        muestras.setdefault(nombre.split(':')[0], dict(caso=nombre, esperado=want, obtenido=got))
        cobertura.update(got['llamadas'])
        operaciones[operacion] += 1
        if operacion == 'pick':
            seleccionados[got['retorno']] += 1
        tiradas[len([e for e in got['eventos'] if e['tipo'] == 'memory'])] += 1
        return ref
    aislados = 0
    for semilla, elev, previo, proy in itertools.product(SEMILLAS, ELEVACIONES, PREVIOS, (0, 1)):
        ref = s.preparar_plan(semilla, elevacion=elev, variante=proy)
        ejecutar(f'pick:{semilla}:{elev}:{previo}:{proy}', ref, 'pick', proyectar=proy, evitar=previo)
        aislados += 1
    # El segundo rechazo (por angulo) solo excluye 2 o3, no el previo.
    for semilla, previo, elev in ((1, 0, bits(1)), (2, 1, bits(1)), (64, 2, bits(-1))):
        ref = s.preparar_plan(semilla, elevacion=elev)
        ejecutar(f'repite_previo_tras_angulo:{semilla}:{previo}', ref, 'pick', evitar=previo)
        revisar(f'repite_previo_tras_angulo:{semilla}:retorno', previo, s.cpu.r[2] & ee.M32)
    for proy in FLAGS:
        par = []
        for variante in (0, 1):
            ref = s.preparar_plan(0x12345678, variante=variante)
            ref = ejecutar(f'proyeccion_ignorada:{proy}:{variante}', ref, 'pick', proyectar=proy)
            par.append((s.cpu.r[2] & ee.M32, s.cpu.r32(s.p['semillap'])))
        revisar(f'proyeccion_ignorada:{proy}:mismo_resultado_y_semilla', par[0], par[1])
    for semilla, pasos in itertools.product((0, 1, 0x80000000, ee.M32), (0, 1, 8, 31, 32, 33)):
        ref = s.preparar_plan(max(1, semilla))
        # Test directo deliberado del punto fijo, fuera del preparador normal.
        s.cpu.w32(s.p['semillap'], semilla); escribir(ref, s.p['semillap'], semilla)
        ejecutar(f'memory_fronteras:{semilla}:{pasos}', ref, 'memory', pasos=pasos)
    for d, paso, modo, apunta, previo in itertools.product(range(6), pose.PASOS, MODOS, FLAGS, PREVIOS):
        semilla = SEMILLAS[(d + modo + apunta + previo) % len(SEMILLAS)]
        ref = s.preparar_plan(semilla, modo, apunta, previo, dificultad=d, paso=paso)
        for n in range(2):
            ref = ejecutar(f'scheduler:{d}:{paso}:{modo}:{apunta}:{previo}:{n}', ref)
            aislados += 1
    for d, paso, estado, geom, armada, fin in itertools.product(
            range(6), pose.PASOS, (1, 2), (pose.GEOMETRIAS[0], pose.GEOMETRIAS[3], pose.GEOMETRIAS[1]),
            (0, 1), (0, 1)):
        ref = s.preparar_plan(0x12345678, dificultad=d, paso=paso, estado=estado,
                             geometria=geom, armada=armada, fin_animacion=fin)
        for n in range(3):
            ref = ejecutar(f'consumidor:{d}:{paso}:{estado}:{geom}:{armada}:{fin}:{n}', ref, 'pose')
            aislados += 1
    for d, paso, semilla, variante in itertools.product(range(6), pose.PASOS, (1, 0x80000000), (0, 1)):
        ref = s.preparar_plan(semilla, modo=1, apunta=1, dificultad=d, paso=paso, variante=variante,
                             estado=1, progreso=bits(300), elevacion=LIMITE_SELECTOR + 1)
        for n in range(120):
            if n % 11 == 0:
                ref = ejecutar(f'continuo:{d}:{paso}:{semilla}:{variante}:{n}:otro_memory', ref,
                               'memory', pasos=(1, 8, 32)[n // 11 % 3])
            # Operaciones externas prefijadas, no se decide el orden desde got.
            ref = ejecutar(f'continuo:{d}:{paso}:{semilla}:{variante}:{n}', ref,
                           'scheduler' if n % 3 == 0 else 'pose')
        continuos.append(dict(dificultad=d, paso_bits=paso, semilla_inicial=semilla, variante=variante,
                              cuadros=120, memory_intercalados=11,
                              semilla_final_esperada=leer(ref, s.p['semillap']),
                              semilla_final_obtenida=s.cpu.r32(s.p['semillap'])))
    for modo, apunta in itertools.product(MODOS, FLAGS):
        ref = s.preparar_plan(modo=modo, apunta=apunta)
        ejecutar(f'nulo:{modo}:{apunta}', ref, nulo=True)
    for nombre, kw in (('semilla0', dict(semilla=0)), ('semilla_bool', dict(semilla=True)),
                       ('semilla_fuera', dict(semilla=1 << 32)), ('modo_bool', dict(modo=True)),
                       ('flag_fuera', dict(apunta=3)), ('previo_fuera', dict(previo=4)),
                       ('elevacion_nan', dict(elevacion=0x7FC00000)), ('variante_fuera', dict(variante=2))):
        ref = s.preparar_plan()
        rechazo = False
        try: s.preparar_plan(**kw)
        except ValueError: rechazo = True
        revisar('rechazo:' + nombre, True, rechazo)
        revisar('rechazo:' + nombre + ':sin_mutacion', True, s.integridad(ref))
    # Semilla nula es un punto fijo LFSR: dos bucles reales acotados por el
    # interprete, no inventar salida ni permitir que cuelgue el banco.
    bucles = []
    for nombre, previo, elev in (('repite3', 3, 0), ('angular3', 0, bits(-1))):
        s.preparar_plan(elevacion=elev)
        s.cpu.w32(s.p['semillap'], 0)
        c = s.cpu
        c.f[12] = elev
        c.io.clear(); c.sin_puente.clear(); c.llamadas.clear(); s.eventos = []
        limitado = False
        try: c.call(s.p['pick'], 0, pose.ACTOR, previo, limite=1000)
        except RuntimeError as exc:
            mensaje = str(exc)
            limitado = mensaje.startswith('limite de instrucciones en ')
        revisar('bucle_semilla0:' + nombre, True,
                limitado and not c.io and not c.sin_puente and c.r32(s.p['semillap']) == 0)
        bucles.append(dict(caso=nombre, limite_instrucciones=1000, limitado=limitado,
                           mensaje=mensaje if limitado else None,
                           memory_completos=sum('valor' in ev for ev in s.eventos),
                           valores_completos=sorted({ev['valor'] for ev in s.eventos if 'valor' in ev}),
                           semilla_final=c.r32(s.p['semillap'])))
    ref = s.preparar_plan()
    s.cpu.w32(s.p['semillap'], 0); escribir(ref, s.p['semillap'], 0)
    ejecutar('semilla0_salida3_sin_rechazo', ref, 'pick', evitar=0)
    for nombre, raiz, off, original, nuevo, kw, op, args in (
            ('contador120_a119', 'scheduler', 0xD8, 0x3C0242F0, 0x3C0242EE, dict(modo=1), 'scheduler', {}),
            ('memory32_a31', 'pick', 0x2C, 0x24040020, 0x2404001F, {}, 'pick', {}),
            ('no_guardar_previo', 'scheduler', 0x110, 0xAE260010, 0, dict(previo=2), 'scheduler', {})):
        addr = s.p[raiz] + off
        revisar('sensibilidad:' + nombre + ':instruccion', original, s.cpu.r32(addr))
        if s.cpu.r32(addr) != original:
            raise ValueError('instruccion de sensibilidad fuera del perfil')
        ref = s.preparar_plan(**kw)
        expected, want = modelo(s, ref, op, **args)
        s.cpu.w32(addr, nuevo); s.cpu.code.pop(addr, None)
        try:
            got = s.paso_plan(op, **args)
            revisar('sensibilidad:' + nombre, True, got != want or not s.integridad(expected))
        finally:
            s.cpu.w32(addr, original); s.cpu.code.pop(addr, None)
        ref = s.preparar_plan(**kw); ejecutar('restaurado:' + nombre, ref, op, **args)
    for (a, b), h in zip(s.intervalos, s.hashes):
        revisar(f'codigo_intacto:{a:08x}:{b:08x}', h, base.huella(s.cpu.m[a:b]))
    primera = next((f for f in filas if not f['igual']), None)
    iguales = sum(f['igual'] for f in filas)
    print(f'Planificador Viewer {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return dict(region=region, elf_sha256=s.digest, perfil=s.p, iguales=iguales, total=len(filas),
                primera_diferencia=primera, correcto=primera is None, llamadas_aisladas=aislados,
                continuos=continuos, semillas=list(SEMILLAS), cobertura_llamadas=dict(cobertura),
                direcciones_obtenidas=dict(seleccionados), histograma_tiradas=dict(tiradas),
                operaciones=dict(operaciones), bucles_semilla0=bucles, muestras=muestras, casos=filas,
                intervalos_codigo=[dict(inicio=a, fin_exclusivo=b, sha256=h)
                                   for (a, b), h in zip(s.intervalos, s.hashes)])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    inicio = time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump(dict(operacion='viewer_planificador_azar_consumidor', pares_dorado_real=0,
                           integrado_cpp=False, referencia_continua='estado propio, sin realimentar got',
                           limites=['hueso/proyeccion artificiales; huesos/audio solo peticiones',
                                    'camara/progreso/fin/direccion controlados; orden de llamadas del banco, no scheduler del motor',
                                    'semilla0 rechazada en contrato, dos bucles nativos limitados observados',
                                    'sin Input/Update, orbita, colision, hardware ni integracion C++'],
                           regiones=regiones, segundos_banco=round(time.monotonic() - inicio, 3)),
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

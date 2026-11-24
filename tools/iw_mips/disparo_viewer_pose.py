# SPDX-License-Identifier: MIT
"""F1: consumidor de pose7D8 EU/USA, geometria y entradas1/2 originales.

El planificador aleatorio se observa como peticion sin efectos: no se ejecuta.
Camara, progreso, fin de animacion y entrada de direccion son fixtures. No
certifica esquiva completa, hardware, colision, clips/audio ni integracion C++.
"""
import argparse
from collections import Counter
import itertools
import json
import math
from pathlib import Path
import struct
import time

import disparo_viewer_resultado as base
import disparo_viewer_agotamiento as ag
import ee

arit, banco = base.arit, base.banco
bits, leer, escribir = base.bits, base.leer, base.escribir
ACTOR, CONTROL = 0x1810000, 0x1813000
TAM_ACTOR = 0x2100
PERFILES = {
    'eu': dict(pose=0x32DC80, pose1=0x32D420, pose2=0x32D2C0,
               vista=0x2ED7E0, camget=0x2ED7D0, bajo25=0x326C40, bajo40=0x326C90,
               scheduler=0x32D6B0, parambc=0x326CE0, huesos=0x238670, sonido=0x240740,
               globales=0x5D9940, despacho=0x499BF0, direcciones=0x499BC0),
    'us': dict(pose=0x32D950, pose1=0x32D0F0, pose2=0x32CF90,
               vista=0x2ED5D0, camget=0x2ED5C0, bajo25=0x326930, bajo40=0x326980,
               scheduler=0x32D380, parambc=0x3269D0, huesos=0x2384E0, sonido=0x2405B0,
               globales=0x5D9140, despacho=0x4993F0, direcciones=0x4993C0)}
PASOS = tuple(map(bits, (.5, 1, 1.2, 2)))
GEOMETRIAS = ((0, 0, 64), (0, 0, -64), (64, 0, 0), (30, 0, 64),
              (31, 0, 64), (32, 0, 64), (110, 0, 64), (111, 0, 64),
              (112, 0, 64), (0, 64, 64), (64, 32, 16))
# Vecinos float32 de los angulos que producen dot=.5/.9/.95. libm solo
# propone la geometria: el dot esperado se calcula despues racionalmente.
GEOMETRIAS += tuple((struct.unpack('<f', struct.pack('<I', bits(math.sqrt(1 / t**2 - 1) * 64) + k))[0], 0, 64)
                   for t in (.5, .9, .95) for k in range(-2, 3))
ANGULO_LIMITE = 0x3F490FDB


def escribir_h(ref, addr, valor, formato='<H'):
    n = struct.calcsize(formato)
    for a, b in ref.items():
        if a <= addr and addr + n <= a + len(b):
            struct.pack_into(formato, b, addr - a, valor)
            return
    raise ValueError('direccion ajena a referencia de pose')


def leer_h(ref, addr):
    for a, b in ref.items():
        if a <= addr and addr + 2 <= a + len(b):
            return struct.unpack_from('<H', b, addr - a)[0]
    raise ValueError('halfword ajeno a referencia de pose')


def huella(lectura, hlectura, p):
    return dict(globales=[lectura(p['globales'] + 4 * i) for i in range(10)],
                semilla=lectura(p['semillap']),
                estado=hlectura(ACTOR + 0xCBC), anterior=hlectura(ACTOR + 0xCBE),
                repeticiones=hlectura(ACTOR + 0xCC0), extra=hlectura(ACTOR + 0xCC4),
                cc6=hlectura(ACTOR + 0xCC6), clip=lectura(ACTOR + 0xB60),
                anim=[lectura(ACTOR + off) for off in (0xCB4, 0xCB8, 0xCC8)],
                byte206f=(lectura(ACTOR + 0x206C) >> 24) & 255)


def punto_direccion(ref):
    ojo = [leer(ref, base.CAM + 0x80 + 4 * i) for i in range(3)]
    obj = [leer(ref, base.CAM + 0x90 + 4 * i) for i in range(3)]
    pos = [leer(ref, ACTOR + 0x40 + 4 * i) for i in range(3)]
    a = arit.escalar(arit.diferencia(obj, ojo), bits(1))
    b = arit.escalar(arit.diferencia(pos, ojo), bits(1))
    productos = [arit.producto(x, y) for x, y in zip(a, b)]
    return arit.suma(arit.suma(productos[0], productos[1]), productos[2])


class PoseViewer(base.ResultadoViewer):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        if ee.VU_CERCANO or ee.FPU_SUMA_PS2:
            raise ValueError('se requiere perfil VU y suma COP1 de serie')
        self.p = p = dict(self.p, **PERFILES[region])
        c = self.cpu
        p['maximos'] = p['factor'] - 8
        p['pasop'] = c.gp - 0x7614
        p['semillap'] = c.gp - 0x7FE8
        js = (p['vista'], p['camget'], 0x142540, 0x142540, 0x1425A0,
              p['bajo25'], p['bajo40'], p['scheduler'], p['scheduler'],
              p['scheduler'], p['parambc'], 0x11A6B0, 0x11A6B0)
        if base.llamadas(c.m, p['pose'], p['pose'] + 0x550) != js:
            raise ValueError('JAL de consumidor pose fuera de perfil regional')
        if (base.llamadas(c.m, p['pose1'], p['pose1'] + 0x70) != (p['huesos'],)
                or base.llamadas(c.m, p['pose2'], p['pose2'] + 0x150) != (p['huesos'], p['sonido'])
                or [c.r32(p['despacho'] + off) for off in (4, 8)] != [p['pose1'], p['pose2']]):
            raise ValueError('despacho/entradas pose fuera del perfil')
        self.roles = {p[k]: k for k in ('vista', 'camget', 'bajo25', 'bajo40',
                                       'scheduler', 'parambc', 'huesos', 'sonido', 'pose1', 'pose2')}
        self.roles.update({0x142540: 'normalizar', 0x1425A0: 'dot', 0x11A6B0: 'truncar',
                           0x119CC8: 'desempaquetar_double', 0x119B80: 'empaquetar_float',
                           0x119A70: 'empaquetar_float_cuerpo'})
        for a, n in ((p['pose'], 0x550), (p['pose1'], 0x70), (p['pose2'], 0x150),
                     (p['vista'], 0x30), (p['camget'], 0x10), (p['bajo25'], 0x50),
                     (p['bajo40'], 0x50), (p['parambc'], 0x30), (0x142540, 0x30),
                     (0x1425A0, 0x20), (0x11A6B0, 0x58), (0x119CC8, 0x90),
                     (0x119B80, 0x30), (0x119A70, 0xB8)):
            c.whitelist.add(a); self.intervalos.append((a, a + n))
        for rol in ('scheduler', 'huesos', 'sonido'):
            c.hooks[p[rol]] = self._peticion(rol)
        self.hashes = [base.huella(c.m[a:b]) for a, b in self.intervalos]
        self.datos_originales = {a: bytes(c.m[a:a + n]) for a, n in (
            (p['maximos'] - 0x40, 0xA0), (p['despacho'], 16), (p['direcciones'], 32))}
        if [c.r32(p['maximos'] - off) for off in (0x18, 0x40)] != [bits(.25), bits(.4)]:
            raise ValueError('umbrales de pose distintos tras init original')

    def _peticion(self, rol):
        def hook(c):
            args = [c.r[i] & ee.M32 for i in range(4, 8 if rol == 'sonido' else 6 if rol == 'scheduler' else 5)]
            ev = dict(tipo=rol, args=args, antes=huella(c.r32, c.r16, self.p))
            if rol == 'sonido':
                ev.update(f12=c.f[12], f13=c.f[13])
            self.eventos.append(ev)
            c.r[2] = 0
        return hook

    def preparar_pose(self, dificultad=0, progreso=bits(900), estado=2, rumbo=0,
                      velocidad=bits(.5), contador=bits(40), elevacion=0, azimut=0,
                      direccion=0, control=True, fin_animacion=0, armada=0,
                      geometria=(0, 0, 64), paso=bits(1), repeticiones=0x7FFF):
        if (type(dificultad) is not int or not 0 <= dificultad <= 5
                or type(estado) is not int or estado not in (0, 1, 2, 3, 0xFFFF)
                or type(rumbo) is not int or not 0 <= rumbo <= 4
                or type(direccion) is not int or not 0 <= direccion <= 3
                or type(control) is not bool or type(fin_animacion) is not int or fin_animacion not in (0, 1, ee.M32)
                or type(armada) is not int or armada not in (0, 1, 2, ee.M32)
                or type(repeticiones) is not int or not 0 <= repeticiones <= 0xFFFF
                or type(paso) is not int or paso not in PASOS
                or type(geometria) is not tuple or geometria not in GEOMETRIAS
                or any(not base.finito(v) or abs(arit.racional(v)) > limite
                       for v, limite in ((progreso, 2400), (velocidad, 4), (contador, 140),
                                         (elevacion, 4), (azimut, 4)))):
            raise ValueError('entrada fuera del fixture pose revisado')
        ref = self.preparar(dificultad, 0)
        c, p = self.cpu, self.p
        for a, n in ((ACTOR, TAM_ACTOR), (CONTROL, 0x10), (p['globales'], 40)):
            c.m[a - 16:a + n + 16] = bytes([0xA5]) * (n + 32)
            ref[a - 16] = bytearray(c.m[a - 16:a + n + 16])
        for a, b in self.datos_originales.items():
            ref[a] = bytearray(b)
        ref[p['pasop']] = bytearray(4)
        ref[p['semillap']] = bytearray(4)
        for a, v in ((base.CTX + 4, progreso), (ACTOR + 0xC74, fin_animacion),
                     (p['pasop'], paso), (p['semillap'], 0x12345678)):
            c.w32(a, v); escribir(ref, a, v)
        glob = (azimut, elevacion, CONTROL if control else 0, 0, 0, velocidad,
                rumbo, 3, contador, armada)
        for i, v in enumerate(glob):
            c.w32(p['globales'] + 4 * i, v); escribir(ref, p['globales'] + 4 * i, v)
        for a, v in ((ACTOR + 0xCBC, estado), (ACTOR + 0xCC0, repeticiones)):
            c.w16(a, v); escribir_h(ref, a, v)
        c.m[CONTROL + 1] = direccion; escribir_h(ref, CONTROL + 1, direccion, '<B')
        for a, xyz in ((base.CAM + 0x80, (0, 0, 0)), (base.CAM + 0x90, (0, 0, 1)),
                       (ACTOR + 0x40, geometria)):
            for i, v in enumerate((*xyz, 1)):
                c.w32(a + 4 * i, bits(v)); escribir(ref, a + 4 * i, bits(v))
        return ref

    def paso_pose(self, operacion='pose'):
        c, p = self.cpu, self.p
        if not self.preparado or operacion not in ('pose', 'pose1', 'pose2'):
            raise ValueError('operacion de pose fuera del perfil')
        if (c.r32(p['ctxp']) != base.CTX or c.r32(p['camp']) != base.CAM
                or c.r32(base.CTX + 0x28) > 5 or c.r32(p['pasop']) not in PASOS
                or c.r32(p['globales'] + 8) not in (0, CONTROL)):
            raise ValueError('contexto de pose alterado')
        self.eventos = []
        c.io.clear(); c.sin_puente.clear(); c.llamadas.clear()
        c.call(p[operacion], ACTOR, limite=12000)
        if c.io or c.sin_puente or any(a not in self.roles for a in c.llamadas):
            raise RuntimeError('E/S o llamada ajena al perfil de pose')
        return dict(huella(c.r32, c.r16, p), eventos=self.eventos.copy(),
                    llamadas=dict(Counter({self.roles[a]: n for a, n in c.llamadas.items()})))


def modelo(s, ref, operacion='pose', planificador=None):
    r, p = {a: bytearray(b) for a, b in ref.items()}, s.p
    g = p['globales']
    calls, eventos = Counter(), []
    L, W = lambda a: leer(r, a), lambda a, v: escribir(r, a, v)
    def evento(rol, args, **kw):
        eventos.append(dict(tipo=rol, args=args, antes=huella(L, lambda a: leer_h(r, a), p), **kw))
        calls[rol] += 1
        if rol == 'scheduler' and planificador is not None:
            planificador(r, calls, eventos, args)
    def pose(n):
        calls['pose' + str(n)] += int(operacion == 'pose')
        previo = leer_h(r, ACTOR + 0xCBC)
        escribir_h(r, ACTOR + 0xCBE, previo)
        escribir_h(r, ACTOR + 0xCBC, n)
        rep = (leer_h(r, ACTOR + 0xCC0) + 1) & 0xFFFF if previo == n else 0
        escribir_h(r, ACTOR + 0xCC0, rep)
        if n == 1:
            W(ACTOR + 0xB60, 1000)
            evento('huesos', [ACTOR])
            W(ACTOR + 0xCB4, 0); W(ACTOR + 0xCB8, bits(.25)); W(ACTOR + 0xCC8, 0)
        else:
            escribir_h(r, ACTOR + 0xCC4, 0)
            if L(g + 24) < 4:
                W(ACTOR + 0xB60, 1010 + L(g + 24))
            rapido = arit.racional(L(g + 32)) > 35
            if rapido:
                W(ACTOR + 0xB60, (L(ACTOR + 0xB60) + 4) & ee.M32)
                escribir_h(r, ACTOR + 0xCC4, 1)
            evento('huesos', [ACTOR])
            W(ACTOR + 0xCB4, 0 if rapido else bits(1))
            W(ACTOR + 0xCB8, bits(.05) if rapido else bits(.25)); W(ACTOR + 0xCC8, 0)
            evento('sonido', [1, 60, 0, 0], f12=0, f13=bits(1))
    def transicion(n):
        escribir_h(r, ACTOR + 0xCC6, 0)
        escribir_h(r, ACTOR + 0x206F, 0, '<B')
        pose(n)
    if operacion != 'pose':
        pose(1 if operacion == 'pose1' else 2)
    else:
        calls.update(vista=1, camget=1, normalizar=2, dot=1, bajo25=1)
        dot = arit.racional(punto_direccion(r))
        W(g + 16, int(dot > arit.racional(bits(.9))))
        ratio = arit.racional(ag.dividir(L(base.CTX + 4), L(p['maximos'] + 16 * L(base.CTX + 0x28))))
        if ratio >= arit.racional(L(p['maximos'] - 0x18)):
            calls['bajo40'] += 1
            W(g + 12, int(ratio < arit.racional(L(p['maximos'] - 0x40))))
            estado = leer_h(r, ACTOR + 0xCBC)
            du, dv, mover = 0, 0, False
            if estado == 1:
                if L(g + 36):
                    if dot >= arit.racional(bits(.5)):
                        evento('scheduler', [g + 12, ACTOR]); transicion(2)
                elif L(ACTOR + 0xC74):
                    evento('scheduler', [g + 12, ACTOR])
                elif dot > arit.racional(bits(.95)):
                    evento('scheduler', [g + 12, ACTOR]); W(g + 36, 1)
            elif estado == 2:
                W(g + 32, arit.suma(L(g + 32), L(p['pasop']), cop1=True, resta=True))
                elev = arit.racional(L(g + 4))
                if elev < -arit.racional(ANGULO_LIMITE) or elev > arit.racional(ANGULO_LIMITE):
                    W(g + 4, ANGULO_LIMITE | (0x80000000 if elev < 0 else 0)); transicion(1)
                elif dot < arit.racional(bits(.5)):
                    transicion(1)
                elif L(g + 12) == 0 and L(ACTOR + 0xC74) != 0:
                    transicion(1)
                elif L(g + 12) != 0 and arit.racional(L(g + 32)) < 0:
                    transicion(1)
                else:
                    mover = True
                    rumbo, vel = L(g + 24), L(g + 20)
                    if rumbo in (0, 1): du = vel ^ (0x80000000 if rumbo == 1 else 0)
                    if rumbo in (2, 3): dv = vel ^ (0x80000000 if rumbo == 3 else 0)
                    if L(g + 12) == 0:
                        calls['parambc'] += 1
                        bc = L(p['maximos'] + 4 + 16 * L(base.CTX + 0x28))
                        dec = ag.dividir(arit.producto(L(p['pasop']), bc, cop1=True), bits(70))
                        W(g + 20, arit.suma(vel, dec, cop1=True, resta=True))
            elif estado == 3 and L(g + 8):
                idx = (L(CONTROL) >> 8) & 255
                du, dv = L(p['direcciones'] + 8 * idx), L(p['direcciones'] + 8 * idx + 4)
                posible = arit.suma(L(g + 4), dv, cop1=True)
                if abs(arit.racional(posible)) > arit.racional(ANGULO_LIMITE):
                    calls['truncar'] += 1
                    calls['desempaquetar_double'] += 1
                    calls['empaquetar_float'] += 1
                    calls['empaquetar_float_cuerpo'] += 1
                    du = arit.producto(du, bits(4 if L(g + 12) == 1 else 2), cop1=True)
                    dv = 0
                mover = True
            if mover:
                W(g, arit.suma(L(g), du, cop1=True))
                W(g + 4, arit.suma(L(g + 4), dv, cop1=True))
    return r, dict(huella(L, lambda a: leer_h(r, a), p), eventos=eventos,
                   llamadas={k: n for k, n in calls.items() if n})


def comprobar(region, elf):
    s, filas, muestras, continuos, cobertura = PoseViewer(region, elf), [], {}, [], Counter()
    def revisar(nombre, esperado, obtenido):
        f = dict(caso=nombre, igual=esperado == obtenido)
        if not f['igual']:
            f.update(esperado=esperado, obtenido=obtenido)
            if all(x['igual'] for x in filas): print('Primer DIF', region, nombre)
        filas.append(f)
    def ejecutar(nombre, ref, operacion='pose'):
        ref, want = modelo(s, ref, operacion)
        got = s.paso_pose(operacion)
        revisar(nombre, want, got)
        revisar(nombre + ':memoria_abi_guardias', True, s.integridad(ref))
        muestras.setdefault(nombre.split(':')[0], dict(caso=nombre, esperado=want, obtenido=got))
        cobertura.update(got['llamadas'])
        return ref
    aislados = 0
    for d, maximo in enumerate((900, 950, 1000, 1050, 1100, 1200)):
        ps = {0, bits(-1), bits(maximo)}
        for f in (.25, .4):
            b = bits(maximo * f); ps.update((b - 1, b, b + 1))
        for progreso, estado, geom in itertools.product(sorted(ps), (0, 1, 2, 3, 0xFFFF), GEOMETRIAS):
            ref = s.preparar_pose(d, progreso, estado=estado, geometria=geom)
            for n in range(2):
                ref = ejecutar(f'fronteras:{d}:{progreso:08x}:{estado}:{geom}:{n}', ref)
                aislados += 1
    for estado, rumbo, contador, elevacion, fin, armada in itertools.product(
            (1, 2), range(5), (bits(-1), 0, bits(1), bits(35), bits(35) + 1),
            (ANGULO_LIMITE | 0x80000000, (ANGULO_LIMITE + 1) | 0x80000000,
             0, ANGULO_LIMITE, ANGULO_LIMITE + 1), (0, 1), (0, 1)):
        ref = s.preparar_pose(0, bits(500), estado, rumbo, contador=contador,
                             elevacion=elevacion, fin_animacion=fin, armada=armada)
        ejecutar(f'estados:{estado}:{rumbo}:{contador}:{elevacion}:{fin}:{armada}', ref)
        aislados += 1
    for op, estado, rumbo, contador, rep in itertools.product(
            ('pose1', 'pose2'), (1, 2, 0xFFFF), range(5),
            (bits(35) - 1, bits(35), bits(35) + 1), (0, 0x7FFF, 0xFFFF)):
        ref = s.preparar_pose(estado=estado, rumbo=rumbo, contador=contador, repeticiones=rep)
        for n in range(2):
            ref = ejecutar(f'entradas:{op}:{estado}:{rumbo}:{contador}:{rep}:{n}', ref, op)
            aislados += 1
    for progreso, direccion, control, elevacion in itertools.product(
            (bits(300), bits(500), 0), range(4), (False, True),
            (ANGULO_LIMITE | 0x80000000, (ANGULO_LIMITE + 1) | 0x80000000,
             0, ANGULO_LIMITE, ANGULO_LIMITE + 1)):
        ref = s.preparar_pose(progreso=progreso, estado=3, direccion=direccion,
                             control=control, elevacion=elevacion)
        ejecutar(f'control:{progreso}:{direccion}:{control}:{elevacion}', ref)
        aislados += 1
    for armada, fin, geom in itertools.product((2, ee.M32), (0, ee.M32), (GEOMETRIAS[0], GEOMETRIAS[3])):
        ref = s.preparar_pose(estado=1, armada=armada, fin_animacion=fin, geometria=geom)
        ejecutar(f'flags_no_booleanas:{armada}:{fin}:{geom}', ref)
        aislados += 1
    for d, paso, estado, rumbo in itertools.product(range(6), PASOS, (2, 3), range(4)):
        ref = s.preparar_pose(d, bits(300), estado=estado, rumbo=rumbo, direccion=rumbo, paso=paso)
        for n in range(100):
            ref = ejecutar(f'continuo:{d}:{paso}:{estado}:{rumbo}:{n}', ref)
        continuos.append(dict(dificultad=d, paso_bits=paso, estado_inicial=estado, rumbo=rumbo, cuadros=100))
    for nombre, kw in (('dificultad_bool', dict(dificultad=True)), ('progreso_nan', dict(progreso=0x7FC00000)),
                       ('estado_bool', dict(estado=True)), ('rumbo_negativo', dict(rumbo=-1)),
                       ('direccion_fuera', dict(direccion=4)), ('control_entero', dict(control=1)),
                       ('paso3', dict(paso=bits(3))), ('geometria_nula', dict(geometria=(0, 0, 0)))):
        ref = s.preparar_pose()
        rechazo = False
        try: s.preparar_pose(**kw)
        except ValueError: rechazo = True
        revisar('rechazo:' + nombre, True, rechazo)
        revisar('rechazo:' + nombre + ':sin_mutacion', True, s.integridad(ref))
    for nombre, raiz, off, original, nuevo, kw in (
            ('no_decrementar_contador', 'pose', 0x1F8,
             0xE4820000 | ((s.p['globales'] + 32) & 0xFFFF), 0, dict(progreso=bits(300))),
            ('clip1000_a999', 'pose1', 0x3C, 0x240203E8, 0x240203E7, dict(estado=1)),
            ('contador35_lt_a_le', 'pose2', 0xAC, 0x46000834, 0x46000836, dict(contador=bits(35)))):
        addr = s.p[raiz] + off
        revisar('sensibilidad:' + nombre + ':instruccion', original, s.cpu.r32(addr))
        if s.cpu.r32(addr) != original:
            raise ValueError('instruccion de sensibilidad fuera del perfil regional')
        ref = s.preparar_pose(**kw)
        op = raiz
        expected, want = modelo(s, ref, op)
        s.cpu.w32(addr, nuevo); s.cpu.code.pop(addr, None)
        try:
            got = s.paso_pose(op)
            revisar('sensibilidad:' + nombre, True, got != want or not s.integridad(expected))
        finally:
            s.cpu.w32(addr, original); s.cpu.code.pop(addr, None)
        ref = s.preparar_pose(**kw); ejecutar('restaurado:' + nombre, ref, op)
    for (a, b), h in zip(s.intervalos, s.hashes):
        revisar(f'codigo_intacto:{a:08x}:{b:08x}', h, base.huella(s.cpu.m[a:b]))
    primera = next((f for f in filas if not f['igual']), None)
    iguales = sum(f['igual'] for f in filas)
    print(f'Pose Viewer {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return dict(region=region, elf_sha256=s.digest, perfil=s.p, iguales=iguales, total=len(filas),
                primera_diferencia=primera, correcto=primera is None, llamadas_aisladas=aislados,
                continuos=continuos, cobertura_llamadas=dict(cobertura), muestras=muestras, casos=filas,
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
            json.dump(dict(operacion='viewer_pose_consumidor', pares_dorado_real=0, integrado_cpp=False,
                           referencia_continua='estado esperado propio, sin realimentar salidas observadas',
                           limites=['planificador aleatorio solo peticion, sin efectos ni consumo de semilla',
                                    'camara/progreso/fin animacion/direccion son fixtures; sin colision ni reloj de mision',
                                    'entradas1/2 y geometria originales; huesos/audio solo peticiones',
                                    'perfil aritmetico EE existente; no certifica hardware ni C++'],
                           regiones=regiones, segundos_banco=round(time.monotonic() - inicio, 3)),
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

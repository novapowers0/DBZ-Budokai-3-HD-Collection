# SPDX-License-Identifier: MIT
"""F1: perseguidor original de marcas Cell EU/USA, sin servicios simulados.

Compara indices, duraciones, objetivo y posicion frente a un modelo racional
separado del interprete. Los angulos se ejecutan pero solo se observan. Son
escenarios sinteticos, no un dorado ni el mundo/juego completo.
"""
import argparse
from collections import Counter
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import struct
import time

import disparo_banco as banco
import disparo_ee
import disparo_geometria as geometria
import disparo_marcas as marcas
import disparo_mundo as mundo
import disparo_regeneracion as regeneracion
import ee

ACTOR, TAM_ACTOR = regeneracion.ACTOR, regeneracion.TAM_ACTOR
SALIDA, CTX = 0x1703000, 0x1704000
PERFILES = {
    'eu': {'fn': 0x31D4E0, 'fin': 0x31D8F0, 'getter': 0x30C490,
           'ctxp': 0x49CDBC, 'tabla': 0x5D93AC, 'switch': 0x499740, 'frente': 0x480A50},
    'us': {'fn': 0x31D1F0, 'fin': 0x31D600, 'getter': 0x30C220,
           'ctxp': 0x49C59C, 'tabla': 0x5D8BAC, 'switch': 0x498F40, 'frente': 0x4802A0}}
VELOCIDADES = tuple(marcas.bits(v) for v in (.5, 1, 2, .5, 1, 2))
PRESERVADOS = [(0x123456789ABCDEF0 + i, 0x0FEDCBA987654321 + i) for i in range(6)]


def racional(b):
    e, m = (b >> 23) & 255, b & 0x7FFFFF
    if e == 255:
        raise ValueError('modelo limitado a flotantes finitos normales o cero')
    if not e:
        return Fraction(0)
    return (-1 if b >> 31 else 1) * Fraction((1 << 23) + m) * potencia(e - 150)


def potencia(e):
    return Fraction(1 << e) if e >= 0 else Fraction(1, 1 << -e)


def truncar(x):
    """Cuantizacion racional a float32 hacia cero, independiente de ee.fb."""
    if not x:
        return 0
    signo, x = (0x80000000 if x < 0 else 0), abs(x)
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if x < potencia(e):
        e -= 1
    if not -126 <= e <= 127:
        raise ValueError('modelo fuera de exponente normal')
    mantisa = int(x / potencia(e - 23))
    return signo | ((e + 127) << 23) | (mantisa - (1 << 23))


def producto(a, b, cop1=False):
    q = racional(a) * racional(b)
    r = truncar(q)
    # Correccion Booth del perfil COP1 ya establecido; no afecta VU.
    if cop1 and b & 2 and q and racional(r) == q:
        r -= 1
    return r


def suma(a, b, cop1=False, resta=False):
    x, y = racional(a), racional(b)
    if cop1 and x and y:
        # Alineacion con un bit de guarda del perfil EE existente.
        e = max((a >> 23) & 255, (b >> 23) & 255) - 127
        unidad = potencia(e - 24)
        if (a >> 23 & 255) < (b >> 23 & 255):
            x = int(x / unidad) * unidad
        elif (b >> 23 & 255) < (a >> 23 & 255):
            y = int(y / unidad) * unidad
    return truncar(x - y if resta else x + y)


def raiz(b, cercano=False):
    q = abs(racional(b))
    if not q:
        return 0
    # libm solo propone un vecino. La seleccion se verifica con cuadrados
    # racionales exactos, incluido empate par en COP1 sqrt.
    r = marcas.bits(math.sqrt(float(q)))
    while racional(r) ** 2 > q:
        r -= 1
    while racional(r + 1) ** 2 <= q:
        r += 1
    if cercano:
        medio = (racional(r) + racional(r + 1)) / 2
        if q > medio ** 2 or q == medio ** 2 and r & 1:
            r += 1
    return r


def diferencia(a, b):
    return [suma(x, y, resta=True) for x, y in zip(a[:3], b[:3])]


def cuadrado(v):
    p = [producto(x, x) for x in v]
    return suma(suma(p[0], p[1]), p[2])


def distancia(a, b):
    return racional(raiz(cuadrado(diferencia(a, b)), cercano=True))


def escalar(v, magnitud):
    raiz_vu = raiz(cuadrado(v))
    inversa = truncar(1 / racional(raiz_vu)) if raiz_vu else 0x7F7FFFFF
    return [producto(producto(x, magnitud), inversa) for x in v]


def vector(pos):
    return [marcas.bits(v) for v in (*pos, 1)]


def contexto(tipo=1, duracion=32, objetivo=(0, 0, 64), marca=(0, 0, 128), indice=0):
    b = [0xA5A5A5A5] * (mundo.TAM_CONTEXTO // 4)
    b[0x140 // 4] = indice & ee.M32
    b[0x150 // 4:0x160 // 4] = vector(objetivo)
    for i in range(5):
        off = (0x50 + i * 0x30) // 4
        b[off] = tipo
        b[off + 4:off + 8] = vector(marca)
        b[off + 8], b[off + 9] = marcas.bits(2), marcas.bits(duracion)
    return b


def modelo(bloque, posicion, direccion, paso, dificultad, nulo=False):
    """Estado persistente propio: nunca lee las salidas del interprete."""
    bloque, posicion, direccion = bloque.copy(), posicion.copy(), direccion.copy()
    velocidad = 0 if nulo else VELOCIDADES[dificultad]
    movimiento = producto(paso, velocidad, cop1=True)
    umbral = racional(producto(marcas.bits(10), movimiento, cop1=True))
    get_calls, vistos = 0, []
    while True:
        i = bloque[0x140 // 4]
        if not 0 <= i < 5:
            return bloque, posicion, direccion, 1, get_calls, vistos
        off = (0x50 + i * 0x30) // 4
        fin = racional(bloque[off + 9]) < 0
        vistos.append(i)
        if fin:
            bloque[0x140 // 4] += 1
        marca, destino = bloque[off + 4:off + 8], bloque[0x150 // 4:0x160 // 4]
        dm, dd = distancia(marca, posicion), distancia(destino, posicion)
        get_calls += 1
        cerca = False
        if dd < umbral:
            get_calls += 1
            if dm > umbral:
                bloque[0x150 // 4:0x160 // 4] = marca.copy()
                cerca = True
        if not cerca:
            get_calls += 1
            avanza = False
            if dd > umbral:
                get_calls += 1
                if dm < umbral:
                    fin = True
                    bloque[0x140 // 4] += 1
                    avanza = True
            # Tambien se evalua este grupo si dd > umbral y dm >= umbral.
            # Solo el salto por incremento de indice lo omite.
            if not avanza:
                get_calls += 1
                if dd < umbral:
                    get_calls += 1
                    if dm < umbral:
                        fin = True
                        bloque[0x140 // 4] += 1
        if not fin:
            break
    tipo = bloque[off]
    if tipo == 0:
        return bloque, posicion, direccion, 1, get_calls, vistos
    destino = bloque[0x150 // 4:0x160 // 4]
    if tipo in (1, 9):
        destino = marca.copy()
    elif 2 <= tipo <= 8:
        delta = escalar(diferencia(marca, destino), bloque[off + 8])
        destino[:3] = [suma(x, y) for x, y in zip(destino[:3], delta)]
    bloque[0x150 // 4:0x160 // 4] = destino.copy()
    delta = escalar(diferencia(destino, posicion), movimiento)
    posicion[:3] = [suma(x, y) for x, y in zip(posicion[:3], delta)]
    direccion = [0, None, 0, marcas.bits(1)]
    bloque[off + 9] = suma(bloque[off + 9], paso, cop1=True, resta=True)
    return bloque, posicion, direccion, 0, get_calls + 1, vistos


class PerseguidorCell:
    def __init__(self, region, elf):
        self.marcas = marcas.MarcasCell(region, elf)
        self.cpu, self.d = self.marcas.cpu, PERFILES[region]
        self.base = mundo.PERFILES[region]['base']
        self.preparado = False
        c, d = self.cpu, self.d
        if ee.VU_CERCANO:
            raise ValueError('se requiere perfil VU por defecto')
        disparo_ee.instalar(c)
        intervalos = [('perseguidor', d['fn'], d['fin']),
                      ('getter', d['getter'], d['getter'] + 0x40),
                      ('norma', 0x1424A0, 0x1424C0), ('escalar', 0x1424E0, 0x142520),
                      ('normalizar', 0x142540, 0x142570), ('dot', 0x1425A0, 0x1425C0)]
        intervalos += [(f'math_{a:08x}', a, b) for a, b in geometria.MATH]
        c.whitelist = {a for _, a, _ in intervalos}
        c.vf[0] = [0, 0, 0, marcas.bits(1)]
        self.identidad = [{'nombre': n, 'inicio': a, 'fin_exclusivo': b,
                          'sha256': hashlib.sha256(c.m[a:b]).hexdigest()} for n, a, b in intervalos]
        def absoluto(pc, destino):
            lui, addiu = c.r32(pc), c.r32(pc + 0x10)
            imm = addiu & 0xFFFF
            if lui >> 26 != 0xF or addiu >> 26 != 9:
                raise ValueError('perfil LUI/ADDIU de datos distinto')
            if (lui & 0xFFFF) * 0x10000 + imm - (0x10000 if imm & 0x8000 else 0) != destino:
                raise ValueError('referencia regional de datos distinta')
        absoluto(d['fn'] + 0x28, self.base + 0x140)
        absoluto(d['fn'] + 0x30, self.base + 0x150)
        load = c.r32(d['getter'])
        if load >> 16 != 0x8F82:
            raise ValueError('getter sin LW de contexto GP')
        imm = load & 0xFFFF
        if self.marcas.d['gp'] + imm - (0x10000 if imm & 0x8000 else 0) != d['ctxp']:
            raise ValueError('puntero de contexto regional distinto')
        if [c.r32(d['switch'] + 4 * k) - d['fn'] for k in range(10)] != [
                0x244, 0x24C, 0x258, *([0x290] * 6), 0x2C8]:
            raise ValueError('tabla switch del perseguidor distinta')
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', c.m[d['fn']:d['fin']])
                 if w >> 26 == 3]
        if calls != [0x1424A0] * 2 + [d['getter']] * 6 + [0x1424E0] * 2 + [
                d['getter'], 0x1424E0, 0x142540, 0x1425A0, 0x119BB0, 0x1177B0, 0x11A6B0]:
            raise ValueError('sitios JAL del perseguidor distintos')
        if [c.r32(d['frente'] + i * 4) for i in range(4)] != [0, 0, marcas.bits(1), 0]:
            raise ValueError('vector frontal regional distinto')

    def preparar(self, bloque, pos, dificultad=0, nulo=False):
        if len(bloque) != mundo.TAM_CONTEXTO // 4 or not 0 <= dificultad < 6:
            raise ValueError('contexto completo y dificultad0..5 requeridos')
        c, b, d = self.cpu, self.base, self.d
        c.m[b - 16:b + mundo.TAM_CONTEXTO + 16] = bytes([0xA5]) * (mundo.TAM_CONTEXTO + 32)
        struct.pack_into('<%dI' % len(bloque), c.m, b, *bloque)
        for a, tam in ((ACTOR, TAM_ACTOR), (SALIDA, 16), (CTX, 0x40), (d['tabla'], 0x90)):
            c.m[a - 16:a + tam + 16] = bytes([0xA5]) * (tam + 32)
        struct.pack_into('<4I', c.m, ACTOR + 0x40, *vector(pos))
        for i, v in enumerate(VELOCIDADES):
            c.w32(d['tabla'] + i * 0x18, v)
        c.w32(d['ctxp'], 0 if nulo else CTX)
        c.w32(CTX + 0x1C, dificultad)
        c.w32(self.marcas.semilla, 0xC0FFEE33)
        for i, (lo, hi) in enumerate(PRESERVADOS, 16):
            c.r[i], c.up[i] = lo, hi
        for i in range(20, 24):
            c.f[i] = marcas.bits(i)
        self.actor = bytes(c.m[ACTOR:ACTOR + TAM_ACTOR])
        self.inmutables = [(a - 16, bytes(c.m[a - 16:a + tam + 16]))
                           for a, tam in ((CTX, 0x40), (d['tabla'], 0x90))]
        self.preparado = True

    def actualizar(self, paso):
        if not self.preparado or paso not in marcas.PASOS:
            raise ValueError('preparacion y paso del dominio requeridos')
        c = self.cpu
        c.w32(self.marcas.paso, paso)
        c.solicitudes.clear(); c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.call(self.d['fn'], ACTOR, SALIDA, limite=100000)
        if c.hooks or c.io or c.sin_puente:
            raise RuntimeError('perseguidor con frontera no autorizada')
        angulo = c.r32(SALIDA + 4)
        salida = self.observar()
        if salida['retorno'] == 0:
            salida['direccion'][1] = None
        return salida, angulo

    def observar(self):
        c, b = self.cpu, self.base
        actor = bytearray(c.m[ACTOR:ACTOR + TAM_ACTOR])
        pos = list(struct.unpack_from('<4I', actor, 0x40))
        actor[0x40:0x50] = self.actor[0x40:0x50]
        return {'bloque': [c.r32(b + 4 * i) for i in range(mundo.TAM_CONTEXTO // 4)],
                'posicion': pos, 'direccion': [c.r32(SALIDA + 4 * i) for i in range(4)],
                'retorno': c.r[2] & ee.M32, 'getter_llamadas': c.llamadas[self.d['getter']],
                'semilla': c.r32(self.marcas.semilla), 'paso_bits': c.r32(self.marcas.paso),
                'memory_llamadas': c.solicitudes.copy(),
                'actor_resto_intacto': actor == self.actor,
                'entradas_intactas': all(bytes(c.m[a:a + len(v)]) == v for a, v in self.inmutables),
                'guardias': [[bytes(c.m[a - 16:a]).hex(), bytes(c.m[a + tam:a + tam + 16]).hex()]
                             for a, tam in ((b, mundo.TAM_CONTEXTO), (ACTOR, TAM_ACTOR), (SALIDA, 16))],
                'preservados': [[c.r[i], c.up[i]] for i in range(16, 22)] + [c.f[20:24]]}


def comprobar(region, elf):
    s, filas, observaciones, continuos, ramas = PerseguidorCell(region, elf), [], [], [], Counter()
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
        if want != got and not any(not f['igual'] for f in filas[:-1]):
            print(f'Primer DIF {region}: {nombre}; detalle en evidencia')
    def quiere(b, p, d, ret, n, paso):
        return {'bloque': b, 'posicion': p, 'direccion': d, 'retorno': ret, 'getter_llamadas': n,
                'semilla': 0xC0FFEE33, 'paso_bits': paso, 'memory_llamadas': [],
                'actor_resto_intacto': True, 'entradas_intactas': True,
                'guardias': [['a5' * 16] * 2 for _ in range(3)],
                'preservados': [list(v) for v in PRESERVADOS] + [[marcas.bits(i) for i in range(20, 24)]]}
    def aislado(nombre, b, pos, paso, dificultad=0, nulo=False):
        s.preparar(b, pos, dificultad, nulo)
        b2, p2, d2, ret, n, vistos = modelo(b, vector(pos), [0xA5A5A5A5] * 4, paso, dificultad, nulo)
        got, angulo = s.actualizar(paso)
        revisar(nombre, quiere(b2, p2, d2, ret, n, paso), got)
        ramas[f'retorno{ret}'] += 1
        ramas[f'indices_recorridos{len(vistos)}'] += 1
        observaciones.append({'caso': nombre, 'indices_esperados': vistos,
                              'angulo_bits_no_comparado': angulo})
    poses = ((0, 0, 0), (0, 0, 32), (0, 0, -32), (32, 0, 0))
    for tipo in (*range(11), ee.M32):
        for duracion in (-1, 0, 32):
            for paso in marcas.PASOS:
                for dificultad in range(6):
                    for i, pos in enumerate(poses):
                        b = contexto(tipo, duracion)
                        # Tras saltar un slot caducado, otro slot esta vivo.
                        for k in range(1, 5):
                            b[(0x50 + k * 0x30) // 4 + 9] = marcas.bits(32)
                        aislado(f'aislado:{tipo}:{duracion}:{paso:08x}:{dificultad}:{i}',
                                b, pos, paso, dificultad)
    for paso in marcas.PASOS:
        for dificultad in range(6):
            t = producto(marcas.bits(10), producto(paso, VELOCIDADES[dificultad], True), True)
            for a in (t - 1, t, t + 1):
                for b in (t - 1, t, t + 1):
                    for duracion in (-1, 0):
                        bloque = contexto(0, 32)
                        bloque[0x150 // 4:0x160 // 4] = [0, 0, b, marcas.bits(1)]
                        bloque[0x60 // 4:0x70 // 4] = [0, 0, a, marcas.bits(1)]
                        bloque[0x74 // 4] = marcas.bits(duracion)
                        aislado(f'umbral:{paso:08x}:{dificultad}:{a:08x}:{b:08x}:{duracion}',
                                bloque, (0, 0, 0), paso, dificultad)
    for i in (-0x80000000, -1, 4, 5, 0x7FFFFFFF):
        aislado(f'indice:{i}', contexto(0, indice=i), (0, 0, 0), marcas.PASOS[1])
    for tipo in (0, 1, 2, 9, 10):
        aislado(f'contexto_nulo:{tipo}', contexto(tipo), (0, 0, 0), marcas.PASOS[1], nulo=True)
    geometrias = (
        ((-32, 16, 8), (8, -16, 64), (96, 32, 128)),
        ((32, -16, -8), (-8, 16, -64), (-96, -32, -128)),
        ((0, 0, 0), (32, 32, 32), (64, 96, 128)),
        ((0, 0, 0), (0, 0, 0), (32, 64, 96)),
        ((0, 0, 0), (32, 64, 96), (0, 0, 0)),
        ((0, 0, 0), (0, 0, 0), (0, 0, 0)),
    )
    for g, (pos, destino, marca) in enumerate(geometrias):
        for tipo in (*range(11), ee.M32):
            for paso in marcas.PASOS:
                aislado(f'geometria:{g}:{tipo}:{paso:08x}',
                        contexto(tipo, objetivo=destino, marca=marca), pos, paso, 1)
    for i in range(5):
        for tipo in (0, 1, 2, 8, 9, 10, ee.M32):
            for paso in marcas.PASOS:
                aislado(f'slot:{i}:{tipo}:{paso:08x}', contexto(tipo, indice=i),
                        (0, 0, 0), paso, 1)
    for patron in ((1, 2, 9, 3, 0), (8, 7, 6, 5, 0), (4, 10, ee.M32, 1, 0)):
        for paso in marcas.PASOS:
            b = contexto()
            for i, tipo in enumerate(patron):
                off = (0x50 + i * 0x30) // 4
                b[off], b[off + 9] = tipo, marcas.bits(4 + i * 2)
                b[off + 4:off + 8] = vector((0, 0, 64 + i * 16))
            s.preparar(b, (0, 0, 0), 1)
            pos, direccion, primera, acabo = vector((0, 0, 0)), [0xA5A5A5A5] * 4, None, False
            for tick in range(512):
                b, pos, direccion, ret, n, vistos = modelo(b, pos, direccion, paso, 1)
                want = quiere(b, pos, direccion, ret, n, paso)
                got, angulo = s.actualizar(paso)
                if ret == 1 and direccion[1] is None:
                    # El retorno temprano conserva la orientacion previa,
                    # que nunca tuvo un oraculo angular independiente.
                    got['direccion'][1] = None
                nombre = f'continuo:{patron}:{paso:08x}:{tick}'
                revisar(nombre, want, got)
                if want != got and primera is None:
                    primera = tick
                if ret:
                    acabo = True
                    break
            revisar(f'continuo_fin:{patron}:{paso:08x}', True, acabo)
            continuos.append({'patron': list(patron), 'paso_bits': paso, 'cuadros': tick + 1,
                              'fin': acabo, 'primera_diferencia': primera,
                              'indice_final': s.cpu.r32(s.base + 0x140)})
    for paso in (0, 0x7FC00000, 0x80000000):
        antes = s.observar()
        try:
            s.actualizar(paso)
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(f'rechazo_paso:{paso:08x}', [True, antes], [rechazo, s.observar()])
    for r in s.identidad:
        revisar('codigo_intacto:' + r['nombre'], r['sha256'],
                hashlib.sha256(s.cpu.m[r['inicio']:r['fin_exclusivo']]).hexdigest())
    primera = next((f for f in filas if not f['igual']), None)
    iguales = sum(f['igual'] for f in filas)
    print(f'Perseguidor Cell {region}: {iguales}/{len(filas)}, primer DIF {primera["caso"] if primera else None}')
    return {'region': region, 'elf_sha256': s.marcas.digest, 'cuerpos': s.identidad,
            'perfil': s.d, 'puentes': [], 'iguales': iguales, 'total': len(filas),
            'primera_diferencia': primera, 'correcto': primera is None,
            'ramas_aisladas': dict(ramas), 'continuos': continuos,
            'cobertura': {'aislados': len(observaciones), 'secuencias_continuas': len(continuos),
                          'cuadros_continuos': sum(c['cuadros'] for c in continuos),
                          'dificultades': list(range(6)), 'velocidades_sinteticas_bits': list(VELOCIDADES),
                          'pasos_bits': list(marcas.PASOS), 'geometrias_adicionales': len(geometrias),
                          'indices_validos': list(range(5)), 'rechazos_paso': 3,
                          'resincronizaciones_continuas': 0, 'angulos_comparados': 0},
            'observaciones_angulares': observaciones, 'casos': filas}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a, inicio = p.parse_args(), time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump({'operacion': 'perseguidor_marcas_cell', 'sha256_banco': banco.sha256(Path(__file__)),
                       'sha256_dependencias': {Path(m.__file__).name: banco.sha256(Path(m.__file__))
                                               for m in (banco, ee, marcas, mundo, disparo_ee, geometria, regeneracion)},
                       'integracion_cpp_nueva': False, 'pares_dorado': 0,
                       'perfil_fpu': sorted(ee.FPU_HW), 'perfil_vu': dict(ee.VU_CERCANO),
                       'limites': ['escenarios sinteticos; no mundo ni minijuego completos',
                                   'oraculo racional del perfil existente, no hardware nuevo',
                                   'angulo nativo solo observado, no comparado',
                                   'sin C++ nuevo/juego/emulador/build'],
                       'regiones': regiones, 'segundos_banco': round(time.monotonic() - inicio, 3)},
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

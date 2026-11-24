# SPDX-License-Identifier: MIT
"""F1: regeneracion/despacho original de marcas al terminar dash de Cell.

AnimationFramesLink, voz y entradas de pose son fronteras controladas. La
geometria angular se ejecuta nativamente y se conserva solo como observacion;
el contrato compara marcas, semilla, guardias, campos del actor y despacho.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_banco as banco
import disparo_ee
import disparo_geometria as geometria
import disparo_marcas as marcas
import disparo_mundo as mundo
import ee

ACTOR, TAM_ACTOR = 0x1700000, 0x2100
PERFILES = {
    'eu': {'dash': 0x31C800, 'fin': 0x31C9E0, 'animacion': 0x238640, 'voz': 0x30D610,
           'pose1': 0x31D350, 'pose3': 0x31CEF0, 'tabla_pose': 0x4996B0},
    'us': {'dash': 0x31C510, 'fin': 0x31C6F0, 'animacion': 0x2384B0, 'voz': 0x30D3A0,
           'pose1': 0x31D060, 'pose3': 0x31CC00, 'tabla_pose': 0x498EB0}}
ANGULO = 0x34 // 4


def flotante(b):
    return struct.unpack('<f', struct.pack('<I', b))[0]


def actor_esperado(posicion):
    datos = bytearray([0xA5]) * TAM_ACTOR
    struct.pack_into('<4I', datos, 0x40, *(marcas.bits(v) for v in (*posicion, 1)))
    return datos


def regenerar(semilla, paso, anterior):
    bloque = anterior.copy()
    bloque[0x30 // 4:0x40 // 4] = [0, None, 0, marcas.bits(1)]
    bloque[0x140 // 4] = 0
    for i in range(5):
        offset = (0x50 + i * 0x30) // 4
        semilla, marca = marcas.marca_esperada(semilla, paso, bloque[offset:offset + 12])
        bloque[offset:offset + 12] = marca
    return semilla, bloque


class RegeneracionCell(mundo.MundoCell):
    def __init__(self, region, elf):
        super().__init__(region, elf)
        self.r = r = PERFILES[region]
        self.eventos = []
        c = self.cpu
        disparo_ee.instalar(c)
        c.whitelist.update(a for a, _ in geometria.MATH)
        c.whitelist.update((r['dash'], 0x142540, 0x1425A0))
        for rol in ('animacion', 'voz', 'pose1', 'pose3'):
            c.hooks[r[rol]] = self._servicio(rol)
        self.puentes = set(c.hooks)
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', c.m[r['dash']:r['fin']])
                 if w >> 26 == 3]
        if calls != [r['animacion'], 0x142540, 0x1425A0, 0x119BB0, 0x1177B0,
                     0x11A6B0, self.marcas.d['fn'], r['voz']]:
            raise ValueError('sitios JAL del dash no coinciden')
        if [c.r32(r['tabla_pose'] + k * 4) for k in (1, 3)] != [r['pose1'], r['pose3']]:
            raise ValueError('destinos indirectos de pose no coinciden con ELF')
        self.identidad += [{'nombre': nombre, 'inicio': a, 'fin_exclusivo': b,
                            'sha256': hashlib.sha256(c.m[a:b]).hexdigest()}
                           for nombre, a, b in [('dash', r['dash'], r['fin']),
                                                ('normalizar', 0x142540, 0x142570),
                                                ('dot', 0x1425A0, 0x1425C0)] +
                           [(f'math_{a:08x}', a, b) for a, b in geometria.MATH]]

    def _servicio(self, rol):
        def hook(c):
            args = [c.r[4] & ee.M32]
            if rol == 'animacion':
                args.append(c.r[5] & ee.M32)
            if rol != 'voz' and args[0] != ACTOR:
                raise RuntimeError('servicio de actor ajeno al banco')
            self.eventos.append({'tipo': rol, 'args': args})
            c.r[2] = 0
        return hook

    def _llamar(self, fn, *args):
        c = self.cpu
        self.eventos.clear(); self.tareas.clear()
        c.solicitudes.clear(); c.io.clear(); c.sin_puente.clear(); c.llamadas.clear()
        c.call(fn, *args, limite=100000)
        if c.io or c.sin_puente or set(c.hooks) != self.puentes:
            raise RuntimeError('frontera no declarada en regeneracion Cell')

    def preparar_actor(self, posicion):
        datos = actor_esperado(posicion)
        self.cpu.m[ACTOR - 16:ACTOR + TAM_ACTOR + 16] = bytes([0xA5]) * (TAM_ACTOR + 32)
        self.cpu.m[ACTOR:ACTOR + TAM_ACTOR] = datos
        return datos

    def actualizar(self, paso, terminado):
        if paso not in marcas.PASOS or type(terminado) is not int or not 0 <= terminado <= ee.M32:
            raise ValueError('paso o bandera de animacion fuera del dominio')
        self.cpu.w32(self.marcas.paso, paso)
        # Salida externa controlada de animacion; el hook no la calcula.
        self.cpu.w32(ACTOR + 0xC74, terminado)
        self._llamar(self.r['dash'], ACTOR)
        c, b = self.cpu, self.d['base']
        bloque = [c.r32(b + i * 4) for i in range(mundo.TAM_CONTEXTO // 4)]
        angulo = bloque[ANGULO]
        salida = {'semilla': c.r32(self.marcas.semilla), 'bloque': bloque,
                  'paso_bits': c.r32(self.marcas.paso), 'solicitudes_memory': c.solicitudes.copy(),
                  'marcas_generadas': c.llamadas[self.marcas.d['fn']],
                  'guardias_mundo': [bytes(c.m[b - 16:b]).hex(),
                                     bytes(c.m[b + mundo.TAM_CONTEXTO:b + mundo.TAM_CONTEXTO + 16]).hex()],
                  'guardias_actor': [bytes(c.m[ACTOR - 16:ACTOR]).hex(),
                                     bytes(c.m[ACTOR + TAM_ACTOR:ACTOR + TAM_ACTOR + 16]).hex()],
                  'actor_sha256': hashlib.sha256(c.m[ACTOR:ACTOR + TAM_ACTOR]).hexdigest(),
                  'eventos': self.eventos.copy(), 'tareas_solicitadas': list(self.tareas),
                  'preservados_s0_f20': [c.r[16], c.up[16], c.f[20]]}
        return salida, angulo


def comprobar(region, elf):
    s, filas, observaciones, continuos = RegeneracionCell(region, elf), [], [], []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    def quiere(st, bloque, paso, activo, actor):
        eventos = [{'tipo': 'animacion', 'args': [ACTOR, 0]}]
        if activo:
            primero = bloque[0x50 // 4]
            eventos += ([{'tipo': 'voz', 'args': [5]}] if primero else [])
            eventos.append({'tipo': 'pose1' if primero else 'pose3', 'args': [ACTOR]})
        return {'semilla': st, 'bloque': bloque.copy(), 'paso_bits': paso,
                'solicitudes_memory': [32] * 25 if activo else [], 'marcas_generadas': 5 if activo else 0,
                'guardias_mundo': ['a5' * 16] * 2, 'guardias_actor': ['a5' * 16] * 2,
                'actor_sha256': hashlib.sha256(actor).hexdigest(),
                'eventos': eventos, 'tareas_solicitadas': [],
                'preservados_s0_f20': [0x123456789ABCDEF0, 0x0FEDCBA987654321, marcas.bits(8)]}
    def paso_modelo(st, bloque, paso, terminado, actor):
        struct.pack_into('<I', actor, 0xC74, terminado)
        if terminado:
            st, bloque = regenerar(st, paso, bloque)
            actor[0xCC6:0xCC8] = b'\0\0'
            actor[0x206F] = 0
        return st, bloque
    ramas = {'pose1': 0, 'pose3': 0, 'sin_fin_animacion': 0}
    semillas = (*range(16), 0x80000000, ee.M32, 3455373742)
    for semilla in semillas:
        for paso in marcas.PASOS:
            st0, inicio = mundo.esperado(semilla, paso)
            x, z = flotante(inicio[0x60 // 4]), flotante(inicio[0x68 // 4])
            for direccion, posicion in enumerate(((x, 0, z - 16), (x, 0, z + 16),
                                                   (x - 16, 0, z), (x + 16, 0, z))):
                for terminado in (0, 1):
                    pref = f'aislado:{semilla:08x}:{paso:08x}:{direccion}:{terminado}'
                    s.preparar(semilla, paso)
                    got_inicio = s.iniciar(paso)
                    revisar(pref + ':arranque', [st0, inicio], [got_inicio['semilla'], got_inicio['bloque']])
                    actor = s.preparar_actor(posicion)
                    st, bloque = paso_modelo(st0, inicio.copy(), paso, terminado, actor)
                    got, angulo = s.actualizar(paso, terminado)
                    if terminado:
                        got['bloque'][ANGULO] = None
                    revisar(pref + ':regeneracion', quiere(st, bloque, paso, terminado, actor), got)
                    rama = ('pose1' if bloque[0x50 // 4] else 'pose3') if terminado else 'sin_fin_animacion'
                    ramas[rama] += 1
                    observaciones.append({'caso': pref, 'angulo_bits_no_comparado': angulo,
                                          'posicion_externa': list(posicion)})
    for semilla in (0, 1, 0x80000000, 3455373742):
        s.preparar(semilla, marcas.PASOS[0])
        s.iniciar(marcas.PASOS[0])
        st, bloque = mundo.esperado(semilla, marcas.PASOS[0])
        actor = s.preparar_actor((0, 0, 0))
        primera = None
        for tick in range(32):
            paso, terminado = marcas.PASOS[tick % 4], (0, 1, 0xFFFFFFFF)[tick % 3]
            st, bloque = paso_modelo(st, bloque, paso, terminado, actor)
            got, angulo = s.actualizar(paso, terminado)
            if bloque[ANGULO] is None:
                got['bloque'][ANGULO] = None
            want = quiere(st, bloque, paso, terminado, actor)
            revisar(f'continuo:{semilla:08x}:{tick}', want, got)
            if want != got and primera is None:
                primera = tick
            observaciones.append({'semilla_inicial': semilla, 'tick': tick,
                                  'angulo_bits_no_comparado': angulo})
        continuos.append({'semilla_inicial': semilla, 'cuadros': 32, 'primera_diferencia': primera,
                          'semilla_final_esperada': st, 'semilla_final_obtenida': s.cpu.r32(s.marcas.semilla)})
    for r in s.identidad:
        revisar('codigo_intacto:' + r['nombre'], r['sha256'],
                hashlib.sha256(s.cpu.m[r['inicio']:r['fin_exclusivo']]).hexdigest())
    primera = next((r for r in filas if not r['igual']), None)
    iguales = sum(r['igual'] for r in filas)
    print(f'Regeneracion Cell {region}: {iguales}/{len(filas)}, '
          f'primer DIF {primera["caso"] if primera else None}; ramas {ramas}')
    return {'region': region, 'elf_sha256': s.marcas.digest, 'cuerpos': s.identidad,
            'puentes': sorted(s.puentes), 'iguales': iguales, 'total': len(filas),
            'primera_diferencia': primera, 'correcto': primera is None,
            'cobertura': {'semillas_aisladas': len(semillas), 'pasos': len(marcas.PASOS),
                          'posiciones_por_pareja': 4, 'banderas_aisladas': [0, 1],
                          'arranques_aislados': 608, 'actualizaciones_aisladas': 608,
                          'actualizaciones_continuas': 128, 'intervalos_codigo_intacto': len(s.identidad)},
            'ramas_aisladas': ramas, 'continuos': continuos,
            'observaciones_geometria': observaciones, 'casos': filas}


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
            json.dump({'operacion': 'regeneracion_marcas_cell', 'sha256_banco': banco.sha256(Path(__file__)),
                       'sha256_dependencias': {Path(m.__file__).name: banco.sha256(Path(m.__file__))
                                               for m in (banco, ee, marcas, mundo, disparo_ee, geometria)},
                       'integracion_cpp_nueva': False, 'pares_dorado': 0,
                       'limites': ['animacion/voz/entradas de pose/planificador son fronteras controladas',
                                   'angulo nativo solo observado; no oraculo geometrico independiente',
                                   'no perseguidor de marcas ni mundo/maquina completos',
                                   'sin C++ nuevo/juego/emulador/build'],
                       'regiones': regiones, 'segundos_banco': round(time.monotonic() - inicio, 3)},
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

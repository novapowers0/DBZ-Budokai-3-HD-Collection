# SPDX-License-Identifier: MIT
"""F1: arranque original del contexto Cell y sus cinco marcas, EU/USA.

Ejecuta s_PLY_PRE_EXEC, memset, generador de marcas y Memory desde ELF.
Unico puente: registra las dos peticiones al planificador (140300); no crea
tareas reales ni ejecuta sus callbacks. Banco offline, sin integracion C++.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_banco as banco
import disparo_marcas as marcas
import ee

MEMSET, PLANIFICADOR = 0x11DEF8, 0x140300
PERFILES = {
    'eu': {'inicio': 0x31E0A0, 'fin': 0x31E1D0, 'base': 0x5D94B0,
           'callbacks': (0x31DE20, 0x31DC30), 'parametros': (0x499768, 0x499780)},
    'us': {'inicio': 0x31DDB0, 'fin': 0x31DEE0, 'base': 0x5D8CB0,
           'callbacks': (0x31DB30, 0x31D940), 'parametros': (0x498F68, 0x498F80)}}
TAM_CONTEXTO = 0x160


def sha_bloque(palabras):
    return hashlib.sha256(struct.pack('<%dI' % len(palabras), *palabras)).hexdigest()


def esperado(semilla, paso):
    bloque = [0] * (TAM_CONTEXTO // 4)
    bloque[0x10 // 4] = 1
    bloque[0x3C // 4] = marcas.bits(1)
    bloque[0x40 // 4] = ee.M32
    for i in range(5):
        semilla, marca = marcas.marca_esperada(semilla, paso, [0] * 12)
        offset = (0x50 + i * 0x30) // 4
        bloque[offset:offset + 12] = marca
    return semilla, bloque


class MundoCell:
    def __init__(self, region, elf):
        self.marcas = marcas.MarcasCell(region, elf)
        self.cpu = c = self.marcas.cpu
        self.d = d = PERFILES[region]
        self.tareas = []
        self.preparado = False
        c.hooks = {PLANIFICADOR: self._crear_tarea}
        c.whitelist.update((d['inicio'], MEMSET))
        c.vf[0] = [0, 0, 0, marcas.bits(1)]
        inicio = d['inicio']
        # LUI/ADDIU a0 para memset, y los cuatro sitios JAL del cuerpo.
        lui, addiu = c.r32(inicio + 4), c.r32(inicio + 0xC)
        imm = addiu & 0xFFFF
        base = ((lui & 0xFFFF) << 16) + imm - (0x10000 if imm & 0x8000 else 0)
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', c.m[inicio:d['fin']])
                 if w >> 26 == 3]
        if lui >> 16 != 0x3C04 or addiu >> 16 != 0x2484 or base != d['base']:
            raise ValueError('base del contexto no coincide con las instrucciones')
        if calls != [MEMSET, self.marcas.d['fn'], PLANIFICADOR, PLANIFICADOR]:
            raise ValueError('llamadas del arranque Cell no coinciden')
        self.identidad = self.marcas.identidad + [
            {'nombre': nombre, 'inicio': a, 'fin_exclusivo': b,
             'sha256': hashlib.sha256(c.m[a:b]).hexdigest()}
            for nombre, a, b in [('s_PLY_PRE_EXEC', inicio, d['fin']),
                                 ('memset_intervalo_seleccionado', MEMSET, MEMSET + 0xC0)]]

    def _crear_tarea(self, c):
        b = self.d['base']
        self.tareas.append({'argumentos': [c.r[k] & ee.M32 for k in range(4, 10)],
                            'semilla': c.r32(self.marcas.semilla),
                            'contexto_sha256': hashlib.sha256(c.m[b:b + TAM_CONTEXTO]).hexdigest()})
        c.r[2] = 0x1600000 + len(self.tareas) * 0x100

    def preparar(self, semilla, paso):
        self.marcas.preparar(semilla, paso)
        c, b = self.cpu, self.d['base']
        c.m[b - 16:b + TAM_CONTEXTO + 16] = bytes([0xA5]) * (TAM_CONTEXTO + 32)
        c.r[16], c.up[16], c.f[20] = 0x123456789ABCDEF0, 0x0FEDCBA987654321, marcas.bits(8)
        self.preparado = True

    def _llamar(self, fn, *args):
        c = self.cpu
        self.tareas.clear()
        c.solicitudes.clear(); c.io.clear(); c.sin_puente.clear(); c.llamadas.clear()
        c.call(fn, *args, limite=100000)
        if c.io or c.sin_puente or set(c.hooks) != {PLANIFICADOR}:
            raise RuntimeError('frontera no declarada en arranque Cell')

    def iniciar(self, paso):
        if not self.preparado or paso not in marcas.PASOS:
            raise ValueError('contexto preparado y paso del dominio requeridos')
        self.cpu.w32(self.marcas.paso, paso)
        self._llamar(self.d['inicio'])
        return self.observar()

    def consumir(self, n):
        if not self.preparado or type(n) is not int or not 0 <= n <= 255:
            raise ValueError('Memory fuera de0..255')
        self._llamar(marcas.MEMORY, n)
        return [self.cpu.r32(self.marcas.semilla), self.cpu.r[2] & ee.M32]

    def observar(self):
        c, b = self.cpu, self.d['base']
        return {'semilla': c.r32(self.marcas.semilla),
                'bloque': [c.r32(b + i * 4) for i in range(TAM_CONTEXTO // 4)],
                'guardias': [bytes(c.m[b - 16:b]).hex(), bytes(c.m[b + TAM_CONTEXTO:b + TAM_CONTEXTO + 16]).hex()],
                'paso_bits': c.r32(self.marcas.paso), 'solicitudes_memory': c.solicitudes.copy(),
                'llamadas': [[hex(pc), n] for pc, n in sorted(c.llamadas.items())],
                'tareas_solicitadas': list(self.tareas),
                'preservados_s0_f20': [c.r[16], c.up[16], c.f[20]]}


def comprobar(region, elf):
    s, filas, continuos = MundoCell(region, elf), [], []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    def quiere(st, bloque, paso):
        tareas = [{'argumentos': [fn, 0, orden, 0x20, 0x12, param],
                   'semilla': st, 'contexto_sha256': sha_bloque(bloque)}
                  for fn, param, orden in zip(s.d['callbacks'], s.d['parametros'], (0x5100, 0x5300))]
        return {'semilla': st, 'bloque': bloque, 'guardias': ['a5' * 16] * 2,
                'paso_bits': paso, 'solicitudes_memory': [32] * 25,
                'llamadas': [[hex(pc), n] for pc, n in sorted({MEMSET: 1, marcas.MEMORY: 25,
                              s.marcas.d['fn']: 5, PLANIFICADOR: 2}.items())],
                'tareas_solicitadas': tareas,
                'preservados_s0_f20': [0x123456789ABCDEF0, 0x0FEDCBA987654321, marcas.bits(8)]}
    semillas = sorted({0, 1, ee.M32, 0x80000000, 0x7FFFFFFF, 3455373742,
                       *(1 << i for i in range(32)), *(ee.M32 ^ (1 << i) for i in range(32)),
                       *((i * 0x9E3779B9 ^ 0xA5A5A5A5) & ee.M32 for i in range(256))})
    for semilla in semillas:
        for paso in marcas.PASOS:
            s.preparar(semilla, paso)
            st, bloque = esperado(semilla, paso)
            revisar(f'inicio:{semilla:08x}:{paso:08x}', quiere(st, bloque, paso), s.iniciar(paso))
    # Son reinicios de contexto con la semilla global conservada, no cuadros de
    # la partida. El arranque original borra el contexto, el banco no lo rellena.
    for semilla in (0, 1, 0x80000000, 3455373742):
        s.preparar(semilla, marcas.PASOS[0])
        st, primera = semilla, None
        for i in range(24):
            if i % 3 == 0:
                n = (1, 8, 32)[i // 3 % 3]
                st, valor = marcas.avanzar(st, n)
                revisar(f'continuo:{semilla:08x}:{i}:otro_Memory', [st, valor], s.consumir(n))
            paso = marcas.PASOS[i % len(marcas.PASOS)]
            st, bloque = esperado(st, paso)
            want, got = quiere(st, bloque, paso), s.iniciar(paso)
            revisar(f'continuo:{semilla:08x}:{i}:inicio', want, got)
            if want != got and primera is None:
                primera = i
        continuos.append({'semilla_inicial': semilla, 'reinicios': 24, 'primera_diferencia': primera,
                          'semilla_final_esperada': st, 'semilla_final_obtenida': s.cpu.r32(s.marcas.semilla)})
    for nombre, fn in [('paso0', lambda: s.iniciar(0)), ('pasoNaN', lambda: s.iniciar(0x7FC00000)),
                       ('Memory negativo', lambda: s.consumir(-1))]:
        antes = s.observar()
        try:
            fn()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar('rechazo:' + nombre, [True, antes], [rechazo, s.observar()])
    for r in s.identidad:
        revisar('codigo_intacto:' + r['nombre'], r['sha256'],
                hashlib.sha256(s.cpu.m[r['inicio']:r['fin_exclusivo']]).hexdigest())
    primera = next((r for r in filas if not r['igual']), None)
    iguales = sum(r['igual'] for r in filas)
    print(f'Arranque mundo Cell {region}: {iguales}/{len(filas)}, '
          f'primer DIF {primera["caso"] if primera else None}')
    return {'region': region, 'elf_sha256': s.marcas.digest, 'cuerpos': s.identidad,
            'base_contexto': s.d['base'], 'contexto_bytes': TAM_CONTEXTO,
            'semillas_aisladas': len(semillas), 'continuos': continuos,
            'iguales': iguales, 'total': len(filas), 'primera_diferencia': primera,
            'correcto': primera is None, 'casos': filas}


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
            json.dump({'operacion': 'arranque_mundo_cell', 'sha256_banco': banco.sha256(Path(__file__)),
                       'sha256_banco_marcas': banco.sha256(Path(marcas.__file__)),
                       'sha256_interprete': banco.sha256(Path(ee.__file__)),
                       'integracion_cpp_nueva': False, 'pares_dorado': 0,
                       'puentes': [{'direccion': PLANIFICADOR, 'rol': 'peticiones_tarea_sin_ejecucion'}],
                       'limites': ['arranque de contexto y generacion de marcas, no mundo completo',
                                   'planificador controlado; callbacks no ejecutados',
                                   'continuos de reinicio, no cuadros ni maquina completa',
                                   'oraculo entero con perfil EE existente, no hardware nuevo',
                                   'sin C++ nuevo/juego/emulador/build'],
                       'regiones': regiones, 'segundos_banco': round(time.monotonic() - inicio, 3)},
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

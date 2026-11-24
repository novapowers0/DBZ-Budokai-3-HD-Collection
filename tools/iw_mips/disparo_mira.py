# SPDX-License-Identifier: MIT
"""F1: movimiento DIGITAL de mira Cell desde el ELF EU/USA, sin juego.

Ejecuta init, MoveAimByPad, getter/setter y suma vectorial originales, sin
puentes de retorno inventado. Palancas centradas: su rama analogica queda
fuera de este banco. La comparacion del port es un modelo Python, no C++.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import time

import disparo_banco as banco
import ee

CAM = 0x1400000
PERFILES = {
    'eu': {'elf': banco.ELF_EU, 'gp': 0x4A3DF0, 'init': 0x30D150,
           'mover': 0x309AE0, 'fin': 0x309DF0, 'camara': 0x2ED7D0,
           'trasladar': 0x2ED9E0, 'camp': 0x49CD6C, 'mando': 0x4ADECC,
           'mando2': 0x4ADF2C, 'analog': 0x4ADEDA, 'velocidad': 0x5D9374},
    'us': {'elf': banco.ELF_US, 'gp': 0x4A35F0, 'init': 0x30CEE0,
           'mover': 0x3098C0, 'fin': 0x309BD0, 'camara': 0x2ED5C0,
           'trasladar': 0x2ED7D0, 'camp': 0x49C54C, 'mando': 0x4AD6CC,
           'mando2': 0x4AD72C, 'analog': 0x4AD6DA, 'velocidad': 0x5D8B74}}


def bits(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


class MiraCell:
    def __init__(self, region, elf=None):
        self.d = d = PERFILES[region]
        mem, self.digest = banco.cargar_elf(elf or d['elf'])
        if self.digest != banco.ELF_HASH[region]:
            raise ValueError('ELF distinto del perfil regional verificado')
        self.cuerpo_sha256 = hashlib.sha256(mem[d['mover']:d['fin']]).hexdigest()
        self.cpu = c = ee.Ee(mem, d['gp'])
        c.call(d['init'], limite=100000)
        if c.io or c.sin_puente:
            raise RuntimeError('init con E/S o llamadas no cubiertas')
        self.velocidad = c.r32(d['velocidad'])
        if self.velocidad != bits(1.5):
            raise ValueError(f'init no produjo velocidad 1.5: {self.velocidad:#x}')
        # Derivar el helper vectorial del JAL real del setter regional.
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack(
            '<I', mem[d['trasladar']:d['trasladar'] + 0x40]) if w >> 26 == 3]
        if len(calls) != 1:
            raise ValueError('setter regional no tiene un unico helper vectorial')
        self.vector = calls[0]
        self.roles = {d['camara']: 'camara', d['trasladar']: 'trasladar', self.vector: 'vector'}
        c.whitelist = {d['mover'], *self.roles}
        c.estricto = True
        self.preparado = False

    def preparar(self, x=0.0, z=200.0):
        if not all(type(v) in (float, int) and math.isfinite(v) for v in (x, z)) or not -111 <= x <= 74 or not 60 <= z <= 280:
            raise ValueError('mira fuera de limites o no finita')
        c, d = self.cpu, self.d
        c.m[CAM:CAM + 0x200] = bytes(0x200)
        c.w32(d['camp'], CAM)
        for i, v in enumerate((x, 400.0, z, 1.0, x, 0.0, z - 200.0, 1.0)):
            c.w32(CAM + 0x80 + 4 * i, bits(v))
        c.w8(d['analog'], 0); c.w8(d['analog'] + 1, 0)
        self.preparado = True

    def pasar(self, mando=0, mando2=0):
        if not self.preparado:
            raise ValueError('preparar camara antes de mover')
        if any(type(v) is not int or v < 0 or v & ~0xF0 for v in (mando, mando2)):
            raise ValueError('solo bits digitales 0x10/20/40/80')
        c, d = self.cpu, self.d
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.w16(d['mando'], mando); c.w16(d['mando2'], mando2)
        c.call(d['mover'], limite=5000)
        if c.io or c.sin_puente:
            raise RuntimeError(f'E/S o llamada no cubierta: {dict(c.io)}, {dict(c.sin_puente)}')
        return {'vectores_bits': [c.r32(CAM + 0x80 + 4 * i) for i in range(8)],
                'llamadas': dict(Counter(self.roles[a] for a in c.llamadas.elements()))}


def esperado(vectores, mando, mando2):
    # Orden literal: izquierda, derecha, arriba, abajo. NO restar bits opuestos
    # antes de comprobar limites; el setter modifica ojo Y objetivo tras cada paso.
    vs = [ee.fv(v) for v in vectores]
    mask, n = mando | mando2, 0
    for flag, eje, delta, lo, hi in ((0x80, 0, -1.5, -111, 74), (0x20, 0, 1.5, -111, 74),
                                    (0x10, 2, -1.5, 60, 280), (0x40, 2, 1.5, 60, 280)):
        if mask & flag and lo <= vs[eje] + delta <= hi:
            vs[eje] += delta
            vs[eje + 4] += delta
            # FUN_00127060 usa vadd.xyz: W permanece intacto aunque el setter
            # construya una cuarta componente 1. El decompilado oculta la mascara.
            n += 1
    llamadas = {'camara': 1}
    if n:
        llamadas.update(trasladar=n, vector=n)
    return {'vectores_bits': [bits(v) for v in vs], 'llamadas': llamadas}


def port_modelo(x, z, mask):
    # Cuadro15, rama digital, sin piloto: suma unica y clamp.
    return [bits(max(-111, min(74, x + 1.5 * (bool(mask & 0x20) - bool(mask & 0x80))))),
            bits(max(60, min(280, z + 1.5 * (bool(mask & 0x40) - bool(mask & 0x10)))))]


def comprobar(region, elf):
    s = MiraCell(region, elf)
    filas, port_iguales, port_total, primer_port = [], 0, 0, None
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    for x in (-111.0, -110.0, -109.5, 0.0, 72.5, 73.0, 74.0):
        for z in (60.0, 60.5, 61.5, 200.0, 278.5, 279.5, 280.0):
            for mando in range(0, 256, 16):
                for mando2 in (0, mando, mando ^ 0xF0):
                    s.preparar(x, z)
                    inicial = [bits(v) for v in (x, 400, z, 1, x, 0, z - 200, 1)]
                    got = s.pasar(mando, mando2)
                    revisar(f'aislado:{x}:{z}:{mando:x}:{mando2:x}', esperado(inicial, mando, mando2), got)
                    original = [got['vectores_bits'][i] for i in (0, 2)]
                    port = port_modelo(x, z, mando | mando2)
                    port_total += 1
                    port_iguales += original == port
                    if original != port and primer_port is None:
                        primer_port = {'x': x, 'z': z, 'mando': mando, 'mando2': mando2,
                                       'original_bits': original, 'modelo_port_bits': port}
    continuos = []
    for mando, n in ((0x20, 150), (0x80, 160), (0x50, 180), (0xA0, 180)):
        s.preparar()
        prev = [bits(v) for v in (0, 400, 200, 1, 0, 0, 0, 1)]
        first = None
        for tick in range(n):
            want, got = esperado(prev, mando, 0), s.pasar(mando)
            revisar(f'continuo:{mando:x}:{tick}', want, got)
            if want != got and first is None:
                first = tick
            # No usar la salida observada como entrada del siguiente esperado.
            prev = want['vectores_bits']
        continuos.append({'mando': mando, 'ticks': n, 'primera_diferencia_tick': first,
                           'final_bits': got['vectores_bits'], 'final_esperado_bits': prev})
    for nombre, fn in (
        ('fuera de X', lambda: s.preparar(75, 200)),
        ('fuera de Z', lambda: s.preparar(0, 59)),
        ('NaN', lambda: s.preparar(float('nan'), 200)),
        ('mando analogico excluido', lambda: s.pasar(0x8000)),
        ('mando negativo', lambda: s.pasar(-1))):
        try:
            fn()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(nombre, True, rechazo)
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Mira digital Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    print(f'  modelo port: {port_iguales}/{port_total}, primer DIF {primer_port}')
    return {'region': region, 'elf_sha256': s.digest, 'cuerpo_sha256': s.cuerpo_sha256,
            'perfil': {k: v for k, v in s.d.items() if k != 'elf'}, 'helper_vectorial': s.vector,
            'iguales': iguales, 'total': len(filas), 'primera_diferencia': primera,
            'correcto': iguales == len(filas), 'continuos': continuos, 'casos': filas,
            'modelo_port': {'iguales': port_iguales, 'total': port_total, 'primera_diferencia': primer_port}}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    args = p.parse_args()
    start = time.monotonic()
    try:
        if args.salida.exists() or not args.salida.parent.is_dir():
            raise ValueError('salida debe ser nueva y su padre debe existir')
        regiones = [comprobar(r, e) for r, e in [('eu', args.elf_eu), ('us', args.elf_us)]]
        report = {'operacion': 'mira_digital_cell', 'alcance': 'MoveAimByPad digital y helpers originales sin puentes',
                  'referencia_continua': 'estado esperado propio, sin realimentar salidas observadas',
                  'FPU_HW': sorted(ee.FPU_HW), 'FPU_CERCANO': ee.FPU_CERCANO,
                  'pares_dorado_real': 0, 'integrado_cpp': False,
                  'limites': ['solo digital con stick centrado, no rama analogica',
                              'sin transiciones de tarea Aim/Charge/Fire ni resto de mundo',
                              'casos semienteros exactos; no prueba general de FPU/VU',
                              'modelo port Python, no ejecucion C++ ni prueba en juego'],
                  'regiones': regiones, 'segundos_banco': round(time.monotonic() - start, 3)}
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

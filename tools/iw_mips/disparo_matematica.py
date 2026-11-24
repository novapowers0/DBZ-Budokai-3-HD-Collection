# SPDX-License-Identifier: MIT
"""F1: helpers soft-double originales frente a aritmetica binaria independiente.

Dominio finito y exacto (potencias de dos). No prueba completa de IEEE754,
trigonometria, FPU/VU, hardware ni geometria del proyectil.
"""
import argparse
import json
from pathlib import Path
import struct

import disparo_geometria as geometria
import ee


def doble(v):
    return struct.unpack('<Q', struct.pack('<d', v))[0]


def simple(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


def comprobar(region, elf):
    s = geometria.GeometriaCell(region, elf)
    c, filas = s.cpu, []
    def revisar(caso, esperado, obtenido):
        limpio = not c.io and not c.sin_puente
        filas.append({'caso': caso, 'igual': esperado == obtenido and limpio,
                      'esperado': esperado, 'obtenido': obtenido,
                      'io': dict(c.io), 'sin_puente': dict(c.sin_puente)})
    # ABI soft-double: argumentos de 64 bits en a0/a1. Ee.call normaliza
    # sus args a 32 bits; preparar GPR completos y llamar sin *args.
    ops = (('suma', 0x119F80, lambda a, b: a + b),
           ('resta', 0x119FD8, lambda a, b: a - b),
           ('producto', 0x11A040, lambda a, b: a * b),
           ('division', 0x11A290, lambda a, b: a / b))
    valores = (-8, -2, -.5, .125, .5, 1, 2, 8)
    for nombre, addr, fn in ops:
        for a in valores:
            for b in valores:
                c.io.clear(); c.sin_puente.clear()
                c.r[4], c.r[5] = doble(a), doble(b)
                got = c.call(addr, limite=100000) & ee.M64
                revisar(f'{nombre}:{a}:{b}', doble(fn(a, b)), got)
    for v in valores:
        c.io.clear(); c.sin_puente.clear()
        # ELF: SWC1 f12 en extender, retorno float en f0 al truncar.
        c.f[12] = simple(v)
        revisar(f'float_a_double:{v}', doble(v), c.call(s.por_rol['extender']) & ee.M64)
        c.io.clear(); c.sin_puente.clear()
        c.r[4] = doble(v)
        c.call(s.por_rol['truncar'])
        revisar(f'double_a_float:{v}', simple(v), c.f[0])
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Soft-double {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return {'region': region, 'elf_sha256': s.digest, 'iguales': iguales,
            'total': len(filas), 'primera_diferencia': primera,
            'correcto': iguales == len(filas), 'casos': filas, 'helpers_rangos': s.helpers}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=geometria.carga.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=geometria.carga.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida debe ser nueva y su padre debe existir')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump({'operacion': 'soft_double_cell', 'pares_dorado_real': 0,
                       'hardware_verificado': False, 'integrado_cpp': False,
                       'alcance': 'potencias de dos: + - * / y conversion float/double exactas',
                       'extension_local': 'PMFHL.LW solo en esta instancia EE',
                       'limites': ['sin NaN, infinitos, subnormales, overflow ni redondeos no exactos',
                                   'sin validacion trigonometrica ni de vectores de proyectil'],
                       'regiones': regiones}, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

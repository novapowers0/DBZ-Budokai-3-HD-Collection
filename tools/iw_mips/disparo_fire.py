# SPDX-License-Identifier: MIT
"""F1: entrada Fire0->espera1->Fire2, temporizador/camaras originales.

No ejecuta geometria ni impacto Fire2. Actuacion/efectos/luz/voz/boca son
fronteras tipadas observadas, no implementaciones reales del mundo.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_esperas as esperas
import disparo_mira as mira
import disparo_fases as carga
import ee

bits = carga.bits
CTRL = 0x1007000
ROLES = ('hud', 'tiempo', 'camara', 'restante', 'camara', 'camara', 'reset',
         'vacio', 'pantalla', 'voz', 'timer', 'par', 'flags', 'objeto',
         'actuar', 'timer', 'camptr', 'viewdir', 'efecto', 'luz', 'transform',
         'vozmini', 'boca', 'tick', 'actuar', 'timer', 'transform', 'camptr',
         'viewdir', 'efecto', 'camara', 'normalizar', 'viewdir', 'normalizar',
         'dot', 'extender', 'acos', 'truncar', 'norma', 'sin', 'escalar',
         'efecto', 'landing', 'fatiga', 'target', 'luz', 'luz', 'luz', 'luz',
         'luz', 'luz', 'luz', 'luz', 'tick', 'mundo', 'luz')


class FireEntradaCell(esperas.EsperasCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        c, d = self.cpu, self.d
        body = c.m[d['fire']:d['mira']]
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        # USA no contiene el JAL de fatiga posterior a consumir energia.
        # Conservar perfiles distintos; no desplazar por ordinal el resto.
        roles = ROLES if region == 'eu' else ROLES[:43] + ROLES[44:]
        if len(calls) != len(roles):
            raise ValueError('frontera completa de Fire distinta del perfil')
        for addr, role in zip(calls, roles):
            if addr in self.roles and self.roles[addr] != role:
                raise ValueError('roles Fire contradictorios')
            self.roles[addr] = role
        self.por_rol = {role: addr for addr, role in self.roles.items()}
        if self.por_rol['camara'] != mira.PERFILES[region]['camara']:
            raise ValueError('getter camara no coincide con banco de mira')
        # El getter de Ctrl+0x80 tiene un lw v0,imm(gp); derivar el global,
        # sin asumir que los dos controladores compartan identidad en el juego.
        w = c.r32(self.por_rol['camptr'])
        if w >> 26 != 0x23 or (w >> 21 & 31) != 28 or (w >> 16 & 31) != 2:
            raise ValueError('getter Ctrl+0x80 sin carga GP verificable')
        imm = w & 0xFFFF
        self.ctrlp = c.gp + imm - (0x10000 if imm & 0x8000 else 0)
        self.camp = mira.PERFILES[region]['camp']
        for role in ('camara', 'camptr', 'viewdir', 'timer'):
            addr = self.por_rol[role]
            c.hooks.pop(addr, None)
            c.whitelist.add(addr)
        for role in ('actuar', 'efecto', 'transform', 'vozmini', 'boca'):
            c.hooks[self.por_rol[role]] = self._entrada_servicio(role)

    def _entrada_servicio(self, role):
        def hook(c):
            if role == 'efecto':
                ident, tabla = c.r[4] & ee.M32, c.r[5] & ee.M32
                if ident != 2 or not ee.PILA <= tabla <= ee.PILA + 0x100000 - 8:
                    raise RuntimeError('solo efecto2 con tabla propia de pila')
                pos, direccion = c.r32(tabla), c.r32(tabla + 4)
                if pos != CTRL + 0x80 or not ee.PILA <= direccion <= ee.PILA + 0x100000 - 16:
                    raise RuntimeError('vectores de efecto2 ajenos al banco')
                self.eventos.append({'tipo': role, 'id': ident,
                    'pos_bits': [c.r32(pos + i * 4) for i in range(4)],
                    'dir_bits': [c.r32(direccion + i * 4) for i in range(4)]})
            else:
                n = {'actuar': 3, 'transform': 0, 'vozmini': 1, 'boca': 3}[role]
                args = [c.r[4 + i] & ee.M32 for i in range(n)]
                if role == 'boca':
                    if args[0] != self.mgr + 0x800:
                        raise RuntimeError('boca recibe objeto de otro gestor')
                    args[0] -= self.mgr
                self.eventos.append({'tipo': role, 'args': args})
            c.r[2] = 0
        return hook

    def preparar_entrada(self, energia=bits(.5), x=0, y=400, z=200):
        if x not in (-111, 0, 74) or y not in (200, 400, 800) or z not in (60, 200, 280):
            raise ValueError('camara fuera de las muestras discretas del banco')
        super().preparar(energia)
        c, d = self.cpu, self.d
        c.m[CTRL:CTRL + 0x100] = bytes(0x100)
        c.w32(self.ctrlp, CTRL); c.w32(self.camp, self.cam)
        for i, v in enumerate((x, y, z, 1, x, 0, z - 200, 1)):
            c.w32(self.cam + 0x80 + i * 4, bits(v))
        for i, v in enumerate((1, 2, 3, 1)):
            c.w32(CTRL + 0x80 + i * 4, bits(v))
        c.w32(self.task + 0x1C, d['fire'])
        c.w32(self.task + 0x30, bits(-7)); c.w32(self.task + 0x34, 0)
        c.w32(self.task + 0x38, bits(7))

    def entrar(self):
        c, d = self.cpu, self.d
        if not self.preparado or c.r32(self.task + 0x1C) != d['fire'] or c.r32(self.task + 0x34) != 0:
            raise ValueError('solo fase Fire0 preparada')
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.call(d['fire'], self.task, limite=5000)
        if c.io or c.sin_puente:
            raise RuntimeError('Fire0 con E/S o llamada sin cubrir')
        return {'reloj_bits': c.r32(self.task + 0x30), 'luces_bits': c.r32(self.task + 0x38),
                'fase': c.r32(self.task + 0x34), 'mira': c.r32(self.task + 0x1C) == d['mira'],
                'energia_bits': c.r32(self.ctx + 4), 'eventos': self.eventos.copy(),
                'llamadas': dict(Counter(self.roles[a] for a in c.llamadas.elements()))}


def esperado(energia, y):
    return {'reloj_bits': bits(32), 'luces_bits': bits(7), 'fase': 1,
            'mira': False, 'energia_bits': energia,
            'eventos': [{'tipo': 'actuar', 'args': [1, 32, 40]},
                        {'tipo': 'efecto', 'id': 2, 'pos_bits': [bits(v) for v in (1, 2, 3, 1)],
                         # Camera_GetViewDir usa vsub.xyz: conserva W del target.
                         'dir_bits': [bits(v) for v in (0, -y, -200, 1)]},
                        {'tipo': 'luz', 'id': 10}, {'tipo': 'transform', 'args': []},
                        {'tipo': 'vozmini', 'args': [8]}, {'tipo': 'boca', 'args': [0x800, 32, 0]}],
            'llamadas': {r: 1 for r in ('hud', 'tiempo', 'camara', 'actuar', 'timer',
                                       'camptr', 'viewdir', 'efecto', 'luz', 'transform', 'vozmini', 'boca')}}


def comprobar(region, elf):
    s, filas, continuos = FireEntradaCell(region, elf), [], []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    for energia in (bits(.1), bits(.5), bits(1)):
        for x in (-111, 0, 74):
            for y in (200, 400, 800):
                for z in (60, 200, 280):
                    s.preparar_entrada(energia, x, y, z)
                    revisar(f'entrada:{energia:x}:{x}:{y}:{z}', esperado(energia, y), s.entrar())
    for paso, ticks in ((1, 33), (.5, 65), (1.2, 27), (2, 17)):
        s.preparar_entrada()
        revisar(f'continuo:{paso}:entrada', esperado(bits(.5), 400), s.entrar())
        reloj, luces, first = bits(32), bits(7), None
        for n in range(100):
            want, got = esperas.esperado(1, reloj, luces, bits(paso)), s.pasar(bits(paso))
            revisar(f'continuo:{paso}:{n}', want, got)
            if want != got and first is None:
                first = n
            # Mantener reloj/luces esperados separados del original persistente.
            reloj, luces = want['reloj_bits'], want['luces_bits']
            if got['fase'] == 2:
                break
        revisar(f'continuo:{paso}:duracion', ticks, n + 1)
        continuos.append({'paso_bits': bits(paso), 'ticks_espera': n + 1,
                          'ticks_esperados': ticks, 'primera_diferencia_tick': first})
    for nombre, fn in (
        ('no reentrada desde Fire2', lambda: s.entrar()),
        ('no ejecutar geometria Fire2', lambda: s.pasar()),
        ('camara no muestreada', lambda: s.preparar_entrada(x=1))):
        try:
            fn()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(nombre, True, rechazo)
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Entrada Fire Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return {'region': region, 'elf_sha256': s.digest, 'cuerpo_sha256': s.cuerpo_sha256,
            'ctrlp': s.ctrlp, 'iguales': iguales, 'total': len(filas),
            'primera_diferencia': primera, 'correcto': iguales == len(filas),
            'continuos': continuos, 'casos': filas}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=carga.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=carga.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    args = p.parse_args()
    start = time.monotonic()
    try:
        if args.salida.exists() or not args.salida.parent.is_dir():
            raise ValueError('salida debe ser nueva y su padre debe existir')
        regiones = [comprobar(r, e) for r, e in [('eu', args.elf_eu), ('us', args.elf_us)]]
        report = {'operacion': 'entrada_fire_cell', 'alcance': 'Fire0->espera1->Fire2 sin ejecutar geometria Fire2',
                  'referencia_continua': 'estado esperado propio, sin realimentar salidas observadas',
                  'pares_dorado_real': 0, 'integrado_cpp': False,
                  'FPU_HW': sorted(ee.FPU_HW), 'FPU_CERCANO': ee.FPU_CERCANO,
                  'limites': ['efectos/luz/actuacion/voz/boca/HUD/tiempo son servicios controlados',
                              'sin Fire2, geometria de proyectil, impacto ni consumo energetico',
                              'camara discreta; no prueba general de FPU/VU ni imagen real',
                              'sin C++ ni juego ni dorados reales'],
                  'regiones': regiones, 'segundos_banco': round(time.monotonic() - start, 3)}
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

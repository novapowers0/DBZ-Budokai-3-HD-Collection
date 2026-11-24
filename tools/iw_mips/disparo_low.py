# SPDX-License-Identifier: MIT
"""F1: LowEnergyWait Cell EU/USA y continuo Aim->Low->Aim->Fire, sin juego.

Ejecuta LowEnergyWait, Aim, temporizadores y movimiento originales. Los
servicios UI/sonido/HUD son observaciones tipadas, no implementaciones reales.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_aim as aim
import disparo_fases as carga
import disparo_ready as ready
import ee

bits = aim.bits
ROLES = ('hud', 'tick', 'timer_ui', 'par', 'clear_flags', 'toggle_objetos',
         'sprite_flag', 'objeto', 'flag_global')


class LowCell(aim.AimCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        c, d = self.cpu, self.d
        d['paso'] = carga.PERFILES[region]['paso']
        body = c.m[d['low']:carga.PERFILES[region]['carga']]
        self.low_sha256 = hashlib.sha256(body).hexdigest()
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        if len(calls) != len(ROLES) or calls[1] != ready.PERFILES[region]['tick']:
            raise ValueError('frontera LowEnergyWait/Timer cambiada')
        for addr, role in zip(calls, ROLES):
            if addr in self.roles and self.roles[addr] != role:
                raise ValueError('roles LowEnergyWait contradictorios')
            self.roles[addr] = role
            if role != 'tick':
                c.hooks[addr] = self._puente(role)
        c.whitelist.update((d['low'], calls[1]))

    def _puente(self, role):
        if role not in ('clear_flags', 'toggle_objetos', 'sprite_flag'):
            return super()._puente(role)
        def hook(c):
            args = [] if role == 'sprite_flag' else [c.r[4] & ee.M32]
            self.eventos.append({'tipo': role, 'args': args})
            c.r[2] = 0
        return hook

    def preparar_low(self, reloj=bits(220)):
        if not ready.finito(reloj):
            raise ValueError('reloj finito requerido')
        self.preparar_aim(bits(.05), False)
        if self.pasar_aim()['estado'] != 'low':
            raise RuntimeError('Aim no entro en LowEnergyWait')
        self.cpu.w32(aim.TASK + 0x30, reloj)

    def pasar_low(self, paso=bits(1)):
        c, d = self.cpu, self.d
        if not self.preparado or c.r32(aim.TASK + 0x1C) != d['low']:
            raise ValueError('solo se entra desde LowEnergyWait')
        if not carga.normal_positivo(paso):
            raise ValueError('paso float32 normal positivo requerido')
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.w32(d['paso'], paso)
        c.call(d['low'], aim.TASK, limite=5000)
        if c.io or c.sin_puente:
            raise RuntimeError('LowEnergyWait con E/S o servicio sin cubrir')
        estado = {d['low']: 'low', d['aim']: 'aim'}.get(c.r32(aim.TASK + 0x1C))
        if estado is None:
            raise RuntimeError('estado LowEnergyWait/Aim inesperado')
        return {'estado': estado, 'reloj_bits': c.r32(aim.TASK + 0x30),
                'energia_bits': c.r32(aim.CTX + 4), 'usada': c.r32(aim.CTX + 8),
                'subfase': c.r32(aim.TASK + 0x34),
                'vectores_bits': [c.r32(aim.mira.CAM + 0x80 + i * 4) for i in range(8)],
                'eventos': self.eventos.copy(),
                'llamadas': dict(Counter(self.roles[a] for a in c.llamadas.elements()))}


def esperado(reloj, paso, energia=bits(.05), vectores=None):
    nuevo = ee.fpu_h('sub', reloj, paso)
    fin, eventos = ee.fv(nuevo) < 0, []
    if fin:
        eventos = [{'tipo': role, 'args': args} for role, args in (
            ('timer_ui', [1]), ('par', [1, 1]), ('clear_flags', [8]),
            ('toggle_objetos', [0]), ('sprite_flag', []), ('objeto', [11, 0]),
            ('flag_global', [1]))]
    llamadas = Counter(hud=1, tick=1)
    llamadas.update(e['tipo'] for e in eventos)
    return {'estado': 'aim' if fin else 'low', 'reloj_bits': nuevo,
            'energia_bits': energia, 'usada': 1, 'subfase': 73,
            'vectores_bits': vectores or [bits(v) for v in (0, 400, 200, 1, 0, 0, 0, 1)],
            'eventos': eventos, 'llamadas': dict(llamadas)}


def comprobar(region, elf):
    s, filas, continuos = LowCell(region, elf), [], []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    for reloj in (-1, 0, .5, 1, 1.2, 219, 220):
        for paso in (.5, 1, 1.2, 2):
            s.preparar_low(bits(reloj))
            revisar(f'aislado:{reloj}:{paso}', esperado(bits(reloj), bits(paso)), s.pasar_low(bits(paso)))
    # Desde Aim NO se vuelve a preparar/reinyectar el contexto entre fases.
    for paso, ticks in ((1, 221), (.5, 441), (1.2, 184), (2, 111)):
        s.preparar_aim(bits(.05), False)
        vs = [bits(v) for v in (0, 400, 200, 1, 0, 0, 0, 1)]
        revisar(f'continuo:{paso}:Aim_entrada', aim.esperado(bits(.05), False, vs, 0, 0), s.pasar_aim())
        reloj, first = bits(220), None
        for n in range(500):
            want, got = esperado(reloj, bits(paso)), s.pasar_low(bits(paso))
            revisar(f'continuo:{paso}:Low:{n}', want, got)
            if want != got and first is None:
                first = n
            # La referencia debe recordar su reloj, no el obtenido del original.
            reloj = want['reloj_bits']
            if got['estado'] == 'aim':
                break
        revisar(f'continuo:{paso}:duracion', ticks, n + 1)
        continuos.append({'paso_bits': bits(paso), 'ticks_low': n + 1,
                          'ticks_esperados': ticks, 'primera_diferencia_tick': first})
        # Aim hereda energia y bandera; no reinicia espera y acepta Fire.
        want = aim.esperado(bits(.05), True, vs, 0x20, 0)
        want['reloj_bits'] = reloj
        got = s.pasar_aim(0x20, 0)
        revisar(f'continuo:{paso}:Aim_retorno', want, got)
        vs = want['vectores_bits']
        want = aim.esperado(bits(.05), True, vs, 0, 0x2000)
        want['reloj_bits'] = reloj
        revisar(f'continuo:{paso}:Fire', want, s.pasar_aim(0, 0x2000))
    for nombre, fn in (
        ('reentrada desde Fire', lambda: s.pasar_low()),
        ('NaN', lambda: s.preparar_low(0x7FC00000)),
        ('cero', lambda: (s.preparar_low(), s.pasar_low(0))),
        ('subnormal', lambda: (s.preparar_low(), s.pasar_low(1))),
        ('negativo', lambda: (s.preparar_low(), s.pasar_low(bits(-1))))):
        try:
            fn()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(nombre, True, rechazo)
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'LowEnergyWait Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return {'region': region, 'elf_sha256': s.digest, 'cuerpo_sha256': s.low_sha256,
            'iguales': iguales, 'total': len(filas), 'primera_diferencia': primera,
            'correcto': iguales == len(filas), 'continuos': continuos, 'casos': filas}


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
        report = {'operacion': 'low_cell', 'alcance': 'Aim->LowEnergyWait->Aim->Fire sin resincronizacion de estado',
                  'referencia_continua': 'estado esperado propio, sin realimentar salidas observadas',
                  'pares_dorado_real': 0, 'integrado_cpp': False,
                  'FPU_HW': sorted(ee.FPU_HW), 'FPU_CERCANO': ee.FPU_CERCANO,
                  'limites': ['HUD/UI/sonido son servicios simulados',
                              'sin Fire completo, impacto, mundo/RNG, caducidad ni derrota',
                              'esperado usa aritmetica EE, no valida independientemente FPU',
                              'sin C++ ni juego ni dorado real'],
                  'regiones': regiones, 'segundos_banco': round(time.monotonic() - start, 3)}
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

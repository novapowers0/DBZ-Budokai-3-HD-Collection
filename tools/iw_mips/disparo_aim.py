# SPDX-License-Identifier: MIT
"""F1: Aim Cell vivo, sin carga, con movimiento digital y Timer_SetSlot originales.

Comprueba permanecer en Aim, pasar a Fire y entrar en LowEnergyWait. Excluye
Square/carga, palancas, caducidad y derrota. Servicios externos son puentes
tipados; camara/movimiento/getter/setter/vector y Timer_SetSlot se ejecutan.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_mira as mira
import disparo_fases as carga
import disparo_ready as ready
import ee

CTX, MGR, TASK = 0x1401000, 0x1402000, 0x1407000
bits = mira.bits
ROLES = ('hud', 'mover', 'tiempo', 'camara', 'restante', 'camara', 'camara',
         'reset', 'vacio', 'pantalla', 'voz', 'timer', 'par', 'flags', 'objeto',
         'extender', 'comparar', 'transform', 'vozmini', 'target', 'camptr',
         'camptr', 'viewdir', 'efecto', 'timer', 'timer_ui', 'par', 'flags',
         'objetos', 'sprite', 'objeto', 'vozmini', 'flag_global')
PARAMETROS = {'timer_ui': 1, 'par': 2, 'flags': 1, 'objetos': 2,
              'sprite': 1, 'objeto': 2, 'vozmini': 1, 'flag_global': 1}


class AimCell(mira.MiraCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        self.d = d = dict(self.d, **{k: carga.PERFILES[region][k] for k in ('ctxp', 'mgrp')})
        d.update(aim=carga.PERFILES[region]['mira'], fin_aim=ready.PERFILES[region]['ready'],
                 fire=carga.PERFILES[region]['fin'], low=0x30A530 if region == 'eu' else 0x30A310)
        c = self.cpu
        body = c.m[d['aim']:d['fin_aim']]
        self.aim_sha256 = hashlib.sha256(body).hexdigest()
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        if len(calls) != len(ROLES) or calls[1] != d['mover'] or calls[3] != d['camara']:
            raise ValueError('frontera JAL de Aim cambiada')
        for addr, role in zip(calls, ROLES):
            if addr in self.roles and self.roles[addr] != role:
                raise ValueError('roles de Aim contradictorios')
            self.roles[addr] = role
        d['timer'] = calls[11]
        for addr, role in self.roles.items():
            if role in ('mover', 'camara', 'trasladar', 'vector', 'timer'):
                continue
            c.hooks[addr] = self._puente(role)
        c.whitelist.update((d['aim'], d['timer']))
        self.eventos = []

    def _puente(self, role):
        def hook(c):
            if role in ('hud', 'tiempo'):
                if role == 'tiempo' and c.r[4] & ee.M32 != TASK:
                    raise RuntimeError('CheckTimeUp con tarea ajena')
                c.r[2] = 0
            elif role in PARAMETROS:
                args = [c.r[4 + i] & ee.M32 for i in range(PARAMETROS[role])]
                # UI_SetObjectsField158 recibe su valor float por f12 y los
                # dos indices por a0/a1. a2 es residual, no un tercer indice.
                if role == 'objetos':
                    args.insert(0, c.f[12])
                self.eventos.append({'tipo': role,
                    'args': args})
                c.r[2] = 0
            else:
                raise RuntimeError(f'servicio Aim fuera de dominio vivo/sin carga: {role}')
        return hook

    def preparar_aim(self, energia, usada=False, x=0.0, z=200.0):
        if not carga.normal_positivo(energia) or ee.fv(energia) > 1 or type(usada) is not bool:
            raise ValueError('energia normal en (0,1] y usada bool requeridos')
        super().preparar(x, z)
        c, d = self.cpu, self.d
        for addr, n in ((CTX, 0x100), (MGR, 0x4000), (TASK, 0x100)):
            c.m[addr:addr + n] = bytes(n)
        c.w32(d['ctxp'], CTX); c.w32(d['mgrp'], MGR)
        c.w32(CTX + 4, energia); c.w32(CTX + 8, int(usada))
        c.w32(MGR + 0x2914, bits(100))
        c.w32(TASK + 0x1C, d['aim']); c.w32(TASK + 0x30, bits(32))
        c.w32(TASK + 0x34, 73)

    def pasar_aim(self, mando=0, nuevo=0):
        c, d = self.cpu, self.d
        if not self.preparado or c.r32(TASK + 0x1C) != d['aim']:
            raise ValueError('solo se entra desde Aim')
        if type(mando) is not int or mando < 0 or mando & ~0xF0 or type(nuevo) is not int or nuevo not in (0, 0x2000):
            raise ValueError('solo cruceta digital y nueva pulsacion Fire; carga excluida')
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.w16(d['mando'], mando); c.w16(d['mando'] + 2, nuevo); c.w16(d['mando2'], 0)
        c.call(d['aim'], TASK, limite=10000)
        if c.io or c.sin_puente:
            raise RuntimeError('Aim con E/S o llamada no cubierta')
        estados = {d['aim']: 'aim', d['fire']: 'fire', d['low']: 'low'}
        estado = estados.get(c.r32(TASK + 0x1C))
        if estado is None:
            raise RuntimeError('Aim paso a un estado no cubierto')
        return {'estado': estado, 'energia_bits': c.r32(CTX + 4), 'usada': c.r32(CTX + 8),
                'reloj_bits': c.r32(TASK + 0x30), 'subfase': c.r32(TASK + 0x34),
                'vectores_bits': [c.r32(mira.CAM + 0x80 + 4 * i) for i in range(8)],
                'eventos': self.eventos.copy(),
                'llamadas': dict(Counter(self.roles[a] for a in c.llamadas.elements()))}


def esperado(energia, usada, vectores, mando, nuevo):
    movimiento = mira.esperado(vectores, mando, 0)
    llamadas = Counter(movimiento['llamadas'])
    llamadas.update(hud=1, mover=1, tiempo=1, camara=1)
    estado, reloj, subfase, eventos = 'aim', bits(32), 73, []
    if usada or ee.fv(energia) > ee.fv(bits(.1)):
        if nuevo:
            estado, subfase = 'fire', 0
    else:
        estado, usada, reloj = 'low', True, bits(220)
        for role, args in [('timer_ui', [3]), ('par', [0, 0]), ('flags', [8]),
                           ('objetos', [0, 0x15, 0x16]), ('sprite', [0]),
                           ('objeto', [0xB, 1]), ('vozmini', [7]), ('flag_global', [0])]:
            eventos.append({'tipo': role, 'args': args})
            llamadas[role] += 1
        llamadas['timer'] += 1
    return {'estado': estado, 'energia_bits': energia, 'usada': int(usada),
            'reloj_bits': reloj, 'subfase': subfase, 'vectores_bits': movimiento['vectores_bits'],
            'eventos': eventos, 'llamadas': dict(llamadas)}


def comprobar(region, elf):
    s, filas = AimCell(region, elf), []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    energias = [bits(.001), bits(.1) - 1, bits(.1), bits(.1) + 1, bits(.5), bits(1)]
    for energia in energias:
        for usada in (False, True):
            for nuevo in (0, 0x2000):
                for mando in (0, 0x20, 0x80, 0x10, 0x40, 0xF0):
                    for x, z in ((0.0, 200.0), (-111.0, 60.0), (74.0, 280.0)):
                        s.preparar_aim(energia, usada, x, z)
                        vs = [bits(v) for v in (x, 400, z, 1, x, 0, z - 200, 1)]
                        revisar(f'aislado:{energia:x}:{usada}:{nuevo:x}:{mando:x}:{x}:{z}',
                                esperado(energia, usada, vs, mando, nuevo), s.pasar_aim(mando, nuevo))
    continuos = []
    for energia, usada, n, nuevo in ((bits(.5), False, 80, 0x2000), (bits(.05), True, 40, 0x2000)):
        s.preparar_aim(energia, usada)
        vs, primera = [bits(v) for v in (0, 400, 200, 1, 0, 0, 0, 1)], None
        for tick in range(n):
            mando = 0x20 if tick < n // 2 else 0x80
            pulsado = nuevo if tick == n - 1 else 0
            want = esperado(energia, usada, vs, mando, pulsado)
            got = s.pasar_aim(mando, pulsado)
            revisar(f'continuo:{energia:x}:{tick}', want, got)
            if want != got and primera is None:
                primera = tick
            # La trayectoria esperada no adopta errores del interprete.
            vs = want['vectores_bits']
        continuos.append({'energia_bits': energia, 'usada': usada, 'ticks': n,
                          'primera_diferencia_tick': primera, 'final': got['estado']})
        try:
            s.pasar_aim()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(f'continuo:{energia:x}:rechaza_Fire', True, rechazo)
    for nombre, fn in (
        ('energia cero', lambda: s.preparar_aim(0)),
        ('energia NaN', lambda: s.preparar_aim(0x7FC00000)),
        ('Square excluido', lambda: (s.preparar_aim(bits(.5)), s.pasar_aim(0x8000))),
        ('nuevo invalido', lambda: (s.preparar_aim(bits(.5)), s.pasar_aim(0, 1)))):
        try:
            fn()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(nombre, True, rechazo)
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Aim Cell vivo/sin carga {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return {'region': region, 'elf_sha256': s.digest, 'cuerpo_sha256': s.aim_sha256,
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
        report = {'operacion': 'aim_cell', 'alcance': 'Aim vivo sin carga; movimiento y temporizador originales, servicios controlados',
                  'referencia_continua': 'estado esperado propio, sin realimentar salidas observadas',
                  'pares_dorado_real': 0, 'integrado_cpp': False,
                  'FPU_HW': sorted(ee.FPU_HW), 'FPU_CERCANO': ee.FPU_CERCANO,
                  'limites': ['sin Square/carga, stick, caducidad ni derrota',
                              'HUD/tiempo/UI/sonido son servicios simulados',
                              'sin impacto, Fire completo ni cierre; sin C++/juego'],
                  'regiones': regiones, 'segundos_banco': round(time.monotonic() - start, 3)}
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

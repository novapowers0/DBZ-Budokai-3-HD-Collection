# SPDX-License-Identifier: MIT
"""F1: fases de ESPERA de Fire Cell EU/USA con temporizador original.

Solo fases internas 1 (antes del disparo) y 3 (despues). Fases 0/2 y su
geometria/impacto no se simulan. HUD/CheckTimeUp/camara/luz/lectura de mundo
son fronteras controladas; Timer_TickSlot SI se ejecuta desde el ELF.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_fases as carga
import disparo_ready as ready
import ee

bits = carga.bits


class EsperasCell(carga.CargaCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        # Charge termina donde comienza Fire; Fire termina donde comienza Aim.
        self.d = dict(self.d, fire=self.d['fin'], tick=ready.PERFILES[region]['tick'])
        c, d = self.cpu, self.d
        body = c.m[d['fire']:d['mira']]
        self.cuerpo_sha256 = hashlib.sha256(body).hexdigest()
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        if len(calls) < 3 or calls[-3] != d['tick']:
            raise ValueError('frontera final de Fire/Timer cambiada')
        self.servicios = {'hud': next(a for a, role in self.puentes.items() if role == 'hud'),
                          'tiempo': next(a for a, role in self.puentes.items() if role == 'tiempo'),
                          'camara': next(a for a, role in self.puentes.items() if role == 'camara'),
                          'luz': calls[-1], 'mundo': calls[-2], 'tick': d['tick']}
        if len(set(self.servicios.values())) != len(self.servicios):
            raise ValueError('roles de Fire ambiguos')
        self.roles = {a: role for role, a in self.servicios.items()}
        c.hooks.clear()
        def rechazo(cpu):
            raise RuntimeError('servicio fuera de las fases Fire 1/3; no inventar retorno')
        for addr in calls:
            if addr != d['tick']:
                c.hooks[addr] = rechazo
        for role, addr in self.servicios.items():
            if role != 'tick':
                c.hooks[addr] = self._servicio(role)
        c.whitelist = {d['fire'], d['tick']}

    def _servicio(self, role):
        def hook(c):
            if role == 'tiempo' and c.r[4] & ee.M32 != self.task:
                raise RuntimeError('CheckTimeUp con tarea ajena')
            if role == 'luz':
                self.eventos.append({'tipo': role, 'id': c.r[4] & ee.M32})
            elif role == 'mundo':
                self.eventos.append({'tipo': role})
            c.r[2] = self.cam if role == 'camara' else 0
        return hook

    def preparar_espera(self, fase, reloj, luces=bits(0.0)):
        if type(fase) is not int or fase not in (1, 3) or not ready.finito(reloj) or not ready.finito(luces) or not 0 <= ee.fv(luces) < 10000:
            raise ValueError('solo fases Fire 1/3 con reloj finito y luces en [0,10000)')
        super().preparar(bits(.5))
        c, d = self.cpu, self.d
        c.w32(self.task + 0x1C, d['fire'])
        c.w32(self.task + 0x30, reloj)
        c.w32(self.task + 0x34, fase)
        c.w32(self.task + 0x38, luces)

    def pasar(self, paso=bits(1.0)):
        c, d = self.cpu, self.d
        if not self.preparado or c.r32(self.task + 0x1C) != d['fire'] or c.r32(self.task + 0x34) not in (1, 3):
            raise ValueError('solo se entra desde espera Fire 1/3')
        if not carga.normal_positivo(paso):
            raise ValueError('paso float32 normal positivo requerido')
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.w32(d['paso'], paso)
        c.call(d['fire'], self.task, limite=5000)
        if c.io or c.sin_puente:
            raise RuntimeError('Fire con E/S o llamada sin cubrir')
        estado = c.r32(self.task + 0x1C)
        if estado not in (d['fire'], d['mira']):
            raise RuntimeError('estado Fire/Aim inesperado')
        return {'reloj_bits': c.r32(self.task + 0x30), 'luces_bits': c.r32(self.task + 0x38),
                'fase': c.r32(self.task + 0x34), 'mira': estado == d['mira'],
                'energia_bits': c.r32(self.ctx + 4), 'eventos': self.eventos.copy(),
                'llamadas': dict(Counter(self.roles[a] for a in c.llamadas.elements()))}


def esperado(fase, reloj, luces, paso):
    nuevo = ee.fpu_h('sub', reloj, paso)
    fin = ee.fv(nuevo) < 0
    eventos = []
    llamadas = {'hud': 1, 'tiempo': 1, 'camara': 1, 'tick': 1}
    if fase == 3:
        n = int(ee.fv(luces))
        if 21 <= n <= 28:
            eventos.append({'tipo': 'luz', 'id': 28 - n})
        luces = ee.fpu_h('add', luces, paso)
        if fin:
            eventos.extend([{'tipo': 'mundo'}, {'tipo': 'luz', 'id': 0}])
    if eventos:
        llamadas.update(Counter(e['tipo'] for e in eventos))
    return {'reloj_bits': nuevo, 'luces_bits': luces,
            'fase': 2 if fase == 1 and fin else fase, 'mira': fase == 3 and fin,
            'energia_bits': bits(.5), 'eventos': eventos, 'llamadas': llamadas}


def comprobar(region, elf):
    s = EsperasCell(region, elf)
    filas, continuos = [], []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    for fase in (1, 3):
        for reloj in (-1, 0, .5, 1, 1.2, 32):
            for paso in (.5, 1, 1.2, 2):
                for luces in ((0, 21) if fase == 1 else (0, 19, 20, 20.5, 21, 22, 23, 24, 25, 26, 27, 28, 29)):
                    s.preparar_espera(fase, bits(reloj), bits(luces))
                    revisar(f'aislado:{fase}:{reloj}:{paso}:{luces}',
                            esperado(fase, bits(reloj), bits(luces), bits(paso)), s.pasar(bits(paso)))
    for fase in (1, 3):
        for paso, ticks in ((1, 33), (.5, 65), (1.2, 27), (2, 17)):
            s.preparar_espera(fase, bits(32))
            reloj, luces, primera = bits(32), bits(0), None
            for n in range(100):
                want, got = esperado(fase, reloj, luces, bits(paso)), s.pasar(bits(paso))
                revisar(f'continuo:{fase}:{paso}:{n}', want, got)
                if want != got and primera is None:
                    primera = n
                # No resincronizar la referencia con el estado observado.
                reloj, luces = want['reloj_bits'], want['luces_bits']
                if got['mira'] or got['fase'] == 2:
                    break
            revisar(f'continuo:{fase}:{paso}:duracion', ticks, n + 1)
            continuos.append({'fase': fase, 'paso_bits': bits(paso), 'ticks': n + 1,
                              'ticks_esperados': ticks, 'primera_diferencia_tick': primera})
            try:
                s.pasar(bits(paso))
                rechazo = False
            except ValueError:
                rechazo = True
            revisar(f'continuo:{fase}:{paso}:no_reentrar', True, rechazo)
    for nombre, fn in (
        ('fase 0 excluida', lambda: s.preparar_espera(0, bits(32))),
        ('fase 2 excluida', lambda: s.preparar_espera(2, bits(32))),
        ('reloj NaN', lambda: s.preparar_espera(1, 0x7FC00000)),
        ('luces negativas', lambda: s.preparar_espera(3, bits(32), bits(-1))),
        ('paso cero', lambda: (s.preparar_espera(1, bits(32)), s.pasar(0)))):
        try:
            fn()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(nombre, True, rechazo)
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Esperas Fire Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    for f in continuos:
        print(f'  fase={f["fase"]}, paso={ee.fv(f["paso_bits"])}: {f["ticks"]} ticks, '
              f'primer DIF {f["primera_diferencia_tick"]}')
    return {'region': region, 'elf_sha256': s.digest, 'cuerpo_sha256': s.cuerpo_sha256,
            'servicios': s.servicios, 'iguales': iguales, 'total': len(filas),
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
        report = {'operacion': 'esperas_fire_cell', 'alcance': 'Fire fases 1/3 con Timer_TickSlot original y mundo controlado',
                  'referencia_continua': 'estado esperado propio, sin realimentar salidas observadas',
                  'pares_dorado_real': 0, 'integrado_cpp': False,
                  'FPU_HW': sorted(ee.FPU_HW), 'FPU_CERCANO': ee.FPU_CERCANO,
                  'limites': ['sin Fire fases 0/2, geometria, impacto ni consumo de energia',
                              'luz/mundo/HUD/tiempo/camara son servicios simulados',
                              'esperado usa aritmetica EE; no prueba independiente de FPU',
                              'sin C++ ni juego; no certifica duracion completa del disparo'],
                  'regiones': regiones, 'segundos_banco': round(time.monotonic() - start, 3)}
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

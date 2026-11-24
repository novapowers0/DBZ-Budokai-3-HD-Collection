# SPDX-License-Identifier: MIT
"""F1: Ready de Cell EU/USA con Timer_TickSlot original, estrictamente offline.

Ejecuta los cuerpos originales de Ready y del temporizador. HUD, pose, camara,
voz y flags externos se observan mediante puentes tipados: no se ejecutan ni
se certifican esos servicios. No integra Ready al juego ni sustituye un dorado.
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

MGR, TASK = 0x1300000, 0x1305000
ROLES = ('hud', 'tick', 'pose', 'camara', 'voz', 'flag', 'timer_ui')
PERFILES = {
    'eu': {'elf': banco.ELF_EU, 'gp': 0x4A3DF0, 'ready': 0x30B110,
           'fin': 0x30B1C0, 'mira': 0x30ADE0, 'mgrp': 0x49CA70,
           'paso': 0x49C7DC, 'tick': 0x2EF990, 'tick_fin': 0x2EF9D0},
    'us': {'elf': banco.ELF_US, 'gp': 0x4A35F0, 'ready': 0x30AEA0,
           'fin': 0x30AF50, 'mira': 0x30AB80, 'mgrp': 0x49C270,
           'paso': 0x49BFDC, 'tick': 0x2EF780, 'tick_fin': 0x2EF7C0}}


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def finito(value):
    return type(value) is int and 0 <= value <= ee.M32 and (value >> 23 & 255) != 255


class ReadyCell:
    def __init__(self, region, elf=None):
        self.d, self.region = PERFILES[region], region
        d = self.d
        mem, self.digest = banco.cargar_elf(elf or d['elf'])
        if self.digest != banco.ELF_HASH[region]:
            raise ValueError('ELF distinto del perfil regional verificado')
        self.identidad_cuerpos = {
            nombre: hashlib.sha256(mem[d[a]:d[b]]).hexdigest()
            for nombre, a, b in [('ready', 'ready', 'fin'), ('tick', 'tick', 'tick_fin')]}
        body = mem[d['ready']:d['fin']]
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        if len(calls) != len(ROLES) or calls[1] != d['tick']:
            raise ValueError('frontera de llamadas Ready/Timer distinta del perfil')
        self.roles = dict(zip(calls, ROLES))
        if len(self.roles) != len(ROLES):
            raise ValueError('destinos Ready ambiguos')
        self.cpu = ee.Ee(mem, d['gp'])
        self.cpu.whitelist = {d['ready'], d['tick']}
        self.cpu.estricto = True
        for addr, role in self.roles.items():
            if role != 'tick':
                self.cpu.hooks[addr] = self._puente(role)
        self.eventos = []
        self.preparado = False

    def _puente(self, role):
        def hook(c):
            if role == 'hud':
                self.eventos.append({'tipo': role})
            else:
                n = 2 if role in ('pose', 'voz') else 1
                args = [c.r[4 + i] & ee.M32 for i in range(n)]
                self.eventos.append({'tipo': role, 'args': args})
            c.r[2] = 0  # estos servicios no deciden la transicion; Timer se ejecuta
        return hook

    def preparar(self, reloj=bits(100.0), gestor=True):
        if not finito(reloj) or type(gestor) is not bool:
            raise ValueError('reloj finito float32 y gestor bool requeridos')
        c, d = self.cpu, self.d
        c.m[MGR:MGR + 0x4000] = bytes(0x4000)
        c.m[TASK:TASK + 0x100] = bytes(0x100)
        c.w32(d['mgrp'], MGR if gestor else 0)
        c.w8(MGR + 0x14CD, 0xA5)
        c.w8(MGR + 0x359D, 0x5A)
        c.w32(TASK + 0x1C, d['ready'])
        c.w32(TASK + 0x30, reloj)
        c.w32(TASK + 0x34, 0x12345678)  # slot vecino intacto
        self.gestor, self.preparado = gestor, True

    def pasar(self, paso=bits(1.0)):
        if not self.preparado or self.cpu.r32(TASK + 0x1C) != self.d['ready']:
            raise ValueError('solo se entra desde Ready')
        if not finito(paso) or ee.fv(paso) <= 0:
            raise ValueError('paso debe ser float32 normal positivo')
        c, d = self.cpu, self.d
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.w32(d['paso'], paso)
        c.call(d['ready'], TASK, limite=5000)
        if c.io or c.sin_puente:
            raise RuntimeError(f'E/S o llamada no cubierta: {dict(c.io)}, {dict(c.sin_puente)}')
        estado = c.r32(TASK + 0x1C)
        if estado not in (d['ready'], d['mira']):
            raise RuntimeError(f'estado Ready/Aim inesperado: {estado:#x}')
        return {'reloj_bits': c.r32(TASK + 0x30), 'mira': estado == d['mira'],
                'banderas': [c.r8(MGR + 0x14CD), c.r8(MGR + 0x359D)],
                'slot_vecino': c.r32(TASK + 0x34), 'eventos': self.eventos.copy(),
                'llamadas': dict(Counter(self.roles[a] for a in c.llamadas.elements()))}


def esperado(reloj, paso, gestor):
    # Regla literal Timer_TickSlot: restar, guardar y devolver reloj < 0 (NO <=).
    # Reutiliza la aritmetica EE: no es una prueba independiente de esa FPU.
    nuevo = ee.fpu_h('sub', reloj, paso)
    mira = ee.fv(nuevo) < 0
    eventos = [{'tipo': 'hud'}]
    llamadas = {'hud': 1, 'tick': 1}
    if mira:
        eventos.extend([
            {'tipo': 'pose', 'args': [(MGR if gestor else 0) + 0x28D0, 1]},
            {'tipo': 'camara', 'args': [1]},
            {'tipo': 'voz', 'args': [1, ee.M32]},
            {'tipo': 'flag', 'args': [1]},
            {'tipo': 'timer_ui', 'args': [1]}])
        llamadas.update(pose=1, camara=1, voz=1, flag=1, timer_ui=1)
    return {'reloj_bits': nuevo, 'mira': mira,
            'banderas': [1, 0] if mira and gestor else [0xA5, 0x5A],
            'slot_vecino': 0x12345678, 'eventos': eventos, 'llamadas': llamadas}


def comprobar(region, elf):
    s = ReadyCell(region, elf)
    filas = []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    # Cero exacto, vecinos de 1 y relojes negativos, ademas de la rama gestor nulo.
    relojes = [bits(v) for v in (-1.0, -0.0, 0.0, .5, 1.0, 1.2, 2.0, 100.0)]
    relojes += [bits(1.0) - 1, bits(1.0) + 1]
    for reloj in relojes:
        for paso in (.5, 1.0, 1.2, 2.0):
            for gestor in (False, True):
                s.preparar(reloj, gestor)
                revisar(f'aislado:{reloj:08x}:{paso}:{gestor}',
                        esperado(reloj, bits(paso), gestor), s.pasar(bits(paso)))
    continuos = []
    for paso, ticks_esperados in ((1.0, 101), (.5, 201), (1.2, 84), (2.0, 51)):
        s.preparar()
        reloj = bits(100.0)
        primera = None
        for n in range(400):
            want, got = esperado(reloj, bits(paso), True), s.pasar(bits(paso))
            revisar(f'continuo:{paso}:{n}', want, got)
            if want != got and primera is None:
                primera = n
            # El modelo conserva su trayectoria, incluso si el original difiere.
            reloj = want['reloj_bits']
            if got['mira']:
                break
        revisar(f'continuo:{paso}:duracion', ticks_esperados, n + 1)
        continuos.append({'paso_bits': bits(paso), 'ticks': n + 1,
                          'ticks_esperados': ticks_esperados, 'primera_diferencia_tick': primera})
        try:
            s.pasar(bits(paso))
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(f'continuo:{paso}:rechaza_reentrada_Aim', True, rechazo)
    for nombre, fn in (
        ('reloj NaN', lambda: s.preparar(0x7FC00000)),
        ('gestor no bool', lambda: s.preparar(gestor=1)),
        ('paso cero', lambda: (s.preparar(), s.pasar(0))),
        ('paso subnormal', lambda: (s.preparar(), s.pasar(1))),
        ('paso negativo', lambda: (s.preparar(), s.pasar(bits(-1.0)))),
        ('paso infinito', lambda: (s.preparar(), s.pasar(0x7F800000)))):
        try:
            fn()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(nombre, True, rechazo)
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Ready Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    for f in continuos:
        print(f'  paso={ee.fv(f["paso_bits"])}: {f["ticks"]} ticks hasta Aim, '
              f'primer DIF {f["primera_diferencia_tick"]}')
    return {'region': region, 'elf_sha256': s.digest,
            'perfil': {k: v for k, v in s.d.items() if k != 'elf'},
            'cuerpos_sha256': s.identidad_cuerpos, 'iguales': iguales, 'total': len(filas),
            'primera_diferencia': primera, 'correcto': iguales == len(filas),
            'continuos': continuos, 'casos': filas}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    args = p.parse_args()
    inicio = time.monotonic()
    try:
        if args.salida.exists() or not args.salida.parent.is_dir():
            raise ValueError('salida debe ser nueva y su padre debe existir')
        regiones = [comprobar(r, e) for r, e in [('eu', args.elf_eu), ('us', args.elf_us)]]
        report = {'operacion': 'ready_cell', 'alcance': 'Ready + Timer_TickSlot originales con mundo controlado',
                  'referencia_continua': 'estado esperado propio, sin realimentar salidas observadas',
                  'FPU_HW': sorted(ee.FPU_HW), 'FPU_CERCANO': ee.FPU_CERCANO,
                  'pares_dorado_real': 0, 'integrado_cpp': False,
                  'limites': ['HUD/pose/camara/voz/flags externos observados, no ejecutados',
                              'sin PreIntro/Intro, Aim/Charge/Fire ni cierre',
                              'esperado reutiliza aritmetica EE, no prueba independiente de FPU',
                              'el port no cambia; diferencia de duracion requiere alinear entrada real'],
                  'regiones': regiones, 'segundos_banco': round(time.monotonic() - inicio, 3)}
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

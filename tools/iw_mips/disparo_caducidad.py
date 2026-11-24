# SPDX-License-Identifier: MIT
"""F1: CheckTimeUp -> GameOver -> WaitResult -> entrada ApplyResult, offline.

Ejecuta getters/timer/caducidad/espera originales. UI/reset/voz/pose/audio
controlados; no ejecuta aun ApplyResult ni acredita premios o mundo real.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_derrota as derrota
import ee

bits = derrota.bits
PERFILES = {
    'eu': {'tabla': 0x5D93A8, 'apply': 0x30A200, 'camfin': 0x2ED8A0},
    'us': {'tabla': 0x5D8BA8, 'apply': 0x309FE0, 'camfin': 0x2ED690}}
ROLES = ('terminado', 'restante', 'camara', 'camara', 'camara', 'reset',
         'pantalla', 'voz', 'pantalla', 'voz', 'objeto', 'timer', 'par', 'flags', 'vacio')


class CaducidadCell(derrota.DerrotaCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        c = self.cpu
        self.q = PERFILES[region]
        self.tiempo = self.por_rol['tiempo']
        body = c.m[self.tiempo:self.por_rol['reset']]
        self.caducidad_sha256 = hashlib.sha256(body).hexdigest()
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        if len(calls) != len(ROLES):
            raise ValueError('numero de llamadas de CheckTimeUp distinto del perfil')
        for addr, role in zip(calls, ROLES):
            if addr in self.roles and self.roles[addr] != role:
                raise ValueError('roles de CheckTimeUp contradictorios')
            self.roles[addr] = role
        self.por_rol = {r: a for a, r in self.roles.items()}
        for addr in (self.tiempo, self.por_rol['terminado'], self.p['wait'], self.q['camfin']):
            c.hooks.pop(addr, None)
            c.whitelist.add(addr)
        calls_wait = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack(
            '<I', c.m[self.p['wait']:self.p['gameover']]) if w >> 26 == 3]
        if calls_wait != [self.q['camfin']]:
            raise ValueError('WaitResult no llama al getter de camara esperado')
        self.roles[self.q['camfin']] = 'camara_fin'
        self.coeficientes = [c.r32(self.q['tabla'] + 24 * i) for i in range(6)]
        if not all(derrota.geometria.carga.normal_positivo(v) for v in self.coeficientes):
            raise ValueError('tabla de coeficientes fuera del dominio positivo finito')
        self.wait_sha256 = hashlib.sha256(c.m[self.p['wait']:self.p['gameover']]).hexdigest()
        self.tabla_sha256 = hashlib.sha256(c.m[self.q['tabla']:self.q['tabla'] + 6 * 24]).hexdigest()

    def preparar_caducidad(self, dificultad, altura, estado_timer=2, restante=bits(0)):
        if type(dificultad) is not int or not 0 <= dificultad <= 5:
            raise ValueError('dificultad fuera de0..5')
        if not derrota.geometria.fire.esperas.ready.finito(altura):
            raise ValueError('altura no finita')
        if estado_timer not in (0, 1, 2, 3):
            raise ValueError('estado timer fuera de las muestras0..3')
        self.preparar_derrota(bits(.5))
        c = self.cpu
        c.w32(self.ctx + 0x1C, dificultad)
        c.w32(self.mgr + 0x2914, altura)
        c.w32(derrota.TIMER + 0x10, estado_timer)
        c.w32(derrota.TIMER + 0x14, restante)
        c.w32(self.cam + 0x168, bits(0)); c.w32(self.cam + 0x16C, bits(2))

    def caducar(self, desde_fire=False):
        if self.cpu.r32(self.task + 0x1C) != self.d['fire']:
            raise ValueError('CheckTimeUp solo desde el fixture Fire')
        r = self._llamar(self.d['fire'] if desde_fire else self.tiempo)
        if not desde_fire:
            r['retorno'] = self.cpu.r[2] & ee.M32
        return r

    def esperar_resultado(self):
        if self.cpu.r32(self.task + 0x1C) != self.p['wait']:
            raise ValueError('solo WaitResult preparado')
        return self._llamar(self.p['wait'])


def es_exito(ratio):
    return 0 <= ee.fv(ratio) < 1


def esperado(s, dificultad, altura, estado_timer, restante):
    terminado = estado_timer == 2
    r = {'estado': s.d['fire'], 'fase': 3, 'reloj_bits': bits(17), 'luces_bits': bits(23),
         'energia_bits': bits(.5), 'resultado': [bits(-17), bits(.75), bits(-17)],
         'flags': 0x80, 'par': [7, 7], 'gestor_flags': [0xA5, 0x5A], 'eventos': [],
         'retorno': 0}
    if terminado:
        denom = ee.fpu_h('mul', s.coeficientes[dificultad], bits(400))
        ratio = ee.fpu_h('div', altura, denom)
        exito = es_exito(ratio)
        r.update(estado=s.p['gameover'], reloj_bits=bits(140), flags=0x88, par=[0, 0], retorno=1,
                 resultado=[restante, ratio, bits(min(ee.fv(altura), 400))])
        r['eventos'] = [{'tipo': 'reset', 'args': [0]},
                        {'tipo': 'pantalla', 'args': [7 if exito else 8], 'f12_bits': bits(0)},
                        {'tipo': 'voz', 'args': [5 if exito else 13, ee.M32]}]
        if not exito:
            r['eventos'].append({'tipo': 'objeto', 'args': [11, 1]})
        r['eventos'].append({'tipo': 'vacio', 'args': []})
    return r


def cierre(exito):
    return [{'tipo': 'reset', 'args': [1]},
            {'tipo': 'uianim', 'args': [0, derrota.ANIMACIONES[0 if exito else 1], 1, 0], 'f12_bits': bits(0)},
            {'tipo': 'pose', 'args': [0x800, 8 if exito else 9]},
            {'tipo': 'audio', 'args': [2 if exito else 3, 0]},
            {'tipo': 'objeto', 'args': [11, 0]}, {'tipo': 'vozmini', 'args': [10]},
            {'tipo': 'vacio', 'args': []}]


def comprobar(region, elf):
    s, filas, observaciones, continuos = CaducidadCell(region, elf), [], [], []

    def revisar(nombre, want, got):
        if want != got and not any(not f['igual'] for f in filas):
            print(f'Primer DIF {region}, {nombre}: esperado={want}, obtenido={got}')
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})

    # Frontera ratio=1 por dificultad, incluyendo vecinos float32 exactos y
    # el clamp de altura400. Estado de timer2, no el signo de su restante,
    # es lo que dispara CheckTimeUp.
    for dificultad in range(6):
        umbral = ee.fpu_h('mul', s.coeficientes[dificultad], bits(400))
        alturas = sorted(set((bits(-1), bits(0), umbral - 1, umbral, umbral + 1, bits(400), bits(400) + 1)))
        for altura in alturas:
            for estado_timer in (0, 1, 2, 3):
                for restante in (bits(-.5), bits(0), bits(75)):
                    s.preparar_caducidad(dificultad, altura, estado_timer, restante)
                    got = s.caducar()
                    observaciones.append({'dificultad': dificultad, 'altura_bits': altura,
                                          'estado_timer': estado_timer, 'restante_bits': restante, 'salida': got})
                    revisar(f'caducidad:{dificultad}:{altura:08x}:{estado_timer}:{restante:08x}',
                            esperado(s, dificultad, altura, estado_timer, restante), derrota.para_comparar(got))

    # Fire llama a CheckTimeUp real. Tras la caducidad, AMBAS regiones siguen
    # con la fase3 de esa llamada (EU solo retorna temprano por agotamiento).
    for dificultad in range(6):
        umbral = ee.fpu_h('mul', s.coeficientes[dificultad], bits(400))
        for altura in (umbral - 1, umbral):
            for paso, duracion in ((1, 141), (.5, 281), (1.2, 117), (2, 71)):
                pref = f'continuo:{dificultad}:{altura:08x}:{paso}'
                s.preparar_caducidad(dificultad, altura)
                s.cpu.w32(s.d['paso'], bits(paso))
                want = esperado(s, dificultad, altura, 2, bits(0))
                want.pop('retorno')
                want['reloj_bits'] = ee.fpu_h('sub', bits(140), bits(paso))
                want['luces_bits'] = ee.fpu_h('add', bits(23), bits(paso))
                want['eventos'].append({'tipo': 'luz', 'id': 5})
                revisar(pref + ':Fire_CheckTimeUp', want, derrota.para_comparar(s.caducar(desde_fire=True)))
                reloj = want['reloj_bits']
                for n in range(400):
                    reloj = ee.fpu_h('sub', reloj, bits(paso))
                    fin = ee.fv(reloj) < 0
                    got = s.esperar_derrota(bits(paso))
                    want_tick = dict(want, reloj_bits=reloj, estado=s.p['wait'] if fin else s.p['gameover'],
                                     flags=0 if fin else 0x88, par=[0, 1] if fin else [0, 0],
                                     gestor_flags=[0, 1] if fin else [0xA5, 0x5A],
                                     eventos=cierre(es_exito(want['resultado'][1])) if fin else [])
                    revisar(pref + f':GameOver:{n}', want_tick, derrota.para_comparar(got))
                    if fin:
                        break
                revisar(pref + ':duracion', duracion - 1, n + 1)
                # El reloj de camara es ENTRADA externa controlada: no se
                # simula el avance de una animacion/render del juego.
                for reloj_cam, destino in ((bits(0), s.p['wait']), (bits(2) - 1, s.p['wait']),
                                           (bits(2), s.q['apply'])):
                    s.cpu.w32(s.cam + 0x168, reloj_cam)
                    got = s.esperar_resultado()
                    want_wait = dict(want_tick, estado=destino, eventos=[])
                    revisar(pref + f':WaitResult:{reloj_cam:08x}', want_wait, derrota.para_comparar(got))
                continuos.append({'dificultad': dificultad, 'altura_bits': altura, 'paso_bits': bits(paso),
                                  'exito': es_exito(want['resultado'][1]), 'ticks_gameover': n + 1,
                                  'estado_final': got['estado']})

    # Getter de camara puro: la igualdad tambien significa terminado. El
    # propio WaitResult no incrementa ninguno de los dos campos de camara.
    for objetivo in (bits(-1), bits(0), bits(2)):
        for actual in (bits(-2), bits(-1), bits(0), bits(1), bits(2), bits(3)):
            s.preparar_caducidad(0, bits(0))
            s.cpu.w32(s.task + 0x1C, s.p['wait'])
            s.cpu.w32(s.cam + 0x168, actual); s.cpu.w32(s.cam + 0x16C, objetivo)
            got = s.esperar_resultado()
            revisar(f'getter_camara:{actual:08x}:{objetivo:08x}',
                    [s.q['apply'] if ee.fv(actual) >= ee.fv(objetivo) else s.p['wait'], actual, objetivo, []],
                    [got['estado'], s.cpu.r32(s.cam + 0x168), s.cpu.r32(s.cam + 0x16C), got['eventos']])
    for nombre, preparar, ejecutar in (
        ('no WaitResult desde Fire', lambda: s.preparar_caducidad(0, bits(0)), s.esperar_resultado),
        ('no caducidad desde GameOver', lambda: (s.preparar_caducidad(0, bits(0)), s.caducar()), s.caducar),
        ('dificultad invalida', lambda: None, lambda: s.preparar_caducidad(6, bits(0))),
        ('altura NaN', lambda: None, lambda: s.preparar_caducidad(0, 0x7FC00000))):
        preparar()
        try:
            ejecutar()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(nombre, True, rechazo)
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Caducidad Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return {'region': region, 'elf_sha256': s.digest, 'caducidad_sha256': s.caducidad_sha256,
            'wait_sha256': s.wait_sha256, 'gameover_sha256': s.gameover_sha256,
            'tabla_sha256': s.tabla_sha256, 'coeficientes_bits': s.coeficientes,
            'servicios': s.por_rol, 'iguales': iguales, 'total': len(filas), 'primera_diferencia': primera,
            'correcto': iguales == len(filas), 'casos': filas, 'observaciones': observaciones, 'continuos': continuos}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=derrota.geometria.carga.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=derrota.geometria.carga.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    inicio = time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, elf) for r, elf in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump({'operacion': 'caducidad_cell', 'pares_dorado_real': 0, 'integrado_cpp': False,
                       'alcance': 'CheckTimeUp/GameOver/WaitResult originales, sin ejecutar ApplyResult',
                       'limites': ['reset/UI/voz/pose/audio/HUD controlados',
                                   'estado del timer/tiempo restante y reloj camara son entradas de fixture',
                                   'sin ApplyResult/premios, mundo/impacto, juego, C++ ni hardware',
                                   'formulas esperadas usan FPU EE; no oraculo independiente de FPU'],
                       'regiones': regiones, 'segundos_banco': round(time.monotonic() - inicio, 3)},
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

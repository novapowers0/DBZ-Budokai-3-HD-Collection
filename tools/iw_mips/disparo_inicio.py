# SPDX-License-Identifier: MIT
"""F1: Start y ResetScene originales EU/USA enlazados hasta entrada Aim.

Banco offline. UI, poses, HUD, luz, audio, mundo, preparacion de actores y
reinicio de scripts son servicios controlados, SIN efectos de escena reales.
Start exige gestor valido: ResetScene(1) contiene escrituras no protegidas
por la comprobacion de gestor nulo. No se integra nuevo gameplay al port.
"""
import argparse
from collections import Counter
import copy
import itertools
import json
from pathlib import Path
import struct
import time

import disparo_intro as intro
import disparo_ronda as ronda
import ee

bits = ronda.bits
PERFILES = {
    'eu': dict(start=0x30BD90, fin_start=0x30BE70, reset_scene=0x30C020,
               fin_reset=0x30C1A0, posiciones=(0x5D92F0, 0x5D9320),
               freeze=0x3102A0, clearflags=0x310320, screen_clear=0x31E1E0),
    'us': dict(start=0x30BB20, fin_start=0x30BC00, reset_scene=0x30BDB0,
               fin_reset=0x30BF30, posiciones=(0x5D8AF0, 0x5D8B20),
               freeze=0x310030, clearflags=0x3100B0, screen_clear=0x31DEF0)}
START_ROLES = ('reset_scene', 'playaudio', 'pose', 'uianim', 'flagsword',
               'settimer', 'vacio', 'flag', 'actores_sin_resolver', 'scripts_reset')
RESET_ROLES = ('flagsword', 'pose', 'pose', 'screen_clear', 'timer_ui',
               'timer_vis', 'fade_hidden', 'ui_flag2', 'ui_flag2',
               'ui_flag2', 'ui_flag2', 'ui_flag2', 'ui_toggle', 'spr_flag2',
               'mundo', 'luz', 'freeze', 'clearflags', 'efecto', 'render_reset')
ARGUMENTOS = dict(timer_ui=1, ui_toggle=1,
                  spr_flag2=0, mundo=0, render_reset=0, actores_sin_resolver=0,
                  scripts_reset=0)
POSICIONES = (
    tuple(tuple(bits(x) for x in v) for v in pareja)
    for pareja in (((-12, 3, 27, 1), (4, 5, 6, 1)),
                  ((0, -0.0, 0, 0), (1, -1, 2, -2)),
                  ((.125, .5, 128, 7), (-256, 32, -.25, 3))))
POSICIONES = tuple(POSICIONES)
INICIALES = tuple(tuple(bits(x) for x in v) for v in ((-99, 98, -97, 2), (96, -95, 94, -3)))
FLAGS = (0, 8, 0x86, 0xFF, 0x8000000F, ee.M32)


class InicioCell(ronda.RondaCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        c, p = self.cpu, PERFILES[region]
        self.inicio = p
        for a, b, roles in ((p['start'], p['fin_start'], START_ROLES),
                            (p['reset_scene'], p['fin_reset'], RESET_ROLES)):
            calls = intro.llamadas(c.m, a, b)
            if len(calls) != len(roles):
                raise ValueError('perfil JAL de Start/ResetScene cambiado')
            for addr, role in zip(calls, roles):
                if addr in self.roles and self.roles[addr] != role:
                    raise ValueError(f'roles Start/ResetScene contradictorios en {addr:08x}: '
                                     f'{self.roles[addr]} frente a {role}')
                self.roles[addr] = role
        self.por_rol = {r: a for a, r in self.roles.items()}
        for role in ARGUMENTOS:
            c.hooks[self.por_rol[role]] = self._servicio_inicio(role)
        for role in ('reset_scene', 'screen_clear', 'freeze', 'clearflags'):
            if self.por_rol[role] != p[role]:
                raise ValueError('destino auxiliar distinto del perfil')
            c.hooks.pop(p[role], None)
            c.whitelist.add(p[role])
        self.intervalos += [('start', p['start'], p['fin_start']),
                            ('reset_scene', p['reset_scene'], p['fin_reset']),
                            ('freeze', p['freeze'], p['freeze'] + 0x40),
                            ('clearflags', p['clearflags'], p['clearflags'] + 0x20),
                            ('screen_clear', p['screen_clear'], p['screen_clear'] + 0x10)]
        self.hashes = {n: intro.huella(c.m[a:b]) for n, a, b in self.intervalos}

    def _servicio_inicio(self, role):
        def hook(c):
            self.eventos.append(dict(tipo=role,
                args=[c.r[4 + i] & ee.M32 for i in range(ARGUMENTOS[role])],
                instante=intro.instante(self.observar())))
            c.r[2] = 0
        return hook

    def preparar_inicio(self, dificultad=0, angulo=bits(45), gestor=True,
                        posiciones=POSICIONES[0], flags=0x86):
        if posiciones not in POSICIONES or type(flags) is not int or not 0 <= flags <= ee.M32:
            raise ValueError('posiciones del corpus y flags u32 requeridos')
        ref = self.preparar_ronda(dificultad, angulo, gestor, 'intro', bits(123))
        c = self.cpu
        for a, v in zip(self.inicio['posiciones'], posiciones):
            c.m[a:a + 16] = struct.pack('<4I', *v)
        c.w32(ronda.CTX, intro.TASK)
        c.w32(intro.CTRL + 0x24, flags)
        c.w32(intro.CTRL + 0x60, 0x1234ABCD)
        for off, v in zip((0x840, 0x2910), INICIALES):
            c.m[intro.MGR + off:intro.MGR + off + 16] = struct.pack('<4I', *v)
        ref.update(tarea=intro.TASK, flags=flags, control60=0x1234ABCD,
                   posiciones_fuente=posiciones,
                   campos_gestor=list(zip((0x840, 0x2910), INICIALES)))
        return ref

    def observar(self):
        r = super().observar()
        c = self.cpu
        r.update(control60=c.r32(intro.CTRL + 0x60),
                 posiciones=[list(struct.unpack_from('<4I', c.m, intro.MGR + off))
                             for off in (0x840, 0x2910)])
        return r

    def _validar(self, completo):
        c = self.cpu
        if (not self.preparado or c.r32(self.ronda['ctxp']) != ronda.CTX
                or c.r32(ronda.CTX) != intro.TASK
                or c.r32(self.ronda['screenp']) != ronda.SCREEN
                or c.r32(self.q['ctrlp']) != intro.CTRL
                or c.r32(self.q['camp']) != intro.CAM):
            raise ValueError('contexto de arranque no preparado')
        gestor = c.r32(self.d['mgrp'])
        if gestor not in (0, intro.MGR) or completo and gestor != intro.MGR:
            raise ValueError('reset no nulo/Start requiere gestor valido')

    def _llamar(self, addr, *args):
        c = self.cpu
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.call(addr, *args, limite=10000)
        if c.io or c.sin_puente:
            raise RuntimeError('Start/ResetScene con E/S o llamada fuera del contrato')
        return dict(self.observar(), eventos=copy.deepcopy(self.eventos),
                    llamadas=dict(Counter(self.roles[a] for a in c.llamadas.elements())))

    def llamar_inicio(self):
        self._validar(True)
        return self._llamar(self.inicio['start'])

    def llamar_reset(self, valor=1):
        if type(valor) is not int or valor not in (0, 1, 2, ee.M32):
            raise ValueError('argumento reset fuera del corpus')
        self._validar(bool(valor))
        return self._llamar(self.inicio['reset_scene'], valor)

    def integridad(self, ref):
        return (super().integridad(ref)
                and all(self.cpu.m[a:a + 16] == struct.pack('<4I', *v)
                        for a, v in zip(self.inicio['posiciones'], ref['posiciones_fuente'])))


def emitir(r, eventos, calls, role, args=(), fargs=None):
    calls[role] += 1
    e = dict(tipo=role, args=list(args), instante=intro.instante(r))
    if fargs is not None:
        e['fargs_bits'] = list(fargs)
    eventos.append(e)


def reset_modelo(s, r, valor, eventos, calls):
    """Escrituras y peticiones de ResetScene; servicios externos sin efectos."""
    if valor:
        r['banderas'] = [1, 1]
        calls['flagsword'] += 1; r['flags'] = 0
        r['campos_gestor'][0] = (0x840, r['posiciones_fuente'][0])
        emitir(r, eventos, calls, 'pose', (intro.MGR + 0x800, 10))
        r['campos_gestor'][1] = (0x2910, r['posiciones_fuente'][1])
        emitir(r, eventos, calls, 'pose', (intro.MGR + 0x28D0, 0))
    calls['screen_clear'] += 1
    emitir(r, eventos, calls, 'ui_flag2', (9, 0))
    emitir(r, eventos, calls, 'timer_ui', (3,))
    if valor:
        emitir(r, eventos, calls, 'timer_vis', (0,))
        for i in range(4):
            emitir(r, eventos, calls, 'fade_hidden', (i, 0))
        for i in (9, 10, 7, 8):
            emitir(r, eventos, calls, 'ui_flag2', (i, 0))
    emitir(r, eventos, calls, 'ui_flag2', (11, 0))
    if valor:
        emitir(r, eventos, calls, 'ui_toggle', (1,))
        emitir(r, eventos, calls, 'spr_flag2')
    emitir(r, eventos, calls, 'mundo'); emitir(r, eventos, calls, 'luz', (0,))
    calls['freeze'] += 1; r['flags'] &= 0xFFFFFFF9; r['control60'] = 0
    calls['clearflags'] += 1; r['flags'] &= 0xFFFFFFF7
    emitir(r, eventos, calls, 'efecto', (0, 0)); emitir(r, eventos, calls, 'render_reset')


def inicio_modelo(s, r, eventos, calls):
    calls['reset_scene'] += 1
    reset_modelo(s, r, 1, eventos, calls)
    emitir(r, eventos, calls, 'playaudio', (0, 0))
    r['estado'] = s.q['preintro']
    emitir(r, eventos, calls, 'pose', (intro.MGR + 0x28D0, 0))
    emitir(r, eventos, calls, 'uianim', (1, 0, 1, 0), (0,))
    r['banderas'] = [1, 1]
    calls['flagsword'] += 1; r['flags'] = 0
    calls['settimer'] += 1; r['reloj_bits'] = bits(2)
    calls['vacio'] += 1
    emitir(r, eventos, calls, 'flag', (0,))
    emitir(r, eventos, calls, 'actores_sin_resolver'); emitir(r, eventos, calls, 'scripts_reset')


def salida_modelo(ref, eventos, calls):
    r = ronda.salida_modelo(ref, eventos, calls)
    r.update(control60=ref['control60'], posiciones=[list(v) for _, v in ref['campos_gestor']])
    return r


def comprobar(region, elf):
    s, filas, continuos = InicioCell(region, elf), [], []
    def revisar(nombre, want, got):
        if want != got and not any(not f['igual'] for f in filas):
            print(f'Primer DIF {region} {nombre}: esperado={want}, obtenido={got}')
        filas.append(dict(caso=nombre, igual=want == got, esperado=want, obtenido=got))

    reinicios = [(False, 0), (True, 0), (True, 1), (True, 2), (True, ee.M32)]
    for (gestor, valor), posiciones, flags in itertools.product(reinicios, POSICIONES, FLAGS):
        ref = s.preparar_inicio(gestor=gestor, posiciones=posiciones, flags=flags)
        for rep in range(2):
            eventos, calls = [], Counter()
            reset_modelo(s, ref, valor, eventos, calls)
            nombre = f'reset:{gestor}:{valor:08x}:{POSICIONES.index(posiciones)}:{flags:08x}:{rep}'
            revisar(nombre, salida_modelo(ref, eventos, calls), s.llamar_reset(valor))
            revisar(nombre + ':integridad', True, s.integridad(ref))

    for dif, angulo, posiciones in itertools.product(range(6), ronda.ANGULOS, POSICIONES):
        ref = s.preparar_inicio(dif, angulo, posiciones=posiciones)
        for rep in range(2):
            eventos, calls = [], Counter()
            inicio_modelo(s, ref, eventos, calls)
            nombre = f'start:{dif}:{angulo:08x}:{POSICIONES.index(posiciones)}:{rep}'
            revisar(nombre, salida_modelo(ref, eventos, calls), s.llamar_inicio())
            revisar(nombre + ':integridad', True, s.integridad(ref))

    def continuo(ref, dif, paso, patron, ciclo):
        eventos, calls = [], Counter()
        inicio_modelo(s, ref, eventos, calls)
        nombre = f'continuo:{dif}:{paso:08x}:{patron}:{ciclo}'
        revisar(nombre + ':inicio', salida_modelo(ref, eventos, calls), s.llamar_inicio())
        revisar(nombre + ':inicio_integridad', True, s.integridad(ref))
        primera, solicitudes = None, 0
        for n in range(500):
            camara = (bits(2) if n >= 12 else 0, bits(2))
            mando = 8 if (patron == 'salto' and n >= 4) or (patron == 'coincide' and n >= 14) else 0
            ref['camara_bits'] = list(camara)
            ref, parcial = intro.modelo(s, ref, paso, camara, mando,
                lambda r, e, c: ronda.preparar_modelo(s, r, e, c))
            want = salida_modelo(ref, parcial['eventos'], parcial['llamadas'])
            solicitudes += parcial['llamadas'].get('setup', 0)
            got = s.pasar_intro(paso, camara, mando)
            revisar(nombre + f':{n}', want, got)
            revisar(nombre + f':{n}:integridad', True, s.integridad(ref))
            if want != got and primera is None:
                primera = n
            if ref['estado'] == s.d['mira']:
                break
        revisar(nombre + ':Aim', s.d['mira'], ref['estado'])
        revisar(nombre + ':rondas', 2 if patron == 'coincide' else 1, solicitudes)
        continuos.append(dict(dificultad=dif, paso_bits=paso, patron=patron, cuadros=n + 1,
                              ciclo=ciclo, preparaciones=solicitudes, primera_diferencia=primera))
        return ref

    for dif, paso, patron in itertools.product(range(6), (bits(.5), bits(1), bits(1.2), bits(2)),
                                               ('natural', 'salto', 'coincide')):
        ref = s.preparar_inicio(dif)
        for ciclo in range(2):
            # El segundo Start parte de la memoria que dejo la entrada Aim;
            # tampoco se resincroniza la referencia a partir de lo obtenido.
            ref = continuo(ref, dif, paso, patron, ciclo)

    for nombre, operacion in (
            ('start_sin_preparar', lambda: setattr(s, 'preparado', False)),
            ('start_gestor_nulo', lambda: s.cpu.w32(s.d['mgrp'], 0)),
            ('start_tarea_ajena', lambda: s.cpu.w32(ronda.CTX, 0)),
            ('start_pantalla_ajena', lambda: s.cpu.w32(s.ronda['screenp'], 0)),
            ('start_control_ajeno', lambda: s.cpu.w32(s.q['ctrlp'], 0))):
        s.preparar_inicio(); operacion()
        antes = intro.huella(s.cpu.m)
        try:
            s.llamar_inicio(); rechazado = False
        except ValueError:
            rechazado = True
        revisar('rechazo:' + nombre, True, rechazado and intro.huella(s.cpu.m) == antes)
    for valor in (True, -1, 3):
        s.preparar_inicio(); antes = intro.huella(s.cpu.m)
        try:
            s.llamar_reset(valor); rechazado = False
        except ValueError:
            rechazado = True
        revisar('rechazo:reset:' + repr(valor), True, rechazado and intro.huella(s.cpu.m) == antes)
    for n, a, b in s.intervalos:
        revisar('codigo_intacto:' + n, s.hashes[n], intro.huella(s.cpu.m[a:b]))
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Inicio Cell {region}: {iguales}/{len(filas)}, primer DIF {primera["caso"] if primera else None}')
    return dict(region=region, elf_sha256=s.digest, perfil=s.inicio, cuerpos_sha256=s.hashes,
                iguales=iguales, total=len(filas), primera_diferencia=primera,
                correcto=iguales == len(filas), casos=filas, continuos=continuos,
                resets_aislados=len(reinicios) * len(POSICIONES) * len(FLAGS) * 2,
                starts_aislados=6 * len(ronda.ANGULOS) * len(POSICIONES) * 2,
                cuadros_continuos=sum(c['cuadros'] for c in continuos))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=intro.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=intro.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    args = p.parse_args(); inicio = time.monotonic()
    try:
        if args.salida.exists() or not args.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in [('eu', args.elf_eu), ('us', args.elf_us)]]
        informe = dict(operacion='inicio_cell', regiones=regiones, pares_dorado_real=0, integrado_cpp=False,
            referencia_continua='estado esperado propio, sin realimentar salidas observadas',
            FPU_HW=sorted(ee.FPU_HW), FPU_CERCANO=ee.FPU_CERCANO,
            limites=['tablas/posiciones de escenario sinteticas, no parametros reales de la mision',
                     'Start requiere gestor valido: ResetScene(1) escribe posiciones sin comprobar nulo',
                     'UI/poses/HUD/audio/luces/mundo/actores/scripts/render controlados SIN efectos reales',
                     'timer comparte aritmetica EE y lente modelo del perfil vigente, no evidencia hardware',
                     'continuo termina al entrar Aim; no escena ni minijuego completo ni C++'],
            segundos_banco=round(time.monotonic() - inicio, 3))
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(informe, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

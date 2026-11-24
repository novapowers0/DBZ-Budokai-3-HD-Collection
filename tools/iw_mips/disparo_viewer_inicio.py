# SPDX-License-Identifier: MIT
"""F1: Start/Intro/Countdown/SetupRound/Ready Viewer7D8, banco offline EU/USA.

Timer de mision, audio, UI, poses, scripts y animacion son fronteras observadas.
Se ejecutan el reloj del task, FOV, copia VU, flags y globals camera originales.
No se integra gameplay nuevo. La referencia conserva su propio estado.
"""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import struct
import time

import disparo_viewer_agotamiento as ag
import disparo_ronda as ronda
import ee

CTX, TASK, CAM, GAME = ag.CTX, ag.TASK, ag.CAM, ag.GAME
ACTOR, PLAYER, UI, TIMER = ag.ACTOR, ag.PLAYER, ag.UI, ag.TIMER
bits, leer, escribir = ag.bits, ag.leer, ag.escribir
PERFILES = {
    'eu': dict(start=0x326450, intro=0x325C30, countdown=0x325AB0,
               ready=0x325840, setup=0x3267F0, input=0x325430,
               pulsado=0x4ADECE, anim=0x499900, datos=0x5D97B0,
               camreset=0x32E6D0, camget=0x2ED7D0, camfields=0x2ED8D0,
               orflags=0x32C230),
    'us': dict(start=0x326160, intro=0x325940, countdown=0x3257C0,
               ready=0x325550, setup=0x3264E0, input=0x325140,
               pulsado=0x4AD6CE, anim=0x499100, datos=0x5D8FB0,
               camreset=0x32E3A0, camget=0x2ED5C0, camfields=0x2ED6C0,
               orflags=0x32BF10)}
CUERPOS = {'start': (0xD0, ('reset', 'pose_actor', 'audio_play', 'camara', 'flags',
                          'timer_set', 'stub', 'gamestate', 'actores', 'scripts_reset')),
           'intro': (0x130, ('timer_tick', 'camfin', 'stub', 'audio_stop', 'camara',
                             'pose_actor', 'pose_actor', 'update', 'flags', 'sonido')),
           'countdown': (0x180, ('camfin', 'flags', 'pose_actor', 'camara', 'fatiga',
                                 'sonido', 'camfin', 'camfields', 'setup', 'timer_set',
                                 'voz', 'render_reset', 'stub')),
           'ready': (0x270, ('fade_ratio', 'fade_ratio', 'ui_datos', 'double', 'compare',
                             'ui_campo158', 'ui_datos', 'impacto', 'ui_campo158',
                             'ui_campo16c', 'impacto', 'ui_campo158', 'ui_campo16c',
                             'timer_tick', 'pose_actor', 'orflags', 'sonido', 'voz',
                             'gamestate', 'timer_state')),
           'setup': (0x3D0, ('ui_pos', 'ui_scale', 'camget', 'pose_actor', 'camreset',
                             'pose_actor', 'orflags', 'screen_field', 'timer_init',
                             'timer_objetos', 'fade_hidden', 'ui_flag', 'fade_ratio',
                             'fade_ratio', 'ui_datos', 'fade_ratio', 'fade_ratio',
                             'ui_datos', 'double', 'compare', 'ui_campo158',
                             'ui_campo158', 'ui_flag', 'ui_datos', 'update',
                             'pose_player', 'script_execute'))}
HOOKS = {'audio_play': (2, False), 'audio_stop': (1, False),
         'gamestate': (1, False), 'actores': (0, False), 'scripts_reset': (0, False),
         'fatiga': (1, False), 'render_reset': (0, False), 'timer_init': (1, False),
         'ui_pos': (1, False), 'ui_scale': (1, False), 'script_execute': (0, False)}


def observar(lectura, p):
    r = ag.huella_estado(lectura, p)
    r.update(fov_bits=lectura(CAM + 0xB4), progreso_bits=lectura(CTX + 4),
             ui4=lectura(UI + 64), ui9=lectura(UI + 144))
    return r


class InicioViewer(ag.AgotamientoViewer):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        self.p.update(PERFILES[region])
        c, p, roles = self.cpu, self.p, {}
        self.jal = {}
        for nombre, (tam, nombres) in CUERPOS.items():
            a = p[nombre]
            js = ag.resultado.llamadas(c.m, a, a + tam)
            if len(js) != len(nombres):
                raise ValueError('JAL inesperados: ' + nombre)
            self.jal[nombre] = js
            for addr, rol in zip(js, nombres):
                if rol in roles and roles[rol] != addr:
                    raise ValueError('destinos contradictorios: ' + rol)
                roles[rol] = addr
            c.whitelist.add(a)
            self.intervalos.append((a, a + tam))
        for rol in ('reset', 'setup', 'camfin', 'camfields', 'camget', 'camreset', 'orflags'):
            if roles[rol] != p[rol]:
                raise ValueError('entrada regional distinta: ' + rol)
        for rol, (nargs, flotante) in HOOKS.items():
            c.hooks[roles[rol]] = self._hook_inicio(rol, nargs, flotante)
        for rol, n in (('camreset', 0x60), ('orflags', 0x20),
                       ('camget', 0x10), ('camfields', 0x20)):
            c.hooks.pop(roles[rol], None)
            c.whitelist.add(roles[rol])
            self.intervalos.append((roles[rol], roles[rol] + n))
        self.roles = roles
        # Direccion del estado escrita por Ready, no el final de StateActive.
        if (c.r32(p['ready'] + 0x21C) != 0x3C020032
                or c.r32(p['ready'] + 0x220) != (0x24420000 | (p['input'] & 0xFFFF))):
            raise ValueError('literal del estado Input distinto del perfil')
        self.hashes = [ag.resultado.huella(c.m[a:b]) for a, b in self.intervalos]

    def _hook_inicio(self, rol, nargs, flotante):
        base = self._hook(rol, nargs, flotante)
        def hook(c):
            base(c)
            if rol in ('ui_pos', 'ui_scale'):
                addr = self.p['datos'] + (16 if rol == 'ui_scale' else 0)
                if c.r[4] != addr:
                    raise RuntimeError('puntero UI fuera del perfil')
                self.eventos[-1]['datos_bits'] = [c.r32(addr + 4 * i) for i in range(4)]
        return hook

    def preparar_inicio(self, dificultad=0, angulo=bits(45), estado='intro',
                        reloj=bits(2), relleno=0xA5):
        reloj_valido = (type(reloj) is int and 0 <= reloj <= ee.M32 if estado == 'countdown'
                        else ag.resultado.finito(reloj))
        if (angulo not in ronda.ANGULOS or estado not in ('intro', 'countdown', 'ready')
                or not reloj_valido):
            raise ValueError('angulo/estado/reloj fuera del corpus')
        ref = self.preparar_agotamiento(dificultad, progreso=bits(10), relleno=relleno, ui_aviso=0)
        c, p = self.cpu, self.p
        extras = ((p['pulsado'] - 2, 4), (p['anim'], 16), (p['datos'], 0x24),
                  (p['update'] + 16, 0x44), (p['maximos'] - 4, 4))
        for a, n in extras:
            ref[a] = bytearray(c.m[a:a + n])
        for a, v in ((CTX, TASK), (TASK + 0x1C, p[estado]), (TASK + 0x30, reloj),
                     (CAM + 0x168, 0), (CAM + 0x16C, bits(1)),
                     (CAM + 0xB4, bits(-17)), (p['pulsado'] - 2, 0),
                     (p['anim'], 0x33333333), (p['anim'] + 12, 0x44444444),
                     (p['datos'] + 0x20, angulo)):
            c.w32(a, v); escribir(ref, a, v)
        for i, v in enumerate(map(bits, (1, 2, 3, 1, .5, 1, 2, 1))):
            c.w32(p['datos'] + 4 * i, v); escribir(ref, p['datos'] + 4 * i, v)
        # Globals del init original: solo se capturan como entrada, no se inventa tiempo de mision.
        return ref

    def paso_inicio(self, operacion='estado', paso=bits(1), pulsado=0, camara=(0, bits(1)), impacto=0):
        c, p = self.cpu, self.p
        if (not self.preparado or c.r32(CTX) != TASK
                or any(c.r32(p[k]) != v for k, v in (('ctxp', CTX), ('camp', CAM),
                        ('gamep', GAME), ('timerp', TIMER), ('actorp', ACTOR),
                        ('playerp', PLAYER), ('screenp', ag.SCREEN), ('readp', 0)))):
            raise ValueError('punteros de inicio fuera del fixture')
        if (operacion not in ('start', 'setup', 'estado') or type(pulsado) is not int
                or not 0 <= pulsado <= 0xFFFF or paso not in tuple(map(bits, (.5, 1, 1.2, 2)))
                or type(impacto) is not int or impacto not in (0, 1)
                or len(camara) != 2 or not all(ag.resultado.finito(b) for b in camara)
                or c.r32(UI + 64) not in (0, 1) or c.r32(UI + 144) not in (0, 1)):
            raise ValueError('entradas fuera del perfil de inicio')
        estado = c.r32(TASK + 0x1C)
        if operacion == 'estado' and estado not in (p['intro'], p['countdown'], p['ready']):
            raise ValueError('estado fuera del tramo intro/ready')
        self.impacto, self.eventos = impacto, []
        c.io.clear(); c.sin_puente.clear(); c.llamadas.clear()
        c.w32(p['pasop'], paso); c.w16(p['pulsado'], pulsado)
        for off, b in zip((0x168, 0x16C), camara):
            c.w32(CAM + off, b)
        c.call(p[operacion] if operacion != 'estado' else estado, *(() if operacion != 'estado' else (TASK,)), limite=20000)
        if c.io or c.sin_puente:
            raise RuntimeError('E/S o llamada fuera del perfil')
        return dict(observar(c.r32, p), eventos=self.eventos.copy())


def modelo(s, ref, operacion='estado', paso=bits(1), pulsado=0, camara=(0, bits(1)), impacto=0):
    r, p, eventos = {a: bytearray(b) for a, b in ref.items()}, s.p, []
    escribir(r, p['pasop'], paso)
    # El mando mantenido adyacente se conserva; solo cambia el halfword pulsado.
    viejo = leer(r, p['pulsado'] - 2)
    escribir(r, p['pulsado'] - 2, (viejo & 0xFFFF) | (pulsado << 16))
    for off, b in zip((0x168, 0x16C), camara):
        escribir(r, CAM + off, b)
    def evento(rol, args=(), f12=None):
        e = dict(tipo=rol, args=[v & ee.M32 for v in args],
                 antes=ag.huella_estado(lambda a: leer(r, a), p))
        if f12 is not None:
            e['f12_bits'] = f12
        if rol in ('ui_pos', 'ui_scale'):
            e['datos_bits'] = [leer(r, args[0] + i * 4) for i in range(4)]
        eventos.append(e)
    def banderas(a, b):
        ag.poner_bytes(r, ACTOR + 0x14CD, bytes([a]))
        ag.poner_bytes(r, ACTOR + 0x359D, bytes([b]))
    def update(valores):
        for i, v in enumerate(valores):
            escribir(r, p['update'] + 4 * i, v)
    def flag(valor):
        escribir(r, PLAYER + 0x3C, valor)
    def copiar_pos():
        src = r[p['posiciones']]
        ag.poner_bytes(r, ACTOR + 0x840, src[:16])
        ag.poner_bytes(r, ACTOR + 0x2910, src[0x30:0x40])
    def aviso_impacto():
        ratio = ag.dividir(leer(r, CTX + 4), leer(r, p['maximos'] + 16 * leer(r, CTX + 0x28)))
        evento('fade_ratio', (0, 0), ratio); evento('fade_ratio', (1, 1), ratio)
        evento('ui_datos', (4,))
        if leer(r, UI + 64) == 0 and ag.arit.racional(ratio) <= Fraction(3602879701896397, 18014398509481984):
            escribir(r, UI + 64, 1); evento('ui_campo158', (10, 13), 0)
        evento('ui_datos', (9,)); evento('impacto', (ACTOR + 0x28D0,))
        ui9 = leer(r, UI + 144)
        if ui9 == 0 and impacto:
            evento('ui_campo158', (9, 16), 0); evento('ui_campo16c', (9, ee.M32))
            escribir(r, UI + 144, 1)
        elif ui9 == 1 and not impacto:
            evento('ui_campo158', (9, 17), 0); evento('ui_campo16c', (9, 15))
            escribir(r, UI + 144, 0)
    def setup():
        evento('ui_pos', (p['datos'],)); evento('ui_scale', (p['datos'] + 16,))
        escribir(r, CAM + 0xB4, ronda.lente(leer(r, p['datos'] + 32)))
        evento('pose_actor', (ACTOR + 0x28D0, 0))
        # ResetCameraGlobals original, preservando los demas bytes de este bloque.
        for off, v in ((0x10, 0), (0x14, 0), (0x18, 0), (0x1C, bits(1)),
                       (0x20, 0xBFC90FDB), (0x24, 0), (0x48, 0), (0x4C, 0), (0x50, 0)):
            escribir(r, p['update'] + off, v)
        evento('pose_actor', (ACTOR + 0x800, 8)); banderas(1, 0)
        flag(leer(r, PLAYER + 0x3C) | 1)
        evento('ui_campo158', (6, 0), 0)
        t = int(ag.arit.racional(leer(r, p['maximos'] - 4 + 16 * leer(r, CTX + 0x28))))
        evento('timer_init', (t,)); evento('timer_objetos', (1,))
        evento('fade_hidden', (0, 1)); evento('fade_hidden', (1, 1)); evento('ui_flag', (10, 0))
        escribir(r, CTX + 4, leer(r, p['maximos'] + 16 * leer(r, CTX + 0x28)))
        evento('fade_ratio', (0, 1), bits(1)); evento('fade_ratio', (1, 1), bits(1))
        evento('ui_datos', (4,)); escribir(r, UI + 64, 0)
        evento('fade_ratio', (0, 0), bits(1)); evento('fade_ratio', (1, 1), bits(1))
        evento('ui_datos', (4,))
        escribir(r, CTX + 8, 0)
        evento('ui_campo158', (9, 16), 0); evento('ui_flag', (9, 1))
        evento('ui_datos', (9,)); escribir(r, UI + 144, 1)
        update((1, 1, 1, 1)); evento('pose_player', (ee.M32 - 1, 0))
        evento('script_execute')
        for off in range(0x14, 0x28, 4):
            escribir(r, CTX + off, 1)
    estado = leer(r, TASK + 0x1C)
    if operacion == 'start':
        r, obs = ag.modelo(s, r, operacion='reset1', paso=paso)
        eventos.extend(obs['eventos'])
        escribir(r, TASK + 0x1C, p['intro'])
        evento('pose_actor', (ACTOR + 0x28D0, 0)); evento('audio_play', (0, 0))
        evento('camara', (1, 0, 1, 0), 0); banderas(1, 1); flag(0)
        escribir(r, TASK + 0x30, bits(2))
        evento('gamestate', (0,)); evento('actores'); evento('scripts_reset')
    elif operacion == 'setup':
        setup()
    elif estado == p['intro']:
        reloj = ag.arit.suma(leer(r, TASK + 0x30), paso, cop1=True, resta=True)
        escribir(r, TASK + 0x30, reloj)
        if ag.arit.racional(reloj) < 0 and (ag.arit.racional(camara[0]) >= ag.arit.racional(camara[1]) or pulsado & 8):
            evento('audio_stop', (0,)); evento('camara', (0, leer(r, p['anim']), 1, 0), 0)
            evento('pose_actor', (ACTOR + 0x28D0, 0)); evento('pose_actor', (ACTOR + 0x800, 5))
            update((1, 0, 1, 0)); banderas(0, 1); flag(0)
            escribir(r, TASK + 0x1C, p['countdown']); copiar_pos(); evento('sonido', (0,))
            escribir(r, TASK + 0x30, 0)
    elif estado == p['countdown']:
        fase = leer(r, TASK + 0x30)
        if fase in (0, 1) and (ag.arit.racional(camara[0]) >= ag.arit.racional(camara[1]) or pulsado & 8):
            if fase == 0:
                banderas(1, 0); flag(0); evento('pose_actor', (ACTOR + 0x28D0, 5))
                evento('camara', (0, leer(r, p['anim'] + 12), 1, 0), 0)
                escribir(r, TASK + 0x30, 1); evento('fatiga', (ACTOR + 0x800,)); evento('sonido', (2,))
            else:
                ag.poner_bytes(r, CAM + 0x164, b'\0\0'); setup()
                escribir(r, TASK + 0x30, bits(100)); escribir(r, TASK + 0x1C, p['ready'])
                evento('voz', (0, ee.M32)); evento('render_reset')
    elif estado == p['ready']:
        aviso_impacto()
        reloj = ag.arit.suma(leer(r, TASK + 0x30), paso, cop1=True, resta=True)
        escribir(r, TASK + 0x30, reloj)
        if ag.arit.racional(reloj) < 0:
            evento('pose_actor', (ACTOR + 0x28D0, 1)); banderas(1, 0)
            flag(leer(r, PLAYER + 0x3C) | 1); escribir(r, TASK + 0x1C, p['input'])
            evento('sonido', (3,)); evento('voz', (1, ee.M32)); evento('gamestate', (1,))
            escribir(r, TIMER + 0x10, 1)
    else:
        raise ValueError('estado esperado fuera del tramo')
    return r, dict(observar(lambda a: leer(r, a), p), eventos=eventos)


def comprobar(region, elf):
    s, filas, muestras, continuos = InicioViewer(region, elf), [], {}, []
    def revisar(nombre, want, got):
        f = dict(caso=nombre, igual=want == got)
        if not f['igual']:
            f.update(esperado=want, obtenido=got)
            if all(t['igual'] for t in filas):
                print('Primer DIF', region, nombre)
        filas.append(f)
    def ejecutar(nombre, ref, **kw):
        ref, want = modelo(s, ref, **kw)
        got = s.paso_inicio(**kw)
        revisar(nombre, want, got); revisar(nombre + ':memoria_abi_guardias', True, s.integridad(ref))
        muestras.setdefault(nombre.split(':')[0], dict(esperado=want, obtenido=got))
        return ref
    aislados = 0
    for d in range(6):
        for angulo in ronda.ANGULOS:
            ref = s.preparar_inicio(d, angulo)
            for n in range(2):
                ref = ejecutar(f'setup:{d}:{angulo:08x}:{n}', ref, operacion='setup')
                aislados += 1
        for relleno in (0, 0xA5, 0xFF):
            ref = s.preparar_inicio(d, relleno=relleno)
            for n in range(2):
                ref = ejecutar(f'start:{d}:{relleno}:{n}', ref, operacion='start')
                aislados += 1
    for estado in ('intro', 'countdown', 'ready'):
        relojes = (bits(-1), 0, bits(1) - 1, bits(1), bits(1) + 1, bits(2)) if estado != 'countdown' else (0, 1, 2, ee.M32)
        for reloj in relojes:
            for paso in map(bits, (.5, 1, 1.2, 2)):
                for pulsado in (0, 8, 0x8000, 0xFFFF):
                    for camara in ((0, bits(1)), (bits(1) - 1, bits(1)), (bits(1), bits(1)), (bits(2), bits(1))):
                        ref = s.preparar_inicio(estado=estado, reloj=reloj)
                        ref = ejecutar(f'{estado}:{reloj:08x}:{paso:08x}:{pulsado}:{camara[0]}', ref,
                                       paso=paso, pulsado=pulsado, camara=camara, impacto=1)
                        aislados += 1
    # Dos arranques por fixture, manteniendo ambas memorias entre ciclos.
    for d in range(6):
        for paso in map(bits, (.5, 1, 1.2, 2)):
            for patron in ('natural', 'start'):
                ref = s.preparar_inicio(d)
                for ciclo in range(2):
                    pref = f'continuo:{d}:{paso:08x}:{patron}:{ciclo}'
                    ref = ejecutar(pref + ':start', ref, operacion='start', paso=paso)
                    n = 0
                    while leer(ref, TASK + 0x1C) != s.p['input'] and n < 320:
                        camara = (0, bits(1)) if n < 9 else (bits(1), bits(1))
                        ref = ejecutar(pref + f':{n}', ref, paso=paso,
                                       pulsado=8 if patron == 'start' else 0,
                                       camara=camara, impacto=n % 2)
                        n += 1
                    revisar(pref + ':input', s.p['input'], leer(ref, TASK + 0x1C))
                    continuos.append(dict(dificultad=d, paso_bits=paso, patron=patron,
                                           ciclo=ciclo, cuadros=n, entrada_input=leer(ref, TASK + 0x1C)))
    # Controles negativos: el esperado no recibe la desviacion de memoria/codigo.
    ref = s.preparar_inicio(estado='ready', reloj=bits(1))
    ref_esperado, want = modelo(s, ref)
    s.cpu.w32(TASK + 0x30, bits(1) - 1)
    got = s.paso_inicio()
    revisar('sensibilidad:reloj_1ulp:salida', False, want == got)
    revisar('sensibilidad:reloj_1ulp:memoria', False, s.integridad(ref_esperado))
    muestras['mutacion_reloj_intencionada'] = dict(esperado=want, obtenido=got)
    ref = s.preparar_inicio(estado='ready', reloj=bits(1))
    ejecutar('restaurado:reloj_1ulp', ref)
    ref = s.preparar_inicio(estado='ready', reloj=bits(-1))
    ref_esperado, want = modelo(s, ref)
    addr = s.p['ready'] + 0x220
    instruccion = s.cpu.r32(addr)
    try:
        s.cpu.w32(addr, instruccion ^ 4)  # addiu v0,v0,Input+4: estado incorrecto.
        s.cpu.code.pop(addr, None)  # El interprete conserva instrucciones ya decodificadas.
        got = s.paso_inicio()
        revisar('sensibilidad:destino_input:salida', False, want == got)
        revisar('sensibilidad:destino_input:memoria', False, s.integridad(ref_esperado))
        muestras['mutacion_destino_intencionada'] = dict(esperado=want, obtenido=got)
    finally:
        s.cpu.w32(addr, instruccion)
        s.cpu.code.pop(addr, None)
    ref = s.preparar_inicio(estado='ready', reloj=bits(-1))
    ejecutar('restaurado:destino_input', ref)
    for nombre, accion in (
            ('dificultad_bool', lambda: s.preparar_inicio(True)),
            ('angulo_nan', lambda: s.preparar_inicio(angulo=0x7FC00000)),
            ('estado_ajeno', lambda: s.preparar_inicio(estado='input')),
            ('paso_ajeno', lambda: s.paso_inicio(paso=bits(3))),
            ('pulsado_bool', lambda: s.paso_inicio(pulsado=True)),
            ('camara_nan', lambda: s.paso_inicio(camara=(0x7FC00000, 0))),
            ('impacto_bool', lambda: s.paso_inicio(impacto=True))):
        s.preparar_inicio()
        h, rechazo = ag.resultado.huella(s.cpu.m), False
        try:
            accion()
        except ValueError:
            rechazo = True
        revisar('rechazo:' + nombre, True, rechazo and ag.resultado.huella(s.cpu.m) == h)
    for (a, b), h in zip(s.intervalos, s.hashes):
        revisar(f'codigo:{a:08x}', h, ag.resultado.huella(s.cpu.m[a:b]))
    iguales = sum(f['igual'] for f in filas)
    dif = next((f for f in filas if not f['igual']), None)
    print(f'Inicio Viewer {region}: {iguales}/{len(filas)}, primer DIF {None if dif is None else dif["caso"]}')
    return dict(region=region, correcto=dif is None, iguales=iguales, total=len(filas),
                primera_diferencia=dif, llamadas_aisladas=aislados, continuos=continuos,
                muestras=muestras, controles_negativos=2,
                elf_sha256=s.digest, perfil=s.p, jal=s.jal,
                intervalos_codigo=[dict(inicio=a, fin_exclusivo=b, sha256=h)
                                   for (a, b), h in zip(s.intervalos, s.hashes)], casos=filas)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=ag.resultado.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=ag.resultado.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    inicio = time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf8') as f:
            json.dump(dict(operacion='viewer_inicio', regiones=regiones,
                           segundos_banco=round(time.monotonic() - inicio, 3), integrado_cpp=False,
                           pares_dorado_real=0,
                           referencia_continua='estado esperado propio, sin realimentar salidas observadas',
                           limites=['Timer_Init de mision solo solicitado, conserva fixture controlado',
                                    'UI/pose/voz/audio/scripts/render/animacion observados sin efectos',
                                    'camara/impacto/mando son entradas externas; posiciones/handles artificiales',
                                    'FOV y aritmetica del perfil EE, no autoridad hardware',
                                    'tramo termina al entrar StateInput: no ejecuta disparo ni minijuego completo']),
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, RuntimeError, OSError, NotImplementedError) as exc:
        print('FALLO:', exc)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

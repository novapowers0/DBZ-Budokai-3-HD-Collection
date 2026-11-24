# SPDX-License-Identifier: MIT
"""F1: SetupRound original EU/USA y continuo de inicio, sin juego ni emulador.

Datos de escenario artificiales (los globals del ELF estan sin inicializar).
Se ejecutan SetupRound, getters y wrapper UiScreen originales; UI/audio/poses/
HUD/luces son fronteras controladas. No es una escena ni un minijuego completo.
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
import disparo_perseguidor as racional
import ee

bits = intro.bits
CTX, UI, SCREEN = 0x1308000, 0x1309000, 0x130A000
PERFILES = {
    'eu': dict(setup=0x30C1A0, fin=0x30C3E0, ctxp=0x49CDBC, screenp=0x49CDE0,
               datos=0x5D9350, tabla=0x5D93A0, angulo=0x5D9370),
    'us': dict(setup=0x30BF30, fin=0x30C170, ctxp=0x49C59C, screenp=0x49C5C0,
               datos=0x5D8B50, tabla=0x5D8BA0, angulo=0x5D8B70)}
ROLES = ('ui_pos', 'ui_escala', 'camget', 'pose', 'pose', 'camara', 'screen',
         'timer_init', 'timer_vis', 'fade_hidden', 'ui_flag2', 'fade_ratio',
         'fade_ratio', 'fade_ratio', 'ui_ptr', 'hud', 'ui_field', 'ui_flag4',
         'ui_field', 'ui_ptr', 'luz')
ARGUMENTOS = dict(ui_pos=1, ui_escala=1, timer_init=1, timer_vis=1,
                  fade_hidden=2, ui_flag2=2, fade_ratio=2, ui_ptr=1,
                  ui_field=2, ui_flag4=2, luz=1)
TIEMPOS = (75, 90, 120, 150, 180, 210)
ENERGIAS = tuple(bits(x) for x in (.25, .5, .75, 1, .125, .875))
POS = tuple(bits(x) for x in (1, 2, 3, 1))
ESCALA = tuple(bits(x) for x in (.5, 1, 2, 1))
ANGULOS = tuple(bits(x) for x in (-360, 0, 5, 45, 90, 175, 360)) + (bits(5) - 1, bits(5) + 1, bits(175) - 1, bits(175) + 1)


def lente(angulo):
    a = racional.producto(bits(2), angulo, cop1=True)
    a = racional.producto(0x40490FDB, a, cop1=True)
    # COP1 div del perfil vigente redondea al vecino par, no hacia cero.
    # Elegirlo por comparacion racional exacta, sin consultar fpu_h ni su salida.
    q = racional.racional(a) / 360
    a = racional.truncar(q)
    if q:
        magnitud = a & 0x7FFFFFFF
        medio = (racional.racional(magnitud) + racional.racional(magnitud + 1)) / 2
        if abs(q) > medio or abs(q) == medio and magnitud & 1:
            a += 1
    if racional.racional(a) < racional.racional(0x3DB2B8C3):
        return 0x3DB2B8C3
    return a if racional.racional(a) <= racional.racional(0x40437A15) else 0x40437A15


class RondaCell(intro.IntroCell):
    def __init__(self, region, elf=None):
        if ee.FPU_HW != set(ee.FPU_HW_SERIE) or ee.FPU_CERCANO:
            raise ValueError('se requiere perfil FPU EE de serie')
        super().__init__(region, elf)
        self.ronda = p = PERFILES[region]
        c = self.cpu
        calls = intro.llamadas(c.m, p['setup'], p['fin'])
        if len(calls) != len(ROLES):
            raise ValueError('perfil JAL SetupRound cambiado')
        for addr, role in zip(calls, ROLES):
            if addr in self.roles and self.roles[addr] != role:
                raise ValueError('roles SetupRound contradictorios')
            self.roles[addr] = role
        self.por_rol = {r: a for a, r in self.roles.items()}
        for role in ARGUMENTOS:
            c.hooks[self.por_rol[role]] = self._servicio_ronda(role)
        for role in ('setup', 'camget', 'screen'):
            c.hooks.pop(self.por_rol[role], None)
            c.whitelist.add(self.por_rol[role])
        self.intervalos += [('setup', p['setup'], p['fin']),
                            ('camget', self.por_rol['camget'], self.por_rol['camget'] + 0x10),
                            ('screen', self.por_rol['screen'], self.por_rol['screen'] + 0x10)]
        self.hashes = {n: intro.huella(c.m[a:b]) for n, a, b in self.intervalos}

    def _servicio_ronda(self, role):
        def hook(c):
            args = [c.r[4 + i] & ee.M32 for i in range(ARGUMENTOS[role])]
            e = dict(tipo=role, args=args, instante=intro.instante(self.observar()))
            if role in ('fade_ratio', 'ui_field'):
                e['fargs_bits'] = [c.f[12]]
            if role in ('ui_pos', 'ui_escala'):
                esperado = self.ronda['datos'] + (16 if role == 'ui_escala' else 0)
                if args != [esperado]:
                    raise RuntimeError('datos UI ajenos al contrato')
                e['datos_bits'] = [c.r32(esperado + 4 * i) for i in range(4)]
            c.r[2] = 0
            if role == 'ui_ptr':
                if args[0] not in (4, 7):
                    raise RuntimeError('objeto UI fuera del contrato')
                c.r[2] = UI + 16 * args[0]
                e['retorno'] = c.r[2]
            self.eventos.append(e)
        return hook

    def preparar_ronda(self, dificultad=0, angulo=bits(45), gestor=True, estado='intro', reloj=1):
        if (type(dificultad) is not int or not 0 <= dificultad <= 5
                or type(angulo) is not int or angulo not in ANGULOS):
            raise ValueError('dificultad0..5 y angulo de muestras requeridos')
        ref = super().preparar_intro(estado, reloj, gestor)
        c, p = self.cpu, self.ronda
        for a, n in ((CTX, 0x100), (SCREEN, 0x100), (UI, 0x200)):
            c.m[a:a + n] = bytes(n) if a != UI else bytes([0xA5]) * n
            self.guardias += [(a - 16, a), (a + n, a + n + 16)]
            c.m[a - 16:a] = bytes([0xA5]) * 16
            c.m[a + n:a + n + 16] = bytes([0xA5]) * 16
        c.w32(p['ctxp'], CTX); c.w32(p['screenp'], SCREEN)
        c.w32(SCREEN + 0x24, 9)
        for a, valores in ((p['datos'], POS), (p['datos'] + 16, ESCALA)):
            for i, v in enumerate(valores):
                c.w32(a + i * 4, v)
        c.w32(p['angulo'], angulo)
        c.w32(CTX + 4, bits(.0625)); c.w32(CTX + 8, 7); c.w32(CTX + 0x1C, dificultad)
        c.w32(intro.CAM + 0xB4, bits(-17))
        for i, (t, e) in enumerate(zip(TIEMPOS, ENERGIAS)):
            for j, v in enumerate((bits(t), e, 0, 0, 0, 0)):
                c.w32(p['tabla'] + i * 24 + j * 4, v)
        ref.update(energia_bits=bits(.0625), usada=7, lente_bits=bits(-17),
                   ui=[0xA5A5A5A5, 0xA5A5A5A5], dificultad=dificultad,
                   angulo_bits=angulo, camara_bits=[0, 0])
        return ref

    def observar(self):
        r = super().observar()
        c = self.cpu
        r.update(energia_bits=c.r32(CTX + 4), usada=c.r32(CTX + 8),
                 lente_bits=c.r32(intro.CAM + 0xB4), ui=[c.r32(UI + 16 * i) for i in (4, 7)])
        return r

    def llamar_ronda(self):
        if not self.preparado or self.cpu.r32(self.ronda['ctxp']) != CTX:
            raise ValueError('contexto de ronda no preparado')
        c = self.cpu
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.call(self.ronda['setup'], limite=10000)
        if c.io or c.sin_puente:
            raise RuntimeError('SetupRound con E/S o llamada ajena al contrato')
        return dict(self.observar(), eventos=copy.deepcopy(self.eventos),
                    llamadas=dict(Counter(self.roles[a] for a in c.llamadas.elements())))

    def integridad(self, ref):
        c, p = self.cpu, self.ronda
        ctx, ui, screen = bytearray(0x100), bytearray([0xA5] * 0x200), bytearray(0x100)
        camara, control = bytearray(0x200), bytearray(0x100)
        for off, v in ((0xB4, ref['lente_bits']), (0x168, ref['camara_bits'][0]),
                       (0x16C, ref['camara_bits'][1])):
            struct.pack_into('<I', camara, off, v)
        struct.pack_into('<I', control, 0x24, ref['flags'])
        struct.pack_into('<I', control, 0x60, ref.get('control60', 0))
        struct.pack_into('<I', ctx, 0, ref.get('tarea', 0))
        for a, v in ((4, ref['energia_bits']), (8, ref['usada']), (0x1C, ref['dificultad'])):
            struct.pack_into('<I', ctx, a, v)
        for i, v in zip((4, 7), ref['ui']):
            struct.pack_into('<I', ui, i * 16, v)
        struct.pack_into('<I', screen, 0x24, 9)
        tabla = b''.join(struct.pack('<6I', bits(t), e, 0, 0, 0, 0) for t, e in zip(TIEMPOS, ENERGIAS))
        return (super().integridad(ref) and c.m[CTX:CTX + len(ctx)] == ctx
                and c.m[UI:UI + len(ui)] == ui and c.m[SCREEN:SCREEN + len(screen)] == screen
                and c.m[intro.CAM:intro.CAM + len(camara)] == camara
                and c.m[intro.CTRL:intro.CTRL + len(control)] == control
                and c.m[p['tabla']:p['tabla'] + len(tabla)] == tabla
                and c.m[p['datos']:p['datos'] + 32] == struct.pack('<8I', *POS, *ESCALA)
                and c.r32(p['angulo']) == ref['angulo_bits']
                and all(c.r32(a) == v for a, v in ((p['ctxp'], CTX), (p['screenp'], SCREEN),
                    (self.q['camp'], intro.CAM), (self.q['ctrlp'], intro.CTRL),
                    (self.d['mgrp'], intro.MGR if ref['gestor'] else 0))))


def preparar_modelo(s, r, eventos, calls):
    """Modelo propio de las escrituras/solicitudes de SetupRound."""
    actor = intro.MGR if r['gestor'] else 0
    p = s.ronda
    def emitir(role, args=(), fargs=None, datos=None, retorno=None):
        calls[role] += 1
        e = dict(tipo=role, args=list(args), instante=intro.instante(r))
        if fargs is not None:
            e['fargs_bits'] = list(fargs)
        if datos is not None:
            e['datos_bits'] = list(datos)
        if retorno is not None:
            e['retorno'] = retorno
        eventos.append(e)
    emitir('ui_pos', (p['datos'],), datos=POS)
    emitir('ui_escala', (p['datos'] + 16,), datos=ESCALA)
    calls['camget'] += 1
    r['lente_bits'] = lente(r['angulo_bits'])
    emitir('pose', (actor + 0x28D0, 0)); emitir('pose', (actor + 0x800, 10))
    if r['gestor']:
        r['banderas'] = [1, 0]
    calls['camara'] += 1; r['flags'] |= 1
    calls['screen'] += 1
    emitir('ui_field', (9, 0), (0,))
    emitir('timer_init', (TIEMPOS[r['dificultad']],)); emitir('timer_vis', (1,))
    for i in range(4):
        emitir('fade_hidden', (i, 1))
    emitir('ui_flag2', (10, 0)); emitir('fade_ratio', (1, 1), (0,))
    r['energia_bits'] = ENERGIAS[r['dificultad']]
    emitir('fade_ratio', (0, 1), (r['energia_bits'],))
    emitir('fade_ratio', (3, 1), (r['energia_bits'],))
    emitir('ui_ptr', (4,), retorno=UI + 64); r['ui'][0] = 0
    emitir('hud'); emitir('ui_field', (7, 5), (0,)); emitir('ui_flag4', (7, 0))
    emitir('ui_field', (8, 4), (0,))
    emitir('ui_ptr', (7,), retorno=UI + 112); r['ui'][1] = 0
    r['usada'] = 0
    emitir('luz', (0,))


def salida_modelo(ref, eventos, calls):
    r = intro.instante(ref)
    r.update({k: copy.deepcopy(ref[k]) for k in ('energia_bits', 'usada', 'lente_bits', 'ui')})
    r.update(eventos=eventos, llamadas=dict(calls))
    return r


def comprobar(region, elf):
    s, filas, continuos = RondaCell(region, elf), [], []
    def revisar(nombre, want, got):
        if want != got and not any(not f['igual'] for f in filas):
            print(f'Primer DIF {region} {nombre}: campos={[(k, want.get(k), got.get(k)) for k in want if want.get(k) != got.get(k)]}'
                  if isinstance(want, dict) and isinstance(got, dict)
                  else f'Primer DIF {region} {nombre}: esperado={want}, obtenido={got}')
        filas.append(dict(caso=nombre, igual=want == got, esperado=want, obtenido=got))
    for dif, angulo, gestor in itertools.product(range(6), ANGULOS, (False, True)):
        ref = s.preparar_ronda(dif, angulo, gestor)
        for repeticion in range(2):
            eventos, calls = [], Counter()
            preparar_modelo(s, ref, eventos, calls)
            prefijo = f'ronda:{dif}:{angulo:08x}:{gestor}:{repeticion}'
            revisar(prefijo, salida_modelo(ref, eventos, calls), s.llamar_ronda())
            revisar(prefijo + ':integridad', True, s.integridad(ref))
    for dif, paso, gestor, patron in itertools.product(range(6), (bits(.5), bits(1), bits(1.2), bits(2)), (False, True), ('natural', 'salto', 'coincide')):
        ref = s.preparar_ronda(dif, bits(45), gestor, 'preintro', bits(2))
        primera, solicitudes = None, 0
        for n in range(500):
            camara = (bits(2) if n >= 12 else 0, bits(2))
            # Camara termina en12; Intro0 corre en13 e Intro1 termina en14.
            # START en13 saltaria antes del final, sin la doble preparacion.
            pad = 8 if (patron == 'salto' and n >= 4) or (patron == 'coincide' and n >= 14) else 0
            ref['camara_bits'] = list(camara)
            ref, parcial = intro.modelo(s, ref, paso, camara, pad,
                                        lambda r, e, c: preparar_modelo(s, r, e, c))
            want = salida_modelo(ref, parcial['eventos'], parcial['llamadas'])
            solicitudes += parcial['llamadas'].get('setup', 0)
            got = s.pasar_intro(paso, camara, pad)
            prefijo = f'continuo:{dif}:{paso:08x}:{gestor}:{patron}:{n}'
            revisar(prefijo, want, got)
            revisar(prefijo + ':integridad', True, s.integridad(ref))
            if want != got and primera is None:
                primera = n
            if ref['estado'] == s.d['mira']:
                break
        revisar(prefijo + ':llega_Aim', s.d['mira'], ref['estado'])
        revisar(prefijo + ':peticiones_ronda', 2 if patron == 'coincide' else 1, solicitudes)
        continuos.append(dict(dificultad=dif, paso_bits=paso, gestor=gestor, patron=patron,
                              cuadros=n + 1, preparaciones=solicitudes, primera_diferencia=primera))
    for nombre, dif, angulo in [('dificultad_negativa', -1, bits(45)), ('dificultad_seis', 6, bits(45)),
                                ('angulo_fuera_dominio', 0, bits(30))]:
        antes = intro.huella(s.cpu.m)
        try:
            s.preparar_ronda(dif, angulo)
            rechazo = False
        except ValueError:
            rechazo = True
        revisar('rechazo:' + nombre, True, rechazo and antes == intro.huella(s.cpu.m))
    for nombre, a, b in s.intervalos:
        revisar('codigo_intacto:' + nombre, s.hashes[nombre], intro.huella(s.cpu.m[a:b]))
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Ronda Cell {region}: {iguales}/{len(filas)}, primer DIF {primera["caso"] if primera else None}')
    return dict(region=region, elf_sha256=s.digest, perfil=s.ronda, cuerpos_sha256=s.hashes,
                iguales=iguales, total=len(filas), primera_diferencia=primera,
                correcto=iguales == len(filas), continuos=continuos, casos=filas,
                preparaciones_aisladas=6 * len(ANGULOS) * 2 * 2,
                cuadros_continuos=sum(c['cuadros'] for c in continuos))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=intro.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=intro.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    args = p.parse_args()
    inicio = time.monotonic()
    try:
        if args.salida.exists() or not args.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in [('eu', args.elf_eu), ('us', args.elf_us)]]
        report = dict(operacion='ronda_cell', referencia_continua='estado esperado propio, sin realimentar salidas observadas',
                      regiones=regiones, pares_dorado_real=0, integrado_cpp=False,
                      FPU_HW=sorted(ee.FPU_HW), FPU_CERCANO=ee.FPU_CERCANO,
                      limites=['datos de escenario sinteticos; globals vacios del ELF no son tabla real de la mision',
                               'UI/pose/HUD/luces/audio/camara animada controlados, no escena real',
                               'se ejecuta wrapper UiScreen y se observa su peticion UI, no objetos graficos reales',
                               'lente con modelo racional del perfil EE establecido; no prueba hardware nueva',
                               'timer esperado reutiliza EE; se termina al entrar Aim, no minijuego completo'],
                      segundos_banco=round(time.monotonic() - inicio, 3))
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

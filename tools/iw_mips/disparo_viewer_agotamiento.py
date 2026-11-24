# SPDX-License-Identifier: MIT
"""F1: agotamiento 7D8 y continuidad Active -> espera -> resultado, offline.

Ejecuta los cuerpos originales regionales, incluidos ambos ResetUIState.
UI/voz/poses/camara/render son peticiones observadas, no efectos reales.
Incluye aviso de progreso bajo y sus conversion/comparacion soft-double.
"""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import struct
import time

import disparo_viewer_resultado as resultado
import disparo_perseguidor as arit
import ee

CTX, TASK, CAM, GAME = resultado.CTX, resultado.TASK, resultado.CAM, resultado.GAME
TIMER, SCREEN, ACTOR, PLAYER, UI = 0x1804000, 0x1805000, 0x1810000, 0x1820000, 0x1830000
bits, leer, escribir = resultado.bits, resultado.leer, resultado.escribir
PERFILES = {
    'eu': dict(agotamiento=0x326520, reset=0x326690, reset_fin=0x3267F0,
               activo=0x324E30, activo_fin=0x325180, maximos=0x5D98B8,
               timerp=0x49CDE4, screenp=0x49CDE0, actorp=0x49CA70,
               playerp=0x49CDF4, readp=0x49CD9C, pasop=0x49C7DC,
               update=0x5D9920, posiciones=0x5D9750, cams=0x499904,
               agot_calls=(0x31EC90, 0x326690, 0x31ECC0, 0x31E1F0, 0x1FC7C0,
                           0x32E7E0, 0x31E1F0, 0x1FC7C0, 0x32C130, 0x2EF9D0,
                           0x32E7B0, 0x2EF4E0),
               activo_calls=(0x31E5A0, 0x31E5A0, 0x2F0770, 0x119BB0, 0x100280,
                             0x2F0F90, 0x2F0770, 0x32E1D0, 0x2F0F90, 0x2F0F60,
                             0x32E1D0, 0x2F0F90, 0x2F0F60, 0x2EF990, 0x326690,
                             0x31ECC0, 0x2ED8F0, 0x32E7E0, 0x2ED8F0, 0x32E7E0,
                             0x32C250, 0x2F0A30, 0x32E7B0, 0x327F90, 0x2EF4E0),
               reset_calls=(0x32C250, 0x31E1E0, 0x31ECE0, 0x31ECF0, 0x31E880,
                            0x2F0A30, 0x2F0A30, 0x2F0A30, 0x323F60, 0x323E90,
                            0x2EF9E0, 0x2F0A30, 0x2F0F60, 0x2F0A30, 0x32C260, 0x302B70),
               nativos=((0x31EC90, 0x30), (0x31ECC0, 0x20), (0x31E1F0, 0x10),
                        (0x32C130, 0xE0), (0x2EF9D0, 0x10), (0x32E7B0, 0x30),
                        (0x2EF4E0, 0x10), (0x32C250, 0x10), (0x31E1E0, 0x10),
                        (0x31ECE0, 0x10), (0x2EF9E0, 0x30), (0x32C260, 0x10),
                        (0x2EF990, 0x40)),
               hooks=(0x2F0A30, 0x2F0F60, 0x2F0F90, 0x1FC7C0, 0x32E7E0,
                      0x32BDD0, 0x31ECF0, 0x31E880, 0x323F60, 0x323E90,
                      0x302B70, 0x31E5A0, 0x2F0770, 0x32E1D0, 0x2ED8F0, 0x327F90)),
    'us': dict(agotamiento=0x326230, reset=0x3263A0, reset_fin=0x3264E0,
               activo=0x324B40, activo_fin=0x324E90, maximos=0x5D90B8,
               timerp=0x49C5C4, screenp=0x49C5C0, actorp=0x49C270,
               playerp=0x49C5D4, readp=0x49C57C, pasop=0x49BFDC,
               update=0x5D9120, posiciones=0x5D8F50, cams=0x499104,
               agot_calls=(0x31E9A0, 0x3263A0, 0x31E9D0, 0x31DF00, 0x1FC7B0,
                           0x32E4B0, 0x31DF00, 0x1FC7B0, 0x32BE10, 0x2EF7C0,
                           0x32E480, 0x2EF2D0),
               activo_calls=(0x31E2B0, 0x31E2B0, 0x2F0560, 0x119BB0, 0x100280,
                             0x2F0D80, 0x2F0560, 0x32DEA0, 0x2F0D80, 0x2F0D50,
                             0x32DEA0, 0x2F0D80, 0x2F0D50, 0x2EF780, 0x3263A0,
                             0x31E9D0, 0x2ED6E0, 0x32E4B0, 0x2ED6E0, 0x32E4B0,
                             0x32BF30, 0x2F0820, 0x32E480, 0x327C80, 0x2EF2D0),
               reset_calls=(0x32BF30, 0x31DEF0, 0x31E9F0, 0x31EA00, 0x31E590,
                            0x2F0820, 0x2F0820, 0x2F0820, 0x323C70, 0x323BA0,
                            0x2EF7D0, 0x2F0820, 0x2F0D50, 0x2F0820),
               nativos=((0x31E9A0, 0x30), (0x31E9D0, 0x20), (0x31DF00, 0x10),
                        (0x32BE10, 0xE0), (0x2EF7C0, 0x10), (0x32E480, 0x30),
                        (0x2EF2D0, 0x10), (0x32BF30, 0x10), (0x31DEF0, 0x10),
                        (0x31E9F0, 0x10), (0x2EF7D0, 0x30), (0x2EF780, 0x40)),
               hooks=(0x2F0820, 0x2F0D50, 0x2F0D80, 0x1FC7B0, 0x32E4B0,
                      0x32BAC0, 0x31EA00, 0x31E590, 0x323C70, 0x323BA0,
                      None, 0x31E2B0, 0x2F0560, 0x32DEA0, 0x2ED6E0, 0x327C80))}
HOOKS = (('ui_flag', 2, False), ('ui_campo16c', 2, False), ('ui_campo158', 2, True),
         ('voz', 2, False), ('pose_actor', 2, False), ('pose_player', 2, False),
         ('timer_objetos', 1, False), ('fade_hidden', 2, False), ('toggle', 1, False),
         ('spr_flag', 0, False), ('render_reset', 0, False), ('fade_ratio', 2, True),
         ('ui_datos', 1, False), ('impacto', 1, False), ('camara', 4, True), ('sonido', 1, False))


def dividir(a, b):
    q = arit.racional(a) / arit.racional(b)
    signo = (a ^ b) & 0x80000000
    if not q or abs(q) < arit.potencia(-126):
        return signo
    r = arit.truncar(q)
    m = r & 0x7FFFFFFF
    medio = (arit.racional(m) + arit.racional(m + 1)) / 2
    if abs(q) > medio or abs(q) == medio and m & 1:
        r += 1
    return r


def poner_bytes(ref, addr, data):
    for a, b in ref.items():
        if a <= addr and addr + len(data) <= a + len(b):
            b[addr - a:addr - a + len(data)] = data
            return
    raise ValueError('bytes ajenos a referencia')


def huella_estado(lectura, p):
    return dict(estado=lectura(TASK + 0x1C), tiempo_bits=lectura(CTX + 0x10),
                reloj_bits=lectura(TASK + 0x30), timer_estado=lectura(TIMER + 0x10),
                flags_viewer=lectura(PLAYER + 0x3C),
                sfx_handle=lectura(PLAYER + 0x148),
                update=[lectura(p['update'] + 4 * i) for i in range(4)])


class AgotamientoViewer(resultado.ResultadoViewer):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        self.p = dict(self.p, **PERFILES[region])
        c, p = self.cpu, self.p
        for rol, a, z in (('agot', p['agotamiento'], p['reset']),
                           ('activo', p['activo'], p['activo_fin']),
                           ('reset', p['reset'], p['reset_fin'])):
            if resultado.llamadas(c.m, a, z) != p[rol + '_calls']:
                raise ValueError('JAL distintos: ' + rol)
            c.whitelist.add(a)
            self.intervalos.append((a, z))
        for a, n in p['nativos']:
            c.whitelist.add(a)
            self.intervalos.append((a, a + n))
        # Conversion float->double y comparacion <=0.2 del aviso, originales.
        for a, b in ((0x119BB0, 0x119BF0), (0x119B28, 0x119B80),
                     (0x11A680, 0x11A6B0), (0x119BF0, 0x119CC8),
                     (0x100280, 0x1002B0), (0x11A4E0, 0x11A530),
                     (0x119CC8, 0x119D58), (0x11A3E0, 0x11A4E0)):
            c.whitelist.add(a)
            self.intervalos.append((a, b))
        self.hashes = [resultado.huella(c.m[a:b]) for a, b in self.intervalos]
        for a, (rol, nargs, flotante) in zip(p['hooks'], HOOKS):
            if a is not None:
                c.hooks[a] = self._hook(rol, nargs, flotante)
        self.maximos = [c.r32(p['maximos'] + 16 * i) for i in range(6)]
        if self.maximos != list(map(bits, (900, 950, 1000, 1050, 1100, 1200))):
            raise ValueError('maximos distintos del init original')
        self.impacto = 0

    def _hook(self, rol, nargs, flotante):
        def observar(c):
            args = [c.r[4 + i] & ee.M32 for i in range(nargs)]
            ev = dict(tipo=rol, args=args, antes=huella_estado(c.r32, self.p))
            if flotante:
                ev['f12_bits'] = c.f[12]
            self.eventos.append(ev)
            if rol == 'ui_datos':
                if args[0] not in (4, 9):
                    raise RuntimeError('objeto UI fuera del perfil')
                c.r[2] = UI + 16 * args[0]
            elif rol == 'impacto':
                c.r[2] = self.impacto
            else:
                c.r[2] = 0
        return observar

    def preparar_agotamiento(self, dificultad=0, progreso=0, total=bits(5400),
                             restante=bits(900), timer_estado=1, relleno=0xA5, ui_aviso=1):
        if (not resultado.finito(progreso) or abs(arit.racional(progreso)) > 2400
                or not resultado.finito(total) or not resultado.finito(restante)
                or not 0 <= arit.racional(total) <= 60000
                or not -60000 <= arit.racional(restante) <= 60000
                or type(timer_estado) is not int or not 0 <= timer_estado <= 3
                or type(ui_aviso) is not int or ui_aviso not in (0, 1)):
            raise ValueError('entrada fuera del dominio revisado')
        ref = super().preparar(dificultad, bits(123), relleno=relleno)
        c, p = self.cpu, self.p
        for a, n in ((TIMER, 0x100), (SCREEN, 0x100), (ACTOR, 0x3800),
                     (PLAYER, 0x200), (UI, 0x100)):
            c.m[a - 16:a + n + 16] = bytes([relleno]) * (n + 32)
            ref[a - 16] = bytearray(c.m[a - 16:a + n + 16])
        for a, valor in ((p['timerp'], TIMER), (p['screenp'], SCREEN),
                         (p['actorp'], ACTOR), (p['playerp'], PLAYER),
                         (p['readp'], 0), (p['pasop'], bits(1)),
                         (p['cams'], 0x11111111), (p['cams'] + 4, 0x22222222)):
            c.w32(a, valor)
            ref[a] = bytearray(struct.pack('<I', valor))
        # Posiciones artificiales diferenciadas, incluido W; no bind/animacion real.
        posiciones = b''.join(struct.pack('<4I', *map(bits, (i + .5, -i, i + 1, 1))) for i in range(5))
        c.m[p['posiciones']:p['posiciones'] + len(posiciones)] = posiciones
        ref[p['posiciones']] = bytearray(posiciones)
        ref[p['maximos']] = bytearray(c.m[p['maximos']:p['maximos'] + 0x54])
        c.m[p['update']:p['update'] + 16] = bytes([relleno]) * 16
        ref[p['update']] = bytearray(c.m[p['update']:p['update'] + 16])
        for a, v in ((CTX + 4, progreso), (TASK + 0x30, bits(17)),
                     (TIMER + 0x10, timer_estado), (TIMER + 0x14, restante),
                     (TIMER + 0x18, total), (SCREEN + 0x24, 6),
                     (PLAYER + 0x3C, 0x87), (PLAYER + 0x148, 0x12345678),
                     (UI + 4 * 16, ui_aviso), (UI + 9 * 16, 0)):
            c.w32(a, v); escribir(ref, a, v)
        return ref

    def paso_agotamiento(self, operacion='agotamiento', paso=bits(1), impacto=0, camara=None):
        c, p = self.cpu, self.p
        punteros = (('ctxp', CTX), ('camp', CAM), ('gamep', GAME), ('timerp', TIMER),
                    ('screenp', SCREEN), ('actorp', ACTOR), ('playerp', PLAYER), ('readp', 0))
        if not self.preparado or any(c.r32(p[k]) != v for k, v in punteros):
            raise ValueError('punteros fuera del fixture preparado')
        if (operacion not in ('agotamiento', 'estado', 'reset0', 'reset1')
                or type(impacto) is not int or impacto not in (0, 1)
                or paso not in tuple(map(bits, (.5, 1, 1.2, 2)))
                or c.r32(UI + 64) not in (0, 1)):
            raise ValueError('operacion/paso/UI/impacto fuera de perfil')
        estado = c.r32(TASK + 0x1C)
        if operacion == 'estado' and estado not in (p['activo'], p['espera'], p['resultado']):
            raise ValueError('estado fuera de perfil')
        if camara is not None and (operacion != 'estado' or estado != p['espera']
                                  or len(camara) != 2 or not all(resultado.finito(b) for b in camara)):
            raise ValueError('camara finita solo en espera')
        self.impacto = impacto
        self.eventos = []
        c.io.clear(); c.sin_puente.clear(); c.llamadas.clear()
        c.w32(p['pasop'], paso)
        if camara is not None:
            c.w32(CAM + 0x168, camara[0]); c.w32(CAM + 0x16C, camara[1])
        entrada = p['agotamiento'] if operacion == 'agotamiento' else (p['reset'] if operacion.startswith('reset') else estado)
        arg = int(operacion == 'reset1') if operacion.startswith('reset') else TASK
        c.call(entrada, arg, limite=20000)
        if c.io or c.sin_puente:
            raise RuntimeError('E/S o llamada fuera del perfil')
        obs = huella_estado(c.r32, p)
        obs.update(eventos=self.eventos.copy(), config_hex=bytes(c.m[resultado.CONFIG:resultado.CONFIG + 0x54]).hex(),
                   flags=c.r32(GAME + 0x8C))
        if operacion == 'agotamiento':
            obs['retorno'] = c.r[2] & ee.M32
        return obs


def modelo(s, ref, operacion='agotamiento', paso=bits(1), impacto=0, camara=None):
    r, p, eventos = {a: bytearray(b) for a, b in ref.items()}, s.p, []
    escribir(r, p['pasop'], paso)
    def evento(rol, args=(), f12=None):
        ev = dict(tipo=rol, args=[v & ee.M32 for v in args], antes=huella_estado(lambda a: leer(r, a), p))
        if f12 is not None:
            ev['f12_bits'] = f12
        eventos.append(ev)
    def update():
        for i, v in enumerate((1, 0, 1, 1)):
            escribir(r, p['update'] + 4 * i, v)
    def reset(completo):
        if completo:
            poner_bytes(r, ACTOR + 0x14CD, b'\x01'); poner_bytes(r, ACTOR + 0x359D, b'\x01')
            escribir(r, PLAYER + 0x3C, 0)
            src = r[p['posiciones']]
            poner_bytes(r, ACTOR + 0x840, src[:16]); poner_bytes(r, ACTOR + 0x2910, src[0x30:0x40])
        evento('ui_flag', (6, 0))
        escribir(r, TIMER + 0x10, 3)
        if completo:
            evento('timer_objetos', (0,))
            evento('fade_hidden', (0, 0)); evento('fade_hidden', (1, 0))
            evento('ui_flag', (10, 0)); evento('ui_flag', (5, 0))
        evento('ui_flag', (6, 0))
        if completo:
            evento('toggle', (1,)); evento('spr_flag')
        evento('ui_flag', (9, 0)); evento('ui_campo16c', (9, ee.M32))
        if completo:
            evento('ui_flag', (11, 0))
            if s.region == 'eu':
                escribir(r, PLAYER + 0x148, ee.M32)
                evento('render_reset')
    estado = leer(r, TASK + 0x1C)
    ratio = dividir(leer(r, CTX + 4), leer(r, p['maximos'] + 16 * leer(r, CTX + 0x28)))
    agotado = arit.racional(ratio) <= 0
    retorno = None
    if operacion.startswith('reset'):
        reset(operacion == 'reset1')
    elif operacion == 'agotamiento':
        retorno = int(agotado)
        if agotado:
            transcurrido = arit.suma(leer(r, TIMER + 0x18), leer(r, TIMER + 0x14), cop1=True, resta=True)
            escribir(r, CTX + 0x10, transcurrido)
            reset(False)
            # Reset original fija estado3 ANTES de IsFinished: no basta mirar el estado de entrada.
            evento('ui_campo158', (6, 7), 0); evento('voz', (7, ee.M32))
            evento('pose_actor', (ACTOR + 0x28D0, 4))
            escribir(r, TASK + 0x1C, p['activo'])
            evento('pose_player', (3, 0))
            escribir(r, TASK + 0x30, bits(140)); update()
    elif estado == p['activo']:
        evento('fade_ratio', (0, 0), ratio); evento('fade_ratio', (1, 1), ratio)
        evento('ui_datos', (4,))
        if leer(r, UI + 64) == 0 and arit.racional(ratio) <= Fraction(3602879701896397, 18014398509481984):
            escribir(r, UI + 64, 1)
            evento('ui_campo158', (10, 13), 0)
        evento('ui_datos', (9,)); evento('impacto', (ACTOR + 0x28D0,))
        ui9 = leer(r, UI + 9 * 16)
        if ui9 == 0 and impacto:
            evento('ui_campo158', (9, 16), 0); evento('ui_campo16c', (9, ee.M32))
            escribir(r, UI + 9 * 16, 1)
        elif ui9 == 1 and not impacto:
            evento('ui_campo158', (9, 17), 0); evento('ui_campo16c', (9, 15))
            escribir(r, UI + 9 * 16, 0)
        reloj = arit.suma(leer(r, TASK + 0x30), paso, cop1=True, resta=True)
        escribir(r, TASK + 0x30, reloj)
        if arit.racional(reloj) < 0:
            reset(True)
            evento('camara', (0, leer(r, p['cams'] + (0 if agotado else 4)), 1, 0), 0)
            evento('pose_actor', (ACTOR + 0x800, 6 if agotado else 7))
            poner_bytes(r, ACTOR + 0x14CD, b'\x00'); poner_bytes(r, ACTOR + 0x359D, b'\x01')
            escribir(r, PLAYER + 0x3C, 0); escribir(r, TASK + 0x1C, p['espera'])
            evento('ui_flag', (6, 0)); update()
            src = r[p['posiciones']]
            poner_bytes(r, ACTOR + 0x840, src[0x10:0x20]); poner_bytes(r, ACTOR + 0x2910, src[0x40:0x50])
            evento('sonido', (10,))
    else:
        r, obs = resultado.modelo(s, r, camara)
        eventos = obs['eventos']
    obs = huella_estado(lambda a: leer(r, a), p)
    off = resultado.CONFIG - (GAME - 16)
    obs.update(eventos=eventos, config_hex=bytes(r[GAME - 16][off:off + 0x54]).hex(), flags=leer(r, GAME + 0x8C))
    if retorno is not None:
        obs['retorno'] = retorno
    return r, obs


def comprobar(region, elf):
    s, filas, continuos, muestras = AgotamientoViewer(region, elf), [], [], {}
    def revisar(nombre, esperado, obtenido):
        fila = dict(caso=nombre, igual=esperado == obtenido)
        if not fila['igual']:
            fila.update(esperado=esperado, obtenido=obtenido)
            if all(f['igual'] for f in filas):
                print('Primer DIF', region, fila)
        filas.append(fila)
    def ejecutar(nombre, ref, **kw):
        anterior = leer(ref, TASK + 0x1C)
        ref, want = modelo(s, ref, **kw)
        got = s.paso_agotamiento(**kw)
        revisar(nombre, want, got)
        revisar(nombre + ':memoria_abi_guardias', True, s.integridad(ref))
        categoria = nombre.split(':', 1)[0]
        if anterior == s.p['activo'] and want['estado'] == s.p['espera']:
            categoria = 'activo_a_espera'
        muestras.setdefault(categoria, dict(caso=nombre, esperado=want, obtenido=got))
        return ref
    progresos = (0, 0x80000000, 1, 0x80000001, 0x007FFFFF, 0x00800000,
                 0x00800001, bits(-1), bits(-.5), bits(.5), bits(1), bits(900))
    aislados = resets = 0
    for d in range(6):
        frontera = bits(float(arit.racional(s.maximos[d]) * arit.potencia(-126)))
        for pr in (*progresos, frontera - 1, frontera, frontera + 1):
            for ts in range(4):
                ref = s.preparar_agotamiento(d, pr, timer_estado=ts)
                for n in range(2):
                    ref = ejecutar(f'aislado:{d}:{pr:08x}:{ts}:{n}', ref)
                    aislados += 1
    # Diferencias total/restante y condicion inicial Finished: Reset fija3 despues.
    for total in map(bits, (0, 900, 5400)):
        for restante in map(bits, (-1, 0, 900, 5400)):
            for ts in (1, 2):
                ref = s.preparar_agotamiento(total=total, restante=restante, timer_estado=ts)
                ejecutar(f'transcurrido:{total:08x}:{restante:08x}:{ts}', ref)
    for completo in (0, 1):
        for ts in range(4):
            for relleno in (0, 0xA5, 0xFF):
                ref = s.preparar_agotamiento(timer_estado=ts, relleno=relleno)
                for n in range(2):
                    ref = ejecutar(f'reset:{completo}:{ts}:{relleno}:{n}', ref, operacion=f'reset{completo}')
                    resets += 1
    # Continuos sin reinyectar resultados: entrada agotada ->140ticks ->camara ->resultado.
    for d in range(6):
        for paso in map(bits, (.5, 1, 1.2, 2)):
            ref = s.preparar_agotamiento(d, 0, timer_estado=2, ui_aviso=0)
            ref = ejecutar(f'continuo:{d}:{paso:08x}:agotamiento', ref)
            cuadros = 1
            while leer(ref, TASK + 0x1C) == s.p['activo'] and cuadros < 400:
                ref = ejecutar(f'continuo:{d}:{paso:08x}:activo{cuadros}', ref,
                               operacion='estado', paso=paso, impacto=int(cuadros % 3 == 0))
                cuadros += 1
            revisar(f'continuo:{d}:{paso:08x}:entra_espera', s.p['espera'], leer(ref, TASK + 0x1C))
            for i, cam in enumerate(((bits(0), bits(1)), (bits(1) - 1, bits(1)), (bits(1), bits(1)))):
                ref = ejecutar(f'continuo:{d}:{paso:08x}:camara{i}', ref, operacion='estado', paso=paso, camara=cam)
            for i in range(2):
                ref = ejecutar(f'continuo:{d}:{paso:08x}:resultado{i}', ref, operacion='estado', paso=paso)
            continuos.append(dict(dificultad=d, paso_bits=paso, cuadros=cuadros + 5,
                                   peticiones_premio=2))
    avisos = 0
    for d in range(6):
        # Progreso exacto alrededor de20% y vecinos float32 de su cociente.
        centro = bits(float(arit.racional(s.maximos[d]) / 5))
        for pr in (0, bits(-1), centro - 1, centro, centro + 1, s.maximos[d]):
            ref = s.preparar_agotamiento(d, pr, ui_aviso=0)
            s.cpu.w32(TASK + 0x1C, s.p['activo']); escribir(ref, TASK + 0x1C, s.p['activo'])
            for n in range(2):
                ref = ejecutar(f'aviso:{d}:{pr:08x}:{n}', ref, operacion='estado')
                avisos += 1
    # Rama Active no agotada y reloj en frontera0: cero no termina hasta siguiente resta.
    for pr in (bits(.5), bits(1), bits(900)):
        for reloj in map(bits, (-1, 0, .5, 1, 2)):
            ref = s.preparar_agotamiento(progreso=pr)
            for a, v in ((TASK + 0x1C, s.p['activo']), (TASK + 0x30, reloj)):
                s.cpu.w32(a, v); escribir(ref, a, v)
            for n in range(3):
                kw = dict(operacion='estado')
                if leer(ref, TASK + 0x1C) == s.p['espera']:
                    kw['camara'] = (bits(1), bits(1))
                ref = ejecutar(f'activo_positivo:{pr:08x}:{reloj:08x}:{n}', ref, **kw)
    rechazos = (
        ('dificultad bool', lambda: s.preparar_agotamiento(dificultad=True)),
        ('progreso NaN', lambda: s.preparar_agotamiento(progreso=0x7FC00000)),
        ('timer bool', lambda: s.preparar_agotamiento(timer_estado=True)),
        ('paso ajeno', lambda: s.paso_agotamiento(paso=bits(3))),
        ('impacto bool', lambda: s.paso_agotamiento(impacto=True)),
        ('camara en agotamiento', lambda: s.paso_agotamiento(camara=(0, 0))),
        ('UI4 fuera de dominio', lambda: s.paso_agotamiento()),
        ('contexto ajeno', lambda: s.paso_agotamiento()))
    for nombre, accion in rechazos:
        s.preparar_agotamiento()
        if nombre == 'UI4 fuera de dominio':
            s.cpu.w32(UI + 64, 2)
        if nombre == 'contexto ajeno':
            s.cpu.w32(s.p['ctxp'], CTX + 4)
        h = resultado.huella(s.cpu.m)
        try:
            accion(); rechazo = False
        except ValueError:
            rechazo = True
        revisar('rechazo:' + nombre, True, rechazo and resultado.huella(s.cpu.m) == h)
    # Control negativo del oraculo: error deliberado SOLO en una instancia.
    # Si SetState deja2 en vez de3, cambian rama, pose y eventos: debe detectarse.
    setter = 0x31ECE0 if region == 'eu' else 0x31E9F0
    ref = s.preparar_agotamiento(timer_estado=2)
    esperado, want = modelo(s, ref)
    def timer_incorrecto(c):
        c.w32(TIMER + 0x10, 2)
    try:
        s.cpu.hooks[setter] = timer_incorrecto
        got = s.paso_agotamiento()
        revisar('sensibilidad:timer2_detectado_en_salida', False, want == got)
        revisar('sensibilidad:timer2_detectado_en_memoria', False, s.integridad(esperado))
        muestras['mutacion_timer_intencionada'] = dict(esperado=want, obtenido=got,
                                                      diferencia_intencionada=True)
    finally:
        s.cpu.hooks.pop(setter, None)
    ejecutar('sensibilidad:original_restaurado', s.preparar_agotamiento(timer_estado=2))
    for (a, b), h in zip(s.intervalos, s.hashes):
        revisar(f'codigo_intacto:{a:08x}', h, resultado.huella(s.cpu.m[a:b]))
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Agotamiento Viewer {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return dict(region=region, iguales=iguales, total=len(filas), correcto=iguales == len(filas),
                primera_diferencia=primera, llamadas_aisladas=aislados, llamadas_reset=resets,
                llamadas_aviso=avisos, llamadas_transcurrido=24,
                muestras=muestras, controles_sensibilidad=2,
                continuos=continuos, elf_sha256=s.digest, maximos_bits=s.maximos, perfil=s.p,
                intervalos_codigo=[dict(inicio=a, fin_exclusivo=b, sha256=h) for (a, b), h in zip(s.intervalos, s.hashes)],
                casos=filas)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=resultado.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=resultado.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    inicio = time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump(dict(operacion='viewer_agotamiento', regiones=regiones,
                           segundos_banco=round(time.monotonic() - inicio, 3), integrado_cpp=False,
                           pares_dorado_real=0,
                           referencia_continua='estado esperado propio, sin realimentar salidas observadas',
                           limites=['objetos UI4/9 restringidos a0/1; impactos/camara son entradas externas',
                                    'UI, voz, poses, audio, render y camara observados sin ejecutar sus efectos',
                                    'w_READ_BattleMain con tarea nula; sin cancelacion de tarea viva',
                                    'tablas maximos/rango originales; posiciones y handles camara artificiales',
                                    'Reset1 EU borra handle SFX y pide render; USA omite ambos',
                                    'aritmetica racional del perfil EE existente, no autoridad hardware',
                                    'sin juego/emulador/C++ ni equivalencia de minijuego completo'], casos_resumidos=True),
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print('FALLO:', exc)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

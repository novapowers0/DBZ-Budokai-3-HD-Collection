# SPDX-License-Identifier: MIT
"""F1: Viewer Input/ClearPrompt originales y continuidad hasta resultado EU/USA.

OrbitInputUpdate es una peticion sin efectos; acumulador/contadores/impacto y
reloj de mision/camara son entradas controladas. No reproduce colision ni UI.
"""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import time

import disparo_viewer_inicio as inicio
import disparo_viewer_agotamiento as ag
import ee

CTX, TASK, CAM, GAME = inicio.CTX, inicio.TASK, inicio.CAM, inicio.GAME
PLAYER, UI, TIMER, ACTOR = inicio.PLAYER, inicio.UI, inicio.TIMER, inicio.ACTOR
bits, leer, escribir = inicio.bits, inicio.leer, inicio.escribir
PRELUDIO = ('fade_ratio', 'fade_ratio', 'ui_datos', 'double', 'compare',
            'ui_campo158', 'ui_datos', 'impacto', 'ui_campo158', 'ui_campo16c',
            'impacto', 'ui_campo158', 'ui_campo16c')
PERIODOS = (1, 5, 7, 100)
ROLES = {
    'input': PRELUDIO + ('orbita', 'seleccion', 'agotamiento', 'terminado', 'transcurrido',
                         'reset', 'stub', 'screen_field', 'voz', 'seleccion', 'timer_set',
                         'update', 'ui_flag', 'flag4', 'acumulado', 'ui_campo170',
                         'ui_campo170', 'hitcount', 'timer_set', 'memory', 'sonido',
                         'ui_campos158', 'spr_place', 'spr_place'),
    'clear': PRELUDIO + ('flag4', 'acumulado', 'ui_campo170', 'ui_campo170',
                         'seleccion', 'timer_tick', 'toggle', 'spr_flag')}


def observar(lectura, p, bloque_config):
    return dict(inicio.observar(lectura, p), ultimo_hit=lectura(CTX + 12),
                seleccion=lectura(CTX + 0x1A0), semilla=lectura(p['semilla']),
                config_hex=bloque_config.hex(), flags=lectura(GAME + 0x8C))


class InputViewer(inicio.InicioViewer):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        c, p = self.cpu, self.p
        p.update(clear=p['activo_fin'], semilla=c.gp - 0x7FE8,
                 periodo=p['maximos'] - 0x14, umbrales=p['maximos'] - 0x10)
        roles = dict(self.roles)
        for nombre, tam in (('input', 0x410), ('clear', 0x2B0)):
            js = ag.resultado.llamadas(c.m, p[nombre], p[nombre] + tam)
            if len(js) != len(ROLES[nombre]):
                raise ValueError('JAL inesperados: ' + nombre)
            self.jal[nombre] = js
            for addr, rol in zip(js, ROLES[nombre]):
                if rol in roles and roles[rol] != addr:
                    raise ValueError('JAL contradictorio: ' + rol)
                roles[rol] = addr
            c.whitelist.add(p[nombre])
            self.intervalos.append((p[nombre], p[nombre] + tam))
        if roles['agotamiento'] != p['agotamiento'] or roles['memory'] != 0x127CA0:
            raise ValueError('dependencias nativas fuera del perfil')
        # Getter de umbrales deriva de los JAL del selector original de poses.
        selector_calls = ag.resultado.llamadas(c.m, roles['seleccion'], roles['seleccion'] + 0xE0)
        if len(selector_calls) != 8 or selector_calls[0] != roles['pose_player']:
            raise ValueError('selector de poses distinto del perfil')
        getter = selector_calls[1]
        if selector_calls != tuple(getter if i in (1, 3, 4, 6) else roles['pose_player'] for i in range(8)):
            raise ValueError('dependencias del selector de poses distintas del perfil')
        self.parametros_originales = dict(periodo=c.r32(p['periodo']),
                                         umbrales=[c.r32(p['umbrales'] + 4 * i) for i in range(2)])
        if self.parametros_originales != dict(periodo=100, umbrales=[40, 80]):
            raise ValueError('parametros originales distintos del perfil')
        for rol, tam in (('flag4', 0x30), ('acumulado', 0x10), ('hitcount', 0x20), ('memory', 0x40)):
            c.whitelist.add(roles[rol]); self.intervalos.append((roles[rol], roles[rol] + tam))
        c.whitelist.add(getter); self.intervalos.append((getter, getter + 0x20))
        for rol, n, f in (('orbita', 0, False), ('ui_campo170', 2, True),
                          ('ui_campos158', 2, True), ('spr_place', 1, False)):
            c.hooks[roles[rol]] = self._hook(rol, n, f)
        self.roles = roles
        self.hashes = [ag.resultado.huella(c.m[a:b]) for a, b in self.intervalos]

    def preparar_input(self, dificultad=0, progreso=bits(10), acumulado=0, hits=0,
                        pose_hits=0, timer_estado=1, semilla=1, periodo=5,
                        anterior=0, bloqueado=0, estado='input', reloj=bits(1), flags=0x80):
        if (estado not in ('input', 'clear') or not ag.resultado.finito(progreso)
                or abs(ag.arit.racional(progreso)) > 2400
                or not ag.resultado.finito(acumulado) or abs(ag.arit.racional(acumulado)) > 600
                or any(type(x) is not int for x in (hits, pose_hits, timer_estado, semilla,
                                                   periodo, anterior, bloqueado, flags))
                or not -1 <= hits <= 200 or not -1 <= pose_hits <= 200
                or not 0 <= timer_estado <= 3 or not 0 <= semilla <= ee.M32
                or periodo not in PERIODOS or not 0 <= anterior <= 200
                or bloqueado not in (0, 1) or not 0 <= flags <= ee.M32):
            raise ValueError('preparacion fuera del corpus Input')
        ref = self.preparar_inicio(dificultad, estado='ready', reloj=reloj)
        c, p = self.cpu, self.p
        # Ampliar contexto, conservando sus primeros0x100 y renovando las guardias.
        old = bytes(c.m[CTX:CTX + 0x100])
        c.m[CTX - 16:CTX + 0x1C0 + 16] = bytes([0xA5]) * (0x1C0 + 32)
        c.m[CTX:CTX + 0x100] = old
        ref[CTX - 16] = bytearray(c.m[CTX - 16:CTX + 0x1D0])
        for a, n in ((p['semilla'], 4), (p['periodo'], 12)):
            ref[a] = bytearray(c.m[a:a + n])
        for a, v in ((CTX + 4, progreso), (CTX + 8, bloqueado), (CTX + 12, anterior),
                      (CTX + 0x1A0, 0), (TASK + 0x1C, p[estado]), (TIMER + 0x10, timer_estado),
                      (PLAYER + 0xA8, acumulado), (PLAYER + 0x130, pose_hits),
                      (PLAYER + 0x134, hits), (PLAYER + 0x3C, flags),
                      (p['semilla'], semilla), (p['periodo'], periodo)):
            c.w32(a, v); escribir(ref, a, v)
        return ref

    def paso_input(self, operacion='estado', paso=bits(1), mantenido=0, acumulado=0,
                   hits=0, pose_hits=0, impacto=0, camara=(0, bits(1))):
        c, p = self.cpu, self.p
        if (not self.preparado or c.r32(CTX) != TASK
                or any(c.r32(p[k]) != v for k, v in (('ctxp', CTX), ('camp', CAM),
                       ('gamep', GAME), ('timerp', TIMER), ('actorp', ACTOR),
                       ('playerp', PLAYER), ('screenp', ag.SCREEN), ('readp', 0)))):
            raise ValueError('punteros fuera del fixture Input')
        if (operacion not in ('start', 'estado') or type(mantenido) is not int
                or not 0 <= mantenido <= 0xFFFF or not ag.resultado.finito(acumulado)
                or abs(ag.arit.racional(acumulado)) > 600
                or type(hits) is not int or not -1 <= hits <= 200
                or type(pose_hits) is not int or not -1 <= pose_hits <= 200
                or type(impacto) is not int or impacto not in (0, 1)
                or paso not in tuple(map(bits, (.5, 1, 1.2, 2)))
                or len(camara) != 2 or not all(ag.resultado.finito(x) for x in camara)
                or c.r32(UI + 64) not in (0, 1) or c.r32(UI + 144) not in (0, 1)
                or c.r32(p['periodo']) not in PERIODOS):
            raise ValueError('entradas fuera del corpus Input')
        estado = c.r32(TASK + 0x1C)
        if operacion == 'estado' and estado not in tuple(p[k] for k in
                ('input', 'clear', 'intro', 'countdown', 'ready', 'activo', 'espera', 'resultado')):
            raise ValueError('estado fuera del tramo Input')
        self.impacto, self.eventos = impacto, []
        c.io.clear(); c.sin_puente.clear(); c.llamadas.clear()
        for a, v in ((p['pasop'], paso), (PLAYER + 0xA8, acumulado),
                      (PLAYER + 0x130, pose_hits), (PLAYER + 0x134, hits),
                      (CAM + 0x168, camara[0]), (CAM + 0x16C, camara[1])):
            c.w32(a, v)
        c.w16(p['pulsado'] - 2, mantenido)
        c.call(p['start'] if operacion == 'start' else estado,
               *(() if operacion == 'start' else (TASK,)), limite=30000)
        if c.io or c.sin_puente:
            raise RuntimeError('E/S o llamada ajena al perfil Input')
        return dict(observar(c.r32, p, bytes(c.m[ag.resultado.CONFIG:ag.resultado.CONFIG + 0x54])),
                    eventos=self.eventos.copy())


def modelo(s, ref, operacion='estado', paso=bits(1), mantenido=0, acumulado=0,
           hits=0, pose_hits=0, impacto=0, camara=(0, bits(1)), orbita=None):
    r, p = {a: bytearray(b) for a, b in ref.items()}, s.p
    for a, v in ((p['pasop'], paso), (PLAYER + 0xA8, acumulado), (PLAYER + 0x130, pose_hits),
                  (PLAYER + 0x134, hits), (CAM + 0x168, camara[0]), (CAM + 0x16C, camara[1])):
        escribir(r, a, v)
    escribir(r, p['pulsado'] - 2, (leer(r, p['pulsado'] - 2) & 0xFFFF0000) | mantenido)
    estado, eventos = leer(r, TASK + 0x1C), []
    def ev(rol, args=(), f12=None):
        e = dict(tipo=rol, args=[v & ee.M32 for v in args], antes=ag.huella_estado(lambda a: leer(r, a), p))
        if f12 is not None:
            e['f12_bits'] = f12
        eventos.append(e)
    def pose(final=False):
        n, a, b = pose_hits, ee.sx32(leer(r, p['umbrales'])), ee.sx32(leer(r, p['umbrales'] + 4))
        ev('pose_player', (3 if final else (0 if n < a else (1 if n < b else 2)), 0))
    if operacion == 'start' or estado in (p['intro'], p['countdown'], p['ready']):
        r, obs = inicio.modelo(s, r, operacion=operacion, paso=paso,
                              pulsado=leer(r, p['pulsado'] - 2) >> 16, impacto=impacto, camara=camara)
        eventos = obs['eventos']
    elif estado in (p['activo'], p['espera'], p['resultado']):
        r, obs = ag.modelo(s, r, operacion='estado', paso=paso, impacto=impacto,
                           camara=camara if estado == p['espera'] else None)
        eventos = obs['eventos']
    elif estado in (p['input'], p['clear']):
        ratio = ag.dividir(leer(r, CTX + 4), leer(r, p['maximos'] + 16 * leer(r, CTX + 0x28)))
        ev('fade_ratio', (0, 0), ratio); ev('fade_ratio', (1, 1), ratio); ev('ui_datos', (4,))
        if leer(r, UI + 64) == 0 and ag.arit.racional(ratio) <= Fraction(3602879701896397, 18014398509481984):
            escribir(r, UI + 64, 1); ev('ui_campo158', (10, 13), 0)
        ev('ui_datos', (9,)); ev('impacto', (ACTOR + 0x28D0,))
        if leer(r, UI + 144) == 0 and impacto:
            ev('ui_campo158', (9, 16), 0); ev('ui_campo16c', (9, ee.M32)); escribir(r, UI + 144, 1)
        elif leer(r, UI + 144) == 1 and not impacto:
            ev('ui_campo158', (9, 17), 0); ev('ui_campo16c', (9, 15)); escribir(r, UI + 144, 0)
        if estado == p['input']:
            ev('orbita')
            if orbita is not None:
                r, extras = orbita(s, r)
                eventos.extend(extras)
            pose()
            r, obs = ag.modelo(s, r, operacion='agotamiento', paso=paso)
            eventos.extend(obs['eventos'])
            if not obs['retorno'] and leer(r, TIMER + 0x10) == 2:
                escribir(r, CTX + 0x10, 0)
                r, obs_reset = ag.modelo(s, r, operacion='reset0', paso=paso)
                eventos.extend(obs_reset['eventos'])
                ev('ui_campo158', (6, 8), 0); ev('voz', (12, ee.M32))
                escribir(r, TASK + 0x1C, p['activo']); pose(True)
                escribir(r, TASK + 0x30, bits(140))
                for i, v in enumerate((0, 0, 0, 1)):
                    escribir(r, p['update'] + 4 * i, v)
                ev('ui_flag', (6, 1))
        flags = leer(r, PLAYER + 0x3C)
        if mantenido & 0x2000 and not flags & 2:
            escribir(r, PLAYER + 0x3C, flags | 4)
        if ag.arit.racional(acumulado) not in (0, -1):
            escribir(r, CTX + 4, ag.arit.suma(leer(r, CTX + 4), acumulado, cop1=True, resta=True))
            ev('ui_campo170', (5, 105), 0); ev('ui_campo170', (10, 105), 0)
        if estado == p['clear']:
            pose()
            reloj = ag.arit.suma(leer(r, TASK + 0x30), paso, cop1=True, resta=True)
            escribir(r, TASK + 0x30, reloj)
            if ag.arit.racional(reloj) < 0:
                escribir(r, TASK + 0x1C, p['input']); ev('toggle', (0,)); ev('spr_flag')
        elif not leer(r, CTX + 8) and hits > 0 and hits % leer(r, p['periodo']) == 0 and hits != leer(r, CTX + 12):
            escribir(r, CTX + 12, hits); escribir(r, TASK + 0x1C, p['clear'])
            escribir(r, TASK + 0x30, bits(120))
            semilla = leer(r, p['semilla'])
            for _ in range(32):
                semilla = ((semilla << 1) | (((semilla >> 31) ^ (semilla >> 4)) & 1)) & ee.M32
            escribir(r, p['semilla'], semilla)
            seleccion = ((semilla - 1) & ee.M32) & 1
            escribir(r, CTX + 0x1A0, seleccion)
            ev('sonido', (7,)); ev('ui_campos158', (1, 2), 0); ev('spr_place', (9 + seleccion,))
    else:
        raise ValueError('estado esperado fuera de perfil')
    off = ag.resultado.CONFIG - (GAME - 16)
    return r, dict(observar(lambda a: leer(r, a), p,
                            bytes(r[GAME - 16][off:off + 0x54])), eventos=eventos)


def comprobar(region, elf):
    s, filas, muestras, continuos = InputViewer(region, elf), [], {}, []
    def revisar(nombre, want, got):
        f = dict(caso=nombre, igual=want == got)
        if not f['igual']:
            f.update(esperado=want, obtenido=got)
            if all(x['igual'] for x in filas):
                print('Primer DIF', region, nombre)
        filas.append(f)
    def ejecutar(nombre, ref, **kw):
        anterior = leer(ref, TASK + 0x1C)
        ref, want = modelo(s, ref, **kw)
        got = s.paso_input(**kw)
        revisar(nombre, want, got); revisar(nombre + ':memoria_abi_guardias', True, s.integridad(ref))
        categoria = nombre.split(':')[0]
        for a, b, cat in (('input', 'clear', 'input_a_clear'), ('clear', 'input', 'clear_a_input'),
                          ('input', 'activo', 'input_a_activo'), ('activo', 'espera', 'activo_a_espera')):
            if anterior == s.p[a] and want['estado'] == s.p[b]:
                categoria = cat
        if anterior == s.p['resultado']:
            categoria = 'resultado'
        muestras.setdefault(categoria, dict(caso=nombre, esperado=want, obtenido=got))
        return ref
    aislados = 0
    # Cruce: agotamiento/caducidad NO impiden que el prompt posterior sustituya Active.
    for d in range(6):
        for progreso in (bits(-1), 0, bits(1), bits(180), bits(900)):
            for timer in (0, 1, 2, 3):
                for hits in (0, 1, 5, 10):
                    for acc in (bits(-1), 0, bits(.5), bits(10)):
                        ref = s.preparar_input(d, progreso, acc, hits, pose_hits=hits, timer_estado=timer)
                        ejecutar(f'input:{d}:{progreso:08x}:{timer}:{hits}:{acc:08x}', ref,
                                 acumulado=acc, hits=hits, pose_hits=hits, mantenido=0x2000, impacto=hits % 2)
                        aislados += 1
    for hits in (0, 1, 4, 5, 6, 7, 10, 14, 99, 100, 101, 200):
        for periodo in PERIODOS:
            for bloqueado in (0, 1):
                for semilla in (0, 1, 4, 0x12345678, ee.M32):
                    ref = s.preparar_input(hits=hits, periodo=periodo, bloqueado=bloqueado, semilla=semilla)
                    for n in range(2):
                        # Reentrada forzada solo de estado Input; ultimo_hit/semilla persistentes.
                        s.cpu.w32(TASK + 0x1C, s.p['input']); escribir(ref, TASK + 0x1C, s.p['input'])
                        ref = ejecutar(f'prompt:{hits}:{periodo}:{bloqueado}:{semilla}:{n}', ref, hits=hits)
                        aislados += 1
    for flags in (0, 1, 2, 4, 0x80, 0xFFFFFFFF):
        for held in (0, 0x2000, 0xFFFF):
            for reloj in (bits(-1), 0, bits(1) - 1, bits(1), bits(1) + 1):
                for paso in map(bits, (.5, 1, 1.2, 2)):
                    ref = s.preparar_input(estado='clear', reloj=reloj, flags=flags)
                    ejecutar(f'clear:{flags}:{held}:{reloj:08x}:{paso:08x}', ref, paso=paso, mantenido=held, acumulado=bits(.5))
                    aislados += 1
    for pose_hits in (-1, 0, 39, 40, 41, 79, 80, 81, 100):
        for estado in ('input', 'clear'):
            for acumulado in (0x80000000, 1, 0x80000001, 0x007FFFFF, 0x00800000,
                              bits(-1) - 1, bits(-1), bits(-1) + 1, bits(-.5), bits(.1)):
                ref = s.preparar_input(estado=estado, pose_hits=pose_hits)
                ejecutar(f'poses_y_acumulador:{estado}:{pose_hits}:{acumulado:08x}', ref,
                         pose_hits=pose_hits, acumulado=acumulado)
                aislados += 1
    # Arranque original -> Input (prompt opcional) -> agotamiento -> Active -> resultado.
    for d in range(6):
        for paso in map(bits, (.5, 1, 1.2, 2)):
            for prompt in (False, True):
                ref = s.preparar_input(d)
                ref = ejecutar(f'continuo:{d}:{paso}:{prompt}:start', ref, operacion='start', paso=paso)
                n, peticiones, prompts = 0, 0, 0
                while n < 1100 and peticiones < 2:
                    estado = leer(ref, TASK + 0x1C)
                    acc = bits(300) if estado == s.p['input'] else 0
                    ref = ejecutar(f'continuo:{d}:{paso}:{prompt}:{n}', ref, paso=paso,
                                   hits=5 if prompt else 0, pose_hits=5 if prompt else 0,
                                   acumulado=acc, impacto=n % 2, camara=(bits(1), bits(1)))
                    if estado == s.p['input'] and leer(ref, TASK + 0x1C) == s.p['clear']:
                        prompts += 1
                    if estado == s.p['resultado']:
                        peticiones += 1
                    n += 1
                revisar(f'continuo:{d}:{paso}:{prompt}:cierre', 2, peticiones)
                revisar(f'continuo:{d}:{paso}:{prompt}:prompt', int(prompt), prompts)
                continuos.append(dict(dificultad=d, paso_bits=paso, prompt=prompt, cuadros=n, peticiones=peticiones))
    # Dos alteraciones de instrucciones originales: destino del prompt e iteraciones
    # Memory(32)->Memory(16). Invalidar la cache al modificar Y al restaurar.
    for nombre, off, esperado, mascara in (
            ('destino_prompt', 0x360, 0x24420000 | (s.p['clear'] & 0xFFFF), 4),
            ('iteraciones_memory', 0x38C, 0x24040020, 0x30)):
        addr = s.p['input'] + off
        original = s.cpu.r32(addr)
        if original != esperado:
            raise ValueError('ancla de sensibilidad distinta del perfil: ' + nombre)
        ref = s.preparar_input(semilla=0x12345678)
        ref_want, want = modelo(s, ref, hits=5)
        try:
            s.cpu.w32(addr, original ^ mascara); s.cpu.code.pop(addr, None)
            got = s.paso_input(hits=5)
            revisar('sensibilidad:' + nombre + ':observacion', True, want != got)
            revisar('sensibilidad:' + nombre + ':memoria', False, s.integridad(ref_want))
            muestras['mutacion_' + nombre] = dict(esperado=want, alterado=got)
        finally:
            s.cpu.w32(addr, original); s.cpu.code.pop(addr, None)
        ref = s.preparar_input(semilla=0x12345678)
        ejecutar('restaurado:' + nombre, ref, hits=5)
    for nombre, accion in (
            ('dificultad_bool', lambda: s.preparar_input(True)),
            ('periodo_cero', lambda: s.preparar_input(periodo=0)),
            ('hits_bool', lambda: s.paso_input(hits=True)),
            ('semilla_bool', lambda: s.preparar_input(semilla=True)),
            ('acumulado_nan', lambda: s.paso_input(acumulado=0x7FC00000)),
            ('paso_ajeno', lambda: s.paso_input(paso=bits(3))),
            ('mantenido_bool', lambda: s.paso_input(mantenido=True))):
        s.preparar_input()
        h, rechazo = ag.resultado.huella(s.cpu.m), False
        try:
            accion()
        except ValueError:
            rechazo = True
        revisar('rechazo:' + nombre, True, rechazo and h == ag.resultado.huella(s.cpu.m))
    for (a, b), h in zip(s.intervalos, s.hashes):
        revisar(f'codigo:{a:08x}', h, ag.resultado.huella(s.cpu.m[a:b]))
    iguales = sum(f['igual'] for f in filas)
    dif = next((f for f in filas if not f['igual']), None)
    print(f'Input Viewer {region}: {iguales}/{len(filas)}, primer DIF {None if dif is None else dif["caso"]}')
    return dict(region=region, correcto=dif is None, iguales=iguales, total=len(filas), primera_diferencia=dif,
                llamadas_aisladas=aislados, continuos=continuos, cuadros_continuos=sum(c['cuadros'] for c in continuos),
                controles_negativos=2, rechazos=7, parametros_originales=s.parametros_originales,
                muestras=muestras, elf_sha256=s.digest,
                perfil=s.p, jal=s.jal, intervalos_codigo=[dict(inicio=a, fin_exclusivo=b, sha256=h)
                for (a, b), h in zip(s.intervalos, s.hashes)], casos=filas)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=ag.resultado.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=ag.resultado.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    t = time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf8') as f:
            json.dump(dict(operacion='viewer_input', regiones=regiones, segundos_banco=round(time.monotonic()-t, 3),
                           integrado_cpp=False, pares_dorado_real=0,
                           referencia_continua='estado propio sin realimentar salidas observadas',
                           limites=['orbita/UI/pose/voz/audio/scripts/render solo peticiones sin efectos',
                                    'impacto/acumulador/contadores/camara y timer mision son entradas controladas',
                                    'Timer_Init no aplicado; posiciones/handles artificiales; periodos 1/5/7 y original100',
                                    'FPU EE compartida, sin autoridad hardware ni maquina completa B8']),
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, RuntimeError, OSError, NotImplementedError) as exc:
        print('FALLO:', exc)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

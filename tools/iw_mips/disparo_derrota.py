# SPDX-License-Identifier: MIT
"""F1: Fire agotado/altura limite -> GameOver -> WaitResult, offline.

Timer/getters/flags/pareja originales. UI/reset/voz/poses son servicios
tipados controlados; no se ejecutan caducidad, ApplyResult ni mundo real.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_geometria as geometria
import ee

bits = geometria.bits
TIMER = 0x1008000
ANIMACIONES = (0x11110001, 0x11110002)
PERFILES = {
    'eu': {'gameover': 0x30A3D0, 'fin': 0x30A530, 'wait': 0x30A380,
           'anim': (0x4990B4, 0x4990B8)},
    'us': {'gameover': 0x30A1B0, 'fin': 0x30A310, 'wait': 0x30A160,
           'anim': (0x4988B4, 0x4988B8)}}
ROLES = ('hud', 'tick', 'reset', 'uianim', 'pose', 'audio',
         'uianim', 'pose', 'audio', 'flagsword', 'par', 'objeto', 'vozmini', 'vacio')


class DerrotaCell(geometria.GeometriaCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        c, p = self.cpu, PERFILES[region]
        self.p = p
        body = c.m[p['gameover']:p['fin']]
        self.gameover_sha256 = hashlib.sha256(body).hexdigest()
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        if len(calls) != len(ROLES):
            raise ValueError('perfil JAL GameOver cambiado')
        for addr, role in zip(calls, ROLES):
            if addr in self.roles and self.roles[addr] != role:
                raise ValueError('roles de GameOver contradictorios')
            self.roles[addr] = role
        self.por_rol = {r: a for a, r in self.roles.items()}
        c.whitelist.add(p['gameover'])
        for role in ('restante', 'flags', 'flagsword', 'par'):
            addr = self.por_rol[role]
            c.hooks.pop(addr, None)
            c.whitelist.add(addr)
        w = c.r32(self.por_rol['restante'])
        if w >> 26 != 0x23 or (w >> 21 & 31) != 28:
            raise ValueError('getter de tiempo sin global GP comprobable')
        imm = w & 0xFFFF
        self.timerp = c.gp + imm - (0x10000 if imm & 0x8000 else 0)
        self.par_globals = []
        a = self.por_rol['par']
        bases = {}
        for pc in range(a, a + 0x20, 4):
            w = c.r32(pc)
            if w >> 26 == 0x0F:
                bases[w >> 16 & 31] = (w & 0xFFFF) << 16
            elif w >> 26 == 0x2B and (w >> 21 & 31) in bases:
                imm = w & 0xFFFF
                self.par_globals.append(bases[w >> 21 & 31] + imm - (0x10000 if imm & 0x8000 else 0))
        if len(self.par_globals) != 2:
            raise ValueError('pareja global no delimitada')
        for role in ('reset', 'vacio', 'pantalla', 'voz', 'objeto', 'uianim', 'pose', 'audio'):
            c.hooks[self.por_rol[role]] = self._servicio_derrota(role)

    def _servicio_derrota(self, role):
        def hook(c):
            n = {'reset': 1, 'vacio': 0, 'pantalla': 1, 'voz': 2,
                 'objeto': 2, 'uianim': 4, 'pose': 2, 'audio': 2}[role]
            args = [c.r[4 + i] & ee.M32 for i in range(n)]
            if role == 'pose':
                if args[0] != self.mgr + 0x800:
                    raise RuntimeError('pose de personaje ajeno')
                args[0] = 0x800
            event = {'tipo': role, 'args': args}
            if role in ('pantalla', 'uianim'):
                event['f12_bits'] = c.f[12]
            self.eventos.append(event)
            c.r[2] = 0
        return hook

    def preparar_derrota(self, energia, altura=bits(100), fase=3, restante=bits(75)):
        if fase not in (0, 1, 2, 3):
            raise ValueError('fase Fire invalida')
        # Este banco inyecta cero/negativos solo como casos aislados de derrota.
        if energia not in (bits(.5), bits(0), bits(-.05)) or altura not in (
                bits(100), bits(400) - 1, bits(400), bits(400) + 1, bits(800)):
            raise ValueError('fuera de las muestras de agotamiento/altura')
        self.preparar_entrada(bits(.5))
        c = self.cpu
        c.w32(self.ctx + 4, energia); c.w32(self.mgr + 0x2914, altura)
        c.w32(self.d['paso'], bits(1))
        c.w32(self.task + 0x34, fase)
        c.w32(self.task + 0x30, bits(17)); c.w32(self.task + 0x38, bits(23))
        c.w32(self.ctx + 0x10, bits(-17)); c.w32(self.ctx + 0x14, bits(.75))
        c.w32(self.ctx + 0x18, bits(-17))
        c.w32(self.timerp, TIMER); c.w32(TIMER + 0x14, restante)
        c.w32(geometria.fire.CTRL + 0x24, 0x80)
        for a in self.par_globals:
            c.w32(a, 7)
        for a, valor in zip(self.p['anim'], ANIMACIONES):
            c.w32(a, valor)
        c.w8(self.mgr + 0x14CD, 0xA5); c.w8(self.mgr + 0x359D, 0x5A)

    def _llamar(self, addr):
        c = self.cpu
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.call(addr, self.task, limite=100000)
        if c.io or c.sin_puente:
            raise RuntimeError('derrota con E/S o llamada ajena al contrato')
        return {'estado': c.r32(self.task + 0x1C), 'fase': c.r32(self.task + 0x34),
                'reloj_bits': c.r32(self.task + 0x30), 'luces_bits': c.r32(self.task + 0x38),
                'energia_bits': c.r32(self.ctx + 4),
                'resultado': [c.r32(self.ctx + p) for p in (0x10, 0x14, 0x18)],
                'flags': c.r32(geometria.fire.CTRL + 0x24),
                'gestor_flags': [c.r8(self.mgr + p) for p in (0x14CD, 0x359D)],
                'par': [c.r32(a) for a in self.par_globals], 'eventos': self.eventos.copy(),
                'llamadas': dict(Counter(self.roles.get(a, f'nativo:{a:08x}') for a in c.llamadas.elements()))}

    def entrar_derrota(self):
        c = self.cpu
        if c.r32(self.task + 0x1C) != self.d['fire']:
            raise ValueError('la tarea no esta en Fire')
        e, h = ee.fv(c.r32(self.ctx + 4)), ee.fv(c.r32(self.mgr + 0x2914))
        if e > 0 and h < ee.fv(c.r32(self.cam + 0x84)):
            raise ValueError('no hay agotamiento ni altura limite; no fabricar derrota')
        return self._llamar(self.d['fire'])

    def esperar_derrota(self, paso):
        c = self.cpu
        if c.r32(self.task + 0x1C) != self.p['gameover'] or not geometria.carga.normal_positivo(paso):
            raise ValueError(f'solo GameOver con paso normal positivo; estado={c.r32(self.task + 0x1C):#x}, '
                             f'esperado={self.p["gameover"]:#x}, reloj={c.r32(self.task + 0x30):#x}')
        c.w32(self.d['paso'], paso)
        return self._llamar(self.p['gameover'])


def entrada_esperada(s, energia, altura, fase, restante, luces=bits(23), paso=bits(1), flags=0x80):
    r = {'estado': s.p['gameover'], 'fase': fase, 'reloj_bits': bits(140), 'luces_bits': luces,
             'energia_bits': energia, 'resultado': [restante, bits(-1), min(altura, bits(400))],
            'flags': flags | 8, 'par': [0, 0], 'gestor_flags': [0xA5, 0x5A], 'eventos': [
                {'tipo': 'reset', 'args': [0]}, {'tipo': 'vacio', 'args': []},
                {'tipo': 'pantalla', 'args': [8], 'f12_bits': bits(0)},
                {'tipo': 'voz', 'args': [13, ee.M32]}, {'tipo': 'objeto', 'args': [11, 1]}]}
    # ELF USA30A730 sigue con el switch Fire; EU30A950..958 retorna.
    # Mantener contrato regional, no simular una salida temprana que USA no tiene.
    if s.region == 'us':
        if fase == 0:
            r.update(fase=1, reloj_bits=bits(32))
            r['eventos'] += geometria.fire.esperado(energia, 400)['eventos']
        elif fase in (1, 3):
            r['reloj_bits'] = ee.fpu_h('sub', bits(140), paso)
            if fase == 3:
                r['luces_bits'] = ee.fpu_h('add', luces, paso)
                n = int(ee.fv(luces))
                if 21 <= n <= 28:
                    r['eventos'].append({'tipo': 'luz', 'id': 28 - n})
        elif fase == 2:
            r.update(fase=3, reloj_bits=bits(32), luces_bits=bits(0), flags=flags | 10,
                     energia_bits=ee.fpu_h('sub', energia, bits(.1)))
            r['eventos'] += [{'tipo': 'actuar', 'args': [128, 16, 40]},
                            {'tipo': 'transform', 'args': []}, {'tipo': 'efecto', 'id': 3},
                            {'tipo': 'efecto', 'id': 5}, {'tipo': 'landing', 'args': [0x28D0]}]
    return para_comparar(r)


def para_comparar(r):
    # Vectores y llamadas quedan en la observacion cruda. Aqui se comprueban
    # estado/tiempo/energia y contrato de peticiones, no geometria independiente.
    r = {k: v for k, v in r.items() if k != 'llamadas'}
    r['eventos'] = [{k: v for k, v in e.items() if k not in ('pos_bits', 'otros_bits', 'dir_bits')}
                    for e in r['eventos']]
    return r


def eventos_cierre():
    return [{'tipo': 'reset', 'args': [1]},
            {'tipo': 'uianim', 'args': [0, ANIMACIONES[1], 1, 0], 'f12_bits': bits(0)},
            {'tipo': 'pose', 'args': [0x800, 9]}, {'tipo': 'audio', 'args': [3, 0]},
            {'tipo': 'objeto', 'args': [11, 0]}, {'tipo': 'vozmini', 'args': [10]},
            {'tipo': 'vacio', 'args': []}]


def comprobar(region, elf):
    s, filas, continuos, observaciones = DerrotaCell(region, elf), [], [], []
    def revisar(caso, want, got):
        if want != got and not any(not f['igual'] for f in filas):
            print(f'Primer DIF {region}, {caso}: esperado={want}, obtenido={got}')
        filas.append({'caso': caso, 'igual': want == got, 'esperado': want, 'obtenido': got})
    for energia in (bits(.5), bits(0), bits(-.05)):
        for altura in (bits(100), bits(400) - 1, bits(400), bits(400) + 1, bits(800)):
            if energia == bits(.5) and altura < bits(400):
                continue
            for fase in (0, 1, 2, 3):
                s.preparar_derrota(energia, altura, fase)
                got = s.entrar_derrota()
                observaciones.append({'energia_in_bits': energia, 'altura_bits': altura, 'fase_in': fase,
                                      'salida': got})
                revisar(f'entrada:{energia:x}:{altura:x}:{fase}',
                        entrada_esperada(s, energia, altura, fase, bits(75)), para_comparar(got))
    for energia in (bits(0), bits(-.05)):
        duraciones = ((1, 141), (.5, 281), (1.2, 117), (2, 71)) if region == 'eu' else (
            (1, 140), (.5, 279), (1.2, 116), (2, 70))
        for paso, duracion in duraciones:
            s.preparar_derrota(energia)
            revisar(f'continuo:{energia:x}:{paso}:entrada',
                    entrada_esperada(s, energia, bits(100), 3, bits(75)), para_comparar(s.entrar_derrota()))
            reloj = bits(140 if region == 'eu' else 139)
            for n in range(400):
                reloj = ee.fpu_h('sub', reloj, bits(paso))
                fin = ee.fv(reloj) < 0
                got = s.esperar_derrota(bits(paso))
                eventos = eventos_cierre() if fin else []
                revisar(f'continuo:{energia:x}:{paso}:{n}',
                         [s.p['wait'] if fin else s.p['gameover'], reloj, [0, 1] if fin else [0, 0],
                          0 if fin else 0x88, eventos, energia, [bits(75), bits(-1), bits(100)],
                          3, bits(23 if region == 'eu' else 24), [0, 1] if fin else [0xA5, 0x5A]],
                         [got[k] for k in ('estado', 'reloj_bits', 'par', 'flags', 'eventos', 'energia_bits',
                                          'resultado', 'fase', 'luces_bits', 'gestor_flags')])
                if fin:
                    break
            revisar(f'duracion:{energia:x}:{paso}', duracion, n + 1)
            continuos.append({'energia_bits': energia, 'paso_bits': bits(paso), 'ticks': n + 1})
    # Secuencia completa desde Fire0; solo se fija el paso de entrada de cada
    # llamada. No preparar ni escribir contexto/reloj entre las fases.
    secuencias = []
    for energia in (bits(.05), bits(.1) - 1, bits(.1)):
        for paso, ticks_fire, ticks_derrota in ((1, 33, 141), (.5, 65, 281), (1.2, 27, 117), (2, 17, 71)):
            pref = f'fire_agotado:{energia:x}:{paso}'
            s.preparar_derrota(bits(.5), fase=0)
            s.cpu.w32(s.ctx + 4, energia)
            want = geometria.fire.esperado(energia, 400)
            want['luces_bits'] = bits(23)
            revisar(pref + ':entrada', want, s.entrar())
            reloj, luces = bits(32), bits(23)
            for n in range(100):
                want = geometria.fire.esperas.esperado(1, reloj, luces, bits(paso))
                want['energia_bits'] = energia
                revisar(pref + f':espera1:{n}', want, s.pasar(bits(paso)))
                reloj, luces = want['reloj_bits'], want['luces_bits']
                if want['fase'] == 2:
                    break
            revisar(pref + ':duracion1', ticks_fire, n + 1)
            nuevo = ee.fpu_h('sub', energia, bits(.1))
            got = s.ejecutar_geometria()
            observaciones.append({'secuencia': pref, 'fire2': got})
            revisar(pref + ':fire2', [nuevo, bits(32), 3, bits(0), s.d['fire'], [0x82, 1]],
                    [got[k] for k in ('energia_bits', 'reloj_bits', 'fase', 'luces_bits', 'estado')] + [got['target'][:2]])
            revisar(pref + ':agotado', True, ee.fv(nuevo) <= 0)
            want = entrada_esperada(s, nuevo, bits(100), 3, bits(75), bits(0), bits(paso), 0x82)
            revisar(pref + ':gameover', want, para_comparar(s.entrar_derrota()))
            reloj = want['reloj_bits']
            for n in range(400):
                reloj = ee.fpu_h('sub', reloj, bits(paso))
                fin = ee.fv(reloj) < 0
                got = s.esperar_derrota(bits(paso))
                want_tick = dict(want, estado=s.p['wait'] if fin else s.p['gameover'], reloj_bits=reloj,
                                 flags=0 if fin else 0x8A, par=[0, 1] if fin else [0, 0],
                                 gestor_flags=[0, 1] if fin else [0xA5, 0x5A],
                                 eventos=eventos_cierre() if fin else [])
                revisar(pref + f':derrota:{n}', want_tick, para_comparar(got))
                if fin:
                    break
            duracion = ticks_derrota - int(region == 'us')
            revisar(pref + ':duracion_derrota', duracion, n + 1)
            secuencias.append({'energia_in_bits': energia, 'energia_out_bits': nuevo, 'paso_bits': bits(paso),
                               'ticks_fire1': ticks_fire, 'ticks_gameover': n + 1, 'estado_final': got['estado']})
    for nombre, fn in (
        ('no reentrar desde WaitResult', s.entrar_derrota),
        ('no esperar desde WaitResult', lambda: s.esperar_derrota(bits(1))),
        ('no fabricar derrota vivo', lambda: (s.preparar_derrota(bits(.5)), s.entrar_derrota()))):
        try:
            fn()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(nombre, True, rechazo)
    for paso in (0, 1, bits(-1), 0x7F800000, 0x7FC00000):
        s.preparar_derrota(bits(0)); s.entrar_derrota()
        antes = hashlib.sha256(s.cpu.m).hexdigest()
        try:
            s.esperar_derrota(paso)
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(f'paso_invalido:{paso:08x}', [True, antes], [rechazo, hashlib.sha256(s.cpu.m).hexdigest()])
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Derrota Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return {'region': region, 'elf_sha256': s.digest, 'gameover_sha256': s.gameover_sha256,
            'fire_sha256': s.cuerpo_sha256, 'helpers_rangos': s.helpers, 'servicios': s.por_rol,
            'globales_pareja': s.par_globals, 'global_timer': s.timerp,
            'iguales': iguales, 'total': len(filas), 'primera_diferencia': primera,
            'correcto': iguales == len(filas), 'casos': filas, 'continuos': continuos,
            'observaciones_entrada': observaciones, 'secuencias_fire_agotado': secuencias}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=geometria.carga.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=geometria.carga.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    inicio = time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump({'operacion': 'derrota_cell', 'pares_dorado_real': 0, 'integrado_cpp': False,
                        'alcance': 'Fire agotado/altura limite->GameOver->WaitResult y continuo Fire0->1->2 agotado->GameOver->WaitResult',
                       'limites': ['mundo/reset/UI/voz/pose/audio controlados', 'sin caducidad ni ApplyResult',
                                   'sin C++/juego/hardware; no referencia funcional real',
                                    'timer esperado reutiliza FPU EE; no oraculo independiente de FPU',
                                    'vectores y llamadas conservados como observaciones, no comparacion geometrica independiente',
                                    'temporizador de mision fijo75 y servicios sin efecto de mundo; no tiempo real transcurrido'],
                        'diferencia_regional': 'EU retorna tras activar GameOver; USA30A730 todavia ejecuta su fase Fire actual',
                        'regiones': regiones, 'segundos_banco': round(time.monotonic() - inicio, 3)},
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

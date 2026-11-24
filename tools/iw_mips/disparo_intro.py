# SPDX-License-Identifier: MIT
"""F1: PreIntro -> Intro -> Ready -> entrada Aim originales EU/USA, offline.

Ejecuta temporizadores, consulta de camara y escrituras de flags/pareja originales.
SetupRound y servicios audiovisuales son peticiones tipadas SIN efectos. La camara
es entrada externa, no una animacion simulada. No integra codigo al juego.
"""
import argparse
from collections import Counter
import copy
import hashlib
import itertools
import json
from pathlib import Path
import struct
import time

import disparo_banco as banco
import disparo_ready as ready
import ee

bits = ready.bits
MGR, TASK = ready.MGR, ready.TASK
CAM, CTRL = 0x1306000, 0x1307000
ANIMACIONES = (0x1111A001, 0x1111A002)
PERFILES = {
    'eu': dict(intro=0x30B1C0, preintro=0x30B410, fin=0x30B520, setup=0x30C1A0,
               camfin=0x2ED8A0, camp=0x49CD6C, ctrlp=0x49CDC4, pad=0x4ADECE,
               anim=(0x4990B0, 0x4990BC), par=(0x5D94C0, 0x5D94C4)),
    'us': dict(intro=0x30AF50, preintro=0x30B1A0, fin=0x30B2B0, setup=0x30BF30,
               camfin=0x2ED690, camp=0x49C54C, ctrlp=0x49C5A4, pad=0x4AD6CE,
               anim=(0x4988B0, 0x4988BC), par=(0x5D8CC0, 0x5D8CC4))}
INTRO_ROLES = ('camfin', 'flagsword', 'pose', 'pose', 'uianim', 'fatiga',
               'camfin', 'efecto', 'campos164', 'setup', 'settimer', 'fatiga',
               'stopaudio', 'voz', 'sonido', 'vacio',
               'efecto', 'campos164', 'setup', 'settimer', 'fatiga', 'fatiga',
               'stopaudio', 'voz', 'sonido', 'vacio')
PRE_ROLES = ('tick', 'camfin', 'vacio', 'uianim', 'pose', 'pose', 'par',
             'flagsword', 'vozmini', 'playaudio')
ARGUMENTOS = dict(hud=0, pose=2, voz=2, flag=1, timer_ui=1, uianim=4,
                  fatiga=2, efecto=2, campos164=2, setup=0, stopaudio=1,
                  sonido=4, vozmini=1, playaudio=2)


def llamadas(mem, a, b):
    return [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', mem[a:b]) if w >> 26 == 3]


def huella(data):
    return hashlib.sha256(data).hexdigest()


def instante(ref):
    return {k: copy.deepcopy(ref[k]) for k in ('estado', 'reloj_bits', 'banderas', 'flags', 'par')}


class IntroCell(ready.ReadyCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        c, d = self.cpu, self.d
        self.q = q = PERFILES[region]
        for a, b, roles in ((q['intro'], q['preintro'], INTRO_ROLES),
                            (q['preintro'], q['fin'], PRE_ROLES)):
            calls = llamadas(c.m, a, b)
            if len(calls) != len(roles):
                raise ValueError('perfil de llamadas Intro/PreIntro cambiado')
            for addr, role in zip(calls, roles):
                if addr in self.roles and self.roles[addr] != role:
                    raise ValueError('roles regionales contradictorios')
                self.roles[addr] = role
        self.por_rol = {r: a for a, r in self.roles.items()}
        if self.por_rol['camfin'] != q['camfin'] or self.por_rol['setup'] != q['setup']:
            raise ValueError('destinos de camara/SetupRound distintos del perfil')
        self.intervalos = [('ready', d['ready'], d['fin']), ('intro', q['intro'], q['preintro']),
                           ('preintro', q['preintro'], q['fin']),
                           ('tick', d['tick'], d['tick_fin']), ('camfin', q['camfin'], q['camfin'] + 0x30)]
        for role, longitud in (('settimer', 0x10), ('flagsword', 0x10),
                                ('camara', 0x20), ('par', 0x20), ('vacio', 0x10)):
            a = self.por_rol[role]
            self.intervalos.append((role, a, a + longitud))
        for role in ARGUMENTOS:
            c.hooks[self.por_rol[role]] = self._servicio_intro(role)
        for role in ('tick', 'camfin', 'settimer', 'flagsword', 'camara', 'par', 'vacio'):
            c.hooks.pop(self.por_rol[role], None)
            c.whitelist.add(self.por_rol[role])
        c.whitelist.update((q['intro'], q['preintro']))
        self.hashes = {n: huella(c.m[a:b]) for n, a, b in self.intervalos}

    def _servicio_intro(self, role):
        def hook(c):
            e = {'tipo': role, 'args': [c.r[4 + i] & ee.M32 for i in range(ARGUMENTOS[role])],
                 'instante': instante(self.observar())}
            if role == 'uianim':
                e['fargs_bits'] = [c.f[12]]
            elif role == 'sonido':
                e['fargs_bits'] = [c.f[12], c.f[13]]
            self.eventos.append(e)
            c.r[2] = 0
        return hook

    def preparar_intro(self, estado='preintro', reloj=bits(2), gestor=True):
        reloj_valido = (type(reloj) is int and 0 <= reloj <= ee.M32) if estado == 'intro' else ready.finito(reloj)
        if estado not in ('preintro', 'intro', 'ready') or not reloj_valido or type(gestor) is not bool:
            raise ValueError('estado/reloj/gestor fuera del contrato')
        super().preparar(0 if estado == 'intro' else reloj, gestor)
        c, q, d = self.cpu, self.q, self.d
        self.guardias = [(a - 16, a) for a in (MGR, TASK, CAM, CTRL)]
        self.guardias += [(a + n, a + n + 16) for a, n in ((MGR, 0x4000), (TASK, 0x100), (CAM, 0x200), (CTRL, 0x100))]
        for a, b in self.guardias:
            c.m[a:b] = bytes([0xA5]) * (b - a)
        c.m[CAM:CAM + 0x200] = bytes(0x200)
        c.m[CTRL:CTRL + 0x100] = bytes(0x100)
        c.w32(q['camp'], CAM); c.w32(q['ctrlp'], CTRL)
        c.w32(CTRL + 0x24, 0x80)
        for a, valor in zip(q['anim'], ANIMACIONES):
            c.w32(a, valor)
        for a in q['par']:
            c.w32(a, 7)
        c.w32(TASK + 0x1C, q[estado] if estado != 'ready' else d['ready'])
        c.w32(TASK + 0x30, reloj)
        for i in range(16, 24):
            c.r[i], c.up[i] = 0x1122334455660000 + i, 0x99AABBCCDDEE0000 + i
        for i in range(20, 32):
            c.f[i] = bits(10 + i)
        return dict(estado=c.r32(TASK + 0x1C), reloj_bits=reloj,
                    banderas=[0xA5, 0x5A], flags=0x80, par=[7, 7], gestor=gestor)

    def observar(self):
        c = self.cpu
        return dict(estado=c.r32(TASK + 0x1C), reloj_bits=c.r32(TASK + 0x30),
                    banderas=[c.r8(MGR + k) for k in (0x14CD, 0x359D)],
                    flags=c.r32(CTRL + 0x24), par=[c.r32(a) for a in self.q['par']])

    def pasar_intro(self, paso=bits(1), camara=(0, bits(2)), mando=0):
        c, d, q = self.cpu, self.d, self.q
        if c.r32(TASK + 0x1C) not in (q['preintro'], q['intro'], d['ready']):
            raise ValueError('fuera de PreIntro/Intro/Ready')
        if not ready.finito(paso) or ee.fv(paso) <= 0 or any(not ready.finito(x) for x in camara):
            raise ValueError('paso positivo normal y camara finita requeridos')
        if type(mando) is not int or not 0 <= mando <= 0xFFFF:
            raise ValueError('mando debe ser u16')
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.w32(d['paso'], paso); c.w16(q['pad'], mando)
        for a, v in zip((0x168, 0x16C), camara):
            c.w32(CAM + a, v)
        c.call(c.r32(TASK + 0x1C), TASK, limite=10000)
        if c.io or c.sin_puente:
            raise RuntimeError('E/S o llamada ajena al contrato Intro')
        return dict(self.observar(), eventos=copy.deepcopy(self.eventos),
                    llamadas=dict(Counter(self.roles[a] for a in c.llamadas.elements())))

    def integridad(self, ref):
        c = self.cpu
        mgr, task = bytearray(0x4000), bytearray(0x100)
        for off, v in zip((0x14CD, 0x359D), ref['banderas']):
            mgr[off] = v
        # Los bancos derivados pueden ampliar la memoria esperada del gestor;
        # los valores proceden de su modelo propio, nunca de la salida EE.
        for off, valores in ref.get('campos_gestor', ()):
            struct.pack_into('<' + 'I' * len(valores), mgr, off, *valores)
        for off, v in ((0x1C, ref['estado']), (0x30, ref['reloj_bits']), (0x34, 0x12345678)):
            struct.pack_into('<I', task, off, v)
        return (huella(c.m[MGR:MGR + len(mgr)]) == huella(mgr)
                and huella(c.m[TASK:TASK + len(task)]) == huella(task)
                and all(c.m[a:b] == bytes([0xA5]) * (b - a) for a, b in self.guardias)
                and all(c.r[i] == 0x1122334455660000 + i and c.up[i] == 0x99AABBCCDDEE0000 + i for i in range(16, 24))
                and all(c.f[i] == bits(10 + i) for i in range(20, 32))
                and c.r[28] == c.gp and c.r[29] == ee.PILA + 0xFF000)


def modelo(s, previo, paso, camara, mando, preparar_ronda=None):
    """Estado de referencia propio; no recibe salidas del interprete."""
    r, eventos, calls = copy.deepcopy(previo), [], Counter()
    d, q = s.d, s.q
    actor = MGR if r['gestor'] else 0
    cam_fin = ee.fv(camara[0]) >= ee.fv(camara[1])

    def emitir(role, args=(), fargs=None):
        calls[role] += 1
        e = dict(tipo=role, args=list(args), instante=instante(r))
        if fargs is not None:
            e['fargs_bits'] = list(fargs)
        eventos.append(e)

    def banderas(a, b):
        if r['gestor']:
            r['banderas'] = [a, b]

    def completar(salto):
        emitir('efecto', (0, 0)); emitir('campos164', (0, 0))
        if preparar_ronda is not None:
            # Extension explicita del contrato: el banco ronda ejecuta SetupRound.
            # El banco Intro base conserva el puente SIN efectos.
            calls['setup'] += 1
            preparar_ronda(r, eventos, calls)
        else:
            emitir('setup')
        calls['settimer'] += 1
        r.update(estado=d['ready'], reloj_bits=bits(100))
        if salto:
            emitir('fatiga', (actor + 0x800, 1))
        emitir('fatiga', (actor + 0x28D0, 1))
        emitir('stopaudio', (0,)); emitir('voz', (0, ee.M32))
        emitir('sonido', (1, 46, 0, 0), (0, bits(1)))
        calls['vacio'] += 1

    if r['estado'] == q['preintro']:
        calls['tick'] += 1
        r['reloj_bits'] = ee.fpu_h('sub', r['reloj_bits'], paso)
        if ee.fv(r['reloj_bits']) < 0:
            calls['camfin'] += 1
            if cam_fin or mando & 8:
                calls['vacio'] += 1
                emitir('uianim', (0, ANIMACIONES[0], 1, 0), (0,))
                emitir('pose', (actor + 0x28D0, 0)); emitir('pose', (actor + 0x800, 7))
                calls['par'] += 1; r['par'] = [1, 1]
                banderas(0, 1)
                calls['flagsword'] += 1; r['flags'] = 0
                r['estado'] = q['intro']
                emitir('vozmini', (0,)); emitir('playaudio', (1, 0))
                r['reloj_bits'] = 0
    elif r['estado'] == q['intro']:
        if r['reloj_bits'] in (0, 1):
            calls['camfin'] += 1
            if cam_fin:
                if r['reloj_bits'] == 0:
                    banderas(1, 0)
                    calls['flagsword'] += 1; r['flags'] = 0
                    emitir('pose', (actor + 0x800, 10)); emitir('pose', (actor + 0x28D0, 7))
                    emitir('uianim', (0, ANIMACIONES[1], 1, 0), (0,))
                    r['reloj_bits'] = 1
                    emitir('fatiga', (actor + 0x800, 1))
                else:
                    completar(False)
        # Es un segundo if, incluso tras completar naturalmente en ESTA llamada.
        if mando & 8:
            completar(True)
    elif r['estado'] == d['ready']:
        emitir('hud')
        calls['tick'] += 1
        r['reloj_bits'] = ee.fpu_h('sub', r['reloj_bits'], paso)
        if ee.fv(r['reloj_bits']) < 0:
            emitir('pose', (actor + 0x28D0, 1)); banderas(1, 0)
            calls['camara'] += 1; r['flags'] |= 1
            r['estado'] = d['mira']
            emitir('voz', (1, ee.M32)); emitir('flag', (1,)); emitir('timer_ui', (1,))
    else:
        raise ValueError('estado de referencia fuera del contrato')
    got = instante(r)
    got.update(eventos=eventos, llamadas=dict(calls))
    return r, got


def comprobar(region, elf):
    s, filas, continuos = IntroCell(region, elf), [], []
    def revisar(nombre, want, got):
        if want != got and not any(not f['igual'] for f in filas):
            print(f'Primer DIF {region} {nombre}: esperado={want}, obtenido={got}')
        filas.append(dict(caso=nombre, igual=want == got, esperado=want, obtenido=got))
    pares = [(bits(0), bits(2)), (bits(2) - 1, bits(2)), (bits(2), bits(2)),
             (bits(2) + 1, bits(2)), (bits(-1), bits(-2))]
    for estado, relojes in [('preintro', [bits(v) for v in (-1, -0.0, 0, .5, 1, 2)] + [bits(1) - 1, bits(1) + 1]),
                             ('intro', [0, 1, 2, 0xFFFFFFFF, bits(1), bits(-1)])]:
        for reloj, paso, camara, gestor, pad in itertools.product(relojes, (bits(.5), bits(1), bits(1.2), bits(2)), pares, (False, True), (0, 8, 1, 0xFFFF)):
            ref = s.preparar_intro(estado, reloj, gestor)
            ref, want = modelo(s, ref, paso, camara, pad)
            got = s.pasar_intro(paso, camara, pad)
            etiqueta = f'{estado}:{reloj:08x}:{paso:08x}:{camara}:{gestor}:{pad}'
            revisar(etiqueta, want, got)
            revisar(etiqueta + ':memoria_registros_guardias', True, s.integridad(ref))
    for paso, gestor, patron in itertools.product((bits(.5), bits(1), bits(1.2), bits(2)), (False, True), ('natural', 'salto', 'coincide')):
        ref = s.preparar_intro(gestor=gestor)
        primera, visitas, solicitudes = None, Counter(), 0
        for n in range(500):
            camara = (bits(2) if n >= 12 else 0, bits(2))
            # PreIntro sale en12, Intro0 corre en13 y el final natural en14.
            pad = 8 if (patron == 'salto' and n >= 4) or (patron == 'coincide' and n >= 14) else 0
            visitas[ref['estado']] += 1
            ref, want = modelo(s, ref, paso, camara, pad)
            solicitudes += want['llamadas'].get('setup', 0)
            got = s.pasar_intro(paso, camara, pad)
            revisar(f'continuo:{paso:08x}:{gestor}:{patron}:{n}', want, got)
            revisar(f'continuo:{paso:08x}:{gestor}:{patron}:{n}:memoria', True, s.integridad(ref))
            if want != got and primera is None:
                primera = n
            if ref['estado'] == s.d['mira']:
                break
        revisar(f'continuo:{paso:08x}:{gestor}:{patron}:llega_Aim', s.d['mira'], ref['estado'])
        revisar(f'continuo:{paso:08x}:{gestor}:{patron}:preparaciones', 2 if patron == 'coincide' else 1, solicitudes)
        continuos.append(dict(paso_bits=paso, gestor=gestor, patron=patron, cuadros=n + 1,
                              visitas=dict(visitas), preparaciones=solicitudes, primera_diferencia=primera))
    for etiqueta, paso, camara, pad in [('paso_cero', 0, pares[0], 0), ('paso_subnormal', 1, pares[0], 0),
                                      ('paso_negativo', bits(-1), pares[0], 0),
                                      ('camara_NaN', bits(1), (0x7FC00000, 0), 0),
                                      ('pad_no_u16', bits(1), pares[0], 0x10000)]:
        s.preparar_intro()
        antes = huella(s.cpu.m)
        try:
            s.pasar_intro(paso, camara, pad)
            rechazo = False
        except ValueError:
            rechazo = True
        revisar('rechazo:' + etiqueta, True, rechazo and huella(s.cpu.m) == antes)
    for nombre, a, b in s.intervalos:
        revisar('codigo_intacto:' + nombre, s.hashes[nombre], huella(s.cpu.m[a:b]))
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Intro Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return dict(region=region, elf_sha256=s.digest, perfiles=dict(s.q, ready=s.d['ready'], aim=s.d['mira']),
                cuerpos_sha256=s.hashes, iguales=iguales, total=len(filas), primera_diferencia=primera,
                correcto=iguales == len(filas), continuos=continuos, casos=filas)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    args = p.parse_args()
    inicio = time.monotonic()
    try:
        if args.salida.exists() or not args.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in [('eu', args.elf_eu), ('us', args.elf_us)]]
        report = dict(operacion='intro_cell', referencia_continua='estado esperado propio, sin realimentar salidas observadas',
                      regiones=regiones, pares_dorado_real=0, integrado_cpp=False,
                      FPU_HW=sorted(ee.FPU_HW), FPU_CERCANO=ee.FPU_CERCANO,
                      limites=['SetupRound es SOLO una peticion sin efectos, no preparacion real de ronda',
                               'UI/pose/fatiga/audio/efectos observados, no ejecutados',
                               'reloj de camara y START externos controlados, no animacion ni mando reales',
                               'aritmetica esperada del timer comparte EE; no oraculo FPU/hardware independiente',
                               'termina al entrar Aim; no ejecuta Aim/Charge/Fire ni minijuego completo'],
                      segundos_banco=round(time.monotonic() - inicio, 3))
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

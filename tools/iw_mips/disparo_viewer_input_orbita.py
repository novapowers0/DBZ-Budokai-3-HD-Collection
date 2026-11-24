# SPDX-License-Identifier: MIT
"""F1: enlace original Input -> orbita7D8 EU/USA en un mismo estado EE.

Digital sobre tres ejes y analogico +X/y0; atan2/acos/sin/cos originales.
Rotacion final, poses, UI y premios siguen siendo peticiones sin efectos.
No aplica Viewer_Update ni scheduler de poses ni reproduce trayectoria real.
"""
import argparse
from collections import Counter
import itertools
import json
from pathlib import Path
import struct
import time

import disparo_viewer_input as entrada
import disparo_viewer_orbita as orb
import ee

ag, base, bits = entrada.ag, orb.base, orb.bits
leer, escribir = base.leer, base.escribir


def huella_json(valor):
    return base.huella(json.dumps(valor, sort_keys=True, separators=(',', ':'),
                                 allow_nan=False).encode('utf8'))


def resumen(obs):
    eventos = obs['eventos']
    return dict({k: v for k, v in obs.items() if k != 'eventos'},
                eventos_sha256=huella_json(eventos),
                orden_eventos=[e['tipo'] for e in eventos],
                rotaciones=[e['fargs_bits'] for e in eventos if e['tipo'] == 'rotacion_solicitada'])


class InputOrbitaViewer(entrada.InputViewer):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        motor = orb.OrbitaViewer(region, elf)
        c, p = self.cpu, self.p
        if (self.digest != motor.digest or self.roles['orbita'] != motor.p['orbita']
                or self.roles['hitcount'] != motor.p['hit']
                or p['playerp'] != motor.p['hitsp']
                or p['pulsado'] - 2 != motor.p['mando']):
            raise ValueError('enlace regional Input/orbita fuera del perfil')
        p.update({k: motor.p[k] for k in orb.PERFILES[region]})
        p['deadzone'] = motor.p['deadzone']
        p['semillap'] = p['semilla']
        self.orbita = motor
        motor.cpu, motor.p = c, p
        self.intervalos = list(dict.fromkeys(self.intervalos + motor.intervalos))
        c.whitelist.update(a for a, _ in self.intervalos)
        c.hooks.pop(p['orbita'])
        self.nombres = {a: k for k, a in self.roles.items()}
        for a, nombre in motor.roles.items():
            self.nombres.setdefault(a, 'orbita.' + nombre)
        for a, _ in self.intervalos:
            self.nombres.setdefault(a, f'nativo_{a:08x}')
        self.nombres[p['orbita']] = 'orbita'
        # Solo observar la entrada nativa: no ejecutar call() anidada, no tocar
        # registros ni adelantar la referencia con resultados del interprete.
        entrar = c.entrar
        def observar(destino):
            if destino == p['orbita']:
                self.eventos.append(dict(tipo='orbita', args=[],
                                         antes=ag.huella_estado(c.r32, p)))
            return entrar(destino)
        c.entrar = observar
        def rotacion(cpu):
            motor.eventos = self.eventos
            motor._rotacion(cpu)
        c.hooks[0x127160] = rotacion
        self.jal_orbita = [a for a in range(p['input'], p['input'] + 0x410, 4)
                           if c.r32(a) == ((3 << 26) | (p['orbita'] >> 2))]
        if len(self.jal_orbita) != 1:
            raise ValueError('se requiere exactamente un JAL Input -> orbita')
        self.hashes = [base.huella(c.m[a:b]) for a, b in self.intervalos]

    def preparar_enlace(self, dificultad=0, deadzone=16, eje=(0, 0, 1), **kw):
        if (type(deadzone) is not int or not 0 <= deadzone <= 127
                or type(eje) is not tuple or eje not in orb.EJES):
            raise ValueError('deadzone/eje fuera del enlace nativo revisado')
        ref = self.preparar_input(dificultad, **kw)
        c, p = self.cpu, self.p
        # El halfword mantenido y el pulsado ya tienen su propio bloque de4B.
        # Las guardias nuevas NO lo solapan; no hay referencias duplicadas.
        for a, n in ((p['mando'] - 16, 16), (p['mando'] + 4, 28),
                     (p['mando2'] - 16, 48), (p['deadzone'] - 16, 36)):
            if any(a < start + len(data) and start < a + n for start, data in ref.items()):
                raise ValueError('guardias del enlace solapadas')
            c.m[a:a + n] = bytes([0xA5]) * n
            ref[a] = bytearray(c.m[a:a + n])
        for a, data in self.orbita.datos.items():
            ref[a] = bytearray(data)
        c.w32(p['deadzone'], deadzone); escribir(ref, p['deadzone'], deadzone)
        for a, xyz in ((base.CAM + 0x80, (0, 0, 0)), (base.CAM + 0x90, eje)):
            for i, v in enumerate((*xyz, 1)):
                c.w32(a + 4 * i, bits(v)); escribir(ref, a + 4 * i, bits(v))
        self.orbita.preparado = True
        self.entradas_enlace(ref)
        return ref

    def entradas_enlace(self, ref, mando2=0, stick=(0, 0)):
        # La entrada mantenida principal la aplica paso_input; aqui solo las
        # entradas adicionales, guardadas tambien en la referencia propia.
        self.orbita.entradas(ref, mando=leer(ref, self.p['mando']) & 0xFFFF,
                             mando2=mando2, stick=stick)

    def paso_enlace(self, **kw):
        c, p = self.cpu, self.p
        if (not self.preparado or c.r32(p['deadzone']) > 127
                or c.r32(p['hitsp']) != entrada.PLAYER):
            raise ValueError('contexto de enlace alterado')
        obs = self.paso_input(**kw)
        if any(a not in self.nombres for a in c.llamadas):
            raise RuntimeError('llamadas no inventariadas en el enlace')
        obs['llamadas_enlace'] = {k: c.llamadas.get(a, 0) for k, a in (
            ('orbita', p['orbita']), ('solicitud', p['solicitud']),
            ('rotacion', 0x127160), ('atan2', 0x1177C8), ('sin', 0x117688), ('cos', 0x117438))}
        return obs


def modelo(s, ref, **kw):
    calls = dict.fromkeys(('orbita', 'solicitud', 'rotacion', 'atan2', 'sin', 'cos'), 0)
    def enlazar(_s, r):
        esperado, obs = orb.modelo(s.orbita, r)
        calls['orbita'] += 1
        for k in calls:
            if k != 'orbita':
                calls[k] += obs['llamadas'].get(k, 0)
        return esperado, obs['eventos']
    r, obs = entrada.modelo(s, ref, orbita=enlazar, **kw)
    obs['llamadas_enlace'] = calls
    return r, obs


def comprobar(region, elf, clase=InputOrbitaViewer, referencia=modelo, mutaciones_extra=()):
    s, filas, muestras, continuos, negativos = clase(region, elf), [], {}, [], []
    cobertura, ramas = Counter(), Counter()
    def revisar(nombre, want, got):
        f = dict(caso=nombre, igual=want == got, esperado=want, obtenido=got)
        if not f['igual'] and all(t['igual'] for t in filas):
            print('Primer DIF', region, f)
        filas.append(f)
    def ejecutar(nombre, ref, mando2=0, stick=(0, 0), **kw):
        anterior = leer(ref, entrada.TASK + 0x1C)
        s.entradas_enlace(ref, mando2=mando2, stick=stick)
        esperado, want = referencia(s, ref, **kw)
        got = s.paso_enlace(**kw)
        revisar(nombre, resumen(want), resumen(got))
        revisar(nombre + ':memoria_abi_guardias', True, s.integridad(esperado))
        calls = want['llamadas_enlace']
        raiz = 'input' if anterior == s.p['input'] else 'clear' if anterior == s.p['clear'] else 'otro'
        ramas[raiz] += 1
        if calls['orbita']:
            ramas['analogico' if calls['atan2'] else 'digital'] += 1
        cobertura.update({s.nombres[a]: n for a, n in s.cpu.llamadas.items()})
        categoria = nombre.split(':')[0] + ':' + raiz + ':' + ('analogico' if calls['atan2'] else 'digital')
        if got['estado'] != anterior:
            destino = next((k for k in ('input', 'clear', 'activo', 'espera', 'resultado') if got['estado'] == s.p[k]), 'otro')
            categoria += ':a_' + destino
        muestras.setdefault(categoria, dict(caso=nombre, esperado=want, obtenido=got))
        return esperado, got
    aislados = 0
    for h, eje, mask, distrib, estado in itertools.product(
            (2, 3, 39, 40), orb.EJES, (0, 0x10, 0x20, 0x40, 0x80, 0xF0, 0xFFFF), range(3), ('input', 'clear')):
        ref = s.preparar_enlace(estado=estado, pose_hits=h, eje=eje)
        m1, m2 = (mask, 0) if distrib == 0 else (0, mask) if distrib == 1 else (mask, mask ^ 0xF0)
        ejecutar(f'digital:{h}:{eje}:{mask}:{distrib}:{estado}', ref,
                 mantenido=m1, mando2=m2, pose_hits=h)
        aislados += 1
    for h, eje, dz, x in itertools.product((0, 2, 3, 80), orb.EJES, (0, 16, 127), (0, 16, 17, 127)):
        ref = s.preparar_enlace(pose_hits=h, eje=eje, deadzone=dz)
        ejecutar(f'stick:{h}:{eje}:{dz}:{x}', ref, mantenido=0xFFFF, mando2=0xF0,
                 stick=(x, 0), pose_hits=h)
        aislados += 1
    for progreso, timer, hits, bloqueo, eje in itertools.product(
            (0, bits(1), bits(10)), range(4), (0, 5, 10), (0, 1), orb.EJES):
        h = 2 if hits == 0 else 3
        ref = s.preparar_enlace(progreso=progreso, timer_estado=timer, hits=hits,
                                 pose_hits=h, bloqueado=bloqueo, eje=eje, semilla=0x12345678)
        ejecutar(f'agotamiento_prompt:{progreso}:{timer}:{hits}:{bloqueo}:{eje}', ref,
                 hits=hits, pose_hits=h, mantenido=0xF0, mando2=0x10)
        aislados += 1
    # Memoria persistente de Input, variando externamente mando y contador de
    # poses. El acumulador/hits no se generan mediante colision real.
    for eje, paso in itertools.product(orb.EJES, orb.pose.PASOS):
        ref = s.preparar_enlace(eje=eje, bloqueado=1)
        pedidos = 0
        for i in range(96):
            ref, got = ejecutar(f'continuo_input:{eje}:{paso}:{i}', ref, paso=paso,
                                 mantenido=(i % 16) * 16, mando2=((7 * i) % 16) * 16,
                                 stick=(17 if i % 3 == 0 else 16, 0), pose_hits=2 + i % 2,
                                 impacto=i % 2)
            pedidos += got['llamadas_enlace']['rotacion']
        revisar(f'continuo_input:{eje}:{paso}:estado', s.p['input'], s.cpu.r32(entrada.TASK + 0x1C))
        continuos.append(dict(tipo='input_persistente', eje=eje, paso_bits=paso,
                              cuadros=96, peticiones_rotacion=pedidos))
    # Input -> Clear -> Input -> Active -> espera -> resultado, sin forzar
    # estados y sin copiar memoria observada al esperado. Giro no aplicado.
    for d, paso, prompt in itertools.product(range(6), (bits(1), bits(1.2)), (False, True)):
        ref = s.preparar_enlace(d, progreso=bits(12))
        n = premios = prompts = retornos = pedidos = 0
        while n < 1100 and premios < 2:
            anterior = leer(ref, entrada.TASK + 0x1C)
            ref, got = ejecutar(f'continuo_resultado:{d}:{paso}:{prompt}:{n}', ref,
                                 paso=paso, mantenido=(n % 16) * 16 | (0x2000 if n % 2 else 0),
                                 mando2=((7 * n) % 16) * 16, stick=(17 if n % 3 == 0 else 0, 0),
                                 pose_hits=2 + n % 2, hits=5 if prompt and n >= 3 else 0,
                                 acumulado=bits(1) if anterior == s.p['input'] else 0,
                                 impacto=n % 2, camara=(bits(1), bits(1)))
            prompts += int(anterior == s.p['input'] and leer(ref, entrada.TASK + 0x1C) == s.p['clear'])
            retornos += int(anterior == s.p['clear'] and leer(ref, entrada.TASK + 0x1C) == s.p['input'])
            premios += int(anterior == s.p['resultado'])
            pedidos += got['llamadas_enlace']['rotacion']
            n += 1
        tag = f'continuo_resultado:{d}:{paso}:{prompt}'
        revisar(tag + ':premios', 2, premios)
        revisar(tag + ':prompts', int(prompt), prompts)
        revisar(tag + ':retornos_input', int(prompt), retornos)
        continuos.append(dict(tipo='hasta_resultado', dificultad=d, paso_bits=paso, prompt=prompt,
                              cuadros=n, peticiones_premio=premios, prompts=prompts,
                              retornos_input=retornos, peticiones_rotacion=pedidos))
    for nombre, accion in (
            ('deadzone_bool', lambda r: s.preparar_enlace(deadzone=True)),
            ('eje_no_revisado', lambda r: s.preparar_enlace(eje=(1, 0, 0))),
            ('stick_fuera', lambda r: s.entradas_enlace(r, stick=(128, 0))),
            ('stick_bool', lambda r: s.entradas_enlace(r, stick=(True, 0))),
            ('analogico_ajeno', lambda r: s.entradas_enlace(r, stick=(0, 17))),
            ('mando2_fuera', lambda r: s.entradas_enlace(r, mando2=0x10000))):
        ref = s.preparar_enlace()
        antes, rechazo = base.huella(s.cpu.m), False
        try: accion(ref)
        except ValueError: rechazo = True
        revisar('rechazo:' + nombre, True, rechazo)
        revisar('rechazo:' + nombre + ':sin_mutacion', antes, base.huella(s.cpu.m))
    for nombre, addr, malo, kw in (
            ('sin_enlace_orbita', s.jal_orbita[0], 0, dict(mantenido=0x80)),
            ('velocidad_hit3', s.p['orbita'] + 0x2C, 0x28410004, dict(mantenido=0x80, pose_hits=3)),
            ('destino_prompt', s.p['input'] + 0x360, (0x24420000 | (s.p['clear'] & 0xFFFF)) ^ 4, dict(hits=5))) + tuple(mutaciones_extra):
        ref = s.preparar_enlace(semilla=0x12345678)
        esperado, want = referencia(s, ref, **kw)
        original = s.cpu.r32(addr)
        try:
            s.cpu.w32(addr, malo); s.cpu.code.pop(addr, None)
            got = s.paso_enlace(**kw)
            detectado = want != got or not s.integridad(esperado)
            revisar('sensibilidad:' + nombre, True, detectado)
            negativos.append(dict(nombre=nombre, direccion=addr, original=original,
                                   mutado=malo, detectado=detectado,
                                   esperado=resumen(want), obtenido_mutado=resumen(got)))
        finally:
            s.cpu.w32(addr, original); s.cpu.code.pop(addr, None)
        # Input consume flags y puede cambiar de estado incluso en la prueba
        # alterada. Restaurar tambien el fixture, nunca mezclar ref inicial con
        # la memoria que dejo el control negativo.
        ref = s.preparar_enlace(semilla=0x12345678)
        ejecutar('restaurado:' + nombre, ref, **kw)
    for (a, b), sha in zip(s.intervalos, s.hashes):
        revisar(f'codigo_intacto:{a:08x}:{b:08x}', sha, base.huella(s.cpu.m[a:b]))
    primera = next((f for f in filas if not f['igual']), None)
    iguales = sum(f['igual'] for f in filas)
    print(f'Input/orbita Viewer {region}: {iguales}/{len(filas)}, primer DIF {None if primera is None else primera["caso"]}')
    return dict(region=region, correcto=primera is None, iguales=iguales, total=len(filas),
                primera_diferencia=primera, casos=filas, muestras=muestras,
                aislados=aislados, continuos=continuos, ramas=dict(ramas),
                controles_negativos=negativos, cobertura_llamadas=dict(cobertura),
                elf_sha256=s.digest, perfil=s.p, jal_enlace=s.jal_orbita,
                intervalos_codigo=[dict(inicio=a, fin_exclusivo=b, sha256=h)
                                   for (a, b), h in zip(s.intervalos, s.hashes)])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=orb.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=orb.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a, start = p.parse_args(), time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva con padre existente requerida')
        rs = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        out = dict(operacion='viewer_input_orbita', regiones=rs, pares_dorado_real=0,
                   integrado_cpp=False, FPU_HW=sorted(ee.FPU_HW),
                   referencia_continua='memoria propia; Input original llama orbita original sin call anidada',
                   eventos_por_caso='SHA256 del payload completo y orden; muestras completas seleccionadas',
                   segundos_banco=round(time.monotonic() - start, 3),
                   limites=['nativo: digital sobre tres ejes y analogico +X/y0, sin acos binario comparado',
                            'rotacion/poses/UI/premios solo peticiones; sin trayectoria de camara ni colision real',
                            'hits/acumulador/impacto/camara/timer son entradas externas controladas',
                            'sin Viewer_Update/scheduler/poses reales ni C++/dorado/hardware nuevos'])
        with a.salida.open('x', encoding='utf8') as f:
            json.dump(out, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in rs) else 1
    except (ValueError, RuntimeError, OSError, NotImplementedError) as exc:
        print('FALLO:', exc)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

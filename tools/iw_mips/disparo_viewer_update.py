# SPDX-License-Identifier: MIT
"""F1: umbrales/eventos Viewer_Update originales EU/USA y enlace con estados.

Camara/UI/timer/fade son peticiones sin efectos. Los progresos impuestos en
secuencias de umbrales y acumuladores/impactos del enlace son fixtures propios.
No certifica audio, colisiones, reloj de mision, hardware ni integracion C++.
"""
import argparse
from fractions import Fraction
import itertools
import json
from pathlib import Path
import time

import disparo_viewer_input as entrada
import disparo_viewer_agotamiento as ag
import ee

CTX, TASK, GAME = entrada.CTX, entrada.TASK, entrada.GAME
bits, leer, escribir = entrada.bits, entrada.leer, entrada.escribir
HITOS = tuple(range(0x14, 0x28, 4))
PERFILES = {
    'eu': (0x3249C0, (0x119BB0, 0x1002B0, 0x119BB0, 0x1002B0, 0x327F90,
                       0x119BB0, 0x1002B0, 0x327F90, 0x327F90, 0x119BB0,
                       0x100280, 0x2EE340, 0x2F1190, 0x31EB60, 0x31E3D0, 0x2EF4D0)),
    'us': (0x3246D0, (0x119BB0, 0x1002B0, 0x119BB0, 0x1002B0, 0x327C80,
                       0x119BB0, 0x1002B0, 0x327C80, 0x327C80, 0x119BB0,
                       0x100280, 0x2EE130, 0x2F0F80, 0x31E870, 0x31E0E0, 0x2EF2C0))}
SERVICIOS = ('camera_update', 'ui_update', 'timer_update', 'fade_update', 'stub_update')


def hitos(lectura):
    return [lectura(CTX + off) for off in HITOS]


def observar(s, lectura, config):
    return dict(entrada.observar(lectura, s.p, config), hitos=hitos(lectura))


class UpdateViewer(entrada.InputViewer):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        c, p = self.cpu, self.p
        p['viewer_update'], esperadas = PERFILES[region]
        p['umbral25'] = p['maximos'] - 0x18
        js = ag.resultado.llamadas(c.m, p['viewer_update'], p['viewer_update'] + 0x260)
        if js != esperadas or js[4] != self.roles['sonido']:
            raise ValueError('JAL de Viewer_Update fuera del perfil regional')
        if c.r32(p['umbral25']) != bits(.25):
            raise ValueError('umbral inicializado distinto de un cuarto')
        self.jal['viewer_update'] = js
        self.umbral_original = c.r32(p['umbral25'])
        for a, n in ((p['viewer_update'], 0x260), (0x1002B0, 0x20)):
            c.whitelist.add(a); self.intervalos.append((a, a + n))
        self.en_update = False
        previo_sonido = c.hooks[self.roles['sonido']]
        def sonido(cpu):
            previo_sonido(cpu)
            if self.en_update:
                self.eventos[-1]['hitos_antes'] = hitos(cpu.r32)
        c.hooks[self.roles['sonido']] = sonido
        for addr, rol in zip(js[-5:], SERVICIOS):
            c.hooks[addr] = self._servicio(rol)
        self.hashes = [ag.resultado.huella(c.m[a:b]) for a, b in self.intervalos]

    def _servicio(self, rol):
        base = self._hook(rol, 2 if rol == 'stub_update' else 0, rol == 'stub_update')
        def servicio(c):
            base(c)
            self.eventos[-1]['hitos_antes'] = hitos(c.r32)
        return servicio

    def preparar_update(self, dificultad=0, progreso=bits(900), banderas=(1, 1, 1, 1, 1), **kw):
        if (type(banderas) not in (tuple, list) or len(banderas) != 5
                or any(type(x) is not int or not 0 <= x <= ee.M32 for x in banderas)):
            raise ValueError('cinco banderas u32 requeridas')
        ref = self.preparar_input(dificultad=dificultad, progreso=progreso, **kw)
        for off, valor in zip(HITOS, banderas):
            self.cpu.w32(CTX + off, valor); escribir(ref, CTX + off, valor)
        ref[self.p['umbral25']] = bytearray(self.cpu.m[self.p['umbral25']:self.p['umbral25'] + 4])
        return ref

    def _validar_update(self):
        c, p = self.cpu, self.p
        if (not self.preparado or c.r32(p['ctxp']) != CTX or c.r32(CTX) != TASK
                or type(c.r32(CTX + 0x28)) is not int or c.r32(CTX + 0x28) > 5
                or not ag.resultado.finito(c.r32(CTX + 4))
                or abs(ag.arit.racional(c.r32(CTX + 4))) > 2400
                or c.r32(p['umbral25']) != self.umbral_original):
            raise ValueError('contexto/parametros fuera del fixture Update')

    def _actualizar(self):
        c = self.cpu
        self.en_update = True
        try:
            c.call(self.p['viewer_update'], limite=20000)
        finally:
            self.en_update = False
        if c.io or c.sin_puente:
            raise RuntimeError('E/S o llamada ajena al perfil Update')
        return dict(observar(self, c.r32, bytes(c.m[ag.resultado.CONFIG:ag.resultado.CONFIG + 0x54])),
                    eventos=self.eventos.copy())

    def paso_update(self):
        self._validar_update()
        self.eventos = []
        self.cpu.io.clear(); self.cpu.sin_puente.clear(); self.cpu.llamadas.clear()
        return self._actualizar()

    def paso_cuadro(self, **kw):
        # Orden de enlace explicitamente elegido: estado y despues Viewer_Update.
        # No se afirma aqui el orden del planificador completo de tareas del juego.
        self._validar_update()
        self.paso_input(**kw)
        return self._actualizar()


def modelo(s, ref, estado=False, **kw):
    if estado:
        r, obs = entrada.modelo(s, ref, **kw)
        eventos = obs['eventos']
    else:
        r, eventos = {a: bytearray(b) for a, b in ref.items()}, []
    p = s.p
    ratio = ag.arit.racional(ag.dividir(leer(r, CTX + 4),
                                      leer(r, p['maximos'] + 16 * leer(r, CTX + 0x28))))
    def evento(rol, args=(), f12=None):
        ev = dict(tipo=rol, args=list(args), antes=ag.huella_estado(lambda a: leer(r, a), p),
                  hitos_antes=hitos(lambda a: leer(r, a)))
        if f12 is not None:
            ev['f12_bits'] = f12
        eventos.append(ev)
    # Tres comparaciones strictas soft-double; la cuarta es COP1 <1/4;
    # la ultima soft-double <=0. Las banderas se consumen SOLO si valen1.
    umbrales = (Fraction(8106479329266893, 9007199254740992), Fraction(3, 4),
                Fraction(1, 2), ag.arit.racional(leer(r, p['umbral25'])), Fraction(0))
    for i, (off, umbral, sonido) in enumerate(zip(HITOS, umbrales, (None, 12, 13, 9, None))):
        if leer(r, CTX + off) == 1 and (ratio <= umbral if i == 4 else ratio < umbral):
            if sonido is not None:
                evento('sonido', (sonido,))
            escribir(r, CTX + off, 0)
    for rol in SERVICIOS:
        evento(rol, (0x15E, 0x28) if rol == 'stub_update' else (), bits(2) if rol == 'stub_update' else None)
    off = ag.resultado.CONFIG - (GAME - 16)
    return r, dict(observar(s, lambda a: leer(r, a), bytes(r[GAME - 16][off:off + 0x54])), eventos=eventos)


def comprobar(region, elf):
    s, filas, muestras, continuos = UpdateViewer(region, elf), [], {}, []
    def revisar(nombre, esperado, obtenido):
        f = dict(caso=nombre, igual=esperado == obtenido)
        if not f['igual']:
            f.update(esperado=esperado, obtenido=obtenido)
            if all(x['igual'] for x in filas):
                print('Primer DIF', region, nombre)
        filas.append(f)
    def ejecutar(nombre, ref, estado=False, **kw):
        ref, want = modelo(s, ref, estado=estado, **kw)
        got = s.paso_cuadro(**kw) if estado else s.paso_update()
        revisar(nombre, want, got)
        revisar(nombre + ':memoria_abi_guardias', True, s.integridad(ref))
        categoria = nombre if nombre.startswith('fronteras:') and nombre.endswith(':(1, 1, 1, 1, 1):0') else nombre.split(':')[0]
        muestras.setdefault(categoria, dict(caso=nombre, esperado=want, obtenido=got))
        return ref
    aislados = 0
    for d, maximo in enumerate((900, 950, 1000, 1050, 1100, 1200)):
        progresos = {0, 0x80000000, 1, 0x80000001, 0x007FFFFF, 0x00800000, bits(-1), bits(maximo)}
        for umbral in (.9, .75, .5, .25):
            b = bits(maximo * umbral)
            progresos.update((b - 1, b, b + 1))
        for progreso in sorted(progresos):
            for banderas in itertools.product((0, 1), repeat=5):
                ref = s.preparar_update(d, progreso, banderas)
                for n in range(2):
                    ref = ejecutar(f'fronteras:{d}:{progreso:08x}:{banderas}:{n}', ref)
                    aislados += 1
    # Exhaustividad sobre flags no booleanas:2 y FFFFFFFF NO son "pendiente".
    for banderas in itertools.product((0, 1, 2, ee.M32), repeat=5):
        ref = s.preparar_update(progreso=bits(-1), banderas=banderas)
        for n in range(2):
            ref = ejecutar(f'flags_no_booleanas:{banderas}:{n}', ref)
            aislados += 1
    # Progreso impuesto por el fixture, no generado por colision/acumulador real.
    for d, maximo in enumerate((900, 950, 1000, 1050, 1100, 1200)):
        for ida_vuelta in (False, True):
            ref = s.preparar_update(d)
            secuencia = (1, .9, .89, .75, .74, .5, .49, .25, .24, 0, -.01)
            if ida_vuelta:
                secuencia += tuple(reversed(secuencia))
            for n, fraccion in enumerate(secuencia):
                progreso = bits(maximo * fraccion)
                s.cpu.w32(CTX + 4, progreso); escribir(ref, CTX + 4, progreso)
                ref = ejecutar(f'progreso_persistente:{d}:{ida_vuelta}:{n}', ref)
            revisar(f'progreso_persistente:{d}:{ida_vuelta}:agotadas', [0] * 5,
                    hitos(lambda a: leer(ref, a)))
            continuos.append(dict(tipo='progreso_controlado', dificultad=d, ida_vuelta=ida_vuelta,
                                  cuadros=len(secuencia)))
    # Enlace explicito estado->Update; no se presume scheduler completo.
    for d in range(6):
        for paso in map(bits, (.5, 1, 1.2, 2)):
            for prompt in (False, True):
                ref = s.preparar_update(d)
                for ciclo in range(2):
                    prefijo = f'enlace:{d}:{paso}:{prompt}:{ciclo}'
                    ref = ejecutar(prefijo + ':start', ref, estado=True, operacion='start', paso=paso)
                    n = premios = rearmes = prompts = 0
                    while n < 1100 and premios < 2:
                        anterior = leer(ref, TASK + 0x1C)
                        ref = ejecutar(f'{prefijo}:{n}', ref, estado=True, paso=paso,
                                       acumulado=bits(300) if anterior == s.p['input'] else 0,
                                       hits=5 if prompt else 0, pose_hits=5 if prompt else 0,
                                       impacto=n % 2, camara=(bits(1), bits(1)))
                        actual = leer(ref, TASK + 0x1C)
                        if anterior == s.p['countdown'] and actual == s.p['ready']:
                            revisar(prefijo + ':rearme_observado', [1] * 5, hitos(s.cpu.r32))
                            rearmes += 1
                        prompts += int(anterior == s.p['input'] and actual == s.p['clear'])
                        premios += int(anterior == s.p['resultado'])
                        n += 1
                    revisar(prefijo + ':cierre', 2, premios)
                    revisar(prefijo + ':agotadas', [0] * 5, hitos(lambda a: leer(ref, a)))
                    revisar(prefijo + ':rearme_unico', 1, rearmes)
                    continuos.append(dict(tipo='estado_y_update', dificultad=d, paso_bits=paso,
                                          prompt=prompt, ciclo=ciclo, cuadros=n, peticiones_premio=premios,
                                          rearmes=rearmes, prompts=prompts))
    # Tres fallos intencionados, restaurando instrucciones y su cache en finally.
    for nombre, off, original, alterado, progreso in (
            ('sonido12_a11', 0xE0, 0x2404000C, 0x2404000B, bits(600)),
            ('lt25_a_le25', 0x19C, 0x46000834, 0x46000836, bits(225)),
            ('no_consumir_flag75', 0xE8, 0xAC400018, 0, bits(600))):
        addr = s.p['viewer_update'] + off
        if s.cpu.r32(addr) != original:
            raise ValueError('ancla de sensibilidad inesperada: ' + nombre)
        ref = s.preparar_update(progreso=progreso)
        ref_want, want = modelo(s, ref)
        try:
            s.cpu.w32(addr, alterado); s.cpu.code.pop(addr, None)
            got = s.paso_update()
            revisar('sensibilidad:' + nombre + ':observacion', True, want != got)
            if nombre != 'sonido12_a11':
                revisar('sensibilidad:' + nombre + ':memoria', False, s.integridad(ref_want))
            muestras['mutacion_' + nombre] = dict(esperado=want, alterado=got)
        finally:
            s.cpu.w32(addr, original); s.cpu.code.pop(addr, None)
        ref = s.preparar_update(progreso=progreso)
        ejecutar('restaurado:' + nombre, ref)
    for nombre, accion in (
            ('dificultad_bool', lambda: s.preparar_update(True)),
            ('progreso_nan', lambda: s.preparar_update(progreso=0x7FC00000)),
            ('flags_longitud', lambda: s.preparar_update(banderas=(1,))),
            ('flags_bool', lambda: s.preparar_update(banderas=(1, 1, True, 1, 1))),
            ('flags_negativa', lambda: s.preparar_update(banderas=(1, 1, -1, 1, 1))),
            ('paso_ajeno', lambda: s.paso_cuadro(paso=bits(3)))):
        s.preparar_update()
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
    print(f'Update Viewer {region}: {iguales}/{len(filas)}, primer DIF {None if dif is None else dif["caso"]}')
    return dict(region=region, correcto=dif is None, iguales=iguales, total=len(filas), primera_diferencia=dif,
                llamadas_aisladas=aislados, continuos=continuos,
                cuadros_continuos=sum(c['cuadros'] for c in continuos), controles_negativos=3,
                rechazos=6, muestras=muestras, elf_sha256=s.digest, perfil=s.p, jal=s.jal,
                umbral25_bits=s.umbral_original, intervalos_codigo=[dict(inicio=a, fin_exclusivo=b, sha256=h)
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
            json.dump(dict(operacion='viewer_update', regiones=regiones,
                           segundos_banco=round(time.monotonic() - t, 3), integrado_cpp=False,
                           pares_dorado_real=0, referencia_continua='estado propio sin realimentar observados',
                           limites=['camera/UI/timer/fade/audio/pose/scripts/render/premios solo peticiones',
                                    'enlace estado->Update elegido; scheduler completo no validado',
                                    'progreso/acumulador/contadores/impacto/camara/timer mision controlados',
                                    'Timer_Init sin efectos; FPU EE compartida, sin autoridad hardware/B8 completo']),
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, RuntimeError, OSError, NotImplementedError) as exc:
        print('FALLO:', exc)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

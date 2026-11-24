# SPDX-License-Identifier: MIT
"""F1: carga Cell EU/USA con mundo controlado, estrictamente offline.

Ejecuta MgShoot_St_Charge completo del ELF del usuario. Solo se admite el
dominio vivo/sin caducidad: el HUD y los servicios del mundo son puentes
explicitos, no se prueban sus implementaciones. Comprueba energia, transicion
y peticiones de servicios; no es un dorado ni una maquina completa.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import time

import disparo_banco as banco
import ee

CTX, MGR, CAM, TASK = 0x1000000, 0x1001000, 0x1005000, 0x1006000
# Las llamadas se leen en orden de sus sitios JAL. No trasladar un delta
# universal: Camera/sonido/efectos y contexto tienen desplazamientos distintos.
ROLES = ('hud', 'tiempo', 'camara', 'restante', 'camara', 'camara',
         'reset', 'vacio', 'pantalla', 'voz', 'timer', 'par', 'flags',
         'objeto', 'freeze', 'efecto', 'parar_sonido')
PERFILES = {
    'eu': {'elf': banco.ELF_EU, 'gp': 0x4A3DF0, 'init': 0x30D150,
           'carga': 0x30A5C0, 'fin': 0x30A800, 'mira': 0x30ADE0,
           'ctxp': 0x49CDBC, 'mgrp': 0x49CA70, 'paso': 0x49C7DC,
           'mando': 0x4ADECC, 'escala': 0x5D9434,
           'cuerpo_sha256': 'c6b9543098c2f226b2c0cfcdc410adc1e4004b3ab7ca738c0644e557fb4d25ef'},
    'us': {'elf': banco.ELF_US, 'gp': 0x4A35F0, 'init': 0x30CEE0,
           'carga': 0x30A3A0, 'fin': 0x30A5E0, 'mira': 0x30AB80,
           'ctxp': 0x49C59C, 'mgrp': 0x49C270, 'paso': 0x49BFDC,
           'mando': 0x4AD6CC, 'escala': 0x5D8C34,
           'cuerpo_sha256': 'dfe8cd38e209c5096224971380cb4e8ce41e3a33135e8d25dfbd702c8eefde39'}}


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def normal_positivo(value):
    return type(value) is int and 0 <= value <= 0xFFFFFFFF and not value >> 31 and 0 < (value >> 23 & 255) < 255


class CargaCell:
    """Adaptador sintetico: nunca inventa retorno cero para llamadas desconocidas."""
    def __init__(self, region, elf=None, scratch=CTX):
        if type(scratch) is not int or scratch & 0xFFF or not 0x1000000 <= scratch <= ee.RAM - 0x7000:
            raise ValueError('scratch fuera de RAM o no alineado')
        self.ctx, self.mgr, self.cam, self.task = scratch, scratch + 0x1000, scratch + 0x5000, scratch + 0x6000
        self.region, self.d = region, PERFILES[region]
        d = self.d
        mem, self.digest = banco.cargar_elf(elf or d['elf'])
        if self.digest != banco.ELF_HASH[region]:
            raise ValueError('ELF distinto del perfil regional verificado')
        body = mem[d['carga']:d['fin']]
        if hashlib.sha256(body).hexdigest() != d['cuerpo_sha256']:
            raise ValueError('cuerpo Charge distinto del censado')
        self.cpu = ee.Ee(mem, d['gp'])
        self.cpu.call(d['init'], limite=100000)
        if self.cpu.io or self.cpu.sin_puente:
            raise RuntimeError('init con E/S o llamadas desconocidas')
        self.escala_bits = self.cpu.r32(d['escala'])
        if self.escala_bits != bits(2.0):
            raise ValueError(f'init no produjo escala de carga 2.0: {self.escala_bits:#x}')
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        if len(calls) != len(ROLES):
            raise ValueError('frontera JAL de Charge cambiada')
        self.puentes = {}
        for addr, role in zip(calls, ROLES):
            if addr in self.puentes and self.puentes[addr] != role:
                raise ValueError('un destino tiene roles de puente contradictorios')
            self.puentes[addr] = role
        self.eventos = []
        for addr, role in self.puentes.items():
            self.cpu.hooks[addr] = self._puente(role)
        self.cpu.whitelist = {d['carga']}
        self.cpu.estricto = True
        self.preparado = False

    def _puente(self, role):
        def hook(c):
            if role == 'camara':
                c.r[2] = self.cam
            elif role == 'tiempo':
                if c.r[4] & ee.M32 != self.task:
                    raise RuntimeError('CheckTimeUp recibio tarea equivocada')
                c.r[2] = 0  # dominio: tiempo positivo, caducidad fuera de este paquete
            elif role == 'hud':
                c.r[2] = 0  # HUD lo conserva el port; no certifica su implementacion
            elif role == 'freeze':
                self.eventos.append({'tipo': role, 'a0': c.r[4] & ee.M32})
            elif role == 'efecto':
                self.eventos.append({'tipo': role, 'a0': c.r[4] & ee.M32, 'a1': c.r[5] & ee.M32})
            elif role == 'parar_sonido':
                if c.r[4] & ee.M32 != self.ctx + 0x20:
                    raise RuntimeError('puntero de sonido fuera del contexto propio')
                self.eventos.append({'tipo': role, 'handle': c.r32(self.ctx + 0x20)})
            else:
                raise RuntimeError(f'puente fuera del dominio vivo: {role}')
        return hook

    def preparar(self, energia_bits, altura_bits=bits(100.0), sonido=0xFFFFFFFF):
        altura = ee.fv(altura_bits) if type(altura_bits) is int and 0 <= altura_bits <= ee.M32 else -1
        if not normal_positivo(energia_bits) or ee.fv(energia_bits) > 1:
            raise ValueError('energia debe ser float32 normal en (0,1]')
        if not math.isfinite(altura) or not 0 <= altura < 400 or (altura_bits >> 23 & 255) == 255:
            raise ValueError('altura debe ser float32 finito en [0,400)')
        if type(sonido) is not int or not 0 <= sonido <= ee.M32:
            raise ValueError('handle sonido fuera de u32')
        c, d = self.cpu, self.d
        for addr, size in ((self.ctx, 0x100), (self.mgr, 0x3000), (self.cam, 0x100), (self.task, 0x100)):
            c.m[addr:addr + size] = bytes(size)
        c.w32(d['ctxp'], self.ctx)
        c.w32(d['mgrp'], self.mgr)
        c.w32(self.ctx + 4, energia_bits)
        c.w32(self.ctx + 0x20, sonido)
        c.w32(self.mgr + 0x2914, altura_bits)
        c.w32(self.cam + 0x84, bits(400.0))
        c.w32(self.task + 0x1C, d['carga'])
        self.preparado = True

    def pasar(self, mantenido=0x8000, paso_bits=bits(1.0)):
        if not self.preparado or self.cpu.r32(self.task + 0x1C) != self.d['carga']:
            raise ValueError('la tarea no esta en Charge; no se reentra sin preparar')
        if type(mantenido) is not int or not 0 <= mantenido <= 0xFFFF or not normal_positivo(paso_bits):
            raise ValueError('mando debe ser u16 y paso float32 normal positivo')
        c, d = self.cpu, self.d
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.w32(d['paso'], paso_bits)
        c.w16(d['mando'], mantenido)
        c.call(d['carga'], self.task, limite=5000)
        if c.io or c.sin_puente:
            raise RuntimeError(f'E/S o llamada no cubierta: {dict(c.io)}, {dict(c.sin_puente)}')
        estado = c.r32(self.task + 0x1C)
        if estado not in (d['carga'], d['mira']):
            raise RuntimeError(f'estado fuera de Charge/Aim: {estado:#x}')
        return {'energia_bits': c.r32(self.ctx + 4), 'volver_mira': estado == d['mira'],
                'sonido': c.r32(self.ctx + 0x20), 'eventos': self.eventos.copy(),
                'puentes': dict(Counter(self.puentes[a] for a in c.llamadas.elements()))}

    def adaptar(self, energia, altura, mando):
        """Modelo del marshalling C++ (no ejecuta C++): init + contexto propio por tick.

        Los cinco globals se restauran incluso al fallar un servicio. No implica
        equivalencia de Ambito ni del interprete C++: Z debe compilar y medirlo.
        """
        c, d = self.cpu, self.d
        dirs = [d[k] for k in ('ctxp', 'mgrp', 'paso', 'mando', 'escala')]
        anteriores = [c.r32(a) for a in dirs]
        try:
            c.call(d['init'], limite=100000)
            if c.io or c.r32(d['escala']) != bits(2.0):
                return None
            self.preparar(energia, altura)
            return self.pasar(mando, bits(1.0))
        except (ValueError, RuntimeError):
            return None
        finally:
            for a, v in zip(dirs, anteriores):
                c.w32(a, v)


def esperado(energia, mantenido, paso, sonido):
    """Formula semantica documentada; reutiliza reglas numericas EE, no prueba la FPU."""
    div = ee.fpu_h('div', bits(60.0), paso)
    dur = ee.fpu_h('mul', bits(2.0), div)
    inc = ee.fpu_h('div', bits(0.1), dur)
    valor = ee.fpu_h('add', energia, inc)
    volver = ee.fv(valor) > 1 or not mantenido & 0x8000
    eventos = []
    if volver:
        valor = min(valor, bits(1.0))
        eventos = [{'tipo': 'freeze', 'a0': 0}, {'tipo': 'efecto', 'a0': 1, 'a1': 0}]
        if sonido != ee.M32:
            eventos.append({'tipo': 'parar_sonido', 'handle': sonido})
    puentes = {'hud': 1, 'tiempo': 1, 'camara': 1}
    if volver:
        puentes.update(freeze=1, efecto=1)
        if sonido != ee.M32:
            puentes['parar_sonido'] = 1
    return {'energia_bits': valor, 'volver_mira': volver,
            'sonido': ee.M32 if volver else sonido, 'eventos': eventos, 'puentes': puentes}


def comprobar(region, elf):
    s = CargaCell(region, elf)
    filas, primero = [], None
    def revisar(nombre, quiere, sale):
        nonlocal primero
        ok = quiere == sale
        filas.append({'caso': nombre, 'igual': ok, 'esperado': quiere, 'obtenido': sale})
        if not ok and primero is None:
            primero = filas[-1]
    for energia in (0.001, 0.1, 0.5, 0.9999, 1.0):
        for paso in (0.5, 1.0, 1.2, 2.0):
            for mando in (0, 0x8000, 0x8020):
                for sonido in (ee.M32, 7):
                    for altura in (0.0, 399.0):
                        s.preparar(bits(energia), bits(altura), sonido)
                        revisar(f'aislado:{energia}:{paso}:{mando}:{sonido}:{altura}',
                                esperado(bits(energia), mando, bits(paso), sonido),
                                s.pasar(mando, bits(paso)))
    continuos = []
    for paso, corte in ((1.0, None), (1.2, None), (1.0, 37)):
        s.preparar(bits(0.2), sonido=7)
        energia, n, first = bits(0.2), 0, None
        while n < 2000:
            mando = 0 if n == corte else 0x8000
            want = esperado(energia, mando, bits(paso), 7)
            got = s.pasar(mando, bits(paso))
            revisar(f'continuo:{paso}:{corte}:{n}', want, got)
            if want != got and first is None:
                first = n
            # Estado propio del modelo; no absorber diferencias del original.
            energia = want['energia_bits']
            n += 1
            if got['volver_mira']:
                break
        revisar(f'continuo:{paso}:{corte}:termina', True, got['volver_mira'])
        continuos.append({'paso_bits': bits(paso), 'soltar_en': corte, 'ticks': n,
                          'primera_diferencia_tick': first, 'fin': got})
    for nombre, fn in (
            ('energia cero', lambda: s.preparar(0)),
            ('energia subnormal', lambda: s.preparar(1)),
            ('energia infinita', lambda: s.preparar(0x7F800000)),
            ('altura final', lambda: s.preparar(bits(.5), bits(400.0))),
            ('altura NaN', lambda: s.preparar(bits(.5), 0x7FC00000)),
            ('handle negativo', lambda: s.preparar(bits(.5), sonido=-1))):
        try:
            fn()
            reject = False
        except ValueError:
            reject = True
        revisar(nombre, True, reject)
    for nombre, mando, paso in [('paso cero', 0, 0), ('paso subnormal', 0, 1),
                                ('paso NaN', 0, 0x7FC00000), ('mando fuera', 0x10000, bits(1.0))]:
        s.preparar(bits(.5))
        try:
            s.pasar(mando, paso)
            reject = False
        except ValueError:
            reject = True
        revisar(nombre, True, reject)
    # El C++ usa otra zona de scratch, init en cada llamada y sonido en el port.
    # Esta secuencia contrasta ese marshalling con el contexto EE persistente.
    modelo = CargaCell(region, elf, scratch=0x1200000)
    md = modelo.d
    dirs = [md[k] for k in ('ctxp', 'mgrp', 'paso', 'mando', 'escala')]
    sentinelas = [0x11111111 + i for i in range(5)]
    for a, v in zip(dirs, sentinelas):
        modelo.cpu.w32(a, v)
    modelo.cpu.w32(0x11D0000, 0x12345678)  # antiguo scratch estaba dentro del DMD
    s.preparar(bits(.2))
    energia = bits(.2)
    for n in range(41):
        mando = 0 if n == 40 else 0x8000
        referencia = esperado(energia, mando, bits(1.0), ee.M32)
        want = s.pasar(mando)
        got = modelo.adaptar(energia, bits(100.0), mando)
        revisar(f'referencia_persistente_sintetica:{n}', referencia, want)
        revisar(f'adaptador_sintetico:{n}', want, got)
        revisar(f'globales_restaurados:{n}', sentinelas, [modelo.cpu.r32(a) for a in dirs])
        if got is None:
            break
        energia = referencia['energia_bits']
    revisar('DMD fuera del scratch nuevo', 0x12345678, modelo.cpu.r32(0x11D0000))
    # Inyectar un fallo del servicio al salir: no devolver exito ni perder globals.
    addr_freeze = next(a for a, role in modelo.puentes.items() if role == 'freeze')
    def falla(c):
        raise RuntimeError('fallo sintetico de servicio, intencionado')
    modelo.cpu.hooks[addr_freeze] = falla
    revisar('servicio fallido no entrega salida', None, modelo.adaptar(bits(.5), bits(100.0), 0))
    revisar('globales restaurados tras fallo', sentinelas, [modelo.cpu.r32(a) for a in dirs])
    ok = sum(f['igual'] for f in filas)
    print(f'Charge Cell {region}: {ok}/{len(filas)}, primer DIF {primero}')
    for r in continuos:
        print(f'  continuo paso={ee.fv(r["paso_bits"])} soltar={r["soltar_en"]}: '
              f'{r["ticks"]} ticks hasta Aim, primer DIF {r["primera_diferencia_tick"]}')
    return {'region': region, 'elf_sha256': s.digest, 'perfil': {k: v for k, v in s.d.items() if k != 'elf'},
            'iguales': ok, 'total': len(filas), 'primera_diferencia': primero,
            'continuos': continuos, 'casos': filas, 'correcto': ok == len(filas)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    args = p.parse_args()
    start = time.monotonic()
    try:
        if args.salida.exists() or not args.salida.parent.is_dir():
            raise ValueError('salida debe ser nueva y su padre debe existir')
        rows = [comprobar(region, elf) for region, elf in [('eu', args.elf_eu), ('us', args.elf_us)]]
        report = {'operacion': 'carga_cell', 'alcance': 'sintetico; energia/salida de Charge con puentes controlados',
                  'referencia_continua': 'estado esperado propio, sin realimentar salidas observadas',
                  'FPU_HW': sorted(ee.FPU_HW), 'FPU_CERCANO': ee.FPU_CERCANO,
                  'pares_dorado_real': 0, 'limites': ['HUD y caducidad son servicios simulados, no ejecutados',
                  'solo vivo (energia > 0, altura < 400, tiempo positivo)',
                  'sin aim/fuego/impacto/resultado, ni validacion visual/sonora',
                  'esperado reutiliza reglas EE numericas; no es prueba independiente de FPU'],
                  'regiones': rows, 'segundos_banco': round(time.monotonic() - start, 3)}
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in rows) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

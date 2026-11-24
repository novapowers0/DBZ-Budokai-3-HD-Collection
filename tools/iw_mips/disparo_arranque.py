# SPDX-License-Identifier: MIT
"""F1: arranque de seguimiento Cell para angulos cero o muy pequenos.

Contador firmado<=0. Seno/coseno y entrada pose1 son originales; el modelo
se limita a la rama nativa diminuta (sin reduccion/polinomios). Huesos y
sonido solo registran peticiones sin efectos, no animacion/audio reales.
"""
import argparse
from collections import Counter
import itertools
import json
from pathlib import Path
import struct
import time

import disparo_banco as banco
import disparo_marcas as marcas
import disparo_perseguidor as perseguidor
import disparo_seguimiento as seguimiento
import ee

ACTOR, TAM_ACTOR = seguimiento.ACTOR, seguimiento.TAM_ACTOR
ANGULOS = (0, 0x80000000, 0x30800000, 0xB0800000, 0x31FFFFFF, 0xB1FFFFFF)
PERFILES = {
    'eu': {'pose1': 0x31D350, 'sonido': 0x240740, 'frente': 0x480A60, 'uno_cos': 0x489184},
    'us': {'pose1': 0x31D060, 'sonido': 0x2405B0, 'frente': 0x4802B0, 'uno_cos': 0x488984}}
TRIG = (('cos', 0x117438, 0x1174E8), ('sin', 0x117688, 0x117740),
        ('cos_nucleo', 0x115F50, 0x1160A0), ('sin_nucleo', 0x1168C0, 0x1169B8))


def modelo(actor, bloque, paso, base):
    actor, bloque, eventos = actor.copy(), bloque.copy(), []
    angulo = bloque[seguimiento.ANGULO]
    if angulo not in ANGULOS:
        raise ValueError('angulo fuera del dominio diminuto')
    # En la rama diminuta original: sin(x)=x y cos(x)=1. Luego se conserva
    # la multiplicacion COP1 por el Z frontal y el orden de sus operandos.
    v = [perseguidor.producto(marcas.bits(1), angulo, cop1=True), 0, marcas.bits(1)]
    v = perseguidor.escalar(v, marcas.bits(1))
    rapidez = perseguidor.producto(marcas.bits(1.5), paso, cop1=True)
    pos = list(struct.unpack_from('<4I', actor, 0x40))
    pos[:3] = [perseguidor.suma(p, perseguidor.producto(x, rapidez)) for p, x in zip(pos[:3], v)]
    struct.pack_into('<4I', actor, 0x40, *pos)
    entra = seguimiento.u16(actor, 0xCBC) == 1 and struct.unpack_from('<I', actor, 0xC74)[0] != 0
    if entra:
        seguimiento.poner16(actor, 0xCC6, 0)
        actor[0x206F] = 0
        seguimiento.poner16(actor, 0xCBE, 1)
        contador = (seguimiento.u16(actor, 0xCC0) + 1) & 0xFFFF
        seguimiento.poner16(actor, 0xCC0, contador)
        seguimiento.poner32(actor, 0xB60, 0x655 if contador else 0x654)
        if not contador and bloque[0x40 // 4] == ee.M32:
            eventos.append({'tipo': 'iniciar_sonido', 'args': [1, 45, 0, base + 0x40],
                            'fargs_bits': [0, marcas.bits(1)],
                            'actor_sha256': seguimiento.firma(actor)})
        eventos.append({'tipo': 'huesos', 'args': [ACTOR], 'actor_sha256': seguimiento.firma(actor)})
        seguimiento.poner32(actor, 0xCB4, 0)
        seguimiento.poner32(actor, 0xCB8, 0x3E800000)
        seguimiento.poner32(actor, 0xCC8, 0)
    return actor, bloque, eventos, entra


class ArranqueCell(seguimiento.SeguimientoCell):
    def __init__(self, region, elf):
        super().__init__(region, elf)
        self.a = d = PERFILES[region]
        c = self.cpu
        if c.r32(self.s['tabla'] + 4) != d['pose1'] or c.r32(d['uno_cos']) != marcas.bits(1):
            raise ValueError('tabla pose1 o constante cos distintas')
        if [c.r32(d['frente'] + i * 4) for i in range(4)] != [0, 0, marcas.bits(1), 0]:
            raise ValueError('frente de arranque distinto')
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', c.m[d['pose1']:d['pose1'] + 0xC4])
                 if w >> 26 == 3]
        if calls != [d['sonido'], self.s['huesos']]:
            raise ValueError('llamadas de entrada pose1 distintas')
        intervalos = (*TRIG, ('pose1', d['pose1'], d['pose1'] + 0xC4))
        c.whitelist.update(a for _, a, _ in intervalos)
        self.identidad += [{'nombre': n, 'inicio': a, 'fin_exclusivo': b,
                            'sha256': seguimiento.firma(c.m[a:b])} for n, a, b in intervalos]
        c.hooks[d['sonido']] = self._iniciar_sonido
        self.puentes = set(c.hooks)

    def _iniciar_sonido(self, c):
        args, fargs = [c.r[i] & ee.M32 for i in range(4, 8)], c.f[12:14]
        if args != [1, 45, 0, self.base + 0x40] or fargs != [0, marcas.bits(1)]:
            raise RuntimeError('peticion de sonido fuera del contrato')
        self.eventos.append({'tipo': 'iniciar_sonido', 'args': args, 'fargs_bits': fargs,
                             'actor_sha256': seguimiento.firma(c.m[ACTOR:ACTOR + TAM_ACTOR])})
        c.r[2] = 0

    def preparar_arranque(self, pos, estado, contador, bandera, handle, angulo):
        if angulo not in ANGULOS or contador not in (0, 0x8000, 0xFFFF):
            raise ValueError('contador o angulo fuera del dominio')
        b = perseguidor.contexto()
        b[seguimiento.ANGULO], b[0x40 // 4] = angulo, handle
        actor = self.preparar_consumidor(b, pos, estado, contador)
        seguimiento.poner32(actor, 0xC74, bandera)
        self.cpu.m[ACTOR:ACTOR + TAM_ACTOR] = actor
        self.inmutables += [(a, bytes(self.cpu.m[a:a + tam]))
                            for a, tam in ((self.a['frente'], 16), (self.a['uno_cos'], 4))]
        return actor, b

    def observar_arranque(self):
        got = self.observar_consumidor()
        got['pose1_llamadas'] = self.cpu.llamadas[self.a['pose1']]
        got['trig_llamadas'] = [self.cpu.llamadas[a] for _, a, _ in TRIG]
        return got

    def actualizar_arranque(self, paso):
        c = self.cpu
        if not self.preparado or paso not in marcas.PASOS:
            raise ValueError('preparacion y paso requeridos')
        if struct.unpack_from('<h', c.m, ACTOR + 0xCC0)[0] > 0 or c.r32(self.base + 0x34) not in ANGULOS:
            raise ValueError('contador positivo o angulo no cubierto')
        self.eventos.clear()
        c.solicitudes.clear(); c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.w32(self.marcas.paso, paso)
        c.call(self.s['fn'], ACTOR, limite=100000)
        if c.io or c.sin_puente or set(c.hooks) != self.puentes:
            raise RuntimeError('servicio inesperado en arranque')
        return self.observar_arranque()


def esperado(actor, b, eventos, entra, paso):
    return {'bloque': b, 'posicion': list(struct.unpack_from('<4I', actor, 0x40)),
            'getter_llamadas': 0, 'semilla': 0xC0FFEE33, 'paso_bits': paso,
            'memory_llamadas': [], 'entradas_intactas': True,
            'guardias': [['a5' * 16] * 2 for _ in range(3)],
            'preservados': [list(v) for v in perseguidor.PRESERVADOS] + [[marcas.bits(i) for i in range(20, 24)]],
            'actor_sha256': seguimiento.firma(actor), 'eventos': eventos,
            'perseguidor_llamadas': 0, 'pose2_llamadas': 0, 'pose1_llamadas': int(entra),
            'trig_llamadas': [1, 1, 1, 1]}


def comprobar(region, elf):
    s, filas, ramas, continuos = ArranqueCell(region, elf), [], Counter(), []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    poses = ((0, 0, 0), (32, 4, -16), (-32, -4, 16))
    for contador, estado, bandera, handle, angulo, pos, paso in itertools.product(
            (0, 0x8000, 0xFFFF), (0, 1, 2, 19), (0, 1, ee.M32),
            (7, ee.M32), ANGULOS, poses, marcas.PASOS):
        actor, b = s.preparar_arranque(pos, estado, contador, bandera, handle, angulo)
        actor, b, eventos, entra = modelo(actor, b, paso, s.base)
        got = s.actualizar_arranque(paso)
        nombre = f'aislado:{contador}:{estado}:{bandera}:{handle}:{angulo:08x}:{pos}:{paso:08x}'
        revisar(nombre, esperado(actor, b, eventos, entra, paso), got)
        ramas['pose1' if entra else 'sin_pose1'] += 1
        ramas['sonido' if any(e['tipo'] == 'iniciar_sonido' for e in eventos) else 'sin_sonido'] += 1
    aislados = len(filas)
    for angulo, paso in itertools.product(ANGULOS, marcas.PASOS):
        actor, b = s.preparar_arranque((0, 0, 0), 1, 0, 0, ee.M32, angulo)
        primera = None
        for tick in range(16):
            # Fin de animacion es una entrada externa, no una salida reinyectada.
            bandera = int(tick == 15)
            seguimiento.poner32(actor, 0xC74, bandera)
            s.cpu.w32(ACTOR + 0xC74, bandera)
            actor, b, eventos, entra = modelo(actor, b, paso, s.base)
            want, got = esperado(actor, b, eventos, entra, paso), s.actualizar_arranque(paso)
            revisar(f'continuo:{angulo:08x}:{paso:08x}:{tick}', want, got)
            if want != got and primera is None:
                primera = tick
        revisar(f'continuo_fin:{angulo:08x}:{paso:08x}', 1, seguimiento.u16(s.cpu.m, ACTOR + 0xCC0))
        continuos.append({'angulo_bits': angulo, 'paso_bits': paso, 'cuadros': 16,
                          'primera_diferencia': primera})
    for caso, contador, angulo in (('contador', 1, 0), ('angulo', 0, marcas.bits(1))):
        s.preparar_arranque((0, 0, 0), 1, 0, 0, 7, 0)
        s.cpu.w32(s.base + 0x34, angulo)
        struct.pack_into('<H', s.cpu.m, ACTOR + 0xCC0, contador)
        antes = s.observar_arranque()
        try:
            s.actualizar_arranque(marcas.PASOS[1])
            rechazo = False
        except ValueError:
            rechazo = True
        revisar('rechazo:' + caso, [True, antes], [rechazo, s.observar_arranque()])
    for r in s.identidad:
        revisar('codigo_intacto:' + r['nombre'], r['sha256'], seguimiento.firma(s.cpu.m[r['inicio']:r['fin_exclusivo']]))
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Arranque Cell {region}: {iguales}/{len(filas)}, primer DIF {primera["caso"] if primera else None}')
    return {'region': region, 'elf_sha256': s.marcas.digest, 'perfil': s.a,
            'cuerpos': s.identidad, 'iguales': iguales, 'total': len(filas), 'primera_diferencia': primera,
            'correcto': primera is None, 'ramas_aisladas': dict(ramas), 'continuos': continuos,
            'cobertura': {'aislados': aislados, 'continuos': 24, 'cuadros_continuos': 384,
                          'resincronizaciones': 0, 'rechazos': 2, 'angulos_bits': list(ANGULOS)},
            'perfil_fpu_hw': sorted(ee.FPU_HW), 'perfil_vu_cercano': sorted(ee.VU_CERCANO), 'casos': filas}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a, inicio = p.parse_args(), time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump({'operacion': 'arranque_seguimiento_cell_diminuto',
                       'sha256_banco': banco.sha256(Path(__file__)),
                       'sha256_dependencias': {Path(m.__file__).name: banco.sha256(Path(m.__file__))
                                               for m in (banco, ee, marcas, perseguidor, seguimiento,
                                                         perseguidor.regeneracion, perseguidor.disparo_ee,
                                                         perseguidor.geometria, perseguidor.mundo)},
                       'limites': ['solo cero/signo cero y +/-2^-30, +/-bits31FFFFFF',
                                   'sin reduccion angular ni polinomios; no orientacion general',
                                   'huesos/iniciar/detener sonido son peticiones sin efectos',
                                   'bandera fin animacion externa; actor/tabla/velocidad sinteticos',
                                   'perfil EE existente, no hardware nuevo ni dorado',
                                   'sin C++ nuevo/juego/emulador/build'],
                       'integracion_cpp_nueva': False, 'pares_dorado': 0, 'regiones': regiones,
                       'segundos_banco': round(time.monotonic() - inicio, 3)},
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

# SPDX-License-Identifier: MIT
"""F1: Fire2 Cell con helpers matematicos originales y mundo controlado.

Comprueba energia/fase/reloj/contratos de peticiones, no colision ni imagen.
La geometria emitida se conserva como observacion; no es un dorado real.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time

import disparo_fire as fire
import disparo_fases as carga
import disparo_ee
import ee

bits = carga.bits
# Extensiones de cuerpos delimitadas por la siguiente entrada analizada EU.
# Se identifican por separado en cada ELF; mismo selector no prueba igualdad.
MATH = ((0x119A70, 0x119B28), (0x119B28, 0x119B80),
        (0x119B80, 0x119BB0), (0x119BF0, 0x119CC8),
        (0x119CC8, 0x119D58), (0x119D58, 0x119F80),
        (0x119F80, 0x119FD8), (0x119FD8, 0x11A040),
        (0x11A040, 0x11A098), (0x11A098, 0x11A290),
        (0x11A290, 0x11A2E8), (0x11A2E8, 0x11A3E0),
        (0x11A3E0, 0x11A4E0), (0x11A4E0, 0x11A530),
        (0x11A680, 0x11A6B0), (0x113A48, 0x1142C8),
        (0x1142C8, 0x1145D8), (0x119BB0, 0x119BF0),
        (0x11A6B0, 0x11A708), (0x1177B0, 0x1177C8))


class GeometriaCell(fire.FireEntradaCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        self.region = region
        c = self.cpu
        disparo_ee.instalar(c)
        for role in ('normalizar', 'dot', 'extender', 'acos', 'truncar', 'norma', 'sin', 'escalar', 'target'):
            addr = self.por_rol[role]
            c.hooks.pop(addr, None)
            c.whitelist.add(addr)
        # Rutinas soft-float de conversion/aritmetica y cuerpo acos, leidas
        # en el decompilado. Son ejecucion nativa, no retornos Python.
        c.whitelist.update(a for a, _ in MATH)
        self.helpers = [{'entrada': a, 'fin_exclusivo_eu': b,
                         'sha256_rango': hashlib.sha256(c.m[a:b]).hexdigest()}
                        for a, b in MATH]
        c.hooks[self.por_rol['efecto']] = self._geometria_servicio('efecto')
        for role in ('landing', 'fatiga'):
            if role in self.por_rol:
                c.hooks[self.por_rol[role]] = self._geometria_servicio(role)

    def _geometria_servicio(self, role):
        def hook(c):
            if role == 'efecto':
                ident, tabla = c.r[4] & ee.M32, c.r[5] & ee.M32
                if ident == 2:
                    return self._entrada_servicio('efecto')(c)
                if ident not in (3, 5) or not ee.PILA <= tabla <= ee.PILA + 0x100000 - 8:
                    raise RuntimeError('solo efecto3/5 con tabla de pila propia')
                a, b = c.r32(tabla), c.r32(tabla + 4)
                if ident == 3 and a != fire.CTRL + 0x80:
                    raise RuntimeError('efecto3 no referencia controlador propio')
                if ident == 5 and not ee.PILA <= a <= ee.PILA + 0x100000 - 16:
                    raise RuntimeError('efecto5 con vector ajeno a la pila')
                if not ee.PILA <= b <= ee.PILA + 0x100000 - 16:
                    raise RuntimeError('efecto con segundo vector fuera de pila')
                self.eventos.append({'tipo': role, 'id': ident,
                    'pos_bits': [c.r32(a + 4 * i) for i in range(4)],
                    'otros_bits': [c.r32(b + 4 * i) for i in range(4)]})
            else:
                if c.r[4] & ee.M32 != self.mgr + 0x28D0:
                    raise RuntimeError('servicio personaje con objeto ajeno')
                args = [0x28D0] + ([c.r[5] & ee.M32] if role == 'fatiga' else [])
                self.eventos.append({'tipo': role, 'args': args})
            c.r[2] = 0
        return hook

    def preparar_geometria(self, energia, x=0, y=400, z=200):
        self.preparar_entrada(energia, x, y, z)
        # Caso aislado de fase2; no se atribuye a una captura/entrada real.
        self.cpu.w32(self.task + 0x34, 2)
        self.cpu.w32(fire.CTRL + 0x24, 0x80)

    def ejecutar_geometria(self):
        c, d = self.cpu, self.d
        if not self.preparado or c.r32(self.task + 0x1C) != d['fire'] or c.r32(self.task + 0x34) != 2:
            raise ValueError('solo fase Fire2 preparada')
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.call(d['fire'], self.task, limite=100000)
        if c.io or c.sin_puente:
            raise RuntimeError(f'Fire2 con E/S o llamada sin cubrir: {dict(c.io)}, {dict(c.sin_puente)}')
        return {'energia_bits': c.r32(self.ctx + 4), 'reloj_bits': c.r32(self.task + 0x30),
                'fase': c.r32(self.task + 0x34), 'luces_bits': c.r32(self.task + 0x38),
                'estado': c.r32(self.task + 0x1C), 'target': [c.r32(fire.CTRL + p) for p in (0x24, 0x68, 0x6C, 0x70)],
                'eventos': self.eventos.copy(),
                'llamadas': dict(Counter(self.roles.get(a, f'nativo:{a:08x}') for a in c.llamadas.elements()))}


def comprobar(region, elf):
    s, filas, observaciones, continuos = GeometriaCell(region, elf), [], [], []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    energias = (bits(.05), bits(.1) - 1, bits(.1), bits(.1) + 1, bits(.5), bits(1))
    def revisar_geometria(pref, energia, got, flags):
        nuevo = ee.fpu_h('sub', energia, bits(.1))
        revisar(pref + ':energia', nuevo, got['energia_bits'])
        revisar(pref + ':tarea', [bits(32), 3, bits(0), s.d['fire']],
                [got[k] for k in ('reloj_bits', 'fase', 'luces_bits', 'estado')])
        fatiga = region == 'eu' and ee.fv(nuevo) < 0
        revisar(pref + ':efectos', [3, 5], [e['id'] for e in got['eventos'] if e['tipo'] == 'efecto'])
        revisar(pref + ':actuacion', [{'tipo': 'actuar', 'args': [128, 16, 40]}],
                [e for e in got['eventos'] if e['tipo'] == 'actuar'])
        revisar(pref + ':personaje', [{'tipo': 'landing', 'args': [0x28D0]}] +
                ([{'tipo': 'fatiga', 'args': [0x28D0, 1]}] if fatiga else []),
                [e for e in got['eventos'] if e['tipo'] in ('landing', 'fatiga')])
        revisar(pref + ':camara_flags', [flags | 2, 1], got['target'][:2])
        return nuevo
    for energia in energias:
        for x, y, z in ((0, 400, 200), (-111, 200, 60), (74, 800, 280)):
            s.preparar_geometria(energia, x, y, z)
            got = s.ejecutar_geometria()
            observaciones.append({'energia_in_bits': energia, 'camara': [x, y, z], 'salida': got})
            pref = f'{energia:x}:{x}:{y}:{z}'
            revisar_geometria(pref, energia, got, 0x80)
    # Memoria persistente: no preparar/reinyectar estado entre fases.
    # Fire comprueba energia<=0 ANTES de la fase3: tras agotar entra GameOver.
    # El continuo hasta Aim se limita a energia posterior positiva; los casos
    # agotados de arriba prueban solo la llamada aislada Fire2.
    for energia in energias[3:]:
        for paso, duracion in ((1, 33), (.5, 65), (1.2, 27), (2, 17)):
            pref = f'continuo:{energia:x}:{paso}'
            s.preparar_entrada(energia)
            revisar(pref + ':entrada', fire.esperado(energia, 400), s.entrar())
            reloj, luces, energia_actual = bits(32), bits(7), energia
            n_fases = []
            for fase in (1, 3):
                for n in range(100):
                    want = fire.esperas.esperado(fase, reloj, luces, bits(paso))
                    want['energia_bits'] = energia_actual
                    got = s.pasar(bits(paso))
                    revisar(pref + f':fase{fase}:{n}', want, got)
                    # Evolucion del esperado, no resincronizar con obtenido.
                    reloj, luces = want['reloj_bits'], want['luces_bits']
                    if want['mira'] or (fase == 1 and want['fase'] == 2):
                        break
                revisar(pref + f':duracion{fase}', duracion, n + 1)
                n_fases.append(n + 1)
                if fase == 1:
                    got = s.ejecutar_geometria()
                    energia_actual = revisar_geometria(pref + ':fire2', energia, got, 0)
                    observaciones.append({'continuo': pref, 'salida': got})
                    reloj, luces = bits(32), bits(0)
            revisar(pref + ':retorno_Aim', s.d['mira'], s.cpu.r32(s.task + 0x1C))
            continuos.append({'energia_in_bits': energia, 'paso_bits': bits(paso),
                              'ticks_esperas': n_fases, 'energia_out_bits': energia_actual})
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Fire2 Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return {'region': region, 'elf_sha256': s.digest, 'cuerpo_sha256': s.cuerpo_sha256,
            'iguales': iguales, 'total': len(filas), 'primera_diferencia': primera,
            'correcto': iguales == len(filas), 'casos': filas, 'observaciones_geometria': observaciones,
            'continuos': continuos, 'helpers_rangos': s.helpers}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=carga.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=carga.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    args = p.parse_args()
    start = time.monotonic()
    try:
        if args.salida.exists() or not args.salida.parent.is_dir():
            raise ValueError('salida debe ser nueva y su padre debe existir')
        regiones = [comprobar(r, e) for r, e in [('eu', args.elf_eu), ('us', args.elf_us)]]
        report = {'operacion': 'fire2_cell', 'alcance': 'Fire2 y continuo Fire0->1->2->3->Aim; geometria solo observada',
                  'pares_dorado_real': 0, 'integrado_cpp': False,
                  'FPU_HW': sorted(ee.FPU_HW), 'FPU_CERCANO': ee.FPU_CERCANO,
                  'limites': ['mundo/efectos/HUD/tiempo/voz/landing/fatiga son servicios simulados',
                              'vectores del proyectil observados, no validados contra referencia independiente',
                               'sin colision, impacto, cierre, imagen, C++ ni juego',
                               'continuo hasta Aim solo con energia posterior positiva; agotamiento entra GameOver fuera del banco',
                              'esperado usa aritmetica EE, no prueba independiente de FPU/VU'],
                  'extension_local': 'PMFHL.LW; contrato sintetico, sin validacion hardware',
                  'regiones': regiones, 'segundos_banco': round(time.monotonic() - start, 3)}
        with args.salida.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

# SPDX-License-Identifier: MIT
"""F1: rama de seguimiento de Char_PoseUpdateSteer y entrada de pose2.

Ejecuta el perseguidor y el destino indirecto originales. Solo BonesMovement
y detener sonido son peticiones controladas sin efectos. La rama de arranque
con contador<=0 y su trigonometria quedan fuera de este banco.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_banco as banco
import disparo_marcas as marcas
import disparo_perseguidor as perseguidor
import disparo_regeneracion as regeneracion
import ee

ACTOR, TAM_ACTOR = perseguidor.ACTOR, perseguidor.TAM_ACTOR
PERFILES = {
    'eu': {'fn': 0x31D1E0, 'fin': 0x31D350, 'pose2': 0x31D140,
           'tabla': 0x4996B0, 'huesos': 0x238670},
    'us': {'fn': 0x31CEF0, 'fin': 0x31D060, 'pose2': 0x31CE50,
           'tabla': 0x498EB0, 'huesos': 0x2384E0}}
SONIDO = 0x13F7A0
ANGULO = 0x34 // 4


def u16(b, off):
    return struct.unpack_from('<H', b, off)[0]


def poner16(b, off, v):
    struct.pack_into('<H', b, off, v & 0xFFFF)


def poner32(b, off, v):
    struct.pack_into('<I', b, off, v & ee.M32)


def firma(b):
    return hashlib.sha256(b).hexdigest()


def modelo_pose2(actor, bloque, base):
    actor, bloque, eventos = actor.copy(), bloque.copy(), []
    previo, contador = u16(actor, 0xCBC), u16(actor, 0xCC0)
    poner16(actor, 0xCBE, previo)
    poner16(actor, 0xCBC, 2)
    poner16(actor, 0xCC0, contador + 1 if previo == 2 else 0)
    poner32(actor, 0xB60, 0x656)
    eventos.append({'tipo': 'huesos', 'args': [ACTOR], 'actor_sha256': firma(actor)})
    poner32(actor, 0xCB4, 0)
    poner32(actor, 0xCB8, 0x3E800000)
    poner32(actor, 0xCC8, 0)
    handle = bloque[0x40 // 4]
    if handle != ee.M32:
        eventos.append({'tipo': 'sonido', 'args': [base + 0x40], 'handle': handle,
                        'actor_sha256': firma(actor)})
        bloque[0x40 // 4] = ee.M32
    return actor, bloque, eventos


class SeguimientoCell(perseguidor.PerseguidorCell):
    def __init__(self, region, elf):
        super().__init__(region, elf)
        self.s = p = PERFILES[region]
        c = self.cpu
        if c.r32(p['tabla'] + 8) != p['pose2']:
            raise ValueError('destino de pose2 no coincide con ELF')
        for inicio, fin, calls in (
                (p['pose2'], p['fn'], [p['huesos'], SONIDO]),
                (p['fn'], p['fin'], [self.d['fn'], 0x117688, 0x117438, 0x142540, 0x1424E0])):
            observadas = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', c.m[inicio:fin])
                          if w >> 26 == 3]
            if observadas != calls:
                raise ValueError('perfil de llamadas del consumidor distinto')
        c.whitelist.update((p['fn'], p['pose2']))
        self.identidad += [{'nombre': n, 'inicio': a, 'fin_exclusivo': b,
                            'sha256': firma(c.m[a:b])}
                           for n, a, b in (('seguimiento', p['fn'], p['fin']),
                                           ('pose2', p['pose2'], p['fn']))]
        self.eventos = []
        for rol, fn in (('huesos', p['huesos']), ('sonido', SONIDO)):
            c.hooks[fn] = self._servicio(rol)
        self.puentes = set(c.hooks)

    def _servicio(self, rol):
        def hook(c):
            arg = c.r[4] & ee.M32
            if arg != (ACTOR if rol == 'huesos' else self.base + 0x40):
                raise RuntimeError('argumento de servicio fuera del contrato')
            e = {'tipo': rol, 'args': [arg],
                 'actor_sha256': firma(c.m[ACTOR:ACTOR + TAM_ACTOR])}
            if rol == 'sonido':
                e['handle'] = c.r32(arg)
            self.eventos.append(e)
            c.r[2] = 0
        return hook

    def preparar_consumidor(self, bloque, pos, estado, contador):
        if type(estado) is not int or not 0 <= estado <= 0xFFFF or type(contador) is not int or not 0 <= contador <= 0xFFFF:
            raise ValueError('estado/contador deben ser u16')
        super().preparar(bloque, pos, 1)
        actor = regeneracion.actor_esperado(pos)
        poner16(actor, 0xCBC, estado)
        poner16(actor, 0xCC0, contador)
        self.cpu.m[ACTOR:ACTOR + TAM_ACTOR] = actor
        self.eventos.clear()
        return actor

    def observar_consumidor(self, angular=False):
        got = super().observar()
        for nombre in ('direccion', 'retorno', 'actor_resto_intacto'):
            del got[nombre]
        if angular:
            got['bloque'][ANGULO] = None
        got['actor_sha256'] = firma(self.cpu.m[ACTOR:ACTOR + TAM_ACTOR])
        got['eventos'] = self.eventos.copy()
        got['perseguidor_llamadas'] = self.cpu.llamadas[self.d['fn']]
        got['pose2_llamadas'] = self.cpu.llamadas[self.s['pose2']]
        return got

    def llamar_consumidor(self, paso, directo=False, angular=False):
        c = self.cpu
        if not self.preparado or paso not in marcas.PASOS:
            raise ValueError('preparacion y paso del dominio requeridos')
        contador = struct.unpack_from('<h', c.m, ACTOR + 0xCC0)[0]
        if not directo and contador <= 0:
            raise ValueError('rama de arranque no cubierta; contador debe ser positivo')
        self.eventos.clear()
        c.solicitudes.clear(); c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.w32(self.marcas.paso, paso)
        c.call(self.s['pose2'] if directo else self.s['fn'], ACTOR, limite=100000)
        if c.io or c.sin_puente or set(c.hooks) != self.puentes:
            raise RuntimeError('servicio no autorizado en seguimiento Cell')
        return self.observar_consumidor(angular), c.r32(self.base + 0x34)


def comprobar(region, elf):
    s, filas, continuos, angulos = SeguimientoCell(region, elf), [], [], []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
    def quiere(b, actor, paso, consultas, eventos, directo, termina):
        return {'bloque': b, 'posicion': list(struct.unpack_from('<4I', actor, 0x40)),
                'getter_llamadas': consultas, 'semilla': 0xC0FFEE33, 'paso_bits': paso,
                'memory_llamadas': [], 'entradas_intactas': True,
                'guardias': [['a5' * 16] * 2 for _ in range(3)],
                'preservados': [list(v) for v in perseguidor.PRESERVADOS] + [[marcas.bits(i) for i in range(20, 24)]],
                'actor_sha256': firma(actor), 'eventos': eventos,
                'perseguidor_llamadas': 0 if directo else 1,
                # Ee.call entra en la raiz sin pasar por Ee.entrar:
                # este contador mide llamadas internas, no la entrada directa.
                'pose2_llamadas': 1 if not directo and termina else 0}
    def avanzar(b, actor, paso):
        pos = list(struct.unpack_from('<4I', actor, 0x40))
        b, pos, direccion, ret, n, _ = perseguidor.modelo(b, pos, b[0x30 // 4:0x40 // 4], paso, 1)
        actor = actor.copy()
        struct.pack_into('<4I', actor, 0x40, *pos)
        if ret == 0:
            b[0x30 // 4:0x40 // 4] = direccion
        eventos = []
        if ret:
            poner16(actor, 0xCC6, 0)
            actor[0x206F] = 0
            actor, b, eventos = modelo_pose2(actor, b, s.base)
        return b, actor, ret, n, eventos
    # Entrada de pose2 aislada: cambio de estado, incremento u16 y sonido.
    for estado in (0, 1, 2, 3, 0xFFFF):
        for contador in (0, 1, 0x7FFF, 0xFFFF):
            for handle in (ee.M32, 0, 7):
                b = perseguidor.contexto()
                b[0x40 // 4] = handle
                actor = s.preparar_consumidor(b, (0, 0, 0), estado, contador)
                actor, b, eventos = modelo_pose2(actor, b, s.base)
                got, _ = s.llamar_consumidor(marcas.PASOS[1], directo=True)
                revisar(f'pose2:{estado}:{contador}:{handle}',
                        quiere(b, actor, marcas.PASOS[1], 0, eventos, True, True), got)
    for indice in (-1, 0, 1, 4, 5):
        for tipo in (0, 1, 2, 8, 9, 10, ee.M32):
            for estado, contador in ((1, 1), (2, 1), (2, 0x7FFF)):
                for handle in (ee.M32, 7):
                    for paso in marcas.PASOS:
                        b = perseguidor.contexto(tipo, indice=indice)
                        b[0x40 // 4] = handle
                        actor = s.preparar_consumidor(b, (0, 0, 0), estado, contador)
                        b, actor, ret, n, eventos = avanzar(b, actor, paso)
                        angular = b[ANGULO] is None
                        got, angulo = s.llamar_consumidor(paso, angular=angular)
                        nombre = f'seguimiento:{indice}:{tipo}:{estado}:{contador}:{handle}:{paso:08x}'
                        revisar(nombre, quiere(b, actor, paso, n, eventos, False, bool(ret)), got)
                        angulos.append({'caso': nombre, 'angulo_bits_no_comparado': angulo})
    for patron in ((1, 2, 9, 3, 0), (8, 7, 6, 5, 0), (4, 10, ee.M32, 1, 0)):
        for paso in marcas.PASOS:
            b = perseguidor.contexto()
            b[0x40 // 4] = 7
            for i, tipo in enumerate(patron):
                off = (0x50 + i * 0x30) // 4
                b[off], b[off + 9] = tipo, marcas.bits(4 + i * 2)
                b[off + 4:off + 8] = perseguidor.vector((0, 0, 64 + i * 16))
            actor = s.preparar_consumidor(b, (0, 0, 0), 1, 1)
            primera, termino = None, False
            for tick in range(512):
                b, actor, ret, n, eventos = avanzar(b, actor, paso)
                want = quiere(b, actor, paso, n, eventos, False, bool(ret))
                got, angulo = s.llamar_consumidor(paso, angular=b[ANGULO] is None)
                revisar(f'continuo:{patron}:{paso:08x}:{tick}', want, got)
                if want != got and primera is None:
                    primera = tick
                if ret:
                    termino = True
                    break
            revisar(f'continuo_fin:{patron}:{paso:08x}', True, termino)
            continuos.append({'patron': list(patron), 'paso_bits': paso, 'cuadros': tick + 1,
                              'fin_pose2': termino, 'primera_diferencia': primera})
    for contador in (0, 0x8000, 0xFFFF):
        s.preparar_consumidor(perseguidor.contexto(), (0, 0, 0), 1, contador)
        antes = s.observar_consumidor()
        try:
            s.llamar_consumidor(marcas.PASOS[1])
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(f'rechazo_arranque:{contador}', [True, antes], [rechazo, s.observar_consumidor()])
    for r in s.identidad:
        revisar('codigo_intacto:' + r['nombre'], r['sha256'], firma(s.cpu.m[r['inicio']:r['fin_exclusivo']]))
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Seguimiento Cell {region}: {iguales}/{len(filas)}, primer DIF {primera["caso"] if primera else None}')
    return {'region': region, 'elf_sha256': s.marcas.digest, 'perfil': s.s,
            'cuerpos': s.identidad, 'puentes': ['BonesMovement', 'detener_sonido'],
            'perfil_fpu_hw': sorted(ee.FPU_HW), 'fpu_cercano': ee.FPU_CERCANO,
            'perfil_vu_cercano': sorted(ee.VU_CERCANO),
            'iguales': iguales, 'total': len(filas), 'primera_diferencia': primera,
            'correcto': primera is None, 'continuos': continuos,
            'cobertura': {'entradas_pose2': 60, 'seguimientos_aislados': len(angulos),
                          'continuos': len(continuos), 'cuadros_continuos': sum(c['cuadros'] for c in continuos),
                          'resincronizaciones': 0, 'rechazos_rama_no_cubierta': 3,
                          'angulos_comparados': 0},
            'observaciones_angulares': angulos, 'casos': filas}


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
            json.dump({'operacion': 'consumidor_seguimiento_cell', 'sha256_banco': banco.sha256(Path(__file__)),
                       'sha256_dependencias': {Path(m.__file__).name: banco.sha256(Path(m.__file__))
                                               for m in (banco, ee, marcas, perseguidor, regeneracion,
                                                         perseguidor.disparo_ee, perseguidor.geometria,
                                                         perseguidor.mundo)},
                       'limites': ['solo rama contador firmado positivo de seguimiento',
                                   'entrada pose2 original; BonesMovement y sonido sin efectos',
                                   'oraculo racional de perfil existente, angulo no comparado',
                                   'velocidades/actor/contexto sinteticos, no dorado ni mundo completo',
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

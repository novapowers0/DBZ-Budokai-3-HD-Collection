# SPDX-License-Identifier: MIT
"""F1: ApplyResult Cell, rango/premio solicitado y bloque de resultados offline.

Getter config, memset, clasificador, ApplyResult y flag4 se ejecutan desde ELF.
Battle_ApplyResultRewards es una frontera observada: no acredita un premio real.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_caducidad as caducidad
import ee

bits = caducidad.bits
GAME = 0x1009000
CONFIG = GAME + 0x11C
ROLES = ('config', 'memset', 'rango', 'premios', 'flag4')
PERFILES = {
    'eu': {'premios': 0x5D9390, 'factor': 0x5D93B4, 'rangos': 0x5D9378},
    'us': {'premios': 0x5D8B90, 'factor': 0x5D8BB4, 'rangos': 0x5D8B78}}


class ResultadoCell(caducidad.CaducidadCell):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        c = self.cpu
        self.z = PERFILES[region]
        body = c.m[self.q['apply']:self.p['wait']]
        self.apply_sha256 = hashlib.sha256(body).hexdigest()
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        if len(calls) != len(ROLES):
            raise ValueError('ApplyResult no contiene los cinco JAL del perfil')
        for addr, role in zip(calls, ROLES):
            if addr in self.roles and self.roles[addr] != role:
                raise ValueError('roles ApplyResult contradictorios')
            self.roles[addr] = role
        self.por_rol = {r: a for a, r in self.roles.items()}
        for role in ('config', 'memset', 'rango', 'flag4'):
            addr = self.por_rol[role]
            c.hooks.pop(addr, None)
            c.whitelist.add(addr)
        c.whitelist.add(self.q['apply'])
        w = c.r32(self.por_rol['config'])
        if w >> 26 != 0x23 or (w >> 21 & 31) != 28:
            raise ValueError('getter config sin global GP')
        imm = w & 0xFFFF
        self.gamep = c.gp + imm - (0x10000 if imm & 0x8000 else 0)
        self.premios_base = [c.r16(self.z['premios'] + 2 * i) for i in range(6)]
        self.factores = [c.r32(self.z['factor'] + 24 * i) for i in range(6)]
        self.tabla_rango = [ee.fv(c.r32(self.z['rangos'] + 4 * i)) for i in range(6)]
        self.helpers_resultado = [
            {'inicio': a, 'fin_exclusivo': b, 'sha256': hashlib.sha256(c.m[a:b]).hexdigest()}
            for a, b in ((self.por_rol['config'], self.por_rol['config'] + 0x10),
                         (self.por_rol['memset'], self.por_rol['memset'] + 0xC0),
                         (self.por_rol['rango'], self.por_rol['rango'] + 0x100),
                         (self.por_rol['flag4'], self.por_rol['flag4'] + 0x20))]
        c.hooks[self.por_rol['premios']] = self._premios
        self.aplicado = False

    def _premios(self, c):
        if c.r32(self.gamep) != GAME:
            raise RuntimeError('premios con game-state ajeno')
        self.eventos.append({'tipo': 'premios_solicitados',
                             'config_hex': bytes(c.m[CONFIG:CONFIG + 0x54]).hex(),
                             'flags_antes': c.r32(GAME + 0x8C)})
        c.r[2] = 0

    def preparar_resultado(self, dificultad, ratio, estado_apply=True):
        if type(dificultad) is not int or not 0 <= dificultad <= 5:
            raise ValueError('dificultad fuera de0..5')
        if not caducidad.derrota.geometria.fire.esperas.ready.finito(ratio):
            raise ValueError('ratio no finito')
        self.preparar_caducidad(dificultad, bits(100))
        c = self.cpu
        c.w32(self.ctx + 0x14, ratio)
        c.w32(self.task + 0x1C, self.q['apply'] if estado_apply else self.d['fire'])
        c.m[GAME:GAME + 0x200] = bytes([0xA5]) * 0x200
        c.w32(self.gamep, GAME)
        c.w32(GAME + 0x8C, 0x80)
        self.aplicado = False

    def aplicar(self):
        c = self.cpu
        if self.aplicado or c.r32(self.task + 0x1C) != self.q['apply']:
            raise ValueError('el banco solo admite una llamada desde ApplyResult por fixture')
        r = self._llamar(self.q['apply'])
        r['config_hex'] = bytes(c.m[CONFIG:CONFIG + 0x54]).hex()
        r['flags_game'] = c.r32(GAME + 0x8C)
        r['guardias'] = [bytes(c.m[CONFIG - 16:CONFIG]).hex(), bytes(c.m[CONFIG + 0x54:CONFIG + 0x64]).hex()]
        self.aplicado = True
        return r


def resultado_esperado(s, dificultad, ratio, base=None, factor=None):
    value = ee.fv(ee.fb(ee.fv(ratio)))
    rango = caducidad.derrota.geometria.carga.banco.clasificar_tabla(value, s.tabla_rango, 0, '7DE')
    if rango == -1:
        premio = s.premios_base[0]
    else:
        b = s.premios_base[rango + 1] if base is None else base
        f = s.factores[dificultad] if factor is None else factor
        entero = int(ee.fv(ee.fpu_h('mul', bits(b), f)))
        premio = ((entero // 100 + int(entero % 100 >= 50)) * 100) & 0xFFFF
    config = bytearray(0x54)
    struct.pack_into('<II', config, 0, 2, rango + 1)
    struct.pack_into('<II', config, 0x48, rango & ee.M32, premio)
    return bytes(config).hex(), rango, premio


def comprobar(region, elf):
    s, filas, continuos = ResultadoCell(region, elf), [], []

    def revisar(nombre, want, got):
        if want != got and not any(not f['igual'] for f in filas):
            print(f'Primer DIF {region}, {nombre}: esperado={want}, obtenido={got}')
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})

    def verificar(pref, dificultad, ratio, got, base=None, factor=None):
        config, rango, premio = resultado_esperado(s, dificultad, ratio, base, factor)
        revisar(pref + ':bloque', config, got['config_hex'])
        revisar(pref + ':peticion', [{'tipo': 'premios_solicitados', 'config_hex': config, 'flags_antes': 0x80}], got['eventos'])
        revisar(pref + ':flag4', 0x84, got['flags_game'])
        revisar(pref + ':guardias', ['a5' * 16, 'a5' * 16], got['guardias'])
        revisar(pref + ':estado', s.q['apply'], got['estado'])
        revisar(pref + ':sin_mutar_ratio', ratio, got['resultado'][1])
        return rango, premio

    for dificultad in range(6):
        for ratio in caducidad.derrota.geometria.carga.banco.muestras(s.tabla_rango):
            rb = bits(ratio)
            s.preparar_resultado(dificultad, rb)
            verificar(f'aislado:{dificultad}:{rb:08x}', dificultad, rb, s.aplicar())

    # Operand fixtures para ambos lados de redondeo a centenas y wrap u16.
    # Restaurar los DOS operandos al terminar cada caso; no cambiar codigo ELF.
    for base in (1, 49, 50, 51, 149, 150, 151, 65535):
        for factor in (bits(1), bits(1.5), bits(2)):
            s.preparar_resultado(0, bits(.9))
            bp, fp = s.z['premios'] + 2, s.z['factor']
            anterior = (s.cpu.r16(bp), s.cpu.r32(fp))
            try:
                s.cpu.w16(bp, base); s.cpu.w32(fp, factor)
                verificar(f'operandos:{base}:{factor:08x}', 0, bits(.9), s.aplicar(), base, factor)
            finally:
                s.cpu.w16(bp, anterior[0]); s.cpu.w32(fp, anterior[1])

    # Caducidad->GameOver->WaitResult->ApplyResult. No preparar ni reinyectar
    # contexto entre estados. Solo reloj de camara externo al salir de GameOver.
    for dificultad in range(6):
        umbral = ee.fpu_h('mul', s.coeficientes[dificultad], bits(400))
        for altura in (bits(0), umbral - 1, umbral):
            s.preparar_resultado(dificultad, bits(.75), estado_apply=False)
            s.cpu.w32(s.mgr + 0x2914, altura)
            pref = f'continuo:{dificultad}:{altura:08x}'
            want = caducidad.esperado(s, dificultad, altura, 2, bits(0))
            want.pop('retorno')
            want['reloj_bits'] = bits(139); want['luces_bits'] = bits(24)
            want['eventos'].append({'tipo': 'luz', 'id': 5})
            revisar(pref + ':entrada', want, caducidad.derrota.para_comparar(s.caducar(desde_fire=True)))
            reloj = bits(139)
            for n in range(200):
                reloj = ee.fpu_h('sub', reloj, bits(1))
                fin = ee.fv(reloj) < 0
                got = s.esperar_derrota(bits(1))
                want_tick = dict(want, reloj_bits=reloj, estado=s.p['wait'] if fin else s.p['gameover'],
                                 flags=0 if fin else 0x88, par=[0, 1] if fin else [0, 0],
                                 gestor_flags=[0, 1] if fin else [0xA5, 0x5A],
                                 eventos=caducidad.cierre(caducidad.es_exito(want['resultado'][1])) if fin else [])
                revisar(pref + f':GameOver:{n}', want_tick, caducidad.derrota.para_comparar(got))
                if fin:
                    break
            revisar(pref + ':ticks', 140, n + 1)
            s.cpu.w32(s.cam + 0x168, bits(2))
            got = s.esperar_resultado()
            revisar(pref + ':WaitResult', dict(want_tick, estado=s.q['apply'], eventos=[]), caducidad.derrota.para_comparar(got))
            ratio = want['resultado'][1]
            rango, premio = verificar(pref + ':ApplyResult', dificultad, ratio, s.aplicar())
            continuos.append({'dificultad': dificultad, 'altura_bits': altura, 'ratio_bits': ratio,
                              'rango': rango, 'premio_solicitado': premio, 'ticks_gameover': n + 1})

    for nombre, preparar, ejecutar in (
        ('no duplicar peticion desde banco', lambda: None, s.aplicar),
        ('no aplicar desde Fire', lambda: s.preparar_resultado(0, bits(.5), False), s.aplicar),
        ('ratio NaN', lambda: None, lambda: s.preparar_resultado(0, 0x7FC00000))):
        preparar()
        try:
            ejecutar()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar(nombre, True, rechazo)
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'ApplyResult Cell {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return {'region': region, 'elf_sha256': s.digest, 'apply_sha256': s.apply_sha256,
            'helpers_resultado': s.helpers_resultado, 'premios_base': s.premios_base, 'factores_bits': s.factores,
            'tabla_rango': s.tabla_rango, 'global_game': s.gamep, 'iguales': iguales, 'total': len(filas),
            'primera_diferencia': primera, 'correcto': iguales == len(filas), 'casos': filas, 'continuos': continuos}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=caducidad.derrota.geometria.carga.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=caducidad.derrota.geometria.carga.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    inicio = time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump({'operacion': 'resultado_cell', 'pares_dorado_real': 0, 'integrado_cpp': False,
                       'alcance': 'ApplyResult/rango/memset/flag4 originales y continuo desde caducidad',
                       'limites': ['peticion Battle_ApplyResultRewards observada, sin premio/persistencia real',
                                   'mundo/UI/reset/voz/audio controlados; reloj camara externo',
                                   '24 operandos artificiales adicionales, no datos originales ni dorados',
                                   'producto esperado reutiliza FPU EE; no prueba independiente de redondeo FPU',
                                   'el guardia de una aplicacion es del banco; no afirma idempotencia del original',
                                   'sin C++/juego/hardware'],
                       'regiones': regiones, 'segundos_banco': round(time.monotonic() - inicio, 3)},
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

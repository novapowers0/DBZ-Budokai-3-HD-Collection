# SPDX-License-Identifier: MIT
"""F1: resultado y espera de camara 7D8 originales EU/USA, offline.

Init, clasificador, memset, getter y flag4 se ejecutan desde ELF verificado.
Solo Battle_ApplyResultRewards se observa sin aplicar premios ni persistencia.
Referencia propia racional del perfil EE existente; no prueba de hardware.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_banco as banco
import disparo_perseguidor as arit
import ee

CTX, TASK, CAM, GAME = 0x1800000, 0x1801000, 0x1802000, 0x1803000
CONFIG = GAME + 0x11C
bits = arit.marcas.bits
ROLES = ('config', 'memset', 'rango', 'premios', 'flag4')
PERFILES = {
    'eu': dict(init=0x3279F0, init_fin=0x327F80, resultado=0x324C50,
               espera=0x324DE0, fin=0x324E30, ctxp=0x49CDF0, camp=0x49CD6C,
               gamep=0x49CBA4, tabla=0x5D97DC, premios=0x5D986C, factor=0x5D98C0,
               camfin=0x2ED8A0, rango=0x324490,
               servicios=(0x28AA40, 0x11DEF8, 0x324490, 0x28A020, 0x28A300)),
    'us': dict(init=0x3276E0, init_fin=0x327C70, resultado=0x324960,
               espera=0x324AF0, fin=0x324B40, ctxp=0x49C5D0, camp=0x49C54C,
               gamep=0x49C394, tabla=0x5D8FDC, premios=0x5D906C, factor=0x5D90C0,
               camfin=0x2ED690, rango=0x3241A0,
               servicios=(0x28A810, 0x11DEF8, 0x3241A0, 0x289DF0, 0x28A0D0))}


def huella(data):
    return hashlib.sha256(data).hexdigest()


def finito(b):
    return type(b) is int and 0 <= b <= ee.M32 and (b >> 23 & 255) != 255


def dividir60(b):
    """COP1 div de serie: vecino par, con subnormal de entrada como cero."""
    q = arit.racional(b) / 60
    if not q or abs(q) < arit.potencia(-126):
        return b & 0x80000000
    r = arit.truncar(q)
    m = r & 0x7FFFFFFF
    medio = (arit.racional(m) + arit.racional(m + 1)) / 2
    if abs(q) > medio or abs(q) == medio and m & 1:
        r += 1
    return r


def premio_redondeado(base, factor):
    # COP1 mul/Booth y CVT.W.S del perfil revisado, luego div MIPS hacia cero.
    producto = arit.racional(arit.producto(bits(base), factor, cop1=True))
    entero = max(-(1 << 31), min((1 << 31) - 1, int(producto)))
    cociente = (abs(entero) // 100) * (-1 if entero < 0 else 1)
    resto = entero - 100 * cociente
    return ((cociente + int(resto >= 50)) * 100) & 0xFFFF


def llamadas(mem, a, b):
    return tuple((w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', mem[a:b])
                 if w >> 26 == 3)


class ResultadoViewer:
    def __init__(self, region, elf=None):
        if ee.FPU_HW != set(ee.FPU_HW_SERIE) or ee.FPU_CERCANO:
            raise ValueError('se requiere perfil FPU de serie')
        self.region, self.p = region, PERFILES[region]
        p = self.p
        mem, self.digest = banco.cargar_elf(elf or (banco.ELF_EU if region == 'eu' else banco.ELF_US))
        if self.digest != banco.ELF_HASH[region]:
            raise ValueError('ELF ajeno al perfil revisado')
        if llamadas(mem, p['resultado'], p['espera']) != p['servicios']:
            raise ValueError('cinco JAL de resultado distintos del perfil')
        if llamadas(mem, p['espera'], p['fin']) != (p['camfin'],):
            raise ValueError('JAL de espera distinto del perfil')
        self.roles = dict(zip(p['servicios'], ROLES))
        self.roles[p['camfin']] = 'camfin'
        self.cpu = c = ee.Ee(mem, 0x4A3DF0 if region == 'eu' else 0x4A35F0)
        c.estricto = True
        c.whitelist = set(p['servicios']) | {p['init'], p['resultado'], p['espera'], p['camfin']}
        c.hooks[p['servicios'][3]] = self._premios
        c.call(p['init'], limite=20000)
        if c.io or c.sin_puente:
            raise RuntimeError('init con E/S o llamadas fuera de perfil')
        self.tablas = [[c.r32(p['tabla'] + 24 * d + 4 * i) for i in range(6)] for d in range(6)]
        if any(t != [bits(x) for x in (999, 75, 60, 45, 30, 0)] for t in self.tablas):
            raise ValueError('tabla de rangos distinta de la revisada')
        self.premios_base = [c.r16(p['premios'] + 2 * i) for i in range(6)]
        if self.premios_base != [100, 3000, 5000, 6000, 8000, 10000]:
            raise ValueError('premios inicializados distintos del perfil')
        self.factores = [c.r32(p['factor'] + 16 * d) for d in range(6)]
        self.intervalos = [(p['init'], p['init_fin']), (p['resultado'], p['espera']),
                           (p['espera'], p['fin']), (p['rango'], p['rango'] + 0x140),
                           (p['camfin'], p['camfin'] + 0x30),
                           (p['servicios'][0], p['servicios'][0] + 0x10),
                           (p['servicios'][1], p['servicios'][1] + 0xC0),
                           (p['servicios'][4], p['servicios'][4] + 0x20)]
        self.hashes = [huella(c.m[a:b]) for a, b in self.intervalos]
        self.eventos = []
        self.preparado = False

    def _premios(self, c):
        self.eventos.append(dict(tipo='premios_solicitados',
                                 config_hex=bytes(c.m[CONFIG:CONFIG + 0x54]).hex(),
                                 flags_antes=c.r32(GAME + 0x8C)))
        c.r[2] = 0

    def preparar(self, dificultad, tiempo, espera=False, relleno=0xA5):
        if type(dificultad) is not int or not 0 <= dificultad <= 5:
            raise ValueError('dificultad fuera de0..5')
        if not finito(tiempo) or type(espera) is not bool or relleno not in (0, 0xA5, 0xFF):
            raise ValueError('entrada fuera del corpus revisado')
        c, p = self.cpu, self.p
        ref = {}
        for a, n in ((CTX, 0x100), (TASK, 0x100), (CAM, 0x200), (GAME, 0x200)):
            c.m[a - 16:a + n + 16] = bytes([relleno]) * (n + 32)
            ref[a - 16] = bytearray(c.m[a - 16:a + n + 16])
        for a, valor in ((CTX + 0x10, tiempo), (CTX + 0x28, dificultad),
                         (TASK + 0x1C, p['espera'] if espera else p['resultado']),
                         (GAME + 0x8C, 0x80)):
            c.w32(a, valor)
            escribir(ref, a, valor)
        for a, v in ((p['ctxp'], CTX), (p['camp'], CAM), (p['gamep'], GAME)):
            c.w32(a, v)
            ref[a] = bytearray(struct.pack('<I', v))
        # Tablas y factores son estado de entrada del ELF/init, no salidas del recorrido.
        for a, n in ((p['tabla'], 0x90), (p['premios'], 12), (p['factor'], 0x54)):
            ref[a] = bytearray(c.m[a:a + n])
        for i in (*range(16, 24), 30):
            c.r[i], c.up[i] = 0x1234567890000000 + i, 0xABCDEF0120000000 + i
        self.gpr = {i: (c.r[i], c.up[i]) for i in (*range(16, 24), 30)}
        for i in range(20, 32):
            c.f[i] = bits(i + .5)
        self.fpr = c.f[20:32]
        self.preparado = True
        return ref

    def paso(self, camara=None):
        c, p = self.cpu, self.p
        if not self.preparado or c.r32(p['ctxp']) != CTX or c.r32(p['camp']) != CAM or c.r32(p['gamep']) != GAME:
            raise ValueError('punteros/contexto fuera del fixture preparado')
        estado = c.r32(TASK + 0x1C)
        if estado not in (p['espera'], p['resultado']):
            raise ValueError('estado ajeno al contrato espera/resultado')
        if camara is not None:
            if estado != p['espera'] or len(camara) != 2 or not all(finito(b) for b in camara):
                raise ValueError('camara finita solo en espera')
            c.w32(CAM + 0x168, camara[0]); c.w32(CAM + 0x16C, camara[1])
        self.eventos = []
        c.io.clear(); c.sin_puente.clear(); c.llamadas.clear()
        c.call(estado, TASK, limite=10000)
        if c.io or c.sin_puente:
            raise RuntimeError('E/S o llamada fuera de perfil')
        return dict(estado=c.r32(TASK + 0x1C),
                    config_hex=bytes(c.m[CONFIG:CONFIG + 0x54]).hex(),
                    flags=c.r32(GAME + 0x8C), eventos=self.eventos.copy(),
                    llamadas=dict(Counter(self.roles[a] for a in c.llamadas.elements())))

    def integridad(self, ref):
        c = self.cpu
        return (all(bytes(c.m[a:a + len(b)]) == bytes(b) for a, b in ref.items())
                and all((c.r[i], c.up[i]) == v for i, v in self.gpr.items())
                and c.f[20:32] == self.fpr and c.r[28] == c.gp
                and c.r[29] == ee.PILA + 0xFF000)


def escribir(ref, addr, valor):
    for a, b in ref.items():
        if a <= addr and addr + 4 <= a + len(b):
            struct.pack_into('<I', b, addr - a, valor & ee.M32)
            return
    raise ValueError('direccion ajena al estado esperado')


def leer(ref, addr):
    for a, b in ref.items():
        if a <= addr and addr + 4 <= a + len(b):
            return struct.unpack_from('<I', b, addr - a)[0]
    raise ValueError('direccion ajena al estado esperado')


def modelo(s, ref, camara=None):
    r, p = {a: bytearray(b) for a, b in ref.items()}, s.p
    estado, eventos = leer(r, TASK + 0x1C), []
    if estado == p['espera']:
        if camara is not None:
            escribir(r, CAM + 0x168, camara[0]); escribir(r, CAM + 0x16C, camara[1])
        if arit.racional(leer(r, CAM + 0x168)) >= arit.racional(leer(r, CAM + 0x16C)):
            escribir(r, TASK + 0x1C, p['resultado'])
        calls = {'camfin': 1}
    elif estado == p['resultado']:
        dificultad = leer(r, CTX + 0x28)
        valor = arit.racional(dividir60(leer(r, CTX + 0x10)))
        tabla = [arit.racional(leer(r, p['tabla'] + 24 * dificultad + 4 * i)) for i in range(6)]
        rango = banco.clasificar_tabla(valor, tabla, 0, '7D8')
        base = struct.unpack_from('<H', r[p['premios']], 2 * (rango + 1))[0]
        premio = base if rango == -1 else premio_redondeado(base, leer(r, p['factor'] + 16 * dificultad))
        config = bytearray(0x54)
        struct.pack_into('<II', config, 0, 2, rango + 1)
        struct.pack_into('<II', config, 0x48, rango & ee.M32, premio)
        offset = CONFIG - (GAME - 16)
        r[GAME - 16][offset:offset + 0x54] = config
        flags = leer(r, GAME + 0x8C)
        eventos.append(dict(tipo='premios_solicitados', config_hex=config.hex(), flags_antes=flags))
        escribir(r, GAME + 0x8C, flags | 4)
        calls = dict.fromkeys(ROLES, 1)
    else:
        raise ValueError('estado esperado no cubierto')
    offset = CONFIG - (GAME - 16)
    return r, dict(estado=leer(r, TASK + 0x1C),
                   config_hex=bytes(r[GAME - 16][offset:offset + 0x54]).hex(),
                   flags=leer(r, GAME + 0x8C), eventos=eventos, llamadas=calls)


def comprobar(region, elf):
    s, filas, continuos = ResultadoViewer(region, elf), [], []
    aislados = operandos = 0
    def revisar(nombre, want, got):
        fila = dict(caso=nombre, igual=want == got, esperado=want, obtenido=got)
        if not fila['igual'] and not any(not f['igual'] for f in filas):
            print(f'Primer DIF {region}: {fila}')
        filas.append(fila)
    def ejecutar(nombre, ref, camara=None):
        ref, want = modelo(s, ref, camara)
        revisar(nombre, want, s.paso(camara))
        revisar(nombre + ':memoria_abi_guardias', True, s.integridad(ref))
        return ref

    tiempos = {bits(v * 60) for v in banco.muestras([999, 75, 60, 45, 30, 0])}
    tiempos.update((0, 1, 0x80000000, 0x80000001))
    for v in (0, 30, 45, 60, 75, 999):
        b = bits(v * 60)
        tiempos.update(k for k in (b - 1, b, b + 1) if 0 <= k < 0x7F800000)
    for dificultad in range(6):
        for tiempo in sorted(tiempos):
            ref = s.preparar(dificultad, tiempo)
            for repeticion in range(2):
                ref = ejecutar(f'aislado:{dificultad}:{tiempo:08x}:{repeticion}', ref)
                aislados += 1

    # Operand fixtures adicionales: tabla original restaurada sin reinyectar resultados.
    for base in (1, 49, 50, 51, 149, 150, 151, 65535):
        for factor in map(bits, (-2, -1.5, 0, .5, 1, 1.5, 2, 32768, 65536)):
            ref = s.preparar(0, bits(90 * 60), relleno=0xFF)  # rango0, premio[1]
            bp, fp = s.p['premios'] + 2, s.p['factor']
            anterior = s.cpu.r16(bp), s.cpu.r32(fp)
            try:
                s.cpu.w16(bp, base); s.cpu.w32(fp, factor)
                struct.pack_into('<H', ref[s.p['premios']], 2, base)
                escribir(ref, fp, factor)
                for repeticion in range(2):
                    ref = ejecutar(f'operandos:{base}:{factor:08x}:{repeticion}', ref)
                    operandos += 1
            finally:
                s.cpu.w16(bp, anterior[0]); s.cpu.w32(fp, anterior[1])

    for dificultad in range(6):
        for tiempo in map(bits, (0, 30 * 60, 45 * 60, 75 * 60, 999 * 60)):
            ref = s.preparar(dificultad, tiempo, espera=True, relleno=0)
            for n, camara in enumerate(((bits(-1), bits(1)), (0, bits(1)),
                                       (bits(.5), bits(1)), (bits(1) - 1, bits(1)),
                                       (bits(1), bits(1)))):
                ref = ejecutar(f'continuo:{dificultad}:{tiempo:08x}:espera{n}', ref, camara)
            revisar(f'continuo:{dificultad}:{tiempo:08x}:entra_resultado', s.p['resultado'], leer(ref, TASK + 0x1C))
            for n in range(2):
                ref = ejecutar(f'continuo:{dificultad}:{tiempo:08x}:resultado{n}', ref)
            continuos.append(dict(dificultad=dificultad, tiempo_bits=tiempo, cuadros=7,
                                  peticiones_premio=2))

    for nombre, accion in (
            ('dificultad negativa', lambda: s.preparar(-1, 0)),
            ('dificultad bool', lambda: s.preparar(True, 0)),
            ('tiempo NaN', lambda: s.preparar(0, 0x7FC00000)),
            ('tiempo infinito', lambda: s.preparar(0, 0x7F800000)),
            ('camara NaN', lambda: s.paso((0x7FC00000, 0))),
            ('contexto ajeno', lambda: s.paso())):
        s.preparar(0, bits(90 * 60), espera=True)
        if nombre == 'contexto ajeno':
            s.cpu.w32(s.p['ctxp'], CTX + 4)
        previo = huella(s.cpu.m)
        try:
            accion()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar('rechazo:' + nombre, True, rechazo and previo == huella(s.cpu.m))
    for (a, b), h in zip(s.intervalos, s.hashes):
        revisar(f'codigo_intacto:{a:08x}', h, huella(s.cpu.m[a:b]))
    iguales = sum(f['igual'] for f in filas)
    primera = next((f for f in filas if not f['igual']), None)
    print(f'Resultado Viewer {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return dict(region=region, elf_sha256=s.digest, perfil=s.p, iguales=iguales,
                total=len(filas), primera_diferencia=primera, correcto=iguales == len(filas),
                llamadas_aisladas=aislados, llamadas_operandos=operandos, continuos=continuos,
                premios_base=s.premios_base, factores_bits=s.factores, tablas_rango_bits=s.tablas,
                intervalos_codigo=[dict(inicio=a, fin_exclusivo=b, sha256=h)
                                   for (a, b), h in zip(s.intervalos, s.hashes)], casos=filas)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    inicio = time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump(dict(operacion='viewer_resultado', pares_dorado_real=0, integrado_cpp=False,
                           referencia_continua='estado esperado propio, sin realimentar salidas observadas',
                           limites=['solo espera y resultado 7D8, sin juego/intro/impactos/mando',
                                    'Battle_ApplyResultRewards es peticion, no premio ni persistencia real',
                                    'reentrada original repite peticion; no se afirma idempotencia',
                                    'camara externa, tablas originales tras init y operandos artificiales separados',
                                    'referencia racional reproduce perfil EE existente, no prueba hardware ni IEEE completo',
                                    'limites de helpers USA son intervalos seleccionados, no tamanos nativos certificados'],
                           regiones=regiones, segundos_banco=round(time.monotonic() - inicio, 3)),
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

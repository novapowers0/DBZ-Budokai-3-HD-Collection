# SPDX-License-Identifier: MIT
"""F1: marcas Cell y Memory originales EU/USA, semillas continuas sin puentes.

Contrato independiente de enteros y producto positivo float32 hacia cero,
incluida la correccion Booth del perfil EE existente (no hardware nuevo).
La referencia grabada disponible es EU, solo siete campos escritos y semilla;
no es un dorado de USA ni del minijuego completo. Sin juego ni emulador.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

import disparo_banco as banco
import ee

TABLA = 0x1500000
MEMORY, MEMORY_FIN = 0x127CA0, 0x127CE0
TAM_MARCA, NUM_MARCAS = 0x30, 5
PASOS = (0x3F000000, 0x3F800000, 0x3F99999A, 0x40000000)
CAP_EU = Path('E:/DBZ3HD_trabajo/dorados_noche/7DE/marcas.jsonl')
PERFILES = {
    'eu': {'elf': banco.ELF_EU, 'gp': 0x4A3DF0, 'fn': 0x31D8F0, 'fin': 0x31DAA0},
    'us': {'elf': banco.ELF_US, 'gp': 0x4A35F0, 'fn': 0x31D600, 'fin': 0x31D7B0}}


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def avanzar(semilla, pasos):
    """Contrato LFSR de 32 bits; no usa instrucciones ni helpers del interprete."""
    for _ in range(pasos):
        entrada = ((semilla >> 31) ^ (semilla >> 4)) & 1
        semilla = (2 * semilla + entrada) % (1 << 32)
    return semilla, (semilla - 1) % (1 << 32)


def producto_entero(n, paso):
    """Producto positivo exacto en enteros, truncado a 24 bits significativos.

    Dominio del banco: n en32..159, paso en .5/1/float32(1.2)/2. Sin
    desbordamiento ni subnormales; no reutiliza ee.fpu_h/ee.fb/ee.fv.
    El multiplicador EE del perfil existente resta una unidad minima al
    producto cuando ft tiene activo el bit1 de mantisa. En este dominio basta
    restar1 antes de truncar el producto entero; solo cambia productos exactos.
    """
    if not 32 <= n <= 159 or paso not in PASOS:
        raise ValueError('producto fuera del dominio documentado')
    producto = n * ((1 << 23) | (paso & 0x7FFFFF))
    if paso & 2:
        producto -= 1
    alto = producto.bit_length() - 1
    exponente = alto + (paso >> 23) - 150
    significando = producto >> (alto - 23)
    return ((exponente + 127) << 23) | (significando & 0x7FFFFF)


def marca_esperada(semilla, paso, anterior):
    numeros = []
    for _ in range(5):
        semilla, valor = avanzar(semilla, 32)
        numeros.append(valor)
    salida = list(anterior)
    salida[0] = numeros[0] % 10
    salida[4:8] = [bits(numeros[1] % 230 - 140), 0,
                   bits(numeros[2] % 270 - 90), bits(1)]
    salida[8] = bits((numeros[3] & 127) / 16)
    salida[9] = producto_entero((numeros[4] & 127) + 32, paso)
    return semilla, salida


class EeMarcas(ee.Ee):
    """Observa argumentos al entrar en Memory; ejecuta su cuerpo nativo."""
    def entrar(self, destino):
        if destino == MEMORY:
            self.solicitudes.append(self.r[4] & ee.M32)
        return super().entrar(destino)


class MarcasCell:
    def __init__(self, region, elf=None):
        if ee.FPU_HW != set(ee.FPU_HW_SERIE) or ee.FPU_CERCANO:
            raise ValueError('se requiere perfil EE de serie, sin override de FPU')
        self.d = d = PERFILES[region]
        mem, self.digest = banco.cargar_elf(elf or d['elf'])
        if self.digest != banco.ELF_HASH[region]:
            raise ValueError('ELF distinto del perfil regional')
        c = self.cpu = EeMarcas(mem, d['gp'])
        c.whitelist = {d['fn'], MEMORY}
        c.estricto = True
        c.solicitudes = []
        body = mem[d['fn']:d['fin']]
        calls = [(w & 0x3FFFFFF) << 2 for (w,) in struct.iter_unpack('<I', body) if w >> 26 == 3]
        if calls != [MEMORY] * 5:
            raise ValueError('frontera de cinco llamadas Memory distinta')
        # Globals derivados de las instrucciones propias de cada ELF.
        load, store, paso = c.r32(MEMORY), c.r32(MEMORY + 0x2C), c.r32(d['fin'] - 0x24)
        if load >> 26 != 0x23 or store >> 26 != 0x2B or paso >> 26 != 0x31:
            raise ValueError('perfil LW/SW/LWC1 distinto')
        if any((w >> 21 & 31) != 28 for w in (load, store, paso)) or (load & 0xFFFF) != (store & 0xFFFF):
            raise ValueError('globals no basados en GP o semillas distintas')
        def global_gp(w):
            imm = w & 0xFFFF
            return d['gp'] + imm - (0x10000 if imm & 0x8000 else 0)
        self.semilla, self.paso = global_gp(load), global_gp(paso)
        self.rangos = [('marcas', d['fn'], d['fin']), ('memory', MEMORY, MEMORY_FIN)]
        self.identidad = [{'nombre': nombre, 'inicio': a, 'fin_exclusivo': b,
                          'sha256': hashlib.sha256(mem[a:b]).hexdigest()}
                         for nombre, a, b in self.rangos]
        self.preparado = False

    def preparar(self, semilla, paso):
        if type(semilla) is not int or not 0 <= semilla <= ee.M32 or paso not in PASOS:
            raise ValueError('semilla u32 y paso del dominio requeridos')
        c = self.cpu
        c.m[TABLA - 16:TABLA + 0x100] = bytes([0xA5]) * 0x110
        c.w32(self.semilla, semilla)
        c.w32(self.paso, paso)
        self.preparado = True

    def _llamar(self, fn, arg):
        c = self.cpu
        c.solicitudes.clear()
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.call(fn, arg, limite=10000)
        if c.io or c.sin_puente or c.hooks:
            raise RuntimeError('E/S, servicio externo o puente no permitido')

    def generar(self, indice=0, nulo=False):
        if not self.preparado or type(indice) is not int or not 0 <= indice < NUM_MARCAS or type(nulo) is not bool:
            raise ValueError('banco no preparado o indice fuera de0..4')
        self._llamar(self.d['fn'], 0 if nulo else TABLA + indice * TAM_MARCA)
        return self.observar()

    def azar(self, pasos):
        if not self.preparado or type(pasos) is not int or not 0 <= pasos <= 255:
            raise ValueError('Memory fuera del dominio de0..255 pasos')
        self._llamar(MEMORY, pasos)
        return [self.cpu.r32(self.semilla), self.cpu.r[2] & ee.M32]

    def observar(self):
        c = self.cpu
        return {'semilla': c.r32(self.semilla),
                'tabla': [c.r32(TABLA + 4 * i) for i in range(NUM_MARCAS * 12)],
                'guardias': [bytes(c.m[TABLA - 16:TABLA]).hex(),
                             bytes(c.m[TABLA + 0xF0:TABLA + 0x100]).hex()],
                'solicitudes_memory': c.solicitudes.copy(), 'paso_bits': c.r32(self.paso)}


def comprobar(region, elf, cap):
    s, filas, continuos = MarcasCell(region, elf), [], []
    def revisar(nombre, want, got):
        filas.append({'caso': nombre, 'igual': want == got, 'esperado': want, 'obtenido': got})
        if want != got and not any(not f['igual'] for f in filas[:-1]):
            print(f'Primer DIF {region}: {nombre}; detalle esperado/obtenido en evidencia')

    semillas = sorted({0, 1, ee.M32, 0x80000000, 0x7FFFFFFF, 3455373742,
                       *(1 << i for i in range(32)),
                       *(ee.M32 ^ (1 << i) for i in range(32)),
                       *((i * 0x9E3779B9 ^ 0xA5A5A5A5) & ee.M32 for i in range(256))})
    vacia = [0xA5A5A5A5] * (NUM_MARCAS * 12)
    def quiere(semilla, tabla, paso, llamadas):
        return {'semilla': semilla, 'tabla': tabla.copy(), 'guardias': ['a5' * 16] * 2,
                'solicitudes_memory': llamadas, 'paso_bits': paso}

    for semilla in semillas:
        for n in (0, 1, 2, 8, 31, 32, 33, 255):
            s.preparar(semilla, PASOS[1])
            revisar(f'Memory:{semilla:08x}:{n}', list(avanzar(semilla, n)), s.azar(n))
        for paso in PASOS:
            s.preparar(semilla, paso)
            st, marca = marca_esperada(semilla, paso, vacia[:12])
            tabla = marca + vacia[12:]
            revisar(f'marca:{semilla:08x}:{paso:08x}', quiere(st, tabla, paso, [32] * 5), s.generar())
            s.preparar(semilla, paso)
            revisar(f'nulo:{semilla:08x}:{paso:08x}', quiere(semilla, vacia, paso, []), s.generar(nulo=True))

    # No resincronizar semilla/tabla desde las salidas observadas. Cuatro
    # secuencias de128 marcas con otros consumidores Memory intercalados.
    for semilla in (0, 1, 0x80000000, 3455373742):
        s.preparar(semilla, PASOS[0])
        st, tabla = semilla, vacia.copy()
        first = None
        for tick in range(128):
            if tick % 7 == 0:
                n = (1, 8, 32)[tick // 7 % 3]
                st, valor = avanzar(st, n)
                revisar(f'continuo:{semilla:08x}:{tick}:otro_Memory', [st, valor], s.azar(n))
            paso, indice = PASOS[tick % len(PASOS)], tick % NUM_MARCAS
            # El paso es una entrada externa por tick, no una salida reconstruida.
            s.cpu.w32(s.paso, paso)
            offset = indice * 12
            st, marca = marca_esperada(st, paso, tabla[offset:offset + 12])
            tabla[offset:offset + 12] = marca
            want, got = quiere(st, tabla, paso, [32] * 5), s.generar(indice)
            revisar(f'continuo:{semilla:08x}:{tick}:marca', want, got)
            if want != got and first is None:
                first = tick
        continuos.append({'semilla_inicial': semilla, 'marcas': 128, 'primera_diferencia_tick': first,
                          'semilla_final_esperada': st, 'semilla_final_obtenida': s.cpu.r32(s.semilla)})

    # Una referencia EU con15 filas (solo5 entradas/salidas distintas), no
    # convertirla en referencia USA ni inventar su snapshot/semilla por cuadro.
    dorado = None
    if region == 'eu':
        registros = [banco.leer_json(linea) for linea in cap.read_text(encoding='utf-8-sig').splitlines()
                     if linea.strip()]
        if not registros:
            raise ValueError('referencia EU vacia')
        escritos = (0, 4, 5, 6, 7, 8, 9)
        ejemplos, resultados_dorado = set(), []
        for i, r in enumerate(registros):
            s.preparar(r['seed_in'], PASOS[2])
            got = s.generar()
            want = [r['seed_out']] + [r[f'm{k}'] for k in escritos]
            sale = [got['semilla']] + [got['tabla'][k] for k in escritos]
            revisar(f'referencia_EU:{i}', want, sale)
            ejemplos.add(tuple([r['seed_in']] + want))
            resultados_dorado.append(want == sale)
        dorado = {'ruta': str(cap), 'sha256': banco.sha256(cap), 'filas': len(registros),
                  'pares_distintos': len(ejemplos), 'iguales': sum(resultados_dorado),
                  'total': len(resultados_dorado), 'campos_marca': list(escritos),
                  'paso_bits_asumido_PAL_no_grabado': PASOS[2],
                  'primera_diferencia': next((i for i, ok in enumerate(resultados_dorado) if not ok), None)}

    for nombre, fn in (
            ('semilla negativa', lambda: s.preparar(-1, PASOS[0])),
            ('semilla bool', lambda: s.preparar(True, PASOS[0])),
            ('semilla >u32', lambda: s.preparar(1 << 32, PASOS[0])),
            ('paso cero', lambda: s.preparar(1, 0)),
            ('paso NaN', lambda: s.preparar(1, 0x7FC00000)),
            ('indice fuera', lambda: s.generar(5)),
            ('Memory negativo', lambda: s.azar(-1))):
        antes = s.observar()
        try:
            fn()
            rechazo = False
        except ValueError:
            rechazo = True
        revisar('rechazo:' + nombre, [True, antes], [rechazo, s.observar()])
    for r in s.identidad:
        revisar('codigo_intacto:' + r['nombre'], r['sha256'],
                hashlib.sha256(s.cpu.m[r['inicio']:r['fin_exclusivo']]).hexdigest())
    primera = next((f for f in filas if not f['igual']), None)
    iguales = sum(f['igual'] for f in filas)
    print(f'Marcas/RNG Cell {region}: {iguales}/{len(filas)}, '
          f'primer DIF {primera["caso"] if primera else None}')
    return {'region': region, 'elf_sha256': s.digest, 'cuerpos': s.identidad,
            'global_semilla': s.semilla, 'global_paso': s.paso, 'semillas_aisladas': len(semillas),
            'fpu_hw': sorted(ee.FPU_HW), 'fpu_cercano': ee.FPU_CERCANO,
            'iguales': iguales, 'total': len(filas), 'primera_diferencia': primera,
            'correcto': primera is None, 'referencia_eu_local': dorado,
            'cobertura': {'memory_aislado': len(semillas) * 8,
                          'marcas_aisladas': len(semillas) * len(PASOS),
                          'puntero_nulo': len(semillas) * len(PASOS),
                          'marcas_continuas': 512, 'otros_consumidores_controlados': 76,
                          'rechazos': 7, 'intervalos_codigo_intacto': 2},
            'continuos': continuos, 'casos': filas}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--referencia-eu', type=Path, default=CAP_EU)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    inicio = time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva y padre existente requeridos')
        regiones = [comprobar(r, e, a.referencia_eu) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump({'operacion': 'marcas_rng_cell', 'integrado_cpp_nuevo': False,
                       'sha256_banco': banco.sha256(Path(__file__)),
                       'sha256_interprete': banco.sha256(Path(ee.__file__)),
                       'pares_maquina_completa': 0, 'puentes': [],
                       'alcance': 'Memory/marcas originales, oraculo independiente y semilla persistente',
                       'limites': ['oraculo sintetico; no hardware nuevo ni distribucion estadistica',
                                   'referencia solo EU15 filas/5 pares distintos, paso PAL supuesto',
                                   'sin snapshot completo ni referencia USA ni impacto/mundo/render',
                                   'otro consumidor RNG controlado; no integra la maquina de estados',
                                   'sin C++ nuevo/juego/emulador/build'],
                       'regiones': regiones, 'segundos_banco': round(time.monotonic() - inicio, 3)},
                      f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in regiones) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

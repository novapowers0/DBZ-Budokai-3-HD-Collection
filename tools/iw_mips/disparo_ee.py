# SPDX-License-Identifier: MIT
"""F1: contrato PMFHL.LW de Ee y compatibilidad con la extension local antigua.

Semantica: empaquetar las palabras bajas de LO/HI de ambas unidades en
rd[31:0],rd[63:32],rd[95:64],rd[127:96]. Banco sintetico, no hardware.
"""
import argparse
import json
from pathlib import Path
from types import MethodType

import disparo_banco as banco
import ee


def i_pmfhl_lw(s, pc, w, rs, rt, rd, sa, imm, simm):
    def ejecutar():
        s.r[rd] = (s.lo & ee.M32) | ((s.hi & ee.M32) << 32)
        s.up[rd] = (s.lo1 & ee.M32) | ((s.hi1 & ee.M32) << 32)
    return s._w(rd, ejecutar)


def instalar(cpu):
    if not hasattr(cpu, 'i_pmfhl_lw'):
        cpu.i_pmfhl_lw = MethodType(i_pmfhl_lw, cpu)


def comprobar(region='eu', local=False):
    mem, digest = banco.cargar_elf(banco.ELF_EU if region == 'eu' else banco.ELF_US)
    if digest != banco.ELF_HASH[region]:
        raise ValueError('ELF distinto del perfil revisado')
    c = ee.Ee(mem, 0x4A3DF0 if region == 'eu' else 0x4A35F0)
    if local:
        c.i_pmfhl_lw = MethodType(i_pmfhl_lw, c)
    elif not hasattr(type(c), 'i_pmfhl_lw'):
        raise ValueError('falta PMFHL.LW en la clase Ee; no usar fallback en esta prueba')
    antes = c.i_pmfhl_lw.__func__
    instalar(c)
    if c.i_pmfhl_lw.__func__ is not antes or ('i_pmfhl_lw' in vars(c)) != local:
        raise ValueError('instalar sustituyo el metodo o introdujo un parche inesperado')
    # Instruccion realmente alcanzada por el producto double de ambos ELF.
    original = c.r32(0x11A17C)
    if ee.RB.Instruction(original, 0x11A17C, ee.RB.InstrCategory.R5900).getOpcodeName() != 'pmfhl.lw':
        raise ValueError('ancla PMFHL.LW distinta del ELF revisado')
    filas = []
    valores = [(0, 0, 0, 0), (1, 2, 3, 4), (ee.M64,) * 4,
               (0x1234567889ABCDEF, 0xFEDCBA9876543210, 0xABCDEF0112345678, 0x87654321FEDCBA98)]
    for lane in range(4):
        for bit in (0, 15, 31, 32, 63):
            v = [0] * 4
            v[lane] = 1 << bit
            valores.append(tuple(v))
    for i, v in enumerate(valores):
        c.lo, c.hi, c.lo1, c.hi1 = v
        # Ensamblar solo el destino; el resto de bits viene del ELF.
        for rd in (1, 0, 31):
            word = (original & ~(31 << 11)) | rd << 11
            c.w32(0x1500000, word)
            c.code.pop(0x1500000, None)
            c.r[rd] = 0 if rd == 0 else ee.M64
            c.up[rd] = 0 if rd == 0 else ee.M64
            registros = (c.r[:], c.up[:])
            c.decodificar(0x1500000)()
            # Oraculo por bytes: no reutiliza el empaquetado del interprete.
            esperado = b''.join((n & ee.M32).to_bytes(4, 'little') for n in v)
            obtenido = c.r[rd].to_bytes(8, 'little') + c.up[rd].to_bytes(8, 'little')
            if rd == 0:
                esperado = bytes(16)
            intactos = all(c.r[i] == registros[0][i] and c.up[i] == registros[1][i]
                           for i in range(32) if i != rd)
            igual = esperado == obtenido and (c.lo, c.hi, c.lo1, c.hi1) == v and intactos
            filas.append({'caso': f'{i}:rd{rd}', 'igual': igual,
                          'otros_registros_intactos': intactos,
                          'esperado_hex': esperado.hex(), 'obtenido_hex': obtenido.hex()})
    return {'region': region, 'implementacion': 'extension_local' if local else 'clase_Ee',
            'iguales': sum(f['igual'] for f in filas), 'total': len(filas),
            'primera_diferencia': next((f for f in filas if not f['igual']), None),
            'elf_sha256': digest, 'instruccion': original, 'casos': filas}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    if a.salida.exists() or not a.salida.parent.is_dir():
        raise ValueError('salida debe ser nueva y su padre debe existir')
    resultados = [comprobar(region, local) for region in ('eu', 'us') for local in (False, True)]
    for r in resultados:
        print(f'PMFHL.LW {r["region"]}/{r["implementacion"]}: '
              f'{r["iguales"]}/{r["total"]}, primer DIF {r["primera_diferencia"]}')
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump({'operacion': 'pmfhl_lw_comun_y_compatibilidad',
                   'autoridad': 'contrato sintetico de empaquetado, no hardware',
                   'hardware_verificado': False, 'resultados': resultados,
                   'iguales': sum(r['iguales'] for r in resultados),
                   'total': sum(r['total'] for r in resultados),
                   'primera_diferencia': next((r for r in resultados if r['primera_diferencia']), None)},
                  f, indent=2, ensure_ascii=False)
    return 0 if all(r['iguales'] == r['total'] for r in resultados) else 1


if __name__ == '__main__':
    raise SystemExit(main())

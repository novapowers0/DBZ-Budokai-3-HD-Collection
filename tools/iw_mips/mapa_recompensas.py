# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (10/11): banco SIN JUEGO de las recompensas de una mision de IW (IW-A03 e IW-D01).
El oraculo es el ELF del jugador ejecutado en ee.py, no una tabla copiada:
  - Zeni: Char_ComputeScaledStat (EU 0x2ABB80) + Char_RoundAndScaleStat (EU 0x2ABD90), con la sexta dificultad (Z, x200),
    el multiplicador de la capsula de Zeni (param_3 = +5 del registro de capsulas: x1,1/1,2/1,3/1,5/x2) y el modo
    especial (0x5CB88C == 0x4C: mision 77, x100, sin dificultad ni capsula).
  - Rango: Char_ComputeStatGrade (EU 0x2AC0D0) x3 y los cortes de Battle_ComputeRankAndRecord (EU 0x28A5E0).
Compara con el espejo EXACTO de src/pasarela_misiones.cpp (GanarIW/RangoIW con dbz3_iw_resultado_original).
Cifra: casos iguales / total y la primera diferencia.
  python tools/iw_mips/mapa_recompensas.py [--region eu|usa] [--verbose]"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import bubbles as B  # noqa: E402
import ee  # noqa: E402
import mapa_espejo as ME  # noqa: E402

USA_ELF = Path('E:/DBZ3HD_trabajo/T2B/usa/SLUS_218.42')
REG = {
    'eu': dict(elf=B.ELF, gp=0x4A3DF0, zeni=0x2ABB80, redondeo=0x2ABD90, grado=0x2AC0D0, modo=0x5CB88C, bloque=0x5CB840,
               tabla=0x4774D0, premio=0x0E, paso=0x60),
    'usa': dict(elf=USA_ELF, gp=0x4A35F0, zeni=0x2AB690, redondeo=0x2AB8A0, grado=0x2ABBE0, modo=0x5CB08C, bloque=0x5CB040,
                tabla=0x476CD0, premio=0x0E, paso=0x60),
}
KZ = (50, 80, 100, 130, 160, 200)   # dificultades 0..5 (Very Easy..Very Hard, Z)
KC = (0, 110, 120, 130, 150, 200)   # capsula de Zeni: +5 del registro (1..5)
KE = (0, 20, 40, 60, 100)           # bonus por rango 1..4


def zeni_port(premio, n, dif, cap, rango):
    """GanarIW con dbz3_iw_resultado_original: mismo entero que el ELF (division C, truncada)."""
    if n == 77:   # modo 0x4C: el ELF salta la dificultad (x100), pero la capsula de Zeni SI se aplica
        base = premio * 100
        if cap > 0:
            base = (base // 100) * KC[cap]
        return ((base + 50) // 100) * 100
    base = premio * KZ[dif]
    if cap > 0:
        base = (base // 100) * KC[cap]
    if n <= 10:
        return ((base + 50) // 100) * 100   # sin bonus de rango, pero SI redondeo a la centena (comun a todo modo)
    extra = (base // 100) * KE[rango]
    return ((base + extra + 50) // 100) * 100


def rango_port(notas, vida, vida_max, vida_rival, rival_max, segundos, objetivo, dmg_pct, combo_dano,
               ki_rival, dano_rival, combo_max, exacto=True):
    def escala(v, cortes):
        return sum(1 for c in cortes if v >= c)

    def pct(a, b):
        return a * 100 // b if b > 0 else 0
    ki = ki_rival >= 5
    dano = dano_rival > 499

    def contadores(fight):
        c = 1 if combo_max > 0 else 0
        if fight:
            c += 2 * ki + 1 * dano
        else:
            c += 2 * ki + 2 * dano
        return c
    if notas == 1:
        g0 = escala(vida, (25, 35, 45, 55, 65, 75, 85))
        g1 = 7 - escala(segundos, (60, 75, 90, 120, 150, 180))
        g2 = contadores(False)
    elif notas == 2:
        g0 = escala(vida, (25, 35, 45, 55, 65, 75, 85))
        g1 = escala(pct(rival_max - vida_rival, rival_max - objetivo), (39, 50, 60, 70, 80, 90, 100))
        g2 = contadores(True)
    elif notas == 3:
        g0 = 1 + escala(100 - pct(vida_max - vida, vida_max - objetivo), (15, 30, 45, 60, 75, 90))
        g1 = escala(pct(vida_rival, rival_max), (40, 70, 75, 85, 90, 95, 100))
        g2 = (2 + 2 * (combo_dano < 301)) if exacto else 3
    elif notas == 4:
        suma = escala(vida, (25, 35, 45, 55, 65, 75, 85)) + contadores(False)
        return 4 if suma >= 13 else 3 if suma >= 11 else 2 if suma >= 8 else 1 if suma >= 5 else 0
    else:
        return -1
    suma = g0 + g1 + g2
    return 4 if suma >= 19 else 3 if suma >= 16 else 2 if suma >= 12 else 1 if suma >= 7 else 0


class Banco:
    def __init__(s, reg):
        s.reg = reg
        mem = ME.imagen_vacia() if reg['elf'] == B.ELF else None
        if mem is None:
            d = Path(reg['elf']).read_bytes()
            mem = bytearray(ee.TOTAL)
            phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
            for i in range(phnum):
                tipo, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
                if tipo == 1 and fsz:
                    mem[va:va + fsz] = d[off:off + fsz]
        s.c = ee.Ee(mem, reg['gp'])

    def premio(s, n):
        return struct.unpack_from('<H', s.c.m, s.reg['tabla'] + s.reg['paso'] * (n - 1) + s.reg['premio'])[0]

    def zeni(self, n, dif, cap, rango=0):
        s = self
        # param_1 es el INDICE 0-based del registro (el combate pasa +0x8d2a); sRam005cb88c = ese mismo indice
        s.c.w8(s.reg['modo'], (n - 1) & 0xFF)
        v = s.c.call(s.reg['zeni'], n - 1, dif, cap, limite=1_000_000) & 0xFFFFFFFF
        s.c.w32(s.reg['bloque'] + 0x5A * 4, v)   # bloque de estadisticas: +0x5A es el premio
        s.c.call(s.reg['redondeo'], s.reg['bloque'] + 0x5A * 4, rango, limite=1_000_000)
        return s.c.r32(s.reg['bloque'] + 0x5A * 4)

    def grado(self, offset, nota, que):
        s = self
        return s.c.call(s.reg['grado'], s.reg['bloque'], nota, que, limite=1_000_000) & 0xFF


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--region', choices=('eu', 'usa'), default='eu')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    reg = REG[a.region]
    if not Path(reg['elf']).exists():
        print('region %s: falta el ELF %s' % (a.region, reg['elf']))
        return 3
    banco = Banco(reg)
    if struct.unpack_from('<H', banco.c.m, reg['tabla'])[0] != 1:
        print('tabla de misiones no reconocida en %#x; no se mide' % reg['tabla'])
        return 3
    # --- Zeni: 201 misiones x 6 dificultades x 6 niveles de capsula ---
    tot = ok = 0
    difs = []
    for n in range(1, 202):
        premio = banco.premio(n)
        for dif in range(6):
            for cap in range(6):
                elf = banco.zeni(n, dif, cap)
                mio = zeni_port(premio, n, dif, cap, 0)
                tot += 1
                if elf == mio:
                    ok += 1
                elif len(difs) < 8:
                    difs.append((n, premio, dif, cap, elf, mio))
    print('Zeni: %d/%d iguales (201 misiones x 6 dificultades x 6 capsulas)' % (ok, tot))
    # --- Zeni con el bonus de rango 0..4 ---
    tot3 = ok3 = 0
    difs3 = []
    for n in range(1, 202):
        premio = banco.premio(n)
        for rango in range(5):
            for dif in (0, 2, 5):
                for cap in (0, 1, 5):
                    elf = banco.zeni(n, dif, cap, rango)
                    mio = zeni_port(premio, n, dif, cap, rango)
                    tot3 += 1
                    if elf == mio:
                        ok3 += 1
                    elif len(difs3) < 6:
                        difs3.append((n, dif, cap, rango, elf, mio))
    print('Zeni+rango: %d/%d iguales (201 x 5 rangos x 3 dificultades x 3 capsulas)' % (ok3, tot3))
    for d in difs3:
        print('  DIF mision %d dif %d cap %d rango %d: ELF %d, port %d' % d)
    for d in difs[:6]:
        print('  DIF mision %d premio %d dif %d cap %d: ELF %d, port %d' % d)
    # --- Rango: grados y cortes contra el ELF ---
    tot2 = ok2 = 0
    difs2 = []
    casos = []
    for nota in (1, 2, 3, 4):
        for vida in (0, 24, 25, 34, 35, 44, 45, 54, 55, 64, 65, 74, 75, 84, 85, 100):
            for seg in (0, 59, 60, 74, 75, 89, 90, 119, 120, 149, 150, 179, 180, 200):
                casos.append((nota, vida, seg, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    for nota in (2, 3):
        for rival in (0, 39, 40, 50, 60, 70, 70, 75, 80, 85, 90, 95, 100):
            for dm in (0, 30, 39, 40, 50, 60, 70, 80, 90, 100):
                casos.append((nota, 100, 0, rival, 100, rival, 40, dm, 0, 0, 0, 0))
    for extra in (0, 1, 3, 4, 5, 10, 300, 301, 499, 500):
        for nota in (1, 2, 3, 4):
            casos.append((nota, 50, 100, 50, 100, 50, 40, 50, extra, extra, extra, 1))
    for (nota, vida, seg, rival, rival_max, _r, dmg, _d, combo, ki, dano, combomax) in casos:
        vida_max, objetivo = 100, 40
        # bloque de estadisticas del ELF (LoadWaitBattleMainExitEnd EU 0x208EA0)
        m = banco.c.m
        struct.pack_into('<i', m, reg['bloque'] + 0x10, vida)
        struct.pack_into('<i', m, reg['bloque'] + 0x14, rival)
        struct.pack_into('<i', m, reg['bloque'] + 0x18, seg)
        struct.pack_into('<i', m, reg['bloque'] + 0x1C, 1 if combomax else 0)
        struct.pack_into('<i', m, reg['bloque'] + 0x20, 0)
        struct.pack_into('<i', m, reg['bloque'] + 0x24, ki)
        struct.pack_into('<i', m, reg['bloque'] + 0x28, dano)
        struct.pack_into('<i', m, reg['bloque'] + 0x2C, 0)
        struct.pack_into('<i', m, reg['bloque'] + 0x30, 0)
        struct.pack_into('<i', m, reg['bloque'] + 0x34, 0)
        struct.pack_into('<i', m, reg['bloque'] + 0x38, 0)
        struct.pack_into('<i', m, reg['bloque'] + 0x3C, combo)
        struct.pack_into('<i', m, reg['bloque'] + 0x40, 100 - (100 * (vida_max - vida) // (vida_max - objetivo)))
        struct.pack_into('<i', m, reg['bloque'] + 0x44,
                         100 * (rival_max - rival) // (rival_max - objetivo) if rival_max > objetivo else 0)
        g0 = banco.grado(reg['bloque'], nota, 0)
        g1 = banco.grado(reg['bloque'], nota, 1)
        g2 = banco.grado(reg['bloque'], nota, 2)
        suma = g0 + g1 + g2
        elf = (4 if suma >= 13 else 3 if suma >= 11 else 2 if suma >= 8 else 1 if suma >= 5 else 0) if nota == 4 else \
              (4 if suma >= 19 else 3 if suma >= 16 else 2 if suma >= 12 else 1 if suma >= 7 else 0)
        mio = rango_port(nota, vida, vida_max, rival, rival_max, seg, objetivo, dmg, combo, ki, dano, combomax)
        tot2 += 1
        if elf == mio:
            ok2 += 1
        elif len(difs2) < 8:
            difs2.append((nota, vida, seg, rival, combo, ki, dano, (g0, g1, g2), elf, mio))
    print('Rango: %d/%d iguales (grados 0/1/2 del ELF y cortes de la mision y del Road)' % (ok2, tot2))
    for d in difs2[:6]:
        print('  DIF nota %d vida %d seg %d rival %d combo %d ki %d dano %d: grados %s -> ELF %d, port %d' % d)
    # --- Capsula de Zeni: la familia de SCD 0x58 y la regla del port sobre la lista equipada de cada mision ---
    elf = Path(reg['elf']).read_bytes()
    base = None
    for q in range(0, len(elf) - 0x319 * 16, 2):
        if (struct.unpack_from('<H', elf, q)[0] == 700 and struct.unpack_from('<H', elf, q + 2)[0] == 1 and
                struct.unpack_from('<H', elf, q + 4)[0] == 256 and struct.unpack_from('<H', elf, q + 16)[0] == 701):
            base = q
            break
    prop, scd2id = {}, {}
    for i in range(0x319 if base is not None else 0):
        cid = struct.unpack_from('<H', elf, base + i * 16)[0]
        scd = struct.unpack_from('<H', elf, base + i * 16 + 2)[0]
        prop[cid] = elf[base + i * 16 + 5]
        scd2id.setdefault(scd, cid)
    lo = hi = -1
    for i in range(0x319 if base is not None else 0):
        if struct.unpack_from('<H', elf, base + i * 16 + 2)[0] != 0x58:
            continue
        lo = struct.unpack_from('<H', elf, base + i * 16)[0]
        nivel, hi = elf[base + i * 16 + 5], lo
        for j in range(i + 1, 0x319):
            if elf[base + j * 16 + 5] != nivel + 1:
                break
            hi = struct.unpack_from('<H', elf, base + j * 16)[0]
            nivel += 1
        break
    phoff = struct.unpack_from('<I', elf, 28)[0]
    _t, foff, fva = struct.unpack_from('<3I', elf, phoff)
    delta = fva - foff   # vaddr - offset del primer PT_LOAD (en estos ELF 0xFFF80)
    tabla_file = reg['tabla'] - delta
    print('capsula de Zenie: SCD 0x58 = ID %d..%d (niveles %s)' % (lo, hi, [prop.get(k) for k in range(lo, hi + 1)]))
    con_cap = 0
    niveles = {}
    for n in range(1, 202):
        r0 = tabla_file + reg['paso'] * (n - 1)
        sel = 0
        for slot in (0x1E, 0x20, 0x22, 0x2C, 0x2E, 0x30, 0x32, 0x34):
            scd = struct.unpack_from('<H', elf, r0 + slot)[0]
            cid = scd2id.get(scd)
            if cid is None or not (lo <= cid <= hi):
                continue
            if 1 <= prop.get(cid, 0) <= 5:
                sel = prop[cid]
                break
        if sel:
            con_cap += 1
            niveles[sel] = niveles.get(sel, 0) + 1
    print('misiones con capsula de Zenie equipada en su registro: %d/201 (niveles %s)' % (con_cap, niveles))
    if a.verbose:
        for n in (1, 10, 11, 77, 96, 97, 201):
            print('  mision %d premio %d: Zeni dif Z cap 5 = %d' % (n, banco.premio(n), banco.zeni(n, 5, 5)))
    bien = ok == tot and ok2 == tot2 and ok3 == tot3
    print('RESUMEN recompensas %s: %s' % (a.region, '0 DIF' if bien else 'DIF'))
    return 0 if bien else 1


if __name__ == '__main__':
    sys.exit(main())

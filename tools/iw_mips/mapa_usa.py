# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (10/11): manifest y verificacion USA de las funciones/datos del mapa y de las recompensas de IW
(IW-U01 en lo que toca a S3). No ejecuta el juego ni PCSX2: compara el CUERPO de cada funcion EU con el USA ignorando
inmediatos (misma normalizacion que T2B/verifica_usa.py) y las tablas por su firma, y rechaza un par si el cuerpo no
coincide. Imprime la tabla corregida (con `--cpp` sale como pares EU->USA listos para kIm*).
  python tools/iw_mips/mapa_usa.py [--cpp] [--todo] [--ventana 0x800]
Criterio: 0 MAL. Una direccion USA no se acepta por restar 0x800: se acepta solo si el cuerpo coincide."""
import argparse
import bisect
import struct
import sys
from pathlib import Path

EU = Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47')
USA = Path('E:/DBZ3HD_trabajo/T2B/usa/SLUS_218.42')
GHIDRA = Path('E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c')

# (nombre, EU, USA esperado/hint, obligatorio para S3). Las direcciones USA de la tabla del .inl son las de MapaUsaIm.
PARES = [
    ('Story_MapObjectsAppearTick', 0x2FCCB0, 0x2FCAA0, True),
    ('LoadWaitGeneralDragonMissionIcons_PlayerCommands', 0x2FD0F0, 0x2FCEE0, True),
    ('GeneralMapScripts_MissionDots', 0x2FE270, 0x2FE060, True),
    ('Story_MapCameraUpdate', 0x2F9280, 0x2F9070, True),
    ('Capsule_LoadAndInitData', 0x2AB8E0, 0x2AB3F0, True),
    ('Capsule_UpdateUnlockFlagsAt0xC4', 0x2AB590, 0x2AB0A0, True),
    ('DragonMissionStoryModeBattlePointCapsules_Moderation', 0x2AB730, 0x2AB240, True),
    ('MiniGokuAnimations', 0x31BFA0, 0x31BCB0, True),
    ('Transition_SetupWithParams', 0x2ADD80, 0x2AD890, True),
    ('Story_PickObjectAtCursor', 0x2F8200, 0x2F7FF0, True),
    ('Story_MapObjectVisibility', 0x2FD3A0, 0x2FD190, True),
    ('Story_MapIntroDelayTick', 0x2F8790, 0x2F8580, False),
    ('_StartUnlock___Func', 0x2F8830, 0x2F8620, False),
    ('_StartUnlock___Func2', 0x2F86B0, 0x2F84A0, False),
    ('DragonMissionMiscPlayerFuncs', 0x2F98D0, 0x2F96C0, False),
    ('DragonMissionScenarioChange_L1_R1', 0x2FDF80, 0x2FDD70, False),
    ('Story_MapResultDelayTick', 0x2F9100, 0x2F8EF0, False),
    ('Story_MapReloadAfterDelayTick', 0x2F9060, 0x2F8E50, False),
    ('Story_MapScrollToTargetTick', 0x2F8390, 0x2F8180, False),
    ('Story_MapWaitThenPop', 0x2FF3E0, 0x2FF1D0, False),
    ('Story_MapHandleStartButton', 0x365090, 0x3651D0, False),
    ('Story_MapDrawMenu', 0x365110, 0x365250, False),
    ('Minigame_StartButton_PauseMenu', 0x289880, 0x289650, False),
    ('LoadWaitDragonMissionMinigame_Battle', 0x289C20, 0x2899F0, False),
    ('Char_ComputeScaledStat', 0x2ABB80, 0x2AB690, True),
    ('Char_RoundAndScaleStat', 0x2ABD90, 0x2AB8A0, True),
    ('Char_ComputeStatGrade', 0x2AC0D0, 0x2ABBE0, True),
    ('Battle_ComputeRankAndRecord', 0x28A5E0, 0x28A3B0, True),
    ('SetScore', 0x2AE040, 0x2ADB50, False),
    ('Battle_ApplyResultRewards', 0x28A020, 0x289DF0, False),
    ('Save_UpdateRecordIfChanged', 0x2B81D0, 0x2B7FA0, False),
    ('ShenronSystemFunc', 0x2AF360, 0x2AEE70, False),
    ('CapsulePick', 0x2B1220, 0x2B0D10, False),
    ('DragonMission_RandomizeMatchSetup', 0x2AE4D0, 0x2ADFF0, False),   # cuerpo distinto EU/USA

    ('StoryModeLockedCapsules', 0x2ABEC0, 0x2AB9D0, False),
    ('DragonMissionMainMenu_TitleScripts', 0x2FC330, 0x2FC120, False),
    ('Story_SpawnMapObjectSprite', 0x2FD580, 0x2FD370, False),
    ('LoadWaitBattleMainExitEnd', 0x208EA0, 0x2086E0, False),   # cuerpo distinto EU/USA

]


def cargar(p):
    d = Path(p).read_bytes()
    phoff = struct.unpack_from('<I', d, 28)[0]
    _t, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff)
    return lambda a, n: d[off + a - va:off + a - va + n], d, va - off


def limites():
    ini = []
    if GHIDRA.exists():
        for l in GHIDRA.read_text(encoding='utf-8', errors='replace').splitlines():
            if l.startswith('// ==== '):
                ini.append(int(l.split()[2], 16))
    return sorted(ini)


INI = limites()


def normal(b):
    o = []
    for i in range(0, len(b) - 3, 4):
        w = struct.unpack_from('<I', b, i)[0]
        op = w >> 26
        if op in (2, 3):
            w &= 0xFC000000
        elif op != 0:
            w &= 0xFFFF0000
        o.append(w)
    return o


def largo(a):
    i = bisect.bisect_right(INI, a)
    return min(INI[i] - a, 0x800) if i < len(INI) else 0x100


def buscar(eu, us, a, hint, ventana):
    n = largo(a)
    t = normal(eu(a, n))
    if not t:
        return None
    for d in sorted(range(-ventana, ventana + 1, 4), key=abs):
        if hint + d < 0:
            continue
        if normal(us(hint + d, n)) == t:
            return hint + d
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cpp', action='store_true')
    ap.add_argument('--todo', action='store_true', help='incluye los pares no obligatorios en el fallo')
    ap.add_argument('--ventana', type=lambda x: int(x, 0), default=0x8000)
    a = ap.parse_args()
    eu, _, _ = cargar(EU)
    us, _, _ = cargar(USA)
    mal = ok = 0
    sin = 0
    filas = []
    for (nom, e, u, obliga) in PARES:
        m = buscar(eu, us, e, u, a.ventana)
        bien = m == u
        if bien:
            ok += 1
        elif not m and not obliga:
            sin += 1   # el cuerpo EU y el USA no coinciden (funcion reescrita entre regiones); no se usa en S3
        else:
            mal += 1
        filas.append((nom, e, u, m, bien, obliga))
        print('%-52s EU %#08x  USA %#08x  %s' % (nom[:52], e, u, 'OK' if bien else
              ('sin pareja por cuerpo (no usada)' if (not m and not obliga) else
               ('MAL -> %#x' % m if m else 'MAL -> sin pareja'))))
    # tablas USA por firma
    _, du, delta = cargar(USA)
    tab = None
    for q in range(0, len(du) - 0x60 * 201, 4):
        if all(struct.unpack_from('<H', du, q + 0x60 * (k - 1))[0] == k for k in range(1, 7)):
            tab = q
            break
    print('tabla de misiones USA: %s (vaddr %#x)' % (hex(tab) if tab is not None else 'NO', (tab or 0) + delta))
    scd = None
    for q in range(0, len(du) - 0x319 * 16, 2):
        if (struct.unpack_from('<H', du, q)[0] == 700 and struct.unpack_from('<H', du, q + 2)[0] == 1 and
                struct.unpack_from('<H', du, q + 16)[0] == 701):
            scd = q
            break
    print('tabla de capsulas USA (Capsules_SCDindexes): %s (vaddr %#x)' % (hex(scd) if scd is not None else 'NO', (scd or 0) + delta))
    z = None
    for i in range(0x319 if scd is not None else 0):
        if struct.unpack_from('<H', du, scd + i * 16 + 2)[0] == 0x58:
            z = scd + i * 16
            break
    cap_ok = z is not None
    print('capsula de Zenie USA: SCD 0x58 -> ID %s (+5 %s)' % (
        struct.unpack_from('<H', du, z)[0] if cap_ok else 'NO', du[z + 5] if cap_ok else 'NO'))
    if a.cpp:
        print('\n// EU -> USA (cuerpo verificado)')
        for (nom, e, u, m, bien, obliga) in filas:
            if bien:
                print('// %s\nkIm...M = %#x;  // USA %#x' % (nom, e, u))
    fallo = mal if a.todo else sum(1 for f in filas if not f[4] and f[5])
    print('RESUMEN mapa_usa: %d OK, %d sin pareja por cuerpo, %d MAL (%d obligatorios)' %
          (ok, sin, mal, sum(1 for f in filas if f[5] and not f[4])))
    return 0 if fallo == 0 and tab is not None and cap_ok else 1


if __name__ == '__main__':
    sys.exit(main())

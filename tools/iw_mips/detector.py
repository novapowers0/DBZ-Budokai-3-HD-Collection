# SPDX-License-Identifier: MIT
"""Detector de puentes (T2-A, 7/11): que le falta al interprete (src/iw_mips.*, tools/iw_mips/ee.py) para ejecutar la
funcion de entrada de un minijuego de IW (ELF EU). Solo lectura del ELF y del decompilado de Ghidra (nombres).

Recorre las llamadas directas (jal) desde la entrada. Cada funcion alcanzada es:
  cubierta  -> ya tiene puente en src/iw_mips.cpp (se lee de ahi: Puente/Mudo/puentes[...]); no se recorre.
  puente    -> hay que escribir un puente: por nombre (audio, efecto, camara, dibujo, carga, SO...) o por cuerpo (syscall,
               registros de hardware 0x10xx/0x12xx). 'mudo' (sin efecto en la logica, ~5 min con Ambito::Mudo) o
               'datos' (devuelve algo que la logica lee: camara, modelo, pad, colision; ~1-2 h con su dorado).
  pura      -> el interprete la ejecuta tal cual; se recorre.
Ademas: lecturas de memoria (gp-relativas y lui+offset, palabras distintas) que el espejo tiene que dar (leidas y no
escritas por el arbol), y saltos indirectos (jalr/jr no-ra: tablas de estados que el recorrido estatico no ve).

Uso: python tools/iw_mips/detector.py 7D4 [--prof 8] [--json salida.json]   |   detector.py todos   |   detector.py --autoprueba
"""
import argparse
import json
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

AQUI = Path(__file__).parent
RAIZ = AQUI.parent.parent
ELF = Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47')
GHIDRA = Path('E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c')
FUENTE_PUENTES = RAIZ / 'src/iw_mips.cpp'
GP = 0x4A3DF0
TEXTO0, TEXTO1 = 0x100000, 0x37E000       # igual que bubbles.FIN_TEXTO: lo que sigue son datos

# entradas por minijuego (T1C_puentes_A/B.md, raices de logica por cuadro). Nombres del decompilado.
ENTRADAS = {
    '7D0': ['~Minigame_State', 'Minigame_ProcessPlayerInput', 'GroundMinigameTimer', 'Minigame_MovePlayerByStick',
            'GroundMinigameObjectsAroundTheMap', 'MgResult_ComputeScore', 'MgLogic_GaugeRatioTier', 'Battle_TriggerProgressCutins'],
    '7D3': ['~MgFly_State', 'MgFly_SetupNextNode', 'MgFly_StateIntroSetup', 'MgFly_StateSegmentProgress',
            'MgFly_StateFlyTick', 'MgFly_FinishAndApplyResult', 'MgFly_ProjectToScreen'],
    '7D4': [],
    '7D5': ['MiniGame_UpdateFrame', 'MiniGame_UpdateDispatch', 's_MSN_NB007_Update'],
    '7D6': ['Viewer_StateFlyUpdate0', 'Viewer_StateFlyUpdate2', 'Viewer_StateFlyUpdate3', 'Viewer_StateFlyUpdate4',
            'Viewer_StateFlyUpdate5', 'Viewer_StateFlyUpdate6', 'Viewer_StateFlyUpdate7', 'Viewer_UpdateObjectCollection',
            'Viewer_ReadControlAxes', 'Viewer_UpdateEventTimers'],
    '7DA': ['Mg_UpdateFrame@315B00', 'Mg_UpdateMain@315010'],
    '7DB': ['Mg_Update@3550A0', 'Mg_UpdateClashPhase', 'Mg_StartClashSequence', 'Mg_ApplyHitDamage', 'Mg_ApplyRoundOutcome'],
    '7DC': ['QteMg_Update'],
    '7E0': ['Viewer_ReadControlAxes', 'Viewer_StateFlyUpdate0', 'Viewer_StateFlyUpdate5', 'Viewer_StateFlyUpdate3',
            'Viewer_StateCheckResult', 'Viewer_StateResultSuccess', 'Viewer_StateResultFail',
            'Viewer_UpdateObjectCollection', 'Viewer_StateEnterFinalPose', 'FlyingMinigameCharacterHeightPosition'],
}
ENTRADAS['7D2'] = ENTRADAS['7D9'] = ENTRADAS['7DF'] = ENTRADAS['7D0']
# direcciones fijas cuando el nombre se repite entre minijuegos (formato nombre@hex)
ENTRADAS_DIR = {'7D0': [0x31F280, 0x29BFF0], '7D3': [0x3691A0], '7D6': [0x33C660], '7E0': [0x33C660], '7D4': [0x37CFC0, 0x37A560], '7DA': [0x315B00, 0x315010, 0x314060, 0x314840, 0x314C70, 0x3136B0, 0x315540, 0x315230, 0x3144F0, 0x315FB0],
                '7DB': [0x3550A0, 0x353090, 0x352A50, 0x352510, 0x351950]}
for _k in ('7D2', '7D9', '7DF'):
    ENTRADAS_DIR[_k] = ENTRADAS_DIR['7D0']
LOS_12 = ['7D0', '7D2', '7D3', '7D4', '7D5', '7D6', '7D9', '7DA', '7DB', '7DC', '7DF', '7E0']

# clasificacion por nombre -> (clase, es_mudo). Orden = prioridad. 'datos' = devuelve algo que la logica lee.
REGLAS = [
    (r'^(Audio|AudioFade|s_ADX|Hit_PlayHealth|Gasping|MenuPopUpSound|.*Sound|.*Voice|.*Vib)', 'sonido', True),
    (r'^(Effect|MgEffect|MgPly_SpawnEffect|Mg_SpawnFinishEffect|EffectDraw)', 'efecto', True),
    (r'^(Camera|MiniGame_TransformToCamera|Node_GetCameraBlock|CameraAdjustment|s_SPACE_CAM)', 'camara', False),
    (r'^(UiAnim|UiFade|UI_UpdateFrame|Model_UpdateUvAnim|s_UTL_|s_BG_|s_PLY_(FADE|CUTIN)|MiniGame_DrawBackground|'
     r'TransitionFilter|MenuScripts|Mg_IntroScreen|Mg_SetResultText|CharacterOutlines|FlyingMinigameSprites|'
     r'ObjectsDisappearing|MiniGame_(ResetSlotsDisplay|HighlightCorrect))', 'dibujo/ui', True),
    (r'^(Load|Minigame_Load|FreeAllResource|AFS|RAMOpen|KernelLevelShit3|StagesAndCharacters|GameEngineLoad|MgFly_Load|'
     r'MapLoader|s_Ground)', 'carga', True),
    (r'^(Sce|SceKernel|KernelLevelShit|KernelRelated|CreateSema|WaitSema|DeleteSema|Task_ResetTaskList)', 'SO', True),
    (r'^(CharModel|Char_(Set|Enter|Dispatch)|MgPly(B)?_(Set|Change)|AnimationFrames|MiscellaneousCharacterMesh|SprAnim|'
     r'Mg_StartResultAnim|Collision_|FlyingObjectsHitboxes)', 'modelo/anim/colision', False),
]
# lectura del pad / mando: no es puente (lee un global), pero el port debe dar el global; se anota aparte
PAD = re.compile(r'(ReadPlayer(Buttons|Stick)|ReadControlAxes|GetPressedButton|PlayerInput|MovePlayerByStick)')
TOKEN_HW = (0x10000000, 0x12000000)   # registros DMA/GS/timers del EE (lui 0x1000..0x1200..)


class Elf:
    def __init__(self, p=ELF, ghidra=GHIDRA):
        d = Path(p).read_bytes()
        phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
        self.seg = []
        for i in range(phnum):
            t, off, va, _, fsz, msz, fl, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
            if t == 1 and fsz:
                self.seg.append((va, off, fsz))
        self.d = d
        self.nombres, self.dir_de = {}, {}
        for m in re.finditer(rb'^// ==== ([0-9a-f]{8}) (\S+)', Path(ghidra).read_bytes(), re.M):
            a = int(m.group(1), 16)
            self.nombres[a] = m.group(2).decode()
            self.dir_de.setdefault(self.nombres[a], a)
        self.inicios = sorted(self.nombres)
        self.tablas = self._tablas()

    def _tablas(self):
        """Tablas de punteros a funcion en los datos (>= 3 seguidos): despachos por tabla (jalr) que el recorrido directo no ve."""
        fs, out = set(self.inicios), {}
        for va, off, fsz in self.seg:
            if va + fsz <= TEXTO1:
                continue
            run = []
            for i in range(max(0, TEXTO1 - va), fsz - 3, 4):
                w = struct.unpack_from('<I', self.d, off + i)[0]
                if w in fs:
                    run.append((va + i, w))
                    continue
                if len(run) >= 3:
                    for a, _ in run:
                        out[a] = [x for _, x in run]
                run = []
        return out

    def w(self, a):
        for va, off, fsz in self.seg:
            if va <= a < va + fsz:
                return struct.unpack_from('<I', self.d, off + a - va)[0]
        return None

    def fin(self, a):
        """Fin de la funcion = siguiente simbolo del decompilado (o 0x1000 como tope)."""
        import bisect
        i = bisect.bisect_right(self.inicios, a)
        return min(self.inicios[i] if i < len(self.inicios) else TEXTO1, a + 0x4000, TEXTO1)

    def resolver(self, ref):
        if isinstance(ref, int):
            return ref
        return self.dir_de[ref]


def cubiertas(elf):
    """Direcciones con puente en src/iw_mips.cpp (constantes kX usadas en Puente/Mudo/puentes[...])."""
    s = FUENTE_PUENTES.read_text(encoding='utf-8')
    k = {n: int(v, 16) for n, v in re.findall(r'\b(k[A-Za-z0-9_]+)\s*=\s*0x([0-9A-Fa-f]{5,6})\b', s)}
    # los argumentos de Puente(kX, lambda...) arrastran a la lambda: nos quedamos con el primer identificador de cada llamada
    ids = set()
    for m in re.finditer(r'Puente\(\s*(k[A-Za-z0-9_]+)', s):
        ids.add(m.group(1))
    for m in re.finditer(r'Mudo\(\{([^}]*)\}', s):
        ids |= set(re.findall(r'\bk[A-Za-z0-9_]+\b', m.group(1)))
    for m in re.finditer(r'puentes\[\s*(k[A-Za-z0-9_]+)', s):
        ids.add(m.group(1))
    out = {k[i] for i in ids if i in k}
    out |= {0x127DE0, 0x22FD20, 0x207080, 0x317B70}      # puentes de bubbles.py (printf, voz, boca, icono)
    return out


def clasifica_nombre(n):
    for rx, clase, mudo in REGLAS:
        if re.match(rx, n):
            return clase, mudo
    return None


def analiza(elf, a):
    """Una funcion: llamadas directas, saltos indirectos, hardware, lecturas/escrituras de memoria global (decodificacion a mano)."""
    r = dict(llama=set(), indirectos=0, hw=False, lee=set(), escribe=set(), consts=set())
    lui = {}
    CARGAS = {0x20, 0x21, 0x23, 0x24, 0x25, 0x37, 0x1E, 0x31, 0x27}
    GUARDAS = {0x28, 0x29, 0x2B, 0x3F, 0x1F, 0x39}
    for pc in range(a, elf.fin(a), 4):
        w = elf.w(pc)
        if w is None:
            break
        op, rs, rt, rd, fn = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, w & 63
        imm = w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 3:
            r['llama'].add((w & 0x3FFFFFF) << 2)
        elif op == 0:
            if fn == 9 or (fn == 8 and rs != 31):
                r['indirectos'] += 1
            elif fn == 0xC:
                r['hw'] = True
            lui.pop(rd, None)
        elif op == 0xF:
            lui[rt] = imm << 16
        elif op in (9, 0xD) and rs in lui:
            lui[rt] = (lui[rs] + (simm if op == 9 else imm)) & 0xFFFFFFFF
            if TEXTO1 <= lui[rt] < 0x2000000:
                r['consts'].add(lui[rt])
        elif op in CARGAS | GUARDAS:
            ad = (GP + simm) & 0xFFFFFFFF if rs == 28 else (lui[rs] + simm) & 0xFFFFFFFF if rs in lui else None
            if ad is not None and 0x10000000 <= ad < 0x20000000:
                r['hw'] = True          # DMA / GS / timers del EE
            if ad is not None and TEXTO1 <= ad < 0x2000000:
                (r['escribe'] if op in GUARDAS else r['lee']).add(ad & ~3)
            if op in CARGAS and op != 0x31:
                lui.pop(rt, None)
        elif op not in (4, 5, 6, 7, 1, 0x14, 0x15, 0x16, 0x17):
            lui.pop(rt, None)
    return r


def detecta(clave, prof=8, elf=None, cov=None):
    elf = elf or Elf()
    cov = cov if cov is not None else cubiertas(elf)
    raices = list(ENTRADAS_DIR.get(clave, []))
    for n in ENTRADAS.get(clave, []):
        if n.startswith('~'):
            raices += [x for x, nm in elf.nombres.items() if nm.startswith(n[1:])]
            continue
        if '@' in n:
            continue
        if n in elf.dir_de:
            raices.append(elf.dir_de[n])
    raices = sorted(set(raices))
    res = dict(clave=clave, raices=[f'{a:06X} {elf.nombres.get(a, "?")}' for a in raices], puentes={}, cubiertas=[], puras=0,
               indirectos=0, por_tabla=set(), desconocidas=[], lee=set(), escribe=set(), pad=[])
    visto = {}
    pila = [(a, 0) for a in raices]
    while pila:
        a, p = pila.pop()
        if a in visto:
            continue
        nom = elf.nombres.get(a, f'FUN_{a:08x}')
        visto[a] = nom
        if a in cov and p > 0:
            res['cubiertas'].append(f'{a:06X} {nom}')
            continue
        if not (TEXTO0 <= a < TEXTO1):
            res['desconocidas'].append(f'{a:06X} fuera del texto')
            continue
        c = None if (p == 0 or PAD.search(nom)) else clasifica_nombre(nom)
        if PAD.search(nom):
            res['pad'].append(f'{a:06X} {nom}')
        info = analiza(elf, a)
        if c is None and p > 0 and info['hw']:
            c = ('hardware', True)
        if c is not None:
            res['puentes'][a] = dict(nombre=nom, clase=c[0], mudo=c[1], prof=p)
            continue
        res['puras'] += 1
        if info['indirectos']:
            for c in info['consts']:
                for d in range(0, 0x100, 4):      # la base de la tabla puede estar unas palabras antes del indice usado
                    for x in elf.tablas.get(c + d, ()) if d == 0 else ():
                        res.setdefault('por_tabla', set()).add(x)
                        if p < prof:
                            pila.append((x, p + 1))
        res['indirectos'] += info['indirectos']
        res['lee'] |= info['lee']
        res['escribe'] |= info['escribe']
        if p < prof:
            pila += [(x, p + 1) for x in sorted(info['llama'])]
    res['solo_lee'] = sorted(res['lee'] - res['escribe'])
    return res


def grupos(dirs, hueco=0x40):
    out = []
    for a in dirs:
        if out and a - out[-1][1] <= hueco:
            out[-1][1] = a
            out[-1][2] += 1
        else:
            out.append([a, a, 1])
    return out


def resumen(r):
    pu = r['puentes']
    mudos = [v for v in pu.values() if v['mudo']]
    datos = [v for v in pu.values() if not v['mudo']]
    g = grupos(r['solo_lee'])
    return dict(clave=r['clave'], puentes=len(pu), mudos=len(mudos), datos=len(datos), cubiertas=len(r['cubiertas']), puras=r['puras'],
                indirectos=r['indirectos'], palabras_solo_lee=len(r['solo_lee']), zonas_solo_lee=len(g), palabras_escritas=len(r['escribe']), por_tabla=len(r['por_tabla']))


def informe(r):
    s = resumen(r)
    L = [f"## 0x{r['clave']}  puentes {s['puentes']} (mudos {s['mudos']}, con datos {s['datos']}); ya cubiertos {s['cubiertas']}; "
         f"funciones puras {s['puras']}; saltos indirectos {s['indirectos']} (funciones alcanzadas por tabla {s['por_tabla']}); palabras que solo lee {s['palabras_solo_lee']} en {s['zonas_solo_lee']} zonas"]
    L.append('raices: ' + ', '.join(r['raices']))
    for k in sorted(r['puentes'].items(), key=lambda kv: (kv[1]['mudo'], kv[1]['clase'], kv[0])):
        a, v = k
        L.append(f"  - {a:06X} {v['nombre']}  [{v['clase']}{' mudo' if v['mudo'] else ' DATOS'}]")
    if r['cubiertas']:
        L.append('  ya cubiertas: ' + ', '.join(r['cubiertas']))
    if r['pad']:
        L.append('  pad (global que debe dar el port): ' + ', '.join(r['pad']))
    return '\n'.join(L)


def autoprueba():
    elf = Elf()
    cov = cubiertas(elf)
    assert len(cov) >= 15, len(cov)
    # IA de Bubbles (s_len_3_2f 0x31A940) ya corre en el interprete con muy pocos puentes: el detector ve lo mismo
    ENTRADAS['_ia'] = []
    ENTRADAS_DIR['_ia'] = [0x31A940]
    r = detecta('_ia', elf=elf, cov=cov)
    pu = {v['nombre'] for v in r['puentes'].values()}
    assert r['puras'] > 5 and r['solo_lee'], r['puras']
    assert 0x127DE0 not in r['puentes'], 'printf va cubierto'
    # un puente conocido de una entrada sintetica: la funcion de audio se detecta como puente mudo
    ra = detecta('7D4', elf=elf, cov=cov)
    assert any(v['nombre'] == 'Audio_PlaySlot' and v['clase'] == 'sonido' and v['mudo'] for v in ra['puentes'].values()), ra['puentes']
    # idempotente
    assert resumen(detecta('7D4', elf=elf, cov=cov)) == resumen(ra)
    print(f"detector.py autoprueba OK (cubiertas {len(cov)}; IA Bubbles: {r['puras']} puras, {len(r['puentes'])} puentes {sorted(pu)[:6]}; "
          f"7D4: {len(ra['puentes'])} puentes)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('clave', nargs='?', default='todos', help='7D4 | todos (los 12)')
    ap.add_argument('--prof', type=int, default=8)
    ap.add_argument('--json', type=Path)
    ap.add_argument('--md', type=Path, help='tabla Markdown + detalle')
    ap.add_argument('--autoprueba', action='store_true')
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    elf = Elf()
    cov = cubiertas(elf)
    claves = LOS_12 if a.clave == 'todos' else [a.clave.upper().replace('0X', '')]
    res = [detecta(c, a.prof, elf, cov) for c in claves]
    for r in res:
        print(informe(r))
    if a.json:
        a.json.write_text(json.dumps([dict(resumen(r), puentes=[dict(dir=f'{k:06X}', **v) for k, v in sorted(r['puentes'].items())],
                                           cubiertas=r['cubiertas'], solo_lee=[f'{x[0]:06X}-{x[1]:06X} ({x[2]})' for x in grupos(r['solo_lee'])])
                                      for r in res], indent=1, ensure_ascii=False), encoding='utf-8')
    if a.md:
        a.md.write_text(markdown(res), encoding='utf-8')
    return res


def markdown(res):
    L = ['# T2-A: puentes que le faltan a cada uno de los 12 minijuegos (detector.py, 7/11)', '',
         'Generado con `python tools/iw_mips/detector.py todos --md ...` sobre el ELF EU y el decompilado de Ghidra. Recorrido estatico de llamadas '
         'directas (+ tablas de punteros a funcion). **Mudo** = sin efecto en la logica (Ambito::Mudo, ~5 min); **datos** = devuelve algo que la '
         'logica lee (camara, modelo/anim, colision; ~1-2 h con su dorado). Cifras por clasificacion de nombre: margen ±30 %.', '',
         '| minijuego | puentes | mudos | con datos | ya cubiertos | funciones puras | palabras de memoria que solo lee | por tabla |',
         '|---|---|---|---|---|---|---|---|']
    nombres = {'7D0': 'Raditz', '7D2': 'Camino de la Serpiente', '7D3': 'Ladrillos de Kaio', '7D4': 'NB006', '7D5': 'NB007', '7D6': 'Vuelo Guru',
               '7D9': 'La nave', '7DA': 'NB012', '7DB': 'NB013', '7DC': 'NB014', '7DF': 'Radar', '7E0': 'Carrera Goten'}
    cuenta = {}
    for r in res:
        s_ = resumen(r)
        L.append(f"| 0x{r['clave']} {nombres.get(r['clave'], '')} | {s_['puentes']} | {s_['mudos']} | {s_['datos']} | {s_['cubiertas']} | "
                 f"{s_['puras']} | {s_['palabras_solo_lee']} en {s_['zonas_solo_lee']} zonas | {s_['por_tabla']} |")
        for k, v in r['puentes'].items():
            cuenta.setdefault(k, [v, []])[1].append(r['clave'])
    L += ['', '## Puentes comunes (en cuantos de los 12 aparecen)', '', '| dir | funcion | clase | tipo | minijuegos |', '|---|---|---|---|---|']
    for k, (v, ms) in sorted(cuenta.items(), key=lambda kv: (-len(kv[1][1]), kv[0])):
        if len(ms) >= 3:
            L.append(f"| {k:06X} | {v['nombre']} | {v['clase']} | {'mudo' if v['mudo'] else 'DATOS'} | {len(ms)}: {' '.join(ms)} |")
    unicos = [(k, v, ms) for k, (v, ms) in cuenta.items() if len(ms) < 3]
    L += ['', f'Puentes en 1-2 minijuegos: {len(unicos)} (detalle por minijuego abajo).', '']
    for r in res:
        L += ['', informe(r).replace('## ', '### ', 1)]
        g = grupos(r['solo_lee'])
        L.append('  zonas que solo lee (el espejo debe darlas): ' + ', '.join(f'{x[0]:06X}..{x[1]:06X}({x[2]})' for x in g[:12]) + (' ...' if len(g) > 12 else ''))
    return '\n'.join(L) + '\n'


if __name__ == '__main__':
    main()

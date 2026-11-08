# SPDX-License-Identifier: MIT
"""CPU ORIGINAL de Piccolo en el 0x7DD (IW EU, Piccolo_sCPUScript 0x3638C0) ejecutada desde el ELF con ee.py.

N2 (noche 6-7/11). La CPU NO es la de Bubbles (s_len_3_2f): Battle_UpdateAllCharacters, tipo 0xE, actor 1 ->
Char_DispatchPoseTick (tabla 0x49B2C0) -> MinigameHideNSeekPiccolo_sCPU 0x363D10 -> Piccolo_sCPUScript. Lee el arbol de
colisiones del escenario (Collision_SphereTreeQueryRay, ...), los dos actores, la tabla work y globales.
Dorado: muestras de cap_7dd.py (regiones copiadas en PCSX2 antes y despues de cada llamada) sobre la RAM del savestate
de la persecucion (arbol de colisiones fijo). Nada del juego se copia al repo.

P1 (7/11): ESPEJO MINIMO = el de src/iw_mips.cpp (persecucion::Tick). Sin savestate: solo el ELF, los bloques propios de
abajo y tres puentes: Collision_Raycast (rayo de visibilidad -> wcf::Backend::Ray del campo 906, el de Bubbles; arnes
rayo7dd.cpp), la matriz del hueso 1 de Piccolo (pose fija) y la creacion de la tarea (bloque propio).
Uso: python tools/iw_mips/persecucion.py dorado  [--cap <carpeta de cap_7dd>] [--p2s <savestate>]   (RAM entera)
     python tools/iw_mips/persecucion.py puentes [--cap ...] [--wcf <907_0.bin>]   (espejo minimo + puentes, el del juego)
     python tools/iw_mips/persecucion.py juego --log <dbz3.log>                     (llamadas del juego C++ en ee.py)
"""
import argparse
import json
import re
import struct
import subprocess
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import bubbles as B  # noqa: E402
import ee  # noqa: E402
import raiz7dd as RZ  # noqa: E402

IA = 0x3638C0
CAP = Path('E:/DBZ3HD_trabajo/dorados_noche/7DD/m2')
P2S = Path('E:/DBZ3HD_trabajo/dorados_noche/7DD/estado_persecucion.p2s')
WCF = Path('E:/DBZ3HD_trabajo/pendientes_0711/P1/wcf907_0.bin')       # DATA_CMN 907 hijo 0 de la copia del jugador
RAYO_EXE = Path('E:/DBZ3HD_trabajo/pendientes_0711/P1/rayo7dd.exe')   # tools/iw_mips/rayo7dd.cpp compilado


def u32(m, a): return struct.unpack_from('<I', m, a)[0]


def prueba_dorado(cap=CAP, p2s=P2S, ver=6):
    mem, _ = B.cargar(B.ELF, p2s)
    mem[0xF0000:0x100000] = bytes(0x10000)       # restos de la sonda de la captura del savestate
    base = bytes(mem)
    nom = B.leer_simbolos()
    meta = json.loads((Path(cap) / 'muestras.json').read_text(encoding='utf-8'))
    datos = (Path(cap) / 'muestras.bin').read_bytes()
    b0 = meta['base']
    tam = sum(r[2] for r in meta['pre']) + sum(r[2] for r in meta['post'])
    tot, malas, campos = Counter(), [], Counter()
    for k, mu in enumerate(meta['muestras']):
        d = datos[mu['off']:mu['off'] + tam]
        mem[:] = base
        sitios = {}
        for nombre, dir_, n, o in meta['pre']:
            src = u32(mem, dir_) if nombre else dir_
            if nombre:   # el puntero puede haber cambiado: se toma del bloque de globales si cae dentro
                g = next(((gn, gd, gl, go) for gn, gd, gl, go in meta['pre'] if gn is None and gd <= dir_ < gd + gl), None)
                if g:
                    src = u32(d, g[3] - b0 + dir_ - g[1])
            sitios[nombre or hex(dir_)] = src
            mem[src:src + n] = d[o - b0:o - b0 + n]
        struct.pack_into('<I', mem, B.SEMILLA, mu['seed_in'])
        c = B.nueva_cpu(mem, nom)
        try:
            c.call(IA, mu['actor'])
        except Exception as e:   # noqa: BLE001
            tot['excepcion'] += 1
            malas.append((k, 'excepcion', str(e)[:80]))
            continue
        ok_s = u32(mem, B.SEMILLA) == mu['seed_out']
        dif = []
        for nombre, dir_, n, o in meta['post']:
            src = sitios[nombre or hex(dir_)]
            q = d[o - b0:o - b0 + n]
            s_ = mem[src:src + n]
            for w in range(0, n, 4):
                if q[w:w + 4] != s_[w:w + 4]:
                    dif.append((nombre or hex(dir_), w, q[w:w + 4].hex(), bytes(s_[w:w + 4]).hex()))
        tot['muestras'] += 1
        tot['semilla_igual'] += ok_s
        tot['tirada'] += mu['seed_in'] != mu['seed_out']
        tot['iguales'] += ok_s and not dif
        for x in dif:
            campos[(x[0], hex(x[1]))] += 1
        if not (ok_s and not dif) and len(malas) < ver:
            malas.append((k, mu['cnt'], ok_s, dif[:6]))
    return tot, malas, campos


# ------------------------------------------------------------------------------------------------------------------
# P1: espejo minimo (= src/iw_mips.cpp Tick7DD). Bloques propios del espejo (los de Bubbles/Freezer):
MGR = 0x1000000
JUG, PIC = MGR + 0x800, MGR + 0x28D0          # actor 0 (el jugador, Cell) y actor 1 (Piccolo)
OBJS = 0x1010000                              # tabla work (0x260 por actor): *(0x49CBC0)
W0, W1 = OBJS, OBJS + 0x260
CTXD = 0x1012000                              # *(0x49CBA4) LoadWaitMinigamesFlag: +0xC dificultad
TAREA = 0x1020000                             # READ_SYSTEMFunctions: la tarea de Piccolo (bloque propio, 0x100)
MGW = 0x1032000                               # *(0x49CBB8) MiniGame_GetWork: +0x1E0 contexto de colision, +0x25A tipo
CTXM = 0x1039000                              # *(0x49CDC0) Minigame_GetContextPtr: +0x75C distancia, +0x760 visto
MODELO = 0x103A000                            # *(PIC+0xB78): +0x408 -> matrices de los huesos (locales)
MATS = MODELO + 0x100
HUESO = MODELO + 0x800                        # *(PIC+0xB7C) -> short 1 (hueso del frente, medido en las 380 muestras)
PILA_M = 0x103B000                            # pila de matrices VU (0x49C700 puntero, 0x49C704 profundidad)
COLCTX = 0x1035000
HUESO1 = [0x3F800000, 0, 0, 0, 0, 0x3F800000, 0, 0, 0, 0, 0x3F800000, 0, 0, 0x3F800000, 0, 0x3F800000]  # pose fija: I + y 1
GRID = 0x1033000                              # *(0x49CDB8) rejilla del escenario (Node_WrapPositionInGrid)
CLIP = 0x1031000                              # descriptor de clip a cero (como Bubbles: sin multiplicador de velocidad)
# Cuadros de los clips de Piccolo (+0xB60 -> +0xC6C): BonesMovement del ELF sobre la RAM del savestate del dorado (0..0x3F,
# 0x600..0x62F). Los que faltan caen en la entrada 0x10036 = 30 cuadros, como en el original. = iw_mips.cpp Largo7DD.
LARGOS = {0x0: 100, 0x1: 53, 0x2: 16, 0x3: 18, 0x4: 32, 0x5: 24, 0x6: 38, 0x7: 34, 0x8: 20, 0x9: 100, 0xA: 10, 0xB: 100, 0x14: 10, 0x15: 60, 0x16: 10, 0x17: 30, 0x18: 15, 0x19: 15, 0x1A: 15, 0x1B: 30, 0x1C: 15, 0x1D: 15, 0x1E: 15, 0x1F: 5, 0x20: 11, 0x21: 40, 0x22: 2, 0x23: 23, 0x24: 5, 0x25: 16, 0x29: 7, 0x2A: 20, 0x2B: 20, 0x3D: 44, 0x3E: 27, 0x3F: 26, 0x605: 55, 0x606: 115, 0x607: 31, 0x60E: 70, 0x60F: 2, 0x610: 40, 0x611: 40, 0x612: 20, 0x623: 170, 0x624: 40, 0x625: 55, 0x626: 115, 0x627: 31}
LARGO_OTRO = 30
ELF_USA = Path('D:/DBZ3HD/ps2_games/Infinite World (USA)/SLUS_218.42')


class Dir:
    """Direcciones del ELF (EU SLES_553.47; USA SLUS_218.42 = iw_mips.cpp MapaUsa, comprobadas el 7/11)."""
    def __init__(s, usa=False):
        s.usa = usa
        s.GP = 0x4A35F0 if usa else 0x4A3DF0
        s.SEMILLA = s.GP - 0x7FE8
        (s.P_MGR, s.P_CTXD, s.P_MGW, s.P_OBJS, s.P_CTXM, s.P_PILA, s.PASO) = (
            (0x49C270, 0x49C394, 0x49C3A8, 0x49C3B0, 0x49C5A0, 0x49BF00, 0x49BFDC) if usa else
            (0x49CA70, 0x49CBA4, 0x49CBB8, 0x49CBC0, 0x49CDC0, 0x49C700, 0x49C7DC))
        s.PASO_BITS = 0x3F800000 if usa else 0x3F99999A          # kPasoCampo: 1,0 NTSC / 1,2 PAL
        (s.CPU, s.ENTRAR, s.TRAY, s.IA) = ((0x363E50, 0x363FD0, 0x31A280, 0x363A00) if usa else
                                           (0x363D10, 0x363E90, 0x31A570, 0x3638C0))
        (s.RAYO, s.CREAR, s.GUION, s.VOZ, s.SONIDO, s.BONES, s.AURA, s.RAIZ, s.BORRAR) = (
            (0x2E75C0, 0x140300, 0x11DEF8, 0x22FBB0, 0x2405B0, 0x2384E0, 0x1D55A0, 0x2F5B90, 0x140460) if usa else
            (0x2E77D0, 0x140300, 0x11DEF8, 0x22FD20, 0x240740, 0x238670, 0x1D55A0, 0x2F5DA0, 0x140460))
        s.SWEEP, s.CAMLOC = ((0x2E7430, 0x208370) if usa else (0x2E7640, 0x208390))
        s.FALLAN = ((0x317880, 0x317500) if usa else (0x317B70, 0x3177F0))   # icono y platano de Bubbles
        s.P_GRID = 0x49C598 if usa else 0x49CDB8
        s.PARAMS_PIC = 0x44E2D0 if usa else 0x44EAD0   # *(actor+0xBB8): tabla de Piccolo (+0xC = 9, altura del rayo)
        s.MUDOS = ((0x127DE0, 0x207060, 0x240260) if usa else (0x127DE0, 0x207080, 0x2403F0))   # printf, boca, hitbox
        # accesores con puente en el C++ (kObjsA/B, kCtxF, kGetWork): mismo valor que los globales de arriba
        s.OBJS_F = ((0x29A340, 0x318110) if usa else (0x29A5D0, 0x318400))
        s.CTXF, s.GETWORK = ((0x28A7A0, 0x296C90) if usa else (0x28A9D0, 0x296F20))
        # tick: Minigame_ProcessPlayerInput (borra las banderas de accion del actor), Char_DispatchPoseTick (tabla 0x49B2C0)
        (s.ENTRADA, s.TICK, s.UPD) = ((0x299F40, 0x363FB0, 0x363F90) if usa else (0x29A1D0, 0x363E70, 0x363E50))
        # sin efecto: botones y palanca (Piccolo no tiene mando), mano del #BSK y pista de eventos de la animacion
        s.MUDOS += ((0x299290, 0x298FC0, 0x23F650, 0x23F390) if usa else (0x299520, 0x299250, 0x23F7E0, 0x23F520))


def f32(x): return struct.unpack('<f', struct.pack('<f', x))[0]
def b2f(w): return struct.unpack('<f', struct.pack('<I', w & ee.M32))[0]
def s16(v): return v - 0x10000 if v & 0x8000 else v


class Espejo:
    """Imagen = PT_LOAD del ELF (como Ee::Cargar). El estado de Piccolo sigue en el invitado entre llamadas."""

    def __init__(s, usa=False):
        s.A = A = Dir(usa)
        d = (ELF_USA if usa else B.ELF).read_bytes()
        s.mem = bytearray(ee.TOTAL)
        phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
        for i in range(phnum):
            tipo, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
            if tipo == 1 and fsz:
                s.mem[va:va + fsz] = d[off:off + fsz]
        c = s.cpu = ee.Ee(s.mem, A.GP)
        s.rayos, s.col, s.ev, s.fallo, s.vision = [], [], [], False, []
        s.proveedor = None   # sin registro: f(tipo, desde, hasta, radio, mascara) -> (hit, punto, normal, d, mat, tipo)

        def choque(c, es_rayo):
            # Collision_Raycast(ctx, desde a1, hasta a2, punto a3, contacto t0, t1, mascara t2) / Collision_MoveSphere(...,
            # mascara t1; radio f12): el resultado sale del registro del juego (o del proveedor) = iw_mips.cpp `choque`
            a, b, pt, ct = (c.r[i] & ee.M32 for i in (5, 6, 7, 8))
            hasta = [c.r32(b + 4 * i) for i in range(3)]
            k = 'Y' if es_rayo else 'S'
            if s.rayos:
                r = s.rayos.pop(0)
                if r[0] != k:
                    s.fallo = True
            elif s.proveedor:
                r = (k,) + s.proveedor(k, [c.r32(a + 4 * i) for i in range(3)], hasta, c.f[12],
                                       (c.r[10] & 0xFFFF) if es_rayo else (c.r[9] & ee.M32))
            else:
                r = (k, 0, hasta, [0, 0, 0], 0, 0, 0)
            _, hit, punto, normal, d, mat, tipo = r
            if es_rayo and (c.r[10] & 0xFFFF) == 0xFFFF:   # MiniGame_DrawPrompt (mascara 0xFFFF): rayo de vision
                s.vision.append(([c.r32(a + 4 * i) for i in range(3)], int(hit)))
            for i in range(3):
                c.w32(pt + 4 * i, punto[i] if hit else hasta[i])
            c.w32(pt + 12, c.r32(b + 12))
            c.w32(ct + 0x10, 1 if hit else 0)
            if hit:
                for i in range(3):
                    c.w32(ct + 4 * i, normal[i])
                c.w32(ct + 0xC, d); c.w16(ct + 0x20, mat); c.w16(ct + 0x22, tipo)
            s.col.append((k, int(hit), [c.r32(pt + 4 * i) for i in range(3)], [c.r32(ct + 4 * i) for i in range(3)],
                          c.r32(ct + 0xC), c.r16(ct + 0x20), c.r16(ct + 0x22)))
            c.r[2] = 1 if hit else 0

        def camloc(c):  # CameraLocation(actor+0x640): Piccolo = +0x40 + giro * hueso vivo; el resto, raiz + 0,97 / + 9,2
            x = (c.r[4] & ee.M32) - 0x640
            if x == PIC and s.vivo is not None:
                s._ancla()
                return
            off = 0.97 if s.mem[x + 0x2061] else 9.2
            c.w32(x + 0x640, c.r32(x + 0x40))
            c.w32(x + 0x644, B.f2b(f32(b2f(c.r32(x + 0x44)) + f32(off))))
            c.w32(x + 0x648, c.r32(x + 0x48)); c.w32(x + 0x64C, 0x3F800000)

        def crear(c):  # READ_SYSTEMFunctions (Task_Create): la tarea de Piccolo es un bloque propio; +0x1C = su Update (a0)
            s.mem[TAREA:TAREA + 0x100] = bytes(0x100)
            c.w32(TAREA + 0x1C, c.r[4] & ee.M32)
            s.ev.append(0x7A000000)
            c.r[2] = TAREA

        def voz(c):
            s.voz = c.r[5] & 0xFFFF
            s.ev.append(0x7B000000 | s.voz)

        def falla(c):
            s.fallo = True
            c.r[2] = 0

        def valor(v):
            def f(c): c.r[2] = v
            return f
        nada = lambda c: None
        # A.GUION = 0x11DEF8, memset(tarea+0x30, 0, 0x40): la tarea ya sale a cero de `crear`
        s.R = RZ.Recursos() if RZ.CMN_USA.exists() else None
        s.cintura = False   # fase 1: giro del hueso 1 desde su clip (opt-in hasta medirlo)
        s.raiz_previa = True   # fase 1: +0x50 = raiz al empezar el cuadro y la tarea antes de la Update (= iw_mips.cpp)
        s.lut = RZ.lut_seno(d) if s.R else None
        s.vivo, s.clip_raiz = None, None
        c.hooks.update({A.RAYO: lambda c: choque(c, True), A.SWEEP: lambda c: choque(c, False), A.CAMLOC: camloc,
                        A.CREAR: crear, A.VOZ: voz, A.GUION: nada, A.SONIDO: nada, A.AURA: nada,
                        A.RAIZ: s._raiz, A.BORRAR: nada, A.BONES: s._bones,
                        A.OBJS_F[0]: valor(OBJS), A.OBJS_F[1]: valor(OBJS), A.CTXF: valor(CTXD), A.GETWORK: valor(MGW)})
        for a in A.FALLAN:
            c.hooks[a] = falla
        for a in A.MUDOS:
            c.hooks[a] = nada

    def _bones(s, c):
        """BonesMovement: el clip lo anota y lo pone el port (AnimationsStatic con el largo de LARGOS; = Bubbles)."""
        x = c.r[4] & ee.M32
        cod = c.r32(x + 0xB60)
        n = LARGOS.get(cod, LARGO_OTRO)
        c.w32(x + 0xC68, 0); c.w32(x + 0xC6C, n); c.w32(x + 0xC70, B.f2b(float(n))); c.w32(x + 0xC74, 0)
        c.w32(x + 0xC64, 0x10000); c.w32(MODELO + 0x708, n)   # banco propio para MgChar_PoseTickRise
        for o in (0xC88, 0xC90, 0xC9C, 0xCB0):
            c.w32(x + o, 0)
        c.w32(x + 0xAC4, 0x3F800000)
        s.ev.append(cod)
        if x == PIC and s.R:
            s.clip_raiz = s.R.clip(cod)

    def _ancla(s):
        """Ancla +0x640 de Piccolo = +0x40 + giro(work+0x50) * hueso vivo (CameraLocation / Char_UpdateBoneTransforms)."""
        c = s.cpu
        d = RZ.girar(b2f(c.r32(W1 + 0x50)), b2f(c.r32(W1 + 0x58)), s.vivo)
        for i in range(3):
            c.w32(PIC + 0x640 + 4 * i, B.f2b(f32(b2f(c.r32(PIC + 0x40 + 4 * i)) + d[i])))
        c.w32(PIC + 0x64C, 0x3F800000)

    def _raiz(s, c, x=None, w=None):
        """Char_UpdateRootAndBones(actor, work): vuelca el hueso vivo en +0x40 segun +0x18E8 y pone el ancla."""
        if x is None:
            x, w = c.r[4] & ee.M32, c.r[5] & ee.M32
        if x != PIC or s.vivo is None:   # sin recursos de la raiz: sin efecto, como antes
            return
        fl, v = c.r32(x + 0x18E8), s.vivo
        d = RZ.girar(b2f(c.r32(w + 0x50)), b2f(c.r32(w + 0x58)), v)
        suma = lambda o, k: c.w32(x + o, B.f2b(f32(b2f(c.r32(x + o)) + k)))
        if fl & 1:
            suma(0x40, d[0]); suma(0x48, d[2]); v[0] = 0.0; v[2] = 0.0
        if fl & 2:
            suma(0x44, d[1]); v[1] = 0.0
        if fl & 4:
            suma(0x44, d[1]); v[1] = f32(v[1] - d[1])
        elif fl & 8:
            v[1] = b2f(c.r32(c.r32(x + 0xBB8) + 0xC))
        c.w32(x + 0x18E8, 0)
        s._ancla()

    def animar(s):
        """Animations2 0x207180 sobre el hueso 1 de Piccolo: mezcla hacia el clip en +0xC68 y sube +0xCB4."""
        c = s.cpu
        if s.vivo is None or s.mem[PIC + 0xCCC] or c.r32(PIC + 0xCAC) == 0:
            return
        paso = f32(b2f(s.A.PASO_BITS) * b2f(c.r32(PIC + 0xCAC)))
        beta, u = RZ.mezcla(b2f(c.r32(PIC + 0xCB4)), b2f(c.r32(PIC + 0xCB8)), paso)
        s.vivo = RZ.interpolar(s.vivo, s.R.muestra(s.clip_raiz, b2f(c.r32(PIC + 0xC68))), beta)
        if b2f(c.r32(PIC + 0xCB4)) < 1.0:
            c.w32(PIC + 0xCB4, B.f2b(u))

    def fin_campo(s):
        """ExplorationMinigameBodyAnimationsMerge 0x2AA0A0 tras las Update: AllAnimations (Animations2), y por actor
        CameraLocation(+0x640, 1) + Char_UpdateRootAndBones(actor, work): consume el +0x18E8 que dejo la Update."""
        if s.vivo is None:
            return
        s.animar()
        s._ancla()
        s._raiz(s.cpu, PIC, W1)

    def preparar(s):
        """= Tick7DD con in.inicio (bloques a cero, globales, Piccolo, hueso 1 de pose fija)."""
        m, w32, A = s.mem, s.cpu.w32, s.A
        for a, n in ((MGR, 0x9000), (OBJS, 0x800), (CTXD, 0x40), (TAREA, 0x100), (MGW, 0x300), (CTXM, 0x800),
                     (MODELO, 0x1000), (PILA_M, 0x1000), (CLIP, 0x40)):
            m[a:a + n] = bytes(n)
        w32(A.P_MGR, MGR); w32(A.P_CTXD, CTXD); w32(A.P_MGW, MGW); w32(A.P_OBJS, OBJS); w32(A.P_CTXM, CTXM)
        w32(A.P_PILA, PILA_M); w32(A.P_PILA + 4, 0); w32(A.PASO, A.PASO_BITS)
        w32(MGR + 0x8B80, A.PASO_BITS)                                   # factor del gestor = el paso del campo
        w32(CTXD + 4, 0xE)   # tipo de minijuego (LoadWaitMinigamesFlag+4): Minigame_MovePlayerByStick da la palanca virtual en el 9
        w32(PIC + 0x1380, 1); w32(PIC + 0x1384, JUG); w32(JUG + 0x1384, PIC); w32(PIC + 0xCAC, 0x3F800000)
        w32(PIC + 0xB78, MODELO); w32(MODELO + 0x408, MATS); w32(MODELO + 0x46C, MODELO + 0x600)
        w32(MODELO + 0x614, MODELO + 0x700); w32(PIC + 0xB7C, HUESO); s.cpu.w16(HUESO, 1)
        w32(MGW + 0x1E0, COLCTX); w32(PIC + 0xBB8, A.PARAMS_PIC)
        for i, v in enumerate(HUESO1):
            w32(MATS + 0x40 + 4 * i, v)
        # instancia de animacion (actor+0x640) como Bubbles: autopuntero, descriptor a cero, velocidad 1, sin mezclas
        w32(PIC + 0xBC0, PIC); w32(PIC + 0xB64, CLIP); w32(PIC + 0xAC4, 0x3F800000); w32(PIC + 0x18D8, 0x3F800000)
        w32(PIC + 0xC84, 0xFFFFFFFF); w32(PIC + 0xC8C, 0xFFFFFFFF)

    def tick(s, e):
        """Una llamada del juego (= Tick7DD). e: ini, seed, pic0/j40/j640/p640/p770 (bits), dist, jcbc, dif, t25a,
        w180 y rayos [(hit, punto bits)] del registro. Devuelve las salidas que el C++ escribe en el registro."""
        c, w32, w16, A = s.cpu, s.cpu.w32, s.cpu.w16, s.A
        s.ev, s.col, s.fallo, s.voz = [], [], False, -1
        s.rayos = list(e['rayos'])
        w32(A.SEMILLA, e['seed'])
        if e['ini']:
            s.preparar()
            for i in range(3):
                w32(PIC + 0x40 + 4 * i, e['pic0'][i])
            ei = e.get('estado_ini', 13)   # fase 1 (7/11): 4 = la entrada del original (en el suelo, luego sube)
            w32(PIC + 0x4C, 0x3F800000); w32(W1 + 0x58, 0x3F800000); s.mem[PIC + 0x2061] = 1 if ei == 13 else 0
            for i in range(3):
                w32(PIC + 0x640 + 4 * i, B.f2b(f32(b2f(e['pic0'][i]) + (f32(0.97) if i == 1 else 0.0))))
            w32(PIC + 0x64C, 0x3F800000)
            m = s.mem
            m[GRID:GRID + 0xB00] = bytes(0xB00)
            w32(A.P_GRID, GRID); w32(GRID + 0x630, 7); w32(GRID + 0xAD4, B.f2b(3000.0)); w32(GRID + 0xAD8, B.f2b(3000.0))
            w32(GRID + 0xADC, 10); w32(GRID + 0xAE0, 10); w32(GRID + 0xAF0, B.f2b(-1350.0)); w32(GRID + 0xAF8, B.f2b(-1350.0))
            c.call(A.ENTRAR, PIC, ei)
            if s.R:   # hueso vivo de partida = el clip de entrada en su cuadro
                s.vivo = s.R.muestra(s.clip_raiz, b2f(c.r32(PIC + 0xC68)))
        fx, fz = c.r32(W1 + 0x50), c.r32(W1 + 0x58)
        mat = [fz, 0, fx ^ 0x80000000, 0, 0, 0x3F800000, 0, 0, fx, 0, fz, 0,
               c.r32(PIC + 0x40), c.r32(PIC + 0x44), c.r32(PIC + 0x48), 0x3F800000]
        for i, v in enumerate(mat):
            w32(PIC + 4 * i, v)
        if s.R and s.cintura and s.clip_raiz is not None:
            # Fase 1 (7/11): hueso 1 (WAIST) de MgChar_IsTargetInFront con el giro de su clip en el cuadro +0xC68 (antes
            # pose fija). Medido contra las 380 muestras de m2: eje z a 0,6 grados de mediana (fija: 12,5).
            qv = RZ.giro_waist(s.R, s.lut, s.clip_raiz, b2f(c.r32(PIC + 0xC68)))
            if qv:
                filas = RZ.matriz_giro(qv)
                for f_ in range(3):
                    for k_ in range(3):
                        w32(MATS + 0x40 + 16 * f_ + 4 * k_, B.f2b(f32(filas[f_][k_])))
                if s.vivo is not None:   # fila 3 = la posicion viva del hueso (en vuelo ~ (0; 0,97; 0))
                    for k_ in range(3):
                        w32(MATS + 0x70 + 4 * k_, B.f2b(f32(s.vivo[k_])))
        p640_ok = [c.r32(PIC + 0x640 + 4 * i) for i in range(3)] == list(e['p640'])   # el ancla la lleva el invitado
        for i in range(3):
            w32(PIC + 0x770 + 4 * i, e['p770'][i])
            w32(JUG + 0x40 + 4 * i, e['j40'][i]); w32(JUG + 0x640 + 4 * i, e['j640'][i])
        for a in (PIC + 0x77C, JUG + 0x4C, JUG + 0x64C):
            w32(a, 0x3F800000)
        w16(JUG + 0xCBC, e['jcbc'])
        w32(CTXM + 0x75C, e['dist']); w16(CTXD + 0xC, e['dif']); w16(MGW + 0x25A, e['t25a'])
        w32(W0 + 0x180, 1 if e['w180'] else 0)
        paso = b2f(A.PASO_BITS)
        for o in (0x1A0, 0x1A4):
            w32(W1 + o, B.f2b(f32(b2f(c.r32(W1 + o)) + paso)))
        antes = c.r16(PIC + 0xCBC)
        if s.raiz_previa:
            # Fase 1 (7/11): +0x50 = la raiz al EMPEZAR el cuadro (dorado del tick: A y C traen +0x50 == +0x40 en las
            # 272 parejas). Minigame_RotatePlayerToMoveDir 0x299D80 gira a Piccolo a +0x40 - +0x50; sin esto +0x50 se
            # quedaba en 0 y Piccolo miraba "del centro hacia fuera": de espaldas al jugador, que sale en el centro.
            for i in range(4):
                w32(PIC + 0x50 + 4 * i, c.r32(PIC + 0x40 + 4 * i))
        c.call(A.ENTRADA, PIC)   # Battle_UpdateAllCharacters, actor 1 del tipo 0xE: entrada y tick de pose
        c.call(A.TICK, PIC)
        if s.raiz_previa:
            # Fase 1 (7/11): la tarea mueve ANTES de la Update de pose, que gira a Piccolo hacia +0x40 - +0x50 (en el
            # original el frente queda alineado con el desplazamiento del cuadro: 171 de 177 parejas a < 30 grados)
            if c.r32(W1 + 0xB8):
                c.call(c.r32(c.r32(W1 + 0xB8) + 0x1C) or A.TRAY, c.r32(W1 + 0xB8))
            c.call(A.UPD, PIC)
        else:
            c.call(A.UPD, PIC)       # Char_DispatchPoseUpdate (tabla 0x49B330): palanca, gravedad, suelo, raiz
            if c.r32(W1 + 0xB8):   # la Update de la tarea (Piccolo: MgChar_UpdateMovement, no la de Bubbles)
                c.call(c.r32(c.r32(W1 + 0xB8) + 0x1C) or A.TRAY, c.r32(W1 + 0xB8))
        s.fin_campo()
        return dict(seed=c.r32(A.SEMILLA), codigo=antes, estado=c.r16(PIC + 0xCBC), clip=c.r32(PIC + 0xB60),
                    p=[c.r32(PIC + 0x40 + 4 * i) for i in range(3)], tarea=1 if c.r32(W1 + 0xB8) else 0,
                    modo=c.r16(TAREA + 0x60), vuela=1 if s.mem[PIC + 0x2061] else 0, w1a4=c.r32(W1 + 0x1A4),
                    ev=list(s.ev), col=list(s.col), fallo=s.fallo or not p640_ok, p640_ok=p640_ok)

    def llamar(s, e, fn):
        """Solo la prueba del dorado: Piccolo_sCPUScript con el estado completo de la muestra (work, +0xCBC.., tipo)."""
        c, w32, w16, A = s.cpu, s.cpu.w32, s.cpu.w16, s.A
        s.ev, s.col = [], []
        s.rayos = [('Y', h, p, [0, 0, 0], 0, 0, t) for h, p, t in e.get('rayos', [])]
        w32(A.SEMILLA, e['seed'])
        for o, v in ((0x0, e['pm']), (0x40, e['p40']), (0x640, e['p640']), (0x770, e['p770'])):
            for i, x in enumerate(v):
                w32(PIC + o + 4 * i, x)
        for o, v in ((0x40, e['j40']), (0x640, e['j640'])):
            for i, x in enumerate(v):
                w32(JUG + o + 4 * i, x)
        w16(JUG + 0xCBC, e['jcbc']); s.mem[JUG + 0x2061] = e['j2061']; s.mem[PIC + 0x2061] = e['p2061']
        w32(CTXM + 0x75C, e['dist']); w16(CTXD + 0xC, e['dif'])
        w32(W0 + 0x17C, e['w17c']); w32(W0 + 0x180, e['w180'])
        for blk, a in (('w0', W0), ('w1', W1), ('pic_cbc', PIC + 0xCBC)):
            v = e['estado'][blk]
            s.mem[a:a + len(v)] = v
        w16(MGW + 0x25A, e['estado']['t25a'])
        for i, v in enumerate(e['estado'].get('hueso', [])):
            w32(MATS + 0x40 + 4 * i, v)
        return c.call(fn, PIC) & 0xFFFF


RX = re.compile(r'dbz3 iwmips persecucion t=(\d+) ini=(\d) seed_in=([0-9A-F]+) seed_out=([0-9A-F]+) in=(\S+) col=(\S+) '
                r'out=(\S+) ev=(\S+)')


def prueba_juego(log):
    """Cada llamada del juego (dbz3.log, DBZ3_TEST_IWMINI) reejecutada EN ORDEN en el espejo de ee.py: el estado de
    Piccolo sigue en el invitado igual que en el C++, asi que una diferencia se arrastraria a las siguientes."""
    texto = Path(log).read_text(encoding='utf-8', errors='replace')
    usa = 'ELF USA' in texto
    esp = Espejo(usa)
    tot, malas = Counter(), []
    tot['elf_usa'] = int(usa)
    H = lambda t: [int(x, 16) for x in t.split(',') if x]
    for linea in texto.splitlines():
        m = RX.search(linea)
        if not m:
            continue
        t, ini, si, so, ins, col, out, ev = m.groups()
        w = H(ins)
        rayos = []
        if col != '-':
            for r in col.rstrip(';').split(';'):
                h, p, n, d, mt, tp = r[1:].split('/')
                rayos.append((r[0], int(h), H(p), H(n), int(d, 16), int(mt, 16), int(tp, 16)))
        e = dict(ini=int(ini), seed=int(si, 16), pic0=w[0:3], j40=w[3:6], j640=w[6:9], p640=w[9:12], p770=w[12:15],
                 dist=w[15], jcbc=w[16], dif=w[17], t25a=w[18], w180=w[19], rayos=rayos)
        o = esp.tick(e)
        sale = [o['seed'], o['codigo'], o['estado'], o['clip']] + o['p'] + [o['tarea'], o['modo'], o['vuela'], o['w1a4']]
        quiere = [int(so, 16)] + H(out)
        evs = ','.join('%X' % v for v in o['ev']) or '-'
        cols = ''.join('%s%d/%X,%X,%X/%X,%X,%X/%X/%X/%X;' % (k, h, *p, *n, d, mt, tp) for k, h, p, n, d, mt, tp in o['col']) or '-'
        ok = sale == quiere and evs == ev and cols == col and not o['fallo']
        tot['llamadas'] += 1
        tot['iguales'] += ok
        tot['inicios'] += int(ini)
        tot['tiradas'] += int(si, 16) != int(so, 16)
        tot['tareas_creadas'] += evs.count('7A000000')
        tot['rayos'] += sum(r[0] == 'Y' for r in rayos)
        tot['barridos'] += sum(r[0] == 'S' for r in rayos)
        tot['p640_distinto'] += not o['p640_ok']
        tot['estado_%X' % o['estado']] += 1
        if not ok:
            tot['DIF'] += 1
            if len(malas) < 8:
                malas.append((t, quiere, sale, ev, evs, col, cols))
    tot['DIF'] += 0
    return tot, malas, Counter()


def _cargar_muestra(mem, base, meta, d, b0):
    mem[:] = base
    for nombre, dir_, n, o in meta['pre']:
        src = dir_
        if nombre:
            g = next((x for x in meta['pre'] if x[0] is None and x[1] <= dir_ < x[1] + x[2]), None)
            src = u32(d, g[3] - b0 + dir_ - g[1]) if g else u32(mem, dir_)
        mem[src:src + n] = d[o - b0:o - b0 + n]


# Fase 1 (7/11): punto del rayo de vision de Piccolo (+0x770 = hueso de la cabeza) respecto al ancla +0x640, en ejes
# locales (derecha, arriba, frente) por clip: media de las 360 parejas del dorado del tick (desviacion <= 1,1 u salvo
# 0x16/0x18/0x1E, transiciones). Antes: ancla + (0, 5,5, 0) siempre; el original lo lleva hasta 6 u por delante.
CABEZA = {0x15: (0.07, 7.23, 0.6), 0x16: (-0.17, 5.6, 3.45), 0x17: (-0.12, 4.05, 5.45), 0x18: (-0.21, 5.88, 2.66),
          0x21: (-0.67, 5.99, 1.87), 0x23: (0.05, 6.55, 1.64), 0x1C: (-0.07, 3.89, 4.16), 0x1D: (0.81, 2.95, 5.94),
          0x1E: (-0.01, 5.29, 3.21), 0x605: (-1.99, 5.98, 1.76), 0x625: (-1.7, 6.53, 1.38), 0x606: (-0.88, 6.37, 1.98),
          0x626: (-2.03, 6.67, 1.13), 0x60F: (0.08, 4.27, 3.98), 0x610: (0.37, 6.78, -1.69)}


def cabeza(p640, fx, fz, clip):
    """+0x770 (bits) desde el ancla p640 (bits), el frente (fx, fz) de work+0x50/+0x58 y el clip +0xB60."""
    d, a, f = CABEZA.get(clip, (0.0, 5.5, 0.0))
    o = (d * fz + f * fx, a, -d * fx + f * fz)
    return [B.f2b(f32(b2f(p640[i]) + o[i])) for i in range(3)]


# Fase 1 (7/11): en el original el LFSR de Memory (0x127CA0) lo usan tambien otras partes del juego ENTRE dos ticks de
# Piccolo: 184..1104 pasos por cuadro (272 parejas B->C del dorado del tick; (pasos, veces)). El espejo solo tira por
# Piccolo, asi que sus tiradas seguidas salen casi pegadas en el registro de desplazamiento.
PASOS_ENTRE = [(184, 19), (214, 39), (216, 4), (227, 3), (244, 15), (246, 34), (257, 2), (273, 1), (275, 3), (276, 48),
               (289, 6), (292, 2), (302, 1), (303, 2), (305, 12), (307, 1), (319, 8), (332, 2), (335, 4), (337, 6),
               (362, 5), (364, 2), (365, 4), (367, 3), (378, 1), (394, 6), (412, 2), (414, 1), (424, 6), (426, 1),
               (444, 1), (456, 1), (460, 1), (497, 1), (533, 1), (586, 1), (605, 3), (634, 1), (635, 1), (637, 6),
               (638, 1), (667, 4), (776, 1), (806, 1), (819, 2), (994, 1), (1074, 1), (1104, 1)]


def lfsr(s, n):
    """n pasos del LFSR de Memory: s = s << 1 | (s >> 31 ^ s >> 4) & 1 (comprobado contra el ELF, 500 pasos)."""
    for _ in range(n):
        s = ((s << 1) & 0xFFFFFFFF) | (((s >> 31) ^ (s >> 4)) & 1)
    return s


def proveedor_ps2(base):
    """Fase 1 (7/11): proveedor de choques con la colision ORIGINAL: Collision_Raycast / Collision_MoveSphere del ELF sobre
    la RAM del savestate `base` (arbol de colisiones fijo del campo). Misma firma que el del arnes rayo7dd."""
    mem = bytearray(base)
    mem[0xF0000:0x100000] = bytes(0x10000)
    cpu = B.nueva_cpu(mem, B.leer_simbolos())
    ctx = u32(mem, u32(mem, 0x49CBB8) + 0x1E0)
    A_, S0 = Dir(), 0xF0000

    def proveedor(k, x, y, radio, m):
        for i in range(3):
            cpu.w32(S0 + 4 * i, x[i]); cpu.w32(S0 + 0x10 + 4 * i, y[i])
        cpu.w32(S0 + 0xC, 0x3F800000); cpu.w32(S0 + 0x1C, 0x3F800000)
        mem[S0 + 0x20:S0 + 0x80] = bytes(0x60)
        if k == 'S':
            h = cpu.call(A_.SWEEP, ctx, S0, S0 + 0x10, S0 + 0x20, S0 + 0x40, m, fargs=(b2f(radio),))
        else:
            h = cpu.call(A_.RAYO, ctx, S0, S0 + 0x10, S0 + 0x20, S0 + 0x40, 0, m)
        return (int(h & 1), [cpu.r32(S0 + 0x20 + 4 * i) for i in range(3)], [cpu.r32(S0 + 0x40 + 4 * i) for i in range(3)],
                cpu.r32(S0 + 0x4C), cpu.r16(S0 + 0x60), cpu.r16(S0 + 0x62))
    return proveedor


def rayos_wcf(rayos, wcf=WCF, exe=RAYO_EXE):
    """Puente de rayo del juego (wcf::Backend::Ray) sobre una lista [(desde bits, hasta bits, mascara)]."""
    f2 = lambda w: repr(struct.unpack('<f', struct.pack('<I', w))[0])
    txt = '\n'.join(' '.join([f2(x) for x in a] + [f2(x) for x in b] + [str(m)]) for a, b, m in rayos) + '\n'
    out = subprocess.run([str(exe), str(wcf)], input=txt, capture_output=True, text=True, check=True).stdout.split('\n')
    res = []
    for linea in out[:len(rayos)]:
        h, x, y, z, t = linea.split()[:5]
        res.append((int(h), [B.f2b(float(x)), B.f2b(float(y)), B.f2b(float(z))], int(t)))
    return res


def prueba_puentes(cap=CAP, p2s=P2S, wcf=WCF, ver=6):
    """Cada muestra del dorado por el ESPEJO MINIMO (sin savestate) con las entradas grabadas y los puentes del juego:
    v1 = rayo del original y hueso del original (el espejo); v2 = rayo wcf; v3 = rayo wcf + hueso de pose fija."""
    mem, _ = B.cargar(B.ELF, p2s)
    mem[0xF0000:0x100000] = bytes(0x10000)
    base = bytes(mem)
    nom = B.leer_simbolos()
    meta = json.loads((Path(cap) / 'muestras.json').read_text(encoding='utf-8'))
    datos = (Path(cap) / 'muestras.bin').read_bytes()
    b0 = meta['base']
    tam = sum(r[2] for r in meta['pre']) + sum(r[2] for r in meta['post'])
    post_w = next(o for nm, _, _, o in meta['post'] if nm == 'work') - b0
    esp = Espejo()
    entradas, rayos_orig = [], []
    for k, mu in enumerate(meta['muestras']):
        d = datos[mu['off']:mu['off'] + tam]
        _cargar_muestra(mem, base, meta, d, b0)
        A = esp.A
        mgr = u32(mem, A.P_MGR); pic, jug, w = mgr + 0x28D0, mgr + 0x800, u32(mem, A.P_OBJS)
        mgw, ctxm, ctxd = u32(mem, A.P_MGW), u32(mem, A.P_CTXM), u32(mem, A.P_CTXD)
        ww = lambda a, n: [u32(mem, a + 4 * i) for i in range(n)]
        mat = u32(mem, u32(mem, pic + 0xB78) + 0x408) + 0x40 * struct.unpack_from('<h', mem, u32(mem, pic + 0xB7C))[0]
        e = dict(seed=mu['seed_in'], pm=ww(pic, 16), p40=ww(pic + 0x40, 4), p640=ww(pic + 0x640, 4),
                 p770=ww(pic + 0x770, 4), j40=ww(jug + 0x40, 4), j640=ww(jug + 0x640, 4),
                 jcbc=struct.unpack_from('<H', mem, jug + 0xCBC)[0], j2061=mem[jug + 0x2061], p2061=mem[pic + 0x2061],
                 dist=u32(mem, ctxm + 0x75C), dif=struct.unpack_from('<H', mem, ctxd + 0xC)[0],
                 w17c=u32(mem, w + 0x17C), w180=u32(mem, w + 0x180),
                 estado=dict(w0=bytes(mem[w:w + 0x260]), w1=bytes(mem[w + 0x260:w + 0x4C0]),
                             pic_cbc=bytes(mem[pic + 0xCBC:pic + 0xCC8]),
                             t25a=struct.unpack_from('<H', mem, mgw + 0x25A)[0], hueso=ww(mat, 16)))
        # el rayo del ORIGINAL (RAM entera) para v1: se observa Collision_Raycast
        c = B.nueva_cpu(mem, nom)
        obs = []

        def h(c, t=esp.A.RAYO):
            pt, b = c.r[7] & ee.M32, c.r[6] & ee.M32
            reg = (c.r[5] & ee.M32, b, c.r[10] & 0xFFFF)
            ra = c.r[31]; c.r[31] = ee.RETORNO
            del c.hooks[t]
            c.run(t, 50_000_000)
            c.hooks[t] = h
            c.r[31] = ra
            obs.append(([c.r32(reg[0] + 4 * i) for i in range(3)], [c.r32(b + 4 * i) for i in range(3)], reg[2],
                        int(c.r[2] & 1), [c.r32(pt + 4 * i) for i in range(3)], c.r16(mgw + 0x25A)))
        c.hooks[esp.A.RAYO] = h
        c.call(IA, pic)
        rayos_orig.append(obs)
        entradas.append((k, mu, e, d))
    # rayos del puente (wcf) para v2/v3, en una sola tanda por el arnes
    lista = [(o[0], o[1], o[2]) for obs in rayos_orig for o in obs]
    wres = rayos_wcf(lista, wcf) if lista and Path(wcf).exists() else None
    tot, malas, campos = Counter(), [], Counter()
    tot['rayos'] = len(lista)
    if wres:
        ow = [o for obs in rayos_orig for o in obs]
        tot['rayo_hit_igual'] = sum(a[3] == b[0] for a, b in zip(ow, wres))
        tot['rayo_tipo_igual'] = sum(a[3] == b[0] and (not a[3] or a[5] == b[2]) for a, b in zip(ow, wres))
    it = iter(wres or [])
    for k, mu, e, d in entradas:
        quiere_w = d[post_w:post_w + 0x4C0]
        r_orig = [(o[3], o[4], o[5]) for o in rayos_orig[k]]
        r_wcf = [next(it) for _ in rayos_orig[k]] if wres else None
        for var, rayos, hueso in (('v1', r_orig, True), ('v2', r_wcf, True), ('v3', r_wcf, False)):
            if rayos is None:
                continue
            e2 = dict(e, rayos=rayos)
            if not hueso:
                e2['estado'] = dict(e['estado'], hueso=HUESO1)
            esp.preparar()
            try:
                esp.llamar(e2, IA)
            except Exception as ex:   # noqa: BLE001
                tot[var + '_excepcion'] += 1
                malas.append((var, k, str(ex)[:80]))
                continue
            sale_w = bytes(esp.mem[W0:W0 + 0x4C0])
            dif = [o for o in range(0, 0x4C0, 4) if quiere_w[o:o + 4] != sale_w[o:o + 4]
                   and not (o == 0x260 + 0xB8 and (u32(quiere_w, o) != 0) == (u32(sale_w, o) != 0))]
            ok = not dif and u32(esp.mem, B.SEMILLA) == mu['seed_out']
            tot[var] += ok
            tot[var + '_decision'] += not [o for o in dif if o != 0x264] and u32(esp.mem, B.SEMILLA) == mu['seed_out']
            for o in dif:
                campos[(var, hex(o))] += 1
            if not ok and len(malas) < ver:
                malas.append((var, k, mu['cnt'], [hex(o) for o in dif[:6]], u32(esp.mem, B.SEMILLA) == mu['seed_out']))
    tot['muestras'] = len(entradas)
    return tot, malas, campos



# ------------------------------------------------------------------------------------------------------------------
# P1 (7/11, segunda parte): dorado del TICK por parejas de cuadros (tools/iw_mips/cap_tick7dd.py, PCSX2 EU).
# A = antes del tick del cuadro N, B = despues, C = antes del tick N+1. Se comparan los campos de Piccolo (+0x28D0 del
# gestor, 0x20D0 B), su tabla work (+0x260, 0x260 B), la tarea y la semilla.
CAP_TICK = Path('E:/DBZ3HD_trabajo/dorados_noche/7DD/tick')
HITS_SON = 0x240740          # Hits/TransformationSounds: sonido (llamadas al kernel), sin efecto en la RAM del juego
# campos de Piccolo que deciden (offset en el actor): estado/anterior/contador, clip, cuadro, fin de clip, vuela, raiz
CAMPOS_PIC = [('cbc', 0xCBC, 'h'), ('cbe', 0xCBE, 'h'), ('cc0', 0xCC0, 'h'), ('cc4', 0xCC4, 'h'), ('clip', 0xB60, 'I'),
              ('c70', 0xC70, 'I'), ('c74', 0xC74, 'I'), ('vuela', 0x2061, 'B'), ('x', 0x40, 'I'), ('y', 0x44, 'I'),
              ('z', 0x48, 'I')]
CAMPOS_W1 = [('w%X' % o, o, 'I') for o in (0x0, 0x4, 0x8, 0x50, 0x58, 0xB0, 0xB4, 0x194, 0x1A0, 0x1A4, 0x1F4, 0x1F8, 0x23C,
                                         0x240, 0x244, 0x24C)]


def _bloques(meta, datos, base, mem, nombre):
    """Vuelca el bloque `nombre` de la pareja en mem; devuelve {(tipo, dir): direccion real}."""
    sitios = {}
    lista = meta['bloques'][nombre]
    for pasada in (0, 1, 2):   # fijas (traen los punteros globales), luego *(dir) y por ultimo *(*(dir) + off)
        for tipo, d, n, o in lista:
            if pasada != (0 if tipo is None else 1 if tipo == 'p' else 2):
                continue
            if tipo is None:
                src = d
            elif tipo == 'p':
                src = u32(mem, d)
            else:
                src = u32(mem, u32(mem, d) + tipo[1])
            sitios[(str(tipo), d)] = src
            if src and src + n <= ee.RAM:
                mem[src:src + n] = datos[o - base:o - base + n]
    return sitios


def _campos(m, pic, w1, tarea):
    r = {}
    for nom, o, f in CAMPOS_PIC:
        r[nom] = struct.unpack_from('<' + f, m, pic + o)[0]
    for nom, o, f in CAMPOS_W1:
        r[nom] = struct.unpack_from('<' + f, m, w1 + o)[0]
    r['tarea'] = int(u32(m, w1 + 0xB8) != 0)
    if tarea and u32(m, w1 + 0xB8):
        t = u32(m, w1 + 0xB8)
        r.update({'t_modo': struct.unpack_from('<H', m, t + 0x60)[0], 't_dur': u32(m, t + 0x54), 't_tiempo': u32(m, t + 0x50),
                  't_vx': u32(m, t + 0x40), 't_vz': u32(m, t + 0x48)})
    return r


def prueba_tick(cap=CAP_TICK, p2s=P2S, ver=8, usar_raiz=True):
    """1) RAM entera: tick original de ee.py desde A == B de PCSX2 (todas las palabras de Piccolo, su work y la tarea).
    2) Espejo minimo del juego (bloques propios + puentes) desde el estado de A: los campos que deciden == B.
    3) Lo que pasa entre ticks (B -> C): Update de pose + tarea + entrada en el espejo minimo, campos == C."""
    mem, _ = B.cargar(B.ELF, p2s)
    mem[0xF0000:0x100000] = bytes(0x10000)
    base_ram = bytes(mem)
    nom = B.leer_simbolos()
    meta = json.loads((Path(cap) / 'parejas.json').read_text(encoding='utf-8'))
    datos_all = (Path(cap) / 'parejas.bin').read_bytes()
    b0 = meta['base']
    tam = max(o + n for bl in meta['bloques'].values() for _, _, n, o in bl) - b0
    tot, malas, campos = Counter(), [], Counter()
    import subprocess as sp
    pr = sp.Popen([str(RAYO_EXE), str(WCF)], stdin=sp.PIPE, stdout=sp.PIPE, text=True, bufsize=1)

    def proveedor(k, a, b, radio, m):
        q = ' '.join(repr(b2f(x)) for x in a + b)
        pr.stdin.write(('S %s %r %d\n' % (q, b2f(radio), m)) if k == 'S' else ('%s %d\n' % (q, m)))
        pr.stdin.flush()
        h, x, y, z, t, mt, nx, ny, nz, dd = pr.stdout.readline().split()
        return (int(h), [B.f2b(float(x)), B.f2b(float(y)), B.f2b(float(z))],
                [B.f2b(float(nx)), B.f2b(float(ny)), B.f2b(float(nz))], B.f2b(float(dd)), int(mt), int(t))
    esp = Espejo()
    esp.proveedor = proveedor
    for k, par in enumerate(meta['parejas']):
        d = datos_all[par['off']:par['off'] + tam]
        # --- A en la RAM entera
        mem[:] = base_ram
        _bloques(meta, d, b0, mem, 'A')
        mgr = u32(mem, 0x49CA70); pic = mgr + 0x28D0; w = u32(mem, 0x49CBC0); w1 = w + 0x260
        estadoA = bytes(mem)
        cA = _campos(mem, pic, w1, True)
        # B y C de PCSX2 (sobre A)
        mB = bytearray(estadoA); _bloques(meta, d, b0, mB, 'B'); cB = _campos(mB, pic, w1, True)
        mC = bytearray(mB); _bloques(meta, d, b0, mC, 'C'); cC = _campos(mC, pic, w1, True)
        tot['parejas'] += 1
        tot['estado_%X' % cA['cbc']] += 1
        tot['tick_cambia_estado'] += cA['cbc'] != cB['cbc']
        tot['tick_crea_tarea'] += cB.get('t_tiempo', 1) == 0 and cB['tarea'] == 1
        # 1) tick original en ee.py con la RAM entera
        c = B.nueva_cpu(mem, nom)
        c.hooks[HITS_SON] = lambda c: None
        try:
            c.call(0x363E70, pic)
            dif = [o for o in list(range(0, 0x20D0, 4)) if mem[pic + o:pic + o + 4] != mB[pic + o:pic + o + 4]]
            dif += [0x10000 + o for o in range(0, 0x260, 4) if mem[w1 + o:w1 + o + 4] != mB[w1 + o:w1 + o + 4]]
            t = u32(mB, w1 + 0xB8)
            if t and mem[t:t + 0x100] != mB[t:t + 0x100]:
                dif.append(0x20000)
            if u32(mem, B.SEMILLA) != u32(mB, B.SEMILLA):
                dif.append(0x30000)
            ok = not dif
            tot['ram_tick_iguales'] += ok
            for o in dif[:4]:
                campos[('ram', hex(o))] += 1
            if not ok and len(malas) < ver:
                malas.append(('ram', k, cA['cbc'], [hex(o) for o in dif[:8]]))
        except Exception as ex:   # noqa: BLE001
            tot['ram_excepcion'] += 1
            malas.append(('ram', k, str(ex)[:80]))
        # 2) espejo minimo del juego desde A: estado de Piccolo, su work y la tarea pasados al espejo
        esp.preparar()
        m2, A_ = esp.mem, esp.A
        PUNT = {0xB64, 0xB78, 0xB7C, 0xBB8, 0xBC0, 0x1380, 0x1384}
        for o in range(0, 0x20D0, 4):
            if o not in PUNT and not (0x640 <= o < 0x77C and False):
                m2[PIC + o:PIC + o + 4] = estadoA[pic + o:pic + o + 4]
        m2[W0:W0 + 0x4C0] = estadoA[w:w + 0x4C0]
        t = u32(estadoA, w1 + 0xB8)
        if t:
            m2[TAREA:TAREA + 0x100] = estadoA[t:t + 0x100]
            struct.pack_into('<I', m2, W1 + 0xB8, TAREA)
        jug = mgr + 0x800
        for o in (0x40, 0x44, 0x48, 0x4C, 0x640, 0x644, 0x648, 0x64C, 0xCBC):
            m2[JUG + o:JUG + o + 4] = estadoA[jug + o:jug + o + 4]
        m2[JUG + 0x2060:JUG + 0x2064] = estadoA[jug + 0x2060:jug + 0x2064]
        mgw = u32(estadoA, 0x49CBB8); ctxm = u32(estadoA, 0x49CDC0); ctxd = u32(estadoA, 0x49CBA4)
        m2[MGW + 0x1E8:MGW + 0x260] = estadoA[mgw + 0x1E8:mgw + 0x260]
        m2[CTXM + 0x75C:CTXM + 0x760] = estadoA[ctxm + 0x75C:ctxm + 0x760]
        m2[CTXD + 0xC:CTXD + 0xE] = estadoA[ctxd + 0xC:ctxd + 0xE]
        struct.pack_into('<I', m2, A_.SEMILLA, u32(estadoA, B.SEMILLA))
        esp.ev, esp.col, esp.fallo, esp.rayos = [], [], False, []
        if esp.R:   # hueso vivo en A ~ el clip de A en su cuadro (Animations2 del campo anterior; sin mezcla pendiente)
            esp.clip_raiz = esp.R.clip(cA['clip'])
            esp.vivo = esp.R.muestra(esp.clip_raiz, b2f(u32(estadoA, pic + 0xC68))) if usar_raiz else None
        try:
            esp.cpu.call(A_.TICK, PIC)
            cM = _campos(m2, PIC, W1, True)
            dif = sorted(x for x in cB if x != 'tarea' and cM.get(x) != cB.get(x) and not (x.startswith('t_') and not cB['tarea']))
            if cM['tarea'] != cB['tarea']:
                dif.append('tarea')
            if u32(m2, A_.SEMILLA) != u32(mB, B.SEMILLA):
                dif.append('semilla')
            ok = not dif
            tot['espejo_tick_iguales'] += ok
            sin_ulp = [x for x in dif if x not in ('w4', 'c70')]   # yaw (1 ulp) y la cuenta atras del clip (1 ulp)
            tot['espejo_tick_decisiones'] += not sin_ulp
            for x in dif:
                campos[('espejo_tick', x)] += 1
            if not ok and len(malas) < 2 * ver:
                malas.append(('espejo_tick', k, hex(cA['cbc']), {x: (cB.get(x), cM.get(x)) for x in dif[:6]}))
            # 3) B -> C: Update + tarea + entrada desde el estado B de PCSX2 (aisla lo que pasa entre ticks)
            mBB = bytes(mB)
            for o in range(0, 0x20D0, 4):
                if o not in PUNT:
                    m2[PIC + o:PIC + o + 4] = mBB[pic + o:pic + o + 4]
            m2[W0:W0 + 0x4C0] = mBB[w:w + 0x4C0]
            tb = u32(mBB, w1 + 0xB8)
            if tb:
                m2[TAREA:TAREA + 0x100] = mBB[tb:tb + 0x100]
                struct.pack_into('<I', m2, W1 + 0xB8, TAREA)
            m2[MGW + 0x1E8:MGW + 0x260] = mBB[mgw + 0x1E8:mgw + 0x260]
            G = GRID
            m2[G:G + 0xB00] = bytes(0xB00)
            for a_, v_ in ((A_.P_GRID, G), (G + 0x630, 7), (G + 0xAD4, B.f2b(3000.0)), (G + 0xAD8, B.f2b(3000.0)),
                           (G + 0xADC, 10), (G + 0xAE0, 10), (G + 0xAF0, B.f2b(-1350.0)), (G + 0xAF8, B.f2b(-1350.0))):
                struct.pack_into('<I', m2, a_, v_)
            if esp.R:
                esp.clip_raiz = esp.R.clip(cB['clip'])
            esp.cpu.call(A_.UPD, PIC)
            if u32(m2, W1 + 0xB8):
                esp.cpu.call(u32(m2, TAREA + 0x1C) or A_.TRAY, u32(m2, W1 + 0xB8))
            esp.fin_campo()
            paso = b2f(A_.PASO_BITS)   # pegamento del juego antes de la entrada del cuadro siguiente (= Tick7DD)
            for o in (0x1A0, 0x1A4):
                struct.pack_into('<I', m2, W1 + o, B.f2b(f32(b2f(u32(m2, W1 + o)) + paso)))
            esp.cpu.call(A_.ENTRADA, PIC)
            cM2 = _campos(m2, PIC, W1, False)
            difc = sorted(x for x in cM2 if x not in ('tarea',) and not x.startswith('t_') and cM2.get(x) != cC.get(x))
            tot['espejo_entre_iguales'] += not difc
            for x in difc:
                campos[('espejo_entre', x)] += 1
            dp = max(abs(b2f(cM2[x]) - b2f(cC[x])) for x in ('x', 'y', 'z'))
            tot['espejo_entre_pos_max_e3'] = max(tot['espejo_entre_pos_max_e3'], int(dp * 1000))
            tot['espejo_entre_pos_ok'] += dp < 0.01
            tot['entre_pos_%s_e%X' % ('ok' if dp < 0.01 else 'mal', cB['cbc'])] += 1
        except Exception as ex:   # noqa: BLE001
            tot['espejo_excepcion'] += 1
            malas.append(('espejo', k, str(ex)[:80]))
    pr.stdin.close()
    return tot, malas, campos


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('que', choices=['dorado', 'puentes', 'juego', 'tick'])
    ap.add_argument('--cap', default=str(CAP))
    ap.add_argument('--p2s', default=str(P2S))
    ap.add_argument('--wcf', default=str(WCF))
    ap.add_argument('--log')
    ap.add_argument('--pcsx2', action='store_true', help='reglas FPU de PCSX2 (sin mul), las del dorado')
    ap.add_argument('--sin-raiz', action='store_true', help='tick: sin el hueso vivo (el espejo de antes)')
    a = ap.parse_args()
    if a.pcsx2:
        ee.FPU_HW = set(ee.FPU_HW) - {'mul'}
    if a.que == 'dorado':
        tot, malas, campos = prueba_dorado(Path(a.cap), Path(a.p2s))
        ok = tot['muestras'] and tot['iguales'] == tot['muestras']
    elif a.que == 'tick':
        tot, malas, campos = prueba_tick(Path(a.cap) if a.cap != str(CAP) else CAP_TICK, Path(a.p2s),
                                         usar_raiz=not a.sin_raiz)
        ok = tot['parejas'] and tot['ram_tick_iguales'] == tot['parejas']
    elif a.que == 'puentes':
        tot, malas, campos = prueba_puentes(Path(a.cap), Path(a.p2s), Path(a.wcf))
        ok = tot['muestras'] and tot['v3'] == tot['muestras']
    else:
        tot, malas, campos = prueba_juego(Path(a.log))
        ok = tot['llamadas'] and tot['iguales'] == tot['llamadas']
    print(json.dumps(tot))
    for m in malas:
        print('DISTINTA', m)
    if campos:
        print('campos distintos', campos.most_common(12))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()

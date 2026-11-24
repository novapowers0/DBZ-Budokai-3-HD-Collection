# SPDX-License-Identifier: MIT
"""IW-F05b: banco CONTINUO de 7DE (Stop Cell) EU, sin teclas, de Ready a ApplyResult. Sin juego ni emulador.

Original: un solo estado EE; por campo, la tarea MgShoot (prioridad 0x4100, s_MSN016_UPDATE 0x30CD08) ejecuta su
estado del ELF (Ready/Aim/GameOver/WaitResult/ApplyResult, con CheckTimeUp, MoveAimByPad, ResetScene, Timer_*,
UiTimer_* y la libreria soft-double originales) y despues la tarea del reloj (0x54FF, MgShoot_UpdateUiTimerBeeps
0x30A120 -> UiTimer_Update 0x31EB60). Las tareas de Cell (0x5100/0x5300, s_PLY_PRE_EXEC) van ENTRE las dos: aqui su
altura es una entrada (frontera declarada: la maquina de poses de Cell y AllAnimations no se ejecutan). Servicios de
UI/voz/sonido/poses/camara se registran sin efectos. Camera_IsTimerElapsed (fin del clip) devuelve 1 a la primera.
Port: modelo Python de Cuadro15 (src/iw_mini_disparo.cpp, rama sin teclas) con float32 a lo x86.
Uso: python tools/iw_mips/disparo_continuo.py [--json salida.json]
"""
import argparse
import json
import struct
from pathlib import Path

import disparo_banco as banco
import ee

GP, INIT, PASO = 0x4A3DF0, 0x30D150, 0x49C7DC
CTXP, MGRP, CAMP, TIMP = 0x49CDBC, 0x49CA70, 0x49CD6C, 0x49CDE4
READY, AIM, GAMEOVER, WAIT, APPLY = 0x30B110, 0x30ADE0, 0x30A3D0, 0x30A380, 0x30A200
RELOJ, UITIMER_INIT = 0x30A120, 0x31ED40
CTX, TASK, TIMER, CONFIG, MGR, CAM = 0x1000000, 0x1006000, 0x1008000, 0x1009000, 0x1100000, 0x1400000
ALTURA = MGR + 0x2914                       # y de Cell (actor 0x28D0 + 0x44)
TIEMPO = (40, 60, 80, 100, 120, 160)        # tabla 0x5D93A0 (+0), Scene_InitDefaultParams desde 0x47E010
EJECUTADAS = ((0x100000, 0x120000),         # soft-double y libreria C
              (0x3099E0, 0x30D600),         # MgShoot_* completo
              (0x31EAE0, 0x31F000),         # UiTimer_*
              (0x2ED7D0, 0x2ED8A0), (0x2ED9E0, 0x2EDA40), (0x127000, 0x127100),   # camara/mira y suma vectorial
              (0x2EF990, 0x2EF9E0), (0x31E050, 0x31E070))   # Timer_TickSlot/SetSlot, Store5d94c0Pair
SERVICIOS = {0x1FC7C0: 'voz', 0x31E1F0: 'pantalla', 0x31E070: 'pose', 0x240740: 'sonido', 0x30D610: 'evento',
             0x2ED8F0: 'uianim', 0x35EB60: 'audio', 0x2F0A30: 'objeto', 0x28A020: 'premio', 0x28A300: 'flag4'}


def bits(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


class Ejecutadas:
    def __contains__(self, a):
        return any(lo <= a < hi for lo, hi in EJECUTADAS)


class Original:
    def __init__(self, dif, altura):
        mem, digest = banco.cargar_elf(banco.ELF_EU)
        if digest != banco.ELF_HASH['eu']:
            raise ValueError('ELF EU distinto del verificado')
        self.c = c = ee.Ee(mem, GP)
        c.call(INIT, limite=100000)
        if c.io or c.sin_puente:
            raise RuntimeError('init con E/S o llamadas no cubiertas')
        self.altura, self.eventos, self.campo = altura, [], 0
        for a, rol in SERVICIOS.items():
            c.hooks[a] = self._servicio(rol)
        c.hooks[0x2ED8A0] = self._ret(1, 'fin_clip')        # Camera_IsTimerElapsed (frontera)
        c.hooks[0x28AA40] = self._ret(CONFIG, None)         # GameState_GetConfigBlock
        c.whitelist = Ejecutadas()
        for p, v in ((CTXP, CTX), (MGRP, MGR), (CAMP, CAM), (TIMP, TIMER), (CTX, TASK)):
            c.w32(p, v)
        c.w32(PASO, bits(1.0))
        c.w32(CTX + 4, bits(1.0)); c.w32(CTX + 8, 0); c.w32(CTX + 0x1C, dif)
        for i, v in enumerate((0.0, 400.0, 200.0, 1.0, 0.0, 0.0, 0.0, 1.0)):
            c.w32(CAM + 0x80 + 4 * i, bits(v))
        c.call(UITIMER_INIT, TIEMPO[dif])                   # SetupRound: UiTimer_Init(tabla + 0)
        c.w32(TASK + 0x1C, READY)                           # Intro: Timer_SetSlot(100) y Ready
        c.w32(TASK + 0x30, bits(100.0))

    def _servicio(self, rol):
        def hook(c):
            self.eventos.append((self.campo, rol, c.r[4] & ee.M32, c.r[5] & ee.M32))
            c.r[2] = 0
        return hook

    def _ret(self, v, rol):
        def hook(c):
            if rol:
                self.eventos.append((self.campo, rol, 0, 0))
            c.r[2] = v
        return hook

    def correr(self, limite=20000):
        c, estados = self.c, []
        while self.campo < limite:
            self.campo += 1
            c.w32(ALTURA, bits(self.altura(self.campo - 1)))   # Cell se movio en el campo anterior (0x5300 > 0x4100)
            fn = c.r32(TASK + 0x1C)
            estados.append(fn)
            c.call(fn, TASK, limite=2_000_000)
            if fn == APPLY:
                break
            c.call(RELOJ, limite=2_000_000)
        if c.io:
            raise RuntimeError(f'E/S fuera de memoria: {dict(c.io)}')
        return self.resumen(estados)

    def resumen(self, estados):
        c = self.c
        def primero(fn):
            return estados.index(fn) + 1 if fn in estados else None
        r = {'fin_ready': primero(AIM) - 1, 'entrada_gameover': primero(GAMEOVER) - 1,
             'fin_gameover': primero(WAIT) - 1, 'apply': primero(APPLY),
             'ratio': ee.fv(c.r32(CTX + 0x14)), 'restante': ee.fv(c.r32(CTX + 0x10)),
             'rango': c.r32(CONFIG + 0x48) if c.r32(CONFIG + 0x48) < 0x80000000 else -1,
             'zeni': c.r32(CONFIG + 0x4C) & 0xFFFF,
             'sin_puente': sorted(hex(a) for a in c.sin_puente)}
        ev = self.eventos
        r['voz_entrada'] = [a for n, rol, a, _ in ev if rol == 'voz' and n == r['entrada_gameover']]
        r['pantalla'] = [a for n, rol, a, b in ev if rol == 'pantalla' and n == r['entrada_gameover']]
        r['pose_ten'] = [(n, b) for n, rol, a, b in ev if rol == 'pose' and a == MGR + 0x800 and b in (8, 9)]
        r['evento10'] = [n for n, rol, a, _ in ev if rol == 'evento' and a == 10]
        r['pitidos'] = [n for n, rol, a, b in ev if rol == 'sonido' and (a, b) == (1, 0x29)]
        return r


def port(dif, altura, fin_ready, fin_dur=140, continuo=False):
    """Cuadro15 sin teclas (iw_mini_disparo.cpp, Final15, PasoFinal con t_ == 140).

    continuo=True: con el parche IW-F05b_continuo.patch (dbz3_iw_disparo_continuo_original): decide con la altura y el
    reloj del campo anterior, reloj en tics enteros (T*60 - 1 al acabar Ready) y GameOver con t_ = -1 (llamada 141).
    """
    if continuo:
        # RESCATE IW-F07: Pitidos15 (MgShoot_UpdateUiTimerBeeps EU 0x30A120) tras el descuento de cada campo, y en
        # GameOver con el reloj parado: sonido (banco 1, 0x29) si quedan 60/120/180/240/300 tics.
        reloj, k, pitidos = TIEMPO[dif] * 60 - 1, fin_ready, []
        while True:
            k += 1
            p = f32(altura(k - 1) / 400.0)       # cy_ del campo anterior (CellCuadro va al final del bloque)
            if reloj <= 0:
                ratio, causa = p, 'tiempo'
                break
            if p >= 1.0:
                ratio, causa = -1.0, 'altura'
                break
            reloj -= 1
            if reloj in (60, 120, 180, 240, 300):
                pitidos.append(k)
        if reloj in (60, 120, 180, 240, 300):   # GameOver: el reloj ya no baja (kDerribo, k .. fin_gameover)
            pitidos += list(range(k, k + 2 + fin_dur))
        exito = 0 <= ratio < 1
        return {'fin_ready': fin_ready, 'entrada_gameover': k, 'ratio': ratio, 'causa': causa,
                'voz_entrada': [5 if exito else 0xD], 'pantalla': [7 if exito else 8],
                'pose_ten': [(k + 141, 8 if exito else 9)], 'evento10': [k + 141], 'fin_gameover': k + 1 + fin_dur,
                'pitidos': pitidos}
    resta, d1, k = f32(TIEMPO[dif]), f32(1.0 / 60.0), fin_ready   # fase_ = kJuego en el campo del fin de Ready
    while True:
        k += 1
        resta = f32(resta - d1)                  # 1448: restante_ -= 1/60 (antes de CellCuadro y de mirar)
        p = f32(altura(k) / 400.0)               # 1449-1451: CellCuadro y despues p = cy_ / kAltura15
        if resta <= 0:
            ratio, causa = p, 'tiempo'
            break
        if p >= 1.0:
            ratio, causa = -1.0, 'altura'
            break
        if k > 20000:
            raise RuntimeError('sin fin')
    exito = 0 <= ratio < 1
    r = {'fin_ready': fin_ready, 'entrada_gameover': k, 'ratio': ratio, 'causa': causa,
         'voz_entrada': [5 if exito else 0xD], 'pantalla': [7 if exito else 8],
         'pose_ten': [(k + 140, 8 if exito else 9)], 'evento10': [k + 140], 'fin_gameover': k + fin_dur,
         'pitidos': []}
    return r


def premio_port(dif, rango):
    # 2403-2418: rango del clasificador ORIGINAL (g_rango); puntos = kBase15[rango] * kMult[dif] redondeado a 100
    if rango < 0:
        return 100
    raw = int(f32(f32([3600, 4200, 4800, 5400, 6000][rango]) * f32([0.6, 0.8, 1.0, 1.3, 1.6, 2.0][dif])))
    return (raw // 100 + (raw % 100 >= 50)) * 100


ESCENARIOS = {
    # Cell contenido (p = 0,025): sale por tiempo con exito.
    'contenido': lambda n: 10.0,
    # Sin teclas (referencia G02_0x7DE_P02: FAILED con el reloj aun en 78): Cell sube 3,5 por campo desde el 160.
    'sin_teclas': lambda n: max(0.0, 3.5 * (n - 160)),
}
CAMPOS = ('zeni', 'fin_ready', 'entrada_gameover', 'ratio', 'voz_entrada', 'pantalla', 'pose_ten', 'evento10', 'pitidos')


def comprobar(continuo=False):
    filas, difs = [], 0
    for dif in range(6):
        for nombre, alt in ESCENARIOS.items():
            o = Original(dif, alt).correr()
            p = port(dif, alt, o['fin_ready'], continuo=continuo)
            p['zeni'] = premio_port(dif, o['rango'])
            d = {k: (o[k], p[k]) for k in CAMPOS if o[k] != p[k]}
            difs += len(d)
            filas.append({'dificultad': dif, 'escenario': nombre, 'original': o, 'port': p, 'diferencias': d})
            print(f'dif {dif} {nombre:10s}: GameOver orig {o["entrada_gameover"]} port {p["entrada_gameover"]}; '
                  f'{len(d)} DIF {sorted(d)}; sin puente {o["sin_puente"]}')
    print(f'TOTAL diferencias: {difs}')
    return {'diferencias': difs, 'filas': filas}


# ---------------------------------------------------------------------------------------------------------------
# IW-F06: 7D7 (MSN009, Krilin lanza Kienzan a Freezer) y 7D8 (MSN010, rayos a Piccolo), EU, sin teclas.
# Mismo esquema de tareas que el 7DE (s_MSN009_UPDATE 0x34B3F0 / s_MSN010_UPDATE 0x327590): estado a 0x4100 y la tarea
# del reloj a 0x54FF (Viewer_UpdateFrame 0x348AD0 / Viewer_Update 0x3249C0 -> UiTimer_Update). Arranca en el campo en
# que la camara de la entrada acaba (frontera: Camera_IsTimerElapsed = 1), es decir, en ShootIntroReady (7D7) o en la
# fase 1 de Viewer_StateCountdown (7D8). Las llamadas fuera de la lista ejecutada se registran con sus argumentos.
VISOR = {
    '7d7': dict(init=0x34B800, ctxp=0x49CE78, dif=0x14, inicio=0x34A1D0, reloj=0x348AD0,
                juego=(0x349CE0, 0x349250, 0x348EF0, 0x348B40, 0x3499C0), fin=0x3498B0, espera=0x349850,
                resultado=0x3496B0, familia=(0x3481E0, 0x34BD90), pose=0x35E950, tiempo=0x5D9B44, paso=0x18,
                hooks={0x34BDA0: 'reaccion'}),
    '7d8': dict(init=0x3279F0, ctxp=0x49CDF0, dif=0x28, inicio=0x325AB0, reloj=0x3249C0,
                juego=(0x325430, 0x325180), fin=0x324E30, espera=0x324DE0, resultado=0x324C50,
                familia=(0x324490, 0x327F80), pose=None, tiempo=0x5D98B4, paso=0x10,
                hooks={0x327F90: 'sfx'}),
}


class EeTraza(ee.Ee):
    """ee.Ee que apunta cada llamada sin puente: (campo, destino, a0..a3, f12)."""
    def entrar(s, t):
        if t not in s.hooks and s.whitelist is not None and t not in s.whitelist:
            s.traza.append((s.campo, t, s.r[4] & ee.M32, s.r[5] & ee.M32, s.r[6] & ee.M32, s.r[7] & ee.M32,
                            ee.fv(s.f[12])))
        return super().entrar(t)


class OriginalVisor:
    def __init__(self, clave, dif):
        p = self.p = VISOR[clave]
        mem, digest = banco.cargar_elf(banco.ELF_EU)
        if digest != banco.ELF_HASH['eu']:
            raise ValueError('ELF EU distinto del verificado')
        self.c = c = EeTraza(mem, GP)
        c.traza, c.campo = [], 0
        c.call(p['init'], limite=200000)
        self.eventos, self.campo = [], 0
        for a, rol in {**SERVICIOS, **p['hooks']}.items():
            c.hooks[a] = self._servicio(rol)
        c.hooks[0x2ED8A0] = lambda cc: cc.r.__setitem__(2, 1)      # Camera_IsTimerElapsed (frontera)
        c.hooks[0x28AA40] = lambda cc: cc.r.__setitem__(2, CONFIG)  # GameState_GetConfigBlock
        lista = list(EJECUTADAS) + [p['familia']]
        lista.remove((0x3099E0, 0x30D600))                          # MgShoot no
        c.whitelist = type('L', (), {'__contains__': lambda _, a: any(lo <= a < hi for lo, hi in lista)})()
        for q, v in ((p['ctxp'], CTX), (MGRP, MGR), (CAMP, CAM), (TIMP, TIMER), (CTX, TASK)):
            c.w32(q, v)
        c.w32(PASO, bits(1.0))
        c.w32(CTX + p['dif'], dif)
        self.tiempo = c.rf(p['tiempo'] + dif * p['paso'])
        c.w32(TASK + 0x1C, p['inicio'])
        c.w32(TASK + 0x30, 1)                                       # 7D8: fase 1 del Countdown (el 7D7 no la mira)

    def _servicio(self, rol):
        def hook(c):
            self.eventos.append((self.campo, rol, c.r[4] & ee.M32, c.r[5] & ee.M32, c.r[6] & ee.M32))
            c.r[2] = 0
        return hook

    def correr(self, limite=20000):
        c, p, estados = self.c, self.p, []
        while self.campo < limite:
            self.campo += 1
            c.campo = self.campo
            fn = c.r32(TASK + 0x1C)
            estados.append(fn)
            c.call(fn, TASK, limite=2_000_000)
            if fn == p['resultado']:
                break
            c.call(p['reloj'], limite=2_000_000)
        if c.io:
            raise RuntimeError(f'E/S fuera de memoria: {dict(c.io)}')
        return self.resumen(estados)

    def resumen(self, estados):
        c, p, ev = self.c, self.p, self.eventos
        def primero(fns):
            fns = fns if isinstance(fns, tuple) else (fns,)
            return next((i + 1 for i, f in enumerate(estados) if f in fns), None)
        juego = primero(p['juego'])
        fin = primero(p['fin'])
        espera = primero(p['espera'])
        voz = lambda n: [a for k, rol, a, _, _ in ev if rol == 'voz' and k == n]
        r = {'tiempo': self.tiempo, 'inicio_juego': juego - 1, 'entrada_fin': fin - 1, 'fin_espera': espera - 1,
             'resultado': primero(p['resultado']),
             'voz_ready': voz(1), 'voz_juego': voz(juego - 1), 'voz_fin': voz(fin - 1),
             'clip_fin': [b for k, rol, a, b, _ in ev if rol == 'uianim' and k == espera - 1],   # UiAnim_Start(f12, a0, CLIP, ..)
             'rango': c.r32(CONFIG + 0x48) if c.r32(CONFIG + 0x48) < 0x80000000 else -1,
             'zeni': c.r32(CONFIG + 0x4C) & 0xFFFF,
             'sin_puente': sorted({hex(t) for _, t, *_ in c.traza})}
        if p['pose']:
            r['pose_fin'] = [(k, b) for k, t, a, b, *_ in c.traza if t == p['pose'] and a == MGR + 0x800 and
                             k == espera - 1]
        else:   # 7D8: Viewer_EnterPoseState (la direccion sale de la traza)
            r['pose_fin'] = [(k, hex(t), b) for k, t, a, b, *_ in c.traza if a == MGR + 0x800 and k == espera - 1]
        # Viewer_PlayEventSfx (7D8) / Viewer_PlayCharReaction (7D7): voz de reaccion del actor + boca
        r['sfx'] = [(k, a) for k, rol, a, *_ in ev if rol in ('sfx', 'reaccion')]
        return r


KLIMITE_PORT = {'7d7': (180,) * 6, '7d8': (180, 160, 140, 120, 110, 90)}   # restante_ de Empezar (8: 180; 9: kLimite)
KTIROS_PORT = (12, 12, 11, 10, 9, 8)
VOCES = {'7d7': (0xE, 0xF, 0x19), '7d8': (0, 1, 0xC)}
CLIP_MAL = {'7d7': 952, '7d8': 1002}            # d.fin_mal del port (descriptor 0x49A690 / 0x499908)
POSE_MAL = 7                                     # Krilin / Goku: Char_EnterPoseState(+0x800, 7) en el original


def port_visor(clave, dif, continuo=False):
    """CuadroDisco (8) / Cuadro (9) sin teclas, desde el primer campo de PausaListo (= salida de la camara de entrada).

    continuo=True: parche IW-F06_visor.patch (dbz3_iw_disparo_continuo_original): Ready de 101 llamadas, reloj en tics
    enteros mirado con el valor del campo anterior, espera final de 141 llamadas (t_ = -1) y rango -1 al acabar por
    tiempo (7D8: +0x10 = 0) o sin siete aciertos (7D7: ce78+8 = -1).
    """
    v0, v1, v2 = VOCES[clave]
    T = KLIMITE_PORT[clave][dif]
    ready = 101 if continuo else 100
    inicio = 1 + ready                       # PausaListo: true en las llamadas 1..ready; la siguiente pasa a kJuego
    k = inicio
    if continuo:
        reloj = T * 60 - 1                   # el campo de salida de AimStart/Ready ya descuenta un tic
        while True:
            k += 1
            if reloj <= 0:
                break
            reloj -= 1
    else:
        resta, d1 = f32(T), f32(1.0 / 60.0)
        while True:
            k += 1
            resta = f32(resta - d1)          # restante_ -= 1/60 al principio de kJuego
            if resta <= 0:
                break
    espera = 141 if continuo else 140        # PasoFinal: Cine (pose y clip) con t_ == 140; t_ = -1 con el parche
    if continuo or (clave == '7d7'):         # 7D7 con dbz3_iw_disparo_original: g_rango(-1) sin siete aciertos
        rango, zeni = -1, 100
    else:                                    # 7D8: g_rango(jugado = T) -> banda 0 (999 >= T > 75)
        raw = int(f32(f32(kBASE_7D8[0]) * f32([0.6, 0.8, 1.0, 1.3, 1.6, 2.0][dif])))
        rango, zeni = 0, (raw // 100 + (raw % 100 >= 50)) * 100
    return {'tiempo': float(T), 'inicio_juego': inicio, 'entrada_fin': k, 'fin_espera': k + espera,
            'voz_ready': [v0], 'voz_juego': [v1], 'voz_fin': [v2], 'clip_fin': [CLIP_MAL[clave]],
            'pose_fin': [POSE_MAL], 'rango': rango, 'zeni': zeni,
            # RESCATE IW-F07: VozKrilinFin en PasoFinal (t_ == 140) = Viewer_PlayCharReaction(8) EU 0x3499A0. El 7D8
            # (Viewer_PlayEventSfx 3 y 10, actor +0x800) sigue sin puerto: perfil ADX del tirador sin medir.
            'sfx': ([(k + espera, 8)] if clave == '7d7' else [(inicio, 3), (k + espera, 10)]) if continuo else []}   # IWFIN: 7D8 VozFreezer7D8


kBASE_7D8 = (3000, 5000, 6000, 8000, 10000)
CAMPOS_VISOR = ('tiempo', 'inicio_juego', 'entrada_fin', 'fin_espera', 'voz_ready', 'voz_juego', 'voz_fin',
                'clip_fin', 'pose_fin', 'rango', 'zeni', 'sfx')


def comprobar_visor(clave, continuo=False):
    filas, difs = [], 0
    for dif in range(6):
        o = OriginalVisor(clave, dif).correr()
        o = dict(o, pose_fin=[x[-1] for x in o['pose_fin'] if x[-1] != 8])   # 8 = Viewer_SetupShootUi(1), no es el final
        p = port_visor(clave, dif, continuo)
        d = {k: (o[k], p[k]) for k in CAMPOS_VISOR if o[k] != p[k]}
        difs += len(d)
        filas.append({'dificultad': dif, 'original': o, 'port': p, 'diferencias': d})
        print(f'{clave} dif {dif}: fin orig {o["entrada_fin"]} port {p["entrada_fin"]}; {len(d)} DIF {d}')
    print(f'{clave} sin puente: {filas[0]["original"]["sin_puente"]}')
    print(f'TOTAL diferencias {clave}: {difs}')
    return {'juego': clave, 'diferencias': difs, 'filas': filas}


def tabla_clips():
    """Clip de esquiva (+0xB60) que pone el ELF EU por direccion y potencia, frente a la formula del port.

    7D7 Freezer: Char_EnterPoseState2Transform 0x35D150, dir = 0x5D9D78, potencia = 0x5D9D80 (port: kEsquivaFz[dir +
    4*(fz_t_ > 35)], iw_mini_disparo.cpp). 7D8 Piccolo: Viewer_PoseEnterState2 0x32D2C0, dir = 0x5D9958, potencia =
    0x5D9960 (port: 0x3F2 + dir + 4*(tiempo9960 > 35), iw_piccolo_estados.h).
    """
    mem, _ = banco.cargar_elf(banco.ELF_EU)
    filas, difs = [], 0
    for nombre, fn, dirp, potp, base in (('7d7', 0x35D150, 0x5D9D78, 0x5D9D80, 0x38E),
                                         ('7d8', 0x32D2C0, 0x5D9958, 0x5D9960, 0x3F2)):
        c = ee.Ee(bytearray(mem), GP)
        c.whitelist = set()                              # solo el cuerpo; BonesMovement y sonido sin efecto
        for d in range(4):
            for pot in (0.0, 35.0, f32(35.000004), 40.0, 119.0):
                c.w32(dirp, d); c.w32(potp, bits(pot)); c.w32(MGR + 0xB60, 0)
                c.call(fn, MGR, limite=10000)
                o = c.r32(MGR + 0xB60)
                p = base + d + (4 if pot > 35.0 else 0)
                difs += o != p
                filas.append({'juego': nombre, 'dir': d, 'potencia': pot, 'original': hex(o), 'port': hex(p)})
        print(nombre, sorted({(f['dir'], f['potencia'] > 35, f['original']) for f in filas if f['juego'] == nombre}))
    # Poses de cierre e impacto (Char_EnterPoseState 0x35E950 / Viewer_EnterPoseState 0x32E7E0) frente al codigo IW que
    # el port pide por su alias (anim_iw.cpp kExtras: PasoFinal 0xFA4/0xFA5, 0xFAE/0xFAF; impacto 0xFAB/0xFB2).
    alias = {0x3D5: 0xFA4, 0x3D6: 0xFA5, 0x439: 0xFAE, 0x43A: 0xFAF, 0x3AC: 0xFAB, 0x410: 0xFB2}
    for nombre, fn, actor, pose, port_alias in (('7d7 Krilin bien', 0x35E950, 0x800, 6, 0xFA4),
                                                ('7d7 Krilin mal', 0x35E950, 0x800, 7, 0xFA5),
                                                ('7d7 Freezer golpe', 0x35E950, 0x28D0, 4, 0xFAB),
                                                ('7d8 Goku bien', 0x32E7E0, 0x800, 6, 0xFAE),
                                                ('7d8 Goku mal', 0x32E7E0, 0x800, 7, 0xFAF),
                                                ('7d8 Piccolo derribo', 0x32E7E0, 0x28D0, 4, 0xFB2)):
        c = ee.Ee(bytearray(mem), GP)
        c.whitelist = set()
        c.w32(MGRP, MGR)
        c.call(fn, MGR + actor, pose, limite=20000)
        o = c.r32(MGR + actor + 0xB60)
        difs += alias.get(o) != port_alias
        filas.append({'juego': nombre, 'pose': pose, 'original': hex(o), 'port_alias': hex(port_alias)})
        print(nombre, hex(o), hex(port_alias), 'OK' if alias.get(o) == port_alias else 'DIF')
    print(f'TOTAL diferencias tabla de clips: {difs}')
    return {'diferencias': difs, 'filas': filas}


def main_visor(clave, continuo=False, json_path=None):
    if clave == 'clips':
        r = tabla_clips()
        if json_path:
            json_path.write_text(json.dumps(r, indent=1), encoding='utf-8')
        return
    r = comprobar_visor(clave, continuo)
    if json_path:
        json_path.write_text(json.dumps(r, indent=1, default=str), encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--json', type=Path)
    ap.add_argument('--continuo', action='store_true', help='port con el parche IW-F05b (cvar continuo encendida)')
    ap.add_argument('--juego', choices=('7de', '7d7', '7d8', 'clips'), default='7de')
    a = ap.parse_args()
    if a.juego != '7de':
        return main_visor(a.juego, a.continuo, a.json)
    r = comprobar(a.continuo)
    if a.json:
        a.json.write_text(json.dumps(r, indent=1, default=str), encoding='utf-8')


if __name__ == '__main__':
    main()

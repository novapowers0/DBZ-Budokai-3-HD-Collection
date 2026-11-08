# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (8/11): banco en ee.py del mapa de Dragon Mission (EU SLES_553.47) sin juego.
Corre las funciones ORIGINALES del mapa sobre una imagen de memoria: aparicion de casillas (Story_MapObjectsAppearTick
2FCCB0, que llama a Story_MapObjectVisibility 2FD3A0), seleccion (Story_PickObjectAtCursor 2F8200) y el manejador de
mandos (GeneralMapScripts_MissionDots... 2FE270: palanca, confirmar, L1/R1). Las funciones de sprites/sonido/menus son
mudas (se anotan como eventos). Uso como biblioteca (mapa_banco.Mapa) o `python mapa_banco.py [p2s]` (autoprueba)."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import bubbles as B  # noqa: E402
import ee  # noqa: E402

P2S = Path('D:/DBZ3HD/_pcsx2_pool/3/sstates/SLES-55347 (335A5A1F).04.p2s')   # mapa de Dragon Mission, saga 0
PUL = ('MiscellaneousGameProperties', 'Sprite_', 'MenuFade', 'MenuPopUp', 'MenuShowUp', 'RAMGraphicVisuals', 'HelpMenu',
       'SkillEditIcon', 'Transition_', 'CopyRecord', 'LoadWaitExit', 'MiniGokuAnim', 'Story_SetSprite',
       'LoadWaitPlayer', 'LoadWaitMapIcons')
# direcciones EU
APPEAR, ICONS, MANDOS, CAMARA = 0x2FCCB0, 0x2FD0F0, 0x2FE270, 0x2F9280   # CAMARA = Story_MapCameraUpdate: topes, matriz y Story_PickObjectAtCursor
G_M, G_CTX, G_PASO = 0x49CDAC, 0x49CDDC, 0x49C7DC
PAD = 0x4ADECC   # +0 u16 mantenido, +2 u16 recien pulsado, +0xE/+0xF palanca izq. (x, y) con signo


class Mapa:
    def __init__(s, p2s=P2S):
        s.base, _ = B.cargar(B.ELF, Path(p2s))
        s.nom = B.leer_simbolos()
        s.c = ee.Ee(s.base, B.GP, entradas=s.nom.keys(), nombres=s.nom)
        s.ev = []
        for t, n in s.nom.items():
            if n.startswith(PUL):
                s.c.hooks[t] = (lambda n: lambda cpu: (s.ev.append((n, cpu.r[4] & 0xFFFFFFFF, cpu.r[5] & 0xFFFFFFFF)),
                                                       cpu.r.__setitem__(2, 0)))(n)
        s.c.w32(G_PASO, 0x3F99999A)   # paso 1,2 (PAL); el USA va a 1,0
        s.poner_mudos()
        s.M = s.c.r32(G_M)
        s.ctx = s.c.r32(G_CTX)

    def poner_mudos(s):
        # MiniGokuAnimations(nodo, anim, tabla): lo unico que hace la logica es dejar la animacion en nodo+0x48
        s.c.hooks[0x31BFA0] = lambda cpu: (s.ev.append(('MiniGokuAnimations', cpu.r[4] & 0xFFFFFFFF, cpu.r[5] & 0xFFFFFFFF)),
                                           cpu.w32(cpu.r[4] + 0x48, cpu.r[5] & 0xFFFFFFFF))

    def u(s, a): return s.c.r32(a)
    def f(s, a): return struct.unpack('<f', struct.pack('<I', s.c.r32(a)))[0]

    def nodos(s):
        out = []
        for i in range(0x80):
            b = bytes(s.c.m[s.ctx + i * 0x30:s.ctx + i * 0x30 + 0x30])
            fl = struct.unpack_from('<I', b, 4)[0]
            if fl:
                out.append((i, fl, struct.unpack_from('<H', b, 0)[0], struct.unpack_from('<I', b, 8)[0],
                            struct.unpack_from('<ff', b, 0x18)))
        return out

    def aparecer(s, de_vuelta=False):
        """Entrada al mapa (2FD0F0 + 2FCCB0 hasta agotar la lista) o vuelta de combate (todas a la vez)."""
        c = s.c
        a0 = c.r32(s.M)
        for i in range(0x80 * 0x30): c.m[s.ctx + i] = 0
        for k in range(0x3C // 4 * 2): c.w32(0x5D9220 + 4 * k, 0)
        c.w32(0x5D9260, 1 if de_vuelta else 0)   # argumento del comando 10: 1 = de golpe (vuelta de combate)
        c.w32(0x5D92A0, 1)                       # la cola solo lleva el comando 10, que saca el manejador al acabar
        c.call(ICONS, a0, limite=5_000_000)
        k = 0
        while not de_vuelta and struct.unpack('<i', c.m[a0 + 0x30:a0 + 0x34])[0] >= 0 and k < 600:
            c.call(APPEAR, a0, limite=5_000_000)
            k += 1
        return k

    def pad(s, mant=0, pulsa=0, lx=0, ly=0):
        c = s.c
        c.m[PAD:PAD + 2] = struct.pack('<H', mant)
        c.m[PAD + 2:PAD + 4] = struct.pack('<H', pulsa)
        c.m[PAD + 0xE] = lx & 255
        c.m[PAD + 0xF] = ly & 255

    def cuadro(s, mant=0, pulsa=0, lx=0, ly=0):
        """Un cuadro de mapa libre: seleccion bajo Goku y mandos. Devuelve (x, y, sel, orden pendiente)."""
        c, M = s.c, s.M
        s.ev.clear()
        s.pad(mant, pulsa, lx, ly)
        c.call(MANDOS, limite=5_000_000)
        c.call(CAMARA, limite=5_000_000)
        return s.f(M + 0x1D0), s.f(M + 0x1D4), c.r32(M + 0x330)


def autoprueba(p2s=P2S):
    m = Mapa(p2s)
    oro = m.nodos()
    k = m.aparecer()
    assert m.nodos() == oro, 'aparicion distinta del PCSX2'
    print('aparicion: %d casillas en %d cuadros: igual que PCSX2' % (len(oro), k))
    x0, y0, _ = m.cuadro(lx=100)
    x1, y1, sel = m.cuadro(lx=100)
    assert abs((x1 - x0) - 2.4) < 1e-4, (x0, x1)
    print('palanca derecha: +%.3f px/cuadro; casilla bajo Goku %#x' % (x1 - x0, sel))


if __name__ == '__main__':
    autoprueba(*(sys.argv[1:2] or [P2S]))




# SPDX-License-Identifier: MIT
"""Raiz animada de Piccolo en el 0x7DD (= src/iw_mips.cpp persecucion::Raiz). Lee la copia del jugador; nada al repo.

El original (EU): Animations2 0x207180 mezcla cada campo el hueso vivo hacia el clip (Animations, beta de +0x674/+0x678);
Char_UpdateRootAndBones 0x2F5DA0 vuelca el hueso raiz (modelo+0x40C = 1, PIC_WAIST) en +0x40 segun +0x18E8; CameraLocation
0x208390 pone el ancla +0x640 = +0x40 + giro * hueso. Solo importa la POSICION del hueso 1: cada hueso se mezcla por
separado (medido en el savestate: vivo = pista de 925/1 clip 0x2E en el cuadro +0xC68, a 1e-5).
Clips: #BSK de Piccolo (DATA_CMN 925/0) y si falta el comun (915/3): bloque [indice u16][almacen u16]. Almacen 0/1 =
915/0, 915/1 (GOK_*), 3 = 925/1 (PIC_*), 4 = 925/2. Hueso por nombre sin el prefijo (WAIST en los dos).
"""
import math
import struct
from pathlib import Path

CMN_USA = Path('D:/DBZ3HD/ps2_games/Infinite World (USA)/USR/DATA_CMN.AFS')
ALMACENES = {0: (915, 0), 1: (915, 1), 3: (925, 1), 4: (925, 2)}


def U(b, o): return struct.unpack_from('<I', b, o)[0]
def H(b, o): return struct.unpack_from('<H', b, o)[0]
def F(b, o): return struct.unpack_from('<f', b, o)[0]
def f32(x): return struct.unpack('<f', struct.pack('<f', x))[0]


def tr(x):
    """iwbubbles::poses::Truncar: double -> float hacia cero (EE)."""
    r = f32(x)
    if abs(r) > abs(x):
        u = struct.unpack('<I', struct.pack('<f', r))[0]
        if u & 0x7FFFFFFF:
            r = struct.unpack('<f', struct.pack('<I', u - 1))[0]
    return r


def _hijo(b, c):
    t = U(b, 0x14)
    return b[U(b, t + 16 * c):U(b, t + 16 * c) + U(b, t + 16 * c + 4)]


class Recursos:
    def __init__(s, cmn=CMN_USA):
        d = Path(cmn).read_bytes()
        ent = lambda e: d[U(d, 8 + 8 * e):U(d, 8 + 8 * e) + U(d, 12 + 8 * e)]
        e915, e925 = ent(915), ent(925)
        s.bsk = [_hijo(e925, 0), _hijo(e915, 3)]
        s.amm = {k: _hijo(e915 if e == 915 else e925, c) for k, (e, c) in ALMACENES.items()}
        s.hueso = {}
        for k, a in s.amm.items():
            nombres = [a[U(a, 28) + 32 * j:U(a, 28) + 32 * j + 32].split(b'\0')[0] for j in range(U(a, 24))]
            s.hueso[k] = next(j for j, n in enumerate(nombres) if n.split(b'_', 1)[-1] == b'WAIST')

    def clip(s, codigo):
        """Animations 0x207E90 (codigo < 0x10000): primera tabla con el codigo -> (almacen, indice); None = el reposo."""
        for b in s.bsk:
            if codigo < U(b, 0x10):
                blk = U(b, U(b, 0x14) + 4 * codigo)
                if blk:
                    return H(b, blk + 2), H(b, blk)
        return None

    def muestra(s, clip, cuadro):
        """Posicion del hueso WAIST del clip en `cuadro` (= poses::Banco::Muestrear, curva de posicion)."""
        if clip is None or clip[0] not in s.amm:
            return [0.0, 0.0, 0.0]
        a = s.amm[clip[0]]
        if clip[1] >= U(a, 16):
            return [0.0, 0.0, 0.0]
        desc = U(a, 20) + 16 * clip[1]
        flags, off = U(a, desc), U(a, desc + 12)
        pistas = 3 if flags & 16 else 2
        p = U(a, off + 4 * (pistas * s.hueso[clip[0]] + (1 if flags & 1 else 0)))
        if not p:
            return [0.0, 0.0, 0.0]
        n = U(a, p + 8)
        if not n:
            return [0.0, 0.0, 0.0]
        t = lambda i: U(a, p + 12 + 16 * i)
        ia = 0
        while ia + 1 < n and t(ia + 1) <= cuadro:
            ia += 1
        ib = min(ia + 1, n - 1)
        delta = f32(float(t(ib)) - float(t(ia)))
        k = min(max(f32(f32(cuadro - float(t(ia))) / delta), 0.0), 1.0) if delta > 0 else 0.0
        va = [F(a, p + 16 + 16 * ia + 4 * i) for i in range(3)]
        vb = [F(a, p + 16 + 16 * ib + 4 * i) for i in range(3)]
        return [tr(va[i] + (vb[i] - va[i]) * k) for i in range(3)]


def lut_seno(elf_bytes):
    """Tabla de seno del ELF (16385 floats, la de poses::LeerLut): std::sin NO es equivalente."""
    firma = struct.pack('<4f', 0.0, 0.000096, 0.000192, 0.000288)
    p = elf_bytes.find(firma)
    while p >= 0 and p % 4:
        p = elf_bytes.find(firma, p + 1)
    assert p >= 0, 'LUT de seno no encontrada en el ELF'
    return struct.unpack_from('<16385f', elf_bytes, p)


def giro_waist(r, lut, clip, cuadro):
    """Fase 1 (7/11): cuaternion (x, y, z, w) del hueso WAIST del clip en `cuadro` (= poses::Banco::Muestrear, curva de
    giro: angulos u16 >> 1 con la tabla de seno del ELF, slerp normalizado). None si el clip no trae giro."""
    if clip is None or clip[0] not in r.amm:
        return None
    a = r.amm[clip[0]]
    if clip[1] >= U(a, 16):
        return None
    desc = U(a, 20) + 16 * clip[1]
    flags, off = U(a, desc), U(a, desc + 12)
    if not flags & 1:
        return None
    pistas = 3 if flags & 16 else 2
    p = U(a, off + 4 * (pistas * r.hueso[clip[0]]))
    if not p:
        return None
    n = U(a, p + 8)

    def seno(ang):
        ang &= 65535
        j = ang & 16383
        if ang & 16384:
            j = 16384 - j
        return -lut[j] if ang & 32768 else lut[j]

    def q(o):
        x, y, z = (H(a, o + k) >> 1 for k in (2, 4, 6))
        sx, cx, sy, cy, sz, cz = seno(x), seno(x + 16384), seno(y), seno(y + 16384), seno(z), seno(z + 16384)
        return [f32(sx * cy * cz - cx * sy * sz), f32(cx * sy * cz + sx * cy * sz), f32(cx * cy * sz - sx * sy * cz),
                f32(cx * cy * cz + sx * sy * sz)]
    t = lambda i: H(a, p + 12 + 8 * i)   # noqa: E731
    ia = 0
    while ia + 1 < n and t(ia + 1) <= cuadro:
        ia += 1
    ib = min(ia + 1, n - 1)
    qa = q(p + 12 + 8 * ia)
    delta = float(t(ib)) - float(t(ia))
    k = min(max((cuadro - float(t(ia))) / delta, 0.0), 1.0) if delta > 0 else 0.0
    if k <= 0:
        return qa
    qb = q(p + 12 + 8 * ib)
    if sum(x * y for x, y in zip(qa, qb)) < 0:
        qb = [-x for x in qb]
    v = [qa[i] + (qb[i] - qa[i]) * k for i in range(4)]
    nn = math.sqrt(sum(x * x for x in v))
    return [f32(x / nn) for x in v]


def matriz_giro(qv):
    """Filas 0..2 de poses::Matriz (fila 2 = el eje z del hueso: lo que mira MgChar_IsTargetInFront)."""
    x, y, z, w = qv
    return [[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
            [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
            [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]]


def mezcla(u, rate, paso):
    """Animations2: beta del campo y el +0x674 nuevo (= poses::Instancia::Aplicar)."""
    a = tr(u - 1.0)
    t = tr(a * a)
    beta = min(max(f32((1.0 - t) * u + t * u * paso), 0.0), 1.0) if paso > 0 else 0.0
    return beta, min(1.0, tr(u + rate * paso))


def interpolar(a, b, beta):
    if beta == 1.0:
        return list(b)
    if beta <= 0:
        return list(a)
    return [tr(a[i] + (b[i] - a[i]) * beta) for i in range(3)]


def girar(fx, fz, v):
    """Giro de la matriz del actor (derecha (fz,0,-fx), arriba, frente (fx,0,fz)) sobre v; operaciones float sueltas."""
    x = f32(f32(v[0] * fz) + f32(v[2] * fx))
    z = f32(f32(-v[0] * fx) + f32(v[2] * fz))
    return [x, v[1], z]


if __name__ == '__main__':   # comprobacion: el vivo del savestate (clip 0x606 en 67,2) sale de la pista del jugador
    r = Recursos()
    assert r.clip(0x606) == (3, 0x2E) and r.clip(0x15) == (0, 0xF) and r.clip(0x16) == (3, 5)
    m = r.muestra((3, 0x2E), 67.19989013671875)
    assert max(abs(x - y) for x, y in zip(m, [-0.0718454, -0.2322115, -0.1301815])) < 1e-4, m
    assert mezcla(1.0, 0.03, 1.2) == (1.0, 1.0) and not math.isnan(mezcla(0.5, 0.01, 1.2)[0])
    print('raiz7dd ok', m)

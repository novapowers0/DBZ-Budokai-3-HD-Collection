"""Recursos originales de transición B2, separados del guion donante de B3.

Se construye exclusivamente desde la ISO del jugador. No ejecuta ni aproxima el
guion. Cámara y trayectoria se identifican por la lista D8 del objeto original,
NO por tipo de clip: ambos son0x1D en las cuatro transiciones de la ISO PAL.

Contenedor B2TZ v2: cabecera64B LE + tabla(n*24B) + bloques alineados32.
Registro: etiqueta8B, offset u32, longitud u32, CRC32 u32, reservado u32.
CAMR/TRAY contienen ACC BE; SPX0, MDD0 y FX00 son originales LE. MDD0 retiene
raíces, animaciones y colisión antes del saneado del conversor de escenario.
"""
from dataclasses import dataclass
import math
import struct
import zlib

GRUPOS = (0, 2, 4, 7, 8)
VERSION = 2
MAX_ARCHIVO = 64 * 1024 * 1024
ETIQUETAS = (b"CAMR", b"TRAY", b"SPX0", b"MDD0", b"FX00", b"MAD0", b"MAS0", b"SND0", b"ANM0", b"ANM1", b"FXHD", b"BSK0")


def sonidos(fuente, grupo):
    """Índices originales, no audio distribuido. Perfil PAL medido del jugador."""
    import hashlib
    elf = fuente.iso.read(fuente.iso.boot_elf())
    # La tabla de voces debe leerse del ELF correspondiente, nunca de otro juego.
    if hashlib.sha256(elf).hexdigest() != "30c362969ad64184df0b4a4ee498badb101265470883948f3131b6302ae3c797":
        return b"#B2S" + struct.pack("<3I", 0, fuente.escenarios()[grupo] - 2, 0)
    ph = _u(elf, 28)
    es, n = struct.unpack_from("<HH", elf, 42)
    def va(a, size):
        for i in range(n):
            typ, off, addr, _, fs, *_ = struct.unpack_from("<8I", elf, ph + i * es)
            if typ == 1 and addr <= a and a + size <= addr + fs:
                return elf[off + a - addr:off + a - addr + size]
        raise ValueError("tabla de sonido fuera del ELF B2")
    ptr = struct.unpack("<I", va(0x3748C0 + 54 * 4, 4))[0]
    # La tabla54 empieza388750 y la55 en388770 en este ELF:16 entradas.
    fin = struct.unpack("<I", va(0x3748C0 + 55 * 4, 4))[0]
    if fin - ptr != 32:
        raise ValueError("tamaño inesperado de la tabla de voces200")
    return b"#B2S" + struct.pack("<3I", 1, fuente.escenarios()[grupo] - 2, 16) + va(ptr, 32)


@dataclass(frozen=True)
class Transicion:
    grupo: int
    bloques: dict


def _u(b, p, orden="<"):
    if p < 0 or p + 4 > len(b):
        raise ValueError("puntero fuera del recurso B2")
    return struct.unpack_from(orden + "I", b, p)[0]


def validar_acc(b):
    """Valida todas las pistas antes de entregarlas a un consumidor de escena."""
    if len(b) < 32 or b[:4] != b"#ACC":
        raise ValueError("recurso de transición no es ACC")
    n, t = _u(b, 16, ">"), _u(b, 20, ">")
    if not 1 <= n <= 64 or t < 32 or t + 16 * n > len(b):
        raise ValueError("tabla de clips inválida")
    duraciones = []
    for i in range(n):
        tipo, _, frames, data = struct.unpack_from(">4I", b, t + 16 * i)
        if tipo not in (0x0D, 0x1D) or not 1 <= frames <= 10000:
            raise ValueError("clip de transición inválido")
        count = 4 if tipo & 0x10 else 3
        if data < t + 16 * n or data + count * 4 > len(b):
            raise ValueError("descriptor de pistas inválido")
        for j in range(count):
            ptr = _u(b, data + 4 * j, ">")
            nk = _u(b, ptr + 8, ">")
            paso, comp = (16, 3) if j < 2 else (8, 1)
            if not ptr or not 1 <= nk <= 20000 or ptr + 12 + nk * paso > len(b):
                raise ValueError("pista fuera de límites")
            anterior = -1
            for k in range(nk):
                p = ptr + 12 + paso * k
                f = _u(b, p, ">")
                if f <= anterior or f >= frames:
                    raise ValueError("claves de pista desordenadas o fuera del clip")
                anterior = f
                if not all(math.isfinite(x) for x in struct.unpack_from(">%df" % comp, b, p + 4)):
                    raise ValueError("valor no finito en la pista")
        duraciones.append(frames)
    return duraciones


def construir(fuente, grupo):
    """Extrae ANTES de neutralizar raíces; ningún dato del donante se mezcla aquí."""
    if grupo not in GRUPOS:
        return None
    import escenarios_convertir as E
    mdd, mad, mas, spx = fuente.piezas(grupo)
    # Templo de Kami (grupo2/stageELF6) destruye sus raíces sin cambiar
    # de zona: una variante es válida y conserva sus dos AMC originales.
    if _u(mdd, 4) < (1 if grupo == 2 else 2):
        return None
    amcs = E.camaras_transicion(mdd)
    if len(amcs) != 2:
        raise ValueError("B2: se esperaban cámara y trayectoria en el objeto0")
    camara, trayectoria = (bytes(E.ps2hd.convert_block(x)) for x in amcs)
    validar_acc(camara)
    validar_acc(trayectoria)
    if not spx or spx[:4] != b"#SPX":
        raise ValueError("B2: falta el guion original de la transición")
    # Descriptor ELF372AB0: banco = MAD-3. MAD+5 es el banco del SIGUIENTE
    # escenario (en nieve, Ciudad:35 recursos en vez de los37 del original).
    banco = fuente.entrada(fuente.escenarios()[grupo] - 3)
    if banco[:4] != b"#AMB":
        raise ValueError("B2: falta el banco de efectos del escenario")
    # Bancos originales medidos por PINE: común134/0 (275 animaciones/42huesos)
    # y Goku175/0 (166/42). El puente remapea pistas por sufijo al banco HD.
    anms = []
    for indice in (134, 175):
        amb = fuente.entrada(indice)
        hijo = E.hijos_amb(amb, "<")[0]
        amm = amb[hijo[0]:hijo[0] + hijo[1]]
        if amm[:4] != b"#AMM" or _u(amm, 24) != 42:
            raise ValueError("perfil de animación B2 no coincide con el medido")
        anms.append(bytes(E.ps2hd.convert_block(amm)))
    fxhd = bytes(E.ps2hd.convert_block(banco))
    # El índice de clip NO basta: BSK173 contiene también bucles y centinelas
    # de reproducción. Se conserva LE; el puente solo consume sus escalares.
    moveset = fuente.entrada(173)
    bsks = [(o, n) for o, n, *_ in E.hijos_amb(moveset, "<") if moveset[o:o+4] == b"#BSK"]
    if len(bsks) != 1:
        raise ValueError("perfil B2 sin banco de movimientos173/BSK")
    off, size = bsks[0]
    bsk = moveset[off:off+size]
    return empaquetar(Transicion(grupo, dict(zip(ETIQUETAS, (camara, trayectoria, spx, mdd, banco, mad, mas, sonidos(fuente,grupo), *anms, fxhd, bsk)))))


def instalar(fuente, grupo, carpeta):
    """Añade el recurso al mod sin cambiar la lista ACC usada por B3."""
    from pathlib import Path
    if fuente.juego != "Budokai 2":
        return False
    datos = construir(fuente, grupo)
    if datos is None:
        return False
    carpeta = Path(carpeta)
    (carpeta / "transicion_b2.b2t").write_bytes(datos)
    with (carpeta / "stage.toml").open("a", encoding="utf-8") as f:
        f.write('\ntransicion_b2 = "transicion_b2.b2t"  # cámara/trayectoria/SPX/raíces/efectos originales\n')
    return True


def empaquetar(t):
    if t.grupo not in GRUPOS or set(t.bloques) != set(ETIQUETAS):
        raise ValueError("grupo o componentes de transición inválidos")
    inicio = (64 + 24 * len(ETIQUETAS) + 31) & ~31
    out = bytearray(inicio)
    for i, etiqueta in enumerate(ETIQUETAS):
        b = bytes(t.bloques[etiqueta])
        if not b:
            raise ValueError("componente de transición vacío")
        off = len(out)
        out.extend(b)
        out.extend(bytes((-len(out)) % 32))
        struct.pack_into("<8s4I", out, 64 + 24 * i, etiqueta, off, len(b), zlib.crc32(b), 0)
    if len(out) > MAX_ARCHIVO:
        raise ValueError("transición demasiado grande")
    struct.pack_into("<4s7I", out, 0, b"B2TZ", VERSION, t.grupo, len(ETIQUETAS), 64, len(out), 0, 0)
    return bytes(out)


def leer(b):
    """Rechaza archivo truncado/corrupto, duplicados y componentes solapados."""
    if len(b) < 64 or len(b) > MAX_ARCHIVO:
        raise ValueError("tamaño de transición inválido")
    magic, ver, grupo, n, tabla, tam, res0, res1 = struct.unpack_from("<4s7I", b)
    etiquetas = ETIQUETAS[:n] if ver == 2 and n in (7,8,10,11,12) else ETIQUETAS[:5] if ver == 1 else ()
    if magic != b"B2TZ" or not etiquetas or grupo not in GRUPOS or n != len(etiquetas) or tabla != 64 or tam != len(b):
        raise ValueError("cabecera de transición inválida")
    if res0 or res1 or any(b[32:64]):
        raise ValueError("campos reservados no nulos")
    bloques, fin = {}, (tabla + 24 * n + 31) & ~31
    for i in range(n):
        tag, off, size, crc, reservado = struct.unpack_from("<8s4I", b, tabla + 24 * i)
        etiqueta = tag.rstrip(b"\0")
        if etiqueta != etiquetas[i] or reservado or off % 32 or off < fin or not size or off + size > len(b):
            raise ValueError("tabla de componentes inválida")
        dato = bytes(b[off:off + size])
        if zlib.crc32(dato) != crc:
            raise ValueError("CRC de transición incorrecto")
        bloques[etiqueta] = dato
        fin = off + size
    validar_acc(bloques[b"CAMR"])
    validar_acc(bloques[b"TRAY"])
    for tag, magic in ((b"SPX0", b"#SPX"), (b"MDD0", b"#MDD"), (b"FX00", b"#AMB")):
        if bloques[tag][:4] != magic:
            raise ValueError("componente original de transición inválido")
    for tag, magic in ((b"MAD0", b"#MAD"), (b"MAS0", b"#MAS")):
        if tag in bloques and bloques[tag][:4] != magic:
            raise ValueError("colisión original de transición inválida")
    if b"BSK0" in bloques:
        bsk = bloques[b"BSK0"]
        if len(bsk) < 32 or bsk[:4] != b"#BSK":
            raise ValueError("banco de movimientos original inválido")
        n, tabla = struct.unpack_from("<II", bsk, 16)
        if not 0 < n <= 4096 or tabla < 32 or tabla+4*n > len(bsk):
            raise ValueError("tabla de movimientos original inválida")
    return Transicion(grupo, bloques)

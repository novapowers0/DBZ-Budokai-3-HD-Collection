#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ps2_b3.py - Mods para el Budokai 3 de PS2 (Greatest Hits USA, SLUS-20998) desde la copia del jugador.

Nunca se toca la copia original: "montar" escribe una ISO nueva con los mods encendidos.

  python ps2_b3.py info <copia>                         comprueba la copia (ISO o carpeta extraida)
  python ps2_b3.py texturas <copia> <entrada> <carpeta> saca las texturas de una entrada de data_cmn a PNG
  python ps2_b3.py textura <copia> <entrada> <carpeta> <mod>   PNG editados -> mod
  python ps2_b3.py personaje <copia> <quien> <por_quien> <mod> un personaje con el aspecto de otro
  python ps2_b3.py musica <audio> <entrada> <mod>       una pista de adx_usa/adx_jpn por tu audio
  python ps2_b3.py montar <copia> <salida.iso> [--mods CARPETA] [--solo MOD,MOD]
  python ps2_b3.py prueba                               comprobacion sin juego

Un mod es una carpeta mods_ps2/<mod>/usr/<fichero.afs>/<n> con los bytes de la entrada n.
Medido contra la copia (31/10): data_cmn tiene la misma numeracion que la HD, entradas crudas en little-endian.
Las entradas del AFS se reescriben contiguas y en orden (ver _plan_afs).
"""
import argparse
import datetime
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
for _d in (HERE, os.path.join(HERE, "..", "awo_tools"), os.path.join(HERE, "awo_tools")):
    if os.path.isdir(_d) and _d not in sys.path:
        sys.path.append(_d)

import iso as iso_mod  # noqa: E402

SECTOR = 2048
SERIE = "slus_209.98"     # unica copia medida; otra region tiene otra numeracion
MODS = os.path.join(HERE, "mods_ps2")
HZ_MUSICA = 48000         # las pistas del juego miden 48 kHz estereo (entradas 122, 4187, 4267, 4276)

# Quien -> (modelos, boca). De docs/03_formatos/MAPA_ROSTER_HD.md §4; misma numeracion que la HD.
PERSONAJES = {
    "Android 16": ((70, 71), (73,)), "Android 17": ((75, 76), (78,)), "Android 18": ((80, 81, 82), (84,)),
    "Bardock": ((99, 100), (102,)), "Broly": ((119, 120, 121, 122), (124, 125)),
    "Majin Buu": ((128, 129), (131,)), "Super Buu": ((133, 134), (136,)), "Kid Buu": ((141, 142), (144,)),
    "Cell": ((146, 147, 148, 149, 150, 151), (153, 154, 155)), "Cooler": ((166, 167, 168, 169, 170, 171), (173,)),
    "Dabura": ((176, 177), (179,)), "Freezer": ((181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193),
                                                (195, 196, 197, 198)),
    "Gogeta": ((217, 218, 219, 220), (221,)), "Gohan adulto": ((225, 226, 227, 228, 229, 230), (232,)),
    "Gohan niño": ((246, 247, 248), (250,)), "Goku niño": ((252, 253), (255,)), "Ginyu": ((258, 259), (261,)),
    "Goku": ((264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275), (289,)),
    "Goten": ((298, 299, 300, 301), (303,)), "Krillin": ((327, 328, 329), (331,)),
    "Nappa": ((345, 346), (348,)), "Piccolo": ((350, 351, 352, 353), (355, 356)), "Raditz": ((360, 361), (363,)),
    "Recoome": ((366, 367), (369,)), "Saibaman": ((371, 372), (374,)), "Mr. Satan": ((377, 378), (380,)),
    "Trunks niño": ((383, 384, 385, 386), (388,)), "Trunks": ((390, 391, 392, 393, 394, 395), (397,)),
    "Tenshinhan": ((400, 401), (403,)), "Uub": ((405, 406), (408,)), "Videl": ((410, 411), (413,)),
    "Vegeta": ((416, 417, 418, 419, 420, 421), (431, 432)), "Vegito": ((438, 439, 440, 441), (442,)),
    "Yamcha": ((445, 446, 447), (449,)),
}


class CopiaNoValida(Exception):
    """La copia no es la que sabemos modificar; el mensaje va directo al jugador."""


class Copia:
    """B3 de PS2 del jugador: ISO o carpeta extraida. Solo lectura."""

    def __init__(self, ruta):
        self.ruta = ruta
        self.ficheros = {}    # "/USR/DATA_CMN.AFS" -> (tamano, abrir())
        self._img = None
        if ruta.lower().endswith((".chd", ".cso", ".gz", ".bin", ".cue")):
            raise CopiaNoValida("Ese formato no se puede leer directamente. Convierte tu copia a .iso "
                                "(chdman extractdvd, por ejemplo) o usa la carpeta extraida.")
        if os.path.isdir(ruta):
            for base, _dirs, nombres in os.walk(ruta):
                for n in nombres:
                    p = os.path.join(base, n)
                    rel = "/" + os.path.relpath(p, ruta).replace("\\", "/").upper()
                    self.ficheros[rel] = (os.path.getsize(p), lambda p=p: open(p, "rb"))
        elif os.path.isfile(ruta):
            try:
                self._img = img = iso_mod.Iso(ruta)
            except (OSError, ValueError, struct.error) as e:
                raise CopiaNoValida("No se puede leer como ISO de PS2: %s" % e)
            for p, (_lba, tam) in img.files().items():
                self.ficheros[p.upper()] = (tam, lambda p=p, img=img: img.open(p))
        else:
            raise CopiaNoValida("No existe: %s" % ruta)
        self.serie = self._serie()
        if self.serie != SERIE:
            raise CopiaNoValida("Esta copia es %s. El modo PS2 solo funciona con Budokai 3 Greatest Hits USA "
                                "(SLUS-20998): es la unica versión medida, otras tienen otra numeración."
                                % (self.serie or "de otro juego"))

    def cerrar(self):
        if self._img:
            self._img._f.close()

    def _serie(self):
        if "/SYSTEM.CNF" not in self.ficheros:
            return None
        with self.ficheros["/SYSTEM.CNF"][1]() as f:
            elf = iso_mod.boot_elf_name(f.read(), lambda n: n)
        return elf.lower() if elf else None

    def afs(self, nombre):
        return "/USR/" + nombre.upper()

    def tabla(self, nombre):
        """[(desplazamiento, tamano)] de un AFS."""
        with self.ficheros[self.afs(nombre)][1]() as f:
            cab = f.read(8)
            if cab[:3] != b"AFS":
                raise CopiaNoValida("%s no es un AFS." % nombre)
            n = struct.unpack("<I", cab[4:])[0]
            t = f.read(8 * n)
        return [struct.unpack_from("<II", t, i * 8) for i in range(n)]

    def entrada(self, nombre, n):
        off, tam = self.tabla(nombre)[n]
        with self.ficheros[self.afs(nombre)][1]() as f:
            f.seek(off)
            return f.read(tam)


    def pistas(self):
        """[(n, segundos)] de la musica: ADX estereo largos de adx_usa (mide la propia copia)."""
        out = []
        with self.ficheros[self.afs("adx_usa.afs")][1]() as f:
            for n, (off, tam) in enumerate(self.tabla("adx_usa.afs")):
                if tam < 300000:
                    continue
                f.seek(off)
                h = f.read(16)
                if h[0] == 0x80 and h[7] == 2:
                    out.append((n, struct.unpack(">I", h[12:16])[0] // max(1, struct.unpack(">I", h[8:12])[0])))
        return out


# ---------------------------------------------------------------- texturas
def amts(b):
    """Desplazamientos de cada #AMT dentro de una entrada."""
    out, i = [], b.find(b"#AMT")
    while i >= 0:
        out.append(i)
        i = b.find(b"#AMT", i + 4)
    return out


def codificar_textura(amt, t, rgba):
    """Escribe en `amt` (bytearray) la imagen RGBA de la textura `t` con su paleta de 16/256 colores."""
    import numpy as np
    from PIL import Image
    from amt_ps2 import GIF_HDR
    n = t["csize"] // 4
    im = Image.fromarray(rgba, "RGBA")
    if len({tuple(c) for c in rgba.reshape(-1, 4)}) <= n:
        pal, idx = np.unique(rgba.reshape(-1, 4), axis=0, return_inverse=True)
        idx = idx.reshape(-1).astype(np.uint8)
    else:
        q = im.quantize(colors=n, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
        pal = np.array(q.getpalette("RGBA")[:n * 4], dtype=np.uint8).reshape(-1, 4)
        idx = np.asarray(q).reshape(-1).astype(np.uint8)
    c = np.zeros((n, 4), dtype=np.uint8)
    c[:len(pal)] = pal
    c[:, 3] = (c[:, 3].astype(np.int32) + 1) // 2      # alfa del GS: 0..0x80
    if n == 256:   # CSM1: la misma permutacion que al leer
        c = c.reshape(8, 4, 8, 4)[:, [0, 2, 1, 3]].reshape(256, 4)
    amt[t["clut"] + GIF_HDR:t["clut"] + GIF_HDR + 4 * n] = c.tobytes()
    o = t["data"] + GIF_HDR
    if t["psm"] == 0x14:
        raw = idx[0::2] | (np.resize(idx[1::2], (len(idx) + 1) // 2) << 4)
    else:
        raw = idx
    raw = raw.tobytes()[:t["dsize"]]
    amt[o:o + len(raw)] = raw


def sacar_texturas(b, carpeta, prefijo=""):
    """PNG de cada textura; devuelve [(nombre, w, h)]."""
    from PIL import Image
    import amt_ps2
    os.makedirs(carpeta, exist_ok=True)
    salen = []
    for a, off in enumerate(amts(b)):
        amt = b[off:]
        for k, t in enumerate(amt_ps2.entries(amt)):
            nom = "%sa%d_t%02d.png" % (prefijo, a, k)
            Image.fromarray(amt_ps2.decode(amt, t), "RGBA").save(os.path.join(carpeta, nom))
            salen.append((nom, t["w"], t["h"]))
    return salen


def aplicar_texturas(b, carpeta, prefijo=""):
    """Entrada `b` con las PNG de `carpeta`. Las iguales al original no se tocan (asi una textura sin editar no
    pierde colores). Devuelve (bytes, cambiadas, avisos)."""
    import numpy as np
    from PIL import Image
    import amt_ps2
    out = bytearray(b)
    cambiadas, avisos = 0, []
    for a, off in enumerate(amts(b)):
        amt = bytearray(out[off:])
        for k, t in enumerate(amt_ps2.entries(bytes(amt))):
            nom = "%sa%d_t%02d.png" % (prefijo, a, k)
            p = os.path.join(carpeta, nom)
            if not os.path.exists(p):
                continue
            with Image.open(p) as im:
                rgba = im.convert("RGBA")
            if rgba.size != (t["w"], t["h"]):
                avisos.append("%s mide %dx%d y debe medir %dx%d: se ha ajustado." % (nom, *rgba.size, t["w"], t["h"]))
                rgba = rgba.resize((t["w"], t["h"]), Image.LANCZOS)
            arr = np.asarray(rgba)
            if np.array_equal(arr, amt_ps2.decode(bytes(amt), t)):
                continue
            codificar_textura(amt, t, arr)
            cambiadas += 1
        out[off:] = amt
    return bytes(out), cambiadas, avisos


# ---------------------------------------------------------------- mods
def ruta_mod(mod, afs, n, mods=None):
    d = os.path.join(mods or MODS, mod, "usr", afs.lower())
    os.makedirs(d, exist_ok=True)
    return os.path.join(d, str(n))


def lista_mods(mods=None):
    """[(nombre, encendido, {afs: {n: ruta}})] en orden alfabetico; el ultimo gana si dos tocan lo mismo."""
    raiz = mods or MODS
    out = []
    if not os.path.isdir(raiz):
        return out
    for nom in sorted(os.listdir(raiz)):
        d = os.path.join(raiz, nom)
        if not os.path.isdir(os.path.join(d, "usr")):
            continue
        tabla = {}
        for afs in os.listdir(os.path.join(d, "usr")):
            carpeta = os.path.join(d, "usr", afs)
            if os.path.isdir(carpeta):
                tabla[afs.lower()] = {int(n): os.path.join(carpeta, n) for n in os.listdir(carpeta) if n.isdigit()}
        out.append((nom, not os.path.exists(os.path.join(d, ".disabled")), tabla))
    return out


def mod_personaje(copia, quien, por_quien, mod, mods=None):
    """Cada forma de `quien` pasa a verse como la forma correspondiente de `por_quien` (la ultima se repite).
    Las animaciones casan por sufijo de hueso: siguen siendo las de `quien`."""
    ma, ba = PERSONAJES[quien]
    mb, bb = PERSONAJES[por_quien]
    pares = [(a, mb[min(i, len(mb) - 1)]) for i, a in enumerate(ma)] + \
            [(a, bb[min(i, len(bb) - 1)]) for i, a in enumerate(ba)]
    for a, b in pares:
        with open(ruta_mod(mod, "data_cmn.afs", a, mods), "wb") as f:
            f.write(copia.entrada("data_cmn.afs", b))
    return pares


def con_bucle(adx):
    """ADX con bucle de principio a fin, en la forma de las pistas del juego (version 4, bucle en +0x24, datos en
    sector entero como su inicio de bucle). Un ADX que ya trae bucle se deja igual."""
    off = struct.unpack(">H", adx[2:4])[0] + 4
    if adx[0x12] >= 3 and off >= 0x38 and struct.unpack(">I", adx[0x24:0x28])[0] == 1 and adx[0x12] == 4:
        return adx
    if adx[0x12] == 3 and off >= 0x2C and struct.unpack(">I", adx[0x18:0x1C])[0] == 1:
        return adx
    can, muestras = adx[7], struct.unpack(">I", adx[12:16])[0]
    datos_ini = SECTOR
    cab = bytearray(datos_ini)
    cab[:0x12] = adx[:0x12]
    struct.pack_into(">H", cab, 2, datos_ini - 4)
    cab[0x12] = 4
    bloques = (muestras + 31) // 32
    struct.pack_into(">H", cab, 0x22, 1)     # como la pista nativa 4214, con este mismo bucle completo
    struct.pack_into(">5I", cab, 0x24, 1, 0, datos_ini, muestras, datos_ini + bloques * adx[5] * can)
    cab[datos_ini - 6:datos_ini] = b"(c)CRI"
    return bytes(cab) + adx[off:]


def mod_musica(audio, entrada, mod, mods=None):
    """`audio` -> ADX 48 kHz estereo con bucle como entrada de adx_usa y adx_jpn."""
    from kit import musica
    if audio.lower().endswith(musica.EXT_ADX):
        datos = open(audio, "rb").read()
    else:
        exe = musica.ffmpeg()
        if not exe:
            raise RuntimeError("Falta ffmpeg para convertir ese audio. Instálalo o usa un .adx.")
        with tempfile.TemporaryDirectory(prefix="ps2_b3_", dir=os.environ.get("DBZ3_TMP") or None) as tmp:
            adx = os.path.join(tmp, "p.adx")
            r = subprocess.run([exe, "-v", "error", "-y", "-i", audio, "-vn", "-ac", "2", "-ar", str(HZ_MUSICA),
                                "-f", "adx", adx], capture_output=True, text=True)
            if r.returncode or not os.path.exists(adx):
                raise RuntimeError("No se pudo convertir el audio: %s" % (r.stderr.strip() or "ffmpeg falló"))
            datos = open(adx, "rb").read()
    cab = musica.cabecera_adx(datos)
    if not cab:
        raise RuntimeError("El audio convertido no es un ADX válido.")
    datos = con_bucle(datos)
    for afs in ("adx_usa.afs", "adx_jpn.afs"):
        with open(ruta_mod(mod, afs, entrada, mods), "wb") as f:
            f.write(datos)
    return cab


# ---------------------------------------------------------------- montar la ISO
def _sectores(n):
    return (n + SECTOR - 1) // SECTOR


def _plan_afs(copia, nombre, sobre):
    """Tabla nueva de un AFS con `sobre` = {n: ruta}. Las entradas siguen contiguas y en orden: el lector CRI del
    juego (ADXF, entradas < 128 MB) situa cada una sumando los tamanos; una fuera de orden cuelga el juego (medido)."""
    tabla = copia.tabla(nombre)
    for n in sobre:
        if not 0 <= n < len(tabla):
            raise CopiaNoValida("%s no tiene entrada %d." % (nombre, n))
    nueva, pos = [], tabla[0][0]
    for n, (_off, tam) in enumerate(tabla):
        if n in sobre:
            tam = os.path.getsize(sobre[n])
            if tam >= 128 << 20:
                raise CopiaNoValida("La entrada %d de %s pasa de 128 MB: el juego no puede leerla." % (n, nombre))
        nueva.append((pos, tam))
        pos += _sectores(tam) * SECTOR
    fin = tabla[-1][0] + tabla[-1][1]
    cola = copia.ficheros[copia.afs(nombre)][0] - fin
    return nueva, pos - (_sectores(nueva[-1][1]) * SECTOR - nueva[-1][1]) + cola


def _copiar(src, dst, n):
    while n > 0:
        d = src.read(min(n, 1 << 22))
        if not d:
            raise OSError("copia cortada: faltan %d bytes" % n)
        dst.write(d)
        n -= len(d)


def _registro(nombre, lba, tam, es_dir, fecha):
    nb = nombre if isinstance(nombre, bytes) else (nombre + ";1" if not es_dir else nombre).encode("ascii")
    ln = 33 + len(nb)
    ln += (ln & 1) + 14     # los 14 bytes de cola que llevan los registros de un DVD de PS2
    r = bytearray(ln)
    r[0] = ln
    struct.pack_into("<I", r, 2, lba)
    struct.pack_into(">I", r, 6, lba)
    struct.pack_into("<I", r, 10, tam)
    struct.pack_into(">I", r, 14, tam)
    r[18:25] = fecha
    r[25] = 2 if es_dir else 0
    struct.pack_into("<H", r, 28, 1)
    struct.pack_into(">H", r, 30, 1)
    r[32] = len(nb)
    r[33:33 + len(nb)] = nb
    return bytes(r)


def _dir_datos(hijos, propio, padre, fecha):
    """Bytes de un directorio; un registro no puede cruzar sector."""
    regs = [_registro(b"\x00", propio[0], propio[1], True, fecha), _registro(b"\x01", padre[0], padre[1], True, fecha)]
    regs += [_registro(n, l, t, d, fecha) for n, l, t, d in hijos]
    out = bytearray()
    for r in regs:
        if len(out) % SECTOR + len(r) > SECTOR:
            out += bytes(SECTOR - len(out) % SECTOR)
        out += r
    return out


def _tam_dir(nombres):
    tam = 48 + 48
    for n, es_dir in nombres:
        nb = len(n) + (0 if es_dir else 2)
        ln = 33 + nb
        ln += (ln & 1) + 14
        if tam % SECTOR + ln > SECTOR:
            tam += SECTOR - tam % SECTOR
        tam += ln
    return tam


def escribir_iso(salida, ficheros, progreso=print):
    """ISO 9660 con `ficheros` = {"/USR/X.AFS": (tam, escribir(f))}. El arranque primero; los AFS van por orden."""
    dirs = {"": []}
    for ruta in ficheros:
        partes = ruta.strip("/").split("/")
        for i in range(1, len(partes)):
            d = "/".join(partes[:i])
            if d not in dirs:
                dirs[d] = []
                dirs["/".join(partes[:i - 1])].append((partes[i - 1], True))
        dirs["/".join(partes[:-1])].append((partes[-1], False))
    orden_dirs = sorted(dirs, key=lambda d: (d.count("/") + (1 if d else 0), d))
    lba = 261       # 16 PVD, 17 fin, 18-20 marcas UDF; tablas 257-260 y directorios desde 261, como el disco original
    pos = {}
    for d in orden_dirs:
        dirs[d].sort(key=lambda x: x[0])
        tam = _tam_dir(dirs[d])
        if tam % 65536 == 2048:   # PCSX2 toma por CD una imagen con raiz de 2048 bytes, y el juego no arranca
            tam += 2
        pos[d] = (lba, tam)
        lba += _sectores(tam)
    primero = [r for r in ficheros if r == "/SYSTEM.CNF"] + [r for r in ficheros if r.startswith("/SLUS")]
    resto = sorted(r for r in ficheros if r not in primero)
    lbas = {}
    for r in primero + resto:
        lbas[r] = lba
        lba += _sectores(ficheros[r][0])
    total = lba
    ahora = datetime.datetime.now()
    fecha = bytes([ahora.year - 1900, ahora.month, ahora.day, ahora.hour, ahora.minute, ahora.second, 0])

    tabla_l, tabla_m = bytearray(), bytearray()
    numeros = {d: i + 1 for i, d in enumerate(orden_dirs)}
    for d in orden_dirs:
        nom = d.rsplit("/", 1)[-1].encode("ascii") if d else b"\x00"
        padre = numeros[d.rsplit("/", 1)[0] if "/" in d else ""]
        cab = bytes([len(nom), 0])
        relleno = bytes(len(nom) & 1)
        tabla_l += cab + struct.pack("<IH", pos[d][0], padre) + nom + relleno
        tabla_m += cab + struct.pack(">IH", pos[d][0], padre) + nom + relleno

    def texto(s, n):
        return s.encode("ascii").ljust(n, b" ")

    fecha_pvd = ahora.strftime("%Y%m%d%H%M%S00").encode("ascii") + b"\x00"
    pvd = bytearray(SECTOR)
    pvd[0:7] = b"\x01CD001\x01"
    pvd[8:40] = texto("PLAYSTATION", 32)
    pvd[40:72] = texto("DBZ3_MODS", 32)
    struct.pack_into("<I", pvd, 80, total)
    struct.pack_into(">I", pvd, 84, total)
    struct.pack_into("<H", pvd, 120, 1)
    struct.pack_into(">H", pvd, 122, 1)
    struct.pack_into("<H", pvd, 124, 1)
    struct.pack_into(">H", pvd, 126, 1)
    struct.pack_into("<H", pvd, 128, SECTOR)
    struct.pack_into(">H", pvd, 130, SECTOR)
    struct.pack_into("<I", pvd, 132, len(tabla_l))
    struct.pack_into(">I", pvd, 136, len(tabla_l))
    struct.pack_into("<I", pvd, 140, 257)
    struct.pack_into("<I", pvd, 144, 258)
    struct.pack_into(">I", pvd, 148, 259)
    struct.pack_into(">I", pvd, 152, 260)
    pvd[156:190] = bytes([34]) + _registro(b"\x00", pos[""][0], pos[""][1], True, fecha)[1:34]
    for o, n in ((190, 128), (318, 128), (446, 128), (574, 128)):
        pvd[o:o + n] = b" " * n
    for o in (702, 739, 776):
        pvd[o:o + 37] = b" " * 37
    pvd[813:830] = fecha_pvd
    pvd[830:847] = fecha_pvd
    pvd[847:864] = b"0" * 16 + b"\x00"
    pvd[864:881] = b"0" * 16 + b"\x00"
    pvd[881] = 1

    os.makedirs(os.path.dirname(os.path.abspath(salida)), exist_ok=True)
    temporal = salida + ".parcial"
    with open(temporal, "wb") as f:
        f.write(bytes(16 * SECTOR))
        f.write(pvd)
        f.write(b"\xffCD001\x01".ljust(SECTOR, b"\0"))
        for marca in (b"BEA01", b"NSR02", b"TEA01"):    # como en un DVD de PS2; sin ellas PCSX2 ve un CD y el juego se cuelga
            f.write((b"\0" + marca + b"\x01").ljust(SECTOR, b"\0"))
        f.write(bytes((257 - 21) * SECTOR))
        for t in (tabla_l, tabla_l, tabla_m, tabla_m):
            f.write(bytes(t).ljust(SECTOR, b"\0"))
        for d in orden_dirs:
            prefijo = "/" + d + "/" if d else "/"
            hijos = []
            for n, es_dir in dirs[d]:
                if es_dir:
                    sub = (d + "/" + n) if d else n
                    hijos.append((n, pos[sub][0], pos[sub][1], True))
                else:
                    r = prefijo + n
                    hijos.append((n, lbas[r], ficheros[r][0], False))
            padre = pos[d.rsplit("/", 1)[0] if "/" in d else ""]
            f.write(bytes(_dir_datos(hijos, pos[d], padre, fecha)).ljust(_sectores(pos[d][1]) * SECTOR, b"\0"))
        for r in primero + resto:
            if f.tell() != lbas[r] * SECTOR:
                raise OSError("descuadre al escribir %s" % r)
            progreso("Escribiendo %s (%d MB)" % (r, ficheros[r][0] >> 20))
            ficheros[r][1](f)
            sobra = f.tell() % SECTOR
            if sobra:
                f.write(bytes(SECTOR - sobra))
    os.replace(temporal, salida)
    return total * SECTOR


def montar(copia, salida, mods=None, solo=None, progreso=print):
    """ISO nueva = copia + mods encendidos. Devuelve {segundos, bytes, entradas, conflictos}."""
    t0 = time.perf_counter()
    if os.path.abspath(salida).lower() == os.path.abspath(copia.ruta).lower():
        raise CopiaNoValida("La ISO de salida no puede ser tu copia original: elige otro nombre.")
    sobre, duenos, conflictos = {}, {}, []
    for nom, encendido, tabla in lista_mods(mods):
        if not encendido if solo is None else nom not in solo:
            continue
        for afs, entradas in tabla.items():
            for n, ruta in entradas.items():
                if (afs, n) in duenos:
                    conflictos.append("%s %d: gana %s sobre %s" % (afs, n, nom, duenos[afs, n]))
                duenos[afs, n] = nom
                sobre.setdefault(afs, {})[n] = ruta
    salen = {}
    for r, (tam, abrir) in copia.ficheros.items():
        afs = r.rsplit("/", 1)[-1].lower()
        if afs in sobre:
            salen[r] = _afs_parcheado(copia, afs, sobre[afs])
            continue

        def escribe(f, abrir=abrir, tam=tam):
            with abrir() as s:
                _copiar(s, f, tam)
        salen[r] = (tam, escribe)
    tam = sum(v[0] for v in salen.values()) + (1 << 20)
    libre = shutil.disk_usage(os.path.dirname(os.path.abspath(salida)) or ".").free
    if libre < tam:
        raise CopiaNoValida("No cabe: la ISO ocupa %.1f GB y en ese disco quedan %.1f GB." % (tam / 2**30, libre / 2**30))
    total = escribir_iso(salida, salen, progreso)
    return {"segundos": time.perf_counter() - t0, "bytes": total, "entradas": sum(map(len, sobre.values())),
            "conflictos": conflictos}


def _afs_parcheado(copia, nombre, sobre):
    nueva, final = _plan_afs(copia, nombre, sobre)
    tabla = copia.tabla(nombre)
    abrir = copia.ficheros[copia.afs(nombre)][1]

    def escribe(f):
        with abrir() as s:
            cab = bytearray(s.read(tabla[0][0]))
            struct.pack_into("<%dI" % (2 * len(nueva)), cab, 8, *[v for e in nueva for v in e])
            f.write(cab)
            for n, ((off, tam), (_o, nuevo)) in enumerate(zip(tabla, nueva)):
                if n in sobre:
                    with open(sobre[n], "rb") as m:
                        f.write(m.read())
                else:
                    s.seek(off)
                    _copiar(s, f, tam)
                if n < len(nueva) - 1 and nuevo % SECTOR:
                    f.write(bytes(SECTOR - nuevo % SECTOR))
            s.seek(tabla[-1][0] + tabla[-1][1])
            f.write(s.read())
    return final, escribe


# ---------------------------------------------------------------- comprobacion sin juego
def prueba():
    import numpy as np
    import amt_ps2
    with tempfile.TemporaryDirectory(dir=os.environ.get("DBZ3_TMP") or None) as tmp:
        origen = os.path.join(tmp, "copia")
        usr = os.path.join(origen, "USR")
        os.makedirs(usr)
        with open(os.path.join(origen, "SYSTEM.CNF"), "wb") as f:
            f.write(b"BOOT2 = cdrom0:\\SLUS_209.98;1\r\nVER = 1.00\r\n")
        with open(os.path.join(origen, "SLUS_209.98"), "wb") as f:
            f.write(b"ELF" * 1000)
        entradas = [b"A" * 100, b"B" * 3000, b"C" * 50]
        tabla, cuerpo = [], bytearray()
        base = 0x1000
        for e in entradas:
            tabla.append((base + len(cuerpo), len(e)))
            cuerpo += e + bytes(-len(e) % SECTOR)
        with open(os.path.join(usr, "data_cmn.afs"), "wb") as f:
            f.write(b"AFS\0" + struct.pack("<I", len(entradas)))
            f.write(b"".join(struct.pack("<II", *t) for t in tabla))
            f.write(bytes(base - f.tell()))
            f.write(cuerpo)
        mods = os.path.join(tmp, "mods")
        for n, d in ((0, b"X" * 200), (2, b"Y" * 5000)):   # uno cabe en su hueco, otro va al final
            with open(ruta_mod("m", "data_cmn.afs", n, mods), "wb") as f:
                f.write(d)
        with open(ruta_mod("apagado", "data_cmn.afs", 1, mods), "wb") as f:
            f.write(b"Z")
        open(os.path.join(mods, "apagado", ".disabled"), "w").close()
        copia = Copia(origen)
        salida = os.path.join(tmp, "s.iso")
        r = montar(copia, salida, mods, progreso=lambda _m: None)
        assert r["entradas"] == 2, r
        hecha = Copia(salida)    # la ISO generada se lee con el lector normal
        assert hecha.entrada("data_cmn.afs", 0) == b"X" * 200
        assert hecha.entrada("data_cmn.afs", 1) == b"B" * 3000
        assert hecha.entrada("data_cmn.afs", 2) == b"Y" * 5000
        assert copia.entrada("data_cmn.afs", 0) == b"A" * 100, "la copia original no se toca"
        try:
            montar(hecha, salida, mods, progreso=lambda _m: None)
            raise AssertionError("no debe escribir sobre su propia copia")
        except CopiaNoValida:
            pass
        hecha.cerrar()
        # textura: ida y vuelta de 4 y 8 bpp, con CSM1 y alfa del GS
        for psm, bits, nclut in ((0x14, 4, 16), (0x13, 8, 256)):
            w, h = 16, 8
            raw = w * h * bits // 8
            cab = 0x20 + 4
            ent = cab
            datos = ent + 0x30
            clut = datos + 0x20 + raw
            amt = bytearray(clut + 0x20 + 4 * nclut)
            amt[0:4] = b"#AMT"
            struct.pack_into("<II", amt, 0x10, 1, 0x20)
            struct.pack_into("<I", amt, 0x20, ent)
            struct.pack_into("<3I2H2H7I", amt, ent, 0, 0x21, psm, 4, 3, w, h, datos, raw, 1, 0, clut, 4 * nclut, 0)
            t = amt_ps2.entries(bytes(amt))[0]
            img = np.zeros((h, w, 4), dtype=np.uint8)
            img[..., 0] = (np.arange(w * h).reshape(h, w) % (nclut - 1)) * (256 // nclut)
            img[..., 3] = 255
            img[0, 0] = (1, 2, 3, 0)
            codificar_textura(amt, t, img)
            assert np.array_equal(amt_ps2.decode(bytes(amt), t), img), "textura %d bpp" % bits
    try:
        Copia("x.chd")
    except CopiaNoValida as e:
        assert "chdman" in str(e)
    crudo = bytearray(0x24 + 36 * 4)
    crudo[:0x12] = bytes.fromhex("8000002003120402") + struct.pack(">II", 48000, 128) + bytes.fromhex("01f4")
    crudo[0x12] = 3
    crudo[0x22:0x24] = b"RI"
    hecho = con_bucle(bytes(crudo))
    assert struct.unpack(">5I", hecho[0x24:0x38]) == (1, 0, SECTOR, 128, SECTOR + 144) and hecho[0x12] == 4
    assert hecho[SECTOR - 6:SECTOR] == b"(c)CRI" and len(hecho) == SECTOR + 144
    assert con_bucle(hecho) == hecho, "uno con bucle no se toca"
    print("ps2_b3.py: prueba OK")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("info")
    s.add_argument("copia")
    s = sub.add_parser("texturas")
    s.add_argument("copia")
    s.add_argument("entrada", type=int)
    s.add_argument("carpeta")
    s = sub.add_parser("textura")
    s.add_argument("copia")
    s.add_argument("entrada", type=int)
    s.add_argument("carpeta")
    s.add_argument("mod")
    s = sub.add_parser("personaje")
    s.add_argument("copia")
    s.add_argument("quien", choices=sorted(PERSONAJES))
    s.add_argument("por_quien", choices=sorted(PERSONAJES))
    s.add_argument("mod")
    s = sub.add_parser("musica")
    s.add_argument("audio")
    s.add_argument("entrada", type=int)
    s.add_argument("mod")
    s = sub.add_parser("montar")
    s.add_argument("copia")
    s.add_argument("salida")
    s.add_argument("--mods")
    s.add_argument("--solo")
    sub.add_parser("prueba")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "prueba":
            prueba()
        elif a.cmd == "info":
            c = Copia(a.copia)
            print("Copia válida:", c.serie, "| %d ficheros | %.2f GB" % (
                len(c.ficheros), sum(v[0] for v in c.ficheros.values()) / 2**30))
        elif a.cmd == "texturas":
            for nom, w, h in sacar_texturas(Copia(a.copia).entrada("data_cmn.afs", a.entrada), a.carpeta):
                print("%s  %dx%d" % (nom, w, h))
        elif a.cmd == "textura":
            b, n, avisos = aplicar_texturas(Copia(a.copia).entrada("data_cmn.afs", a.entrada), a.carpeta)
            for av in avisos:
                print("AVISO:", av)
            if not n:
                print("Ninguna textura cambió respecto al original: no se crea nada.")
                return 1
            with open(ruta_mod(a.mod, "data_cmn.afs", a.entrada), "wb") as f:
                f.write(b)
            print("Mod %s: %d texturas cambiadas en la entrada %d." % (a.mod, n, a.entrada))
        elif a.cmd == "personaje":
            print("Mod %s: %d entradas." % (a.mod, len(mod_personaje(Copia(a.copia), a.quien, a.por_quien, a.mod))))
        elif a.cmd == "musica":
            hz, can, seg = mod_musica(a.audio, a.entrada, a.mod)
            print("Mod %s: %d Hz, %d canales, %d s." % (a.mod, hz, can, seg))
        elif a.cmd == "montar":
            solo = set(a.solo.split(",")) if a.solo else None
            r = montar(Copia(a.copia), a.salida, a.mods, solo)
            for c in r["conflictos"]:
                print("AVISO:", c)
            print("ISO lista: %s (%.2f GB, %d entradas cambiadas, %.0f s)" % (
                a.salida, r["bytes"] / 2**30, r["entradas"], r["segundos"]))
    except (CopiaNoValida, RuntimeError, OSError) as e:
        print("ERROR:", e)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

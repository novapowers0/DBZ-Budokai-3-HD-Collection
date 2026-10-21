#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ut_formato.py - Contenedores y modelo del creador de Ultimate Tenkaichi (X360).

Modulo AUTOCONTENIDO para `mod center hd/cac/` (frente 02, studies 6). Reune en un
solo fichero lo que studies 5/04 tenia en `stpz_ut2.py` + `sprp_ut.py` + el lector
XDVDFS, y anade la lectura del skin (pesos/indices, usage 0x06/0x07) y una API de
GRUPOS (una malla por VBUF, con su nombre de hueso/material) lista para el
conversor a B3 HD (`ut_to_b3.py`).

Cadena del formato (spec en studies 5/04, datos/ESPECIFICACION_STPZ.md):

    AFS (XDVDFS) -> entrada .zpak (STPZ, LZSS de bloques)
        -> STPK (.pak, tabla de 48 B) -> .spr / .ioram / .vram / .dat

  .spr     = arbol SPR3 (secciones TX2D/MTRL/SHAP/VBUF/SCNE/BONE/...)
  .ioram   = buffers de vertices + IB (los VBUF del .spr apuntan AQUI)
  .vram    = texturas tiled 360 (TX2D del .spr describen offset/tamano)

Implementacion propia (no se copia codigo de terceros); se contrasta solo como
referencia con `adsl14/Raging-tools` (GPL-3.0, ver INFORME.md de studies 5).

Uso:
    python ut_formato.py info    <pieza.spr[.zpak]> [<pieza.ioram>]
    python ut_formato.py grupos  <pieza.spr[.zpak]> [<pieza.ioram>]
    python ut_formato.py tex     <pieza.spr[.zpak]>
    python ut_formato.py iso <iso> <afs-en-iso> <idx> <dst>
    python ut_formato.py selftest
"""
import os
import struct
import sys

MAGIC_STPZ = b"STPZ"
MAGIC_STPK = b"STPK"
MAGIC_SPR = (b"SPR3", b"SPRP", b"SPR\x00")
BLOQUE = 0x3C00                      # bytes que rinde cada bloque LZSS (constante)
SECTOR = 2048
MAGIC_XDVDFS = b"MICROSOFT*XBOX*MEDIA"


# ---------------------------------------------------------------------------
# LZSS de bloque (tokens de 1 byte)
# ---------------------------------------------------------------------------
def lzss_descomprimir(data: bytes) -> bytes:
    """Bloque LZSS de Spike: token = 1 byte, size = token>>1.
    token&1 == 0 -> `size` literales; token&1 == 1 -> 1 byte de distancia y copia
    solapada `size` veces desde out[len-off]."""
    out = bytearray()
    pos, n = 0, len(data)
    while pos < n:
        tok = data[pos]
        pos += 1
        size = tok >> 1
        if tok & 1:
            if pos >= n:
                break
            off = data[pos]
            pos += 1
            i = len(out) - off
            if i < 0:
                raise ValueError("LZSS: distancia %d > salida %d" % (off, len(out)))
            for _ in range(size):
                out.append(out[i])
                i += 1
        else:
            out += data[pos:pos + size]
            pos += size
    return bytes(out)


class Stpz:
    """Cabecera STPZ + flujo LZSS -> datos del STPK."""

    def __init__(self, datos: bytes):
        if datos[:4] != MAGIC_STPZ:
            raise ValueError("no es STPZ: %r" % datos[:4])
        self.version, self.n_streams, self.alineacion = struct.unpack_from(">III", datos, 4)
        off = 0x10
        if datos[off:off + 4] != b"SCD0":
            raise ValueError("se esperaba SCD0 en 0x10, hay %r" % datos[off:off + 4])
        self.tam_sin_comprimir, self.tam_total, self.bloque_unc = struct.unpack_from(">III", datos, off + 4)
        self.bloques = []
        off += 16
        fin = 0x10 + self.tam_total
        salida = bytearray()
        while off + 16 <= len(datos) and off < fin and datos[off:off + 4] == b"SCL0":
            b_unc, b_tot, _b3 = struct.unpack_from(">III", datos, off + 4)
            dec = lzss_descomprimir(datos[off + 16: off + b_tot])
            self.bloques.append((off, b_tot, b_tot - 16, len(dec)))
            salida += dec
            off += b_tot
        self.datos = bytes(salida)

    def info(self):
        return ("STPZ v%d n_streams=%d align=0x%X sin_comprimir=%d bloques=%d descomprimido=%d"
                % (self.version, self.n_streams, self.alineacion, self.tam_sin_comprimir,
                   len(self.bloques), len(self.datos)))


class Stpk:
    """Vista de un .pak (STPK): tabla de ficheros de 48 B."""

    def __init__(self, datos: bytes):
        if datos[:4] != MAGIC_STPK:
            raise ValueError("no es STPK: %r" % datos[:4])
        self.datos = datos
        self.version = struct.unpack_from(">I", datos, 4)[0]
        self.n = struct.unpack_from(">I", datos, 8)[0]
        self.tipo = datos[12:16]
        self.entradas = []
        for i in range(self.n):
            rec = 16 + i * 48
            off, size = struct.unpack_from(">II", datos, rec)
            nombre = datos[rec + 16:rec + 48].split(b"\x00")[0].decode("utf-8", "replace")
            self.entradas.append((nombre, off, size))

    def sub(self, clave):
        if isinstance(clave, int):
            nombre, off, size = self.entradas[clave]
        else:
            for nombre, off, size in self.entradas:
                if nombre == clave:
                    break
            else:
                raise KeyError(clave)
        return self.datos[off:off + size]


def abrir_contenedor(datos: bytes):
    """(Stpz|None, Stpk) aceptando .zpak o .pak."""
    if datos[:4] == MAGIC_STPZ:
        stpz = Stpz(datos)
        return stpz, Stpk(stpz.datos)
    if datos[:4] == MAGIC_STPK:
        return None, Stpk(datos)
    raise ValueError("ni STPZ ni STPK: %r" % datos[:4])


# ---------------------------------------------------------------------------
# SPR3: cabecera + arbol de secciones
# ---------------------------------------------------------------------------
class Spr:
    """Modelo SPR3. `ioram` son los bytes del fichero hermano con VBUF e IB."""

    def __init__(self, datos: bytes, ioram: bytes = b""):
        self.d = datos
        self.ioram = ioram or b""
        if datos[:4] not in MAGIC_SPR:
            raise ValueError("no es SPR3/SPRP: %r" % datos[:4])
        self.data_tag = datos[0:4]
        self.entry_count = struct.unpack_from(">I", datos, 8)[0]
        (self.name_offset, self.entry_info_size, self.string_table_size, self.data_info_size,
         self.data_block_size, self.ioram_name_offset, self.ioram_data_size,
         self.vram_name_offset, self.vram_data_size) = struct.unpack_from(">IIIIIIIII", datos, 0x10)
        self.entry_info_base = 64
        self.string_table_base = self.entry_info_base + self.entry_info_size
        self.data_info_base = self.string_table_base + self.string_table_size
        self.data_block_base = self.data_info_base + self.data_info_size
        self.tipos = []
        self._leer()

    def cstr(self, rel):
        if rel == 0:
            return ""
        p = self.string_table_base + rel
        e = self.d.find(b"\x00", p)
        return self.d[p:(e if e >= 0 else p + 64)].decode("utf-8", "replace")

    def _leer(self):
        tipo_off = 0
        for i in range(self.entry_count):
            rec = self.entry_info_base + i * 12
            tipo = self.d[rec:rec + 4]
            n = struct.unpack_from(">I", self.d, rec + 8)[0]
            ents = []
            for j in range(n):
                b = self.data_info_base + tipo_off + j * 32
                (dt, idx, name_off, data_off, data_size, child_count, child_off, _) = \
                    struct.unpack_from(">4sIIIIIiI", self.d, b)
                e = {"tipo": dt, "index": idx, "nombre": self.cstr(name_off),
                     "data_offset": data_off, "data_size": data_size,
                     "child_count": child_count, "child_offset": child_off, "hijos": []}
                if child_count > 0:
                    self._hijos(e)
                ents.append(e)
            self.tipos.append((tipo, ents))
            tipo_off += n * 32

    def _hijos(self, padre):
        p = self.data_block_base + padre["child_offset"]
        for k in range(padre["child_count"]):
            b = p + k * 20
            (name_off, data_off, data_size, child_count, child_off) = struct.unpack_from(">IIIII", self.d, b)
            h = {"nombre": self.cstr(name_off), "data_offset": data_off, "data_size": data_size,
                 "child_count": child_count, "child_offset": child_off, "hijos": []}
            if child_count > 0:
                self._hijos(h)
            padre["hijos"].append(h)

    def seccion(self, tag):
        for t, ents in self.tipos:
            if t == tag:
                return ents
        return []

    def bloque(self, data_offset, data_size):
        p = self.data_block_base + data_offset
        return self.d[p:p + data_size]


USAGE = {0x00: "pos", 0x02: "nrm", 0x03: "tan", 0x05: "uv",
         0x06: "skin_w", 0x07: "skin_i"}


class Grup:
    """Un VBUF de UT: una malla con su nombre `<forma>Shape<n>:<hueso/material>`."""

    def __init__(self, spr, e):
        self.nombre = e["nombre"]
        self.spr = spr
        b = spr.bloque(e["data_offset"], e["data_size"])
        self.ut = (e["data_size"] == 40)
        self.data_offset = struct.unpack_from(">I", b, 8)[0]
        self.data_size = struct.unpack_from(">I", b, 12)[0]
        self.vertex_count = struct.unpack_from(">I", b, 16)[0]
        o = 20
        if self.ut:
            self.index_count = struct.unpack_from(">I", b, o)[0]
            o += 4
        self.decl_count_0 = struct.unpack_from(">H", b, o + 4)[0]
        self.decl_offset = struct.unpack_from(">I", b, o + 8)[0]
        o += 12
        self.index_offset = struct.unpack_from(">I", b, o)[0] if self.ut else 0
        if not self.ut:
            self.index_count = 0
        self.vertex_data = (spr.ioram if spr.ioram else spr.d)[self.data_offset:
                                                                self.data_offset + self.data_size]
        self.decls = []
        base = spr.data_block_base + self.decl_offset
        for k in range(self.decl_count_0):
            p = base + k * 20
            rno = struct.unpack_from(">I", spr.d, p + 4)[0]
            usage = struct.unpack_from(">H", spr.d, p + 8)[0]
            idx = struct.unpack_from(">H", spr.d, p + 10)[0]
            formato = struct.unpack_from(">H", spr.d, p + 12)[0]
            stride = struct.unpack_from(">H", spr.d, p + 14)[0]
            off = struct.unpack_from(">I", spr.d, p + 16)[0]
            self.decls.append({"usage": usage, "index": idx, "formato": formato, "stride": stride, "offset": off,
                               "nombre": spr.cstr(rno)})

    # -- helpers de declaraciones -------------------------------------------
    def _decl(self, usage, idx=0):
        return next((d for d in self.decls if d["usage"] == usage and d["index"] == idx), None)

    def hueso_del_nombre(self):
        """Nombre-clave del grupo: lo que va tras el ultimo ':' (p.ej. 'C_DOUGI_UE_03',
        'H_HAIR_00', 'S_FACE_02'). Es el hueso/material de la malla en el esqueleto UT."""
        n = self.nombre
        return n.split(":")[-1] if ":" in n else n

    def vertices(self):
        """[(x,y,z,nx,ny,nz,u,v,bone,peso)] en el espacio del modelo UT (bind-pose).
        bone/peso = del skin (usage 0x06/0x07); 0/1.0 si la malla no esta skinneada."""
        dpos = self._decl(0x00)
        if dpos is None or not dpos["stride"]:
            return []
        dnrm = self._decl(0x02)
        duv = self._decl(0x05)
        skin = self.influencias()
        out = []
        for i in range(self.vertex_count):
            off = i * dpos["stride"]
            x, y, z = struct.unpack_from(">fff", self.vertex_data, off + dpos["offset"])
            if dnrm:
                nx, ny, nz = struct.unpack_from(">fff", self.vertex_data, i * dnrm["stride"] + dnrm["offset"])
            else:
                nx = ny = nz = 0.0
            if duv:
                u, v = struct.unpack_from(">ff", self.vertex_data, i * duv["stride"] + duv["offset"])
            else:
                u = v = 0.0
            bone, peso = max(skin[i], key=lambda t: t[1]) if skin[i] else (0, 1.0)
            out.append((x, y, z, nx, ny, nz, u, v, bone, peso))
        return out

    def influencias(self):
        """Skin UT original: FLOAT1 BE (usage 6), u32 BE (usage 7), stride propio.

        Conserva todas las influencias: los índices son IDs BONE, no bytes ni materiales.
        La API vertices() mantiene su tupla histórica (influencia dominante).
        """
        out = [[] for _ in range(self.vertex_count)]
        for lane in range(4):
            wd, bd = self._decl(6, lane), self._decl(7, lane)
            if wd is None and bd is None:
                continue
            if wd is None or bd is None or wd["formato"] != 8 or bd["formato"] != 2:
                raise ValueError("skin UT no soportado en " + self.nombre)
            for i in range(self.vertex_count):
                w = struct.unpack_from(">f", self.vertex_data, wd["offset"] + i * wd["stride"])[0]
                b = struct.unpack_from(">I", self.vertex_data, bd["offset"] + i * bd["stride"])[0]
                if not 0 <= w <= 1:
                    raise ValueError("peso UT inválido en " + self.nombre)
                if w > 0:
                    out[i].append((b, w))
        return out

    def triangulos(self):
        ib = self.spr.ioram if self.spr.ioram else self.spr.d
        o = self.data_offset + self.index_offset
        idx = [struct.unpack_from(">H", ib, o + 2 * k)[0] for k in range(self.index_count)]
        tris = []
        for k in range(len(idx) - 2):          # strip
            a, b, c = idx[k], idx[k + 1], idx[k + 2]
            if len({a, b, c}) != 3 or max(a, b, c) >= self.vertex_count:
                continue
            if k % 2:
                a, b = b, a
            tris.append((a, b, c))
        return tris


def cargar_spr(path):
    """Devuelve (bytes del .spr crudo). Acepta .spr, .pak o .zpak con el .spr dentro."""
    datos = open(path, "rb").read()
    if datos[:4] in MAGIC_SPR:
        return datos
    _stpz, stpk = abrir_contenedor(datos)
    for i in range(stpk.n):
        nom, off, size = stpk.entradas[i]
        if nom.endswith(".spr"):
            return stpk.datos[off:off + size]
    return stpk.datos[stpk.entradas[0][1]:stpk.entradas[0][1] + stpk.entradas[0][2]]


def cargar(hermano_spr, hermano_ioram=None):
    """(Spr, ioram) a partir del .spr (+ .ioram opcional)."""
    spr = Spr(cargar_spr(hermano_spr), open(hermano_ioram, "rb").read() if hermano_ioram else b"")
    return spr


def grupos(spr):
    return [Grup(spr, e) for e in spr.seccion(b"VBUF")]


# ---------------------------------------------------------------------------
# TX2D / vram
# ---------------------------------------------------------------------------
ENC = {0x00: "RGBA", 0x08: "DXT1", 0x0F: "DXT1", 0x18: "DXT5", 0x1F: "DXT5", 0x20: "ATI2"}


def texturas(spr):
    out = []
    for e in spr.seccion(b"TX2D"):
        b = spr.bloque(e["data_offset"], e["data_size"])
        rel, doff, _u8, dsize, w, h, _u14, mips, _u18, _u1c, dxt = struct.unpack_from(">IIIIHHHHIIB", b, 0)
        out.append({"nombre": e["nombre"], "data_off": doff, "size": dsize, "w": w, "h": h,
                    "mips": mips, "enc": ENC.get(dxt, "0x%02X" % dxt)})
    return out


# ---------------------------------------------------------------------------
# Lector XDVDFS (ISO de UT) para extraer entradas del AFS
# ---------------------------------------------------------------------------
def _find_xdvdfs_base(fh, limit=0x30000000, step=0x10000):
    pos = 0
    while pos < limit:
        fh.seek(pos)
        d = fh.read(step)
        if not d:
            break
        i = d.find(MAGIC_XDVDFS)
        if i >= 0:
            return pos + i - 32 * SECTOR
        pos += step
    return None


class Xiso:
    def __init__(self, path):
        self.path = path
        self._f = open(path, "rb")
        base = _find_xdvdfs_base(self._f)
        if base is None:
            raise ValueError("no aparece MICROSOFT*XBOX*MEDIA: %s" % path)
        self.base = base
        vd = self._sector(32)
        root_sec, root_size = struct.unpack_from("<II", vd, 0x14)
        self._files = {}
        self._walk(root_sec, root_size, "")

    def _sector(self, sec, n=1):
        self._f.seek(self.base + sec * SECTOR)
        return self._f.read(n * SECTOR)

    def _walk(self, sec, size, prefix, depth=0):
        if depth > 16:
            return
        data = self._sector(sec, (size + SECTOR - 1) // SECTOR)
        seen = set()

        def visit(off):
            if off in seen or off + 14 > len(data):
                return
            seen.add(off)
            l, r, fsec, fsize, attrs, nlen = struct.unpack_from("<HHIIBB", data, off)
            name = data[off + 14:off + 14 + nlen].decode("utf-8", "replace")
            if l:
                visit(l * 4)
            if name not in ("", "."):
                full = prefix + "/" + name
                if attrs & 0x10:
                    self._walk(fsec, fsize, full, depth + 1)
                else:
                    self._files[full] = (fsec, fsize)
            if r:
                visit(r * 4)

        visit(0)

    def files(self):
        return dict(self._files)

    def open(self, path):
        return _XSub(self, path)


class _XSub:
    def __init__(self, x, path):
        self._x, self._sec, self._size, self._pos = x, x._files[path][0], x._files[path][1], 0

    def seek(self, off, whence=0):
        self._pos = off if whence == 0 else (self._pos + off if whence == 1 else self._size + off)
        return self._pos

    def read(self, n=-1):
        if n is None or n < 0:
            n = self._size - self._pos
        n = max(0, min(n, self._size - self._pos))
        self._x._f.seek(self._x.base + self._sec * SECTOR + self._pos)
        b = self._x._f.read(n)
        self._pos += len(b)
        return b


AFS_CHAR = "/data/X360/st_pack_battle_character_pt_x360.afs"


def afs_leer_entrada(fh, idx):
    fh.seek(8 + idx * 8)
    off, size = struct.unpack("<II", fh.read(8))
    fh.seek(off)
    return fh.read(size)


# ---------------------------------------------------------------------------
def _cli():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    if cmd == "selftest":
        src = bytes([0x10]) + b"STPK\x00\x00\x00\x01"
        assert lzss_descomprimir(src) == b"STPK\x00\x00\x00\x01"
        assert lzss_descomprimir(bytes([0x04]) + b"AB" + bytes([0x03, 0x02])) == b"ABA"
        print("selftest OK")
        return 0
    if cmd == "iso":
        d = Xiso(sys.argv[2])
        fh = d.open(sys.argv[3])
        datos = afs_leer_entrada(fh, int(sys.argv[4]))
        open(sys.argv[5], "wb").write(datos)
        print("escrito", sys.argv[5], len(datos), "bytes")
        return 0
    spr = cargar(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    if cmd == "info":
        print("SPR", spr.data_tag, "entry_count=%d ioram=(%s,%d) vram=(%s,%d)" % (
            spr.entry_count, spr.cstr(spr.ioram_name_offset), spr.ioram_data_size,
            spr.cstr(spr.vram_name_offset), spr.vram_data_size))
        for t, ents in spr.tipos:
            print("  seccion %r n=%d" % (t.decode("utf-8", "replace"), len(ents)))
    elif cmd == "grupos":
        for g in grupos(spr):
            print("%-44s verts=%-6d tris=%-6d hueso=%s" % (
                g.nombre, g.vertex_count, len(g.triangulos()), g.hueso_del_nombre()))
    elif cmd == "tex":
        for t in texturas(spr):
            print("%-28s off=0x%X sz=%d %dx%d mips=%d %s" % (
                t["nombre"], t["data_off"], t["size"], t["w"], t["h"], t["mips"], t["enc"]))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(_cli())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_leer.py - lectores de Dragon Ball Xenoverse 2 (E3 de studies 5). Solo lectura de la copia del jugador.

Reutiliza awo_tools/xeno_model.py (CPK + CRILAYLA, EMD, ESK, EMB): medido el 22/10/2026 que los EMD/ESK de XV2 usan
el mismo formato que XV1 (vertice comprimido 0x8207; solo cambia el sello de version +8: 0x0001 -> 0x92C0).
Nuevo aqui: EMM (cabecera 0x20 en XV2, 0x10 en XV1), dye (.dyt.emb) y BCS (partes, colores, cuerpos del CaC).
Layout del BCS: hechos de XV2-Tools (LazyBone152, MIT), contrastados con HUM.bcs real.

  cpk_xv2()                       Cpk de data.cpk de XV2 (DBZ3_XENO2_CPK o la carpeta de Steam)
  leer_emm(bytes)                 [{'nombre','shader','params':{nombre: float|int}}]
  leer_bcs(bytes)                 dict: version, raza, genero, partsets, colores, cuerpos
  dye_filas(emb_bytes)            [(nombre, RGBA)] de un .dyt.emb (paleta por filas)
"""
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "awo_tools"))
import xeno_model as xm  # noqa: E402

RUTA_XV2 = r"D:\SteamLibrary\steamapps\common\DB Xenoverse 2\cpk\data.cpk"


def carpetas_steam():
    """Bibliotecas de Steam: libraryfolders.vdf (Program Files) + las unidades habituales."""
    out = []
    for base in (r"C:\Program Files (x86)\Steam", r"C:\Program Files\Steam"):
        v = os.path.join(base, "steamapps", "libraryfolders.vdf")
        try:
            txt = open(v, encoding="utf-8", errors="replace").read()
            out += [m.replace("\\\\", "\\") for m in re.findall(r'"path"\s+"([^"]+)"', txt)]
        except OSError:
            pass
        out.append(base)
    for d in "CDEFGHIJ":
        out += [r"%s:\SteamLibrary" % d, r"%s:\Steam" % d]
    return list(dict.fromkeys(out))


def buscar_xv2():
    """Ruta del data.cpk de XV2 del jugador (DBZ3_XENO2_CPK o bibliotecas de Steam), o None. Solo se lee.

    Si DBZ3_XENO2_CPK viene definida se toma como la ruta a usar: si no existe, XV2 no esta instalado
    (asi se puede probar el modo sin XV2 apuntando a una ruta inexistente). Sin la variable se buscan
    la ruta habitual y las bibliotecas de Steam."""
    env = os.environ.get("DBZ3_XENO2_CPK", "")
    if env:
        return env if os.path.isfile(env) else None
    c = [RUTA_XV2]
    c += [os.path.join(b, "steamapps", "common", "DB Xenoverse 2", "cpk", "data.cpk") for b in carpetas_steam()]
    return next((x for x in c if x and os.path.isfile(x)), None)


_DISPONIBLE = []


def disponible():
    """True si el data.cpk de XV2 del jugador esta localizado (una sola comprobacion por proceso)."""
    if not _DISPONIBLE:
        _DISPONIBLE.append(buscar_xv2() is not None)
    return _DISPONIBLE[0]


class CpkRapido(xm.Cpk):
    """K: mismo Cpk, pero el indice (TOC, ~1,5 s de parseo) y las piezas ya descomprimidas (CRILAYLA, ~90 ms cada una)
    se guardan en disco: cache local del jugador (DBZ3_CAC_CACHE o TEMP/dbz3_xv2_cache), nunca se distribuye."""

    def __init__(self, ruta):
        import hashlib
        import pickle
        import tempfile
        self.ruta = ruta
        base = os.environ.get("DBZ3_CAC_CACHE") or os.path.join(tempfile.gettempdir(), "dbz3_xv2_cache")
        os.makedirs(base, exist_ok=True)
        st = os.stat(ruta)
        self.dir = os.path.join(base, hashlib.sha1(("%s|%d|%d" % (ruta, st.st_size, st.st_mtime_ns)).encode()).hexdigest()[:12])
        os.makedirs(self.dir, exist_ok=True)
        idx = os.path.join(self.dir, "toc.pkl")
        try:
            with open(idx, "rb") as fh:
                self.toc = pickle.load(fh)
        except Exception:  # noqa: BLE001
            self.toc = {n: (sz, o) for n, sz, o in xm.cri.cpk_toc(ruta)}
            with open(idx + ".tmp", "wb") as fh:
                pickle.dump(self.toc, fh, protocol=4)
            os.replace(idx + ".tmp", idx)
        self.f = open(ruta, "rb")
        self._mem = {}

    def get(self, nombre):
        if nombre in self._mem:
            return self._mem[nombre]
        import hashlib
        fp = os.path.join(self.dir, hashlib.sha1(nombre.encode()).hexdigest()[:20] + ".bin")
        try:
            with open(fp, "rb") as fh:
                b = fh.read()
        except OSError:
            b = super().get(nombre)
            try:
                with open(fp + ".tmp", "wb") as fh:
                    fh.write(b)
                os.replace(fp + ".tmp", fp)
            except OSError:
                pass
        self._mem[nombre] = b
        return b


_CPK = {}


def cpk_xv2():
    ruta = buscar_xv2()
    if not ruta:
        raise FileNotFoundError("no encuentro el data.cpk de Dragon Ball Xenoverse 2 (DBZ3_XENO2_CPK)")
    if ruta not in _CPK:
        _CPK[ruta] = CpkRapido(ruta)
    return _CPK[ruta]


def nombres_xv2(ruta_data=None):
    """{CLAVE: nombre en ingles} de proper_noun_character_name_en.msg (data1.cpk, junto a data.cpk)."""
    ruta = ruta_data or buscar_xv2()
    b = xm.Cpk(os.path.join(os.path.dirname(ruta), "data1.cpk")).get("data/msg/proper_noun_character_name_en.msg")

    def u(o):
        e = o
        while b[e:e + 2] != b"\0\0" or (e - o) % 2:
            e += 1
        return b[o:e].decode("utf-16-le")
    n, t2 = _u32(b, 8), _u32(b, 0x1C)
    out = {}
    for i in range(n):
        m = re.match(r"chara_([A-Za-z0-9]{3})_(\d+)$", u(_u32(b, 0x20 + 16 * i)))
        if m and m.group(2) == "000":
            out[m.group(1).upper()] = u(_u32(b, t2 + 16 * i))
    return out


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _i32(b, o):
    return struct.unpack_from("<i", b, o)[0]


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _i16(b, o):
    return struct.unpack_from("<h", b, o)[0]


def _s(b, o):
    return b[o:b.index(b"\0", o)].decode("latin1")


def _z(b):
    return b.split(b"\0")[0].decode("latin1")


# ------------------------------------------------------------------ EMM
def leer_emm(b):
    if b[:4] != b"#EMM":
        raise ValueError("no es EMM")
    h = _u16(b, 6)                       # tamano de cabecera: 0x10 (XV1) o 0x20 (XV2)
    n = _u32(b, h)
    out = []
    for i in range(n):
        m = h + _u32(b, h + 4 + 4 * i) + 0   # offsets relativos al fin de la cabecera de la tabla
        m = h + _u32(b, h + 4 + 4 * i)
        par = {}
        cnt = _u16(b, m + 64)
        for k in range(cnt):
            p = m + 68 + 40 * k
            nom, tipo = _z(b[p:p + 32]), _u32(b, p + 32)
            par[nom] = struct.unpack_from("<f" if tipo == 0 else "<I", b, p + 36)[0]
        out.append({"nombre": _z(b[m:m + 32]), "shader": _z(b[m + 32:m + 64]), "params": par})
    return out


# ------------------------------------------------------------------ dye
def dye_filas(b):
    """Cada textura de un .dyt.emb es una paleta: ancho = colores por zona (4 zonas: x 0..), alto = variantes."""
    return xm.leer_emb(b)


# ------------------------------------------------------------------ BCS
PARTES = ("FaceBase", "FaceForehead", "FaceEye", "FaceNose", "FaceEar", "Hair", "Bust", "Pants", "Rist", "Boots")


def _parte(b, B):
    d = {"modelo": _i16(b, B), "modelo2": _i16(b, B + 2), "textura": _i16(b, B + 4), "shader": _i16(b, B + 16),
         "flags": _u32(b, B + 24), "ocultar": _i32(b, B + 28), "ocultar_mat": _i32(b, B + 32),
         "codigo": b[B + 52:B + 56].split(b"\0")[0].decode("latin1")}
    for k, o in (("emd", 56), ("emm", 60), ("emb", 64), ("ean", 68)):
        r = _i32(b, B + o)
        d[k] = _s(b, B + r) if r else ""
    nc, oc = _i16(b, B + 18), _i32(b, B + 20)
    d["colores"] = [(_u16(b, B + oc + 4 * i), _u16(b, B + oc + 4 * i + 2)) for i in range(nc)] if oc else []
    npf, of = _i16(b, B + 74), _i32(b, B + 76)
    d["fisicas"] = []
    for i in range(npf if of else 0):
        E = B + of + 72 * i
        f = {"emd": _s(b, E + _i32(b, E + 40)) if _i32(b, E + 40) else "",
             "hueso": _s(b, E + 56 + 0) if False else "",
             "scd": _s(b, E + _i32(b, E + 60)) if _i32(b, E + 60) else ""}
        r = _i32(b, E + 56)
        f["hueso"] = _s(b, E + r) if r else ""
        d["fisicas"].append(f)
    return d


def leer_bcs(b):
    if b[:4] != b"#BCS" and any(b[:12]):      # 89 BCS de XV2 llevan los 12 primeros bytes a cero (medido): son validos
        raise ValueError("no es BCS")
    ver = _i16(b, 6)
    if ver not in (72, 76, 0):
        raise ValueError("version BCS desconocida %d" % ver)
    xv2 = ver != 72
    ns, nc, nb = _u16(b, 12), _u16(b, 14), _u16(b, 16)
    ops, opc, opb = (_i32(b, 24), _i32(b, 28), _i32(b, 32)) if xv2 else (_i32(b, 20), _i32(b, 24), _i32(b, 28))
    r, g = (b[44], b[45]) if xv2 else (b[32], b[33])
    sets = []
    for i in range(ns):
        P = _i32(b, ops + 4 * i)
        if not P:
            sets.append(None)
            continue
        if _i32(b, P + 20) != 10:
            raise ValueError("partset %d con %d partes" % (i, _i32(b, P + 20)))
        t = P + _i32(b, P + 24)
        ps = {}
        for k, nom in enumerate(PARTES):
            rel = _i32(b, t + 4 * k)
            if rel:
                ps[nom] = _parte(b, P + rel)
        sets.append(ps)
    cols = []
    for i in range(nc):
        C = _i32(b, opc + 4 * i)
        if not C:
            cols.append(None)
            continue
        n, lo = _i16(b, C + 10), _i32(b, C + 12)
        ent = [np.frombuffer(b, "<f4", 16, C + lo + 80 * j).reshape(4, 4).copy() for j in range(n)]
        cols.append({"nombre": _s(b, C + _i32(b, C)), "entradas": ent})
    cuerpos = []
    for i in range(nb):
        K = _i32(b, opb + 4 * i)
        n, lo = (_i16(b, K + 2), _i32(b, K + 4)) if K else (0, 0)
        esc = []
        for j in range(n if lo and K + lo + 16 * n <= len(b) else 0):    # TG3/TOK: cuerpo con puntero a cero (medido)
            E = K + lo + 16 * j
            esc.append((_s(b, E + _i32(b, E + 12)), struct.unpack_from("<3f", b, E)))
        cuerpos.append(esc)
    return {"version": "XV2" if xv2 else "XV1", "raza": r, "genero": g, "partsets": sets, "colores": cols,
            "cuerpos": cuerpos}




#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""t3.py - Lector de Dragon Ball Z Budokai Tenkaichi 3 (PS2) para el proyecto (S1-F08). Solo lectura.

Lee la ISO del propio jugador (nunca se reparte nada del juego): DBZ3_T3_ISO, o un *.iso / *.ruta de ps2_games con
"tenkaichi 3" (o "tenkaichi" sin "2"/"4"/"collection") en el nombre.

Formatos (medidos sobre la ISO PAL SLES-54945; detalle en studies 1\ENTREGAS\F08_tenkaichi3\INFORME.md):
  /DATA/PZS3EU1.AFS  3645 entradas, tabla de nombres al final (48 B/registro, Shift-JIS, nombres cortados).
  /DATA/PZS3EU2.AFS  65201 entradas: voces ADX mono 24 kHz, musica zs3_NN_US.adx (ADX v4 estereo 24 kHz),
                     ambientes MapSe_NN.adx.
  PAK  = u32 n + (n+1) u32 desplazamientos (el ultimo = tamano). Se anidan.
  BPE  = u32 tamano_final + u32 tamano_comprimido + bloques "byte pair encoding" de Philip Gage
         (tabla de pares + u16 BE de longitud por bloque). Lo usan las animaciones, menus, retratos,
         Dragon History y World Tournament.
  Personaje = 8 huecos de modelo (PAK de 252: [2] 'pmdl', [3..7] paquetes de geometria VIF, [0]/[12] texturas GS,
              [40..41] 'CMAn' camara, [43..51] lista de tecnicas UTF-16 en 9 idiomas, 'LIPS' labios)
              + PAK de animaciones (440, cada una BPE) + PAK de especiales (6).

  python t3.py info      [--iso ISO]
  python t3.py lista     [--iso ISO] [--afs 1|2] [--filtro REGEX]
  python t3.py personajes [--iso ISO] [--idioma es|en|de|fr|it|ja|ko]
  python t3.py musica    [--iso ISO]
  python t3.py extraer   AFS ENTRADA SALIDA [--sub K[,K2...]] [--bpe] [--iso ISO]
        AFS = 1 o 2; --sub baja por PAK anidados; --bpe descomprime. La salida va a una carpeta del jugador.
  python t3.py prueba    [--iso ISO]   autocomprobacion (sin ISO: BPE y PAK sinteticos; con ISO: 440/440 anims)
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import iso as isomod  # noqa: E402

IDIOMA_SUB = {"ja": 43, "en": 44, "es": 47, "de": 48, "fr": 49, "it": 50, "ko": 51}


# --------------------------------------------------------------------------------------------- localizar la ISO
def buscar_iso(explicita=None):
    if explicita:
        return explicita
    env = os.environ.get("DBZ3_T3_ISO")
    if env and os.path.exists(env):
        return env
    raiz = os.path.join(os.path.dirname(HERE), "ps2_games")
    if not os.path.isdir(raiz):
        return None
    for fn in sorted(os.listdir(raiz)):
        low = fn.lower()
        if not low.endswith((".iso", ".ruta")) or "tenkaichi" not in low:
            continue
        if "tenkaichi 3" in low or not re.search(r"tenkaichi (2|4)|collection|dx", low):
            hit = isomod.ruta_destino(os.path.join(raiz, fn))
            if hit:
                return hit
    return None


# --------------------------------------------------------------------------------------------- contenedores
def pak(d):
    """[(desplazamiento, tamano)] de un PAK, o None si no lo parece."""
    if len(d) < 8:
        return None
    n = struct.unpack_from("<I", d, 0)[0]
    if n == 0 or n > 4096 or 4 + 4 * (n + 1) > len(d):
        return None
    offs = struct.unpack_from("<%dI" % (n + 1), d, 4)
    if offs[-1] != len(d) and not (offs[-1] <= len(d) and len(d) - offs[-1] < 0x800):
        return None
    if any(offs[k] > offs[k + 1] for k in range(n)) or offs[0] < 4 + 4 * (n + 1):
        return None
    return [(offs[k], offs[k + 1] - offs[k]) for k in range(n)]


def bpe_bloques(src, pos, fin, limite=None):
    """Descompresor BPE de Philip Gage (formato por bloques). Devuelve bytes."""
    out = bytearray()
    while pos < fin and (limite is None or len(out) < limite):
        left = list(range(256))
        right = [0] * 256
        c = 0
        while True:
            count = src[pos]
            pos += 1
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            for _ in range(count + 1):
                left[c] = src[pos]
                pos += 1
                if left[c] != c:
                    right[c] = src[pos]
                    pos += 1
                c += 1
            if c == 256:
                break
        size = (src[pos] << 8) | src[pos + 1]
        pos += 2
        pila, i = [], 0
        while i < size or pila:
            if pila:
                b = pila.pop()
            else:
                b = src[pos]
                pos += 1
                i += 1
            if b == left[b]:
                out.append(b)
            else:
                pila.append(right[b])
                pila.append(left[b])
    return bytes(out)


def es_bpe(d):
    if len(d) < 12:
        return False
    dec, comp = struct.unpack_from("<II", d, 0)
    return 0 < comp <= len(d) - 8 and len(d) - 8 - comp < 0x800 and dec < 64 << 20 and pak(d) is None


def bpe(d):
    dec, comp = struct.unpack_from("<II", d, 0)
    out = bpe_bloques(d, 8, 8 + comp, dec)
    if len(out) != dec:
        raise ValueError("BPE: salen %d bytes, la cabecera dice %d" % (len(out), dec))
    return out


# --------------------------------------------------------------------------------------------- AFS dentro de la ISO
class Afs:
    def __init__(self, img, ruta):
        self.f = img.open(ruta)
        h = self.f.read(8)
        if h[:3] != b"AFS":
            raise ValueError("no es AFS: %s" % ruta)
        n = struct.unpack_from("<I", h, 4)[0]
        tab = self.f.read(8 * n + 8)
        self.ent = [struct.unpack_from("<II", tab, 8 * k) for k in range(n)]
        no, ns = struct.unpack_from("<II", tab, 8 * n)
        self.nombres = [""] * n
        if no and ns >= 48 * n:
            self.f.seek(no)
            nd = self.f.read(48 * n)
            self.nombres = [nd[48 * k:48 * k + 32].split(b"\0")[0].decode("cp932", "replace") for k in range(n)]

    def __len__(self):
        return len(self.ent)

    def leer(self, k, n=None):
        o, s = self.ent[k]
        self.f.seek(o)
        return self.f.read(s if n is None else min(n, s))


class T3:
    def __init__(self, ruta):
        self.ruta = ruta
        self.img = isomod.Iso(ruta)
        fs = self.img.files()
        self.afs = {}
        for k in ("1", "2"):
            hit = next((p for p in sorted(fs) if re.fullmatch(r"/DATA/PZS3[A-Z]{2}%s\.AF[SL]" % k, p.upper())), None)
            if hit:
                self.afs[k] = Afs(self.img, hit)
        if "1" not in self.afs:
            raise ValueError("no parece Tenkaichi 3 (falta /DATA/PZS3xx1.AFS): %s" % ruta)
        self.elf = self.img.boot_elf()

    def personajes(self):
        """[(entrada_anim, [huecos de modelo], entrada_especiales)] buscando los PAK de 440 animaciones."""
        a = self.afs["1"]
        out = []
        for k in range(len(a)):
            cab = a.leer(k, 8)
            if len(cab) == 8 and struct.unpack_from("<I", cab, 0)[0] == 440:
                modelos = [j for j in range(max(0, k - 8), k) if struct.unpack_from("<I", a.leer(j, 4), 0)[0] in (252, 1)]
                out.append((k, modelos, k + 1))
        return out

    def tecnicas(self, entrada_modelo, idioma="es"):
        d = self.afs["1"].leer(entrada_modelo)
        subs = pak(d)
        sub = IDIOMA_SUB.get(idioma, 44)
        if not subs or len(subs) <= sub:
            return []
        o, s = subs[sub]
        t = d[o:o + s]
        if t[:2] != b"\xff\xfe":
            return []
        txt = t[2:].decode("utf-16-le", "replace")
        return [ln[6:].strip() for ln in txt.split("\n") if ln.startswith("!F*")]   # "!F*NNN<nombre>"


# --------------------------------------------------------------------------------------------- comandos
def abrir(a):
    ruta = buscar_iso(a.iso)
    if not ruta:
        raise SystemExit("sin ISO de Tenkaichi 3 (define DBZ3_T3_ISO o ponla en ps2_games)")
    return T3(ruta)


def c_info(a):
    t = abrir(a)
    print("ISO      %s" % t.ruta)
    print("ELF      %s" % t.elf)
    for k, af in sorted(t.afs.items()):
        print("AFS %s    %d entradas" % (k, len(af)))
    print("personajes (PAK de 440 animaciones): %d" % len(t.personajes()))


def c_lista(a):
    t = abrir(a)
    af = t.afs[a.afs]
    rx = re.compile(a.filtro, re.I) if a.filtro else None
    for k in range(len(af)):
        if rx and not rx.search(af.nombres[k]):
            continue
        o, s = af.ent[k]
        cab = af.leer(k, 16)
        print("%d\t%s\t%d\t%s" % (k, af.nombres[k], s, cab.hex()))


def c_personajes(a):
    t = abrir(a)
    for anim, modelos, esp in t.personajes():
        llenos = [m for m in modelos if struct.unpack_from("<I", t.afs["1"].leer(m, 4), 0)[0] == 252]
        tec = t.tecnicas(llenos[0], a.idioma) if llenos else []
        print("%d\tmodelos=%s\tanim=%d\tespeciales=%d\t%s" % (anim, ",".join(map(str, llenos)), anim, esp,
                                                             " / ".join(tec[:4])))


def c_musica(a):
    t = abrir(a)
    af = t.afs.get("2")
    if not af:
        raise SystemExit("falta PZS3xx2.AFS")
    for k in range(len(af)):
        nm = af.nombres[k]
        if not nm.lower().endswith(".adx"):
            continue
        h = af.leer(k, 0x20)
        if h[:2] != b"\x80\x00":
            continue
        sr, ns = struct.unpack_from(">II", h, 8)
        print("%d\t%s\tcanales=%d\t%d Hz\t%.1f s" % (k, nm, h[7], sr, ns / float(sr or 1)))


def c_extraer(a):
    t = abrir(a)
    d = t.afs[a.afs].leer(a.entrada)
    for k in [int(x) for x in a.sub.split(",")] if a.sub else []:
        if es_bpe(d):
            d = bpe(d)
        subs = pak(d)
        if not subs or k >= len(subs):
            raise SystemExit("la entrada no es un PAK con sub %d" % k)
        o, s = subs[k]
        d = d[o:o + s]
    if a.bpe and es_bpe(d):
        d = bpe(d)
    salida = os.path.abspath(a.salida)
    if os.path.normcase(salida).startswith(os.path.normcase(os.path.dirname(HERE)) + os.sep):
        print("aviso: la salida esta dentro del proyecto; no la subas ni la repartas")
    os.makedirs(os.path.dirname(salida) or ".", exist_ok=True)
    with open(salida, "wb") as f:
        f.write(d)
    print("OK %s (%d bytes)" % (salida, len(d)))


def c_musica_mod(a):
    """Escribe mods/<mod>/us/adx_usa.afs/<n> con una pista de T3 (ADX 24 kHz estereo, con bucle en la cabecera) en cada
    entrada pedida, rellenada con ceros al tamano de la original. Se lee de la ISO del jugador; nada se reparte."""
    t = abrir(a)
    pista = t.afs["2"].leer(64974 + a.pista)          # zs3_00..19_US.adx
    if pista[:2] != b"\x80\x00":
        raise SystemExit("la entrada %d no es un ADX" % (64974 + a.pista))
    ent = []
    for r in a.entradas.split(","):
        lo, _, hi = r.partition("-")
        ent += range(int(lo), int(hi or lo) + 1)
    with open(a.afs, "rb") as f:
        tabla = f.read(8 + 8 * (max(ent) + 1))
    dest = os.path.join(a.mods, a.mod, "us", "adx_usa.afs")
    os.makedirs(dest, exist_ok=True)
    for i in ent:
        tam = struct.unpack_from("<I", tabla, 12 + 8 * i)[0]
        with open(os.path.join(dest, str(i)), "wb") as f:
            f.write(pista + b"\0" * max(0, tam - len(pista)))
    print("OK %d entradas en %s" % (len(ent), dest))


def _bpe_codificar_crudo(datos):
    """Bloques BPE sin pares (tabla identidad): sirve para probar el lector sin datos del juego."""
    out = bytearray()
    for i in range(0, len(datos), 0xFFFF):
        trozo = datos[i:i + 0xFFFF]
        out += bytes([0xFE, 0x7F, 0xFE, 0xFF, len(trozo) >> 8, len(trozo) & 0xFF]) + trozo
    return struct.pack("<II", len(datos), len(out)) + bytes(out)


def c_prueba(a):
    datos = bytes(range(256)) * 3
    assert bpe(_bpe_codificar_crudo(datos)) == datos
    # un bloque con un par: 0x80 -> (0x41, 0x42); el resto literal
    tabla = bytes([0xFF, 0x41, 0x42, 0xFE])  # salta 128 (0..127), par 128 = (A, B), salta 127 (129..255)
    cuerpo = bytes([0x80, 0x43, 0x80])
    blk = tabla + bytes([0, len(cuerpo)]) + cuerpo
    assert bpe(struct.pack("<II", 5, len(blk)) + blk) == b"ABCAB", bpe(struct.pack("<II", 5, len(blk)) + blk)
    p = struct.pack("<I3I", 2, 16, 20, 24) + b"abcdefgh"
    assert pak(p) == [(16, 4), (20, 4)]
    print("t3.py: prueba sin ISO OK")
    ruta = buscar_iso(a.iso)
    if not ruta:
        print("t3.py: sin ISO de Tenkaichi 3; prueba con ISO omitida")
        return
    t = T3(ruta)
    pj = t.personajes()
    assert pj, "no hay PAK de animaciones"
    d = t.afs["1"].leer(pj[0][0])
    ok = 0
    for o, s in pak(d):
        if s >= 8:
            bpe(d[o:o + s])
            ok += 1
    print("t3.py: %d personajes; animaciones del primero descomprimidas %d/%d OK" % (len(pj), ok, len(pak(d))))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    for nm in ("info", "lista", "personajes", "musica", "musica_mod", "extraer", "prueba"):
        s = sp.add_parser(nm)
        s.add_argument("--iso")
        if nm == "lista":
            s.add_argument("--afs", default="1", choices=("1", "2"))
            s.add_argument("--filtro")
        if nm == "personajes":
            s.add_argument("--idioma", default="es", choices=sorted(IDIOMA_SUB))
        if nm == "musica_mod":
            s.add_argument("mods")
            s.add_argument("--mod", default="t3_musica")
            s.add_argument("--pista", type=int, default=0)
            s.add_argument("--entradas", default="4187-4215")
            s.add_argument("--afs", default=os.path.join(os.path.dirname(HERE), "us", "adx_usa.afs"))
        if nm == "extraer":
            s.add_argument("afs", choices=("1", "2"))
            s.add_argument("entrada", type=int)
            s.add_argument("salida")
            s.add_argument("--sub")
            s.add_argument("--bpe", action="store_true")
    a = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    {"info": c_info, "lista": c_lista, "personajes": c_personajes, "musica": c_musica, "musica_mod": c_musica_mod,
     "extraer": c_extraer, "prueba": c_prueba}[a.cmd](a)


if __name__ == "__main__":
    main()

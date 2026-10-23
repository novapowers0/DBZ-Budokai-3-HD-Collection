#!/usr/bin/env python3
"""eu_map.py - pareja EU de cada funcion y dato del juego que usan los ficheros del HUB, modos, pasarela, escenarios,
Tag Team, campanas, select/roster/stage y ports_guard (frente eu, 2026-10-15).

  python tools/eu_map.py                                   # imagenes ya volcadas en D:\\DBZ3HD\\work\\eu
  python tools/eu_map.py --xex-us yae3_xenon.xex --xex-eu yae3_xenon_eu.xex   # las rehace (pycryptodome)
  python tools/eu_map.py --comprobar                       # solo la autoprueba (parejas ya medidas a mano)

Salidas: src/eu_map_gen.h (solo parejas verificadas: lo que compila el exe) y docs/14_frentes/eu_mapa.txt (todas,
con su evidencia). Metodo (ampliado de D:\\DBZ3HD\\work\\eu\\map_addrs.py, informe del roster EU 2026-10-07):
 - funciones: listas de generated/ y generated_eu/; cuerpo normalizado (inmediatos de direcciones y saltos
   enmascarados), alineacion por orden + hash unico. Se verifica que las diferencias crudas del par son solo
   direcciones (lis + desplazamiento sobre ese registro) o saltos relativos, y que cada `bl` de un llamador
   emparejado cae en la pareja EU.
 - codigo dentro de una funcion (lr de retorno, etiquetas): misma distancia en la funcion pareja; si la
   instruccion previa es un `bl`, su destino EU debe ser la pareja del destino US.
 - datos: cada sitio US que calcula la direccion (lis + D-form) se lee en la funcion EU pareja en la misma
   posicion (votos); si nadie la calcula, por la base mas cercana que si (campo de una estructura). Los datos
   inicializados se comparan byte a byte (los punteros dentro, por su pareja).
"""
import argparse
import bisect
import collections
import difflib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMG_DIR = Path(r"D:\DBZ3HD\work\eu")
B = 0x82000000
FUENTES = ["src/hub.cpp", "src/hub_menu.cpp", "src/ingame/hub_panel.cpp", "src/ingame/hub_panel.h", "src/modos.*",
           "src/net/*.cpp", "src/net/*.h", "src/ingame/online_panel.*",
           "src/pasarela*.cpp", "src/pasarela.h", "src/escenarios.cpp", "src/escenarios.h", "src/ports_guard.cpp",
           "src/tagteam*.cpp", "src/tagteam*.h", "src/ingame/tagteam_panel.*", "src/campaigns.cpp",
           "src/select_ext.cpp", "src/roster_ext.cpp", "src/roster_ext.h", "src/stage_ext.cpp", "src/stage_ext.h",
           # nuevos de la ronda 2 (escenarios, combate, rpg, transformaciones): tambien tendran que ir en EU
           "src/clima.*", "src/cine.*", "src/cinematica.*", "src/gigantes.*", "src/combate.*", "src/rpg.*", "src/transformaciones.*",
           "src/suelo_iw.*",   # frente R11: consulta de suelo y logica de luchadores (sub_8210E700)
           "src/iw_minijuegos.cpp",   # frente R16: tope de distancia entre luchadores (sub_82114910)
           # funciones del juego que copia el generador del selector de escenarios (su .inc EU las necesita)
           "tools/gen_escenarios_sel.py"]
GEN_H = ROOT / "src" / "eu_map_gen.h"
INFORME = ROOT / "docs" / "14_frentes" / "eu_mapa.txt"


# ---------------------------------------------------------------- imagenes
def xex2img(xex):
    """XEX2 retail (AES + compresion basica) -> volcado plano desde 0x82000000 (como D:\\DBZ3HD\\work\\eu\\xex2img)."""
    from Crypto.Cipher import AES
    d = Path(xex).read_bytes()
    _, _, pe_off, _, sec_off, n = struct.unpack(">4sIIIII", d[:24])
    hdr = {}
    for i in range(n):
        k, v = struct.unpack(">II", d[24 + i * 8:32 + i * 8])
        hdr[k >> 8] = v
    key = AES.new(bytes.fromhex("20B185A59D28FDC340583FBB0896BF91"), AES.MODE_ECB).decrypt(
        d[sec_off + 0x150:sec_off + 0x160])
    ffi = hdr[0x3]
    size, enc, comp = struct.unpack(">IHH", d[ffi:ffi + 8])
    assert enc == 1 and comp == 1, (enc, comp)
    raw = AES.new(key, AES.MODE_CBC, b"\0" * 16).decrypt(d[pe_off:][:(len(d) - pe_off) // 16 * 16])
    img, p = bytearray(), 0
    for b in range((size - 8) // 8):
        ds, zs = struct.unpack(">II", d[ffi + 8 + b * 8:ffi + 16 + b * 8])
        img += raw[p:p + ds] + b"\0" * zs
        p += ds
    assert img[:2] == b"MZ"
    out = bytearray(0x6D0000)
    out[:min(len(img), len(out))] = img[:len(out)]
    return bytes(out)


def secciones(img):
    e = struct.unpack_from("<I", img, 0x3C)[0]
    n, opt = struct.unpack_from("<H", img, e + 6)[0], struct.unpack_from("<H", img, e + 20)[0]
    s = e + 24 + opt
    out = {}
    for i in range(n):
        nm = img[s + 40 * i:s + 40 * i + 8].rstrip(b"\0").decode("latin-1")
        vs, va = struct.unpack_from("<II", img, s + 40 * i + 8)
        out[nm] = (B + va, B + va + vs)
    return out


class Imagen:
    def __init__(self, img, init_cpp):
        self.img = img
        self.sec = secciones(img)
        self.t0, self.t1 = self.sec[".text"]
        s = init_cpp.read_text(encoding="utf-8", errors="replace")
        self.funcs = sorted({int(m, 16) for m in re.findall(r"\{ 0x([0-9A-F]{8}), (?:dbz3eu_)?sub_", s)})
        self.fset = set(self.funcs)
        self.body = {}
        for i, a in enumerate(self.funcs):
            e = min(self.funcs[i + 1] if i + 1 < len(self.funcs) else self.t1, a + 0x8000)
            self.body[a] = tuple(norm(self.u32(x)) for x in range(a, e, 4))

    def u32(self, a):
        return struct.unpack_from(">I", self.img, a - B)[0]

    def contiene(self, a):
        """funcion que contiene la direccion de codigo a (o None)."""
        i = bisect.bisect_right(self.funcs, a) - 1
        if i < 0:
            return None
        f = self.funcs[i]
        return f if a < f + 4 * len(self.body[f]) else None

    def seccion(self, a):
        for nm, (x, y) in self.sec.items():
            if x <= a < y:
                return nm
        return "?"


DFORM = {7, 8, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29} | set(range(32, 56)) | {58, 62}
BASE_RA = {14} | set(range(32, 56)) | {58, 62}            # addi y cargas/guardados D/DS-form: base en rA
ESCRIBE_RT = {14, 15, 32, 33, 34, 35, 40, 41, 42, 43, 46, 58}  # escriben rD (GPR)


def norm(w):
    op = w >> 26
    if op == 18:
        return w & ~0x03FFFFFC
    if op == 16:
        return w & ~0xFFFC
    if op in DFORM:
        return w & ~0xFFFF
    return w


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def destino_salto(a, w):
    d = w & 0x03FFFFFC
    if d & 0x02000000:
        d -= 0x04000000
    return (a + d) & 0xFFFFFFFF


M32 = 0xFFFFFFFF
VOLATILES = {0} | set(range(3, 13))
UPDATE = {33, 35, 37, 39, 41, 43, 45, 49, 51, 53, 55}


def valores_pareja(us, eu, f, e, idx):
    """Recorre a la vez la funcion US f y su pareja EU e (mismas instrucciones salvo direcciones) siguiendo los
    registros con valor constante (lis/addis/addi/ori/add/mr) y anota en idx cada valor calculado (addi, ori, add)
    y cada direccion de carga/guardado: idx[valor US][valor EU] += 1."""
    lo, hi = B, B + len(us.img)
    ru, rv = {}, {}

    def pon(r, vu, ve, anota=True):
        ru[r], rv[r] = vu & M32, ve & M32
        if anota and lo <= ru[r] < hi:
            idx[ru[r]][rv[r]] += 1

    def quita(*rs):
        for r in rs:
            ru.pop(r, None)
            rv.pop(r, None)

    for k in range(len(us.body[f])):
        wu, we = us.u32(f + 4 * k), eu.u32(e + 4 * k)
        op, rd, ra = wu >> 26, (wu >> 21) & 31, (wu >> 16) & 31
        iu, ie = wu & 0xFFFF, we & 0xFFFF
        if op in (14, 15):                                   # addi / addis (lis si rA = 0)
            sh = 16 if op == 15 else 0
            if ra == 0:
                pon(rd, s16(iu) << sh, s16(ie) << sh, anota=False)
            elif ra in ru:
                pon(rd, ru[ra] + (s16(iu) << sh), rv[ra] + (s16(ie) << sh), anota=op == 14)
            else:
                quita(rd)
        elif op in (24, 25):                                 # ori / oris rA, rS
            sh = 16 if op == 25 else 0
            if rd in ru:
                pon(ra, ru[rd] | (iu << sh), rv[rd] | (ie << sh))
            else:
                quita(ra)
        elif op in BASE_RA or op in (58, 62):                # cargas y guardados D/DS-form
            if op in (58, 62):
                iu, ie = iu & 0xFFFC, ie & 0xFFFC
            if ra and ra in ru:
                eau, eae = (ru[ra] + s16(iu)) & M32, (rv[ra] + s16(ie)) & M32
                if lo <= eau < hi:
                    idx[eau][eae] += 1
                if op in UPDATE or (op in (58, 62) and wu & 3 == 1):
                    pon(ra, eau, eae, anota=False)
            elif op in UPDATE:
                quita(ra)
            if op in ESCRIBE_RT:
                quita(rd)
            elif op == 46:                                   # lmw
                quita(*range(rd, 32))
        elif op == 31:
            xo, rb = (wu >> 1) & 0x3FF, (wu >> 11) & 31
            if xo == 266 and ra in ru and rb in ru:          # add
                pon(rd, ru[ra] + ru[rb], rv[ra] + rv[rb])
            elif xo == 444 and rd == rb:                     # mr rA, rS
                if rd in ru:
                    pon(ra, ru[rd], rv[rd], anota=False)
                else:
                    quita(ra)
            else:
                quita(rd, ra)
        elif op in (7, 8, 12, 13):
            quita(rd)
        elif op in (20, 21, 23, 26, 27, 28, 29, 30):
            quita(ra)
        elif op in (18, 19) and wu & 1:                      # llamada: registros volatiles perdidos
            quita(*VOLATILES)


# ---------------------------------------------------------------- mapa
class Mapa:
    def __init__(self, us, eu):
        self.us, self.eu = us, eu
        uh = [hash(us.body[a]) for a in us.funcs]
        eh = [hash(eu.body[a]) for a in eu.funcs]
        sm = difflib.SequenceMatcher(None, uh, eh, autojunk=False)
        self.fmap, self.bloque = {}, {}
        for blk in sm.get_matching_blocks():
            for k in range(blk.size):
                self.fmap[us.funcs[blk.a + k]] = eu.funcs[blk.b + k]
                self.bloque[us.funcs[blk.a + k]] = blk.size
        self.by_hash = collections.defaultdict(list)
        for a in eu.funcs:
            self.by_hash[eu.body[a]].append(a)
        self.llamadores = collections.defaultdict(list)    # funcion US -> [(llamador, offset)]
        for f in us.funcs:
            for x in range(f, f + 4 * len(us.body[f]), 4):
                w = us.u32(x)
                if w >> 26 == 18 and not w & 2:
                    t = destino_salto(x, w)
                    if t in us.fset and (w & 1 or not f <= t < f + 4 * len(us.body[f])):
                        self.llamadores[t].append((f, x - f))
        self.us_hash = collections.Counter(us.body.values())
        # valores constantes de todo el codigo emparejado (US -> votos EU), en la misma instruccion de cada par
        self.valores = collections.defaultdict(collections.Counter)
        for f in us.funcs:
            e, _ = self.funcion(f)
            if e is not None and not self.difs_crudas(f, e):
                valores_pareja(us, eu, f, e, self.valores)
        self.claves = sorted(self.valores)

    def funcion(self, a):
        if a in self.fmap:
            return self.fmap[a], "alineada (bloque de %d iguales seguidas)" % self.bloque[a]
        body = self.us.body.get(a, ())
        c = self.by_hash.get(body, [])
        if len(c) == 1 and self.us_hash[body] == 1:
            return c[0], "unica"
        return None, "ambigua (US %d, EU %d)" % (self.us_hash[body], len(c))

    def difs_crudas(self, a, e):
        """palabras distintas no explicadas por direcciones (lis/lo16 sobre el registro del lis) ni saltos."""
        lis, malas, n = set(), [], len(self.us.body[a])
        for k in range(n):
            wu, we = self.us.u32(a + 4 * k), self.eu.u32(e + 4 * k)
            op = wu >> 26
            if op == 15 and (wu >> 16) & 31 == 0:
                lis.add((wu >> 21) & 31)
            if wu == we or op in (15, 16, 18):
                continue
            if op in BASE_RA and (wu >> 16) & 31 in lis:
                continue
            if op == 24 and (wu >> 21) & 31 in lis:
                continue
            malas.append(4 * k)
        return malas

    def llamadas(self, a, e):
        ok = mal = 0
        for cf, o in self.llamadores.get(a, []):
            ecf, _ = self.funcion(cf)
            if ecf is None:
                continue
            w = self.eu.u32(ecf + o)
            if w >> 26 == 18 and destino_salto(ecf + o, w) == e:
                ok += 1
            else:
                mal += 1
        return ok, mal

    def par_funcion(self, a):
        e, como = self.funcion(a)
        if e is None:
            return None, "sin pareja: " + como, False
        malas = self.difs_crudas(a, e)
        ok, mal = self.llamadas(a, e)
        ev = "%s, %d instr, difs sin explicar %d, llamadas %d bien / %d mal" % (
            como, len(self.us.body[a]), len(malas), ok, mal)
        pv = self.valores.get(a)                           # punteros a la funcion calculados en el codigo
        if pv:
            ev += ", punteros %d/%d a %08X" % (pv[e], sum(pv.values()), e)
            mal += sum(pv.values()) - pv[e]
            ok += pv[e]
        if not ok:                                         # tablas de punteros (vtables): vecinas emparejadas
            t_ok, t_n = self.tablas(a, e)
            if t_n:
                ev += ", tablas %d/%d con las vecinas emparejadas" % (t_ok, t_n)
                ok += t_ok
                mal += t_n - t_ok
        # sin llamadas ni punteros: basta la alineacion por orden (vecinas emparejadas) y el cuerpo, salvo en las
        # diminutas (hay series de funciones iguales de 2-5 instrucciones: la alineacion puede cruzarlas)
        unico = self.us_hash[self.us.body[a]] == 1 and len(self.by_hash[self.us.body[a]]) == 1
        if not ok:
            ev += ", cuerpo %s" % ("unico en las dos imagenes" if unico else "repetido")
        bien = not malas and mal == 0 and (ok > 0 or (como.startswith("alineada") and len(self.us.body[a]) >= 6))
        return e, ev, bien

    def tablas(self, a, e):
        """cada palabra = a en .rdata/.data de la US; sus vecinas (+-4, +-8) que sean funciones emparejadas deben
        rodear a una palabra = e en la EU."""
        def sitios(im, v):
            pat = struct.pack(">I", v)
            return [B + m.start() for m in re.finditer(re.escape(pat), im.img)
                    if m.start() % 4 == 0 and im.seccion(B + m.start()) in (".rdata", ".data")]
        eus = sitios(self.eu, e)
        ok = n = 0
        for loc in sitios(self.us, a):
            vec = {}
            for d in (-8, -4, 4, 8):
                w = self.us.u32(loc + d)
                if w in self.us.fset and len(self.us.body[w]) >= 6:   # las diminutas (li+blr) se repiten
                    pe, _ = self.funcion(w)
                    if pe:
                        vec[d] = pe
            if not vec:
                continue
            n += 1
            ok += any(all(self.eu.u32(l + d) == pe for d, pe in vec.items()) for l in eus)
        return ok, n

    def par_codigo(self, a):
        f = self.us.contiene(a)
        if f is None:
            return None, "fuera de toda funcion", False
        ef, ev, bien = self.par_funcion(f)
        if ef is None:
            return None, "funcion sub_%08X sin pareja" % f, False
        e = ef + (a - f)
        ev = "dentro de sub_%08X +0x%X -> sub_%08X (%s)" % (f, a - f, ef, ev)
        wu = self.us.u32(a - 4)
        if wu >> 26 == 18 and wu & 1:                      # direccion de retorno: el bl previo debe cuadrar
            tu, te = destino_salto(a - 4, wu), destino_salto(e - 4, self.eu.u32(e - 4))
            pe, _ = self.funcion(tu)
            ev += "; bl previo sub_%08X -> %08X (pareja %s)" % (tu, te, "%08X" % pe if pe else "?")
            bien = bien and pe == te
        return e, ev, bien

    def votos(self, k):
        v = self.valores[k]
        e, n = v.most_common(1)[0]
        return e, n, sum(v.values())

    def vecino(self, a, paso):
        """base calculada mas cercana (paso -1 debajo, +1 encima) en la misma seccion, con votos unanimes."""
        i = bisect.bisect_right(self.claves, a) - 1 if paso < 0 else bisect.bisect_left(self.claves, a)
        sec = self.us.seccion(a)
        while 0 <= i < len(self.claves) and abs(a - self.claves[i]) <= 0x80000:
            k = self.claves[i]
            if k != a and self.us.seccion(k) == sec:
                e, n, t = self.votos(k)
                if n == t:
                    return k, (e + (a - k)) & M32, n
            i += paso
        return None

    def par_dato(self, a):
        sec = self.us.seccion(a)
        if a in self.valores:                              # el codigo la calcula: votos de cada sitio
            e, n, t = self.votos(a)
            bien, ev = self.contenido(a, e, sec, "%s, %d/%d votos" % (sec, n, t), n == t)
            return e, ev, bien
        # nadie la calcula (campo de una estructura): bases calculadas mas cercanas por debajo y por encima
        bajo, alto = self.vecino(a, -1), self.vecino(a, +1)
        if not bajo and not alto:
            return self.por_contenido(a, sec)
        cand = [x for x in (bajo, alto) if x]
        e = cand[0][1]
        ev = "%s, nadie la calcula; " % sec + "; ".join(
            "base 0x%08X %+d (%d votos) -> %08X" % (k, a - k, n, ee) for k, ee, n in cand)
        # mismo desplazamiento US->EU a los dos lados, o campo cercano (< 0x100) de una base muy votada
        bien = (bajo and alto and bajo[1] == alto[1]) or (bajo and a - bajo[0] < 0x100 and bajo[2] >= 2)
        bien, ev = self.contenido(a, e, sec, ev, bool(bien))
        return e, ev, bien

    def contenido(self, a, e, sec, ev, bien):
        cu = self.us.img[a - B:a - B + 16]
        ce = self.eu.img[e - B:e - B + 16]
        if sec in (".rdata", ".data") and any(cu):
            if cu == ce:
                ev += ", 16 B iguales"
            else:
                igual = all(cu[i:i + 4] == ce[i:i + 4] or (cu[i] == 0x82 and ce[i] == 0x82) for i in range(0, 16, 4))
                ev += ", 16 B iguales salvo punteros" if igual else ", CONTENIDO DISTINTO"
                bien = bien and igual
        else:
            ev += ", sin contenido inicial (bss)"
        return bien, ev

    def por_contenido(self, a, sec):
        cu = self.us.img[a - B:a - B + 32]
        if sec not in (".rdata", ".data") or not any(cu) or len(set(cu)) < 4:
            return None, "%s, nadie la calcula y no tiene contenido unico" % sec, False
        hits = [m.start() for m in re.finditer(re.escape(cu), self.eu.img) if m.start() % 4 == a % 4]
        nu = len([m for m in re.finditer(re.escape(cu), self.us.img)])
        if len(hits) == 1 and nu == 1:
            return B + hits[0], "%s, nadie la calcula; 32 B unicos en las dos imagenes" % sec, True
        return None, "%s, nadie la calcula; contenido repetido (US %d, EU %d)" % (sec, nu, len(hits)), False

    def par(self, a):
        if a in self.us.fset:
            return ("funcion",) + self.par_funcion(a)
        if self.us.t0 <= a < self.us.t1:
            return ("codigo",) + self.par_codigo(a)
        if not B < a < B + len(self.us.img):
            return "fuera", None, "fuera de la imagen (rango o constante)", False
        return ("dato",) + self.par_dato(a)


# ---------------------------------------------------------------- fuentes
QUITA = re.compile(r'("(?:\\.|[^"\\])*")|//[^\n]*|/\*.*?\*/', re.S)


def leer_fuentes():
    usos = collections.defaultdict(set)      # direccion US -> {fichero}
    funcs = set()
    manual = []                              # (fichero, us, eu) ya escritas a mano en el codigo
    for pat in FUENTES:
        for p in sorted(ROOT.glob(pat)):
            rel = p.relative_to(ROOT).as_posix()
            s = QUITA.sub(lambda m: m.group(1) or "", p.read_text(encoding="utf-8", errors="replace"))
            for m in re.finditer(r"sub_(82[0-9A-F]{6})\s*,\s*dbz3eu_sub_(82[0-9A-F]{6})", s):
                manual.append((rel, int(m.group(1), 16), int(m.group(2), 16)))
            for m in re.finditer(r"GuestAddr\(\s*0x(82[0-9A-Fa-f]{6})u?\s*,\s*0x(82[0-9A-Fa-f]{6})u?\s*\)", s):
                manual.append((rel, int(m.group(1), 16), int(m.group(2), 16)))
            # los valores EU ya escritos a mano (2.o argumento de GuestAddr, tablas kXxxEu) no son direcciones US
            s = re.sub(r"(GuestAddr\([^,()]*,\s*)0x82[0-9A-Fa-f]{6}", r"\1EU", s)
            s = re.sub(r"^.*\bk\w*Eu\b(\[\w*\])?\s*=.*$", "", s, flags=re.M)
            for m in re.finditer(r"(?<![0-9A-Za-z])(?<!eu_)sub_(82[0-9A-F]{6})\b", s):
                a = int(m.group(1), 16)
                funcs.add(a)
                usos[a].add(rel)
            for m in re.finditer(r"\b0x(82[0-9A-Fa-f]{6})\b", s):
                usos[int(m.group(1), 16)].add(rel)
            # funciones nombradas solo por su numero: DBZ3_SEL(8219F4F8) (escenarios.cpp) y "8219F4F8": (generador)
            for m in re.finditer(r"DBZ3_SEL\((82[0-9A-F]{6})\)|^\s*\"(82[0-9A-F]{6})\":", s, re.M):
                a = int(m.group(1) or m.group(2), 16)
                funcs.add(a)
                usos[a].add(rel)
    return usos, funcs, manual


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--img-dir", default=str(IMG_DIR))
    ap.add_argument("--xex-us")
    ap.add_argument("--xex-eu")
    ap.add_argument("--comprobar", action="store_true", help="solo la autoprueba con parejas medidas a mano")
    a = ap.parse_args()
    d = Path(a.img_dir)
    if a.xex_us and a.xex_eu:
        d.mkdir(parents=True, exist_ok=True)
        (d / "us_img.bin").write_bytes(xex2img(a.xex_us))
        (d / "eu_img.bin").write_bytes(xex2img(a.xex_eu))
    us = Imagen((d / "us_img.bin").read_bytes(), ROOT / "generated" / "dbz3_init.cpp")
    eu = Imagen((d / "eu_img.bin").read_bytes(), ROOT / "generated_eu" / "dbz3_eu_init.cpp")
    print("funciones US %d EU %d, .text US %08X-%08X" % (len(us.funcs), len(eu.funcs), us.t0, us.t1), flush=True)
    m = Mapa(us, eu)
    print("alineadas %d" % len(m.fmap), flush=True)

    # autoprueba: parejas medidas y probadas en el juego (roster EU 2026-10-07; hooks.cpp; tagteam/campanas)
    conocidas = {0x82146430: 0x821A1F00, 0x821B7218: 0x821B6000, 0x8217E090: 0x8217E048, 0x821B98E8: 0x821B86D0,
                 0x82020618: 0x82020620, 0x8247971C: 0x8247970C, 0x824BA110: 0x824BA100, 0x823D6CF4: 0x823D6CF4,
                 0x8201F60C: 0x8201F604, 0x8234ABB8: 0x8234ABB8}
    fallos = 0
    for u, e in conocidas.items():
        _, got, ev, bien = m.par(u)
        if got != e or not bien:
            fallos += 1
            print("AUTOPRUEBA MAL %08X: esperado %08X, sale %s (%s)" % (u, e, got and "%08X" % got, ev))
    print("autoprueba: %d/%d parejas conocidas bien" % (len(conocidas) - fallos, len(conocidas)))
    if a.comprobar or fallos:
        return 1 if fallos else 0

    usos, funcs, manual = leer_fuentes()
    filas = []
    for addr in sorted(usos):
        tipo, e, ev, bien = m.par(addr)
        filas.append((addr, tipo, e, ev, bien, sorted(usos[addr])))
    res = {f[0]: f for f in filas}
    # parejas ya escritas a mano en el codigo frente a las del mapa
    choques = []
    for rel, u, e in manual:
        f = res.get(u)
        if f and f[2] != e:
            choques.append("%s: a mano %08X -> %08X, mapa %s" % (rel, u, e, "%08X" % f[2] if f[2] else "sin pareja"))

    ok = [f for f in filas if f[4]]
    lineas = ["# Mapa US -> EU (generado por tools/eu_map.py; no editar a mano)",
              "# tipo, US, EU, verificada, evidencia, ficheros que la usan",
              "# %d direcciones; %d verificadas; %d funciones; choques con parejas escritas a mano: %d" % (
                  len(filas), len(ok), len([f for f in filas if f[1] == "funcion"]), len(choques)), ""]
    for addr, tipo, e, ev, bien, fic in filas:
        lineas.append("%-7s %08X -> %-8s %-3s %s  [%s]" % (tipo, addr, "%08X" % e if e else "-", "SI" if bien else "NO",
                                                         ev, ", ".join(Path(x).name for x in fic)))
    if choques:
        lineas += ["", "## Choques con parejas escritas a mano"] + choques
    INFORME.write_text("\n".join(lineas) + "\n", encoding="utf-8")

    h = ["// GENERADO por tools/eu_map.py - no editar a mano. Parejas US -> EU verificadas (evidencia en",
         "// docs/14_frentes/eu_mapa.txt). Se usa a traves de src/eu_map.h.",
         "#pragma once", "", "#include <cstdint>", ""]
    fn = [f for f in ok if f[1] == "funcion" and f[0] in funcs]
    h += ["#define DBZ3EU_sub_%08X dbz3eu_sub_%08X" % (f[0], f[2]) for f in fn]
    h += ["", "#if defined(DBZ3_DUAL_REGION)", "#include <rex/hook.h>", ""]
    h += ["REX_EXTERN(dbz3eu_sub_%08X);\nREX_EXTERN(__imp__dbz3eu_sub_%08X);" % (f[2], f[2]) for f in fn]
    h += ["#endif"]
    h += ["", "namespace dbz3::eu_map {", "struct Par {", "  uint32_t us, eu;", "};",
          "inline constexpr Par kPares[] = {"]
    h += ["    {0x%08Xu, 0x%08Xu},  // %s" % (f[0], f[2], f[1]) for f in ok if f[1] != "fuera"]
    h += ["};", "}  // namespace dbz3::eu_map", ""]
    GEN_H.write_text("\n".join(h), encoding="utf-8", newline="\n")
    porfich = collections.Counter()
    for f in filas:
        if not f[4] and f[1] != "fuera":
            for x in f[5]:
                porfich[x] += 1
    print("direcciones %d, verificadas %d, fuera de la imagen %d, sin verificar %d %s" % (
        len(filas), len(ok), len([f for f in filas if f[1] == "fuera"]),
        len([f for f in filas if not f[4] and f[1] != "fuera"]), dict(porfich)))
    print("choques con parejas a mano: %d" % len(choques))
    for c in choques:
        print("  " + c)
    print("escrito %s y %s" % (GEN_H.relative_to(ROOT), INFORME.relative_to(ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

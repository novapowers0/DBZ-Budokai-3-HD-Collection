#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""biblioteca_anim.py - Biblioteca de clips de animacion de los personajes del Budokai 3 HD y motor de recetas.

Lista cada clip de cada personaje del roster (id, nombre, codigo BSK, indice de animacion, n de
fotogramas) leyendo us/data_cmn.afs del jugador con los lectores del proyecto (afs_pair, studio_core,
altura). Nunca escribe en us/; aplicar_receta escribe solo en la carpeta del mod que se le da.

  python biblioteca_anim.py listar [--texto goku] [--categoria ataque] [--json]
  python biblioteca_anim.py resumen
  python biblioteca_anim.py receta-guardar --destino-personaje 0 --destino-codigo 0x21b \\
         --fuente-personaje 5 --fuente-codigo 0x21b --out receta.toml [--nombre "Goku con golpe de Saiyaman"]
  python biblioteca_anim.py aplicar --receta receta.toml --mod mods/mi_mod   (KIT-08: ver "aplicar recetas")
  python biblioteca_anim.py selftest

Mapeo (RE del proyecto, docs/03_formatos/ACM_FORMAT.md y awo_tools/altura.py:code_anim):
  #CSK (moveset) -> cabecera +0x10 = n de codigos, +0x14 = tabla de direcciones (4 B por codigo).
  Cada bloque empieza con [anim u16][pool u16]. pool == 3 -> anim es el indice de un clip del
  #ACM del propio bin (el primer hijo de tipo 3). Otros pools no se resuelven aqui (frames = None).
  #ACM -> +0x10 = n de clips, +0x14 = tabla de 16 B por clip [flags, variante, n_fotogramas, offset].

Categorias: INFERIDAS de los datos del roster (roster_db.json, skills.ataques / transformaciones) y de
rangos documentados. No hay una tabla oficial de clips por categoria: "ki", "golpe recibido" y "dash"
no se pueden inferir de los codigos BSK y no aparecen.
"""
import argparse
import json
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
_DEF_ROOT = "C:/Users/javie/Desktop/PROYECTOS IA/DBZ Budokai 3 HD Collection"


def _bootstrap():
    """Encuentra la raiz del proyecto (env DBZ3_ROOT, carpeta padre de este fichero si esta dentro de
    'mod center hd', o la ruta por defecto) y deja importables studio_core, afs_pair y altura."""
    root = os.environ.get("DBZ3_ROOT")
    if not root:
        here_mch = HERE if os.path.isdir(os.path.join(HERE, "studio")) else None
        root = os.path.dirname(here_mch) if here_mch else _DEF_ROOT
    mch = os.path.join(root, "mod center hd")
    awo = os.path.join(root, "awo_tools")
    for p in (os.path.join(mch, "studio"), awo, mch):
        if p not in sys.path:
            sys.path.append(p)
    return root, mch


ROOT, MCH = _bootstrap()
import studio_core as sc  # noqa: E402  (amb_kids, code_info, anim_pool, anim_locals, hd_entry, us_dir)

CATEGORIAS = ("reposo", "movimiento", "ataque", "agarre", "transformacion", "otro", "sin codigo")
GROUND_CODES = (2, 15, 16, 17)     # altura.py: codigos de andar por el suelo (pies apoyados)
AGARRE_RANGO = (0x480, 0x4C0)       # SB_VS_B3_MOVESET.md: agarre BASE 0x480 en B3 (techo inferido)
ATAQUE_RANGO = (0x200, 0x500)       # docs: codigos de ataque 0x200-0x4FF (tec_sb.py / spx_graph.py)

_cache = {}


# ------------------------------------------------------------------ roster
def personajes(mch=MCH):
    """[{id, nombre, anm, modelo, ataques, transformaciones}] de roster_db.json (solo los que tienen moveset)."""
    with open(os.path.join(mch, "roster_db.json"), encoding="utf-8") as fh:
        db = json.load(fh)
    out = []
    for e in db["ids"]:
        anm = next((a for a in e.get("anm") or [] if a < 0xFFFFFFFF and a), None)
        if anm is None:
            continue
        sk = e.get("skills") or {}
        out.append(dict(id=e["id"], nombre=e["name"].title(), anm=anm,
                        modelo=(e.get("models") or [None])[0],
                        ataques=set(sk.get("ataques") or []),
                        transformaciones=set(sk.get("transformaciones") or [])))
    return out


# ------------------------------------------------------------------ categorias
def categoria(code, ataques=(), transformaciones=()):
    """Categoria inferida de un codigo BSK (ver la cabecera del modulo)."""
    if code in transformaciones:
        return "transformacion"
    if code in ataques:
        return "ataque"
    if code == 0:
        return "reposo"
    if code in GROUND_CODES:
        return "movimiento"
    if AGARRE_RANGO[0] <= code < AGARRE_RANGO[1]:
        return "agarre"
    if ATAQUE_RANGO[0] <= code < ATAQUE_RANGO[1]:
        return "ataque"
    return "otro"


# ------------------------------------------------------------------ lectura de un personaje
def frames_de(acm, e, anim):
    """Numero de fotogramas del clip `anim` del #ACM, o None si no existe."""
    n, tbl = struct.unpack_from(e + "II", acm, 0x10)
    if anim >= n:
        return None
    _flags, _var, nf, off = struct.unpack_from(e + "4I", acm, tbl + 16 * anim)
    return nf if off else None


def clips_de(p, us=None):
    """Filas de un personaje: una por codigo BSK con su clip, mas los clips del pool sin codigo.
    Lee la entrada HD de data_cmn.afs (descomprimida, con cache de afs_pair)."""
    anm = sc.hd_entry(p["anm"], us)
    csk, e = sc.csk_of(anm)
    acm, _ = sc.anim_pool(anm)
    filas, usados = [], set()
    if csk:
        n = struct.unpack_from(e + "I", csk, 0x10)[0]
        for code in range(n):
            info = sc.code_info(csk, e, code)
            if info is None:
                continue
            anim, pool, _marks = info
            fr = frames_de(acm, e, anim) if (acm and pool == 3) else None
            if pool == 3:
                usados.add(anim)
            filas.append(_fila(p, code, anim if pool == 3 else None, pool, fr))
    if acm:
        for anim in range(sc.anim_count(acm, e)):
            if anim not in usados:
                filas.append(_fila(p, None, anim, 3, frames_de(acm, e, anim)))
    return filas


def _fila(p, code, anim, pool, frames):
    cat = "sin codigo" if code is None else categoria(code, p["ataques"], p["transformaciones"])
    return dict(personaje=p["id"], nombre=p["nombre"], fid=p["anm"], codigo=code, anim=anim, pool=pool,
                frames=frames, categoria=cat)


def biblioteca(us=None, refrescar=False):
    """Todas las filas (cache en memoria). `us` = carpeta us/ del juego (por defecto la del proyecto)."""
    key = os.path.abspath(sc.us_dir(us))
    if refrescar or key not in _cache:
        filas = []
        for p in personajes():
            filas += clips_de(p, us)
        _cache[key] = filas
    return _cache[key]


# ------------------------------------------------------------------ busqueda
# Nombres de jugador (es / en) de los IDs de B3: el roster usa los internos ("GOKOU"); asi "goku" o "krilin" encuentran.
NOMBRES = {0: "goku", 2: "gohan nino kid gohan", 3: "gohan adolescente teen gohan", 4: "gohan", 5: "gran saiyaman great",
           6: "goten", 7: "vegeta", 8: "trunks", 9: "trunks nino kid trunks", 10: "krilin krillin", 11: "piccolo",
           12: "ten shin han tien", 13: "yamcha", 14: "mr satan hercule", 15: "videl", 16: "kaio-shin supreme kai",
           18: "raditz", 19: "nappa", 20: "ginyu", 21: "recoome", 27: "freezer frieza", 28: "androide 16 android",
           29: "androide 17 android", 30: "androide 18 android", 32: "dr gero", 33: "cell", 34: "majin bu buu",
           35: "super bu buu", 36: "bu nino kid buu", 37: "dabura", 38: "cooler", 42: "saibaman", 43: "cell jr"}


def buscar(filas, texto="", categoria_=None, personaje=None):
    """Filtra por texto libre (nombre, id, codigo en hex, indice de clip, categoria) y/o categoria."""
    t = (texto or "").strip().lower()
    hex_t = t[2:] if t.startswith("0x") else t
    out = []
    for f in filas:
        if categoria_ and f["categoria"] != categoria_:
            continue
        if personaje is not None and f["personaje"] != personaje:
            continue
        if t:
            code_hex = "%x" % f["codigo"] if f["codigo"] is not None else ""
            campos = [f["nombre"].lower(), NOMBRES.get(f["personaje"], ""), str(f["personaje"]), f["categoria"],
                      str(f["anim"]) if f["anim"] is not None else "", code_hex]
            if not any(t == c or (hex_t and hex_t == c) or (len(t) > 2 and t in c) for c in campos):
                continue
        out.append(f)
    return out


def formato_fila(f):
    cod = "%#05x" % f["codigo"] if f["codigo"] is not None else "   -  "
    anim = "%4d" % f["anim"] if f["anim"] is not None else "   -"
    fr = "%5s" % (f["frames"] if f["frames"] is not None else "-")
    banco = "" if f["pool"] in (3, None) else "  (banco %d: no resuelto)" % f["pool"]
    return "%3d  %-16s %s  anim %s  %s fr  %s%s" % (f["personaje"], f["nombre"][:16], cod, anim, fr,
                                                   f["categoria"], banco)


# ------------------------------------------------------------------ recetas
def guardar_receta(path, destino, fuente, nombre="", notas=""):
    """Escribe una receta .toml: 'destino' = accion a reemplazar, 'fuente' = de donde sale la animacion.
    destino/fuente: dict(personaje=id, codigo=int, anim=int|None). Escritura TOML a mano (sin dependencias)."""
    for nom, d in (("destino", destino), ("fuente", fuente)):
        if d.get("codigo") is None:
            raise ValueError("%s sin codigo BSK: solo se puede recetar una accion con codigo" % nom)
    L = ["# Receta de la biblioteca de animaciones (no modifica nada por si sola)", "version = 1",
         'nombre = "%s"' % nombre.replace('"', "'"), 'notas = "%s"' % notas.replace('"', "'"), "",
         "[destino]", "personaje = %d" % destino["personaje"], "codigo = %#x" % destino["codigo"],
         "anim = %d" % destino["anim"] if destino.get("anim") is not None else "# anim = (sin clip)", "",
         "[fuente]", "personaje = %d" % fuente["personaje"], "codigo = %#x" % fuente["codigo"],
         "anim = %d" % fuente["anim"] if fuente.get("anim") is not None else "# anim = (sin clip)", ""]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L))
    return path


def leer_receta(path):
    import tomllib  # noqa: PLC0415
    with open(path, "rb") as fh:
        r = tomllib.load(fh)
    if r.get("version") != 1 or "destino" not in r or "fuente" not in r:
        raise ValueError("%s no es una receta v1 (faltan [destino] o [fuente])" % path)
    return r


# ------------------------------------------------------------------ aplicar recetas (KIT-08)
# Receta (dict o .toml v1):
#   [destino] personaje, codigo              accion que cambia de movimiento
#   [fuente]  personaje, codigo, desde?, hasta?   de donde sale (desde/hasta = recorte en fotogramas)
#   [despues] personaje, codigo, desde?, hasta?   (opcional) segundo clip empalmado tras la fuente
#   [[mas]]   personaje, codigo, desde?, hasta?   (opcional, KIT-09) mas tramos tras despues, en orden
#   mezcla = N                                    fotogramas de mezcla entre cada par de tramos
#   duracion = N                                  (opcional) re-temporizar el resultado a N fotogramas
# Como cinematica.py (patron probado en juego): el clip se ANADE al #ACM propio del destino y el
# sub-bloque del codigo en el #CSK pasa a apuntarlo. El resto del #CSK (golpes, dano, efectos, marcas)
# es el del destino: editar fotogramas de las lineas AP crashea en combate (ACM_FORMAT.md 4.1), asi que
# llevarse las marcas de golpe de la fuente queda como "no viable aun".
# Huesos: el motor empareja pistas por nombre (sufijo tras el prefijo GOK_/VGT_...; asi comparte el
# banco de poses del select, memoria b1-moveset-port); acm_append ya reasigna por sufijo. Si el destino
# no tiene un hueso del cuerpo (b1port.CORE_BONES) que la fuente mueve, se rechaza.
CINE_RANGO = (0x480, 0x500)       # agarres (0x480+) y cinematica de la definitiva (0x4A0+): con camara del guion
CORE_BONES = {"BODY", "WAIST", "STMC", "CHEST", "NECK", "HEAD", "LCHN", "RCHN", "LARMROT", "RARMROT",
              "LARM1", "RARM1", "LARM2", "RARM2", "LHANDROT", "RHANDROT", "L00_LHAND", "L00_RHAND",
              "LLEGROT", "RLEGROT", "LLEG1", "RLEG1", "LLEG2", "RLEG2", "LFOOT1", "RFOOT1",
              "LFOOT2", "RFOOT2", "M_JAW"}   # = awo_tools/b1port.CORE_BONES (b1port arrastra el puerto B1 entero)


def _cin():
    import cinematica  # noqa: PLC0415  (awo_tools: _keys/_track/_rot_at)
    return cinematica


def _rb():
    import roster_build  # noqa: PLC0415  (acm_read/acm_append)
    return roster_build


def _subbloques(csk, code):
    """Offsets de los sub-bloques de 48 B del codigo (regla de b1port.bsk_blocks / ps2hd.conv_bsk, en BE)."""
    n, lst, _nhr, hr = struct.unpack_from(">4I", csk, 0x10)
    if code >= n:
        return []
    a = struct.unpack_from(">I", csk, lst + 4 * code)[0]
    if not a:
        return []
    aset = set(struct.unpack_from(">%dI" % n, csk, lst))
    s, subs = a, []
    while s + 48 <= hr:
        pad, nap, apo = struct.unpack_from(">3I", csk, s + 0x24)
        if pad or not 0 <= nap < 64 or (nap and not hr > apo >= 0x20):
            break
        subs.append(s)
        s += 48
        if s in aset:
            break
    return subs


_acm_cache = {}


def _acm_de(fid, us=None):
    """(nombres, anims, csk) del ANM nativo `fid` (acm_read del primer #ACM)."""
    key = (fid, os.path.abspath(sc.us_dir(us)))
    if key not in _acm_cache:
        anm = sc.hd_entry(fid, us)
        csk, _ = sc.csk_of(anm)
        acm, _ = sc.anim_pool(anm)
        names, anims = _rb().acm_read(acm)
        _acm_cache[key] = (names, anims, csk)
    return _acm_cache[key]


def _persona(pid):
    p = next((x for x in personajes() if x["id"] == int(pid)), None)
    if p is None:
        raise ValueError("el personaje %s no existe en el roster" % pid)
    return p


def clip_de(d, us=None):
    """Clip (flags, var, frames, {hueso: [giro, pos]}) de {personaje, codigo} (+ desde/hasta = recorte)."""
    p = _persona(d["personaje"])
    _names, anims, csk = _acm_de(p["anm"], us)
    info = sc.code_info(csk, ">", int(d["codigo"])) if csk else None
    if info is None:
        raise ValueError("%s no tiene el codigo %#x" % (p["nombre"], int(d["codigo"])))
    anim, pool, _ = info
    if pool != 3 or anim >= len(anims) or anims[anim][3] is None:
        raise ValueError("%s %#x usa un clip del banco comun %d: no viable aun (solo clips propios, banco 3)"
                         % (p["nombre"], int(d["codigo"]), pool))
    c = anims[anim]
    if d.get("desde") is not None or d.get("hasta") is not None:
        c = recortar(c, int(d.get("desde") or 0), int(d["hasta"]) if d.get("hasta") is not None else c[2] - 1)
    return c


def _remuestrear(c, fuentes):
    """Clip con un fotograma por cada frame fuente (float) de `fuentes` (claves en todos los fotogramas)."""
    cin = _cin()
    fl, var, _n, bones = c
    out = {}
    for bone, tr in (bones or {}).items():
        nt = []
        for i, kind in enumerate(("rot", "pos")):
            head, keys = cin._keys(tr[i] if len(tr) > i else None, kind)
            if not keys:
                nt.append(None)
                continue
            val = (lambda f: cin._rot_at(keys, f)) if kind == "rot" else (lambda f: sc.sample(keys, f))
            nt.append(cin._track(head, [[d] + list(val(f)) for d, f in enumerate(fuentes)], kind))
        out[bone] = nt
    return (fl & ~0x10, var, len(fuentes), out)


def recortar(c, desde, hasta):
    """Fotogramas desde..hasta (incluidos; hasta < desde = al reves)."""
    nf = c[2]
    a, b = max(0, min(desde, nf - 1)), max(0, min(hasta, nf - 1))
    paso = 1 if b >= a else -1
    return _remuestrear(c, list(range(a, b + paso, paso)))


def cambiar_duracion(c, n):
    """El mismo movimiento re-temporizado a n fotogramas (mas lento o mas rapido)."""
    if n < 2:
        raise ValueError("la duracion minima es 2 fotogramas")
    nf = c[2]
    return _remuestrear(c, [d * (nf - 1) / (n - 1) for d in range(n)])


def mezclar(a, b, n):
    """a y despues b, con los ultimos n fotogramas de a fundidos con los n primeros de b
    (giros por la vuelta mas corta, posiciones en linea recta). Total = len(a) + len(b) - n.
    Un hueso que solo mueve uno de los dos clips toma, en el otro tramo, el valor del clip que lo
    mueve en el empalme (asi no salta)."""
    cin = _cin()
    na, nb = a[2], b[2]
    n = max(0, min(int(n), na, nb))
    start = na - n
    total = na + nb - n

    def lerp(x, y, t, kind):
        if kind == "rot":
            return [int(round(u + (((v - u + 32768) % 65536) - 32768) * t)) % 65536 for u, v in zip(x, y)]
        return [u + (v - u) * t for u, v in zip(x, y)]

    out = {}
    for bone in set(a[3] or {}) | set(b[3] or {}):
        ta, tb = (a[3] or {}).get(bone) or [None, None], (b[3] or {}).get(bone) or [None, None]
        nt = []
        for i, kind in enumerate(("rot", "pos")):
            ha, ka = cin._keys(ta[i], kind)
            hb, kb = cin._keys(tb[i], kind)
            if not ka and not kb:
                nt.append(None)
                continue
            at = (lambda k: (lambda f: cin._rot_at(k, f)) if kind == "rot" else (lambda f: sc.sample(k, f)))
            va = at(ka) if ka else (lambda f, _v=at(kb)(0): _v)
            vb = at(kb) if kb else (lambda f, _v=at(ka)(na - 1): _v)
            keys = []
            for d in range(total):
                if d < start:
                    v = va(d)
                elif d >= na:
                    v = vb(d - start)
                else:
                    v = lerp(va(d), vb(d - start), (d - start + 1) / (n + 1), kind)
                keys.append([d] + list(v))
            nt.append(cin._track(ha or hb, keys, kind))
        out[bone] = nt
    return (a[0] & ~0x10, a[1], total, out)


def _tramos(receta):
    """[(nombre, tramo)] de los clips de una receta, en orden: fuente, despues y los de `mas` (KIT-09)."""
    out = [("fuente", receta["fuente"])] + ([("despues", receta["despues"])] if receta.get("despues") else [])
    return out + [("tramo %d" % (k + 3), t) for k, t in enumerate(receta.get("mas") or [])]


def clip_receta(receta, us=None):
    """Clip final de una receta (fuente [+ recorte] [+ despues y mas, con mezcla] [+ duracion])."""
    c = clip_de(receta["fuente"], us)
    for _nom, t in _tramos(receta)[1:]:
        c = mezclar(c, clip_de(t, us), int(receta.get("mezcla") or 0))
    if receta.get("duracion"):
        c = cambiar_duracion(c, int(receta["duracion"]))
    return c


def _limites(filas):
    fr = [f["frames"] for f in filas if f["pool"] == 3 and f["frames"]]
    return (min(fr), max(fr)) if fr else (2, 600)


def validar_receta(receta, filas=None, us=None):
    """Avisos de una receta antes de aplicarla (vacia = sin pegas). Los que empiezan por "ERROR:"
    impiden aplicarla: no existe el clip, banco comun, faltan huesos del cuerpo en el destino.
    Avisos: huesos secundarios que el destino no tiene, duracion fuera de lo que usan los clips del
    juego, marcas de golpe del destino que caen fuera del clip nuevo, clip de cinematica (con camara
    del guion) puesto en otro sitio o con otra duracion."""
    filas = biblioteca(us) if filas is None else filas
    av = []
    for nom, d in [("destino", receta["destino"])] + _tramos(receta):
        if not any(f["personaje"] == d["personaje"] and f["codigo"] == d["codigo"] for f in filas):
            av.append("ERROR: %s: personaje %s, codigo %#x no existe en el juego" % (nom, d["personaje"], d["codigo"]))
    if av:
        return av
    try:
        nuevo = clip_receta(receta, us)
        orig = clip_de({k: receta["destino"][k] for k in ("personaje", "codigo")}, us)
    except ValueError as ex:
        return ["ERROR: %s" % ex]
    dst = receta["destino"]
    pd = _persona(dst["personaje"])
    names_d, _anims, csk_d = _acm_de(pd["anm"], us)
    tiene = {_rb()._suffix(n) for n in names_d}
    for nom, t in _tramos(receta):
        movidos = {b for b, tr in (clip_de(t, us)[3] or {}).items() if any(tr[:2])}
        faltan = sorted(movidos - tiene)
        core = [b for b in faltan if b in CORE_BONES]
        if core:
            av.append("ERROR: %s mueve huesos del cuerpo que %s no tiene: %s (esqueleto distinto)" % (
                nom, pd["nombre"], ", ".join(core)))
        elif faltan:
            av.append("aviso: %s no tiene %s: esa parte no se movera" % (pd["nombre"], ", ".join(faltan)))
    lo, hi = _limites(filas)
    if not lo <= nuevo[2] <= hi:
        av.append("aviso: el clip dura %d fotogramas; los del juego van de %d a %d" % (nuevo[2], lo, hi))
    marcas = [f for f, t in sc.code_info(csk_d, ">", int(dst["codigo"]))[2] if t == 1]
    fuera = [f for f in marcas if f >= nuevo[2]]
    if fuera:
        av.append("aviso: los golpes del destino caen en los fotogramas %s y el clip nuevo acaba en el %d: "
                  "esos golpes no llegaran a salir" % (fuera, nuevo[2] - 1))
    cine = lambda c: CINE_RANGO[0] <= int(c) < CINE_RANGO[1]  # noqa: E731
    if cine(dst["codigo"]) and nuevo[2] != orig[2]:
        av.append("aviso: %#x es de una cinematica: su camara dura %d fotogramas y el clip %d (desincronizado)"
                  % (int(dst["codigo"]), orig[2], nuevo[2]))
    if cine(receta["fuente"]["codigo"]) and not cine(dst["codigo"]):
        av.append("aviso: el clip de la fuente es de una cinematica: aqui se vera sin su camara")
    return av


def _anm_mod(fid, mod_dir, us=None):
    """ANM del destino: el que ya dejo el mod (las recetas se acumulan) o el del juego."""
    ruta = os.path.join(mod_dir, "us", "data_cmn.afs", str(fid), "geom.bin")
    if os.path.isfile(ruta):
        from kit import base  # noqa: PLC0415
        with open(ruta, "rb") as fh:
            return base.lzx_bytes(fh.read(), False), ruta
    return sc.hd_entry(fid, us), ruta


def anm_con_clip(anm, code, clip):
    """ANM HD con `clip` anadido al #ACM propio y el codigo apuntandolo (sub-bloques con el mismo clip
    que el primero; los de otros clips, p.ej. la version en el aire, se quedan). -> (bin, n sub-bloques)."""
    from sb_tecnicas import amb_hd, kids  # noqa: PLC0415
    ks = list(kids(anm, ">"))
    ci = next(i for i, (x, _) in enumerate(ks) if x[:4] == b"#CSK")
    ai = next(i for i, (_x, t) in enumerate(ks) if t == 3)
    csk, acm = bytearray(ks[ci][0]), ks[ai][0]
    n_anims = struct.unpack_from(">I", acm, 0x10)[0]
    subs = _subbloques(csk, code)
    if not subs:
        raise ValueError("el codigo %#x no tiene bloque en el moveset destino: anadir golpes nuevos no es "
                         "viable aun (haria falta injertar golpes y dano)" % code)
    first, pool = struct.unpack_from(">HH", csk, subs[0])
    if pool != 3:
        raise ValueError("el codigo %#x del destino usa el banco comun %d: no viable aun" % (code, pool))
    hechos = 0
    for s in subs:
        if struct.unpack_from(">HH", csk, s) == (first, 3):
            struct.pack_into(">H", csk, s, n_anims)
            hechos += 1
    ks[ci] = (bytes(csk), ks[ci][1])
    ks[ai] = (_rb().acm_append(acm, [clip]), ks[ai][1])
    return amb_hd(ks), hechos


def aplicar_receta(receta, mod_dir, us=None):
    """Escribe en `mod_dir` el override del ANM del destino (mods/<mod>/us/data_cmn.afs/<fid>/geom.bin,
    LZX /N:2048, relleno al tamano que lee el juego) con el clip de la receta en su codigo, mas
    manifest.txt y animaciones.json (registro de recetas). Respaldo previo en <mod>/respaldo/<fecha>/.
    Errores legibles (ValueError) si la receta no es viable; no escribe nada en ese caso.
    -> dict(ruta, fid, tamano, informe, avisos)."""
    av = validar_receta(receta, us=us)
    errs = [a for a in av if a.startswith("ERROR")]
    if errs:
        raise ValueError("\n".join(errs))
    from kit import base  # noqa: PLC0415
    dst = receta["destino"]
    pd = _persona(dst["personaje"])
    code = int(dst["codigo"])
    clip = clip_receta(receta, us)
    anm, ruta = _anm_mod(pd["anm"], mod_dir, us)
    out, hechos = anm_con_clip(anm, code, clip)
    comp = base.lzx_bytes(out, True)
    slot = sc.afs_slot(pd["anm"], us)
    tam = max((slot + 0xFFF) & ~0xFFF, (len(comp) + 0xFFF) & ~0xFFF)   # >= to_read del juego (sin eso, crash)
    when = sc.stamp()
    reg = os.path.join(mod_dir, "animaciones.json")
    man = os.path.join(mod_dir, "manifest.txt")
    sc.backup(mod_dir, [ruta, reg, man], when)
    sc.atomic_write(ruta, comp + bytes(tam - len(comp)))
    with open(ruta, "rb") as fh:
        if base.lzx_bytes(fh.read(), False) != out:
            raise RuntimeError("la comprobacion del override fallo (LZX)")
    hist = []
    if os.path.isfile(reg):
        with open(reg, encoding="utf-8") as fh:
            hist = json.load(fh)
    hist.append(dict(receta, aplicada=when, fid=pd["anm"], frames=clip[2]))
    sc.atomic_write(reg, json.dumps(hist, indent=1).encode("utf-8"))
    mod = os.path.basename(os.path.normpath(mod_dir))
    if not os.path.isfile(man):
        sc.atomic_write(man, ("name=%s\ndescription=Animaciones de %s (Kit)\ntype=animacion\nsource=ANM %d\n"
                              "target=%d\nregion=us\n" % (mod, pd["nombre"], pd["anm"], pd["anm"])).encode("utf-8"))
    f = receta["fuente"]
    inf = "%s %#x: %d fotogramas de %s %#x%s (%d sub-bloque(s))" % (
        pd["nombre"], code, clip[2], _persona(f["personaje"])["nombre"], int(f["codigo"]),
        " + empalme" if receta.get("despues") else "", hechos)
    return dict(ruta=ruta, fid=pd["anm"], tamano=len(comp), informe=inf, avisos=av)


# ------------------------------------------------------------------ pose de un fotograma (para la pagina)
def render_fotograma(personaje_id, anim, frame, size=(320, 320), yaw=0.0, us=None, filas=None):
    """RGBA (H, W, 4) uint8 del modelo del personaje en la pose del clip `anim` en `frame`.
    Usa el primer modelo del personaje (traje 0). Pose estatica: sin reproduccion."""
    import math  # noqa: PLC0415
    import numpy as np  # noqa: PLC0415
    import altura  # noqa: PLC0415
    from model_render import Model, render, rot_y  # noqa: PLC0415
    p = next(x for x in personajes() if x["id"] == personaje_id)
    if p["modelo"] is None:
        raise ValueError("el personaje %s no tiene modelo" % personaje_id)
    mb = sc.hd_entry(p["modelo"], us)
    m, skel = Model(mb), altura.skeleton(mb)
    acm, e = sc.anim_pool(sc.hd_entry(p["anm"], us))
    m.local = sc.anim_locals(skel, acm, e, anim, frame)
    P, _ = m.posed(None)
    center = (P.min(0) + P.max(0)) / 2
    ext = float(np.max(P.max(0) - P.min(0))) or 1.0
    W, H = size
    scale = 0.92 * min(W, H) / ext
    return render(m, size=size, view=rot_y(yaw), center=center, scale=scale, ss=2, outline=1.2)


def frames_clip(personaje_id, anim, us=None):
    """Numero de fotogramas de un clip (lectura directa, sin renderizar)."""
    p = next(x for x in personajes() if x["id"] == personaje_id)
    acm, e = sc.anim_pool(sc.hd_entry(p["anm"], us))
    return frames_de(acm, e, anim)


# ------------------------------------------------------------------ CLI
def _selftest(us=None):
    filas = biblioteca(us)
    ok = []

    def chk(nombre, cond, extra=""):
        ok.append(bool(cond))
        print("[%s] %s %s" % ("OK" if cond else "FALLO", nombre, extra))

    per = sorted({f["personaje"] for f in filas})
    chk("personajes con clips", len(per) >= 30, "(%d)" % len(per))
    chk("filas totales", len(filas) > 500, "(%d)" % len(filas))
    gok = [f for f in filas if f["personaje"] == 0]
    chk("Goku tiene codigos BSK", any(f["codigo"] is not None for f in gok), "(%d filas)" % len(gok))
    chk("frames > 0 en clips con pool 3", all((f["frames"] or 0) > 0 for f in filas if f["pool"] == 3 and f["anim"] is not None),
        "(sin frames: %d)" % sum(1 for f in filas if f["pool"] == 3 and not f["frames"]))
    chk("categoria 0 = reposo", categoria(0) == "reposo")
    chk("busqueda por nombre", buscar(filas, "gokou", personaje=0) == gok and buscar(filas, "gokou"))
    chk("busqueda hex", all(f["codigo"] == 0x21B for f in buscar(filas, "0x21b")) and buscar(filas, "0x21b"))
    chk("filtro categoria", all(f["categoria"] == "ataque" for f in buscar(filas, categoria_="ataque")))
    with tempfile.TemporaryDirectory() as td:
        pth = os.path.join(td, "r.toml")
        d = dict(personaje=0, codigo=0x21B, anim=None)
        f_ = dict(personaje=5, codigo=0x21B, anim=None)
        ref = next((x for x in filas if x["codigo"] is not None and x["anim"] is not None), None)
        if ref:
            d.update(anim=ref["anim"]); d["personaje"] = ref["personaje"]; d["codigo"] = ref["codigo"]
            f_.update(anim=ref["anim"], personaje=ref["personaje"], codigo=ref["codigo"])
        guardar_receta(pth, d, f_, nombre="prueba", notas="selftest")
        r = leer_receta(pth)
        chk("receta ida y vuelta", r["destino"]["personaje"] == d["personaje"] and r["fuente"]["codigo"] == f_["codigo"])
        chk("receta valida contra la biblioteca", validar_receta(r, filas) == [], str(validar_receta(r, filas)))
    try:   # receta imposible: error legible y nada escrito (la prueba completa: kit/prueba_pelicula.py)
        aplicar_receta({"destino": dict(personaje=0, codigo=0x7FFF), "fuente": dict(personaje=0, codigo=0)}, td)
        chk("aplicar_receta rechaza un codigo inexistente", False)
    except ValueError:
        chk("aplicar_receta rechaza un codigo inexistente", not os.path.exists(td))
    print("RESULTADO: %d/%d OK" % (sum(ok), len(ok)))
    return all(ok)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--us", help="carpeta us/ del juego (por defecto la del proyecto)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("listar")
    l.add_argument("--texto", default="")
    l.add_argument("--categoria", choices=CATEGORIAS)
    l.add_argument("--personaje", type=int)
    l.add_argument("--json", action="store_true")
    sub.add_parser("resumen")
    rg = sub.add_parser("receta-guardar")
    rg.add_argument("--destino-personaje", type=int, required=True)
    rg.add_argument("--destino-codigo", type=lambda s: int(s, 0), required=True)
    rg.add_argument("--fuente-personaje", type=int, required=True)
    rg.add_argument("--fuente-codigo", type=lambda s: int(s, 0), required=True)
    rg.add_argument("--out", required=True)
    rg.add_argument("--nombre", default="")
    sub.add_parser("selftest")
    apl = sub.add_parser("aplicar")
    apl.add_argument("--receta", required=True)
    apl.add_argument("--mod", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "aplicar":
        r = leer_receta(a.receta)
        for x in validar_receta(r, us=a.us):
            print(x)
        res = aplicar_receta(r, a.mod, a.us)
        print(res["informe"], "->", res["ruta"])
        return 0
    if a.cmd == "selftest":
        return 0 if _selftest(a.us) else 1
    filas = biblioteca(a.us)
    if a.cmd == "listar":
        res = buscar(filas, a.texto, a.categoria, a.personaje)
        if a.json:
            print(json.dumps(res, ensure_ascii=False, indent=1))
        else:
            for f in res:
                print(formato_fila(f))
            print("%d clips" % len(res))
    elif a.cmd == "resumen":
        for c in CATEGORIAS:
            print("%-16s %d" % (c, sum(1 for f in filas if f["categoria"] == c)))
        print("total %d clips, %d personajes" % (len(filas), len({f["personaje"] for f in filas})))
    elif a.cmd == "receta-guardar":
        d = dict(personaje=a.destino_personaje, codigo=a.destino_codigo)
        s = dict(personaje=a.fuente_personaje, codigo=a.fuente_codigo)
        for x, nom in ((d, "destino"), (s, "fuente")):
            row = next((f for f in filas if f["personaje"] == x["personaje"] and f["codigo"] == x["codigo"]), None)
            if row is None:
                ap.error("%s: no existe personaje %s codigo %#x" % (nom, x["personaje"], x["codigo"]))
            x["anim"] = row["anim"]
        guardar_receta(a.out, d, s, nombre=a.nombre)
        print("receta guardada en", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

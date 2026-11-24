# -*- coding: utf-8 -*-
"""Generador de animaciones (KIT-09), logica sin interfaz de kit/paginas/generador.py y kit/paginas/tecnica.py.

  clips(us)                         -> biblioteca: [{personaje, quien, codigo, frames, tipo, nombre, oficial, origen}]
  buscar(clips, texto, personaje, tipo, origen)
  tecnicas(pid, us)                 -> {codigo: (capsula, nombre oficial, "tecnica"|"definitiva")}  (#CCM de su CAM)
  receta(destino, tramos, mezcla, duracion) -> receta del motor (biblioteca_anim.aplicar_receta)
  duracion_natural(tramos, mezcla)  -> fotogramas del empalme sin re-temporizar
  avisos(receta, filas, ingles)     -> [(nivel "ok"|"warn"|"err", texto llano)]  (biblioteca_anim.validar_receta)
  guardar(env, receta, mod_dir, anotar) -> aplicar_receta con copia para «Deshacer» (indice de modkit_backups)
  receta_toml(receta, ruta)         -> la receta en .toml (python biblioteca_anim.py aplicar --receta ...)

Nombres: el oficial de la capsula (texto de docs/07_ports/.../skills_ocr.json, el juego los tiene como imagen) y, si no
lo hay, «tipo + n.º». El codigo hex solo en modo avanzado (lo pone la pagina).
Prueba: python "mod center hd/kit/prueba_generador.py"
"""
from __future__ import annotations

import functools
import json
import os
import re
import shutil
import time
import uuid

from kit import pelicula as pel

ba, sc = pel.ba, pel.sc
OCR = os.path.join(pel.base.RAIZ, "docs", "07_ports", "GOHAN_FUTURO_EXPLORACION_2026-10-06", "04_transformaciones",
                   "skills_ocr.json")
TIPOS = (("golpe", "Golpe", "Strike"), ("agarre", "Agarre", "Throw"), ("tecnica", "Técnica", "Technique"),
         ("definitiva", "Definitiva", "Ultimate"), ("pose", "Pose", "Pose"))
LIMITES = ("Límites: los golpes y el daño siguen siendo los del movimiento que sustituyes; no se pueden mover "
           "cinemáticas con su cámara, ni clips compartidos del juego, ni movimientos que el personaje no tiene.",
           "Limits: hits and damage stay those of the move you replace; cinematics with their camera, shared game "
           "clips and moves the character does not have cannot be moved.")


def tipo_txt(t, ingles=False):
    return next((en if ingles else es) for k, es, en in TIPOS if k == t)


@functools.lru_cache(None)
def _ocr():
    try:
        with open(OCR, encoding="utf-8") as fh:
            return {int(k): v.get("name", "").strip() for k, v in json.load(fh).items()}
    except (OSError, ValueError):
        return {}


@functools.lru_cache(None)
def tecnicas(pid, us=None):
    """{codigo BSK: (capsula, nombre oficial o "", tipo)} de los golpes con capsula del #CCM del CAM del personaje."""
    import capsulas as cp  # noqa: PLC0415
    with open(os.path.join(ba.MCH, "roster_db.json"), encoding="utf-8") as fh:
        e = next((x for x in json.load(fh)["ids"] if x["id"] == int(pid)), None)
    if not e or not e.get("cam"):
        return {}
    cam = sc.hd_entry(e["cam"], us)
    loc = cp.ccm_child(cam)
    if not loc:
        return {}
    ccm = cam[loc[0]:loc[0] + loc[1]]
    out = {}
    for o, _st in cp.ccm_blocks(ccm):
        c1, cap = cp.w(ccm, o, 5), cp.w(ccm, o, 8)
        if not cap:
            continue
        tipo = "definitiva" if c1 & 0x0008 else "tecnica"
        for k in range(12, 16):
            code = cp.w(ccm, o, k)
            if code and code not in out:
                out[code] = (cap, _ocr().get(cap, ""), tipo)
    return out


def _tipo(fila, tec):
    if fila["codigo"] in tec:
        return tec[fila["codigo"]][2]
    return {"ataque": "golpe", "agarre": "agarre"}.get(fila["categoria"], "pose")


def clips(us=None):
    """Clips utilizables (con codigo y clip propio, banco 3) con nombre legible y tipo. Un clip al que apuntan varios
    codigos (suelo/aire, variantes) sale una vez: con el codigo que tiene nombre oficial, si alguno lo tiene."""
    unicos = {}
    for f in ba.biblioteca(us):
        if f["codigo"] is None or f["pool"] != 3 or not f["frames"]:
            continue
        tec = tecnicas(f["personaje"], us)
        c = dict(personaje=f["personaje"], codigo=f["codigo"], anim=f["anim"], frames=f["frames"], tipo=_tipo(f, tec),
                 oficial=tec.get(f["codigo"], (0, ""))[1], origen="B3")
        k = (f["personaje"], f["anim"])
        if k not in unicos or (c["oficial"] and not unicos[k]["oficial"]):
            unicos[k] = c
    out, cuenta = sorted(unicos.values(), key=lambda c: (c["personaje"], c["codigo"])), {}
    for c in out:
        clave = (c["personaje"], c["tipo"], c["oficial"])
        cuenta[clave] = c["n"] = cuenta.get(clave, 0) + 1
    return out


def nombre(c, ingles=False):
    """Nombre legible, sin codigo: «Dragon Fist», «Dragon Fist 2», «Golpe 14»."""
    if c["oficial"]:
        return c["oficial"] + ("" if c["n"] == 1 else " %d" % c["n"])
    return "%s %d" % (tipo_txt(c["tipo"], ingles), c["n"])


def quien(pid, ingles=False):
    from kit import nucleo  # noqa: PLC0415
    n = next(((en if ingles else es) for i, es, en in nucleo.DONORS if i == int(pid)), None)
    return n or ba._persona(pid)["nombre"]


def buscar(lista, texto="", personaje=None, tipo=None, origen=None, ingles=False):
    t = (texto or "").strip().lower()
    out = []
    for c in lista:
        if (personaje is not None and c["personaje"] != personaje) or (tipo and c["tipo"] != tipo) or (
                origen and c["origen"] != origen):
            continue
        if t and not any(t in s for s in (nombre(c, ingles).lower(), quien(c["personaje"], ingles).lower(),
                                          ba.NOMBRES.get(c["personaje"], ""), tipo_txt(c["tipo"], ingles).lower(),
                                          "%x" % c["codigo"], "0x%x" % c["codigo"])):
            continue
        out.append(c)
    return out


# ------------------------------------------------------------------------------------------ linea de tiempo
def largo(t):
    """Fotogramas de un tramo {personaje, codigo, frames, desde, hasta}."""
    return abs(int(t["hasta"]) - int(t["desde"])) + 1


def duracion_natural(tramos, mezcla):
    if not tramos:
        return 0
    total = largo(tramos[0])
    for t in tramos[1:]:
        total += largo(t) - max(0, min(int(mezcla), total, largo(t)))
    return total


def receta(destino, tramos, mezcla=0, duracion=0):
    """Receta del motor: tramo 1 = fuente, 2 = despues, 3+ = mas. duracion 0 = la natural."""
    if not tramos:
        raise ValueError("sin tramos")
    tr = [dict(personaje=int(t["personaje"]), codigo=int(t["codigo"]), desde=int(t["desde"]), hasta=int(t["hasta"]))
          for t in tramos]
    r = {"version": 1, "destino": dict(personaje=int(destino["personaje"]), codigo=int(destino["codigo"])),
         "fuente": tr[0]}
    if len(tr) > 1:
        r["despues"] = tr[1]
        r["mezcla"] = int(mezcla)
    if len(tr) > 2:
        r["mas"] = tr[2:]
    if duracion and int(duracion) != duracion_natural(tramos, mezcla):
        r["duracion"] = int(duracion)
    return r


_LLANO = [  # (patron del aviso del motor, es, en, nivel); {0},{1}... = numeros del aviso
    (r"no existe en el juego", "Ese movimiento no existe en el personaje.",
     "That move does not exist for the character.", "err"),
    (r"banco comun", "Ese movimiento usa un clip compartido del juego: aún no se puede mover.",
     "That move uses a shared game clip: it cannot be moved yet.", "err"),
    (r"huesos del cuerpo", "Los esqueletos no encajan: este personaje no puede hacer ese movimiento.",
     "The skeletons do not match: this character cannot do that move.", "err"),
    (r"no tiene .*esa parte no se movera", "Alguna pieza (pelo, capa, cola…) no se moverá: el personaje no la tiene.",
     "Some part (hair, cape, tail…) will not move: the character does not have it.", "warn"),
    (r"el clip dura", "Dura {0} fotogramas; los movimientos del juego van de {1} a {2}.",
     "It lasts {0} frames; the game's moves go from {1} to {2}.", "warn"),
    (r"golpes del destino", "Los golpes del movimiento original caen después del final del nuevo (acaba en el "
     "fotograma {last}): no saldrán. Alarga la duración.", "The original move's hits land after the new one ends "
     "(frame {last}): they will not happen. Make it longer.", "warn"),
    (r"clip de la fuente es de una cinematica", "El clip viene de una escena de definitiva: aquí se verá sin su cámara.",
     "The clip comes from an ultimate scene: here it will play without its camera.", "warn"),
    (r"es de una cinematica", "Es la escena de una definitiva: su cámara no se alargará con el clip.",
     "It is an ultimate's scene: its camera will not stretch with the clip.", "warn"),
]


def llano(aviso, ingles=False):
    """(nivel, texto llano) de un aviso de biblioteca_anim.validar_receta."""
    nums = re.findall(r"\d+", aviso.split(":", 1)[-1])
    for pat, es, en, nivel in _LLANO:
        if re.search(pat, aviso):
            txt = en if ingles else es
            return nivel, txt.format(*(nums + ["?"] * 3), last=nums[-1] if nums else "?")
    return ("err" if aviso.startswith("ERROR") else "warn"), aviso.split(":", 1)[-1].strip()


def avisos(r, filas=None, us=None, ingles=False):
    try:
        crudos = ba.validar_receta(r, filas, us)
    except Exception as ex:  # noqa: BLE001  (un clip roto no tumba la pantalla)
        crudos = ["ERROR: %s" % ex]
    return [llano(a, ingles) for a in crudos]


def mod_de(destino):
    return "tecnica_" + sc.slug(ba._persona(destino["personaje"])["nombre"]).lower()


def copia_para_deshacer(env, mod_dir, anotar):
    """Antes de tocar un mod: si existe, copia entera en modkit_backups anotada «movido» (al deshacer vuelve la copia).
    -> funcion que se llama DESPUES de escribir (anota «nuevo» si no existia: al deshacer se aparta)."""
    stamp = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex
    if os.path.isdir(mod_dir):
        rel = os.path.join(stamp + "-tecnica", os.path.basename(os.path.normpath(mod_dir)))
        shutil.copytree(mod_dir, os.path.join(env.backups, rel))
        return lambda: anotar(env, stamp, "movido", mod_dir, rel)
    return lambda: anotar(env, stamp, "nuevo", mod_dir, "")


def guardar(env, r, mod_dir, anotar):
    """aplicar_receta con «Deshacer» (copia_para_deshacer). -> resultado de aplicar_receta."""
    anotado = copia_para_deshacer(env, mod_dir, anotar)
    res = ba.aplicar_receta(r, mod_dir)
    anotado()
    return res


def mod_color(pid):
    return "kit_color_" + sc.slug(ba._persona(pid)["nombre"]).lower()


def guardar_colores(env, pid, aura, ki, anotar):
    """Aura y/o ki de otro tono (colores.mod_nativo, el mismo motor que `colores.py nativo`) con «Deshacer»."""
    import colores  # noqa: PLC0415
    mod = mod_color(pid)
    anotado = copia_para_deshacer(env, os.path.join(env.mods, mod), anotar)
    rep = colores.mod_nativo(int(pid), env.mods, mod, aura=aura, ki=ki)
    anotado()
    return mod, rep


def receta_toml(r, ruta):
    """La receta como .toml que lee biblioteca_anim.leer_receta (modo avanzado, lotes por consola)."""
    def tabla(d):
        return ["%s = %s" % (k, ("%#x" % v) if k == "codigo" else v) for k, v in d.items()]
    L = ["# Receta del generador de animaciones (python biblioteca_anim.py aplicar --receta este.toml --mod ...)",
         "version = 1"] + ["%s = %d" % (k, r[k]) for k in ("mezcla", "duracion") if k in r]
    for k in ("destino", "fuente", "despues"):
        if k in r:
            L += ["", "[%s]" % k] + tabla(r[k])
    for t in r.get("mas") or []:
        L += ["", "[[mas]]"] + tabla(t)
    with open(ruta, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")
    return ruta

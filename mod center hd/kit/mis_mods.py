# -*- coding: utf-8 -*-
"""«Mis mods» sin interfaz (KIT-10): activar/desactivar con Deshacer, «Volver al juego limpio», compartir en .zip,
tipo legible de cada mod y la última prueba en el juego.

Activar/desactivar usa el mecanismo del juego y del launcher: el marcador vacío `mods/<mod>/.disabled` (o la carpeta
`<mod>.disabled`), que respetan src/mods.cpp ListMods, rexglue afs.cpp ScanModDirs, el pack de texturas y
roster_build. Nada se borra: desactivar CREA el marcador (anotado «nuevo» en modkit_backups/indice.tsv) y activar lo
MUEVE a modkit_backups (anotado «movido»); así «Deshacer» (nucleo.deshacer_ultimo) devuelve el estado anterior.

  python "mod center hd/kit/mis_mods.py" limpio <carpeta_mods>            (desactiva todos; deshacer en el Kit)
  python "mod center hd/kit/mis_mods.py" activar|desactivar <carpeta_mods> <mod> [...]
  python "mod center hd/kit/mis_mods.py" compartir <carpeta_mod> [<salida.zip>]
"""
from __future__ import annotations

import json
import os
import sys
import time
import uuid
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
if os.path.dirname(HERE) not in sys.path:
    sys.path.insert(0, os.path.dirname(HERE))
from kit import choques  # noqa: E402

MARCA = ".disabled"
PRUEBA = "ultima_prueba.json"
TIPOS = {  # clave -> (es, en)
    "personaje": ("personaje", "character"), "traje": ("traje", "costume"), "escenario": ("escenario", "stage"),
    "tecnica": ("técnica", "technique"), "texturas": ("texturas", "textures"), "generado": ("lista de personajes (la "
               "hace el Kit)", "character list (made by the Kit)"), "audio": ("música y voces", "music and voices"),
    "data": ("datos del juego", "game data"), "historia": ("historia", "story"), "studio": ("cámaras", "cameras"),
    "other": ("otro", "other"),
}


def tipo(d, del_manifiesto=""):
    """Clave de TIPOS deducida de los ficheros del mod (lo que manda es lo que trae, no lo que dice)."""
    def hay(*p):
        return os.path.exists(os.path.join(d, *p))
    if hay("personaje.toml"):
        return "personaje"
    if hay("traje.toml"):
        return "traje"
    if hay("stage.toml"):
        return "escenario"
    if hay("animaciones.json"):
        return "tecnica"
    if hay("roster.toml"):
        return "generado"
    if hay("historia.toml"):
        return "historia"
    if hay("textures", "textures_meta.json") or _es_pack(d):
        return "texturas"
    return del_manifiesto if del_manifiesto in TIPOS else "other"


def _es_pack(d):
    for _dp, _ds, fs in os.walk(d):
        for f in fs:
            stem, ext = os.path.splitext(f)
            if ext.lower() in (".dds", ".png") and choques._PACK.match(stem):
                return True
    return False


def tipo_txt(clave, ingles=False):
    es, en = TIPOS.get(clave, (clave, clave))
    return en if ingles else es


# ------------------------------------------------------------------------------------------ activar
def activo(mods_dir, nombre):
    d = os.path.join(mods_dir, nombre)
    return os.path.isdir(d) and not os.path.exists(os.path.join(d, MARCA))


def poner(env, nombres, encender, anotar, stamp=None):
    """Activa o desactiva varios mods en UN lote deshacible. -> nombres cambiados. env: .mods y .backups."""
    stamp = stamp or time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6]   # dos en el mismo segundo: dos lotes
    hechos = []
    for n in nombres:
        d = os.path.join(env.mods, n)
        if encender:
            sufijo = d + MARCA
            if not os.path.isdir(d) and os.path.isdir(sufijo):        # carpeta «<mod>.disabled» -> «<mod>»
                os.replace(sufijo, d)
                anotar(env, stamp, "movido", sufijo, os.path.relpath(d, env.backups))
                hechos.append(n)
            marca = os.path.join(d, MARCA)
            if os.path.isfile(marca):
                dst_dir = os.path.join(env.backups, stamp + "-mis_mods")
                os.makedirs(dst_dir, exist_ok=True)
                dst = os.path.join(dst_dir, "%d_%s%s" % (len(os.listdir(dst_dir)), n, MARCA))
                os.replace(marca, dst)
                anotar(env, stamp, "movido", marca, os.path.relpath(dst, env.backups))
                if n not in hechos:
                    hechos.append(n)
        elif os.path.isdir(d) and not os.path.exists(os.path.join(d, MARCA)):
            marca = os.path.join(d, MARCA)
            with open(marca, "w", encoding="utf-8") as fh:
                fh.write("disabled\n")
            anotar(env, stamp, "nuevo", marca, "")
            hechos.append(n)
    return hechos


def juego_limpio(env, anotar):
    """Desactiva TODOS los mods activos (un solo lote: «Deshacer» los vuelve a encender todos)."""
    return poner(env, choques.activos(env.mods), False, anotar)


# ------------------------------------------------------------------------------------------ compartir
def compartir(mod_dir, destino=None):
    """<mod>.zip con la carpeta del mod dentro (lo que entiende «Instalar .zip»: planes.inspeccionar_zip).
    Sin el marcador .disabled. -> ruta del zip."""
    mod_dir = os.path.normpath(mod_dir)
    nombre = os.path.basename(mod_dir)
    if nombre.endswith(MARCA):
        nombre = nombre[:-len(MARCA)]
    destino = destino or os.path.join(os.path.dirname(os.path.dirname(mod_dir)), "mods_compartidos", nombre + ".zip")
    os.makedirs(os.path.dirname(destino), exist_ok=True)
    tmp = destino + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for dp, _ds, fs in os.walk(mod_dir):
            for f in fs:
                p = os.path.join(dp, f)
                rel = os.path.relpath(p, mod_dir)
                if rel == MARCA:
                    continue
                z.write(p, nombre + "/" + rel.replace(os.sep, "/"))
    os.replace(tmp, destino)
    return destino


# ------------------------------------------------------------------------------------------ última prueba
def guardar_prueba(env, estado, frase, lineas, mods):
    os.makedirs(env.backups, exist_ok=True)
    datos = {"fecha": time.strftime("%Y-%m-%d %H:%M"), "estado": estado, "frase": frase, "lineas": list(lineas),
             "mods": list(mods)}
    with open(os.path.join(env.backups, PRUEBA), "w", encoding="utf-8") as fh:
        json.dump(datos, fh, ensure_ascii=False, indent=1)
    return datos


def leer_prueba(env):
    try:
        with open(os.path.join(env.backups, PRUEBA), encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def estado(m, prueba, lista_choques, ingles=False):
    """(clave, texto) de la pastilla de cada mod: apagado | choque | visto | sin_probar."""
    t = (lambda es, en: en) if ingles else (lambda es, en: es)
    if not m["enabled"]:
        return "apagado", t("apagado", "off")
    mios = [c for c in choques.de_mod(lista_choques, m["name"]) if c["nivel"] == "error"]
    if mios:
        c = mios[0]
        otro = c["gana"] if c["gana"] != m["name"] else c["pierden"][0]
        return "choque", t("choca con «%s»", "clashes with '%s'") % (otro if len(otro) <= 14 else otro[:13] + "…")
    if prueba and prueba.get("estado") == "ok" and m["name"] in prueba.get("mods", []) and not any(
            ln.startswith("✕") and m["name"] in ln for ln in prueba.get("lineas", [])):
        return "visto", t("✓ visto en el juego", "✓ seen in the game")
    return "sin_probar", t("sin probar", "untested")


def _cli(argv):
    class Env:
        pass
    if len(argv) >= 2 and argv[0] in ("limpio", "activar", "desactivar"):
        sys.path.insert(0, os.path.dirname(HERE))
        from kit import nucleo  # noqa: PLC0415
        env = Env()
        env.mods = argv[1]
        env.backups = os.path.join(os.path.dirname(os.path.abspath(argv[1])), "modkit_backups")
        if argv[0] == "limpio":
            r = juego_limpio(env, nucleo.anotar_indice)
        else:
            r = poner(env, argv[2:], argv[0] == "activar", nucleo.anotar_indice)
        print("%s: %s" % (argv[0], ", ".join(r) or "nada que cambiar"))
        return 0
    if len(argv) >= 2 and argv[0] == "compartir":
        print(compartir(argv[1], argv[2] if len(argv) > 2 else None))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(_cli(sys.argv[1:]))

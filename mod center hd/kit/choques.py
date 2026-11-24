# -*- coding: utf-8 -*-
"""Choques entre mods activos (KIT-10): dos mods que escriben el mismo destino, quién gana HOY y por qué.

Reglas reales del juego (no inventadas; si cambian allí, cambian aquí):
- Entradas de AFS (mods/<mod>/us/<afs>/<N>) y archivos enteros (us/<archivo>): las carpetas de mods activas se
  ordenan con std::sort (rexglue-sdk-0.10/src/filesystem/afs.cpp:396, orden de código: MAYÚSCULAS antes que
  minúsculas) y gana la PRIMERA (afs.cpp:427 OverrideIndex, :456 AfsFindModOverride, :535 AfsFindModFileOverride).
  Escenarios (entrada >= 5000 de data_cmn) y recetas de animación (entrada del .anm del personaje) van por aquí.
- Texturas de packs (<hash16>_<W>x<H>...dds|png): gana el pack con MENOS texturas
  (rexglue-sdk-0.10/src/graphics/dbz3_texture_pack.cpp:823, :859 stable_sort); empate: orden alfabético de la
  carpeta (src/launcher/settings.cpp:2860).
- Plaza (id) pedida por dos personajes: la conserva quien ya la tenía en roster_ids.toml; si no, el primero en orden
  de carpeta; el otro recibe otra plaza libre (mod center hd/roster_build.py:2194-2199). No se pierde nada: es aviso.
Cambiar quién gana no tiene mecanismo (no hay orden de prioridad configurable): se resuelve desactivando uno.

  python "mod center hd/kit/choques.py" <carpeta_mods> [--json]
"""
from __future__ import annotations

import json
import os
import re
import sys

MARCA = ".disabled"
_PACK = re.compile(r"^([0-9A-Fa-f]{16})_\d+x\d")
_NUM = re.compile(r"^\s*(entrada|menu)\s*=\s*(\d+)", re.M)


def activos(mods_dir):
    """Carpetas de mods activas en el orden en que las mira el juego (afs.cpp:376-396)."""
    try:
        nombres = os.listdir(mods_dir)
    except OSError:
        return []
    out = [n for n in nombres if n and n[0] != "." and not n.endswith(MARCA)
           and os.path.isdir(os.path.join(mods_dir, n)) and not os.path.exists(os.path.join(mods_dir, n, MARCA))]
    return sorted(out)          # sorted() de Python = orden de código, como std::sort de std::filesystem::path


def _toml(path):
    try:
        import tomllib
        with open(path, "rb") as fh:
            data = fh.read()
        return tomllib.loads(data.decode("utf-8-sig"))
    except Exception:  # noqa: BLE001  (un toml roto lo dice validar.py, aquí no choca)
        return {}


def texturas(d):
    """{hash: ruta} de las texturas de pack del mod (vacío si no es un pack)."""
    out = {}
    for dp, _dirs, fs in os.walk(d):
        for f in fs:
            stem, ext = os.path.splitext(f)
            m = _PACK.match(stem) if ext.lower() in (".dds", ".png") else None
            if m:
                out.setdefault(m.group(1).lower(), os.path.join(dp, f))
    return out


def destinos(d):
    """{clave: texto} de lo que escribe un mod. Claves: ("afs", archivo, N) | ("archivo", nombre) |
    ("textura", hash) | ("plaza", id) | ("receta", personaje, codigo)."""
    out = {}
    for reg in ("us", "eu"):
        base = os.path.join(d, reg)
        try:
            hijos = os.listdir(base)
        except OSError:
            continue
        for h in hijos:
            p = os.path.join(base, h)
            if os.path.isfile(p):
                out[("archivo", h.lower())] = "%s/%s" % (reg, h)
            elif reg == "us" and os.path.isdir(p):           # entradas: solo us/ (afs.cpp:432)
                for n in os.listdir(p):
                    if n.isdigit() and len(n) <= 9 and str(int(n)) == n:
                        out[("afs", h.lower(), int(n))] = "us/%s/%s" % (h, n)
    for h in texturas(d):
        out[("textura", h)] = h
    ficha = _toml(os.path.join(d, "personaje.toml")).get("personaje", {})
    if isinstance(ficha, dict) and isinstance(ficha.get("id"), int):
        out[("plaza", ficha["id"])] = "id = %d" % ficha["id"]
    try:
        with open(os.path.join(d, "animaciones.json"), encoding="utf-8") as fh:
            for r in json.load(fh):
                dst = r.get("destino", {})
                out[("receta", int(dst["personaje"]), int(dst["codigo"]))] = "personaje %s, código %#x" % (
                    dst["personaje"], int(dst["codigo"]))
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        pass
    return out


def escenarios(d):
    """Entradas de data_cmn que declara su stage.toml (entrada y menu)."""
    try:
        with open(os.path.join(d, "stage.toml"), encoding="utf-8", errors="replace") as fh:
            return {int(n) for _k, n in _NUM.findall(fh.read())}
    except OSError:
        return set()


def _plazas_previas(mods_dir):
    doc = _toml(os.path.join(mods_dir, "roster_ids.toml"))
    return {str(k): v for k, v in doc.get("ids", {}).items() if isinstance(v, int)}


def _explicar(tipo, gana, pierden, n, ingles, ej):
    quien = ", ".join("«%s»" % p for p in pierden)
    if tipo == "textura":
        es = ("«%s» y %s cambian %d imágenes iguales. Ahora gana %s (es el pack más pequeño: el juego pone encima "
              "el que trae menos imágenes)." % (gana, quien, n, gana))
        en = ("'%s' and %s change %d of the same images. Now %s wins (it is the smaller pack: the game puts the one "
              "with fewer images on top)." % (gana, quien.replace("«", "'").replace("»", "'"), n, gana))
    elif tipo == "plaza":
        es = ("«%s» y %s piden la misma plaza de la rueda. Se la queda %s; el otro sale en otra plaza libre (no se "
              "pierde nada)." % (gana, quien, gana))
        en = ("'%s' and %s ask for the same wheel slot. %s keeps it; the other goes to another free slot (nothing is "
              "lost)." % (gana, quien.replace("«", "'").replace("»", "'"), gana))
    elif tipo == "receta":
        es = ("«%s» y %s cambian el mismo movimiento (%s). Ahora gana %s (va antes por orden alfabético) y el "
              "otro no se ve." % (gana, quien, ej, gana))
        en = ("'%s' and %s change the same move (%s). Now %s wins (it comes first alphabetically) and the other "
              "is not seen." % (gana, quien.replace("«", "'").replace("»", "'"), ej, gana))
    else:
        es = ("«%s» y %s cambian %d archivo(s) iguales del juego (%s). Ahora gana %s (va antes por orden "
              "alfabético) y lo del otro no se ve." % (gana, quien, n, ej, gana))
        en = ("'%s' and %s change %d of the same game file(s) (%s). Now %s wins (it comes first alphabetically) "
              "and the other's is not seen." % (gana, quien.replace("«", "'").replace("»", "'"), n, ej, gana))
    return en if ingles else es


def choques(mods_dir, ingles=False):
    """[{tipo, gana, pierden, claves, texto}] entre los mods ACTIVOS de mods_dir. Un mod solo aparece con quien
    choca de verdad; varios destinos del mismo par y tipo van en un solo choque."""
    orden = activos(mods_dir)
    por_clave, packs = {}, {}
    for m in orden:
        d = os.path.join(mods_dir, m)
        ds = destinos(d)
        packs[m] = sum(1 for k in ds if k[0] == "textura")
        for k, txt in ds.items():
            por_clave.setdefault(k, []).append((m, txt))
    previas = _plazas_previas(mods_dir)
    grupos = {}
    recetas_par = set()
    for k, quienes in por_clave.items():
        if len(quienes) < 2:
            continue
        nombres = [q[0] for q in quienes]           # ya en el orden del juego
        if k[0] == "textura":
            nombres = sorted(nombres, key=lambda n: packs[n])       # stable: empate = orden alfabetico
        elif k[0] == "plaza":
            nombres = sorted(nombres, key=lambda n: (previas.get(n) != k[1], orden.index(n)))
        tipo = "afs" if k[0] in ("afs", "archivo") else k[0]
        if tipo == "receta":
            recetas_par.add(frozenset(nombres))
        g = grupos.setdefault((tipo, nombres[0], tuple(nombres[1:])), [])
        g.append(quienes[0][1] if k[0] != "afs" else "%s/%d" % (k[1], k[2]))
    out = []
    for (tipo, gana, pierden), claves in sorted(grupos.items()):
        if tipo == "afs" and frozenset((gana,) + pierden) in recetas_par and all(c.startswith("data_cmn.afs/")
                                                                                 for c in claves):
            continue      # ya sale como «mismo movimiento» (la receta escribe el .anm entero del personaje)
        ej = ", ".join(sorted(claves)[:3]) + ("…" if len(claves) > 3 else "")
        out.append({"tipo": tipo, "gana": gana, "pierden": list(pierden), "claves": sorted(claves),
                    "nivel": "aviso" if tipo == "plaza" else "error",
                    "texto": _explicar(tipo, gana, list(pierden), len(claves), ingles, ej)})
    return out


def de_mod(lista, mod):
    return [c for c in lista if c["gana"] == mod or mod in c["pierden"]]


def prueba():
    import tempfile  # noqa: PLC0415
    with tempfile.TemporaryDirectory(dir=os.environ.get("TEMP")) as t:
        def f(*p, data=b"x"):
            ruta = os.path.join(t, *p)
            os.makedirs(os.path.dirname(ruta), exist_ok=True)
            with open(ruta, "wb") as fh:
                fh.write(data)
        f("b_mod", "us", "data_cmn.afs", "292", "geom.bin")
        f("A_mod", "us", "data_cmn.afs", "292")
        f("c_off", "us", "data_cmn.afs", "292")
        f("c_off", MARCA)
        f("pack_grande", "0123456789abcdef_64x64_DXT1.dds")
        f("pack_grande", "1111111111111111_64x64_DXT1.dds")
        f("pack_peque", "0123456789ABCDEF_128x128_DXT5.png")
        c = choques(t)
        afs = [x for x in c if x["tipo"] == "afs"]
        assert len(afs) == 1 and afs[0]["gana"] == "A_mod" and afs[0]["pierden"] == ["b_mod"], c
        tex = [x for x in c if x["tipo"] == "textura"]
        assert len(tex) == 1 and tex[0]["gana"] == "pack_peque", c
    print("choques: prueba OK")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] != "--prueba":
        r = choques(sys.argv[1], "--en" in sys.argv)
        print(json.dumps(r, ensure_ascii=False, indent=1) if "--json" in sys.argv else
              "\n".join("%s: %s" % (x["nivel"], x["texto"]) for x in r) or "sin choques")
        sys.exit(1 if any(x["nivel"] == "error" for x in r) else 0)
    prueba()

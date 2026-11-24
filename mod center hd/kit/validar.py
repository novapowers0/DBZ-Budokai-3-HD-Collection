# -*- coding: utf-8 -*-
"""«Validar mod» (KIT-10): TODO de una vez, sin abrir el juego.

Reúne revisar.py (TOML roto, ficheros que faltan, voces, carpeta de más), los límites (8 formas, entradas de
escenario, plazas libres con `roster_build.py estado`), claves desconocidas con sugerencia (las válidas salen del
docstring y del código de roster_build.py y de docs/02_mods/ESCENARIOS_STAGE_TOML.md, sin editarlos), huesos de los
trajes (kit/traje.py), recetas de animación (biblioteca_anim.validar_receta) y choques con otros mods (kit/choques.py).

validar(carpeta, mods_dir=None, us=None, ingles=False, pesado=True) -> [(nivel, mensaje llano, detalle técnico)]
nivel: "error" (no cargará bien o se pierde algo) | "aviso" (carga, conviene mirarlo) | "ok" (comprobado y bien).
pesado=False se salta lo lento (huesos, recetas, plazas).

  python "mod center hd/kit/validar.py" <carpeta_mod> [--json] [--mods DIR] [--us DIR] [--en] [--rapido]
  (código de salida 1 si hay errores: sirve para lotes)
"""
from __future__ import annotations

import difflib
import functools
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MC = os.path.dirname(HERE)                       # mod center hd
RAIZ = os.path.dirname(MC)
if MC not in sys.path:
    sys.path.insert(0, MC)
from kit import choques, escenarios, revisar  # noqa: E402

MAX_FORMAS = 8
_CLAVE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=", re.M)


def _t(ingles):
    return (lambda es, en: en) if ingles else (lambda es, en: es)


@functools.lru_cache(maxsize=1)
def claves_validas():
    """{"personaje": set, "capsula": set, "traje": set, "escenario": set} de las fuentes de verdad (solo lectura)."""
    try:
        with open(os.path.join(MC, "roster_build.py"), encoding="utf-8") as fh:
            rb = fh.read()
    except OSError:
        rb = ""
    doc = rb.split('"""', 2)[1] if rb.count('"""') >= 2 else ""

    def bloque(desde, hasta):
        i = doc.find(desde)
        if i < 0:
            return set()
        j = doc.find(hasta, i + len(desde)) if hasta else -1
        return set(_CLAVE.findall(doc[i + len(desde):j if j > 0 else None]))
    try:      # importar.py escribe en personaje.toml claves suyas (preset, origen_*) que lee al reimportar
        with open(os.path.join(MC, "importar.py"), encoding="utf-8") as fh:
            imp = fh.read()
    except OSError:
        imp = ""
    usadas = set()
    for txt in (rb, imp):
        usadas |= set(re.findall(r'\.get\("([a-z_0-9]+)"', txt)) | set(re.findall(r'\["([a-z_0-9]+)"\]', txt))
    usadas |= set(re.findall(r"\b([a-z_0-9]+)=json\.dumps", imp))
    per = bloque("[personaje]", "[[capsula]]") | usadas
    cap = bloque("[[capsula]]", "Un traje EXTRA") | usadas | {"forma"}
    tra = bloque("[traje]", "Los trajes se") | usadas | {"tono", "colores"}
    esc = set()
    try:
        with open(os.path.join(RAIZ, "docs", "02_mods", "ESCENARIOS_STAGE_TOML.md"), encoding="utf-8") as fh:
            esc |= set(re.findall(r"^\|\s*`([a-z_0-9]+)`", fh.read(), re.M))
    except OSError:
        pass
    try:      # lo que de verdad lee el juego (src/escenarios.cpp CargarDeMods), si esta el codigo
        with open(os.path.join(RAIZ, "src", "escenarios.cpp"), encoding="utf-8", errors="replace") as fh:
            esc |= set(re.findall(r'k == "([a-z_0-9]+)"', fh.read()))
    except OSError:
        pass
    return {"personaje": per, "capsula": cap, "traje": tra, "escenario": esc}


def _sugerir(clave, validas):
    m = difflib.get_close_matches(clave, sorted(validas), n=1, cutoff=0.75)
    return m[0] if m else None


def _desconocidas(fichero, seccion, claves, validas, t):
    out = []
    for k in claves:
        if k in validas or k.startswith("_"):
            continue
        s = _sugerir(k, validas)
        if s:
            out.append(("error", t("%s: la clave «%s» no existe (¿«%s»?). El juego la ignora: corrígela." % (fichero, k, s),
                                   "%s: key '%s' does not exist ('%s'?). The game ignores it: fix it." % (fichero, k, s)),
                        "[%s] %s -> %s" % (seccion, k, s)))
        else:
            out.append(("aviso", t("%s: la clave «%s» no la conoce el juego: se ignora." % (fichero, k),
                                   "%s: the game does not know key '%s': it is ignored." % (fichero, k)),
                        "[%s] %s" % (seccion, k)))
    return out


def _formas(fichero, n, t, que):
    if n > MAX_FORMAS:
        return [("error", t("%s: tiene %d formas; el juego admite %d como mucho. Quita %d." % (
            fichero, n, MAX_FORMAS, n - MAX_FORMAS), "%s: it has %d forms; the game allows %d at most. Remove %d." % (
            fichero, n, MAX_FORMAS, n - MAX_FORMAS)), "%s = %d > %d" % (que, n, MAX_FORMAS))]
    return []


def _escenario(carpeta, t):
    out = []
    ruta = os.path.join(carpeta, "stage.toml")
    with open(ruta, encoding="utf-8", errors="replace") as fh:
        texto = fh.read()
    validas = claves_validas()["escenario"]
    bloques = re.split(r"(?m)^\s*\[\[?escenario\]\]?\s*$", texto)[1:]
    if not bloques and not re.search(r"(?m)^\s*\[parche\]", texto):     # [parche]: src/stage_ext.cpp, otro formato
        out.append(("error", t("stage.toml no tiene ningún [[escenario]]: el juego no añadirá nada.",
                               "stage.toml has no [[escenario]]: the game will add nothing."), "stage.toml"))
    for b in bloques:
        b = re.split(r"(?m)^\s*\[", b)[0]
        kv = {}
        for ln in b.splitlines():
            ln = ln.split("#", 1)[0] if not ln.strip().startswith("#") else ""
            if "=" in ln:
                k, v = ln.split("=", 1)
                kv.setdefault(k.strip(), v.strip().strip('"'))
        out += _desconocidas("stage.toml", "escenario", list(kv), validas, t)
        nombre = kv.get("nombre", "")
        if not nombre:
            out.append(("error", t("stage.toml: falta el nombre del escenario.", "stage.toml: the stage name is missing."),
                        "nombre"))
        elif len(nombre) > escenarios.LIMITE:
            out.append(("aviso", t("stage.toml: el nombre «%s» pasa de %d letras: el selector lo corta." % (
                nombre, escenarios.LIMITE), "stage.toml: name '%s' is over %d letters: the selector cuts it." % (
                nombre, escenarios.LIMITE)), "len(nombre) = %d" % len(nombre)))
        ent = kv.get("entrada", "")
        if not ent.isdigit():
            out.append(("error", t("stage.toml: falta la entrada del escenario.", "stage.toml: the stage entry is missing."),
                        "entrada"))
        else:
            e = int(ent)
            if not escenarios.BASE <= e <= escenarios.ULTIMA:
                out.append(("error", t("stage.toml: la entrada %d está fuera del hueco de escenarios nuevos (%d-%d)." % (
                    e, escenarios.BASE, escenarios.ULTIMA), "stage.toml: entry %d is outside the new-stage range "
                    "(%d-%d)." % (e, escenarios.BASE, escenarios.ULTIMA)), "entrada = %d" % e))
            elif not os.path.exists(os.path.join(carpeta, "us", "data_cmn.afs", str(e))):
                out.append(("aviso", t("stage.toml pide la entrada %d, pero el mod no la trae: el escenario saldría "
                                       "vacío." % e, "stage.toml asks for entry %d, but the mod does not have it: the "
                                       "stage would be empty." % e), "us/data_cmn.afs/%d" % e))
        tema = kv.get("tema", "")
        if tema and (not tema.lstrip("-").isdigit() or not 0 <= int(tema) <= 16):
            out.append(("error", t("stage.toml: el ambiente (tema) %s no existe: va de 0 a 16." % tema,
                                   "stage.toml: mood (tema) %s does not exist: it goes from 0 to 16." % tema), "tema"))
        for k, v in kv.items():
            if re.fullmatch(r"[\w./\\-]+\.[A-Za-z]\w{0,3}", v) and not os.path.exists(os.path.join(carpeta, v)):
                out.append(("error", t("stage.toml usa «%s», y ese archivo no está en el mod." % v,
                                       "stage.toml uses '%s', and that file is not in the mod." % v), "%s = %s" % (k, v)))
    return out


def _huesos(carpeta, ficha, us, t):
    """Trajes: cada modelo con el esqueleto de su forma (kit/traje.py, el mismo que el asistente)."""
    from kit import traje  # noqa: PLC0415
    cid = ficha.get("personaje")
    if not isinstance(cid, int):
        return []
    fs = traje.formas(cid)
    rv, out, bien = traje.Revisor(us), [], 0
    for f, o in enumerate(ficha.get("modelos", [])[:len(fs)]):
        if not isinstance(o, str) or o.startswith("juego:"):
            continue
        ruta = os.path.join(carpeta, o)
        if not os.path.isfile(ruta):
            continue                       # ya lo dice «ese archivo no está en el mod»
        ex = rv.revisar(ruta, cid, f)
        nombre = fs[f]["en" if t("", "x") else "es"]
        if ex is None:
            bien += 1
        else:
            out.append(("error", t("Forma %s («%s»): %s" % (nombre, o, ex.es), "Form %s ('%s'): %s" % (nombre, o, ex.en)),
                        ex.detalle))
    if bien and not out:
        out.append(("ok", t("Los %d modelos tienen el esqueleto de su forma." % bien,
                            "The %d models have their form's skeleton." % bien), "kit/traje.py preparar"))
    return out


def _recetas(carpeta, us, ingles, t):
    try:
        with open(os.path.join(carpeta, "animaciones.json"), encoding="utf-8") as fh:
            recetas = json.load(fh)
    except (OSError, ValueError) as ex:
        return [("error", t("animaciones.json no se puede leer.", "animaciones.json cannot be read."), str(ex))]
    try:
        from kit import generador  # noqa: PLC0415
    except Exception as ex:  # noqa: BLE001  (sin numpy: no se comprueba, se dice)
        return [("aviso", t("No he podido comprobar los movimientos (faltan librerías).",
                            "I could not check the moves (libraries missing)."), str(ex))]
    out = []
    for r in recetas if isinstance(recetas, list) else []:
        try:
            crudos = generador.ba.validar_receta(r, None, us)
        except Exception as ex:  # noqa: BLE001
            out.append(("aviso", t("No he podido comprobar un movimiento.", "I could not check a move."), str(ex)))
            continue
        for a in crudos:
            nivel, txt = generador.llano(a, ingles)
            out.append(({"err": "error"}.get(nivel, "aviso"), txt, a))
    if recetas and not out:
        out.append(("ok", t("%d movimiento(s) comprobado(s): encajan con el personaje." % len(recetas),
                            "%d move(s) checked: they fit the character." % len(recetas)), "validar_receta"))
    return out


def _plaza(carpeta, mods_dir, t):
    mod = os.path.basename(os.path.normpath(carpeta))
    try:
        p = subprocess.run([sys.executable, os.path.join(MC, "roster_build.py"), "estado", "--json", "--mods", mods_dir],
                           capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        datos = json.loads(p.stdout[p.stdout.index("{"):])
    except (OSError, ValueError, subprocess.SubprocessError) as ex:
        return [("aviso", t("No he podido mirar las plazas libres de la rueda.", "I could not check the free wheel slots."),
                 str(ex))]
    yo = next((x for x in datos.get("personajes", []) if x["mod"] == mod), None)
    if yo is None:
        return []
    if yo["sin_plaza"]:
        return [("error", t("No queda plaza libre en la rueda: desactiva otro personaje para jugar con este.",
                            "No free wheel slot left: disable another character to play with this one."),
                 "roster_build.py estado: sin_plaza")]
    if yo["conflicto"]:
        return [("aviso", t("Pide la plaza %s, que ya usa otro mod: saldrá en la %s." % (yo["id_pedido"], yo["id"]),
                            "It asks for slot %s, already used by another mod: it will use %s." % (
                                yo["id_pedido"], yo["id"])), "roster_build.py estado: conflicto")]
    libres = sum(1 for x in datos.get("plazas", []) if x["estado"] == "libre")
    return [("ok", t("Tiene plaza en la rueda (quedan %d libres)." % libres,
                     "It has a wheel slot (%d free left)." % libres), "roster_build.py estado")]


def validar(carpeta, mods_dir=None, us=None, ingles=False, pesado=True):
    t = _t(ingles)
    carpeta = os.path.normpath(carpeta)
    mods_dir = mods_dir or os.path.dirname(carpeta)
    out = [(n, x, "revisar.py") for n, x in revisar.revisar(carpeta, ingles)]
    if out and out[0][1] in (t("No encuentro la carpeta del mod.", "I cannot find the mod folder."),
                             t("La carpeta está vacía.", "The folder is empty.")):
        return out
    validas = claves_validas()
    formas = None
    for fichero, sec in (("personaje.toml", "personaje"), ("traje.toml", "traje")):
        ruta = os.path.join(carpeta, fichero)
        if not os.path.isfile(ruta):
            continue
        datos = choques._toml(ruta)
        ficha = datos.get(sec, {})
        ficha = (ficha[0] if ficha else {}) if isinstance(ficha, list) else ficha
        out += _desconocidas(fichero, sec, list(ficha), validas[sec], t)
        for i, c in enumerate(datos.get("capsula", []) if sec == "personaje" else []):
            out += _desconocidas(fichero, "capsula %d" % (i + 1), list(c), validas["capsula"], t)
        if sec == "personaje":
            n = ficha.get("formas") if isinstance(ficha.get("formas"), int) else 0
            n = max([n] + [len(ficha[k]) for k in ("ki_base", "aura_formas", "modelo_forma")
                           if isinstance(ficha.get(k), list)] + [ficha.get("modelos_por_traje", 0)
                                                                 if isinstance(ficha.get("modelos_por_traje"), int) else 0])
        else:
            n = len(ficha.get("modelos", [])) if isinstance(ficha.get("modelos"), list) else 0
        out += _formas(fichero, n, t, "formas")
        formas = n
        if pesado and sec == "traje" and us:
            try:
                out += _huesos(carpeta, ficha, us, t)
            except Exception as ex:  # noqa: BLE001
                out.append(("aviso", t("No he podido comprobar los huesos de los modelos.",
                                       "I could not check the models' bones."), str(ex)))
        if pesado and sec == "personaje":
            out += _plaza(carpeta, mods_dir, t)
    if os.path.isfile(os.path.join(carpeta, "stage.toml")):
        out += _escenario(carpeta, t)
    if os.path.isfile(os.path.join(carpeta, "animaciones.json")) and pesado and us:
        out += _recetas(carpeta, us, ingles, t)
    if formas and formas <= MAX_FORMAS:
        out.append(("ok", t("%d de %d formas." % (formas, MAX_FORMAS), "%d of %d forms." % (formas, MAX_FORMAS)), ""))
    if not any(n == "error" and "archivo" in x for n, x, _d in out):
        out.append(("ok", t("Los archivos que nombra están en el mod.", "The files it names are in the mod."), ""))
    mod = os.path.basename(carpeta)
    if os.path.normcase(os.path.dirname(carpeta)) == os.path.normcase(os.path.normpath(mods_dir)):
        if mod in choques.activos(mods_dir):
            mios = choques.de_mod(choques.choques(mods_dir, ingles), mod)
            for c in mios:
                out.append((c["nivel"], c["texto"], "%s: %s" % (c["tipo"], ", ".join(c["claves"][:8]))))
            if not mios:
                out.append(("ok", t("No choca con otros mods.", "It does not clash with other mods."), "choques.py"))
        else:
            out.append(("aviso", t("Está desactivado: no se han buscado choques.", "It is disabled: clashes were not "
                                   "checked."), ""))
    orden = {"error": 0, "aviso": 1, "ok": 2}
    return sorted(out, key=lambda x: orden.get(x[0], 1))


def main(argv=None):
    import argparse  # noqa: PLC0415
    ap = argparse.ArgumentParser(description="Validar un mod (todo de una vez)")
    ap.add_argument("carpeta")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--mods", default=None, help="carpeta mods (por defecto, la que contiene el mod)")
    ap.add_argument("--us", default=None)
    ap.add_argument("--en", action="store_true")
    ap.add_argument("--rapido", action="store_true", help="sin huesos, recetas ni plazas")
    a = ap.parse_args(argv)
    us = a.us
    if not us and not a.rapido:
        try:
            from kit import base  # noqa: PLC0415
            us = base.find_us()
        except Exception:  # noqa: BLE001
            us = None
    r = validar(a.carpeta, a.mods, us, a.en, not a.rapido)
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    if a.json:
        print(json.dumps([{"nivel": n, "mensaje": m, "detalle": d} for n, m, d in r], ensure_ascii=False, indent=1))
    else:
        marca = {"error": "✕", "aviso": "⚠", "ok": "✓"}
        for n, m, d in r:
            print("%s %s%s" % (marca.get(n, "·"), m, ("   [%s]" % d) if d else ""))
    return 1 if any(n == "error" for n, _m, _d in r) else 0


if __name__ == "__main__":
    sys.exit(main())

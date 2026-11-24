# -*- coding: utf-8 -*-
"""«Nuevo escenario» (KIT-07) sin interfaz: estados de cada escenario, fotos en la cache, entrada libre, validacion,
ordenes del conversor y resumen para «Montar y probar». La conversion la hace SIEMPRE escenarios_convertir.py
(por su CLI, con este fichero de lanzador para fijarle la carpeta us/ y ps2_games del jugador): aqui no se duplica.

  python kit/escenarios.py fotos US PS2 b2|b1 CARPETA        fotos de todos los escenarios de la ISO (las que falten)
  python kit/escenarios.py convertir US PS2 ZONA_A <args de escenarios_convertir.py>
      ZONA_A = entrada de la zona A cuando se crea la zona B en otra entrada que la de siempre (0 = nada que fijar)
"""
from __future__ import annotations

import hashlib
import os
import re
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
MCH = os.path.dirname(HERE)
ESTADO = os.path.join(HERE, "escenarios_estado.toml")
CONVERSOR = os.path.join(MCH, "escenarios_convertir.py")
LIMITE = 15                      # stage.toml `nombre`: max. 15 letras (ESCENARIOS_STAGE_TOML.md; el conversor corta)
BASE, ULTIMA = 5000, 5099        # data_cmn 5000-5199 reservado a escenarios; la casilla va en entrada + 100
CVAR_ZONAS = "dbz3_escenarios_zonas_b"


def _toml(path):
    try:
        import tomllib  # noqa: PLC0415
    except ImportError:          # Python < 3.11
        from kit.nucleo import read_toml  # noqa: PLC0415
        return read_toml(path)
    with open(path, "rb") as fh:
        return tomllib.load(fh)


def datos():
    """{'escenario': [...], 'ambiente': [...]} de escenarios_estado.toml."""
    return _toml(ESTADO)


def estados(juego=None):
    out = [e for e in datos().get("escenario", []) if juego is None or e["juego"] == juego]
    return sorted(out, key=lambda e: (e["juego"], e["grupo"]))


def estado(juego, grupo):
    return next((e for e in estados(juego) if e["grupo"] == grupo), None)


def propuesto(e, ingles=False):
    """Nombre que se propone para el selector (cabe en LIMITE: se quitan palabras del final si hace falta)."""
    n = e["en"] if ingles else e.get("corto") or e["es"]
    while len(n) > LIMITE and " " in n:
        n = n.rsplit(" ", 1)[0]
    return n[:LIMITE]


def ambientes():
    return datos().get("ambiente", [])


# ------------------------------------------------------------------------------------------ ISO y fotos
def iso_de(juego, ps2):
    sys.path[:0] = [p for p in (MCH,) if p not in sys.path]
    import iso  # noqa: PLC0415
    try:
        return iso.find_game(juego, root=ps2)
    except Exception:  # noqa: BLE001 (carpeta rara: como si no hubiera ISO)
        return None


def falta_iso(juego, ps2, ingles=False):
    """Frase llana cuando no hay ISO del juego."""
    nombre = {"b2": "Dragon Ball Z Budokai 2", "b1": "Dragon Ball Z Budokai"}[juego]
    if ingles:
        return ("I cannot find your %s (PS2) disc image. Put the .iso file in the folder %s and come back here."
                % (nombre, ps2))
    return ("No encuentro tu copia de %s (PS2). Pon el archivo .iso en la carpeta %s y vuelve aquí."
            % (nombre, ps2))


def foto_png(cache, iso_path, juego, grupo):
    """Ruta de la foto de un escenario en la cache del Kit (cambia si cambia la ISO)."""
    try:
        tam = os.path.getsize(iso_path)
    except OSError:
        tam = 0
    clave = hashlib.sha1(("%s|%d" % (os.path.abspath(iso_path), tam)).encode()).hexdigest()[:10]
    return os.path.join(cache, "escenario_%s_g%d_%s.png" % (juego, grupo, clave))


def cmd_fotos(python, us, ps2, juego, cache):
    return [python, os.path.abspath(__file__), "fotos", us or "-", ps2, juego, cache]


# ------------------------------------------------------------------------------------------ entradas
_NUM = re.compile(r"^\s*(entrada|menu)\s*=\s*(\d+)", re.M)


def ocupadas(mods_dir):
    """Entradas de data_cmn ya usadas por algun mod (stage.toml de todos, tambien apagados, y carpetas us/data_cmn)."""
    usadas = set()
    if not os.path.isdir(mods_dir):
        return usadas
    for m in os.listdir(mods_dir):
        d = os.path.join(mods_dir, m)
        st = os.path.join(d, "stage.toml")
        if os.path.isfile(st):
            with open(st, encoding="utf-8", errors="replace") as fh:
                txt = fh.read()
            ents = [int(n) for k, n in _NUM.findall(txt) if k == "entrada"]
            usadas.update(ents)
            usadas.update(int(n) for k, n in _NUM.findall(txt) if k == "menu")
            if "menu" not in txt:
                usadas.update(e + 100 for e in ents)
        afs = os.path.join(d, "us", "data_cmn.afs")
        if os.path.isdir(afs):
            usadas.update(int(n) for n in os.listdir(afs) if n.isdigit())
    return usadas


def entrada_preferida(juego, grupo, zona_b=False):
    """La que pondria el conversor (5000+grupo; B1 5020+grupo; zona B de B2 5040+grupo)."""
    return BASE + (20 if juego == "b1" else 0) + (40 if zona_b else 0) + grupo


def entrada_libre(usadas, juego, grupo, zona_b=False):
    """La preferida si esta libre (ella y su casilla +100); si no, la primera libre >= 5000. None si no queda."""
    pref = entrada_preferida(juego, grupo, zona_b)
    for n in [pref] + list(range(BASE, ULTIMA + 1)):
        if n not in usadas and n + 100 not in usadas:
            return n
    return None


def carpeta_de(nombre):
    """Nombre de carpeta (y `nombre` del stage.toml) sin tildes ni signos raros: «Montañas» -> «Montanas»."""
    s = unicodedata.normalize("NFKD", nombre).encode("ascii", "ignore").decode()
    return re.sub(r"\s+", " ", re.sub(r"[^A-Za-z0-9 _-]", "", s)).strip()


def validar(nombre, juego, grupo, tema, mods_dir, entrada=None, ingles=False):
    """Texto del primer problema o None."""
    def t(es, en):
        return en if ingles else es
    e = estado(juego, grupo)
    if e is None:
        return t("Elige un escenario de la lista.", "Pick a stage from the list.")
    if e["estado"] == "no_funciona":
        return t("Ese escenario aún no funciona (cierra el juego). Elige otro.",
                 "That stage does not work yet (it closes the game). Pick another one.")
    nombre = (nombre or "").strip()
    if not nombre or not carpeta_de(nombre):
        return t("Escribe un nombre para el selector (con alguna letra o número).",
                 "Type a name for the selector (with some letter or number).")
    if len(nombre) > LIMITE:
        return t("El nombre es demasiado largo: como mucho %d letras (sobran %d)." % (LIMITE, len(nombre) - LIMITE),
                 "The name is too long: %d letters at most (%d too many)." % (LIMITE, len(nombre) - LIMITE))
    if not isinstance(tema, int) or not 0 <= tema <= 16:
        return t("Ese ambiente no existe. Elige uno de los botones.", "That mood does not exist. Pick one of the buttons.")
    if os.path.exists(os.path.join(mods_dir, carpeta_de(nombre))):
        return t("Ya tienes un mod con ese nombre. Ponle otro.", "You already have a mod with that name. Pick another.")
    usadas = ocupadas(mods_dir)
    if entrada is not None and (entrada in usadas or entrada + 100 in usadas or not BASE <= entrada <= ULTIMA):
        return t("Ese hueco de escenario ya lo usa otro mod.", "That stage slot is already used by another mod.")
    if entrada_libre(usadas, juego, grupo) is None:
        return t("Ya no caben más escenarios nuevos (100). Apaga o quita alguno en «Mis mods».",
                 "No room for more new stages (100). Turn off or remove one in 'My mods'.")
    return None


# ------------------------------------------------------------------------------------------ ordenes
def ordenes(python, us, ps2, mods_dir, juego, grupo, nombre, tema, usadas=None):
    """[(titulo_es, titulo_en, orden, carpeta)]: la zona A y, si el escenario la tiene, la zona B (oculta)."""
    usadas = set(ocupadas(mods_dir) if usadas is None else usadas)
    carpeta = carpeta_de(nombre)
    ent = entrada_libre(usadas, juego, grupo)
    usadas |= {ent, ent + 100}
    base = [python, os.path.abspath(__file__), "convertir", us or "-", ps2]
    comunes = ["--juego", juego, "--mods", mods_dir, "--tema", str(tema)]
    out = [("Crear el escenario", "Create the stage",
            base + ["0", "mod", str(grupo), "--nombre", carpeta, "--rotulo", nombre.strip(), "--entrada", str(ent)]
            + comunes, carpeta)]
    e = estado(juego, grupo)
    if e and e.get("zona_b"):
        eb = entrada_libre(usadas, juego, grupo, zona_b=True)
        zona_a = "0" if ent == entrada_preferida(juego, grupo) else str(ent)
        cb = carpeta_de(nombre + " B")
        out.append(("Crear su segunda zona", "Create its second area",
                    base + [zona_a, "mod", str(grupo), "--zona", "b", "--nombre", cb, "--rotulo", nombre.strip(),
                            "--entrada", str(eb)] + comunes, cb))
    return out


# ------------------------------------------------------------------------------------------ resumen
def resumen(mod_dir, ingles=False):
    """Lineas llanas para «Montar y probar»: ['✓ 1 escenario nuevo: Montes nevados']. [] si no es de escenario."""
    st = os.path.join(mod_dir, "stage.toml")
    if not os.path.isfile(st):
        return []
    try:
        altas = _toml(st).get("escenario", [])
    except Exception:  # noqa: BLE001
        return ["✗ " + ("stage.toml cannot be read" if ingles else "no se puede leer stage.toml")]
    if isinstance(altas, dict):
        altas = [altas]
    vis = [a for a in altas if str(a.get("selector", True)).lower() not in ("false", "no", "0")]
    ocultas = [a for a in altas if a not in vis]
    out = []
    if vis:
        nombres = ", ".join(str(a.get("titulo") or a.get("nombre", "?")) for a in vis)
        out.append(("✓ %d new stage: %s" if ingles else "✓ %d escenario nuevo: %s") % (len(vis), nombres)
                   if len(vis) == 1 else
                   ("✓ %d new stages: %s" if ingles else "✓ %d escenarios nuevos: %s") % (len(vis), nombres))
    for a in ocultas:
        if a.get("zona_b"):
            out.append(("✓ second area of «%s» (works with the setting %s)" if ingles else
                        "✓ segunda zona de «%s» (funciona con el ajuste %s)") % (a.get("titulo") or a.get("nombre"),
                                                                                  CVAR_ZONAS))
    return out


# ------------------------------------------------------------------------------------------ lanzador
def _conversor(us, ps2):
    sys.path[:0] = [MCH]
    from pathlib import Path  # noqa: PLC0415
    import escenarios_convertir as e  # noqa: PLC0415
    if us != "-":
        e.US = Path(us)
    e.PS2 = Path(ps2)
    return e


def _fotos(us, ps2, juego, cache):
    e = _conversor(us, ps2)
    fuente = e.IsoB2(None, juego)
    n = len(fuente.escenarios())
    print("escenarios: %d" % n, flush=True)
    os.makedirs(cache, exist_ok=True)
    for g in range(n):
        png = foto_png(cache, fuente.ruta, juego, g)
        if os.path.isfile(png) or os.path.isfile(png + ".fallo"):
            continue
        try:
            datos_ = e.escenario_b2(fuente, g, 44)
            h = e.hijos_amb(datos_, ">")
            e.foto_escenario(datos_[h[0][0]:h[0][0] + h[0][1]]).save(png + ".tmp.png")
            os.replace(png + ".tmp.png", png)
            print("foto %d ok" % g, flush=True)
        except BaseException as ex:  # noqa: BLE001 (B1 g9/g11 no convierten: se marca y se sigue)
            open(png + ".fallo", "w").write(repr(ex)[:300])
            print("foto %d fallo: %r" % (g, ex), flush=True)


def _convertir(us, ps2, zona_a, args):
    e = _conversor(us, ps2)
    sys.argv = [CONVERSOR] + args
    r = e.main()
    if zona_a != "0":            # la zona B apunta a la entrada de su zona A: el conversor supone la de siempre
        mods = args[args.index("--mods") + 1]
        st = os.path.join(mods, args[args.index("--nombre") + 1], "stage.toml")
        with open(st, encoding="utf-8", newline="") as fh:
            txt = fh.read()
        with open(st, "w", encoding="utf-8", newline="") as fh:
            fh.write(re.sub(r"^zona_b = \d+", "zona_b = %s" % zona_a, txt, count=1, flags=re.M))
    return r


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="backslashreplace")
    except (AttributeError, ValueError):
        pass
    if sys.argv[1] == "fotos":
        _fotos(*sys.argv[2:6])
    elif sys.argv[1] == "convertir":
        sys.exit(_convertir(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:]) or 0)

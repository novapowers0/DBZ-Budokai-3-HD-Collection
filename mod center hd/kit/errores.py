# -*- coding: utf-8 -*-
"""Errores en lenguaje llano: explicar(texto) -> (que paso, que hacer, accion). Nunca devuelve "Traceback".
El texto tecnico va solo al registro; informe() lo junta para pegarlo en el Discord."""
from __future__ import annotations

import platform
import re
import sys

# (regex, que paso ES, que hacer ES, que paso EN, que hacer EN, accion); la primera que case gana.
REGLAS = [
    (r"No module named|ModuleNotFoundError|ImportError",
     "Falta una librería de Python (numpy, Pillow o scipy).", "Pulsa «Instalar librerías» y vuelve a intentarlo.",
     "A Python library is missing (numpy, Pillow or scipy).", "Press 'Install libraries' and try again.", "pip"),
    (r"Microsoft Store|exit code 9009|código 9009|WinError 193|Python was not found",
     "Python no está bien instalado (el acceso directo de Microsoft Store no sirve).",
     "Instala Python 3.11 o superior desde python.org y marca «Add python.exe to PATH».",
     "Python is not installed properly (the Microsoft Store shortcut does not work).",
     "Install Python 3.11 or newer from python.org and tick 'Add python.exe to PATH'.", None),
    (r"PermissionError|Permission denied|WinError (5|32)\b|being used by another process",
     "Un archivo está en uso o protegido.",
     "Cierra el juego (dbz3.exe) y cualquier programa que tenga abierta la carpeta de mods, y reintenta.",
     "A file is in use or protected.",
     "Close the game (dbz3.exe) and any program that has the mods folder open, then retry.", None),
    (r"No space left|WinError 112\b|Errno 28\b|disk full",
     "No queda espacio en el disco.", "Libera espacio (hacen falta unos GB) y reintenta.",
     "The disk is full.", "Free up some space (a few GB are needed) and retry.", None),
    (r"Ya hay una tarea|task is already running",
     "Ya hay otra tarea en marcha.", "Espera a que termine o pulsa «Cancelar» abajo.",
     "Another task is already running.", "Wait for it to finish or press 'Cancel' below.", None),
    (r"data_cmn|vista previa|VistaError|IndexError",
     "Los datos del juego no son los que el kit espera (por ejemplo el data_cmn.afs de otro juego o versión).",
     "En «Comprobar instalación» revisa que la carpeta de datos (us) sea la de Budokai 3 HD original.",
     "The game data is not what the kit expects (for example another game's or version's data_cmn.afs).",
     "In 'Check installation' make sure the data folder (us) is the original Budokai 3 HD one.", None),
    (r"KeyError",
     "La historia o el mod está incompleto (le falta un dato que otro apartado necesita).",
     "Abre el mod o la historia, revisa que no falte ningún paso y vuelve a montarlo.",
     "The story or mod is incomplete (a piece of data another part needs is missing).",
     "Open the mod or story, check no step is missing and build it again.", None),
    (r"TypeError|ValueError|XTRS_BODY|BODY_1|zero-size|AttributeError",
     "El modelo trae algo que el kit no sabe leer (por ejemplo sin huesos o con piezas vacías).",
     "Prueba con otro donante o con el preset «estilo B3» y cuéntalo en el Discord si sigue pasando.",
     "The model has something the kit cannot read (for example no bones or empty parts).",
     "Try another donor or the 'B3 style' preset, and tell us on Discord if it keeps happening.", None),
    (r"texture_b3|textura",
     "Una textura no tiene el tamaño o formato que necesita.",
     "Usa un PNG del mismo tamaño que el original y vuelve a pulsar «Reconstruir».",
     "A texture does not have the size or format it needs.",
     "Use a PNG of the same size as the original and press 'Rebuild' again.", None),
    (r"already exists|ya existe|FileExistsError",
     "Ya existe un mod con ese nombre.", "Elige otro nombre o desactiva el mod anterior.",
     "A mod with that name already exists.", "Pick another name or disable the old mod.", None),
    (r"FileNotFoundError|No such file or directory|WinError [23]\b",
     "Falta un archivo que la herramienta necesita.",
     "En «Comprobar instalación» mira que la carpeta del juego y los datos (us) estén bien.",
     "A file the tool needs is missing.",
     "In 'Check installation' make sure the game folder and its data (us) are right.", None),
    (r"UnicodeDecodeError|UnicodeEncodeError|codec can't",
     "Un archivo tiene letras raras que no se pueden leer.", "Guárdalo como UTF-8 y reintenta.",
     "A file has characters that cannot be read.", "Save it as UTF-8 and retry.", None),
    (r"JSONDecodeError|TOMLDecodeError|Invalid statement|Expecting value",
     "Un archivo de configuración (.toml/.json) tiene un fallo de escritura.",
     "Ábrelo, mira la línea que indica el registro y corrige la coma o comilla que falta.",
     "A configuration file (.toml/.json) has a typing mistake.",
     "Open it, check the line named in the log and fix the missing comma or quote.", None),
    (r"MemoryError|out of memory",
     "Se acabó la memoria del ordenador.", "Cierra otros programas y reintenta.",
     "The computer ran out of memory.", "Close other programs and retry.", None),
]


def limpiar(texto: str) -> str:
    """Ultima linea util, sin rutas largas ni la palabra Traceback (para la frase de rescate)."""
    for ln in reversed((texto or "").splitlines()):
        ln = ln.strip()
        if ln and not ln.startswith(("[exit code", "File ", "Traceback", "^")):
            return re.sub(r"[A-Za-z]:\\[^\s\"']+", "…", ln)[:160]
    return ""


def explicar(texto: str, en: bool = False):
    """(que paso, que hacer, accion); accion = 'pip' si el arreglo es instalar librerias, si no None."""
    texto = texto or ""
    for rx, qe, he, qn, hn, acc in REGLAS:
        if re.search(rx, texto, re.I):
            return (qn, hn, acc) if en else (qe, he, acc)
    det = limpiar(texto)
    if en:
        return ("Something went wrong" + (" (%s)." % det if det else "."),
                "Press 'Copy report' and paste it on the Discord to get help.", None)
    return ("Algo ha fallado" + (" (%s)." % det if det else "."),
            "Pulsa «Copiar informe» y pégalo en el Discord para que te ayuden.", None)


def frase(texto: str, en: bool = False) -> str:
    q, h, _ = explicar(texto, en)
    return "%s %s" % (q, h)


def informe(app, lineas: int = 120) -> str:
    """Texto para pegar en el Discord: sistema, rutas y el final del registro (aqui si va el detalle tecnico)."""
    env = getattr(app, "env", None)
    out = ["Mod Kit - informe", "Python %s | %s" % (sys.version.split()[0], platform.platform())]
    if env is not None:
        out += ["Kit: %s" % getattr(env, "kit", "?"), "Juego: %s" % (getattr(env, "game", None) or "(no encontrado)"),
                "Datos us: %s" % (getattr(env, "us", None) or "(no encontrado)")]
    buf = "".join(t for t, _ in getattr(app, "log_buffer", []))
    out += ["--- registro (últimas líneas) ---"] + buf.splitlines()[-lineas:]
    return "\n".join(out)


EJEMPLOS = [
    "Traceback (most recent call last):\n  File \"x.py\", line 925, in v\nIndexError: list index out of range",
    "Algo falló (código 1): KeyError: 'next'",
    "Traceback (most recent call last):\nKeyError: 'next'",
    "TypeError: 'NoneType' object is not subscriptable (XTRS_BODY_1)",
    "ValueError: zero-size array to reduction operation (HEAD)",
    "Ya hay una tarea en marcha.",
    "No se pudo ejecutar Python (el alias de Microsoft Store no sirve)",
    "ejecuta: texture_b3.py build --mod x",
    "ERROR: vista previa: falta ui/cara.png",
    "Traceback (most recent call last):\nZeroDivisionError: division by zero",
    "ModuleNotFoundError: No module named 'numpy'",
    "",
]


def prueba(extra=()):
    """Cada texto debe dar frase y arreglo en ES y EN, sin 'Traceback'. Devuelve cuantos ha probado."""
    n = 0
    for t in list(EJEMPLOS) + list(extra):
        for en in (False, True):
            q, h, _ = explicar(t, en)
            assert q and h and "traceback" not in (q + h).lower(), (t, q, h)
            n += 1
    return n


if __name__ == "__main__":
    print("errores OK:", prueba())

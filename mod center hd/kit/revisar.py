# -*- coding: utf-8 -*-
"""«Revisar este mod»: mira una carpeta de mod sin abrir el juego y dice en lenguaje llano qué falla y qué hacer.
revisar(carpeta, en) -> [(nivel, texto)], nivel "error" (no cargará bien) o "aviso" (carga, pero conviene mirarlo)."""
from __future__ import annotations

import os
import tomllib
import wave

TOMLS = ("personaje.toml", "traje.toml")   # stage.toml no: repite claves (`nubes`) y el juego lo lee por lineas
DE_MOD = TOMLS + ("stage.toml", "historia.toml", "roster.toml")


def _parece_fichero(v):
    # la misma regla que MissingTomlFiles de src/mods.cpp: lo que el launcher tambien marca como fichero
    punto = v.rfind(".")
    return (":" not in v and punto >= 0 and len(v) - punto <= 5 and " " not in v and ("/" in v or "\\" in v))


def _cadenas(n):
    if isinstance(n, dict):
        for v in n.values():
            yield from _cadenas(v)
    elif isinstance(n, list):
        for v in n:
            yield from _cadenas(v)
    elif isinstance(n, str):
        yield n


def _ficha(datos):
    p = datos.get("personaje", {})       # [personaje] o [[personaje]]
    return (p[0] if p else {}) if isinstance(p, list) else p


def _voces(carpeta, datos, t):
    out = []
    for clave in ("voces", "gritos"):
        v = _ficha(datos).get(clave)
        if not (isinstance(v, str) and v.startswith("wav:")):
            continue
        sub = os.path.join(carpeta, v[4:])
        wavs = [f for f in os.listdir(sub) if f.lower().endswith(".wav")] if os.path.isdir(sub) else []
        if not wavs:
            out.append(("error", t("«%s» pide los .wav de la carpeta «%s», pero no hay ninguno. Crea esa carpeta y pon "
                                   "ahí los .wav, o quita esa línea." % (clave, v[4:]),
                                   "'%s' asks for the .wav files in folder '%s', but there are none. Create that folder "
                                   "and put the .wav files there, or remove that line." % (clave, v[4:]))))
        for f in wavs:
            try:
                with wave.open(os.path.join(sub, f), "rb") as w:
                    w.getnframes()
            except (wave.Error, EOFError, OSError):
                out.append(("error", t("«%s» no es un .wav normal. Expórtalo de nuevo como WAV PCM." % f,
                                       "'%s' is not a normal .wav. Export it again as PCM WAV." % f)))
    return out


def revisar(carpeta, en=False):
    t = (lambda es, ing: ing) if en else (lambda es, ing: es)
    if not os.path.isdir(carpeta):
        return [("error", t("No encuentro la carpeta del mod.", "I cannot find the mod folder."))]
    nombres = [f for f in os.listdir(carpeta) if f != "__MACOSX"]
    if not nombres:
        return [("error", t("La carpeta está vacía.", "The folder is empty."))]
    out, hay = [], False
    for f in TOMLS:
        ruta = os.path.join(carpeta, f)
        if not os.path.isfile(ruta):
            continue
        hay = True
        try:
            with open(ruta, "rb") as h:
                datos = tomllib.load(h)
        except (tomllib.TOMLDecodeError, UnicodeDecodeError) as e:
            out.append(("error", t("«%s» tiene un error de escritura: %s. Suele ser una comilla o un corchete sin cerrar."
                                   % (f, e), "'%s' has a typing error: %s. It is usually an unclosed quote or bracket."
                                   % (f, e))))
            continue
        for v in sorted({s for s in _cadenas(datos) if _parece_fichero(s)}):
            if not os.path.exists(os.path.join(carpeta, v)):
                out.append(("error", t("«%s» usa «%s», y ese archivo no está en el mod." % (f, v),
                                       "'%s' uses '%s', and that file is not in the mod." % (f, v))))
        out += _voces(carpeta, datos, t)
        if f == "personaje.toml":
            p = _ficha(datos)
            if not p.get("nombre"):
                out.append(("error", t("Falta el nombre del personaje (nombre = \"...\").",
                                       "The character name is missing (nombre = \"...\").")))
            if "donante" not in p:
                out.append(("aviso", t("No indica «donante», el personaje del que hereda lo que le falte.",
                                       "It has no 'donante', the character it inherits what it lacks from.")))
            for c in datos.get("capsula", []):
                if not c.get("nombre"):
                    out.append(("aviso", t("Una cápsula no tiene nombre: saldrá sin nombre en el juego.",
                                           "A capsule has no name: it will show without a name in the game.")))
    if not hay and not any(f in nombres for f in DE_MOD):
        subs = [f for f in nombres if os.path.isdir(os.path.join(carpeta, f))]
        if len(subs) == 1 and any(os.path.isfile(os.path.join(carpeta, subs[0], f)) for f in DE_MOD):
            out.append(("error", t("El mod está dentro de una carpeta de más («%s»). Sácalo un nivel hacia arriba." % subs[0],
                                   "The mod is inside an extra folder ('%s'). Move it up one level." % subs[0])))
    return out


def prueba():
    import tempfile  # noqa: PLC0415
    with tempfile.TemporaryDirectory(dir=os.environ.get("TEMP")) as d:
        m = os.path.join(d, "m")
        assert revisar(m)[0][0] == "error"
        os.makedirs(m)
        assert "vacía" in revisar(m)[0][1]
        os.makedirs(os.path.join(m, "voces"))
        os.makedirs(os.path.join(m, "moveset"))
        with open(os.path.join(m, "moveset", "anm.bin"), "wb") as h:
            h.write(b"x")
        with open(os.path.join(m, "personaje.toml"), "w", encoding="utf-8") as h:
            h.write('[personaje]\nnombre = "A"\ndonante = 1\nmoveset = ["moveset/anm.bin"]\ncamara = "moveset/c.bin"\n'
                    'voces = "wav:voces"\n[[capsula]]\nnombre = ""\n')
        r = revisar(m)
        assert sum("moveset/c.bin" in x[1] for x in r) == 1 and not any("anm.bin" in x[1] for x in r), r
        assert any("wav" in x[1] and x[0] == "error" for x in r), r
        assert any(x[0] == "aviso" and "cápsula" in x[1] for x in r), r
        with open(os.path.join(m, "voces", "1.wav"), "wb") as h:
            h.write(b"no")
        assert any("no es un .wav" in x[1] for x in revisar(m))
        with wave.open(os.path.join(m, "voces", "1.wav"), "wb") as w:
            w.setnchannels(1), w.setsampwidth(2), w.setframerate(24000), w.writeframes(b"\0\0")
        assert not any("wav" in x[1] for x in revisar(m))
        with open(os.path.join(m, "personaje.toml"), "w", encoding="utf-8") as h:
            h.write('[personaje\n')
        assert "error de escritura" in revisar(m)[0][1]
        o = os.path.join(d, "o")
        os.makedirs(os.path.join(o, "dentro"))
        with open(os.path.join(o, "dentro", "stage.toml"), "w", encoding="utf-8") as h:
            h.write("")
        assert "carpeta de más" in revisar(o)[0][1]
    print("revisar: prueba OK")


if __name__ == "__main__":
    prueba()

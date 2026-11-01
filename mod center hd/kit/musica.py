# -*- coding: utf-8 -*-
"""«Poner música»: un fichero de audio del jugador -> mods/<carpeta>/musica.afs (un AFS con un ADX estéreo) + pistas.txt.
El juego (src/mods_opcionales.cpp, fuente «propia») lo ofrece junto a las demás pistas; no sustituye música nativa.
ffmpeg convierte mp3/wav/ogg/flac...; un .adx se usa tal cual. Sin ffmpeg solo vale .adx."""
from __future__ import annotations

import os
import shutil
import struct
import subprocess
import tempfile

EXT_ADX = ".adx"


def ffmpeg():
    p = shutil.which("ffmpeg")
    alt = os.path.join(os.environ.get("APPDATA", ""), "easy-whisper-electron", "whisper-workspace", "bin", "ffmpeg.exe")
    return p or (alt if os.path.exists(alt) else None)


def a_adx(origen, destino):
    """Deja `origen` como ADX estéreo en `destino`. Lanza RuntimeError con una frase llana si no puede."""
    if origen.lower().endswith(EXT_ADX):
        shutil.copyfile(origen, destino)
        return
    exe = ffmpeg()
    if not exe:
        raise RuntimeError("Falta ffmpeg para convertir ese audio. Instálalo o pasa tu canción a .adx tú.")
    r = subprocess.run([exe, "-v", "error", "-y", "-i", origen, "-vn", "-ac", "2", "-ar", "44100", "-f", "adx", destino],
                       capture_output=True, text=True, errors="replace")
    if r.returncode or not os.path.isfile(destino) or os.path.getsize(destino) < 64:
        raise RuntimeError("No se pudo convertir ese audio: %s" % (r.stderr.strip()[-200:] or "formato no reconocido"))


def cabecera_adx(datos):
    """-> (hz, canales, segundos) o None. Mismas reglas que CabeceraAdx del motor."""
    if len(datos) < 32 or datos[0] != 0x80:
        return None
    hz, muestras = struct.unpack(">II", datos[8:16])
    canales = datos[7]
    if not 8000 <= hz <= 48000 or canales not in (1, 2):
        return None
    return hz, canales, muestras / hz


def escribir_afs(destino, entradas):
    """AFS mínimo (tabla offset/tamaño, datos alineados a 0x800), el mismo que lee AfsPs2."""
    n = len(entradas)
    pos = (8 + 8 * n + 0x7FF) & ~0x7FF
    tabla, cuerpo = b"", b""
    for e in entradas:
        tabla += struct.pack("<II", pos + len(cuerpo), len(e))
        cuerpo += e + b"\0" * (-len(e) % 0x800)
    with open(destino, "wb") as fh:
        fh.write(b"AFS\0" + struct.pack("<I", n) + tabla)
        fh.write(b"\0" * (pos - 8 - len(tabla)) + cuerpo)


def crear(carpeta, origen, nombre):
    """Crea mods/<carpeta>/ con la canción. Devuelve la frase del resultado."""
    with tempfile.TemporaryDirectory(prefix="kit4_") as tmp:
        adx = os.path.join(tmp, "p.adx")
        a_adx(origen, adx)
        datos = open(adx, "rb").read()
    cab = cabecera_adx(datos)
    if not cab:
        raise RuntimeError("El audio convertido no es un ADX válido.")
    if cab[2] < 1:
        raise RuntimeError("La canción es demasiado corta (menos de 1 segundo).")
    os.makedirs(carpeta)
    escribir_afs(os.path.join(carpeta, "musica.afs"), [datos])
    with open(os.path.join(carpeta, "pistas.txt"), "w", encoding="utf-8") as fh:
        fh.write(nombre.replace("\n", " ").strip() + "\n")
    with open(os.path.join(carpeta, "manifest.txt"), "w", encoding="utf-8") as fh:
        fh.write("name=%s\ndescription=Canción propia creada con el asistente Poner música del Mod Kit\nauthor=\n"
                 "version=1.0\ntype=musica\n" % nombre.replace("\n", " ").strip())
    return "«%s»: %.0f s a %d Hz" % (nombre, cab[2], cab[0])

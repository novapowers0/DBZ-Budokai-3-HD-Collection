#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Fuente XV2 del Kit: conversion en el PC del jugador, sin assets distribuidos.

importar.py xv2 GOK --sobre 0 [--mods DIR] [--us DIR] [--cpk data.cpk]
--selftest usa datos inventados: no abre juegos ni registro, ni escribe temporales.
"""
import argparse
from contextlib import contextmanager
from datetime import datetime
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
MISSING = "No encuentro Xenoverse 2. Instala tu copia de Steam o indica su data.cpk con --cpk."


def registro_steam():
    try:
        import winreg
    except ImportError:
        return []
    out = []
    for hive, key, value in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                             (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
                             (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam", "InstallPath")):
        try:
            with winreg.OpenKey(hive, key) as k:
                out.append(winreg.QueryValueEx(k, value)[0])
        except OSError:
            pass
    return out


def valores_vdf(text, key):
    return [m.replace(r"\\", "\\").replace(r'\"', '"') for m in
            re.findall(r'"' + re.escape(key) + r'"\s*"((?:\\.|[^"\\])*)"', text, re.I)]


def buscar_xv2():
    """Registro + libraryfolders.vdf + appmanifest_454650.acf; solo lectura.

    Una ruta explicita por variable tiene prioridad, incluso si no existe.
    """
    for variable in ("DBZ3_XV2_CPK", "DBZ3_XENO2_CPK"):
        if os.environ.get(variable):
            p = Path(os.environ[variable])
            return str(p) if p.is_file() else None
    roots = registro_steam() + [str(Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))/"Steam"),
                                str(Path(os.environ.get("ProgramFiles", "C:/Program Files"))/"Steam")]
    for drive in "CDEFGHIJKLMNOPQRSTUVWXYZ":
        roots += [drive + ":/SteamLibrary", drive + ":/Steam"]
    libraries = list(roots)
    for root in dict.fromkeys(roots):
        try:
            text = (Path(root)/"steamapps/libraryfolders.vdf").read_text(encoding="utf-8-sig")
            libraries += valores_vdf(text, "path")
        except OSError:
            pass
    for library in dict.fromkeys(libraries):
        steamapps = Path(library)/"steamapps"
        names = ["DB Xenoverse 2"]
        try:
            text = (steamapps/"appmanifest_454650.acf").read_text(encoding="utf-8-sig")
            names = valores_vdf(text, "installdir") + names
        except OSError:
            pass
        for name in names:
            # No seguir rutas absolutas/ascendentes de un manifiesto ajeno.
            if "/" in name or "\\" in name or name in ("", ".", ".."):
                continue
            path = steamapps/"common"/name/"cpk/data.cpk"
            if path.is_file():
                return str(path)
    return None


@contextmanager
def fuente_legacy():
    """La lista y los demas ports XV2 del Kit usan la misma deteccion Steam."""
    key = "DBZ3_XENO2_CPK"
    previous = os.environ.get(key)
    os.environ[key] = buscar_xv2() or "__xv2_no_encontrado__.cpk"
    try:
        yield
    finally:
        if previous is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = previous


def proteger_fuente(source, outputs):
    """Las salidas y respaldos nunca se escriben dentro de la copia de XV2."""
    source = Path(source).resolve()
    root = source.parent.parent if source.parent.name.casefold() == "cpk" else source.parent
    for output in outputs:
        try:
            Path(output).resolve().relative_to(root)
        except ValueError:
            continue
        raise ValueError("Elige una carpeta de salida fuera de Xenoverse 2.")


def mod_name(character, target):
    from xeno_a_b3 import CHARACTERS
    character = character.upper()
    if character not in CHARACTERS:
        raise ValueError("Esta conversion admite GOK (Goku), VGT (Vegeta) y PIC (Piccolo).")
    if target < 0:
        raise ValueError("--sobre debe ser el ID de un personaje de B3 (por ejemplo, 0 = Goku).")
    return "xv2_%s_sobre_%d_local" % (character, target)


def convert(character, target, afs, work, cpk=None):
    """Genera AMB y diagnostico local; se comparte con el importador de personajes."""
    sys.path.insert(0, str(ROOT/"mod center hd"))
    import roster_build as rb
    import swap_b3
    from xeno_a_b3 import main as converter
    character = character.upper()
    mod_name(character, target)
    donor = rb.IDS.get(target)
    if not donor or not donor.get("models"):
        raise ValueError("No existe ese personaje de B3: %s" % target)
    source = str(cpk) if cpk else buscar_xv2()
    if not source or not Path(source).is_file():
        raise FileNotFoundError(MISSING)
    output, report = Path(work)/(character+".bin"), Path(work)/(character+".json")
    rc = converter(["--personaje", character, "--cpk", source, "--b3-afs", str(afs),
                    "--entrada", str(donor["models"][0]), "--xbdecompress", swap_b3.XBDECOMPRESS,
                    "--trabajo", str(work), "--salida", str(output), "--informe", str(report)])
    if rc:
        raise ValueError("No se pudo convertir el personaje; consulta el mensaje anterior.")
    return output, report, donor["models"][0]


def install(args):
    sys.path.insert(0, str(ROOT/"mod center hd"))
    import roster_build as rb
    import swap_b3
    from xeno_prueba import padded, slot_size
    name = mod_name(args.personaje, args.sobre)
    mods = Path(args.mods or rb.default_mods()).resolve()
    afs = Path(args.us or rb.default_us())/"data_cmn.afs"
    if not afs.is_file():
        raise FileNotFoundError("No encuentro los datos de B3. Indica su carpeta us con --us.")
    source = args.cpk or buscar_xv2()
    if not source or not Path(source).is_file():
        raise FileNotFoundError(MISSING)
    # Temporales del jugador: junto a sus datos, fuera del arbol de Steam.
    base = Path(args.trabajo) if args.trabajo else mods.parent/"user_data/dbz3/xv2_conversion"
    proteger_fuente(source, [mods, base] + ([args.respaldo] if args.respaldo else []))
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="importar_", dir=base))
    output, report, entry = convert(args.personaje, args.sobre, afs, work, source)
    lzx = work/"modelo.lzx"
    subprocess.run([swap_b3.XBCOMPRESS, "/N:2048", str(output), str(lzx)], check=True, capture_output=True)
    payload = lzx.read_bytes()
    if payload[:4] != b"\x0f\xf5\x12\xee":
        raise ValueError("No se pudo comprimir el modelo para B3.")
    staging = work/"mod"
    target = staging/"us/data_cmn.afs"/str(entry)
    target.mkdir(parents=True)
    (target/"geom.bin").write_bytes(padded(payload, slot_size(afs, entry)))
    # Mismo modelo para la region EU: la tabla usa los mismos indices.
    eu = staging/"eu/data_cmn.afs"/str(entry)
    eu.mkdir(parents=True)
    shutil.copy2(target/"geom.bin", eu/"geom.bin")
    shutil.copy2(report, staging/"conversion_local.json")
    (staging/"README.txt").write_text(
        "Modelo convertido en tu PC desde tu copia de Xenoverse 2. No distribuir.\n"
        "Activa o desactiva este mod desde el Centro de mods.\n"
        "Reemplaza solo el modelo base; movimientos y voces son de B3.\n"
        "Manos: reposo, puno, abierta, senalar y garra; poses aproximadas [H].\n"
        "Cara: instantaneas de los gestos FCE XV2 elegidas por listas B3; sin reloj EAN ni lipsync continuo.\n", encoding="utf-8")
    (staging/".disabled").write_text("Activalo desde el Centro de mods cuando quieras usarlo.\n", encoding="utf-8")
    mods.mkdir(parents=True, exist_ok=True)
    destination = mods/name
    backup = None
    if destination.exists():
        # En el PC del jugador, respaldo portable. En este encargo, cuarentena D:.
        backup_root = Path(args.respaldo) if args.respaldo else mods.parent/"mods_respaldo"
        backup_root.mkdir(parents=True, exist_ok=True)
        backup = backup_root/(name+"_"+datetime.now().strftime("%Y%m%d_%H%M%S_%f"))
        shutil.move(str(destination), str(backup))
    try:
        shutil.move(str(staging), str(destination))
    except BaseException:
        if backup is not None and not destination.exists():
            shutil.move(str(backup), str(destination))
        raise
    print("Mod local creado y desactivado: %s\nActivalo desde el Centro de mods." % destination)
    return 0


def selftest():
    """Steam en unidad no habitual, manifiesto y ausencia; nada del PC real."""
    from unittest.mock import patch
    steam = Path("Z:/SteamInventado")
    library = Path("Y:/Juegos inventados")
    cpk = library/"steamapps/common/XV2 local/cpk/data.cpk"
    files = {steam/"steamapps/libraryfolders.vdf": '"libraryfolders" { "0" { "path" "Y:/Juegos inventados" } }',
             library/"steamapps/appmanifest_454650.acf": '"AppState" { "installdir" "XV2 local" }'}
    def read(p, **kw):
        if p not in files:
            raise FileNotFoundError(str(p))
        return files[p]
    with patch.dict(os.environ, {}, clear=True), patch(__name__+".registro_steam", return_value=[str(steam)]), \
         patch.object(Path, "read_text", read), patch.object(Path, "is_file", lambda p: p == cpk):
        assert buscar_xv2() == str(cpk)
        with patch.dict(os.environ, {"DBZ3_XV2_CPK": "no_existe.cpk"}):
            assert buscar_xv2() is None
        with patch.dict(os.environ, {"DBZ3_XENO2_CPK": str(cpk)}):
            assert buscar_xv2() == str(cpk)
        with patch.object(Path, "is_file", return_value=False):
            assert buscar_xv2() is None
    assert valores_vdf('"path" "Q:\\\\Steam\\\\Biblioteca"', "path") == ["Q:\\Steam\\Biblioteca"]
    assert mod_name("gok", 0) == "xv2_GOK_sobre_0_local"
    from xeno_binary import rejects
    rejects(lambda: mod_name("../GOK", 0))
    rejects(lambda: mod_name("GOK", -1))
    proteger_fuente("X:/Steam/common/XV2/cpk/data.cpk", ["Y:/mis mods"])
    rejects(lambda: proteger_fuente("X:/Steam/common/XV2/cpk/data.cpk", ["X:/Steam/common/XV2/mods"]))
    print("xeno_importar: N0 OK (registro/biblioteca/manifiesto simulados, rutas explicitas, ausencia, IDs)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("personaje", nargs="?", default="GOK")
    ap.add_argument("--sobre", type=int, required=False)
    ap.add_argument("--mods")
    ap.add_argument("--us")
    ap.add_argument("--cpk")
    ap.add_argument("--trabajo")
    ap.add_argument("--respaldo")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument('--cac', action='store_true', help='prepara el catalogo local del creador; no activa piezas XV2')
    ap.add_argument('--juego', type=Path, help='carpeta del juego B3 para --cac')
    args = ap.parse_args(argv)
    if args.selftest:
        selftest()
        return 0
    if args.cac:
        if not args.juego:
            ap.error('--cac requiere --juego (carpeta B3)')
        sys.path.insert(0, str(ROOT/'mod center hd/xv2'))
        from xv2_cac_preparar import preparar
        try:
            preparar(args.juego, args.cpk, args.respaldo)
            return 0
        except (OSError, ValueError) as error:
            print(str(error), file=sys.stderr)
            return 1
    if args.sobre is None:
        ap.error("indica --sobre con el ID de B3: 0 Goku, 7 Vegeta, 11 Piccolo")
    try:
        return install(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

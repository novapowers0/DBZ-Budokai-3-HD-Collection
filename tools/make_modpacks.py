#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_modpacks.py - Builds the two modpacks of a release (v1.4.0+).

  release_packs/DBZ3HD-<ver>-Personajes.zip
      mods/cut_* (source mods of the new characters) + a prebuilt mods/_roster built
      from exactly those, so they work right away (even without Python).
  release_packs/DBZ3HD-<ver>-Kit-Modding.zip
      The tools behind the launcher's modding tabs (new characters, importer,
      capsules, voices/yells, textures, model swap): mod center hd/ + awo_tools/ (only
      the modules they import), the XDK LZX tools, the community reference lists the
      capsule importer reads, docs and a one-click requirements installer. Also the
      DBZ3 HD Mod Kit window (mod center hd/modkit_gui.py, opened with DBZ3_ModKit.bat at
      the kit root) plus the standalone modder tools it exposes (KIT_EXTRA_TOOLS).

Usage:  python tools/make_modpacks.py [--version 1.4.1] [--solo kit|personajes]
        (v1.4.1: the characters pack stays the 1.4.0 one on Google Drive -> --solo kit)
"""
import argparse
import ast
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODS = os.path.join(ROOT, "out", "build", "win-amd64-release", "mods")
OUT = os.path.join(ROOT, "release_packs")
CHARACTERS = ["cut_janemba", "cut_android19", "cut_zarbon", "cut_dodoria", "cut_guldo", "cut_jeice",
              "cut_burter"]
KIT_ENTRIES = ["roster_build", "importar", "swap_b3", "texture_b3", "texture_dump_import", "texture_pack",
               "capsulas", "gritos", "voces", "iso", "model_render",
               "modkit_gui",      # DBZ3 HD Mod Kit: the no-console window over all of these
               "diagnostico",     # its Diagnostics page (reads a game log, explains it)
               "hexedit",         # its Hex page (expert byte editor that knows #AMB/#AMO)
               "log_en"]          # English log of the tools when the Mod Kit is in EN
# Standalone modder tools the Mod Kit window also exposes (advanced mode > Tools), with the
# local modules they import (kit_modules follows them).
KIT_EXTRA_TOOLS = ["name_banner", "portrait_hd", "extract_azt_afs", "awg_to_obj_b3", "awg0_export",
                   "awg_cara_export", "swap_matrix", "texture_upscale_b3", "psp_amo", "sb_amm", "csk_edit",
                   "acm_parse", "sbport", "sb_tablas", "sdbh_model", "sb_tecnicas", "sb_voces"]
KIT_DATA = ["catalog_b3.cat", "roster_db.json", "b1_capsulas.txt"]   # b1_capsulas: nombres de B1 (importador)
AWO_DATA = ["psp_ramp.npy"]      # rampa toon de PSP: la carga psp_amo.py (importar de Shin Budokai)
KIT_ROOT_FILES = ["LEEME_KIT.txt", "LEEME_KIT_EN.txt", "requirements.txt", "instalar_requisitos.bat", "DBZ3_ModKit.bat"]
KIT_FORMAT_DOCS = ["CAPSULAS_B3.md", "MAPA_ROSTER_HD.md", "ACM_FORMAT.md", "AMO_AWO.md", "BIN_LAYOUT.md",
                   "STAGES_FORMAT.md", "CAMARA_ACC.md", "SB_VS_B3_MOVESET.md", "FORMAS_Y_KI.md",
                   "TOON_Y_BRILLO_HD.md"]
XDK = os.path.join(ROOT, "mod center", "Xbox 360 Compression - Decompression tool from the XBOX Development Kit")
RES = os.path.join(ROOT, "modding resources")
RES_FILES = ["Budokai_3_Capsules_IDs.txt", "Dragon Ball Z Infinite World Capsule List.xlsx",
             "Budokai 1 and Budokai 2 Capsule Data", "Voice list for Infinite World.txt",
             "DBZ_B3_GH_Character_Bin_List.txt", "Character IDs (BUDOKAI 3).rtf",
             "Character IDs (INFINITE WORLD).rtf"]


def kit_modules():
    """Local modules reached from the entry scripts (imports inside functions too)."""
    dirs = [os.path.join(ROOT, "mod center hd"), os.path.join(ROOT, "awo_tools")]

    def find(m):
        for d in dirs:
            p = os.path.join(d, m + ".py")
            if os.path.exists(p):
                return p
        return None
    seen, todo = set(), [find(e) for e in KIT_ENTRIES + KIT_EXTRA_TOOLS]
    while todo:
        p = todo.pop()
        if not p or p in seen:
            continue
        seen.add(p)
        for n in ast.walk(ast.parse(open(p, encoding="utf-8").read())):
            names = []
            if isinstance(n, ast.Import):
                names = [a.name.split(".")[0] for a in n.names]
            elif isinstance(n, ast.ImportFrom) and n.module and n.level == 0:
                names = [n.module.split(".")[0]]
            todo += [find(m) for m in names]
    return sorted(seen)


def zip_dir(src, dst):
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dp, _, files in os.walk(src):
            for fn in files:
                full = os.path.join(dp, fn)
                z.write(full, os.path.relpath(full, src))


def characters_pack(ver, stage):
    mods = os.path.join(stage, "mods")
    os.makedirs(mods)
    for m in CHARACTERS:
        shutil.copytree(os.path.join(MODS, m), os.path.join(mods, m),
                        ignore=shutil.ignore_patterns(".disabled", "*.antes_de_importar", "respaldo"))
    # the generated mod for exactly these characters
    r = subprocess.run([sys.executable, os.path.join(ROOT, "mod center hd", "roster_build.py"), "construir",
                        "--mods", mods, "--force"], capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    print(r.stdout[-1500:], r.stderr[-1500:])
    if r.returncode != 0 or not os.path.isdir(os.path.join(mods, "_roster")):
        raise SystemExit("roster_build failed")
    shutil.copyfile(os.path.join(ROOT, "tools", "modpacks", "LEEME_PERSONAJES.txt"),
                    os.path.join(stage, "LEEME_PERSONAJES.txt"))
    out = os.path.join(OUT, "DBZ3HD-%s-Personajes.zip" % ver)
    zip_dir(stage, out)
    return out


def kit_pack(ver, stage):
    mch = os.path.join(stage, "mod center hd")
    awo = os.path.join(stage, "awo_tools")
    os.makedirs(os.path.join(mch, "tools"))
    os.makedirs(awo)
    for p in kit_modules():
        dst = mch if os.path.basename(os.path.dirname(p)) == "mod center hd" else awo
        shutil.copyfile(p, os.path.join(dst, os.path.basename(p)))
    studio = os.path.join(ROOT, "mod center hd", "studio")   # Studio de camaras (paquete)
    if os.path.isdir(studio):
        shutil.copytree(studio, os.path.join(mch, "studio"), ignore=shutil.ignore_patterns("__pycache__"))
    fonts = os.path.join(ROOT, "mod center hd", "fonts")      # fuente propia: nunca "cannot open resource"
    if os.path.isdir(fonts):
        shutil.copytree(fonts, os.path.join(mch, "fonts"))
    for f in KIT_DATA:
        shutil.copyfile(os.path.join(ROOT, "mod center hd", f), os.path.join(mch, f))
    for f in AWO_DATA:
        shutil.copyfile(os.path.join(ROOT, "awo_tools", f), os.path.join(awo, f))
    for f in os.listdir(XDK):
        shutil.copyfile(os.path.join(XDK, f), os.path.join(mch, "tools", f))
    res = os.path.join(stage, "modding resources")
    os.makedirs(res)
    for f in RES_FILES:
        src = os.path.join(RES, f)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(res, f))
        elif os.path.exists(src):
            shutil.copyfile(src, os.path.join(res, f))
    os.makedirs(os.path.join(stage, "ps2_games"))
    docs = os.path.join(stage, "docs")
    os.makedirs(docs)
    mod_docs = ("COMO_HACER_MODS.md", "PACKS_DE_TEXTURAS.md", "TEXTURAS_MOD.md", "MODEL_SWAP.md", "STUDIO_CAMARAS.md")
    for f in mod_docs + tuple(x.replace(".md", "_EN.md") for x in mod_docs):     # _EN = la del Kit en ingles
        src = os.path.join(ROOT, "docs", "02_mods", f)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(docs, f))
    formats = os.path.join(docs, "formatos")      # the Mod Kit's Help tab lists them
    os.makedirs(formats)
    for f in KIT_FORMAT_DOCS + [x.replace(".md", "_EN.md") for x in KIT_FORMAT_DOCS]:
        src = os.path.join(ROOT, "docs", "03_formatos", f)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(formats, f))
    for f in KIT_ROOT_FILES:
        shutil.copyfile(os.path.join(ROOT, "tools", "modpacks", f), os.path.join(stage, f))
    shutil.copyfile(os.path.join(ROOT, "tools", "modpacks", "LEEME_PS2_GAMES.txt"),
                    os.path.join(stage, "ps2_games", "LEEME.txt"))
    shutil.copyfile(os.path.join(ROOT, "tools", "modpacks", "LEEME_PS2_GAMES_EN.txt"),
                    os.path.join(stage, "ps2_games", "LEEME_EN.txt"))
    # Python portatil (python/ con numpy, Pillow, scipy y tkinter): el Kit, el launcher (texturas,
    # personajes) y DBZ3_ModKit.bat lo usan antes que el del sistema. Nadie instala nada.
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import make_python_bundle  # noqa: PLC0415
    make_python_bundle.build(stage)
    if not make_python_bundle.check(os.path.join(stage, "python")):
        raise SystemExit("python portatil: la comprobacion fallo")
    out = os.path.join(OUT, "DBZ3HD-%s-Kit-Modding.zip" % ver)
    zip_dir(stage, out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="1.4.2")
    ap.add_argument("--solo", choices=("kit", "personajes"),
                    help="build only one pack (the other one is reused from an earlier release)")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    stage = os.path.join(OUT, "_stage")
    if os.path.exists(stage):
        shutil.rmtree(stage)   # our own scratch folder from a previous run
    for name, fn in (("personajes", characters_pack), ("kit", kit_pack)):
        if a.solo and a.solo != name:
            continue
        out = fn(a.version, os.path.join(stage, name))
        print("%s: %s (%.1f MB)" % (name, out, os.path.getsize(out) / 1e6))


if __name__ == "__main__":
    main()

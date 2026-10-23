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

Usage:  python tools/make_modpacks.py [--version 1.4.1] [--solo kit|personajes] [--dry-run]
        (v1.4.1: the characters pack stays the 1.4.0 one on Google Drive -> --solo kit)
"""
import argparse
import ast
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODS = os.path.join(ROOT, "out", "build", "win-amd64-release", "mods")
OUT = os.path.join(ROOT, "release_packs")
CHARACTERS = ["cut_janemba", "cut_android19", "cut_zarbon", "cut_dodoria", "cut_guldo", "cut_jeice",
              "cut_burter"]
KIT_ENTRIES = ["t3", "inventario_copias",   # studies 1 F08/F09: Tenkaichi 3 e inventario de las copias del jugador
               "vista_heroe",   # Modo Heroe: vista previa 3D y traje de combate (cac/vista_heroe.py + awo_tools/injertar_pelo.py)
               "roster_build", "importar", "swap_b3", "texture_b3", "texture_dump_import", "texture_pack",
               "capsulas", "gritos", "voces", "iso", "model_render",
               "modkit_gui",      # DBZ3 HD Mod Kit: the no-console window over all of these
               "diagnostico",     # its Diagnostics page (reads a game log, explains it)
               "hexedit",         # its Hex page (expert byte editor that knows #AMB/#AMO)
               "log_en",          # English log of the tools when the Mod Kit is in EN
               "campana", "editor_historias",   # Historias page (montar) and its story editor
               "modos_reto",      # challenge modes card (run as a subprocess: no import reaches it)
               "retos_predefinidos",   # "Retos predefinidos" row (HUB challenges, tower of the week)
               "forma_unica",   # respaldo de la capsula "Todas las transformaciones" (hoy la crea el juego)
               "campos_iw"]     # escenarios de los juegos de PS2 del jugador (campos de IW, B1/B2) al pulsar
                                # Jugar (ModPipeline::BuildStages); trae escenarios_convertir. Solo el conversor:
                                # los escenarios se generan en el equipo del jugador desde sus ISOs
# Standalone modder tools the Mod Kit window also exposes (advanced mode > Tools), with the
# local modules they import (kit_modules follows them).
KIT_EXTRA_TOOLS = ["name_banner", "portrait_hd", "extract_azt_afs", "awg_to_obj_b3", "awg0_export",
                   "awg_cara_export", "swap_matrix", "texture_upscale_b3", "psp_amo", "sb_amm", "csk_edit",
                   "acm_parse", "sbport", "sb_tablas", "sdbh_model", "sb_tecnicas", "sb_voces",
                   "cri",           # voces de SDBH (importar.py lo importa dentro de una funcion: el rastreo no lo ve)
                   "comprobar_assets"]   # studies 6 frente 01: validador de bins (ventana 44 B, rampa, pesos PS2);
                                         # opcional: si no esta en mod center hd/, `find` devuelve None y se ignora
KIT_DATA = ["catalog_b3.cat", "cac/piezas_b3_base.json", "roster_db.json", "b1_capsulas.txt",   # b1_capsulas: nombres de B1 (importador)
            # sin estos el Kit fallaba en casa de los jugadores (log de un jugador, 2026-10-12): presets del
            # importador, nombres de los modelos de IW, codigos de Dragon Universe, etiquetas de texturas
            # y la lista de herramientas de la ventana
            "presets_importar.toml", "iw_modelos.json", "du_codigos.json", "texturas_etiquetas.json",
            "tools_manifest.json",
            "ports_oficiales.toml",   # recetas de la tarjeta "4. Ports oficiales" (importar.py oficial)
            "ports_vision.toml"]      # tarjeta "5. Personajes de la vision" (importar.py oficial --recetas)
# editor de historias y modos de reto: solo los .toml (globs valen). NUNCA las imagenes de campanas/imagenes
# ni campanas/retratos (arte de terceros): campana.py usa el panel / retrato del juego si faltan.
KIT_CAMPAIGNS = ["plantilla_historia.toml", "ejemplo_freezer.toml", "historia_freezer.toml", "retos/*.toml"]
ART_EXT = (".png", ".jpg", ".jpeg", ".webp", ".avif", ".gif", ".bmp", ".tga", ".dds", ".psd")
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


# Zip del juego (tools/make_release.ps1 -> --game-stage): lo que el launcher y el juego lanzan SIN Kit instalado
# (escenarios de PS2 al pulsar Jugar, vista previa/traje del Modo Heroe, personajes de mods). Mismas rutas que
# el Kit: si el Kit se descomprime encima nada cambia.
GAME_ENTRIES = ["campos_iw", "escenarios_convertir", "iso", "importar", "vista_heroe", "roster_build",
                "swap_b3", "texture_b3", "texture_pack", "texture_dump_import", "capsulas", "colores", "gritos",
                "hornada_support", "voces", "model_render", "heroe_sagas_a_du", "nombre_levelup", "campana",
                "retrato_anime", "tecnicas_tipo"]


def kit_modules(entries=None):
    """Local modules reached from the entry scripts (imports inside functions too)."""
    dirs = [os.path.join(ROOT, "mod center hd"), os.path.join(ROOT, "mod center hd", "cac"), os.path.join(ROOT, "awo_tools")]

    def find(m):
        for d in dirs:
            p = os.path.join(d, m + ".py")
            if os.path.exists(p):
                return p
        return None
    seen, todo = set(), [find(e) for e in (entries or KIT_ENTRIES + KIT_EXTRA_TOOLS)]
    while todo:
        p = todo.pop()
        if not p or p in seen:
            continue
        seen.add(p)
        for n in ast.walk(ast.parse(open(p, encoding="utf-8-sig").read(), filename=p)):
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


def no_third_party_art(mch):
    """Fails the build if any image reached the staged campanas/ (third-party art never ships)."""
    bad = [os.path.relpath(os.path.join(dp, f), mch) for dp, _, fs in os.walk(os.path.join(mch, "campanas"))
           for f in fs if f.lower().endswith(ART_EXT)]
    if bad:
        raise SystemExit("third-party art in the Kit: " + ", ".join(bad))


def kit_pack(ver, stage, dry=False):
    mch = os.path.join(stage, "mod center hd")
    awo = os.path.join(stage, "awo_tools")
    os.makedirs(os.path.join(mch, "tools"))
    os.makedirs(awo)
    for p in kit_modules():
        dst = mch if os.path.basename(os.path.dirname(p)) == "mod center hd" else awo
        if os.path.basename(os.path.dirname(p)) == "cac":     # cac/vista_heroe.py va en mod center hd/cac/
            dst = os.path.join(mch, "cac")
            os.makedirs(dst, exist_ok=True)
        shutil.copyfile(p, os.path.join(dst, os.path.basename(p)))
    studio = os.path.join(ROOT, "mod center hd", "studio")   # Studio de camaras (paquete)
    if os.path.isdir(studio):
        shutil.copytree(studio, os.path.join(mch, "studio"), ignore=shutil.ignore_patterns("__pycache__"))
    fonts = os.path.join(ROOT, "mod center hd", "fonts")      # fuente propia: nunca "cannot open resource"
    if os.path.isdir(fonts):
        shutil.copytree(fonts, os.path.join(mch, "fonts"))
    for f in KIT_DATA:
        shutil.copyfile(os.path.join(ROOT, "mod center hd", f), os.path.join(mch, f))
    camp = os.path.join(ROOT, "mod center hd", "campanas")
    for pat in KIT_CAMPAIGNS:
        found = glob.glob(os.path.join(camp, pat))
        if not found:
            raise SystemExit("missing campaign file: campanas/" + pat)
        for src in found:
            dst = os.path.join(mch, "campanas", os.path.relpath(src, camp))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
    no_third_party_art(mch)
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
    if dry:     # --dry-run: what would ship, without the portable Python and without the zip
        for dp, _, fs in os.walk(mch):
            for f in fs:
                print("  " + os.path.relpath(os.path.join(dp, f), stage).replace(os.sep, "/"))
        return "(dry run: %d files staged, no zip)" % sum(len(fs) for _, _, fs in os.walk(stage))
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


def game_stage(stage):
    """Anade al stage del zip del juego los scripts (con sus imports), sus datos y el Python portatil minimo."""
    mch = os.path.join(stage, "mod center hd")
    awo = os.path.join(stage, "awo_tools")
    os.makedirs(os.path.join(mch, "cac"), exist_ok=True)
    os.makedirs(awo, exist_ok=True)
    n = 0
    for p in kit_modules(GAME_ENTRIES):
        d = os.path.basename(os.path.dirname(p))
        dst = os.path.join(mch, "cac") if d == "cac" else mch if d == "mod center hd" else awo
        shutil.copyfile(p, os.path.join(dst, os.path.basename(p)))
        n += 1
    for sub in ("studio", "fonts"):
        src = os.path.join(ROOT, "mod center hd", sub)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(mch, sub), dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__"))
    for f in KIT_DATA:
        shutil.copyfile(os.path.join(ROOT, "mod center hd", f), os.path.join(mch, f))
    for f in AWO_DATA:
        shutil.copyfile(os.path.join(ROOT, "awo_tools", f), os.path.join(awo, f))
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import make_python_bundle  # noqa: PLC0415
    py = os.path.join(stage, "python")
    if os.path.isdir(py):
        raise SystemExit("ya existe %s" % py)
    make_python_bundle.build(stage, slim=True)
    if not make_python_bundle.check(py, slim=True):
        raise SystemExit("python portatil: la comprobacion fallo")
    print("zip del juego: %d scripts + python portatil (numpy+Pillow)" % n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-stage", metavar="DIR",
                    help="anade al stage de make_release.ps1 los scripts de PS2/Modo Heroe y el Python portatil; no hace mas")
    ap.add_argument("--version", default="1.4.2")
    ap.add_argument("--solo", choices=("kit", "personajes"),
                    help="build only one pack (the other one is reused from an earlier release)")
    ap.add_argument("--dry-run", action="store_true",
                    help="stage the Kit in a temp folder and list its mod center hd/ files (no python bundle, no zip, "
                         "release_packs/ untouched); fails if third-party art would ship")
    a = ap.parse_args()
    if a.game_stage:
        return game_stage(a.game_stage)
    if a.dry_run:
        tmp = tempfile.mkdtemp(prefix="kit_dry_")
        try:
            print("kit:", kit_pack(a.version, os.path.join(tmp, "kit"), dry=True))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)   # our own temp staging folder
        return
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

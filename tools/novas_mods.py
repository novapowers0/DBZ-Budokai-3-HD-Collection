#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""novas_mods.py - Nova's Mods: el repo privado de mods que descarga el launcher.

  python tools/novas_mods.py clave
      Pide la clave de solo lectura del repo (fine-grained, Contents: read) y escribe
      novas_mods.key en la raiz del proyecto (make_release la copia junto al exe). La clave
      va en hex con XOR 0x5A, como la lee src/launcher/novas_mods.cpp.

  python tools/novas_mods.py publicar [--mods DIR] [--solo id ...]
      Empaqueta los mods de tools/novas_mods.toml (carpeta mods/ del juego), escribe la
      version en su manifest.txt, genera catalogo.toml (con `size` y `sha256` del zip entero;
      el launcher comprueba el sha256 tras bajar y no instala si no coincide) y lo sube al repo con git.

      Mods grandes (`release = "<tag>"` en el toml, p. ej. el pack de texturas HD de 2,9 GB):
      git no admite ficheros de mas de 100 MB, asi que el zip va FUERA del repo, partido en
      trozos de 1900 MiB (GitHub: < 2 GiB por asset) y subido como assets de esa release del
      mismo repo privado. El catalogo lleva `partes = [ids]` y el launcher los baja y une.
      `autor` y `enlaces = ["https://..."]` salen en la ficha con botones clicables.

      Mods que necesitan un exe nuevo (`exe_nuevo = true`): su zip va a mods/nuevos/ y el catalogo lo da como
      `archivo` en vez de `zip`, asi los launchers de antes (que solo leen `zip`) no los ven. Para tener las dos
      versiones de un mod, dos entradas con el mismo id: la de antes con `congelado = true` (no se vuelve a
      empaquetar: se queda su zip publicado) y despues la nueva con `exe_nuevo = true` (el launcher nuevo se queda
      con la ultima entrada de cada id).
"""
import argparse
import getpass
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tomllib
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = "novapowers0/DBZ3HD-Novas-Mods"
LIST = os.path.join(ROOT, "tools", "novas_mods.toml")
KEY = os.path.join(ROOT, "novas_mods.key")
WORK = os.path.join(ROOT, "novas_mods_repo")
BIG = os.path.join(ROOT, "novas_mods_grandes")    # zips de las releases (fuera del repo git)
MODS = os.path.join(ROOT, "out", "build", "win-amd64-release", "mods")
PART = 1900 * 1024 * 1024                           # GitHub: < 2 GiB por asset de release
STORED = (".png", ".dds", ".zip", ".jpg", ".ogg")    # ya comprimidos: deflate solo gasta tiempo


def obfuscate(token):
    return "".join("%02x" % (ord(c) ^ 0x5A) for c in token)


def reveal(hexs):
    return "".join(chr(int(hexs[i:i + 2], 16) ^ 0x5A) for i in range(0, len(hexs) - 1, 2))


def cmd_clave(_a):
    # primero el portapapeles (copiada de donde sea), si no se pide a mano
    t = open(KEY).read().strip() if os.path.isfile(KEY) else ""     # pegada tal cual en el fichero
    if not t.startswith("github_pat_"):
        t = subprocess.run(["powershell", "-NoProfile", "-Command", "Get-Clipboard"], capture_output=True,
                           text=True).stdout.strip()
    if not t.startswith("github_pat_"):
        t = getpass.getpass("Clave de solo lectura del repo de mods (no se ve al escribir): ").strip()
    if not t.startswith("github_pat_"):
        sys.exit("Eso no parece una clave fine-grained (github_pat_...).")
    open(KEY, "w").write(obfuscate(t))
    print("Clave guardada en", KEY)


def manifest_with_version(path, version, name, author=None):
    lines = open(path, encoding="utf-8").read().splitlines() if os.path.isfile(path) else ["name=" + name]
    drop = ("version=", "author=") if author else ("version=",)
    lines = [ln for ln in lines if not ln.startswith(drop)] + ["version=" + version]
    if author:
        lines.append("author=" + author)       # credito tambien en la pestana Mods
    return "\n".join(lines) + "\n"


def pack(src, mod_id, version, name, out_zip, author=None):
    """Zip con la carpeta <id>/ (InstallModFromZip la desenvuelve); el manifest lleva la version."""
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for d, ds, fs in os.walk(src):
            ds[:] = [x for x in ds if x not in ("respaldo", "_respaldo")]   # copias de trabajo del mod, no van
            for f in sorted(fs):
                if f.startswith(".modelo_hd."):   # cache de la vista previa (ui/_vista): se rehace sola
                    continue
                p = os.path.join(d, f)
                rel = os.path.relpath(p, src).replace("\\", "/")
                if rel == "manifest.txt" or rel == ".disabled" or f.endswith((".bak", ".log")):
                    continue
                kind = zipfile.ZIP_STORED if f.lower().endswith(STORED) else zipfile.ZIP_DEFLATED
                z.write(p, mod_id + "/" + rel, compress_type=kind)
        z.writestr(mod_id + "/manifest.txt", manifest_with_version(os.path.join(src, "manifest.txt"), version, name, author))


def sha256_file(path):
    """SHA-256 (hex) del zip entero: el launcher lo comprueba tras la descarga (campo opcional `sha256`)."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(8 << 20), b""):
            h.update(b)
    return h.hexdigest()


def toml_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def split(path, size=PART):
    """Trocea `path` en <path>.001, .002... (se unen en orden, byte a byte). Devuelve las rutas."""
    parts = []
    with open(path, "rb") as src:
        while True:
            b = src.read(min(size, 64 << 20))
            if not b:
                return parts
            name = "%s.%03d" % (path, len(parts) + 1)
            with open(name, "wb") as out:
                left = size
                while b:
                    out.write(b)
                    left -= len(b)
                    b = src.read(min(left, 64 << 20)) if left else b""
            parts.append(name)


def upload_release(tag, title, parts):
    """Sube los trozos como assets de la release `tag` del repo privado y devuelve sus ids en orden."""
    gh = ["gh", "release"]
    if subprocess.run(gh + ["view", tag, "-R", REPO], capture_output=True).returncode:
        subprocess.run(gh + ["create", tag, "-R", REPO, "--title", title, "--notes",
                             "Nova's Mods: zip partido, lo descarga y une el launcher."], check=True)
    subprocess.run(gh + ["upload", tag, "-R", REPO, "--clobber"] + parts, check=True)
    rel = json.loads(subprocess.run(["gh", "api", "repos/%s/releases/tags/%s" % (REPO, tag)],
                                    check=True, capture_output=True, text=True).stdout)
    ids = {a["name"]: a["id"] for a in rel["assets"]}
    return [ids[os.path.basename(p)] for p in parts]


def cmd_publicar(a):
    cfg = tomllib.load(open(LIST, "rb"))
    if not os.path.isdir(os.path.join(WORK, ".git")):
        subprocess.run(["gh", "repo", "clone", REPO, WORK], check=True)
    os.makedirs(os.path.join(WORK, "mods"), exist_ok=True)
    out = ["# Nova's Mods: lo lee el launcher (src/launcher/novas_mods.cpp). Generado por tools/novas_mods.py.", ""]
    for m in cfg["mod"]:
        mid = m["id"]
        src = os.path.join(a.mods, mid)
        if not os.path.isdir(src) and os.path.isdir(src + ".disabled"):
            src += ".disabled"
        big = "release" in m
        nuevo = m.get("exe_nuevo", False)
        zrel = ("mods/nuevos/%s.zip" if nuevo else "mods/%s.zip") % mid
        zpath = os.path.join(BIG if big else WORK, zrel)
        os.makedirs(os.path.dirname(zpath), exist_ok=True)
        ids_file = zpath + ".ids.json"    # ids de los assets subidos (para regenerar el catalogo)
        if m.get("congelado"):
            if not os.path.isfile(zpath):
                sys.exit("El mod congelado %s no tiene su zip publicado en %s" % (mid, zpath))
        elif a.solo and mid not in a.solo and not os.path.isfile(zpath) and not big:
            print("sin publicar todavia (fuera del catalogo): %s" % mid)   # p. ej. uno preparado para mas tarde
            continue
        elif not a.solo or mid in a.solo:
            if not os.path.isdir(src):
                sys.exit("No encuentro el mod %s en %s" % (mid, a.mods))
            pack(src, mid, m["version"], m["nombre_es"], zpath, m.get("autor"))
            print("empaquetado %s v%s (%.1f MB)" % (mid, m["version"], os.path.getsize(zpath) / 1048576))
            if big:
                parts = split(zpath)
                ids = upload_release(m["release"], m["nombre_es"], parts)
                json.dump(ids, open(ids_file, "w"))
                print("subido como release %s: %d trozos" % (m["release"], len(ids)))
        out.append("[[mod]]")
        for k in ("id", "version", "tipo"):
            out.append("%s = %s" % (k, toml_str(m[k])))
        if big:
            out.append("partes = [%s]" % ", ".join(str(i) for i in json.load(open(ids_file))))
        else:
            out.append(("archivo = " if nuevo else "zip = ") + toml_str(zrel))
        out.append("size = %d" % os.path.getsize(zpath))
        out.append('sha256 = "%s"' % sha256_file(zpath))    # el zip entero (en los partidos, ya unido)
        if "autor" in m:
            out.append("autor = " + toml_str(m["autor"]))
        if "excluye" in m:    # otra version del mismo mod (pack HD V1/V2): el launcher la aparta al instalar esta
            out.append("excluye = [%s]" % ", ".join(toml_str(x) for x in m["excluye"]))
        if "enlaces" in m:
            out.append("enlaces = [%s]" % ", ".join(toml_str(u) for u in m["enlaces"] if u.startswith("https://")))
        for k, v in m.items():
            if k.startswith(("nombre_", "descripcion_")):
                out.append("%s = %s" % (k, toml_str(v)))
        out.append("")
    open(os.path.join(WORK, "catalogo.toml"), "w", encoding="utf-8", newline="\n").write("\n".join(out))
    tomllib.load(open(os.path.join(WORK, "catalogo.toml"), "rb"))     # se lee bien
    git = ["git", "-C", WORK]
    subprocess.run(git + ["add", "-A"], check=True)
    if subprocess.run(git + ["diff", "--cached", "--quiet"]).returncode:
        # H8 24/10: confirmacion antes de subir al repo (--si la omite)
        if not a.si and input("¿Publicar en Nova's Mods? s/N ").strip().lower() != "s":
            print("cancelado: no se ha subido nada (el catalogo queda en %s)" % WORK)
            return
        subprocess.run(git + ["commit", "-q", "-m", a.mensaje], check=True)
        subprocess.run(git + ["push", "-q", "origin", "HEAD"], check=True)
        print("subido a", REPO)
    else:
        print("sin cambios")


def prueba():
    import tempfile
    assert reveal(obfuscate("github_pat_X1")) == "github_pat_X1"
    assert "version=2" in manifest_with_version("/no/existe", "2", "A")
    assert "author=PV" in manifest_with_version("/no/existe", "2", "A", "PV")
    with tempfile.TemporaryDirectory() as d:     # trocear y unir devuelve el mismo fichero
        f = os.path.join(d, "x.zip")
        data = os.urandom(2500)
        open(f, "wb").write(data)
        assert sha256_file(f) == hashlib.sha256(data).hexdigest()
        parts = split(f, 1000)
        assert [os.path.getsize(p) for p in parts] == [1000, 1000, 500]
        assert b"".join(open(p, "rb").read() for p in parts) == data
        parts = split(f, 1250)                     # multiplo exacto: sin trozo vacio
        assert [os.path.getsize(p) for p in parts] == [1250, 1250]
    print("novas_mods OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("clave")
    p = sub.add_parser("publicar")
    p.add_argument("--mods", default=MODS)
    p.add_argument("--solo", nargs="*")
    p.add_argument("--mensaje", default="Actualiza Nova's Mods")
    p.add_argument("--si", action="store_true", help="publicar sin preguntar")
    sub.add_parser("prueba")
    a = ap.parse_args()
    {"clave": cmd_clave, "publicar": cmd_publicar, "prueba": lambda _a: prueba()}[a.cmd](a)


if __name__ == "__main__":
    main()

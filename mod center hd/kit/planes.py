# -*- coding: utf-8 -*-
"""Planes de los asistentes (sin interfaz): respuestas -> lista de pasos con los motores de siempre.
Un plan = [(titulo, orden | funcion)]. `ejecutar` lo corre y deja un lote deshacible
(nuevo / movido en modkit_backups/indice.tsv; lo restaura modkit_gui.deshacer_ultimo).
Nada se borra: lo que sobra se mueve a modkit_backups. `g` = modulo modkit_gui (como en portada.py)."""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GENERADOS = ("_roster", "roster_avisos.txt", "roster_ids.toml")      # salida de roster_build construir
EJECUTABLES = (".dll", ".exe", ".py", ".bat", ".cmd", ".ps1", ".vbs", ".js", ".msi", ".scr", ".lnk")
MAX_ZIP = 6 << 30                                                  # 6 GB descomprimido


class Plan:
    def __init__(self, nombre, pasos, nuevos=(), construye=False):
        self.nombre, self.pasos, self.nuevos, self.construye = nombre, list(pasos), list(nuevos), construye
        self.zip = None


class Lote:
    """Lo que un asistente cambia en mods/, anotado para «Deshacer»."""

    def __init__(self, env, anotar):
        self.env, self.anotar = env, anotar
        self.stamp = time.strftime("%Y%m%d-%H%M%S")
        self.dir = os.path.join(env.backups, self.stamp + "-asistente")
        self.antes, self.extra = set(), []

    def abrir(self, nuevos=()):
        os.makedirs(self.env.mods, exist_ok=True)
        self.antes, self.extra = set(os.listdir(self.env.mods)), list(nuevos)

    def mover(self, path):
        """Aparta un fichero o carpeta que se va a sustituir (a modkit_backups; se restaura al deshacer)."""
        if not os.path.exists(path):
            return
        os.makedirs(self.dir, exist_ok=True)
        dst = os.path.join(self.dir, "%d_%s" % (len(os.listdir(self.dir)), os.path.basename(path)))
        os.replace(path, dst)
        self.anotar(self.env, self.stamp, "movido", path, os.path.relpath(dst, self.env.backups))

    def apartar_generados(self):
        for n in GENERADOS:
            self.mover(os.path.join(self.env.mods, n))

    def cerrar(self):
        hoy = set(os.listdir(self.env.mods)) if os.path.isdir(self.env.mods) else set()
        for n in sorted((hoy - self.antes) | {e for e in self.extra if e in hoy}):
            self.anotar(self.env, self.stamp, "nuevo", os.path.join(self.env.mods, n), "")


def avisos(texto):
    """Lineas de aviso/error del registro (las mismas reglas que auditar_importar.py)."""
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    import auditar_importar  # noqa: PLC0415
    return auditar_importar.problemas(texto)


# ------------------------------------------------------------------------------------------ planes
def plan_personaje(g, env, r):
    """r: fuente, clave, nombre, preset ('' = el de la fuente), donante (-1 = el del catalogo), mod."""
    mod = r.get("mod") or g.unique_mod_name(env.mods, g.mod_slug(r["fuente"], r["nombre"]))
    extra = ["--preset", r["preset"]] if r.get("preset") else []
    return Plan("personaje", [
        (g.T("Importar personaje", "Import character"),
         g.cmd_import_character(env, r["fuente"], r["clave"], mod, r["nombre"], r.get("donante", -1), extra)),
        (g.T("Montar el juego", "Build the game"), g.cmd_roster_build(env, True))], [mod], True)


def plan_aspecto(g, env, r):
    """r: id (personaje del juego), nombre, aura / ki (nombre de color o None), bin + png_dir (opcional)."""
    pasos, nuevos = [], []
    base = g.slug(r["nombre"])
    if r.get("aura") or r.get("ki"):
        mod = r.get("mod_colores") or g.unique_mod_name(env.mods, "colores_" + base)
        cmd = [env.python, os.path.join(HERE, "kit", "correr.py"), env.us or "-", "colores.py", "nativo",
               str(r["id"]), "--mod", mod, "--mods", env.mods]
        for k, f in (("aura", "--aura"), ("ki", "--ki")):
            if r.get(k):
                cmd += [f, r[k]]
        pasos.append((g.T("Colores de aura y ki", "Aura and ki colours"), cmd))
        nuevos.append(mod)
    if r.get("bin") is not None and r.get("png_dir"):
        mod = r.get("mod_texturas") or g.unique_mod_name(env.mods, "tex_" + base)
        carpeta = os.path.join(env.mods, mod, "textures")
        if not os.path.isfile(os.path.join(r["png_dir"], "textures_meta.json")):
            pasos.append((g.T("Preparar texturas", "Prepare textures"), g.cmd_tex_extract(env, r["bin"], mod)))
            pasos.append((g.T("Poner tus imágenes", "Use your images"), lambda: _copiar_png(r["png_dir"], carpeta)))
            pasos.append((g.T("Montar texturas", "Build textures"), g.cmd_tex_build(env, mod)))
        else:                                       # carpeta ya extraida por el asistente: se monta tal cual
            pasos.append((g.T("Montar texturas", "Build textures"), g.cmd_tex_build(env, mod, -1, r["png_dir"])))
        nuevos.append(mod)
    return Plan("aspecto", pasos, nuevos, False)


def _copiar_png(origen, destino):
    n = 0
    for f in os.listdir(origen):
        if f.lower().endswith(".png"):
            shutil.copy2(os.path.join(origen, f), os.path.join(destino, f))
            n += 1
    if not n:
        raise RuntimeError("No hay ningún PNG en %s" % origen)
    return "%d imágenes copiadas" % n


def plan_traje(g, env, r):
    """r: personaje (ID del juego), nombre, y uno de: port (mod ya importado) | modelo (.amb/.bin)."""
    mod = r.get("mod") or g.unique_mod_name(env.mods, g.slug(r["nombre"], "traje_"))
    if r.get("port"):
        paso = (g.T("Crear el traje", "Create the costume"), g.cmd_importer(env, [
            "traje", r["port"], "--personaje", r["personaje"], "--nombre", r["nombre"], "--salida", mod,
            "--mods", env.mods] + env.us_args()))
    else:
        paso = (g.T("Crear el traje", "Create the costume"), lambda: _traje_de_modelo(env, mod, r))
    return Plan("traje", [paso, (g.T("Montar el juego", "Build the game"), g.cmd_roster_build(env, True))],
                [mod], True)


def _traje_de_modelo(env, mod, r):
    d = os.path.join(env.mods, mod)
    os.makedirs(os.path.join(d, "modelos"))
    fn = os.path.basename(r["modelo"])
    shutil.copy2(r["modelo"], os.path.join(d, "modelos", fn))
    nombre = r["nombre"].replace('"', "").replace("\\", "")
    with open(os.path.join(d, "manifest.txt"), "w", encoding="utf-8") as fh:
        fh.write("name=%s\ndescription=Traje creado con el asistente del Mod Kit\nauthor=\nversion=1.0\ntype=traje\n"
                 % nombre)
    with open(os.path.join(d, "traje.toml"), "w", encoding="utf-8") as fh:
        fh.write('[traje]\npersonaje = %d\nnombre = "%s"\nmodelos = ["modelos/%s"]\n' % (r["personaje"], nombre, fn))
    return "traje.toml creado"


def plan_musica(g, env, r):
    """r: archivo (audio del jugador), nombre. Una canción = un mod nuevo `musica_<nombre>` (Deshacer lo aparta)."""
    from kit import musica  # noqa: PLC0415
    mod = r.get("mod") or g.unique_mod_name(env.mods, g.slug(r["nombre"], "musica_"))
    return Plan("musica", [(g.T("Convertir la canción", "Convert the song"),
                            lambda: musica.crear(os.path.join(env.mods, mod), r["archivo"], r["nombre"]))], [mod], False)


# ------------------------------------------------------------------------------------------ .zip
def inspeccionar_zip(path):
    """-> dict(mods={nombre: [(miembro, ruta dentro de mods/)]}, ejecutables=[...], error='', bytes=N).
    Sin extraer nada. Acepta zips con la carpeta del mod, con 'mods/<mod>/...' o con los ficheros sueltos
    (entonces la carpeta se llama como el zip)."""
    out = {"mods": {}, "ejecutables": [], "error": "", "bytes": 0}
    try:
        z = zipfile.ZipFile(path)
    except (zipfile.BadZipFile, OSError):
        out["error"] = "no_zip"
        return out
    with z:
        miembros = [i for i in z.infolist() if not i.is_dir() and not i.filename.startswith("__MACOSX/")]
        for i in miembros:
            n = i.filename.replace("\\", "/")
            if n.startswith("/") or re.match(r"^[A-Za-z]:", n) or ".." in n.split("/"):
                out["error"] = "ruta_peligrosa"
                return out
        out["bytes"] = sum(i.file_size for i in miembros)
        if out["bytes"] > MAX_ZIP:
            out["error"] = "enorme"
            return out
        nombres = [i.filename.replace("\\", "/") for i in miembros]
        con_mods = bool(nombres) and all(n.split("/")[0].lower() == "mods" and n.count("/") >= 1 for n in nombres)
        stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", os.path.splitext(os.path.basename(path))[0]).strip("_") or "mod"
        for i, n in zip(miembros, nombres):
            rel = n.split("/", 1)[1] if con_mods else n
            if "/" not in rel:
                rel = stem + "/" + rel                    # fichero suelto -> carpeta con el nombre del zip
            out["mods"].setdefault(rel.split("/")[0], []).append((i.filename, rel))
            if n.lower().endswith(EJECUTABLES):
                out["ejecutables"].append(n)
    if not out["mods"]:
        out["error"] = "vacio"
    return out


def instalar_zip(env, lote, path, mods_elegidos=None, con_ejecutables=False):
    """Extrae los mods del zip en env.mods. Si ya hay uno con ese nombre se aparta (lote.mover): nada se pisa.
    Los ejecutables (.dll, .py...) solo se extraen con con_ejecutables. Devuelve los nombres instalados."""
    info = inspeccionar_zip(path)
    if info["error"]:
        raise RuntimeError("zip: " + info["error"])
    raiz = os.path.normpath(env.mods)
    hechos = []
    with zipfile.ZipFile(path) as z:
        for mod, miembros in info["mods"].items():
            if mods_elegidos is not None and mod not in mods_elegidos:
                continue
            for suf in ("", ".disabled"):
                lote.mover(os.path.join(env.mods, mod + suf))
            for miembro, rel in miembros:
                if rel.lower().endswith(EJECUTABLES) and not con_ejecutables:
                    continue
                dst = os.path.normpath(os.path.join(env.mods, rel))
                if not dst.startswith(raiz + os.sep):
                    raise RuntimeError("ruta fuera de mods: " + miembro)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with z.open(miembro) as src, open(dst, "wb") as fh:
                    shutil.copyfileobj(src, fh)
            hechos.append(mod)
    return hechos


def plan_zip(g, env, r):
    """r: zip, mods (lista o None = todos), ejecutables (bool)."""
    info = inspeccionar_zip(r["zip"])
    elegidos = r.get("mods") or list(info["mods"])
    construye = any(rel.endswith(("/personaje.toml", "/traje.toml")) and rel.count("/") == 1
                    for m in elegidos for _, rel in info["mods"].get(m, []))
    p = Plan("zip", [(g.T("Instalar el .zip", "Install the .zip"), None)], elegidos, construye)
    p.zip = (r["zip"], elegidos, bool(r.get("ejecutables")))
    if construye:
        p.pasos.append((g.T("Montar el juego", "Build the game"), g.cmd_roster_build(env, True)))
    return p


# ------------------------------------------------------------------------------------------ ejecutar
def ejecutar(g, env, plan, log=print, parar=lambda: False, proc_hook=lambda p: None):
    """Corre el plan paso a paso. Devuelve (ok, texto_completo, lote). Siempre cierra el lote."""
    lote = Lote(env, g.anotar_indice)
    lote.abrir(plan.nuevos)
    if plan.construye:
        lote.apartar_generados()
    ok, todo = True, []
    ambiente = dict(os.environ, PYTHONIOENCODING="utf-8", DBZ3_LANG=g.LANG)
    try:
        for titulo, paso in plan.pasos:
            if parar():
                ok = False
                todo.append("cancelado")
                break
            log("== %s" % titulo)
            if paso is None:
                z, elegidos, ejecs = plan.zip
                try:
                    txt = "instalados: " + ", ".join(instalar_zip(env, lote, z, elegidos, ejecs))
                except Exception as ex:  # noqa: BLE001
                    ok, txt = False, "ERROR: %s" % ex
                log(txt)
            elif callable(paso):
                try:
                    txt = str(paso() or "")
                except Exception as ex:  # noqa: BLE001
                    ok, txt = False, "ERROR: %s: %s" % (type(ex).__name__, ex)
                log(txt)
            else:
                p = subprocess.Popen([str(c) for c in paso], cwd=env.cwd(), stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, env=ambiente, text=True, encoding="utf-8",
                                     errors="replace")
                proc_hook(p)
                lineas = []
                for ln in p.stdout:
                    lineas.append(ln)
                    log(ln.rstrip())
                p.wait()
                txt = "".join(lineas)
                if p.returncode != 0:
                    ok, txt = False, txt + "\nERROR: código %s" % p.returncode
            todo.append(txt)
            if not ok:
                break
    finally:
        lote.cerrar()
    return ok, "\n".join(todo), lote

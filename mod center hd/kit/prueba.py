# -*- coding: utf-8 -*-
"""Pruebas de los asistentes. `rapido` (dentro de modkit_gui.py --selftest): construye los cuatro asistentes,
los recorre por programa, mira el resumen y las ordenes que montarian, y prueba instalar .zip + deshacer en una
carpeta temporal. `real` (acepcion completa, sin ventanas): crea el mod, monta, mira avisos y deshace sobre una
COPIA de data_cmn.afs:

  python modkit_gui.py --selftest-asistentes D:\\tmp\\kit2\\us D:\\tmp\\kit2\\run1
"""
from __future__ import annotations

import hashlib
import os
import shutil
import struct
import sys
import tempfile
import types
import zipfile

from kit import asistente, musica, planes, probar


def _snap(carpeta):
    """{ruta relativa: sha1} de todo lo que hay en la carpeta."""
    out = {}
    for dp, _d, fs in os.walk(carpeta):
        for f in fs:
            p = os.path.join(dp, f)
            h = hashlib.sha1()
            with open(p, "rb") as fh:
                for blk in iter(lambda: fh.read(1 << 20), b""):
                    h.update(blk)
            out[os.path.relpath(p, carpeta)] = h.hexdigest()
    return out


def _env_falso(tmp):
    mods, back = os.path.join(tmp, "mods"), os.path.join(tmp, "modkit_backups")
    os.makedirs(mods, exist_ok=True)
    return types.SimpleNamespace(mods=mods, backups=back, cwd=lambda: tmp, python=sys.executable, us="", game="",
                                 us_args=lambda: [], afs_args=lambda: [])


def _zip(destino, ficheros):
    with zipfile.ZipFile(destino, "w") as z:
        for n, txt in ficheros.items():
            z.writestr(n, txt)


# ------------------------------------------------------------------------------------------ rapido
def rapido(app, g, say):
    ok = True

    def check(cond, msg):
        nonlocal ok
        if not cond:
            ok = False
        say(("OK" if cond else "FAIL") + ": " + msg)

    n0 = len(app.selftest_cmds)
    # 1) Personaje
    w = asistente.AsistentePersonaje(app, g)
    w.v["fuente"].set("b1")
    check(w.siguiente(), "wizard Personaje: paso 1 -> 2")
    check(not w.siguiente(), "wizard Personaje: no deja avanzar sin personaje")
    w.elegir({"key": "13", "name": "Zarbon", "donor": 38, "port": False, "suggested": "Zarbon B1"})
    check(w.siguiente(), "wizard Personaje: paso 2 -> 3")
    w.v["preset"].set("solo_modelos")
    w.v["donante"].set(g.donor_values(w._auto())[g.donor_index(11, True)])      # Piccolo
    check(w.siguiente() and w.idx == 3, "wizard Personaje: llega al resumen")
    et = [e for _k, e, _v in w.filas()]
    check(len(et) >= 5 and any("Ajuste" in e or "Preset" in e for e in et) and any(
        "donante" in e or "donor" in e for e in et), "resumen Personaje muestra preset y donante: %s" % et)
    w.cambiar(2)
    check(w.idx == 2 and w.siguiente() and w.idx == 3, "«Cambiar» vuelve al paso y de ahí al resumen")
    w.montar()
    cmds = [c for t, c in app.selftest_cmds[n0:] if t.startswith("asistente:")]
    check(len(cmds) == 2 and "--preset" in cmds[0] and "solo_modelos" in cmds[0] and "--donante" in cmds[0]
          and cmds[0][cmds[0].index("--donante") + 1] == "11" and cmds[1][2] == "construir",
          "wizard Personaje monta: importar --preset/--donante + construir")
    w.cerrar()
    # 2) Aspecto y aura
    n0 = len(app.selftest_cmds)
    w = asistente.AsistenteAspecto(app, g)
    check(w.siguiente(), "wizard Aspecto: paso 1")
    w.v["aura"].set(g.T("Azul", "Blue"))
    w.v["ki"].set(g.T("Rojo", "Red"))
    check(w.siguiente() and w.siguiente() and w.idx == 3, "wizard Aspecto: llega al resumen")
    w.montar()
    cmds = [c for t, c in app.selftest_cmds[n0:] if t.startswith("asistente:")]
    check(len(cmds) == 1 and "colores.py" in cmds[0] and "--aura" in cmds[0] and "azul" in cmds[0] and "rojo" in cmds[0],
          "wizard Aspecto monta: colores nativo con aura y ki")
    w.cerrar()
    # 3) Traje (con modelo propio: funcion + construir)
    n0 = len(app.selftest_cmds)
    w = asistente.AsistenteTraje(app, g)
    w.siguiente()
    w.v["origen"].set("modelo")
    check(not w.siguiente(), "wizard Traje: pide el archivo del modelo")
    modelo = tempfile.NamedTemporaryFile(suffix=".amb", delete=False)
    modelo.close()
    w.v["modelo"].set(modelo.name)
    check(w.siguiente() and w.siguiente() and w.idx == 3, "wizard Traje: llega al resumen")
    w.montar()
    cmds = [c for t, c in app.selftest_cmds[n0:] if t.startswith("asistente:")]
    check(len(cmds) == 2 and cmds[0] == ["<funcion>"] and cmds[1][2] == "construir", "wizard Traje monta")
    w.cerrar()
    os.unlink(modelo.name)
    # 4) Instalar .zip + deshacer (carpeta temporal, sin motores)
    tmp = tempfile.mkdtemp(prefix="kit2_zip_")
    try:
        env = _env_falso(tmp)
        os.makedirs(os.path.join(env.mods, "viejo"))
        open(os.path.join(env.mods, "viejo", "manifest.txt"), "w").write("name=viejo\n")
        z = os.path.join(tmp, "pack.zip")
        _zip(z, {"nuevo/manifest.txt": "name=nuevo\n", "nuevo/datos.bin": "x", "nuevo/plugin.dll": "MZ",
                 "viejo/manifest.txt": "name=viejo2\n"})
        w2 = asistente.AsistenteZip.__new__(asistente.AsistenteZip)
        antes = _snap(env.mods)
        info = planes.inspeccionar_zip(z)
        check(sorted(info["mods"]) == ["nuevo", "viejo"] and info["ejecutables"] == ["nuevo/plugin.dll"],
              "zip: detecta los mods y el .dll")
        plan = planes.plan_zip(g, env, {"zip": z})
        okp, _txt, _lote = planes.ejecutar(g, env, plan, lambda _t: None)
        post = _snap(env.mods)
        check(okp and os.path.join("nuevo", "manifest.txt") in post and os.path.join("nuevo", "plugin.dll") not in post and
              open(os.path.join(env.mods, "viejo", "manifest.txt")).read() == "name=viejo2\n",
              "zip: instala, NO extrae el .dll y sustituye «viejo»")
        back = g.deshacer_ultimo(env)
        check(len(back) >= 2 and _snap(env.mods) == antes, "zip: Deshacer deja mods/ exactamente como estaba")
        _zip(z, {"../fuera.txt": "x"})
        check(planes.inspeccionar_zip(z)["error"] == "ruta_peligrosa", "zip: rechaza rutas con ..")
        open(z, "wb").write(b"no es un zip")
        check(planes.inspeccionar_zip(z)["error"] == "no_zip", "zip: rechaza un archivo que no es zip")
        del w2
    finally:
        shutil.rmtree(tmp, ignore_errors=True)       # carpeta temporal propia de esta prueba
    # 5) Poner música: wav (ffmpeg) -> mods/musica_x/musica.afs con un ADX válido; Deshacer lo aparta
    tmp = tempfile.mkdtemp(prefix="kit4_mus_")
    try:
        env = _env_falso(tmp)
        env.mods = os.path.join(tmp, "mods")
        wav = os.path.join(tmp, "t.wav")
        _tono(wav)
        w = asistente.AsistenteMusica(app, g)
        check(not w.siguiente(), "wizard Música: pide el archivo")
        w.v["archivo"].set(wav)
        check(w.siguiente() and w.idx == 1 and w.v["nombre"].get() == "t", "wizard Música: llega al resumen con el nombre")
        w.cerrar()
        antes = _snap(env.mods)
        plan = planes.plan_musica(g, env, {"archivo": wav, "nombre": "Mi canción"})
        okp, txt, _l = planes.ejecutar(g, env, plan, lambda _t: None)
        d = os.path.join(env.mods, "musica_mi_canci_n")
        afs = open(os.path.join(d, "musica.afs"), "rb").read() if okp else b""
        n, (off, tam) = struct.unpack("<I", afs[4:8])[0], struct.unpack("<II", afs[8:16]) if afs else (0, (0, 0))
        check(okp and afs[:3] == b"AFS" and n == 1 and musica.cabecera_adx(afs[off:off + tam]) is not None,
              "música: crea musica.afs con un ADX válido (%s)" % txt.strip()[-80:])
        back = g.deshacer_ultimo(env)
        check(back and _snap(env.mods) == antes, "música: Deshacer aparta la carpeta")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    ok = probar.rapido(say) and ok
    return ok


def _tono(destino):
    """WAV de 2 s con un tono (para probar la conversión; si no hay ffmpeg, un ADX mínimo)."""
    import math, wave  # noqa: E401
    with wave.open(destino, "wb") as w:
        w.setnchannels(2), w.setsampwidth(2), w.setframerate(44100)
        w.writeframes(b"".join(struct.pack("<hh", v, v) for v in (int(8000 * math.sin(i * 0.05)) for i in range(88200))))


# ------------------------------------------------------------------------------------------ real
def real(g, us, base, say):
    """Los cuatro asistentes de verdad, sin ventanas, con us/ = COPIA de los datos. base = carpeta de trabajo nueva."""
    sys.path.insert(0, g.HERE)
    import auditar_importar as aud
    import subprocess
    ok = True

    def check(cond, msg):
        nonlocal ok
        if not cond:
            ok = False
        say(("OK" if cond else "FAIL") + ": " + msg)

    real_cmn = os.path.join(os.path.dirname(g.HERE), "us", "data_cmn.afs")
    huella = (os.path.getsize(real_cmn), os.path.getmtime(real_cmn)) if os.path.isfile(real_cmn) else None
    os.makedirs(base, exist_ok=True)
    env = g.Env({"us_dir": us})
    env.mods, env.backups = os.path.join(base, "mods"), os.path.join(base, "modkit_backups")
    os.makedirs(env.mods, exist_ok=True)
    check(os.path.normcase(env.us) == os.path.normcase(os.path.abspath(us)) and env.pass_us(), "usa la copia de data_cmn: %s" % env.us)

    def norm(lineas, *mods):
        out = set()
        for ln in lineas:
            for m in mods:
                ln = ln.replace(m, "<mod>")
            out.add(ln)
        return out

    def lanzar(plan, nombre):
        antes = _snap(env.mods)
        ok1, texto, _l = planes.ejecutar(g, env, plan, lambda t: None)
        return antes, ok1, texto

    def deshacer(antes, nombre):
        g.deshacer_ultimo(env)
        check(_snap(env.mods) == antes, "%s: Deshacer deja mods/ idéntico (hash de todos los archivos)" % nombre)

    # ---- 1) Personaje: B1 Zarbon
    r = {"fuente": "b1", "clave": "13", "nombre": "Zarbon B1", "preset": "original", "donante": -1}
    plan = planes.plan_personaje(g, env, r)
    antes_p, ok1, txt = lanzar(plan, "personaje")
    mod_p = plan.nuevos[0]
    check(ok1 and os.path.isfile(os.path.join(env.mods, mod_p, "personaje.toml")), "Personaje: crea el mod %s" % mod_p)
    check(ok1 and "_roster listo" in txt and os.path.isdir(os.path.join(env.mods, "_roster")), "Personaje: el montaje termina (_roster listo)")
    ref = os.path.join(base, "aud")
    rc1, t1 = aud.run(["importar.py", "importar", "b1", "13", "--mod", "aud_x", "--nombre", "Zarbon B1", "--mods", ref,
                       "--us", us, "--preset", "original"])
    rc2, t2 = aud.run(["roster_build.py", "construir", "--mods", ref, "--us", us, "--force"], timeout=1800)
    nuevos = norm(planes.avisos(txt), mod_p) - norm(aud.problemas(t1) + aud.problemas(t2), "aud_x")
    check(rc1 == 0 and rc2 == 0 and not nuevos, "Personaje: sin avisos nuevos frente a auditar_importar (%s)" % sorted(nuevos)[:3])

    # ---- 2) Aspecto y aura (sacar PNG como hace el botón, luego montar colores + texturas)
    pre = _snap(env.mods)
    mod_t = g.unique_mod_name(env.mods, "tex_goku")
    rc, t = aud.run(g.cmd_tex_extract(env, 264, mod_t)[1:])
    carpeta = os.path.join(env.mods, mod_t, "textures")
    r = {"id": 0, "nombre": "Goku", "aura": "azul", "ki": "rojo", "bin": 264, "png_dir": carpeta, "mod_texturas": mod_t}
    plan = planes.plan_aspecto(g, env, r)
    ok2, texto, _l = planes.ejecutar(g, env, plan, lambda t: None)
    mod_c = plan.nuevos[0]
    check(rc == 0 and ok2 and os.path.isfile(os.path.join(env.mods, mod_c, "us", "data_cmn.afs", "23", "geom.bin")) and os.path.isfile(
        os.path.join(env.mods, mod_c, "us", "data_cmn.afs", "533", "geom.bin")) and os.path.isfile(
        os.path.join(env.mods, mod_c, "manifest.txt")), "Aspecto: crea el mod de colores %s" % mod_c)
    check(ok2 and os.path.isfile(os.path.join(env.mods, mod_t, "us", "data_cmn.afs", "264", "geom.bin")),
          "Aspecto: el montaje de texturas termina (entrada 264 del mod %s)" % mod_t)
    check(not [a for a in planes.avisos(texto) if "aviso" in a.lower() or "error" in a.lower()],
          "Aspecto: sin avisos (%s)" % planes.avisos(texto)[:2])
    deshacer(pre, "Aspecto")

    # ---- 3) Traje desde el personaje importado + con modelo propio
    pre = _snap(env.mods)
    plan = planes.plan_traje(g, env, {"personaje": 38, "nombre": "Traje de prueba", "port": mod_p})
    ok3, txt3, _l = planes.ejecutar(g, env, plan, lambda t: None)
    mod_tr = plan.nuevos[0]
    check(ok3 and os.path.isfile(os.path.join(env.mods, mod_tr, "traje.toml")) and "_roster listo" in txt3,
          "Traje: crea %s y el montaje termina" % mod_tr)
    check(not [a for a in planes.avisos(txt3) if "Traceback" in a], "Traje: sin errores en el registro")
    deshacer(pre, "Traje")
    pre = _snap(env.mods)
    modelo = next(os.path.join(dp, f) for dp, _d, fs in os.walk(os.path.join(env.mods, mod_p, "modelos")) for f in fs)
    plan = planes.plan_traje(g, env, {"personaje": 38, "nombre": "Traje propio", "modelo": modelo})
    ok3b, txt3b, _l = planes.ejecutar(g, env, plan, lambda t: None)
    check(ok3b and "_roster listo" in txt3b, "Traje (modelo propio): crea y monta")
    deshacer(pre, "Traje (modelo propio)")

    # ---- 4) Instalar .zip (un mod de personaje: instala + monta) y sustituir uno existente
    pre = _snap(env.mods)
    z = os.path.join(base, "pack.zip")
    with zipfile.ZipFile(z, "w") as zf:
        raiz = os.path.join(env.mods, mod_p)
        for dp, _d, fs in os.walk(raiz):
            for f in fs:
                zf.write(os.path.join(dp, f), "copia_zip/" + os.path.relpath(os.path.join(dp, f), raiz).replace("\\", "/"))
    plan = planes.plan_zip(g, env, {"zip": z})
    ok4, txt4, _l = planes.ejecutar(g, env, plan, lambda t: None)
    check(ok4 and os.path.isfile(os.path.join(env.mods, "copia_zip", "personaje.toml")) and "_roster listo" in txt4,
          "Zip: instala el mod y lo monta")
    deshacer(pre, "Zip")
    pre = _snap(env.mods)
    with zipfile.ZipFile(z, "w") as zf:
        zf.writestr(mod_p + "/manifest.txt", "name=sustituto\n")
    plan = planes.plan_zip(g, env, {"zip": z})
    ok4b, _t, _l = planes.ejecutar(g, env, plan, lambda t: None)
    check(ok4b and os.path.isfile(os.path.join(env.mods, mod_p, "manifest.txt")) and not os.path.isfile(
        os.path.join(env.mods, mod_p, "personaje.toml")), "Zip: sustituye un mod existente")
    deshacer(pre, "Zip (sustituir)")

    # ---- deshacer el personaje: mods/ vuelve a estar vacío
    deshacer(antes_p, "Personaje")
    huella2 = (os.path.getsize(real_cmn), os.path.getmtime(real_cmn)) if os.path.isfile(real_cmn) else None
    check(huella == huella2, "los datos reales del usuario (us/data_cmn.afs) no se han tocado")
    return ok

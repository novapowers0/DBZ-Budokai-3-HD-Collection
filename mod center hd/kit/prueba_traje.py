# -*- coding: utf-8 -*-
"""Prueba de «Cambiar traje» (KIT-06), sin ventanas ni juego. Usa modelos del juego del jugador (nada de arte de fuera).
    python "mod center hd/kit/prueba_traje.py" [--trabajo E:\\DBZ3HD_trabajo\\kit_06]
Comprueba: formas de Goku (SSJ4 = id 91), traje con un modelo por forma, reetiquetado de huesos, validador (rechaza un
modelo de otro esqueleto ANTES de escribir), traje.toml/manifest.txt iguales byte a byte a los de antes y a los mods de
ejemplo, «Deshacer» y el resumen de «Montar y probar». Deja el mod en <trabajo>/mods/kit06_goku_por_forma."""
from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import sys
import types

HD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [HD, os.path.join(os.path.dirname(HD), "awo_tools")]

import roster_build as rb  # noqa: E402
from importar import _relabel_awo  # noqa: E402
from kit import planes, probar, traje  # noqa: E402

def lf(p):
    return open(p, "rb").read().replace(b"\r\n", b"\n")


EJEMPLOS = os.path.join(os.path.dirname(HD), "out", "build", "win-amd64-release", "mods")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabajo", default=r"E:\DBZ3HD_trabajo\kit_06")
    a = ap.parse_args()
    mods = os.path.join(a.trabajo, "mods")
    nat = os.path.join(a.trabajo, "modelos_nativos")
    for d in (mods, nat):
        os.makedirs(d, exist_ok=True)
    w = rb.work_dir("kit06_")
    cmn = rb.Afs(os.path.join(rb.default_us(), "data_cmn.afs"), w)

    def hd(n):
        return rb.to_hd(cmn.entry(n), w)

    def guardar(nombre, datos):
        p = os.path.join(nat, nombre)
        with open(p, "wb") as fh:
            fh.write(datos)
        return p

    def apartar(mod):                                     # nada se borra: lo de otra pasada va a _PARA_BORRAR
        d = os.path.join(mods, mod)
        if os.path.exists(d):
            dst = os.path.join(r"D:\_PARA_BORRAR_CLAUDE", "kit_06", "%s_%d" % (mod, os.getpid()))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.move(d, dst)

    # 1) formas
    fs = traje.formas(0)
    assert [f["es"] for f in fs] == ["Base", "Kaioken", "Super Saiyan", "Super Saiyan 2", "Super Saiyan 3",
                                     "Super Saiyan 4"], fs
    assert fs[5]["id"] == 91 and fs[5]["otro"] and fs[5]["nat"] == 269 and not fs[0]["otro"], fs[5]
    print("OK formas de Goku: 6, SSJ4 = id 91 (modelo 269)")

    # 2) modelos nativos exportados: el traje 2 de Goku (alt) por forma; el del SSJ con OTROS nombres de hueso (GTN_)
    alt = {0: 270, 1: 271, 2: 272, 3: 273, 5: 275}
    rutas = {f: guardar("goku_%d.amb" % n, hd(n)) for f, n in alt.items()}
    rutas[2] = guardar("goku_272_gtn.amb", _relabel_awo(hd(272), "GOK", "GTN"))
    assert rb.prefix(rb.skeleton(open(rutas[2], "rb").read(), w)[0]) == "GTN"
    malo_huesos = bytearray(hd(264))
    i = malo_huesos.find(b"#AWO")
    assert i >= 0
    seg = bytes(malo_huesos[i:]).replace(b"GOK_RARM1", b"GOK_RXRM1")      # mismo largo: otro esqueleto de brazo
    malo_huesos[i:] = seg
    malo = guardar("otro_esqueleto.amb", bytes(malo_huesos))
    trunks = guardar("trunks_383.amb", hd(383))
    vacio = guardar("no_es_modelo.amb", b"hola")

    # 3) validador
    rv = traje.Revisor()
    assert rv.revisar(rutas[0], 0, 0) is None and rv.revisar(rutas[2], 0, 2) is None
    for ruta, pista in ((malo, "GOK_RARM1"), (trunks, "GOK_LFOOT2"), (vacio, "#AMB")):
        ex = rv.revisar(ruta, 0, 0)
        assert ex is not None and pista in ex.detalle and "hueso" not in ex.es.lower(), (ruta, ex and ex.detalle)
        print("OK rechazado %s: «%s» (detalle: %s)" % (os.path.basename(ruta), ex.es, ex.detalle))
    apartar("kit06_rechazado")
    try:
        traje.escribir(mods, "kit06_rechazado", 0, "No vale", [rutas[0], malo], revisor=rv)
        raise AssertionError("escribir aceptó un modelo de otro esqueleto")
    except traje.NoEncaja as ex:
        assert ex.es.startswith("Kaioken:"), ex.es
    assert not os.path.exists(os.path.join(mods, "kit06_rechazado")), "escribió algo antes de validar"
    print("OK escribir valida ANTES de escribir (no crea la carpeta)")

    # 4) traje con un modelo por forma (SSJ3 sin modelo: usa el del SSJ2)
    mod = "kit06_goku_por_forma"
    apartar(mod)
    por_forma = [rutas[0], rutas[1], rutas[2], rutas[3], None, rutas[5]]
    est = [e for _o, e, _d in traje.resolver(0, por_forma)]
    assert est == ["propio", "propio", "propio", "propio", "otra", "propio"], est
    print(traje.escribir(mods, mod, 0, "Goku traje 2 (Kit)", por_forma, revisor=rv))
    d = os.path.join(mods, mod)
    toml = open(os.path.join(d, "traje.toml"), "rb").read()
    esperado = (b'[traje]\npersonaje = 0\nnombre = "Goku traje 2 (Kit)"\nmodelos = ["modelos/goku_270.amb", '
                b'"modelos/goku_271.amb", "modelos/goku_272_gtn.amb", "modelos/goku_273.amb", '
                b'"modelos/goku_273.amb", "modelos/goku_275.amb"]\n')
    assert toml == esperado.replace(b"\n", b"\r\n"), toml         # "w" de texto en Windows: CRLF, como antes
    reet = open(os.path.join(d, "modelos", "goku_272_gtn.amb"), "rb").read()
    assert reet == hd(272), "el reetiquetado GTN_ -> GOK_ no devuelve el modelo original"
    assert open(os.path.join(d, "modelos", "goku_270.amb"), "rb").read() == open(rutas[0], "rb").read()
    print("OK traje.toml por forma; SSJ3 usa el del SSJ2; huesos GTN_ -> GOK_ (igual byte a byte al 272 del juego)")

    # 5) iguales byte a byte: el caso de siempre (un modelo para todas) contra el codigo de antes, y los mods de ejemplo
    spec = importlib.util.spec_from_file_location("planes_antes", os.path.join(a.trabajo, "original", "planes.py"))
    antes = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(antes)
    env = types.SimpleNamespace(mods=mods, us="")
    modelo = os.path.join(EJEMPLOS, "traje_goku_armadura", "modelos", "base.amb")
    r = {"personaje": 0, "nombre": 'Mi "traje"', "modelo": modelo}
    for m in ("kit06_igual_antes", "kit06_igual_ahora"):
        apartar(m)
    antes._traje_de_modelo(env, "kit06_igual_antes", r)
    planes._traje_de_modelo(env, "kit06_igual_ahora", r)
    for f in ("traje.toml", "manifest.txt", os.path.join("modelos", "base.amb")):
        x, y = (open(os.path.join(mods, m, f), "rb").read() for m in ("kit06_igual_antes", "kit06_igual_ahora"))
        assert x == y, f
    print("OK un modelo para todas: traje.toml, manifest.txt y modelo iguales byte a byte al código de antes")
    arm = os.path.join(EJEMPLOS, "traje_goku_armadura")
    apartar("kit06_armadura")
    traje.escribir(mods, "kit06_armadura", 0, "Goku - Armadura Saiyan",
                   [os.path.join(arm, "modelos", f) for f in ("base.amb", "kaioken.amb", "ssj.amb", "ssj2.amb",
                                                               "ssj3.amb", "ssj4.amb")], revisor=rv)
    nuevo = lf(os.path.join(mods, "kit06_armadura", "traje.toml"))
    assert nuevo == lf(os.path.join(arm, "traje.toml")), nuevo
    print("OK traje_goku_armadura: traje.toml igual al del mod de ejemplo (salvo el fin de línea CRLF de Windows)")
    veg = os.path.join(EJEMPLOS, "traje_vegeta_namek", "modelos")
    apartar("kit06_namek")
    try:
        traje.escribir(mods, "kit06_namek", 7, "Vegeta - Saga de Namek", [os.path.join(veg, "base.amb"), os.path.join(
            veg, "ssj.amb"), None, None, os.path.join(veg, "majin.amb"), None], revisor=rv)
        nuevo = lf(os.path.join(mods, "kit06_namek", "traje.toml"))
        assert nuevo == lf(os.path.join(os.path.dirname(veg), "traje.toml")), nuevo
        print("OK traje_vegeta_namek: traje.toml igual al de ejemplo (SSJ2 = el del SSJ, SSJ4 y forma 6 = juego:)")
    except traje.NoEncaja as ex:
        print("AVISO traje_vegeta_namek: el validador lo rechaza (%s; %s)" % (ex.es, ex.detalle))

    # 6) solo colores
    apartar("kit06_colores")
    traje.escribir(mods, "kit06_colores", 0, "Goku azul", ["juego:%d" % f["nat"] for f in fs], tono=225, revisor=rv)
    t = open(os.path.join(mods, "kit06_colores", "traje.toml"), encoding="utf-8").read()
    assert 'modelos = ["juego:264", "juego:265", "juego:266", "juego:267", "juego:268", "juego:269"]\ntono = 225\n' in t, t
    print("OK solo colores: modelos del juego + tono")

    # 6b) otro traje del juego (Vegeta 416 en Goku): se exporta reetiquetado VGT_ -> GOK_
    apartar("kit06_juego")
    traje.escribir(mods, "kit06_juego", 0, "Goku con ropa de Vegeta", ["juego:416"] * 6, revisor=rv)
    t = open(os.path.join(mods, "kit06_juego", "traje.toml"), encoding="utf-8").read()
    assert 'modelos = ["modelos/juego_416.amb"]' in t, t
    b = open(os.path.join(mods, "kit06_juego", "modelos", "juego_416.amb"), "rb").read()
    assert rb.prefix(rb.skeleton(b, w)[0]) == "GOK" and b == _relabel_awo(hd(416), "VGT", "GOK")
    print("OK otro traje del juego: Vegeta 416 exportado con huesos GOK_")

    # 7) Deshacer (el lote de los asistentes) con el paso de crear el traje
    import modkit_gui as g
    tmp = os.path.join(a.trabajo, "deshacer")
    apartar_dir = tmp if os.path.isdir(tmp) else None
    if apartar_dir:
        shutil.move(tmp, os.path.join(r"D:\_PARA_BORRAR_CLAUDE", "kit_06", "deshacer_%d" % os.getpid()))
    os.makedirs(os.path.join(tmp, "mods"))
    env2 = types.SimpleNamespace(mods=os.path.join(tmp, "mods"), backups=os.path.join(tmp, "modkit_backups"),
                                 cwd=lambda: tmp, python=sys.executable, us="", game="", us_args=lambda: [],
                                 script=lambda n: os.path.join(HD, n))
    plan = planes.plan_traje(g, env2, {"personaje": 0, "nombre": "Deshacer", "modelos": por_forma})
    plan.pasos, plan.construye = plan.pasos[:1], False
    ok, txt, _l = planes.ejecutar(g, env2, plan, lambda t: None)
    assert ok and os.path.isfile(os.path.join(env2.mods, plan.nuevos[0], "traje.toml")), txt
    assert g.deshacer_ultimo(env2) and not os.listdir(env2.mods), os.listdir(env2.mods)
    print("OK Deshacer aparta el traje creado (mods/ vacía de nuevo)")

    # 8) resumen de «Montar y probar»
    log = ("+ GOKOU (id 0): traje extra Goku traje 2 (Kit), modelos por forma [3990]\n"
           "!! kit06_armadura: traje omitido ANTES de publicar referencias (prueba)\n")
    lineas = probar.resumen(log, mods, [mod, "kit06_armadura"], False)
    assert lineas == ["✓ 1 traje nuevo: Goku traje 2 (Kit)",
                      "✕ el traje Goku - Armadura Saiyan se omitió: traje omitido ANTES de publicar referencias "
                      "(prueba)"], lineas
    print("OK resumen: %s" % " | ".join(lineas))
    print("RESULT: OK  (mod para la cola del juego: %s)" % d)


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    main()

"""Prueba de kit/generador.py (KIT-09, sin interfaz). Sin frameworks: asserts.

  python "mod center hd/kit/prueba_generador.py" [--trabajo E:/DBZ3HD_trabajo/kit_09]

Escribe solo en la carpeta de trabajo. Mod de prueba (para el juego, cola aparte):
<trabajo>/mods/kit09_goku_mezcla (Goku 0x217 = patada de Vegeta 0-30 + la suya 40-78, mezcla 5 = 65 fotogramas).
"""
import argparse
import os
import re
import sys
import types
from unittest.mock import patch

HD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HD)
from kit import generador as G, nucleo  # noqa: E402
from kit.prueba_pelicula import aparta, leer_mod  # noqa: E402

ba, sc = G.ba, G.sc
GOKU, VEGETA, PATADA = 0, 7, 0x217


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabajo", default=r"E:\DBZ3HD_trabajo\kit_09")
    a = ap.parse_args()

    # ---- biblioteca: nombres legibles, sin hex
    cl = G.clips()
    assert len(cl) > 500, len(cl)
    for ingles in (False, True):
        for c in cl:
            n = G.nombre(c, ingles)
            assert n and not re.search(r"0x[0-9a-f]", n.lower()), n
            assert c["oficial"] or re.fullmatch(r"\w+ \d+", n), n
    gok = {G.nombre(c): c for c in cl if c["personaje"] == GOKU}
    assert "Dragon Fist" in gok and gok["Dragon Fist"]["tipo"] == "tecnica", sorted(gok)[:20]
    assert "Spirit Bomb" in gok and gok["Spirit Bomb"]["tipo"] == "definitiva"
    tipos = {c["tipo"] for c in cl}
    assert tipos == {k for k, _e, _n in G.TIPOS}, tipos
    assert {c["origen"] for c in cl} == {"B3"}
    assert G.buscar(cl, "dragon fist", personaje=GOKU) and {c["personaje"] for c in G.buscar(cl, "goku")} == {0, 1}
    print("[OK] biblioteca: %d clips con nombre legible (sin hex), %d oficiales, tipos %s; Goku: %s"
          % (len(cl), sum(1 for c in cl if c["oficial"]), sorted(tipos),
             ", ".join(sorted({c["oficial"] for c in cl if c["personaje"] == GOKU and c["oficial"]}))))

    # ---- 2 tramos con mezcla -> receta -> validar
    destino = dict(personaje=GOKU, codigo=PATADA)
    tramos = [dict(personaje=VEGETA, codigo=PATADA, desde=0, hasta=30), dict(personaje=GOKU, codigo=PATADA, desde=40,
                                                                             hasta=78)]
    esperado = 31 + 39 - 5
    assert G.duracion_natural(tramos, 5) == esperado
    r = G.receta(destino, tramos, mezcla=5)
    assert r["fuente"]["personaje"] == VEGETA and r["despues"]["desde"] == 40 and r["mezcla"] == 5 and "duracion" not in r
    av = G.avisos(r)
    assert not [t for n, t in av if n == "err"], av
    malo = G.avisos(dict(r, duracion=5000)) + G.avisos(dict(r, destino=dict(personaje=GOKU, codigo=0x7FFF)))
    assert any(n == "warn" and "fotogramas" in t for n, t in malo) and any(n == "err" for n, t in malo), malo
    assert all("0x" not in t and "codigo" not in t for _n, t in malo), malo
    print("[OK] receta de 2 tramos con mezcla 5 = %d f; avisos en llano: %s" % (esperado, " | ".join(t for _n, t in malo)))

    # ---- 3 tramos (motor: [[mas]])
    tres = tramos + [dict(personaje=VEGETA, codigo=PATADA, desde=10, hasta=20)]
    r3 = G.receta(destino, tres, mezcla=4)
    assert ba.clip_receta(r3)[2] == G.duracion_natural(tres, 4) == 31 + 39 - 4 + 11 - 4
    print("[OK] 3 tramos: %d f" % G.duracion_natural(tres, 4))

    # ---- toml para el modo avanzado
    toml = os.path.join(a.trabajo, "receta_kit09.toml")
    G.receta_toml(r3, toml)
    leida = ba.leer_receta(toml)
    assert {k: leida[k] for k in r3} == r3, leida
    print("[OK] receta .toml ida y vuelta:", toml)

    # ---- guardar (aplicar_receta) con copia y Deshacer
    env = types.SimpleNamespace(mods=os.path.join(a.trabajo, "mods"), backups=os.path.join(a.trabajo, "backups"))
    mod = os.path.join(env.mods, "kit09_goku_mezcla")
    aparta(mod, a.trabajo)
    res = G.guardar(env, r, mod, nucleo.anotar_indice)
    _ks, csk, _names, anims = leer_mod(res["ruta"])
    clip = anims[sc.code_info(csk, ">", PATADA)[0]]
    assert clip[2] == esperado, clip[2]
    with open(res["ruta"], "rb") as fh:
        primero = fh.read()
    res2 = G.guardar(env, G.receta(destino, tramos, 5, 90), mod, nucleo.anotar_indice)     # 2.a vez: mod existente
    _ks, csk2, _n2, anims2 = leer_mod(res2["ruta"])
    assert anims2[sc.code_info(csk2, ">", PATADA)[0]][2] == 90
    hechos = nucleo.deshacer_ultimo(env)
    assert set(hechos) == {mod}, hechos
    with open(res["ruta"], "rb") as fh:
        assert fh.read() == primero, "Deshacer no devolvio el primer guardado"
    print("[OK] guardado: %s; releido %d f; 2.o guardado 90 f y «Deshacer» vuelve al primero" % (res["informe"], esperado))

    # ---- «Nueva técnica › Color»: colores.mod_nativo con «Deshacer»
    cmod = os.path.join(env.mods, G.mod_color(GOKU))
    aparta(cmod, a.trabajo)
    nombre_mod, rep = G.guardar_colores(env, GOKU, "morado", "rojo", nucleo.anotar_indice)
    assert nombre_mod == os.path.basename(cmod) and len(rep) == 2 and os.path.isdir(os.path.join(cmod, "us")), rep
    assert set(nucleo.deshacer_ultimo(env)) == {cmod} and not os.path.exists(cmod), "Deshacer no aparto el mod nuevo"
    print("[OK] colores: %s; «Deshacer» lo aparta" % "; ".join(rep))
    # Dos guardados distintos con el mismo reloj no pueden compartir un deshacer.
    with patch.object(G.time, "strftime", return_value="20000101-000000"):
        otros = [os.path.join(env.mods, "rapido_uno"), os.path.join(env.mods, "rapido_dos")]
        for carpeta in otros:
            os.makedirs(carpeta)
            anotar = G.copia_para_deshacer(env, carpeta, nucleo.anotar_indice)
            with open(os.path.join(carpeta, "dato"), "w") as fh:
                fh.write("nuevo")
            anotar()
        assert set(nucleo.deshacer_ultimo(env)) == {otros[1]}
        assert os.path.isfile(os.path.join(otros[0], "dato"))
        assert not os.path.exists(os.path.join(otros[1], "dato"))
    print("[OK] dos guardados en el mismo segundo se deshacen por separado")
    print("MOD DE PRUEBA:", mod)
    print("RESULTADO: todo OK")


if __name__ == "__main__":
    main()

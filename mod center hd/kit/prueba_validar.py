# -*- coding: utf-8 -*-
"""Prueba de KIT-10 (asserts, sin abrir el juego): validar.py, choques.py y mis_mods.py.
Trabaja en E:\\DBZ3HD_trabajo\\kit_10 (nunca en la carpeta mods del juego).
  python "mod center hd/kit/prueba_validar.py"
"""
from __future__ import annotations

import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from kit import choques, mis_mods, nucleo, validar  # noqa: E402

TRABAJO = r"E:\DBZ3HD_trabajo\kit_10"
RAIZ = os.path.dirname(os.path.dirname(HERE))
REALES = [r"E:\DBZ3HD_trabajo\kit_06\montaje\mods\kit06_goku_por_forma", r"E:\DBZ3HD_trabajo\kit_07\mods\Montanas Nieve",
          r"E:\DBZ3HD_trabajo\kit_08\mods\kit08_goku_patada_vegeta", r"E:\DBZ3HD_trabajo\kit_09\mods\kit09_goku_mezcla",
          os.path.join(RAIZ, "out", "build", "win-amd64-release", "mods", "traje_goku_armadura"),
          os.path.join(RAIZ, "out", "build", "win-amd64-release", "mods", "traje_vegeta_namek")]


def escribir(ruta, texto=b"x"):
    os.makedirs(os.path.dirname(ruta), exist_ok=True)
    with open(ruta, "wb") as fh:
        fh.write(texto if isinstance(texto, bytes) else texto.encode("utf-8"))


def apartar(path):
    """Lo de la pasada anterior va a D:\\_PARA_BORRAR_CLAUDE (nada se borra)."""
    if os.path.exists(path):
        dst = os.path.join(r"D:\_PARA_BORRAR_CLAUDE\kit_10", time.strftime("%Y%m%d_%H%M%S_") + os.path.basename(path))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.move(path, dst)


def prueba_errores(mods):
    m = os.path.join(mods, "prueba_mal")
    escribir(os.path.join(m, "personaje.toml"), '[personaje]\nnombre = "Prueba"\ndonante = 0\nformas = 9\n'
                                                'ki_bse = [3, 4]\nmodelos = ["modelos/falta.amb"]\n')
    r = validar.validar(m, mods, pesado=False)
    errores = [x for x in r if x[0] == "error"]
    for x in errores:
        print("   ", x)
    assert any("ki_bse" in x[1] and "ki_base" in x[1] for x in errores), r
    assert any("modelos/falta.amb" in x[1] for x in errores), r
    assert any("9 formas" in x[1] for x in errores), r
    assert len(errores) == 3, errores
    en = validar.validar(m, mods, ingles=True, pesado=False)
    assert any("'ki_base'?" in x[1] for x in en), en
    print("OK: clave mal escrita + fichero que falta + 9 formas = 3 errores con sugerencia")
    mis_mods.poner(_env(mods), ["prueba_mal"], False, nucleo.anotar_indice)


def prueba_choque(mods):
    for n in ("Zeta_anim", "alfa_anim"):     # orden del juego: mayusculas antes ("Zeta" < "alfa")
        escribir(os.path.join(mods, n, "us", "data_cmn.afs", "292", "geom.bin"))
        escribir(os.path.join(mods, n, "animaciones.json"), '[{"destino": {"personaje": 0, "codigo": 535}}]')
    c = choques.de_mod(choques.choques(mods), "alfa_anim")
    print("   ", c[0]["texto"] if c else c)
    assert len(c) == 1 and c[0]["tipo"] == "receta" and c[0]["gana"] == "Zeta_anim" and c[0]["pierden"] == ["alfa_anim"], c
    r = validar.validar(os.path.join(mods, "alfa_anim"), mods, pesado=False)
    assert any(x[0] == "error" and "Zeta_anim" in x[1] for x in r), r
    for n in ("pack_b", "pack_a"):
        escribir(os.path.join(mods, n, "0123456789abcdef_64x64_DXT1.dds"))
    escribir(os.path.join(mods, "pack_a", "fedcba9876543210_64x64_DXT1.dds"))
    t = [x for x in choques.choques(mods) if x["tipo"] == "textura"]
    assert len(t) == 1 and t[0]["gana"] == "pack_b", t          # el pack mas pequeno gana aunque vaya despues
    print("OK: choques con el ganador del juego (AFS: primero en orden; texturas: pack mas pequeno)")
    mis_mods.poner(_env(mods), ["Zeta_anim", "alfa_anim", "pack_a", "pack_b"], False, nucleo.anotar_indice)


def prueba_reales():
    us = None
    try:
        from kit import base  # noqa: PLC0415
        us = base.find_us()
    except Exception:  # noqa: BLE001
        pass
    for d in REALES:
        r = validar.validar(d, us=us)
        propios = [x for x in r if x[0] == "error" and not x[2].startswith(("receta:", "afs:", "textura:"))]
        choq = [x for x in r if x[0] != "ok" and x not in propios]
        print("   %-28s %d bien, %d avisos, %d errores propios%s" % (
            os.path.basename(d), sum(x[0] == "ok" for x in r), sum(x[0] == "aviso" for x in r), len(propios),
            "".join("\n      [%s] %s" % (x[0], x[1]) for x in choq)))
        assert not propios, propios
    print("OK: los 6 mods reales validan sin errores propios (us=%s)" % us)


def _env(mods):
    class E:
        pass
    e = E()
    e.mods, e.backups = mods, os.path.join(os.path.dirname(mods), "modkit_backups")
    return e


def prueba_limpio(mods):
    """Sobre una COPIA de mods: activar/desactivar, juego limpio y deshacer (con el marcador .disabled)."""
    for d in REALES[:2] + REALES[4:]:
        shutil.copytree(d, os.path.join(mods, os.path.basename(d)))
    env = _env(mods)
    antes = choques.activos(mods)
    assert "traje_goku_armadura" in antes, antes
    assert mis_mods.poner(env, ["traje_goku_armadura"], False, nucleo.anotar_indice) == ["traje_goku_armadura"]
    assert "traje_goku_armadura" not in choques.activos(mods)
    assert nucleo.scan_mods(mods)[-1]["enabled"] is False          # el Kit lo ve apagado (mismo marcador)
    assert nucleo.deshacer_ultimo(env, os.path.join(mods, "traje_goku_armadura"))
    assert "traje_goku_armadura" in choques.activos(mods)
    apagados = mis_mods.juego_limpio(env, nucleo.anotar_indice)
    assert apagados == antes and choques.activos(mods) == [], (apagados, choques.activos(mods))
    assert nucleo.deshacer_ultimo(env)
    assert choques.activos(mods) == antes, choques.activos(mods)
    mis_mods.poner(env, ["traje_vegeta_namek"], False, nucleo.anotar_indice)
    assert mis_mods.poner(env, ["traje_vegeta_namek"], True, nucleo.anotar_indice) == ["traje_vegeta_namek"]
    assert nucleo.deshacer_ultimo(env) and "traje_vegeta_namek" not in choques.activos(mods)   # vuelve a apagado
    nucleo.deshacer_ultimo(env)
    assert choques.activos(mods) == antes
    z = mis_mods.compartir(os.path.join(mods, "traje_vegeta_namek"), os.path.join(TRABAJO, "zips", "namek.zip"))
    from kit import planes  # noqa: PLC0415
    info = planes.inspeccionar_zip(z)
    assert not info["error"] and list(info["mods"]) == ["traje_vegeta_namek"], info
    assert {mis_mods.tipo(os.path.join(mods, n)) for n in antes} == {"traje", "escenario"}, antes
    p = mis_mods.guardar_prueba(env, "ok", "Funciona", ["✓ 1 traje nuevo: x"], ["traje_vegeta_namek"])
    m = {"name": "traje_vegeta_namek", "enabled": True}
    assert mis_mods.estado(m, p, [])[0] == "visto" and mis_mods.estado(dict(m, name="otro"), p, [])[0] == "sin_probar"
    print("OK: activar/desactivar, «Volver al juego limpio» + Deshacer, Compartir .zip (copia %s)" % mods)


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    validar.claves_validas.cache_clear()
    for p in ("mods", "mods_choques", "modkit_backups", "zips"):
        apartar(os.path.join(TRABAJO, p))
    prueba_errores(os.path.join(TRABAJO, "mods_choques"))
    prueba_choque(os.path.join(TRABAJO, "mods_choques"))
    prueba_limpio(os.path.join(TRABAJO, "mods"))
    prueba_reales()
    choques.prueba()
    print("prueba_validar: TODO OK")


if __name__ == "__main__":
    main()

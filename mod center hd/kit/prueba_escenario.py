# -*- coding: utf-8 -*-
"""Prueba de «Nuevo escenario» (KIT-07): estados, entrada libre, validacion y creacion REAL de «Montañas nevadas»
(B2 grupo 7) con el Kit, comparada byte a byte con lo que deja `escenarios_convertir.py mod 7 ...` con los mismos
parametros.   python kit/prueba_escenario.py [carpeta_trabajo]   (por defecto E:\\DBZ3HD_trabajo\\kit_07)"""
import filecmp
import os
import shutil
import subprocess
import sys
import tempfile
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
MCH = os.path.dirname(HERE)
sys.path[:0] = [MCH]
from kit import escenarios as ES  # noqa: E402
from kit import nucleo, planes  # noqa: E402

TRABAJO = sys.argv[1] if len(sys.argv) > 1 else r"E:\DBZ3HD_trabajo\kit_07"
PS2 = os.path.join(os.path.dirname(MCH), "ps2_games")
NOMBRE = "Montañas Nieve"            # 14 letras; la carpeta queda «Montanas Nieve»


def apartar(d):
    """Lo de una pasada anterior va a _viejos (no se borra nada)."""
    if os.path.exists(d):
        dst = os.path.join(TRABAJO, "_viejos", time.strftime("%Y%m%d-%H%M%S"), os.path.basename(d))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.move(d, dst)
    os.makedirs(d)


def iguales(a, b):
    """[ruta relativa distinta o que falta] entre dos carpetas (recursivo, byte a byte)."""
    malos = []
    for raiz, _ds, fs in os.walk(a):
        for f in fs:
            pa = os.path.join(raiz, f)
            pb = os.path.join(b, os.path.relpath(pa, a))
            if not os.path.isfile(pb) or not filecmp.cmp(pa, pb, shallow=False):
                malos.append(os.path.relpath(pa, a))
    for raiz, _ds, fs in os.walk(b):
        malos += [os.path.relpath(os.path.join(raiz, f), b) for f in fs
                  if not os.path.exists(os.path.join(a, os.path.relpath(os.path.join(raiz, f), b)))]
    return malos


def main():
    # 1. estados
    b2, b1 = ES.estados("b2"), ES.estados("b1")
    assert [e["grupo"] for e in b2] == list(range(9)), b2
    assert [e["grupo"] for e in b1] == list(range(13)), b1
    est = {(e["juego"], e["grupo"]): e for e in b2 + b1}
    assert {k for k, e in est.items() if e["estado"] == "no_funciona"} == {("b2", 1), ("b2", 8), ("b1", 9), ("b1", 11)}
    assert {k for k, e in est.items() if e["estado"] == "probado"} == {("b2", 2), ("b2", 7), ("b1", 3)}
    assert {g for (j, g), e in est.items() if j == "b2" and e.get("zona_b")} == {0, 4, 7, 8}
    assert all(e["es"] and e["en"] for e in est.values())
    assert all(len(ES.propuesto(e, i)) <= ES.LIMITE and ES.propuesto(e, i) for e in est.values() for i in (0, 1))
    assert ES.propuesto(est[("b2", 7)]) == "Montes nevados" and ES.propuesto(est[("b2", 1)], True) == "Tournament"
    temas = [a["tema"] for a in ES.ambientes()]
    assert sorted(temas) == list(range(17)) and any(a.get("principal") for a in ES.ambientes())
    print("OK estados: B2 %d, B1 %d, ambientes %d" % (len(b2), len(b1), len(temas)))

    # 2. entrada libre
    assert ES.entrada_libre(set(), "b2", 7) == 5007
    assert ES.entrada_libre(set(), "b1", 3) == 5023
    assert ES.entrada_libre(set(), "b2", 7, zona_b=True) == 5047
    assert ES.entrada_libre({5007, 5107}, "b2", 7) == 5000
    assert ES.entrada_libre({5007, 5000, 5001}, "b2", 7) == 5002
    assert ES.entrada_libre({5107}, "b2", 7) == 5000             # su casilla (+100) ocupada tambien cuenta
    assert ES.entrada_libre(set(range(5000, 5100)), "b2", 7) is None
    tmp = tempfile.mkdtemp(dir=TRABAJO, prefix="libre_")
    os.makedirs(os.path.join(tmp, "otro"))
    with open(os.path.join(tmp, "otro", "stage.toml"), "w", encoding="utf-8") as fh:
        fh.write("[[escenario]]\nnombre = \"X\"\nentrada = 5007\ntema = 16\n")
    os.makedirs(os.path.join(tmp, "tex", "us", "data_cmn.afs", "5000"))
    assert ES.ocupadas(tmp) == {5007, 5107, 5000}, ES.ocupadas(tmp)
    assert ES.entrada_libre(ES.ocupadas(tmp), "b2", 7) == 5001
    o = ES.ordenes("py", "-", PS2, tmp, "b2", 7, "Montañas Nieve", 16)
    assert len(o) == 2 and o[0][2][o[0][2].index("--entrada") + 1] == "5001"
    assert o[1][2][5] == "5001" and o[1][2][o[1][2].index("--entrada") + 1] == "5047"   # zona B -> apunta a 5001
    assert len(ES.ordenes("py", "-", PS2, tmp, "b2", 2, "Templo", 16)) == 1           # sin zona B
    print("OK entrada libre (preferida, ocupada por stage.toml y por carpeta, casilla +100, lleno)")

    # 3. validacion
    v = ES.validar
    assert v("Montañas Nieve", "b2", 7, 16, tmp) is None
    assert "nombre" in v("", "b2", 7, 16, tmp) and "nombre" in v("  ", "b2", 7, 16, tmp)
    assert "largo" in v("Montañas nevadas", "b2", 7, 16, tmp)                 # 16 > 15
    assert "ambiente" in v("Nieve", "b2", 7, 17, tmp) and "ambiente" in v("Nieve", "b2", 7, -1, tmp)
    assert "no funciona" in v("Estadio", "b2", 1, 16, tmp) and "no funciona" in v("X", "b1", 9, 16, tmp)
    assert "otro mod" in v("Nieve", "b2", 7, 16, tmp, entrada=5007)
    assert "otro mod" in v("Nieve", "b2", 7, 16, tmp, entrada=4999)
    assert "Ya tienes" in v("otro", "b2", 7, 16, tmp)
    assert "too long" in v("Montañas nevadas", "b2", 7, 16, tmp, ingles=True)
    assert ES.carpeta_de("Montañas Nieve") == "Montanas Nieve" and ES.carpeta_de("¡Ñu!") == "Nu"
    shutil.move(tmp, os.path.join(TRABAJO, "_viejos", os.path.basename(tmp)))
    print("OK validacion (nombre vacio/largo, ambiente fuera de 0..16, grupo que no funciona, entrada ocupada)")

    # 4. creacion real con el Kit (planes.ejecutar: lote deshacible) frente a la orden del conversor
    if not ES.iso_de("b2", PS2):
        print("SIN ISO de Budokai 2 en %s: %s" % (PS2, ES.falta_iso("b2", PS2)))
        return 1
    kit_mods, cli_mods = os.path.join(TRABAJO, "mods"), os.path.join(TRABAJO, "cli_mods")
    apartar(kit_mods)
    apartar(cli_mods)
    env = types.SimpleNamespace(mods=kit_mods, backups=os.path.join(TRABAJO, "backups"), cwd=lambda: MCH)
    ords = ES.ordenes(sys.executable, "-", PS2, kit_mods, "b2", 7, NOMBRE, 16)
    plan = planes.Plan("escenario", [(t, c) for t, _en, c, _d in ords], [d for *_x, d in ords])
    t0 = time.time()
    ok, texto, lote = planes.ejecutar(nucleo, env, plan, lambda t: None)
    assert ok, texto[-2000:]
    print("Kit: %d pasos en %.0f s; nuevos: %s" % (len(ords), time.time() - t0, sorted(lote.extra)))
    conv = os.path.join(MCH, "escenarios_convertir.py")
    for extra in (["--nombre", "Montanas Nieve", "--entrada", "5007"],
                  ["--zona", "b", "--nombre", "Montanas Nieve B", "--entrada", "5047"]):
        cmd = [sys.executable, conv, "mod", "7", "--rotulo", NOMBRE, "--mods", cli_mods, "--juego", "b2",
               "--tema", "16"] + extra
        r = subprocess.run(cmd, cwd=MCH, capture_output=True, text=True, encoding="utf-8", errors="replace")
        assert r.returncode == 0, r.stdout[-1500:] + r.stderr[-1500:]
    malos = iguales(kit_mods, cli_mods)
    assert not malos, malos
    n = sum(len(fs) for _r, _d, fs in os.walk(kit_mods))
    st = os.path.join(kit_mods, "Montanas Nieve", "stage.toml")
    txt = open(st, encoding="utf-8").read()
    assert "entrada = 5007" in txt and "menu = 5107" in txt and 'titulo = "Montañas Nieve"' in txt, txt
    assert "zona_b = 5007" in open(os.path.join(kit_mods, "Montanas Nieve B", "stage.toml"), encoding="utf-8").read()
    print("OK identico byte a byte al conversor: %d ficheros (stage.toml, geom.bin, transicion, manifest...)" % n)
    res = ES.resumen(os.path.join(kit_mods, "Montanas Nieve")) + ES.resumen(os.path.join(kit_mods, "Montanas Nieve B"))
    assert res[0] == "✓ 1 escenario nuevo: Montañas Nieve", res
    assert "segunda zona" in res[1], res
    assert ES.resumen(TRABAJO) == []
    print("OK resumen: %s" % " | ".join(res))
    print("mod de prueba: %s" % os.path.join(kit_mods, "Montanas Nieve"))
    print("TODO OK")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="backslashreplace")
    except (AttributeError, ValueError):
        pass
    sys.exit(main())

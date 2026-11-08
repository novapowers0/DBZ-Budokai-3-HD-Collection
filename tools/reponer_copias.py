r"""Repone a su valor por defecto dos cvars en las copias fijas de pruebas (D:\DBZ3HD\<copia>).

Cvars: dbz3_original_on y dbz3_iw_logica_original. Si la copia tiene esas lineas puestas a mano
(en cualquier seccion, tambien en OVERRIDES), se quitan para que el exe use su valor por defecto.

Uso:
  python tools\reponer_copias.py                  (= --simular: solo muestra la tabla, no cambia nada)
  python tools\reponer_copias.py --aplicar        (copia cada toml a D:\_PARA_BORRAR_CLAUDE\tomls_0611\<copia>\ y quita las lineas)
  python tools\reponer_copias.py --aplicar --forzar-uso   (incluye copias marcadas _en_uso_<copia>)
  python tools\reponer_copias.py --autoprueba     (comprueba la logica con ficheros temporales en D:\tmp\enjambre)

Nunca borra: el toml original queda copiado en la carpeta de respaldo antes de tocar nada.
"""
import argparse
import os
import re
import shutil
import sys
import time
from pathlib import Path

COPIAS = ["smoke_a", "smoke_b", "smoke_c", "smoke_d", "smoke_e", "smoke_f",
          "opus", "opus2", "eu_test2", "s5_i1_eu", "sol"]   # copias fijas usadas por tools/pruebas.py
BASE_DEF = Path(r"D:\DBZ3HD")
RESPALDO_DEF = Path(r"D:\_PARA_BORRAR_CLAUDE") / "tomls_0611"
RE_LINEA = re.compile(r"^\s*(dbz3_original_on|dbz3_iw_logica_original)\s*=(.*)$")


def leer(ruta):
    with open(ruta, encoding="utf-8", errors="surrogateescape", newline="") as f:
        return f.readlines()


def escribir(ruta, lineas):
    with open(ruta, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
        f.write("".join(lineas))


def hallar(lineas):
    """Devuelve [(linea_1_based, cvar, valor)] de las lineas puestas a mano."""
    out = []
    for i, l in enumerate(lineas, 1):
        m = RE_LINEA.match(l.rstrip("\r\n"))
        if m:
            out.append((i, m.group(1), m.group(2).strip()))
    return out


def en_uso(base, copia):
    # pruebas.py marca una copia en uso con la carpeta _en_uso_<copia> dentro de la base
    return (Path(base) / f"_en_uso_{copia}").exists()


def analizar(base, copias, forzar_uso):
    filas = []
    for c in copias:
        ruta = Path(base) / c / "dbz3_user.toml"
        f = {"copia": c, "ruta": ruta, "hallazgos": [], "omitida": False, "sin_archivo": False, "uso": False}
        if not ruta.exists():
            f["sin_archivo"] = True
        else:
            f["hallazgos"] = hallar(leer(ruta))
            f["uso"] = en_uso(base, c)
            f["omitida"] = f["uso"] and not forzar_uso
        filas.append(f)
    return filas


def imprimir(filas):
    cab = ("copia", "archivo", "linea", "valor", "nota")
    filas_txt = []
    for f in filas:
        nota = ""
        if f["uso"]:
            nota = "EN USO: omitida" if f["omitida"] else "EN USO: se toca (--forzar-uso)"
        if f["sin_archivo"]:
            filas_txt.append((f["copia"], "dbz3_user.toml", "-", "-", "sin dbz3_user.toml"))
        elif not f["hallazgos"]:
            filas_txt.append((f["copia"], "dbz3_user.toml", "-", "-", "ya por defecto" + (f" ({nota})" if nota else "")))
        else:
            for linea, cvar, valor in f["hallazgos"]:
                filas_txt.append((f["copia"], "dbz3_user.toml", str(linea), f"{cvar} = {valor}", nota))
    anchos = [max(len(str(r[i])) for r in [cab] + filas_txt) for i in range(len(cab))]
    for r in [cab] + filas_txt:
        print("  ".join(str(v).ljust(anchos[i]) for i, v in enumerate(r)).rstrip())


def aplicar(ruta, copia, respaldo, hallazgos):
    """Copia el toml al respaldo, quita las lineas y vuelve a leer para comprobarlo. Devuelve lineas quitadas."""
    lineas = leer(ruta)
    dest = Path(respaldo) / copia / "dbz3_user.toml"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():   # no pisar un respaldo anterior
        dest = dest.with_name(f"dbz3_user.{time.strftime('%Y%m%d_%H%M%S')}.toml")
    shutil.copy2(ruta, dest)
    if dest.read_bytes() != Path(ruta).read_bytes():
        raise SystemExit(f"el respaldo de {copia} no coincide con el original; no se toca nada")
    marcas = {i for i, _, _ in hallazgos}
    nuevas = [l for i, l in enumerate(lineas, 1) if i not in marcas]
    escribir(ruta, nuevas)
    releido = leer(ruta)
    if hallar(releido):
        raise SystemExit(f"{copia}: quedan lineas tras quitarlas")
    if releido != nuevas:
        raise SystemExit(f"{copia}: el fichero releido no coincide con lo escrito")
    return len(lineas) - len(nuevas)


def autoprueba():
    tmp = Path(r"D:\tmp\enjambre")
    tmp.mkdir(parents=True, exist_ok=True)
    raiz = Path(tempfile_dir(tmp))
    base, respaldo = raiz / "base", raiz / "respaldo"
    original_ca = (b"[general]\r\nwindow_width = 1280\r\ndbz3_original_on = true\r\naudio_mute = true\r\n\r\n"
                   b"[OVERRIDES]\r\ndbz3_iw_logica_original = false\r\nfullscreen = false\r\n")
    for nombre, datos in [("ca", original_ca), ("cb", b"dbz3_original_on = false\nx = 1\n"),
                          ("cc", b"[general]\naudio_mute = true\n")]:
        (base / nombre).mkdir(parents=True, exist_ok=True)
        (base / nombre / "dbz3_user.toml").write_bytes(datos)
    (base / "cb" / "dbz3_user.toml").write_bytes(b"dbz3_original_on = false\nx = 1\n")
    (base / "_en_uso_cb").mkdir()                       # cb marcada en uso
    copias = ["ca", "cb", "cc", "cd"]                   # cd no tiene toml

    f = {x["copia"]: x for x in analizar(base, copias, False)}
    assert [(l, c, v) for l, c, v in f["ca"]["hallazgos"]] == [
        (3, "dbz3_original_on", "true"), (7, "dbz3_iw_logica_original", "false")], f["ca"]["hallazgos"]
    assert f["cb"]["uso"] and f["cb"]["omitida"] and f["cb"]["hallazgos"], "cb deberia omitirse por uso"
    assert f["cc"]["hallazgos"] == [] and not f["cc"]["omitida"], "cc no tiene nada que tocar"
    assert f["cd"]["sin_archivo"], "cd no tiene toml"
    f2 = {x["copia"]: x for x in analizar(base, copias, True)}
    assert not f2["cb"]["omitida"], "--forzar-uso deberia incluir cb"

    quitadas = aplicar(base / "ca" / "dbz3_user.toml", "ca", respaldo, f["ca"]["hallazgos"])
    assert quitadas == 2, quitadas
    assert (respaldo / "ca" / "dbz3_user.toml").read_bytes() == original_ca, "respaldo distinto del original"
    nuevo = (base / "ca" / "dbz3_user.toml").read_bytes()
    assert b"dbz3_original_on" not in nuevo and b"dbz3_iw_logica_original" not in nuevo
    assert b"window_width = 1280\r\n" in nuevo and b"fullscreen = false\r\n" in nuevo, "se perdieron lineas ajenas"
    assert b"\r\n" in nuevo and b"\n" not in nuevo.replace(b"\r\n", b""), "fin de linea alterado"
    assert hallar(leer(base / "ca" / "dbz3_user.toml")) == [], "quedan hallazgos tras aplicar"
    assert analizar(base, ["ca"], False)[0]["hallazgos"] == [], "no es idempotente"

    # segunda pasada con copia de respaldo ya existente: no se pisa el respaldo anterior
    (base / "ca" / "dbz3_user.toml").write_bytes(b"dbz3_original_on = false\n")
    aplicar(base / "ca" / "dbz3_user.toml", "ca", respaldo, hallar(leer(base / "ca" / "dbz3_user.toml")))
    assert (respaldo / "ca" / "dbz3_user.toml").read_bytes() == original_ca, "se piso el respaldo anterior"
    assert len(list((respaldo / "ca").glob("dbz3_user*.toml"))) == 2, "falta el segundo respaldo"

    # copia con uso forzado: se toca
    assert aplicar(base / "cb" / "dbz3_user.toml", "cb", respaldo, f2["cb"]["hallazgos"]) == 1
    assert hallar(leer(base / "cb" / "dbz3_user.toml")) == []
    assert (base / "cc" / "dbz3_user.toml").read_bytes() == b"[general]\naudio_mute = true\n", "cc cambio"

    # imprimir no debe fallar
    imprimir(analizar(base, copias, False))
    print(f"AUTOPRUEBA OK (ficheros en {raiz})")
    return 0


def tempfile_dir(padre):
    import tempfile
    return tempfile.mkdtemp(prefix="reponer_copias_", dir=str(padre))


def main(argv=None):
    ap = argparse.ArgumentParser(description="Repone dbz3_original_on y dbz3_iw_logica_original en las copias fijas.")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--simular", action="store_true", help="solo muestra la tabla (por defecto)")
    g.add_argument("--aplicar", action="store_true", help="copia al respaldo y quita las lineas")
    ap.add_argument("--forzar-uso", action="store_true", help="incluye copias marcadas en uso")
    ap.add_argument("--autoprueba", action="store_true", help="comprueba la logica con ficheros temporales")
    ap.add_argument("--base", default=str(BASE_DEF), help=argparse.SUPPRESS)
    ap.add_argument("--respaldo", default=str(RESPALDO_DEF), help="carpeta de respaldo (por defecto D:\\_PARA_BORRAR_CLAUDE\\tomls_0611)")
    a = ap.parse_args(argv)

    if a.autoprueba:
        return autoprueba()

    filas = analizar(a.base, COPIAS, a.forzar_uso)
    imprimir(filas)
    if not a.aplicar:
        print("Simulacion: no se ha cambiado nada. Usa --aplicar para quitar las lineas.")
        return 0

    total = 0
    for f in filas:
        if not f["hallazgos"] or f["omitida"] or f["sin_archivo"]:
            continue
        n = aplicar(f["ruta"], f["copia"], a.respaldo, f["hallazgos"])
        total += n
        print(f"{f['copia']}: quitadas {n} lineas (respaldo en {a.respaldo}\\{f['copia']})")
    print(f"Hecho: {total} lineas quitadas en total.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

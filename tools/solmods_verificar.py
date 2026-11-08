"""Sonda acotada de formas/tecnicas, siempre mediante pruebas.py y sus copias."""
import argparse
import ctypes
import csv
from collections import Counter
import json
import os
from pathlib import Path

import pruebas as P


def medir(j, par):
    j.log(r"hub: combate nativo", 90)
    P._esperar_pelea(j, 120)
    datos = {}
    for i in sorted({0, 7, 23, 58, 59, 61, par["id"]}):
        datos[str(i)] = {}
        for nombre, addr, n in (("char372", 0x82329CF0 + 372 * i, 372),
                                ("char96", 0x8234ABB8 + 96 * i, 96)):
            raw = P.leer(j.h, P.GBASE + addr, n)
            datos[str(i)][nombre] = raw.hex() if raw else None
    mgr = int.from_bytes(P.leer(j.h, P.GBASE + 0x823D6CF4, 4), "big")
    x = mgr + 2096
    datos["luchador"] = {str(off): P.leer(j.h, P.GBASE + x + off, 4).hex()
                         for off in (2972, 2980, 7416, 7420)}
    (j.run / "formas.json").write_text(json.dumps(datos, indent=2), encoding="utf-8")
    if int.from_bytes(P.leer(j.h, P.GBASE + x + 2980, 4), "big") != par["forma"]:
        raise P.Fallo("el arnes no cargo la forma solicitada")
    if par.get("capsula_forma_probe") is not None or par.get("id_combate_probe") is not None:
        c372 = int.from_bytes(P.leer(j.h, P.GBASE + x + 7416, 4), "big")
        addr = c372 + 212 + 20 * par["forma"]
        h = P.K32.OpenProcess(0x0438, False, j.proc.pid)
        try:
            dato = par["capsula_forma_probe"].to_bytes(2, "big") if par.get("capsula_forma_probe") is not None else par["id_combate_probe"].to_bytes(4, "big")
            if par.get("id_combate_probe") is not None:
                addr = x + 7420
            n = ctypes.c_size_t()
            if not P.K32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(P.GBASE + P.DESPL + addr),
                                           dato, len(dato), ctypes.byref(n)):
                raise P.Fallo("no se pudo sondear la capsula de forma")
        finally:
            P.K32.CloseHandle(h)
        j.info("sonda: " + ("capsula de forma = " + str(par["capsula_forma_probe"]) if par.get("id_combate_probe") is None
                            else "ID de combate = " + str(par["id_combate_probe"])) + " (solo RAM de esta prueba)")
    if par.get("hiper") or par.get("rush"):
        j.tecla("L+P+Space+Quote", 2500)
    if par.get("rush"):
        # Entrada real: acercarse, P+K (HR de expulsion en el CSK nativo) y atajo Hyper.
        # El resultado se acredita por los codigos cinematicos, no por estas pulsaciones.
        j.tecla("D 900", 1100)
        j.tecla("L+P", 450)
        j.tecla("L+P+Space+Quote", 200)
    if par.get("capturas"):
        j.captura("forma_hiper")
    duracion = float(par.get("seg", "25"))
    muestras = []
    anterior = 0.0
    for segundo in par.get("capturas_en", []):
        if segundo < anterior or segundo > duracion:
            raise P.Fallo("tiempos de captura fuera de orden o duracion")
        P.dormir(segundo - anterior)
        anterior = segundo
        j.captura("tecnica_%gs" % segundo)
        muestras.append({"seg": segundo, "escenario": P.leer(j.h, P.GBASE + 0x823A3D80, 80).hex(),
                         "codigo": P.leer(j.h, P.GBASE + x + 2928, 4).hex()})
    P.dormir(duracion - anterior)
    (j.run / "escenario_muestras.json").write_text(json.dumps(muestras, indent=2), encoding="utf-8")
    if par.get("identity_probe"):
        h = P.K32.OpenProcess(0x0438, False, j.proc.pid)
        try:
            n = ctypes.c_size_t()
            dato = b"\0\0\0\0"
            if not P.K32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(P.GBASE + P.DESPL + x + 2972),
                                           dato, len(dato), ctypes.byref(n)):
                raise P.Fallo("no se pudo medir la identidad de forma en la copia")
        finally:
            P.K32.CloseHandle(h)
        j.info("sonda: X+2972 = 0 (solo memoria de esta prueba)")
        P.dormir(float(par.get("seg", "25")))
    with (j.dest / "logs" / "combate_sonda.csv").open(encoding="utf-8", newline="") as f:
        # El juego sigue abierto: el ultimo registro puede estar a medio escribir.
        filas = [r for r in csv.DictReader(f) if all(v is not None for v in r.values())]
    with (j.run / "combate_sonda.csv").open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=filas[0].keys() if filas else [])
        writer.writeheader()
        writer.writerows(filas)
    resumen = {"cuadros": len(filas), "codigos_por_forma": {
        forma: dict(Counter(r["1_codigo"] for r in filas if r["1_forma"] == forma))
        for forma in sorted({r["1_forma"] for r in filas})},
        "hiper": dict(Counter(r["1_hiper_estado"] + "/" + r["1_hiper"] for r in filas)),
        "ki_inicio_fin": [filas[0]["1_ki"], filas[-1]["1_ki"]] if filas else [],
        "vida_rival_inicio_fin": [filas[0]["2_vida"], filas[-1]["2_vida"]] if filas else []}
    (j.run / "tecnicas.json").write_text(json.dumps(resumen, indent=2), encoding="utf-8")
    j.info("tecnicas: " + json.dumps(resumen))
    if par.get("espera") and not any(r["1_codigo"] == par["espera"] for r in filas):
        raise P.Fallo("no se ejecuto la tecnica esperada " + par["espera"])
    if par.get("max_forma") is not None and any(int(r["1_forma"]) > par["max_forma"] for r in filas):
        raise P.Fallo("la transformacion excedio la ultima forma del mod")
    if par.get("espera_hiper") and not any(r["1_hiper"] == "1" for r in filas):
        raise P.Fallo("el personaje no llego a activar el modo hiper")
    if par.get("espera_dano") is not None:
        dano = float(filas[0]["2_vida"]) - min(float(r["2_vida"]) for r in filas) if filas else 0
        if dano < par["espera_dano"]:
            raise P.Fallo("la tecnica no hizo el dano minimo esperado: %g < %g" % (dano, par["espera_dano"]))
    if par.get("rush") and not any(0x400 <= int(r["1_codigo"], 16) < 0x440 for r in filas):
        raise P.Fallo("las pulsaciones no iniciaron un Dragon Rush real (0x400..0x43F)")
    if par.get("espera_rush_fin"):
        cambios = [r["1_codigo"] for i, r in enumerate(filas)
                   if i == 0 or r["1_codigo"] != filas[i - 1]["1_codigo"]]
        if cambios.count("400") != 1 or "40B" not in cambios:
            raise P.Fallo("Dragon Rush no completo una sola cadena (reinicio o cadena incompleta)")
        fin = cambios.index("40B")
        if not any(c in ("000", "100") for c in cambios[fin + 1:]):
            raise P.Fallo("Dragon Rush no regreso al combate tras el remate")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--copia", default="sub66")
    ap.add_argument("--id", default="23")
    ap.add_argument("--forma", type=int, default=0)
    ap.add_argument("--escenario", type=int, default=2)
    ap.add_argument("--vida-rival", type=int, default=0)
    ap.add_argument("--seg", default="25")
    ap.add_argument("--identity-probe", action="store_true")
    ap.add_argument("--capsula-forma-probe", type=int)
    ap.add_argument("--id-combate-probe", type=int)
    ap.add_argument("--transformar", action="store_true")
    ap.add_argument("--espera", default="")
    ap.add_argument("--espera-hiper", action="store_true")
    ap.add_argument("--espera-dano", type=float, help="descenso minimo medido de vida del rival")
    ap.add_argument("--espera-rush-fin", action="store_true")
    ap.add_argument("--max-forma", type=int)
    ap.add_argument("--modo", choices=("esp", "ult", "hiper", "rush"), default="esp")
    ap.add_argument("--caps", default="")
    ap.add_argument("--capturas", action="store_true")
    ap.add_argument("--capturas-en", default="", help="segundos separados por comas; capturas a media escala")
    a = ap.parse_args()
    os.environ["DBZ3_AGENTE"] = "solMODS"
    # La prueba mide datos; no necesita fotos ni la captura automatica final.
    if not a.capturas and not a.capturas_en:
        P.Juego.captura = lambda *args, **kw: None
    else:
        captura_original = P.Juego.captura

        def captura_media(j, *args, **kw):
            ruta = captura_original(j, *args, **kw)
            if P.Image and ruta:
                with P.Image.open(ruta) as im:
                    im.resize((im.width // 2, im.height // 2)).save(ruta)
            return ruta

        P.Juego.captura = captura_media
    P.OVERRIDES.update(dbz3_combate_prueba_muneco='"quieto"',
                       dbz3_combate_definitiva_directa="false")
    P.ESCENARIOS["solmods_formas"] = (
        "solMODS: registros de formas y especial E+direccion", lambda p: {
            "DBZ3_TEST_BATTLE": f"{a.id},7,{a.escenario},0,0,{a.vida_rival},{a.forma},0", "DBZ3_COMBATE_SONDA": "1",
            **({"DBZ3_TEST_ESP": "9"} if a.modo == "esp" else {"DBZ3_TEST_ULT": "1"} if a.modo == "ult" else {}),
            **({"DBZ3_TEST_CAPS": a.caps} if a.caps else {}), "DBZ3_COMBATE_MUNECO": "quieto",
            "DBZ3_TEST_OCULTO": "0" if a.capturas or a.capturas_en else "1",
            **({"DBZ3_TEST_FORMAS": "1", "DBZ3_TEST_PKG": "1"} if a.transformar else {})}, medir)
    r = P.correr(a.copia, "solmods_formas", par={"seg": a.seg, "identity_probe": a.identity_probe,
                                                "capsula_forma_probe": a.capsula_forma_probe,
                                                "id_combate_probe": a.id_combate_probe,
                                                 "espera": a.espera, "espera_hiper": a.espera_hiper, "id": int(a.id),
                                                 "espera_dano": a.espera_dano,
                                                "espera_rush_fin": a.espera_rush_fin,
                                                "max_forma": a.max_forma, "forma": a.forma,
                                                "hiper": a.modo == "hiper", "rush": a.modo == "rush",
                                                "capturas": a.capturas,
                                                "capturas_en": [float(s) for s in a.capturas_en.split(",") if s]}, forzar=True)
    print(json.dumps({k: str(v) for k, v in r.items()}))
    raise SystemExit(0 if r["ok"] else 1)


if __name__ == "__main__":
    main()

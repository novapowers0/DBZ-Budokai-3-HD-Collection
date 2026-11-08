r"""dif_camaras_ranking.py (P2, 7/11) - veredicto IGUAL/DISTINTO de la camara de cada minijuego IW, con numeros.

  python tools\dif_camaras_ranking.py [--salida studies 5\ENTREGAS\noche]

Toma, por minijuego EU, el ultimo resultado de tools\dorados\resultados\iw_<clave>_*.json (filas 'camara tramo ...' de
diferencial.comparar_camara_iw; si el dorado no trae ojo medido, las filas de mediana de lector_camara) y, para USA, compara el
dorado USA contra el dorado EU con la misma metrica (iw_usa_<clave>.json). Escribe CAMARAS_IW.json / .md en --salida y
anade la seccion al DIFERENCIAS_RANKING.md/.json."""
import json
import re
import sys
from pathlib import Path

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI))
import diferencial as D  # noqa: E402

RES = AQUI / "dorados" / "resultados"
RAIZ = AQUI.parent
CLAVES = [f"0x{n:X}" for n in range(0x7D0, 0x7E2)]
FAMILIA = {"0x7D3": "disparo (camara fija medida)", "0x7D6": "vuelo (camara fija CameraFocusWhileFlying)", "0x7E0": "vuelo (camara fija CameraFocusWhileFlying)"}


def ultimo(clave):
    c = sorted(RES.glob(f"iw_{clave}_*.json"), key=lambda p: p.stat().st_mtime)
    return (c[-1], json.loads(c[-1].read_text(encoding="utf-8"))) if c else (None, None)


def veredicto_eu(clave):
    p, d = ultimo(clave)
    if not d:
        return dict(clave=clave, veredicto="SIN DATO", numero="sin resultado del port", fuente="-")
    tr = [f for f in d["filas"] if f["nombre"].startswith("camara tramo") and f["ok"] is not None]   # los de presentacion (ok None) no cuentan
    if tr:
        nums = []
        for f in tr:
            m = re.search(r"original dist (\S+) alt (\S+) giro (\S+); port mas cercano \(clip (\S+?), (\d+) muestras\) dist (\S+) alt (\S+) giro (\S+); dif dist (\S+) alt (\S+) giro (\S+)", f["detalle"])
            ok = f["ok"]
            if m:
                nums.append(f"{f['nombre'].split(' (')[0].replace('camara ', '')}: orig dist {m[1]} alt {m[2]} / port dist {m[6]} alt {m[7]} (dif {m[9]}, {m[10]}; giro {m[11]}{'' if 'NO comparable' not in f['detalle'] else ' no cuenta'}) {'IGUAL' if ok else 'DISTINTO'}")
            else:
                nums.append(f"{f['nombre']}: {f['detalle'][:120]} {'IGUAL' if ok else 'DISTINTO'}")
        return dict(clave=clave, veredicto="IGUAL" if all(f["ok"] for f in tr) else "DISTINTO", numero=" | ".join(nums), fuente=p.name, familia=FAMILIA.get(clave, ""))
    med = [f for f in d["filas"] if f["nombre"].startswith(("dist_ojo", "altura_ojo", "yaw del ojo")) and f["ok"] is not None]
    if med:
        return dict(clave=clave, veredicto="IGUAL" if all(f["ok"] for f in med) else "DISTINTO",
                    numero=" | ".join(f"{f['nombre'].split(' (')[0]}: {f['detalle'].split(' (')[0]}" for f in med) + " (medianas; el dorado es una camara fija sin tramos)",
                    fuente=p.name, familia=FAMILIA.get(clave, ""))
    return dict(clave=clave, veredicto="SIN DATO", numero="; ".join(f["detalle"][:100] for f in d["filas"] if f["nombre"].startswith(("camara", "serie"))) or "el dorado no trae ojo", fuente=p.name)


def veredicto_usa(clave):
    e, u = AQUI / "dorados" / f"iw_{clave}.json", AQUI / "dorados" / f"iw_usa_{clave}.json"
    if not u.exists():
        return dict(clave=clave, veredicto="SIN DATO", numero="sin dorado USA" + (" (camara fija: mismo codigo que EU)" if clave in FAMILIA else ""))
    oe, ou = (json.loads(x.read_text(encoding="utf-8")) for x in (e, u))
    so = oe["muestras"]
    sp = [dict(m, clip=f"amc{m.get('amc', 0)}") for m in ou["muestras"]]   # tramo = racha de #AMC; mismo espacio del mundo IW
    # los puntos de USA se tratan como 'port' sin clip; el giro solo cuenta si los dos traen objetivo
    r = [x for x in D.comparar_camara_iw(so, sp) if x[1] is not None] or [("camara", None, "sin tramo de juego medible")]
    if len(r) == 1 and r[0][1] is None:
        return dict(clave=clave, veredicto="SIN DATO", numero=r[0][2])
    return dict(clave=clave, veredicto="IGUAL" if all(x[1] for x in r) else "DISTINTO",
                numero=" | ".join(f"{x[0].split(' (')[0]}: {x[2].split('; port')[0]} -> {'IGUAL' if x[1] else 'DISTINTO'}" for x in r))


def main(a):
    sal = Path(a[a.index("--salida") + 1]) if "--salida" in a else RAIZ / "studies 5" / "ENTREGAS" / "noche"
    eu = [veredicto_eu(c) for c in CLAVES]
    usa = [veredicto_usa(c) for c in CLAVES]
    (sal / "CAMARAS_IW.json").write_text(json.dumps({"eu_port_vs_original": eu, "usa_original_vs_eu_original": usa}, indent=1, ensure_ascii=False), encoding="utf-8")
    L = ["# Camara de los minijuegos de IW: port contra original (P2, 7/11)", "",
         "Metrica: camara relativa al jugador y por tramo de #AMC (mediana por tramo; del dorado se compara el tramo mas largo y los siguientes), "
         "giro respecto a jugador->objetivo cuando ambos lo traen o el port esta en el espacio del clip; tolerancia 15 % en distancia/altura (minimo 1) y 15 grados. "
         "Si el jugador del port no esta donde el del original, los ojos se comparan en absoluto. Una fila es DISTINTO si algun tramo del original no tiene "
         "equivalente en el port.", "", "## EU: port (exe actual) contra dorado del original", "", "| minijuego | veredicto | numeros (original vs port) | resultado |", "|---|---|---|---|"]
    L += [f"| iw:{r['clave']} {('(' + r['familia'] + ')') if r.get('familia') else ''} | **{r['veredicto']}** | {r['numero'].replace('|', '/')} | {r.get('fuente', '')} |" for r in eu]
    L += ["", "## USA: original USA contra original EU (no es un fallo del port)", "", "| minijuego | veredicto | numeros (EU vs USA) |", "|---|---|---|"]
    L += [f"| iwusa:{r['clave']} | {r['veredicto']} | {r['numero'].replace('|', '/')} |" for r in usa]
    texto = "\n".join(L) + "\n"
    (sal / "CAMARAS_IW.md").write_text(texto, encoding="utf-8")
    # engancha la seccion en el ranking (sustituye una anterior)
    md = sal / "DIFERENCIAS_RANKING.md"
    if md.exists():
        t = md.read_text(encoding="utf-8")
        t = re.split(r"\n# Camara de los minijuegos de IW", t)[0].rstrip("\n") + "\n\n" + texto
        md.write_text(t, encoding="utf-8")
    js = sal / "DIFERENCIAS_RANKING.json"
    if js.exists():
        d = json.loads(js.read_text(encoding="utf-8"))
        d["camaras_iw"] = {"eu_port_vs_original": eu, "usa_original_vs_eu_original": usa}
        js.write_text(json.dumps(d, indent=1, ensure_ascii=False), encoding="utf-8")
    print("DISTINTO EU:", [r["clave"] for r in eu if r["veredicto"] == "DISTINTO"])
    print("IGUAL EU:", [r["clave"] for r in eu if r["veredicto"] == "IGUAL"])
    print("SIN DATO EU:", [r["clave"] for r in eu if r["veredicto"] == "SIN DATO"])


if __name__ == "__main__":
    main(sys.argv[1:])

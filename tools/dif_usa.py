r"""dif_usa.py (N4b) - filas IW USA: dorado de la ISO USA contra el dorado de la ISO EU (mismos numeros que usa comparar_modo).

  python tools\dif_usa.py [--autoprueba]

Lee tools\dorados\iw_usa_0x7NN.json (dif_grabar_pool.py --usa) y iw_0x7NN.json (EU); escribe tools\dorados\resultados\iwusa_0x7NN_*.json
con modo "iwusa". El port no se ejecuta aqui: si el original USA es igual que el EU, el resultado del port EU vale tambien para USA."""
import json, sys, time
from pathlib import Path
AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI))
import lector_camara as LC

def comparar(eu, usa, muestras_eu=None, muestras_usa=None):
    """filas de comparar_resumen con EU como 'original' y USA como 'port' (+ duracion). Con las muestras de los dos dorados, la camara
    se compara con diferencial.comparar_camara_iw (relativa al jugador, tramo de juego) en lugar de las medianas de toda la captura."""
    f = [{"nombre": n, "ok": ok, "detalle": d} for n, ok, d in LC.comparar_resumen(eu, usa) if ok is not None]   # los [H] del port no aplican
    sal = eu.get("salida"), usa.get("salida")
    if any(s == [-15.0, 0.0, 0.0] for s in sal):                       # valor por defecto previo a la partida, no una salida
        f = [x for x in f if x["nombre"] != "salida del jugador"]
    if muestras_eu and muestras_usa:
        import diferencial as DIF
        f = [x for x in f if not x["nombre"].startswith(("dist_ojo", "altura_ojo", "yaw del ojo"))]
        f += [{"nombre": n, "ok": ok, "detalle": d} for n, ok, d in DIF.comparar_camara_iw(muestras_eu, [dict(m, clip=f"amc{m.get('amc', 0)}") for m in muestras_usa]) if ok is not None]
    return f

def main():
    n = 0
    for k in range(0x7D0, 0x7E2):
        c = f"0x{k:X}"
        e, u = AQUI / "dorados" / f"iw_{c}.json", AQUI / "dorados" / f"iw_usa_{c}.json"
        if not (e.exists() and u.exists()):
            continue
        je, ju = (json.loads(p.read_text(encoding="utf-8")) for p in (e, u))
        re_, ru = je["resumen"], ju["resumen"]
        res = {"modo": "iwusa", "escena": c, "dorado": e.name, "fuente_dorado": u.name, "filas": comparar(re_, ru, je.get("muestras"), ju.get("muestras")),
               "exe": "no aplica (originales)", "region": "dorado ISO IW USA vs dorado ISO IW EU"}
        (AQUI / "dorados" / "resultados" / f"iwusa_{c}_{time.strftime('%Y%m%d-%H%M%S')}.json").write_text(
            json.dumps(res, indent=1, ensure_ascii=False), encoding="utf-8")
        n += 1
    print("filas USA escritas:", n)

if __name__ == "__main__":
    if "--autoprueba" in sys.argv:
        r = {"n_vivas": 5, "dist_ojo_jugador": {"mediana": 10.0}, "yaw_ojo_grados": {"mediana": 0.0}}
        assert all(x["ok"] for x in comparar(r, dict(r)) if x["ok"] is not None)
        assert any(x["ok"] is False for x in comparar(r, {"n_vivas": 5, "dist_ojo_jugador": {"mediana": 30.0}}))
        print("dif_usa autoprueba OK")
    else:
        main()

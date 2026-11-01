"""Una partida de nieve: conservar ki en los limites exactos de la escena.

Los muestreos externos llegan despues de reanudarse la pelea. Para comparar
ki se usan las medidas del guest en inicio/cambio/terminada, no las fotos.
"""
import json
import re

import solesc_port


def main():
    run = solesc_port.main(nombre="solESC2_nieve_ki")
    log = (run / "dbz3.log").read_text(encoding="utf-8")
    patron = (r"medida zona (inicio|cambio|terminada) cuadro=(\d+) guest=(\d+) "
              r"zona=(\d+) lado=(\d+).*?vida=([\d.]+) ki=([\d.]+)")
    filas = [dict(fase=m[0], cuadro=int(m[1]), guest=int(m[2]), zona=int(m[3]),
                  lado=int(m[4]), vida=float(m[5]), ki=float(m[6]))
             for m in re.findall(patron, log)]
    fallos = []
    for lado in range(2):
        fases = {f["fase"]: f for f in filas if f["lado"] == lado}
        if set(fases) != {"inicio", "cambio", "terminada"}:
            fallos.append(f"lado {lado}: faltan medidas exactas")
            continue
        inicial = fases["inicio"]
        for fase in ("cambio", "terminada"):
            if fases[fase]["ki"] != inicial["ki"] or fases[fase]["vida"] != inicial["vida"]:
                fallos.append(f"lado {lado}: ki/vida no conservados en {fase}")
        if fases["cambio"]["cuadro"] != 420 or fases["terminada"]["cuadro"] != 690:
            fallos.append(f"lado {lado}: reloj distinto de 420/690")
    resultado = dict(aceptacion_completa=False, medidas=filas, fallos=fallos)
    (run / "ki_verificado.json").write_text(json.dumps(resultado, indent=2), encoding="utf-8")
    print(json.dumps(resultado, ensure_ascii=False), flush=True)
    if fallos:
        raise RuntimeError("; ".join(fallos))
    print("PASS ki/vida y reloj; NO acredita disparador ni cinematica B2", flush=True)


if __name__ == "__main__":
    main()

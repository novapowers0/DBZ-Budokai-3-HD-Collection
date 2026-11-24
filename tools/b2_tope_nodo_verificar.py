"""Valida ocupacion b2-state-v1; exige estados y evidencia de recolocacion."""
import argparse
import json
from pathlib import Path
import re


def comprobar(estados, texto):
    fallos = []
    activos = 0
    for fila in estados:
        if fila.get("formato") != "b2-state-v1":
            fallos.append("Formato de estado no reconocido")
            continue
        grupos = {}
        for p in fila.get("piezas", []):
            if not p.get("en_juego"):
                continue
            activos += 1
            # El port distingue el lado rival por el bit 2, NO por la veracidad del bando:
            # el jugador vale 4, el rival 2; ambos son numeros distintos de cero.
            k = (p["casilla"], bool(p["bando"] & 2))
            grupos.setdefault(k, []).append(p)
        for k, piezas in grupos.items():
            huecos = [p.get("slot") for p in piezas]
            if (k[0] <= 0 or len(piezas) > 2
                    or any(type(h) is not int or h not in (0, 1) for h in huecos)
                    or len(set(huecos)) != len(huecos)):
                fallos.append(f"evento {fila.get('evento')} {fila.get('hito')}: casilla/lado {k} -> {huecos}")
    if not estados or not activos:
        fallos.append("Sin estados activos: la prueba no acredita ocupacion")
    fallos.extend(re.findall(r"[^\n]*TOPE_NODO INVARIANTE ROTA[^\n]*", texto))
    recolocaciones = re.findall(r"vuelta de combate: .* recolocado de \d+ a \d+ \(tope de 2 por nodo\)", texto)
    if not recolocaciones:
        fallos.append("Sin recolocacion observada: falta ejercitar el destino lleno")
    if recolocaciones and not any(f.get("hito") == "vuelta_tope_nodo" for f in estados):
        fallos.append("Recolocacion sin estado posterior de vuelta")
    return fallos, len(recolocaciones)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("state", type=Path)
    p.add_argument("log", type=Path)
    a = p.parse_args()
    try:
        estados = [json.loads(x) for x in a.state.read_text(encoding="utf-8-sig").splitlines() if x.strip()]
        texto = a.log.read_text(encoding="utf-8-sig", errors="replace")
        fallos, recolocaciones = comprobar(estados, texto)
    except (OSError, ValueError, KeyError, TypeError) as e:
        p.exit(2, f"TOPE_NODO: evidencia ilegible: {e}\n")
    print(f"TOPE_NODO: {len(estados)} estados, {recolocaciones} recolocaciones, {len(fallos)} fallos")
    for fallo in fallos[:20]:
        print("  " + fallo)
    return 1 if fallos else 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Genera los .toml de la bateria B2 (9 fases x 5 desenlaces). Solo escribe ficheros de datos.

Los escenarios ya curados de `studies 5/ENTREGAS/R21/INTEGRACION/escenarios/` no se tocan:
la salida por defecto es una carpeta aparte y nunca se pisa un fichero existente salvo
`--forzar`. No lee la ISO ni el juego: solo compone texto de escenario.

Uso:
    python tools\\gen_pruebas_b2.py
    python tools\\gen_pruebas_b2.py --salida "ruta\\carpeta" --forzar
"""
import argparse
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[1]
SALIDA_DEF = RAIZ / "studies 5" / "ENTREGAS" / "R21" / "escenarios_b2"
# fase -> (desde, nº de aliados del guion 301, guion de cierre, orden de cierre)
FASES = {
    1: (0, 1, 303, 41), 2: (1, 1, 303, 41), 3: (2, 1, 304, 41),
    4: (3, 2, 306, 41), 5: (4, 2, 307, 41), 6: (5, 1, 307, 41),
    7: (6, 1, 304, 41), 8: (7, 1, 308, 41), 9: (8, 2, 303, 42),
}
DESENLACES = ("base", "victoria", "derrota", "defensiva", "bolas")


def aliados(n):
    if n == 1:
        return ['  "tecla Space 2500"', '  \'log "tablero b2: aliado elegido" 60\'']
    # Con 2 aliados hay que mover el cursor antes del segundo A (dsC1, :9517-9518).
    return [
        '  "tecla Space 2500"',
        '  \'hasta_log "tablero b2: aliado marcado: .* quedan 1" 60\'',
        '  "tecla S 450"',
        '  "tecla Space 2500"',
        '  "tecla W 400"',
        '  \'log "tablero b2: aliado elegido" 60\'',
    ]


def desenlace(kind):
    if kind == "victoria":
        return ['  "tecla Space 1500"', '  "repetir 14"', '    "tecla Backspace 2600"',
                '    "tecla Backspace 2600"', '  "fin"']
    if kind == "derrota":
        return ['  "tecla Space 1500"', '  "repetir 16"', '    "tecla Backspace 2600"',
                '    "tecla Backspace 2600"', '  "fin"']
    if kind == "defensiva":
        # B -> combate: dos Backspace por turno, SIN mover ficha propia.
        return ['  "repetir 22"', '    "tecla Backspace 2600"',
                '    "tecla Backspace 2600"', '  "fin"']
    return ['  "tecla Space 1500"', '  "repetir 10"', '    "tecla Backspace 2600"',
            '    "tecla Backspace 2600"', '  "fin"']


def toml(fase, kind):
    desde, nal, guion, orden = FASES[fase]
    env = [
        "'DBZ3_PS2_GAMES=D:\\DBZ3HD\\ps2_games'",
        '"DBZ3_TEST_MISIONES=1,auto,reset,desde=%d"' % desde,
        '"DBZ3_TEST_TURBO=1"',
    ]
    if kind == "victoria" or kind == "defensiva" or kind == "bolas":
        env.append('"DBZ3_TEST_RINGOUT=1"')
    if kind == "derrota":
        env.append('"DBZ3_TEST_RINGOUT=2"')
    if kind == "bolas":
        env.append('"DBZ3_TEST_B2_BOLAS=127"')
    pasos = [
        '  \'log "tablero b2: fase %d leida" 240\'' % fase,
        '  "esperar 2500"',
        '  \'hasta_log "tablero b2: script [0-9]+: eleccion de [0-9] aliado" 150\'',
    ]
    pasos += aliados(nal)
    pasos += [
        '  \'log "tablero b2: guiones terminados" 240\'',
        '  "esperar 1500"',
        '  \'log "tablero b2: Bola [0-9]+ en la casilla" 60\'',
        '  "captura 01_tablero"',
    ]
    pasos += desenlace(kind)
    pasos += [
        '  \'log "tablero b2: (combate: |contacto )" 120\'',
        '  "captura 02_contacto"',
        '  \'log "resultado del suceso [0-9]+: codigo [0-9]+" 480\'',
        '  "captura 03_resultado"',
        '  \'log "tablero b2: script [0-9]+: orden (%d|42), fase [0-9]+ (completada|perdida)" 240\'' % orden,
        '  "captura 04_cierre"',
    ]
    head = ("# Generado por tools/gen_pruebas_b2.py. No editar a mano.\n"
            'descripcion = "B2 fase %d, desenlace %s (cierre %d -> orden %d)"\n'
            % (fase, kind, guion, orden)
            + "env = [\n  " + ",\n  ".join(env) + ",\n]\n")
    return head + "pasos = [\n" + ",\n".join(pasos) + ",\n]\n"


def main(argv):
    ap = argparse.ArgumentParser(description="Genera los 45 escenarios de regresion de B2 (9 fases x 5 desenlaces).")
    ap.add_argument("--salida", default=str(SALIDA_DEF), help="carpeta de destino (por defecto escenarios_b2)")
    ap.add_argument("--forzar", action="store_true", help="sobrescribe los .toml que ya existan")
    a = ap.parse_args(argv)
    salida = Path(a.salida)
    salida.mkdir(parents=True, exist_ok=True)
    escritos = saltados = 0
    for fase in FASES:
        for kind in DESENLACES:
            ruta = salida / ("b2_f%d_%s.toml" % (fase, kind))
            if ruta.is_file() and not a.forzar:
                saltados += 1
                continue
            ruta.write_text(toml(fase, kind), encoding="utf-8")
            escritos += 1
    print("escenarios B2: %d escritos, %d ya existian en %s" % (escritos, saltados, salida))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

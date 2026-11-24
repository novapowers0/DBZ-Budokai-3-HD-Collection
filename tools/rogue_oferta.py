# Dragon Rogue: que cartas ofrece cada piso con la semilla fija de pruebas (DBZ3_TEST_ROGUE), para escribir los
# escenarios de docs/14_frentes/escenarios_pruebas/modos_rogue*.toml. Misma mezcla que src/rogue.cpp (Hash + Oferta).
# Uso: python tools/rogue_oferta.py [decisiones...]   una por piso: 0|1|2 = carta de esa posicion, s = saltar,
#      con "c" delante = gastar antes un cambio (c1). Sin decisiones ensena la oferta del piso 1.
import os
import sys
import tomllib
from pathlib import Path

R = Path(__file__).resolve().parent.parent
M = 0xFFFFFFFF


def h(a, b):
    x = ((a * 0x9E3779B1) & M) ^ ((b * 0x85EBCA6B + 0x7F4A7C15) & M)
    x ^= x >> 15
    x = (x * 0x2C1B3C6D) & M
    x ^= x >> 12
    x = (x * 0x297A2D39) & M
    return x ^ (x >> 15)


CADA = int(os.environ.get("ROGUE_JEFE", 5))  # = DBZ3_TEST_ROGUE_JEFE del guion (jefe cada N combates)
TRANS, FUSION = 5, True  # ROG-04: el luchador de los guiones es Goku (id 0): 5 transformaciones y ficha de fusion (72)
DIAS = ("entrenar", "explorar", "descansar")  # ROG-05: solo en la pausa de Simulacion


def evolucion_vale(c, n, d, tras_jefe):
    paso = max([d[k]["valor"] for k in range(len(d)) if n[k] and d[k]["efecto"] == "forma"] + [0])
    mitad = any(n[k] and d[k]["efecto"] == "mitad" for k in range(len(d)))
    fusion = any(n[k] and d[k]["efecto"] == "fusion" for k in range(len(d)))
    if not tras_jefe:
        return False
    if c["efecto"] == "forma":
        return not fusion and paso < c["valor"] <= TRANS
    if c["efecto"] == "mitad":
        return FUSION and not fusion and not mitad
    return FUSION and not fusion and mitad


def oferta(d, n, piso, var, semilla=1, cada=5):
    caps = sum(n[k] for k in range(len(d)) if d[k]["efecto"] == "capsula")
    tras = cada > 0 and piso % cada == 0
    v = [k for k in range(len(d)) if n[k] < d[k]["max"] and (d[k]["efecto"] != "capsula" or caps < 7)
         and d[k]["efecto"] not in DIAS
         and (d[k]["efecto"] not in ("forma", "mitad", "fusion") or evolucion_vale(d[k], n, d, tras))]
    if tras:  # tras un jefe (cada 5): solo raras y legendarias si quedan 3
        raras = [k for k in v if d[k].get("estrellas", 1) >= 2]
        if len(raras) >= 3:
            v = raras
    s = (semilla + piso) & M
    for i in range(len(v) - 1, 0, -1):
        j = h(s, i) % (i + 1)
        v[i], v[j] = v[j], v[i]
    return [v[(3 * var + k) % len(v)] for k in range(min(3, len(v)))]


def main():
    d = tomllib.load(open(R / "res" / "mejoras" / "rogue_cartas.toml", "rb"))["carta"]
    d = [c for c in d if not c.get("precio") and not c.get("hito_jefes")]   # ROG-06: cerradas en pruebas
    lib = {m["id"] for m in tomllib.load(open(R / "res" / "mejoras" / "mejoras.toml", "rb"))["mejora"]}
    assert all(c["id"] in lib for c in d) and len({c["id"] for c in d}) == len(d), "ids repetidos o fuera de mejoras.toml"
    n = [0] * len(d)
    piso = 1
    for dec in sys.argv[1:] + [None]:
        var = 0
        o = oferta(d, n, piso, var, cada=CADA)
        print(f"piso {piso}:", [d[k]["id"] for k in o])
        if dec is None:
            break
        if dec.startswith("c"):
            var, dec = 1, dec[1:]
            o = oferta(d, n, piso, var, cada=CADA)
            print("  cambio:", [d[k]["id"] for k in o])
        if dec == "s":
            print("  salta")
        else:
            k = o[int(dec)]
            n[k] += 1
            print(f"  elige {d[k]['id']} ({d[k]['efecto']} {d[k]['valor']}, {n[k]} de {d[k]['max']})")
        piso += 1


if __name__ == "__main__":
    main()

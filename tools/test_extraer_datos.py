"""Prueba N0 de los extractores (X0c): los .toml de res/datos contra valores medidos a mano en studies 5-8.
  python tools/test_extraer_datos.py        (genera los .toml si faltan)"""
import sys
import tomllib
from pathlib import Path

R = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import extraer_datos as E  # noqa: E402

E.main(["todo"])
d = lambda n: tomllib.loads((R / "res" / "datos" / n).read_text(encoding="utf-8"))  # noqa: E731

# 1) cinco ordenes del tablero medidas a mano (src/pasarela_b2.cpp:1149: ELF 0x293CE0/CA0/C60/C20/BE0 = ordenes 58-62)
o = d("b2_ordenes.toml")["orden"]
for k, f in {58: 0x293CE0, 59: 0x293CA0, 60: 0x293C60, 61: 0x293C20, 62: 0x293BE0}.items():
    assert int(o[str(k)]["funcion"], 16) == f, (k, o[str(k)])
assert d("b2_ordenes.toml")["entradas"] == len(o) == 86

# 2) sonidos medidos con PCSX2 (medir_sonidos_b2.py: 74-79 llaman FUN_00147260(tipo,0,id,0); el port loguea 76 = tipo 4)
s = d("b2_sonidos.toml")["sonido"]
assert [s[str(k)]["tipo"] for k in range(74, 80)] == [0, 3, 4, 0, 3, 4]
assert [s[str(k)]["repite"] for k in range(74, 80)] == [False] * 3 + [True] * 3
assert all(s[str(k)]["id_en"] == "arg+0x34" for k in range(74, 80))

# 3) minijuegos de IW: clave <-> indice (forzar_mini_iw.py) y los #AMC de NB013 (INV_IW_MINIJUEGOS 2.3)
m = d("iw_minijuegos.toml")["minijuego"]
for idx, clave in {5: "0x7D4", 6: "0x7D5", 12: "0x7DB", 13: "0x7DC", 18: "0x7E1"}.items():
    assert m[str(idx)]["clave"] == clave, (idx, m[str(idx)])
assert m["12"]["amc_915_clips"][:4] == ["0x186", "0x187", "0x190", "0x191"] and m["12"]["amc_entrada"] == 876

# 4) satan: tiempos de la previa (AUDITORIA_MODOS 4.5)
t = d("satan_tiempos.toml")
assert t["previa"]["cell"] == 5.80 and t["previa"]["goku_abajo"] == 3.80 and t["combate"]["ready"] == 10.0
print("test_extraer_datos: OK (b2_ordenes 86, sonidos 6, iw 12 con #AMC, satan 4 tomas)")

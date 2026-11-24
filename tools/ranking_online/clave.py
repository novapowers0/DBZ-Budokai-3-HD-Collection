"""Clave HMAC del ranking online (RANK-ONLINE). Una por version publicada: la Action acepta la actual y la anterior.

  python tools/ranking_online/clave.py            crea la clave si no hay (no pisa una que ya exista)
  python tools/ranking_online/clave.py --nueva    clave nueva (la de antes pasa a RANK_HMAC_KEY_ANTERIOR.txt)

Escribe src/ranking_clave.local.inc (la clave ofuscada que compila el juego; NUNCA se sube: *.local.inc va en .gitignore)
y E:/DBZ3HD_trabajo/ranking_online/clave/RANK_HMAC_KEY.txt (lo que el usuario pega en el secreto del repo DBZ3HD-Ranking).
"""
import secrets
import shutil
import sys
from pathlib import Path

R = Path(__file__).resolve().parents[2]
INC = R / "src" / "ranking_clave.local.inc"
DIR = Path(r"E:\DBZ3HD_trabajo\ranking_online\clave")
TXT = DIR / "RANK_HMAC_KEY.txt"
XOR = 0x5A   # = kXor de src/ranking_online.cpp (ofusca, no cifra: frena a quien edita el mensaje, no a quien desmonta el exe)


def escribir_inc(clave):
    b = ", ".join(f"0x{c ^ XOR:02X}" for c in clave.encode())
    INC.write_text(f"// RANK-ONLINE: clave HMAC ofuscada (tools/ranking_online/clave.py). NO SUBIR.\n{b}\n", encoding="utf-8")


def main():
    DIR.mkdir(parents=True, exist_ok=True)
    if TXT.is_file() and "--nueva" not in sys.argv:
        clave = TXT.read_text(encoding="utf-8").strip()
    else:
        if TXT.is_file():
            shutil.copy2(TXT, DIR / "RANK_HMAC_KEY_ANTERIOR.txt")
        clave = secrets.token_hex(24)
        TXT.write_text(clave + "\n", encoding="utf-8")
    escribir_inc(clave)
    print(f"clave en {TXT} y {INC.relative_to(R)}")


if __name__ == "__main__":
    main()

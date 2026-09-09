"""acm_analyze.py - RE profunda del bloque #ACM HD (pool de animaciones).

El bin moveset HD = #AMB contenedor con:
  - #CSK  (BSK PS2 renombrado: propiedades de animacion + hit reactions)
  - #ACM xN (pools de animacion, AMM PS2 renombrado)

El bloque #ACM tiene header 0x20 y una tabla de entradas de 0x10 B que siguen
al header. Cada entrada empieza con 0x09 (o 0x19 si hay scale), como el AMM PS2.
Datos angulares PS2: por frame: frame_no u16 + roll u16 + pitch u16 + yaw u16
(0x0000 = 0 grados, 0xFFFF = 360). Este script valida la hipotesis.

Uso:
  python awo_tools/acm_analyze.py --bin out\\analysis\\acm\\sub2.bin
"""
import argparse
import os
import struct

BE = ">"

def parse(b):
    print(f"block size {len(b)}")
    print("header:", b[:0x20].hex())
    magic = b[:4]
    h = struct.unpack_from(BE + "IIIIIIII", b, 0)
    print(f"  magic={magic} +4={h[1]:#x} +8={h[2]:#x} +0C={h[3]:#x} "
          f"+10={h[4]:#x} +14={h[5]:#x} +18={h[6]:#x} +1C={h[7]:#x}")

    # tabla de entradas desde +0x20; probamos n = +0x10
    n_entries = h[4]
    entries = []
    for i in range(n_entries):
        off = 0x20 + i * 0x10
        if off + 0x10 > len(b):
            break
        e = struct.unpack_from(BE + "IIII", b, off)
        entries.append(e)
    print(f"\nn_entries(+0x10)={n_entries}, leidas={len(entries)}")
    print("primeras entradas:")
    for i, e in enumerate(entries[:12]):
        print(f"  [{i:3d}] {e[0]:#x} {e[1]:#x} {e[2]:#x} {e[3]:#x}")

    # probar base de datos: tabla termina en 0x20+n*0x10
    tbl_end = 0x20 + len(entries) * 0x10
    print(f"\ntabla termina en {tbl_end:#x} ({tbl_end - 0x20 - len(entries)*0x10})")

    # para cada entrada, probar las 3 bases candidatas de offset (campo e[3])
    # y validar el patron frame/angulos
    def try_data(base, e):
        data_off = base + e[3]
        if data_off < 0 or data_off + 16 > len(b):
            return None
        chunk = b[data_off:data_off + 32]
        vals = struct.unpack_from(BE + "16H", chunk) if len(chunk) >= 32 else None
        return data_off, chunk, vals

    for label, base in [("+0x20(tabla)", 0x20), ("bloque", 0x0),
                        ("tbl_end", tbl_end)]:
        print(f"\n-- base {label} = {base:#x} --")
        for i, e in enumerate(entries[:8]):
            r = try_data(base, e)
            if r is None:
                print(f"  [{i}] off fuera de rango")
                continue
            data_off, chunk, vals = r
            print(f"  [{i}] e={e} -> data@{data_off:#x}: {chunk[:16].hex()}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    args = ap.parse_args()
    with open(args.bin, "rb") as f:
        b = f.read()
    parse(b)

if __name__ == "__main__":
    main()
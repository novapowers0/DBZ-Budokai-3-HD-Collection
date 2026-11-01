#!/usr/bin/env python3
"""Comprobacion del traductor de guiones de IW (roster_build.iw_spx_traducir): mismo largo y ordenes sustituidas."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "mod center hd"))
import roster_build as rb  # noqa: E402


def llamada(orden):
    return (b"\x01\x10" + bytes((orden,)) if orden < 0x80 else b"\x01\x20" + struct.pack("<H", orden)) + b"\x02\x80\x02"


def main():
    cabeza = b"#SPX ver 0.01\0\0\0" + struct.pack("<IIII", 0x20, 0x24, 0, 0) + bytes(4)
    cod = llamada(0x88) + llamada(0x35) + llamada(0x9A) + llamada(0x99) + llamada(0x36) + llamada(0x44)
    out, n = rb.iw_spx_traducir(cabeza + cod)
    assert len(out) == len(cabeza + cod)
    assert n == {0x88: 1, 0x35: 1, 0x9A: 1, 0x99: 1, 0x36: 1}, n
    c = out[len(cabeza):]
    assert c[:4] == b"\x01\x10\x01\x00" and len(llamada(0x88)) == 7        # 0x88 -> mov #1, r0
    assert c[7:11] == b"\x01\x10\x02\x00"                                   # 0x35 -> mov #2, r0
    assert c[13:17] == b"\x01\x5B\x08\x00"                                  # 0x9A -> mov [r11+8], r0
    assert c[20:22] == b"\x04\x00"                                          # 0x99 -> fclr f0
    assert c.endswith(llamada(0x44))                                        # una orden de B3 no se toca
    print("test_iw_guion: OK")


if __name__ == "__main__":
    main()

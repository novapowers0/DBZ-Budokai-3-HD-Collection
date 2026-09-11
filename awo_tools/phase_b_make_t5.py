#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_b_make_t5.py - Test decisivo T5: invertir el pool sec34 SIN tocar el IB.

Objetivo: aislar el papel del IB. Comparar el render de:
  T4 = pool invertido dentro de bloques A + IB remapeado  -> "deforme"
  T5 = pool invertido (mismo tipo)  pero IB SIN remapear
- Si T5 renderiza IGUAL que T4 -> el IB NO controla la geometria (el guest
  dibuja por POSICION). Explica por que un relabeling consistente deforma.
- Si T5 renderiza DISTINTO (peor/caotico) -> el IB SI se usa; entonces T4 debio
  ser identico y habria un bug/pieza no considerada.

Uso: python phase_b_make_t5.py <entrada.bin> <salida.bin>
"""
import struct, sys
def be32(b,o): return struct.unpack(">I",b[o:o+4])[0]
def main():
    path,out=sys.argv[1],sys.argv[2]
    b=bytearray(open(path,"rb").read())
    awo=0x40; tbl=awo+be32(b,awo+0x1C); awg0=awo+be32(b,tbl)
    def g(o): return be32(b,awg0+o)
    sec=awg0+g(0x34)+2; vb2=awg0+g(0x2C)
    n_sec=(vb2-sec-2)//44
    orig=bytes(b)
    new=bytearray(n_sec*44)
    for o in range(n_sec):
        m=n_sec-1-o
        new[m*44:m*44+44]=orig[sec+o*44:sec+o*44+44]
    b[sec:sec+n_sec*44]=new
    open(out,"wb").write(bytes(b))
    print("T5: sec34 invertido (n=%d), IB y descriptores SIN tocar -> %s"%(n_sec,out))
if __name__=="__main__": main()

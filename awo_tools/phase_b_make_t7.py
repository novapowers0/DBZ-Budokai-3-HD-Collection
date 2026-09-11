#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_b_make_t7.py - Test T7: invertir el IB entero, pool INTACTO.

Aisla si el IB se usa para dibujar. Pool y descriptores NO se tocan.
- Si el render CAMBIA -> el IB SI se usa (entonces T4 debio ser identico: hay bug
  o pieza no considerada en el remapeo).
- Si el render NO cambia -> el IB es IGNORADO en el dibujo -> dibujo POSICIONAL
  (el pool se dibuja en su orden). Vía B bloqueada; Vía A (conserva orden) = via.

Uso: python phase_b_make_t7.py <entrada.bin> <salida.bin>
"""
import struct, sys
def be32(b,o): return struct.unpack(">I",b[o:o+4])[0]
def be16(b,o): return struct.unpack(">H",b[o:o+2])[0]
def main():
    path,out=sys.argv[1],sys.argv[2]
    b=bytearray(open(path,"rb").read())
    awo=0x40; tbl=awo+be32(b,awo+0x1C); awg0=awo+be32(b,tbl)
    def g(o): return be32(b,awg0+o)
    ibo=g(0x30); endo=g(0x38)
    ib=awg0+ibo; end=awg0+endo
    n_ib=(end-ib)//2
    vals=[be16(b,ib+k*2) for k in range(n_ib)]
    for k in range(n_ib):
        struct.pack_into(">H",b,ib+k*2,vals[n_ib-1-k])
    open(out,"wb").write(bytes(b))
    print("T7: IB entero invertido (n=%d), pool descriptores intactos -> %s"%(n_ib,out))
if __name__=="__main__": main()

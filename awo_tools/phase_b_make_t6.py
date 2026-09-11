#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_b_make_t6.py - Test T6: tocar SOLO los rangos A (pool e IB intactos).

Rota los rangos A=[A_start,A_count) entre los descriptores sec34 (descriptor i
recibe el A del siguiente). Pool e IB NO se tocan.
- Si el render CAMBIA (partes con vertices equivocados) -> el guest dibuja por A
  (posicional) -> explica por que T4 (reverse intra-bloque) deforma.
- Si el render NO cambia -> A no se usa para dibujar; la deformacion de T4 viene
  de otra pieza (habria que leer el codigo del guest).

Uso: python phase_b_make_t6.py <entrada.bin> <salida.bin>
"""
import struct, sys, re
def be32(b,o): return struct.unpack(">I",b[o:o+4])[0]
def main():
    path,out=sys.argv[1],sys.argv[2]
    b=bytearray(open(path,"rb").read())
    awo=0x40; tbl=awo+be32(b,awo+0x1C); awg0=awo+be32(b,tbl)
    def g(o): return be32(b,awg0+o)
    mgo=awg0+g(0x20); seco=g(0x34); vb2o=g(0x2C)
    sec33=seco
    descs=[]
    for m in re.finditer(rb"max \d+ m", bytes(b[mgo:awg0+seco])):
        dd=mgo+(m.start()-0x18)
        if be32(b,dd+0x44)!=0x2C00: continue
        As=be32(b,dd+0x50)>>8; Ac=be32(b,dd+0x54)>>8
        descs.append((dd,As,Ac))
    secs=[(dd,As,Ac) for dd,As,Ac in descs if As+Ac <= (vb2o-seco-2)//44]
    print("descriptores sec34: %d -> roto sus rangos A"%len(secs))
    for k in range(len(secs)):
        dd,As,Ac=secs[k]
        _,As2,Ac2=secs[(k+1)%len(secs)]
        struct.pack_into(">I",b,dd+0x50,(As2&0xFFFFFF)<<8)
        struct.pack_into(">I",b,dd+0x54,(Ac2&0xFFFFFF)<<8)
        print("  [%2d] A=[%d,%d) -> [%d,%d)"%(k,As,As+Ac,As2,As2+Ac2))
    open(out,"wb").write(bytes(b))
    print("escrito",out)
if __name__=="__main__": main()

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_b_ab_compare.py - ¿A (bloque) y B (IB) describen la MISMA malla?

Para cada descriptor sec34:
  - triangulos_lista_A = {(As+k, As+k+1, As+k+2)}
  - triangulos_strip_B = del rango B del IB (winding de strip)
Comprueba solape. Si son iguales -> el guest podria dibujar A como lista y B
como strip (dos vias). Si difieren -> vias distintas.

Uso: python phase_b_ab_compare.py <bin>
"""
import struct, sys, re
def be32(b,o): return struct.unpack(">I",b[o:o+4])[0]
def be16(b,o): return struct.unpack(">H",b[o:o+2])[0]
def main():
    b=open(sys.argv[1],"rb").read()
    awo=0x40; tbl=awo+be32(b,awo+0x1C); awg0=awo+be32(b,tbl)
    def g(o): return be32(b,awg0+o)
    mg=g(0x20); seco=g(0x34); vb2o=g(0x2C); ibo=g(0x30); endo=g(0x38)
    sec=awg0+seco+2; vb2=awg0+vb2o; ib=awg0+ibo; end=awg0+endo
    n_sec=(vb2-sec-2)//44; n_vb2=(ib-vb2)//44; n_ib=(end-ib)//2
    ibv=[be16(b,ib+k*2) for k in range(n_ib)]
    mgo=awg0+mg
    descs=[]
    for m in re.finditer(rb"max \d+ m", bytes(b[mgo:awg0+seco])):
        dd=mgo+(m.start()-0x18)
        if be32(b,dd+0x44)!=0x2C00: continue
        descs.append(dd)
    for i,dd in enumerate(descs):
        As=be32(b,dd+0x50)>>8; Ac=be32(b,dd+0x54)>>8
        Bs=be32(b,dd+0x58)>>8; Bc=be32(b,dd+0x5C)>>8
        if As+Ac>n_sec:
            print("[%2d] A fuera de sec34 (vb2) -> skip"%i); continue
        # lista A (indices absolutos)
        A_list={tuple(sorted((As+k,As+k+1,As+k+2))) for k in range(max(0,Ac-2))}
        # strip B
        idx=[ibv[x] for x in range(Bs,min(Bs+Bc,n_ib))]
        B_strip=set()
        for j in range(len(idx)-2):
            t=(idx[j],idx[j+1],idx[j+2])
            if len(set(t))==3: B_strip.add(tuple(sorted(t)))
        A_list={t for t in A_list if len(set(t))==3}
        ov=len(A_list & B_strip)
        print("[%2d] A=[%d,%d) len=%d  B=[%d,%d) idx=%d | triA(list)=%d triB(strip)=%d solape=%d (%.0f%% de A)"%(
            i,As,Ac,Ac,Bs,Bc,len(idx),len(A_list),len(B_strip),ov,100.0*ov/max(1,len(A_list))))
if __name__=="__main__": main()

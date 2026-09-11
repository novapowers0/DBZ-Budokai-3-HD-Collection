#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_b_desc_detail.py - Volcado detallado de descriptores A/B y del IB.

Objetivo: resolver la contradiccion "seguir el IB da geometria identica pero
T3/T4 deforman". Comprueba si los indices del IB del rango B son ABSOLUTOS
(al pool) o LOCALES al bloque A, si los bloques A son disjuntos, y lista los
indices fuera de rango.

Uso: python phase_b_desc_detail.py <bin>
"""
import struct, sys, re

def be32(b,o): return struct.unpack(">I",b[o:o+4])[0]
def be16(b,o): return struct.unpack(">H",b[o:o+2])[0]

def main():
    path=sys.argv[1]
    b=open(path,"rb").read()
    awo=0x40
    tbl=awo+be32(b,awo+0x1C); awg0=awo+be32(b,tbl)
    def g(o): return be32(b,awg0+o)
    mg=g(0x20); vb2o=g(0x2C); ibo=g(0x30); seco=g(0x34); endo=g(0x38)
    sec=awg0+seco+2; vb2=awg0+vb2o; ib=awg0+ibo; end=awg0+endo
    n_sec=(vb2-sec-2)//44; n_vb2=(ib-vb2)//44; n_ib=(end-ib)//2
    ntot=n_sec+n_vb2
    print("awg0=0x%X mg=0x%X sec=0x%X vb2=0x%X ib=0x%X end=0x%X"%(awg0,awg0+mg,sec,vb2,ib,end))
    print("n_sec=%d n_vb2=%d n_ib=%d ntot=%d"%(n_sec,n_vb2,n_ib,ntot))
    ibv=[be16(b,ib+k*2) for k in range(n_ib)]

    # indices del IB fuera de rango (distintos de 0xFFFF)
    oor=[(k,v) for k,v in enumerate(ibv) if v!=0xFFFF and v>=ntot]
    print("\nIB fuera de rango (>=%d): %d"%(ntot,len(oor)))
    from collections import Counter
    print("  valores:",sorted(Counter(v for _,v in oor).items()))
    print("  posiciones:",[k for k,_ in oor])

    # descriptores
    mgo=awg0+mg; zona_end=awg0+seco
    descs=[]
    for m in re.finditer(rb"max \d+ m", bytes(b[mgo:zona_end])):
        dd=mgo+(m.start()-0x18)
        if be32(b,dd+0x44)!=0x2C00: continue
        descs.append(dd)
    print("\ndescriptores (0x2C00): %d"%len(descs))
    Ablocks=[]
    for i,dd in enumerate(descs):
        As=be32(b,dd+0x50)>>8; Ac=be32(b,dd+0x54)>>8
        Bs=be32(b,dd+0x58)>>8; Bc=be32(b,dd+0x5C)>>8
        flag=be32(b,dd+0x48)
        idxs=[ibv[x] for x in range(Bs,min(Bs+Bc,n_ib)) if ibv[x]!=0xFFFF]
        if idxs:
            mn,mx=min(idxs),max(idxs)
            inA=all(As<=v<As+Ac for v in idxs)
            # local?
            local=all(0<=v<Ac for v in idxs)
            print("  [%2d] dd=0x%X flag=0x%X A=[%4d,%4d) B=[%4d,%4d) n=%3d vmin=%4d vmax=%4d inA=%s local=%s"%(
                i,dd,flag,As,Ac,Bs,Bc,len(idxs),mn,mx,inA,local))
        else:
            print("  [%2d] dd=0x%X flag=0x%X A=[%4d,%4d) B=[%4d,%4d) n=  0 (vacio)"%(
                i,dd,flag,As,Ac,Bs,Bc))
        if Ac: Ablocks.append((As,Ac))
    # bloques disjuntos?
    Ablocks.sort()
    ov=0
    for i in range(1,len(Ablocks)):
        if Ablocks[i][0]<Ablocks[i-1][0]+Ablocks[i-1][1]: ov+=1
    cov=sorted(set(range(s for s,c in Ablocks for _ in [0])))  # placeholder
    covset=set()
    for s,c in Ablocks:
        for x in range(s,s+c): covset.add(x)
    print("\nbloques A: %d, solapes=%d, verts cubiertos=%d/%d, fuente=%s"%(
        len(Ablocks),ov,len(covset),n_sec,"sec34"))
    # rango global de indices del IB (excl FFFF y oor)
    allv=[v for v in ibv if v!=0xFFFF and v<ntot]
    print("IB index rango [%d..%d]"%(min(allv),max(allv)))

if __name__=="__main__": main()

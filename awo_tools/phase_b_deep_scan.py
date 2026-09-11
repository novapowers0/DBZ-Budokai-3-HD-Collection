#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_b_deep_scan.py - Analisis profundo del binding pool<->dibujo.

(A) Dentro de cada bloque A, examina la secuencia de huesos (+28): runs contiguos.
(B) Busca referencias ESCALADAS/byte-offset al pool fuera de los buffers
    (v, v*44, v*44+sec, v<<8, v<<2) en u16 y u32.

Uso: python phase_b_deep_scan.py <bin>
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
    n_sec=(vb2-sec-2)//44; n_vb2=(ib-vb2)//44; n_ib=(end-ib)//2; ntot=n_sec+n_vb2
    ibv=[be16(b,ib+k*2) for k in range(n_ib)]
    bone=lambda v: be32(b, sec+v*44+28) if v<n_sec else 0xFFFFFFFF

    mgo=awg0+mg
    descs=[]
    for m in re.finditer(rb"max \d+ m", bytes(b[mgo:awg0+seco])):
        dd=mgo+(m.start()-0x18)
        if be32(b,dd+0x44)!=0x2C00: continue
        descs.append(dd)

    print("=== (A) huesos dentro de cada bloque A ===")
    for i,dd in enumerate(descs):
        As=be32(b,dd+0x50)>>8; Ac=be32(b,dd+0x54)>>8
        if As+Ac>n_sec: 
            print("  [%2d] A=[%d,%d) FUERA de sec34 (vb2/otro)"%(i,As,As+Ac)); continue
        seq=[bone(v) for v in range(As,As+Ac)]
        runs=1
        for k in range(1,len(seq)):
            if seq[k]!=seq[k-1]: runs+=1
        # resumen: hueso(min..max) y conteo de runs
        uniq=sorted(set(seq))
        print("  [%2d] A=[%4d,%4d) len=%4d runs=%3d huesos=%s%s"%(
            i,As,As+Ac,Ac,runs,uniq if len(uniq)<=8 else (uniq[:8]+['...']),
            "  <-- 1 hueso" if len(uniq)==1 else ""))

    print("\n=== (B) referencias escaladas fuera de buffers ===")
    sec_a=sec; sec_b=sec+n_sec*44; vb2_a=vb2; vb2_b=vb2+n_vb2*44; ib_a=ib; ib_b=ib+n_ib*2
    def in_buf(o): return (sec_a<=o<sec_b) or (vb2_a<=o<vb2_b) or (ib_a<=o<ib_b)
    def encs(v):
        yield ('v', v)
        yield ('v*44', v*44)
        yield ('v*44+sec', v*44+seco)
        yield ('v<<8', v<<8)
        yield ('v<<2', v<<2)
    hits={}
    # enfoque rapido por conjuntos (evita O(region*ntot))
    print("  (enfoque rapido por conjuntos)")
    enc={}
    for v in range(ntot):
        for nm,val in encs(v):
            enc.setdefault(val,[]).append((nm,v))
    fast={}
    for o in range(awg0, awg0+endo-1, 2):
        if in_buf(o): continue
        u=be16(b,o)
        if u in enc:
            for nm,v in enc[u]:
                fast.setdefault((2,nm),[]).append((o-awg0,v))
    for o in range(awg0, awg0+endo-3, 4):
        if in_buf(o): continue
        u=be32(b,o)
        if u in enc:
            for nm,v in enc[u]:
                fast.setdefault((4,nm),[]).append((o-awg0,v))
    for k in sorted(fast):
        r=fast[k]
        # quita falsos: si nm=='v' y ya lo cuenta el scan crudo, aun asi reportar
        offs=sorted(set(o for o,_ in r))
        print("  u%d %-9s hits=%4d (offs unicos=%d) ej: %s"%(
            k[0],k[1],len(r),len(offs),["+0x%X(v=%d)"%(o,v) for o,v in r[:4]]))

if __name__=="__main__": main()

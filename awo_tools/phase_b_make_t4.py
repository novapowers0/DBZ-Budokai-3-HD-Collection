#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_b_make_t4.py - Test T4: permutar SOLO dentro de cada bloque A.

Hipotesis H3: el guest trata el rango A=[A_start,A_start+A_count) de cada
descriptor como un BLOQUE contiguo de pool; el orden DENTRO del bloque es libre
(T2 lo confirmo), pero los bloques NO pueden mezclarse (T3 lo rompio).
T4 invierte los vertices DENTRO de cada bloque A (sec34), remapea el IB y deja
A intacto. Si renderiza identico -> el bloque A es la unidad; el port debe
construir el pool con los vertices de cada descriptor CONTIGUOS.

Uso: python phase_b_make_t4.py <entrada.bin> <salida.bin>
"""
import struct, sys, re
def be32(b,o): return struct.unpack(">I",b[o:o+4])[0]
def be16(b,o): return struct.unpack(">H",b[o:o+2])[0]
def load(path):
    b=bytearray(open(path,"rb").read()); awo=0x40
    tbl=awo+be32(b,awo+0x1C); awg0=awo+be32(b,tbl)
    def g(o): return be32(b,awg0+o)
    h={"mg":g(0x20),"vb2":g(0x2C),"ib":g(0x30),"sec":g(0x34),"end":g(0x38),"awg0":awg0}
    h["n_sec"]=(h["vb2"]-h["sec"]-2)//44; h["n_vb2"]=(h["ib"]-h["vb2"])//44
    h["n_ib"]=(h["end"]-h["ib"])//2
    return b,h
def main():
    path,out=sys.argv[1],sys.argv[2]
    b,h=load(path); awg0=h["awg0"]; sec=awg0+h["sec"]+2; vb2=awg0+h["vb2"]; ib_a=awg0+h["ib"]
    n_sec,n_vb2,n_ib=h["n_sec"],h["n_vb2"],h["n_ib"]; orig=bytes(b)
    mg=awg0+h["mg"]
    ib=[be16(b,ib_a+k*2) for k in range(n_ib)]
    perm=list(range(n_sec+n_vb2)); blocks=[]
    for m in re.finditer(rb"max \d+ m", bytes(b[mg:awg0+h["sec"]])):
        dd=mg+(m.start()-0x18)
        if be32(b,dd+0x44)!=0x2C00 or be32(b,dd+0x40)==0: continue
        As=be32(b,dd+0x50)>>8; Ac=be32(b,dd+0x54)>>8
        if Ac>0 and As+Ac<=n_sec:
            for x in range(Ac): perm[As+x]=As+(Ac-1-x)
            blocks.append((As,Ac))
    print("bloques A invertidos: %d  (vertices=%d)" % (len(blocks),sum(a for _,a in blocks)))
    new_sec=bytearray(n_sec*44)
    for o in range(n_sec):
        m=perm[o]; new_sec[m*44:m*44+44]=orig[sec+o*44:sec+o*44+44]
    b[sec:sec+n_sec*44]=new_sec
    for k in range(n_ib):
        v=ib[k]
        if v!=0xFFFF and v<len(perm): struct.pack_into(">H",b,ib_a+k*2,perm[v])
    open(out,"wb").write(bytes(b))
    b2,_=load(out)
    def grec(bb,v):
        o=(sec+v*44) if v<n_sec else (vb2+(v-n_sec)*44); return bytes(bb[o:o+44])
    ok=True
    for k in range(n_ib-2):
        for t in range(3):
            a1=ib[k+t]; a2=be16(b2,ib_a+(k+t)*2)
            if 0xFFFF in (a1,a2) or a1>=len(perm) or a2>=len(perm): continue
            if grec(orig,a1)!=grec(b2,a2): ok=False;break
        if not ok: break
    print("validacion:", "OK" if ok else "FALLO", "| escrito",out)
if __name__=="__main__": main()

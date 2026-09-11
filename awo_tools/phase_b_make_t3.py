#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_b_make_t3.py - Test T3 "reverse bien hecho" (Fase B / C1).

Reordena el pool de vertices de un bin HD manteniendo INVARIANTES:
  - permuta sec34 (y opcionalmente vb2) por una permutacion INTERNA a cada
    buffer (no se cruzan: layouts distintos -> sec34 bone@+28, vb2 FFFFFFFF@+28);
  - remapea el IB con la inversa de la permutacion;
  - recomputa A = [min(indices de B), max+1) de cada descriptor.

Si el juego renderiza identico -> la Via B solo exigia invariantes (no RE).
A diferencia del reverse de 2026-08-26, aqui NO se toca mesh-ref/arms/zonas y
el IB se remapea en TODO el espacio (sec34+vb2).

Uso:
  python phase_b_make_t3.py <entrada.bin> <salida.bin> [--reverse-vb2]
"""
import struct, sys, re

def be32(b,o): return struct.unpack(">I", b[o:o+4])[0]
def be16(b,o): return struct.unpack(">H", b[o:o+2])[0]

def load(path):
    b=bytearray(open(path,"rb").read())
    awo=0x40
    awg_tbl=awo+be32(b,awo+0x1C)
    awg0=awo+be32(b,awg_tbl)
    def g(o): return be32(b,awg0+o)
    h={"axes":g(0x14),"mg":g(0x20),"vb2":g(0x2C),"ib":g(0x30),"sec":g(0x34),"end":g(0x38)}
    h["n_sec"]=(h["vb2"]-h["sec"]-2)//44
    h["n_vb2"]=(h["ib"]-h["vb2"])//44
    h["n_ib"]=(h["end"]-h["ib"])//2
    h["awg0"]=awg0
    return b,h

def find_descriptors(b,awg0,h):
    mg=awg0+h["mg"]; zone_end=awg0+h["sec"]
    out=[]
    for m in re.finditer(rb"max \d+ m", bytes(b[mg:zone_end])):
        dd=mg+(m.start()-0x18)
        if be32(b,dd+0x44)!=0x2C00 or be32(b,dd+0x40)==0: continue
        out.append(dd)
    return out

def main():
    path,out=sys.argv[1],sys.argv[2]
    rev_vb2=("--reverse-vb2" in sys.argv)
    b,h=load(path)
    awg0=h["awg0"]; sec=awg0+h["sec"]+2; vb2=awg0+h["vb2"]; ib_a=awg0+h["ib"]
    n_sec,n_vb2,n_ib=h["n_sec"],h["n_vb2"],h["n_ib"]
    orig=bytes(b)
    ib=[be16(b,ib_a+k*2) for k in range(n_ib)]

    # permutaciones internas (reverse)
    def f(v):
        if v<n_sec: return n_sec-1-v
        if rev_vb2: return n_sec+(n_vb2-1-(v-n_sec))
        return v
    perm=[f(v) for v in range(n_sec+n_vb2)]

    # aplicar al pool
    new_sec=bytearray(n_sec*44); new_vb2=bytearray(n_vb2*44)
    for o in range(n_sec):
        m=perm[o]; assert m<n_sec
        new_sec[m*44:m*44+44]=b[sec+o*44:sec+o*44+44]
    for j in range(n_vb2):
        m=perm[n_sec+j]; assert m>=n_sec
        k=m-n_sec
        new_vb2[k*44:k*44+44]=b[vb2+j*44:vb2+j*44+44]
    b[sec:sec+n_sec*44]=new_sec
    b[vb2:vb2+n_vb2*44]=new_vb2

    # remapear IB
    oor=0
    for k in range(n_ib):
        v=be16(b,ib_a+k*2)
        if v==0xFFFF: continue
        if v<len(perm): struct.pack_into(">H",b,ib_a+k*2,perm[v])
        else: oor+=1
    print("IB fuera de rango [0,%d): %d" % (len(perm), oor))

    # recomputar A=[min(B),max+1)
    descs=find_descriptors(b,awg0,h)
    nd=0
    for dd in descs:
        Bs=be32(b,dd+0x58)>>8; Bc=be32(b,dd+0x5C)>>8
        idxs=[be16(b,ib_a+x*2) for x in range(Bs,min(Bs+Bc,n_ib)) if be16(b,ib_a+x*2)!=0xFFFF]
        if not idxs: continue
        mn,mx=min(idxs),max(idxs)
        struct.pack_into(">I",b,dd+0x50,(mn&0xFFFFFF)<<8)
        struct.pack_into(">I",b,dd+0x54,((mx+1-mn)&0xFFFFFF)<<8)
        nd+=1
    print("descriptores A/B recomputados: %d/%d" % (nd,len(descs)))

    open(out,"wb").write(bytes(b))

    # validacion: cada triangulo resuelve a los mismos bytes (via perm inversa)
    b2,_=load(out)
    def grec(bb,v):
        o=(sec+v*44) if v<n_sec else (vb2+(v-n_sec)*44)
        return bytes(bb[o:o+44])
    same=True
    for k in range(n_ib-2):
        for t in range(3):
            a1=ib[k+t]; a2=be16(b2,ib_a+(k+t)*2)
            if a1==0xFFFF or a2==0xFFFF or a1>=len(perm) or a2>=len(perm): continue
            if grec(orig,a1)!=grec(b2,a2): same=False; break
        if not same: break
    print("validacion (mismos bytes por triangulo):", "OK" if same else "FALLO")
    print("escrito:",out,"(%d bytes)"%len(b2))

if __name__=="__main__":
    main()

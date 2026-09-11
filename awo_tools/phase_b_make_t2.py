import struct, sys, os

def be32(b,o): return struct.unpack(">I",b[o:o+4])[0]
def be16(b,o): return struct.unpack(">H",b[o:o+2])[0]
def bef(b,o):  return struct.unpack(">f",b[o:o+4])[0]

def load(path):
    b=bytearray(open(path,"rb").read())
    awo=0x40
    awg_tbl=awo+be32(b,awo+0x1C)
    awg0=awo+be32(b,awg_tbl)
    def g(o): return be32(b,awg0+o)
    h={"n_bones":g(0x10),"axes":g(0x14),"mg":g(0x20),"vb2":g(0x2C),
       "ib":g(0x30),"sec":g(0x34),"end":g(0x38)}
    h["n_sec"]=(h["vb2"]-h["sec"]-2)//44
    h["n_vb2"]=(h["ib"]-h["vb2"])//44
    h["n_ib"]=(h["end"]-h["ib"])//2
    return b,awg0,h

def scan_ext(b,awg0,h):
    sec_a=awg0+h["sec"]+2; sec_b=sec_a+h["n_sec"]*44
    vb2_a=awg0+h["vb2"];   vb2_b=vb2_a+h["n_vb2"]*44
    ib_a=awg0+h["ib"];     ib_b=ib_a+h["n_ib"]*2
    def in_buf(o): return (sec_a<=o<sec_b) or (vb2_a<=o<vb2_b) or (ib_a<=o<ib_b)
    ext=set()
    for o in range(awg0, awg0+h["end"]-1, 2):
        if in_buf(o): continue
        v=be16(b,o)
        if 0<=v<h["n_sec"]+h["n_vb2"]: ext.add(v)
    for o in range(awg0, awg0+h["end"]-3, 4):
        if in_buf(o): continue
        v=be32(b,o)
        if 0<=v<h["n_sec"]+h["n_vb2"]: ext.add(v)
    return ext

def main():
    path=sys.argv[1]; out=sys.argv[2]
    b,awg0,h=load(path)
    orig=bytes(b)
    sec=awg0+h["sec"]+2
    ib_a=awg0+h["ib"]
    ib=[be16(b,ib_a+k*2) for k in range(h["n_ib"])]
    used={}
    for k,v in enumerate(ib):
        if v!=0xFFFF: used.setdefault(v,0); used[v]+=1
    ext=scan_ext(b,awg0,h)
    bone={i:be32(b,sec+i*44+28) for i in range(h["n_sec"])}
    # candidatos "limpios": usados por IB, sin ref externa
    clean=[v for v in used if v < h["n_sec"] and v not in ext]
    # agrupa por hueso y elige un par del mismo hueso con distinta posicion
    from collections import defaultdict
    bybone=defaultdict(list)
    for v in clean: bybone[bone[v]].append(v)
    pair=None
    for bn,lst in sorted(bybone.items()):
        if len(lst)>=2:
            pair=(lst[0],lst[1]); break
    if not pair:
        print("no se encontro par limpio"); return
    i,j=pair
    print("%s: par T2 i=%d j=%d (bone=%d) | usos IB: i=%d j=%d | %s" % (
        os.path.basename(path),i,j,bone[i],used[i],used[j],
        "AMBOS LIMPIOS (sin ref externa)" if (i not in ext and j not in ext) else "OJO"))
    def rec(k): return bytes(b[sec+k*44:sec+k*44+44])
    print("  rec i bone=%d zxy=(%.3f,%.3f,%.3f)" % (be32(b,sec+i*44+28),
        bef(b,sec+i*44+12),bef(b,sec+i*44+16),bef(b,sec+i*44+20)))
    print("  rec j bone=%d zxy=(%.3f,%.3f,%.3f)" % (be32(b,sec+j*44+28),
        bef(b,sec+j*44+12),bef(b,sec+j*44+16),bef(b,sec+j*44+20)))
    ri,rj=rec(i),rec(j)
    b[sec+i*44:sec+i*44+44]=rj
    b[sec+j*44:sec+j*44+44]=ri
    # remapear IB
    cnt=0
    for k in range(h["n_ib"]):
        v=be16(b,ib_a+k*2)
        if v==i: struct.pack_into(">H",b,ib_a+k*2,j); cnt+=1
        elif v==j: struct.pack_into(">H",b,ib_a+k*2,i); cnt+=1
    print("  IB remapeado: %d entradas" % cnt)
    open(out,"wb").write(bytes(b))
    print("  escrito:",out,"(%d bytes)" % len(b))
    # validacion: los triangulos resuelven a los MISMOS registros
    b2,_,_=load(out); ib2=[be16(b2,ib_a+k*2) for k in range(h["n_ib"])]
    ok=True
    for k in range(h["n_ib"]):
        a1=ib[k]; a2=ib2[k]
        exp = j if a1==i else (i if a1==j else a1)
        if a2 != exp: ok=False; break
    print("  validacion remap IB:", "OK" if ok else "FALLO")
    # los registros resueltos por cada triangulo son identicos por construccion:
    def rec_of(bb,k): return bytes(bb[sec+k*44:sec+k*44+44])
    same=True
    for k in range(h["n_ib"]-2):
        a1,c1,d1=ib[k],ib[k+1],ib[k+2]
        a2,c2,d2=ib2[k],ib2[k+1],ib2[k+2]
        for (x,y) in ((a1,a2),(c1,c2),(d1,d2)):
            if x!=0xFFFF and y!=0xFFFF and rec_of(orig,x)!=rec_of(b2,y): same=False; break
    print("  validacion registros (mismos bytes por triangulo):", "OK" if same else "FALLO")

if __name__=="__main__":
    main()

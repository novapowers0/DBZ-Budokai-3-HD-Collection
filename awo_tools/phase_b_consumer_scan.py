import struct, sys

def be32(b,o): return struct.unpack(">I",b[o:o+4])[0]
def be16(b,o): return struct.unpack(">H",b[o:o+2])[0]

def load(path):
    b=open(path,"rb").read()
    awo=0x40
    awg_tbl=awo+be32(b,awo+0x1C)
    awg0=awo+be32(b,awg_tbl)
    def be32o(o): return be32(b,awg0+o)
    h={"n_bones":be32o(0x10),"axes":be32o(0x14),"mg":be32o(0x20),"mg_size":be32o(0x28),
       "vb2":be32o(0x2C),"ib":be32o(0x30),"sec":be32o(0x34),"end":be32o(0x38)}
    h["n_sec"]=(h["vb2"]-h["sec"]-2)//44
    h["n_vb2"]=(h["ib"]-h["vb2"])//44
    h["n_ib"]=(h["end"]-h["ib"])//2
    return b,awg0,h

def main():
    path=sys.argv[1]
    b,awg0,h=load(path)
    name=path.split(chr(92))[-1]
    n=h["n_sec"]+h["n_vb2"]
    # regiones absolutas dentro de AWG0
    sec_a=awg0+h["sec"]+2; sec_b=sec_a+h["n_sec"]*44
    vb2_a=awg0+h["vb2"];   vb2_b=vb2_a+h["n_vb2"]*44
    ib_a=awg0+h["ib"];     ib_b=ib_a+h["n_ib"]*2
    def in_buf(off): return (sec_a<=off<sec_b) or (vb2_a<=off<vb2_b) or (ib_a<=off<ib_b)
    print("="*78)
    print("%s AWG0=0x%X n=%d (n_sec=%d n_vb2=%d) n_ib=%d" % (name,awg0,n,h["n_sec"],h["n_vb2"],h["n_ib"]))
    # 1) ¿cuántos indices de vertice aparecen como u16 FUERA del IB (dentro de AWG0)?
    ext16={}
    for o in range(awg0, awg0+h["end"]-1, 2):
        if ib_a<=o<ib_b: continue
        v=be16(b,o)
        if 0<=v<n:
            ext16.setdefault(v,[]).append(o-awg0)
    # 2) ¿cuántos indices aparecen como u32 FUERA de todos los buffers?
    ext32={}
    for o in range(awg0, awg0+h["end"]-3, 4):
        if in_buf(o): continue
        v=be32(b,o)
        if 0<=v<n:
            ext32.setdefault(v,[]).append(o-awg0)
    zero16=[v for v in range(n) if v not in ext16]
    zero32=[v for v in range(n) if v not in ext32]
    print("indices de vertice con CERO ref u16 fuera del IB: %d / %d" % (len(zero16), n))
    print("indices de vertice con CERO ref u32 fuera de buffers: %d / %d" % (len(zero32), n))
    print("--- valores u16 externos mas frecuentes (posible consumer) ---")
    for v in sorted(ext16, key=lambda k:-len(ext16[k]))[:12]:
        locs=ext16[v][:6]
        print("  v=%4d  veces=%3d  offs=%s" % (v,len(ext16[v]),["+0x%X"%x for x in locs]))
    print("--- valores u32 externos mas frecuentes ---")
    for v in sorted(ext32, key=lambda k:-len(ext32[k]))[:12]:
        locs=ext32[v][:6]
        print("  v=%4d  veces=%3d  offs=%s" % (v,len(ext32[v]),["+0x%X"%x for x in locs]))
    # 3) ¿existen indices referenciados SOLO por el IB? (=> permutables sin consumer)
    ib_counts={}
    for k in range(h["n_ib"]):
        v=be16(b, ib_a+k*2)
        if v!=0xFFFF: ib_counts[v]=ib_counts.get(v,0)+1
    clean=[v for v in range(n) if ib_counts.get(v,0)>0 and v not in ext16 and v not in ext32]
    print("indices usados por el IB y SIN ninguna ref externa (permutables aislados): %d" % len(clean))

if __name__=="__main__":
    main()

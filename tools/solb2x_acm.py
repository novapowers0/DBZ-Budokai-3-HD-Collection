import struct
def load(path,blk):
    d=open(path,'rb').read()
    q=64+24*blk; off,tam=struct.unpack_from('<2I',d,q+8)
    return d[off:off+tam]
def info(a):
    n,tbl,nb,nom=struct.unpack_from('>4I',a,16)
    names=[a[nom+32*j:nom+32*j+32].split(b'\0')[0].decode('latin1') for j in range(nb)]
    return n,tbl,nb,names
def clip(a,c):
    n,tbl,nb,names=info(a)
    fl,var,nf,off=struct.unpack_from('>4I',a,tbl+16*c)
    per=3 if fl&0x10 else 2
    rot={};pos={}
    for j in range(nb):
        rp,pp=struct.unpack_from('>2I',a,off+4*per*j)
        if rp:
            k=struct.unpack_from('>I',a,rp+8)[0]
            rot[names[j]]=[struct.unpack_from('>4H',a,rp+12+8*i) for i in range(k)]
        if pp:
            k=struct.unpack_from('>I',a,pp+8)[0]
            pos[names[j]]=[(struct.unpack_from('>I',a,pp+12+16*i)[0],)+struct.unpack_from('>3f',a,pp+16+16*i) for i in range(k)]
    return fl,var,nf,rot,pos

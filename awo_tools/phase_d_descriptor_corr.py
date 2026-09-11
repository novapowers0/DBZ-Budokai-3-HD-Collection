import re, struct, sys
b=open(sys.argv[1],"rb").read()
awo=0x40
awg_tbl=awo+struct.unpack(">I",b[awo+0x1C:awo+0x20])[0]
n_awg=struct.unpack(">I",b[awo+0x18:awo+0x1C])[0]
awg0=awo+struct.unpack(">I",b[awg_tbl:awg_tbl+4])[0]
h=lambda o: struct.unpack(">I",b[awg0+o:awg0+o+4])[0]
end=awg0+h(0x38)
tags=[m.start() for m in re.finditer(rb"max \d", b[awg0:end])]
print("desc  B_s   B_c   +10 +14 +40 +44      +48    +4C   flag")
rows=[]
for t in tags:
    dd=awg0+t-0x18
    lab=bytes(b[dd:dd+0x10]).split(b"\x00")[0].decode("latin1","replace")
    g=lambda o: struct.unpack(">I",b[dd+o:dd+o+4])[0]
    As,Ac=g(0x50)>>8,g(0x54)>>8
    Bs,Bc=g(0x58)>>8,g(0x5C)>>8
    rows.append((Bs,Bc,lab,g(0x10),g(0x14),g(0x40),g(0x44),g(0x48),g(0x4C),g(0x5C)&0xFF))
for r in sorted(rows):
    print("       %4d %5d   %2d  %2d  %2d %08X %08X %08X  %02X  %s"%(r[0],r[1],r[3],r[4],r[5],r[6],r[7],r[8],r[9],r[2]))

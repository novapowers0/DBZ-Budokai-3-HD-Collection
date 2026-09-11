import re, sys
sys.path.insert(0, sys.argv[3])
from awg_vertex_buffer import AwgVertexBuffer
log=sys.argv[1]; port=AwgVertexBuffer.load(sys.argv[2]); pib=port.indices()
draws=[]; cur=None
for ln in open(log,'r',errors='replace').read().splitlines():
    m=re.match(r"DRAW prim=(\d+) idx=(\d+) src=(\d+) isz=(\d+) dma=([0-9A-Fa-f]+) nw=(\d+)",ln)
    if m:
        cur=dict(prim=int(m.group(1)),idx=int(m.group(2)),dma=int(m.group(5),16),ib=None); draws.append(cur)
    elif ln.startswith("  IB:") and cur is not None:
        cur['ib']=[int(x) for x in ln.split(":",1)[1].split()]
body=[d for d in draws if 0x1BD00000<=d['dma']<0x1BD10000 and d['ib']]
mism=0
for d in sorted(body,key=lambda x:x['dma']):
    off=(d['dma']-0x1BD00000)//2; seg=pib[off:off+len(d['ib'])]
    if seg!=d['ib']:
        mism+=1
        if mism<=6:
            print("MISMATCH off=%d\n  guest:%s\n  file :%s"%(off,d['ib'][:10],seg[:10]))
print("total body=%d  mismatches=%d"%(len(body),mism))

r"""R7: lee los padlog_host/guest (DBZ3_NET_PADLOG=1, columnas 16=vblanks acumulados, 17=us de pared) y cuenta vblanks por tick en cada lado.
Uso: python tools\analiza_vblank.py [carpeta] [fichero_host fichero_guest]"""
import sys, collections
from pathlib import Path
d = Path(sys.argv[1] if len(sys.argv) > 1 else r"D:\tmp\padlog")
if len(sys.argv) > 3:
    h, g = d / sys.argv[2], d / sys.argv[3]
else:
    h = max(d.glob("padlog_host_*.csv"), key=lambda p: p.stat().st_mtime)
    g = max(d.glob("padlog_guest_*.csv"), key=lambda p: p.stat().st_mtime)
def carga(p):
    r = {}
    for l in p.read_text().splitlines():
        c = l.split(",")
        if len(c) >= 17: r[int(c[0])] = (",".join(c[:15]), int(c[15]), int(c[16]))
    return r
A, B = carga(h), carga(g)
ts = sorted(set(A) & set(B))
print(f"{h.name} {len(A)} ticks / {g.name} {len(B)} ticks / comunes {len(ts)}")
print("mandos distintos:", sum(A[t][0] != B[t][0] for t in ts))
for n, X in (("host", A), ("guest", B)):
    ks = sorted(X); hist = collections.Counter(); mal = []
    for a, b in zip(ks, ks[1:]):
        dv = X[b][1] - X[a][1]; hist[min(dv, 4)] += 1
        if dv != 1 and len(mal) < 5: mal.append((b, dv, round((X[b][2] - X[a][2]) / 1000, 1)))
    ms = [(X[b][2] - X[a][2]) / 1000 for a, b in zip(ks, ks[1:])]
    print(n, "vblanks por tick (4=4+):", dict(sorted(hist.items())), "ms/tick max", round(max(ms), 1) if ms else 0, "primeros !=1 (tick,dv,ms):", mal)
# deltas distintos entre lados en el mismo tick
dif = []
for a, b in zip(ts, ts[1:]):
    if b == a + 1 and (A[b][1] - A[a][1]) != (B[b][1] - B[a][1]): dif.append(b)
print("ticks con vblanks-por-tick distinto entre lados:", len(dif), "primeros:", dif[:8])

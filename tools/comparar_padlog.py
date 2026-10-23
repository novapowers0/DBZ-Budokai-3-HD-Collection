"""Compara los padlog_host/guest (DBZ3_NET_PADLOG=1) de una pareja: uso `python tools\comparar_padlog.py [carpeta]` (por defecto D:\tmp\padlog, los dos mas recientes)."""
import sys
from pathlib import Path

d = Path(sys.argv[1] if len(sys.argv) > 1 else r"D:\tmp\padlog")
h = max(d.glob("padlog_host_*.csv"), key=lambda p: p.stat().st_mtime)
g = max(d.glob("padlog_guest_*.csv"), key=lambda p: p.stat().st_mtime)
L = lambda p: {int(r.split(",")[0]): r.strip() for r in p.read_text().splitlines() if r}
a, b = L(h), L(g)
comunes = sorted(set(a) & set(b))
dif = [t for t in comunes if a[t] != b[t]]
print(f"host {h.name}: {len(a)} ticks; guest {g.name}: {len(b)} ticks; comunes {len(comunes)}; distintos {len(dif)}")
for t in dif[:10]:
    print(f"  tick {t}: host {a[t]} | guest {b[t]}")
print("primer tick distinto:", dif[0] if dif else None)

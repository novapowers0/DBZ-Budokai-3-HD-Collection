"""Localiza definiciones recompiladas ignoradas por el buscador del harness."""
from pathlib import Path
for f in Path('generated').glob('dbz3_recomp.*.cpp'):
    for n,l in enumerate(f.read_text(encoding='utf-8').splitlines(),1):
        if any(x in l for x in ('820F9548','82110A38','82111D58')) and ('{' in l or 'PPC_FUNC' in l):print(f,n,l)

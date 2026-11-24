# R23 G3: hojas PCSX2 | port en cuadros exactos. El port se pausa en 4 cuadros separados 200 (G3_<clave>.toml); las
# referencias de PCSX2 (fx_auditoria/orig, una cada 40 cuadros) se alinean buscando el desfase de la serie que minimiza
# la diferencia media de los 4 pares.
import glob, os, re, sys
from PIL import Image
import numpy as np
O = 'E:/DBZ3HD_trabajo/fx_auditoria/orig/'
S = 'E:/DBZ3HD_trabajo/g23/g3_hojas/'
NS = [5800, 6000, 6200, 6400]
def peq(im): return np.asarray(im.convert('L').resize((80, 60)), dtype=float)
claves = sys.argv[1:] or ['7D%X' % i for i in range(10)] + ['7DA', '7DB', '7DC', '7DD', '7DE', '7DF', '7E0', '7E1']
for c in claves:
    run = sorted(glob.glob('D:/DBZ3HD/_pruebas/*_G3_%s' % c))[-1]
    port, ns = [], []
    for n in sorted(int(re.search(r'g(\d+)\.png', f).group(1)) for f in glob.glob(run + '/g*.png')):
        p = Image.open(run + '/g%d.png' % n).convert('RGB')
        port.append(p.crop((8, 31, p.width - 8, p.height - 8))); ns.append(n)
    pp = [peq(p) for p in port]
    mejor = (1e9, None, None)
    for d in sorted(glob.glob(O + 'G02_0x%s_*' % c)):
        refs = sorted(glob.glob(d + '/c*.png'), key=lambda f: int(re.search(r'c(\d+)', f).group(1)))
        idx = {int(re.search(r'c(\d+)', f).group(1)): f for f in refs}
        cache = {}
        for s in idx:
            tot, ok = 0, 0
            for k in range(len(pp)):
                f = idx.get(s + ns[k] - ns[0])
                if not f: continue
                if f not in cache: cache[f] = peq(Image.open(f))
                tot += np.abs(pp[k] - cache[f]).mean(); ok += 1
            if ok == len(pp) and tot / ok < mejor[0]:
                mejor = (tot / ok, d, s)
    d_, d, s = mejor
    if d is None:
        print(c, 'sin serie alineable'); continue
    W, H = 400, 300
    hoja = Image.new('RGB', (W * len(pp), H * 2))
    for k in range(len(pp)):
        f = '%s/c%05d.png' % (d, s + ns[k] - ns[0])
        hoja.paste(Image.open(f).convert('RGB').resize((W, H)), (k * W, 0))
        hoja.paste(port[k].resize((W, H)), (k * W, H))
    hoja.save(S + c + '.png')
    print(c, 'dif media %.1f' % d_, os.path.basename(d), 'desde', s, flush=True)

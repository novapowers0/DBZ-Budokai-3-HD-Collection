"""auditar_nombres.py - Cuenta, sin juego, las capsulas de cada personaje del roster construido que no tienen rotulo
de nombre en el banco de combate (por idioma).  Uso: python auditar_nombres.py <carpeta del juego>   (lee mods/_roster)
Salida: por personaje, rotulos presentes/esperados y los IDs que faltan (miss) o estan vacios (blank)."""
import sys, os, tomllib, struct, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capsulas, swap_b3, numpy as np
root = sys.argv[1]
R = os.path.join(root, "mods", "_roster")
t = tomllib.load(open(os.path.join(R, "roster.toml"), "rb"))
def ent(afs, e):
    p = os.path.join(R, "us", afs, str(e), "geom.bin")
    if not os.path.isfile(p): return None
    out = os.path.join(os.path.join(tempfile.gettempdir(), "auditar_nombres_dec"), afs, str(e)+".bin")
    if not os.path.isfile(out):
        os.makedirs(os.path.dirname(out), exist_ok=True)
        swap_b3.lzx_decompress(p, out, tempfile.gettempdir())
    return open(out,"rb").read()
langs = {"en":"data_usi.afs","es":"data_spn.afs","fr":"data_fra.afs","de":"data_ger.afs","it":"data_ita.afs","eng":"data_eng.afs"}
names = {c["id"]:c for c in t.get("capsula",[])}
tot = {}
for p in t["personaje"]:
    hud = p.get("hud"); 
    exp = set(p.get("capsulas",[]))|set(p.get("hereda",[]))|set(p.get("habilidades_ataques",[]))|set(p.get("habilidades_transformaciones",[]))
    exp.discard(0)
    if not hud: print(p["nombre"], "SIN hud"); continue
    fid = hud[0]*256+hud[1]
    row=[]
    for lg, afs in langs.items():
        b = ent(afs, fid)
        if b is None: row.append("%s:noBank"%lg); continue
        base = struct.unpack(">I", b[0x18:0x1C])[0]
        offs = capsulas.azt_entries(b)
        miss=[]; blank=[]
        for c in sorted(exp):
            k=c-base
            if k<0 or k>=len(offs) or not offs[k]: miss.append(c); continue
            im = capsulas.azt_read(b,k)
            if im is None or im[...,3].max()==0: blank.append(c)
        row.append("%s:%d/%d miss=%s blank=%s"%(lg,len(exp)-len(miss)-len(blank),len(exp),miss,blank))
    print("%s (id %s): "%(p["nombre"],p["id"]), " | ".join(row))
    for r in row[2:]:
        if "miss=[]" not in r or "blank=[]" not in r: print("    ", r)

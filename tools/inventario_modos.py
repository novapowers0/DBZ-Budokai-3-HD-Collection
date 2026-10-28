"""Inventario funcion a funcion de un modo: semillas = direcciones citadas en el mapa; se baja por las llamadas
del descompilado (sin entrar en utilidades muy llamadas) y se mira si el port cita cada direccion."""
import re, sys, os, collections
SAL = r"E:/DBZ3HD_trabajo/ghidra/salida"
RAIZ = r"C:/Users/javie/Desktop/PROYECTOS IA/DBZ Budokai 3 HD Collection"
MAPAS = RAIZ + "/studies 5/ENTREGAS/MAPAS_CODIGO"
modo, c_file, mapa, prof_max = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 3

txt = open(os.path.join(SAL, c_file), encoding="utf-8", errors="replace").read()
partes = re.split(r"^// ==== ([0-9a-fA-F]{8}) (\S+)\s*$", txt, flags=re.M)
fun = {}  # addr -> (nombre, cuerpo)
for i in range(1, len(partes), 3):
    fun[int(partes[i], 16)] = (partes[i + 1], partes[i + 2])
por_nombre = {n: a for a, (n, _) in fun.items()}
ident = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
llamadas, llamantes = {}, collections.Counter()
for a, (n, cuerpo) in fun.items():
    cs = {por_nombre[x] for x in ident.findall(cuerpo) if x in por_nombre and por_nombre[x] != a}
    llamadas[a] = cs
    for c in cs:
        llamantes[c] += 1

mtxt = open(os.path.join(MAPAS, mapa), encoding="utf-8").read()
semillas = {int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{5,6})\b", mtxt)} & set(fun)
semillas |= {int(h, 16) for h in re.findall(r"FUN_00([0-9A-Fa-f]{6})", mtxt) if int(h, 16) in fun}
semillas |= {por_nombre[n] for n in re.findall(r"\b([A-Z][A-Za-z0-9]+_[A-Za-z0-9_]+)\b", mtxt) if n in por_nombre}

src = ""
for d, _, fs in os.walk(RAIZ + "/src"):
    for f in fs:
        if f.endswith((".cpp", ".h")):
            src += open(os.path.join(d, f), encoding="utf-8", errors="replace").read().lower()

UTIL = 25  # funciones con mas llamantes = utilidades del motor: se listan pero no se baja por ellas
vistos, cola = {}, [(s, 0) for s in sorted(semillas)]
while cola:
    a, p = cola.pop(0)
    if a in vistos:
        continue
    vistos[a] = p
    if p < prof_max and llamantes[a] <= UTIL:
        cola += [(c, p + 1) for c in sorted(llamadas[a])]

# MAPAS_CODIGO/SUSTITUIDAS_<MODO>.txt: una direccion (0x...) por linea + motivo; = lo hace el motor de B3 (combate, Edit Skills...)
sust_txt = os.path.join(MAPAS, "SUSTITUIDAS_%s.txt" % modo)
sust = {int(h, 16) for h in re.findall(r"^\s*0x([0-9A-Fa-f]+)", open(sust_txt, encoding="utf-8").read(), re.M)} if os.path.exists(sust_txt) else set()


def estado(a):
    if a in sust:
        return "sustituida"
    n = fun[a][0].lower()
    h = "%x" % a
    if re.search(r"// eu 0x0*%s\b" % h, src):
        return "citada"   # solo cuenta `// EU 0xADDR Nombre` encima del codigo traducido
    if re.search(r"(0x0*%s|fun_00%s)\b" % (h, h), src) or (not n.startswith("fun_") and n in src):
        return "mencionada"   # nombrada en una lista o nota, sin traduccion marcada: sigue pendiente
    return "falta"

filas = []
for a, p in sorted(vistos.items(), key=lambda x: (x[1], x[0])):
    n, cuerpo = fun[a]
    util = llamantes[a] > UTIL
    filas.append((p, a, n, cuerpo.count("\n"), llamantes[a], "utilidad" if util else estado(a)))
out = os.path.join(MAPAS, "INVENTARIO_%s.md" % modo)
with open(out, "w", encoding="utf-8") as f:
    c = collections.Counter(r[5] for r in filas)
    f.write("# Inventario %s (%s, semillas de %s, profundidad %d)\n\n" % (modo, c_file, mapa, prof_max))
    f.write("Generado por D:/tmp/inventario/inventario.py. citada = `// EU 0xADDR` encima del codigo traducido; mencionada = solo nombrada (pendiente); "
            "falta = no aparece; sustituida = en SUSTITUIDAS_<MODO>.txt (B3 lo hace con su motor); utilidad = funcion del motor con >%d llamantes (no se baja por ella).\n\n" % UTIL)
    f.write("Total %d: %s\n\n| prof | direccion | nombre | lineas | llamantes | estado |\n|---|---|---|---|---|---|\n" % (len(filas), dict(c)))
    for p, a, n, l, ll, e in filas:
        f.write("| %d | 0x%06X | %s | %d | %d | %s |\n" % (p, a, n, l, ll, e))
print(modo, len(semillas), "semillas", dict(collections.Counter(r[5] for r in filas)),
      "lineas falta:", sum(r[3] for r in filas if r[5] == "falta"))

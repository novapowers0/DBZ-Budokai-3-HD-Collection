"""Test de claves de idioma: falla si a un texto de la interfaz le falta traduccion en alguno de los 7 idiomas.
Idiomas: es (el texto fuente), en, fr/de/it (kTable por texto espanol, o i18n_modos.toml por texto ingles), pt/ja (toml).
Uso: python tools/test_i18n.py [--lista]   (sale 1 si falta algo; --lista imprime las claves que faltan)
"""
import re, sys, tomllib
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
LENG = ("fr", "de", "it", "pt", "ja")
ESC = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'", "r": "\r", "0": "\0"}


def literales(s, i):
    """Lee desde s[i] una o mas cadenas C adyacentes; devuelve (texto, pos_final) o None si no es solo literales."""
    out, n = [], len(s)
    while True:
        while i < n and s[i] in " \t\r\n":
            i += 1
        if i < n and s.startswith("//", i):
            i = s.find("\n", i)
            continue
        if i >= n or s[i] != '"':
            break
        i += 1
        buf = []
        while i < n and s[i] != '"':
            if s[i] == "\\":
                i += 1
                buf.append(ESC.get(s[i], s[i]))
            else:
                buf.append(s[i])
            i += 1
        i += 1
        out.append("".join(buf))
    return ("".join(out), i) if out else None


def args(s, i, cuantos):
    """cuantos argumentos literales separados por coma desde s[i]; None si alguno no es literal."""
    r = []
    for k in range(cuantos):
        x = literales(s, i)
        if not x:
            return None
        r.append(x[0])
        i = x[1]
        while i < len(s) and s[i] in " \t\r\n":
            i += 1
        if k < cuantos - 1:
            if s[i] != ",":
                return None
            i += 1
    return r


def tabla_k(cpp):
    """kTable de i18n.cpp: {es: (it, de, fr)}."""
    t = cpp[cpp.index("kTable[] = {"):]
    t = t[: t.index("\n  };")]
    r = {}
    for m in re.finditer(r"\{\s*\"", t):
        a = args(t, m.end() - 1, 4)
        if a:
            r[a[0]] = tuple(a[1:])
    return r


def llamadas():
    """(archivo, es, en, es_L) de cada i18n::T(es, en) y L(es, en, fr, de, it) con todos sus argumentos literales."""
    r = []
    for f in sorted((RAIZ / "src").rglob("*.cpp")):
        if f.name in ("i18n.cpp", "i18n_selftest.cpp"):
            continue
        s = f.read_text(encoding="utf-8-sig", errors="replace")
        for m in re.finditer(r"(?<![\w:])(?:i18n::|T::)?T\(|(?<![\w:])L\(", s):
            l5 = m.group(0).endswith("L(")   # L(es, en, fr, de, it): ya trae fr/de/it, faltan pt/ja
            a = args(s, m.end(), 5 if l5 else 2)
            if a:
                r.append((f.name, a[0], a[1], l5))
    return r


def comprobar():
    toml = tomllib.load(open(RAIZ / "res/ingame_ui/i18n_modos.toml", "rb"))
    k = tabla_k((RAIZ / "src/launcher/i18n.cpp").read_text(encoding="utf-8"))
    falta = []
    for f, es, en, l5 in llamadas():
        if not es.strip() or len(en) < 2 or not any(c.isalpha() for c in en):  # "s" (plural) es neutra
            continue
        fila = toml.get(en, {})
        cubre_kt = l5 or (es in k and all(k[es]))  # fr/de/it por el texto espanol
        for l in LENG:
            if l in ("fr", "de", "it") and cubre_kt:
                continue
            if not fila.get(l):
                falta.append((f, l, en))
    # pt/ja siempre en el toml; fr/de/it pueden venir de kTable (se juzga por llamada)
    for en, fila in toml.items():
        if len(en) > 1 and not (fila.get("pt") and fila.get("ja")):
            falta.append(("i18n_modos.toml", "pt/ja", en))
    return falta


def demo():
    assert literales('"a" "b\\n" x', 0)[0] == "ab\n"
    assert args('"a", "b")', 0, 2)[:2] == ["a", "b"] and args('x, "b")', 0, 2) is None


if __name__ == "__main__":
    demo()
    falta = comprobar()
    vistas = sorted(set(falta))
    print(f"{len(vistas)} traducciones que faltan")
    if "--lista" in sys.argv:
        for f, l, en in vistas:
            print(f"{f}\t{l}\t{en[:90]!r}")
    sys.exit(1 if vistas else 0)

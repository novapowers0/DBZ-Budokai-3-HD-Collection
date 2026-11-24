r"""chequeo.py - portero de las herramientas compartidas: que ninguna se quede rota a medias.

  python tools\chequeo.py                 comprueba las herramientas de todos (lista HERRAMIENTAS)
  python tools\chequeo.py a.py b.py       comprueba esos archivos
  python tools\chequeo.py --autoprueba

Mira la sintaxis y los nombres usados que no estan definidos ni importados (lo que tumbo todas las pruebas
con `dormir()` el 23/10). Usa pyflakes si esta instalado; si no, `symtable` de la biblioteca estandar.
No ejecuta nada de los archivos. Codigo de salida 1 si algo falla. Correlo antes de dejar editada una herramienta.
"""
import ast
import builtins
import sys
import symtable
import time
from pathlib import Path

R = Path(__file__).resolve().parent.parent
HERRAMIENTAS = ["tools/pruebas.py", "tools/compilar.py", "tools/tanda.py", "tools/pcsx2_pool.py",
                "mod center hd/roster_build.py", "mod center hd/importar.py", "tools/infra.py",
                "tools/chequeo.py", "tools/oc.py"]
OPCIONALES = {"tools/infra.py", "tools/oc.py"}
CONOCIDOS = set(dir(builtins)) | {"__file__", "__builtins__", "__annotations__", "__path__", "__cached__",
                                  "WindowsError"}


def _linea(arbol, nombre):
    for n in ast.walk(arbol):
        if isinstance(n, ast.Name) and n.id == nombre and isinstance(n.ctx, ast.Load):
            return n.lineno
    return 0


def _sin_definir_symtable(src, arbol, ruta):
    if any(isinstance(n, ast.ImportFrom) and any(a.name == "*" for a in n.names) for n in ast.walk(arbol)):
        return []  # ponytail: con `import *` no se sabe que nombres entran; se salta la comprobacion
    mod = symtable.symtable(src, str(ruta), "exec")
    tablas, i = [mod], 0
    while i < len(tablas):
        tablas.extend(tablas[i].get_children())
        i += 1
    definidos = {s.get_name() for t in tablas for s in t.get_symbols()
                 if (t is mod or s.is_declared_global()) and (s.is_assigned() or s.is_imported())}
    malos = set()
    for t in tablas:
        for s in t.get_symbols():
            n = s.get_name()
            if (s.is_referenced() and (t is mod or s.is_global()) and n not in definidos
                    and n not in CONOCIDOS):
                malos.add(n)
    return sorted((_linea(arbol, n), n) for n in malos)


def _sin_definir_pyflakes(arbol, ruta):
    from pyflakes import checker, messages
    tipos = (messages.UndefinedName, messages.UndefinedExport, messages.UndefinedLocal)
    return sorted((m.lineno, m.message_args[0]) for m in checker.Checker(arbol, str(ruta)).messages
                  if isinstance(m, tipos))


def revisar(ruta):
    """Lista de textos de fallo para un archivo (vacia si esta bien)."""
    try:
        src = Path(ruta).read_text(encoding="utf-8-sig")
    except (OSError, UnicodeDecodeError) as e:
        return [f"{ruta}: no se puede leer ({e})"]
    try:
        arbol = ast.parse(src, str(ruta))
        compile(arbol, str(ruta), "exec")
    except SyntaxError as e:
        return [f"{ruta}:{e.lineno}: error de sintaxis: {e.msg}"]
    try:
        malos = _sin_definir_pyflakes(arbol, ruta)
    except ImportError:
        malos = _sin_definir_symtable(src, arbol, ruta)
    return [f"{ruta}:{l}: nombre sin definir: {n}" for l, n in malos]


def main(args):
    t0 = time.time()
    if args:
        rutas = [Path(a) for a in args]
    else:
        rutas = [R / h for h in HERRAMIENTAS if h not in OPCIONALES or (R / h).is_file()]
    fallos = [f for r in rutas for f in revisar(r)]
    for f in fallos[:20]:
        print("FALLO " + f)
    print(f"chequeo: {len(rutas)} archivos, {len(fallos)} fallos ({time.time() - t0:.1f} s)")
    return 1 if fallos else 0


def autoprueba():
    try:
        from infra import tmp  # temporales en E:, no en C:
        d = tmp("chequeo")
    except ImportError:
        import tempfile
        d = Path(tempfile.mkdtemp())
    casos = {
        "bien.py": "import os\nX = 1\ndef f(a):\n    global Y\n    Y = a\n    return [os.sep for _ in range(X)], Y, __file__\n"
                   "class C:\n    k = 2\n    def m(self):\n        return C.k\ntry:\n    import nada_de_nada as N\n"
                   "except ImportError:\n    N = None\n",
        "dormir.py": "import time\n\ndef esperar():\n    dormir(3)\n    time.sleep(1)\n",
        "sintaxis.py": "def f(:\n    pass\n",
        "clase.py": "class C:\n    k = 2\n    def m(self):\n        return k\n",
    }
    for n, t in casos.items():
        (d / n).write_text(t, encoding="utf-8")
    assert revisar(d / "bien.py") == [], revisar(d / "bien.py")
    assert revisar(d / "dormir.py") == [f"{d / 'dormir.py'}:4: nombre sin definir: dormir"], revisar(d / "dormir.py")
    assert "error de sintaxis" in revisar(d / "sintaxis.py")[0]
    assert revisar(d / "clase.py") == [f"{d / 'clase.py'}:4: nombre sin definir: k"], revisar(d / "clase.py")
    assert main([str(d / "bien.py")]) == 0 and main([str(d / "dormir.py")]) == 1
    print("chequeo autoprueba OK")
    return 0


if __name__ == "__main__":
    sys.exit(autoprueba() if sys.argv[1:] == ["--autoprueba"] else main(sys.argv[1:]))

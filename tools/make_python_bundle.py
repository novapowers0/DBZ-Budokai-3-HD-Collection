#!/usr/bin/env python3
"""Python portatil para la release de Windows (carpeta python/ junto a dbz3.exe).

Copia la instalacion OFICIAL de python.org que ejecuta este script (sys.base_prefix: las de
python.org son reubicables) sin lo que no usa nadie (documentacion, cabeceras, pruebas, IDLE),
con tkinter (ventana del Mod Kit) y SOLO los paquetes que piden el launcher y el Kit: numpy,
Pillow y scipy. Asi nadie tiene que instalar Python ni ejecutar pip.

Uso:  python tools/make_python_bundle.py <destino>        (crea <destino>/python)
      python tools/make_python_bundle.py --prueba <destino> (y lo comprueba en un proceso nuevo)
Licencias: PSF (Python), Tcl/Tk (BSD), numpy y scipy (BSD), Pillow (MIT-CMU): se copian sus
ficheros de licencia y un LEEME_LICENCIAS.txt.
"""
import os
import shutil
import subprocess
import sys

PACKAGES = ["numpy", "numpy.libs", "PIL", "pillow.libs", "scipy", "scipy.libs"]
SKIP_TOP = {"Doc", "include", "libs", "Scripts", "share", "Tools", "NEWS.txt"}
SKIP_LIB = {"test", "idlelib", "ensurepip", "site-packages", "turtledemo", "lib2to3", "venv", "pydoc_data"}
SKIP_ANY = {"__pycache__", "tests", "test"}     # numpy.testing si: lo importa scipy


def ignore(top):
    def f(d, names):
        rel = os.path.relpath(d, top)
        out = {n for n in names if n in SKIP_ANY or n.endswith((".pyc", ".pdb"))}
        if rel == ".":
            out |= {n for n in names if n in SKIP_TOP}
        elif rel == "Lib":
            out |= {n for n in names if n in SKIP_LIB}
        return out
    return f


def build(dest):
    src = sys.base_prefix
    py = os.path.join(dest, "python")
    if os.path.exists(py):
        raise SystemExit("ya existe %s: muevelo a un respaldo antes" % py)
    shutil.copytree(src, py, ignore=ignore(src))
    site_src = os.path.join(src, "Lib", "site-packages")
    site = os.path.join(py, "Lib", "site-packages")
    os.makedirs(site)
    lic = []
    for name in os.listdir(site_src):
        base = name.split("-")[0]
        keep = name in PACKAGES or (name.endswith(".dist-info") and base.lower() in ("numpy", "pillow", "scipy"))
        if keep:
            shutil.copytree(os.path.join(site_src, name), os.path.join(site, name),
                            ignore=lambda d, n: {x for x in n if x in SKIP_ANY or x.endswith(".pyc")})
            if name.endswith(".dist-info"):
                lic.append(name)
    missing = [p for p in ("numpy", "PIL", "scipy") if not os.path.isdir(os.path.join(site, p))]
    if missing:
        raise SystemExit("faltan paquetes en %s: %s (pip install numpy pillow scipy)" % (site_src, missing))
    with open(os.path.join(py, "LEEME_LICENCIAS.txt"), "w", encoding="utf-8") as f:
        f.write("Python %s (python.org, licencia PSF: LICENSE.txt) con Tcl/Tk (tcl/, licencia BSD),\n"
                "numpy y scipy (BSD) y Pillow (MIT-CMU). Sus licencias estan en Lib/site-packages/*.dist-info:\n"
                "%s\nSe incluye para que el launcher y el Mod Kit funcionen sin instalar nada.\n"
                % (sys.version.split()[0], "\n".join("  " + x for x in sorted(lic))))
    return py


def check(py):
    exe = os.path.join(py, "python.exe")
    code = ("import sys, numpy, PIL.Image, scipy.signal, scipy.spatial, scipy.optimize, tkinter, tomllib;"
            "assert sys.prefix.lower().startswith(%r.lower()), sys.prefix;"
            "print('python portatil OK', sys.version.split()[0], numpy.__version__, PIL.__version__)" % py)
    env = {k: v for k, v in os.environ.items() if not k.startswith("PYTHON")}
    r = subprocess.run([exe, "-I", "-c", code], capture_output=True, text=True, env=env)
    print(r.stdout.strip() or r.stderr.strip())
    return r.returncode == 0


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        raise SystemExit(__doc__)
    out = build(os.path.abspath(args[0]))
    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs)
    print("creado %s (%.0f MB)" % (out, size / 2**20))
    if "--prueba" in sys.argv and not check(out):
        sys.exit(1)

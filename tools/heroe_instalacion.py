# -*- coding: utf-8 -*-
"""HEROE-16 (12/11): arnes versionado del Heroe en instalacion real, sobre una copia fija (por defecto opus).

Cada caso prepara la copia SIN borrar nada (lo apartado va a <copia>\\_heroe16_bak\\<caso>, mismo volumen: rename),
corre el escenario `heroe_creador` (abre el creador y espera la vista previa 3D) y mira el registro del juego:

  frio            sin cache de vista (heroe_vista) -> el juego la genera: "vista previa 3D ... cargada"
  sin_python      cache fria y PATH sin Python (sin DBZ3_PYTHON) -> aviso "modelo no disponible", sin cierre
  sin_herramienta cache fria y sin cac/vista_heroe.py -> "vista previa ...: falta", sin cierre
  corrupta        malla de la cache truncada -> se regenera o se queda el retrato, sin cierre
  recuperacion    tras sin_herramienta: se devuelve el script, cache fria -> carga el modelo
  primera_vez     sin cac.toml/hub_heroe.toml/rpg.toml ni cache -> carga el modelo nativo

No cubre ACL de solo lectura (cambia permisos), datos B3 ausentes ni DLL LZX ausente (medidos aparte en solHERO).
Uso:  python tools/heroe_instalacion.py [--copia opus] [caso ...]
"""
import argparse
import os
import re
import subprocess
import sys
import time

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# sin_python y sin_herramienta NO valen con pruebas.py: su prep recopia vista_heroe.py y el juego halla otro Python (12/11)
CASOS = ["frio", "sin_python", "solo_lectura", "corrupta", "recuperacion", "primera_vez", "exportar"]


def _apartar(copia, caso, rutas):
    bak = os.path.join(copia, "_heroe16_bak", caso)
    os.makedirs(bak, exist_ok=True)
    movidas = []
    for rel in rutas:
        src = os.path.join(copia, rel)
        if os.path.exists(src):
            dst = os.path.join(bak, rel.replace("\\", "__").replace("/", "__"))
            os.rename(src, dst)
            movidas.append((src, dst))
    return movidas


def _devolver(movidas):
    # lo generado durante el caso se aparta junto a lo original (no se borra)
    for src, dst in movidas:
        if os.path.exists(src):
            os.rename(src, dst + "_generado_" + time.strftime("%H%M%S"))
        os.rename(dst, src)


def correr(copia_nombre, caso):
    copia = os.path.join(r"D:\DBZ3HD", copia_nombre)
    vistas = ["ingame_ui\\heroe_vista", "user_data\\dbz3\\heroe_vista"]
    script = "mod center hd\\cac\\vista_heroe.py"
    extra, env = [], []
    mov = []
    if caso == "frio":
        mov = _apartar(copia, caso, vistas)
    elif caso == "sin_python":
        mov = _apartar(copia, caso, vistas)
        env = ["DBZ3_TEST_SIN_PYTHON=1"]   # heroe_ui.cpp: pys.clear() (el PATH no vale: el juego prueba py -3 y el python portable)
    elif caso == "sin_herramienta":
        mov = _apartar(copia, caso, vistas + [script])
    elif caso == "corrupta":
        corruptas = []
        for d in vistas:
            for f in (os.listdir(os.path.join(copia, d)) if os.path.isdir(os.path.join(copia, d)) else []):
                if f.endswith(".malla"):
                    p = os.path.join(copia, d, f)
                    os.makedirs(os.path.join(copia, "_heroe16_bak", caso), exist_ok=True)
                    os.replace(p, os.path.join(copia, "_heroe16_bak", caso, d.replace("\\", "__") + "__" + f))
                    open(p, "wb").write(b"HMV1basura")      # malla invalida del mismo nombre
                    corruptas.append((p, os.path.join(copia, "_heroe16_bak", caso, d.replace("\\", "__") + "__" + f)))
    elif caso == "solo_lectura":   # ACL real: ingame_ui\heroe_vista sin escritura -> la cache va a user_data\dbz3\heroe_vista
        mov = _apartar(copia, caso, vistas)
        os.makedirs(os.path.join(copia, vistas[0]), exist_ok=True)
        subprocess.run(["icacls", os.path.join(copia, vistas[0]), "/deny", os.environ.get("USERNAME", "") + ":(W,AD,WD)"], capture_output=True)
    elif caso == "exportar":
        pass
    elif caso == "recuperacion":
        mov = _apartar(copia, caso, vistas)
    elif caso == "primera_vez":
        mov = _apartar(copia, caso, vistas + ["user_data\\dbz3\\cac.toml", "user_data\\dbz3\\hub_heroe.toml", "user_data\\dbz3\\rpg.toml"])
    cmd = [sys.executable, os.path.join(R, "tools", "pruebas.py"), "correr", copia_nombre, "heroe_exportar" if caso == "exportar" else "heroe_creador"]
    for e in env:
        cmd += ["--env", e]
    o = subprocess.run(cmd, capture_output=True, text=True, errors="replace", cwd=R)
    if caso == "solo_lectura":   # restaura la ACL y aparta lo que el juego dejo en la carpeta protegida
        subprocess.run(["icacls", os.path.join(copia, vistas[0]), "/remove:d", os.environ.get("USERNAME", "")], capture_output=True)
    _devolver(mov)
    if caso == "corrupta":
        for p, orig in corruptas:       # devuelve la malla buena (la corrupta queda junto, sin borrar)
            os.replace(p, p + ".corrupta_" + time.strftime("%H%M%S"))
            os.replace(orig, p)
    pr = [l for l in o.stdout.splitlines() if re.match(r"(PASS|FAIL)\s", l)]
    ev = re.findall(r"resultados: (\S+)", o.stdout)
    log = ""
    if ev:
        try:
            log = open(os.path.join(ev[-1], "dbz3.log"), encoding="utf-8", errors="replace").read()
        except OSError:
            pass
    cargada = bool(re.search(r"vista previa 3D .* cargada", log))
    aviso = re.findall(r"dbz3 heroe: (?:modelo no disponible|vista previa [^\r\n]*falta)[^\r\n]*", log)
    cierre = bool(re.search(r"access violation|UNREGISTERED|0xC000", log))
    pasa = bool(pr) and pr[0].startswith("PASS") and not cierre
    if caso in ("frio", "recuperacion", "primera_vez", "corrupta", "solo_lectura", "exportar"):
        pasa = pasa and cargada
    else:
        pasa = pasa and bool(aviso)
    print(f"{'PASS' if pasa else 'FAIL'} {caso}: {(pr or ['sin resultado'])[0][:90]} | cargada={cargada} avisos={len(aviso)} cierre={cierre} | {ev[-1] if ev else ''}", flush=True)
    return pasa


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--copia", default="opus")
    ap.add_argument("casos", nargs="*", default=CASOS)
    a = ap.parse_args()
    res = [correr(a.copia, c) for c in a.casos]
    print(f"heroe_instalacion: {sum(res)}/{len(res)} PASS")
    sys.exit(0 if all(res) else 1)

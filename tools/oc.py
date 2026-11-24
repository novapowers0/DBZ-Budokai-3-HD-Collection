r"""oc.py - una sola puerta para lanzar encargos en opencode (comando fijo: una sola regla de permiso).

  python tools\oc.py lanzar <encargo.md> --modelo sol|ds|<proveedor/modelo> [--nombre X]
  python tools\oc.py estado
  python tools\oc.py --autoprueba        (no lanza opencode de verdad)

`lanzar` arranca opencode en segundo plano desde la raiz del proyecto (sobrevive a quien lo lanza), con el log
en E:\DBZ3HD_trabajo\oc\oc_<nombre>.log (si ya existe, se anade al final) y una linea en oc\lanzados.jsonl
(nombre, pid, hora, modelo, encargo, log). No lanza un nombre que ya este vivo.
`estado`: una linea por nombre: vivo o no, minutos y la ultima linea del log.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from infra import _anadir_linea, _vivo
except ImportError:
    def _anadir_linea(ruta, texto):
        ruta.parent.mkdir(parents=True, exist_ok=True)
        with open(ruta, "a", encoding="utf-8") as f:
            f.write(texto)

    def _vivo(pid):
        r = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True, errors="replace")
        return str(pid) in r.stdout

R = Path(__file__).resolve().parent.parent
OC = Path(os.environ.get("DBZ3_OC", r"E:\DBZ3HD_trabajo\oc"))
MODELOS = {  # los que funcionaron el 11/11 (ESTADO_GLOBAL-00.md)
    "sol": ["-m", "openai/gpt-6.2-sol", "--variant", "high"],   # 21/11: usuario: nunca 6.1 existiendo 6.2
    "solm": ["-m", "openai/gpt-6.2-sol", "--variant", "medium"],
    "terra": ["-m", "openai/gpt-6.1-terra"],
    "ds": ["-m", "opencode-go/deepseek-v4.1-flash"],
    "sol62": ["-m", "openai/gpt-6.2-sol", "--variant", "medium"],   # 21/11: suscripcion OpenAI del usuario (oauth en opencode)
    "astra": ["-m", "openai/gpt-6.2-astra", "--variant", "xhigh"],   # 22/11: SOLO si el usuario lo pide (un bloqueo grande)
    "gem": ["-m", "google/gemini-4-hydrogen"],   # 17/11: claves de ~/.dbz3_gemini_keys, una por sesion (ver _clave_gemini)
    "hy": ["-m", "opencode/hy4-flash-free"],   # 16/11: gratis 2 semanas; bueno leyendo/auditando (probado), no para arreglos dificiles
}
AGY = os.environ.get("DBZ3_AGY", r"E:\Herramientas\agy\agy.exe")   # --modelo agy-<id de 'agy models'>
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def _opencode():
    """El exe de verdad, no el envoltorio .cmd/.ps1 de npm (cmd.exe estropea las comillas)."""
    exe = Path(os.environ.get("APPDATA", "")) / "npm" / "node_modules" / "opencode-ai" / "bin" / "opencode.exe"
    return [str(exe) if exe.is_file() else shutil.which("opencode") or "opencode"]


def _lanzados():
    """{nombre: ultima entrada} de lanzados.jsonl."""
    d = {}
    try:
        for l in (OC / "lanzados.jsonl").read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                e = json.loads(l)
                d[e["nombre"]] = e
            except (ValueError, KeyError):
                continue
    except OSError:
        pass
    return d


GEM_ESTADO = Path(r"E:\DBZ3HD_trabajo\gemini_clave.txt")   # el mismo indice que tools/gemini.py
CUOTA = re.compile(r"RESOURCE_EXHAUSTED|quota|API key not valid|429|retry in", re.I)  # Google acaba con "Please retry in 8h50m"
# ponytail: capa gratis de Hydrogen = 20 peticiones/dia por clave (17/11); un agente se las come en 15 min.


def _clave_gemini():
    """Una clave cada vez (usuario 17/11): la ultima buena; si la ultima sesion gem acabo por cuota, la siguiente.
    Va a OpenCode por OPENCODE_CONFIG_CONTENT (la variable GOOGLE_GENERATIVE_AI_API_KEY no gana a auth.json)."""
    ks = [k for k in dict.fromkeys(open(os.path.expanduser("~/.dbz3_gemini_keys"), encoding="utf-8").read().split())]
    try:
        i = int(GEM_ESTADO.read_text()) % len(ks)
    except (OSError, ValueError):
        i = 0
    ult = max((e for e in _lanzados().values() if e.get("modelo") == "gem"), key=lambda e: e.get("t", 0), default=None)
    if ult and not _vivo(ult["pid"]) and CUOTA.search(_ultima(ult["log"])):
        i = (i + 1) % len(ks)
    GEM_ESTADO.write_text(str(i))
    return i, ks[i]


def lanzar(encargo, modelo, nombre=None, exe=None):
    encargo = Path(encargo).resolve()
    if not encargo.is_file():
        raise SystemExit(f"oc: no existe el encargo {encargo}")
    nombre = nombre or encargo.stem
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", nombre):
        raise SystemExit(f"oc: nombre no valido '{nombre}' (solo letras, numeros, _ . -)")
    previo = _lanzados().get(nombre)
    if previo and _vivo(previo["pid"]):
        raise SystemExit(f"oc: '{nombre}' ya esta vivo (pid {previo['pid']}); usa otro --nombre")
    args = MODELOS.get(modelo, ["-m", modelo])
    texto = (f"Eres el carril {nombre}. Proyecto en el directorio actual (Windows; PowerShell 5.1 sin &&; rutas con "
             f"espacios entre comillas). Lee tu encargo en {encargo} y cumplelo hasta acabar.")
    cmd = (exe or _opencode()) + ["run"] + args + ["--title", nombre, texto]
    if modelo.startswith("agy-"):   # 22/11: Antigravity CLI con la suscripcion Gemini Pro del usuario (sin API, sin cobros)
        cmd = (exe or [AGY]) + ["-p", texto, "--model", modelo[4:], "--dangerously-skip-permissions"]
    OC.mkdir(parents=True, exist_ok=True)
    log = OC / f"oc_{nombre}.log"
    env, extra = None, ""
    if modelo == "gem" and exe is None:
        i, k = _clave_gemini()
        env = dict(os.environ, OPENCODE_CONFIG_CONTENT=json.dumps({"provider": {"google": {"options": {"apiKey": k}}}}))
        extra = f" clave {i + 1}"   # nunca la clave en el log
    with open(log, "ab") as f:
        f.write(f"\n=== oc.py {time.strftime('%Y-%m-%d %H:%M:%S')} {modelo}{extra} {encargo}\n".encode("utf-8"))
        f.flush()
        flags = 0x00000008 | 0x00000200 | 0x08000000 if os.name == "nt" else 0  # DETACHED, NEW_GROUP, NO_WINDOW
        p = subprocess.Popen(cmd, cwd=R, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT,
                             creationflags=flags, env=env)
    e = {"nombre": nombre, "pid": p.pid, "hora": time.strftime("%Y-%m-%d %H:%M:%S"), "t": time.time(),
         "modelo": modelo, "encargo": str(encargo), "log": str(log)}
    _anadir_linea(OC / "lanzados.jsonl", json.dumps(e, ensure_ascii=False) + "\n")
    print(f"oc: lanzado {nombre} (pid {p.pid}, {modelo}); log {log}")
    return p


def _ultima(log):
    try:
        with open(log, "rb") as f:
            f.seek(max(0, os.path.getsize(log) - 4096))
            lineas = [ANSI.sub("", l).strip() for l in f.read().decode("utf-8", "replace").splitlines()]
        return next((l for l in reversed(lineas) if l), "")
    except OSError:
        return "(sin log)"


def _inicio(pid):
    """Hora de arranque (epoch s) del proceso, o None. VEL r23: un pid reutilizado por otro programa salia 'vivo'."""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = ctypes.c_void_p
    h = k32.OpenProcess(0x1000, False, pid)
    if not h:
        return None
    try:
        t = [wintypes.FILETIME() for _ in range(4)]
        if not k32.GetProcessTimes(ctypes.c_void_p(h), *[ctypes.byref(x) for x in t]):
            return None
        return ((t[0].dwHighDateTime << 32 | t[0].dwLowDateTime) - 116444736000000000) / 1e7
    finally:
        k32.CloseHandle(ctypes.c_void_p(h))


def estado(ahora=None, imprimir=True):
    """Lo vivo y lo lanzado en las ultimas 24 h."""
    ahora = ahora or time.time()
    filas = []
    for n, e in sorted(_lanzados().items(), key=lambda x: x[1].get("t", 0)):
        vivo = _vivo(e["pid"])
        ini = _inicio(e["pid"]) if vivo else None
        if ini is not None and "t" in e and ini > e["t"] + 60:
            vivo = False                      # pid reutilizado por otro programa
        mins = (ahora - e.get("t", ahora)) / 60
        if not vivo and mins > 24 * 60:
            continue
        filas.append(f"{'vivo ' if vivo else 'fin  '} {n:<20} {mins:5.0f} min  {_ultima(e['log'])[:110]}")
    if imprimir:
        print("\n".join(filas) or "(nada lanzado con oc.py en 24 h)")
    return filas


def autoprueba():
    global OC
    viejo = OC
    try:
        from infra import tmp
        OC = tmp("oc")
    except ImportError:
        import tempfile
        OC = Path(tempfile.mkdtemp())
    try:
        enc = OC / "PRUEBA-1.md"
        enc.write_text("encargo de prueba", encoding="utf-8")
        falso = [sys.executable, "-c", "import sys,time; print('args', sys.argv[1:4]); sys.stdout.flush(); time.sleep(4)"]
        p = lanzar(enc, "ds", exe=falso)
        time.sleep(1.5)
        try:
            lanzar(enc, "ds", exe=falso)
            raise AssertionError("dejo lanzar dos veces el mismo nombre vivo")
        except SystemExit as ex:
            assert "ya esta vivo" in str(ex), ex
        f = estado()
        assert len(f) == 1 and f[0].startswith("vivo") and "PRUEBA-1" in f[0] and "'run', '-m', 'opencode-go" in f[0], f
        p.wait(10)
        f = estado()
        assert f[0].startswith("fin"), f
        try:
            lanzar(OC / "no_existe.md", "sol", exe=falso)
            raise AssertionError("lanzo un encargo que no existe")
        except SystemExit as ex:
            assert "no existe" in str(ex), ex
    finally:
        OC = viejo
    print("oc autoprueba OK")
    return 0


def main():
    sys.stdout.reconfigure(errors="replace")   # los logs traen flechas y acentos; en una tuberia cp1252 rompia
    if sys.argv[1:] == ["--autoprueba"]:
        return autoprueba()
    ap = argparse.ArgumentParser(description="lanzar y vigilar encargos de opencode")
    sp = ap.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("lanzar")
    s.add_argument("encargo")
    s.add_argument("--modelo", required=True, help="sol, ds o proveedor/modelo")
    s.add_argument("--nombre")
    sp.add_parser("estado")
    a = ap.parse_args()
    if a.cmd == "lanzar":
        lanzar(a.encargo, a.modelo, a.nombre)
    else:
        estado()
    return 0


if __name__ == "__main__":
    sys.exit(main())

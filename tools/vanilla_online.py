r"""Pruebas de la oferta "jugar vanilla" del online (dos copias v_a = anfitrion, v_b = invitado, en D:\DBZ3HD).

  python tools\vanilla_online.py prep          # crea v_a / v_b desde rb_a (robocopy /XJ) y el mod de prueba zz_vtest
  python tools\vanilla_online.py caso1         # mismos mods: juego normal, combate hasta el final
  python tools\vanilla_online.py caso2         # mods distintos, los dos aceptan: reinicio vanilla + combate sin desincronia
  python tools\vanilla_online.py caso3         # mods distintos, el invitado no acepta: no empieza nada ni se reinicia

Usa el Juego de pruebas.py (plazas, teclas, logs). Tras el reinicio el proceso es otro: se busca por la ruta del exe.
No toca mods de nadie: el mod zz_vtest es de la copia; "desactivado" = marcador .disabled (se aparta, no se borra).
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pruebas as P  # noqa: E402

BASE = P.BASE
COP = dict(zip(("host", "guest"), os.environ.get("VANILLA_COPIAS", "v_a,v_b").split(",")))   # VANILLA_COPIAS=v_c,v_d para otro par
APARTADO = Path(r"D:\_PARA_BORRAR_CLAUDE")
ENV = {"DBZ3_NET_LOCAL": "1", "DBZ3_NET_SUMAS": "1", "DBZ3_NET_NOFAIL": "1", "DBZ3_TEST_OCULTO": "1", "DBZ3_RB_SCRIPT": "1"}   # RB_SCRIPT: mandos por tick iguales en los dos (sin teclas del runner)   # ocultas: plazas 3-8, sin molestar al usuario
if os.environ.get("VANILLA_VISIBLE") == "1":
    ENV["DBZ3_TEST_OCULTO"] = "0"  # captura visual de la fila; usa solo las plazas 1-2

if os.environ.get("VANILLA_TECLAS") == "1":   # pasada con teclas reales del runner (sin guion de mandos por tick)
    ENV.pop("DBZ3_RB_SCRIPT")
if os.environ.get("VANILLA_PADLOG") == "1":   # R5: mandos aplicados por tick de cada PC en D:\tmp\padlog (comparar con tools\comparar_padlog.py)
    ENV["DBZ3_NET_PADLOG"] = "1"


for _kv in [x for x in os.environ.get("VANILLA_ENV", "").split(";") if "=" in x]:   # R6: variables extra para los dos juegos
    ENV[_kv.split("=",1)[0]] = _kv.split("=",1)[1]


def arbol_hash(d):
    h = hashlib.sha1()
    for f in sorted(Path(d).rglob("*")):
        if f.is_file():
            h.update(str(f.relative_to(d)).encode())
            h.update(f.read_bytes())
    return h.hexdigest()


def marcador(copia, activo):
    """activo=True: el mod zz_vtest cuenta en esa copia; False: lleva el marcador .disabled."""
    m = BASE / copia / "mods" / "zz_vtest" / ".disabled"
    if activo and m.exists():
        APARTADO.mkdir(parents=True, exist_ok=True)
        shutil.move(str(m), str(APARTADO / f"vtest_disabled_{copia}_{int(time.time())}"))
    elif not activo and not m.exists():
        m.write_text("disabled\n")


def apagar(nombres, activo):
    """R6 biseccion: VANILLA_OFF=mod1,mod2 pone/quita .disabled de esos mods en las DOS copias (activo=False -> apagados)."""
    for c in COP.values():
        for n in [x for x in nombres.split(",") if x]:
            m = BASE / c / "mods" / n / ".disabled"
            if not (BASE / c / "mods" / n).exists():
                continue
            if not activo and not m.exists():
                m.write_text("disabled"+chr(10))
            elif activo and m.exists():
                APARTADO.mkdir(parents=True, exist_ok=True)
                shutil.move(str(m), str(APARTADO / f"off_{c}_{n}_{int(time.time())}"))


def estado_marcador(copia, nombre):
    p = BASE / copia / "mods" / nombre / ".disabled"
    return p.read_bytes() if p.is_file() else None


def restaurar_marcador(copia, nombre, bytes_previos):
    """Restaura el marcador previo sin borrar: al habilitar, aparta el marker a cuarentena."""
    mod = BASE / copia / "mods" / nombre
    if not mod.is_dir():
        return
    marker = mod / ".disabled"
    if bytes_previos is not None:
        if marker.is_file() and marker.read_bytes() == bytes_previos:
            return
        if marker.exists():
            APARTADO.mkdir(parents=True, exist_ok=True)
            shutil.move(str(marker), str(APARTADO / f"vtest_marker_replaced_{copia}_{nombre}_{int(time.time())}"))
        marker.write_bytes(bytes_previos)
    elif marker.exists():
        APARTADO.mkdir(parents=True, exist_ok=True)
        shutil.move(str(marker), str(APARTADO / f"vtest_disabled_{copia}_{nombre}_{int(time.time())}"))


def prep():
    for c in COP.values():
        if not (BASE / c).exists():
            subprocess.run(["robocopy", str(BASE / "rb_a"), str(BASE / c), "/E", "/XJ", "/XD", "logs", "user_data", "/NFL", "/NDL", "/NJH", "/NJS"])
        (BASE / c / "mods" / "zz_vtest").mkdir(parents=True, exist_ok=True)
        (BASE / c / "mods" / "zz_vtest" / "datos.bin").write_bytes(b"vtest")
        for dll in ("rexruntime.dll", "rexgpu-xenos.dll", "dbz3.exe"):
            shutil.copy2(P.R / "out" / "build" / "win-amd64-release" / dll, BASE / c / dll)
        if not (BASE / c / "user_data").exists():
            shutil.copytree(BASE / "rb_a" / "user_data", BASE / c / "user_data")
    print("prep ok")


def proc_nuevo(copia, viejo_pid):
    exe = str(BASE / copia / "dbz3.exe").lower().replace("/", "\\")
    ps = ("Get-CimInstance Win32_Process -Filter \"Name='dbz3.exe'\" | ForEach-Object { \"$($_.ProcessId)|$($_.ExecutablePath)\" }")
    for _ in range(60):
        out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout
        for ln in out.splitlines():
            pid, _, path = ln.partition("|")
            if pid.isdigit() and int(pid) != viejo_pid and path.lower() == exe:
                return int(pid)
        time.sleep(1)
    return None


def adoptar(j, pid):
    """El juego se reinicio solo: el Juego pasa a mandar sobre el proceso nuevo."""
    class Pr:
        def __init__(self, pid):
            self.pid = pid
        def poll(self):
            h = P.K32.OpenProcess(0x1000, False, self.pid)
            if not h:
                return 0
            import ctypes
            code = ctypes.c_ulong()
            P.K32.GetExitCodeProcess(h, ctypes.byref(code))
            P.K32.CloseHandle(h)
            return None if code.value == 259 else code.value
        def wait(self, t=None):
            while self.poll() is None:
                time.sleep(0.2)
    j.proc = Pr(pid)
    j.h = P.K32.OpenProcess(0x0410, False, pid)
    j.base_real()
    threading.Thread(target=P._ocultar_ventanas, args=(j.proc,), daemon=True).start()   # el hijo hereda la variable pero su ventana la oculta la prueba


def puerto_par():
    """Regla de R5 (R4/correr_par.py): 20000 + crc32("<host>,<guest>") % 4000 * 2, para que parejas en paralelo no choquen."""
    import zlib
    return 20000 + zlib.crc32((COP["host"] + "," + COP["guest"]).encode()) % 4000 * 2


def lanzar(rol, vt):
    j = P.Juego(COP[rol], f"vanilla_{rol}")
    pt = str(puerto_par())   # sin proxy: el invitado llama directo al puerto del anfitrion
    env = dict(ENV, DBZ3_NET_AUTO=rol, DBZ3_VANILLA_TEST=vt, DBZ3_NET_PORT=pt, DBZ3_NET_JOIN_PORT=pt)
    visible_role = os.environ.get("VANILLA_VISIBLE_ROLE", "").lower()
    if visible_role in ("host", "guest"):
        env["DBZ3_TEST_OCULTO"] = "0" if rol == visible_role else "1"
    j.lanzar(env)
    return j


def a_menu(js):
    th = [threading.Thread(target=j.arrancar, args=(150,)) for j in js]
    [t.start() for t in th]
    [t.join() for t in th]


def a_fila_online(j):
    """Abre el panel real del Duelo donde se dibuja JUGAR VANILLA."""
    P._a_duelo(j)
    for _ in range(4):  # 1P VS COM ... COM VS COM -> fila ONLINE
        j.tecla("S", 700)
    j.tecla("Space", 1500)


def todo_log(j):
    """Texto de TODOS los logs nuevos de la copia (tras un reinicio el juego escribe otro archivo)."""
    out = []
    for f, v in j._logs().items():
        if j.logs_antes is None or j.logs_antes.get(f) != v:
            try:
                out.append(Path(f).read_text(encoding="utf-8", errors="replace"))
            except OSError:
                pass
    return chr(10).join(out)


def esperar(js, regex, seg):
    fin = time.time() + seg
    while time.time() < fin:
        if all(re.search(regex, todo_log(j)) for j in js):
            return True
        time.sleep(1)
    return False


def combate(js, etiqueta):
    ok = esperar(js, r"dbz3 rb: auto: empezando", 150)
    ok = ok and esperar(js, r"dbz3 rb: auto: combate pedido \(ok\)", 40)
    print(f"[{etiqueta}] lockstep + combate pedido en los dos: {ok}")
    if not ok:
        return False
    fin = time.time() + 40
    i = 0
    while time.time() < fin and all(j.vivo() for j in js):
        if ENV.get("DBZ3_RB_SCRIPT") != "1":
            for j in js:
                j.tecla(P._RB_GUION[i % len(P._RB_GUION)], 0)
        i += 1
        time.sleep(0.25)
    ok = esperar(js, r"tag: combate acabado|hub: fin del combate nativo", 150)
    res = True
    for j in js:
        t = j.texto_log()
        ult = re.findall(r"auto tick (\d+): sumas comparadas (\d+) distintas (\d+) \(RNG: (\w+)", t)
        # criterio: RNG ok y 0 sumas distintas (R5: la suma es estado real de juego, no cosmetico)
        bueno = bool(ult) and int(ult[-1][2]) == 0 and ult[-1][3] == "ok"
        res = res and bueno
        print(f"[{etiqueta}] {j.nombre}: ultima medida {ult[-1] if ult else 'sin medida'} huella: {re.findall(r'huella de (\d+) mods', t)[-1:]}")
    print(f"[{etiqueta}] combate acabado en los dos: {ok}; sin desincronia: {res}")
    return ok and res


def caso(n):
    hash_antes = {c: arbol_hash(BASE / c / "mods") for c in COP.values()}
    off = [x for x in os.environ.get("VANILLA_OFF", "").split(",") if x]
    estado_previo = {(c, nombre): estado_marcador(c, nombre) for c in COP.values()
                     for nombre in ["zz_vtest", *off]}
    marcador(COP["host"], True)
    marcador(COP["guest"], n == 1)
    apagar(",".join(off), False)
    vt = {"host": "accept", "guest": "accept" if n != 3 else "decline"}
    if n == 4:
        vt = {"host": "accept_ui", "guest": "accept_ui"}
    js = [lanzar(r, vt[r]) for r in ("host", "guest")]
    try:
        a_menu(js)
        if n == 1:
            ok = combate(js, "caso1")
        elif n in (2, 4):
            ok = esperar(js, r"el otro PC propone jugar vanilla", 120)
            print(f"[caso2] oferta recibida en los dos: {ok}")
            viejos = [j.proc.pid for j in js]
            ok = ok and esperar(js, r"reiniciando en modo vanilla", 60)
            print(f"[caso2] los dos se reinician en vanilla: {ok}")
            if ok:
                for j, v, rol in zip(js, viejos, ("host", "guest")):
                    nuevo = proc_nuevo(COP[rol], v)
                    print(f"[caso2] {rol}: proceso nuevo {nuevo}")
                    if not nuevo:
                        ok = False
                        continue
                    j.logs_antes = j._logs()   # el log vanilla es otro archivo
                    adoptar(j, nuevo)
                if ok:
                    a_menu(js)
                    if n == 2:
                        ok = combate(js, "caso2")
                    else:   # caso4: camino real de la pantalla (temporal + reconexion automatica)
                        ok = esperar(js, r"sesion vanilla reconectada", 200)
                        print(f"[caso4] fichero temporal leido y reconectados solos: {ok}")
        else:
            if n == 3 and os.environ.get("VANILLA_EVIDENCIA") == "1":
                # DBZ3_NET_AUTO crea la sesión desde el menú principal; entra además
                # en OnlinePanel para capturar la fila (no solo el menú principal).
                for j in js:
                    a_fila_online(j)
            ok = esperar([js[0]], r"mods distintos", 120)
            if ok and os.environ.get("VANILLA_EVIDENCIA") == "1":
                # Caso3 deja la oferta visible (el invitado declina por hook); guarda el HUD de ambas copias
                # antes de esperar el tiempo de seguridad que confirma que no arranca ni se reinicia.
                # VANILLA_EVIDENCIA_ROL=guest limita la captura al jugador visible si sólo hay una ventana libre.
                target = os.environ.get("VANILLA_EVIDENCIA_ROL", "").lower()
                for j, rol in zip(js, ("host", "guest")):
                    if not target or target == rol:
                        print(f"[caso{n}] captura de fila vanilla {rol}: {j.captura('vanilla_jugar_row')}", flush=True)
            time.sleep(45)
            t = [todo_log(j) for j in js]
            reinicio = any("reiniciando en modo vanilla" in x for x in t)
            lucha = any("auto: empezando" in x for x in t)
            print(f"[caso3] mods distintos detectado: {ok}; reinicio: {reinicio}; pelea empezada: {lucha}")
            ok = ok and not reinicio and not lucha
    finally:
        for j in js:
            try:
                j.cerrar()
            except Exception as e:  # noqa: BLE001
                print("cerrar:", e)
        for rol in COP:   # un reinicio que no se adopto deja el proceso vanilla vivo: se cierra el de ESTA copia
            while (pid := proc_nuevo(COP[rol], -1)) is not None:
                P.cerrar_pid(pid, 2)
                time.sleep(2)
    # restauración exacta de los marcadores previos en las copias de prueba; nada se borra.
    for (copia, nombre), bytes_previos in estado_previo.items():
        restaurar_marcador(copia, nombre, bytes_previos)
    despues = {c: arbol_hash(BASE / c / "mods") for c in COP.values()}
    print(f"[caso{n}] arbol de mods v_a igual: {hash_antes[COP['host']] == despues[COP['host']]}; "
          f"mods_vanilla en la copia: {[str(p) for p in (BASE / COP['host']).glob('mods_vanilla*')]}")
    print(f"CASO{n} {'PASS' if ok else 'FAIL'}")
    return ok


if __name__ == "__main__":
    c = sys.argv[1] if len(sys.argv) > 1 else ""
    if c == "prep":
        prep()
    elif c == "exe":   # exe <ruta>: pone ese dbz3.exe en v_a y v_b (para comparar con una build anterior)
        for cp in COP.values():
            shutil.copy2(sys.argv[2], BASE / cp / "dbz3.exe")
        print("exe puesto:", sys.argv[2])
    elif c in ("caso1", "caso2", "caso3", "caso4"):
        sys.exit(0 if caso(int(c[-1])) else 1)
    else:
        print(__doc__)

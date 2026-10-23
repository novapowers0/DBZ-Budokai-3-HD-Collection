"""Comprobacion sin juego de tools/pruebas.py:  python tools\\test_pruebas.py"""
import re
import os
import io
import subprocess
import sys
import tempfile
import tomllib
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pruebas as P  # noqa: E402

# escenarios integrados y de la bateria: todos cargan y piden los hooks que tocan
for nombre in P.ESCENARIOS:
    desc, fn, env = P.cargar_escenario(nombre, {"rol": "host"} if nombre == "online_rb" else {})   # online_rb exige --par rol
    assert desc and callable(fn), nombre
assert P.cargar_escenario("batalla", {})[2] == {"DBZ3_TEST_BATTLE": "0,7,2", "DBZ3_TEST_RINGOUT": "1"}
assert P.cargar_escenario("batalla", {"ids": "3,4,1"})[2]["DBZ3_TEST_BATTLE"] == "3,4,1"
assert P.cargar_escenario("tag_swap", {"swap": "0"})[2] == {"DBZ3_TAG_SLOW": "3", "DBZ3_TEST_KO": "1"}
assert P.cargar_escenario("campana", {})[2] == {"DBZ3_TEST_RINGOUT": "1"}
assert P.cargar_escenario("online_rb", {"rol": "host"})[2]["DBZ3_NET_AUTO"] == "host"
assert P.cargar_escenario("online_rb", {"rol": "guest"})[2]["DBZ3_NET_AUTO"] == "guest"
for familia in ("campana", "du_continuar", "tag_swap", "heroe_fichas"):
    assert P._rapido(familia, {}) == {"DBZ3_TEST_TURBO": "3", "DBZ3_TEST_OCULTO": "1"}, familia
assert P._rapido("r2_nave", {}) == {}  # igualdad no aprobada: conserva velocidad normal
assert all(i in ("freezer_cap2", "perfil_nuevo") for _, i, _ in P.SMOKE)
assert {s[0] for s in P.SMOKE} <= set(P.ESCENARIOS)

# .toml: pasos validos se aceptan, un verbo desconocido se rechaza antes de tocar el juego
with tempfile.TemporaryDirectory() as d:
    ok = Path(d) / "ok.toml"
    ok.write_text('env = ["DBZ3_TAG_SLOW=2"]\npasos = ["tecla Space 100", "esperar_pantalla 5,7 3"]\n')
    assert P.cargar_escenario(str(ok), {})[2] == {"DBZ3_TAG_SLOW": "2"}
    malo = Path(d) / "malo.toml"
    malo.write_text('pasos = ["saltar 3"]\n')
    try:
        P.cargar_escenario(str(malo), {})
        raise AssertionError("verbo desconocido aceptado")
    except SystemExit:
        pass

# deteccion de fallos: los tres mensajes reales del runtime (main.cpp:976, xmemory.cpp:547/628)
for l in ["UNHANDLED EXCEPTION: Code=0xC0000005 Addr=0x1",
          "Unhandled guest access violation: read of guest 0x00000000 (host 0x1) on thread 0x2",
          "Unhandled guest physical write fault: guest=1 host=2 phys=3"]:
    assert re.search(P.ERRORES, l), l
assert not re.search(P.ERRORES, "dbz3: perf fps=60.0 frames=300")
log = "\n".join([f"linea {i}" for i in range(40)] + ["dbz3: perf fps=1", "UNHANDLED EXCEPTION: Code=0xC0000005"])
ext = P.informe_fallo(log, ["UNHANDLED EXCEPTION: Code=0xC0000005"])
assert ext.startswith("-- errores --") and "perf fps" not in ext and "linea 39" in ext and "linea 5\n" not in ext
assert P.informe_fallo("", []) == "(sin log del juego)"

# tabla: una fila por prueba y el recuento final
t = P.tabla([{"ok": True, "nombre": "smoke_a/menu", "dur": 20.0, "menu": 12.0, "cierre": 1.0, "msg": ""},
             {"ok": False, "nombre": "smoke_b/batalla", "dur": 0.0, "msg": "copia ocupada"}], 21)
assert "PASS  smoke_a/menu" in t and "FAIL  smoke_b/batalla" in t and "1/2 PASS en 21 s" in t
# X0e: DBZ3_TEST_ESCENA reparte a los ganchos DBZ3_TEST_* que ya existen
env, rx, av = P.resolver_escena("b2:fase3:evento12")
assert env["DBZ3_TEST_MISIONES"] == "1,auto,reset,desde=2" and rx == "tablero b2: fase 3 leida" and env["DBZ3_TEST_B2_EVENTO"] == "12" and env["DBZ3_TEST_B2_ORDENES"] == "1"
assert P.resolver_escena("iw:mini:0x7D9")[0]["DBZ3_TEST_IWMINI"] == "0x7D9,auto"
assert P.resolver_escena("satan:ronda2")[0]["DBZ3_TEST_PASARELA"] == "auto,seg=8,desde=2"
assert P.resolver_escena("b2:nave:prueba2")[0]["DBZ3_TEST_NAVE_PRUEBA"] == "2"
assert P.resolver_escena("b2:fase1")[0]["DBZ3_TEST_MISIONES"] == "1,auto,reset"
for malo in ("", "b2:fase10", "iw:mini:xyz", "foo:bar", "satan:otra"):
    try:
        P.resolver_escena(malo)
        raise AssertionError(f"escena mala aceptada: {malo!r}")
    except SystemExit:
        pass
# X0g: comparacion de firmas del turbo (cronometros con tolerancia, el resto exacto)
assert P.comparar_firmas(["a 1 s", "b 2"], ["a 1 s", "b 2"])[0] == "igual"
assert P.comparar_firmas(["aro 3 quedando 298.5 s"], ["aro 3 quedando 298.6 s"])[0] == "tolerancia"
assert P.comparar_firmas(["aro 3 quedando 290.5 s"], ["aro 3 quedando 298.6 s"])[0] == "distinto"
assert P.comparar_firmas(["gana el lado 1"], ["gana el lado 2"])[0] == "distinto"
assert P.comparar_firmas(["x"], ["x", "y"])[0] == "distinto"

# X0 INF: plaza huerfana se mueve a cuarentena y una plaza con PID vivo se conserva.
with tempfile.TemporaryDirectory() as d:
    raiz = Path(d)
    old_plazas, old_cuarentena = P.PLAZAS, P.PLAZAS_HUERFANAS
    try:
        P.PLAZAS = raiz / "_juego"
        P.PLAZAS_HUERFANAS = raiz / "cuarentena"
        P.PLAZAS.mkdir()
        muerta = P.PLAZAS / "plaza1"
        muerta.mkdir()
        (muerta / "quien.txt").write_text("prueba pid 4294967294\n", encoding="utf-8")
        j = object.__new__(P.Juego)
        j.nombre, j.dest = "test_reserva", raiz
        j.plaza = None
        assert j._tomar_plaza()                    # recupera y vuelve a reservar plaza1
        assert j.plaza == muerta and int(re.search(r"\bpid\s+(\d+)", (muerta / "quien.txt").read_text()).group(1)) == os.getpid()
        assert len(list(P.PLAZAS_HUERFANAS.glob("plazas_huerfanas_*"))) == 1

        viva = P.PLAZAS / "plaza2"
        viva.mkdir()
        (viva / "quien.txt").write_text(f"prueba pid {os.getpid()}\n", encoding="utf-8")
        assert not P._recuperar_plaza_huerfana(viva)
        assert viva.is_dir() and (viva / "quien.txt").exists()
        assert P._vivo(os.getpid(), P.ts())   # un proceso Python vivo sigue teniendo la plaza
    finally:
        P.PLAZAS, P.PLAZAS_HUERFANAS = old_plazas, old_cuarentena

# INF2 #11 R6: tres corridas reclaman a la vez plaza1/plaza2; colisión EEXIST es ocupación, nunca excepción.
with tempfile.TemporaryDirectory() as d:
    raiz = Path(d)
    old_plazas, old_cuarentena = P.PLAZAS, P.PLAZAS_HUERFANAS
    try:
        P.PLAZAS = raiz / "_juego"
        P.PLAZAS_HUERFANAS = raiz / "cuarentena"
        P.PLAZAS.mkdir()
        barrera = threading.Barrier(3)
        errores = []

        def reclamar(i):
            j = object.__new__(P.Juego)
            j.nombre, j.dest, j.plaza = f"simultanea_{i}", raiz, None
            barrera.wait(timeout=5)
            fin = time.time() + 8
            while time.time() < fin:
                try:
                    if j._tomar_plaza():
                        time.sleep(0.03)
                        j._soltar_plaza()
                        return True
                except FileExistsError as e:
                    errores.append(repr(e))
                    return False
                time.sleep(0.005)
            return False

        with ThreadPoolExecutor(3) as pool:
            resultados = list(pool.map(reclamar, range(3)))
        assert all(resultados), resultados
        assert not errores, errores
        assert not list(P.PLAZAS.glob("plaza*"))

        # Reproducir la misma carrera con tres procesos/ PIDs independientes, no sólo hilos.
        plazas_mp, cuarentena_mp = raiz / "_juego_mp", raiz / "cuarentena_mp"
        go = raiz / "salir_a_reservar"
        worker = r'''import sys,time
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import pruebas as P
P.PLAZAS=Path(sys.argv[2]); P.PLAZAS_HUERFANAS=Path(sys.argv[3]); P.PLAZAS.mkdir(parents=True,exist_ok=True)
go=Path(sys.argv[4]); deadline=time.time()+15
while not go.exists() and time.time()<deadline: time.sleep(0.002)
j=object.__new__(P.Juego); j.nombre=sys.argv[5]; j.dest=Path(sys.argv[3]); j.plaza=None
deadline=time.time()+12
while time.time()<deadline:
    if j._tomar_plaza():
        time.sleep(0.12); j._soltar_plaza(); sys.exit(0)
    time.sleep(0.005)
sys.exit(2)
'''
        procesos = [subprocess.Popen([sys.executable, "-c", worker, str(Path(__file__).resolve().parent),
                                      str(plazas_mp), str(cuarentena_mp), str(go), f"corrida_{i}"],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace")
                    for i in range(3)]
        go.write_text("go\n", encoding="ascii")
        resultados_mp = [p.communicate(timeout=20) + (p.returncode,) for p in procesos]
        assert all(rc == 0 for _, _, rc in resultados_mp), resultados_mp
        assert not list(plazas_mp.glob("plaza*"))
    finally:
        P.PLAZAS, P.PLAZAS_HUERFANAS = old_plazas, old_cuarentena

# INF2 #9: un PASS con la misma clave devuelve sus artefactos sin invocar el juego.
with tempfile.TemporaryDirectory() as d:
    raiz = Path(d)
    old_base, old_pruebas = P.BASE, P.PRUEBAS
    old_key, old_read, old_prepare, old_launch = P._cache_key, P._leer_cache, P.preparar, P.Juego.lanzar
    try:
        P.BASE, P.PRUEBAS = raiz / "copias", raiz / "pruebas"
        P.BASE.mkdir()
        source = P.PRUEBAS / "cache_origen"
        source.mkdir(parents=True)
        (source / "dbz3.log").write_text("dbz3: prueba hash=clave turbo_solicitado=3 turbo_real=3.0\n", encoding="utf-8")
        (source / "escenario.toml").write_text('escenario = "menu"\n', encoding="utf-8")
        (source / "captura.png").write_bytes(b"png")
        P._cache_key = lambda *a, **k: "clave"
        P._leer_cache = lambda key: {"ok": True, "run": str(source), "dur": 8.0, "menu": 4.0,
                                     "turbo_real": 3.0} if key == "clave" else None
        P.preparar = lambda *a, **k: None
        def no_lanzar(*_a, **_k):
            raise AssertionError("la caché lanzó el juego")
        P.Juego.lanzar = no_lanzar
        r = P.correr("copia", "menu", forzar=False)
        assert r["ok"] and r["cache"] and "cache=PASS" in (r["run"] / "dbz3.log").read_text()
        assert (r["run"] / "captura.png").is_file() and (r["run"] / "escenario.toml").is_file()
    finally:
        P.BASE, P.PRUEBAS = old_base, old_pruebas
        P._cache_key, P._leer_cache, P.preparar, P.Juego.lanzar = old_key, old_read, old_prepare, old_launch

# INF2: caché sólo reutiliza PASS, cambia con binario/config/mods y guarda evidencia TOML+PNG.
with tempfile.TemporaryDirectory() as d:
    raiz = Path(d)
    old_pruebas, old_inst = P.PRUEBAS, P.INST
    P.PRUEBAS, P.INST = raiz / "pruebas", raiz / "instantaneas"
    try:
        copia = raiz / "copia"
        (copia / "mods" / "ejemplo").mkdir(parents=True)
        (copia / "dbz3.exe").write_bytes(b"exe-a")
        mf = copia / "mods" / "ejemplo" / "manifest.txt"
        mf.write_text("name=Ejemplo\n", encoding="utf-8")
        cfg = raiz / "escena.toml"
        cfg.write_text('descripcion = "caso"\npasos = ["esperar 1"]\n', encoding="utf-8")
        clave = P._cache_key(copia, cfg, {}, {"DBZ3_TEST_TURBO": "3"}, None)
        assert clave == P._cache_key(copia, cfg, {}, {"DBZ3_TEST_TURBO": "3"}, None)
        mf.write_text("name=Ejemplo distinto\n", encoding="utf-8")
        assert clave != P._cache_key(copia, cfg, {}, {"DBZ3_TEST_TURBO": "3"}, None)
        mf.write_text("name=Ejemplo\n", encoding="utf-8")
        clave = P._cache_key(copia, cfg, {}, {"DBZ3_TEST_TURBO": "3"}, None)
        (copia / "dbz3.exe").write_bytes(b"exe-b")
        assert clave != P._cache_key(copia, cfg, {}, {"DBZ3_TEST_TURBO": "3"}, None)
        (copia / "dbz3.exe").write_bytes(b"exe-a")
        run = raiz / "run"
        run.mkdir()
        for n in ("dbz3.log", "escenario.toml", "captura.png"):
            (run / n).write_bytes(b"evidencia")
        passed = {"ok": True, "run": run, "dur": 12.0, "menu": 4.0}
        P._guardar_cache(clave, passed, 3, 2.8)
        assert P._leer_cache(clave)["turbo_real"] == 2.8
        P._guardar_cache(clave + "fail", dict(passed, ok=False), 3, 2.8)
        assert P._leer_cache(clave + "fail") is None
        registro = raiz / "registro"
        P._escenario_toml(registro, "batalla", "combate", {"id": "2"}, {"TURBO": "3"}, "freezer_cap2")
        assert tomllib.loads((registro / "escenario.toml").read_text())["escenario"] == "batalla"
        P._png_sin_captura(registro / "zz_sin_captura.png", "test sin juego")
        assert (registro / "zz_sin_captura.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert "test sin juego" in (registro / "zz_sin_captura.txt").read_text()
    finally:
        P.PRUEBAS, P.INST = old_pruebas, old_inst

# Una prueba que falla antes de lanzar deja siempre log, PNG (aviso de captura no disponible) y TOML.
with tempfile.TemporaryDirectory() as d:
    old_base, old_pruebas = P.BASE, P.PRUEBAS
    try:
        P.BASE = Path(d) / "copias"
        P.PRUEBAS = Path(d) / "evidencias"
        P.BASE.mkdir()
        with redirect_stdout(io.StringIO()):
            r = P.correr("copia_test", "escenario_que_no_existe", forzar=True)
        assert not r["ok"]
        run = Path(r["run"])
        assert (run / "dbz3.log").is_file() and (run / "escenario.toml").is_file()
        assert (run / "zz_sin_captura.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert (run / "fallo.txt").is_file()
    finally:
        P.BASE, P.PRUEBAS = old_base, old_pruebas
print("test_pruebas: OK")

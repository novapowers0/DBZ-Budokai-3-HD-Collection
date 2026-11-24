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
_TMP_TEST = tempfile.mkdtemp(prefix="test_pruebas_")   # VEL r23: ni cola ni respaldos reales
os.environ.setdefault("DBZ3_AISLAMIENTO_DIR", _TMP_TEST + "/aisl")
os.environ.setdefault("DBZ3_COLA_DIR", _TMP_TEST + "/cola")
import pruebas as P  # noqa: E402

# escenarios integrados y de la bateria: todos cargan y piden los hooks que tocan
# valores ficticios para los escenarios que exigen --par; solo se usan para armar el entorno, nunca se lanza el juego
PAR_FICTICIO = {"online_rb": {"rol": "host"}, "heroe_pieza": {"pelo": "0"}, "online_mm": {"url": "http://127.0.0.1:9"}}
for nombre in P.ESCENARIOS:
    par = PAR_FICTICIO.get(nombre, {})
    desc, fn, env, prep = P.cargar_escenario(nombre, par)   # online_rb exige rol; heroe_pieza, una pieza; online_mm, url
    assert desc and callable(fn), nombre
assert P.cargar_escenario("batalla", {})[2] == {"DBZ3_TEST_BATTLE": "0,7,2", "DBZ3_TEST_RINGOUT": "1"}
assert P.cargar_escenario("batalla", {"ids": "3,4,1"})[2]["DBZ3_TEST_BATTLE"] == "3,4,1"
assert P.cargar_escenario("tag_swap", {"swap": "0"})[2] == {"DBZ3_TAG_SLOW": "3", "DBZ3_TEST_KO": "1"}
assert P.cargar_escenario("campana", {})[2] == {"DBZ3_TEST_RINGOUT": "1"}
# DS173: el vuelo reenvía la dificultad por la misma vía que iw_minijuego (g_test de src/iw_minijuegos.cpp).
assert P.cargar_escenario("_r12_vuelo", {"clave": "0x7E0", "dificultad": "1"})[2]["DBZ3_TEST_IWMINI_DIFICULTAD"] == "1"
assert P.cargar_escenario("_r12_vuelo", {})[2]["DBZ3_TEST_IWMINI_DIFICULTAD"] == ""
assert P.cargar_escenario("online_rb", {"rol": "host"})[2]["DBZ3_NET_AUTO"] == "host"
assert P.cargar_escenario("online_rb", {"rol": "guest"})[2]["DBZ3_NET_AUTO"] == "guest"
for familia in ("campana", "du_continuar", "tag_swap", "heroe_fichas"):
    assert P._rapido(familia, {}) == {"DBZ3_TEST_TURBO": "3", "DBZ3_TEST_OCULTO": "1"}, familia
assert P._rapido("r2_nave", {}) == {}  # igualdad no aprobada: conserva velocidad normal
assert all(i in ("freezer_cap2", "perfil_nuevo") or (i is None and str(e).endswith(".toml")) for e, i, _ in P.SMOKE)   # PM-25: guiones Rogue autoaislados declaran None
assert {s[0] for s in P.SMOKE if not s[0].endswith(".toml")} <= set(P.ESCENARIOS)

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
        (P.BASE / "copia").mkdir(parents=True)   # INFRA-02: preparar() va de mentira y la copia debe existir
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

# INFRA-02: clases de fallo vistas en las baterias reales (1-7/11) y un solo tope de plazas.
assert P.clase_fallo("no llego al menu principal en 120.0 s (pantalla 0)") == "herramienta"
assert P.clase_fallo("captura '02_tagteam' sin respuesta del juego") == "herramienta"
assert P.clase_fallo("no se abrio el menu de capsulas del jugador 1") == "juego"
assert P.clase_fallo("no llego a la pantalla [7] en 40 s (estaba en 6)") == "juego"
assert P.clase_fallo("el juego murio (codigo 0x00000001). el juego se cerro al arrancar") == "juego"
assert P.MAX_PARALELO == P.MAX_PARALELO_N1 == P.TOPE_JUEGOS

# INFRA-02: reintento unico solo para la herramienta, visible en la tabla y anotado; DBZ3_PRUEBAS_REINTENTO=0 lo apaga.
with tempfile.TemporaryDirectory() as d:
    old = P.BASE, P.REINTENTOS, P._correr
    llamadas = []

    def falso(fallos):
        def _c(copia, esc, *a):
            llamadas.append(esc)
            msg = fallos[len(llamadas) - 1] if len(llamadas) <= len(fallos) else ""
            return {"ok": not msg, "nombre": f"{copia}/{esc}", "dur": 1.0, "menu": 1.0, "cierre": 0.0, "msg": msg,
                    "run": Path(d) / f"run{len(llamadas)}"}
        return _c
    try:
        P.BASE, P.REINTENTOS = Path(d), Path(d) / "_reintentos.jsonl"
        with redirect_stdout(io.StringIO()):
            P._correr = falso(["no llego al menu principal en 120.0 s (pantalla 0)"])
            r = P.correr("c", "menu")
            assert r["ok"] and r["reintentado"] and len(llamadas) == 2 and r["run_previo"] == Path(d) / "run1"
            t = P.tabla([r])
            assert "PASS (reintento)" in t and f"evidencia 1er intento: {Path(d) / 'run1'}" in t
            assert '"ok2": true' in P.REINTENTOS.read_text(encoding="utf-8")
            llamadas.clear()
            P._correr = falso(["no se abrio el menu de capsulas del jugador 1"])   # logica: nunca se repite
            r = P.correr("c", "tag_swap")
            assert not r["ok"] and len(llamadas) == 1 and f"evidencia: {Path(d) / 'run1'}" in P.tabla([r])
            llamadas.clear()
            os.environ["DBZ3_PRUEBAS_REINTENTO"] = "0"
            P._correr = falso(["no llego al menu principal en 120.0 s (pantalla 0)"])
            r = P.correr("c", "menu")
            assert not r["ok"] and len(llamadas) == 1 and r["clase"] == "herramienta"
    finally:
        os.environ.pop("DBZ3_PRUEBAS_REINTENTO", None)
        P.BASE, P.REINTENTOS, P._correr = old

# INFRA-02: con el PC cargado (juego a 30 fps de 60) dormir() alarga la espera, como mucho x2.
class _JLento:
    def factor(self):
        return 0.5


esperas, old_sleep, P._TL.j = [], P.time.sleep, _JLento()
try:
    P.time.sleep = esperas.append
    P.dormir(3)
finally:
    P.time.sleep, P._TL.j = old_sleep, None
assert esperas == [6.0], esperas

# INFRA-22: turbo por defecto solo en guiones que no miden imagen/memoria/tiempos; BOM aceptado; mensajes claros
assert P._turbo_guion({"pasos": ["tecla Space", "log hola 20"]})[0] == P.TURBO_GUION
assert P._turbo_guion({"pasos": ["tecla Space", "captura a"]})[0] == 1
assert P._turbo_guion({"pasos": ["dump"]})[0] == 1
assert P._turbo_guion({"pasos": ["log x"], "env": ["DBZ3_TEST_MANOS15_RAFAGA=24"]})[0] == 1
assert P._turbo_guion({"pasos": ["log x"], "turbo": False})[0] == 1
assert P._turbo_guion({"pasos": ["captura a"], "turbo": 3})[0] == 3
with tempfile.TemporaryDirectory() as d:
    g = Path(d) / "bom.toml"
    g.write_bytes(b"\xef\xbb\xbfpasos = [\"esperar 100\"]\n")
    assert P.cargar_escenario(str(g), {})[0] == "bom.toml"
    for malo, texto in ((["esperar 100", "fin"], "sobra un 'fin'"), (["esperar 0.3"], "MILISEGUNDOS")):
        try:
            P._dsl_bloques(malo, "x.toml")
            raise AssertionError(malo)
        except SystemExit as e:
            assert texto in str(e), e
    g.write_text("pasos = [\"captura a\"]\n", encoding="utf-8")
    assert P._rapido(str(g), {}) == {}
    g.write_text("pasos = [\"log a\"]\n", encoding="utf-8")
    assert P._rapido(str(g), {}) == {"DBZ3_TEST_TURBO": str(P.TURBO_GUION)}
    assert P._rapido(str(g), {"turbo": "0"}) == {}
# VEL: veredicto corto, preflight, tiempos previos
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "dbz3.log").write_text("a\nperf fps=60\nfin\n", encoding="utf-8")
    (Path(d) / "x.png").write_bytes(b"x")
    lin = P.veredicto({"ok": False, "nombre": "c/e", "dur": 3, "msg": "no sale 'x'", "run": d, "clase": "juego"}, imprimir=False)
    assert lin[0].startswith("VEREDICTO FAIL c/e") and any("no sale" in x for x in lin) and len(lin) <= 15, lin
    assert not any("perf fps" in x for x in lin) and (Path(d) / "veredicto.txt").is_file()
    assert P.veredicto({"ok": True, "nombre": "c/e", "dur": 1, "msg": "", "run": d}, imprimir=False)[0].startswith("VEREDICTO PASS")
    g = Path(d) / "malo.toml"
    g.write_bytes(b'\xef\xbb\xbfpasos = ["esperar 5", \'log "zzqqxxnoexiste" 5\']\n')
    av = P.preflight_uno(g)
    assert any("BOM" in x for x in av) and any("MILISEGUNDOS" in x for x in av) and any("zzqqxxnoexiste" in x for x in av), av
P._previo("x_s", 1.5)
assert P._TL.prev["x_s"] == 1.5
print("test_pruebas: OK")

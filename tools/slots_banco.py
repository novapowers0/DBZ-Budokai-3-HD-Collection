r"""Banco de slots (SLOTS_PJ-01): cientos de personajes sobre una copia fija. Registra sus escenarios en pruebas.ESCENARIOS
sin tocar pruebas.py (las teclas y los registros salen del estudio ESTUDIO_SLOTS/carga.py).

  python tools/slots_banco.py correr slots_paginas|slots_trajes|slots_guardado|slots_recursos|slots_edit [--n 120] [--region us|eu] [--forzar]
  python tools/slots_banco.py todo [--n 120] [--region us|eu]        los 5 escenarios + la ida y vuelta de data.bin
  python tools/slots_banco.py ida [--n 120] [--region us]            guardar con todos / sin mods / volver con todos y comparar data.bin
  python tools/slots_banco.py sin_juego [--n 120 300 500]            (lo mismo que tools/test_slots.py: sin emulador)

US = copia `sol`; EU = copia fija `eu_test2` (default.xex EU; sol no lleva xex EU). Antes: `gen_carga.py gen N`.
Cada pasada pone antes la instantanea freezer_cap2 (partida conocida) y genera/construye los N mods si hacen falta.
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(R / "tools"))
import gen_carga as G  # noqa: E402
import pruebas as P  # noqa: E402

SAVE_AT, SAVE_LEN = 0x824BA110 + 0x150, 0x2AFE9 + 2048 - 0x150
DATA = "user_data/dbz3/B13EBABEBABEBABE/4E4D0856/00000001/DBZ3/data.bin"
OUT = Path(r"E:\DBZ3HD_trabajo\slots\ida")
COPIAS = {"us": "sol", "eu": "eu_test2"}
RE_PAG = r"pagina logica \d+ -> (\d+) de (\d+)"


def partida(j):
    off = -0x10 if j.dest.name in os.environ.get("DBZ3_TEST_EU_COPIAS", "").split(",") or os.environ.get("DBZ3_TEST_PANTALLA_EU") == "1" else 0
    b = P.leer(j.h, P.GBASE + SAVE_AT + off, SAVE_LEN)
    if not b or len(b) != SAVE_LEN:
        raise P.Fallo("no pude leer la partida")
    return b


def iguales(j, antes, que):
    b = partida(j)
    dif = [i for i in range(SAVE_LEN) if antes[i] != b[i]]
    j.info("%s: partida en memoria %s (%d bytes distintos: %s)" % (que, "IGUAL" if not dif else "CAMBIA", len(dif), ",".join("+0x%X" % i for i in dif[:12])))
    return len(dif)


def al_duelo(j):
    time.sleep(2.5)
    j.tecla("S", 1200); j.tecla("Space", 0)
    j.esperar_pantalla([14], 10); time.sleep(2.5)
    j.tecla("Space", 8000)                                  # 1P vs COM: la rueda


def paginas_vistas(j):
    return [tuple(map(int, m)) for m in re.findall(RE_PAG, j.texto_log())]


def _paginas(j, par):
    """Rueda paginada: R1 recorre todas las paginas (ida y vuelta), elige de pagina 3, combate, menu y partida igual."""
    antes_ = None
    time.sleep(2.5)
    antes_ = partida(j)
    j.tecla("S", 1200); j.tecla("Space", 0)
    j.esperar_pantalla([14], 10); time.sleep(2.5)
    j.tecla("Space", 8000)
    j.captura("a_rueda")
    j.log(r"pagina logica", 1)
    for _ in range(6):
        j.tecla("3", 2000)
    pv = paginas_vistas(j)
    j.info("paginas vistas: %d cambios, ultima %s" % (len(pv), pv[-1] if pv else None))
    if not pv:
        raise P.Fallo("R1 no cambia de pagina (sin 'pagina logica N -> M de T' en el log)")
    if len(set(p[0] for p in pv)) < 2:
        raise P.Fallo("las paginas no avanzan: %s" % pv)
    for _ in range(17): j.tecla("S", 400)
    j.captura("c_casilla_paginada")
    j.tecla("Space", 4000)
    j.captura("d_menu_capsulas")
    j.tecla("Space", 5000)
    j.tecla("3", 2500)                                      # el rival, de otra pagina
    for _ in range(3): j.tecla("S", 400)
    j.captura("f_rival")
    j.tecla("Space", 4000)
    j.tecla("Space", 5000)
    for _ in range(5):
        if "combate: lado 1" in j.texto_log(): break
        j.tecla("Space", 3000)
    j.log(r"combate: lado 1 = proxy", 60)
    P._esperar_pelea(j)
    time.sleep(3)
    j.captura("h_combate")
    j.log(r"tag: combate acabado", 60)
    j.log(r"estado del combate 7", 40)
    j.tecla("S", 800); j.tecla("S", 800); j.tecla("Space", 2000)
    j.tecla("W", 800); j.tecla("Space", 3000)
    j.esperar_pantalla([5], 60)
    j.captura("i_menu")
    if iguales(j, antes_, "tras el combate"):
        raise P.Fallo("la partida en memoria cambio tras un combate con personajes de pagina")


def _trajes(j, par):
    al_duelo(j)
    for k in range(20):
        j.tecla("D", 700)
    j.captura("a_traje20")
    j.tecla("Space", 4000); j.tecla("Space", 5000)
    j.captura("b_elegido")
    for _ in range(12):
        j.tecla("3", 2500)
        if (paginas_vistas(j) or [(0, 0)])[-1][0] == 3:
            break
    for _ in range(25): j.tecla("S", 350)
    j.captura("c_rival")
    j.tecla("D", 900); j.tecla("D", 900)
    j.captura("d_rival_traje")
    j.tecla("Space", 4000); j.tecla("Space", 5000)
    for _ in range(5):
        if "tag: empieza un combate" in j.texto_log() or "combate: lado 1" in j.texto_log(): break
        j.tecla("Space", 3000)
    P._esperar_pelea(j)
    time.sleep(2)
    j.captura("e_combate")
    j.log(r"tag: combate acabado", 60)


def _recursos(j, par):
    """150 cambios de pagina seguidos + combate: sin 'sin memoria' ni errores, y los cambios cuadran."""
    al_duelo(j)
    t0 = time.time()
    for _ in range(150):
        j.tecla("3", 450)
    ch = paginas_vistas(j)
    j.info("cambios de pagina vistos: %d en %.0f s" % (len(ch), time.time() - t0))
    if len(ch) < 100:
        raise P.Fallo("solo %d de 150 cambios de pagina llegaron al log" % len(ch))
    j.captura("a")
    for _ in range(16): j.tecla("S", 350)
    j.tecla("Space", 4000); j.tecla("Space", 5000)
    j.tecla("Space", 4000); j.tecla("Space", 5000)
    for _ in range(5):
        if "combate: lado 1" in j.texto_log(): break
        j.tecla("Space", 3000)
    P._esperar_pelea(j)
    j.captura("b")
    j.log(r"tag: combate acabado", 60)
    if "sin memoria" in j.texto_log():
        raise P.Fallo("el juego dijo 'sin memoria'")
    j.info("arranque: menu en %.1f s" % (getattr(j, "t_menu", 0) or 0))


def _edit(j, par):
    antes = (time.sleep(2.5), partida(j))[1]
    j.tecla("S", 1200); j.tecla("Space", 0)
    j.esperar_pantalla([14], 10); time.sleep(2.5)
    j.tecla("Space", 8000)
    for _ in range(2): j.tecla("3", 2500)
    for _ in range(16): j.tecla("S", 400)
    j.captura("a_casilla")
    j.tecla("Space", 5000)
    j.tecla("S", 1500); j.tecla("S", 1500)
    j.captura("b_opcion_edit")
    j.tecla("Space", 5000)
    j.captura("c_bandeja")
    j.tecla("Space", 4000)
    j.tecla("S", 1500)
    j.captura("d_lista")
    j.tecla("Space", 3000)
    j.captura("e_equipa")
    for _ in range(3): j.tecla("P", 2500)
    j.captura("f_salir")
    n = iguales(j, antes, "en Edit Skills con personaje de pagina")
    for _ in range(4): j.tecla("P", 2000)
    j.captura("g_fuera")
    if n:
        raise P.Fallo("Edit Skills con personaje de pagina cambio la partida en memoria (%d bytes)" % n)


def _unica_pagina(j, par):
    """SLOTS_PJ-07: con dbz3_capsula_unica_paginas, el personaje de pagina con 2+ transformaciones
    (id logico `objetivo`, por defecto 10107 = GOKU B1) recibe capsula unica 1984+plaza. Recorre todas
    las paginas (R1) y comprueba la linea del log; la partida en memoria no cambia."""
    objetivo = str(par.get("objetivo", "10107"))
    antes = (time.sleep(2.5), partida(j))[1]
    al_duelo(j)
    for _ in range(14):
        j.tecla("3", 1800)
    pv = paginas_vistas(j)
    j.info("paginas visitadas: %d" % len(pv))
    j.captura("a_paginas")
    txt = j.texto_log()
    patron = (r"capsula unica del id logico " + re.escape(objetivo) + r" \(plaza (\d+)\) -> (\d+)"
              if objetivo != "auto" else r"capsula unica del id logico (\d+) \(plaza (\d+)\) -> (\d+)")
    lineas = re.findall(patron, txt)
    if not lineas:
        if "capsula unica activa" not in txt:
            raise P.Fallo("el mod _forma_unica no esta activo: sin capsula unica que probar")
        raise P.Fallo("ninguna linea de capsula unica para el id logico %s (id en roster.toml?)" % objetivo)
    if objetivo == "auto":
        j.info("capsula unica de pagina: id logico %s, plaza %s -> %d" % lineas[-1])
        uid = int(lineas[-1][2])
    else:
        uid = 1984 + int(lineas[-1][0])
        j.info("capsula unica del id logico %s: plaza %s -> %d" % (objetivo, lineas[-1][0], uid))
    # Edit Skills con el personaje objetivo: equipar/ver la capsula unica no puede tocar la partida
    for _ in range(16): j.tecla("S", 400)
    j.tecla("Space", 5000); j.tecla("S", 1500); j.tecla("S", 1500); j.tecla("Space", 5000)
    j.captura("b_bandeja")
    j.tecla("P", 2500); j.tecla("P", 2500)
    n = iguales(j, antes, "tras ver la capsula unica de un personaje de pagina")
    for _ in range(4): j.tecla("P", 2000)
    if n:
        raise P.Fallo("la partida en memoria cambio (%d bytes)" % n)


def _ws(j):
    """Memoria residente del proceso del juego (bytes). GetProcessMemoryInfo basta con el handle de lectura."""
    import ctypes  # noqa: PLC0415
    from ctypes import wintypes  # noqa: PLC0415

    class PMC(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                    ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                    ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]
    pmc = PMC()
    pmc.cb = ctypes.sizeof(PMC)
    ctypes.windll.psapi.GetProcessMemoryInfo(wintypes.HANDLE(j.h), ctypes.byref(pmc), pmc.cb)
    return int(pmc.WorkingSetSize)


def _fugas(j, par):
    """SLOTS_PJ-12a: 1000 cambios de pagina (RB/LB) con crecimiento del proceso < 100 KB, medido en
    regimen (primero 300 cambios para que el juego cargue todas sus cache; luego la cuenta)."""
    al_duelo(j)
    time.sleep(2)
    for k in range(300):          # calienta: todas las paginas y caches del juego
        j.tecla("3" if k % 40 < 20 else "1", 300)
    time.sleep(3)
    m0 = _ws(j)
    n0 = len(paginas_vistas(j))
    for k in range(1000):
        j.tecla("3" if k % 40 < 20 else "1", 300)     # RB / LB: ida y vuelta por las paginas
    time.sleep(3)
    m1 = _ws(j)
    n1 = len(paginas_vistas(j))
    crece = (m1 - m0) / 1024.0
    j.info("memoria del proceso: %.1f MB -> %.1f MB (crecimiento %.1f KB; %d cambios de pagina en el tramo medido)"
           % (m0 / 1048576.0, m1 / 1048576.0, crece, n1 - n0))
    j.captura("a_fin")
    if n1 - n0 < 100:
        raise P.Fallo("solo %d cambios de pagina en el tramo medido (¿se perdieron las teclas?)" % (n1 - n0))
    if crece > 100:
        raise P.Fallo("crecimiento %.1f KB por 1000 cambios de pagina (tope 100 KB)" % crece)


def _edit40(j, par):
    """SLOTS_PJ-08: bandeja de Edit Skills con las capsulas nuevas de la ventana visible, 7 del final
    equipadas, y la lista Custom (mods/capsulas_custom.txt) conservada tras 10 cambios (lo valida el arnes
    leyendo el fichero; las lineas «capsula N sin nombre» no deben salir para las visibles)."""
    antes = (time.sleep(2.5), partida(j))[1]
    custom = j.dest / "mods" / "capsulas_custom.txt"
    c0 = custom.read_text(encoding="utf-8", errors="replace") if custom.exists() else ""
    j.tecla("S", 1200); j.tecla("Space", 0)
    j.esperar_pantalla([14], 10); time.sleep(2.5)
    j.tecla("Space", 8000)
    for _ in range(2): j.tecla("3", 2500)                 # pagina 3: personaje de pagina
    for _ in range(16): j.tecla("S", 400)
    j.tecla("Space", 5000)
    j.tecla("S", 1500); j.tecla("S", 1500)
    j.tecla("Space", 5000)
    j.captura("a_bandeja")
    for _ in range(10):                                   # recorre la bandeja: 10 cambios
        for _ in range(5):
            j.tecla("S", 300)
        j.captura("b_bandeja")
    for _ in range(45): j.tecla("S", 250)                 # hasta el final (40+ capsulas)
    j.captura("c_final")
    for _ in range(7):                                    # equipa 7 del final
        j.tecla("Space", 1200)
        j.tecla("S", 300)
    j.captura("d_equipadas")
    j.tecla("P", 2500); j.tecla("P", 2500)
    n = iguales(j, antes, "en Edit Skills (40 capsulas, 7 del final)")
    c1 = custom.read_text(encoding="utf-8", errors="replace") if custom.exists() else ""
    if c1 == c0:
        j.info("capsulas_custom.txt no cambio (la lista Custom solo se escribe al salir del editor con cambios)")
    else:
        j.info("capsulas_custom.txt actualizado (%d lineas)" % len([x for x in c1.splitlines() if x and x[0] != "#"]))
    for _ in range(4): j.tecla("P", 2000)
    j.captura("e_fuera")
    if n:
        raise P.Fallo("Edit Skills con 40 capsulas cambio la partida en memoria (%d bytes)" % n)
    nom = re.findall(r"capsula (\d+) sin nombre en el banco", j.texto_log())
    if nom:
        j.info("avisos de capsula sin nombre: %s" % ", ".join(sorted(set(nom))[:10]))
        raise P.Fallo("%d capsulas visibles sin nombre en el banco" % len(set(nom)))


def _guardar_data(j, filas=5):   # ida.py del estudio: 5 pulsaciones desde el menu principal
    f = j.dest / DATA
    h0 = hashlib.sha1(f.read_bytes()).hexdigest()
    for _ in range(filas): j.tecla("S", 700)
    j.tecla("Space", 4000); j.tecla("S", 900); j.tecla("Space", 2500)
    j.tecla("Space", 3000)
    for k in range(4):
        j.tecla("W", 600); j.tecla("Space", 3000)
        if hashlib.sha1(f.read_bytes()).hexdigest() != h0: break
    time.sleep(4)
    if hashlib.sha1(f.read_bytes()).hexdigest() == h0:
        raise P.Fallo("la partida no se escribio")
    j.captura("guardado")


def _guardado(j, par):
    """Con la etiqueta (--par etiqueta=A_todos|B_sin|C_vuelta): ficha de arranque y, con guardar=1, guarda data.bin."""
    et = par.get("etiqueta", "A")
    OUT.mkdir(parents=True, exist_ok=True)
    time.sleep(2.5)
    pre = partida(j)
    (OUT / f"{et}_memoria.bin").write_bytes(pre)
    j.captura("a_menu")
    if par.get("guardar") == "1":
        _guardar_data(j)
        (OUT / f"{et}_memoria_guardado.bin").write_bytes(partida(j))
    import shutil
    shutil.copy2(j.dest / DATA, OUT / f"{et}_data.bin")
    j.info("%s: data.bin %d bytes sha1 %s" % (et, (j.dest / DATA).stat().st_size, hashlib.sha1((j.dest / DATA).read_bytes()).hexdigest()[:12]))


def _prep(j, par, env):
    if par.get("region") == "eu":
        P._prep_iw_minijuego(j, par, env)


def _cfg(par):
    return {"DBZ3_TEST_RINGOUT": "1", "DBZ3_TAG_SLOW": "3"}


ESC = {"slots_paginas": ("slots: rueda con paginas (R1), combate con personaje de pagina, partida intacta", _paginas),
       "slots_trajes": ("slots: traje extra de un personaje de pagina", _trajes),
       "slots_guardado": ("slots: ficha / guardado de data.bin con N personajes (--par etiqueta= guardar=1)", _guardado),
       "slots_recursos": ("slots: 150 cambios de pagina + combate sin 'sin memoria'", _recursos),
       "slots_edit": ("slots: Edit Skills con personaje de pagina", _edit),
       "slots_fugas": ("slots: 1000 cambios de pagina sin fuga (< 100 KB)", _fugas),
       "slots_edit40": ("slots: Edit Skills con 40 capsulas nuevas y 7 del final", _edit40),
       "slots_unica_pagina": ("slots: capsula unica de un personaje de pagina (--par objetivo=ID)", _unica_pagina)}
for k, (d, f) in ESC.items():
    P.ESCENARIOS[k] = (d, _cfg, f, _prep)


def preparar(copia, n, region):
    """N mods carga_* presentes y activos, y roster montado."""
    mods = G._mods(copia)
    for d in mods.glob("carga_*"):
        (d / ".disabled").unlink(missing_ok=True)
    if len(list(mods.glob("carga_*"))) != n:
        G.gen(copia, n)
    if region == "eu":
        for d in ("eu", "us"):
            if not (G.BASE / copia / d).exists():
                raise SystemExit(f"{copia}/{d} (enlace a la raiz EU) falta")


def pasada(esc, n, region, forzar=False, par=None):
    copia = COPIAS[region]
    preparar(copia, n, region)
    p = dict(par or {}, region=region)
    r = P.correr(copia, esc, [], "freezer_cap2", p, forzar)
    malos = G.revisar(copia, n)                   # el roster ya esta montado: iconos con casilla propia y no vacios
    if malos and r["ok"]:
        r["ok"], r["msg"] = False, "roster: " + "; ".join(malos[:3])
    return r


def ida(n, region):
    copia = COPIAS[region]
    mods = G._mods(copia)
    preparar(copia, n, region)
    res = []
    todos = sorted(mods.glob("carga_*"))
    res.append(pasada("slots_guardado", n, region, True, {"etiqueta": f"A_todos_{region}{n}", "guardar": "1"}))
    for d in todos: (d / ".disabled").write_text("")
    G.construir(copia)
    res.append(P.correr(copia, "slots_guardado", [], "freezer_cap2", {"etiqueta": f"B_sin_{region}{n}", "guardar": "1", "region": region}, True))
    for d in todos: (d / ".disabled").unlink()
    G.construir(copia)
    res.append(P.correr(copia, "slots_guardado", [], None, {"etiqueta": f"C_vuelta_{region}{n}", "region": region}, True))
    res.append(P.correr(copia, "slots_guardado", [], None, {"etiqueta": f"D_guarda_{region}{n}", "guardar": "1", "region": region}, True))
    a, b, c, dd = (OUT / f"{x}_{region}{n}_data.bin" for x in ("A_todos", "B_sin", "C_vuelta", "D_guarda"))
    mem = lambda e, g="": (OUT / f"{e}_{region}{n}_memoria{g}.bin").read_bytes()
    for x, y, g1, g2 in (("A_todos", "C_vuelta", "", ""), ("B_sin", "C_vuelta", "_guardado", ""), ("A_todos", "D_guarda", "_guardado", "_guardado")):
        m1, m2 = mem(x, g1), mem(y, g2)
        print(f"partida en memoria {x}{g1} vs {y}{g2}: {sum(p != q for p, q in zip(m1, m2))} bytes distintos", flush=True)
    for x, y in ((a, b), (a, c), (b, c), (a, dd), (b, dd)):
        da, db = x.read_bytes(), y.read_bytes()
        dif = [i for i in range(min(len(da), len(db))) if da[i] != db[i]]
        print(f"data.bin {x.name[0]} vs {y.name[0]}: {len(dif)} bytes distintos (largos {len(da)}/{len(db)})"
              f"{'; rango 0x%X..0x%X' % (dif[0], dif[-1]) if dif else ''}", flush=True)
    return res


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("modo", choices=["correr", "todo", "ida", "sin_juego"])
    ap.add_argument("esc", nargs="?")
    ap.add_argument("--n", type=int, nargs="+", default=[120])
    ap.add_argument("--region", default="us", choices=["us", "eu"])
    ap.add_argument("--forzar", action="store_true")
    ap.add_argument("--objetivo", help="slots_unica_pagina: id logico (auto = cualquiera)")
    a = ap.parse_args()
    if a.modo == "sin_juego":
        import subprocess
        sys.exit(subprocess.call([sys.executable, str(R / "tools" / "test_slots.py")] + [str(x) for x in a.n]))
    n = a.n[0]
    if a.modo == "correr":
        rs = [pasada(a.esc, n, a.region, a.forzar, {"objetivo": a.objetivo} if a.objetivo else None)]
    elif a.modo == "ida":
        rs = ida(n, a.region)
    else:
        rs = [pasada(e, n, a.region, a.forzar) for e in ESC if e != "slots_guardado"]
    print("RESUMEN:", "; ".join("%s %s" % (r["nombre"], "PASS" if r["ok"] else "FAIL " + r["msg"][:80]) for r in rs))
    sys.exit(0 if all(r["ok"] for r in rs) else 1)

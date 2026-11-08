"""tPEND 1.5.6.1: Dragon Rush con la entrada del usuario (R1 + B) y rafaga de fotos.

Entrada que pide el usuario: modo hiper -> R1 + circulo (B).
Teclas (src/launcher/settings.cpp): hiper = P+K+G+E -> L+P+Space+Quote;
R1 = "3"; B = "Quote". Solo R1 no lanza el Rush (se repite R1+B en la ventana).

Uso (US):
  python tools/tpend_pruebas.py correr dsb68 tpendrush --par ids=44,7,0 --par seg=26 --forzar

Deja: shots/ (rafaga), tpend_codigos.csv (codigo de accion por muestra), pantallas.txt.
PASS solo acredita la recogida; el Rush se contrasta con el codigo 0x400.. y las fotos.
"""
import csv
import json
from pathlib import Path
import shutil
import struct
import sys
import threading
import time

import pruebas as P
import solcam_pruebas as S

P.OVERRIDES.update(dbz3_combate_prueba_muneco='"quieto"',
                   dbz3_combate_definitiva_directa="false",
                   dbz3_escenarios_guardas="true")


def _mgr(j):
    b = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
    if not b:
        return None
    mgr = int.from_bytes(b, "big")
    if not mgr:
        return None
    cb = P.leer(j.h, P.GBASE + mgr, 36000)
    if not cb or len(cb) < 36000:
        return None
    return cb


def _cod0(cb):
    return int.from_bytes(cb[2096 + 2928:2096 + 2932], "big")


def ejecutar(j, par):
    j.log(r"tag: empieza un combate", 90)
    activos = [d.name for d in (j.dest / "mods").iterdir()
               if d.is_dir() and not (d / ".disabled").exists()]
    contexto = dict(exe_sha256=S.hashlib.sha256((j.dest / "dbz3.exe").read_bytes()).hexdigest(),
                    mods_activos=sorted(activos), parametros=par,
                    muestreo="codigo de accion cada >=8 ms + rafaga de fotos")
    (j.run / "contexto.json").write_text(json.dumps(contexto, indent=2), encoding="utf-8")
    shutil.copy2(j.dest / "dbz3_user.toml", j.run / "configuracion.toml")
    (j.run / "shots").mkdir(exist_ok=True)
    for p in sorted(j.dest.glob("dbz3_shot_*.bmp")):
        p.unlink(missing_ok=True)

    filas = []
    parar = threading.Event()
    t0 = time.monotonic()

    def medir():
        while not parar.wait(0.008):
            cb = _mgr(j)
            if not cb:
                continue
            u = lambda o: struct.unpack_from(">I", cb, o)[0]
            f = lambda o: struct.unpack_from(">f", cb, o)[0]
            h = lambda o: struct.unpack_from(">H", cb, o)[0]
            x, x1 = 2096, 2096 + 8240
            p0 = (f(x + 64), f(x + 72)); p1 = (f(x1 + 64), f(x1 + 72))
            filas.append(dict(t=round(time.monotonic() - t0, 3),
                              estado=u(35128), cod0=u(x + 2928), act0=h(x + 3244),
                              hiper0=cb[x + 8208], ki0=f(x + 6100),
                              vida0=f(x + 6080), vida1=f(x1 + 6080),
                              p0x=round(p0[0], 2), p0z=round(p0[1], 2),
                              p1x=round(p1[0], 2), p1z=round(p1[1], 2),
                              dist=round(((p0[0]-p1[0])**2 + (p0[1]-p1[1])**2) ** 0.5, 2)))

    def traza(nombre):
        if par.get("trace") in ("1", "rush", "pelea"):
            try:
                (j.dest / "dbz3_frame_trace.done").unlink(missing_ok=True)
            except OSError:
                pass
            (j.dest / "dbz3_frame_trace.req").write_text("1", encoding="ascii")
            (j.run / f"trace_{nombre}.txt").write_text("pedido", encoding="utf-8")

    hilo = threading.Thread(target=medir)
    hilo.start()
    lanzada = False
    try:
        S.foto(j, "a_intro")
        if par.get("intro") == "1":
            # Presentacion/versus: dispara la rafaga en cuanto el estado pasa a 3.
            t1 = time.monotonic()
            while time.monotonic() - t1 < 60:
                cb = _mgr(j)
                if cb and struct.unpack_from(">I", cb, 35128)[0] == 3:
                    (j.dest / "dbz3_shot.req").write_text("240 1", encoding="ascii")
                    break
                time.sleep(0.05)
        P._esperar_pelea(j, 90)
        S.foto(j, "b_pelea")
        P.dormir(1.0)
        if par.get("trace") == "1":
            traza("pelea")
        # Modo hiper: P+K+G+E (L+punch + P+kick + Space+guard + Quote+energia)
        j.tecla("L+P+Space+Quote", 2500)
        P.dormir(0.5)
        cb = _mgr(j)
        (j.run / "estado_hiper.json").write_text(json.dumps(
            dict(hiper=int(cb[2096 + 8208]) if cb else None), indent=2), encoding="utf-8")
        if par.get("nodash") != "1":
            j.tecla(par.get("dashkey", "D") + " " + par.get("acercar", "900"), 1100)
        entrada = par.get("entrada", "4b")
        # Medido por solCAM2/dsSB68: en hiper, P+K (L+P) y luego P+K+G+E arranca
        # la familia 0x400. El usuario pide R1+B ("3+Quote"); se prueba lo mismo
        # con esa pareja en lugar de los cuatro botones.
        if entrada in ("4b", "r1b"):
            j.tecla("L+P", 450)
            disparo = "L+P+Space+Quote" if entrada == "4b" else "3+Quote"
            for intento in range(5):
                j.tecla(disparo + " 220", 260)
                t1 = time.monotonic()
                while time.monotonic() - t1 < 1.2:
                    cb = _mgr(j)
                    if cb and 0x400 <= _cod0(cb) < 0x480:
                        (j.dest / "dbz3_shot.req").write_text("180 4", encoding="ascii")
                        lanzada = True
                        traza("rush")
                        break
                    time.sleep(0.05)
                if lanzada:
                    break
        elif entrada == "r1b_direct":
            for intento in range(5):
                j.tecla("3+Quote 220", 260)
                t1 = time.monotonic()
                while time.monotonic() - t1 < 1.0:
                    cb = _mgr(j)
                    if cb and 0x400 <= _cod0(cb) < 0x480:
                        (j.dest / "dbz3_shot.req").write_text("180 4", encoding="ascii")
                        lanzada = True
                        traza("rush")
                        break
                    time.sleep(0.05)
                if lanzada:
                    break
        else:
            P.dormir(4)
        t1 = time.monotonic()
        while time.monotonic() - t1 < 20:
            if len(list(j.dest.glob("dbz3_shot_*.bmp"))) >= 180:
                break
            time.sleep(0.1)
        S.foto(j, "c_medio")
        P.dormir(float(par.get("seg", "26")) / 2)
        S.foto(j, "d_tecnica")
        P.dormir(float(par.get("seg", "26")) / 2)
        S.foto(j, "e_despues")
        if par.get("recuperacion") == "1":
            j.tecla("A 900", 1500)
            S.foto(j, "f_seguimiento")
    finally:
        for p in sorted(j.dest.glob("dbz3_shot_*.bmp")):
            shutil.copy2(p, j.run / "shots" / p.name)
        parar.set()
        hilo.join()
        campos = ["t", "estado", "cod0", "act0", "hiper0", "ki0", "vida0", "vida1",
                  "p0x", "p0z", "p1x", "p1z", "dist"]
        with (j.run / "tpend_codigos.csv").open("w", newline="", encoding="utf-8") as fp:
            w = csv.DictWriter(fp, fieldnames=campos)
            w.writeheader()
            w.writerows(filas)
        codigos = sorted({r["cod0"] for r in filas})
        resumen = dict(fotos=len(list((j.run / "shots").glob("*.bmp"))),
                       muestras=len(filas), entrada=par.get("entrada", "4b"), rush=lanzada,
                       codigos_cinematicos=[hex(c) for c in codigos if 0x400 <= c < 0x500])
        (j.run / "resumen.json").write_text(json.dumps(resumen, indent=2), encoding="utf-8")
        j.info(str(resumen))
    if not list((j.run / "shots").glob("*.bmp")):
        raise P.Fallo("no se guardo ninguna foto de la rafaga; la prueba no es concluyente")


def entorno(p):
    ids = p.get("ids", "44,7,0")
    if any(int(v) >= 10000 for v in ids.split(",")[:2]):
        raise ValueError("DBZ3_TEST_BATTLE no resuelve aqui los IDs logicos >=10000")
    if p.get("guardas") in ("true", "false"):
        P.OVERRIDES["dbz3_escenarios_guardas"] = p["guardas"]
    env = {"DBZ3_TEST_BATTLE": ids, "DBZ3_COMBATE_KI": "7000", "DBZ3_COMBATE_SONDA": "1"}
    if p.get("nomuneco") != "1":
        env["DBZ3_COMBATE_MUNECO"] = "quieto"
    if p.get("camtraza") == "1":
        env["DBZ3_TEST_CAM_TRAZA"] = "1"
    if p.get("ult") == "1":
        env["DBZ3_TEST_ULT"] = "1"
        P.OVERRIDES["dbz3_combate_definitiva_directa"] = "false"
    return env


P.ESCENARIOS["tpendrush"] = ("tPEND: Rush con R1+B (entrada del usuario) y rafaga", entorno, ejecutar)
if __name__ == "__main__":
    sys.exit(P.main())

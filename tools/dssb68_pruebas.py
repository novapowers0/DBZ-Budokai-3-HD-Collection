"""dsSB68 1.5.6.1: Rush real de un port de Shin Budokai, con rafaga de fotos.

Uso (US):
  python tools/dssb68_pruebas.py correr COPIA sb68rush --par ids=44,7,0 --par seg=32 --forzar

Reutiliza el arnes de solcam_pruebas.py (entrada Rush acreditada por solCAM2:
hiper -> D 900 -> L+P -> cuatro botones) y lanza dbz3_shot.req "240 1" justo
despues de los cuatro botones: 240 fotos, una por cuadro, a media escala.
Las fotos se copian a la carpeta de la corrida (shots/).
"""
import json
from pathlib import Path
import shutil
import sys
import time

import pruebas as P
import solcam_pruebas as S


def ejecutar(j, par):
    j.log(r"tag: empieza un combate", 90)
    activos = [d.name for d in (j.dest / "mods").iterdir()
               if d.is_dir() and not (d / ".disabled").exists()]
    contexto = dict(exe_sha256=S.hashlib.sha256((j.dest / "dbz3.exe").read_bytes()).hexdigest(),
                    mods_activos=sorted(activos), parametros=par,
                    muestreo="rafaga dbz3_shot_240x1 tras los cuatro botones")
    (j.run / "contexto.json").write_text(json.dumps(contexto, indent=2), encoding="utf-8")
    shutil.copy2(j.dest / "dbz3_user.toml", j.run / "configuracion.toml")
    (j.run / "shots").mkdir(exist_ok=True)
    viejas = sorted(j.dest.glob("dbz3_shot_*.bmp"))
    for p in viejas:
        p.unlink(missing_ok=True)
    try:
        S.foto(j, "a_intro")
        P._esperar_pelea(j, 90)
        S.foto(j, "b_pelea")
        j.tecla("L+P+Space+Quote", 2500)
        j.tecla("D " + par.get("acercar", "900"), 1100)
        j.tecla("L+P", 450)
        # La ventana de los cuatro botones varia con la IA: se repite hasta 4 veces
        # (1 s entre intentos) y se para en cuanto empieza el intercambio (0x400).
        t0 = time.monotonic()
        lanzada = False
        for intento in range(4):
            j.tecla("L+P+Space+Quote", 200)
            t1 = time.monotonic()
            while time.monotonic() - t1 < 1.1:
                b = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
                if b:
                    mgr = int.from_bytes(b, "big")
                    cb = P.leer(j.h, P.GBASE + mgr, 36000) if mgr else None
                    if cb and len(cb) >= 36000:
                        cod0 = int.from_bytes(cb[2096 + 2928:2096 + 2932], "big")
                        if 0x400 <= cod0 < 0x480:
                            (j.dest / "dbz3_shot.req").write_text("240 6", encoding="ascii")
                            lanzada = True
                            break
                time.sleep(0.05)
            if lanzada:
                break
        if not lanzada:
            t0 = time.monotonic()
            while time.monotonic() - t0 < 20:
                b = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
                if b:
                    mgr = int.from_bytes(b, "big")
                    cb = P.leer(j.h, P.GBASE + mgr, 36000) if mgr else None
                    if cb and len(cb) >= 36000:
                        cod0 = int.from_bytes(cb[2096 + 2928:2096 + 2932], "big")
                        if 0x400 <= cod0 < 0x480:
                            (j.dest / "dbz3_shot.req").write_text("240 6", encoding="ascii")
                            lanzada = True
                            break
                time.sleep(0.05)
        if not lanzada:
            (j.dest / "dbz3_shot.req").write_text("240 6", encoding="ascii")
        t0 = time.monotonic()
        while time.monotonic() - t0 < 30:
            if len(list(j.dest.glob("dbz3_shot_*.bmp"))) >= 240:
                break
            time.sleep(0.1)
        S.foto(j, "c_medio")
        P.dormir(float(par.get("seg", "32")) / 2)
        S.foto(j, "d_tecnica")
        P.dormir(float(par.get("seg", "32")) / 2)
        S.foto(j, "e_despues")
        if par.get("recuperacion") == "1":
            j.tecla("A 900", 1500)
            S.foto(j, "f_seguimiento")
    finally:
        fotos = sorted(j.dest.glob("dbz3_shot_*.bmp"))
        for p in fotos:
            shutil.copy2(p, j.run / "shots" / p.name)
        for nombre in ("combate_sonda.csv", "combate_golpes.csv"):
            origen = j.dest / "logs" / nombre
            if origen.is_file():
                shutil.copy2(origen, j.run / nombre)
        resumen = dict(fotos=len(fotos))
        (j.run / "rafaga.json").write_text(json.dumps(resumen, indent=2), encoding="utf-8")
        j.info(str(resumen))
    if not fotos:
        raise P.Fallo("no se guardo ninguna foto de la rafaga; la prueba no es concluyente")


P.ESCENARIOS["sb68rush"] = ("dsSB68: Rush real de un port de Shin Budokai con rafaga de fotos",
                            S.entorno, ejecutar)
if __name__ == "__main__":
    sys.exit(P.main())

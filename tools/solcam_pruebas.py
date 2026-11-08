"""solCAM 1.5.6.1: medida de cámara/animación en las copias de pruebas.py.

Uso (US): python tools/solcam_pruebas.py correr COPIA solcam
     --par modo=ult|ult_manual|rush|observar
     --par ids=0,7,5 --par seg=35 --forzar
Registra RAM y capturas a 0,5; las instantáneas usan el respaldo de pruebas.py.
observar no pulsa botones: permite ejecutar la técnica a mano y conservar el CSV.
PASS solo acredita la recogida; Rush/definitiva se contrastan con captura y estado.
ids admite id1,id2,escenario,tiempo,vida1,vida2,forma1,forma2; las formas guest empiezan en 0.
"""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import threading
import time

import pruebas as P

# La copia puede conservar ajustes de otros escenarios; se fijan al prepararla.
P.OVERRIDES.update(dbz3_combate_prueba_muneco='"quieto"',
                   dbz3_combate_definitiva_directa="false",
                   dbz3_escenarios_guardas="true")


def foto(j, nombre):
    p = j.captura(nombre)
    if P.Image:
        with P.Image.open(p) as im:
            im.resize((im.width//2, im.height//2)).save(p)


def ejecutar(j, par):
    j.log(r"tag: empieza un combate", 90)
    activos = [d.name for d in (j.dest/"mods").iterdir()
               if d.is_dir() and not (d/".disabled").exists()]
    contexto = dict(exe_sha256=hashlib.sha256((j.dest/"dbz3.exe").read_bytes()).hexdigest(),
                    mods_activos=sorted(activos), parametros=par,
                    muestreo="RAM asincrona cada >=8 ms; combate_sonda.csv por cuadro del guest")
    (j.run/"contexto.json").write_text(json.dumps(contexto, indent=2), encoding="utf-8")
    shutil.copy2(j.dest/"dbz3_user.toml", j.run/"configuracion.toml")
    filas = []
    parar = threading.Event()
    t0 = time.monotonic()

    def medir():
        while not parar.wait(0.008):  # sonda temporal; no es captura sincronizada por cuadro
            b = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
            if not b:
                continue
            mgr = int.from_bytes(b, "big")
            b = P.leer(j.h, P.GBASE + mgr, 36000) if mgr else None
            if not b or len(b) < 36000:
                continue
            u = lambda o: struct.unpack_from(">I", b, o)[0]
            f = lambda o: struct.unpack_from(">f", b, o)[0]
            h = lambda o: struct.unpack_from(">H", b, o)[0]
            row = dict(t=round(time.monotonic()-t0, 3), estado=u(35128), reloj=f(35144),
                       cam_a=u(0x260), cam_b=u(0x264))
            for k in range(2):
                x = 2096 + 8240*k
                row.update({f"id{k}":u(x+7420), f"forma{k}":u(x+2980), f"act{k}":h(x+3244),
                            f"cod{k}":u(x+2928), f"cuadro{k}":f(x+3160), f"hiper{k}":b[x+8208],
                            f"vida{k}":f(x+6080), f"ki{k}":f(x+6100), f"identidad{k}":u(x+2972)})
                for c in range(3):
                    row[f"p{k}_{c}"] = f(x+64+4*c)
            for i in range(16):
                row[f"m{i}"] = f(0x60+4*i)
            cb = P.leer(j.h, P.GBASE + row["cam_b"], 64) if row["cam_b"] else None
            if cb and len(cb)==64:
                for i in range(14):
                    row[f"c{i}"] = struct.unpack_from(">f", cb, 4*i)[0]
                row["flags"] = struct.unpack_from(">I", cb, 56)[0]
                # Es la pose final de 64 B, NO el objeto de sub_820F4678:
                # +0x20 quaternion, +0x30 giro, +0x34 FOV (freecam.cpp).
                row["cam_giro"] = struct.unpack_from(">f", cb, 48)[0]
                row["cam_fov"] = struct.unpack_from(">f", cb, 52)[0]
            filas.append(row)

    hilo = threading.Thread(target=medir)
    hilo.start()
    try:
        foto(j, "a_intro")
        P._esperar_pelea(j, 90)
        foto(j, "b_pelea")
        if par.get("modo", "ult") == "ult_manual":
            # Dos pulsaciones reales: hiper y definitiva, sin relanzarla al acabar.
            j.tecla("L+P+Space+Quote", 2500)
            if "acercar" in par:
                j.tecla("D " + par["acercar"], 1100)
            j.tecla("L+P+Space+Quote", 2500)
        elif par.get("modo", "ult") == "rush":
            # solCAM2: entrada acreditada por solMODS con nativos. Hiper ANTES
            # de acercarse, expulsión P+K y cuatro botones durante su ventana.
            j.tecla("L+P+Space+Quote", 2500)
            j.tecla("D " + par.get("acercar", "900"), 1100)
            j.tecla("L+P", 450)
            j.tecla("L+P+Space+Quote", 200)
            P.dormir(3)
            foto(j, "rush_intento")
        P.dormir(float(par.get("seg", "35"))/2)
        foto(j, "c_tecnica")
        P.dormir(float(par.get("seg", "35"))/2)
        foto(j, "d_despues")
        if par.get("recuperacion") == "1":
            j.tecla("A 900", 1500)
            foto(j, "e_seguimiento")
    finally:
        parar.set()
        hilo.join()
        if filas:
            campos = list(dict.fromkeys(k for row in filas for k in row))
            with (j.run/"camara.csv").open("w", newline="", encoding="utf-8") as fp:
                w=csv.DictWriter(fp,fieldnames=campos);w.writeheader();w.writerows(filas)
            codigos = sorted(set(r["cod0"] for r in filas))
            resumen = dict(muestras=len(filas), codigos=[hex(c) for c in codigos],
                           # Los códigos por sí solos no acreditan la escena.
                           rush="pendiente de contrastar con captura/estado",
                           codigos_280_28f=[hex(c) for c in codigos if 0x280 <= c < 0x290],
                           codigos_cinematicos_4xx=[hex(c) for c in codigos if 0x400 <= c < 0x500],
                           definitiva="contrastar captura/estado; 0x490 también puede ser agarre")
            (j.run/"medida.json").write_text(json.dumps(resumen, indent=2),encoding="utf-8")
            j.info(str(resumen))
        for nombre in ("combate_sonda.csv", "combate_golpes.csv"):
            origen = j.dest/"logs"/nombre
            if origen.is_file():
                shutil.copy2(origen, j.run/nombre)
    if not filas:
        raise P.Fallo("no se recogieron muestras de cámara; la prueba no es concluyente")


def entorno(p):
    if p.get("modo", "ult") not in ("ult", "ult_manual", "rush", "observar"):
        raise ValueError("modo debe ser ult, ult_manual, rush u observar")
    ids = p.get("ids", "0,7,5")
    if any(int(v) >= 10000 for v in ids.split(",")[:2]):
        raise ValueError("DBZ3_TEST_BATTLE no resuelve aquí los IDs lógicos >=10000; "
                          "usar un ID guest o entrar por la rueda real")
    if "guardas" in p:
        if p["guardas"] not in ("true", "false"):
            raise ValueError("guardas debe ser true o false")
        P.OVERRIDES["dbz3_escenarios_guardas"] = p["guardas"]
    env = {"DBZ3_TEST_BATTLE":ids, "DBZ3_COMBATE_MUNECO":"quieto",
           "DBZ3_COMBATE_KI":"7000", "DBZ3_COMBATE_SONDA":"1"}
    if p.get("caps"):
        env["DBZ3_TEST_CAPS"] = p["caps"]
    if p.get("modo", "ult")=="ult":
        env["DBZ3_TEST_ULT"]="1"
    return env


P.ESCENARIOS["solcam"]=("solCAM: cámara y códigos de intro/Rush/definitiva",entorno,ejecutar)
if __name__=="__main__":
    if len(sys.argv)>2 and sys.argv[1]=="resumen":
        rows=list(csv.DictReader((Path(sys.argv[2])/"camara.csv").open(encoding="utf-8")))
        anterior=None
        for r in rows:
            clave=tuple(r[k] for k in ("estado","cod0","cod1","hiper0"))
            if clave==anterior: continue
            anterior=clave
            ojo=[round(float(r.get(f"c{i}") or 0),2) for i in range(3)]
            print(r["t"],"st",r["estado"],"cod",hex(int(r["cod0"])),hex(int(r["cod1"])),"hyper",r["hiper0"],"ojo",ojo)
        sys.exit(0)
    sys.exit(P.main())

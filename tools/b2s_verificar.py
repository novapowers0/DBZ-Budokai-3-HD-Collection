"""Verificacion B2S con el arnes del proyecto; copia fija smoke_e y exe identificado."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from b2s_medir import ROOT, TMP, guardar


def fijar():
    exe = TMP / "dbz3.exe"
    if exe.exists():
        raise FileExistsError(exe)
    shutil.copy2(ROOT / "out/build/win-amd64-dual/dbz3.exe", exe)
    guardar(TMP / "build.json", dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        fuentes={rel:hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()
                 for rel in ("src/pasarela.cpp", "src/pasarela_b2.cpp", "src/pasarela_nave.cpp", "src/pasarela_misiones.cpp")}))


def tablero():
    import pcsx2_cuadros as Q
    import pruebas as P
    Q.limitar_port(P)
    P.EXE = TMP / "dbz3.exe"
    os.environ["DBZ3_AGENTE"] = "solB2S"
    def run(j, _):
        j.log(r"pasarela: paso 3/\d+ \(charla", 90)
        j.log(r"dbz3 b2s: tablero AMA 50 ", 30)
        j.captura("tablero_0x1d")
        texto = j.texto_log()
        rx = r"dbz3 b2s: tablero AMA (\d+) escala ([\d.]+) ancla ([\d.-]+) ([\d.-]+) quad ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)"
        filas = {int(m[0]):list(map(float,m[1:])) for m in re.findall(rx,texto)}
        original = json.loads((TMP/"tablero/tablero_ram.json").read_text(encoding="utf-8"))
        errores = []
        for f in original["sprites"]:
            if f["anim"] not in filas:
                raise P.Fallo(f"Falta el sprite original {f['anim']}; revisar carga AMA")
            v = filas[f["anim"]]
            # El letrero pulsa; comparar su quad en escala unitaria y el ancla.
            e = max(abs(a/v[0]-b/f["escala"][0]) for a,b in zip(v[3:],f["quad_original"]))
            e = max(e,*(abs(a-b) for a,b in zip(v[1:3],f["ancla"])))
            if f["slot"] <= 26:
                e = max(e, abs(v[0]-f["escala"][0]))
            errores.append(dict(anim=f["anim"],error=e))
        guardar(TMP / "tablero/runtime.json", dict(sprites=filas,comparacion=errores))
        if max(f["error"] for f in errores) > 0.001:
            raise P.Fallo("Geometria distinta de la RAM original: " + str(errores))
        j.info("13/13 sprites contra RAM original; error <0.001; tiempos de entrada no certificados")
    P.ESCENARIOS["b2s_tablero"] = ("B2S: tablero de Satan contra RAM de B1",
        lambda _:dict(P._PS2,DBZ3_TEST_PASARELA="auto,seg=8,desde=2",DBZ3_TEST_B2S_TABLERO="1"),run)
    copia = "smoke_e"
    snap = "solB2S_tablero_"+time.strftime("%Y%m%d_%H%M%S")
    P.comprobar_libre(P.BASE/copia)
    P.guardar_instantanea(copia,snap)
    try:
        r = P.correr(copia,"b2s_tablero",["DBZ3_TEST_OCULTO=0"],None,{},forzar=True)
        guardar(TMP / "tablero/prueba.json",r)
        return 0 if r["ok"] else 1
    finally:
        P.cargar_instantanea(copia,snap)


def bateria():
    # Una sola bateria final, comando/argumentos pedidos, sin cache ni turbo.
    env = dict(os.environ, DBZ3_PRUEBAS_EXE=str(TMP/"dbz3.exe"),
               DBZ3_PRUEBAS_COPIAS="smoke_e", DBZ3_AGENTE="solB2S", PYTHONIOENCODING="utf-8")
    code = subprocess.call([sys.executable,str(ROOT/"tools/pruebas.py"),"todo","--max","1","--forzar"],env=env)
    guardar(TMP/"bateria.json",dict(codigo=code,exe_sha256=hashlib.sha256((TMP/"dbz3.exe").read_bytes()).hexdigest()))
    return code


def cierre():
    build = json.loads((TMP/"build.json").read_text(encoding="utf-8"))
    runtime = json.loads((TMP/"tablero/runtime.json").read_text(encoding="utf-8"))
    original = json.loads((TMP/"tablero/tablero_ram.json").read_text(encoding="utf-8"))
    antes = []
    for f in original["sprites"]:
        if f["slot"] > 26:
            continue
        s = 1.0 if f["slot"]-16 < original["ronda"] else .88
        quad = [-.5-36*s,-12.5-36*s,-.5+36*s,-12.5+36*s]
        antes.append(dict(anim=f["anim"], error=max(abs(a-b) for a,b in zip(quad,f["quad_original"]))))
    datos = dict(build=build,
        fuentes_actuales_coinciden={rel:hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()==sha for rel,sha in build["fuentes"].items()},
        tablero=dict(antes=antes, error_max_antes=max(f["error"] for f in antes),
            error_max_despues=max(f["error"] for f in runtime["comparacion"]),
            prueba=json.loads((TMP/"tablero/prueba.json").read_text(encoding="utf-8")),
            criterio="Antes: quads del codigo inspeccionado (0.88/1); despues: log del juego frente a RAM original. No es identidad de la pantalla completa."),
        bateria=json.loads((TMP/"bateria.json").read_text(encoding="utf-8")))
    guardar(ROOT/"studies 5/ENTREGAS/B2S_medidas.json",datos)
    print(json.dumps(dict(coincidencia=datos["fuentes_actuales_coinciden"],
        error_antes=datos["tablero"]["error_max_antes"],error_despues=datos["tablero"]["error_max_despues"],
        bateria=datos["bateria"]),indent=2))


def archivar():
    import pruebas as P
    from b2s_medir import ARCHIVO
    P.cargar_instantanea("smoke_e", "solB2S_tablero_20261031_112243")
    destino = ARCHIVO / "intentos"
    destino.mkdir(parents=True, exist_ok=True)
    movidos = []
    for nombre in ["original"]+[f"original{i}" for i in range(2,9)]+["original.log","original.err"]:
        p = TMP / nombre
        if not p.exists():
            continue
        if (destino/nombre).exists():
            raise FileExistsError(destino/nombre)
        n = sum(f.stat().st_size for f in p.rglob("*") if f.is_file()) if p.is_dir() else p.stat().st_size
        shutil.move(str(p), destino/nombre)
        movidos.append(dict(origen=str(p),archivo=str(destino/nombre),bytes=n))
    # .99 lo genero el primer intento B2S; fallo por no existir Screenshot.png.
    p = Path(r"E:\DBZ3HD_trabajo\R7\emu\pcsx2_luna\sstates\SLES-51233 (37472BE0).99.p2s")
    if p.exists():
        import zipfile,struct
        with zipfile.ZipFile(p) as z:
            ram=z.read("eeMemory.bin")
            propio = "Screenshot.png" not in z.namelist() and struct.unpack_from("<I",ram,0x583a10)[0]==0 and struct.unpack_from("<I",ram,0x361514)[0]==15782
        if propio:
            n=p.stat().st_size
            shutil.move(str(p), destino/p.name)
            movidos.append(dict(origen=str(p),archivo=str(destino/p.name),bytes=n))
    guardar(ROOT/"studies 5/ENTREGAS/B2S_archivo.json",dict(movidos=movidos,total_bytes=sum(f["bytes"] for f in movidos)))
    print("Archivados sin borrar:",sum(f["bytes"] for f in movidos),"bytes")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "fijar":
        fijar()
    elif cmd == "tablero":
        raise SystemExit(tablero())
    elif cmd == "bateria":
        raise SystemExit(bateria())
    elif cmd == "cierre":
        cierre()
    elif cmd == "archivar":
        archivar()

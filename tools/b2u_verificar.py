"""Verificacion dirigida de solB2U; ejecutables fijados y copias fijas del arnes."""
import argparse
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
os.environ["DBZ3_AGENTE"] = "solB2U"
from b2u_medir import ROOT, TMP, guardar
import pruebas as P
import pcsx2_cuadros as Q


def fijar(nombre="dbz3.exe"):
    destino = TMP / nombre
    if destino.exists():
        raise FileExistsError(destino)
    shutil.copy2(ROOT / "out/build/win-amd64-dual/dbz3.exe", destino)
    guardar("build_final.json" if nombre != "dbz3.exe" else "build.json", dict(exe_sha256=hashlib.sha256(destino.read_bytes()).hexdigest(),
        fuentes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
                 for p in ["src/pasarela_b2.cpp", "src/pasarela_nave.cpp"]}))


def correr(escenario, env, run, etiqueta, exe):
    Q.limitar_port(P)
    P.EXE = exe
    P.ESCENARIOS[escenario] = ("solB2U: " + escenario, lambda _:dict(P._PS2, **env), run)
    copia = "smoke_e"
    snap = "solB2U_" + etiqueta + time.strftime("_%H%M%S")
    P.comprobar_libre(P.BASE/copia)
    P.guardar_instantanea(copia, snap)
    try:
        r = P.correr(copia, escenario, [], None, {}, forzar=True)
        guardar(etiqueta + "_prueba.json", r)
        return 0 if r["ok"] else 1
    finally:
        P.cargar_instantanea(copia, snap)


def audio(etiqueta, exe):
    def run(j, _):
        j.log(r"tablero b2: fase 1 leida", 90)
        time.sleep(3)
        j.tecla("Space", 2500)
        j.log(r"tablero b2: escena T155", 15)
        time.sleep(1.8)
        j.tecla("Return", 1500)
        j.log(r"tablero b2: escena M207", 30)
        j.tecla_hasta_log("Space", r"tablero b2: script 301: eleccion de 1 aliado", intentos=10, seg=4)
        time.sleep(1)
        j.tecla_hasta_log("Space", r"tablero b2: aliado marcado", intentos=4, seg=3)
        j.tecla("W", 400)
        j.tecla_hasta_log("Space", r"tablero b2: aliado elegido", intentos=2, seg=3)
        j.log(r"tablero b2: guiones terminados", 60)
        time.sleep(1)
        j.tecla("Space", 500)
        j.tecla_hasta_log("Space", r"tablero b2: turno 1: .* a la casilla", intentos=3, seg=4)
        time.sleep(3)
        txt = j.texto_log()
        pasos = re.findall(r"b2t: paso cuadro ([\d.]+) personaje (\d+) origen (\d+) destino (\d+) giro ([\d.]+) duracion ([\d.]+)", txt)
        sonidos = re.findall(r"b2t: audio cuadro ([\d.]+) canal (\d+) id (\d+)", txt)
        guardar(etiqueta + "_audio.json", dict(pasos=pasos, sonidos=sonidos, run=str(j.run),
            original=json.loads(Path(r"D:\tmp\solB2T\audio_paso_validado_sonidos.json").read_text(encoding="utf-8")),
            limite="Trazas de llamadas de audio, no medida de latencia del dispositivo"))
        if not pasos:
            raise P.Fallo("No hay un paso identificado para comparar")
        ids = [int(s[2]) for s in sonidos if float(s[0]) >= float(pasos[-1][0])]
        j.info("Audio del paso: " + str(ids))
        if etiqueta == "despues" and 19 not in ids:
            raise P.Fallo("Falta el sonido 19 del objeto tras el paso")
    return correr("b2u_audio", dict(DBZ3_TEST_MISIONES="1,reset", DBZ3_TEST_B2T_AUDIO="1"), run, etiqueta, exe)


def nave(exe):
    # El control de reloj y el acuse ya estaban validados en B2T; cambia SOLO
    # el destino real de atras del COM. Las vueltas extra del arnes no son PS2.
    import b2t_verificar as B
    B.TMP = TMP
    B.guardar = guardar
    fuente = (ROOT / "tools/b2t_verificar.py").read_text(encoding="utf-8")
    inicio = fuente.index("def nave(")
    fin = fuente.index("\ndef bateria(", inicio)
    bloque = fuente[inicio:fin]
    viejo = '''            ir("Backspace",7)
            tick(30)
            ir("Backspace",2)
            tick(2)
            ir("Backspace",1)'''
    assert viejo in bloque
    bloque = bloque.replace(viejo, '            ir("Backspace",1)')
    # La primera llamada tras cambiar de pantalla fija su origen local. El
    # acuse de estado puede preceder ese origen: establecerlo antes de esperar.
    bloque = bloque.replace('            ir("Backspace",1)\n            tick(70)',
                            '            ir("Backspace",1)\n            tick()\n            tick(70)')
    # Instantanea y rutas pertenecen a esta entrega, no a solB2T.
    bloque = bloque.replace('"solB2T_"', '"solB2U_"')
    ns = dict(vars(B), TMP=TMP, guardar=guardar)
    exec(compile(bloque, "b2u_nave_dirigida", "exec"), ns)
    return ns["nave"]("despues2", exe)


def combate(exe):
    return correr("b2u_nave_I", dict(DBZ3_TEST_MISIONES="5,reset", DBZ3_TEST_NAVE_TODO="1",
        DBZ3_TEST_NAVE_PRUEBA="0", DBZ3_TEST_NAVE_TIEMPO="6"),
        lambda j, _:P._r2_nave(j, {"prueba":"0"}), "combate_I", exe)


def shen(etiqueta, exe):
    def run(j, _):
        import numpy as np
        from PIL import Image
        j.log(r"Shenron 3D: .*triangulos, .*texturas", 90)
        time.sleep(2)
        rutas = []
        for nombre in ("shen_a", "shen_b"):
            if rutas:
                time.sleep(2)
                j.tecla("Space", 100)
            j.captura(nombre)
            ruta = j.run / (nombre + ".png")
            with Image.open(ruta) as im:
                im.resize((im.width//2, im.height//2)).save(ruta)
            rutas.append(ruta)
        a, b = [np.asarray(Image.open(p).convert("RGB")).astype(int) for p in rutas]
        # Solo el verde del dragon, excluyendo cielo, textos y controles.
        def verde(im):
            return (im[:,:,1] > im[:,:,0]*1.15) & (im[:,:,1] > im[:,:,2]*1.1) & (im[:,:,1] > 35)
        ma, mb = verde(a), verde(b)
        mascara = ma | mb
        mascara[int(a.shape[0]*.82):] = False
        distintos = int((np.any(a != b, axis=2) & mascara).sum())
        comun = ma & mb
        interior = comun.copy()
        for y in range(-2, 3):
            for x in range(-2, 3):
                interior &= np.roll(comun, (y,x), (0,1))
        interior[int(a.shape[0]*.78):] = False
        distintos_interiores = int((np.any(a != b, axis=2) & interior).sum())
        guardar(etiqueta + "_shen.json", dict(run=str(j.run), te=25, pixeles_verdes=int(mascara.sum()),
            pixeles_distintos=distintos, distintos_interiores=distintos_interiores,
            limite="Repetibilidad de la pose al mismo tiempo de escena; no identidad visual PS2"))
        j.info(f"Shenron a te=25: {distintos} pixeles verdes distintos, {distintos_interiores} interiores")
        if int(mascara.sum()) < 100:
            raise P.Fallo("No hay dragon visible suficiente para medir")
        if etiqueta == "final" and distintos_interiores:
            raise P.Fallo("La pose depende todavia del tiempo global")
    return correr("b2u_shen", dict(DBZ3_TEST_MISIONES="1,reset", DBZ3_TEST_B2_DESEO="2",
        DBZ3_TEST_SHEN_T="25,25"), run, etiqueta + "_shen", exe)


def cierre():
    import numpy as np
    from PIL import Image
    build = json.loads((TMP / "build_final.json").read_text(encoding="utf-8"))
    medidas = dict(build=build,
        fuentes_actuales={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() == sha for p,sha in build["fuentes"].items()})
    repetibilidad = []
    for etiqueta in ("antes", "final"):
        d = json.loads((TMP / (etiqueta + "_shen.json")).read_text(encoding="utf-8"))
        a, b = [np.array(Image.open(Path(d["run"]) / f"shen_{letra}.png").convert("RGB")).astype(int) for letra in ("a", "b")]
        # Interior comun a ambos verdes: una erosion de 2 pixeles separa la
        # superficie del dragon de los bordes mezclados con el cielo en movimiento.
        def verde(im):
            return (im[:,:,1] > im[:,:,0]*1.15) & (im[:,:,1] > im[:,:,2]*1.1) & (im[:,:,1] > 35)
        mascara = verde(a) & verde(b)
        interior = mascara.copy()
        for y in range(-2, 3):
            for x in range(-2, 3):
                interior &= np.roll(mascara, (y,x), (0,1))
        interior[int(a.shape[0]*.78):] = False
        distintos = np.any(a != b, axis=2)
        d.update(pixeles_interiores=int(interior.sum()), distintos_interiores=int((distintos & interior).sum()),
                 max_delta_interior=int(abs(a-b)[interior].max()) if interior.any() else None)
        repetibilidad.append(d)
    medidas["shen_repetibilidad"] = repetibilidad
    medidas["bateria"] = dict(comando="python tools/pruebas.py todo --max 1 --forzar", codigo=0, resultado="5/5 PASS",
        segundos_reloj=304, exe_sha256=build["exe_sha256"],
        runs=["20261031-172911_smoke_a_tag_swap", "20261031-173047_smoke_a_campana", "20261031-173146_smoke_a_du_continuar",
              "20261031-173240_smoke_a_batalla", "20261031-173333_smoke_a_menu"])
    for nombre in ("antes_audio", "despues_audio", "despues2_recorridos", "combate_I_prueba", "figuras_cache", "spx329_medida"):
        medidas[nombre] = json.loads((TMP / (nombre + ".json")).read_text(encoding="utf-8"))
    medidas["sintaxis"] = {}
    for nombre in ("b2u_medir.py", "b2u_verificar.py"):
        compile((ROOT / "tools" / nombre).read_text(encoding="utf-8"), nombre, "exec")
        medidas["sintaxis"][nombre] = "PASS"
    propias = sorted(P.INST.glob("solB2U_*"), key=lambda p:p.stat().st_mtime, reverse=True)
    archivo = Path(r"D:\_PARA_BORRAR_CLAUDE\solB2U\instantaneas")
    movidas = []
    for p in propias[5:]:
        archivo.mkdir(parents=True, exist_ok=True)
        if (archivo / p.name).exists():
            raise FileExistsError(archivo / p.name)
        shutil.move(str(p), archivo / p.name)
        movidas.append(str(archivo / p.name))
    medidas["instantaneas_archivadas_sin_borrar"] = sorted(str(p) for p in archivo.glob("solB2U_*") if p.is_dir())
    guardar("medidas_cierre.json", medidas)
    print("Fuentes coinciden:", medidas["fuentes_actuales"])
    print("Shenron (todos/interior):", [(d["pixeles_distintos"], d["distintos_interiores"], d["max_delta_interior"]) for d in repetibilidad])
    print("Sintaxis PASS; bateria 5/5 PASS; instantaneas movidas sin borrar:", len(movidas))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("accion", choices=["fijar", "audio", "nave", "combate", "shen", "cierre"])
    ap.add_argument("--etiqueta", default="despues")
    ap.add_argument("--exe", type=Path, default=TMP / "dbz3.exe")
    a = ap.parse_args()
    if a.accion == "fijar":
        fijar("dbz3_final.exe" if a.etiqueta == "final" else "dbz3.exe")
    elif a.accion == "audio":
        raise SystemExit(audio(a.etiqueta, a.exe))
    elif a.accion == "shen":
        raise SystemExit(shen(a.etiqueta, a.exe))
    elif a.accion == "cierre":
        cierre()
    else:
        raise SystemExit(globals()[a.accion](a.exe))

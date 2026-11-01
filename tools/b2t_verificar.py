"""Pruebas dirigidas B2T con copias fijas y restauracion de la partida."""
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
from b2t_medir import ROOT, TMP, guardar
import pcsx2_cuadros as Q
import pruebas as P


def fijar():
    destino = TMP / "dbz3.exe"
    if destino.exists():
        raise FileExistsError(destino)
    shutil.copy2(ROOT / "out/build/win-amd64-dual/dbz3.exe", destino)
    guardar("build.json", dict(exe_sha256=hashlib.sha256(destino.read_bytes()).hexdigest(),
        fuentes={rel:hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()
            for rel in ("src/pasarela_b2.cpp", "src/pasarela_nave.cpp", "src/pasarela_misiones.cpp")}))


def nave(etiqueta, exe):
    Q.limitar_port(P)
    P.EXE = exe
    reloj = TMP / (etiqueta + "_reloj.txt")
    reloj.write_text("0", encoding="ascii")
    medidas = []
    def run(j, _):
        campo = 0
        def tick(n=1):
            nonlocal campo
            campo += n
            cuadro = campo*1.2
            Q.fijar_reloj(reloj, cuadro)
            # Acuse del campo solicitado (no nueva partida ni espera de escena).
            patron = r"solP cuadro " + re.escape(f"{cuadro:.3f}") + r" estado"
            fin = time.monotonic()+12
            while not re.search(patron,j.texto_log()):
                if time.monotonic()>fin:
                    raise P.Fallo("Sin acuse del campo " + str(campo))
                time.sleep(.01)
            return estado()
        def estado():
            rs = re.findall(r"solP cuadro [\d.]+ estado (\d+) fase", j.texto_log())
            return int(rs[-1]) if rs else -1
        def ir(tecla, destino, limite=160):
            ini, desde = campo, estado()
            j.tecla(tecla, 100)
            for _ in range(limite):
                if tick() == destino:
                    medidas.append(dict(desde=desde, destino=destino, inicio=ini, campo=campo, tecla=tecla))
                    return
            raise P.Fallo(f"No paso de {desde} a {destino}")
        def foto(nombre):
            from PIL import Image
            j.captura(nombre)
            ruta = j.run / (nombre+".png")
            with Image.open(ruta) as im:
                im.resize((im.width//2, im.height//2)).save(ruta)
        j.log(r"nave: entrar", 90)
        tick(100)
        tick(100)  # el primer reloj puede coincidir con la creacion de kModo
        # El panel de misiones conserva su fundido REAL (alpha += dt*4) y
        # wait_release descarta entradas hasta alpha=1; el reloj de Nave no lo acelera.
        time.sleep(.5)
        ir("Space", 1)
        tick(70)
        for planta in range(1,5):
            if planta > 1:
                j.tecla("S",100)
                tick(2)
            ir("Space",2)
            tick(60)
            if planta == 1:
                j.log(r"modelo B3 con huesos fid 264 cargado",90)
            foto(f"planta{planta}_figura")
            ir("Space",7)
            tick(30)
            ir("Space",3)
            tick(60)
            ir("Space",4)
            tick(60)
            if planta == 1:
                foto("planta1_burbuja")
            ir("Backspace",3)
            tick(60)
            ir("Backspace",7)
            tick(30)
            ir("Backspace",2)
            tick(2)
            ir("Backspace",1)
            tick(70)
        guardar(etiqueta+"_recorridos.json", dict(medidas=medidas, run=str(j.run),
            limite="Recorridos de seleccion sin combate; no certifica equivalencia visual por campo"))
        j.info("Cuatro plantas: rueda, menu, COM, burbuja y regreso; sin combate")
    P.ESCENARIOS["b2t_nave"] = ("B2T: cuatro selecciones de Nave por reloj PAL",
        lambda _:dict(P._PS2, DBZ3_TEST_MISIONES="5,reset", DBZ3_TEST_NAVE_TODO="1",
            DBZ3_TEST_NAVE_CUADRO=str(reloj), DBZ3_SEL_POSEFRAC="0.25"), run)
    copia = "smoke_e"
    snap = "solB2T_"+etiqueta+time.strftime("_%H%M%S")
    P.comprobar_libre(P.BASE/copia)
    P.guardar_instantanea(copia,snap)
    try:
        r = P.correr(copia,"b2t_nave",["DBZ3_TEST_OCULTO=0"],None,{},forzar=True)
        guardar(etiqueta+"_prueba.json",r)
        return 0 if r["ok"] else 1
    finally:
        P.cargar_instantanea(copia,snap)


def bateria():
    Q.limitar_port(P)
    P.EXE = TMP/"dbz3.exe"
    os.environ["DBZ3_PRUEBAS_EXE"] = str(P.EXE)
    os.environ["DBZ3_PRUEBAS_COPIAS"] = "smoke_f"
    r = P.main(["todo","--max","1","--forzar"])
    guardar("bateria.json",dict(codigo=r, exe_sha256=hashlib.sha256((TMP/"dbz3.exe").read_bytes()).hexdigest()))
    return r


def cierre():
    import numpy as np
    from PIL import Image,ImageDraw
    antes=json.loads((TMP/"antes_corregido_prueba.json").read_text(encoding="utf-8"))
    despues=json.loads((TMP/"despues_corregido_prueba.json").read_text(encoding="utf-8"))
    imagenes=[]
    hoja=Image.new("RGB",(800,450))
    for planta in range(1,5):
        a=Image.open(Path(antes["run"])/f"planta{planta}_figura.png").convert("RGB")
        b=Image.open(Path(despues["run"])/f"planta{planta}_figura.png").convert("RGB")
        # Figura en el tercio izquierdo; no interpretar el cambio del cielo como fidelidad.
        aa=np.array(a)[45:445,100:315].astype(int)
        bb=np.array(b)[45:445,100:315].astype(int)
        df=bb-aa
        imagenes.append(dict(planta=planta, pixeles_distintos=int(np.any(df!=0,axis=2).sum()),
            rgb_diferencia_media=float(abs(df).mean()),rgb_mayor=int(df.max()),rgb_menor=int(df.min()),
            limite="Cambio visible antes/despues, no medida de identidad contra PS2"))
        if planta==1:
            hoja.paste(a.resize((400,225)),(0,24))
            hoja.paste(b.resize((400,225)),(400,24))
    ImageDraw.Draw(hoja).text((4,4),"ANTES (izq.) / DESPUES (der.) - misma pose de figura",fill="white")
    hoja.crop((0,0,800,249)).save(TMP/"figura_antes_despues.jpg")
    build=json.loads((TMP/"build.json").read_text(encoding="utf-8"))
    nativos={}
    for i in range(1,5):
        p=TMP/f"nave_recorridos_validos_gp3_p{i}_atras_torre.json"
        nativos[i]=json.loads(p.read_text(encoding="utf-8")) if p.exists() else None
    datos=dict(build=build, fuentes_actuales={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==sha
        for p,sha in build["fuentes"].items()},
        resultado=json.loads((TMP/"resultado_limites.json").read_text(encoding="utf-8")),
        nave=dict(antes=antes,despues=despues,imagenes=imagenes,
            originales_completos={i:bool(d and d.get("completa")) for i,d in nativos.items()}),
        audio_original=json.loads((TMP/"audio_paso_validado_sonidos.json").read_text(encoding="utf-8")),
        bateria=json.loads((TMP/"bateria.json").read_text(encoding="utf-8")),
        pendientes=["Comparacion completa por campo de los cuatro recorridos frente al original y combate I",
            "Escala y pelo de todo el roster", "Audio de todos los eventos contra port",
            "SPX 329: ranuras 5/6, particulas y animaciones de Shenron", "Resto de diferencias de Shenron"])
    propias=sorted((p for p in P.INST.glob("solB2T_*") if p.is_dir()),key=lambda p:p.stat().st_mtime,reverse=True)
    archivo=Path(r"D:\_PARA_BORRAR_CLAUDE\solB2T\instantaneas")
    movidas=[]
    for p in propias[5:]:
        archivo.mkdir(parents=True,exist_ok=True)
        dst=archivo/p.name
        if dst.exists():
            raise FileExistsError(dst)
        shutil.move(str(p),dst)
        movidas.append(str(dst))
    datos["instantaneas_archivadas_sin_borrar"]=movidas
    guardar("medidas_cierre.json",datos)
    (ROOT/"studies 5/ENTREGAS/B2T_medidas.json").write_text(json.dumps(datos,indent=2,ensure_ascii=False),encoding="utf-8")
    print("Nave antes/despues",antes["ok"],despues["ok"],"bateria",datos["bateria"],"fuentes",datos["fuentes_actuales"])
    print("Cambios de imagen:",imagenes)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("accion",choices=["fijar","nave","bateria","lanzar-nave","lanzar-bateria","restaurar","cierre"])
    ap.add_argument("--etiqueta",default="despues")
    ap.add_argument("--exe",type=Path,default=TMP/"dbz3.exe")
    ap.add_argument("--instantanea")
    a = ap.parse_args()
    os.environ["DBZ3_AGENTE"] = "solB2T"
    if a.accion == "fijar":
        fijar()
    elif a.accion.startswith("lanzar-"):
        accion = a.accion.split("-",1)[1]
        destino = TMP/(a.etiqueta+"_"+accion+".log")
        with destino.open("x",encoding="utf-8") as out:
            env = dict(os.environ,PYTHONIOENCODING="utf-8",PYTHONUNBUFFERED="1")
            p = subprocess.Popen([sys.executable,str(Path(__file__).resolve()),accion,
                "--etiqueta",a.etiqueta,"--exe",str(a.exe)],stdout=out,stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL,env=env,
                creationflags=subprocess.DETACHED_PROCESS|subprocess.CREATE_NEW_PROCESS_GROUP)
        print("Prueba en cola, PID",p.pid,"log",destino)
    elif a.accion == "restaurar":
        P.comprobar_libre(P.BASE/"smoke_e")
        P.cargar_instantanea("smoke_e",a.instantanea)
    elif a.accion == "cierre":
        cierre()
    else:
        raise SystemExit(nave(a.etiqueta,a.exe) if a.accion == "nave" else bateria())

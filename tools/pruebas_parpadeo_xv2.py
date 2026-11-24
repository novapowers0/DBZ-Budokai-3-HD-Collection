"""R16 solPARPADEO: rafagas reales, un BMP por pintado consecutivo."""
import hashlib
import json
import os
import shutil
import time


def registrar(g):
    def entorno(p):
        return dict(DBZ3_TEST_FREECAM="0,0,0,0,0,45", DBZ3_COMBATE_SONDA="1", DBZ3_COMBATE_MUNECO="quieto",
                    DBZ3_TEST_CFG='dbz3_combate_prueba_muneco="quieto";dbz3_cine_intro=false;dbz3_camara_sigue=false')

    def preparar(j, p, env):
        p.setdefault("xv2", "HUM")
        p["fase"] = "parpadeo"
        # Colores constantes durante toda la sesion: gi verde, pantalon azul,
        # botas y guantes oscuros, pelo rubio (HSV del creador).
        # Orden de vista_heroe.XV2_ZONAS: piel, pelo, torso, piernas, botas, guantes.
        p.setdefault("partes", "0,0,0|45,70,15|120,80,0|225,70,0|225,-200,-100|225,-200,-100")
        g["_prep_heroe_exportado"](j, p, env)
        if "escenario" in p:
            env["DBZ3_TEST_BATTLE"] = "%s,7,%d" % (p["_id"], int(p["escenario"]))

    def ejecutar(j, p):
        import numpy as np
        from PIL import Image
        g["_esperar_pelea"](j)
        # Confirmar el estimulo: no aceptar una rafaga tapada por ataques del COM.
        j.tecla("set dbz3_combate_prueba_muneco=quieto", 300)
        j.log(r"muneco en el lado 2 \(quieto\)", 20)
        j.log(r"camara libre: activa", 30)
        g["_colocar"](j, ["-55,0,0", "55,0,0"])
        # Encuadre conocido, relativo al actor y a su orientacion anatomica.
        mgr = int.from_bytes(g["leer"](j.h, g["GBASE"] + 0x823D6CF4, 4), "big")
        actor = mgr + 2096
        pie = g["struct"].unpack(">3f", g["leer"](j.h, g["GBASE"] + actor + 64, 12))
        giro = int.from_bytes(g["leer"](j.h, g["GBASE"] + actor + 6052, 4), "big") & 65535
        a = giro * (2 * g["math"].pi / 65536)
        obj = (pie[0], pie[1] + 10, pie[2])
        distancia = float(p.get("distancia", "36"))
        ojo = (obj[0] + distancia * g["math"].sin(a), obj[1], obj[2] + distancia * g["math"].cos(a))
        (j.dest / "dbz3_freecam.req").write_text(
            "mirar 1 %s 45" % " ".join("%.5f" % v for v in (*ojo, *obj)), encoding="ascii")
        j.log(r"camara libre: prueba mirar 1 ojo", 20)
        g["dormir"](1)
        for nombre, entrada in (("reposo", None), ("guardia", "Space 12000")):
            if entrada:
                j.tecla(entrada, 200)
            previas = j.run / (nombre + "_anteriores")
            for vieja in j.dest.glob("dbz3_shot_*.bmp"):
                previas.mkdir(exist_ok=True)
                shutil.move(str(vieja), str(previas / vieja.name))
            n = int(p.get("cuadros", "60"))
            tmp = j.dest / "dbz3_shot.tmp"
            tmp.write_text("%d 1" % n, encoding="ascii")
            os.replace(tmp, j.dest / "dbz3_shot.req")
            fin = time.time() + 60
            ultimo = j.dest / ("dbz3_shot_%03d.bmp" % (n - 1))
            while time.time() < fin and (not ultimo.exists() or ultimo.stat().st_size < 54):
                if not j.vivo():
                    raise g["Fallo"]("se cerro durante la rafaga")
                time.sleep(.1)
            time.sleep(.3)
            frames, hashes = [], []
            for k in range(n):
                src = j.dest / ("dbz3_shot_%03d.bmp" % k)
                if not src.exists():
                    raise g["Fallo"]("rafaga incompleta: falta %s" % src.name)
                dst = j.run / ("%s_%03d.png" % (nombre, k))
                with Image.open(src) as im:
                    im.convert("RGB").save(dst)
                    frames.append(np.asarray(im.convert("RGB"), dtype=np.int16))
                hashes.append(hashlib.sha256(dst.read_bytes()).hexdigest())
            # Diferencias informativas: una animacion real cambia entre cuadros;
            # este dato NO convierte el PASS funcional en una aceptacion visual.
            dif = [float(np.abs(b - a).mean()) for a, b in zip(frames, frames[1:])]
            (j.run / (nombre + "_medidas.json")).write_text(json.dumps(dict(
                raza=p["xv2"], cuadros=n, paso_pintados=1, colores=p["partes"],
                distancia=distancia, escenario=int(p.get("escenario", "2")),
                sha256=hashes, diferencias_medias=dif), indent=2), encoding="utf-8")
            thumb = Image.new("RGB", (frames[0].shape[1] * 4, frames[0].shape[0] * 3))
            for celda, k in enumerate(np.linspace(0, n - 1, 12, dtype=int)):
                thumb.paste(Image.fromarray(frames[k].astype("uint8")),
                            ((celda % 4) * frames[k].shape[1], (celda // 4) * frames[k].shape[0]))
            thumb.save(j.run / (nombre + "_lamina.png"))
            j.info("PARPADEO XV2 %s %s: %d pintados consecutivos capturados; revision visual pendiente" % (p["xv2"], nombre, n))
        sonda = j.dest / "logs" / "combate_sonda.csv"
        if sonda.exists():
            shutil.copy2(sonda, j.run / "combate_sonda.csv")

    g["ESCENARIOS"]["xv2_parpadeo"] = (
        "R16 solPARPADEO: colores fijos y 60 pintados consecutivos en reposo y guardia; --par xv2=HUM|HUF|MAM",
        entorno, ejecutar, preparar)

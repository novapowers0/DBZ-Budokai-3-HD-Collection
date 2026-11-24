"""R16: primeros planos del cuerpo XV2 exportado y del B3 de referencia."""
import csv
import json
import os
import shutil
import time
from collections import Counter
from pathlib import Path


def resumen_sonda(path):
    with Path(path).open(encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    return {"filas": len(rows), "codigos": dict(Counter(r.get("1_codigo") for r in rows)),
            "acciones": dict(Counter(r.get("1_accion") for r in rows)),
            "ultimas": rows[-3:]}


def registrar(g):
    def entorno(p):
        poses = p.get("fase") == "poses"
        e = dict(DBZ3_TEST_CARA="0", DBZ3_TEST_CARA_HUESO="cabeza" if poses else "codo_der",
                    DBZ3_TEST_CARA_ESPERA="60", DBZ3_TEST_CARA_DURACION="0",
                    DBZ3_TEST_CARA_DIST_BRAZO="7", DBZ3_TEST_CARA_LADO_BRAZO="2",
                    DBZ3_TEST_CARA_DIST="7", DBZ3_TEST_CARA_LADO="1.2",
                    DBZ3_TEST_CARA_QUIETO="0" if poses else "1",
                    DBZ3_COMBATE_SONDA="1")
        if poses:
            # La sonda de cara cambia la camara durante la tecnica. Para medir
            # su codigo se conserva la camara nativa y el entorno de esp.
            e = dict(DBZ3_TEST_ESP=p.get("direccion", "9"), DBZ3_COMBATE_SONDA="1",
                     DBZ3_TEST_CFG='dbz3_combate_prueba_muneco="quieto"')
        elif p.get("fase") in ("movimientos", "acciones"):
            e = dict(DBZ3_COMBATE_SONDA="1",
                     DBZ3_TEST_CFG='dbz3_combate_prueba_muneco="quieto";dbz3_combate_prueba_ki=1000')
        return e

    def preparar(j, p, env):
        raza = p.get("raza", "HUM").upper()
        if raza not in ("HUM", "MAM", "FRI"):
            raise g["Fallo"]("raza: HUM, MAM o FRI")
        if p.get("nativo") == "1":
            cid = {"HUM": 0, "MAM": 34, "FRI": 27}[raza]
            env["DBZ3_TEST_BATTLE"] = "%d,7,2" % cid
            p["_id"] = cid
        else:
            p["xv2"] = raza
            p.setdefault("fase", "planos")
            g["_prep_heroe_exportado"](j, p, env)

    def ejecutar(j, p):
        dormir = g["dormir"]
        sonda = j.dest / "logs" / "combate_sonda.csv"

        def filas():
            try:
                with sonda.open(encoding="utf-8") as fh:
                    return [r for r in csv.DictReader(fh) if r.get("2_z") is not None]
            except (OSError, UnicodeError):
                return []

        def esperar(condicion, segundos, motivo):
            fin = time.time() + segundos
            while time.time() < fin:
                rows = filas()
                if rows and condicion(rows):
                    return rows
                dormir(.08)
            raise g["Fallo"]("CUERPO XV2: " + motivo)
        def cam(texto):
            req = j.dest / "dbz3_cam.req"
            tmp = req.with_suffix(".tmp")
            tmp.write_text(texto + "\n", encoding="ascii")
            os.replace(tmp, req)
            dormir(.35)

        def foto(nombre):
            j.captura(nombre)
            Image = g["Image"]
            with Image.open(j.run / (nombre + ".png")) as im:
                im.resize((im.width // 2, im.height // 2), Image.Resampling.LANCZOS).save(j.run / (nombre + "_05.png"))
            (j.run / (nombre + "_sonda.json")).write_text(
                json.dumps(filas()[-8:], ensure_ascii=False, indent=2), encoding="utf-8")

        j.log(r"tag: empieza un combate", 90)
        g["_esperar_pelea"](j)
        if p.get("fase") in ("movimientos", "acciones"):
            dormir(1.)
            j.tecla("Space 2200", 700)
            esperar(lambda r: r[-1].get("1_accion") == "1" and r[-1].get("1_codigo") == "002",
                    2, "sin guardia nativa 1/002")
            foto("h_guardia")
            dormir(2.)
            # Descubrir la entrada efectiva sobre la partida del jugador en
            # vez de confundir la regeneracion pasiva con una carga nativa.
            cargando = False
            entradas = ("L+Space 5000", "Space+A 80\nSpace+A 5000", "Space+D 80\nSpace+D 5000", "P+Backspace 5000") if p.get("fase") == "movimientos" else ()
            for entrada in entradas:
                j.tecla("set dbz3_combate_prueba_ki=1000", 500)
                ki0 = float(filas()[-1]["1_ki"])
                j.tecla(entrada, 300)
                try:
                    esperar(lambda r: r[-1].get("1_accion") == "21" and
                            float(r[-1]["1_ki"]) > ki0 + 100, 4,
                            "sin carga nativa 21 con " + entrada.replace("\n", ";"))
                    j.info("CUERPO XV2: carga nativa con " + entrada.replace("\n", ";"))
                    cargando = True
                    break
                except g["Fallo"]:
                    dormir(2.)
            if entradas:
                if not cargando:
                    raise g["Fallo"]("CUERPO XV2: ninguna entrada produjo carga nativa 21")
                foto("i_carga_ki")
                dormir(7.)
            prev = filas()[-1]["cuadro"]
            # Un golpe dura menos que la captura de ventana. Conservar tambien
            # un clip corto para seleccionar su fotograma sin fingir una pose.
            j.clip("golpe", 5, fps=15)
            dormir(1.)
            j.tecla("L 100\nL 100\nL 100\nL 100\nL 100\nL 100", 50)
            esperar(lambda r: r[-1]["cuadro"] != prev and
                    r[-1].get("1_accion") == "15", 4,
                    "sin accion de ataque tras el boton de golpe")
            foto("j_golpe")
            j.clips_fin()
            j.info("CUERPO XV2: guardia 1/002 y ataque accion 15" + (", carga 21" if cargando else "; carga NO ensayada en esta fase") + "; clip y traza para revisar las fotos 0.5")
            return
        if p.get("fase") != "poses":
            j.log(r"sonda de brazo: quieto", 30)
            for lado, vista, nombre in (("der", "frente", "a_frente_der"), ("der", "espalda", "b_espalda_der"),
                                        ("izq", "frente", "c_frente_izq"), ("izq", "espalda", "d_espalda_izq")):
                cam("codo_%s %s 7 2" % (lado, vista))
                foto(nombre)
            cam("off")
            j.info("CUERPO XV2: cuatro planos de brazos en reposo; PNG _05 a escala 0.5")
            return
        # Las poses se prueban en otra sesion, sin el mando virtual de las fotos.
        # Se exige un codigo especial actual, no uno antiguo de la traza.
        fin = time.time() + 90
        while time.time() < fin:
            try:
                with sonda.open(encoding="utf-8") as fh:
                    rows = list(csv.DictReader(fh))
            except (OSError, UnicodeError):
                rows = []
            if rows and any((r.get("1_codigo") or "").upper() == "24A" for r in rows[-8:]):
                if p.get("video") == "1":
                    j.clip("rayo", 6, fps=15)
                foto("f_rayo")
                if p.get("video") == "1":
                    dormir(1.5)
                    foto("f_rayo_despues")
                    j.clips_fin()
                j.info("CUERPO XV2: especial 0x24A actual; PNG _05 a escala 0.5")
                cam("off")
                return
            dormir(.12)
        raise g["Fallo"]("CUERPO XV2: no se capturo el especial 0x24A actual")

    def con_traza(j, p):
        try:
            return ejecutar(j, p)
        finally:
            sonda = j.dest / "logs" / "combate_sonda.csv"
            if sonda.exists():
                shutil.copy2(sonda, j.run / "combate_sonda.csv")
                (j.run / "sonda_resumen.json").write_text(
                    json.dumps(resumen_sonda(sonda), ensure_ascii=False, indent=2), encoding="utf-8")

    g["ESCENARIOS"]["xv2_cuerpo_combate"] = (
        "R16 cuerpo XV2: --par raza=HUM|MAM|FRI; --par fase=planos|poses|acciones|movimientos; --par nativo=1 para referencia B3",
        entorno, con_traza, preparar)


if __name__ == "__main__":
    import sys
    print(json.dumps(resumen_sonda(sys.argv[1]), ensure_ascii=False, indent=2))

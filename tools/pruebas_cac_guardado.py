"""R16: crear/guardar/reiniciar/recargar sin soltar el candado de smoke_c.

Uso: python tools/pruebas.py correr smoke_c cac_guardado --par raza=FRI
     --par raza=todas recorre las seis razas, cada una con user_data nuevo.
Los datos previos se apartan y restauran; los de la prueba quedan en su evidencia.
"""
import json
import re
from pathlib import Path
import subprocess
import time
import tomllib

RAZAS = {"FRI": (10, 113), "HUF": (8, 111), "HUM": (7, 110),
         "MAF": (12, 115), "MAM": (11, 114), "NMC": (9, 112)}


def mover_guardados(desde, hacia):
    """La cache de shaders no es partida; un generador hijo puede retenerla abierta.

    rename sin fallback a copiar/borrar: si Windows bloquea una entrada, se conserva
    entera en el origen y se informa, nunca queda una copia a medias.
    """
    hacia.mkdir(parents=True, exist_ok=True)
    if not desde.exists():
        return
    for archivo in list(desde.iterdir()):
        if archivo.name == "cache":
            continue
        fin = time.monotonic() + 30
        while True:
            try:
                archivo.rename(hacia / archivo.name)
                break
            except PermissionError:
                if time.monotonic() >= fin:
                    raise
                time.sleep(.25)


def escenario(api, par):
    # Un perfil vacio ofrece Nueva partida/Cargar; el arranque generico alterna
    # solo START/A y se queda en Cargar. Este escenario usa el arranque de perfil nuevo.
    par["sin_arranque"] = "1"
    seleccion = par.get("raza", "FRI").upper()
    razas = list(RAZAS) if seleccion == "TODAS" else seleccion.split(",")
    if any(r not in RAZAS for r in razas):
        raise api.Fallo("raza debe ser FRI, HUF, HUM, MAF, MAM, NMC o todas")
    env = {"DBZ3_TEST_MODO": "heroe,0", "DBZ3_TEST_HEROE_ABRIR": "2",
           "DBZ3_TEST_CFG": "dbz3_cac_xv2=true;dbz3_heroe_ui_nueva=true;dbz3_language=5",
           "DBZ3_TEST_HEROE_RAZA": str(RAZAS[razas[0]][0]), "DBZ3_TEST_TURBO": "1"}

    def preparar(j, p, entorno):
        test = api.R / "out/build/win-amd64-dual/cac_guardado_test.exe"
        r = subprocess.run([str(test), str(j.run / "unitarios")], capture_output=True, text=True, errors="replace", timeout=30)
        (j.run / "unitarios.log").write_text(r.stdout + r.stderr, encoding="utf-8")
        print(r.stdout + r.stderr, end="", flush=True)
        if r.returncode:
            raise api.Fallo("persistencia nativa: " + (r.stderr or r.stdout).strip())
        origen = j.dest / "user_data/dbz3"
        origen.mkdir(parents=True, exist_ok=True)
        if p.get("recuperar"):
            recuperar = Path(p["recuperar"])
            if not recuperar.is_dir():
                raise api.Fallo("no existe el respaldo a recuperar")
            mover_guardados(origen, j.run / "antes_recuperar")
            mover_guardados(recuperar, origen)
        previo = j.run / "user_data_previo"

        def restaurar():
            if j.vivo():
                raise api.Fallo("el juego sigue vivo: no se cambia su carpeta de guardado")
            mover_guardados(origen, j.run / "user_data_final")
            if previo.exists():
                mover_guardados(previo, origen)
        j.restaurar_guardado = restaurar
        mover_guardados(origen, previo)
        j.cac_env = dict(entorno)

    def ejecutar(j, p):
        tabla = []
        catalogo = tomllib.loads((j.dest / "res/datos/xv2_cac.toml").read_text(encoding="utf-8"))
        tipos = ("Hair", "FaceBase", "Bust", "Pants", "Boots", "Rist", "FaceEye")
        nombres_color = ("piel", "pelo", "torso", "piernas", "botas", "guantes")

        def pulso(tecla):
            # Pulso corto, seguido de tiempo para observar tambien la suelta.
            # Con modelos generandose, encadenar teclas cada 250 ms perdia flancos.
            j.tecla(tecla + " 100", 650)

        def abrir_color(k):
            objetivo = "Color de " + nombres_color[k]
            patron = r"Create: panel de color de (Color de [^\r\n]+)"
            for _ in range(20):
                antes = len(re.findall(patron, j.texto_log()))
                pulso("Space")
                paneles = re.findall(patron, j.texto_log())
                if len(paneles) > antes:
                    if paneles[-1] == objetivo:
                        return
                    pulso("Space")
                    pulso("S")
                # Sobre una pieza, A avanza una fila; no se suma otra flecha.
            raise api.Fallo("no se pudo abrir el panel " + objetivo)

        def guardar_log(sub, fase):
            (sub / (fase + ".log")).write_text(j.texto_log(), encoding="utf-8")

        def esperar_creador(raza):
            j.log(r"modos: prueba: Modo Heroe", 90)
            j.log(r"Create abierto", 60)
            j.log(r"vista previa 3D .* cargada", float(p.get("vista", "180")))
            api.dormir(1)

        def ficha():
            t = tomllib.loads((j.dest / "user_data/dbz3/cac.toml").read_text(encoding="utf-8"))
            return t["heroe"][t.get("activo", 0)]

        def esperar_ficha(raza, anterior=None):
            fin = time.monotonic() + 30
            while time.monotonic() < fin:
                try:
                    h = ficha()
                    if h["raza"] == RAZAS[raza][0] and h["cuerpo"] == RAZAS[raza][1]:
                        if anterior is None or h.get("partes") != anterior.get("partes"):
                            return h
                except (OSError, ValueError, KeyError):
                    pass
                time.sleep(.2)
            raise api.Fallo(f"{raza}: no se guardo la ficha modificada")

        for numero, raza in enumerate(razas):
            sub = j.run / raza
            sub.mkdir()
            if numero:
                j.cerrar()
                if j.vivo():
                    raise api.Fallo("el proceso anterior no termino")
                mover_guardados(j.dest / "user_data/dbz3", j.run / razas[numero - 1] / "user_data")
                e = dict(j.cac_env, DBZ3_TEST_HEROE_RAZA=str(RAZAS[raza][0]))
                j.lanzar(e)
            j.arrancar(180, titulo=("Return", "W", "Space"))
            esperar_creador(raza)
            # Los hooks eligen la raza en memoria: se exporta a cac.toml al
            # confirmar, no al abrir Create. Antes solo existe la ficha migrada.
            inicial = ficha()
            # Siete piezas reales y seis colores; cada flecha queda comprobada en el log.
            for k in range(7):
                disponibles = [n for n in catalogo["raza"][raza]["parte"]
                               if n["tipo"] == tipos[k] and n.get("existe")]
                previo = len(re.findall(r"XV2: .* = modelo", j.texto_log()))
                j.tecla("D", 450)
                fin = time.monotonic() + 15
                while disponibles and len(re.findall(r"XV2: .* = modelo", j.texto_log())) <= previo:
                    if time.monotonic() > fin:
                        raise api.Fallo(f"{raza}: no se aplico la pieza {k}")
                    time.sleep(.2)
                j.tecla("S", 350)
            for k in range(6):
                abrir_color(k)
                for tecla in ("D", "S", "D", "S", "A", "Space"):
                    pulso(tecla)
                j.log(rf"Create: Color de {nombres_color[k]} = \(10, 10, -10\)", 15)
                pulso("S")
            j.tecla("3", 1000)  # RB = Confirmar con el creador nuevo.
            j.log(r"Create: confirmar", 30)
            guardada = esperar_ficha(raza, inicial)
            if any(c != [10, 10, -10] for c in guardada["partes"]):
                raise api.Fallo(f"{raza}: colores guardados inesperados {guardada['partes']}")
            (sub / "guardada.json").write_text(json.dumps(guardada, indent=2), encoding="utf-8")
            guardar_log(sub, "crear")
            j.cerrar()
            if j.vivo():
                raise api.Fallo("no termino el proceso al guardar")
            if ficha() != guardada:
                raise api.Fallo(f"{raza}: el cierre cambio la ficha")
            # Sin hooks de raza/cuerpo al recargar: la unica autoridad es el guardado.
            e = {k: v for k, v in j.cac_env.items() if k not in ("DBZ3_TEST_HEROE_RAZA", "DBZ3_TEST_HEROE_CUERPO")}
            j.lanzar(e)
            j.arrancar(180, titulo=("Return", "W", "Space"))
            esperar_creador(raza)
            j.log(rf"ficha 0 importada \(raza {RAZAS[raza][0]}, cuerpo {RAZAS[raza][1]},", 30)
            recargada = ficha()
            if recargada != guardada:
                raise api.Fallo(f"{raza}: ficha distinta tras reiniciar")
            j.captura(raza + "_recarga")
            if raza == "HUM":
                # Serie sin tocar piezas/colores: permite distinguir el ciclado
                # deliberado del guion de un parpadeo con receta fija.
                api.dormir(3)
                for n in range(6):
                    j.captura(f"HUM_colores_fijos_{n:02}")
                    api.dormir(.25)
            guardar_log(sub, "recarga")
            # Abrir cada panel permite leer los colores de la memoria del creador recargado.
            for _ in range(7):
                pulso("S")
            for k in range(6):
                abrir_color(k)
                pulso("Space")
                j.log(rf"Create: Color de {nombres_color[k]} = \(10, 10, -10\)", 15)
                pulso("S")
            j.tecla("3", 800)
            j.log(r"Create: confirmar", 30)
            if esperar_ficha(raza) != guardada:
                raise api.Fallo(f"{raza}: confirmar tras recargar cambia piezas/colores")
            guardar_log(sub, "recarga")
            fila = {"raza": raza, "guardar": "PASS", "recargar": "PASS", "colores": "PASS",
                    "id": guardada["id"], "xv2": guardada["xv2"], "partes": guardada["partes"]}
            tabla.append(fila)
            (j.run / "tabla.json").write_text(json.dumps(tabla, indent=2), encoding="utf-8")
            j.info(f"PASS {raza}: guardado/recarga/7 piezas/6 colores; id={guardada['id']}")

    return "CaC XV2: guardado y reinicio aislados (" + ", ".join(razas) + ")", ejecutar, env, preparar

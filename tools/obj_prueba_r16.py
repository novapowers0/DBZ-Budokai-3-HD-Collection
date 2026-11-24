"""Prueba OBJ por guiones TOML de pruebas.py; no modifica el arnés compartido.

Prepara un roster aislado, ejecuta el comando oficial y envía las peticiones de
cámara de prueba al recibir 'colocar'. Todos los mods se restauran por el arnés.
"""
import argparse
from datetime import datetime
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("mod", type=Path)
    ap.add_argument("--modo", choices=("lamina", "select", "acciones", "recibir", "carga", "carga_pad", "poses", "salida"), default="lamina")
    ap.add_argument("--copia", default="smoke_f", help="copia aislada del arnés (si está ocupada, elegir otra libre)")
    ap.add_argument("--preparado", type=Path, help="mods ya construidos por esta prueba; verifica que el mod no cambió")
    ap.add_argument("--alto", type=float, default=9)
    ap.add_argument("--distancia", type=float, default=30)
    ap.add_argument("--rival", type=int, default=7, help="ID B3 nativo para comparación (0 = Goku)")
    ap.add_argument("--pad", default="600,900,4096,1", help="carga_pad: inicio,duracion,botones XInput,doble toque")
    a = ap.parse_args()
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    work = Path("E:/DBZ3HD_trabajo/obj_import") / ("prueba_" + a.modo + "_" + stamp)
    mods = work / "mods"
    if a.preparado:
        # Solo se reutilizan fuentes idénticas. Cada ejecución sigue aislándose
        # mediante pruebas.py; no se reconstruyen ocho idiomas para cada movimiento.
        mods = a.preparado.resolve()
        for source in a.mod.rglob("*"):
            if source.is_file():
                cached = mods / a.mod.name / source.relative_to(a.mod)
                if source.name == "personaje.toml":
                    expected = source.read_text(encoding="utf-8").replace("[personaje]", "[personaje]\nid = 54\nformas = 1", 1)
                    same = cached.is_file() and cached.read_text(encoding="utf-8") == expected
                else:
                    same = cached.is_file() and cached.read_bytes() == source.read_bytes()
                if not same:
                    ap.error("el mod preparado está desactualizado: " + str(source))
        work.mkdir(parents=True)
    else:
        mods.mkdir(parents=True)
        shutil.copytree(a.mod, mods / a.mod.name)
        config = mods / a.mod.name / "personaje.toml"
        original = config.read_text(encoding="utf-8")
        (work / "personaje.original.toml").write_text(original, encoding="utf-8")
        config.write_text(original.replace("[personaje]", "[personaje]\nid = 54\nformas = 1", 1), encoding="utf-8")
        subprocess.run([sys.executable, str(ROOT / "mod center hd/roster_build.py"),
                        "construir", "--mods", str(mods)], check=True, cwd=ROOT)
    generated = mods / "_roster" / "roster.toml"
    import tomllib
    characters = tomllib.loads(generated.read_text(encoding="utf-8"))["personaje"]
    char = next(c for c in characters if c["nombre"].casefold() == "novapowers")
    if char["id"] != 54:
        raise RuntimeError("el roster no respetó la plaza aislada 54")
    putting = [str(f).replace("\\", "/") + " -> mods/" + str(f.relative_to(mods)).replace("\\", "/")
               for f in mods.rglob("*") if f.is_file() and "respaldo" not in f.parts]
    env = ["DBZ3_TEST_TURBO=1", "DBZ3_TEST_OCULTO=0",
           'DBZ3_TEST_CFG=dbz3_cine_intro=false;dbz3_combate_prueba_muneco="%s";dbz3_camara_sigue=false' %
            ("ataca" if a.modo == "recibir" else "quieto"), "DBZ3_COMBATE_SONDA=1",
           "DBZ3_NAMES_DIAG=1", "DBZ3_TEST_PREPARAR_CAMPOS=0"]
    # La copia compartida puede tener otros controles guardados: fijarlos SOLO en
    # esta prueba. El juego transforma después los bits del mando en botones B3.
    env[2] += ';dbz3_keybind_a="Space";dbz3_keybind_b="Quote,Backspace";dbz3_keybind_x="L";dbz3_keybind_y="P"'
    env[2] += ';dbz3_keybind_start="Return";dbz3_keybind_lstick_up="W";dbz3_keybind_lstick_down="S";dbz3_keybind_lstick_left="A";dbz3_keybind_lstick_right="D"'
    if a.modo == "carga_pad":
        env += ["DBZ3_TEST_OBJ_PAD=" + a.pad]
    if a.modo == "salida":
        env += ["DBZ3_TEST_RINGOUT=1"]
    if a.modo == "select":
        steps = ["esperar 2500", "tecla S 1200", "tecla Space 1800", "esperar_pantalla 14 20",
                 "esperar 2000", "tecla Space 6500", "captura select_inicial", "tecla S 1200", "captura select_derecha",
                 "tecla Space 2000", "captura select_elegido"]
    else:
        env += ["DBZ3_TEST_BATTLE=54,%d,2,0,50000,50000" % a.rival]
        if a.modo not in ("recibir", "carga"):
            env += ["DBZ3_TEST_FREECAM=0,0,0,0,0,45", "DBZ3_TEST_FREECAM_LOG=2", "DBZ3_TEST_FREECAM_HUD=1"]
        positions = ("-55,0,0", "55,0,0") if a.modo == "lamina" else ("-5,0,0", "5,0,0")
        steps = ["esperar_pelea 120"]
        if a.modo not in ("recibir", "carga"):
            steps += ['log "camara libre: activa" 30']
        steps += ["colocar " + " ".join(positions)]
        n = 4 if a.modo == "lamina" else 0 if a.modo in ("recibir", "carga") else 1
        for i in range(n):
            steps += ['log "camara libre: prueba mirar %d ojo" 30' % (101 + i),
                      "esperar 700", "captura nova_%d" % i]
        if a.modo == "salida":
            steps += ['log "OBJ cel: mascara propia del modelo; brillo HD nativo intacto" 30',
                      "esperar_pantalla 5 90", "captura brillo_nativo_intacto"]
        if a.modo == "carga_pad":
            steps += ['log "prueba OBJ: mando nativo inicio" 30', "esperar 350"]
            if a.pad.endswith(",2"):
                for i in range(17):
                    steps += ['log "prueba OBJ: combinacion %d botones" 30' % i,
                              "esperar 700", "captura combinacion_%02d" % i]
            else:
                for i in range(8):
                    steps += ["captura carga_pad_%d" % i, "esperar 500"]
            steps += ['log "prueba OBJ: mando nativo fin" 30']
        if a.modo == "poses":
            steps += ['tecla "Space 4000" 900', "captura guardia_cruzada", "esperar 4500",
                      # Un único fichero: 50 ms, separación nativa de 150 ms y
                      # segundo toque sostenido. Dos peticiones independientes
                      # añadían una espera variable al doble toque de guardia.
                      'tecla "Space 50\nSpace 6500" 950']
            for i in range(6):
                steps += ["captura doble_guardia_%d" % i, "esperar 450"]
            steps += ["esperar 5000"]
        if a.modo == "acciones":
            # La imagen de reposo tras un golpe no verifica la deformación durante
            # el golpe. Muestrear toda la acción y conservar la sonda y el vídeo.
            steps += ["clip acciones 60"]
            for name, key in (("punetazo", "L"), ("patada", "P")):
                steps += ["colocar -5,0,0 5,0,0", 'tecla "%s 1800" 100' % key]
                for i in range(8):
                    steps += ["captura %s_%d" % (name, i), "esperar 90"]
                steps += ["esperar 2200"]
            steps += ["colocar -6,0,0 55,0,0", 'tecla "Space 50" 50',
                      'tecla "Space 5000" 600']
            for i in range(6):
                steps += ["captura carga_doble_g_%d" % i, "esperar 350"]
            steps += ["esperar 3000", 'tecla "P+Backspace 5000" 600']
            for i in range(6):
                steps += ["captura carga_yb_%d" % i, "esperar 350"]
            steps += ["esperar 2500"]
            for name, key in (("carga_ge", "Space+Backspace"), ("carga_kg", "Space+P"),
                              ("carga_atras_g", "A+Space")):
                steps += ['tecla "%s 4500" 600' % key]
                for i in range(3):
                    steps += ["captura %s_%d" % (name, i), "esperar 350"]
                steps += ["esperar 3500"]
        if a.modo == "recibir":
            steps += ["clip recibir 25"]
            for i in range(50):
                steps += ["captura impacto_%02d" % i, "esperar 200"]
        if a.modo == "carga":
            steps += ["colocar -5,0,0 55,0,0", "clip carga 10", "esperar 1000", "posiciones", "esperar 300"]
            for i in range(6):
                steps += ["captura carga_real_%d" % i, "esperar 600"]
            steps += ["esperar 2000"]
    script = work / ("obj_" + a.modo + "_" + stamp + ".toml")
    script.write_text("\n".join(["descripcion = " + json.dumps("NovaPowers OBJ: " + a.modo),
                                  "turbo = false", 'apartar = ["mods", "dbz3_freecam.req"]',
                                  "poner = " + json.dumps(putting), "env = " + json.dumps(env),
                                  "pasos = " + json.dumps(steps)]), encoding="utf-8")
    run = [None]
    camera_errors = []
    def carga_rapida():
        # El inyector de ficheros impone 150 ms de separación: no sirve para
        # comprobar un doble toque corto. Mensajes SOLO a la ventana de esta
        # copia; restaura el foco al terminar y no envía teclas a otras ventanas.
        try:
            import pruebas as P
            pids = P.pids_de_copia(Path("D:/DBZ3HD") / a.copia)
            if len(pids) != 1:
                raise RuntimeError("la carga necesita un único juego de nuestra copia")
            hwnd = P._hwnd_juego(pids[0])
            if not hwnd:
                raise RuntimeError("sin ventana de nuestra copia para el doble toque")
            P.U32.GetForegroundWindow.restype = P.wt.HWND
            previous = P.U32.GetForegroundWindow()
            def key(vk, pressed):
                down = 1 | (P.U32.MapVirtualKeyW(vk, 0) << 16)
                P.U32.PostMessageW(hwnd, 0x0100 if pressed else 0x0101, vk,
                                   down if pressed else down | (3 << 30))
            try:
                P.U32.SetForegroundWindow(hwnd)
                time.sleep(.3)
                for hold, gap in ((50, 35), (2500, 100)):
                    key(0x20, True); time.sleep(hold / 1000)
                    key(0x20, False); time.sleep(gap / 1000)
                # Segundo gesto, G mantenida y dos toques hacia atrás. La cámara
                # nativa deja las direcciones al mando, a diferencia de freecam.
                key(0x20, True)
                key(0x41, True); time.sleep(.10)
                key(0x41, False); time.sleep(.10)
                key(0x41, True); time.sleep(2.5)
            finally:
                key(0x20, False); key(0x41, False)
                if previous and P.U32.GetForegroundWindow() == hwnd:
                    P.U32.SetForegroundWindow(previous)
        except Exception as ex:
            camera_errors.append("carga: " + str(ex))
    def cameras():
        try:
            until = time.monotonic() + 20
            while run[0] is None and time.monotonic() < until:
                hits = list(Path("D:/DBZ3HD/_pruebas").glob("*_" + a.copia + "_" + script.stem))
                if hits:
                    run[0] = max(hits, key=lambda p: p.stat().st_mtime_ns)
                else:
                    time.sleep(.1)
            if run[0] is None:
                raise RuntimeError("no encuentro la carpeta de evidencias")
            n = 4 if a.modo == "lamina" else 1
            details = []
            px = -55 if a.modo == "lamina" else -5
            for i in range(n):
                angle = math.radians(45 + 90 * i)
                target = (px, a.alto, 0)
                eye = (px + a.distancia * math.sin(angle), a.alto, a.distancia * math.cos(angle))
                command = "mirar %d %s 45" % (101 + i, " ".join(str(v) for v in (*eye, *target)))
                (Path("D:/DBZ3HD") / a.copia / "dbz3_freecam.req").write_text(command, encoding="ascii")
                details.append(dict(vista=i, ojo=eye, objetivo=target))
                until = time.monotonic() + 60
                while not (run[0] / ("nova_%d.png" % i)).exists():
                    if time.monotonic() > until:
                        raise RuntimeError("no llegó la captura de cámara %d" % i)
                    time.sleep(.1)
            (run[0] / "obj_camaras.json").write_text(json.dumps(details, indent=2), encoding="utf-8")
        except Exception as ex:
            camera_errors.append(str(ex))
    env_child = dict(os.environ, DBZ3_AGENTE="r18_solmNOVA_PERFECTO", DBZ3_PRUEBAS_REINTENTO="0", PYTHONUNBUFFERED="1")
    command = [sys.executable, str(ROOT / "tools/pruebas.py"), "correr", a.copia, str(script)]
    print("COMANDO: " + subprocess.list2cmdline(command), flush=True)
    process = subprocess.Popen(command, cwd=ROOT, env=env_child, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    thread = None
    charge_thread = None
    with (work / "prueba.log").open("w", encoding="utf-8") as log:
        for line in process.stdout:
            print(line, end="", flush=True); log.write(line); log.flush()
            if line.startswith("PASS  "):
                evidence = work / "evidencias"
                evidence.mkdir(exist_ok=True)
                for name in ("combate_sonda.csv", "combate_golpes.csv"):
                    src = Path("D:/DBZ3HD") / a.copia / "logs" / name
                    if src.is_file():
                        shutil.copy2(src, evidence / name)
            if "colocar:" in line and thread is None and a.modo not in ("recibir", "carga"):
                thread = threading.Thread(target=cameras, daemon=True); thread.start()
            if a.modo == "carga" and "posiciones:" in line and charge_thread is None:
                charge_thread = threading.Thread(target=carga_rapida, daemon=True); charge_thread.start()
    code = process.wait()
    if thread:
        thread.join(3)
    if charge_thread:
        charge_thread.join(6)
    if camera_errors:
        print("ERROR cámaras: " + "; ".join(camera_errors))
        code = code or 1
    if run[0] is None:
        hits = list(Path("D:/DBZ3HD/_pruebas").glob("*_" + a.copia + "_" + script.stem))
        if hits:
            run[0] = max(hits, key=lambda p: p.stat().st_mtime_ns)
    if run[0]:
        # Copia pequeña y duradera en E; no depende de la limpieza del banco D.
        evidence = work / "evidencias"
        evidence.mkdir(exist_ok=True)
        for f in run[0].iterdir():
            if f.is_file() and f.suffix.lower() in (".png", ".json", ".txt", ".csv", ".mp4", ".log"):
                shutil.copy2(f, evidence / f.name)
                if f.suffix.lower() == ".png":
                    with Image.open(f) as im:
                        im.resize((max(1, im.width // 2), max(1, im.height // 2)), Image.Resampling.LANCZOS).save(evidence / (f.stem + "_05.png"))
    if run[0] and a.modo == "lamina" and not code:
        photos = [Image.open(run[0] / ("nova_%d.png" % i)).convert("RGB") for i in range(4)]
        photos = [im.resize((im.width // 2, im.height // 2), Image.Resampling.LANCZOS) for im in photos]
        w, h = photos[0].size
        sheet = Image.new("RGB", (w * 2, (h + 24) * 2), "#181818")
        draw = ImageDraw.Draw(sheet)
        for i, im in enumerate(photos):
            x, y = i % 2 * w, i // 2 * (h + 24)
            draw.text((x + 8, y + 4), ("Frente", "Costado", "Espalda", "Otro costado")[i], fill="white")
            sheet.paste(im, (x, y + 24))
        sheet.save(run[0] / "nova_lamina_05.png")
        sheet.save(work / "evidencias/nova_lamina_05.png")
        print("LÁMINA: " + str(run[0] / "nova_lamina_05.png"))
    if a.modo in ("carga", "carga_pad"):
        import csv
        sonda = work / "evidencias/combate_sonda.csv"
        rows = list(csv.DictReader(sonda.open(encoding="utf-8-sig"))) if sonda.exists() else []
        rows = [row for row in rows if row["1_id"] == "54"]
        values = [float(row["1_ki"]) for row in rows]
        gained = max(values) - min(values) if values else 0
        charging = [row for row in rows if int(row["1_accion"]) in (20, 21)]
        confirmed = gained >= 400 and len(charging) >= 30
        (work / "carga_medida.json").write_text(json.dumps(dict(confirmada=confirmed, subida_ki=gained,
            cuadros_carga=len(charging), acciones=sorted({int(row["1_accion"]) for row in rows}))), encoding="utf-8")
        print(("PASS" if confirmed else "FAIL") + " carga medida: subida de ki " + str(gained))
        if not confirmed:
            code = code or 1
    print("TRABAJO: " + str(work))
    return code


if __name__ == "__main__":
    raise SystemExit(main())

"""Las sagas del Modo Heroe (res/heroe/sagas.toml) como historia de Dragon Universe (campana.py).

  python heroe_sagas_a_du.py                 escribe campanas/historia_heroe.toml
  python heroe_sagas_a_du.py --montar        ...y lo monta (python campana.py montar)
  python heroe_sagas_a_du.py --comprobar     autocomprobacion (sin juego)

El Heroe es exclusivo de su modo: entra en DU con hub::RequestDu (salta titulo, menu y rueda de personajes) en la
casilla CASILLA (36 por defecto: libre en DU) y el juego lo crea como el ID propio (63) en el mapa y en el combate. Una casilla tiene 4 bloques de codigos de mapa (5x36), asi que
una historia cabe en 3 sagas (empieza en el bloque 5100, Namek); el resto de sagas, pendiente (C3/PENDIENTE.md).
Los combates reutilizan los #BPC sin usar del juego (data_cmn 627-653, 27: uno por combate de las 8 sagas).
Los retratos de los rivales son los de data_cmn 1416-1499 (los de B3), el del heroe el de su estilo (1416 por defecto).
"""
import argparse
import os
import sys
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(HERE)
# ID de luchador -> retrato de dialogo (data_cmn). Medido mirando 1416-1499 (hoja de contacto, 22/10).
RETRATO = {0: 1416, 7: 1436, 11: 1449, 12: 1450, 18: 1459, 20: 1461, 21: 1462, 27: 1466, 29: 1469, 30: 1471, 33: 1477,
           38: 1485, 41: 1490, 42: 1491, 43: 1492, 64: 1494, 70: 1497, 72: 1498}
# Marcas del mapa de Namek (zona 2): las de la historia de Goku del juego (medidas en 5100.x)
MARCAS = [(4918, 67, -4916), (5010, 20, -5399), (5517, 39, -5053), (2060, 45, -982), (2090, 45, 40), (3384, 45, 6021),
          (4682, 45, 2366), (-4510, 45, 354), (-379, 45, -956), (2000, 45, -801), (-4735, 45, 4170)]
# H3: marcas medidas en el mapa de la Tierra (historia de Freezer / 104.0): zona, x, y, z
MARCAS_TIERRA = [(25, 3213, 48, -6083), (4, 1821, 20, 2954), (25, 3213, 48, -6083)]
SAGA0 = 1          # bloque de codigos 5100 (Namek): 5000 + 100*SAGA0 + casilla
MAX_SAGAS = 8      # 3 con codigos del juego (5100-5300 mapa, 5500-5700 combate, 5900-6100 dialogo) + 5 virtuales (campana.py)
BPC0 = 627         # #BPC sin usar del juego (data_cmn 627-653)


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def lst(xs):
    return "[" + ", ".join(q(x) for x in xs) + "]"


def leer(ruta):
    with open(ruta, "rb") as f:
        return tomllib.load(f)


CASILLAS = (35, 36, 37)   # C4: una casilla de DU por ficha del Heroe (ficha 1 = 35, 2 = 36, 3 = 37)


def generar(sagas, casilla=36, heroe_id=63, retrato_heroe=1416, nombre=None, n_sagas=MAX_SAGAS):
    nombre = nombre or "historia_heroe_%d" % casilla
    """sagas: dict de res/heroe/sagas.toml. Devuelve (texto del .toml de campana.py, [(saga, combate, rival)])."""
    sg = sagas["saga"][:n_sagas]
    out = ["# GENERADO por heroe_sagas_a_du.py desde res/heroe/sagas.toml (no editar a mano: se rehace).",
           "[campana]", 'nombre = "Modo Heroe en Dragon Universe"', "mod = %s" % q(nombre),
           'descripcion = "Las sagas del Modo Heroe como historia de Dragon Universe: entra por la celda del Heroe."', "",
           "[rueda]", "casillas_virtuales = [%d]" % casilla, "",
           "[casilla_luchador]", "%d = %d" % (casilla, heroe_id), "",
           "[historia]", "codigo = %d" % (5000 + 100 * SAGA0 + casilla), 'intro_de = "102.0"', "animacion_mapa = 0",
           "altura_mapa = 600", "fondo = 1411", "fondo_dialogo = 1536",
           *([] if os.environ.get("HEROE_GUARDAR") else ["sin_guardar = true"]),   # C8: HEROE_GUARDAR=1 = cierre nativo (guardar+titulo+reanudar). # el cierre de saga no pasa por el guardado nativo (vuelve al titulo): sigue con la intro (el progreso va en la ficha)
           'final = { contenedor = 1728, codigo = 1, entrada = 39, fondo = "heroe" }',
           "combates = { codigo = %d, contenedor = 2132, plantilla = 709 }" % (5400 + 100 * SAGA0 + casilla),
           "dialogos = { codigo = %d, contenedor = 1659 }" % (5800 + 100 * SAGA0 + casilla), "",
           '[[panel_limpio]]', "cmn = 1411", "filas = 17", ""]
    plan, k = [], 0
    for si, s in enumerate(sg):
        for ci, c in enumerate(s["combate"]):
            x, y, z = MARCAS[k % len(MARCAS)]
            tierra = int(s.get("planeta", 1)) == 0   # H3: sagas en la Tierra (planeta = 0 en sagas.toml)
            zona = 2
            if tierra:
                zona, x, y, z = MARCAS_TIERRA[ci % len(MARCAS_TIERRA)]
            if os.environ.get("HEROE_MARCA_FIJA"):   # C7: prueba (todas las marcas en el mismo punto)
                x, y, z = [int(v) for v in os.environ["HEROE_MARCA_FIJA"].split(",")]
            rival = int(c["rival"])
            es, en = c.get("es", "..."), c.get("en", "...")
            boss_es, boss_en = c.get("jefe_es"), c.get("jefe_en")
            out += ["[[capitulo]]", "combate = %d" % (BPC0 + k), "rival = %d" % rival,
                    "escenario = %d" % int(c.get("escenario", 7)),
                    *(["plantilla = 717"] if tierra and not (ci == 0 and si > 0) else []),   # H3: #BPC de la Tierra (escenario 8); con 709 (Namek) toda pelea salia en Namek
                    "retratos = [%d, %d]" % (RETRATO.get(rival, 1416), retrato_heroe),
                    "marca = [1, %d, %d, %d, %d, %d]" % (zona, x, y, z, 1500 if tierra else 1000),
                    "dialogo_es = %s" % lst([boss_es or es, "Voy a por ti."]),
                    "dialogo_en = %s" % lst([boss_en or en, "I'm coming for you."]),
                    "victoria_es = %s" % lst(["Impresionante...", "Sigo mi camino."]),
                    "victoria_en = %s" % lst(["Impressive...", "I go on my way."]),
                    "mapa_es = %s" % lst(["Sigamos."]), "mapa_en = %s" % lst(["Onwards."])]
            if ci == 0:
                if si > 0:   # abre saga: cierre de la anterior, "guardar?" y la introduccion de esta
                    prev = sg[si - 1]
                    out += ["saga = true", "planeta = %d" % (0 if tierra else 1), "plantilla = %d" % (717 if tierra else 709), "fondo_dialogo = %d" % (1549 if tierra else 1536)]
                    if tierra:   # receta de historia_freezer.toml (sin objetos_mapa_de, R1 en la Tierra cerraba el juego)
                        out += ["objetos_mapa_de = \"104.0\""]
                    out += ["cierre_es = %s" % lst(prev.get("cierre_es", ["..."])),
                            "cierre_en = %s" % lst(prev.get("cierre_en", ["..."]))]
                out += ["salida = [5700, 100, 1700]" if tierra else "salida = [%d, %d, 600]" % (x, z + int(os.environ.get("HEROE_SALIDA_DIST", "500"))),
                        "es = %s" % lst(s.get("intro_es", ["..."])), "en = %s" % lst(s.get("intro_en", ["..."])),
                        "llegada_es = %s" % lst(["Empieza la aventura."]), "llegada_en = %s" % lst(["The adventure begins."])]
            out.append("")
            plan.append((si, ci, rival))
            k += 1
    last = sg[-1]
    out += ["[[capitulo]]", "es = %s" % lst(last.get("cierre_es", ["Fin."])), "en = %s" % lst(last.get("cierre_en", ["The end."])), ""]
    return "\n".join(out), plan


def comprobar():
    d = leer(os.path.join(RAIZ, "res", "heroe", "sagas.toml"))
    txt, plan = generar(d)
    cfg = tomllib.loads(txt)   # el .toml generado es TOML valido
    caps = cfg["capitulo"]
    assert len(plan) <= 27 and len(caps) == len(plan) + 1, (len(plan), len(caps))
    assert [c["combate"] for c in caps[:-1]] == list(range(BPC0, BPC0 + len(plan)))
    assert sum(1 for c in caps if c.get("saga")) == min(MAX_SAGAS, len(d["saga"])) - 1
    assert cfg["historia"]["codigo"] % 100 == cfg["rueda"]["casillas_virtuales"][0]
    assert "celdas_rueda" not in cfg, "el Heroe no sale en ninguna rueda"
    assert all(c["rival"] in RETRATO for c in caps[:-1]), [c["rival"] for c in caps[:-1] if c["rival"] not in RETRATO]
    print("heroe_sagas_a_du: autocomprobacion OK (%d combates, %d sagas)" % (len(plan), min(MAX_SAGAS, len(d["saga"]))))


PLANTILLAS = ("Template warrior.png", "Template warrior female.png")   # pack opcional Dragon HUB del autor (no se reparte)
PLANTILLAS_HUB = ("plantilla_guerrero.png", "plantilla_guerrera.png")   # como las deja el pack en ingame_ui/hub/
PACK_AUTOR = os.path.join("E:\\", "Videos", "Efectos Canal", "MATERIAL DE EDICI\u00d3N",
                          "DRAGON BALL Z (Auras, Sprites, Paneles...)", "Dragon HUB (pack opcional)")
MUJERES = (101, 103, 105)   # [H] cuerpos UT con la cara B (la femenina del catalogo); sin dato medido todavia


def buscar_plantilla(mods, mujer):
    """Silueta del pack Dragon HUB (nunca se reparte): ingame_ui/hub/ junto al juego, assets_propios del proyecto o la
    carpeta del autor. None si el pack no esta (el retrato sale vacio: sin cara)."""
    juego = os.path.dirname(os.path.abspath(mods))
    k = 1 if mujer else 0
    for d, n in ((os.path.join(juego, "ingame_ui", "hub"), PLANTILLAS_HUB[k]),
                 (os.path.join(juego, "res", "ingame_ui", "hub"), PLANTILLAS_HUB[k]),
                 (os.path.join(RAIZ, "assets_propios", "png", "add", "dragon_hub"), PLANTILLAS[k]),
                 (PACK_AUTOR, PLANTILLAS[k])):
        p = os.path.join(d, n)
        if os.path.isfile(p):
            return p
    return None


def retrato_de_plantilla(ruta, salida):
    """Silueta -> 416x408 como los retratos nativos (data_cmn 1416: busto abajo, algo a la derecha del centro)."""
    from PIL import Image  # noqa: PLC0415
    im = Image.open(ruta).convert("RGBA")
    k = min(408 / im.height, 416 / im.width)
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    lienzo = Image.new("RGBA", (416, 408), (0, 0, 0, 0))
    lienzo.paste(im, (min(416 - im.width, max(0, round(0.58 * 416 - im.width / 2))), 408 - im.height), im)
    lienzo.save(salida)


def sello(sagas_toml, extra=""):
    """Huella de lo que decide el contenido de las campanas (para no remontar en cada confirmacion)."""
    import hashlib  # noqa: PLC0415
    h = hashlib.sha256(extra.encode())
    for p in (sagas_toml, os.path.abspath(__file__), os.path.join(HERE, "campana.py"), os.path.join(HERE, "du_codigos.json")):
        if os.path.isfile(p):
            with open(p, "rb") as f:
                h.update(f.read())
    return h.hexdigest()[:16]


def montar_si_hace_falta(mods, us=None, sagas_toml=None, casilla=None, cuerpo=0, nombre=None):
    """C4: lo llama vista_heroe.py --combate (el mismo momento en que se monta heroe_propio). Monta las historias de las
    casillas 35-37 que falten o esten desfasadas; la de `casilla` (la ficha que se acaba de confirmar) lleva el retrato
    de plantilla segun el `cuerpo`. Devuelve 0 si quedan montadas."""
    import shutil  # noqa: PLC0415
    import subprocess  # noqa: PLC0415
    import time  # noqa: PLC0415
    sagas_toml = sagas_toml or os.path.join(RAIZ, "res", "heroe", "sagas.toml")
    if not os.path.isfile(sagas_toml):   # release: res\heroe junto al juego
        sagas_toml = os.path.join(os.path.dirname(os.path.abspath(mods)), "res", "heroe", "sagas.toml")
    viejo = os.path.join(mods, "historia_heroe")   # C3: la historia de una sola casilla (36) choca con la nueva: al respaldo
    if os.path.isdir(viejo):
        resp = os.path.join(os.path.dirname(os.path.abspath(mods)), "mods_respaldo")
        os.makedirs(resp, exist_ok=True)
        shutil.move(viejo, os.path.join(resp, "historia_heroe_c3_%d" % int(time.time())))
    mujer = int(cuerpo) in MUJERES
    rc = 0
    for cas in CASILLAS:
        d = os.path.join(mods, "historia_heroe_%d" % cas)
        marca = os.path.join(d, ".sello")
        propia = casilla is None or cas == casilla
        pl = buscar_plantilla(mods, mujer if propia else False)
        base = os.path.join(mods, "heroe_propio", "modelos", "base.bin")
        anime = None   # retrato propio del modelo del heroe (solo la casilla de la ficha confirmada: base.bin es el suyo)
        # el retrato anime del modelo es OPCION BETA (22/10: aun no iguala a los nativos): se activa creando
        # <juego>/ingame_ui/heroe_vista/retrato_anime.on; por defecto, la silueta de plantilla
        opcion = os.path.isfile(os.path.join(os.path.dirname(os.path.abspath(mods)), "ingame_ui", "heroe_vista", "retrato_anime.on"))
        if opcion and propia and casilla is not None and os.path.isfile(base):
            try:
                sys.path.insert(0, os.path.join(HERE, "cac"))
                import retrato_anime as ra  # noqa: PLC0415
                hh = ra.huella(base)
                anime = os.path.join(os.path.dirname(os.path.abspath(mods)), "ingame_ui", "heroe_vista", "retrato_%d_%s.png" % (cas, hh))
                if not os.path.isfile(anime):   # cache por huella del modelo (cambia al editar la apariencia)
                    from PIL import Image  # noqa: PLC0415
                    os.makedirs(os.path.dirname(anime), exist_ok=True)
                    Image.fromarray(ra.retrato(base)).save(anime)
            except Exception as e:   # noqa: BLE001 (sin retrato propio: la plantilla)
                print("retrato propio del heroe no hecho (%s): se usa la plantilla" % e)
                anime = None
        if anime:
            pl = anime
        extra = "%s|%s" % (os.path.basename(pl or "-"), os.path.getmtime(pl) if pl else 0)
        try:
            previo = open(marca, encoding="utf-8").read().strip()
        except OSError:
            previo = ""
        faltan = not os.path.isfile(os.path.join(d, "campana.toml"))
        base_s = sello(sagas_toml)
        actual = base_s + "|" + sello(sagas_toml, extra)
        if not faltan and (previo == actual if propia else previo.split("|")[0] == base_s):
            continue
        cmd = [sys.executable, "-X", "utf8", os.path.abspath(__file__), "--montar", "--casilla", str(cas), "--mods", mods,
               "--sagas", sagas_toml] + (["--us", us] if us else []) + (["--plantilla", pl] if pl else [])
        r = subprocess.run(cmd).returncode
        if r == 0:
            with open(marca, "w", encoding="utf-8") as f:
                f.write(actual + "\n")
        rc = r or rc
    if casilla and nombre and rc == 0:   # rotulo de nombre de la pantalla de subida de nivel (cac/nombre_levelup.py)
        cmd = [sys.executable, "-X", "utf8", os.path.join(HERE, "cac", "nombre_levelup.py"), "--mods", mods] +             (["--us", us] if us else []) + ["%d=%s" % (casilla, nombre)]
        rc = subprocess.run(cmd).returncode or rc
    return rc


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--sagas", default=os.path.join(RAIZ, "res", "heroe", "sagas.toml"))
    p.add_argument("--salida", help="solo con --casilla (por defecto campanas/historia_heroe_<casilla>.toml)")
    p.add_argument("--casilla", type=int, default=0, help="una sola casilla (por defecto las 3: 35, 36 y 37)")
    p.add_argument("--id", type=int, default=63, help="ID de la celda del heroe (roster: heroe_propio)")
    p.add_argument("--retrato", type=int, default=1416, help="retrato de dialogo del heroe (data_cmn)")
    p.add_argument("--plantilla", help="silueta (png) del retrato de dialogo del heroe; sin ella el retrato sale vacio")
    p.add_argument("--montar", action="store_true")
    p.add_argument("--mods", help="carpeta mods/ donde montar")
    p.add_argument("--us", help="carpeta us/ (se pasa a campana.py)")
    p.add_argument("--comprobar", action="store_true")
    a = p.parse_args()
    if a.comprobar:
        return comprobar()
    sagas = leer(a.sagas)
    rc = 0
    for cas in ([a.casilla] if a.casilla else CASILLAS):
        txt, plan = generar(sagas, cas, a.id, a.retrato)
        salida = a.salida if a.casilla and a.salida else os.path.join(HERE, "campanas", "historia_heroe_%d.toml" % cas)
        os.makedirs(os.path.dirname(salida), exist_ok=True)
        png = os.path.splitext(salida)[0] + "_retrato.png"
        con = bool(a.plantilla and os.path.isfile(a.plantilla))
        if con:
            retrato_de_plantilla(a.plantilla, png)
        else:
            png = os.path.join(os.path.dirname(png), "sin_retrato.png")   # no existe: campana.py deja el retrato vacio
        txt = txt.replace("[historia]\n", '[historia]\nretrato_heroe = "%s"\n' % os.path.basename(png), 1)
        with open(salida, "w", encoding="utf-8", newline="\n") as f:
            f.write(txt)
        print("escrito %s (%d combates, retrato %s)" % (salida, len(plan), "plantilla" if con else "vacio"))
        if a.montar:
            # un proceso por campana: campana.py guarda estado global (SHARED, STORY_CODES, CMN) entre montajes
            import subprocess  # noqa: PLC0415
            cmd = [sys.executable, "-X", "utf8", os.path.join(HERE, "campana.py")] + (["--mods", a.mods] if a.mods else []) + \
                  (["--us", a.us] if a.us else []) + ["montar", salida]
            rc = subprocess.run(cmd).returncode or rc
    return rc


if __name__ == "__main__":
    sys.exit(main())

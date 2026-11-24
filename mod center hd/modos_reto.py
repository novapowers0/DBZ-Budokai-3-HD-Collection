#!/usr/bin/env python3
"""modos_reto.py - Genera campanas de Dragon Universe para los modos reto (base de los modos futuros).

  python modos_reto.py torre --pisos 10 --salida campanas/retos/reto_torre.toml
  python modos_reto.py supervivencia --oleadas 8 --salida campanas/retos/reto_supervivencia.toml
  python modos_reto.py kumite --jefes 5 --salida campanas/retos/reto_kumite.toml
  python modos_reto.py --selftest

Cada TOML sale en el formato de historia_freezer.toml y se monta igual:
  python campana.py montar campanas/retos/reto_torre.toml

Retos NATIVOS (sin campana: el juego encadena combates por su cuenta, src/modos.cpp). El TOML se copia a
<juego>/user_data/dbz3/modos/<tipo>.toml (o se escribe ahi con --juego):
  python modos_reto.py nativo torre --rivales 42,18,19,21,20,27,33,36 --vida-rival 2000:300
  python modos_reto.py nativo supervivencia --vida-inicial 2000 --recupera 500 --juego "D:\\DBZ3HD\\modos_test"
  python modos_reto.py nativo kumite --rivales 27,33,40 --vida-rival 6000 --escenarios 1,2
Sin --rivales sale el reto por defecto del juego. IDs 44-63 = personajes extra de un mod (roster.toml).

Solo reutiliza lo que ya usa historia_freezer.toml (se lee de ese archivo): #BPC 654-663, rivales,
trajes, formas, retratos, marcas, escena de introduccion, imagen y panel. Limite: 10 combates por modo.
Ver studies/11_vision_futura/BASE_MODOS_RETO.md.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

try:
    import tomllib
except ImportError:  # Python < 3.11
    import tomli as tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FREEZER = os.path.join(HERE, "campanas", "historia_freezer.toml")
SALIDA_DEFECTO = os.path.join(HERE, "campanas", "retos")

COMBATES = list(range(654, 664))  # #BPC de historia_freezer.toml de la saga de Namek (con marca en el mapa)
MAX_COMBATES = len(COMBATES)
# Entradas de texto propias de cada modo, fijas: 120 seguidas desde aqui (5620-5740). Con el bloque
# automatico de campana.py, el torre caia en 14997-15068: al cargar el primer banco de texto del mapa
# el juego leia otro recurso (#SPX) y cerraba (2026-10-15). Probado en juego: Freezer en 5525-5608 y
# 5781-5864 funciona, y el torre en 5620-5740 pasa sin errores. Como va fijo, no comprueba otros
# mods: no usar a la vez con Freezer (los dos ocupan el mismo casillero de la rueda).
TEXTOS_DESDE = 5620
# rival -> (nombre es, nombre en); todos aparecen en historia_freezer.toml
RIVALES = {0: ("Goku", "Goku"), 2: ("Gohan", "Gohan"), 7: ("Vegeta", "Vegeta"),
           8: ("Trunks", "Trunks"), 10: ("Krilin", "Krillin"), 11: ("Piccolo", "Piccolo")}
RETRATO_RIVAL = {0: 1416, 2: 1425, 7: 1436, 8: 1443, 10: 1447, 11: 1449}
RETRATO_RIVAL_SSJ = 1417          # Goku Super Saiyan (forma_rival 2, como en historia_freezer.toml)
TRAJE_RIVAL = {2: 1, 7: 3, 10: 2}  # Gohan, Vegeta y Krilin con la armadura de Namek
RETRATO_JUGADOR = {0: 1463, 1: 1464, 2: 1465, 3: 1466, 4: 1466, 5: 4501}  # Freezer por forma (5 = Mecha)
LLEGADA = {"torre": ("La torre empieza aquí.", "The tower starts here."),
           "supervivencia": ("La primera oleada empieza ya.", "The first wave starts now."),
           "kumite": ("El kumite empieza.", "The kumite begins.")}
FINAL = {"torre": (["Has subido los {n} pisos de la torre.", "Ningún guerrero ha llegado más alto."],
                   ["You climbed all {n} floors of the tower.", "No warrior has reached higher."]),
         "supervivencia": (["Has sobrevivido a las {n} oleadas.", "Los guerreros caen uno a uno."],
                           ["You survived all {n} waves.", "Warriors fall one by one."]),
         "kumite": (["Has vencido a los {n} jefes del kumite.", "Ninguno pudo contigo."],
                    ["You beat all {n} kumite bosses.", "None of them could stop you."])}
NOMBRE = {"torre": "Torre de Freezer", "supervivencia": "Supervivencia de Freezer", "kumite": "Kumite de Freezer"}
DESCRIPCION = {"torre": "Sube una torre de {n} pisos con rivales cada vez mas fuertes.",
               "supervivencia": "Resiste {n} oleadas de guerreros, cada una mas dura.",
               "kumite": "Vence a {n} jefes seguidos, todos al maximo nivel."}


def combates_de(modo, n):
    """Por combate: rival, nivel (0..4, como el de historia_freezer.toml), forma del jugador y, si hace
    falta, forma del rival, plantilla y premio. Solo se usan combinaciones que ya estan en el juego."""
    out = []
    for i in range(n):
        d = {"nivel": round(i * 4 / (n - 1))}
        if modo == "torre":
            d["rival"] = [10, 2, 11, 7, 0, 8][min(5, i * 6 // n)]
            f = i / (n - 1)
            forma = 0 if f < 0.4 else 1 if f < 0.6 else 2 if f < 0.9 else 3
        elif modo == "supervivencia":
            d["rival"] = [11, 10, 2, 7, 0, 8][i % 6]
            forma = 0
        else:  # kumite: Trunks, Vegeta y Piccolo, y al final Goku Super Saiyan
            d["rival"], d["nivel"], forma = ([8, 7, 11][i % 3], 4, 4) if i < n - 1 else (0, 4, 4)
        if forma:
            d["forma_jugador"] = forma
        if modo == "kumite" and i == n - 1:
            d.update(forma_rival=2, plantilla=715)   # Goku SSJ en el escenario de Namek a punto de explotar
        out.append(d)
    out[-1]["premio"] = 102                          # capsula de premio del ultimo combate
    return out


def textos(modo, p, n, rival):
    """Frases es/en de un combate (p = piso u oleada o jefe, empezando en 1)."""
    r_es, r_en = RIVALES[rival]
    last = p == n
    if modo == "torre":
        return dict(
            dialogo_es=[f"Piso {p} de {n}: {r_es} te cierra el paso.", "Sube si puedes. Cada piso es más duro."],
            dialogo_en=[f"Floor {p} of {n}: {r_en} blocks the way.", "Climb if you can. Each floor is harder."],
            victoria_es=[f"Piso {p} superado.", "Ya solo queda la cima." if last else "Uno menos. Sigue subiendo."],
            victoria_en=[f"Floor {p} cleared.", "Only the summit is left." if last else "One less. Keep climbing."],
            mapa_es=["La cima es mía." if last else f"Al piso {p + 1}."],
            mapa_en=["The summit is mine." if last else f"On to floor {p + 1}."])
    if modo == "supervivencia":
        return dict(
            dialogo_es=[f"Oleada {p} de {n}. {r_es} llega primero.", "¡Que vengan todos!"],
            dialogo_en=[f"Wave {p} of {n}. {r_en} comes first.", "Bring them all!"],
            victoria_es=[f"Oleada {p} resistida.", "Última oleada superada." if last else "Respira. Ya llega la siguiente."],
            victoria_en=[f"Wave {p} survived.", "Last wave survived." if last else "Breathe. The next one is coming."],
            mapa_es=["Solo queda el silencio." if last else f"Quedan {n - p} oleadas."],
            mapa_en=["Only silence is left." if last else f"{n - p} waves left."])
    return dict(  # kumite
        dialogo_es=[f"Jefe {p} de {n}: {r_es} acepta el reto.", "Veamos cuánto aguantas."],
        dialogo_en=[f"Boss {p} of {n}: {r_en} accepts the challenge.", "Let's see how long you last."],
        victoria_es=[f"{r_es} cae.", "Eres el campeón del kumite." if last else "Siguiente."],
        victoria_en=[f"{r_en} falls.", "You are the kumite champion." if last else "Next."],
        mapa_es=["Queda el último jefe." if p == n - 1 else "Otro jefe espera."],
        mapa_en=["Only the last boss is left." if p == n - 1 else "Another boss waits."])


def capitulo(modo, i, n, d, ref):
    comb, p = COMBATES[i], i + 1
    rival = d["rival"]
    retrato = RETRATO_RIVAL_SSJ if d.get("forma_rival") == 2 and rival == 0 else RETRATO_RIVAL[rival]
    c = {"combate": comb, "rival": rival, "nivel": d["nivel"], "marca": ref[comb]["marca"],
         "retratos": [retrato, RETRATO_JUGADOR[d.get("forma_jugador", 0)]]}
    for k in ("forma_jugador", "forma_rival", "plantilla", "premio"):
        if k in d:
            c[k] = d[k]
    if rival in TRAJE_RIVAL:
        c["traje_rival"] = TRAJE_RIVAL[rival]
    c.update(textos(modo, p, n, rival))
    if i == 0:
        c["llegada_es"], c["llegada_en"] = [LLEGADA[modo][0]], [LLEGADA[modo][1]]
    return c


def construir(modo, n, salida):
    """Documento TOML (dict) del modo, con los bloques copiados de historia_freezer.toml."""
    tpl = tomllib.load(open(FREEZER, "rb"))
    base, fdir = os.path.dirname(os.path.abspath(salida)), os.path.dirname(FREEZER)

    def ruta(p):  # las rutas del .toml de Freezer son relativas a su carpeta; aqui, a la del archivo nuevo
        try:
            return os.path.relpath(os.path.join(fdir, p), base).replace(os.sep, "/")
        except ValueError:  # otra unidad: ruta absoluta
            return os.path.join(fdir, p).replace(os.sep, "/")

    ref = {c["combate"]: c for c in tpl["capitulo"] if c.get("combate") in COMBATES and not c.get("saga")}
    if set(ref) != set(COMBATES):
        raise SystemExit("historia_freezer.toml ya no tiene los combates %s con marca" % COMBATES)
    capitulos = [capitulo(modo, i, n, d, ref) for i, d in enumerate(combates_de(modo, n))]
    fin = FINAL[modo]
    capitulos.append({"es": [t.format(n=n) for t in fin[0]], "en": [t.format(n=n) for t in fin[1]]})
    historia = dict(tpl["historia"], textos_desde=TEXTOS_DESDE)   # textos fijos (ver TEXTOS_DESDE)
    historia["dialogos"] = dict(tpl["historia"]["dialogos"], textos_desde=TEXTOS_DESDE)
    return {
        "campana": {"nombre": NOMBRE[modo], "mod": "reto_" + modo,
                    "descripcion": DESCRIPCION[modo].format(n=n)},
        "rueda": {"personajes": [27]},            # Freezer (casilla 22), igual que historia_freezer.toml
        "escena": tpl["escena"],                  # introduccion de Namek (contenedor 1725, entrada 22)
        "imagen": [dict(x, archivo=ruta(x["archivo"])) for x in tpl["imagen"]],
        "historia": historia,
        "retrato": [dict(r, png=ruta(r["png"])) if "png" in r else r for r in tpl.get("retrato", [])],
        "panel_limpio": tpl["panel_limpio"],
        "capitulo": capitulos,
    }


def toml_val(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, list):
        return "[" + ", ".join(toml_val(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{ " + ", ".join("%s = %s" % (k, toml_val(x)) for k, x in v.items()) + " }"
    raise TypeError(repr(v))


def toml_text(doc, cabecera):
    out = list(cabecera)
    for k, v in doc.items():
        if isinstance(v, dict):                   # [seccion]
            out.append("\n[%s]" % k)
            out += ["%s = %s" % (kk, toml_val(vv)) for kk, vv in v.items()]
        else:                                     # [[lista de tablas]]
            for item in v:
                out.append("\n[[%s]]" % k)
                out += ["%s = %s" % (kk, toml_val(vv)) for kk, vv in item.items()]
    return "\n".join(out) + "\n"


def escribir(modo, n, salida):
    cabecera = ["# Generado por modos_reto.py (modo %s, %d combates). No editar a mano: regenerar con el script." % (modo, n),
                "# Montar: python campana.py montar %s" % salida.replace(os.sep, "/")]
    os.makedirs(os.path.dirname(os.path.abspath(salida)), exist_ok=True)
    with open(salida, "w", encoding="utf-8", newline="\n") as f:
        f.write(toml_text(construir(modo, n, salida), cabecera))


# ---- Retos nativos: src/modos.cpp lee <juego>/user_data/dbz3/modos/<tipo>.toml ----
# Los mismos retos por defecto que el juego (PorDefecto en src/modos.cpp): rivales, vida del primer rival y
# cuanto sube en cada combate, si la vida del jugador pasa al siguiente combate y cuanta recupera entre medias.
NATIVO = {"torre": dict(rivales=[42, 18, 19, 21, 20, 27, 33, 36], vida_rival=(2000, 300), arrastra=False, recupera=0,
                         premio=800),   # MODOS-02: el mismo premio que PorDefecto (src/modos.cpp)
          # ficha 09: 1000 Zenie al completarla; oleadas 3/6/9 con 60 s y 4/7/10 con un rival que se regenera
          "supervivencia": dict(rivales=[42, 43, 29, 28, 30, 38, 37, 35, 40, 41], vida_rival=(1500, 150),
                                arrastra=True, recupera=500, premio=1000,
                                por_combate={2: {"tiempo": 60}, 5: {"tiempo": 60}, 8: {"tiempo": 60},
                                             3: {"regen": 25}, 6: {"regen": 25}, 9: {"regen": 25}}),
          "kumite": dict(rivales=[27, 33, 38, 40, 36], vida_rival=(5000, 500), arrastra=True, recupera=1000,
                         premio=1200)}   # MODOS-02: el mismo premio que PorDefecto (src/modos.cpp)
ESCENARIOS_NATIVO = [0, 1, 2, 3, 4, 5, 6, 7]   # indice de escenario del Duelo (0 = Torneo, con ring)
VIDA_TOPE = 7000                                # 7 barras
RECORTADOS = {22, 23, 24, 25, 26, 31}           # sin modelo en el juego: solo con un mod de roster


def nativo(tipo, rivales=None, escenarios=None, vida=3000, vida_inicial=0, vida_rival=None, arrastra=None,
           recupera=None, pausa=False, nombre="", name="", tiempo=0, regen=0, objetivo=0, premio=None,
           clima=False, gigante=0.0, intro=False, gigante_en=None):
    """Reto nativo (dict) con los valores por defecto del juego para lo que no se indique.
    tiempo = limite de cada combate (s); regen = vida por segundo que recupera el rival; objetivo = aguantar
    esos segundos gana el combate (kumite de aguante). nombre/name = el nombre que ensenan el HUB y las pantallas.
    premio = Zenie del monedero del HUB al completarlo (src/rpg.cpp). Con los rivales por defecto, los
    modificadores por oleada del juego (supervivencia). Reglas de escenario (docs/14_frentes/escenarios.md R1):
    clima = el cielo se oscurece; gigante = escala del rival (0.5..3; 0 = normal) en todos los combates o, con
    gigante_en = [indices], solo en esos; intro = intro de cine de 10 s antes de cada combate."""
    d = NATIVO[tipo]
    rivales, escenarios = rivales or d["rivales"], escenarios or ESCENARIOS_NATIVO
    v0, paso = vida_rival or d["vida_rival"]
    doc = {}
    if nombre:
        doc["nombre"] = nombre
    if name:
        doc["name"] = name
    doc.update({"vida": vida, "vida_inicial": vida_inicial,
                "arrastra_vida": d["arrastra"] if arrastra is None else arrastra,
                "recupera": d["recupera"] if recupera is None else recupera, "pausa": pausa})
    premio = d.get("premio", 0) if premio is None else premio
    if premio:
        doc["premio"] = premio
    for k, v in (("tiempo", tiempo), ("regen", regen), ("objetivo_tiempo", objetivo), ("clima", clima),
                 ("intro", intro)):
        if v:
            doc[k] = v
    if gigante and gigante_en is None:
        doc["gigante"] = float(gigante)
    doc["combate"] = [{"rival": r, "escenario": escenarios[i % len(escenarios)],
                       "vida": min(VIDA_TOPE, v0 + paso * i)} for i, r in enumerate(rivales)]
    if rivales is d["rivales"]:
        for i, extra in d.get("por_combate", {}).items():
            doc["combate"][i].update(extra)
    if gigante and gigante_en is not None:
        for i in gigante_en:
            doc["combate"][i]["gigante"] = float(gigante)
    return doc


def comprobar_nativo(doc):
    """Errores de un reto nativo (lista vacia = bien) con los mismos limites que aplica el juego."""
    err = []
    if not 1 <= doc.get("vida", 3000) <= VIDA_TOPE:
        err.append("vida fuera de 1..%d" % VIDA_TOPE)
    if not 0 <= doc.get("vida_inicial", 0) <= VIDA_TOPE:
        err.append("vida_inicial fuera de 0..%d" % VIDA_TOPE)
    cs = doc.get("combate", [])
    if not cs:
        err.append("sin combates")
    for i, c in enumerate(cs, 1):
        if not 0 <= c.get("rival", -1) < 64:
            err.append("combate %d: rival fuera de 0..63" % i)
        if not 0 <= c.get("escenario", 0) <= 16:
            err.append("combate %d: escenario fuera de 0..16" % i)
        if not 0 <= c.get("vida", 0) <= VIDA_TOPE:
            err.append("combate %d: vida fuera de 0..%d" % (i, VIDA_TOPE))
    # modificadores (en el reto o en cada combate): mismos topes que el juego
    for d, donde in [(doc, "reto")] + [(c, "combate %d" % i) for i, c in enumerate(cs, 1)]:
        for k, tope in (("tiempo", 999), ("regen", 1000), ("objetivo_tiempo", 999), ("premio", 100000)):
            if k in d and not (0 <= d[k] <= tope):
                err.append("%s: %s fuera de 0..%d" % (donde, k, tope))
        if "gigante" in d and not (d["gigante"] == 0 or 0.5 <= d["gigante"] <= 3):
            err.append("%s: gigante fuera de 0.5..3 (0 = normal)" % donde)
        for k in ("clima", "intro"):
            if k in d and not isinstance(d[k], bool):
                err.append("%s: %s tiene que ser true o false" % (donde, k))
    return err


def escribir_torneo(participantes, salida):
    """user_data/dbz3/modos/torneo.toml: los COM del Torneo Mundial (en orden; los que falten, al azar)."""
    if not participantes or not all(0 <= x < 64 for x in participantes):
        raise SystemExit("participantes: IDs de 0 a 63, separados por comas")
    os.makedirs(os.path.dirname(os.path.abspath(salida)), exist_ok=True)
    with open(salida, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Torneo Mundial con bots elegidos (src/modos.cpp): los participantes COM, en orden.\n"
                "# Los que falten salen al azar, como siempre; un ID 44-63 necesita su mod de roster.\n"
                "participantes = %s\n" % toml_val(list(participantes)))


def escribir_nativo(tipo, doc, salida):
    lineas = ["# Reto nativo (%s) generado por modos_reto.py. Lo lee el juego en user_data/dbz3/modos/%s.toml." % (tipo, tipo),
              "# vida = maxima del jugador; vida_inicial = la del primer combate (0 = la maxima); arrastra_vida =",
              "# la vida que le queda pasa al siguiente (+ recupera); pausa = espera a la pantalla de resultados del HUB.",
              "# Por combate: rival (ID 0-63), traje, escenario (0-16) y vida del rival (0 = la normal, 3000).",
              "# Opcional (reto o combate): tiempo = limite en s; regen = vida/s del rival; objetivo_tiempo = aguantar.",
              "# premio = Zenie del monedero del HUB al completarlo.",
              "# clima = true (el cielo se oscurece), gigante = 1.8 (rival a esa escala, 0.5..3), intro = true (cine de 10 s)."]
    lineas += ["%s = %s" % (k, toml_val(v)) for k, v in doc.items() if k != "combate"]
    for c in doc["combate"]:
        lineas += ["", "[[combate]]"] + ["%s = %s" % (k, toml_val(v)) for k, v in c.items()]
    os.makedirs(os.path.dirname(os.path.abspath(salida)), exist_ok=True)
    with open(salida, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lineas) + "\n")


def comprobar(doc, modo, n, salida):
    """Errores de un TOML generado (lista vacia = bien): claves obligatorias y combinaciones usadas."""
    err = []

    def chk(cond, msg):
        if not cond:
            err.append("%s: %s" % (modo, msg))

    camp, h = doc.get("campana", {}), doc.get("historia", {})
    chk(camp.get("mod") == "reto_" + modo and camp.get("nombre"), "[campana] mod/nombre")
    chk(doc.get("rueda", {}).get("personajes") == [27], "[rueda] personajes = [27]")
    for k in ("codigo", "contenedor", "fondo", "final", "combates", "dialogos"):
        chk(k in h, "[historia] falta %s" % k)
    chk(all(k in h.get("combates", {}) for k in ("codigo", "contenedor", "plantilla")), "[historia.combates]")
    chk(all(k in h.get("final", {}) for k in ("contenedor", "codigo", "entrada", "fondo")), "[historia.final]")
    chk(h.get("textos_desde") == TEXTOS_DESDE and h.get("dialogos", {}).get("textos_desde") == TEXTOS_DESDE,
        "[historia] y dialogos sin textos_desde fijo (%d)" % TEXTOS_DESDE)
    chk(doc.get("escena") and "cambiar" in doc["escena"][0], "falta la escena de introduccion")
    caps = doc.get("capitulo", [])
    chk(len(caps) == n + 1, "%d capitulos, se esperaban %d" % (len(caps), n + 1))
    fights = [c for c in caps if "combate" in c]
    chk(len(fights) == n and "combate" not in caps[-1], "combates o final mal colocados")
    chk(len({c["combate"] for c in fights}) == len(fights), "combate repetido")
    for c in fights:
        chk(c["combate"] in COMBATES, "combate %s fuera de los reutilizables" % c["combate"])
        chk(c["rival"] in RIVALES, "rival %s fuera de los usados en historia_freezer" % c["rival"])
        chk(len(c.get("retratos", [])) == 2 and len(c.get("marca", [])) == 6, "retratos/marca de %s" % c["combate"])
        for k in ("dialogo", "victoria", "mapa"):
            chk(c.get(k + "_es") and c.get(k + "_en"), "%s_es/_en de %s" % (k, c["combate"]))
    chk(caps and caps[-1].get("es") and caps[-1].get("en"), "texto final es/en")
    if modo == "torre":
        niv = [c["nivel"] for c in fights]
        chk(niv == sorted(niv) and niv[-1] > niv[0], "la torre no sube de nivel")
    # Las imagenes y retratos propios son opcionales: el zip del Kit no lleva arte de terceros y campana.py
    # usa entonces el del juego (panel de la historia / retrato de Freezer). Solo se avisa.
    for img in doc.get("imagen", []) + doc.get("retrato", []):
        p = img.get("archivo") or img.get("png")
        if p and not os.path.isfile(os.path.join(os.path.dirname(os.path.abspath(salida)), p)):
            print("aviso: %s: no existe %s (se usara el arte del juego)" % (modo, p))
    return err


MODOS_SELFTEST = (("torre", 10), ("supervivencia", 8), ("kumite", 5))


def selftest():
    tmp = tempfile.mkdtemp(prefix="modos_reto_")   # carpeta temporal: nada se escribe en el juego ni en mods/
    err = []
    try:
        for tipo in NATIVO:                         # retos nativos: por defecto y con opciones, ida y vuelta
            for doc in (nativo(tipo), nativo(tipo, rivales=[44, 7], escenarios=[2], vida_rival=(6500, 1000),
                                              vida_inicial=1200, pausa=True)):
                s = os.path.join(tmp, "nativo_%s.toml" % tipo)
                escribir_nativo(tipo, doc, s)
                back = tomllib.load(open(s, "rb"))
                e = comprobar_nativo(back) + ([] if back == doc else ["%s: el TOML no vuelve igual" % tipo])
                err += ["nativo " + x for x in e]
            print("nativo %s: %d combates, %s" % (tipo, len(nativo(tipo)["combate"]), "OK" if not err else "CON ERRORES"))
        if nativo("kumite", rivales=[1, 2], vida_rival=(6500, 1000))["combate"][1]["vida"] != VIDA_TOPE:
            err.append("nativo: la vida del rival no se recorta a %d" % VIDA_TOPE)
        doc = nativo("kumite", nombre="Aguante", name="Endurance", tiempo=60, regen=50, objetivo=45)
        s = os.path.join(tmp, "nativo_mod.toml")
        escribir_nativo("kumite", doc, s)
        back = tomllib.load(open(s, "rb"))
        if back != doc or comprobar_nativo(back):
            err.append("nativo: modificadores sin ida y vuelta: %s" % comprobar_nativo(back))
        jg = nativo("kumite", rivales=[33, 40, 36], clima=True, gigante=1.8, gigante_en=[2], intro=True)
        if jg.get("clima") is not True or jg["combate"][2].get("gigante") != 1.8 or "gigante" in jg["combate"][0] \
                or comprobar_nativo(jg):
            err.append("nativo: reglas de escenario (clima, gigante, intro) sin ida y vuelta: %s" % comprobar_nativo(jg))
        if not comprobar_nativo(dict(jg, gigante=9.0)):
            err.append("nativo: gigante fuera de tope no se detecta")
        sv = nativo("supervivencia")
        if sv.get("premio") != 1000 or sv["combate"][2].get("tiempo") != 60 or sv["combate"][3].get("regen") != 25:
            err.append("nativo: supervivencia sin premio o sin modificadores por oleada (ficha 09)")
        if not comprobar_nativo(dict(doc, regen=5000)):
            err.append("nativo: regen fuera de tope no se detecta")
        t = os.path.join(tmp, "torneo.toml")
        escribir_torneo([44, 33, 7], t)
        if tomllib.load(open(t, "rb")).get("participantes") != [44, 33, 7]:
            err.append("torneo.toml sin ida y vuelta")
        for f in sorted(os.listdir(SALIDA_DEFECTO)):   # catalogo de retos nativos (los del frente de vision incluidos)
            if f.startswith("nativo_") and f.endswith(".toml"):
                e = comprobar_nativo(tomllib.load(open(os.path.join(SALIDA_DEFECTO, f), "rb")))
                err += ["%s: %s" % (f, x) for x in e]
                print("catalogo %s: %s" % (f, "OK" if not e else "CON ERRORES"))
        salidas = {}
        for modo, n in MODOS_SELFTEST:
            salidas[modo] = os.path.join(tmp, "reto_%s.toml" % modo)
            escribir(modo, n, salidas[modo])
            e = comprobar(tomllib.load(open(salidas[modo], "rb")), modo, n, salidas[modo])
            err += e
            print("%s: %d combates, TOML %s" % (modo, n, "OK" if not e else "CON ERRORES"))
        # montar real con campana.py, en un mods/ temporal (solo lee us/ del juego)
        if not os.path.isfile(os.path.join(ROOT, "us", "data_cmn.afs")):
            print("montar: omitido (no encuentro us/data_cmn.afs)")
        else:
            env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
            for modo, n in MODOS_SELFTEST:
                # una carpeta mods/ por modo: los tres usan la casilla de Freezer y campana.py no deja montar
                # dos campanas en la misma casilla (en el juego se activa un modo a la vez)
                mods = os.path.join(tmp, "mods_" + modo)
                os.makedirs(mods)
                r = subprocess.run([sys.executable, os.path.join(HERE, "campana.py"), "--mods", mods,
                                    "montar", salidas[modo]], cwd=HERE, capture_output=True, text=True,
                                   encoding="utf-8", env=env)
                d = os.path.join(mods, "reto_" + modo)
                ok = r.returncode == 0 and os.path.isfile(os.path.join(d, "campana.toml")) and all(
                    os.path.isfile(os.path.join(d, "us", "data_cmn.afs", str(c), "geom.bin")) for c in COMBATES[:n])
                if ok:
                    print("montar %s: %s" % (modo, r.stdout.strip().splitlines()[-1]))
                else:
                    print("montar %s: FALLO (codigo %s)" % (modo, r.returncode))
                    err.append("montar %s fallo: %s" % (modo, (r.stdout + r.stderr)[-600:]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if err:
        print("selftest: %d error(es)" % len(err))
        for e in err:
            print("  - " + e)
        return 1
    print("selftest OK")
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--selftest", action="store_true", help="genera los tres modos en una carpeta temporal y los valida")
    sub = p.add_subparsers(dest="modo")
    t = sub.add_parser("torre", help="torre de N pisos")
    t.add_argument("--pisos", type=int, default=10)
    s = sub.add_parser("supervivencia", help="N oleadas")
    s.add_argument("--oleadas", type=int, default=8)
    k = sub.add_parser("kumite", help="N jefes seguidos")
    k.add_argument("--jefes", type=int, default=5)
    for sp in (t, s, k):
        sp.add_argument("--salida", help="TOML a escribir (por defecto campanas/retos/reto_<modo>.toml)")
    nv = sub.add_parser("nativo", help="reto nativo para el juego (user_data/dbz3/modos/<tipo>.toml)")
    nv.add_argument("tipo", choices=list(NATIVO))
    ints = lambda x: [int(v) for v in x.split(",") if v.strip()]
    nv.add_argument("--rivales", type=ints, help="IDs separados por comas (0-63; 44-63 = personajes extra)")
    nv.add_argument("--escenarios", type=ints, help="escenario de cada combate (se repiten en ciclo)")
    nv.add_argument("--vida", type=int, default=3000, help="vida maxima del jugador")
    nv.add_argument("--vida-inicial", type=int, default=0, help="vida del primer combate (0 = la maxima)")
    nv.add_argument("--vida-rival", help="INICIO[:PASO]: vida del primer rival y cuanto sube en cada combate")
    nv.add_argument("--arrastra", choices=["si", "no"], help="la vida pasa al combate siguiente")
    nv.add_argument("--recupera", type=int, help="vida que recupera entre combates")
    nv.add_argument("--pausa", action="store_true", help="espera a la pantalla de resultados del HUB")
    nv.add_argument("--nombre", default="", help="nombre del reto (espanol)")
    nv.add_argument("--name", default="", help="nombre del reto (ingles)")
    nv.add_argument("--tiempo", type=int, default=0, help="limite de tiempo de cada combate (s)")
    nv.add_argument("--regen", type=int, default=0, help="vida por segundo que recupera el rival")
    nv.add_argument("--objetivo", type=int, default=0, help="aguantar estos segundos gana el combate")
    nv.add_argument("--premio", type=int, help="Zenie del monedero del HUB al completarlo")
    nv.add_argument("--clima", action="store_true", help="el cielo se oscurece durante los combates")
    nv.add_argument("--gigante", type=float, default=0.0, help="escala del rival (0.5..3)")
    nv.add_argument("--gigante-en", type=ints, help="solo en estos combates (indices desde 0)")
    nv.add_argument("--intro", action="store_true", help="intro de cine de 10 s antes de cada combate")
    tn = sub.add_parser("torneo", help="Torneo Mundial con bots elegidos (user_data/dbz3/modos/torneo.toml)")
    tn.add_argument("--participantes", type=ints, required=True, help="IDs de los COM, en orden")
    dest_t = tn.add_mutually_exclusive_group()
    dest_t.add_argument("--salida", help="TOML a escribir (por defecto campanas/retos/torneo.toml)")
    dest_t.add_argument("--juego", help="carpeta del juego: escribe <juego>/user_data/dbz3/modos/torneo.toml")
    dest = nv.add_mutually_exclusive_group()
    dest.add_argument("--salida", help="TOML a escribir (por defecto campanas/retos/nativo_<tipo>.toml)")
    dest.add_argument("--juego", help="carpeta del juego: escribe <juego>/user_data/dbz3/modos/<tipo>.toml")
    a = p.parse_args(argv)
    if a.selftest:
        return selftest()
    if a.modo == "nativo":
        vr = tuple(int(x) for x in a.vida_rival.split(":")) + (0,) if a.vida_rival else None
        doc = nativo(a.tipo, a.rivales, a.escenarios, a.vida, a.vida_inicial, vr[:2] if vr else None,
                     None if a.arrastra is None else a.arrastra == "si", a.recupera, a.pausa, a.nombre, a.name,
                     a.tiempo, a.regen, a.objetivo, a.premio, a.clima, a.gigante, a.intro, a.gigante_en)
        err = comprobar_nativo(doc)
        if err:
            p.error("; ".join(err))
        for c in doc["combate"]:
            if c["rival"] in RECORTADOS or c["rival"] >= 44:
                print("aviso: el rival %d solo existe con un mod de roster activo" % c["rival"])
        salida = (os.path.join(a.juego, "user_data", "dbz3", "modos", a.tipo + ".toml") if a.juego
                  else a.salida or os.path.join(SALIDA_DEFECTO, "nativo_%s.toml" % a.tipo))
        escribir_nativo(a.tipo, doc, salida)
        print("nativo %s: %d combates -> %s" % (a.tipo, len(doc["combate"]), salida))
        return 0
    if a.modo == "torneo":
        salida = (os.path.join(a.juego, "user_data", "dbz3", "modos", "torneo.toml") if a.juego
                  else a.salida or os.path.join(SALIDA_DEFECTO, "torneo.toml"))
        escribir_torneo(a.participantes, salida)
        print("torneo: %d participantes -> %s" % (len(a.participantes), salida))
        return 0
    if not a.modo:
        p.error("indica un modo (torre, supervivencia, kumite, nativo, torneo) o --selftest")
    n = a.pisos if a.modo == "torre" else a.oleadas if a.modo == "supervivencia" else a.jefes
    if not 2 <= n <= MAX_COMBATES:
        p.error("entre 2 y %d combates por modo (los #BPC reutilizables)" % MAX_COMBATES)
    salida = a.salida or os.path.join(SALIDA_DEFECTO, "reto_%s.toml" % a.modo)
    escribir(a.modo, n, salida)
    print("%s: %d combates -> %s" % (a.modo, n, salida))
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Las sagas del Modo Heroe (res/heroe/sagas.toml) como historia de Dragon Universe (campana.py).

  python heroe_sagas_a_du.py                 escribe campanas/historia_heroe.toml
  python heroe_sagas_a_du.py --montar        ...y lo monta (python campana.py montar)
  python heroe_sagas_a_du.py --comprobar     autocomprobacion (sin juego)

El Heroe es exclusivo de su modo: entra en DU con hub::RequestDu (salta titulo, menu y rueda de personajes) en la
casilla CASILLA (35/36/37: libres en DU) y el juego lo crea como el ID propio (63) en el mapa y en el combate.
La casilla tiene 4 bloques nativos (sagas 1-4: 5100-5300 y bloque 0). Las sagas 5-8 NO usan codigos virtuales
(el motor DU no los reproduce, HS4): reutilizan el bloque 0 y cada una lleva sus contenedores propios
([codigos_saga], campana.py), que campaigns.cpp elige por la saga en curso. 8 sagas / 27 combates por casilla.
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
RETRATO = {0: 1416, 7: 1436, 10: 1447, 11: 1449, 12: 1450, 18: 1459, 20: 1461, 21: 1462, 27: 1466, 29: 1469, 30: 1471, 33: 1477,
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


_TRAD = None


def _trad():
    """fr/de/it del juego: i18n_modos.toml (clave = frase inglesa). Sin entrada queda el ingles."""
    global _TRAD
    if _TRAD is None:
        _TRAD = {}
        aqui = os.path.dirname(os.path.abspath(__file__))
        for c in (os.path.join(aqui, "..", "res", "ingame_ui", "i18n_modos.toml"), os.path.join(aqui, "..", "ingame_ui", "i18n_modos.toml")):
            if os.path.isfile(c):
                try:
                    with open(c, "rb") as f:
                        _TRAD = tomllib.load(f)
                    break
                except Exception:
                    pass
    return _TRAD


def _con_idiomas(out, extra=None):
    """Tras cada `clave_en = [...]` (o `en = [...]`) anade clave_fr/_de/_it con la traduccion de cada frase."""
    t, res = _trad(), []
    for l in out:
        res.append(l)
        if " = [" not in l:
            continue
        k, v = l.split(" = ", 1)
        if not (k.endswith("_en") or k == "en"):
            continue
        base = k[:-2]
        try:
            items = tomllib.loads("x = " + v)["x"]
        except Exception:
            continue
        for lg in ("fr", "de", "it"):
            res.append("%s%s = %s" % (base, lg, lst([t.get(x, {}).get(lg) or (extra or {}).get(x, {}).get(lg) or x for x in items])))
    return res


def leer(ruta):
    with open(ruta, "rb") as f:
        return tomllib.load(f)


MAESTROS = (0, 7, 11, 10, 27, 33)   # el mismo orden que kEstilos de src/heroe.cpp: sagas.toml `maestro` -> ID
# Marca del maestro: un punto del mapa de cada planeta distinto de los combates de la saga (zona, x, y, z; 29 = bolas nativas de 106.0)
MARCA_MAESTRO_TIERRA = (29, 4520, 20, 8362)
MARCA_MAESTRO_NAMEK = (2, -4735, 45, 4170)
# Puntos del mapa por saga: escenas y posiciones nativas de la historia de Goku (intros 100-106), con icono nativo (bits 9-10 de los que llevan 0x41 = color de la columna de luz, medido en juego 31/10):
# 1089 columna azul = Esfera (op 38; el 176 nativo no se ve), 2688 destellos = objeto/Zeni (op 43/19/18), 577 columna roja = combate opcional (2400-2403), 1601 dorada = maestro. hueco: 1 opcional, 2 Zeni, 3 objeto, 4 Esfera.
PUNTOS_TIERRA = [
    [("2400.0", 1, 577, 33, 4700, 80, -500, 1000), ("2000.2", 2, 2688, 29, -1800, 30, -7000, 1000),
     ("2000.9", 3, 2688, 29, 7267, 0, -7217, 1000), ("2000.6", 4, 1089, 29, 9200, 122, -8700, 1000)],
    [("2402.0", 1, 577, 33, 3726, 70, 3151, 1500), ("2200.3", 2, 2688, 29, 3528, 20, -8921, 1000),
     ("2200.6", 3, 2688, 29, -9112, 20, 2571, 1000), ("2200.4", 4, 1089, 29, 9079, 20, 9969, 750)],
    [("2403.0", 1, 577, 33, 1902, 40, -721, 1500), ("2300.9", 2, 2688, 29, 7414, 20, -6821, 1000),
     ("2300.2", 3, 2688, 29, 8349, 60, -1647, 1000), ("2300.10", 4, 1089, 29, 464, 20, -9065, 750)],
]
PUNTOS_NAMEK = [   # (opcional, objeto, Esfera); las escenas se repiten entre sagas: cada una borra las marcas de la anterior (op 22)
    [("2401.0", 1, 577, 6, -4160, 130, -3202, 1000), ("2100.1", 3, 2688, 9, -3010, 15, -4781, 1000), ("2100.3", 4, 1089, 9, 4799, 15, -6998, 1000)],
    [("2401.0", 1, 577, 6, -4160, 130, -3202, 1000), ("2100.0", 3, 2688, 9, -6214, 15, 5477, 1000), ("2100.4", 4, 1089, 9, -5788, 25, -2764, 1000)],
    [("2401.0", 1, 577, 6, -4160, 130, -3202, 1000), ("2100.8", 3, 2688, 9, 6907, 15, 3096, 1000), ("2100.3", 4, 1089, 9, 4799, 15, -6998, 1000)],
]
ESFERA = 1089
CASILLAS = (35, 36, 37)   # C4: una casilla de DU por ficha del Heroe (ficha 1 = 35, 2 = 36, 3 = 37)


# Traducciones fr/de/it de las frases de las plantillas (clave = frase inglesa con {h}/{s}/{n}). {s} se dice "saga" sin su nombre
# (los nombres de saga de sagas.toml siguen en ingles). Japones y portugues no tienen idioma de datos: el juego usa el ingles.
TRAD_PLANTILLAS = {
    "The name's {h}. Remember it.": ("Je m'appelle {h}. Retiens-le.", "Ich heiße {h}. Merk dir das.", "Mi chiamo {h}. Ricordalo."),
    "I'm coming for you.": ("J'arrive.", "Ich komme dich holen.", "Vengo a prenderti."),
    "Step aside, I'm in a hurry.": ("Pousse-toi, je suis pressé.", "Geh zur Seite, ich habe es eilig.", "Spostati, ho fretta."),
    "{h} doesn't back down.": ("{h} ne recule pas.", "{h} weicht nicht zurück.", "{h} non indietreggia."),
    "Let's see what you've got.": ("Voyons ce que tu vaux.", "Mal sehen, was du kannst.", "Vediamo di cosa sei capace."),
    "Your road ends here.": ("Ta route s'arrête ici.", "Hier endet dein Weg.", "La tua strada finisce qui."),
    "I'm stronger than I was yesterday.": ("Je suis plus fort qu'hier.", "Ich bin stärker als gestern.", "Oggi sono più forte di ieri."),
    "You haven't seen anything yet.": ("Tu n'as encore rien vu.", "Du hast noch gar nichts gesehen.", "Non hai ancora visto nulla."),
    "Out of my way.": ("Écarte-toi de mon chemin.", "Aus dem Weg.", "Fuori dai piedi."),
    "This {s} ends with me.": ("Cette saga s'achève avec moi.", "Diese Saga endet mit mir.", "Questa saga finisce con me."),
    "For the Dragon Balls, let's go.": ("Pour les Dragon Balls, allons-y.", "Für die Dragon Balls, los geht's.", "Per le Sfere del Drago, andiamo."),
    "Impressive...": ("Impressionnant...", "Beeindruckend...", "Impressionante..."),
    "Who... are you?": ("Qui... es-tu ?", "Wer... bist du?", "Chi... sei?"),
    "This can't be...": ("Impossible...", "Das kann nicht sein...", "Non può essere..."),
    "Your strength is real.": ("Ta force est réelle.", "Deine Stärke ist echt.", "La tua forza è reale."),
    "You beat me, {h}.": ("Tu m'as battu, {h}.", "Du hast mich besiegt, {h}.", "Mi hai battuto, {h}."),
    "This isn't over.": ("Ce n'est pas fini.", "Das ist noch nicht vorbei.", "Non è finita."),
    "I'll remember you, {h}.": ("Je me souviendrai de toi, {h}.", "Ich werde mich an dich erinnern, {h}.", "Mi ricorderò di te, {h}."),
    "You're stronger than you look.": ("Tu es plus fort que tu en as l'air.", "Du bist stärker, als du aussiehst.", "Sei più forte di quanto sembri."),
    "Damn it...": ("Zut...", "Verdammt...", "Maledizione..."),
    "I'll be on my way.": ("Je continue ma route.", "Ich gehe weiter.", "Proseguo per la mia strada."),
    "Good fight.": ("Beau combat.", "Guter Kampf.", "Bel combattimento."),
    "There's still more to this {s}.": ("Cette saga n'est pas finie.", "Diese Saga ist noch nicht vorbei.", "Questa saga non è ancora finita."),
    "That was just the warm-up.": ("Ce n'était que l'échauffement.", "Das war nur das Aufwärmen.", "Era solo il riscaldamento."),
    "Train harder and come back.": ("Entraîne-toi et reviens.", "Trainiere mehr und komm wieder.", "Allenati di più e torna."),
    "One step closer to the Dragon Balls.": ("Un pas de plus vers les Dragon Balls.", "Ein Schritt näher an den Dragon Balls.", "Un passo più vicino alle Sfere del Drago."),
    "{h} is still standing.": ("{h} est toujours debout.", "{h} steht noch.", "{h} è ancora in piedi."),
    "It had to be done.": ("Il fallait le faire.", "Es musste sein.", "Andava fatto."),
    "Onwards.": ("En avant.", "Weiter.", "Avanti."),
    "Who's next?": ("Qui est le suivant ?", "Wer ist der Nächste?", "Chi è il prossimo?"),
    "The radar shows something over there.": ("Le radar indique quelque chose par là.", "Das Radar zeigt dort etwas an.", "Il radar segnala qualcosa laggiù."),
    "I can still sense energy nearby.": ("Je sens encore de l'énergie près d'ici.", "Ich spüre noch Energie in der Nähe.", "Sento ancora energia nelle vicinanze."),
    "No time to lose.": ("Pas de temps à perdre.", "Keine Zeit zu verlieren.", "Non c'è tempo da perdere."),
    "This doesn't end here.": ("Ça ne s'arrête pas là.", "Das endet hier nicht.", "Non finisce qui."),
    "On my way.": ("J'y vais.", "Ich bin unterwegs.", "Arrivo."),
    # llegada y maestro (la frase inglesa lleva el nombre de la saga {n} / del heroe {h})
    "{n}: here it begins.": ("{n} : tout commence ici.", "{n}: Hier beginnt es.", "{n}: si comincia."),
    "{n}: here I come.": ("{n} : j'arrive.", "{n}: Ich komme.", "{n}: eccomi."),
    "{h}, before you go on with the {n}, train with me.": ("{h}, avant de continuer, entraîne-toi avec moi.", "{h}, bevor du weitermachst, trainiere mit mir.", "{h}, prima di proseguire, allenati con me."),
    "All right, master.": ("D'accord, maître.", "Einverstanden, Meister.", "D'accordo, maestro."),
    "You've improved a lot, {h}.": ("Tu as beaucoup progressé, {h}.", "Du hast dich sehr verbessert, {h}.", "Sei migliorato molto, {h}."),
    "Thank you, master.": ("Merci, maître.", "Danke, Meister.", "Grazie, maestro."),
    "Back to training.": ("On continue l'entraînement.", "Zurück zum Training.", "Si torna ad allenarsi."),
}


def _extra(h_en, nombres):
    """{frase inglesa ya con nombres: {lg: texto}} para el heroe y las sagas de esta generacion."""
    ex = {}
    for k, v in TRAD_PLANTILLAS.items():
        for n in (nombres if ("{n}" in k or "{s}" in k) else [""]):
            e = k.replace("{h}", h_en).replace("{s}", n or "saga").replace("{n}", n)
            ex[e] = {lg: t.replace("{h}", h_en).replace("{n}", n) for lg, t in zip(("fr", "de", "it"), v)}
    return ex


# Frases del heroe y de los rivales (sin repetir entre combates seguidos): {h} = su nombre, {s} = la saga. Los rivales abren con su
# frase de sagas.toml (es/en, jefe_es/jefe_en) y el heroe contesta.
RESPUESTA = [("Me llamo {h}. Recuérdalo.", "The name's {h}. Remember it."), ("Voy a por ti.", "I'm coming for you."), ("Apártate, tengo prisa.", "Step aside, I'm in a hurry."),
             ("{h} no retrocede.", "{h} doesn't back down."), ("Veamos de qué eres capaz.", "Let's see what you've got."),
             ("Aquí se acaba tu camino.", "Your road ends here."), ("Hoy soy más fuerte que ayer.", "I'm stronger than I was yesterday."),
             ("No has visto nada todavía.", "You haven't seen anything yet."),
             ("Sal de mi camino.", "Out of my way."), ("Esta {s} acaba conmigo.", "This {s} ends with me."),
             ("Por las Esferas, adelante.", "For the Dragon Balls, let's go.")]
RIVAL_CAE = [("Impresionante...", "Impressive..."), ("¿Quién... eres?", "Who... are you?"), ("No puede ser...", "This can't be..."),
             ("Tu fuerza es de verdad.", "Your strength is real."), ("Me has vencido, {h}.", "You beat me, {h}."),
             ("Esto no ha terminado.", "This isn't over."), ("Te recordaré, {h}.", "I'll remember you, {h}."),
             ("Eres más fuerte de lo que pareces.", "You're stronger than you look."), ("Maldición...", "Damn it...")]
HEROE_GANA = [("Sigo mi camino.", "I'll be on my way."), ("Buen combate.", "Good fight."), ("Aún queda mucho en esta {s}.", "There's still more to this {s}."),
              ("Eso ha sido solo el calentamiento.", "That was just the warm-up."), ("Entrena más y vuelve.", "Train harder and come back."),
              ("Otro paso hacia las Esferas.", "One step closer to the Dragon Balls."), ("{h} sigue en pie.", "{h} is still standing."),
              ("Tenía que hacerse.", "It had to be done.")]
VUELTA_MAPA = [("Sigamos.", "Onwards."), ("¿Quién será el siguiente?", "Who's next?"), ("El radar marca algo por allí.", "The radar shows something over there."),
               ("Aún noto energía cerca.", "I can still sense energy nearby."), ("No hay tiempo que perder.", "No time to lose."),
               ("Esto no acaba aquí.", "This doesn't end here."), ("Voy para allá.", "On my way.")]


def generar(sagas, casilla=36, heroe_id=63, retrato_heroe=1416, nombre=None, n_sagas=MAX_SAGAS, heroe=None):
    nombre = nombre or "historia_heroe_%d" % casilla
    h_es, h_en = heroe or "Héroe", heroe or "Hero"

    def frase(pool, i, s):
        """(es, en) de la frase i del grupo, con el nombre del heroe y el de su saga."""
        es, en = pool[i % len(pool)]
        return (es.replace("{h}", h_es).replace("{s}", s.get("nombre_es", "saga")),
                en.replace("{h}", h_en).replace("{s}", s.get("nombre_en", "saga")))
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
           "eventos_mapa = { codigos = [2000, 2100, 2200, 2300], retrato = %d }" % retrato_heroe,
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
            r_es, r_en = frase(RESPUESTA, k, s)
            rc_es, rc_en = frase(RIVAL_CAE, k, s)
            hg_es, hg_en = frase(HEROE_GANA, k, s)
            vm_es, vm_en = frase(VUELTA_MAPA, k, s)
            boss_es, boss_en = c.get("jefe_es"), c.get("jefe_en")
            out += ["[[capitulo]]", "combate = %d" % (BPC0 + k), "rival = %d" % rival,
                    "escenario = %d" % int(c.get("escenario", 7)),
                    *(["plantilla = 717"] if tierra and not (ci == 0 and si > 0) else []),   # H3: #BPC de la Tierra (escenario 8); con 709 (Namek) toda pelea salia en Namek
                    "retratos = [%d, %d]" % (RETRATO.get(rival, 1416), retrato_heroe),
                    "marca = [1, %d, %d, %d, %d, %d]" % (zona, x, y, z, 1500 if tierra else 1000),
                    "dialogo_es = %s" % lst([boss_es or es, r_es]),
                    "dialogo_en = %s" % lst([boss_en or en, r_en]),
                    "victoria_es = %s" % lst([rc_es, hg_es]),
                    "victoria_en = %s" % lst([rc_en, hg_en]),
                    "mapa_es = %s" % lst([vm_es]), "mapa_en = %s" % lst([vm_en])]
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
                        "llegada_es = %s" % lst(["%s, %s." % (s.get("nombre_es", "Saga"), "aquí empieza" if si == 0 else "allá voy")]),
                        "llegada_en = %s" % lst(["%s: %s." % (s.get("nombre_en", "Saga"), "here it begins" if si == 0 else "here I come")])]
            out.append("")
            plan.append((si, ci, rival))
            k += 1
    for si, s in enumerate(sg):
        m = int(s.get("maestro", -1))
        if not 0 <= m < len(MAESTROS):
            continue
        tierra = int(s.get("planeta", 1)) == 0
        zona, x, y, z = MARCA_MAESTRO_TIERRA if tierra else MARCA_MAESTRO_NAMEK
        if os.environ.get("HEROE_PUNTOS_CERCA"):   # PRUEBA: ver prueba de colores (abajo)
            cx, y, z = [int(v) for v in os.environ["HEROE_PUNTOS_CERCA"].split(",")]
            x = cx + 2500
        rival = MAESTROS[m]
        out += ["[[maestro_marca]]", "saga = %d" % si, "rival = %d" % rival, "escenario = %d" % int(s["combate"][0].get("escenario", 8 if tierra else 7)),
                "plantilla = %d" % (717 if tierra else 709), "fondo_dialogo = %d" % (1549 if tierra else 1536),
                "retratos = [%d, %d]" % (RETRATO[rival], retrato_heroe), "marca = [1601, %d, %d, %d, %d, 1000]" % (zona, x, y, z),   # icono nativo de 2400.3
                "dialogo_es = %s" % lst(["%s, antes de seguir con la %s, entrena conmigo." % (h_es, s.get("nombre_es", "saga")), "De acuerdo, maestro."]),
                "dialogo_en = %s" % lst(["%s, before you go on with the %s, train with me." % (h_en, s.get("nombre_en", "saga")), "All right, master."]),
                "victoria_es = %s" % lst(["Has mejorado mucho, %s." % h_es, "Gracias, maestro."]),
                "victoria_en = %s" % lst(["You've improved a lot, %s." % h_en, "Thank you, master."]),
                "mapa_es = %s" % lst(["A seguir entrenando."]), "mapa_en = %s" % lst(["Back to training."]), ""]
    nt = nn = 0
    for si, s in enumerate(sg):   # la saga 8 no lleva Esfera: se ganan 7
        tierra = int(s.get("planeta", 1)) == 0
        fila = PUNTOS_TIERRA[nt % len(PUNTOS_TIERRA)] if tierra else PUNTOS_NAMEK[nn % len(PUNTOS_NAMEK)]
        nt, nn = nt + tierra, nn + (not tierra)
        for np, p in enumerate(fila):
            if p[2] == ESFERA and si >= 7:
                continue
            if os.environ.get("HEROE_PUNTOS_CERCA"):   # PRUEBA (captura de colores): en fila delante de la salida de la saga
                cx, cy, cz = [int(v) for v in os.environ["HEROE_PUNTOS_CERCA"].split(",")]
                p = p[:4] + (cx - 1500 + 1000 * np, cy, cz) + p[7:]
            out += ["[[punto_mapa]]", "saga = %d" % si, "palabra = %s" % q(p[0]), "marca = %s" % list(p[1:]), ""]
    last = sg[-1]
    out += ["[[capitulo]]", "es = %s" % lst(last.get("cierre_es", ["Fin."])), "en = %s" % lst(last.get("cierre_en", ["The end."])), ""]
    return "\n".join(_con_idiomas(out, _extra(h_en, [x.get("nombre_en", "Saga") for x in sg]))), plan


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
    for a0, b0 in zip(caps, caps[1:]):   # dos combates seguidos no repiten frase del heroe
        if "dialogo_es" in b0 and "dialogo_es" in a0:
            assert a0["dialogo_es"][1] != b0["dialogo_es"][1] and a0["victoria_es"] != b0["victoria_es"], a0["combate"]
    txt2, _ = generar(d, heroe="Zarkon")
    assert "Zarkon" in txt2 and "{h}" not in txt2 and "{s}" not in txt2
    assert sum(1 for p in cfg["punto_mapa"] if p["marca"][1] == ESFERA) == 7, "una Esfera por saga, 7 en total"
    assert {p["saga"] for p in cfg["punto_mapa"]} == set(range(len(d["saga"])))
    assert all(c["rival"] in RETRATO for c in caps[:-1]), [c["rival"] for c in caps[:-1] if c["rival"] not in RETRATO]
    for c in caps[:-1]:   # las frases de plantilla salen traducidas en fr/de/it (no iguales al ingles)
        for lg in ("fr", "de", "it"):
            assert c["victoria_" + lg][0] != c["victoria_en"][0] and c["mapa_" + lg] != c["mapa_en"], (c["combate"], lg)
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
    juego = os.path.dirname(os.path.abspath(mods))
    # el mismo orden que heroe.cpp: el del usuario, y luego res\heroe junto al juego
    sagas_toml = sagas_toml or next((p for p in (os.path.join(juego, "user_data", "dbz3", "heroe", "sagas.toml"),
                                                 os.path.join(juego, "res", "heroe", "sagas.toml"),
                                                 os.path.join(RAIZ, "res", "heroe", "sagas.toml")) if os.path.isfile(p)), None)
    if not sagas_toml:
        print("heroe_sagas_a_du: FALTA res\\heroe\\sagas.toml en %s: sin el no hay historia del Heroe" % juego)
    viejo = os.path.join(mods, "historia_heroe")   # C3: la historia de una sola casilla (36) choca con la nueva: al respaldo
    if os.path.isdir(viejo):
        resp = os.path.join(os.path.dirname(os.path.abspath(mods)), "mods_respaldo")
        os.makedirs(resp, exist_ok=True)
        shutil.move(viejo, os.path.join(resp, "historia_heroe_c3_%d" % int(time.time())))
    mujer = int(cuerpo) in MUJERES
    rc = 0
    # primero el rotulo, que tarda segundos: si lo de despues falla o lo cortan, el nombre ya esta
    if casilla and nombre:   # rotulo de nombre de la pantalla de subida de nivel (cac/nombre_levelup.py)
        cmd = [sys.executable, "-X", "utf8", os.path.join(HERE, "cac", "nombre_levelup.py"), "--mods", mods] +             (["--us", us] if us else []) + ["%d=%s" % (casilla, nombre)]
        rc = subprocess.run(cmd).returncode
    if not sagas_toml:
        return 1
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
        if propia and casilla is not None and os.path.isfile(base):   # el retrato del modelo, como los ports; la plantilla solo si falla
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
        extra = "%s|%s|%s" % (os.path.basename(pl or "-"), os.path.getmtime(pl) if pl else 0, nombre if propia else "")
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
               "--sagas", sagas_toml] + (["--us", us] if us else []) + (["--plantilla", pl] if pl else []) + (["--heroe", nombre] if propia and nombre else [])
        r = subprocess.run(cmd).returncode
        if r == 0:
            with open(marca, "w", encoding="utf-8") as f:
                f.write(actual + "\n")
        rc = r or rc
    return rc


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--sagas", default=os.path.join(RAIZ, "res", "heroe", "sagas.toml"))
    p.add_argument("--salida", help="solo con --casilla (por defecto campanas/historia_heroe_<casilla>.toml)")
    p.add_argument("--casilla", type=int, default=0, help="una sola casilla (por defecto las 3: 35, 36 y 37)")
    p.add_argument("--id", type=int, default=63, help="ID de la celda del heroe (roster: heroe_propio)")
    p.add_argument("--retrato", type=int, default=1416, help="retrato de dialogo del heroe (data_cmn)")
    p.add_argument("--plantilla", help="silueta (png) del retrato de dialogo del heroe; sin ella el retrato sale vacio")
    p.add_argument("--heroe", help="nombre del heroe en los dialogos")
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
        txt, plan = generar(sagas, cas, a.id, a.retrato, heroe=a.heroe)
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

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
import json
import os
import re
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
        for c in (os.path.join(aqui, "..", "res", "heroe", "sagas_i18n.toml"), os.path.join(aqui, "..", "heroe", "sagas_i18n.toml")):   # R23 HER-11: frases propias del Heroe
            if os.path.isfile(c):
                try:
                    with open(c, "rb") as f:
                        for k, v in tomllib.load(f).items():
                            _TRAD.setdefault(k, v)
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
        for lg in ("fr", "de", "it", "pt", "ja"):
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


# ---- Rutas (ajuste dbz3_heroe_rutas, apagado de serie; DISENO 12/11 §1, maqueta 1 "cruce") ----
# Un tramo = una [[saga]] de sagas.toml: las 8 de siempre + las `variante = true`. Indice de tramo = su orden en el archivo.
# La regla es la de src/heroe_rutas.h (la misma en C++; tools/test_heroe_rutas.cpp lo comprueba con la tabla de abajo).
ENT_CRUCE, ENT_OPC = 1000, 1200   # entradas de mentira de las marcas de camino (1000 + 2*tramo) y del opcional "?" (1200):
#                                   campaigns.cpp las traduce al combate de verdad al tocarlas (codigo 5400 + casilla)
MAESTRO_MARCA = ("goku", "vegeta", "piccolo", "krilin", "freezer", "cell")   # marca maestro_<x> por indice de kEstilos
NOMBRES_RESPALDO = {21: ("Recoome", "Recoome"), 38: ("Cooler", "Cooler"),
                    42: ("Saibaman", "Saibaman"), 43: ("Cell Jr.", "Cell Jr.")}


def texto_rival(c, ids_mod):
    """El dialogo debe nombrar al rival instalado o a su sustituto nativo real."""
    if c.get("rival_mod") and c["rival_mod"] not in (ids_mod or {}):
        es, en = NOMBRES_RESPALDO.get(int(c["rival"]), ("Un rival", "An opponent"))
        return es + " sale al paso.", en + " blocks the way."
    return c.get("es", "..."), c.get("en", "...")
MARCA_OPC_NAMEK = (2, 3384, 45, 6021)    # marcador "?" del opcional en Namek (zona, x, y, z: una marca de Goku sin usar aqui)
MARCA_OPC_TIERRA = (33, 3726, 70, 3151)  # en la Tierra (la del opcional 2402 del juego)


def canon(sagas):
    return [s for s in sagas["saga"] if not s.get("variante")]


def tramos(sagas):
    """Todos los tramos en orden de indice: los de siempre primero (sus indices no cambian) y luego las variantes."""
    return canon(sagas) + [s for s in sagas["saga"] if s.get("variante")]


def cumple(condicion, marcas):
    alts = [a for a in (condicion or "").split("|") if a]
    return not alts or any((a[1:] not in marcas) if a[0] == "!" else (a in marcas) for a in alts)


def ruta_siguiente(ts, desde, marcas):
    """(tramo, [cruce]) del primer tramo jugable desde la etapa `desde`; (-1, []) = final. Igual que rutas::Elegir."""
    etapas = [int(t.get("etapa", -1)) for t in ts]
    for e in range(max(desde, 0), max(etapas) + 1):
        c = [i for i, t in enumerate(ts) if etapas[i] == e and cumple(t.get("condicion"), marcas)]
        if not c:
            continue
        cr = [i for i in c if ts[i].get("cruce_es")]
        if len(cr) >= 2:
            return cr[0], cr
        mejor = c[0]
        for i in c:
            if int(ts[i].get("prioridad", 0)) > int(ts[mejor].get("prioridad", 0)):
                mejor = i
        return mejor, []
    return -1, []


def mezcla(semilla, tramo):
    x = (semilla ^ (tramo * 2654435761)) & 0xFFFFFFFF
    x ^= x >> 16
    x = (x * 0x45D9F3B) & 0xFFFFFFFF
    return x ^ (x >> 16)


def opcional_de(t, semilla, tramo):
    n = len(t.get("opcional", []))
    return mezcla(semilla, tramo) % n if n else -1


def bcode_de(t, casilla):
    """Codigo de combate del tramo t (0-based): 5500/5600/5700 los 3 primeros (bloques del juego), 5400 el resto."""
    return 5500 + 100 * t + casilla if t < 3 else 5400 + casilla


def grupos_cruce(ts):
    """{tramo de la intro: [tramos del cruce]}: los de la misma etapa con cruce y la MISMA condicion salen juntos."""
    g = {}
    for i, t in enumerate(ts):
        if t.get("cruce_es"):
            g.setdefault((int(t.get("etapa", -1)), t.get("condicion", "")), []).append(i)
    return {v[0]: v for v in g.values() if len(v) >= 2}


def palabra_cruce(ts, t, casilla):
    """Palabra de la marca de camino del tramo t: la del primero del cruce es su combate de verdad; las demas, de mentira."""
    primero = next((a for a, v in grupos_cruce(ts).items() if t in v), t)
    return (bcode_de(t, casilla) << 16) if t == primero else ((5400 + casilla) << 16) | (ENT_CRUCE + 2 * t)


def rutas_extra(sg, casilla):
    ext = {"quitar": {}, "planeta_sig": {}}
    for a, v in grupos_cruce(sg).items():
        palabras = [palabra_cruce(sg, t, casilla) for t in v]
        for t in v:   # el combate elegido borra las marcas de todos los caminos (la suya la borra campana.py)
            ext["quitar"][t] = [w for w in palabras if w != (bcode_de(t, casilla) << 16)]
    etapas = sorted({int(t.get("etapa", -1)) for t in sg if int(t.get("etapa", -1)) >= 0})
    # R23 HER-13: cada tramo se cierra en SU planeta (sin op 47). Con el cambio de planeta en la escena de cierre, la carga del
    # otro mapa no pasa por 821C6A04 (la que lanza la escena guardada +4564) y la intro siguiente no salia nunca: la partida
    # completa se quedaba en Namek tras la etapa 2. El planeta del tramo siguiente lo pone campaigns.cpp al lanzar su intro.
    for i, t in enumerate(sg):
        if int(t.get("etapa", -1)) >= 0:
            ext["planeta_sig"][i] = int(t.get("planeta", 1))
    return ext


def rutas_marcas(sg, casilla, ext, rival_de, retrato_heroe, ids_mod=None):
    """[[marca_extra]] (caminos del cruce y "?" del opcional, en la intro de su tramo) y los combates opcionales ocultos
    ([[maestro_marca]] con oculta: van detras del maestro del tramo; campaigns.cpp lanza el que sorteo la semilla)."""
    out = []
    for a, v in grupos_cruce(sg).items():
        zona, x, y, z, esc = ext["primera"][a]
        for j, t in enumerate(v[1:], 1):
            out += ["[[marca_extra]]", "saga = %d" % a, "palabra = %d" % palabra_cruce(sg, t, casilla),
                    "marca = [0, 1, %d, %d, %d, %d, %d]" % (zona, x + 1500 * j, y, z, esc), ""]
    opc = ((5400 + casilla) << 16) | ENT_OPC
    for i, t in enumerate(sg):
        if not t.get("opcional"):
            continue
        tierra = int(t.get("planeta", 1)) == 0
        zona, x, y, z = MARCA_OPC_TIERRA if tierra else MARCA_OPC_NAMEK
        out += ["[[marca_extra]]", "saga = %d" % i, "palabra = %d" % opc, "marca = [0, 577, %d, %d, %d, %d, 1000]" % (zona, x, y, z), ""]
        for o in t["opcional"]:
            r = rival_de(o)
            es, en = texto_rival(o, ids_mod)
            out += ["[[maestro_marca]]", "saga = %d" % i, "oculta = true", "quitar = [%d]" % opc, "rival = %d" % r,
                    "escenario = %d" % int(o.get("escenario", 7)), "plantilla = %d" % (717 if tierra else 709),
                    "fondo_dialogo = %d" % (1549 if tierra else 1536), "retratos = [%d, %d]" % (RETRATO.get(r, 1416), retrato_heroe),
                    "marca = [577, %d, %d, %d, %d, 1000]" % (zona, x, y, z),
                    "dialogo_es = %s" % lst([es, "Apartate."]), "dialogo_en = %s" % lst([en, "Out of my way."]),
                    "victoria_es = %s" % lst(["Maldicion...", "Un rival menos."]), "victoria_en = %s" % lst(["Damn it...", "One less rival."]),
                    "mapa_es = %s" % lst(["Sigamos."]), "mapa_en = %s" % lst(["Onwards."]), ""]
    return out


def ids_de_mods(mods):
    """{mod: ID} de los luchadores de mods montados (mods/_roster/roster.toml: '# Nombre (mod X)' + 'id = N')."""
    ids, mod = {}, None
    try:
        with open(os.path.join(mods, "_roster", "roster.toml"), encoding="utf-8") as f:
            for l in f:
                l = l.strip()
                if l.startswith("#") and "(mod " in l:
                    mod = l.split("(mod ", 1)[1].split(")")[0].strip()
                elif mod and l.startswith("id = "):
                    ids[mod] = int(l[5:].split()[0])
                    mod = None
    except (OSError, ValueError):
        pass
    return ids


def rutas_activas(juego=None):
    """Ajuste dbz3_heroe_rutas (dbz3_user.toml junto al juego); HEROE_RUTAS=1/0 lo fuerza en pruebas."""
    e = os.environ.get("HEROE_RUTAS")
    if e not in (None, ""):
        return e.strip().lower() not in ("0", "false", "off", "no")
    try:
        with open(os.path.join(juego or "", "dbz3_user.toml"), "rb") as f:
            return bool(tomllib.load(f).get("dbz3_heroe_rutas", False))
    except Exception:   # noqa: BLE001 (sin fichero o sin la clave: apagado)
        return False


def fondos_activos(juego=None):
    """Ajuste dbz3_heroe_fondos_historia (dbz3_user.toml junto al juego); HEROE_FONDOS=1/0 lo fuerza en pruebas."""
    e = os.environ.get("HEROE_FONDOS")
    if e not in (None, ""):
        return e.strip().lower() not in ("0", "false", "off", "no")
    try:
        with open(os.path.join(juego or "", "dbz3_user.toml"), "rb") as f:
            return bool(tomllib.load(f).get("dbz3_heroe_fondos_historia", True))   # R23: encendido de serie, como el ajuste de campaigns.cpp
    except Exception:   # noqa: BLE001 (sin fichero: el ajuste va encendido de serie)
        return True


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
    "This ends with me.": ("Ça se termine avec moi.", "Das endet mit mir.", "Finisce con me."),
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
    "There's still a long way to go.": ("Il reste encore un long chemin.", "Es ist noch ein weiter Weg.", "C'è ancora tanta strada da fare."),
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
    # R23: frases de cada rival al caer (CAE_POR_RIVAL)
    'Impossible! A fighter of your level...?': ('Impossible ! Un guerrier de ton niveau... ?', 'Unmöglich! Ein Kämpfer deines Niveaus...?', 'Impossibile! Un guerriero del tuo livello...?'),
    'Well done. Keep it up.': ('Bien joué. Continue comme ça.', 'Gut gemacht. Weiter so.', 'Ben fatto. Continua così.'),
    'Your technique is flawless.': ('Ta technique est impeccable.', 'Deine Technik ist makellos.', 'La tua tecnica è impeccabile.'),
    'Curse you... worm!': ('Maudit... vermisseau !', 'Verfluchter... Wurm!', 'Maledetto... verme!'),
    'The Ginyu Force... defeated!': ('Le commando Ginyu... vaincu !', 'Das Ginyu-Sonderkommando... besiegt!', 'La Squadra Ginew... sconfitta!'),
    'My pose... ruined!': ('Ma pose... gâchée !', 'Meine Pose... ruiniert!', 'La mia posa... rovinata!'),
    "You'll pay for this humiliation!": ('Tu paieras pour cette humiliation !', 'Für diese Demütigung wirst du bezahlen!', 'Pagherai per questa umiliazione!'),
    "Well, didn't see that coming.": ("Tiens, je ne m'y attendais pas.", 'Na, damit hab ich nicht gerechnet.', "Beh, non me l'aspettavo."),
    'Tch. You win this time.': ('Tss. Tu gagnes cette fois.', 'Tss. Diesmal gewinnst du.', 'Tsk. Stavolta hai vinto tu.'),
    'My perfect body...!': ('Mon corps parfait... !', 'Mein perfekter Körper...!', 'Il mio corpo perfetto...!'),
    "You're more annoying than my brother.": ('Tu es plus agaçant que mon frère.', 'Du nervst mehr als mein Bruder.', 'Sei più fastidioso di mio fratello.'),
    'The Dragon Balls... are leaving me!': ("Les Dragon Balls... m'abandonnent !", 'Die Dragonballs... verlassen mich!', 'Le Sfere... mi abbandonano!'),
    'Kiii...!': ('Kiii... !', 'Kiii... autsch!', 'Kiii... ahi!'),
    'Kii! Papa will be angry!': ('Kii ! Papa va être fâché !', 'Kii! Papa wird sauer sein!', 'Kii! Papà si arrabbierà!'),
    "Hey, our fusion's wearing off!": ('Hé, notre fusion se dissipe !', 'He, unsere Fusion lässt nach!', 'Ehi, la nostra fusione sta finendo!'),
    "You're ready for Omega.": ('Tu es prêt pour Omega.', 'Du bist bereit für Omega.', 'Sei pronto per Omega.'),
    "You've earned the Dragon Ball.": ('Tu as mérité la Dragon Ball.', 'Du hast dir den Dragonball verdient.', 'Ti sei guadagnato la Sfera.'),
}


def _ptja():
    """pt/ja de TRAD_PLANTILLAS (heroe_plantillas_pt_ja.json, misma clave inglesa)."""
    try:   # sin el archivo (copia vieja del Kit) pt/ja quedan en ingles, no se rompe el montaje
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "heroe_plantillas_pt_ja.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {"pt": {}, "ja": {}}


def _extra(h_en, nombres):
    """{frase inglesa ya con nombres: {lg: texto}} para el heroe y las sagas de esta generacion."""
    ex = {}
    for k, v in TRAD_PLANTILLAS.items():
        for n in (nombres if ("{n}" in k or "{s}" in k) else [""]):
            e = k.replace("{h}", h_en).replace("{s}", n or "saga").replace("{n}", n)
            pj = _ptja()
            ex[e] = {lg: t.replace("{h}", h_en).replace("{s}", n or "saga").replace("{n}", n) for lg, t in
                     list(zip(("fr", "de", "it"), v)) + [(lg, pj[lg][k]) for lg in ("pt", "ja") if k in pj.get(lg, {})]}
    return ex


# Frases del heroe y de los rivales (sin repetir entre combates seguidos): {h} = su nombre, {s} = la saga. Los rivales abren con su
# frase de sagas.toml (es/en, jefe_es/jefe_en) y el heroe contesta.
RESPUESTA = [("Me llamo {h}. Recuérdalo.", "The name's {h}. Remember it."), ("Voy a por ti.", "I'm coming for you."), ("Apártate, tengo prisa.", "Step aside, I'm in a hurry."),
             ("{h} no retrocede.", "{h} doesn't back down."), ("Veamos de qué eres capaz.", "Let's see what you've got."),
             ("Aquí se acaba tu camino.", "Your road ends here."), ("Hoy soy más fuerte que ayer.", "I'm stronger than I was yesterday."),
             ("No has visto nada todavía.", "You haven't seen anything yet."),
             ("Sal de mi camino.", "Out of my way."), ("Esto acaba conmigo.", "This ends with me."),
             ("Por las Esferas, adelante.", "For the Dragon Balls, let's go.")]
RIVAL_CAE = [("Impresionante...", "Impressive..."), ("¿Quién... eres?", "Who... are you?"), ("No puede ser...", "This can't be..."),
             ("Tu fuerza es de verdad.", "Your strength is real."), ("Me has vencido, {h}.", "You beat me, {h}."),
             ("Esto no ha terminado.", "This isn't over."), ("Te recordaré, {h}.", "I'll remember you, {h}."),
             ("Eres más fuerte de lo que pareces.", "You're stronger than you look."), ("Maldición...", "Damn it...")]
# R23 (usuario): la frase del rival al caer, en su papel (antes salian al azar: Vegito preguntaba "¿Quien... eres?" y varios
# decian "No puede ser..."). Por ID nativo; con un villano revivido de mod (otro personaje) se usa RIVAL_CAE.
CAE_POR_RIVAL = {
    7: ('¡Imposible! ¿Un guerrero de tu nivel...?', 'Impossible! A fighter of your level...?'),
    11: ('Bien hecho. Sigue así.', 'Well done. Keep it up.'),
    12: ('Tu técnica es impecable.', 'Your technique is flawless.'),
    18: ('¡Maldito... gusano!', 'Curse you... worm!'),
    20: ('¡Las Fuerzas Especiales... derrotadas!', 'The Ginyu Force... defeated!'),
    21: ('¡Mi pose... arruinada!', 'My pose... ruined!'),
    27: ('¡Pagarás por esta humillación!', "You'll pay for this humiliation!"),
    29: ('Vaya, eso no me lo esperaba.', "Well, didn't see that coming."),
    30: ('Tch. Esta vez ganas tú.', 'Tch. You win this time.'),
    33: ('¡Mi cuerpo perfecto...!', 'My perfect body...!'),
    38: ('Eres más molesto que mi hermano.', "You're more annoying than my brother."),
    41: ('¡Las Esferas... me abandonan!', 'The Dragon Balls... are leaving me!'),
    42: ('¡Kiii...!', 'Kiii...!'),
    43: ('¡Kii! ¡Papá se enfadará!', 'Kii! Papa will be angry!'),
    64: ('¡Eh, que se nos acaba la fusión!', "Hey, our fusion's wearing off!"),
    70: ('Estás listo para Omega.', "You're ready for Omega."),
    72: ('Te has ganado la Esfera.', "You've earned the Dragon Ball."),
}
HEROE_GANA = [("Sigo mi camino.", "I'll be on my way."), ("Buen combate.", "Good fight."), ("Aún queda mucho camino.", "There's still a long way to go."),
              ("Eso ha sido solo el calentamiento.", "That was just the warm-up."), ("Entrena más y vuelve.", "Train harder and come back."),
              ("Otro paso hacia las Esferas.", "One step closer to the Dragon Balls."), ("{h} sigue en pie.", "{h} is still standing."),
              ("Tenía que hacerse.", "It had to be done.")]
VUELTA_MAPA = [("Sigamos.", "Onwards."), ("¿Quién será el siguiente?", "Who's next?"), ("El radar marca algo por allí.", "The radar shows something over there."),
               ("Aún noto energía cerca.", "I can still sense energy nearby."), ("No hay tiempo que perder.", "No time to lose."),
               ("Esto no acaba aquí.", "This doesn't end here."), ("Voy para allá.", "On my way.")]


def _idioma_img():
    """HER-02: idioma del juego (id Xbox: 1 en, 5 es, 4 fr...) para el rotulo y el pie PINTADOS en la imagen de la narracion.
    Lo fija montar_si_hace_falta con HEROE_IDIOMA; sin el, ingles."""
    try:
        return int(os.environ.get("HEROE_IDIOMA", "1"))
    except ValueError:
        return 1


def idioma_juego(juego):
    """user_language de <juego>/dbz3_user.toml (1 si no esta)."""
    try:
        m = re.search(r"^user_language\s*=\s*(\d+)", open(os.path.join(juego, "dbz3_user.toml"), encoding="utf-8").read(), re.M)
        return int(m.group(1)) if m else 1
    except OSError:
        return 1


def generar(sagas, casilla=36, heroe_id=63, retrato_heroe=1416, nombre=None, n_sagas=MAX_SAGAS, heroe=None, rutas=False, ids_mod=None):
    """rutas: con el ajuste dbz3_heroe_rutas se generan TODOS los tramos (variantes incluidas) y los cruces/opcionales del
    mapa (ver rutas_extra). ids_mod: {mod: ID de luchador} de los villanos revividos montados (rival_mod)."""
    nombre = nombre or "historia_heroe_%d" % casilla
    h_es, h_en = heroe or "Héroe", heroe or "Hero"

    def frase(pool, i, s):
        """(es, en) de la frase i del grupo, con el nombre del heroe y el de su saga."""
        es, en = pool[i % len(pool)]
        return (es.replace("{h}", h_es).replace("{s}", s.get("nombre_es", "saga")),
                en.replace("{h}", h_en).replace("{s}", s.get("nombre_en", "saga")))
    """sagas: dict de res/heroe/sagas.toml. Devuelve (texto del .toml de campana.py, [(saga, combate, rival)])."""
    sg = tramos(sagas) if rutas else canon(sagas)[:n_sagas]
    FONDOS = fondos_activos()
    rival_de = lambda c: int((ids_mod or {}).get(c.get("rival_mod"), c["rival"]))   # villano revivido montado, o su respaldo nativo
    out = ["# GENERADO por heroe_sagas_a_du.py desde res/heroe/sagas.toml (no editar a mano: se rehace).",
           "[campana]", 'nombre = "Modo Heroe en Dragon Universe"', "mod = %s" % q(nombre),
           'descripcion = "Las sagas del Modo Heroe como historia de Dragon Universe: entra por la celda del Heroe."', "",
           "[rueda]", "casillas_virtuales = [%d]" % casilla, "",
           "[casilla_luchador]", "%d = %d" % (casilla, heroe_id), "",
           "[historia]", "codigo = %d" % (5000 + 100 * SAGA0 + casilla), 'intro_de = "102.0"', "animacion_mapa = 0",
           "altura_mapa = 600", "fondo = 1411", "fondo_dialogo = 1536", *(["entradas_propias = 160"] if rutas else []),
           *([] if os.environ.get("HEROE_GUARDAR") else ["sin_guardar = true"]),   # C8: HEROE_GUARDAR=1 = cierre nativo (guardar+titulo+reanudar). # el cierre de saga no pasa por el guardado nativo (vuelve al titulo): sigue con la intro (el progreso va en la ficha)
           'final = { contenedor = 1728, codigo = 1, entrada = 39, fondo = "heroe" }',
           "eventos_mapa = { codigos = [2000, 2100, 2200, 2300], retrato = %d }" % retrato_heroe,
           "combates = { codigo = %d, contenedor = 2132, plantilla = 709 }" % (5400 + 100 * SAGA0 + casilla),
           "dialogos = { codigo = %d, contenedor = 1659 }" % (5800 + 100 * SAGA0 + casilla), "",
           '[[panel_limpio]]', "cmn = 1411", "filas = 17", ""]
    plan, k = [], 0
    ext = rutas_extra(sg, casilla) if rutas else {}
    for si, s in enumerate(sg):
        for ci, c in enumerate(s["combate"]):
            x, y, z = MARCAS[k % len(MARCAS)]
            tierra = int(s.get("planeta", 1)) == 0   # H3: sagas en la Tierra (planeta = 0 en sagas.toml)
            zona = 2
            if tierra:
                zona, x, y, z = MARCAS_TIERRA[ci % len(MARCAS_TIERRA)]
            if os.environ.get("HEROE_MARCA_FIJA"):   # C7: prueba (todas las marcas en el mismo punto)
                x, y, z = [int(v) for v in os.environ["HEROE_MARCA_FIJA"].split(",")]
            rival = rival_de(c)
            es, en = texto_rival(c, ids_mod)
            r_es, r_en = frase(RESPUESTA, k, s)
            rc_es, rc_en = CAE_POR_RIVAL[rival] if rival in CAE_POR_RIVAL and not c.get("rival_mod") else frase(RIVAL_CAE, k, s)
            hg_es, hg_en = frase(HEROE_GANA, k, s)
            vm_es, vm_en = frase(VUELTA_MAPA, k, s)
            sin_mod = c.get("rival_mod") and c["rival_mod"] not in (ids_mod or {})
            boss_es, boss_en = (None, None) if sin_mod else (c.get("jefe_es"), c.get("jefe_en"))
            out += ["[[capitulo]]", "combate = %d" % (-1 if s.get("variante") else BPC0 + k), "rival = %d" % rival,   # -1: #BPC nuevo (campana.py)
                    *(["quitar = %s" % ext["quitar"][si]] if ci == 0 and si in ext.get("quitar", {}) else []),   # rutas: el cruce elegido borra los otros caminos
                    *(["planeta_siguiente = %d" % ext["planeta_sig"][si]] if ci + 1 == len(s["combate"]) and si in ext.get("planeta_sig", {}) else []),
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
                    if FONDOS and prev.get("fondo_cierre"):
                        out.append("cierre_imagen = %s" % q("heroe_fondos/%s.png" % prev["fondo_cierre"]))
                        out.append("cierre_imagen_titulo = %s" % q(("Fin de la " + prev.get("nombre_es", "Saga").lower()) if _idioma_img() == 5 else ("End of the " + prev.get("nombre_en", "Saga"))))
                    sin_cierre = rutas and si >= 4   # rutas: en las intros virtuales el tramo anterior de la lista no es el jugado
                    out += ["cierre_es = %s" % lst([] if sin_cierre else prev.get("cierre_es", ["..."])),
                            "cierre_en = %s" % lst([] if sin_cierre else prev.get("cierre_en", ["..."]))]
                if FONDOS and s.get("fondo"):   # fondos de historia (apagado de serie): imagen grande de la saga; sin el .png el juego usa el panel de siempre
                    nativo = s.get("fondo_nativo") if os.environ.get("HEROE_FONDOS_B3") == "1" else None
                    raiz_nativa = os.environ.get("HEROE_FONDOS_B3_DIR", "")
                    candidato = os.path.join(raiz_nativa, nativo + ".png") if nativo and raiz_nativa else ""
                    imagen = candidato if os.path.isfile(candidato) else "heroe_fondos/%s.png" % s["fondo"]
                    out.append("imagen = %s" % q(imagen.replace("\\", "/")))
                    out.append("imagen_titulo = %s" % q(s.get("nombre_es", "Saga") if _idioma_img() == 5 else s.get("nombre_en", "Saga")))
                out += ["salida = [5700, 100, 1700]" if tierra else "salida = [%d, %d, 600]" % (x, z + int(os.environ.get("HEROE_SALIDA_DIST", "500"))),
                        "es = %s" % lst(s.get("intro_es", ["..."])), "en = %s" % lst(s.get("intro_en", ["..."])),
                        "llegada_es = %s" % lst(["%s, %s." % (s.get("nombre_es", "Saga"), "aquí empieza" if si == 0 else "allá voy")]),
                        "llegada_en = %s" % lst(["%s: %s." % (s.get("nombre_en", "Saga"), "here it begins" if si == 0 else "here I come")])]
            out.append("")
            plan.append((si, ci, rival))
            if ci == 0:
                ext.setdefault("primera", {})[si] = (zona, x, y, z, 1500 if tierra else 1000)
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
    if rutas:
        out += rutas_marcas(sg, casilla, ext, rival_de, retrato_heroe, ids_mod)
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
    last = canon(sagas)[-1] if rutas else sg[-1]   # rutas: el texto final es el de siempre (el ultimo de la lista es una variante)
    out += ["[[capitulo]]", "es = %s" % lst(last.get("cierre_es", ["Fin."])), "en = %s" % lst(last.get("cierre_en", ["The end."])), ""]
    return "\n".join(_con_idiomas(out, _extra(h_en, [x.get("nombre_en", "Saga") for x in sg]))), plan


def comprobar():
    todo = leer(os.path.join(RAIZ, "res", "heroe", "sagas.toml"))
    d = dict(todo, saga=canon(todo))   # apagado: solo las sagas de siempre (las variantes son de las rutas)
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
    assert "Zarkon" in txt2 and "{h}" not in txt2 and "{s}" not in txt2 and "{n}" not in txt2
    assert sum(1 for p in cfg["punto_mapa"] if p["marca"][1] == ESFERA) == 7, "una Esfera por saga, 7 en total"
    assert {p["saga"] for p in cfg["punto_mapa"]} == set(range(len(d["saga"])))
    # HER06: cada saga lleva su objeto (marca 3) y su evento opcional (marca 1); las 7 primeras, su Esfera (marca 4).
    for si in range(len(d["saga"])):
        marcas = [p["marca"][0] for p in cfg["punto_mapa"] if p["saga"] == si]
        assert 3 in marcas, "saga %d sin objeto de mapa" % si
        assert 1 in marcas, "saga %d sin evento opcional" % si
        assert (4 in marcas) == (si < 7), "saga %d: Esfera de mas o de menos" % si
    for c in caps[:-1]:   # HER08: sin marcadores de plantilla en ninguna traduccion (ni pt/ja)
        for lg in ("fr", "de", "it", "pt", "ja"):
            for k in ("dialogo_", "victoria_", "mapa_"):
                for frase in c.get(k + lg, []):
                    assert not any(x in frase for x in ("{h}", "{s}", "{n}")), (c["combate"], k + lg, frase)
    assert all(c["rival"] in RETRATO for c in caps[:-1]), [c["rival"] for c in caps[:-1] if c["rival"] not in RETRATO]
    for c in caps[:-1]:   # las frases de plantilla salen traducidas en fr/de/it (no iguales al ingles)
        for lg in ("fr", "de", "it"):
            assert c["victoria_" + lg][0] != c["victoria_en"][0] and c["mapa_" + lg] != c["mapa_en"], (c["combate"], lg)
    # HER08: una marca de maestro por saga, con el icono dorado NATIVO (1601) y el rival
    # del maestro declarado en sagas.toml; asi el borde y el contraste son los del juego.
    marcas_m = {m["saga"]: m for m in cfg.get("maestro_marca", [])}
    con_maestro = {si for si, s in enumerate(d["saga"]) if 0 <= int(s.get("maestro", -1)) < len(MAESTROS)}
    assert set(marcas_m) == con_maestro, "sagas con marca de maestro: %s, esperadas: %s" % (
        sorted(marcas_m), sorted(con_maestro))
    for si, m in marcas_m.items():
        assert m["marca"][0] == 1601, "saga %d: la marca de maestro no usa el icono nativo 1601" % si
        assert m["rival"] == MAESTROS[int(d["saga"][si]["maestro"])], (si, m["rival"])
    # HER08: cada punto del mapa usa una escena nativa: las de eventos_mapa (2000/2100/2200/2300)
    # o las de los combates opcionales (2400-2403, historia de Goku).
    ev = {int(x) for x in cfg["historia"]["eventos_mapa"]["codigos"]} | {2400, 2401, 2402, 2403}
    for p in cfg["punto_mapa"]:
        assert int(str(p["palabra"]).split(".")[0]) in ev, (p["saga"], p["palabra"])
    # HER08: recompensa declarada = alcanzable: todo desbloqueo con `entreno` apunta a un
    # maestro que se puede entrenar en el mapa. Broly (40) queda declarado y pendiente.
    desb = leer(os.path.join(RAIZ, "res", "heroe", "desbloqueos.toml"))
    fuera = sorted({int(e["maestro"]) for e in desb.get("desbloqueo", [])
                    if "maestro" in e and "entreno" in e and int(e["maestro"]) not in MAESTROS})
    assert fuera == [40], "desbloqueo con maestro no entrenable: %s" % fuera
    print("heroe_sagas_a_du: autocomprobacion OK (%d combates, %d sagas)" % (len(plan), min(MAX_SAGAS, len(d["saga"]))))
    comprobar_rutas(todo)


def simular(ts, semilla, caminos, maestro):
    """Una partida guiada con la regla de rutas, como heroe.cpp: en cada cruce se toma el camino cuya marca esta en
    `caminos`; al entrar en el Acto 2 se fija la rama del maestro actual (marca maestro_<x>). Devuelve (tramos, marcas)."""
    marcas, jugados, etapa = set(), [], 0
    while True:
        if etapa >= 2 and not any(m.startswith("maestro_") for m in marcas):
            marcas.add("maestro_" + maestro)
        t, cruce = ruta_siguiente(ts, etapa, marcas)
        if t < 0:
            return jugados, marcas
        if cruce:
            t = next(c for c in cruce if ts[c].get("marca") in caminos)
        if ts[t].get("marca"):
            marcas.add(ts[t]["marca"])
        if ts[t].get("opcional") and ts[t].get("opcional_marca"):   # en la prueba siempre se hace el opcional
            marcas.add(ts[t]["opcional_marca"])
        jugados.append(t)
        etapa = int(ts[t]["etapa"]) + 1


def comprobar_rutas(d):
    """Test SIN JUEGO de las rutas (DISENO paso 2-4): tabla semilla + marcas -> tramo siguiente, los tramos variantes del
    generador y la misma regla en C++ (src/heroe_rutas.h, si hay compilador)."""
    ts = tramos(d)
    nombre = {s["nombre_es"]: i for i, s in enumerate(ts)}
    G, A, P, B, O, R, T, F3 = range(8)
    G2, A_CIU, A_CELL, A_CIU_CELL, VILL, JC = (nombre[n] for n in ("Saga de Ginyu: tras Vegeta", "Saga de los Androides: la Ciudad",
                                                                   "Saga de los Androides: Cell", "Saga de los Androides: la Ciudad y Cell",
                                                                   "Villanos revividos", "Juegos de Cell"))
    assert len(ts) == 14 and canon(d) == ts[:8], "8 tramos de siempre (indices 0-7) + 6 variantes"
    TABLA = [   # (etapa desde, marcas, tramo, cruce)
        (0, set(), G, [G, G2]),
        (1, {"camino_vegeta"}, A, [A, A_CIU]),
        (1, {"camino_nave"}, A_CELL, [A_CELL, A_CIU_CELL]),
        (2, {"maestro_piccolo"}, P, []), (2, {"maestro_krilin"}, P, []),
        (2, {"maestro_vegeta"}, B, []), (2, {"maestro_freezer"}, B, []),
        (2, {"maestro_goku"}, VILL, []), (2, {"maestro_cell"}, VILL, []),
        (2, set(), O, []),   # sin rama no hay Acto 2 (heroe.cpp siempre pone una)
        (3, set(), O, []), (4, set(), R, []),
        (5, {"maestro_vegeta"}, F3, []), (5, {"maestro_cell"}, JC, []), (5, {"maestro_goku"}, JC, []),
        (5, {"deseo_revivir", "maestro_cell"}, T, []),
        (6, set(), -1, []),
    ]
    for etapa, marcas, tramo, cruce in TABLA:
        assert ruta_siguiente(ts, etapa, marcas) == (tramo, cruce), (etapa, marcas, ruta_siguiente(ts, etapa, marcas))
    assert cumple("a|!b", {"b"}) is False and cumple("a|!b", set()) is True and cumple("", set()) is True
    # dos partidas guiadas de ejemplo (DISENO §1.6): caminos distintos
    pa, ma = simular(ts, 1, {"camino_nave", "camino_lab"}, "cell")
    pb, mb = simular(ts, 2, {"camino_vegeta", "camino_ciudad"}, "vegeta")
    assert pa == [G, A_CELL, VILL, O, R, JC], pa
    assert pb == [G2, A_CIU, B, O, R, F3], pb
    assert {"camino_nave", "camino_lab", "opc_namek", "maestro_cell"} <= ma and {"camino_vegeta", "camino_ciudad", "maestro_vegeta"} <= mb
    # semilla -> opcional sorteado: determinista y reparte los dos rivales
    MEZCLA = [(0, 0, mezcla(0, 0)), (1, 0, mezcla(1, 0)), (12345, 8, mezcla(12345, 8)), (0xFFFFFFFF, 13, mezcla(0xFFFFFFFF, 13))]
    assert mezcla(12345, 8) == mezcla(12345, 8) and {opcional_de(ts[G], s, G) for s in range(1, 30)} == {0, 1}
    # el generador con rutas: los 14 tramos, combates nuevos (-1) solo en las variantes, cruces y opcionales
    txt, plan = generar(d, rutas=True, casilla=36)
    cfg = tomllib.loads(txt)
    caps = [c for c in cfg["capitulo"] if "combate" in c]
    assert len({si for si, _, _ in plan}) == 14 and len(caps) == sum(len(t["combate"]) for t in ts)
    assert [c["combate"] for c in caps[:27]] == list(range(BPC0, BPC0 + 27)) and all(c["combate"] == -1 for c in caps[27:])
    assert sum(1 for c in cfg["capitulo"] if c.get("saga")) == 13 and cfg["historia"]["entradas_propias"] >= 128
    extra = cfg.get("marca_extra", [])
    camino = [m for m in extra if m["palabra"] & 0xFFFF == ENT_CRUCE + 2 * G2]
    assert len(camino) == 1 and camino[0]["saga"] == G and camino[0]["palabra"] >> 16 == 5436, "el camino B sale en la intro de Ginyu"
    assert {m["saga"] for m in extra if m["palabra"] & 0xFFFF == ENT_OPC} == {i for i, t in enumerate(ts) if t.get("opcional")}
    ocultas = [m for m in cfg["maestro_marca"] if m.get("oculta")]
    assert len(ocultas) == sum(len(t.get("opcional", [])) for t in ts) and all(m["quitar"] == [(5436 << 16) | ENT_OPC] for m in ocultas)
    primeros = {}
    si = -1
    for c in cfg["capitulo"]:
        if "combate" in c and (c.get("saga") or si < 0):
            si += 1
            primeros[si] = c
    assert primeros[G2]["quitar"] == [5536 << 16, (5436 << 16) | (ENT_CRUCE + 2 * G2)] and primeros[G]["quitar"] == [(5436 << 16) | (ENT_CRUCE + 2 * G2)], "el camino elegido borra el otro"
    ult = {}
    si = -1
    for c in cfg["capitulo"]:
        if "combate" in c:
            si += 1 if (c.get("saga") or si < 0) else 0
            ult[si] = c
    assert all(ult[k]["planeta_siguiente"] == int(ts[k].get("planeta", 1)) for k in (G2, A_CIU, VILL, JC)), "cada tramo se cierra en su planeta (HER-13)"
    assert generar(d)[0] == generar(dict(d, saga=canon(d)))[0], "apagado: las variantes no cambian nada"
    rutas_cpp(ts, TABLA, MEZCLA)
    print("heroe_sagas_a_du: rutas OK (14 tramos, %d casos, partidas %s / %s)" % (len(TABLA), pa, pb))


def rutas_cpp(ts, tabla, mezclas):
    """La misma tabla contra src/heroe_rutas.h (tools/test_heroe_rutas.cpp). Sin compilador de C++ se avisa y se sigue."""
    import subprocess  # noqa: PLC0415
    import tempfile  # noqa: PLC0415
    sys.path.insert(0, os.path.join(RAIZ, "tools"))
    try:
        from test_heroe_formas import compilador  # noqa: PLC0415
        cc = compilador()
    except SystemExit:
        print("heroe_sagas_a_du: sin compilador de C++: la regla de C++ no se comprueba aqui")
        return
    tmp = tempfile.mkdtemp(prefix="rutas_", dir=os.environ.get("TEMP"))
    v = lambda s: s if s else "-"
    with open(os.path.join(tmp, "rutas.tsv"), "w", encoding="utf-8") as f:
        for t in ts:
            f.write("T %d %s %s %d %d\n" % (int(t.get("etapa", -1)), v(t.get("condicion", "")), v("x" if t.get("cruce_es") else ""),
                                            int(t.get("prioridad", 0)), len(t.get("opcional", []))))
        for etapa, marcas, tramo, cruce in tabla:
            f.write("C %d %s %d %s\n" % (etapa, v(",".join(sorted(marcas))), tramo, v(",".join(map(str, cruce)))))
        for s, t, x in mezclas:
            f.write("M %d %d %d\n" % (s, t, x))
    exe = os.path.join(tmp, "test_rutas.exe")
    r = subprocess.run([cc, "-std=c++20", "-O0", "-o", exe, os.path.join(RAIZ, "tools", "test_heroe_rutas.cpp")], capture_output=True, text=True)
    assert r.returncode == 0, "test_heroe_rutas.cpp no compila:\n" + r.stdout + r.stderr
    r = subprocess.run([exe, os.path.join(tmp, "rutas.tsv")], capture_output=True, text=True)
    assert r.returncode == 0, "la regla de C++ no da lo mismo:\n" + r.stdout + r.stderr
    print(r.stdout.strip())


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
    # HER08: pt/ja usan copias de data_usi; si se crean DESPUES del montaje, el mod no lleva sus escenas y el
    # juego usa el texto nativo (en ja se midio un cierre al entrar). Se crean antes y el sello obliga a remontar.
    if us:
        usi = os.path.join(us, "data_usi.afs")
        for lk in ("por", "jpn"):
            dst = os.path.join(us, "data_%s.afs" % lk)
            if os.path.isfile(usi) and not os.path.isfile(dst):
                shutil.copyfile(usi, dst)
                print("heroe_sagas_a_du: copia de idioma creada: data_%s.afs" % lk)
    # Mismo directorio que usa main para las fotos; la presencia o cambio invalida la cache.
    raiz_nativa = os.path.join(juego, "ingame_ui", "heroe_fondos_b3")
    sello_b3 = ""
    if fondos_activos(juego) and os.environ.get("HEROE_FONDOS_B3") == "1":
        import hashlib  # noqa: PLC0415
        fotos = sorted({s["fondo_nativo"] for s in tramos(leer(sagas_toml)) if s.get("fondo_nativo")})
        huella = hashlib.sha256()
        for nombre_foto in fotos:
            huella.update(nombre_foto.encode())
            ruta_foto = os.path.join(raiz_nativa, nombre_foto + ".png")
            if os.path.isfile(ruta_foto):
                with open(ruta_foto, "rb") as f:
                    huella.update(hashlib.sha256(f.read()).digest())
        sello_b3 = "+b3=" + huella.hexdigest()[:16]
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
        langs_ok = all(os.path.isdir(os.path.join(d, "us", "data_%s.afs" % lk)) for lk in ("por", "jpn"))
        extra = "%s|%s|%s|langs=%d|img=%d" % (os.path.basename(pl or "-"), os.path.getmtime(pl) if pl else 0,
                                              nombre if propia else "", int(langs_ok), idioma_juego(juego))
        try:
            previo = open(marca, encoding="utf-8").read().strip()
        except OSError:
            previo = ""
        faltan = not os.path.isfile(os.path.join(d, "campana.toml"))
        base_s = sello(sagas_toml)
        if rutas_activas(juego):   # rutas: otra historia (tramos y cruces); el sello obliga a remontar al cambiar el ajuste
            base_s += "+rutas"
        if fondos_activos(juego):
            base_s += "+fondos" + sello_b3
        actual = base_s + "|" + sello(sagas_toml, extra)
        if not faltan and (previo == actual if propia else previo.split("|")[0] == base_s):
            continue
        cmd = [sys.executable, "-X", "utf8", os.path.abspath(__file__), "--montar", "--casilla", str(cas), "--mods", mods,
               "--sagas", sagas_toml] + (["--rutas"] if rutas_activas(juego) else []) + (["--fondos"] if fondos_activos(juego) else []) + (["--us", us] if us else []) + (["--plantilla", pl] if pl else []) + (["--heroe", nombre] if propia and nombre else [])
        r = subprocess.run(cmd, env=dict(os.environ, HEROE_IDIOMA=str(idioma_juego(juego)))).returncode
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
    p.add_argument("--rutas", action="store_true", help="tramos y cruces del ajuste dbz3_heroe_rutas (o HEROE_RUTAS=1)")
    p.add_argument("--fondos", action="store_true", help="dibujo grande de la narracion (dbz3_heroe_fondos_historia o HEROE_FONDOS=1)")
    a = p.parse_args()
    if a.fondos:
        os.environ["HEROE_FONDOS"] = "1"
    if a.comprobar:
        return comprobar()
    if a.mods:
        juego = os.path.dirname(os.path.abspath(a.mods))
        os.environ["HEROE_FONDOS_B3_DIR"] = os.path.join(juego, "ingame_ui", "heroe_fondos_b3")
    sagas = leer(a.sagas)
    rc = 0
    for cas in ([a.casilla] if a.casilla else CASILLAS):
        rutas = a.rutas or rutas_activas()
        txt, plan = generar(sagas, cas, a.id, a.retrato, heroe=a.heroe, rutas=rutas, ids_mod=ids_de_mods(a.mods) if rutas and a.mods else None)
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
            if rutas and a.mods:
                retratos_rivales(sagas, a.mods, os.path.join(a.mods, "historia_heroe_%d" % cas))
    return rc


def retratos_rivales(sagas, mods, mod):
    """Rutas: el retrato de dialogo de cada villano revivido INSTALADO (mods/<cut_x>/ui/retrato.png, 416x408 como los de B3)
    recortado a la cara como los nativos (native_ui kRetratos) en <historia>/ui_ingame/rutas/rival_<cut_x>.png: lo pinta
    la placa del cruce (heroe_v2_cruce.inl). Sin el mod no hay retrato (sale la foto "?")."""
    from PIL import Image  # noqa: PLC0415
    hechos = set()
    for s in tramos(sagas):
        for c in s.get("combate", []) + s.get("opcional", []):
            m = c.get("rival_mod")
            src = os.path.join(mods, m or "", "ui", "retrato.png")
            if not m or m in hechos or not os.path.isfile(src):
                continue
            im = Image.open(src).convert("RGBA")
            w, h = im.size   # `retrato_recorte` del combate (medido) o el recorte relativo del nativo (110,40-430,360 de 541x530)
            caja = c.get("retrato_recorte") or (round(w * 110 / 541), round(h * 40 / 530), round(w * 430 / 541), round(h * 360 / 530))
            cara = im.crop(tuple(int(v) for v in caja)).resize((320, 320), Image.LANCZOS)
            os.makedirs(os.path.join(mod, "ui_ingame", "rutas"), exist_ok=True)
            cara.save(os.path.join(mod, "ui_ingame", "rutas", "rival_%s.png" % m))
            hechos.add(m)
    if hechos:
        print("retratos de villanos revividos para el cruce: %s" % ", ".join(sorted(hechos)))


if __name__ == "__main__":
    sys.exit(main())

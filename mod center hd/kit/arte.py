# -*- coding: utf-8 -*-
"""Arte opcional del Dragon HUB: revisa una carpeta de imagenes y la instala como mod (mods/<mod>/ui_ingame/hub/).
El juego busca ahi cada pieza hub/<nombre>.png (src/ingame/game_style.cpp); apagar el mod devuelve el aspecto de siempre.
Sin interfaz: la pantalla esta en kit/pagina_arte.py. `python kit/arte.py` = prueba."""
from __future__ import annotations

import difflib
import os
import re
import shutil
import time

MOD = "pack_hub_mio"
FONDO, RETRATO, ICONO, PLACA = (16 / 9, False), (None, True), (1.0, True), (None, True)
# nombre -> (forma, uso es, uso en). forma = (proporcion o None, necesita transparencia)
PIEZAS = {
    "fondo": (FONDO, "Fondo de Kumite, Torre, Supervivencia y Pasarela", "Kumite, Tower, Survival and Gateway background"),
    "fondo_online": (FONDO, "Fondo de Online", "Online background"),
    "fondo_heroe": (FONDO, "Fondo del menú del modo Héroe", "Hero mode menu background"),
    "fondo_lab": (FONDO, "Fondo del Laboratorio", "Laboratory background"),
    "fondo_menu": (FONDO, "Fondo del menú del HUB", "HUB menu background"),
    "fondo_mision": (FONDO, "Fondo de misiones", "Missions background"),
    "fondo_ranking": (FONDO, "Fondo del ranking", "Ranking background"),
    "fondo_retos": (FONDO, "Fondo de retos", "Challenges background"),
    "heroe_fondo_creador": (FONDO, "Fondo del creador de héroe", "Hero creator background"),
    "heroe_fondo_maestros": (FONDO, "Fondo de elegir maestro", "Master select background"),
    "heroe_panel_maestro": (PLACA, "Placa de cada maestro", "Plate of each master"),
    "heroe_icono_traje": (ICONO, "Icono de la fila Traje", "Costume row icon"),
    "heroe_icono_emblema_estrella": (ICONO, "Icono de la fila Logo", "Logo row icon"),
    "heroe_icono_logo": (ICONO, "Icono de la fila Logo (si no hay emblema)", "Logo row icon (if there is no emblem)"),
    "heroe_icono_scouter": (ICONO, "Icono de la fila Scouter", "Scouter row icon"),
    "anfitrion_goku": (RETRATO, "Anfitrión: Goku", "Host: Goku"),
    "anfitrion_vegeta": (RETRATO, "Anfitrión: Vegeta", "Host: Vegeta"),
    "anfitrion_trunks": (RETRATO, "Anfitrión: Trunks", "Host: Trunks"),
    "anfitrion_gohan_futuro": (RETRATO, "Anfitrión de la Pasarela: Gohan del futuro", "Gateway host: Future Gohan"),
    "anfitrion_paikuhan": (RETRATO, "Anfitrión de Online: Paikuhan", "Online host: Pikkon"),
    "anfitrion_whis": (RETRATO, "Anfitrión: Whis", "Host: Whis"),
    "anfitrion_heroe": (RETRATO, "Anfitrión: el héroe", "Host: the hero"),
    "guardado": (ICONO, "Icono del aviso de guardado", "Save notice icon"),
    "logo": (None, "Logo del HUB", "HUB logo"),
}
# Nombres con los que ya circulan estas imagenes
ALIAS = {"tournament background": "fondo", "icono guardado ssj4": "guardado", "goku sprite for menu": "anfitrion_goku",
         "vegeta sprite for menu": "anfitrion_vegeta", "trunks sword for menu": "anfitrion_trunks",
         "future gohan for menu": "anfitrion_gohan_futuro", "paikuhan (pikkon) for menu": "anfitrion_paikuhan",
         "whis sprite for menu": "anfitrion_whis"}


def nombre_limpio(fichero):
    """«x.PNG.png» -> «x» (la doble extension sale sola al guardar con Windows ocultando extensiones)."""
    return re.sub(r"(\.png)+$", "", os.path.basename(fichero), flags=re.I)


def _revisar(path, forma, en):
    from PIL import Image
    avisos = []
    with Image.open(path) as im:
        w, h = im.size
        if forma:
            prop, transp = forma
            if prop and abs(w / h - prop) / prop > 0.03:
                avisos.append(("Proporción %d:%d: se recortará o deformará; se espera %s." % (w, h, "16:9" if prop > 1 else "cuadrada"),
                               "Shape %d:%d: it will be cropped or stretched; expected %s." % (w, h, "16:9" if prop > 1 else "square"))[en])
            if prop and prop > 1 and w < 1280:
                avisos.append(("Pequeña (%d px de ancho): se verá borrosa." % w, "Small (%d px wide): it will look blurry." % w)[en])
            alfa = im.convert("RGBA").getchannel("A").getextrema()
            if transp and alfa[0] == 255:
                avisos.append(("Sin transparencia: se verá el rectángulo del fondo.", "No transparency: its background rectangle will show.")[en])
            if not transp and alfa[0] < 255:
                avisos.append(("Tiene zonas transparentes y es un fondo.", "It has transparent areas and it is a background.")[en])
    return avisos


def analizar(carpeta, en=False):
    """-> (filas, desconocidas). fila = (pieza, ruta o None, avisos); desconocida = (fichero, nombre que se parece o None)."""
    halladas, desconocidas = {}, []
    rutas = []
    for raiz, dirs, ficheros in os.walk(carpeta):
        dirs[:] = [x for x in dirs if not x.startswith("_")]            # _fuentes y similares: material de trabajo
        rutas += [os.path.join(raiz, x) for x in ficheros if x.lower().endswith(".png")]
    for ruta in sorted(rutas):
        f = os.path.basename(ruta)
        n = nombre_limpio(f)
        pieza = n if n in PIEZAS or n.startswith("vuelo_") else ALIAS.get(n.lower())
        if pieza:
            halladas.setdefault(pieza, ruta)
        else:
            parecido = difflib.get_close_matches(n.lower(), PIEZAS, 1, 0.8)
            desconocidas.append((f, parecido[0] if parecido else None))
    filas = []
    for pieza in list(PIEZAS) + sorted(k for k in halladas if k not in PIEZAS):
        ruta = halladas.get(pieza)
        forma = PIEZAS[pieza][0] if pieza in PIEZAS else None
        try:
            filas.append((pieza, ruta, _revisar(ruta, forma, en) if ruta else []))
        except OSError:
            filas.append((pieza, ruta, [("La imagen está dañada.", "The image is damaged.")[en]]))
    return filas, desconocidas


def instalar(env, filas, anotar):
    """Copia las piezas halladas a mods/<MOD>/ui_ingame/hub/. Lo sustituido va a modkit_backups; «Deshacer» lo devuelve."""
    destino = os.path.join(env.mods, MOD, "ui_ingame", "hub")
    os.makedirs(destino, exist_ok=True)
    manifest = os.path.join(env.mods, MOD, "manifest.txt")
    stamp = time.strftime("%Y%m%d-%H%M%S")
    if not os.path.exists(manifest):
        with open(manifest, "w", encoding="utf-8") as fh:
            fh.write("name=Mi arte del Dragon HUB\ndescription=Imagenes propias del HUB (Mod Kit, Arte opcional)\nversion=1.0\n")
        anotar(env, stamp, "nuevo", manifest, "")
    apartado = os.path.join(env.backups, stamp + "-arte")
    n = 0
    for pieza, ruta, _av in filas:
        if not ruta:
            continue
        dst = os.path.join(destino, pieza + ".png")
        if os.path.exists(dst):
            os.makedirs(apartado, exist_ok=True)
            copia = os.path.join(apartado, "%d_%s.png" % (n, pieza))
            os.replace(dst, copia)
            anotar(env, stamp, "movido", dst, os.path.relpath(copia, env.backups))
        shutil.copyfile(ruta, dst)
        anotar(env, stamp, "nuevo", dst, "")
        n += 1
    return n


def prueba():
    import tempfile
    from types import SimpleNamespace
    from PIL import Image
    with tempfile.TemporaryDirectory() as d:
        ent, mods = os.path.join(d, "ent"), os.path.join(d, "mods")
        os.makedirs(ent)
        Image.new("RGBA", (1600, 900), (1, 2, 3, 255)).save(os.path.join(ent, "fondo_online.png"))
        Image.new("RGBA", (64, 64), (1, 2, 3, 255)).save(os.path.join(ent, "heroe_icono_traje.png"))
        Image.new("RGBA", (400, 100), (0, 0, 0, 0)).save(os.path.join(ent, "heroe_panel_maestro.png.png"))
        Image.new("RGBA", (64, 64), (0, 0, 0, 0)).save(os.path.join(ent, "Icono guardado SSJ4.png"))
        Image.new("RGBA", (64, 64), (0, 0, 0, 0)).save(os.path.join(ent, "heroe_icono_scuter.png"))
        filas, raras = analizar(ent)
        d_f = {f[0]: f for f in filas}
        assert d_f["fondo_online"][1] and not d_f["fondo_online"][2]
        assert any("transparencia" in a for a in d_f["heroe_icono_traje"][2])
        assert d_f["heroe_panel_maestro"][1] and d_f["guardado"][1] and not d_f["logo"][1]
        assert raras == [("heroe_icono_scuter.png", "heroe_icono_scouter")], raras
        notas = []
        env = SimpleNamespace(mods=mods, backups=os.path.join(d, "bak"))
        assert instalar(env, filas, lambda *a: notas.append(a)) == 4
        assert os.path.isfile(os.path.join(mods, MOD, "ui_ingame", "hub", "heroe_panel_maestro.png"))
        assert instalar(env, filas, lambda *a: notas.append(a)) == 4          # la segunda vez aparta lo anterior
        assert sum(1 for x in notas if x[2] == "movido") == 4
    print("arte: prueba OK")


if __name__ == "__main__":
    prueba()

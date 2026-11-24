"""R18: vista previa XV2, receta R16 fija y 60 pintados consecutivos a escala 0.5."""
import json
import time

from PIL import Image, ImageDraw, ImageChops, ImageStat
from pruebas_cac_guardado import mover_guardados

RAZAS = {"HUM": (7, 110, 301, 2), "HUF": (8, 111, 301, 2), "MAM": (11, 114, 302, 13)}


def escenario(api, par):
    razas = par.get("raza", "HUM").upper().split(",")
    raza = razas[0]
    if any(r not in RAZAS for r in razas):
        raise api.Fallo("raza debe ser HUM, HUF o MAM")
    r, cuerpo, traje, ojos = RAZAS[raza]
    par["sin_arranque"] = "1"
    env = {"DBZ3_TEST_MODO": "heroe,0", "DBZ3_TEST_HEROE_ABRIR": "2",
           "DBZ3_TEST_CFG": "dbz3_cac_xv2=true;dbz3_heroe_ui_nueva=true;dbz3_language=5",
           "DBZ3_TEST_VISTA_VERTICES": "1", "DBZ3_TEST_TURBO": "1"}

    def sembrar(origen):
        receta = (f'version = 2\nactivo = 0\n[[heroe]]\nid = "r18vista"\nnombre = "Heroe"\n'
                  f'raza = {r}\ncuerpo = {cuerpo}\ntraje = 0\nprogreso = 2\n'
                  f'xv2 = [2, 2, {traje}, {traje}, {traje}, {traje}, {ojos}]\n'
                  'partes = [[10,10,-10],[10,10,-10],[10,10,-10],[10,10,-10],[10,10,-10],[10,10,-10]]\n')
        (origen / "cac.toml").write_text(receta, encoding="utf-8")
        return receta

    def preparar(j, p, entorno):
        origen = j.dest / "user_data/dbz3"
        origen.mkdir(parents=True, exist_ok=True)
        previo = j.run / "user_data_previo"

        def restaurar():
            if j.vivo():
                raise api.Fallo("el juego sigue vivo al restaurar")
            mover_guardados(origen, j.run / "user_data_final")
            mover_guardados(previo, origen)
        j.restaurar_guardado = restaurar
        mover_guardados(origen, previo)
        j.vista_env = dict(entorno)
        receta = sembrar(origen)
        (j.run / "receta.toml").write_text(receta, encoding="utf-8")
        # Nunca reutilizar las imagenes de una prueba anterior.
        viejas = list(j.dest.glob("dbz3_shot_*.bmp"))
        if viejas:
            carpeta = j.run / "rafaga_previa"
            carpeta.mkdir()
            for f in viejas:
                f.rename(carpeta / f.name)

    def ejecutar(j, p):
        j.arrancar(180, titulo=("Return", "W", "Space"))
        j.log(r"Create abierto", 60)
        j.log(rf"ficha 0 importada \(raza {r}, cuerpo {cuerpo},", 30)
        j.log(r"vista previa 3D en vivo", 240)
        api.dormir(5)
        # N K: captura desde el presentador del juego cada K pintados; K=1 no salta cuadros.
        (j.dest / "dbz3_shot.req").write_text("60 1", encoding="ascii")
        fin = time.monotonic() + 180
        ultimo = j.dest / "dbz3_shot_059.bmp"
        while not ultimo.exists():
            if not j.vivo() or time.monotonic() > fin:
                raise api.Fallo("no llegaron los 60 pintados de la rafaga")
            time.sleep(.1)
        api.dormir(.5)
        carpeta = j.run / raza
        carpeta.mkdir()
        fotos, cambios = [], []
        for i in range(60):
            archivo = j.dest / f"dbz3_shot_{i:03}.bmp"
            with Image.open(archivo) as im:
                foto = im.convert("RGB")
                foto.save(carpeta / f"{i:03}.png")
                fotos.append(foto)
            archivo.rename(carpeta / archivo.name)
        # Hoja sin reducir otra vez: las capturas ya las produce el SDK a 0.5.
        w, h = fotos[0].size
        hoja = Image.new("RGB", (w * 6, (h + 24) * 10), "#202020")
        texto = ImageDraw.Draw(hoja)
        for i, foto in enumerate(fotos):
            x, y = i % 6 * w, i // 6 * (h + 24)
            hoja.paste(foto, (x, y + 24))
            texto.text((x + 5, y + 5), f"{raza} {i:03}", fill="white")
            if i:
                dif = ImageChops.difference(fotos[i-1], foto)
                cambios.append(sum(ImageStat.Stat(dif).mean) / 3)
        hoja.save(j.run / f"{raza}_60.png")
        (j.run / f"{raza}_serie.json").write_text(json.dumps(dict(raza=raza, cuadros=60, cada_pintado=1,
            escala=.5, tamano=[w,h], diferencia_media=cambios), indent=2), encoding="utf-8")
        # El PASS confirma la captura completa; el informe recoge la revision visual.
        j.info(f"PASS {raza}: 60 pintados consecutivos capturados a escala 0.5; revision visual en {raza}_60.png")

    def ejecutar_todas(j, p):
        nonlocal raza, r, cuerpo, traje, ojos
        for i, raza in enumerate(razas):
            r, cuerpo, traje, ojos = RAZAS[raza]
            if i:
                j.cerrar()
                mover_guardados(j.dest / "user_data/dbz3", j.run / razas[i-1] / "user_data")
                sembrar(j.dest / "user_data/dbz3")
                j.lanzar(j.vista_env)
            ejecutar(j, p)
            (j.run / raza / "dibujo.log").write_text(j.texto_log(), encoding="utf-8")

    return "R18 vista previa fija " + ",".join(razas), ejecutar_todas, env, preparar

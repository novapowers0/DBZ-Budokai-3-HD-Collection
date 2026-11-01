# -*- coding: utf-8 -*-
"""Ayudas del Mod Kit: un tooltip en cada control y un «¿Cómo se hace?» en cada pantalla.

Los tooltips se resuelven al pasar el ratón (un solo enganche global), así valen también para lo que se construye
después: el editor de Personajes, los asistentes y las ventanas aparte. Clave = texto visible del control o, si no
tiene, el de la etiqueta que lo precede.
"""
from __future__ import annotations

import re
import tkinter as tk
from tkinter import ttk

# ("texto en español", "text in English"), "ayuda", "help". Lo que empieza por símbolos (⟳, ✚...) se compara sin ellos.
_TIPS = [
    (("Elegir carpeta", "Choose folder"), "Elige la carpeta donde tienes tus imágenes PNG. No se cambia nada todavía.",
     "Pick the folder with your PNG images. Nothing changes yet."),
    (("Instalar en mis mods", "Install into my mods"), "Copia las imágenes válidas a un mod que puedes apagar o deshacer.",
     "Copies the usable images into a mod you can turn off or undo."),
    (("Inicio", "Home"), "Vuelve a la pantalla de inicio. Lo que ya has hecho no se pierde.",
     "Back to the home screen. What you already did is kept."),
    (("Comprobar de nuevo", "Check again"), "Vuelve a mirar Python, librerías y carpetas. Úsalo tras instalar algo.",
     "Checks Python, libraries and folders again. Use it after installing something."),
    (("Instalar librerías", "Install libraries"), "Instala lo que falta con pip (numpy, Pillow, scipy). Hace falta internet.",
     "Installs what is missing with pip (numpy, Pillow, scipy). Needs internet."),
    (("Revisar este mod", "Check this mod"), "Mira si a este mod le falta algún archivo o tiene un error de escritura, y dice qué hacer. No cambia nada ni abre el juego.",
     "Checks whether this mod is missing a file or has a typing error, and says what to do. Changes nothing and does not open the game."),
    (("Revisar modelos", "Check models"), "Comprueba que los modelos de tus mods se pueden cargar. No cambia nada.",
     "Checks that the models in your mods can load. Changes nothing."),
    (("Revisar los modelos de mis mods", "Check my mods' models"),
     "Comprueba que los modelos de tus mods se pueden cargar. No cambia nada.",
     "Checks that the models in your mods can load. Changes nothing."),
    (("Abrir ps2_games", "Open ps2_games"), "Abre la carpeta donde dejar tus juegos de PS2 y PSP (.iso) para importar.",
     "Opens the folder for your PS2 and PSP games (.iso) to import from."),
    (("Cómo nombrar los juegos", "How to name the games"), "Qué nombre debe llevar cada juego para que se reconozca.",
     "What each game must be called so it is recognised."),
    (("Refrescar", "Refresh"), "Vuelve a leer la lista desde el disco.", "Reads the list from disk again."),
    (("Abrir carpeta mods", "Open mods folder"), "Abre la carpeta donde viven tus mods.",
     "Opens the folder where your mods live."),
    (("Reconstruir personajes nuevos", "Rebuild new characters"),
     "Vuelve a montar la rueda con tus personajes. Tarda unos minutos la primera vez; JUGAR también lo hace.",
     "Builds the wheel again with your characters. A few minutes the first time; PLAY does it too."),
    (("Deshacer último paso", "Undo last step"),
     "Recupera la copia del último cambio hecho por el Kit. Solo el último.",
     "Restores the copy of the Kit's last change. Only the last one."),
    (("Buscar de nuevo", "Look again"), "Vuelve a buscar tus juegos de PS2 y PSP.", "Looks for your PS2 and PSP games again."),
    (("Pies al suelo", "Feet on the ground"),
     "Recomendado. Ajusta la altura para que no flote ni se hunda.", "Recommended. Adjusts height so it neither floats nor sinks."),
    (("Pose del select propia", "Own select pose"), "Usa su pose original en la pantalla de elegir personaje.",
     "Uses its original pose on the character select screen."),
    (("Técnica al final de los combos", "Technique at combo end"),
     "Añade un remate con técnica al acabar los combos, como en su juego de origen.",
     "Adds a technique finisher at combo end, like in its original game."),
    (("Importar", "Import"), "Crea el mod del personaje con estas opciones. Tus datos originales no se tocan.",
     "Creates the character mod with these options. Your original data is not touched."),
    (("Reconstruir ahora", "Rebuild now"), "Mete ya los cambios en el juego, sin esperar a pulsar JUGAR.",
     "Puts the changes in the game now, without waiting for PLAY."),
    (("Ver en Personajes nuevos", "Show in New characters"), "Abre el personaje para retocar nombre, plaza y colores.",
     "Opens the character to adjust name, slot and colours."),
    (("Activar / desactivar", "Enable / disable"),
     "Enciende o apaga el mod sin borrarlo. Se aplica al reconstruir.", "Turns the mod on or off without deleting it. Applied on rebuild."),
    (("Abrir carpeta", "Open folder"), "Abre la carpeta de este mod en el explorador.", "Opens this mod's folder in Explorer."),
    (("Cámaras (Studio)", "Cameras (Studio)"), "Abre el Studio para rehacer las cámaras de sus técnicas.",
     "Opens the Studio to redo its technique cameras."),
    (("Regenerar vista previa", "Refresh preview"), "Vuelve a dibujar icono y retratos desde su modelo 3D.",
     "Draws the icon and portraits again from its 3D model."),
    (("Guardar nombre", "Save name"), "Guarda el nombre. Recomendado: menos de 14 letras para que quepa en el rótulo.",
     "Saves the name. Recommended: under 14 letters so it fits the banner."),
    (("Guardar", "Save"), "Guarda estos valores en el mod. Se verán al reconstruir.", "Saves these values in the mod. Visible after rebuild."),
    (("Renombrar", "Rename"), "Cambia el nombre de esta cápsula.", "Changes this capsule's name."),
    (("…",), "Elige el archivo o la carpeta.", "Pick the file or folder."),
    (("Como cara (icono)", "As face (icon)"), "Tu imagen es una cara: el Kit le añade el círculo del icono oficial.",
     "Your image is a face: the Kit adds the official icon circle."),
    (("Como icono terminado", "As finished icon"), "Tu imagen ya es el icono terminado: se usa tal cual, sin retoques.",
     "Your image is already the finished icon: used as is."),
    (("Como retrato", "As portrait"), "Tu imagen es el retrato grande del select. Recomendado: PNG con fondo transparente.",
     "Your image is the big select portrait. Recommended: PNG with a transparent background."),
    (("Generar desde el modelo 3D", "Render from the 3D model"),
     "Dibuja icono y retratos con su modelo, con la cámara y la luz medidas en los oficiales.",
     "Draws the icon and portraits from its model, with the camera and light measured on the official ones."),
    (("Subir", "Up"), "Sube el elemento elegido en la lista.", "Moves the selected item up."),
    (("Bajar", "Down"), "Baja el elemento elegido en la lista.", "Moves the selected item down."),
    (("Quitar", "Remove"), "Quita el elemento elegido de la lista.", "Removes the selected item from the list."),
    (("Fijar ki", "Set ki"), "Guarda las barras de ki. Recomendado: entre 3 y 7, como los nativos.",
     "Saves the ki bars. Recommended: 3 to 7, like native characters."),
    (("Añadir cápsula", "Add capsule"), "Crea una cápsula nueva con el nombre y el tipo de la izquierda.",
     "Creates a new capsule with the name and type on the left."),
    (("Traer las cápsulas de su juego", "Bring capsules from its game"),
     "Copia las cápsulas que tenía en su juego de origen.", "Copies the capsules it had in its original game."),
    (("Añadir…", "Add…", "Add..."), "Elige uno o varios archivos de modelo (OBJ o glTF).", "Pick one or more model files (OBJ or glTF)."),
    (("Crear", "Create"), "Crea el mod con estos datos. No cambia tus datos originales.", "Creates the mod with this data. Your original data is untouched."),
    (("Ventana aparte", "Separate window"), "Abre el editor de historias en su propia ventana, con más sitio.",
     "Opens the story editor in its own window, with more room."),
    (("Crear y montar", "Create and build"), "Crea el modo de reto y lo mete ya en el juego.", "Creates the challenge mode and puts it in the game now."),
    (("Poner en el HUB", "Use in the HUB"), "Lo deja disponible en el menú nativo del juego.", "Makes it available in the game's native menu."),
    (("Poner rivales", "Set rivals"), "Aplica estos rivales al Torneo Mundial.", "Applies these rivals to the World Tournament."),
    (("Poner", "Use it"), "Instala el reto elegido de la lista.", "Installs the challenge picked in the list."),
    (("Torre de la semana", "Tower of the week"), "Genera una torre nueva distinta cada semana.", "Generates a new tower that changes each week."),
    (("Abrir carpeta del mod", "Open mod folder"), "Abre la carpeta de esta historia.", "Opens this story's folder."),
    (("Dejar solo esta activa", "Keep only this one enabled"), "Apaga las demás historias para probar solo esta.", "Turns the other stories off to test only this one."),
    (("Montar mod", "Build mod"), "Convierte la historia en un mod listo para jugar.", "Turns the story into a mod ready to play."),
    (("Probar", "Test"), "Monta la historia y comprueba errores antes de jugarla.", "Builds the story and checks for errors before playing it."),
    (("Guardar como...", "Save as..."), "Guarda la historia en otro archivo.", "Saves the story to another file."),
    (("Nueva historia", "New story"), "Empieza una historia en blanco.", "Starts a blank story."),
    (("Abrir...", "Open..."), "Abre una historia guardada (.toml).", "Opens a saved story (.toml)."),
    (("Rellenar traducciones que faltan", "Fill missing translations"),
     "Copia el texto al idioma que esté vacío, para que no quede en blanco.", "Copies the text to the empty language so it is not blank."),
    (("Añadir", "Add"), "Añade un elemento nuevo a la lista.", "Adds a new item to the list."),
    (("Duplicar", "Duplicate"), "Copia el elemento elegido.", "Copies the selected item."),
    (("Borrar", "Delete"), "Quita el elemento elegido de la historia.", "Removes the selected item from the story."),
    (("Abre una saga nueva", "Opens a new saga"), "Este capítulo empieza una saga nueva en el mapa.", "This chapter starts a new saga on the map."),
    (("Elegir...", "Choose..."), "Elige un archivo de tu ordenador.", "Pick a file on your computer."),
    (("Restablecer", "Reset"), "Vuelve al valor original.", "Back to the original value."),
    (("Elegir color...", "Choose color..."), "Abre el selector de color.", "Opens the colour picker."),
    (("Restablecer este estilo", "Reset this style"), "Devuelve este estilo a sus valores originales.", "Returns this style to its original values."),
    (("Extraer texturas a PNG", "Extract textures to PNG"),
     "Saca las texturas a una carpeta como imágenes. Edítalas sin cambiar su tamaño.",
     "Exports the textures to a folder as images. Edit them without changing their size."),
    (("Abrir carpeta de texturas", "Open textures folder"), "Abre la carpeta con los PNG para editarlos.", "Opens the folder with the PNGs to edit them."),
    (("Reconstruir mod con texturas editadas", "Rebuild mod with edited textures"),
     "Vuelve a meter tus PNG en el mod y lo activa.", "Puts your PNGs back into the mod and enables it."),
    (("Cambiar B3 → B3", "Swap B3 → B3"), "Crea un mod con el modelo de origen en el sitio de destino. Se puede apagar en «Mis mods».",
     "Creates a mod with the source model in the target slot. You can turn it off in 'My mods'."),
    (("Analizar mi último registro", "Analyse my latest log"), "Lee el último registro del juego y te dice qué pasó y qué hacer.",
     "Reads the game's latest log and tells you what happened and what to do."),
    (("Abrir otro registro…", "Open another log…"), "Analiza un registro que hayas guardado o te hayan pasado.",
     "Analyses a log you saved or someone sent you."),
    (("Copiar informe", "Copy report"), "Copia un resumen sin datos personales para pedir ayuda en el Discord.",
     "Copies a summary without personal data to ask for help on Discord."),
    (("Abrir", "Open"), "Abre este enlace o documento.", "Opens this link or document."),
    (("Leer", "Read"), "Lee esta guía dentro del Kit.", "Reads this guide inside the Kit."),
    (("Abrir carpeta docs", "Open docs folder"), "Abre la carpeta con todas las guías.", "Opens the folder with all the guides."),
    (("Ver detalles", "Details"), "Muestra qué se ha comprobado y qué falta.", "Shows what was checked and what is missing."),
    (("Ajustes…", "Settings…"), "Editor de las opciones del mod, campo a campo.", "Editor for the mod's options, field by field."),
    (("¿Cómo se hace?", "How is it done?"), "Pasos cortos y recomendaciones para esta pantalla.", "Short steps and recommendations for this screen."),
    (("Usar una carpeta", "Use a folder"), "Si tienes el disco extraído en una carpeta en vez de una .iso.", "If you have the disc extracted to a folder instead of an .iso."),
    (("Sacar texturas", "Extract textures"), "Saca las texturas de esa forma a una carpeta como PNG. Abajo las verás.", "Exports that form's textures to a folder as PNGs. You will see them below."),
    (("Crear mod con mis cambios", "Create mod with my changes"), "Crea el mod con los PNG que hayas cambiado. Los que no toques quedan como el original.", "Creates the mod with the PNGs you changed. Untouched ones stay original."),
    (("Crear mod", "Create mod"), "Crea el mod. Después enciéndelo en el paso 3 y monta la ISO.", "Creates the mod. Then enable it in step 3 and build the ISO."),
    (("Montar ISO", "Build ISO"), "Crea la ISO con los mods encendidos.", "Creates the ISO with the enabled mods."),
    (("Cerrar", "Close"), "Cierra esta ventana. Si no has aplicado nada, no cambia nada.", "Closes this window. If you applied nothing, nothing changes."),
    (("Siguiente", "Next"), "Comprueba este paso y pasa al siguiente. Aún no se cambia nada.", "Checks this step and moves on. Nothing changes yet."),
    (("Atrás", "Back"), "Vuelve al paso anterior sin perder lo escrito.", "Back to the previous step, keeping what you typed."),
    (("De mi propio modelo (.amb o .bin)", "From my own model (.amb or .bin)"), "El traje sale de un archivo de modelo tuyo.", "The costume comes from your own model file."),
    (("De un personaje que ya importé", "From a character I already imported"), "El traje sale de un personaje que ya está en tus mods.", "The costume comes from a character already in your mods."),
    (("No cambiar las imágenes", "Do not change the images"), "Deja su icono y sus retratos como están.", "Keeps its icon and portraits as they are."),
    (("Poner mis imágenes (PNG)", "Use my images (PNG)"), "Usa tus propias imágenes para icono y retratos.", "Uses your own images for icon and portraits."),
    (("Sacar las imágenes del personaje para editarlas", "Export the character's images so I can edit them"), "Saca sus imágenes a una carpeta para retocarlas y volver aquí.", "Exports its images to a folder to retouch them and come back."),
    # campos (la etiqueta que les precede)
    (("Imagen", "Image"), "Tu imagen: PNG, WEBP o JPG. Abajo la verás recortada como quedará.", "Your image: PNG, WEBP or JPG. Below you see it cropped as it will be."),
    (("Se usará como", "It will be used as"), "El sitio donde saldrá: cambia el recorte y el tamaño.", "Where it will appear: it changes the crop and size."),
    (("Personaje", "Character"), "Personaje al que aplicar el cambio.", "Character to apply the change to."),
    (("Forma", "Form"), "1 es su aspecto base; los números siguientes, sus trajes y transformaciones.", "1 is its base look; the next numbers, its costumes and transformations."),
    (("En el sitio de", "In the slot of"), "Quién cambia de aspecto: conserva nombre, icono y golpes.", "Who changes look: keeps name, icon and moves."),
    (("ponle el aspecto de", "give it the look of"), "De quién toma el modelo y las texturas.", "Whose model and textures it takes."),
    (("Pista que sustituye", "Track to replace"), "Número, duración y título de cada música de tu copia. Pulsa «Escuchar» al lado para oírla antes de cambiarla.", "Number, length and title of each piece of music in your copy. Press 'Listen' next to it to hear it before changing it."),
    (("Nombre en el select", "Select-screen name"), "El nombre que verás en la rueda. Recomendado: menos de 14 letras.",
     "The name shown on the wheel. Recommended: under 14 letters."),
    (("Golpes de (personaje base)", "Moves from (base character)"),
     "De quién hereda los golpes si el importado no trae los suyos. Recomendado: uno de tamaño parecido.",
     "Whose moves it inherits if it has none of its own. Recommended: one of similar size."),
    (("Preset",), "Una receta ya probada que rellena las opciones por ti.", "A tested recipe that fills in the options for you."),
    (("Plaza", "Slot"), "A qué personaje del juego sustituye por dentro. Un personaje que no uses es la mejor opción.",
     "Which game character it replaces internally. One you do not use is the best choice."),
    (("Casilla tras", "Cell after"), "Detrás de quién aparece en la rueda.", "Who it appears after on the wheel."),
    (("Aura",), "Color del aura. La muestra indica el tono; en el juego brilla más.", "Aura colour. The swatch shows the tone; it glows more in game."),
    (("Ki y técnicas", "Ki and techniques"), "Color del ki y de sus técnicas.", "Colour of its ki and techniques."),
    (("Forma del aura", "Aura shape"), "Forma del aura, tomada de otro personaje.", "Aura shape, taken from another character."),
    (("Base",), "Forma base que se muestra en el select.", "Base form shown on the select screen."),
    (("Nombre", "Name"), "Nombre visible.", "Visible name."),
    (("Ki (barras):", "Ki (bars):"), "Barras de ki de esta forma. Recomendado: entre 3 y 7.", "Ki bars of this form. Recommended: 3 to 7."),
    (("Nueva:", "New:"), "Nombre de la cápsula nueva. Recomendado: menos de 20 letras.", "Name of the new capsule. Recommended: under 20 letters."),
    (("forma", "form"), "Forma a la que se aplica (0 = base).", "Form it applies to (0 = base)."),
    (("Nombre (rótulo)", "Name (banner)"), "Nombre que aparece en el rótulo del select.", "Name on the select banner."),
    (("Carpeta del mod", "Mod folder"), "Nombre de la carpeta. Sin espacios ni acentos; se rellena solo.", "Folder name. No spaces or accents; it fills itself."),
    (("Personaje base (golpes y técnicas)", "Base character (moves and techniques)"),
     "De quién hereda golpes y técnicas. Elige uno con cuerpo parecido.", "Whose moves and techniques it inherits. Pick one with a similar body."),
    (("Formas por traje", "Forms per costume"), "Cuántos modelos forman un traje (base, super...).", "How many models make a costume (base, super...)."),
    (("Modos de reto", "Challenge modes"), "Tipo de reto a crear.", "Type of challenge to create."),
    (("combates", "fights"), "Cuántos combates tendrá. Recomendado: 5 a 15.", "How many fights it has. Recommended: 5 to 15."),
    (("Torneo Mundial", "World Tournament"), "Personajes separados por comas.", "Characters separated by commas."),
    (("Retos predefinidos", "Ready-made challenges"), "Retos listos para instalar.", "Challenges ready to install."),
    (("Nombre del mod", "Mod name"), "Vacío = se elige solo. Sin espacios ni acentos.", "Empty = chosen for you. No spaces or accents."),
    (("Tamaño", "Size"), "Tamaño en pantalla.", "Size on screen."),
    (("Izquierda / derecha", "Left / right"), "Posición horizontal.", "Horizontal position."),
    (("Arriba / abajo (+ abajo)", "Up / down (+ down)"), "Posición vertical.", "Vertical position."),
    (("Archivo de audio", "Audio file"), "Tu canción: mp3, wav, ogg, flac, m4a o adx. Usa solo audio que tengas derecho a usar.",
     "Your song: mp3, wav, ogg, flac, m4a or adx. Use only audio you have the right to use."),
    (("Nombre en el juego", "Name in game"), "Cómo se llamará en la lista de música.", "What it is called in the music list."),
    (("Tu copia de Budokai 3 (PS2)", "Your Budokai 3 copy (PS2)"), "Tu ISO o la carpeta extraída del disco. Solo se lee, nunca se modifica.",
     "Your ISO or the folder extracted from the disc. Only read, never changed."),
    (("ISO que se creará", "ISO to create"), "Dónde se guardará la copia con tus mods. Ocupa unos 2,4 GB.",
     "Where the copy with your mods is saved. About 2.4 GB."),
]

_POR_TIPO = {
    "Treeview": ("Haz clic en una fila para elegirla.", "Click a row to select it."),
    "Listbox": ("Haz clic en una fila para elegirla.", "Click a row to select it."),
    "Combobox": ("Despliega y elige una opción.", "Open it and pick an option."),
    "Spinbox": ("Escribe un número o usa las flechas.", "Type a number or use the arrows."),
    "Entry": ("Escribe aquí.", "Type here."),
    "Text": ("Escribe aquí.", "Type here."),
    "Scale": ("Arrastra para ajustar el valor.", "Drag to adjust the value."),
}
_BOTONES = (ttk.Button, ttk.Checkbutton, ttk.Radiobutton, tk.Button, tk.Checkbutton)
_CAMPOS = (ttk.Combobox, ttk.Spinbox, ttk.Entry, tk.Entry, tk.Text, tk.Listbox, ttk.Treeview, tk.Scale, ttk.Scale)


def _norma(texto):
    return re.sub(r"^[^\wÀ-ÿ¿(]+", "", str(texto)).strip().rstrip(":→ ").lower()


_INDICE = {}
for _claves, _es, _en in _TIPS:
    for _c in _claves:
        _INDICE[_norma(_c) or _c] = (_es, _en)


def _elegir(par, lang):
    return par[0] if lang == "es" else par[1]


def _etiqueta_previa(w):
    """Texto de la etiqueta que precede a un campo dentro de su contenedor (también en rejilla)."""
    previa = None
    for h in w.master.winfo_children():
        if h is w:
            break
        if isinstance(h, (tk.Label, ttk.Label)):
            try:
                previa = h.cget("text") or previa
            except tk.TclError:
                pass
    return previa


def tip_de(w, lang):
    """Texto de ayuda de un control o None si no es un control. Nunca lanza: esto corre con cada movimiento."""
    if getattr(w, "_con_tip", False):
        return None
    try:
        if isinstance(w, _BOTONES):
            t = w.cget("text")
            if isinstance(w, (ttk.Checkbutton, tk.Checkbutton)) and _norma(t) not in _INDICE:
                return None if len(t) > 60 else _elegir(("Marca o desmarca: %s" % t, "Tick or untick: %s" % t), lang)
            hit = _INDICE.get(_norma(t) or t)
            return _elegir(hit, lang) if hit else None
        if isinstance(w, _CAMPOS):
            previa = _etiqueta_previa(w)
            hit = _INDICE.get(_norma(previa)) if previa else None
            if hit:
                return _elegir(hit, lang)
            for clase, par in _POR_TIPO.items():
                if type(w).__name__ == clase or any(k.__name__ == clase for k in type(w).__mro__):
                    if previa and len(previa) < 40:
                        return _elegir(("%s. %s" % (previa.strip(), par[0]), "%s. %s" % (previa.strip(), par[1])), lang)
                    return _elegir(par, lang)
    except tk.TclError:
        pass
    return None


def instalar(root, g):
    """Enganche global: al entrar el ratón en un control sin tooltip propio, muestra el suyo. g = modkit_gui."""
    estado = {"w": None, "job": None, "tip": None}

    def quitar(_e=None):
        if estado["job"]:
            try:
                root.after_cancel(estado["job"])
            except tk.TclError:
                pass
            estado["job"] = None
        if estado["tip"]:
            try:
                estado["tip"].destroy()
            except tk.TclError:
                pass
            estado["tip"] = None

    def mostrar(w, texto):
        estado["job"] = None
        try:
            if not w.winfo_exists() or root.winfo_containing(*root.winfo_pointerxy()) is not w:
                return
            t = tk.Toplevel(w)
            t.wm_overrideredirect(True)
            t.configure(bg=g.C["line"])
            tk.Label(t, text=texto, bg=g.C["card_hi"], fg=g.C["text"], font=g.F["small"], justify="left",
                     wraplength=440, padx=10, pady=6).pack(padx=1, pady=1)
            t.wm_geometry("+%d+%d" % (w.winfo_rootx() + 14, w.winfo_rooty() + w.winfo_height() + 6))
            estado["tip"] = t
        except (tk.TclError, KeyError):
            estado["tip"] = None

    def entrar(e):
        quitar()
        w = e.widget
        if isinstance(w, str):
            return
        texto = tip_de(w, g.LANG)
        if texto:
            estado["job"] = root.after(500, lambda: mostrar(w, texto))

    root.bind_all("<Enter>", entrar, add="+")
    root.bind_all("<Leave>", quitar, add="+")
    root.bind_all("<ButtonPress>", quitar, add="+")


def sin_tip(raiz, lang):
    """Botones de `raiz` sin ayuda (para la prueba del Kit): [texto]."""
    faltan = []

    def andar(w):
        for h in w.winfo_children():
            if isinstance(h, _BOTONES) and not getattr(h, "_con_tip", False):
                t = h.cget("text")
                if t and tip_de(h, lang) is None and not (isinstance(h, (ttk.Checkbutton, tk.Checkbutton)) and len(t) > 60):
                    faltan.append(t)
            andar(h)
    andar(raiz)
    return faltan


# ---------------------------------------------------------------- «¿Cómo se hace?»
# clave de pantalla -> (título es, título en, pasos es, pasos en, consejo es, consejo en, guía opcional)
GUIA = "docs/02_mods/COMO_HACER_MODS.md"
COMO = {
    "arte": ("Poner tus imágenes", "Adding your images",
             ["Pulsa «Elegir carpeta» y elige donde tienes tus PNG.", "Mira los avisos: nombre, forma y transparencia.",
              "Pulsa «Instalar en mis mods» y abre el juego."],
             ["Press 'Choose folder' and pick where your PNGs are.", "Check the warnings: name, shape and transparency.",
              "Press 'Install into my mods' and open the game."],
             "Es un mod más: apágalo en «Mis mods» y vuelve el aspecto de siempre. No se reparte con el juego.",
             "It is just another mod: turn it off in 'My mods' and the usual look returns. It does not ship with the game."),
    "setup": ("Dejar el Kit a punto", "Getting the Kit ready",
              ["Pulsa «Comprobar de nuevo» y mira qué sale en rojo.", "Si faltan librerías, pulsa «Instalar librerías».",
               "Pon tus juegos de PS2 y PSP en ps2_games si vas a importar personajes."],
              ["Press 'Check again' and see what is red.", "If libraries are missing, press 'Install libraries'.",
               "Put your PS2 and PSP games in ps2_games if you will import characters."],
              "No hace falta extraer los juegos: el Kit lee las .iso directamente.",
              "No need to extract the games: the Kit reads the .iso files directly."),
    "mods": ("Encender y apagar mods", "Turning mods on and off",
             ["Elige un mod en la lista.", "Pulsa el botón de activar para encenderlo o apagarlo.",
              "Pulsa «Reconstruir personajes nuevos» si es un personaje."],
             ["Pick a mod in the list.", "Press the enable button to turn it on or off.",
              "Press 'Rebuild new characters' if it is a character."],
             "Apagar no borra nada. Si algo deja de ir, apaga el último mod que instalaste.",
             "Turning off deletes nothing. If something stops working, turn off the last mod you installed."),
    "importer": ("Importar un personaje", "Importing a character",
                 ["Elige el juego de origen arriba.", "Elige el personaje de la lista.",
                  "Deja «Pies al suelo» marcado y pulsa «Importar».", "Pulsa «Reconstruir ahora» o JUGAR en el launcher."],
                 ["Pick the source game at the top.", "Pick the character in the list.",
                  "Keep 'Feet on the ground' ticked and press 'Import'.", "Press 'Rebuild now' or PLAY in the launcher."],
                 "Empieza por uno solo y pruébalo antes de importar más.",
                 "Start with just one and test it before importing more."),
    "characters": ("Retocar un personaje nuevo", "Adjusting a new character",
                   ["Elige el personaje en la lista.", "Cambia nombre, plaza, colores o cápsulas y guarda en cada tarjeta.",
                    "Mira la vista previa: icono y retratos salen de su modelo.", "Pulsa «Reconstruir ahora» para verlo en el juego."],
                   ["Pick the character in the list.", "Change name, slot, colours or capsules and save on each card.",
                    "Check the preview: icon and portraits come from its model.", "Press 'Rebuild now' to see it in game."],
                   "Las plazas de personajes que no uses son las mejores: no pierdes a nadie de la rueda.",
                   "Slots of characters you do not use are best: nobody is lost from the wheel."),
    "create": ("Crear con tus modelos", "Creating from your models",
               ["Escribe el nombre y elige un personaje base con cuerpo parecido.", "Añade tus archivos OBJ o glTF.",
                "Pulsa «Crear» y luego reconstruye."],
               ["Type the name and pick a base character with a similar body.", "Add your OBJ or glTF files.",
                "Press 'Create' and then rebuild."],
               "El modelo debe tener esqueleto con huesos parecidos. Si flota, usa «Pies al suelo» al importar.",
               "The model needs a skeleton with similar bones. If it floats, use 'Feet on the ground' when importing."),
    "stories": ("Hacer una historia o un reto", "Making a story or challenge",
                ["Para un reto rápido: elige modo y combates, y pulsa «Crear y montar».",
                 "Para una historia: rellena las pestañas en orden, de izquierda a derecha.",
                 "Pulsa «Probar» para buscar errores y «Montar mod» cuando esté bien."],
                ["For a quick challenge: pick a mode and fights, and press 'Create and build'.",
                 "For a story: fill the tabs in order, left to right.",
                 "Press 'Test' to find errors and 'Build mod' when it is fine."],
                "Guarda a menudo. «Rellenar traducciones» evita textos en blanco en otro idioma.",
                "Save often. 'Fill translations' avoids blank texts in another language."),
    "textures": ("Cambiar texturas", "Changing textures",
                 ["Elige el personaje.", "Pulsa «Extraer texturas a PNG»: abajo verás todas las imágenes.",
                  "Edítalas con cualquier programa, sin cambiar su tamaño.", "Pulsa «Reconstruir mod con texturas editadas»."],
                 ["Pick the character.", "Press 'Extract textures to PNG': you will see all the images below.",
                  "Edit them with any program, without changing their size.", "Press 'Rebuild mod with edited textures'."],
                 "La transparencia decide qué zona lleva sombra: si la cambias, comprueba el resultado. Una imagen con otro tamaño sale marcada en rojo.",
                 "Transparency decides which area gets shading: if you change it, check the result. An image with another size is marked in red."),
    "swap": ("Poner un modelo en el sitio de otro", "Putting one model in another's slot",
             ["Elige el personaje de origen: su aspecto.", "Elige el destino: a quién sustituye.",
              "Compara las dos imágenes y pulsa «Cambiar»."],
             ["Pick the source character: its look.", "Pick the target: who it replaces.",
              "Compare the two images and press 'Swap'."],
             "Conserva los golpes del destino. Elige cuerpos parecidos: un personaje mucho más grande puede atravesar el suelo o a su rival.",
             "It keeps the target's moves. Pick similar bodies: a much bigger character can clip through the floor or its rival."),
    "diag": ("Cuando algo va mal", "When something goes wrong",
             ["Abre el juego y deja que falle o vaya lento.", "Pulsa «Analizar mi último registro».",
              "Sigue lo que indica; si no se arregla, pulsa «Copiar informe» y pégalo en el Discord."],
             ["Open the game and let it fail or run slowly.", "Press 'Analyse my latest log'.",
              "Follow what it says; if it is not fixed, press 'Copy report' and paste it on Discord."],
             "Suele ser el último mod: prueba a apagarlo en «Mis mods».", "It is usually the last mod: try turning it off in 'My mods'."),
    "help": ("Dónde está cada guía", "Where each guide is", ["Elige una guía de la lista y pulsa «Leer»."],
             ["Pick a guide in the list and press 'Read'."],
             "El Discord es el sitio más rápido para preguntar.", "Discord is the fastest place to ask."),
    "ps2": ("Crear mods para Budokai 3 de PS2", "Making mods for PS2 Budokai 3",
            ["Elige tu copia: una .iso o la carpeta del disco (Greatest Hits USA, SLUS-20998).",
             "Crea un mod de textura, de personaje o de música.",
             "Enciende los que quieras y pulsa «Montar ISO»: se crea una ISO nueva y tu copia no se toca.",
             "Abre la ISO nueva en PCSX2 como un juego más. Empieza partida: una partida guardada del original sigue valiendo."],
            ["Pick your copy: an .iso or the disc folder (Greatest Hits USA, SLUS-20998).",
             "Create a texture, character or music mod.",
             "Turn on the ones you want and press 'Build ISO': a new ISO is created and your copy is untouched.",
             "Open the new ISO in PCSX2 like any game. Start playing: a save from the original keeps working."],
            "Probado en PCSX2. En consola real no se ha podido probar.", "Tested in PCSX2. Real consoles could not be tested."),
}


def abrir_como(page, g):
    """Ventana «¿Cómo se hace?» de una pantalla."""
    datos = COMO.get(page.key)
    if not datos:
        return
    T, es = g.T, g.LANG == "es"
    top = tk.Toplevel(page.app.root)
    top.title(T("¿Cómo se hace?", "How is it done?"))
    top.configure(bg=g.C["bg"])
    top.transient(page.app.root)
    caja = tk.Frame(top, bg=g.C["bg"])
    caja.pack(fill="both", expand=True, padx=22, pady=16)
    g.label(caja, datos[0] if es else datos[1], font=g.F["h2"]).pack(anchor="w", pady=(0, 10))
    for i, paso in enumerate(datos[2] if es else datos[3], 1):
        g.label(caja, "%d.  %s" % (i, paso), wrap=520).pack(anchor="w", pady=2)
    g.label(caja, T("Consejo", "Tip"), font=g.F["bold"], fg=g.C["accent"]).pack(anchor="w", pady=(12, 2))
    g.label(caja, datos[4] if es else datos[5], wrap=520, fg=g.C["dim"]).pack(anchor="w")
    bf = tk.Frame(caja, bg=g.C["bg"])
    bf.pack(fill="x", pady=(14, 0))
    ttk.Button(bf, text=T("Cerrar", "Close"), command=top.destroy).pack(side="right")
    ttk.Button(bf, text=T("Leer la guía completa", "Read the full guide"),
               command=lambda: page.app.goto("help")).pack(side="right", padx=6)
    return top

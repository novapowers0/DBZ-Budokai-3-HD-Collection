# -*- coding: utf-8 -*-
"""«En construcción» (KIT-05): tarjetas de la portada cuyo asistente aún no existe. Dice qué hará y enlaza a lo que
ya hay hoy. Una clase por clave (Page.key es de clase); se registran en modkit_gui.PAGE_CLASSES con OBRA_CLASES."""
from __future__ import annotations

from kit.nucleo import C, F, Card, Page, T, label, make_clickable, tk, icon

# clave -> (titulo es, en, que hara es, en, [(clave que abre, es, en, detalle es, en)])
OBRAS = {
    "obra_tecnica": (
        "Nueva técnica", "New technique",
        "Aquí elegirás a quién y qué técnica (por su nombre oficial), y la ajustarás en tres pestañas: el movimiento "
        "(con el generador de animaciones y su vista previa), la cámara (plantillas del Studio) y el color del aura y "
        "del ki.",
        "Here you will pick who and which technique (by its official name), and tune it in three tabs: the movement "
        "(with the animation generator and its preview), the camera (Studio templates) and the aura and ki colour.",
        [("studio", "Studio de cámaras", "Camera Studio", "Rehaz la cámara de una técnica.",
          "Redo a technique's camera."),
         ("biblioteca", "Biblioteca de animaciones", "Animation library",
          "Busca un movimiento, mira su pose y guarda una receta.", "Find a move, see its pose and save a recipe."),
         ("w_aspecto", "Color de aura y ki", "Aura and ki colour", "Asistente de aspecto y aura.",
          "Look and aura wizard.")]),
    "obra_escenario": (
        "Nuevo escenario", "New stage",
        "Aquí verás una foto de cada escenario que hay en tus juegos de PS2, marcados «probado» o «aún no funciona». "
        "Eliges uno, le pones nombre y ambiente, y queda en el selector de escenarios.",
        "Here you will see a photo of every stage in your PS2 games, marked 'tested' or 'not working yet'. You pick "
        "one, give it a name and mood, and it goes into the stage selector.",
        [("tools", "Herramientas expertas", "Expert tools",
          "escenarios_convertir.py ya convierte escenarios de B1 y B2 (modo Avanzado).",
          "escenarios_convertir.py already converts B1 and B2 stages (Advanced mode)."),
         ("arte", "Arte opcional", "Optional art", "Tus imágenes para el Dragon HUB y el modo Héroe.",
          "Your images for the Dragon HUB and Hero mode.")]),
    "obra_fusiones": (
        "Fusiones", "Fusions",
        "Aquí podrás elegir cuánto dura una fusión (corta, normal, larga o infinita), para todas o una a una, y añadir "
        "Gogeta, Vegito, Gotenks… a la rueda como personajes sueltos, sin tener que fusionarse en combate.",
        "Here you will choose how long a fusion lasts (short, normal, long or endless), for all or one by one, and add "
        "Gogeta, Vegito, Gotenks… to the wheel as standalone characters, without fusing in battle.",
        [("characters", "Personajes nuevos", "New characters",
          "Formas y transformación de los personajes que ya importaste.",
          "Forms and transformation of the characters you already imported."),
         ("w_personaje", "Añadir personaje", "Add a character", "Asistente para traer un luchador.",
          "Wizard to bring a fighter.")]),
    "obra_probar": (
        "Probar en el juego", "Test in the game",
        "Aquí abrirás el juego con tus mods y el Kit te dirá en palabras llanas qué se ha montado (personajes, trajes, "
        "escenarios, cámaras) y si algo no ha cargado.",
        "Here you will open the game with your mods and the Kit will tell you in plain words what was built "
        "(characters, costumes, stages, cameras) and whether anything failed to load.",
        [("mods", "Mis mods", "My mods", "Activa, apaga y revisa tus mods.", "Turn on, off and check your mods."),
         ("diag", "Algo no va bien", "Something is wrong", "Lee el registro del juego y te dice qué hacer.",
          "Reads the game log and tells you what to do.")]),
}


class ObraPage(Page):
    key = ""

    def build(self):
        es, en, des, den, enlaces = OBRAS[self.key]
        self.header(T(es, en), T("En construcción: este asistente llega en una próxima versión del Kit.",
                                 "Under construction: this wizard arrives in a coming Kit version."))
        _sf, body = self.scroll_body()
        card = Card(body, accent=True)
        card.pack(fill="x", pady=(4, 14))
        label(card.body, T("Qué hará", "What it will do"), font=F["h3"], bg=C["card"]).pack(fill="x")
        label(card.body, T(des, den), fg=C["dim"], bg=C["card"], wrap=860).pack(fill="x", pady=(4, 0))
        label(body, T("Mientras tanto, puedes usar", "Meanwhile, you can use"), font=F["h2"]).pack(fill="x",
                                                                                                 pady=(0, 6))
        for key, les, len_, ldes, lden in enlaces:
            f = tk.Frame(body, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
            f.pack(fill="x", pady=4)
            tk.Label(f, text=icon(key), bg=C["card"], fg=C["accent"], font=F["icon_sm"]).pack(side="left",
                                                                                            padx=(14, 10), pady=12)
            label(f, T(les, len_), font=F["bold"], bg=C["card"]).pack(side="left")
            label(f, "  ·  " + T(ldes, lden), fg=C["dim"], bg=C["card"]).pack(side="left")
            make_clickable(f, lambda k=key: self.app.goto(k), C["card"], C["card_hi"])


OBRA_CLASES = {k: type("ObraPage_" + k, (ObraPage,), {"key": k}) for k in OBRAS}

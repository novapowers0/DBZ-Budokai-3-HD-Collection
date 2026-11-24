# -*- coding: utf-8 -*-
"""Modo B3 PS2 (la pantalla la construye kit/pagina_ps2.py). Sacado tal cual de modkit_gui.py (KIT-05)."""
from __future__ import annotations

from kit import nucleo, pagina_ps2
from kit.nucleo import Page


class Ps2Page(Page):
    key = "ps2"

    def __init__(self, master, app, _adv):
        super().__init__(master, app, True)     # sin boton de Inicio: el selector de arriba hace de salida

    def build(self):
        self.ps2 = pagina_ps2.PaginaPs2(self, nucleo.G)

    def on_show(self):
        self.ps2.refrescar()

# -*- coding: utf-8 -*-
"""Portada del modo facil (la construye kit/portada.py).

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


class HomePage(Page):
    key = "home"

    def build(self):
        portada.construir(self, nucleo.G)

    def on_show(self):
        portada.al_mostrar(self, nucleo.G)


# --------------------------------------------------------------------------------- comprobar

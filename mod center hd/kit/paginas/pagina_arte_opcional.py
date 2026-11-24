# -*- coding: utf-8 -*-
"""Arte opcional (la pantalla la construye kit/pagina_arte.py). Sacado tal cual de modkit_gui.py (KIT-05)."""
from __future__ import annotations

from kit import nucleo, pagina_arte
from kit.nucleo import Page


class ArtePage(Page):
    key = "arte"

    def build(self):
        self.arte = pagina_arte.PaginaArte(self, nucleo.G)

    def on_show(self):
        self.arte.refrescar()

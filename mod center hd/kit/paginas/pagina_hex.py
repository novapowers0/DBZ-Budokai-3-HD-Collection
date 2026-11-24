# -*- coding: utf-8 -*-
"""Editor hexadecimal (hexedit.py).

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


class HexPage(Page):
    """Editor hexadecimal (hexedit.py) para modders expertos: sabe de #AMB/#AMO y guarda con copia."""
    key = "hex"

    def build(self):
        self.header(T("Editor hexadecimal", "Hex editor"),
                    T("Para modders expertos: edita bytes como en HxD. El árbol de la izquierda entiende los #AMB "
                      "(PS2 en little endian, HD en big endian) y las partes de los #AMO (ocultar una parte para "
                      "un traje alternativo). Al guardar se hace una copia de seguridad.",
                      "For expert modders: edit bytes like in HxD. The tree on the left understands #AMB (PS2 "
                      "little endian, HD big endian) and #AMO parts (hide a part for an alternate costume). "
                      "Saving makes a backup first."))
        try:
            import hexedit  # noqa: PLC0415
            hexedit.COLORS.update(bg=C["bg"], card=C["card"], text=C["text"], dim=C["dim"], accent=C["accent"],
                                  err=C["err"])
            self.ed = hexedit.HexEditor(self, fonts=F, tr=T)
            self.ed.pack(fill="both", expand=True, padx=22, pady=(0, 10))
        except Exception as ex:  # noqa: BLE001
            label(self, T("No se pudo cargar el editor: %s", "Could not load the editor: %s") % ex,
                  fg=C["err"]).pack(fill="x", padx=22)

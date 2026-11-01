# -*- coding: utf-8 -*-
"""Nombres de tecnicas leidos de los rotulos bf*.amt de SB1/SB2 del jugador.

Solo texto transcrito; ninguna imagen ni audio. Clave = personaje y codigo del
BCM, NO posicion de la tecnica ni numero de capsula provisional. Los codigos
compartidos entre ambos juegos tienen el mismo nombre. Revision solCB 1/11.
"""
import re

NOMBRES = {
    "18G": {0x460: "Destructo Disc", 0x464: "Energy Field", 0x46D: "Accel Dance"},
    "BDK": {0x460: "Riot Javelin", 0x46A: "Heat Phalanx", 0x474: "Saiyan Spirit"},
    "BRL": {0x460: "Blaster Shell", 0x470: "Blaster Shell", 0x464: "Gigantic Press",
            0x46D: "Gigantic Meteor", 0x473: "Gigantic Meteor", 0x488: "Gigantic Claw"},
    "BUL": {0x460: "Innocence Cannon", 0x464: "Innocence Express", 0x475: "Angry Explosion"},
    "BUM": {0x460: "Ill Flash", 0x464: "Ill Ball Attack", 0x468: "Victory Cannon",
            0x46B: "Galactica Donuts", 0x46E: "Kamehameha", 0x475: "Genocide Rain",
            0x478: "DIEDIE Missile Barrage", 0x47B: "Super Kamehameha", 0x484: "Evasive Kick"},
    "BUS": {0x460: "Assault Rain", 0x469: "Vanishing Ball", 0x46C: "Kamehameha"},
    "CEL": {0x460: "Kamehameha", 0x464: "Energy Field", 0x46D: "Death Beam",
            0x471: "Super Kamehameha", 0x473: "Warp Kamehameha", 0x475: "Tail Shoot"},
    "COO": {0x460: "Destructive Ray", 0x478: "Destructive Ray", 0x468: "Sauzer Blade", 0x465: "Supernova"},
    "DBR": {0x460: "Hell Blitz", 0x464: "Evil Blast", 0x492: "Hell Blade Rush"},
    "FRZ": {0x460: "Death Beam", 0x46C: "Death Slicer", 0x464: "Death Wave",
            0x468: "Death Crasher", 0x479: "Death Ball"},
    "GGT": {0x460: "Soul Strike", 0x469: "Big Bang Kamehameha", 0x46E: "Soul Punisher"},
    "GHF": {0x460: "Kamehameha", 0x464: "Super Kamehameha", 0x488: "Spirit Shot",
            0x48B: "Evasive Kick", 0x491: "Masenko", 0x495: "Z Sword",
            0x499: "Special Beam Cannon", 0x49C: "Burning Attack"},
    "GHL": {0x460: "Kamehameha", 0x464: "Super Kamehameha", 0x467: "Soaring Dragon Strike",
            0x488: "Justice Punch", 0x48B: "Kamehameha", 0x48E: "Z Sword", 0x491: "Justice Kick"},
    "GHM": {0x460: "Kamehameha", 0x464: "Super Kamehameha", 0x468: "Masenko",
            0x46F: "Soaring Dragon Strike", 0x492: "Full Power Energy Wave Barrage"},
    "GOK": {0x460: "Kamehameha", 0x464: "Spirit Shot", 0x468: "Super Dragon Fist",
            0x46C: "Kamehameha", 0x471: "Kamehameha", 0x475: "Spirit Bomb",
            0x478: "Warp Kamehameha", 0x48B: "x10 Kamehameha", 0x492: "Dragon Fist"},
    "GTX": {0x460: "Kamehameha", 0x464: "Victory Cannon", 0x468: "Galactica Donuts",
            0x471: "Super Ghost Kamikaze Attack", 0x488: "DIEDIE Missile Barrage"},
    "JNB": {0x460: "Hell Gate", 0x469: "Rakshasa's Claw", 0x471: "Needle Mountain Hell"},
    "KLL": {0x460: "Kamehameha", 0x464: "Sun Fist", 0x468: "Destructo Disc",
            0x471: "Scatter Kamehameha", 0x474: "Fierce Destructo Disc"},
    "PIC": {0x460: "Destructive Wave", 0x464: "Special Beam Cannon", 0x468: "Light Grenade",
            0x471: "Maou Shot", 0x476: "Hellzone Grenade", 0x488: "Explosive Wave"},
    "PKH": {0x460: "Hyper Tornado", 0x469: "Thunder Rush", 0x46C: "Blazing Shoot"},
    "TRF": {0x464: "Finish Buster", 0x476: "Heat Dome Attack", 0x479: "Evasive Kick",
            0x47F: "Burning Attack", 0x482: "Cyclone Buster"},
    # El rotulo de SB1 escribe "Finish Bustert"; se usa el nombre oficial sin la errata final.
    "TRX": {0x45C: "Masenko", 0x460: "Buster Cannon", 0x464: "Finish Buster",
            0x468: "Shining Slash", 0x471: "Burning Slash", 0x476: "Heat Dome Attack"},
    "VGT": {0x460: "Galick Gun", 0x46C: "Galick Gun", 0x464: "Big Bang Attack",
            0x468: "Final Impact", 0x474: "Final Explosion", 0x47D: "Final Flash",
            0x480: "Atomic Blast", 0x48B: "Full Power Energy Wave Barrage", 0x48D: "Atomic Rush"},
    "VTO": {0x460: "Final Kamehameha", 0x469: "Savage Counter", 0x46D: "Spirit Sword"},
}


def oficial(personaje, codigo):
    return NOMBRES.get(personaje.upper(), {}).get(codigo)


_CAP = re.compile(r"(?m)^\[\[capsula\]\][^\n]*(?:\n|$)")
_NAME = re.compile(r'(?m)^(nombre\s*=\s*)"([^"\n]*)"')
_CODE = re.compile(r"\bSB\s+(0x[0-9a-f]+)\b", re.I)
_GENERIC = re.compile(r"(?:Special\s+\d+|Ultimate|Transformation|\?)", re.I)


def actualizar_toml(texto, personaje, nombre_personaje=None):
    """Solo sustituye nombres genericos con codigo SB documentado en su propio encabezado.

    No reordena bloques, no cambia opciones, no adivina por reemplaza y conserva nombres
    puestos a mano. Devuelve texto y numero de cambios. Una segunda pasada es inocua.
    """
    headers = list(_CAP.finditer(texto))
    edits = []
    for i, header in enumerate(headers):
        code = _CODE.search(header.group())
        if not code:
            continue
        end = headers[i + 1].start() if i + 1 < len(headers) else len(texto)
        # Otro tipo de seccion termina tambien la capsula.
        block = texto[header.end():end]
        section = re.search(r"(?m)^\[", block)
        if section:
            block = block[:section.start()]
        name = _NAME.search(block)
        official = oficial(personaje, int(code.group(1), 16))
        current = name.group(2) if name else ""
        if nombre_personaje and current.lower().startswith(nombre_personaje.lower() + " "):
            current = current[len(nombre_personaje) + 1:]
        if not name or not official or not _GENERIC.fullmatch(current):
            continue
        begin = header.end() + name.start(2)
        edits.append((begin, header.end() + name.end(2), official))
    for begin, end, official in reversed(edits):
        texto = texto[:begin] + official + texto[end:]
    return texto, len(edits)

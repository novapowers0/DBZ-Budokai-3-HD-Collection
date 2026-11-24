#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""skin_slots.py - Dominio de huesos del VERTEX B3 HD: SLOT DE PALETA, no indice de esqueleto.

HALLAZGO (RE 2026-10-03, verificado en runtime; ver HANDOFF_CLAUDE_2026-10-03):
  El campo `bone` (u32 BE @+16 de la ventana de 44 B) que consume el vertex shader
  de skinning de la GPU (`vfetch ... vf1` = paleta de 48 B/entrada, indexada por el
  byte bajo de `bone`) es un SLOT DE PALETA:

      slot(b) = 1 + (numero de huesos con flag SKIN_FLAG anteriores a b)

  donde el flag es el bit 0x10000000 de la palabra u32 en `eje+0x30` (ejes de 80 B,
  `axes_base = AWG0 + g(0x14)`). Los huesos SIN ese flag (raiz XXX_BODY, nulos
  X*_N??, ROT de mano, partes rigidas L00 de mano/cara...) NO tienen slot.
  `bone_of[slot]` es la inversa.  slot 0 no se usa (entrada de paleta a cero).

  ⚠️ CORRECCION (RE en runtime 2026-10-03, sub_8208F658): la fuente de verdad es la
  TABLA DE PALETA del propio AWG0: puntero rel AWG0 en +0x38, n entradas en +0x3C,
  u32 BE `tabla[slot] = hueso` (slot 0 = FFFFFFFF). El guest escribe palette[k]
  para el hueso tabla[k]. La regla de flags de arriba solo es una aproximacion:
  falla en 74/185 personajes (p.ej. Cell: CEL_T_TAIL1 tiene el flag pero NO esta
  en la tabla -> todos los slots siguientes se desplazan uno). SkinSlots lee la
  tabla y solo cae a la regla de flags si no hay tabla valida.

  Los vertices estan en el marco local del hueso de ESQUELETO `bone_of[slot]`:
      pos_local = inv(world[bone_of[slot]]) . pos_model         (bind-pose)
  y a `weight == 1.0` la entrada de paleta == matriz de pose de ese hueso
  (verificado: error 0.00000 contra la matriz c8..c11 de un draw rigido).

Uso tipico (conversion al escribir una ventana):
    info = SkinSlots(avb)                    # avb = AwgVertexBuffer ya cargado
    eff  = info.effective_bone(hd_bone)      # hueso con slot (ancestro si hd_bone no skinnea)
    slot = info.slot_of[eff]
    # local = inv(world[eff]) . model ; ventana.bone = slot
"""
import struct

SKIN_FLAG = 0x10000000


def bone_flags(avb):
    """Palabra de flags (u32 BE @ eje+0x30) de cada hueso del AWG0."""
    b = avb.data
    a0 = avb.awg0
    axes_base = a0 + struct.unpack(">I", bytes(b[a0 + 0x14:a0 + 0x18]))[0]
    return [struct.unpack(">I", bytes(b[axes_base + i * 80 + 0x30:
                                        axes_base + i * 80 + 0x34]))[0]
            for i in range(avb.bone_count())]


def palette_table(avb):
    """tabla[slot] = hueso (AWG0+0x38 puntero rel, +0x3C n). None si no es valida."""
    b, a0 = avb.data, avb.awg0
    p = a0 + struct.unpack(">I", bytes(b[a0 + 0x38:a0 + 0x3C]))[0]
    n = struct.unpack(">I", bytes(b[a0 + 0x3C:a0 + 0x40]))[0]
    if not 1 <= n <= 128 or p + 4 * n > len(b):
        return None
    t = list(struct.unpack(">%dI" % n, bytes(b[p:p + 4 * n])))
    nb = avb.bone_count()
    # 12/11 (Shenron 534, #AMB de 3 hijos): tabla SIN el FFFFFFFF del slot 0 (1, 2, 3...). El guest escribe palette[k]
    # para tabla[k] desde k = 0, asi que ahi el slot 0 tambien es un hueso (SkinSlots empieza en 0). Antes se rechazaba
    # -> regla de flags -> un hueso de desfase en toda la malla (el triangulo estirado del Shenron HD).
    if t[0] != 0xFFFFFFFF:
        return t if all(x < nb for x in t) and t == sorted(set(t)) else None
    # La tabla indexa slots: al ampliar una paleta se añaden huesos al final,
    # sin ordenar ni cambiar los indices que ya usan los vertices del modelo.
    if any(x >= nb for x in t[1:]) or len(t[1:]) != len(set(t[1:])):
        return None
    return t


class SkinSlots:
    def __init__(self, avb):
        self.flags = bone_flags(avb)
        _, self.parents = avb.bind_worlds()
        self.slot_of = {}     # hueso de esqueleto -> slot de paleta (1..)
        self.bone_of = {}     # slot de paleta -> hueso de esqueleto
        self.table = palette_table(avb)
        primero = 1
        if self.table:
            primero = 1 if self.table[0] == 0xFFFFFFFF else 0   # sin centinela: el slot 0 es tabla[0]
            bones = self.table[primero:]
            self.source = "tabla"
        else:
            bones = [i for i, f in enumerate(self.flags) if f & SKIN_FLAG]
            self.source = "flags"
        for s, i in enumerate(bones, primero):
            self.slot_of[i] = s
            self.bone_of[s] = i
        self.n_slots = len(bones)

    def skins(self, b):
        return b in self.slot_of

    def effective_bone(self, b):
        """b si tiene slot; si no, su ancestro mas cercano con slot; si no hay, el
        primer hueso con slot (el vertice no queda sin marco)."""
        seen = 0
        while b >= 0 and b not in self.slot_of and seen < 256:
            b = self.parents[b]
            seen += 1
        if b < 0 or b not in self.slot_of:
            b = self.bone_of.get(1, 0)
        return b

    def slot_for(self, b):
        return self.slot_of[self.effective_bone(b)]

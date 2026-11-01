#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""GOK/VGT/PIC de XV2 (copia local) -> #AMB B3 HD, sin modificar los juegos.

Usa los lectores xeno_* y los escritores AWG/AWO/AZT de sdbh_model/amo2awo.
Conserva el rig B3: las orientaciones ESK de las extremidades NO son las de B3.
Rebind por segmentos anatomicos en espacio de modelo, luego local del hueso B3.
Hornea gestos EAN y emite cadenas SCD/ESK para el controlador XV5S propio.
No emula los coeficientes de fisica XV2. El material DYT es una aproximacion offline,
no una implementacion de los shaders de XV2 ni una afirmacion de fidelidad 1:1.
--selftest no abre juegos ni escribe archivos.
"""
import argparse
import copy
import hashlib
import io
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

from xeno_binary import rejects, write_json
from xeno_model import Cpk, Esk, leer_emb, leer_emd
from xeno_emm import leer_emm
from xeno_bcs import leer_bcs, PART_TYPES
from xeno_rig import ALIASES, extraer_b3
import sdbh_model as S


# Hijos anatomicos, no los ejes locales del ESK (en XV2 la pierna es identidad).
NEXT = {"b_C_Pelvis": "b_C_Spine1", "b_C_Spine1": "b_C_Chest",
        "b_C_Chest": "b_C_Neck1", "b_C_Neck1": "b_C_Head"}
for _side in "LR":
    for _a, _b in (("Shoulder", "Arm1"), ("Arm1", "Arm2"), ("Arm2", "Hand"),
                   ("Leg1", "Leg2"), ("Leg2", "Foot"), ("Foot", "Toe")):
        NEXT["b_%s_%s" % (_side, _a)] = "b_%s_%s" % (_side, _b)

# Mitad baja de la cara: B3 la mueve con huesos (M_JAW, comisuras), no con variantes. Comprobado en el Goku nativo (paleta y
# malla de cara pielada); la cara XV2 rigida dejaba la boca sin moverse.
BOCA = {"f_C_Jaw": "M_JAW", "f_C_MouthBottom": "M_JAW", "f_C_ToothBottom": "M_JAW", "f_C_Tongue1": "M_JAW", "f_C_Tongue2": "M_JAW",
        "f_C_Tongue3": "M_JAW", "f_C_Tongue4": "M_JAW", "f_L_MouthBottom": "M_JAW", "f_R_MouthBottom": "M_JAW",
        "f_L_MouthCorners": "M_LMOUTH1", "f_R_MouthCorners": "M_RMOUTH1"}

CHARACTERS = {"GOK": ("Goku", 0, 264), "VGT": ("Vegeta", 7, 416), "PIC": ("Piccolo", 11, 350)}


def order(parents):
    """Orden topologico con comprobacion completa de padres/ciclos."""
    out, state = [], [0] * len(parents)

    def visit(i):
        if not 0 <= i < len(parents) or state[i] == 1:
            raise ValueError("rig: padre invalido o ciclo")
        if state[i] == 2:
            return
        state[i] = 1
        p = parents[i]
        if p != -1:
            visit(p)
        state[i] = 2
        out.append(i)
    for i in range(len(parents)):
        visit(i)
    return out


def ancestors(parents, child):
    seen = set()
    while child != -1:
        if child in seen or not 0 <= child < len(parents):
            raise ValueError("rig: ciclo o padre invalido")
        seen.add(child)
        yield child
        child = parents[child]


def segment_matrix(a, b, x, y):
    """Afine minimo: a->x, b->y; estira SOLO el eje del segmento.

    No usa las bases ESK/B3 como si fueran iguales. Conserva el grosor de la
    malla y alinea el eje por el giro minimo; antiparalelos tambien definidos.
    """
    a, b, x, y = (np.asarray(v, float) for v in (a, b, x, y))
    u, v = b - a, y - x
    lu, lv = np.linalg.norm(u), np.linalg.norm(v)
    if min(lu, lv) < 1e-7:
        raise ValueError("retarget: segmento de longitud cero")
    u, v = u / lu, v / lv
    c, cross = float(np.clip(u @ v, -1, 1)), np.cross(u, v)
    if c < -1 + 1e-8:
        axis = np.eye(3)[int(np.argmin(np.abs(u)))]
        axis = np.cross(u, axis)
        axis /= np.linalg.norm(axis)
        R = 2 * np.outer(axis, axis) - np.eye(3)
    else:
        K = np.array([[0, -cross[2], cross[1]], [cross[2], 0, -cross[0]],
                      [-cross[1], cross[0], 0]])
        R = np.eye(3) + K + K @ K / (1 + c)
    M = np.eye(4)
    M[:3, :3] = R @ (np.eye(3) + (lv / lu - 1) * np.outer(u, u))
    M[:3, 3] = x - M[:3, :3] @ a
    return M


class Retarget:
    """Mapeo auditable; conserva ejes, jerarquia y paleta del donante B3."""

    def __init__(self, esk, tpl):
        order(esk.parent)
        order(tpl.parent)
        if len(set(esk.names)) != len(esk.names):
            raise ValueError("ESK: nombres de hueso duplicados")
        if esk.inverse_bind_error is not None and esk.inverse_bind_error > 1e-4:
            raise ValueError("ESK: TRS no coincide con las inversas bind originales")
        self.esk, self.tpl = esk, tpl
        self.W = tpl.world()
        self.invW = [np.linalg.inv(w) for w in self.W]
        self.direct = {}
        for name, suffix in ALIASES.items():
            i, t = esk.index.get(name), tpl.bone(suffix)
            if i is not None and t is not None and name != "f_C_Jaw":
                self.direct[i] = t
        # Todos los segmentos del tronco/extremidades tienen que estar presentes.
        required = set(NEXT) | set(NEXT.values()) | {"b_C_Base"}
        if any(esk.index.get(n) not in self.direct for n in required):
            raise ValueError("retarget: faltan huesos anatomicos obligatorios")
        self.matrices, checks = {}, []
        for name in sorted(required):
            i, t = esk.index[name], self.direct[esk.index[name]]
            p = next((p for p in list(ancestors(esk.parent, i))[1:] if p in self.direct), None)
            if p is not None and self.direct[p] not in set(ancestors(tpl.parent, t)):
                raise ValueError("retarget: jerarquia incompatible: %s -> %s" % (name, tpl.labels[t]))
            M = np.eye(4)
            M[:3, 3] = self.W[t][:3, 3] - esk.world[i]
            child = NEXT.get(name)
            residual = 0.0
            if child:
                j = esk.index[child]
                U, V = self.W[t][:3, 3], self.W[self.direct[j]][:3, 3]
                if name in ("b_C_Chest", "b_C_Neck1"):
                    # Pecho-cabeza como un solo tramo: mide lo mismo en XV2 y B3 (3,12 / 3,06), pero el cuello cae en otro sitio
                    # (pecho-cuello 1,46 / 2,30; cuello-cabeza 1,66 / 0,77). Por tramos, el cuello de las prendas subia un 57 %
                    # y la cabeza se hundia hasta la boca.
                    a0, b0 = esk.index["b_C_Chest"], esk.index["b_C_Head"]
                    M = segment_matrix(esk.world[a0], esk.world[b0], self.W[self.direct[a0]][:3, 3], self.W[self.direct[b0]][:3, 3])
                    residual = float(np.linalg.norm(M[:3, :3] @ esk.world[b0] + M[:3, 3] - self.W[self.direct[b0]][:3, 3]))
                    self.matrices[name] = M
                    checks.append(dict(source=name, target=tpl.labels[t], target_index=t,
                                       source_parent=esk.names[p] if p is not None else None,
                                       hierarchy_ok=True, endpoint_error=residual))
                    continue
                M = segment_matrix(esk.world[i], esk.world[j], U, V)
                residual = float(np.linalg.norm(M[:3, :3] @ esk.world[j] + M[:3, 3] - V))
            self.matrices[name] = M
            checks.append(dict(source=name, target=tpl.labels[t], target_index=t,
                               source_parent=esk.names[p] if p is not None else None,
                               hierarchy_ok=True, endpoint_error=residual))
        # El rostro no se fragmenta trasladando cada pivote: toda la cara conserva
        # la forma de XV2. Los labios y dedos no tienen animacion equivalente 1:1.
        self.target, self.anchor, self.missing = {}, {}, []
        for i, name in enumerate(esk.names):
            anchor, t, reason = self._resolve(i)
            self.anchor[name], self.target[name] = anchor, t
            if i not in self.direct:
                self.missing.append(dict(source_index=i, source=name, reason=reason,
                                         target=tpl.labels[t] if t is not None else None,
                                         geometry_anchor=anchor))
        self.report = dict(status="offline_hierarchy_and_bind_validated_runtime_separate",
                           source_bones=len(esk.names), target_bones=tpl.nb,
                           direct_matches=len(self.direct), unmatched=self.missing,
                           checks=checks, inverse_bind_error=esk.inverse_bind_error,
                           endpoint_max_error=max(c["endpoint_error"] for c in checks),
                           b3_axes_preserved=True,
                            limitations=["B3: un hueso+mezcla implicita con padre; no 4 influencias",
                                          "Dedos/gestos horneados en variantes; sin reloj EAN; SCD usa controlador propio [H]"])

    def _resolve(self, i):
        e, name = self.esk, self.esk.names[i]
        if i in self.direct and name != "f_C_Jaw":
            return name, self.direct[i], "direct"
        # Los helpers son hermanos, no padres, del joint que corrigen en XV2.
        for side in "LR":
            helper = {"Knee": "Leg2", "Elbow": "Arm2", "ArmHelper": "Arm1",
                      "ArmorParts": "Arm1", "ArmRoll": "Arm2", "LegHelper": "Leg1"}
            for suffix, target in helper.items():
                if name == "b_%s_%s" % (side, suffix):
                    anchor = "b_%s_%s" % (side, target)
                    return anchor, self.direct[e.index[anchor]], "helper XV2 sin controlador equivalente; joint anatomico"
        if name in BOCA and self.tpl.bone(BOCA[name]) is not None:
            return "b_C_Head", self.tpl.bone(BOCA[name]), "boca: hueso B3 equivalente, sin mover la geometria"
        if name.startswith("f_"):
            return "b_C_Head", self.direct[e.index["b_C_Head"]], "rostro sin controlador equivalente; rigido a cabeza"
        for p in ancestors(e.parent, i):
            if p in self.direct:
                return e.names[p], self.direct[p], ("dedos sin huesos B3; horneados a mano" if name.startswith("h_")
                                                    else "sin equivalente; ancestro anatomico")
        return "b_C_Base", self.direct[e.index["b_C_Base"]], "nodo exportacion/camara; no deformador"

    def geometry(self, sm, attachment=None):
        """Rebind LBS de 4 influencias en model-space (antes de reducir para B3)."""
        P, N = np.zeros_like(sm.pos), np.zeros_like(sm.nrm)
        weights = np.clip(sm.weights, 0, None)
        sums = weights.sum(1)
        if np.any(sums < 1e-8) or not np.isfinite(weights).all():
            raise ValueError("EMD: pesos invalidos")
        weights = weights / sums[:, None]
        for k in range(4):
            for bi in np.unique(sm.bones[weights[:, k] > 0, k]):
                ids = np.where((sm.bones[:, k] == bi) & (weights[:, k] > 0))[0]
                name = sm.bone_names[bi]
                anchor = attachment or self.anchor.get(name)
                if anchor is None:
                    raise ValueError("EMD: hueso sin mapeo explicito: %s" % name)
                M = self.matrices[anchor]
                A = M[:3, :3]
                pos = sm.pos[ids]
                # Las piezas SCD estan en el marco de su attachment, NO en el
                # espacio global del EMD del cuerpo (pelo alrededor de y=0).
                if attachment:
                    W = self.esk.world_matrix[self.esk.index[attachment]]
                    pos = pos @ W[:3, :3].T + W[:3, 3]
                    normal = sm.nrm[ids] @ np.linalg.inv(W[:3, :3])
                else:
                    normal = sm.nrm[ids]
                w = weights[ids, k, None]
                P[ids] += (pos @ A.T + M[:3, 3]) * w
                N[ids] += (normal @ np.linalg.inv(A)) * w
        N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
        if not np.isfinite(P).all() or not np.isfinite(N).all():
            raise ValueError("retarget: geometria no finita")
        result = copy.copy(sm)
        result.pos, result.nrm, result.weights = P, N, weights
        # UV de XV2 en [-1,0]: desplazamiento entero, NO espejo vertical.
        result.uv = sm.uv - np.floor(sm.uv.mean(0)) if len(sm.uv) else sm.uv.copy()
        return result


def compact(sm, triangles):
    out = copy.copy(sm)
    triangles = np.asarray(triangles, int).reshape(-1, 3)
    used, back = np.unique(triangles, return_inverse=True)
    for key in ("pos", "nrm", "uv", "bones", "weights"):
        setattr(out, key, getattr(sm, key)[used])
    out.tris = back.reshape(-1, 3)
    return out


def remap_weights(rig, sm, slots, suavizar=False):
    """Reduce cuatro influencias a la representacion B3; mide la masa descartada."""
    bones, weights, dropped = [], [], []
    for k in range(len(sm.pos)):
        infl = {}
        for bi, w in zip(sm.bones[k], sm.weights[k]):
            if w <= 0:
                continue
            name = sm.bone_names[bi]
            if name not in rig.target:
                raise ValueError("peso: hueso EMD desconocido: %s" % name)
            t = rig.target[name]
            t = next((a for a in ancestors(rig.tpl.parent, t) if a in slots), None)
            if t is None:
                raise ValueError("peso: no hay slot B3 para %s" % name)
            infl[t] = infl.get(t, 0.0) + float(w)
        best, mass, weight = None, -1.0, 1.0
        for b, bw in sorted(infl.items()):
            # Semantica que usa tambien sdbh_model: el segundo peso solo puede
            # seguir la cadena de padres (a lo sumo un ROT intercalado).
            p = rig.tpl.parent[b]
            parent = next((a for a in ancestors(rig.tpl.parent, p) if a in slots), None) if p >= 0 else None
            pw = infl.get(parent, 0.0)
            if bw + pw > mass:
                best, mass, weight = b, bw + pw, bw / (bw + pw)
        if best is None:
            raise ValueError("peso: vertice sin influencias")
        if suavizar and mass < 0.75 and len(infl) > 1:
            # Dos influencias hermanas (muslo + estomago en el faldon) no caben en B3 (solo hueso + su padre): se perdia la
            # mitad y el faldon se abria en picos dejando ver el pantalon. El vertice sigue al ancestro comun de lo que pesa.
            a, b = sorted(infl, key=lambda k: -infl[k])[:2]
            cadena_b = set(ancestors(rig.tpl.parent, b))
            comun = next((x for x in ancestors(rig.tpl.parent, a) if x in cadena_b and x in slots), None)
            if comun is not None:
                total = sum(v for k, v in infl.items() if comun in set(ancestors(rig.tpl.parent, k)))
                if total > mass + 0.15:
                    best, mass, weight = comun, total, 1.0
        bones.append(best)
        weights.append(weight)
        dropped.append(max(0.0, 1.0 - mass))
    return np.array(bones, int), np.array(weights), np.array(dropped)


def faldon_degradado(rig, pos, bs, ws):
    """Faldon de la camisa sobre la cadera: pesos suaves en vez de los del vertice vecino del pantalon (que variaban de un vertice a
    otro y estiraban los triangulos al levantar la pierna). Cada vertice por debajo de la cadera sigue a la pierna de su lado
    segun cuanto baja y cuanto se aleja del centro; el resto, al hueso WAIST. B3 permite un hueso + su padre: pierna + cadera."""
    lab = rig.tpl.labels
    idx = {n: i for i, n in enumerate(lab)}
    try:
        legs = (idx["GOK_RLEG1"], idx["GOK_LLEG1"], idx["GOK_WAIST"])
    except KeyError:
        return bs, ws
    W = rig.W
    bs, ws = bs.copy(), ws.copy()
    sm_ = lambda t: t * t * (3 - 2 * t)
    for k in range(len(pos)):
        x, y = pos[k, 0], pos[k, 1]
        side = legs[0] if W[legs[0]][0, 3] * x > 0 else legs[1]
        px, py = W[side][0, 3], W[side][1, 3]
        down = sm_(float(np.clip((py - y) / 1.6, 0, 1)))
        lat = sm_(float(np.clip((abs(x) - 0.25 * abs(px)) / (0.75 * abs(px)), 0, 1)))
        wl = down * lat
        if y >= py:
            bs[k], ws[k] = legs[2], 1.0
        else:
            bs[k], ws[k] = side, max(wl, 1e-3)
    return bs, ws


def material_image(base, dyt, material, cap, subtract_ramp=False, tint=None, zoom=1, pelo=False):
    """Textura B3 de un material DFD de XV2 (R,G,B = manchas de combate, inactivas; A = tinta). El alfa B3 es control de
    sombreado, no el del DFD. zoom: la tinta se umbraliza a mas resolucion, para bordes limpios de cerca (como hace el shader)."""
    if material.get("legends"):     # fuente DAE (dae_a_b3): textura ya compilada, sin DYT
        from dae_a_b3 import imagen_legends
        return imagen_legends(base, cap, material.get("tono"))
    pars = {p["name"]: p["value"] for p in material["parameters"]}
    if base is None or dyt is None:
        raise ValueError("material: falta textura DDS base o DYT")
    h, w = base.shape[:2]
    scale = min(1.0, cap / max(h, w))
    if scale < 1:
        w, h = max(4, int(w * scale) // 4 * 4), max(4, int(h * scale) // 4 * 4)
        base = S.resize_rgba(base, w, h)
    elif zoom > 1 and max(h, w) * zoom <= cap:
        w, h = w * zoom, h * zoom
        base = S.resize_rgba(base, w, h)
    row = int(round(pars.get("MatScale1X", 0))) * 16
    if not 0 <= row < dyt.shape[0]:
        raise ValueError("material: bloque DYT fuera de rango")
    x = int(round(0.82 * (dyt.shape[1] - 1)))
    color = dyt[row, x, :3].astype(float)
    shader = material["shader"]
    alpha = base[..., 3].astype(float) / 255.0
    if "EYE" in shader:
        # Medido en GOK/VGT/HUM/FRI/NMC/MAF: alfa > MatCol0A = tinta negra (pupila y aro), B = anillo del iris.
        # GOK y HUM_000 traen R=G=B en el disco: exigir B bajo para la pupila la borraba (iris gris sin pupila).
        white = np.array([pars.get("MatCol3" + c, 1.0) for c in "RGB"]) * 255
        iris = dyt[row, int(0.05 * (dyt.shape[1] - 1)), :3].astype(float)
        mut3 = "MUT3" in shader    # R = color MatCol1 (esclerotica negra de la Majin, sombra de la pupila en HUF), G = brillo MatCol2
        if mut3:
            white, iris = np.full(3, 255.), np.array((205., 25, 35))
        if tint is not None:
            iris = np.asarray(tint, float) * 255
        ring = base[..., 2, None].astype(float) / 255
        pupil = np.clip((alpha - pars.get("MatCol0A", .6)) / .04, 0, 1)[..., None]
        rgb = white * (1 - ring) + iris * ring
        if mut3:
            r = base[..., 0, None].astype(float) / 255
            rgb = rgb * (1 - r) + np.array([pars.get("MatCol1" + c, 0.0) for c in "RGB"]) * 255 * r
        rgb = rgb * (1 - pupil) + 8 * pupil
        if mut3:
            g = base[..., 1, None].astype(float) / 255
            rgb = rgb * (1 - g) + np.array([pars.get("MatCol2" + c, 1.0) for c in "RGB"]) * 255 * g
    else:
        # Alfa XV2 medida (GOK/HUM/FRI/NMC...): nucleo de la linea 0,66-0,81, halo <= 0,44. Hay que dibujarla en negro: con
        # (a-0,6)/0,4 y fuerza 0,4 oscurecia un 10 % y desaparecia toda la tinta de ropa, cara y manos.
        lines = ink_lines(alpha, .63, .05) if pelo else ink_lines(alpha)     # pelo: lineas mas finas (salian gruesas frente a XV2)
        if subtract_ramp:
            color = np.full(3, 255.0)
        rgb = color[None, None] * (1 - lines[..., None])
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[..., :3] = np.clip(np.rint(rgb), 0, 255)
    rgba[..., 3] = 255 if "EYE" in shader else 0
    return rgba


RAMPA_U = (.76, .99)   # B3 usa toda la rampa; en XV2 lo normal cae en la parte alta (medido en capturas reales: piel crema, no salmon)
RANGO_NAMEK = (.62, .86)   # tramo verde puro de la curva de piel de Namek (la parte alta es amarilla: en juego salia verde neon)
SOMBRA_MIN = .6        # suelo de la sombra respecto al tono iluminado (rampa nativa de piel B3: 0,7)


def ink_lines(alpha, lo=.55, ancho=.06):
    """alpha 0-1 -> tinta 0-1, con borde estrecho (la resta B3 tine de color cada texel a medias). Alfa saturada = sin lineas."""
    if (alpha > .9).mean() > .5:
        return np.zeros_like(alpha)
    return np.clip((alpha - lo) / ancho, 0, 1)


def material_ramp(dyt, material, native, tint=None, rango=RAMPA_U, pelo=False, tono=None):
    """DYT de color -> resta B3 (base blanca - (255 - DYT) = DYT), 64x16. No es el shader de XV2: el reparto [H] se
    calibra en juego contra capturas reales de XV2."""
    from PIL import Image
    pars = {p["name"]: p["value"] for p in material["parameters"]}
    row = int(round(pars.get("MatScale1X", 0))) * 16
    if not 0 <= row < dyt.shape[0]:
        raise ValueError("material: bloque DYT fuera de rango")
    with Image.open(io.BytesIO(native)) as image:
        ramp = np.array(image.convert("RGBA").resize((64, 16), Image.Resampling.NEAREST))
    curve = dyt[row, :, :3].astype(float)
    n = curve.shape[0] - 1
    if tint is not None:       # color elegido (BCS del creador o HSV de la ficha): se conserva el perfil de luz de la curva
        tint = np.asarray(tint, float) * 255
        curve = tint[None] * (curve.mean(1, keepdims=True) / max(curve[int(.97 * n)].mean(), 1.0))
    if tono is not None:       # piel de Namek: en juego salia verde neon (60,200,30) frente al (30,170,62) de XV2
        curve = np.clip(curve * np.asarray(tono, float) + np.array((0., 0., 8.)), 0, 255)
    x = np.linspace(*rango, 64)
    if pelo and curve[int(.97 * n)].max() < 130:    # pelo oscuro (Goku): el rango entero daba un negro plano; brillo con un punto azul
        curve = np.clip(curve * 1.5 + np.array((0., 3., 10.)), 0, 255)
    c = np.maximum(curve[np.rint(x * n).astype(int)], SOMBRA_MIN * curve[int(.85 * n)])
    # Degradado entre bandas: la curva tiene saltos de un texel que en la cara salian como curvas de nivel.
    c = np.stack([np.convolve(np.pad(c[:, k], 3, mode="edge"), np.array((1, 2, 3, 3, 3, 2, 1)) / 15, "valid") for k in range(3)], 1)
    ramp[:14, :, :3] = 255 - np.clip(c, 0, 255).astype(np.uint8)
    ramp[..., 3] = 255
    return ramp


def ramp_dds(rgba):
    """A8R8G8B8 nativo: RGB565 pierde los tonos casi negros de la curva DYT.

    No cambia la curva ni el umbral. La deduplicacion de texturas mantiene el
    presupuesto pese a conservar los ocho bits de cada canal.
    """
    h, w = rgba.shape[:2]
    header = bytearray(128)
    header[:4] = b'DDS '
    struct.pack_into('<7I', header, 4, 124, 0x100F, h, w, w*4, 0, 0)
    struct.pack_into('<8I', header, 76, 32, 0x41, 0, 32,
                     0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    struct.pack_into('<I', header, 108, 0x1000)
    return bytes(header) + rgba[..., [2, 1, 0, 3]].tobytes()


def source_recipe(bcs, character, costume=0, parts=None):
    """Resuelve el conjunto BCS, incluidas piezas compartidas y attachments SCD. parts: piezas sueltas (creador)."""
    if parts is None:
        outfit = next((s for s in bcs["part_sets"] if s["id"] == costume), None)
        if outfit is None:
            raise ValueError("XV2: no existe el traje %d de %s" % (costume, character))
        parts = outfit["parts"]
    recipes = []
    for p in parts:
        if p is None:
            continue
        code = p["chara"] or character
        base = "data/chara/%s/" % code
        if p["model"] >= 0:
            model = "%s_%03d_%s" % (code, p["model"], p["type"])
            # El campo texture del BCS NO da el .emb: sin ruta explicita es el del modelo (HUM_301_Bust.emb), y si
            # el modelo no trae, el 000 de su tipo o el atlas de la cara base (forehead/nose/ear).
            texture = [base + p["paths"]["emb"]] if p["paths"]["emb"] else [base + model, base + "%s_000_%s" % (code, p["type"])]
            if p["type"].startswith("Face_") and p["type"] != "Face_eye":
                texture.append(base + "%s_000_Face_base" % code)
            recipes.append(dict(part=p["type"], model=base + (p["paths"]["emd"] or model),
                                material=base + (p["paths"]["emm"] or model),
                                texture=texture, attachment=None, tint=p.get("tint"),
                                hide_flags=p.get("hide_flags", 0), hide_materials=p.get("hide_materials", 0)))
        for ph in p["physics"]:
            paths = ph["paths"]
            if not paths["emd"]:
                raise ValueError("XV2: pieza fisica BCS sin ruta EMD")
            base = "data/chara/%s/" % (ph["chara"] or code)
            recipes.append(dict(part=paths["emd"], model=base + paths["emd"],
                                material=base + (paths["emm"] or paths["emd"]),
                                texture=[base + (paths["emb"] or paths["emd"])], attachment=paths["bone"],
                                dyt_fallback=base + "%s_%03d_%s" % (code, p["texture"], p["type"])))
    return recipes


def load_source(get, lod, character="GOK", costume=0, parts=None, face_ean=None, esk=None):
    """Conjunto BCS del jugador; lecturas en memoria, ninguna extraccion en Steam."""
    prefix = "data/chara/%s/%s_000" % (character, character)
    esk = esk or Esk(get(prefix + ".esk"))
    pieces, evidence = [], []
    from xeno_gestos import FaceEan
    face_ean = face_ean or FaceEan(get('data/chara/%s/%s.fce.ean' % (character, character)))
    bcs = leer_bcs(get("data/chara/%s/%s.bcs" % (character, character)))
    def primero(rutas, ext):
        for k, ruta in enumerate(rutas):
            try:
                return ruta, get(ruta + ext)
            except (KeyError, FileNotFoundError):
                if k == len(rutas) - 1:
                    raise
    recipes = source_recipe(bcs, character, costume, parts)
    recortar = []
    ocultas = set()
    for recipe in recipes:      # hide_flags: bit i = PART_TYPES[i] oculta esa pieza entera (Hair 16 -> Face_ear)
        ocultas |= {t for i, t in enumerate(PART_TYPES) if recipe.get("hide_flags", 0) >> i & 1}
    for recipe in recipes:
        part, path = recipe["part"], recipe["model"]
        if part in ocultas:
            continue
        # Los LOD bajos eliminan los huesos de dedos: no permiten cerrar un puno.
        piece_lod = 0 if part == "Rist" or part.startswith('Face') else lod
        emd_path = path + ("_LOD0%d" % piece_lod if piece_lod else "") + ".emd"
        data = get(emd_path)
        subs = leer_emd(data)
        texture_path = recipe["texture"]
        texture_path, emb_data = primero(texture_path, ".emb")
        textures = leer_emb(emb_data)
        # La curva de color va con el material: HUM_002_Hair.dyt.emb aunque su .emb sea el del 000.
        dyt_path, dyt_data = primero([path, texture_path] + ([recipe["dyt_fallback"]] if recipe.get("dyt_fallback") else []), ".dyt.emb")
        dyt_path += ".dyt.emb"
        dyts = leer_emb(dyt_data)
        mats = {m["name"]: m for m in leer_emm(get(recipe["material"] + ".emm")) if m}
        attachment = recipe["attachment"]
        extra = None
        if attachment:
            extra = Esk(get(path + ".esk"))
            if attachment not in extra.index:
                raise ValueError("SCD: falta attachment %s" % attachment)
            evidence.append(dict(esk=path + ".esk", attachment=attachment,
                                 frozen_bones=[n for n in extra.names if n.startswith("SCD_")],
                                 reason="bind al attachment + nodos/pesos para controlador XV5S propio [H]"))
        for sm in subs:
            # hide_materials de la propia pieza: bit i = oculta sus materiales hide_from_<PART_TYPES[i]> (medido: mangas
            # hide_from_Rist solapan la banda del guante 5,97-6,76 y se veian grises/serradas; los bits de Pants/Boots/Bust coinciden con sus nombres).
            nombre = sm.name.lower()
            if "hide" in nombre:
                propio = next((r.get("hide_materials", 0) for r in recipes if r["part"] == part), 0)
                tipos = [t.lower() for i, t in enumerate(PART_TYPES) if propio >> i & 1]
                if "hide_from_" in nombre:
                    if any("from_%s" % t in nombre for t in tipos):
                        if "from_rist" in nombre and part == "Bust":
                            recortar.append(sm.name)    # la manga larga no se quita entera: queda hasta donde empieza el guante (si no, un hueco)
                        else:
                            continue
                else:
                    quien = [r["part"] for r in recipes if r.get("hide_materials", 0) >> (PART_TYPES.index(part) if part in PART_TYPES else 31) & 1]
                    if quien:
                        continue
            if sm.name not in mats or not sm.texdefs:
                raise ValueError("EMD: falta material/texdef para %s" % sm.name)
            tx = sm.texdefs[0]
            if tx >= len(textures):
                raise ValueError("EMD: textura fuera de rango")
            # La variante DYT 0 es la base. El segundo texdef EMD NO
            # elige variante de DYT: dougi_syatu tiene 1 y la variante 1 es blanca.
            dy = 0
            if dy >= len(dyts):
                raise ValueError("EMD: DYT fuera de rango")
            pieces.append(dict(part=part, sm=sm, material=mats[sm.name], base=textures[tx][1],
                                dyt=dyts[dy][1], attachment=attachment, path=emd_path,
                                sha256=hashlib.sha256(data).hexdigest(), texture=tx, dyt_index=dy,
                                dyt_path=dyt_path, face_ean=face_ean if part.startswith('Face') else None,
                                physics_esk=extra, tint=recipe.get("tint")))
    # Pantalon bajo el faldon: si el faldon se abre al mover la pierna, lo que asoma es la parte alta del pantalon (blanca/gris
    # con luz). Esa parte, que solo se ve por esos huecos, va con el color de la camisa; el pantalon visible (bajo el faldon) no cambia.
    tinte_camisa = next((q["tint"] for q in pieces if q["part"] == "Bust" and callable(q.get("tint")) and "waist" in q["sm"].name.lower()), None)
    if tinte_camisa is not None:
        for q in list(pieces):
            if q["part"] == "Pants" and len(q["sm"].tris) > 1000:
                alto = q["sm"].pos[q["sm"].tris][:, :, 1].mean(1) > -1.3
                if alto.any() and not alto.all():
                    pieces.remove(q)
                    arriba = dict(q, sm=compact(q["sm"], q["sm"].tris[alto]), tint=tinte_camisa, bajo_faldon=True)
                    if os.environ.get("DBZ3_FALDON_MATERIAL", "1") != "0":
                        # El tinte de la camisa se pide por nombre: con el nombre del pantalon salia el gris 0,6 del pantalon (medido en juego:
                        # el parche gris dentado del faldon es esta pieza; brillo 167 frente a 201 del faldon, con esto 196). Mismo color y
                        # rampa que el faldon: su DYT, su material y su tinte. DBZ3_FALDON_MATERIAL=0 lo apaga.
                        ref = max((x for x in pieces if x["part"] == "Bust" and "waist" in x["sm"].name.lower()), key=lambda x: len(x["sm"].tris))
                        tf = ref.get("tint")
                        arriba.update(dyt=ref["dyt"], dyt_index=ref["dyt_index"], material=ref["material"],
                                      tint=(lambda _n, f=tf, n=ref["sm"].name: f(n)) if callable(tf) else tf)
                    pieces.append(arriba)
                    pieces.append(dict(q, sm=compact(q["sm"], q["sm"].tris[~alto])))
    # Cinturon doble: la camisa trae su obi y el pantalon otro, a la misma altura (y 1,3-1,8), con pesos distintos (estomago /
    # pecho): al girar el torso se cruzaban y el del pantalon (gris, blanco con luz) asomaba en manchas dentadas.
    if any(q["part"] == "Bust" and "obi" in q["sm"].name.lower() for q in pieces):
        pieces[:] = [q for q in pieces if not (q["part"] == "Pants" and "obi" in q["sm"].name.lower())]
    if recortar:
        rist = [abs(q["sm"].pos[:, 0]).min() for q in pieces if q["part"] == "Rist" and not q["sm"].name.upper().startswith("SKIN")]
        limite = min(rist) if rist else None
        for q in list(pieces):
            if q["part"] == "Bust" and q["sm"].name in recortar:
                pieces.remove(q)
                if limite is None:
                    continue
                dentro = np.abs(q["sm"].pos[q["sm"].tris][:, :, 0]).max(1) <= limite + 0.02
                if dentro.any():
                    q["sm"] = compact(q["sm"], q["sm"].tris[dentro])
                    pieces.append(q)
    return esk, pieces, evidence


def pose_hand(esk, sm, side, number):
    """Pose semantica [H] en los ejes ESK reales, antes del rebind a B3.

    Conserva el bind y los cuatro pesos. Las bases de dedos XV2 no son las de
    Heroes: la flexion se calcula hacia la palma en espacio de modelo.
    """
    W = [w.copy() for w in esk.world_matrix]
    wrist = esk.index["b_%s_Hand" % side]
    index = esk.world[esk.index["h_%s_Index1" % side]]
    pinky = esk.world[esk.index["h_%s_Pinky1" % side]]
    middle = esk.world[esk.index["h_%s_Middle1" % side]]
    forward = middle - esk.world[wrist]
    normal = np.cross(index - pinky, forward)
    normal /= max(np.linalg.norm(normal), 1e-12)
    # En pose T la palma de XV2 mira ABAJO (medido: normal (+-0,056, +-0,998, +-0,006)). Elegir el signo por Z, que es ~0,
    # daba +Y en las dos manos: los dedos se cerraban hacia el dorso.
    if normal[1] > 0:
        normal = -normal
    angles = S.HAND_POSES[number]
    rotations = {}
    for finger in S.FINGERS:
        for k in (1, 2, 3):
            name = "h_%s_%s%d" % (side, finger, k)
            i = esk.index.get(name)
            if i is None:
                continue
            child = esk.index.get("h_%s_%s%d" % (side, finger, k+1))
            direction = (esk.world[child] - esk.world[i]) if child is not None else (
                esk.world[i] - esk.world[esk.parent[i]])
            # El pulgar cierra HACIA el centro de la palma, no hacia abajo (con el eje de los otros dedos salia en punta).
            hacia = normal if finger != "Thumb" else (middle + normal * .25 * np.linalg.norm(forward) - esk.world[i])
            axis = np.cross(direction, hacia)
            axis /= max(np.linalg.norm(axis), 1e-12)
            # Pulgar: menos cierre, hacia el centro de la palma.
            deg = (angles[finger][k-1] if finger != "Thumb" else
                   ({0: (12, 12, 4), 1: (35, 45, 45), 2: (0, 0, 0),
                     4: (12, 30, 13), 5: (0, 0, 0), 6: (14, 10, 63),
                     10: (6, 39, 67), 11: (40, 56, 70)}[number][k-1]))
            theta = np.deg2rad(deg)
            local_axis = np.linalg.solve(esk.world_matrix[i][:3, :3], axis)
            local_axis /= max(np.linalg.norm(local_axis), 1e-12)
            x, y, z = local_axis
            K = np.array([[0,-z,y], [z,0,-x], [-y,x,0]])
            R = np.eye(4)
            R[:3,:3] += np.sin(theta)*K + (1-np.cos(theta))*(K@K)
            rotations[i] = R
    for i in order(esk.parent):
        p = esk.parent[i]
        local = (np.linalg.inv(esk.world_matrix[p]) @ esk.world_matrix[i]) if p >= 0 else esk.world_matrix[i]
        W[i] = (W[p] @ local if p >= 0 else local) @ rotations.get(i, np.eye(4))
    M = np.array([W[esk.index[n]] @ np.linalg.inv(esk.world_matrix[esk.index[n]]) for n in sm.bone_names])
    weights = np.clip(sm.weights, 0, None)
    weights /= weights.sum(1, keepdims=True)
    blend = np.einsum("vk,vkij->vij", weights, M[sm.bones])
    out = copy.copy(sm)
    out.pos = np.einsum("vij,vj->vi", blend[:,:3,:3], sm.pos) + blend[:,:3,3]
    out.nrm = np.einsum("vij,vj->vi", blend[:,:3,:3], sm.nrm)
    out.nrm /= np.maximum(np.linalg.norm(out.nrm, axis=1, keepdims=True), 1e-12)
    return out


def prepare_pieces(rig, pieces, triangle_ratio, gestos=True):
    from xeno_gestos import FACE_CLIPS, HAND_NUMBERS, HAND_ALIASES, skin_pose
    result, reductions = [], []
    face_poses = {}
    for p in pieces:
        sm = rig.geometry(p["sm"], p["attachment"])
        is_hand = p["part"] == "Rist" and any(n.startswith("h_") for n in sm.bone_names)
        if not is_hand and triangle_ratio < 1 and len(sm.tris) > 24:
            # El reductor existente conserva UV/costuras y pesos; no elimina
            # triangulos arbitrarios para forzar un presupuesto.
            sm = S.decimate(sm, max(12, int(len(sm.tris) * triangle_ratio)))
        reductions.append(dict(part=p["part"], material=sm.name,
                               vertices_before=len(p["sm"].pos), vertices_after=len(sm.pos),
                               triangles_before=len(p["sm"].tris), triangles_after=len(sm.tris)))
        if is_hand:
            # Una submalla XV2 contiene AMBAS manos. Cada triangulo completo a
            # su lado, sin duplicar ni cortar triangulos en la muñeca.
            center = sm.pos[sm.tris].mean(1)[:, 0]
            for side, ids in (("L", center >= 0), ("R", center < 0)):
                if np.any(ids):
                    # Posar antes de reducir conserva las articulaciones de cada
                    # variante; no se reutilizan indices de una reduccion ajena.
                    bone = rig.tpl.bone("L00_%sHAND" % side)
                    numbers = set(getattr(rig.tpl, 'gesture_lists', {}).get(bone, HAND_NUMBERS))
                    numbers = {HAND_ALIASES.get(n, n) for n in numbers}
                    for number in sorted({0} | (numbers & set(HAND_NUMBERS))) if gestos else (0,):
                        posed = pose_hand(rig.esk, p["sm"], side, number)
                        side_ids = posed.pos[posed.tris].mean(1)[:,0] >= 0
                        if side == "R":
                            side_ids = ~side_ids
                        q = compact(posed, posed.tris[side_ids])
                        q = rig.geometry(q)
                        if triangle_ratio < 1 and len(q.tris) > 24:
                            q = S.decimate(q, max(12, int(len(q.tris)*triangle_ratio)))
                        result.append(dict(p, sm=q, rigid=rig.tpl.bone("L00_%sHAND" % side), variant=number))
        else:
            rigid = None
            arriba = None
            if p["part"].startswith('Face'):
                rigid = next((i for i, n in enumerate(rig.tpl.labels) if "L00_" in n and "FACE" in n), None)
                # Triangulo con algo de peso en la boca -> pielado a HEAD/M_JAW/comisuras; el resto, variantes rigidas de FACE.
                ids = np.array([n in BOCA and rig.tpl.bone(BOCA[n]) is not None for n in sm.bone_names])
                boca = (ids[sm.bones] & (sm.weights > .02)).any(1)
                baja = boca[sm.tris].any(1)
                arriba = ~baja
                if baja.any():
                    result.append(dict(p, sm=compact(sm, sm.tris[baja]), rigid=None))
                sm = compact(sm, sm.tris[arriba]) if arriba.any() else None
            elif p["attachment"]:
                rigid = rig.target[p["attachment"]]
            if sm is not None:
                result.append(dict(p, sm=sm, rigid=rigid))
            if gestos and sm is not None and p.get('face_ean') is not None and rigid is not None:
                numbers = set(getattr(rig.tpl, 'gesture_lists', {}).get(rigid, FACE_CLIPS))
                for number in sorted(numbers & set(FACE_CLIPS)):
                    if number not in face_poses:
                        face_poses[number] = p['face_ean'].pose(rig.esk, FACE_CLIPS[number])
                    matrices, evidence = face_poses[number]
                    q = rig.geometry(skin_pose(p['sm'], matrices))
                    q = compact(q, q.tris[arriba])
                    if triangle_ratio < 1 and len(q.tris) > 24:
                        q = S.decimate(q, max(12, int(len(q.tris)*triangle_ratio)))
                    result.append(dict(p, sm=q, rigid=rigid, variant=number, gesture=evidence))
    return result, reductions


def build_model(tpl, rig, pieces, cap):
    from amo2awo import win_bytes
    slots = {b: s for s, b in enumerate(tpl.palette) if s and b < tpl.nb}
    if not slots or tpl.palette[0] != 0xFFFFFFFF:
        raise ValueError("B3: paleta invalida")
    textures, tex_keys, materials, groups, weights_report = [], {}, [], {}, []
    texture_blobs = {}
    def add_texture(w, h, flags, blob):
        key = (w, h, flags, blob)
        if key not in texture_blobs:
            texture_blobs[key] = len(textures)
            textures.append(key)
        return texture_blobs[key]
    bindings = []
    variants = []
    bind_roundtrip = 0.0
    ramp = tpl.ramp_of(lambda _: True)
    if ramp is None:
        raise ValueError("B3: no hay rampa nativa")
    # Faldon sobre pantalon: el faldon (Bust, waist) llevaba pesos propios (muslo entero) y el pantalon pesos mezclados con la
    # pelvis; al levantar la pierna se separaban y el pantalon (blanco/gris) asomaba en picos. Los vertices del faldon cerca
    # del pantalon toman el reparto de su vecino: se mueven juntos.
    capa_baja = []
    # Superficies casi pegadas (pantalon a menos de 0,15 u del faldon: medido 0,05-0,10 a la altura de la cadera) fluctuan en el
    # z-buffer del juego y dejan manchas dentadas del color del pantalon. El pantalon se mete hacia dentro hasta 0,15 u.
    faldon = [q["sm"].pos for q in pieces if q["part"] == "Bust" and "waist" in q["sm"].name.lower()]
    if faldon:
        from scipy.spatial import cKDTree
        arbol = cKDTree(np.vstack(faldon))
        for q in pieces:
            if q["part"] == "Pants" and len(q["sm"].tris) > 400:
                sm = q["sm"]
                d, _ = arbol.query(sm.pos)
                m = (d < 0.15) & (sm.pos[:, 1] > -1.5) & (sm.pos[:, 1] < 1.4)
                if m.any():
                    n = sm.nrm / np.maximum(np.linalg.norm(sm.nrm, axis=1, keepdims=True), 1e-9)
                    pos = sm.pos.copy()
                    pos[m] -= n[m] * np.minimum(0.15 - d[m], 0.12)[:, None]
                    sm.pos = pos
    for p in pieces:
        if p["part"] == "Pants" and p["rigid"] is None and len(p["sm"].tris) > 400:
            b0, w0, _ = remap_weights(rig, p["sm"], slots, True)
            capa_baja.append((p["sm"].pos, b0, w0))
    for p in pieces:
        sm = p["sm"]
        lit = "EYE" not in p["material"]["shader"]
        tint = p.get("tint")
        if callable(tint):
            tint = tint(sm.name)
        key = (p["part"], sm.name, p["texture"], p["dyt_index"], None if tint is None else tuple(np.round(tint, 3)))
        if key not in tex_keys:
            # Prioridad de resolucion: ojos y atlas de la cara al maximo; pelo y manos, tinta a doble resolucion.
            zoom = 2 if p["part"] in ("Hair", "Rist", "Face_eye") else 1
            pelo = "hair" in p["part"].lower()
            rgba = material_image(p["base"], p["dyt"], p["material"], cap, subtract_ramp=lit, tint=tint, zoom=zoom, pelo=pelo)
            h, w = rgba.shape[:2]
            base_idx = add_texture(w, h, 0x21, S.dds_dxt3(w, h) + S.encode_dxt3(rgba))
            ramp_idx = 0xFFFFFFFF
            if lit:
                if p["material"].get("legends"):
                    from dae_a_b3 import rampa_legends
                    rgba = rampa_legends(tpl.textures[ramp][3])
                else:
                    fila = int(round({q["name"]: q["value"] for q in p["material"]["parameters"]}.get("MatScale1X", 0))) * 16
                    luz = p["dyt"][fila, int(.9 * (p["dyt"].shape[1] - 1)), :3].astype(int)
                    namek = any("/%s/" % r in p.get("dyt_path", "").replace("\\", "/") for r in ("NMC", "PIC")) and luz[1] >= 150 and luz[0] < 130 and luz[2] < 70   # piel verde (PIC no la llama SKIN)
                    rango = RANGO_NAMEK if namek else RAMPA_U     # piel de Namek: sin el realce amarillo
                    rgba = material_ramp(p["dyt"], p["material"], tpl.textures[ramp][3], tint, rango, pelo, (0.7, 0.9, 2.2) if namek else None)
                ramp_idx = add_texture(64, 16, 0x80000001, ramp_dds(rgba))
            tex_keys[key] = (base_idx, ramp_idx)
            bindings.append(dict(part=p['part'], material=sm.name, texture=p['texture'], dyt_index=p['dyt_index'],
                                 base_index=base_idx, ramp_index=ramp_idx, lit=lit,
                                  width=w, height=h, shader=p['material']['shader'],
                                  ramp_format='A8R8G8B8' if lit else None))
        mat = S.material(lit, *tex_keys[key])
        if mat not in materials:
            materials.append(mat)
        mi = materials.index(mat)
        win, rigid, nmax = [], p["rigid"], 0
        if rigid is None:
            bs, ws, dropped = remap_weights(rig, sm, slots, p["part"] in ("Bust", "Pants", "Boots"))
            weights_report.append(dict(part=p["part"], material=sm.name, vertices=len(bs),
                                       discarded_mean=float(dropped.mean()), discarded_max=float(dropped.max()),
                                       two_weights=int(np.count_nonzero(ws < 0.99999)),
                                       target_slots={tpl.labels[b]: dict(slot=slots[b], vertices=int(np.count_nonzero(bs == b)))
                                                     for b in sorted(set(bs.tolist()))}))
            if capa_baja and p["part"] == "Bust" and "waist" in sm.name.lower():
                from scipy.spatial import cKDTree
                pos0 = np.vstack([c[0] for c in capa_baja]); b0 = np.concatenate([c[1] for c in capa_baja]); w0 = np.concatenate([c[2] for c in capa_baja])
                d, ix = cKDTree(pos0).query(sm.pos)
                cerca = (d < 0.4) & (sm.pos[:, 1] < 1.0)
                bs, ws = np.where(cerca, b0[ix], bs), np.where(cerca, w0[ix], ws)
                if os.environ.get("DBZ3_FALDON_GRAD", "0") == "1":
                    bs, ws = faldon_degradado(rig, sm.pos, bs, ws)
            for k, (b, w) in enumerate(zip(bs, ws)):
                M = rig.invW[b]
                pos = M[:3, :3] @ sm.pos[k] + M[:3, 3]
                normal = M[:3, :3] @ sm.nrm[k]
                win.append(win_bytes(pos.tolist(), float(w), slots[b], normal.tolist(), None, sm.uv[k].tolist()))
                packed = np.array(struct.unpack_from(">3f", win[-1], 0))
                roundtrip = rig.W[b][:3, :3] @ packed + rig.W[b][:3, 3]
                bind_roundtrip = max(bind_roundtrip, float(np.max(np.abs(roundtrip - sm.pos[k]))))
                nmax = max(nmax, int(b))
            group_bone = 0
            pts = sm.pos
        else:
            M = rig.invW[rigid]
            win, pts = S.windows_rigid(M, sm.pos, sm.nrm, sm.uv, lit)
            group_bone = rigid
        group = dict(skinned=rigid is None, mat=mi, win=win, tris=sm.tris.tolist(), pts=pts,
                      label=tpl.labels[group_bone], nmax=nmax)
        if p.get('physics_esk') is not None:
            group['physics_piece'] = p
        if p.get("variant", 0):
            variants.append((p["variant"], rigid, group))
        else:
            groups.setdefault(group_bone, []).append(group)
    from xeno_fisica import metadata
    physics_data, physics_stats = metadata(rig, groups)
    raw, ax, stats = S.awg_bytes(tpl.names + physics_data, tpl.axes, tpl.links, tpl.word0c, 0, groups, materials, tpl.palette)
    awgs, var_of, aux_stats = [(raw, ax)], {}, []
    for number, bone in sorted(set((n, b) for n, b, _ in variants)):
        label = tpl.labels[bone].replace("L00_", "L%02d_" % number, 1)
        meshes = [dict(g, label=label) for n, b, g in variants if (n,b) == (number,bone)]
        index = len(awgs)
        kind = 'FACE' if 'FACE' in label else 'HAND'
        raw, ax, st = S.awg_bytes(label.encode().ljust(32,b"\0"), [tpl.aux_axis[kind]],
                                   [(-1,-1,-1)], tpl.word0c, tpl.nb+index-1, {0: meshes}, materials, [0xFFFFFFFF])
        awgs.append((raw, ax))
        var_of.setdefault(bone, {})[number] = index
        aux_stats.append(dict(label=label, **st))
    # Cada lista conserva el alias del donante; solo se incorporan los gestos
    # disponibles. Lista de mano 6 = senalar; los no soportados usan reposo.
    lists = [[(0, i, i)] * tpl.nlist for i in range(tpl.nb)]
    for bone, mapping in var_of.items():
        for n in range(tpl.nlist):
            native = getattr(tpl, 'gesture_lists', {}).get(bone)
            pose = native[n] if native else (1 if n == 1 else (2 if n in (2,4,5) else 0))
            if 'HAND' in tpl.labels[bone]:
                from xeno_gestos import HAND_ALIASES
                # (antes la lista 6 se forzaba a "senalar": en el Goku nativo la lista 6 es la mano abierta L05)
                pose = HAND_ALIASES.get(pose, pose)
            if pose in mapping:
                k = mapping[pose]
                lists[bone][n] = (k, tpl.nb+k-1, 0)
    awo = S.awo_bytes(tpl, awgs, lists, tpl.nlist)
    azt = S.build_azt(textures)
    data = S.amb_pack([(awo, tpl.kid_types[b"#AWO"]), (azt, tpl.kid_types[b"#AZT"])])
    return data, dict(awg0=stats, awo=len(awo), azt=len(azt), bin=len(data),
                       textures=len(textures), weights=weights_report, material_bindings=bindings,
                       packed_bind_roundtrip_max_error=bind_roundtrip, physics_runtime=physics_stats,
                       materials="Curva DYT convertida a resta B3; detalle DFD horneado; no shader XV2",
                       aux=aux_stats,
                        hand_face_variants=dict(hands="L00/01/02/04/05/10/11 del donante con dedos XV2 [H]; 20->11 y 38->1",
                                                face="instantaneas FCE EAN XV2, listas/alias del donante B3; asociaciones semanticas [H]",
                                                samples={str(p['variant']): p['gesture'] for p in pieces if p.get('gesture')},
                                                native_lists=getattr(tpl, 'gesture_lists', {}),
                                                missing="reloj EAN/lipsync continuo, accesorios de mano y coeficientes fisicos originales de XV2"))


def synthetic_template():
    """Rig inventado con los sufijos requeridos; no contiene datos de ningun juego."""
    from types import SimpleNamespace
    names = ["b_C_Base", "b_C_Pelvis", "b_C_Spine1", "b_C_Chest", "b_C_Neck1", "b_C_Head"]
    parent = [-1, 0, 1, 2, 3, 4]
    pos = [[0, 0, 0], [0, 1, 0], [0, 2, 0], [0, 3, 0], [0, 4, 0], [0, 5, 0]]
    for side, sign in (("L", 1), ("R", -1)):
        for seq, root, xyz in ((("Shoulder", "Arm1", "Arm2", "Hand"), 3,
                                [(sign, 3, 0), (sign * 2, 3, 0), (sign * 3, 3, 0), (sign * 4, 3, 0)]),
                               (("Leg1", "Leg2", "Foot", "Toe"), 1,
                                [(sign, 1, 0), (sign, 0, 0), (sign, -1, 0), (sign, -1, 1)])):
            for suffix, p in zip(seq, xyz):
                names.append("b_%s_%s" % (side, suffix))
                parent.append(root)
                pos.append(p)
                root = len(names) - 1
    e = SimpleNamespace(names=names, parent=parent, index={n: i for i, n in enumerate(names)},
                        world=[np.array(p, float) for p in pos], inverse_bind_error=0.0)
    e.world_matrix = []
    for p in pos:
        w = np.eye(4)
        w[:3, 3] = p
        e.world_matrix.append(w)
    labels = ["TST_" + ALIASES[n] for n in names]
    worlds = [w.copy() for w in e.world_matrix]
    for w in worlds:
        w[:3, 3] *= [1.2, 1.3, 1.1]
    t = SimpleNamespace(labels=labels, nb=len(names), parent=parent, world=lambda: worlds,
                        bone=lambda suffix: next((i for i, n in enumerate(names) if ALIASES[n] == suffix), None))
    return e, t


def selftest():
    from types import SimpleNamespace
    e, t = synthetic_template()
    r = Retarget(e, t)
    assert r.report["endpoint_max_error"] < 1e-10
    for a, b in (([0, 0, 0], [1, 0, 0]), ([0, 0, 0], [-1, 0, 0])):
        M = segment_matrix(a, b, [2, 3, 4], [2, 5, 4])
        np.testing.assert_allclose(M @ np.r_[a, 1], [2, 3, 4, 1], atol=1e-10)
        np.testing.assert_allclose(M @ np.r_[b, 1], [2, 5, 4, 1], atol=1e-10)
    rejects(lambda: segment_matrix([0]*3, [0]*3, [0]*3, [1]*3))
    rejects(lambda: order([1, 0]))
    rejects(lambda: order([5]))
    i, j = e.index["b_L_Arm1"], e.index["b_L_Arm2"]
    sm = SimpleNamespace(name="inventado", pos=np.array([[2.2, 3.1, 0], [2.5, 3, 0], [3, 3.1, 0.0]]),
                         nrm=np.tile([0., 0, 1], (3, 1)), uv=np.tile([.25, -.5], (3, 1)),
                         bones=np.tile([0, 1, 0, 0], (3, 1)), weights=np.tile([.25, .75, 0, 0], (3, 1)),
                         bone_names=["b_L_Arm1", "b_L_Arm2"], tris=np.array([[0, 1, 2]]))
    transformed = r.geometry(sm)
    slots = {k: k + 1 for k in range(t.nb)}
    bs, ws, dropped = remap_weights(r, transformed, slots)
    assert bs.tolist() == [j]*3
    np.testing.assert_allclose(ws, .75)
    np.testing.assert_allclose(dropped, 0)
    for k, b in enumerate(bs):
        local = r.invW[b] @ np.r_[transformed.pos[k], 1]
        np.testing.assert_allclose(r.W[b] @ local, np.r_[transformed.pos[k], 1])
    assert compact(sm, [[2, 1, 0]]).tris.tolist() == [[2, 1, 0]]
    # Normales con escalado no uniforme: perpendicularidad independiente.
    M = segment_matrix([0,0,0], [1,0,0], [0,0,0], [0,2,0])
    tangent, normal = M[:3,:3] @ [1,1,0], np.array([1,-1,0]) @ np.linalg.inv(M[:3,:3])
    assert abs(tangent @ normal) < 1e-10
    base, dyt = np.zeros((4,4,4), np.uint8), np.full((128,128,4), 128, np.uint8)
    image = material_image(base, dyt, dict(shader="TOON_UNIF_STAIN1_DFD", parameters=[]), 4)
    assert image[0,0].tolist() == [128,128,128,0]
    # Curva inventada: el material no vuelve a restar una rampa de piel ajena.
    from PIL import Image
    fake_dds = io.BytesIO()
    Image.fromarray(np.zeros((64,64,4), np.uint8)).save(fake_dds, format="DDS")
    fake = dict(shader="TOON_UNIF_STAIN1_DFD", parameters=[])
    white = material_image(base, dyt, fake, 4, subtract_ramp=True)
    ramp = material_ramp(dyt, fake, fake_dds.getvalue())
    assert (white[0,0,:3].astype(int) - ramp[0,0,:3]).tolist() == [128]*3
    # Integracion real del escritor, en memoria: #AMB -> #AWO -> #AWG + #AZT,
    # vuelta por el lector independiente y comprobador de rangos/listas.
    from unittest.mock import patch
    t.names = b"".join(n.encode().ljust(32, b"\0") for n in t.labels)
    t.axes, t.links, t.bone_recs = [], [], []
    t.bones_off, t.word0c, t.hdr_tail, t.nlist = 0x30, 0, (0, 0), 2
    t.palette = [0xFFFFFFFF] + list(range(1, t.nb))
    t.kid_types = {b"#AWO": 1, b"#AZT": 2}
    t.ramp_of = lambda pred: 0
    t.textures = [(64, 64, 0x80000001, fake_dds.getvalue())]
    for i in range(t.nb):
        children = [k for k, p in enumerate(t.parent) if p == i]
        siblings = [k for k, p in enumerate(t.parent) if p == t.parent[i] and k > i]
        child, sibling, parent = children[0] if children else -1, siblings[0] if siblings else -1, t.parent[i]
        axis = bytearray(80)
        local = np.linalg.inv(r.W[parent]) @ r.W[i] if parent >= 0 else r.W[i]
        struct.pack_into(">4f3f", axis, 0, 0, 0, 0, 1, *local[:3, 3])
        t.axes.append(axis)
        t.links.append((child, sibling, parent))
        ptr = lambda k: t.bones_off + k * 32 if k >= 0 else 0
        t.bone_recs.append([i, 0, ptr(child), ptr(sibling), ptr(parent), 0, 0, 0])
    part = dict(part="Bust", sm=transformed, rigid=None, material=fake, base=base, dyt=dyt,
                texture=0, dyt_index=0)
    data, stats = build_model(t, r, [part], 4)
    with patch("builtins.open", side_effect=lambda *a, **kw: io.BytesIO(data)):
        assert S.verify("inventado.bin") == []
        back = S.Template("inventado.bin")
    assert back.labels == t.labels and back.palette == t.palette
    np.testing.assert_allclose(back.world(), r.W, atol=2e-6)
    assert stats["packed_bind_roundtrip_max_error"] < 1e-5
    assert data[:4] == b"#AMB" and stats["awg0"]["windows"] == 3
    # Mano inventada: la palma no cambia, la punta del indice si; los tres
    # huesos no se colapsan a una traslacion del brazo ni se muta el original.
    hand = copy.deepcopy(e)
    for side, sign in (("L",1),("R",-1)):
        wrist = e.index["b_%s_Hand" % side]
        for finger, y in (("Index",.3),("Middle",0),("Ring",-.15),("Pinky",-.3),("Thumb",.5)):
            parent = wrist
            for k in (1,2,3):
                name = "h_%s_%s%d" % (side,finger,k)
                hand.index[name] = len(hand.names)
                hand.names.append(name)
                hand.parent.append(parent)
                w = np.eye(4)
                w[:3,3] = e.world[wrist] + [sign*k*.2,y,0]
                hand.world_matrix.append(w)
                hand.world.append(w[:3,3].copy())
                parent = hand.index[name]
    palm = hand.world[hand.index["b_L_Hand"]]
    tip = hand.world[hand.index["h_L_Index3"]] + [.1,0,0]
    fingers = SimpleNamespace(pos=np.array([palm,tip,tip+[0,.1,0]]), nrm=np.tile([0.,0,1],(3,1)),
                              weights=np.tile([1.,0,0,0],(3,1)), bones=np.array([[0]*4,[1]*4,[1]*4]),
                              bone_names=["b_L_Hand","h_L_Index3"], tris=np.array([[0,1,2]]), uv=np.zeros((3,2)))
    saved = fingers.pos.copy()
    opened, fist = pose_hand(hand,fingers,"L",2), pose_hand(hand,fingers,"L",1)
    np.testing.assert_allclose(opened.pos, saved, atol=1e-10)
    np.testing.assert_allclose(fist.pos[0], palm, atol=1e-10)
    assert np.linalg.norm(fist.pos[1]-palm) < np.linalg.norm(tip-palm)
    assert np.linalg.norm(fist.pos[1]-tip) > .1
    np.testing.assert_allclose(np.linalg.norm(fist.nrm,axis=1),1)
    np.testing.assert_array_equal(fingers.pos,saved)
    pointing = pose_hand(hand, fingers, 'L', 6)
    claw = pose_hand(hand, fingers, 'L', 10)
    np.testing.assert_allclose(pointing.pos, opened.pos, atol=1e-10)  # indice extendido
    assert np.linalg.norm(claw.pos[1]-opened.pos[1]) > .02
    # Roundtrip de variantes/listas: seleccion L01/L02, no tres copias del reposo.
    t.nlist = 6
    axis = bytearray(80)
    struct.pack_into(">4f",axis,0,0,0,0,1)
    t.aux_axis = {"HAND": axis}
    bone = t.bone("L00_LHAND")
    variants = [dict(part, rigid=bone, variant=n, sm=q) for n,q in ((0,transformed),(1,copy.copy(transformed)),(2,copy.copy(transformed)))]
    variants[1]["sm"].pos = transformed.pos + [0,0,.2]
    data2, stats2 = build_model(t,r,variants,4)
    with patch("builtins.open",side_effect=lambda *a,**kw: io.BytesIO(data2)):
        assert S.verify("variantes.bin") == []
        back = S.Template("variantes.bin")
    assert back.namg == 3 and len(stats2["aux"]) == 2
    a = next(o for o,size,typ,_ in S.amb_kids(data2) if data2[o:o+4] == b"#AWO")
    bones_off = struct.unpack_from(">I",data2,a+0x14)[0]
    row = a + struct.unpack_from(">I",data2,a+bones_off+bone*32+4)[0]
    # Entradas de 16 B: AWG, ID global, eje, cero. L00/L03 usan el reposo.
    entries = [struct.unpack_from(">4I",data2,row+16*i) for i in range(6)]
    assert entries[0] == entries[3] and entries[2] == entries[4] == entries[5]
    assert entries[0] != entries[1] != entries[2]
    from xeno_gestos import native_lists
    plan = native_lists(data2, back)
    assert plan[bone][:3] == [0, 1, 2]  # nombres en AWG+0x1C, no el campo anterior
    rejects(lambda: material_image(base, dyt, dict(shader="", parameters=[dict(name="MatScale1X", value=8)]), 4))
    from amo2awo import win_bytes
    window = win_bytes([1,2,3], .75, 7, [0,0,1], None, [.25,.5])
    assert len(window) == 44 and struct.unpack_from(">I", window, 16)[0] == 7
    assert struct.unpack_from(">I", window, 32)[0] == 0xFFFFFFFF
    print("xeno_a_b3: N0 OK (retarget, pesos, normales, DYT, dedos, variantes/listas y roundtrip AMB/AWO/AWG/AZT sin juego)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--cpk", help="CPK de XV2 (solo lectura)")
    ap.add_argument("--fuente", type=Path, help="carpeta de personaje ya extraida; alternativa a CPK")
    ap.add_argument("--personaje", choices=CHARACTERS, default="GOK")
    ap.add_argument("--traje", type=int, default=0)
    ap.add_argument("--plantilla", type=Path, help="#AMB nativo B3 descomprimido")
    ap.add_argument("--b3-afs", type=Path)
    ap.add_argument("--entrada", type=int)
    ap.add_argument("--xbdecompress")
    ap.add_argument("--salida", type=Path, help="#AMB nuevo descomprimido (extension .bin)")
    ap.add_argument("--informe", type=Path, help="JSON nuevo con retarget, materiales y limites")
    ap.add_argument("--trabajo", type=Path, default=Path(tempfile.gettempdir()) / "dbz3_xv3")
    ap.add_argument("--lod", type=int, choices=range(4), default=0)
    ap.add_argument("--textura", type=int, choices=(64,128,256,512,1024), default=1024)
    ap.add_argument("--reduccion", type=float, help="fraccion de triangulos (0,35; PIC 0,25); conserva costuras")
    args = ap.parse_args(argv)
    if args.entrada is None:
        args.entrada = CHARACTERS[args.personaje][2]
    if args.selftest:
        selftest()
        return 0
    if args.reduccion is None:
        args.reduccion = 1.0
    if not args.salida or (not args.plantilla and not (args.b3_afs and args.xbdecompress)):
        ap.error("indica --salida y --plantilla o --b3-afs/--xbdecompress")
    if not 0 < args.reduccion <= 1:
        ap.error("--reduccion debe estar en (0,1]")
    try:
        outputs = [args.salida] + ([args.informe] if args.informe else [])
        from xeno_importar import buscar_xv2, proteger_fuente, MISSING
        source = str(args.fuente) if args.fuente else (args.cpk or buscar_xv2())
        if not source or not (Path(source).is_dir() if args.fuente else Path(source).is_file()):
            raise FileNotFoundError(MISSING)
        proteger_fuente(Path(source)/"origen.cpk" if args.fuente else source, outputs+[args.trabajo])
        for path in outputs:
            if path.exists():
                raise FileExistsError("no se sobreescribe: %s" % path)
            if not path.parent.is_dir():
                raise ValueError("no existe la carpeta de salida: %s" % path.parent)
        if not args.trabajo.is_dir():
            raise ValueError("crea primero --trabajo: %s" % args.trabajo)
        if args.plantilla:
            template = args.plantilla
        else:
            decoder = str(Path(args.xbdecompress).resolve())
            data = extraer_b3(args.b3_afs, args.entrada, decoder, args.trabajo)
            template = args.trabajo / ("plantilla_%d_%s.amb" % (args.entrada, hashlib.sha256(data).hexdigest()[:12]))
            if not template.exists():
                with template.open("xb") as f:
                    f.write(data)
        tpl = S.Template(str(template))
        from xeno_gestos import native_lists
        tpl.gesture_lists = native_lists(template.read_bytes(), tpl)
        if tpl.bone("HEAD") is None:
            raise ValueError("el donante B3 no tiene hueso de cabeza")
        if args.fuente:
            def get(name):
                return (args.fuente / Path(name).name).read_bytes()
            esk, pieces, physics = load_source(get, args.lod, args.personaje, args.traje)
        else:
            with Cpk(source) as cpk:
                # El propio CPK mezcla Face_nose/Face_Nose. La busqueda respeta
                # rutas completas y rechaza colisiones en vez de escoger al azar.
                names = {}
                for name in cpk.toc:
                    names.setdefault(name.casefold(), []).append(name)
                def get(name):
                    hits = names.get(name.casefold(), [])
                    if len(hits) != 1:
                        raise KeyError("XV2: ruta ausente o ambigua: " + name)
                    return cpk.get(hits[0])
                esk, pieces, physics = load_source(get, args.lod, args.personaje, args.traje)
        rig = Retarget(esk, tpl)
        converted, reductions = prepare_pieces(rig, pieces, args.reduccion)
        data, stats = build_model(tpl, rig, converted, args.textura)
        with args.salida.open("xb") as f:
            f.write(data)
        errors = S.verify(str(args.salida))
        result = dict(source=str(source), character=args.personaje, costume=args.traje, lod=args.lod,
                      template=str(template), output=str(args.salida), sha256=hashlib.sha256(data).hexdigest(),
                      retarget=rig.report, physics=physics, reduction=reductions, **stats,
                       source_files=[dict(path=p["path"], sha256=p["sha256"], material=p["sm"].name,
                                          shader=p["material"]["shader"], dyt_path=p["dyt_path"]) for p in pieces],
                      structural_errors=errors, runtime="not_tested_by_converter")
        if args.informe:
            write_json(args.informe, result)
        print("%s -> %s: %d B, %d ventanas, %d draws, %d texturas" %
              (args.personaje, args.salida, len(data), stats["awg0"]["windows"], stats["awg0"]["draws"], stats["textures"]))
        print("retarget: %d matches; error segmentos %.3g; estructura %s; runtime no lo valida este comando" %
              (rig.report["direct_matches"], rig.report["endpoint_max_error"], "OK" if not errors else errors))
        return 1 if errors else 0
    except (OSError, ValueError, KeyError, StopIteration, subprocess.CalledProcessError) as error:
        print("xeno_a_b3: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

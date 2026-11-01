#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DAE con rig Xenoverse (p. ej. Legends) -> #AMB B3 HD. Reutiliza xeno_a_b3 (Retarget, manos, build_model).

Lee un .dae con dae_lector, parte el cuerpo en manos/ropa, deriva la boca (M_JAW) por posicion,
alisa normales, reduce triangulos y entrega las piezas al conversor de Xenoverse. Solo lee:
el .dae y las PNG compiladas del jugador no se modifican ni se copian al repo.
"""
import argparse
import copy
import hashlib
import io
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sdbh_model as S                      # noqa: E402
from dae_lector import leer_dae             # noqa: E402

# Piel verde de Legends (96,197,54) -> verde nativo de Piccolo en B3 (126,171,60); evita el "neon".
VERDE_LEGENDS = np.array((96., 197., 54.))
VERDE_B3 = np.array((126., 171., 60.))


def imagen_legends(base, cap, tono=None):
    """PNG compilada (ya con colores y tinta) -> textura B3 de color; la rampa aporta la sombra (alfa 0, como XV2)."""
    h, w = base.shape[:2]
    if max(h, w) > cap:
        s = cap / max(h, w)
        base = S.resize_rgba(base, max(4, int(w * s) // 4 * 4), max(4, int(h * s) // 4 * 4))
    rgb = base[..., :3].astype(float)
    if tono is not None:
        r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
        verde = (g > 1.25 * r) & (g > 1.6 * b)     # solo pixeles de piel; tinta negra y ojos quedan igual
        k = np.asarray(tono)
        rgb = np.where(verde[..., None], rgb * k, rgb)
    out = np.zeros(base.shape, np.uint8)
    out[..., :3] = np.clip(np.rint(rgb), 0, 255)
    return out


def rampa_legends(native):
    """Rampa del Piccolo nativo (resta 49/30/0 en B3): su fila 0 en las 14 filas de material, como hace XV2."""
    from PIL import Image
    with Image.open(io.BytesIO(native)) as image:
        full = np.array(image.convert("RGBA"))
        ramp = np.array(image.convert("RGBA").resize((64, 16), Image.Resampling.NEAREST))
    ramp[:14, :, :3] = full[0, :, :3][None]
    ramp[..., 3] = 255
    return ramp


def alisar_normales(sm, grados=48.0):
    """Blender exporta sombreado plano (3 vertices por triangulo). Normales suaves por posicion+UV,
    conservando las aristas duras (> grados) de cinturones, bolsas y casco. Fusiona vertices iguales."""
    T = sm.tris
    P = sm.pos
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    area = np.linalg.norm(fn, axis=1, keepdims=True)
    fn = fn / np.maximum(area, 1e-12)
    corner = T.reshape(-1)
    key = np.c_[np.round(P[corner], 4), np.round(sm.uv[corner], 4)]
    _, grp = np.unique(key, axis=0, return_inverse=True)
    grp = grp.reshape(-1)
    face_of = np.repeat(np.arange(len(T)), 3)
    cos = np.cos(np.deg2rad(grados))
    order = np.argsort(grp, kind="stable")
    bounds = np.r_[0, np.cumsum(np.bincount(grp))]
    nrm = np.zeros((len(corner), 3))
    for g in range(len(bounds) - 1):
        ids = order[bounds[g]:bounds[g + 1]]
        f = face_of[ids]
        nf = fn[f]
        w = (nf @ nf.T > cos) * area[f, 0][None, :]
        nrm[ids] = w @ nf
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    key2 = np.c_[grp, np.round(nrm * 200).astype(int)]
    _, first, inv = np.unique(key2, axis=0, return_index=True, return_inverse=True)
    inv = inv.reshape(-1)
    src = corner[first]
    out = copy.copy(sm)
    out.pos, out.uv, out.bones, out.weights = sm.pos[src], sm.uv[src], sm.bones[src], sm.weights[src]
    out.nrm = nrm[first]
    out.tris = inv.reshape(-1, 3)
    return out


def tomar(sm, tris):
    out = copy.copy(sm)
    tris = np.asarray(tris, int).reshape(-1, 3)
    used, back = np.unique(tris, return_inverse=True)
    for k in ("pos", "nrm", "uv", "bones", "weights"):
        setattr(out, k, getattr(sm, k)[used])
    out.tris = back.reshape(-1, 3)
    return out


def separar_manos(sm, esk):
    """(manos, resto): triangulo en la mano si sus tres vertices pesan sobre la muñeca o los dedos."""
    mano = np.array([n.startswith("h_") or n in ("b_L_Hand", "b_R_Hand", "g_L_Hand", "g_R_Hand") for n in sm.bone_names])
    peso = (mano[sm.bones] * sm.weights).sum(1)
    en = (peso > .5)[sm.tris].all(1)
    return (tomar(sm, sm.tris[en]) if en.any() else None), tomar(sm, sm.tris[~en])


def boca_a_mandibula(sm, esk, nombre="f_C_Jaw", y0=None, ancho=1.3, z0=0.15, altura=0.55):
    """Sin hueso de mandibula: los vertices de la mascara/cara bajo la banda de ojos, por delante de las orejas,
    pesan sobre M_JAW (rampa suave hacia arriba, para que no se abra una grieta). Devuelve (sm, n_vertices)."""
    out = copy.copy(sm)
    out.bone_names = list(sm.bone_names) + [nombre]
    j = len(sm.bone_names)
    y0 = esk.world[esk.index["b_C_Head"]][1] + 0.26 if y0 is None else y0      # justo bajo la banda de ojos
    P = sm.pos
    t = np.clip((y0 - P[:, 1]) / altura, 0, 1)
    t = t * t * (3 - 2 * t)
    t *= (np.abs(P[:, 0]) < ancho) & (P[:, 2] > z0)
    t *= np.clip((P[:, 2] - z0) / .35, 0, 1)        # el borde trasero se queda con la cabeza
    out.bones, out.weights = sm.bones.copy(), sm.weights.copy()
    m = t > .02
    out.bones[m] = np.c_[sm.bones[m][:, 0], np.full(m.sum(), j), sm.bones[m][:, 2:]]
    out.weights[m] = np.c_[(1 - t[m]) * sm.weights[m][:, 0], t[m] * sm.weights[m][:, 0], np.zeros((m.sum(), 2))]
    return out, int(m.sum()), t


def piezas(dae, png_cuerpo, png_cara, ratios, tono_piel=True, jaw=True):
    from PIL import Image
    esk, subs = leer_dae(dae)
    # bone sinteticos de boca: hijos de la cabeza (la geometria se ancla a b_C_Head; el peso va a M_JAW)
    for nombre in ("f_C_Jaw",):
        esk.names.append(nombre)
        esk.parent.append(esk.index["b_C_Head"])
        esk.index[nombre] = len(esk.names) - 1
        esk.world_matrix.append(esk.world_matrix[esk.index["b_C_Head"]].copy())
        esk.world.append(esk.world[esk.index["b_C_Head"]].copy())
    tex = {"body-material": np.array(Image.open(png_cuerpo).convert("RGBA")),
           "face-material": np.array(Image.open(png_cara).convert("RGBA"))}
    tono = VERDE_B3 / VERDE_LEGENDS if tono_piel else None
    info, pieces = {}, []
    face = next(s for s in subs if s.name == "face_01")
    for s in subs:
        if s.name not in ("face_01", "faceparts_01_Shape", "PiccoloSH_03_Body"):
            continue
        s = alisar_normales(s)
        if s.name == "face_01":
            # la tira del menton (y < cabeza+0,30) queda dentro del pasamontanas, pero con el casco reducido sus vertices
            # de abajo (medido: 12 de 60 sin casco por delante) asomaban como una mancha verde sobre la mascara gris
            ycut = esk.world[esk.index["b_C_Head"]][1] + .30
            s = tomar(s, s.tris[s.pos[s.tris][..., 1].mean(1) >= ycut])
            info["tira_menton_quitada"] = True
        partes = []
        if s.name == "PiccoloSH_03_Body":
            manos, resto = separar_manos(s, esk)
            partes += [("Rist", manos, ratios["manos"]), ("Bust", resto, ratios["cuerpo"])]
        elif s.name == "faceparts_01_Shape":
            if jaw:
                s, nj, _ = boca_a_mandibula(s, esk)
                info["vertices_a_mandibula"] = nj
            partes.append(("Head", s, ratios["casco"]))
        else:
            partes.append(("Face_base", s, ratios["cara"]))
        for part, sm, ratio in partes:
            antes = len(sm.tris)
            if ratio < 1 and len(sm.tris) > 24:
                sm = S.decimate(sm, max(12, int(len(sm.tris) * ratio)))
            info[part] = (antes, len(sm.tris))
            mat = {"shader": "LEGENDS", "parameters": [], "legends": True}
            if s.material == "face-material":
                mat["tono"] = tono
            pieces.append(dict(part=part, sm=sm, material=mat, base=tex[s.material], dyt=None, attachment=None,
                               path=str(dae), sha256=hashlib.sha256(Path(dae).read_bytes()).hexdigest(),
                               texture=0 if s.material == "body-material" else 1, dyt_index=0, dyt_path="",
                               face_ean=None, physics_esk=None, tint=None))
    return esk, pieces, info


def convertir(dae, png_cuerpo, png_cara, plantilla, salida, ratios, cap=1024, jaw=True):
    import xeno_a_b3 as X
    tpl = S.Template(str(plantilla))
    from xeno_gestos import native_lists
    tpl.gesture_lists = native_lists(Path(plantilla).read_bytes(), tpl)
    esk, pieces, info = piezas(dae, png_cuerpo, png_cara, ratios, jaw=jaw)
    rig = X.Retarget(esk, tpl)
    # Piccolo no lleva LARM2 en la paleta del brazo izquierdo: el antebrazo nativo cuelga de LZARM4 (hijo de LARM2).
    # Sin esto el antebrazo izquierdo seguia al brazo y no doblaba en el codo.
    slots = {b for s_, b in enumerate(tpl.palette) if s_ and b < tpl.nb}
    for lado in "LR":
        arm2, z4 = tpl.bone(lado + "ARM2"), tpl.bone(lado + "ZARM4")
        if arm2 is not None and z4 is not None and arm2 not in slots and z4 in slots:
            for k, v in list(rig.target.items()):
                if v == arm2:
                    rig.target[k] = z4
    conv, red = X.prepare_pieces(rig, pieces, 1.0)
    data, stats = X.build_model(tpl, rig, conv, cap)
    Path(salida).write_bytes(data)
    errors = S.verify(str(salida))
    return dict(info=info, errores=errors, bytes=len(data), awg0=stats["awg0"], texturas=stats["textures"],
                pesos=[(w["part"], round(w["discarded_mean"], 4), round(w["discarded_max"], 3)) for w in stats["weights"]],
                retarget_max=rig.report["endpoint_max_error"])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dae", required=True)
    ap.add_argument("--cuerpo", required=True, help="body_compiled.png")
    ap.add_argument("--cara", required=True, help="face_compiled.png")
    ap.add_argument("--plantilla", required=True, help="#AMB nativo B3 descomprimido (Piccolo, entrada 350)")
    ap.add_argument("--salida", required=True)
    ap.add_argument("--cuerpo-ratio", type=float, default=.6)
    ap.add_argument("--manos-ratio", type=float, default=.7)
    ap.add_argument("--casco-ratio", type=float, default=.6)
    ap.add_argument("--cara-ratio", type=float, default=.85)
    ap.add_argument("--sin-mandibula", action="store_true")
    ap.add_argument("--mod", help="carpeta de mod nueva: escribe modelo.lzx y traje.toml (personaje/nombre por --personaje/--nombre)")
    ap.add_argument("--personaje", type=int, default=11, help="ID de B3 al que se anade el traje (11 = Piccolo)")
    ap.add_argument("--nombre", default="Red Ribbon Disguise")
    a = ap.parse_args(argv)
    r = convertir(a.dae, a.cuerpo, a.cara, a.plantilla, a.salida,
                  dict(cuerpo=a.cuerpo_ratio, manos=a.manos_ratio, casco=a.casco_ratio, cara=a.cara_ratio), jaw=not a.sin_mandibula)
    for k, v in r.items():
        print(k, v)
    if a.mod and not r["errores"]:
        import subprocess
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "mod center hd"))
        import swap_b3
        mod = Path(a.mod)
        mod.mkdir(parents=True, exist_ok=False)
        subprocess.run([swap_b3.XBCOMPRESS, "/N:2048", a.salida, str(mod / "modelo.lzx")], check=True, capture_output=True)
        (mod / "traje.toml").write_text("[traje]" + chr(10) + "personaje = %d" % a.personaje + chr(10) + "nombre = \"%s\"" % a.nombre + chr(10) + "modelos = [\"modelo.lzx\"]" + chr(10), encoding="utf-8")
        print("mod escrito en", mod)
    return 1 if r["errores"] else 0


if __name__ == "__main__":
    sys.exit(main())

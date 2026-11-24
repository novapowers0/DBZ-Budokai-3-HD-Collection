#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_proporcion.py - calibracion de proporciones del heroe XV2 (HER09, carril C2).

Mide el esqueleto XV2 (ya escalado por la constitucion de la ficha) y el de la
plantilla B3, y aplica a la plantilla SOLO lo que se puede medir sin juego:

  * largo de pierna por raza (relacion pierna/torso de XV2 sobre la de B3), como
    `xv2_cac_b3.proporciones_piernas`, pero con cadera exacta: los pies quedan
    donde los de la plantilla (no una caida fija a ojo);
  * altura relativa opcional (`escala`, 1.0 = dejar la de B3). Se mide, no se
    enciende sola: la decision es del banco `xv2_razas.py`.

No abre el juego ni XV2: recibe el Esk y el Template ya cargados. `--selftest`
usa un esqueleto y una plantilla sinteticos.
"""
import argparse
import os
import struct

import numpy as np

# Puntos de medida (nombres del ESK de XV2 y sufijos del B3). Se eligen huesos que
# existen en las seis razas y en el donante (Goku/Piccolo/Vegeta).
PARES = (
    ("pierna", "b_L_Leg1", "b_L_Foot", "LLEG1", "LFOOT1"),
    ("torso", "b_C_Head", "b_L_Leg1", "HEAD", "LLEG1"),
    ("cabeza", "b_C_Head", "b_C_Neck1", "HEAD", "NECK"),
    ("hombro", "b_L_Shoulder", "b_R_Shoulder", "LCHN", "RCHN"),
    ("brazo", "b_L_Arm1", "b_L_Hand", "LARM1", "LHANDROT"),
)
SUELA_B3 = ("LLEG1", "LFOOT2", "RFOOT2")


def _v(w):
    return np.asarray(w, float)[:3, 3] if np.asarray(w).ndim == 2 else np.asarray(w, float)


def ratios_esk(esk):
    """Distancias en el bind de XV2 (unidades del ESK) y sus cocientes sin escala."""
    ix = esk.index
    p = lambda n: _v(esk.world[ix[n]])  # noqa: E731
    out = {"alto": float(p("b_C_Head")[1] - min(p("b_L_Foot")[1], p("b_R_Foot")[1]))}
    for nombre, a, b, _, _ in PARES:
        out[nombre] = float(np.linalg.norm(p(a) - p(b)))
    out["pierna_torso"] = out["pierna"] / max(out["torso"], 1e-9)
    out["cabeza_alto"] = out["cabeza"] / max(out["alto"], 1e-9)
    out["brazo_alto"] = out["brazo"] / max(out["alto"], 1e-9)
    out["hombro_alto"] = out["hombro"] / max(out["alto"], 1e-9)
    return out


def ratios_tpl(tpl):
    W = tpl.world()
    b = tpl.bone
    p = lambda s: _v(W[b(s)])  # noqa: E731
    out = {"alto": float(p("HEAD")[1] - min(p("LFOOT2")[1], p("RFOOT2")[1]))}
    for nombre, _, _, a, c in PARES:
        out[nombre] = float(np.linalg.norm(p(a) - p(c)))
    out["pierna_torso"] = out["pierna"] / max(out["torso"], 1e-9)
    out["cabeza_alto"] = out["cabeza"] / max(out["alto"], 1e-9)
    out["brazo_alto"] = out["brazo"] / max(out["alto"], 1e-9)
    out["hombro_alto"] = out["hombro"] / max(out["alto"], 1e-9)
    return out


def k_pierna(esk, tpl):
    """Factor de pierna que iguala la relacion pierna/torso de XV2 con la de B3."""
    a = ratios_esk(esk)["pierna_torso"]
    b = ratios_tpl(tpl)["pierna_torso"]
    return float(np.clip(a / max(b, 1e-9), 0.45, 1.0))


def _set_local(tpl, i, t):
    tpl.local[i] = tpl.local[i].copy()
    tpl.local[i][:3, 3] = t
    tpl.axes[i][16:28] = struct.pack(">3f", *t)


def _hijos_pierna(tpl, lado):
    raiz = tpl.bone(lado + "LEG1")
    hijos = [i for i in range(tpl.nb) if raiz in set(_ancestros(tpl, i)) and i != raiz]
    return hijos


def _ancestros(tpl, i):
    while i != -1:
        yield i
        i = tpl.parent[i]


def pies(tpl):
    W = tpl.world()
    b = tpl.bone
    return float(min(_v(W[b("LFOOT2")])[1], _v(W[b("RFOOT2")])[1]))


def _escalar_todo(tpl, s):
    if abs(s - 1.0) < 1e-9:
        return
    for i in range(tpl.nb):
        t = tpl.local[i][:3, 3] * s
        _set_local(tpl, i, t)


def k_brazo(esk, tpl):
    """Factor que iguala brazo/alto de XV2 (ARM1 -> mano); solo alarga (1.0..1.4)."""
    a = ratios_esk(esk)["brazo_alto"]
    b = ratios_tpl(tpl)["brazo_alto"]
    return float(np.clip(a / max(b, 1e-9), 1.0, 1.4))


def aplicar(tpl, esk, raza="HUM", k=None, escala=1.0, cadera=True, brazo=True):
    """Aplica pierna (k) y altura (escala) a la plantilla, con los pies en su sitio.

    Devuelve el informe con los factores y los ratios antes/despues. No toca el
    modelo: solo el esqueleto de la plantilla (lo mismo que hacia
    `proporciones_piernas`, pero con la cadera medida).
    """
    antes = ratios_tpl(tpl)
    k = k_pierna(esk, tpl) if k is None else float(np.clip(k, 0.45, 1.0))
    d_cadera = 0.0
    if k < 0.999:
        f0 = pies(tpl)
        for lado in ("R", "L"):
            for i in _hijos_pierna(tpl, lado):
                _set_local(tpl, i, tpl.local[i][:3, 3] * k)
        if cadera:
            d_cadera = pies(tpl) - f0            # los pies suben al acortar: bajar la cadera lo mismo
            w = tpl.bone("WAIST")
            t = tpl.local[w][:3, 3].copy()
            t[1] -= d_cadera
            _set_local(tpl, w, t)
    if abs(escala - 1.0) > 1e-9:
        f0 = pies(tpl)
        _escalar_todo(tpl, escala)
        if cadera:
            d2 = pies(tpl) - f0
            w = tpl.bone("WAIST")
            t = tpl.local[w][:3, 3].copy()
            t[1] -= d2
            _set_local(tpl, w, t)
            d_cadera += d2
    kb = 1.0
    if brazo and os.environ.get("DBZ3_XV2_BRAZO") != "0":
        # POST/CAC: el brazo de B3 queda corto segun la raza (HUM -13 %, NMC -10 %, MAM -22 %);
        # se alarga ARM2 y HANDROT de cada lado (los dedos no se escalan).
        kb = k_brazo(esk, tpl)
        if kb > 1.001:
            for lado in ("L", "R"):
                for suf in ("ARM2", "HANDROT"):
                    i = tpl.bone(lado + suf)
                    _set_local(tpl, i, tpl.local[i][:3, 3] * kb)
    despues = ratios_tpl(tpl)
    errores = {}
    for clave in ("pierna_torso", "cabeza_alto", "brazo_alto", "hombro_alto"):
        ref = ratios_esk(esk)[clave]
        errores[clave] = abs(despues[clave] - ref) / max(abs(ref), 1e-9)
    return dict(raza=raza, k=k, k_brazo=kb, escala=escala, d_cadera=d_cadera, antes=antes, despues=despues,
                referencia=ratios_esk(esk), error_relativo=errores,
                error_medio=float(np.mean(list(errores.values()))))


EXTREMIDAD = ("LEG2", "FOOT1", "FOOT2", "ARM2", "HAND")     # sufijos B3 de los tramos que conservan el eje del hueso
ALTO_XV2_REF = 14.085     # cabeza-pie de GOK_000.esk / HUM_000.esk de XV2 (medido 12/11): el mismo Goku en los dos juegos da la escala
REPOSO_B3 = 9.44          # cadera sobre el suelo en el reposo de la plantilla (medido en juego, SUB_XV2)


def escala_xv2(tpl):
    """Unidades B3 por unidad XV2: Goku B3 (cabeza-tobillo) / Goku XV2. Una sola escala para todas las razas."""
    W = tpl.world()
    return float(_v(W[tpl.bone("HEAD")])[1] - _v(W[tpl.bone("LFOOT1")])[1]) / ALTO_XV2_REF


def esqueleto(tpl, esk, direct, s, suela_xv2=None, guardia=0.0):
    """Pone la plantilla B3 en las proporciones del esqueleto XV2 (XV2RAZAS, 12/11).

    Cada hueso B3 casado (`direct`: indice ESK -> indice B3) queda a s * (distancia XV2) de su ancestro casado. Asi la malla XV2 entra
    con una semejanza (escala s) en vez de estirarse a lo largo de los huesos de Goku. Antes solo se tocaba la pierna.
    suela_xv2 = y minima de la malla XV2 (unidades XV2): los pies quedan en el suelo de la plantilla. `guardia`: caida
    absoluta de la cadera en la guardia; con otra altura de cadera se compensa su parte proporcional.
    Devuelve dict(k_pierna, cadera, factores)."""
    W0 = tpl.world()
    X = [_v(w) for w in esk.world]
    inv = {t: i for i, t in direct.items()}
    suelo = float(_v(W0[tpl.bone("WAIST")])[1]) - REPOSO_B3
    pierna0 = float(_v(W0[tpl.bone("LLEG1")])[1] - _v(W0[tpl.bone("LFOOT1")])[1])
    factores = {}
    for t in sorted(inv):                      # los padres van antes que los hijos en la plantilla
        camino, p = [t], tpl.parent[t]
        while p != -1 and p not in inv:
            camino.append(p)
            p = tpl.parent[p]
        if p == -1:
            continue
        d0 = float(np.linalg.norm(_v(W0[t]) - _v(W0[p])))
        if d0 < 1e-6:
            continue
        f = s * float(np.linalg.norm(X[inv[t]] - X[inv[p]])) / d0
        factores[tpl.labels[t]] = f
        con_t = [b for b in camino if float(np.linalg.norm(tpl.local[b][:3, 3])) > 1e-6]
        if len(con_t) == 1 and not tpl.labels[t].endswith(EXTREMIDAD):
            # Tronco y arranque de las extremidades (cadera, clavicula, hombro, cuello, cabeza): la articulacion va DONDE la
            # tiene XV2 (direccion incluida). Con la direccion de Goku la cadera quedaba 0,5 u por encima de la pelvis y el
            # hombro mas alto: la relacion pierna/torso fallaba un 8 % y la malla se torcia en la ingle y la axila.
            b = con_t[0]
            R = np.asarray(W0[tpl.parent[b]], float)[:3, :3]
            _set_local(tpl, b, R.T @ (s * (X[inv[t]] - X[inv[p]])))
        else:
            # Tramos de extremidad: se conserva el eje del hueso B3 (las animaciones giran sobre el) y solo cambia el largo.
            for b in camino:
                _set_local(tpl, b, tpl.local[b][:3, 3] * f)
    W = tpl.world()
    w = tpl.bone("WAIST")
    pie = tpl.bone("LFOOT1")
    y_suela = float(_v(W[pie])[1]) + s * ((suela_xv2 if suela_xv2 is not None else X[inv[pie]][1] - 0.75) - X[inv[pie]][1])
    t = tpl.local[w][:3, 3].copy()
    t[1] += suelo - y_suela
    cadera = float(_v(W[w])[1]) + (suelo - y_suela) - suelo        # altura nueva de la cadera sobre el suelo
    t[1] += (1.0 - cadera / REPOSO_B3) * guardia
    _set_local(tpl, w, t)
    W = tpl.world()
    pierna = float(_v(W[tpl.bone("LLEG1")])[1] - _v(W[pie])[1])
    return dict(k_pierna=pierna / max(pierna0, 1e-9), cadera=cadera, factores=factores)


TABLA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "xv2_razas.json")


def tabla(ruta=None):
    """Tabla medida por `xv2_razas.py` (None si aun no hay banco)."""
    import json
    try:
        with open(ruta or TABLA, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def habilitada(raza, ruta=None):
    d = tabla(ruta)
    return bool(d and d.get("razas", {}).get(raza, {}).get("encendida"))


# HEROE-06: motivo legible de una raza apagada (el creador lo muestra y el informe
# lo lista). Claves = puertas de xv2_razas.py; orden estable para que el texto no
# cambie entre ejecuciones.
MOTIVOS = (
    ("ssim_silueta", "silueta por debajo del minimo"),
    ("ssim_color", "color por debajo del minimo"),
    ("error_pierna", "relacion pierna/torso fuera de tolerancia"),
    ("error_forma", "cabeza/brazo/hombro fuera de tolerancia"),
    ("fisica", "sin bloque de fisica valido"),
    ("mejora", "la calibracion no mejora la linea base"),
)


def motivos_fallidos(puertas):
    """Lista de motivos (texto) de las puertas en rojo, en orden fijo."""
    p = puertas or {}
    return [txt for clave, txt in MOTIVOS if p.get(clave) is False]


def motivo(raza, ruta=None):
    """None si la raza esta encendida o no hay banco; si no, por que esta oculta."""
    d = tabla(ruta)
    fila = (d or {}).get("razas", {}).get(raza)
    if not fila:
        return "sin banco de medida"
    if fila.get("encendida"):
        return None
    fallos = motivos_fallidos(fila.get("puertas"))
    return "; ".join(fallos) if fallos else "no paso el banco"


def aplicar_tabla(tpl, esk, raza, ruta=None):
    """Aplica la fila medida de esa raza; 1.0 si no hay banco o la raza no paso."""
    import os as _os
    if _os.environ.get("DBZ3_XV2_PROPORCIONES") is not None:
        return 1.0            # prueba/legado: manda la variable de entorno
    d = tabla(ruta)
    fila = (d or {}).get("razas", {}).get(raza)
    if not fila or not fila.get("encendida"):
        return 1.0
    # usuario 14/11: XV2 queda pequeno junto a B3 HD; +10 % global (DBZ3_XV2_ESCALA para afinar)
    glob = float(os.environ.get("DBZ3_XV2_ESCALA") or "1.10")   # variable vacia = valor por defecto
    r = aplicar(tpl, esk, raza, k=fila.get("k"), escala=fila.get("escala", 1.0) * glob)
    return r["k"] if r["k"] < 0.999 else 1.0


def guardar(ruta, informes):
    import json
    with open(ruta, "w", encoding="utf-8") as fh:
        json.dump(informes, fh, indent=1, sort_keys=True, ensure_ascii=False)


def cargar(ruta):
    import json
    with open(ruta, encoding="utf-8") as fh:
        return json.load(fh)


def _esk_sintetico(k=0.6, altura=1.0):
    """Esk minimo con la interfaz usada (index, world). Sin XV2."""
    from types import SimpleNamespace

    def m(y, x=0.0):
        return np.array([[1, 0, 0, x], [0, 1, 0, y], [0, 0, 1, 0], [0, 0, 0, 1]], float)
    nombres = ["b_C_Head", "b_C_Neck1", "b_L_Shoulder", "b_R_Shoulder", "b_L_Arm1", "b_L_Hand",
               "b_L_Leg1", "b_L_Foot", "b_R_Leg1", "b_R_Foot"]
    y = {"b_C_Head": 1.0, "b_C_Neck1": 0.85, "b_L_Shoulder": 0.8, "b_R_Shoulder": 0.8,
         "b_L_Arm1": 0.8, "b_L_Hand": 0.8, "b_L_Leg1": 0.5 * k, "b_L_Foot": 0.0,
         "b_R_Leg1": 0.5 * k, "b_R_Foot": 0.0}
    x = {"b_L_Shoulder": -0.2, "b_R_Shoulder": 0.2, "b_L_Arm1": -0.4, "b_L_Hand": -0.6}
    return SimpleNamespace(index={n: i for i, n in enumerate(nombres)},
                           world=[m(y[n] * altura, x.get(n, 0.0)) for n in nombres])


def _tpl_sintetico():
    """Template minimo con local/axes/bone/parent/nb/world() como sdbh_model."""
    from types import SimpleNamespace
    lab = ["X_BODY", "BODY_WAIST", "BODY_LLEG1", "BODY_LLEG2", "BODY_LFOOT1", "BODY_LFOOT2",
           "BODY_RLEG1", "BODY_RLEG2", "BODY_RFOOT1", "BODY_RFOOT2", "BODY_STMC", "BODY_CHEST",
           "BODY_NECK", "BODY_HEAD", "BODY_LCHN", "BODY_RCHN", "BODY_LARM1", "BODY_LARM2",
           "BODY_LHANDROT", "BODY_RARM1", "BODY_RARM2", "BODY_RHANDROT"]
    par = [-1, 0, 1, 2, 3, 4, 1, 6, 7, 8, 1, 10, 11, 12, 11, 11, 14, 16, 17, 15, 19, 20]
    pos = {"BODY": (0, 0, 0), "WAIST": (0, 0, 0), "LLEG1": (0.3, -1, 0), "LLEG2": (0.3, -2, 0), "LFOOT1": (0.3, -3, 0),
           "LFOOT2": (0.3, -3.2, 0.2), "RLEG1": (-0.3, -1, 0), "RLEG2": (-0.3, -2, 0),
           "RFOOT1": (-0.3, -3, 0), "RFOOT2": (-0.3, -3.2, 0.2), "STMC": (0, 0.6, 0),
           "CHEST": (0, 1.2, 0), "NECK": (0, 1.7, 0), "HEAD": (0, 2.1, 0), "LCHN": (-0.2, 1.5, 0),
           "RCHN": (0.2, 1.5, 0), "LARM1": (-0.5, 1.5, 0), "LARM2": (-0.9, 1.5, 0),
           "LHANDROT": (-1.3, 1.5, 0), "RARM1": (0.5, 1.5, 0), "RARM2": (0.9, 1.5, 0),
           "RHANDROT": (1.3, 1.5, 0)}
    suf = lambda n: n.split("_", 1)[1]  # noqa: E731
    local, axes = [], []
    for i, l in enumerate(lab):
        t = np.eye(4)
        if i > 0:
            s = suf(l)
            p = suf(lab[par[i]])
            a = np.array(pos[s], float)
            b = np.array(pos[p], float)
            t[:3, 3] = a - b
        local.append(t)
        ax = bytearray(80)
        ax[16:28] = struct.pack(">3f", *t[:3, 3])
        axes.append(ax)
    tpl = SimpleNamespace(nb=len(lab), labels=lab, parent=par, local=local, axes=axes)
    tpl.bone = lambda s: next((i for i, l in enumerate(lab) if l.endswith(s)), None)
    def world():
        W = [None] * tpl.nb
        for i in range(tpl.nb):
            W[i] = W[tpl.parent[i]] @ tpl.local[i] if tpl.parent[i] >= 0 else tpl.local[i].copy()
        return W
    tpl.world = world
    return tpl


def selftest():
    esk = _esk_sintetico(k=0.6)
    tpl = _tpl_sintetico()
    f0 = pies(tpl)
    r = aplicar(tpl, esk, "SYN")
    despues = ratios_tpl(tpl)["pierna_torso"]
    assert 0.45 <= r["k"] <= 1.0 and r["k"] < 1.0, r["k"]
    assert abs(despues / ratios_esk(esk)["pierna_torso"] - 1) < 1e-6, (despues, ratios_esk(esk)["pierna_torso"])
    assert abs(pies(tpl) - f0) < 1e-6, (pies(tpl), f0)
    assert r["despues"]["alto"] < r["antes"]["alto"], "acortar piernas baja la cabeza"
    tpl2 = _tpl_sintetico(); f0 = pies(tpl2)
    r2 = aplicar(tpl2, esk, "SYN", k=1.0, escala=0.8)
    assert abs(pies(tpl2) - f0) < 1e-6, (pies(tpl2), f0)
    assert abs(r2["despues"]["alto"] / r2["antes"]["alto"] - 0.8) < 0.02, r2["despues"]["alto"] / r2["antes"]["alto"]
    tpl3 = _tpl_sintetico(); r3 = aplicar(tpl3, esk, "SYN", k=1.0, escala=1.0)
    assert abs(r3["d_cadera"]) < 1e-12
    # XV2RAZAS: esqueleto completo. Cada tramo casado mide s * XV2 y los pies quedan en el suelo de la plantilla.
    tpl4 = _tpl_sintetico()
    pares = {"b_C_Head": "HEAD", "b_C_Neck1": "NECK", "b_L_Leg1": "LLEG1", "b_L_Foot": "LFOOT1", "b_L_Arm1": "LARM1"}
    direct = {esk.index[a]: tpl4.bone(b) for a, b in pares.items()}
    suelo = _v(tpl4.world()[tpl4.bone("WAIST")])[1] - REPOSO_B3
    r4 = esqueleto(tpl4, esk, direct, 2.0, suela_xv2=-0.1)
    W4 = tpl4.world()
    d = np.linalg.norm(_v(W4[tpl4.bone("LFOOT1")]) - _v(W4[tpl4.bone("LLEG1")]))
    assert abs(d - 2.0 * 0.3) < 1e-6, d
    assert abs(_v(W4[tpl4.bone("LFOOT1")])[1] + 2.0 * (-0.1 - 0.0) - suelo) < 1e-6
    assert abs(np.linalg.norm(_v(W4[tpl4.bone("HEAD")]) - _v(W4[tpl4.bone("NECK")])) - 2.0 * 0.15) < 1e-6
    assert r4["k_pierna"] < 1.0
    # HEROE-06: el motivo de una raza apagada es estable y solo lista puertas en rojo.
    assert motivos_fallidos({"ssim_silueta": False, "ssim_color": True}) == ["silueta por debajo del minimo"]
    assert motivos_fallidos({"ssim_color": False, "fisica": False}) == [
        "color por debajo del minimo", "sin bloque de fisica valido"]
    assert motivos_fallidos({}) == []
    print("xv2_proporcion: OK (pierna y altura medibles y cadera exacta; sin XV2)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        selftest()
        return
    ap.print_help()


if __name__ == "__main__":
    main()

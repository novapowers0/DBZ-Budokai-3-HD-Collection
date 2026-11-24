#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Prueba con datos reales: TKB2, cuatro poses antes/despues y siete clips intactos.

  python prueba_andar_iw.py --previo <respaldo/tactics_manifest.toml> --salida <dir>
  Anadir --solo vegeta,krilin,cell para inspeccion; --juego <smoke_f> instala los .tk.
  No arranca el juego ni cambia ajustes. Guarda hashes y medidas en resultado.json.
"""
import argparse
import hashlib
import json
import math
import struct
import tomllib
from pathlib import Path

import tactics_andar_iw as iw
import vista_tactics as vt
import model_render as mr
import numpy as np
from PIL import Image, ImageDraw


def imagen(modelo, pose, centro, escala, tam=224):
    modelo.local = [np.vstack([p, [0, 0, 0, 1]]) for p in pose]
    rgba = mr.render(modelo, size=(tam, tam), view=mr.rot_y(math.radians(-35)),
                     center=centro, scale=escala, ss=1, outline=1.2)
    im = Image.new("RGBA", (tam, tam), (38, 42, 52, 255))
    im.alpha_composite(Image.fromarray(rgba))
    return im.convert("RGB")


def comparar(afs, unidad, previo, fuente, destino, correr=False):
    datos0, error = vt.hornear_unidad(afs, previo, unidad["b3"])
    if error:
        raise ValueError(error)
    # El horno nuevo solo debe sustituir andar: se compara cada bloque completo,
    # no un hash global que podria ocultar un cambio en ataque/dano/derrota.
    base, error = vt.hornear_unidad(afs, unidad, unidad["b3"])
    if error:
        raise ValueError(error)
    nuevo, error = iw.hornear_unidad(afs, unidad, unidad["b3"], fuente, correr=correr, exigir_iw=True)
    if error:
        raise ValueError(error)
    nb, bloques0 = iw.separar_tk(datos0[0])
    _, bloques_base = iw.separar_tk(base[0])
    _, bloques1 = iw.separar_tk(nuevo[0])
    sin_cambio = [vt.CLIPS[k] for k in range(8) if k != 1 and bloques0[k] == bloques1[k] == bloques_base[k]]
    if len(sin_cambio) != 7 or bloques0[1] == bloques1[1]:
        raise ValueError("%s: cambiaron clips ajenos o no cambio andar" % unidad["id"])
    poses0, poses1 = iw.matrices(bloques0[1], nb), iw.matrices(bloques1[1], nb)
    mb = afs.entry(unidad["modelo"])
    modelo, skel = mr.Model(mb), iw.altura.skeleton(mb)
    cintura = next(i for i, s in enumerate(skel) if iw.altura.suffix(s[0]) == "WAIST")
    piernas = [i for i, s in enumerate(skel) if iw.altura.suffix(s[0]) in ("LLEG1", "RLEG1", "LLEG2", "RLEG2")]
    amplitud = float(np.max(np.abs(poses1[:, piernas, :, :3] - poses1[0, piernas, :, :3])))
    if amplitud < 0.1:
        raise ValueError("%s: piernas sin movimiento" % unidad["id"])
    cierre = float(abs(poses1[-1, cintura, 1, 3] - poses1[0, cintura, 1, 3]))
    if cierre > 1e-5 or not np.isfinite(poses1).all():
        raise ValueError("%s: deriva de cadera o valores no finitos" % unidad["id"])
    reposo = np.array([s[2] for s in skel], dtype="<f4")
    otros = [i for i in range(nb) if i != cintura]
    if not np.allclose(poses1[:, otros][:, :, :, 3], reposo[None, otros, :], atol=1e-6):
        raise ValueError("%s: se alteraron las longitudes de los huesos" % unidad["id"])
    # Encuadre comun de las ocho poses: no recentrar cada fotograma (ocultaria
    # saltos o cambios de altura). La escala tampoco cambia entre antes/despues.
    muestras = []
    for poses in (poses0, poses1):
        muestras.append([poses[min(len(poses) - 1, len(poses) * k // 4)] for k in range(4)])
    limites = []
    apoyo = []
    huesos_pie = [i for i, s in enumerate(skel) if iw.altura.suffix(s[0]) in ("LFOOT1", "LFOOT2", "RFOOT1", "RFOOT2")]
    vertices_pie = np.isin(modelo.vbone, huesos_pie)
    if not vertices_pie.any():
        raise ValueError("%s: no hay geometria de pies para comprobar apoyo" % unidad["id"])
    for r, row in enumerate(muestras):
        for pose in row:
            modelo.local = [np.vstack([p, [0, 0, 0, 1]]) for p in pose]
            P, _ = modelo.posed(None)
            limites.append((P.min(0), P.max(0)))
            if r == 1:
                apoyo.append(float(P[vertices_pie, 1].min()))
    bajo = np.min([p[0] for p in limites], axis=0)
    alto = np.max([p[1] for p in limites], axis=0)
    altura_modelo = float(alto[1] - bajo[1])
    error_apoyo = max(abs(y) for y in apoyo) / altura_modelo
    # La carrera tiene fase de vuelo: ambos pies se despegan. Exigir contacto en
    # los cuatro cuartos rechazaria el clip nativo correcto de Piccolo.
    contacto = min(abs(y) for y in apoyo) / altura_modelo
    fallo_apoyo = (error_apoyo > 0.18 or contacto > 0.03) if correr else error_apoyo > 0.06
    if fallo_apoyo:
        raise ValueError("%s: pies separados del suelo: %s (%.1f%% de altura)" % (
            unidad["id"], apoyo, 100 * error_apoyo))
    centro = (bajo + alto) / 2
    escala = 0.84 * 224 / max(alto - bajo)
    hoja = Image.new("RGB", (4 * 224, 2 * (224 + 28) + 34), (23, 26, 33))
    d = ImageDraw.Draw(hoja)
    c = unidad["clips"]["andar"]
    prefijo = "iw_correr" if correr else "iw"
    d.text((8, 8), "%s | fid %d | IW %d / %02X / anim %d | %s" % (
        unidad["id"], unidad["modelo"], c["iw_banco"], c[prefijo + "_codigo"],
        c[prefijo + "_anim"], "correr" if correr else "andar"), fill="white")
    for r, row in enumerate(muestras):
        for k, pose in enumerate(row):
            y = 34 + r * 252
            d.text((k * 224 + 6, y + 5), "%s  %d%%" % ("B3 anterior" if r == 0 else "IW adaptado", k * 25), fill="white")
            hoja.paste(imagen(modelo, pose, centro, escala), (k * 224, y + 28))
    nombre = unidad["id"] + ".png"
    hoja.save(destino / nombre)
    return nuevo[0], dict(id=unidad["id"], fid=unidad["modelo"], huesos=nb,
        banco=c["iw_banco"], codigo=c[prefijo + "_codigo"], anim=c[prefijo + "_anim"],
        modo="correr" if correr else "andar", cuadros=len(poses1),
        anterior=previo["clips"]["andar"], siete_clips_intactos=sin_cambio,
        amplitud_piernas=amplitud, cierre_cadera=cierre, suelo_pies=apoyo,
        error_apoyo_relativo=error_apoyo, hoja=nombre,
        tk_sha256=hashlib.sha256(nuevo[0]).hexdigest(), bytes=len(nuevo[0]))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--previo", required=True)
    p.add_argument("--salida", required=True)
    p.add_argument("--solo", default="")
    p.add_argument("--juego")
    p.add_argument("--iw")
    p.add_argument("--us")
    p.add_argument("--correr", action="store_true")
    a = p.parse_args()
    destino = Path(a.salida)
    destino.mkdir(parents=True, exist_ok=True)
    man = tomllib.loads((iw.HERE / "tactics_manifest.toml").read_text(encoding="utf-8"))
    previo = {u["id"]: u for u in tomllib.loads(Path(a.previo).read_text(encoding="utf-8"))["unidad"]}
    unidades = [u for u in man["unidad"] if not a.solo or u["id"] in a.solo.split(",")]
    if not unidades:
        p.error("ninguna unidad seleccionada")
    fuente = iw.fuente_iw(a.juego, a.iw)
    afs = vt.vh.rb.Afs(str(Path(vt.vh.rb.us_dir(a.us)) / "data_cmn.afs"), vt.vh.rb.work_dir("andar_iw_"))
    resultados = []
    for u in unidades:
        datos, resultado = comparar(afs, u, previo[u["id"]], fuente, destino, a.correr)
        if a.juego:
            vt.escribir_tk(str(Path(a.juego) / "ingame_ui" / "nave_vista"), u["modelo"], datos)
        resultados.append(resultado)
        (destino / "resultado_parcial.json").write_text(json.dumps(dict(fuente=fuente, unidades=resultados),
            indent=2, ensure_ascii=False), encoding="utf-8")
        print("PASS %s: %s %d cuadros; 7 clips intactos; hoja %s" % (
            u["id"], resultado["modo"], resultado["cuadros"], resultado["hoja"]), flush=True)
    (destino / "resultado.json").write_text(json.dumps(dict(fuente=fuente, unidades=resultados), indent=2, ensure_ascii=False), encoding="utf-8")
    print("PASS: %d unidades, %d poses comparadas" % (len(resultados), 8 * len(resultados)), flush=True)


if __name__ == "__main__":
    main()

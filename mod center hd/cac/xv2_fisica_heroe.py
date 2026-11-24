#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_fisica_heroe.py - HER09: valida la fisica XV5S del heroe creado con piezas XV2.

El creador de XV2 no trae piezas con huesos SCD (faldon/banda/delantal): el bloque
sale vacio (`off_no_SCD`) y el controlador no se enciende, igual que en un luchador
nativo sin telas. Este banco lo mide por raza y, si algun dia hubiera bloque, lo
valida campo a campo (cabecera, orden topologico, pesos e indices de ventana).

No abre el juego. `--selftest` construye el bloque sintetico de `xeno_fisica` y lo
valida sin XV2.
"""
import argparse
import json
import os
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(HERE), str(ROOT / "mod center hd" / "xv2"), str(ROOT / "mod center hd"), str(ROOT / "awo_tools")]

RAZAS = ("HUM", "HUF", "NMC", "FRI", "MAM", "MAF")
TRABAJO = Path(os.environ.get("DBZ3_C2_TRABAJO") or r"E:\DBZ3HD_trabajo\c2-xv2")


def validar(data):
    """Valida un bloque XV5S. Devuelve (stats, errores)."""
    errores = []
    if len(data) < 32:
        return None, ["bloque corto"]
    magic, ver, nn, nv, size, cursor, nb, _ = struct.unpack_from(">4s7I", data, 0)
    if magic != b"XV5S":
        errores.append("magic %r" % magic)
    if ver != 1:
        errores.append("version %d" % ver)
    if size > len(data) or len(data) - size >= 32:
        errores.append("size %d fuera de %d (padding >= 32)" % (size, len(data)))
    if nn <= 0 or nv <= 0:
        errores.append("sin nodos o vertices")
    if cursor <= 0:
        errores.append("cursor de ventanas 0")
    if nb > 255:
        errores.append("nb %d" % nb)
    esperado = 32 + nn * 32 + nv * 40
    if size != esperado:
        errores.append("tamano %d != cabecera %d" % (size, esperado))
    for k in range(nn):
        bone, parent, root, _z, x, y, z, r = struct.unpack_from(">4I4f", data, 32 + 32 * k)
        if parent > nn:
            errores.append("nodo %d padre %d" % (k, parent))
        if parent and parent - 1 >= k:
            errores.append("nodo %d no topologico (padre %d)" % (k, parent))
        if (parent == 0) != bool(root):
            errores.append("nodo %d raiz/padre incoherentes" % k)
        if not np.isfinite([x, y, z, r]).all():
            errores.append("nodo %d no finito" % k)
    for k in range(nv):
        o = 32 + nn * 32 + 40 * k
        win, bone, a, b, c, d, w0, w1, w2, w3 = struct.unpack_from(">6I4f", data, o)
        if win >= cursor:
            errores.append("vertice %d ventana %d >= %d" % (k, win, cursor))
        if any(x and x > nn for x in (a, b, c, d)):
            errores.append("vertice %d nodo fuera de rango" % k)
        w = np.array([w0, w1, w2, w3])
        if not np.isfinite(w).all() or abs(w.sum() - 1.0) > 1e-4 or (w < 0).any():
            errores.append("vertice %d pesos %s" % (k, w.tolist()))
    return dict(magic="XV5S", version=ver, nodos=nn, vertices=nv, bytes=size, ventanas=cursor,
                huesos=nb, controlador="XV5S_fixed60_springs_body_spheres"), errores


def inspeccionar(bin_path):
    b = Path(bin_path).read_bytes()
    i = b.find(b"XV5S")
    if i < 0:
        return dict(bloque="off_no_SCD", nodos=0, vertices=0, ventanas=0, errores=[]), b
    size = struct.unpack_from(">I", b, i + 16)[0]     # cabecera: 4s7I (size en +16)
    stats, errores = validar(b[i:i + size])
    return dict(bloque="XV5S", offset=i, **(stats or {}), errores=errores), b


def modelos_con_fisica(raza):
    """Modelos del BCS de esa raza con pieza fisica valida (EMD y attachment en su ESK)."""
    import xv2_cac_b3 as C
    import xeno_model as xm
    c = C.L.cpk_xv2()
    bcs = C.L.leer_bcs(c.get("data/chara/%s/%s.bcs" % (raza, raza)))
    esk = xm.Esk(c.get("data/chara/%s/%s_000.esk" % (raza, raza)))
    clave = {"Bust": "bust", "Hair": "pelo", "Pants": "pants"}
    vistos, out = set(), []
    for ps in bcs.get("partsets", []):
        for tipo, v in (ps or {}).items():
            if tipo not in clave or not v.get("fisicas") or v.get("modelo", 0) <= 0:
                continue
            modelo = v["modelo"]
            if (tipo, modelo) in vistos:
                continue
            vistos.add((tipo, modelo))
            ok = True
            for fz in v["fisicas"]:
                if fz.get("hueso") not in esk.index:
                    ok = False
                ruta = "data/chara/%s/%s.emd" % (raza, fz.get("emd", ""))
                if not C.X._ci(c, ruta):
                    ok = False
                try:      # el attachment tiene que existir tambien en el ESK de la propia pieza SCD
                    e = xm.Esk(c.get(C.X._ci(c, "data/chara/%s/%s.esk" % (raza, fz.get("emd", "")))))
                    if fz.get("hueso") not in e.index:
                        ok = False
                except Exception:   # noqa: BLE001
                    ok = False
            if ok:
                out.append((clave[tipo], modelo))
    return out


def medir(razas=RAZAS, js=None):
    js = js or (HERE / "xv2_razas.json")
    gate = {}
    try:
        gate = json.loads(Path(js).read_text(encoding="utf-8")).get("razas", {})
    except (OSError, ValueError):
        pass
    res = {}
    for raza in razas:
        base = TRABAJO / "razas" / f"{raza}_cal.bin"
        if not base.is_file():
            res[raza] = dict(error="falta el bin; ejecuta xv2_razas.py --todo")
            continue
        info, _ = inspeccionar(base)
        info["raza"] = raza
        info["encendida"] = bool(gate.get(raza, {}).get("encendida"))
        for tipo, modelo in modelos_con_fisica(raza):
            sel = dict(C_DEFECTO(), raza=raza, ojos=1)
            sel[tipo] = modelo
            fis = TRABAJO / "razas" / f"{raza}_fis_{tipo}{modelo}.bin"
            try:
                if not fis.is_file():
                    _componer(sel, fis)
            except Exception as e:   # noqa: BLE001 (candidato sin attachment para esa raza)
                info.setdefault("intentos", []).append("%s=%d: %s" % (tipo, modelo, e))
                continue
            ifis, _ = inspeccionar(fis)
            ifis["pieza"] = "%s=%d" % (tipo, modelo)
            info["con_fisica"] = ifis
            break
        res[raza] = info
        cf = info.get("con_fisica") or {}
        print("%s: base %s nodos %d | con fisica %s nodos %d verts %d errores %d" % (
            raza, info.get("bloque"), info.get("nodos", 0), cf.get("pieza", "-"),
            cf.get("nodos", 0), cf.get("vertices", 0), len(cf.get("errores", []))))
    return res


def C_DEFECTO():
    import xv2_cac_b3 as C
    return dict(C.DEFECTO)


def _componer(sel, fis):
    import xv2_cac_b3 as C
    import xv2_fuente as F
    import tempfile
    fis.parent.mkdir(parents=True, exist_ok=True)
    tpl = F.plantilla_donante(0, None, str(Path(tempfile.mkdtemp(prefix="xv2_fis_", dir=TRABAJO)) / "plantilla.bin"))
    C.componer_xeno(sel, tpl, fis)


def selftest():
    from types import SimpleNamespace as Obj
    import xeno_fisica as F
    from xeno_a_b3 import synthetic_template, Retarget
    e, t = synthetic_template()
    rig = Retarget(e, t)
    head = t.bone("HEAD")
    extra = Obj(names=["anchor", "SCD_tip", "SCD_root"], parent=[-1, 2, 0],
                world=np.array([[0, 0, 0], [0, 1, 0], [0, 0, 0]], float))
    sm = Obj(bone_names=["SCD_tip"], bones=np.zeros((3, 4), int), weights=np.tile([1., 0, 0, 0], (3, 1)))
    p = dict(physics_esk=extra, attachment="b_C_Head", part="test", sm=sm)
    groups = {0: [dict(win=[b"body"])], head: [dict(win=[b"a", b"a", b"b"], physics_piece=p)]}
    blob, stats = F.metadata(rig, groups)
    assert stats["nodes"] == 2 and stats["vertices"] == 2
    info, errores = validar(blob)
    assert not errores, errores
    assert info["nodos"] == 2 and info["vertices"] == 2
    malo = bytearray(blob)
    struct.pack_into(">I", malo, 36, 99)          # padre imposible
    assert validar(bytes(malo))[1]
    # Sin pieza SCD el contrato no emite nada (lo que pasa en las seis razas del creador).
    assert F.metadata(rig, {0: [dict(win=[b"body"])]})[0] == b""
    print("xv2_fisica_heroe: OK (bloque XV5S valido/invalido y ausencia sin SCD; sin XV2)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--todo", action="store_true")
    ap.add_argument("--salida", type=Path, default=HERE / "xv2_fisica.json")
    a = ap.parse_args(argv)
    if a.selftest:
        selftest()
        return
    res = medir()
    a.salida.write_text(json.dumps(res, indent=1, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    print("escrito", a.salida)


if __name__ == "__main__":
    main()

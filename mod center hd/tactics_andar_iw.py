#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Andar de campo de IW para el horno TKB2; conserva los otros siete clips de B3.

No confundir los numeros de entrada de IW con los de B3. El manifiesto conserva
el clip B3 de respaldo y declara la fuente IW con las claves iw_* de `andar`.
La conexion del runtime es cac/vista_tactics_iw.py (misma CLI que el horno B3).
"""
import functools
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE / "studio"), str(HERE / "cac"), str(HERE.parent / "awo_tools")]
import numpy as np
import altura
import afs_pair
import studio_core as sc

ESENCIALES = ("WAIST", "STMC", "CHEST", "NECK", "HEAD", "LARM1", "RARM1",
              "LARM2", "RARM2", "LLEGROT", "RLEGROT", "LLEG1", "RLEG1",
              "LLEG2", "RLEG2", "LFOOT1", "RFOOT1")


def fuente_iw(juego=None, explicita=None):
    """Disco extraido/ISO/.ruta; respeta la biblioteca externa DBZ3_PS2_GAMES."""
    if explicita:
        candidatos = [Path(explicita)]
    else:
        candidatos = []
        for raiz in (os.environ.get("DBZ3_PS2_GAMES"),
                     str(Path(juego) / "ps2_games") if juego else None,
                     str(HERE.parent / "ps2_games")):
            if raiz and Path(raiz).is_dir():
                candidatos.extend(sorted(Path(raiz).glob("*Infinite World*")))
        candidatos.append(Path(afs_pair.PS2_IW))
    for p in candidatos:
        if p.suffix.lower() == ".ruta" and p.is_file():
            p = Path(p.read_text(encoding="utf-8-sig").strip())
        if p.is_file() and p.suffix.lower() == ".iso":
            return str(p)
        if p.is_dir():
            for q in (p, p / "USR", p / "usr"):
                if (q / "DATA_CMN.AFS").is_file() or (q / "data_cmn.afs").is_file():
                    return str(q)
    raise FileNotFoundError("Infinite World: falta DATA_CMN.AFS o la ISO en ps2_games")


@functools.lru_cache(maxsize=4)
def banco_iw(fuente, entrada):
    """Resuelve BSK y AMM por su tipo; 915 usa almacen 0 propio, 914 almacen 3."""
    afs = "DATA_CMN.AFS"
    if Path(fuente).is_dir() and not (Path(fuente) / afs).exists():
        afs = "data_cmn.afs"
    b = afs_pair.entry(os.path.join(fuente, afs), entrada)
    csk, e = sc.csk_of(b)
    acm, ea = sc.anim_pool(b)
    if csk is None or acm is None or acm[:4] != b"#AMM" or e != "<" or ea != "<":
        raise ValueError("IW entrada %d: no es el banco BSK/AMM esperado" % entrada)
    return csk, acm, ea


def clip_iw(fuente, entrada, codigo):
    csk, acm, e = banco_iw(fuente, entrada)
    info = sc.code_info(csk, e, codigo)
    esperado = 0 if entrada == 915 else 3
    n, tabla, _nb, _nombres = struct.unpack_from(e + "4I", acm, 0x10)
    if not info or info[1] != esperado or not 0 <= info[0] < n:
        raise ValueError("IW %d codigo %#x: animacion/almacen no resuelto" % (entrada, codigo))
    cuadros = struct.unpack_from(e + "I", acm, tabla + 16 * info[0] + 8)[0]
    if not 1 <= cuadros <= 600:
        raise ValueError("IW %d codigo %#x: cuadros invalidos" % (entrada, codigo))
    return acm, e, info[0], cuadros


def metadatos(bid, forma, fuente):
    # LocoDe: Cell tiene banco propio 914; el resto comparte 915. La familia
    # Kaio depende del minijuego del planeta de Kaio, NO de la forma Kaioken.
    # Tactics/Survivors no estan en ese planeta: todas las formas usan 15/16/17.
    banco = 914 if bid == 33 else 915
    andar, inicio, correr = 0x15, 0x16, 0x17
    out = dict(iw_banco=banco)
    for prefijo, codigo in (("iw", andar), ("iw_correr_inicio", inicio), ("iw_correr", correr)):
        _acm, _e, anim, cuadros = clip_iw(fuente, banco, codigo)
        out.update({prefijo + "_codigo": codigo, prefijo + "_anim": anim, prefijo + "_frames": cuadros})
    return out


def compatibilidad(skel, acm, e, anim):
    """Se exigen pistas de giro de piernas y tronco, no solo etiquetas parecidas."""
    _n, tabla, nb, nombres = struct.unpack_from(e + "4I", acm, 0x10)
    flags, _v, _nf, off = struct.unpack_from(e + "4I", acm, tabla + 16 * anim)
    per = 3 if flags & 0x10 else 2
    giros = set()
    for j in range(nb):
        if off and struct.unpack_from(e + "I", acm, off + 4 * per * j)[0]:
            nm = acm[nombres + 32 * j:nombres + 32 * (j + 1)].split(b"\0")[0].decode("latin1")
            giros.add(altura.suffix(nm))
    nombres_destino = [altura.suffix(x[0]) for x in skel]
    faltan = sorted(set(ESENCIALES) - (set(nombres_destino) & giros))
    duplicados = sorted(n for n in ESENCIALES if nombres_destino.count(n) > 1)
    if faltan or duplicados:
        raise ValueError("esqueleto incompatible: faltan %s; duplicados %s" % (faltan, duplicados))
    return len(giros & set(nombres_destino))


def separar_tk(datos):
    """Vista validada de los bloques; permite demostrar que solo cambia andar."""
    if datos[:4] != b"TKB2":
        raise ValueError("no es TKB2")
    nb, nc = struct.unpack_from("<II", datos, 4)
    if not 0 < nb <= 256 or nc != 8:
        raise ValueError("cabecera TKB2 invalida")
    bloques, pos = [], 12
    for _ in range(nc):
        n, bucle = struct.unpack_from("<II", datos, pos)
        fin = pos + 8 + n * nb * 48
        if fin > len(datos) or n > 10000 or bucle > 1:
            raise ValueError("clip TKB2 truncado o invalido")
        bloques.append(bytes(datos[pos:fin]))
        pos = fin
    if pos != len(datos):
        raise ValueError("bytes sobrantes en TKB2")
    return nb, bloques


def matrices(bloque, nb):
    n = struct.unpack_from("<I", bloque)[0]
    return np.frombuffer(bloque, dtype="<f4", offset=8).reshape(n, nb, 3, 4).copy()


def _pierna(skel):
    return sum(float(np.linalg.norm(p)) for nm, _q, p, _par in skel
               if altura.suffix(nm) in ("LLEG2", "LFOOT1"))


def hornear_andar(skel, acm, e, anim, cuadros, padres, fijas, referencia):
    """Giros por sufijo, longitudes del destinatario y cadera proporcional.

    Igual que anim_iw.cpp con Cell, las posiciones ajenas no alargan huesos.
    Se cierra la deriva vertical de WAIST; x/z pertenecen al tablero. Pelo,
    alas y cola sin pista mantienen la pose que ya obtiene el horno B3.
    """
    compatibilidad(skel, acm, e, anim)
    escala = _pierna(skel) / _pierna(referencia)
    if not np.isfinite(escala) or not 0.15 < escala < 4:
        raise ValueError("proporcion de piernas invalida: %s" % escala)
    cintura = next(i for i, s in enumerate(skel) if altura.suffix(s[0]) == "WAIST")
    reposo = np.array([p for _nm, _q, p, _par in skel], float)
    salida = np.array([sc.anim_locals(skel, acm, e, anim, f) for f in range(cuadros)])[:, :, :3, :4]
    # La pista es relativa al reposo del DONANTE. Goku tiene WAIST.y=-0.9718,
    # mientras Vegeta/Krilin lo tienen a cero. Escalar solo el delta deja a esos
    # modelos flotando: hay que escalar la altura completa del donante.
    cintura_ref = next(s for s in referencia if altura.suffix(s[0]) == "WAIST")
    y = (salida[:, cintura, 1, 3] - reposo[cintura, 1] + cintura_ref[2][1]) * escala
    if cuadros > 1:
        y -= np.linspace(0, 1, cuadros) * (y[-1] - y[0])
    salida[:, :, :, 3] = reposo[None, :, :]
    salida[:, cintura, 1, 3] = y
    for i, r in fijas.items():
        salida[:, i, :3, :3] = r
    # Verificar el resultado que consumira C++ (matrices de giro, no deformaciones).
    rot = salida[:, :, :3, :3]
    error = np.max(np.abs(np.swapaxes(rot, -1, -2) @ rot - np.eye(3)))
    if not np.isfinite(salida).all() or error > 1e-4:
        raise ValueError("andar con matrices invalidas: %s" % error)
    return salida.astype("<f4")


def hornear_unidad(afs, uni, cid, fuente=None, correr=False, exigir_iw=False):
    """API compatible con vista_tactics.hornear_unidad; ausencia IW = respaldo B3."""
    import vista_tactics as vt
    datos, error = vt.hornear_unidad(afs, uni, cid)
    if error:
        return datos, error
    c = uni["clips"].get("andar", {})
    if "iw_banco" not in c:
        if exigir_iw:
            raise ValueError("%s: el manifiesto no declara animacion IW" % uni["id"])
        return datos, None
    try:
        fuente = fuente or fuente_iw()
    except FileNotFoundError:
        if exigir_iw:
            raise
        print("andar_iw: %s: sin IW, respaldo B3" % uni["id"], flush=True)
        return datos, None
    entrada = int(c["iw_banco"])
    prefijo = "iw_correr" if correr else "iw"
    acm, e, anim, cuadros = clip_iw(fuente, entrada, int(c[prefijo + "_codigo"]))
    if anim != c[prefijo + "_anim"] or cuadros != c[prefijo + "_frames"]:
        raise ValueError("%s: fuente IW distinta al manifiesto (regenera)" % uni["id"])
    mb = afs.entry(int(uni["modelo"]))
    skel = altura.skeleton(mb)
    modelo = vt.vh.mr.Model(mb)
    acm_b3, eb3 = vt.banco_anim(afs, uni["banco"])
    padres = [int(p) for p in modelo.parent]
    fijas = vt.poses_fijas(afs, modelo, skel, acm_b3, eb3, cid, padres)
    sin = vt.sin_pista(skel, acm, e, anim)
    fijas = {i: r for i, r in fijas.items() if i in sin}
    # Modelos de referencia nativos: mismas proporciones que los bancos de campo
    # GOK y CEL, ya documentadas por el oraculo PS2/HD.
    referencia = altura.skeleton(afs.entry(146 if entrada == 914 else 264))
    poses = hornear_andar(skel, acm, e, anim, cuadros, padres, fijas, referencia)
    nb, bloques = separar_tk(datos[0])
    bloques[1] = struct.pack("<II", cuadros, 1) + poses.tobytes()
    out = b"TKB2" + struct.pack("<II", nb, len(bloques)) + b"".join(bloques)
    print("andar_iw: %s IW %d codigo %#x anim %d (%d cuadros), %s" % (
        uni["id"], entrada, c[prefijo + "_codigo"], anim, cuadros, "correr" if correr else "andar"), flush=True)
    return (out, nb), None

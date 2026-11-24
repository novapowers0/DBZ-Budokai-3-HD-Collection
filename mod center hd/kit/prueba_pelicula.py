"""Prueba de kit/pelicula.py y de biblioteca_anim.aplicar_receta (KIT-08). Sin frameworks: asserts.

  python "mod center hd/kit/prueba_pelicula.py" [--trabajo E:/DBZ3HD_trabajo/kit_08]

Escribe solo en la carpeta de trabajo (cache de la pelicula y el mod de prueba) y la tira de evidencia en
studies 5/ENTREGAS/KIT_VISUAL/real_receta_goku.png. El mod de prueba (para probarlo luego en el juego):
<trabajo>/mods/kit08_goku_patada_vegeta (Goku 0x217 con la patada 0x217 de Vegeta).
"""
import argparse
import os
import shutil
import struct
import sys
import time

HD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HD)
from kit import base, pelicula as pel  # noqa: E402

ba = pel.ba
sc = pel.sc
GOKU, VEGETA, PATADA = 0, 7, 0x217


def leer_mod(ruta):
    """(kids [(bytes, tipo)], csk, nombres, anims) del geom.bin que deja aplicar_receta (con el parser)."""
    from sb_tecnicas import kids  # noqa: PLC0415
    with open(ruta, "rb") as fh:
        b = base.lzx_bytes(fh.read(), False)
    ks = kids(b, ">")
    csk = next(x for x, _ in ks if x[:4] == b"#CSK")
    names, anims = ba._rb().acm_read(next(x for x, t in ks if t == 3))
    return ks, csk, names, anims


def aparta(d, trabajo):
    """Un mod de prueba anterior se aparta (no se borra) para empezar de cero."""
    if os.path.isdir(d):
        dst = os.path.join(trabajo, "_viejos", "%s_%s" % (os.path.basename(d), sc.stamp()))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.move(d, dst)


def prueba_operaciones(cin):
    v = ba.clip_de(dict(personaje=VEGETA, codigo=PATADA))          # 49 fotogramas
    rot = lambda c, b, f: cin._keys(c[3][b][0], "rot")[1][f][1:]  # noqa: E731
    r = ba.recortar(v, 5, 20)
    assert r[2] == 16 and all(rot(r, "CHEST", k) == cin._rot_at(cin._keys(v[3]["CHEST"][0], "rot")[1], 5 + k)
                              for k in range(16)), "recorte"
    d = ba.cambiar_duracion(v, 97)
    assert d[2] == 97 and rot(d, "CHEST", 0) == rot(ba.recortar(v, 0, 48), "CHEST", 0)
    assert rot(d, "CHEST", 96) == rot(ba.recortar(v, 0, 48), "CHEST", 48)
    assert rot(d, "CHEST", 48) == rot(ba.recortar(v, 0, 48), "CHEST", 24), "duracion"
    g = ba.clip_de(dict(personaje=GOKU, codigo=PATADA))            # 79 fotogramas
    m = ba.mezclar(v, g, 6)
    full_v, full_g = ba.recortar(v, 0, 48), ba.recortar(g, 0, 78)
    assert m[2] == 49 + 79 - 6
    assert rot(m, "CHEST", 0) == rot(full_v, "CHEST", 0) and rot(m, "CHEST", 42) == rot(full_v, "CHEST", 42)
    assert rot(m, "CHEST", 49) == rot(full_g, "CHEST", 6) and rot(m, "CHEST", m[2] - 1) == rot(full_g, "CHEST", 78)
    mid = rot(m, "CHEST", 45)                                      # dentro de la mezcla: entre los dos
    assert mid != rot(full_v, "CHEST", 45) and mid != rot(full_g, "CHEST", 2), "mezcla"
    print("[OK] recorte 5-20 = 16 f, duracion 49->97 f, mezcla 49+79-6 = %d f" % m[2])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabajo", default=r"E:\DBZ3HD_trabajo\kit_08")
    a = ap.parse_args()
    cin = ba._cin()
    cache = os.path.join(a.trabajo, "cache_prueba", sc.stamp())     # nueva: mide el render de verdad

    # ---- pelicula: tiempos y cache
    t = time.time()
    rutas = pel.pelicula(GOKU, PATADA, "lado", 240, cache=cache, trabajadores=1)
    t240 = time.time() - t
    assert len(rutas) == 79 and all(os.path.isfile(r) for r in rutas)
    mt = [os.path.getmtime(r) for r in rutas]
    t = time.time()
    assert pel.pelicula(GOKU, PATADA, "lado", 240, cache=cache) == rutas
    t2 = time.time() - t
    assert [os.path.getmtime(r) for r in rutas] == mt, "la segunda llamada no debe renderizar"
    t = time.time()
    rp = pel.pelicula(GOKU, PATADA, "lado", 240, cache=cache + "_p", trabajadores=4)
    tpar = time.time() - t
    assert len(rp) == 79
    t = time.time()
    r480 = pel.pelicula(GOKU, PATADA, "frente", 480, cache=cache, paso=8, trabajadores=1)
    t480 = time.time() - t
    fino = pel.fotograma_fino(GOKU, PATADA, 40, "3/4", 720, cache=cache)
    assert os.path.isfile(fino)
    mk = pel.marcas(GOKU, PATADA)
    assert (10, "golpe") in mk and (16, "golpe") in mk, mk
    print("[OK] pelicula 79 f a 240 px: %.1f s (1 proceso, %.3f s/f; con encuadre), %.1f s (4 procesos); "
          "cache 2a llamada %.2f s; 480 px: %.3f s/f (%d f); marcas golpe %s"
          % (t240, t240 / 79, tpar, t2, t480 / len(r480), len(r480), [f for f, k in mk if k == "golpe"]))

    # ---- operaciones de clip
    prueba_operaciones(cin)

    # ---- validacion
    assert any(x.startswith("ERROR") for x in ba.validar_receta(
        {"destino": dict(personaje=GOKU, codigo=0x7FFF), "fuente": dict(personaje=VEGETA, codigo=PATADA)}))
    av = ba.validar_receta({"destino": dict(personaje=GOKU, codigo=PATADA),
                            "fuente": dict(personaje=VEGETA, codigo=PATADA, desde=0, hasta=8)})
    assert any("golpes del destino" in x for x in av), av      # golpes de Goku en 10/16/38/42 > 8
    av = ba.validar_receta({"destino": dict(personaje=GOKU, codigo=PATADA),
                            "fuente": dict(personaje=VEGETA, codigo=PATADA), "duracion": 5000})
    assert any("los del juego van" in x for x in av), av
    av = ba.validar_receta({"destino": dict(personaje=GOKU, codigo=PATADA),
                            "fuente": dict(personaje=GOKU, codigo=0x4A0)})
    assert any("cinematica" in x for x in av), av
    print("[OK] validar_receta: codigo inexistente, golpes fuera, duracion, cinematica sin camara")

    # ---- aplicar_receta: patada 0x217 de Vegeta en la 0x217 de Goku
    mod = os.path.join(a.trabajo, "mods", "kit08_goku_patada_vegeta")
    aparta(mod, a.trabajo)
    receta = {"destino": dict(personaje=GOKU, codigo=PATADA), "fuente": dict(personaje=VEGETA, codigo=PATADA)}
    res = ba.aplicar_receta(receta, mod)
    from sb_tecnicas import kids  # noqa: PLC0415
    orig = sc.hd_entry(ba._persona(GOKU)["anm"])
    oks = kids(orig, ">")
    ocsk = next(x for x, _ in oks if x[:4] == b"#CSK")
    onames, oanims = ba._rb().acm_read(next(x for x, t in oks if t == 3))
    ks, csk, names, anims = leer_mod(res["ruta"])
    nuevo = sc.code_info(csk, ">", PATADA)[0]
    assert nuevo == len(oanims) and len(anims) == len(oanims) + 1
    v = ba.clip_de(dict(receta["fuente"]))
    clip = anims[nuevo]
    assert clip[2] == v[2] == 49
    for nm in names:                                   # los fotogramas son los de la fuente (por hueso)
        suf = ba._rb()._suffix(nm)
        assert clip[3][suf] == (v[3].get(suf) or [None, None])[:2], suf
    assert anims[:len(oanims)] == oanims, "otro clip cambio"
    sub = ba._subbloques(ocsk, PATADA)[0]
    esperado = bytearray(ocsk)
    struct.pack_into(">H", esperado, sub, nuevo)
    assert csk == bytes(esperado), "el #CSK solo cambia en el clip del 0x217"
    assert [x for x, t in ks if t == 3][1:] == [x for x, t in oks if t == 3][1:], "#ACM 2/3"
    assert os.path.getsize(res["ruta"]) >= sc.afs_slot(ba._persona(GOKU)["anm"])
    assert os.path.isfile(os.path.join(mod, "manifest.txt"))
    print("[OK] aplicar_receta: %s; %d clips viejos iguales byte a byte; #CSK igual salvo 2 bytes; %s"
          % (res["informe"], len(oanims), res["ruta"]))

    # ---- receta con recorte + empalme + duracion (otro mod), releida con el parser
    mod2 = os.path.join(a.trabajo, "mods", "kit08_prueba_empalme")
    aparta(mod2, a.trabajo)
    r2 = {"destino": dict(personaje=GOKU, codigo=PATADA), "fuente": dict(personaje=VEGETA, codigo=PATADA, desde=0, hasta=30),
          "despues": dict(personaje=GOKU, codigo=PATADA, desde=40, hasta=78), "mezcla": 5, "duracion": 60}
    res2 = ba.aplicar_receta(r2, mod2)
    _ks, csk2, names2, anims2 = leer_mod(res2["ruta"])
    c2, ref = anims2[sc.code_info(csk2, ">", PATADA)[0]], ba.clip_receta(r2)
    assert c2[2] == 60 and all(c2[3][s] == (ref[3].get(s) or [None, None]) for s in map(ba._rb()._suffix, names2))
    print("[OK] receta recorte+empalme+duracion: %d f (%s)" % (c2[2], "; ".join(res2["avisos"]) or "sin avisos"))

    # ---- evidencia visual: Goku con el clip resultante
    film = pel.pelicula(GOKU, PATADA, "lado", 240, anm=res["ruta"], cache=cache)
    assert len(film) == 49
    out = os.path.join(base.RAIZ, "studies 5", "ENTREGAS", "KIT_VISUAL", "real_receta_goku.png")
    pel.tira(film, out, 8)
    pel.tira(pel.pelicula(VEGETA, PATADA, "lado", 240, cache=cache), os.path.join(a.trabajo, "vegeta_0x217.png"), 8)
    print("[OK] tira:", out)
    print("RESULTADO: todo OK")


if __name__ == "__main__":
    main()

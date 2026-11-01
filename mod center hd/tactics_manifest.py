#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tactics_manifest.py - Manifiesto de clips de Dragon Tactics (T5). Lee data_cmn.afs con biblioteca_anim.

  python tactics_manifest.py generar    # reescribe tactics_manifest.toml desde el juego
  python tactics_manifest.py validar     # falla si falta un clip obligatorio o si el toml no coincide con el juego

Clip = codigo BSK (#CSK) -> clip del #ACM. El #ACM es el del bin anm[0] del personaje (los bins de forma
solo traen #CSK y apuntan a el); el codigo se busca primero en el bin de la forma y luego en anm[0].
estado: medido (valido por RE/uso), inferido (por rango o parecido, sin confirmar en juego), pendiente (no resuelto).
"""
import json
import os
import sys

import biblioteca_anim as ba

sc = ba.sc
AQUI = os.path.dirname(os.path.abspath(__file__))
TOML = os.path.join(AQUI, "tactics_manifest.toml")
OBLIGATORIOS = ("espera", "andar", "ataque", "tecnica")
OPCIONALES = ("dano", "derrota", "carga_ki", "transformacion")
BUCLE = {"espera": True, "andar": True, "carga_ki": True}
# H9: cadena de formas por unidad (1 a 4 estrellas = su siguiente transformacion NATIVA de B3): (forma, ...) del propio personaje. El modelo
# de la forma k es models[form_models[k][1]] de roster_db.json (lo mismo que FidB3 del juego); las formas que son OTRO id (SSJ4...) no se usan.
# unidad: (clave, id B3, id B2 de la rueda, formas)   [b2 = -1: sin id de rueda de B2; no se usa]
CADENAS = (("goku", 0, 0, (0, 1, 2, 3)), ("vegeta", 7, 3, (0, 1, 4)), ("piccolo", 11, 6, (0,)), ("krilin", 10, 4, (0,)),
           ("a16", 28, 16, (0,)), ("a18", 30, 18, (0,)), ("freezer", 27, 15, (1, 2, 3, 4)), ("cell", 33, 20, (0, 1, 2)),
           ("gohan", 3, -1, (0, 1, 2)), ("trunks", 8, -1, (0, 1)), ("goten", 6, -1, (0, 1)), ("superbuu", 35, -1, (0,)),
           ("broly", 40, -1, (0, 1)), ("bardock", 39, -1, (0,)), ("cooler", 38, -1, (0, 1, 2)), ("recoome", 21, -1, (0,)),
           # H11: 14 mas, todos ids 0-43 del juego base (default.xex) con modelo propio; ninguno del roster extendido ni de ports
           ("saibaman", 42, -1, (0,)), ("tenshinhan", 12, -1, (0,)), ("nappa", 19, -1, (0,)),
           ("a17", 29, -1, (0,)), ("dabura", 37, -1, (0,)), ("gero", 32, -1, (0,)), ("celljr", 43, -1, (0,)),
           ("satan", 14, -1, (0,)), ("kaioshin", 16, -1, (0,)), ("videl", 15, -1, (0,)),
           ("buuninio", 36, -1, (0,)), ("gohankid", 2, -1, (0,)), ("trunkskid", 9, -1, (0,)), ("gokukid", 1, -1, (0,)),
           # H12: Yamcha, Raditz, Uub y Ginyu (ids 13, 18, 17, 20 del juego base). Les faltan codigos de la lista comun en su banco: ver EXTRA
           ("yamcha", 13, -1, (0,)), ("raditz", 18, -1, (0,)), ("uub", 17, -1, (0,)), ("ginyu", 20, -1, (0,)))


def _fid(bid, forma):
    e = next(x for x in json.load(open(os.path.join(AQUI, "roster_db.json"), encoding="utf-8"))["ids"] if x["id"] == bid)
    m = e["form_models"][forma]
    assert m[0] == bid, "%d forma %d es otro id" % (bid, forma)
    return e["models"][m[1]]


# entradas del manifiesto: la 1 estrella lleva el id de la unidad; las demas "<unidad>_<k>" (k = estrella 2..4). (clave, b3, b2, forma, fid)
UNIDADES = tuple((c if k == 0 else "%s_%d" % (c, k + 1), bid, b2, f, _fid(bid, f)) for c, bid, b2, fs in CADENAS for k, f in enumerate(fs))
# clip -> lista de (codigo, estado, nota); gana el primero que exista
CANDIDATOS = {
    "espera": [(0x000, "medido", "neutro 0 (RONDA4_PULIDO.md)")],
    "andar": [(0x00F, "medido", "andar por el suelo (altura.py GROUND_CODES)"),
              (0x002, "medido", "andar por el suelo, codigo 2 (altura.py GROUND_CODES): el andar real de los que no traen 0xF"),
              (0x009, "inferido", "codigo 9 (serie 9-0xB de los ninos: Goku, Trunks, Gohan ninos): andar del suelo"),
              (0x0F4, "inferido", "sin andar en el suelo: bucle de 60 cuadros de la familia 0xF4-0xFF (vuelo); sin confirmar en juego")],
    "ataque": [(0x21B, "medido", "combo >P (ACM_FORMAT.md)")],
    "tecnica": [(0x258, "inferido", "bloque hiper/definitiva (CAPSULAS_B3.md:202); puede ser cinematica")],
    # dsTAC: pose del ganador al acabar la ronda (sonda de combate: estado 6, accion 15 codigo 0x2A1; 120 cuadros, bucle al final)
    "victoria": [(0x2A1, "medido", "pose del ganador en el resultado (sonda de combate 1/11: accion 15 cod 0x2A1)")],
}
# dsTAC: clips de reaccion del banco COMUN (entrada 165 de data_cmn; tabla compartida de 2172 codigos). Medido con la sonda de
# combate 1/11 (accion/codigo por cuadro) y visto en el render offline: 0x9A = golpe recibido, 0x24 = KO tumbado, 0x25 = se levanta.
BANCO_COMUN = 165
COMUNES = {
    "dano": (0x9A, "golpe recibido (accion 20/22 del motor)"),
    "derrota": (0x24, "KO tumbado boca abajo (accion 18)"),
    "levantarse": (0x25, "se levanta desde el suelo (accion 19)"),
}
# H12: causa real de que se descartaran en H11: sus bancos no traen todos los codigos comunes (medido con code_info sobre su #CSK, banco propio = pool 3):
# Yamcha y Ginyu no definen ningun codigo de suelo (0-0x20: ni espera ni andar), Uub no tiene 0xF/0xF4 (andar) ni 0x21B, Raditz no tiene 0x21B.
# Se usa lo que SI tienen, tras los candidatos normales (el primero que exista gana): bucle de vuelo 0xF4 como espera, la espera como andar,
# y el golpe vecino de la serie 0x210-0x21F. Estado "inferido": se ve a simple vista en las fotos de H12.
EXTRA = {
    "yamcha": {"espera": [(0x0F4, "inferido", "sin espera de suelo en su banco: bucle de vuelo")]},
    "ginyu": {"espera": [(0x0F4, "inferido", "sin espera de suelo en su banco: bucle de vuelo")]},
    "uub": {"andar": [(0x000, "inferido", "sin andar ni vuelo en su banco: la espera")], "ataque": [(0x21C, "inferido", "sin 0x21B: golpe vecino de la serie")]},
    "raditz": {"ataque": [(0x21A, "inferido", "sin 0x21B: golpe vecino de la serie")]},
}
PENDIENTES = {
    "carga_ki": "accion 21 del motor; codigo sin medir",
    "transformacion": "0x2E0/0x3E0 apunta a otro banco (pool 0) en casi todos: no resuelto aqui",
}


def _datos(uid, bid, forma):
    e = next(x for x in json.load(open(os.path.join(AQUI, "roster_db.json"), encoding="utf-8"))["ids"] if x["id"] == bid)
    a0, af = e["anm"][0], e["anm"][forma]
    if af >= 0xFFFFFFF0:   # la forma no trae banco propio: usa el del personaje
        af = a0
    anm0, anmf = sc.hd_entry(a0), sc.hd_entry(af)
    csk0, en = sc.csk_of(anm0)
    cskf, _ = sc.csk_of(anmf)
    acm, _ = sc.anim_pool(anm0)
    return a0, af, csk0, cskf, acm, en


def _clip(csk0, cskf, acm, en, codigo):
    info = sc.code_info(cskf, en, codigo) or sc.code_info(csk0, en, codigo)
    if not info or info[1] != 3:
        return None
    fr = ba.frames_de(acm, en, info[0])
    return (info[0], fr) if fr else None


def construir():
    out = []
    # dsTAC: banco comun de reacciones (una sola lectura para las 51 entradas)
    comun = sc.hd_entry(BANCO_COMUN)
    csk_c, ec = sc.csk_of(comun)
    acm_c, enc = sc.anim_pool(comun)
    for uid, bid, b2, forma, fid in UNIDADES:
        a0, af, csk0, cskf, acm, en = _datos(uid, bid, forma)
        u = dict(id=uid, b3=bid, b2=b2, forma=forma, traje=0, modelo=fid, anm_forma=af, banco=a0, clips={})
        for nom, cands in CANDIDATOS.items():
            for cod, est, nota in cands + EXTRA.get(uid, {}).get(nom, []):
                r = _clip(csk0, cskf, acm, en, cod)
                if r:
                    u["clips"][nom] = dict(codigo=cod, anim=r[0], frames=r[1], bucle=BUCLE.get(nom, False), estado=est, nota=nota)
                    break
        if uid in ("yamcha", "ginyu"):   # H14: su banco no trae espera ni pose de pie (no son luchadores de B3: sin codigo 0 ni 2); espera = pose de espera generica del select (banco 3881, anim 5, 61 cuadros)
            u["clips"]["espera"] = dict(codigo=-1, anim=5, frames=61, bucle=True, banco=3881, estado="inferido", nota="sin espera propia: espera generica del select (banco 3881 anim 5)")
            u["clips"]["andar"] = dict(u["clips"]["espera"], nota="sin andar propio: se desliza en la espera generica del select (banco 3881 anim 5)")
        an = u["clips"].get("andar")
        if an and an["codigo"] == 0xF4 and uid != "freezer" and "espera" in u["clips"]:   # H14: sin andar de suelo y no vuela por naturaleza: se desliza en su espera (el bucle de vuelo inclina y abre las piernas)
            u["clips"]["andar"] = dict(u["clips"]["espera"], nota="sin andar de suelo en su banco: se desliza en su espera")
        for nom, (cod, nota) in COMUNES.items():   # dsTAC: golpe recibido, KO y levantarse del banco comun
            info = sc.code_info(csk_c, ec, cod)
            anim = info[0] if info else None
            fr = ba.frames_de(acm_c, enc, anim) if anim is not None else None
            if anim is not None and fr:
                u["clips"][nom] = dict(codigo=cod, anim=anim, frames=fr, bucle=False, banco=BANCO_COMUN, estado="medido", nota=nota)
            else:
                u["clips"][nom] = dict(estado="pendiente", nota=nota + " (no resuelto en el banco comun)")
        for nom, nota in PENDIENTES.items():
            u["clips"][nom] = dict(estado="pendiente", nota=nota)
        out.append(u)
    return out


def escribir(unidades, path=TOML):
    L = ["# Generado por tactics_manifest.py generar (no editar a mano). Ver el docstring del script.", "version = 1", "",
         "[reglas]",
         '# Python (studio_core.anim_locals) SUMA la posicion de la pista al reposo; C++ (pasarela_nave.cpp EjesMundo, modo_rel 0) la SUSTITUYE.',
         '# Tactics usa el C++: la raiz de la animacion NO se suma al movimiento del tablero. La figura se coloca en su casilla con',
         '# Escena::Poner y el desplazamiento horizontal (x, z) de la pista de la cadera se anula; el avance entre casillas lo da el tablero.',
         'raiz = "ignorar_xz"', 'cuadros_por_segundo = 60', ""]
    for u in unidades:
        L += ["[[unidad]]"] + ["%s = %s" % (k, json.dumps(v)) for k, v in u.items() if k != "clips"]
        for nom, c in u["clips"].items():
            L.append("[unidad.clips.%s]" % nom)
            L += ["%s = %s" % (k, json.dumps(v, ensure_ascii=False)) for k, v in c.items()]
        L.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L))


def validar(path=TOML):
    import tomllib
    with open(path, "rb") as fh:
        m = tomllib.load(fh)
    prob = []
    if m.get("reglas", {}).get("raiz") != "ignorar_xz":
        prob.append("falta la regla de raiz")
    us = m.get("unidad", [])
    for c, _b, _b2, fs in CADENAS:   # H9: la cadena de cada unidad en tactics_units.toml (formas = [fid...]) es la del manifiesto
        fids = [x[4] for x in UNIDADES if x[0] == c or x[0].startswith(c + "_")]
        with open(os.path.join(AQUI, "tactics_units.toml"), "rb") as fh:
            uu = next((x for x in tomllib.load(fh)["unidad"] if x["id"] == c), None)
        if uu is None or list(uu.get("formas", [])) != fids:
            prob.append("%s: 'formas' de tactics_units.toml debe ser %s" % (c, fids))
    if len(us) != len(UNIDADES):
        prob.append("hay %d unidades, deben ser %d" % (len(us), len(UNIDADES)))
    real = {u["id"]: u for u in construir()}
    for u in us:
        for nom in OBLIGATORIOS:
            c = u.get("clips", {}).get(nom)
            if not c or c.get("estado") == "pendiente" or not c.get("frames"):
                prob.append("%s: falta el clip obligatorio '%s'" % (u["id"], nom))
        for nom in OPCIONALES:
            if nom not in u.get("clips", {}):
                prob.append("%s: '%s' ni resuelto ni marcado pendiente" % (u["id"], nom))
        if u["id"] in real and real[u["id"]]["clips"] != u["clips"]:
            prob.append("%s: el toml no coincide con el juego (regenera)" % u["id"])
    for p in prob:
        print("FALLO:", p)
    print("%s: %d unidades, %d problemas" % ("OK" if not prob else "FALLO", len(us), len(prob)))
    return not prob


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "generar":
        escribir(construir())
        print("escrito", TOML)
    elif cmd == "validar":
        sys.exit(0 if validar() else 1)
    else:
        print(__doc__)
        sys.exit(2)

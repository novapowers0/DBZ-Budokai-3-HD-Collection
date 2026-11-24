"""R16: captura reproducible de cola FRI; siempre por pruebas.py y su turno."""
import json
import re
import shutil
import struct
import time

from PIL import Image, ImageDraw


def registrar(g):
    def prep(j, par, env):
        if par.get("xv2", "FRI") != "FRI" or int(par.get("donante", "27")) != 27:
            raise g["Fallo"]("cola_fri_r16 requiere xv2=FRI y donante=27")
        par.update(xv2="FRI", donante="27", fase="cola")
        par["piezas_xv2"] = "bust:%s,pants:%s,boots:%s,rist:%s,cara:1,pelo:1,ojos:1" % tuple(
            par.get(k, "300") for k in ("bust", "pants", "boots", "rist"))
        env["DBZ3_TEST_XV2_COLA_ANIMADA"] = par.get("animada", "1")
        # Un unico mod evita herencias ajenas y regenerar los otros 58 modelos.
        # pruebas.py ya mantiene su transaccion verificada sobre toda la copia.
        apartados = j.run / "mods_ajenos_apartados"
        apartados.mkdir()
        for mod in (j.dest / "mods").iterdir():
            if mod.is_dir() and mod.name not in ("_roster", "heroe_xv2_fri"):
                shutil.move(str(mod), str(apartados / mod.name))
        g["_prep_heroe_exportado"](j, par, env)
        # El hub solo admite IDs guest 0..104, nunca IDs logicos 10000+.
        # Reserva una plaza libre SOLO en el roster generado de esta prueba.
        roster = j.dest / "mods" / "_roster" / "roster.toml"
        original = roster.read_bytes()
        tabla = g["tomllib"].loads(original.decode("utf-8"))
        plaza = int(par.get("plaza", "52"))
        previo = par["_id"]
        if not 44 <= plaza <= 63 or (previo != plaza and any(p.get("id") == plaza for p in tabla.get("personaje", []))):
            raise g["Fallo"]("cola_fri_r16: plaza de prueba ocupada o fuera de 44..63")
        texto, n = re.subn(r"(?m)^id = %d(?=\r?$)" % previo, "id = %d" % plaza, original.decode("utf-8"))
        if n != 1:
            raise g["Fallo"]("cola_fri_r16: fila exportada ambigua")
        (j.run / "roster_antes_cola.toml").write_bytes(original)
        cerrar = j.cerrar

        def restaurar():
            try:
                cerrar()
            finally:
                roster.write_bytes(original)
        j.cerrar = restaurar
        g["tomllib"].loads(texto)
        roster.write_bytes(texto.encode("utf-8"))
        par["_id"] = plaza
        env.update(DBZ3_TEST_BATTLE=f"{plaza},27,2,0,50000,50000",
                   DBZ3_TEST_FREECAM="0,0,0,0,0,45", DBZ3_TEST_FREECAM_LOG="2", DBZ3_TEST_FREECAM_HUD="1",
                   DBZ3_COMBATE_SONDA="1", DBZ3_TEST_FORMAS="1",
                   DBZ3_TEST_PKG="1" if par.get("fase_cola", "base") == "forma" else "0",
                   DBZ3_TEST_FORMAS_AUTO="0")
        cfg = "dbz3_combate_prueba_muneco=quieto;dbz3_cine_intro=false;dbz3_camara_sigue=false"
        env["DBZ3_TEST_CFG"] = ";".join(filter(None, (env.get("DBZ3_TEST_CFG", ""), cfg)))
        j.info(f"COLA FRI: logico {previo} -> plaza temporal {plaza}; donante 27; animada={par.get('animada', '1')}")

    def prueba(j, par):
        g["_esperar_pelea"](j, 120)
        j.log(r"camara libre: activa", 30)
        g["_colocar"](j, ["-30,0,0", "30,0,0"])
        mgr = int.from_bytes(g["leer"](j.h, g["GBASE"] + 0x823D6CF4, 4), "big")
        actor = mgr + 2096
        pie = struct.unpack(">3f", g["leer"](j.h, g["GBASE"] + actor + 64, 12))
        # Vista trasera de tres cuartos, cola y cuerpo completos con camara fija.
        objetivo = (pie[0], pie[1] + 8, pie[2] - 2)
        ojo = (objetivo[0] - 22, objetivo[1] + 4, objetivo[2] - 30)
        pedido = "mirar 91 %s 45" % " ".join(f"{v:.5f}" for v in (*ojo, *objetivo))
        (j.dest / "dbz3_freecam.req").write_text(pedido, encoding="ascii")
        j.log(r"camara libre: prueba mirar 91 ojo", 20)
        g["dormir"](0.5)
        datos = []

        def tira(nombre, tecla=None, n=6):
            if tecla:
                j.tecla(tecla, 0)
            fotos = []
            for i in range(n):
                g["dormir"](float(par.get("paso", "0.18")))
                foto = f"cola_{nombre}_{i:02d}"
                g["_tactics_reducir"](j, foto)
                with Image.open(j.run / (foto + ".png")) as im:
                    fotos.append(im.convert("RGB").copy())
                accion = int.from_bytes(g["leer"](j.h, g["GBASE"] + actor + 3244, 2), "big")
                forma = int.from_bytes(g["leer"](j.h, g["GBASE"] + actor + 2980, 4), "big")
                datos.append(dict(fase=nombre, indice=i, tiempo=time.time() - j.t0, imagen=foto + ".png", accion=accion, forma=forma))
            w, h = fotos[0].size
            hoja = Image.new("RGB", (w * 3, (h + 24) * ((len(fotos) + 2) // 3)), "#151515")
            d = ImageDraw.Draw(hoja)
            for i, im in enumerate(fotos):
                x, y = (i % 3) * w, (i // 3) * (h + 24)
                d.text((x + 8, y + 5), f"{nombre} / {i}", fill="white")
                hoja.paste(im, (x, y + 24))
            hoja.save(j.run / ("tira_" + nombre + "_05.png"))
            (j.run / "cola_capturas.json").write_text(json.dumps(datos, indent=2), encoding="utf-8")

        if par.get("fase_cola", "base") == "forma":
            tira("transformacion", n=18)
            j.log(r"formas traza: lado 1 en la forma [1-9]|formas: prueba: sube a la forma [1-9]", 30)
            (j.dest / "dbz3_freecam.req").write_text(pedido.replace("91", "92", 1), encoding="ascii")
            j.log(r"camara libre: prueba mirar 92 ojo", 20)
            tira("dorada")
            j.info("COLA FRI: transformacion y forma dorada capturadas; revision visual obligatoria")
            return
        # Registro nativo que usa PruebaMando (transformaciones.cpp:297-305):
        # 1=P, 2=K, 4=G; el bit fisico es 0x100 << j (A esta en j=4).
        pad = int.from_bytes(g["leer"](j.h, g["GBASE"] + actor + 5028, 2), "big")
        mapa = g["leer"](j.h, g["GBASE"] + mgr + (pad + 4382) * 8, 8)
        fisicos = {0: "1", 1: "3", 4: "Space", 5: "Backspace", 6: "L", 7: "P"}
        claves = {int(mapa[i]): key for i, key in fisicos.items() if mapa[i] in (1, 2, 4)}
        if not {1, 2, 4} <= claves.keys():
            raise g["Fallo"](f"cola_fri_r16: P/K/G no resueltos; pad={pad}, mapa={mapa.hex()}")
        j.info(f"COLA FRI: botones nativos P={claves[1]} K={claves[2]} G={claves[4]}")
        tira("idle")
        tira("guardia", claves[4] + " 4500")
        j.tecla(claves[1], 400)
        for _ in range(4):
            j.tecla(claves[1], 70)
        tira("golpes", claves[1] + " 2000")
        if any(d["forma"] != 0 for d in datos):
            raise g["Fallo"]("cola_fri_r16: transformacion adelantada en las capturas base")
        j.info("COLA FRI: tiras 0.5 de idle/guardia/golpes en forma base; revision visual obligatoria")

    g["ESCENARIOS"]["cola_fri_r16"] = (
        "R16: cola FRI con donante 27, tiras 0.5; --par animada=1 pants=300", lambda p: {}, prueba, prep)

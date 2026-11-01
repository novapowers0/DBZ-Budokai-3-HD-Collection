"""Sonda de la escena parcial de nieve. Ring out SINTETICO: no valida el disparador B2."""
import ctypes
import json
from pathlib import Path
import re
import struct
import time
import shutil

import pruebas as P


def alta_nieve(mods):
    altas = []
    for carpeta in sorted(mods.iterdir(), key=lambda p: p.name):
        if not carpeta.is_dir() or carpeta.name.startswith(".") or carpeta.name.endswith(".disabled") or (carpeta / ".disabled").exists():
            continue
        archivo = carpeta / "stage.toml"
        if archivo.is_file():
            # stage.toml admite claves repetidas (nubes/capas): reproduce solo
            # los campos de alta, como el lector del juego; no es TOML estándar.
            registros, r = [], None
            for linea in archivo.read_text(encoding="utf-8-sig").splitlines():
                linea = linea.split("#", 1)[0].strip()
                if linea.startswith("["):
                    r = {} if linea in ("[[escenario]]", "[escenario]") else None
                    if r is not None:
                        registros.append(r)
                elif r is not None and "=" in linea:
                    clave, valor = (v.strip() for v in linea.split("=", 1))
                    if clave in ("entrada", "tema"):
                        r[clave] = int(valor)
                    elif clave == "selector":
                        r[clave] = valor != "false"
            for r in registros:
                if r.get("entrada", 0) >= 5000 and 0 <= r.get("tema", 16) < 17:
                    altas.append((carpeta.name, r))
    altas = altas[:23]
    altas.sort(key=lambda x: x[1].get("selector") is False or x[0].startswith("Campo IW"))
    return next((17 + i for i, (nombre, _) in enumerate(altas) if nombre == "Montanas nevadas"), None)


def main(copia="smoke_f", nombre="solESC_nieve_paso_guest_sin_muneco"):
    j = P.Juego(copia, nombre)
    datos = dict(completa=False, disparador="aviso de ring out sintetico; NO prueba del original", muestras=[])
    marcadores = []
    ajustes = None
    try:
        P.respaldar(j.dest)
        P.preparar(j.dest)
        ajustes = (j.dest / "dbz3_user.toml").read_bytes()
        carpeta = j.dest / "mods/Montanas nevadas"
        for nombre in (".disabled",):
            marcador = carpeta / nombre
            if marcador.exists():
                guardado = j.run / ("marker_" + nombre)
                shutil.move(str(marcador), guardado)
                marcadores.append((marcador, guardado))
        stage = alta_nieve(j.dest / "mods")
        if stage is None:
            raise RuntimeError("No aparece el alta de nieve entre las 23 admitidas")
        config = (j.dest / "dbz3_user.toml").read_text(encoding="utf-8")
        config = re.sub(r"(?m)^dbz3_escenarios_zona_(?:paso_guest|escena)\s*=.*\n?", "", config)
        # Una copia reutilizada traía el muñeco de laboratorio en 'ataca':
        # escribe entradas por su cuenta incluso durante la escena.
        config = re.sub(r"(?m)^dbz3_combate_prueba_muneco\s*=.*\n?", "", config)
        (j.dest / "dbz3_user.toml").write_text(config +
            '\ndbz3_escenarios_zona_paso_guest = true\ndbz3_escenarios_zona_escena = true\n'
            'dbz3_combate_prueba_muneco = ""\n', encoding="utf-8")
        j.lanzar(dict(DBZ3_TEST_BATTLE=f"0,7,{stage}", DBZ3_TEST_ESCENA_VOLCADO="1"))
        j.arrancar(120, rapido=True)
        # Ese texto solo existe con DBZ3_TEST_RINGOUT, que aqui debe estar
        # ausente para no saltarse la transicion. Medir el estado en memoria.
        P._esperar_pelea(j, 90)
        if not re.search(rf"alta {stage} = .*\(mod Montanas nevadas, entrada 5007\)", j.texto_log()):
            raise RuntimeError("La numeracion de altas del exe no coincide con la sonda")

        def leer(a, n):
            b = P.leer(j.h, P.GBASE + a, n)
            if b is None or len(b) != n:
                raise RuntimeError(f"Sin memoria {a:X}")
            return b

        def u32(a):
            return struct.unpack(">I", leer(a, 4))[0]

        mgr = u32(0x823D6CF4)

        def muestra(fase):
            fila = dict(fase=fase, paso_guest=u32(0x8238D6B4), zona=u32(0x823A3DC0),
                        estado=u32(mgr + 35128), luchadores=[])
            for k in range(2):
                x = mgr + 2096 + 8240 * k
                fila["luchadores"].append(dict(pos=struct.unpack(">3f", leer(x + 64, 12)),
                    dibujo=struct.unpack(">3f", leer(x + 48, 12)),
                    vida=struct.unpack(">f", leer(x + 6080, 4))[0],
                    ki=struct.unpack(">f", leer(x + 6100, 4))[0]))
            datos["muestras"].append(fila)
            foto = j.captura(fase)
            if P.Image:
                with P.Image.open(foto) as im:
                    im.resize((im.width // 2, im.height // 2)).save(j.run / (fase + "_05.png"))
            print(fila, flush=True)

        muestra("antes")
        h = P.K32.OpenProcess(0x0438, False, j.proc.pid)
        try:
            n = ctypes.c_size_t()
            if not P.K32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(P.GBASE + P.DESPL + mgr + 35569),
                                           b"\x01", 1, ctypes.byref(n)):
                raise RuntimeError("No se pudo poner el aviso sintetico")
        finally:
            P.K32.CloseHandle(ctypes.c_void_p(h))
        j.log("escena de zona: empieza", 15)
        muestra("inicio")
        j.log("cambio de zona del motor en el cuadro", 30)
        muestra("cambio")
        j.log("escena de zona terminada", 20)
        muestra("despues")
        datos["completa"] = datos["muestras"][-1]["zona"] == 1 and datos["muestras"][-1]["estado"] == 4
        if not datos["completa"]:
            raise RuntimeError("No se conserva zona B y pelea activa")
        print("PASS de camara parcial/continuidad; NO de transicion completa", j.run, flush=True)
    finally:
        (j.run / "medida.json").write_text(json.dumps(datos, indent=2), encoding="utf-8")
        (j.run / "dbz3.log").write_text(j.texto_log(), encoding="utf-8")
        j.cerrar()
        if not j.vivo():
            for marcador, guardado in marcadores:
                shutil.move(str(guardado), marcador)
            if ajustes is not None:
                shutil.move(str(j.dest / "dbz3_user.toml"), j.run / "ajustes_usados.toml")
                (j.dest / "dbz3_user.toml").write_bytes(ajustes)
        print("evidencias", j.run, flush=True)
    return j.run


if __name__ == "__main__":
    main()

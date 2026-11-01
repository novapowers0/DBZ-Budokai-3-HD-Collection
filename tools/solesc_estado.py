"""Vínculo medido entre trayectorias de luchadores y recursos de escena B2 en estados PINE."""
import argparse
import json
from pathlib import Path
import struct
import zipfile


def medir(archivo):
    with zipfile.ZipFile(archivo) as z:
        ram = z.read("eeMemory.bin")
    def u(a):
        return struct.unpack_from("<I", ram, a)[0]
    def magia(a):
        return ram[a:a + 4].decode("ascii", "replace")
    mgr, mdd = u(0x3A44B4), u(0x37348C)
    objetos = []
    for i in range(u(mdd + 4)):
        ob = u(u(mdd + 0x20) + 4 * i)
        camaras = []
        for j in range(u(ob + 0xD4)):
            cam = u(u(ob + 0xD8) + 4 * j)
            tab = u(cam + 0x14)
            camaras.append(dict(bloque=j, direccion=hex(cam), magic=magia(cam),
                                duraciones=[u(tab + 16 * k + 8) for k in range(u(cam + 0x10))]))
        objetos.append(dict(objeto=i, camaras=camaras))
    jugadores = []
    for i in range(2):
        x = u(mgr + 0x19C54 + 4 * i)
        if not 0x100000 <= x < len(ram) - 0x9F50:
            continue
        trayectoria = u(x + 0x9C34)
        jugadores.append(dict(lado_guion=i, x=hex(x), trayectoria=hex(trayectoria),
                              modo_clip=struct.unpack_from("<2h", ram, x + 0x9C2C),
                              hueso=struct.unpack_from("<b", ram, x + 0x9BC4)[0],
                              tiempo=struct.unpack_from("<f", ram, x + 0x9C30)[0],
                              pos=struct.unpack_from("<3f", ram, x + 0x40)))
    return dict(estado=str(archivo), campo=u(0x371550), zona=u(0x3734AC),
                mdd=hex(mdd), objetos=objetos, jugadores=jugadores)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("carpeta", type=Path)
    p.add_argument("--salida", type=Path, required=True)
    a = p.parse_args()
    datos = [medir(f) for f in sorted(a.carpeta.glob("*.p2s"))]
    a.salida.write_text(json.dumps(datos, indent=2), encoding="utf-8")
    for d in datos:
        print("campo", d["campo"], "zona", d["zona"], "MDD", d["mdd"])
        for ob in d["objetos"]:
            print("objeto", ob["objeto"], "bloques", ob["camaras"])
        for j in d["jugadores"]:
            print("luchador", j)


if __name__ == "__main__":
    main()

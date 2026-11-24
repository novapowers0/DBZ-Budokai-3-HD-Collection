# -*- coding: utf-8 -*-
r"""b3hd_conv.py - puerta unica del kit de conversion a B3 HD (diseno: studies 5\ENTREGAS\KIT_CONVERSION_B3HD.md).
  python tools\b3hd_conv\b3hd_conv.py comparar <original> <port> [--pares auto|orden|t00=sh_054,...] [--salida DIR]
  python tools\b3hd_conv\b3hd_conv.py anim --esq-origen b2:327/0 --anim-origen b2:328/3 --esq-destino hd:534 ... [--render]
  python tools\b3hd_conv\b3hd_conv.py autoprueba        (las de todas las piezas hechas)
  python tools\b3hd_conv\b3hd_conv.py                   (lista de comandos; los que faltan dicen que herramienta usar hoy)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# ponytail: los comandos sin hacer solo apuntan a la herramienta de hoy; se construyen cuando toque (orden en el diseno)
POR_HACER = {
    "logica": "hoy: tools/iw_mips/ee.py (oraculo) + src/iw_mips.cpp (Ambito/Puente); B2: tools/b2_oraculo (S2, Sol)",
    "modelo": "hoy: awo_tools/ps2hd.py (datos) + awo_tools/b3_gateway.py --ps2 (modelos) + mod center hd/importar.py",
    "escena": "hoy: a mano en src/pasarela_*.cpp (#AMC/#AML/#AMP de la ISO) + mod center hd/escenarios_convertir.py",
}


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        for k, v in POR_HACER.items():
            print("  %-9s (por hacer) %s" % (k, v))
        return 0
    cmd, resto = argv[0], argv[1:]
    if cmd == "comparar":
        import lamina  # noqa: PLC0415
        return lamina.main(resto)
    if cmd == "anim":
        import retarget  # noqa: PLC0415
        return retarget.main(resto)
    if cmd == "autoprueba":
        import lamina  # noqa: PLC0415
        import retarget  # noqa: PLC0415
        return retarget.autoprueba() or lamina.autoprueba()
    if cmd in POR_HACER:
        print("%s: aun no esta en el kit; %s" % (cmd, POR_HACER[cmd]))
        return 2
    print("comando desconocido: %s" % cmd)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

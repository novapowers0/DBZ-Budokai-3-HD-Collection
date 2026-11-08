"""eANI3 (6/11): pies de los personajes importados frente al ajuste de pies de B3 (sub_8211B1F0).

RE (generated/dbz3_recomp.25.cpp, sub_8211B1F0): por cada pie, con el hueso [X+2956]+24/+26 (los NULOS XNNN_NLF/NRF,
no el tobillo) y el suelo g bajo el punto de cuerpo 12/13: T = [[X+2996]+8] + g - X.y + X+6664.
  - T > pie + 0,1                         -> IK sube el pie a T (dobla la pierna).
  - X+8185 == 0, peso X+6644/6648 > 0 y T < pie - 0,1 -> IK baja el pie a pie - (pie - T) * peso.
  ([X+2996]+8 = "r8": Goku y Freezer 1,0; Krillin 0,81. Peso: sube/baja a 0,35 por cuadro con los bits 0-4 de la
  accion, sub_820FB0D0.) Nativos: en su reposo el NLF queda a r8 -0,05 (banda de 0,1).
Medido sin IK (DBZ3_TEST_SIN_IK_PIES=1, tactics_sonda): el NLF de los importados en su reposo REAL queda por debajo
de r8: Goku B1 0x40000 -0,97, Freezer B2 0x10000 -1,25, Krillin SB 0x40001 -1,87; tambien en los clips comunes
(0x10046: -1,00 / -1,25). Dos causas: (1) en los modelos de B1/B2 el NLF va a la altura de la planta (y local 0,83 en
Goku B1; el nativo lo tiene a 0,22, un 20 % del pie) y (2) el cuerpo entero queda bajo.

Arreglo (modelo PS2 del mod, el que convierte roster_build):
  nulos(): NLF/NRF a la convencion del donante (misma fraccion del vector FOOT1 -> FOOT2).
  luego altura.lift_waist(modelo, dy) con el resto medido; HD (SB con modelo de B3): altura.scale_hips(anm, 1, dy).
uso: python tools/eani3_pies.py nulos <modelo_mod> <modelo_donante> [salida]
     python tools/eani3_pies.py subir <modelo_mod> <dy> [salida]       (PS2: cintura del bind)
     python tools/eani3_pies.py subir_anm <anm_mod> <dy> [salida]      (HD/PS2: pistas de la cadera del moveset)
     python tools/eani3_pies.py subir_clip <anm_mod> <codigo> <dy> [salida]   (solo la cadera de ese codigo)
     python tools/eani3_pies.py --prueba"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "awo_tools"))
import altura  # noqa: E402


from pies import _amg, _ejes, nulos  # noqa: E402,F401  (eANI4: la logica vive en awo_tools/pies.py)


def subir_clip(anm, code, dy):
    """ANM con la pista de posicion de la cadera de la animacion del codigo `code` subida dy (solo si esa pista no la
    comparte otra animacion). -> (anm, n claves tocadas)."""
    a = altura.code_anim(anm, code)
    if a is None:
        return anm, 0
    e, kids = altura.amb_kids(anm)
    o = kids[1][0]
    out = bytearray(anm)
    n, t, nb, no = struct.unpack_from(e + "4I", out, o + 0x10)
    w = [j for j in range(nb) if altura.suffix(bytes(out[o + no + 32 * j:o + no + 32 * j + 32]).split(b"\0")[0]
                                               .decode("latin1")) == "WAIST"]
    usos = {}
    for k in range(n):
        flags, var, nf, off = struct.unpack_from(e + "4I", out, o + t + 16 * k)
        per = 3 if flags & 0x10 else 2
        for j in w if off else ():
            pp = struct.unpack_from(e + "I", out, o + off + 4 * (per * j + 1))[0]
            if pp:
                usos.setdefault(pp, set()).add(k)
    flags, var, nf, off = struct.unpack_from(e + "4I", out, o + t + 16 * a)
    per = 3 if flags & 0x10 else 2
    tocadas = 0
    for j in w if off else ():
        pp = struct.unpack_from(e + "I", out, o + off + 4 * (per * j + 1))[0]
        if not pp or usos[pp] != {a}:
            continue
        for q in range(struct.unpack_from(e + "I", out, o + pp + 8)[0]):
            at = o + pp + 12 + 16 * q + 8
            struct.pack_into(e + "f", out, at, struct.unpack_from(e + "f", out, at)[0] + dy)
            tocadas += 1
    return bytes(out), tocadas


def prueba():
    """Esqueleto de 3 huesos sintetico: el nulo pasa a la fraccion del donante."""
    def amo(nulo_y, toe_y):
        nb, ax, na = 3, 0x20, 0x20 + 80 * 3
        g = bytearray(na + 32 * nb)
        g[:4] = b"#AMG"
        struct.pack_into("<I", g, 0x10, nb)
        struct.pack_into("<I", g, 0x1C, na)
        for i, (nm, p, par) in enumerate((("X_LFOOT1", (0, 0, 0), 0), ("X_LFOOT2", (1.0, toe_y, 0), 1),
                                          ("XX_NLF", (0.3, nulo_y, 0), 1))):
            o = ax + 80 * i
            struct.pack_into("<7f", g, o, 0, 0, 0, 1, *p)
            struct.pack_into("<I", g, o + 0x40, ax + 80 * (par - 1) if par else 0)
            g[na + 32 * i:na + 32 * i + len(nm)] = nm.encode()
        return b"#AMO" + bytes(12) + bytes(g)
    don, mod = amo(0.2, 1.0), amo(0.8, 0.8)
    _, h = nulos(mod, don)
    assert h["NLF"][1] == (0.3, 0.16, 0.0), h
    print("prueba OK", h)


def main(a):
    if a[:1] == ["--prueba"]:
        return prueba()
    op, src = a[0], Path(a[1])
    b = src.read_bytes()
    if op == "nulos":
        nb, h = nulos(b, Path(a[2]).read_bytes())
        dst = Path(a[3]) if len(a) > 3 else src
        print(src.name, h)
    elif op == "subir":
        nb = altura.lift_waist(b, float(a[2]))
        if nb is None:
            raise SystemExit("no es un modelo PS2 con WAIST")
        dst = Path(a[3]) if len(a) > 3 else src
    elif op == "subir_clip":
        nb, k = subir_clip(b, int(a[2], 0), float(a[3]))
        print("claves tocadas", k)
        dst = Path(a[4]) if len(a) > 4 else src
    elif op == "subir_anm":
        nb = altura.scale_hips(b, 1.0, float(a[2]))
        dst = Path(a[3]) if len(a) > 3 else src
    else:
        raise SystemExit(__doc__)
    dst.write_bytes(nb)
    print("escrito", dst)


if __name__ == "__main__":
    main(sys.argv[1:])

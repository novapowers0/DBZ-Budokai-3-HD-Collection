#!/usr/bin/env python3
"""reloj_equivalencia.py - prueba de equivalencia del reloj antes de cambiar los 1,2 escritos a mano (Fase 0 O, 6/11).

Regla (studies 5/DECISIONES_DIA1.md 1a): el original pone fRam0049c7dc = 1,2 si el modo de video es PAL
(cRam0049c814 == 3) y 1,0 si no (iw_sles55347.c:51281-51286). El port avanza a 60 pasos/s, asi que:
  Delta()            = 1,2f PAL (ELF EU SLES_553.47 / B2 SLES_518.39) | 1,0f NTSC (ELF USA SLUS_218.42)
  CamposAPasos(int)  = n*60/Hz entero (EU = n*6/5 exacto)       CamposAPasos(float) = n*Delta() en float
Lo que el original multiplica por Delta en cada campo ya va bien a 60 pasos/s con factor 1 en las dos regiones:
esos sitios NO se tocan (meterles Delta repite el error x1,2).

  python tools/reloj_equivalencia.py     # sale 0 si la rama EU de cada sitio da lo mismo bit a bit
"""
import math
import struct
import sys


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


HZ = {"eu": 50, "usa": 60}
DELTA = {r: f32(60.0 / hz) for r, hz in HZ.items()}     # 1,2f y 1,0f


def lround(x):          # C lround: mitad lejos del cero (round() de Python es al par)
    return int(math.floor(abs(x) + 0.5)) * (1 if x >= 0 else -1)


def pasos_int(n, r):        # original::CamposAPasos(int)
    return n * 60 // HZ[r]


def pasos_f(x, r):          # original::CamposAPasos(float): misma operacion que el x*1.2f de hoy
    return f32(f32(x) * DELTA[r])


def main():
    assert DELTA["eu"] == f32(1.2) and DELTA["usa"] == 1.0
    malos = []
    # iw_mini_botones.cpp:483  hueco = lround(hueco*1.2)  (double)
    malos += [("botones:483", h) for h in range(0, 5000) if lround(h * 1.2) != lround(pasos_f(h, "eu"))]
    # iw_mini_botones.cpp:3019  elf.eu ? hueco*6/5 : hueco/10  (la rama USA es formato de la tabla, no reloj)
    malos += [("botones:3019", h) for h in range(0, 100000) if h * 6 // 5 != pasos_int(h, "eu")]
    # iw_mini_botones.cpp:456 campo_original, iw_nave_hud_b2.h:29 paso_hud_original, Bubbles (iw_minijuegos.cpp
    # 2134/2135/2191/2192/2197/3228/3242/3248, iw_bubbles_gp2.h:36, iw_bubbles_audio.h:30): la constante 1,2f
    if DELTA["eu"] != f32(1.2):
        malos.append(("constante 1,2f", 0))
    # iw_mini_disparo.cpp:1062  b48/126.0f  ->  b48/float(CamposAPasos(105))
    for b in (0.5, 1.0, 3.7, 48.0, 105.0, 1234.5):
        if f32(f32(b) / 126.0) != f32(f32(b) / float(pasos_int(105, "eu"))):
            malos.append(("disparo:1062", b))
    # pasarela_nave.cpp:3111/3125/3127  (campo - x)*1.2f y ceil(fin/1.2f)  (B2 PAL)
    for c in [i * 0.25 for i in range(0, 4000)]:
        if f32(f32(c) * f32(1.2)) != pasos_f(c, "eu") or math.ceil(f32(f32(c) / f32(1.2))) != math.ceil(f32(f32(c) / DELTA["eu"])):
            malos.append(("nave:3111", c))
            break
    # Sitios neutros: ritmo por segundo del original (por campo * Delta * Hz) = el del port (por paso * 60).
    neutros = {"pasarela_iw.cpp:2905 t_intro 0,01": (0.01, 0.01), "pasarela_nave.cpp:1546 26 px": (26.0, 26.0),
               "pasarela_nave.cpp:656 UV 0,0005": (0.0005, 0.0005)}
    for k, (por_campo, por_paso) in neutros.items():
        for r in HZ:
            if not math.isclose(por_campo * (60.0 / HZ[r]) * HZ[r], por_paso * 60):
                malos.append((k + " " + r, 0))
    # dia1O: los 5 sitios del reloj ya pasan por src/modo_original.h (constexpr con static_assert de estos valores)
    from pathlib import Path
    src = Path(__file__).resolve().parent.parent / "src"
    for f, txt in (("iw_mini_botones.cpp", "original::Delta(elf.fuente())"),
                   ("iw_mini_botones.cpp", "original::CamposAPasos(float(j.c[l].hueco[i]), elf.fuente())"),
                   ("iw_mini_botones.cpp", "original::CamposAPasos(j.hueco[r][i], elf.fuente())"),
                   ("iw_mini_disparo.cpp", "b48 / float(::dbz3::original::CamposAPasos(105, ::dbz3::original::FuenteIw()))"),
                   ("iw_nave_hud_b2.h", "Delta(::dbz3::original::Fuente::B2Pal)")):
        if (src / f).is_file() and txt not in (src / f).read_text(encoding="utf-8", errors="replace"):
            malos.append(("sitio sin modo_original: " + f, txt))
    # Lo que cambia con el ELF USA (esperado; sin datos USA para probarlo en juego):
    print("USA: botones:483 hueco tal cual; disparo:1062 b48/%d; Bubbles y nave_hud Delta %.1f; 1 campo = %d paso"
          % (pasos_int(105, "usa"), DELTA["usa"], pasos_int(1, "usa")))
    for m in malos[:20]:
        print("DISTINTO", m)
    print("equivalencia EU: %s (%d fallos)" % ("OK" if not malos else "MAL", len(malos)))
    return 1 if malos else 0


if __name__ == "__main__":
    sys.exit(main())

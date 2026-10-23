r"""extraer_datos.py (X0c, studies 5) - saca de la ingenieria inversa (Ghidra + RAM de savestate de PCSX2) los datos que el
port necesita y los deja en res\datos\*.toml, para que el juego los lea (y recargue con F5 en modo pruebas, X0d).

  python tools\extraer_datos.py todo                  los cuatro
  python tools\extraer_datos.py b2_ordenes            tabla de ordenes del tablero de B2 (ELF 0x379EC0, 86 entradas)
  python tools\extraer_datos.py b2_sonidos            ordenes 74-79: que sonido piden (FUN_00147260(tipo,0,id,0))
  python tools\extraer_datos.py iw_minijuegos         tabla de minijuegos de IW (ELF 0x473310) + #AMC/entrada de la familia de botones
  python tools\extraer_datos.py satan_tiempos         tiempos de las tomas de Satan (B1) medidos en PCSX2

Fuentes (solo lectura): E:\DBZ3HD_trabajo\ghidra\salida\{b1,b2,iw_sles55347}.c (codigo) y la RAM de un savestate de
E:\DBZ3HD_trabajo\R7\emu\pcsx2\sstates (tablas de datos del ELF: el .c de Ghidra no las trae). Nada del juego se copia:
solo numeros medidos (direcciones, indices, tipos de sonido, tiempos). Lo que no se pudo sacar queda como [H] en el
propio toml y en studies 5\ENTREGAS\X0\PENDIENTE.md.
"""
import re
import struct
import sys
import zipfile
from pathlib import Path

R = Path(__file__).resolve().parent.parent
SALIDA = R / "res" / "datos"
GHIDRA = Path(r"E:\DBZ3HD_trabajo\ghidra\salida")
SSTATES = Path(r"E:\DBZ3HD_trabajo\R7\emu\pcsx2\sstates")
RAM = {"b2": SSTATES / "SLES-51839 (FE09A7CC).03.p2s", "iw": SSTATES / "SLES-55347 (335A5A1F).04.p2s"}
INV_IW = R / "studies 5" / "referencias" / "INV_IW_MINIJUEGOS.md"
CAB = "# GENERADO por tools\\extraer_datos.py {} -- no editar a mano (en modo pruebas se recarga con F5).\n"


def ram(juego):
    with zipfile.ZipFile(RAM[juego]) as z:          # savestate = zip con zstd; Python 3.14 lo lee
        return z.read("eeMemory.bin")


def bloques_c(juego_c):
    """{direccion 8 hex: texto de la funcion} del C de Ghidra ('// ==== 00293ce0 nombre')."""
    t = (GHIDRA / juego_c).read_text(encoding="utf-8", errors="replace")
    h = [(m.start(), m.group(1)) for m in re.finditer(r"^// ==== ([0-9a-f]{8}) ", t, re.M)]
    return {a: t[s:(h[i + 1][0] if i + 1 < len(h) else len(t))] for i, (s, a) in enumerate(h)}


def u32(mem, a):
    return struct.unpack_from("<I", mem, a)[0]


# ------------------------------------------------------------------ b2: ordenes del tablero
TABLA_B2 = 0x379EC0     # u32[0..86]: puntero a la funcion de cada orden (indice = numero de orden; 0 = sin orden)


def b2_ordenes():
    mem, c = ram("b2"), bloques_c("b2.c")
    ordenes = {}
    for k in range(1, 87):
        f = u32(mem, TABLA_B2 + 4 * k)
        if f:
            ordenes[k] = f
    out = [CAB.format("b2_ordenes"),
           f'fuente = "ELF SLES_518.39 (B2 EU), tabla {TABLA_B2:#x} en RAM de savestate; codigo: ghidra\\\\salida\\\\b2.c"',
           f"entradas = {len(ordenes)}\n"]
    for k, f in ordenes.items():
        cuerpo = c.get(f"{f:08x}", "")
        llama = list(dict.fromkeys(re.findall(r"FUN_([0-9a-f]{8})\(", cuerpo.split("{", 1)[-1])))
        out += [f"[orden.{k}]", f'funcion = "0x{f:08X}"', f"lineas = {cuerpo.count(chr(10))}",
                "llama = [" + ", ".join(f'"0x{x.upper()}"' for x in llama) + "]",
                f"sonido = {'true' if '00147260' in llama else 'false'}\n"]
    return "b2_ordenes.toml", "\n".join(out), len(ordenes)


# ------------------------------------------------------------------ b2: sonidos por evento
_SON = re.compile(r"FUN_00147260\((\d+),0,\*\(undefined4 \*\)\(\(?(?:int\))?\s*(?:param_1|iVar1) \+ (0x[0-9a-f]+)\),0\)")


def b2_sonidos():
    mem, c = ram("b2"), bloques_c("b2.c")
    out = [CAB.format("b2_sonidos"),
           'fuente = "b2.c: ordenes 74-79 llaman FUN_00147260(tipo, 0, id, 0); el id viene del guion (arg +0x34)"',
           "# repite = true: la orden vuelve a sonar cada +0x38 cuadros (contador en +0x30)\n"]
    n = 0
    for k in range(74, 80):
        f = u32(mem, TABLA_B2 + 4 * k)
        cuerpo = c.get(f"{f:08x}", "")
        m = _SON.search(cuerpo)
        if not m:
            out += [f"[sonido.{k}]", f'funcion = "0x{f:08X}"', "# [H] no se reconoce la llamada\n"]
            continue
        n += 1
        out += [f"[sonido.{k}]", f'funcion = "0x{f:08X}"', f"tipo = {m.group(1)}", f'id_en = "arg+{m.group(2)}"',
                f"repite = {'true' if '+ 0x30' in cuerpo else 'false'}\n"]
    return "b2_sonidos.toml", "\n".join(out), n


# ------------------------------------------------------------------ iw: minijuegos
TABLA_IW = 0x473310     # u32[0..18]: descriptor de cada minijuego (forzar_mini_iw.py lo documenta)
_FILA = re.compile(r"^\| `(0x7[0-9A-F]{2})` \| (\d+) \| (\S+) \|")


def iw_minijuegos():
    mem = ram("iw")
    inv = {}
    for l in INV_IW.read_text(encoding="utf-8").splitlines():
        m = _FILA.match(l)
        if not m:
            continue
        col = [x.strip() for x in l.strip().strip("|").split("|")]
        cam = col[7] if len(col) > 7 else ""
        amc915 = re.search(r"#AMC 915`: ([^;]*)", cam)
        clips = re.findall(r"0x[0-9A-Fa-f]+", amc915.group(1)) if amc915 else []
        entrada = re.search(r"entrada `?(?:#AMC )?(\d+)`?", cam)
        inv[int(m.group(2))] = (m.group(1), m.group(3), clips, entrada.group(1) if entrada else "")
    out = [CAB.format("iw_minijuegos"),
           f'fuente = "ELF SLES-55347 (IW EU), tabla {TABLA_IW:#x} en RAM de savestate; clips #AMC y entrada: '
           f'studies 5/referencias/INV_IW_MINIJUEGOS.md 2.3 (medido en studies 8)"',
           "# fov: [H] no esta en una tabla estatica (se mide en marcha: C+0xB4 de la camara); ver PENDIENTE.md\n"]
    for i in range(19):
        d = u32(mem, TABLA_IW + 4 * i)
        out += [f"[minijuego.{i}]", f'descriptor = "0x{d:08X}"']
        if i in inv:
            clave, mod, clips, ent = inv[i]
            out += [f'clave = "{clave}"', f'modulo = "{mod}"', "amc_915_clips = [" + ", ".join(f'"{x}"' for x in clips) + "]"]
            if ent:
                out.append(f"amc_entrada = {ent}")
        out.append("")
    return "iw_minijuegos.toml", "\n".join(out), len(inv)


# ------------------------------------------------------------------ satan: tiempos (B1)
def satan_tiempos():
    # Medido en PCSX2 (studies 1 F03 y s1; AUDITORIA_MODOS 4.5): NO sale de Ghidra todavia. El consumidor de la camara de
    # B1 (struct 0x555B00) es FUN_0015f870 en b1.c: de ahi hay que seguir las rondas 2-11 [H].
    out = [CAB.format("satan_tiempos"),
           'fuente = "PCSX2 + PINE (studies 1 F03, AUDITORIA_MODOS 4.5); Ghidra [H]: b1.c FUN_0015f870 usa la camara 0x555B00"',
           "# segundos de cada toma de la previa (ronda 1)",
           "[previa]", "ring_reportero = 9.98", "cell = 5.80", "goku_abajo = 3.80", "hombro_espera = 14.42",
           "hombro = 10.62", "espera = 3.80", "",
           "# combate rondas 2-11, segundos tras Start (aproximado, [H] hasta medir con cuadros)",
           "[combate]", "satan = 5.0", "otro_plano = 6.0", "rival = 8.0", "ready = 10.0", "",
           "# subtitulos de la ronda 1: [H] no localizados (imagenes del guion de B1 en DATA_<idioma>.AFS)",
           "[subtitulos]", "estado = \"H\"", ""]
    return "satan_tiempos.toml", "\n".join(out), 4


EXTRACTORES = {"b2_ordenes": b2_ordenes, "b2_sonidos": b2_sonidos, "iw_minijuegos": iw_minijuegos,
               "satan_tiempos": satan_tiempos}


def main(a):
    if not a or a[0] not in list(EXTRACTORES) + ["todo"]:
        print(__doc__)
        return 2
    SALIDA.mkdir(parents=True, exist_ok=True)
    for nombre in (EXTRACTORES if a[0] == "todo" else [a[0]]):
        fich, texto, n = EXTRACTORES[nombre]()
        (SALIDA / fich).write_text(texto + "\n", encoding="utf-8", newline="\n")
        print(f"{fich}: {n} entradas -> {SALIDA / fich}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

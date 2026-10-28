#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""t3_audio.py - Audio de Budokai Tenkaichi 3 (PS2) como mod LOCAL y OPCIONAL de B3 HD.

Lee la ISO de Tenkaichi 3 DEL JUGADOR (nunca se extrae ni se reparte nada al proyecto) y
prepara un mod normal de mods/ que sustituye pistas del Budokai 3 HD por musica de T3.
El mod nace APAGADO (marcador .disabled) y solo se usa si el jugador lo marca en el launcher.

Formato medido (studies 5/ENTREGAS/E2/E2_T3.md):
  - T3: PZS3EU2.AFS 64974-64993 (zs3_00_US.adx .. zs3_19_US.adx), ADX v4 estereo 24 kHz,
    con bucle en la cabecera. B3 HD acepta ese ADX tal cual (mismo decodificador CRI): no
    hace falta remuestrear; el tono y el bucle se midieron con un ADX sintetico.
  - B3 us/adx_usa.afs: la musica es estereo (122-146, 4187-4215, 4267-4276...). Las entradas
    4187-4215 y 4267-4276 son las que suenan en menu y combate [V]. La 4276 se lee en bucle
    en el menu (log de E/S del jugador, studies/SESION_IO_FOCO_2026-09-19.md).
  - El override debe tener al menos el tamano de la entrada original (se rellena con ceros):
    el guest lee con el tamano de la tabla del AFS. Si la pista de T3 es mayor, la entrada
    crece con la tabla virtual del runtime (mid-insert) y se avisa.

Voces: T3 tambien es viable como FUENTE para personajes importados (voces = "t3:BABY_V" o
gritos = "t3:BABY_V" en personaje.toml, ya soportado por voces.py/gritos.py). No se genera
un mod de voces para personajes nativos porque B3 saca los gritos de golpe de un banco XMA
por personaje (gritos.py) y repartir clips por duracion cambiaria frases de sitio; el mapa
clip -> situacion de T3 no esta medido. El comando `voces` extrae los clips a WAV para
escucharlos y comprobar el banco.

Uso (desde la carpeta del proyecto):
  python "mod center hd\t3_audio.py" info [--iso ISO]
  python "mod center hd\t3_audio.py" pistas [--iso ISO] [--pista N] [--wav DIR]
  python "mod center hd\t3_audio.py" b3 [--afs RUTA]
  python "mod center hd\t3_audio.py" mod [--iso ISO] [--mods DIR] [--mod NOMBRE]
        [--pista N[,N...]] [--entradas E[,E...]] [--permitir-crecer] [--activar|--desactivar]
        [--forzar]
  python "mod center hd\t3_audio.py" voces --personaje CLAVE [--iso ISO] [--idioma usa|jpn]
        [--juego 1|2] [--salida DIR] [--max N]
  python "mod center hd\t3_audio.py" prueba [--iso ISO]

Busqueda de la ISO: --iso, DBZ3_T3_ISO, ps2_games del proyecto/copia (DBZ3_PS2_GAMES),
D:\DBZ3HD\ps2_games, E:\Emuladores y E:\DBZ3HD_trabajo (siempre en SOLO LECTURA).
"""
import argparse
import glob
import os
import struct
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import iso as isomod  # noqa: E402
import t3 as t3mod  # noqa: E402
import voces  # noqa: E402

T3_MUSICA = (64974, 20)          # zs3_00_US.adx .. zs3_19_US.adx
RANGOS_B3 = {
    (4187, 4215): "menu/HUB (medido: suena)",
    (4267, 4276): "menu/combate (medido: suena)",
    (134, 134): "leida en un combate nativo (log de E/S 2026-10-25)",
    (122, 146): "tema (E2: no cambio el volcado de menu/combate)",
}
ETIQUETA = "t3_audio.txt"


def _be32(d, o):
    return struct.unpack_from(">I", d, o)[0] if o + 4 <= len(d) else 0


def adx_info(d):
    """Cabecera ADX: canal, frecuencia, muestras y segundos (None si no es ADX)."""
    if len(d) < 0x24 or d[0] != 0x80:
        return None
    hz, muestras = _be32(d, 8), _be32(d, 12)
    return {"enc": d[4], "bloque": d[5], "bits": d[6], "ch": d[7], "hz": hz, "muestras": muestras,
            "seg": (muestras / float(hz)) if hz else 0.0, "ver": d[0x12]}


def adx_bucle(d, inf, tam=None):
    """Bucle de la cabecera (muestras inicial..final) o None. v3: 0x14; v4: 0x28 (E2)."""
    if not inf:
        return None
    if inf["ver"] == 3:
        a, ab, b, bb = _be32(d, 0x14), _be32(d, 0x18), _be32(d, 0x1C), _be32(d, 0x20)
    else:
        a, ab, b, bb = _be32(d, 0x28), _be32(d, 0x2C), _be32(d, 0x30), _be32(d, 0x34)
    if ab <= bb <= (tam or len(d)) and a < b <= inf["muestras"] and b - a > inf["hz"]:
        return a, b
    return None


def adx_pcm(d):
    """ADX -> (lista de canales int16, Hz). Copia de gritos.adx_decode conservando el estereo."""
    inf = adx_info(d)
    if not inf:
        raise ValueError("no es ADX")
    off = struct.unpack_from(">H", d, 2)[0] + 4
    bs, ch, hz, total = inf["bloque"], inf["ch"], inf["hz"], inf["muestras"]
    cut = struct.unpack_from(">H", d, 16)[0]
    z = np.cos(2 * np.pi * cut / hz)
    a = np.sqrt(2) - z
    bb = np.sqrt(2) - 1
    c = (a - np.sqrt((a + bb) * (a - bb))) / bb
    c1, c2 = int(np.floor(c * 8192)), int(np.floor(c * c * -4096))
    spb = (bs - 2) * 2
    out = [[] for _ in range(ch)]
    hist = [[0, 0] for _ in range(ch)]
    o, n = off, 0
    while n < total and o + bs * ch <= len(d):
        for k in range(ch):
            fr = d[o + k * bs:o + (k + 1) * bs]
            scale = struct.unpack(">H", fr[:2])[0] + 1
            h1, h2 = hist[k]
            for i in range(spb):
                v = fr[2 + i // 2]
                nib = (v >> 4) if i % 2 == 0 else (v & 15)
                if nib >= 8:
                    nib -= 16
                s = nib * scale + ((c1 * h1 + c2 * h2) >> 12)
                s = max(-32768, min(32767, s))
                out[k].append(s)
                h2, h1 = h1, s
            hist[k] = [h1, h2]
        o += bs * ch
        n += spb
    return [np.array(x[:total], np.int16) for x in out], hz


def escribir_wav(path, pcm, hz):
    if not pcm:
        raise ValueError("sin audio")
    ch = len(pcm)
    datos = np.stack(pcm, axis=1) if ch > 1 else pcm[0].reshape(-1, 1)
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(hz)
        w.writeframes(datos.astype("<i2").tobytes())


def rms(pcm):
    return float(np.sqrt(np.mean(np.concatenate([x.astype(np.float64) for x in pcm]) ** 2))) if pcm else 0.0


# ------------------------------------------------------------------ ISO de T3
def buscar_iso(explicita=None):
    if explicita:
        return explicita if os.path.isfile(explicita) else None
    env = os.environ.get("DBZ3_T3_ISO")
    if env and os.path.isfile(env):
        return env
    raices = [isomod.ps2_root(), r"D:\DBZ3HD\ps2_games", r"E:\Emuladores", r"E:\DBZ3HD_trabajo"]
    patrones = ["*.iso", "*.ISO", "*.ruta", "*/*.ruta", "*/*.iso", "*/*.ISO",
                "*/roms/ps2/*.iso", "*/roms/ps2/*.ISO", "*/PS2/*.iso", "*/ps2/*.iso",
                "*/t3.iso", "*/t3.ISO", "*/t3/*.iso", "*/t3/*.ISO"]
    vistos = []
    for raiz in raices:
        if not raiz or not os.path.isdir(raiz):
            continue
        for pat in patrones:
            for p in sorted(glob.glob(os.path.join(raiz, pat))):
                low = os.path.basename(p).lower()
                if not (low.endswith(".iso") or low.endswith(".ruta")):
                    continue
                if "tenkaichi" in low:
                    if "tenkaichi 2" in low or "tenkaichi 4" in low or "collection" in low or "dx" in low:
                        continue
                elif "t3" not in low:
                    continue
                real = isomod.ruta_destino(p)
                if real and real not in vistos:
                    vistos.append(real)
    for p in vistos:
        try:
            t = t3mod.T3(p)
            if "1" in t.afs and "2" in t.afs:
                return p
        except (OSError, ValueError, KeyError):
            continue
    return None


def abrir_t3(a):
    ruta = buscar_iso(a.iso)
    if not ruta:
        raise SystemExit("no encuentro la ISO de Tenkaichi 3: pásala con --iso o define DBZ3_T3_ISO")
    print("ISO: %s (solo lectura)" % ruta)
    return t3mod.T3(ruta)


def pistas_t3(t):
    af = t.afs["2"]
    out = []
    for i in range(T3_MUSICA[1]):
        k = T3_MUSICA[0] + i
        d = af.leer(k, 0x40)
        inf = adx_info(d)
        if not inf:
            continue
        out.append((i, k, af.nombres[k], inf, adx_bucle(d, inf, af.ent[k][1])))
    return out


# ------------------------------------------------------------------ B3 HD
class AfsB3:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            h = f.read(8)
            if h[:3] != b"AFS":
                raise ValueError("no es un AFS: %s" % path)
            n = struct.unpack_from("<I", h, 4)[0]
            t = f.read(8 * n)
        self.ent = [struct.unpack_from("<II", t, 8 * i) for i in range(n)]

    def __len__(self):
        return len(self.ent)

    def leer(self, k, n=None):
        o, s = self.ent[k]
        with open(self.path, "rb") as f:
            f.seek(o)
            return f.read(s if n is None else min(n, s))

    def info(self, k):
        return adx_info(self.leer(k, 0x40)) if 0 <= k < len(self.ent) else None


def clase_b3(k):
    for (a, b), txt in RANGOS_B3.items():
        if a <= k <= b:
            return txt
    return "musica"


def b3_default():
    for p in (os.path.join(ROOT, "us", "adx_usa.afs"), os.path.join(ROOT, "out", "build",
              "win-amd64-release", "us", "adx_usa.afs")):
        if os.path.isfile(p):
            return p
    return os.path.join(ROOT, "us", "adx_usa.afs")


def b3_jpn(afs_usa):
    return os.path.join(os.path.dirname(afs_usa), "adx_jpn.afs")


# ------------------------------------------------------------------ mod
def _expandir(txt, etiqueta):
    out = []
    for trozo in str(txt).replace(";", ",").split(","):
        trozo = trozo.strip()
        if not trozo:
            continue
        if "-" in trozo[1:]:
            a, b = trozo.split("-", 1)
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(trozo))
    if not out:
        raise SystemExit("sin %s" % etiqueta)
    return out


def _marcador(mod_dir):
    return os.path.join(mod_dir, ".disabled")


def _poner_estado(mod_dir, activar=None):
    marca = _marcador(mod_dir)
    if activar is True:
        if os.path.exists(marca):
            os.remove(marca)
    elif activar is False:
        os.makedirs(mod_dir, exist_ok=True)
        with open(marca, "w", encoding="utf-8") as f:
            f.write("disabled\n")
    return not os.path.exists(marca)


def _manifest(mod_dir, pistas, entradas):
    lineas = [
        "name=Musica de Budokai Tenkaichi 3",
        "type=audio",
        "description=Musica de tu propia copia de Tenkaichi 3 en pistas de Budokai 3 HD. "
        "Se genera en tu PC; no se reparte el audio.",
        "author=DBZ3HD",
        "version=1.0",
        "source=t3 (tu ISO)",
        "target=us/adx_usa.afs, us/adx_jpn.afs",
    ]
    with open(os.path.join(mod_dir, "manifest.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lineas) + "\n")
    with open(os.path.join(mod_dir, ETIQUETA), "w", encoding="utf-8", newline="\n") as f:
        f.write("Generado por mod center hd/t3_audio.py desde la ISO del jugador.\n")
        f.write("pistas T3: %s\nentradas B3: %s\n" % (", ".join(map(str, pistas)), ", ".join(map(str, entradas))))


def c_mod(a):
    t = abrir_t3(a)
    pistas = pistas_t3(t)
    if not pistas:
        raise SystemExit("la ISO no tiene las pistas de musica esperadas")
    afs_usa = a.afs or b3_default()
    if not os.path.isfile(afs_usa):
        raise SystemExit("no encuentro us/adx_usa.afs: pásalo con --afs")
    b3 = AfsB3(afs_usa)
    b3j = AfsB3(b3_jpn(afs_usa)) if os.path.isfile(b3_jpn(afs_usa)) else None

    elegidas = _expandir(a.pista, "pista") if a.pista else list(range(len(pistas)))
    if any(i < 0 or i >= len(pistas) for i in elegidas):
        raise SystemExit("pista fuera de rango (0-%d)" % (len(pistas) - 1))
    grupos = [_expandir(g, "entradas") for g in str(a.entradas).split(",")]
    grupos = [g for g in grupos if g]
    if not grupos:
        raise SystemExit("sin entradas")

    pares = []
    if len(elegidas) == len(grupos):
        for p, g in zip(elegidas, grupos):
            pares += [(p, e) for e in g]
    elif len(grupos) == 1:
        for n, e in enumerate(grupos[0]):
            pares.append((elegidas[n % len(elegidas)], e))
    else:
        raise SystemExit("usa el mismo numero de pistas que de grupos de entradas (o una sola lista de entradas)")

    mod_dir = os.path.join(a.mods, a.mod)
    nuevo = not os.path.isdir(mod_dir) or not os.listdir(mod_dir)
    if not nuevo and not os.path.isfile(os.path.join(mod_dir, ETIQUETA)):
        if not a.forzar:
            raise SystemExit("ya existe %s y no lo creo esta herramienta: usa --forzar o --mod otro" % mod_dir)
    os.makedirs(mod_dir, exist_ok=True)

    escritas, saltadas, crecidas = 0, 0, 0
    for pi, k in pares:
        inf_b3 = b3.info(k)
        if not inf_b3 or inf_b3["ch"] != 2:
            print("  %5d: no es una pista estereo de B3; se salta" % k)
            saltadas += 1
            continue
        _, kt3, nombre, inf_t3, bucle = pistas[pi]
        d = t.afs["2"].leer(kt3)
        inf = adx_info(d)
        if not inf or inf["ch"] != 2 or inf["hz"] != 24000:
            print("  %5d: la pista T3 %s no es ADX estereo 24 kHz; se salta" % (k, nombre))
            saltadas += 1
            continue
        if len(d) > b3.ent[k][1] and not a.permitir_crecer:
            print("  %5d: %s (%d B) no cabe en la entrada (%d B): usa --permitir-crecer o elige otra"
                  % (k, nombre, len(d), b3.ent[k][1]))
            saltadas += 1
            continue
        for afs, afs_obj in (("adx_usa.afs", b3), ("adx_jpn.afs", b3j)):
            if afs_obj is None or k >= len(afs_obj):
                continue
            inf2 = afs_obj.info(k)
            if not inf2 or inf2["ch"] != 2:
                continue
            tam = afs_obj.ent[k][1]
            if len(d) > tam:
                crecidas += 1
            datos = d + b"\0" * max(0, tam - len(d))
            dest = os.path.join(mod_dir, "us", afs, str(k))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(datos)
        escritas += 1
        print("  %5d <- T3 %02d %-18s %6.1f s %8d B (B3 %6.1f s %8d B)%s"
              % (k, pi, nombre, inf_t3["seg"], len(d), inf_b3["seg"], b3.ent[k][1],
                 "  CRECE" if len(d) > b3.ent[k][1] else ""))

    if not escritas:
        raise SystemExit("no se escribio ninguna pista")
    _manifest(mod_dir, elegidas, [k for _, k in pares])
    if a.activar:
        activo = _poner_estado(mod_dir, True)
    elif a.desactivar or (nuevo and not a.activar):
        activo = _poner_estado(mod_dir, False)
    else:
        activo = _poner_estado(mod_dir, None)
    print("MOD %s%s: %d entradas (%d saltadas, %d que hacen crecer la tabla)"
          % (mod_dir, "" if activo else "  [APAGADO]", escritas, saltadas, crecidas))
    if crecidas:
        print("aviso: las entradas que crecen usan la tabla virtual del runtime (sigue siendo valido, "
              "pero alarga un poco las lecturas de ese AFS)")
    print("Para usarlo: launcher > Centro de mods > marca el mod. No lo subas ni lo repartas: es audio del juego.")


# ------------------------------------------------------------------ comandos
def c_info(a):
    t = abrir_t3(a)
    print("AFS 1: %d entradas | AFS 2: %d entradas" % (len(t.afs["1"]), len(t.afs["2"])))
    print("musica T3: %d pistas" % len(pistas_t3(t)))
    b3 = AfsB3(getattr(a, "afs", None) or b3_default())
    est = [k for k in range(len(b3)) if (b3.info(k) or {}).get("ch") == 2]
    print("B3 %s: %d entradas, %d estereo (musica)" % (os.path.basename(b3.path), len(b3), len(est)))
    for r, txt in sorted(RANGOS_B3.items()):
        n = sum(1 for k in est if r[0] <= k <= r[1])
        print("  %d-%d: %d pistas  %s" % (r[0], r[1], n, txt))


def c_pistas(a):
    t = abrir_t3(a)
    pistas = pistas_t3(t)
    sel = _expandir(a.pista, "pista") if a.pista else [p[0] for p in pistas]
    for p in pistas:
        i, k, nombre, inf, bucle = p
        if i not in sel:
            continue
        print("%2d  %-18s %d Hz  %d ch  %6.1f s  %s" % (i, nombre, inf["hz"], inf["ch"], inf["seg"],
              "bucle %d..%d" % bucle if bucle else "sin bucle declarado"))
        if a.wav:
            d = t.afs["2"].leer(k)
            pcm, hz = adx_pcm(d)
            dest = os.path.join(a.wav, "t3_%02d.wav" % i)
            escribir_wav(dest, pcm, hz)
            print("      -> %s (RMS %.0f)" % (dest, rms(pcm)))


def c_b3(a):
    afs = a.afs or b3_default()
    b3 = AfsB3(afs)
    print("%s: %d entradas" % (afs, len(b3)))
    for k in range(len(b3)):
        inf = b3.info(k)
        if not inf or inf["ch"] != 2:
            continue
        print("%5d  %5d Hz  %6.1f s  %8d B  %s" % (k, inf["hz"], inf["seg"], b3.ent[k][1], clase_b3(k)))


def c_voces(a):
    t = voces.T3Voices(a.iso or buscar_iso(None))
    clips = t.clips_tiempos(a.personaje, a.idioma, a.juego)
    if not clips:
        raise SystemExit("sin clips con audio para '%s' (%s)" % (a.personaje, a.idioma))
    if a.max:
        clips = clips[:a.max]
    total = sum(d for _, d in clips)
    print("T3 %s (%s): %d clips, %.1f s" % (a.personaje, a.idioma, len(clips), total))
    for i, (k, seg) in enumerate(clips):
        print("  %5d  %5.2f s" % (k, seg))
        if a.salida and i < (a.max or len(clips)):
            pcm, hz = adx_pcm(t.adx(k))
            dest = os.path.join(a.salida, "%05d_%.2fs.wav" % (k, seg))
            escribir_wav(dest, pcm, hz)
    if a.salida:
        print("WAV -> %s" % a.salida)
    print('Para un personaje importado (importar.py / personaje.toml): voces = "t3:%s" y gritos = "t3:%s"'
          % (a.personaje, a.personaje))
    print("No se genera mod de voces para personajes nativos: los gritos de golpe van por el banco XMA "
          "del donante (gritos.py) y el mapa clip -> situacion de T3 no esta medido.")


def _check(ok, txt):
    print(("OK   " if ok else "FALLO") + " " + txt)
    return bool(ok)


def c_prueba(a):
    base = os.path.join("D:\\tmp", "t3_audio_prueba_%d" % os.getpid())
    os.makedirs(base, exist_ok=True)
    ok = True
    # 1) cabecera y relleno, sin ISO
    falso = bytearray(b"\x80\x00\x00\x24\x03\x12\x04\x02" + struct.pack(">II", 24000, 24000)
                      + b"\x01\xf4\x04\x00" + bytes(0x28))
    falso += b"(c)CRI" + bytes(16)
    inf = adx_info(bytes(falso))
    ok &= _check(inf and inf["ch"] == 2 and inf["hz"] == 24000 and inf["seg"] == 1.0,
                 "cabecera ADX (estereo 24 kHz, 1 s)")
    ok &= _check(len(falso) < 1000 and adx_bucle(bytes(falso), inf) is None, "ADX corto sin bucle")
    # 2) ISO: 2 pistas reales, bucle, decodificacion y mod con 2 entradas
    ruta = buscar_iso(a.iso)
    if not ruta:
        print("ISO no encontrada: se omite la prueba con datos reales (define DBZ3_T3_ISO o usa --iso)")
        print("PRUEBA %s" % ("OK" if ok else "FALLO"))
        return 0 if ok else 1
    t = t3mod.T3(ruta)
    pistas = pistas_t3(t)
    ok &= _check(len(pistas) == 20, "20 pistas de musica (zs3_00..19): %d" % len(pistas))
    for i in (0, 1):
        k = pistas[i][1]
        d = t.afs["2"].leer(k)
        inf = adx_info(d)
        bucle = adx_bucle(d, inf)
        pcm, hz = adx_pcm(d[:min(len(d), 0x40000)])
        ok &= _check(inf["ch"] == 2 and inf["hz"] == 24000 and inf["seg"] > 100 and bucle is not None,
                     "pista %d: %d ch, %d Hz, %.1f s, bucle %s" % (i, inf["ch"], inf["hz"], inf["seg"], bucle))
        ok &= _check(hz == 24000 and rms(pcm) > 100, "pista %d decodifica (RMS %.0f)" % (i, rms(pcm)))
        escribir_wav(os.path.join(base, "t3_%02d.wav" % i), pcm, hz)
    # mod de prueba: 2 pistas en 2 entradas conocidas
    mods = os.path.join(base, "mods")
    aa = argparse.Namespace(iso=ruta, afs=None, mods=mods, mod="t3_prueba", pista="0,1",
                            entradas="4187,4267", permitir_crecer=False, activar=True, desactivar=False,
                            forzar=True)
    c_mod(aa)
    mod_dir = os.path.join(mods, "t3_prueba")
    for k, pi in ((4187, 0), (4267, 1)):
        p = os.path.join(mod_dir, "us", "adx_usa.afs", str(k))
        orig = AfsB3(b3_default()).ent[k][1]
        if os.path.isfile(p):
            with open(p, "rb") as f:
                d = f.read()
            src = t.afs["2"].leer(pistas[pi][1])
            ok &= _check(len(d) == max(orig, len(src)) and d[:len(src)] == src,
                         "entrada %d: ADX intacto y relleno a %d B" % (k, len(d)))
        else:
            ok &= _check(False, "entrada %d escrita" % k)
    ok &= _check(os.path.isfile(os.path.join(mod_dir, "manifest.txt")), "manifest.txt")
    ok &= _check(not os.path.exists(os.path.join(mod_dir, ".disabled")), "mod de prueba activado")
    # 3) voces
    tv = voces.T3Voices(ruta)
    clips = tv.clips("BABY_V", "usa")
    ok &= _check(len(clips) > 100, "voces BABY_V usa: %d clips" % len(clips))
    if clips:
        pcm, hz = adx_pcm(tv.adx(clips[0]))
        ok &= _check(hz == 24000 and rms(pcm) > 50, "primer clip de BABY_V decodifica (RMS %.0f)" % rms(pcm))
    print("temporales: %s" % base)
    print("PRUEBA %s" % ("OK" if ok else "FALLO"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    for nm in ("info", "pistas", "b3", "mod", "voces", "prueba"):
        s = sp.add_parser(nm)
        s.add_argument("--iso")
        if nm in ("b3", "mod"):
            s.add_argument("--afs")
        if nm in ("pistas", "mod"):
            s.add_argument("--pista")
        if nm == "pistas":
            s.add_argument("--wav")
        if nm == "mod":
            s.add_argument("--mods", default=None)
            s.add_argument("--mod", default="t3_musica")
            s.add_argument("--entradas", default="4187-4215")
            s.add_argument("--permitir-crecer", action="store_true")
            g = s.add_mutually_exclusive_group()
            g.add_argument("--activar", action="store_true")
            g.add_argument("--desactivar", action="store_true")
            s.add_argument("--forzar", action="store_true")
        if nm == "voces":
            s.add_argument("--personaje", required=True)
            s.add_argument("--idioma", default="usa", choices=("usa", "jpn"))
            s.add_argument("--juego", type=int, choices=(1, 2))
            s.add_argument("--salida")
            s.add_argument("--max", type=int)
    a = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if a.cmd == "mod" and not a.mods:
        import roster_build  # noqa: PLC0415
        a.mods = roster_build.default_mods()
    fn = {"info": c_info, "pistas": c_pistas, "b3": c_b3, "mod": c_mod, "voces": c_voces,
          "prueba": c_prueba}[a.cmd]
    return fn(a) or 0


if __name__ == "__main__":
    sys.exit(main())

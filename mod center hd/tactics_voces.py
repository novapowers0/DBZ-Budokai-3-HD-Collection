"""tactics_voces.py - TACTICS-09: gritos de Dragon Tactics por PAPEL, no por indice suelto.

Cada unidad grita con el banco nativo de su personaje (lang_usa/lang_jpn.afs, char372+0x24). Los ids del banco se agrupan por papel
(medidos en un combate normal, H5): tecnica = 42..50, KO = 19..25. Este script decodifica (XMA -> PCM, ffmpeg) cada id de cada grupo
en los dos idiomas y deja en tactics_units.toml solo ids que SUENAN (pico PCM > 0) en ambos; conserva los que ya sonaban.

  python tactics_voces.py comprobar   # informe: ids mudos de la seleccion actual (sale 1 si hay alguno)
  python tactics_voces.py escribir    # reescribe voz.ataque / voz.ko de las unidades con ids mudos
"""
import os
import re
import struct
import subprocess
import shutil
import sys
import tomllib
from pathlib import Path

AQUI = Path(__file__).resolve().parent
RAIZ = AQUI.parent
sys.path[:0] = [str(AQUI), str(RAIZ / "awo_tools")]
import roster_build as rb  # noqa: E402
import gritos  # noqa: E402

GRUPO = {"ataque": range(42, 51), "ko": range(19, 26)}   # papel -> ids del banco nativo (mismo contrato que src/tactics/catalogo.cpp)
FFMPEG = shutil.which("ffmpeg")


def pico(bank, sound):
    start = struct.unpack_from(">I", bank, 0x10)[0]
    r = gritos.records(bank)[sound]
    size, off = struct.unpack_from(">II", bank, r + 0x14)
    length = struct.unpack_from(">I", bank, r + 0xE0)[0]
    data = bank[start + off:start + off + size]
    if not size:
        return 0
    if data[:7] == gritos.MAGIC:
        pcm = gritos.rx_decode(data, length)
        return int(abs(pcm.astype("int32")).max()) if len(pcm) else 0
    f = bank[r + 0xBC:r + 0xBC + 52]
    fmt = (struct.pack("<HHIIHHH", 0x166, *struct.unpack(">HIIHH", f[2:16]), 34)
           + struct.pack("<HIIIIIIIBBH", *struct.unpack(">HIIIIIIIBBH", f[18:52])))
    riff = (b"RIFF" + struct.pack("<I", 20 + len(fmt) + len(data)) + b"WAVEfmt " + struct.pack("<I", len(fmt))
            + fmt + b"data" + struct.pack("<I", len(data)) + data)
    p = subprocess.run([FFMPEG, "-v", "error", "-i", "pipe:0", "-f", "s16le", "-ac", "1", "-ar", "16000", "pipe:1"], input=riff, capture_output=True, timeout=20)
    pcm = gritos.np.frombuffer(p.stdout, dtype="<i2")[:length]
    return int(gritos.np.abs(pcm.astype("int32")).max()) if len(pcm) else 0


def suenan(tmp):
    """{personaje B3: {papel: [ids que suenan en usa Y jpn]}}"""
    man = {u["id"]: u["b3"] for u in tomllib.loads((AQUI / "tactics_manifest.toml").read_text("utf-8"))["unidad"]}
    res = {}
    for lang in ("usa", "jpn"):
        afs = rb.Afs(str(RAIZ / "us" / ("lang_" + lang + ".afs")), tmp)
        for ch in sorted(set(man.values())):
            amb = rb.unlzx(afs.raw(rb.IDS[ch]["lang"][0]), tmp)
            c = gritos.amb_children(amb)[0]
            bank = amb[c[0]:c[0] + c[1]]
            n = len(gritos.records(bank))
            for papel, ids in GRUPO.items():
                ok = {i for i in ids if i < n and pico(bank, i) > 0}
                d = res.setdefault(ch, {})
                d[papel] = ok if papel not in d else d[papel] & ok
    return man, res


def main():
    modo = sys.argv[1] if len(sys.argv) > 1 else "comprobar"
    assert FFMPEG, "falta ffmpeg"
    tmp = os.environ.get("TEMP", str(AQUI))
    man, res = suenan(tmp)
    ruta = AQUI / "tactics_units.toml"
    texto = ruta.read_text("utf-8")
    units = tomllib.loads(texto)["unidad"]
    mudos, cambios = 0, {}
    for u in units:
        ok = res[man[u["id"]]]
        nuevo = {}
        for papel in GRUPO:
            sel = u["voz"][papel]
            m = [i for i in sel if i not in ok[papel]]
            mudos += len(m)
            if m:
                print("%-10s %-6s mudos %s (suenan %s)" % (u["id"], papel, m, sorted(ok[papel])))
            quedan = [i for i in sel if i in ok[papel]]
            libres = [i for i in sorted(ok[papel]) if i not in quedan]
            lista = (quedan + libres)[:3]
            while len(lista) < 3 and lista:   # el catalogo exige 3: repite los que suenan
                lista.append(lista[len(lista) % len(quedan + libres)])
            assert len(lista) == 3, (u["id"], papel, "ningun id suena")
            nuevo[papel] = lista
        if nuevo["ataque"] != u["voz"]["ataque"] or nuevo["ko"] != u["voz"]["ko"]:
            cambios[u["id"]] = nuevo
    print("seleccion actual: %d ids mudos en %d unidades x 2 idiomas comprobados" % (mudos, len(units)))
    if modo == "escribir" and cambios:
        lineas, uid, hechas = texto.split("\n"), None, 0
        for i, ln in enumerate(lineas):
            if m := re.match(r'id = "([^"]+)"', ln):
                uid = m.group(1)
            elif ln.startswith("voz = {") and uid in cambios:
                n = cambios[uid]
                lineas[i], k = re.subn(r"ataque = \[[^\]]*\], ko = \[[^\]]*\]", "ataque = [%s], ko = [%s]" % (", ".join(map(str, n["ataque"])), ", ".join(map(str, n["ko"]))), ln)
                assert k == 1, uid
                hechas += 1
        assert hechas == len(cambios), (hechas, len(cambios))
        ruta.write_text("\n".join(lineas), "utf-8", newline="\r\n")   # el toml va con CRLF
        print("reescritas %d unidades" % len(cambios))
        return 0
    return 1 if mudos else 0


if __name__ == "__main__":
    sys.exit(main())

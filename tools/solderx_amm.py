"""Comprueba las pistas AMM PSP originales frente a PS2 y HD, sin ejecutar juegos.

python tools/solderx_amm.py --salida D:/tmp/solDERX/amm_amplios.json
No acepta retarget, interpolacion ni fidelidad visual. No guarda recursos de juego.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / "awo_tools"), str(ROOT / "mod center hd")]
import b1port as bp
import iso
import ps2hd
import sb_amm
import sbport


def digest(b):
    return hashlib.sha256(b).hexdigest()


def original_tracks(b):
    """Lector independiente: cuatro u16, dos punteros, frames segun selector."""
    na, table, nb, _ = struct.unpack_from("<4I", b, 0x10)
    for a in range(na):
        nf, options, bones = struct.unpack_from("<HHI", b, table + 8*a)
        if not bones:
            continue
        for bone in range(nb):
            count, tracks = struct.unpack_from("<II", b, bones + 8*bone)
            for i in range(count):
                channel, width, n, typ, frames, values = struct.unpack_from("<4H2I", b, tracks + 16*i)
                if not n:
                    continue
                if channel not in (0, 1, 2) or width not in (0, 1, 2):
                    raise ValueError("Canal/ancho original desconocido")
                fmt, size = ("B", 1) if width == 0 else ("H", 2) if width == 1 else ("I", 4)
                indices = struct.unpack_from("<%d%s" % (n, fmt), b, frames)
                yield a, bone, nf, channel, width, n, typ, indices, values


def compare(b, converted, *, hd=False, wide=True):
    """Cada clave contra el origen; no usa el parser de pistas del conversor."""
    endian = ">" if hd else "<"
    na, table, nb, names = struct.unpack_from(endian + "4I", converted, 0x10)
    sna, stable, snb, snames = struct.unpack_from("<4I", b, 0x10)
    if (na, nb) != (sna, snb) or converted[names:names+32*nb] != b[snames:snames+32*nb]:
        raise ValueError("Cabecera/nombres distintos")
    for a in range(na):
        nf, _, bones = struct.unpack_from("<HHI", b, stable + 8*a)
        flags, _, dnf, dst = struct.unpack_from(endian + "4I", converted, table + 16*a)
        if bool(bones) != bool(dst) or (bones and nf != dnf):
            raise ValueError("Clip/duracion distintos")
    stats = Counter()
    for a, bone, nf, channel, width, n, typ, frames, values in original_tracks(b):
        if width and not wide:
            stats["omitted_tracks"] += 1
            stats["omitted_keys"] += n
            continue
        flags, _, _, dst = struct.unpack_from(endian + "4I", converted, table + 16*a)
        per = 3 if flags & 16 else 2
        if channel >= per:
            raise ValueError("Falta canal de escala")
        track = struct.unpack_from(endian + "I", converted, dst + 4*(per*bone+channel))[0]
        if not track or struct.unpack_from(endian + "3I", converted, track) != (0, typ, n):
            raise ValueError("Metadatos distintos: clip %d, hueso %d, canal %d" % (a, bone, channel))
        unit, frame_size, value_size = (2, 2, 6) if channel == 0 else (4, 4, 12)
        for k, frame in enumerate(frames):
            p = track + 12 + (frame_size + value_size)*k
            got_frame = struct.unpack_from(endian + ("H" if channel == 0 else "I"), converted, p)[0]
            raw = b[values+value_size*k:values+value_size*(k+1)]
            expected = b"".join(raw[x:x+unit][::-1] for x in range(0, value_size, unit)) if hd else raw
            if got_frame != frame or converted[p+frame_size:p+frame_size+value_size] != expected:
                raise ValueError("Clave distinta: clip %d, hueso %d, canal %d, clave %d" % (a, bone, channel, k))
            stats["keys"] += 1
            if width:
                stats["recovered_keys"] += 1
                stats["recovered_keys_above_255"] += frame > 255
        stats["tracks"] += 1
        stats["nonmonotonic_original_tracks"] += any(y < x for x, y in zip(frames, frames[1:]))
        if width:
            stats["recovered_tracks"] += 1
        stats["max_frame"] = max(stats["max_frame"], max(frames))
    return dict(stats)


def historic_hashes(path):
    hashes = {}
    def walk(x):
        if isinstance(x, dict):
            if "conversion_check" in x:
                hashes[x["sha256"]] = x["conversion_check"]["converted"]["sha256"]
            for v in x.values():
                walk(v)
        elif isinstance(x, list):
            for v in x:
                walk(v)
    if path:
        walk(json.loads(Path(path).read_text(encoding="utf-8")))
    return hashes


def acquire(game, historical):
    source = sbport.SbIso(iso.find_game(game))
    unique = {}
    try:
        for code, index in source.characters():
            if code == "GHF":
                continue
            packages = [(index, source.names[index])]
            bsp = source.find("BSP_%s.AMB" % code)
            if bsp is not None:
                packages.append((bsp, source.names[bsp]))
            packages += [(i, name) for i, name in enumerate(source.names)
                         if re.fullmatch("BC" + code + r"(M\d+|B\d+)\.AMB", name)]
            for index, name in packages:
                for child, (b, _) in enumerate(bp.amb_kids(source.entry(index))):
                    if b[:4] != b"#AMM" or not sb_amm.is_sb(b):
                        continue
                    sha = digest(b)
                    origin = dict(character=code, package=name, index=index, child=child)
                    if sha in unique:
                        unique[sha]["origins"].append(origin)
                        continue
                    old = sb_amm.convert(b)
                    if sha in historical and digest(old) != historical[sha]:
                        raise ValueError("La ruta por defecto cambia respecto al censo anterior: " + sha)
                    new = sb_amm.convert(b, wide_frames=True)
                    hd = ps2hd.conv_amm(new)
                    before = compare(b, old, wide=False)
                    after = compare(b, new)
                    hd_after = compare(b, hd, hd=True)
                    if after != hd_after:
                        raise ValueError("Contadores PS2/HD distintos")
                    anomalies = []
                    for a, bone, nf, channel, width, n, typ, frames, _ in original_tracks(b):
                        falls = [[k, frames[k-1], frames[k]] for k in range(1, n) if frames[k] < frames[k-1]]
                        if falls:
                            anomalies.append(dict(clip=a, bone=bone, channel=channel, width=width,
                                                  type=typ, duration=nf, falls=falls))
                    unique[sha] = dict(sha256=sha, origins=[origin], before=before, after=after,
                                       ps2_sha256=digest(new), hd_sha256=digest(hd),
                                       default_unchanged=sha in historical, changed=old != new,
                                       original_anomalies=anomalies)
    finally:
        source.f.close()
        source.iso._f.close()
    totals = Counter()
    for row in unique.values():
        for k, v in row["after"].items():
            if k == "max_frame":
                totals[k] = max(totals[k], v)
            else:
                totals[k] += v
    return dict(unique_amm=len(unique), historical_matches=sum(x["default_unchanged"] for x in unique.values()),
                changed_amm=sum(x["changed"] for x in unique.values()), totals=dict(totals), resources=list(unique.values()))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--salida", required=True)
    ap.add_argument("--censo-anterior")
    args = ap.parse_args()
    dest = Path(args.salida)
    if dest.exists() or not dest.parent.is_dir():
        ap.error("la salida debe ser nueva y su carpeta debe existir")
    old = historic_hashes(args.censo_anterior)
    result = dict(scope="PSP -> PS2 -> HD, antes del retarget; no acepta fidelidad en juego",
                  default_enabled=False, games={g: acquire(g, old) for g in ("sb1", "sb2")})
    with dest.open("x", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    for game, result in result["games"].items():
        print(game, result["unique_amm"], "AMM;", result["historical_matches"], "predeterminados identicos;", result["totals"])


if __name__ == "__main__":
    main()

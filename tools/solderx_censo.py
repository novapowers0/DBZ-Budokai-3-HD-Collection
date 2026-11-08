"""Censo estático de B1/SB1/SB2 desde las copias del jugador, sin extraer arte.

python tools/solderx_censo.py --salida D:/tmp/solDERX
La carpeta de salida debe existir. No prueba fidelidad en juego ni acepta modos.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / "awo_tools"), str(ROOT / "mod center hd")]
import iso
import b1port as bp
import sbport as sb
import sb_tecnicas as st
import sb_amm


def ident(b):
    return dict(bytes=len(b), sha256=hashlib.sha256(b).hexdigest(), magic=b[:8].hex())


def check_sb_animation(b):
    """Compara cada clave PSP con la clave emitida; no evalúa la pose en B3."""
    converted = sb_amm.convert(b)
    na, table, nb, _ = struct.unpack_from("<4I", b, 0x10)
    dest_table = struct.unpack_from("<I", converted, 0x14)[0]
    checked = collections.Counter()
    unknown = collections.Counter()
    for a in range(na):
        nf, _, bones = struct.unpack_from("<HHI", b, table+8*a)
        flags, _, dest_nf, dest_bones = struct.unpack_from("<4I", converted, dest_table+16*a)
        if not bones:
            if dest_bones:
                raise ValueError("AMM: clip vacío adquiere pistas")
            checked["empty_clips"] += 1
            continue
        if nf != dest_nf or not dest_bones:
            raise ValueError("AMM: duración/clip difieren después de convertir")
        per = 3 if flags & 0x10 else 2
        checked["clips"] += 1
        for j in range(nb):
            cnt, ptr = struct.unpack_from("<II", b, bones+8*j)
            seen = set()
            for r in range(cnt):
                cls, nk, typ, fp, vp = struct.unpack_from("<IHHII", b, ptr+16*r)
                if cls > 2:
                    unknown["track_class_%d" % cls] += 1
                    continue
                if not nk:
                    continue
                if cls in seen:
                    unknown["duplicate_class"] += 1
                    continue
                seen.add(cls)
                if cls >= per:
                    raise ValueError("AMM: falta pista de escala")
                out = struct.unpack_from("<I", converted, dest_bones+4*(per*j+cls))[0]
                if not out or struct.unpack_from("<II", converted, out+4) != (typ, nk):
                    raise ValueError("AMM: metadatos de pista distintos")
                size = 6 if cls == 0 else 12
                for k in range(nk):
                    off = out+12+(size+(2 if cls == 0 else 4))*k
                    frame = struct.unpack_from("<H" if cls == 0 else "<I", converted, off)[0]
                    values = off+(2 if cls == 0 else 4)
                    if frame != b[fp+k] or converted[values:values+size] != b[vp+size*k:vp+size*(k+1)]:
                        raise ValueError("AMM: una clave de origen cambia")
                    checked["keys"] += 1
                checked["tracks"] += 1
    return dict(status="partial" if unknown else "pass", checked=dict(checked), unknown=dict(unknown),
                scope="source PSP to intermediate PS2; not HD retarget/runtime", converted=ident(converted))


def payload(b):
    out = ident(b)
    magic = b[:4]
    if magic == b"#BSK":
        source = sb.Src(b, True)
        out.update(codes=[i for i, a in enumerate(source.L) if a], hr_count=source.nhr)
    elif magic in (b"#AMM", b"#AMC"):
        n, table = struct.unpack_from("<II", b, 0x10)
        compressed = magic == b"#AMM" and sb_amm.is_sb(b)
        out["clip_layout"] = "PSP 8 B" if compressed else "PS2 16 B"
        out["clips"] = [dict(index=i, frames=struct.unpack_from("<H" if compressed else "<I", b,
                            table + 8*i if compressed else table + 16*i + 8)[0]) for i in range(n)]
        if compressed:
            out["conversion_check"] = check_sb_animation(b)
    elif magic == b"#SPX":
        n = struct.unpack_from("<I", b, 0x18)[0]
        entries = struct.unpack_from("<%dI" % n, b, 0x20)
        starts = sorted(set(x for x in entries if x != 0xFFFFFFFF)) + [len(b)]
        if any(x < 0x20 + 4*n or x >= len(b) for x in starts[:-1]):
            raise ValueError("SPX con rangos de guion inválidos")
        out["slots"] = [dict(index=i, offset=x, bytes=starts[starts.index(x)+1]-x)
                        for i, x in enumerate(entries) if x != 0xFFFFFFFF]
    if magic == b"#AMB":
        out["children"] = [dict(index=i, type=t, **payload(k))
                           for i, (k, t) in enumerate(bp.amb_kids(b))]
    return out


def afs_inventory(im):
    result = []
    for name, (_, size) in sorted(im.files().items()):
        if not name.lower().endswith(".afs"):
            continue
        with im.open(name) as f:
            head = f.read(8)
            if head[:4] != b"AFS\0":
                raise ValueError("No es AFS: " + name)
            n = struct.unpack_from("<I", head, 4)[0]
            if 8 + 8*(n+1) > size:
                raise ValueError("Tabla AFS truncada: " + name)
            raw = f.read(8*(n+1))
            tab = [struct.unpack_from("<II", raw, 8*i) for i in range(n)]
            no, ns = struct.unpack_from("<II", raw, 8*n)
            names = []
            if no and ns == n*48 and no+ns <= size:
                f.seek(no)
                nt = f.read(ns)
                names = [nt[48*i:48*i+32].split(b"\0")[0].decode("latin1") for i in range(n)]
            rows = []
            for i, (o, s) in enumerate(tab):
                if o+s > size:
                    raise ValueError("Entrada AFS fuera del archivo: %s/%d" % (name, i))
                f.seek(o)
                rows.append(dict(index=i, offset=o, bytes=s, magic=f.read(min(s, 8)).hex(),
                                 name=names[i] if names else None, usage="sin comprobar"))
            result.append(dict(path=name, entries=n, table_sha256=hashlib.sha256(raw).hexdigest(), rows=rows))
    return result


def satan_table(elf):
    hits = [p for p in range(0, len(elf)-128*11+1, 16)
            if all(elf[p+128*k+0x20] == 21 and elf[p+128*k+0x70] == 3 and
                   elf[p+128*k+0x0F] == 0x3F for k in range(11))]
    if len(hits) != 1:
        raise ValueError("Tabla Satán ambigua o ausente: %r" % hits)
    p = hits[0]
    return [dict(round=k+1, elf_offset=p+128*k, **{
        key: struct.unpack_from("<"+fmt, elf, p+128*k+offset)[0]
        for key, fmt, offset in (("actor", "B", 0x20), ("rival", "B", 0x50),
            ("forma", "B", 0x52), ("cantidad", "H", 0x56), ("vida", "H", 0x5A),
            ("ki", "H", 0x5C), ("escenario", "B", 0x71), ("musica", "B", 0x72),
            ("tiempo", "i", 0x74))}) for k in range(11)]


def sb_losses(bsk):
    """Cada AP7 de origen se pasa por la misma regla que usa sbport; no ejecuta el port."""
    src = sb.Src(bsk, True)
    changed, dropped = [], []
    counts = collections.Counter()
    for code, a in enumerate(src.L):
        if not a:
            continue
        _, nap, table = struct.unpack_from("<3I", bsk, a+0x24)
        for j in range(nap):
            kind, n, offset = struct.unpack_from("<HHI", bsk, table+8*j)
            if kind != 7:
                continue
            for i in range(n):
                ln = bsk[offset+20*i:offset+20*(i+1)]
                converted = sb.fx_line(ln, counts)
                frame, cat, value = struct.unpack_from("<H", ln)[0], *struct.unpack_from("<II", ln, 4)
                row = dict(code=code, frame=frame, category=cat, value=value, source_offset=offset+20*i)
                if converted is None:
                    dropped.append(row)
                elif struct.unpack_from("<I", converted, 8)[0] != value:
                    changed.append(dict(row, destination_value=struct.unpack_from("<I", converted, 8)[0]))
    hr_types = collections.Counter(struct.unpack_from("<H", bsk, src.hr + 160*h + 20*i + 4)[0]
                                   for h in range(src.nhr) for i in range(8))
    return dict(counts=dict(counts), dropped=dropped, changed=changed, hr_types=dict(hr_types))


def acquire(game):
    path = iso.find_game(game)
    if not path:
        raise FileNotFoundError("No se encuentra la copia de " + game)
    im = iso.Iso(path)
    try:
        boot = im.boot_elf() if game == "b1" else im.find("EBOOT.BIN")
        elf = im.read(boot)
        g = dict(path=str(Path(path).resolve()), iso_bytes=Path(path).stat().st_size,
                 boot=dict(path=boot, **ident(elf)), files=im.files(), afs=afs_inventory(im))
        src = bp.B1(path) if game == "b1" else sb.SbIso(path)
        try:
            if game == "b1":
                records = []
                for i in range(25):
                    r = src.record(i)
                    resources = {key: dict(afs_index=r[key], **payload(src.entry(r[key])))
                                 for key in ("common", "amm", "small", "bcm", "bsk", "spx") if r[key] >= 0}
                    resources["models"] = [dict(afs_index=j, **ident(src.entry(j))) for j in r["models"]]
                    records.append(dict(index=i, elf_offset=src.rec0+i*bp.B1_REC_SIZE,
                                        references=r, resources=resources))
                g.update(records=records, satan_rounds=satan_table(elf),
                         satan_packages={str(i): payload(src.entry(i)) for i in (2372, 2374)})
            else:
                chars = []
                for code, i in src.characters():
                    if code == "GHF":
                        continue
                    bc = src.entry(i)
                    bsp = src.entry(src.find("BSP_%s.AMB" % code))
                    techs, p = st.sb_tecnicas(bc, bsp)
                    bsk = next(k for k, _ in bp.amb_kids(bc) if k[:4] == b"#BSK")
                    bcm = next(k for k, _ in bp.amb_kids(bc) if k[:4] == b"#BCM")
                    log = []
                    sb.sb_bcm_nodes(bcm, log)
                    related = [dict(index=j, name=n, **payload(src.entry(j))) for j, n in enumerate(src.names)
                               if re.fullmatch("BC"+code+r"(M\d+|B\d+)\.AMB", n)]
                    chars.append(dict(code=code, index=i, package=payload(bc), related=related,
                                      bsp=payload(bsp), techs=techs, conversion=log,
                                      ap7=sb_losses(bsk), ast_codes=list(p["ast"]), ase_codes=list(p["ase"])))
                g.update(characters=chars, excluded=[code for code, _ in src.characters() if code == "GHF"])
        finally:
            src.f.close()
            src.iso._f.close()
        return g
    finally:
        im._f.close()


def markdown(result):
    lines = ["# Censo medido solDERX — originales B1 / SB1 / SB2", "",
             "**Inventario estático, sin aceptación de fidelidad en juego.** GHF excluido por encargo.",
                "Los estados describen las rutas actuales del conversor; no acreditan una importación ni una comparación visual.",
             "Identidades, offsets, todos los archivos/AFS, modelos y pérdidas AP7 en `censo_medido.json`.",
             "Las entradas sin consumidor identificado conservan `usage=sin comprobar`; no se atribuyen a un modo.", ""]
    for game, g in result["games"].items():
        lines += ["## " + game.upper(), "", "Ejecutable `%s`: `%s` (%d B)." %
                  (g["boot"]["path"], g["boot"]["sha256"], g["boot"]["bytes"]), "",
                  "| AFS original | Entradas |", "|---|---:|"]
        lines += ["| `%s` | %d |" % (a["path"], a["entries"]) for a in g["afs"]]
        if game == "b1":
            lines += ["", "### 25 registros originales (no catálogo del port)", "",
                      "| Registro / ELF | AMM propio / clips | BSK / códigos | SPX / ranuras | Estado / prueba |", "|---|---|---|---|---|"]
            for r in g["records"]:
                a, b, s = (r["resources"][x] for x in ("amm", "bsk", "spx"))
                lines.append("| %d / `%#x` | %d / %d | %d / %d | %d / %d | falta acreditar traducción de animaciones; SPX/cámara sustituidos en `b1port.port` |" %
                             (r["index"], r["elf_offset"], a["afs_index"], len(a["clips"]), b["afs_index"], len(b["codes"]), s["afs_index"], len(s["slots"])))
            lines += ["", "### Leyenda de Satán: 11 registros medidos en el ELF", "",
                      "| Ronda / ELF | Rival / forma / cantidad | Vida / ki / tiempo | Estado |", "|---|---|---|---|"]
            for r in g["satan_rounds"]:
                lines.append("| %d / `%#x` | %d / %d / %d | %d / %d / %d | traducido sin comprobar en juego completo |" %
                             tuple(r[k] for k in ("round", "elf_offset", "rival", "forma", "cantidad", "vida", "ki", "tiempo")))
            for index, p in g["satan_packages"].items():
                lines += ["", "### Escena AFS " + index, "",
                          "| Hijo / recurso | Tamaño | Clips (cuadros) / ranuras | Estado |", "|---|---:|---|---|"]
                for c in p["children"]:
                    magic = bytes.fromhex(c["magic"])[:4].decode("ascii", "replace")
                    detail = ", ".join(str(x["frames"]) for x in c.get("clips", []))
                    detail = detail or ", ".join(str(x["index"]) for x in c.get("slots", [])) or "identidad en JSON"
                    lines.append("| %d / `%s` | %d | %s | falta acreditar uso y traducción completos |" % (c["index"], magic, c["bytes"], detail))
        else:
            lines += ["", "### Luchadores y variantes de origen", "",
                      "| Código / BC | Técnicas | Variantes/modelos | AP7 quitadas / cambiadas | Estado |", "|---|---:|---:|---:|---|"]
            for c in g["characters"]:
                lines.append("| %s / %d | %d | %d | %d / %d | falta acreditar traducción de animaciones; guion/cámara y agarre sustituidos en `sbport` |" %
                             (c["code"], c["index"], len(c["techs"]), len(c["related"]), len(c["ap7"]["dropped"]), len(c["ap7"]["changed"])))
            lines += ["", "### Cada técnica encontrada en el BCM original", "",
                      "| Luchador / código | Condición / formas / ki | AST / ASE referidos | Estado actual / prueba de ruta |", "|---|---|---|---|"]
            for c in g["characters"]:
                for t in c["techs"]:
                    e = t["entrada"]
                    r = st.RECETAS.get(c["code"], {}).get(t["codigo"], {})
                    status = ("falta: " + r["omitir"] if r.get("omitir") else
                              "sustituido: cinemática donante" if r.get("definitivo") or e["cond"] & 0x18 else
                              "falta acreditar traducción; ruta de efectos híbridos/donante")
                    lines.append("| %s / `%#x` | `%#x` / `%#x` / %d | %s / %s | %s (`sb_tecnicas`) |" %
                                 (c["code"], t["codigo"], e["cond"], e["booster"], e["ki"],
                                  ",".join(hex(x) for x in t["ast"]) or "—", ",".join(hex(x) for x in t["ase"]) or "—", status))
    lines += ["", "## Bloqueo de reconstrucción local", "", result["donor_probe"], "",
              "No se ha ejecutado un traductor fiel de SPX SB: `sbport.port` conserva los hijos de cámara/SPX del donante.",
              "No hay comparación por fotograma de todas las ramas, actores, cámaras ni voces de estos modos.",
              "El inventario no convierte guiones desconocidos ni sustituye esa prueba de aceptación."]
    return "\n".join(lines) + "\n"


def summary(result):
    out = {}

    def descendants(p):
        yield p
        for c in p.get("children", []):
            yield from descendants(c)

    for game, g in result["games"].items():
        v = dict(boot=g["boot"], afs_entries=sum(a["entries"] for a in g["afs"]),
                 boot_bin=[dict(path=p, bytes=s) for p, (_, s) in g["files"].items() if p.endswith("/BOOT.BIN")])
        if game == "b1":
            v.update(records=len(g["records"]), rounds=len(g["satan_rounds"]),
                     own_clips=sum(len(r["resources"]["amm"]["clips"]) for r in g["records"]),
                     spx_slots=sum(len(r["resources"]["spx"]["slots"]) for r in g["records"]))
        else:
            unique = {}
            origins = collections.defaultdict(list)
            for c in g["characters"]:
                packages = [("BC", c["package"]), ("BSP", c["bsp"])]
                packages += [(p["name"], p) for p in c["related"]]
                for role, package in packages:
                    for p in descendants(package):
                        if "conversion_check" in p:
                            unique[p["sha256"]] = p["conversion_check"]
                            origins[p["sha256"]].append(dict(character=c["code"], package=role,
                                                             child=p.get("index")))
            checked = collections.Counter()
            unknown = collections.Counter()
            for p in unique.values():
                checked.update(p["checked"])
                unknown.update(p["unknown"])
            v.update(characters=len(g["characters"]), techs=sum(len(c["techs"]) for c in g["characters"]),
                     dropped_ap7=sum(len(c["ap7"]["dropped"]) for c in g["characters"]),
                      changed_ap7=sum(len(c["ap7"]["changed"]) for c in g["characters"]),
                      unique_compressed_amm=len(unique), animation_checked=dict(checked),
                      animation_unknown_counts=dict(unknown),
                      animation_unknown=[dict(source_sha256=sha, sources=origins[sha], **p)
                                         for sha, p in unique.items() if p["unknown"]])
            c = next(c for c in g["characters"] if c["code"] == "TRX")
            v["TRX"] = dict(techs=[dict(code=hex(t["codigo"]), entry=t["entrada"]) for t in c["techs"]],
                            conversion=c["conversion"],
                            resources=[dict(index=p["index"], magic=p["magic"], bytes=p["bytes"],
                                            clips=len(p.get("clips", [])), slots=p.get("slots", []))
                                       for p in c["package"]["children"]],
                            dropped_ap7_by_class=dict(collections.Counter(x["category"] for x in c["ap7"]["dropped"])))
        out[game] = v
    out["donor_probe"] = result["donor_probe"]
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--salida", type=Path, required=True)
    parser.add_argument("--desde-json", type=Path,
                        help="Proyecta evidencia guardada sin releer ISOs ni repetir la prueba del donante")
    args = parser.parse_args()
    if not args.salida.is_dir():
        parser.error("La carpeta de salida debe existir")
    destinations = [args.salida / name for name in ("censo_medido.json", "censo_medido.md", "resumen_medido.json")]
    if any(p.exists() for p in destinations):
        parser.error("La salida ya existe; use otra carpeta para conservar la evidencia anterior")
    if args.desde_json:
        result = json.loads(args.desde_json.read_text(encoding="utf-8"))
        if result.get("scope") != "original static inventory; no runtime equivalence" or set(result.get("games", {})) != {"b1", "sb1", "sb2"}:
            parser.error("No es un censo solDERX de las tres fuentes")
    else:
        result = dict(scope="original static inventory; no runtime equivalence", games={})
        for game in ("b1", "sb1", "sb2"):
            g = acquire(game)
            result["games"][game] = g
            print(game, "archivos", len(g["files"]), "entradas AFS", sum(a["entries"] for a in g["afs"]),
                  "registros/luchadores", len(g.get("records", g.get("characters", []))), flush=True)
        import afs_pair
        donor_source = Path(afs_pair.PS2_GH)
        donor_file = donor_source if donor_source.is_file() else donor_source / "data_cmn.afs"
        if not donor_file.is_file():
            result["donor_probe"] = "Falta el archivo exigido por afs_pair.ps2: `%s`. No se reintenta el port." % donor_file
        else:
            try:
                sb.donor(8)
                result["donor_probe"] = "Donante Trunks B3 PS2 legible. No acredita equivalencia de guiones."
            except (OSError, ValueError) as ex:
                result["donor_probe"] = "%s: %s" % (type(ex).__name__, ex)
    destinations[0].write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    destinations[1].write_text(markdown(result), encoding="utf-8")
    destinations[2].write_text(json.dumps(summary(result), ensure_ascii=False, indent=2), encoding="utf-8")
    for path in destinations:
        print(path)
    print(result["donor_probe"])


if __name__ == "__main__":
    main()

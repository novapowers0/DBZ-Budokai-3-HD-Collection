r"""referencias.py - INFRA-23: biblioteca de tomas/grabaciones del juego ORIGINAL (PS2 en PCSX2) que ya existen.

  python tools/referencias.py indice          escribe TRABAJO/referencias/INDICE.md, indice.json y FALTAN.md
  python tools/referencias.py busca <texto>   dice si ya hay una toma (todas las palabras, sin mayusculas)
  python tools/referencias.py --autoprueba

Solo lee (nunca borra ni mueve). Carpetas: las que usan pcsx2_pool/pcsx2_turnos/dif_grabar_pool/grabar (lista FUENTES)
mas emu_turnos/registro.jsonl (intentos de grabacion: ok y fallos). Solo biblioteca estandar.
"""
import datetime
import json
import os
import re
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import infra

VIDEO, FOTO, DATO = {".mp4", ".mkv", ".webm", ".avi"}, {".png", ".jpg", ".jpeg"}, {".json", ".tsv", ".jsonl"}
# (carpeta relativa a TRABAJO, profundidad maxima, juego por defecto). Sin recorridos de disco entero.
FUENTES = [("fx_auditoria/orig", 2, ""), ("iw_visual", 1, "iw"), ("video", 1, "b3"), ("dorados_0911", 1, "iw"),
           ("dorados_noche", 1, "iw"), ("B2_FASE2/cuadros", 1, "b2"), ("B2_FASE2/dobles", 1, "b2"),
           ("B2_FASE2/vacios", 1, "b2"), ("B2_FASE2", 1, "b2"), ("A2nave_emu/capturas", 1, "b2"),
           ("A3b_emu/capturas", 1, "b2")]
# Lo que piden los planes (ROADMAP, OLEADA_1_REGISTRO, INFORME_OLEADA_1, _inventario_dorados) y no hay una toma limpia.
PEDIDAS = [
    ("iw", "7d1", "INFORME_OLEADA_1 / IW-M02", "dorado pool2 vacio (154 B) y grabaciones con teclas pendientes"),
    ("iw", "stop cell", "INFORME_OLEADA_1", "grabacion que falta, no hay ninguna toma"),
    ("iw", "7dd", "INFORME_OLEADA_1", "grabacion con teclas que falta (7DD apagado por el usuario)"),
    ("iw", "7d4", "INFORME_OLEADA_1 / IW-FX-03", "7D4 con teclas y referencia del Masenko (solo hay fotos de YouTube)"),
    ("iw", "7d2", "OLEADA_1_REGISTRO (PUL-IW-ANIM 7D2 camara)", "se perdio la toma del original para la cifra por 1000"),
    ("iw", "7d6", "IW-M07", "toma de ~700 cuadros hasta recoger los objetos que faltan"),
    ("b2", "ciudad", "OLEADA_1_REGISTRO B2-P06", "transicion Ciudad: hace falta una toma del original (llegada)"),
    ("b2", "llanura", "OLEADA_1_REGISTRO B2-P06", "transicion Llanura: sin medida, el golpe no llego"),
]


def _mvhd(p):
    """Duracion en s de un mp4 (atomo mvhd), o None. Barato: solo lee la cabecera/cola."""
    try:
        with open(p, "rb") as f:
            n = os.path.getsize(p)
            for ini in (0, max(0, n - (1 << 20))):
                f.seek(ini)
                b = f.read(1 << 20)
                i = b.find(b"mvhd")
                if i < 0:
                    continue
                v = b[i + 4]
                if v == 1:
                    ts, du = struct.unpack(">IQ", b[i + 24:i + 36])
                else:
                    ts, du = struct.unpack(">II", b[i + 16:i + 24])
                return round(du / ts, 1) if ts else None
    except Exception:
        pass
    return None


def _juego(rel, defecto):
    s = rel.lower()
    for k, j in (("b2", "b2"), ("nave", "b2"), ("iw", "iw"), ("0x7d", "iw"), ("0x7e", "iw"), ("masenko", "iw"),
                 ("opening", "b3"), ("ending", "b3")):
        if k in s:
            return j
    return defecto or "?"


def _escena(nombre):
    """Clave de escena: sin extension, sin prefijo orig_/port_, sin sufijos pool/_v/_t1/_frames."""
    s = Path(nombre).stem.lower()
    s = re.sub(r"^(orig|port|hoja|f)_", "", s)
    s = re.sub(r"_(pool\d|v|t\d+(\.\d+)?|frames|nuevo)$", "", s)
    return re.sub(r"_(pool\d|v|t\d+)$", "", s)


def _fuentes(trab):
    for rel, prof, jdef in FUENTES:
        base = trab / rel
        if not base.is_dir():
            continue
        for dp, dn, fn in os.walk(base):
            if Path(dp).relative_to(base).parts.__len__() >= prof:
                dn[:] = []
            for n in fn:
                yield base, Path(dp) / n, jdef
            if Path(dp) != base and Path(dp).name.endswith("_frames"):
                dn[:] = []


def construir(trab):
    items = []
    vistos = set()
    for base, p, jdef in _fuentes(trab):
        if p.parent.name.endswith("_frames"):
            continue                                   # cuadros sueltos de una toma ya indexada
        ext = p.suffix.lower()
        if ext in VIDEO:
            tipo = "video"
        elif ext in FOTO:
            tipo = "foto"
        elif ext in DATO and re.search(r"(iw_0x|b2_fase|toma|orig)", p.name.lower()):
            tipo = "dato"
        else:
            continue
        if p in vistos or (tipo == "video" and p.stem.endswith("_v") and p.name.lower().startswith("port_")):
            continue
        vistos.add(p)
        if p.name.lower().startswith("port_"):
            continue                                   # port_* son capturas de NUESTRO port, no del original
        st = p.stat()
        rel = str(p.relative_to(trab)).replace("\\", "/")
        items.append(dict(ruta=str(p), juego=_juego(rel, jdef), escena=_escena(p.name), tipo=tipo,
                          fecha=datetime.datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d"),
                          dur_s=_mvhd(p) if tipo == "video" else None, bytes=st.st_size))
    # carpetas de cuadros por toma (G02_0x7D7_t1: png sueltos): una entrada por carpeta
    d = trab / "fx_auditoria/orig"
    if d.is_dir():
        for c in sorted(d.iterdir()):
            if c.is_dir():
                items = [i for i in items if not i["ruta"].startswith(str(c) + os.sep)]
                n = [x for x in c.iterdir() if x.is_file()]
                items.append(dict(ruta=str(c), juego=_juego(c.name, "iw"), escena=_escena(c.name), tipo="cuadros",
                                  fecha=datetime.datetime.fromtimestamp(c.stat().st_mtime).strftime("%Y-%m-%d"),
                                  dur_s=None, bytes=sum(x.stat().st_size for x in n), n=len(n)))
    # registro de turnos: grabaciones de PCSX2 (no guarda la ruta de salida: solo nombre, resultado y fecha)
    reg, regs = trab / "emu_turnos" / "registro.jsonl", {}
    if reg.is_file():
        for l in reg.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                e = json.loads(l)
            except ValueError:
                continue
            if e.get("evento") == "grabacion_fin":
                r = regs.setdefault(e["nombre"], dict(ok=0, fallos=0, fecha="", dur_s=0.0, error=""))
                r["ok" if e["ok"] else "fallos"] += 1
                r["fecha"] = e["hora"][:10]
                r["dur_s"] += e.get("dur_s") or 0
                if not e["ok"]:
                    r["error"] = e.get("error", "")[:80]
    for n, r in regs.items():
        items.append(dict(ruta="registro.jsonl", juego=_juego(n, "?"), escena=n.lower(), tipo="registro",
                          fecha=r["fecha"], dur_s=round(r["dur_s"], 1), bytes=0, ok=r["ok"], fallos=r["fallos"],
                          error=r["error"]))
    # duplicados: misma (juego, escena, tipo) en mas de una ruta (los registros se agrupan ya por nombre)
    cuenta = {}
    for i in items:
        if i["tipo"] != "registro":
            cuenta.setdefault((i["juego"], i["escena"], i["tipo"]), []).append(i)
    for v in cuenta.values():
        for i in v:
            i["duplicado"] = len(v) > 1
    return items


def faltan(items):
    out = []
    for n, (j, esc, pide, porque) in enumerate(PEDIDAS):
        hay = [i for i in items if i["juego"] == j and esc.replace(" ", "") in i["escena"].replace(" ", "")
               and i["tipo"] in ("video", "cuadros")]
        out.append((j, esc, pide, porque + (f" (hay {len(hay)} video(s) de la escena)" if hay else " (sin video en el indice)")))
    for i in items:
        if i["tipo"] == "registro" and i["ok"] == 0 and i["fallos"]:
            out.append((i["juego"], i["escena"], "registro.jsonl (intento fallido)",
                        f"{i['fallos']} intento(s), ninguno ok, ultimo {i['fecha']}: {i['error']}"))
    return out


def md(items, falt, trab):
    v = [i for i in items if i["tipo"] != "registro"]
    L = [f"# Indice de referencias del original ({datetime.date.today()})", "",
         f"{len(v)} tomas en disco + {len(items) - len(v)} nombres de grabacion en registro.jsonl. "
         "Duplicado = misma escena y tipo en varias rutas. Generado por `tools/referencias.py indice`.", "",
         "| juego | escena | tipo | fecha | dur s | MB | dup | ruta |", "|---|---|---|---|---|---|---|---|"]
    for i in sorted(items, key=lambda i: (i["juego"], i["escena"], i["tipo"])):
        L.append(f"| {i['juego']} | {i['escena']} | {i['tipo']} | {i['fecha']} | {i['dur_s'] or ''} | "
                 f"{i['bytes'] / 1e6:.1f} | {'SI' if i.get('duplicado') else ''} | "
                 + (f"{i['ok']} ok / {i['fallos']} fallos" if i["tipo"] == "registro" else i["ruta"].replace(str(trab), "~")) + " |")
    f = ["# Tomas del original que faltan (cola nocturna PCSX2)", "",
         "Una linea: juego | escena | quien lo pide | por que falta.", ""]
    f += [f"- {a} | {b} | {c} | {d}" for a, b, c, d in falt]
    return "\n".join(L) + "\n", "\n".join(f) + "\n"


def indice():
    trab = infra.TRABAJO
    items = construir(trab)
    falt = faltan(items)
    out = trab / "referencias"
    out.mkdir(parents=True, exist_ok=True)
    a, b = md(items, falt, trab)
    (out / "INDICE.md").write_text(a, encoding="utf-8")
    (out / "FALTAN.md").write_text(b, encoding="utf-8")
    (out / "indice.json").write_text(json.dumps(items, indent=1, ensure_ascii=False), encoding="utf-8")
    d = sum(1 for i in items if i.get("duplicado"))
    print(f"{len(items)} entradas ({sum(i['tipo'] != 'registro' for i in items)} en disco), {d} duplicadas, {len(falt)} faltan -> {out}")


def busca(texto):
    p = [w.lower() for w in texto.split()]
    f = out = infra.TRABAJO / "referencias" / "indice.json"
    items = json.loads(f.read_text(encoding="utf-8")) if f.is_file() else construir(infra.TRABAJO)
    hit = [i for i in items if all(w in (i["juego"] + " " + i["escena"] + " " + i["ruta"]).lower() for w in p)]
    for i in hit:
        print(f"SI  {i['juego']} {i['escena']} [{i['tipo']}] {i['fecha']} {i['dur_s'] or ''}s  {i['ruta']}")
    if not hit:
        print("NO hay toma de", texto)
    return hit


def autoprueba():
    with tempfile.TemporaryDirectory() as d:
        os.environ["DBZ3_TRABAJO"] = d
        infra._rutas()
        t = Path(d)
        (t / "iw_visual").mkdir()
        (t / "fx_auditoria/orig").mkdir(parents=True)
        for p in ("iw_visual/orig_0x7D6.mp4", "fx_auditoria/orig/orig_0x7D6.mp4", "iw_visual/port_0x7D6.mp4"):
            (t / p).write_bytes(b"\0\0\0\x20ftypmvhd" + bytes(4) + bytes(8) + struct.pack(">II", 1000, 5000) + bytes(20))
        (t / "emu_turnos").mkdir()
        (t / "emu_turnos/registro.jsonl").write_text(
            json.dumps(dict(hora="2026-11-01T00:00:00", evento="grabacion_fin", nombre="b2 fase6 pool2", ok=False, dur_s=1, error="x")) + "\n")
        indice()
        its = json.loads((t / "referencias/indice.json").read_text(encoding="utf-8"))
        v = [i for i in its if i["tipo"] == "video"]
        assert len(v) == 2 and all(i["duplicado"] and i["dur_s"] == 5.0 for i in v), v    # port_ excluido
        assert busca("iw 7d6") and not busca("iw 7dd")
        assert "b2 fase6 pool2" in (t / "referencias/FALTAN.md").read_text(encoding="utf-8")
    print("autoprueba OK")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["--autoprueba"]:
        autoprueba()
    elif a[:1] == ["indice"]:
        indice()
    elif a[:1] == ["busca"] and len(a) > 1:
        busca(" ".join(a[1:]))
    else:
        print(__doc__)

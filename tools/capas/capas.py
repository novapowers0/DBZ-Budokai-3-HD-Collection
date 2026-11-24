#!/usr/bin/env python3
"""Valida borradores de capas sin modificarlos (Python 3.11+, sin dependencias).

Uso: python tools/capas/capas.py [archivo_o_carpeta ...] [--png-root RAIZ]
     python tools/capas/capas.py --self-check

Sin rutas: IW iw_capas/*.toml y los TOML de b2_volcado, si existe.
Salida Markdown por stdout; 0 = completo, 1 = fallos, 2 = cobertura parcial.
No escribe ficheros. Los hashes XXH3 identifican texeles/CLUT crudos, NO los
bytes del contenedor PNG: se comprueba el nombre completo y su cabecera PNG.
Rectángulos [x0,y0,x1,y1), escala desde la esquina superior izquierda y luego
desplazamiento. Cuadros [primero,ultimo] inclusivos; si faltan, tiempo desconocido.
Los rectángulos y cuadros agregados solo permiten señalar solapes potenciales.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
import math
from pathlib import Path
import re
import struct
import sys
import tomllib


IW = Path(r"E:\DBZ3HD_trabajo\iw_volcado\iw_capas")
B2 = Path(r"E:\DBZ3HD_trabajo\b2_volcado")
HASH = re.compile(r"[0-9a-fA-F]{16}(?:_[0-9a-fA-F]{16})?")
TABLE = re.compile(r"^\s*\[\[\s*elemento\s*\]\]\s*(?:#.*)?$")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
SOURCES = {"original", "port", "mod"}


@dataclass
class Issue:
    level: str
    where: str
    message: str


@dataclass
class Element:
    identity: str
    source: str
    where: str
    rect: tuple | None
    frames: tuple | None


@dataclass
class Draft:
    path: Path
    key: str
    rows: list
    lines: list[int]


def forbidden(path):
    """No abrir configuración privada, credenciales ni includes locales."""
    parts = [p.casefold() for p in path.parts]
    name = path.name.casefold()
    return (name.endswith(".local.inc") or name == ".mcp.json"
            or name.startswith((".env", "credentials", "credenciales"))
            or (".claude" in parts and name.startswith("settings")))


def numbers(value, count):
    return (isinstance(value, list) and len(value) == count
            and all(type(n) in (int, float) and math.isfinite(n) for n in value))


def parse_draft(text, path):
    data = tomllib.loads(text)
    key = data.get("clave")
    if not isinstance(key, str) or not key.strip():
        raise ValueError('falta clave (texto no vacío)')
    rows = data.get("elemento")
    if not isinstance(rows, list) or any(not isinstance(r, dict) for r in rows):
        raise ValueError('se requiere una lista [[elemento]]')
    # Los borradores usan cabeceras explícitas. Rechazar formatos alternativos
    # evita atribuir líneas inventadas a estructuras TOML inline o anidadas.
    lines = [i for i, line in enumerate(text.splitlines(), 1) if TABLE.fullmatch(line)]
    if len(lines) != len(rows):
        raise ValueError('use una cabecera [[elemento]] explícita por registro')
    return Draft(path, key.strip(), rows, lines)


def transformed_rect(row):
    rect = row.get("rect")
    if rect is None:
        return None
    if not numbers(rect, 4) or rect[2] < rect[0] or rect[3] < rect[1]:
        raise ValueError('rect debe ser [x0,y0,x1,y1] finito y ordenado')
    shift = row.get("desplazamiento", [0, 0])
    scale = row.get("escala", 1.0)
    if not numbers(shift, 2):
        raise ValueError('desplazamiento debe contener dos números finitos')
    if type(scale) not in (int, float) or not math.isfinite(scale) or scale <= 0:
        raise ValueError('escala debe ser finita y positiva')
    x, y = rect[0] + shift[0], rect[1] + shift[1]
    result = (x, y, x + (rect[2] - rect[0]) * scale,
              y + (rect[3] - rect[1]) * scale)
    if not all(math.isfinite(n) for n in result):
        raise ValueError('rect transformado fuera de rango')
    return result


def frame_range(row):
    value = row.get("cuadros")
    if value is None:
        return None
    if (not isinstance(value, list) or len(value) != 2
            or any(type(n) is not int or n < 0 for n in value) or value[0] > value[1]):
        raise ValueError('cuadros debe ser [primero,ultimo], enteros no negativos ordenados')
    return tuple(value)


def intersection(a, b):
    rect = (max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3]))
    return rect if rect[2] > rect[0] and rect[3] > rect[1] else None


def shared_frames(a, b):
    if a is None or b is None:
        return "desconocidos"
    lo, hi = max(a[0], b[0]), min(a[1], b[1])
    return f"{lo}..{hi}" if lo <= hi else None


def normalized_id(identity):
    return identity.upper() if HASH.fullmatch(identity) else identity


def validate(drafts, pngs):
    """pngs: hash completo -> [(ruta, cabecera PNG válida), ...]."""
    issues, elements, seen = [], [], {}
    checks = 0
    for draft in drafts:
        if not draft.rows:
            issues.append(Issue("PARCIAL", f"{draft.path}:1", "tabla sin elementos"))
        for row, line in zip(draft.rows, draft.lines):
            where = f"{draft.path}:{line}"
            identity, source = row.get("id"), row.get("fuente")
            if not isinstance(identity, str) or not identity.strip():
                issues.append(Issue("FALLO", where, "id ausente o vacío"))
                continue
            if not isinstance(source, str) or source not in SOURCES:
                issues.append(Issue("FALLO", where, f"{identity}: fuente debe ser original, port o mod (una sola)"))
                continue
            token = normalized_id(identity)
            if token in seen:
                previous = seen[token]
                kind = "dos fuentes" if previous.source != source else "registro duplicado"
                issues.append(Issue("FALLO", where, f"{identity}: {kind}; anterior {previous.where} ({previous.source})"))
            try:
                rect, frames = transformed_rect(row), frame_range(row)
                if rect is not None and (rect[0] == rect[2] or rect[1] == rect[3]):
                    issues.append(Issue("PARCIAL", where,
                                        f"{identity}: rect sin área {rect}; captura degenerada, no prueba de fallo del juego"))
            except ValueError as exc:
                issues.append(Issue("FALLO", where, f"{identity}: {exc}"))
                rect, frames = None, None
            element = Element(identity, source, where, rect, frames)
            seen.setdefault(token, element)
            elements.append(element)
            texture_hash = row.get("hash")
            if texture_hash is None and HASH.fullmatch(identity):
                texture_hash = identity
            if texture_hash is not None:
                checks += 1
                if not isinstance(texture_hash, str) or not HASH.fullmatch(texture_hash):
                    issues.append(Issue("FALLO", where, f"{identity}: hash no válido (16 hex o 16hex_16hex)"))
                else:
                    matches = pngs.get(texture_hash.upper(), [])
                    if not matches:
                        issues.append(Issue("FALLO", where, f"{identity}: no existe PNG para el hash completo {texture_hash}"))
                    elif not any(valid for _, valid in matches):
                        issues.append(Issue("FALLO", where, f"{identity}: PNG ilegible o cabecera inválida: {', '.join(str(p) for p, _ in matches)}"))
            elif "tex0" in row:
                issues.append(Issue("PARCIAL", where, f"{identity}: tex0 sin hash comprobable"))
    originals = [e for e in elements if e.source == "original"]
    ports = [e for e in elements if e.source == "port"]
    if not originals or not ports:
        issues.append(Issue("PARCIAL", f"{drafts[0].path}:1", "no están representadas ambas fuentes original/port"))
    overlaps = []
    for element in elements:
        opposite = ports if element.source == "original" else originals if element.source == "port" else []
        if opposite and element.rect is None:
            issues.append(Issue("PARCIAL", element.where, f"{element.identity}: sin rect; cruce original/port no comprobable"))
    for original in originals:
        for port in ports:
            if original.rect is None or port.rect is None:
                continue
            rect = intersection(original.rect, port.rect)
            frames = shared_frames(original.frames, port.frames)
            if rect is not None and frames is not None:
                overlaps.append((original, port, rect, frames))
                issues.append(Issue("FALLO", original.where,
                                    f"solape potencial {original.identity} / {port.identity} ({port.where}); "
                                    f"rect={rect}, cuadros={frames}"))
    return elements, checks, overlaps, issues


def png_index(drafts, root):
    """Índice por minijuego: no aceptar el PNG de otro volcado por accidente."""
    directories = set()
    for draft in drafts:
        key = draft.key.removeprefix("0x").removeprefix("0X")
        directories.update((root / key / "tex", root / draft.key / "tex",
                            draft.path.parent / key / "tex", draft.path.parent / "tex"))
    result = defaultdict(list)
    for directory in sorted(directories):
        if not directory.is_dir():
            continue
        for path in sorted(directory.iterdir()):
            if path.suffix.lower() != ".png" or not HASH.fullmatch(path.stem) or not path.is_file():
                continue
            try:
                with path.open("rb") as stream:
                    head = stream.read(33)
                valid = (len(head) == 33 and head[:8] == PNG_SIGNATURE
                         and head[8:16] == b"\x00\x00\x00\rIHDR"
                         and all(struct.unpack(">II", head[16:24])))
            except OSError:
                valid = False
            result[path.stem.upper()].append((path, valid))
    return result


def safe_files(root):
    """Recorrer nombres sin entrar en carpetas privadas ni seguir enlaces."""
    import os
    for parent, directories, names in os.walk(root, followlinks=False):
        directories[:] = sorted(d for d in directories if not d.startswith(".")
                                and not (Path(parent) / d).is_symlink())
        for name in sorted(names):
            path = Path(parent) / name
            if path.suffix.lower() == ".toml" and not forbidden(path) and not path.is_symlink():
                yield path


def cell(value):
    return str(value).replace("|", "\\|").replace("\n", " ").replace("\r", " ")


def run(inputs, png_root=None):
    groups = defaultdict(list)
    global_issues, scope = [], []
    for root in inputs:
        if forbidden(root):
            global_issues.append(Issue("FALLO", str(root), "ruta privada rechazada sin leer"))
            continue
        if not root.exists():
            scope.append(f"{root}: no existe; sin cobertura")
            global_issues.append(Issue("PARCIAL", str(root), "ruta ausente; sin línea de archivo aplicable"))
            continue
        files = [root] if root.is_file() else list(safe_files(root))
        scope.append(f"{root}: {len(files)} borradores")
        if not files:
            global_issues.append(Issue("PARCIAL", str(root), "sin borradores TOML; no valida capas (sin línea aplicable)"))
        for path in files:
            try:
                draft = parse_draft(path.read_text(encoding="utf-8-sig"), path)
                base = root.parent if root.is_file() else root
                if "capas" in base.name.lower():
                    base = base.parent
                groups[(base.resolve(), draft.key)].append(draft)
            except (OSError, UnicodeError, ValueError) as exc:
                match = re.search(r"at line (\d+)", str(exc))
                line = match.group(1) if match else "1"
                global_issues.append(Issue("FALLO", f"{path}:{line}", f"no se pudo leer el borrador: {exc}"))
    output = ["# Validación de capas", "", "## Alcance", *[f"- {s}" for s in scope], "",
              "Hashes: coincidencia completa de nombre + cabecera PNG; no recálculo de XXH3 de texeles/CLUT.",
              "Solapes: cajas transformadas y rangos inclusivos de cuadros; son candidatos, no prueba de doble dibujo.",
              "Sin rectángulos del port el resultado es PARCIAL, nunca una certificación sin solapes.", "",
              "| Minijuego | Elementos | Original | Port | Mod | Hashes | Solapes potenciales | Estado |",
              "|---|---:|---:|---:|---:|---:|---:|---|"]
    details, all_issues = [], list(global_issues)
    for (base, key), drafts in sorted(groups.items(), key=lambda item: (str(item[0][0]), item[0][1])):
        pngs = png_index(drafts, png_root or base)
        elements, checks, overlaps, issues = validate(drafts, pngs)
        counts = Counter(e.source for e in elements)
        state = "FALLO" if any(i.level == "FALLO" for i in issues) else "PARCIAL" if issues else "PASS"
        output.append(f"| {cell(key)} | {len(elements)} | {counts['original']} | {counts['port']} | {counts['mod']} | {checks} | {len(overlaps)} | {state} |")
        details.extend(["", f"## {cell(key)} — {state}",
                        *[f"- Borrador: `{d.path}:1`" for d in drafts],
                        f"- PNG indexados por nombre completo: {len(pngs)}.",
                        f"- Cruces original/port detectados: {len(overlaps)}."])
        details.extend(f"- **{i.level}** `{i.where}`: {i.message}" for i in issues)
        all_issues.extend(issues)
    output.extend(details)
    if global_issues:
        output.extend(["", "## Incidencias de lectura / cobertura"])
        output.extend(f"- **{i.level}** `{i.where}`: {i.message}" for i in global_issues)
    counts = Counter(i.level for i in all_issues)
    code = 1 if counts["FALLO"] else 2 if counts["PARCIAL"] else 0
    output.extend(["", f"Resultado: {counts['FALLO']} fallos, {counts['PARCIAL']} pendientes de cobertura. Código {code}."])
    return "\n".join(output), code


def self_check():
    """Casos en memoria: no crea PNG/TOML, cachés ni directorios temporales."""
    if not __debug__:
        raise RuntimeError("Autocomprobación requiere assert activos; no use python -O")
    h = "0123456789ABCDEF_FEDCBA9876543210"
    pngs = {h: [(Path(h + ".png"), True)]}

    def check(rows, index=None):
        draft = Draft(Path("caso.toml"), "0x7D5", rows, list(range(5, 5 + len(rows))))
        return validate([draft], pngs if index is None else index)

    a = dict(id=h, fuente="original", rect=[0, 0, 10, 10], cuadros=[0, 3])
    b = dict(id="actor", fuente="port", rect=[10, 0, 20, 10], cuadros=[0, 3])
    assert check([a, b])[3] == []  # tocar borde no solapa
    assert len(check([a, dict(b, rect=[9, 0, 20, 10])])[2]) == 1
    assert not check([a, dict(b, rect=[0, 0, 10, 10], cuadros=[4, 5])])[2]
    assert check([a, dict(b, rect=[0, 0, 10, 10], cuadros=[3, 5])])[2]
    assert check([a, dict(b, rect=[0, 0, 10, 10], cuadros=None)])[2][0][3] == "desconocidos"
    assert transformed_rect(dict(rect=[2, 3, 4, 5], escala=2, desplazamiento=[-2, 1])) == (0, 4, 4, 8)
    assert check([a, dict(b, desplazamiento=[-1, 0])])[2]
    assert check([dict(a, escala=2), b])[2]
    assert any("dos fuentes" in i.message for i in check([a, dict(b, id=h)])[3])
    assert any("duplicado" in i.message for i in check([a, a, b])[3])
    assert any("dos fuentes" in i.message for i in check([a, dict(b, id=h.lower())])[3])
    assert any("no existe PNG" in i.message for i in check([a, b], {})[3])
    assert any("cabecera inválida" in i.message for i in check([a, b], {h: [(Path("roto.png"), False)]})[3])
    assert any("no existe PNG" in i.message for i in check([a, b], {h[:16]: [(Path("incompleto.png"), True)]})[3])
    assert any(i.level == "PARCIAL" for i in check([a, dict(id="actor", fuente="port")])[3])
    assert any("rect sin área" in i.message for i in check([dict(a, rect=[0, 0, 0, 1]), b])[3])
    assert not check([a, dict(b, fuente="mod")])[2]  # solo cruce original/port
    for changes in (dict(fuente=["original", "port"]), dict(rect=[1, 0, 0, 1]),
                    dict(rect=[False, 0, 1, 1]), dict(escala=float("nan")),
                    dict(cuadros=[4, 3]), dict(cuadros=[True, 3]), dict(hash="incorrecto")):
        assert any(i.level == "FALLO" for i in check([dict(a, **changes), b])[3]), changes
    parsed = parse_draft('clave="x"\n\n[[elemento]] # uno\nid="actor"\nfuente="port"', Path("x.toml"))
    assert parsed.lines == [3] and parsed.rows[0]["id"] == "actor"
    try:
        parse_draft('clave="x"\nclave="y"', Path("x.toml"))
    except tomllib.TOMLDecodeError:
        pass
    else:
        raise AssertionError("TOML duplicado aceptado")
    assert forbidden(Path(".claude/settings.local.json"))
    assert forbidden(Path("clave.local.inc"))
    # Integración del informe y sus códigos, usando un sistema de archivos
    # simulado: los únicos ficheros reales leídos al usar la herramienta son
    # los borradores y las cabeceras PNG solicitados.
    from unittest.mock import patch
    text = ('clave="0x7D5"\n[[elemento]]\nid="funcion_1:orden_2"\n'
            'fuente="original"\nrect=[0,0,10,10]\ncuadros=[0,2]\n'
            '[[elemento]]\nid="actor"\nfuente="port"\nrect=[10,0,20,10]\ncuadros=[0,2]\n')
    with patch.object(Path, "exists", return_value=True), \
            patch.object(Path, "is_file", return_value=True), \
            patch.object(Path, "read_text", return_value=text), \
            patch(__name__ + ".png_index", return_value={}):
        report, code = run([Path("simulado.toml")])
        assert code == 0 and "| 0x7D5 | 2 | 1 | 1 | 0 | 0 | 0 | PASS |" in report
        with patch.object(Path, "read_text", return_value=text.replace("[10,0,20,10]", "[9,0,20,10]")):
            report, code = run([Path("simulado.toml")])
            assert code == 1 and "solape potencial" in report and "simulado.toml:7" in report
        with patch.object(Path, "read_text", return_value=text.replace("rect=[10,0,20,10]\n", "")):
            assert run([Path("simulado.toml")])[1] == 2
        assert run([Path("uno.toml"), Path("dos.toml")])[1] == 1
        with patch.object(Path, "read_text", return_value='clave="x"\nclave="y"'):
            assert run([Path("roto.toml")])[1] == 1
    with patch.object(Path, "exists", return_value=False):
        assert run([Path("no_existe")])[1] == 2
    print("PASS: autocomprobación (assert), sin escrituras.")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rutas", nargs="*", type=Path, help="TOML o carpetas con borradores")
    parser.add_argument("--png-root", type=Path, help="raíz alternativa: <raíz>/<clave>/tex/*.png")
    parser.add_argument("--self-check", action="store_true", help="solo autocomprobación en memoria")
    args = parser.parse_args()
    if args.self_check:
        self_check()
        return 0
    report, code = run(args.rutas or [IW, B2], args.png_root)
    print(report)
    return code


if __name__ == "__main__":
    sys.exit(main())

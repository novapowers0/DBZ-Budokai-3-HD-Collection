"""Validar logs ya grabados por Z. No abre juegos ni modifica sus archivos.

python -B src/net/f10_validate_runs.py --host-log <log> --guest-log <log> --fights 50
Los checks de logs no certifican la interfaz, el allocator ni una red fisica.
"""
import argparse
from pathlib import Path
import re

SUMMARY = re.compile(r"dbz3 rb: resumen del combate (\d+): (\d+) ticks, (\d+) rollbacks .*?sumas (\d+) / malas (\d+)")
BAD = re.compile(r"SUMA distinta|DESINCRONIA|reparacion aplicada|F10: corte|UNREGISTERED|0xC0000005|0x80000003|access violation|"
                 r"(?:ORACULO|PURE) tick [^\r\n]*\([0-9]+ pruebas, [1-9][0-9]* malas\)", re.I)


def validate(host, guest, fights=50, min_ticks=30, min_sums=20, require_rollback=False):
    """Numerador por combate y lado; rechaza omisiones, duplicados y cualquier diferencia."""
    passed = 0
    total = 2 * fights + 4
    first = None
    counts = {}
    def check(case, observed, expected):
        nonlocal passed, first
        if observed == expected:
            passed += 1
        elif first is None:
            first = dict(case=case, observed=observed, expected=expected)
    for name, text in (("host", host), ("guest", guest)):
        rows = [tuple(map(int, r)) for r in SUMMARY.findall(text)]
        counts[name] = len(rows)
        errors = BAD.search(text)
        check(name + ": log sin divergencia/excepcion", errors.group(0) if errors else None, None)
        check(name + ": numero de resumenes", len(rows), fights)
        for fight in range(1, fights + 1):
            found = [r for r in rows if r[0] == fight]
            acceptable = (len(found) == 1 and found[0][1] >= min_ticks
                          and found[0][3] >= min_sums and found[0][4] == 0
                          and (not require_rollback or any(r[2] > 0 for r in rows)))
            check(f"{name}: combate {fight}", True if acceptable else found, True)
    return dict(passed=passed, total=total, first_difference=first, summaries=counts,
                fights_per_side=fights, min_ticks=min_ticks, min_sums=min_sums,
                require_rollback=require_rollback)


def main():
    import json
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host-log", required=True, type=Path)
    ap.add_argument("--guest-log", required=True, type=Path)
    ap.add_argument("--fights", type=int, default=50)
    ap.add_argument("--min-ticks", type=int, default=30)
    ap.add_argument("--min-sums", type=int, default=20)
    ap.add_argument("--require-rollback", action="store_true")
    ap.add_argument("--output", type=Path, help="Guardar JSON en un archivo nuevo (sin sobrescribir)")
    a = ap.parse_args()
    if a.fights < 1 or a.min_ticks < 1 or a.min_sums < 1:
        ap.error("Los limites deben ser positivos")
    if a.host_log.resolve() == a.guest_log.resolve():
        ap.error("Se necesitan dos archivos distintos: un log de cada lado")
    result = validate(a.host_log.read_text(encoding="utf-8-sig", errors="replace"),
                      a.guest_log.read_text(encoding="utf-8-sig", errors="replace"),
                      a.fights, a.min_ticks, a.min_sums, a.require_rollback)
    report = json.dumps(result, indent=2, ensure_ascii=False)
    if a.output:
        with a.output.open("x", encoding="utf-8") as stream:
            stream.write(report + "\n")
    print(report)
    return int(result["passed"] != result["total"])


if __name__ == "__main__":
    raise SystemExit(main())

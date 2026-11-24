"""Consolidar la evidencia existente de captura05. No abre juegos ni modifica originales."""
import hashlib
import json
from pathlib import Path
import re
import shutil

from f10_validate_runs import validate


ROOT = Path(__file__).resolve().parents[2]
CAPTURE = Path("E:/DBZ3HD_trabajo/f10-online/ronda6_captura_05")
RUNS = Path("D:/DBZ3HD/_pruebas")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def mask(data, source):
    """Proyeccion de las exclusiones actuales de ComputeBuf, sin inferir tipos nuevos."""
    out = bytearray(data)
    for name, starts in (("kMaskMgr", [0]), ("kMaskFighter", [2096, 2096 + 8240])):
        declaration = re.search(r"constexpr MaskRange " + name + r"\[\] = (.*?);", source).group(1)
        ranges = re.findall(r"\{(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+)\}", declaration)
        assert ranges, name
        for start in starts:
            for offset, length in ranges:
                a, n = start + int(offset, 0), int(length, 0)
                out[a:a+n] = bytes(n)
    for f in range(2):
        start = 2096 + f * 8240
        for i in range(start, start + 8240, 4):
            if out[i] == 0x4E or (out[i] == 0x4D and out[i+1] >= 0xF0):
                out[i:i+4] = b"\0\0\0\1"
        for i in range(start + 0x1E20, start + 0x1E70, 4):
            if out[i] == 0x40 and out[i+1] & 0xF0 == 0x10 and not out[i+3] & 3:
                out[i:i+4] = b"\0\0\0\1"
        for k in range(16):
            i = start + 0x1B0C + 32*k
            if out[i:i+4] != b"\0\0\0\1":
                out[i:i+4] = bytes(4)
    return out


def main():
    logs = [RUNS / f"20261111-010331_rb_{c}_05_primera_diferencia/dbz3.log" for c in "ab"]
    texts = [p.read_text(encoding="utf-8-sig", errors="replace") for p in logs]
    result = validate(*texts, fights=25, min_ticks=4000, require_rollback=True)
    assert (result["passed"], result["total"]) == (44, 54), result
    dumps = [CAPTURE / f"datos/dump_{role}_bad5370.bin" for role in ("host", "guest")]
    raw = [p.read_bytes() for p in dumps]
    assert all(len(d) == 35200 for d in raw)
    checksum_source = ROOT / "src/net/rollback.h"
    source = checksum_source.read_text(encoding="utf-8-sig")
    normal = [mask(d, source) for d in raw]
    differing = [i for i, (a, b) in enumerate(zip(*raw)) if a != b]
    unmasked = [i for i, (a, b) in enumerate(zip(*normal)) if a != b]
    target = 2096 + 8240 + 6832
    assert len(differing) == 14 and unmasked and all(target <= i < target+4 for i in unmasked)
    a, b = (int.from_bytes(d[target:target+4], "big") for d in raw)
    words = []
    for i in sorted({i & ~3 for i in differing}):
        words.append(dict(offset=i, host=raw[0][i:i+4].hex(), guest=raw[1][i:i+4].hex(),
                          masked_equal=normal[0][i:i+4] == normal[1][i:i+4]))
    report_paths = [Path(f"D:/DBZ3HD/rb_{c}/informes_online/desincronia_20261111_015212.txt") for c in "ab"]
    evidence = dict(validation=result, first_bad_global_tick=173118, first_bad_relative_tick=5370,
                    mask="00100", equal_sums_before=117470, raw_different_bytes=len(differing),
                    unmasked_different_offsets=unmasked, words=words,
                    task_pointer=dict(fighter=1, offset="0x1AB0", host=f"{a:08X}", guest=f"{b:08X}",
                                      address_delta=b-a, slots_if_stride112=(b-a)//112),
                    conclusion="La unica palabra distinta tras las exclusiones existentes es la direccion de tarea. Faltan contenidos para decidir equivalencia semantica; cierre 17840 sin causa/arreglo demostrado.",
                    sources=[dict(path=str(p), sha256=digest(p)) for p in logs + dumps + report_paths + [checksum_source]])
    out = CAPTURE / "revision_solF10"
    out.mkdir(exist_ok=False)
    (out / "validacion.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    (out / "primera_diferencia.json").write_text(json.dumps(evidence, indent=2, ensure_ascii=False), encoding="utf-8")
    for role, path in zip(("host", "guest"), report_paths):
        shutil.copy2(path, out / f"{role}_{path.name}")
    print(f"Evidencia guardada: {out}; validacion 44/54; 14 bytes brutos, {len(unmasked)} fuera de mascara, solo luch1+0x1AB0")


if __name__ == "__main__":
    main()

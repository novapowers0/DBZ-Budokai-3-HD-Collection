"""Contraste acotado de las solicitudes de objetos BBR contra instrucciones EE.

No ejecuta el juego. Intercepta los servicios externos de los tres updates;
los saltos, umbrales, dificultad, argumentos y latch se ejecutan desde el ELF.
No acredita trayectoria, dibujo, sonido ni colision de los objetos creados.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
from types import SimpleNamespace

sys.dont_write_bytecode = True
ELF_SHA = "afe8c7b99f31e635750a17618ae8248d1a64fe3ad0669996990abdf33e1819f4"
FUNCIONES = {7: (0x318E00, 0x3190F0), 8: (0x318A60, 0x318CB0),
             9: (0x318740, 0x318910)}


def cargar_elf(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != ELF_SHA:
        raise ValueError("ELF IW EU no reconocido")
    return data


def memoria_elf(data, addr, size):
    ph = struct.unpack_from("<I", data, 28)[0]
    stride, count = struct.unpack_from("<HH", data, 42)
    for i in range(count):
        typ, off, va, _, n, *_ = struct.unpack_from("<8I", data, ph+i*stride)
        if typ == 1 and va <= addr and addr+size <= va+n:
            return data[off+addr-va:off+addr-va+size]
    raise ValueError(f"rango ELF no mapeado: {addr:X}/{size}")


def llamadas(data, lo, hi):
    out = []
    for pc in range(lo, hi, 4):
        w, = struct.unpack("<I", memoria_elf(data, pc, 4))
        if w >> 26 == 3:
            out.append({"pc": hex(pc), "destino": hex((w & 0x3FFFFFF) << 2)})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", type=Path, required=True)
    ap.add_argument("--inventario", action="store_true")
    ap.add_argument("--oraculo", type=Path)
    ap.add_argument("--salida", type=Path)
    ap.add_argument("--cpp", type=Path)
    ap.add_argument("--esperar-diferencias", action="store_true")
    args = ap.parse_args()
    data = cargar_elf(args.elf)
    inventory = {str(k): {"inicio": hex(lo), "fin_exclusivo": hex(hi),
                          "sha256": hashlib.sha256(memoria_elf(data, lo, hi-lo)).hexdigest(),
                          "llamadas": llamadas(data, lo, hi)}
                 for k, (lo, hi) in FUNCIONES.items()}
    if args.inventario:
        print(json.dumps({"elf_sha256": ELF_SHA, "updates": inventory}, indent=2))
        return
    if not args.oraculo or not args.salida:
        ap.error("el contraste requiere --oraculo y --salida")
    spec = importlib.util.spec_from_file_location("iwx_eventos_oraculo", args.oraculo)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    actor, work, mini, target = 0x800000, 0x810000, 0x820000, 0x830000
    hooks = {0x29A5D0, 0x28A9D0, 0x238640, 0x299BC0, 0x3177F0}

    class EventosEE(oracle.EE):
        def __init__(self):
            super().__init__(SimpleNamespace(elf=data))
            self.eventos = []

        def step(self, pc, in_delay=False):
            result = super().step(pc, in_delay)
            w = self.get(pc)
            if w >> 26 == 3 and ((w & 0x3FFFFFF) << 2) in hooks:
                return result[0], result[1], True
            return result

        def hook(self, addr):
            if addr == 0x29A5D0:
                self.r[2] = work-0x260
            elif addr == 0x28A9D0:
                self.r[2] = mini
            elif addr == 0x3177F0:
                if self.r[4] != actor:
                    raise AssertionError("actor distinto en llamada original")
                self.eventos.append({"tipo": oracle.S(self.r[5]),
                                     "desvio_bits": self.f[12]})
            elif addr in (0x238640, 0x299BC0):
                pass  # reloj ya publicado; rotacion externa fuera de este contrato.
            else:
                super().hook(addr)

    args.salida.mkdir(parents=False, exist_ok=False)
    cases = []
    for state, threshold in ((7, 291.), (8, 36.), (9, 13.)):
        bits = oracle.U(threshold)
        for difficulty in range(7):
            for frame in (oracle.V(bits-1), threshold, oracle.V(bits+1), threshold+1.2):
                for latch in (0, 1):
                    ee = EventosEE()
                    ee.r[4] = actor
                    ee.put(actor+0x1380, 1)
                    ee.put(actor+0x1384, target)
                    ee.put(actor+0xCBC, state, 2)
                    ee.put(actor+0xC74, 0)
                    ee.pf(actor+0xC68, frame)
                    ee.pv(actor+0x640, [0., 0., 0., 1.])
                    ee.pv(target+0x640, [1000., 0., 0., 1.])
                    ee.pv(work+0x50, [-1., 0., 0., 0.])
                    ee.put(work+0x224, latch)
                    ee.put(mini+0xC, difficulty, 2)
                    instructions = ee.run(FUNCIONES[state][0])
                    first = ee.eventos[:]
                    # Segunda pasada con el mismo reloj: el latch evita duplicados.
                    ee.r[4] = actor
                    instructions += ee.run(FUNCIONES[state][0])
                    if ee.eventos != first:
                        raise AssertionError("el original repitio el evento sin reentrada")
                    cases.append({"estado": state, "dificultad": difficulty,
                                  "cuadro_bits": oracle.U(frame), "latch_inicial": latch,
                                  "latch_final": ee.get(work+0x224), "eventos": first,
                                  "instrucciones": instructions})
    fixture = args.salida/"original.bin"
    with fixture.open("xb") as f:
        f.write(b"BBEVT001"+struct.pack("<I", len(cases)))
        for case in cases:
            f.write(struct.pack("<5I", *(case[k] for k in
                         ("estado", "dificultad", "cuadro_bits", "latch_inicial", "latch_final"))))
            f.write(struct.pack("<I", len(case["eventos"])))
            for event in case["eventos"]:
                f.write(struct.pack("<iI", event["tipo"], event["desvio_bits"]))
    report = {"elf_sha256": ELF_SHA, "oraculo_sha256": hashlib.sha256(args.oraculo.read_bytes()).hexdigest(),
              "updates": inventory, "casos": cases, "entrada": "sintetica posterior a AnimationFramesLink",
              "servicios_interceptados": [hex(x) for x in sorted(hooks)],
              "alcance": "solo llamadas/umbrales/dificultad/latch; no objetos en vuelo ni imagen ni sonido",
              "normal": False}
    if args.cpp:
        run = subprocess.run([str(args.cpp), str(fixture)], capture_output=True, text=True, check=False)
        report["cpp"] = {"codigo": run.returncode, "stdout": run.stdout, "stderr": run.stderr,
                         "exe_sha256": hashlib.sha256(args.cpp.read_bytes()).hexdigest()}
        expected = 1 if args.esperar_diferencias else 0
        report["pasa"] = run.returncode == expected
    else:
        report["pasa"] = True
    (args.salida/"resultado.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"casos_originales": len(cases), "pasa": report["pasa"],
                      "cpp": report.get("cpp"), "salida": str(args.salida), "normal": False}))
    raise SystemExit(0 if report["pasa"] else 1)


if __name__ == "__main__":
    main()

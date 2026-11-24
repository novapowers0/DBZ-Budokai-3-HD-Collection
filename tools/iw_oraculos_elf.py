#!/usr/bin/env python3
"""Infinite World: oraculos offline de los ELF EU (SLES_553.47) y USA (SLUS_218.42).

Comprueba sin abrir el juego ni PCSX2 cuatro reglas cerradas en AP_NIWL:
- vuelo_obj8: prioridades Task_Create (0x9000 < 0x9003 < 0x9025) y retardo 33 PAL / 39 NTSC.
- hito_space: s_SPACE EU 0x31F280 / USA 0x31EF90 en 0x7D0 y 0x7D9 (0x16BC: 2 -> 0 -> 2, pose 0x2E, 0x299900).
- rango_combate: Char_ComputeStatGrade EU 0x2AC0D0 / USA 0x2ABBE0 (Fight 39->0 / 40->1 y notas 1..4).
- disparo_rango: RangoDisparo 0x7D7/0x7D8/0x7DE (303 casos, 0 DIF EU-vs-USA y 0 DIF ELF-vs-C++).

Reutiliza tools/iw_mips/ee.py y tools/iw_mips/disparo_banco.py.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys

RAIZ = Path(__file__).resolve().parent.parent
IW_MIPS = RAIZ / "tools" / "iw_mips"
if str(IW_MIPS) not in sys.path:
    sys.path.insert(0, str(IW_MIPS))

import ee  # noqa: E402
import disparo_banco as banco  # noqa: E402


def _buscar_cmn() -> Path:
    for p in (
        Path(r"D:\DBZ3HD\ps2_games\Infinite World (USA)\USR\DATA_CMN.AFS"),
        RAIZ / "ps2_games" / "Infinite World (USA)" / "USR" / "DATA_CMN.AFS",
    ):
        if p.is_file():
            return p
    raise FileNotFoundError("No se encontro DATA_CMN.AFS de Infinite World")


def _leer_afs(afs_p: Path, idx: int) -> bytes:
    with afs_p.open("rb") as f:
        f.seek(8 + idx * 8)
        off, sz = struct.unpack("<II", f.read(8))
        f.seek(off)
        return f.read(sz)


def _extraer_itm(pack_bytes: bytes) -> bytes:
    n = struct.unpack_from("<I", pack_bytes, 0x10)[0]
    tbl = struct.unpack_from("<I", pack_bytes, 0x14)[0]
    for i in range(n):
        co, cs = struct.unpack_from("<II", pack_bytes, tbl + i * 16)
        if cs >= 4 and pack_bytes[co : co + 4] == b"#ITM":
            return pack_bytes[co : co + cs]
    raise RuntimeError("No se encontro #ITM en paquete")


def oraculo_vuelo_obj8(mem_eu: bytes, mem_us: bytes) -> dict:
    """Prueba prioridades Task_Create (0x9000 < 0x9003 < 0x9025) y retardo 33 PAL / 39 NTSC."""
    def scan_prios(mem: bytes, a: int, b: int) -> list[int]:
        found = []
        for pc in range(a, b, 4):
            w = struct.unpack_from("<I", mem, pc)[0]
            if (w >> 26) == 3 and ((w & 0x3FFFFFF) << 2) == 0x140300:
                for p2 in range(pc - 32, pc, 4):
                    w2 = struct.unpack_from("<I", mem, p2)[0]
                    s2 = str(ee.RB.Instruction(w2, p2, ee.RB.InstrCategory.R5900))
                    if "$a2" in s2 and ("ori" in s2 or "addiu" in s2):
                        v = int(s2.split(",")[-1].strip(), 0)
                        if 0x9000 <= v <= 0x9030:
                            found.append(v)
        return sorted(set(found))

    prios_eu = scan_prios(mem_eu, 0x33A000, 0x33EA00)
    prios_us = scan_prios(mem_us, 0x339000, 0x33EA00)
    t_eu = struct.unpack_from("<f", mem_eu, 0x49C3A0)[0]
    t_us = struct.unpack_from("<f", mem_us, 0x49BBA0)[0]

    def sim_viaje(step: float, t_lim: float) -> int:
        clk = struct.unpack("<f", struct.pack("<f", 0.0))[0]
        st = struct.unpack("<f", struct.pack("<f", step))[0]
        n = 0
        while True:
            n += 1
            clk = struct.unpack("<f", struct.pack("<f", clk + st))[0]
            f6 = 1.0 if clk >= t_lim else struct.unpack("<f", struct.pack("<f", 1.0 / (t_lim - clk)))[0]
            if f6 >= 1.0:
                return n

    pal_n = sim_viaje(1.2, t_eu)
    ntsc_n = sim_viaje(1.0, t_us)
    ok = (
        0x9000 in prios_eu and 0x9003 in prios_eu and 0x9025 in prios_eu
        and 0x9000 in prios_us and 0x9003 in prios_us and 0x9025 in prios_us
        and t_eu == 40.0 and t_us == 40.0
        and pal_n == 33 and ntsc_n == 39
    )
    return {
        "ok": ok,
        "prios_eu_clave": [hex(x) for x in (0x9000, 0x9003, 0x9025) if x in prios_eu],
        "prios_us_clave": [hex(x) for x in (0x9000, 0x9003, 0x9025) if x in prios_us],
        "t_eu": t_eu,
        "t_us": t_us,
        "campos_pal": pal_n,
        "campos_ntsc": ntsc_n,
        "error_campos": 0,
    }


def oraculo_hito_space(mem_eu: bytes, mem_us: bytes) -> dict:
    """Ejecuta s_SPACE EU (0x31F280) y USA (0x31EF90) en 0x7D0 y 0x7D9."""
    cmn_p = _buscar_cmn()
    src_inl = (RAIZ / "src" / "iw_mips_campo.inl").read_text("utf-8")
    mudos_map = {
        "eu": [int(x.strip(), 16) for x in src_inl.split("kMudosC[] = {")[1].split("};")[0].replace("\n", "").split(",") if x.strip()],
        "us": [int(x.strip(), 16) for x in src_inl.split("kMudosU[] = {")[1].split("};")[0].replace("\n", "").split(",") if x.strip()],
    }
    casos = []
    for reg, mem in (("eu", mem_eu), ("us", mem_us)):
        gp = 0x4A3DF0 if reg == "eu" else 0x4A35F0
        if reg == "eu":
            space, init_a, pmgr, pg, pctx, pcam, pobjs, pgrid, pctxp, ctx, masc, obj_fn = (
                0x31F280, 0x323840, 0x49CA70, 0x49CDE8, 0x49CBA4, 0x49CBBC, 0x49CBC0, 0x49CDB8, 0x49CDC0, 0x5C0DC0, 0x5C1D78, 0x299900
            )
            getters = (0x2962E0, 0x296320, 0x296330, 0x26FF20)
        else:
            space, init_a, pmgr, pg, pctx, pcam, pobjs, pgrid, pctxp, ctx, masc, obj_fn = (
                0x31EF90, 0x323550, 0x49C270, 0x49C5C8, 0x49C394, 0x49C3AC, 0x49C3B0, 0x49C598, 0x49C5A0, 0x5C05C0, 0x5C1578, 0x299670
            )
            getters = (0x296050, 0x296090, 0x2960A0, 0x26FD10)

        for clave, idx_msn, pack_id in ((0x7D0, 0, 887), (0x7D9, 10, 893)):
            itm = _extraer_itm(_leer_afs(cmn_p, pack_id))
            c = ee.Ee(mem, gp)
            obj_calls = []

            def make_zero():
                def _h(s):
                    s.r[2] = 0
                return _h

            for a in mudos_map[reg]:
                c.hooks[a] = make_zero()

            def _hook_obj(s, _lst=obj_calls):
                _lst.append((s.r[4] & 0xFFFFFFFF, s.r[5] & 0xFFFFFFFF))
                s.r[2] = 0

            c.hooks[obj_fn] = _hook_obj
            c.hooks[getters[0]] = lambda s, _v=ctx: s.r.__setitem__(2, _v)
            c.hooks[getters[1]] = lambda s: s.r.__setitem__(2, 0x118C000)
            c.hooks[getters[2]] = lambda s: s.r.__setitem__(2, 0x118C000)
            c.hooks[getters[3]] = lambda s: s.r.__setitem__(2, 0x118D000)

            kG, kMgr, kObjs, kCam, kGrid, kCtxP, kItm = (
                0x1180000, 0x1182000, 0x118C000, 0x118D000, 0x118E000, 0x118F000, 0x1190000
            )
            for a, sz in ((kG, 0x2000), (kMgr, 0xA000), (kObjs, 0x1000), (kCam, 0x1000), (kGrid, 0x1000), (kCtxP, 0x1000), (kItm, 0x2000)):
                c.m[a : a + sz] = b"\x00" * sz
            c.m[kItm : kItm + len(itm)] = itm
            c.w32(pmgr, kMgr); c.w32(pg, kG); c.w32(pctx, ctx); c.w32(pcam, kCam); c.w32(pobjs, kObjs); c.w32(pgrid, kGrid); c.w32(pctxp, kCtxP)
            c.w32(ctx + 4, idx_msn); c.w16(ctx + 0xC, 0); c.w8(masc, 0)
            c.w32(kG + 0x148, kItm); c.w8(kG + 0x16A6, 1)
            c.w32(kMgr + 0x8B80, struct.unpack("<I", struct.pack("<f", 1.2 if reg == "eu" else 1.0))[0])
            c.w32(kGrid + 0x630, 7)
            for o, val in ((0xAD4, 3000.0), (0xAD8, 3000.0), (0xAF0, -1350.0), (0xAF8, -1350.0)):
                c.w32(kGrid + o, struct.unpack("<I", struct.pack("<f", val))[0])
            c.w32(kGrid + 0xADC, 10); c.w32(kGrid + 0xAE0, 10)
            c.call(init_a)

            def step_at(x: float, y: float, z: float) -> None:
                jug = kMgr + 0x800
                for i, v in enumerate((x, y, z, 1.0)):
                    vb = struct.unpack("<I", struct.pack("<f", v))[0]
                    c.w32(jug + 0x640 + 4 * i, vb); c.w32(jug + 0x40 + 4 * i, vb)
                    for co in (0x80, 0x90, 0xC0):
                        c.w32(kCam + co + 4 * i, vb)
                c.w16(jug + 0xCBC, 1)
                c.w32(kMgr + 0x8B7C, 0)
                c.call(space)

            bc_init = c.m[kG + 0x16BC]
            na = c.m[kG + 0x166F]
            hito_pt = struct.unpack_from("<3f", itm, 0x30)
            step_at(*hito_pt)
            bc_before = c.m[kG + 0x16BC]
            calls_before = len(obj_calls)
            for k in range(na):
                step_at(*struct.unpack_from("<3f", itm, 0x40 + 16 * k))
            bc_after_a = c.m[kG + 0x16BC]
            step_at(*hito_pt)
            bc_touch = c.m[kG + 0x16BC]
            pose_win = c.r32(kObjs + 0x1A8)
            ok = (
                bc_init == 2 and bc_before == 2 and calls_before == 0
                and bc_after_a == 0 and bc_touch == 2 and pose_win == 0x2E
                and len(obj_calls) == 1 and obj_calls[0][1] == 1
            )
            casos.append({
                "region": reg,
                "clave": hex(clave),
                "na": na,
                "bc_init": bc_init,
                "bc_before": bc_before,
                "bc_after_a": bc_after_a,
                "bc_touch": bc_touch,
                "pose": hex(pose_win),
                "obj_param1": obj_calls[0][1] if obj_calls else None,
                "ok": ok,
            })
    return {"ok": all(x["ok"] for x in casos), "casos": casos}


def oraculo_rango_combate(mem_eu: bytes, mem_us: bytes) -> dict:
    """Verifica Char_ComputeStatGrade EU 0x2AC0D0 y USA 0x2ABBE0 (Fight 39 vs 40 y contadores notas 1..4)."""
    casos = []
    for reg, mem, gp, fn in (("eu", mem_eu, 0x4A3DF0, 0x2AC0D0), ("us", mem_us, 0x4A35F0, 0x2ABBE0)):
        c = ee.Ee(mem, gp)
        st = 0x1200000
        for i in range(0, 0x80, 4):
            c.w32(st + i, 0)
        c.w32(st + 0x44, 39); c.call(fn, st, 2, 1); g39 = c.r[2] & 0xFF
        c.w32(st + 0x44, 40); c.call(fn, st, 2, 1); g40 = c.r[2] & 0xFF
        c.w32(st + 0x1C, 1); c.w32(st + 0x20, 1); c.w32(st + 0x24, 5); c.w32(st + 0x28, 500); c.w32(st + 0x2C, 3)
        c.call(fn, st, 1, 2); n1 = c.r[2] & 0xFF
        c.call(fn, st, 2, 2); n2 = c.r[2] & 0xFF
        c.call(fn, st, 4, 1); n4 = c.r[2] & 0xFF
        c.w32(st + 0x30, 5); c.w32(st + 0x2C, 3); c.w32(st + 0x34, 10); c.w32(st + 0x38, 4); c.w32(st + 0x3C, 300)
        c.call(fn, st, 3, 2); n3_hi = c.r[2] & 0xFF
        c.w32(st + 0x38, 5); c.w32(st + 0x3C, 301)
        c.call(fn, st, 3, 2); n3_lo = c.r[2] & 0xFF
        ok = (g39 == 0 and g40 == 1 and n1 == 7 and n2 == 7 and n4 == 7 and n3_hi == 7 and n3_lo == 3)
        casos.append({"region": reg, "fn": hex(fn), "fight_39": g39, "fight_40": g40,
                      "notas1": n1, "notas2": n2, "notas4": n4, "notas3_hi": n3_hi, "notas3_lo": n3_lo, "ok": ok})
    return {"ok": all(x["ok"] for x in casos), "casos": casos}


def oraculo_disparo_rango(mem_eu: bytes, mem_us: bytes) -> dict:
    """Verifica RangoDisparo (0x7D7/0x7D8/0x7DE) EU vs USA vs C++ en 303 casos."""
    dirs = {
        "eu": {0x7D7: (0x34B800, 0x3481E0, 0x49CE78, 0x14), 0x7D8: (0x3279F0, 0x324490, 0x49CDF0, 0x28), 0x7DE: (0x30D150, 0x3099E0, 0, 0)},
        "us": {0x7D7: (0x34B300, 0x347CE0, 0x49C65C, 0x14), 0x7D8: (0x3276E0, 0x3241A0, 0x49C5D0, 0x28), 0x7DE: (0x30CEE0, 0x3097C0, 0, 0)},
    }

    def run_r(mem: bytes, reg: str, clave: int, dif: int, ac: int, val: float) -> int:
        c = ee.Ee(mem, 0x4A3DF0 if reg == "eu" else 0x4A35F0)
        init_a, rango_a, ctxp, idx_off = dirs[reg][clave]
        c.call(init_a)
        if ctxp:
            for i in range(0, 0x200, 4):
                c.w32(0x1200000 + i, 0)
            c.w32(ctxp, 0x1200000)
            c.w32(0x1200000 + idx_off, dif)
            if clave == 0x7D7:
                c.w8(0x1200000 + 0xC, ac)
        c.f[12] = struct.unpack("<I", struct.pack("<f", val))[0]
        c.call(rango_a)
        raw = c.r[2] & 0xFFFFFFFF
        return -1 if raw == 0xFFFFFFFF else raw

    def cpp_r(clave: int, ac: int, val: float) -> int:
        if clave != 0x7DE:
            idx = -1
            if 0.0 < val < 999.0:
                for i, th in reversed(list(enumerate((999.0, 75.0, 60.0, 45.0, 30.0)))):
                    if val <= th:
                        idx = i
                        break
            if clave == 0x7D7 and ac < 7:
                idx = -1
            return idx
        return -1 if (val <= 0 or val >= 1) else (4 if val <= 0.0001 else 3 if val <= 0.1 else 2 if val <= 0.4 else 1 if val <= 0.7 else 0)

    total = 0
    dif_reg = 0
    dif_cpp = 0
    pruebas = [
        (0x7D7, [(d, a, v) for d in range(6) for a in (0, 6, 7) for v in (0.0, 15.0, 30.0, 30.01, 45.0, 60.0, 75.0, 90.0, 180.0)]
         + [(0, 7, v) for v in (-1.0, 0.001, 29.9999, 30.0001, 998.99, 999.0, 999.01)]),
        (0x7D8, [(d, 0, v) for d in range(6) for v in (0.0, 15.0, 30.0, 30.01, 45.0, 60.0, 75.0, 90.0, 180.0)]
         + [(0, 0, v) for v in (-1.0, 0.001, 29.9999, 30.0001, 998.99, 999.0, 999.01)]),
        (0x7DE, [(d, 0, v) for d in range(6) for v in (0.0, 0.00005, 0.0001, 0.05, 0.1, 0.2, 0.4, 0.5, 0.7, 0.9, 1.0)]
         + [(0, 0, v) for v in (-0.1, 0.00009, 0.00011, 0.0999, 0.1001, 0.9999, 1.01)]),
    ]
    for clave, lista in pruebas:
        for d, a, v in lista:
            total += 1
            re_eu = run_r(mem_eu, "eu", clave, d, a, v)
            re_us = run_r(mem_us, "us", clave, d, a, v)
            re_cp = cpp_r(clave, a, v)
            if re_eu != re_us:
                dif_reg += 1
            if re_eu != re_cp:
                dif_cpp += 1
    return {"ok": dif_reg == 0 and dif_cpp == 0, "total": total, "dif_eu_us": dif_reg, "dif_elf_cpp": dif_cpp}


def correr_oraculos() -> dict:
    mem_eu, h_eu = banco.cargar_elf(banco.ELF_EU)
    mem_us, h_us = banco.cargar_elf(banco.ELF_US)
    res = {
        "elf_eu_sha256": h_eu,
        "elf_us_sha256": h_us,
        "vuelo_obj8": oraculo_vuelo_obj8(mem_eu, mem_us),
        "hito_space": oraculo_hito_space(mem_eu, mem_us),
        "rango_combate": oraculo_rango_combate(mem_eu, mem_us),
        "disparo_rango": oraculo_disparo_rango(mem_eu, mem_us),
    }
    res["ok"] = all(res[k]["ok"] for k in ("vuelo_obj8", "hito_space", "rango_combate", "disparo_rango"))
    return res


def autoprueba() -> int:
    mem_eu, _ = banco.cargar_elf(banco.ELF_EU)
    mem_us, _ = banco.cargar_elf(banco.ELF_US)
    vo = oraculo_vuelo_obj8(mem_eu, mem_us)
    assert vo["ok"] and vo["campos_pal"] == 33 and vo["campos_ntsc"] == 39, vo
    rc = oraculo_rango_combate(mem_eu, mem_us)
    assert rc["ok"], rc
    for row in rc["casos"]:
        assert row["fight_39"] == 0 and row["fight_40"] == 1
    print("Autoprueba iw_oraculos_elf: vuelo 33/39 y Fight 39->0/40->1 (EU+USA) OK")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-check", "--autoprueba", action="store_true",
                   help="Ejecuta comprobaciones rapidas (vuelo 33/39 y Fight 39->0/40->1) contra ELF EU+USA")
    p.add_argument("--guardar", type=Path, help="Guarda el JSON de salida en la ruta indicada")
    args = p.parse_args()
    if args.self_check:
        return autoprueba()
    res = correr_oraculos()
    txt = json.dumps(res, indent=2, ensure_ascii=False)
    if args.guardar:
        args.guardar.parent.mkdir(parents=True, exist_ok=True)
        args.guardar.write_text(txt + "\n", encoding="utf-8")
    print(txt)
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

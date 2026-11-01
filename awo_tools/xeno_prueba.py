#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Instala y prueba el port XV2 SOLO en una copia fija de tools/pruebas.py.

Ejemplo: python -B awo_tools/xeno_prueba.py --modelo D:/tmp/xv2b3/GOK.bin
El mod queda desactivado al acabar. Restaura los markers de los otros mods.
No publica, no instala dependencias y no borra: los reemplazos van a cuarentena.
--selftest usa datos inventados y no abre el juego.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parent.parent
QUARANTINE = Path(r"D:\_PARA_BORRAR_CLAUDE\xv")
MOD_NAME = "xv2_GOK_local"


def padded(data, slot_size):
    size = (max(slot_size, len(data)) + 0xFFF) & ~0xFFF
    return data + bytes(size - len(data))


def slot_size(afs, index):
    with Path(afs).open("rb") as f:
        header = f.read(8)
        if len(header) != 8 or header[:4] != b"AFS\0":
            raise ValueError("AFS de B3 invalido")
        if not 0 <= index < struct.unpack_from("<I", header, 4)[0]:
            raise ValueError("entrada AFS fuera de rango")
        f.seek(8 + index * 8)
        row = f.read(8)
        if len(row) != 8:
            raise ValueError("tabla AFS truncada")
        return struct.unpack("<II", row)[1]


def check_log(text, mod=MOD_NAME, entry=264):
    hits = [l for l in text.splitlines() if "AFS OVERRIDE HIT" in l and mod in l and str(entry) in l]
    errors = [l for l in text.splitlines() if re.search(r"\[error\]|access violation|UNHANDLED EXCEPTION|physical write fault", l, re.I)]
    return dict(override_hits=len(hits), errors=errors, override_lines=hits[:4])


def model_vb(data):
    """Pool AWG0: evidencia unica del port, independiente del log de overrides.

    El mid-insert virtual sirve los archivos por su ruta silenciosa; por eso
    la ausencia de 'AFS OVERRIDE HIT' no prueba que se cargara el modelo nativo.
    """
    import sdbh_model as S
    a = next(o for o, size, typ, _ in S.amb_kids(data) if data[o:o+4] == b"#AWO")
    table = a + struct.unpack_from(">I", data, a + 0x1C)[0]
    g = a + struct.unpack_from(">I", data, table)[0]
    start, size = struct.unpack_from(">II", data, g + 0x28)
    return data[g + start:g + start + size]


def runtime_vb(P, j, expected, physics=(), direct=None):
    """Lectura de las paginas guest accesibles; nunca escribe memoria del juego."""
    import ctypes as C
    from ctypes import wintypes as W

    class MBI(C.Structure):
        _fields_ = [("base", C.c_void_p), ("allocation", C.c_void_p), ("allocation_protect", W.DWORD),
                    ("size", C.c_size_t), ("state", W.DWORD), ("protect", W.DWORD), ("kind", W.DWORD)]
    query = P.K32.VirtualQueryEx
    query.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(MBI), C.c_size_t]
    query.restype = C.c_size_t
    base = P.GBASE + P.DESPL
    address, end = base + 0x10000000, base + 0xA0000000
    scanned, needle, matches = 0, expected[:128], []
    def compare(actual, address):
        allowed = {i*44+k for i in physics for k in range(12)}
        changed = [i for i,(a,b) in enumerate(zip(expected,actual or b'')) if a!=b]
        preserved = len(actual or b'') == len(expected) and all(i in allowed for i in changed)
        return dict(exact=actual==expected, preserved=preserved, changed_bytes=len(changed),
                    changed_physics_vertices=len({i//44 for i in changed}),
                    guest_address=hex(address), windows=len(expected)//44,
                    sha256=hashlib.sha256(actual).hexdigest() if actual else None)
    if direct:
        return compare(P.leer(j.h,P.GBASE+direct,len(expected)),direct)
    while address < end:
        info = MBI()
        if not query(j.h, C.c_void_p(address), C.byref(info), C.sizeof(info)) or not info.size:
            break
        high = min(end, info.base + info.size)
        if info.state == 0x1000 and not (info.protect & 0x101):
            offset = address
            while offset < high:
                n = min(2 * 1024 * 1024, high - offset)
                block = P.leer(j.h, offset - P.DESPL, n)
                if block:
                    scanned += len(block)
                    pos = block.find(needle)
                    if pos >= 0:
                        start = offset + pos
                        actual = P.leer(j.h, start - P.DESPL, len(expected))
                        match = actual == expected
                        matches.append(dict(guest_address=hex(start - base), bytes=len(expected), exact=match,
                                            sha256=hashlib.sha256(actual).hexdigest() if actual else None))
                        if match:
                            return dict(exact=True, windows=len(expected)//44, matches=matches, scanned_bytes=scanned)
                offset += max(1, n - 127) if n > 127 else n
        address = high
    return dict(exact=False, windows=len(expected)//44, matches=matches, scanned_bytes=scanned)


def runtime_pose(P, j):
    def u32(a):
        b = P.leer(j.h, P.GBASE + a, 4)
        return int.from_bytes(b, "big") if b and len(b) == 4 else 0
    mgr = u32(0x823D6CF4)
    x = mgr + 2096
    model = u32(x + 2952)
    table = u32(model + 1032) if model else 0
    matrices = P.leer(j.h, P.GBASE + table, 46 * 64) if table else b""
    awg = u32(model+4) if model else 0
    raw = P.leer(j.h,P.GBASE+awg,64) if awg else b''
    gpu = u32(awg+0x28) if raw and raw[:4] in (b'#AWG',b'!AWG') else 0
    ib = u32(awg+0x30) if gpu else 0
    if ib and ib < 0x1000000: ib+=awg
    vb = ib-u32(awg+0x2C) if ib else 0
    gpu_header=P.leer(j.h,P.GBASE+gpu,128) if gpu else b''
    return dict(fighter=hex(x), model=hex(model), matrix_table=hex(table),
                animation=u32(x + 2928) & 0xFFF,
                pose_sha256=hashlib.sha256(matrices).hexdigest() if matrices else None,
                awg=hex(awg), awg_header=(raw or b'').hex(), vb=vb,
                gpu=hex(gpu),gpu_header=(gpu_header or b'').hex())


def physics_vertices(data):
    import sdbh_model as S
    a=next(o for o,_,_,_ in S.amb_kids(data) if data[o:o+4]==b'#AWO')
    g=a+struct.unpack_from('>I',data,a+struct.unpack_from('>I',data,a+0x1C)[0])[0]
    nb,names=struct.unpack_from('>I',data,g+16)[0],struct.unpack_from('>I',data,g+28)[0]
    m=g+names+nb*32
    if data[m:m+4]!=b'XV5S': return ()
    nn,nv=struct.unpack_from('>II',data,m+8)
    return tuple(struct.unpack_from('>I',data,m+32+nn*32+i*40)[0] for i in range(nv))


def selftest():
    assert len(padded(b"abc", 100)) == 4096
    assert padded(b"abc", 100)[:4] == b"abc\0"
    assert len(padded(bytes(4100), 100)) == 8192
    assert len(padded(bytes(4096), 4096)) == 4096
    assert check_log("[info] AFS OVERRIDE HIT: mods/xv2_GOK_local/us/data_cmn.afs/264/geom.bin")["override_hits"] == 1
    assert len(check_log("[error] failure\nUNHANDLED EXCEPTION")["errors"]) == 2
    assert check_log("[info] all good")["errors"] == []
    import sdbh_model as S
    awo = bytearray(256)
    awo[:4], awo[64:68] = b"#AWO", b"#AWG"
    struct.pack_into(">I", awo, 0x1C, 48)
    struct.pack_into(">I", awo, 48, 64)
    struct.pack_into(">II", awo, 64 + 0x28, 64, 44)
    awo[128:172] = bytes(range(44))
    assert model_vb(S.amb_pack([(bytes(awo), 1)])) == bytes(range(44))
    print("xeno_prueba: N0 OK (padding real/virtual, evidencia de pool AWG, errores)")


def run(args):
    sys.path.insert(0, str(ROOT / "tools"))
    import pruebas as P
    from PIL import Image
    # Solo copias fijas ya existentes. Nunca se acepta una ruta al release.
    if args.copia not in ("sol", "sol2", "smoke_a", "smoke_b", "smoke_c", "smoke_d", "smoke_e", "smoke_f"):
        raise ValueError("elige una copia fija de pruebas (sol, sol2 o smoke_*)")
    dest = P.BASE / args.copia
    P.comprobar_libre(dest)
    if not (dest / "dbz3.exe").is_file():
        raise ValueError("prepara primero la copia fija con tools/pruebas.py prep")
    if not args.trabajo.is_dir():
        raise ValueError("crea primero --trabajo")
    if args.region == 'eu':
        root = P.BASE / 'eu_test2'
        if not (root/'default.xex').is_file() or not (root/'eu/data_cmn.afs').is_file():
            raise ValueError('falta la fuente EU fija eu_test2 (default.xex y eu/data_cmn.afs)')
        P.R = root
        P.PANTALLA = P.PANTALLA_EU
        P.ESTRUCTAS = (0x8247A8D0, 0x824BA100)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    work = args.trabajo / ("prueba_" + stamp)
    work.mkdir()
    backup = QUARANTINE / stamp
    backup.mkdir(parents=True)
    sys.path.insert(0, str(ROOT / "mod center hd"))
    import swap_b3
    import roster_build as rb
    from xeno_a_b3 import CHARACTERS
    fighter = args.sobre if args.sobre is not None else CHARACTERS[args.personaje][1]
    if fighter not in rb.IDS or not rb.IDS[fighter].get("models"):
        raise ValueError("ID B3 sin modelo: %s" % fighter)
    entry = rb.IDS[fighter]["models"][0]
    name = args.mod or ("xv2_%s_local" % args.personaje)
    if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
        raise ValueError("--mod debe ser un nombre de carpeta de mods, no una ruta")
    compressed = work / (args.personaje + ".lzx")
    subprocess.run([swap_b3.XBCOMPRESS, "/N:2048", str(args.modelo.resolve()), str(compressed)],
                   check=True, capture_output=True)
    payload = compressed.read_bytes()
    if payload[:4] != b"\x0f\xf5\x12\xee":
        raise ValueError("xbcompress: no genero LZX 360")
    afs = (P.R/'eu/data_cmn.afs') if args.region == 'eu' else ROOT/'us/data_cmn.afs'
    served = padded(payload, slot_size(afs, entry))
    mod = dest / "mods" / name
    if args.mod:
        # El hook AFS busca mods/<mod>/us incluso ejecutando el guest EU.
        installed = mod / 'us' / "data_cmn.afs" / str(entry) / "geom.bin"
        if not installed.is_file() or installed.read_bytes() != served:
            raise ValueError("el mod importado no coincide con --modelo; no valida la importacion")
    changed = []
    runtime = {}

    def prep(j, par, env):
        if mod.exists() and not args.mod:
            shutil.move(str(mod), str(backup / "mod_anterior"))
        # No se mezcla el port con modelos/AFS de ninguna otra prueba.
        for other in sorted((dest / "mods").iterdir()):
            if other != mod and other.is_dir() and not (other / ".disabled").exists():
                marker = other / ".disabled"
                marker.write_text("xeno_prueba temporal " + stamp, encoding="utf-8")
                changed.append(marker)
        if args.mod:
            if (mod / ".disabled").exists():
                shutil.move(str(mod / ".disabled"), str(backup / "marker_importado.disabled"))
            return
        target = mod / 'us' / "data_cmn.afs" / str(entry)
        target.mkdir(parents=True)
        with (target / "geom.bin").open("xb") as f:
            f.write(served)
        (mod / "README.txt").write_text("Conversion LOCAL de la copia del jugador. No distribuir.\n"
                                        "Quita .disabled para activarlo solo en esta copia de pruebas.\n", encoding="utf-8")

    class HalfCapture(P.Juego):
        def captura(self, nombre):
            if args.oculto:
                return None  # N1: no se finge una captura visual con la ventana oculta.
            path = super().captura(nombre)
            full = self.run / "full"
            full.mkdir(exist_ok=True)
            saved = full / path.name
            path.rename(saved)
            with Image.open(saved) as im:
                im.resize((max(1, im.width // 2), max(1, im.height // 2)), Image.Resampling.LANCZOS).save(path)
            return path

    def scene(j, par):
        j.log('using EU image config' if args.region == 'eu' else 'using US image config', 50)
        j.log(r"hub: combate nativo", 50)
        j.log(r"tag: empieza un combate", 60)
        P._esperar_pelea(j, 90)
        time.sleep(0.5)
        runtime["rest"] = runtime_pose(P, j)
        j.captura("01_reposo_05")
        j.tecla("D 1400", 400)
        j.captura("02_andando_05")
        time.sleep(1.1)
        j.tecla("L 700", 300)
        runtime["attack"] = runtime_pose(P, j)
        j.captura("03_golpe_05")
        time.sleep(1.5)
        j.captura("04_animado_05")
        model_data=args.modelo.read_bytes()
        physical=physics_vertices(model_data)
        runtime["vb"] = runtime_vb(P, j, model_vb(model_data),physical,runtime['attack'].get('vb'))
        runtime["pose_changed"] = runtime["rest"]["pose_sha256"] != runtime["attack"]["pose_sha256"]
        runtime["hit_confirmed"] = bool(re.search(r"muneco .*golpe 200 -> golpe", j.texto_log()))
        (j.run / "runtime.json").write_text(json.dumps(runtime, indent=2), encoding="utf-8")
        diag = check_log(j.texto_log(), name, entry)
        if not runtime["vb"].get("preserved",runtime['vb']['exact']):
            raise P.Fallo("el pool de vertices XV2 no coincide en memoria guest; no valida el modelo")
        if not runtime["pose_changed"] or not runtime["rest"]["pose_sha256"]:
            raise P.Fallo("no se comprobo un cambio de pose del esqueleto")
        if not runtime["hit_confirmed"]:
            raise P.Fallo("el log no confirma el golpe nativo 0x200; no valida el moveset")
        if diag["errors"]:
            raise P.Fallo("log con [error]: " + diag["errors"][0])
        j.log(r"tag: combate acabado", 45)
        runtime["fight_finished"] = True
        log=j.texto_log()
        runtime['physics_active']=bool(re.search(r'xv2 fisica: SCD activada',log))
        runtime['physics_steps']=[int(n) for n in re.findall(r'xv2 fisica: cuadros=(\d+)',log)]
        runtime['fps']=[float(n) for n in re.findall(r'perf fps=([0-9.]+)',log)]
        if physical and (not runtime['physics_active'] or not runtime['physics_steps'] or
                         not runtime['vb'].get('changed_physics_vertices')):
            raise P.Fallo('SCD no confirmada: faltan cuadros del controlador o vertices deformados')
        j.captura("05_fin_combate_05")
        (j.run / "runtime.json").write_text(json.dumps(runtime, indent=2), encoding="utf-8")

    P.PRUEBAS = work
    P.PAPELERA = QUARANTINE / "respaldos_pruebas"
    P.VENTANAS = 1
    P.OVERRIDES.update(dbz3_dev_mode="true", dbz3_diag_logging="true", dbz3_region='"%s"' % args.region,
                       dbz3_freecam="false", dbz3_freecam_pausa="false", dbz3_freecam_hud="false",
                        dbz3_cac_xv2="false", dbz3_combate_prueba_muneco='"quieto"',
                        dbz3_perf_logging='true',frame_cap='60')
    P.Juego = HalfCapture
    scenario = "xeno_" + args.personaje.lower()
    P.ESCENARIOS[scenario] = (args.personaje + " XV2: reposo, caminar y golpe con moveset B3", lambda p: {
        "DBZ3_TEST_BATTLE": "%d,%d,2,15" % (fighter, 7 if fighter != 7 else 0),
         "DBZ3_TEST_OCULTO": '1' if args.oculto else '0', "DBZ3_TEST_TURBO": "1",
        "DBZ3_HUB_MODE": "5", "DBZ3_COMBATE_MUNECO": "quieto",
        "DBZ3_CINE_TRAZA": "1",
        "REX_DRAW_RESOLUTION_SCALE_X": "1", "REX_DRAW_RESOLUTION_SCALE_Y": "1"}, scene, prep)
    result = None
    # La reserva incluye la restauracion de markers: otro agente no debe poder
    # tomar la copia en el intervalo entre cerrar el juego y restaurar sus mods.
    with P.copia_en_uso(dest):
        P.comprobar_libre(dest)
        for name in ("dbz3.exe", "rexruntime.dll", "rexgpu-xenos.dll", "dbz3_user.toml"):
            if (dest / name).is_file():
                shutil.copy2(dest / name, backup / name)
        try:
            result = P._correr(args.copia, scenario, par={"ventana": "1280x720"}, forzar=True)
        finally:
            if mod.is_dir() and not (mod / ".disabled").exists():
                (mod / ".disabled").write_text("Prueba terminada; port local provisional", encoding="utf-8")
            for k, marker in enumerate(changed):
                if marker.is_file() and stamp in marker.read_text(encoding="utf-8"):
                    shutil.move(str(marker), str(backup / ("marker_%03d.disabled" % k)))
    diag = check_log((Path(result["run"]) / "dbz3.log").read_text(encoding="utf-8", errors="replace"), name, entry)
    result.update(diag)
    if diag["errors"]:
        result["ok"] = False
        result["msg"] = "log final con errores: " + diag["errors"][0]
    result["runtime"] = runtime
    result.update(model=str(args.modelo), model_sha256=hashlib.sha256(args.modelo.read_bytes()).hexdigest(),
                   character=args.personaje, fighter=fighter, entry=entry, region=args.region, hidden=args.oculto, imported_mod=bool(args.mod),
                  compressed=len(payload), served=len(served), mod=str(mod), mod_disabled=(mod/".disabled").is_file(), backup=str(backup))
    output = work / "resultado.json"
    output.write_text(json.dumps(result, indent=2, ensure_ascii=False, default=str), encoding="utf-8")
    print("Resultado: %s\nMod desactivado: %s\nInforme: %s" % ("PASS" if result["ok"] else "FAIL", mod, output))
    return 0 if result["ok"] else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--modelo", type=Path)
    ap.add_argument("--personaje", choices=("GOK", "VGT", "PIC"), default="GOK")
    ap.add_argument("--sobre", type=int)
    ap.add_argument("--mod", help="prueba el mod ya creado por importar.py, sin reemplazarlo")
    ap.add_argument("--copia", default="sol")
    ap.add_argument('--region', choices=('us', 'eu'), default='us')
    ap.add_argument('--oculto', action='store_true', help='N1 sin capturas; misma prueba de memoria, golpe y final')
    ap.add_argument("--trabajo", type=Path, default=Path(r"D:\tmp\xv4"))
    args = ap.parse_args(argv)
    if args.selftest:
        selftest()
        return 0
    if not args.modelo:
        ap.error("indica --modelo")
    try:
        return run(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("xeno_prueba: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

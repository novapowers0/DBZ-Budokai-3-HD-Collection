"""Port B1 HD -> B3 HD: convierte un par AWO+AZT del B1 al formato #AMB del B3.

⚠️ ESTADO 2026-08-17: el port B1->B3 ESTÁ BLOQUEADO por tamaño. El AMB B1
comprimido (271516) excede el slot de Krillin (106496) -> el LZX se trunca y
el guest cuelga. Para que funcione hay que DECIMAR el AWO B1 a ~195KB
descomprimido (ver SESION_2026-08-17.md). Este script hace la conversión de
sellos correcta; falta el paso de decimación.

El runtime HD dibuja el bin completo tal cual (swap nativo, validado B3->B3):
  - B1: bins separados #AWO (geom) + #AZT (tex) en data_sp.afs
  - B3: contenedor #AMB con [header, #AWO, #AZT] en una sola entrada
Las unicas conversiones de sellos necesarias (verificado contra e326 del B3):
  1. Flag AWG +0x0C: B1=0x2 -> B3=0x4
  2. Type2 mesh part +0x38/+0x3C: B1=0x1BD/0x11BD -> B3=0x29BD
  3. Sombra: B1=0x190 -> B3=0x1B4 (si existe)
  4. Materiales: B1 escala 4x128.0 -> B3 escala 4x1.0
     (pesos y type2 se mantienen: el B3 acepta los del B1)
  5. AZT: conservar alpha original (NO forzar 0xFF como en B3->B1)

NOTA esqueleto: B1 (52 huesos) y B3 (51) comparten labels KLL pero en ORDEN
DISTINTO (OBI/ROBI/LOBI al final en B1). Si se decima el AWO B1 hay que
re-mapear los bone indices POR LABEL (AGENTS §3.3).

Uso:
  python port_b1_to_b3.py <awo_b1.bin> <azt_b1.bin> --dest <bin_b3> [--mod <nombre>]
  python port_b1_to_b3.py --extract-b1 <data_sp.afs> <bin_geom> <bin_tex> --dest <bin_b3> [--mod]

Flujo:
  1. Extraer AWO+AZT del B1 (si --extract-b1).
  2. Convertir sellos B1->B3.
  3. Empaquetar #AMB (header B3 identico al e326).
  4. Comprimir LZX /N:2048.
  5. Reconstruir data_cmn.afs (build_afs.py, mid-insert delta 0x800).
  6. Instalar en mods/<mod>/us/data_cmn.afs y activar.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

U32 = struct.Struct('>I')

COMP_CANDIDATES = [
    os.path.join(os.environ.get('TEMP', ''), 'opencode', 'xbcomp'),
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'),
]
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
B3_AFS = os.path.join(ROOT, 'us', 'data_cmn.afs')
B3_BUILD = os.path.join(ROOT, 'out', 'build', 'win-amd64-release')

ESCALA_B3 = struct.pack('>4f', 1.0, 1.0, 1.0, 1.0)


def u32r(b, o):
    return U32.unpack_from(b, o)[0]


def find_tools():
    for d in COMP_CANDIDATES:
        comp = os.path.join(d, "xbcompress.exe")
        dec = os.path.join(d, "xbdecompress.exe")
        if os.path.isfile(comp) and os.path.isfile(dec):
            return comp, dec
    return None, None


def lzx_compress(comp, src, dst):
    r = subprocess.run([comp, "/N:2048", src, dst], input=b"A\n", capture_output=True)
    if not os.path.isfile(dst) or os.path.getsize(dst) == 0:
        raise RuntimeError("xbcompress fallo: %s" % r.stdout)
    return os.path.getsize(dst)


def lzx_decompress(dec, src, dst):
    r = subprocess.run([dec, src, dst], input=b"A\n", capture_output=True)
    if not os.path.isfile(dst) or os.path.getsize(dst) == 0:
        raise RuntimeError("xbdecompress fallo: %s" % r.stdout)
    return os.path.getsize(dst)


# ---------------------------------------------------------------------------
# AFS helpers (tabla en 0x10, little-endian — MISMA convencion que build_afs.py)
# OJO: el SDK del runtime lee la tabla en offset 8 (desfase +1); build_afs.py y
# este port usan 0x10, que es la que coincide con el bin visible del juego.
# ---------------------------------------------------------------------------
def read_afs_index(afs_path):
    with open(afs_path, "rb") as f:
        f.seek(0)
        data = f.read(0x10)
        if data[:3] != b"AFS":
            raise RuntimeError("no es un AFS: %s" % data[:4])
        count = struct.unpack("<I", data[4:8])[0]
        entries = []
        with open(afs_path, "rb") as f:
            f.seek(0x10)
            for _ in range(count):
                raw = f.read(8)
                addr, size = struct.unpack("<II", raw)
                entries.append((addr, size))
    return entries


def extract_afs_entry(afs_path, idx):
    entries = read_afs_index(afs_path)
    addr, size = entries[idx]
    with open(afs_path, "rb") as f:
        f.seek(addr)
        return f.read(size)


def decompress_entry(dec, data, workdir, name):
    lzx = os.path.join(workdir, name + ".lzx")
    out = os.path.join(workdir, name + ".bin")
    open(lzx, "wb").write(data)
    try:
        lzx_decompress(dec, lzx, out)
    except RuntimeError:
        return None
    return open(out, "rb").read()


# ---------------------------------------------------------------------------
# Conversion de sellos B1 -> B3
# ---------------------------------------------------------------------------
def convert_sellos(awo):
    """Convierte los sellos del AWO B1 (flag 0x2, type2 0x1BD) a B3 (0x4, 0x29BD)."""
    b = bytearray(awo)
    amg_am = u32r(b, 0x18)
    amg_tbl = u32r(b, 0x1C)
    tot_flag = tot_type = tot_mat = 0
    for i in range(amg_am):
        awg = u32r(b, amg_tbl + i * 4)
        if awg + 0x40 > len(b):
            continue
        # 1. flag +0x0C -> 0x4
        if u32r(b, awg + 0x0C) != 0x4:
            struct.pack_into('>I', b, awg + 0x0C, 0x4)
            tot_flag += 1
        # 2. type2 + materiales
        hdr_off = u32r(b, awg + 0x20)
        hdr_count = u32r(b, awg + 0x24)
        hdr_abs = awg + hdr_off
        for p in range(hdr_count):
            pos = hdr_abs + p * 0x50
            t38 = u32r(b, pos + 0x38)
            t3c = u32r(b, pos + 0x3C)
            shadow = (t38 == 0x190 or t3c == 0x190)
            # type2 -> B3
            if t38 in (0x1BD, 0x11BD):
                struct.pack_into('>I', b, pos + 0x38, 0x29BD)
                tot_type += 1
            elif t38 == 0x190:
                struct.pack_into('>I', b, pos + 0x38, 0x1B4)
                tot_type += 1
            if t3c in (0x1BD, 0x11BD):
                struct.pack_into('>I', b, pos + 0x3C, 0x29BD)
                tot_type += 1
            elif t3c == 0x190:
                struct.pack_into('>I', b, pos + 0x3C, 0x1B4)
                tot_type += 1
            # materiales B3 (escala 1.0, NO tocar sombras)
            if not shadow:
                b[pos:pos + 16] = ESCALA_B3
                tot_mat += 1
    return bytes(b), tot_flag, tot_type, tot_mat


def build_amb(awo, azt):
    """Empaqueta AWO+AZT en un contenedor #AMB con el header del B3."""
    n = 2
    hdr = bytearray(0x40)
    hdr[0:4] = b'#AMB'
    struct.pack_into('>I', hdr, 0x04, 0x20)
    struct.pack_into('>I', hdr, 0x0C, n)
    struct.pack_into('>I', hdr, 0x10, n)
    struct.pack_into('>I', hdr, 0x14, 0x20)
    struct.pack_into('>I', hdr, 0x18, 0x40)
    # tabla: [loc, size, tipo] x2 (16B cada una)
    loc0 = 0x40
    sz0 = len(awo)
    loc1 = 0x40 + len(awo)
    sz1 = len(azt)
    struct.pack_into('>I', hdr, 0x20, loc0)
    struct.pack_into('>I', hdr, 0x24, sz0)
    struct.pack_into('>I', hdr, 0x28, 1)
    struct.pack_into('>I', hdr, 0x30, loc1)
    struct.pack_into('>I', hdr, 0x34, sz1)
    struct.pack_into('>I', hdr, 0x38, 2)
    return bytes(hdr) + awo + azt


# ---------------------------------------------------------------------------
# build_afs (usa build_afs.py del proyecto, MID-INSERT)
# NOTA 2026-08-17: --append DESCARTADO (rompe la busqueda binaria del guest,
# crash 0xC0000005). El bin DEBE caber en el slot (comprimido <= 106496) o el
# LZX se trunca -> cuelgue. El AWO B1 (685856 B) no cabe sin decimar.
# ---------------------------------------------------------------------------
def build_afs(afs_orig, idx, new_bin, out_path):
    script = os.path.join(HERE, 'build_afs.py')
    r = subprocess.run([sys.executable, script, afs_orig, str(idx), new_bin, out_path],
                       capture_output=True)
    print(r.stdout.decode('utf-8', 'ignore'))
    if r.returncode != 0:
        raise RuntimeError('build_afs fallo: %s' % r.stderr.decode('utf-8', 'ignore'))


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description='Port B1 HD -> B3 HD')
    ap.add_argument('awo', nargs='?', default=None, help='AWO del B1 (o usar --extract-b1)')
    ap.add_argument('azt', nargs='?', default=None, help='AZT del B1')
    ap.add_argument('--extract-b1', nargs=3, metavar=('AFS', 'GEOM_BIN', 'TEX_BIN'),
                    help='extraer AWO+AZT del data_sp.afs del B1')
    ap.add_argument('--dest', type=int, required=True, help='entrada B3 destino (ej 326=Krillin)')
    ap.add_argument('--mod', default=None, help='nombre del mod (default port_b1_b3_<dest>)')
    ap.add_argument('--dry', action='store_true')
    ap.add_argument('--keep', action='store_true', help='no activar el mod ni desactivar otros')
    args = ap.parse_args()

    comp, dec = find_tools()
    if dec is None:
        print('ERROR: no se encontraron xbcompress/xbdecompress')
        return 1

    workdir = os.path.join(os.environ.get('TEMP', '/tmp'), 'opencode', 'port_b1_b3')
    os.makedirs(workdir, exist_ok=True)

    # 1. Obtener AWO + AZT
    if args.extract_b1:
        afs_b1, gbin, tbin = args.extract_b1
        print('Extraer B1: %s geom=%s tex=%s' % (afs_b1, gbin, tbin))
        geom_data = decompress_entry(dec, extract_afs_entry(afs_b1, int(gbin)), workdir, 'b1_geom')
        tex_data = decompress_entry(dec, extract_afs_entry(afs_b1, int(tbin)), workdir, 'b1_tex')
        if geom_data is None or tex_data is None:
            print('ERROR: no se pudieron extraer del AFS B1')
            return 1
    else:
        if not args.awo or not args.azt:
            ap.print_help()
            return 1
        geom_data = open(args.awo, 'rb').read()
        tex_data = open(args.azt, 'rb').read()
    if geom_data[:4] != b'#AWO' or tex_data[:4] != b'#AZT':
        print('ERROR: se esperaba #AWO + #AZT, se obtuvo %s + %s' % (
            geom_data[:4], tex_data[:4]))
        return 1
    print('B1: #AWO %d bytes (%d AWGs) + #AZT %d bytes' % (
        len(geom_data), u32r(geom_data, 0x18), len(tex_data)))

    # 2. Convertir sellos
    awo_b3, nf, nt, nm = convert_sellos(geom_data)
    print('Sellos convertidos: flag=%d type2=%d materiales=%d' % (nf, nt, nm))

    # 3. Empaquetar #AMB
    amb = build_amb(awo_b3, tex_data)
    print('AMB: %d bytes (header + AWO + AZT)' % len(amb))

    if args.dry:
        print('DRY: no se comprime ni instala')
        return 0

    # 4. Comprimir LZX /N:2048
    amb_raw = os.path.join(workdir, 'port_amb.bin')
    amb_lzx = os.path.join(workdir, 'port_amb.lzx')
    open(amb_raw, 'wb').write(amb)
    sz = lzx_compress(comp, amb_raw, amb_lzx)
    print('Comprimido: %d -> %d bytes LZX' % (len(amb), sz))

    # 5. Verificar round-trip
    rt = os.path.join(workdir, 'port_rt.bin')
    lzx_decompress(dec, amb_lzx, rt)
    if open(rt, 'rb').read() != amb:
        print('ERROR: round-trip fallo')
        return 1
    print('Round-trip OK')

    # 6. Reconstruir AFS del B3
    mod_name = args.mod or 'port_b1_b3_%d' % args.dest
    mod_dir = os.path.join(B3_BUILD, 'mods', mod_name, 'us')
    os.makedirs(mod_dir, exist_ok=True)
    out_afs = os.path.join(mod_dir, 'data_cmn.afs')
    build_afs(B3_AFS, args.dest, amb_lzx, out_afs)

    if args.keep:
        print('Mod creado sin activar: %s' % mod_name)
        return 0

    # 7. Activar (toml) + desactivar el resto
    toml = os.path.join(B3_BUILD, 'dbz3_user.toml')
    txt = open(toml, 'r', encoding='utf-8').read()
    import re
    txt = re.sub(r'dbz3_enabled_mods\s*=\s*"[^"]*"',
                 'dbz3_enabled_mods = "%s"' % mod_name, txt, count=1)
    open(toml, 'w', encoding='utf-8').write(txt)
    mods_root = os.path.join(B3_BUILD, 'mods')
    for m in sorted(os.listdir(mods_root)):
        d = os.path.join(mods_root, m)
        if not os.path.isdir(d) or m == mod_name:
            continue
        marker = os.path.join(d, '.disabled')
        if not os.path.exists(marker):
            open(marker, 'w').write('disabled\n')
            print('Desactivado: %s' % m)
    keep = os.path.join(mods_root, mod_name, '.disabled')
    if os.path.exists(keep):
        os.remove(keep)
    print('\nMod activo: %s (AFS completo con bin %s = Krillin B1)' % (mod_name, args.dest))
    print('Probar: Krillin en el B3 deberia verse con el modelo del B1.')


if __name__ == '__main__':
    sys.exit(main())
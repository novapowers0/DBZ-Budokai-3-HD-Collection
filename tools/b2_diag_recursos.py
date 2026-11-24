"""Mide el radar PAL y los recursos de Shenron de una ISO local; no dibuja ni ejecuta el juego."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

from b2_oraculo import original as orig

ROOT = Path(__file__).resolve().parents[1]
ISO_DEF = Path('D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
ESTADO_DEF = Path('E:/DBZ3HD_trabajo/A2/sstates/fase1.p2s')


def u32(d, p):
    return struct.unpack_from('<I', d, p)[0]


def radar(base):
    resultados = []
    for bit in (0, 1):
        c, eventos = base.cpu({0x26FFF0, 0x245C00}), []
        orig.service(c, 0x2A7710, 'ayuda', eventos)
        circular, barra = c.r32(c.gp - 0x732C), c.r32(c.gp - 0x7328)
        c.w8(0x6992DC, bit)
        orig.invoke(c, 0x26FFF0)
        resultados.append(dict(bit=bit, circular=c.r32(circular+0x3C), anim=c.r32(circular+0x38),
                               barra=c.r32(barra+0x3C), traza=orig.trace(c, eventos)))
    return resultados


def magic(data, sig):
    if not data.startswith(sig):
        raise ValueError(f'falta firma {sig!r}')


def hijo(amb, i, sig):
    off, size = struct.unpack_from('<II', amb, u32(amb, 0x14) + 16*i)
    if not 0 < size <= len(amb)-off:
        raise ValueError(f'hijo {i} truncado de #AMB')
    data = amb[off:off+size]
    magic(data, sig)
    return data


def radar_ydw(y):
    magic(y, b'#YDW')
    n, tabla = struct.unpack_from('<II', y, 0x58)
    if tabla + n*4 > len(y):
        raise ValueError('tabla T8 truncada')
    out = []
    for i in range(n):
        p = u32(y, tabla+4*i)
        casilla, enterrado, visible, tipo = struct.unpack_from('<HBBB', y, p)
        if tipo == 9:
            out.append(dict(i=i, casilla=casilla, enterrado=bool(enterrado), visible=bool(visible)))
    return out


def sitio_luz(amb):
    magic(amb, b'#AMB')
    amo, amm, amt = [hijo(amb, i, sig) for i, sig in ((0, b'#AMO'), (2, b'#AMM'), (12, b'#AMT'))]
    n, tabla = u32(amo, 0x10), u32(amo, 0x24)
    if tabla + n*32 > len(amo):
        raise ValueError('nombres de huesos truncados')
    nombres = [amo[p:p+32].split(b'\0', 1)[0].decode('ascii') for p in range(tabla, tabla+n*32, 32)]
    clips = []
    nclips, ct = struct.unpack_from('<II', amm, 0x10)
    if ct + nclips*16 > len(amm):
        raise ValueError('tabla #AMM truncada')
    for i in range(nclips):
        flags, _, cuadros, pistas = struct.unpack_from('<4I', amm, ct+i*16)
        stride = 12 if flags & 0x10 else 8
        huesos = {}
        for j, nombre in enumerate(nombres):
            ang, pos = struct.unpack_from('<II', amm, pistas+j*stride)
            rot, npos = (u32(amm, p+8) if p else 0 for p in (ang, pos))
            valores = [round(struct.unpack_from('<f', amm, pos+20+16*k)[0], 2)
                       for k in (0, npos-1)] if npos else []
            huesos[nombre] = dict(claves_rot=rot, claves_pos=npos, y_ini_fin=valores)
        clips.append(dict(clip=i, cuadros=cuadros, huesos=huesos))
    ntex, tt = struct.unpack_from('<II', amt, 0x10)
    if tt + ntex*4 > len(amt):
        raise ValueError('tabla #AMT truncada')
    flags = [u32(amt, u32(amt, tt+4*i)+4) for i in range(ntex)]
    return dict(clips=clips, flags_luz=[hex(f) for f in flags])


def recursos(path):
    sys.path.insert(0, str(ROOT / 'mod center hd'))
    from iso import Iso
    img = Iso(str(path))
    files = img.files()
    idx = {name.lstrip('/').upper(): name for name in files}
    inner = idx['USR/DATA_CMN.AFS']
    cab = img.read(inner, 0, 8)
    magic(cab, b'AFS\0')
    n = u32(cab, 4)
    tam = files[inner][1]
    if not 396 <= n <= (tam-8)//8:
        raise ValueError('tabla AFS fuera del contrato')
    hashes = {}

    def entrada(i):
        off, sz = struct.unpack('<II', img.read(inner, 8+8*i, 8))
        if not 8+8*n <= off <= tam or not 0 < sz <= min(tam-off, 32*1024*1024):
            raise ValueError(f'entrada AFS {i} truncada o inesperada')
        b = img.read(inner, off, sz)
        if len(b) != sz:
            raise ValueError(f'lectura corta de entrada AFS {i}')
        hashes[i] = hashlib.sha256(b).hexdigest()
        return b

    elf_key = idx['SLES_518.39']
    elf = hashlib.sha256(img.read(elf_key, 0, files[elf_key][1])).hexdigest()
    if elf != orig.EXPECTED_ELF_SHA256:
        raise ValueError('el ELF de esta ISO no es el PAL del banco')
    ydw = [radar_ydw(entrada(387+i)) for i in range(9)]
    shenron = sitio_luz(entrada(328))
    return dict(iso=str(path.resolve()), elf_sha256=elf, entradas_sha256=hashes, ydw=ydw, shenron=shenron)


def selfcheck():
    """Controles offline de lectores #YDW y #AMB/#AMO/#AMM/#AMT sin ISO ni savestates."""
    # 1. #YDW sintetico con 2 objetos T8 (uno tipo 9 radar en casilla 4 y otro tipo 1)
    ydw = bytearray(0x80)
    ydw[0:4] = b'#YDW'
    struct.pack_into('<II', ydw, 0x58, 2, 0x60)
    struct.pack_into('<II', ydw, 0x60, 0x68, 0x70)
    struct.pack_into('<HBBB', ydw, 0x68, 4, 0, 1, 9)
    struct.pack_into('<HBBB', ydw, 0x70, 7, 1, 0, 1)
    res_ydw = radar_ydw(bytes(ydw))
    if res_ydw != [dict(i=0, casilla=4, enterrado=False, visible=True)]:
        raise ValueError(f'radar_ydw sintetico inesperado: {res_ydw}')
    try:
        radar_ydw(b'BAD!' + bytes(ydw[4:]))
    except ValueError:
        pass
    else:
        raise ValueError('radar_ydw acepto firma invalida')

    # 2. #AMB sintetico con hijo 0 (#AMO), hijo 2 (#AMM) e hijo 12 (#AMT)
    amo = bytearray(0x50)
    amo[0:4] = b'#AMO'
    struct.pack_into('<I', amo, 0x10, 1)
    struct.pack_into('<I', amo, 0x24, 0x30)
    amo[0x30:0x30 + 10] = b'PBSKY_YUKA'

    amm = bytearray(0x80)
    amm[0:4] = b'#AMM'
    struct.pack_into('<II', amm, 0x10, 1, 0x20)
    struct.pack_into('<4I', amm, 0x20, 0, 0, 360, 0x30)
    struct.pack_into('<II', amm, 0x30, 0, 0x40)
    struct.pack_into('<I', amm, 0x40 + 8, 2)
    struct.pack_into('<f', amm, 0x40 + 20, 0.0)
    struct.pack_into('<f', amm, 0x40 + 20 + 16, -5000.0)

    amt = bytearray(0x40)
    amt[0:4] = b'#AMT'
    struct.pack_into('<II', amt, 0x10, 2, 0x20)
    struct.pack_into('<II', amt, 0x20, 0x28, 0x30)
    struct.pack_into('<II', amt, 0x28, 0, 0x21)
    struct.pack_into('<II', amt, 0x30, 0, 0x21)

    header_size = 0x20 + 13 * 16
    amb = bytearray(header_size)
    amb[0:4] = b'#AMB'
    struct.pack_into('<I', amb, 0x14, 0x20)
    for idx, blob in ((0, bytes(amo)), (2, bytes(amm)), (12, bytes(amt))):
        off = len(amb)
        amb.extend(blob)
        struct.pack_into('<II', amb, 0x20 + 16 * idx, off, len(blob))
    res_amb = sitio_luz(bytes(amb))
    if res_amb['flags_luz'] != ['0x21', '0x21'] or res_amb['clips'][0]['huesos']['PBSKY_YUKA']['y_ini_fin'] != [0.0, -5000.0]:
        raise ValueError(f'sitio_luz sintetico inesperado: {res_amb}')
    print('b2_diag_recursos autoprueba: OK (#YDW T8 radar + #AMB/#AMO/#AMM/#AMT + control negativo)')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-check', '--autoprueba', dest='self_check', action='store_true',
                   help='controles offline de lectores #YDW y #AMB sin ISO ni savestates')
    p.add_argument('--iso', type=Path, default=ISO_DEF)
    p.add_argument('--estado', type=Path, default=ESTADO_DEF, help='savestate PAL fase I con actores del HUD cargados')
    p.add_argument('--salida', type=Path, help='JSON nuevo (por defecto se imprime)')
    a = p.parse_args(argv)
    try:
        if a.self_check:
            selfcheck()
            return 0
        base = orig.Original(a.estado)
        data = dict(estado=base.identity, radar=radar(base), **recursos(a.iso))
        msg = json.dumps(data, ensure_ascii=True, indent=2) + '\n'
        if a.salida:
            with a.salida.open('x', encoding='utf-8') as f:
                f.write(msg)
            print(a.salida)
        else:
            print(msg, end='')
        return 0
    except (ValueError, KeyError, OSError, RuntimeError, struct.error) as e:
        print(f'ERROR: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

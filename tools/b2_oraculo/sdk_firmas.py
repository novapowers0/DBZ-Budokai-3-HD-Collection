# SPDX-License-Identifier: MIT
"""B2-1: localiza en el ELF de B2 PAL las funciones del SDK/CRI que el oraculo de IW sustituye, por firma.
Firma = primeras N instrucciones de la funcion en el ELF de IW con lo reubicable enmascarado (destino de j/jal, inmediato
de lui y de todo tipo I cuya base no sea sp). Se busca en el texto de B2; se prueba con N decreciente.

  python tools/b2_oraculo/sdk_firmas.py            -> tabla IW -> B2
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / 'iw_mips'))
import oraculo  # noqa: E402

B2_ELF = Path('E:/DBZ3HD_trabajo/ghidra/elfs/b2.elf')

BUSCAR = {
    0x105C90: 'sceSifBindRpc', 0x105E70: 'sceSifCallRpc', 0x10B658: 'sceSifRebootIop', 0x10B608: 'sceSifSyncIop',
    0x10B128: 'sceSifLoadModule', 0x102698: 'SyncDCache', 0x1025F0: 'InvalidDCache',
    0x10EB18: 'scePadRead', 0x10EB90: 'scePadGetState', 0x10EFB8: 'scePadInfoMode', 0x10ED88: 'scePadInfoAct',
    0x10F4A8: 'scePadInfoPressMode',
    0x1113D0: 'sceCdRead', 0x110998: 'sceCdSync', 0x110488: 'sceCdSearchFile', 0x111680: 'sceCdGetError',
    0x1111B0: 'sceCdDiskReady',
    0x1034F8: 'TTY (escritura DECI2)', 0x159B38: 'printf de CRI', 0x13B630: 'espera de la cola DMA (juego)',
    0x13D4D0: 'arranque Sofdec (juego)', 0x13DB50: 'estado Sofdec (juego)', 0x1498D0: 'bucle principal (juego)',
    0x142710: 'vblank+envio del cuadro (juego)', 0x140200: 'planificador de tareas (juego)',
    0x14AFE0: 'mando (juego)', 0x13E7C0: 'sonido RPC (juego)', 0x14D550: 'CD/AFS (juego)',
}


def texto(p):
    d = Path(p).read_bytes()
    phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
    for i in range(phnum):
        t, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
        if t == 1 and fsz:
            return va, d[off:off + fsz]


def mascara(w):
    op = w >> 26
    if op in (2, 3):
        return w & 0xFC000000
    if op == 15:
        return w & 0xFFFF0000
    if op >= 8 and op not in (16, 17, 18, 28) and ((w >> 21) & 31) != 29 and not 20 <= op <= 23:
        return w & 0xFFFF0000
    return w


def main():
    va_i, t_i = texto(oraculo.ELF)
    va_b, t_b = texto(B2_ELF)
    fin_b = min(len(t_b), 0x200000) // 4
    mb = [mascara(w) for w in struct.unpack_from(f'<{fin_b}I', t_b)]
    ini = sorted(oraculo.nombres())
    indice = {}
    for i, w in enumerate(mb):
        indice.setdefault(w, []).append(i)
    res = {}
    for a, nom in sorted(BUSCAR.items()):
        k = ini.index(a) if a in ini else None
        largo = min(((ini[k + 1] - a) // 4) if k is not None and k + 1 < len(ini) else 48, 64)
        fi = [mascara(w) for w in struct.unpack_from(f'<{largo}I', t_i, a - va_i)]
        hall, n = [], largo
        while n >= 6:
            hall = [i for i in indice.get(fi[0], []) if mb[i:i + n] == fi[:n]]
            if hall:
                break
            n = n * 3 // 4
        res[a] = (nom, largo, n, [va_b + 4 * i for i in hall[:6]], len(hall))
        print(f'{a:#x} {nom:34s} largo {largo:3d} coincide {n:3d} -> {[hex(x) for x in res[a][3]]}{" (+%d)" % (len(hall) - 6) if len(hall) > 6 else ""}')
    return res


if __name__ == '__main__':
    main()

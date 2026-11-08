"""Lee destinos de la tabla nativa de combate del ELF PAL B2 de la ISO.

Lectura estática: no ejecuta el juego ni demuestra que se invoque cada destino.
"""
import argparse
import hashlib
import json
import struct

from solb2x_censo import E, u


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('ids', nargs='+', type=lambda s: int(s, 0))
    ap.add_argument('--iso', default='D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
    a = ap.parse_args()
    if any(i < 0 or i > 0x163 for i in a.ids):
        ap.error('id fuera de la tabla examinada (0..0x163)')
    fuente = E.IsoB2(a.iso)
    try:
        elf = fuente.iso.read(fuente.iso.boot_elf())
    finally:
        fuente.f.close()
    digest = hashlib.sha256(elf).hexdigest()
    if digest != '30c362969ad64184df0b4a4ee498badb101265470883948f3131b6302ae3c797':
        raise ValueError('tabla sin acreditar para este ELF')

    def va(addr, size):
        ph, es, count = u(elf, 28), *struct.unpack_from('<HH', elf, 42)
        for i in range(count):
            typ, off, start, _, length, *_ = struct.unpack_from('<8I', elf, ph+i*es)
            if typ == 1 and start <= addr and addr+size <= start+length:
                return elf[off+addr-start:off+addr-start+size]
        raise ValueError(f'dirección ELF sin respaldo: {addr:X}')

    tabla = 0x375190
    entradas = []
    for i in a.ids:
        grupo = u(va(tabla+4*(i >> 4), 4), 0)
        if not grupo:
            raise ValueError(f'grupo nativo sin tabla: {i:X}')
        direccion = grupo+4*(i & 15)
        entradas.append(dict(id=hex(i), grupo=hex(grupo), direccion=hex(direccion),
                             destino=hex(u(va(direccion, 4), 0))))
    print(json.dumps(dict(elf_sha256=digest, tabla=hex(tabla), entradas=entradas), indent=2))


if __name__ == '__main__':
    main()

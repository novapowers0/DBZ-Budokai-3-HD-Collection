"""
Reconstruir un AFS de data_cmn reemplazando una entrada con un bin mas grande.

Metodo (replica data_cmn_big.afs, validado como "modo archivo completo"):
  - Header (0x10 + tabla A + tabla B + desc) se copia INTACTO hasta first_data.
  - El bin nuevo se escribe en la posicion del slot (old_loc), desplazando
    los datos posteriores.
  - Tabla A: entradas con index < idx intactas; entradas con index >= idx
    desplazadas +delta (excepto la propia idx, cuyo size cambia a len(new_bin)).
  - Los datos fisicos: [first_data, old_loc) intactos + [old_loc, ...) con el
    bin nuevo y luego el resto del archivo desde old_loc+old_sz.

Nota: asume que los datos fisicos son continuos desde first_data (con
padding entre entradas, pero sin solapamiento de regiones con distinto
contenido). Los offsets duplicados (372 en data_cmn) apuntan a la misma
region de datos, que se copia una sola vez (se desplaza igual).

Header AFS (data_cmn):
  +0x00: 'AFS\x00'
  +0x04: num entradas
  +0x08: header info size
  Tabla A en 0x10: n * 8 bytes (offset u32, size u32)
  Tabla B en 0x10+n*8: n * 8 bytes (intacta)
  Desc/padding hasta first_data (= tabla[0].loc)

Uso:
  python build_afs.py <afs_orig> <idx> <new_bin> <output_afs> [--append]

  --append: en vez de insertar el bin en el medio (desplazando las entradas
  posteriores, lo que rompe la lectura del guest cuando el delta es grande),
  coloca el bin al FINAL del archivo y solo actualiza la tabla A de la entrada
  idx. Las demas entradas mantienen su offset fisico -> el guest (que usa
  offsets hardcodeados del XEX para entradas no modificadas) no se cuelga.
"""

import struct
import sys


def u32(data, off):
    return struct.unpack('<I', data[off:off + 4])[0]


def main():
    if len(sys.argv) < 5:
        print('Uso: python build_afs.py <afs_orig> <idx> <new_bin> <output>')
        return
    afs = open(sys.argv[1], 'rb').read()
    idx = int(sys.argv[2])
    new_bin = open(sys.argv[3], 'rb').read()
    out = sys.argv[4]

    n = u32(afs, 0x04)
    base = 0x10
    first_data = u32(afs, base)  # header real
    old_loc = u32(afs, base + idx * 8)
    old_sz = u32(afs, base + idx * 8 + 4)
    append_mode = '--append' in sys.argv

    if append_mode:
        # Modo append: el bin se coloca al final del archivo. La entrada idx
        # apunta ahi; el resto del archivo queda intacto.
        # El nuevo loc debe estar alineado a 0x800 tras el ultimo byte de datos.
        # El ultimo dato real es el max(loc+size) de todas las entradas con loc!=0.
        last_end = 0
        for i in range(n):
            loc = u32(afs, base + i * 8)
            sz = u32(afs, base + i * 8 + 4)
            if loc and loc + sz > last_end:
                last_end = loc + sz
        # redondear hacia arriba a 0x800
        new_loc = ((last_end + 0x7FF) & ~0x7FF)
        delta = new_loc - old_loc
        print('AFS: %d entradas, header real=0x%X' % (n, first_data))
        print('Entrada %d: loc=0x%X size=%d -> APPEND al final (loc 0x%X, %d bytes, delta %+d)' % (
            idx, old_loc, old_sz, new_loc, len(new_bin), delta))
        table_a = bytearray()
        for i in range(n):
            loc = u32(afs, base + i * 8)
            sz = u32(afs, base + i * 8 + 4)
            if i == idx:
                loc = new_loc
                sz = len(new_bin)
            table_a += struct.pack('<II', loc, sz)
        out_b = bytearray()
        out_b += afs[:0x10]
        out_b += table_a
        out_b += afs[base + n * 8:first_data]  # tabla B + desc intactas
        assert len(out_b) == first_data, 'header size mismatch'
        out_b += afs[first_data:]  # todos los datos originales intactos
        out_b += bytes(new_loc - len(out_b))  # padding hasta new_loc
        out_b += new_bin
        with open(out, 'wb') as f:
            f.write(bytes(out_b))
        print('AFS reconstruido (append): %d bytes -> %s' % (len(out_b), out))
        return

    delta_raw = len(new_bin) - old_sz
    # Metodo validado (data_cmn_janemba.afs): la entrada idx mantiene su loc,
    # el bin crece en su lugar, y las entradas posteriores se desplazan por
    # delta redondeado. IMPORTANTE: las entradas AFS estan alineadas a 0x800
    # (verificado: 0/3990 misalineadas en el AFS real) -> el delta debe ser
    # multiplo de 0x800, no de 0x100, o el guest se cuelga al escanear.
    delta = ((delta_raw + 0x7FF) & ~0x7FF) if delta_raw > 0 else 0
    print('AFS: %d entradas, header real=0x%X' % (n, first_data))
    print('Entrada %d: loc=0x%X size=%d -> nuevo bin %d bytes (delta raw %+d, redondeado 0x800 %+d)' % (
        idx, old_loc, old_sz, len(new_bin), delta_raw, delta))

    # Construir tabla A ajustada
    table_a = bytearray()
    for i in range(n):
        loc = u32(afs, base + i * 8)
        sz = u32(afs, base + i * 8 + 4)
        if i == idx:
            sz = len(new_bin)
        elif i > idx and loc != 0:
            loc += delta
        table_a += struct.pack('<II', loc, sz)

    # Reconstruir: header (0x10 + tabla A + tabla B + desc) + datos
    out_b = bytearray()
    out_b += afs[:0x10]
    out_b += table_a
    out_b += afs[base + n * 8:first_data]  # tabla B + desc intactas
    assert len(out_b) == first_data, 'header size mismatch'

    # Datos: desde first_data hasta old_loc intactos, luego nuevo bin + padding
    # hasta el nuevo loc de la siguiente entrada, luego el resto desplazado.
    out_b += afs[first_data:old_loc]
    out_b += new_bin
    if delta_raw > 0:
        # La siguiente entrada original esta en old_loc + old_sz; ahora debe
        # estar en old_loc + len(new_bin) + padding. El padding es
        # delta - delta_raw (hasta alinear a 0x100).
        out_b += bytes(delta - delta_raw)
    out_b += afs[old_loc + old_sz:]

    with open(out, 'wb') as f:
        f.write(bytes(out_b))
    print('AFS reconstruido: %d bytes -> %s' % (len(out_b), out))


if __name__ == '__main__':
    main()

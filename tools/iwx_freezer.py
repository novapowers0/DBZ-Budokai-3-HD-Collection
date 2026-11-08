"""eIW6: lectura pura del 7D7 original por campo (esquiva de Freezer y huesos), para iwx_original_cuadros --freezer.
Globales de la esquiva (EU Char_PoseInputTransition 0x35DF50): 0x5D9D60 azimut, 0x5D9D64 elevacion, 0x5D9D6C larga,
0x5D9D70 apunta>0,95, 0x5D9D74 velocidad, 0x5D9D78 direccion, 0x5D9D7C anterior, 0x5D9D80 tiempo, 0x5D9D84 preparada,
0x5D9D88 tirada B50 hecha. Huesos: actor+0xB78 -> modelo, nombres +4 (#AMG/#AWG), matrices 64 B en +0x408."""
import struct


def leer(read, huesos=False):
    u = lambda a: struct.unpack('<I', read(a, 4))[0]
    g = read(0x5D9D60, 0x30)
    f = lambda o: struct.unpack_from('<f', g, o)[0]
    i = lambda o: struct.unpack_from('<i', g, o)[0]
    mgr = u(0x49CA70)
    d = dict(az=f(0), elev=f(4), larga=i(0xC), apunta95=i(0x10), vel=f(0x14), dir=i(0x18), dir_prev=i(0x1C),
             tiempo=f(0x20), preparada=i(0x24), tirada=i(0x28))
    for k, a in (('krillin', mgr + 0x800), ('freezer', mgr + 0x28D0)):
        e = read(a + 0xCBC, 2)
        d[k] = dict(estado=struct.unpack('<h', e)[0], codigo=u(a + 0xB60), c74=u(a + 0xC74))
    if huesos:
        a = mgr + 0x28D0
        modelo = u(a + 0xB78)
        h = u(modelo + 4)
        cab = read(h, 0x28)
        if cab[1:4] in (b'AMG', b'AWG'):
            n, no = struct.unpack_from('<I', cab, 0x10)[0], struct.unpack_from('<I', cab, 0x1C)[0]
        else:
            n, no = struct.unpack_from('<I', cab, 0x20)[0], struct.unpack_from('<I', cab, 0x24)[0]
        if no < 0x100000: no += h
        if not 1 <= n <= 128: raise ValueError('modelo de Freezer fuera de rango')
        nombres = read(no, 32 * n)
        d['huesos'] = dict(nombres=[nombres[32 * k:32 * k + 32].split(b'\0')[0].decode() for k in range(n)],
                           m=read(u(modelo + 0x408), 64 * n).hex())
    return d

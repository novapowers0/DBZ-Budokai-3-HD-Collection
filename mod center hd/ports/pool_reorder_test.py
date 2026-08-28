import struct, sys

def be32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def be16(b, o): return struct.unpack('>H', b[o:o+2])[0]
def f32i(b, o, v): b[o:o+4] = struct.pack('>f', v)
def u32i(b, o, v): b[o:o+4] = struct.pack('>I', v)
def u16i(b, o, v): b[o:o+2] = struct.pack('>H', v)

def main():
    src, dst = sys.argv[1], sys.argv[2]
    mode = sys.argv[3] if len(sys.argv) > 3 else 'reverse'
    t = bytearray(open(src, 'rb').read())
    AWG0 = 0xcc0
    sec_rel = be32(t, AWG0 + 0x34); vb2_rel = be32(t, AWG0 + 0x2C)
    sec_real = AWG0 + sec_rel + 2
    n_slots = (vb2_rel - sec_rel - 2) // 44
    ib_rel = be32(t, AWG0 + 0x30); ib = AWG0 + ib_rel
    ib_end = be32(t, AWG0 + 0x38); ib_size = (ib_end - ib_rel) // 2

    # permutacion de slots sec34
    if mode == 'reverse':
        perm = list(range(n_slots))[::-1]
    else:
        raise ValueError('mode: reverse')
    inv = [0] * n_slots
    for i, p in enumerate(perm):
        inv[p] = i

    # reescribir los slots del pool (copiar a un buffer nuevo)
    old_pool = bytes(t[sec_real:sec_real + n_slots * 44])
    for i in range(n_slots):
        o = sec_real + i * 44
        t[o:o + 44] = old_pool[perm[i] * 44:perm[i] * 44 + 44]

    # remapear el IB (solo indices sec34)
    for k in range(ib_size):
        idx = be16(t, ib + 2 * k)
        if idx < n_slots:
            u16i(t, ib + 2 * k, inv[idx])

    # recomputar A/B de los descriptores (solo los que apuntan a este IB)
    desc = 0x2EA9
    n_desc = 36
    for i in range(n_desc):
        d = desc + i * 0x60
        B = be32(t, d + 0x58) >> 8; Bc = be32(t, d + 0x5C) >> 8
        if Bc == 0 or B + Bc > ib_size:
            continue
        idxs = [be16(t, ib + 2 * (B + j)) for j in range(Bc)]
        mn, mx = min(idxs), max(idxs)
        u32i(t, d + 0x50, (mn << 8) | (be32(t, d + 0x50) & 0xFF))
        u32i(t, d + 0x54, (((mx - mn + 1) << 8) | (be32(t, d + 0x54) & 0xFF)))
    open(dst, 'wb').write(bytes(t))
    print('ok: pool invertido, IB remapeado, A/B recomputados (%d slots, %d indices)' % (n_slots, ib_size))

if __name__ == '__main__':
    main()
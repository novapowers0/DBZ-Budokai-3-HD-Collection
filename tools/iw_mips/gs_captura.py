# SPDX-License-Identifier: MIT
"""TRAD/1: captura del dibujo del oraculo. Cada envio de DMA del juego al GS (canal 2, GIF) o al VIF1 (canal 1) se recorre
(cadena de DMA incluida) y se traduce a un guion de ordenes por cuadro:

  GIF (y los DIRECT del VIF1): escrituras de registros del GS ya resueltas -> ['G', reg, valor64] (PRIM, RGBAQ, ST, UV,
       XYZ2/XYZF2, TEX0, ALPHA, TEST, ZBUF, SCISSOR...) y subidas de imagen -> ['I', qwords, md5]
  VIF1: lo que alimenta a la VU1 (la geometria 3D la transforma el microprograma, no pasa por aqui como primitivas):
       ['U', dir, num, formato, usn/flg, datos_hex|md5] UNPACK, ['M', dir] MSCAL/MSCNT, ['P', dir, num, md5] MPG,
       ['V', cmd, num, imm] el resto con efecto (STCYCL, OFFSET, BASE, ITOP, STMOD, STMASK, STROW, STCOL)

Un cuadro = lo enviado entre dos vblanks. `volcar` escribe un JSONL: {"n": vblank, "ordenes": [...], "huella": md5}.
"""
import hashlib
import json
import struct

M64 = (1 << 64) - 1
DATOS_CORTOS = 96        # palabras: los UNPACK hasta este tamano se guardan enteros (matrices, cabeceras); el resto, md5


class Captura:
    def __init__(s, c, reloj, desde=0):
        s.c, s.reloj, s.desde = c, reloj, desde
        s.cuadros = {}           # vblank -> lista de ordenes
        s.cl, s.wl = 1, 1        # STCYCL del VIF1
        s.q = 0x3F800000         # Q pendiente (ST -> RGBAQ)
        s.errores = []

    # ---------------- DMA ----------------
    def leer(s, a, n):
        c = s.c
        return b''.join(struct.pack('<Q', c.r64(a + k)) for k in range(0, n, 8))

    def cadena(s, chcr, madr, qwc, tadr, vif):
        """Devuelve los datos (bytes) que salen por el canal. Modo normal: QWC qwords desde MADR. Cadena de origen: sigue
        las etiquetas desde TADR; con TTE (bit 6) la mitad alta de cada etiqueta tambien viaja (son codigos VIF)."""
        modo = (chcr >> 2) & 3
        if modo == 0:
            return s.leer(madr, (qwc & 0xFFFF) * 16)
        tte = bool(chcr & 0x40) and vif
        out, pila, t = [], [], tadr
        for _ in range(100_000):
            tag = s.c.r64(t)
            n, ident, addr = tag & 0xFFFF, (tag >> 28) & 7, (tag >> 32) & 0x7FFFFFFF
            if tag >> 63:                           # bit SPR: la direccion es del scratchpad
                addr = 0x70000000 | (addr & 0x3FFF)
            if tte:
                out.append(struct.pack('<Q', s.c.r64(t + 8)))
            if ident in (0, 3, 4):                  # refe / ref / refs: datos en addr
                out.append(s.leer(addr, n * 16))
                t += 16
                if ident == 0:
                    break
            elif ident == 1:                        # cnt: datos tras la etiqueta, sigue despues
                out.append(s.leer(t + 16, n * 16))
                t += 16 + n * 16
            elif ident == 2:                        # next: datos tras la etiqueta, sigue en addr
                out.append(s.leer(t + 16, n * 16))
                t = addr
            elif ident == 5:                        # call
                out.append(s.leer(t + 16, n * 16))
                pila.append(t + 16 + n * 16)
                t = addr
            elif ident == 6:                        # ret
                out.append(s.leer(t + 16, n * 16))
                if not pila:
                    break
                t = pila.pop()
            else:                                   # end
                out.append(s.leer(t + 16, n * 16))
                break
        else:
            s.errores.append(('cadena sin fin', hex(tadr)))
        return b''.join(out)

    def __call__(s, canal, chcr, madr, qwc, tadr):
        if s.reloj.n < s.desde:
            return
        o = s.cuadros.setdefault(s.reloj.n, [])
        datos = s.cadena(chcr, madr, qwc, tadr, canal == 1)
        if canal == 2:
            s.gif(datos, o)
        else:
            s.vif(datos, o)

    # ---------------- GIF ----------------
    def gif(s, d, o):
        p, n = 0, len(d)
        while p + 16 <= n:
            lo, hi = struct.unpack_from('<QQ', d, p)
            p += 16
            nloop, eop, pre, prim, flg, nreg = lo & 0x7FFF, (lo >> 15) & 1, (lo >> 46) & 1, (lo >> 47) & 0x7FF, (lo >> 58) & 3, (lo >> 60) & 15
            nreg = nreg or 16
            regs = [(hi >> (4 * i)) & 15 for i in range(nreg)]
            if pre and flg != 2:
                o.append(['G', 0, prim])
            if flg == 0:                            # PACKED
                for _ in range(nloop):
                    for r in regs:
                        if p + 16 > n:
                            s.errores.append('gif corto')
                            return
                        a, b = struct.unpack_from('<QQ', d, p)
                        p += 16
                        s.empaquetado(r, a, b, o)
            elif flg == 1:                          # REGLIST
                for k in range(nloop * nreg):
                    if p + 8 > n:
                        s.errores.append('gif corto')
                        return
                    v = struct.unpack_from('<Q', d, p)[0]
                    p += 8
                    if regs[k % nreg] != 15:
                        o.append(['G', regs[k % nreg], v])
                p = (p + 15) & ~15
            else:                                   # IMAGE
                tam = nloop * 16
                o.append(['I', nloop, hashlib.md5(d[p:p + tam]).hexdigest()])
                p += tam

    def empaquetado(s, r, a, b, o):
        if r == 0:
            o.append(['G', 0, a & 0x7FF])
        elif r == 1:
            o.append(['G', 1, (a & 0xFF) | ((a >> 32) & 0xFF) << 8 | (b & 0xFF) << 16 | ((b >> 32) & 0xFF) << 24 | s.q << 32])
        elif r == 2:
            s.q = b & 0xFFFFFFFF
            o.append(['G', 2, a])
        elif r == 3:
            o.append(['G', 3, (a & 0x3FFF) | ((a >> 32) & 0x3FFF) << 16])
        elif r == 4:
            v = (a & 0xFFFF) | ((a >> 32) & 0xFFFF) << 16 | ((b >> 4) & 0xFFFFFF) << 32 | ((b >> 36) & 0xFF) << 56
            o.append(['G', 0xC if (b >> 47) & 1 else 4, v])
        elif r == 5:
            v = (a & 0xFFFF) | ((a >> 32) & 0xFFFF) << 16 | (b & 0xFFFFFFFF) << 32
            o.append(['G', 0xD if (b >> 47) & 1 else 5, v])
        elif r == 14:
            o.append(['G', b & 0xFF, a])
        elif r != 15:
            o.append(['G', r, a])

    # ---------------- VIF1 ----------------
    def vif(s, d, o):
        w = struct.unpack_from(f'<{len(d) // 4}I', d)
        p, n = 0, len(w)
        while p < n:
            code = w[p]
            p += 1
            cmd, num, imm = (code >> 24) & 0x7F, (code >> 16) & 0xFF, code & 0xFFFF
            if cmd == 0:
                continue
            if cmd == 1:
                s.cl, s.wl = imm & 0xFF, imm >> 8
                o.append(['V', cmd, num, imm])
            elif cmd in (2, 3, 4, 5):
                o.append(['V', cmd, num, imm])
            elif cmd in (6, 7, 0x10, 0x11, 0x13):
                pass
            elif cmd in (0x14, 0x15):
                o.append(['M', imm])
            elif cmd == 0x17:
                o.append(['M', -1])
            elif cmd == 0x20:
                o.append(['V', cmd, 0, w[p]])
                p += 1
            elif cmd in (0x30, 0x31):
                o.append(['V', cmd, 0, list(w[p:p + 4])])
                p += 4
            elif cmd == 0x4A:
                k = (num or 256) * 2
                o.append(['P', imm, num or 256, hashlib.md5(struct.pack(f'<{k}I', *w[p:p + k])).hexdigest()])
                p += k
            elif cmd in (0x50, 0x51):
                k = (imm or 65536) * 4
                s.gif(struct.pack(f'<{len(w[p:p + k])}I', *w[p:p + k]), o)
                p += k
            elif cmd >= 0x60:
                vn, vl = (cmd >> 2) & 3, cmd & 3
                nn = num or 256
                elems = nn if s.wl <= s.cl or s.wl == 0 else s.cl * (nn // s.wl) + min(nn % s.wl, s.cl)
                bits = 16 if (vn, vl) == (3, 3) else (32 >> vl) * (vn + 1)
                k = (elems * bits + 31) // 32
                dat = w[p:p + k]
                p += k
                crudo = struct.pack(f'<{len(dat)}I', *dat)
                o.append(['U', imm & 0x3FF, nn, cmd & 0xF, (imm >> 14) | ((cmd >> 4) & 1) << 2,
                          crudo.hex() if k <= DATOS_CORTOS else 'md5:' + hashlib.md5(crudo).hexdigest()])
            else:
                s.errores.append(('vif desconocido', hex(code)))
                return

    # ---------------- salida ----------------
    def resumen(s):
        r = {}
        for n, o in sorted(s.cuadros.items()):
            cuenta = {}
            for x in o:
                k = x[0] if x[0] != 'G' else f'G{x[1]:02x}'
                cuenta[k] = cuenta.get(k, 0) + 1
            r[n] = cuenta
        return r

    def volcar(s, ruta):
        with open(ruta, 'w', encoding='utf-8') as f:
            for n, o in sorted(s.cuadros.items()):
                txt = json.dumps(o, separators=(',', ':'))
                f.write(json.dumps({'n': n, 'huella': hashlib.md5(txt.encode()).hexdigest(), 'ordenes': o},
                                   separators=(',', ':')) + '\n')

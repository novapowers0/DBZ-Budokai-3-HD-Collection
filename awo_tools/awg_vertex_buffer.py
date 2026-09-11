#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""awg_vertex_buffer.py - Modelo REAL del vertex buffer del B3 HD (Vía B).

==============================================================================
MODELO VERIFICADO (en juego, 2026-09-11 / semántica fijada 2026-09-12)
==============================================================================
El #AWG0 (cuerpo) NO usa "sec34/vb2 + descriptores A/B" para dibujar. El GPU lee
una region CONTIGUA de bytes copiada VERBATIM del fichero:

    region = [vb0, ib)          (ib = AWG0 + g(0x30))
    vb0    = ib - N*44          (N = max(indice del IB) + 1)
    stride = 44 bytes por vertice ("ventana" autocontenida)

Cada VENTANA de 44 B (offsets confirmados por el vfetch del shader):

    +0   pos.x  +4 pos.y  +8 pos.z   (3f BE)   fmt 57   BONE-LOCAL, orden (x,y,z)
    +12  weight (f BE)                          fmt 36
    +16  bone   (u32 BE; el shader lee 1 byte)  fmt 6 (8_8_8_8)
    +20  nrm.x  +24 nrm.y  +28 nrm.z (3f BE)    fmt 57   BONE-LOCAL, orden (x,y,z)
    +32  0xFFFFFFFF
    +36  uv.x   +40 uv.y   (2f BE)              fmt 37

El IB (g(0x30), u16 BE) referencia DIRECTAMENTE indices de ventana (0..N-1).
La primitiva del cuerpo es **kTriangleList (prim=4)**: cada 3 indices u16 BE
consecutivos = un triangulo. (Otras AWG/partes pueden usar strip, prim=6.)

=======================  SEMANTICA (decisiva, 2026-09-12)  ====================
Comparando el HD e147 con su equivalente PS2 (cell_extract2.json, mismo modelo):
  * pos ventana  == inv(world[bone])·model   (bone-local), orden NATURAL (x,y,z)
      - HD c0[-3.31,6.21]=PS2 c0, HD c2[-3.37,2.33]=PS2 c2 (exacto)
  * nrm ventana  == inv(world[bone]).R · model_nrm, orden NATURAL (x,y,z)
  * bone (+16)   = indice de hueso HD; weight (+12) = peso del skin PS2
  * world[bone]  = matriz bind del eje (quat+pos, 80B/hueso, parent +0x40)
  ⇒ NO hay permutacion (z,x,y) ni (nz,-ny,nx): eso era la vista `sec34`
    desalineada +428 B. La inyeccion Vía A permutaba por eso -> solo "reconocible".

Pruebas (docs/07_ports/SESION_GPU_DRAW_2026-09-11.md):
  * buffer GPU == file[vb0:ib] (match 32307/32307 dwords).
  * Permutar ventanas + remapear el IB (T11) -> identidad TOTAL (geo+textura).

==============================================================================
Uso
==============================================================================
  python awg_vertex_buffer.py info    <bin>
  python awg_vertex_buffer.py permute <in> <out> [--reverse | --swap I J]
  python awg_vertex_buffer.py roundtrip <in> <out>
  python awg_vertex_buffer.py grow    <in> <out> <N_nuevo>   # crece la region
  python awg_vertex_buffer.py selftest <bin>                 # forward/inverse

API:
  avb = AwgVertexBuffer.load(path)
  avb.vertices / avb.indices / avb.bone_count() / avb.bone_labels()
  avb.bind_worlds()                       # (worlds, parents); world = list 4x4
  AwgVertexBuffer.window_from_model(p, n, bone, weight, uv, invw)
  avb.emit(out_path, vertices=None, indices=None)     # N <= capacidad
  avb.grow(new_n)                         # devuelve un NUEVO AwgVertexBuffer
"""
import struct
import sys

WINDOW = 44


def be32(b, o): return struct.unpack(">I", b[o:o + 4])[0]
def be16(b, o): return struct.unpack(">H", b[o:o + 2])[0]
def be_i16(b, o): return struct.unpack(">h", b[o:o + 2])[0]
def be_f(b, o): return struct.unpack(">f", b[o:o + 4])[0]
def set32(b, o, v): struct.pack_into(">I", b, o, v & 0xFFFFFFFF)


def qmat(qx, qy, qz, qw, px, py, pz):
    x, y, z, w = qx, qy, qz, qw
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), px],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), py],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), pz],
            [0.0, 0.0, 0.0, 1.0]]


def mmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)]
            for i in range(4)]


def mvec(m, v):
    return [sum(m[i][k] * v[k] for k in range(4)) for i in range(4)]


def minv_affine(m):
    """Inversa de una 4x4 afin (fila inferior 0,0,0,1)."""
    r = [row[:3] for row in m[:3]]
    t = [m[i][3] for i in range(3)]
    # inversa 3x3
    a, b, c = r[0]
    d, e, f = r[1]
    g, h, i = r[2]
    A = e * i - f * h
    B = -(d * i - f * g)
    C = d * h - e * g
    det = a * A + b * B + c * C
    if abs(det) < 1e-12:
        return None
    inv = [[A / det, -(b * i - c * h) / det, (b * f - c * e) / det],
           [B / det, (a * i - c * g) / det, -(a * f - c * d) / det],
           [C / det, -(a * h - b * g) / det, (a * e - b * d) / det]]
    it = [-sum(inv[k][j] * t[j] for j in range(3)) for k in range(3)]
    return [inv[k] + [it[k]] for k in range(3)] + [[0.0, 0.0, 0.0, 1.0]]


class AwgVertexBuffer:
    def __init__(self, data, awg0, vb0, n, ib_abs, n_ib, sec_rel, vb2_rel,
                 end_rel, mg_rel, mg_size, awg_tbl, n_awg):
        self.data = bytearray(data)
        self.awg0 = awg0
        self.vb0 = vb0
        self.n = n
        self.ib_abs = ib_abs
        self.n_ib = n_ib
        self.sec_rel = sec_rel
        self.vb2_rel = vb2_rel
        self.end_rel = end_rel
        self.mg_rel = mg_rel
        self.mg_size = mg_size
        self.awg_tbl = awg_tbl
        self.n_awg = n_awg

    @classmethod
    def _parse(cls, data):
        b = data
        awo = 0x40
        n_awg = be32(b, awo + 0x18)
        awg_tbl = awo + be32(b, awo + 0x1C)
        awg0 = awo + be32(b, awg_tbl)
        def g(o): return be32(b, awg0 + o)
        sec_rel, vb2_rel, ib_rel, end_rel = g(0x34), g(0x2C), g(0x30), g(0x38)
        mg_rel, mg_size = g(0x20), g(0x28)
        ib_abs = awg0 + ib_rel
        end_abs = awg0 + end_rel
        n_ib = (end_abs - ib_abs) // 2
        ib = [be16(b, ib_abs + k * 2) for k in range(n_ib)]
        # g(0x2C) = TAMANO del buffer de ventanas en bytes (NO un offset). El
        # guest fija el fetch con ese tamano -> es la fuente fiable de n. (El
        # maximo indice del IB solo vale si el IB usa todas las ventanas; en un
        # fichero crecido con `grow` el IB puede no referenciar el tramo nuevo,
        # y el loader antiguo calculaba mal vb0.)
        n = vb2_rel // WINDOW
        if n <= 0:
            maxv = max((v for v in ib if v != 0xFFFF), default=-1)
            n = maxv + 1
        vb0 = ib_abs - n * WINDOW
        return cls(bytes(b), awg0, vb0, n, ib_abs, n_ib, sec_rel, vb2_rel,
                   end_rel, mg_rel, mg_size, awg_tbl, n_awg)

    @classmethod
    def load(cls, path):
        return cls._parse(open(path, "rb").read())

    # ---------------- read ----------------
    def raw_window(self, w):
        o = self.vb0 + w * WINDOW
        return bytes(self.data[o:o + WINDOW])

    def vertices(self):
        out = []
        for w in range(self.n):
            r = self.data[self.vb0 + w * WINDOW:self.vb0 + w * WINDOW + WINDOW]
            out.append({
                "pos": struct.unpack(">3f", r[0:12]),
                "w": struct.unpack(">f", r[12:16])[0],
                "bone": be32(r, 16),
                "nrm": struct.unpack(">3f", r[20:32]),
                "marker": be32(r, 32),
                "uv": struct.unpack(">2f", r[36:44]),
            })
        return out

    def indices(self):
        return [be16(self.data, self.ib_abs + k * 2) for k in range(self.n_ib)]

    def bone_count(self):
        return be32(self.data, self.awg0 + 0x10)

    def bone_labels(self):
        """Labels de hueso en AWG0+0x40, stride 32."""
        nb = self.bone_count()
        o = self.awg0 + 0x40
        return [self.data[o + i * 32:o + i * 32 + 32].split(b"\x00")[0]
                .decode("latin1") for i in range(nb)]

    def bind_worlds(self):
        """Matrices bind (world) por hueso desde los ejes (80B, quat+pos, parent
        = (AWG0+poff-axes_base)//80). Devuelve (worlds, parents)."""
        axes_base = self.awg0 + be32(self.data, self.awg0 + 0x14)
        nb = self.bone_count()
        loc, par = [], []
        for i in range(nb):
            o = axes_base + i * 80
            q = (be_f(self.data, o), be_f(self.data, o + 4),
                 be_f(self.data, o + 8), be_f(self.data, o + 12))
            p = (be_f(self.data, o + 16), be_f(self.data, o + 20),
                 be_f(self.data, o + 24))
            poff = be32(self.data, o + 0x40)
            pidx = (self.awg0 + poff - axes_base) // 80 if poff else -1
            loc.append(qmat(q[0], q[1], q[2], q[3], p[0], p[1], p[2]))
            par.append(pidx)
        world = [None] * nb
        for i in range(nb):
            pr = par[i]
            if 0 <= pr < nb and pr != i and world[pr] is not None:
                world[i] = mmul(world[pr], loc[i])
            else:
                world[i] = [row[:] for row in loc[i]]
        return world, par

    # ---------------- build ----------------
    @staticmethod
    def pack_window(v):
        r = bytearray(WINDOW)
        struct.pack_into(">3f", r, 0, *v["pos"])
        struct.pack_into(">f", r, 12, v.get("w", 1.0))
        struct.pack_into(">I", r, 16, int(v.get("bone", 0)) & 0xFFFFFFFF)
        struct.pack_into(">3f", r, 20, *v["nrm"])
        struct.pack_into(">I", r, 32, 0xFFFFFFFF)
        struct.pack_into(">2f", r, 36, *v["uv"])
        return bytes(r)

    @staticmethod
    def window_from_model(p, n, bone, weight, uv, invw):
        """Model-space -> ventana. invw = inversa del world del hueso (4x4)."""
        L = mvec(invw, [p[0], p[1], p[2], 1.0])
        Rn = [invw[i][0] * n[0] + invw[i][1] * n[1] + invw[i][2] * n[2]
              for i in range(3)]
        ln = (Rn[0] ** 2 + Rn[1] ** 2 + Rn[2] ** 2) ** 0.5
        if ln > 1e-12:
            Rn = [c / ln for c in Rn]
        return {"pos": (L[0], L[1], L[2]), "w": float(weight), "bone": int(bone),
                "nrm": (Rn[0], Rn[1], Rn[2]), "uv": (float(uv[0]), float(uv[1]))}

    def emit(self, out_path, vertices=None, indices=None):
        """Reescribe la region de ventanas y el IB (mismo N). Si vertices tiene
        menos de N entradas, se rellenan con la primera. El IB se rellena 0xFFFF."""
        verts = self.vertices() if vertices is None else vertices
        idx = self.indices() if indices is None else indices
        if len(verts) > self.n:
            raise ValueError("vertices=%d > capacidad de ventanas=%d (usa grow)"
                             % (len(verts), self.n))
        for w in range(self.n):
            v = verts[w] if w < len(verts) else verts[0]
            self.data[self.vb0 + w * WINDOW:self.vb0 + w * WINDOW + WINDOW] = \
                self.pack_window(v)
        n_ib = min(len(idx), self.n_ib)
        for k in range(n_ib):
            struct.pack_into(">H", self.data, self.ib_abs + k * 2,
                             int(idx[k]) & 0xFFFF)
        for k in range(n_ib, self.n_ib):
            struct.pack_into(">H", self.data, self.ib_abs + k * 2, 0xFFFF)
        open(out_path, "wb").write(bytes(self.data))

    # ---------------- grow ----------------
    def grow(self, new_n, new_nib=None):
        """Expande la region de ventanas (capacidad new_n >= n) y opcionalmente
        el IB (new_nib >= n_ib). Reconstruye [vb0, end) como
        [ventanas new_n*44][IB new_nib*2] y desplaza el resto (AWGs posteriores).
        Las ventanas viejas se conservan al inicio; las nuevas van a cero.
        Devuelve un NUEVO objeto con n=new_n, n_ib=new_nib.
        Despues: emite las new_n ventanas y un IB de new_nib indices (max = new_n-1
        para que N se derive bien)."""
        if new_nib is None:
            new_nib = self.n_ib
        if new_n < self.n:
            raise ValueError("new_n=%d < n=%d" % (new_n, self.n))
        if new_nib < self.n_ib:
            raise ValueError("new_nib=%d < n_ib=%d" % (new_nib, self.n_ib))
        old_win = self.ib_abs - self.vb0            # bytes de ventanas actuales
        region = bytearray(new_n * WINDOW)
        region[:old_win] = self.data[self.vb0:self.ib_abs]
        ib = bytearray(self.data[self.ib_abs:self.ib_abs + self.n_ib * 2])
        ib += b"\xff\xff" * (new_nib - self.n_ib)
        tail = self.data[self.ib_abs + self.n_ib * 2:]
        new_data = bytearray(self.data[:self.vb0]) + region + ib + tail
        total_delta = len(region) - old_win + (new_nib - self.n_ib) * 2
        new_ib_abs = self.vb0 + new_n * WINDOW
        new_end_abs = new_ib_abs + new_nib * 2
        b = new_data
        awo = 0x40
        tbl = awo + be32(b, awo + 0x1C)
        n_awg = be32(b, awo + 0x18)
        old_end_abs = self.ib_abs + self.n_ib * 2
        # AWG0 +0x2C = TAMANO del buffer de vertices en bytes (2948*44=129712 en
        # la plantilla). El guest fija el fetch con ese tamano -> hay que
        # actualizarlo o solo lee las ventanas viejas (bug 2026-09-12: geometria
        # del port correcta pero render "explotado").
        set32(b, self.awg0 + 0x2C, new_n * WINDOW)
        # AWG0 +0x34 = TAMANO del IB en bytes (== 2*n_ib en todos los AWGs de la
        # plantilla: AWG1 816=2*408, AWG2 912=2*456, ...). El guest dimensiona su
        # copia del IB con este campo -> si no se actualiza solo sirve el IB viejo
        # (12602 B = 6301 idx) y el resto de indices es BASURA (0xAAAA/0xFFFF) ->
        # vertex fetch fuera de rango -> "explosion". Bug 2026-09-13 (no era el
        # skin: con geometria/draw correctos, este unico campo rompia el render).
        set32(b, self.awg0 + 0x34, new_nib * 2)
        # AWG0: ib_rel / end_rel (relativos a awg0)
        set32(b, self.awg0 + 0x30, new_ib_abs - self.awg0)
        set32(b, self.awg0 + 0x38, new_end_abs - self.awg0)
        # tabla AWG: entradas que apuntan al tail desplazado (relativas a awo)
        for i in range(n_awg):
            off = be32(b, tbl + i * 4)
            if awo + off >= old_end_abs:
                set32(b, tbl + i * 4, off + total_delta)
        # cabecera #AMB: offsets absolutos (AZT y vecinos)
        for o in range(0, awo, 4):
            v = be32(b, o)
            if old_end_abs <= v < len(b) - total_delta:
                set32(b, o, v + total_delta)
        # cabecera #AWO [awo, awg0): punteros absolutos (tabla por-hueso en
        # +0x34, stride 0x20, y similares). ⚠️ La tabla AWG (relativa a awo)
        # vive DENTRO de [awo, awg0) y ya se ajusto arriba: hay que EXCLUIRLA o
        # sus valores (relativos) coinciden con el rango absoluto y se ajustan
        # DOS veces (bug detectado 2026-09-12 -> crash parser #AMB).
        tbl_end = tbl + n_awg * 4
        for o in range(awo, self.awg0 - 2, 4):
            if tbl <= o < tbl_end:
                continue
            v = be32(b, o)
            if old_end_abs <= v < len(b) - total_delta:
                set32(b, o, v + total_delta)
        o = AwgVertexBuffer.__new__(AwgVertexBuffer)
        o.data = b
        o.awg0 = self.awg0
        o.vb0 = self.vb0
        o.n = new_n
        o.ib_abs = new_ib_abs
        o.n_ib = new_nib
        o.sec_rel = self.sec_rel
        o.vb2_rel = self.vb2_rel
        o.end_rel = new_end_abs - self.awg0
        o.mg_rel = self.mg_rel
        o.mg_size = self.mg_size
        o.awg_tbl = self.awg_tbl
        o.n_awg = self.n_awg
        return o


def cmd_info(path):
    a = AwgVertexBuffer.load(path)
    print("awg0=%d  ventanas N=%d  vb0=%d (awg0+%d)  ib_abs=%d  n_ib=%d  huesos=%d"
          % (a.awg0, a.n, a.vb0, a.vb0 - a.awg0, a.ib_abs, a.n_ib, a.bone_count()))
    vs = a.vertices()
    bones = {}
    for v in vs:
        bones[v["bone"]] = bones.get(v["bone"], 0) + 1
    print("huesos usados: %d -> %s" % (len(bones), sorted(bones.items(), key=lambda x: -x[1])[:10]))
    print("labels[:6]:", a.bone_labels()[:6])
    print("primera ventana:", vs[0])
    idx = [v for v in a.indices() if v != 0xFFFF]
    print("IB: min=%d max=%d  (primeros 12: %s)" % (min(idx), max(idx), idx[:12]))


def cmd_permute(inp, out, reverse=True, swap=None):
    a = AwgVertexBuffer.load(inp)
    n = a.n
    if swap is not None:
        perm = list(range(n))
        perm[swap[0]], perm[swap[1]] = swap[1], swap[0]
    else:
        perm = [n - 1 - w for w in range(n)]
    new = bytearray(n * WINDOW)
    for w in range(n):
        new[w * WINDOW:(w + 1) * WINDOW] = a.raw_window(perm[w])
    a.data[a.vb0:a.vb0 + n * WINDOW] = new
    idx = a.indices()
    idx = [0xFFFF if v == 0xFFFF else perm[v] for v in idx]
    a.emit(out, indices=idx)
    print("permute OK -> %s (N=%d, IB=%d)" % (out, n, len(idx)))


def cmd_selftest(path):
    """Forward (ventana->modelo) e inverse (modelo->ventana) deben reproducir el
    bin exactamente -> valida bind_worlds + window_from_model."""
    a = AwgVertexBuffer.load(path)
    world, par = a.bind_worlds()
    inv = [minv_affine(w) for w in world]
    if any(x is None for x in inv):
        print("SELFTEST FAIL: world singular"); return 1
    src = a.vertices()
    out = []
    for v in src:
        b = v["bone"]
        p = mvec(world[b], [v["pos"][0], v["pos"][1], v["pos"][2], 1.0])
        n = [world[b][i][0] * v["nrm"][0] + world[b][i][1] * v["nrm"][1] +
             world[b][i][2] * v["nrm"][2] for i in range(3)]
        out.append(AwgVertexBuffer.window_from_model(
            p, n, b, v["w"], v["uv"], inv[b]))
    mx = 0.0
    for s, o in zip(src, out):
        mx = max(mx, max(abs(s["pos"][i] - o["pos"][i]) for i in range(3)),
                 max(abs(s["nrm"][i] - o["nrm"][i]) for i in range(3)))
    # world[0] debe ser identidad
    w0 = world[0]
    ident = all(abs(w0[i][j] - (1.0 if i == j else 0.0)) < 1e-6
                for i in range(4) for j in range(4))
    ok = mx < 1e-3 and ident
    print("SELFTEST forward/inverse: max_diff=%.2e  world[0]=I:%s  -> %s"
          % (mx, ident, "OK" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    if cmd == "info":
        cmd_info(sys.argv[2])
    elif cmd == "permute":
        swap = None
        reverse = "--reverse" in sys.argv or "--swap" not in sys.argv
        if "--swap" in sys.argv:
            i = sys.argv.index("--swap")
            swap = (int(sys.argv[i + 1]), int(sys.argv[i + 2]))
        cmd_permute(sys.argv[2], sys.argv[3], reverse=reverse, swap=swap)
    elif cmd == "roundtrip":
        a = AwgVertexBuffer.load(sys.argv[2])
        a.emit(sys.argv[3])
        print("roundtrip OK ->", sys.argv[3])
    elif cmd == "grow":
        a = AwgVertexBuffer.load(sys.argv[2])
        new_n = int(sys.argv[4])
        new_nib = int(sys.argv[5]) if len(sys.argv) > 5 else None
        b = a.grow(new_n, new_nib)
        vs = b.vertices()
        for w in range(a.n, b.n):
            vs[w] = vs[0]
        idx = list(b.indices())
        if idx:
            idx[-1] = b.n - 1
        b.emit(sys.argv[3], vertices=vs, indices=idx)
        c = AwgVertexBuffer.load(sys.argv[3])
        print("grow OK -> %s (N %d->%d, n_ib %d->%d, vb0=%d, reload N=%d)"
              % (sys.argv[3], a.n, b.n, a.n_ib, b.n_ib, c.vb0, c.n))
    elif cmd == "selftest":
        return cmd_selftest(sys.argv[2])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

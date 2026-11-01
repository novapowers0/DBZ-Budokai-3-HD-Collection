#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xeno_model.py - lector de piezas de Dragon Ball Xenoverse (CPK del jugador) para el creador del Modo Heroe (studies 9b).

  Cpk(ruta_data_cpk).get("data/chara/HUM/HUM_300_Bust.emd") -> bytes (CRILAYLA descomprimido)
  leer_emd(bytes)  -> [Sub] con pos/nrm (n,3) YA en el espacio de B3 (z espejada, x10), uv, indices de hueso y pesos
                      (4 por vertice; los 3 primeros guardados, el 4.o es el resto, como SDBH), nombres de hueso por vertice
  Esk(bytes)       -> names, parent, world (posiciones de bind), world_matrix (TRS jerarquica, espacio B3)
  leer_emb(bytes)  -> [(nombre, RGBA uint8)]
Los EMD de Xenoverse llevan los vertices COMPRIMIDOS (flag 0x8000): pos 3 f32, normal 4 f16, uv 2 f16, 4 u8 de hueso y
3 f16 de peso (36 B). Los sin comprimir son los de sdbh_model (48 B). No se guarda nada del juego en el repo.
"""
import io
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import cri       # noqa: E402
import crilayla  # noqa: E402
from xeno_binary import Reader, rejects, write_json  # noqa: E402

MIRROR = np.diag([1.0, 1.0, -1.0])
SCALE = 10.0
RUTA_STEAM = r"D:\SteamLibrary\steamapps\common\DB Xenoverse\data.cpk"


def buscar_cpk(xv2=False):
    """data.cpk de Xenoverse del jugador: DBZ3_XENO_CPK, la carpeta de Steam o las bibliotecas habituales."""
    if xv2:
        from xeno_importar import buscar_xv2
        return buscar_xv2()
    game = "DB Xenoverse 2" if xv2 else "DB Xenoverse"
    tail = r"cpk\data.cpk" if xv2 else "data.cpk"
    cands = [os.environ.get("DBZ3_XV2_CPK" if xv2 else "DBZ3_XENO_CPK", "")]
    if not xv2:
        cands.append(RUTA_STEAM)
    for drv in "CDEFG":
        cands.append(r"%s:\SteamLibrary\steamapps\common\%s\%s" % (drv, game, tail))
        cands.append(r"%s:\Program Files (x86)\Steam\steamapps\common\%s\%s" % (drv, game, tail))
    return next((c for c in cands if c and os.path.isfile(c)), None)


class Cpk:
    def __init__(self, ruta=None):
        self.ruta = ruta or buscar_cpk()
        if not self.ruta:
            raise FileNotFoundError("no encuentro el data.cpk de Dragon Ball Xenoverse (DBZ3_XENO_CPK)")
        self.toc = {n: (s, o) for n, s, o in cri.cpk_toc(self.ruta)}
        self.f = open(self.ruta, "rb")

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def get(self, nombre):
        s, o = self.toc[nombre]
        self.f.seek(o)
        data = self.f.read(s)
        if len(data) != s:
            raise ValueError("CPK: entrada truncada: %s" % nombre)
        return descomprimir(data)

    def buscar(self, patron):
        import re
        r = re.compile(patron)
        return sorted(n for n in self.toc if r.search(n))


def descomprimir(data):
    if data[:8] != b"CRILAYLA":
        return data
    r = Reader(data, "CRILAYLA")
    size, offset = r.unpack("<II", 8)
    r.span(16 + offset, 256)
    if len(data) != 16 + offset + 256 or (size and not offset):
        raise ValueError("CRILAYLA: cabecera/tamano inconsistente")
    try:
        out = crilayla.decompress(data)
    except (IndexError, struct.error) as error:
        raise ValueError("CRILAYLA: flujo comprimido invalido") from error
    if len(out) != size + 256:
        raise ValueError("CRILAYLA: salida truncada")
    return out


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _cstr(b, o):
    return b[o:b.index(b"\0", o)].decode("latin1")


class Esk:
    def __init__(self, b):
        r = Reader(b, "ESK")
        r.header(b"#ESK", 0x20)
        so = r.u32(0x10)
        n = r.u16(so)
        o_idx, o_nm, o_trs = (r.u32(so + 4 + 4 * k) for k in range(3))
        r.array(so + o_idx, n, 8)
        r.array(so + o_nm, n, 4)
        r.array(so + o_trs, n, 48)
        self.names, self.parent, local = [], [], []
        for i in range(n):
            p = r.i16(so + o_idx + 8 * i)
            if p < -1 or p >= n or p == i:
                raise ValueError("ESK: padre invalido en hueso %d" % i)
            self.parent.append(p)
            self.names.append(r.text(so + r.u32(so + o_nm + 4 * i)))
            t = np.array(r.unpack("<12f", so + o_trs + 48 * i))
            if not np.isfinite(t).all():
                raise ValueError("ESK: transformacion no finita")
            q = t[4:8]
            norm = np.linalg.norm(q)
            if norm < 1e-12:
                raise ValueError("ESK: quaternion nulo")
            x, y, z, w = q / norm
            rot = np.array([[1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w)],
                            [2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w)],
                            [2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y)]])
            mat = np.eye(4)
            mat[:3, :3] = rot @ np.diag(t[8:11])
            mat[:3, 3] = t[:3]
            local.append(mat)
        # Resuelve incluso padres posteriores; detecta ciclos sin depender del orden del fichero.
        world, state = [None] * n, [0] * n

        def resolve(i):
            if state[i] == 1:
                raise ValueError("ESK: ciclo en la jerarquia")
            if state[i] == 2:
                return world[i]
            state[i] = 1
            p = self.parent[i]
            world[i] = resolve(p) @ local[i] if p >= 0 else local[i]
            state[i] = 2
            return world[i]

        conversion = np.diag([SCALE, SCALE, -SCALE, 1.0])
        inverse = np.linalg.inv(conversion)
        self.world_matrix = [conversion @ resolve(i) @ inverse for i in range(n)]
        self.world = [m[:3, 3].copy() for m in self.world_matrix]
        self.index = {nm: i for i, nm in enumerate(self.names)}
        # Oraculo independiente opcional: el ESK guarda inversas bind transpuestas.
        self.inverse_bind_error = None
        absolute = r.u32(so + 16)
        if absolute and n:
            r.span(so + absolute, n * 64)
            stored = np.frombuffer(b, "<f4", n * 16, so + absolute).reshape(n, 4, 4)
            try:
                reference = conversion @ np.linalg.inv(stored.transpose(0, 2, 1)) @ inverse
            except np.linalg.LinAlgError as error:
                raise ValueError("ESK: matriz inversa bind singular") from error
            self.inverse_bind_error = float(np.max(np.abs(reference - self.world_matrix)))


class Sub:
    pass


def leer_emd(b):
    r = Reader(b, "EMD")
    r.header(b"#EMD", 0x1C)
    subs = []
    for entry in r.array(r.u32(0x14), r.u16(0x12), 4):
        mo = r.u32(entry)
        r.span(mo, 8)
        for mesh in r.array(mo + r.u32(mo + 4), r.u16(mo + 2), 4):
            me = mo + r.u32(mesh)
            r.span(me, 0x3C)
            for sub in r.array(me + r.u32(me + 0x38), r.u16(me + 0x36), 4):
                s = me + r.u32(sub)
                subs.append(_sub(b, s))
    return subs


def _sub(b, s):
    r = Reader(b, "EMD/submesh")
    r.span(s, 0x50)
    flags, vsz, nv, vo = r.unpack("<4I", s + 0x30)
    if not vsz or not (flags & 1):
        raise ValueError("EMD: vertices sin posiciones o stride nulo")
    sm = Sub()
    sm.name = r.relative_text(s, 0x40)
    sm.flags = flags
    ntd, to = b[s + 0x45], s + r.u32(s + 0x48)
    sm.texdefs = [b[t + 1] for t in r.array(to, ntd, 12)]
    r.span(s + vo, nv * vsz)
    raw = np.frombuffer(b, np.uint8, nv * vsz, s + vo).reshape(nv, vsz)
    comp = bool(flags & 0x8000)
    o = 0

    def take(cnt, dt):
        nonlocal o
        sz = np.dtype(dt).itemsize * cnt
        if o + sz > vsz:
            raise ValueError("EMD: atributos exceden el stride")
        v = raw[:, o:o + sz].copy().view(dt).reshape(nv, cnt)
        o += sz
        return v
    P = take(3, "<f4").astype(np.float64)
    if flags & 0x2:
        N = take(4, "<f2")[:, :3].astype(np.float64) if comp else take(3, "<f4").astype(np.float64)
    else:
        N = np.zeros((nv, 3))
    UV = (take(2, "<f2") if comp else take(2, "<f4")).astype(np.float64) if flags & 0x4 else np.zeros((nv, 2))
    if flags & 0x8:
        take(2, "<f2" if comp else "<f4")
    if flags & 0x40:
        take(4, "u1")
    if flags & 0x80:
        take(4, "<f2") if comp else take(3, "<f4")
    if flags & 0x200:
        BI = take(4, "u1").astype(int)
        BW = (take(3, "<f2") if comp else take(3, "<f4")).astype(np.float64)
        BW = np.c_[BW, np.clip(1 - BW.sum(1), 0, 1)]
    else:
        BI, BW = np.zeros((nv, 4), int), np.c_[np.ones(nv), np.zeros((nv, 3))]
    sm.pos = (P @ MIRROR) * SCALE
    sm.nrm = N @ MIRROR
    sm.uv = UV
    sm.bone_names, bones, tris = [], np.zeros((nv, 4), int), []
    assigned = {}
    for group in r.array(s + r.u32(s + 0x4C), r.u16(s + 0x46), 4):
        go = s + r.u32(group)
        ni, nb, io_, no = r.unpack("<4I", go)
        names = [r.text(go + r.u32(q)) for q in r.array(go + no, nb, 4)]
        base = len(sm.bone_names)
        sm.bone_names += names
        r.span(go + io_, ni * (2 if nv <= 0xFFFF else 4))
        if ni % 3:
            raise ValueError("EMD: lista de indices no divisible por 3")
        idx = np.frombuffer(b, "<u2" if nv <= 0xFFFF else "<u4", ni, go + io_).astype(int)
        if ni and idx.max() >= nv:
            raise ValueError("EMD: indice fuera del buffer de vertices")
        used = np.unique(idx)
        if flags & 0x200 and np.any((BI[used] >= nb) & (BW[used] > 0)):
            raise ValueError("EMD: influencia fuera de la paleta de huesos")
        for v in used:
            palette = tuple(names[i] if weight > 0 and i < nb else "" for i, weight in zip(BI[v], BW[v]))
            if v in assigned and assigned[v] != palette:
                raise ValueError("EMD: vertice compartido entre paletas incompatibles")
            assigned[v] = palette
        bones[used] = BI[used] + base
        tris += [(idx[i], idx[i + 2], idx[i + 1]) for i in range(0, ni - 2, 3)]   # espejo: winding invertido
    sm.bones, sm.weights = bones, BW
    sm.tris = np.array(tris, int).reshape(-1, 3)
    return sm


def leer_emb(b):
    from PIL import Image
    r = Reader(b, "EMB")
    r.header(b"#EMB", 0x20)
    n, off, names = r.u32(0xC), r.u32(0x18), r.u32(0x1C)
    r.array(off, n, 8)
    if names:
        r.array(names, n, 4)
    out = []
    for i in range(n):
        o, size = struct.unpack_from("<II", b, off + 8 * i)
        d = r.span(off + 8 * i + o, size)
        nm = r.text(r.u32(names + 4 * i)) if names else "tex%d" % i
        if d[:4] == b"DDS ":
            with Image.open(io.BytesIO(d)) as source:
                img = np.array(source.convert("RGBA"))
        else:
            img = None
        out.append((nm, img))
    return out


def _test_esk():
    b = bytearray(208)
    struct.pack_into("<4sHH", b, 0, b"#ESK", 0xFFFE, 32)
    struct.pack_into("<I", b, 16, 32)
    struct.pack_into("<HHIII", b, 32, 2, 0, 32, 48, 56)
    struct.pack_into("<4h", b, 64, 1, -1, -1, -1)
    struct.pack_into("<4h", b, 72, -1, 0, -1, -1)
    struct.pack_into("<II", b, 80, 152, 156)
    struct.pack_into("<12f", b, 88, 1, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1)
    struct.pack_into("<12f", b, 136, 1, 2, 3, 1, 0, 0, 2 ** -0.5, 2 ** -0.5, 2, 2, 2, 1)
    b[184:193] = b"tip\0root\0"
    return bytes(b)


def _test_emd(compressed):
    stride = 36 if compressed else 48
    s, vo = 0xA0, 0x60
    table = vo + 3 * stride
    group = table + 4
    b = bytearray(s + group + 48)
    struct.pack_into("<4sHH", b, 0, b"#EMD", 0xFFFE, 28)
    struct.pack_into("<HII", b, 0x12, 1, 0x20, 0)
    struct.pack_into("<I", b, 0x20, 0x40)
    struct.pack_into("<HHII", b, 0x40, 0, 1, 12, 0)
    struct.pack_into("<I", b, 0x4C, 0x20)
    struct.pack_into("<HI", b, 0x60 + 0x36, 1, 0x3C)
    struct.pack_into("<I", b, 0x60 + 0x3C, 0x40)
    struct.pack_into("<4I", b, s + 0x30, 0x207 | (0x8000 if compressed else 0), stride, 3, vo)
    struct.pack_into("<I", b, s + 0x40, 0x50)
    struct.pack_into("<H", b, s + 0x46, 1)
    struct.pack_into("<I", b, s + 0x4C, table)
    b[s + 0x50:s + 0x55] = b"Body\0"
    for i, pos in enumerate(((1, 2, 3), (0, 0, 0), (1, 0, 0))):
        p = s + vo + i * stride
        struct.pack_into("<3f", b, p, *pos)
        if compressed:
            struct.pack_into("<4e2e4B3e", b, p + 12, 0, 0, 1, 0, 0.25, 0.5, 0, 1, 0, 1, 0.5, 0.25, 0.25)
        else:
            struct.pack_into("<3f2f4B3f", b, p + 12, 0, 0, 1, 0.25, 0.5, 0, 1, 0, 1, 0.5, 0.25, 0.25)
    struct.pack_into("<I", b, s + table, group)
    p = s + group
    struct.pack_into("<4I", b, p, 3, 2, 16, 24)
    struct.pack_into("<3H", b, p + 16, 0, 1, 2)
    struct.pack_into("<2I", b, p + 24, 32, 37)
    b[p + 32:p + 41] = b"root\0tip\0"
    return bytes(b)


def _test_emb(named=True):
    # DDS RGBA8 1x1: canales distintos para comprobar orden y alfa.
    dds = bytearray(132)
    dds[:4] = b"DDS "
    struct.pack_into("<7I", dds, 4, 124, 0x100F, 1, 1, 4, 0, 0)
    struct.pack_into("<8I", dds, 76, 32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000)
    struct.pack_into("<I", dds, 108, 0x1000)
    dds[128:132] = bytes([11, 22, 33, 44])
    b = bytearray(216)
    struct.pack_into("<4sHH", b, 0, b"#EMB", 0xFFFE, 32)
    struct.pack_into("<I", b, 12, 2)
    struct.pack_into("<II", b, 24, 32, 48 if named else 0)
    struct.pack_into("<4I", b, 32, 32, len(dds), 156, 4)
    struct.pack_into("<II", b, 48, 200, 208)
    b[64:196], b[196:200] = dds, b"RAW!"
    b[200:208], b[208:216] = b"col.dds\0", b"raw.bin\0"
    return bytes(b)


def selftest():
    from unittest.mock import patch
    from xeno_binary import _test_cpk, _test_crilayla
    blob = _test_cpk()
    with patch("builtins.open", side_effect=lambda *a, **kw: io.BytesIO(blob)):
        with Cpk("synthetic.cpk") as cpk:
            assert cpk.get("data/chara/TST/test.emm") == b"#EMM!"
            assert cpk.buscar(r"\.dat$") == ["raw.dat"]
        assert cpk.f.closed
    compressed, expected = _test_crilayla()
    assert descomprimir(compressed) == expected
    assert descomprimir(b"plain") == b"plain"
    rejects(lambda: descomprimir(b"CRILAYLA"))
    rejects(lambda: descomprimir(compressed[:-1]))
    esk = Esk(_test_esk())
    assert esk.names == ["tip", "root"] and esk.parent == [1, -1]
    np.testing.assert_allclose(esk.world, [[10, 40, -30], [10, 20, -30]], atol=1e-5)
    np.testing.assert_allclose(esk.world_matrix[1][:3, :3], [[0, -2, 0], [2, 0, 0], [0, 0, 2]], atol=1e-5)
    bad = bytearray(_test_esk())
    struct.pack_into("<h", bad, 72, 0)
    rejects(lambda: Esk(bad))
    struct.pack_into("<h", bad, 72, 2)
    rejects(lambda: Esk(bad))
    rejects(lambda: Esk(_test_esk()[:120]))
    for compressed in (False, True):
        data = _test_emd(compressed)
        sm, = leer_emd(data)
        assert sm.name == "Body" and sm.bone_names == ["root", "tip"]
        np.testing.assert_allclose(sm.pos[0], [10, 20, -30])
        np.testing.assert_allclose(sm.nrm[0], [0, 0, -1])
        np.testing.assert_allclose(sm.uv[0], [0.25, 0.5])
        np.testing.assert_allclose(sm.weights[0], [0.5, 0.25, 0.25, 0])
        assert sm.bones[0].tolist() == [0, 1, 0, 1] and sm.tris.tolist() == [[0, 2, 1]]
        rejects(lambda: leer_emd(data[:0x110]))
        bad = bytearray(data)
        # primer indice, despues de la tabla y cabecera del grupo
        pos = 0xA0 + 0x60 + 3 * (36 if compressed else 48) + 4 + 16
        struct.pack_into("<H", bad, pos, 3)
        rejects(lambda: leer_emd(bad))
    for named in (False, True):
        textures = leer_emb(_test_emb(named))
        assert [n for n, image in textures] == (["col.dds", "raw.bin"] if named else ["tex0", "tex1"])
        assert textures[0][1].shape == (1, 1, 4) and textures[0][1][0, 0].tolist() == [11, 22, 33, 44]
        assert textures[1][1] is None
    rejects(lambda: leer_emb(_test_emb()[:100]))
    for parser in (Esk, leer_emd, leer_emb):
        rejects(lambda parser=parser: parser(b""))
    # Integracion de los cinco formatos sin instalar ni abrir ningun juego.
    from types import SimpleNamespace
    from xeno_bcs import _fixture as bcs_fixture
    from xeno_emm import _fixture as emm_fixture
    prefix = "data/chara/TST/"
    files = {prefix + name: data for name, data in
             (("TST.bcs", bcs_fixture()), ("TST_000.esk", _test_esk()),
              ("TST_000_Bust.emd", _test_emd(True)), ("TST_000_Bust.emm", emm_fixture()),
              ("TST_000_Bust.emb", _test_emb()))}
    synthetic = SimpleNamespace(toc=files, get=files.__getitem__)
    result = resumen_personaje(synthetic, "TST")
    assert result["bcs"]["parts"] == 1 and result["emm"]["parameters"] == 6
    assert result["emd"]["vertices"] == 3 and result["emd"]["material_links_ok"]
    bad = bytearray(files[prefix + "TST_000_Bust.emm"])
    bad[44:48] = b"FAIL"
    files[prefix + "TST_000_Bust.emm"] = bytes(bad)
    rejects(lambda: resumen_personaje(synthetic, "TST"))
    print("xeno_model: N0 OK (CPK, CRILAYLA literal/backref, ESK TRS/padres/ciclos, EMD f16/f32, EMB DDS/RGBA)")


def resumen_personaje(cpk, code):
    """Metadatos: BCS completo y todos los materiales; un cuerpo/ESK/EMB como muestra."""
    import re
    from xeno_bcs import leer_bcs
    from xeno_emm import leer_emm
    if not re.fullmatch(r"[A-Za-z0-9_]{3,16}", code):
        raise ValueError("codigo de personaje invalido")
    code = code.upper()
    prefix = "data/chara/%s/" % code
    files = sorted(n for n in cpk.toc if n.startswith(prefix))
    if not files:
        raise ValueError("personaje ausente de este CPK: %s" % code)
    bcs = leer_bcs(cpk.get(prefix + code + ".bcs"))
    materials = []
    for name in (n for n in files if n.lower().endswith(".emm")):
        mats = leer_emm(cpk.get(name))
        materials.append(dict(path=name, materials=sum(m is not None for m in mats),
                              parameters=sum(len(m["parameters"]) for m in mats if m),
                              shaders=sorted({m["shader"] for m in mats if m})))
    esk_path = next(n for n in files if n.lower().endswith("_000.esk"))
    emd_path = next(n for n in files if n.lower().endswith("_000_bust.emd"))
    emb_candidates = [n for n in files if n.lower().endswith("_000_bust.emb")]
    if not emb_candidates:
        emb_candidates = [n for n in files if n.lower().endswith(".emb") and ".dyt." not in n.lower()]
    esk = Esk(cpk.get(esk_path))
    subs = leer_emd(cpk.get(emd_path))
    textures = leer_emb(cpk.get(emb_candidates[0])) if emb_candidates else []
    sample_materials = leer_emm(cpk.get(emd_path[:-4] + ".emm"))
    material_names = {m["name"] for m in sample_materials if m}
    if any(s.name not in material_names for s in subs):
        raise ValueError("EMD: material del cuerpo no encontrado en su EMM")
    if any(t >= len(textures) for s in subs for t in s.texdefs):
        raise ValueError("EMD: textura del cuerpo fuera de su EMB")
    return dict(character=code, cpk_entries=len(cpk.toc), character_files=len(files),
                bcs=dict(version=bcs["version"], sets=len(bcs["part_sets"]),
                         parts=sum(sum(p is not None for p in s["parts"]) for s in bcs["part_sets"]),
                         color_groups=len(bcs["colors"]), bodies=len(bcs["bodies"]),
                         physics=sum(len(p["physics"]) for s in bcs["part_sets"] for p in s["parts"] if p)),
                emm=dict(files=len(materials), materials=sum(m["materials"] for m in materials),
                         parameters=sum(m["parameters"] for m in materials), detail=materials),
                esk=dict(path=esk_path, bones=len(esk.names), roots=sum(p < 0 for p in esk.parent),
                         inverse_bind_error=esk.inverse_bind_error),
                emd=dict(path=emd_path, submeshes=len(subs), vertices=sum(len(s.pos) for s in subs),
                         triangles=sum(len(s.tris) for s in subs), material_links_ok=True,
                         texture_links_ok=True),
                emb=dict(path=emb_candidates[0] if emb_candidates else None, entries=len(textures),
                         rgba=sum(image is not None for name, image in textures)))


def main(argv=None):
    import argparse
    import json
    ap = argparse.ArgumentParser(description="Lectores XV1/XV2; solo lectura de la copia del jugador")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--cpk", help="CPK explicito; si falta, busca XV2 (DBZ3_XV2_CPK)")
    ap.add_argument("--personaje", default="GOK")
    ap.add_argument("--salida", help="JSON de metadatos nuevo; no exporta arte")
    args = ap.parse_args(argv)
    if args.selftest:
        selftest()
        return 0
    try:
        path = args.cpk or buscar_cpk(xv2=True)
        if not path:
            raise FileNotFoundError("no encuentro XV2; indica --cpk")
        with Cpk(path) as cpk:
            result = resumen_personaje(cpk, args.personaje)
        if args.salida:
            write_json(args.salida, result)
        display = dict(result, emm={k: v for k, v in result["emm"].items() if k != "detail"})
        print(json.dumps(display, ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print("xeno_model: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

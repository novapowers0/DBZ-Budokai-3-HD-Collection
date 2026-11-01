#!/usr/bin/env python3
"""Gestos XV2 horneados desde FCE EAN; seleccion por las listas del donante B3.

No reproduce el reloj EAN: B3 elige instantaneas con su animacion nativa.
Las asociaciones semanticas entre juegos son aproximadas y se informan como [H].
"""
import copy
import re
import struct
import numpy as np
from xeno_binary import Reader, rejects
from xeno_model import MIRROR, SCALE


# L01 parpadeo y L18 dolor: comprobados en las placas del Goku B3.
# Labios/ira/grito son equivalencias semanticas, no una pista temporal 1:1.
# L04 pupilas pequenas, L05 cenyudo, L06 ojos abiertos, L09 cejas, L42 tenso: vistas en las variantes del Goku B3. [H] por parecido;
# la boca no entra aqui: la mueve M_JAW en la malla pielada, como en B3.
FACE_CLIPS = {1: 'BHV_BLINK', 4: 'EMO_HATE', 5: 'EMO_GLARE', 6: 'EMO_FEAR', 9: 'EMO_ANGER', 18: 'DMG_HEAD_F', 42: 'EMO_SHOUT'}
HAND_NUMBERS = (0, 1, 2, 4, 5, 10, 11)
HAND_ALIASES = {20: 11, 38: 1}


class FaceEan:
    """EAN PC acotado: IDs de hueso ZERO-based, 4 componentes por canal XV2."""
    def __init__(self, data):
        r = Reader(data, 'FCE EAN')
        r.header(b'#EAN', 32)
        n, sk, tab, nt = r.unpack('<HIII', 0x12)
        count = r.u16(sk)
        names = sk + r.u32(sk + 8)
        self.names = [r.text(sk + r.u32(q)) for q in r.array(names, count, 4)]
        if len(set(self.names)) != count:
            raise ValueError('FCE EAN: huesos duplicados')
        self.clips = {}
        r.array(nt, n, 4)
        for i, q in enumerate(r.array(tab, n, 4)):
            a, name = r.u32(q), r.text(r.u32(nt + i*4))
            _, ids_size, val_size, frames, nb, bt = r.unpack('<HBBIII', a)
            if ids_size not in (0, 1) or val_size not in (1, 2) or not frames:
                raise ValueError('FCE EAN: formato de claves no admitido')
            bones = {}
            for off in r.array(a + bt, nb, 4):
                b = a + r.u32(off)
                bid, nc, ct = r.unpack('<HHI', b)
                if bid >= count or bid in bones:
                    raise ValueError('FCE EAN: ID de hueso invalido/duplicado')
                channels = {}
                for cq in r.array(b + ct, nc, 4):
                    c = b + r.u32(cq)
                    ch, _, _, nk, ki, kv = r.unpack('<BBHIII', c)
                    if ch not in (0, 1, 2) or ch in channels or not nk:
                        raise ValueError('FCE EAN: canal invalido/duplicado/vacio')
                    ids = np.array(r.unpack('<' + str(nk) + ('B' if ids_size == 0 else 'H'), c + ki))
                    vals = np.array(r.unpack('<' + str(nk*4) + ('e' if val_size == 1 else 'f'), c + kv)).reshape(nk, 4)
                    if not np.isfinite(vals).all() or np.any(np.diff(ids) <= 0) or ids[-1] >= frames:
                        raise ValueError('FCE EAN: claves invalidas')
                    channels[ch] = (ids, vals)
                bones[bid] = channels
            if name in self.clips:
                raise ValueError('FCE EAN: nombre de clip duplicado')
            self.clips[name] = (frames, bones)

    def mezclar(self, otro):
        """CaC: cara partida en dos EAN (HUM000U ojos/cejas, HUM000L boca), mismos clips y fotogramas."""
        corto = lambda n: n.split('_FCE_')[-1]
        mios = {corto(n): n for n in self.clips}
        base = len(self.names)
        self.names = self.names + list(otro.names)
        for n, (frames, bones) in otro.clips.items():
            if corto(n) in mios:
                k = mios[corto(n)]
                self.clips[k] = (max(self.clips[k][0], frames),
                                 {**self.clips[k][1], **{b + base: v for b, v in bones.items()}})
        return self

    def clip(self, suffix):
        hits = [n for n in self.clips if n.endswith('_FCE_' + suffix)]
        if len(hits) != 1:
            raise ValueError('FCE EAN: clip ausente/ambiguo: ' + suffix)
        return hits[0]

    def values(self, clip, frame):
        frames, bones = self.clips[clip]
        frame = np.clip(frame, 0, frames-1)
        out = {}
        for bid, channels in bones.items():
            vals = [np.array([0., 0, 0, 1]), np.array([0., 0, 0, 1]), np.ones(4)]
            for ch, (ids, keys) in channels.items():
                hi = min(int(np.searchsorted(ids, frame, side='right')), len(ids)-1)
                lo = max(0, hi-1) if frame < ids[hi] else hi
                t = 0 if hi == lo else (frame - ids[lo]) / (ids[hi] - ids[lo])
                v0, v1 = keys[lo], keys[hi]
                if ch == 1 and v0 @ v1 < 0:
                    v1 = -v1
                vals[ch] = v0*(1-t) + v1*t
                if ch == 1:
                    norm = np.linalg.norm(vals[ch])
                    if norm < 1e-10:
                        raise ValueError('FCE EAN: cuaternion nulo')
                    vals[ch] /= norm
            out[self.names[bid]] = vals
        return out

    def pose(self, esk, suffix):
        from xeno_a_b3 import order
        from scipy.spatial.transform import Rotation
        default = self.values(self.clip('DEFAULT'), 0)
        clip = self.clip(suffix)
        # Instantanea de maxima desviacion, elegida por datos (incluye parpadeo).
        def energy(frame):
            v = self.values(clip, frame)
            return sum(np.linalg.norm(v[n][0][:3]-default[n][0][:3])**2 +
                       1-abs(float(v[n][1] @ default[n][1])) for n in v if n in default)
        frame = max(range(self.clips[clip][0]), key=energy)
        target, worlds = self.values(clip, frame), [None]*len(esk.names)
        for i in order(esk.parent):
            parent = esk.parent[i]
            local = (np.linalg.inv(esk.world_matrix[parent]) @ esk.world_matrix[i]
                     if parent >= 0 else esk.world_matrix[i].copy())
            name = esk.names[i]
            if name in target and name in default and name.startswith('f_'):
                p, q, s = target[name]
                p0, q0, s0 = default[name]
                local = local.copy()
                local[:3, 3] += (p[:3]-p0[:3]) @ MIRROR * SCALE
                rot = Rotation.from_quat(q).as_matrix() @ Rotation.from_quat(q0).as_matrix().T
                local[:3, :3] = MIRROR @ rot @ MIRROR @ local[:3, :3] @ np.diag(s[:3]/s0[:3])
            worlds[i] = worlds[parent] @ local if parent >= 0 else local
        matrices = {n: worlds[i] @ np.linalg.inv(esk.world_matrix[i]) for i, n in enumerate(esk.names)}
        return matrices, dict(clip=clip, frame=frame, frames=self.clips[clip][0], association='semantic_[H]')


def skin_pose(sm, matrices):
    mats = np.array([matrices[n] for n in sm.bone_names])
    w = np.clip(sm.weights, 0, None)
    w = w / w.sum(1, keepdims=True)
    mix = np.einsum('vk,vkij->vij', w, mats[sm.bones])
    out = copy.copy(sm)
    out.pos = np.einsum('vij,vj->vi', mix[:, :3, :3], sm.pos) + mix[:, :3, 3]
    out.nrm = np.einsum('vji,vj->vi', np.linalg.inv(mix[:, :3, :3]), sm.nrm)
    out.nrm /= np.maximum(np.linalg.norm(out.nrm, axis=1, keepdims=True), 1e-12)
    return out


def native_lists(data, tpl):
    """Numero Lnn realmente elegido en CADA lista B3, incluido su alias/fallback."""
    import sdbh_model as S
    a = next(o for o, s, t, _ in S.amb_kids(data) if data[o:o+4] == b'#AWO')
    table = a + struct.unpack_from('>I', data, a+0x1c)[0]
    labels = {}
    for k in range(1, tpl.namg):
        g = a + struct.unpack_from('>I', data, table+4*k)[0]
        no = struct.unpack_from('>I', data, g+0x1c)[0]
        label = data[g+no:g+no+32].split(b'\0')[0].decode('latin1')
        match = re.search(r'_L(\d\d)_', label)
        if match:
            labels[k] = int(match[1])
    return {i: [labels.get(struct.unpack_from('>I', data, a+rec[1]+16*n)[0], 0)
                for n in range(tpl.nlist)] for i, rec in enumerate(tpl.bone_recs) if rec[1]}


def selftest():
    # EAN sintetico con DOS huesos: bid=1 debe ser el segundo, nunca el primero.
    b = bytearray(384)
    struct.pack_into('<4sHH', b, 0, b'#EAN', 0xfffe, 32)
    struct.pack_into('<HIII', b, 0x12, 1, 32, 96, 100)
    struct.pack_into('<HHIII', b, 32, 2, 0, 16, 32, 40)
    struct.pack_into('<II', b, 64, 192, 200)
    b[224:229], b[232:240] = b'root\0', b'f_C_Jaw\0'
    struct.pack_into('<II', b, 96, 112, 248)
    b[248:264] = b'TST_FCE_DEFAULT\0'
    struct.pack_into('<HBBIII', b, 112, 0, 0, 2, 2, 1, 16)
    struct.pack_into('<I', b, 128, 24)
    struct.pack_into('<HHII', b, 136, 1, 1, 8, 16)
    struct.pack_into('<BBHIII', b, 152, 0, 7, 0, 2, 16, 32)
    b[168:170] = bytes([0, 1])
    struct.pack_into('<8f', b, 184, 0, 0, 0, 1, 2, 4, 6, 1)
    e = FaceEan(b)
    assert list(e.values(e.clip('DEFAULT'), .5)) == ['f_C_Jaw']
    np.testing.assert_allclose(e.values(e.clip('DEFAULT'), .5)['f_C_Jaw'][0], [1, 2, 3, 1])
    rejects(lambda: FaceEan(b[:190]))
    rejects(lambda: e.clip('AUSENTE'))
    bad = b.copy(); struct.pack_into('<H', bad, 136, 2)
    rejects(lambda: FaceEan(bad))
    print('xeno_gestos: N0 OK (EAN zero-based, interpolacion, limites y clips ausentes; sin juego)')


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--selftest', action='store_true', required=True)
    ap.parse_args()
    selftest()

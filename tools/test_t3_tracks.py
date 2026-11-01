"""Regresiones sinteticas del lector/retarget T3. No usa ni escribe datos de juego."""
import math
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / "awo_tools"), str(ROOT / "mod center hd")]
import altura
import roster_build
import t3_anim
import t3_retarget


IDENTITY_PACKED = 0x37FFFF7FFFF7FFFF


def clip_bytes(node=2):
    # Un evento y una pista de giro, con dos claves iguales de identidad.
    out = bytearray(0x94 + 16)
    struct.pack_into("<BBHH", out, 0, 0x61, 1, 12, 0)
    struct.pack_into("<H", out, 6 + 2 * node, len(out) // 4)
    struct.pack_into("<QHHI", out, 0x94, 0x400, 6, 0, 17)
    out += struct.pack("<HHQQHH", 1, 2, IDENTITY_PACKED, IDENTITY_PACKED, 0, 12)
    return bytes(out)


class T3Tracks(unittest.TestCase):
    def test_real_node_ids_and_event_header(self):
        data = t3_anim.read_clip(clip_bytes(70))
        self.assertEqual(set(data["tracks"]), {70})
        self.assertEqual(data["last_frame"], 12)
        self.assertEqual(data["events"][0]["frame"], 6)
        self.assertEqual(data["events"][0]["arg"], 17)

    def test_smallest_three_identity_and_invalid_index(self):
        q = t3_anim.unpack_quat(IDENTITY_PACKED)
        self.assertLess(max(abs(v) for v in q[:3]), 1e-6)
        self.assertAlmostEqual(q[3], 1.0, places=10)
        with self.assertRaises(ValueError):
            t3_anim.unpack_quat(0xFFFFFFFFFFFFFFFF)

    def test_malformed_offsets_truncation_and_key_times(self):
        bad_offset = bytearray(clip_bytes())
        struct.pack_into("<H", bad_offset, 10, 1)
        bad_time = bytearray(clip_bytes())
        struct.pack_into("<H", bad_time, len(bad_time) - 2, 13)
        for data in (clip_bytes()[:100], bad_offset, bad_time):
            with self.subTest(size=len(data)):
                with self.assertRaises(ValueError):
                    t3_anim.read_clip(data)

    def test_slerp_short_arc_and_outside_track(self):
        track = dict(flags=1, keys=[
            dict(frame=0, rotation=[0, 0, 0, 1], position=None),
            dict(frame=10, rotation=[0, 0, -1, 0], position=None),
        ])
        q, _ = t3_anim.sample_track(track, 5)
        self.assertAlmostEqual(q[2], -math.sqrt(0.5))
        self.assertAlmostEqual(q[3], math.sqrt(0.5))
        self.assertEqual(t3_anim.sample_track(track, 11), ([0, 0, 0, 1], None))

    def test_pmdl_parent_stack(self):
        out = bytearray(0x90 + 128)
        out[:4] = b"pmdl"
        struct.pack_into("<I", out, 0x6C, 0x90)
        struct.pack_into("<I4H", out, 0x90, 64, 0, 0, 1, 0)
        struct.pack_into("<I4H", out, 0xD0, 64, 2, 1, 1, 1)
        struct.pack_into("<3f", out, 0x100, 0, 1, 0)
        nodes = t3_anim.read_skeleton(out)
        self.assertEqual([(n["id"], n["parent"]) for n in nodes], [(0, None), (1, 0)])
        pose = t3_anim.pose(dict(tracks={}), nodes, 0)
        self.assertEqual(pose[1][1], [0, 1, 0])

    def test_runtime_root_flags_keep_original_immutable(self):
        clip = dict(last_frame=10, tracks={0: dict(flags=0, keys=[
            dict(frame=0, position=[1, 2, 3], rotation=[0, 0, 0, 1]),
            dict(frame=10, position=[4, 8, 12], rotation=[0, 0, 0, 1]),
        ])})
        rebased = t3_anim.runtime_clip(clip, 0x80000000)
        self.assertEqual(rebased["tracks"][0]["keys"][1]["position"], [3, 6, 9])
        zero_x = t3_anim.runtime_clip(clip, 0x80000002)
        self.assertEqual(zero_x["tracks"][0]["keys"][1]["position"], [0, 6, 9])
        zero_all = t3_anim.runtime_clip(clip, 0xC0000000)
        self.assertEqual(zero_all["tracks"][0]["keys"][1]["position"], [0, 0, 0])
        self.assertEqual(clip["tracks"][0]["keys"][0]["position"], [1, 2, 3])

    def test_secondary_aim_layer_uses_normalized_time(self):
        primary = dict(last_frame=10, tracks={0: dict(flags=0, keys=[
            dict(frame=0, position=[2, 0, 0], rotation=[0, 0, 0, 1]),
        ])})
        secondary = dict(last_frame=20, tracks={0: dict(flags=0, keys=[
            dict(frame=0, position=[0, 4, 0], rotation=[0, 0, 0, 1]),
            dict(frame=20, position=[0, 4, 0], rotation=[0, 0, 1, 0]),
        ])})
        q, pos = t3_anim.pose(primary, [dict(id=0, parent=None, rest=[0, 0, 0])], 5, secondary, 0.5)[0]
        self.assertEqual(pos, [1, 2, 0])
        self.assertAlmostEqual(q[2], math.sin(math.pi / 8))
        self.assertAlmostEqual(q[3], math.cos(math.pi / 8))


class T3Retarget(unittest.TestCase):
    def inputs(self):
        source = [dict(id=0, parent=None, rest=[0, 0, 0])]
        target = [("TEST_WAIST", [0, math.sqrt(0.5), 0, math.sqrt(0.5)], [0, 1, 0], -1)]
        track = dict(flags=0, keys=[dict(frame=0, rotation=[math.sqrt(0.5), 0, 0, math.sqrt(0.5)], position=[1, 2, 3])])
        clip = dict(last_frame=12, tracks={0: track})
        mapping = dict(bones={"WAIST": 0}, basis=[0, 0, 1, 0], scale=2.0, offset=[1, 2, 3], root="WAIST")
        return clip, source, target, mapping

    def test_axis_change_bind_and_additive_translation(self):
        args = self.inputs()
        names, animation = t3_retarget.convert(*args, 60, 60)
        self.assertEqual(animation[2], 13)  # 0..12 a 60 Hz = 0,2 s; no duplicar a 30 Hz.
        rot, pos = animation[3]["WAIST"]
        angles = struct.unpack_from(">3H", rot, 14)
        # C = giro Z de 180 grados: el giro X de origen se convierte en -X.
        expected = t3_anim.qmul([-math.sqrt(0.5), 0, 0, math.sqrt(0.5)], args[2][0][1])
        actual = altura.euler(angles)
        self.assertGreater(abs(sum(a*b for a, b in zip(expected, actual))), 1 - 1e-8)
        delta = struct.unpack_from(">3f", pos, 16)
        self.assertEqual(delta, (-1, -3, 9))  # reposo (0,1,0) + delta = C*(1,2,3)*2 + (1,2,3).
        parsed_names, parsed = roster_build.acm_read(t3_retarget.build_acm(names, animation))
        self.assertEqual(parsed_names, names)
        self.assertEqual(parsed[0], animation)

    def test_nonfinite_map_and_bad_parent_are_rejected(self):
        for change in (dict(basis=[0, 0, float("nan"), 0]), dict(offset=[0, float("inf"), 0])):
            clip, source, target, mapping = self.inputs()
            mapping.update(change)
            with self.assertRaises(ValueError):
                t3_retarget.convert(clip, source, target, mapping, 60, 60)
        clip, source, target, mapping = self.inputs()
        target[0] = (*target[0][:3], -2)
        with self.assertRaises(ValueError):
            t3_retarget.convert(clip, source, target, mapping, 60, 60)

    def test_bank_keeps_multiple_animation_tables(self):
        names, first = t3_retarget.convert(*self.inputs(), 60, 60)
        second = (first[0], 7, first[2], first[3])
        data = t3_retarget.build_acm_bank(names, [first, second])
        parsed_names, animations = roster_build.acm_read(data)
        self.assertEqual(parsed_names, names)
        self.assertEqual(animations, [first, second])
        with self.assertRaises(ValueError):
            t3_retarget.build_acm_bank(names, [])


if __name__ == "__main__":
    unittest.main()

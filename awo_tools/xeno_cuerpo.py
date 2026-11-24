#!/usr/bin/env python3
"""Uniones de brazos XV2/B3. Solo adapta pesos de los archivos locales del jugador."""
import copy
import numpy as np


def destino_helper(tpl, side, suffix, default):
    """El corrector del hombro sigue el nodo intermedio de B3, no todo el humero.

    El ancla geometrica sigue siendo Arm1: esto no cambia la pose de reposo.
    Otros donantes sin esa cadena conservan el mapeo anatomico anterior.
    """
    if suffix in ("ArmHelper", "ArmorParts"):
        rot = tpl.bone(side + "ARMROT")
        if rot is not None and tpl.parent[default] == rot:
            return rot
    return default


def particion(sm):
    """Triangulos de mano y borde compartido, usando pesos activos, no la paleta.

    Las influencias se guardan como half en XV2: no se exige que sumen 1 exacto.
    Las copias de vertices por UV tambien pertenecen a la misma costura.
    """
    mano = np.array([n.startswith("h_") or n in ("b_L_Hand", "b_R_Hand")
                     for n in sm.bone_names], bool)
    proximal = ((~mano[sm.bones]) & (sm.weights > 1e-5)).any(1)
    hand = (~proximal[sm.tris]).all(1)
    borde = np.zeros(len(sm.pos), bool)
    if hand.any() and (~hand).any():
        _, coord = np.unique(np.round(sm.pos, 6), axis=0, return_inverse=True)
        h = np.unique(coord[sm.tris[hand]])
        b = np.unique(coord[sm.tris[~hand]])
        borde = np.isin(coord, np.intersect1d(h, b))
    return hand, borde


def preparar_brazos(rig, pieces):
    """Conserva el antebrazo pielado y solo hornea gestos en la mano distal.

    Ambos lados de la nueva costura siguen la misma muñeca al 100 %. Los dedos
    interiores mantienen sus pesos y gestos; el borde queda anclado para que un
    gesto de pulgar no despegue una manga que no tiene variantes.
    """
    from xeno_a_b3 import compact
    result, report = [], []
    for p in pieces:
        sm = p["sm"]
        if p.get("cuerpo_proximal") or p["part"] != "Rist" or not any(n.startswith("h_") for n in sm.bone_names):
            result.append(p)
            continue
        hand, border = particion(sm)
        if hand.all():
            result.append(p)
            continue
        q = copy.copy(sm)
        q.bones, q.weights = sm.bones.copy(), sm.weights.copy()
        q.bone_names = list(sm.bone_names)
        for side, sign in (("L", 1), ("R", -1)):
            ids = np.flatnonzero(border & ((sm.pos[:, 0] >= 0) if side == "L" else (sm.pos[:, 0] < 0)))
            if not len(ids):
                continue
            name = "b_%s_Hand" % side
            if name not in q.bone_names:
                q.bone_names.append(name)
            q.bones[ids] = q.bone_names.index(name)
            q.weights[ids] = (1., 0., 0., 0.)
        result.append(dict(p, sm=compact(q, q.tris[~hand]), cuerpo_proximal=True))
        if hand.any():
            result.append(dict(p, sm=compact(q, q.tris[hand])))
        report.append(dict(material=sm.name, triangles_before=len(sm.tris),
                           triangles_skinned=int((~hand).sum()), triangles_hand=int(hand.sum()),
                           seam_vertices=int(border.sum())))
    if report:
        # HANDROT tiene ARM2 de padre. L00_HAND es el grupo intercambiable de los
        # gestos; sus ejes se conservan. No insertar slots: solo ampliar al final.
        for side in "LR":
            wrist = rig.tpl.bone(side + "HANDROT")
            if wrist is None:
                wrist = rig.tpl.bone("L00_%sHAND" % side)
            if wrist is None:
                raise ValueError("B3: falta muñeca %s para el guante pielado" % side)
            if wrist not in rig.tpl.palette:
                rig.tpl.palette.append(wrist)
            for name in rig.target:
                if name == "b_%s_Hand" % side or name.startswith("h_%s_" % side):
                    rig.target[name] = wrist
    rig.tpl.cuerpo_report = report
    return result


def selftest():
    from types import SimpleNamespace
    from xeno_a_b3 import prepare_pieces
    # Un quad proximal y un triangulo distal; el borde tiene copias por UV.
    sm = SimpleNamespace(name="guante_inventado", pos=np.array([
        [1., 0, 0], [1., 1, 0], [2., 0, 0], [2., 1, 0], [2., 0, 0], [3., .5, 0]]),
        nrm=np.tile([0., 0, 1], (6, 1)), uv=np.zeros((6, 2)),
        bones=np.array([[0]*4, [0]*4, [1]*4, [1]*4, [1]*4, [2]*4]),
        weights=np.tile([.9998, 0, 0, 0], (6, 1)),
        bone_names=["b_L_Arm2", "b_L_Hand", "h_L_Index1"],
        tris=np.array([[0, 1, 2], [1, 3, 2], [4, 3, 5]]))
    hand, border = particion(sm)
    assert hand.tolist() == [False, False, True]
    assert np.flatnonzero(border).tolist() == [2, 3, 4]
    tpl = SimpleNamespace(palette=[0xFFFFFFFF, 0], parent=[-1, 0, 1],
                          bone=lambda n: {"LARMROT": 1, "LHANDROT": 1, "RHANDROT": 2}.get(n))
    rig = SimpleNamespace(tpl=tpl, target={n: 0 for n in sm.bone_names})
    p = dict(part="Rist", sm=sm, attachment=None)
    pieces = preparar_brazos(rig, [p])
    assert sum(len(q["sm"].tris) for q in pieces) == len(sm.tris)
    assert [q.get("cuerpo_proximal", False) for q in pieces] == [True, False]
    assert tpl.palette == [0xFFFFFFFF, 0, 1, 2]
    assert rig.target["h_L_Index1"] == 1
    assert destino_helper(tpl, "L", "ArmHelper", 2) == 1
    assert destino_helper(tpl, "L", "Elbow", 2) == 2
    # Deformacion rigida de muñeca frente a LBS del borde: iguales con cualquier
    # giro. El interior del guante conserva un peso de antebrazo sin modificar.
    for deg in (0, 45, 90, 155):
        t = np.deg2rad(deg)
        R = np.array([[np.cos(t), -np.sin(t), 0], [np.sin(t), np.cos(t), 0], [0, 0, 1]])
        proximal, distal = [q["sm"] for q in pieces]
        for pos in sm.pos[border]:
            a = np.flatnonzero(np.all(proximal.pos == pos, axis=1))
            b = np.flatnonzero(np.all(distal.pos == pos, axis=1))
            if len(a) and len(b):
                assert proximal.weights[a[0], 0] == distal.weights[b[0], 0] == 1
                np.testing.assert_allclose(R @ proximal.pos[a[0]], R @ distal.pos[b[0]], atol=1e-12)
    np.testing.assert_array_equal(sm.weights[:, 0], np.full(6, .9998))
    # Una paleta con dedos sin usar no debe convertir el antebrazo en rigido.
    rig.geometry = lambda sm, attachment=None: sm
    out, _ = prepare_pieces(rig, [pieces[0]], 1, False)
    assert len(out) == 1 and out[0]["rigid"] is None
    print("PASS xeno_cuerpo: particion, UV, triángulos, paleta aditiva y costura 0/45/90/155 grados")


if __name__ == "__main__":
    selftest()

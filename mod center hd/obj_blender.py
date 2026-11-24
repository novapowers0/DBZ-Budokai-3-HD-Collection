"""Worker Blender del importador OBJ. Se ejecuta únicamente con Blender -b."""
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector, Matrix
import numpy as np


def suffix(label):
    return label.lstrip("X").split("_", 1)[-1]


def _hsv(color):
    """H (grados), S y V de un color sRGB 0..1, sin depender de colorsys."""
    c = np.clip(np.asarray(color, dtype=float), 0, 1)
    hi, lo = float(c.max()), float(c.min())
    d = hi - lo
    s = d / hi if hi > 1e-6 else 0.0
    if d < 1e-6:
        h = 0.0
    elif hi == c[0]:
        h = (60.0 * ((c[1] - c[2]) / d)) % 360.0
    elif hi == c[1]:
        h = 60.0 * ((c[2] - c[0]) / d) + 120.0
    else:
        h = 60.0 * ((c[0] - c[1]) / d) + 240.0
    return h, s, hi


def paleta_cel(rgb):
    """Agrupa por pigmento, no por luz: elimina degradados sin mezclar bordes.

    Trabaja en los valores sRGB del atlas (Image.pixels no los decodifica). Se deducen
    de cada OBJ, sin atlas, partes ni paletas prestados de personajes del juego.
    """
    srgb = np.clip(rgb, 0, 1)
    hi, lo = srgb.max(1), srgb.min(1)
    sat = (hi - lo) / np.maximum(hi, .001)
    # Negro (pelo/líneas), tejido neutro, blanco real y pigmentos cromáticos.
    # El blanco solo si es BLANCO (suela, ojos): antes cualquier gris claro
    # (hi>=0,58) sin saturación caía aquí y las luces horneadas del pantalón
    # salían como manchas blancas. Ahora todo neutro por debajo de 0,86 se
    # funde en un único tejido oscuro y desaparece el degradado pintado.
    groups = np.full(len(rgb), -1, dtype=np.int32)
    groups[hi < .14] = 0
    groups[(groups < 0) & (sat < .22) & (hi > .86)] = 2
    groups[(groups < 0) & (sat < .38)] = 1
    chromatic = np.flatnonzero(groups < 0)
    palette = []
    for group in range(3):
        values = srgb[groups == group]
        if len(values):
            palette.append((group, np.median(values, axis=0)))
    if len(chromatic):
        values = srgb[chromatic]
        # Cromaticidad independiente de la intensidad: mano y cara comparten tono.
        features = values / np.maximum(values.sum(1, keepdims=True), .001)
        sample = features[::max(1, len(features) // 16000)]
        centers = [sample[len(sample) // 2]]
        for _ in range(7):
            d = np.min(np.sum((sample[:, None] - np.array(centers)) ** 2, axis=2), axis=1)
            if d.max() < .002:
                break
            centers.append(sample[int(d.argmax())])
        centers = np.array(centers)
        for _ in range(12):
            labels = np.argmin(np.sum((sample[:, None] - centers) ** 2, axis=2), axis=1)
            for k in range(len(centers)):
                if np.any(labels == k):
                    centers[k] = np.median(sample[labels == k], axis=0)
        labels = np.argmin(np.sum((features[:, None] - centers) ** 2, axis=2), axis=1)
        # Un mismo pigmento repartido en varios centros (piel clara/oscura, azul
        # de la banda con su sombra horneada) se funde por tono. El emblema
        # dorado no se mezcla con la piel porque su saturación es muy distinta.
        chroma, merged = [], []
        for k in range(len(centers)):
            chosen = labels == k
            if np.any(chosen):
                chroma.append((k + 3, np.median(values[chosen], axis=0), chosen))
        for gid, col, chosen in chroma:
            h, s, v = _hsv(col)
            for m in merged:
                if (m["s"] > .25 and s > .25 and abs(((h - m["h"] + 180) % 360) - 180) < 14
                        and abs(s - m["s"]) < .25 and abs(v - m["v"]) < .30):
                    m["chosen"].append(chosen)
                    break
            else:
                merged.append(dict(gid=gid, h=h, s=s, v=v, chosen=[chosen]))
        for m in merged:
            allchosen = np.any(np.stack(m["chosen"]), axis=0)
            groups[chromatic[allchosen]] = m["gid"]
            palette.append((m["gid"], np.median(values[allchosen], axis=0)))
    result = srgb.copy()
    for group, color in palette:
        result[groups == group] = color
    return result, [np.rint(c * 255).astype(int).tolist() for _, c in palette]


def afinar_tinta(pixels, size):
    """Conserva la tinta (ojos, cejas, líneas) al reducir la textura 4:1.

    Un píxel oscuro rodeado de tono claro es una línea fina que la reducción
    difumina; se lleva a negro para que la cara siga teniendo ojos y cejas
    legibles en juego. No engorda el trazo: solo oscurece lo que ya era oscuro.
    """
    img = pixels.reshape(size, size, 3)
    around = (np.roll(img, 1, 0) + np.roll(img, -1, 0)
              + np.roll(img, 1, 1) + np.roll(img, -1, 1)) / 4
    lines = (img.max(2) < .40) & (around.max(2) > .45)
    img[lines] *= .35
    return int(lines.sum())


def dilatar_islas(pixels, mesh, size, margin=8):
    """Extiende el texel vecino solo por UV vacías, sin repintar otras islas."""
    mask = np.zeros((size, size), dtype=bool)
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active
    for tri in mesh.loop_triangles:
        p = np.array([uv.data[li].uv[:] for li in tri.loops]) * size - .5
        low = np.maximum(0, np.floor(p.min(0)).astype(int))
        high = np.minimum(size - 1, np.ceil(p.max(0)).astype(int))
        if np.any(low > high):
            continue
        x, y = np.meshgrid(np.arange(low[0], high[0] + 1), np.arange(low[1], high[1] + 1))
        a, b, c = p
        den = (b[1]-c[1])*(a[0]-c[0]) + (c[0]-b[0])*(a[1]-c[1])
        if abs(den) < 1e-10:
            continue
        u = ((b[1]-c[1])*(x-c[0]) + (c[0]-b[0])*(y-c[1])) / den
        v = ((c[1]-a[1])*(x-c[0]) + (a[0]-c[0])*(y-c[1])) / den
        inside = (u >= -1e-6) & (v >= -1e-6) & (u+v <= 1.000001)
        mask[low[1]:high[1]+1, low[0]:high[0]+1] |= inside
    image = pixels.reshape(size, size, 4)
    original = int(mask.sum())
    for _ in range(margin):
        previous = mask.copy()
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            neighbor = np.roll(previous, (dy, dx), axis=(0, 1))
            if dy == 1:
                neighbor[0] = False
            if dy == -1:
                neighbor[-1] = False
            if dx == 1:
                neighbor[:, 0] = False
            if dx == -1:
                neighbor[:, -1] = False
            fill = ~mask & neighbor
            image[fill] = np.roll(image, (dy, dx), axis=(0, 1))[fill]
            mask[fill] = True
    return int(mask.sum()) - original


def refinar_cabeza(positions, height):
    """Proporcion anime moderada sobre la fuente, con transicion en el cuello."""
    result = positions.copy()
    t = np.clip((positions[:, 2] / height - .805) / .05, 0, 1)
    lateral = np.clip((.23 - np.abs(positions[:, 0]) / height) / .06, 0, 1)
    blend = t * t * (3 - 2 * t) * lateral
    result[:, 0] *= 1 + .12 * blend
    center = float(np.median(positions[blend > .8, 1])) if np.any(blend > .8) else 0.
    result[:, 1] += (positions[:, 1] - center) * .04 * blend
    result[:, 2] += np.maximum(positions[:, 2] - height * .805, 0) * .08 * blend
    return result


def normales_suaves(mesh, angle=65):
    """Normales ponderadas por área, soldadas SOLO para sombreado entre UV.

    No mueve vértices ni redondea las puntas: conserva los saltos >65 grados.
    Orienta primero cada cara usando las normales importadas, igual que el writer.
    """
    positions = np.array([v.co[:] for v in mesh.vertices])
    key = [tuple(np.round(p, 5)) for p in positions]
    adjacent, face = {}, []
    for t in mesh.loop_triangles:
        a, b, c = t.vertices
        cross = np.cross(positions[b] - positions[a], positions[c] - positions[a])
        original = np.array([mesh.loops[li].normal[:] for li in t.loops])
        if sum(original @ cross < 0) >= 2:
            cross = -cross
        face.append(cross)
        for vi in t.vertices:
            adjacent.setdefault(key[vi], []).append(t.index)
    face = np.array(face)
    unit = face / np.maximum(np.linalg.norm(face, axis=1, keepdims=True), 1e-12)
    out = {}
    for t in mesh.loop_triangles:
        for li, vi in zip(t.loops, t.vertices):
            candidates = np.array(adjacent[key[vi]])
            candidates = candidates[unit[candidates] @ unit[t.index] >= math.cos(math.radians(angle))]
            normal = face[candidates].sum(0)
            out[li] = normal / max(float(np.linalg.norm(normal)), 1e-12)
    return out


def main():
    job = json.loads(Path(sys.argv[sys.argv.index("--") + 1]).read_text(encoding="utf-8"))
    work = Path(job["work"])
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.obj_import(filepath=job["obj"], forward_axis="Y", up_axis="Z")
    objects = [o for o in bpy.context.selected_objects if o.type == "MESH"]
    if not objects:
        raise ValueError("El OBJ no contiene mallas")
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = job["nombre"]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    P = np.array([v.co[:] for v in obj.data.vertices])
    extent = np.ptp(P, axis=0)
    up = job["arriba"]
    if up == "auto":
        up = "y" if extent[1] >= extent[2] else "z"
    # Marco de trabajo B3: X lateral, Y arriba, Z hacia la cara. Blender usa Z arriba.
    if up == "y":
        P = P[:, [0, 2, 1]] * np.array([1, -1, 1])
    angle = math.radians(job["giro"])
    rot = np.array([[math.cos(angle), -math.sin(angle), 0],
                    [math.sin(angle), math.cos(angle), 0], [0, 0, 1]])
    P = P @ rot.T
    low, high = P.min(0), P.max(0)
    height = float(high[2] - low[2])
    if height <= 1e-8:
        raise ValueError("Malla de altura nula")
    W = np.array(job["world"])
    labels = [suffix(s) for s in job["labels"]]
    parents = job["parent"]
    def bi(s):
        return labels.index(s) if s in labels else None
    head = bi("HEAD")
    if head is None:
        raise ValueError("Donante sin HEAD: necesita un esqueleto humanoide")
    feet = [bi(s + "FOOT1") for s in "LR"]
    if any(i is None for i in feet):
        raise ValueError("Donante sin pies: necesita un esqueleto humanoide")
    # El origen B3 está en la cadera, NO en la suela: la cabeza puede estar en +5
    # y los pies en -9. No se debe confundir head.y con la altura del personaje.
    floor = min(W[i, 1, 3] for i in feet) - .5
    donor_h = max(1., (W[head, 1, 3] - floor) / .88)
    P = (P - np.array([(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, low[2]])) * donor_h / height
    if job.get("forma") == "refinada":
        P = refinar_cabeza(P, donor_h)
    for v, p in zip(obj.data.vertices, P):
        v.co = p + np.array([0., 0., floor])
    obj.data.update()
    # Silueta: secciones del tronco y extremos de cada miembro, antes de decimar.
    H = donor_h
    def section(z, thick=.035, central=False):
        q = P[np.abs(P[:, 2] / H - z) < thick]
        if central:
            q = q[np.abs(q[:, 0]) < H * .19]
        return q if len(q) > 4 else P
    def mid(z):
        q = section(z, central=True)
        return np.array([0., float(np.median(q[:, 1])), z * H])
    landmarks = {"WAIST": mid(.50), "STMC": mid(.59), "CHEST": mid(.72),
                 "NECK": mid(.82), "HEAD": mid(.88)}
    # Signo L/R del donante (algunos esqueletos usan X negativo para el lado izquierdo).
    for side in "LR":
        arm = bi(side + "ARM1")
        sign = np.sign(W[arm, 0, 3]) if arm is not None else (1 if side == "L" else -1)
        sign = sign or (1 if side == "L" else -1)
        q = P[(P[:, 0] * sign > H * .21) & (P[:, 2] > H * .38) & (P[:, 2] < H * .86)]
        if len(q) < 10:
            raise ValueError("No se distinguen los brazos: usa una pose T/A o --landmarks")
        # Extremo de mano con percentil para no usar un único vértice aislado.
        end = q[q[:, 0] * sign >= np.quantile(q[:, 0] * sign, .91)].mean(0)
        shoulder = np.array([sign * H * .105, mid(.76)[1], H * .76])
        wrist = shoulder + .83 * (end - shoulder)
        elbow = shoulder + .46 * (end - shoulder)
        landmarks.update({side + "CHN": shoulder * np.array([.55, 1, 1]),
                          side + "ARMROT": shoulder, side + "ARM1": shoulder,
                          side + "ARM2": elbow, side + "HANDROT": wrist,
                          "L00_" + side + "HAND": wrist})
        legq = P[(P[:, 0] * sign > 0) & (P[:, 2] < .12 * H)]
        foot = legq.mean(0) if len(legq) else np.array([sign * H * .055, 0, H * .04])
        hip = np.array([sign * H * .055, mid(.49)[1], H * .49])
        knee = hip * .5 + foot * .5
        ankle = foot.copy(); ankle[2] = H * .055
        landmarks.update({side + "LEGROT": hip, side + "LEG1": hip,
                          side + "LEG2": knee, side + "FOOT1": ankle,
                          side + "FOOT2": foot})
    # Ajustes opcionales normalizados: X lateral, Y arriba, Z frente (coordenadas B3).
    for name, p in job.get("landmarks", {}).items():
        landmarks[name] = np.array([p[0], -p[2], p[1]]) * H
    # Conserva las rotaciones y jerarquía originales; solo adapta los pivotes de reposo.
    old = W.copy()
    fitted = W.copy()
    for i in range(len(W)):
        p = parents[i]
        if labels[i] in landmarks:
            v = landmarks[labels[i]]
            fitted[i, :3, 3] = [v[0], v[2] + floor, -v[1]]
        elif p >= 0:
            fitted[i, :3, 3] += fitted[p, :3, 3] - old[p, :3, 3]
    # Rig de pesos: segmentos anatómicos; excluye nulos, variantes y rotadores coincidentes.
    tails = {"WAIST": "STMC", "STMC": "CHEST", "CHEST": "NECK", "NECK": "HEAD"}
    for s in "LR":
        tails.update({s + "ARM1": s + "ARM2", s + "ARM2": "L00_" + s + "HAND",
                      s + "LEG1": s + "LEG2", s + "LEG2": s + "FOOT1",
                      s + "FOOT1": s + "FOOT2"})
    deform = set(tails) | {"HEAD", "L00_LHAND", "L00_RHAND"}
    rig_data = bpy.data.armatures.new("B3_auto")
    rig = bpy.data.objects.new("B3_auto", rig_data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    def coord(i):
        p = fitted[i, :3, 3]
        return Vector((p[0], -p[2], p[1]))
    segments = {}
    for i, name in enumerate(labels):
        b = rig_data.edit_bones.new(str(i))
        b.head = coord(i)
        target = bi(tails.get(name, ""))
        b.tail = coord(target) if target is not None else b.head + Vector((0, 0, H * .065))
        if (b.tail - b.head).length < H * .008:
            b.tail = b.head + Vector((0, 0, H * .045))
        if name == "HEAD":
            b.tail = b.head + Vector((0, 0, H * .105))
        if name in ("L00_LHAND", "L00_RHAND"):
            s = "L" if name == "L00_LHAND" else "R"
            elbow = coord(bi(s + "ARM2"))
            b.tail = b.head + (b.head - elbow).normalized() * H * .065
        b.use_deform = name in deform
        segments[i] = (np.array(b.head), np.array(b.tail))
    for i, p in enumerate(parents):
        if p >= 0:
            rig_data.edit_bones[str(i)].parent = rig_data.edit_bones[str(p)]
    bpy.ops.object.mode_set(mode="OBJECT")
    # Decimación antes de heat weights: menos coste y pesos válidos para el pool definitivo.
    before = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    if before > job["triangulos"]:
        # El collapse uniforme gastaba los triangulos en los pliegues del gi y
        # deshacia ojos, mandibula y puntas. Prioridad anatomica sobre la propia
        # fuente: sin trasplantar mallas y sin cambiar el presupuesto total.
        detail = None
        if job.get("forma") == "refinada":
            detail = obj.vertex_groups.new(name="Detalle_cabeza_OBJ")
            for v in obj.data.vertices:
                z = (v.co.z - floor) / H
                weight = float(np.clip((z - .79) / .055, 0, 1))
                if abs(v.co.x) < H * .17 and weight > 0:
                    detail.add([v.index], weight, "REPLACE")
        mod = obj.modifiers.new("Densidad_B3", "DECIMATE")
        mod.ratio = job["triangulos"] / before
        mod.use_collapse_triangulate = True
        if detail is not None:
            mod.vertex_group = detail.name
            mod.vertex_group_factor = 20.0
        bpy.ops.object.modifier_apply(modifier=mod.name)
        remaining = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        if remaining > job["triangulos"]:
            # Con pesos de proteccion Blender puede detenerse antes del objetivo.
            # El presupuesto explicito sigue siendo un limite, no una sugerencia.
            cap = obj.modifiers.new("Limite_B3", "DECIMATE")
            cap.ratio = max(1, job["triangulos"] - 2) / remaining
            cap.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier=cap.name)
    tri = obj.modifiers.new("Triangulos", "TRIANGULATE")
    bpy.ops.object.modifier_apply(modifier=tri.name)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    heat_error = None
    try:
        bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    except RuntimeError as e:
        heat_error = str(e)
    # Sella huecos y limpia cara/dedos; solo exporta mezclas padre-hijo compatibles B3.
    active = [i for i, name in enumerate(labels) if name in deform]
    vertices, triangles, unweighted, cleaned = [], [], 0, 0
    obj.data.calc_loop_triangles()
    uv_layer = obj.data.uv_layers.active
    if uv_layer is None:
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True); bpy.context.view_layer.objects.active = obj
        bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(island_margin=.02)
        bpy.ops.object.mode_set(mode="OBJECT")
        uv_layer = obj.data.uv_layers.active
    assignments = {}
    for v in obj.data.vertices:
        weights = []
        for g in v.groups:
            name = obj.vertex_groups[g.group].name
            if name.isdigit() and int(name) in active and g.weight > .001:
                weights.append((int(name), float(g.weight)))
        point = np.array(v.co)
        if not weights:
            unweighted += 1
            distances = []
            for b in active:
                x, y = segments[b]; d = y - x
                t = np.clip(np.dot(point - x, d) / np.dot(d, d), 0, 1)
                distances.append((float(np.linalg.norm(point - (x + t * d))), b))
            weights = [(min(distances)[1], 1.)]
        weights.sort(key=lambda it: -it[1])
        b, weight = weights[0][0], 1.
        if point[2] > floor + H * .835 and abs(point[0]) < H * .15:
            b = head; cleaned += 1
        else:
            for side in "LR":
                hb = bi("L00_" + side + "HAND")
                if hb is not None and (point - segments[hb][0]) @ (segments[hb][1] - segments[hb][0]) > 0:
                    if np.linalg.norm(point - segments[hb][0]) < H * .11:
                        b = hb; cleaned += 1; break
            if len(weights) > 1 and b == weights[0][0]:
                c, wc = weights[1]
                # El resto va SOLO al padre inmediato; no se simulan cuatro influencias.
                if parents[b] == c:
                    weight = weights[0][1] / (weights[0][1] + wc)
                elif parents[c] == b:
                    b, weight = c, wc / (weights[0][1] + wc)
        assignments[v.index] = (b, max(.001, min(1., weight)))
    # Atlas único de materiales/vertex-colours; diffuse bake sin luz ni sombras pintadas añadidas.
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 1
    atlas = bpy.data.images.new("B3_cel_atlas", width=job["textura"], height=job["textura"], alpha=True)
    direct_image = None
    if not obj.data.materials:
        mat = bpy.data.materials.new("Sin_MTL"); mat.diffuse_color = (.65, .65, .65, 1)
        obj.data.materials.append(mat)
    for slot in obj.material_slots:
        if slot.material is None:
            slot.material = bpy.data.materials.new("Sin_material")
        m = slot.material
        color = m.diffuse_color[:]
        m.use_nodes = True
        bsdf = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if len(obj.material_slots) == 1 and bsdf and bsdf.inputs["Base Color"].is_linked:
            link = bsdf.inputs["Base Color"].links[0]
            source_node = link.from_node
            if (source_node.type == "TEX_IMAGE" and link.from_socket.name == "Color"
                    and source_node.image and source_node.projection == "FLAT"
                    and not source_node.inputs["Vector"].is_linked):
                direct_image = source_node.image
        if bsdf and not bsdf.inputs["Base Color"].is_linked:
            bsdf.inputs["Base Color"].default_value = color
            colors = next(iter(obj.data.color_attributes), None)
            if colors:
                node = m.node_tree.nodes.new("ShaderNodeVertexColor"); node.layer_name = colors.name
                m.node_tree.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
        node = m.node_tree.nodes.new("ShaderNodeTexImage"); node.image = atlas
        m.node_tree.nodes.active = node
    # Se conserva UV original si hay un único material; con varios, atlas nuevo y UV fuente separada.
    source_uv = uv_layer.name
    if len(obj.material_slots) > 1:
        for slot in obj.material_slots:
            m = slot.material
            for node in list(m.node_tree.nodes):
                if node.type == "TEX_IMAGE" and node.image != atlas and not node.inputs["Vector"].is_linked:
                    uvnode = m.node_tree.nodes.new("ShaderNodeUVMap"); uvnode.uv_map = source_uv
                    m.node_tree.links.new(uvnode.outputs["UV"], node.inputs["Vector"])
        uv_layer = obj.data.uv_layers.new(name="B3_atlas")
        obj.data.uv_layers.active = uv_layer; uv_layer.active_render = True
        bpy.ops.object.select_all(action="DESELECT"); obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(island_margin=.008)
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT"); obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    scene.render.bake.use_pass_direct = False; scene.render.bake.use_pass_indirect = False
    scene.render.bake.use_pass_color = True; scene.render.bake.margin = 4
    if direct_image is not None:
        # Un material con imagen y UV existentes no necesita bake. El margen del
        # bake invadía islas próximas/reutilizadas y pintaba motas en cara y ropa.
        # Escalar una copia conserva el color original sin tocar la fuente.
        resized = direct_image.copy()
        resized.scale(job["textura"], job["textura"])
        atlas.pixels[:] = resized.pixels[:]
    else:
        bpy.ops.object.bake(type="DIFFUSE")
    # La imagen Tripo contiene iluminación: eliminarla antes del toon del juego.
    pixels = np.array(atlas.pixels[:], dtype=np.float32).reshape(-1, 4)
    pixels[:, :3] = np.clip(pixels[:, :3], 0, 1)
    palette = []
    padding = 0
    if job.get("acabado", "original") == "cel":
        tinta = afinar_tinta(pixels[:, :3], job["textura"])
        pixels[:, :3], palette = paleta_cel(pixels[:, :3])
        # Rasterizar tras EDIT: sus wrappers UV anteriores ya no son válidos.
        padding = dilatar_islas(pixels, obj.data, job["textura"])
    else:
        tinta = 0
    # PNG opaco para Blender/retratos; el writer convierte a alfa toon 0 en DDS.
    pixels[:, 3] = 1
    atlas.pixels[:] = pixels.ravel()
    atlas.filepath_raw = str(work / "atlas.png"); atlas.file_format = "PNG"; atlas.save()
    # Salir de EDIT invalida los wrappers RNA anteriores en Blender 5.2. Con
    # varios materiales, recuperar UV y triangulación del mesh definitivo.
    obj.data.calc_loop_triangles()
    uv_layer = obj.data.uv_layers.active
    smooth = normales_suaves(obj.data)
    for t in obj.data.loop_triangles:
        ids = []
        center_uv = np.mean([uv_layer.data[li].uv[:] for li in t.loops], axis=0)
        tx, ty = np.clip((center_uv * job["textura"]).astype(int), 0, job["textura"] - 1)
        color = pixels.reshape(job["textura"], job["textura"], 4)[ty, tx, :3]
        # Como el pelo negro nativo: negro plano, sin facetas grises ni rim.
        # Incluye también los reflejos azul oscuro horneados del negro. Los
        # pigmentos claros (piel/ojos/banda azul/dorado) no entran en ese rango.
        black_head = (job.get("acabado") == "cel" and float(color.max()) < .40
                      and all(assignments[vi][0] == head for vi in t.vertices))
        # Mascara del propio OBJ, con la convencion nativa N=0. Solo las telas
        # oscuras/negras: no se anulan las normales de piel ni del rival. Se
        # clasifica por cara para no interpolar mascara a traves de una costura.
        dark_cloth = (job.get("acabado") == "cel" and float(color.max()) < .25)
        cloth_color = None
        if dark_cloth and not black_head:
            # N=0 evita el halo, pero no debe borrar todos los pliegues del gi.
            # Tres planos cel a partir de las normales PROPIAS, no luces pintadas
            # de la foto ni una rampa de otro personaje. Quedan ligados al OBJ.
            face_n = np.mean([smooth[li] for li in t.loops], axis=0)
            light = float(face_n @ np.array([.35, -.48, .8]))
            shade = 1.0 if light > .35 else .82 if light > -.25 else .66
            cloth_color = np.rint(color * 255 * shade).astype(int).tolist()
        for li in t.loops:
            loop = obj.data.loops[li]; v = obj.data.vertices[loop.vertex_index]
            b, w = assignments[v.index]
            n = smooth[li]
            ids.append(len(vertices))
            vertices.append(dict(pos=[v.co.x, v.co.z, -v.co.y], normal=[float(n[0]), float(n[2]), -float(n[1])],
                                  uv=[uv_layer.data[li].uv.x, 1 - uv_layer.data[li].uv.y], bone=b, weight=w,
                                   sin_luz=black_head, sin_rim=dark_cloth))
            if cloth_color is not None:
                vertices[-1]["color_sin_rim"] = cloth_color
        triangles.append(ids)
    report = dict(arriba=up, altura=H, suelo=floor, triangulos_antes=before, triangulos=len(triangles),
                   atlas_metodo="imagen_uv_directa" if direct_image is not None else "bake_materiales",
                   acabado=job.get("acabado", "original"), paleta_srgb=palette,
                   padding_texeles=padding, padding_margen=8, tinta_reforzada=tinta,
                     normales="area_65_grados_sin_costuras_uv", alfa_toon=0,
                    ropa_cel="tres_planos_del_OBJ_sin_rim; sombras_ligadas_a_la_malla",
                    forma=job.get("forma", "fuente"),
                    detalle_cabeza=("fuente_integra" if before <= job["triangulos"] else
                                    "collapse_ponderado_20" if job.get("forma") == "refinada" else "collapse_uniforme"),
                    triangulos_cabeza=sum(all(assignments[vi][0] == head for vi in t.vertices)
                                         for t in obj.data.loop_triangles),
                  vertices=len(obj.data.vertices), heat_error=heat_error, sin_pesos_heat=unweighted,
                  limpieza_cara_manos=cleaned, landmarks={k: v.tolist() for k, v in landmarks.items()},
                  limitacion="Auto-rig humanoide T/A; revisar en combate. Cara y dedos rígidos, sin expresiones nuevas.")
    mesh = dict(world=fitted.tolist(), vertices=vertices, triangles=triangles,
                texture=str(work / "atlas.png"), report=report)
    (work / "malla.json").write_text(json.dumps(mesh), encoding="utf-8")
    # Vista de inspección y retrato: usa atlas, no imágenes copiadas del donante.
    m = bpy.data.materials.new("Cel_previa"); m.use_nodes = True
    nodes = m.node_tree.nodes; nodes.clear()
    tex = nodes.new("ShaderNodeTexImage"); tex.image = atlas
    emission = nodes.new("ShaderNodeEmission")
    output = nodes.new("ShaderNodeOutputMaterial")
    m.node_tree.links.new(tex.outputs["Color"], emission.inputs["Color"])
    m.node_tree.links.new(emission.outputs[0], output.inputs[0])
    obj.data.materials.clear(); obj.data.materials.append(m)
    for p in obj.data.polygons:
        p.material_index = 0
    scene.render.engine = "BLENDER_EEVEE"
    scene.world.color = (.08, .08, .08)
    scene.view_settings.view_transform = "Standard"
    scene.render.resolution_x = 768; scene.render.resolution_y = 768
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = True
    cam_data = bpy.data.cameras.new("Retrato"); cam = bpy.data.objects.new("Retrato", cam_data)
    bpy.context.collection.objects.link(cam); scene.camera = cam
    cam_data.type = "ORTHO"; cam_data.ortho_scale = H * 1.13
    views = []
    for k, angle in enumerate((0, 90, 180, 270)):
        r = math.radians(angle)
        cam.location = (math.sin(r) * H * 3, -math.cos(r) * H * 3, floor + H * .53)
        cam.rotation_euler = (Vector((0, 0, floor + H * .51)) - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(work / ("vista_%d.png" % k))
        bpy.ops.render.render(write_still=True)
        views.append(scene.render.filepath)
    # Retrato legible en la rueda: cabeza y torso, no los brazos completos de la T.
    cam.location = (H * .22, -H * 3, floor + H * .80)
    cam.rotation_euler = (Vector((0, 0, floor + H * .78)) - cam.location).to_track_quat("-Z", "Y").to_euler()
    cam_data.ortho_scale = H * .50
    scene.render.resolution_x = 512; scene.render.resolution_y = 512
    scene.render.filepath = str(work / "retrato.png")
    bpy.ops.render.render(write_still=True)
    # Blender lleva numpy, no Pillow: compone una lámina con los píxeles ya renderizados.
    images = [bpy.data.images.load(p, check_existing=False) for p in views]
    sheet = bpy.data.images.new("Lamina", width=768 * 4, height=768, alpha=True)
    data = np.concatenate([np.array(im.pixels[:]).reshape(768, 768, 4) for im in images], axis=1)
    sheet.pixels[:] = data.ravel(); sheet.filepath_raw = str(work / "lamina_blender.png")
    sheet.file_format = "PNG"; sheet.save()
    bpy.ops.wm.save_as_mainfile(filepath=str(work / "rig.blend"))
    print("PASS Blender: %d tris, %d vértices sin heat, %s" % (len(triangles), unweighted, up))


if __name__ == "__main__":
    main()

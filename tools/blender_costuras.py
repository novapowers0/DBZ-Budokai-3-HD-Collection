"""Costuras originales para Blender, en coordenadas locales del hueso de reposo.

Importar y llamar a ajustar(objeto, rig, zona, indices). Ambos lados deben
usar exactamente el mismo rig y zona. Los valores son una plantilla inicial,
no medidas verificadas sobre todos los cuerpos B3.
"""
import math

# Eje local XY del hueso; radio en unidades Blender/B3. Un hueso efectivo,
# compatible con el formato B3 de una sola influencia por ventana.
ANILLOS = {
    'cuello': ('_NECK', .12),
    'hombro_izq': ('_LARM1', .115),
    'hombro_der': ('_RARM1', .115),
    'cintura': ('_WAIST', .22),
    'muneca_izq': ('_LHANDROT', .065),
    'muneca_der': ('_RHANDROT', .065),
    'tobillo_izq': ('_LFOOT1', .075),
    'tobillo_der': ('_RFOOT1', .075),
}
VERTICES = 16


def _orden(obj, indices):
    ids = list(indices)
    if len(ids) != VERTICES or len(set(ids)) != VERTICES:
        raise ValueError('El borde debe contener exactamente 16 vertices distintos')
    if any(not isinstance(i, int) or i < 0 or i >= len(obj.data.vertices) for i in ids):
        raise ValueError('Indice de vertice invalido')
    chosen = set(ids)
    # Una arista del borde pertenece a una sola cara; no aceptar un corte interior.
    counts = {}
    for poly in obj.data.polygons:
        vs = list(poly.vertices)
        for a, b in zip(vs, vs[1:] + vs[:1]):
            key = tuple(sorted((a, b)))
            counts[key] = counts.get(key, 0) + 1
    neighbors = {i: [] for i in ids}
    for (a, b), count in counts.items():
        if a in chosen and b in chosen and count == 1:
            neighbors[a].append(b)
            neighbors[b].append(a)
    if any(len(v) != 2 for v in neighbors.values()):
        raise ValueError('La seleccion debe ser un unico borde abierto cerrado')
    order = [min(ids)]
    previous = None
    while True:
        nxt = min(x for x in neighbors[order[-1]] if x != previous)
        if nxt == order[0]:
            break
        if nxt in order:
            raise ValueError('Borde desconectado o ambiguo')
        previous, nxt_old = order[-1], nxt
        order.append(nxt_old)
    if len(order) != VERTICES:
        raise ValueError('El borde contiene varios ciclos')
    return order


def ajustar(obj, rig, zona, indices):
    """Ajusta borde y pesos en reposo; devuelve indices en orden canonico.

    El objeto puede tener transformaciones. El rig debe estar en pose de reposo;
    el llamante agrega el modificador Armature si aun no existe.
    """
    from mathutils import Vector
    if obj.type != 'MESH' or rig.type != 'ARMATURE' or obj.mode != 'OBJECT':
        raise ValueError('Se necesita malla en modo objeto y armature')
    if zona not in ANILLOS:
        raise ValueError('Zona desconocida')
    suffix, radius = ANILLOS[zona]
    matches = [b for b in rig.data.bones if b.name.endswith(suffix)]
    if len(matches) != 1:
        raise ValueError('Hueso ausente o ambiguo: ' + suffix)
    bone = matches[0]
    order = _orden(obj, indices)
    frame = rig.matrix_world @ bone.matrix_local
    local = obj.matrix_world.inverted() @ frame
    ring = [local @ Vector((radius * math.cos(2 * math.pi * i / VERTICES),
                            radius * math.sin(2 * math.pi * i / VERTICES), 0.))
            for i in range(VERTICES)]
    # Elegir correspondencia ciclica que minimiza desplazamiento sin cruzar.
    candidates = []
    for seq in (order, list(reversed(order))):
        for shift in range(VERTICES):
            rotated = seq[shift:] + seq[:shift]
            score = sum((obj.data.vertices[v].co - ring[i]).length_squared
                        for i, v in enumerate(rotated))
            candidates.append((score, rotated))
    order = min(candidates, key=lambda item: item[0])[1]
    group = obj.vertex_groups.get(bone.name) or obj.vertex_groups.new(name=bone.name)
    # Quitar otras influencias solo para estos vertices, sin afectar el resto.
    for g in obj.vertex_groups:
        if g.index != group.index:
            g.remove(order)
    group.add(order, 1.0, 'REPLACE')
    for i, vertex in enumerate(order):
        obj.data.vertices[vertex].co = ring[i]
    obj.data.update()
    return order

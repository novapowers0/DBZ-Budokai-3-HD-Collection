"""
Parser de FBX ASCII (exportado por emdfbx.exe -ExportAscii) para Budokai HD.

Extrae: vertices, indices (triangulos), normales, uvs, y skinning
(clusters con weights por vertice + huesos).

El FBX de emdfbx (LibXenoverse) de un EMD de SDBH WM tiene:
  Geometry > Vertices (3 floats por vertice), PolygonVertexIndex (int, negativo=fin de polygono)
  LayerElementNormal > Normals
  LayerElementUV > UV
  Deformer (skin) > SubDeformer (cluster) > Indexes + Weights
  Model con Lcl Translation/Rotation/Scaling (pose de los huesos)

Uso:
  from fbx_ascii import FbxAscii
  p = FbxAscii(path)
  p.parse()
  p.meshes[0].verts / .tris / .nrm / .uv
  p.bones  -> [{'name', 'translation', 'rotation', 'scaling'}]
  p.weights_per_vertex (cluster idx -> (bone_name, weight))
"""

import re
import struct


def parse_floats(s):
    # quitar 'a: ' y limpiar
    s = s.replace('a:', ' ')
    return [float(x) for x in s.replace('\n', ' ').replace('\t', ' ').split(',') if x.strip()]


def parse_ints(s):
    s = s.replace('a:', ' ')
    return [int(x) for x in s.replace('\n', ' ').replace('\t', ' ').split(',') if x.strip()]


def extract_block(text, start):
    """Extrae un bloque {...} desde la llave en start, balanceando anidados."""
    depth = 0
    i = start
    in_str = False
    while i < len(text):
        c = text[i]
        if c == '"':
            in_str = not in_str
        elif not in_str:
            if c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    return text[start:i+1]
        i += 1
    return text[start:]


def extract_array(text, kw):
    """Extrae el array del bloque 'KW: *N { a: ... }' dentro de text."""
    m = re.search(r'%s: \*(\d+) \{' % kw, text)
    if not m:
        return None
    # buscar 'a:' tras la llave
    am = re.search(r'a: ', text[m.end():])
    if not am:
        return None
    start = m.end() + am.end()
    # buscar el cierre '}' al nivel 1 (el array no tiene llaves anidadas)
    end = text.find('}', start)
    if end < 0:
        return None
    return text[start:end]


class FbxAscii:
    def __init__(self, path):
        self.path = path
        self.text = open(path, 'r', encoding='utf-8', errors='replace').read()
        self.objects = {}
        self.meshes = []
        self.bones = []
        self.skins = []

    def parse(self):
        text = self.text
        # --- Geometry ---
        for m in re.finditer(r'Geometry: (\d+), "([^"]+)", "([^"]+)" \{', text):
            oid, name, gclass = m.group(1), m.group(2), m.group(3)
            block = extract_block(text, m.end()-1)
            geo = {'name': name, 'id': oid}
            av = extract_array(block, 'Vertices')
            if av:
                vals = parse_floats(av)
                geo['verts'] = [(vals[i], vals[i+1], vals[i+2]) for i in range(0, len(vals)-2, 3)]
            ap = extract_array(block, 'PolygonVertexIndex')
            if ap:
                idx = parse_ints(ap)
                tris = []
                poly = []
                for x in idx:
                    real = -x - 1 if x < 0 else x
                    poly.append(real)
                    if x < 0:
                        for k in range(1, len(poly)-1):
                            tris.append((poly[0], poly[k], poly[k+1]))
                        poly = []
                geo['tris'] = tris
            # normales
            an = extract_array(block, 'Normals')
            if an:
                vals = parse_floats(an)
                geo['nrm'] = [(vals[i], vals[i+1], vals[i+2]) for i in range(0, len(vals)-2, 3)]
            # uv
            au = extract_array(block, 'UV')
            if au:
                vals = parse_floats(au)
                geo['uv'] = [(vals[i], vals[i+1]) for i in range(0, len(vals)-1, 2)]
            aui = extract_array(block, 'UVIndex')
            if aui:
                geo['uv_idx'] = parse_ints(aui)
            self.meshes.append(geo)

        # --- Model ---
        for m in re.finditer(r'Model: (\d+), "([^"]+)", "([^"]+)" \{', text):
            oid, name, mclass = m.group(1), m.group(2), m.group(3)
            block = extract_block(text, m.end()-1)
            model = {'name': name, 'class': mclass, 'id': oid}
            tm = re.search(r'Lcl Translation: (.*?)\n', block)
            if tm:
                model['translation'] = tuple(parse_floats(tm.group(1))[:3])
            rm = re.search(r'Lcl Rotation: (.*?)\n', block)
            if rm:
                model['rotation'] = tuple(parse_floats(rm.group(1))[:3])
            sm = re.search(r'Lcl Scaling: (.*?)\n', block)
            if sm:
                model['scaling'] = tuple(parse_floats(sm.group(1))[:3])
            self.objects[oid] = model
            self.objects[name] = model
            if mclass == 'LimbNode':
                self.bones.append(model)

        # --- Deformers (clusters de skinning) ---
        # Los clusters: 'Deformer: <id>, "SubDeformer::larm2", "Cluster" {'
        for sm in re.finditer(r'Deformer: (\d+), "SubDeformer::([^"]+)", "Cluster" \{', text):
            cid, cname = sm.group(1), sm.group(2)
            cblock = extract_block(text, sm.end()-1)
            cluster = {'name': cname, 'id': cid}
            im = extract_array(cblock, 'Indexes')
            if im:
                cluster['indexes'] = parse_ints(im)
            wm = extract_array(cblock, 'Weights')
            if wm:
                cluster['weights'] = parse_floats(wm)
            tm = extract_array(cblock, 'Transform')
            if tm:
                cluster['transform'] = parse_floats(tm)
            self.skins.append(cluster)

        # --- Connections ---
        self.connections = []
        for m in re.finditer(r'C: "(\w+)",(\d+),(\d+)', text):
            self.connections.append((m.group(1), m.group(2), m.group(3)))

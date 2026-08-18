"""
Parser de FBX binario (Autodesk FBX 7.x) para extraer geometria + skinning.

Formato FBX binario:
  header: 'Kaydara FBX Binary  ' + 0x1a 0x00 + version(u32 LE)
  luego nodos: [end_offset u64][num_props u32][prop_list_len u32][name_len u8][name][props][children...]
  props: por cada prop, 1 byte de tipo + datos
    tipos: Y=2B, C=1B, I=4B, F=4B, D=8B, L=8B, S=string, R=raw, 
           f=array float, i=array int, l=array long, d=array double, b=array bool
  arrays: [len u32][encoding u32][compressed_len u32][datos]

Estructura de datos que buscamos:
  Objects > Geometry: Vertices(f/d array), PolygonVertexIndex(i array),
    LayerElementNormal > Normals: Normals(d/f array) + MappingInformationType + ReferenceInformationType
    LayerElementUV > UV: UV(d/f array) + UVIndex(i array)
    LayerElementMaterial > Materials
  Objects > Model (mesh): Properties > Lcl Translation/Rotation/Scaling
  Objects > Model (bone) + Deformer (skin): SubDeformer (cluster) > 
    Indexes(i array), Weights(d array), Transform, TransformLink
  Connections: geometria <-> modelo, modelo <-> cluster, cluster <-> bone

Uso:
  from fbx_parser import FbxParser
  p = FbxParser(fbx_bytes)
  p.parse()  -> p.objects['Geometry'], p.objects['Model'], p.connections
"""

import struct
import sys


class FbxParser:
    def __init__(self, data):
        self.data = data
        self.pos = 23 + 4  # header 23 + version 4
        self.objects = {}   # id -> {type, name, props, children}
        self.connections = []

    def _read(self, n):
        b = self.data[self.pos:self.pos+n]
        self.pos += n
        return b

    def _read_prop(self):
        t = chr(self._read(1)[0])
        if t == 'Y':
            return struct.unpack('<H', self._read(2))[0]
        if t == 'C':
            return self._read(1)[0]
        if t == 'I':
            return struct.unpack('<i', self._read(4))[0]
        if t == 'F':
            return struct.unpack('<f', self._read(4))[0]
        if t == 'D':
            return struct.unpack('<d', self._read(8))[0]
        if t == 'L':
            return struct.unpack('<q', self._read(8))[0]
        if t == 'S':
            ln = struct.unpack('<I', self._read(4))[0]
            s = self._read(ln)
            if ln > 0 and s[-1] == 0:
                s = s[:-1]
            return s.decode('utf-8', 'replace')
        if t == 'R':
            ln = struct.unpack('<I', self._read(4))[0]
            return self._read(ln)
        if t in 'fildb':
            arr_len = struct.unpack('<I', self._read(4))[0]
            enc = struct.unpack('<I', self._read(4))[0]
            comp_len = struct.unpack('<I', self._read(4))[0]
            fmt = {'f': 'f', 'i': 'i', 'l': 'q', 'd': 'd', 'b': '?'}[t]
            size = {'f': 4, 'i': 4, 'l': 8, 'd': 8, 'b': 1}[t]
            if enc == 0:
                raw = self._read(arr_len * size)
                return list(struct.unpack('<%d%s' % (arr_len, fmt), raw))
            else:
                # comprimido (rare) - leer comp_len y no descomprimir
                self._read(comp_len)
                return None
        # tipo desconocido
        return None

    def _read_node(self, max_end):
        if self.pos + 13 > max_end:
            return None
        end_offset = struct.unpack('<Q', self._read(8))[0]
        num_props = struct.unpack('<I', self._read(4))[0]
        prop_list_len = struct.unpack('<I', self._read(4))[0]
        name_len = self._read(1)[0]
        name = self._read(name_len).decode('utf-8', 'replace') if name_len else ''
        props_end = self.pos + prop_list_len
        props = []
        # leer props hasta props_end (con un limite de seguridad)
        guard = 0
        while self.pos < props_end and guard < 10000:
            guard += 1
            props.append(self._read_prop())
        # los children van desde props_end hasta end_offset
        self.pos = props_end
        children = []
        guard2 = 0
        while self.pos < end_offset and self.pos < len(self.data) and guard2 < 100000:
            guard2 += 1
            c = self._read_node(end_offset)
            if c is None:
                break
            children.append(c)
            if self.pos >= end_offset:
                break
        self.pos = end_offset if end_offset > self.pos else self.pos
        return {'name': name, 'props': props, 'children': children, 'end': end_offset}

    def parse(self):
        # recorrer nodos raiz hasta el final
        while self.pos < len(self.data):
            n = self._read_node(len(self.data))
            if n is None:
                break
            self._walk(n)

    def _walk(self, node, parent=None):
        if node['name'] == 'Object':
            # Objects > Geometry/Model/etc
            self._collect_object(node)
        elif node['name'] == 'Connection':
            self._collect_connection(node)
        # else: ignorar otros contenedores, pero seguir children
        for c in node['children']:
            self._walk(c, node)

    def _collect_object(self, node):
        # props: [id u64, type_name string, subtype string]
        oid = node['props'][0] if node['props'] else None
        otype = node['props'][1] if len(node['props']) > 1 else ''
        # hijos del Object: el nombre del object esta en un child con props [name]
        name = ''
        for c in node['children']:
            if c['name'] == '' and c['props'] and isinstance(c['props'][0], str):
                name = c['props'][0]
        self.objects[oid] = {'type': otype, 'name': name, 'node': node}

    def _collect_connection(self, node):
        # props: [type, child_id, parent_id]
        if node['props'] and len(node['props']) >= 3:
            self.connections.append((node['props'][0], node['props'][1], node['props'][2]))

    def get_object(self, oid):
        return self.objects.get(oid)

    def find_objects(self, otype):
        return {k: v for k, v in self.objects.items() if v['type'] == otype}

    def find_child(self, node, name):
        for c in node['children']:
            if c['name'] == name:
                return c
        return None

    def find_prop_array(self, node, name):
        """Busca un array (f/d/i) en un nodo por nombre de child."""
        for c in node['children']:
            if c['name'] == name and c['props']:
                # el array esta en props[0] o en un child
                for p in c['props']:
                    if isinstance(p, list):
                        return p
                # puede estar en child '' con props
                for cc in c['children']:
                    if cc['props'] and isinstance(cc['props'][0], list):
                        return cc['props'][0]
        return None

    def get_prop(self, node, key):
        """Lee un child property scalar (ej Lcl Translation)."""
        for c in node['children']:
            if c['name'] == key and c['props']:
                if len(c['props']) == 3 and all(isinstance(p, (int, float)) for p in c['props']):
                    return tuple(c['props'])
                if c['props'] and isinstance(c['props'][0], (int, float)):
                    return c['props'][0]
        return None

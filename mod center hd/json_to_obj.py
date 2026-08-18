"""
SDBH JSON -> OBJ v1 — exportar un modelo SDBH (del FBX parseado) a OBJ.

Para retopología 3D: exporta los meshes de Android 18 a OBJ (world space)
para superponer sobre el Krillin.obj en Blender.

Uso:
  python json_to_obj.py <model_v2.json> <salida.obj>
"""

import io, sys, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

def main():
    if len(sys.argv) < 3:
        print('Uso: json_to_obj.py <model_v2.json> <salida.obj>')
        return
    data = json.load(open(sys.argv[1]))
    out = sys.argv[2]

    all_v = []
    all_vt = []
    all_vn = []
    base = 0
    with open(out, 'w') as f:
        f.write('# SDBH to OBJ export\n')
        for mesh in data['meshes']:
            f.write('o %s\n' % mesh['name'].replace('Geometry::',''))
            verts = mesh.get('verts', [])
            nrm = mesh.get('nrm', [])
            uv = mesh.get('uv', [])
            for i, (x, y, z) in enumerate(verts):
                f.write('v %f %f %f\n' % (x, y, z))
            for i, (u, v) in enumerate(uv):
                f.write('vt %f %f\n' % (u, v))
            for i, (x, y, z) in enumerate(nrm):
                f.write('vn %f %f %f\n' % (x, y, z))
            for t in mesh.get('tris', []):
                a, b, c = t[0]+base+1, t[1]+base+1, t[2]+base+1
                f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (a, a, a, b, b, b, c, c, c))
            base += len(verts)
    print('OBJ: %d verts totales -> %s' % (base, out))


if __name__ == '__main__':
    main()

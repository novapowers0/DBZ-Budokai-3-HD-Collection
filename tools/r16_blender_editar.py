"""Prueba de edicion REAL de vertices en Blender; invocar solo sobre derivados locales."""
import sys
from pathlib import Path
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_partes_b3 import exportar_blend

salida = Path(sys.argv[sys.argv.index('--') + 1])
changed = 0
for obj in bpy.data.objects:
    if obj.type != 'MESH' or 'awg' not in obj:
        continue
    for vertex in obj.data.vertices:
        names = [obj.vertex_groups[g.group].name for g in vertex.groups]
        if any(n.endswith('_HEAD') or '_M_' in n for n in names):
            vertex.co.x *= 1.03
            changed += 1
    obj.data.update()
if not changed:
    raise RuntimeError('No se editaron vertices de cabeza/cara')
exportar_blend(salida)
bpy.ops.wm.save_as_mainfile(filepath=str(salida.with_suffix('.blend')))
print('PASS edicion Blender real: %d vertices, cabeza y expresiones' % changed)

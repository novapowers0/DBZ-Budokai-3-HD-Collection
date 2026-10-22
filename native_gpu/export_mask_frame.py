"""Validate native shadow-mask transforms against independent GPU positions."""
import argparse,json
from pathlib import Path
import numpy as np
from reference_geometry import Geometry,pipeline_flags
from native_scene import NativeDraw,write_scene


def convert(root,row):
    vs=row['stages']['ShaderStage.Vertex'].get('dump_name') or '';ps=row['stages']['ShaderStage.Pixel'].get('dump_name') or ''
    skin='DA9A3E04256563FF' in vs
    if (not skin and '176E0EF1D21F5A52' not in vs) or '0BFD5B9C11C8FC07' not in ps:raise ValueError('unsupported mask family')
    g=Geometry(root,row);c=g.constants();g.n=np.zeros_like(g.n)
    if c.shape!=(10,4) or g.gpu.shape[1]!=4:raise ValueError('unvalidated shadow constants/output layout')
    world,normal,palette=g.transform(c[4:8],skin);projection=g.projection(c);clip=world@projection
    error=float(abs(clip-g.gpu).max())
    if error>.002:raise ValueError('mask position mismatch '+str(error))
    bone=g.bone if skin else np.zeros(len(world));weight=g.weight if skin else np.zeros(len(world))
    vertices=np.column_stack([g.p,np.zeros((len(world),5)),bone,weight,np.zeros((len(world),2)),np.ones(len(world))]).astype('<f4')
    material=np.zeros(20,dtype='<f4');material[3]=11 if skin else 12;material[8]=g.constants('Pixel')[0,0]
    flags=pipeline_flags(row);proof={'event':row['event'],'vertices':len(world),'kind':'skinned' if skin else 'rigid','clip_error':error}
    native=NativeDraw(vertices,g.indices,projection.T.copy(),material,flags,palette,proof=proof)
    expected=vertices.copy();expected[:,:3]=g.gpu[:,:3]/g.gpu[:,3,None];expected[:,8:11]=material[8];expected[:,11]=1
    flat=np.zeros(20,dtype='<f4');flat[3]=3;flat[8:12]=1
    reference=NativeDraw(expected,g.indices,np.eye(4,dtype='<f4'),flat,flags)
    return native,reference


def export(reference,output,start=0,count=4):
    root=Path(reference);rows=json.loads((root/'frame.json').read_text())['draws'][start:start+count];converted=[convert(root,row) for row in rows];output=Path(output)
    write_scene(output,[v[0] for v in converted]);write_scene(output.with_name(output.stem+'-reference.dbzgpu'),[v[1] for v in converted])
    proof={'scope':'Shadow-mask vertex deformation/projection oracle. Standalone positions; not full-frame shadow/blur/resolve fidelity.',
        'draws':len(rows),'vertices':sum(len(v[0].vertices) for v in converted),'validation':[v[0].proof for v in converted]}
    output.with_suffix('.json').write_text(json.dumps(proof,indent=2));print('Validated mask draws',len(rows))
    return converted


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--reference',required=True);p.add_argument('--output',required=True);p.add_argument('--start',type=int,default=0);p.add_argument('--count',type=int,default=4);a=p.parse_args();export(a.reference,a.output,a.start,a.count)

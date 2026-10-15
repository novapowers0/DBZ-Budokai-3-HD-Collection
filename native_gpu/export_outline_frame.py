"""Check native expanded-hull outlines against independent reference vertices."""
import argparse,json
from pathlib import Path
import numpy as np
from reference_geometry import Geometry,pipeline_flags
from native_scene import NativeDraw,write_scene


def convert(root,row):
    name=row['stages']['ShaderStage.Vertex'].get('dump_name') or ''
    skinned='9B2CDBD500DB8645' in name
    if not skinned and '8B5902B7B177B0B0' not in name:raise ValueError('unsupported outline family')
    ps=row['stages']['ShaderStage.Pixel'].get('dump_name') or ''
    if 'A4A965C189287B99' not in ps:raise ValueError('unvalidated outline pixel material')
    g=Geometry(root,row);c=g.constants()
    if c.shape!=(12,4) or not np.array_equal(c[11,:2],[1,0]):raise ValueError('unvalidated outline constants')
    if g.gpu.shape[1]!=8:raise ValueError('unvalidated outline vertex outputs')
    world,normal,palette=g.transform(c[4:8],skinned)
    length=np.linalg.norm(normal,axis=1);direction=normal/np.where(length>0,length,1)[:,None]
    world[:,:3]+=direction*c[9,0];projection=g.projection(c);clip=world@projection
    color=np.tile(c[10],(len(world),1));color[:,3]*=np.any(g.n!=0,axis=1)
    errors={'clip':float(abs(clip-g.gpu[:,:4]).max()),'color':float(abs(color-g.gpu[:,4:8]).max())}
    if errors['clip']>.002 or errors['color']>2e-6:raise ValueError('outline mismatch: '+str(errors))
    bone=g.bone if skinned else np.zeros(len(world));weight=g.weight if skinned else np.zeros(len(world))
    vertices=np.column_stack([g.p,g.n,np.zeros((len(world),2)),bone,weight,np.zeros((len(world),2)),np.ones(len(world))]).astype('<f4')
    material=np.zeros(20,dtype='<f4');material[3]=7 if skinned else 8;material[4]=c[9,0];material[8:12]=c[10]
    flags=pipeline_flags(row);proof={'event':row['event'],'kind':'skinned' if skinned else 'rigid','vertices':len(world),'errors':errors,'outline_width':float(c[9,0])}
    native=NativeDraw(vertices,g.indices,projection.T.copy(),material,flags,palette,proof=proof)
    expected=vertices.copy();expected[:,:3]=g.gpu[:,:3]/g.gpu[:,3,None];expected[:,3:6]=0;expected[:,8:12]=g.gpu[:,4:8]
    flat=np.zeros(20,dtype='<f4');flat[3]=3;flat[8:12]=1
    reference=NativeDraw(expected,g.indices,np.eye(4,dtype='<f4'),flat,flags)
    return native,reference


def export(reference,output,start=0,count=36):
    root=Path(reference)
    rows=json.loads((root/'frame.json').read_text())['draws'] if (root/'frame.json').exists() else json.loads((root/'draws.partial.json').read_text())
    rows=rows[start:start+count]
    converted=[convert(root,row) for row in rows]
    output=Path(output);write_scene(output,[v[0] for v in converted]);write_scene(output.with_name(output.stem+'-reference.dbzgpu'),[v[1] for v in converted])
    proof={'scope':'Outline deformation / extrusion / color oracle; not a complete game frame or validation of stencil and pass ordering.',
        'draws':len(rows),'vertices':sum(len(v[0].vertices) for v in converted),'validation':[v[0].proof for v in converted]}
    output.with_suffix('.json').write_text(json.dumps(proof,indent=2));print('Validated outline draws',len(rows),'max clip error',max(v[0].proof['errors']['clip'] for v in converted))
    return converted


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--reference',required=True);p.add_argument('--output',required=True);p.add_argument('--start',type=int,default=0);p.add_argument('--count',type=int,default=36);a=p.parse_args();export(a.reference,a.output,a.start,a.count)

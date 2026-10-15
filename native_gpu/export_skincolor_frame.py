"""Independent geometry/color/fog oracle for the B461 skinned-color family."""
import argparse,json
from pathlib import Path
import numpy as np
from reference_geometry import Geometry,pipeline_flags
from export_character_frame import character_textures
from native_scene import NativeDraw,write_scene


def convert(root,row):
    name=row['stages']['ShaderStage.Vertex'].get('dump_name') or ''
    if 'B4611D5E3A350359' not in name:raise ValueError('unsupported skinned-color family')
    g=Geometry(root,row);c=g.constants();ps=g.constants('Pixel')
    if c.shape!=(12,4) or ps.shape!=(2,4) or g.gpu.shape[1]!=16 or not np.array_equal(c[11],[1,0,-1,0]):
        raise ValueError('unvalidated skinned-color constants/outputs')
    world,normal,palette=g.transform(c[4:8],True);projection=g.projection(c);clip=world@projection
    color=g.color*c[10];uv=g.uv+c[9,:2]
    fog=np.clip(np.clip((c[8,0]-clip[:,2])*c[8,1],0,1),1-c[8,3],1-c[8,2])
    errors={'clip':float(abs(clip-g.gpu[:,:4]).max()),'uv':float(abs(uv-g.gpu[:,4:6]).max()),
            'color':float(abs(color-g.gpu[:,8:12]).max()),'fog':float(abs(fog-g.gpu[:,12]).max())}
    if any(errors[k]>v for k,v in {'clip':.002,'uv':.00002,'color':.000002,'fog':.00002}.items()):raise ValueError('skinned-color mismatch '+str(errors))
    vertices=np.column_stack([g.p,g.bone,g.weight,np.zeros(len(world)),g.uv,g.color,np.ones(len(world))]).astype('<f4')
    material=np.concatenate([[*c[9,:2],float(ps[1,3]!=0),10],c[8],c[10],ps[0],ps[1]]).astype('<f4')
    flags=pipeline_flags(row);textures=tuple(character_textures(root,row,1))
    proof={'event':row['event'],'vertices':len(world),'errors':errors}
    native=NativeDraw(vertices,g.indices,projection.T.copy(),material,flags,palette,textures,proof=proof)
    expected=vertices.copy();expected[:,:3]=g.gpu[:,:3]/g.gpu[:,3,None];expected[:,3:6]=0;expected[:,6:8]=g.gpu[:,4:6];expected[:,8:12]=g.gpu[:,8:12];expected[:,12]=g.gpu[:,12]
    flat=material.copy();flat[:4]=[0,0,material[2],1];flat[8:12]=1
    reference=NativeDraw(expected,g.indices,np.eye(4,dtype='<f4'),flat,flags,textures=textures)
    return native,reference


def export(reference,output,start=0,count=2):
    root=Path(reference);rows=json.loads((root/'frame.json').read_text())['draws'][start:start+count]
    converted=[convert(root,row) for row in rows];output=Path(output)
    write_scene(output,[v[0] for v in converted]);write_scene(output.with_name(output.stem+'-reference.dbzgpu'),[v[1] for v in converted])
    proof={'scope':'Skinned-color deformation / UV / color / fog oracle; not a complete game-frame pixel-fidelity check.',
        'draws':len(rows),'vertices':sum(len(v[0].vertices) for v in converted),'validation':[v[0].proof for v in converted]}
    output.with_suffix('.json').write_text(json.dumps(proof,indent=2));print('Validated skinned-color draws',len(rows))
    return converted


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--reference',required=True);p.add_argument('--output',required=True);p.add_argument('--start',type=int,default=0);p.add_argument('--count',type=int,default=2);a=p.parse_args();export(a.reference,a.output,a.start,a.count)

"""Check shadow projection/UV/color; captured mask textures are ORACLE ONLY.

The live renderer must bind its own mask target. This offline test deliberately
holds the pixel shader and mask texture fixed to isolate vertex semantics.
"""
import argparse,json
from pathlib import Path
import numpy as np
from reference_geometry import Geometry,pipeline_flags
from export_character_frame import character_textures
from native_scene import NativeDraw,write_scene


def convert(root,row):
    vs=row['stages']['ShaderStage.Vertex'].get('dump_name') or ''
    if '4B41B10E837C6871' not in vs:raise ValueError('unsupported projected shadow family')
    g=Geometry(root,row);c=g.constants();ps=g.constants('Pixel');g.n=np.zeros_like(g.n)
    if c.shape!=(13,4) or ps.shape!=(1,4) or g.gpu.shape[1]!=12:raise ValueError('unvalidated projected-shadow constants/outputs')
    world,normal,palette=g.transform(c[4:8],False);projection=g.projection(c);clip=world@projection;uv=world@c[9:13,:2];color=g.color*c[8]
    errors={'clip':float(abs(clip-g.gpu[:,:4]).max()),'uv':float(abs(uv-g.gpu[:,4:6]).max()),'color':float(abs(color-g.gpu[:,8:12]).max())}
    if any(errors[k]>v for k,v in {'clip':.002,'uv':.00002,'color':.000002}.items()):raise ValueError('projected shadow mismatch '+str(errors))
    vertices=np.column_stack([g.p,np.zeros_like(g.p),uv,g.color,np.ones(len(world))]).astype('<f4')
    material=np.zeros(20,dtype='<f4');material[3]=2;material[8:12]=c[8];material[12:16]=ps[0]
    # Local vertices use the combined model/projection; UVs were formed from
    # the source world position independently of the GPU post-VS output.
    transform=(c[4:8]@projection).T.copy();textures=tuple(character_textures(root,row,1));flags=pipeline_flags(row)
    proof={'event':row['event'],'vertices':len(world),'errors':errors}
    native=NativeDraw(vertices,g.indices,transform,material,flags,textures=textures,proof=proof)
    expected=vertices.copy();expected[:,:3]=g.gpu[:,:3];expected[:,3:6]=0;expected[:,12]=g.gpu[:,3];expected[:,6:8]=g.gpu[:,4:6];expected[:,8:12]=g.gpu[:,8:12]
    flat=material.copy();flat[8:12]=1
    reference=NativeDraw(expected,g.indices,np.eye(4,dtype='<f4'),flat,flags|0x400000,textures=textures)
    return native,reference


def export(reference,output,start=0,count=2):
    root=Path(reference);rows=json.loads((root/'frame.json').read_text())['draws'][start:start+count];converted=[convert(root,row) for row in rows];output=Path(output)
    write_scene(output,[v[0] for v in converted],version=4);write_scene(output.with_name(output.stem+'-reference.dbzgpu'),[v[1] for v in converted],version=4)
    proof={'scope':'Projected shadow vertex/UV/color oracle with a reference mask held fixed in both test paths. Not the live mask producer or a stencil/blur fidelity check.',
        'draws':len(rows),'vertices':sum(len(v[0].vertices) for v in converted),'validation':[v[0].proof for v in converted]}
    output.with_suffix('.json').write_text(json.dumps(proof,indent=2));print('Validated projected-shadow draws',len(rows))
    return converted


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--reference',required=True);p.add_argument('--output',required=True);p.add_argument('--start',type=int,default=0);p.add_argument('--count',type=int,default=2);a=p.parse_args();export(a.reference,a.output,a.start,a.count)

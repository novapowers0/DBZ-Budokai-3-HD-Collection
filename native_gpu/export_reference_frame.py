"""Verify and export the two validated world-position shader families.
Reference-only conversion: raw vertex/index buffers and draw constants become
host data. GPU post-VS output is used solely as an independent check.
"""
import argparse
import json
from pathlib import Path
import struct
import numpy as np
from PIL import Image

SUPPORTED={'FDF960B5D7869030':44,'432273B438E1AB50':24,'F37B3AC56DA38C91':24}
PS='1BFD0840B053E193'
COMPARISON_NATIVE={0:0,1:7,2:1,3:3,4:4,5:6,6:2,7:5}

def block(root,row,stage,name):
    cb=next(b for b in row['stages'][stage]['constants'] if b['name']==name)
    return (root/cb['file']).read_bytes()

def convert(root,row):
    vertex=row['stages']['ShaderStage.Vertex'];pixel=row['stages'].get('ShaderStage.Pixel',{})
    shader=(vertex.get('dump_name') or '').split('_')
    pixel_shader=(pixel.get('dump_name') or '').split('_')
    if len(shader)<2 or shader[1] not in SUPPORTED or len(pixel_shader)<2 or pixel_shader[1]!=PS:
        raise ValueError('unsupported material family')
    stride=SUPPORTED[shader[1]];prefix=f"e{row['event']}"
    fetch=next(f for f in row['vertex_fetches'] if f['slot']==95)
    if fetch['endian']!=2:raise ValueError('unvalidated vertex endianness')
    data=(root/fetch['file']).read_bytes();source=np.frombuffer(data,dtype=np.uint8).reshape(-1,stride)
    system=block(root,row,'ShaderStage.Vertex','xe_system_cbuffer')
    base_index=struct.unpack_from('<i',system,20)[0]
    post=row['postvs'];postdata=np.frombuffer((root/f'{prefix}-postvs.bin').read_bytes(),dtype='<f4').reshape(-1,post['vertexByteStride']//4)
    if post['indexResourceId']!='ResourceId::0':
        if not (root/f'{prefix}-original-indices.bin').exists():raise ValueError('original index buffer was not exported')
        width=row['original_index']['byteStride']
        # The current reference's u16 guest index buffers are big endian.
        if width!=2:raise ValueError('unvalidated index format')
        count=row['count'];original=np.frombuffer((root/f'{prefix}-original-indices.bin').read_bytes(),dtype='>u2',count=count).astype(np.int64)+base_index
        post_indices=np.frombuffer((root/f'{prefix}-indices.bin').read_bytes(),dtype='<u'+str(post['indexByteStride']),count=count)
        look={}
        for output,input in zip(post_indices,original):
            if int(output) in look and look[int(output)]!=int(input):raise ValueError('inconsistent post-VS index mapping')
            look[int(output)]=int(input)
        used=np.array([look[i] for i in range(len(postdata))]);indices=post_indices.astype('<u4')
    else:
        used=np.arange(row['vertex_offset'],row['vertex_offset']+row['count'])+base_index
        indices=np.arange(len(used),dtype='<u4')
    if used.min()<0 or used.max()>=len(source):raise ValueError('source vertex range invalid')
    floats=np.frombuffer(data,dtype='>f4').reshape(-1,stride//4)
    positions=floats[used,:3].astype('<f4');uv=floats[used,-2:].astype('<f4')
    color_offset=32 if stride==44 else 12
    packed=source[used,color_offset:color_offset+4].astype(np.float32)/255
    colors=packed[:,[1,2,3,0]]
    constants=np.frombuffer(block(root,row,'ShaderStage.Vertex','xe_float_cbuffer'),dtype='<f4').reshape(-1,4)
    screen=shader[1]=='F37B3AC56DA38C91'
    if screen:
        c=constants[:4].astype(np.float64)
        if abs(c[2,0])+abs(c[2,1])+abs(c[0,1])+abs(c[1,0])>1e-7:
            raise ValueError('unvalidated screen transform terms')
        z=np.clip(positions[:,2],c[0,2],c[0,3])
        positions[:,2]=(z*c[2,2]+c[3,2])/(z*c[2,3]+c[3,3])
        transform=np.eye(4,dtype=np.float64);transform[0,0]=c[0,0];transform[1,1]=c[1,1];transform[3,:2]=c[3,:2]
        uv_offset=constants[5,:2];tint=np.ones(4,dtype=np.float32);fog_params=constants[4]
    else:
        transform=constants[4:8].astype(np.float64)@constants[:4].astype(np.float64)
        uv_offset=constants[9,:2]
        tint=constants[10] if stride==44 else np.ones(4,dtype=np.float32)
        fog_params=constants[8]
    # Apply the captured viewport mapping. It differs between the shadow,
    # world and screen-space passes and must not be assumed to be constant.
    ndc_scale=struct.unpack_from('<3f',system,128)
    ndc_offset=struct.unpack_from('<3f',system,144)
    for axis in range(3):
        transform[:,axis]=transform[:,axis]*ndc_scale[axis]+transform[:,3]*ndc_offset[axis]
    projected=np.column_stack([positions,np.ones(len(positions))])@transform
    clip_error=float(abs(projected-postdata[:,:4]).max())
    if clip_error>0.002:raise ValueError(f'projection mismatch {clip_error:g}')
    color_error=float(abs(colors*tint-postdata[:,8:12]).max())
    uv_error=float(abs(uv+uv_offset-postdata[:,4:6]).max())
    if color_error>2e-6 or uv_error>2e-5:raise ValueError(f'varying mismatch color={color_error:g} uv={uv_error:g}')
    # Fog is reconstructed from the original draw constants and clip depth.
    fog=np.clip(np.clip((fog_params[0]-projected[:,2])*fog_params[1],0,1),1-fog_params[3],1-fog_params[2]).astype('<f4')
    fog_error=float(abs(fog-postdata[:,12]).max())
    if fog_error>2e-5:raise ValueError(f'fog mismatch {fog_error:g}')
    vertex_data=np.column_stack([positions,np.zeros_like(positions),uv,colors,fog]).astype('<f4')
    if row['topology']=='Topology.TriangleStrip':
        indices=np.array([v for i in range(len(indices)-2) for v in ((indices[i+1],indices[i],indices[i+2]) if i&1 else (indices[i],indices[i+1],indices[i+2]))],dtype='<u4')
    elif row['topology']!='Topology.TriangleList':raise ValueError('unsupported topology')
    ps=np.frombuffer(block(root,row,'ShaderStage.Pixel','xe_float_cbuffer'),dtype='<f4').reshape(-1,4)
    descriptor=next(d['descriptor'] for d in row['descriptors'] if d['access']['stage']==4 and d['access']['type']==4)
    resource=descriptor['resource'];texture=root/('texture-'+''.join(c for c in resource if c.isdigit())+'.png')
    blend=row['blend'];mode=0
    if blend['enabled']:
        equation=blend['colorBlend'];key=(equation['source'],equation['destination'],equation['operation'])
        modes={(6,7,0):1,(1,1,0):2,(4,0,0):3}
        if key not in modes:raise ValueError(f'unsupported blend {key}')
        mode=modes[key]
    depth=row['depth'];raster=row['raster']
    if not raster['frontCCW'] and raster['cullMode']:raise ValueError('unvalidated front-face convention')
    flags=int(depth['depthEnable'])|(int(depth['depthWrites'])<<1)|(raster['cullMode']<<2)|(mode<<4)|(COMPARISON_NATIVE[depth['depthFunction']]<<8)|(blend['writeMask']<<16)
    if blend['enabled'] and blend['alphaBlend']=={'destination':1,'operation':0,'source':1}:flags|=0x800
    material=np.concatenate([[*uv_offset,float(ps[1,3]!=0),1],fog_params,tint,ps[0],ps[1]]).astype('<f4')
    return vertex_data,indices,transform.T.astype('<f4'),material,flags,texture,{'event':row['event'],'vertices':len(used),'triangles':len(indices)//3,'clip_error':clip_error,'color_error':color_error,'uv_error':uv_error,'fog_error':fog_error,'source_vertex_address':fetch['address']}


def export(reference,additional,output):
    root=Path(reference);rows=json.loads((root/'frame.json').read_text())['draws'];additional=Path(additional) if additional else None
    replacements={r['event']:r for r in json.loads((additional/'frame.json').read_text())['draws']} if additional else {}
    vertices=[];indices=[];draws=[];states=[];textures=[];texture_map={};passed=[];rejected=[];vcount=icount=0
    for row in rows:
        source=root
        if row['event'] in replacements:row=replacements[row['event']];source=additional
        try:
            v,i,matrix,material,flags,texture,proof=convert(source,row)
        except (ValueError,StopIteration,FileNotFoundError,KeyError) as error:
            rejected.append({'event':row['event'],'reason':str(error)});continue
        key=str(texture.resolve())
        if key not in texture_map:
            rgba=Image.open(texture).convert('RGBA');texture_map[key]=len(textures);textures.append((rgba.width,rgba.height,rgba.tobytes()))
        draws.append(struct.pack('<5I3f',icount,len(i),texture_map[key],0xffffffff,0,1,1,1))
        states.append(matrix.tobytes()+material.tobytes()+struct.pack('<I',flags))
        vertices.append(v);indices.append(i+vcount);vcount+=len(v);icount+=len(i);passed.append(proof)
    if not vertices:raise ValueError('No fully validated native draws')
    v=np.concatenate(vertices);idx=np.concatenate(indices).astype('<u4');bounds=np.concatenate([v[:,:3].min(0),v[:,:3].max(0)]).astype('<f4')
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('wb') as f:
        f.write(b'DBZNGPU2');f.write(struct.pack('<4I',len(v),len(idx),len(draws),len(textures)));f.write(bounds.tobytes());f.write(struct.pack('<2I',1280,720));f.write(v.tobytes());f.write(idx.tobytes());f.write(b''.join(draws));f.write(b''.join(states))
        for w,h,data in textures:f.write(struct.pack('<3I',w,h,len(data)));f.write(data)
    report={'scope':'Partial reference frame; unsupported draws are recorded and omitted. This is not a complete game frame or a live replacement.',
            'validation_scope':'CPU projection, UVs, vertex colors and fog agree with the reference vertex outputs. Final pixel colors, stencil and cross-target ordering are not validated.',
            'is_complete_frame':False,'validated_draws':len(passed),'unsupported_draws':len(rejected),'validation':passed,'unsupported':rejected}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:report[k] for k in ('validated_draws','unsupported_draws')},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--reference',required=True);p.add_argument('--additional');p.add_argument('--output',required=True);a=p.parse_args();export(a.reference,a.additional,a.output)

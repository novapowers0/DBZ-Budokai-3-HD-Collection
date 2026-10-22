"""Reference oracle for the skinned F3AC / 5F27 character family.

The native scene retains original local vertices and compact joint palettes.
Post-VS data is only used to verify the independent math and to export a
separate predeformed comparison scene, never as input to native skinning.
"""
import argparse, hashlib, json, struct
from pathlib import Path
import numpy as np
from PIL import Image
from export_reference_frame import block,COMPARISON_NATIVE


def quaternion(xyz):
    xyz=np.asarray(xyz,dtype=np.float32)
    return np.column_stack([xyz,np.sqrt(abs(1-(xyz*xyz).sum(1)))])


def rotate(q,v):
    t=2*np.cross(q[:,:3],v)/(q*q).sum(1)[:,None]
    return v+q[:,3,None]*t+np.cross(q[:,:3],t)


def character_textures(root,row,count=2):
    descriptors=[v for v in row['descriptors'] if v['access']['stage']==4 and v['access']['type']==4]
    cb=next((b for b in row['stages']['ShaderStage.Pixel']['constants'] if b['name']=='xe_descriptor_indices_cbuffer'),None)
    if cb:
        # This checked PS packs sampler0/texture0, sampler1/texture1 at 1/2,4/5.
        # Descriptor instrumentation may omit offscreen reads. Recover the same
        # bindless allocation from other draws in this capture, with conflicts
        # rejected rather than guessing its identity.
        words=np.frombuffer((root/cb['file']).read_bytes(),dtype='<u4')
        if len(words)!=4*((1+count*3+3)//4):raise ValueError('unvalidated character descriptor indices')
        desired=[int(words[2+3*i]) for i in range(count)];ordered=[]
        for index in desired:
            candidates=[v for v in descriptors if v['access']['arrayElement']==index]
            if not candidates:
                rows=json.loads((root/'frame.json').read_text())['draws']
                candidates=[v for other in rows for v in other['descriptors'] if v['access']['stage']==4 and v['access']['type']==4 and v['access']['arrayElement']==index]
            resources={v['descriptor']['resource'] for v in candidates}
            if len(resources)!=1:raise ValueError('missing or reused character bindless descriptor')
            ordered.append(next(iter(resources)))
    else:
        if len(descriptors)!=count:raise ValueError('character texture count differs')
        ordered=[v['descriptor']['resource'] for v in sorted(descriptors,key=lambda d:d['access']['index'])]
    return [root/row.get('texture_files',{}).get(rid,'texture-'+''.join(c for c in rid if c.isdigit())+'.png') for rid in ordered]


def convert(root,row):
    vs=row['stages']['ShaderStage.Vertex'];ps=row['stages']['ShaderStage.Pixel']
    rigid='E9DD8CD3AADD7CF1' in (vs.get('dump_name') or '')
    if (not rigid and 'F3AC1AA2FE3EA253' not in (vs.get('dump_name') or '')) or '5F27AACEB38B1088' not in (ps.get('dump_name') or ''):
        raise ValueError('unsupported character family')
    prefix='e'+str(row['event']);post=row['postvs']
    fetch={v['slot']:v for v in row['vertex_fetches']}
    if any(fetch[s]['endian']!=2 for s in ((95,) if rigid else (94,95))):raise ValueError('unvalidated fetch endianness')
    raw=(root/fetch[95]['file']).read_bytes();source=np.frombuffer(raw,dtype='>f4').reshape(-1,11)
    packed=np.frombuffer(raw,dtype=np.uint8).reshape(-1,44)
    gpu=np.frombuffer((root/(prefix+'-postvs.bin')).read_bytes(),dtype='<f4').reshape(-1,post['vertexByteStride']//4)
    if gpu.shape[1]!=20 or row['original_index']['byteStride']!=2:raise ValueError('unvalidated reference layout')
    system=block(root,row,'ShaderStage.Vertex','xe_system_cbuffer');base=struct.unpack_from('<i',system,20)[0]
    original=np.frombuffer((root/(prefix+'-original-indices.bin')).read_bytes(),dtype='>u2',count=row['count']).astype(int)+base
    remap=np.frombuffer((root/(prefix+'-indices.bin')).read_bytes(),dtype='<u'+str(post['indexByteStride']),count=row['count'])
    mapping={}
    for out,src in zip(remap,original):
        if int(out) in mapping and mapping[int(out)]!=int(src):raise ValueError('inconsistent vertex mapping')
        mapping[int(out)]=int(src)
    used=np.array([mapping[i] for i in range(len(gpu))])
    if used.min()<0 or used.max()>=len(source):raise ValueError('invalid vertex range')
    bone=np.zeros(len(used),dtype=np.uint8) if rigid else packed[used,19]
    weight=np.zeros(len(used),dtype='<f4') if rigid else source[used,3].astype('<f4')
    if bone.max()>127 or (weight<0).any() or (weight>1).any():raise ValueError('invalid bone or weight')
    # Do not read uninitialized, unused palette entries as floats.
    palette=np.zeros((128,3,4),dtype='<f4')
    palette_bytes=bytes(6144) if rigid else (root/fetch[94]['file']).read_bytes()
    if len(palette_bytes)!=6144:raise ValueError('invalid palette size')
    for joint in np.unique(bone):palette[joint]=np.frombuffer(palette_bytes,dtype='>f4',count=12,offset=int(joint)*48).reshape(3,4)
    if not np.isfinite(palette).all():raise ValueError('nonfinite used joint')
    selected=palette[bone];q0=quaternion(selected[:,:,3]);qa=quaternion(selected[:,1,:3]);qb=quaternion(selected[:,2,:3])
    qb*=np.where((qa*qb).sum(1)<0,-1,1)[:,None]
    q=(qa*(1-weight[:,None])+qb*weight[:,None]).astype('<f4')
    constants=np.frombuffer(block(root,row,'ShaderStage.Vertex','xe_float_cbuffer'),dtype='<f4').reshape(-1,4)
    pixels=np.frombuffer(block(root,row,'ShaderStage.Pixel','xe_float_cbuffer'),dtype='<f4').reshape(-1,4)
    if len(constants)!=12 or len(pixels)!=5 or not np.array_equal(constants[11,:2],[1,0]) or (not rigid and constants[11,2]!=-1):
        raise ValueError('unvalidated packed constants')
    local=source[used,:3].astype('<f4');normals=source[used,5:8].astype('<f4');uv=source[used,9:11].astype('<f4')
    model=constants[4:8];p=local if rigid else rotate(q0,rotate(q,local))+selected[:,0,:3]
    world=np.column_stack([p,np.ones(len(p),dtype=np.float32)])@model
    normal=(normals if rigid else rotate(q0,rotate(q,normals)))@model[:3,:3]
    projection=constants[:4].copy();scale=struct.unpack_from('<3f',system,128);offset=struct.unpack_from('<3f',system,144)
    for axis in range(3):projection[:,axis]=projection[:,axis]*scale[axis]+projection[:,3]*offset[axis]
    clip=world@projection
    fog=np.clip(np.clip((constants[8,0]-clip[:,2])*constants[8,1],0,1),1-constants[8,3],1-constants[8,2])
    errors={'world':float(abs(world-gpu[:,8:12]).max()),'normal':float(abs(normal-gpu[:,12:15]).max()),
            'clip':float(abs(clip-gpu[:,:4]).max()),'uv':float(abs(uv+constants[9,:2]-gpu[:,4:6]).max()),
            'rim':float(abs(np.any(normals!=0,axis=1)*constants[10,0]-gpu[:,15]).max()),'fog':float(abs(fog-gpu[:,16]).max())}
    if any(errors[k]>v for k,v in {'world':.0002,'normal':.0001,'clip':.002,'uv':.00002,'rim':.000002,'fog':.00002}.items()):
        raise ValueError('character reference mismatch: '+str(errors))
    vertices=np.column_stack([local,normals,uv,bone,weight,np.zeros((len(local),2)),np.ones(len(local))]).astype('<f4')
    reference=vertices.copy();reference[:,:3]=gpu[:,8:11];reference[:,3:6]=gpu[:,12:15]
    indices=remap.astype('<u4')
    if row['topology']=='Topology.TriangleStrip':
        indices=np.array([v for i in range(len(indices)-2) for v in ((indices[i+1],indices[i],indices[i+2]) if i&1 else (indices[i],indices[i+1],indices[i+2]))],dtype='<u4')
    elif row['topology']!='Topology.TriangleList':raise ValueError('unsupported character topology')
    material=np.concatenate([[*constants[9,:2],float(pixels[1,3]!=0),6 if rigid else 4],constants[8],
        [*pixels[2,:3],constants[10,0]],[*pixels[3,:3],pixels[0,0]],pixels[1]]).astype('<f4')
    if pixels[3,3]!=1:raise ValueError('unvalidated camera homogeneous coordinate')
    textures=character_textures(root,row)
    depth=row['depth'];raster=row['raster'];blend=row['blend']
    if not raster['frontCCW'] and raster['cullMode']:raise ValueError('unvalidated character winding')
    mode=0
    if blend['enabled']:
        if blend['colorBlend']!={'destination':7,'operation':0,'source':6} or blend['alphaBlend']!={'destination':1,'operation':0,'source':1}:
            raise ValueError('unvalidated character blending')
        mode=0x810
    flags=int(depth['depthEnable'])|(int(depth['depthWrites'])<<1)|(raster['cullMode']<<2)|(COMPARISON_NATIVE[depth['depthFunction']]<<8)|(blend['writeMask']<<16)|mode
    proof={'event':row['event'],'vertices':len(local),'triangles':len(indices)//3,'errors':errors,'kind':'rigid' if rigid else 'skin',
        'local_geometry_sha256':hashlib.sha256(vertices.tobytes()+indices.tobytes()).hexdigest(),'bones':[] if rigid else np.unique(bone).tolist()}
    return vertices,reference,indices,projection.T.copy(),material,flags,model.copy() if rigid else np.concatenate([model,palette.reshape(-1,4)]),textures,pixels[4,:3],proof


def export(reference,output,start=0,count=13):
    root=Path(reference);rows=json.loads((root/'frame.json').read_text())['draws'][start:start+count]
    converted=[convert(root,row) for row in rows]
    vertices=[];expected=[];indices=[];draws=[];states=[];palettes=[];textures=[];lookup={};proof=[];vcount=icount=0
    for v,ref,idx,matrix,material,flags,palette,images,diffuse,checked in converted:
        ti=[]
        for path in images:
            key=str(path.resolve())
            if key not in lookup:
                image=Image.open(path).convert('RGBA');lookup[key]=len(textures);textures.append((image.width,image.height,image.tobytes()))
            ti.append(lookup[key])
        vertices.append(v);expected.append(ref);indices.append(idx+vcount)
        draws.append(struct.pack('<5I3f',icount,len(idx),*ti,0,*diffuse));states.append((matrix,material,flags));palettes.append(palette);proof.append(checked)
        vcount+=len(v);icount+=len(idx)
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    def write(path,cpu):
        v=np.concatenate(expected if cpu else vertices);idx=np.concatenate(indices).astype('<u4')
        bounds=np.concatenate([v[:,:3].min(0),v[:,:3].max(0)]).astype('<f4')
        with path.open('wb') as f:
            f.write(b'DBZNGPU3'+struct.pack('<4I',len(v),len(idx),len(draws),len(textures))+bounds.tobytes()+struct.pack('<2I',1280,720))
            f.write(v.tobytes());f.write(idx.tobytes());f.write(b''.join(draws))
            for matrix,material,flags in states:
                m=material.copy()
                if cpu:m[3]=5
                f.write(matrix.tobytes()+m.tobytes()+struct.pack('<I',flags))
            for palette in palettes:
                f.write(struct.pack('<I',0 if cpu else len(palette)))
                if not cpu:f.write(palette.astype('<f4').tobytes())
            for w,h,data in textures:f.write(struct.pack('<3I',w,h,len(data))+data)
    write(output,False);write(output.with_name(output.stem+'-reference.dbzgpu'),True)
    report={'scope':'Partial skinned character bodies from one local reference capture; not a complete game frame or live integration.',
        'validation_scope':'Independent quaternion/model/projection/normal/UV/rim/fog math checked against GPU vertex output. Pixel material fidelity is not established.',
        'draws':len(proof),'vertices':vcount,'validation':proof}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2));print(json.dumps({'draws':len(proof),'vertices':vcount,'max_errors':{k:max(p['errors'][k] for p in proof) for k in proof[0]['errors']}},indent=2))
    return converted


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--reference',required=True);p.add_argument('--output',required=True);p.add_argument('--start',type=int,default=0);p.add_argument('--count',type=int,default=13);a=p.parse_args();export(a.reference,a.output,a.start,a.count)

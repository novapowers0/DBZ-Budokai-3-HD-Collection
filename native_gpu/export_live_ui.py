"""Export native loading-screen geometry directly from compiled guest hooks.
No RenderDoc output or Xbox shaders are required by this converter or renderer.
"""
import argparse,json,struct
from pathlib import Path
import numpy as np
from texture_decode import decode

def fnv(data):
    value=14695981039346656037
    for b in data:value=((value^b)*1099511628211)&0xffffffffffffffff
    return value

def pipeline(device,explicit_alpha=False):
    depth,blend,raster,mask=[struct.unpack_from('>I',device,o)[0] for o in (10548,10552,10568,10460)]
    if depth&1:raise ValueError('stencil is not covered by the loading-screen bridge')
    key=(blend&31,blend>>8&31,blend>>5&7)
    modes={(1,0,0):0,(6,7,0):1,(1,1,0):2,(8,0,0):3,(6,1,0):4,(6,1,4):5,(1,6,1):6}
    if key not in modes:raise ValueError(f'unsupported loading blend {key}')
    cull=raster&3
    if cull==3:raise ValueError('both faces culled')
    if raster&4:cull=((cull&1)<<1)|((cull&2)>>1)
    flags=(int(bool(depth&2)))|(int(bool(depth&4))<<1)|(cull<<2)|(modes[key]<<4)|((depth>>4&7)<<8)|((mask&15)<<16)
    alpha=(blend>>16&31,blend>>24&31,blend>>21&7)
    if alpha==(1,1,0):flags|=0x800
    elif alpha not in ((1,7,0),(1,0,0)):raise ValueError(f'unsupported loading alpha blend {alpha}')
    if explicit_alpha:
        if alpha==(1,0,0) and modes[key] in (1,2,4,5):flags|=0x1000
        if alpha==(1,7,0) and modes[key]!=1:flags|=0x2000
    return flags

def geometry(data,device):
    source=np.frombuffer(data,dtype='>f4').reshape(-1,6);words=np.frombuffer(data,dtype=np.uint8).reshape(-1,24)
    constants=np.frombuffer(device[1920:1920+4096],dtype='>f4').reshape(-1,4).astype(np.float64)
    c=constants[:4]
    if not (np.isfinite(c[0,2:4]).all() and c[0,2]<=c[0,3] and c[0,0]>0 and c[1,1]<0):raise ValueError('unvalidated screen projection')
    if abs(c[2,0])+abs(c[2,1])+abs(c[0,1])+abs(c[1,0])>1e-7:raise ValueError('unvalidated screen cross terms')
    positions=source[:,:3].astype('<f4');z=np.clip(positions[:,2],c[0,2],c[0,3]);positions[:,2]=(z*c[2,2]+c[3,2])/(z*c[2,3]+c[3,3])
    matrix=np.eye(4,dtype='<f4');matrix[0,0]=c[0,0];matrix[1,1]=c[1,1];matrix[0,3]=c[3,0];matrix[1,3]=c[3,1]
    colors=words[:,12:16][:,[1,2,3,0]].astype(np.float32)/255
    fog=np.clip(np.clip((constants[12,0]-positions[:,2])*constants[12,1],0,1),1-constants[12,3],1-constants[12,2])
    vertices=np.column_stack([positions,np.zeros_like(positions),source[:,4:6],colors,fog]).astype('<f4')
    ps=np.frombuffer(device[6016:6016+4096],dtype='>f4').reshape(-1,4)
    material=np.concatenate([[*constants[13,:2],float(ps[3,3]!=0),1],constants[12],np.ones(4),ps[2],ps[3]]).astype('<f4')
    if not np.isfinite(vertices).all() or not np.isfinite(material).all():raise ValueError('nonfinite loading state')
    return vertices,matrix,material

def export(prefixes,output):
    vertices=[];draws=[];states=[];textures=[];entries=[];proof=[];total=0
    for i,name in enumerate(prefixes):
        prefix=Path(name);meta=json.loads(prefix.with_suffix('.json').read_text())
        if meta['stride']!=24 or meta['primitive']!=4 or meta['count']%3:raise ValueError('unvalidated loading topology')
        texture_path=Path(str(prefix)+'-texture.bin')
        if not texture_path.exists():texture_path=Path(str(prefix)+'-texture0.bin')
        raw=texture_path.read_bytes();tex,desc=decode(raw,meta['texture_fetch'])
        device=Path(str(prefix)+'-device.bin').read_bytes();v,m,material=geometry(Path(str(prefix)+'-vertices.bin').read_bytes(),device);flags=pipeline(device)
        vertices.append(v);draws.append(struct.pack('<5I3f',total,len(v),i,0xffffffff,0,1,1,1));states.append(m.tobytes()+material.tobytes()+struct.pack('<I',flags))
        textures.append((tex.width,tex.height,tex.tobytes()));entries.append(struct.pack('<Q7I',fnv(raw),len(raw),tex.width,tex.height,desc['format'],total,len(v),flags));total+=len(v)
        proof.append({'source':prefix.name,'frame':meta['frame'],'texture_raw_fnv1a64':f'{fnv(raw):016X}','texture_dimensions':tex.size,'vertices':len(v),'pipeline_flags':flags})
    v=np.concatenate(vertices);bounds=np.concatenate([v[:,:3].min(0),v[:,:3].max(0)]).astype('<f4');output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('wb') as f:
        f.write(b'DBZNGPU2'+struct.pack('<4I',len(v),len(v),len(draws),len(textures))+bounds.tobytes()+struct.pack('<2I',1280,720));f.write(v.tobytes());f.write(np.arange(len(v),dtype='<u4').tobytes());f.write(b''.join(draws));f.write(b''.join(states))
        for w,h,data in textures:f.write(struct.pack('<3I',w,h,len(data))+data)
    output.with_suffix('.bridge').write_bytes(b'DBZUI001'+struct.pack('<I',len(entries))+b''.join(entries))
    output.with_suffix('.json').write_text(json.dumps({'scope':'Native loading-screen sprites, captured from CPU draw calls. Other game passes remain on the reference renderer.','draws':proof},indent=2));print('Exported loading UI',len(draws),'draws',len(v),'vertices')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--prefix',action='append',required=True);p.add_argument('--output',required=True);a=p.parse_args();export(a.prefix,a.output)

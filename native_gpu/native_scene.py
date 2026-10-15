"""Shared, explicit host-scene representation for geometry oracle tests."""
from dataclasses import dataclass,field
from pathlib import Path
import hashlib,struct
import numpy as np
from PIL import Image


@dataclass
class NativeDraw:
    vertices: np.ndarray
    indices: np.ndarray
    matrix: np.ndarray
    material: np.ndarray
    flags: int
    palette: np.ndarray=field(default_factory=lambda:np.empty((0,4),dtype='<f4'))
    textures: tuple=()
    diffuse: tuple=(1,1,1)
    proof: dict=field(default_factory=dict)
    stencil: bytes=bytes([0,0,255,255,7,0,0,0,7,0,0,0])
    alpha: tuple=(0,7,0.)
    viewport: tuple|None=None
    scissor: tuple|None=None


def write_scene(path,draws,width=1280,height=720,version=3,clear=None):
    if version not in (3,4):raise ValueError('unsupported host scene version')
    vertices=[];indices=[];records=[];textures=[];lookup={};vcount=icount=0
    for draw in draws:
        ids=[]
        for source in draw.textures:
            image=source if isinstance(source,Image.Image) else Image.open(source)
            image=image.convert('RGBA');data=image.tobytes();key=(image.size,hashlib.sha256(data).digest())
            if key not in lookup:lookup[key]=len(textures);textures.append((image.width,image.height,data))
            ids.append(lookup[key])
        ids=(ids+[0xffffffff,0xffffffff])[:2]
        v=np.asarray(draw.vertices,dtype='<f4');idx=np.asarray(draw.indices,dtype='<u4')
        if v.ndim!=2 or v.shape[1]!=13 or len(idx)%3:raise ValueError('invalid native draw geometry')
        vertices.append(v);indices.append(idx+vcount)
        records.append(struct.pack('<5I3f',icount,len(idx),*ids,0,*draw.diffuse));vcount+=len(v);icount+=len(idx)
    v=np.concatenate(vertices);idx=np.concatenate(indices).astype('<u4');bounds=np.concatenate([v[:,:3].min(0),v[:,:3].max(0)]).astype('<f4')
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('wb') as f:
        f.write((b'DBZNGPU4' if version==4 else b'DBZNGPU3')+struct.pack('<4I',len(v),len(idx),len(draws),len(textures))+bounds.tobytes()+struct.pack('<2I',width,height))
        if version==4:f.write(struct.pack('<5f2I',*(clear or (.025,.038,.07,1,1,0,7))))
        f.write(v.tobytes());f.write(idx.tobytes());f.write(b''.join(records))
        for draw in draws:
            f.write(np.asarray(draw.matrix,dtype='<f4').tobytes()+np.asarray(draw.material,dtype='<f4').tobytes()+struct.pack('<I',draw.flags))
            if version==4:
                if len(draw.stencil)!=12:raise ValueError('invalid stencil encoding')
                f.write(draw.stencil+struct.pack('<BBHf',draw.alpha[0],draw.alpha[1],0,draw.alpha[2])+struct.pack('<6f',*(draw.viewport or (0,0,width,height,0,1)))+struct.pack('<4i',*(draw.scissor or (0,0,width,height))))
        for draw in draws:f.write(struct.pack('<I',len(draw.palette))+np.asarray(draw.palette,dtype='<f4').tobytes())
        for w,h,data in textures:f.write(struct.pack('<3I',w,h,len(data))+data)

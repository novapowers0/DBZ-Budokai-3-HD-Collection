"""Bounded decoding of captured vertex data for independent native GPU oracles."""
import struct
import numpy as np
from export_reference_frame import block,COMPARISON_NATIVE
from export_character_frame import quaternion,rotate
STENCIL_OP_NATIVE={0:0,1:1,2:2,3:3,4:4,5:6,6:7,7:5}


class Geometry:
    def __init__(self,root,row):
        self.root=root;self.row=row;prefix='e'+str(row['event']);post=row['postvs']
        self.fetch={v['slot']:v for v in row['vertex_fetches']}
        if self.fetch[95]['endian']!=2 or row['original_index']['byteStride']!=2:raise ValueError('unvalidated geometry endianness/indices')
        raw=(root/self.fetch[95]['file']).read_bytes()
        source=np.frombuffer(raw,dtype='>f4').reshape(-1,11);packed=np.frombuffer(raw,dtype=np.uint8).reshape(-1,44)
        self.gpu=np.frombuffer((root/(prefix+'-postvs.bin')).read_bytes(),dtype='<f4').reshape(-1,post['vertexByteStride']//4)
        self.system=block(root,row,'ShaderStage.Vertex','xe_system_cbuffer');base=struct.unpack_from('<i',self.system,20)[0]
        original=np.frombuffer((root/(prefix+'-original-indices.bin')).read_bytes(),dtype='>u2',count=row['count']).astype(int)+base
        remap=np.frombuffer((root/(prefix+'-indices.bin')).read_bytes(),dtype='<u'+str(post['indexByteStride']),count=row['count'])
        mapping={}
        for out,src in zip(remap,original):
            if int(out) in mapping and mapping[int(out)]!=int(src):raise ValueError('inconsistent reference mapping')
            mapping[int(out)]=int(src)
        used=np.array([mapping[i] for i in range(len(self.gpu))])
        if used.min()<0 or used.max()>=len(source):raise ValueError('invalid source vertex range')
        self.p=source[used,:3].astype('<f4');self.n=source[used,5:8].astype('<f4');self.uv=source[used,9:11].astype('<f4')
        self.bone=packed[used,19];self.weight=source[used,3].astype('<f4')
        self.color=packed[used,32:36][:,[1,2,3,0]].astype('<f4')/255
        self.indices=remap.astype('<u4')
        if row['topology']=='Topology.TriangleStrip':
            self.indices=np.array([v for i in range(len(remap)-2) for v in ((remap[i+1],remap[i],remap[i+2]) if i&1 else (remap[i],remap[i+1],remap[i+2]))],dtype='<u4')
        elif row['topology']!='Topology.TriangleList':raise ValueError('unsupported reference topology')

    def constants(self,stage='Vertex'):
        return np.frombuffer(block(self.root,self.row,'ShaderStage.'+stage,'xe_float_cbuffer'),dtype='<f4').reshape(-1,4)

    def transform(self,model,skinned):
        p,n=self.p,self.n;palette=np.empty((0,4),dtype='<f4')
        if skinned:
            if self.bone.max()>127 or (self.weight<0).any() or (self.weight>1).any():raise ValueError('invalid bone/weight')
            f=self.fetch[94];raw=(self.root/f['file']).read_bytes()
            if f['endian']!=2 or len(raw)!=6144:raise ValueError('invalid palette')
            palette=np.zeros((128,3,4),dtype='<f4')
            for joint in np.unique(self.bone):palette[joint]=np.frombuffer(raw,dtype='>f4',count=12,offset=int(joint)*48).reshape(3,4)
            selected=palette[self.bone];q0=quaternion(selected[:,:,3]);qa=quaternion(selected[:,1,:3]);qb=quaternion(selected[:,2,:3])
            qb*=np.where((qa*qb).sum(1)<0,-1,1)[:,None]
            q=(qa*(1-self.weight[:,None])+qb*self.weight[:,None]).astype('<f4')
            p=rotate(q0,rotate(q,p))+selected[:,0,:3];n=rotate(q0,rotate(q,n))
        world=np.column_stack([p,np.ones(len(p),dtype=np.float32)])@model;normal=n@model[:3,:3]
        rows=np.concatenate([model,palette.reshape(-1,4)]) if skinned else model.copy()
        if not all(np.isfinite(a).all() for a in (world,normal,rows)):raise ValueError('nonfinite transform')
        return world,normal,rows

    def projection(self,constants):
        projection=constants[:4].copy();scale=struct.unpack_from('<3f',self.system,128);offset=struct.unpack_from('<3f',self.system,144)
        for axis in range(3):projection[:,axis]=projection[:,axis]*scale[axis]+projection[:,3]*offset[axis]
        return projection


def pipeline_flags(row):
    depth,raster,blend=row['depth'],row['raster'],row['blend'];mode=0
    cull=raster['cullMode']
    if not raster['frontCCW'] and cull:cull=3-cull
    if blend['enabled']:
        b=blend['colorBlend'];key=(b['source'],b['destination'],b['operation'])
        choices={(6,7,0):1,(1,1,0):2,(4,0,0):3,(6,1,0):4}
        if key not in choices:raise ValueError('unsupported reference blend')
        mode=choices[key]
    flags=int(depth['depthEnable'])|(int(depth['depthWrites'])<<1)|(cull<<2)|(mode<<4)|(COMPARISON_NATIVE[depth['depthFunction']]<<8)|(blend['writeMask']<<16)
    if blend['enabled']:
        a=blend['alphaBlend']
        if a=={'source':1,'destination':1,'operation':0}:flags|=0x800
        elif a not in ({'source':1,'destination':0,'operation':0},{'source':1,'destination':7,'operation':0}):raise ValueError('unsupported reference alpha blend')
        if a['destination']==0 and mode in (1,2,4):flags|=0x1000
        if a['destination']==7 and mode!=1:flags|=0x2000
    return flags

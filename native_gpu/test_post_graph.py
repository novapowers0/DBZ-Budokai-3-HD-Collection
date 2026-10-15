"""Independent typed postprocess graph from captured CPU call metadata.

Original resolved color/depth memory is deliberately never mapped. Small-target
color is seeded from the native video scene, replacing only the intervening 3D
scene draws; their fidelity is tested separately. Real depth resample, packed
depth aliases, restores, Gaussian parameters and composition are retained.
"""
import ctypes as C,json,struct
from pathlib import Path
import numpy as np
from live_raster import encode,pipeline
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear,Raster
from test_frame_operations import Operation


class PostGraph:
    def __init__(self,trace,native,reference,api,snapshots,check,half_trace=None):
        self.trace=Path(trace).resolve();self.frame=7945;self.native=native;self.reference=reference;self.api=api;self.snapshots=snapshots;self.check=check;self.nodes=[];self.targets={0:0}
        native.dbz_native_add_render_target.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(Clear),C.POINTER(C.c_uint32),C.POINTER(C.c_uint32)]
        for width in (320,160,80):
            target=C.c_uint32();texture=C.c_uint32();clear=Clear((C.c_float*4)(0,0,0,0),1,1,7)
            check(native.dbz_native_add_render_target(reference,width,width*9//16,C.byref(clear),C.byref(target),C.byref(texture)));self.targets[width]=target.value
        # Declared intent is independent of the C++ frontend's target inference.
        self.resolve(47786,2,0,1280,720);self.resolve(47806,1,0,1280,720)
        self.draw(47856,17,0);self.resolve(47877,1,0,1280,720)
        self.draw(47934,18,320);self.resolve(47969,2,320,320,180)
        self.restore(48031,320,0x19311000);self.noop(48072)
        self.resolve(48728,2,320,320,180)
        # Original intervening 3D content is replaced by a native color copy.
        self.draw(48793,15,320,seed=True);self.resolve(48750,1,320,320,180)
        for draw,resolve,width in ((48793,48814,320),(48856,48875,320),(48915,48932,160),(48968,48989,160),(49029,49053,80),(49093,49110,80)):
            self.draw(draw,16,width);self.resolve(resolve,1,width,width,width*9//16)
        self.draw(49151,15,0);self.draw(49185,15,0);self.resolve(49209,1,0,1280,720)
        if half_trace:
            self.trace=Path(half_trace).resolve();self.frame=8750
            target=C.c_uint32();texture=C.c_uint32();clear=Clear((C.c_float*4)(0,0,0,0),1,1,7)
            check(native.dbz_native_add_render_target(reference,640,360,C.byref(clear),C.byref(target),C.byref(texture)));self.targets[640]=target.value
            self.resolve(15482,1,0,1280,720);self.sprite(15540,640,10);self.resolve(15566,1,640,640,360);self.sprite(16130,0,11)

    def load(self,kind,event):
        prefix=self.trace/f'{kind}-{self.frame}-{event}';meta=json.loads(prefix.with_suffix('.json').read_text())
        return prefix,meta,bytearray(Path(str(prefix)+'-device.bin').read_bytes())

    def reuse_storage(self):
        # Same CPU allocation, two distinct native storage objects. Earlier
        # draws already reference the full-size ID when the small resolve runs.
        small=next(node for node in reversed(self.nodes) if node['kind']=='resolve' and node['width']==640)
        full=next(node for node in reversed(self.nodes) if node['kind']=='resolve' and node['width']==1280)
        old=small['meta']['copy_address'];address=full['meta']['copy_address'];assert old!=address
        small['meta']=dict(small['meta'],copy_address=address)
        rewritten=[]
        for offset,payload in small['writes']:
            payload=bytearray(payload)
            if offset==0x200000:struct.pack_into('>I',payload,10780,address)
            else:
                f1=struct.unpack_from('>I',payload,32)[0]
                struct.pack_into('>I',payload,32,(f1&4095)|0xe0000000|((address-4096)&0x1ffff000))
            rewritten.append((offset,payload))
        small['writes']=rewritten
        for node in self.nodes:
            if node['kind']!='draw':continue
            for offset,payload in node['writes']:
                if offset==0x200000 and (('operation' in node and node['operation'].type==4) or ('draw' in node and node['draw'].material[3]==18)):
                    z=struct.unpack_from('>I',payload,10548)[0];assert not z&3
                    struct.pack_into('>I',payload,10548,z|4)
                    # Actual game resamples can clamp U while wrapping V.
                    # The validated full-quad point geometry stays in bounds.
                    if 'draw' in node and node['draw'].material[3]==18:
                        f0=struct.unpack_from('>I',payload,0x480)[0];struct.pack_into('>I',payload,0x480,f0&~(7<<13))
                if offset==0x200000 and struct.unpack_from('>I',payload,0x484)[0]&0xfffff000==old:
                    f1=struct.unpack_from('>I',payload,0x484)[0];struct.pack_into('>I',payload,0x484,(f1&4095)|address)

    def snapshot(self,address,kind,width,height):
        key=(address,kind)
        if key not in self.snapshots:
            value=C.c_uint32();self.check(self.native.dbz_native_add_snapshot_texture(self.reference,kind,width,height,C.byref(value)));self.snapshots[key]=value.value
        return self.snapshots[key]

    def resolve(self,event,kind,target,width,height):
        prefix,meta,device=self.load('resolve',event);texture=self.snapshot(meta['copy_address'],kind,width,height)
        self.nodes.append(dict(kind='resolve',meta=meta,writes=[(0x200000,device),(meta['destination'],Path(str(prefix)+'-destination.bin').read_bytes())],operation=Operation(0,kind,self.targets[target],texture,Clear()),width=width,height=height))

    def call(self,prefix,meta,device):
        raw=Path(str(prefix)+'-vertices.bin').read_bytes();vs=Path(str(prefix)+'-vs-program.bin').read_bytes();pixel=Path(str(prefix)+'-ps-program.bin');ps=pixel.read_bytes() if pixel.exists() else None
        vo=bytearray(1024);po=bytearray(128);struct.pack_into('>I',device,13072,0x100000);struct.pack_into('>I',device,13068,0x110000 if ps else 0)
        struct.pack_into('>I',vo,32,0x120000);struct.pack_into('>2I',vo,872,0,len(vs));writes=[(0x200000,device),(0x100000,vo),(0x120000,vs),(0x300000,raw)]
        if ps:
            struct.pack_into('>I',po,24,0x130000);struct.pack_into('>2I',po,40,0,len(ps));writes.extend([(0x110000,po),(0x130000,ps)])
        return dict(kind='draw',meta=meta,writes=writes),raw

    def restore(self,event,target,address):
        prefix,meta,device=self.load('up',event);node,_=self.call(prefix,meta,device);node['operation']=Operation(0,4,self.targets[target],self.snapshots[(address,2)],Clear());self.nodes.append(node)

    def noop(self,event):
        prefix,meta,device=self.load('up',event);node,_=self.call(prefix,meta,device);self.nodes.append(node)

    def sprite(self,event,target,kind):
        from test_streaming_sprites import expected
        prefix,meta,device=self.load('up',event);node,raw=self.call(prefix,meta,device);vertices,matrix,control=expected(raw,device,kind)
        if meta['primitive']==6:vertices=vertices[[index for i in range(len(vertices)-2) for index in ((i+1,i,i+2) if i&1 else (i,i+1,i+2))]]
        address=struct.unpack_from('>I',device,0x484)[0]&0xfffff000
        d=FrameDraw();d.index_count=len(vertices);d.base_texture=self.snapshots[(address,1)];d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=pipeline(device);d.view_projection=(C.c_float*16)(*matrix.reshape(-1));d.material=(C.c_float*20)(*control);d.raster=Raster.from_buffer_copy(encode(device));d.reserved=self.targets[target]
        node.update(vertices=vertices,draw=d);self.nodes.append(node)

    def draw(self,event,mode,target,seed=False):
        prefix,meta,device=self.load('up',event)
        if seed:
            # Use actual clip-copy shader and source descriptor, retaining the
            # small destination raster. No original framebuffer data is supplied.
            cp,cm,cd=self.load('up',49151);device[0x480:0x498]=cd[0x480:0x498]
            node,raw=self.call(cp,cm,device)
        else:node,raw=self.call(prefix,meta,device)
        screen=node['meta']['stride']==16
        source=np.frombuffer(raw,dtype='>f4').reshape(-1,4 if screen else 5)
        points=np.array([source[k] for k in (0,1,2,2,1)]+[source[1]+source[2]-source[0]],dtype='<f4') if screen else source[[0,1,2,2,1,3]].astype('<f4')
        vertices=np.zeros((6,13),dtype='<f4');vertices[:,:2 if screen else 3]=points[:,:2 if screen else 3];vertices[:,6:8]=points[:,2:4] if screen else points[:,3:5];vertices[:,8:]=1
        d=FrameDraw();d.index_count=6;d.reserved=self.targets[target];d.diffuse=(C.c_float*3)(1,1,1);d.flags=0x703 if mode==18 else pipeline(device)|(0 if screen else 1<<22)
        f0,f1,_,f3=struct.unpack_from('>4I',device,0x480);d.base_texture=self.snapshots[(f1&0xfffff000,2 if mode==18 else 1)];d.ramp_texture=d.base_texture if mode==18 else 0xffffffff
        width=target or 1280;height=width*9//16;matrix=np.array([[2/width,0,0,-1],[0,-2/height,0,1],[0,0,1,0],[0,0,0,1]],dtype='<f4') if screen else np.eye(4,dtype='<f4');d.view_projection=(C.c_float*16)(*matrix.reshape(-1));d.raster=Raster.from_buffer_copy(encode(device))
        if screen:d.raster.viewport=(C.c_float*6)(0,0,width,height,0,1)
        d.material[3]=mode;d.material[4]=float((f3>>19&3)==0);d.material[5]=float((f0>>10&7)==2);d.material[8:12]=[1]*4;palette=None
        if mode==16:
            palette=np.frombuffer(device[6016:6016+13*16],dtype='>f4').reshape(13,4).astype('<f4');palette[:,3]=0
        elif mode==17:
            address=struct.unpack_from('>I',device,0x49c)[0]&0xfffff000;d.ramp_texture=self.snapshots[(address,2)]
            palette=np.array([struct.unpack_from('>4f',device,6032),(.1111111119389534,0,-2,0),(.0020833334419876337,-.004166666883975267,.004166666883975267,-.0020833334419876337),(.0011718750465661287,-.0011718750465661287,-.0023437500931322575,.0023437500931322575)],dtype='<f4')
        if palette is not None:d.palette_rows=len(palette);d.palette=palette.ctypes.data_as(C.POINTER(C.c_float))
        node.update(vertices=vertices,draw=d,palette=palette);self.nodes.append(node)

    def execute(self,vv,dd,ops,offset,write,base,committed,history=False):
        calls=resolves=0
        for node in self.nodes:
            if history and node['kind']=='resolve':continue
            if node['kind']=='resolve':
                meta=node['meta'];address=0x100000000+meta['copy_address']
                assert not any(address<=page<address+node['width']*node['height']*4 for page in committed),'Original GPU resolve RAM must remain unmapped'
            for address,payload in node['writes']:write(address,payload)
            meta=node['meta']
            if node['kind']=='resolve':self.api.replay_stream_resolve_rect(0x200000,meta['flags'],meta['destination'],base,0,0);resolves+=1
            else:self.api.replay_stream_up(0x200000,meta['primitive'],meta['count'],0x300000,meta['stride'],base);calls+=1
            if 'operation' in node:
                operation=Operation.from_buffer_copy(node['operation']);operation.before_draw=len(dd);ops.append(operation)
            if 'draw' in node:
                copy=FrameDraw.from_buffer_copy(node['draw']);copy.first_index=offset;dd.append(copy);vv.append(node['vertices']);offset+=len(node['vertices'])
        return offset,calls,resolves

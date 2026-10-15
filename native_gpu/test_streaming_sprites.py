"""Independent CPU conversion and D3D12 replay of current UP sprites."""
import argparse,ctypes as C,json,os,struct
from pathlib import Path
import numpy as np
from PIL import Image
from export_live_ui import fnv,geometry
from live_raster import encode,pipeline
from texture_decode import linearize
from test_streaming_frame import FrameDraw
from test_raster_targets import Raster,Clear


def expected(raw,device,kind):
    if kind==15:
        packed=np.frombuffer(raw,dtype=np.uint8).reshape(-1,16);extended=np.zeros((len(packed),24),dtype=np.uint8);extended[:,:16]=packed
        vertices,matrix,material=geometry(extended.tobytes(),device);material[:2]=0;material[3]=19;return vertices,matrix,material
    if kind==10:return geometry(raw,device)
    source=np.frombuffer(raw,dtype='>f4').reshape(-1,6)
    packed=np.frombuffer(raw,dtype=np.uint8).reshape(-1,24)
    c=np.frombuffer(device[1920:1920+4096],dtype='>f4').reshape(-1,4).astype(np.float64)
    ps=np.frombuffer(device[6016:6016+4096],dtype='>f4').reshape(-1,4).astype(np.float64)
    matrix=(c[8:12]@c[:4]).T.astype('<f4')
    positions=source[:,:3].astype('<f4');clip=np.column_stack([positions,np.ones(len(positions))])@matrix.T
    fog=np.clip(np.clip((c[12,0]-clip[:,2])*c[12,1],0,1),1-c[12,3],1-c[12,2])
    vertices=np.column_stack([positions,np.zeros_like(positions),source[:,4:6],packed[:,12:16][:,[1,2,3,0]]/255,fog]).astype('<f4')
    material=np.concatenate([[*c[13,:2],float(ps[3,3]!=0),1],c[12],np.ones(4),ps[2],ps[3]]).astype('<f4')
    return vertices,matrix,material


def run(bridge,renderer,registry,inventory,scratch):
    bridge,renderer,registry,inventory,scratch=map(lambda p:Path(p).resolve(),(bridge,renderer,registry,inventory,scratch));scratch.mkdir(parents=True,exist_ok=True)
    data=registry.read_bytes();materials={}
    for i in range(struct.unpack_from('<I',data,8)[0]):
        v,p,vs,ps,kind=struct.unpack_from('<2Q3I',data,12+i*40);materials[v,p,vs,ps]=kind
    os.environ.update(DBZ3_NATIVE_STREAM='1',DBZ3_NATIVE_RENDERER=str(renderer),DBZ3_NATIVE_SHADERS=str(renderer.parent/'materials.hlsl'),DBZ3_NATIVE_MATERIALS=str(registry));os.environ.pop('DBZ3_NATIVE_WORLD_SCENE',None)
    api=C.CDLL(str(bridge));api.replay_sprite.argtypes=[C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.c_void_p,C.POINTER(FrameDraw)]
    api.replay_stream_up.argtypes=[C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p]
    traces={Path(sample).parent for row in json.loads(inventory.read_text()) if row['stride']==24 for sample in row['samples']}
    checked=[];fixtures={};maximum={key:0. for key in ('vertices','matrix','material')}
    for trace in traces:
        for path in trace.glob('up-*.json'):
            if '-texture' in path.name:continue
            prefix=path.with_suffix('');meta=json.loads(path.read_text())
            if meta['stride'] not in (16,24):continue
            def binary(s):return Path(str(prefix)+s).read_bytes()
            if not Path(str(prefix)+'-vs-program.bin').exists() or not Path(str(prefix)+'-ps-program.bin').exists():continue
            device=binary('-device.bin');vs=binary('-vs-program.bin');ps=binary('-ps-program.bin');kind=materials.get((fnv(vs),fnv(ps),len(vs),len(ps)))
            if kind not in (10,11,15):continue
            raw=binary('-vertices.bin');v,m,control=expected(raw,device,kind);output=np.empty_like(v);draw=FrameDraw()
            assert api.replay_sprite(C.create_string_buffer(raw),len(v),C.create_string_buffer(device),kind,output.ctypes.data,C.byref(draw)),path
            deltas={'vertices':float(abs(output-v).max()),'matrix':float(abs(np.array(draw.view_projection).reshape(4,4)-m).max()),'material':float(abs(np.array(draw.material)-control).max())}
            assert deltas['vertices']<.0001 and deltas['matrix']<.0001 and deltas['material']<.00001,(path,deltas)
            for key,value in deltas.items():maximum[key]=max(maximum[key],value)
            checked.append(dict(prefix=str(prefix),kind=kind,count=len(v),errors=deltas))
            key=(kind,meta['primitive'],struct.unpack_from('>I',device,10368)[0]&0x3fff,struct.unpack_from('>2f',device,1928) if kind in (10,15) else ())
            if key not in fixtures:fixtures[key]=(prefix,meta,device,vs,ps,raw,v,m,control)
    assert {row['kind'] for row in checked}=={10,11,15} and len(checked)>100,len(checked)
    selected=[fixtures[key] for key in sorted(fixtures)]
    # The HUD also uses triangle fans. Reorder a real strip quad around its
    # perimeter, then build the reference explicitly as (0,1,2), (0,2,3).
    fan=list(next(row for row in selected if row[1]['primitive']==6 and row[1]['count']==4))
    order=[0,1,3,2];stride=fan[1]['stride'];fan[1]=dict(fan[1],primitive=5)
    fan[5]=b''.join(fan[5][i*stride:(i+1)*stride] for i in order);fan[6]=fan[6][order];selected.append(tuple(fan))
    line=list(fan);line[1]=dict(line[1],primitive=3);selected.append(tuple(line))
    subtract=list(selected[0]);subtract_device=bytearray(subtract[2])
    struct.pack_into('>I',subtract_device,10552,1|(6<<8)|(1<<5)|(1<<16)|(1<<24))
    subtract[2]=bytes(subtract_device);selected.append(tuple(subtract))
    relaxed=list(selected[0]);relaxed_device=bytearray(relaxed[2]);struct.pack_into('>I',relaxed_device,0x480,struct.unpack_from('>I',relaxed_device,0x480)[0]&~3);relaxed[2]=bytes(relaxed_device);selected.append(tuple(relaxed))
    null_fixture=list(selected[0]);null_device=bytearray(null_fixture[2]);struct.pack_into('>6I',null_device,0x480,*([0]*6));null_fixture[2]=bytes(null_device);selected.append(tuple(null_fixture))
    native=C.CDLL(str(renderer));native.dbz_native_last_error.restype=C.c_char_p
    native.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];native.dbz_native_create_stream.restype=C.c_void_p
    native.dbz_native_add_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    native.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    native.dbz_native_render.argtypes=[C.c_void_p];native.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];native.dbz_native_destroy.argtypes=[C.c_void_p]
    def check(value):assert value,native.dbz_native_last_error().decode()
    reference=native.dbz_native_create_stream(None,1280,720,str(renderer.parent/'materials.hlsl').encode());check(reference)
    k=C.WinDLL('kernel32');k.VirtualAlloc.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32,C.c_uint32];k.VirtualAlloc.restype=C.c_void_p;k.VirtualFree.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32]
    base=k.VirtualAlloc(None,0x120000000,0x2000,1);assert base;committed=set()
    def write(offset,payload):
        for page in range(offset&~4095,(offset+len(payload)+4095)&~4095,4096):
            if page not in committed:assert k.VirtualAlloc(base+page,4096,0x1000,4);committed.add(page)
        C.memmove(base+offset,bytes(payload),len(payload))
    clear=Clear((C.c_float*4)(.025,.038,.07,1),1,1,7);prepared=[];mutated=False
    previous=os.getcwd();os.chdir(scratch)
    try:
        for prefix,meta,device,vs,ps,raw,v,m,control in selected:
            words=struct.unpack_from('>6I',device,0x480)
            kind=materials[fnv(vs),fnv(ps),len(vs),len(ps)]
            if kind==15:textureRaw=b'';linear=bytes([255])*4;desc=dict(format=6,width=1,height=1,swizzle=0x688)
            elif not (words[1]&0xfffff000) and not (words[5]&0xfffff000):textureRaw=b'';linear=bytes(4);desc=dict(format=6,width=1,height=1,swizzle=0x688)
            else:
                normalized=list(words);normalized[0]|=2;textureRaw=Path(str(prefix)+'-texture0.bin').read_bytes();linear,desc=linearize(textureRaw,normalized)
            texture=C.c_uint32();buffer=C.create_string_buffer(linear);check(native.dbz_native_add_texture_blocks(reference,{6:0,18:1,19:2,20:3}[desc['format']],desc['width'],desc['height'],buffer,len(linear),desc['swizzle'],C.byref(texture)))
            count=len(v)
            if meta['primitive']==3:indices=np.array([0,1,1,2,2,3],dtype='<u4')
            elif meta['primitive']==4:indices=np.arange(count,dtype='<u4')
            elif meta['primitive']==5:indices=np.array([0,1,2,0,2,3],dtype='<u4')
            else:indices=np.array([index for i in range(count-2) for index in ((i+1,i,i+2) if i&1 else (i,i+1,i+2))],dtype='<u4')
            d=FrameDraw();d.index_count=len(indices);d.base_texture=texture.value;d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=pipeline(device)|((1<<20) if kind==15 and (struct.unpack_from('>I',device,10368)[0]&0x3fff)==560 else 0);d.view_projection=(C.c_float*16)(*m.reshape(-1));d.material=(C.c_float*20)(*control);d.raster=Raster.from_buffer_copy(encode(device))
            # Independent equivalent: sample opaque white, retaining vertex alpha.
            if meta['primitive']==3:d.flags|=1<<25
            if kind==15:d.material[3]=1
            device=bytearray(device);struct.pack_into('>I',device,13072,0x100000);struct.pack_into('>I',device,13068,0x110000)
            vo=bytearray(1024);po=bytearray(128);struct.pack_into('>I',vo,32,0x120000);struct.pack_into('>2I',vo,872,0,len(vs));struct.pack_into('>I',po,24,0x130000);struct.pack_into('>2I',po,40,0,len(ps))
            writes=[(0x200000,device),(0x100000,vo),(0x110000,po),(0x120000,vs),(0x130000,ps),(0x300000,raw)]
            if textureRaw:writes.append((0x100000000+(words[1]&0xfffff000),textureRaw))
            prepared.append((v,indices,d,meta,writes))
            if textureRaw and kind!=15 and not mutated:
                mutant=bytes(value^0x55 for value in textureRaw);linear_mutant,mutant_desc=linearize(mutant,normalized);mutant_id=C.c_uint32()
                check(native.dbz_native_add_texture_blocks(reference,{6:0,18:1,19:2,20:3}[mutant_desc['format']],mutant_desc['width'],mutant_desc['height'],C.create_string_buffer(linear_mutant),len(linear_mutant),mutant_desc['swizzle'],C.byref(mutant_id)))
                mutant_draw=FrameDraw.from_buffer_copy(d);mutant_draw.base_texture=mutant_id.value;physical=0x100000000+(words[1]&0xfffff000);mutant_writes=[(address,mutant if address==physical else payload) for address,payload in writes]
                prepared.append((v,indices,mutant_draw,meta,mutant_writes));mutated=True
        frames=[]
        for frame in range(12):
            selection=[] if frame==6 else [prepared[frame%len(prepared)]] if frame%3 else list(reversed(prepared))
            vv=[];ii=[];dd=[];offset=first=0
            for v,indices,d,meta,writes in selection:
                copy=FrameDraw.from_buffer_copy(d);copy.first_index=first;dd.append(copy);vv.append(v);ii.append(indices+offset);first+=len(indices);offset+=len(v)
                for address,payload in writes:write(address,payload)
                api.replay_stream_up(0x200000,meta['primitive'],meta['count'],0x300000,meta['stride'],base)
            api.replay_stream_present();api.replay_stream_status();status=json.loads(Path('native-stream-status.json').read_text());assert not status['error'] and not status['rejected_up_draws'],status
            vertices=np.concatenate(vv) if vv else np.empty((0,13),dtype='<f4');indices=np.concatenate(ii).astype('<u4') if ii else np.empty(0,dtype='<u4');draws=(FrameDraw*len(dd))(*dd)
            check(native.dbz_native_set_frame(reference,vertices.ctypes.data,len(vertices),indices.ctypes.data,len(indices),draws,len(draws),C.byref(clear)));check(native.dbz_native_render(reference));path=scratch/'expected.ppm';check(native.dbz_native_capture(reference,str(path).encode()))
            # Capture the frontend's current renderer via its test-only entry.
            path2=scratch/'actual.ppm';api.replay_stream_capture.argtypes=[C.c_char_p];assert api.replay_stream_capture(str(path2).encode())
            delta=abs(np.array(Image.open(path)).astype(int)-np.array(Image.open(path2)).astype(int));assert delta.max()<=2,(frame,int(delta.max()))
            frames.append(dict(frame=frame+1,draws=len(selection),max_pixel_error=int(delta.max()),mean_pixel_error=float(delta.mean())))
        assert status['accepted_up_draws']>12 and status['frames_with_indexed_draws']==0 and mutated and status['source_texture_cache']['hits']>0,status
        result=dict(scope='Current CPU sprite conversion across real captures, three checked sprite/solid-color shader families, triangle list/strip, changing combinations, order, textures and empty frames. Includes mask-target solid draws, valid type-zero asset bindings and genuinely absent textures. No geometry manifest or reference runtime. Other UP/UI/postprocess families remain separate.',checked_draws=len(checked),maximum_cpu_errors=maximum,frames=frames,status=status,conversion=checked)
        Path('streaming-sprite-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps({key:value for key,value in result.items() if key!='conversion'},indent=2))
    finally:api.replay_stream_shutdown();native.dbz_native_destroy(reference);k.VirtualFree(base,0,0x8000);os.chdir(previous)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--bridge',required=True);p.add_argument('--renderer',required=True);p.add_argument('--registry',required=True);p.add_argument('--inventory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.bridge,a.renderer,a.registry,a.inventory,a.scratch)

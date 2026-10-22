"""Replay current CPU video planes, including concurrent movies and resource reuse."""
import argparse,ctypes as C,json,os,struct
from pathlib import Path
import numpy as np
from PIL import Image
from export_live_ui import geometry
from texture_decode import linearize
from live_raster import encode,pipeline
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear,Raster
from test_frame_operations import Operation


def run(bridge,renderer,registry,fixtures,scratch,fxaa_fixtures=None,resolve_fixtures=None,fxaa_resolve_prefix=None,post_graph_trace=None,half_resolution_trace=None,history_frames=0,world_fixture=None,reuse_resolve_storage=False):
    bridge,renderer,registry,fixtures,scratch=map(lambda p:Path(p).resolve(),(bridge,renderer,registry,fixtures,scratch));scratch.mkdir(parents=True,exist_ok=True)
    if post_graph_trace:post_graph_trace=Path(post_graph_trace).resolve()
    if half_resolution_trace:half_resolution_trace=Path(half_resolution_trace).resolve();assert post_graph_trace
    assert 0<=history_frames<=16 and (not history_frames or post_graph_trace)
    os.environ.update(DBZ3_NATIVE_STREAM='1',DBZ3_NATIVE_RENDERER=str(renderer),DBZ3_NATIVE_SHADERS=str(renderer.parent/'materials.hlsl'),DBZ3_NATIVE_MATERIALS=str(registry));os.environ.pop('DBZ3_NATIVE_WORLD_SCENE',None)
    source=[]
    for row in json.loads(fixtures.read_text()):
        prefix=Path(row['prefix']);device=Path(str(prefix)+'-device.bin').read_bytes();raw=Path(str(prefix)+'-vertices.bin').read_bytes()
        source.append(dict(prefix=prefix,device=device,raw=raw,vs=Path(str(prefix)+'-vs-program.bin').read_bytes(),ps=Path(str(prefix)+'-ps-program.bin').read_bytes(),textures=[Path(str(prefix)+f'-texture{i}.bin').read_bytes() for i in range(3)]))
    assert len(source)>=2
    clamped=dict(source[0]);device=bytearray(clamped['device'])
    for slot in range(3):struct.pack_into('>I',device,0x480+slot*24,struct.unpack_from('>I',device,0x480+slot*24)[0]|(2<<10)|(2<<13))
    clamped['device']=bytes(device);coords=np.frombuffer(clamped['raw'],dtype='>f4').reshape(-1,6).copy();coords[:,4:6]=coords[:,4:6]*1.5-.25;clamped['raw']=coords.tobytes();source.append(clamped)
    for side,values in ((0,(80,90,220)),(1,(140,200,50))):
        item=dict(source[0]);positions=np.frombuffer(item['raw'],dtype='>f4').reshape(-1,6).copy();sx=struct.unpack_from('>f',item['device'],1920)[0]
        positions[:,0]=positions[:,0]*.5+side/sx;item['raw']=positions.tobytes();item['textures']=[bytes([value])*len(raw) for value,raw in zip(values,item['textures'])];source.append(item)
    if world_fixture:
        prefix=Path(world_fixture).resolve()
        source.append(dict(prefix=prefix,world=True,device=Path(str(prefix)+'-device.bin').read_bytes(),raw=Path(str(prefix)+'-vertices.bin').read_bytes(),vs=Path(str(prefix)+'-vs-program.bin').read_bytes(),ps=Path(str(prefix)+'-ps-program.bin').read_bytes(),textures=[Path(str(prefix)+f'-texture{i}.bin').read_bytes() for i in range(3)]))
    post=[]
    if fxaa_fixtures:
        for row in json.loads(Path(fxaa_fixtures).resolve().read_text()):
            prefix=Path(row['prefix']);device=Path(str(prefix)+'-device.bin').read_bytes();raw=Path(str(prefix)+'-vertices.bin').read_bytes()
            post.append(dict(device=device,raw=raw,vs=Path(str(prefix)+'-vs-program.bin').read_bytes(),ps=Path(str(prefix)+'-ps-program.bin').read_bytes()))
        assert post
        # Exercise both game sampler variants without supplying scene pixels.
        clamped=dict(post[0]);device=bytearray(clamped['device']);fetch=struct.unpack_from('>I',device,0x480)[0];struct.pack_into('>I',device,0x480,(fetch&~((7<<10)|(7<<13)))|(2<<10)|(2<<13));clamped['device']=bytes(device);post.append(clamped)
    api=C.CDLL(str(bridge));api.replay_stream_up.argtypes=[C.c_uint32]*5+[C.c_void_p];api.replay_stream_capture.argtypes=[C.c_char_p]
    api.replay_stream_resolve.argtypes=[C.c_uint32]*3+[C.c_void_p]
    api.replay_stream_resolve_rect.argtypes=[C.c_uint32]*3+[C.c_void_p]+[C.c_uint32]*2
    native=C.CDLL(str(renderer));native.dbz_native_last_error.restype=C.c_char_p
    native.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];native.dbz_native_create_stream.restype=C.c_void_p
    native.dbz_native_add_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    native.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    native.dbz_native_set_frame_operations.argtypes=[C.c_void_p,C.POINTER(Operation),C.c_uint32];native.dbz_native_add_snapshot_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    native.dbz_native_render.argtypes=[C.c_void_p];native.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];native.dbz_native_destroy.argtypes=[C.c_void_p]
    def check(value):assert value,native.dbz_native_last_error().decode()
    reference=native.dbz_native_create_stream(None,1280,720,str(renderer.parent/'materials.hlsl').encode());check(reference)
    k=C.WinDLL('kernel32');k.VirtualAlloc.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32,C.c_uint32];k.VirtualAlloc.restype=C.c_void_p;k.VirtualFree.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32]
    base=k.VirtualAlloc(None,0x120000000,0x2000,1);assert base;committed=set()
    def write(offset,data):
        for page in range(offset&~4095,(offset+len(data)+4095)&~4095,4096):
            if page not in committed:assert k.VirtualAlloc(base+page,4096,0x1000,4);committed.add(page)
        C.memmove(base+offset,bytes(data),len(data))
    resolve_rows=json.loads(Path(resolve_fixtures).resolve().read_text()) if resolve_fixtures else []
    fxaa_resolve=None
    if fxaa_resolve_prefix:
        prefix=Path(fxaa_resolve_prefix).resolve();meta=json.loads(prefix.with_suffix('.json').read_text());fxaa_resolve=dict(meta=meta,device=Path(str(prefix)+'-device.bin').read_bytes(),destination=Path(str(prefix)+'-destination.bin').read_bytes())
    prepared=[];clear=Clear((C.c_float*4)(.025,.038,.07,1),1,1,7);previous=os.getcwd();os.chdir(scratch)
    try:
        for item in source:
            device=item['device']
            if item.get('world'):
                from test_streaming_sprites import expected
                v,m,control=expected(item['raw'],device,11)
            else:v,m,control=geometry(item['raw'],device)
            control[2]=0;control[3]=13;control[4:8]=0;control[16:20]=0;v[:,-1]=1
            clamps=[float((struct.unpack_from('>I',device,0x480+slot*24)[0]>>10&7)==2) for slot in range(3)];control[4:7]=[clamps[1],clamps[2],clamps[0]]
            planes=[];layouts=[];writes=[(0x300000,item['raw'])]
            for slot,raw in enumerate(item['textures']):
                words=struct.unpack_from('>6I',device,0x480+slot*24);data,layout=linearize(raw,words);planes.append(data);layouts.append(layout);writes.append((0x100000000+(words[1]&0xfffff000),raw))
            chroma=np.column_stack([np.frombuffer(planes[1],dtype=np.uint8),np.frombuffer(planes[2],dtype=np.uint8)]).tobytes();ids=[]
            for fmt,layout,data in ((4,layouts[0],planes[0]),(5,layouts[1],chroma)):
                texture=C.c_uint32();check(native.dbz_native_add_texture_blocks(reference,fmt,layout['width'],layout['height'],C.create_string_buffer(data),len(data),0x688,C.byref(texture)));ids.append(texture.value)
            d=FrameDraw();d.index_count=len(v);d.base_texture,d.ramp_texture=ids;d.diffuse=(C.c_float*3)(1,1,1);d.flags=pipeline(device);d.view_projection=(C.c_float*16)(*m.reshape(-1));d.material=(C.c_float*20)(*control);d.raster=Raster.from_buffer_copy(encode(device))
            device=bytearray(device);struct.pack_into('>I',device,13072,0x100000);struct.pack_into('>I',device,13068,0x110000)
            vo=bytearray(1024);po=bytearray(128);struct.pack_into('>I',vo,32,0x120000);struct.pack_into('>2I',vo,872,0,len(item['vs']));struct.pack_into('>I',po,24,0x130000);struct.pack_into('>2I',po,40,0,len(item['ps']))
            writes.extend([(0x200000,device),(0x100000,vo),(0x110000,po),(0x120000,item['vs']),(0x130000,item['ps'])]);prepared.append((v,d,writes))
        prepared_post=[]
        for item in post:
            device=bytearray(item['device']);source=np.frombuffer(item['raw'],dtype='>f4').reshape(-1,4);points=[]
            for a,b,c in source.reshape(-1,3,4):points.extend((a,b,c,c,b,b+c-a))
            points=np.array(points,dtype='<f4');v=np.zeros((len(points),13),dtype='<f4');v[:,:2]=points[:,:2];v[:,6:8]=points[:,2:4];v[:,8:]=1
            d=FrameDraw();d.index_count=len(v);d.base_texture=d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=pipeline(device)|(1<<24)
            matrix=np.array([[2/1280,0,0,-1],[0,-2/720,0,1],[0,0,1,0],[0,0,0,1]],dtype='<f4');d.view_projection=(C.c_float*16)(*matrix.reshape(-1));d.material[3]=14;d.material[4:6]=struct.unpack_from('>2f',device,1920);d.material[6]=float((struct.unpack_from('>I',device,0x480)[0]>>10&7)==2);d.material[8:12]=[1]*4;d.material[12:14]=struct.unpack_from('>2f',device,6016)
            d.raster=Raster.from_buffer_copy(encode(device));d.raster.viewport=(C.c_float*6)(0,0,1280,720,0,1)
            scene_offset=0x100000000+(struct.unpack_from('>I',device,0x484)[0]&0xfffff000)
            struct.pack_into('>I',device,13072,0x100000);struct.pack_into('>I',device,13068,0x110000)
            vo=bytearray(1024);po=bytearray(128);struct.pack_into('>I',vo,32,0x120000);struct.pack_into('>2I',vo,872,0,len(item['vs']));struct.pack_into('>I',po,24,0x130000);struct.pack_into('>2I',po,40,0,len(item['ps']))
            writes=[(0x200000,device),(0x300000,item['raw']),(0x100000,vo),(0x110000,po),(0x120000,item['vs']),(0x130000,item['ps'])];prepared_post.append((v,d,writes,scene_offset))
        prepared_resolves=[];resolve_snapshots={}
        for row in resolve_rows:
            rp=Path(row['resolve_prefix']);cp=Path(row['copy_prefix']);meta=json.loads(rp.with_suffix('.json').read_text());copy_meta=json.loads(cp.with_suffix('.json').read_text());device=bytearray(Path(str(cp)+'-device.bin').read_bytes());raw=Path(str(cp)+'-vertices.bin').read_bytes();vs=Path(str(cp)+'-vs-program.bin').read_bytes();ps=Path(str(cp)+'-ps-program.bin').read_bytes();screen=copy_meta['stride']==16
            source=np.frombuffer(raw,dtype='>f4').reshape(-1,4 if screen else 5)
            points=np.array([source[k] for k in (0,1,2,2,1)]+[source[1]+source[2]-source[0]],dtype='<f4') if screen else source[[0,1,2,2,1,3]].astype('<f4')
            v=np.zeros((6,13),dtype='<f4');v[:,:2 if screen else 3]=points[:,:2 if screen else 3];v[:,6:8]=points[:,2:4] if screen else points[:,3:5];v[:,8:]=1
            sources=[]
            for prefix in [rp]+([Path(row['depth_resolve_prefix'])] if row.get('depth_resolve_prefix') else []):
                resolve_meta=json.loads(prefix.with_suffix('.json').read_text());kind=2 if resolve_meta['flags']==4 else 1;key=(resolve_meta['copy_address'],kind)
                if key not in resolve_snapshots:
                    snapshot=C.c_uint32();check(native.dbz_native_add_snapshot_texture(reference,kind,1280,720,C.byref(snapshot)));resolve_snapshots[key]=snapshot.value
                resolve_writes=[(0x200000,Path(str(prefix)+'-device.bin').read_bytes()),(resolve_meta['destination'],Path(str(prefix)+'-destination.bin').read_bytes())];sources.append((resolve_meta,resolve_writes,kind,resolve_snapshots[key]))
            d=FrameDraw();d.index_count=6;d.base_texture=sources[0][3];d.ramp_texture=sources[1][3] if len(sources)>1 else 0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=pipeline(device)|(0 if screen else 1<<22);matrix=np.array([[2/1280,0,0,-1],[0,-2/720,0,1],[0,0,1,0],[0,0,0,1]],dtype='<f4') if screen else np.eye(4,dtype='<f4');d.view_projection=(C.c_float*16)(*matrix.reshape(-1));d.material[3]=row.get('mode',15);f0=struct.unpack_from('>I',device,0x480)[0];f3=struct.unpack_from('>I',device,0x48c)[0];d.material[4]=float((f3>>19&3)==0);d.material[5]=float((f0>>10&7)==2);d.material[8:12]=[1]*4;d.raster=Raster.from_buffer_copy(encode(device));palette=None
            if d.material[3]==17:
                palette=np.array([struct.unpack_from('>4f',device,6032),(.1111111119389534,0,-2,0),(.0020833334419876337,-.004166666883975267,.004166666883975267,-.0020833334419876337),(.0011718750465661287,-.0011718750465661287,-.0023437500931322575,.0023437500931322575)],dtype='<f4');d.palette_rows=4;d.palette=palette.ctypes.data_as(C.POINTER(C.c_float))
            if screen:d.raster.viewport=(C.c_float*6)(0,0,1280,720,0,1)
            struct.pack_into('>I',device,13072,0x100000);struct.pack_into('>I',device,13068,0x110000);vo=bytearray(1024);po=bytearray(128);struct.pack_into('>I',vo,32,0x120000);struct.pack_into('>2I',vo,872,0,len(vs));struct.pack_into('>I',po,24,0x130000);struct.pack_into('>2I',po,40,0,len(ps))
            writes=[(0x200000,device),(0x300000,raw),(0x100000,vo),(0x110000,po),(0x120000,vs),(0x130000,ps)];services=[]
            for value in row.get('services',[]):
                prefix=Path(value);service_meta=json.loads(prefix.with_suffix('.json').read_text());service_device=bytearray(Path(str(prefix)+'-device.bin').read_bytes());service_raw=Path(str(prefix)+'-vertices.bin').read_bytes();service_vs=Path(str(prefix)+'-vs-program.bin').read_bytes();ps_path=Path(str(prefix)+'-ps-program.bin');service_ps=ps_path.read_bytes() if ps_path.exists() else None
                struct.pack_into('>I',service_device,13072,0x100000);struct.pack_into('>I',service_device,13068,0x110000 if service_ps else 0);service_vo=bytearray(1024);struct.pack_into('>I',service_vo,32,0x120000);struct.pack_into('>2I',service_vo,872,0,len(service_vs));service_writes=[(0x200000,service_device),(0x300000,service_raw),(0x100000,service_vo),(0x120000,service_vs)]
                if service_ps:
                    service_po=bytearray(128);struct.pack_into('>I',service_po,24,0x130000);struct.pack_into('>2I',service_po,40,0,len(service_ps));service_writes.extend([(0x110000,service_po),(0x130000,service_ps)])
                services.append(dict(writes=service_writes,meta=service_meta,restore=bool(service_ps),snapshot=sources[1][3] if service_ps else None))
            prepared_resolves.append(dict(vertices=v,draw=d,writes=writes,sources=sources,copy_meta=copy_meta,palette=palette,services=services))
            if d.material[3]==17:
                # Actual battles also use bilinear D24 sampling in this same
                # checked DOF family. Keep a point variant and a linear variant.
                linear_device=bytearray(device);f3=struct.unpack_from('>I',linear_device,0x4a4)[0]
                struct.pack_into('>I',linear_device,0x4a4,(f3&~((3<<19)|(3<<21)))|(1<<19)|(1<<21))
                linear_writes=[(offset,linear_device if offset==0x200000 else payload) for offset,payload in writes]
                linear_draw=FrameDraw.from_buffer_copy(d);linear_draw.material[7]=1
                prepared_resolves.append(dict(vertices=v,draw=linear_draw,writes=linear_writes,sources=sources,copy_meta=copy_meta,palette=palette,services=services))
        if fxaa_resolve:
            fxaa_resolve['sources']={}
            for _,d,_,scene_offset in prepared_post:
                address=scene_offset-0x100000000;key=(address,1)
                if key not in resolve_snapshots:
                    snapshot=C.c_uint32();check(native.dbz_native_add_snapshot_texture(reference,1,1280,720,C.byref(snapshot)));resolve_snapshots[key]=snapshot.value
                meta=dict(fxaa_resolve['meta']);meta['copy_address']=address;device=bytearray(fxaa_resolve['device']);struct.pack_into('>I',device,10780,address);destination=bytearray(fxaa_resolve['destination']);f1=struct.unpack_from('>I',destination,32)[0];struct.pack_into('>I',destination,32,(f1&4095)|0xe0000000|((address-4096)&0x1ffff000));fxaa_resolve['sources'][scene_offset]=dict(meta=meta,device=device,destination=destination,snapshot=resolve_snapshots[key]);d.base_texture=resolve_snapshots[key];d.flags&=~(1<<24)
        graph=None
        if post_graph_trace:
            from test_post_graph import PostGraph
            graph=PostGraph(post_graph_trace,native,reference,api,resolve_snapshots,check,half_resolution_trace)
            if reuse_resolve_storage:
                assert half_resolution_trace and not history_frames
                graph.reuse_storage()
        results=[];warmed=None;observed=0;resolves=0
        for frame in range(24+history_frames):
            history=frame>=24
            selection=[] if frame==7 or history else prepared[-2:] if frame%6==2 else list(reversed(prepared[-2:])) if frame%6==3 else [prepared[(frame//6+frame%6)%(len(prepared)-2)]]
            vv=[];dd=[];offset=0
            for vertices,d,writes in selection:
                for address,payload in writes:write(address,payload)
                api.replay_stream_up(0x200000,4,len(vertices),0x300000,24,base);copy=FrameDraw.from_buffer_copy(d);copy.first_index=offset;dd.append(copy);vv.append(vertices);offset+=len(vertices);observed+=1
            ops=[];copy_count=0;service_count=0
            if selection and prepared_resolves:
                copy_count=2 if frame%3==0 else 1
                for step in range(copy_count):
                    item=prepared_resolves[(frame+step)%len(prepared_resolves)];vertices,d,writes,copy_meta=item['vertices'],item['draw'],item['writes'],item['copy_meta']
                    for meta,resolve_writes,kind,snapshot in item['sources']:
                        assert not any(0x100000000+meta['copy_address']<=page<0x100000000+meta['copy_address']+1280*720*4 for page in committed),'Original resolve pixels must remain unmapped'
                        for address,payload in resolve_writes:write(address,payload)
                        source_rect=0x170000 if frame%2==0 else 0;destination_point=0x170100 if frame%2==0 else 0
                        if source_rect:write(source_rect,struct.pack('>4I',0,0,1280,720));write(destination_point,struct.pack('>2I',0,0))
                        api.replay_stream_resolve_rect(0x200000,meta['flags'],meta['destination'],base,source_rect,destination_point);ops.append(Operation(len(dd),kind,0,snapshot,Clear()));resolves+=1
                    for service in item['services']:
                        for address,payload in service['writes']:write(address,payload)
                        sm=service['meta'];api.replay_stream_up(0x200000,sm['primitive'],sm['count'],0x300000,sm['stride'],base);observed+=1;service_count+=1
                        if service['restore']:ops.append(Operation(len(dd),4,0,service['snapshot'],Clear()))
                    for address,payload in writes:write(address,payload)
                    api.replay_stream_up(0x200000,copy_meta['primitive'],copy_meta['count'],0x300000,copy_meta['stride'],base);copy=FrameDraw.from_buffer_copy(d);copy.first_index=offset;dd.append(copy);vv.append(vertices);offset+=len(vertices);observed+=1
            if (selection or history) and graph:
                offset,calls,graph_resolves=graph.execute(vv,dd,ops,offset,write,base,committed,history);observed+=calls;resolves+=graph_resolves
            post_count=0
            if selection and prepared_post:
                post_count=2 if frame%2==0 else 1
                for step in range(post_count):
                    vertices,d,writes,scene_offset=prepared_post[(frame+step)%len(prepared_post)]
                    assert not any(scene_offset<=page<scene_offset+1280*720*4 for page in committed),'Reference scene memory must remain unmapped'
                    if fxaa_resolve:
                        source=fxaa_resolve['sources'][scene_offset];meta=source['meta'];write(0x200000,source['device']);write(meta['destination'],source['destination']);api.replay_stream_resolve_rect(0x200000,meta['flags'],meta['destination'],base,0,0);ops.append(Operation(len(dd),1,0,source['snapshot'],Clear()));resolves+=1
                        if meta['flags']&0x100:ops.append(Operation(len(dd),3,0,0,Clear((C.c_float*4)(0,0,0,0),1,0,1)))
                    for address,payload in writes:write(address,payload)
                    api.replay_stream_up(0x200000,8,len(vertices)//2,0x300000,16,base);copy=FrameDraw.from_buffer_copy(d);copy.first_index=offset
                    if not fxaa_resolve:
                        prior_snapshot=resolve_snapshots.get((scene_offset-0x100000000,1))
                        if prior_snapshot is not None and any(op.type==1 and op.destination_texture==prior_snapshot for op in ops):copy.base_texture=prior_snapshot;copy.flags&=~(1<<24)
                    dd.append(copy);vv.append(vertices);offset+=len(vertices);observed+=1
            api.replay_stream_present();api.replay_stream_status();status=json.loads(Path('native-stream-status.json').read_text());assert not status['error'] and not status['rejected_up_draws'] and status['accepted_up_draws']==observed,status
            vertices=np.concatenate(vv) if vv else np.empty((0,13),dtype='<f4');indices=np.arange(len(vertices),dtype='<u4');draws=(FrameDraw*len(dd))(*dd)
            check(native.dbz_native_set_frame(reference,vertices.ctypes.data,len(vertices),indices.ctypes.data,len(indices),draws,len(draws),C.byref(clear)));check(native.dbz_native_set_frame_operations(reference,(Operation*len(ops))(*ops),len(ops)));check(native.dbz_native_render(reference));expected=scratch/'expected.ppm';check(native.dbz_native_capture(reference,str(expected).encode()));actual=scratch/'actual.ppm';assert api.replay_stream_capture(str(actual).encode())
            delta=abs(np.array(Image.open(actual)).astype(int)-np.array(Image.open(expected)).astype(int));assert delta.max()<=2,(frame,int(delta.max()),status)
            if frame==8:warmed={key:status['renderer_resources'][key] for key in ('owned_objects','pipeline_count')}
            if frame>8:assert {key:status['renderer_resources'][key] for key in warmed}==warmed,status
            if frame in (2,15):Image.open(actual).save(f'video-{frame+1}.png')
            results.append(dict(frame=frame+1,draws=len(selection),history_only=history,native_copy_draws=copy_count,native_service_calls=service_count,fxaa_draws=post_count,max_pixel_error=int(delta.max()),mean_pixel_error=float(delta.mean())))
        assert status['video_texture_slots']==2 and status['registered_textures']==4+len(resolve_snapshots)+(len(graph.targets)-1 if graph else 0) and status['video_texture_updates']>16,status
        result=dict(scope='Actual dynamic native video frontend, supplied original CPU Y/Cb/Cr buffers. Canonical chroma interleaved at its original resolution; colorspace conversion on GPU. Independent typed Python geometry/plane conversion matches the frontend pixels. Two simultaneous distinct movies test pending draw resource ownership; repeated frames and empty frames test bounded native resource reuse. Original-GPU pixel fidelity is tested separately.',frames=results,status=status,warmed_resources=warmed)
        if prepared_post:result.update(fxaa_scope='Actual captured game FXAA calls, original scene memory unmapped. Native frontend samples its own GPU scene snapshot. One/two consecutive passes, clamp/wrap, changing source and empty frames match independently prepared typed geometry/state.',reference_scene_memory_supplied=False)
        if graph:result.update(post_graph_scope='Real CPU depth downsample, packed depth resolve, small depth/stencil restore, 6 Gaussian passes at 320/160/80, and main composition. Small color scene seeded from own GPU video instead of intervening 3D geometry. Original resolved GPU RAM unmapped; independent typed targets, palettes, geometry and operations match frontend pixels.',post_graph_frames=sum(row['draws']>0 for row in results))
        if half_resolution_trace:result.update(half_resolution_scope='Real CPU screen sprite reads native main color into a 640x360 target, resolves it on GPU and composites the own resolved image back into the main target. No original color pixels supplied.')
        if history_frames:result.update(history_frames=history_frames,history_scope='Extra complete postprocess graphs omit every resolve and CPU video producer, consuming only GPU snapshots retained from earlier completed frames. Covers native color/depth copies, depth/stencil resample/restore and ordinary world sprites. All original GPU framebuffer RAM remains unmapped.')
        if world_fixture:
            assert status['material_draw_counts'][22]>0,status
            result['world_video_scope']='Actual recovered world-space YUV shader identity, CPU matrix and vertices; synthetic source Y/Cb/Cr planes. Independent world matrix and BT.601 material match frontend pixels.'
        if reuse_resolve_storage:result['resolve_storage_reuse']='One guest physical alias changes from full-size color to 640x360 color and back each frame; pending draws keep independent native image versions and resource counts stay bounded.'
        if prepared_resolves or fxaa_resolve:
            assert status['accepted_resolves']==resolves and not status['rejected_resolves'],status
            result.update(resolve_scope='Actual CPU color/depth resolve destination headers and pixel/clip copy/depth-of-field calls. Original resolved GPU color/depth RAM remains unmapped. High-level resolves create ordered own-GPU snapshots, followed by native copy/post materials. Optional real FXAA resolve-and-clear fixtures verify preserved input after clearing the main target. Independent typed geometry and operations match frontend pixels.',original_resolve_pixels_supplied=False)
        null_services=[service for item in prepared_resolves for service in item['services'] if not service['restore']]
        if null_services:
            service=null_services[0]
            for address,payload in service['writes']:write(address,payload)
            bad_device=bytearray(service['writes'][0][1]);struct.pack_into('>I',bad_device,10548,struct.unpack_from('>I',bad_device,10548)[0]|4);write(0x200000,bad_device);sm=service['meta'];api.replay_stream_up(0x200000,sm['primitive'],sm['count'],0x300000,sm['stride'],base);api.replay_stream_status();bad_status=json.loads(Path('native-stream-status.json').read_text());assert bad_status['rejected_up_draws']==1 and bad_status['up_rejections'].get('Unsupported null pixel shader intent')==1 and not bad_status['error'],bad_status
            assert api.replay_stream_capture(str(actual).encode());assert np.array_equal(np.array(Image.open(actual)),np.array(Image.open(expected)));result['depth_writing_null_shader_rejected']=True
        Path('streaming-video-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    finally:api.replay_stream_shutdown();native.dbz_native_destroy(reference);k.VirtualFree(base,0,0x8000);os.chdir(previous)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--bridge',required=True);p.add_argument('--renderer',required=True);p.add_argument('--registry',required=True);p.add_argument('--fixtures',required=True);p.add_argument('--scratch',required=True);p.add_argument('--fxaa-fixtures');p.add_argument('--resolve-fixtures');p.add_argument('--fxaa-resolve-prefix');p.add_argument('--post-graph-trace');p.add_argument('--half-resolution-trace');p.add_argument('--history-frames',type=int,default=0);p.add_argument('--world-fixture');p.add_argument('--reuse-resolve-storage',action='store_true');a=p.parse_args();run(a.bridge,a.renderer,a.registry,a.fixtures,a.scratch,a.fxaa_fixtures,a.resolve_fixtures,a.fxaa_resolve_prefix,a.post_graph_trace,a.half_resolution_trace,a.history_frames,a.world_fixture,a.reuse_resolve_storage)

"""Replay original CPU draws through the production C++ indexed bridge.

This is a hardware regression, without the game runtime or reference GPU. It
checks guest addressing (including the E0000000 alias), original indices and
vertices, draw matching, matrix/material conversion, and live camera updates.
It does not establish pixel fidelity against the original game.
"""
import argparse,ctypes as C,json,os,struct,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from texture_decode import describe,tiled_offset


def run(bridge,trace,scene,renderer,scratch):
    bridge,trace,scene,renderer,scratch=map(lambda p:Path(p).resolve(),(bridge,trace,scene,renderer,scratch));scratch.mkdir(parents=True,exist_ok=True)
    os.environ['DBZ3_NATIVE_WORLD_SCENE']=str(scene);os.environ['DBZ3_NATIVE_RENDERER']=str(renderer);os.environ['DBZ3_NATIVE_SHADERS']=str(renderer.parent/'materials.hlsl')
    os.chdir(scratch)
    k=C.WinDLL('kernel32',use_last_error=True);k.VirtualAlloc.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32,C.c_uint32];k.VirtualAlloc.restype=C.c_void_p
    k.VirtualFree.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32]
    base=k.VirtualAlloc(None,0x120000000,0x2000,1);assert base,'Guest reserve failed'
    committed=set()
    def write(offset,data):
        for page in range(offset&~4095,(offset+len(data)+4095)&~4095,4096):
            if page not in committed:assert k.VirtualAlloc(base+page,4096,0x1000,4);committed.add(page)
        C.memmove(base+offset,bytes(data),len(data))
    api=C.CDLL(str(bridge));api.replay_draw.argtypes=[C.c_uint32,C.c_uint32,C.c_int32,C.c_uint32,C.c_uint32,C.c_void_p]
    rows=[];padding_mutations=0
    report=json.loads(scene.with_suffix('.json').read_text())
    for item in report['draws']:
        prefix=trace/item['source']
        def binary(s):return Path(str(prefix)+s).read_bytes()
        meta=json.loads(prefix.with_suffix('.json').read_text());device=bytearray(binary('-device.bin'))
        def put(o,v):struct.pack_into('>I',device,o,v)
        put(13072,0x100000);put(13068,0x110000);put(12812,0x140000)
        vs=bytearray(1024);ps=bytearray(128);vc,pc=binary('-vs-program.bin'),binary('-ps-program.bin')
        struct.pack_into('>I',vs,32,0x120000);struct.pack_into('>I',vs,896,0);struct.pack_into('>2I',vs,872,0,len(vc))
        struct.pack_into('>I',ps,24,0x130000);struct.pack_into('>I',ps,64,0);struct.pack_into('>2I',ps,40,0,len(pc))
        ib=bytearray(32);struct.pack_into('>I',ib,0,meta['index_flags']);struct.pack_into('>I',ib,24,0xe1000000)
        physical=struct.unpack_from('>I',device,0x484)[0]&0xfffff000
        texture=b'';padding=None
        if item.get('kind') not in ('skin_outline','rigid_outline','skin_mask','rigid_mask','projected_shadow','untextured_effect'):
            texture=binary('-texture0.bin');fetch=struct.unpack_from('>6I',device,0x480);layout=describe(fetch)
            w=(layout['width']+layout['block']-1)//layout['block'];h=(layout['height']+layout['block']-1)//layout['block'];y,x=np.indices((h,w),dtype=np.int64)
            x+=layout['packed_x'];y+=layout['packed_y']
            offset=tiled_offset(x,y,layout['pitch'],layout['bpp'].bit_length()-1) if layout['tiled'] else (y*layout['pitch']+x)*layout['bpp']
            used=np.zeros(len(texture),dtype=bool);used[(offset[:,:,None]+np.arange(layout['bpp'])).reshape(-1)]=True
            padding=np.flatnonzero(~used);padding=int(padding[0]) if len(padding) else None
        extra=[]
        if item.get('kind') in ('character','rigid'):
            fetch1=struct.unpack_from('>6I',device,0x498);palette=struct.unpack_from('>I',device,0x770)[0]&0x1ffffffc
            extra=[(0x100000000+(fetch1[1]&0xfffff000),binary('-texture1.bin'))]
        if item.get('kind') in ('character','skin_outline','skin_color','skin_mask'):
            palette=struct.unpack_from('>I',device,0x770)[0]&0x1ffffffc
            extra.append((0x100000000+palette,binary('-palette.bin')))
        rows.append((meta,device,vs,ps,ib,vc,pc,binary('-vertices.bin'),binary('-indices.bin'),physical,texture,padding,extra))
    try:
        for frame in range(15):
            for draw,(meta,original,vs,ps,ib,vc,pc,vb,indices,physical,texture,padding,extra) in enumerate(rows):
                if frame==14 and draw==0:continue
                device=bytearray(original)
                if frame:
                    value=struct.unpack_from('>f',device,1920)[0];struct.pack_into('>f',device,1920,value*(1-.005*frame))
                    if padding is not None:
                        texture=bytearray(texture);texture[padding]^=(frame*17)&255;padding_mutations+=1
                for offset,data in ((0x200000,device),(0x100000,vs),(0x110000,ps),(0x140000,ib),(0x120000,vc),(0x130000,pc),(0x100000000+meta['vertex_address'],vb),(0x100000000+0x01001000,indices),(0x100000000+physical,texture)):write(offset,data)
                for offset,data in extra:write(offset,data)
                api.replay_draw(0x200000,meta['primitive'],meta['base_vertex'],0,meta['count'],base)
            api.replay_present()
        api.replay_shutdown()
        status=json.loads(Path('native-world-status.json').read_text());assert not status['error'],status
        assert status['native_frames']==15 and status['changed_geometry_or_camera_frames']==14,status
        assert status['matched_draws']==15*len(rows)-1 and status['frames_with_partial_component']==1,status
        assert padding_mutations>0,'Capture does not exercise texture padding identity'
        a=np.asarray(Image.open('native-world-1.ppm')).astype(int);b=np.asarray(Image.open('native-world-15.ppm')).astype(int)
        changed=int((np.abs(a-b).max(2)>10).sum());assert changed>50000,changed
        reference=scratch/'exporter-reference.ppm'
        subprocess.run([str(renderer.parent/'dbz3-native-gpu.exe'),'--model',str(scene),'--shaders',str(renderer.parent/'materials.hlsl'),'--capture',str(reference)],check=True,capture_output=True)
        exported=np.asarray(Image.open(reference)).astype(int);error=float(np.abs(a-exported).mean());assert error<2,error
        psapi=C.WinDLL('psapi');modules=(C.c_void_p*1024)();needed=C.c_ulong();process=C.c_void_p(-1)
        psapi.EnumProcessModules.argtypes=[C.c_void_p,C.c_void_p,C.c_ulong,C.POINTER(C.c_ulong)];assert psapi.EnumProcessModules(process,modules,C.sizeof(modules),C.byref(needed))
        psapi.GetModuleFileNameExW.argtypes=[C.c_void_p,C.c_void_p,C.c_wchar_p,C.c_ulong];loaded=[]
        for module in modules[:needed.value//C.sizeof(C.c_void_p)]:
            path=C.create_unicode_buffer(4096);psapi.GetModuleFileNameExW(process,module,path,4096);loaded.append(path.value)
        assert not any('rexgpu' in s.lower() or 'xenia' in s.lower() or 'rexruntime' in s.lower() for s in loaded)
        for frame in (1,15):Image.open(f'native-world-{frame}.ppm').save(f'native-world-{frame}.png')
        result={'scope':'Actual CPU capture replay through native C++ bridge. Test-generated camera changes, texture padding mutations and one hidden draw. No reference GPU/runtime loaded. Not an original-frame pixel-fidelity test.','draws_per_frame':len(rows),'skinned_draws_per_frame':sum(v.get('kind')=='character' for v in report['draws']),'rigid_draws_per_frame':sum(v.get('kind')=='rigid' for v in report['draws']),'native_frames':15,'camera_update_frames':14,'ignored_texture_padding_mutations':padding_mutations,'per_frame_visibility_test':True,'changed_pixels':changed,'mean_pixel_error_against_python_conversion':error,'loaded_modules':loaded}
        Path('indexed-replay-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='loaded_modules'},indent=2))
    finally:api.replay_shutdown();k.VirtualFree(base,0,0x8000)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--bridge',required=True);p.add_argument('--trace',required=True);p.add_argument('--scene',required=True);p.add_argument('--renderer',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.bridge,a.trace,a.scene,a.renderer,a.scratch)

"""Replay different CPU scenes through the actual dynamic indexed frontend."""
import argparse,ctypes as C,hashlib,json,os,struct
from pathlib import Path
from PIL import Image
from export_live_ui import fnv


def run(bridge,renderer,registry,captures,scratch,wide_indices=False):
    bridge,renderer,registry,captures,scratch=map(lambda p:Path(p).resolve(),(bridge,renderer,registry,captures,scratch));scratch.mkdir(parents=True,exist_ok=True)
    os.environ.update(DBZ3_NATIVE_STREAM='1',DBZ3_NATIVE_RENDERER=str(renderer),DBZ3_NATIVE_SHADERS=str(renderer.parent/'materials.hlsl'),DBZ3_NATIVE_MATERIALS=str(registry));os.environ.pop('DBZ3_NATIVE_WORLD_SCENE',None)
    data=registry.read_bytes();count=struct.unpack_from('<I',data,8)[0];materials={}
    for i in range(count):v,p,vs,ps,kind=struct.unpack_from('<2Q3I',data,12+i*40);materials[v,p,vs,ps]=kind
    config=json.loads(captures.read_text());all_rows=[]
    for capture in config:
        trace=Path(capture['trace']);frame=capture['frame'];paths=[p for p in trace.glob(f'indexed-{frame}-*.json') if '-texture' not in p.name];paths.sort(key=lambda p:int(p.stem.rsplit('-',1)[1]));rows=[]
        for path in paths:
            prefix=path.with_suffix('')
            def binary(s):return Path(str(prefix)+s).read_bytes()
            meta=json.loads(path.read_text());device=bytearray(binary('-device.bin'));vc,pc=binary('-vs-program.bin'),binary('-ps-program.bin');kind=materials.get((fnv(vc),fnv(pc),len(vc),len(pc)))
            def put(o,v):struct.pack_into('>I',device,o,v)
            put(13072,0x100000);put(13068,0x110000);put(12812,0x140000)
            vs=bytearray(1024);ps=bytearray(128);struct.pack_into('>I',vs,32,0x120000);struct.pack_into('>I',vs,896,0);struct.pack_into('>2I',vs,872,0,len(vc));struct.pack_into('>I',ps,24,0x130000);struct.pack_into('>I',ps,64,0);struct.pack_into('>2I',ps,40,0,len(pc))
            ib=bytearray(32);source_indices=binary('-indices.bin')
            if wide_indices and meta['index_stride']==2 and len(rows)%2==0:
                values=struct.unpack('>'+str(meta['count'])+'H',source_indices)
                source_indices=struct.pack('>'+str(len(values))+'I',*(value+70000 for value in values))
                meta=dict(meta,index_flags=meta['index_flags']|0x80000000,index_stride=4,base_vertex=meta['base_vertex']-70000)
            struct.pack_into('>I',ib,0,meta['index_flags']);struct.pack_into('>I',ib,24,0xe1000000)
            writes=[(0x200000,device),(0x100000,vs),(0x110000,ps),(0x120000,vc),(0x130000,pc),(0x140000,ib),(0x100000000+meta['vertex_address'],binary('-vertices.bin')),(0x100000000+0x01001000,source_indices)]
            for slot in range(2 if kind in (1,2) else 1 if kind in (0,5) else 0):
                fetch=struct.unpack_from('>6I',device,0x480+slot*24);writes.append((0x100000000+(fetch[1]&0xfffff000),binary(f'-texture{slot}.bin')))
            if kind in (1,3,5,6,12):writes.append((0x100000000+(struct.unpack_from('>I',device,0x770)[0]&0x1ffffffc),binary('-palette.bin')))
            rows.append((meta,writes))
        assert rows,capture;all_rows.append(rows)
    os.chdir(scratch);k=C.WinDLL('kernel32');k.VirtualAlloc.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32,C.c_uint32];k.VirtualAlloc.restype=C.c_void_p;k.VirtualFree.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32]
    base=k.VirtualAlloc(None,0x120000000,0x2000,1);assert base;committed=set()
    def write(offset,data):
        for page in range(offset&~4095,(offset+len(data)+4095)&~4095,4096):
            if page not in committed:assert k.VirtualAlloc(base+page,4096,0x1000,4);committed.add(page)
        C.memmove(base+offset,bytes(data),len(data))
    api=C.CDLL(str(bridge));api.replay_stream_draw.argtypes=[C.c_uint32,C.c_uint32,C.c_int32,C.c_uint32,C.c_uint32,C.c_void_p];observed=accepted=0;reports=[];expected_rejections={}
    try:
        for frame in range(16):
            index=frame%len(all_rows);rows=all_rows[index]
            for meta,writes in rows:
                for offset,payload in writes:write(offset,payload)
                api.replay_stream_draw(0x200000,meta['primitive'],meta['base_vertex'],0,meta['count'],base)
            api.replay_stream_present();api.replay_stream_status();status=json.loads(Path('native-stream-status.json').read_text());assert not status['error'],status
            got=status['accepted_indexed_draws']-accepted;assert got==config[index]['admitted'],(frame,got,config[index],status)
            if 'rejections' in config[index]:
                for reason,count in config[index]['rejections'].items():expected_rejections[reason]=expected_rejections.get(reason,0)+count
                assert status['rejections']==expected_rejections,status
            assert status['observed_indexed_draws']-observed==len(rows),status
            api.replay_stream_capture.argtypes=[C.c_char_p];assert api.replay_stream_capture(b'current.ppm')
            reports.append({'frame':frame+1,'capture':index,'accepted':got,'rejected':len(rows)-got,'image_sha256':hashlib.sha256(Path('current.ppm').read_bytes()).hexdigest()});accepted=status['accepted_indexed_draws'];observed=status['observed_indexed_draws']
        assert status['native_frames']==16 and status['frames_with_indexed_draws']==16 and status['changed_pose_or_geometry_frames']>=15,status
        assert status['rejections']==expected_rejections if any('rejections' in row for row in config) else all(reason=='Separate postprocess target omitted' for reason in status['rejections']),status
        psapi=C.WinDLL('psapi');modules=(C.c_void_p*1024)();needed=C.c_ulong();process=C.c_void_p(-1);psapi.EnumProcessModules.argtypes=[C.c_void_p,C.c_void_p,C.c_ulong,C.POINTER(C.c_ulong)];assert psapi.EnumProcessModules(process,modules,C.sizeof(modules),C.byref(needed));psapi.GetModuleFileNameExW.argtypes=[C.c_void_p,C.c_void_p,C.c_wchar_p,C.c_ulong];loaded=[]
        for module in modules[:needed.value//C.sizeof(C.c_void_p)]:name=C.create_unicode_buffer(4096);psapi.GetModuleFileNameExW(process,module,name,4096);loaded.append(name.value)
        assert not any(any(word in name.lower() for word in ('rexgpu','rexruntime','xenia')) for name in loaded)
        for number in (1,15):Image.open(f'native-stream-{number}.ppm').save(f'native-stream-{number}.png')
        result={'scope':'Actual dynamic C++ guest frontend replay. Different CPU scene geometry, topology, palettes and compressed assets accepted without captured geometry manifests. Only known indexed material families and main/mask targets; remaining postprocess explicitly rejected. No reference runtime/GPU loaded.','mixed_32_bit_indices_above_65535':wide_indices,'captures':config,'frames':reports,'final_status':status,'loaded_modules':loaded};Path('dynamic-bridge-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k not in ('loaded_modules','frames')},indent=2))
    finally:api.replay_stream_shutdown();k.VirtualFree(base,0,0x8000)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--bridge',required=True);p.add_argument('--renderer',required=True);p.add_argument('--registry',required=True);p.add_argument('--captures',required=True);p.add_argument('--scratch',required=True);p.add_argument('--wide-indices',action='store_true');a=p.parse_args();run(a.bridge,a.renderer,a.registry,a.captures,a.scratch,a.wide_indices)

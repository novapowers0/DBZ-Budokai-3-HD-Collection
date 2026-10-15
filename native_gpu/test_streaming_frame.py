"""Exercise runtime geometry, palettes, pipelines, textures and scene changes."""
import argparse,ctypes as C,hashlib,json,struct,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from test_raster_targets import Raster,Clear
from test_resource_cache import Stats


class FrameDraw(C.Structure):
    _fields_=[('first_index',C.c_uint32),('index_count',C.c_uint32),('base_texture',C.c_uint32),('ramp_texture',C.c_uint32),('diffuse',C.c_float*3),('flags',C.c_uint32),('view_projection',C.c_float*16),('material',C.c_float*20),('raster',Raster),('palette',C.POINTER(C.c_float)),('palette_rows',C.c_uint32),('reserved',C.c_uint32)]


def load(path):
    data=Path(path).read_bytes();assert data[:8]==b'DBZNGPU4';vc,ic,dc,tc=struct.unpack_from('<4I',data,8);width,height=struct.unpack_from('<2I',data,48)
    clear=Clear.from_buffer_copy(data[56:84]);at=84
    vertices=np.frombuffer(data,dtype='<f4',count=vc*13,offset=at).reshape(vc,13).copy();at+=vc*52
    indices=np.frombuffer(data,dtype='<u4',count=ic,offset=at).copy();at+=ic*4
    records=[struct.unpack_from('<5I3f',data,at+i*32) for i in range(dc)];at+=dc*32
    draws=(FrameDraw*dc)()
    for i,d in enumerate(draws):
        d.first_index,d.index_count,d.base_texture,d.ramp_texture=records[i][:4];d.diffuse=(C.c_float*3)(*records[i][5:])
        values=struct.unpack_from('<36fI',data,at);d.view_projection=(C.c_float*16)(*values[:16]);d.material=(C.c_float*20)(*values[16:36]);d.flags=values[36];d.raster=Raster.from_buffer_copy(data[at+148:at+208]);at+=208
    palettes=[]
    for d in draws:
        rows=struct.unpack_from('<I',data,at)[0];at+=4;p=np.frombuffer(data,dtype='<f4',count=rows*4,offset=at).copy();at+=rows*16;palettes.append(p)
        d.palette_rows=rows;d.palette=p.ctypes.data_as(C.POINTER(C.c_float)) if rows else C.POINTER(C.c_float)()
    textures=[]
    for _ in range(tc):
        w,h,size=struct.unpack_from('<3I',data,at);at+=12;textures.append((w,h,data[at:at+size]));at+=size
    assert at==len(data)
    return {'vertices':vertices,'indices':indices,'draws':draws,'palettes':palettes,'textures':textures,'width':width,'height':height,'clear':clear}


def run(directory,scenes,scratch):
    directory=Path(directory).resolve();scenes=[Path(p).resolve() for p in scenes];scratch=Path(scratch).resolve();scratch.mkdir(parents=True,exist_ok=True)
    assert C.sizeof(FrameDraw)==256
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    for name,args in {'render':[C.c_void_p],'capture':[C.c_void_p,C.c_char_p],'destroy':[C.c_void_p],'get_stats':[C.c_void_p,C.POINTER(Stats)]}.items():getattr(api,'dbz_native_'+name).argtypes=args
    def check(value):assert value,api.dbz_native_last_error().decode()
    frames=[load(p) for p in scenes];w,h=frames[0]['width'],frames[0]['height'];assert all((f['width'],f['height'])==(w,h) for f in frames)
    shader=str(directory/'materials.hlsl').encode();handle=api.dbz_native_create_stream(None,w,h,shader);check(handle)
    texture_cache={};expected=[];passed=[]
    def stats():s=Stats();check(api.dbz_native_get_stats(handle,C.byref(s)));return {n:getattr(s,n) for n,_ in Stats._fields_}
    def set_frame(f):return api.dbz_native_set_frame(handle,f['vertices'].ctypes.data,len(f['vertices']),f['indices'].ctypes.data,len(f['indices']),f['draws'],len(f['draws']),C.byref(f['clear']))
    def capture(name):
        check(api.dbz_native_render(handle));p=scratch/(name+'.ppm');check(api.dbz_native_capture(handle,str(p).encode()));return np.array(Image.open(p))
    try:
        for i,(scene,f) in enumerate(zip(scenes,frames)):
            ids=[]
            for tw,th,rgba in f['textures']:
                key=(tw,th,hashlib.sha256(rgba).digest())
                if key not in texture_cache:
                    out=C.c_uint32();buf=C.create_string_buffer(rgba);check(api.dbz_native_add_texture(handle,tw,th,buf,len(rgba),C.byref(out)));texture_cache[key]=out.value
                ids.append(texture_cache[key])
            for d in f['draws']:
                if d.base_texture!=0xffffffff:d.base_texture=ids[d.base_texture]
                if d.ramp_texture!=0xffffffff:d.ramp_texture=ids[d.ramp_texture]
            if i:assert np.array_equal(capture(f'assets-added-{i}'),expected[-1]),'Registering new assets changed active frame'
            p=scratch/f'static-{i}.ppm';subprocess.run([str(directory/'dbz3-native-gpu.exe'),'--model',str(scene),'--shaders',str(directory/'materials.hlsl'),'--capture',str(p)],check=True,capture_output=True);expected.append(np.array(Image.open(p)))
            check(set_frame(f));assert np.array_equal(capture(f'stream-{i}'),expected[-1]),'Streaming/static pixels differ'
        passed+=['different game scenes, counts, topology, palettes, draw order and materials match static rendering exactly','new textures registered after rendering preserve the current frame']
        warmed=stats()
        for i in range(12):
            index=i%len(frames);check(set_frame(frames[index]));assert np.array_equal(capture(f'reuse-{i}'),expected[index]);current=stats()
            assert current['owned_objects']==warmed['owned_objects'] and current['pipeline_count']==warmed['pipeline_count'] and current['texture_uploads']==warmed['texture_uploads'],(warmed,current)
        passed+=['geometry/palette/descriptor capacity and cached textures/PSOs remain stable on repeated scene changes']
        f=frames[-1];check(set_frame(f));original=bytes(f['draws'][0]);d=f['draws'][0]
        def reject():assert not set_frame(f);assert np.array_equal(capture('rejected'),expected[-1]);C.memmove(C.addressof(d),original,C.sizeof(d))
        d.material[0]=float('nan');reject();d.base_texture=999999;reject();d.index_count=len(f['indices'])+3;reject();d.flags|=0x300000;reject();d.palette_rows=388;d.palette=C.POINTER(C.c_float)();reject();d.raster.scissor[0]=d.raster.scissor[2]+1;reject()
        old=f['indices'][0];f['indices'][0]=len(f['vertices']);assert not set_frame(f);f['indices'][0]=old;assert np.array_equal(capture('invalid-index'),expected[-1])
        old=f['clear'].flags;f['clear'].flags=8;assert not set_frame(f);f['clear'].flags=old;assert np.array_equal(capture('invalid-clear'),expected[-1]);passed+=['invalid frame inputs reject before changing the active frame']
        clear=Clear((C.c_float*4)(.125,.25,.5,1),1,0,7);check(api.dbz_native_set_frame(handle,None,0,None,0,None,0,C.byref(clear)));image=capture('empty');assert np.max(abs(image.astype(int)-np.array([32,64,128])))<=1
        check(set_frame(frames[0]));assert np.array_equal(capture('restored'),expected[0]);passed+=['empty frames remove old draws and scene rendering resumes afterward']
        assert not api.dbz_native_add_texture(handle,0,1,None,0,C.byref(C.c_uint32()))
        result={'scope':'Dynamic native D3D12 frame/resource API tested using CPU-exported game geometry. Renderer is created without a scene file; test fixture loader supplies typed native arrays. Does not yet replace the guest fixed-manifest bridge or implement remaining postprocessing.','scenes':[{'file':str(p),'draws':len(f['draws']),'vertices':len(f['vertices']),'indices':len(f['indices'])} for p,f in zip(scenes,frames)],'passed':passed,'warmed_stats':warmed,'different_pixels_against_static_paths':0}
        (scratch/'streaming-frame-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    finally:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scenes',nargs='+',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scenes,a.scratch)

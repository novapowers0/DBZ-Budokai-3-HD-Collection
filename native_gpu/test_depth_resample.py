"""GPU point resampling of independent D24 depth and all eight stencil bits."""
import argparse,ctypes as C,json
from pathlib import Path
import numpy as np
from PIL import Image
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear
from test_frame_operations import Operation
from test_resource_cache import Stats


def run(directory,scratch):
    directory,scratch=map(lambda p:Path(p).resolve(),(directory,scratch));scratch.mkdir(parents=True,exist_ok=True)
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_render_target.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(Clear),C.POINTER(C.c_uint32),C.POINTER(C.c_uint32)]
    api.dbz_native_add_snapshot_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_set_frame_operations.argtypes=[C.c_void_p,C.POINTER(Operation),C.c_uint32]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    for name,args in {'render':[C.c_void_p],'capture':[C.c_void_p,C.c_char_p],'destroy':[C.c_void_p],'get_stats':[C.c_void_p,C.POINTER(Stats)]}.items():getattr(api,'dbz_native_'+name).argtypes=args
    def check(value):assert value,api.dbz_native_last_error().decode()
    clear=Clear((C.c_float*4)(0,0,0,1),1,0,7);handle=api.dbz_native_create_stream(None,96,64,str(directory/'materials.hlsl').encode());check(handle)
    def draw(mode=3,texture=0xffffffff,w=96,h=64,target=0,flags=0xf0700):
        d=FrameDraw();d.index_count=6;d.base_texture=texture;d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=flags|(1<<22);d.view_projection=(C.c_float*16)(*np.eye(4,dtype=np.float32).reshape(-1));d.material[3]=mode;d.material[8:12]=[1]*4;d.material[4:6]=[1,1];d.raster.viewport=(C.c_float*6)(0,0,w,h,0,1);d.raster.scissor=(C.c_int32*4)(0,0,w,h);d.raster.alpha.function=7;d.raster.stencil.read_mask=255;d.raster.stencil.front[0]=d.raster.stencil.back[0]=7;d.reserved=target;return d
    def quad(left=-1,right=1,bottom=-1,top=1,z=.5,color=(1,1,1,1)):
        return np.array([[x,y,z,0,0,0,u,v,*color,1] for x,y,u,v in ((left,bottom,0,1),(left,top,0,0),(right,bottom,1,1),(right,top,1,0))],dtype='<f4')
    def frame(items):
        vv=[];ii=[];dd=[]
        for vertices,d in items:
            d=FrameDraw.from_buffer_copy(d);d.first_index=len(ii);ii.extend((np.array([0,1,2,2,1,3],dtype=np.uint32)+len(vv)).tolist());vv.extend(vertices);dd.append(d)
        vertices=np.array(vv,dtype='<f4').reshape(-1,13);indices=np.array(ii,dtype='<u4');draws=(FrameDraw*len(dd))(*dd);return api.dbz_native_set_frame(handle,vertices.ctypes.data,len(vertices),indices.ctypes.data,len(indices),draws,len(draws),C.byref(clear))
    def snapshot(w,h):
        out=C.c_uint32();check(api.dbz_native_add_snapshot_texture(handle,2,w,h,C.byref(out)));return out.value
    def capture(name):
        check(api.dbz_native_render(handle));path=scratch/(name+'.ppm');check(api.dbz_native_capture(handle,str(path).encode()));return np.array(Image.open(path))
    try:
        saved=snapshot(96,64);targets=[]
        for w,h in ((24,16),(32,24)):
            target=C.c_uint32();texture=C.c_uint32();check(api.dbz_native_add_render_target(handle,w,h,C.byref(clear),C.byref(target),C.byref(texture)));targets.append((w,h,target.value,texture.value,snapshot(w,h)))
        passed=[];warmed=None;values=set()
        for step in range(12):
            w,h,target,texture,small_saved=targets[step%2];items=[];stencil_source=np.empty((64,96),dtype=np.uint8);depth_source=np.empty((64,96),dtype=np.uint8)
            for y in range(8):
                for x in range(12):
                    reference=(y*12+x+step*17)&255;values.add(reference);depth=.25 if (x+y+step)%2==0 else .75;d=draw(flags=0xf0703);d.raster.stencil.enabled=1;d.raster.stencil.reference=reference;d.raster.stencil.write_mask=255;d.raster.stencil.front[3]=d.raster.stencil.back[3]=2;items.append((quad(-1+x/6,-1+(x+1)/6,1-(y+1)/4,1-y/4,depth),d));stencil_source[y*8:(y+1)*8,x*8:(x+1)*8]=reference;depth_source[y*8:(y+1)*8,x*8:(x+1)*8]=64 if depth==.25 else 191
            resample=draw(18,saved,w,h,target,0x703);resample.ramp_texture=saved;resample.material[5]=step%2;items.append((quad(),resample));operations=[Operation(96,2,0,saved,Clear()),Operation(97,2,target,small_saved,Clear())]
            for reference in range(256):
                d=draw(w=w,h=h,target=target);d.raster.stencil.enabled=1;d.raster.stencil.reference=reference;d.raster.stencil.front[0]=d.raster.stencil.back[0]=2;color=(reference/255,(255-reference)/255,((reference*17)&255)/255,1);items.append((quad(color=color),d))
            items.extend([(quad(-1,0),draw(15,texture)),(quad(0,1),draw(15,small_saved))]);check(frame(items));check(api.dbz_native_set_frame_operations(handle,(Operation*len(operations))(*operations),len(operations)));actual=capture('actual')
            sx=np.floor((np.arange(w)+.5)*96/w+3/2048).astype(int);sy=np.floor((np.arange(h)+.5)*64/h+3/2048).astype(int);small_stencil=stencil_source[sy[:,None],sx];small_depth=depth_source[sy[:,None],sx];xx=np.floor((np.arange(48)+.5)*w/48+3/2048).astype(int);yy=np.floor((np.arange(64)+.5)*h/64+3/2048).astype(int);sample=small_stencil[yy[:,None],xx].astype(int);expected=np.zeros((64,96,3),dtype=np.uint8);expected[:,:48]=np.stack([sample,255-sample,(sample*17)&255],axis=2);expected[:,48:,0]=small_depth[yy[:,None],xx];delta=abs(actual.astype(int)-expected.astype(int));assert delta.max()==0,(step,int(delta.max()),api.dbz_native_last_error().decode())
            stats=Stats();check(api.dbz_native_get_stats(handle,C.byref(stats)))
            if step==1:warmed=(stats.owned_objects,stats.pipeline_count,stats.texture_uploads)
            elif step>1:assert (stats.owned_objects,stats.pipeline_count,stats.texture_uploads)==warmed
            passed.append(dict(frame=step+1,destination=[w,h],different_pixels=0))
        assert values==set(range(256));before=actual.copy();invalid=FrameDraw.from_buffer_copy(resample);invalid.raster.viewport[2]-=1;bad=list(items);bad[96]=(quad(),invalid);assert not frame(bad);assert np.array_equal(capture('after-invalid-quad'),before)
        Image.fromarray(actual).save(scratch/'native.png');report=dict(scope='Own GPU geometry generates varying normalized D24 depth and every stencil byte value 0..255. Native point resampling into two smaller depth/stencil targets uses a depth pass and eight stencil mask passes; optional pixel stencil export is not required. Native stencil-equality rendering and depth sampling match independent CPU point sampling exactly. No original GPU data or CPU stencil/depth texture upload.',frames=passed,stencil_values_tested=len(values),stable_owned_objects=warmed[0],stable_pipeline_count=warmed[1],stable_texture_uploads=warmed[2],partial_quad_rejected_without_changing_frame=True);(scratch/'depth-resample-tests.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    finally:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

"""Native offscreen dependency chain, independent target depth and bounded reuse."""
import argparse,ctypes as C,json
from pathlib import Path
import numpy as np
from PIL import Image
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear
from test_resource_cache import Stats


def run(directory,scratch):
    directory,scratch=map(lambda p:Path(p).resolve(),(directory,scratch));scratch.mkdir(parents=True,exist_ok=True)
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_render_target.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(Clear),C.POINTER(C.c_uint32),C.POINTER(C.c_uint32)]
    api.dbz_native_add_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_update_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    for name,args in {'render':[C.c_void_p],'capture':[C.c_void_p,C.c_char_p],'destroy':[C.c_void_p],'get_stats':[C.c_void_p,C.POINTER(Stats)]}.items():getattr(api,'dbz_native_'+name).argtypes=args
    def check(result):assert result,api.dbz_native_last_error().decode()
    handles=[api.dbz_native_create_stream(None,96,64,str(directory/'materials.hlsl').encode()) for _ in range(2)]
    for handle in handles:check(handle)
    clear=Clear((C.c_float*4)(0,0,0,1),1,0,7);target_ids=[];texture_ids=[]
    def draw(mode,texture,w,h,target=0,depth=False):
        d=FrameDraw();d.index_count=6;d.base_texture=texture;d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=0xf0103 if depth else 0xf0700;d.view_projection=(C.c_float*16)(*np.eye(4,dtype=np.float32).reshape(-1));d.material[3]=mode;d.material[8:12]=[1]*4;d.reserved=target;d.raster.viewport=(C.c_float*6)(0,0,w,h,0,1);d.raster.scissor=(C.c_int32*4)(0,0,w,h);d.raster.alpha.function=7;d.raster.stencil.read_mask=d.raster.stencil.write_mask=255;d.raster.stencil.front[0]=d.raster.stencil.back[0]=7;return d
    def quad(left=-1,right=1,bottom=-1,top=1,z=.5,color=(1,1,1,1)):
        return np.array([[x,y,z,0,0,0,u,v,*color,1] for x,y,u,v in ((left,bottom,0,1),(left,top,0,0),(right,bottom,1,1),(right,top,1,0))],dtype='<f4')
    def frame(handle,items):
        vv=[];ii=[];dd=[]
        for vertices,d in items:
            d=FrameDraw.from_buffer_copy(d);d.first_index=len(ii);vv.extend(vertices);ii.extend((np.array([0,1,2,2,1,3],dtype=np.uint32)+len(vv)-4).tolist());dd.append(d)
        vertices=np.array(vv,dtype='<f4').reshape(-1,13);indices=np.array(ii,dtype='<u4');draws=(FrameDraw*len(dd))(*dd)
        return api.dbz_native_set_frame(handle,vertices.ctypes.data,len(vertices),indices.ctypes.data,len(indices),draws,len(draws),C.byref(clear))
    def capture(handle,name):
        check(api.dbz_native_render(handle));path=scratch/(name+'.ppm');check(api.dbz_native_capture(handle,str(path).encode()));return np.array(Image.open(path))
    try:
        for w,h in ((64,48),(32,24)):
            target=C.c_uint32();texture=C.c_uint32();check(api.dbz_native_add_render_target(handles[0],w,h,C.byref(clear),C.byref(target),C.byref(texture)));target_ids.append(target.value);texture_ids.append(texture.value)
        small=np.zeros((24,32,4),dtype=np.uint8);reference_texture=C.c_uint32();check(api.dbz_native_add_texture(handles[1],32,24,C.create_string_buffer(small.tobytes()),small.nbytes,C.byref(reference_texture)))
        marker=(quad(-.25,.25,-.25,.25,.1,(1,0,1,1)),draw(3,0xffffffff,96,64,depth=True));a=draw(3,0xffffffff,64,48,target_ids[0],True);b=draw(1,texture_ids[0],32,24,target_ids[1]);bad_depth=(quad(z=.6,color=(1,0,0,1)),a);main=draw(1,texture_ids[1],96,64,depth=True);reference=draw(1,reference_texture.value,96,64,depth=True);results=[];warmed=None
        for step in range(12):
            palette=np.array([[31+step*3,82,213,255],[221,133-step*2,47,255],[74,219,109+step*2,255]],dtype=np.uint8);pattern=[]
            for y in range(6):
                for x in range(8):
                    color=palette[(x+y+step)%3];pattern.append((quad(-1+x/4,-1+(x+1)/4,1-(y+1)/3,1-y/3,.3,color.astype(np.float32)/255),a));small[y*4:(y+1)*4,x*4:(x+1)*4]=color
            check(api.dbz_native_update_texture_blocks(handles[1],reference_texture.value,0,32,24,C.create_string_buffer(small.tobytes()),small.nbytes,0x688))
            empty=step==7;actual_items=[] if empty else [marker,*pattern,(quad(),b),bad_depth,(quad(),b),(quad(),main)];reference_items=[] if empty else [marker,(quad(),reference)]
            check(frame(handles[0],actual_items));actual=capture(handles[0],'actual');check(frame(handles[1],reference_items));expected=capture(handles[1],'expected');delta=abs(actual.astype(int)-expected.astype(int));assert delta.max()==0,(step,int(delta.max()))
            stats=Stats();check(api.dbz_native_get_stats(handles[0],C.byref(stats)))
            if step==1:warmed=(stats.owned_objects,stats.pipeline_count,stats.texture_uploads)
            elif step>1:assert (stats.owned_objects,stats.pipeline_count,stats.texture_uploads)==warmed
            results.append(dict(frame=step+1,draws=len(actual_items),different_pixels=0))
        original=actual.copy()
        invalid=FrameDraw.from_buffer_copy(main);invalid.reserved=999;assert not frame(handles[0],[(quad(),invalid)])
        assert not frame(handles[0],[(quad(),main)]),'A dependent target needs an earlier producer'
        feedback=FrameDraw.from_buffer_copy(b);feedback.reserved=target_ids[0];assert not frame(handles[0],[(quad(),feedback)]),'Sampling a bound color target must be rejected'
        assert not api.dbz_native_update_texture_blocks(handles[0],texture_ids[1],0,32,24,C.create_string_buffer(small.tobytes()),small.nbytes,0x688)
        assert np.array_equal(capture(handles[0],'after-invalid-input'),original)
        Image.fromarray(actual).save(scratch/'native.png');report=dict(scope='Two native RGBA8/D24S8 targets at different resolutions, produced and sampled entirely on GPU. Independent CPU checker image supplies only the separate expected renderer. Target A depth survives A->B->A; main depth survives all offscreen passes. Empty frames and changing geometry exercise bounded reuse. Invalid IDs, feedback, missing producers and CPU replacement of a GPU target are rejected without changing the prior frame.',frames=results,target_ids=target_ids,target_texture_ids=texture_ids,stable_owned_objects=warmed[0],stable_pipeline_count=warmed[1],stable_texture_uploads=warmed[2]);(scratch/'target-graph-tests.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    finally:
        for handle in handles:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

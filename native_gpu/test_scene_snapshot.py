"""Verify GPU-only scene copies before one or multiple postprocess draws."""
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
    api.dbz_native_add_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_update_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    api.dbz_native_render.argtypes=[C.c_void_p];api.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];api.dbz_native_destroy.argtypes=[C.c_void_p];api.dbz_native_get_stats.argtypes=[C.c_void_p,C.POINTER(Stats)]
    def check(value):assert value,api.dbz_native_last_error().decode()
    width,height=96,64;handles=[api.dbz_native_create_stream(None,width,height,str(directory/'materials.hlsl').encode()) for _ in range(2)]
    for handle in handles:check(handle)
    vertices=np.array([[x,y,0,0,0,0,(x+1)/2,(1-y)/2,1,1,1,1,1] for x,y in ((-1,-1),(-1,1),(1,-1),(1,1))],dtype='<f4');one=np.array([0,1,2,2,1,3],dtype='<u4');indices=np.tile(one,3)
    clear=Clear((C.c_float*4)(0,0,0,1),1,0,7)
    def draw(mode,texture,flags=0,first=0):
        d=FrameDraw();d.first_index=first;d.index_count=6;d.base_texture=texture;d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=0xf0700|flags;d.view_projection=(C.c_float*16)(*np.eye(4,dtype=np.float32).reshape(-1));d.material[3]=mode
        for k in range(8,12):d.material[k]=1
        if mode==14:d.material[4]=.5/width;d.material[5]=.5/height;d.material[12]=1/width;d.material[13]=1/height
        d.raster.viewport=(C.c_float*6)(0,0,width,height,0,1);d.raster.scissor=(C.c_int32*4)(0,0,width,height);d.raster.stencil.read_mask=d.raster.stencil.write_mask=255;d.raster.stencil.front[0]=d.raster.stencil.back[0]=7;d.raster.alpha.function=7;return d
    def set_frame(handle,draws):
        array=(FrameDraw*len(draws))(*draws);return api.dbz_native_set_frame(handle,vertices.ctypes.data,4,indices.ctypes.data,6*len(draws),array,len(draws),C.byref(clear))
    def capture(handle,name):
        check(api.dbz_native_render(handle));path=scratch/(name+'.ppm');check(api.dbz_native_capture(handle,str(path).encode()));return np.array(Image.open(path))
    def texture(handle,rgba):
        out=C.c_uint32();check(api.dbz_native_add_texture(handle,width,height,C.create_string_buffer(rgba.tobytes()),rgba.nbytes,C.byref(out)));return out.value
    def update(handle,index,rgba):check(api.dbz_native_update_texture_blocks(handle,index,0,width,height,C.create_string_buffer(rgba.tobytes()),rgba.nbytes,0x688))
    y,x=np.mgrid[:height,:width];rgba=np.stack([(x*37+y*19)%256,(x*11+y*31)%256,((x//5+y//7)%2)*255,np.full_like(x,255)],axis=2).astype(np.uint8)
    try:
        ids=[texture(handle,rgba) for handle in handles];second=texture(handles[1],rgba);base=draw(1,ids[0]);post=draw(14,0xffffffff,1<<24,6);post2=draw(14,0xffffffff,1<<24,12);reference=draw(14,ids[1]);passed=[];warmed=None
        for frame in range(12):
            current=np.roll(rgba,frame*5,axis=1);update(handles[0],ids[0],current);update(handles[1],ids[1],current)
            reference.base_texture=ids[1];check(set_frame(handles[1],[reference]));expected=capture(handles[1],'reference-first')
            double=frame%2==0
            if double:
                next_rgba=np.concatenate([expected,np.full((height,width,1),255,dtype=np.uint8)],axis=2);update(handles[1],second,next_rgba);reference.base_texture=second;check(set_frame(handles[1],[reference]));expected=capture(handles[1],'reference-second')
            check(set_frame(handles[0],[base,post,post2] if double else [base,post]));actual=capture(handles[0],'scene-snapshot');assert np.array_equal(actual,expected),(frame,int(abs(actual.astype(int)-expected.astype(int)).max()))
            if frame==0:assert np.count_nonzero(abs(actual.astype(int)-current[:,:,:3].astype(int)).max(2)>10)>1000
            stats=Stats();check(api.dbz_native_get_stats(handles[0],C.byref(stats)))
            if frame==1:warmed=(stats.owned_objects,stats.pipeline_count)
            elif frame>1:assert (stats.owned_objects,stats.pipeline_count)==warmed
            passed.append(dict(frame=frame+1,postprocess_draws=2 if double else 1,different_pixels=0))
        original=capture(handles[0],'before-rejection');bad=FrameDraw.from_buffer_copy(post);bad.flags|=1<<20;assert not set_frame(handles[0],[base,bad]);assert np.array_equal(capture(handles[0],'after-rejection'),original)
        bad=FrameDraw.from_buffer_copy(post);bad.flags|=1<<21;assert not set_frame(handles[0],[base,bad]);assert np.array_equal(capture(handles[0],'after-rejection-mask'),original)
        Image.fromarray(actual).save(scratch/'native.png');result=dict(scope='Native D3D12 GPU-only scene-color snapshot. A source image is first drawn by native GPU geometry, then copied and sampled by one/two native FXAA passes. Results exactly match separate immutable-input shader renders. No reference runtime, game textures or reference framebuffer used.',frames=passed,stable_owned_objects=warmed[0],stable_pipeline_count=warmed[1],invalid_target_or_mask_combinations_preserve_prior_frame=True)
        (scratch/'scene-snapshot-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    finally:
        for handle in handles:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

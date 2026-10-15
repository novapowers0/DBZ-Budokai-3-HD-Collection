"""Check homogeneous toon-camera normalization against an independent equation."""
import argparse,ctypes as C,json
from pathlib import Path
import numpy as np
from PIL import Image
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear


def run(directory,scratch):
    directory,scratch=map(lambda p:Path(p).resolve(),(directory,scratch));scratch.mkdir(parents=True,exist_ok=True)
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    api.dbz_native_render.argtypes=[C.c_void_p];api.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];api.dbz_native_destroy.argtypes=[C.c_void_p]
    def check(value):assert value,api.dbz_native_last_error().decode()
    size=96;handle=api.dbz_native_create_stream(None,size,size,str(directory/'materials.hlsl').encode());check(handle)
    vertices=np.array([[x,y,0,0,0,1,0,0,0,0,0,0,1] for x,y in ((-1,-1),(-1,1),(1,-1),(1,1))],dtype='<f4');indices=np.array([0,1,2,2,1,3],dtype='<u4')
    d=FrameDraw();d.index_count=6;d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(.5,1,.99);d.view_projection=(C.c_float*16)(*np.eye(4,dtype=np.float32).reshape(-1));d.raster.viewport=(C.c_float*6)(0,0,size,size,0,1);d.raster.scissor=(C.c_int32*4)(0,0,size,size);d.raster.stencil.front[0]=d.raster.stencil.back[0]=7;d.raster.stencil.read_mask=d.raster.stencil.write_mask=255;d.raster.alpha.function=7
    d.material[3]=5;d.material[10]=d.material[11]=1;d.material[14]=.2;d.material[15]=.5
    clear=Clear((C.c_float*4)(0,0,0,0),1,0,7);result=[]
    y,x=np.mgrid[:size,:size];xx=(x+.5)*2/size-1;yy=1-(y+.5)*2/size
    def expected(w):
        edge=1-abs(-.2/np.sqrt(xx*xx+yy*yy+.04+(1-w)**2));square=edge*edge;rim=.5*square+(square**3>=.5)
        return np.rint(np.minimum(rim,1)*255).astype(np.int32)
    try:
        texture=C.c_uint32();check(api.dbz_native_add_texture(handle,1,1,C.create_string_buffer(bytes([0,0,0,255])),4,C.byref(texture)));d.base_texture=texture.value
        images={}
        for enabled,w in ((True,0),(True,.25),(True,1),(True,2),(True,-1),(False,0),(False,2)):
            d.flags=0xf0700|(1<<22)|((1<<23) if enabled else 0);d.material[19]=w
            check(api.dbz_native_set_frame(handle,vertices.ctypes.data,4,indices.ctypes.data,6,C.byref(d),1,C.byref(clear)));check(api.dbz_native_render(handle));path=scratch/f'camera-{int(enabled)}-{w}.ppm';check(api.dbz_native_capture(handle,str(path).encode()));actual=np.array(Image.open(path)).astype(int)
            target=np.repeat(expected(w if enabled else 1)[:,:,None],3,axis=2);delta=abs(actual-target);assert delta.max()<=1,(enabled,w,int(delta.max()))
            images[enabled,w]=actual;result.append(dict(explicit_homogeneous_camera=enabled,camera_w=w,max_error=int(delta.max()),mean_error=float(delta.mean())))
        assert np.count_nonzero(abs(images[True,0]-images[True,1]).max(2)>10)>1000
        assert np.array_equal(images[False,0],images[True,1]) and np.array_equal(images[False,2],images[True,1])
        report=dict(scope='Native toon pixel material normalizes the full four-component world-minus-camera difference, as independently established from source material instructions. A synthetic full-screen surface checks actual D3D12 pixels against the mathematical equation. Legacy states retain camera w=1.',cases=result)
        (scratch/'toon-camera-tests.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    finally:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

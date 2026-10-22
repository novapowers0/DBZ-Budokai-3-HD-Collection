"""Native D24 bilinear depth in DOF versus independently interpolated CPU depth."""
import argparse,ctypes as C,json
from pathlib import Path
import numpy as np
from PIL import Image
from test_streaming_frame import FrameDraw
from test_frame_operations import Operation
from test_raster_targets import Clear


def run(directory,scratch):
    directory,scratch=map(lambda p:Path(p).resolve(),(directory,scratch));scratch.mkdir(parents=True,exist_ok=True)
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_render_target.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(Clear),C.POINTER(C.c_uint32),C.POINTER(C.c_uint32)]
    api.dbz_native_add_snapshot_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_add_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    api.dbz_native_set_frame_operations.argtypes=[C.c_void_p,C.POINTER(Operation),C.c_uint32]
    api.dbz_native_render.argtypes=[C.c_void_p];api.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];api.dbz_native_destroy.argtypes=[C.c_void_p]
    def check(value):assert value,api.dbz_native_last_error().decode()
    width,height=256,128;handle=api.dbz_native_create_stream(None,width,height,str(directory/'materials.hlsl').encode());check(handle)
    clear=Clear((C.c_float*4)(0,0,0,1),1,0,7)
    def draw(mode=3,texture=0xffffffff,ramp=0xffffffff,w=width,h=height):
        d=FrameDraw();d.index_count=6;d.base_texture=texture;d.ramp_texture=ramp;d.diffuse=(C.c_float*3)(1,1,1);d.flags=0xf0700;d.material[3]=mode;d.material[8:12]=[1]*4;d.view_projection=(C.c_float*16)(*np.eye(4,dtype='<f4').reshape(-1));d.raster.viewport=(C.c_float*6)(0,0,w,h,0,1);d.raster.scissor=(C.c_int32*4)(0,0,w,h);d.raster.alpha.function=7;d.raster.stencil.read_mask=d.raster.stencil.write_mask=255;d.raster.stencil.front[0]=d.raster.stencil.back[0]=7;return d
    def quad(left=-1,right=1,top=1,bottom=-1,z=.5):return np.array([[x,y,z,0,0,0,u,v,1,1,1,1,1] for x,y,u,v in ((left,bottom,0,1),(left,top,0,0),(right,bottom,1,1),(right,top,1,0))],dtype='<f4')
    def texture(fmt,pixels):
        value=C.c_uint32();pixels=np.ascontiguousarray(pixels);check(api.dbz_native_add_texture_blocks(handle,fmt,pixels.shape[1],pixels.shape[0],pixels.ctypes.data,pixels.nbytes,0x688,C.byref(value)));return value.value
    def frame(items,name,operations=()):
        vv=[];ii=[];dd=[]
        for vertices,d in items:
            d=FrameDraw.from_buffer_copy(d);d.first_index=len(ii);ii.extend((np.array([0,1,2,2,1,3])+len(vv)).tolist());vv.extend(vertices);dd.append(d)
        vertices=np.array(vv,dtype='<f4');indices=np.array(ii,dtype='<u4');draws=(FrameDraw*len(dd))(*dd)
        check(api.dbz_native_set_frame(handle,vertices.ctypes.data,len(vertices),indices.ctypes.data,len(indices),draws,len(draws),C.byref(clear)))
        check(api.dbz_native_set_frame_operations(handle,(Operation*len(operations))(*operations),len(operations)));check(api.dbz_native_render(handle));path=scratch/(name+'.ppm');check(api.dbz_native_capture(handle,str(path).encode()));return np.array(Image.open(path))
    try:
        target=C.c_uint32();unused=C.c_uint32();check(api.dbz_native_add_render_target(handle,2,2,C.byref(clear),C.byref(target),C.byref(unused)))
        snapshot=C.c_uint32();check(api.dbz_native_add_snapshot_texture(handle,2,2,2,C.byref(snapshot)))
        y,x=np.indices((height,width));colors=np.stack([((x//2+y//3)&1)*255,(x%13)*19,(y%11)*23,np.full_like(x,255)],2).astype(np.uint8);color_id=texture(0,colors)
        depths=np.array([[.2,.8],[.4,.6]],dtype=np.float32);stored=(np.rint(depths.astype(float)*0xffffff)/0xffffff).astype(np.float32)
        tx=2*(x+.5)/width-.5+3/2048;ty=2*(y+.5)/height-.5+3/2048;ix=np.floor(tx).astype(int);iy=np.floor(ty).astype(int)
        fx=np.rint((tx-ix)*256)/256;fy=np.rint((ty-iy)*256)/256
        at=lambda dx,dy:stored[np.clip(iy+dy,0,1),np.clip(ix+dx,0,1)]
        interpolated=((at(0,0)*(1-fx)+at(1,0)*fx)*(1-fy)+(at(0,1)*(1-fx)+at(1,1)*fx)*fy).astype('<f4');expected_id=texture(6,interpolated)
        palette=np.array([(1,100,0,6),(.1111111119389534,0,-2,0),(.0020833334419876337,-.004166666883975267,.004166666883975267,-.0020833334419876337),(.0011718750465661287,-.0011718750465661287,-.0023437500931322575,.0023437500931322575)],dtype='<f4')
        effect=draw(17,color_id,snapshot.value);effect.material[5]=1;effect.material[7]=1;effect.palette_rows=4;effect.palette=palette.ctypes.data_as(C.POINTER(C.c_float))
        items=[]
        for row in range(2):
            for column in range(2):
                depth_draw=draw(w=2,h=2);depth_draw.flags=0x703;depth_draw.reserved=target.value
                items.append((quad(-1+column,column,1-row,-row,float(depths[row,column])),depth_draw))
        items.append((quad(),effect));actual=frame(items,'linear-native-d24',[Operation(4,2,target.value,snapshot.value,Clear())])
        reference=FrameDraw.from_buffer_copy(effect);reference.material[7]=0;reference.ramp_texture=expected_id;expected=frame([(quad(),reference)],'cpu-bilinear-depth-point-sampled')
        point=FrameDraw.from_buffer_copy(effect);point.material[7]=0;wrong=frame([(quad(),point)],'wrong-point-depth')
        delta=abs(actual.astype(int)-expected.astype(int));distinction=abs(actual.astype(int)-wrong.astype(int));assert delta.max()<=2,int(delta.max());assert distinction.max()>=5,int(distinction.max())
        result=dict(scope='Own GPU 2x2 D24 depth target with four distinct depths, saved natively and sampled bilinearly by DOF. Independent CPU UNORM24/bilinear depth resampled into a dense float texture produces equivalent pixels through the point-depth path. A deliberately wrong point-depth path must visibly differ.',max_pixel_error=int(delta.max()),mean_pixel_error=float(delta.mean()),wrong_point_max_difference=int(distinction.max()),original_depth_pixels_supplied=False)
        (scratch/'dof-depth-filter-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    finally:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

"""Independent original-game pixel oracle for the native YUV material."""
import argparse,ctypes as C,json
from pathlib import Path
import numpy as np
from PIL import Image
from export_live_ui import geometry
from live_raster import encode,pipeline
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear,Raster


def run(directory,proof,reference,scratch):
    directory,proof,reference,scratch=map(lambda p:Path(p).resolve(),(directory,proof,reference,scratch));scratch.mkdir(parents=True,exist_ok=True);proof=json.loads(proof.read_text());prefix=Path(proof['prefix'])
    device=Path(str(prefix)+'-device.bin').read_bytes();raw=Path(str(prefix)+'-vertices.bin').read_bytes();v,m,control=geometry(raw,device);control[2]=0;control[3]=13;control[4:8]=0;control[16:20]=0;v[:,-1]=1
    # The original inputs are CPU-decoded source Y/Cb/Cr images, recovered from
    # read-only texture resources. No original RGB render target is an input.
    planes=[np.array(Image.open(path))[:,:,0].copy() for path in proof['texture_files']];luma=planes[0].tobytes();chroma=np.stack(planes[1:],axis=2).tobytes()
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    api.dbz_native_render.argtypes=[C.c_void_p];api.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];api.dbz_native_destroy.argtypes=[C.c_void_p]
    def check(value):assert value,api.dbz_native_last_error().decode()
    handle=api.dbz_native_create_stream(None,1280,720,str(directory/'materials.hlsl').encode());check(handle)
    try:
        textures=[]
        for fmt,plane,data in ((4,planes[0],luma),(5,planes[1],chroma)):
            out=C.c_uint32();check(api.dbz_native_add_texture_blocks(handle,fmt,plane.shape[1],plane.shape[0],C.create_string_buffer(data),len(data),0x688,C.byref(out)));textures.append(out.value)
        draw=FrameDraw();draw.index_count=len(v);draw.base_texture,draw.ramp_texture=textures;draw.diffuse=(C.c_float*3)(1,1,1);draw.flags=pipeline(device);draw.view_projection=(C.c_float*16)(*m.reshape(-1));draw.material=(C.c_float*20)(*control);draw.raster=Raster.from_buffer_copy(encode(device));indices=np.arange(len(v),dtype='<u4');clear=Clear((C.c_float*4)(0,0,0,1),1,1,7)
        check(api.dbz_native_set_frame(handle,v.ctypes.data,len(v),indices.ctypes.data,len(indices),C.byref(draw),1,C.byref(clear)));check(api.dbz_native_render(handle));path=scratch/'native.ppm';check(api.dbz_native_capture(handle,str(path).encode()));actual=np.array(Image.open(path)).astype(int);expected=np.array(Image.open(reference))[:720,:1280,:3].astype(int)
        projected=np.column_stack([v[:,:3],np.ones(len(v))])@m.T;ndc=projected[:,:3]/projected[:,3,None];vp=draw.raster.viewport
        xx=vp[0]+(ndc[:,0]+1)*vp[2]/2;yy=vp[1]+(1-ndc[:,1])*vp[3]/2
        left=max(0,int(np.ceil(xx.min())));right=min(1280,int(np.floor(xx.max())));top=max(0,int(np.ceil(yy.min())));bottom=min(720,int(np.floor(yy.max())))
        delta=abs(actual-expected);roi=delta[top:bottom,left:right];assert roi.size;metrics=dict(max_error=int(roi.max()),mean_error=float(roi.mean()),pixels_above_2=int((roi.max(2)>2).sum()))
        Image.fromarray(actual.astype(np.uint8)).save(scratch/'native.png');Image.fromarray(np.minimum(delta*8,255).astype(np.uint8)).save(scratch/'difference.png')
        report=dict(scope='Native HLSL YUV conversion versus actual original-game pixel shader output, with identical source video planes and independently verified CPU screen geometry. Y/Cb/Cr read-only source textures recovered from the reference capture supply this isolated oracle. No RGB framebuffer input, translated shader or reference runtime in native rendering. Separate live frontend test supplies current CPU video buffers; this oracle does not equate asynchronous CPU/reference video frame timing.',video_rectangle=[left,top,right,bottom],source_plane_dimensions=[list(p.shape[::-1]) for p in planes],pixel_metrics=metrics,full_viewport_mean_error=float(delta.mean()),cpu_geometry_errors={key:proof[key] for key in ('clip_max_error','uv_max_error','color_max_error')})
        (scratch/'video-pixel-oracle.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2));assert not metrics['pixels_above_2'],report
    finally:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--proof',required=True);p.add_argument('--reference',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.proof,a.reference,a.scratch)

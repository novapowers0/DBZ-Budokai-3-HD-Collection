"""Original GPU oracle for native image-copy and weighted 13-tap blur shaders."""
import argparse,ctypes as C,json
from pathlib import Path
import numpy as np
from PIL import Image
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear


def run(directory,frame,oracles,scratch):
    directory,frame,oracles,scratch=map(lambda p:Path(p).resolve(),(directory,frame,oracles,scratch));scratch.mkdir(parents=True,exist_ok=True)
    rows={r['event']:r for r in json.loads(frame.read_text())['draws']};proof={r['event']:r for r in json.loads((oracles/'oracles.json').read_text())};folder=frame.parent;passed=[]
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_add_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    api.dbz_native_render.argtypes=[C.c_void_p];api.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];api.dbz_native_destroy.argtypes=[C.c_void_p]
    def check(value):assert value,api.dbz_native_last_error().decode()
    for event in (410,470,915,945,998,1028,1081,1111,1179,1317):
        row=rows[event];p=proof[event];assert not row['blend']['enabled'];post=np.frombuffer((folder/f'e{event}-postvs.bin').read_bytes(),dtype='<f4').reshape(-1,8)
        if len(post)==3:post=np.concatenate([post,(post[1]+post[2]-post[0])[None,:]],axis=0);order=[0,1,2,2,1,3]
        else:assert len(post)==4;order=[0,1,2,2,1,3]
        vp=row['viewport'];xx=vp['x']+(post[:,0]/post[:,3]+1)*vp['width']/2;yy=vp['y']+(1-post[:,1]/post[:,3])*vp['height']/2;w,h=round(float(xx.max())),round(float(yy.max()));v=np.zeros((4,13),dtype='<f4');v[:,:3]=post[:,:3];v[:,6:8]=post[:,4:6];v[:,8:12]=1;v[:,12]=post[:,3];indices=np.array(order,dtype='<u4')
        expected=np.array(Image.open(oracles/f'e{event}-output.png').convert('RGB'))[:h,:w];source=p['inputs'][0];swizzle=source['descriptor']['swizzle'];swizzle=[swizzle[k] for k in ('red','green','blue','alpha')];image=np.array(Image.open(oracles/source['file']).convert('RGBA'))[:,:,swizzle].copy();stage=row['stages']['ShaderStage.Pixel'];blur='555EBAAE' in stage['dump_name'];dof='4DFCBC49' in stage['dump_name'];palette=None
        fetch=np.frombuffer((folder/next(cb['file'] for cb in stage['constants'] if cb['name']=='xe_fetch_cbuffer')).read_bytes(),dtype='<u4');f0,f3=int(fetch[0]),int(fetch[3]);handle=api.dbz_native_create_stream(None,w,h,str(directory/'materials.hlsl').encode());check(handle)
        try:
            texture=C.c_uint32();check(api.dbz_native_add_texture(handle,image.shape[1],image.shape[0],C.create_string_buffer(image.tobytes()),image.nbytes,C.byref(texture)))
            d=FrameDraw();d.index_count=6;d.base_texture=texture.value;d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=0xf0700|(1<<22);d.view_projection=(C.c_float*16)(*np.eye(4,dtype=np.float32).reshape(-1));d.material[3]=16 if blur else 15;d.material[4]=float((f3>>19&3)==0);d.material[5]=float((f0>>10&7)==2);d.material[8:12]=[1]*4;d.raster.viewport=(C.c_float*6)(vp['x'],vp['y'],vp['width'],vp['height'],vp['minDepth'],vp['maxDepth']);d.raster.scissor=(C.c_int32*4)(0,0,w,h);d.raster.alpha.function=7;d.raster.stencil.read_mask=d.raster.stencil.write_mask=255;d.raster.stencil.front[0]=d.raster.stencil.back[0]=7
            if blur or dof:
                palette=np.frombuffer((folder/next(cb['file'] for cb in stage['constants'] if cb['name']=='xe_float_cbuffer')).read_bytes(),dtype='<f4').copy();assert len(palette)==(16 if dof else 52);d.palette_rows=4 if dof else 13;d.palette=palette.ctypes.data_as(C.POINTER(C.c_float))
            if dof:
                d.material[3]=17;depth_source=p['inputs'][1];dw,dh=depth_source['texture']['width'],depth_source['texture']['height'];depth=(oracles/Path(depth_source['file']).with_suffix('.bin')).read_bytes();assert len(depth)==dw*dh*4;depth_id=C.c_uint32();check(api.dbz_native_add_texture_blocks(handle,6,dw,dh,C.create_string_buffer(depth),len(depth),0x688,C.byref(depth_id)));d.ramp_texture=depth_id.value
            clear=Clear((C.c_float*4)(0,0,0,1),1,0,7);check(api.dbz_native_set_frame(handle,v.ctypes.data,4,indices.ctypes.data,6,C.byref(d),1,C.byref(clear)));check(api.dbz_native_render(handle));path=scratch/f'e{event}.ppm';check(api.dbz_native_capture(handle,str(path).encode()));actual=np.array(Image.open(path));delta=abs(actual.astype(int)-expected.astype(int));Image.fromarray(np.minimum(delta*8,255).astype(np.uint8)).save(scratch/f'e{event}-difference.png');metrics=dict(event=event,material='depth-of-field9' if dof else 'gaussian13' if blur else 'image-copy',output_dimensions=[w,h],max_error=int(delta.max()),mean_error=float(delta.mean()),different_pixels=int((delta.max(2)>0).sum()),pixels_above_2=int((delta.max(2)>2).sum()));passed.append(metrics)
        finally:api.dbz_native_destroy(handle)
    result=dict(scope='Isolated native HLSL image-copy, 13-tap weighted Gaussian blur and 9-tap depth-of-field versus exact-event original GPU output, at three effect resolutions. Read-only color and raw float depth inputs and post-VS geometry are oracle-only fixtures. Production rendering uses its own GPU targets/snapshots; upstream native scene fidelity and effect graph are separate.',cases=passed);(scratch/'post-pixel-oracles.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2));assert not any(r['pixels_above_2'] for r in passed),result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--frame',required=True);p.add_argument('--oracles',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.frame,a.oracles,a.scratch)

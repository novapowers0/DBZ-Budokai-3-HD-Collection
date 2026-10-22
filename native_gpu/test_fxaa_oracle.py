"""Isolated native FXAA pixel shader versus exact-event original GPU output."""
import argparse,ctypes as C,json,struct
from pathlib import Path
import numpy as np
from PIL import Image
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear


def run(directory,frame,oracle,scratch):
    directory,frame,oracle,scratch=map(lambda p:Path(p).resolve(),(directory,frame,oracle,scratch));scratch.mkdir(parents=True,exist_ok=True)
    proof=json.loads((oracle/'oracle.json').read_text());row=next(r for r in json.loads(frame.read_text())['draws'] if r['event']==proof['event']);folder=frame.parent
    original=np.array(Image.open(oracle/'output.png').convert('RGB'))
    source=np.array(Image.open(oracle/'input.png').convert('RGBA'))[:,:,proof['input_swizzle']].copy()
    post=np.frombuffer((folder/f"e{row['event']}-postvs.bin").read_bytes(),dtype='<f4').reshape(-1,8);assert len(post)==3 and np.all(post[:,3]==1)
    quad=np.concatenate([post,(post[1]+post[2]-post[0])[None,:]],axis=0);vertices=np.zeros((4,13),dtype='<f4');vertices[:,:3]=quad[:,:3];vertices[:,6:8]=quad[:,4:6];vertices[:,8:]=1;indices=np.array([0,1,2,2,1,3],dtype='<u4')
    vp=row['viewport'];screen_x=vp['x']+(quad[:,0].astype(np.float64)+1)*vp['width']/2;screen_y=vp['y']+(1-quad[:,1].astype(np.float64))*vp['height']/2;width=round(screen_x.max());height=round(screen_y.max());expected=original[:height,:width];tile_width=width
    constants={}
    for stage in ('Vertex','Pixel'):
        entry=row['stages']['ShaderStage.'+stage];path=folder/next(c['file'] for c in entry['constants'] if c['name']=='xe_float_cbuffer');constants[stage]=np.frombuffer(path.read_bytes(),dtype='<f4').reshape(-1,4)
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    api.dbz_native_render.argtypes=[C.c_void_p];api.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];api.dbz_native_destroy.argtypes=[C.c_void_p]
    def check(value):assert value,api.dbz_native_last_error().decode()
    handle=api.dbz_native_create_stream(None,tile_width,height,str(directory/'materials.hlsl').encode());check(handle)
    try:
        texture=C.c_uint32();check(api.dbz_native_add_texture(handle,source.shape[1],source.shape[0],C.create_string_buffer(source.tobytes()),source.nbytes,C.byref(texture)))
        d=FrameDraw();d.index_count=6;d.base_texture=texture.value;d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=0xf0700|(1<<22);d.view_projection=(C.c_float*16)(*np.eye(4,dtype=np.float32).reshape(-1));d.material[3]=14;d.material[4:6]=constants['Vertex'][0,:2];d.material[6]=1;d.material[8:12]=[1]*4;d.material[12:14]=constants['Pixel'][0,:2]
        d.raster.scissor=(C.c_int32*4)(0,0,tile_width,height);d.raster.alpha.function=7;d.raster.stencil.read_mask=d.raster.stencil.write_mask=255;d.raster.stencil.front[0]=d.raster.stencil.back[0]=7
        clear=Clear((C.c_float*4)(0,0,0,1),1,0,7);tiles=[]
        # The original host target includes unused rows beyond the FXAA draw.
        # Tile the affected area, preserving the original screen sample positions.
        for x in range(0,width,tile_width):
            d.raster.viewport=(C.c_float*6)(vp['x']-x,vp['y'],vp['width'],vp['height'],vp['minDepth'],vp['maxDepth']);check(api.dbz_native_set_frame(handle,vertices.ctypes.data,4,indices.ctypes.data,6,C.byref(d),1,C.byref(clear)));check(api.dbz_native_render(handle));path=scratch/f'native-{x}.ppm';check(api.dbz_native_capture(handle,str(path).encode()));tiles.append(np.array(Image.open(path))[:,:min(tile_width,width-x)])
        actual=np.concatenate(tiles,axis=1);delta=abs(actual.astype(int)-expected.astype(int))
        Image.fromarray(actual).save(scratch/'native.png');Image.fromarray(np.minimum(delta*8,255).astype(np.uint8)).save(scratch/'difference.png')
        result=dict(scope='Isolated native FXAA HLSL versus original game GPU output at the same captured event. Reference read-only color texture and post-VS geometry are test fixtures only. Production frontend is tested separately using its own GPU snapshot with original scene memory unmapped. This does not prove native upstream scene fidelity.',event=proof['event'],input_dimensions=list(source.shape[1::-1]),output_dimensions=[width,height],max_error=int(delta.max()),mean_error=float(delta.mean()),pixels_above_2=int((delta.max(2)>2).sum()),different_pixels=int((delta.max(2)>0).sum()))
        (scratch/'fxaa-pixel-oracle.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2));assert not result['pixels_above_2'],result
    finally:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--frame',required=True);p.add_argument('--oracle',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.frame,a.oracle,a.scratch)

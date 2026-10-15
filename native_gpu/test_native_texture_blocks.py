"""Native GPU BC decoding/swizzle checks; no CPU decompression in renderer."""
import argparse,ctypes as C,json,struct
from pathlib import Path
import numpy as np
from PIL import Image
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear,Raster
from test_resource_cache import Stats
from texture_decode import linearize,decode

SPEC='https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#BlockCompression'


def allowance(data,fmt,w,h,swizzle):
    # D3D functional spec 19.5.2: absolute error +0.03*endpoint range.
    # BC1/2/3 absolute error is 1/255. Add 1 for the integer CPU oracle's
    # interpolation rounding and 0.5 for RGBA8 output quantization. This is a
    # block/channel bound, not an arbitrary global image tolerance.
    if fmt in (0,4,5):return np.full((h,w,4),.5)
    out=np.empty((h,w,4));size=8 if fmt==1 else 16
    for by in range((h+3)//4):
        for bx in range((w+3)//4):
            block=data[(by*((w+3)//4)+bx)*size:][:size];a,b=struct.unpack_from('<HH',block,size-8)
            raw=np.array([[(v>>11)&31,(v>>5)&63,v&31] for v in (a,b)],dtype=float)
            promoted=np.array([[(int(c)<<3)|(int(c)>>2) if i!=1 else (int(c)<<2)|(int(c)>>4) for i,c in enumerate(row)] for row in raw])
            endpoint=np.maximum(abs(promoted[0]-promoted[1]),abs((raw[0]-raw[1])*255/np.array([31,63,31])))
            bound=np.array([*(2.5+.03*endpoint),.5 if fmt==1 else 1.5 if fmt==2 else 2.5+.03*abs(block[0]-block[1])])
            select=[swizzle>>(3*i)&7 for i in range(4)];bound=np.array([bound[i] if i<4 else .5 for i in select]);out[by*4:min(h,by*4+4),bx*4:min(w,bx*4+4)]=bound
    return out


def run(directory,trace,scratch):
    directory,trace,scratch=map(lambda p:Path(p).resolve(),(directory,trace,scratch));scratch.mkdir(parents=True,exist_ok=True)
    fixtures=[]
    # Actual assets exercise tiled/endian/packed conversion before native BC upload.
    seen=set()
    for p in trace.glob('*-texture0.json'):
        try:
            words=json.loads(p.read_text());raw=p.with_suffix('.bin').read_bytes();data,desc=linearize(raw,words)
        except (ValueError,OSError):continue
        fmt=desc['format']
        if fmt in seen:continue
        seen.add(fmt);image,_=decode(raw,words);swizzle=desc['swizzle']
        if fmt==2:swizzle=sum((0 if (swizzle>>(3*c)&7)<4 else swizzle>>(3*c)&7)<<(3*c) for c in range(4))
        fixtures.append((p.stem,{2:4,6:0,18:1,19:2,20:3}[fmt],desc['width'],desc['height'],data,swizzle,np.array(image)))
        if len(seen)==5:break
    # Known BC2 alpha nibbles and BC1 punch-through, independent expected pixels.
    color=struct.pack('<HHI',0xf800,0x07e0,0)
    fixtures.append(('bc2-alpha',2,4,4,bytes.fromhex('1032547698badcfe')+color,0x688,np.array([[[255,0,0,a*17] for a in range(y*4,y*4+4)] for y in range(4)],dtype=np.uint8)))
    fixtures.append(('bc1-transparent',1,4,4,struct.pack('<HHI',0,0xffff,0xffffffff),0x688,np.zeros((4,4,4),dtype=np.uint8)))
    fixtures.append(('bc1-colors',1,4,4,struct.pack('<HHI',0xf800,0x07e0,0xe4e4e4e4),0x688,np.array([[[255,0,0,255],[0,255,0,255],[170,85,0,255],[85,170,0,255]]]*4,dtype=np.uint8)))
    rgba=np.array([[[10,40,90,128]]*4]*4,dtype=np.uint8);fixtures.append(('rgba-swizzle',0,4,4,rgba.tobytes(),2|(1<<3)|(0<<6)|(5<<9),np.array([[[90,40,10,255]]*4]*4,dtype=np.uint8)))
    fixtures.append(('r8-channel',4,4,4,bytes([70])*16,0x688,np.array([[[70,0,0,255]]*4]*4,dtype=np.uint8)))
    fixtures.append(('rg8-channels',5,4,4,bytes([30,90])*16,0x688,np.array([[[30,90,0,255]]*4]*4,dtype=np.uint8)))
    width=max(f[2] for f in fixtures);height=max(f[3] for f in fixtures)
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_update_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32]
    api.dbz_native_get_stats.argtypes=[C.c_void_p,C.POINTER(Stats)]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    api.dbz_native_render.argtypes=[C.c_void_p];api.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];api.dbz_native_destroy.argtypes=[C.c_void_p]
    def check(value):assert value,api.dbz_native_last_error().decode()
    handle=api.dbz_native_create_stream(None,width,height,str(directory/'materials.hlsl').encode());check(handle)
    v=np.array([[x,y,0,0,0,0,(x+1)/2,(1-y)/2,1,1,1,1,1] for x,y in ((-1,-1),(-1,1),(1,-1),(1,1))],dtype='<f4');idx=np.array([0,1,2,2,1,3],dtype='<u4')
    d=FrameDraw();d.index_count=6;d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.view_projection=(C.c_float*16)(*np.eye(4,dtype=np.float32).reshape(-1));d.raster.stencil.read_mask=d.raster.stencil.write_mask=255;d.raster.stencil.front[0]=d.raster.stencil.back[0]=7;d.raster.alpha.function=7
    clear=Clear((C.c_float*4)(0,0,0,0),1,0,7);results=[]
    try:
        for name,fmt,w,h,data,swizzle,expected in fixtures:
            texture=C.c_uint32();buf=C.create_string_buffer(data);check(api.dbz_native_add_texture_blocks(handle,fmt,w,h,buf,len(data),swizzle,C.byref(texture)));d.base_texture=texture.value
            d.raster.viewport=(C.c_float*6)(0,0,w,h,0,1);d.raster.scissor=(C.c_int32*4)(0,0,w,h)
            metrics={}
            for channel in ('rgb','alpha'):
                d.flags=0xf0000|0x700;d.material=(C.c_float*20)();d.material[3]=1;d.material[8]=d.material[9]=d.material[10]=d.material[11]=1
                if channel=='alpha':d.material[3]=2;d.flags|=0x10;d.material[12]=d.material[13]=d.material[14]=d.material[15]=1
                check(api.dbz_native_set_frame(handle,v.ctypes.data,4,idx.ctypes.data,6,C.byref(d),1,C.byref(clear)));check(api.dbz_native_render(handle));p=scratch/(name+'-'+channel+'.ppm');check(api.dbz_native_capture(handle,str(p).encode()))
                actual=np.array(Image.open(p))[:h,:w].astype(int);target=expected[:,:,:3].astype(int) if channel=='rgb' else np.repeat(expected[:,:,3,None],3,axis=2).astype(int)
                bound=allowance(data,fmt,w,h,swizzle);bound=bound[:,:,:3] if channel=='rgb' else np.repeat(bound[:,:,3,None],3,axis=2)
                delta=abs(actual-target);metrics[channel]={'max_error':int(delta.max()),'mean_error':float(delta.mean()),'pixels_outside_spec_and_quantization_bound':int((delta>bound).any(2).sum())};assert not (delta>bound).any(),(name,channel,metrics)
            results.append({'fixture':name,'native_format':fmt,'dimensions':[w,h],'encoded_bytes':len(data),'rgba_bytes':w*h*4,'metrics':metrics})
        out=C.c_uint32();assert not api.dbz_native_add_texture_blocks(handle,4,4,4,None,8,0x688,C.byref(out));assert not api.dbz_native_add_texture_blocks(handle,1,4,4,C.create_string_buffer(bytes(8)),8,0x68e,C.byref(out))
        d.material[3]=1;d.flags=0xf0000|0x700;d.raster.viewport=(C.c_float*6)(0,0,4,4,0,1);d.raster.scissor=(C.c_int32*4)(0,0,4,4)
        for c in range(12,16):d.material[c]=0
        check(api.dbz_native_set_frame(handle,v.ctypes.data,4,idx.ctypes.data,6,C.byref(d),1,C.byref(clear)))
        before=Stats();check(api.dbz_native_get_stats(handle,C.byref(before)))
        for i in range(16):
            fmt=1 if i%2 else 0;size=4 if fmt else 8;color=(0,255,0) if fmt else (255,0,0)
            data=struct.pack('<HHI',0x07e0,0,0) if fmt else bytes([255,0,0,255])*size*size
            buf=C.create_string_buffer(data);check(api.dbz_native_update_texture_blocks(handle,d.base_texture,fmt,size,size,buf,len(data),0x688));check(api.dbz_native_render(handle));p=scratch/'updated.ppm';check(api.dbz_native_capture(handle,str(p).encode()));assert np.array_equal(np.array(Image.open(p))[1,1],color)
            after=Stats();check(api.dbz_native_get_stats(handle,C.byref(after)));assert after.owned_objects==before.owned_objects and after.pipeline_count==before.pipeline_count
        assert not api.dbz_native_update_texture_blocks(handle,999999,0,1,1,None,4,0x688)
        tiny=C.c_uint32();tiny_data=C.create_string_buffer(struct.pack('<HHI',0xf800,0,0));tiny_supported=bool(api.dbz_native_add_texture_blocks(handle,1,1,1,tiny_data,8,0x688,C.byref(tiny)))
        if not tiny_supported:assert api.dbz_native_last_error().decode()=='Unaligned native BC textures unavailable'
        else:
            d.base_texture=tiny.value;d.raster.viewport=(C.c_float*6)(0,0,1,1,0,1);d.raster.scissor=(C.c_int32*4)(0,0,1,1)
            check(api.dbz_native_set_frame(handle,v.ctypes.data,4,idx.ctypes.data,6,C.byref(d),1,C.byref(clear)));check(api.dbz_native_render(handle));p=scratch/'tiny.ppm';check(api.dbz_native_capture(handle,str(p).encode()));assert np.array_equal(np.array(Image.open(p))[0,0],[255,0,0])
        result={'scope':'Native D3D12 BC1/2/3 sampling and SRV channel swizzle. Original CPU asset bytes are linearized, uploaded still compressed and decoded by hardware. Comparison uses the standard endpoint-dependent BC error allowance plus CPU-oracle/output quantization.','specification':SPEC,'fixtures':results,'in_place_texture_id_updates':16,'format_and_size_changes_rebind_active_descriptors':True,'stable_owned_objects_after_updates':after.owned_objects,'unaligned_bc_supported':tiny_supported};(scratch/'native-texture-block-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    finally:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--trace',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.trace,a.scratch)

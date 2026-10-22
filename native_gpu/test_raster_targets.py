"""Hardware tests of native mask targets, stencil, alpha test and dynamic raster."""
import argparse,ctypes as C,json
from pathlib import Path
import numpy as np
from PIL import Image
from native_scene import NativeDraw,write_scene


class Stencil(C.Structure):
    _fields_=[('enabled',C.c_uint8),('reference',C.c_uint8),('read_mask',C.c_uint8),('write_mask',C.c_uint8),('front',C.c_uint8*4),('back',C.c_uint8*4)]
class Alpha(C.Structure):
    _fields_=[('enabled',C.c_uint8),('function',C.c_uint8),('reserved',C.c_uint16),('reference',C.c_float)]
class Raster(C.Structure):
    _fields_=[('stencil',Stencil),('alpha',Alpha),('viewport',C.c_float*6),('scissor',C.c_int32*4)]
class Clear(C.Structure):
    _fields_=[('color',C.c_float*4),('depth',C.c_float),('stencil',C.c_uint32),('flags',C.c_uint32)]


def run(directory,scratch):
    directory,scratch=map(lambda p:Path(p).resolve(),(directory,scratch));scratch.mkdir(parents=True,exist_ok=True)
    assert C.sizeof(Raster)==60 and C.sizeof(Clear)==28
    dll=C.CDLL(str(directory/'dbz3-native-renderer.dll'))
    dll.dbz_native_create.argtypes=[C.c_void_p,C.c_char_p,C.c_char_p];dll.dbz_native_create.restype=C.c_void_p
    dll.dbz_native_last_error.restype=C.c_char_p
    for name,args in {'destroy':[C.c_void_p],'render':[C.c_void_p],'capture':[C.c_void_p,C.c_char_p],
        'set_draw_visible':[C.c_void_p,C.c_uint32,C.c_int],
        'update_raster':[C.c_void_p,C.c_uint32,C.POINTER(Raster)],'set_clear':[C.c_void_p,C.POINTER(Clear)]}.items():getattr(dll,'dbz_native_'+name).argtypes=args
    def check(value):
        if not value:raise AssertionError(dll.dbz_native_last_error().decode())
    matrix=np.eye(4,dtype='<f4');indices=np.array([0,1,2,2,1,3],dtype='<u4')
    def quad(left,right,z,color):
        return np.array([[x,y,z,0,0,1,(x+1)/2,(1-y)/2,*color,1] for x,y in ((left,-1),(left,1),(right,-1),(right,1))],dtype='<f4')
    def material(mode):
        v=np.zeros(20,dtype='<f4');v[3]=mode;v[8:12]=1;return v
    solid=material(3);mask=material(12);shadow=material(2);shadow[12:16]=[0,0,0,1]
    disabled=bytes([0,0,255,255,7,0,0,0,7,0,0,0]);once=bytes([1,1,255,255,2,0,0,4,2,0,0,4])
    opaque=0xf0000|0x300|3;mask_flags=0x100000|0xf0000|0x700;sample_flags=0x200000|0x70000|0x300|0x800|16|1
    draws=[
        NativeDraw(quad(-1,1,.7,(1,0,0,1)),indices,matrix,solid,opaque),
        NativeDraw(quad(-1,0,.2,(1,1,1,1)),indices,matrix,mask,mask_flags,matrix,viewport=(0,0,64,64,0,1),scissor=(0,0,64,64)),
        NativeDraw(quad(-1,1,.3,(1,1,1,.5)),np.tile(indices,2),matrix,shadow,sample_flags,stencil=once,alpha=(1,4,.1)),
        NativeDraw(quad(0,1,.2,(1,1,1,1)),indices,matrix,mask,mask_flags,matrix,viewport=(0,0,64,64,0,1),scissor=(0,0,64,64)),
        NativeDraw(quad(-1,1,.3,(1,1,1,.5)),indices,matrix,shadow,sample_flags,stencil=disabled,alpha=(1,4,.1)),
        NativeDraw(quad(-1,1,.9,(0,1,0,1)),indices,matrix,solid,opaque)
    ]
    path=scratch/'mask-stencil.dbzgpu';write_scene(path,draws,128,96,version=4,clear=(.025,.038,.07,1,1,1,7))
    h=dll.dbz_native_create(None,str(path).encode(),str(directory/'materials.hlsl').encode());check(h)
    passed=[]
    def capture(name):
        check(dll.dbz_native_render(h));p=scratch/(name+'.ppm');check(dll.dbz_native_capture(h,str(p).encode()));a=np.array(Image.open(p));Image.fromarray(a).save(p.with_suffix('.png'));return a
    def expect(image,x,y,value):assert np.max(abs(image[y,x].astype(int)-np.array(value)))<=2,(x,y,image[y,x],value)
    def raster(draw):
        d=draws[draw];r=Raster();C.memmove(C.addressof(r.stencil),d.stencil,12);r.alpha=Alpha(d.alpha[0],d.alpha[1],0,d.alpha[2]);r.viewport=(C.c_float*6)(*(d.viewport or (0,0,128,96,0,1)));r.scissor=(C.c_int32*4)(*(d.scissor or (0,0,128,96)));return r
    try:
        a=capture('two-mask-groups');expect(a,32,48,[128,0,0]);expect(a,96,48,[128,0,0])
        passed+=['two mask groups clear and sample independently','stencil prevents duplicate shadow darkening','main depth survives mask target switches']
        check(dll.dbz_native_set_draw_visible(h,3,0));check(dll.dbz_native_set_draw_visible(h,4,0));a=capture('one-mask-group');expect(a,32,48,[128,0,0]);expect(a,96,48,[255,0,0])
        check(dll.dbz_native_set_draw_visible(h,1,0));a=capture('hidden-producers');expect(a,32,48,[255,0,0]);expect(a,96,48,[255,0,0]);passed+=['hidden mask producers cannot reuse a previous frame']
        for draw in (1,3,4):check(dll.dbz_native_set_draw_visible(h,draw,1))
        r=raster(2);r.stencil.reference=0;check(dll.dbz_native_update_raster(h,2,C.byref(r)));a=capture('stencil-reference');expect(a,32,48,[255,0,0]);expect(a,96,48,[128,0,0])
        r=raster(2);check(dll.dbz_native_update_raster(h,2,C.byref(r)));passed+=['stencil reference changes without changing the pipeline']
        r=raster(2);r.alpha.reference=.6;check(dll.dbz_native_update_raster(h,2,C.byref(r)));a=capture('alpha-reference');expect(a,32,48,[255,0,0]);expect(a,96,48,[128,0,0]);r=raster(2);check(dll.dbz_native_update_raster(h,2,C.byref(r)));passed+=['alpha test discards fragments before stencil writes']
        r=raster(4);r.scissor=(C.c_int32*4)(64,48,128,96);check(dll.dbz_native_update_raster(h,4,C.byref(r)));a=capture('scissor');expect(a,96,24,[255,0,0]);expect(a,96,72,[128,0,0]);passed+=['dynamic scissor clips the sampled mask']
        r=raster(4);r.viewport=(C.c_float*6)(64,0,64,96,0,1);check(dll.dbz_native_update_raster(h,4,C.byref(r)));a=capture('viewport');expect(a,80,48,[255,0,0]);expect(a,112,48,[128,0,0]);passed+=['dynamic viewport changes geometry and UV interpolation']
        r=raster(4);check(dll.dbz_native_update_raster(h,4,C.byref(r)))
        clear=Clear((C.c_float*4)(.025,.038,.07,1),1,0,7);check(dll.dbz_native_set_clear(h,C.byref(clear)));a=capture('stencil-clear-zero');expect(a,32,48,[255,0,0]);expect(a,96,48,[128,0,0]);passed+=['explicit stencil clear value controls subsequent tests']
        r=raster(2);r.stencil.front[0]=7;assert not dll.dbz_native_update_raster(h,2,C.byref(r))
        r=raster(2);r.stencil.write_mask=0;assert not dll.dbz_native_update_raster(h,2,C.byref(r))
        r=raster(2);r.viewport[2]=-1;assert not dll.dbz_native_update_raster(h,2,C.byref(r))
        r=raster(2);r.viewport[0]=np.nan;assert not dll.dbz_native_update_raster(h,2,C.byref(r))
        r=raster(2);r.scissor[0]=200;assert not dll.dbz_native_update_raster(h,2,C.byref(r))
        r=raster(2);r.alpha.reference=np.nan;assert not dll.dbz_native_update_raster(h,2,C.byref(r))
        r=raster(2);assert not dll.dbz_native_update_raster(h,6,C.byref(r));assert not dll.dbz_native_update_raster(h,2,None)
        clear.flags=8;assert not dll.dbz_native_set_clear(h,C.byref(clear));assert not dll.dbz_native_set_clear(h,None)
        passed+=['invalid raster/clear updates and pipeline mask/operation changes rejected']
    finally:dll.dbz_native_destroy(h)
    result={'scope':'Synthetic hardware tests for native mask resources, D24S8 stencil, alpha test, viewport/scissor and clears. Not gameplay or original-frame fidelity.','passed':passed}
    (scratch/'raster-target-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

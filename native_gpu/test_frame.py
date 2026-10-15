"""Hardware tests for native frame ordering, depth, blending and live updates."""
import argparse,ctypes as C,json,struct
from pathlib import Path
import numpy as np
from PIL import Image


def run(directory,scratch):
    directory=Path(directory).resolve();scratch=Path(scratch).resolve();scratch.mkdir(parents=True,exist_ok=True)
    dll=C.CDLL(str(directory/'dbz3-native-renderer.dll'))
    dll.dbz_native_create.argtypes=[C.c_void_p,C.c_char_p,C.c_char_p];dll.dbz_native_create.restype=C.c_void_p
    dll.dbz_native_destroy.argtypes=[C.c_void_p]
    dll.dbz_native_render.argtypes=[C.c_void_p];dll.dbz_native_render.restype=C.c_int
    dll.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p];dll.dbz_native_capture.restype=C.c_int
    dll.dbz_native_last_error.restype=C.c_char_p
    dll.dbz_native_update_draw.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(C.c_float),C.POINTER(C.c_float)];dll.dbz_native_update_draw.restype=C.c_int
    dll.dbz_native_update_frame_vertices.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32];dll.dbz_native_update_frame_vertices.restype=C.c_int
    dll.dbz_native_set_draw_visible.argtypes=[C.c_void_p,C.c_uint32,C.c_int];dll.dbz_native_set_draw_visible.restype=C.c_int
    def check(result):
        if not result:raise AssertionError(dll.dbz_native_last_error().decode())
    colors=[(1,0,0,1),(0,0,1,.5),(0,1,0,1)]
    vertices=[];indices=[];draws=[];states=[];matrix=np.eye(4,dtype='<f4')
    material=np.array([0,0,0,1, 0,0,1,1, 1,1,1,1, 0,0,0,0, 0,0,0,0],dtype='<f4')
    for draw,z in enumerate((.7,.3,.9)):
        for x,y in ((-.8,-.8),(-.8,.8),(.8,-.8),(.8,.8)):
            vertices.append([x,y,z,0,0,1,0,0,*colors[draw],1])
        indices.extend(np.array([0,1,2,2,1,3])+4*draw)
        draws.append(struct.pack('<5I3f',draw*6,6,0,0xffffffff,0,1,1,1))
        flags=0xf0000|0x300|1|(0 if draw==1 else 2)|(16 if draw==1 else 0)
        states.append(matrix.tobytes()+material.tobytes()+struct.pack('<I',flags))
    vertices=np.array(vertices,dtype='<f4');indices=np.array(indices,dtype='<u4')
    path=scratch/'synthetic-frame.dbzgpu'
    with path.open('wb') as f:
        f.write(b'DBZNGPU2'+struct.pack('<4I',12,18,3,1)+struct.pack('<6f2I',-.8,-.8,.3,.8,.8,.9,128,96));f.write(vertices.tobytes());f.write(indices.tobytes());f.write(b''.join(draws));f.write(b''.join(states));f.write(struct.pack('<3I',1,1,4)+b'\xff\xff\xff\xff')
    handle=dll.dbz_native_create(None,str(path).encode(),str(directory/'materials.hlsl').encode());check(handle)
    def capture(name):
        check(dll.dbz_native_render(handle));target=scratch/(name+'.ppm');check(dll.dbz_native_capture(handle,str(target).encode()));return np.array(Image.open(target))[48,64].astype(int)
    passed=[]
    try:
        center=capture('blend-depth');assert abs(center-np.array([127,0,128])).max()<=2,center;passed+=['alpha blend and depth rejection']
        check(dll.dbz_native_set_draw_visible(handle,1,0));center=capture('hidden-draw');assert abs(center-np.array([255,0,0])).max()<=1,center
        check(dll.dbz_native_set_draw_visible(handle,1,1));center=capture('visible-again');assert abs(center-np.array([127,0,128])).max()<=2,center;passed+=['per-frame draw visibility and restoration']
        assert not dll.dbz_native_set_draw_visible(handle,1,2);assert not dll.dbz_native_set_draw_visible(handle,3,1);passed+=['invalid visibility and draw rejected']
        new=vertices.copy();new[4:8,0]+=3
        check(dll.dbz_native_update_frame_vertices(handle,new.ctypes.data,len(new)))
        center=capture('geometry-update');assert abs(center-np.array([255,0,0])).max()<=1,center;passed+=['live world geometry update']
        m=material.copy();m[12:16]=[0,1,0,1]
        check(dll.dbz_native_update_draw(handle,0,matrix.ctypes.data_as(C.POINTER(C.c_float)),m.ctypes.data_as(C.POINTER(C.c_float))))
        center=capture('material-update');assert abs(center-np.array([0,255,0])).max()<=1,center;passed+=['per-draw material update']
        bad=new.copy();bad[0,9]=np.nan
        assert not dll.dbz_native_update_frame_vertices(handle,bad.ctypes.data,len(bad));passed+=['NaN color rejected']
        assert not dll.dbz_native_update_frame_vertices(handle,new.ctypes.data,len(new)-1);passed+=['topology mismatch rejected']
        assert not dll.dbz_native_update_draw(handle,100,matrix.ctypes.data_as(C.POINTER(C.c_float)),material.ctypes.data_as(C.POINTER(C.c_float)));passed+=['draw range rejected']
    finally:dll.dbz_native_destroy(handle)
    result={'scope':'Synthetic hardware tests for version-2 frame API, not gameplay FPS or full-frame fidelity','passed':passed};(scratch/'frame-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

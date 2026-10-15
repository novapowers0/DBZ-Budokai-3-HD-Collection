"""Check shared textures/PSOs and bounded capture resource lifetimes."""
import argparse,ctypes as C,json,struct
from pathlib import Path
import numpy as np
from PIL import Image


class Stats(C.Structure):
    _fields_=[('draw_count',C.c_uint32),('texture_uploads',C.c_uint32),('pipeline_count',C.c_uint32),('owned_objects',C.c_uint32),('texture_bytes_uploaded',C.c_uint64),('submitted_frames',C.c_uint64)]


def run(directory,scene,reference,scratch):
    directory,scene,reference,scratch=map(lambda p:Path(p).resolve(),(directory,scene,reference,scratch));scratch.mkdir(parents=True,exist_ok=True)
    dll=C.CDLL(str(directory/'dbz3-native-renderer.dll'));dll.dbz_native_create.argtypes=[C.c_void_p,C.c_char_p,C.c_char_p];dll.dbz_native_create.restype=C.c_void_p
    dll.dbz_native_last_error.restype=C.c_char_p;dll.dbz_native_get_stats.argtypes=[C.c_void_p,C.POINTER(Stats)]
    dll.dbz_native_render.argtypes=[C.c_void_p];dll.dbz_native_destroy.argtypes=[C.c_void_p];dll.dbz_native_capture.argtypes=[C.c_void_p,C.c_char_p]
    def check(value):assert value,dll.dbz_native_last_error().decode()
    def stats():s=Stats();check(dll.dbz_native_get_stats(h,C.byref(s)));return {name:getattr(s,name) for name,_ in Stats._fields_}
    h=dll.dbz_native_create(None,str(scene).encode(),str(directory/'materials.hlsl').encode());check(h)
    try:
        initial=stats();texture_count=struct.unpack_from('<I',scene.read_bytes(),20)[0]
        assert initial['texture_uploads']<=texture_count+2 and initial['texture_uploads']<initial['draw_count'],initial
        assert initial['pipeline_count']<initial['draw_count']//2 and initial['submitted_frames']==0,initial
        expected=np.array(Image.open(reference))
        for frame in range(6):
            check(dll.dbz_native_render(h));capture=scratch/f'cache-{frame}.ppm';check(dll.dbz_native_capture(h,str(capture).encode()))
            assert np.array_equal(np.array(Image.open(capture)),expected),'Resource sharing changed output pixels'
            current=stats();assert current['owned_objects']==initial['owned_objects'],(initial,current)
            assert current['texture_uploads']==initial['texture_uploads'] and current['pipeline_count']==initial['pipeline_count']
            assert current['submitted_frames']==frame+1
        assert not dll.dbz_native_get_stats(h,None) and not dll.dbz_native_get_stats(None,C.byref(Stats()))
        result={'scope':'Shared-resource and immutable-PSO cache hardware regression. Exact output preserved; capture temporaries do not accumulate. Uploaded byte count is input RGBA size, not driver allocation size.','initial':initial,'after_six_render_and_capture_calls':current,'different_pixels':0}
        (scratch/'resource-cache-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    finally:dll.dbz_native_destroy(h)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scene',required=True);p.add_argument('--reference',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scene,a.reference,a.scratch)

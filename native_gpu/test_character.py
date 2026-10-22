"""Hardware oracle test: native GPU skinning versus captured GPU vertex output.

The reference scene uses the captured positions/normals, the same native pixel
shader and matching topology. This tests deformation, not reference pixel-shader
fidelity. The native scene never uses those captured transformed vertices.
"""
import argparse, ctypes as C, hashlib, json
from pathlib import Path
import numpy as np
from PIL import Image
from export_character_frame import export


def run(directory,reference,scratch):
    directory=Path(directory).resolve();scratch=Path(scratch).resolve();scratch.mkdir(parents=True,exist_ok=True)
    dll=C.CDLL(str(directory/'dbz3-native-renderer.dll'))
    for name,args in {'create':[C.c_void_p,C.c_char_p,C.c_char_p], 'destroy':[C.c_void_p],
        'render':[C.c_void_p], 'capture':[C.c_void_p,C.c_char_p],
        'update_draw':[C.c_void_p,C.c_uint32,C.c_void_p,C.c_void_p],
        'update_palette':[C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32],
        'update_frame_vertices':[C.c_void_p,C.c_void_p,C.c_uint32]}.items():
        getattr(dll,'dbz_native_'+name).argtypes=args
    dll.dbz_native_create.restype=C.c_void_p;dll.dbz_native_last_error.restype=C.c_char_p
    def check(v):
        if not v:raise AssertionError(dll.dbz_native_last_error().decode())
    def create(path):
        h=dll.dbz_native_create(None,str(path).encode(),str(directory/'materials.hlsl').encode());check(h);return h
    def capture(h,name):
        check(dll.dbz_native_render(h));path=scratch/(name+'.ppm');check(dll.dbz_native_capture(h,str(path).encode()))
        pixels=np.array(Image.open(path));Image.fromarray(pixels).save(path.with_suffix('.png'));return pixels
    comparisons=[];poses=[];images=[];passed=[]
    for pose in range(3):
        path=scratch/('pose-'+str(pose)+'.dbzgpu');data=export(reference,path,pose*13,13);poses.append(data)
        h=create(path);oracle=create(path.with_name(path.stem+'-reference.dbzgpu'))
        try:
            actual=capture(h,'native-'+str(pose));expected=capture(oracle,'reference-'+str(pose));images.append(actual)
            error=abs(actual.astype(int)-expected.astype(int))
            assert error.mean()<.01 and np.count_nonzero(error.max(2)>2)<20,(pose,error.mean(),error.max())
            mask=np.max(abs(actual.astype(int)-actual[0,0].astype(int)),2)>15
            assert mask.sum()>(1000 if len(data[0][6])==4 else 30000),'Character image unexpectedly empty'
            comparisons.append({'pose':pose,'draws':len(data),'vertices':sum(len(v[0]) for v in data),
                'mean_rgb_error':float(error.mean()),'max_rgb_error':int(error.max()),'pixels_over_2':int(np.count_nonzero(error.max(2)>2)),
                'visible_pixels':int(mask.sum())})
        finally:dll.dbz_native_destroy(h);dll.dbz_native_destroy(oracle)
    passed.append('three captured poses: GPU deformation agrees with independent post-VS oracle')
    # Update the original renderer in place using only local geometry/constants
    # and compact palettes from the next frame. No transformed oracle data.
    h=create(scratch/'pose-0.dbzgpu')
    try:
        for pose in (1,2):
            for draw,(previous,current) in enumerate(zip(poses[0],poses[pose])):
                assert np.array_equal(previous[2],current[2]),'Topology changed during fixed-topology test'
                check(dll.dbz_native_update_palette(h,draw,current[6].ctypes.data,len(current[6])))
                check(dll.dbz_native_update_draw(h,draw,current[3].ctypes.data,current[4].ctypes.data))
            local=np.concatenate([v[0] for v in poses[pose]]).astype('<f4')
            check(dll.dbz_native_update_frame_vertices(h,local.ctypes.data,len(local)))
            actual=capture(h,'updated-'+str(pose));assert np.array_equal(actual,images[pose]),'Live palette update differs from a fresh renderer'
        passed.append('palette, camera, material and local-vertex updates reproduce subsequent poses')
        valid=poses[2][0][6];bad=valid.copy();bad[-1,0]=np.nan
        assert not dll.dbz_native_update_palette(h,0,valid.ctypes.data,len(valid)-1)
        assert not dll.dbz_native_update_palette(h,13,valid.ctypes.data,len(valid))
        assert not dll.dbz_native_update_palette(h,0,None,len(valid))
        assert not dll.dbz_native_update_palette(h,0,bad.ctypes.data,len(valid))
        passed.append('invalid palette size, draw, null pointer and NaN rejected')
        if len(valid)==388:
            invalid=local.copy();invalid[0,8]=128
            assert not dll.dbz_native_update_frame_vertices(h,invalid.ctypes.data,len(invalid))
            invalid=local.copy();invalid[0,9]=-1
            assert not dll.dbz_native_update_frame_vertices(h,invalid.ctypes.data,len(invalid))
            passed.append('invalid bone and blend weight rejected before GPU upload')
        moved=valid.copy();moved[3,0]+=10
        check(dll.dbz_native_update_palette(h,0,moved.ctypes.data,len(valid)));actual=capture(h,'palette-translation')
        assert np.count_nonzero(np.max(abs(actual.astype(int)-images[2].astype(int)),2)>10)>100
        passed.append('GPU palette translation changes the rendered geometry')
    finally:dll.dbz_native_destroy(h)
    changed=[int(np.count_nonzero(np.max(abs(images[i].astype(int)-images[0].astype(int)),2)>2)) for i in (1,2)]
    assert all(n>100 for n in changed),'Captured poses did not change'
    forbidden=[name for name in ('rexruntime.dll','rexgpu-xenos.dll','xenia.exe') if C.windll.kernel32.GetModuleHandleW(name)]
    assert not forbidden,'Reference runtime was loaded: '+str(forbidden)
    result={'scope':'Hardware skinning oracle and palette-update tests for partial character bodies. No live-game or complete pixel-fidelity claim.',
        'reference_runtime_loaded':False,
        'native_renderer_sha256':hashlib.sha256((directory/'dbz3-native-renderer.dll').read_bytes()).hexdigest(),
        'passed':passed,'comparisons':comparisons,'changed_pixels_from_pose_0':changed}
    (scratch/'character-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--reference',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.reference,a.scratch)

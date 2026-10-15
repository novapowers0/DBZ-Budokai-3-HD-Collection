"""Hardware regression for outline and colored-skin native vertex families."""
import argparse,ctypes as C,hashlib,json
from pathlib import Path
import numpy as np
from PIL import Image
import export_outline_frame,export_skincolor_frame,export_mask_frame,export_projected_frame


def run(directory,reference,scratch,family,draws_per_pose):
    directory,reference,scratch=map(lambda p:Path(p).resolve(),(directory,reference,scratch));scratch.mkdir(parents=True,exist_ok=True)
    module={'outline':export_outline_frame,'skincolor':export_skincolor_frame,'mask':export_mask_frame,'projected':export_projected_frame}[family]
    dll=C.CDLL(str(directory/'dbz3-native-renderer.dll'))
    for name,args in {'create':[C.c_void_p,C.c_char_p,C.c_char_p],'destroy':[C.c_void_p],'render':[C.c_void_p],
        'capture':[C.c_void_p,C.c_char_p],'update_draw':[C.c_void_p,C.c_uint32,C.c_void_p,C.c_void_p],
        'update_palette':[C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32],'update_frame_vertices':[C.c_void_p,C.c_void_p,C.c_uint32]}.items():getattr(dll,'dbz_native_'+name).argtypes=args
    dll.dbz_native_create.restype=C.c_void_p;dll.dbz_native_last_error.restype=C.c_char_p
    def check(v):
        if not v:raise AssertionError(dll.dbz_native_last_error().decode())
    def create(path):
        h=dll.dbz_native_create(None,str(path).encode(),str(directory/'materials.hlsl').encode());check(h);return h
    def capture(h,name):
        check(dll.dbz_native_render(h));path=scratch/(name+'.ppm');check(dll.dbz_native_capture(h,str(path).encode()));a=np.array(Image.open(path));Image.fromarray(a).save(path.with_suffix('.png'));return a
    poses=[];images=[];rows=[]
    for pose in range(3):
        path=scratch/('pose-'+str(pose)+'.dbzgpu');converted=module.export(reference,path,pose*draws_per_pose,draws_per_pose);assert len(converted)==draws_per_pose;poses.append(converted)
        h=create(path);oracle=create(path.with_name(path.stem+'-reference.dbzgpu'))
        try:
            actual=capture(h,'native-'+str(pose));expected=capture(oracle,'reference-'+str(pose));images.append(actual)
            error=abs(actual.astype(int)-expected.astype(int));different=int(np.count_nonzero(error.max(2)>2));visible=int(np.count_nonzero(np.max(abs(actual.astype(int)-actual[0,0].astype(int)),2)>5))
            assert error.mean()<.02 and different<max(20,visible*.001),(pose,error.mean(),different,visible)
            assert visible>20,'Oracle image unexpectedly empty'
            rows.append({'pose':pose,'draws':len(converted),'vertices':sum(len(v[0].vertices) for v in converted),'mean_rgb_error':float(error.mean()),'max_rgb_error':int(error.max()),'pixels_over_2':different,'visible_pixels':visible})
        finally:dll.dbz_native_destroy(h);dll.dbz_native_destroy(oracle)
    h=create(scratch/'pose-0.dbzgpu')
    try:
        for pose in (1,2):
            for draw,((prior,_),(current,_)) in enumerate(zip(poses[0],poses[pose])):
                assert np.array_equal(prior.indices,current.indices),'Topology changed'
                if len(current.palette):check(dll.dbz_native_update_palette(h,draw,current.palette.ctypes.data,len(current.palette)))
                check(dll.dbz_native_update_draw(h,draw,current.matrix.ctypes.data,current.material.ctypes.data))
            local=np.concatenate([v[0].vertices for v in poses[pose]]).astype('<f4');check(dll.dbz_native_update_frame_vertices(h,local.ctypes.data,len(local)))
            assert np.array_equal(capture(h,'updated-'+str(pose)),images[pose]),'In-place pose differs from a fresh renderer'
        if family=='skincolor':
            invalid=local.copy();invalid[0,3]=128;assert not dll.dbz_native_update_frame_vertices(h,invalid.ctypes.data,len(invalid))
            invalid=local.copy();invalid[0,4]=-1;assert not dll.dbz_native_update_frame_vertices(h,invalid.ctypes.data,len(invalid))
    finally:dll.dbz_native_destroy(h)
    changed=[int(np.count_nonzero(np.max(abs(v.astype(int)-images[0].astype(int)),2)>2)) for v in images[1:]];assert all(v>10 for v in changed)
    k=C.WinDLL('kernel32');k.GetModuleHandleW.argtypes=[C.c_wchar_p];k.GetModuleHandleW.restype=C.c_void_p
    assert not any(k.GetModuleHandleW(n) for n in ('rexruntime.dll','rexgpu-xenos.dll','xenia.exe'))
    result={'scope':'Native '+family+' vertex-family oracle and in-place pose updates. Native pixel shader is shared by both paths; not full-game pixel fidelity.',
        'family':family,'native_renderer_sha256':hashlib.sha256((directory/'dbz3-native-renderer.dll').read_bytes()).hexdigest(),
        'reference_runtime_loaded':False,'comparisons':rows,'changed_pixels':changed,'in_place_updates_match':True}
    (scratch/'oracle-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--reference',required=True);p.add_argument('--scratch',required=True);p.add_argument('--family',choices=['outline','skincolor','mask','projected'],required=True);p.add_argument('--draws-per-pose',type=int,required=True);a=p.parse_args();run(a.directory,a.reference,a.scratch,a.family,a.draws_per_pose)

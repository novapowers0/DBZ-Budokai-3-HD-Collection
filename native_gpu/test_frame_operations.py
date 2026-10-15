"""Ordered GPU color/depth copies, clears, frozen snapshots and validation."""
import argparse,ctypes as C,json
from pathlib import Path
import numpy as np
from PIL import Image
from test_streaming_frame import FrameDraw
from test_raster_targets import Clear
from test_resource_cache import Stats


class Operation(C.Structure):
    _fields_=[('before_draw',C.c_uint32),('type',C.c_uint32),('source_target',C.c_uint32),('destination_texture',C.c_uint32),('clear',Clear)]


def run(directory,scratch):
    directory,scratch=map(lambda p:Path(p).resolve(),(directory,scratch));scratch.mkdir(parents=True,exist_ok=True);assert C.sizeof(Operation)==44
    api=C.CDLL(str(directory/'dbz3-native-renderer.dll'));api.dbz_native_last_error.restype=C.c_char_p
    api.dbz_native_create_stream.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_char_p];api.dbz_native_create_stream.restype=C.c_void_p
    api.dbz_native_add_snapshot_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_set_frame_operations.argtypes=[C.c_void_p,C.POINTER(Operation),C.c_uint32]
    api.dbz_native_add_render_target.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.POINTER(Clear),C.POINTER(C.c_uint32),C.POINTER(C.c_uint32)]
    api.dbz_native_add_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32)]
    api.dbz_native_update_texture_blocks.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32]
    api.dbz_native_set_frame.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(FrameDraw),C.c_uint32,C.POINTER(Clear)]
    for name,args in {'render':[C.c_void_p],'capture':[C.c_void_p,C.c_char_p],'destroy':[C.c_void_p],'get_stats':[C.c_void_p,C.POINTER(Stats)]}.items():getattr(api,'dbz_native_'+name).argtypes=args
    def check(value):assert value,api.dbz_native_last_error().decode()
    handles=[api.dbz_native_create_stream(None,96,64,str(directory/'materials.hlsl').encode()) for _ in range(2)]
    for handle in handles:check(handle)
    clear=Clear((C.c_float*4)(0,0,0,1),1,0,7)
    def draw(texture=0xffffffff,depth=False,target=0):
        d=FrameDraw();d.index_count=6;d.base_texture=texture;d.ramp_texture=0xffffffff;d.diffuse=(C.c_float*3)(1,1,1);d.flags=0xf0103 if depth else 0xf0700;d.view_projection=(C.c_float*16)(*np.eye(4,dtype=np.float32).reshape(-1));d.material[3]=3 if texture==0xffffffff else 1;d.material[8:12]=[1]*4;d.raster.viewport=(C.c_float*6)(0,0,96,64,0,1);d.raster.scissor=(C.c_int32*4)(0,0,96,64);d.raster.alpha.function=7;d.raster.stencil.read_mask=d.raster.stencil.write_mask=255;d.raster.stencil.front[0]=d.raster.stencil.back[0]=7;d.reserved=target;return d
    def quad(left=-1,right=1,z=.5,color=(1,1,1,1)):
        return np.array([[x,y,z,0,0,0,u,v,*color,1] for x,y,u,v in ((left,-1,0,1),(left,1,0,0),(right,-1,1,1),(right,1,1,0))],dtype='<f4')
    def frame(handle,items):
        vv=[];ii=[];dd=[]
        for vertices,d in items:
            d=FrameDraw.from_buffer_copy(d);d.first_index=len(ii);ii.extend((np.array([0,1,2,2,1,3],dtype=np.uint32)+len(vv)).tolist());vv.extend(vertices);dd.append(d)
        v=np.array(vv,dtype='<f4').reshape(-1,13);i=np.array(ii,dtype='<u4');draws=(FrameDraw*len(dd))(*dd);check(api.dbz_native_set_frame(handle,v.ctypes.data,len(v),i.ctypes.data,len(i),draws,len(draws),C.byref(clear)))
    def operations(handle,values):return api.dbz_native_set_frame_operations(handle,(Operation*len(values))(*values),len(values))
    def capture(handle,name):
        check(api.dbz_native_render(handle));p=scratch/(name+'.ppm');check(api.dbz_native_capture(handle,str(p).encode()));return np.array(Image.open(p))
    def snapshot(kind,w=96,h=64):
        out=C.c_uint32();check(api.dbz_native_add_snapshot_texture(handles[0],kind,w,h,C.byref(out)));return out.value
    try:
        color_id=snapshot(1);depth_id=snapshot(2);ids=[];source=np.zeros((64,96,4),dtype=np.uint8);depth=np.zeros_like(source);depth[:,:,:]=[191,0,0,255];depth[:,:48,:]=[64,0,0,255]
        for pixels in (source,depth):
            out=C.c_uint32();check(api.dbz_native_add_texture(handles[1],96,64,C.create_string_buffer(pixels.tobytes()),pixels.nbytes,C.byref(out)));ids.append(out.value)
        copy_color=Operation(2,1,0,color_id,Clear());copy_depth=Operation(2,2,0,depth_id,Clear());reset=Operation(2,3,0,0,Clear((C.c_float*4)(0,0,0,1),.5,7,7));warmed=None;passed=[]
        frame(handles[0],[(quad(),draw(color_id))]);assert not api.dbz_native_render(handles[0]),'A new snapshot is uninitialized'
        frame(handles[0],[]);check(operations(handles[0],[Operation(0,1,0,color_id,Clear())]))
        frame(handles[0],[(quad(),draw(color_id))]);assert not api.dbz_native_render(handles[0]),'Registering a copy without submitting it must not initialize GPU history'
        for step in range(12):
            colors=np.array([[37+step*4,91,212,255],[218,143-step*3,41,255]],dtype=np.uint8);source[:,:48]=colors[0];source[:,48:]=colors[1]
            check(api.dbz_native_update_texture_blocks(handles[1],ids[0],0,96,64,C.create_string_buffer(source.tobytes()),source.nbytes,0x688))
            items=[(quad(-1,0,.25,colors[0].astype(np.float32)/255),draw(depth=True)),(quad(0,1,.75,colors[1].astype(np.float32)/255),draw(depth=True)),(quad(-1,0),draw(color_id)),(quad(0,1),draw(depth_id)),(quad(z=.6,color=(0,1,0,1)),draw(depth=True))]
            frame(handles[0],items);check(operations(handles[0],[copy_color,copy_depth,reset]));actual=capture(handles[0],'actual');frame(handles[1],[(quad(-1,0),draw(ids[0])),(quad(0,1),draw(ids[1]))]);expected=capture(handles[1],'expected');delta=abs(actual.astype(int)-expected.astype(int));assert delta.max()==0,(step,int(delta.max()))
            stats=Stats();check(api.dbz_native_get_stats(handles[0],C.byref(stats)))
            if step==1:warmed=(stats.owned_objects,stats.pipeline_count,stats.texture_uploads)
            elif step>1:assert (stats.owned_objects,stats.pipeline_count,stats.texture_uploads)==warmed
            passed.append(dict(frame=step+1,different_pixels=0))
        assert not operations(handles[0],[Operation(3,1,0,color_id,Clear()),copy_depth]),'Unsorted operations must fail'
        assert not operations(handles[0],[Operation(2,1,0,depth_id,Clear())]),'Color/depth destination mismatch must fail'
        uncopied_depth=snapshot(2)
        assert not operations(handles[0],[Operation(0,4,0,uncopied_depth,Clear())]),'An uninitialized depth snapshot cannot be restored'
        assert not api.dbz_native_update_texture_blocks(handles[0],color_id,0,96,64,C.create_string_buffer(source.tobytes()),source.nbytes,0x688)
        assert np.array_equal(capture(handles[0],'after-rejected-operations'),actual)
        target=C.c_uint32();target_texture=C.c_uint32();check(api.dbz_native_add_render_target(handles[0],96,64,C.byref(clear),C.byref(target),C.byref(target_texture)))
        frozen=draw(color_id);producer=draw(target=target.value);frame(handles[0],[(quad(color=(.2,.4,.6,1)),producer),(quad(color=(1,0,0,1)),producer),(quad(),frozen)]);check(operations(handles[0],[Operation(1,1,target.value,color_id,Clear())]));offscreen=capture(handles[0],'offscreen-frozen');assert np.all(offscreen==[51,102,153])
        frame(handles[0],[]);check(operations(handles[0],[Operation(0,3,0,0,Clear((C.c_float*4)(.4,.6,.8,1),1,0,7))]));empty=capture(handles[0],'clear-after-last-draw');assert np.all(empty==[102,153,204])
        stencil_draw=draw(depth=True);stencil_draw.raster.stencil.enabled=1;stencil_draw.raster.stencil.reference=0;stencil_draw.raster.stencil.write_mask=0;stencil_draw.raster.stencil.front[0]=stencil_draw.raster.stencil.back[0]=2
        frame(handles[0],[(quad(-1,0,.25,(1,0,0,1)),draw(depth=True)),(quad(0,1,.75,(0,0,1,1)),draw(depth=True)),(quad(z=.5,color=(0,1,0,1)),stencil_draw)])
        restore=Operation(2,4,0,depth_id,Clear());check(operations(handles[0],[copy_depth,Operation(2,3,0,0,Clear((C.c_float*4)(0,0,0,0),.1,7,6)),restore]));restored=capture(handles[0],'depth-stencil-restored');assert np.all(restored[:,:48]==[255,0,0]) and np.all(restored[:,48:]==[0,255,0]),'Depth and stencil must both return from the saved snapshot'
        assert not operations(handles[0],[Operation(2,4,0,uncopied_depth,Clear())]),'Restoring an uncopied snapshot must fail'
        frame(handles[0],[(quad(z=.5,color=(0,1,0,1)),stencil_draw)]);check(operations(handles[0],[Operation(0,4,0,depth_id,Clear())]));historical_depth=capture(handles[0],'historical-depth-stencil');assert np.all(historical_depth[:,:48]==[0,0,0]) and np.all(historical_depth[:,48:]==[0,255,0]),'Depth/stencil restoration must accept a completed prior-frame snapshot'
        crop_id=snapshot(1,48,64);mask_draw=draw();mask_draw.flags|=1<<20;frame(handles[0],[(quad(-1,0,color=(1,0,0,1)),mask_draw),(quad(0,1,color=(0,0,1,1)),mask_draw),(quad(),draw(crop_id))]);check(operations(handles[0],[Operation(2,1,0xffffffff,crop_id,Clear())]));mask_crop=capture(handles[0],'cropped-native-mask');assert np.all(mask_crop==[255,0,0])
        solid=draw();solid.material[3]=19;solid.material[2]=1;solid.material[16:19]=[.2,.4,.6];solid_vertices=quad(color=(1,0,0,.5));solid_vertices[:,-1]=.5;composite=draw(color_id);composite.material[3]=15;composite.flags|=1<<4;frame(handles[0],[(solid_vertices,solid),(quad(),composite)]);check(operations(handles[0],[Operation(1,1,0,color_id,Clear()),Operation(1,3,0,0,Clear((C.c_float*4)(0,0,1,1),1,0,1))]));alpha_result=capture(handles[0],'solid-alpha-fog');assert np.all(alpha_result==[38,13,210]),'Untextured color must preserve vertex alpha and fog attenuation through later blending'
        frame(handles[0],[(quad(),draw(color_id))]);history=capture(handles[0],'history-without-old-operations');assert np.all(history==[153,51,76]),'Historical snapshot survives; set_frame resets old copy and clear operations'
        Image.fromarray(actual).save(scratch/'native.png');result=dict(scope='Ordered native GPU color and normalized D24 depth snapshots, followed by explicit color/depth/stencil clear. Snapshot contents survive overwriting their original source and later frames. Uninitialized snapshots and copies registered without GPU submission cannot be consumed. Native offscreen target snapshots, cropped native-mask snapshot, depth+stencil restoration from current/prior frame, solid-color alpha/fog through later blending and end-of-frame operations match independent expected values. No reference runtime or framebuffer input.',frames=passed,stable_owned_objects=warmed[0],stable_pipeline_count=warmed[1],stable_texture_uploads=warmed[2],offscreen_snapshot_survives_overwrite=True,native_mask_crop=True,solid_alpha_fog_composite=True,depth_stencil_restore=True,historical_depth_stencil_restore=True,historical_color_snapshot=True,unsubmitted_copy_does_not_initialize=True,empty_frame_clear=True,invalid_operations_preserve_prior_frame=True);(scratch/'frame-operations-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    finally:
        for handle in handles:api.dbz_native_destroy(handle)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

"""Export a local RenderDoc reference frame for native-renderer research.

Run with qrenderdoc --python capture_reference.py. DBZ_CAPTURE_CONFIG names a
JSON file containing capture, output and optional shader_dump absolute paths.
Only geometry/constants/textures are exported; no translated shader is emitted.
RenderDoc is a research oracle and is not a native runtime dependency.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import traceback
import renderdoc as rd


def plain(v, depth=0):
    if v is None or isinstance(v,(int,float,str,bool)):return v
    if isinstance(v,bytes):return {'bytes':len(v)}
    if type(v).__name__=='ResourceId':return str(v)
    if depth>5:return str(v)
    if hasattr(v,'__len__'):
        try:return [plain(x,depth+1) for x in v]
        except TypeError:pass
    result={}
    for name in dir(v):
        if name.startswith('_') or name in ('this','thisown','rawBytes','debugInfo'):continue
        value=getattr(v,name)
        if not callable(value):result[name]=plain(value,depth+1)
    return result or str(v)


def export(config):
    output=Path(config['output']);output.mkdir(parents=True,exist_ok=True)
    shaders={}
    folders=config.get('shader_dumps',[])
    if config.get('shader_dump'):folders=[config['shader_dump']]+folders
    for folder in folders:
        for p in Path(folder).glob('*.d3d12*.bin.*'):
            shaders[hashlib.sha256(p.read_bytes()).hexdigest()]=p.name
    for mapping in config.get('shader_maps',[]):
        recorded=json.loads(Path(mapping).read_text())
        for row in recorded['draws']:
            for stage in row['stages'].values():
                if stage.get('dump_name'):shaders[stage['sha256']]=stage['dump_name']
    cap=rd.OpenCaptureFile();controller=None
    try:
        status=cap.OpenFile(config['capture'],'',None)
        if status!=rd.ResultCode.Succeeded:raise RuntimeError(str(status))
        status,controller=cap.OpenCapture(rd.ReplayOptions(),None)
        if status!=rd.ResultCode.Succeeded:raise RuntimeError(str(status))
        textures={str(t.resourceId):t for t in controller.GetTextures()}
        actions=[]
        def walk(nodes):
            for a in nodes:
                if a.flags & rd.ActionFlags.Drawcall:actions.append(a)
                walk(a.children)
        walk(controller.GetRootActions())
        draws=[];exported_textures=set()
        for action in actions:
            if action.numIndices < config.get('min_indices',0):continue
            if config.get('events') and action.eventId not in config['events']:continue
            controller.SetFrameEvent(action.eventId,True)
            pipe=controller.GetPipelineState()
            vertex=pipe.GetShaderReflection(rd.ShaderStage.Vertex)
            if not vertex or not any(r.name=='xe_shared_memory_srv' for r in vertex.readOnlyResources):continue
            vertex_name=shaders.get(hashlib.sha256(bytes(vertex.rawBytes)).hexdigest(),'')
            if config.get('vertex_shader') and config['vertex_shader'] not in vertex_name:continue
            if config.get('vertex_shaders') and not any(name in vertex_name for name in config['vertex_shaders']):continue
            (output/'export-progress.json').write_text(json.dumps({'event':action.eventId,'vertex_shader':vertex_name,'phase':'resources'}))
            row={'event':action.eventId,'count':action.numIndices,'index_offset':action.indexOffset,'vertex_offset':action.vertexOffset,
                 'topology':str(pipe.GetPrimitiveTopology()),'viewport':plain(pipe.GetViewport(0)),
                 'depth':plain(pipe.GetDepthTestState()),'blend':plain(pipe.GetColorBlends()[0]),'raster':plain(pipe.GetRasterState()),
                 'stages':{},'descriptors':[],'vertex_fetches':[],'texture_files':{}}
            prefix=f'e{action.eventId}'
            original_index=pipe.GetIBuffer()
            row['original_index']=plain(original_index)
            if original_index.resourceId!=rd.ResourceId() and original_index.byteSize<16777216:
                (output/f'{prefix}-original-indices.bin').write_bytes(controller.GetBufferData(original_index.resourceId,original_index.byteOffset,original_index.byteSize))
            for stage in (rd.ShaderStage.Vertex,rd.ShaderStage.Pixel):
                reflection=pipe.GetShaderReflection(stage)
                if not reflection:continue
                sha=hashlib.sha256(bytes(reflection.rawBytes)).hexdigest()
                info={'sha256':sha,'dump_name':shaders.get(sha),'input':plain(reflection.inputSignature),'output':plain(reflection.outputSignature),
                      'resources':plain(reflection.readOnlyResources),'constants':[]}
                for cb in pipe.GetConstantBlocks(stage):
                    i=cb.access.index;ref=reflection.constantBlocks[i];desc=cb.descriptor
                    if ref.byteSize>65536:raise RuntimeError('Unexpected constant block size')
                    data=bytes(controller.GetBufferData(desc.resource,desc.byteOffset,ref.byteSize))
                    name=f'{prefix}-{str(stage)}-{ref.name}.bin';(output/name).write_bytes(data)
                    info['constants'].append({'name':ref.name,'file':name,'variables':plain(ref.variables),'descriptor':plain(desc)})
                row['stages'][str(stage)]=info
            descs=pipe.GetAllUsedDescriptors(True)
            for used in descs:
                access=used.access;d=used.descriptor;rid=str(d.resource)
                row['descriptors'].append({'access':plain(access),'descriptor':plain(d),'sampler':plain(used.sampler)})
                texture_key=(action.eventId,rid) if config.get('texture_versions') else rid
                name=('e'+str(action.eventId)+'-' if config.get('texture_versions') else '')+'texture-'+re.sub(r'\D','',rid)+'.png'
                if rid in textures and texture_key not in exported_textures:
                    texture=textures[rid]
                    if texture.width*texture.height>16777216:continue
                    save=rd.TextureSave();save.resourceId=d.resource;save.destType=rd.FileType.PNG
                    save.mip=d.firstMip;save.slice.sliceIndex=d.firstSlice;save.alpha=rd.AlphaMapping.Preserve
                    result=controller.SaveTexture(save,str(output/name))
                    if result!=rd.ResultCode.Succeeded:raise RuntimeError('Save texture: '+str(result))
                    exported_textures.add(texture_key)
                if texture_key in exported_textures:row['texture_files'][rid]=name
            # Raw guest vertex buffers are in a native ByteAddressBuffer. Fetch
            # registers identify bounded physical byte ranges; no PM4 is replayed.
            shared=None
            for used in descs:
                if used.access.stage==rd.ShaderStage.Vertex and used.access.index<len(vertex.readOnlyResources):
                    resource=vertex.readOnlyResources[used.access.index]
                    if resource.name=='xe_shared_memory_srv' and used.access.type==resource.descriptorType:shared=used.descriptor.resource
            fetch=next((cb for cb in row['stages']['ShaderStage.Vertex']['constants'] if cb['name']=='xe_fetch_cbuffer'),None)
            if shared and fetch:
                data=(output/fetch['file']).read_bytes()
                for i in range(0,len(data)-7,8):
                    if config.get('vertex_slots') and i//8 not in config['vertex_slots']:continue
                    a,b=struct.unpack_from('<II',data,i)
                    if a&3!=3:continue
                    address=a&0xfffffffc;size=(b>>2&0xffffff)*4
                    if not size or size>4194304 or address+size>0x20000000:continue
                    name=f'{prefix}-vf{i//8}.bin'
                    (output/name).write_bytes(controller.GetBufferData(shared,address,size))
                    row['vertex_fetches'].append({'slot':i//8,'address':address,'size':size,'endian':b&3,'file':name})
            (output/'export-progress.json').write_text(json.dumps({'event':action.eventId,'vertex_shader':vertex_name,'phase':'postvs'}))
            mesh=controller.GetPostVSData(0,0,rd.MeshDataStage.VSOut)
            row['postvs']=plain(mesh)
            if mesh.vertexByteSize>16777216:raise RuntimeError('Unexpected vertex output size')
            (output/f'{prefix}-postvs.bin').write_bytes(controller.GetBufferData(mesh.vertexResourceId,mesh.vertexByteOffset,mesh.vertexByteSize))
            if mesh.indexResourceId!=rd.ResourceId():
                (output/f'{prefix}-indices.bin').write_bytes(controller.GetBufferData(mesh.indexResourceId,mesh.indexByteOffset,mesh.indexByteSize))
            # Preserve the native render target of the first scene draw to
            # compare against; later targets may include UI / upscaler output.
            targets=pipe.GetOutputTargets()
            row['targets']=plain(targets)
            draws.append(row)
            (output/'draws.partial.json').write_text(json.dumps(draws,indent=2))
        controller.SetFrameEvent(actions[-1].eventId,True)
        result={'capture':str(Path(config['capture']).name),'scene_draws':len(draws),'host_draws':len(actions)-len(draws),
                'draws':draws,'textures':[plain(t) for rid,t in textures.items() if rid in exported_textures]}
        (output/'frame.json').write_text(json.dumps(result,indent=2))
    finally:
        if controller:controller.Shutdown()
        cap.Shutdown()

if __name__=='__main__':
    config=json.loads(Path(os.environ['DBZ_CAPTURE_CONFIG']).read_text())
    stale=Path(config['output'])/'export-error.txt'
    if stale.exists():stale.unlink()
    try:export(config)
    except Exception:
        Path(config['output']).mkdir(parents=True,exist_ok=True)
        (Path(config['output'])/'export-error.txt').write_text(traceback.format_exc())
    sys.exit(0)

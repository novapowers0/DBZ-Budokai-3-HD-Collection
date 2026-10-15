"""Export a guarded native world component from indexed CPU draw captures.

Only the independently checked FDF960/1BFD shader pair is admitted. Shader
bytes are used to identify semantics; the output contains no shader microcode.
The matching live bridge reads the original CPU vertex/index buffers, never
the reference renderer's framebuffer or transformed vertices.
"""
import argparse,collections,json,struct
from pathlib import Path
import numpy as np
from texture_decode import decode,linearize
from export_live_ui import fnv,pipeline
from live_raster import encode as raster_bytes,pipeline as target_pipeline


def geometry(data,device,source_ids,base):
    source=np.frombuffer(data,dtype='>f4').reshape(-1,11)
    words=np.frombuffer(data,dtype=np.uint8).reshape(-1,44)
    used=source_ids.astype(np.int64)+base
    if used.min()<0 or used.max()>=len(source):raise ValueError('indexed vertex range invalid')
    positions=source[used,:3].astype('<f4');uv=source[used,9:11].astype('<f4')
    colors=words[used,32:36][:,[1,2,3,0]].astype(np.float32)/255
    c=np.frombuffer(device[1920:6016],dtype='>f4').reshape(-1,4).astype(np.float64)
    transform=c[8:12]@c[:4]
    projected=np.column_stack([positions,np.ones(len(positions))])@transform
    fog=np.clip(np.clip((c[12,0]-projected[:,2])*c[12,1],0,1),1-c[12,3],1-c[12,2])
    vertices=np.column_stack([positions,np.zeros_like(positions),uv,colors,fog]).astype('<f4')
    ps=np.frombuffer(device[6016:10112],dtype='>f4').reshape(-1,4)
    material=np.concatenate([[*c[13,:2],float(ps[3,3]!=0),1],c[12],c[14],ps[2],ps[3]]).astype('<f4')
    matrix=transform.T.astype('<f4')
    if not all(np.isfinite(a).all() for a in (vertices,matrix,material)):raise ValueError('nonfinite world state')
    return vertices,matrix,material


def character_geometry(data,device,palette_bytes,source_ids,base,rigid=False,outline=False,colored=False,mask=False):
    source=np.frombuffer(data,dtype='>f4').reshape(-1,11)
    packed=np.frombuffer(data,dtype=np.uint8).reshape(-1,44);used=source_ids.astype(np.int64)+base
    if used.min()<0 or used.max()>=len(source) or (not rigid and len(palette_bytes)!=6144):raise ValueError('invalid character geometry/palette range')
    bone=np.zeros(len(used),dtype=np.uint8) if rigid else packed[used,19]
    weight=np.zeros(len(used),dtype='<f4') if rigid else source[used,3].astype('<f4')
    if bone.max()>127 or (weight<0).any() or (weight>1).any():raise ValueError('invalid character bone or weight')
    palette=np.zeros((128,3,4),dtype='<f4')
    if not rigid:
        for joint in np.unique(bone):palette[joint]=np.frombuffer(palette_bytes,dtype='>f4',count=12,offset=int(joint)*48).reshape(3,4)
    c=np.frombuffer(device[1920:6016],dtype='>f4').reshape(-1,4).astype('<f4')
    ps=np.frombuffer(device[6016:10112],dtype='>f4').reshape(-1,4).astype('<f4')
    if not outline and not colored and not mask and ps[14,3]!=1:raise ValueError('unvalidated character camera w')
    vertices=np.column_stack([source[used,:3],source[used,5:8],source[used,9:11],bone,weight,np.zeros((len(used),2)),np.ones(len(used))]).astype('<f4')
    material=np.concatenate([[*c[13,:2],float(ps[3,3]!=0),6 if rigid else 4],c[12],[*ps[4,:3],c[39,0]],[*ps[14,:3],ps[1,0]],ps[3]]).astype('<f4')
    palette=c[8:12].copy() if rigid else np.concatenate([c[8:12],palette.reshape(-1,4)]);matrix=c[:4].T.copy()
    if outline:
        material=np.zeros(20,dtype='<f4');material[3]=8 if rigid else 7;material[4]=c[16,0];material[8:12]=c[17]
    if colored:
        vertices[:,3:6]=np.column_stack([bone,weight,np.zeros(len(used))])
        vertices[:,8:12]=packed[used,32:36][:,[1,2,3,0]].astype('<f4')/255
        material=np.concatenate([[*c[13,:2],float(ps[3,3]!=0),10],c[12],c[14],ps[2],ps[3]]).astype('<f4')
    if mask:
        material=np.zeros(20,dtype='<f4');material[3]=12 if rigid else 11;material[8]=ps[255,0]
    if not all(np.isfinite(v).all() for v in (vertices,matrix,material,palette)):raise ValueError('nonfinite character state')
    return vertices,matrix,material,palette


def export(trace,frame,known,output,captured_after=0,characters=False,targets=False):
    characters=characters or targets
    trace,known,output=Path(trace),Path(known),Path(output)
    vs=(known/'shader_FDF960B5D7869030.ucode.bin.vert').read_bytes()
    ps=(known/'shader_1BFD0840B053E193.ucode.bin.frag').read_bytes()
    def variants(b):return (b,b''.join(b[i:i+4][::-1] for i in range(0,len(b),4)))
    vs,ps=variants(vs),variants(ps)
    cvs=variants((known/'shader_F3AC1AA2FE3EA253.ucode.bin.vert').read_bytes()) if characters else ()
    cps=variants((known/'shader_5F27AACEB38B1088.ucode.bin.frag').read_bytes()) if characters else ()
    rvs=variants((known/'shader_E9DD8CD3AADD7CF1.ucode.bin.vert').read_bytes()) if characters else ()
    outline_vs=variants((known/'shader_9B2CDBD500DB8645.ucode.bin.vert').read_bytes()) if characters else ()
    rigid_outline_vs=variants((known/'shader_8B5902B7B177B0B0.ucode.bin.vert').read_bytes()) if characters else ()
    outline_ps=variants((known/'shader_A4A965C189287B99.ucode.bin.frag').read_bytes()) if characters else ()
    colored_vs=variants((known/'shader_B4611D5E3A350359.ucode.bin.vert').read_bytes()) if characters else ()
    mask_vs=variants((known/'shader_DA9A3E04256563FF.ucode.bin.vert').read_bytes()) if targets else ()
    rigid_mask_vs=variants((known/'shader_176E0EF1D21F5A52.ucode.bin.vert').read_bytes()) if targets else ()
    mask_ps=variants((known/'shader_0BFD5B9C11C8FC07.ucode.bin.frag').read_bytes()) if targets else ()
    projected_vs=variants((known/'shader_4B41B10E837C6871.ucode.bin.vert').read_bytes()) if targets else ()
    projected_ps=variants((known/'shader_4228617C65DB9D17.ucode.bin.frag').read_bytes()) if targets else ()
    effect_vs=variants((known/'shader_E7F84460B7D340C5.ucode.bin.vert').read_bytes()) if targets else ()
    effect_ps=variants((known/'shader_CB0605B416DF85B8.ucode.bin.frag').read_bytes()) if targets else ()
    prefixes=[p.with_suffix('') for p in trace.glob(f'indexed-{frame}-*.json') if '-texture' not in p.name and p.stat().st_mtime>=captured_after]
    prefixes.sort(key=lambda p:int(p.name.rsplit('-',1)[1]))
    vertices=[];indices=[];draws=[];states=[];palettes=[];textures=[];texmap={};entries=[];proof=[];rejected=[];vcount=icount=0
    for prefix in prefixes:
        def binary(suffix):return Path(str(prefix)+suffix).read_bytes()
        try:
            vc,pc=binary('-vs-program.bin'),binary('-ps-program.bin')
            mask=vc in mask_vs+rigid_mask_vs and pc in mask_ps
            projected=vc in projected_vs and pc in projected_ps
            effect=vc in effect_vs and pc in effect_ps
            outline=vc in outline_vs+rigid_outline_vs and pc in outline_ps
            rigid=vc in rvs and pc in cps or outline and vc in rigid_outline_vs or mask and vc in rigid_mask_vs
            colored=vc in colored_vs and pc in ps
            character=rigid or mask or outline or colored or vc in cvs and pc in cps
            kind=7 if mask and rigid else 6 if mask else 8 if projected else 9 if effect else 4 if outline and rigid else 3 if outline else 5 if colored else 2 if rigid else 1 if character else 0
            if not character and not projected and not effect and (vc not in vs or pc not in ps):raise ValueError('unsupported shader semantics')
            meta=json.loads(prefix.with_suffix('.json').read_text())
            if meta['stride_hint']!=44 or meta['vertex_endian']!=2 or meta['index_stride']!=2 or meta['primitive'] not in (4,6):raise ValueError('unsupported vertex/index layout')
            raw_index=binary('-indices.bin');original=np.frombuffer(raw_index,dtype='>u2').astype(np.uint32)
            source_ids,idx=np.unique(original,return_inverse=True)
            if meta['primitive']==6:
                idx=np.array([v for i in range(len(idx)-2) for v in ((idx[i+1],idx[i],idx[i+2]) if i&1 else (idx[i],idx[i+1],idx[i+2]))],dtype='<u4')
            elif len(idx)%3:raise ValueError('incomplete triangles')
            device=binary('-device.bin');flags=target_pipeline(device) if targets else pipeline(device)
            raster=raster_bytes(device) if targets else b''
            if targets:
                pitch=struct.unpack_from('>I',device,10368)[0]&0x3fff
                if pitch!=(560 if mask else 1280):raise ValueError('separate postprocess target is not integrated')
                if mask:flags|=0x100000
                if projected:flags|=0x200000
            texture_data=[]
            for slot in range(0 if outline or mask or projected or effect else 2 if character and not colored else 1):
                fetch=json.loads(Path(str(prefix)+f'-texture{slot}.json').read_text());raw_texture=binary(f'-texture{slot}.bin');texture,desc=decode(raw_texture,fetch)
                linear,_=linearize(raw_texture,fetch);identity=fnv(linear+struct.pack('<I',desc['swizzle']))
                texture_data.append((fetch,raw_texture,texture,desc,identity))
            if character:v,m,material,palette=character_geometry(binary('-vertices.bin'),device,None if rigid else binary('-palette.bin'),source_ids,meta['base_vertex'],rigid,outline,colored,mask)
            else:v,m,material=geometry(binary('-vertices.bin'),device,source_ids,meta['base_vertex']);palette=np.empty((0,4),dtype='<f4')
            if projected:
                c=np.frombuffer(device[1920:6016],dtype='>f4').reshape(-1,4).astype('<f4')
                world=np.column_stack([v[:,:3],np.ones(len(v),dtype='<f4')])@c[8:12]
                v[:,6:8]=world@c[18:22,:2];v[:,12]=1
                material=np.zeros(20,dtype='<f4');material[3]=2;material[8:12]=c[14];material[12:16]=np.frombuffer(device[6048:6064],dtype='>f4')
                fetch=struct.unpack_from('>6I',device,0x480)
                if fetch[1]&63!=6 or (fetch[2]&8191)+1!=512 or ((fetch[2]>>13)&8191)+1!=512 or fetch[1]&0xfffff000!=struct.unpack_from('>I',device,10780)[0]:raise ValueError('unvalidated projected mask binding')
                if (fetch[0]>>10&7,fetch[0]>>13&7,fetch[3]>>19&3,fetch[3]>>21&3)!=(2,2,0,0):raise ValueError('unvalidated projected mask sampler')
        except (ValueError,FileNotFoundError) as e:
            rejected.append({'source':prefix.name,'reason':str(e)});continue
        texture_indices=[]
        if effect:
            key=('white',1,1)
            if key not in texmap:texmap[key]=len(textures);textures.append((1,1,bytes([255]*4)))
            texture_indices.append(texmap[key])
        for fetch,raw_texture,texture,desc,identity in texture_data:
            key=(identity,texture.width,texture.height)
            if key not in texmap:texmap[key]=len(textures);textures.append((texture.width,texture.height,texture.tobytes()))
            texture_indices.append(texmap[key])
        draws.append(struct.pack('<5I3f',icount,len(idx),texture_indices[0] if texture_indices else 0xffffffff,texture_indices[1] if len(texture_indices)>1 else 0xffffffff,0,*([.5,1,.99] if kind in (1,2) else [1,1,1])))
        states.append(m.tobytes()+material.tobytes()+struct.pack('<I',flags)+raster)
        texture_fields=[0]*4;identity=0
        if texture_data:
            fetch,raw_texture,texture,desc,identity=texture_data[0];texture_fields=[len(raw_texture),texture.width,texture.height,desc['format']]
        entry=struct.pack('<4Q13I',fnv(vc),fnv(pc),fnv(raw_index),identity,meta['primitive'],44,2,meta['count'],vcount,len(v),flags,*texture_fields,len(vc),len(pc))
        if characters:
            if kind in (1,2):
                _,second,image,descriptor,second_identity=texture_data[1]
                entry+=struct.pack('<Q5I',second_identity,len(second),image.width,image.height,descriptor['format'],kind)
            else:entry+=struct.pack('<Q5I',0,0,0,0,0,kind)
        entries.append(entry+raster+source_ids.astype('<u4').tobytes())
        palettes.append(palette)
        vertices.append(v);indices.append(idx+vcount);vcount+=len(v);icount+=len(idx)
        proof.append({'source':prefix.name,'vertices':len(v),'triangles':len(idx)//3,'flags':flags,'source_base_vertex':meta['base_vertex'],'kind':{0:'world',1:'character',2:'rigid',3:'skin_outline',4:'rigid_outline',5:'skin_color',6:'skin_mask',7:'rigid_mask',8:'projected_shadow',9:'untextured_effect'}[kind]})
    if not vertices:raise ValueError('No supported indexed draws captured: '+str(collections.Counter(r['reason'] for r in rejected)))
    v=np.concatenate(vertices);idx=np.concatenate(indices).astype('<u4');bounds=np.concatenate([v[:,:3].min(0),v[:,:3].max(0)]).astype('<f4')
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('wb') as f:
        f.write((b'DBZNGPU4' if targets else b'DBZNGPU3' if characters else b'DBZNGPU2')+struct.pack('<4I',len(v),len(idx),len(draws),len(textures))+bounds.tobytes()+struct.pack('<2I',1280,720))
        # Checked stage capture starts at depth 1 / stencil 1 (GPU oracle).
        # Independent targets preserve this depth/stencil across mask groups.
        # Other game scenes need their own clear intent, not this stage policy.
        if targets:f.write(struct.pack('<5f2I',.025,.038,.07,1,1,1,7))
        f.write(v.tobytes());f.write(idx.tobytes());f.write(b''.join(draws));f.write(b''.join(states))
        if characters:
            for palette in palettes:f.write(struct.pack('<I',len(palette))+palette.astype('<f4').tobytes())
        for w,h,data in textures:f.write(struct.pack('<3I',w,h,len(data))+data)
    output.with_suffix('.indexed').write_bytes((b'DBZIDX04' if targets else b'DBZIDX03' if characters else b'DBZIDX02')+struct.pack('<I',len(entries))+b''.join(entries))
    report={'scope':'Partial indexed component from CPU source geometry. V4 optionally adds own masks and typed raster states. UP restore passes, depth/stencil restoration and separate postprocess targets are not integrated; projected shadows are not final-frame validated. Reference GPU remains active.','version':4 if targets else 3 if characters else 2,'draws':proof,'omitted':rejected,'vertices':len(v),'triangles':len(idx)//3,'textures':len(textures)}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'draws':len(draws),'vertices':len(v),'triangles':len(idx)//3,'omitted':len(rejected),'reasons':dict(collections.Counter(r['reason'] for r in rejected))},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--trace',required=True);p.add_argument('--frame',required=True,type=int);p.add_argument('--known-shaders',required=True);p.add_argument('--output',required=True);p.add_argument('--captured-after',type=float,default=0);p.add_argument('--characters',action='store_true');p.add_argument('--targets',action='store_true');a=p.parse_args();export(a.trace,a.frame,a.known_shaders,a.output,a.captured_after,a.characters,a.targets)

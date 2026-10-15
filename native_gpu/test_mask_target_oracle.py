"""Compare an entire native-produced mask to the original GPU target texture."""
import argparse,json,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from export_mask_frame import convert
from export_character_frame import character_textures
from native_scene import NativeDraw,write_scene


def run(reference,texture_reference,directory,scratch,event=2771):
    reference,texture_reference,directory,scratch=map(lambda p:Path(p).resolve(),(reference,texture_reference,directory,scratch));scratch.mkdir(parents=True,exist_ok=True)
    rows=json.loads((reference/'frame.json').read_text())['draws'];texture_rows=json.loads((texture_reference/'frame.json').read_text())['draws']
    projected=sorted(r['event'] for r in texture_rows)
    rows=[row for row in rows if next((e for e in projected if e>row['event']),None)==event]
    assert rows,'No mask producers for this projected event'
    draws=[convert(reference,row)[0] for row in rows]
    texture_row=next(row for row in texture_rows if row['event']==event)
    expected=Image.open(character_textures(texture_reference,texture_row,1)[0]).convert('RGBA')
    assert expected.size==(512,512)
    for draw in draws:
        draw.flags|=0x100000;draw.viewport=(0,0,512,512,0,1);draw.scissor=(0,0,512,512)
    vertices=np.array([[x,y,0,0,0,0,(x+1)/2,(1-y)/2,1,1,1,1,1] for x,y in ((-1,-1),(-1,1),(1,-1),(1,1))],dtype='<f4')
    indices=np.array([0,1,2,2,1,3],dtype='<u4');material=np.zeros(20,dtype='<f4');material[3]=1;material[8:12]=1
    final=NativeDraw(vertices,indices,np.eye(4,dtype='<f4'),material,0x200000|0xf0000|0x700)
    metrics={}
    for channel in ('rgb','alpha'):
        if channel=='alpha':final.material[3]=2;final.material[12:16]=1;final.flags|=0x10
        scene=scratch/('mask-'+channel+'.dbzgpu');capture=scene.with_suffix('.ppm')
        write_scene(scene,[*draws,final],512,512,version=4,clear=(0,0,0,0,1,0,7))
        subprocess.run([str(directory/'dbz3-native-gpu.exe'),'--model',str(scene),'--shaders',str(directory/'materials.hlsl'),'--capture',str(capture)],check=True,capture_output=True)
        actual=np.array(Image.open(capture)).astype(int);target=np.array(expected).astype(int)
        if channel=='rgb':target=target[:,:,:3]
        else:target=np.repeat(target[:,:,3,None],3,axis=2)
        delta=abs(actual-target);metrics[channel]={'mean_error':float(delta.mean()),'different_pixels':int((delta.max(2)>2).sum()),'max_error':int(delta.max())}
        Image.fromarray(actual.astype('uint8')).save(capture.with_suffix('.png'))
        Image.fromarray(target.astype('uint8')).save(scratch/('original-mask-'+channel+'.png'))
        assert metrics[channel]['mean_error']<.05 and metrics[channel]['different_pixels']<=50,metrics
    result={'scope':'Entire original GPU mask texture versus native mask produced from original CPU mesh/palette data, then sampled from the native target. No reference mask used as native input. One checked actor/pose, not final-game frame fidelity.','reference_projected_event':event,'mask_draws':len(draws),'vertices':sum(len(d.vertices) for d in draws),'metrics':metrics}
    (scratch/'mask-target-oracle.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--reference',required=True);p.add_argument('--texture-reference',required=True);p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);p.add_argument('--event',type=int,default=2771);a=p.parse_args();run(a.reference,a.texture_reference,a.directory,a.scratch,a.event)

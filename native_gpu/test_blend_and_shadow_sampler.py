"""Hardware checks of additive alpha, separate alpha factors and mask clamping."""
import argparse,json,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from native_scene import NativeDraw,write_scene


def run(directory,scratch):
    directory,scratch=map(lambda p:Path(p).resolve(),(directory,scratch));scratch.mkdir(parents=True,exist_ok=True)
    idx=np.array([0,1,2,2,1,3],dtype='<u4');matrix=np.eye(4,dtype='<f4')
    def vertices(color,left=-1,right=1,uv_min=0,uv_max=1):return np.array([[x,y,0,0,0,0,u,(1-y)/2,*color,1] for x,y,u in ((left,-1,uv_min),(left,1,uv_min),(right,-1,uv_max),(right,1,uv_max))],dtype='<f4')
    def material(mode):m=np.zeros(20,dtype='<f4');m[3]=mode;m[8:12]=1;return m
    def mask(color,flags,left=-1,right=1):return NativeDraw(vertices(color,left,right),idx,matrix,material(3),0x100000|0xf0000|0x700|flags,viewport=(0,0,64,64,0,1),scissor=(0,0,64,64))
    def capture(name,draws):
        path=scratch/(name+'.dbzgpu');ppm=path.with_suffix('.ppm');write_scene(path,draws,128,64,version=4,clear=(0,0,0,0,1,0,7))
        subprocess.run([str(directory/'dbz3-native-gpu.exe'),'--model',str(path),'--shaders',str(directory/'materials.hlsl'),'--capture',str(ppm)],check=True,capture_output=True)
        im=np.array(Image.open(ppm));Image.fromarray(im).save(ppm.with_suffix('.png'));return im
    passed=[]
    for name,alpha_flag,alpha in (('zero',0x1000,.5),('one',0x800,.8),('inverse',0x2000,.65)):
        draws=[mask((.2,.4,.6,.3),0),mask((.8,.2,.1,.5),0x40|alpha_flag)]
        final=NativeDraw(vertices((1,1,1,1)),idx,matrix,material(1),0x200000|0xf0000|0x700)
        image=capture('additive-rgb-'+name,[*draws,final]);assert np.max(abs(image[32,64].astype(int)-np.array([153,128,166])))<=2,(name,image[32,64])
        final.material[3]=2;final.material[12:16]=1;final.flags|=0x10
        image=capture('additive-alpha-'+name,[*draws,final]);assert np.max(abs(image[32,64].astype(int)-round(alpha*255)))<=2,(name,image[32,64])
        passed.append('source-alpha additive RGB with '+name+' destination-alpha factor')
    final=NativeDraw(vertices((1,1,1,1)),idx,matrix,material(1),0x200000|0xf0000|0x700)
    image=capture('reverse-subtract',[mask((.8,.6,.4,.3),0),mask((.6,.4,.2,.5),0x50|0x800),final])
    assert np.max(abs(image[32,64].astype(int)-np.array([128,102,77])))<=2,image[32,64]
    final.material[3]=2;final.material[12:16]=1;final.flags|=0x10
    image=capture('reverse-subtract-alpha',[mask((.8,.6,.4,.3),0),mask((.6,.4,.2,.5),0x50|0x800),final])
    assert np.max(abs(image[32,64].astype(int)-204))<=2,image[32,64]
    passed+=['reverse-subtract RGB keeps its separate additive alpha equation']
    final=NativeDraw(vertices((1,1,1,1)),idx,matrix,material(1),0x200000|0xf0000|0x700)
    image=capture('subtract-source-alpha-destination',[mask((.8,.6,.4,.3),0),mask((.6,.4,.2,.5),0x60|0x800),final])
    assert np.max(abs(image[32,64].astype(int)-np.array([51,26,0])))<=2,image[32,64]
    final.material[3]=2;final.material[12:16]=1;final.flags|=0x10
    image=capture('subtract-separate-alpha',[mask((.8,.6,.4,.3),0),mask((.6,.4,.2,.5),0x60|0x800),final])
    assert np.max(abs(image[32,64].astype(int)-204))<=2,image[32,64]
    passed+=['source ONE minus destination SRC_ALPHA RGB with separate additive alpha']
    final=NativeDraw(vertices((1,1,1,1),uv_min=-1,uv_max=2),idx,matrix,material(2),0x200000|0xf0000|0x700|0x10);final.material[12:16]=1
    image=capture('shadow-clamp',[mask((1,1,1,1),0,left=-.5,right=.5),final])
    for x,value in ((10,0),(25,0),(50,0),(64,255),(77,0),(100,0),(120,0)):assert np.max(abs(image[32,x].astype(int)-value))<=1,(x,image[32,x])
    assert set(np.unique(image[32]))=={0,255},'Point sampling must not soften the checked mask edge'
    passed+=['projected mask does not repeat outside [0,1]','checked point sampling preserves hard mask edges']
    result={'scope':'Synthetic hardware checks of original blend factors and checked point/clamp mask semantics; not complete-game fidelity.','passed':passed};(scratch/'blend-sampler-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

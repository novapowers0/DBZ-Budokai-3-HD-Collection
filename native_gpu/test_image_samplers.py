"""GPU image addressing against independent integer/linear CPU sampling."""
import argparse,itertools,json,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from native_scene import NativeDraw,write_scene


def run(directory,scratch):
    directory,scratch=map(lambda p:Path(p).resolve(),(directory,scratch));scratch.mkdir(parents=True,exist_ok=True)
    yy,xx=np.indices((8,8));pixels=np.stack([xx*31,yy*29,(xx+yy)*15,np.full_like(xx,255)],2).astype(np.uint8)
    vertices=np.array([[x,y,.5,0,0,0,u,v,1,1,1,1,1] for x,y,u,v in ((-1,-1,-.75,1.5),(-1,1,-.75,-.5),(1,-1,1.75,1.5),(1,1,1.75,-.5))],dtype='<f4')
    indices=np.array([0,1,2,2,1,3],dtype='<u4');y,x=np.indices((64,128));u=-.75+2.5*(x+.5)/128;v=-.5+2*(y+.5)/64
    results=[]
    for address,v_address in itertools.product(('wrap','clamp','mirror'),repeat=2):
        def index(values,mode):
            if mode=='wrap':return values%8
            if mode=='clamp':return np.clip(values,0,7)
            reflected=values%16;return np.where(reflected<8,reflected,15-reflected)
        for point in (False,True):
            if point:expected=pixels[index(np.floor(v*8).astype(int),v_address),index(np.floor(u*8).astype(int),address),:3].astype(float)
            else:
                tx=u*8-.5;ty=v*8-.5;ix=np.floor(tx).astype(int);iy=np.floor(ty).astype(int);fx=(tx-ix)[:,:,None];fy=(ty-iy)[:,:,None]
                a=pixels[index(iy,v_address),index(ix,address),:3].astype(float);b=pixels[index(iy,v_address),index(ix+1,address),:3].astype(float);c=pixels[index(iy+1,v_address),index(ix,address),:3].astype(float);d=pixels[index(iy+1,v_address),index(ix+1,address),:3].astype(float)
                expected=(a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy
            material=np.zeros(20,dtype='<f4');material[3]=15;material[4]=int(point);material[5]={'wrap':0,'clamp':1,'mirror':2}[address];material[8:12]=1
            material[6]={'wrap':1,'clamp':2,'mirror':3}[v_address]
            draw=NativeDraw(vertices,indices,np.eye(4,dtype='<f4'),material,0xf0700,textures=(Image.fromarray(pixels),))
            scene=scratch/f'{address}-{v_address}-{int(point)}.dbzgpu';capture=scene.with_suffix('.ppm');write_scene(scene,[draw],128,64,version=4)
            subprocess.run([str(directory/'dbz3-native-gpu.exe'),'--model',str(scene),'--shaders',str(directory/'materials.hlsl'),'--capture',str(capture)],check=True,capture_output=True)
            actual=np.array(Image.open(capture));delta=abs(actual.astype(int)-np.rint(expected).astype(int));maximum=int(delta.max());assert maximum<=1,(address,point,maximum)
            results.append(dict(address_u=address,address_v=v_address,point=point,max_pixel_error=maximum))
    result=dict(scope='Independent CPU texel addressing/filtering versus actual D3D12 sampling across negative UV, interior texels and repeated edges. Tests wrap, clamp and mirror with point and bilinear filters.',cases=results)
    (scratch/'image-sampler-tests.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--scratch',required=True);a=p.parse_args();run(a.directory,a.scratch)

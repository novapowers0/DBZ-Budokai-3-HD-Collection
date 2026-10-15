"""Exhaustive packed RGBA4 values plus tiled/endian/packed-tail addressing."""
import argparse,ctypes as C,json
from pathlib import Path
import numpy as np
from texture_decode import tiled_offset


def run(bridge,output):
    api=C.CDLL(str(Path(bridge).resolve()));api.replay_linearize.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32),C.c_void_p,C.c_uint32]
    cases=[]
    for width,height,packed in ((256,256,False),(32,8,True),(8,32,True),(8,8,True)):
        values=np.arange(width*height,dtype=np.uint32).reshape(height,width)
        if packed:values=(values*257+0x1357)&65535
        expected=(((values[:,:,None]>>np.array([0,4,8,12]))&15)*17).astype(np.uint8).tobytes()
        for tiled in (False,True):
            pitch=max(32,width);px=16 if packed and width<=height else 0;py=16 if packed and width>height else 0
            rows=height+py;rows=(rows+31)&~31 if tiled else rows
            size=pitch*rows*2
            if tiled:size=(size+4095)&~4095
            raw=np.zeros(size,dtype=np.uint8)
            y,x=np.indices((height,width),dtype=np.int64);offset=tiled_offset(x+px,y+py,pitch,1) if tiled else ((y+py)*pitch+x+px)*2
            raw[offset]=(values&255).astype(np.uint8);raw[offset+1]=(values>>8).astype(np.uint8)
            for endian in range(4):
                mask=(0,1,3,2)[endian];source=raw[np.arange(len(raw))^mask].tobytes()
                words=[2|((pitch//32)<<22)|(int(tiled)<<31),0x100000|15|(endian<<6),(width-1)|((height-1)<<13),0x688<<1,0,(1<<9)|(int(packed)<<11)]
                fetch=(C.c_uint32*6)(*words);src=C.create_string_buffer(source);dst=C.create_string_buffer(len(expected))
                count=api.replay_linearize(src,len(source),fetch,dst,len(expected))
                assert count==len(expected) and dst.raw==expected,(width,height,tiled,endian,count)
                assert not api.replay_linearize(src,len(source),fetch,dst,len(expected)-1)
                assert not api.replay_linearize(src,1,fetch,dst,len(expected))
                cases.append(dict(width=width,height=height,tiled=tiled,endian=endian,packed_tail=packed))
    result=dict(scope='All 65536 RGBA4 words preserve all four UNORM channels exactly in RGBA8. Independent expected channels, all endian modes, linear/tiled addressing, horizontal/vertical packed tails, and truncated input/output rejection.',cases=cases,passed=len(cases))
    Path(output).write_text(json.dumps(result,indent=2));print('RGBA4 cases passed:',len(cases))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--bridge',required=True);p.add_argument('--output',required=True);a=p.parse_args();run(a.bridge,a.output)

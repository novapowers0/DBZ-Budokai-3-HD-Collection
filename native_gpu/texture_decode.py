# CPU asset decoding for the native path. Tiled addressing follows the BSD
# texture_util::GetTiledOffset2D algorithm in the project's ReXGlue SDK.
import io,struct

def tiled_offset(x,y,pitch,log2_bytes):
    pitch=(pitch+31)&~31
    macro=((x>>5)+(y>>5)*(pitch>>5))<<(log2_bytes+7)
    micro=((x&7)+((y&14)<<2))<<log2_bytes
    offset=macro+((micro&~15)<<1)+(micro&15)+((y&1)<<4)
    return ((offset&~511)<<3)+((y&16)<<7)+((offset&448)<<2)+(((((y&8)>>2)+(x>>3))&3)<<6)+(offset&63)

def describe(words):
    if len(words)!=6 or words[0]&3!=2:raise ValueError('not a bound texture')
    fmt=words[1]&63;block=4 if fmt in (18,19,20) else 1
    if fmt not in (2,6,15,18,19,20):raise ValueError(f'unsupported texture format {fmt}')
    w=(words[2]&8191)+1;h=(words[2]>>13&8191)+1
    if w>4096 or h>4096:raise ValueError('texture dimensions exceed native decoder limit')
    if (words[1]>>10&1) or (words[5]>>9&3)!=1:raise ValueError('only non-stacked 2D textures are supported')
    # Base level of a packed tail occupies the 16-texel half of its tile.
    # Wider-than-tall tails pack along Y; square/tall tails pack along X.
    # This is GetPackedMipOffset(..., mip=0), in format blocks.
    packed_x=packed_y=0
    if words[5]>>11&1 and min(w,h)<=16:
        if (w-1).bit_length()>(h-1).bit_length():packed_y=16//block
        else:packed_x=16//block
    bpp=1 if fmt==2 else 2 if fmt==15 else 8 if fmt==18 else 16 if fmt in (19,20) else 4
    pitch=((words[0]>>22&511)*32+block-1)//block
    if pitch<(w+block-1)//block:raise ValueError('texture pitch shorter than width')
    return dict(width=w,height=h,format=fmt,block=block,bpp=bpp,pitch=pitch,tiled=bool(words[0]>>31),endian=words[1]>>6&3,address=words[1]&0xfffff000,swizzle=words[3]>>1&4095,packed_x=packed_x,packed_y=packed_y)

def linearize(raw,words):
    import numpy as np
    d=describe(words);w=(d['width']+d['block']-1)//d['block'];h=(d['height']+d['block']-1)//d['block'];b=d['bpp'];p=d['pitch']
    data=np.frombuffer(raw,dtype=np.uint8)
    if d['endian']==1:
        if len(data)%2:raise ValueError('unaligned 16-bit texture bytes')
        data=data.reshape(-1,2)[:,::-1].copy().reshape(-1)
    elif d['endian'] in (2,3):
        if len(data)%4:raise ValueError('unaligned 32-bit texture bytes')
        data=data.reshape(-1,4)[:,[3,2,1,0] if d['endian']==2 else [2,3,0,1]].copy().reshape(-1)
    y,x=np.indices((h,w),dtype=np.int64)
    x+=d['packed_x'];y+=d['packed_y']
    offset=tiled_offset(x,y,p,(b.bit_length()-1)) if d['tiled'] else (y*p+x)*b
    if int(offset.max())+b>len(data):raise ValueError('truncated texture allocation')
    linear=data[offset[:,:,None]+np.arange(b)].tobytes()
    if d['format']==15:
        packed=np.frombuffer(linear,dtype='<u2').astype(np.uint32)
        linear=(((packed[:,None]>>np.array([0,4,8,12]))&15)*17).astype(np.uint8).tobytes()
        d['source_format']=15;d['format']=6
    return linear,d

def decode(raw,words,apply_swizzle=True):
    import numpy as np
    from PIL import Image
    linear,d=linearize(raw,words);w,h=d['width'],d['height']
    if d['format']==2:
        channel=Image.frombytes('L',(w,h),linear);image=Image.merge('RGBA',(channel,channel,channel,channel))
    elif d['format']==6:image=Image.frombytes('RGBA',(w,h),linear)
    else:
        fourcc={18:b'DXT1',19:b'DXT3',20:b'DXT5'}[d['format']]
        header=struct.pack('<7I',124,0x81007,h,w,len(linear),0,0)+bytes(44)+struct.pack('<2I4s5I',32,4,fourcc,0,0,0,0,0)+struct.pack('<5I',0x1000,0,0,0,0)
        image=Image.open(io.BytesIO(b'DDS '+header+linear)).convert('RGBA')
    if apply_swizzle:
        rgba=np.asarray(image);channels=[rgba[:,:,i] for i in range(4)]+[np.zeros((h,w),dtype=np.uint8),np.full((h,w),255,dtype=np.uint8)]
        select=[d['swizzle']>>(3*i)&7 for i in range(4)]
        if max(select)>5:raise ValueError('invalid texture swizzle')
        image=Image.fromarray(np.stack([channels[i] for i in select],axis=2))
    return image,d

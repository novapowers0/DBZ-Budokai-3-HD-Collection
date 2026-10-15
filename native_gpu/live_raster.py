"""Convert captured CPU state to explicit native raster data, outside D3D12."""
import math,struct

DISABLED_STENCIL=bytes([0,0,255,255,7,0,0,0,7,0,0,0])


def decode(device):
    u=lambda offset:struct.unpack_from('>I',device,offset)[0]
    depth,front,back,raster=u(10548),u(10496),u(10492),u(10568)
    stencil=DISABLED_STENCIL
    if depth&1:
        def face(shift):return [(depth>>shift)&7,(depth>>(shift+3))&7,(depth>>(shift+9))&7,(depth>>(shift+6))&7]
        a=face(8);b=face(20) if depth&128 else a
        if depth&128 and (front&0xffffff)!=(back&0xffffff):raise ValueError('different front/back stencil references or masks')
        if raster&4:a,b=b,a
        stencil=bytes([1,front&255,front>>8&255,front>>16&255,*a,*b])
    color=u(10556);alpha=(1,color&7,struct.unpack_from('>f',device,10500)[0]) if color&8 else (0,7,0.)
    if not math.isfinite(alpha[2]):raise ValueError('nonfinite alpha-test reference')
    xs,xo,ys,yo,zs,zo=struct.unpack_from('>6f',device,10504)
    if not all(math.isfinite(v) for v in (xs,xo,ys,yo,zs,zo)) or xs<0 or ys>0 or zs<0 or zo<0 or zo+zs>1:raise ValueError('unsupported viewport orientation or depth range')
    viewport=(xo-abs(xs),yo-abs(ys),2*abs(xs),2*abs(ys),zo,zo+zs)
    def signed(v):return (v&0x3fff)-(v&0x4000)
    def screen(v):return signed(v),signed(v>>16)
    left,top=screen(u(10424));right,bottom=screen(u(10428))
    window=u(10436);end=u(10440);offset=(0,0) if window&0x80000000 else screen(u(10432))
    left=max(left,(window&0x3fff)+offset[0]);top=max(top,((window>>16)&0x3fff)+offset[1])
    right=max(left,min(right,(end&0x3fff)+offset[0]));bottom=max(top,min(bottom,((end>>16)&0x3fff)+offset[1]))
    return stencil,alpha,viewport,(left,top,right,bottom)


def encode(device):
    stencil,alpha,viewport,scissor=decode(device)
    return stencil+struct.pack('<BBHf',*alpha[:2],0,alpha[2])+struct.pack('<6f4i',*viewport,*scissor)


def pipeline(device):
    # The V2 helper still rejects stencil for callers without typed raster data.
    from export_live_ui import pipeline as legacy
    copy=bytearray(device);struct.pack_into('>I',copy,10548,struct.unpack_from('>I',copy,10548)[0]&~1)
    return legacy(copy,explicit_alpha=True)

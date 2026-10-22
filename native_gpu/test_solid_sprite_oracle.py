"""Native CPU adaptation of the solid screen material versus original GPU output."""
import argparse,ctypes as C,json,struct
from pathlib import Path
import numpy as np
from test_streaming_frame import FrameDraw


def run(bridge,reference,output):
    reference=Path(reference);api=C.CDLL(str(Path(bridge).resolve()));api.replay_sprite.argtypes=[C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.c_void_p,C.POINTER(FrameDraw)]
    rows=json.loads((reference/'frame.json').read_text())['draws'];reports=[]
    for row in rows:
        if '57152B8FFBB31741' not in (row['stages']['ShaderStage.Vertex'].get('dump_name') or ''):continue
        assert 'CB0605B416DF85B8' in row['stages']['ShaderStage.Pixel']['dump_name']
        def block(stage,name):return (reference/next(c['file'] for c in row['stages']['ShaderStage.'+stage]['constants'] if c['name']==name)).read_bytes()
        source=np.frombuffer(block('Vertex','xe_float_cbuffer'),dtype='<f4').reshape(-1,4);pixels=np.frombuffer(block('Pixel','xe_float_cbuffer'),dtype='<f4').reshape(-1,4)
        assert len(source)==6 and len(pixels)==2,(source,pixels)
        device=bytearray(0x6400)
        for register,value in zip((0,1,2,3,12,255),source):struct.pack_into('>4f',device,1920+register*16,*value)
        for register,value in zip((2,3),pixels):struct.pack_into('>4f',device,6016+register*16,*value)
        raw=(reference/next(f['file'] for f in row['vertex_fetches'] if f['slot']==95)).read_bytes();count=len(raw)//16
        vertices=np.empty((count,13),dtype='<f4');draw=FrameDraw();assert api.replay_sprite(C.create_string_buffer(raw),count,C.create_string_buffer(bytes(device)),15,vertices.ctypes.data,C.byref(draw))
        post=np.fromfile(reference/f"e{row['event']}-postvs.bin",dtype='<f4').reshape(-1,row['postvs']['vertexByteStride']//4);assert count==len(post)
        projected=np.column_stack([vertices[:,:3],np.ones(count)])@np.array(draw.view_projection).reshape(4,4).T
        system=block('Vertex','xe_system_cbuffer');scale=np.array(struct.unpack_from('<3f',system,128));offset=np.array(struct.unpack_from('<3f',system,144));projected[:,:3]=projected[:,:3]*scale+projected[:,3,None]*offset
        errors=dict(clip=float(abs(projected-post[:,:4]).max()),color=float(abs(vertices[:,8:12]-post[:,4:8]).max()),fog=float(abs(vertices[:,12]-post[:,8]).max()))
        assert errors['clip']<.0001 and errors['color']<.000001 and errors['fog']<.000001,errors
        reports.append(dict(event=row['event'],vertices=count,errors=errors))
    assert reports
    result=dict(scope='Production solid-screen CPU geometry/material adaptation checked independently against original GPU post-VS position, vertex color and fog. Source input is original pre-transform vertex data and material constants; post-VS data used only for comparison.',draws=reports)
    Path(output).parent.mkdir(parents=True,exist_ok=True);Path(output).write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--bridge',required=True);p.add_argument('--reference',required=True);p.add_argument('--output',required=True);a=p.parse_args();run(a.bridge,a.reference,a.output)

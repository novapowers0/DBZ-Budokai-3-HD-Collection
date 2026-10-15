"""Cross-language raster conversion using real device states and edge cases."""
import argparse,ctypes as C,json,struct
from pathlib import Path
from live_raster import decode,encode


def run(bridge,trace,output):
    api=C.CDLL(str(Path(bridge).resolve()));api.replay_raster.argtypes=[C.c_void_p,C.c_void_p]
    def convert(data):
        buf=C.create_string_buffer(bytes(data));out=C.create_string_buffer(60)
        ok=api.replay_raster(buf,out)
        try:expected=encode(data)
        except ValueError:assert not ok;return None
        assert ok and out.raw==expected,(ok,out.raw.hex(),expected.hex())
        return decode(data)
    files=list(Path(trace).glob('*-device.bin'));states=set()
    for file in files:
        data=file.read_bytes();convert(data);states.add(encode(data))
    assert any(s[0] for s in states) and any(s[12] for s in states),'Need real stencil and alpha-test states'
    assert len({s[20:44] for s in states})>=3,'Need real main, mask and postprocess viewports'
    data=bytearray(files[0].read_bytes())
    def word(offset,value):struct.pack_into('>I',data,offset,value)
    word(10424,0);word(10428,(8192<<16)|8192);word(10432,(6<<16)|0x7ffc)
    word(10436,(20<<16)|10);word(10440,(70<<16)|100)
    assert convert(data)[3]==(6,26,96,76),'signed window offset'
    word(10436,0x80000000|(20<<16)|10)
    assert convert(data)[3]==(10,20,100,70),'window offset disable'
    word(10424,(30<<16)|40);word(10428,(60<<16)|80)
    assert convert(data)[3]==(40,30,80,60),'screen/window intersection'
    word(10436,0x80000000|(100<<16)|100)
    assert convert(data)[3]==(100,100,100,100),'empty scissor'
    word(10548,1|(2<<8)|(2<<14));word(10496,0x55aa23);word(10568,0)
    s=convert(data)[0];assert s==bytes([1,0x23,0xaa,0x55,2,0,0,2,2,0,0,2])
    word(10548,1|128|(2<<8)|(2<<14)|(7<<20)|(4<<26));word(10492,0x55aa23)
    s=convert(data)[0];assert s[4:8]==bytes([2,0,0,2]) and s[8:]==bytes([7,0,0,4])
    word(10568,4);s=convert(data)[0];assert s[4:8]==bytes([7,0,0,4]) and s[8:]==bytes([2,0,0,2])
    word(10492,0x55aa24);assert convert(data) is None
    word(10548,0);word(10556,0);word(10500,0x7fc00000);assert convert(data)[1]==(0,7,0.)
    word(10556,8);assert convert(data) is None
    word(10556,0);word(10504,0xbf800000);assert convert(data) is None
    result={'captured_states_checked':len(files),'distinct_native_states':len(states),'edge_cases':['signed offset','offset disable','scissor intersection and empty scissor','stencil face selection and winding','different face masks/references rejected','inactive stale alpha NaN ignored','active NaN and unsupported viewport rejected'],'scope':'CPU raster conversion and native ABI agreement; not complete-frame fidelity.'}
    Path(output).write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--bridge',required=True);p.add_argument('--trace',required=True);p.add_argument('--output',required=True);a=p.parse_args();run(a.bridge,a.trace,a.output)

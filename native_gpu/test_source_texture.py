"""Compare C++ CPU tile/endian/packed addressing to the independent decoder."""
import argparse,ctypes as C,hashlib,json,collections
from pathlib import Path
from texture_decode import linearize


def run(bridge,traces,output):
    api=C.CDLL(str(Path(bridge).resolve()));api.replay_linearize.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32),C.c_void_p,C.c_uint32]
    checked=0;seen=set();formats=collections.Counter();packed=0;signed_rejections=0
    for trace in map(Path,traces):
        for path in trace.glob('*-texture*.json'):
            try:words=json.loads(path.read_text());raw=path.with_suffix('.bin').read_bytes();expected,layout=linearize(raw,words)
            except (ValueError,OSError):continue
            key=(tuple(words),hashlib.sha256(raw).digest())
            if key in seen:continue
            seen.add(key);src=C.create_string_buffer(raw);dst=C.create_string_buffer(len(expected));fetch=(C.c_uint32*6)(*words)
            count=api.replay_linearize(src,len(raw),fetch,dst,len(expected))
            if words[0]&0x3fc:assert not count;signed_rejections+=1;continue
            assert count==len(expected) and dst.raw==expected,path
            if checked==0:assert not api.replay_linearize(src,len(raw),fetch,dst,max(0,len(expected)-1))
            checked+=1;formats[layout['format']]+=1;packed+=bool(layout['packed_x'] or layout['packed_y'])
    assert checked>=10 and packed>0,(checked,packed)
    result={'scope':'C++ CPU asset linearization, not decompression. Exact bytes against independent Python addressing for real captured textures, including endian, tiles and packed tails.','unique_captured_texture_states':checked,'formats':dict(formats),'packed_states':packed,'unsupported_signed_states_rejected':signed_rejections}
    Path(output).write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--bridge',required=True);p.add_argument('--traces',nargs='+',required=True);p.add_argument('--output',required=True);a=p.parse_args();run(a.bridge,a.traces,a.output)

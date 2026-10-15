"""CPU emitter oracle for native buffer-completion and display-counter ABIs.

This is a private diagnostic oracle, not a native packet execution path. The
production service adapter only receives high-level CPU arguments. Samples
contain original CPU command-emission bytes and are deliberately not packaged.
"""
import argparse,json,struct,time
from pathlib import Path


def run(directory,process,scratch,observe_seconds):
    directory,scratch=Path(directory).resolve(),Path(scratch).resolve();scratch.mkdir(parents=True,exist_ok=True)
    def physical(virtual):
        return (virtual&0x1fffffff)+((((virtual>>20)+512)&4096))
    def emissions(prefix):
        raw=Path(str(prefix)+'-emission.bin').read_bytes();words=struct.unpack('>'+str(len(raw)//4)+'I',raw)
        return [words[i+1:i+4] for i,value in enumerate(words[:-3]) if value==0xc0025800]
    checked=[];counter_endians=set()
    for kind in ('fence','counter'):
        for path in sorted(directory.glob(f'native-service-{kind}-*.json')):
            meta=json.loads(path.read_text())
            if meta['process_id']!=process:continue
            events=emissions(path.with_suffix(''));label=meta['label']
            if kind=='fence':
                assert events==[(3,physical(label+4)|2,meta['cursor']),(3,physical(label)|2,meta['serial'])],(path,events,meta)
                assert meta['actual_serial']==meta['serial'] and meta['actual_cursor']==meta['cursor'],meta
                cpu=Path(str(path.with_suffix(''))+'-device.bin').read_bytes();word=lambda at:struct.unpack_from('>I',cpu,at)[0]
                assert word(11024)==label and word(11036)==(meta['serial']+2)&0xffffffff and word(13608)==(meta['cursor']&~3) and word(13612)&3==meta['cursor']&3,(path,meta)
            else:
                endian=meta['endian'];assert len(events)==1 and events[0][:2]==(0x80000003,physical(label)|endian),(path,events,meta)
                value=meta['counter'];data=value.to_bytes(4,'little')
                if endian==1:data=bytes((data[1],data[0],data[3],data[2]))
                elif endian==2:data=data[::-1]
                elif endian==3:data=data[2:]+data[:2]
                else:assert endian==0,endian
                assert meta['actual_host_word']==int.from_bytes(data,'little'),meta;counter_endians.add(endian)
            checked.append(dict(kind=kind,sample=path.name))
    assert sum(row['kind']=='fence' for row in checked)==64 and any(row['kind']=='counter' for row in checked),len(checked)
    samples=[];first=time.monotonic();deadline=first+observe_seconds;previous_vblank=None
    while time.monotonic()<deadline:
        try:
            status=json.loads((directory/'native-services-status.json').read_text())
            assert status['process_id']==process and not status['invalid_label_writes'],status
            if previous_vblank is not None and status['vblanks']!=previous_vblank:samples.append(dict(time=time.monotonic()-first,**{key:status[key] for key in ('vblanks','paced_frames','buffer_completions','interrupts')}))
            previous_vblank=status['vblanks']
        except json.JSONDecodeError:pass
        time.sleep(.05)
    assert len(samples)>=3,samples
    start,end=samples[0],samples[-1];ticks=end['vblanks']-start['vblanks'];frames=end['paced_frames']-start['paced_frames'];hz=frames/(end['time']-start['time'])
    assert .95<=frames/ticks<=1.01 and 52<=hz<=68,(samples,hz)
    result=dict(scope='Native high-level service results versus independently decoded original CPU emitter diagnostics. All 64 buffer label pairs match original serial/cursor/endian destinations. Counter writes match original CPU address and endian intent. No reference GPU loaded in the separately validated native live session. Live cadence is display pacing, not a full-game performance benchmark.',process_id=process,buffer_contracts=64,counter_contracts=sum(row['kind']=='counter' for row in checked),counter_endians=sorted(counter_endians),observed_display_cadence_hz=hz,observed_frames=frames,observed_vblanks=ticks,cadence_samples=samples)
    (scratch/'native-service-contracts.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--directory',required=True);parser.add_argument('--process',required=True,type=int);parser.add_argument('--scratch',required=True);parser.add_argument('--observe-seconds',type=float,default=6);args=parser.parse_args();run(args.directory,args.process,args.scratch,args.observe_seconds)

"""Inventario de solo lectura CapsulePick; no copia assets al proyecto."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ELF = Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47')
CMN = Path('ps2_games/Infinite World (USA)/USR/DATA_CMN.AFS')
USA = CMN.with_name('DATA_USA.AFS')


def u(b, p):
    return struct.unpack_from('<I', b, p)[0]


def entrada(i):
    with (USA if i >> 16 else CMN).open('rb') as f:
        f.seek(8 + 8 * (i & 65535))
        off, tam = struct.unpack('<II', f.read(8))
        f.seek(off)
        b = f.read(tam)
        assert len(b) == tam
        return b


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', type=Path, default=Path('D:/tmp/solIWF/datos.json'))
    p.add_argument('--ame', action='store_true', help='solo inventario AME972/IECS de capsulas')
    a = p.parse_args()
    assert a.out.parent.is_dir()
    elf = ELF.read_bytes()
    if a.ame:
        b=entrada(972); nodes=[]; seen=set()
        def walk(q,parent=0):
            while q:
                assert q not in seen and q+0x60<=len(b), 'ciclo/AME truncado'
                seen.add(q)
                nodes.append(dict(offset=q,parent=parent,type=hex(u(b,q+4)),flags=hex(u(b,q+8)),
                    pos=struct.unpack_from('<3f',b,q+0x30),q=struct.unpack_from('<4f',b,q+0x40),
                    params=struct.unpack_from('<40f',b,q+0x50)))
                walk(u(b,q+0x24),q)
                q=u(b,q+0x28)
        walk(u(b,12))
        out=dict(ame=nodes)
        print('AME972 nodos',len(nodes),'tipos',sorted(set(r['type'] for r in nodes)),
              'particulas infinitas',sum(r['params'][0]<0 for r in nodes if r['type'].startswith('0x2')))
        out['iecs']={}
        for id in (957,960):
            s=entrada(id);out['iecs'][id]=s.hex()
            print('IECS',id,len(s),'bytes; flujo MIDI original, no one-shot SESQ')
        irx=CMN.parent.parent/'MODULES/AMSOUND.IRX'
        ib=irx.read_bytes();sections=[]
        for k in range(struct.unpack_from('<H',ib,48)[0]):
            h=struct.unpack_from('<10I',ib,u(ib,32)+k*struct.unpack_from('<H',ib,46)[0])
            if h[1]!=8 and h[5]: sections.append((h[3],h[4],h[5]))
        def iw(va):
            for v,o,n in sections:
                if v<=va and va+4<=v+n:return u(ib,o+va-v)
            raise ValueError(hex(va))
        out['rpc_dispatch']={hex(i):hex(iw(0x4610+4*i)) for i in range(0x10,0x20)}
        print('AMSOUND RPC',out['rpc_dispatch'])
        a.out.write_text(json.dumps(out,indent=2),encoding='utf-8');return
    tabla = 0x472f40 - 0x100000 + 0x80
    out = {'elf': str(ELF), 'sha256': hashlib.sha256(elf).hexdigest(), 'recursos': []}
    for i in range(45):
        id = u(elf, tabla + i*4)
        b = entrada(id)
        r = dict(slot=i, id=id, size=len(b), magic=b[:4].decode('ascii', 'replace'),
                 header=[hex(u(b, j)) for j in range(0, min(64, len(b)), 4)])
        if b[:4] in (b'#AMP', b'#AML'):
            r['clips'] = [[hex(u(b, u(b, 20)+k*16+j)) for j in range(0, 16, 4)] for k in range(u(b, 16))]
            if b[:4] == b'#AMP':
                r['labels'] = [b[u(b, 28)+32*k:u(b, 28)+32*(k+1)].split(b'\0')[0].decode() for k in range(u(b, 24))]
                r['pistas'] = []
                for k in range(u(b, 16)):
                    q = u(b, u(b, 20)+k*16+12)
                    for j in range(u(b, 24)):
                        t = u(b, q+4*j)
                        if t:
                            r['pistas'].append(dict(clip=k, label=j, header=[hex(u(b, t+x)) for x in (0,4,8)],
                                keys=[struct.unpack_from('<hh', b, t+12+z*4) for z in range(u(b,t+8))]))
        if b[:4] == b'#AMO' and id in (978,982,986):
            r['nodos'] = []
            for k in range(u(b,16)):
                n = u(b,20)+32*k
                s = u(b,n+4)
                r['nodos'].append(dict(i=k, words=[hex(u(b,n+j)) for j in range(0,32,4)],
                    selector=[hex(u(b,s+j)) for j in range(0, min(48,len(b)-s),4)] if s else []))
            amg = u(b,u(b,28))
            r['amg'] = dict(offset=amg,header=[hex(u(b,amg+j)) for j in range(0,64,4)],
                ejes=[[hex(u(b,amg+u(b,amg+20)+80*k+j)) for j in (0x30,0x34,0x38,0x3c,0x40)] for k in range(u(b,amg+16))])
        out['recursos'].append(r)
    out['transicion10'] = hex(u(elf,0x4725c0-0x100000+0x80+40))
    out['llamadas_uv'] = [hex(pc) for pc in range(0x2832f0,0x283390,4)
        if u(elf,pc-0x100000+0x80)==(0x0c000000|(0x283160>>2))]
    a.out.write_text(json.dumps(out, indent=2), encoding='utf-8')
    for r in out['recursos']:
        print(r['slot'], r['id'], r['magic'], r['size'], r.get('labels', ''), r.get('clips', ''))
    print('2832F0 llama283160:',out['llamadas_uv'],'transicion10:',out['transicion10'])


if __name__ == '__main__':
    main()

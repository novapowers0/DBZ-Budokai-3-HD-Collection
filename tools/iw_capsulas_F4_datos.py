"""Lectura de las tablas originales de microprogramas VU y material AMO."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parent))
from iw_capsulas_F_datos import ELF,entrada,u


def main():
    p=argparse.ArgumentParser();p.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'));a=p.parse_args()
    assert a.tmp.is_dir();out=a.tmp/('capsulas_F4_'+str(time.time_ns()));out.mkdir()
    elf=ELF.read_bytes()
    def word(va):return u(elf,va-0x100000+0x80)
    def memory(va,n):return elf[va-0x100000+0x80:va-0x100000+0x80+n]
    def chain(pc):
        stack=[];parts=[];tags=[]
        for _ in range(1024):
            t=[word(pc+j) for j in range(0,16,4)];n=t[0]&65535;op=(t[0]>>28)&7;addr=t[1]&0x0fffffff
            tags.append([hex(pc),*map(hex,t)]);parts.append(struct.pack('<II',t[2],t[3]))
            parts.append(memory(addr if op in (0,3,4) else pc+16,n*16))
            after=pc+16+(n*16 if op in (1,2,5,6,7) else 0)
            if op in (0,7):break
            if op==5:stack.append(after);pc=addr
            elif op==6:
                if not stack:break
                pc=stack.pop()
            else:pc=addr if op==2 else after
        else:raise ValueError('DMA no termina')
        stream=b''.join(parts);p=0;vif=[];program=[]
        while p+4<=len(stream):
            w=u(stream,p);p+=4;cmd=(w>>24)&127;num=(w>>16)&255;imm=w&65535
            vif.append(hex(w));size=0
            if cmd==0x4a:
                size=(num or 256)*8;program.append(dict(addr=imm,pairs=num or 256,sha256=hashlib.sha256(stream[p:p+size]).hexdigest(),
                    head=stream[p:p+min(64,size)].hex(),code=stream[p:p+size].hex()))
            elif cmd in (0x50,0x51):size=imm*16
            elif cmd in (0x30,0x31):size=16
            elif cmd==0x20:size=4
            elif cmd>=0x60:raise ValueError('UNPACK en carga programa: '+hex(w))
            elif cmd not in (0,1,2,3,4,5,6,7,0x10,0x11,0x13,0x14,0x15,0x17):raise ValueError('VIF '+hex(w))
            p+=size
        return dict(tags=tags,vif=vif,mpg=program)
    data=dict(elf_sha256=hashlib.sha256(elf).hexdigest(),programas=[],modelos=[])
    for family,n in ((1,4),(3,4),(5,16),(7,144)):
        table=word(0x40fad0+4*family)
        for i in range(n):
            record=[word(table+16*i+j) for j in range(0,16,4)]
            addr=record[1]&0x0fffffff;size=(record[0]&65535)*16
            item=dict(family=family,index=i,table=hex(table),record=list(map(hex,record)),addr=hex(addr),bytes=size)
            if addr>=0x100000:item['dma']=chain(addr)
            data['programas'].append(item)
    for id in (962,965,970,978,982,986):
        b=entrada(id);amg=u(b,u(b,28));axes=amg+u(b,amg+20);parts=[]
        for bone in range(u(b,amg+16)):
            arm=u(b,axes+80*bone+0x34)
            if not arm:continue
            group=u(b,amg+arm+4)
            if not group:continue
            g=amg+group
            for k in range(u(b,g)):
                q=g+u(b,g+u(b,g+4)+4*k);flags=u(b,q)
                parts.append(dict(offset=q,bone=bone,flags=hex(flags),flags2=hex(u(b,q+4)),tex=struct.unpack_from('<i',b,q+8)[0],rampa=struct.unpack_from('<i',b,q+12)[0],
                    ka=struct.unpack_from('<4f',b,q+0x20),kd=struct.unpack_from('<4f',b,q+0x30),
                    gs=[hex(u(b,q+j)) for j in range(0x40,0x90,4)],vif=b[q+0xa0:q+0xe0].hex()))
        data['modelos'].append(dict(id=id,parts=parts))
        print('AMO',id,'partes',len(parts),'flags',sorted(set(p['flags'] for p in parts)))
        print(' flags2',sorted(set(p['flags2'] for p in parts)))
    for f in (1,3,5,7):
        for item in data['programas']:
            if item['family']==f and (f!=7 or item['index'] in (5,7,13,15,0x3d,0x3f)):
                print('VU',item['family'],item['index'],item['addr'],[(p['addr'],p['pairs'],p['head']) for p in item.get('dma',{}).get('mpg',[])])
    data['global_vif']=[hex(word(0x485f00+j)) for j in range(0,0x1e0,4)]
    data['globals']={hex(p):hex(word(p)) for p in (0x49c658,0x49c65c,0x49be34,0x49be30,0x49be2c,0x49c660,0x49c664,0x49c668,0x49be18,0x49be1c,0x40f9d0,0x40f9d8)}
    import capstone
    dis=capstone.Cs(capstone.CS_ARCH_MIPS,capstone.CS_MODE_MIPS64|capstone.CS_MODE_LITTLE_ENDIAN);dis.skipdata=True
    (out/'selector.txt').write_text('\n'.join(f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in dis.disasm(memory(0x1b1130,0x500),0x1b1130)),encoding='utf-8')
    (out/'datos.json').write_text(json.dumps(data,indent=2),encoding='utf-8');print('DATOS',out)


if __name__=='__main__':main()

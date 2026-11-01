"""Desensamblado VU acotado del ELF, sin ensambladores externos."""
import argparse
import json
from pathlib import Path
import struct

UP={0x28:'add',0x29:'madd',0x2a:'mul',0x2b:'max',0x2c:'sub',0x2d:'msub',0x2e:'opmsub',0x2f:'mini',
    0x1c:'mulq',0x1d:'maxi',0x1e:'muli',0x1f:'minii',0x20:'addq',0x21:'maddq',0x22:'addi',0x23:'maddi',
    0x24:'subq',0x25:'msubq',0x26:'subi',0x27:'msubi'}
EXT={0x10:'itof0',0x11:'itof4',0x12:'itof12',0x13:'itof15',0x14:'ftoi0',0x15:'ftoi4',0x16:'ftoi12',0x17:'ftoi15',
    0x1c:'mulaq',0x1d:'abs',0x1e:'mulai',0x1f:'clipw',0x20:'addaq',0x21:'maddaq',0x22:'addai',0x23:'maddai',
    0x24:'subaq',0x25:'msubaq',0x26:'subai',0x27:'msubai',0x28:'adda',0x29:'madda',0x2a:'mula',0x2c:'suba',0x2d:'msuba',0x2e:'opmula',0x2f:'nop'}
LOW={0:'lq',1:'sq',4:'ilw',5:'isw',8:'iaddiu',9:'isubiu',0x10:'fceq',0x11:'fcset',0x12:'fcand',0x13:'fcor',
     0x14:'fseq',0x15:'fsset',0x16:'fsand',0x17:'fsor',0x18:'fmeq',0x1a:'fmand',0x1b:'fmor',
     0x20:'b',0x21:'bal',0x24:'jr',0x25:'jalr',0x28:'ibeq',0x29:'ibne',0x2c:'ibltz',0x2d:'ibgtz',0x2e:'iblez',0x2f:'ibgez'}
LE={0x30:'move',0x31:'mr32',0x34:'lqi',0x35:'sqi',0x36:'lqd',0x37:'sqd',0x38:'div',0x39:'sqrt',0x3a:'rsqrt',0x3b:'waitq',
    0x3c:'mtir',0x3d:'mfir',0x3e:'ilwr',0x3f:'iswr',0x40:'rnext',0x41:'rget',0x42:'rinit',0x43:'rxor',0x64:'mfp',0x68:'xtop',0x69:'xitop',0x6c:'xgkick'}

def decode(pc,lo,hi):
    mask=''.join(c for i,c in enumerate('xyzw') if (hi>>(24-i))&1)
    ft,fs,fd,fn=(hi>>16)&31,(hi>>11)&31,(hi>>6)&31,hi&63
    if fn<0x1c:up=['add','sub','madd','msub','max','mini','mul'][fn>>2]+'.'+ 'xyzw'[fn&3]
    elif fn>=0x3c:
        ext=(fd<<2)|(fn&3)
        if ext<16:up=['adda','suba','madda','msuba'][ext>>2]+'.'+'xyzw'[ext&3]
        elif 0x18<=ext<0x1c:up='mula.'+'xyzw'[ext&3]
        else:up=EXT.get(ext,'?EXT'+hex(ext))
    else:up=UP.get(fn,'?UP'+hex(fn))
    upper=f'{up}.{mask} d{fd},s{fs},t{ft}'
    if hi>>31:lower='I='+str(struct.unpack('<f',struct.pack('<I',lo))[0])
    else:
        op=lo>>25;it,is_,idd,f=(lo>>16)&31,(lo>>11)&31,(lo>>6)&31,lo&63
        if op==0x40:
            lower={0x30:'iadd',0x31:'isub',0x32:'iaddi',0x34:'iand',0x35:'ior'}.get(f)
            if lower is None:lower=LE.get((idd<<2)|(f&3),'?L'+hex((idd<<2)|(f&3)))
        else:lower=LOW.get(op,'?OP'+hex(op))
        lower+=f' it{it},is{is_},id{idd},imm{lo&2047:#x},mask{(lo>>21)&15:x}'
    return f'{pc:04x}: {lo:08x} {hi:08x} | {upper:30} | {lower}'+(' [E]' if hi&(1<<30) else '')


def main():
    p=argparse.ArgumentParser();p.add_argument('datos',type=Path);p.add_argument('--indice',type=lambda s:int(s,0),default=53);a=p.parse_args()
    lines=[]
    if a.datos.suffix=='.bin':
        b=a.datos.read_bytes()
        for i in range(0,len(b),8):lines.append(decode(i//8,*struct.unpack_from('<II',b,i)))
        out=a.datos.with_suffix('.txt')
    else:
        d=json.loads(a.datos.read_text());prog=next(p for p in d['programas'] if p['family']==7 and p['index']==a.indice)
        for part in prog['dma']['mpg']:
            b=bytes.fromhex(part['code'])
            for i in range(0,len(b),8):lines.append(decode(part['addr']+i//8,*struct.unpack_from('<II',b,i)))
        out=a.datos.with_name('vu_'+str(a.indice)+'.txt')
    out.write_text('\n'.join(lines),encoding='utf-8');print(out)


if __name__=='__main__':main()

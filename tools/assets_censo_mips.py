"""Recupera argumentos constantes locales de JAL en ELF PS2. No ejecuta codigo.

Solo instrucciones enteras MIPS basicas; invalida registros volatiles tras llamada
y flujo de control previo. No adivina argumentos variables ni interpreta MMI/VU.
"""
import bisect
import struct

def segment(game):
    b=game.elf
    ph=struct.unpack_from('<I',b,28)[0]
    ent,n=struct.unpack_from('<HH',b,42)
    for i in range(n):
        typ,off,va,_,size=struct.unpack_from('<5I',b,ph+i*ent)
        if typ==1:
            yield va,b[off:off+size]

def recover(game, funcs, targets):
    b=game.elf;gp=None
    sh=struct.unpack_from('<I',b,32)[0];se,sn=struct.unpack_from('<HH',b,46)
    for k in range(sn):
        if struct.unpack_from('<I',b,sh+k*se+4)[0]==0x70000006:
            off=struct.unpack_from('<I',b,sh+k*se+16)[0]
            gp=struct.unpack_from('<I',b,off+20)[0]
    starts=sorted(int(x,16) for x in funcs)
    out=[]
    for base,raw in segment(game):
        words=struct.unpack_from('<%dI'%(len(raw)//4),raw)
        for i,w in enumerate(words):
            if w>>26!=3 or ((w&0x3ffffff)<<2) not in targets: continue
            pc=base+i*4
            a=starts[bisect.bisect_right(starts,pc)-1]
            fn=funcs[f'{a:08x}']
            initial={0:0,**({28:gp} if gp is not None else {})}
            regs=dict(initial); origins={0:'zero'}
            proof=[]
            for j in range(max((a-base)//4,i-20),min(i+2,len(words))):
                q=words[j]; op=q>>26;rs=(q>>21)&31;rt=(q>>16)&31;rd=(q>>11)&31
                imm=q&65535;si=imm if imm<32768 else imm-65536
                val=None;dest=None
                if j<i and op in (1,2,3,4,5,6,7,20,21,22,23):
                    regs=dict(initial);origins={0:'zero'}
                    continue
                if op==15: dest=rt;val=imm<<16
                elif op in (9,25):
                    dest=rt
                    if rs in regs: val=(regs[rs]+si)&0xffffffff
                elif op==13:
                    dest=rt
                    if rs in regs: val=regs[rs]|imm
                elif op==0 and q&63 in (33,37,45):
                    dest=rd
                    if rs in regs and rt in regs:
                        val=(regs[rs]|regs[rt]) if q&63==37 else (regs[rs]+regs[rt])&0xffffffff
                elif op==35:
                    dest=rt
                    if rs in regs:
                        addr=(regs[rs]+si)&0xffffffff
                        try: val=game.va(addr,1)[0]
                        except ValueError: pass
                elif op in (8,10,11,12,14,32,33,34,36,37,38,39,55):dest=rt
                elif op==0 and q&63 not in (8,9,12,13,16,17,18,19,24,25,26,27):dest=rd
                if dest:
                    regs.pop(dest,None)
                    if val is not None:
                        regs[dest]=val
                        origins[dest]=f'{base+j*4:08x}:{q:08x}'
                        if dest in (4,5,6,7):proof.append(origins[dest])
            row={'funcion':f'0x{a:08x} {fn["name"]}','linea_decomp':fn['line'],
                 'pc':hex(pc),'destino':hex((w&0x3ffffff)<<2),
                 'palabras_prueba':';'.join(proof), 'tipo_evidencia':'ELF estatico; NO traza runtime'}
            for reg in range(4,8):row['a'+str(reg-4)]=hex(regs[reg]) if reg in regs else 'VARIABLE'
            out.append(row)
    return out

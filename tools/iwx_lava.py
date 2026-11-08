"""Contrasta los predicados C++ de lava con instrucciones del ELF EU autenticado.

Interprete acotado a EU2A5DE0/2A5F40. Los tres getters externos solo devuelven
direcciones de fixtures. No simula colision, efectos, vida ni una partida.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from iwx_estados import SHA_EU


def f32(v):
    return struct.unpack('<f',struct.pack('<f',v))[0]


class Original:
    def __init__(self,path):
        self.elf=path.read_bytes()
        if hashlib.sha256(self.elf).hexdigest()!=SHA_EU:
            raise ValueError('ELF distinto de la revision EU acreditada')
        ph,=struct.unpack_from('<I',self.elf,28)
        paso,n=struct.unpack_from('<HH',self.elf,42)
        self.bloques=[struct.unpack_from('<8I',self.elf,ph+i*paso) for i in range(n)]

    def bytes(self,a,n):
        for tipo,off,va,_,tam,*_ in self.bloques:
            if tipo==1 and va<=a and a+n<=va+tam:
                return self.elf[off+a-va:off+a-va+n]
        raise ValueError(f'Lectura fuera de PT_LOAD: {a:X}/{n}')

    def ejecutar(self,inicio,c):
        # Memoria separada del ejecutable: solo campos que leen los predicados.
        actor,flag,obj,work=0x100000,0x200000,0x300000,0x400000
        mem={};r=[0]*32;fp=[0]*32;cond=False
        def put(a,v,n=4):
            for i in range(n):mem[a+i]=(v>>(8*i))&255
        def get(a,n,signed=False):
            if any(a+i not in mem for i in range(n)):
                raise ValueError(f'Lectura no declarada {a:X}/{n}')
            return int.from_bytes(bytes(mem[a+i] for i in range(n)),'little',signed=signed)
        put(actor+0x1380,0);put(actor+0xCBC,c['estado'],2)
        put(actor+0x44,struct.unpack('<I',struct.pack('<f',c['y']))[0])
        put(actor+0x18D0,struct.unpack('<I',struct.pack('<f',c['suelo']))[0])
        put(actor+0x2061,c['aire'],1);put(flag+4,10)
        for o in (0x248,0x180,0x1B0,0x1AC):put(obj+o,0)
        put(work+0x1E8,c['pies']);put(work+0x1F0,c['cuerpo'])
        r[4]=actor;r[29]=0x800000;r[31]=0xFFFFFFFC
        getters={0x28A9D0:flag,0x29A5D0:obj,0x296F20:work}

        def simple(w):
            nonlocal cond
            op,rs,rt,rd,sa,fn=w>>26,(w>>21)&31,(w>>16)&31,(w>>11)&31,(w>>6)&31,w&63
            im=w&65535;sg=im if im<32768 else im-65536
            if op==0:
                if fn==0:r[rd]=(r[rt]<<sa)&0xFFFFFFFF
                elif fn in (0x21,0x2D):r[rd]=(r[rs]+r[rt])&0xFFFFFFFF
                else:raise ValueError(f'SPECIAL no soportado {w:08X}')
            elif op==9:r[rt]=(r[rs]+sg)&0xFFFFFFFF
            elif op==10:r[rt]=int((r[rs] if r[rs]<0x80000000 else r[rs]-0x100000000)<sg)
            elif op==13:r[rt]=r[rs]|im
            elif op==15:r[rt]=im<<16
            elif op in (0x20,0x21,0x23,0x37,0x1E):
                n={0x20:1,0x21:2,0x23:4,0x37:8,0x1E:16}[op]
                r[rt]=get(r[rs]+sg,n,op in (0x20,0x21))
            elif op in (0x3F,0x1F):put(r[rs]+sg,r[rt],8 if op==0x3F else 16)
            elif op==0x31:fp[rt]=get(r[rs]+sg,4)
            elif op==0x11:
                ft,fs,fd=rt,rd,sa
                if rs==4:fp[fs]=r[rt]&0xFFFFFFFF
                elif rs==20 and fn==0x20:
                    v=fp[fs] if fp[fs]<0x80000000 else fp[fs]-0x100000000
                    fp[fd]=struct.unpack('<I',struct.pack('<f',v))[0]
                elif rs==16:
                    a=struct.unpack('<f',struct.pack('<I',fp[fs]))[0]
                    b=struct.unpack('<f',struct.pack('<I',fp[ft]))[0]
                    if fn==1:fp[fd]=struct.unpack('<I',struct.pack('<f',f32(a-b)))[0]
                    elif fn==5:fp[fd]=fp[fs]&0x7FFFFFFF
                    elif fn==0x36:cond=a<=b
                    else:raise ValueError(f'COP1 no soportado {w:08X}')
                else:raise ValueError(f'COP1 formato no soportado {w:08X}')
            else:raise ValueError(f'Opcode no soportado {w:08X}')
            r[0]=0

        pc=inicio
        for _ in range(180):
            w,=struct.unpack('<I',self.bytes(pc,4));op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31
            im=w&65535;sg=im if im<32768 else im-65536
            branch=op in (4,5,0x14,0x15) or (op==0x11 and rs==8)
            if branch:
                if op==0x11:take=cond==bool(rt&1);likely=bool(rt&2)
                else:take=(r[rs]==r[rt])==(op in (4,0x14));likely=op in (0x14,0x15)
                if take or not likely:simple(struct.unpack('<I',self.bytes(pc+4,4))[0])
                pc=pc+4+sg*4 if take else pc+8
            elif op==3:
                target=(w&0x3FFFFFF)<<2
                if target not in getters:raise ValueError(f'Llamada externa no declarada {target:X}')
                r[31]=pc+8;simple(struct.unpack('<I',self.bytes(pc+4,4))[0]);r[2]=getters[target];pc+=8
            elif op==0 and w&63==8:
                target=r[rs];simple(struct.unpack('<I',self.bytes(pc+4,4))[0])
                if target!=0xFFFFFFFC:raise ValueError('Retorno inesperado')
                return r[2]
            else:simple(w);pc+=4
        raise ValueError('Presupuesto de instrucciones agotado')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf',type=Path,required=True)
    ap.add_argument('--salida',type=Path,required=True)
    ap.add_argument('--captura',type=Path)
    a=ap.parse_args();original=Original(a.elf);a.salida.mkdir(exist_ok=False)
    casos=[]
    # Fronteras positivas/negativas y estados excluidos, todas las combinaciones.
    for estado in (0,3,0xE,0x10,0x17,0x18,0x1F,0x20,0x25,0x26,0x27,0x28,0x29,0x2A):
        for y in (-2.00000024,-2.,-.010000001,-.01,0,.01,.010000001,2.,2.00000024):
            for pies,cuerpo,aire in ((3,0,0),(3,2,0),(3,2,1),(4,0,0),(0,4,1),(1,0,0)):
                casos.append(dict(estado=estado,pies=pies,cuerpo=cuerpo,aire=aire,y=f32(y),suelo=0.,fuente='frontera'))
    observados=[]
    if a.captura:
        for linea in a.captura.read_text(encoding='utf-8').splitlines():
            if not linea.startswith('{'):continue
            row=json.loads(linea)
            if row.get('s',{}).get('cnt',0)==0 or 'p' not in row:continue
            q=row['q']
            # El escalar en p es el dy INMEDIATAMENTE anterior a Allowed2,
            # redondeado a 4 decimales. No mezclarlo con q, leido mas tarde.
            dy,ret=row['p']
            if abs(abs(dy)-.01)<.0001:continue
            c=dict(estado=int(row['st'],16),pies=q['t1e8'],cuerpo=q['t1f0'],aire=q['a'],y=f32(dy),suelo=0.,fuente='sonda_anterior')
            # Los otros campos no son atomicos con la sonda. Comparacion solo
            # del rechazo geometrico: dy fuera del limite => retorno0.
            if abs(dy)>.01:
                observados.append(dict(n=row['n'],dy=dy,retorno=ret))
                if ret!=0:raise ValueError('Captura contradice el rechazo geometrico')
                casos.append(c)
    esperado=[(original.ejecutar(0x2A5F40,c),original.ejecutar(0x2A5DE0,c)) for c in casos]
    raiz=Path(__file__).resolve().parents[1];exe=a.salida/'lava.exe'
    subprocess.run(['C:/Program Files/LLVM/bin/clang++.exe','-std=c++17','-ffp-contract=off',str(raiz/'tools/iwx_lava_cpp.cpp'),'-o',str(exe)],check=True)
    text=''.join('{estado} {pies} {cuerpo} {y:.9g} {suelo:.9g} {aire}\n'.format(**c) for c in casos)
    ret=subprocess.run([str(exe)],input=text,text=True,capture_output=True,check=True)
    actual=[tuple(map(int,l.split())) for l in ret.stdout.splitlines()]
    fallos=[dict(caso=c,original=e,port=p) for c,e,p in zip(casos,esperado,actual) if e!=p]
    if len(actual)!=len(casos):raise ValueError('Resultado C++ incompleto')
    resultado=dict(elf_sha256=SHA_EU,casos=len(casos),fallos=fallos,
        funciones=[dict(direccion=hex(f),sha256=hashlib.sha256(original.bytes(f,0x160)).hexdigest()) for f in (0x2A5DE0,0x2A5F40)],
        captura=str(a.captura) if a.captura else None,captura_sha256=hashlib.sha256(a.captura.read_bytes()).hexdigest() if a.captura else None,
        rechazos_geometricos_observados=observados,paridad_colision=False,paridad_integral=False,
        limites='Solo predicados y datos finitos; getters externos sustituidos por direcciones de fixtures. Captura no atomica/redondeada; no prueba movimiento ni continuidad de video.')
    for nombre,datos in (('casos.json',casos),('resultado.json',resultado)):
        with (a.salida/nombre).open('x',encoding='utf-8') as f:json.dump(datos,f,indent=1,ensure_ascii=False,allow_nan=False)
    print(json.dumps(dict(casos=len(casos),fallos=len(fallos),rechazos_captura=len(observados),salida=str(a.salida)),indent=1))
    return 1 if fallos else 0


if __name__=='__main__':raise SystemExit(main())

"""Oraculo acotado: ejecuta instrucciones EU; no lanza juego ni pruebas.py.

Contexto RAM controlado y recursos originales USA de CapsulePick (solo lectura).
El interprete R5900 compartido es el del arnes SPX previo; las funciones AMP,
UV, RPC y transicion NO se reescriben en Python como resultados esperados.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from iw_capsulas_F_datos import ELF, CMN, USA, entrada, u

spec = importlib.util.spec_from_file_location('spx_retail', ROOT/'tests/iw_spx_original_prueba.py')
retail = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retail)
FB, BF = retail.FB, retail.BF


class MipsLuz(retail.Mips):
    """Solo LQC2/SQC2 en los extremos AML; no pretende emular todo VU0."""
    def __init__(self, elf):
        super().__init__(elf)
        self.vf=[bytes(16) for _ in range(32)]
        self.acc=0.

    def step(self):
        w=self.load(self.pc,4)
        op,s,t=w>>26,(w>>21)&31,(w>>16)&31
        if self.pc not in self.hooks and op==0x11 and s==16 and (w&63) in (0x1a,0x1c,0x1e):
            self.instructions.append((self.pc,w))
            fs,ft,fd,fn=(w>>11)&31,t,(w>>6)&31,w&63
            product=retail.F32(BF(self.fp[fs])*BF(self.fp[ft]))
            if fn==0x1a: self.acc=product
            elif fn==0x1e: self.acc=retail.F32(self.acc+product)
            else: self.fp[fd]=FB(retail.F32(self.acc+product))
            old,self.pending=self.pending,None
            self.pc=old if old is not None else self.pc+4
        elif self.pc not in self.hooks and op in (0x36,0x3e):
            self.instructions.append((self.pc,w))
            a=(self.r[s]+retail.SX(w&65535,16))&0xffffffff
            if op==0x36: self.vf[t]=bytes(self.mem[a:a+16])
            else: self.mem[a:a+16]=self.vf[t]
            old,self.pending=self.pending,None
            self.pc=old if old is not None else self.pc+4
        else:
            super().step()


def call(m, pc, args=(), f=0):
    m.r = [0]*32
    m.r[4:4+len(args)] = args
    m.r[28], m.r[29], m.r[31] = 0x4A3DF0, 0x1F00000, retail.END
    m.fp = [0]*32
    m.fp[12] = FB(f)
    m.pc, m.pending = pc, None
    for _ in range(1000000):
        if m.pc == retail.END:
            return m.r[2] & 0xffffffff
        m.step()
    raise RuntimeError(f'limite instrucciones {m.pc:#x}')


def relocate(m, b, base, amp=False, amo=False):
    m.mem[base:base+len(b)] = b
    if amo:
        m.store(base+0x24, 4, base+u(b, 0x24))
    else:
        m.store(base+20, 4, base+u(b, 20))
        if amp:
            m.store(base+28, 4, base+u(b, 28))
        for i in range(u(b, 16)):
            p = u(b, 20)+16*i+12
            q = u(b, p)
            m.store(base+p, 4, base+q)
            if amp:
                for j in range(u(b,24)):
                    t = u(b,q+4*j)
                    m.store(base+q+4*j,4,base+t if t else 0)


def amp_cases(elf):
    out = []
    for amo_id, amp_id in ((978,979),(982,983),(986,987)):
        m = retail.Mips(elf)
        amo, amp = entrada(amo_id), entrada(amp_id)
        base, ab, work, obj = 0x800000, 0xA00000, 0xB00000, 0xB01000
        relocate(m,amo,base,amo=True)
        relocate(m,amp,ab,amp=True)
        count = u(amo,16)
        m.store(obj,4,base)
        m.store(obj+4,4,count)
        m.store(work,4,obj)
        # Solo strlen (MMI optimizado) es servicio sustituido. El comparador
        # 1372F0, el bind13BCF0 y el muestreador13BDF0 se ejecutan del ELF.
        def strlen():
            addr=m.r[4]&0xffffffff
            m.low(2,m.mem.index(0,addr)-addr)
        m.hooks[0x120b90]=strlen
        call(m,0x13bcf0,(work,0,ab))
        mapping = [retail.SX(m.load(work+0x44+2*i,2),16) for i in range(count)]
        frames = sorted(set([0.0,0.5,1.0,float(u(amp,u(amp,20)+8)-1)] +
                            [float(i) for i in range(u(amp,u(amp,20)+8))]))
        samples=[]
        for f in frames:
            for i in range(count):
                m.store(obj+8+8*i,4,0xffffffff)
            call(m,0x13bdf0,(work,0x10000),f)
            samples.append(dict(frame=f,texture=[retail.SX(m.load(obj+8+8*i,4),32) for i in range(count)]))
        out.append(dict(amo=amo_id,amp=amp_id,mapping=mapping,samples=samples,
                        instructions=len(m.instructions),hooks=m.boundaries))
        print('AMP retail',amo_id,amp_id,count,'canales;',len(frames),'cuadros;',len(m.instructions),'instrucciones')
    return out


def uv_cases(elf):
    out=[]
    for ticks,du,dv in ((2,.25,.125),(-1,-.125,1.25),(0,.25,.125)):
        m=retail.Mips(elf)
        work,regs,temp,args,instance=0x900000,0x901000,0x902000,0x903000,0x904000
        m.store(work,4,instance)
        m.store(instance+12,4,2)
        m.store(regs+44,4,args)
        m.store(regs+56,4,temp)
        for i,v in enumerate((FB(dv),FB(du),ticks)): m.store(args+4*i,4,v)
        samples=[]
        ret=call(m,0x2832f0,(work,regs))
        for pause in (False,True,False,False):
            samples.append(dict(ret=ret,remaining=retail.SX(m.load(work+0x3c,4),32),
                uv=[m.load(temp+0x70,4),m.load(temp+0x74,4)],
                gs=[m.load(0x4c4e38+4*j,4) for j in range(4)]+[m.load(0x4c4e78+4*j,4) for j in range(4)],
                poll=m.load(work+16,4)))
            m.store(instance+12,4,3 if pause else 2)
            if m.load(work+16,4): ret=call(m,0x283160,(work,regs))
        out.append(dict(ticks=ticks,du=du,dv=dv,samples=samples,instructions=len(m.instructions)))
    return out


def rpc_cases(elf):
    out=[]
    for value,status in ((0,0),(15,0),(0xffffffff,-1)):
        m=retail.Mips(elf)
        m.store(0x49c740,4,0xA00000)
        busy=[1,0]
        m.hooks[0x106070]=lambda: m.low(2,busy.pop(0))
        def copy():
            dest,src,n=[m.r[i]&0xffffffff for i in (4,5,6)]
            m.mem[dest:dest+n]=m.mem[src:src+n]
        m.hooks[0x11dd40]=copy
        requests=[]
        def rpc():
            r=m.r
            requests.append(dict(command=r[5],mode=r[6],size=r[8],payload=m.load(r[7],r[8])))
            m.low(2,status)
        m.hooks[0x105e70]=rpc
        ret=call(m,0x13fa20,(value,))
        out.append(dict(value=value,status=status,ret=ret,requests=requests,instructions=len(m.instructions)))
    return out


def transition_case(elf):
    m=retail.Mips(elf)
    task=0x900000
    m.store(0x49cb74,4,task)
    m.store(0x49cb7c,4,FB(.25))
    m.store(0x49cb80,4,FB(.125))
    m.store(0x49cb88,4,0x123456)
    m.store(0x49cb8c,4,0x123457)
    draws=[]
    def draw():
        p=m.r[5]&0xffffffff
        draws.append(dict(color=[m.load(p+4*i,4) for i in range(4)],args=[v&0xffffffff for v in m.r[4:10]]))
    m.hooks[0x133a30]=draw
    m.hooks[0x12c5f0]=lambda: m.low(2,0x30000)
    m.hooks[0x12c1d0]=lambda: None
    m.hooks[0x126880]=lambda: None
    m.hooks[0x13fbd0]=lambda: None  # RPC de volumen: limite explicito, no SPU2.
    call(m,0x281e80)
    start=dict(kind=m.load(0x49cb78,4),value=m.load(0x49cb7c,4),callback=m.load(0x49cb88,4),
               draw_callback=m.load(0x49cb8c,4),update=m.load(task+28,4))
    samples=[]
    for _ in range(8):
        call(m,m.load(task+28,4),(task,))
        samples.append(dict(value=m.load(0x49cb7c,4),update=m.load(task+28,4),draw=draws[-1]))
    return dict(start=start,samples=samples,instructions=len(m.instructions),hooks=m.boundaries)


def light_cases(elf):
    out=[]
    for id in (989,990,991,992):
        m=MipsLuz(elf)
        b=entrada(id)
        base,obj=0x900000,0xA00000
        relocate(m,b,base)
        for k in range(u(b,16)):
            p=u(b,u(b,20)+16*k+12)
            for j in (0,4): m.store(base+p+j,4,base+u(b,p+j))
        def normalize():
            p=m.r[4]&0xffffffff
            xyz=[BF(m.load(p+4*i,4)) for i in range(3)]
            length=retail.F32(sum(x*x for x in xyz)**.5)
            for i in range(4): m.store(p+4*i,4,FB(BF(m.load(p+4*i,4))/length))
        m.hooks[0x142540]=normalize  # primitiva vectorial declarada; fuera del sampler.
        for clip in range(u(b,16)):
            for frame in (0.,1.):
                # ABI float en f12, objeto/a0, banco/a1, clip/a2.
                call(m,0x134230,(obj,base,clip),frame)
                out.append(dict(id=id,clip=clip,frame=frame,
                    direction=[m.load(obj+16+4*i,4) for i in range(4)],
                    color=[m.load(obj+32+4*i,4) for i in range(4)]))
    return out


def fixture(out,elf):
    b=bytearray()
    def word(v): b.extend(struct.pack('<I',v&0xffffffff))
    def blob(v): word(len(v));b.extend(v)
    word(0x43465031)
    word(len(out['amp']))
    for case in out['amp']:
        blob(entrada(case['amo']));blob(entrada(case['amp']))
        word(len(case['mapping']))
        for v in case['mapping']: word(v)
        word(len(case['samples']))
        for s in case['samples']:
            word(FB(s['frame']))
            for v in s['texture']: word(v)
    word(len(out['uv']))
    for case in out['uv']:
        word(case['ticks']);word(FB(case['du']));word(FB(case['dv']))
        for s in case['samples']:
            word(s['ret']);word(s['remaining'])
            for v in s['uv']+s['gs']: word(v)
            word(s['poll'])
    word(len(out['rpc']))
    for case in out['rpc']:
        for v in (case['value'],case['status'],case['ret']): word(v)
        req,=case['requests']
        for k in ('command','mode','size','payload'): word(req[k])
    case=out['transition']
    word(FB(.25));word(FB(.125));word(case['start']['value']);word(len(case['samples']))
    for s in case['samples']:
        word(s['value'])
        for v in s['draw']['color']: word(v)
        word(s['draw']['args'][0]);word(s['draw']['args'][3])
    word(len(out['light']))
    for s in out['light']:
        blob(entrada(s['id']));word(s['clip']);word(FB(s['frame']))
        for v in s['direction']+s['color']: word(v)
    blob(elf)
    ids=set(u(elf,0x472f40-0x100000+0x80+4*i) for i in range(45))
    word(len(ids))
    for id in sorted(ids): word(id);blob(entrada(id))
    return b


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'))
    p.add_argument('--sonda',action='store_true')
    p.add_argument('--regenerar',action='store_true',help='regenerar AMP aunque exista oraculo guardado')
    a=p.parse_args()
    a.tmp=a.tmp.resolve()
    assert a.tmp.is_dir(), 'verificar padre temporal antes'
    elf=ELF.read_bytes()
    saved=a.tmp/'original_F.json'
    digest=hashlib.sha256(elf).hexdigest()
    out=json.loads(saved.read_text()) if saved.exists() and not a.regenerar else dict(elf=str(ELF),elf_sha256=digest,amp=amp_cases(elf))
    assert out['elf_sha256']==digest,'oraculo de otro ELF'
    out['uv']=uv_cases(elf)
    out['rpc']=rpc_cases(elf)
    out['transition']=transition_case(elf)
    out['light']=light_cases(elf)
    (a.tmp/'original_F.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
    if a.sonda:
        return 0
    commands=[]
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,text=True,capture_output=True,
            env=dict(os.environ,TEMP=str(a.tmp),TMP=str(a.tmp),TMPDIR=str(a.tmp)))
        (a.tmp/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        commands.append(dict(command=list(map(str,cmd)),returncode=r.returncode,log=name+'.log'))
        print(r.stdout+r.stderr)
        return r.returncode
    f=a.tmp/'fixture_F.bin';f.write_bytes(fixture(out,elf))
    exe=a.tmp/'iw_capsulas_F_prueba.exe'
    compiler='C:/Program Files/LLVM/bin/clang++.exe'
    result=run([compiler,'--version'],'compilador_F_version')
    if not result: result=run([compiler,'-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic',
        '-fsanitize=undefined','-fsanitize-trap=all',ROOT/'tests/iw_capsulas_F_prueba.cpp','-o',exe],'compilar_F')
    if not result: result=run([exe,f],'prueba_F')
    evidence=dict(status='PASS' if not result else 'FAIL',elf=str(ELF),elf_sha256=digest,
        originals=str(saved),originals_sha256=hashlib.sha256(saved.read_bytes()).hexdigest(),
        fixture_sha256=hashlib.sha256(f.read_bytes()).hexdigest(),commands=commands,
        amp_frames=sum(len(c['samples']) for c in out['amp']),
        amp_values=sum(len(c['mapping'])*len(c['samples']) for c in out['amp']),
        retail_amp_instructions=sum(c['instructions'] for c in out['amp']),
        files={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
            (Path(__file__),ROOT/'src/iw_spx_capsulas.h',ROOT/'src/iw_spx.h',ROOT/'tests/iw_capsulas_F_prueba.cpp',ROOT/'tests/iw_spx_original_prueba.py')},
        limits=['RAM/contexto controlados; no savestate ni partida',
                'AMP ejecuta13BCF0,1372F0,13BDF0,1370A0 retail; solo strlen sustituido',
                'UV ejecuta2832F0/283160 completos; sin hooks',
                'RPC13 ejecuta13FA20; servicios SIF/memcpy sustituidos, no reproduce audio',
                'AML ejecuta134230/1342B0 en extremos; normalizacion vectorial primitiva sustituida',
                'transicion ejecuta281E80/27DA10/27D700/27D570/27B5C0; limite en API GS/volumen',
                'geometria del test: topologia/UV VIF; no pose ni rasterizador',
                'sin integracion pasarela ni certificacion visual/audio ni activacion de pantallas'])
    (a.tmp/'evidencia_F.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    return result


if __name__=='__main__':
    sys.exit(main())

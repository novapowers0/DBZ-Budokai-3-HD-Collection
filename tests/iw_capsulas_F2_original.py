"""Control AME/estados GS retail + compilacion del bloque CapsulePick aislado.

No compila el juego. El extractor reutiliza cuerpos del proyecto sin editarlos.
Las funciones de GPU/logging se sustituyen solo en el arnes sin ventana.
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

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from iw_capsulas_F_datos import ELF,entrada,u
spec=importlib.util.spec_from_file_location('F',ROOT/'tests/iw_capsulas_F_original.py')
F=importlib.util.module_from_spec(spec);spec.loader.exec_module(F)


class MipsF2(F.retail.Mips):
    def step(self):
        w=self.load(self.pc,4)
        if self.pc not in self.hooks and w>>26==8:
            s,t=(w>>21)&31,(w>>16)&31
            result=F.retail.SX(self.r[s]&0xffffffff,32)+F.retail.SX(w&65535,16)
            if not -2147483648<=result<2147483648:raise F.retail.Unsupported('ADDI overflow MIPS')
            self.instructions.append((self.pc,w));self.word(t,result)
            old,self.pending=self.pending,None;self.pc=old if old is not None else self.pc+4
        elif self.pc not in self.hooks and w>>26==0 and (w&63) in (0x20,0x22):
            s,t,d=(w>>21)&31,(w>>16)&31,(w>>11)&31
            a,b=F.retail.SX(self.r[s]&0xffffffff,32),F.retail.SX(self.r[t]&0xffffffff,32)
            result=a+b if (w&63)==0x20 else a-b
            if not -2147483648<=result<2147483648:raise F.retail.Unsupported('ADD/SUB overflow MIPS')
            self.instructions.append((self.pc,w));self.word(d,result)
            old,self.pending=self.pending,None;self.pc=old if old is not None else self.pc+4
        else:super().step()


def lifecycle(elf):
    out=[]
    for op,pc in ((0xD4,0x2796b0),(0xD5,0x279630),(0xD6,0x279560)):
        for present in (0,1):
            for pause in (0,1):
                for after in (2,-1):
                    m=F.retail.Mips(elf);reg,arg,ef=0x900000,0x901000,0x902000
                    m.store(reg+44,4,arg);m.store(arg,4,3);m.store(0x5b5af0+12,4,ef if present else 0)
                    m.store(0x49cb70,4,pause);m.store(ef+0x48,4,2);events=[]
                    def update(): events.append(1);m.store(ef+0x48,4,after)
                    m.hooks[0x277830]=update
                    m.hooks[0x277630]=lambda:events.append(2)
                    m.hooks[0x2786e0]=lambda:None
                    m.hooks[0x278620]=lambda:None
                    m.observe=lambda pc:events.append(3) if pc==0x277e90 else None
                    ret=F.call(m,pc,(0,reg));assert ret==1
                    out.append(dict(op=op,present=present,pause=pause,before=2,after=after,
                        slot=int(m.load(0x5b5af0+12,4)!=0),timer=F.retail.SX(m.load(ef+0x48,4),32),events=events,
                        instructions=m.instructions))
    return out


def gs(elf):
    m=F.retail.Mips(elf)
    for a in (0x40f9c0,0x40f9c8):m.store(a,8,0x44)
    for a in (0x40f9e0,0x40f9f0):
        for j in range(4):m.store(a+4*j,4,F.FB(1))
    m.store(0x49c018,8,0x42);m.store(0x49cb28,4,0)
    for j in range(4):m.store(0x458eb0+4*j,4,F.FB(1 if j==3 else 0))
    def sample():
        return [m.load(a,4) for a in (0x49be20,0x49be24,0x49c650,0x49c654,0x40f9c0,0x40f9c8)]+[
            m.load(a+4*j,4) for a in (0x40f9e0,0x40f9f0) for j in range(4)]+[m.load(0x49be7c,4)]
    F.call(m,0x263290,(0,));first=sample()
    F.call(m,0x263230);m.store(0x49be7c,4,F.FB(.025));F.call(m,0x263460,(0,));second=sample()
    return dict(samples=[first,second],instructions=m.instructions,hooks=m.boundaries)


def light_matrix(elf):
    samples=F.light_cases(elf)
    lights=[next(s for s in samples if s['id']==id and s['clip']==0 and s['frame']==0) for id in (989,990)]
    lights.sort(key=lambda s:F.BF(s['color'][3]),reverse=True)
    m=MipsF2(elf);bases=[0x900000,0x900100,0x900200,0x900300,0x900400]
    for i,a in enumerate(bases):m.store(0x48a010+4*i,4,a)
    m.store(0x48a024,4,bases[-1]);m.store(0x49c66c,4,1)
    directions=[s['direction'][:3]+[F.FB(0)] for s in lights]+[[F.FB(0)]*4,[F.FB(0)]*3+[F.FB(1)]]
    colors=[s['color'] for s in lights]+[[F.FB(0)]*4,[F.FB(.8)]*3+[F.FB(0)]]
    for i in range(4):
        dp,cp=0x901000+i*32,0x901010+i*32
        for j in range(4):m.store(dp+j*4,4,directions[i][j]);m.store(cp+j*4,4,colors[i][j])
        F.call(m,0x12c070,(i,1,dp,cp))
    return dict(lights=lights,ambient=colors[3],
        directions=[[m.load(bases[3]+4*i+16*j,4) for j in range(4)] for i in range(4)],
        colors=[[m.load(bases[4]+16*i+4*j,4) for j in range(4)] for i in range(4)],instructions=m.instructions)


def rpc_start(elf):
    irx=ROOT/'ps2_games/Infinite World (USA)/MODULES/AMSOUND.IRX';b=irx.read_bytes();m=F.retail.Mips(elf)
    for i in range(struct.unpack_from('<H',b,48)[0]):
        h=struct.unpack_from('<10I',b,u(b,32)+i*struct.unpack_from('<H',b,46)[0])
        if (h[2]&2) and h[1]!=8 and h[5]:m.mem[h[3]:h[3]+h[5]]=b[h[4]:h[4]+h[5]]
    calls=[]
    for pc in (0x4460,0x4470,0x4468):
        def hook(pc=pc):calls.append(dict(pc=pc,args=[v&0xffffffff for v in m.r[4:7]]));m.low(2,0)
        m.hooks[pc]=hook
    m.store(0x900000,4,0)
    reply=F.call(m,0x12c,(0x13,0x900000,4))
    assert any(c['pc']==0x4468 and c['args'][2]==1 for c in calls),'RPC13 no inicia'
    return dict(path=str(irx),sha256=hashlib.sha256(b).hexdigest(),calls=calls,reply=m.load(reply,4),instructions=m.instructions,
                limit='imports MODSESQ sustituidos: se prueba dispatch/inicio, NO se reproduce el MIDI')


def extract(text,signature,start=0):
    i=text.index(signature,start);p=text.index('{',i);depth=1;j=p+1
    while depth:
        depth+=(text[j]=='{')-(text[j]=='}');j+=1
    return text[i:j],j


def motor_sources(out):
    # Solo fronteras SDK/GPU: datos de geometria, texturas, animacion y todos
    # los cuerpos de CapsulePick son los de produccion, no versiones espejo.
    stubs=out/'include';stubs.mkdir();(stubs/'rex').mkdir();(stubs/'rex/ui').mkdir()
    (stubs/'rex/logging.h').write_text('''#pragma once
namespace rex { template<class... T> inline void LogF2(const char*,const T&...) {} }
#define REXLOG_INFO(...) rex::LogF2(__VA_ARGS__)
#define REXLOG_WARN(...) rex::LogF2(__VA_ARGS__)
''')
    (stubs/'rex/ui/immediate_drawer.h').write_text('''#pragma once
#include <cstdint>
namespace rex::ui { class ImmediateTexture {public: virtual ~ImmediateTexture()=default;
uint32_t width,height;protected:ImmediateTexture(uint32_t w,uint32_t h):width(w),height(h){} }; }
''')
    (stubs/'rex/ui/imgui_drawer.h').write_text('''#pragma once
#include <imgui.h>
namespace rex::ui {inline void BlendA(const ImDrawList*,const ImDrawCmd*){}
inline void BlendB(const ImDrawList*,const ImDrawCmd*){}
inline constexpr ImDrawCallback kImDrawBlendAlpha=BlendA,kImDrawBlendAdditive=BlendB;}
''')
    modelo=(ROOT/'src/ps2_modelo.cpp').read_text(encoding='utf-8')
    # Las dependencias nuevas del bloque extraido conservan sus namespaces
    # globales tambien en los arneses anteriores (no copiar su implementacion).
    bloques=['#include "'+(ROOT/'src/iw_capsulas_render.h').as_posix()+'"',
             '#include "'+(ROOT/'src/iw_azar.h').as_posix()+'"',
             'namespace dbz3::ps2 { namespace P=dbz3::pasarela;']
    for name in ('V3 Unitaria(', 'bool LeerAmo(', 'Anim LeerAnim(', 'void Posar(', 'void Centrar(',
                 'uint32_t RampasPiel(', 'Modelo CargarSuelto('):bloques.append(extract(modelo,name)[0])
    pos=0
    for _ in range(2):
        b,pos=extract(modelo,'void Escena::Poner(',pos);bloques.append(b)
    for name in ('void Escena::PonerT(', 'static uint16_t Media(', 'static ImVec2 Empaquetar(', 'void Escena::Pintar('):
        bloques.append(extract(modelo,name)[0])
    bloques.append('}')
    texturas=(ROOT/'src/pasarela_b1.cpp').read_text(encoding='utf-8')
    bloques.append('namespace dbz3::pasarela {')
    for name in ('uint32_t U32(', 'uint16_t U16(', 'int AmtN(', 'Imagen Textura(', 'int CuantasAmt(', 'Imagen TexturaAmt('):
        bloques.append(extract(texturas,name)[0])
    bloques.append('}')
    iw=(ROOT/'src/pasarela_iw.cpp').read_text(encoding='utf-8')
    inicio=iw.index('struct AmeCapsulasSpx final');fin=iw.index('bool PruebaCapsulasSpxActiva(',inicio)
    bloques+=['namespace dbz3::pasarela { namespace { namespace SPXC=dbz3::iw::spx::capsulas;',iw[inicio:fin],'} }']
    cuerpos=out/'cuerpos_F2.inl';cuerpos.write_text('\n'.join(bloques),encoding='utf-8')
    return stubs,cuerpos


def main():
    p=argparse.ArgumentParser();p.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'));a=p.parse_args()
    assert a.tmp.is_dir();out=a.tmp/('capsulas_F2_'+str(__import__('time').time_ns()));out.mkdir()
    elf=ELF.read_bytes();e=dict(lifecycle=lifecycle(elf),gs=gs(elf),light_matrix=light_matrix(elf),
        rpc13=rpc_start(elf),elf_sha256=hashlib.sha256(elf).hexdigest())
    (out/'original_F2.json').write_text(json.dumps(e,indent=2),encoding='utf-8')
    b=bytearray()
    def word(v):b.extend(struct.pack('<I',v&0xffffffff))
    def blob(v):word(len(v));b.extend(v)
    word(0x46324350);word(len(e['lifecycle']))
    for c in e['lifecycle']:
        for k in ('op','present','pause','before','after','slot','timer'):word(c[k])
        word(len(c['events']))
        for v in c['events']:word(v)
    for s in e['gs']['samples']:
        for v in s:word(v)
    for l in e['light_matrix']['lights']:
        for v in l['direction']+l['color']:word(v)
    for v in e['light_matrix']['ambient']:word(v)
    for name in ('directions','colors'):
        for row in e['light_matrix'][name]:
            for v in row:word(v)
    blob(entrada(972));fixture=out/'fixture_F2.bin';fixture.write_bytes(b)
    commands=[]
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,
            env=dict(os.environ,TMP=str(out),TEMP=str(out),TMPDIR=str(out)))
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        commands.append(dict(command=list(map(str,cmd)),returncode=r.returncode,log=name+'.log'));print(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name)
    compiler='C:/Program Files/LLVM/bin/clang++.exe';exe=out/'iw_capsulas_F2_prueba.exe'
    run([compiler,'-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined',
         '-fsanitize-trap=all',ROOT/'tests/iw_capsulas_F2_prueba.cpp','-o',exe],'compilar_control')
    run([exe,fixture],'control')
    stubs,cuerpos=motor_sources(out)
    b=bytearray()
    def word(v):b.extend(struct.pack('<I',v&0xffffffff))
    def blob(v):word(len(v));b.extend(v)
    blob(elf);ids=sorted(set(u(elf,0x472f40-0x100000+0x80+4*i) for i in range(45)));word(len(ids))
    for id in ids:word(id);blob(entrada(id))
    fixture_motor=out/'bancos_F2.bin';fixture_motor.write_bytes(b)
    imgui=ROOT/'rexglue-sdk-0.10/thirdparty/imgui';motor=out/'iw_capsulas_F2_motor.exe'
    run([compiler,'-std=c++23','-O1','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-D_CRT_SECURE_NO_WARNINGS',
         '-fsanitize=undefined','-fsanitize-trap=all','-I'+str(stubs),'-I'+str(imgui),
         '-DDBZ3_CAPSULAS_F2_CUERPOS="'+cuerpos.as_posix()+'"',ROOT/'tests/iw_capsulas_F2_motor.cpp',
         imgui/'imgui.cpp',imgui/'imgui_draw.cpp',imgui/'imgui_tables.cpp',imgui/'imgui_widgets.cpp','-o',motor],'compilar_motor')
    run([motor,fixture_motor],'motor')
    e['commands']=commands;e['status']='PASS'
    e['fuentes']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
        (Path(__file__),ROOT/'src/iw_spx.h',ROOT/'src/iw_spx_capsulas.h',ROOT/'src/pasarela_iw.cpp',
         ROOT/'src/iw_ame.h',ROOT/'src/iw_iop.h',ROOT/'tests/iw_capsulas_F2_motor.cpp',ROOT/'tests/iw_capsulas_F2_prueba.cpp')}
    e['limites']=['AME lifecycle: regulator/draw sustituidos para verificar solo dispatch EE, no particulas completas',
        'GS: ejecuta263290/263230/263460 y setters retail completos, no GPU/VU del modelo',
        'motor: cuerpos CapsulePick y lectores AMO/AMM/AMT/PS2-ImGui extraidos sin cambios; no juego completo',
        'AFS del motor sirven los bancos originales del fixture; GPU/logging/cvar son servicios sustituidos',
        'vertices ImGui reales sin imagen GPU ni salida sonora; renderer VU/doble contexto permanece bloqueante']
    (out/'evidencia_F2.json').write_text(json.dumps(e,indent=2),encoding='utf-8')
    print('EVIDENCIA',out)
    return 0


if __name__=='__main__':sys.exit(main())

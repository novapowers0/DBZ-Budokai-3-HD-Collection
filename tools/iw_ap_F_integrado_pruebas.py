"""Consumidores de integracion extraidos: AP9/0/5, audio y PasosAp.
Sin juego/build del juego. Si iw_azar aun no existe, declara el limite del doble
de contrato en el resultado: jamas instala ese doble en src.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from campo_F_pruebas import funcion
from campo_F_investigar import Elf

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from iw_ap_F_original import EE


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF/ap/integrado'))
    ap.add_argument('--juego',type=Path,default=Path('ps2_games/Infinite World (USA)'))
    a=ap.parse_args();assert a.tmp.is_dir();root=Path(__file__).resolve().parents[1]
    anim=(root/'src/anim_iw.cpp').read_text(encoding='utf-8');mini=(root/'src/iw_minijuegos.cpp').read_text(encoding='utf-8');ame=(root/'src/iw_ame.h').read_text(encoding='utf-8')
    antes=(a.tmp/'iw_minijuegos.cpp.antes').read_text(encoding='utf-8')
    for marker in ('void AuraDeCuerpo(','void Polvo(','void Efectos('):assert funcion(mini,marker)==funcion(antes,marker),marker
    assert 'semilla_pasos_ap_' not in mini and 'static uint32_t semilla=1' not in anim
    assert 'iwazar::Global().Iniciar(P::LeerElf(iw))' in funcion(anim,'bool Leer(uint8_t*')
    assert 'iwazar::Global().Iniciar(P::LeerElf(iw))' in funcion(mini,'bool Cargar(Datos&')
    assert 'ConsumirImpactosAP(base)' in funcion(mini,'void Fotograma(')
    assert 'PrepararContextoAp(x,codigo)' in funcion(anim,'void Arrancar(PPCContext&')
    assert 'PrepararContextoAp(l.x,codigo)' in funcion(anim,'void Tick(PPCContext&')
    assert 'QuitarContextoAp(l.x)' in funcion(anim,'void Quitar(PPCContext&')
    assert mini.count('VelocidadCampoAp(')==5 # definicion, dosNPC, andar y scatto
    # Motor AME y pintor REALES; el receptor solo registra primitivas/mezclas.
    body=ame[ame.index('namespace dbz3::iwame {'):ame.index('// EU2716E0/2719A0')]
    (a.tmp/'ame.h').write_text(body+'\n}}\n',encoding='utf-8')
    globals_ap=anim[anim.index('struct ContextoApActor {'):anim.index('// Roles del cache IW:')]
    pieces=[globals_ap]+[funcion(anim,k) for k in ('bool AnclaImpactoAp(','bool ImpactoAp5(',
        'void DispararEvento(uint8_t*','void DispararEvento(PPCContext&','void AvanzarEventos(',
        'float VelocidadAP(','void ConsumirImpactosAP(')]
    logic='namespace dbz3::anim {\n'+'\n'.join(pieces)+'\n}\nnamespace dbz3::iwmini {\n'
    logic+='\n'.join(funcion(mini,k) for k in ('float LoadF(uint8_t*','V3 Leer(uint8_t*','float VelocidadCampoAp(','int EscenaApCampo(','bool EventoApImpacto(','bool EventoApCampo('))+'\n}\n'
    (a.tmp/'logica.h').write_text(logic,encoding='utf-8')
    panel='namespace dbz3::iwmini {struct PanelPrueba {\n'
    panel+='std::vector<std::shared_ptr<iwame::ame::Instancia>> pasos_ap_;std::vector<Tex>tx_pasos_ap_;\n'
    panel+='struct ImpactoVivo {std::shared_ptr<iwame::ame::Instancia>in;V3 p;std::array<float,4>q;};\nstd::vector<ImpactoVivo> impactos_ap_;std::vector<Tex>tx_impactos_ap_;\n'
    panel+='\n'.join(funcion(mini,k) for k in ('void ImpactosAp(','void PasosAp('))+'\n};}\n'
    (a.tmp/'panel.h').write_text(panel,encoding='utf-8')
    real=root/'src/iw_azar.h';contrato=not real.exists()
    if contrato:
        # Prueba de cableado del contrato comunicado por el coordinador. No se
        # considera verificacion de SU implementacion thread-safe aun ausente.
        codigo='''#pragma once
namespace dbz3::iwazar {
struct Contexto {
 std::mutex mu;uint32_t estado=0;bool iniciado=false;
 void Iniciar(const std::vector<uint8_t>&){std::lock_guard l(mu);if(!iniciado){estado=0x87654321u;iniciado=true;}}
 uint32_t Cambiar(uint32_t s){std::lock_guard l(mu);auto a=estado;estado=s;return a;}
 uint32_t Memory(int n){std::lock_guard l(mu);while(n-->0)estado=(estado<<1)|(((estado>>31)^(estado>>4))&1);return estado-1;}
 template<class F>void ConEstado(F f){std::lock_guard l(mu);f(estado);}
};inline Contexto& Global(){static Contexto c;return c;}
}
'''
        real=a.tmp/'azar_contrato.h';real.write_text(codigo,encoding='utf-8')
    commands=[]
    def run(cmd,name):
        p=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,cwd=root)
        (a.tmp/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8');commands.append(dict(comando=list(map(str,cmd)),exit=p.returncode));print(name,p.returncode,flush=True)
        if p.returncode:print(p.stdout+p.stderr)
        assert p.returncode==0,name
        return p.stdout
    cmd=['C:/Program Files/LLVM/bin/clang++.exe','-std=c++23','-O1','-Wall','-Wextra','-D_CRT_SECURE_NO_WARNINGS',
         '-ffp-contract=off','-fsanitize=undefined','-fsanitize-trap=all']
    for macro,path in [('IW_AP_F_AZAR',real),('IW_AP_F_AME',a.tmp/'ame.h'),('IW_AP_F_LOGICA',a.tmp/'logica.h'),('IW_AP_F_PANEL',a.tmp/'panel.h')]:
        cmd.append('-D'+macro+'="'+str(path.resolve()).replace('\\','/')+'"')
    cmd += [root/'tests/iw_ap_F_integrado.cpp','-o',a.tmp/'integrado.exe']
    run(cmd,'compilar');log=run([a.tmp/'integrado.exe',a.juego.resolve()],'consumidores')
    eu=Elf(Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47'))
    c=EE(eu.b);g=0x800000;c.write(0x49ca70,g);c.write(g+0x8b8c,7)
    def memset(vm):
        p,n=vm.r[4]&0xffffffff,vm.r[6]&0xffffffff;c.m[p:p+n]=bytes([vm.r[5]&255])*n;vm.r[2]=p
    c.run(0x20e3d0,hooks={0x11def8:memset});assert c.read(g+0x8b8c)==0
    assert c.read(0x1fc294)==0x2404032d # apertura original813
    # Fabricas originales de los dos subtipos portados. Se observa el paquete
    # que entregan a1FA330, sin hacer pasar un callback observado por dibujo.
    factory=[]
    for pc,esperado in ((0x1fb870,1),(0x1fb720,6)):
        c.reset();p=0xc80000;c.r[4]=p;c.write(p,0xc90000)
        def cam(vm):
            for i,v in enumerate((0,0,-1,0)):c.putf(vm.r[4]+4*i,v)
        observado=[]
        c.run(pc,hooks={0x1ebc60:cam,0x1fa330:lambda vm:observado.append(c.read(vm.r[4]+0x20,2))})
        assert observado==[esperado],(hex(pc),observado);factory.append(dict(funcion=hex(pc),recurso=esperado))
    us=Elf(a.juego/'SLUS_218.42')
    def nombres(e,tabla):
        return [e.b[e.off(e.u32(tabla+4*i),1):].split(b'\0',1)[0].decode('ascii') for i in range(21)]
    assert nombres(eu,0x458ed4)==nombres(us,0x4586d4)
    assert struct.unpack('<4f',eu.bytes(0x44bb90,16))==(0,1,0,1)
    result=dict(comandos=commands,resultado=log.strip(),rng_header_real=not contrato,
                fuente_rng=str(real),original_escena=0,fabricas_originales=factory,roles_por_nombre=21,
                fuentes={n:hashlib.sha256((root/'src'/n).read_bytes()).hexdigest() for n in ('anim_iw.cpp','anim_iw.h','iw_minijuegos.cpp','iw_minijuegos.h')},
                limites=['Sin build/juego ni salida GPU; primitivas registradas por un receptor de prueba',
                         'Anclas contrastadas con roles reales ELF y esquema AWO simulado; el runtime puede rechazar rigs no reconocidos',
                         'AP5 solo subtipos0/1;2 requiere anclas previas; AP4 y paleta AMM boca siguen pendientes'])
    if contrato:result['limites'].append('iw_azar.h aun ausente: ensayo de contrato/cableado, no de la implementacion final del coordinador')
    (a.tmp/'verificado_integrado.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('PASS offline integracion AP; RNG real:',not contrato,'evidencia',a.tmp/'verificado_integrado.json')


if __name__=='__main__':main()

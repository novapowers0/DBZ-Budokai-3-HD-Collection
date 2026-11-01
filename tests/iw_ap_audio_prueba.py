"""Contraste offline AP7/ELF/IOP retail; nunca arranca el juego ni reproduce audio.
python tests/iw_ap_audio_prueba.py --tmp D:/tmp/solIWV
Opcional --antes <snapshot anim_iw.cpp> para medir la sustitucion anterior.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

U = lambda b, o: struct.unpack_from('<I', b, o)[0]
H = lambda b, o: struct.unpack_from('<H', b, o)[0]
F = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]


def hijo(b, k):
    o, n = struct.unpack_from('<II', b, U(b, 20) + k * 16)
    return b[o:o+n]


def entrada(path, k):
    with path.open('rb') as f:
        f.seek(8+8*k)
        o, n = struct.unpack('<II', f.read(8))
        f.seek(o)
        return f.read(n)


def pcm_referencia(amb, sonido):
    """Resolucion independiente de IECS + filtro PS-ADPCM; cero muestras embebidas."""
    body, ind, sq = [hijo(amb, k) for k in range(3)]
    sc, sb, vi = sq.index(b'IECSqseS'), ind.index(b'IECSbteS'), ind.index(b'IECSigaV')
    g = sc + U(sq, sc+32)
    if sonido > U(sq, g):
        return None
    tab = g+4+U(sq, g+4)
    rec = sc+U(sq, tab+4*sonido)
    p = rec+U(sq, rec)
    ev = sq[p:p+U(sq, rec+8)]
    if len(ev) != 7 or ev[0:2] != b'\0\x90' or ev[5:] != b'\0\xff' or sq[rec+4:rec+6] != b'\x7f\x40':
        return None
    programa, key, vel = ev[2:5]
    bank = sb+U(ind, sb+16)
    prog = sb+U(ind, bank+4+programa*4)
    tono = prog+U(ind, prog)+ind[prog+4]*(key-ind[prog+5])
    if ind[tono+2] or ind[tono+4:tono+7] != b'\x40\0\0':
        return None
    vag = H(ind, tono)
    vrec = vi+U(ind, vi+16+4*vag)
    offset, hz = U(ind, vrec), H(ind, vrec+4)
    pitch = 4096*int(hz*65536/48000) >> 16
    gain = F(vel*ind[tono+3]/16384)
    k = [(0,0),(60,0),(115,-52),(98,-55),(122,-60)]
    prev, prev2, mono = 0, 0, []
    for off in range(offset, len(body)-15, 16):
        filt, shift = body[off] >> 4, body[off] & 15
        for byte in body[off+2:off+16]:
            for nib in (byte & 15, byte >> 4):
                signed = nib if nib < 8 else nib-16
                sample = (signed*4096 >> shift) + ((prev*k[filt][0]+prev2*k[filt][1]+32) >> 6)
                sample = max(-32768,min(32767,sample))
                prev2, prev = prev, sample
                mono.append(sample)
        if body[off+1] & 1:
            break
    phase, pcm = 0, []
    while phase//4096 < len(mono):
        i = phase//4096
        a, b = mono[i], mono[min(i+1,len(mono)-1)]
        x = F(F(a+F((b-a)*(phase%4096/4096)))*gain)
        pcm.append(max(-32768,min(32767,int(x))))
        phase += pitch
    h = 14695981039346656037
    for x in pcm:
        for b in struct.pack('<h', x):
            h = ((h ^ b)*1099511628211) & ((1<<64)-1)
    return vag, pitch, len(pcm), h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tmp',type=Path,required=True)
    ap.add_argument('--juego',type=Path,default=Path('ps2_games/Infinite World (USA)'))
    ap.add_argument('--antes',type=Path)
    ap.add_argument('--integracion',action='store_true',help='solo syntax-check de anim_iw.cpp; no genera objetos ni ejecutable del juego')
    args = ap.parse_args()
    assert args.tmp.is_dir(), 'el directorio temporal debe existir'
    out = args.tmp
    root = Path(__file__).resolve().parents[1]
    game = args.juego.resolve()
    elf = (game/'SLUS_218.42').read_bytes()
    commands = []

    def run(cmd, nombre):
        p = subprocess.run(list(map(str,cmd)),capture_output=True,text=True,cwd=root)
        (out/(nombre+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
        commands.append(dict(comando=list(map(str,cmd)),exit=p.returncode,log=nombre+'.log'))
        print(nombre, 'exit', p.returncode)
        if p.returncode:
            print(p.stdout+p.stderr)
        assert p.returncode == 0, nombre
        return p.stdout

    compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
    if args.antes:
        src = args.antes.read_text(encoding='utf-8')
        cuerpo = src[src.index('struct SeB3 {'):src.index('// 06: "ha sonado')]
        driver = '#include <cstdint>\n#include <iostream>\n'+cuerpo+'''
int main() {for (auto p:{1u,2u,3u,7u}) {auto s=SeB3DeEvento(p);
std::cout<<p<<' '<<s.banco<<' '<<s.id<<'\\n';}}
'''
        (out/'ap_audio_antes.cpp').write_text(driver,encoding='utf-8')
        run([compiler,'-std=c++23',out/'ap_audio_antes.cpp','-o',out/'ap_audio_antes.exe'],'audio_antes_compilar')
        before = run([out/'ap_audio_antes.exe'],'audio_antes')
        assert before.splitlines() == ['1 2 2','2 2 2','3 4 19','7 2 2']

    # Se compila el decoder REAL; no se reemplaza su resolucion ni su PCM.
    iop = (root/'src/iw_iop.h').read_text(encoding='utf-8')
    (out/'iw_iop_core_audio.h').write_text(iop[iop.index('namespace dbz3::iwiop {'):],encoding='utf-8')
    exe = out/'iw_ap_audio_prueba.exe'
    base = [compiler,'-std=c++23','-O1','-Wall','-Wextra',
            '-DDBZ3_IW_IOP_CORE="'+str(out/'iw_iop_core_audio.h').replace('\\','/')+'"',
            root/'tests/iw_ap_audio_prueba.cpp','-o',exe]
    run(base,'audio_compilar')
    result = run([exe,game],'audio_resultado')
    # Misma prueba con deteccion de UB por trap: no necesita runtime instalado.
    run(base[:-1]+[out/'iw_ap_audio_trap.exe','-fsanitize=undefined','-fsanitize-trap=all'], 'audio_compilar_trap')
    trap = run([out/'iw_ap_audio_trap.exe',game],'audio_resultado_trap')
    assert trap == result
    if args.integracion:
        run([compiler,'-std=gnu++23','-fsyntax-only','-msse4.1','-Wno-deprecated-volatile',
             '-DDBZ3_DUAL_REGION=1','-DNOMINMAX','-DREXGLUE_BUILD_CONFIG="Release"',
             '-DREX_HAS_D3D12=1','-DREX_HAS_VULKAN=1','-DSPDLOG_COMPILED_LIB',
             '-DSPDLOG_FMT_EXTERNAL','-DWIN32_LEAN_AND_MEAN','-D_CRT_SECURE_NO_WARNINGS',
             '-I.','-Igenerated','-Igenerated_eu','-isystem','rexglue/include',
             '-isystem','rexglue/include/dxc','src/anim_iw.cpp'],'audio_integracion_sintaxis')

    # Argumentos de tipo6 obtenidos EJECUTANDO el assembly USA retail, no el
    # pseudocodigo (que pierde todos los argumentos de Hits_TransformationSounds).
    def va(a):
        for i in range(H(elf,44)):
            p = U(elf,28)+i*H(elf,42)
            t,o,v,_,n = struct.unpack_from('<5I',elf,p)
            if t==1 and v<=a<v+n:
                return o+a-v
        raise ValueError(hex(a))

    def original_campo(valor,mini,actor):
        r = [0]*32
        r[19], r[17], r[16] = 0x1000, 0x2000, 0x3000
        mem = {0x1000+0x1380:0,0x2000+0x1e8:actor,0x3000+4:mini}
        pc = 0x2a866c if valor==5 else 0x2a87f0
        branch = None
        for _ in range(100):
            w = U(elf,va(pc));op=w>>26;s=(w>>21)&31;t=(w>>16)&31;d=(w>>11)&31
            im = struct.unpack('<h',struct.pack('<H',w&65535))[0]
            old, branch = branch, None
            if op==0x23:r[t]=mem[r[s]+im]
            elif op==9:r[t]=(r[s]+im)&0xffffffff
            elif op==0:
                fn=w&63
                if fn==0:r[d]=(r[t]<<((w>>6)&31))&0xffffffff
                elif fn in (0x21,0x2d):r[d]=(r[s]+r[t])&0xffffffff
                else:raise AssertionError(hex(w))
            elif op==0xb:r[t]=int(r[s]<(im&0xffffffff))
            elif op==0xf:r[t]=(w&65535)<<16
            elif op==0x11:pass  # mtc1: f12/f13 no cambian los indices
            elif op in (4,5,0x14,0x15):
                take = (r[s]==r[t]) == (op in (4,0x14))
                if take:branch=pc+4+4*im
                elif op in (0x14,0x15):pc+=4 # branch likely anula delay slot
            elif op==3:
                assert (w&0x3ffffff)*4==0x2405b0
                # Todos los delay slots de estas llamadas son a3=0.
                assert U(elf,va(pc+4))==0x0000382d
                assert r[4]==0
                return r[5]
            else:raise AssertionError((hex(pc),hex(w)))
            r[0]=0;pc=old if old is not None else pc+4
            if pc==0x2a8974:return -1
        raise AssertionError('bucle del emulador de argumentos')

    checked = 0
    pcm_checks = 0
    for line in result.splitlines():
        parts = line.split()
        if parts[0]=='CAMPO':
            v,m,a,actual=map(int,parts[1:])
            assert actual==original_campo(v,m,a),(v,m,a,actual,original_campo(v,m,a))
            checked+=1
        if parts[0]=='PCM':
            afs,bank,sound=parts[1],int(parts[2]),int(parts[3])
            actual=tuple(map(int,parts[4:]))
            expected=pcm_referencia(entrada(game/'USR'/afs,bank),sound)
            if expected is None:assert actual[2]==0,(parts,expected)
            else:assert actual==expected,(parts,expected)
            pcm_checks+=1
    # La carga original slot1 usa954 (no deducido del nombre del archivo).
    assert U(elf,va(0x302ccc))==0x240403ba
    assert U(elf,va(0x302cf0))==0x24030003
    assert U(elf,va(0x22faf4))==0x00a0202d  # a0=slot del llamante
    assert U(elf,va(0x22fafc))==0x00c0282d  # a1=valor, no constante
    corpus = next(x for x in result.splitlines() if x.startswith('CORPUS '))
    evidence = dict(original_elf_sha256=hashlib.sha256(elf).hexdigest(),
                    assembly_campo_casos=checked,pcm_bancos_secuencias=pcm_checks,
                    corpus=corpus,comandos=commands,
                    limites=['sin juego/ventana/salida sonora','PCM comparado offline; no SPU2 reverb/interpolacion',
                             'tipo6 tabla verificada, falta contexto/conexion host','tipos0/4/5/7/8/9 pendientes por tabla',
                             'sin certificacion de forma especial/cortes de voz ni secuenciador completo'])
    (out/'ap_audio_evidencia.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    print('PASS',checked,'argumentos tipo6 contra assembly;',pcm_checks,'PCM contra decoder independiente;',corpus)


if __name__=='__main__':
    main()

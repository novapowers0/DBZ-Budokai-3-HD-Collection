"""Evidencia local solIWF; solo lectura retail, resultados en --tmp existente."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import sys
import struct

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from iw_ap_audio_prueba import U, H, hijo, entrada


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tmp', type=Path, default=Path('D:/tmp/solIWF'))
    ap.add_argument('--juego', type=Path, default=Path('ps2_games/Infinite World (USA)'))
    ap.add_argument('--modulo')
    ap.add_argument('--ee', nargs=2, type=lambda v: int(v,0))
    ap.add_argument('--rng-global', action='store_true')
    a = ap.parse_args()
    assert a.tmp.is_dir()
    if a.rng_global:
        import capstone
        raw=(a.juego/'SLUS_218.42').read_bytes()
        secs=[struct.unpack_from('<10I',raw,U(raw,32)+i*H(raw,46)) for i in range(H(raw,48))]
        reg=next(s for s in secs if s[1]==0x70000006)
        gp=U(raw,reg[4]+20)
        refs=[]
        for sec in secs:
            if not sec[2]&4:continue
            for off in range(sec[4],sec[4]+sec[5],4):
                w=U(raw,off)
                if (w>>21&31)==28 and (w&65535)==0x8018 and w>>26 in (0x23,0x2b):
                    refs.append(dict(pc=hex(sec[3]+off-sec[4]),op='lw' if w>>26==0x23 else 'sw',reg=(w>>16)&31))
        address=(gp-0x7fe8)&0xffffffff
        sec=next(s for s in secs if s[3]<=address<s[3]+s[5] and s[1]!=8)
        initial=U(raw,sec[4]+address-sec[3])
        result=dict(gp=hex(gp),direccion=hex(address),estado_inicial=initial,referencias=refs)
        (a.tmp/'rng_global.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(json.dumps(result,indent=2));return
    if a.ee:
        import capstone
        elf = (a.juego/'SLUS_218.42').read_bytes()
        dis = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS64|capstone.CS_MODE_LITTLE_ENDIAN)
        dis.skipdata=True
        lo, hi = a.ee
        for i in range(H(elf,44)):
            p=U(elf,28)+i*H(elf,42)
            if U(elf,p)==1 and U(elf,p+8)<=lo<hi<=U(elf,p+8)+U(elf,p+16):
                off=U(elf,p+4)+lo-U(elf,p+8)
                for ins in dis.disasm(elf[off:off+hi-lo],lo):
                    print(f'{ins.address:08x}: {ins.mnemonic} {ins.op_str}')
                return
        raise ValueError(a.ee)
    if a.modulo:
        import capstone
        path = a.juego/'MODULES'/a.modulo
        raw = path.read_bytes()
        sections = [struct.unpack_from('<10I', raw, U(raw,32)+i*H(raw,46)) for i in range(H(raw,48))]
        names = sections[H(raw,50)]
        strings = raw[names[4]:names[4]+names[5]]
        dis = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32|capstone.CS_MODE_LITTLE_ENDIAN)
        lines = [f'{path} SHA256 {hashlib.sha256(raw).hexdigest()}']
        for sec in sections:
            name = strings[sec[0]:].split(b'\0')[0].decode()
            lines.append(f'SECCION {name} direccion {sec[3]:x} offset {sec[4]:x} bytes {sec[5]:x}')
            if sec[2]&4:
                dis.skipdata=True
                lines.extend(f'{ins.address:08x}: {ins.mnemonic} {ins.op_str}' for ins in dis.disasm(raw[sec[4]:sec[4]+sec[5]],sec[3]))
            if name=='.rodata':
                for off in range(0,sec[5]-3,4):lines.append(f'DATO {sec[3]+off:08x}: {U(raw,sec[4]+off):08x}')
        for off in range(0,len(raw)-20,4):
            if U(raw,off)==0x41c00000:
                name=raw[off+12:off+20].split(b'\0')[0].decode('ascii',errors='replace')
                pos=off+20;ordinal=0
                while pos+4<=len(raw) and U(raw,pos):
                    lines.append(f'EXPORT {name} ordinal {ordinal} direccion {U(raw,pos):x}')
                    pos+=4;ordinal+=1
            if U(raw,off)!=0x41e00000:continue
            name=raw[off+12:off+20].split(b'\0')[0].decode('ascii',errors='replace')
            pos=off+20
            while pos+8<=len(raw) and U(raw,pos)==0x03e00008:
                va=next((sec[3]+pos-sec[4] for sec in sections if sec[4]<=pos<sec[4]+sec[5] and sec[2]&4),None)
                lines.append(f'IMPORT {va!s} {hex(va) if va is not None else "?"} {name} ordinal {U(raw,pos+4)&65535}')
                pos+=8
        dest = a.tmp/(a.modulo+'.asm.txt')
        dest.write_text('\n'.join(lines), encoding='utf-8')
        print(dest)
        return
    root = Path(__file__).resolve().parents[1]
    for name in ('iw_iop.h', 'iw_adx.h'):
        dest = a.tmp / (name + '.antes')
        if not dest.exists():
            dest.write_bytes((root / 'src' / name).read_bytes())
    banks = {}
    for afs, ids in [('DATA_CMN.AFS', range(933, 961)), ('ADX_USA.AFS', (1472, 1476))]:
        for bank in ids:
            amb = entrada(a.juego / 'USR' / afs, bank)
            if amb[:4] != b'#AMB' or U(amb, 16) < 3:
                continue
            body, ind, seq = [hijo(amb, k) for k in range(3)]
            sc = seq.find(b'IECSqseS')
            if sc < 0:
                continue
            group = sc + U(seq, sc+32)
            print('CABECERA', afs, bank, seq[sc:sc+48].hex(), 'GRUPO', seq[group:group+16].hex())
            tab = group + 4 + U(seq, group+4)
            rows = []
            for snd in range(U(seq, group)+1):
                rec = sc+U(seq, tab+4*snd)
                start = rec+U(seq, rec)
                flow = seq[start:start+U(seq, rec+8)]
                row = dict(sonido=snd, cabecera=seq[rec:rec+16].hex(), flujo=flow.hex())
                sb = ind.find(b'IECSbteS')
                if len(flow) in (7,13) and flow[:2] == b'\0\x90':
                    prog = sb+U(ind, sb+U(ind, sb+16)+4+flow[2]*4)
                    tone = prog+U(ind, prog)+ind[prog+4]*(flow[3]-ind[prog+5])
                    row['tono'] = ind[tone:tone+50].hex()
                    vi=ind.find(b'IECSigaV')
                    vr=vi+U(ind,vi+16+4*H(ind,tone))
                    start=U(ind,vr)
                    flags=[]
                    for off in range(start,len(body)-15,16):
                        if body[off+1]&5:flags.append((off-start,body[off+1]))
                        if body[off+1]&1:break
                    row['vag'] = dict(hz=H(ind,vr+4), muestras=(off-start+16)//16*28,flags=flags)
                rows.append(row)
            banks[f'{afs}/{bank}'] = rows
    result = dict(bancos=banks)
    (a.tmp/'inventario_F.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    for bank, rows in banks.items():
        print(bank, 'max', len(rows)-1, 'flujos', dict(collections.Counter(len(bytes.fromhex(r['flujo'])) for r in rows)))
        for row in rows:
            if row['sonido'] in (37,39,40,44,45,51,58,88,94,95):
                print(row)
    print('Respaldo e inventario:', a.tmp)


if __name__ == '__main__':
    main()

"""Inventario AP/SESQ retail de IW9c (offline, sin reproduccion sonora)."""
import argparse
import collections
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from iw_ap_audio_prueba import U, H, hijo, entrada


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--juego', type=Path, default=Path('ps2_games/Infinite World (USA)'))
    ap.add_argument('--assembly',action='store_true')
    ap.add_argument('--inicio',type=lambda s:int(s,0),default=0x22fd30)
    ap.add_argument('--fin',type=lambda s:int(s,0),default=0x22ffb0)
    a = ap.parse_args()
    assert a.salida.parent.is_dir()
    events = []
    banks = {}
    for cmn in (204, *range(912, 933)):
        amb = entrada(a.juego/'USR/DATA_CMN.AFS', cmn)
        for hi in range(U(amb, 16)):
            b = hijo(amb, hi)
            if b[:4] != b'#BSK':
                continue
            for cod in range(U(b, 16)):
                blk = U(b, U(b, 20)+4*cod)
                if not blk:
                    continue
                for ln in range(U(b, blk+40)):
                    l = U(b, blk+44)+8*ln
                    if H(b, l) != 7:
                        continue
                    for i in range(H(b, l+2)):
                        e = U(b, l+4)+16*i
                        events.append(dict(cmn=cmn, hijo=hi, codigo=cod, cuadro=H(b, e),
                                           tipo=U(b, e+4), valor=U(b, e+8)))
    for afs, ids in [('DATA_CMN.AFS', [938, 954]), ('ADX_USA.AFS', range(1460, 1505, 2))]:
        for bank in ids:
            amb = entrada(a.juego/'USR'/afs, bank)
            if amb[:4] != b'#AMB':
                continue
            ind, sq = hijo(amb, 1), hijo(amb, 2)
            sc = sq.find(b'IECSqseS')
            if sc < 0:
                continue
            g = sc+U(sq, sc+32)
            tab = g+4+U(sq, g+4)
            seqs = []
            for snd in range(U(sq, g)+1):
                rec = sc+U(sq, tab+4*snd)
                p = rec+U(sq, rec)
                ev = sq[p:p+U(sq, rec+8)]
                seqs.append(dict(sonido=snd, cabecera=sq[rec:rec+16].hex(), flujo=ev.hex()))
            banks[f'{afs}/{bank}'] = seqs
    result = dict(eventos=events, bancos=banks,
                  tipos=dict(collections.Counter(e['tipo'] for e in events)))
    a.salida.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('AP', len(events), 'por tipo', result['tipos'])
    print('AP por banco', {c: dict(collections.Counter(e['tipo'] for e in events if e['cmn']==c))
          for c in sorted(set(e['cmn'] for e in events))})
    elf = (a.juego/'SLUS_218.42').read_bytes()
    def va(addr):
        for n in range(H(elf, 44)):
            p = U(elf, 28)+n*H(elf, 42)
            if U(elf, p)==1 and U(elf, p+8)<=addr<U(elf, p+8)+U(elf, p+16):
                return U(elf, p+4)+addr-U(elf, p+8)
        raise ValueError(hex(addr))
    if a.assembly:
        import capstone
        dis=capstone.Cs(capstone.CS_ARCH_MIPS,capstone.CS_MODE_MIPS64|capstone.CS_MODE_LITTLE_ENDIAN)
        for pc in range(a.inicio,a.fin,4):
            xs=list(dis.disasm(elf[va(pc):va(pc)+4],pc))
            print(hex(pc),xs[0].mnemonic+' '+xs[0].op_str if xs else hex(U(elf,va(pc))))
        return
    high = {}
    import struct
    for i in range(20):
        addr=U(elf,va(0x47fc00+4*i));n=U(elf,va(0x47fc50+4*i))
        print('ADX filtrado',i,n,[U(elf,va(addr+4*j)) for j in range(min(n,8))])
    print('AP7',[(e['cmn'],e['codigo'],e['valor']) for e in events if e['tipo']==7])
    for bank in (1471,1475):
        b=entrada(a.juego/'USR/ADX_USA.AFS',bank)
        print('BOCA',bank,len(b),b[:32].hex())
        if b[:4]==b'#AMB':print([(i,hijo(b,i)[:32].hex(),len(hijo(b,i))) for i in range(U(b,16))])
    for pc in range(0x22FD30, 0x22FFA0, 4):
        w = U(elf, va(pc)); op=w>>26; rs=w>>21&31; rt=w>>16&31
        if op==15:
            high[rt] = (w&65535)<<16
        elif op in (9, 13) and rs in high:
            addr = high[rs]+struct.unpack('<h', struct.pack('<H',w&65535))[0] if op==9 else high[rs]|(w&65535)
            if 0x400000<=addr<0x600000:
                print('TABLA voz', hex(pc),hex(addr), elf[va(addr):va(addr)+56].hex())
        if op==3:
            print('VOZ llamada',hex(pc),hex((w&0x3ffffff)*4),hex(U(elf,va(pc+4))))
    for lo,hi in ((0x31B000,0x31B200),(0x31ED00,0x31F100)):
        for pc in range(lo,hi,4):
            w=U(elf,va(pc))
            if w>>26==9 and w>>21&31==0 and w>>16&31==4:
                print('CARGA a0',hex(pc),w&65535)
    print('Eventos sonoros distintos', sorted(set((e['cmn'], e['tipo'], e['valor'])
          for e in events if e['tipo'] in (1, 2, 3))))
    for k, seqs in banks.items():
        if k.endswith('/938'):
            print(k, [(s['sonido'], s['cabecera'], s['flujo']) for s in seqs])
    for bank in (886,899,902,905,908,911):
        amb=entrada(a.juego/'USR/DATA_CMN.AFS',bank)
        print('FX paquete',bank,[(i,hijo(amb,i)[:4].decode('ascii',errors='replace'),len(hijo(amb,i)))
              for i in range(U(amb,16))])


if __name__ == '__main__':
    main()

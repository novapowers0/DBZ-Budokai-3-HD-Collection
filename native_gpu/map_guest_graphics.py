"""Map conservative graphics hook candidates in locally generated PPC code.

This does static constant propagation in straight-line instruction spans. It
records packet *emitters* as research evidence, not tested hook signatures.
Generated game source and assets are not copied into the report.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

OPCODES = {0x22:'DRAW_INDX',0x36:'DRAW_INDX_2',0x34:'DRAW_INDX_BIN',0x35:'DRAW_INDX_2_BIN',
           0x2d:'SET_CONSTANT',0x55:'SET_CONSTANT2',0x2f:'LOAD_ALU_CONSTANT',
           0x27:'IM_LOAD',0x2b:'IM_LOAD_IMMEDIATE'}
IMPORTS = {'VdSwap','VdInitializeRingBuffer','VdSetGraphicsInterruptCallback','VdGetSystemCommandBuffer'}
DEFINITION = re.compile(r'^DEFINE_REX_FUNC\(([^)]+)\) \{', re.M)


def scan(path):
    text = path.read_text(encoding='utf-8')
    starts = list(DEFINITION.finditer(text))
    results = []
    for k, match in enumerate(starts):
        body = text[match.end(): starts[k+1].start() if k+1 < len(starts) else len(text)]
        regs, packets = {}, []
        function = match.group(1)
        calls = re.findall(r'\b(sub_[0-9A-F]+)\(ctx, base\)', body)
        imports = sorted(IMPORTS & set(re.findall(r'__imp__(\w+)\(ctx, base\)', body)))
        for line in body.splitlines():
            if line.startswith('loc_'):
                regs.clear()
            if '// ' not in line:
                continue
            instruction = line.split('// ',1)[1].strip()
            parts = instruction.replace(',', ' ').split()
            if not parts:
                continue
            op = parts[0]
            if op in ('lis','li') and len(parts)==3:
                try: regs[parts[1]] = (int(parts[2],0) << (16 if op=='lis' else 0)) & 0xffffffff
                except ValueError: regs.pop(parts[1],None)
            elif op in ('ori','oris') and len(parts)==4:
                try:
                    if parts[2] in regs:
                        regs[parts[1]] = (regs[parts[2]] | (int(parts[3],0) << (16 if op=='oris' else 0))) & 0xffffffff
                    else: regs.pop(parts[1],None)
                except ValueError: regs.pop(parts[1],None)
            elif op=='mr' and len(parts)==3:
                if parts[2] in regs: regs[parts[1]]=regs[parts[2]]
                else: regs.pop(parts[1],None)
            elif op in ('stw','stwu','stwx','stwbrx') and len(parts)>=3:
                value = regs.get(parts[1])
                # Bit 0 is predication; normal D3D draws set it. Bit 1 is reserved.
                if value is not None and value >> 30 == 3 and value & 2 == 0:
                    opcode = value >> 8 & 0x7f
                    if opcode in OPCODES:
                        packets.append({'opcode':OPCODES[opcode], 'header':f'0x{value:08X}',
                                        'store_instruction':instruction})
                if op=='stwu':
                    base=re.search(r'\((r\d+)\)',instruction)
                    if base:regs.pop(base.group(1),None)
            elif op.startswith('b'):
                regs.clear()  # no cross-branch guesses / volatile state through calls
            elif op.startswith(('cmp','cmpl','st','mt')):
                pass
            elif len(parts)>1 and re.fullmatch(r'r\d+',parts[1]):
                regs.pop(parts[1],None)
        if packets or imports:
            results.append({'function':function,'partition':path.name,'kernel_calls':imports,
                            'packet_emitters':packets,'direct_callees':sorted(set(calls)),
                            'evidence':'Static PPC comment propagation only; live signatures not validated'})
    return results


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--generated',type=Path,required=True)
    parser.add_argument('--xex',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    entries=[]
    for path in sorted(args.generated.glob('*recomp.*.cpp')):
        entries.extend(scan(path))
    report={'source_xex_sha256':hashlib.sha256(args.xex.read_bytes()).hexdigest(),
        'source':'Local ReXGlue v0.10.0 EU codegen; native_gpu/map_guest_graphics.py',
        'status':'Research candidates; no hooks enabled in the installed game',
        'next_validation':'Trace entry arguments, world-space vertex buffers, materials and camera at these call sites',
        'functions':entries}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(f'{len(entries)} graphics candidates, {sum(bool(e["packet_emitters"]) for e in entries)} command emitters')
    for entry in entries:
        if entry['kernel_calls'] or any('DRAW' in p['opcode'] for p in entry['packet_emitters']):
            print(entry['function'],entry['kernel_calls'],sorted(set(p['opcode'] for p in entry['packet_emitters'])))

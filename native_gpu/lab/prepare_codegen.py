"""Enable optional lab tracing in a locally generated EU PCH.
Run after codegen; generated code and player assets must stay local.
"""
import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--generated',type=Path,required=True);a=p.parse_args()
header=a.generated/'dbz3_eu_pch.h';text=header.read_text();marker='// DBZ native GPU lab function-entry instrumentation'
if marker in text:text=text[:text.index(marker)]
text=text.rstrip()+'''\n\n// DBZ native GPU lab function-entry instrumentation
#if defined(DBZ3_NATIVE_LAB_TRACE)
#include "../native_gpu/lab/trace.h"
#undef REX_FUNC_PROLOGUE
#define REX_FUNC_PROLOGUE() do { constexpr uint32_t address=dbzlab::guestAddress(__func__); if constexpr(address>=0x82220000 && address<0x82260000 && address!=0x822340C8) dbzlab::enter(address,ctx,base); __builtin_assume(((size_t)base & 0x1F)==0); } while(0)
#endif
'''
if header.read_text()!=text:header.write_text(text)
print('Enabled lab trace behind DBZ3_NATIVE_LAB_TRACE; excluded GPU polling loop')

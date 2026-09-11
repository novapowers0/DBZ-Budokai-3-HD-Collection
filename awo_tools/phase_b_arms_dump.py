#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_b_arms_dump.py - Dump de los arms (skinning) del AWG0.

Busca si los arms contienen RANGOS de vertices (start,count) o indices al pool
(la "via posicional" que rompe al reordenar). Segun ESTRUCTURA_DIBUJO_HD:
  eje (80B) +0x34 = arm_ptr (rel AWG0); arm = [bone, ptr, 0, ptr_mat4x4, 0].
Uso: python phase_b_arms_dump.py <bin>
"""
import struct, sys
def be32(b,o): return struct.unpack(">I",b[o:o+4])[0]
def bef(b,o): return struct.unpack(">f",b[o:o+4])[0]
def main():
    b=open(sys.argv[1],"rb").read()
    awo=0x40; tbl=awo+be32(b,awo+0x1C); awg0=awo+be32(b,tbl)
    def g(o): return be32(b,awg0+o)
    n_bones=g(0x10); axes=g(0x14); mgo=g(0x20); seco=g(0x34); vb2o=g(0x2C)
    sec=awg0+seco+2; vb2=awg0+vb2o
    n_sec=(vb2-sec-2)//44
    print("n_bones=%d axes=0x%X (AWG0+0x%X) mg=0x%X n_sec=%d"%(n_bones,axes,axg:=axes,awg0+mgo,n_sec))
    abase=awg0+axes
    armptrs=[]
    for k in range(n_bones):
        e=abase+k*80
        ptr=be32(b,e+0x34)
        seal=be32(b,e+0x30)
        armptrs.append(ptr)
    print("arm_ptrs (rel AWG0) unicos:", sorted(set(armptrs))[:20], "... total", len(set(armptrs)))
    # dump del primer arm
    for ptr in sorted(set(armptrs))[:3]:
        if ptr==0: continue
        a=awg0+ptr
        print("\n=== arm @AWG0+0x%X ==="%ptr)
        for i in range(0,0x80,16):
            row=b[a+i:a+i+16]
            print("  +0x%02X  %s  | %s"%(i,' '.join('%08X'%be32(b,a+i+j) for j in range(0,16,4)),
                  ' '.join('%.3f'%bef(b,a+i+j) for j in range(0,16,4))))
if __name__=="__main__": main()

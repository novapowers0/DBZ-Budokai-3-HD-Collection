#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Efectos originales T3: paquete de ranura -> recursos tipados y texturas RGBA.

Lee las cinco familias de Crusher Ball, conserva sus dos bloques de parametros y
sus fases. No clona efectos de un donante. Salida TCB2 para el consumidor host.
La grafica GS se lee de los paquetes originales; no se reparte material del juego.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "mod center hd"))
from t3 import pak


def u32(b, p):
    return struct.unpack_from("<I", b, p)[0]


def children(b):
    table = pak(b)
    if table is None:
        raise ValueError("PAK de efectos invalido")
    return [b[o:o+s] for o,s in table]


def unswizzle8(src, w, h):
    out = np.zeros((h,w),np.uint8)
    for y in range(h):
        for x in range(w):
            block = (y & ~15)*w + (x & ~15)*2
            swap = (((y+2)>>2)&1)*4
            py = (((y & ~3)>>1)+(y&1))&7
            column = py*w*2 + ((x+swap)&7)*4
            byte = ((y>>1)&1)+((x>>2)&2)
            at = block+column+byte
            if at >= len(src):
                raise ValueError("PSMT8 fuera del paquete")
            out[y,x] = src[at]
    return out


def unswizzle4(src, w, h):
    # Transferencia PSMCT32 -> vista PSMT4 del GS (paginas 128x128).
    out = np.zeros((h,w),np.uint8)
    pages_v = (h+127)//128
    pages_h = (w+127)//128
    for y in range(h):
        for x in range(w):
            page = (y//128)*pages_h+x//128
            page_location = (page//pages_v)*32*h*2 + (page%pages_v)*64*4
            lx,ly=x&127,y&127
            block=((lx & ~31)>>1)*h+(ly & ~15)*2
            swap=(((y+2)>>2)&1)*4
            py=(((y & ~3)>>1)+(y&1))&7
            column=py*h*2+((x+swap)&7)*4
            at=page_location+block+column+((x>>3)&3)
            if at>=len(src):raise ValueError("PSMT4 fuera del paquete")
            out[y,x]=(src[at]>>(((y>>1)&1)*4))&15
    return out


def textures(b):
    if len(b)<32:raise ValueError("texturas T3: cabecera corta")
    count,table=u32(b,0),u32(b,4)*4
    if not 0<count<=32 or table+64*count>len(b):raise ValueError("tabla de texturas T3 invalida")
    out=[]
    for i in range(count):
        at=table+64*i
        po,co,ps,cs=struct.unpack_from('<4I',b,at)
        tex=struct.unpack_from('<Q',b,at+48)[0]
        w,h=1<<((tex>>26)&15),1<<((tex>>30)&15)
        psm=(tex>>20)&63
        po,co=po*4,co*4
        if w*h>1024*1024 or min(ps,cs)<128 or po+ps>len(b) or co+cs>len(b):
            raise ValueError("limites de textura T3 invalidos")
        raw=b[po+128:po+ps]
        pal=np.frombuffer(b[co+128:co+cs],np.uint8).reshape(-1,4).copy()
        if psm==19:
            if len(pal)!=256:raise ValueError("CLUT PSMT8 invalido")
            pal=pal.reshape(8,4,8,4)[:,[0,2,1,3]].reshape(256,4)
            idx=unswizzle8(raw,w,h)
        elif psm==20:
            if len(pal)!=16:raise ValueError("CLUT PSMT4 invalido")
            idx=unswizzle4(raw,w,h)
        else:raise ValueError("PSM no soportado: %d"%psm)
        pal[:,3]=np.minimum(pal[:,3].astype(np.uint16)*2,255).astype(np.uint8)
        rgba=pal[idx]
        out.append(dict(w=w,h=h,psm=psm,rgba=rgba))
    return out


def read_effect(b):
    entries=children(b)
    if len(entries)<4 or len(entries[3])<64:raise ValueError("falta la definicion EftSet")
    head=entries[3];mask,ng,np_=struct.unpack_from('<IBB',head)
    if ng>19 or np_>40 or 64+ng*32+np_*64>len(head):raise ValueError("EftSet fuera de limites")
    groups=[];part_at=64+ng*32;next_resource=4;flat_textures=[];part_count=0
    for k in range(ng):
        kind,nparts,nres=struct.unpack_from('<3B',head,64+k*32)
        if kind not in (0,5,9,14,16):raise ValueError("familia T3 no soportada: %d"%kind)
        group=dict(kind=kind,resources=[],parts=[])
        for r in range(nres):
            if next_resource>=len(entries):raise ValueError("recurso T3 fuera del PAK")
            ids=[]
            for index,t in enumerate(textures(entries[next_resource])):
                ids.append(len(flat_textures));flat_textures.append(dict(t,entry=next_resource,index=index))
            group['resources'].append(ids);next_resource+=1
        for j in range(nparts):
            raw=head[part_at:part_at+64];part_at+=64;part_count+=1
            part=dict(raw=raw,phase=raw[8],end_phase=raw[9],resource=raw[1],blocks=[])
            if kind:
                nb=1 if kind==14 else 2
                if next_resource+nb>len(entries):raise ValueError("parametros de parte fuera del PAK")
                part['blocks']=entries[next_resource:next_resource+nb];next_resource+=nb
                if part['resource']>=nres:raise ValueError("indice de textura de parte invalido")
            group['parts'].append(part)
        groups.append(group)
    if part_count!=np_ or next_resource!=len(entries):raise ValueError("no se consumio el paquete completo")
    return dict(mask=mask,groups=groups,textures=flat_textures,parts=part_count)


def encode(effect):
    out=bytearray(b'TCB2'+struct.pack('<4I',len(effect['groups']),effect['parts'],len(effect['textures']),effect['mask']))
    def blob(b):
        out.extend(struct.pack('<I',len(b)));out.extend(b)
    for g in effect['groups']:
        out.extend(struct.pack('<3I',g['kind'],len(g['resources']),len(g['parts'])))
        for r in g['resources']:
            out.extend(struct.pack('<I',len(r)))
            out.extend(struct.pack('<%dI'%len(r),*r))
        for p in g['parts']:
            blob(p['raw']);out.extend(struct.pack('<I',len(p['blocks'])))
            for block in p['blocks']:blob(block)
    for t in effect['textures']:
        out.extend(struct.pack('<4I',t['w'],t['h'],t['entry'],t['index']));blob(t['rgba'].tobytes())
    return bytes(out)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('bank',type=Path);ap.add_argument('--slot',type=int,default=3)
    ap.add_argument('--out',type=Path,required=True);ap.add_argument('--images',type=Path)
    ap.add_argument('--common-rays',type=Path,help='entrada comun 10 original para la familia EftRay (sin sustituto)')
    a=ap.parse_args()
    if a.out.exists():ap.error('salida existente; usa otra ruta')
    banks=children(a.bank.read_bytes())
    if not 0<=a.slot<len(banks):ap.error('ranura inexistente')
    effect=read_effect(banks[a.slot]);a.out.parent.mkdir(parents=True,exist_ok=True)
    if a.common_rays:
        for index,t in enumerate(textures(a.common_rays.read_bytes())):
            effect['textures'].append(dict(t,entry=0xffffffff,index=index))
    payload=encode(effect);a.out.write_bytes(payload)
    if a.images:
        from PIL import Image
        a.images.mkdir(parents=True,exist_ok=True)
        for i,t in enumerate(effect['textures']):Image.fromarray(t['rgba']).save(a.images/f'{i:02d}_e{t["entry"]}_{t["index"]}.png')
    print('T3 original: %d familias, %d partes, %d texturas; TCB2 %d B SHA256 %s'%
          (len(effect['groups']),effect['parts'],len(effect['textures']),len(payload),hashlib.sha256(payload).hexdigest()))

if __name__=='__main__':main()

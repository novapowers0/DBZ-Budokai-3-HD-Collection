#!/usr/bin/env python3
"""Ensayo local R16: cabezas propias Z-Image/TRELLIS.2 sobre cuerpo B3.

3180..3183 son recetas, nunca índices AFS. Solo se anuncian las mallas
preparadas presentes. Los modelos/texturas B3 se obtienen del disco del usuario.
No se cambian el guardado, las otras razas ni los valores predeterminados.
"""
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent / 'ia_namek'
TIPOS = ((3180, '11_sauce', 'Tipo N1 - Sauce'),
         (3181, '12_bastion', 'Tipo N2 - Bastion'),
         (3182, '13_doble', 'Tipo N3 - Doble'),
         (3183, '14_estela', 'Tipo N4 - Estela'))


def catalogo_b3():
    return [dict(tipo='original_torso', id=i, nombre=nombre, idx_afs=-1,
                 archivo='original:ia_namek:'+clave, cuerpos=[11])
            for i, clave, nombre in TIPOS if (ROOT/clave/'malla.json').is_file()]


def seleccion(ficha):
    if int(ficha.get('cuerpo', -1)) != 11:
        return []
    ident = int(ficha.get('b3_torso', -1))
    return [(i, 0, 'ia_namek:'+clave) for i, clave, _ in TIPOS if ident == i]


def _atlas(path):
    """Quita la iluminación pintada; conserva tinta y blanco de ojos propios."""
    from PIL import Image
    from toon_regiones import validar_colores
    palette = validar_colores({'piel': [174, 219, 100], 'placas': [220, 147, 154]})
    rgb = np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)
    high,low=rgb.max(-1),rgb.min(-1)
    green=(rgb[...,1] > rgb[...,0]*1.015) & (rgb[...,1] > rgb[...,2]*1.08)
    pink=(rgb[...,0] > rgb[...,1]*1.18) & (rgb[...,2] > rgb[...,1]*.85)
    white=(high-low < 50) & (high > 145) & ~green
    ink=(high < 48) | ((high-low < 35) & (high < 115))
    rgba=np.empty(rgb.shape[:2]+(4,),dtype=np.uint8)
    rgba[...,:3]=palette['piel']
    rgba[...,3]=0                 # pide la rampa toon del anfitrión
    rgba[pink,:3]=palette['placas']
    rgba[white,:3]=[244,244,232]
    rgba[white,3]=255
    rgba[ink,:3]=[24,30,20]
    rgba[ink,3]=255
    return rgba


def aplicar_b3(amb, ficha):
    sel=seleccion(ficha)
    if not sel:
        return bytes(amb), {}
    from PIL import Image
    import model_render as mr
    from awg_vertex_buffer import AwgVertexBuffer
    from b3_gateway import DrawTable
    from injertar_pelo import triangulos_por_draw
    from injertar_ropa import injertar_ropa_multi

    ident=sel[0][0]
    _,clave,nombre=next(x for x in TIPOS if x[0] == ident)
    directory=ROOT/clave
    record=json.loads((directory/'malla.json').read_text(encoding='utf-8'))
    m=mr.Model(bytes(amb))
    head,neck=m.bone('HEAD'),m.bone('NECK')
    if not m.labels[head].lstrip('X').startswith('PIC_'):
        raise ValueError('Tipo N requiere el esqueleto nativo Piccolo')
    if int(ficha.get('traje',0)) != 0:
        raise ValueError('Ensayo Tipo N: solo traje base 0, sin turbante')
    worlds=np.asarray(m.worlds(),float)
    tris=np.asarray(m.tris,int)
    td=triangulos_por_draw(m)
    dib=np.asarray([di for di,_ in td])
    # Las piezas de cráneo, mandíbula, ojos y antenas quedan sustituidas.
    facial=[i for i,label in enumerate(m.labels)
            if i == head or any(k in label for k in ('_M_', 'HAIR', '_FACE'))]
    quitar=np.any(np.isin(m.vbone[tris],facial),axis=1)
    candidates=[(int(np.sum((dib==i)&quitar)),i) for i,d in enumerate(m.table.draws)
                if d['prim']==5 and m.mats[d['material']]['shader'] != 0xffffffff]
    if not candidates or max(candidates)[0] == 0:
        raise ValueError('Sin anfitrión toon para la cabeza')
    host=max(candidates)[1]
    material=m.mats[m.table.draws[host]['material']]
    tex=int(material['tex'])
    # La textura del cuello comparte material. Se conserva en 1/4 del atlas;
    # todas sus UV se trasladan. No se repintan piel del cuerpo, manos ni ropa.
    survivors=np.flatnonzero(~quitar)
    if any(int(m.mats[m.tri_mat[t]]['tex'])==tex and dib[t]!=host for t in survivors):
        raise ValueError('Atlas compartido por otra parte superviviente')
    h,w=m.tex[tex].shape[:2]
    native=Image.fromarray(np.rint(m.tex[tex]*255).astype(np.uint8),'RGBA')
    own=Image.fromarray(_atlas(directory/record['texture']),'RGBA')
    atlas=Image.new('RGBA',(w,h))
    atlas.paste(native.resize((w//4,h),Image.Resampling.LANCZOS),(0,0))
    atlas.paste(own.resize((w-w//4,h),Image.Resampling.NEAREST),(w//4,0))
    uv_remap={int(v):(float(m.uv[v,0])*.25,float(m.uv[v,1]))
              for t in survivors if dib[t]==host for v in tris[t]}

    P=np.asarray(record['P'],float)
    N=np.asarray(record['N'],float)
    UV=np.asarray(record['UV'],float)
    # Ajuste uniforme al anclaje HEAD; preserva la silueta del diseño IA.
    lo,hi=P.min(0),P.max(0)
    scale=3.35/max(hi[1]-lo[1],1e-6)
    center=np.array([(lo[0]+hi[0])*.5,lo[1],(lo[2]+hi[2])*.5])
    P=(P-center)*scale
    P += [worlds[head,0,3],worlds[neck,1,3]-.12,worlds[head,2,3]+.25]
    N/=np.maximum(np.linalg.norm(N,axis=1),1e-9)[:,None]
    bones=np.full(len(P),head,dtype=int)
    # B3 es rígido por ventana: la franja inferior acompaña al cuello.
    bones[P[:,1] < worlds[neck,1,3]+.2]=neck
    UV[:,0]=.25+UV[:,0]*.75
    piece=dict(host=host,quitar=quitar,P=P,N=N,UV=UV,bones=bones,
               tris=np.asarray(record['tris'],int),rgba=np.asarray(atlas),
               tex_destino=tex,zona=nombre,uv_remap=uv_remap)
    actual,resumen=injertar_ropa_multi(bytes(amb),m,[piece])
    # Las seis caras alternativas ya no deben resucitar la de Piccolo.
    # El cráneo propio vive completo en BODY y se mantiene en cada pose.
    caras=[]
    for awg in range(1,m.avb.n_awg):
        mm=mr.Model(actual,awg=awg)
        label=bytes(mm.data[mm.avb.awg0+0x40:mm.avb.awg0+0x60]).split(b'\0')[0].decode('latin1')
        if 'FACE' not in label:
            continue
        avb=AwgVertexBuffer._parse(actual,awg=awg)
        dt=DrawTable(avb)
        for draw in dt.draws:
            dt.set_range(draw,draw['A'][0],0,draw['B'][0],0)
        actual=bytes(avb.data)
        caras.append(awg)
    resumen.update(piezas=[nombre],ids=[ident],expresiones='rigida, sin lip-sync propio',
                   caras_nativas_ocultas=caras,licencia='TRELLIS.2 MIT; Z-Image Turbo Apache-2.0',
                   costura='solape provisional HEAD/NECK; pendiente de validar en imágenes',
                   materiales='rampa y borde nativos; atlas plano propio, cuello remapeado')
    return actual,resumen

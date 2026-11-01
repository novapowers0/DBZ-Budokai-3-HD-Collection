"""Oraculos de solSAT2; solo lee copias del jugador, evidencia en D:/tmp/solSAT."""
import io
import json
import os
from pathlib import Path
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TMP = Path(r'D:\tmp\solSAT')
sys.path[:0] = [str(ROOT / 'mod center hd'), str(ROOT / 'mod center hd/studio'), str(ROOT / 'awo_tools')]


def estados():
    from PIL import Image
    rows = []
    for p in sorted(Path(r'D:\_PARA_BORRAR_CLAUDE\solSAT\captura_estados').glob('*.p2s')):
        with zipfile.ZipFile(p) as z:
            ram = z.read('eeMemory.bin')
            campo, escena = [struct.unpack_from('<I', ram, a)[0] for a in (0x361550, 0x583a10)]
            row = dict(estado=str(p), campo=campo, escena=escena,
                       fade=struct.unpack_from('<f', ram, 0x361e64)[0])
            if escena < 100:
                im = Image.open(io.BytesIO(z.read('Screenshot.png'))).convert('RGB')
                row['imagen'] = str(TMP / f'original_toma1_{escena:03d}.png')
                if not Path(row['imagen']).exists():
                    im.save(row['imagen'])
            rows.append(row)
    (TMP / 'sol2_estados.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    print(json.dumps(rows, indent=2))


def clip():
    import numpy as np
    import importar
    import afs_pair
    import fk_b1 as K
    import model_render
    import final_b1 as F
    # La descompresion existente permite dirigir su cache fuera de C.
    afs_pair.CACHE = str(TMP / 'lzx')
    afs = importar.AfsFile(r'D:\DBZ3HD\ps2_games\DragonBall Z - Budokai (Europe) (En,Fr,De,Es,It).iso', 'DATA_EN.AFS')
    amo = afs.entry(2363)
    pk = afs.entry(2372)
    off, size = struct.unpack_from('<2I', pk, struct.unpack_from('<I', pk, 20)[0] + 3 * 16)
    amm = pk[off:off+size]
    names, local, parent = K.amo_info(amo)
    # Lee el modelo descomprimido sin crear un mod.
    import studio_core as sc
    m = model_render.Model(sc.hd_entry(377, str(ROOT / 'us')))
    anm=sc.hd_entry(382,str(ROOT/'us'));kids=sc.amb_kids(anm,'>')
    csk=anm[kids[0][0]:kids[0][0]+kids[0][1]]
    acm=anm[kids[1][0]:kids[1][0]+kids[1][1]]
    nb3,nm3=struct.unpack_from('>2I',acm,24)
    print('accesorios HD',[(i,n) for i,n in enumerate(m.labels) if any(s in n for s in ('FIRE','JPACK'))])
    print('ACM accesorios',[acm[nm3+32*i:nm3+32*i+32].split(b'\0')[0].decode('latin1') for i in range(nb3) if any(s in acm[nm3+32*i:nm3+32*i+32] for s in (b'FIRE',b'JPACK'))])
    own=[F.SUF(acm[nm3+32*i:nm3+32*i+32].split(b'\0')[0].decode('latin1')) for i in range(nb3)]
    print('ACM nombres',own)
    ctab=struct.unpack_from('>I',csk,20)[0]
    print('codigos',[(hex(c),hex(struct.unpack_from('>I',csk,ctab+4*c)[0])) for c in (0x2f7,0x2f8,0x2f9)])
    nm1 = {F.SUF(n): i for i, n in enumerate(names)}
    nm3 = {F.SUF(n): i for i, n in enumerate(m.labels)}
    tr = K.clip_tracks(amm, 0)
    print('cabecera clip',struct.unpack_from('<4I',amm,32))
    print('huesos accesorios',[(n,tr[1].get(n),tr[2].get(n)) for n in names if any(s in n for s in ('FIRE','JPACK','JET'))])
    print('AMM accesorios',[(n,tr[1].get(n),tr[2].get(n)) for n in K.amm_info(amm)[3] if 'JET' in n])
    print('WAIST original',tr[2].get('STN_WAIST'))
    print('giro WAIST original',tr[1].get('STN_WAIST'))
    for c in (0,0x281,0x2f7):
        b=struct.unpack_from('>I',csk,ctab+4*c)[0]
        print('CSK',hex(c),hex(b),csk[b:b+48].hex())
        n,ptr=struct.unpack_from('>2I',csk,b+40)
        for k in range(n):
            ty,nk,pp=struct.unpack_from('>HHI',csk,ptr+8*k)
            print('AP',ty,nk,hex(pp),csk[pp:pp+min(64,16*nk)].hex())
    out = dict(fuente='2372/3', clip=0, cuadros=tr[0], segmentos=[], giros={})
    for modo in ('directo', 'delta'):
        L = [x.copy() for x in m.local]
        for s, j in nm3.items():
            if s not in nm1 or names[nm1[s]] not in tr[1]: continue
            R = K.rot(*K.interp_ang(tr[1][names[nm1[s]]], 0))
            if modo == 'delta' and s != 'WAIST': R = R @ local[nm1[s]][:3,:3].T @ m.local[j][:3,:3]
            L[j][:3,:3] = R
            if modo == 'delta': out['giros'][s] = [int(round(v / (2*np.pi)*65536)) & 65535 for v in F._euler(R)]
        W = []
        for i, mat in enumerate(L): W.append(W[m.parent[i]] @ mat if 0 <= m.parent[i] < i else mat)
        worlds = np.array(W)
        vertices = np.einsum('nij,nj->ni', worlds[m.vbone,:3,:3], m.pos) + worlds[m.vbone,:3,3]
        seg=[]
        for a,b in (('LARM1','LARM2'),('RARM1','RARM2'),('LLEG1','LLEG2'),('RLEG1','RLEG2')):
            d=W[nm3[b]][:3,3]-W[nm3[a]][:3,3]
            seg.append(float(np.degrees(np.arccos(np.clip(-d[1]/np.linalg.norm(d),-1,1)))))
        out['segmentos'].append(dict(modo=modo, grados=seg, suelo=float(vertices[:,1].min()),
                                    cintura=L[nm3['WAIST']][:3,3].tolist(),
                                    cintura_b1=local[nm1['WAIST']][:3,3].tolist()))
    (TMP / 'sol2_clip.json').write_text(json.dumps(out, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in out.items() if k!='giros'}, indent=2))
    afs.f.close()


def tablero():
    from PIL import Image
    p = Path(r'E:\DBZ3HD_trabajo\R7\emu\pcsx2_luna\sstates\SLES-51233 (37472BE0).04.p2s')
    with zipfile.ZipFile(p) as z:
        ram = z.read('eeMemory.bin')
        print('captura', Image.open(io.BytesIO(z.read('Screenshot.png'))).size)
        print('GS', [(n,z.getinfo(n).file_size) for n in z.namelist() if 'GS' in n or 'gs' in n])
        print('archivos', z.namelist())
        gs=z.read('GS.bin')
        print('GS cabecera', gs[:128].hex())
        print('GS contexto', [(hex(o),hex(struct.unpack_from('<Q',gs,o)[0])) for o in range(108,244,8)])
        regs=z.read('GSregs.bin') if 'GSregs.bin' in z.namelist() else b''
        internal=z.read('PCSX2 Internal Structures.dat')
        displays=[]
        for o in range(0,len(internal)-8):
            v=struct.unpack_from('<Q',internal,o)[0]
            dw=(v>>32)&4095;dh=(v>>44)&2047;mh=(v>>23)&15;mv=(v>>27)&3
            if dw in (1279,2559) and dh in (255,511,447,223):
                displays.append(dict(o=hex(o),v=hex(v),dw=dw,dh=dh,mh=mh,mv=mv,
                                     alrededor=internal[max(0,o-32):o+16].hex()))
        print('DISPLAY candidatos',displays)
        for adr in (0x70,0x80,0x90,0xa0,0xb0):
            if len(regs)>=adr+8: print('GS reg',hex(adr),hex(struct.unpack_from('<Q',regs,adr)[0]))
    for x in (272,320,368):
        if x==272: print('dibujante sprite',hex(struct.unpack_from('<I',ram,0x335f14)[0]))
        patron = struct.pack('<2f',x,408)
        off=0; found=[]
        while (off:=ram.find(patron,off))>=0:
            found.append(hex(off)); off+=1
        print(x, found[:15])
        if x==320:
            print('sprite original vivo',struct.unpack_from('<28f',ram,0x70a620))
            for s in found[:4]:
                o=int(s,16)
                print(s,'entorno',struct.unpack_from('<24f',ram,o-32))


def raster():
    import numpy as np
    import cv2
    from PIL import Image
    import importar
    import amt_ps2
    afs=importar.AfsFile(r'D:\DBZ3HD\ps2_games\DragonBall Z - Budokai (Europe) (En,Fr,De,Es,It).iso','DATA_EN.AFS')
    amt=afs.entry(668);ama=afs.entry(667)
    rgba=amt_ps2.decode(amt,amt_ps2.entries(amt)[24])
    print('atlas',rgba.shape)
    Image.fromarray(rgba).save(TMP/'sol2_atlas.png')
    tab=struct.unpack_from('<I',ama,12)[0]
    for fase in (0.0,0.5):
        canvas=np.full((256,640,3),[65,0,0],np.float32)
        for k in range(5,11):
            r=tab+64*(70+k)
            clave,parte=struct.unpack_from('<I',ama,r+52)[0],struct.unpack_from('<I',ama,r+60)[0]
            q=struct.unpack_from('<8f',ama,r)
            x,y=struct.unpack_from('<2f',ama,clave+16)
            uv=np.array(struct.unpack_from('<4f',ama,parte+32)).reshape(2,2)
            uv[0]+=.5;uv[1]-=.5
            p0=np.array([x+.7*q[0],(y+.7*q[1])*4/7])
            p1=np.array([x+.7*q[4],(y+.7*q[5])*4/7])
            p0=np.floor(p0*16+.5)/16;p1=np.floor(p1*16+.5)/16
            xx,yy=np.meshgrid(np.arange(640,dtype=np.float32)+fase,np.arange(256,dtype=np.float32)+fase)
            u=uv[0,0]+(xx-p0[0])/(p1[0]-p0[0])*(uv[1,0]-uv[0,0])-.5
            v=uv[0,1]+(yy-p0[1])/(p1[1]-p0[1])*(uv[1,1]-uv[0,1])-.5
            tex=cv2.remap(rgba,u.astype(np.float32),v.astype(np.float32),cv2.INTER_LINEAR).astype(float)
            alpha=tex[:,:,3:4]/255
            alpha*=((xx>=p0[0])&(xx<p1[0])&(yy>=p0[1])&(yy<p1[1]))[:,:,None]
            canvas=tex[:,:,:3]*alpha+canvas*(1-alpha)
        im=Image.fromarray(np.clip(canvas,0,255).astype(np.uint8)).resize((640,480),Image.Resampling.BILINEAR)
        im.save(TMP/f'sol2_raster_{fase}.png')
        import scipy.ndimage as ndi
        a=np.array(im)
        labels,n=ndi.label(ndi.binary_opening(a[380:475].sum(2).astype(int)<45,iterations=1))
        boxes=[]
        for k in range(1,n+1):
            yy,xx=np.where(labels==k)
            if 120<len(xx)<4000 and xx.min()>280:boxes.append([int(xx.min()),int(yy.min()+380),int(xx.max()+1),int(yy.max()+381)])
        print('fase GS',fase,'cajas',sorted(boxes))
    afs.f.close()


def framebuffer():
    """Exploracion NO validada: GS.bin no incluye los render targets GPU resueltos."""
    import numpy as np
    from PIL import Image
    p=Path(r'E:\DBZ3HD_trabajo\R7\emu\pcsx2_luna\sstates\SLES-51233 (37472BE0).04.p2s')
    with zipfile.ZipFile(p) as z:
        gs=z.read('GS.bin')
        target=np.array(Image.open(io.BytesIO(z.read('Screenshot.png'))).convert('RGB').resize((640,256)))
    blocks=np.array([[0,1,4,5,16,17,20,21],[2,3,6,7,18,19,22,23],
                     [8,9,12,13,24,25,28,29],[10,11,14,15,26,27,30,31]])
    yy,xx=np.indices((256,640))
    adr=((yy//32)*10+xx//64)*8192+blocks[(yy%32)//8,(xx%64)//8]*256+(yy%8//2)*64+((yy%2)*2+(xx%8//2)*4+xx%2)*4
    raw=np.frombuffer(gs,np.uint8)
    best=(1e9,None)
    sy,sx=np.meshgrid(np.arange(15,240,17),np.arange(15,625,19),indexing='ij')
    for fbp in (0,655360):
        for start in range(0,510):
            rgb=raw[(adr[sy,sx]+fbp+start)[:,:,None]+np.arange(3)].astype(float)
            err=float(np.abs(rgb-target[sy,sx]).mean())
            if err<best[0]:best=(err,(fbp,start))
    fbp,start=best[1]
    if best[0]>10:
        print('NO VALIDO: la VRAM guardada no reproduce el render target de Screenshot.png; no usar como oraculo.',best)
        return
    rgb=raw[(adr+fbp+start)[:,:,None]+np.arange(3)]
    Image.fromarray(rgb).save(TMP/'sol2_framebuffer.png')
    print('VRAM',best)
    from scipy import ndimage as ndi
    for dy in (0,1,2):
        im=Image.fromarray(rgb).resize((640,480),Image.Resampling.BILINEAR)
        a=np.array(im)
        mask=ndi.binary_opening(a[380:475].sum(2).astype(int)<45,iterations=1)
        labels,n=ndi.label(mask);boxes=[]
        for k in range(1,n+1):
            ys,xs=np.where(labels==k)
            if 120<len(xs)<4000 and xs.min()>280: boxes.append([int(xs.min()),int(ys.min()+380),int(xs.max()+1),int(ys.max()+381)])
        print('framebuffer sin PCSX2',sorted(boxes));break


def comparar():
    from PIL import Image,ImageDraw
    run=Path(os.environ['SOLSAT_RUN'])
    pairs=[('original_toma1_001.png','toma1_campo1.png'),('original_toma1_010.png','toma1_campo10.png'),
           ('original_toma1_060.png','toma1_campo60.png'),('original_coche_1758.png','coche_campo1758.png'),
           ('original_coche_1841.png','humo_campo1841.png'),('original_coche_1931.png','pose_campo1931.png')]
    sheet=Image.new('RGB',(640,264*len(pairs)))
    for i,(a,b) in enumerate(pairs):
        orig=Image.open(TMP/a).convert('RGB').resize((320,240))
        port=Image.open(run/b).convert('RGB')
        print(b,port.size)
        w,h=port.size;cw=min(w,round(h*4/3))
        port=port.crop(((w-cw)//2,0,(w+cw)//2,h)).resize((320,240))
        sheet.paste(orig,(0,i*264+24));sheet.paste(port,(320,i*264+24))
        ImageDraw.Draw(sheet).text((4,i*264+4),a+' | '+b,fill='white')
        port.save(TMP/('sol2_'+b))
    sheet.save(TMP/'sol2_comparacion.png')


def visuales():
    import pruebas as P
    import numpy as np
    from PIL import Image
    capturar=P.Juego.captura
    def captura(j,nombre):
        if nombre=='pose_campo1931':
            def rd(a,n):
                b=P.leer(j.h,P.GBASE+a,n)
                if b is None or len(b)!=n:raise P.Fallo(f'lectura incompleta {a:X}')
                return b
            u=lambda a:struct.unpack('>I',rd(a,4))[0]
            mgr=u(0x823d6cf4);x=mgr+2096
            out=dict(actor=hex(x),codigo=hex(u(x+2928)),mundo=struct.unpack('>16f',rd(x,64)),cuerpos=[])
            for offset in (0x650,0xcd0):
                A=x+offset;m=u(A+0x538)
                if not m:continue
                tb=u(m+1032)
                mats=np.array(struct.unpack('>896f',rd(tb,56*64))).reshape(56,4,4)
                ang=[]
                for a,b in ((32,34),(11,13),(2,4),(50,52)):
                    d=mats[b,3,:3]-mats[a,3,:3]
                    ang.append(float(np.degrees(np.arccos(np.clip(-d[1]/np.linalg.norm(d),-1,1)))))
                out['cuerpos'].append(dict(offset=hex(offset),clip=hex(u(A+0x604)),grados=ang,
                                            cintura=mats[1,3,:3].tolist(),huesos=mats.tolist()))
            (TMP/'sol2_pose_runtime.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
            print('pose runtime',json.dumps({**out,'cuerpos':[{k:v for k,v in b.items() if k!='huesos'} for b in out['cuerpos']]},indent=2))
            if out['codigo']!='0x2f9' or not out['cuerpos'] or not np.isfinite(out['mundo']).all():
                raise P.Fallo('el clip original no esta aplicado o su mundo no es finito')
            for b in out['cuerpos']:
                if not np.isfinite(b['grados']).all() or np.max(np.abs(np.array(b['grados'])-np.array([16.34457,16.49840,4.93337,4.95402])))>.5:
                    raise P.Fallo('angulos de la pose fuera del original (tolerancia 0,5 grados para el esqueleto HD)')
        p=capturar(j,nombre)
        with Image.open(p) as im:im.resize((im.width//2,im.height//2)).save(p)
        return p
    P.Juego.captura=captura
    r=P.correr(os.environ.get('SOLSAT_COPIA','sol'),os.environ.get('SOLSAT_ESCENARIO','tools/satan_sol2_visuales.toml'),forzar=True)
    (TMP/'sol2_visuales_resultado.json').write_text(json.dumps(r,default=str,indent=2),encoding='utf-8')
    if not r['ok']:raise SystemExit(1)


def alfa():
    import numpy as np
    from PIL import Image
    run=Path(os.environ['SOLSAT_RUN'])
    imgs=[]
    for name in ('alfa_negro.png','alfa_blanco.png'):
        im=Image.open(run/name).convert('RGB');w,h=im.size;cw=round(h*4/3)
        im=im.crop(((w-cw)//2,0,(w+cw)//2,h)).resize((320,240))
        imgs.append(np.array(im).astype(float))
    a=np.clip(1-(imgs[1]-imgs[0])/255,0,1).mean(2)
    referencia=next(r['alfa'] for r in json.loads((TMP/'sol2_perspectiva.json').read_text()) if r['perspectiva'])
    out=dict(metodo='1 - (GPU_blanco - GPU_negro)/255; mismo cuadro 2209.2, misma geometria',
             rect=[5,130,40,145],alfa_gpu=float(a[130:145,5:40].mean()),
             alfa_oraculo_ps2=referencia,unidad_original='CLUT A/128, ST/Q, UV*256-0.5, centros de pixel',
             oraculo_antiguo_no_usar=0.42949)
    out['error_absoluto']=abs(out['alfa_gpu']-out['alfa_oraculo_ps2'])
    Image.fromarray((a*255).astype(np.uint8)).save(TMP/'sol2_alfa_gpu.png')
    (TMP/'sol2_alfa_gpu.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
    print(json.dumps(out,indent=2))


def perspectiva():
    """Contrasta UV afines de ImGui con ST/Q del GS usando los mismos quads."""
    import numpy as np
    import cv2
    import fk_b1 as K
    import amt_ps2
    amo,amm,amt=[(TMP/f'2372_{i}.bin').read_bytes() for i in (13,15,14)]
    u=lambda b,o:struct.unpack_from('<I',b,o)[0]
    names,L,parent=K.amo_info(amo);nf,ang,pos=K.clip_tracks(amm,0)
    clip=u(amm,u(amm,20)+12);f=179.2
    W=[]
    for i,n in enumerate(names):
        m=L[i].copy()
        if n in ang and any(any(k[1:]) for k in ang[n]):m[:3,:3]=K.rot(*K.interp_ang(ang[n],f))
        if n in pos and any(any(k[1:]) for k in pos[n]):m[:3,3]+=K.interp_pos(pos[n],f)
        s=u(amm,clip+4*(3*i+2))
        if s:
            keys=[struct.unpack_from('<I3f',amm,s+12+16*j) for j in range(u(amm,s+8))]
            m[:3,:3]=m[:3,:3]@np.diag(K.interp_pos(keys,f))
        W.append(W[parent[i]]@m if parent[i]>=0 else m)
    ref=next(r for r in json.loads((TMP/'oraculo_humo.json').read_text()) if r['campo']==1841 and r['escalas'])
    eye,target=np.array(ref['ojo']),np.array(ref['obj'])
    front=target-eye;front/=np.linalg.norm(front)
    right=np.cross(front,[0,1,0]);right/=np.linalg.norm(right);up=np.cross(right,front)
    rgba=amt_ps2.decode(amt,amt_ps2.entries(amt)[0]).astype(float)/255
    rgba[:,:,3]*=255/256  # GS: CLUT A/128; decode devuelve 2*A/255
    awg=u(amo,u(amo,28));axes=u(amo,awg+20)
    quads=[]
    for i in range(len(names)):
        arm=u(amo,awg+axes+80*i+52);grp=u(amo,awg+arm+4) if arm else 0
        if not grp:continue
        gg=awg+grp
        for j in range(u(amo,gg)):
            po=gg+u(amo,gg+u(amo,gg+4)+4*j);vp=po+192
            v=np.array([struct.unpack_from('<4f',amo,vp+32*c) for c in (0,1,2,5)])
            uv=np.array([struct.unpack_from('<2f',amo,vp+32*c+16) for c in (0,1,2,5)])
            xyz=(W[i]@v.T).T[:,:3]-eye;depth=xyz@front
            if np.any(depth<=1):continue
            xy=np.array([160+(xyz@right)*822.95/depth,120-(xyz@up)*877.8/depth]).T
            quads.append((xy,depth,uv))
    yy,xx=np.indices((240,320),dtype=float);xx+=.5;yy+=.5
    results=[]
    for perspective in (False,True):
        alpha=np.zeros((240,320))
        for xy,depth,uv in quads:
            layer=np.zeros_like(alpha);done=np.zeros_like(alpha,dtype=bool)
            for ids in ((0,1,2),(0,2,3)):
                p=xy[list(ids)];texuv=uv[list(ids)];zs=depth[list(ids)]
                den=(p[1,1]-p[2,1])*(p[0,0]-p[2,0])+(p[2,0]-p[1,0])*(p[0,1]-p[2,1])
                w0=((p[1,1]-p[2,1])*(xx-p[2,0])+(p[2,0]-p[1,0])*(yy-p[2,1]))/den
                w1=((p[2,1]-p[0,1])*(xx-p[2,0])+(p[0,0]-p[2,0])*(yy-p[2,1]))/den
                weights=np.stack((w0,w1,1-w0-w1),axis=-1)
                mask=(weights.min(2)>=0)&~done
                if perspective:weights=weights/zs;weights/=weights.sum(2,keepdims=True)
                t=weights@texuv
                sample=cv2.remap(rgba[:,:,3],(t[:,:,0]*256-.5).astype('float32'),(t[:,:,1]*256-.5).astype('float32'),cv2.INTER_LINEAR,borderMode=cv2.BORDER_REPLICATE)
                layer[mask]=sample[mask];done|=mask
            alpha=layer+alpha*(1-layer)
        results.append(dict(perspectiva=perspective,alfa=float(alpha[130:145,5:40].mean())))
    (TMP/'sol2_perspectiva.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))


if __name__ == '__main__':
    os.environ.setdefault('DBZ3_AGENTE', 'solSAT2')
    globals()[sys.argv[1]]()

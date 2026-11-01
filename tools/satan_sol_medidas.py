"""Medidas de solSAT; datos privados en D:/tmp/solSAT, sin distribuir assets.

inventario: estados B1 existentes, contadores y miniaturas (sin ejecutarlos).
original: instancia kit propia, capturas PINE sin avanzar y restauracion del INI.
datos / oraculo: textura, material y transformadas originales; alfa rasterizado CPU.
humo: estimacion cromatica exploratoria, NO medida absoluta (el fondo se mueve).
pose / pose_original: clip y angulos efectivos de B3 frente al AMM estatico de B1.
tablero: cajas de siluetas sobre lienzos UI 4:3, sin estirar el port 16:9.
Las pruebas de juego usan las copias y la cola de pruebas.py. PYTHONUTF8=1.
"""
import argparse
import io
import json
import os
from pathlib import Path
import struct
import sys
import shutil
import time
import zipfile

TMP = Path(r"D:\tmp\solSAT")
EMU = Path(r"E:\DBZ3HD_trabajo\R7\emu")
ARCHIVO = Path(r"D:\_PARA_BORRAR_CLAUDE\solSAT")


def original():
    import pcsx2_cuadros as pc
    for n in ("SDL_JOYSTICK_WGI", "SDL_JOYSTICK_RAWINPUT", "SDL_JOYSTICK_HIDAPI",
              "SDL_JOYSTICK_DIRECTINPUT", "SDL_XINPUT_ENABLED"):
        os.environ[n] = "0"
    os.environ["DBZ3_PCSX2_CUADROS_TMP"] = str(TMP)
    os.environ["DBZ3_PCSX2_CUADROS_RESPALDO"] = str(ARCHIVO)
    s = pc.conectar("kit")
    if s.nuestro_pid():
        raise RuntimeError("La instancia kit esta ocupada; no se toca")
    ARCHIVO.mkdir(parents=True, exist_ok=True)
    ini = Path(s.EMU) / "inis/PCSX2.ini"
    estado = Path(s.EMU) / "sstates/SLES-51233 (37472BE0).98.p2s"
    backup = TMP / "PCSX2_kit.ini.original"
    if backup.exists():
        shutil.move(str(ini), str(ARCHIVO / (str(time.time_ns()) + "_kit_interrumpido.ini")))
        shutil.move(str(backup), ini)
    if estado.exists():
        shutil.move(str(estado), str(ARCHIVO / (str(time.time_ns()) + "_base98_interrumpida.p2s")))
    for foto in TMP.glob("original_toma1_*.png"):
        shutil.move(str(foto), str(ARCHIVO / (str(time.time_ns()) + "_" + foto.name)))
    pc.preparar(s)
    if estado.exists():
        raise FileExistsError(estado)
    shutil.copy2(EMU / "pcsx2_luna/sstates/SLES-51233 (37472BE0).08.p2s", estado)
    propio = False
    rows = []
    try:
        p = s.lanzar("b1", str(estado)); propio = True
        p.s.close()  # PINE atiende una conexion; no dejar la del arranque abierta.
        c = pc.Cuadros(s, 0x361550)
        p = c.p
        c.pausa()
        p.cargar(98); time.sleep(1)
        c.pausa()
        # La primera toma empieza en el campo 17954 de este estado conocido.
        # Avanzar en tiempo real hasta cerca; los ultimos campos se miden uno a uno.
        c.pulso(0x75); time.sleep(4.5); c.pausa()
        for i in range(400):
            # B1: primero medir si este contador cambia por campo o por tick.
            c.contador = None
            c.paso()
            c.contador = 0x361550
            v = p.lote([(a, 4) for a in (0x361550, 0x583a10, 0x361e64, 0x361e68, 0x361e5c, 0x5ecc78)])
            fade, paso = [struct.unpack("<f", x.to_bytes(4, "little"))[0] for x in v[2:4]]
            row = dict(campo=v[0], escena=v[1], fade=fade, paso=paso,
                       callback=hex(p.u32(v[4]+0x1c)) if v[4] else None,
                       camara=struct.unpack("<12f", p.leer(0x555b00, 48)))
            rows.append(row)
            (TMP / "original_toma1.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
            if v[1] in (1,2,5,10,20,30,60):
                row["captura"] = c.captura(TMP / f"original_toma1_{v[1]:03d}.png")
                print({k:x for k,x in row.items() if k != "camara"}, flush=True)
            if v[1] >= 60:
                break
        (TMP / "original_toma1.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
        print("Campos observados:", len(rows), "ultimo:", rows[-1], flush=True)
        humo = []
        for objetivo in (1753, 1791, 1803, 1833, 1903, 1931):
            actual = p.u32(0x583a10)
            if objetivo - actual > 20:
                c.pulso(0x75); time.sleep((objetivo-actual-15)/50.0); c.pausa()
            while p.u32(0x583a10) < objetivo:
                c.contador = None; c.paso(); c.contador = 0x361550
            actual = p.u32(0x583a10)
            row = dict(campo=p.u32(0x361550), escena=actual, cuadro_60=actual*1.2,
                       local_coche=actual*1.2-2030,
                       captura=c.captura(TMP / f"original_coche_{actual}.png"))
            humo.append(row)
            print(row, flush=True)
            (TMP / "original_coche.json").write_text(json.dumps(humo, indent=2), encoding="utf-8")
    finally:
        if propio: s.main(["--instancia", "kit", "cerrar"])
        if not s.nuestro_pid():
            ARCHIVO.mkdir(parents=True, exist_ok=True)
            shutil.move(str(estado), str(ARCHIVO / (str(time.time_ns()) + "_base98.p2s")))
            shutil.move(str(ini), str(ARCHIVO / (str(time.time_ns()) + "_kit.ini")))
            shutil.move(str(TMP / "PCSX2_kit.ini.original"), ini)


def inventario():
    from PIL import Image, ImageDraw
    rows, fotos = [], []
    for p in sorted((EMU / "pcsx2_luna/sstates").glob("SLES-51233*.p2s")) + [Path(r"D:\tmp\satan\b1_cell.p2s")]:
        with zipfile.ZipFile(p) as z:
            ram = z.read("eeMemory.bin")
            u32 = lambda a: struct.unpack_from("<I", ram, a)[0]
            rows.append(dict(estado=str(p), campo= u32(0x361550), escena=u32(0x583a10),
                             escena_id=u32(0x5ecc54), fade=ram[0x5e9c00:0x5e9c40].hex()))
            im = Image.open(io.BytesIO(z.read("Screenshot.png"))).convert("RGB")
            im.thumbnail((320, 240))
            foto = Image.new("RGB", (320, 262)); foto.paste(im, (0, 22))
            ImageDraw.Draw(foto).text((4, 4), p.name[-6:] + "  " + str(rows[-1]["escena"]), fill="white")
            fotos.append(foto)
    hoja = Image.new("RGB", (320 * 3, 262 * ((len(fotos) + 2) // 3)))
    for i, im in enumerate(fotos): hoja.paste(im, ((i % 3) * 320, (i // 3) * 262))
    hoja.save(TMP / "estados.png")
    (TMP / "estados.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    print(json.dumps([{k:v for k,v in r.items() if k != 'fade'} for r in rows], indent=2))


def datos():
    sys.path[:0] = [str(Path(__file__).resolve().parents[1] / "mod center hd"),
                    str(Path(__file__).resolve().parents[1] / "awo_tools")]
    from importar import AfsFile
    afs = AfsFile(r"D:\DBZ3HD\ps2_games\DragonBall Z - Budokai (Europe) (En,Fr,De,Es,It).iso", "DATA_EN.AFS")
    paquete = afs.entry(2372)
    n, tabla = struct.unpack_from("<II", paquete, 16)
    for k in (13, 14, 15, 16):
        off, tam = struct.unpack_from("<II", paquete, tabla + 16*k)
        b = paquete[off:off+tam]
        (TMP / f"2372_{k}.bin").write_bytes(b)
        print("hijo", k, "tam", len(b), "cabecera", b[:96].hex())
        if k == 13:
            u = lambda o: struct.unpack_from("<I", b, o)[0]
            a = u(u(0x1c)); ejes = u(a+0x14)
            print("escalas de reposo",[struct.unpack_from('<3f',b,a+ejes+80*i+32) for i in range(u(a+0x10))])
            for i in range(u(a+0x10)):
                brazo = u(a+ejes+80*i+0x34)
                grupo = u(a+brazo+4) if brazo else 0
                if not grupo: continue
                gg = a+grupo
                for j in range(u(gg)):
                    po = gg+u(gg+u(gg+4)+4*j)
                    vt = u(po); pos = po+0xa0
                    print("humo parte", i, hex(vt), "material", struct.unpack_from("<4f",b,po+0x30),
                          "v0", struct.unpack_from("<12f",b,pos+0x20))
        if k == 14:
            import amt_ps2
            import numpy as np
            from PIL import Image
            for tx in amt_ps2.entries(b):
                rgba = amt_ps2.decode(b, tx)
                Image.fromarray(rgba).save(TMP / "humo_textura.png")
                print("textura humo", tx, "RGBA min/max", rgba.reshape(-1,4).min(0), rgba.reshape(-1,4).max(0),
                      "colores distintos", len(np.unique(rgba.reshape(-1,4), axis=0)))
        if k == 16:
            for i in range(16):
                q = struct.unpack_from("<I", b, 0x38+4*i)[0]
                if q: print("AMP", i, hex(q), b[q:q+64].hex(), struct.unpack_from("<7I",b,q+4))
        if k == 15:
            u = lambda o: struct.unpack_from("<I", b, o)[0]
            fl, _, nf, off = struct.unpack_from("<4I", b, u(0x14))
            for j in range(u(0x18)):
                p = u(off+4*(3*j+2))
                if not p: continue
                ks = [struct.unpack_from("<I3f", b, p+12+16*i) for i in range(u(p+8))]
                print("escala",j,"nombre",b[u(0x1c)+32*j:u(0x1c)+32*j+32].split(b'\0')[0],
                      "muestras",[min(ks,key=lambda q:abs(q[0]-f)) for f in (119,134,179,264,399)])
    afs.f.close()


def humo():
    """Canal cromatico: el humo es gris (R=G=B), cancela su RGB sin suponer 250.

    No da igualdad absoluta de pixeles: el fondo se mueve. Declara el intervalo
    usando dos parches de hierba y conserva los RGB de entrada en JSON.
    """
    import numpy as np
    from PIL import Image
    rows = []
    for campo in (1758, 1799, 1803, 1841, 1912, 1931):
        a = np.array(Image.open(TMP / f"original_coche_{campo}.png")).astype(float)
        fila = dict(campo=campo, parches=[])
        for x0,y0,x1,y1 in ((5,130,40,145), (5,145,40,155), (140,130,180,145)):
            rgb = a[y0:y1,x0:x1].mean((0,1))
            fila["parches"].append(dict(rect=[x0,y0,x1,y1], rgb=rgb.tolist(), g_r=float(rgb[1]-rgb[0])))
        rows.append(fila)
    for fila in rows:
        for k,p in enumerate(fila["parches"]):
            p["alfa_cromatico"] = 1-p["g_r"]/rows[-1]["parches"][k]["g_r"]
    (TMP / "humo_cromatico.json").write_text(json.dumps(dict(valida_como_alfa_absoluto=False,
        motivo='Parche no registrado: cambia hierba/carretera con la camara; NO usar para calibrar.',
        exploracion=rows), indent=2), encoding="utf-8")
    for f in rows: print(f)


def oraculo():
    """Render alfa en CPU de los quads originales; contrasta escalas sin abrir juegos."""
    import cv2
    import numpy as np
    from PIL import Image
    sys.path[:0] = [str(Path(__file__).resolve().parents[1] / "mod center hd"),
                    str(Path(__file__).resolve().parents[1] / "awo_tools")]
    import fk_b1 as K
    import amt_ps2
    amo, amm, amt = [(TMP / f"2372_{k}.bin").read_bytes() for k in (13,15,14)]
    nombres, locales, padres = K.amo_info(amo)
    nf, ang, pos = K.clip_tracks(amm,0)
    u = lambda b,o: struct.unpack_from("<I",b,o)[0]
    a = u(amo,u(amo,0x1c)); ejes = u(amo,a+0x14)
    off = u(amm,u(amm,0x14)+12)
    escalas = {}
    for i,nm in enumerate(nombres):
        p = u(amm,off+4*(3*i+2))
        if p: escalas[nm] = [struct.unpack_from("<I3f",amm,p+12+16*j) for j in range(u(amm,p+8))]
    partes = []
    for i in range(len(nombres)):
        brazo = u(amo,a+ejes+80*i+0x34)
        grupo = u(amo,a+brazo+4) if brazo else 0
        if not grupo: continue
        gg = a+grupo
        for j in range(u(amo,gg)):
            po = gg+u(amo,gg+u(amo,gg+4)+4*j)
            vp = po+0xc0
            # KEMURI: lista de dos triangulos (6 vertices), NO strip.
            v = np.array([struct.unpack_from("<4f",amo,vp+32*c) for c in (0,1,2,5)])
            uv = np.array([struct.unpack_from("<2f",amo,vp+32*c+16) for c in (0,1,2,5)])
            partes.append((i,v,uv))
    rgba = amt_ps2.decode(amt,amt_ps2.entries(amt)[0]).astype(np.float32)/255
    resultados = []
    for estado in sorted((ARCHIVO / "captura_estados").glob("*.p2s")):
        with zipfile.ZipFile(estado) as z:
            with z.open("eeMemory.bin") as mem:
                mem.seek(0x583a10); campo = int.from_bytes(mem.read(4),'little')
            if campo not in (1803,1841,1912): continue
            ram = z.read("eeMemory.bin")
        cam = np.array(struct.unpack_from("<12f",ram,0x555b00))
        ojo,obj = cam[4:7],cam[8:11]
        fr = obj-ojo; fr /= np.linalg.norm(fr)
        derecha = np.cross(fr,[0,1,0]); derecha /= np.linalg.norm(derecha)
        arriba = np.cross(derecha,fr)
        f = campo*1.2-2030
        for escala in (False,True):
            W=[]
            for i,nm in enumerate(nombres):
                L=locales[i].copy()
                if nm in ang and any(any(k[1:]) for k in ang[nm]): L[:3,:3]=K.rot(*K.interp_ang(ang[nm],f))
                if nm in pos and any(any(k[1:]) for k in pos[nm]): L[:3,3]+=K.interp_pos(pos[nm],f)
                if escala and nm in escalas: L[:3,:3] = L[:3,:3] @ np.diag(K.interp_pos(escalas[nm],f))
                W.append(W[padres[i]] @ L if padres[i]>=0 else L)
            quads=[]
            for i,v,uv in partes:
                xyz=(W[i] @ v.T).T[:,:3]; q=xyz-ojo
                z=q@fr
                if np.any(z<=1): continue
                xy=np.array([160+(q@derecha)*822.95/z,120-(q@arriba)*877.8/z]).T.astype(np.float32)
                quads.append((float(z.mean()),xy,(uv*255).astype(np.float32)))
            salida=np.zeros((240,320,4),np.float32)
            for _,xy,uv in sorted(quads,key=lambda q:-q[0]):
                h=cv2.getPerspectiveTransform(uv,xy)
                capa=cv2.warpPerspective(rgba,h,(320,240),flags=cv2.INTER_LINEAR)
                # Evitar el rect duplicado fuera de los cuatro vertices.
                mascara=np.zeros((240,320),np.uint8)
                cv2.fillConvexPoly(mascara,xy.astype(np.int32),1)
                aa=capa[:,:,3:4]*mascara[:,:,None]
                salida[:,:,:3]=capa[:,:,:3]*aa+salida[:,:,:3]*(1-aa)
                salida[:,:,3:4]=aa+salida[:,:,3:4]*(1-aa)
            Image.fromarray((np.clip(salida[:,:,3],0,1)*255).astype(np.uint8)).save(TMP/f"alfa_malla_{campo}_{int(escala)}.png")
            r=dict(campo=campo,escalas=escala,quads=len(quads),alfa_parche=float(salida[130:145,5:40,3].mean()),
                   alfa_max=float(salida[:,:,3].max()),ojo=ojo.tolist(),obj=obj.tolist())
            resultados.append(r); print(r)
    (TMP/"oraculo_humo.json").write_text(json.dumps(resultados,indent=2),encoding='utf-8')


def tablero():
    import numpy as np
    from scipy import ndimage as ndi
    from PIL import Image, ImageDraw
    estado = EMU / "pcsx2_luna/sstates/SLES-51233 (37472BE0).04.p2s"
    with zipfile.ZipFile(estado) as z:
        orig = Image.open(io.BytesIO(z.read("Screenshot.png"))).convert("RGB")
    port = Image.open(Path(os.environ.get("SOLSAT_RUN",r"D:\DBZ3HD\_pruebas\20261031-160023_opus2_satan"))/"e_tablero.png").convert("RGB")
    # Este tablero usa Fit43(DisplaySize), NO el rect 16:9 del framebuffer guest.
    w,h=port.size; cw=min(w,round(h*4/3)); ch=min(h,round(w*3/4))
    rect=((w-cw)//2,(h-ch)//2,(w+cw)//2,(h+ch)//2)
    print("dimensiones originales", orig.size, "port", port.size, "recorte UI 4:3", rect)
    orig=orig.resize((640,480)); port=port.crop(rect).resize((640,480))
    rows=[]
    for nombre,im in (("original",orig),("port",port)):
        a=np.array(im)
        mask=(a[380:475].sum(2).astype(int)<45)
        mask=ndi.binary_opening(mask,iterations=1)
        labs,n=ndi.label(mask)
        sil=[]
        for i in range(1,n+1):
            yy,xx=np.where(labs==i)
            if len(xx)<120 or len(xx)>4000 or xx.min()<280: continue
            sil.append(dict(caja=[int(xx.min()),int(yy.min()+380),int(xx.max()+1),int(yy.max()+381)],area=len(xx)))
        rows.append(dict(imagen=nombre,siluetas=sorted(sil,key=lambda s:s['caja'][0])))
        print(rows[-1])
        dr=ImageDraw.Draw(im)
        for s in sil: dr.rectangle(s['caja'],outline='lime')
    hoja=Image.new('RGB',(640,240)); hoja.paste(orig.resize((320,240)),(0,0));hoja.paste(port.resize((320,240)),(320,0))
    hoja.save(TMP/'tablero_comparacion_05.png')
    (TMP/'tablero.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')


def pose():
    import pruebas as P
    import numpy as np
    from PIL import Image
    def observar(j, par):
        j.log(r"cinematica: paso 1/11",200)
        P.dormir(4.7867)
        def rd(a,n):
            b=P.leer(j.h,P.GBASE+a,n)
            if b is None or len(b)!=n: raise P.Fallo(f"lectura incompleta {a:X}")
            return b
        u=lambda a:struct.unpack('>I',rd(a,4))[0]
        mgr=u(0x823d6cf4); x=mgr+2096
        r=dict(mgr=hex(mgr),actor=hex(x),codigo=u(x+2928),cuerpos=[])
        for offset in (0x650,0xcd0):
            aa=x+offset; mod=u(aa+0x538)
            v=dict(offset=hex(offset),codigo=u(aa+0x520),clip=hex(u(aa+0x604)),
                   cuadro=struct.unpack('>f',rd(aa+0x608,4))[0],modelo=hex(mod))
            if mod:
                tb=u(mod+1032)
                if 0x10000000<=tb<0xf0000000:
                    matrices=np.array(struct.unpack('>896f',rd(tb,56*64))).reshape(56,4,4)
                    v['tabla']=hex(tb);v['huesos']=matrices.tolist()
                    angulos=[]
                    for a,b in ((32,34),(34,35),(11,13),(13,14),(2,4),(4,5),(50,52),(52,53)):
                        d=matrices[b,3,:3]-matrices[a,3,:3]
                        angulos.append(dict(segmento=[a,b],grados_vertical=float(np.degrees(np.arccos(np.clip(-d[1]/np.linalg.norm(d),-1,1))))))
                    v['angulos']=angulos
            r['cuerpos'].append(v)
        foto=j.captura('pose_coche')
        im=Image.open(foto);w,h=im.size;cw=min(w,round(h*4/3))
        im.crop(((w-cw)//2,0,(w+cw)//2,h)).resize((320,240)).save(TMP/'port_pose_05.png')
        (TMP/'pose_port.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
        print(json.dumps({**r,'cuerpos':[{k:v for k,v in c.items() if k!='huesos'} for c in r['cuerpos']]},indent=2))
    P.ESCENARIOS['satan_sol_pose']=("solSAT: pose nativa y clip efectivo de la toma del coche",
        lambda par:{'DBZ3_TEST_PASARELA':'auto,seg=8,desde=1','DBZ3_PS2_GAMES':r'D:\DBZ3HD\ps2_games'},observar)
    r=P.correr('opus2','satan_sol_pose',par={'ventana':'1280x960'},forzar=True)
    (TMP/'pose_resultado.json').write_text(json.dumps(r,default=str,indent=2),encoding='utf-8')
    if not r['ok']: raise SystemExit(1)


def pose_original():
    import numpy as np
    sys.path[:0] = [str(Path(__file__).resolve().parents[1]/'mod center hd')]
    from importar import AfsFile
    import fk_b1 as K
    afs=AfsFile(r'D:\DBZ3HD\ps2_games\DragonBall Z - Budokai (Europe) (En,Fr,De,Es,It).iso','DATA_EN.AFS')
    pk=afs.entry(2372); tabla=struct.unpack_from('<I',pk,20)[0]
    off,tam=struct.unpack_from('<2I',pk,tabla+16*3)
    amm=pk[off:off+tam];amo=afs.entry(2363)
    nombres,loc,padres=K.amo_info(amo)
    xyz,mat=K.fk(amo,amm,0,0)
    r=dict(amo=2363,amm='2372/3',clip=0,cuadros=K.clip_tracks(amm,0)[0],segmentos=[])
    for aa,bb in (('LARM1','LARM2'),('LARM2','LHANDROT'),('RARM1','RARM2'),('RARM2','RHANDROT'),
                  ('LLEG1','LLEG2'),('LLEG2','LFOOT1'),('RLEG1','RLEG2'),('RLEG2','RFOOT1')):
        a=next((i for i,n in enumerate(nombres) if n.endswith('_'+aa)),None)
        b=next((i for i,n in enumerate(nombres) if n.endswith('_'+bb)),None)
        if a is None or b is None: continue
        d=xyz[b]-xyz[a]
        r['segmentos'].append(dict(nombres=[nombres[a],nombres[b]],indices=[a,b],
                                  grados_vertical=float(np.degrees(np.arccos(np.clip(-d[1]/np.linalg.norm(d),-1,1))))))
    (TMP/'pose_original.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
    print(json.dumps(r,indent=2));afs.f.close()


if __name__ == "__main__":
    os.environ.setdefault("DBZ3_AGENTE", "solSAT")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("accion", choices=["inventario", "original", "datos", "humo", "oraculo", "tablero", "pose", "pose_original"])
    a = ap.parse_args()
    globals()[a.accion]()

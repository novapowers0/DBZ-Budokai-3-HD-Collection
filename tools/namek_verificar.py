"""Verificación estática del recurso propio y adaptación al B3 local del jugador.

No arranca juegos ni modifica partidas. Las láminas son del renderizador del
taller, no capturas de combate; se etiquetan como tales.
"""
import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'mod center hd/cac'),str(ROOT/'mod center hd'),str(ROOT/'awo_tools')]


def main():
    import numpy as np
    import namek_original as nm
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--salida',type=Path,required=True)
    ap.add_argument('--laminas',action='store_true')
    args=ap.parse_args()
    if args.salida.exists():raise FileExistsError('Conservar evidencia anterior y elegir salida nueva')
    if not args.salida.parent.is_dir():raise FileNotFoundError('Falta directorio padre')
    args.salida.mkdir()
    hashes={}
    for form in ('base','f_nam_nail','f_nam_kami'):
        for p in nm.catalogo():
            mesh=nm.recurso(p['id'],form)
            P,N,T,UV=(np.asarray(mesh[x]) for x in ('P','N','tris','UV'))
            assert P.shape==N.shape and UV.shape==(len(P),2)
            assert np.isfinite(P).all() and np.isfinite(N).all() and np.isfinite(UV).all()
            assert T.min()>=0 and T.max()<len(P)
            assert np.max(abs(np.linalg.norm(N,axis=1)-1))<1e-6
            assert set(mesh['huesos'])<=set(nm.RIG)
            assert np.linalg.norm(np.cross(P[T[:,1]]-P[T[:,0]],P[T[:,2]]-P[T[:,0]]),axis=1).min()>1e-10
            hashes[form+'/'+str(p['id'])]=hashlib.sha256(P.tobytes()+T.tobytes()).hexdigest()
    assert len(set(hashes['base/'+str(x['id'])] for x in nm.catalogo()))==29
    nm.recurso(3599)
    print('PASS NMC recursos: 88 hashes, 29 geometrías distintas, UV/normales/índices/huesos válidos',flush=True)
    import vista_heroe as vh
    import model_render as mr
    import roster_build as rb
    from b3_gateway import DrawTable
    from awg_vertex_buffer import AwgVertexBuffer
    base=dict(cuerpo=11,traje=0,donante=11,b3_torso=3600,b3_piernas=3640,
              b3_guantes=3680,b3_botas=3700,pelo=0,rostro=0)
    fields=dict(cabeza='b3_torso',orejas='b3_piernas',parches='b3_guantes',ropa='b3_botas',antenas='pelo',mandibula='rostro')
    # Contrato REAL de ArgsFicha → parser, no «cara» inventada por el ensayo.
    raw=';'.join('%s=%d'%(k,v) for k,v in dict(base,pelo=5,rostro=3).items())
    parsed=vh.leer_ficha(raw)
    assert nm.seleccion(parsed)['antenas']==5 and nm.seleccion(parsed)['mandibula']==3
    bad=[dict(base,cuerpo=0),dict(base,b3_piernas=3600),dict(base,pelo=6),dict(base,rostro=4)]
    for f in bad:
        try:nm.seleccion(f)
        except ValueError:pass
        else:raise AssertionError('Se acepta combinación incompatible')
    print('PASS NMC selección: serialización rostro/pelo conserva valores; cuatro entradas inválidas rechazadas',flush=True)
    records=[]
    with tempfile.TemporaryDirectory(prefix='nmc_verificar_') as work:
        afs=rb.Afs(str(ROOT/'us/data_cmn.afs'),work)
        source=afs.entry(350)
        native=mr.Model(source)
        if args.laminas:
            vh.tira(native,256,4).save(args.salida/'nativo_B3_taller.png')
        for slot,names in nm.NOMBRES.items():
            pictures=[]
            for i,name in enumerate(names):
                f=dict(base);field=fields[slot]
                f[field]=i if field in ('pelo','rostro') else nm.BASES[slot]+i
                data,report=vh.combate_bin(source,f,str(ROOT),afs)
                model=mr.Model(data)
                active=len(model.tris)
                assert active==report['piezas_b3']['originales']['triangulos'], (slot,i,active)
                assert np.isfinite(model.pos).all()
                # Alfa 0 es selector toon: su RGB debe sobrevivir al escalado.
                # Medir las UV usadas, no el fondo negro de celdas sin asignar.
                # Una media global daba un falso fallo al cambiar de prenda.
                used=np.unique(model.tris)
                material=model.mats[int(model.tri_mat[0])]
                samples=mr._sample(model.tex[material['tex']],model.uv[used,0],model.uv[used,1])
                shaded=samples[:,3]<.01
                assert shaded.any(), 'No quedan superficies con sombreado toon'
                assert (samples[shaded,:3].sum(axis=1)>.05).all(), 'Una superficie sombreada ha perdido su RGB'
                expected=report['piezas_b3']['originales']['colores_rgb']
                palette=np.asarray(list(expected.values()),float)/255
                assert (np.max(abs(samples[:,:3,None]-palette.T[None,:,:]),axis=1).min(axis=1)<.05).all(), 'UV fuera de las regiones de color propias'
                for awg in range(1,model.avb.n_awg):
                    table=DrawTable(AwgVertexBuffer._parse(data,awg=awg))
                    assert all(d['B'][1]==0 for d in table.draws)
                summary={}
                forms=vh.formas_ssj(afs,350,f,str(ROOT),summary)
                assert [x[1] for x in forms]==[1,2]
                for key,value in summary.items():
                    assert value['piezas']['originales']['seleccion']==nm.seleccion(f)
                # No hay copia de superficie nativa activa en los otros AWG.
                record=dict(slot=slot,tipo=i+1,nombre=name,vertices=len(model.pos),triangulos=active,
                            formas=[x[1] for x in forms],seleccion=nm.seleccion(f))
                records.append(record)
                if args.laminas:pictures.append(vh.tira(model,256,4))
                print('PASS NMC injerto + 2 formas:',slot,i+1,'v=',len(model.pos),'t=',active,flush=True)
            if args.laminas:
                from PIL import Image,ImageDraw
                labels=('frente','derecha','espalda','izquierda')
                native_pic=Image.open(args.salida/'nativo_B3_taller.png').convert('RGBA')
                width=(len(names)+1)*256
                page=Image.new('RGB',(width,4*294+44),(26,30,34));draw=ImageDraw.Draw(page)
                draw.text((12,8),'NMC '+slot+' | TALLER B3: no acredita combate ni aceptación artística',fill='white')
                for row,label in enumerate(labels):
                    y=44+row*294
                    for col,pic in enumerate([native_pic]+pictures):
                        tile=pic.crop((row*256,0,(row+1)*256,256)).convert('RGBA')
                        page.paste(tile,(col*256,y+24),tile.getchannel('A'))
                        draw.text((col*256+8,y),'B3 nativo · '+label if col==0 else 'Tipo %d · %s'%(col,names[col-1]),fill='white')
                page.save(args.salida/('lamina_'+slot+'.png'))
        preview=vh.ficha_malla(native,base,str(ROOT),str(args.salida/'tipo1.malla'),afs)
        payload=(args.salida/'tipo1.malla').read_bytes()
        assert payload[:4]==b'HMV1' and b'RMP1' in payload
        assert preview['piezas_b3']['originales']['seleccion']==nm.seleccion(base)
        print('PASS NMC previa: HMV1 y bandas RMP1 de la misma selección de combate',flush=True)
        default=nm.colores_ficha(base)
        for region,index in zip(('piel','musculo','tela','ribete'),range(4)):
            colors=[(0,0,0)]*4;colors[index]=(210,-20,30)
            changed=nm.colores_ficha(dict(base,partes=colors))
            assert changed[region]!=default[region]
            assert all(changed[k]==default[k] for k in default if k!=region)
            _,detail=vh.combate_bin(source,dict(base,partes=colors),str(ROOT),afs)
            assert detail['piezas_b3']['originales']['colores_rgb'][region]!=[round(c*255) for c in default[region][:3]]
        print('PASS NMC colores: cuatro HSV persistentes, cambio por región sin alterar las otras',flush=True)
    result=dict(recursos=88,tipos=29,combinaciones_verificadas=len(records),
                arte_nativo_activo=False,registros=records,
                guardado_real='pendiente',combate_real='pendiente',transformacion_real='pendiente',
                ui='helper preparado; heroe_ui.cpp reservado por solHEROEb')
    (args.salida/'resultado.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('PASS NMC estático: 29 selecciones en B3 y 58 modelos de formas; NO es guardado/combate real',flush=True)


if __name__=='__main__':main()

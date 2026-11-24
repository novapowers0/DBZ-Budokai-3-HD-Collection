"""Verifica recursos reales, unión del cuello, recetas y montaje en contenedor B3.

No equivale a combatir; los bins resultantes quedan sólo en la carpeta de prueba.
"""
import argparse
import hashlib
import itertools
import json
import sys
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'mod center hd/cac'),str(ROOT/'mod center hd'),str(ROOT/'awo_tools')]


def main():
    import arcosiano_original as a
    import partes_nativas as pn
    import model_render as mr
    import vista_heroe as vh
    ap=argparse.ArgumentParser()
    ap.add_argument('--salida',type=Path,required=True)
    ap.add_argument('--todas',action='store_true')
    args=ap.parse_args()
    if args.salida.exists() or not args.salida.parent.is_dir():raise ValueError('Salida nueva requerida')
    args.salida.mkdir()
    folder=ROOT/'res/heroe/arcosiano_r16'
    manifest=json.loads((folder/'catalogo.json').read_text(encoding='utf-8'))
    assert [len(v) for v in a.NAMES.values()]==[8,6,4,4,3,3]
    assert len(manifest['files'])==210
    for r in manifest['files']:
        b=(folder/r['file']).read_bytes();assert hashlib.sha256(b).hexdigest()==r['sha256']
        m=json.loads(b)
        if not m['P']:
            assert r['slot']=='cuernos' and r['variant']==5 and not m['tris'];continue
        p,n,t=(np.asarray(m[k]) for k in ('P','N','tris'))
        assert np.isfinite(p).all() and np.isfinite(n).all()
        assert p.shape==n.shape and len(m['bones'])==len(p)==len(m['bindings'])
        assert t.min()>=0 and t.max()<len(p) and len(m['regions'])==len(t)
        assert np.max(np.abs(np.linalg.norm(n,axis=1)-1))<.002
        assert np.min(np.linalg.norm(np.cross(p[t[:,1]]-p[t[:,0]],p[t[:,2]]-p[t[:,0]]),axis=1))>1e-9
    for f in manifest['forms']:
        b=a.recurso('base',form=f)
        for h in range(8):
            m=a.recurso('cabeza',h,f)
            for key in ('P','N'):
                assert np.allclose(np.asarray(m[key])[m['seams']['neck']],np.asarray(b[key])[b['seams']['neck']],atol=1e-7)
    print('PASS recursos Blender: 210 archivos, 28 tipos, 6 estados; índices, normales, hashes y 48 uniones de cuello',flush=True)
    cases=list(itertools.product(range(8),range(6),range(4),range(4),range(3),range(3)))
    def ficha(v,body=27):
        h,hr,fc,pl,g,t=v
        return dict(cuerpo=body,donante=body,raza=3,traje=0,pelo=0,cara=0,
                    b3_torso=3800+h+hr*8+g*48,b3_piernas=3960+fc,b3_guantes=3980+pl,b3_botas=3990+t)
    for v in cases:
        sel=a.seleccion(ficha(v))
        assert tuple(sel[k] for k in ('cabeza','cuernos','cara','placas','gema','cola'))==v
    for bad in ({'cuerpo':0,'b3_torso':3800},{'cuerpo':27,'b3_torso':3950},
                {'cuerpo':27,'b3_torso':3800,'b3_botas':3960}):
        try:a.seleccion(bad)
        except ValueError:pass
        else:raise AssertionError('Receta incompatible aceptada')
    print('PASS recetas: 6912 combinaciones independientes y rechazo de cuerpo/zona/ID incompatibles',flush=True)
    (args.salida/'afs').mkdir()
    afs=vh.rb.Afs(str(ROOT/'us/data_cmn.afs'),str(args.salida/'afs'))
    reports=[]
    variants=[(0,5,0,0,0,0)]
    if args.todas:
        variants += [tuple(k if i==z else (5 if i==1 else 0) for i in range(6))
                     for z,count in enumerate((8,6,4,4,3,3)) for k in range(count)]
    for body in (27,38):
        source=vh.rb.IDS[body]
        for form in range(int(source['forms'])):
            fid=source['models'][form]
            raw=afs.entry(fid)
            model=mr.Model(raw)
            vv=variants if form==0 and body==27 else [variants[0]]
            for vi,v in enumerate(vv):
                f=dict(ficha(v,body),_nativa_forma=form)
                data,summary=vh.combate_bin(raw,f,str(ROOT),afs)
                edited=mr.Model(data)
                p,n=edited.posed(); used=np.unique(edited.tris)
                assert len(used)>0 and np.isfinite(p[used]).all() and np.isfinite(n[used]).all()
                info=summary['piezas_b3']['originales']
                assert info['original'] and info['raza']=='arcosiano'
                path=args.salida/('cuerpo%d_forma%d_tipo%d.bin'%(body,form,vi))
                path.write_bytes(data)
                reports.append(dict(cuerpo=body,forma=form,receta=v,fid=fid,**info))
                print('PASS montaje B3: cuerpo=%d forma=%d tipo=%d ventanas=%s triángulos=%d'%(body,form,vi,info.get('ventanas'),len(edited.tris)),flush=True)
    # Ejercitar también el callback público de las transformaciones y su donante.
    for body in (27,38):
        f=ficha((1,0,1,1,1,1),body)
        summary={}
        out=pn.formas_b3(afs,vh.rb.IDS[body]['models'][0],f,str(ROOT),summary,vh.combate_bin)
        assert [i for _,i in out]==(list(range(1,6)) if body==27 else [1,2])
        assert a.seleccion(f)==a.seleccion(ficha((1,0,1,1,1,1),body))
        for data,index in out:
            model=mr.Model(data);p,n=model.posed();used=np.unique(model.tris)
            assert len(used) and np.isfinite(p[used]).all() and np.isfinite(n[used]).all()
        print('PASS callback formas: cuerpo=%d índices=%s'%(body,[i for _,i in out]),flush=True)
    (args.salida/'resultados.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')


if __name__=='__main__':main()

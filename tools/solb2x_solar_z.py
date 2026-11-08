"""Contrasta cuatro capturas reales del solar; la sonda Z no es el original."""
import argparse
import hashlib
import json
from pathlib import Path

def validar_metadatos(fotos, guest_estable):
    if not guest_estable:
        raise ValueError('guest cambió durante las capturas')
    a=fotos[0]
    for i,b in enumerate(fotos):
        if not b.get('z_solicitado') or b.get('z_estado_sdk')!=2:
            raise ValueError('sin profundidad real del SDK')
        if any(b[k]!=a[k] for k in ('guest_publicado','guest_image','quads')):
            raise ValueError('contenido publicado distinto')
        if i and b['ui_frame']<=fotos[i-1]['ui_frame']:
            raise ValueError('orden de capturas inválido')
        if b['quads_emitidos']!=(0 if i==3 else 5):
            raise ValueError('emisión de capas distinta')
    if [v['z_modo_foto'] for v in fotos[:3]]!=[0,2,1]:
        raise ValueError('modos Z distintos')
    if fotos[0]['z_d3d_normal']!=0 or fotos[2]['z_d3d_normal']!=-1:
        raise ValueError('Z original o ablación distintos')
    if not fotos[3]['sin_solar']:
        raise ValueError('falta imagen base sin solar')


def analizar(run):
    import numpy as np
    from PIL import Image, ImageDraw
    archivos=['solar_foto_ui.json','solar_foto_pruebaz_ui.json','solar_foto_sinz_ui.json','solar_foto_sin_ui.json']
    fotos=[json.loads((run/p).read_bytes()) for p in archivos]
    m=json.loads((run/'medida.json').read_bytes())['solar_previo_foto']
    estable=m['guest_detenido'] and m['contraste_capas']['guest_igual_en_ambas'] and all(
        v['guest_igual'] for v in m['pruebas_z'])
    validar_metadatos(fotos,estable)
    nombres=['solar_previo.png','solar_pruebaz.png','solar_sinz.png','solar_sin_capas.png']
    arrays=[]
    for n in nombres:
        with Image.open(run/n) as im:arrays.append(np.array(im.convert('RGB'),dtype=np.int16))
    original,sonda,sinz,base=arrays
    if any(x.shape!=base.shape for x in arrays):raise ValueError('tamaños distintos')
    h,w=base.shape[:2]
    mask=Image.new('1',(w,h));d=ImageDraw.Draw(mask)
    l,t,r,b=fotos[0]['guest_image']
    for q in fotos[0]['quads']:
        d.polygon([(l+q['xy'][j][0]*(r-l)/640,t+q['xy'][j][1]*(b-t)/448) for j in (0,1,3,2)],fill=1)
    soporte=np.array(mask,dtype=bool)
    aporte=original-base;contribuye=np.any(aporte!=0,axis=2)
    tapado=np.any(original!=sonda,axis=2)
    resultado=dict(guest_publicado=fotos[0]['guest_publicado'],ui_frames=[f['ui_frame'] for f in fotos],
        profundidad_sdk=2,z_original_d3d=0,z_sonda=fotos[1]['z_d3d_normal'],
        original_vs_sinz_pixeles=int(np.any(original!=sinz,axis=2).sum()),
        solar_aporta_pixeles=int(contribuye.sum()),solar_oscurece_pixeles=int(np.any(aporte<0,axis=2).sum()),
        aporte_fuera_poligonos=int((contribuye&~soporte).sum()),
        sonda_recorta_pixeles=int(tapado.sum()),
        sonda_recorte_fuera_aporte=int((tapado&~contribuye).sum()),
        sonda_recorte_igual_base=int((tapado&np.all(sonda==base,axis=2)).sum()),
        sonda_supervivientes_iguales=int((contribuye&~tapado).sum()),
        sonda_aumenta_aporte_pixeles=int(np.any(sonda>original,axis=2).sum()),
        sonda_oscurece_base_pixeles=int(np.any(sonda<base,axis=2).sum()),
        max_aporte_rgb=aporte.max(axis=(0,1)).tolist(),aceptacion_escena=False,
        limites=['La sonda usa otro Z, no demuestra oclusión PAL del solar fijo.',
                 'Guest detenido y contenido publicado idéntico no acreditan identidad entre resolve Z y frame guest.',
                 'La conversión al plano cercano es derivada de la proyección, no comparación bit a bit PS2/HD.'])
    resultado['composicion_aditiva_observada']=resultado['solar_aporta_pixeles']>0 and not any(
        resultado[k] for k in ('solar_oscurece_pixeles','aporte_fuera_poligonos','original_vs_sinz_pixeles'))
    resultado['oclusion_sonda_observada']=resultado['sonda_recorta_pixeles']>0 and not any(
        resultado[k] for k in ('sonda_recorte_fuera_aporte','sonda_aumenta_aporte_pixeles','sonda_oscurece_base_pixeles'))
    resultado['fuentes']=[dict(ruta=str(run/n),sha256=hashlib.sha256((run/n).read_bytes()).hexdigest())
        for n in archivos+nombres+['medida.json']]
    return resultado


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--salida',type=Path,required=True)
    a=p.parse_args();r=analizar(a.run)
    with a.salida.open('x',encoding='utf-8') as f:json.dump(r,f,ensure_ascii=False,indent=2)
    print(json.dumps({k:v for k,v in r.items() if k not in ('fuentes','limites')},ensure_ascii=False,indent=2))

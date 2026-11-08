"""Audita la entrada natural de Bubbles desde una serie PAL con --bubbles47.

Conserva el LIVE anterior y el TARGET/LIVE de la entrada para replay externo.
No establece una pose inicial del port ni acepta paridad visual.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def live(row):
    nodes=row['bubbles']['npc']['mat47_128']
    if [x['i'] for x in nodes]!=list(range(47)):
        raise ValueError('LIVE sin los 47 huesos en orden')
    raw=b''.join(bytes.fromhex(x['raw']) for x in nodes)
    if len(raw)!=47*128:
        raise ValueError('LIVE truncado')
    return raw


def distancia_target(row):
    a=live(row)
    b=bytes.fromhex(row['bubbles_animacion']['target_hex'])
    if len(b)!=47*64:
        raise ValueError('TARGET truncado')
    maxima=[0.,0.,0.]
    sin_raiz=[0.,0.,0.]
    for i in range(47):
        for j,(off,n) in enumerate(((0,4),(16,3),(32,3))):
            va=struct.unpack_from('<'+'f'*n,a,128*i+64+off)
            vb=struct.unpack_from('<'+'f'*n,b,64*i+off)
            if not all(math.isfinite(x) for x in va+vb):
                raise ValueError('TARGET/LIVE no finito')
            err=max(abs(x-y) for x,y in zip(va,vb))
            maxima[j]=max(maxima[j],err)
            if i!=1:
                sin_raiz[j]=max(sin_raiz[j],err)
    return dict(qps=maxima,qps_sin_raiz1=sin_raiz)


def capa_facial(row):
    f=row['bubbles_animacion'].get('facial')
    if f is None or not f['disponible']:
        return None
    origen,destino=f['mapa_fuente'],f['mapa_destino']
    if len(origen)!=9 or len(destino)!=9 or not 1<=f['count']<=128:
        raise ValueError('Contrato facial incompleto')
    indices=[(i,d,s) for i,(d,s) in enumerate(zip(destino,origen)) if d>=0 and s>=0]
    if [(p['i'],p['destino'],p['fuente']) for p in f['pares']]!=indices:
        raise ValueError('Pares faciales distintos de los mapas originales')
    cuerpo=live(row)
    iguales=[]
    for p in f['pares']:
        d,s=p['destino'],p['fuente']
        b=bytes.fromhex(p['raw_fuente'])
        if not 0<=d<47 or not 0<=s<f['count'] or len(b)!=128:
            raise ValueError('Nodo facial fuera de rango')
        igual=b==cuerpo[128*d:128*(d+1)]
        if igual!=p['igual128']:
            raise ValueError('Comparacion facial capturada incoherente')
        iguales.append(igual)
    return dict(pares=len(indices),iguales128=sum(iguales),
        destinos=[d for _,d,_ in indices],seleccion=f['seleccion'],
        desactivada=f['desactivada'])


def cabecera(row):
    n=row['bubbles']['npc']
    a=row['bubbles_animacion']
    return dict(n=row['n'],estado=n['estado'],codigo=hex(n['codigo']),
        reloj=n['cuadro'],target_cuadro=a['frame'],packed=hex(a['packed']),
        cb4=a['cb4'],cb8=a['cb8'],pos=n['pos'],raiz=n['rootlocal'],
        camara=row['bubbles'].get('camara'),jugador=row['bubbles'].get('jugador'),
        target_contra_live=distancia_target(row),facial=capa_facial(row))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('serie',type=Path)
    p.add_argument('--salida',type=Path,required=True)
    p.add_argument('--cpp',action='store_true',help='Contrasta la entrada con el decoder STAGED47 autenticado')
    a=p.parse_args()
    out=a.salida.resolve()
    if not out.parent.is_dir() or out.exists():
        p.error('--salida debe ser nueva y tener padre existente')
    if out.is_relative_to(Path('D:/tmp/solGP2').resolve()):
        p.error('No escribir en evidencias GP2')
    raw=a.serie.read_bytes()
    rows=[json.loads(x) for x in raw.splitlines() if x.strip()]
    if len(rows)<2:
        raise ValueError('Serie sin pasos suficientes')
    for i,r in enumerate(rows):
        if not r['estable'] or not r['bubbles']['estable'] or r['n']!=r['bubbles']['n']:
            raise ValueError('Lectura original inestable')
        if r['bubbles']['clave']!=0x7D1 or r['bubbles_animacion']['count']!=47:
            raise ValueError('No es Bubbles47')
        if i and (r['n']-rows[i-1]['n'])&0xffffffff!=1:
            raise ValueError('Salto en la serie PAL')
        live(r)
        distancia_target(r)
        capa_facial(r)
    cambios=[]
    for i in range(1,len(rows)):
        old,now=rows[i-1]['bubbles']['npc'],rows[i]['bubbles']['npc']
        if (old['estado'],old['codigo'])!=(now['estado'],now['codigo']):
            cambios.append(dict(anterior=cabecera(rows[i-1]),actual=cabecera(rows[i])))
    entradas=[i for i in range(1,len(rows)) if rows[i]['bubbles']['npc']['estado']==0
              and rows[i-1]['bubbles']['npc']['estado']!=0]
    result=dict(serie=str(a.serie.resolve()),sha256=sha(raw),campos=len(rows),
        primero=cabecera(rows[0]),ultimo=cabecera(rows[-1]),consecutivos=True,
        cambios=cambios,entradas0=len(entradas),normal=False,paridad_visual=False,
        fuente_python_sha256=sha(Path(__file__).read_bytes()),
        limites=['Primera entrada0; no toda la intro ni todas las transiciones',
                 'Facial desde RAM independiente; no decoder facial integrado',
                 'Matrices locales; sin jerarquia/piel/vertices ni aceptacion visual',
                 'Retorno0 del diagnostico no significa igualdad'])
    facial_por_estado={}
    for r in rows:
        f=capa_facial(r)
        if f is not None:
            key=str(r['bubbles']['npc']['estado'])
            x=facial_por_estado.setdefault(key,dict(campos=0,pares=0,iguales128=0))
            x['campos']+=1
            x['pares']+=f['pares']
            x['iguales128']+=f['iguales128']
    result['facial_por_estado']=facial_por_estado
    out.mkdir()
    if entradas:
        k=entradas[0]
        before,after=rows[k-1],rows[k]
        payloads={'antes_live.bin':live(before),'despues_live.bin':live(after),
                  'antes_target.bin':bytes.fromhex(before['bubbles_animacion']['target_hex']),
                  'antes_mapa.bin':bytes.fromhex(before['bubbles_animacion']['mapa_hex']),
                  'target.bin':bytes.fromhex(after['bubbles_animacion']['target_hex']),
                  'mapa.bin':bytes.fromhex(after['bubbles_animacion']['mapa_hex'])}
        ba,aa=before['bubbles_animacion'],after['bubbles_animacion']
        payloads['meta.bin']=struct.pack('<6f2I',ba['cb4'],ba['cb8'],aa['cb4'],aa['cb8'],
                                         ba['frame'],aa['frame'],ba['packed'],aa['packed'])
        if capa_facial(after) is not None:
            f=aa['facial']
            payloads['facial.bin']=struct.pack('<IIiI',len(f['pares']),f['count'],
                f['seleccion'],f['desactivada'])+b''.join(
                    struct.pack('<ii',p['destino'],p['fuente'])+bytes.fromhex(p['raw_fuente'])
                    for p in f['pares'])
        # Inicio de estado0: CB4=.005, CB8=.03/.01. El valor capturado es
        # posterior a207180; guardamos ambos, sin invertir el truncado de float.
        meta=dict(antes=cabecera(before),despues=cabecera(after),
                  limite='Entrada natural observada; no inicializa el port ni reproduce toda la intro')
        result['entrada0']=meta
        for name,data in payloads.items():
            (out/name).write_bytes(data)
        result['fixtures']={name:dict(bytes=len(data),sha256=sha(data)) for name,data in payloads.items()}
        if a.cpp:
            import iwx_bubbles_evidencia as E
            result['autenticacion_staged47']=E.comprobar(E.ORIGEN)
            staged=E.ORIGEN/'staged47'
            fuente=Path(__file__).with_name('iwx_bubbles_matrices.cpp').resolve()
            exe=out/'entrada47.exe'
            E.ejecutar([E.CLANG,'-std=c++17','-O2','-ffp-contract=off','-I',staged,
                        fuente,'-o',exe],out,out/'compilar.json')
            result['fuente_cpp_sha256']=sha(fuente.read_bytes())
            result['cpp']=E.ejecutar([exe,out],staged,out/'cpp.json')
            for line in result['cpp']['stdout'].splitlines():
                if line.startswith(('RESUMEN ','ENTRADA ','TARGET ','FACIAL_RESUMEN ')):
                    print(line)
    elif a.cpp:
        raise ValueError('Sin entrada0 natural para replay C++')
    (out/'resultado.json').write_text(json.dumps(result,indent=1,ensure_ascii=False,allow_nan=False),encoding='utf-8')
    print('CAMPOS',len(rows),rows[0]['n'],rows[-1]['n'],'entradas0',len(entradas))
    for c in cambios:
        print('CAMBIO',json.dumps(c,ensure_ascii=False))
    print('EVIDENCIA',out)


if __name__=='__main__':
    main()

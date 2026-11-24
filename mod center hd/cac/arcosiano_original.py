"""Catálogo modular y recursos de autor Arcosianos. Sin generadores en ejecución."""
import hashlib
import json
from functools import lru_cache
from pathlib import Path

NAMES={
    'cabeza':('Orbe','Estela','Velamen','Bifurca','Estrato','Visera','Martillo','Aguja'),
    'cuernos':('Horizonte','Ascua','Ráfaga','Averno','Brotes','Despejado'),
    'cara':('Cuña','Respirador','Bastión','Lúnula'),
    'placas':('Arco','Vértice','Escama','Baluarte'),
    'gema':('Lente','Rombo','Tríada'),
    'cola':('Látigo','Quilla','Maza')}
ROOT=Path(__file__).resolve().parents[2]


def desempaquetar(ident):
    if not 3800<=ident<3944:raise ValueError('Identidad Arcosiana inválida')
    n=ident-3800
    return n%8,(n//8)%6,n//48


def seleccion(ficha):
    ids=[int(ficha.get('b3_'+s,-1)) for s in ('torso','piernas','guantes','botas')]
    if not any(3800<=i<4000 for i in ids):return {}
    if int(ficha.get('cuerpo',-1)) not in (27,38):raise ValueError('Piezas Arcosianas en cuerpo incompatible')
    h,horn,g=desempaquetar(ids[0] if ids[0]>=0 else 3800+8*5)
    out={'cabeza':h,'cuernos':horn,'gema':g}
    for slot,ident,start,count in zip(('cara','placas','cola'),ids[1:],(3960,3980,3990),(4,4,3)):
        if ident<0:ident=start
        if not start<=ident<start+count:raise ValueError('Pieza de otra zona/receta: '+str(ident))
        out[slot]=ident-start
    return out


def catalogo():
    starts={'cabeza':3800,'cuernos':3950,'cara':3960,'placas':3980,'gema':3970,'cola':3990}
    return [dict(tipo='arcosiano_'+s,id=starts[s]+k,nombre='Tipo %d — %s'%(k+1,n),
                 idx_afs=-1,archivo='arcosiano_r16:'+s,cuerpos=[27,38])
            for s,names in NAMES.items() for k,n in enumerate(names)]


@lru_cache(maxsize=4)
def _manifest(folder):
    return json.loads((Path(folder)/'catalogo.json').read_text(encoding='utf-8'))


def recurso(slot,k=0,form='base',juego=None,expression=0):
    folder=(Path(juego) if juego else ROOT)/'res/heroe/arcosiano_r16'
    manifest=_manifest(str(folder))
    name='%s_%02d_%s%s.json'%(slot,k,form,'_exp1' if expression and slot=='cara' else '')
    entry=next((r for r in manifest['files'] if r['file']==name),None)
    if entry is None:raise ValueError('Recurso propio sin versión para la forma: '+name)
    payload=(folder/name).read_bytes()
    if hashlib.sha256(payload).hexdigest()!=entry['sha256']:raise ValueError('Huella incorrecta: '+name)
    mesh=json.loads(payload)
    mesh['rig']={n:{'pos':p} for n,p in manifest['bind'].items()}
    mesh['palette']=manifest['palette']
    return mesh


def forma(ficha):
    from variantes_formas import formas_raza
    index=int(ficha.get('_nativa_forma',0))
    body=int(ficha['cuerpo'])
    # Enlaces explícitos de roster_build: FIDs 181..186 y 166..168.
    keys={27:{0:'base',1:'f_fre_2',2:'f_fre_3',3:'f_fre_final',4:'f_fre_100',5:'f_fre_final'},
          38:{0:'base',1:'f_fre_cooler',2:'f_fre_cooler'}}
    if index not in keys.get(body,{}):raise ValueError('Forma donante Arcosiana desconocida')
    key=keys[body][index]
    if key!='base' and key not in formas_raza(3):raise ValueError('Forma no permitida para la raza')
    return key


def piezas(ficha,juego=None):
    sel=seleccion(ficha)
    if not sel:return []
    form=forma(ficha)
    return [recurso('base',form=form,juego=juego),
            *[recurso(s,k,form,juego) for s,k in sel.items()],
            recurso('mano_L',form=form,juego=juego),recurso('mano_R',form=form,juego=juego)]


def construir_formas(afs,fid,ficha,juego,resumen,construir):
    """Enlaces explícitos al donante; las alternativas mecánicas conservan arte propio."""
    import roster_build as rb
    import variantes_formas as vf
    body=int(ficha['cuerpo'])
    if int(ficha.get('donante',body))!=body:return []
    source=rb.IDS[body]
    costume=min(max(int(ficha.get('traje',0)),0),source['costumes']-1)
    model_ids=source['models'][costume*source['models_per_costume']:]
    links=[('f_fre_2',1,model_ids[1]),('f_fre_3',2,model_ids[2]),
           ('f_fre_final',3,model_ids[3]),('f_fre_100',4,model_ids[4])] if body==27 else [('f_fre_cooler',1,model_ids[1])]
    def montar(raw,actual,root,container):
        index=next(i for key,i,_ in links if key==actual['_forma_arcosiana'])
        return construir(raw,dict(actual,_nativa_forma=index),root,container)
    # El contrato común comprueba permisos y variantes antes de montar.
    # El índice se pasa en la copia, no se cambia la ficha persistente.
    out=[]
    for key,index,model_fid in links:
        current=dict(ficha,_forma_arcosiana=key)
        variants={ident:{key:ident} for ident in (ficha.get('b3_'+s,-1) for s in ('torso','piernas','guantes','botas')) if ident>=0}
        out.extend(vf.construir_formas(afs,current,juego,resumen,montar,3,[(key,index,model_fid)],variants))
    # B3 exige sus índices alternativos para el moveset. No son nuevas formas
    # orgánicas del catálogo: reutilizan nuestra versión final/acorazada.
    alternate=5 if body==27 else 2
    data,report=construir(afs.entry(model_ids[alternate]),dict(ficha,_nativa_forma=alternate,pelo=0,cara=0),juego,afs)
    out.append((data,alternate))
    resumen['alternativa_arcosiana']=dict(indice_donante=alternate,fid=model_ids[alternate],
                                         arte=forma(dict(ficha,_nativa_forma=alternate)),piezas=report.get('piezas_b3',{}))
    return out

"""Compara los clips observados, sin aprobar acciones ausentes ni huecos de RAM."""
import ast
from collections import defaultdict
import json
from pathlib import Path
import re
import statistics
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
OUT = Path('D:/tmp/solIWN')


def entero(texto, constantes):
    nodo = ast.parse(texto.strip(), mode='eval').body
    def valor(n):
        if isinstance(n, ast.Constant) and isinstance(n.value, int): return n.value
        if isinstance(n, ast.Name): return constantes[n.id]
        if isinstance(n, ast.BinOp) and isinstance(n.op, ast.Add): return valor(n.left)+valor(n.right)
        raise ValueError(texto)
    return valor(nodo)


def alias():
    h = (ROOT/'src/anim_iw.h').read_text(encoding='utf-8')
    c = (ROOT/'src/anim_iw.cpp').read_text(encoding='utf-8')
    constantes = {}
    for texto in (h, c[:c.index('constexpr Extra kExtras[]')]):
        texto = re.sub(r'//[^\n]*', '', texto)
        for nombre, numero in re.findall(r'\b(k\w+)\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)\b', texto):
            constantes[nombre] = int(numero, 0)
    salida = defaultdict(list)
    for cod in range(0x19, 0x1F): salida[cod].append((915, cod))
    salida[0xE2].append((925, 4))
    extras = c.split('constexpr Extra kExtras[]', 1)[1].split('};', 1)[0]
    extras = re.sub(r'//[^\n]*', '', extras)
    for contenido in re.findall(r'\{([^{}]+)\}', extras):
        trozos = contenido.split(',')
        codigo, banco = [entero(t, constantes) for t in trozos[:2]]
        como = entero(trozos[2], constantes) if len(trozos)>2 else codigo
        salida[como].append((banco,codigo))
    rangos = c.split('static constexpr Rango r[]', 1)[1].split('};',1)[0]
    rangos = re.sub(r'//[^\n]*', '', rangos)
    rr = [[int(n.strip(),0) for n in r.split(',')] for r in re.findall(r'\{([^{}]+)\}',rangos)]
    qte = c.split('constexpr QteAnim kQte[]', 1)[1].split('};',1)[0]
    qte = re.sub(r'//[^\n]*','',qte)
    for banco,codigo in re.findall(r'\{\s*(\d+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\}',qte):
        banco,codigo = int(banco),int(codigo,0)
        for b,desde,ancho,desplaza in rr:
            if b==banco and desde<=codigo<desde+ancho:
                salida[0xB00+desplaza+codigo-desde].append((banco,codigo))
                break
        else: raise ValueError(('QTE sin alias',banco,hex(codigo)))
    salida[0xF20].append((915,0x57A))
    return dict(salida)


def analizar(datos, mapa, original):
    actores = []
    for rol in (0,1):
        clips = {}
        anterior = None
        for fila in datos['filas']:
            a = fila['actores'][rol]
            if not a.get('codigo_disponible',True):continue
            c = int(a['c'],0)
            fuentes = [(None,c)] if original else mapa.get(c,[(None,c)])
            fuente = fuentes[0]
            clave = f'{fuente[0]}/{fuente[1]:X}'
            d = clips.setdefault(clave,dict(codigo=hex(fuente[1]),banco=fuente[0],
                alias=a['c'],duraciones=[],acciones=[],muestras=0,velocidades_n=[],velocidades_s=[],congelados=0))
            d['muestras'] += 1
            for k,v in [('duraciones',a['dur']),('acciones',fila['accion'])]:
                if v not in d[k]:d[k].append(v)
            if anterior:
                n0,a0,t0 = anterior
                dn,df,dt = fila['n']-n0,a['f']-a0['f'],fila['t']-t0
                if a0['c']==a['c'] and dn>0 and dt>0:
                    if df>0:
                        d['velocidades_n'].append(df/dn)
                        d['velocidades_s'].append(df/dt)
                    elif df==0:d['congelados']+=1
            anterior = fila['n'],a,fila['t']
        for d in clips.values():
            for k in ('velocidades_n','velocidades_s'):
                valores=d.pop(k)
                d[k+'_mediana']=round(statistics.median(valores),4) if valores else None
        actores.append(clips)
    return actores


def bancos(mapa):
    """Oraculo del disco: no infiere un clip por el numero de su alias."""
    ruta=Path(r'D:\DBZ3HD\ps2_games\Infinite World (USA)\USR\DATA_CMN.AFS')
    with ruta.open('rb') as f:
        assert f.read(4)==b'AFS\0'
        n=struct.unpack('<I',f.read(4))[0]
        tabla=struct.unpack('<'+str(n*2)+'I',f.read(n*8))
        entradas={}
        for banco in {b for vs in mapa.values() for b,_ in vs}|{204}:
            off,tam=tabla[2*banco:2*banco+2]
            f.seek(off);entradas[banco]=f.read(tam)
    u=lambda b,o:struct.unpack_from('<I',b,o)[0]
    def hijo(b,i):
        assert b[:4]==b'#AMB' and i<u(b,0x10)
        off,tam=struct.unpack_from('<II',b,u(b,0x14)+16*i)
        return b[off:off+tam]
    global_=hijo(entradas[204],0)
    filas=[]
    for cod,fuentes in mapa.items():
        assert len(set(fuentes))==1,(cod,fuentes)
        banco,original=fuentes[0]
        bsk=hijo(entradas[banco],3 if banco in (204,915) else 0)
        assert bsk[:4]==b'#BSK' and original<u(bsk,0x10),(banco,original)
        blk=u(bsk,u(bsk,0x14)+4*original)
        if not blk:continue
        indice,almacen=struct.unpack_from('<HH',bsk,blk)
        amm=hijo(entradas[banco],0) if banco==915 else global_ if almacen==0 else hijo(entradas[banco],1)
        assert amm[:4]==b'#AMM' and indice<u(amm,0x10),(banco,original,indice)
        dur=u(amm,u(amm,0x14)+16*indice+8)
        alias_presente=cod<u(bsk,0x10) and bool(u(bsk,u(bsk,0x14)+4*cod))
        filas.append(dict(alias=hex(cod),banco=banco,original=hex(original),indice=indice,
                          almacen=almacen,duracion=dur,alias_presente_en_original=alias_presente))
    (OUT/'bancos.json').write_text(json.dumps(dict(fuente=str(ruta),clips=filas),indent=1),encoding='utf-8')
    print('Disco:',len(filas),'clips resueltos;',sum(not v['alias_presente_en_original'] for v in filas),
          'alias sin entrada original (necesitan traduccion).')


def main():
    mapa=alias()
    if '--bancos' in sys.argv:bancos(mapa)
    colisiones={hex(c):v for c,v in mapa.items() if len(set(v))>1}
    resultado=dict(colisiones=colisiones,minijuegos={})
    for clave in range(0x7D0,0x7E2):
        o,p = OUT/f'original_{clave:X}.json',OUT/f'port_{clave:X}.json'
        d={}
        if o.exists():d['original']=analizar(json.loads(o.read_text()),mapa,True)
        if p.exists():d['port']=analizar(json.loads(p.read_text()),mapa,False)
        if not d:continue
        comunes=[]
        if 'original' in d and 'port' in d:
            for rol in (0,1):
                for port in d['port'][rol].values():
                    orig=d['original'][rol].get('None/'+port['codigo'][2:].upper())
                    if orig and any(orig['duraciones']):
                        comunes.append(dict(rol=rol,codigo=port['codigo'],alias=port['alias'],
                            dur_original=orig['duraciones'],dur_port=port['duraciones'],
                            coincide_duracion=orig['duraciones']==port['duraciones'],
                            avance_original=orig['velocidades_n_mediana'],avance_port=port['velocidades_n_mediana']))
        d['clips_comunes']=comunes
        resultado['minijuegos'][hex(clave)]=d
        print(hex(clave),'O',sum(len(a) for a in d.get('original',[])),
              'P',sum(len(a) for a in d.get('port',[])), 'comunes',len(comunes),
              'dur_distintas',sum(not c['coincide_duracion'] for c in comunes))
        if comunes:
            print('  comunes:',', '.join(f"{c['rol']}:{c['codigo']}({c['dur_original']}/{c['dur_port']};{c['avance_original']}/{c['avance_port']})" for c in comunes))
            for rol in (0,1):
                usados={v['codigo'] for v in d['port'][rol].values()}
                faltan=[v['codigo'] for v in d['original'][rol].values() if v['codigo'] not in usados and any(v['duraciones'])]
                if faltan:print('  solo original rol',rol,':',','.join(faltan))
    (OUT/'resumen.json').write_text(json.dumps(resultado,indent=1),encoding='utf-8')
    print('Alias distintos:',len(mapa),'colisiones:',colisiones)


if __name__=='__main__':main()

"""Lectura pura del ancla y filtro de EU299A70; no modifica la RAM original."""
import math
import struct


def leer(read):
    def raw(a, n):
        b = read(a, n)
        if len(b) != n:
            raise ValueError('Lectura de seguimiento incompleta')
        return b

    def u(a):
        return struct.unpack('<I', raw(a, 4))[0]

    def ptr(a):
        if not 0x100000 <= a < 0x2000000:
            raise ValueError('Puntero de seguimiento fuera de EE')
        return a

    def fs(a, n=3):
        v = list(struct.unpack('<' + 'f' * n, raw(a, 4 * n)))
        if not all(math.isfinite(x) for x in v):
            raise ValueError('Seguimiento original no finito')
        return v

    contador = u(0x4A4D40)
    mgr, cam, work = [ptr(u(a)) for a in (0x49CA70, 0x49CBBC, 0x49CBC0)]
    actor = mgr + 0x800
    descriptor = ptr(u(actor + 0xBB8))
    d = dict(n=contador, actor=actor, camara=cam, work=work,
        estado=struct.unpack('<H', raw(actor + 0xCBC, 2))[0],
        posicion=fs(actor + 0x40), ancla640=fs(actor + 0x640),
        altura_descriptor=fs(descriptor + 0xC, 1)[0],
        modo_ancla=u(work + 0x1C8), seguido=fs(work + 0x10),
        direccion=fs(work + 0x40), ojo=fs(cam + 0x80),
        objetivo=fs(cam + 0x90), camara_animada=u(cam + 0x12C))
    if contador != u(0x4A4D40):
        raise ValueError('La lectura de seguimiento cruza campos PAL')
    return d


def recuperar_estados(path):
    """Adquiere RAM retenida y autentica todos los pares VSync, sin nueva partida."""
    import hashlib
    import io
    import json
    import zipfile
    from iwx_reloj_original import auditar_serie
    auditoria=auditar_serie(path)
    filas=[json.loads(x) for x in path.read_text(encoding='utf-8').splitlines()]
    for r in filas:
        from pathlib import Path
        raw=Path(r['video']['ruta']).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=r['video']['sha256']:
            raise ValueError('El estado cambio desde su auditoria')
        with zipfile.ZipFile(io.BytesIO(raw)) as z:
            ram=z.read('eeMemory.bin')
        r['seguimiento']=leer(lambda a,n:ram[a:a+n])
    return filas,auditoria


def analizar(filas,coeficiente=.3):
    """Una hipotesis de fase por pareja; no alinea trayectorias diferentes."""
    if not filas or not all(r['estable'] and r['seguimiento']['n']==r['n'] for r in filas):
        raise ValueError('Serie de seguimiento vacia/inestable')
    if coeficiente not in (.12,.3):
        raise ValueError('Coeficiente fuera de los dos observados en EU299A70')
    eje='campo_video' if all('campo_video' in r for r in filas) else 'n'
    if not all(b[eje]==a[eje]+1 for a,b in zip(filas,filas[1:])):
        raise ValueError('Serie de seguimiento con huecos')
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    pares=[]
    for a,b in zip(filas,filas[1:]):
        sa,sb=a['seguimiento'],b['seguimiento']
        if sa['camara_animada'] or sb['camara_animada']:
            continue
        errores={}
        for fase,s in [('anterior',sa),('actual',sb)]:
            for modo in ('selector','pies'):
                destino=list(s['ancla640'] if modo=='selector' and s['modo_ancla'] else s['posicion'])
                if modo=='pies' or not s['modo_ancla']:
                    destino[1]=f32(destino[1]+s['altura_descriptor'])
                pred=[f32(p+f32(f32(t-p)*f32(coeficiente))) for p,t in zip(sa['seguido'],destino)]
                errores[fase+'_'+modo]=max(abs(x-y) for x,y in zip(pred,sb['seguido']))
        pares.append(dict(n=b['n'],estado=sb['estado'],modo_ancla=sb['modo_ancla'],
            error=errores,seguido=sb['seguido'],ancla=sb['ancla640'],posicion=sb['posicion']))
    if not pares:
        raise ValueError('No hay parejas de seguimiento sin camara animada')
    estados=sorted(set(r['estado'] for r in pares))
    resumen={str(e):dict(pares=sum(r['estado']==e for r in pares),
        maximos={k:max(r['error'][k] for r in pares if r['estado']==e) for k in pares[0]['error']})
        for e in estados}
    return dict(muestras=len(filas),pares=len(pares),por_estado=resumen,filas=pares,
        coeficiente=coeficiente,contador_temporal=eje,
        paridad_port=False,limite='Replay de una actualizacion contra RAM PS2; no valida el port ni su trayectoria.')


def fixture(filas,path,coeficiente=.3):
    """Entrada al mismo C++ del candidato, en su espacio de altura (Y menos 9)."""
    analizar(filas,coeficiente)
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    pares=[]
    def destino(s):
        v=list(s['ancla640'] if s['modo_ancla'] else s['posicion'])
        if not s['modo_ancla']:
            v[1]=f32(v[1]+s['altura_descriptor'])
        v[1]=f32(v[1]-9)
        return v
    def seguido(s):
        return [s['seguido'][0],f32(s['seguido'][1]-9),s['seguido'][2]]
    for a,b in zip(filas,filas[1:]):
        sa,sb=a['seguimiento'],b['seguimiento']
        if not sa['camara_animada'] and not sb['camara_animada']:
            pares.append(destino(sa)+destino(sb)+seguido(sa)+seguido(sb))
    with path.open('xb') as f:
        f.write(struct.pack('<I',len(pares)))
        for p in pares:
            if not all(math.isfinite(x) for x in p):
                raise ValueError('Fixture no finita')
            f.write(struct.pack('<12f',*p))
    return len(pares)


def analizar_port(path,coeficiente=.3,referencia_anterior=True):
    import csv
    with path.open(encoding='utf-8',newline='') as f:
        filas=list(csv.DictReader(f))
    if len(filas)<2 or any(None in r or any(v is None or not math.isfinite(float(v)) for v in r.values()) for r in filas):
        raise ValueError('Seguimiento port vacio/incompleto/no finito')
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    def v(r,nombre):return [float(r[nombre+k]) for k in ('x','y','z')]
    errores=[]
    for a,b in zip(filas,filas[1:]):
        if int(b['tick'])!=int(a['tick'])+1:
            raise ValueError('Seguimiento port con huecos')
        esperado=v(b,'actual') if int(b['reiniciar']) or not referencia_anterior else v(a,'actual')
        if max(abs(x-y) for x,y in zip(v(b,'usada'),esperado))>.001:
            raise ValueError('El port no consume la fase de referencia declarada')
        pred=[f32(p+f32(f32(t-p)*f32(coeficiente))) for p,t in zip(v(b,'antes'),v(b,'usada'))]
        errores.append(max(abs(x-y) for x,y in zip(pred,v(b,'despues'))))
    movido=max(math.dist(v(filas[0],'actual'),v(r,'actual')) for r in filas)
    if max(errores)>.001 or movido<4 or not any(int(r['estado'])==3 for r in filas) or not any(int(r['estado'])==4 for r in filas):
        raise ValueError('No se midio filtro/carrera/frenada y desplazamiento')
    return dict(filas=len(filas),max_error_filtro=max(errores),desplazamiento_referencia=movido,
        estados=sorted(set(int(r['estado']) for r in filas)),referencia_anterior=referencia_anterior,
        coeficiente=coeficiente,
        paridad_global=False,normal_activado=False,
        limite='Comprueba orden y filtro en el port; las trayectorias de actor distintas no se igualan por ordinal.')


def main():
    import argparse
    import hashlib
    import json
    from pathlib import Path
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serie',type=Path,required=True)
    p.add_argument('--salida',type=Path,required=True)
    p.add_argument('--fixture',type=Path,help='Fixture binaria nueva para el C++ de produccion')
    p.add_argument('--estados',action='store_true',help='Recuperar seguimiento desde los estados VSync conservados')
    p.add_argument('--coeficiente',type=float,choices=(.12,.3),default=.3)
    a=p.parse_args()
    raw=a.serie.read_bytes()
    filas=[json.loads(l) for l in raw.decode('utf-8').splitlines()]
    auditoria=None
    if a.estados:
        filas,auditoria=recuperar_estados(a.serie)
    elif any('campo_video' in r for r in filas):
        from iwx_reloj_original import auditar_serie
        auditoria=auditar_serie(a.serie)
    r=analizar(filas,a.coeficiente)
    if auditoria:
        r['auditoria_video']={k:auditoria[k] for k in ('estados_autenticados','pares_vsync_verificados','anomalias_libpad')}
    if a.fixture:
        r['pares_fixture']=fixture(filas,a.fixture,a.coeficiente)
    r.update(serie=str(a.serie),sha256=hashlib.sha256(raw).hexdigest())
    with a.salida.open('x',encoding='utf-8') as f:
        json.dump(r,f,indent=1,ensure_ascii=False,allow_nan=False)
    print(json.dumps({k:v for k,v in r.items() if k!='filas'},indent=1))


if __name__=='__main__':
    main()

"""Lectura pura del rival 7D8 y su tabla de huesos/impactos EU autenticada."""
import math
import struct


def esqueleto(read, actor, *, hd=False):
    """Identidad de los 22 roles corporales; indices de hueso nunca transferidos."""
    endian='>' if hd else '<'
    def raw(a,n):
        low,high=(0x10000000,0xF0000000) if hd else (0x100000,0x2000000)
        if not low<=a<=high-n:raise ValueError('Esqueleto fuera de RAM')
        b=read(a,n)
        if b is None or len(b)!=n:raise ValueError('Esqueleto incompleto')
        return b
    def u(a):return struct.unpack(endian+'I',raw(a,4))[0]
    cuerpo=0x650 if hd else 0x640
    modelo=u(actor+cuerpo+0x538);tabla=u(actor+cuerpo+0x53C)
    cabecera=u(modelo+4);magic=raw(cabecera,5)
    if magic[:4] in (b'#AMG',b'!AMG',b'#AWG',b'!AWG'):
        n=u(cabecera+0x10);nombres=u(cabecera+0x1C)
    elif magic.startswith(b'#AWO' if hd else b'#AMO'):
        n=u(cabecera+0x20);nombres=u(cabecera+0x24)
    else:
        raise ValueError(f'Cabecera corporal inesperada: {magic!r}')
    if nombres<0x100000:nombres+=cabecera
    if not 1<=n<=512:raise ValueError('Cantidad de huesos fuera de rango')
    etiquetas=[raw(nombres+i*32,32).split(b'\0',1)[0].decode('ascii') for i in range(n)]
    indices=struct.unpack(endian+'22h',raw(tabla,44));matrices=u(modelo+0x408)
    matriz=struct.unpack(endian+'16f',raw(actor,64))
    roles=[]
    for rol,indice in enumerate(indices):
        if not 0<=indice<n:raise ValueError('Rol corporal sin hueso valido')
        p=struct.unpack(endian+'3f',raw(matrices+indice*64+48,12))
        mundo=[sum(p[j]*matriz[4*j+i] for j in range(3))+matriz[12+i] for i in range(3)]
        if not all(math.isfinite(x) for x in mundo):raise ValueError('Hueso no finito')
        roles.append(dict(rol=rol,indice=indice,nombre=etiquetas[indice],mundo=mundo))
    return dict(actor=hex(actor),modelo=hex(modelo),cabecera=hex(cabecera),
        tabla=hex(tabla),cantidad=n,roles=roles)


def leer(read):
    def bloque(a,n):
        if not 0x100000<=a<=0x2000000-n:
            raise ValueError(f'Lectura Piccolo fuera de RAM {a:X}/{n}')
        b=read(a,n)
        if len(b)!=n:raise ValueError('Lectura Piccolo incompleta')
        return b
    def u(a):return struct.unpack('<I',bloque(a,4))[0]
    def f(a):
        v=struct.unpack('<f',bloque(a,4))[0]
        if not math.isfinite(v):raise ValueError('Float Piccolo no finito')
        return v
    n=u(0x4A4D40);actor=u(0x49CA70)+0x28D0
    estado,previo,veces,_,_=struct.unpack('<5H',bloque(actor+0xCBC,10))
    color=bloque(actor+0x18F4,16)
    intensidad=struct.unpack_from('<f',color)[0]
    seleccionado=u(0x5D9948)
    return dict(n=n,estable=u(0x4A4D40)==n,actor=actor,estado=estado,
        estado_previo=previo,reentradas=veces,codigo=u(actor+0xB60),cuadro=f(actor+0xC68),
        duracion=u(actor+0xC6C),fin=u(actor+0xC74),
        intensidad=intensidad,color_luz=list(color[8:12]),color_niebla=list(color[12:16]),
        seleccionado=seleccionado,
        tabla_indice=(seleccionado-0x499A60)//16 if 0x499A60<=seleccionado<0x499BC0 and (seleccionado-0x499A60)%16==0 else None,
        tabla=bloque(0x499A60,22*16).hex(),
        gestor_pose=dict(modo=u(0x5D994C),rumbo=u(0x5D9958),
            giro=f(0x5D9954),contador=f(0x5D9960)),
        temblor=dict(activo=u(0x5D9968),fase=f(0x5D9970)))


def apuntar(read,rol=1):
    """Observa camara/hueso y propone teclas publicas; nunca escribe RAM."""
    def u(a):return struct.unpack('<I',read(a,4))[0]
    def v(a,n=3):return struct.unpack('<'+'f'*n,read(a,n*4))
    # EU2ED7D0/2ED7E0: controlador del visor, distinto de la camara de campo49CBBC.
    actor=u(0x49CA70)+0x28D0;camara=u(0x49CD6C)
    if not 0x100000<=camara<0x2000000-0x100:raise ValueError('Visor sin controlador')
    tabla=u(actor+0xB7C);modelo=u(actor+0xB78);matrices=u(modelo+0x408)
    indice=struct.unpack('<h',read(tabla+2*rol,2))[0]
    if not 0<=indice<128:raise ValueError('Objetivo sin hueso valido')
    p=v(matrices+64*indice+48);m=v(actor,16)
    mundo=[sum(p[j]*m[4*j+i] for j in range(3))+m[12+i] for i in range(3)]
    ojo=v(camara+0x80);objetivo=v(camara+0x90)
    d=[mundo[i]-ojo[i] for i in range(3)];vista=[objetivo[i]-ojo[i] for i in range(3)]
    # El visor conserva un vector de longitud1: al restar floats puede dar
    # 0.99999. Exigir norma>=1 rechazaba una vista valida durante el giro.
    if not all(math.isfinite(x) for x in d+vista) or sum(x*x for x in vista)<1e-8:
        raise ValueError('Vista del piloto degenerada')
    yaw=lambda q:math.atan2(q[0],-q[2])
    pitch=lambda q:math.atan2(q[1],math.hypot(q[0],q[2]))
    dy=(yaw(d)-yaw(vista)+math.pi)%(2*math.pi)-math.pi;dp=pitch(d)-pitch(vista)
    teclas=[]
    if abs(dy)>.008:teclas.append(0x27 if dy>0 else 0x25)
    if abs(dp)>.008:teclas.append(0x26 if dp>0 else 0x28)
    return dict(rol=rol,ojo=ojo,objetivo=objetivo,hueso=mundo,
        error_yaw=dy,error_pitch=dp,teclas=teclas,apuntado=abs(dy)<.035 and abs(dp)<.035)


def inventario(elf):
    import hashlib
    from iwx_lava import Original
    e=Original(elf)
    raw=e.bytes(0x499A60,22*16)
    filas=[]
    for i in range(22):
        b=raw[i*16:(i+1)*16]
        filas.append(dict(indice=i,hueso=b[0],grupo=b[1],
            codigo=hex(struct.unpack_from('<I',b,4)[0]),radio=struct.unpack_from('<f',b,8)[0],raw=b.hex()))
    tick=list(struct.unpack('<12I',e.bytes(0x499C50,48)))
    funciones=[]
    for ini,fin in ((0x32D150,0x32D2C0),(0x32E350,0x32E450),(0x32E730,0x32E7E0)):
        b=e.bytes(ini,fin-ini)
        funciones.append(dict(inicio=hex(ini),fin_exclusivo=hex(fin),sha256=hashlib.sha256(b).hexdigest()))
    return dict(elf_sha256=hashlib.sha256(e.elf).hexdigest(),tabla_direccion='0x499A60',
        tabla_hex=raw.hex(),tabla=filas,ticks_pose=[hex(t) for t in tick],funciones=funciones)


def analizar(captura,original,con_esqueleto=False):
    import json
    filas=[json.loads(l) for l in captura.read_text(encoding='utf-8').splitlines()]
    assert len(filas)>1 and all(y['campo_video']-x['campo_video']==1 for x,y in zip(filas,filas[1:]))
    transiciones=[]; impactos=[]; inicial=[]; fin=[]
    previo=None
    for i,f in enumerate(filas):
        p=f['piccolo']
        assert p['estable'] and p['n']==f['n']
        assert p['tabla']==original['tabla_hex'],'Tabla RAM distinta del ELF'
        firma=(p['estado'],p['codigo'],p['reentradas'])
        if firma!=previo:
            transiciones.append(dict(i=i,video=f['campo_video'],**p))
            if p['estado']==3:
                b=original['tabla'][p['tabla_indice']]
                assert p['codigo']==int(b['codigo'],16),'Reaccion no coincide con tabla'
                reaccion=dict(i=i,hueso=b['hueso'],grupo=b['grupo'],codigo=b['codigo'],cuadro=p['cuadro'])
                # Cargar una instantanea en estado3 no demuestra otro impacto.
                # Solo una transicion observada despues acredita la entrada.
                (inicial if previo is None else impactos).append(reaccion)
            previo=firma
        if p['fin']:fin.append(dict(i=i,estado=p['estado'],codigo=hex(p['codigo']),cuadro=p['cuadro']))
    resultado=dict(captura=str(captura),campos=len(filas),campos_video_verificados=True,
        transiciones=transiciones,impactos=impactos,reacciones_iniciales=inicial,terminales=fin,
        paridad_port=False,limite='Observacion natural original; no fuerza aciertos, estado ni trayectoria.')
    pilotos=[dict(i=i,**f['piloto_piccolo']) for i,f in enumerate(filas) if 'piloto_piccolo' in f]
    if pilotos:
        resultado['piloto']=dict(disparos=sum(0x55 in p['pulsadas'] for p in pilotos),
            menor_error=min(max(abs(p['error_yaw']),abs(p['error_pitch'])) for p in pilotos),
            muestras=[p for p in pilotos if p['i'] in (1,2,3,5,10,20,30,50,80,len(filas)-1)])
    if con_esqueleto:
        import hashlib,zipfile
        from pathlib import Path
        video=filas[0]['video'];estado=Path(video['ruta']).read_bytes()
        if hashlib.sha256(estado).hexdigest()!=video['sha256']:raise ValueError('Estado modificado')
        import io
        with zipfile.ZipFile(io.BytesIO(estado)) as z:ram=z.read('eeMemory.bin')
        resultado['esqueleto']=esqueleto(lambda a,n:ram[a:a+n],filas[0]['piccolo']['actor'])
        resultado['estado_sha256']=video['sha256']
    return resultado


def main():
    import argparse,json
    from pathlib import Path
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf',type=Path,required=True)
    p.add_argument('--captura',type=Path)
    p.add_argument('--esqueleto',action='store_true',help='Identifica los22roles en el primer estado autenticado')
    p.add_argument('--salida',type=Path,required=True)
    a=p.parse_args()
    d=inventario(a.elf)
    if a.esqueleto and not a.captura:p.error('--esqueleto requiere --captura')
    if a.captura:d['original']=analizar(a.captura,d,a.esqueleto)
    with a.salida.open('x',encoding='utf-8') as f:json.dump(d,f,indent=1,allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida),ticks=d['ticks_pose'],
        esqueleto=d.get('original',{}).get('esqueleto'),
        piloto=d.get('original',{}).get('piloto'),
        impactos=d.get('original',{}).get('impactos'),
        reacciones_iniciales=d.get('original',{}).get('reacciones_iniciales'),
        transiciones=[{k:v for k,v in t.items() if k in ('i','estado','estado_previo','codigo','cuadro','fin')}
            for t in d.get('original',{}).get('transiciones',[])]),indent=1))


if __name__=='__main__':main()

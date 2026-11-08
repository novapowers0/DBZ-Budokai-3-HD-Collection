"""Auditoría de datos originales; no equivale a una comparación en pantalla.

No modifica las copias del jugador. Guarda el censo completo de animaciones,
eventos y pistas AMP fuera del proyecto para continuar las medidas de runtime.
"""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
OUT = Path('D:/tmp/astraIW')
sys.path.insert(0, str(ROOT / 'mod center hd'))
from escenarios_convertir import IsoB2, entrada_afs
from iw_animaciones_N_resumen import alias


def u(b, o):
    return struct.unpack_from('<I', b, o)[0]


def hijo(b, n):
    assert b[:4] == b'#AMB' and n < u(b, 16)
    off, tam = struct.unpack_from('<II', b, u(b, 20) + n * 16)
    assert off + tam <= len(b)
    return b[off:off + tam]


def amp(b):
    assert b[:4] == b'#AMP'
    nombres = [b[u(b, 28)+32*i:u(b, 28)+32*(i+1)].split(b'\0')[0].decode('ascii')
               for i in range(u(b, 24))]
    clips = []
    for k in range(u(b, 16)):
        c = u(b, 20) + k * 16
        pistas = []
        for i, nombre in enumerate(nombres):
            p = u(b, u(b, c+12) + i*4)
            claves = [] if not p else [struct.unpack_from('<hh', b, p+12+j*4)
                                      for j in range(u(b, p+8))]
            assert all(a[0] <= z[0] for a, z in zip(claves, claves[1:]))
            pistas.append(dict(nombre=nombre, claves=claves))
        clips.append(dict(cuadros=u(b, c+8), pistas=pistas))
    return dict(sha256=hashlib.sha256(b).hexdigest(), clips=clips)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    ruta = Path('D:/DBZ3HD/ps2_games/Infinite World (USA)/USR/DATA_CMN.AFS')
    mapa = alias()
    rejillas = {}
    for campo in (884, 897, 900, 903, 906, 909):
        paquete = entrada_afs(ruta, campo)
        amo, mcf = hijo(paquete, 5), hijo(paquete, 0)
        a = u(amo, u(amo, 28))
        ejes = a + u(amo, a+20)
        brazo=u(amo,ejes+0x34)
        grupo=u(amo,a+brazo+4) if brazo else 0
        nombre_raiz=amo[u(amo,36):u(amo,36)+32].split(b'\0')[0].decode('ascii')
        print('Raiz',campo,nombre_raiz,'grupo',grupo,'partes',u(amo,a+grupo) if grupo else 0)
        celdas = []
        for i in range(u(amo, 16)):
            nombre = amo[u(amo, 36)+32*i:u(amo, 36)+32*(i+1)].split(b'\0')[0].decode('ascii')
            m = re.search(r'_([A-J])(\d{1,2})$', nombre)
            if m and 1 <= int(m[2]) <= 25:
                celdas.append(dict(nombre=nombre, ix=ord(m[1])-65, iz=int(m[2])-1,
                                   pos=struct.unpack_from('<3f', amo, ejes+80*i+16)))
        # Node_AssignNamesToGrid arranca los extremos en cero; no en infinito.
        xs, zs = [0.] + [c['pos'][0] for c in celdas], [0.] + [c['pos'][2] for c in celdas]
        ix, iz = max((c['ix'] for c in celdas), default=0), max((c['iz'] for c in celdas), default=0)
        ex = (max(xs)-min(xs))*(1+1/ix) if ix else None
        ez = (max(zs)-min(zs))*(1+1/iz) if iz else None
        w = hijo(entrada_afs(ruta, campo+1), 0)
        puntos = []
        caras = []
        for k in range(u(w, 8) >> 16):
            o = u(w, 12)+k*0x90
            mat = struct.unpack_from('<16f', w, o+0x50)
            for j in range(w[o+0x24]):
                g = u(w, u(w, o+0x28)+j*32+16)
                for t in range(u(w, g) >> 16):
                    cara=[]
                    for v in range(3):
                        p = struct.unpack_from('<3f', w, u(w, g+8+t*16+v*4))
                        mundo=[sum(p[q]*mat[q*4+d] for q in range(3))+mat[12+d] for d in (0,1,2)]
                        cara.append(mundo);puntos.append([mundo[0],mundo[2]])
                    if u(w,u(w,o+0x28)+j*32+20)&7:caras.append(cara)
        limites = [min(p[0] for p in puntos), max(p[0] for p in puntos),
                   min(p[1] for p in puntos), max(p[1] for p in puntos)]
        rejillas[str(campo)] = dict(flags=u(mcf,16), dimensiones=[ix+1,iz+1],
                                    periodo_original=[ex,ez], limites_wcf=limites,
                                    periodo_port=[limites[1]-limites[0],limites[3]-limites[2]], celdas=celdas)
        print('Rejilla', campo, 'original', ex, ez, 'port', rejillas[str(campo)]['periodo_port'])
        def suelo(x,z):
            ys=[]
            for aa,bb,cc in caras:
                if not min(aa[0],bb[0],cc[0])<=x<=max(aa[0],bb[0],cc[0]) or not min(aa[2],bb[2],cc[2])<=z<=max(aa[2],bb[2],cc[2]):continue
                den=(bb[2]-cc[2])*(aa[0]-cc[0])+(cc[0]-bb[0])*(aa[2]-cc[2])
                if abs(den)<1e-8:continue
                v=((bb[2]-cc[2])*(x-cc[0])+(cc[0]-bb[0])*(z-cc[2]))/den
                w_=((cc[2]-aa[2])*(x-cc[0])+(aa[0]-cc[0])*(z-cc[2]))/den
                if min(v,w_,1-v-w_)>=-1e-6:ys.append(v*aa[1]+w_*bb[1]+(1-v-w_)*cc[1])
            return max(ys) if ys else None
        caminos=[]
        if ex and ez:
            for z in range(-int(ez/2)+100,int(ez/2),100):
                a1,b1=suelo(ex/2-12,z),suelo(-ex/2+12,z)
                if a1 is not None and b1 is not None and abs(a1-b1)<.1:caminos.append([round(ex/2-12,3),z,round(a1,3)])
        rejillas[str(campo)]['cruces_x_con_suelo']=caminos
        print('Cruces X',campo,caminos[:4])
    (OUT/'rejillas.json').write_text(json.dumps(rejillas, indent=2), encoding='utf-8')
    inverso = {}
    for a, fuentes in mapa.items():
        for fuente in fuentes:
            inverso.setdefault(fuente, []).append(a)
    bancos = {}
    for banco in sorted({b for b, _ in inverso} | {912}):
        entrada = entrada_afs(ruta, banco)
        bsk = hijo(entrada, 3 if banco in (204, 915) else 0)
        if bsk[:4] != b'#BSK':
            bancos[str(banco)] = dict(error='sin BSK en hijo esperado')
            continue
        codigos = []
        for codigo in range(u(bsk, 16)):
            p = u(bsk, u(bsk, 20)+4*codigo)
            if not p:
                continue
            anim, almacen = struct.unpack_from('<hh', bsk, p)
            eventos = []
            lineas, tabla = struct.unpack_from('<II', bsk, p+40)
            for j in range(lineas):
                gestor, n, off = struct.unpack_from('<HHI', bsk, tabla+j*8)
                if gestor == 7:
                    for e in range(n):
                        cuadro, id_, activo, tipo, valor, cola = struct.unpack_from('<HBBIII', bsk, off+e*16)
                        eventos.append(dict(cuadro=cuadro, id=id_, activo=activo, tipo=tipo, valor=valor, cola=cola))
            codigos.append(dict(codigo=codigo, anim=anim, almacen=almacen,
                                alias=inverso.get((banco, codigo), []), eventos=eventos))
        bancos[str(banco)] = dict(sha256=hashlib.sha256(bsk).hexdigest(), codigos=codigos)
    (OUT/'censo_bsk.json').write_text(json.dumps(bancos, indent=2), encoding='utf-8')
    # Una fila por código presente. Estar cargado NO demuestra que el estado lo pida.
    lineas = ['# Censo completo de bancos IW (datos, no aceptación visual)',
              'Fuente: copia USA; funciones de referencia EU207E90, EU207CE0 y EU23F520.',
              'Sin alias no significa necesariamente falta: un banco incluye variantes de otras misiones.',
              '|Banco|Código|Anim/almacén|Alias port|Eventos (cuadro:tipo:valor)|', '|---|---|---|---|---|']
    for banco, datos in bancos.items():
        for c in datos.get('codigos', []):
            ev = ', '.join(f"{e['cuadro']}:{e['tipo']}:{e['valor']}" for e in c['eventos'])
            lineas.append(f"|{banco}|{c['codigo']:X}|{c['anim']}/{c['almacen']}|"
                          f"{','.join(f'{a:X}' for a in c['alias']) or 'sin alias'}|{ev}|")
    (OUT/'ANIMACIONES.md').write_text('\n'.join(lineas)+'\n', encoding='utf-8')
    b2 = IsoB2(r'D:\DBZ3HD\ps2_games\DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
    escena = b2.entrada(328)
    pistas = {str(n): amp(hijo(escena, n)) for n in (10, 14)}
    (OUT/'amp_original.json').write_text(json.dumps(pistas, indent=2), encoding='utf-8')
    for n, datos in pistas.items():
        print('AMP', n, [(c['cuadros'], len(c['pistas']), sum(len(p['claves']) for p in c['pistas'])) for c in datos['clips']])
        modelo = hijo(escena, 7 if n == '10' else 11)
        amg = u(modelo, u(modelo, 28))
        ejes = amg + u(modelo, amg+20)
        nombres = [modelo[u(modelo,36)+32*i:u(modelo,36)+32*(i+1)].split(b'\0')[0].decode('ascii')
                   for i in range(u(modelo,16))]
        clip = datos['clips'][0]
        por_nombre = {p['nombre']:p['claves'] for p in clip['pistas']}
        firma = 2166136261
        for f in range(clip['cuadros']):
            for nombre in nombres:
                claves = por_nombre.get(nombre, [])
                v = 0
                if claves:
                    # Traduccion literal EU13F0F0 (no bisect del lector C++).
                    k, restante = (0 if f < 1 else len(claves)//2), len(claves)
                    if f >= 1:
                        while True:
                            paso = restante+1
                            restante = paso//2
                            if restante < 2: break
                            delta = paso//4
                            k += -delta if f < claves[k][0] else delta
                        if f < claves[k][0]: k -= 1
                    v = claves[k][1]
                for byte in struct.pack('<h',v):
                    firma = ((firma ^ byte)*16777619) & 0xffffffff
        datos['firma_estados_original'] = firma
        datos['huesos_amo'] = len(nombres)
        datos['billboards'] = [dict(nombre=nombre, flags=u(modelo,ejes+i*80+48))
                               for i,nombre in enumerate(nombres) if u(modelo,ejes+i*80+48)&0xc000]
        print('AMP firma original', n, firma, 'huesos', len(nombres))
        print('AMP billboards', n, len(datos['billboards']))
    (OUT/'amp_original.json').write_text(json.dumps(pistas, indent=2), encoding='utf-8')
    print('BSK:', len(bancos), 'bancos,', sum(len(d.get('codigos', [])) for d in bancos.values()), 'códigos originales')
    print('Alias:', len(mapa), 'colisiones:', sum(len(set(v)) > 1 for v in mapa.values()))


if __name__ == '__main__':
    main()

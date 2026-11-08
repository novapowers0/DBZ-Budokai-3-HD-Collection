"""Mide entradas reales HD en escenarios B2 sin escribir evento, acción ni zona.

Usa una copia fija, reserva y respaldos de pruebas.py. Prepara posiciones,
orientación y vida; el fixture solar opcional repite además una cámara PAL
observada y lo declara en el resultado. Registra memoria durante cada entrada
para no perder estados breves20/22. No acredita fidelidad visual de la transición.
"""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import time

import pruebas as P


def alta(mods, entrada):
    filas = []
    for d in sorted(mods.iterdir(), key=lambda p: p.name):
        if not d.is_dir() or d.name.startswith('.') or d.name.endswith('.disabled') or (d/'.disabled').exists():
            continue
        f = d/'stage.toml'
        if not f.is_file():
            continue
        r = None
        for l in f.read_text(encoding='utf-8-sig').splitlines():
            l = l.split('#',1)[0].strip()
            if l.startswith('['):
                r = {} if l in ('[[escenario]]','[escenario]') else None
                if r is not None:
                    filas.append((d.name,r))
            elif r is not None and '=' in l:
                k,v = map(str.strip,l.split('=',1))
                if k in ('entrada','tema'):
                    r[k] = int(v)
                elif k == 'selector':
                    r[k] = v != 'false'
    filas = [(n,r) for n,r in filas if r.get('entrada',0)>=5000 and 0<=r.get('tema',16)<17][:23]
    filas.sort(key=lambda x:x[1].get('selector') is False or x[0].startswith('Campo IW'))
    return next((17+i for i,(_,r) in enumerate(filas) if r['entrada']==entrada),None)


def pared_original(grupo,altura=9,entrada_spx=None):
    """Lee MAS/MAD y triángulos MDD; mismo layout que src/b2_colision.h.

    Solo elige una preparación de prueba. La colisión que dispara el motor
    sigue siendo la del juego; no se escribe el evento ni su resultado.
    """
    sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'mod center hd'))
    import escenarios_convertir as E
    fuente = E.IsoB2('D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
    try:
        mdd,mad,mas,_ = fuente.piezas(grupo)
    finally:
        fuente.f.close()
    def u(b,p):
        return struct.unpack_from('<I',b,p)[0]
    def vec(p):
        return struct.unpack_from('<3f',mdd,p)
    def resta(a,b):
        return tuple(x-y for x,y in zip(a,b))
    def area(a,b):
        return math.sqrt(sum(v*v for v in (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])))
    ob = u(mdd,u(mdd,32))
    tabla = u(mdd,ob+0xb0)
    refs,visitados = set(),set()
    nodo = u(mdd,ob+0xc8)
    while nodo:
        if nodo in visitados or len(visitados)>=10000:
            raise RuntimeError('árbol de colisión cíclico o demasiado grande')
        visitados.add(nodo)
        refs.update(u(mdd,u(mdd,nodo+0x4c)+4*i) for i in range(u(mdd,nodo+0x30)))
        nodo = u(mdd,nodo+0x44)
    indices = list(range(u(mdd,ob+0x98)))
    preferido = {0:88,7:116,8:15}.get(grupo)
    if preferido in indices:
        indices.remove(preferido)
        indices.insert(0,preferido)
    for k in indices:
        r = tabla+48*k
        if r not in refs:
            continue
        geo = u(mdd,r)
        idx = struct.unpack_from('<i',mdd,geo+16)[0]
        if not 0<=idx<u(mas,4):
            continue
        g = u(mas,u(mas,8)+4*idx)
        entrada = False
        for i in range(u(mas,g+4)):
            q = u(mas,g+8)+16*i
            if u(mas,q)!=1:
                continue
            mg = u(mad,u(mad,8)+4)
            p = u(mad,u(mad,mg+12)+4*u(mas,q+4))
            buscada = entrada_spx+1 if entrada_spx is not None else (2 if grupo==0 else 1)
            entrada |= bool(u(mad,p)&1) and u(mad,p+4)==buscada
        if not entrada:
            continue
        normal = vec(geo)
        if abs(normal[1])>.1:
            continue
        pool = u(mdd,r+28)+u(mdd,u(mdd,r+8)+4)+16
        vertices = [vec(pool+16*u(mdd,geo+20+4*i)) for i in range(3)]
        hit = [sum(v[i] for v in vertices)/3 for i in range(3)]
        hit[1] = altura
        total = area(resta(vertices[1],vertices[0]),resta(vertices[2],vertices[1]))
        partes = sum(area(resta(vertices[i],hit),resta(vertices[(i+1)%3],hit)) for i in range(3))
        if partes<=total+.01:
            return dict(registro=k,normal=normal,punto=hit,entrada=(entrada_spx if entrada_spx is not None else (1 if grupo==0 else 0)))
    raise RuntimeError(f'sin superficie vertical de transición a altura{altura}; necesita otro fixture medido')


def capturar_solar_fijo(j,muestra,capas=False,profundidad=False):
    """Detiene pasos guest por el canal propio; no declara sincronización GPU."""
    k32=ctypes.WinDLL('kernel32',use_last_error=True)
    k32.OpenEventW.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_wchar_p]
    k32.OpenEventW.restype=ctypes.c_void_p
    k32.SetEvent.argtypes=[ctypes.c_void_p]
    k32.CloseHandle.argtypes=[ctypes.c_void_p]
    pedir=k32.OpenEventW(2,False,f'Local\\DBZ3_B2_Solar_Pedir_{j.proc.pid}')
    seguir=k32.OpenEventW(2,False,f'Local\\DBZ3_B2_Solar_Seguir_{j.proc.pid}')
    if not pedir or not seguir:
        if pedir:k32.CloseHandle(pedir)
        if seguir:k32.CloseHandle(seguir)
        raise RuntimeError('canales de foto solar no accesibles')
    try:
        if not k32.SetEvent(pedir):raise RuntimeError('no se pudo pedir la foto solar')
        j.log(r'dbz3 B2 solar foto: UI lista .*json=true',10)
        antes=muestra()
        foto=j.captura('solar_previo')
        despues=muestra()
        shutil.copy2(j.dest/'dbz3_b2_solar_foto_ui.json',j.run/'solar_foto_ui.json')
        pruebas_z=[]
        if profundidad:
            for modo,canal,nombre in ((2,'PruebaZ','pruebaz'),(1,'SinZ','sinz')):
                evento=k32.OpenEventW(2,False,f'Local\\DBZ3_B2_Solar_{canal}_{j.proc.pid}')
                if not evento:raise RuntimeError('canal Z solar no accesible')
                try:
                    if not k32.SetEvent(evento):raise RuntimeError('no se pudo pedir el cambio de Z solar')
                    j.log(rf'dbz3 B2 solar foto: Z modo={modo} estado=2 json=true',5)
                    za=muestra();zf=j.captura(f'solar_{nombre}');zb=muestra()
                    shutil.copy2(j.dest/f'dbz3_b2_solar_foto_{nombre}_ui.json',j.run/f'solar_foto_{nombre}_ui.json')
                    from PIL import Image
                    with Image.open(zf) as im:im.resize((im.width//2,im.height//2)).save(j.run/f'solar_{nombre}_mitad.png')
                    pruebas_z.append(dict(modo=modo,foto=str(zf),antes=za,despues=zb,guest_igual=antes==despues==za==zb))
                finally:k32.CloseHandle(evento)
        contraste = None
        if capas:
            sin = k32.OpenEventW(2,False,f'Local\\DBZ3_B2_Solar_Sin_{j.proc.pid}')
            if not sin:raise RuntimeError('canal de ablación solar no accesible')
            try:
                if not k32.SetEvent(sin):raise RuntimeError('no se pudo pedir la ablación solar')
                j.log(r'dbz3 B2 solar foto: UI lista .*emitidos=0 sin_solar=true json=true',5)
                sin_antes=muestra()
                foto_sin=j.captura('solar_sin_capas')
                sin_despues=muestra()
                shutil.copy2(j.dest/'dbz3_b2_solar_foto_sin_ui.json',j.run/'solar_foto_sin_ui.json')
                contraste=dict(foto_sin=str(foto_sin),antes=sin_antes,despues=sin_despues,
                    guest_igual_en_ambas=antes==despues==sin_antes==sin_despues,
                    alcance='ablación UI de los cinco quads; mismo guest detenido, no equivalencia PAL ni pruebaZ')
                from solb2x_solar import comprobar_par_capas
                contraste['validacion']=comprobar_par_capas(
                    json.loads((j.run/'solar_foto_ui.json').read_bytes()),
                    json.loads((j.run/'solar_foto_sin_ui.json').read_bytes()),
                    contraste['guest_igual_en_ambas'])
                from PIL import Image
                with Image.open(foto_sin) as im:
                    im.resize((im.width//2,im.height//2)).save(j.run/'solar_sin_capas_mitad.png')
            finally:
                k32.CloseHandle(sin)
        return foto,dict(antes=antes,despues=despues,captura_atomica=False,contraste_capas=contraste,pruebas_z=pruebas_z,
            guest_detenido=antes==despues,
            alcance='pasos guest detenidos y solar UI conservado; no sincronización de cola GPU')
    finally:
        k32.SetEvent(seguir)
        k32.CloseHandle(pedir);k32.CloseHandle(seguir)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--copia',default='smoke_c',choices=['smoke_c','smoke_d','smoke_e','smoke_f'])
    ap.add_argument('--distancia',type=float,default=12.0)
    ap.add_argument('--dz-atacante',type=float,default=0.0,help='control: desplaza al atacante en Z mundial (recta del golpe)')
    ap.add_argument('--base-pared',type=float,default=10.0,help='distancia del centro de colisión del segundo actor a la pared; 15 iguala el fixture PAL 27/15')
    ap.add_argument('--altura-colision',type=float,default=9.0)
    ap.add_argument('--entrada',default='L+P 450',help='entrada real; L+P es P+K del juego')
    ap.add_argument('--fotos',default='',help='cuadros de VM para comparar, por ejemplo 30,120,300')
    ap.add_argument('--grupo',type=int,choices=[0,2,4,7,8],default=4)
    ap.add_argument('--spx',type=int,choices=[0,1],help='entrada original elegida por la superficie, sin forzar el evento')
    ap.add_argument('--diagnostico',action='store_true',help='registrar los filtros de la consulta real en el motor')
    ap.add_argument('--huesos',action='store_true',help='traza HD con matrices de huesos (DBZ3_TEST_B2_HUESOS); requiere --actores-1450')
    ap.add_argument('--curva',action='store_true',help='registrar cada paso de VM y la cámara escrita antes del dibujo')
    ap.add_argument('--actores-1450',action='store_true',help='sonda acotada de posiciones/colisión/estados entre VM y dibujo; solo grupos7/8 y --curva')
    ap.add_argument('--actores-completo',action='store_true',help='amplía la sonda 1450 a la transición entera para seguir la trayectoria de la víctima; requiere --actores-1450')
    ap.add_argument('--trayectoria-nativa',action='store_true',help='experimento C1/ACC en la actualización nativa del personaje; solo grupos7/8 y --curva')
    ap.add_argument('--reaccion-pal',action='store_true',help='experimento de física PAL18 durante la VM, sin falsear evento HD; requiere grupos7/8 y --curva')
    ap.add_argument('--luces',action='store_true',help='leer perfil, luces y controles HD antes y después de la entrada')
    ap.add_argument('--solar',action='store_true',help='leer tareas solares propias, matriz y proyección durante la prueba')
    ap.add_argument('--solar-previo',type=int,default=0,help='muestras antes del ataque, sin forzar el solar (0..120)')
    ap.add_argument('--solo-solar',action='store_true',help='preparación terrestre común, sin ataque ni transición; requiere grupo4 y --solar-previo')
    ap.add_argument('--solar-camara',type=Path,help='JSON PAL observado: repetir su vista para aislar el solar; requiere --solo-solar')
    ap.add_argument('--solar-foto-fija',action='store_true',help='detener el siguiente paso guest durante la captura solar; requiere --solo-solar')
    ap.add_argument('--solar-capas',action='store_true',help='segunda captura sin los quads solares, en el mismo guest detenido; requiere --solar-foto-fija')
    ap.add_argument('--solar-z',action='store_true',help='profundidad solar optativa, más ablación sin Z y sonda z=.9995; requiere --solar-capas')
    ap.add_argument('--robot-sin-segunda',action='store_true',help='ablación de laboratorio de la segunda pasada del robot; no aceptación visual')
    ap.add_argument('--robot-constantes',action='store_true',help='observar constantes CPU del robot en el par240; no acredita el shader ejecutado')
    ap.add_argument('--robot-sin-brillo-hd',action='store_true',help='ablación del brillo de borde HD solo en el robot; conserva luz, textura y dos pasadas')
    ap.add_argument('--anim-b2',action='store_true',help='DBZ3_TEST_B2_ANIM_B2: la reaccion B2 mantiene la ultima pose (como PAL) en vez de envolver el clip')
    ap.add_argument('--ik-suelo',action='store_true',help='eB2f DBZ3_TEST_B2_IK_SUELO: IK de suelo PAL (cabeza/manos/pies) tras la animacion; apagado por defecto')
    ap.add_argument('--idle-b2',action='store_true',help='eB2f DBZ3_TEST_B2_IDLE_B2: el atacante usa el reposo B2 (banco1 clip0, 0x20000 de PAL); apagado por defecto')
    ap.add_argument('--mirada',action='store_true',help='eB2g DBZ3_TEST_B2_MIRADA: el atacante gira cuello y cabeza hacia el rival como el PS2 (FUN_001F6D80); apagado por defecto')
    ap.add_argument('--levantar-b2',action='store_true',help='eB2g DBZ3_TEST_B2_LEVANTAR_B2: la victima usa los clips B2 de aterrizaje/levantarse/tumbado (HD 141/166/42); apagado por defecto')
    ap.add_argument('--ataque-b2',action='store_true',help='eB2g DBZ3_TEST_B2_ATAQUE_B2: el golpe del atacante (HD 574) usa el clip B2 98 (PAL 573); apagado por defecto')
    ap.add_argument('--ik-escala',type=float,help='eB2h DBZ3_TEST_B2_IK_ESCALA: umbrales de suelo del IK x k (modelo HD ~1,24 veces el B2); apagado por defecto')
    ap.add_argument('--ik-escala-levantar',type=float,help='eB2h DBZ3_TEST_B2_IK_ESCALA_LEVANTAR: escala del IK en los estados de levantarse (HD 166/172)')
    ap.add_argument('--aterrizaje-dt',type=float,help='eB2h DBZ3_TEST_B2_ATERRIZAJE_DT: desfase (cuadros) del clip de aterrizaje, solo barridos (por defecto -1,4)')
    ap.add_argument('--ik-suelo-fin',action='store_true',help='eB2h DBZ3_TEST_B2_IK_SUELO_FIN: atacante en estados finales con suelo=y')
    ap.add_argument('--sin-ik-b3',action='store_true',help='eB2i DBZ3_TEST_B2_SIN_IK_B3: apaga el IK de suelo nativo B3 (x+8190) en la escena')
    ap.add_argument('--ik-tabla',action='store_true',help='eB2i DBZ3_TEST_B2_IK_TABLA: umbrales de pies/manos del atacante de la tabla PAL (fila 0)')
    ap.add_argument('--capa-b2',action='store_true',help='eB2i DBZ3_TEST_B2_CAPA_B2: capa superpuesta B2 (banco 3) en 1285..1291')
    ap.add_argument('--tronco-fin',type=float,help='eB2h DBZ3_TEST_B2_TRONCO_FIN: grados de giro de STMC (x local) del atacante en el estado 1290')
    ap.add_argument('--hit-sonda',action='store_true',help='registra el escritor de x+208 en la acción 22 y de x+2928 en el aterrizaje; solo lectura')
    ap.add_argument('--aterrizaje-15b2',nargs='?',const='1',choices=['1','2'],help='experimento: salta el aterrizaje nativo y sigue la reacción 20 con clip 137')
    ap.add_argument('--tras-fin',type=float,default=0,help='segundos adicionales de observación tras el fin, de 0 a 5')
    ap.add_argument('--ids',default='0,0',help='dia1B: ids de personaje B3 de P1,P2 (DBZ3_TEST_BATTLE); por defecto Goku contra Goku')
    ap.add_argument('--original-on',default='',help='dia1B: valor de la cvar dbz3_original_on (tokens separados por comas) en el dbz3_user.toml de la copia')
    ap.add_argument('--finalizar',action='store_true',help='esperar fin o bloqueo y comprobar bancos HD')
    a = ap.parse_args()
    fotos = [int(v) for v in a.fotos.split(',') if v]
    if fotos != sorted(set(fotos)) or any(v < 0 or v >= 10000 for v in fotos):
        ap.error('--fotos requiere cuadros únicos, crecientes, de 0 a 9999')
    if os.getenv('DBZ3_AGENTE') not in ('solB2X','nB2','eB2','eB2a','eB2c','eB2d','eB2f','eB2g','eB2h','eB2i','eB2j','dia1B'):
        ap.error('requiere DBZ3_AGENTE=solB2X o nB2')
    if not math.isfinite(a.distancia) or a.distancia<=0:
        ap.error('--distancia debe ser positiva y finita')
    if not math.isfinite(a.base_pared) or a.base_pared<0 or a.base_pared>60:
        ap.error('--base-pared debe estar entre 0 y 60')
    if not math.isfinite(a.altura_colision):
        ap.error('--altura-colision debe ser finita')
    if not math.isfinite(a.tras_fin) or not 0<=a.tras_fin<=5:
        ap.error('--tras-fin debe estar entre 0 y 5 segundos')
    if not 0<=a.solar_previo<=120 or (a.solar_previo and not a.solar):
        ap.error('--solar-previo admite0..120 y requiere --solar')
    if a.solo_solar and (a.grupo!=4 or not a.solar_previo or a.altura_colision!=9 or a.spx is not None or fotos):
        ap.error('--solo-solar requiere grupo4, --solar-previo, altura9, sin --spx ni --fotos')
    if a.solar_foto_fija and not a.solo_solar:
        ap.error('--solar-foto-fija requiere --solo-solar')
    if a.solar_capas and not a.solar_foto_fija:
        ap.error('--solar-capas requiere --solar-foto-fija')
    if a.solar_z and not a.solar_capas:
        ap.error('--solar-z requiere --solar-capas')
    referencia_solar = None
    if a.solar_camara:
        if not a.solo_solar:
            ap.error('--solar-camara requiere --solo-solar')
        from solb2x_solar import camara_solar_original
        referencia_solar = json.loads(a.solar_camara.read_text(encoding='utf-8'))
        camara_solar = camara_solar_original(referencia_solar)
    if a.robot_sin_segunda and (a.grupo!=0 or a.spx!=0):
        ap.error('--robot-sin-segunda requiere --grupo 0 --spx 0')
    if a.actores_1450 and (not a.curva or (a.grupo not in (7,8) and not a.actores_completo)):
        ap.error('--actores-1450 requiere --curva; grupos 0/2/4 solo con --actores-completo')
    if a.actores_completo and not a.actores_1450:
        ap.error('--actores-completo requiere --actores-1450')
    if a.trayectoria_nativa and (a.grupo not in (0,4,7,8) or not a.curva):
        ap.error('--trayectoria-nativa requiere grupo7/8 y --curva')
    if a.reaccion_pal and (a.grupo not in (0,4,7,8) or not a.curva):
        ap.error('--reaccion-pal requiere grupo7/8 y --curva')
    if a.robot_constantes and (a.grupo!=0 or a.spx!=0):
        ap.error('--robot-constantes requiere --grupo 0 --spx 0')
    if a.robot_sin_brillo_hd and (a.grupo!=0 or a.spx!=0):
        ap.error('--robot-sin-brillo-hd requiere --grupo 0 --spx 0')
    pared = pared_original(a.grupo,a.altura_colision,a.spx) if a.grupo!=4 or a.spx is not None else None
    nombre_mod = {0:'Base B2',2:'Kami B2',4:'Llanura B2',7:'Nieve B2',8:'Ciudad B2'}[a.grupo]
    j = P.Juego(a.copia,f'solB2X_lanzamientos_grupo{a.grupo}')
    escrituras = 'solo posiciones/orientación/vida de preparación'
    if referencia_solar is not None:
        escrituras += '; cámara observada PAL repetida por el fixture de laboratorio'
    datos = dict(grupo=a.grupo,escrituras=escrituras,casos=[],
                 lanzamiento_observado=False,inicio_real=False,resultado_vm=None,restauracion_bancos=None)
    datos['robot_sin_segunda_solicitado']=a.robot_sin_segunda
    datos['robot_constantes_solicitado']=a.robot_constantes
    datos['robot_sin_brillo_hd_solicitado']=a.robot_sin_brillo_hd
    datos['actores_1450_solicitado']=a.actores_1450
    datos['actores_completo_solicitado']=a.actores_completo
    datos['base_pared']=a.base_pared
    datos['trayectoria_nativa_solicitada']=a.trayectoria_nativa
    datos['reaccion_pal_solicitada']=a.reaccion_pal
    datos['hit_sonda_solicitada']=a.hit_sonda
    datos['solar_capas_solicitadas']=a.solar_capas
    datos['solar_z_solicitado']=a.solar_z
    with P.copia_en_uso(j.dest):
        P.comprobar_libre(j.dest)
        P.respaldar(j.dest)
        P.preparar(j.dest)
        cf = j.dest/'dbz3_user.toml'
        config = cf.read_bytes()
        archivos = [p for p in (j.dest/'mods').glob('*/stage.toml')
                    if re.search(rf'(?m)^entrada\s*=\s*{5000+a.grupo}\b',p.read_text(encoding='utf-8-sig'))]
        if len(archivos)!=1:
            raise RuntimeError('se requiere exactamente un pack B2 del grupo')
        nombre_mod = archivos[0].parent.name
        marker = archivos[0].parent/'.disabled'
        marker_backup = j.run/'marker_disabled'
        curva = j.dest/'dbz3_b2_traza.jsonl'
        h = None
        try:
            if a.curva and curva.exists():
                shutil.move(curva,j.run/'traza_anterior.jsonl')
            if marker.exists():
                shutil.move(marker,marker_backup)
            stage = alta(j.dest/'mods',5000+a.grupo)
            if stage is None:
                raise RuntimeError('alta B2 ausente')
            text = re.sub(r'(?m)^dbz3_combate_prueba_muneco\s*=.*\n?','',cf.read_text(encoding='utf-8'))
            text=re.sub(r'(?m)^dbz3_original_on\s*=.*\n?','',text)
            cf.write_text(text+'\ndbz3_combate_prueba_muneco = "quieto"\n'+('dbz3_original_on = "%s"\n'%a.original_on if a.original_on else ''),encoding='utf-8')
            env = dict(DBZ3_TEST_BATTLE=f'{a.ids},{stage}',DBZ3_TEST_ESCENA_VOLCADO='1',
                       DBZ3_TEST_B2_MOTOR='1',DBZ3_COMBATE_MUNECO='quieto')
            env.update({k:v for k,v in os.environ.items() if k.startswith('DBZ3_TEST_B2_')})   # eB2i: barridos (p. ej. ATAQUE_DT) sin argumento propio
            if a.diagnostico:
                env['DBZ3_TEST_B2_CONSULTA'] = '1'
            if a.curva:
                env['DBZ3_TEST_B2_CURVA'] = '1'
            if a.actores_1450:
                env['DBZ3_TEST_B2_ACTORES_1450'] = '1'
            if a.actores_completo:
                env['DBZ3_TEST_B2_ACTORES_COMPLETO'] = '1'
            if a.trayectoria_nativa:
                env['DBZ3_TEST_B2_TRAYECTORIA_NATIVA'] = '1'
            if a.reaccion_pal:
                env['DBZ3_TEST_B2_REACCION_PAL'] = '1'
            if a.robot_sin_segunda:
                env['DBZ3_TEST_B2_ROBOT_SIN_SEGUNDA'] = '1'
            if a.robot_constantes:
                env['DBZ3_TEST_B2_ROBOT_CONSTANTES'] = '1'
            if a.robot_sin_brillo_hd:
                env['DBZ3_TEST_B2_ROBOT_SIN_BRILLO'] = '1'
            if a.hit_sonda:
                env['DBZ3_TEST_B2_HIT_SONDA'] = '1'
            if a.aterrizaje_15b2:
                env['DBZ3_TEST_B2_ATERRIZAJE_15B2'] = a.aterrizaje_15b2
            if a.solar_foto_fija:
                env['DBZ3_TEST_B2_SOLAR_FOTO']='1'
                anterior=j.dest/'dbz3_b2_solar_foto_ui.json'
                if anterior.exists():shutil.move(anterior,j.run/'solar_foto_ui_anterior.json')
            if a.solar_capas:
                env['DBZ3_TEST_B2_SOLAR_CAPAS']='1'
                anterior=j.dest/'dbz3_b2_solar_foto_sin_ui.json'
                if anterior.exists():shutil.move(anterior,j.run/'solar_foto_sin_ui_anterior.json')
            if a.solar_z:
                env['DBZ3_TEST_B2_SOLAR_Z']='1'
                for nombre in ('sinz','pruebaz'):
                    anterior=j.dest/f'dbz3_b2_solar_foto_{nombre}_ui.json'
                    if anterior.exists():shutil.move(anterior,j.run/f'solar_foto_{nombre}_anterior.json')
            if referencia_solar is not None:
                ruta_camara = j.run/'camara_solar_pal.txt'
                ruta_camara.write_text(' '.join(format(x,'.17g') for x in camara_solar),encoding='ascii')
                env['DBZ3_TEST_B2_SOLAR_CAMARA'] = str(ruta_camara)
                datos['referencia_solar'] = dict(ruta=str(a.solar_camara.resolve()),
                    sha256=hashlib.sha256(a.solar_camara.read_bytes()).hexdigest(),
                    campo=referencia_solar['campo'],camara_repetida=camara_solar,
                    alcance='solo solar con vista observada; no escena completa ni reloj equiparado')
            if a.huesos:
                env['DBZ3_TEST_B2_HUESOS'] = '1'
            if a.anim_b2:
                env['DBZ3_TEST_B2_ANIM_B2'] = '1'
            if a.ik_suelo:
                env['DBZ3_TEST_B2_IK_SUELO'] = '1'
            if a.idle_b2:
                env['DBZ3_TEST_B2_IDLE_B2'] = '1'
            if a.levantar_b2:
                env['DBZ3_TEST_B2_LEVANTAR_B2'] = '1'
            if a.ataque_b2:
                env['DBZ3_TEST_B2_ATAQUE_B2'] = '1'
            if a.ik_escala:
                env['DBZ3_TEST_B2_IK_ESCALA'] = str(a.ik_escala)
            if a.ik_escala_levantar:
                env['DBZ3_TEST_B2_IK_ESCALA_LEVANTAR'] = str(a.ik_escala_levantar)
            if a.aterrizaje_dt is not None:
                env['DBZ3_TEST_B2_ATERRIZAJE_DT'] = str(a.aterrizaje_dt)
            if a.ik_suelo_fin:
                env['DBZ3_TEST_B2_IK_SUELO_FIN'] = '1'
            if a.sin_ik_b3:
                env['DBZ3_TEST_B2_SIN_IK_B3'] = '1'
            if a.ik_tabla:
                env['DBZ3_TEST_B2_IK_TABLA'] = '1'
            if a.capa_b2:
                env['DBZ3_TEST_B2_CAPA_B2'] = '1'
            if a.tronco_fin:
                env['DBZ3_TEST_B2_TRONCO_FIN'] = str(a.tronco_fin)
            if a.mirada:
                env['DBZ3_TEST_B2_MIRADA'] = '1'
            if fotos:
                env['DBZ3_TEST_B2_FOTOS'] = a.fotos
            j.lanzar(env)
            j.arrancar(120,rapido=True)
            P._esperar_pelea(j,90)
            if not re.search(rf'alta {stage} = .*\(mod {re.escape(nombre_mod)}, entrada {5000+a.grupo}\)',j.texto_log()):
                raise RuntimeError('la numeración de altas no coincide con la memoria esperada')
            def leer(addr,n):
                b = P.leer(j.h,P.GBASE+addr,n)
                if b is None or len(b)!=n:
                    raise RuntimeError(f'sin memoria {addr:X}')
                return b
            def u(addr):
                return struct.unpack('>I',leer(addr,4))[0]
            if pared:
                zdd = u(0x823a3d80+32)
                ob = u(u(zdd+32)+4*u(0x823a3d80+64))
                registros = sorted({pared['registro'],86,88}) if a.grupo==0 else [pared['registro']]
                seleccion = []
                for k in registros:
                    if k>=u(ob+0x98):
                        continue
                    r = u(ob+0xb0)+48*k
                    bone,obj = u(r+24),u(r+32)
                    item = dict(registro=k,bone=bone,objeto=obj,cabecera=u(r+16))
                    if bone>=0x10000000 and obj>=0x10000000:
                        indice,lineas = u(bone),u(bone+4)
                        if indice<=512 and lineas>=0x10000000:
                            estado = u(obj+8*(indice+1))
                            item['estado'] = estado
                            if estado<=4096:
                                cab = u(lineas+16*estado+12) or u(lineas+12)
                                item.update(cabecera_activa=cab,seleccionada=cab==item['cabecera'])
                    seleccion.append(item)
                datos['seleccion_colision_antes'] = seleccion
            mgr = u(0x823d6cf4)
            def bancos():
                resultado = []
                for i in range(2):
                    controlador = mgr+2096+8240*i+0x650
                    acm = u(u(controlador+0x538)+0x46c)
                    resultado.append(dict(acm=acm,n=u(acm+16),tabla=u(acm+20),csk=u(controlador+0x2c0)))
                return resultado
            datos['bancos_antes'] = bancos()
            if os.getenv('EB2G_WATCH'):
                # eB2g: vigia de escritura sobre el id de clip del cuerpo (mod+0x41C) de ambos actores (como eANI2 con la rodilla)
                import threading
                src = (Path(__file__).resolve().parents[1]/'studies 5/ENTREGAS/A3b/herramientas/watch_camara.py').read_text(encoding='utf-8')
                ns = {'__name__': 'watchlib'}
                exec(compile(src[:src.index('def _esc(j, par):')], 'watch_camara', 'exec'), ns)
                dirs = [ns['G'] + u(mgr+2096+8240*i+2952) + 1052 for i in range(2)]
                threading.Thread(target=ns['vigilar'], args=(j, j.proc.pid, dirs, float(os.getenv('EB2G_WATCH')), 300), daemon=True).start()
            if os.getenv('EB2I_WATCH'):
                # eB2i: vigia de escritura sobre el cuaternio local (pose A, mod+0x42C) y la matriz de STMC del atacante, armado al entrar en
                # EB2I_WATCH_CODIGO (defecto 1290): quien gira el STMC en HD
                import threading, time as _t
                src = (Path(__file__).resolve().parents[1]/'studies 5/ENTREGAS/A3b/herramientas/watch_camara.py').read_text(encoding='utf-8')
                ns = {'__name__': 'watchlib'}
                exec(compile(src[:src.index('def _esc(j, par):')], 'watch_camara', 'exec'), ns)
                def _vig_stmc():
                    hp = ns['K'].OpenProcess(0x1F0FFF, False, j.proc.pid)
                    g = lambda ad: ns['guest_u32'](hp, ad)
                    x0 = mgr+2096; cod = int(os.getenv('EB2I_WATCH_CODIGO', '1290'))
                    t0 = _t.time()
                    while _t.time()-t0 < 600 and g(x0+2928) != cod: _t.sleep(0.01)
                    mod = g(x0+2952); idx = int(os.getenv('EB2I_WATCH_HUESO', '0'))
                    dirs = [ns['G']+g(mod+0x42c)+0x40*idx, ns['G']+g(mod+0x408)+0x40*idx]
                    j.info('eB2i vigia: codigo %d hueso %d pose %X mats %X' % (cod, idx, g(mod+0x42c), g(mod+0x408)))
                    ns['vigilar'](j, j.proc.pid, dirs, float(os.getenv('EB2I_WATCH')), 300)
                threading.Thread(target=_vig_stmc, daemon=True).start()
            if a.luces:
                from solb2x_robot_luces import luces_hd
                datos['luces_antes'] = luces_hd(leer, con_controles=True)
            h = P.K32.OpenProcess(0x0438,False,j.proc.pid)
            if not h:
                raise RuntimeError('no se pudo abrir el proceso para preparar posiciones')
            def write(addr,b):
                n = ctypes.c_size_t()
                if not P.K32.WriteProcessMemory(ctypes.c_void_p(h),ctypes.c_void_p(P.GBASE+P.DESPL+addr),b,len(b),ctypes.byref(n)) or n.value!=len(b):
                    raise RuntimeError('falló preparación de posiciones')
            def csk_clip(csk,code):
                # Entrada BE de la tabla CSK del actor y clip del bloque; None si no es puntero.
                try:
                    if not 0x10000000<=csk<0xf0000000:return None
                    blk=u(csk+32+4*code)
                    if not 0x10000000<=blk<0xf0000000:return None
                    return struct.unpack('>H',leer(blk,2))[0]
                except Exception:
                    return None
            def muestra():
                acts = []
                for i in range(2):
                    x = mgr+2096+8240*i
                    csk=u(x+0x910)
                    acts.append(dict(accion=struct.unpack('>H',leer(x+3244,2))[0],codigo=u(x+2928),
                        csk=csk,clip154=csk_clip(csk,154),clip152=csk_clip(csk,152),
                        clip160=csk_clip(csk,160),clip137=csk_clip(csk,137),clip44=csk_clip(csk,44),
                        selector=struct.unpack('>b',leer(x+6208,1))[0],
                        habilita_lanzamiento=struct.unpack('>b',leer(x+8184,1))[0],
                         tiempo_reaccion=struct.unpack('>f',leer(x+3256,4))[0],
                         tiempo_lanzamiento=struct.unpack('>f',leer(x+6196,4))[0],
                        umbral_reaccion=struct.unpack('>f',leer(x+6192,4))[0],
                        cuadro_anim=struct.unpack('>f',leer(x+3160,4))[0],
                        pos=struct.unpack('>3f',leer(x+64,12)),
                        posicion_colision=struct.unpack('>3f',leer(x+1616,12)),
                        vida=struct.unpack('>f',leer(x+6080,4))[0],
                        velocidad=struct.unpack('>3f',leer(x+208,12))))
                r = dict(cuadro=u(0x8238d6b4),zona=u(0x823a3dc0),actores=acts,
                         permiso_consultas=leer(mgr+35566,1)[0])
                if a.solar:
                    from solb2x_solar import solar_hd
                    r['solar'] = solar_hd(leer,mgr)
                return r
            # K es R3 en los binds del SDK, NO el botón de energía del juego.
            # CSK/CCM de Goku: P+K -> 0x23E -> AP1 HR65 (tipo2/codigo3).
            # Los P/K individuales ya medidos dan HR0/16 (tipo0/codigo7).
            for nombre,teclas in [('entrada_dirigida',a.entrada)]:
                posiciones = ((-200+a.distancia,0,-31),(-200,0,-31))
                if a.solo_solar:
                    from solb2x_solar import posiciones_solar
                    posiciones=posiciones_solar()
                if pared:
                    datos['pared'] = pared
                    hit,normal = pared['punto'],pared['normal']
                    antes = muestra()['actores']
                    posiciones = [tuple(hit[q]+normal[q]*(a.base_pared+(a.distancia if i==0 else 0))-
                                        (antes[i]['posicion_colision'][q]-antes[i]['pos'][q]) for q in range(3)) for i in range(2)]
                posiciones = [(p[0],p[1],p[2]+(a.dz_atacante if i==0 else 0)) for i,p in enumerate(posiciones)]
                for i,pos in enumerate(posiciones):
                    x = mgr+2096+8240*i
                    for off in (48,64):
                        write(x+off,struct.pack('>3f',*pos))
                    write(x+6080,struct.pack('>f',10000.0))
                    dx,dz = (posiciones[1-i][q]-pos[q] for q in (0,2))
                    write(x+6052,struct.pack('>I',int(math.atan2(dx,dz)*65536/(2*math.pi))&65535))
                if a.solar_previo:
                    previas=[];solar_fotografiado=False
                    for _ in range(a.solar_previo):
                        r=muestra();previas.append(r)
                        visible=any(t['estado'][9]>=.95 and t['estado'][10]>=0 and t['estado'][6]>0 and
                                    0<=t['estado'][4]<=512 and 0<=t['estado'][5]<=512 and
                                    t['quads_generados']>0 for t in r['solar']['tareas'])
                        if referencia_solar is not None:
                            m=r['solar']['matriz_hd'];original=referencia_solar['solar']['matriz']
                            error=max(abs(m[i]*(-1 if i%4 in (1,2) else 1)-original[i]) for i in range(16))
                            r['error_matriz_pal']=error
                            visible=visible and error<.001 and all(t['dibujos']>=8 for t in r['solar']['tareas'])
                        if visible and not solar_fotografiado:
                            if a.solar_foto_fija:
                                foto,datos['solar_previo_foto']=capturar_solar_fijo(j,muestra,a.solar_capas,a.solar_z)
                                if not datos['solar_previo_foto']['guest_detenido']:
                                    raise RuntimeError('la captura solar no conservó el estado guest')
                            else:
                                foto=j.captura('solar_previo')
                                datos['solar_previo_foto']=dict(antes=r,despues=muestra(),captura_atomica=False)
                            solar_fotografiado=True
                            if P.Image:
                                with P.Image.open(foto) as im:
                                    im.resize((im.width//2,im.height//2)).save(j.run/'solar_previo_mitad.png')
                        time.sleep(1/60)
                    datos['solar_previo']=previas
                    print('solar previo',len(previas),'muestras; visible',solar_fotografiado,flush=True)
                    if a.solo_solar:
                        datos['solo_solar']=True
                        datos['bancos_despues']=bancos()
                        datos['restauracion_bancos']=datos['bancos_antes']==datos['bancos_despues']
                        return 0 if solar_fotografiado and datos['restauracion_bancos'] else 4
                caso = dict(nombre=nombre,entrada=teclas,distancia=a.distancia,muestras=[muestra()])
                datos['casos'].append(caso)
                j.tecla(teclas,0)
                # Ventana finita de adquisición; no es un sondeo de espera.
                for _ in range(360):
                    caso['muestras'].append(muestra())
                    time.sleep(1/60)
                estados = sorted(set(q['actores'][1]['accion'] for q in caso['muestras']))
                print(nombre,'reacciones',estados,'vida final',caso['muestras'][-1]['actores'][1]['vida'],flush=True)
                log = j.texto_log()
                if a.luces:
                    datos['luces_despues_entrada'] = luces_hd(leer, con_controles=True)
                if any(v in (20,22) for v in estados) or 'B2 HD: inicio por colision real' in log or 'B2 HD: inicio bloqueado:' in log:
                    datos['lanzamiento_observado'] = any(v in (20,22) for v in estados)
                    datos['inicio_real'] = 'B2 HD: inicio por colision real' in log
                    if fotos and datos['inicio_real']:
                        k32 = ctypes.WinDLL('kernel32',use_last_error=True)
                        k32.OpenEventW.argtypes = [ctypes.c_uint32,ctypes.c_int,ctypes.c_wchar_p]
                        k32.OpenEventW.restype = ctypes.c_void_p
                        k32.SetEvent.argtypes = [ctypes.c_void_p]
                        k32.CloseHandle.argtypes = [ctypes.c_void_p]
                        for cuadro in fotos:
                            j.log(rf'dbz3 B2 foto: detenida cuadro={cuadro} |dbz3 B2 HD: (VM bloqueada|guion finalizado)',30)
                            if re.search(r'dbz3 B2 HD: (VM bloqueada|guion finalizado)',j.texto_log()):
                                break
                            evento = k32.OpenEventW(2,False,f'Local\\DBZ3_GP1_B2_Foto_{j.proc.pid}')
                            if not evento:
                                raise RuntimeError('evento de foto no accesible')
                            try:
                                if a.robot_constantes:
                                    from solb2x_robot_materiales import capturar
                                    material = capturar(leer,j.texto_log(),cuadro)
                                    with (j.run/f'robot_materiales_{cuadro}.json').open('x',encoding='utf-8') as f:
                                        json.dump(material,f,ensure_ascii=False,indent=2)
                                foto = j.captura(f'cuadro_{cuadro}')
                                if P.Image:
                                    with P.Image.open(foto) as im:
                                        im.resize((im.width//2,im.height//2)).save(j.run/f'cuadro_{cuadro}_mitad.png')
                                shutil.copy2(j.dest/f'dbz3_b2_foto_{cuadro}.json',j.run/f'cuadro_{cuadro}.json')
                            finally:
                                k32.SetEvent(evento)
                                k32.CloseHandle(evento)
                    if (fotos or a.finalizar) and datos['inicio_real']:
                        j.log(r'dbz3 B2 HD: (VM bloqueada|guion finalizado)',30)
                        terminal = re.search(r'dbz3 B2 HD: (VM bloqueada|guion finalizado)([^\r\n]*)',j.texto_log())
                        datos['resultado_vm'] = terminal.group(0) if terminal else None
                        datos['bancos_despues'] = bancos()
                        datos['restauracion_bancos'] = datos['bancos_antes'] == datos['bancos_despues']
                        datos['estado_final'] = muestra()
                        print('restauracion bancos',datos['restauracion_bancos'],flush=True)
                        if not datos['restauracion_bancos']:
                            raise RuntimeError('los bancos HD no volvieron a su estado original')
                        if a.tras_fin:
                            time.sleep(a.tras_fin)
                            datos['tras_fin_espera_seg'] = a.tras_fin
                            datos['estado_tras_fin'] = muestra()
                            foto = j.captura('tras_fin')
                            if P.Image:
                                with P.Image.open(foto) as im:
                                    im.resize((im.width//2,im.height//2)).save(j.run/'tras_fin_mitad.png')
                    break
        finally:
            if h:
                P.K32.CloseHandle(ctypes.c_void_p(h))
            (j.run/'medida.json').write_text(json.dumps(datos,indent=2),encoding='utf-8')
            (j.run/'dbz3.log').write_text(j.texto_log(),encoding='utf-8')
            j.cerrar()
            if not j.vivo():
                if a.curva and curva.exists():
                    shutil.move(curva,j.run/'traza_hd.jsonl')
                if marker_backup.exists():
                    shutil.move(marker_backup,marker)
                shutil.move(cf,j.run/'ajustes_usados.toml')
                cf.write_bytes(config)
            print('evidencia',j.run,flush=True)
    # Salida0 solo acredita la adquisición: la fidelidad requiere el original.
    # Una preparación sin inicio o una VM bloqueada deben distinguirse también
    # por código de salida para que otro arnés no las confunda con éxito.
    if not datos['inicio_real']:
        return 2
    if datos['resultado_vm'] and 'VM bloqueada' in datos['resultado_vm']:
        return 3
    return 0


if __name__ == '__main__':
    sys.exit(main())

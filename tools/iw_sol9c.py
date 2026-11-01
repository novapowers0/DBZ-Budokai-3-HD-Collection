"""Arnes IW9c: evidencia aislada, ejecutables fijados y registro de comandos."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import iw_sol9 as I
import pcsx2_cuadros as C

I.BASE = Path('D:/tmp/solIW9c')
I.FUENTES += ('iw_ap_eventos.h','iw_eventos.h','iw_adx.h')


def banco9c(nombre):
    p=I.BASE/nombre
    return p if p.exists() else I.RESPALDO/('IW9c_snapshot_'+nombre)


I.banco=banco9c


def archivar9c():
    import shutil
    activos=sorted((p for p in I.BASE.iterdir() if p.is_dir() and (p/'identidad.json').exists() and (p/'dbz3.exe').exists()),
                   key=lambda p:p.stat().st_ctime,reverse=True)
    for p in activos[5:]:
        dst=I.RESPALDO/('IW9c_snapshot_'+p.name);assert not dst.exists()
        shutil.move(str(p),str(dst));print('Instantanea conservada',dst,flush=True)


def ap_campo(fase):
    import re
    from medir_nb0063 import P,foto
    os.environ['DBZ3_PRUEBAS_EXE']=str(I.banco(fase)/'dbz3.exe')
    def env(_):
        e=P.resolver_escena('iw:mini:0x7D0')[0]
        e.update(DBZ3_TEST_IWMINI='0x7D0',DBZ3_IW_EVENTOS_LOG='1')
        return e
    def jugar(j,_):
        j.log('entrada acabada: mando al jugador',180)
        P.dormir(2)
        j.tecla('W 4000',4500)
        j.tecla('S 3500',4000)
        texto=j.texto_log()
        campos=re.findall(r'AP campo9c: mini (\d+) material (\d+) valor (\d+) -> AME (-?\d+) sonido938 (-?\d+)',texto)
        audios=re.findall(r'AP audio IW: DATA_CMN.AFS entrada 938 sonido (\d+) -> VAG (\d+) PCM (\d+)',texto)
        if fase!='antes':
            assert campos,'tipo6 sin consumidor real'
            assert any(int(c[3])>=0 for c in campos),'sin emisiones AME'
            assert audios and all(int(c[2])>0 for c in audios),'pasos sin PCM938'
        result=dict(campos=campos,audios=audios,limite='consumo real AP y PCM; no igualdad visual de particulas/RNG global')
        (j.run/'ap9c_campo.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        foto(j,'ap_campo')
    nombre='ap9c_'+fase
    P.ESCENARIOS[nombre]=('AP6 IW sobre superficie real',env,jugar)
    r=P.correr('sol',nombre,['DBZ3_TEST_OCULTO=0','DBZ3_TEST_TURBO=1'],None,{'ventana':'960x720','timeout':'360'},True)
    (I.BASE/(nombre+'.json')).write_text(json.dumps(r,indent=2,default=str),encoding='utf-8')
    print(P.tabla([r]),flush=True)
    return int(not r['ok'])


def final_campo(fase,clave,pasos=False,exito=False,continuo=False):
    import struct
    from medir_nb0063 import P,foto
    os.environ['DBZ3_PRUEBAS_EXE']=str(I.banco(fase)/'dbz3.exe')
    P.OVERRIDES['dbz3_aspect']='"stretch"';P.OVERRIDES['dbz3_low_res']='false'
    req=Path('D:/DBZ3HD/sol/iw9c_campo_edad.req')
    clips={'0x7D0':(0x52,0x53),'0x7D1':(0x119,0x11a),'0x7D2':(0x17d,0x17e),
           '0x7D9':(0x49d,0x49e),'0x7DD':(0x62d,0x62e),'0x7DF':(0x6f5,0x6f6)}
    codigo=hex(clips[clave][0 if exito else 1])
    def env(_):
        e=P.resolver_escena('iw:mini:'+clave)[0]
        e.update(DBZ3_TEST_IWMINI=clave+(',auto' if exito else ''),DBZ3_TEST_IWMINI_CAMLOG='1')
        if not exito:e['DBZ3_SOLIW6_FINAL']='2'
        if pasos:e['DBZ3_SOLIW9C_CAMPO_PASOS']='1'
        return e
    def jugar(j,_):
        if pasos:C.fijar_reloj(req,0)
        j.log('iwmini: final de IW',180)
        inicio=time.monotonic();muestras=[]
        def cuerpo():
            mgr=struct.unpack('>I',P.leer(j.h,P.GBASE+0x823D6CF4,4))[0]
            raw=P.leer(j.h,P.GBASE+mgr+2096+0x650+0x520,0xf0)
            return dict(estado=struct.unpack('>I',P.leer(j.h,P.GBASE+mgr+35128,4))[0],
                codigo=hex(struct.unpack_from('>I',raw)[0]),cuadro=struct.unpack_from('>f',raw,0xe8)[0],
                cuadros=struct.unpack_from('>I',raw,0xec)[0])
        fin=time.monotonic()+10
        while time.monotonic()<fin:
            m=cuerpo()
            if m['codigo']==codigo and m['cuadro']>=12:
                muestras.append(dict(foto='primera_pose',antes=m));foto(j,'primera_pose')
                muestras[-1]['despues']=cuerpo();break
            time.sleep(.002)
        else:raise AssertionError('sin primera pose53')
        if continuo:
            j.log('IW campo9c: AMA450 dibujada con conteo420 retenido',20)
            m=cuerpo();assert m['estado']==4 and m['codigo']==codigo and m['cuadro']>=m['cuadros']-2,m
            foto(j,'panel450_conteo420');muestras.append(dict(foto='panel450_conteo420',cuerpo=m,conteo=420,ama_minima=450))
            assert 'tabla confirmada' not in j.texto_log()
            j.tecla('Space',120);j.log('IW campo9c: tabla confirmada edad 420.0',5)
        elif pasos:
            j.log('IW campo9c: tabla sobre escena al terminar pose',12)
            j.log('IW campo9c: tabla detenida edad 0 ',6)
            for edad in (90,320,420):
                C.fijar_reloj(req,edad)
                j.log('IW campo9c: tabla detenida edad '+str(edad)+' ',10)
                P.dormir(.15)
                m=cuerpo();assert m['estado']==4 and m['codigo']==codigo and m['cuadro']>=m['cuadros']-2,m
                nombre='panel'+str(edad).zfill(3);foto(j,nombre)
                muestras.append(dict(foto=nombre,edad=edad,cuerpo=m))
            P.dormir(1.5);assert 'tabla confirmada' not in j.texto_log(),'no retuvo420'
            j.tecla('Space',120);j.log('IW campo9c: tabla confirmada edad 420.0',5)
        else:
            P.dormir(max(0,inicio+4.4-time.monotonic()))
            from PIL import Image,ImageStat
            p=j.captura('sin_tabla_4s')
            with Image.open(p) as im:
                negra=ImageStat.Stat(im.convert('L')).mean[0]
                im.resize((im.width//2,im.height//2)).save(j.run/'sin_tabla_4s_05.png')
            muestras.append(dict(foto='sin_tabla_4s',cuerpo=cuerpo(),segundos=time.monotonic()-inicio))
            muestras[-1]['luminancia']=negra
        (j.run/'final_campo9c.json').write_text(json.dumps(dict(muestras=muestras,
            limite='primera pose muestreada asincrona; edades de panel detenidas en consumidor si pasos'),indent=2),encoding='utf-8')
    nombre='final9c_'+fase+'_'+clave+('_exito' if exito else '')
    P.ESCENARIOS[nombre]=('Campo: pose y tabla sobre escena de IW',env,jugar)
    try:
        r=P.correr('sol',nombre,['DBZ3_TEST_OCULTO=0','DBZ3_TEST_TURBO=1'],None,{'ventana':'960x720','timeout':'360'},True)
    finally:
        if pasos and req.exists():
            import shutil
            shutil.move(str(req),str(I.RESPALDO/('campo9c_edad_'+str(time.time_ns())+'.req')))
    (I.BASE/(nombre+'_'+str(time.time_ns())+'.json')).write_text(json.dumps(r,indent=2,default=str),encoding='utf-8')
    print(P.tabla([r]),flush=True);return int(not r['ok'])


def pose_campo(fase,edad):
    import struct
    from medir_nb0063 import P,foto
    os.environ['DBZ3_PRUEBAS_EXE']=str(I.banco(fase)/'dbz3.exe')
    P.OVERRIDES['dbz3_aspect']='"stretch"';P.OVERRIDES['dbz3_low_res']='false'
    def env(_):
        e=P.resolver_escena('iw:mini:0x7D0')[0]
        e.update(DBZ3_TEST_IWMINI='0x7D0',DBZ3_SOLIW6_FINAL='2',DBZ3_SOLIW9C_POSE=str(edad))
        return e
    def jugar(j,_):
        leer=lambda a,n:P.leer(j.h,P.GBASE+a,n)
        u=lambda a:struct.unpack('>I',leer(a,4))[0]
        P._esperar_pelea(j,180)
        fin=time.monotonic()+100
        while time.monotonic()<fin:
            mgr=u(0x823D6CF4);x=mgr+2096
            if u(x+0x650+0x520)==0x53:
                actual=struct.unpack('>f',leer(x+0x650+0x608,4))[0]
                if actual>=edad:break
            time.sleep(.003)
        else:raise AssertionError('sin pose53 en la edad solicitada')
        assert actual==edad,('no se rebobina: el evaluador cachea claves',actual,edad)
        # Sonda sobre una copia desechable: velocidad nativa A+64C=0,
        # reloj A+608=edad. 820FA4B8 multiplica el delta por64C; el gancho
        # de la pose sujeta conserva un evaluador que no avanza (no suma1).
        # Permite medir tambien el ejecutable ANTES sin parchearlo.
        import ctypes
        h=P.K32.OpenProcess(0x0438,False,j.proc.pid);assert h
        P.K32.WriteProcessMemory.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
        def escribir(ad,valor):
            raw=ctypes.create_string_buffer(struct.pack('>f',valor));n=ctypes.c_size_t()
            assert P.K32.WriteProcessMemory(h,P.GBASE+ad,raw,4,ctypes.byref(n)) and n.value==4
        try:
            for body in (0x650,0xcd0):
                if u(x+body+0x520)==0x53:
                    escribir(x+body+0x64c,0)
        finally:P.K32.CloseHandle(h)
        P.dormir(.2)
        f=lambda a,n:struct.unpack('>'+str(n)+'f',leer(a,4*n))
        cuadro=f(x+0x650+0x608,1)[0]
        assert abs(cuadro-edad)<.01,(cuadro,edad)
        d=dict(codigo=hex(u(x+0x650+0x520)),cuadro=cuadro,
               transform=f(x,16),puntos=[f(x+1616+16*i,4) for i in range(16)],
               vista=f(mgr+96,16),fov=f(mgr+0x1c4,1)[0])
        foto(j,'pose'+str(edad))
        (j.run/'pose9c.json').write_text(json.dumps(d,indent=2),encoding='utf-8')
        original=json.loads((I.BASE/'pose_original.json').read_text(encoding='utf-8'))
        key={10.8:'fracaso10',12:'fracaso10',60:'fracaso50',120:'fracaso100',180:'fracaso150'}.get(edad)
        if key:
            import numpy as np
            org=original[key];pp=np.array(d['puntos'])[:,:3];po=np.array(org['puntos'])[:,:3]
            diff=np.linalg.norm(pp-po,axis=1)
            d['comparacion']=dict(original=key,error_por_punto=diff.tolist(),maximo=float(diff.max()),media=float(diff.mean()))
            (j.run/'pose9c.json').write_text(json.dumps(d,indent=2),encoding='utf-8')
            print('PUNTOS',edad,d['comparacion'],flush=True)
    nombre='pose9c_'+fase+'_'+str(edad)
    P.ESCENARIOS[nombre]=('Pose53 detenida: posiciones contra RAM original',env,jugar)
    r=P.correr('sol',nombre,['DBZ3_TEST_OCULTO=0','DBZ3_TEST_TURBO=1'],None,{'ventana':'960x720','timeout':'300'},True)
    (I.BASE/(nombre+'.json')).write_text(json.dumps(r,indent=2,default=str),encoding='utf-8')
    print(P.tabla([r]),flush=True);return int(not r['ok'])


def ap7(fase,antes=False):
    import re
    from medir_nb0063 import P
    os.environ['DBZ3_PRUEBAS_EXE']=str(I.banco(fase)/'dbz3.exe')
    def env(_):
        e=P.resolver_escena('iw:mini:0x7D6')[0]
        e.update(DBZ3_TEST_IWMINI='0x7D6',DBZ3_IW_EVENTOS_LOG='1')
        return e
    def jugar(j,_):
        if antes:j.log(r'AP IW pendiente:.*tipo 7 ',180)
        else:j.log(r'AP7 ADX9c:.*PCM [1-9][0-9]* Hz',180)
        P.dormir(1)
        text=j.texto_log()
        voces=re.findall(r'AP7 ADX9c: CMN(\d+) valor (\d+) -> (ADX_[A-Z]+.AFS) entrada (\d+) PCM (\d+) Hz (\d+)',text)
        pendientes=re.findall(r'AP IW pendiente:.*tipo 7 valor (\d+)',text)
        if not antes:assert voces and all(int(v[4])>0 for v in voces)
        result=dict(antes=antes,voces=voces,pendientes=pendientes,limite='consumidor real y PCM; audio silenciado, sin certificacion waveOut/SPU2')
        (j.run/'ap7_runtime.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    nombre='ap7_9c_'+fase
    P.ESCENARIOS[nombre]=('Voz AP7 de la intro IW, sin audio audible',env,jugar)
    r=P.correr('sol',nombre,['DBZ3_TEST_OCULTO=0','DBZ3_TEST_TURBO=1'],None,{'ventana':'960x720','timeout':'360'},True)
    (I.BASE/(nombre+'.json')).write_text(json.dumps(r,indent=2,default=str),encoding='utf-8')
    print(P.tabla([r]),flush=True);return int(not r['ok'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('accion', choices=['fijar', 'visual', 'verificar','ap','pares','ama','final','medidas','pose','ap7','auditar_cierre'])
    ap.add_argument('fase')
    ap.add_argument('--clave', default='0x7D0')
    ap.add_argument('--pasos',action='store_true')
    ap.add_argument('--exito',action='store_true')
    ap.add_argument('--continuo',action='store_true')
    ap.add_argument('--edad',type=float,default=12)
    ap.add_argument('--antes',action='store_true')
    a = ap.parse_args()
    assert I.BASE.is_dir()
    if a.accion=='auditar_cierre':
        b=I.banco(a.fase);identidad=json.loads((b/'identidad.json').read_text(encoding='utf-8'))
        fuentes={n:I.sha(I.ROOT/'src'/n)==identidad[n] for n in I.FUENTES}
        exe={n:I.sha(I.ROOT/'out/build'/n/'dbz3.exe')==identidad['dbz3.exe'] for n in ('win-amd64-dual','win-amd64-release')}
        dlls={}
        for build in ('win-amd64-dual','win-amd64-release'):
            for dll in ('rexruntime.dll','rexgpu-xenos.dll','amd_fidelityfx_dx12.dll'):
                dlls[build+'/'+dll]=I.sha(I.ROOT/'out/build'/build/dll)==I.sha(I.ROOT/'rexglue-sdk-0.10/out/win-amd64-baseline'/dll)
        todo=json.loads((I.BASE/(a.fase+'_todo.json')).read_text(encoding='utf-8'))
        campo_run=json.loads((I.BASE/('iw9_visual_'+a.fase+'_0x7D0.json')).read_text(encoding='utf-8'))
        campo=json.loads((Path(campo_run['run'])/'medidas9.json').read_text(encoding='utf-8'))
        activas=[p.name for p in I.BASE.iterdir() if p.is_dir() and (p/'identidad.json').exists() and (p/'dbz3.exe').exists()]
        resultado=dict(fase=a.fase,sha256=identidad['dbz3.exe'],fuentes=fuentes,exe=exe,dlls=dlls,todo=todo,
            campo=campo,instantaneas_activas=activas,
            ap=json.loads((I.BASE/'ap9c_verificado.json').read_text(encoding='utf-8')),
            adx=json.loads((I.BASE/'adx9c_verificado.json').read_text(encoding='utf-8')),
            pose=json.loads((I.BASE/'pose_oraculo9c.json').read_text(encoding='utf-8')),
            pendientes=['AP0/4/5/8/9, transicion de forma y sintetizador SPU2 completo',
                        '34/109 eventos de SE del corpus sin PCM;7 silencios originales se cuentan aparte',
                        'campo FAIL; medida final integra en campo; umbrales0.45/18 intactos'],
            corridas_no_validas=['original_campo_044014: error de identificacion DISPLAY2 resuelto, VM propia cerrada',
                'pose port054151/054549: rebobinar reloj no reinicia caches; excluidas',
                'original_pose_055304: estado corrio al terminal antes de pausar; excluida',
                'original_pose_064908: solicitud2E desde estado30 no consumida; excluida',
                'primer arnes ap_antes omitio DBZ3_PS2_GAMES; corregido y repetido',
                'primer arnes negro rechazaba la captura precisamente por estar negra; corregido y repetido'])
        (I.BASE/'cierre_verificado9c.json').write_text(json.dumps(resultado,indent=2),encoding='utf-8')
        print(json.dumps(dict(sha256=resultado['sha256'],fuentes=fuentes,exe=exe,dlls=dlls,todo=todo),indent=2))
        assert all(dlls.values()) and todo['codigo']==0 and len(activas)<=5
        return 0
    if a.accion=='ama':
        I.auditar();return 0
    if a.accion=='final':
        return final_campo(a.fase,a.clave,a.pasos,a.exito,a.continuo)
    if a.accion=='pose':
        return pose_campo(a.fase,a.edad)
    if a.accion=='ap7':
        return ap7(a.fase,a.antes)
    if a.accion=='medidas':
        from PIL import Image,ImageStat
        import numpy as np
        from skimage.metrics import structural_similarity
        import comparar
        originales=I.BASE/'original_campo_044234'
        ms=sorted(I.BASE.glob('final9c_'+a.fase+'_0x7D0_*.json'),key=lambda p:p.stat().st_mtime)
        corridas=[json.loads(p.read_text(encoding='utf-8')) for p in ms]
        corrida=next(r for r in reversed(corridas) if r['ok'])
        run=Path(corrida['run']);datos=[]
        for edad in (90,320,420):
            nombre='panel'+str(edad).zfill(3)
            org=originales/(nombre+'_05.png');port=run/(nombre+'_05.png')
            a0=Image.open(org).convert('RGB').resize((320,240))
            b0=Image.open(port).convert('RGB').resize((320,240))
            s=float(structural_similarity(np.asarray(a0.convert('L')),np.asarray(b0.convert('L')),data_range=255))
            ph=comparar.hamming(comparar.phash(a0),comparar.phash(b0))
            datos.append(dict(edad=edad,original=str(org),port=str(port),ssim=s,phash=ph,verde=s>=.45 and ph<=18))
            im=Image.new('RGB',(640,240));im.paste(a0);im.paste(b0,(320,0));im.save(I.BASE/(a.fase+'_'+nombre+'_pareja.png'))
        (I.BASE/(a.fase+'_panel_medidas.json')).write_text(json.dumps(dict(imagenes=datos,
            limite='edades AMA emparejadas; tiempo/zeni de corridas controladas distintas; Screenshot GS original≈un campo atras'),indent=2),encoding='utf-8')
        print(json.dumps(datos,indent=2));return 0
    if a.accion=='pares':
        import eu_map as E
        us=E.Imagen((E.IMG_DIR/'us_img.bin').read_bytes(),I.ROOT/'generated/dbz3_init.cpp')
        eu=E.Imagen((E.IMG_DIR/'eu_img.bin').read_bytes(),I.ROOT/'generated_eu/dbz3_eu_init.cpp')
        mapa=E.Mapa(us,eu)
        pares={hex(ad):mapa.par(ad) for ad in (0x82139578,0x82101624,0x820FC1A8,0x820C1AD0)}
        (I.BASE/'pares_sombra.json').write_text(json.dumps(pares,indent=2),encoding='utf-8')
        print(json.dumps(pares,indent=2));assert all(p[-1] for p in pares.values())
        return 0
    if a.accion == 'fijar':
        I.fijar(a.fase)
        archivar9c()
        return 0
    if a.accion == 'visual':
        import struct
        import medir_nb0063 as M
        foto_original=M.foto
        def foto_con_estado(j,nombre):
            foto_original(j,nombre)
            leer=lambda ad,n: M.P.leer(j.h,M.P.GBASE+ad,n)
            u=lambda ad: struct.unpack('>I',leer(ad,4))[0]
            g=0x823A3D80;sg=u(g);var=u(g+64)
            rec=0x82323760+sg*92
            li=u(rec+0x1c+var*32) if sg<17 and var<2 else 0
            mgr=u(0x823D6CF4);x=mgr+2096
            estado=dict(stage=sg,var=var,luz_id=li,
                luz=list(struct.unpack('>20f',leer(0x82322b20+li*80,80))),
                actor=list(struct.unpack('>16f',leer(x,64))),
                cuerpo=M.P.leer(j.h,M.P.GBASE+x+0x650+0x520,0xf0).hex())
            (j.run/(nombre+'_estado9c.json')).write_text(json.dumps(estado,indent=2),encoding='utf-8')
        M.foto=foto_con_estado
        return I.visual(a.fase, a.clave)
    if a.accion == 'ap':
        return ap_campo(a.fase)
    env = os.environ.copy()
    env['DBZ3_PRUEBAS_EXE'] = str(I.banco(a.fase)/'dbz3.exe')
    env['DBZ3_PRUEBAS_COPIAS'] = 'sol'
    cmd = [sys.executable, 'tools/pruebas.py', 'todo', '--max', '1', '--forzar']
    log = I.BASE/(a.fase+'_todo.log')
    assert not log.exists()
    inicio = time.monotonic()
    with log.open('x', encoding='utf-8') as f:
        p = subprocess.run(cmd, env=env, stdout=f, stderr=subprocess.STDOUT, cwd=I.ROOT)
    result = dict(comando=cmd, codigo=p.returncode, segundos=time.monotonic()-inicio,
                  sha256=I.sha(I.banco(a.fase)/'dbz3.exe'), log=str(log))
    (I.BASE/(a.fase+'_todo.json')).write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2), flush=True)
    return p.returncode


if __name__ == '__main__':
    raise SystemExit(main())

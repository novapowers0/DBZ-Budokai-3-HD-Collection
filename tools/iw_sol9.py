"""Evidencia reproducible de sol-IW9; nunca borra ni abre mas de dos juegos."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import re
import ctypes

ROOT = Path(__file__).resolve().parents[1]
BASE = Path('D:/tmp/solIW9')
RESPALDO = Path('D:/_PARA_BORRAR_CLAUDE/solIW')
ANTERIORES = ROOT / 'studies 5/ENTREGAS/solIW'
sys.path.insert(0, str(ANTERIORES))
FUENTES = ('iw_minijuegos.cpp', 'iw_minijuegos.h', 'iw_mini_disparo.cpp',
            'iw_mini_botones.cpp', 'iw_mini_vuelo.cpp', 'iw_ame.h',
           'anim_iw.cpp', 'anim_iw.h', 'pasarela_iw.cpp', 'pasarela_iw.h', 'iw_recompensa.h',
           'iw_gs.h', 'iw_gs_blend.h', 'iw_iop.h')


def sha(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def banco(nombre):
    activo=BASE/nombre
    return activo if activo.exists() else RESPALDO/('IW9_snapshot_'+nombre)


def archivar():
    bancos=sorted((p for p in BASE.iterdir() if p.is_dir() and (p/'identidad.json').exists() and (p/'dbz3.exe').exists()),
                  key=lambda p:p.stat().st_ctime,reverse=True)
    RESPALDO.mkdir(parents=True,exist_ok=True)
    meta=BASE/'snapshots_archivados.json'
    movidos=json.loads(meta.read_text(encoding='utf-8')) if meta.exists() else []
    for p in bancos[5:]:
        dest=RESPALDO/('IW9_snapshot_'+p.name)
        if dest.exists():raise FileExistsError(dest)
        shutil.move(str(p),str(dest));movidos.append(dict(antes=str(p),ahora=str(dest)))
        print('Instantanea conservada en respaldo:',dest,flush=True)
    meta.write_text(json.dumps(movidos,indent=2),encoding='utf-8')


def fijar(nombre):
    dst = BASE / nombre
    if banco(nombre).exists():
        raise RuntimeError('Instantanea existente: ' + str(dst))
    dst.mkdir(parents=True)
    RESPALDO.mkdir(parents=True, exist_ok=True)
    for n in FUENTES:
        shutil.copy2(ROOT / 'src' / n, dst / n)
    shutil.copy2(ROOT / 'out/build/win-amd64-dual/dbz3.exe', dst / 'dbz3.exe')
    for n in ('rexruntime.dll', 'rexgpu-xenos.dll'):
        shutil.copy2(ROOT / 'rexglue-sdk-0.10/out/win-amd64-baseline' / n, dst / n)
    dependencias=('src/iw_eventos.h','src/iw_ap_eventos.h','tests/iw_ama_prueba.h','tests/iw_ap_prueba.h',
        'rexglue-sdk-0.10/include/rex/ui/d3d12/d3d12_presenter.h',
        'tools/iw_sol9.py','tools/iw_sol9_original.py','tools/pcsx2_cuadros.py')
    (dst/'dependencias.json').write_text(json.dumps({n:sha(ROOT/n) for n in dependencias if (ROOT/n).exists()},indent=2),encoding='utf-8')
    identidad = {p.name: sha(p) for p in dst.iterdir() if p.is_file()}
    (dst / 'identidad.json').write_text(json.dumps(identidad, indent=2), encoding='utf-8')
    print(nombre, identidad['dbz3.exe'], flush=True)


def auditar():
    from auditar7 import entrada
    from auditar_bancos import hijo
    datos = {}
    for afs, ent, hijos in [('DATA_USA.AFS', 334, range(5)), ('DATA_USA.AFS', 5, (4, 5))]:
        b = entrada(afs, ent)
        for hi in hijos:
            d = hijo(b, hi)
            if d[:4] != b'#AMA':
                continue
            u = lambda o: struct.unpack_from('<I', d, o)[0]
            fs = lambda o, n: struct.unpack_from('<' + str(n) + 'f', d, o)
            nodos = []
            for i in range(u(0x10)):
                raiz = u(u(0x14) + 4 * i)
                ns, vistos, pend = [], set(), [raiz]
                while pend:
                    no = pend.pop()
                    if not no or no in vistos:
                        continue
                    vistos.add(no)
                    an = u(no + 16)
                    segs = []
                    while an and len(segs) < 12:
                        pista = u(an + 8)
                        cuadro = u(pista + 0x14) if pista else 0
                        pos = u(an + 4)
                        pk = u(pos + 0x1c) if pos else 0
                        segs.append(dict(offset=hex(an), dur=u(an + 28), rect=fs(an + 32, 4),
                            tex=u(cuadro) if cuadro else None,
                            uv=fs(cuadro + 0x10, 4) if cuadro else None,
                            pos=fs(pk, 4) if pk else None))
                        an = u(an + 24)
                    ns.append(dict(offset=hex(no), segmentos=segs))
                    pend.extend((u(no + 4), u(no + 8)))
                nodos.append(dict(objeto=i, nodos=ns))
            datos[f'{ent}/{hi}'] = nodos
    BASE.mkdir(parents=True, exist_ok=True)
    (BASE / 'ama_original.json').write_text(json.dumps(datos, indent=2), encoding='utf-8')
    for k, v in datos.items():
        print(k, 'objetos', len(v), 'nodos', [len(x['nodos']) for x in v])
        if k == '5/4':
            for obj in v:
                print(obj['objeto'], [(n['offset'], [(s['tex'], s['rect'], s['pos'], s['uv'])
                    for s in n['segmentos'][:1]]) for n in obj['nodos']])


def esperar(pid):
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.restype = ctypes.c_void_p
    k.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    k.CloseHandle.argtypes = [ctypes.c_void_p]
    h = k.OpenProcess(0x100000, False, pid)
    if h:
        k.WaitForSingleObject(h, 0xffffffff)
        k.CloseHandle(h)


def bancos():
    from auditar7 import entrada
    from auditar_bancos import hijo
    grupos = {'bubbles': range(0x118, 0x11b), 'snake': range(0x17c, 0x17f),
        'radar': range(0x6f4, 0x6f7),
        'NB012': (0x4b0, 0x4ba, 0x4b1, 0x4b2, 0x4b3, 0x4c4, 0x500, 0x501, 0x502),
        'MSN019': (0x76c, 0x76d, 0x76e, 0x76f, 0x770, 0x776, 0x777, 0x778, 0x780, 0x7bc, 0x7bd, 0x7be)}
    resultados = {}
    for cmn in range(916, 934):
        amb = entrada('DATA_CMN.AFS', cmn)
        bsk, amm = hijo(amb, 0), hijo(amb, 1)
        if bsk[:4] != b'#BSK':
            continue
        u = lambda b, o: struct.unpack_from('<I', b, o)[0]
        filas = {}
        for nombre, codigos in grupos.items():
            fs = []
            for c in codigos:
                tab = u(bsk, 0x14)
                # El numero declarado es una frontera; nunca leer fuera como
                # la auditoria vieja del supuesto F0 de CMN535.
                block = u(bsk, tab + c*4) if c < u(bsk, 0x10) else 0
                if not block or block+4 > len(bsk):
                    fs.append(dict(codigo=hex(c), ausente=True))
                    continue
                idx, almacen = struct.unpack_from('<hh', bsk, block)
                dur = None
                if almacen == 3 and amm[:4] == b'#AMM' and 0 <= idx < u(amm, 0x10):
                    dur = u(amm, u(amm, 0x14) + 16*idx + 8)
                fs.append(dict(codigo=hex(c), anim=idx, almacen=almacen, cuadros=dur))
            filas[nombre] = fs
        resultados[str(cmn)] = filas
        print(cmn, {k: sum(not x.get('ausente') for x in v) for k,v in filas.items()}, flush=True)
    (BASE / 'bancos_originales.json').write_text(json.dumps(resultados, indent=2), encoding='utf-8')


def tally(fase, fallo=False, saltar=False):
    from medir_nb0063 import P, foto
    os.environ['DBZ3_PRUEBAS_EXE'] = str(banco(fase) / 'dbz3.exe')
    P.OVERRIDES['dbz3_aspect'] = '"stretch"'
    P.OVERRIDES['dbz3_low_res'] = 'false'
    # Una medida cada vez; pruebas.py administra la cola compartida del PC.
    # Las ventanas ajenas no forman parte del limite propio de esta tanda.
    def env(_):
        e = P.resolver_escena('iw:mini:0x7D7')[0]
        e['DBZ3_TEST_IWMINI'] = '0x7D7'
        e['DBZ3_SOLIW7_KIENZAN'] = '2' if fallo else '1'
        e['DBZ3_SOLIW9_TALLY'] = '1'
        return e
    def medir(j, _):
        j.log('iwdisparo: fin: ' + ('FALLO' if fallo else 'DERRIBO'), 160)
        foto(j, 'recompensa_entrada')
        if fase == 'antes':
            P.dormir(1)
            foto(j, 'recompensa_1s')
            P.dormir(7)
            texto = j.texto_log()
            assert 'iw recompensa: inicio' not in texto
            evidencia = dict(fase=fase, fallo=fallo, salto_forzado=True,
                multiplicador_inerte=4, secuencia_original=False)
        else:
            j.log('iw recompensa: inicio', 5)
            if saltar:
                P.dormir(1)
                j.tecla('Space', 120)
                P.dormir(.5)
            else:
                j.log('iw recompensa: hito 120 ', 5)
                foto(j, 'recompensa_120')
                j.log('iw recompensa: hito 320 ', 5)
                foto(j, 'recompensa_320')
                j.log('iw recompensa: hito 420 ', 5)
                P.dormir(1.5)
                foto(j, 'recompensa_420_retenida')
                if fase=='punto5e':j.log('AMA450 con conteo 420.000 retenido; relojes independientes',5)
            texto = j.texto_log()
            assert 'iw recompensa: confirmado' not in texto, 'salida anticipada'
            j.tecla('Space', 120)
            j.log('iw recompensa: confirmado reloj 420.000', 6)
            muestras = re.findall(r'muestra reloj ([\d.]+) earned_cifras (\d+) total_cifras (\d+) cerrado (\w+)',
                j.texto_log())
            assert muestras and muestras[-1][3] == 'true'
            if not saltar:
                assert all(int(float(b[0]) - float(a[0])) in (0, 1) for a,b in zip(muestras,muestras[1:]))
                assert any(x[0] == '120.000' and x[1] == '0' for x in muestras)
                assert any(x[0] == '121.000' and x[1] == '1' for x in muestras)
                assert any(x[0] == '320.000' and x[2] == '1' for x in muestras)
                assert sum(x[0] == '420.000' and x[3] == 'false' for x in muestras) >= 30
            evidencia = dict(fase=fase, fallo=fallo, saltar=saltar, muestras=muestras,
                fuente='EU285F30; D:/tmp/solIW8/tally_original_{exito,fallo}_*/medida.json',
                limite='fotos nominales tras log; la cronologia se verifica por registro del consumidor real')
        (j.run / 'tally9.json').write_text(json.dumps(evidencia, indent=2), encoding='utf-8')
    nombre = 'iw9_tally_' + fase + ('_fallo' if fallo else '_exito') + ('_salto' if saltar else '')
    P.ESCENARIOS[nombre] = ('IW9 recompensa medida', env, medir)
    r = P.correr('sol', nombre, ['DBZ3_TEST_TURBO=1', 'DBZ3_TEST_OCULTO=0'], None,
                 {'ventana':'960x720', 'timeout':'300'}, True)
    (BASE / (nombre + '.json')).write_text(json.dumps(r, indent=2, default=str), encoding='utf-8')
    print(P.tabla([r]), flush=True)
    return int(not r['ok'])


def visual(fase, clave):
    from medir_nb0063 import P, foto
    os.environ['DBZ3_PRUEBAS_EXE'] = str(banco(fase) / 'dbz3.exe')
    P.OVERRIDES['dbz3_aspect'] = '"stretch"'
    P.OVERRIDES['dbz3_low_res'] = 'false'
    congelado=(fase=='punto2d' or fase.startswith('punto5')) and clave!='0x7D0'
    pasos=Path('D:/DBZ3HD/sol/iw9_panel_cuadro.req')
    def env(_):
        e = P.resolver_escena('iw:mini:' + clave)[0]
        e['DBZ3_TEST_IWMINI'] = clave
        e['DBZ3_TEST_IWMINI_CAMLOG'] = '1'
        if congelado:e['DBZ3_SOLIW9_PANEL_PASOS']='1'
        return e
    def medir(j, _):
        if congelado:pasos.write_text('0',encoding='ascii')
        if clave == '0x7D0':
            j.log('entrada acabada: mando al jugador', 180)
            P.dormir(.8)
            foto(j, 'campo_048')
            originales = [('campo_048', 'D:/tmp/solIW7/original_campo_083529/campo_040_05.png')]
        else:
            j.log('r14: fin:', 300)
            j.log('r14: pose final de IW', 30)
            if congelado:
                j.log('iw panel9: detenido edad 0',10)
                originales=[]
                for edad in (90,320,420):
                    pasos.write_text(str(edad),encoding='ascii')
                    j.log('iw panel9: detenido edad '+str(edad),8)
                    P.dormir(.1);nombre='panel_'+str(edad).zfill(3);foto(j,nombre)
                    encontrados=[]
                    for meta in sorted(BASE.glob('original_panel_'+clave+'_*/medida.json'),key=lambda p:p.stat().st_mtime):
                        d=json.loads(meta.read_text(encoding='utf-8'))
                        for f in d.get('fotos',[]):
                            if f.get('edad')==edad:encontrados.append(meta.parent/f['imagen'])
                    if encontrados:originales.append((nombre,str(encontrados[-1])))
                pasos.write_text('421',encoding='ascii');j.tecla('Space',120)
                j.log('secuencia de resultado de IW completa',6)
            else:
                originales=panel_nominal(j,clave,fase,foto,P)
        from PIL import Image
        from skimage.metrics import structural_similarity
        import numpy as np
        import comparar
        medidas = []
        for nombre, original in originales:
            port = j.run / (nombre + '_05.png')
            a = Image.open(original).convert('RGB').resize((320, 240))
            b = Image.open(port).convert('RGB').resize((320, 240))
            s = float(structural_similarity(np.asarray(a.convert('L')), np.asarray(b.convert('L')), data_range=255))
            ph = comparar.hamming(comparar.phash(a), comparar.phash(b))
            medidas.append(dict(original=original, port=str(port), ssim=s, phash=ph, verde=s >= .45 and ph <= 18))
            hoja = Image.new('RGB', (640, 240)); hoja.paste(a); hoja.paste(b, (320, 0))
            hoja.save(j.run / (nombre + '_pareja.png'))
        (j.run / 'medidas9.json').write_text(json.dumps(dict(fase=fase, clave=clave,
            sha256=sha(banco(fase) / 'dbz3.exe'), imagenes=medidas,
            limite='Edad AMA detenida si pasos9; cuerpo nativo terminal; no se afirma simultaneidad RAM/Screenshot GS'), indent=2), encoding='utf-8')
        print(json.dumps(medidas, indent=2), flush=True)
    nombre = 'iw9_visual_' + fase + '_' + clave
    P.ESCENARIOS[nombre] = ('IW9 campo/panel contra original', env, medir)
    try:
        r = P.correr('sol', nombre, ['DBZ3_TEST_TURBO=1', 'DBZ3_TEST_OCULTO=0'], None,
                     {'ventana':'960x720', 'timeout':'600'}, True)
    finally:
        if congelado and pasos.exists():
            RESPALDO.mkdir(parents=True,exist_ok=True)
            shutil.move(str(pasos),str(RESPALDO/('panel_pasos_'+str(time.time_ns())+'.req')))
    (BASE / (nombre + '.json')).write_text(json.dumps(r, indent=2, default=str), encoding='utf-8')
    print(P.tabla([r]), flush=True)
    return int(not r['ok'])


def panel_nominal(j,clave,fase,foto,P):
            P.dormir(5)
            foto(j, 'panel_resultado')
            original = 'D:/tmp/solIW5b_original_NB006_fallo/final_05.png' if clave == '0x7D4' else \
                'D:/tmp/solIW7/original_panel_084204/panel_360_05.png'
            originales = [('panel_resultado', original)]
            if fase.startswith('punto2'):
                j.log('iw panel: clave ' + clave.lower() + ' edad 420 ', 12)
                P.dormir(.5)
                assert 'secuencia de resultado de IW completa' not in j.texto_log()
                foto(j, 'panel_terminal')
                originales.append(('panel_terminal', original))
                j.tecla('Space', 120)
                j.log('secuencia de resultado de IW completa', 6)
            return originales


def tanda2(fase='punto2', pid=None):
    if pid:
        esperar(pid)
    if not banco(fase).exists():
        fijar(fase)
    trabajos = []
    for f in (('punto1_221432', fase) if fase == 'punto2' else (fase,)):
        for clave in ('0x7D0', '0x7D4', '0x7DC'):
            trabajos.append(('visual_' + f + '_' + clave,
                [sys.executable, '-B', str(__file__), 'visual', f, '--clave', clave]))
    trabajos.append(('todo', [sys.executable, '-B', str(__file__), 'todo']))
    resultados = []
    for nombre, cmd in trabajos:
        env = os.environ.copy()
        env['DBZ3_PRUEBAS_COPIAS'] = 'sol'
        env['DBZ3_PRUEBAS_EXE'] = str(banco(fase) / 'dbz3.exe')
        inicio = time.monotonic()
        log = BASE / (nombre + '_' + fase + '_' + str(time.time_ns()) + '.log')
        with log.open('x', encoding='utf-8') as f:
            p = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
        resultados.append(dict(caso=nombre, codigo=p.returncode,
            segundos=round(time.monotonic()-inicio, 2), log=str(log), comando=cmd))
        (BASE / ('tanda2_' + fase + '.json')).write_text(json.dumps(resultados, indent=2), encoding='utf-8')
        print(resultados[-1], flush=True)
    return int(any(r['codigo'] for r in resultados))


def vuelo(fase, clave,eu=False):
    from medir_nb0063 import P, foto
    os.environ['DBZ3_PRUEBAS_EXE'] = str(banco(fase) / 'dbz3.exe')
    P.OVERRIDES['dbz3_aspect'] = '"stretch"'
    P.OVERRIDES['dbz3_low_res'] = 'false'
    medido=fase.startswith(('punto3','punto5'))
    os.environ['DBZ3_TEST_PANTALLA_EU']='1' if eu else '0'
    P.OVERRIDES['dbz3_region']='"eu"' if eu else '"us"'
    if eu:
        datos=BASE/'datos_eu'
        datos.mkdir(exist_ok=True)
        fuente=ROOT/'yae3_xenon_eu.xex'
        assert hashlib.md5(fuente.read_bytes()).hexdigest().upper()=='C37EB979B762DA0AB5B8C9BA8037CE4E'
        if not (datos/'default.xex').exists():shutil.copy2(fuente,datos/'default.xex')
        for reg in ('us','eu'):
            if not (datos/reg).exists():
                subprocess.run(['cmd','/c','mklink','/J',str(datos/reg),str(ROOT/reg)],check=True,capture_output=True)
        os.environ['DBZ3_TEST_CFG']='dbz3_game_dir="'+datos.as_posix()+'"'
        Original=P.Juego
        class JuegoEU(Original):
            def lanzar(self,env):
                anterior=P.R
                try:P.R=datos;super().lanzar(env)
                finally:P.R=anterior
            def base_real(self,seg=20):
                super().base_real(seg)
                assert 'using EU image config' in self.texto_log(),'no arranco la imagen PPC EU'
        P.Juego=JuegoEU
    copia=os.environ.get('DBZ3_SOLIW9_COPIA','sol')
    if copia not in ('sol','opus','opus2'):raise ValueError('solo copias fijas del arnes')
    pasos = Path('D:/DBZ3HD')/copia/'iw9_vuelo_cuadro.req'
    def env(_):
        e=P.resolver_escena('iw:mini:'+clave)[0]
        e['DBZ3_TEST_IWMINI']=clave
        if medido:
            e['DBZ3_SOLIW9_VUELO_PASOS']='1'
        return e
    def medir(j,_):
        pasos.write_text('0',encoding='ascii')
        if medido:
            j.log(r'afectores tipos0..5 5/0/1/2/5/0; emisores anidados [1-9]',30)
        j.log(r'READY\? terminado; empieza el vuelo',200)
        if fase in ('punto5d','punto3d','punto5e'):
            j.log(r'camara real fovy [\d.]+ pxx 1.706667 pyy 2.275556',5)
        foto(j,'vuelo_000')
        j.tecla('W+3 20000',1)
        if medido and fase not in ('punto3','punto3b'):
            j.log('mando arriba\\+R1 confirmado antes de avanzar',5)
        for edad in (30,60):
            pasos.write_text(str(edad),encoding='ascii')
            if medido:
                j.log('iw vuelo9: edad '+str(edad)+' ',8)
                P.dormir(.15)
            else:
                P.dormir(.5)
            foto(j,'vuelo_'+str(edad).zfill(3))
        if medido:
            j.log('dibujos de resta reales',10)
            if fase=='punto5e':j.log('iwame9: rayo41 geometria original',5)
        (j.run/'identidad9.json').write_text(json.dumps(dict(fase=fase,clave=clave,
            sha256=sha(banco(fase)/'dbz3.exe'),limite='port detenido en edad logica30/60; cuerpo nativo sigue animandose; NO mismo vblank'),indent=2))
    nombre='iw9_vuelo_'+fase+'_'+clave+('_eu' if eu else '')
    P.ESCENARIOS[nombre]=('IW9 vuelo edad y GS',env,medir)
    try:
        r=P.correr(copia,nombre,['DBZ3_TEST_TURBO=1','DBZ3_TEST_OCULTO=0'],None,
            {'ventana':'960x720','timeout':'400'},True)
    finally:
        if pasos.exists():
            RESPALDO.mkdir(parents=True,exist_ok=True)
            shutil.move(str(pasos),str(RESPALDO/('vuelo_pasos_'+str(time.time_ns())+'.req')))
    (BASE/(nombre+'.json')).write_text(json.dumps(r,indent=2,default=str),encoding='utf-8')
    print(P.tabla([r]),flush=True)
    return int(not r['ok'])


def tanda3(fase='punto3'):
    resultados=[]
    trabajos=[('vuelo_'+clave,[sys.executable,'-B',str(__file__),'vuelo',fase,'--clave',clave])
              for clave in ('0x7D6','0x7E0')]
    trabajos.append(('todo',[sys.executable,'-B',str(__file__),'todo']))
    for nombre,cmd in trabajos:
        env=os.environ.copy();env['DBZ3_PRUEBAS_COPIAS']='sol'
        env['DBZ3_PRUEBAS_EXE']=str(banco(fase)/'dbz3.exe')
        inicio=time.monotonic();log=BASE/(nombre+'_'+fase+'_'+str(time.time_ns())+'.log')
        with log.open('x',encoding='utf-8') as f:
            p=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
        resultados.append(dict(caso=nombre,codigo=p.returncode,segundos=round(time.monotonic()-inicio,2),log=str(log),comando=cmd))
        (BASE/('tanda3_'+fase+'.json')).write_text(json.dumps(resultados,indent=2),encoding='utf-8')
        print(resultados[-1],flush=True)
    return int(any(r['codigo'] for r in resultados))


def originales():
    previo=BASE/'originales9.json'
    resultados=json.loads(previo.read_text(encoding='utf-8')) if previo.exists() else []
    for caso,clave in (('vuelo','0x7D6'),('panel','0x7D4'),('panel','0x7DC')):
        if any(r['caso']==caso and r['clave']==clave and r['codigo']==0 for r in resultados):continue
        cmd=[sys.executable,'-B',str(ROOT/'tools/iw_sol9_original.py'),caso,'--clave',clave]
        log=BASE/('original_'+caso+'_'+clave+'_'+str(time.time_ns())+'.log')
        with log.open('x',encoding='utf-8') as f:
            p=subprocess.run(cmd,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
        resultados.append(dict(caso=caso,clave=clave,codigo=p.returncode,log=str(log)))
        (BASE/'originales9.json').write_text(json.dumps(resultados,indent=2),encoding='utf-8')
    return int(any(r['codigo'] for r in resultados))


def combo(fase,fallo=False):
    from medir_nb0063 import P,foto
    os.environ['DBZ3_PRUEBAS_EXE']=str(banco(fase)/'dbz3.exe')
    P.OVERRIDES['dbz3_aspect']='"stretch"';P.OVERRIDES['dbz3_low_res']='false'
    def env(_):
        e=P.resolver_escena('iw:mini:0x7DC')[0]
        if fallo:e['DBZ3_TEST_IWMINI']='0x7DC'
        return e
    def medir(j,_):
        if fase.startswith('punto5'):
            j.log(r'IOP9: sonido 0x21 programa 0 tecla 33 -> VAG 0x49, 5068 muestras fuente, pitch 0x555',40)
        j.log('r14: en juego',150)
        if fallo:
            j.log('iw CHRFADE9: actor 0 restante 14 ',90)
            if fase.startswith('punto5'):j.log('iw CHRFADE9: consumidor dibujo actor 0 intensidad',5)
            j.log('iw CHRFADE9: actor 0 restante 0 intensidad 0.000000',5)
            foto(j,'chr_fade')
            return
        filas=[];fin=time.monotonic()+120;hasta=None;firma=None
        while time.monotonic()<fin and (hasta is None or time.monotonic()<hasta):
            mgr=struct.unpack('>I',P.leer(j.h,P.GBASE+0x823D6CF4,4))[0]
            raw=P.leer(j.h,P.GBASE+mgr+2096+0x650+0x520,0xF0)
            if raw and len(raw)==0xF0:
                cod=struct.unpack_from('>I',raw)[0];cuadro=struct.unpack_from('>f',raw,0xE8)[0]
                if cod==0xDE6 and hasta is None:hasta=time.monotonic()+.75
                if hasta:
                    cam=struct.unpack('>I',P.leer(j.h,P.GBASE+mgr+0x264,4))[0]
                    fov=struct.unpack('>f',P.leer(j.h,P.GBASE+cam+0x34,4))[0]
                    f=(cod,cuadro,fov)
                    if f!=firma:filas.append(dict(t=time.monotonic(),codigo=hex(cod),cuadro=cuadro,fov=fov));firma=f
            time.sleep(.001)
        assert filas,'sin combo port'
        if fase.startswith('punto5'):
            j.log('comando1D -> billboard0F',5)
            j.log('dibujos de resta reales',5)
        minimo=min(filas,key=lambda f:f['fov'])
        (j.run/'combo9.json').write_text(json.dumps(dict(fase=fase,filas=filas,minimo=minimo,
            limite='muestras RAM de cuerpo/camara; lectura asincrona, no mismo vblank'),indent=2),encoding='utf-8')
        print('MINIMO',minimo,flush=True);foto(j,'combo_final')
    nombre='iw9_combo_'+fase+('_fallo' if fallo else '')
    P.ESCENARIOS[nombre]=('IW9 combo, CHRFADE y bancoIOP',env,medir)
    r=P.correr('sol',nombre,['DBZ3_TEST_OCULTO=0','DBZ3_TEST_TURBO=1'],None,{'ventana':'960x720','timeout':'400'},True)
    (BASE/(nombre+'.json')).write_text(json.dumps(r,indent=2,default=str),encoding='utf-8')
    print(P.tabla([r]),flush=True);return int(not r['ok'])


ALTERNATIVAS=(('0x7D1',11,0xA40),('0x7D1',12,0xA46),('0x7D1',13,0xA4C),
    ('0x7D2',11,0xA43),('0x7D2',12,0xA49),('0x7D2',13,0xA4F),
    ('0x7DF',3,0xA52),('0x7DF',10,0xA55),('0x7DF',8,0xA58),('0x7DF',7,0xA5B),
    ('0x7DA',10,0xE2A),('0x7DA',12,0xE42),('0x7DA',13,0xE5A),('0x7E1',14,0xE66))


def alternativa(fase,clave,personaje,completo=False,fallo=False):
    from medir_nb0063 import P,foto
    os.environ['DBZ3_PRUEBAS_EXE']=str(banco(fase)/'dbz3.exe')
    alias=next(a for k,p,a in ALTERNATIVAS if k==clave and p==personaje)
    def env(_):
        e=P.resolver_escena('iw:mini:'+clave)[0]
        e['DBZ3_SOLIW9_PERSONAJE']=str(personaje)
        if fallo:e['DBZ3_TEST_IWMINI']=clave
        return e
    def medir(j,_):
        P._esperar_pelea(j,180)
        actual=struct.unpack('>H',P.leer(j.h,P.GBASE+0x8245EFC0+32,2))[0]
        antes=not fase.startswith('punto5')
        if antes:
            assert actual!=personaje,'el antes ya aplica esta alternativa'
            evidencia=dict(actor_real=actual,solicitado=personaje,alternativa_aplicada=False)
        else:
            assert actual==personaje,(actual,personaje)
            j.log(r'iw alternativa9: CMN\d+ BSK0x[0-9a-f]+ alias'+hex(alias)+r' ',60)
            fin=time.monotonic()+90;muestras=[]
            while time.monotonic()<fin:
                mgr=struct.unpack('>I',P.leer(j.h,P.GBASE+0x823D6CF4,4))[0]
                for cuerpo in (0x650,0xCD0):
                    raw=P.leer(j.h,P.GBASE+mgr+2096+cuerpo+0x520,0xF0)
                    if not raw or len(raw)!=0xF0:continue
                    codigo=struct.unpack_from('>I',raw)[0]
                    if codigo==alias:
                        muestras.append(dict(cuerpo=hex(cuerpo),codigo=hex(codigo),
                            clip=hex(struct.unpack_from('>I',raw,0xE4)[0]),cuadro=struct.unpack_from('>f',raw,0xE8)[0],
                            duracion=struct.unpack_from('>I',raw,0xEC)[0]))
                if muestras:break
                time.sleep(.005)
            assert muestras,'el actor no resuelve su alias de entrada'
            leidas=re.findall(r'iw alternativa9: CMN(\d+) BSK(0x[0-9a-f]+) alias(0x[0-9a-f]+) (\d+) cuadros (\d+) huesos',j.texto_log())
            assert len(leidas)==69,('entradas propias cargadas',len(leidas))
            assert not re.search(r'pose de botones .*no se pudo leer',j.texto_log())
            evidencia=dict(actor_real=actual,solicitado=personaje,alternativa_aplicada=True,
                cuerpo=muestras,entradas_propias=leidas,
                limite='entrada resuelta en el cuerpo real y69 recursos cargados; no certifica accesorios ni cada desenlace')
        foto(j,'alternativa')
        if completo:
            assert not antes,'el antes no recibe la alternativa'
            j.log('r14: fin:',300)
            banco={10:924,12:929,13:932,14:926}[personaje]
            j.log('final real banco '+str(banco),20)
            foto(j,'final_propio')
            j.log('desenlace completo banco '+str(banco),20)
            j.log(r'r14: resultado .* superado '+('false' if fallo else 'true'),30)
            evidencia['desenlace_completo']=True;evidencia['superado']=not fallo
        (j.run/'alternativa9.json').write_text(json.dumps(evidencia,indent=2),encoding='utf-8')
    nombre='iw9_alt_'+fase+'_'+clave+'_'+str(personaje)+('_completo_fallo' if completo and fallo else '_completo' if completo else '')
    P.ESCENARIOS[nombre]=('IW9 alternativa con su banco original',env,medir)
    r=P.correr('sol',nombre,['DBZ3_TEST_OCULTO=0','DBZ3_TEST_TURBO=1'],None,{'ventana':'960x720','timeout':'600'},True)
    (BASE/(nombre+'.json')).write_text(json.dumps(r,indent=2,default=str),encoding='utf-8')
    print(P.tabla([r]),flush=True);return int(not r['ok'])


def tanda45(numero,fase):
    if numero==4:
        trabajos=[('combo_antes',['combo','punto1_221432']),('combo_despues',['combo',fase]),
                  ('CHRFADE',['combo',fase,'--fallo']),('tally_final',['tally',fase])]
    else:
        trabajos=[('antes_'+k,['alternativa','punto1_221432','--clave',k,'--personaje',str(p)])
                  for k,p,a in ALTERNATIVAS if (k,p) in (('0x7D1',11),('0x7D2',11),('0x7DF',3),('0x7DA',10),('0x7E1',14))]
        trabajos += [('alt_'+k+'_'+str(p),['alternativa',fase,'--clave',k,'--personaje',str(p)]) for k,p,a in ALTERNATIVAS]
    trabajos.append(('todo',['todo']))
    resultados=[]
    for nombre,args in trabajos:
        cmd=[sys.executable,'-B',str(__file__)]+args;env=os.environ.copy()
        env['DBZ3_PRUEBAS_COPIAS']='sol';env['DBZ3_PRUEBAS_EXE']=str(banco(fase)/'dbz3.exe')
        log=BASE/(nombre+'_'+fase+'_'+str(time.time_ns())+'.log');inicio=time.monotonic()
        with log.open('x',encoding='utf-8') as f:p=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
        resultados.append(dict(caso=nombre,codigo=p.returncode,segundos=round(time.monotonic()-inicio,2),comando=cmd,log=str(log)))
        (BASE/('tanda'+str(numero)+'_'+fase+'.json')).write_text(json.dumps(resultados,indent=2),encoding='utf-8')
        print(resultados[-1],flush=True)
    return int(any(r['codigo'] for r in resultados))


def final9(fase,corta=False):
    trabajos=[('vuelo_final',['vuelo',fase,'--clave','0x7D6']),
              ('panel006',['visual',fase,'--clave','0x7D4']),('panel014',['visual',fase,'--clave','0x7DC']),
              ('CHRFADE_consumidor',['combo',fase,'--fallo'])]
    for clave,personaje in (() if corta else (('0x7DA',10),('0x7E1',14))):
        for fallo in (False,True):
            trabajos.append(('final_'+clave+('_fallo' if fallo else '_exito'),
                ['alternativa',fase,'--clave',clave,'--personaje',str(personaje),'--completo']+(['--fallo'] if fallo else [])))
    trabajos.append(('todo',['todo']))
    resultados=[]
    for nombre,args in trabajos:
        cmd=[sys.executable,'-B',str(__file__)]+args;env=os.environ.copy()
        env['DBZ3_PRUEBAS_COPIAS']='sol';env['DBZ3_PRUEBAS_EXE']=str(banco(fase)/'dbz3.exe')
        log=BASE/(nombre+'_'+fase+'_'+str(time.time_ns())+'.log');inicio=time.monotonic()
        with log.open('x',encoding='utf-8') as f:p=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
        resultados.append(dict(caso=nombre,codigo=p.returncode,segundos=round(time.monotonic()-inicio,2),comando=cmd,log=str(log)))
        (BASE/('final9_'+fase+'.json')).write_text(json.dumps(resultados,indent=2),encoding='utf-8')
        print(resultados[-1],flush=True)
    return int(any(r['codigo'] for r in resultados))


def cierre(fase):
    trabajos=[('campo_final',['visual',fase,'--clave','0x7D0']),
        ('vuelo_US',['vuelo',fase,'--clave','0x7D6']),('vuelo_EU',['vuelo',fase,'--clave','0x7E0','--eu']),
        ('tally_exito',['tally',fase]),('tally_fallo',['tally',fase,'--fallo']),('tally_salto',['tally',fase,'--saltar']),
        ('combo_final',['combo',fase]),('todo',['todo'])]
    resultados=[]
    for nombre,args in trabajos:
        cmd=[sys.executable,'-B',str(__file__)]+args;env=os.environ.copy()
        env['DBZ3_PRUEBAS_COPIAS']='sol';env['DBZ3_PRUEBAS_EXE']=str(banco(fase)/'dbz3.exe')
        log=BASE/(nombre+'_'+fase+'_'+str(time.time_ns())+'.log');inicio=time.monotonic()
        with log.open('x',encoding='utf-8') as f:p=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
        resultados.append(dict(caso=nombre,codigo=p.returncode,segundos=round(time.monotonic()-inicio,2),comando=cmd,log=str(log)))
        (BASE/('cierre_'+fase+'.json')).write_text(json.dumps(resultados,indent=2),encoding='utf-8')
        print(resultados[-1],flush=True)
    return int(any(r['codigo'] for r in resultados))


def comprobar(fase):
    b=banco(fase);identidad=json.loads((b/'identidad.json').read_text(encoding='utf-8'))
    comprobaciones={}
    for tipo in ('win-amd64-dual','win-amd64-release'):
        build=ROOT/'out/build'/tipo
        comprobaciones[tipo+'_exe']=sha(build/'dbz3.exe')==identidad['dbz3.exe']
        for dll in ('rexruntime.dll','rexgpu-xenos.dll'):
            canon=ROOT/'rexglue-sdk-0.10/out/win-amd64-baseline'/dll
            comprobaciones[tipo+'_'+dll]=sha(build/dll)==sha(canon)==identidad[dll]
    fuentes={n:sha(ROOT/'src'/n)==identidad[n] for n in FUENTES}
    r=dict(fase=fase,comprobaciones=comprobaciones,fuentes=fuentes)
    (BASE/('comprobacion_'+fase+'.json')).write_text(json.dumps(r,indent=2),encoding='utf-8')
    print(json.dumps(r,indent=2),flush=True)
    return int(not all(comprobaciones.values()))


def audio(fase):
    trabajos=[('combo_ADSR',[sys.executable,'-B',str(__file__),'combo',fase]),
        ('AP_actor',[sys.executable,'-B',str(__file__),'alternativa',fase,'--clave','0x7DA','--personaje','10']),
        ('todo',[sys.executable,'tools/pruebas.py','todo','--max','1','--forzar'])]
    resultados=[]
    for nombre,cmd in trabajos:
        env=os.environ.copy();env['DBZ3_PRUEBAS_COPIAS']='sol';env['DBZ3_PRUEBAS_EXE']=str(banco(fase)/'dbz3.exe')
        inicio=time.monotonic();log=BASE/(nombre+'_'+fase+'_'+str(time.time_ns())+'.log')
        with log.open('x',encoding='utf-8') as f:p=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
        resultados.append(dict(caso=nombre,codigo=p.returncode,segundos=round(time.monotonic()-inicio,2),comando=cmd,log=str(log)))
        (BASE/('audio_'+fase+'.json')).write_text(json.dumps(resultados,indent=2),encoding='utf-8')
        print(resultados[-1],flush=True)
    comprobar(fase)
    return int(any(r['codigo'] for r in resultados))


def medidas():
    from PIL import Image
    import numpy as np
    from skimage.metrics import structural_similarity
    import comparar
    out=BASE/'comparaciones';out.mkdir(exist_ok=True)
    res=dict(umbral=dict(ssim=.45,phash=18),normalizacion='RGB→luminancia320x240; sin ORB ni registro geometrico',imagenes={},combo={},vuelo_posicion={})
    def par(org,port,nombre,**extra):
        if not Path(org).exists() or not Path(port).exists():return
        a=Image.open(org).convert('RGB').resize((320,240));b=Image.open(port).convert('RGB').resize((320,240))
        ss=float(structural_similarity(np.asarray(a.convert('L')),np.asarray(b.convert('L')),data_range=255))
        ph=comparar.hamming(comparar.phash(a),comparar.phash(b))
        hoja=Image.new('RGB',(640,240));hoja.paste(a);hoja.paste(b,(320,0));hoja.save(out/(nombre+'.png'))
        res['imagenes'][nombre]=dict(original=str(org),port=str(port),ssim=ss,phash=ph,verde=ss>=.45 and ph<=18,**extra)
    originales=[]
    for p in sorted(BASE.glob('original_vuelo_0x7D6_*/medida.json'),key=lambda p:p.stat().st_mtime):
        d=json.loads(p.read_text(encoding='utf-8'))
        if len(d.get('filas',[]))==51:originales.append((p,d))
    if originales:
        org,datos=originales[-1]
        for p in BASE.glob('iw9_vuelo_*_0x7D6.json'):
            r=json.loads(p.read_text(encoding='utf-8'));run=Path(r['run'])
            for n,campo in ((0,0),(30,25),(60,50)):
                par(org.parent/f'vuelo_{campo:03}_05.png',run/f'vuelo_{n:03}_05.png',p.stem+'_'+str(n),
                    tiempo_ms_original=datos['filas'][campo]['tiempo_ms'],
                    codigo_original=datos['filas'][campo]['codigo'],
                    limite='Original Screenshot≈un campo atras de RAM; 1s incluye aro/mensaje, no vuelo libre; cuerpo del port no detenido')
            log=run/'dbz3.log'
            if log.exists():
                matches=re.findall(r'iw vuelo9: edad (\d+) tiempo_ms ([\d.]+) estado (\d+) posicion \(([-\d.]+),([-\d.]+)\)',log.read_text(encoding='utf-8',errors='replace'))
                res['vuelo_posicion'][p.stem]=[]
                for edad,ms,estado,x,z in matches:
                    if int(edad) not in (30,60):continue
                    f=datos['filas'][int(edad)*5//6]
                    res['vuelo_posicion'][p.stem].append(dict(edad=int(edad),tiempo_ms=float(ms),estado=int(estado),x=float(x),z=float(z),
                        original=f,error_x=float(x)-f['posicion'][0],error_z=float(z)-f['posicion'][2]))
    for p in BASE.glob('iw9_visual_*.json'):
        r=json.loads(p.read_text(encoding='utf-8'));run=Path(r['run'])
        if '_0x7D0' in p.stem:
            par('D:/tmp/solIW7/original_campo_083529/campo_040_05.png',run/'campo_048_05.png',p.stem,limite='fase nominal READY40PAL/48NTSC; no mismo vblank')
        else:
            clave='0x7D4' if '_0x7D4' in p.stem else '0x7DC'
            for edad in (90,320,420):
                orgs=[]
                for m in sorted(BASE.glob('original_panel_'+clave+'_*/medida.json'),key=lambda p:p.stat().st_mtime):
                    for f in json.loads(m.read_text(encoding='utf-8')).get('fotos',[]):
                        if f.get('edad')==edad:orgs.append(m.parent/f['imagen'])
                if orgs:par(orgs[-1],run/f'panel_{edad:03}_05.png',p.stem+'_'+str(edad),edad=edad,ejecucion_ok=r['ok'])
    om=Path('D:/tmp/solIW8/combo_original_135810/medida.json')
    if om.exists():
        original=min(json.loads(om.read_text())['filas'],key=lambda f:f['fov'])
        for p in BASE.glob('iw9_combo_*.json'):
            r=json.loads(p.read_text(encoding='utf-8'));m=Path(r['run'])/'combo9.json'
            if not m.exists():continue
            minimo=json.loads(m.read_text(encoding='utf-8'))['minimo']
            res['combo'][p.stem]=dict(minimo_port=minimo,minimo_original=original,error_fov=minimo['fov']-original['fov'],
                cuadro_port_en_unidades_PAL=minimo['cuadro']*1.2,
                limite='conversion de cuadros del cuerpo, no segundos; lecturas RAM asincronas')
    (BASE/'medidas9.json').write_text(json.dumps(res,indent=2),encoding='utf-8')
    for k,r in res['imagenes'].items():print(k,round(r['ssim'],4),r['phash'],r['verde'])
    for k,r in res['combo'].items():print(k,'minimo',r['minimo_port']['cuadro'],'FOV',r['minimo_port']['fov'],'error',r['error_fov'])


def tanda1(pid=None):
    if pid:
        esperar(pid)
    resultados = []
    fase = 'punto1' if not banco('punto1').exists() else 'punto1_' + time.strftime('%H%M%S')
    trabajos = [
        ('tally_antes', [sys.executable, '-B', str(__file__), 'tally', 'antes']),
        ('build', [sys.executable, 'tools/compilar.py']),
        ('fijar', [sys.executable, '-B', str(__file__), 'fijar', fase]),
        ('tally_exito', [sys.executable, '-B', str(__file__), 'tally', fase]),
        ('tally_fallo', [sys.executable, '-B', str(__file__), 'tally', fase, '--fallo']),
        ('tally_salto', [sys.executable, '-B', str(__file__), 'tally', fase, '--saltar']),
        ('todo', [sys.executable, '-B', str(__file__), 'todo'])]
    for nombre, cmd in trabajos:
        env = os.environ.copy()
        if nombre == 'todo':
            env['DBZ3_PRUEBAS_COPIAS'] = 'sol'
            env['DBZ3_PRUEBAS_EXE'] = str(banco(fase) / 'dbz3.exe')
        inicio = time.monotonic()
        print('EMPIEZA', nombre, flush=True)
        log = BASE / (nombre + '_' + fase + '.log')
        with log.open('x', encoding='utf-8') as f:
            p = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
        resultados.append(dict(caso=nombre, codigo=p.returncode,
            segundos=round(time.monotonic()-inicio, 2), comando=cmd))
        (BASE / ('tanda1_' + fase + '.json')).write_text(json.dumps(resultados, indent=2), encoding='utf-8')
        print(resultados[-1], flush=True)
        if p.returncode:
            return p.returncode
    return 0


def registrar_pausa():
    # La tanda no habia abierto su segundo juego. Se han parado SOLO los
    # runners propios 24524/20220, tras comprobar que no tenian dbz3 hijo.
    lock = Path('D:/DBZ3HD/_en_uso_sol')
    if lock.exists() and (lock / 'pid').read_text().split()[0] == '20220':
        RESPALDO.mkdir(parents=True, exist_ok=True)
        shutil.move(str(lock), str(RESPALDO / ('IW9_reserva_sol_' + str(time.time_ns()))))
    p = BASE / 'punto1/iw_recompensa.h'
    if not p.exists():
        shutil.copy2(ROOT / 'src/iw_recompensa.h', p)
    (BASE / 'punto1/fuente_adicional.json').write_text(json.dumps(
        {'iw_recompensa.h': sha(p)}, indent=2), encoding='utf-8')
    (BASE / 'bloqueo.json').write_text(json.dumps(dict(
        estado='PAUSADO; no se certifica ningun cierre',
        motivo='guardia global de dos juegos: otras sesiones mantienen 3-5 procesos',
        antes='PASS', build='PASS, codigo 0', despues='NO EJECUTADO: esperando cupo',
        todo='NO EJECUTADO', runners_propios_detenidos=[24524, 20220],
        reanudar='python -B tools/iw_sol9.py lanzar1'), indent=2), encoding='utf-8')
    print('Pausa registrada; reserva propia conservada y ningun juego ajeno cerrado.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('modo', choices=['fijar', 'archivar', 'comprobar', 'medidas', 'auditar', 'bancos', 'tally', 'visual', 'vuelo', 'combo', 'alternativa', 'lanzar1', 'tanda1', 'lanzar2', 'tanda2', 'lanzar3', 'tanda3', 'lanzar4', 'tanda4', 'lanzar5', 'tanda5', 'lanzar_final', 'final', 'lanzar_cierre', 'cierre', 'lanzar_audio', 'audio', 'lanzar_originales', 'originales', 'lanzar_vuelo', 'todo', 'registrar_pausa'])
    ap.add_argument('nombre', nargs='?', default='antes')
    ap.add_argument('--fallo', action='store_true')
    ap.add_argument('--saltar', action='store_true')
    ap.add_argument('--clave', default='0x7D0')
    ap.add_argument('--personaje',type=int,default=11)
    ap.add_argument('--completo',action='store_true')
    ap.add_argument('--corta',action='store_true')
    ap.add_argument('--eu',action='store_true')
    ap.add_argument('--esperar-pid', type=int)
    a = ap.parse_args()
    if a.modo in ('lanzar1', 'lanzar2', 'lanzar3', 'lanzar4', 'lanzar5', 'lanzar_originales', 'lanzar_vuelo', 'lanzar_final','lanzar_cierre','lanzar_audio'):
        tanda = a.modo.removeprefix('lanzar_') if a.modo.startswith('lanzar_') else a.modo.replace('lanzar','tanda')
        cmd = [sys.executable, '-B', '-u', str(__file__), tanda]
        if a.nombre != 'antes':
            cmd.append(a.nombre)
        if a.modo=='lanzar_vuelo':cmd+=['--clave',a.clave]
        if a.modo=='lanzar_vuelo' and a.eu:cmd.append('--eu')
        if a.modo=='lanzar_final' and a.corta:cmd.append('--corta')
        if a.esperar_pid:
            cmd += ['--esperar-pid', str(a.esperar_pid)]
        log = BASE / (tanda + '_' + str(time.time_ns()) + '.log')
        with log.open('x', encoding='utf-8') as f:
            p = subprocess.Popen(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
        print('Tanda IW9 PID', p.pid)
        return
    if a.esperar_pid:
        esperar(a.esperar_pid)
    if a.modo == 'fijar':
        fijar(a.nombre)
    elif a.modo == 'archivar':
        archivar()
    elif a.modo == 'medidas':
        medidas()
    elif a.modo == 'comprobar':
        raise SystemExit(comprobar(a.nombre))
    elif a.modo == 'auditar':
        auditar()
    elif a.modo == 'bancos':
        bancos()
    elif a.modo == 'tanda1':
        raise SystemExit(tanda1())
    elif a.modo == 'tanda2':
        raise SystemExit(tanda2('punto2' if a.nombre == 'antes' else a.nombre))
    elif a.modo == 'tanda3':
        raise SystemExit(tanda3('punto3' if a.nombre == 'antes' else a.nombre))
    elif a.modo == 'originales':
        raise SystemExit(originales())
    elif a.modo in ('tanda4','tanda5'):
        raise SystemExit(tanda45(int(a.modo[-1]),'punto5' if a.nombre=='antes' else a.nombre))
    elif a.modo == 'combo':
        raise SystemExit(combo(a.nombre,a.fallo))
    elif a.modo == 'alternativa':
        raise SystemExit(alternativa(a.nombre,a.clave,a.personaje,a.completo,a.fallo))
    elif a.modo == 'final':
        raise SystemExit(final9(a.nombre,a.corta))
    elif a.modo == 'cierre':
        raise SystemExit(cierre(a.nombre))
    elif a.modo == 'audio':
        raise SystemExit(audio(a.nombre))
    elif a.modo == 'vuelo':
        raise SystemExit(vuelo(a.nombre,a.clave,a.eu))
    elif a.modo == 'visual':
        raise SystemExit(visual(a.nombre, a.clave))
    elif a.modo == 'registrar_pausa':
        registrar_pausa()
    elif a.modo == 'todo':
        import pruebas as P
        raise SystemExit(P.main(['todo', '--max', '1', '--forzar']))
    else:
        raise SystemExit(tally(a.nombre, a.fallo, a.saltar))


if __name__ == '__main__':
    main()

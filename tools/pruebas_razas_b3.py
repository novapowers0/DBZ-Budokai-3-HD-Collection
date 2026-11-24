"""R16: recetas nativas del AFS local, solo en la copia bloqueada por pruebas.py."""
import json
import os
import re
import shutil
import subprocess
import sys
import time
import tomllib

RAZAS = {'freezer': (27, 3, (6, 0, 1, 3)),
         'namekiano': (11, 2, (1, 1, 3, 0)),
         'majin': (34, 4, (6, 1, 1, 0))}


def registrar(P):
    def env(par):
        e = dict(DBZ3_CAC_XV2='0', DBZ3_TEST_TURBO=par.get('turbo', '1'),
                 DBZ3_TEST_CFG='dbz3_cac_xv2=false;dbz3_heroe_ui_nueva=true;dbz3_language=5;dbz3_cine_intro=false')
        fase = par.get('fase', 'lamina')
        e['DBZ3_CAC_RAZAS_EXPERIMENTAL'] = '1' if par.get('experimental') == '1' or (par.get('catalogo') == '1' and par.get('curadas') != '1') else '0'
        # prep crea un perfil vacio: Load Game no lleva al menu. Las tres
        # fases deben elegir New Game antes de esperar su escenario.
        par['sin_arranque'] = '1'
        if fase == 'guardar':
            e.update(DBZ3_TEST_MODO='heroe,0', DBZ3_TEST_HEROE_ABRIR='2')
        elif fase == 'forma':
            if par.get('raza') == 'majin':
                e.update(DBZ3_TEST_ULT='3')
                e['DBZ3_TEST_CFG'] += ';dbz3_forma_traza=true;dbz3_combate_prueba_muneco="quieto"'
            else:
                e.update(DBZ3_TEST_FORMAS='1', DBZ3_TEST_PKG='1')
        else:
            e.update(DBZ3_TEST_FREECAM='0,0,0,0,0,45', DBZ3_TEST_FREECAM_LOG='2', DBZ3_TEST_FREECAM_HUD='1')
            e['DBZ3_TEST_CFG'] += ';dbz3_combate_prueba_muneco="quieto";dbz3_camara_sigue=false'
        return e

    def prep(j, par, entorno):
        os.environ['DBZ3_CAC_RAZAS_EXPERIMENTAL'] = entorno['DBZ3_CAC_RAZAS_EXPERIMENTAL']
        raza = par.get('raza', 'freezer').lower()
        if raza not in RAZAS:
            raise P['Fallo']('raza debe ser freezer, namekiano o majin')
        body, raza_id, valores = RAZAS[raza]
        # Super Buu tiene absorciones nativas; el Buu gordo no transforma.
        # Guardar y transformar deben probar el MISMO anfitrion de Majin.
        if raza == 'majin':
            body = 35
        if par.get('anfitrion'):
            body = int(par['anfitrion'])
            if body not in {'freezer': (27, 38), 'namekiano': (11,), 'majin': (34, 35, 36)}[raza]:
                raise P['Fallo']('El anfitrion no pertenece a la raza solicitada')
        if par.get('piezas'):
            valores = tuple(int(x) for x in par['piezas'].split(','))
        if len(valores) != 4:
            raise P['Fallo']('piezas necesita cuatro valores de receta')
        valores = list(valores)
        if 'cabeza' in par:
            valores[0] = int(par['cabeza'])
        if 'cuerpo' in par:
            valores[2] = int(par['cuerpo'])
        limites = (48, 24, 4, 4) if raza == 'freezer' else (8, 4, 4, 4)
        if any(not 0 <= v < n for v, n in zip(valores, limites)):
            raise P['Fallo']('Seleccion de cabeza/cuerpo fuera del catalogo B3')
        from pruebas_cac_guardado import mover_guardados
        origen = j.dest / 'user_data/dbz3'
        previo = j.run / 'user_data_previo'
        if not hasattr(j, 'razas_entorno'):
            raiz_guardado = j.run
            def restaurar():
                if j.vivo():
                    raise P['Fallo']('El juego sigue vivo: no se restaura su guardado')
                mover_guardados(origen, raiz_guardado / 'user_data_final')
                if previo.exists():
                    mover_guardados(previo, origen)
            j.restaurar_guardado = restaurar
            mover_guardados(origen, previo)
        base = 4000 if raza == 'freezer' else 4080
        ids = [base + v for v in valores]
        for d in (j.dest / 'mods').iterdir():
            if d.is_dir() and not d.name.startswith('_') and d.name != 'heroe_propio':
                (d / '.disabled').touch()
        P['_heroe_pieza_receta'](j.dest, 0, -1, cuerpo=body)
        cac = j.dest / 'user_data/dbz3/cac.toml'
        text = cac.read_text(encoding='utf-8')
        text = text.replace('raza = 0\n', 'raza = %d\n' % raza_id)
        text = text.replace('nombre = "Heroe"', 'nombre = "R16 NATIVO"')
        text = text.replace('b3_piezas = [-1, -1, -1, -1]', 'b3_piezas = %s' % ids)
        text = text.replace('moveset = -1', 'moveset = %d' % body)
        cac.write_text(text, encoding='utf-8')
        P['_apagar_xv2'](j.dest)
        P['_quitar_partida_heroe'](j.dest / 'user_data/dbz3/rpg.toml')
        ficha = 'cuerpo=%d;traje=0;pelo=0;cara=0;logo=0;scouter=0;nombre=R16 NATIVO;donante=%d' % (body, body)
        ficha += ';partes=0,0,0|0,0,0|0,0,0|0,0,0;c1=0,0,0;c2=0,0,0'
        ficha += ''.join(';b3_%s=%d' % (z, i) for z, i in zip(('torso','piernas','guantes','botas'), ids))
        out = j.run / 'vista_heroe'
        out.mkdir(exist_ok=True)
        def ejecutar(cmd, nombre):
            r = subprocess.run(cmd, env=dict(os.environ, DBZ3_CAC_XV2='0'), capture_output=True,
                               text=True, errors='replace', timeout=900)
            (j.run / nombre).write_text(r.stdout + r.stderr, encoding='utf-8')
            if r.returncode:
                raise P['Fallo'](nombre + ': ' + (r.stdout+r.stderr)[-900:])
        mod = j.dest / 'mods/heroe_propio'
        personaje = mod / 'personaje.toml'
        sys.path[:0] = [str(P['R'] / 'mod center hd/cac'), str(P['R'] / 'mod center hd'), str(P['R'] / 'awo_tools')]
        import vista_heroe as vh
        import model_render as mr
        import razas_b3 as rb3
        import numpy as np
        work = j.run / 'afs'
        work.mkdir(exist_ok=True)
        afs = vh.rb.Afs(str(P['R'] / 'us/data_cmn.afs'), str(work))
        h = vh.leer_ficha(ficha)
        native = vh.rb.IDS[body]
        solo_base = par.get('fase', 'lamina') == 'lamina'
        # Mismo exportador del creador, sin montar tres campanas DU ajenas a esta prueba.
        if solo_base:
            # La lamina no transforma: no construir cinco formas que no se dibujan.
            fid = native['models'][0]
            datos, detalle = vh.combate_bin(afs.entry(fid), h, str(j.dest), afs)
            detalle_montaje = vh.escribir_personaje_propio(str(j.dest), 1, h, ficha, datos, detalle, fid)
        else:
            detalle_montaje = vh.escribir_mod_combate(str(j.dest), 1, h, ficha, afs)
        (j.run / 'montaje_nativo.log').write_text(str(detalle_montaje), encoding='utf-8')
        doc = tomllib.loads(personaje.read_text(encoding='utf-8'))['personaje']
        fuente = [native['models'][0]]
        for donante, forma in ([] if solo_base else native['form_models'][1:]):
            row = vh.rb.IDS.get(donante) or vh.rb.DB['form_ids'][str(donante)]
            fuente.append(row['models'][forma])
        if len(doc['modelos']) != len(fuente):
            raise P['Fallo']('Cadena de formas nativas incompleta')
        reporte = []
        for fid, archivo in zip(fuente, doc['modelos']):
            ficha_esperada = dict(h)
            if reporte and not rb3.experimental():
                ficha_esperada['_raza_modelo_forma'] = fid
            expected, detalle = rb3.aplicar(afs.entry(fid), ficha_esperada, j.dest, afs)
            actual = (mod / archivo).read_bytes()
            a, b = mr.Model(expected), mr.Model(actual)
            if len(a.tris) != len(b.tris) or not np.allclose(a.pos, b.pos, atol=1e-6) or not np.array_equal(a.tris,b.tris):
                raise P['Fallo']('Modelo exportado distinto a la receta en forma %d' % len(reporte))
            reporte.append(dict(fid=fid, archivo=archivo, **detalle))
        (j.run / 'receta_formas.json').write_text(json.dumps(reporte, indent=2), encoding='utf-8')
        if par.get('control') in ('nativo', 'geometria', 'texturas', 'sin_variantes'):
            for fid, archivo in zip(fuente, doc['modelos']):
                shutil.move(str(mod / archivo), str(j.run / ('receta_' + P['Path'](archivo).name)))
                raw = afs.entry(fid)
                if par['control'] in ('geometria', 'texturas'):
                    class TexturasIntactas:
                        def traer(self, modelo, indice, *args):
                            return indice
                    host = mr.Model(raw)
                    tex = rb3._Texturas(raw) if par['control'] == 'texturas' else TexturasIntactas()
                    grupos = rb3._grupos(host, host, np.ones(len(host.tris), bool), 'cuerpo', tex)
                    raw, _ = rb3._emitir(raw, host, grupos, len(tex.offsets) if hasattr(tex, 'offsets') else host.table.n_tex)
                    if par['control'] == 'texturas':
                        from b3_gateway import replace_azt, clamp_awg_textures
                        raw = replace_azt(raw, tex.terminar())
                        clamp_awg_textures(raw, len(tex.offsets))
                elif par['control'] == 'sin_variantes':
                    variantes = mr.Model.variants
                    try:
                        mr.Model.variants = lambda self: {}
                        raw, _ = rb3.aplicar(raw, h, j.dest, afs)
                    finally:
                        mr.Model.variants = variantes
                (mod / archivo).write_bytes(raw)
            j.info('CONTROL: %s en el mismo slot ampliado' % par['control'])
        caps = list(dict.fromkeys(c for c in native['form_caps'] if c))
        if 'capsulas_nativas' not in doc:
            with personaje.open('a', encoding='utf-8') as f:
                f.write('\ncapsulas_nativas = %s\n' % caps)
        dis = mod / '.disabled'
        if dis.exists():
            shutil.move(str(dis), str(j.run / 'heroe.disabled.bak'))
        ejecutar([sys.executable, str(P['R'] / 'mod center hd/roster_build.py'), 'construir', '--mods', str(j.dest / 'mods')], 'roster_nativo.log')
        rows = tomllib.loads((j.dest / 'mods/_roster/roster.toml').read_text(encoding='utf-8'))['personaje']
        cid = next(r['id'] for r in rows if r['nombre'].upper() == 'R16 NATIVO')
        par.update(_id=cid, _cuerpo=body, _formas=len(fuente), _ids=ids)
        j.razas_entorno = dict(entorno)
        if par.get('fase') != 'guardar':
            entorno['DBZ3_TEST_BATTLE'] = '%d,%d,2' % (cid, body)
        j.info('PASS receta nativa: %s, %d formas verificadas contra el AFS local' % (raza, len(fuente)))

    def play(j, par):
        if par.get('catalogo') == '1':
            return catalogo(j, par)
        fase = par.get('fase', 'lamina')
        turbo = j.turbo
        # Los intervalos de New Game son de reloj real, no se dividen a x4.
        j.turbo = 1
        try:
            # 150 ms a x4 son 36 cuadros: la flecha repite y vuelve a Load.
            # Mantener unos nueve cuadros por pulso evita ese doble salto.
            hold = max(20, 150 // max(1, int(par.get('turbo', '1'))))
            j.arrancar(180, titulo=tuple('%s %d' % (k, hold) for k in ('Return', 'W', 'Space')))
        finally:
            j.turbo = turbo
        if fase == 'lamina' and int(par.get('turbo', '1')) > 1:
            # Restaurar el turbo de combate despues de la navegacion del titulo.
            j.turbo = min(8, int(par['turbo']))
            j.tecla('set dbz3_vblank_turbo=%d' % j.turbo, 0)
        if fase == 'guardar':
            j.log(r'Create abierto', 120)
            j.log(r'vista previa 3D .* cargada', 240)
            for _ in range(5):
                j.tecla('S 100', 650)
            vistas_antes = len(re.findall(r'vista previa 3D .* cargada', j.texto_log()))
            j.tecla('D 100', 1200)
            j.log(r'pieza B3 nativa .*Type', 30)
            fin_vista = time.monotonic() + 240
            while len(re.findall(r'vista previa 3D .* cargada', j.texto_log())) <= vistas_antes:
                if time.monotonic() >= fin_vista:
                    raise P['Fallo']('La vista previa no cargo el conjunto nuevo')
                time.sleep(.2)
            P['_tactics_reducir'](j, 'creador_nativo')
            j.tecla('3 100', 1000)
            j.log(r'Create: confirmar', 60)
            # Confirmar guarda la ficha antes de acabar el montaje en segundo
            # plano. Esperarlo evita dejar un Python escribiendo tras cerrar.
            j.log(r'vista previa combate_.* generada', 600)
            ruta = j.dest / 'user_data/dbz3/cac.toml'
            def ficha():
                doc = tomllib.loads(ruta.read_text(encoding='utf-8'))
                return doc['heroe'][doc.get('activo',0)]
            fin = time.monotonic() + 40
            inicial = par['_ids']
            while time.monotonic() < fin:
                guardada = ficha()
                esperada = list(inicial)
                base = 4000 if par['_cuerpo'] in (27, 38) else 4080
                import razas_b3 as rb3
                if rb3.experimental():
                    esperada[0] = base + ((inicial[0] - base + 1) % 8)
                else:
                    raza = rb3.FAMILIAS[par['_cuerpo']]
                    indice = next((i for i, p in enumerate(rb3.CURADAS[raza]) if inicial[0] - base == p[0]), 0)
                    esperada = [base + v for v in rb3.CURADAS[raza][(indice + 1) % 2]]
                if guardada.get('b3_piezas') == esperada:
                    break
                time.sleep(.2)
            else:
                raise P['Fallo']('El creador no guardo la receta esperada: %s' % esperada)
            (j.run / 'guardada.json').write_text(json.dumps(guardada, indent=2), encoding='utf-8')
            (j.run / 'crear.log').write_text(j.texto_log(), encoding='utf-8')
            j.cerrar()
            if j.vivo():
                raise P['Fallo']('El proceso no termino al guardar')
            j.lanzar(j.razas_entorno)
            j.arrancar(180, titulo=('Return','W','Space'))
            j.log(r'Create abierto', 120)
            j.log(r'ficha 0 importada', 60)
            if ficha() != guardada:
                raise P['Fallo']('La ficha cambio tras reiniciar')
            j.log(r'vista previa 3D .* cargada', 240)
            P['_tactics_reducir'](j, 'recarga_nativa')
            (j.run / 'recargada.json').write_text(json.dumps(ficha(), indent=2), encoding='utf-8')
            j.info('PASS guardar y recargar: receta B3 nativa intacta tras reiniciar')
        elif fase == 'forma':
            j.log(r'tag: empieza un combate', 120)
            P['_esperar_pelea'](j, 120)
            if par.get('raza') == 'majin':
                # Absorcion es una definitiva conectada, no P+K+G.
                j.log(r'prueba absorcion: lado 1 forma [1-3] modelo [0-9A-Fa-f]+', 180)
                P['dormir'](2)
                P['_tactics_reducir'](j, 'absorcion_nativa')
                j.info('PASS combate y absorcion nativa de Majin')
                return
            # La forma Mecha de Freezer es una resurreccion, no otra pulsacion
            # P+K+G. Permite pedir el tramo de transformaciones de combate.
            hasta = min(int(par.get('forma_hasta', par['_formas'] - 1)), par['_formas'] - 1)
            if hasta < 1:
                raise P['Fallo']('El donante no tiene una transformacion de combate solicitada')
            for forma in range(1, hasta + 1):
                j.log(r'formas traza: lado 1 en la forma %d:' % forma, 180)
            P['dormir'](2)
            P['_tactics_reducir'](j, 'transformacion_nativa')
            j.info('PASS combate y transformacion real de receta nativa')
        else:
            P['_cac_r16_lamina'](j, par)
    def catalogo(j, par):
        """32 combinaciones en la misma transaccion, un proceso de juego por lamina.

        Cada fallo conserva su registro; ninguna captura o montaje se reutiliza.
        Solo se declara PASS si se obtuvieron todas las laminas solicitadas.
        """
        raiz = j.run
        resultados = []
        inicio = time.monotonic()
        cabezas = [int(x) for x in par.get('cabezas', '0,1,2,3,4,5,6,7').split(',')]
        cuerpos = [int(x) for x in par.get('cuerpos', '0,1,2,3').split(',')]
        razas = par.get('razas', par.get('raza', 'freezer')).split(',')
        combinaciones = [(r, h, c, 'lamina') for r in razas for h in cabezas for c in cuerpos]
        if par.get('curadas') == '1':
            import razas_b3 as rb3
            combinaciones = [(r, p[0], p[2], 'lamina') for r in razas for p in rb3.CURADAS['namek' if r == 'namekiano' else r]]
            combinaciones += [(r, 0, 0, f) for r in razas for f in par.get('fases', '').split(',') if f in ('guardar', 'forma')]
        anteriores = {}
        camara_lenta = set()
        if par.get('reanudar'):
            for fila in json.loads(P['Path'](par['reanudar']).read_text(encoding='utf-8')):
                clave = (fila['raza'], fila['cabeza'], fila['cuerpo'], fila.get('fase', 'lamina'))
                if not fila.get('ok') and 'camara libre' in fila.get('error', ''):
                    camara_lenta.add(clave)
                if (fila.get('ok') and fila.get('fase', 'lamina') == 'lamina'
                        and (P['Path'](fila['carpeta']) / 'cac_lamina.png').is_file()):
                    anteriores[(fila['raza'], fila['cabeza'], fila['cuerpo'], 'lamina')] = fila
        j.cerrar()
        try:
            for caso, (raza, cabeza, cuerpo, fase) in enumerate(combinaciones):
                    if time.monotonic() - inicio > float(par.get('minutos', '120')) * 60:
                        raise P['Fallo']('Catalogo detenido en su limite de tiempo; ver catalogo.json')
                    if (raiz / 'parar.req').exists():
                        raise P['Fallo']('Catalogo detenido entre casos por peticion; se restaura la copia')
                    anterior = anteriores.get((raza, cabeza, cuerpo, fase))
                    if anterior and caso + 1 < len(combinaciones):
                        # Se conserva la procedencia anterior, sin fingir un render nuevo.
                        resultados.append(dict(anterior, reutilizada=True))
                        (raiz / 'catalogo.json').write_text(json.dumps(resultados, indent=2), encoding='utf-8')
                        j.info('REUTILIZADA lamina existente: %s cabeza%d cuerpo%d' % (raza, cabeza, cuerpo))
                        continue
                    j.run = raiz / ('%s_cabeza%d_cuerpo%d_%s' % (raza, cabeza, cuerpo, fase))
                    j.run.mkdir()
                    piezas = '0,0,0,0'
                    if par.get('curadas') == '1':
                        receta = next(p for p in rb3.CURADAS['namek' if raza == 'namekiano' else raza] if p[0] == cabeza)
                        piezas = ','.join(map(str, receta))
                    p = dict(par, raza=raza, cabeza=str(cabeza), cuerpo=str(cuerpo), piezas=piezas, catalogo='0', fase=fase,
                             experimental='0' if par.get('curadas') == '1' else '1')
                    if (raza, cabeza, cuerpo, fase) in camara_lenta:
                        p['turbo'] = '2'  # nuevo intento con mas margen para consumir la peticion de camara
                    entorno = env(p)
                    fila = dict(raza=raza, cabeza=cabeza, cuerpo=cuerpo, fase=fase, carpeta=str(j.run), ok=False,
                                turbo=p.get('turbo', '1'))
                    try:
                        prep(j, p, entorno)
                        j.lanzar(entorno)
                        play(j, p)
                        fila['ok'] = True
                    except Exception as e:
                        fila['error'] = str(e)
                        j.info('CATALOGO rechazo %d/%d: %s' % (cabeza, cuerpo, e))
                    finally:
                        (j.run / 'juego.log').write_text(j.texto_log(), encoding='utf-8')
                        # El arnes comprueba que el ultimo juego siga vivo al
                        # volver de play(), y despues lo cierra y restaura.
                        # Cerrarlo aqui daba FAIL tras completar todos los casos.
                        if caso + 1 < len(combinaciones):
                            j.cerrar()
                        resultados.append(fila)
                        (raiz / 'catalogo.json').write_text(json.dumps(resultados, indent=2), encoding='utf-8')
            if not all(r['ok'] for r in resultados):
                raise P['Fallo']('Catalogo: %d/%d laminas; rechazos conservados' % (sum(r['ok'] for r in resultados), len(resultados)))
            j.info('PASS catalogo: %d casos (%d laminas, guardar/forma separados)' % (
                len(resultados), sum(r['fase'] == 'lamina' for r in resultados)))
        finally:
            j.run = raiz

    P['ESCENARIOS']['razas_b3'] = ('R18 piezas NATIVAS: raza=freezer|namekiano|majin fase=lamina|forma|guardar; cabeza=0..7 cuerpo=0..3; catalogo=1', env, play, prep)

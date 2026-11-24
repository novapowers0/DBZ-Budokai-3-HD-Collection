"""R16 B: montaje y pruebas reales por pruebas.py, usando solo su copia."""
import json
import hashlib
import os
import shutil
import subprocess
import sys
import tomllib
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path


def _huellas_estado(dest):
    archivos = list((dest / 'mods').rglob('*'))
    archivos += [dest / 'user_data/dbz3' / n for n in ('cac.toml', 'rpg.toml')]
    return {str(p.relative_to(dest)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(archivos) if p.is_file()}


def _restaurar_mods(dest, run):
    """Conserva el resultado y devuelve los mods de la copia a su estado anterior."""
    estado = run / 'mods_antes.json'
    if (not estado.exists() or (run / 'copia_restaurada.txt').exists()
            or (run / 'restauracion_verificada.json').exists()):
        # La verificación se escribe DESPUÉS de devolver los datos; una brecha
        # documentada no debe aplicar otra vez los movimientos ya terminados.
        return
    mods = dest / 'mods'
    resultado = run / 'mods_despues'
    resultado.mkdir(exist_ok=True)
    previo = run / 'heroe_propio_anterior'
    actual = mods / 'heroe_propio'
    if actual.exists():
        shutil.move(str(actual), str(resultado / 'heroe_propio'))
    if previo.exists():
        shutil.move(str(previo), str(actual))
    roster = mods / '_roster'
    if (run / 'roster_anterior').exists():
        if roster.exists():
            shutil.move(str(roster), str(resultado / '_roster'))
        shutil.move(str(run / 'roster_anterior'), str(roster))
    elif (run / 'roster_ausente.txt').exists() and roster.exists():
        shutil.move(str(roster), str(resultado / '_roster'))
    cache = mods / '_forma_unica'
    if (run / 'forma_unica_anterior').exists():
        if cache.exists():
            shutil.move(str(cache), str(resultado / '_forma_unica'))
        shutil.move(str(run / 'forma_unica_anterior'), str(cache))
    elif (run / 'forma_unica_ausente.txt').exists() and cache.exists():
        shutil.move(str(cache), str(resultado / '_forma_unica'))
    for nombre, desactivado in json.loads(estado.read_text(encoding='utf-8')).items():
        marker = mods / nombre / '.disabled'
        if desactivado and marker.parent.exists():
            marker.touch()
        elif not desactivado and marker.exists():
            shutil.move(str(marker), str(resultado / (nombre + '.disabled')))
    originales = run / 'ficheros_antes.json'
    existian = json.loads(originales.read_text(encoding='utf-8')) if originales.exists() else {}
    for nombre in ('cac.toml', 'rpg.toml', 'dbz3_user.toml'):
        p = dest / nombre if nombre == 'dbz3_user.toml' else dest / 'user_data/dbz3' / nombre
        copia = run / ('antes_' + nombre)
        if copia.exists() or existian.get(nombre) is False:
            if p.exists():
                shutil.move(str(p), str(run / ('despues_' + nombre)))
            if copia.exists():
                shutil.copy2(copia, p)
    huellas = run / 'huellas_antes.json'
    if huellas.exists():
        antes = json.loads(huellas.read_text(encoding='utf-8'))
        despues = _huellas_estado(dest)
        diferencias = [p for p in sorted(antes.keys() | despues.keys())
                       if antes.get(p) != despues.get(p)]
        (run / 'restauracion_verificada.json').write_text(
            json.dumps(dict(ok=not diferencias, archivos=len(antes), diferencias=diferencias), indent=2),
            encoding='utf-8')
        if diferencias:
            raise RuntimeError('La restauracion B difiere en: %s' % ', '.join(diferencias[:8]))
    (run / 'copia_restaurada.txt').write_text(
        'Mods y fichas restaurados; resultados conservados. El TOML de pruebas lo normaliza pruebas.py al cerrar.\n',
        encoding='utf-8')


def registrar(P):
    def entorno(par):
        e = dict(DBZ3_TEST_CFG='dbz3_cac_xv2=false', DBZ3_CAC_XV2='0', DBZ3_TEST_TURBO='1')
        if par.get('fase') == 'forma':
            e.update(DBZ3_TEST_FORMAS='1', DBZ3_TEST_PKG='1')
        elif par.get('fase') == 'creador':
            e.update(DBZ3_TEST_HEROE_ABRIR='2', DBZ3_TEST_MODO='heroe,0', DBZ3_TEST_HEROE_CAM='2')
        else:
            d, l = {'frente': (9, 0), 'derecha': (.15, 9),
                    'espalda': (-9, 0), 'izquierda': (.15, -9)}[par.get('vista', 'frente')]
            # El modo cabeza fija toma dist/lado DEL TEXTO, no de *_BRAZO.
            # ParseModo activa cabeza_fija con una distancia positiva; después
            # *_DIST_BRAZO admite el signo negativo para mirar desde detrás.
            modo = 'cabeza frente %s %s 0' % (abs(d), l)
            e.update(DBZ3_TEST_CARA='0', DBZ3_TEST_CARA_HUESO=modo,
                     DBZ3_TEST_CARA_DIST=str(d), DBZ3_TEST_CARA_LADO=str(l),
                     DBZ3_TEST_CARA_QUIETO='1',
                     DBZ3_TEST_CARA_DIST_BRAZO=str(d), DBZ3_TEST_CARA_LADO_BRAZO=str(l),
                     DBZ3_TEST_CARA_ESPERA='15', DBZ3_TEST_CARA_DURACION='0')
        return e

    def preparar(j, par, env):
        # Recuperar interrupciones del host, ya dentro del cerrojo de pruebas.py.
        for anterior in sorted(j.run.parent.glob('*_smoke_a_partes_b'), reverse=True):
            if anterior != j.run:
                _restaurar_mods(j.dest, anterior)
        cuerpo = int(par.get('cuerpo', '11'))
        if cuerpo not in (11, 34, 35, 36):
            raise P['Fallo']('Base incompatible con partes B')
        defecto = '3101,3121,-1,-1' if cuerpo == 11 else '3203,3221,3242,-1'
        ids = list(map(int, par.get('piezas', defecto).split(',')))
        if len(ids) != 4:
            raise P['Fallo']('Se necesitan cuatro selecciones')
        if par.get('base') == '1':
            ids = [-1] * 4
        traje = int(par.get('traje', '0'))
        ficha = ('cuerpo=%d;traje=%d;pelo=0;cara=0;partes=0,0,0|0,0,0|0,0,0|0,0,0;'
                 'c1=0,0,0;c2=0,0,0;logo=0;scouter=0;nombre=R16 Partes B;donante=%d'
                 % (cuerpo, traje, cuerpo))
        ficha += ''.join(';b3_%s=%d' % (z, n) for z, n in zip(('torso', 'piernas', 'guantes', 'botas'), ids))
        mods = j.dest / 'mods'
        (j.run / 'huellas_antes.json').write_text(json.dumps(_huellas_estado(j.dest)), encoding='utf-8')
        if (mods / '_roster').exists():
            shutil.copytree(mods / '_roster', j.run / 'roster_anterior')
        else:
            (j.run / 'roster_ausente.txt').touch()
        if (mods / '_forma_unica').exists():
            shutil.copytree(mods / '_forma_unica', j.run / 'forma_unica_anterior')
        else:
            (j.run / 'forma_unica_ausente.txt').touch()
        previo = mods / 'heroe_propio'
        if previo.exists():
            shutil.move(str(previo), str(j.run / 'heroe_propio_anterior'))
        estados = {}
        for d in mods.iterdir():
            if d.is_dir() and not d.name.startswith('_'):
                estados[d.name] = (d / '.disabled').exists()
                (d / '.disabled').touch()
        (j.run / 'mods_antes.json').write_text(json.dumps(estados), encoding='utf-8')
        existian = {}
        for nombre in ('cac.toml', 'rpg.toml', 'dbz3_user.toml'):
            p = j.dest / nombre if nombre == 'dbz3_user.toml' else j.dest / 'user_data/dbz3' / nombre
            existian[nombre] = p.exists()
            if p.exists():
                shutil.copy2(p, j.run / ('antes_' + nombre))
        (j.run / 'ficheros_antes.json').write_text(json.dumps(existian), encoding='utf-8')
        cerrar = j.cerrar

        def cerrar_y_restaurar():
            try:
                cerrar()
            finally:
                _restaurar_mods(j.dest, j.run)
        j.cerrar = cerrar_y_restaurar
        P['_heroe_pieza_receta'](j.dest, 0, -1, cuerpo=cuerpo, traje=traje)
        cac = j.dest / 'user_data/dbz3/cac.toml'
        texto = cac.read_text(encoding='utf-8')
        texto = texto.replace('raza = 0\n', 'raza = %d\n' % (1 if cuerpo == 11 else 3))
        texto = texto.replace('b3_piezas = [-1, -1, -1, -1]', 'b3_piezas = [%s]' % ', '.join(map(str, ids)))
        texto = texto.replace('moveset = -1', 'moveset = %d' % cuerpo)
        cac.write_text(texto, encoding='utf-8')
        P['_apagar_xv2'](j.dest)
        out = j.run / 'vista_b'
        out.mkdir(exist_ok=True)

        def ejecutar(cmd, nombre):
            r = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=900,
                               env=dict(os.environ, DBZ3_CAC_XV2='0'))
            (j.run / nombre).write_text(r.stdout + r.stderr, encoding='utf-8')
            if r.returncode:
                raise P['Fallo']('%s: %s' % (nombre, (r.stdout + r.stderr)[-900:]))
        # La misma funcion del producto; evita montar tres campanas DU ajenas a esta prueba.
        sys.path[:0] = [str(P['R'] / 'mod center hd/cac'), str(P['R'] / 'mod center hd')]
        import vista_heroe as vh
        import partes_originales as po
        work = j.run / 'afs_b'
        work.mkdir(exist_ok=True)
        afs = vh.rb.Afs(str(P['R'] / 'us/data_cmn.afs'), str(work))
        with (j.run / 'preparar_b.log').open('w', encoding='utf-8') as log:
            with redirect_stdout(log), redirect_stderr(log):
                resultado = vh.escribir_mod_combate(str(j.dest), 1, vh.leer_ficha(ficha), ficha, afs)
                print(resultado)
        mod = mods / 'heroe_propio'
        doc = tomllib.loads((mod / 'personaje.toml').read_text(encoding='utf-8'))['personaje']
        source = vh.rb.IDS[cuerpo]
        fid = source['models'][traje * source['models_per_costume']]
        esperado, _ = po.aplicar(afs.entry(fid), vh.leer_ficha(ficha), j.dest)
        formas = 3 if cuerpo == 11 and any(i >= 0 for i in ids) else 1
        if doc.get('formas') != formas or len(doc['modelos']) != formas:
            raise P['Fallo']('Cadena de formas B incorrecta')
        report = []
        for i, f in enumerate(doc['modelos']):
            actual = (mod / f).read_bytes()
            if po.huella_geometria(actual) != po.huella_geometria(esperado):
                raise P['Fallo']('La forma %d no conserva las piezas B seleccionadas' % i)
            report.append(dict(forma=i, fid=fid, geometria=po.huella_geometria(actual)))
        (j.run / 'formas_b.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        caps = [x for x in source.get('form_caps', []) if x] if formas > 1 else []
        if caps:
            with (mod / 'personaje.toml').open('a', encoding='utf-8') as f:
                f.write('\ncapsulas_nativas = [%s]\n' % ', '.join(map(str, caps)))
        ejecutar([sys.executable, str(P['R'] / 'mod center hd/roster_build.py'),
                  'construir', '--mods', str(mods)], 'roster_b.log')
        filas = tomllib.loads((mods / '_roster/roster.toml').read_text(encoding='utf-8'))['personaje']
        cid = next(x['id'] for x in filas if x['nombre'] == 'R16 PARTES B')
        if par.get('fase') != 'creador':
            env['DBZ3_TEST_BATTLE'] = '%d,%d,2' % (cid, cuerpo)
        P['_quitar_partida_heroe'](j.dest / 'user_data/dbz3/rpg.toml')
        j.info('PASS montaje B: cuerpo %d, piezas %s, %d modelos con geometria verificada' % (cuerpo, ids, formas))

    def jugar(j, par):
        if par.get('fase') == 'creador':
            j.log(r'Create abierto', 120)
            P['dormir'](10)
            for n in range(4):
                P['_tactics_reducir'](j, 'b_creador_%d' % n)
                if n < 3:
                    j.tecla('E', 150)
                    P['dormir'](1)
            return
        j.log(r'tag: empieza un combate', 120)
        P['_esperar_pelea'](j, 120)
        if par.get('fase') == 'forma':
            j.log(r'formas traza: lado 1 en la forma [1-9]', 120)
            j.log(r'formas: prueba: tope en la forma', 180)
            P['dormir'](2)
            P['_tactics_reducir'](j, 'b_forma_final')
            j.info('PASS transformacion B: Piccolo hasta la fusion final')
        else:
            j.log(r'dbz3 cinematica: sonda de cara', 40)
            P['dormir'](2)
            P['_tactics_reducir'](j, 'b_' + par.get('vista', 'frente'))
            j.info('PASS combate B: modelo verificado y render real; aspecto pendiente de revisar')

    P['ESCENARIOS']['partes_b'] = ('R16 piezas namekianas/majin: montaje, combate, formas y capturas 0.5',
                                  entorno, jugar, preparar)

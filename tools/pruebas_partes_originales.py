"""Escenarios R16 por la puerta unica pruebas.py; solo usa la copia elegida."""
import json
import os
import re
import shutil
import subprocess
import sys
import tomllib


def registrar(P):
    def env(par):
        e = dict(DBZ3_CAC_XV2='0', DBZ3_TEST_CFG='dbz3_cac_xv2=false', DBZ3_TEST_TURBO='1')
        if par.get('fase') == 'forma':
            e.update(DBZ3_TEST_FORMAS='1', DBZ3_TEST_PKG='1')
        elif par.get('fase') == 'creador':
            e.update(DBZ3_TEST_HEROE_ABRIR='2')
        else:
            views = {'frente': (26, 0), 'derecha': (.15, 26), 'espalda': (-26, 0), 'izquierda': (.15, -26)}
            d, l = views[par.get('vista', 'frente')]
            e.update(DBZ3_TEST_CARA='0', DBZ3_TEST_CARA_HUESO='cabeza frente 20 0 -3',
                     DBZ3_TEST_CARA_DIST_BRAZO=str(d), DBZ3_TEST_CARA_LADO_BRAZO=str(l),
                     DBZ3_TEST_CARA_DURACION='0', DBZ3_TEST_CARA_ESPERA='240')
        return e

    def prep(j, par, e):
        request = j.dest / 'dbz3_cam.req'
        if request.exists():
            shutil.move(str(request), str(j.run / 'camara_previa.req.bak'))
        body = int(par.get('cuerpo', '27'))
        if body not in (27, 38):
            raise P['Fallo']('Cuerpo original incompatible')
        ids = [int(x) for x in par.get('piezas', '3000,3020,3040,3061').split(',')]
        if len(ids) != 4:
            raise P['Fallo']('Se necesitan cuatro selecciones')
        if par.get('base') == '1':
            ids = [-1] * 4
        if par.get('blender'):
            from pathlib import Path
            sys.path.insert(0, str(P['R'] / 'tools'))
            import blender_partes_b3 as bp
            shutil.copy2(P['R'] / 'mod center hd/cac/piezas_cac.toml',
                         j.dest / 'mod center hd/cac/piezas_cac.toml')
            entries = sorted(Path(par['blender']).glob('*/editado.json'))
            bp.registrar(entries, j.dest, 3080, 'Cabeza Blender local R16')
            ids = [3080, -1, -1, -1]
        zonas = ('torso', 'piernas', 'guantes', 'botas')
        ficha = ('cuerpo=%d;traje=0;pelo=0;cara=0;partes=0,0,0|0,0,0|0,0,0|0,0,0;'
                 'c1=0,0,0;c2=0,0,0;logo=0;scouter=0;nombre=R16 Freezer;donante=%d' % (body, body))
        ficha += ''.join(';b3_%s=%d' % (z, i) for z, i in zip(zonas, ids))
        # Evitar que otro mod de la copia pise esta geometria. Conservar todos los archivos.
        for d in (j.dest / 'mods').iterdir():
            if d.is_dir() and not d.name.startswith('_') and d.name != 'heroe_propio':
                (d / '.disabled').touch()
        args = P['_heroe_pieza_receta'](j.dest, 0, -1, cuerpo=body)
        cac = j.dest / 'user_data' / 'dbz3' / 'cac.toml'
        text = cac.read_text(encoding='utf-8').replace('raza = 0\n', 'raza = 3\n')
        text = text.replace('b3_piezas = [-1, -1, -1, -1]', 'b3_piezas = [%s]' % ', '.join(map(str, ids)))
        text = text.replace('moveset = -1', 'moveset = %d' % body)
        cac.write_text(text, encoding='utf-8')
        P['_apagar_xv2'](j.dest)
        out = j.run / 'vista_heroe'
        out.mkdir(exist_ok=True)
        cmd = [sys.executable, '-X', 'utf8', str(j.dest / 'mod center hd/cac/vista_heroe.py'),
               '--juego', str(j.dest), '--us', str(P['R'] / 'us'), '--salida', str(out),
               '--ficha', ficha, '--combate', '1']
        def run(cmd, name):
            r = subprocess.run(cmd, env=dict(os.environ, DBZ3_CAC_XV2='0'), capture_output=True,
                               text=True, errors='replace', timeout=900)
            (j.run / name).write_text(r.stdout + r.stderr, encoding='utf-8')
            if r.returncode:
                raise P['Fallo']('%s: %s' % (name, (r.stdout + r.stderr)[-700:]))
        run(cmd, 'preparar_original.log')
        mod = j.dest / 'mods' / 'heroe_propio'
        p = mod / 'personaje.toml'
        doc = tomllib.loads(p.read_text(encoding='utf-8'))['personaje']
        forms = 6 if body == 27 else 3
        if doc.get('formas') != forms or len(doc.get('modelos', [])) != forms:
            raise P['Fallo']('No se construyo la cadena nativa de formas')
        # Guardar prueba completa de geometria por forma, no solo nombres en el TOML.
        sys.path[:0] = [str(P['R'] / 'mod center hd/cac'), str(P['R'] / 'mod center hd')]
        import vista_heroe as vh
        import partes_originales as po
        (j.run / 'afs').mkdir(exist_ok=True)
        afs = vh.rb.Afs(str(P['R'] / 'us/data_cmn.afs'), str(j.run / 'afs'))
        report = []
        for f, filename in enumerate(doc['modelos']):
            fid = vh.rb.IDS[body]['models'][f]
            raw = afs.entry(fid)
            expected, _ = po.aplicar(raw, vh.leer_ficha(ficha), j.dest)
            actual = (mod / filename).read_bytes()
            if po.huella_geometria(expected) != po.huella_geometria(actual):
                raise P['Fallo']('La forma %d no lleva la geometria original seleccionada' % f)
            report.append(dict(forma=f, fid=fid, geometria=po.huella_geometria(actual)))
        (j.run / 'formas_originales.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        # Equipar las capsulas NATIVAS, igual que un personaje con sus formas desbloqueadas.
        caps = [i for i in vh.rb.IDS[body]['form_caps'] if i]
        with p.open('a', encoding='utf-8') as f:
            f.write('\ncapsulas_nativas = [%s]\n' % ', '.join(map(str, caps)))
        if par.get('modelo') == 'final':
            shutil.copy2(mod / 'modelos/base.bin', j.run / 'base_original.bin')
            final = 3 if body == 27 else 1  # Mecha/Metal son variantes, no el final organico.
            shutil.copy2(mod / ('modelos/ssj%d.bin' % final), mod / 'modelos/base.bin')
        dis = mod / '.disabled'
        if dis.exists():
            shutil.move(str(dis), str(j.run / 'heroe.disabled.bak'))
        run([sys.executable, str(P['R'] / 'mod center hd/roster_build.py'), 'construir', '--mods', str(j.dest / 'mods')], 'roster_original.log')
        rows = tomllib.loads((j.dest / 'mods/_roster/roster.toml').read_text(encoding='utf-8'))['personaje']
        cid = next(x['id'] for x in rows if x['nombre'] == 'R16 FREEZER')
        par['_id'] = cid
        if par.get('fase') != 'creador':
            e['DBZ3_TEST_BATTLE'] = '%d,%d,2' % (cid, body)
        P['_quitar_partida_heroe'](j.dest / 'user_data/dbz3/rpg.toml')
        j.info('PASS montaje original: %d modelos y fichas de las %d formas' % (len(report), forms))

    def play(j, par):
        if par.get('fase') == 'creador':
            j.log(r'Create abierto', 90)
            for _ in range(5):
                j.tecla('S', 250)
            j.tecla('D', 500)
            j.log(r'pieza B3.*Cresta dorsal|pieza B3.*Cupula alta', 20)
            P['dormir'](8)
            for k in range(4):
                P['_tactics_reducir'](j, 'creador_vista%d' % k)
                if k < 3:
                    j.tecla('E', 100)
                    P['dormir'](1)
            j.tecla('Escape', 500)
            return
        j.log(r'tag: empieza un combate', 120)
        P['_esperar_pelea'](j, 120)
        if par.get('fase') == 'forma':
            final = 4 if int(par.get('cuerpo', '27')) == 27 else 1
            for form in range(1, final + 1):
                j.log(r'formas traza: lado 1 en la forma %d:' % form, 90)
            P['dormir'](2)
            P['_tactics_reducir'](j, 'forma_final_real')
            j.info('PASS transformacion real: lado 1 hasta la forma organica %d por P+K+G' % final)
        else:
            j.log(r'dbz3 cinematica: sonda de cara', 40)
            P['dormir'](4)
            P['_tactics_reducir'](j, 'original_' + par.get('modelo', 'base') + '_' + par.get('vista', 'frente'))
            j.info('PASS combate original: geometria verificada antes de montar, render en juego')
    P['ESCENARIOS']['partes_originales'] = ('R16 Freezer/Cooler: geometria propia, formas, creador y fotos 0.5', env, play, prep)

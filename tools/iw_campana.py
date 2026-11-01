"""IW: pruebas de campaña aisladas, con selección explícita de imagen y misión.

python tools/iw_campana.py dificultad --region us --copia s5_b3_a
python tools/iw_campana.py dm --region eu --copia s5_b3_b --misiones 1-96
python tools/iw_campana.py dm --region us --copia s5_b3_b --continuo --turbo 4
python tools/iw_campana.py dm --region eu --copia s5_b3_c --continuo --turbo 4

--continuo mantiene una sola instancia y comprueba cada solicitud, MISSION START,
fin y resultado. La petición IPC solo está habilitada en esta copia de pruebas.

Un PASS de dm acredita ficha -> combate -> resultado, no el cumplimiento de la
regla por el piloto RINGOUT. El resultado conserva victoria/derrota por separado.
No reutiliza resultados previos ni cambia las plazas de pruebas.py.
"""
import argparse
import json
import os
import re
import hashlib
import shutil
import struct
import subprocess
import sys
import time
import tomllib
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

import pruebas as P


def numeros(texto):
    out = []
    for parte in texto.split(','):
        a, sep, b = parte.partition('-')
        out.extend(range(int(a), int(b) + 1) if sep else [int(a)])
    if not out or len(set(out)) != len(out) or any(n < 1 or n > 96 for n in out):
        raise argparse.ArgumentTypeError('misiones únicas entre 1 y 96')
    return out


def esperar(j, patron, segundos=120, saltar_video=False):
    fin = time.monotonic() + segundos
    ultimo = 0
    while time.monotonic() < fin:
        texto = j.texto_log()
        if re.search(patron, texto):
            return texto
        if not j.vivo():
            raise P.Fallo('el juego terminó esperando ' + patron)
        # Solo se pulsan las escenas que han empezado realmente. No enviar A al
        # resultado/ficha mientras se espera un combate.
        videos = len(re.findall(r'video de la historia .*: reproduciendo', texto))
        if saltar_video and videos > ultimo:
            ultimo = videos
            P.dormir(1)
            j.tecla('Space', 500)
        time.sleep(.2)
    raise P.Fallo(f'no apareció {patron!r} en {segundos} s')


def ficha(j, n):
    esperar(j, r'misiones: iw: mapa de Dragon Mission')
    # El aviso del coordinador precede al primer dibujo del overlay. Pulsar A
    # ahí puede llegar todavía al menú B3 y abrir Dragon Universe (M78).
    esperar(j, r'iw mapa: intro acabada', 180)
    # El mapa bloquea el mando durante introducción, scroll y desbloqueos.
    j.tecla_hasta_log('Space', rf'misiones: iw: ficha de la mision {n}\b', 90, 1)


def publicar_solicitud(ruta, numero):
    temporal = ruta.with_suffix('.tmp')
    temporal.write_text(str(numero), encoding='ascii')
    fin = time.monotonic() + 3
    while True:
        try:
            os.replace(temporal, ruta)
            return
        except PermissionError:
            if time.monotonic() >= fin: raise
            time.sleep(.01)


def cadena_continua(j, par):
    lista = [int(n) for n in par['misiones'].split(',')]
    imagen = 'EU' if par['region'] == 'eu' else 'US'
    esperar(j, rf'using {imagen} image config', 20)
    resultados = []
    ruta = j.dest / 'dbz3_iw_mision.req'
    corte = 0
    anterior = None
    def esperar_nuevo(patron, marca, segundos=120, tecla=None):
        fin = time.monotonic() + segundos
        siguiente = 0
        while time.monotonic() < fin:
            texto = j.texto_log()[marca:]
            if re.search(patron, texto): return texto
            if not j.vivo(): raise P.Fallo('el juego terminó en la cadena')
            if tecla and time.monotonic() >= siguiente:
                j.tecla(tecla, 0)
                siguiente = time.monotonic() + .8
            time.sleep(.15)
        raise P.Fallo(f'cadena: no apareció {patron!r}')
    for n in lista:
        marca = len(j.texto_log()) if anterior is not None else 0
        publicar_solicitud(ruta, n)
        if anterior is not None:
            j.esperar_pantalla([5], 120)
            time.sleep(.8)   # menú nativo reconstruido; overlay ya dibujado
        esperar_nuevo(rf'iw cadena: solicitud mision {n} aceptada', marca, 180,
                      None if anterior is None else 'Backspace' if anterior == 'derrota' else 'Space')
        esperar_nuevo(r'iw mapa: intro acabada', marca, 180)
        esperar_nuevo(rf'misiones: iw: ficha de la mision {n}\b', marca, 120, 'Space')
        j.tecla('Space', 250)
        esperar_nuevo(rf'MISSION START mision {n}\b', marca, 30)
        # Las escenas previas se pueden saltar con A. Dejar de pulsar al
        # aparecer la petición real de combate, sin tocar su resultado.
        esperar_nuevo(r'misiones: iw combate', marca, 180, 'Space')
        esperar_nuevo(r'misiones: fin del combate \(iw paso 1\)', marca, 300)
        texto = esperar_nuevo(rf'misiones: iw: mision {n} superada|misiones: iw paso 1/1: perdido', marca, 60)
        anterior = 'victoria' if re.search(rf'misiones: iw: mision {n} superada', texto) else 'derrota'
        resultados.append({'mision': n, 'ok': True, 'resultado': anterior, 'regla_certificada': False})
        (j.run / 'iw_cadena_resultados.json').write_text(json.dumps(resultados, indent=2), encoding='utf8')
        j.info(f'PASS flujo continuo {par["region"]} misión {n}: {anterior}; {len(resultados)}/{len(lista)}')
    j.captura('cadena_final')
    if [r['mision'] for r in resultados] != lista: raise P.Fallo('la cadena perdió una misión')
    j.info(f'PASS iw_dm continuo {par["region"]}: {len(resultados)}/{len(lista)}; una instancia, sin caché')


def escenario(j, par):
    n = int(par['mision'])
    imagen = 'EU' if par['region'] == 'eu' else 'US'
    esperar(j, rf'dual-region core detected .*using {imagen} image config', 20)
    ficha(j, n)
    if par.get('tipo') == 'habilidades':
        return habilidades(j, par)
    if 'dificultad' in par:
        dif = int(par['dificultad'])
        for _ in range(abs(dif - 2)):
            j.tecla('A' if dif < 2 else 'D', 350)
    j.tecla('Space', 400)
    texto = esperar(j, rf'MISSION START mision {n}\b', 25)
    texto = esperar(j, r'misiones: iw combate', 120, saltar_video=True)
    if 'dificultad' in par:
        esperado = (0, 0, 1, 2, 2)[int(par['dificultad'])]
        # La presentación puede tardar varios minutos con otras ventanas
        # compilando shaders. El nivel ya se aplica al crear el administrador
        # (estado 1), antes de que la IA lea su tabla, no al terminar la intro.
        fin = time.monotonic() + 180
        while time.monotonic() < fin:
            dato = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
            direccion = struct.unpack('>I', dato)[0] if dato and len(dato) == 4 else 0
            nivel = P.leer(j.h, P.GBASE + direccion + 35573, 1) if direccion else None
            estado = P.leer(j.h, P.GBASE + direccion + 35128, 4) if direccion else None
            if nivel == bytes([esperado]) and estado and 1 <= struct.unpack('>I', estado)[0] <= 4:
                break
            time.sleep(.2)
        else:
            raise P.Fallo('el nivel elegido no llegó al administrador del combate')
        # Comprueba el byte REAL del administrador de combate, además del log.
        mgr = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
        if not mgr or len(mgr) != 4:
            raise P.Fallo('no se puede leer el administrador del combate')
        direccion = struct.unpack('>I', mgr)[0]
        nivel = P.leer(j.h, P.GBASE + direccion + 35573, 1)
        if nivel != bytes([esperado]):
            raise P.Fallo(f'COM real {nivel!r}, esperado {esperado}')
        j.info(f'PASS dificultad {par["dificultad"]}: COM real={esperado}')
        j.captura('combate_dificultad_' + par['dificultad'])
        return
    esperar(j, r'misiones: fin del combate \(iw paso 1\)', 420, saltar_video=True)
    texto = esperar(j, rf'misiones: iw: mision {n} superada|misiones: iw paso 1/1: perdido', 60)
    resultado = 'victoria' if re.search(rf'misiones: iw: mision {n} superada', texto) else 'derrota'
    j.info(f'PASS flujo misión {n}: {resultado} (RINGOUT; no certifica la regla)')
    P.dormir(2)
    j.captura('resultado_' + resultado)
    (j.run / 'iw_resultado.json').write_text(json.dumps({'mision': n, 'resultado': resultado,
        'alcance': 'flujo', 'regla_certificada': False}, indent=2), encoding='utf-8')


def habilidades(j, par):
    ruta = j.dest / 'user_data/dbz3/pasarela/iw_habilidades.toml'
    j.tecla('W', 400)  # MISSION START -> SKILL EDIT
    j.tecla('Space', 400)
    esperar(j, r'Skill Edit mision 12, personaje 11, fijas 11000000', 30)
    j.captura('a_habilidades')
    if par['fase'] == 'recargar':
        tabla = tomllib.loads(ruta.read_text(encoding='utf8'))
        if tabla['estado']['885'] != 224: raise P.Fallo('no se conservó el inventario')
        esperar(j, r'equipo 469,115,0,0,0,0,0,0 \(EU 0x2FAD10\)', 10)
        if '12' in tabla.get('equipo', {}): raise P.Fallo('se inventó un perfil persistente por misión')
        j.info('PASS recarga: inventario conservado, equipo de misión temporal reiniciado como IW')
        return
    if par['fase'] == 'invalido':
        esperar(j, r'iw habilidades: no se modifica', 10)
        for _ in range(2): j.tecla('S', 200)
        j.tecla('Space', 300)
        esperar(j, r'posicion 4 protegida', 10)
        if ruta.read_text(encoding='utf8') != 'version = "invalida"\n':
            raise P.Fallo('se modificó el guardado de versión desconocida')
        j.info('PASS guardado inválido conservado y edición rechazada')
        return
    if par['fase'] in ('editar', 'variantes'):
        # El original empieza en la primera posición editable (2 en M12).
        # Arriba debe saltar las dos fijas hasta OK, y abajo volver a 2.
        j.tecla('W', 250)
        esperar(j, r'cursor de bandeja 2 -> 8', 10)
        j.tecla('S', 250)
        esperar(j, r'cursor de bandeja 8 -> 2', 10)
        for _ in range(2): j.tecla('S', 200)
        j.tecla('Space', 300)
        # #SCD: Senzu 885 es categoría 1 (posición 3), no una verde de
        # categoría 0. Debe excluirse en 4 aunque esté poseída.
        esperar(j, r'posicion 4: 0 opciones poseidas', 15)
        j.tecla('Backspace', 250); j.tecla('W', 250); j.tecla('Space', 250)
        esperar(j, r'posicion 3: 1 opciones poseidas', 15)
        j.tecla('S', 250)
        j.tecla('Space', 250)
        esperar(j, r'mision 12 posicion 3 equipada 885', 15)
        j.captura('b_habilidades_equipadas')
    j.tecla('Backspace', 500)
    esperar(j, r'Skill Edit cerrado, mision 12', 20)
    tabla = tomllib.loads(ruta.read_text(encoding='utf8'))
    if '12' in tabla.get('equipo', {}):
        raise P.Fallo('DM no debe guardar un banco histórico de equipos por misión')
    if tabla['estado']['885'] != 224:
        raise P.Fallo('la cápsula poseída se perdió')
    if par['fase'] in ('editar', 'variantes') and not list(ruta.parent.glob('iw_habilidades.toml.anterior.*')):
        raise P.Fallo('no se conservó la revisión anterior')
    j.tecla('Space', 300)  # misma misión: se conserva el equipo del contexto
    fija = 471 if par['fase'] == 'variantes' else 469
    esperar(j, rf'equipo {fija},115,0,885,0,0,0,0 \(EU 0x2FAD10\)', 15)
    j.tecla('Backspace', 400)
    j.tecla('S', 250)  # SKILL EDIT -> MISSION START
    corte = len(j.texto_log())
    j.tecla('Space', 300)
    if par['fase'] == 'variantes':
        esperar(j, r'mision 12 no lanzada: capsula 471 sin equivalente', 20)
        if re.search(r'hub: combate nativo', j.texto_log()[corte:]): raise P.Fallo('se lanzó un combate con una variante falsa')
        j.info('PASS variante fija: conserva nivel 3 poseído; rechaza su sustitución por un nivel o efecto B3 no equivalente')
        return
    esperar(j, r'mision 12, jugador 11: 3 capsulas aplicadas antes del lector \(61 59 254\)', 180)
    fin = time.monotonic() + 180
    while time.monotonic() < fin:
        dato = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
        mgr = struct.unpack('>I', dato)[0] if dato and len(dato) == 4 else 0
        dato = P.leer(j.h, P.GBASE + mgr + 2096 + 7412, 4) if mgr else None
        rec = struct.unpack('>I', dato)[0] if dato and len(dato) == 4 else 0
        equipo = P.leer(j.h, P.GBASE + rec + 52, 8) if rec else None
        if equipo and struct.unpack('>4H', equipo) == (3, 61, 59, 254): break
        time.sleep(.2)
    else: raise P.Fallo('el equipo guardado no llegó al registro leído por el luchador')
    j.info('PASS habilidades: fijas protegidas, ocho posiciones temporales conservadas y 3 cápsulas reales en combate; inventario guardado')
    j.captura('c_equipo_en_combate')


def seleccion(j, par):
    clave = par.get('clave', '0x7D6')
    corta = clave.lower()
    # Indices de la ficha != IDs: NB012 indice 6 es Vegeta/7.
    ib, ia, base, alterno = {
        '0x7D6': (10, 2, 10, 2), '0x7E0': (4, 9, 4, 9),
        '0x7D3': (0, 11, 0, 11), '0x7DA': (6, 10, 7, 10),
        '0x7D1': (0, 11, 0, 11), '0x7D2': (0, 11, 0, 11),
        '0x7DF': (0, 3, 0, 3), '0x7E1': (15, 14, 15, 14),
    }[clave]
    bloqueo = clave in ('0x7D1', '0x7D2', '0x7DF', '0x7DA', '0x7E1')
    esperar(j, r'using (EU|US) image config', 20)
    esperar(j, r'misiones: iw: mapa de Dragon Mission', 120)
    esperar(j, r'iw mapa: intro acabada', 180)
    j.tecla_hasta_log('Space', rf'iw seleccion: ficha minijuego {corta}, personaje {base}\b', 40, 1)
    j.captura('a_ficha_minijuego')
    j.tecla('S', 250); j.tecla('D', 250)
    esperar(j, rf'iw seleccion: {corta} {ib} -> {ia} \(personaje {alterno}, capsula 2126=0x80\)', 15)
    j.tecla('A', 250)
    esperar(j, rf'iw seleccion: {corta} {ia} -> {ib} \(personaje {base}', 15)
    j.tecla('D', 250)
    j.tecla('S', 250); j.tecla('Space', 250)
    elegido = alterno
    if bloqueo:
        texto = esperar(j, rf'iw seleccion: {corta}, personaje {alterno}: faltan .*; .*no se sustituye el actor', 20)
        if re.search(r'iw seleccion: modulo|hub: combate nativo', texto):
            raise P.Fallo('se lanzó un combate a pesar de faltar la animación del personaje')
        j.info(f'PASS rechazo {clave}: personaje {alterno}, animación propia pendiente, sin sustitución')
        # Elegir explícitamente el fijo: el módulo no lo ha sustituido en secreto.
        j.tecla('W', 250); j.tecla('A', 250)
        esperar(j, rf'iw seleccion: {corta} {ia} -> {ib} \(personaje {base}', 15)
        j.tecla('S', 250); j.tecla('Space', 250)
        elegido = base
    esperar(j, rf'iw seleccion: modulo {corta} recibe personaje {elegido}\b', 120, saltar_video=True)
    esperar(j, rf'hub: combate nativo: id {elegido} contra id ', 120, saltar_video=True)
    P._esperar_pelea(j, 180)
    # Dirección verificada igual en US/EU (src/eu_map_gen.h, kCfg).
    actor = P.leer(j.h, P.GBASE + 0x8245EFC0 + 32, 2)
    if actor != struct.pack('>H', elegido):
        raise P.Fallo(f'actor real {actor!r}, esperado {elegido}')
    (j.run / 'iw_seleccion.json').write_text(json.dumps(dict(clave=clave, solicitado=alterno,
        alternativa_portada=not bloqueo, rechazado_sin_sustituir=bloqueo, actor_arrancado=elegido)), encoding='utf8')
    j.info(f'PASS selección {clave}: módulo recibe {elegido}, actor real confirmado; '
           + ('alternativa rechazada por animaciones pendientes' if bloqueo else f'alternativa {alterno} aplicada'))


def deseos(j, par):
    texto = esperar(j, r'iw shenron: CapsulePick_Start_LoadCmnFiles .*capsulas', 120)
    m = re.search(r'CapsulePick_Start_LoadCmnFiles .*?: \d+ capsulas ([0-9,]+) de', texto)
    if not m: raise P.Fallo('no se registraron las tres opciones de Shenron')
    ids = list(map(int, m[1].split(',')))
    if len(ids) != 3: raise P.Fallo('Shenron no conserva los tres espacios')
    if par['fase'] == 'zeni' and ids != [0, 0, 0]: raise P.Fallo('con toda la lista poseída deben quedar tres premios monetarios')
    if par['fase'] == 'capsula' and (not ids[0] or len(set(ids)) != 3): raise P.Fallo('sorteo no válido de tres cápsulas sin reposición')
    ruta = j.dest / 'user_data/dbz3/pasarela/iw_habilidades.toml'
    antes = ruta.read_bytes()
    j.tecla_hasta_log('Space', r'iw shenron: opcion 0 .*aun sin conceder', 20, 1)
    if ruta.read_bytes() != antes: raise P.Fallo('se concedió el deseo antes de confirmar la información')
    j.captura('a_deseo_informacion')
    j.tecla('Return', 400)
    esperar(j, rf'iw shenron: deseo \d+: capsula {ids[0]}, estado .*Bolas 0; concesion EU 0x280D00', 20)
    t = tomllib.loads(ruta.read_text(encoding='utf8'))
    progreso = tomllib.loads((ruta.parent / 'progreso.toml').read_text(encoding='utf8'))
    if progreso['iw_bolas'] != 0: raise P.Fallo('no se dispersaron las siete Bolas')
    if ids[0]:
        if t['estado'][str(ids[0])] & 0xE0 != 0xE0: raise P.Fallo('no se guardó la cápsula seleccionada')
        if any(t['estado'][str(c)] & 0xE0 == 0xE0 for c in ids[1:]): raise P.Fallo('se concedió una opción que no fue elegida')
    elif progreso['iw_zeni'] != 999900: raise P.Fallo('premio monetario no acotado a 999900')
    j.captura('b_deseo_concedido')
    j.info('PASS Shenron: lista nativa, concesión tras START, solo premio elegido y Bolas dispersadas')


def preparar_habilidades(j, par, env):
    if par.get('continuo'):
        publicar_solicitud(j.dest / 'dbz3_iw_mision.req', int(par['mision']))
        return
    if par.get('tipo') not in ('habilidades', 'seleccion', 'deseos'): return
    ruta = j.dest / 'user_data/dbz3/pasarela/iw_habilidades.toml'
    if par.get('fase') == 'recargar': return
    if ruta.exists():
        respaldo = Path(r'D:\_PARA_BORRAR_CLAUDE\solIW') / ('habilidades_' + str(time.time_ns()))
        respaldo.mkdir(parents=True)
        shutil.move(str(ruta), str(respaldo / ruta.name))
    ruta.parent.mkdir(parents=True, exist_ok=True)
    if par.get('fase') == 'invalido': texto = 'version = "invalida"\n'
    elif par.get('tipo') == 'seleccion': texto = 'version = 1\n[estado]\n"2126" = 128\n'
    elif par.get('tipo') == 'deseos':
        texto = 'version = 1\n[estado]\n'
        if par['fase'] == 'zeni':
            # Referencia independiente: la lista REAL CMN967 de la copia del jugador.
            sys.path.insert(0, str(P.R / 'mod center hd'))
            import iso
            juego = Path(iso.find_game('iw', root=env['DBZ3_PS2_GAMES'], carpetas=True))
            if juego.is_dir(): f = (juego / 'USR/DATA_CMN.AFS').open('rb')
            else:
                img = iso.Iso(str(juego)); f = img.open(img.find('DATA_CMN.AFS'))
            with f:
                f.seek(8 + 967 * 8); off, n = struct.unpack('<II', f.read(8)); f.seek(off); lista = f.read(n)
            if len(lista) != 508: raise P.Fallo('CMN967 no contiene 127 IDs')
            ids = struct.unpack('<127I', lista)
            texto += ''.join(f'"{c}" = 224\n' for c in ids[1:])
        progreso = ruta.parent / 'progreso.toml'
        contenido = progreso.read_text(encoding='utf8') if progreso.exists() else ''
        respaldo = Path(r'D:\_PARA_BORRAR_CLAUDE\solIW') / ('progreso_deseos_' + str(time.time_ns()) + '.toml')
        respaldo.parent.mkdir(parents=True, exist_ok=True)
        if progreso.exists(): shutil.copy2(progreso, respaldo)
        for clave, valor in {'iw_bolas': 127, 'iw_zeni': 950000 if par['fase'] == 'zeni' else 0, 'iw_deseo': 0}.items():
            patron = rf'(?m)^\s*{clave}\s*=.*$'
            if re.search(patron, contenido): contenido = re.sub(patron, f'{clave} = {valor}', contenido)
            else: contenido += f'\n{clave} = {valor}\n'
        progreso.write_text(contenido, encoding='utf8')
    else:
        texto = 'version = 1\n[estado]\n"885" = 224\n'
        if par.get('fase') == 'variantes': texto += '"471" = 224\n'
    ruta.write_text(texto, encoding='utf8')


def corrida_paralela(a):
    copias = [c.strip() for c in a.copias.split(',') if c.strip()]
    if a.tipo != 'dm' or not 1 <= len(copias) <= 3 or len(set(copias)) != len(copias):
        raise SystemExit('--copias: una a tres copias distintas, solo para dm')
    salida = a.salida / (time.strftime('%Y%m%d-%H%M%S') + '_dm_' + a.region + '_completa')
    salida.mkdir(parents=True, exist_ok=False)
    exe = salida / 'dbz3.exe'
    shutil.copy2(a.exe or P.EXE, exe)
    def trabajador(i):
        lista = a.misiones[i::len(copias)]
        if not lista: return []
        destino = salida / copias[i]
        destino.mkdir()
        cmd = [sys.executable, '-X', 'utf8', str(Path(__file__).resolve()), 'dm', '--region', a.region,
               '--copia', copias[i], '--misiones', ','.join(map(str, lista)), '--salida', str(destino), '--exe', str(exe),
               '--turbo', str(a.turbo)]
        if a.continuo: cmd.append('--continuo')
        with (destino / 'ejecucion.log').open('w', encoding='utf8') as f:
            p = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
        archivos = list(destino.glob('*/misiones.json' if a.continuo else '*/resultados.json'))
        return json.loads(archivos[0].read_text(encoding='utf8')) if len(archivos) == 1 else []
    with ThreadPoolExecutor(max_workers=len(copias)) as pool:
        resultados = [r for lote in pool.map(trabajador, range(len(copias))) for r in lote]
    resultados.sort(key=lambda r: int(r['mision']))
    presentes = [int(r['mision']) for r in resultados]
    bien = sum(r['ok'] for r in resultados)
    ok = sorted(presentes) == sorted(a.misiones) and bien == len(a.misiones)
    (salida / 'resultados.json').write_text(json.dumps(resultados, indent=2, ensure_ascii=False), encoding='utf8')
    resumen = f'{"PASS" if ok else "FAIL"} iw_dm {a.region}: {bien}/{len(a.misiones)}; corrida nueva, un único exe, sin caché'
    (salida / 'resumen.txt').write_text(resumen + '\n', encoding='utf8')
    print(resumen, flush=True)
    return 0 if ok else 1


def main():
    # Este encargo limita también el arnés aunque haya un cupo temporal mayor.
    P.TOPE_JUEGOS = min(P.TOPE_JUEGOS, 2)
    P.VENTANAS = min(P.VENTANAS, 2)
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('tipo', choices=('dificultad', 'dm', 'habilidades', 'seleccion', 'deseos'))
    ap.add_argument('--region', choices=('us', 'eu'), required=True)
    ap.add_argument('--copia')
    ap.add_argument('--copias', help='dm completo repartido entre 1-3 copias; un único resumen de corrida')
    ap.add_argument('--exe', type=Path)
    ap.add_argument('--turbo', choices=(1, 4), type=int, default=1, help='1 real; 4 N1 (familias IW/batalla validadas por el arnés)')
    ap.add_argument('--continuo', action='store_true', help='dm: todas las misiones en la misma instancia, con retorno real al mapa')
    ap.add_argument('--fases', help='habilidades: editar,recargar,invalido,variantes (por defecto las cuatro)')
    ap.add_argument('--claves', help='seleccion: claves hex separadas por coma (por defecto vuelo, ladrillos, NB012 y MSN019)')
    ap.add_argument('--misiones', type=numeros, default=numeros('1-96'))
    ap.add_argument('--salida', type=Path, default=Path(r'D:\tmp\solIWC'))
    a = ap.parse_args()
    if a.copias: return corrida_paralela(a)
    if not a.copia: ap.error('falta --copia o --copias')
    # Juego.pantalla consulta el entorno DEL ARNÉS, no el entorno que recibe el
    # hijo. Esa distinción era la causa de esperar para siempre el menú EU.
    os.environ['DBZ3_TEST_PANTALLA_EU'] = '1' if a.region == 'eu' else '0'
    P.OVERRIDES['dbz3_region'] = '"' + a.region + '"'
    P.OVERRIDES['dbz3_game_dir'] = '"' + str(P.R).replace('\\', '/') + '"'
    ps2 = r'D:\DBZ3HD\ps2_games_eu' if a.region == 'eu' else r'D:\DBZ3HD\ps2_games'
    P.ESCENARIOS['iw_campana'] = ('IW: campaña y COM real', lambda p: {
        'DBZ3_PS2_GAMES': ps2, 'DBZ3_TEST_MISIONES': '4,reset,desde=95' if p.get('continuo') else ('4,desde=' if p.get('tipo') == 'deseos' else '4,reset,desde=') + str(int(p['mision']) - 1),
        **({'DBZ3_TEST_IW_CADENA': '1'} if p.get('continuo') else {'DBZ3_TEST_IW_CASILLA': p.get('clave', '0x7D6')} if p.get('tipo') == 'seleccion' else {'DBZ3_IW_MISION': p['mision']}),
        'DBZ3_TEST_RINGOUT': '1', 'DBZ3_R1_TRAZA': '1',
        'DBZ3_TEST_OCULTO': '0' if p.get('tipo') in ('seleccion', 'habilidades', 'deseos') else '1', 'DBZ3_TEST_TURBO': str(a.turbo)},
        cadena_continua if a.continuo else seleccion if a.tipo == 'seleccion' else deseos if a.tipo == 'deseos' else escenario, preparar_habilidades)
    carpeta = a.salida / (time.strftime('%Y%m%d-%H%M%S') + '_' + a.tipo + '_' + a.region + '_' + a.copia)
    carpeta.mkdir(parents=True, exist_ok=False)
    # Un mismo exe durante toda la corrida aunque otros frentes recompilen.
    exe = carpeta / 'dbz3.exe'
    shutil.copy2(a.exe or P.EXE, exe)
    os.environ['DBZ3_PRUEBAS_EXE'] = str(exe)
    (carpeta / 'exe.sha256').write_text(P._hash_archivo(exe) + '\n', encoding='ascii')
    # dbz3_region cambia los AFS, NO la imagen PPC: el --game_directory fijo de
    # pruebas.py gana al TOML. Raíz mínima con XEX verificado y enlaces a los
    # assets existentes; no otra copia de juego ni cambios a los originales.
    raiz = carpeta / 'datos'
    raiz.mkdir()
    P.OVERRIDES['dbz3_game_dir'] = '"' + raiz.as_posix() + '"'
    # escribir_toml fuerza R incluso sobre OVERRIDES. Su punto de extensión
    # documentado es DBZ3_TEST_CFG, aplicado después de esa asignación.
    os.environ['DBZ3_TEST_CFG'] = 'dbz3_game_dir="' + raiz.as_posix() + '"'
    fuente = P.R / ('yae3_xenon_eu.xex' if a.region == 'eu' else 'default.xex')
    md5 = hashlib.md5(fuente.read_bytes()).hexdigest().upper()
    esperado = 'C37EB979B762DA0AB5B8C9BA8037CE4E' if a.region == 'eu' else 'A53E324B5D2A65EBCBF648E4F85A7271'
    if md5 != esperado:
        raise SystemExit(f'XEX de {a.region} inesperado: {md5}')
    shutil.copy2(fuente, raiz / 'default.xex')
    for reg in ('us', 'eu'):
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(raiz / reg), str(P.R / reg)],
                       check=True, capture_output=True)
    original = P.Juego
    class JuegoImagen(original):
        def base_real(self, seg=20):
            super().base_real(seg)
            imagen = 'EU' if a.region == 'eu' else 'US'
            if not re.search(rf'using {imagen} image config', self.texto_log()):
                raise P.Fallo(f'no se cargó la imagen PPC {imagen}; no cuenta como prueba de esa región')
        def lanzar(self, env):
            proyecto = P.R
            try:
                P.R = raiz
                super().lanzar(env)
            finally:
                P.R = proyecto
    P.Juego = JuegoImagen
    casos = [{'mision': '12', 'dificultad': str(d), 'region': a.region} for d in range(5)] if a.tipo == 'dificultad' else [
        {'mision': str(n), 'region': a.region} for n in a.misiones]
    if a.tipo == 'habilidades': casos = [{'mision': '12', 'region': a.region, 'fase': f} for f in ('editar', 'recargar', 'invalido', 'variantes')]
    if a.fases:
        fases = a.fases.split(',')
        if a.tipo != 'habilidades' or any(f not in ('editar', 'recargar', 'invalido', 'variantes') for f in fases): ap.error('--fases solo admite fases de habilidades')
        casos = [{'mision': '12', 'region': a.region, 'fase': f} for f in fases]
    if a.claves and a.tipo != 'seleccion': ap.error('--claves solo admite seleccion')
    if a.tipo == 'seleccion':
        try:
            claves = [f'0x{int(c, 0):X}' for c in a.claves.split(',')] if a.claves else ['0x7D6', '0x7E0', '0x7D3', '0x7DA', '0x7E1']
        except ValueError: ap.error('--claves requiere numeros hexadecimales')
        if not claves or len(set(claves)) != len(claves) or any(c not in (
                '0x7D6', '0x7E0', '0x7D3', '0x7DA', '0x7E1', '0x7D1', '0x7D2', '0x7DF') for c in claves):
            ap.error('--claves: claves unicas de minijuegos con alternativas originales')
        casos = [{'mision': '12', 'region': a.region, 'clave': c} for c in claves]
    if a.tipo == 'deseos': casos = [{'mision': '12', 'region': a.region, 'fase': f} for f in ('capsula', 'zeni')]
    if a.continuo:
        if a.tipo != 'dm': ap.error('--continuo solo es de dm')
        casos = [{'mision': str(a.misiones[0]), 'region': a.region, 'misiones': ','.join(map(str, a.misiones)), 'continuo': '1'}]
    resultados = []
    for caso in casos:
        caso['arranque'] = '300'
        caso['tipo'] = a.tipo
        r = P.correr(a.copia, 'iw_campana', par=caso, forzar=True)
        resultados.append(dict(caso, **{k: str(v) if isinstance(v, Path) else v for k, v in r.items()}))
        (carpeta / 'resultados.json').write_text(json.dumps(resultados, indent=2, ensure_ascii=False), encoding='utf-8')
        if not r['ok']:
            break   # reparar el primer fallo antes de consumir las otras 95 plazas
    bien = sum(r['ok'] for r in resultados)
    resumen = f'{a.tipo} {a.region}: {bien}/{len(casos)} PASS (corrida nueva, sin caché)'
    if a.continuo:
        misiones = []
        if resultados:
            fichero = Path(resultados[0]['run']) / 'iw_cadena_resultados.json'
            if fichero.exists(): misiones = json.loads(fichero.read_text(encoding='utf8'))
        completas = [r['mision'] for r in misiones] == a.misiones
        if not bien and misiones: misiones[-1]['ok'] = False
        for r in misiones:
            r.update(region=a.region, run=resultados[0]['run'])
        (carpeta / 'misiones.json').write_text(json.dumps(misiones, indent=2), encoding='utf8')
        bien = sum(r['ok'] for r in misiones)
        resumen = f'iw_dm continuo {a.region}: {bien}/{len(a.misiones)} PASS; instancia única; sin caché'
        if not completas: bien = -1
        casos = a.misiones
    (carpeta / 'resumen.txt').write_text(resumen + '\n', encoding='utf-8')
    if a.tipo in ('seleccion', 'habilidades', 'deseos'):
        fixture = P.BASE / a.copia / 'user_data/dbz3/pasarela/iw_habilidades.toml'
        if fixture.exists(): shutil.move(str(fixture), str(carpeta / 'fixture_final.toml'))
    print(resumen, flush=True)
    return 0 if bien == len(casos) else 1


if __name__ == '__main__':
    raise SystemExit(main())

"""Referencias PS2 por TOML. Por defecto, solo lectura; --ejecutar queda para REF.

Cada toma declara estado + cuadro absoluto ya guardado, o estado + desplazamientos
del contador para una sesión pausada. Los pendientes nunca se ejecutan.
"""
import argparse
import ctypes
import hashlib
import io
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import tomllib
import zipfile
from contextlib import contextmanager
from functools import cache
from pathlib import Path

from PIL import Image

import pcsx2_pool as pool
from pcsx2_cuadros import CONTADORES, Cuadros


TRABAJO = Path(r'E:\DBZ3HD_trabajo')
DESTINO = TRABAJO / 'refs_1a1'
VERSION_ESTADO = bytes.fromhex('0000599a')
JUEGOS = {
    'iw': {'eu': ('SLES-55347', '335A5A1F', CONTADORES['iw'], 0x5C0DC8),
           'usa': ('SLUS-21842', 'E90DC5C9', 0x4A4540, 0x5C05C8)},
    'b2': {'eu': ('SLES-51839', 'FE09A7CC', CONTADORES['b2'], None)},
}
NOMBRE = re.compile(r'[a-z0-9][a-z0-9_-]{0,79}')
PATCH = re.compile(r'patch\s*=\s*[012]\s*,\s*(EE|IOP)\s*,\s*[0-9a-f]{8}\s*,\s*'
                   r'(byte|short|word|double|extended)\s*,\s*[0-9a-f]{1,16}', re.I)
MANDO = frozenset('up down left right z x a s q w 1 2 start select'.split())
ERRORES = (OSError, ValueError, TypeError, KeyError, RuntimeError, zipfile.BadZipFile,
           subprocess.SubprocessError, SystemExit)


def exigir(condicion, mensaje):
    if not condicion:
        raise ValueError(mensaje)


def campos(tabla, permitidos):
    exigir(isinstance(tabla, dict), 'se esperaba una tabla TOML')
    extras = tabla.keys() - set(permitidos.split())
    exigir(not extras, f'campos desconocidos: {sorted(extras)}')


def archivo(base, texto, *exts):
    exigir(isinstance(texto, str) and bool(texto.strip()), 'se esperaba una ruta')
    p = (base / texto).resolve(strict=True)
    exigir(p.suffix.lower() in exts and p.is_file() and p.stat().st_size > 0,
           f'archivo inválido ({", ".join(exts)}): {p}')
    return p


def entero(n, minimo, maximo, campo):
    exigir(type(n) is int and minimo <= n <= maximo, f'{campo} fuera de {minimo}..{maximo}: {n!r}')
    return n


def firma(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


@cache
def datos_estado(p, crc, contador, dir_clave, imagen=False):
    with zipfile.ZipFile(p) as z:
        for nombre in ('PCSX2 Savestate Version.id', 'PCSX2 Internal Structures.dat', 'eeMemory.bin'):
            exigir(z.namelist().count(nombre) == 1, f'{p}: falta o se repite {nombre}')
        with z.open('PCSX2 Savestate Version.id') as f:
            exigir(f.read(4) == VERSION_ESTADO, f'{p}: versión del estado no comprobada')
        # Formato 0x9A590000 observado en los estados canónicos EU/USA y B2.
        with z.open('PCSX2 Internal Structures.dat') as f:
            cab = f.read(0x128)
        exigir(cab.startswith(b'BIOS\0') and len(cab) == 0x128 and
               int.from_bytes(cab[0x124:0x128], 'little') == int(crc, 16),
               f'{p}: CRC interno no coincide con {crc}')
        with z.open('eeMemory.bin') as f:
            exigir(z.getinfo('eeMemory.bin').file_size >= 32 * 1024 * 1024, f'{p}: RAM EE incompleta')
            def leer(a):
                f.seek(a)
                b = f.read(4)
                exigir(len(b) == 4, f'{p}: RAM EE truncada en {a:#x}')
                return int.from_bytes(b, 'little')
            cuadro = leer(contador)
            clave = leer(dir_clave) if dir_clave is not None else None
        if imagen:
            exigir(z.namelist().count('Screenshot.png') == 1, f'{p}: falta o se repite Screenshot.png')
            comprobar_png(z.read('Screenshot.png'))
    return cuadro, clave


def comprobar_png(data):
    with Image.open(io.BytesIO(data), formats=['PNG']) as im:
        im.verify()
    with Image.open(io.BytesIO(data), formats=['PNG']) as im:
        im.load()


def validar_pnach(p, crc):
    exigir(pool.crc_pnach(p) == crc, f'{p}: pnach de otro CRC (esperado {crc})')
    parches = [s.split('//', 1)[0].strip() for s in p.read_text(encoding='utf-8').splitlines()]
    parches = [s for s in parches if s.lower().startswith('patch')]
    exigir(parches and all(PATCH.fullmatch(s) for s in parches), f'{p}: faltan parches o hay un patch inválido')


def validar_entradas(lista, ultimo):
    exigir(isinstance(lista, list) and len(lista) <= 2000, 'eventos de mando inválidos')
    anterior, pulsadas = 0, set()
    for ev in lista:
        campos(ev, 'desplazamiento tecla pulsar')
        anterior = entero(ev['desplazamiento'], anterior, ultimo, 'evento de mando')
        tecla, pulsar = ev['tecla'], ev['pulsar']
        exigir(tecla in MANDO and type(pulsar) is bool, f'evento de mando inválido: {ev}')
        exigir(pulsar != (tecla in pulsadas), f'evento de mando sin pareja: {ev}')
        pulsadas.symmetric_difference_update({tecla})
    exigir(not pulsadas, f'faltan eventos de soltar: {sorted(pulsadas)}')
    return lista


def planificar(manifiesto, raiz, compat_poke_vacio=False):
    with manifiesto.open('rb') as f:
        m = tomllib.load(f)
    campos(m, 'juego discos tomas')
    juego = m['juego']
    regiones = JUEGOS[juego]
    base = manifiesto.resolve().parent
    discos = {}
    for region, texto in m.get('discos', {}).items():
        exigir(region in regiones, f'región de disco desconocida: {region}')
        discos[region] = archivo(base, texto, '.iso', '.bin', '.chd')
    tomas = m['tomas']
    exigir(isinstance(tomas, list) and tomas, 'faltan [[tomas]]')
    vistas, trabajos, errores, pendientes = set(), [], 0, 0
    for t in tomas:
        nombre = t.get('id', '?') if isinstance(t, dict) else '?'
        try:
            campos(t, 'id region modo estado cuadro clave pnach velocidad desplazamientos motivo fuente mando poke')
            exigir(isinstance(nombre, str) and NOMBRE.fullmatch(nombre) and
                   not re.fullmatch(r'con|prn|aux|nul|com[0-9]|lpt[0-9]', nombre) and
                   nombre not in vistas, f'id inválido/repetido: {nombre}')
            vistas.add(nombre)
            region = t['region']
            serial, crc, contador, dir_clave = regiones[region]
            modo = t['modo']
            exigir(modo in ('reusar', 'capturar', 'pendiente'), f'modo desconocido: {modo}')
            p = archivo(base, t['estado'], '.p2s') if 'estado' in t else None
            exigir(p is not None or modo == 'pendiente', 'falta estado .p2s')
            inicio, clave = datos_estado(p, crc, contador, dir_clave, modo == 'reusar') if p else (None, None)
            if 'cuadro' in t:
                exigir(entero(t['cuadro'], 0, 0xffffffff, 'cuadro') == inicio,
                       f'cuadro declarado {t["cuadro"]} != guardado {inicio}')
            if 'clave' in t:
                exigir(entero(t['clave'], 0x7d0, 0x7e1, 'clave') == clave, f'clave guardada distinta: {clave!r}')
            pn = archivo(base, t['pnach'], '.pnach') if 'pnach' in t else None
            if pn:
                validar_pnach(pn, crc)
            poke = t.get('poke', [])   # [[direccion EE, "hex"]]: se escriben una vez por PINE al empezar (un pnach reescribe cada cuadro)
            exigir(isinstance(poke, list) and all(isinstance(x, list) and len(x) == 2 and isinstance(x[0], int) and
                                                  isinstance(x[1], str) and re.fullmatch(r'([0-9a-f]{2})+', x[1]) for x in poke),
                   'poke: lista de [direccion, "hex"]')
            vel = entero(t.get('velocidad', 2), 2, 3, 'velocidad')
            desp = t.get('desplazamientos', [0])
            exigir(isinstance(desp, list) and 1 <= len(desp) <= 1000, 'desplazamientos: lista de 1 a 1000 enteros')
            for d in desp:
                entero(d, 0, min(18000, 0xffffffff - (inicio or 0)), 'desplazamiento')
            exigir(desp == sorted(set(desp)), 'desplazamientos deben ser únicos y crecientes')
            exigir('mando' not in t or modo == 'capturar', 'el mando solo se admite al capturar')
            mando = validar_entradas(t.get('mando', []), desp[-1])
            if modo == 'pendiente':
                exigir(isinstance(t.get('motivo'), str) and t['motivo'].strip(), 'falta motivo del pendiente')
                exigir('desplazamientos' not in t, 'los pendientes no programan cuadros')
                pendientes += 1
                print(f'PENDIENTE {juego}/{nombre}: {t["motivo"]}' + (f' (semilla: {p.name}, cuadro {inicio})' if p else ''))
                continue
            if modo == 'reusar':
                exigir('cuadro' in t and not {'desplazamientos', 'velocidad', 'pnach'}.intersection(t),
                       'reusar requiere el cuadro guardado y no admite opciones de emulación')
            else:
                exigir(region in discos and pn is not None and 'cuadro' in t and 'desplazamientos' in t,
                       'capturar requiere disco, pnach, cuadro base y desplazamientos explícitos')
            carpeta = raiz / juego / nombre
            datos = dict(juego=juego, id=nombre, region=region, modo=modo, serial=serial, crc=crc,
                         contador=contador, estado=str(p), estado_sha256=firma(p), fuente=t.get('fuente'),
                         pnach=str(pn) if pn else None, pnach_sha256=firma(pn) if pn else None,
                         velocidad=vel if pn else None, disco=str(discos[region]) if pn else None, mando=mando, poke=poke)
            faltan = []
            for d in desp:
                cuadro = inicio + d
                ficha = dict(datos, cuadro=cuadro, desplazamiento=d)
                destino = carpeta / f'{cuadro:010d}'
                if disponible(destino, ficha, compat_poke_vacio):
                    faltan.append((destino, ficha))
            trabajos.append((faltan, inicio, dir_clave))
            print(f'{"PREVISTO" if faltan else "YA HECHO"} {juego}/{nombre}: {len(faltan)}/{len(desp)} cuadros nuevos')
        except ERRORES as e:
            errores += 1
            print(f'ERROR {juego}/{nombre}: {e}', file=sys.stderr)
    return trabajos, errores, pendientes


def disponible(destino, ficha, compat_poke_vacio=False):
    if not destino.exists():
        return True
    try:
        j = json.loads((destino / 'referencia.json').read_text(encoding='utf-8'))
        publicada = j['toma']
        if (compat_poke_vacio and isinstance(publicada, dict) and
                'poke' not in publicada and ficha.get('poke') == []):
            publicada = dict(publicada, poke=[])
        exigir(publicada == ficha, 'entrada diferente a la ya publicada')
        exigir(firma(destino / 'cuadro.png') == j['png_sha256'], 'PNG modificado')
        exigir(firma(destino / 'cuadro.p2s') == j['captura_sha256'], 'estado modificado')
        return False
    except ERRORES as e:
        raise ValueError(f'{destino}: salida en conflicto/incompleta; se conserva: {e}') from e


def publicar(destino, ficha, trabajo, metodo):
    png, ss = trabajo / 'cuadro.png', trabajo / 'cuadro.p2s'
    metadata = dict(toma=ficha, metodo=metodo, png_sha256=firma(png), captura_sha256=firma(ss))
    with (trabajo / 'referencia.json').open('x', encoding='utf-8') as f:
        json.dump(metadata, f, indent=2, ensure_ascii=False)
        f.write('\n')
    # En Windows rename de directorio no sustituye un destino existente.
    trabajo.rename(destino)


def etapa(destino):
    destino.parent.mkdir(parents=True, exist_ok=True)
    p = destino.parent / f'.ref-{os.getpid()}-{time.time_ns()}'
    p.mkdir()
    return p


def reusar(destino, ficha):
    origen = Path(ficha['estado'])
    tmp = etapa(destino)
    shutil.copyfile(origen, tmp / 'cuadro.p2s')
    exigir(firma(tmp / 'cuadro.p2s') == ficha['estado_sha256'], 'estado cambiado desde el preflight')
    with zipfile.ZipFile(tmp / 'cuadro.p2s') as z:
        (tmp / 'cuadro.png').write_bytes(z.read('Screenshot.png'))
    publicar(destino, ficha, tmp, 'Screenshot.png del estado original (sin recapturar)')


@contextmanager
def sesion(n, ficha, inicio):
    import pcsx2_sonda as s
    from ctypes import wintypes
    from pcsx2_turnos import afinar, comprobar_tope

    t = pool._turno(n)
    t.adquirir(os.getpid(), sys.executable)
    dueno, proc, p = os.getpid(), None, None
    try:
        exe = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'], capture_output=True, check=True,
                             creationflags=subprocess.CREATE_NO_WINDOW).stdout.decode('mbcs', errors='replace')
        procesos = [line.split(',', 1)[0].strip('"').lower() for line in exe.splitlines()]
        exigir(procesos.count('dbz3.exe') < 3 and not any(x.startswith('pcsx2') for x in procesos),
               'cupo REF ocupado: hay otro PCSX2 o tres juegos abiertos')
        # No se escribe el perfil si el puerto ya está en uso, incluso sin cerrojo.
        with socket.socket() as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            sock.bind(('127.0.0.1', pool.puerto(n)))
        perfil = pool.perfil(n)
        base = Path(ficha['estado'])
        pn = Path(ficha['pnach'])
        exigir(firma(base) == ficha['estado_sha256'] and firma(pn) == ficha['pnach_sha256'], 'entradas cambiadas tras preflight')
        instalado = perfil / 'cheats' / f'{ficha["crc"]}.pnach'
        exigir(not instalado.exists() or firma(instalado) == ficha['pnach_sha256'],
               f'{instalado}: pnach distinto; REF debe apartarlo antes')
        existia = instalado.exists()
        pool.preparar(n, pausado=True, truco=None if existia else pn, velocidad=ficha['velocidad'])
        if existia:
            # Si ya existía el mismo cheat, preparar respaldó el ini y desactivó cheats.
            ini = perfil / 'inis' / 'PCSX2.ini'
            pool.escribir_ini(pool.PLANTILLA / 'inis' / 'PCSX2.ini', ini, n, True, True, ficha['velocidad'])
        prefijo = f'{ficha["serial"]} ({ficha["crc"]})'
        estados = perfil / 'sstates'
        otros = [p for p in estados.glob(f'{ficha["serial"]} (*).*.p2s') if not p.name.startswith(prefijo + '.')]
        exigir(not otros, f'perfil con CRC ambiguo para {ficha["serial"]}: {otros}')
        copia = next((p for p in estados.glob(prefijo + '.*.p2s') if firma(p) == ficha['estado_sha256']), None)
        if copia is None:
            copia = next((estados / f'{prefijo}.{i:02d}.p2s' for i in range(80, 10, -1)
                          if not (estados / f'{prefijo}.{i:02d}.p2s').exists()), None)
            exigir(copia is not None, 'no queda slot libre para una copia reproducible de la semilla')
            with base.open('rb') as src, copia.open('xb') as dst:
                shutil.copyfileobj(src, dst)
            exigir(firma(copia) == ficha['estado_sha256'], 'copia del estado no coincide')
        si = subprocess.STARTUPINFO()
        si.dwFlags, si.wShowWindow = subprocess.STARTF_USESHOWWINDOW, 4
        proc = subprocess.Popen(pool.comando(n, ficha['disco'], copia.resolve()), cwd=perfil, startupinfo=si)
        t.adoptar(proc.pid)
        dueno = proc.pid
        afinar(proc.pid)
        pool.registrar_en_sonda(s, n)
        s.u32dll.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        s.u32dll.PostMessageW.restype = wintypes.BOOL
        fin = time.monotonic() + 240
        while time.monotonic() < fin and proc.poll() is None:
            comprobar_tope()
            try:
                p = s.Pine()
                p.REINTENTOS = 0
                if (p.status(), p.id(), p.u32(ficha['contador'])) == (1, ficha['serial'], inicio):
                    break
            except (OSError, RuntimeError):
                pass
            if p:
                p.s.close()
                p = None
            time.sleep(.5)
        else:
            raise RuntimeError('PCSX2 no confirmó pausa, serial y contador inicial del estado')
        c = Cuadros(s, ficha['contador'], p=p)
        c.exacto = ficha['juego'] == 'b2'
        yield c
    finally:
        if p:
            p.s.close()
        if proc and proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=15)
        t.liberar(dueno)


def capturar(n, filas, inicio, dir_clave):
    from pcsx2_turnos import Grabacion, comprobar_tope

    ficha0 = filas[0][1]
    with Grabacion(f'refs_lote {ficha0["juego"]}/{ficha0["id"]}', espera_max=0), sesion(n, ficha0, inicio) as c:
        for dir_poke, hexa in ficha0.get('poke', []):
            c.p.escribir(dir_poke, bytes.fromhex(hexa))
        def avanzar(objetivo, libre):
            comprobar_tope()
            # Turbo solo en tramo libre; el último tramo es FrameAdvance exacto.
            if libre and objetivo - c.numero() > 400:
                c.reanudar()
                fin = time.monotonic() + 180
                while objetivo - c.numero() > 400:
                    comprobar_tope()
                    exigir(time.monotonic() < fin, 'el contador no alcanza el objetivo en marcha libre')
                    time.sleep(.01)
                c.pausa()
            while c.numero() < objetivo:
                c.paso()
            exigir(c.numero() == objetivo, f'cuadro inalcanzado: {c.numero()} != {objetivo}')

        eventos = iter(ficha0['mando'])
        ev, pulsadas = next(eventos, None), set()
        for destino, ficha in filas:
            objetivo = ficha['cuadro']
            while ev and inicio + ev['desplazamiento'] <= objetivo:
                avanzar(inicio + ev['desplazamiento'], not pulsadas)
                c.tecla(c.s.VK[ev['tecla']], ev['pulsar'])
                pulsadas.symmetric_difference_update({ev['tecla']})
                ev = next(eventos, None)
            avanzar(objetivo, not pulsadas)
            tmp = etapa(destino)
            var = 'DBZ3_PCSX2_CUADROS_RESPALDO'
            antes = os.environ.get(var)
            os.environ[var] = str(tmp)
            try:
                d = c.captura(tmp / 'cuadro.png')
            finally:
                if antes is None:
                    os.environ.pop(var, None)
                else:
                    os.environ[var] = antes
            ss, = (tmp / 'captura_estados').glob('*.p2s')
            ss.rename(tmp / 'cuadro.p2s')
            cuadro, _ = datos_estado(tmp / 'cuadro.p2s', ficha['crc'], ficha['contador'], dir_clave, True)
            exigir(cuadro == objetivo == d['cuadro'], 'el cuadro capturado no coincide con el objetivo')
            # Cuadros.captura produce también una vista al 50 %. Publicar el PNG
            # original del estado, igual que en el camino de reutilización.
            (tmp / 'cuadro.png').rename(tmp / 'vista_50.png')
            with zipfile.ZipFile(tmp / 'cuadro.p2s') as z:
                (tmp / 'cuadro.png').write_bytes(z.read('Screenshot.png'))
            publicar(destino, ficha, tmp, d['metodo'])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('manifiestos', type=Path, nargs='+')
    modos = ap.add_mutually_exclusive_group()
    modos.add_argument('--seco', action='store_true', help='solo lectura (por defecto)')
    modos.add_argument('--ejecutar', action='store_true', help='solo sesión REF: materializa tomas e inicia PCSX2 si hace falta')
    ap.add_argument('--estricto', action='store_true', help='devuelve 1 también si hay tomas pendientes')
    ap.add_argument('--compat-poke-vacio', action='store_true',
                    help='acepta fichas antiguas sin poke si la toma no declara ningún poke')
    ap.add_argument('--pool', type=int, choices=range(1, pool.N_MAX + 1), default=1)
    ap.add_argument('--salida', type=Path, default=DESTINO)
    a = ap.parse_args(argv)
    todos, fallos, pendientes, destinos = [], 0, 0, set()
    for m in a.manifiestos:
        try:
            trabajos, e, p = planificar(m, a.salida.resolve(), a.compat_poke_vacio)
            fallos += e
            pendientes += p
            for filas, inicio, clave in trabajos:
                for dest, _ in filas:
                    exigir(dest not in destinos, f'destino duplicado entre manifiestos: {dest}')
                    destinos.add(dest)
                todos.append((filas, inicio, clave))
        except ERRORES as e:
            fallos += 1
            print(f'ERROR {m}: {e}', file=sys.stderr)
    print(f'{len(todos)} tomas válidas, {len(destinos)} cuadros nuevos, {pendientes} pendientes, {fallos} errores', flush=True)
    if fallos or (a.estricto and pendientes):
        return 1
    if a.ejecutar:
        if os.name != 'nt':
            ap.error('--ejecutar requiere Windows')
        for filas, inicio, clave in todos:
            if not filas:
                continue
            try:
                if filas[0][1]['modo'] == 'reusar':
                    reusar(*filas[0])
                else:
                    capturar(a.pool, filas, inicio, clave)
            except ERRORES as e:
                print(f'ERROR {filas[0][0]}: {e}', file=sys.stderr)
                return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

"""Auditoria offline de relojes IW: instrucciones ELF y estados ya capturados.

No ejecuta el juego ni modifica estados. Los contadores del mando no acreditan
por si solos campos de video. Conserva bytes originales y coordenadas del ELF.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import time
import zipfile


ELF_SHA = 'afe8c7b99f31e635750a17618ae8248d1a64fe3ad0669996990abdf33e1819f4'
FUENTE_CONTADORES = 'https://raw.githubusercontent.com/PCSX2/pcsx2/v2.9.103/pcsx2/Counters.cpp'


def u32(b, p):
    return struct.unpack_from('<I', b, p)[0]


def reloj_video(internal, version):
    """Lectura propia del formato serializado de la version observada.

    Las coordenadas se contrastan con rcntFreeze y sus tipos en v2.9.103.
    No se interpreta otra version mediante offsets supuestos. Un ancla VBlank
    permite contar periodos entre estados; no acredita sus filas intermedias.
    """
    # Los bytes de relleno posteriores al NUL no son deterministas.
    if not version.startswith(bytes.fromhex('0000599a76322e392e31303300')):
        raise ValueError('Version de estado no acreditada: solo v2.9.103/9A590000')
    tag = b'EE-Subsystems'.ljust(32, b'\0')
    p = internal.find(tag)
    if p < 0 or internal.find(tag, p+1) >= 0:
        raise ValueError('Marcador EE-Subsystems ausente o ambiguo')
    p += 32
    if len(internal) < p+233:
        raise ValueError('Estructuras de contadores truncadas')
    if [u32(internal, p+32*i+20) for i in range(4)] != [9, 10, 11, 12]:
        raise ValueError('Tabla de contadores distinta del formato acreditado')
    mode = u32(internal, p+152)
    start = struct.unpack_from('<Q', internal, p+160)[0]
    delta = struct.unpack_from('<i', internal, p+168)[0]
    fps = struct.unpack_from('<d', internal, p+188)[0]
    video, render, blank, gsblank = struct.unpack_from('<4I', internal, p+196)
    scanlines = u32(internal, p+224)
    if mode not in (0, 1, 2) or fps != 25.0 or video != 3 or scanlines != 625:
        raise ValueError('Solo se acredita el modo PAL observado, no otros modos de video')
    if render+blank != 5898240 or not 0 < gsblank < blank < render:
        raise ValueError('Tiempos PAL incoherentes')
    if delta != (render, blank, gsblank)[mode]:
        raise ValueError('Fase VSync y duracion de evento incoherentes')
    if u32(internal, p+228) != video or internal[p+232] != 1:
        raise ValueError('Modo GS e interlazado no corresponden a la temporizacion PAL')
    return dict(formato='PCSX2 v2.9.103 / 0x9A590000 / Windows x64', fuente=FUENTE_CONTADORES,
        subsistemas_offset=p-32, vsync_mode=mode, vsync_start_cycle=start,
        ancla_vblank_cycle=start-blank if mode == 0 else start,
        render_cycles=render, blank_cycles=blank, gsblank_cycles=gsblank,
        periodo_campo_cycles=render+blank, frecuencia_campos_hz=2*fps,
        alcance='Ancla de video serializada; no es g_FrameCount ni contador del mando')


def comparar_estados(a, b):
    ca, cb = a['video'], b['video']
    if (ca['formato'], ca['periodo_campo_cycles'], a['clave']) != (
            cb['formato'], cb['periodo_campo_cycles'], b['clave']):
        raise ValueError('Los estados no comparten formato, temporizacion y mision')
    delta = cb['ancla_vblank_cycle']-ca['ancla_vblank_cycle']
    periodo = ca['periodo_campo_cycles']
    if delta <= 0 or delta % periodo:
        raise ValueError('Intervalo no positivo o no alineado con periodos VBlank')
    campos = delta//periodo
    serial = (b['mando']['serial']-a['mando']['serial']) & 0xffffffff
    return dict(desde=a['ruta'], hasta=b['ruta'], ciclos=delta, campos_video=campos,
        incremento_mando=serial, coinciden_extremos=campos == serial,
        filas_intermedias_certificadas=False,
        alcance='Extremos de estados suministrados; no demuestra ausencia de carga/reset intermedios')


def estado(path):
    with zipfile.ZipFile(path) as z:
        version = z.read('PCSX2 Savestate Version.id')
        internal = z.read('PCSX2 Internal Structures.dat')
        with z.open('eeMemory.bin') as ram:
            ram.seek(0x4a4c50)
            pad = struct.unpack('<4I', ram.read(16))
            ram.seek(0x4a4d40)
            serial = int.from_bytes(ram.read(4), 'little')
            ram.seek(0x49c7cc)
            motor = ram.read(0x48)
            ram.seek(0x5c0dc4)
            indice, clave = struct.unpack('<II', ram.read(8))
        video = reloj_video(internal, version)
        return dict(ruta=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            version_hex=version.hex(), version_texto=version[4:].split(b'\0')[0].decode('ascii', 'strict'),
            estructuras_sha256=hashlib.sha256(internal).hexdigest(),
            video=video, clave=hex(clave), indice=indice,
            mando=dict(buffer_lectura=hex(pad[0]), buffer_envio=hex(pad[1]),
                destino=hex(pad[2]), dma=pad[3], serial=serial),
            motor_49c7cc_hex=motor.hex(), reloj_video_verificado=True)


def guardar_pausado(c, carpeta, ordinal):
    """PINE SaveState propio: archiva el estado y lee su reloj sin sacar fotos."""
    c.pausa()
    antes = c.numero()
    estados = Path(c.s.EMU)/'sstates'
    serial = c.p.id()
    candidatos = list(estados.glob(serial+' (*).*.p2s'))
    if not candidatos:
        raise RuntimeError('Falta estado base para identificar el CRC')
    prefijo = candidatos[0].name.rsplit('.', 2)[0]
    slot = next((i for i in range(99, 9, -1)
        if not (estados/f'{prefijo}.{i:02d}.p2s').exists()), None)
    if slot is None:
        raise RuntimeError('No hay slot temporal libre para leer el reloj')
    src = estados/f'{prefijo}.{slot:02d}.p2s'
    carpeta.mkdir(exist_ok=True)
    dst = carpeta/f'{ordinal:05d}_{src.name}'
    if dst.exists():
        raise FileExistsError(dst)
    try:
        c.p.guardar(slot)
        fin = time.monotonic()+20
        while True:
            try:
                dato = estado(src)
                break
            except (OSError, zipfile.BadZipFile, KeyError):
                if time.monotonic() >= fin:
                    raise TimeoutError('SaveState no ha terminado de escribir su reloj')
                time.sleep(.05)  # Solo finalizacion de E/S, VM pausada.
        if c.p.status() != 1 or c.numero() != antes or dato['mando']['serial'] != antes:
            raise RuntimeError('La VM avanzo durante la lectura del reloj')
        shutil.move(str(src), str(dst))
        dato['ruta'] = str(dst.resolve())
        return dato
    finally:
        if src.exists() and not dst.exists():
            shutil.move(str(src), str(dst))  # Conservar tambien cualquier estado rechazado.


def auditar_serie(path):
    filas = [json.loads(x) for x in path.read_text(encoding='utf-8').splitlines()]
    if len(filas) < 2:
        raise ValueError('Faltan pares de campos en la serie')
    anteriores, anomalias, segmentos = None, [], []
    for fila in filas:
        declarado = fila['video']
        real = estado(Path(declarado['ruta']))
        if real != declarado or real['mando']['serial'] != fila['n'] or not fila['estable']:
            raise ValueError('El estado conservado no autentica la fila RAM declarada')
        campo = real['video']['ancla_vblank_cycle']//real['video']['periodo_campo_cycles']
        if campo != fila['campo_video']:
            raise ValueError('Indice de campo distinto del reloj autentico')
        if anteriores is not None:
            par = comparar_estados(anteriores, real)
            if par['campos_video'] != 1:
                raise ValueError('Hueco real de video: '+str(par))
            if not par['coinciden_extremos']:
                anomalias.append(dict(campo_video=campo, serial_antes=anteriores['mando']['serial'],
                    serial_despues=fila['n'], incremento_mando=par['incremento_mando']))
        anteriores = real
        actor = fila['actores'][0]
        firma = (actor['estado'], actor['codigo'], actor['dur'])
        if not segmentos or firma != tuple(segmentos[-1]['firma']):
            segmentos.append(dict(firma=firma, primero=fila['n'], campo_video=campo,
                f_inicial=actor['f'], muestras=0))
        segmentos[-1].update(ultimo=fila['n'], f_final=actor['f'], energia_final=fila.get('energia'))
        segmentos[-1]['muestras'] += 1
    return dict(fuente=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        filas=len(filas), pares_vsync_verificados=len(filas)-1, estados_autenticados=len(filas),
        contador_temporal='campo_video', anomalias_libpad=anomalias, segmentos=segmentos,
        energia_min=min(x['energia']['actual'] for x in filas) if 'energia' in filas[0] else None,
        paridad_certificada=False)


def instrucciones(path):
    b = path.read_bytes()
    if hashlib.sha256(b).hexdigest() != ELF_SHA or b[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Identidad ELF IW EU distinta')
    ph, size, count = u32(b, 28), struct.unpack_from('<H', b, 42)[0], struct.unpack_from('<H', b, 44)[0]
    segments = [struct.unpack_from('<8I', b, ph+i*size) for i in range(count)]
    rows = []
    for lo, hi in ((0x10e5b0, 0x10e6c8), (0x10e6c8, 0x10e968), (0x10f1a8, 0x10f270)):
        segment = next(x for x in segments if x[0] == 1 and x[2] <= lo and hi <= x[2]+x[4])
        for pc in range(lo, hi, 4):
            off = segment[1]+pc-segment[2]
            word = u32(b, off)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            imm = struct.unpack('<h', struct.pack('<H', word & 0xffff))[0]
            text = None
            if op in (9, 15, 35, 43):
                name = {9:'addiu', 15:'lui', 35:'lw', 43:'sw'}[op]
                text = f'{name} r{rt}, r{rs}, {imm}'
            elif op == 3:
                text = f'jal 0x{((pc+4)&0xf0000000)|((word&0x3ffffff)<<2):08x}'
            rows.append(dict(pc=f'{pc:08X}', archivo_offset=off, palabra=f'{word:08X}',
                lectura_parcial=text))
    return dict(ruta=str(path.resolve()), sha256=ELF_SHA, instrucciones=rows,
        alcance='Solo opcodes MIPS basicos; palabras R5900 conservadas, sin decoder de ABI ni flujo completo')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--estado', type=Path, action='append', default=[])
    p.add_argument('--elf', type=Path)
    p.add_argument('--salida', type=Path)
    p.add_argument('--comparar', action='store_true', help='Contrastar cada par de extremos consecutivos, sin acreditar filas internas')
    p.add_argument('--serie', type=Path, help='Autenticar todos los estados y cada paso VSync de cuadros.jsonl')
    a = p.parse_args()
    result = dict(estados=[estado(x) for x in a.estado],
        elf=instrucciones(a.elf) if a.elf else None, paridad_certificada=False)
    if a.serie:
        result['serie'] = auditar_serie(a.serie)
    if a.comparar:
        if len(result['estados']) < 2:
            p.error('--comparar requiere al menos dos estados declarados')
        result['comparaciones'] = [comparar_estados(x, y) for x, y in zip(result['estados'], result['estados'][1:])]
    if a.salida:
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump(result, f, indent=1, ensure_ascii=False, allow_nan=False)
        print(a.salida)
        for e in result['estados']:
            print(e['version_texto'], e['mando']['serial'], e['video']['ancla_vblank_cycle'])
        for c in result.get('comparaciones', []):
            print('VIDEO', c['campos_video'], 'MANDO', c['incremento_mando'], 'extremos', c['coinciden_extremos'])
        if result['elf']:
            print('ELF', ELF_SHA, len(result['elf']['instrucciones']), 'palabras')
        if 'serie' in result:
            print(json.dumps(result['serie'], indent=1, allow_nan=False))
    else:
        print(json.dumps(result, indent=1, ensure_ascii=False, allow_nan=False))


if __name__ == '__main__':
    main()

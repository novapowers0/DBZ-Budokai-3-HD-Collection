# SPDX-License-Identifier: MIT
"""F1: bancos OFFLINE de disparo; nunca abre juego ni PCSX2.

censo: cuerpos completos hasta siguiente simbolo, candidatos unicos (no equivalencia).
rangos: ejecuta init/clasificador ORIGINAL de los tres motores, EU y USA.
regionales: contrasta limites y diferencias de tres cuerpos MgShoot EU/USA.
validar: contrato B8 v1 estructural; NO ejecuta ni certifica el motor completo.
autoprueba: rechazos del contrato con datos artificiales, sin dorados ni ELF.

Los ELF y las capturas son del usuario y no se copian a la entrega.
"""
import argparse
import difflib
import hashlib
import json
import math
import re
import struct
import time
from pathlib import Path

import ee

ELF_EU = Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47')
ELF_US = Path('E:/DBZ3HD_trabajo/T2B/usa/SLUS_218.42')
GHIDRA = Path('E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c')
ELF_HASH = {'eu': 'afe8c7b99f31e635750a17618ae8248d1a64fe3ad0669996990abdf33e1819f4',
            'us': '9b903cbfd9bd551a6893912dd47d2bb7661533867ed40cc83d31cdb3044b42b7'}
CONTEXTO = 0x1000000
FAMILIAS = {'7DE': (0x3099E0, 0x30D600), '7D8': (0x324490, 0x327F80),
            '7D7': (0x3481E0, 0x34BD90)}
# Entradas unicas del censo, ejecutadas regionalmente por el banco rangos.
# init, rank, puntero contexto, indice dificultad, tabla, sizeof(elemento).
PERFILES = {
    'eu': {'7DE': (0x30D150, 0x3099E0, 0, 0, 0x5D9378, 4),
           '7D8': (0x3279F0, 0x324490, 0x49CDF0, 0x28, 0x5D97DC, 4),
           '7D7': (0x34B800, 0x3481E0, 0x49CE78, 0x14, 0x5D9AEC, 2)},
    'us': {'7DE': (0x30CEE0, 0x3097C0, 0, 0, 0x5D8B78, 4),
           '7D8': (0x3276E0, 0x3241A0, 0x49C5D0, 0x28, 0x5D8FDC, 4),
           '7D7': (0x34B300, 0x347CE0, 0x49C65C, 0x14, 0x5D92EC, 2)}}
FASES = {'init', 'ready', 'aim', 'carga', 'fuego', 'impacto', 'fin'}
OBSERVABLES = {'estado', 'reloj_bits', 'semilla', 'vida_bits', 'potencia_bits',
               'impactos', 'fin', 'rango', 'pose', 'camara', 'sonido'}


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for data in iter(lambda: f.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def cargar_elf(path):
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('se requiere ELF32 little endian')
    po, = struct.unpack_from('<I', data, 28)
    pe, pn = struct.unpack_from('<HH', data, 42)
    if pe != 32 or po + pe * pn > len(data):
        raise ValueError('tabla PT_LOAD truncada o incompatible')
    mem = bytearray(ee.TOTAL)
    for i in range(pn):
        tipo, off, va, _, fs, ms, _, _ = struct.unpack_from('<8I', data, po + i * pe)
        if tipo == 1:
            if fs > ms or va + ms > ee.RAM or off + fs > len(data):
                raise ValueError('PT_LOAD fuera de rango')
            mem[va:va + fs] = data[off:off + fs]
    return mem, hashlib.sha256(data).hexdigest()


def normalizar(data):
    words = []
    for (w,) in struct.iter_unpack('<I', data):
        op = w >> 26
        words.append(w & (0xFC000000 if op in (2, 3) else 0xFFFF0000 if op else 0xFFFFFFFF))
    return struct.pack('<%dI' % len(words), *words)


def regionales(args):
    """Limites inferidos por prologo/retorno; no una prueba de equivalencia."""
    eu, eh = cargar_elf(args.elf_eu)
    us, uh = cargar_elf(args.elf_us)
    def referencias(imagen):
        # Solo pares ADYACENTES lui rt,hi / addiu dst,rt,lo: evidencia de una
        # direccion construida. No suponer propagacion por instrucciones intermedias.
        refs = {}
        words = struct.unpack('<%dI' % ((0x37E000 - 0x100000) // 4), imagen[0x100000:0x37E000])
        for i, (w, siguiente) in enumerate(zip(words, words[1:])):
            rt = (w >> 16) & 31
            if w >> 26 != 0xF or siguiente >> 26 != 9 or ((siguiente >> 21) & 31) != rt or rt == 0:
                continue
            destino = ((w & 0xFFFF) << 16) + struct.unpack('<h', struct.pack('<H', siguiente & 0xFFFF))[0]
            dstreg = (siguiente >> 16) & 31
            store = words[i + 2] if i + 2 < len(words) else 0
            refs.setdefault(destino, []).append({'lui': 0x100000 + i * 4,
                'addiu': 0x100000 + i * 4 + 4,
                'store_inmediato_estado_1c': store >> 26 == 0x2B
                    and ((store >> 16) & 31) == dstreg and store & 0xFFFF == 0x1C})
        return refs
    refs_e, refs_u = referencias(eu), referencias(us)
    filas = []
    for nombre, ea, fin_e, ua, fin_u, previo in (
            ('Charge', 0x30A5C0, 0x30A800, 0x30A3A0, 0x30A5E0, 0x30A3A0),
            ('Fire', 0x30A800, 0x30ADE0, 0x30A5E0, 0x30AB80, 0x30A5D4),
            ('Aim', 0x30ADE0, 0x30B110, 0x30AB80, 0x30AEA0, 0x30AB88)):
        cuerpos = []
        for region, imagen, ini, fin in [('eu', eu, ea, fin_e), ('us', us, ua, fin_u)]:
            words = list(struct.unpack('<%dI' % ((fin - ini) // 4), imagen[ini:fin]))
            pila = [ini + i * 4 for i, w in enumerate(words[:8])
                    if w & 0xFFFF0000 == 0x27BD0000 and w & 0x8000]
            # MIPS jr ra seguido de su delay slot; las palabras restantes deben ser padding.
            retornos = [ini + i * 4 for i, w in enumerate(words[:-1])
                        if w == 0x03E00008 and not any(words[i + 2:])]
            calls = [{'sitio': ini + i * 4, 'destino': (w & 0x3FFFFFF) << 2}
                     for i, w in enumerate(words) if w >> 26 == 3]
            cuerpos.append({'region': region, 'inicio': ini, 'fin_intervalo': fin,
                            'bytes': fin - ini, 'prologos_pila': pila, 'retornos_finales': retornos,
                            'sha256_intervalo': hashlib.sha256(imagen[ini:fin]).hexdigest(),
                            'llamadas': calls})
        a = list(struct.iter_unpack('<I', normalizar(eu[ea:fin_e])))
        b = list(struct.iter_unpack('<I', normalizar(us[ua:fin_u])))
        cambios = [{'tipo': t, 'eu': [ea + i * 4, ea + j * 4], 'us': [ua + x * 4, ua + y * 4]}
                   for t, i, j, x, y in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes()
                   if t != 'equal']
        limite = all(c['prologos_pila'] == [c['inicio']] and len(c['retornos_finales']) == 1 for c in cuerpos)
        viejo = struct.unpack_from('<I', us, previo)[0]
        re, ru = refs_e.get(ea, []), refs_u.get(ua, [])
        filas.append({'funcion': nombre, 'cuerpos': cuerpos,
                      'referencias_direccion_eu': re, 'referencias_direccion_us': ru,
                      'referencias_entrada_previa_us': refs_u.get(previo, []),
                      'entrada_previa_usa': previo,
                      'entrada_previa_es_padding': viejo == 0, 'limites_compatibles': limite,
                      'cambios_normalizados': cambios, 'equivalencia_funcional': 'no verificada'})
        primero = cambios[0] if cambios else None
        print(f'{nombre}: EU {ea:#x} -> US {ua:#x}; limites {int(limite)}/1, '
              f'referencias direccion {len(re)}/{len(ru)}; '
              f'cambios normalizados {len(cambios)}, primer DIF {primero}')
    total = sum(r['limites_compatibles'] for r in filas)
    return {'operacion': 'regionales', 'sha256_eu': eh, 'sha256_us': uh,
            'alcance': 'candidato por limites prologo/retorno; no equivalencia ni motor ejecutado',
            'limites_compatibles': total, 'total': len(filas), 'funciones': filas,
            'correcto': total == len(filas)}


def censo(args):
    eu, eh = cargar_elf(args.elf_eu)
    us, uh = cargar_elf(args.elf_us)
    text = args.ghidra.read_text(encoding='utf-8')
    nombres = [(int(a, 16), n.strip()) for a, n in re.findall(
        r'^// ==== ([0-9a-f]{8}) (.+)$', text, re.M)]
    if not nombres or any(a >= b for (a, _), (b, _) in zip(nombres, nombres[1:])):
        raise ValueError('lista de entradas vacia o no estrictamente creciente')
    nt = dict(nombres)
    usnorm = normalizar(us[0x100000:0x37E000])
    rows = []
    for i, (a, name) in enumerate(nombres[:-1]):
        fam = next((f for f, (lo, hi) in FAMILIAS.items() if lo <= a < hi), None)
        if fam is None:
            continue
        n = nombres[i + 1][0] - a
        body = bytes(eu[a:a + n])
        needle = normalizar(body)
        candidates = []
        lo, hi = max(0, a - 0x100000 - 0x2000), a - 0x100000 + 0x2000 + n
        k = usnorm.find(needle, lo, hi)
        while k >= 0:
            if k % 4 == 0:
                candidates.append(0x100000 + k)
            k = usnorm.find(needle, k + 1, hi)
        u = candidates[0] if len(candidates) == 1 else None
        calls = []
        for off, (w,) in enumerate(struct.iter_unpack('<I', body)):
            if w >> 26 == 3:
                dst = (w & 0x3FFFFFF) << 2
                calls.append({'sitio_eu': a + off * 4, 'destino_eu': dst, 'nombre': nt.get(dst)})
        rawdiff = None
        if u is not None:
            rawdiff = next((o for o in range(0, n, 4) if body[o:o + 4] != us[u + o:u + o + 4]), None)
        rows.append({'familia': fam, 'nombre': name, 'eu': a, 'usa': u, 'bytes': n,
                     'estado': 'N' if u is not None else 'P', 'candidatos': candidates,
                     'sha256_intervalo_eu': hashlib.sha256(body).hexdigest(),
                     'primer_inmediato_distinto': rawdiff, 'llamadas_directas_eu': calls})
    resumen = {}
    for fam in FAMILIAS:
        fs = [r for r in rows if r['familia'] == fam]
        ok = sum(r['estado'] == 'N' for r in fs)
        resumen[fam] = {'unicos': ok, 'total': len(fs)}
        print(f'{fam}: {ok}/{len(fs)} candidatos unicos; pendientes {len(fs) - ok}')
    return {'operacion': 'censo', 'sha256_eu': eh, 'sha256_us': uh, 'resumen': resumen,
            'metodo': 'intervalo completo hasta siguiente entrada Ghidra, sin tope 0x800; mascara gruesa; candidato unico +-0x2000',
            'limite': 'N no prueba limites nativos, inmediatos, destinos, globals ni comportamiento; incluye helpers del intervalo de cada familia',
            'funciones': rows}


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def muestras(tabla):
    # Vecinos FLOAT32 exactos, no math.nextafter de double.
    vals = {-1.0, -0.0, 0.0, 1000.0, 1.0e6, f32(1.1754943508222875e-38)}
    for t in tabla:
        b, = struct.unpack('<I', struct.pack('<f', float(t)))
        for k in (b - 1, b, b + 1):
            if 0 <= k < 0x7F800000:
                vals.add(struct.unpack('<f', struct.pack('<I', k))[0])
    top = max(tabla)
    vals.update(f32(top * i / 200) for i in range(201))
    return sorted(vals)


def clasificar_tabla(value, tabla, aciertos, clave):
    if clave == '7D7' and aciertos <= 6:
        return -1
    if value < 0 or value >= tabla[0]:
        return -1
    for i in range(5):
        if tabla[i + 1] < value <= tabla[i]:
            return i
    return -1


def clasificar_port(value, clave):
    # Acabar() vigente: 7D7/7D8 no condicionan rango a ganar.
    if clave == '7DE':
        if value <= 0 or value >= 1:
            return -1
        return (4 if value <= f32(0.0001) else 3 if value <= f32(0.1)
                else 2 if value <= f32(0.4) else 1 if value <= f32(0.7) else 0)
    return clasificar_tabla(value, [999, 75, 60, 45, 30, 0], 7, '7D8')


def rangos(args):
    resultados = []
    for region, path in [('eu', args.elf_eu), ('us', args.elf_us)]:
        mem, digest = cargar_elf(path)
        for clave, (init, rank, ptr, idx, table, size) in PERFILES[region].items():
            cpu = ee.Ee(bytearray(mem), 0x4A3DF0 if region == 'eu' else 0x4A35F0)
            cpu.estricto = True
            cpu.whitelist = {init, rank}  # ningun puente ni helper silenciado
            cpu.call(init, limite=100000)
            if ptr:
                cpu.w32(ptr, CONTEXTO)
            total = ok = port_ok = 0
            first = port_first = None
            tablas = []
            casos = []
            for dif in range(6):
                if ptr:
                    cpu.w32(CONTEXTO + idx, dif)
                t = table + (dif * 6 * size if ptr else 0)
                tabla = [cpu.rf(t + i * 4) if size == 4 else cpu.r16(t + i * 2) for i in range(6)]
                tablas.append(tabla)
                esperada = ([f32(x) for x in (1, .7, .4, .1, .0001, 0)]
                            if clave == '7DE' else [999, 75, 60, 45, 30, 0])
                if tabla != esperada:
                    raise ValueError(f'{region}/{clave}/dif={dif}: tabla inesperada {tabla}')
                for aciertos in ((0, 6, 7) if clave == '7D7' else (0,)):
                    if clave == '7D7':
                        cpu.w8(CONTEXTO + 0xC, aciertos)
                    for value in muestras(tabla):
                        r = cpu.call(rank, fargs=(value,), limite=5000) & 0xFFFFFFFF
                        actual = r if r < 0x80000000 else r - 0x100000000
                        # El EE lee los subnormales como cero. Conservar tambien el
                        # valor solicitado para que la frontera no desaparezca del informe.
                        efectivo = ee.fv(ee.fb(value))
                        esperado = clasificar_tabla(efectivo, tabla, aciertos, clave)
                        port = clasificar_port(efectivo, clave)
                        total += 1
                        ok += actual == esperado
                        port_ok += actual == port
                        caso = {'dificultad': dif, 'aciertos': aciertos, 'valor': value, 'valor_ee': efectivo,
                                'original': actual, 'esperado': esperado, 'port': port}
                        if actual != esperado and first is None:
                            first = caso
                        if actual != port and port_first is None:
                            port_first = caso
                        casos.append(caso)
            if cpu.sin_puente or cpu.io or cpu.desconocidas:
                raise ValueError(f'{region}/{clave}: ejecucion incompleta')
            row = {'region': region, 'clave': clave, 'elf_sha256': digest,
                   'init': init, 'rank': rank, 'tablas': tablas, 'iguales': ok, 'total': total,
                   'primera_diferencia': first, 'port_iguales': port_ok,
                   'primera_diferencia_port': port_first, 'casos': casos}
            resultados.append(row)
            print(f'{region}/{clave}: original vs contrato {ok}/{total}, primer DIF {first}; '
                  f'port {port_ok}/{total}, primer DIF {port_first}')
    return {'operacion': 'rangos', 'alcance': 'sintetico: init + clasificador, no motor/captura/resultado entero; modelo port con entrada normalizada por EE',
            'perfiles': resultados, 'correcto': all(r['iguales'] == r['total'] for r in resultados)}


def entero(x, lo, hi):
    return type(x) is int and lo <= x <= hi


def validar_datos(manifiesto, filas, clave_esperada=None, region_esperada=None):
    """Solo estructura de B8: nunca una afirmacion de igualdad de comportamiento."""
    errores = []
    if not isinstance(manifiesto, dict) or manifiesto.get('schema') != 'iw.disparo.v1':
        return ['schema debe ser iw.disparo.v1']
    clave = manifiesto.get('clave')
    region = manifiesto.get('region')
    if not isinstance(clave, str) or clave not in FAMILIAS or region not in ('eu', 'us'):
        errores.append('clave/region invalida')
    if clave_esperada is not None and clave != clave_esperada:
        errores.append('clave distinta de la solicitada')
    if region_esperada is not None and region != region_esperada:
        errores.append('region distinta de la solicitada')
    if isinstance(region, str) and region in ELF_HASH and manifiesto.get('elf_sha256') != ELF_HASH[region]:
        errores.append('ELF no corresponde a la region verificada')
    for field in ('elf_sha256', 'instantanea_sha256'):
        if not re.fullmatch('[0-9a-f]{64}', str(manifiesto.get(field, ''))):
            errores.append(f'{field} ausente/invalido')
    if not isinstance(manifiesto.get('instantanea'), str) or not manifiesto.get('instantanea'):
        errores.append('falta instantanea inicial')
    if not entero(manifiesto.get('dificultad'), 0, 5):
        errores.append('dificultad debe ser 0..5')
    if not isinstance(filas, list) or len(filas) < 2:
        return errores + ['banco vacio o sin pares continuos (minimo 2)']
    fases = set()
    mando_activo = False
    anterior = None
    for n, f in enumerate(filas):
        if not isinstance(f, dict):
            errores.append(f'fila {n}: no es objeto')
            continue
        if not entero(f.get('n'), 0, 0xFFFFFFFF) or f['n'] != n:
            errores.append(f'fila {n}: ordinal no continuo')
        fase = f.get('fase')
        if not isinstance(fase, str) or fase not in FASES:
            errores.append(f'fila {n}: fase invalida')
        else:
            fases.add(fase)
        if (n == 0 and fase != 'init') or (n == len(filas) - 1 and fase != 'fin'):
            errores.append(f'fila {n}: la secuencia debe empezar en init y terminar en fin')
        e, s = f.get('entrada'), f.get('salida')
        if not isinstance(e, dict) or not isinstance(s, dict):
            errores.append(f'fila {n}: falta entrada/salida pareada')
            continue
        if not entero(e.get('mando'), 0, 0xFFFF) or not entero(e.get('semilla'), 0, 0xFFFFFFFF):
            errores.append(f'fila {n}: mando/semilla invalido')
        mando_activo |= entero(e.get('mando'), 1, 0xFFFF)
        if not entero(e.get('paso_bits'), 0, 0xFFFFFFFF):
            errores.append(f'fila {n}: falta paso_bits')
        else:
            paso, = struct.unpack('<f', struct.pack('<I', e['paso_bits']))
            if not math.isfinite(paso) or paso < ee.MINF:
                errores.append(f'fila {n}: paso_bits debe ser float32 normal finito positivo (EE no subnormal)')
        if anterior is not None and e.get('semilla') != anterior:
            errores.append(f'fila {n}: semilla no continua desde la salida anterior')
        anterior = s.get('semilla')
        if not OBSERVABLES <= s.keys():
            errores.append(f'fila {n}: faltan observables {sorted(OBSERVABLES - s.keys())}')
            continue
        for field in ('reloj_bits', 'semilla', 'vida_bits', 'potencia_bits'):
            if not entero(s[field], 0, 0xFFFFFFFF):
                errores.append(f'fila {n}: {field} no es u32')
        for field in ('pose', 'camara', 'sonido'):
            if not isinstance(s[field], list):
                errores.append(f'fila {n}: eventos {field} no es lista')
                continue
            for i, evento in enumerate(s[field]):
                if (not isinstance(evento, dict) or not entero(evento.get('id'), 0, 0xFFFFFFFF)
                        or not isinstance(evento.get('parametros_bits'), list)
                        or not all(entero(v, 0, 0xFFFFFFFF) for v in evento['parametros_bits'])):
                    errores.append(f'fila {n}: evento {field}[{i}] sin id/parametros_bits validos')
        if type(s['fin']) is not bool or not entero(s['rango'], -1, 4):
            errores.append(f'fila {n}: fin/rango invalido')
        if n < len(filas) - 1 and s['fin'] is True:
            errores.append(f'fila {n}: cierre anterior al final de la secuencia')
        if not entero(s['estado'], 0, 0xFFFFFFFF) or not entero(s['impactos'], 0, 0xFFFFFFFF):
            errores.append(f'fila {n}: estado/impactos invalido')
    faltan = {'init', 'ready', 'aim', 'fuego', 'impacto', 'fin'} - fases
    if clave == '7DE':
        faltan |= {'carga'} - fases
    if faltan:
        errores.append(f'faltan fases objetivo {sorted(faltan)}')
    if not mando_activo:
        errores.append('captura sin entradas activas de mando')
    last = filas[-1] if isinstance(filas[-1], dict) else {}
    if not isinstance(last.get('salida'), dict) or last['salida'].get('fin') is not True:
        errores.append('captura sin cierre observable')
    return errores


def leer_json(texto):
    def objeto(pares):
        resultado = {}
        for k, v in pares:
            if k in resultado:
                raise ValueError(f'JSON con clave repetida: {k}')
            resultado[k] = v
        return resultado

    def constante(valor):
        raise ValueError(f'constante JSON no finita: {valor}')

    return json.loads(texto, object_pairs_hook=objeto, parse_constant=constante)


def validar(args):
    manifest = leer_json(args.manifiesto.read_text(encoding='utf-8-sig'))
    filas = [leer_json(line) for line in args.filas.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    errores = validar_datos(manifest, filas, args.clave, args.region)
    if not isinstance(manifest, dict):
        return {'operacion': 'validar', 'alcance': 'solo estructura, sin replay',
                'pares_logicos_verificados': 0, 'errores': errores, 'correcto': False}
    ruta = manifest.get('instantanea')
    snap = args.manifiesto.parent / ruta if isinstance(ruta, str) and ruta else None
    if snap is None or not snap.is_file():
        errores.append('instantanea ausente')
    elif sha256(snap) != manifest.get('instantanea_sha256'):
        errores.append('hash instantanea no coincide')
    if sha256(args.elf) != manifest.get('elf_sha256'):
        errores.append('hash ELF no coincide')
    print(f'B8 estructura: {len(filas)} filas; errores {len(errores)}; '
          f'primer DIF {errores[0] if errores else None}; pares logicos verificados 0')
    return {'operacion': 'validar', 'alcance': 'solo estructura e identidad; sin replay',
            'clave_solicitada': args.clave, 'region_solicitada': args.region,
            'filas': len(filas), 'pares_estructurales': len(filas) if not errores else 0,
            'pares_logicos_verificados': 0, 'errores': errores, 'correcto': not errores}


def autoprueba(_):
    import copy
    m = {'schema': 'iw.disparo.v1', 'clave': '7DE', 'region': 'eu', 'dificultad': 2,
         'elf_sha256': ELF_HASH['eu'], 'instantanea_sha256': 'b' * 64, 'instantanea': 'inicio.p2s'}
    fs = [{'n': n, 'fase': fase,
           'entrada': {'mando': 0x2000, 'semilla': 1, 'paso_bits': 0x3F99999A},
           'salida': {'estado': n, 'reloj_bits': 0, 'semilla': 1, 'vida_bits': 0,
                      'potencia_bits': 0, 'impactos': 0, 'fin': fase == 'fin',
                      'rango': -1, 'pose': [], 'camara': [], 'sonido': []}}
          for n, fase in enumerate(('init', 'ready', 'aim', 'carga', 'fuego', 'impacto', 'fin'))]
    casos = [('estructura sintetica completa', m, fs, True)]
    for name, change in [
        ('vacio', lambda a, b: b.clear()),
        ('sin pareja', lambda a, b: b[2].pop('entrada')),
        ('hueco ordinal', lambda a, b: b[2].update(n=20)),
        ('semilla negativa', lambda a, b: b[2]['entrada'].update(semilla=-1)),
        ('mando desbordado', lambda a, b: b[2]['entrada'].update(mando=65536)),
        ('dificultad bool', lambda a, b: a.update(dificultad=True)),
        ('sin camara', lambda a, b: b[2]['salida'].pop('camara')),
        ('sin hash', lambda a, b: a.pop('elf_sha256')),
        ('sin carga', lambda a, b: b[3].update(fase='aim')),
        ('sin cierre', lambda a, b: b[-1]['salida'].update(fin=False)),
        ('sin entradas', lambda a, b: [f['entrada'].update(mando=0) for f in b]),
        ('rango desbordado', lambda a, b: b[-1]['salida'].update(rango=5)),
        ('observacion no objeto', lambda a, b: b[2].update(salida=[])),
        ('clave no escalar', lambda a, b: a.update(clave=[])),
        ('fase no escalar', lambda a, b: b[2].update(fase=[])),
        ('region falsa', lambda a, b: a.update(region='us')),
        ('instantanea no ruta', lambda a, b: a.update(instantanea=3)),
        ('semilla discontinua', lambda a, b: b[2]['entrada'].update(semilla=2)),
        ('paso cero', lambda a, b: b[2]['entrada'].update(paso_bits=0)),
        ('paso cero negativo', lambda a, b: b[2]['entrada'].update(paso_bits=0x80000000)),
        ('paso subnormal', lambda a, b: b[2]['entrada'].update(paso_bits=1)),
        ('paso negativo', lambda a, b: b[2]['entrada'].update(paso_bits=0xBF800000)),
        ('paso NaN', lambda a, b: b[2]['entrada'].update(paso_bits=0x7FC00000)),
        ('paso infinito', lambda a, b: b[2]['entrada'].update(paso_bits=0x7F800000)),
        ('fin adelantado', lambda a, b: b[2]['salida'].update(fin=True)),
        ('inicio falso', lambda a, b: b.insert(0, dict(b[2], n=0))),
        ('evento opaco', lambda a, b: b[2]['salida'].update(sonido=['desconocido'])),
        ('evento sin parametros', lambda a, b: b[2]['salida'].update(pose=[{'id': 1000}])),
    ]:
        a, b = copy.deepcopy(m), copy.deepcopy(fs)
        change(a, b)
        casos.append((name, a, b, False))
    ok, first = 0, None
    for clave in FAMILIAS:
        for region in ELF_HASH:
            a, b = copy.deepcopy(m), copy.deepcopy(fs)
            a.update(clave=clave, region=region, elf_sha256=ELF_HASH[region])
            b[2]['salida']['sonido'] = [{'id': 3, 'parametros_bits': [0x3F800000]}]
            casos.append((f'estructura {clave}/{region}', a, b, True))
    resultados = []
    for name, a, b, expected in casos:
        actual = not validar_datos(a, b)
        ok += actual == expected
        resultados.append({'caso': name, 'esperado': expected, 'obtenido': actual})
        if actual != expected and first is None:
            first = name
    for name, texto in [('JSON repetido', '{"n":0,"n":1}'), ('JSON NaN', '{"n":NaN}')]:
        try:
            leer_json(texto)
            actual = False
        except ValueError:
            actual = True
        resultados.append({'caso': name, 'esperado': True, 'obtenido': actual})
        ok += actual
        if not actual and first is None:
            first = name
    for name, clave, region in [('clave solicitada', '7D7', 'eu'), ('region solicitada', '7DE', 'us')]:
        actual = bool(validar_datos(m, fs, clave, region))
        resultados.append({'caso': name, 'esperado': True, 'obtenido': actual})
        ok += actual
        if not actual and first is None:
            first = name
    print(f'Contrato B8 sintetico: {ok}/{len(resultados)}, primera diferencia {first}')
    return {'operacion': 'autoprueba', 'iguales': ok, 'total': len(resultados), 'casos': resultados,
            'primera_diferencia': first, 'correcto': ok == len(resultados)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='comando', required=True)
    for name in ('censo', 'rangos', 'regionales'):
        p = sub.add_parser(name)
        p.add_argument('--elf-eu', type=Path, default=ELF_EU)
        p.add_argument('--elf-us', type=Path, default=ELF_US)
        if name == 'censo':
            p.add_argument('--ghidra', type=Path, default=GHIDRA)
        p.add_argument('--salida', type=Path)
    p = sub.add_parser('validar')
    p.add_argument('--manifiesto', type=Path, required=True)
    p.add_argument('--filas', type=Path, required=True)
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--clave', choices=FAMILIAS, required=True)
    p.add_argument('--region', choices=ELF_HASH, required=True)
    p.add_argument('--salida', type=Path)
    p = sub.add_parser('autoprueba')
    p.add_argument('--salida', type=Path)
    args = parser.parse_args()
    start = time.monotonic()
    try:
        report = globals()[args.comando](args)
        report['segundos_banco'] = round(time.monotonic() - start, 3)
        if args.salida:
            # Solo un padre existente: nunca crea arboles ni modifica referencias.
            with args.salida.open('x', encoding='utf-8') as f:
                json.dump(report, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if report.get('correcto', True) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print(f'FALLO: {exc}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

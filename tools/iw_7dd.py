# SPDX-License-Identifier: MIT
"""Cotejo 7DD/906 sin juego ni PCSX2: registros, estados PAL y banco causal."""
from pathlib import Path
import argparse
import collections
import json
import math
import re
import struct
import subprocess
import sys
import hashlib
from contextlib import contextmanager
from datetime import datetime
from itertools import islice

sys.dont_write_bytecode = True
BASE = Path(r'E:\DBZ3HD_trabajo')
RAIZ = Path(__file__).resolve().parents[1]
LOG = BASE / 'fabrica/NIWG/evidencias/01_niwe_7dd/dbz3.log'
REF = BASE / 'GRAB_NOCHE/alcance7dd_noche.jsonl'
CAM = re.compile(r'\bcam7dd=([0-9A-F]+),([0-9A-F]+)\b')


def cargar_herramientas(raiz):
    sys.path.insert(0, str(raiz / 'tools/iw_mips'))
    import persecucion
    return persecucion


def hexs(s):
    return [int(x, 16) for x in s.split(',') if x]


def ticks_log(P, log):
    with log.open(encoding='utf-8', errors='replace') as f:
        for linea, texto in enumerate(f, 1):
            m = P.RX.search(texto)
            if not m:
                continue
            t, ini, si, so, entrada, col, salida, eventos = m.groups()
            w, o = hexs(entrada), hexs(salida)
            if len(w) != 20 or len(o) != 10:
                raise ValueError(f'log:{linea}: ancho de registro incorrecto')
            cam = CAM.search(texto)
            yield dict(t=int(t), linea=linea, ini=int(ini), seed=int(si, 16), seed_out=int(so, 16),
                       pic0=w[:3], j40=w[3:6], j640=w[6:9], p640=w[9:12], p770=w[12:15],
                       dist=w[15], jcbc=w[16], dif=w[17], t25a=w[18], w180=w[19], out=o,
                       col=col, ev=eventos, hora=texto[1:24],
                       cam=[int(x, 16) for x in cam.groups()] if cam else None)


def resumen_log(P, log):
    prev, primero, minimo = None, None, None
    estados, tipos = collections.Counter(), collections.Counter()
    gaps = iguales = 0
    saltos = {}
    for r in ticks_log(P, log):
        if prev:
            gaps += 1
            iguales += prev['seed_out'] == r['seed']
            if r['ini'] or r['t'] != prev['t'] + 1:
                raise ValueError('El log debe contener una sola partida continua')
            if prev['seed_out'] != r['seed']:
                saltos[r['t']] = pasos_rng(P, prev['seed_out'], r['seed'])
        primero = primero or r
        prev = r
        estados[r['out'][1]] += 1
        tipos[r['t25a']] += 1
        if minimo is None or P.b2f(r['dist']) < P.b2f(minimo['dist']):
            minimo = r
    if not primero or not primero['ini'] or primero['t'] != 1:
        raise ValueError('Log sin primer tick de persecución')
    return dict(ticks=sum(estados.values()), estados=estados, tipos=tipos, semillas_sin_avance=iguales, huecos=gaps,
                saltos_rng=saltos,
                segundos_muro=(datetime.fromisoformat(prev['hora']) - datetime.fromisoformat(primero['hora'])).total_seconds(),
                primero=primero, ultimo=prev, minimo=dict(t=minimo['t'], linea=minimo['linea'], valor=P.b2f(minimo['dist'])))


def pasos_rng(P, a, b):
    for n in range(4097):
        if a == b:
            return n
        a = P.lfsr(a, 1)
    raise ValueError('salto de RNG no explicable en 4096 pasos; no se pueden parear entradas')


def muestras(P, cap, p2s):
    meta = json.loads((cap / 'parejas.json').read_text(encoding='utf-8'))
    dat = (cap / 'parejas.bin').read_bytes()
    b0 = meta['base']
    tam = max(o + n for bl in meta['bloques'].values() for _, _, n, o in bl) - b0
    mem, _ = P.B.cargar(P.B.ELF, p2s)
    base = bytes(mem)
    for i, par in enumerate(meta['parejas']):
        mem[:] = base
        off = par['off']
        if not 0 <= off <= len(dat) - tam:
            raise ValueError(f'pareja truncada: {i}')
        sitios = P._bloques(meta, dat[off:off + tam], b0, mem, 'A')
        yield i, par['cnt'], mem, sitios


def original(P, cap, p2s):
    prim, prim13, capt = {}, None, None
    campo = lambda m, a, o: struct.unpack_from('<3f', m, a + o)
    valores = collections.defaultdict(collections.Counter)
    for i, cnt, mem, _ in muestras(P, cap, p2s):
        mgr = P.u32(mem, 0x49CA70)
        jug, pic = mgr + 0x800, mgr + 0x28D0
        if not mgr or pic + 0x20D0 > len(mem):
            raise ValueError(f'gestor inválido en pareja {i}')
        est, j = (struct.unpack_from('<h', mem, a + 0xCBC)[0] for a in (pic, jug))
        f = dict(i=i, contador=cnt, estado=est, jugador=j, p=campo(mem, pic, 0x40), j40=campo(mem, jug, 0x40),
                 p640=campo(mem, pic, 0x640), j640=campo(mem, jug, 0x640), p770=campo(mem, pic, 0x770),
                 aire=mem[pic + 0x2061], semilla=hex(P.u32(mem, P.B.SEMILLA)))
        prim.setdefault(est, f)
        if est == 13 and prim13 is None:
            prim13 = f
        if j == 9 and capt is None:
            capt = f
        if est in (3, 4, 8, 9, 12, 13, 14, 15, 16):
            for o in (0xB54, 0xCAC, 0x18D8):
                valores[hex(o)][P.b2f(P.u32(mem, pic + o))] += 1
            valores['tipo25a'][struct.unpack_from('<H', mem, P.u32(mem, 0x49CBB8) + 0x25A)[0]] += 1
        if est == 9:
            work = P.u32(mem, 0x49CBC0)
            valores['pose9_eje'][str(campo(mem, work + 0x260, 0x40))] += 1
    if not prim13 or not capt:
        raise ValueError('El corpus no contiene entrada 13 y captura 9')
    return dict(prim_por_estado=prim, primer13=prim13, captura=capt, valores=valores)


@contextmanager
def wcf(exe, campo):
    # Protocolo de tools/iw_mips/rayo7dd.cpp; sin backend ficticio si falla.
    with subprocess.Popen([str(exe), str(campo)], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, text=True) as proc:
        def enviar(k, a, b, radio, mask):
            from persecucion import b2f, B
            coords = ' '.join(repr(b2f(v)) for v in (*a, *b))
            suf = f'{b2f(radio)!r} {mask}' if k == 'S' else str(mask)
            proc.stdin.write(f'{k if k == "S" else "R"} {coords} {suf}\n')
            proc.stdin.flush()
            h, x, y, z, t, mt, nx, ny, nz, d = proc.stdout.readline().split()
            fp = lambda *v: [B.f2b(float(x)) for x in v]
            return int(h), fp(x, y, z), fp(nx, ny, nz), fp(d)[0], int(mt), int(t)
        try:
            yield enviar
        finally:
            proc.stdin.close()
            try:
                proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=4)
                raise
        if proc.returncode:
            raise RuntimeError(f'arnés WCF: código {proc.returncode}')


def inicio_suelo(P, esp, dificultad):
    # 0x35FAE0 (por tabla): origen de la presentación. Al liberarla, 0x29BB70
    # entra en 0. No sintetizar el salto posterior ni copiar su posición de una foto.
    c, A = esp.cpu, esp.A
    esp.preparar()
    c.w16(P.CTXD + 0xC, dificultad)
    G = P.GRID
    for a, v in ((A.P_GRID, G), (G + 0x630, 7), (G + 0xAD4, P.B.f2b(3000.)), (G + 0xAD8, P.B.f2b(3000.)),
                 (G + 0xADC, 10), (G + 0xAE0, 10), (G + 0xAF0, P.B.f2b(-1350.)), (G + 0xAF8, P.B.f2b(-1350.))):
        c.w32(a, v)
    c.call(A.ENTRAR, P.PIC, 22)
    c.call(A.ENTRAR, P.PIC, 0)
    if esp.R is None or esp.clip_raiz is None:
        raise ValueError('faltan los recursos nativos de la raíz de Piccolo')
    esp.vivo = esp.R.muestra(esp.clip_raiz, 0.)
    esp._ancla()
    return [P.b2f(c.r32(P.PIC + 0x40 + i * 4)) for i in range(3)]


def simular(P, registros, proveedor, usa, suelo=False, entre=False, semilla=None, ejes=None):
    esp = P.Espejo(usa=usa)
    esp.proveedor = proveedor
    if esp.R is None:
        raise ValueError('faltan recursos nativos del espejo')
    c = esp.cpu
    r0 = registros[0]
    if ejes:
        # P.Espejo reinicia W0/W1 por dentro en el arranque base.
        def nuevo():
            preparar()
            for i, v in enumerate((0, 0, 0x3F800000, 0x3F800000)):
                c.w32(P.W1 + 0x30 + 4*i, v)
        preparar = esp.preparar
        esp.preparar = nuevo
    paso = P.b2f(esp.A.PASO_BITS)
    if suelo:
        spawn = inicio_suelo(P, esp, r0['dif'])
    else:
        spawn = [P.b2f(x) for x in r0['pic0']]
    seed = r0['seed'] if semilla is None else semilla
    primero13 = contacto = None
    min_dist = math.inf
    estados, tareas, vision = (collections.Counter() for _ in range(3))
    cambios = []
    primera_dif = primera_pos = None
    primera_embestida = None
    iguales = 0
    cam_f = None
    prev_root = spawn
    if ejes == 'host':
        # kYawOjoIW[14], LanzarSolo y Piccolo7DD; aproximación Python float32.
        yaw = P.f32(P.f32(150.61 + 180.0) * P.f32(math.pi / 180))
        cam_f = [P.B.f2b(math.sin(yaw)), P.B.f2b(math.cos(yaw))]
    for i, r in enumerate(registros):
        if i and not entre:
            seed = P.lfsr(seed, pasos_rng(P, registros[i-1]['seed_out'], r['seed']))
        p640 = [c.r32(P.PIC + 0x640 + 4*j) for j in range(3)] if i or suelo else r0['p640']
        p770 = [p640[0], P.B.f2b(P.b2f(p640[1]) + 5.5), p640[2]]
        distancia = math.dist([P.b2f(v) for v in p640], [P.b2f(v) for v in r['j640']])
        e = dict(r, ini=int(not i and not suelo), p640=p640, p770=p770,
                 dist=P.B.f2b(distancia), rayos=[], seed=seed)
        cam = (r['cam'] if ejes == 'log' else cam_f if ejes == 'host' else
               (0xBEFAE148, 0x3F5EB852) if ejes == 'nativo' else None)
        if ejes == 'log' and cam is None:
            raise ValueError(f'falta cam7dd en t={r["t"]}')
        if cam is not None:
            def escribir_eje():
                for j, v in enumerate((cam[0], 0, cam[1], 0x3F800000)):
                    c.w32(P.W0 + 0x40 + 4*j, v)
            if e['ini']:
                def primer_preparar():
                    nuevo()
                    escribir_eje()
                esp.preparar = primer_preparar
            else:
                escribir_eje()
        o = esp.tick(e)
        if o['fallo']:
            raise RuntimeError(f'EE no válido en t={r["t"]}')
        sale = [o['codigo'], o['estado'], o['clip'], *o['p'], o['tarea'], o['modo'], o['vuela'], o['w1a4']]
        cols = ''.join('%s%d/%X,%X,%X/%X,%X,%X/%X/%X/%X;' % (k, h, *p, *n, d, mt, tp)
                       for k, h, p, n, d, mt, tp in o['col']) or '-'
        coincide = cols == r['col'] and sale == r['out'] and seed == r['seed'] and o['seed'] == r['seed_out'] \
            and (','.join(f'{v:X}' for v in o['ev']) or '-') == r['ev']
        iguales += coincide
        if primera_dif is None and not coincide:
            primera_dif = dict(t=r['t'], esperado=r['out'], obtenido=sale, col_log=r['col'], col_banco=cols,
                                seed_in_log=hex(r['seed']), seed_in=hex(seed), seed_out_log=hex(r['seed_out']),
                                seed_out=hex(o['seed']))
        if primera_pos is None and sale[3:6] != r['out'][3:6]:
            primera_pos = r['t']
        if o['estado'] == 9 and primera_embestida is None:
            primera_embestida = dict(t=r['t'], base_camara=[P.b2f(c.r32(P.W1 + 0x40 + 4*j)) for j in range(3)],
                p=[P.b2f(x) for x in o['p']], avance=math.dist(prev_root, [P.b2f(x) for x in o['p']]))
        prev_root = [P.b2f(x) for x in o['p']]
        seed = o['seed']
        if entre:
            seed = P.lfsr(seed, P.pasos_entre_port(seed))  # solo ablación F09, no es autoridad nativa
        est = o['estado']
        if not cambios or est != cambios[-1]['estado']:
            cambios.append(dict(t=r['t'], previo=o['codigo'], estado=est,
                                p=[P.b2f(v) for v in o['p']], distancia=distancia))
        estados[est] += 1
        vision.update(h for _, h in esp.vision)
        esp.vision.clear()
        if 0x7A000000 in o['ev']:
            tareas[o['modo']] += 1
        if est == 13 and primero13 is None:
            primero13 = r['t']
        if P.b2f(e['dist']) <= 8 and o['codigo'] != 11 and o['codigo'] < 18 and contacto is None:
            contacto = r['t']
        min_dist = min(min_dist, distancia)
    return dict(spawn=spawn, ticks=len(registros), paso_ee=paso, estados=estados, tareas=tareas, rayos_vision=vision,
                primero13=primero13, contacto_anclas8=contacto, minimo=min_dist, cambios=cambios,
                desde_inicio_s=contacto * paso / 60 if contacto is not None else None,
                desde_primer13_s=(contacto-primero13) * paso / 60 if primero13 and contacto and primero13 <= contacto else None,
                iguales_log=iguales, primera_dif_log=primera_dif, primera_pos_distinta=primera_pos,
                primera_embestida=primera_embestida)


def banco(P, args):
    registros = list(islice(ticks_log(P, args.log), args.ticks))
    if len(registros) != args.ticks or any(r['t'] != i for i, r in enumerate(registros, 1)):
        raise ValueError('faltan ticks contiguos desde t=1')
    if not registros[0]['ini'] or any(r['ini'] for r in registros[1:]):
        raise ValueError('se requiere una sola partida')
    casos = []
    for region in args.region or ['usa']:
        for semilla in args.semilla or [registros[0]['seed']]:
            for variante in args.variante or ['base', 'ejes-host' if not registros[0]['cam'] else 'ejes-log']:
                with wcf(args.rayo or P.RAYO_EXE, args.wcf or P.WCF) as proveedor:
                    casos.append(dict(region=region, semilla=hex(semilla), variante=variante,
                                      **simular(P, registros, proveedor, region == 'usa',
                                                suelo='suelo' in variante,
                                                ejes={'ejes-log': 'log', 'ejes-host': 'host',
                                                      'ejes': 'nativo', 'suelo-ejes': 'nativo',
                                                      'frente': 'solo-frente'}.get(variante),
                                                entre='rng' in variante, semilla=semilla)))
    ref = []
    with args.referencia.open(encoding='utf-8') as f:
        ref = [json.loads(x) for x in f if x.strip()]
    segundos = [x['cuadros']/50 for x in ref if x.get('alcanza') is True]
    if not segundos:
        raise ValueError('faltan capturas originales PAL')
    return dict(alcance='EE + backend WCF en arnés. Inyecta jugador y saltos externos de RNG del log '
                        '(salvo ablación F09); contacto es predicado, no captura. Referencia PAL no pareada.',
                referencia_pal=dict(partidas=len(ref), capturas=len(segundos), min_s=min(segundos), max_s=max(segundos)),
                entradas=[huella(p) for p in [args.log, args.referencia, args.rayo or P.RAYO_EXE, args.wcf or P.WCF,
                          *[P.ELF_USA if r == 'usa' else P.B.ELF for r in dict.fromkeys(c['region'] for c in casos)],
                          Path(P.ee.__file__), Path(P.__file__), Path(P.RZ.__file__), P.RZ.CMN_USA]], casos=casos)


def huella(path):
    with path.open('rb') as f:
        return dict(archivo=str(path), sha256=hashlib.file_digest(f, 'sha256').hexdigest())


def exigir(condicion, mensaje):
    if not condicion:
        raise ValueError(mensaje)


def self_check(P):
    a = argparse.Namespace(log=LOG, ticks=3721, region=['usa'], variante=['base', 'frente', 'ejes-host'],
                           semilla=None, rayo=None, wcf=None, referencia=REF)
    h = resumen_log(P, a.log)
    exigir(h['minimo']['t'] == 1687 and abs(h['minimo']['valor'] - 300.53955) < 0.001,
           'No se reprodujo NIWG-01')
    exigir(h['saltos_rng'] == {336: 332, 353: 332, 370: 332}, 'Saltos de RNG distintos')
    dorado = original(P, P.CAP_TICK, P.P2S)
    exigir(dorado['prim_por_estado'][22]['p'] == (-10., -50., -966.) and
           dorado['valores']['pose9_eje'] and all(not k.startswith('(0.0, 0.0, 0.0)')
                                                 for k in dorado['valores']['pose9_eje']), 'Dorado PAL inesperado')
    r = banco(P, a)
    base, frente, ejes = r['casos']
    exigir(base['iguales_log'] == frente['iguales_log'] == a.ticks and base['contacto_anclas8'] is None,
           'La base no reproduce todo el log o el control de solo frente no es neutro')
    exigir(ejes['primera_pos_distinta'] == ejes['primera_embestida']['t'] == 41 and
           base['primera_embestida']['avance'] == 0. and 12.1 < ejes['primera_embestida']['avance'] < 12.2,
           'El eje de cámara no restaura la primera embestida de NIWG')
    exigir(ejes['contacto_anclas8'] == 1340 and ejes['minimo'] < 2., 'No se reprodujo el contacto offline')
    print('NIWI: base 3721/3721; control solo frente 3721/3721; tick 41 avance 0 -> 12.13 u;')
    print('NIWI: contacto de anclas t=1340 (22.33 s NTSC sinteticos); no es una captura en juego')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raiz', type=Path, default=RAIZ, help='raíz operativa (por defecto, la de este script)')
    p.add_argument('--log', type=Path, default=LOG, help='dbz3.log de una sola partida 7DD; por defecto NIWG-01')
    p.add_argument('--referencia', type=Path, default=REF, help='dorado histórico de capturas PAL (JSONL)')
    p.add_argument('--parejas', type=Path, help='carpeta PAL con parejas.json y parejas.bin')
    p.add_argument('--p2s', type=Path, help='savestate PAL de 7DD del mismo corpus')
    p.add_argument('--salida', type=Path, help='JSON nuevo; no sobrescribe evidencia')
    p.add_argument('--ticks', type=int, default=3721)
    p.add_argument('--region', choices=('pal', 'usa'), action='append')
    p.add_argument('--semilla', type=lambda x: int(x, 0), action='append')
    p.add_argument('--variante', choices=('base', 'suelo', 'ejes', 'suelo-ejes', 'ejes-host', 'ejes-log',
                                         'frente', 'rng', 'suelo-rng'), action='append',
                   help='base/ejes-host de serie; ejes-log usa cam7dd guardada; suelo/RNG son experimentos')
    p.add_argument('--rayo', type=Path, help='arnés offline compilado de tools/iw_mips/rayo7dd.cpp')
    p.add_argument('--wcf', type=Path, help='colisión original #WCF 907/0')
    p.add_argument('--self-check', '--autoprueba', action='store_true',
                   help='regresión fija de NIWG-01 y control negativo; requiere el corpus local')
    p.add_argument('modo', nargs='?', choices=('log', 'original', 'banco'),
                   help='resume un registro, lee las parejas PAL o simula la persecución con colisión WCF')
    a = p.parse_args()
    P = cargar_herramientas(a.raiz)
    if a.ticks < 1 or any(not 0 < x <= 0xFFFFFFFF for x in a.semilla or []):
        p.error('ticks positivos y semillas uint32 no nulas')
    if a.self_check:
        self_check(P)
        return 0
    if not a.modo:
        p.error('elige un modo o --self-check')
    r = (resumen_log(P, a.log) if a.modo == 'log' else banco(P, a) if a.modo == 'banco' else
         original(P, a.parejas or P.CAP_TICK, a.p2s or P.P2S))
    texto = json.dumps(r, ensure_ascii=True, indent=2, allow_nan=False)
    if a.salida:
        with a.salida.open('x', encoding='utf-8') as f:
            f.write(texto + '\n')
    else:
        print(texto)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

# SPDX-License-Identifier: MIT
"""B2-P07a: quads 2D del ORIGINAL de B2 (b2-quads-v1) a partir de un savestate de PCSX2, sin emulador.

Carga la RAM del .p2s con el ELF (tools/mips_perfil.py) y ejecuta en el interprete (tools/iw_mips/ee.py) UNA pasada de
las tareas vivas del gestor (EU 0x1483B0 FUN_001483b0: lista 0x3B4B58 -> +0x28 hasta 0x3B4BB0, funcion en +0x1C). Cada
sprite 2D del original sale por EU 0x1A9030 FUN_001a9030(rect, banderas, pose, color): su quad logico (plano 640x448) es
  pose+0x10 (x, y) + rect (x0, y0, x1, y1) escalado por pose+4 / pose+8   (EU 0x1A81A0 FUN_001a81a0)
y la pose sale justo antes de EU 0x1A86E0 FUN_001a86e0(cuadro f12, anim a0 = tabla del #AMA + anim*0x40, salida a1).
Identidad: ama = indice del DATA_CMN cuyo #AMA (en RAM '!AMA', +8 n, +0xC tabla) contiene la entrada; anim; cuadro
(redondeado a 0,1: en PAL avanza 1,2 por campo, FUN_002452a0 + gp-0x7994); instancia = img<imagen>#<k>, imagen = la
entrada de la tabla con el mismo rect (FUN_002452a0 toma x0,y0 de [0],[1] y x1,y1 de [4],[5]) y k el orden por (y0, x0)
entre las del mismo ama/anim/cuadro/imagen (lo mismo escribe el port).

  python tools/b2_quads_original.py <estado.p2s> <escena> <salida.json> [--campo N] [--amas 399] [--solo 0x2a1e30,...]

--amas: solo los quads de esos #AMA del DATA_CMN (la ventana de aliados es el 399; el tablero de debajo, 344/343/397,
lo dibujan tareas 3D). --aceptar-fallos: ver "fallos" del JSON; desde B2-P07b parte 2 (ee.py con vmax/vmini y la espera
de DMA 0x1434C0 puenteada) las tareas del tablero corren enteras y en las listas de aliados solo falla el texto 0x2A72E0
(syscall 100), que no usa los #AMA 399/402/397.

--campo: campos PAL (50 Hz) desde que se abre la escena. El savestate no lo sabe: sin --campo se escribe -1 y
comparar_modo.py b2-quads da PENDIENTE_DATO (no se inventa el instante).
"""
import argparse
import json
import os
import struct
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(os.environ.get('DBZ3_RAIZ') or Path(__file__).resolve().parents[1])   # DBZ3_RAIZ: probarlo fuera del repo
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/iw_mips'), str(ROOT / 'mod center hd'), str(ROOT / 'mod center hd/pasarela')]
import ee  # noqa: E402
import mips_perfil  # noqa: E402

ISO = ROOT / 'ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso'
LISTA, FIN = 0x3B4B58, 0x3B4BB0
POSE, EMITIR, REENCOLAR, DIBAMA = 0x1A86E0, 0x1A9030, 0x1A91C0, 0x1A8D10


def amas_de_ram(cpu, ram):
    """'!AMA' en RAM -> (base, n). La tabla empieza en base+0x10 (+0xC apunta a ella)."""
    r, p = [], ram.find(b'!AMA')
    while p >= 0:
        n, tabla = struct.unpack_from('<II', ram, p + 8)
        if tabla == p + 0x10 and 0 < n < 4096:
            r.append((p, n))
        p = ram.find(b'!AMA', p + 4)
    return r


def cmn_de_ama(ram, amas):
    """base en RAM -> indice del DATA_CMN, por n y la tabla (n*0x40 bytes tras la cabecera)."""
    from iso import Iso
    from pasarela import Afs
    afs = Afs(Iso(str(ISO)), '/USR/DATA_CMN.AFS')
    porn = defaultdict(list)
    for k in range(len(afs.tab)):
        h = afs.head(k, 0x10)
        if h[:4] == b'#AMA':
            porn[struct.unpack_from('<I', h, 8)[0]].append(k)
    r = {}
    for base, n in amas:
        tabla = ram[base + 0x10:base + 0x10 + 0x40 * n]
        for k in porn.get(n, ()):
            d = afs.entry(k)
            # las entradas llevan punteros a sus claves (reubicados en RAM): se comparan solo las palabras que no cambian
            iguales = sum(a == b for a, b in zip(struct.iter_unpack('<I', d[0x10:0x10 + len(tabla)]), struct.iter_unpack('<I', tabla)))
            if iguales >= 0.75 * (len(tabla) // 4):
                r[base] = k
                break
    return r


def volcar(p2s, escena, campo=-1, solo=None, filtro=None, aceptar_fallos=False, antes=(), sin_vaciar=False):
    cpu, info = mips_perfil.cargar('b2', p2s)
    cpu.hooks[0x1434C0] = lambda s: None   # FUN_001434c0: espera de DMA (0x3B4344) que no llega sin hardware; 0x23A9F0 la usa
    cpu.hooks[('syscall', 100)] = lambda s: None   # RESCATE B2-NAVE: syscall 0x64 = FlushCache (sin cache en el interprete)
    ram =zipfile.ZipFile(p2s).read('eeMemory.bin')
    amas = amas_de_ram(cpu, ram)
    cmn = cmn_de_ama(ram, amas)
    poses, quads, sin_id, pend, por_tarea, fn_actual, ctx, reloj = {}, [], 0, [], defaultdict(int), 0, [], [0]

    def de_ama(p):
        for base, n in amas:
            if base + 0x10 <= p < base + 0x10 + 0x40 * n and (p - base - 0x10) % 0x40 == 0:
                return base, (p - base - 0x10) // 0x40
        return None, None

    entrar_orig = cpu.entrar

    def entrar(t):
        if t == POSE:
            reloj[0] += 1
            poses[cpu.r[5] & 0xFFFFFFFF] = (ee.fv(cpu.f[12]), cpu.r[4] & 0xFFFFFFFF, reloj[0])
        elif t == DIBAMA:   # FUN_001a8d10(cuadro f12, #AMA a0, anim a1): dibujante de #AMA (tareas 0x2459c0/0x245890)
            reloj[0] += 1
            base = cpu.r[4] & 0xFFFFFFFF
            ctx[:] = [ee.fv(cpu.f[12]), base + 0x10 + 0x40 * (cpu.r[5] & 0xFFFFFFFF), reloj[0]]
        elif t == EMITIR:   # se apunta la identidad y se deja correr el original (el llamante retoca el registro despues)
            rect = [cpu.rf((cpu.r[4] & 0xFFFFFFFF) + 4 * i) for i in range(4)]
            cuadro, ent, cuando = poses.get(cpu.r[6] & 0xFFFFFFFF, (None, None, -1))
            if ctx and ctx[2] > cuando:
                cuadro, ent = ctx[0], ctx[1]
            base, anim = de_ama(ent) if ent else (None, None)
            img = -1 if base is None else next(
                (i for i in range(amas_n[base]) if [cpu.rf(base + 0x10 + 0x40 * i + 4 * j) for j in (0, 1, 4, 5)] == rect), -1)
            pend.append((base, anim, cuadro, img))
        elif t == REENCOLAR:
            pend.append(None)
        return entrar_orig(t)

    def recoger():
        """Registros nuevos de la lista 0x3D1A10 (gp-0x7890 = cuantos; FUN_001a9030/FUN_001a91c0) en orden de emision."""
        nonlocal sin_id
        n = cpu.r32(cpu.gp - 0x7890)
        for i, idn in zip(range(n), pend):
            rec = cpu.r32(0x3D1A14 + 8 * i)
            if idn is None:
                continue
            base, anim, cuadro, img = idn
            if base is None or base not in cmn:
                sin_id += 1
                por_tarea[f'{fn_actual:#x} sin identidad'] += 1
                continue
            por_tarea[f'{fn_actual:#x}/{cmn[base]}'] += 1
            x, y = cpu.rf(rec + 0x20), cpu.rf(rec + 0x24)
            x0, y0, x1, y1 = (cpu.rf(rec + 0x30 + 4 * j) for j in range(4))
            quads.append(dict(ama=cmn[base], anim=anim, cuadro=round(max(0.0, cuadro), 1), img=img,
                              xy=[[x + x0, y + y0], [x + x1, y + y0], [x + x1, y + y1], [x + x0, y + y1]]))
        pend.clear()
        cpu.w32(cpu.gp - 0x7890, 0)

    amas_n = dict(amas)
    cpu.entrar = entrar
    t, tareas, fallos = cpu.r32(LISTA), [], []
    while t != FIN and len(tareas) < 512:
        tareas.append((t, cpu.r32(t + 0x1C)))
        t = cpu.r32(t + 0x28)
    cpu.w32(cpu.gp - 0x7890, 0)   # FUN_001a9270 al empezar el cuadro
    if antes or sin_vaciar:   # FUN_001a89c0 vacia la lista de sprites al acabar la cola del mapa (0x23A9F0): aqui se deja para recogerla
        cpu.hooks[0x1A89C0] = lambda s: None
    for fn in antes:   # B2-TAB: funciones que encolan sprites del mapa (p. ej. 0x274880 marcador y 0x274210 barras de cada pieza)
        cpu.call(fn, limite=5_000_000)
    for t, fn in tareas:
        if fn in (0, 0xFFFFFFFF) or fn & 0x80000000 or (solo and fn not in solo):
            continue
        fn_actual = fn
        poses.clear()
        ctx.clear()
        try:
            cpu.call(fn, t, limite=5_000_000)
        except Exception as e:  # una tarea que el interprete no sabe ejecutar no tumba las demas: se informa
            fallos.append(f'{fn:#x}@{t:#x}: {e}'[:160])
            pend.clear()
            cpu.w32(cpu.gp - 0x7890, 0)
            continue
        recoger()
    # instancia: img<imagen>#k, k por (y0, x0) dentro de la misma identidad (igual en el port)
    grupos = defaultdict(list)
    for q in quads:
        if filtro and q['ama'] not in filtro:
            continue
        grupos[(q['ama'], q['anim'], q['cuadro'], q['img'])].append(q)
    salida = []
    for g in grupos.values():
        for k, q in enumerate(sorted(g, key=lambda q: (round(q['xy'][0][1], 3), round(q['xy'][0][0], 3)))):
            salida.append(dict(ama=q['ama'], anim=q['anim'], cuadro=q['cuadro'], instancia=f"img{q['img']}#{k}",
                               xy=[[round(a, 3), round(b, 3)] for a, b in q['xy']]))
    doc = dict(formato='b2-quads-v1', escena=escena, region='PAL', campo=campo, plano=[640, 448], viewport=[0, 0, 640, 448],
               completo=not sin_id and (not fallos or aceptar_fallos), fallos_aceptados=bool(fallos and aceptar_fallos),
               origen='original', fuente=str(p2s), elf_sha1=info['sha1'],
               amas={f'{b:#x}': k for b, k in cmn.items()}, filtro_amas=sorted(filtro or []), sin_identidad=sin_id, fallos=fallos, por_tarea=dict(por_tarea),
               quads=salida)
    return doc


if __name__ == '__main__':
    a = argparse.ArgumentParser()
    a.add_argument('p2s')
    a.add_argument('escena')
    a.add_argument('salida')
    a.add_argument('--campo', type=int, default=-1)
    a.add_argument('--solo', default='')
    a.add_argument('--amas', default='')
    a.add_argument('--antes', default='', help='funciones (hex, comas) que se llaman antes de las tareas: encolan sprites del mapa')
    a.add_argument('--sin-vaciar', action='store_true', help='no vaciar la lista de sprites (FUN_001a89c0): recoge tambien los del mapa (0x232D10 / 0x23A9F0)')
    a.add_argument('--aceptar-fallos', action='store_true',
                   help='marca completo aunque fallen tareas (quedan listadas en "fallos"); solo si se sabe que no dibujan esos #AMA')
    x = a.parse_args()
    doc = volcar(x.p2s, x.escena, x.campo, {int(v, 16) for v in x.solo.split(',') if v} or None,
                 {int(v) for v in x.amas.split(',') if v} or None, x.aceptar_fallos,
                 [int(v, 16) for v in x.antes.split(',') if v], x.sin_vaciar)
    Path(x.salida).write_text(json.dumps(doc, indent=1), encoding='utf-8')
    print(f"{len(doc['quads'])} quads ({doc['sin_identidad']} sin identidad, {len(doc['fallos'])} tareas fallidas, "
          f"amas {doc['amas']}) -> {x.salida}")
    for f in doc['fallos'][:10]:
        print('  fallo', f)

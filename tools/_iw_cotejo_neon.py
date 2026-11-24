# SPDX-License-Identifier: MIT
"""Sondas de persecución, Cell y mapa basadas en los bancos de HIW/Neon."""
import math
import struct
import subprocess

import comparar as cmp
import disparo_banco as banco
from _iw_cotejo_focos import cpu_eu, imagen, llamar
from bubbles import f2b
from persecucion import b2f


def persecucion(cuadros, wcf, rayo):
    import bubbles
    import persecucion as p

    sha = imagen(bubbles.ELF)[1]
    esp = p.Espejo(usa=False)
    pic = (-9., 124., -875.)
    raiz = list(map(f2b, pic))
    jugador = list(map(f2b, (0, 9.2, 0)))
    seed = 0x12345678
    px, _, pz = pic
    oo, pp = [], []
    rayos = barridos = 0
    with subprocess.Popen([str(rayo), str(wcf)], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, text=True, bufsize=1) as pr:
        def proveedor(k, x, y, radio, m):
            coords = ' '.join(repr(b2f(v)) for v in x + y)
            pr.stdin.write(f'S {coords} {b2f(radio)!r} {m}\n' if k == 'S' else f'{coords} {m}\n')
            pr.stdin.flush()
            h, x, y, z, t, mt, nx, ny, nz, d = pr.stdout.readline().split()
            return (int(h), [f2b(float(v)) for v in (x, y, z)],
                    [f2b(float(v)) for v in (nx, ny, nz)], f2b(float(d)), int(mt), int(t))

        esp.proveedor = proveedor
        try:
            for i in range(cuadros):
                a = ([esp.cpu.r32(p.PIC + 0x640 + 4 * k) for k in range(3)] if i else
                     [raiz[0], f2b(124.97), raiz[2]])
                cabeza = [a[0], f2b(b2f(a[1]) + 5.5), a[2]]
                e = dict(ini=int(i == 0), estado_ini=13, seed=seed, pic0=raiz, j40=[0, 0, 0],
                         j640=jugador, p640=a, p770=cabeza, dist=f2b(math.dist(list(map(b2f, a)), list(map(b2f, jugador)))),
                         jcbc=1, dif=2, t25a=1, w180=0, rayos=[])
                o = esp.tick(e)
                seed = o['seed']
                rayos += sum(r[0] == 'Y' for r in o['col'])
                barridos += sum(r[0] == 'S' for r in o['col'])
                oo.append(dict(i=i, estado=o['estado'], x=b2f(o['p'][0]), y=b2f(o['p'][1]), z=b2f(o['p'][2])))
                # iw_minijuegos.cpp:2831–2843, jugador fijo en el origen, paso PAL y multiplicadores neutros.
                d = max(math.hypot(px, pz), .001)
                px -= px * 4.5 * 1.2 / d
                pz -= pz * 4.5 * 1.2 / d
                pp.append(dict(i=i, estado=4, x=px, y=124., z=pz))
        finally:
            pr.stdin.close()
            try:
                rc = pr.wait(timeout=3)
            except subprocess.TimeoutExpired:
                pr.kill()
                pr.wait(timeout=3)
                raise
        if rc != 0:
            raise RuntimeError(f'colisionador WCF: código {rc}')
    return dict(alcance='Piccolo EE+WCF con jugador inmóvil vs modelo de rama C++; estado inicial EE inyectado; no partida.',
                elf_sha256=sha, wcf_sha256=banco.sha256(wcf), colisionador_sha256=banco.sha256(rayo),
                rayos=rayos, barridos=barridos, comparacion=cmp.primer_dif(oo, pp, tol=.05),
                separacion_final=math.dist([oo[-1][k] for k in 'xyz'], [pp[-1][k] for k in 'xyz']),
                altura_elf=[min(v['y'] for v in oo), max(v['y'] for v in oo)],
                predicado_vertical=dict(altura=124, distancia_anclas_supuesta=114.8,
                                         contacto_anclas8=False, contacto_horizontal14=True), elf=oo, modelo_cpp=pp)


def port_reloj_baja(pausa):
    # El contador se evalúa DESPUÉS de la transición de fase (iw_mini_disparo.cpp:1752–55,1793–1802).
    fase, reloj, filas = 4, 2400, []
    for t in range(221):  # entrada + 220 actualizaciones, la última ya en Aim para el contador
        if t == 220:
            fase = 0
        reloj -= not pausa or fase != 4
        filas.append(reloj)
    return filas


def cell(elf):
    import disparo_fire as fire
    import disparo_fases as carga
    import disparo_low as low
    import disparo_mira as mira

    _, sha = imagen(elf)
    e = mira.MiraCell('eu', elf)
    o, p = [], []
    digitales = [(-110, 200, 0x80), (73, 200, 0x20), (0, 60.5, 0x10),
                 (0, 279.5, 0x40), (-110, 200, 0xA0), (0, 200, 0x20)]
    for i, (x, z, mask) in enumerate(digitales):
        e.preparar(x, z)
        v = e.pasar(mask, 0)['vectores_bits']
        a, b = mira.port_modelo(x, z, mask)
        o.append(dict(i=i, x=b2f(v[0]), z=b2f(v[2])))
        p.append(dict(i=i, x=b2f(a), z=b2f(b)))
    digital = dict(entradas=digitales, comparacion=cmp.primer_dif(o, p, tol=1e-4), elf=o, modelo_cpp=p)

    d = mira.PERFILES['eu']
    c, _ = cpu_eu(elf, [d['init']])
    c.call(d['init'], limite=100000)
    if c.io or c.sin_puente:
        raise RuntimeError('init de mira incompleto')
    c.w32(0x49C380, 32)
    c.whitelist = None  # atan2 y helpers reales; se comprueba E/S por cada llamada
    analogos = [(0, 200, 64, 0), (0, 200, 64, 64), (0, 200, 127, 127),
                (0, 200, 100, -40), (73.5, 200, 100, 0), (0, 279.5, 0, 100)]
    o, p = [], []
    f = banco.f32
    for i, (x, z, sx, sy) in enumerate(analogos):
        c.m[mira.CAM:mira.CAM + 0x200] = bytes(0x200)
        c.w32(d['camp'], mira.CAM)
        for k, v in enumerate((x, 400, z, 1, x, 0, z - 200, 1)):
            c.w32(mira.CAM + 0x80 + k * 4, f2b(v))
        c.w16(d['mando'], 0)
        c.w16(d['mando2'], 0)
        c.w8(d['analog'], sx & 255)
        c.w8(d['analog'] + 1, sy & 255)
        llamar(c, d['mover'])
        o.append(dict(i=i, x=c.rf(mira.CAM + 0x80), z=c.rf(mira.CAM + 0x88)))
        p.append(dict(i=i, x=max(-111, min(74, f(x + f(f(sx / 127) * 1.5)))),
                      z=max(60, min(280, f(z + f(f(sy / 127) * 1.5))))))
    analogico = dict(entradas=analogos, comparacion=cmp.primer_dif(o, p, tol=1e-4), elf=o, modelo_cpp=p)

    ch = carga.CargaCell('eu', elf)
    ch.preparar(f2b(.5))
    energia, o, p = .5, [], []
    for i in range(15):
        v = ch.pasar(0x8000, f2b(1))
        energia = f(energia + f(.1 / 120))
        o.append(dict(i=i, energia=b2f(v['energia_bits']), vuelve=bool(v['volver_mira'])))
        p.append(dict(i=i, energia=energia, vuelve=energia > 1))
    carga_cmp = cmp.primer_dif(o, p, tol=1e-5)

    fr = fire.FireEntradaCell('eu', elf)
    fr.preparar_entrada()
    fr.entrar()
    for n in range(2, 101):
        if fr.pasar(f2b(1))['fase'] == 2:
            break
    else:
        raise RuntimeError('Fire nunca transita a fase 2')

    lo = low.LowCell('eu', elf)
    lo.preparar_aim(f2b(.05), False)
    entra = lo.pasar_aim()
    if entra['estado'] != 'low':
        raise RuntimeError('Aim no entró en LowEnergyWait')
    for t in range(1, 300):
        fin = lo.pasar_low(f2b(1))
        if fin['estado'] == 'aim':
            break
    else:
        raise RuntimeError('LowEnergyWait no devolvió el control')
    reloj_viejo, reloj_parche = port_reloj_baja(False), port_reloj_baja(True)
    diferencias = any(not v['ok'] for v in (digital['comparacion'], analogico['comparacion'], carga_cmp)) or (n != 31 or t != 220)
    return dict(alcance='Mira, carga, entrada Fire y transición Low del EE vs modelo C++; no geometría ni HUD real.',
                elf_sha256=sha, coincide=not diferencias, mira_digital=digital, mira_analogica=analogico,
                carga=carga_cmp, fire=dict(transicion_a_fase2_llamada=n, primera_llamada_fire2_inferida=n + 1,
                                           disparo_modelo_cpp=32),
                low=dict(llamadas_ee=t, llamadas_modelo_cpp=220,
                         timer_ui_entrada=[e['args'] for e in entra['eventos'] if e['tipo'] == 'timer_ui'],
                         timer_ui_salida=[e['args'] for e in fin['eventos'] if e['tipo'] == 'timer_ui'],
                         tics_perdidos_en_low_sin_parche=reloj_parche[-1] - reloj_viejo[-1],
                         reloj_en_salida_sin_parche=reloj_viejo[-1], reloj_en_salida_con_parche=reloj_parche[-1]))


def mapa(road, afs):
    import mapa_espejo as m

    sha = imagen(m.B.ELF)[1]
    dmd = m.dmd_de(m.entrada_afs(1092 if road else 1093, afs))
    scd = m.entrada_afs(1127, afs)
    if not dmd or not scd.startswith(b'#SCD'):
        raise ValueError('faltan #DMD/#SCD esperados')
    if road:
        sp = m.Espejo(road=True, dmd=dmd, scd=scd)
        sp.aparecer()
        x0, _, _ = sp.cuadro(lx=100)
        x1, _, _ = sp.cuadro(lx=100)
        n = len(sp.nodos())
        return dict(alcance='Diagnóstico EE: nodos y movimiento, no cotejo con el port.',
                    elf_sha256=sha, nodos=n, dx=x1-x0, cumple=n > 0 and abs(x1-x0-2.4) < .05)
    casos = []
    for mini in (False, True):
        hechas = list(range(0x7E2, 0x7EC)) + ([0x7D0] if mini else [])
        sp = m.Espejo(hechas=hechas, goku=(890., 610.), dmd=dmd, scd=scd)
        sp.aparecer()
        cap = sp.c.r8(m.ESTADO + sp.clave_idx(0x7EC))
        nodo = next((a for a in range(sp.ctx, sp.ctx + 0x80 * 0x30, 0x30)
                     if sp.c.r16(a) == 6 and sp.c.r32(a + 4)), None)
        if nodo is None:
            raise ValueError('no aparece la fila 6 en el perfil de mapa')
        cola = any(sp.c.r32(sp.M + 0x3A0 + 4*k) == nodo for k in range(min(sp.c.r32(sp.M + 0x4A0), 64)))
        if cola:
            sp.c.w32(nodo + 4, sp.c.r32(nodo + 4) & ~4)  # intervención del banco, no mide los 90 cuadros
        sp.cuadro(lx=0)
        sp.cuadro(lx=0)
        globo = sp.f(sp.M + 0x334) >= 0 and sp.c.r16(sp.M + 0x338) == 6
        sp.cuadro(mant=0x4000, pulsa=0x4000, lx=0)
        cmd = [sp.c.r32(0x5D9220 + 4*i) for i in range(min(sp.c.r32(0x5D92A0), 8))]
        casos.append(dict(con_mini2000=mini, cap_7ec=cap, en_cola=cola, globo_fila6=globo,
                          ficha_cmd9=9 in cmd, indice_mision=sp.c.r32(sp.M + 0x10)))
    return dict(alcance='Diagnóstico A/B EE de misión 11; se limpia manualmente el bit 4 al entrar en cola.',
                elf_sha256=sha, cumple=all(not casos[0][k] and casos[1][k] for k in ('en_cola', 'globo_fila6', 'ficha_cmd9'))
                and casos[1]['indice_mision'] == 10, casos=casos)

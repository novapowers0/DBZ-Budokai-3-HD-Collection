# SPDX-License-Identifier: MIT
"""Bancos focales HIWB para finales de botones y de vuelo (ELF PAL vs modelo C++)."""
from functools import lru_cache
from hashlib import sha256
from struct import unpack_from

import comparar as cmp
import disparo_banco as banco
import ee
from bubbles import GP

CTX = banco.CONTEXTO
ACTOR = CTX + 0x4000
PARAM = CTX + 0x8000
TASK = CTX + 0x9000

QTE = {  # Clasificador de cada ELF, punteros de contexto y actor.
    '0x7D4': (0x378090, 0x49CEBC, 0x49CECC),  # NB006
    '0x7DB': (0x351FB0, 0x49CE7C, 0x49CE8C),  # NB013
    '0x7DC': (0x32FA20, 0x49CDF8, 0x49CE08),  # NB014
}
PARES_QTE = ((1, 55), (56, 68), (69, 80), (81, 94), (95, 100))
TOPES_NB007 = (55, 45, 40, 35, 25)  # iw_mini_botones.cpp:kUmbralT
PARES_NB012 = ((301, 600), (601, 750), (751, 800), (801, 900), (901, 1000))


@lru_cache(maxsize=1)
def imagen(elf):
    mem, sha = banco.cargar_elf(elf)
    if sha != banco.ELF_HASH['eu']:
        raise ValueError(f'ELF PAL no coincide con el perfil de direcciones: {sha}')
    return mem, sha


def cpu_eu(elf, entradas):
    mem, sha = imagen(elf)
    c = ee.Ee(bytearray(mem), GP)
    c.estricto = True
    c.whitelist = set(entradas)
    return c, sha


def llamar(c, fn, *args, **kw):
    c.io.clear()
    c.sin_puente.clear()
    r = c.call(fn, *args, limite=12000, **kw)
    if c.io or c.sin_puente or c.desconocidas:
        raise RuntimeError(f'EE {fn:#x}: E/S o llamadas no cubiertas ({c.io}, {c.sin_puente})')
    return r


def wf(c, addr, v):
    c.w32(addr, ee.fb(v))


def tabla(c, base, pares, fallo, flotante):
    escribir = (lambda a, v: wf(c, a, v)) if flotante else c.w32
    for i, (bajo, alto) in enumerate(reversed(pares)):
        escribir(base + i * 8, bajo)
        escribir(base + i * 8 + 4, alto)
    escribir(base + 40, fallo)


def cierre_qte(curso, elf):
    fn, ctx, actor = QTE[curso]
    c, sha = cpu_eu(elf, [fn])
    c.w32(ctx, CTX)
    c.w32(actor, ACTOR)
    tabla(c, CTX + 0x310, PARES_QTE, 0, False)
    filas = []
    for nota in (0, 1, 54.75, 55, 56, 68, 69, 80, 81, 94, 95, 100):
        for vida in (0, 1):
            wf(c, CTX + 0x2DC, nota)
            wf(c, ACTOR + 0x1850, vida)
            llamar(c, fn)
            port = max((i for i, (bajo, _) in enumerate(PARES_QTE, 1) if vida > 0 and int(nota) >= bajo), default=0)
            filas.append(({'nota_precalculada': nota, 'vida': vida}, {'rango': c.r32(CTX + 0x1C)}, {'rango': port}))
    return sha, [fn], 'Rango final a nota y vida inyectadas (no generación de nota, combate ni QTE).', filas


def cierre_nb007(elf):
    fn = 0x293820  # MiniGame_CalcRank
    c, sha = cpu_eu(elf, [fn])
    c.w32(0x49CBA8, CTX)
    # s_MSN_NB007_Update (EU 0x295C10), palabras exactas en +0xCC..+0xF8.
    words = (0x3C23D70A, 0x41C80000, 0x41C8147B, 0x420C0000, 0x420C0A3D, 0x42200000,
             0x42200A3D, 0x42340000, 0x42340A3D, 0x425C0000, 0x425C0A3D, 0x40E00000)
    for i, w in enumerate(words):
        c.w32(CTX + 0xCC + i * 4, w)
    casos = [(0,) * 10, (.005,) + (0,) * 9, (.01,) + (0,) * 9, (7,) * 10]
    for t in TOPES_NB007:
        # Acumular el decimal AL FINAL para no absorberlo antes de las sumas enteras.
        q, r = divmod(t, 6)
        casos += [(6,) * q + (0,) * (9 - q) + (r + d,) for d in (0, .005, .02)]
    filas = []
    for tiempos in casos:
        total = 0.0
        for i, v in enumerate(tiempos):
            wf(c, CTX + 0x1C + i * 4, v)
            total = banco.f32(total + banco.f32(v))
        llamar(c, fn)
        port = max((i for i, alto in enumerate(TOPES_NB007, 1) if total <= alto and min(tiempos) < 7), default=0)
        filas.append(({'tiempos_ronda': tiempos, 'total_modelo_s': total}, {'rango': c.r8(CTX + 0x88)}, {'rango': port}))
    return sha, [fn], 'Rango por tiempos inyectados de diez rondas; umbrales nativos de 0x295C10.', filas


def cierre_nb012(elf):
    fn = 0x313500  # Mg_ComputeTotalAndRank
    c, sha = cpu_eu(elf, [fn])
    c.w32(0x49CDC8, CTX)
    tabla(c, CTX + 0xCC, PARES_NB012, 300, True)  # Mg_InitParamsByLevel (EU 0x312070)
    casos = (0, 299, 300, 301, 600, 601, 750, 751, 800, 801, 900, 901, 999, 1000)
    filas = []
    for total in casos:
        q, r = divmod(total, 10)
        puntos = (q + 1,) * r + (q,) * (10 - r)
        for i, v in enumerate(puntos):
            wf(c, CTX + 0x1C + i * 4, v)
        llamar(c, fn)
        # En esta sonda se iguala el total ya agregado: el port suma tres categorías,
        # mientras el clasificador del ELF suma rondas. No se coteja esa etapa de agregación.
        rango = max((i for i, (bajo, _) in enumerate(PARES_NB012, 1) if total >= bajo), default=0)
        filas.append(({'puntos_ronda': puntos, 'total_categorias_modelo': total}, {'total': int(c.rf(CTX + 0x44)), 'rango': c.r8(CTX + 0x88)},
                      {'total': total, 'rango': rango}))
    return sha, [fn], 'Rango NB012 con el mismo total agregado y umbrales nativos (no timing ni agregación de categorías).', filas


def cierre_vuelo(curso, elf, afs):
    from mapa_espejo import entrada_afs, hijo_amb

    hijo = 23 if curso == '0x7D6' else 24
    msf = hijo_amb(entrada_afs(891, afs), hijo)
    if not msf.startswith(b'#MSF') or len(msf) < 0x200:
        raise ValueError(f'891/{hijo} no contiene #MSF válido')
    bloques = unpack_from('<6I', msf, 0xA8)
    elegir, ganar, perder = 0x33C340, 0x33C190, 0x33C420
    # Servicios externos: fin de animación/escena simulado; config/premio se aíslan.
    servicios = {0x342170: 1, 0x342C20: 1, 0x3424F0: 0, 0x346F40: 0,
                 0x28AA40: ACTOR, 0x28A9E0: 0, 0x28A020: 0, 0x28A300: 0}
    c, sha = cpu_eu(elf, [elegir, ganar, *servicios])
    for addr, retval in servicios.items():
        c.hooks[addr] = lambda s, v=retval: s.r.__setitem__(2, v)
    c.w32(0x49CE3C, CTX)
    c.w32(CTX + 0x50, TASK)
    c.w32(CTX + 0x500, PARAM)
    filas = []
    for nivel, b in enumerate(bloques):
        if not b or b + 0x64 > len(msf):
            raise ValueError(f'#MSF: bloque {nivel} fuera de rango')
        cortes = unpack_from('<5i', msf, b + 0x38)
        if not 0 < min(cortes) <= max(cortes) < 10_000 or sorted(cortes) != list(cortes):
            raise ValueError(f'#MSF: umbrales no ordenados o fuera de rango: {cortes}')
        for i, t in enumerate(cortes):
            c.w32(PARAM + 0x38 + i * 4, t)
        for ms in sorted({0, *(1000 * t + d for t in cortes for d in (-1, 0, 1))}):
            wf(c, CTX + 0x518, ms)
            c.w32(TASK + 0x1C, 0)
            llamar(c, elegir)
            salida = c.r32(TASK + 0x1C)
            if salida not in (ganar, perder):
                raise RuntimeError(f'estado de vuelo no cubierto {salida:#x}')
            ok = salida == ganar
            if ok:
                llamar(c, ganar, TASK)
            port = ms <= cortes[-1] * 1000  # iw_mini_vuelo.cpp:CerrarJuego y clasificación del resultado
            filas.append(({'dificultad': nivel, 'ms': ms, 'cortes_s': cortes},
                          {'meta_a_tiempo': ok, 'rango': c.r32(ACTOR + 0x48) if ok else -1},
                          {'meta_a_tiempo': port, 'rango': 4 - sum(ms // 1000 >= t for t in cortes[:4]) if port else -1}))
    return sha, [elegir, ganar], filas, dict(afs=str(afs), entrada=891, hijo=hijo, sha256_msf=sha256(msf).hexdigest())


def cotejar(curso, elf, afs):
    datos = {}
    if curso in QTE:
        sha, funciones, alcance, casos = cierre_qte(curso, elf)
    elif curso == '0x7D5':
        sha, funciones, alcance, casos = cierre_nb007(elf)
    elif curso == '0x7DA':
        sha, funciones, alcance, casos = cierre_nb012(elf)
    else:
        sha, funciones, casos, datos = cierre_vuelo(curso, elf, afs)
        alcance = 'Final de vuelo con meta ya tocada (seis bloques #MSF nativos); sin física, aros ni reloj real.'
    o = [{'i': i, **v} for i, (_, v, _) in enumerate(casos)]
    p = [{'i': i, **v} for i, (_, _, v) in enumerate(casos)]
    return dict(alcance=alcance, region='eu', elf_sha256=sha, funciones=[hex(x) for x in funciones],
                comparacion=cmp.primer_dif(o, p), casos=[dict(entrada=e, elf=a, modelo_cpp=b) for e, a, b in casos], **datos)

# SPDX-License-Identifier: MIT
"""Productores de pares: entradas idénticas, salidas EE y C++ independientes."""
from functools import lru_cache
from itertools import product

import _iw_cotejo_focos as hiwb
import audita7dd
import comparar as cmp
import disparo_banco as banco
import ee


def comparar(entradas, a, b, claves):
    if not (len(entradas) == len(a) == len(b) > 0) or not claves:
        raise ValueError('lado vacío o desalineado')
    if any(len(x) != len(claves) or any(v is None for v in x) for lado in (a, b) for x in lado):
        raise ValueError('salida ausente o incompleta')
    def filas(lado):
        return [dict(i=i, **{f'in_{k}': v for k, v in e.items()}, **dict(zip(claves, r)))
                for i, (e, r) in enumerate(zip(entradas, lado))]
    return dict(comparacion=cmp.primer_dif(filas(a), filas(b)),
                filas=[dict(entrada=e, original=dict(zip(claves, x)), cpp=dict(zip(claves, y)))
                       for e, x, y in zip(entradas, a, b)])


@lru_cache(maxsize=1)
def qte_original(elf, mascaras):
    fn, check, get = 0x2EE790, 0x2EEBF0, 0x2EEE80
    c, _ = hiwb.cpu_eu(elf, (fn, check, get))
    q = hiwb.CTX
    c.w32(0x49CD70, q)
    c.w32(q + 0x110, 1)
    res = []
    for m in mascaras:
        c.w32(q + 4, 0xFFFFFFFF)  # flanco sin pulsación previa; sin bloqueos en +0x100
        c.w16(0x4ADECE, m)
        hiwb.llamar(c, fn)
        boton = ee.s64(ee.sx32(hiwb.llamar(c, get, 0, 1, 0)))
        c.w8(q + 0x28, 14)
        juicio = ee.s64(ee.sx32(hiwb.llamar(c, check, 0, 1, 0, 0, 0)))
        res.append((boton, juicio))
    return res


def qte(curso, elf, sonda, _):
    mascaras = (0, *(1 << i for i in range(4, 16)),
                *((1 << i) | (1 << j) for i in range(4, 16) for j in range(i + 1, 16)), 0xFFF0)
    # Sólo botones de juego; se excluyen Start/Select y no se modela pausa, ni carril doble/hold.
    a = qte_original(elf, mascaras)
    b = [(v, -1 if v < 0 else int(v == 14)) for v, in sonda.medir('q', [(m, 0xFFF0) for m in mascaras], (int,))]
    return dict(**comparar([dict(flanco_ps2=m, esperado=14) for m in mascaras], a, b, ('boton', 'juicio')),
                autoridad='ELF PAL vs BotonPulsado C++; juicio C++ proyectado (objetivo 14)',
                funciones=['0x2ee790', '0x2eebf0', '0x2eee80'])


@lru_cache(maxsize=1)
def pad_original(elf, casos):
    fn = 0x3394F0
    c, _ = hiwb.cpu_eu(elf, (fn,))
    c.w8(0x5C14B4, 0)  # ranura mando 0
    c.w8(0x5C1534, 0)  # convención Y normal, como el adaptador C++
    a = []
    for izq, der, y in casos:
        for off, v in enumerate((izq, y, der)):
            c.w8(0x4ADED8 + off, v & 0xFF)
        hiwb.llamar(c, fn, hiwb.CTX, hiwb.CTX + 4)
        a.append((c.rf(hiwb.CTX), c.rf(hiwb.CTX + 4)))
    return a


def vuelo(_, elf, sonda, __):
    # Casos únicos: zona muerta, empates de magnitud, signo, extremos y alternancia de mando.
    casos = tuple(product((-127, -65, -64, -63, 0, 63, 64, 65, 127), repeat=3))
    a = pad_original(elf, casos)
    b = sonda.medir('v', casos, (float, float))
    return dict(**comparar([dict(izq_x=i, der_x=d, der_y=y) for i, d, y in casos], a, b, ('x', 'y')),
                autoridad='ELF PAL vs PadGetStickAxes C++; sólo decodificación de cámara, Y normal', funciones=['0x3394f0'])


@lru_cache(maxsize=1)
def rango_original(elf):
    sha, fn, _, filas = hiwb.cierre_nb007(elf)
    return sha, fn, [(f['tiempos_ronda'], f['total_modelo_s']) for f, _, _ in filas], [(a['rango'],) for _, a, _ in filas]


def rango(_, elf, sonda, ajuste):
    _, fn, casos, a = rango_original(elf)
    entradas = [dict(tiempos_ronda=t, total_float32=x) for t, x in casos]
    b = sonda.medir('r', [(ajuste, *t) for t, _ in casos], (int,))
    off = sonda.medir('r', [(0, *t) for t, _ in casos], (int,))
    return dict(**comparar(entradas, a, b, ('rango',)), off=[v for v, in off],
                autoridad='ELF PAL de HIWB vs cierre de PasoTeclear compilado', funciones=[hex(v) for v in fn])


@lru_cache(maxsize=1)
def cadena_7dd(elf):
    # El auditor existente fija el perfil en su propia entrada. No se da por medido otro ELF.
    if banco.sha256(audita7dd.B.ELF) != banco.sha256(elf):
        raise ValueError('la auditoría 7DD requiere el mismo ELF PAL que los demás focos')
    r = audita7dd.objetivo_continuo(False)
    if not r['casos'] or r['iguales'] != r['casos']:
        raise RuntimeError('el auditor 7DD no reprodujo el contrato de objetivo')
    return r


def captura(_, elf, sonda, ajuste):
    r = cadena_7dd(elf)
    # Esto es semántica de cierre dada una captura, no paridad entre los detectores de contacto.
    f = next(x for x in r['casos_detalle'] if x['causa'] == 'contacto' and x['jugador'] == 1 and
             not x['aire'] and not x['pausa'] and x['cuadros'][0]['estado'] == 9)
    if not f['cuadros'][0]['procesada']:
        raise RuntimeError('el caso nativo no produjo entrada9')
    m = sonda.fuentes.joinpath('iw_minijuegos.cpp').read_text(encoding='utf-8')
    from _sonda_cpp import trozo
    nativa = trozo(m, '        if (aviso_antes && !salida.tarea && salida.procesada && salida.capturado)', '\n        }')
    if 'Acabar(base, mgr, d, false);' not in nativa:
        raise RuntimeError('revisar la correspondencia con el cierre de captura nativo')
    entrada = (2, 0, 0, 1, 0)
    b = sonda.medir('p', [(ajuste, *entrada)], (int, int))
    off, = sonda.medir('p', [(0, *entrada)], (int, int))
    controles = sonda.medir('p', [(ajuste, *c) for c in [(1, 0, 0, 1, 0), (2, 0, 0, 14, 0),
                                   (2, 1, 0, 1, 0), (1, 0, 1, 1, 0), (2, 0, 0, 0, 124)]], (int, int))
    return dict(**comparar([dict(contacto_decidido=True)], [(2, 0)], b, ('g_final', 'superado')),
                autoridad='EE entrada9 + cierre de la ruta nativa ya existente vs salida del respaldo C++',
                 funciones=r['funciones'], dependencias_sustituidas=r['dependencias_sustituidas'],
                 nativo=dict(casos=r['casos'], iguales=r['iguales'], campos=r['campos'], muestra=f),
                 off=off,
                controles_cpp=dict(zip(('bubbles', 'limite', 'captura_nativa', 'bubbles_nativo', 'sobrevuelo'), controles)))


FOCOS = {
    'pulsacion': (('0x7D4', '0x7D5', '0x7DA', '0x7DB', '0x7DC'), qte),
    'camara': (('0x7D6', '0x7E0'), vuelo),
    'rango': (('0x7D5',), rango),
    'captura': (('0x7DD',), captura),
}

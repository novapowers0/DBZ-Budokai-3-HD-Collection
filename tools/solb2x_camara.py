"""Contrasta una cámara medida con el AMC original, sin abrir el juego.

El índice y la fase son explícitos: no busca un ajuste que esconda diferencias.
La interpolación aquí evalúa claves del recurso; no inventa cuadros capturados.
"""
import argparse
from bisect import bisect_right
import hashlib
import json
import math
from pathlib import Path
import struct

from solb2x_censo import E, u
from solb2x_curvas import cargar, distancia


def leer_clip(b, indice):
    if b[:4] != b'#AMC' or not 0 <= indice < u(b, 16):
        raise ValueError('índice fuera del AMC original')
    tipo, _, duracion, ptr = struct.unpack_from('<4I', b, u(b, 20)+16*indice)
    if tipo not in (0x0d, 0x1d) or not duracion:
        raise ValueError('tipo/duración de cámara no acreditados')
    pistas = []
    for j in range(4 if tipo & 0x10 else 3):
        p = u(b, ptr+4*j)
        paso, comp = (16, 3) if j < 2 else (8, 1)
        claves = [(u(b, p+12+paso*k), struct.unpack_from('<'+'f'*comp, b, p+16+paso*k))
                  for k in range(u(b, p+8))]
        if (not claves or any(not all(math.isfinite(x) for x in v) for _, v in claves)
                or any(a[0] >= c[0] for a, c in zip(claves, claves[1:]))
                or claves[-1][0] >= duracion):
            raise ValueError('claves originales inválidas')
        pistas.append(claves)
    return duracion, pistas


def muestrear(clip, tiempo):
    duracion, pistas = clip
    t = min(max(tiempo, 0), duracion-1)
    salida = {}
    for nombre, claves in zip(('ojo', 'objetivo', 'giro', 'fov'), pistas):
        i = bisect_right([k for k, _ in claves], t)
        if i == 0 or i == len(claves):
            v = claves[0 if i == 0 else -1][1]
        else:
            ka, va = claves[i-1]
            kb, vb = claves[i]
            fraccion = (t-ka)/(kb-ka)
            v = tuple(x+(y-x)*fraccion for x, y in zip(va, vb))
        salida[nombre] = list(v) if nombre in ('ojo', 'objetivo') else v[0]
    if 'fov' in salida:
        salida['fov'] = min(max(salida['fov'], 0.08726647), 3.0543263)
    return salida


def errores(a, b):
    return {k: distancia(a[k], b[k]) if isinstance(b[k], list) else abs(a[k]-b[k])
            for k in b}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--original', type=Path, required=True)
    ap.add_argument('--hd', type=Path, required=True)
    ap.add_argument('--indice', type=int, required=True)
    ap.add_argument('--fase-original', type=float, default=0,
                    help='tiempo de recurso = reloj observado + fase; sin ajuste automático')
    ap.add_argument('--iso', default='D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    if a.salida.exists() or not math.isfinite(a.fase_original):
        ap.error('conservar la salida anterior; la fase debe ser finita')
    original, sha_original = cargar(a.original/'traza.json')
    preparacion, sha_prep = cargar(a.original/'preparacion.json')
    fuente = E.IsoB2(a.iso)
    try:
        mdd = fuente.piezas(preparacion['grupo'])[0]
        amc = E.camaras_transicion(mdd)[0]
        elf = fuente.iso.read(fuente.iso.boot_elf())
    finally:
        fuente.f.close()
    elf_sha = hashlib.sha256(elf).hexdigest()
    if elf_sha != '30c362969ad64184df0b4a4ee498badb101265470883948f3131b6302ae3c797':
        raise ValueError('perfil original no acreditado para este lector')
    clip = leer_clip(amc, a.indice)
    datos_hd = (a.hd/'traza_hd.jsonl').read_bytes()
    hd = [json.loads(l) for l in datos_hd.splitlines() if l.strip()]
    medida, sha_medida = cargar(a.hd/'medida.json')
    grupo, entrada = preparacion['grupo'], preparacion['pared']['entrada']
    if (not medida['inicio_real'] or medida['grupo'] != grupo or medida['pared']['entrada'] != entrada
            or any(r['grupo'] != grupo or r['entrada'] != entrada for r in hd)):
        raise ValueError('las capturas no acreditan la misma escena')
    filas_original = []
    for r in original:
        if not r['evento'] or r['reloj'] == 0:
            continue  # la cámara de combate anterior aún está aplicada al arrancar
        c = r['escena_original']['camaras']['aplicada']
        esperado = muestrear(clip, r['reloj']+a.fase_original)
        filas_original.append(dict(campo=r['campo'], reloj=r['reloj'], observado=c,
                                   recurso=esperado, error=errores(c, esperado)))
    filas_hd = []
    for r in hd:
        if r['tipo'] != 'antes_dibujo' or r['indice_camara'] != a.indice:
            continue
        # Son dos relojes registrados, no dos fases elegidas buscando el mínimo.
        esperado = muestrear(clip, r['tiempo_camara'])
        filas_hd.append(dict(guest=r['guest'], reloj=r['cuadro'], tiempo_camara=r['tiempo_camara'],
                            error_recurso=errores(r['aplicada'], esperado),
                            error_reloj=errores(r['aplicada'], muestrear(clip, r['cuadro']))))
    def maximos(filas, key):
        if not filas:
            raise ValueError('sin muestras comparables de cámara')
        return {k: max(r[key][k] for r in filas) for k in filas[0][key]}
    resumen = dict(original=maximos(filas_original, 'error'),
                   hd_tiempo_camara=maximos(filas_hd, 'error_recurso'),
                   hd_reloj=maximos(filas_hd, 'error_reloj'))
    resultado = dict(indice=a.indice, fase_original=a.fase_original, grupo=grupo, entrada=entrada,
        elf_sha256=elf_sha, amc_sha256=hashlib.sha256(amc).hexdigest(),
        original_sha256=sha_original, preparacion_sha256=sha_prep, medida_sha256=sha_medida,
        hd_sha256=hashlib.sha256(datos_hd).hexdigest(), maximos=resumen,
        original=filas_original, hd=filas_hd, aceptacion_escena=False,
        cobertura='evaluación de un índice explícito del AMC; no acredita actores, render ni sonido')
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(resultado, f, indent=2, allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida), muestras_original=len(filas_original),
                          muestras_hd=len(filas_hd), maximos=resumen), indent=2))


if __name__ == '__main__':
    main()

"""Contrasta muestras originales y HD por su reloj observado, sin interpolar.

Conserva cada emparejamiento y su desfase. Ni una cámara igual ni un final de
VM acreditan la escena completa; tampoco se inventan muestras entre campos PAL.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def cargar(path):
    datos = path.read_bytes()
    return json.loads(datos), hashlib.sha256(datos).hexdigest()


def resumen_original(filas):
    activas = [r for r in filas if r['evento']]
    if not activas:
        raise ValueError('la toma no contiene una escena activa')
    cambios = []
    anterior = None
    for r in activas:
        escena = r['escena_original']
        c = escena['camaras']['aplicada']
        estado = (r['zona'], c['direccion'] if c else None,
                  tuple((a['banco'], a['clip'], a['modo']) if a else None
                        for a in escena['controles']))
        if estado != anterior:
            cambios.append(dict(campo=r['campo'], reloj=r['reloj'], zona=r['zona'],
                                camara=estado[1], controles=estado[2]))
            anterior = estado
    fin = next((r for r in filas if r['campo'] > activas[0]['campo'] and not r['evento']), None)
    pasos = [b['campo']-a['campo'] for a, b in zip(activas, activas[1:])]
    return dict(muestras=len(filas), activas=len(activas),
                inicio_campo=activas[0]['campo'], primer_reloj=activas[0]['reloj'],
                ultimo_reloj=activas[-1]['reloj'],
                huecos_campos=sum(max(0, p-1) for p in pasos),
                fin=None if fin is None else dict(campo=fin['campo'], reloj=fin['reloj'], zona=fin['zona']),
                cambios=cambios)


def distancia(a, b):
    if len(a) != len(b) or not all(math.isfinite(x) for x in (*a, *b)):
        raise ValueError('vector no comparable')
    return math.sqrt(sum((x-y)**2 for x, y in zip(a, b)))


def comparar(original, hd, desfase, papeles_pal=(0, 1)):
    if tuple(sorted(papeles_pal)) != (0, 1):
        raise ValueError('los papeles PAL deben ser una permutación de0,1')
    activas = [r for r in original if r['evento']]
    dibujadas = [r for r in hd if r['tipo'] == 'antes_dibujo']
    if not dibujadas:
        raise ValueError('sin muestras HD de cámara antes del dibujo')
    if any(not math.isfinite(r['cuadro']) for r in dibujadas):
        raise ValueError('reloj HD no finito')
    if len({r['guest'] for r in dibujadas}) != len(dibujadas):
        raise ValueError('varias muestras antes del dibujo para un mismo guest')
    pares, sin_par = [], []
    for r in activas:
        h = min(dibujadas, key=lambda v: abs(v['cuadro']-r['reloj']))
        dt = h['cuadro']-r['reloj']
        if abs(dt) > desfase:
            sin_par.append(r['campo'])
            continue
        a, b = r['escena_original']['camaras']['aplicada'], h.get('aplicada')
        if a is None or b is None:
            raise ValueError('falta cámara aplicada en un par temporal')
        errores = {k: distancia(a[k], b[k]) for k in ('ojo', 'objetivo', 'arriba')}
        errores.update(giro=abs(a['giro']-b['giro']), fov=abs(a['fov']-b['fov']))
        pares.append(dict(campo=r['campo'], reloj_original=r['reloj'], guest=h['guest'],
                          reloj_hd=h['cuadro'], desfase=dt, tiempo_camara_hd=h['tiempo_camara'],
                          indice_camara_hd=h['indice_camara'],
                          zonas=[r['zona'], h['zona']], error_camara=errores,
                          error_pos=[distancia(r['actores'][j]['pos'], h['actores'][i]['pos'])
                                     for i, j in enumerate(papeles_pal)],
                          codigos=[[r['actores'][j]['codigo'], h['actores'][i]['codigo']]
                                   for i, j in enumerate(papeles_pal)],
                          original=a, hd=b))
    if not pares:
        raise ValueError('no hay relojes comparables con el desfase declarado')
    return dict(metodo='vecino temporal observado; sin interpolación ni ajuste automático de fase',
                papeles_pal_por_lado_hd=list(papeles_pal),
                max_desfase_permitido=desfase, muestras_hd=len(dibujadas), pares=len(pares),
                campos_originales_sin_par=sin_par,
                error_max_camara={k: max(p['error_camara'][k] for p in pares)
                                  for k in ('ojo', 'objetivo', 'arriba', 'giro', 'fov')},
                zonas_distintas=sum(p['zonas'][0] != p['zonas'][1] for p in pares),
                pares_observados=pares,
                cobertura='solo los pares declarados; preparación/contacto distintos no aíslan la causa de las diferencias',
                aceptacion_escena=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--original', type=Path, required=True, help='carpeta de toma original')
    ap.add_argument('--hd', type=Path, help='carpeta de prueba con traza_hd.jsonl y medida.json')
    ap.add_argument('--max-desfase', type=float, default=.03, help='unidades del reloj original; se conserva el desfase de cada par')
    ap.add_argument('--papeles-pal', choices=('0,1', '1,0'), default='0,1',
                    help='índices PAL por papel HD, declarados explícitamente; Nieve observada:1,0')
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    if not math.isfinite(a.max_desfase) or not 0 <= a.max_desfase < .5:
        ap.error('--max-desfase debe ser finito entre0 y menos de0,5')
    if a.salida.exists():
        ap.error('la salida ya existe; conservarla y elegir otra')
    filas, sha = cargar(a.original/'traza.json')
    preparacion, prep_sha = cargar(a.original/'preparacion.json')
    resultado = dict(original=dict(ruta=str(a.original), traza_sha256=sha,
                                   preparacion_sha256=prep_sha, preparacion=preparacion,
                                   **resumen_original(filas)))
    if a.hd:
        datos = (a.hd/'traza_hd.jsonl').read_bytes()
        hd = [json.loads(l) for l in datos.splitlines() if l.strip()]
        medida, medida_sha = cargar(a.hd/'medida.json')
        if not hd or not medida['inicio_real']:
            raise ValueError('la prueba HD no acredita inicio real')
        grupo, entrada = preparacion['grupo'], preparacion['pared']['entrada']
        if medida['grupo'] != grupo or medida['pared']['entrada'] != entrada:
            raise ValueError('original y HD seleccionan un grupo o entrada distintos')
        if any(r['grupo'] != grupo or r['entrada'] != entrada for r in hd):
            raise ValueError('la traza HD mezcla escenas')
        resultado['hd'] = dict(ruta=str(a.hd), traza_sha256=hashlib.sha256(datos).hexdigest(),
                               medida_sha256=medida_sha, resultado_vm=medida['resultado_vm'],
                               restauracion_bancos=medida['restauracion_bancos'])
        resultado['comparacion'] = comparar(filas, hd, a.max_desfase,
                                            tuple(map(int, a.papeles_pal.split(','))))
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(resultado, f, indent=2, allow_nan=False)
    c = resultado.get('comparacion')
    print(json.dumps(dict(salida=str(a.salida), original=resultado['original'],
                          comparacion=None if c is None else {k: v for k, v in c.items()
                                                             if k not in ('pares_observados', 'campos_originales_sin_par')}), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

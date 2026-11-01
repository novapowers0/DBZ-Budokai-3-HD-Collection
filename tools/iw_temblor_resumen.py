"""Resume curvas IW: cambios de signo dentro de un plano, nunca entre cortes.

No rellena datos ausentes ni aprueba automaticamente la igualdad visual.
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np


def cargar(fn):
    if 'original' in fn.stem:
        rows = list(csv.DictReader(fn.open(encoding='utf-8')))
        if not rows:
            return np.array([]), np.empty((0, 3)), np.array([]), None
        n = np.array([int(r['n']) for r in rows])
        m = np.array([[float(r[f'm{i}']) for i in range(16)] for r in rows])
        e = -np.einsum('ni,nji->nj', m[:, 12:15], m[:, :12].reshape(-1, 3, 4)[:, :, :3])
        st = np.array([int(r['estado']) for r in rows])
        c = np.array([int(r['anim']) for r in rows])
        meta = json.loads(fn.with_suffix('.json').read_text()) if fn.with_suffix('.json').exists() else {}
        inicio = meta.get('n_inicio')
        if inicio is not None:
            mask = n >= inicio
            n, e, st, c = n[mask], e[mask], st[mask], c[mask]
        return n * 1.2, e, st * 0x1000 + c, None
    r = [r for r in csv.reader(fn.open()) if len(r) == 19]
    e = np.array([[float(x) for x in r[1:4]] for r in r])
    c = np.array([int(r[17], 16) for r in r])
    on = np.array([int(r[8]) for r in r])
    pedido = np.array([[float(x) for x in r[11:14]] for r in r])
    n = np.arange(len(r), dtype=float)
    meta = json.loads(fn.with_suffix('.json').read_text()) if fn.with_suffix('.json').exists() else {}
    if meta.get('marcas_fila'):
        k = meta['marcas_fila'][0][0]
        n, e, c, on, pedido = n[k:], e[k:], c[k:], on[k:], pedido[k:]
    err = np.linalg.norm(e[on != 0] - pedido[on != 0], axis=1)
    return n, e, c, err


def medir(n, e, c):
    if len(n) < 3:
        return {'muestras': len(n)}
    dt = np.diff(n)
    de = np.diff(e, axis=0)
    v = de / dt[:, None]
    continuidad = (dt > 0) & (dt <= 1.21) & (c[1:] == c[:-1]) & (np.linalg.norm(de, axis=1) < 50)
    valido = continuidad[1:] & continuidad[:-1]
    cambios = ((v[1:] * v[:-1] < 0) & (np.abs(v[1:]) > .02) & (np.abs(v[:-1]) > .02) & valido[:, None])
    accel = np.linalg.norm(np.diff(v, axis=0), axis=1)[valido]
    return {'muestras': len(n), 'pares_validos': int(valido.sum()), 'signos': int(cambios.sum()),
            'signos_1000': round(float(cambios.sum()) * 1000 / max(1, valido.sum()), 3),
            'aceleracion_p95': round(float(np.percentile(accel, 95)), 5) if len(accel) else None,
            'salto_max': round(float(np.linalg.norm(de, axis=1).max()), 3),
            'huecos': int((dt > 1.21).sum())}


def grafica(series, dst, clave):
    colores = {'original': '#3fa36c', 'antes': '#ce684d', 'despues': '#547bdd', 'antecedente_piloto': '#a7a7a7'}
    s = ['<svg xmlns="http://www.w3.org/2000/svg" width="1100" height="630" viewBox="0 0 1100 630">',
         '<rect width="1100" height="630" fill="#fff"/>',
         f'<text x="60" y="26" font-size="19">{clave}: desplazamiento del ojo desde el inicio del tramo (unidades IW)</text>']
    for eje in range(3):
        vals = [e[:, eje] - e[0, eje] for _, n, e in series if len(e)]
        if not vals:
            continue
        bajo, alto = min(v.min() for v in vals), max(v.max() for v in vals)
        rango = max(1, alto - bajo)
        top = 65 + eje * 175
        s.append(f'<path d="M60,{top} V{top+140} H1050" fill="none" stroke="#ddd"/>')
        s.append(f'<text x="8" y="{top+60}" font-size="13">{"XYZ"[eje]}</text>')
        for etiqueta, n, e in series:
            if not len(e):
                continue
            x = n - n[0]
            pts = ' '.join(f'{60+min(990,float(xx)*990/720):.2f},{top+140-float(v-bajo)*140/rango:.2f}'
                           for xx, v in zip(x, e[:, eje] - e[0, eje]) if xx <= 720)
            s.append(f'<polyline points="{pts}" fill="none" stroke="{colores[etiqueta]}" stroke-width="1"/>')
    for i, (etiqueta, _, _) in enumerate(series):
        s.append(f'<text x="{60+i*245}" y="610" fill="{colores[etiqueta]}" font-size="15">{etiqueta}</text>')
    s.append('</svg>')
    dst.write_text('\n'.join(s), encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--datos', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--antecedentes', type=Path)
    ap.add_argument('--tabla', type=Path)
    a = ap.parse_args()
    a.salida.mkdir(parents=True, exist_ok=True)
    resumen = {}
    for clave in range(0x7D0, 0x7E2):
        k = f'{clave:X}'
        datos, series = {}, []
        for etiqueta in ('original', 'antes', 'despues', 'antecedente_piloto'):
            fn = a.datos / f'{k}_{etiqueta}.csv'
            if etiqueta == 'antecedente_piloto':
                if not a.antecedentes:
                    continue
                fn = a.antecedentes / f'{k}.csv'
            if not fn.exists():
                continue
            if etiqueta != 'antecedente_piloto' and not fn.with_suffix('.json').exists():
                continue  # tanda todavia abierta: no presentar una curva parcial como final
            n, e, c, err = cargar(fn)
            datos[etiqueta] = medir(n, e, c)
            if etiqueta == 'original':
                meta = json.loads(fn.with_suffix('.json').read_text())
                datos[etiqueta]['ready_observado'] = meta.get('n_inicio') is not None
            if err is not None and len(err):
                datos[etiqueta]['error_render_pedido_p95'] = round(float(np.percentile(err, 95)), 5)
            series.append((etiqueta, n, e))
            # CSV de medidas, no recursos del juego. Conserva el original sin sobrescribirlo.
            with (a.salida / f'{k}_{etiqueta}_ojo.csv').open('w', newline='') as f:
                wr = csv.writer(f)
                wr.writerow(['cuadro_logico', 'ojo_x', 'ojo_y', 'ojo_z', 'estado_anim'])
                wr.writerows(zip(n, e[:, 0], e[:, 1], e[:, 2], c))
        datos['dictamen'] = 'PENDIENTE: no certifica igualdad; faltan original o antes/despues comparables'
        resumen[k] = datos
        # Los teletransportes del piloto no deben comprimir las curvas nuevas
        # hasta hacer invisible el movimiento real en la misma escala.
        activas = [s for s in series if s[0] != 'antecedente_piloto']
        grafica(activas or series, a.salida / f'{k}.svg', k)
        print(k, json.dumps(datos, ensure_ascii=False))
    (a.salida / 'resumen.json').write_text(json.dumps(resumen, indent=2, ensure_ascii=False), encoding='utf-8')
    cobertura = {'original_con_READY': sum(d.get('original', {}).get('ready_observado', False) for d in resumen.values()),
                 'antes': sum('antes' in d for d in resumen.values()),
                 'despues': sum('despues' in d for d in resumen.values()),
                 'con_las_tres_curvas': sum(d.get('original', {}).get('ready_observado', False) and
                     'antes' in d and 'despues' in d for d in resumen.values()),
                 'igualdad_certificada': 0, 'total_minijuegos': 18}
    (a.salida / 'cobertura.json').write_text(json.dumps(cobertura, indent=2), encoding='utf-8')
    if a.tabla:
        # Releer justo antes de cambiar solo nuestra columna: modelos/animacion
        # pueden estar siendo escritos por los otros dos encargos.
        lineas = a.tabla.read_text(encoding='utf-8').splitlines()
        for i, linea in enumerate(lineas):
            if not linea.startswith('| 0x7'):
                continue
            cols = linea.split('|')
            k = cols[1].strip()[2:]
            if k not in resumen:
                continue
            d = resumen[k]
            partes = []
            for e in ('original', 'antes', 'despues'):
                if e in d:
                    sufijo = ' (sin READY, no comparable)' if e == 'original' and not d[e].get('ready_observado') else ''
                    partes.append(f'{e}: {d[e].get("signos_1000", "sin pares")}{sufijo}')
            cols[4] = ' ' + ('; '.join(partes) if partes else 'Solo antecedente con piloto') + '; **comparacion pendiente** (`temblor/' + k + '.svg`) '
            lineas[i] = '|'.join(cols)
        a.tabla.write_text('\n'.join(lineas)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()

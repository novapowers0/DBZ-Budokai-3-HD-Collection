#!/usr/bin/env python3
"""Inventario local, sin modificar los juegos, de la cola FRI de XV2 y una plantilla B3."""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..', '..', 'awo_tools')]

import numpy as np
import sdbh_model as SM
import xeno_bcs as B
import xv2_a_b3 as X
import xv2_leer as L


def cadena(names, parents, marca):
    ids = [i for i, n in enumerate(names) if marca in n.upper()]
    return [{'indice': i, 'nombre': names[i], 'padre': names[parents[i]] if parents[i] >= 0 else None}
            for i in ids]


def propuesta_mapeo(origen, destino):
    """Candidatos por posicion en cadenas lineales; no aplica deformaciones ni animaciones."""
    if not origen or not destino:
        return []
    for cadena_ in (origen, destino):
        if any(cadena_[i]['padre'] != cadena_[i - 1]['nombre'] for i in range(1, len(cadena_))):
            raise ValueError('La cadena de cola no es lineal o no esta ordenada')
    salida = []
    for i, fuente in enumerate(origen):
        p = i * (len(destino) - 1) / max(1, len(origen) - 1)
        padre = int(p)
        hijo = min(padre + 1, len(destino) - 1)
        fraccion = p - padre
        salida.append(dict(origen=fuente['nombre'], padre=destino[padre]['nombre'],
                           hijo=destino[hijo]['nombre'], peso_hijo=fraccion))
    return salida


def estudiar(plantilla, limite):
    c = L.cpk_xv2()

    def get(path):
        actual = X._ci(c, path)
        if not actual:
            raise FileNotFoundError(path)
        return c.get(actual)

    esk = X.xm.Esk(get('data/chara/FRI/FRI_000.esk'))
    bcs = B.leer_bcs(get('data/chara/FRI/FRI.bcs'))
    tpl = SM.Template(plantilla)
    pants = {}
    for ps in bcs['part_sets']:
        for p in ps['parts']:
            if p and p['type'] == 'Pants' and p['model'] >= 0:
                key = (p['chara'] or 'FRI', p['model'])
                pants.setdefault(key, []).append((ps['id'], p['hide_flags'], p['hide_materials']))
    muestras = []
    for (chara, model), flags in sorted(pants.items())[:limite]:
        ruta = 'data/chara/{0}/{0}_{1:03d}_Pants.emd'.format(chara, model)
        try:
            subs = X.xm.leer_emd(get(ruta))
        except FileNotFoundError:
            muestras.append(dict(chara=chara, modelo=model, disponible=False))
            continue
        cola, resto = 0, 0
        for sm in subs:
            influencias = np.asarray(['T_TAIL' in n.upper() for n in sm.bone_names])
            if not len(influencias):
                continue
            mascara = np.any(influencias[sm.bones] & (sm.weights > 0), axis=1)
            cola += int(np.count_nonzero(np.any(mascara[sm.tris], axis=1)))
            resto += int(np.count_nonzero(~np.any(mascara[sm.tris], axis=1)))
        muestras.append(dict(chara=chara, modelo=model, disponible=True, triangulos_con_cola=cola,
                             triangulos_sin_cola=resto, partsets_y_flags=flags))
    origen = cadena(esk.names, esk.parent, 'T_TAIL')
    destino = cadena(tpl.labels, tpl.parent, 'T_TAIL')
    return dict(cadena_xv2=origen, cadena_b3=destino, propuesta=propuesta_mapeo(origen, destino),
                paleta_b3=tpl.palette, prendas_total=len(pants), prendas_examinadas=muestras,
                alcance='La propuesta es preliminar: no prueba deformacion ni animacion; las influencias no prueban visibilidad.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('plantilla', help='bin B3 descomprimido del donante')
    ap.add_argument('--limite', type=int, default=20)
    args = ap.parse_args()
    if args.limite < 0:
        ap.error('--limite debe ser >= 0')
    print(json.dumps(estudiar(args.plantilla, args.limite), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

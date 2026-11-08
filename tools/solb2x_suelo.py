"""Contrasta el suelo MDD original en un punto; no ejecuta ni modifica el juego.

Enumera candidatos del árbol BC, selección geométrica y propiedades activas.
Es una medida estática: no sustituye la selección de cabecera del motor vivo.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import sys


def medir(grupo, variante, x, z):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent/'mod center hd'))
    import escenarios_convertir as E
    fuente = E.IsoB2('D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
    try:
        m, mad, mas, _ = fuente.piezas(grupo)
    finally:
        fuente.f.close()
    u = lambda b, p: struct.unpack_from('<I', b, p)[0]
    vec = lambda p: struct.unpack_from('<3f', m, p)
    ob = u(m, u(m, 32)+4*variante)
    tabla = u(m, ob+0xb0)
    pendientes = [u(m, ob+0xbc)]
    nodos, refs = set(), set()
    while pendientes:
        nodo = pendientes.pop()
        if not nodo or nodo in nodos:
            continue
        if len(nodos) >= 10000:
            raise ValueError('árbol BC excesivo')
        nodos.add(nodo)
        refs.update(u(m, u(m, nodo+0x4c)+4*i) for i in range(u(m, nodo+0x30)))
        pendientes.extend((u(m, nodo+0x40), u(m, nodo+0x44)))
    resultado = []
    for rec in sorted(refs):
        geo, bloque, amo = u(m, rec), u(m, rec+8), u(m, rec+28)
        n = vec(geo)
        if abs(n[1]) < 1e-8:
            continue
        pool = amo+u(m, bloque+4)+16
        vertices = [vec(pool+16*u(m, geo+20+4*i)) for i in range(3)]
        p = vertices[0]
        y = (sum(p[i]*n[i] for i in range(3))-n[0]*x-n[2]*z)/n[1]
        signos = []
        for i in range(3):
            a, b = vertices[i], vertices[(i+1)%3]
            signos.append((a[2]-z)*(b[0]-x)-(a[0]-x)*(b[2]-z))
        if min(signos) < -1e-4 and max(signos) > 1e-4:
            continue
        atributo = struct.unpack_from('<i', m, geo+16)[0]
        propiedades = []
        if 0 <= atributo < u(mas, 4):
            g = u(mas, u(mas, 8)+4*atributo)
            for i in range(u(mas, g+4)):
                q = u(mas, g+8)+16*i
                cat, idx = u(mas, q), u(mas, q+4)
                mg = u(mad, u(mad, 8)+4*cat)
                prop = u(mad, u(mad, mg+12)+4*idx)
                propiedades.append(dict(categoria=cat, indice=idx, flags=u(mad, prop)))
        resultado.append(dict(registro=(rec-tabla)//48, altura=y, normal=n,
                              atributo=atributo, propiedades=propiedades,
                              activa=any(p['flags'] & 1 for p in propiedades)))
    return dict(grupo=grupo, variante=variante, x=x, z=z, nodos_bc=len(nodos),
                referencias_bc=len(refs), candidatos=resultado,
                limite='Selección de cabecera viva y orden de búsqueda no evaluados')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--grupo', type=int, required=True)
    p.add_argument('--variante', type=int, default=0)
    p.add_argument('--x', type=float, required=True)
    p.add_argument('--z', type=float, required=True)
    a = p.parse_args()
    if not math.isfinite(a.x) or not math.isfinite(a.z):
        p.error('coordenadas no finitas')
    print(json.dumps(medir(a.grupo, a.variante, a.x, a.z), indent=2))


if __name__ == '__main__':
    main()

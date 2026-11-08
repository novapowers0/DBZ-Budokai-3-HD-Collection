"""Identidad del ancla corporal: RAM original retenida o muestreo B3 de lectura sola.

No compara trayectorias por ordinal ni valida una camara completa. El productor
EU208390 usa el primer indice s16 de la tabla del cuerpo; B3 820F9548 conserva
ese contrato, con el objeto en +650 en lugar de +640.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import struct
import zipfile


def leer(read, actor, *, hd=False):
    endian = '>' if hd else '<'
    body = 0x650 if hd else 0x640

    def raw(a, n):
        b = read(a, n)
        if b is None or len(b) != n:
            raise ValueError('Lectura incompleta del ancla corporal')
        return b

    def unpack(fmt, a):
        return struct.unpack(endian + fmt, raw(a, struct.calcsize(endian + fmt)))

    def pointer(a):
        p = unpack('I', a)[0]
        low, high = (0x40000000, 0xA0000000) if hd else (0x100000, 0x2000000)
        if not low <= p < high:
            raise ValueError('Puntero de cuerpo fuera del espacio observado')
        return p

    model = pointer(actor + body + 0x538)
    table = pointer(actor + body + 0x53C)
    bones = pointer(model + 0x408)
    index = unpack('h', table)[0]
    if not 0 <= index < 128:
        raise ValueError('Indice corporal no representable')
    root = unpack('3f', bones + 64 * index + 48)
    matrix = unpack('16f', actor)
    point = unpack('3f', actor + body)
    feet = unpack('3f', actor + 0x40)
    if not all(math.isfinite(x) for x in root + matrix + point + feet):
        raise ValueError('Ancla o matriz no finitas')
    # Matrices por filas del guest: t.x*fila0 + t.y*fila1 + t.z*fila2 + fila3.
    expected = [sum(root[j] * matrix[4*j+i] for j in range(3)) + matrix[12+i]
                for i in range(3)]
    return dict(actor=hex(actor), modelo=hex(model), tabla=hex(table),
        indice=index, raiz_modelo=root, matriz_actor=matrix, ancla=point,
        pies=feet, reconstruida=expected,
        error=max(abs(x-y) for x, y in zip(point, expected)),
        diferencia_pies_altura9=max(abs(x-y) for x, y in zip(point,
            (feet[0], feet[1]+9, feet[2]))))


def original(path):
    raw = path.read_bytes()
    rows = [json.loads(x) for x in raw.splitlines() if x.strip()]
    samples = []
    for row in rows:
        video = row['video']
        state_path = Path(video['ruta'])
        state = state_path.read_bytes()
        if hashlib.sha256(state).hexdigest() != video['sha256']:
            raise ValueError('El estado retenido cambio')
        with zipfile.ZipFile(state_path) as z:
            ram = z.read('eeMemory.bin')
        if len(ram) != 0x2000000:
            raise ValueError('RAM EE truncada')
        u = lambda a: struct.unpack_from('<I', ram, a)[0]
        if u(0x4A4D40) != row['n']:
            raise ValueError('RAM y fila no corresponden')
        actor = u(0x49CA70) + 0x800
        sample = leer(lambda a, n: ram[a:a+n], actor)
        sample.update(n=row['n'], sha256_estado=video['sha256'],
            estado=struct.unpack_from('<H', ram, actor+0xCBC)[0],
            modo_ancla=u(u(0x49CBC0)+0x1C8))
        samples.append(sample)
    if not samples:
        raise ValueError('Serie vacia')
    # La identificacion espacial usa estados independientes: no se declara
    # continuidad VSync a partir del contador de libpad.
    result = dict(serie=str(path), sha256=hashlib.sha256(raw).hexdigest(),
        muestras=len(samples), indices=sorted({r['indice'] for r in samples}),
        por_estado={str(e):dict(muestras=sum(r['estado']==e for r in samples),
            error=max(r['error'] for r in samples if r['estado']==e))
            for e in sorted({r['estado'] for r in samples})},
        max_error=max(r['error'] for r in samples),
        max_diferencia_pies_altura9=max(r['diferencia_pies_altura9'] for r in samples),
        filas=samples, continuidad_video=False, paridad_port=False)
    if result['indices'] != [1] or result['max_error'] > .001:
        raise ValueError('El primer ancla no coincide con la matriz del hueso1')
    return result


def analizar_port(path, seguimiento):
    with path.open(encoding='utf-8', newline='') as f:
        rows = list(csv.DictReader(f))
    with seguimiento.open(encoding='utf-8', newline='') as f:
        follow = [r for r in csv.DictReader(f) if int(r['raiz'])]
    if not rows or len(rows) != len(follow):
        raise ValueError('Ancla y seguimiento no conservan la misma publicacion')
    maximum, cache_error, publication_error = 0., 0., 0.
    for r, s in zip(rows, follow):
        if None in r or any(v is None or not math.isfinite(float(v)) for v in r.values()):
            raise ValueError('Sonda corporal incompleta/no finita')
        if int(r['indice']) != 1 or int(r['estado']) != int(s['estado']):
            raise ValueError('Indice/estado corporal sin pareja')
        local = [float(r['local'+k]) for k in 'xyz']
        matrix = [float(r['m'+str(i)]) for i in range(16)]
        point = [float(r['publicada'+k]) for k in 'xyz']
        expected = [sum(local[j]*matrix[4*j+i] for j in range(3))+matrix[12+i] for i in range(3)]
        maximum = max(maximum, max(abs(x-y) for x,y in zip(point, expected)))
        cache_error = max(cache_error, max(abs(x-float(r['cache'+k])) for k,x in zip('xyz', expected)))
        target = [point[0], point[1]-9, point[2]]
        publication_error = max(publication_error,
            max(abs(x-float(s['actual'+k])) for k,x in zip('xyz', target)))
    states = sorted({int(r['estado']) for r in rows})
    if maximum > .001 or publication_error > .001 or cache_error < .05 or not {4,21} <= set(states):
        raise ValueError(f'Ancla sin reconstruccion/frenada/dash: {maximum}/{publication_error}/{cache_error}/{states}')
    return dict(muestras=len(rows), estados=states, max_error_matriz=maximum,
        max_error_publicacion=publication_error, max_desfase_cache=cache_error,
        paridad_camara=False, paridad_trayectoria=False, normal_activado=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serie', type=Path, required=True)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    result = original(a.serie)
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(result, f, indent=1, ensure_ascii=False, allow_nan=False)
    print(json.dumps({k:v for k,v in result.items() if k!='filas'}, indent=1))


if __name__ == '__main__':
    main()

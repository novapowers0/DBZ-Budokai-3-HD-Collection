"""Recupera cámara, raíces y tareas de estados B2 PAL ya archivados.

Vincula cada estado con una muestra exacta de las tomas indicadas. No ejecuta
PCSX2 ni reconstruye cuadros intermedios: las lagunas siguen siendo lagunas.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import zipfile


def ampliar_estado(leer, mgr, con_tareas=True):
    """Direcciones PAL: 1CDED0/1CDC80, 1B2BC0/1B2990/1B29D0 y1ADC40."""
    def datos(p, n):
        if p < 0x10000 or p+n > 0x2000000:
            raise ValueError(f'rango RAM PAL inválido: {p:X}+{n:X}')
        b = leer(p, n)
        if len(b) != n:
            raise ValueError(f'lectura RAM incompleta: {p:X}+{n:X}')
        return b

    def u(p):
        return struct.unpack('<I', datos(p, 4))[0]

    def f(p, n=1):
        return list(struct.unpack('<'+'f'*n, datos(p, 4*n)))

    camaras = {}
    for nombre, off in (('actual', 0x1f0), ('aplicada', 0x1f4)):
        p = u(mgr+off)
        camaras[nombre] = None if not p else dict(
            direccion=p, ojo=f(p, 3), objetivo=f(p+16, 3), arriba=f(p+32, 3),
            giro=f(p+48)[0], fov=f(p+52)[0], flags=u(p+56))
    def controles_raiz(raiz):
        controles = []
        if not raiz:
            return controles
        for off in (8, 12):
            p = u(raiz+off)
            if not p:
                controles.append(None)
                continue
            modelo = u(p)
            c = dict(offset=off, direccion=p, modelo=modelo, tiempo=f(p+4)[0],
                     banco=u(p+8), clip=u(p+16), velocidad=f(p+20)[0],
                     modo=u(p+24), mezcla=f(p+28)[0])
            if off == 8 and modelo:
                matrices = u(modelo+0x408)
                c['clip_modelo'] = u(modelo+0x41c)
                c['matriz_hueso10'] = f(matrices+0x280, 16) if matrices else None
            controles.append(c)
        return controles

    raiz = u(0x3734a8)
    controles = controles_raiz(raiz)
    tareas = []
    for i in range(256 if con_tareas else 0):
        p = u(0x3dce50+4*i)
        if p:
            t = dict(ranura=i, direccion=p, usuario=u(p+16),
                     dibujo=u(p+20), paso=u(p+24), prioridad=u(p+28), flags=u(p+32))
            if t['dibujo'] == 0x1b29d0 and t['paso'] == 0x1b2990:
                t['controles90'] = controles_raiz(t['usuario'])
            tareas.append(t)
    return dict(camaras=camaras, raiz=raiz, controles=controles, tareas=tareas)


def clave(r):
    return r['campo'], r['stage'], r['zona'], r['evento']


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--toma', type=Path, action='append', required=True)
    ap.add_argument('--estados', type=Path, default=Path('D:/tmp/solB2X/estados/captura_estados'))
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    if a.salida.exists():
        ap.error('la salida ya existe; conservarla y elegir otra')
    muestras = {}
    for toma in a.toma:
        for r in json.loads((toma/'traza.json').read_text(encoding='utf-8')):
            muestras.setdefault(clave(r), []).append((toma, r))
    hallazgos = []
    examinados = 0
    for estado in sorted(a.estados.glob('*.p2s')):
        examinados += 1
        with zipfile.ZipFile(estado) as z:
            ram = z.read('eeMemory.bin')
            if len(ram) != 0x2000000:
                raise ValueError(f'RAM de tamaño no acreditado: {estado}')
            u = lambda p: struct.unpack_from('<I', ram, p)[0]
            mgr = u(0x3a44b4)
            if not 0x10000 <= mgr < len(ram)-0x19c60:
                continue
            k = (u(0x371550), u(0x373460), u(0x3734ac),
                 struct.unpack_from('<H', ram, mgr+0x19c5c)[0])
            enlaces = []
            for toma, r in muestras.get(k, []):
                reloj = struct.unpack_from('<f', ram, mgr+0x142fc)[0]
                if reloj != r['reloj']:
                    continue
                def actor_original(i):
                    x = mgr+0x210+0x9f50*i
                    vec = lambda off: list(struct.unpack_from('<3f', ram, x+off))
                    fl = lambda off: struct.unpack_from('<f', ram, x+off)[0]
                    modelo = u(x+0x9a4)
                    return dict(pos=vec(0x40), colision=vec(0x4b0), vel=vec(0x60),
                                codigo=u(x+0x990), clip=u(modelo+0x41c) if modelo else None,
                                giro=u(x+0xfac), tiempo_reaccion=fl(0xa00), umbral_reaccion=fl(0x9c14),
                                estado=ram[x+0xa04], pendiente=struct.unpack_from('<h', ram, x+0x9c26)[0],
                                tray=u(x+0x9c34))
                if any(actor_original(i) != v for i, v in enumerate(r['actores'])):
                    continue
                contacto = dict(registro=u(0x373500), punto=list(struct.unpack_from('<3f', ram, 0x3734e0)),
                                normal=list(struct.unpack_from('<3f', ram, 0x3734f0)))
                if contacto != r['contacto']:
                    continue
                fotos = []
                for meta in sorted(toma.glob('cuadro_*.json')):
                    if clave(json.loads(meta.read_text(encoding='utf-8'))) != k:
                        continue
                    # La imagen y RAM pertenecen al mismo SaveState sin avance.
                    from PIL import Image
                    with Image.open(io.BytesIO(z.read('Screenshot.png'))) as im:
                        reducido = im.convert('RGB').resize((im.width//2, im.height//2))
                    with Image.open(meta.with_suffix('.png')) as im:
                        if im.size != reducido.size or im.convert('RGB').tobytes() != reducido.tobytes():
                            continue
                    fotos.append(meta.stem)
                enlaces.append(dict(toma=str(toma), campo=r['campo'], reloj=reloj, fotos=fotos))
            if not enlaces:
                continue
            extra = ampliar_estado(lambda p, n: ram[p:p+n], mgr)
            with estado.open('rb') as f:
                digest = hashlib.file_digest(f, 'sha256').hexdigest()
            hallazgos.append(dict(estado=str(estado), sha256=digest,
                                  ram_sha256=hashlib.sha256(ram).hexdigest(), enlaces=enlaces, **extra))
    resultado = dict(cobertura='muestras archivadas; no es una curva completa',
                     estados_examinados=examinados, estados_vinculados=len(hallazgos), muestras=hallazgos)
    a.salida.write_text(json.dumps(resultado, indent=2, allow_nan=False), encoding='utf-8')
    print(json.dumps(dict(salida=str(a.salida), examinados=examinados, vinculados=len(hallazgos),
                         muestras=[dict(enlaces=r['enlaces'], tareas90=[dict(direccion=t['direccion'],
                                      flags=t['flags'], controles=t['controles90']) for t in r['tareas']
                                      if 'controles90' in t]) for r in hallazgos]), indent=2))
    return 0 if hallazgos else 2


if __name__ == '__main__':
    raise SystemExit(main())

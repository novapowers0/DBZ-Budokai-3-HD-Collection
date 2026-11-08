"""Relaciona el paquete de Serpiente con los slots de fondo originales retenidos.

Solo inventario binario autenticado; no ejecuta escena ni acepta imagen GS.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

from assets_censo import Game
from iwx_censo import entry, child, u32
from iwx_fondo_original import ELF_SHA


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fuente', type=Path, required=True)
    ap.add_argument('--original', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    original_bytes = a.original.read_bytes()
    original = json.loads(original_bytes)
    game = Game('iw_eu', a.fuente)
    if original['elf_sha256'] != ELF_SHA or game.source['elf_sha256'] != ELF_SHA:
        raise ValueError('Fuente o referencia distinta de IW EU')
    cmn = next(p for p in game.files if Path(p).name.upper() == 'DATA_CMN.AFS')
    raw = entry(game, cmn, 884)
    count, table = u32(raw, 0x10), u32(raw, 0x14)
    if raw[:4] != b'#AMB' or not 1 <= count <= 64:
        raise ValueError('Campo884 no es un paquete AMB acotado')
    children = []
    for i in range(count):
        offset, size, kind, selector = struct.unpack_from('<4I', raw, table+16*i)
        data = child(raw, i)
        children.append(dict(indice=i, offset=offset, bytes=size, tipo=kind,
            selector=selector, magic=data[:8].hex(), sha256=sha(data),
            cabecera=data[:80].hex()))
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'awo_tools'))
    import amo2awo
    geometries = {}
    ranges = {}
    for i in (1, 3):
        model_bytes = child(raw, i)
        parsed = amo2awo.PS2AMO(model_bytes)
        parts, axes, vertices = [], [], []
        for group_index, group in enumerate(parsed.amgs):
            for bone in range(group.nb):
                off = group.a+group.axes_rel+80*bone
                axes.append(dict(grupo=group_index, hueso=bone, offset=off,
                    padre=u32(model_bytes, off+0x40),
                    quaternion=struct.unpack_from('<4f', model_bytes, off),
                    posicion=struct.unpack_from('<4f', model_bytes, off+16)))
                vertices.append((off, 32))
            for part in group.parts:
                stride = 16*(1+bool(part.vtype & 1)+bool(part.vtype & 2)+bool(part.vtype & 4))
                points = [v[1] for v in part.verts]
                parts.append(dict(grupo=group_index, offset=part.off, hueso=part.bone,
                    textura=part.tex, shader=part.shader,
                    vtype=part.vtype, vflags=part.vflags,
                    vertices=len(points), stride=stride,
                    bounds_locales=[min(p[j] for p in points) for j in range(3)]+
                                   [max(p[j] for p in points) for j in range(3)] if points else None))
                vertices.extend((group.a+v[0], stride) for v in part.verts)
        ranges[i] = vertices
        geometries[i] = dict(grupos=len(parsed.amgs), huesos=parsed.nb,
            partes=parts, ejes=axes,
            bytes_ejes_vertices=sum(size for _, size in vertices),
            ejes_vertices_sha256=sha(b''.join(model_bytes[o:o+n] for o, n in vertices)))
    states = []
    for retained in original['instantaneas']:
        path = Path(retained['estado'])
        if sha(path.read_bytes()) != retained['sha256']:
            raise ValueError('Cambio el guardado original')
        with zipfile.ZipFile(path) as z:
            ram = z.read('eeMemory.bin')
        if len(ram) != 0x2000000 or u32(ram, 0x4A4D40) != retained['n']:
            raise ValueError('RAM o serial distinto')
        def checked(ptr, size):
            if not 0x100000 <= ptr <= len(ram)-size:
                raise ValueError(f'Puntero fuera de EE: {ptr:X}+{size:X}')
            return ram[ptr:ptr+size]
        stage, mini = u32(ram, 0x49CDB8), u32(ram, 0x49CBB8)
        checked(stage, 0xAE0)
        checked(mini, 0x1D0)
        bundle = u32(ram, mini+0x1C8)
        checked(bundle, 0x50)
        if checked(bundle, table+16*count) != raw[:table+16*count]:
            raise ValueError('La tabla AMB retenida difiere de884')
        matches = []
        for source_child in children:
            i, size = source_child['indice'], source_child['bytes']
            ptr = bundle+source_child['offset']
            before = child(raw, i)
            current = checked(ptr, size)
            changes = []
            counts = dict(igual=0, magic=0, puntero_rebasado=0, otro=0)
            if size % 4:
                raise ValueError('Hoja no alineada a palabra')
            for off in range(0, size, 4):
                old, new = u32(before, off), u32(current, off)
                if old == new:
                    category = 'igual'
                elif off == 0 and before[:1] == b'#' and current[:4] == b'!'+before[1:4]:
                    category = 'magic'
                elif 0 < old < size and new == ptr+old:
                    category = 'puntero_rebasado'
                else:
                    category = 'otro'
                counts[category] += 1
                if category != 'igual':
                    changes.append(dict(offset=off, fuente=old, ram=new, tipo=category))
            matches.append(dict(indice=i, direccion=ptr, bytes=size,
                fuente_sha256=source_child['sha256'], ram_sha256=sha(current),
                comparacion_palabras=counts, cambios=changes))
            if i in geometries:
                geom_bytes = b''.join(current[o:o+n] for o, n in ranges[i])
                if sha(geom_bytes) != geometries[i]['ejes_vertices_sha256']:
                    raise AssertionError(('Ejes/vertices RAM difieren de fuente', retained['n'], i))
                matches[-1]['ejes_vertices_byteidenticos'] = True
                matches[-1]['ejes_vertices_sha256'] = sha(geom_bytes)
        slots = []
        for i in range(4):
            model = u32(ram, stage+0x230+4*i)
            res = u32(ram, stage+0x118+4*i)
            source = u32(ram, stage+4*i)
            slots.append(dict(indice=i, modelo=model, recurso=res, fuente_modelo=source,
                hijo_modelo=next((r['indice'] for r in matches if r['direccion'] == source), None),
                hijo_recurso=next((r['indice'] for r in matches if r['direccion'] == res), None),
                modelo_cabecera=checked(model, 0x80).hex() if model else None,
                recurso_cabecera=checked(res, 0x80).hex() if res else None,
                fuente_cabecera=checked(source, 0x80).hex() if source else None))
        states.append(dict(n=retained['n'], estado=str(path), sha256=retained['sha256'],
            work=stage, mini=mini, paquete=bundle, cabecera=checked(bundle, 0x50).hex(),
            slots=slots, hojas=matches))
    result = dict(fuente=game.source, referencia_sha256=sha(original_bytes),
        herramienta_sha256=sha(Path(__file__).read_bytes()), paquete884_sha256=sha(raw),
        parser_sha256=sha(Path(amo2awo.__file__).read_bytes()),
        hijos=children, geometria_fuente=geometries, instantaneas=states,
        limites=['Identidad de punteros/tabla AMB retenidos; no historial de llamadas',
                 'Modelos AMO cargados tienen otros cambios no clasificados, conservados en el informe',
                 'Geometria verificada: registros VIF de vertices y primeros32B de cada eje',
                 'Bounds de partes locales; no son bounds finales de dibujo GS',
                 'No compara aun ventanas/materiales del port ni imagen GS'],
        ejecucion_natural=False,
        imagen_gs_comparada=False, paridad_port=False)
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(result, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida),
        instantaneas=[dict(n=r['n'], slots=[dict(indice=s['indice'],
            hijo_modelo=s['hijo_modelo'], hijo_recurso=s['hijo_recurso']) for s in r['slots']],
            hojas=[dict(indice=c['indice'], comparacion=c['comparacion_palabras'])
                   for c in r['hojas']]) for r in states]), ensure_ascii=False))


if __name__ == '__main__':
    main()

"""Audita en memoria el montaje884 y su reparto UV en el conversor vigente.

No instala mods ni escribe modelos de juego. Contrasta descriptores, no GS.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from assets_censo import Game
from iwx_censo import entry, child
from iwx_fondo_original import ELF_SHA


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fuente', type=Path, required=True)
    ap.add_argument('--modelos', type=Path, required=True)
    ap.add_argument('--uv', type=Path, required=True)
    ap.add_argument('--base', type=Path, help='auditoria anterior para verificar el resultado por defecto')
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    model_bytes, uv_bytes = a.modelos.read_bytes(), a.uv.read_bytes()
    models, uv = json.loads(model_bytes), json.loads(uv_bytes)
    if uv['modelos_sha256'] != sha(model_bytes) or not uv['pasa'] or uv['elf_sha256'] != ELF_SHA:
        raise ValueError('Contraste UV distinto del inventario o sin PASS')
    game = Game('iw_eu', a.fuente)
    if game.source['elf_sha256'] != ELF_SHA:
        raise ValueError('Fuente distinta de IW EU')
    cmn = next(p for p in game.files if Path(p).name.upper() == 'DATA_CMN.AFS')
    raw = entry(game, cmn, 884)
    if sha(raw) != models['paquete884_sha256']:
        raise ValueError('Paquete884 distinto del inventario')
    root = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(root/'mod center hd'), str(root/'awo_tools')]
    import campos_iw
    import amo2awo
    pieces = campos_iw.CAMPOS[884][3]
    if pieces != ((5, 6), (1, 2), (3, 4)):
        raise ValueError('Cambio el orden de montaje de884')
    terrain = child(raw, 5)
    terrain_textures = campos_iw.le(child(raw, 6), 0x10)
    texture_counts = [campos_iw.le(child(raw, t), 0x10) for _, t in pieces[1:]]
    attached, banks, next_tex = [], [], terrain_textures
    corrections = []
    for (model, texture), count in zip(pieces[1:], texture_counts):
        source = child(raw, model)
        corrected = campos_iw.sin_raiz_del_terreno(source, terrain)
        attached.append((corrected, next_tex))
        corrections.append(dict(hijo=model, fuente_sha256=sha(source),
            tras_resta_raiz_sha256=sha(corrected), cambio=corrected != source,
            raices=campos_iw._raices(source)))
        banks.append((model, next_tex, next_tex+count))
        next_tex += count
    original_attach = amo2awo.attach_rigid
    amo2awo.build_awg_todos.clear()
    amo2awo.attach_rigid = lambda model, extra, offset: original_attach(model, extra, offset, head=0)
    try:
        converted = amo2awo.convert_amo(terrain, attach=attached)
        captures = list(amo2awo.build_awg_todos)
    finally:
        amo2awo.attach_rigid = original_attach
    mcf = child(raw, 0)
    layers = campos_iw.capas_de(mcf)
    if layers is None:
        raise ValueError('Falta el MCF')
    # _nubes_de usa tam_zdd como metadato opaco de cada registro: aqui0,
    # porque esta auditoria no construye ni instala un ZDD.
    records = campos_iw._nubes_de(captures, terrain_textures, mcf, texture_counts, 0,
                                 layers[3], campos_iw.CIELO[884])
    candidates = campos_iw._nubes_de(captures, terrain_textures, mcf, texture_counts, 0,
        layers[3], campos_iw.CIELO[884], canales_material=True)
    if len(candidates) != len(records) or any(
            tuple(old[i] for i in (0, 1, 4, 8)) != tuple(new[i] for i in (0, 1, 4, 8))
            for old, new in zip(records, candidates)):
        raise AssertionError('El candidato cambio firmas, conteos o seguimiento de camara')
    candidate_by_signature = {r[0]: r for r in candidates}
    record_by_signature = {}
    for record in records:
        key = record[0]
        if key in record_by_signature:
            raise ValueError('Firma de ventanas duplicada: emparejamiento ambiguo')
        record_by_signature[key] = record
    originals = {}
    for state in uv['instantaneas']:
        for model in state['modelos']:
            key = model['hijo_modelo']
            current = {p['offset']: p for p in model['partes']}
            if key in originals and current != originals[key]:
                raise ValueError('Reparto original distinto entre instantaneas')
            originals[key] = current
    rows = []
    for draws, windows in captures:
        for draw in draws:
            part = draw['part']
            bank = next(((i, lo) for i, lo, hi in banks if lo <= part.tex < hi), None)
            if bank is None:
                continue
            model, tex_start = bank
            source_part = originals[model][part.off]
            if part.vtype != int(source_part['flags'], 16) or part.tex-tex_start != source_part['textura']:
                raise ValueError('Descriptor convertido no empareja con fuente')
            start, count = draw['A']
            signature = b''.join(windows[start:start+3]).hex().upper() if count >= 3 else None
            record = record_by_signature.get(signature)
            candidate = candidate_by_signature.get(signature)
            if count >= 3 and record is None:
                raise AssertionError('La firma convertida no emparejo con _nubes_de')
            channel = source_part['canal']
            expected_layer = channel-6 if channel is not None else None
            actual_layer = record[7] if record else None
            original_motion = list(layers[3][expected_layer]) if expected_layer is not None else [0., 0., 0., 0.]
            converted_motion = [record[2], record[3], record[5], record[6]] if record else [0., 0., 0., 0.]
            candidate_motion = [candidate[2], candidate[3], candidate[5], candidate[6]] if candidate else [0., 0., 0., 0.]
            rows.append(dict(hijo=model, offset=part.off, textura_origen=source_part['textura'],
                textura_convertida=part.tex, flags=source_part['flags'],
                ventanas=count, firma_sha256=sha(bytes.fromhex(signature)) if signature else None,
                canal_original=channel, capa_original=expected_layer,
                registro_convertido=record is not None, capa_convertida=actual_layer,
                sigue_convertido=record[8] if record else None,
                movimiento_original=original_motion, movimiento_convertido=converted_motion,
                movimiento_distinto=original_motion != converted_motion,
                capa_candidata=candidate[7] if candidate else None,
                movimiento_candidato=candidate_motion,
                candidato_distinto=original_motion != candidate_motion))
    if len(rows) != sum(len(m) for m in originals.values()):
        raise AssertionError('No se emparejaron todas las partes de fondo visibles')
    if any(r['candidato_distinto'] for r in rows):
        raise AssertionError('El candidato no reproduce el reparto UV contrastado')
    previous_sha = None
    if a.base:
        previous_bytes = a.base.read_bytes()
        previous_sha = sha(previous_bytes)
        previous = json.loads(previous_bytes)
        if previous['conversion_awo_sha256'] != sha(converted):
            raise AssertionError('El AWO cambio frente al resultado anterior')
        if len(previous['partes']) != len(rows) or any(
                old != {k: new[k] for k in old} for old, new in zip(previous['partes'], rows)):
            raise AssertionError('El reparto por defecto cambio frente a la auditoria anterior')
    report = dict(fuente=game.source, modelos_sha256=sha(model_bytes), uv_sha256=sha(uv_bytes),
        herramienta_sha256=sha(Path(__file__).read_bytes()),
        conversor_sha256=sha(Path(campos_iw.__file__).read_bytes()),
        amo2awo_sha256=sha(Path(amo2awo.__file__).read_bytes()),
        conversion_awo_sha256=sha(converted), bytes_awo=len(converted),
        raices_terreno=campos_iw._raices(terrain), correccion_raices=corrections,
        texturas_terreno=terrain_textures, bancos=banks, capas=layers,
        partes=rows, registros_nubes=len(records),
        partes_movimiento_distinto=sum(r['movimiento_distinto'] for r in rows),
        partes_candidato_distinto=sum(r['candidato_distinto'] for r in rows),
        paridad_reparto_uv_candidata=True,
        auditoria_previa_sha256=previous_sha, defecto_identico_al_anterior=True if a.base else None,
        auditoria_completa=True, paridad_reparto_uv=not any(r['movimiento_distinto'] for r in rows),
        ejecucion_natural=False, imagen_gs_comparada=False, paridad_port=False,
        limites=['Montaje AWO en memoria con head0; no ZDD completo ni mod instalado',
                 'tam_zdd=0 es metadato de auditoria, no un archivo utilizable',
                 'Compara seleccion de desplazamiento UV; no microprograma VU, GS ni pantalla',
                 'No prueba la precision geometrica final ni el seguimiento de camara del port'])
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida), auditoria_completa=True,
        partes=len(rows), registros_nubes=len(records),
        partes_movimiento_distinto=report['partes_movimiento_distinto'],
        partes_candidato_distinto=report['partes_candidato_distinto'],
        defecto_identico_al_anterior=report['defecto_identico_al_anterior'],
        paridad_reparto_uv=report['paridad_reparto_uv'], paridad_port=False)))


if __name__ == '__main__':
    main()

"""Contrasta bin_campo884 completo en memoria, sin instalar ni escribir arte."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys

from assets_censo import Game
from iwx_censo import entry
from iwx_fondo_original import ELF_SHA


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fuente', type=Path, required=True)
    ap.add_argument('--montaje', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    previous_bytes = a.montaje.read_bytes()
    previous = json.loads(previous_bytes)
    if not previous['auditoria_completa'] or previous['partes_candidato_distinto']:
        raise ValueError('Montaje previo sin contraste valido')
    game = Game('iw_eu', a.fuente)
    if game.source['elf_sha256'] != ELF_SHA:
        raise ValueError('Fuente distinta de IW EU')
    cmn = next(p for p in game.files if Path(p).name.upper() == 'DATA_CMN.AFS')
    used = {}

    class Source:
        def entry(self, index):
            data = entry(game, cmn, index)
            used[index] = dict(bytes=len(data), sha256=sha(data))
            return data

    root = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(root/'mod center hd'), str(root/'awo_tools')]
    import campos_iw
    import amo2awo
    if sha(Path(campos_iw.__file__).read_bytes()) != previous['conversor_sha256']:
        raise ValueError('Conversor distinto del montaje previo')
    if sha(Path(amo2awo.__file__).read_bytes()) != previous['amo2awo_sha256']:
        raise ValueError('Conversor AMO distinto del montaje previo')
    baseline = campos_iw.bin_campo(Source(), 884)
    old = copy.deepcopy(campos_iw.NUBES[884])
    candidate = campos_iw.bin_campo(Source(), 884, canales_uv_originales=True)
    new = copy.deepcopy(campos_iw.NUBES[884])
    if baseline != candidate:
        raise AssertionError('El candidato altero geometria/recursos del bin')
    parts = campos_iw.EC.hijos_amb(candidate, '>')
    zdd = next(candidate[o:o+s] for o, s, _t, _f in parts if candidate[o:o+4] == b'#ZDD')
    if len(old) != len(new) or len(new) != len(previous['partes']):
        raise AssertionError('Numero de registros distinto del montaje contrastado')
    by_signature = {p['firma_sha256']: p for p in previous['partes']}
    changed = 0
    for before, after in zip(old, new):
        if any(before[i] != after[i] for i in (0, 1, 4, 8)):
            raise AssertionError('Cambiaron firmas/conteos/tamano/seguimiento')
        if after[4] != len(zdd):
            raise AssertionError('Metadato tam_zdd no corresponde al ZDD completo')
        row = by_signature[sha(bytes.fromhex(after[0]))]
        if list(after[i] for i in (2, 3, 5, 6)) != row['movimiento_original']:
            raise AssertionError('Reparto del bin completo no coincide con el original contrastado')
        if after[7] != row['capa_candidata'] or after[1] != row['ventanas']:
            raise AssertionError('Capa o conteo distinto del montaje')
        # Mismo formato que poner_nubes; valida que -1 sobrevive a la serializacion.
        text = '%s,%d,%.7f,%.7f,%d,%.7f,%.7f,%d,%d' % after
        columns = text.split(',')
        if int(columns[7]) != after[7] or int(columns[4]) != len(zdd):
            raise AssertionError('Capa/tamano no sobrevive al formato de stage.toml')
        changed += before != after
    if changed != previous['partes_movimiento_distinto']:
        raise AssertionError('Cantidad de correcciones distinta del contraste previo')
    report = dict(pasa=True, fuente=game.source, entradas_iw=used,
        donante_hd=str(campos_iw.EC.US/'data_cmn.afs'), entrada_donante_hd=54,
        bin_default_sha256=sha(baseline), bin_candidato_sha256=sha(candidate),
        bytes_bin=len(candidate), bytes_zdd=len(zdd), registros=len(new),
        registros_corregidos=changed, bin_byteidentico=True, mod_instalado=False,
        arte_escrito=False, imagen_gs_comparada=False, paridad_port=False,
        montaje_sha256=sha(previous_bytes),
        herramienta_sha256=sha(Path(__file__).read_bytes()),
        conversor_sha256=sha(Path(campos_iw.__file__).read_bytes()),
        limites=['Conversion completa en memoria, sin instalacion ni ejecucion del juego',
                 'La seleccion UV corregida va en registros externos, no cambia los bytes del bin',
                 'Serializacion de capa/tamano comprobada, no carga natural por el runtime ni imagen GS'])
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida), pasa=True, registros=len(new),
        registros_corregidos=changed, bytes_bin=len(candidate), bytes_zdd=len(zdd),
        bin_byteidentico=True, mod_instalado=False)))


if __name__ == '__main__':
    main()

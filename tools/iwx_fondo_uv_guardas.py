"""Comprueba los limites opt-in del candidato UV884 sin abrir juegos ni mods."""
import argparse
import hashlib
import inspect
import json
from pathlib import Path
import sys
from types import SimpleNamespace


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(root/'mod center hd'), str(root/'awo_tools')]
    import campos_iw
    cases = []
    if inspect.signature(campos_iw.bin_campo).parameters['canales_uv_originales'].default is not False:
        raise AssertionError('Candidato activo por defecto en bin_campo')
    if inspect.signature(campos_iw._nubes_de).parameters['canales_material'].default is not False:
        raise AssertionError('Candidato activo por defecto en _nubes_de')
    cases.append('dos opciones internas apagadas por defecto')

    def forbidden():
        raise AssertionError('La guarda intento abrir la fuente del juego')

    campos_iw.afs_iw = forbidden
    for name, fields in (
            ('otro campo', dict(campos=[883])),
            ('varios campos', dict(campos=[884, 897])),
            ('solo terreno', dict(solo_terreno=True)),
            ('bin existente', dict(bin=str(Path(__file__))))):
        opts = dict(campos=[884], solo_terreno=False, bin=None,
                    intro_serpiente=False, uv_materiales_serpiente=True)
        opts.update(fields)
        try:
            campos_iw.cmd_mod(SimpleNamespace(**opts))
        except ValueError as exc:
            if '--uv-materiales-serpiente' not in str(exc):
                raise
            cases.append(name)
        else:
            raise AssertionError(('La CLI acepto un caso fuera del contrato', name))
    for field, terrain in ((883, False), (884, True)):
        try:
            campos_iw.bin_campo(None, field, terrain, canales_uv_originales=True)
        except ValueError:
            cases.append(f'bin_campo rechaza campo{field}/solo_terreno={terrain}')
        else:
            raise AssertionError('bin_campo acepto un montaje fuera del contrato')
    for selector in (1, 5, 10, 15):
        part = SimpleNamespace(tex=1, vtype=selector << 22)
        try:
            campos_iw._nubes_de([([{'part': part, 'A': (0, 3)}], [bytes(44)]*3)],
                1, b'#MCF', [1], 0, [(0., 0., 0., 0.)]*8, canales_material=True)
        except ValueError as exc:
            if 'selector' not in str(exc):
                raise
            cases.append(f'selector no contrastado{selector}')
        else:
            raise AssertionError('Selector desconocido aceptado')
    report = dict(pasa=True, casos=cases, fuente_abierta=False, mod_instalado=False,
        conversor_sha256=hashlib.sha256(Path(campos_iw.__file__).read_bytes()).hexdigest(),
        herramienta_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1)
    print(json.dumps(dict(salida=str(a.salida), pasa=True, guardas=len(cases),
                         fuente_abierta=False, mod_instalado=False)))


if __name__ == '__main__':
    main()

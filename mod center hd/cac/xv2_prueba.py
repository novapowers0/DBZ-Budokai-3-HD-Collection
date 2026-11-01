#!/usr/bin/env python3
"""Prueba local de las seis razas CaC: seleccion real -> bin -> lector independiente.

No toca partidas ni activa mods. --selftest no usa datos del juego.
"""
import argparse
import hashlib
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'awo_tools'), str(ROOT/'mod center hd/xv2')]
import xv2_cac_b3 as C


def selftest():
    C.selftest()
    import xv2_cac_preparar as P
    P.selftest()
    print('xv2_prueba: N0 OK (selecciones opcionales y ausencia, sin juegos)')


def run(args):
    import sdbh_model as S
    from xeno_binary import write_json
    if not args.trabajo.is_dir():
        raise ValueError('crea primero --trabajo')
    os.environ['DBZ3_CAC_CACHE'] = str(args.trabajo/'cache')
    os.environ['TEMP'] = os.environ['TMP'] = str(args.trabajo)
    results = []
    for race in C.PIEL_RAZA:
        output = args.trabajo/('cac_'+race+'.bin')
        if output.exists():
            raise FileExistsError('no se sobreescribe: '+str(output))
        sel = dict(C.DEFECTO, raza=race)
        esk, bcs, parts, _ = C.cargar_cac(C.L.cpk_xv2(), sel)
        required = {'Bust', 'Pants', 'Boots', 'Rist', 'FaceBase', 'FaceEye'}
        missing = sorted(required-set(parts))
        if missing:
            raise ValueError(race+': faltan piezas '+str(missing))
        C.componer(sel, str(args.plantilla), str(output))
        errors = S.verify(str(output))
        results.append(dict(race=race, selection=sel, pieces={k: sum(len(s.tris) for s in v['subs']) for k,v in parts.items()},
                            output=str(output), bytes=output.stat().st_size, structural_errors=errors,
                            sha256=hashlib.sha256(output.read_bytes()).hexdigest(), passed=not errors))
        print(race, 'PASS' if not errors else errors, output.stat().st_size)
    write_json(args.trabajo/'CAC_SEIS_RAZAS.json', results)
    return 0 if all(r['passed'] for r in results) else 1


def sin_xv2(args):
    from unittest.mock import patch
    from xeno_binary import write_json
    sys.path[:0] = [str(Path(__file__).parent), str(ROOT/'mod center hd')]
    import vista_heroe as H
    import sdbh_model as S
    data = args.plantilla.read_bytes()
    class Afs:
        def entry(self, index):
            assert index == 264
            return data
    ficha = H.leer_ficha('cuerpo=110;traje=0;donante=0;pelo=0;c1=0,0,0;c2=0,0,0;logo=0;scouter=0')
    results = []
    for installed, enabled in ((False, '1'), (True, '0')):
        with patch.object(C.L, 'disponible', return_value=installed), \
             patch.object(C, 'componer', side_effect=AssertionError('no debe convertir XV2')), \
             patch.dict(os.environ, {'DBZ3_CAC_XV2': enabled}):
            model, report = H.modelo_xv2(Afs(), ficha, str(args.trabajo))
        assert report['fuente'] == 'nativo B3 264' and report['cuerpo_nativo'] == 264
        assert model == data, 'sin cambios de apariencia debe conservar exactamente el bin B3'
        results.append(dict(installed=installed, enabled=enabled, passed=True, **report))
    write_json(args.trabajo/'CAC_SIN_XV2.json', results)
    print('PASS: sin XV2 y con opcion apagada, modelo B3 exacto y conversor XV2 no invocado')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--sin-xv2', action='store_true')
    ap.add_argument('--plantilla', type=Path)
    ap.add_argument('--trabajo', type=Path, default=Path('D:/tmp/xv4'))
    args = ap.parse_args()
    if args.selftest:
        selftest(); return 0
    if not args.plantilla:
        ap.error('indica --plantilla')
    return sin_xv2(args) if args.sin_xv2 else run(args)


if __name__ == '__main__':
    sys.exit(main())

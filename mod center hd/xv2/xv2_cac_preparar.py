#!/usr/bin/env python3
"""Prepara en local los indices CaC que ya consume el menu Heroe de B3.

No activa dbz3_cac_xv2 ni convierte/reparte arte. Sin XV2 no escribe nada.
"""
from pathlib import Path
import os
import sys
from datetime import datetime
import shutil

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent.parent/'awo_tools')]


def preparar(juego, cpk=None, respaldo=None):
    import xv2_leer as L
    from xeno_importar import proteger_fuente
    from xv2_bcs_datos import volcar
    source = cpk or L.buscar_xv2()
    if not source or not Path(source).is_file():
        print('Xenoverse 2 no instalado: se conservan las piezas B3 y los ajustes actuales.')
        return None
    game = Path(juego).resolve()
    if not game.is_dir():
        raise ValueError('no existe la carpeta del juego B3')
    dst = game/'res/datos/xv2_cac.toml'
    backup = Path(respaldo) if respaldo else game/'user_data/dbz3/xv2_respaldos'
    proteger_fuente(source, [dst, backup])
    dst.parent.mkdir(parents=True, exist_ok=True)
    old = os.environ.get('DBZ3_XENO2_CPK')
    try:
        os.environ['DBZ3_XENO2_CPK'] = str(source)
        stage = dst.with_name('xv2_cac_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.tmp')
        volcar(str(stage))
        if dst.exists():
            backup.mkdir(parents=True, exist_ok=True)
            shutil.move(str(dst), str(backup/('xv2_cac_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.toml')))
        shutil.move(str(stage), str(dst))
    finally:
        if old is None: os.environ.pop('DBZ3_XENO2_CPK', None)
        else: os.environ['DBZ3_XENO2_CPK'] = old
    print('Catalogo local preparado. En Personajes activa «Usar piezas de Xenoverse 2 (opcional)». B3 sigue por defecto.')
    return dst


def selftest():
    from unittest.mock import patch
    import xv2_leer as L
    with patch.object(L, 'buscar_xv2', return_value=None), patch.object(Path, 'mkdir', side_effect=AssertionError('escritura')):
        assert preparar('no_se_abre') is None
    print('xv2_cac_preparar: N0 OK (sin XV2 no se escribe ni se activa nada)')


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--juego', type=Path)
    args = ap.parse_args()
    if args.selftest: selftest()
    elif args.juego: preparar(args.juego)
    else: ap.error('indica --juego o --selftest')

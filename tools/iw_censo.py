# SPDX-License-Identifier: MIT
"""Censa los minijuegos IW, los valores compilados y la cobertura offline."""
import argparse
import hashlib
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[1]
AFS = Path('ps2_games/Infinite World (USA)/USR/DATA_CMN.AFS')
sys.path.insert(0, str(RAIZ / 'tools/iw_mips'))

# Inventario curado según las ramas de src; no deduce la ruta por contar cvars.
# Familia, ruta de la revisión, cvars relevantes (sin prefijo), alcance del CLI.
CURSOS = {
    '0x7D0': ('Raditz', 'mixta', '7d0_original cierre_campo_original hito_original seguimiento_original', 'trazas'),
    '0x7D1': ('Bubbles', 'mixta', '7d1_original seguimiento_original', 'regresion'),
    '0x7D2': ('Camino de la Serpiente', 'mixta', '7d0_original serpiente_original caida_original hito_original', 'trazas'),
    '0x7D3': ('Ladrillos', 'original', '7d3_original', 'capturas'),
    '0x7D4': ('NB006', 'original', '7d4_original', 'focal'),
    '0x7D5': ('NB007', 'port', '7d5_original 7d5_fin_camara_original', 'focal'),
    '0x7D6': ('Vuelo Vegeta', 'original', 'vuelo_original', 'focal'),
    '0x7D7': ('Esquiva Freezer', 'mixta', 'disparo_continuo_original disparo_original', 'focal'),
    '0x7D8': ('Esquiva Piccolo', 'mixta', 'disparo_continuo_original disparo_original', 'focal'),
    '0x7D9': ('Nave Namek', 'mixta', '7d0_original geiser_original hito_original', 'trazas'),
    '0x7DA': ('NB012', 'original', '7da_original', 'focal'),
    '0x7DB': ('NB013', 'original', '7db_original', 'focal'),
    '0x7DC': ('NB014', 'original', '7dc_original', 'focal'),
    '0x7DD': ('Persecución Piccolo', 'port', '7dd_original 7dd_captura_original seguimiento_original', 'focal'),
    '0x7DE': ('Ten contra Cell', 'port', 'disparo_continuo_original disparo_original disparo_carga_original disparo_intro_original disparo_mira_original disparo_voces_poses_original', 'focal'),
    '0x7DF': ('Radar', 'mixta', '7d0_original radar_original hito_original', 'trazas'),
    '0x7E0': ('Vuelo Goten', 'original', 'vuelo_original', 'focal'),
    '0x7E1': ('MSN019', 'original', 'traducido', 'trazas'),
    'MAPA': ('Dragon Mission', 'original', 'mapa_original mapa_globo_original mapa_frase_original mapa_inicio_original resultado_original', 'diagnostico'),
    'ROAD': ("Fighter's Road", 'original', 'mapa_original mapa_inicio_original', 'diagnostico'),
}

# Supuestos de la clasificación curada; un default distinto pide revisión.
OFF = set('7d1_original hito_original seguimiento_original serpiente_original caida_original '
          '7d5_original disparo_original 7dd_original 7dd_captura_original disparo_carga_original '
          'disparo_intro_original disparo_mira_original disparo_voces_poses_original'.split())
R_BOOL = re.compile(r'\bREXCVAR_DEFINE_BOOL\s*\(\s*(dbz3_iw_\w+)\s*,\s*(true|false)\s*,')
R_COMENTARIO = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:(?:\\.)|[^\'\\\n])*\'|//[^\n]*|/\*[\s\S]*?\*/')


def leer_cvars(raiz):
    out = {}
    for ruta in sorted((raiz / 'src').rglob('*')):
        if ruta.suffix not in {'.cpp', '.cc', '.cxx', '.hpp', '.h', '.inl'} or ruta.name.endswith('.local.inc'):
            continue
        texto = ruta.read_text(encoding='utf-8', errors='replace')
        limpio = R_COMENTARIO.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]), texto)
        for m in R_BOOL.finditer(limpio):
            out.setdefault(m[1], []).append({'valor': m[2] == 'true', 'archivo': ruta.relative_to(raiz).as_posix(),
                                              'linea': 1 + limpio.count('\n', 0, m.start())})
    return out


def filas_dmd(dmd):
    if len(dmd) < 16 or not dmd.startswith(b'#DMD'):
        raise ValueError('#DMD 1093 no localizado o incompleto')
    n, = struct.unpack_from('<I', dmd, 8)
    fin = 16 + 4 * n
    if fin > len(dmd):
        raise ValueError('índice #DMD truncado')
    filas = {}
    for fila, (pos,) in enumerate(struct.iter_unpack('<I', dmd[16:fin])):
        if pos < fin or pos + 0x24 > len(dmd):
            raise ValueError(f'fila #DMD {fila} fuera de rango')
        clave, = struct.unpack_from('<I', dmd, pos + 0x20)
        if 0x7D0 <= clave <= 0x7E1:
            filas.setdefault(f'0x{clave:03X}', []).append(fila)
    return filas


def leer_dmd(afs):
    import mapa_espejo as mapa
    try:
        dmd = mapa.dmd_de(mapa.entrada_afs(1093, afs))
        return {'afs': str(afs), 'sha256_dmd': hashlib.sha256(dmd).hexdigest(), 'filas': filas_dmd(dmd)}
    except (OSError, ValueError, struct.error) as exc:
        return {'afs': str(afs), 'filas': None, 'motivo': str(exc)}


def generar(raiz, afs):
    cvars = leer_cvars(raiz)
    dmd = leer_dmd(afs)
    cursos = []
    for clave, (nombre, ruta, cvar, cobertura) in CURSOS.items():
        usados, cambios = {}, []
        for corto in ('logica_original ' + cvar).split():
            k = 'dbz3_iw_' + corto
            defs = cvars.get(k, [])
            valor = defs[0]['valor'] if len(defs) == 1 else None
            usados[k] = valor
            if valor != (corto not in OFF) and corto != '7d5_fin_camara_original':
                cambios.append(k)
        cursos.append(dict(clave=clave, nombre=nombre, ruta='revisar' if cambios else ruta,
                           cvars=usados, revisar=cambios, cobertura=cobertura,
                           filas_dmd=dmd['filas'].get(clave, []) if dmd['filas'] is not None and clave.startswith('0x') else None))
    return dict(raiz=str(raiz), cvars=cvars, dmd_1093=dmd, cursos=cursos,
                por_ruta=dict(Counter(c['ruta'] for c in cursos)),
                por_cobertura=dict(Counter(c['cobertura'] for c in cursos)))


def markdown(datos):
    dmd = datos['dmd_1093']
    lineas = ['# Censo IW', '', 'Ruta curada a partir de src; no mide la ruta tomada en juego.', '',
              f"AFS: `{dmd['afs']}` · #DMD: {dmd.get('sha256_dmd') or dmd.get('motivo')}", '',
              '| Clave | Curso | Ruta por defecto | Cotejo | Filas #DMD |', '|---|---|---|---|---|']
    for c in datos['cursos']:
        filas = c['filas_dmd']
        lineas.append(f"| {c['clave']} | {c['nombre']} | {c['ruta']} | {c['cobertura']} | "
                      f"{', '.join(map(str, filas)) if filas else ('sin dato' if filas is None else 'no consta')} |")
    lineas += ['', '## Cvars', '', '| Cvar | Valor declarado | Fuente |', '|---|---|---|']
    for k, defs in sorted(datos['cvars'].items()):
        for d in defs:
            lineas.append(f"| `{k}` | {str(d['valor']).lower()} | `{d['archivo']}:{d['linea']}` |")
    return '\n'.join(lineas) + '\n'


def autoprueba():
    muestra = bytearray(0x50)
    muestra[:4] = b'#DMD'
    struct.pack_into('<I', muestra, 8, 1)
    struct.pack_into('<I', muestra, 16, 0x20)
    struct.pack_into('<I', muestra, 0x40, 0x7D5)
    if filas_dmd(muestra) != {'0x7D5': [0]}:
        raise RuntimeError('autoprueba #DMD fallida')
    try:
        filas_dmd(muestra[:0x43])
    except ValueError:
        pass
    else:
        raise RuntimeError('se aceptó una fila truncada')
    if len(CURSOS) != 20:
        raise RuntimeError('catálogo incompleto')
    print('iw_censo: autoprueba OK')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raiz', type=Path, default=RAIZ, help='árbol con src/')
    p.add_argument('--afs', type=Path, help='DATA_CMN.AFS (por defecto la copia USA bajo --raiz)')
    p.add_argument('--json', type=Path, help='guardar censo estructurado')
    p.add_argument('--md', type=Path, help='guardar tabla Markdown')
    p.add_argument('--autoprueba', '--self-check', dest='autoprueba', action='store_true', help='verificar sin recursos del juego')
    a = p.parse_args()
    if a.autoprueba:
        return autoprueba()
    raiz = a.raiz.resolve()
    if not (raiz / 'src/iw_mips.cpp').is_file():
        p.error('--raiz no contiene src/iw_mips.cpp')
    d = generar(raiz, (a.afs or raiz / AFS).resolve())
    if a.json:
        a.json.write_text(json.dumps(d, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if a.md:
        a.md.write_text(markdown(d), encoding='utf-8')
    if not (a.md or a.json):
        print(markdown(d), end='')


if __name__ == '__main__':
    raise SystemExit(main())

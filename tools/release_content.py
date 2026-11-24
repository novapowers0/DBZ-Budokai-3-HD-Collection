"""File policy and byte-for-byte ZIP verification for the Windows game package."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import stat
from zipfile import ZIP_DEFLATED, ZipFile


ASSET_EXT = set('afs xex iso xiso chd cpk amb awo amo awg amg azt adx sfd mp4 mkv avi mp3 wav ogg flac p2s dds bin'.split())
RESIDUE = {'user_data', 'logs', 'saves', 'savegames', 'savedata', 'sstates', 'cache', 'caches', 'iso_cache', 'cache_ps2',
           'shader_cache', 'xex_cache', '__pycache__', '.pytest_cache', '.git', 'mods_nativos', 'partida_100'}
RESROOTS = {'ingame_ui': 'ingame_ui', 'heroe': 'res/heroe', 'mejoras': 'res/mejoras',
            'survivors': 'res/survivors', 'datos': 'res/datos'}
ART_EXT = {'.png', '.jpg', '.jpeg', '.webp', '.gif', '.bmp', '.tga', '.avif', '.svg', '.ico', '.psd'}
RESOURCE_EXT = {'.png', '.toml', '.json', '.md', '.txt'}


def blocked(name):
    """Accept either separator, reject ambiguous paths before touching their bytes."""
    norm = name.replace('\\', '/').rstrip('/').casefold()
    parts = norm.split('/')
    leaf = parts[-1]
    ext = leaf.rpartition('.')[-1]
    if any(not p or p in {'.', '..'} or p.endswith(('.', ' ')) or ':' in p for p in parts):
        return 'ruta ambigua'
    # Mandatory exception: the 100% save of Native mods ("Unlock everything" needs it; it's a progress save,
    # not game content). Only these two exact files. 1.5.7 shipped without them because of this rule (issue #74).
    if norm in {'mods_nativos/partida_100/data.bin', 'mods_nativos/partida_100/dbz3.header'}:
        return None
    if (parts[0] in {'us', 'eu', 'ps2_games'} or {'pack_hub', 'mods_ps2'}.intersection(parts)
            or ext in ASSET_EXT or 'eememory' in leaf):
        return 'datos/medios del juego'
    if (RESIDUE.intersection(parts) or ext in {'log', 'sav', 'save', 'dmp', 'pyc', 'pyo'}
            or leaf == 'dbz3.header' or re.search(r'(^dbz3_user\.toml(?:\.|$)|\.log\.|\.local\.inc$)', leaf)):
        return 'residuo/ajuste local/partida'
    if ext in {'pem', 'key', 'pfx', 'p12'} or leaf in {'.env', '.mcp.json', '.netrc'} or leaf.startswith('.env.'):
        return 'credencial/configuracion privada'
    if re.search(r'(?:historia_b1|b1_historia|b1_escen[ae]|b1_cine|escena_b1|escena_1910)', norm):
        return 'escena/historia B1 experimental'
    if leaf == 'xv2_cac.toml' or ('xv2' in norm and '.' + ext in ART_EXT):
        return 'datos locales o arte XV2'
    if leaf == 'psp_ramp.npy':
        return 'rampa de textura derivada del juego'
    if norm.startswith('ingame_ui/hub/anfitrion_'):
        return 'retrato: se resuelve desde el juego del jugador'
    if (norm.startswith(('res/heroe/gpt3/', 'res/mejoras/gpt3/', 'res/survivors/tanda5/', 'res/survivors/documentacion/'))
            or norm in {'res/survivors/inventario_tanda3.csv', 'res/survivors/leeme_tanda3.txt'}):
        return 'material de trabajo/arte pendiente de aprobacion'
    if parts[0] == 'mods' and norm not in {'mods', 'mods/readme.md'}:
        return 'mod preinstalado'
    if parts[0] == 'mod center hd' and '.' + ext in ART_EXT:
        return 'arte no permitido en las herramientas'
    return None


def tree(root):
    if root.is_symlink() or root.is_junction():
        raise ValueError(f'Enlace inesperado: {root}')
    for directory, dirs, files in os.walk(root):
        for name in dirs + files:
            path = Path(directory) / name
            rel = path.relative_to(root).as_posix()
            if path.is_symlink() or path.is_junction():
                raise ValueError(f'Enlace inesperado: {rel}')
        for name in files:
            path = Path(directory) / name
            rel = path.relative_to(root).as_posix()
            yield rel, path


def copy_resources(root, stage):
    for name, dest in RESROOTS.items():
        source = root / 'res' / name
        if not source.is_dir():
            raise ValueError(f'Faltan recursos: {source}')
        for rel, path in tree(source):
            publish = f'{dest}/{rel}'
            if blocked(publish) or path.suffix.lower() not in RESOURCE_EXT:
                print(f'Recurso omitido: {publish}')
                continue
            target = stage / publish
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)


def digest(stream):
    return hashlib.file_digest(stream, 'sha256').hexdigest()


def index(stage):
    if not stage.is_dir():
        raise ValueError(f'Falta el directorio: {stage}')
    entries = dict(tree(stage))
    for rel in entries:
        if reason := blocked(rel):
            raise ValueError(f'{rel}: {reason}')
    if len({name.casefold() for name in entries}) != len(entries):
        raise ValueError('Stage con nombres que colisionan en Windows')
    return entries


def create_zip(stage, archive):
    entries = index(stage)
    with ZipFile(archive, 'x', ZIP_DEFLATED, compresslevel=6) as z:
        for rel, path in sorted(entries.items()):
            z.write(path, rel)


def compare_zip(stage, archive):
    entries = index(stage)
    seen = set()
    with ZipFile(archive) as z:
        for info in z.infolist():
            rel = info.filename.replace('\\', '/').rstrip('/')
            key = rel.casefold()
            if (key in seen or info.orig_filename != info.filename or blocked(info.filename)
                    or stat.S_ISLNK(info.external_attr >> 16)):
                raise ValueError(f'Entrada no admisible o duplicada: {info.filename}')
            seen.add(key)
            if info.is_dir() or info.filename.endswith('\\'):
                if not any(name.startswith(rel + '/') for name in entries) and not (stage / rel).is_dir():
                    raise ValueError(f'Carpeta fuera del stage: {rel}')
                continue
            if rel not in entries:
                raise ValueError(f'Archivo fuera del stage: {rel}')
            with z.open(info) as packed, entries.pop(rel).open('rb') as local:
                if digest(packed) != digest(local):
                    raise ValueError(f'SHA-256 distinto, ZIP vs stage: {rel}')
    if entries:
        raise ValueError(f'Faltan en ZIP: {", ".join(sorted(entries))}')
    print(f'ZIP OK: {len(seen)} entradas; CRC y SHA-256 de todos los archivos frente al stage')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('action', choices=('resources', 'zip', 'check'))
    ap.add_argument('first', type=Path)
    ap.add_argument('second', type=Path)
    a = ap.parse_args()
    try:
        {'resources': copy_resources, 'zip': create_zip, 'check': compare_zip}[a.action](a.first, a.second)
    except (OSError, ValueError, RuntimeError) as exc:
        ap.exit(1, f'{exc}\n')


if __name__ == '__main__':
    main()

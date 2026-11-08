"""Censo reproducible de B2 desde la ISO; no escribe recursos del juego al repo.

La presencia de un recurso no demuestra su uso ni la fidelidad de su consumidor.
El JSON conserva cada guion YDW y cada clip de los bancos examinados.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'mod center hd'))
import escenarios_convertir as E


def u(b, p):
    return struct.unpack_from('<I', b, p)[0]


def inventario(fuente):
    elf = fuente.iso.read(fuente.iso.boot_elf())
    def va(a, n):
        ph, es, cantidad = u(elf, 28), *struct.unpack_from('<HH', elf, 42)
        for i in range(cantidad):
            typ, off, addr, _, size, *_ = struct.unpack_from('<8I', elf, ph+i*es)
            if typ == 1 and addr <= a and a+n <= addr+size:
                return elf[off+a-addr:off+a-addr+n]
        raise ValueError(f'dirección ELF sin respaldo: {a:X}')
    perfil = hashlib.sha256(elf).hexdigest()
    if perfil != '30c362969ad64184df0b4a4ee498badb101265470883948f3131b6302ae3c797':
        raise ValueError('Las tablas de este censo requieren el ELF PAL SLES-51839 medido')
    entradas = []
    for i in range(fuente.n):
        magic = fuente.magic(i).decode('ascii', errors='replace')
        entradas.append(dict(entrada=i, magic=magic, bytes=fuente.tab[2*i+1]))
    escenarios = []
    for grupo, mad_id in enumerate(fuente.escenarios()):
        mdd, mad, mas, spx = fuente.piezas(grupo)
        amcs = E.camaras_transicion(mdd)
        escenarios.append(dict(grupo=grupo, mad=mad_id, variantes=u(mdd, 4),
            raices=u(mdd, 8), camaras_trayectorias=len(amcs),
            spx_bytes=len(spx or b''), spx_sha256=hashlib.sha256(spx or b'').hexdigest(),
            estado='traducido sin comprobar' if grupo in (0,2,4,7,8) else 'sin transición acreditada'))
    fases = []
    for fase in range(9):
        y = fuente.entrada(387+fase)
        if y[:4] != b'#YDW':
            raise ValueError('falta YDW original')
        scripts = []
        for i in range(u(y, 0x48)):
            p = u(y, u(y, 0x4c)+4*i)
            ordenes = [struct.unpack_from('<6i', y, p+4+24*j) for j in range(u(y,p))]
            scripts.append(dict(id=i, ordenes=ordenes, estado='traducido sin comprobar'))
        fases.append(dict(fase=fase+1, entrada=387+fase, sha256=hashlib.sha256(y).hexdigest(),
            tablas=[u(y, 0x20+8*i) for i in range(8)], guiones=scripts,
            operaciones=dict(sorted(Counter(o[1] for g in scripts for o in g['ordenes']).items()))))
    # Bancos completos de conversación/viñetas y contexto: derivados de los
    # magics presentes en los intervalos originales, no de un catálogo del port.
    bancos = []
    for i in list(range(411,443))+list(range(794,849))+[134,175,334,336,931]:
        b = fuente.entrada(i)
        hijos = [(0,len(b))] if b[:4] == b'#AMM' else (
            [(o,n) for o,n,*_ in E.hijos_amb(b, '<')] if b[:4] == b'#AMB' else [])
        for off, size in hijos:
            amm = b[off:off+size]
            if amm[:4] != b'#AMM':
                continue
            clips = []
            for j in range(u(amm,16)):
                t = u(amm,20)+j*16
                tipo, variante, cuadros, datos = struct.unpack_from('<4I',amm,t)
                clips.append(dict(id=j,tipo=tipo,variante=variante,cuadros=cuadros,
                                  estado='traducido sin comprobar'))
            bancos.append(dict(entrada=i, offset=off, bytes=size, huesos=u(amm,24), clips=clips))
    return dict(elf_sha256=perfil, afs_entradas=entradas, escenarios=escenarios,
                fases=fases, bancos=bancos,
                tabla_escenarios_372AB0=va(0x372AB0,13*4).hex(),
                tabla_nave_premios_3782A0=va(0x3782A0,43*4).hex(),
                limitaciones=['Censo estático: no acredita ejecución ni igualdad visual.',
                    'Los bancos observados no son un cierre del conjunto de dependencias SPX.',
                    'Faltan traza completa de actores/cámara y sonido de cada modo.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', required=True)
    p.add_argument('--salida', required=True, type=Path)
    a = p.parse_args()
    if not a.salida.parent.is_dir():
        p.error('la carpeta de salida debe existir')
    f = E.IsoB2(a.iso)
    try:
        d = inventario(f)
    finally:
        f.f.close()
    a.salida.write_text(json.dumps(d,ensure_ascii=False,indent=2),encoding='utf-8')
    print('ELF',d['elf_sha256'])
    print('AFS',len(d['afs_entradas']),'escenarios',len(d['escenarios']))
    for e in d['escenarios']:
        print('escenario',e)
    for fase in d['fases']:
        print('fase',fase['fase'],'guiones',len(fase['guiones']),
              'no vacíos',sum(bool(g['ordenes']) for g in fase['guiones']),
              'órdenes',sum(len(g['ordenes']) for g in fase['guiones']),
              'operaciones',list(fase['operaciones']))
    print('bancos',len(d['bancos']),'clips',sum(len(b['clips']) for b in d['bancos']))


if __name__ == '__main__':
    main()

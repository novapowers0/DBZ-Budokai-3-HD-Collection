"""Censo IW desde ELF/AFS del jugador; no confunde banco cargado y recurso usado.

Escribe solo metadatos en una salida fechada de D:/tmp/solIWX.
No ejecuta juegos ni modifica sus copias. Los códigos no observados se conservan
como candidatos, nunca como acciones que necesariamente use una misión.
"""
from collections import Counter
from datetime import datetime
import csv
import hashlib
import json
from pathlib import Path
import struct

from assets_censo import Game, ROOT, inventory
from assets_censo_iw import EU_DEFAULT
from iw_animaciones_N_resumen import alias

OUT = Path('D:/tmp/solIWX')/('iw_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f'))


def u32(b, p):
    return struct.unpack_from('<I', b, p)[0]


def child(b, i):
    assert b[:4] == b'#AMB' and i < u32(b, 16)
    p, n = struct.unpack_from('<II', b, u32(b, 20) + 16*i)
    assert p+n <= len(b)
    return b[p:p+n]


def entry(game, name, i):
    with game.open(name) as f:
        assert f.read(4) == b'AFS\0'
        count = struct.unpack('<I', f.read(4))[0]
        assert i < count
        f.seek(8+8*i)
        p, n = struct.unpack('<II', f.read(8))
        f.seek(p)
        b = f.read(n)
        assert len(b) == n
        return b


def csv_write(path, rows, fields=None):
    with path.open('w', encoding='utf-8-sig', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fields or list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    game = Game('iw_eu', EU_DEFAULT)
    game.files = {p: n for p, n in game.files.items()
                  if Path(p).name.upper() in ('DATA_CMN.AFS', 'DATA_ENG.AFS')}
    inv, issues = inventory(game)
    cmn = next(p for p in game.files if Path(p).name.upper() == 'DATA_CMN.AFS')
    # GroundMinigameCharacterProperties: 17 filas de 40 B; revisar firma,
    # no aplicar a USA el delta de direcciones de un ELF distinto.
    table = game.va(0x474080, 170)
    assert table[:2] == [922, 0xffffffff] and table[10] == 921
    actor_rows = []
    banks = {204, 915, 912}
    for i in range(17):
        row = table[10*i:10*i+10]
        assert 1400 <= row[8] <= 1600 and row[9]+1 == row[8]
        actor_rows.append(dict(fila=i, bancos_cmn=';'.join(str(v) for v in row[:8] if v not in (0, 0xffffffff)),
                               sonido=row[8], boca=row[9], original='ELF EU 474080 + 40*fila; consumidor 2F54C0'))
        banks.update(v for v in row[:8] if v not in (0, 0xffffffff))
    csv_write(OUT/'actores_elf.csv', actor_rows)
    selected = [r for r in inv if (Path(r['archivo']).name.upper() == 'DATA_CMN.AFS' and
                                  (884 <= r['entrada'] <= 954 or r['entrada'] in banks or r['entrada'] in (1092, 1093, 1127)))
                or (Path(r['archivo']).name.upper() == 'DATA_ENG.AFS' and
                    (5 <= r['entrada'] <= 9 or 132 <= r['entrada'] <= 227 or r['entrada'] in (333, 334)))]
    for r in selected:
        r['estado'] = 'traducido sin comprobar'
        r['alcance'] = 'inventario de banco candidato; uso de cada hoja por mision aun no certificado'
    csv_write(OUT/'recursos_candidatos.csv', selected)
    translated = {v for values in alias().values() for v in values}
    clips, events = [], []
    common = child(entry(game, cmn, 204), 0)
    for bank in sorted(banks):
        pack = entry(game, cmn, bank)
        bsk = child(pack, 3 if bank in (204, 915) else 0)
        if bsk[:4] != b'#BSK':
            continue
        for code in range(u32(bsk, 16)):
            off = u32(bsk, u32(bsk, 20)+4*code)
            if not off:
                continue
            index, storage = struct.unpack_from('<HH', bsk, off)
            amm = child(pack, 0) if bank == 915 else common if storage == 0 else child(pack, 1)
            duration = None
            if amm[:4] == b'#AMM' and index < u32(amm, 16):
                duration = u32(amm, u32(amm, 20)+16*index+8)
            clips.append(dict(banco=bank, codigo=hex(code), indice_amm=index, almacen=storage,
                              duracion=duration, selector_port=(bank, code) in translated,
                              estado='traducido sin comprobar' if (bank, code) in translated else 'falta',
                              alcance='codigo definido en banco original; uso por mision sin certificar'))
            # BSK: once palabras; AP7 es gestor 7 de las lineas de eventos.
            count, lines = struct.unpack_from('<II', bsk, off+40)
            assert count < 1024 and lines+count*8 <= len(bsk)
            for k in range(count):
                manager, n, data = struct.unpack_from('<HHI', bsk, lines+8*k)
                if manager != 7:
                    continue
                assert data+n*16 <= len(bsk)
                for j in range(n):
                    frame, ident, active, typ, value, queue = struct.unpack_from('<HBBIII', bsk, data+16*j)
                    events.append(dict(banco=bank, codigo=hex(code), cuadro=frame, id=ident,
                                       activo=active, tipo=typ, valor=value, cola=queue,
                                       selector_port=(bank, code) in translated))
    csv_write(OUT/'animaciones_bancos.csv', clips)
    csv_write(OUT/'eventos_originales.csv', events,
              ['banco', 'codigo', 'cuadro', 'id', 'activo', 'tipo', 'valor', 'cola', 'selector_port'])
    result = dict(salida=str(OUT), fuente=game.source, tabla_elf='0x474080', filas_actor=17,
                  bancos=sorted(banks), hojas_candidatas=len(selected),
                  tipos=dict(Counter(r['tipo'] for r in selected)), codigos=len(clips),
                  codigos_con_selector=sum(r['selector_port'] for r in clips),
                  eventos_por_tipo=dict(Counter(r['tipo'] for r in events)),
                  ap4_ap8_con_selector=dict(Counter(r['tipo'] for r in events if r['tipo'] in (4, 8) and r['selector_port'])),
                  incidencias=issues,
                  cobertura='PARCIAL: bancos del original, no cierre de consumidores/guiones ni prueba de modo entero')
    for name in ('actores_elf.csv', 'recursos_candidatos.csv', 'animaciones_bancos.csv', 'eventos_originales.csv'):
        result.setdefault('sha256', {})[name] = hashlib.sha256((OUT/name).read_bytes()).hexdigest()
    (OUT/'censo.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

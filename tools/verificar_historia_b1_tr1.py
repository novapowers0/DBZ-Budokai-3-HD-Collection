"""Verifica censo, origen de recursos, 403 cabeceras ADX y fronteras del audio."""
import argparse
from collections import Counter
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import historia_b1_tr1 as tr

CORTES = {(1923, 771), (1929, 803), (1952, 925), (1982, 1057), (1985, 1098), (1993, 1120)}


def require(value, message):
    if not value:
        raise ValueError(message)


def verify(args):
    tx, en, jp, elf, evidence, ids = tr.source(args.iso, args.opus, args.decomp)
    load = lambda p: json.loads((args.cache / p).read_bytes())
    idx = load('indice.json')
    req_sha = hashlib.sha256(args.decomp.read_bytes()).hexdigest()
    require(idx['elf_sha256'] == tr.ELF_SHA and idx['b1_c_sha256'] == req_sha and idx['iso_sha256'] == tr.ISO_SHA,
            'Huellas del índice no coinciden')
    require(idx['spx'] == ids[b'#SPX'] and set(map(int, idx['amb'])) == set(ids[b'#AMB']), 'Inventario incompleto')
    require({p.name for p in args.cache.glob('escena_*.json')} == {f'escena_{n}.json' for n in idx['spx']}, 'Escenas ausentes o extra')
    # Los JAL del ELF autentican los enlaces nativos usados en el argumento de audio.
    for site, target in [(0x15d4c0, 0x1369d0), (0x15d4d4, 0x137100), (0x217174, 0x1ea770), (0x1eaaf4, 0x1530a0)]:
        word, = struct.unpack('<I', tr.base.pal_elf_bytes(elf, site, 4))
        require(word == (3 << 26) | (target >> 2), f'JAL incorrecto en {site:#x}')
    for k, r in idx['builtins'].items():
        ev = evidence(int(k))
        require(ev['handler'] == r['handler'] and ev['b1_c_line'] == r['b1_c_line'] and
                (r['b1_c_line'] or r['ref_lectura'] == tr.C_REFS.get(int(k)) and r['ref_lectura']), f'Evidencia de builtin {k}')
    all_rows, calls, dynamic = [], 0, 0
    cuts, events, transition = set(), Counter(), set()
    for n in idx['spx']:
        s = load(f'escena_{n}.json')
        script = en.read(n)
        require(s['spx'] == n and s['spx_sha256'] == hashlib.sha256(script).hexdigest(), f'SPX {n} ajeno')
        sites, gaps = tr.census(tx.V, script)
        require(not gaps and not s['huecos'] and not s['diagnosticos'] and s['cierre'] == 'fin_escena', f'SPX {n} incompleto')
        require(dict(s['sitios']) == sites and all(str(i) in idx['builtins'] for i in sites.values()), f'Sitios SPX {n}')
        c = s['cobertura']
        require(c['estaticas'] == len(sites) == c['estaticas_traducidas'] and c['ejecutadas'] == c['ejecutadas_traducidas'], f'Cobertura {n}')
        calls += len(sites)
        dynamic += c['ejecutadas']
        seq = s['eventos']
        events.update(e['tipo'] for e in seq)
        end = [e for e in seq if e['tipo'] == 'fin_escena']
        require(len(end) == 1 and end[0]['selector'] == 10 and not end[0]['es_parada_stream'], f'Fundido {n}')
        rows = []
        for k, e in enumerate(seq):
            if e['tipo'] != 'voz':
                continue
            ent, ch = e['adx_jp'], e['canal']
            audio = tr.base.adx_info(jp.read(ent, 16))
            require(e['audio'] == audio, f'ADX {ent}: cabecera distinta')
            prep = next((x for x in reversed(seq[:k]) if x['tipo'] == 'preparar_stream' and x['canal'] == ch), None)
            require(prep and prep['adx_jp'] == ent and prep['builtin'] == 0x8a and e['builtin'] == 0x84, f'ADX {ent}: sin preparar')
            limit = next(x for x in seq[k+1:] if x['tipo'] == 'fin_escena' or
                         x['tipo'] in ('voz', 'parar_stream', 'preparar_stream') and x.get('canal') in (ch, -1))
            excess = Fraction(audio['samples'] * 60, audio['sample_rate']) + Fraction(str(e['t'])) - Fraction(str(limit['t']))
            row = s['voces_tr1'][len(rows)]
            status = 'dentro'
            if excess > 1:
                if limit['builtin'] == 0x8a and limit['adx_jp'] != ent:
                    status = 'cierre_handle_anterior'
                    cuts.add((n, ent))
                elif limit['builtin'] == 0xd3:
                    status = 'cola_transicion'
                    require(row['fin_nativo_confirmado'] is False, 'El fundido no prueba el stop nativo')
                    if excess - Fraction(tr.fade_ticks()*6, 5) > 1:
                        transition.add((n, ent))
                else:
                    raise ValueError(f'ADX {ent}: exceso no explicado')
            require((row['adx'], row['canal'], row['inicio'], row['limite_spx'], row['exceso_u'], row['estado']) ==
                    (ent, ch, e['t'], limit['t'], float(excess), status), f'ADX {ent}: ventana inconsistente')
            rows.append(row)
        require(rows == s['voces_tr1'], f'ADX extra en {n}')
        all_rows.extend(dict(spx=n, **row) for row in rows)
        if n == 1910:
            require(len(s['camaras']) == 14 and any(x['actor'] == 4 and x['raiz'] == 'SKY00' for x in s['cielos']), 'Testigo 1910 incompleto')
    require(calls == 29547 and dynamic == 794354 and len(idx['builtins']) == 79, 'Censo de corpus cambiado')
    require(len(all_rows) == 403 and cuts == CORTES and transition == {(1923, 776), (1982, 1063)}, 'Conteos de voces cambiados')
    counts = Counter(r['estado'] for r in all_rows)
    require(load('audio.json') == {'resumen': counts, 'voces': all_rows}, 'Audio agregado no coincide')
    require(counts == {'dentro': 381, 'cola_transicion': 16, 'cierre_handle_anterior': 6}, 'Reparto ADX cambiado')
    required = {'luz', 'luz_base', 'niebla', 'objeto_procedural', 'efecto_particulas', 'fundido_velo',
                'fundido_entrada', 'selector_nodo', 'animacion_amp'}
    require(required <= events.keys(), 'Faltan familias de eventos')
    banks = Counter()
    for n, rows in idx['amb'].items():
        data = en.read(int(n))
        require(len(rows) == len(tr.base.children(data)), f'AMB {n} incompleto')
        for r in rows:
            blob = data[r['off']:r['off']+r['tam']]
            require(len(blob) == r['tam'] and hashlib.sha256(blob).hexdigest() == r['sha256'], f'AMB {n}/{r["hijo"]} no coincide')
            if r['magic'] in ('#AML', '#AMP'):
                banks[r['magic']] += 1
                require(r['pistas'] and all('color' in clip if r['magic'] == '#AML' else 'selectores' in clip
                                            for clip in r['pistas']), 'Banco no traducido')
    require(banks == {'#AML':79, '#AMP':109}, 'Inventario de bancos cambió')
    print(f'38/38; {calls}/{calls} estáticas; {dynamic}/{dynamic} ejecutadas; 79 handlers; {dict(banks)}')
    print(f'403/403 ADX cotejados: {dict(counts)}; dos colas sobre fundido nominal: {sorted(transition)}')
    if args.estricto:
        print('BLOQ: no hay instante nativo de parada para las colas de transición.')
        return 1
    print('OK: cobertura y explicación estática reproducibles. No acredita 403 voces íntegras en B1.')
    return 0


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', required=True, type=Path)
    p.add_argument('--cache', required=True, type=Path)
    p.add_argument('--opus', type=Path, default=tr.FACTORY.parents[1] / 'estudio_b1/opus')
    p.add_argument('--decomp', type=Path, default=tr.FACTORY.parents[1] / 'ghidra/salida/b1.c')
    p.add_argument('--estricto', action='store_true')
    sys.exit(verify(p.parse_args()))

#!/usr/bin/env python3
"""Censo solAST: solo lectura de juegos/codigo; escribe metadatos, nunca arte.

Uso: DBZ3_AGENTE=solAST python tools/assets_censo.py
No interpreta una coincidencia numerica como consumidor. Los enlaces candidatos
se conservan separados de los cruces comprobados. No ejecuta el juego.
"""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import struct
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'studies 5/ESTUDIO_ASSETS'
sys.path.insert(0, str(ROOT / 'mod center hd'))
from iso import Iso, find_game, folder_elf  # lector local existente, sin instalar

FIELDS = ['modo', 'juego', 'archivo', 'entrada', 'hijo', 'id', 'tipo', 'bytes',
          'offset_archivo', 'cabecera_sha256', 'subrecursos_cabecera',
          'estado', 'original_funcion_linea', 'original_prueba',
          'port_fichero_linea', 'port_prueba', 'cruzado', 'impacto', 'arreglo',
          'limite', 'prioridad']
FUNC = re.compile(r'^// ==== ([0-9a-fA-F]+) (.+)$')


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def source_ref(path, needle):
    p = ROOT / path
    if not p.is_file():
        return '', 'NO COMPROBADO: fichero ausente'
    lines = p.read_text(encoding='utf-8-sig', errors='replace').splitlines()
    for n, text in enumerate(lines, 1):
        if needle in text:
            return f'{path}:{n}', text.strip()
    return '', f'NO COMPROBADO: ancla ausente {needle}'


class Game:
    def __init__(self, name, path):
        self.name, self.path = name, Path(path)
        self.iso = None if self.path.is_dir() else Iso(str(self.path))
        if self.iso:
            self.files = {p: sz for p, (_, sz) in self.iso.files().items()}
            self.elf = self.iso.read(self.iso.boot_elf())
        else:
            self.files = {str(p.relative_to(self.path)).replace('\\', '/'): p.stat().st_size
                          for p in self.path.rglob('*') if p.is_file()}
            self.elf = Path(folder_elf(str(self.path))).read_bytes()
        self.source = {'juego': name, 'ruta': str(self.path),
                       'elf_sha256': hashlib.sha256(self.elf).hexdigest(),
                       'archivos': len(self.files)}

    def open(self, name):
        return self.iso.open(name) if self.iso else (self.path / name).open('rb')

    def va(self, addr, count):
        b = self.elf
        if b[:5] != b'\x7fELF\x01':
            raise ValueError('ELF32 requerido')
        off, ents, n = u32(b, 28), struct.unpack_from('<H', b, 42)[0], struct.unpack_from('<H', b, 44)[0]
        for i in range(n):
            p = off + i * ents
            typ, fileoff, va, _, sz = struct.unpack_from('<5I', b, p)
            if typ == 1 and va <= addr and addr + count * 4 <= va + sz:
                return list(struct.unpack_from(f'<{count}I', b, fileoff + addr - va))
        raise ValueError(f'VA no respaldada por ELF: {addr:x}')


def inventory(game):
    rows, problems = [], []
    for name, size in sorted(game.files.items()):
        if not name.lower().endswith('.afs'):
            continue
        with game.open(name) as f:
            h = f.read(8)
            if len(h) != 8 or h[:3] != b'AFS':
                problems.append(f'{name}: sin AFS'); continue
            n = u32(h, 4)
            if n > 65536 or 8 + n * 8 > size:
                problems.append(f'{name}: tabla invalida'); continue
            table = f.read(n * 8)

            def visit(entry, child, off, length, depth=0):
                if off < 0 or length < 0 or off + length > size:
                    problems.append(f'{name}:{entry}/{child}: rango invalido'); return
                f.seek(off)
                head = f.read(min(length, 128))
                magic = head[:4]
                if magic == b'#AMB' and len(head) >= 0x20 and depth < 8:
                    nc, t = u32(head, 0x10), u32(head, 0x14)
                    if nc <= 65536 and t + 16 * nc <= length:
                        f.seek(off + t)
                        tb = f.read(nc * 16)
                        valid = all(u32(tb, k*16) + u32(tb, k*16+4) <= length for k in range(nc))
                        if valid and nc:
                            for k in range(nc):
                                visit(entry, f'{child}/{k}'.strip('/'), off + u32(tb,k*16), u32(tb,k*16+4), depth+1)
                            return
                    problems.append(f'{name}:{entry}/{child}: AMB non developpe')
                typ = magic.decode('ascii', errors='replace').replace('\x00', '')
                if not typ.startswith('#'):
                    if magic == b'\x0f\xf5\x12\xee': typ = 'LZX Xbox360 (contenido sin descomprimir)'
                    elif magic[:2] == b'\x80\x00': typ = 'ADX (signature candidate)'
                    elif magic == b'IECS': typ = 'IECS'
                    elif magic[:3] == b'\x00\x00\x01': typ = 'MPEG/SFD (signature candidate)'
                    else: typ = 'OPAQUE:' + magic.hex()
                counts = ''
                # Valeur d'entete seulement: ne pretend pas decoder les pistes.
                if magic[:4] in (b'#AMT', b'#AMM', b'#BSK', b'#AMC', b'#AMA') and len(head) >= 0x18:
                    counts = f'u32@10={u32(head,16)};u32@14={u32(head,20)} (brut)'
                rows.append(dict(juego=game.name, archivo=name, entrada=entry, hijo=child,
                                 id=f'{game.name}:{name}:{entry}' + (f'/{child}' if child else ''),
                                 tipo=typ, bytes=length, offset_archivo=off,
                                 cabecera_sha256=hashlib.sha256(head).hexdigest(), subrecursos_cabecera=counts))
            for k in range(n):
                off, length = struct.unpack_from('<II', table, k*8)
                visit(k, '', off, length)
    return rows, problems


def decomp(path):
    lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
    funcs, current = {}, None
    for n, text in enumerate(lines, 1):
        m = FUNC.match(text)
        if m:
            current = (m[1].lower().zfill(8), m[2])
            funcs[current[0]] = {'name': current[1], 'line': n, 'lines': []}
        elif current:
            funcs[current[0]]['lines'].append((n, text))
    return funcs


def write_csv(path, rows, fields=FIELDS):
    with path.open('w', encoding='utf-8-sig', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction='ignore')
        w.writeheader(); w.writerows(rows)


def main():
    os.environ.setdefault('DBZ3_AGENTE', 'solAST2')
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--games', default=os.environ.get('DBZ3_PS2_GAMES', str(ROOT / 'ps2_games')))
    ap.add_argument('--decomp', default=r'E:\DBZ3HD_trabajo\ghidra\salida')
    from assets_censo_iw import EU_DEFAULT
    ap.add_argument('--iw-eu', type=Path, default=EU_DEFAULT)
    ap.add_argument('--validate', action='store_true')
    ap.add_argument('--selftest', action='store_true', help='Prueba del lector con AFS/AMB sinteticos en memoria')
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if args.validate:
        return validate()
    OUT.mkdir(parents=True, exist_ok=True)
    games, inv, problems = {}, [], []
    for key in ('iw', 'b2', 'b1'):
        path = find_game(key, args.games, carpetas=True)
        if not path:
            problems.append(f'{key}: copia ausente'); continue
        g = games[key] = Game(key, path)
        rows, err = inventory(g)
        inv.extend(rows); problems.extend(err)
        print(f'{key}: {len(rows)} hojas fisicas AFS; incidencias {len(err)}')
    # HD es contenedor de recursos, no ELF PS2; no se descomprime ni modifica.
    hd = ROOT / 'us'
    if (hd / 'data_cmn.afs').exists():
        g = object.__new__(Game)
        g.name, g.path, g.iso = 'b3hd', hd, None
        g.files = {'data_cmn.afs':(hd/'data_cmn.afs').stat().st_size}
        g.source = {'juego':'b3hd','ruta':str(hd),'archivos':1,'elf_sha256':'NO APLICA: XEX'}
        games['b3hd'] = g
        rows, err = inventory(g)
        inv.extend(rows); problems.extend(err)
    dec = {g: decomp(Path(args.decomp) / fn) for g, fn in
           [('iw','iw_sles55347.c'), ('b2','b2.c'), ('b1','b1.c')]}
    loads=[]
    for game, funcs in dec.items():
        for addr, fn in funcs.items():
            for n,text in fn['lines']:
                if re.search(r'\b(?:RAMOpenDATA_CMNFolder|READ_AFSfile|RAMShootingMinigame_|RAMFlyingMinigamesRelated_|FUN_0012d100|FUN_00138240)\(',text):
                    loads.append({'juego':game,'funcion':f'0x{addr} {fn["name"]}',
                                  'linea_decomp':n,'llamada':text.strip(),
                                  'limite':'Referencia de carga; constantes no tipadas no son IDs de asset confirmados.'})
    write_csv(OUT/'ORIGINAL_CARGAS.csv',loads,['juego','funcion','linea_decomp','llamada','limite'])
    rows_by_mode = {}
    from assets_censo_reglas import RULES
    rules = RULES
    # Evidencia de tablas: solo B2/B1 PAL, misma serie que el decomp.
    tables = []
    for g, va, count in [('b2',0x377b60,3), ('b2',0x377af0,27), ('b2',0x377b70,36),
                         ('b2',0x377c00,18), ('b2',0x377c50,9), ('b2',0x377c80,7),
                         ('b2',0x3a4018,2), ('b2',0x378350,3), ('b2',0x3a3ee0,2),
                         ('b2',0x3a3ea8,2)]:
        if g in games:
            try:
                words = games[g].va(va,count)
                tables.append({'juego':g,'va':hex(va),'palabras':[hex(v) for v in words]})
            except ValueError as e:
                problems.append(str(e))
    for rule in rules:
        mode, game = rule['modo'], rule['juego']
        dest = rows_by_mode.setdefault(mode, {})
        pref, entries = rule.get('archivo', 'DATA_CMN.AFS').upper(), rule.get('entradas', [])
        refs = [source_ref(*ref) for ref in rule.get('port', [])]
        port_ref = '; '.join(a for a, b in refs if a)
        port_proof = ' | '.join(b for a, b in refs)
        orig_ref, orig_proof, confirmed = [], [], False
        for original in rule.get('original', []):
            addr = original['funcion'].lower().replace('0x','').zfill(8)
            fn = dec[game].get(addr)
            if not fn: continue
            path = Path(args.decomp) / {'iw':'iw_sles55347.c','b2':'b2.c','b1':'b1.c'}[game]
            orig_ref.append(f'{path}:{fn["line"]} {fn["name"]}')
            evidence = original.get('contiene')
            hits = [(n,s.strip()) for n,s in fn['lines'] if evidence and evidence in s]
            orig_proof.extend(f'{n}: {s}' for n,s in hits)
            # Confirmacion exige relacion de ID por literal o tabla ELF, especificada en la regla.
            if hits and original.get('verificado'):
                va = original.get('tabla')
                if va:
                    words = games[game].va(int(va,0), original['n'])
                    orig_proof.append(f'ELF {va}: ' + ','.join(hex(v) for v in words))
                    confirmed = all((i | (0x10000 if pref != 'DATA_CMN.AFS' else 0)) in words for i in entries)
                else:
                    confirmed = bool(original.get('literal_id'))
        for item in inv:
            if item['juego'] != game or Path(item['archivo']).name.upper() != pref or item['entrada'] not in entries:
                continue
            if 'hijos' in rule and item['hijo'] not in rule['hijos']: continue
            if 'tipo' in rule and item['tipo'] != rule['tipo']: continue
            row = {**item, 'modo':mode, 'estado':rule.get('estado','NO COMPROBADO'),
                   'original_funcion_linea':'; '.join(orig_ref), 'original_prueba':' | '.join(orig_proof),
                   'port_fichero_linea':port_ref, 'port_prueba':port_proof,
                   'cruzado':'si' if confirmed and port_ref and rule.get('port_lectura') else 'no',
                   'impacto':rule.get('impacto','No medido visualmente'), 'arreglo':rule.get('arreglo',''),
                   'prioridad':rule.get('prioridad','P3'),
                   'limite':rule.get('limite','Funcion localizada; ID original/consumo efectivo no demostrado')}
            if row['estado'] == 'bien' and row['cruzado'] != 'si':
                row['estado'] = 'NO COMPROBADO'
            dest[item['id']] = row
    from assets_censo_modos import modos_propios, hallazgos
    modos_propios(rows_by_mode, inv, source_ref, ROOT)
    from assets_censo_iw import augment, cross
    iw_context = augment(games, inv, dec, OUT, ROOT, source_ref, write_csv, inventory, Game, args.iw_eu)
    cross(rows_by_mode, iw_context, source_ref)
    from assets_censo_detalle import details
    details(iw_context, rows_by_mode, games, inv, dec, OUT, ROOT, source_ref, write_csv)
    findings = hallazgos(source_ref, dec, args.decomp)
    for finding,typ in [('AST07',4),('AST02',8)]:
        selected=[e for e in iw_context['events'] if e['tipo']==typ and e['selector_literal_en_port']=='si']
        row=next(r for r in findings if r['id']==finding)
        row['asset_o_dependencia']+='; '+ '; '.join(f'{e["asset"]} BSK{e["codigo_bsk"]} f{e["cuadro"]} valor{e["valor_iw"]}' for e in selected)
        row['limite']+=f' {len(selected)} registros presentes en selectores literales del port; NO ejecuciones observadas.'
    handle_events=[e for e in iw_context['events'] if e['tipo'] in (1,2)
                   and e['selector_literal_en_port']=='si' and 1000<=int(e['valor_iw'],16)<32768]
    iw_context['summary']['eventos_handle_seleccion_literal']=len(handle_events)
    if not handle_events:
        row=next(r for r in findings if r['id']=='AST09')
        row.update(estado='NO COMPROBADO',prioridad='P3',
                   impacto='El consumidor de audio tiene rutas pendientes de handle/transicion de forma, sin una solicitud seleccionada de handle demostrada en este censo.')
        row['limite']+=' Cero solicitudes de handle en los selectores literales censados; no se cuenta como sonido perdido observado.'
    write_csv(OUT/'HALLAZGOS.csv',findings,
              ['id','modo','prioridad','estado','asset_o_dependencia','port_fichero_linea',
               'port_prueba','original_funcion_linea','impacto','arreglo','limite'])
    all_mode_ids = {k for d in rows_by_mode.values() for k in d}
    assigned = all_mode_ids & {r['id'] for r in inv}
    unassigned = [{**r, 'modo':'SIN_ADSCRIPCION', 'estado':'NO COMPROBADO', 'cruzado':'no',
                   'limite':'Inventariado fisicamente; no se ha probado su uso en estos modos. No equivale a huerfano.'}
                  for r in inv if r['id'] not in assigned]
    write_csv(OUT/'INVENTARIO_ORIGINALES.csv', inv, FIELDS[1:11])
    write_csv(OUT/'CENSO_NO_ADSCRITOS.csv', unassigned)
    stats = {}
    for mode, d in rows_by_mode.items():
        rows = sorted(d.values(), key=lambda r:(r['archivo'],r['entrada'],r['hijo']))
        write_csv(OUT/f'CENSO_{mode}.csv', rows)
        crossed = sum(r['cruzado']=='si' for r in rows)
        na=sum(r['cruzado']=='no_aplica' for r in rows)
        stats[mode] = {'assets':len(rows),'cruzados':crossed,'original_no_aplica':na,
                       'porcentaje':round(100*crossed/(len(rows)-na),2) if len(rows)>na else None,
                       'estados':{s:sum(r['estado']==s for r in rows) for s in sorted({r['estado'] for r in rows})}}
        print(mode, stats[mode])
    manifest = {'fecha_utc':datetime.now(timezone.utc).isoformat(), 'agente':os.environ['DBZ3_AGENTE'],
                'fuentes':[g.source for g in games.values()]+[iw_context['eu'].source], 'hojas_fisicas':len(inv),
                'iw_eu_usa':iw_context['summary'],
                'adscritas_unicas':len(assigned),'no_adscritas':len(unassigned),
                'selectores_configuracion':len(all_mode_ids-assigned),
                'sitios_de_carga_original':len(loads),
                'modos':stats,'tablas_elf':tables,'incidencias':problems,
                'limites':['IW: IDs del censo son de la copia USA consumida por el port; evidencia original EU homologada por SHA256 completo de cada hoja.',
                           'No se ejecuta juego ni se confirma equivalencia visual.',
                           'Unidad: hoja AFS/#AMB; AMT/AMM contienen imagenes/clips no censados individualmente.',
                           'cabecera_sha256 cubre hasta128 bytes; HOMOLOGACION_IW.csv usa la hoja completa y conserva ambiguedades/diferencias.',
                           'No adscrito no significa sin uso; no se declaran assets huerfanos.',
                           'Censo parcial de consumidores; AFS de tres copias PS2 y DATA_CMN HD inventariados; un AMB opaco declarado.']}
    (OUT/'METRICAS.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # Identidad de las fuentes vivas: otros agentes pueden editarlas despues.
    refs={a.split(':')[0] for rule in rules for a,b in rule.get('port',[])}
    refs.update(['tools/assets_censo.py','tools/assets_censo_reglas.py','tools/assets_censo_modos.py',
                 'tools/assets_censo_iw.py','tools/assets_censo_mips.py','tools/assets_censo_explorar.py',
                 'tools/assets_censo_detalle.py','tools/assets_censo_efectos_hd.py',
                 'src/iw_mini_botones.cpp','src/iw_mini_vuelo.cpp','src/iw_mini_disparo.cpp','src/ingame/native_ui.cpp',
                 'src/anim_iw.cpp','src/iw_cuerpo_ps2.h','src/tactics/entrada.cpp','src/heroe.cpp'])
    sources=[]
    for p in sorted(refs):
        q=ROOT/p
        if q.is_file(): sources.append({'ruta':p,'bytes':q.stat().st_size,'sha256':hashlib.sha256(q.read_bytes()).hexdigest()})
    for fn in ('b1.c','b2.c','iw_sles55347.c'):
        q=Path(args.decomp)/fn
        sources.append({'ruta':str(q),'bytes':q.stat().st_size,'sha256':hashlib.sha256(q.read_bytes()).hexdigest()})
    write_csv(OUT/'FUENTES_CODIGO.csv',sources,['ruta','bytes','sha256'])
    report=['# Censo de assets — solAST2 — 31/10/2026',
            'Estudio PARCIAL de consumidores; inventario fisico reproducible. No se ha modificado el juego.',
            'Regla: modelos B3 HD + animacion del modo; original solo si falta personaje/forma en HD.',
            f'Inventario: {len(inv):,} hojas AFS/AMB; {len(assigned):,} IDs adscritos; {len(unassigned):,} sin adscripcion (no son huerfanos).',
            'Unidad = banco/hoja; las imagenes AMT, clips AMM y operaciones SPX no estan cruzados uno a uno.',
            '## Cobertura original → port (lectura/carga; NO prueba visual)',
            '| Modo | Filas | Cruzadas | % |', '|---|---:|---:|---:|']
    for mode,s in stats.items():
        pct='N/A: modo propio' if s['porcentaje'] is None else str(s['porcentaje'])
        report.append(f'| {mode} | {s["assets"]} | {s["cruzados"]} | {pct} |')
    report.extend(['## Prioridad para el jugador (detalle y pruebas en HALLAZGOS.csv)'])
    report.extend(f'- {r["prioridad"]} {r["id"]}: {r["impacto"]} Arreglo: {r["arreglo"]}' for r in sorted(findings,key=lambda r:(r['prioridad'],r['id'])))
    report.extend(['## Limites y reproduccion',
                   'IW: ELF/AFS EU SLES_553.47; puente a USA por SHA256 completo en HOMOLOGACION_IW.csv; argumentos recuperados de MIPS, no inventados del decomp.',
                   'B2/B1: copias PAL; cruces estrictos por literal o tabla ELF. bien significa conectado, no fidelidad visual aprobada.',
                   'Tactics/Heroe son modos propios: no existe consumidor original equivalente en estos tres decomps; N/A no es FALTA.',
                   'IW_POR_MINIJUEGO.csv cubre18 fichas de la guia; EVENTOS_IW_BSK.csv y TRAZAS_IW_POSES.csv separan presencia, seleccion y observacion historica.',
                   'Pendiente: uso por funcion de los no adscritos; clips/imagenes/AP/camaras; audio y fuentes por evento; paquetes externos UT/XV2.',
                   'Incidencia fisica: B2 DATA_CMN 892/0 es AMB no desarrollado; conservado como opaco, no ocultado.',
                   '`DBZ3_AGENTE=solAST2 PYTHONDONTWRITEBYTECODE=1 python tools/assets_censo.py` regenera CSV/metricas/informe.',
                   '`DBZ3_AGENTE=solAST2 PYTHONDONTWRITEBYTECODE=1 python tools/assets_censo.py --validate` valida entregables.',
                   'No se compila ni se ejecuta todo: encargo de estudio. Validacion CSV ejecutada; sin partida ni comparacion visual.',
                   'FUENTES_CODIGO.csv fija hashes del estado observado; las lineas pueden moverse por trabajo concurrente.',
                   'EFECTOS_B3HD_TACTICS.csv: siete DDS verificadas por hash/hoja; fx_rayo504/33 y fx_brillo553/36 declaradas pero PintarFx no las pide.',
                   '`DBZ3_AGENTE=solAST2 PYTHONDONTWRITEBYTECODE=1 python tools/assets_censo_efectos_hd.py` regenera el detalle HD (temporales D:/tmp/solAST2).'])
    assert len(report)<=40, len(report)
    (OUT/'INFORME.md').write_text('\n'.join(report)+'\n',encoding='utf-8')
    return validate()


def validate():
    count = 0
    for path in sorted(OUT.glob('CENSO_*.csv')):
        with path.open(encoding='utf-8-sig',newline='') as f:
            reader=csv.DictReader(f)
            assert reader.fieldnames == FIELDS, (path,'columnas')
            rows=list(reader)
        ids=[r['id'] for r in rows]
        assert len(ids)==len(set(ids)), (path,'IDs duplicados')
        for r in rows:
            assert r['id'] and r['tipo'] and r['estado'], (path,'vacio')
            assert int(r['bytes'])>=0 and int(r['offset_archivo'])>=0
            if r['cruzado']=='si':
                assert r['original_funcion_linea'] and r['original_prueba'] and r['port_fichero_linea']
            if r['estado'] in ('FALTA','mal entendido'):
                assert r['impacto'] and r['arreglo']
        count+=len(rows)
    assert count, 'No hay filas'
    with (OUT/'INVENTARIO_ORIGINALES.csv').open(encoding='utf-8-sig',newline='') as f:
        physical=list(csv.DictReader(f))
    ids={r['id'] for r in physical}
    assert len(ids)==len(physical), 'Inventario duplicado'
    m=json.loads((OUT/'METRICAS.json').read_text(encoding='utf-8'))
    assert m['hojas_fisicas']==len(ids)
    assert m['adscritas_unicas']+m['no_adscritas']==len(ids), 'Reconciliacion inventario'
    assert len((OUT/'INFORME.md').read_text(encoding='utf-8').splitlines())<=40
    with (OUT/'HALLAZGOS.csv').open(encoding='utf-8-sig',newline='') as f:
        findings=list(csv.DictReader(f))
    assert findings
    for r in findings:
        assert r['port_fichero_linea'] and r['port_prueba'], ('Hallazgo sin ancla',r['id'])
        assert r['impacto'] and r['arreglo'] and r['limite']
    # Verificacion independiente de los nuevos entregables: no basta con que el
    # generador afirme haber cruzado una hoja.
    def csvrows(name):
        with (OUT/name).open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
    hom=csvrows('HOMOLOGACION_IW.csv')
    assert len(hom)==m['iw_eu_usa']['hojas_eu_comparadas']
    assert len({r['original_id'] for r in hom})==len(hom)
    equivalent={r['port_id']:r for r in hom if r['estado']=='IDENTICO'}
    for mode in ('IW_MINIJUEGOS','IW_CAMPANA'):
        rows=csvrows('CENSO_'+mode+'.csv')
        assert len(rows)==m['modos'][mode]['assets']
        assert sum(r['cruzado']=='si' for r in rows)==m['modos'][mode]['cruzados']
        for r in rows:
            if r['cruzado']=='si':
                assert r['id'] in equivalent,(r['id'],'sin homologacion integral')
                assert equivalent[r['id']]['sha256_completo'] in r['original_prueba']
    mini=csvrows('IW_POR_MINIJUEGO.csv')
    assert len(mini)==18 and len({r['clave'] for r in mini})==18
    events=csvrows('EVENTOS_IW_BSK.csv')
    assert len(events)==m['iw_eu_usa']['eventos_bsk']
    for path in (ROOT/'tools').glob('assets_censo*.py'):
        compile(path.read_bytes(),str(path),'exec')
    hd=OUT/'EFECTOS_B3HD_TACTICS.csv'
    if hd.exists():
        pieces=csvrows(hd.name)
        assert len(pieces)==7 and len({r['nombre'] for r in pieces})==7
        assert all(int(r['ancho'])>0 and int(r['alto'])>0 and len(r['sha256_bitmap'])==64 for r in pieces)
    print(f'VALIDACION solAST2 OK: {len(hom)} homologaciones integrales;18 minijuegos;{len(events)} eventos BSK; sintaxis de herramientas')
    print(f'VALIDACION OK: {count} filas CSV; esquema, IDs, rangos y evidencia minima')
    return 0


def selftest():
    def afs(blobs):
        table=bytearray(b'AFS\0'+struct.pack('<I',len(blobs))+bytes(8*len(blobs)))
        for i,b in enumerate(blobs):
            struct.pack_into('<II',table,8+8*i,len(table),len(b));table.extend(b)
        return bytes(table)
    def amb(blobs):
        b=bytearray(0x20+16*len(blobs));b[:4]=b'#AMB'
        struct.pack_into('<II',b,0x10,len(blobs),0x20)
        for i,k in enumerate(blobs):
            struct.pack_into('<II',b,0x20+16*i,len(b),len(k));b.extend(k)
        return bytes(b)
    class MemoryGame:
        name='sintetico'
        def __init__(self,raw):self.raw=raw;self.files={'TEST.AFS':len(raw)}
        def open(self,_):return io.BytesIO(self.raw)
    raw=afs([b'#AMT'+bytes(28),amb([b'#AMO'+bytes(28),amb([b'#AMC'+bytes(28)])])])
    rows,problems=inventory(MemoryGame(raw))
    assert not problems
    assert [(r['entrada'],r['hijo'],r['tipo']) for r in rows]==[(0,'','#AMT'),(1,'0','#AMO'),(1,'1/0','#AMC')]
    for r in rows:
        off,n=r['offset_archivo'],r['bytes']
        assert r['cabecera_sha256']==hashlib.sha256(raw[off:off+min(n,128)]).hexdigest()
    broken=bytearray(raw);struct.pack_into('<I',broken,8,len(raw)+1)
    _,problems=inventory(MemoryGame(broken))
    assert problems and 'rango invalido' in problems[0]
    empty,problems=inventory(MemoryGame(afs([b''])))
    assert len(empty)==1 and empty[0]['bytes']==0 and not problems
    print('SELFTEST OK: tabla AFS@8, AMB anidado, offsets/tamanos/hash, rango invalido y entrada vacia; sin archivos temporales')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

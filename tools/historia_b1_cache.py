"""Proyecta el censo PAL B1C2 a cache reproducible por el reproductor B1.

El censo se puede generar con historia_b1_escenas.py. Las rutas se derivan
de la tabla PAL y la VM, o se leen de historia_b1_pal.py --salida.
"""
import argparse
from collections import Counter
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from historia_b1_escenas import Afs, Iso, ROOT, children, iso_digest
from historia_b1_1910 import adx_info, census, toml_records


def need(ok, detail):
    if not ok:
        raise ValueError(detail)


def chapter_rows(known, chapters):
    rows = []
    starts = {c['spx'] for c in chapters}
    for c in chapters:
        cap = c['chapter']
        seen = set()

        def put(spx, after):
            if spx in known and spx not in seen:
                rows.append(dict(chapter=cap, spx=spx, after=after))
                seen.add(spx)

        for spx in c['route']['path']:
            put(spx, 0)
        after = 0
        for screen, entry in c['continuation_candidate']['steps']:
            if screen == 9:
                after = (entry if entry >= 0 else 30 - entry) + 1
            elif screen == 8:
                if entry in starts:
                    break
                put(entry, after)
    need(len(rows) == len(known) and {r['spx'] for r in rows} == known,
         'Rutas PAL no cubren 38 escenas sin duplicados')
    return rows


def voice_windows(events, read_adx, fin, fade):
    # Unidades PAL de 1/60 s, alineadas a 400 muestras a 24 kHz.
    # Un slot de stream no reproduce dos voces simultaneas. El fin D3 no
    # prueba un corte: como B1C1, dejamos sonar la ultima cola bajo el fundido.
    end_scene = round((fin + fade) * 400)
    rows = []
    for i, ev in enumerate(events):
        if ev['tipo'] != 'voz':
            continue
        entry, ch = ev['adx_jp'], ev['canal']
        header = read_adx(entry, 16)
        audio = adx_info(header)
        need(audio == ev['audio'] and audio['sample_rate'] == 24000 and header[7] == 1,
             f'Cabecera ADX {entry}')
        start = round(ev['t'] * 400)
        end = start + audio['samples']
        cancel = next((x for x in events[i+1:] if x['tipo'] in ('voz','preparar_stream','parar_stream')
                       and x.get('canal') in (ch,-1)), None)
        limit = round(cancel['t'] * 400) if cancel else end
        played = min(end, limit) - start
        need(played > 0, f'Ventana vacía ADX {entry}')
        end_scene = max(end_scene, start + played)
        rows.append(dict(entry=entry, channel=ch, start=start, samples=audio['samples'], played=played,
                         boundary=cancel['tipo'] if cancel else 'transicion', limit=limit if cancel else None))
    for row in rows:
        if row['limit'] is None:
            row['limit'] = end_scene
    return end_scene, rows


def generate(iso_path, source, out, routes, opus, hd_afs):
    iso = Iso(iso_path)
    boot = iso.boot_elf()
    need(boot and Path(boot).name.lower() == 'sles_512.33', 'Se requiere PAL SLES_512.33')
    before = iso_path.stat()
    en, jp = Afs(iso, 'DATA_EN.AFS'), Afs(iso, 'ADX_JP.AFS')
    index = json.loads((source/'indice.json').read_bytes())
    digest = iso_digest(iso_path)
    ids = {i for i in range(1909,1997) if en.read(i,4) == b'#SPX'}
    need(digest == index['iso_sha256'] and ids == set(index['spx']) and len(ids) == 38,
         'Censo no corresponde a esta ISO')
    chapter_table, _ = census(iso.read(boot), en.read)
    if routes:
        audited = json.loads(routes.read_bytes())
    else:
        from historia_b1_pal import audit
        audited = audit(iso_path, opus)
    chapters = audited['DATA_EN.AFS']['chapters']
    need([(c['chapter'],c['spx']) for c in chapter_table] == [(c['chapter'],c['spx']) for c in chapters],
         'Censo de capítulos distinto del ELF')
    layout = chapter_rows(ids, chapters)

    en_read = lru_cache(maxsize=12)(en.read)
    def child(amb, index):
        package = en_read(amb)
        h = children(package)[index]
        return package[h['off']:h['off']+h['tam']]

    if hd_afs:
        sys.path[:0] = [str(ROOT/'awo_tools'), str(ROOT/'tools/b3hd_conv')]
        import retarget as rt
    # Confirmados por P1/1910; identidad por pareja AFS, no por ranura.
    hd_pairs = {(1230,1231):264,(1770,1771):352,(1218,1219):264,(1147,1148):246,
                (2002,2003):360,(1766,1767):352,(1302,1303):327,(237,238):110}

    out.mkdir()
    total = 0
    for spx in sorted(ids):
        s = json.loads((source/f'escena_{spx}.json').read_bytes())
        need(s['spx'] == spx and s['iso_sha256'] == digest and s['cierre'] == 'fin_escena'
             and s['spx_sha256'] == hashlib.sha256(en_read(spx)).hexdigest(), f'Guion {spx}')
        dest = out/f'escena_{spx}'
        dest.mkdir()
        files = {}

        def asset(data, ext):
            key = (hashlib.sha256(data).digest(), ext)
            if key not in files:
                files[key] = f'r{len(files)}.{ext}'
                (dest/files[key]).write_bytes(data)
            return files[key]

        actors, clips, evout, subs = [], [], [], []
        state, variants, clip_ids, sub_ids = {}, {}, {}, {}
        omitted = Counter()
        targets = {}

        def variant(slot):
            refs = state.get(slot, {})
            if 1 not in refs:
                return None
            model, texture = refs[1], refs.get(2)
            key = (slot, model, texture)
            if key in variants:
                return variants[key]
            get = lambda r: en_read(r[0]) if len(r)==1 else child(*r)
            actor = dict(slot=slot, variant=len(actors), model=asset(get(model),'amo'),
                         texture=asset(get(texture),'amt') if texture else None)
            actors.append(actor)
            variants[key] = actor['variant']
            if hd_afs and len(model)==1 and texture and len(texture)==1 and (model[0],texture[0]) in hd_pairs:
                targets[actor['variant']] = hd_pairs[model[0],texture[0]]
            return actor['variant']

        events = s['eventos']
        fin, = [e for e in events if e['tipo']=='fin_escena']
        fade = max(1, fin['fundido'])
        for e in events:
            kind, t = e['tipo'], e['t']
            if kind == 'carga_amb':
                need(e['entrada'] is not None, f'AMB por ruta sin resolver: {spx}')
                for h in children(en_read(e['entrada'])):
                    if h['tipo'] in (1,2) and h['sub']==0:
                        state.setdefault(h['ranura'],{})[h['tipo']] = (e['entrada'],h['hijo'])
            elif kind in ('carga_modelo','carga_textura'):
                need(e['entrada'] is not None, f'Recurso por ruta sin resolver: {spx}')
                state.setdefault(e['actor'],{})[1 if kind=='carga_modelo' else 2] = (e['entrada'],)
            elif kind in ('actor_dibujo','actor_dibujo_b','liberar_actor','animacion'):
                slot = e['actor']
                v = variant(slot) if kind != 'liberar_actor' else None
                if kind != 'liberar_actor' and v is None:
                    omitted[kind+'_sin_modelo'] += 1
                    continue
                row = dict(tipo=kind, t=t, actor=slot, variant=v, activo=e.get('activo'))
                if kind=='animacion':
                    key = (v,e['amb'],e['hijo'],e['clip'])
                    if key not in clip_ids:
                        clip_ids[key] = len(clips)
                        clips.append(dict(id=len(clips), variant=v, source_clip=e['clip'], frames=e['largo'],
                                          file=asset(child(e['amb'],e['hijo']),'amm')))
                    row.update(clip=clip_ids[key], vueltas=e['vueltas'])
                evout.append(row)
            elif kind in ('texto_imagen','texto_visible','texto_posicion') and e['sprite']==0:
                row = dict(tipo=kind,t=t,activo=e.get('activo'),x=e.get('x'),y=e.get('y'))
                if kind == 'texto_imagen':
                    if e['amb'] is None:
                        omitted['texto_sin_amb'] += 1
                        continue
                    key = (e['amb'],e['hijo'],e['imagen'])
                    if key not in sub_ids:
                        sub_ids[key] = len(subs)
                        subs.append(dict(amb=key[0], child=key[1], image=key[2]))
                    row['imagen'] = sub_ids[key]
                evout.append(row)
        shots = []
        for c in s['camaras']:
            if c['canal'] != 0:
                omitted['camara_auxiliar'] += 1
                continue
            row = {k:c[k] for k in ('t','frames','cam_ojo','cam_obj','cam_fov','cam_roll')}
            # El cargador recibe el ultimo corte principal cuando comparten tiempo.
            if shots and shots[-1]['t']==row['t']:
                shots[-1] = row
            else:
                shots.append(row)

        if targets:
            export_targets(dest, actors, clips, targets, hd_afs, rt)
        end_sample, voices = voice_windows(events, jp.read, fin['t'], fade)
        total += len(voices)
        (dest/'escena.toml').write_text(toml_records(dict(schema=2,spx=spx,iso_size=before.st_size,
            iso_sha256=digest,units_per_second=60,duration_units=fin['t'],fade=fade,end_sample=end_sample,
            subtitulos=[] if not subs else None),dict(actores=actors,clips=clips,eventos=evout,
            camaras=shots,voces=voices,subtitulos=subs)),encoding='utf-8')
        print(spx, len(actors), len(clips), len(shots), len(voices), dict(omitted), flush=True)

    after = iso_path.stat()
    need(total == 403 and (before.st_size,before.st_mtime_ns)==(after.st_size,after.st_mtime_ns),
         'Voces incompletas o cambió la ISO')
    (out/'capitulos.toml').write_text(toml_records(dict(schema=2,iso_size=before.st_size,iso_sha256=digest),
                                         dict(escenas=layout)),encoding='utf-8')


def export_targets(dest, actors, clips, targets, hd_afs, rt):
    # Contrato B1M1 de historia_b1_1910.py, con variantes en vez de ranuras fijas.
    from awg_vertex_buffer import qmat
    import afs_pair as afs
    sys.path.insert(0,str(ROOT/'mod center hd/cac'))
    import vista_b2 as view
    import subprocess

    decoded = {}
    for v, bin_id in targets.items():
        if bin_id not in decoded:
            raw = afs.entry(str(hd_afs),bin_id)
            if raw.startswith(afs.LZX_MAGIC):
                packed, unpacked = dest/f'hd{bin_id}.lzx', dest/f'hd{bin_id}.amb'
                packed.write_bytes(raw)
                subprocess.run([afs.XBDEC,str(packed),str(unpacked)],check=True,capture_output=True,timeout=30)
                raw = unpacked.read_bytes()
            dst = rt.esqueleto(raw)
            mesh = raw
            if raw.startswith(b'#AMB'):
                awo, = struct.unpack_from('>I',raw,0x20)
                if awo > 0x40:
                    mesh = raw[awo-0x40:]
            model = view.vh.mr.Model(mesh)
            need(tuple(model.parent)==tuple(b[3] for b in dst) and
                 [n.lstrip('X') for n in model.labels]==[b[0].lstrip('X') for b in dst], f'Orden HMS3 {bin_id}')
            skin = f'sk_{bin_id}.sk'
            view.exportar_sk(model,str(dest/skin))
            decoded[bin_id] = dst, skin
        dst, skin = decoded[bin_id]
        actor = actors[v]
        src = rt.esqueleto((dest/actor['model']).read_bytes())
        def suffix(sk):
            return [(name.partition('_')[2] or name,*rest) for name,*rest in sk]
        mapping,_ = rt.mapa_auto(suffix(src),suffix(dst))
        waist = next((j for j in mapping if dst[j][0].endswith('_WAIST')),None)
        need(waist is not None and any(dst[j][0].endswith('_HEAD') for j in mapping), f'Pareja HD {bin_id}')
        actor.update(hd_skin=skin)
        base = rt.locales(dst,{},0,False)
        for c in clips:
            if c['variant'] != v:
                continue
            anim,nf = rt.leer_anim((dest/c['file']).read_bytes(),c['source_clip'])
            need(nf==c['frames'], 'Duración retarget HD')
            frames,_ = rt.retarget(src,anim,nf,dst,base,mapping,raiz=waist)
            data = bytearray(struct.pack('<4sII',b'B1M1',nf,len(dst)))
            for frame in frames:
                for q,p in frame:
                    values = tuple(f for row in qmat(*q,*p)[:3] for f in row)
                    need(all(map(math.isfinite,values)), 'B1M1 no finita')
                    data.extend(struct.pack('<12f',*values))
            c['matrices'] = f'm{c["id"]}.mat'
            (dest/c['matrices']).write_bytes(data)


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('iso','censo','salida'):
        parser.add_argument('--'+option,required=True,type=Path)
    for option in ('rutas','opus','hd-afs'):
        parser.add_argument('--'+option,type=Path)
    args = parser.parse_args()
    dest = args.salida.resolve()
    if dest.exists() or dest.is_relative_to(ROOT) or not dest.parent.is_dir():
        parser.error('--salida: directorio nuevo fuera del proyecto, con padre existente')
    if not (args.rutas or args.opus):
        parser.error('Indica --rutas o --opus')
    generate(args.iso.resolve(),args.censo.resolve(),dest,args.rutas,args.opus,args.hd_afs)

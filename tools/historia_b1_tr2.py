"""Inventario y verificador offline de recursos del recorrido PAL EN (TR2)."""
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
WORK = Path('E:/DBZ3HD_trabajo/fabrica')
OUT = WORK / 'TR2'
sys.path[:0] = [str(ROOT / p) for p in ('tools', 'awo_tools', 'tools/b3hd_conv')]
from historia_b1_escenas import Afs, Iso, children, iso_digest
import afs_pair
import amt_ps2
import retarget as rt

U = lambda b, o: struct.unpack_from('<I', b, o)[0]
SHA = lambda b: hashlib.sha256(b).hexdigest()
# Autorizados por P1/1910, conservando identidad por modelo + textura.
PAIRS = {(1230,1231):264, (1770,1771):352, (1218,1219):264, (1147,1148):246,
         (2002,2003):360, (1766,1767):352, (1302,1303):327, (237,238):110}
LIMIT = 0.10  # desplazamiento residual máximo / altura del esqueleto B1
MAX_SAMPLES = 33


def need(ok, why):
    if not ok:
        raise ValueError(why)


def inventory(read, scenes, source, digest):
    @lru_cache(None)
    def amb(entry):
        return children(read(entry))

    models, uses, unresolved = {}, [], []
    for scene in scenes:
        data = json.loads((source / f'escena_{scene}.json').read_bytes())
        need((data['spx'], data['iso_sha256'], data['spx_sha256'], data['cierre']) ==
             (scene, digest, SHA(read(scene)), 'fin_escena'), f'censo SPX {scene}')
        state = defaultdict(dict)
        for seq, e in enumerate(data['eventos']):
            kind = e['tipo']
            slot = e.get('actor')
            if kind == 'carga_amb':
                for h in amb(e['entrada']):
                    if h['tipo'] in (1, 2) and h['sub'] == 0:
                        state[h['ranura']][h['tipo']] = (e['entrada'], h['hijo'])
            elif kind in ('carga_modelo', 'carga_textura'):
                state[slot][1 if kind == 'carga_modelo' else 2] = (e['entrada'],)
            elif kind in ('actor_dibujo', 'actor_dibujo_b', 'animacion', 'animacion_amp'):
                model, tex = state[slot].get(1), state[slot].get(2)
                if model is None:
                    unresolved.append(dict(scene=scene, slot=slot, seq=seq, kind=kind, time=e['t'], active=e.get('activo')))
                    continue
                key = (scene, slot, model, tex or ())
                models.setdefault(key, dict(scene=scene, slot=slot, model=model, texture=tex or (), first_seq=seq))
                if kind.startswith('animacion'):
                    bank = (e['amb'], e['hijo'])
                    uses.append(dict(scene=scene, slot=slot, seq=seq, time=e['t'],
                                     model=model, texture=tex or (), kind=kind, bank=bank,
                                     clip=e['clip'], frames=e['largo'], loops=e['vueltas']))
            elif kind == 'liberar_actor':
                # En B1 el objeto se libera, pero las ligaduras de recursos AMB siguen disponibles.
                continue
        need(any(k[0] == scene for k in models), f'escena sin recursos: {scene}')
    return models, uses, unresolved, amb


def parts(b):
    """Partes del AMG, mismo recorrido e índices que LeerAmo (sin tope silencioso)."""
    need(b[:4] == b'#AMO', 'modelo no AMO')
    for variant in range(U(b, 0x18)):
        a = U(b, U(b, 0x1c) + 4 * variant)
        need(b[a:a+4] == b'#AMG', 'modelo sin AMG')
        part = 0
        for bone in range(U(b, a+0x10)):
            axis = a + U(b, a+0x14) + 80*bone
            arm = U(b, axis+0x34)
            group = U(b, a+arm+4) if arm else 0
            if not group:
                continue
            g = a + group
            for k in range(U(b, g)):
                off = g + U(b, g+U(b,g+4)+4*k)
                vt, flags, tex, ramp = struct.unpack_from('<4I', b, off)
                dma = U(b, off+0x90)
                if vt & 0xffff == 0xffff or not 0x60000000 <= dma < 0x70000000:
                    continue
                end = off + 0xa0 + 16*(dma-0x60000000)
                need(end <= len(b), f'DMA fuera de AMO en {off:#x}')
                rgba = struct.unpack_from('<4f', b, off+0x30)
                need(all(map(math.isfinite, rgba)), 'RGBA no finito')
                gif = [f'{v:016x}' for v in struct.unpack_from('<8Q', b, off+0x40)]
                yield dict(variant=variant, part=part, bone=bone, offset=off,
                           vtype=vt, flags_word=flags, texture_slot=tex if vt & 4 and tex != 0xffffffff else -1,
                           ramp=ramp, rgba=rgba, gif_qwords_40_7f=gif)
                part += 1


@lru_cache(12)
def textures(b):
    need(b[:4] == b'#AMT', 'textura no AMT')
    import numpy as np
    out = {}
    for slot, t in enumerate(amt_ps2.entries(b, keep_empty=True)):
        if t is None:
            continue
        out[slot] = dict(psm=t['psm'])
        if t['psm'] == 0:  # PSMCT32, igual que Textura32Amt (pasarela_iw.cpp).
            o, n = t['data'] + amt_ps2.GIF_HDR, t['w'] * t['h'] * 4
            if not 0 < n <= t['dsize'] or o+n > len(b):
                continue
            alpha = np.frombuffer(b, dtype=np.uint8, count=n, offset=o)[3::4].astype(np.uint16)*2
            alpha = np.minimum(alpha, 255)
        elif t['psm'] in (0x13, 0x14):
            alpha = amt_ps2.decode(b,t)[:,:,3].ravel()
        else:
            continue
        alphas = np.flatnonzero(np.bincount(alpha, minlength=256)).tolist()
        out[slot].update(alpha_min=alphas[0], alpha_max=alphas[-1], alpha_values=alphas)
    return out


def amp(b, clip, frames):
    need(b[:4] == b'#AMP' and 0 <= clip < U(b,16), 'selector AMP')
    c = U(b,20)+16*clip
    need(U(b,c+8) == frames, 'duración AMP')
    table, names = U(b,c+12), U(b,28)
    tracks = []
    for i in range(U(b,24)):
        p = U(b,table+4*i)
        if not p:
            continue
        name = b[names+32*i:names+32*(i+1)].split(b'\0',1)[0].decode('latin1')
        keys = list(struct.iter_unpack('<hh', memoryview(b)[p+12:p+12+4*U(b,p+8)]))
        need(len(keys) == U(b,p+8) and all(0 <= f < frames for f,_ in keys)
             and all(a[0] <= z[0] for a,z in zip(keys,keys[1:])), 'claves AMP inválidas')
        if keys:
            tracks.append(dict(name=name, keys=keys))
    return tracks


def suffix(name):
    # Quitar solo el prefijo de personaje, no el de una cadena o parte de escenario.
    return name.partition('_')[2] or name


def bonemap(src, dst):
    m, why = rt.mapa_auto(*[[(suffix(n), *r) for n,*r in sk] for sk in (src, dst)])
    # No aceptar etiquetas ambiguas, reutilización de origen ni eslabones adivinados.
    counts = [Counter(suffix(n) for n,*_ in sk) for sk in (src,dst)]
    m = {j:i for j,i in m.items() if why[j] in ('nombre','sinonimo') and
         counts[0][suffix(src[i][0])] == counts[1][suffix(dst[j][0])] == 1}
    hits = Counter(m.values())
    return {j:i for j,i in m.items() if hits[i] == 1}, why


def measure(src, dst, mapping, anim, nf):
    base = rt.locales(dst, {}, 0, False)
    waist = next((j for j in mapping if suffix(dst[j][0]) == 'WAIST'), None)
    need(waist is not None and any(suffix(dst[j][0]) == 'HEAD' for j in mapping), 'HD sin cadera/cabeza mapeadas')
    s0 = rt.mundo(src, rt.locales(src, {}, 0, False))
    d0 = rt.mundo(dst, base)
    height = max(p[1] for _,p in s0) - min(p[1] for _,p in s0)
    need(height > 0 and nf > 0, 'altura/duración degenerada')
    num = min(MAX_SAMPLES, nf)
    times = [i*(nf-1)//max(num-1,1) for i in range(num)]
    err = (0., 0, next(iter(mapping)))
    indices = {k: tuple([key[0] for key in track] for track in pair) for k,pair in anim.items() if any(pair)}
    for f in times:
        # Conservar los dos extremos del segmento interpolado y llevar su reloj
        # al cuadro 0. retarget.py conserva su interpolación y los giros u16.
        clipped = {}
        for k, inds in indices.items():
            tracks = []
            for track, ts in zip(anim[k], inds):
                j = max(bisect_right(ts,f)-1,0)
                tracks.append([(t-f,*v) for t,*v in track[j:j+2]])
            clipped[k] = tracks
        (loc,), _ = rt.retarget(src, clipped, 1, dst, base, mapping, raiz=waist)
        sw = rt.mundo(src, rt.locales(src, clipped, 0, False))
        dw = rt.mundo(dst, loc)
        for j, k in mapping.items():
            sp = tuple(a-b for a,b in zip(sw[k][1],s0[k][1]))
            dp = tuple(a-b for a,b in zip(dw[j][1],d0[j][1]))
            e = math.dist(sp,dp)/height
            need(math.isfinite(e), 'error no finito')
            if e > err[0]:
                err = (e, f, j)
    return (*err, num)


def toml_value(v):
    if isinstance(v, dict):
        return '{' + ', '.join(f'{json.dumps(k)} = {toml_value(x)}' for k,x in v.items() if x is not None) + '}'
    if isinstance(v, (list,tuple)):
        return '[' + ', '.join(map(toml_value,v)) + ']'
    return json.dumps(v, ensure_ascii=False, allow_nan=False)


def document(header, **groups):
    lines = [f'{k} = {toml_value(v)}' for k,v in header.items()]
    for table, rows in groups.items():
        for row in rows:
            lines.extend(('', f'[[{table}]]'))
            lines.extend(f'{k} = {toml_value(v)}' for k,v in row.items() if v is not None)
    body = '\n'.join(lines)+'\n'
    tomllib.loads(body)
    return body


def material_rows(b, tex):
    for p in parts(b):
        slot = p['texture_slot']
        p['texture_status'] = 'not_used' if slot < 0 else 'missing'
        if slot in tex:
            p.update(tex[slot])
            p['texture_status'] = 'decoded' if 'alpha_values' in p else 'unsupported'
        words = [int(x,16) for x in p['gif_qwords_40_7f']]
        # GIF packed A+D pairs stored by the AMO. Preserve the register value,
        # not an inferred blend mode from texel alpha; runtime can override it.
        for reg, name in ((0x42,'alpha_1'),(0x43,'alpha_2'),(0x47,'test_1'),(0x48,'test_2')):
            regs = [f'{v:016x}' for v,r in zip(words[::2],words[1::2]) if r == reg]
            if regs:
                p[name] = regs
                v = int(regs[-1],16)
                if name.startswith('alpha'):
                    p[name+'_equation_abcd_fix'] = [(v>>x)&3 for x in (0,2,4,6)] + [(v>>32)&255]
                else:
                    p[name+'_ate_atst_aref_afail_date_datm_zte_ztst'] = [
                        (v>>bit)&mask for bit,mask in ((0,1),(1,7),(4,255),(12,3),(14,1),(15,1),(16,1),(17,3))]
        yield p


def hd_reader(path):
    @lru_cache(None)
    def read(entry):
        b = afs_pair.entry(str(path), entry)
        digest = SHA(b)
        if b.startswith(afs_pair.LZX_MAGIC):
            d = OUT / 'lecturas_hd'
            d.mkdir(exist_ok=True)
            source, unpacked = d / f'{entry}_{digest}.lzx', d / f'{entry}_{digest}.amb'
            # Re-decode the verified source on every audit; preserve prior files.
            if not source.exists():
                with source.open('xb') as f:
                    f.write(b)
            need(SHA(source.read_bytes()) == digest, 'entrada HD de trabajo alterada')
            if unpacked.exists():
                from uuid import uuid4
                unpacked = d / f'{entry}_{digest}_{uuid4().hex}.amb'
            subprocess.run([afs_pair.XBDEC, str(source), str(unpacked)], check=True, capture_output=True, timeout=30)
            b = unpacked.read_bytes()
        need(b.startswith(b'#AMB'), f'entrada HD {entry} no AMB')
        return rt.esqueleto(b), digest
    return read


def build(asset, models, uses, hd_read, catalog):
    by_model = defaultdict(list)
    for u in uses:
        by_model[u['model'], u['texture']].append(u)
    fits, actor_assets, stage_assets, problems = [], [], [], []
    for idx,(m,t) in enumerate(sorted({(m,t) for _,_,m,t in models})):
        if idx % 50 == 0:
            print(f'Leyendo recursos {idx}...', flush=True)
        b, tb = asset(m), asset(t) if t else b''
        aid = '/'.join(map(str,m)) + ':' + '/'.join(map(str,t))
        src = rt.esqueleto(b)
        need(src and all(-1 <= par < i for i,(*_,par) in enumerate(src)), f'jerarquía B1 {aid}')
        root = src[0][0]
        row = dict(id=aid, source_model=m, source_texture=t, model_sha256=SHA(b), texture_sha256=SHA(tb), root=root)
        character = root.startswith('X') and root.endswith('_BODY')
        mat = list(material_rows(b, textures(tb) if tb else {}))
        needs_mat = any(p['texture_status'] not in ('decoded', 'not_used') for p in mat)
        row.update(parts=len(mat), material_refs_complete=bool(mat) and not needs_mat)
        if not character:
            s = dict(row, materials=mat, conversion='B1_AMO_AMT', blend_runtime='unverified')
            if root == 'PBKAM01':
                s.update(location='Kame House', hd_stage_candidate=56, hd_candidate_status='name_only_unselected')
            stage_assets.append(s)
        hd = PAIRS.get((m[0], t[0])) if len(m) == len(t) == 1 else None
        row.update(target='b1_original', hd_candidates=catalog.get(root, []),
                   target_reason='no_confirmed_exact_hd_variant' if character else 'scene_geometry_or_prop',
                   b1_bones=[dict(index=i, name=n, parent=p) for i,(n,_,_,p) in enumerate(src)],
                   b1_parts=[{k:p[k] for k in ('variant','part','bone','texture_slot')} for p in mat] if character else None)
        actor_assets.append(row)
        relevant = by_model[m,t]
        skeleton = defaultdict(list)
        for i,(name,*_) in enumerate(src):
            skeleton[name].append(i)
        measurements = {}
        if hd is not None:
            dst, sha = hd_read(hd)
            need(all(-1 <= par < i for i,(*_,par) in enumerate(dst)), f'jerarquía HD {hd}')
            mapping, why = bonemap(src, dst)
            row.update(hd_entry=hd, hd_sha256=sha,
                       hd_bones=[dict(index=j, name=n, parent=p, source_index=mapping.get(j,-1),
                                      rule=why[j] if j in mapping else 'keep_bind_local')
                                 for j,(n,_,_,p) in enumerate(dst)],
                       source_unmapped=sorted(set(range(len(src))) - set(mapping.values())))
        for u in relevant:
            if u['bank'][0] is None or u['bank'][1] is None or not u['frames']:
                problems.append(f"clip sin resolver: {u['scene']}/{u['slot']}/{u['seq']}")
                continue
            key = (u['kind'], u['bank'], u['clip'], u['frames'])
            if key not in measurements:
                bank = asset(u['bank'])
                value = dict(bank_sha256=SHA(bank))
                if u['kind'] == 'animacion_amp':
                    tr = amp(bank, u['clip'], u['frames'])
                    for track in tr:
                        track['source_indices'] = skeleton.get(track['name'], [])
                        track['selector_hide'] = -1
                    value['part_tracks'] = tr
                else:
                    need(bank[:4] == b'#AMM', 'banco no AMM')
                    anim, nf = rt.leer_anim(bank, u['clip'])
                    need(nf == u['frames'], 'duración AMM')
                    value['active_tracks_unmatched'] = sorted(n for n,tracks in anim.items() if any(tracks) and n not in skeleton)
                    if hd is not None:
                        err, f, j, samples = measure(src, dst, mapping, anim, nf)
                        value.update(max_error=err, worst_frame=f, worst_hd_bone=j, samples=samples)
                    else:
                        value['measurement'] = 'not_retargeted'
                measurements[key] = value
            fits.append(dict(u, asset=aid, **measurements[key]))
        measured = [v for v in measurements.values() if 'max_error' in v]
        if hd is not None:
            row['max_error'] = max((v['max_error'] for v in measured), default=None)
            # Until the mesh, costumes, and selectors have been fitted, retain the
            # original combination instead of feeding an unverified rig to the player.
            row['target_reason'] = 'hd_over_threshold' if row['max_error'] is not None and row['max_error'] >= LIMIT else 'hd_mesh_and_parts_unverified'
    return actor_assets, stage_assets, fits, problems


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', type=Path, default=ROOT / 'ps2_games/DragonBall Z - Budokai (Europe) (En,Fr,De,Es,It).iso')
    p.add_argument('--hd-afs', type=Path, default=ROOT / 'eu/data_cmn.afs')
    p.add_argument('--censo', type=Path, default=WORK / 'B1C2/cache_pal_en')
    p.add_argument('--salida', type=Path, default=OUT, help='directorio dentro de fabrica/TR2')
    p.add_argument('--sonda', action='store_true')
    p.add_argument('--usar-huella-censo', action='store_true',
                   help='reutiliza la huella B1C2, pero vuelve a leer y comprobar los SPX y recursos')
    mode = p.add_mutually_exclusive_group()
    mode.add_argument('--generar', action='store_true')
    mode.add_argument('--verificar-inventario', action='store_true', help='comprueba datos reproducibles; no certifica fidelidad')
    a = p.parse_args()
    out = a.salida.resolve()
    need(out.is_relative_to(OUT.resolve()), 'salida fuera de TR2')
    iso = Iso(a.iso)
    need(Path(iso.boot_elf()).name.lower() == 'sles_512.33', 'ISO no PAL SLES_512.33')
    afs = Afs(iso, 'DATA_EN.AFS')
    read = lru_cache(10)(afs.read)
    scenes = [i for i in range(1909, 1997) if read(i, 4) == b'#SPX']
    idx = json.loads((a.censo / 'indice.json').read_bytes())
    before = a.iso.stat()
    print('Censo rápido...' if a.sonda or a.usar_huella_censo else 'Verificando SHA-256 de la ISO...', flush=True)
    digest = idx['iso_sha256'] if a.sonda or a.usar_huella_censo else iso_digest(a.iso)
    need(len(scenes) == 38 and set(scenes) == set(idx['spx']) and digest == idx['iso_sha256'], 'ISO/censo incompatibles')
    models, uses, unresolved, amb = inventory(read, scenes, a.censo, digest)

    @lru_cache(18)
    def asset(ref):
        if len(ref) == 1:
            return read(ref[0])
        h = amb(ref[0])[ref[1]]
        off, _ = afs.entries[ref[0]]
        data = iso.read(afs.path, off + h['off'], h['tam'])
        need(len(data) == h['tam'], f'lectura corta de recurso {ref}')
        return data

    if a.sonda:
        for row in models.values():
            if row['scene']==1910 and row['slot'] in (0,10):
                print(row['slot'], [n for n,*_ in rt.esqueleto(asset(row['model']))])
                print(list(material_rows(asset(row['model']), {}))[:3])
        return 0
    catalog = defaultdict(list)
    for line in (ROOT/'mod center hd/catalog_b3.cat').read_text(encoding='utf-8-sig').splitlines():
        fields = line.split('|')
        if len(fields) >= 3 and fields[0].isdigit():
            catalog[fields[2]].append(int(fields[0]))
    ar, st, clips, problems = build(asset, models, uses, hd_reader(a.hd_afs), catalog)
    after = a.iso.stat()
    need((before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns), 'ISO modificada durante la lectura')
    ids = {(tuple(r['source_model']),tuple(r['source_texture'])):r['id'] for r in ar}
    per_actor = defaultdict(list)
    for c in clips:
        if 'max_error' in c:
            per_actor[c['scene'],c['slot'],c['asset']].append(c)
    for r in models.values():
        r['asset'] = ids[r['model'],r['texture']]
        measured = per_actor[r['scene'],r['slot'],r['asset']]
        if measured:
            worst_clip = max(measured, key=lambda c:c['max_error'])
            r.update(max_error=worst_clip['max_error'], worst_seq=worst_clip['seq'])
    stage_ids = {r['id'] for r in st}
    header = dict(schema=1, iso_sha256=digest, scenes=scenes, bone_error_limit=LIMIT, max_samples_per_clip=MAX_SAMPLES)
    outputs = {
        'mapa_actores.toml': document(header, assets=ar, actors=models.values(), clips=clips, unresolved=unresolved),
        'mapa_escenarios.toml': document(header, assets=st, instances=(r for r in models.values() if r['asset'] in stage_ids)),
    }
    if a.generar:
        out.mkdir(exist_ok=True)
        need(not any((out/name).exists() for name in outputs), 'ya existen mapas; elige una salida nueva')
    for name, text in outputs.items():
        dst = out/name
        if a.generar:
            with dst.open('x', encoding='utf-8', newline='\n') as f:
                f.write(text)
        else:
            need(dst.read_text(encoding='utf-8') == text, f'no coincide {name}')
    hd = [r for r in ar if 'hd_entry' in r]
    worst = max((r['max_error'] for r in hd if r['max_error'] is not None), default=math.inf)
    failures = [r for r in st if not r['material_refs_complete']]
    print(f'{len(scenes)} escenas; {len(models)} variantes; {len(ar)} assets; {len(st)} geometrías/atrezos; {len(clips)} clips ({sum(c["kind"]=="animacion_amp" for c in clips)} AMP); {len(unresolved)} actores sin resolver.')
    print(f'{len(hd)} parejas HD medidas, {sum("max_error" in c for c in clips)} usos HD; peor error {worst:.6f} (umbral < {LIMIT}); {len(failures)} geometrías con materiales incompletos.')
    for r in hd:
        print(r['id'], r['root'], r['hd_entry'], r['max_error'], len(r['hd_bones'])-sum(b['source_index']<0 for b in r['hd_bones']), len(r['hd_bones']))
    for r in failures[:20]:
        print('MATERIAL', r['id'], r['root'], Counter(p['texture_status'] for p in r['materials']))
    print('Clips sin resolver:', problems)
    if unresolved or problems:
        return 1
    if a.generar or a.verificar_inventario:
        print('Inventario reproducible; no certifica fidelidad.')
        return 0
    blockers = {
        'parejas HD sobre umbral': sum(r.get('max_error', -1) >= LIMIT for r in hd),
        'geometrías con materiales incompletos': len(failures),
        'geometrías con mezcla final sin validar': sum(r['blend_runtime'] != 'verified' for r in st),
        'recursos con candidato HD sin aprobar': sum(bool(r['hd_candidates']) and r['target'] != 'b3_hd' for r in ar),
    }
    print('TOML regenerados: coinciden. Criterio TR2:', blockers)
    return int(worst >= LIMIT or any(blockers.values()))


if __name__ == '__main__':
    sys.exit(main())

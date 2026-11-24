"""BL1: inventario explícito, costuras de normales, láminas y verificación offline."""
from collections import Counter, defaultdict
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tomllib

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(r'E:\DBZ3HD_trabajo')
HOME = WORK / 'fabrica' / 'BL1'
BLENDER = Path(r'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')
KIT = ROOT / 'mod center hd'
AFS = ROOT / 'us' / 'data_cmn.afs'
sys.dont_write_bytecode = True
sys.path[:0] = [str(KIT), str(ROOT / 'awo_tools')]
import afs_pair
import colores
import model_render as mr
import ps2hd
import roster_build as rb
import swap_b3
from awg_vertex_buffer import WINDOW
from skin_slots import SkinSlots


def sha(b):
    return hashlib.sha256(b).hexdigest()


def write_json(path, obj):
    with path.open('x', encoding='utf-8') as f:
        json.dump(obj, f, ensure_ascii=False, indent=1)


def require(ok, msg):
    if not ok:
        raise ValueError(msg)


def decompress(raw, folder):
    if raw[:4] != afs_pair.LZX_MAGIC:
        return raw
    folder.mkdir(exist_ok=True)
    key = sha(raw)
    src, dst = folder / (key + '.lzx'), folder / (key + '.dec')
    if not dst.is_file():
        src.write_bytes(raw)
        subprocess.run([afs_pair.XBDEC, str(src), str(dst)], check=True, capture_output=True)
    return dst.read_bytes()


def native(fid, folder):
    b = decompress(afs_pair.entry(str(AFS), fid), folder)
    require(b[:4] == b'#AMB' and b[64:68] == b'#AWO', f'AFS {fid}: no es modelo HD')
    return b


def port(p, folder, name, cfg):
    raw = p.read_bytes()
    b = decompress(raw, folder)
    require(b[:4] == b'#AMB', f'{p}: no es AMB')
    if b[12:16] == b'\x03\0\0\0':
        b = ps2hd.convert_block(b)
    require(b[12:16] == b'\0\0\0\x02' and b[64:68] == b'#AWO', f'{p}: no es AWO HD')
    return rb.b3_ramps(b, name, cfg), sha(raw)


def inventory():
    dlc = WORK / 'respaldos_r23/sol/DLCB1-20261122-012948/prueba-final/mods-confirmar'
    sample = Path(r'D:\DBZ3HD\s5_i1_eu\mods')
    nova = Path(r'D:\DBZ3HD\lim_prueba\mods')
    goku = WORK / 'infra/INFRA-05/salida/run29/mods'
    roots = [*sorted(dlc.glob('dlcb1_*/personaje.toml')),
             *sorted(sample.glob('vis_b2_*/personaje.toml')),
             *sorted(nova.glob('nova_*/personaje.toml')),
             *sorted(goku.glob('imp_b[12]_goku/personaje.toml'))]
    require(len(roots) >= 16, 'Faltan manifiestos representativos de DLC/B1/B2')
    for p in roots:
        c = tomllib.loads(p.read_text(encoding='utf-8-sig'))['personaje']
        if c.get('origen_fuente') not in ('b1', 'b2'):
            continue
        d = rb.IDS[c['donante']]
        per = int(c.get('modelos_por_traje', 1))
        for i, rel in enumerate(c['modelos']):
            form = i % per
            if p.parent.name in ('imp_b1_goku','imp_b2_goku') and form == 1:
                form = 2  # SSJ; forma 1 de Goku B3 es Kaioken
            j = min(i // per, d['costumes'] - 1)*d['models_per_costume'] + min(form, d['forms'] - 1)
            yield dict(manifest=str(p), name=p.parent.name, asset=rel, index=i+1,
                       origin=c['origen_fuente'], ref=d['models'][j], ref_name=d['name'], cfg=c)
    for p in sorted((KIT / 'campanas').glob('trajes_*/*/traje.toml')):
        c = tomllib.loads(p.read_text(encoding='utf-8-sig'))['traje']
        d = rb.IDS[c['personaje']]
        for i, rel in enumerate(c['modelos']):
            fid = int(rel.removeprefix('juego:'))
            yield dict(manifest=str(p), name=p.parent.name, asset=rel, index=i+1,
                       origin='seleccion HD' if 'tono' not in c else 'recolor HD',
                       ref=fid, ref_name=d['name'], cfg=c)
    gt = ('imp_iw_goku_gt','imp_iw_vegeta_gt','imp_iw_pan','imp_iw_baby')
    other = [*(sample / n / 'personaje.toml' for n in gt),
             *sorted(sample.glob('traje_*/traje.toml')),
             *sorted(Path(r'D:\DBZ3HD\smoke_b\mods').glob('traje_*/traje.toml')),
             WORK/'kit_14/a_orig/mods/traje_goku_armadura/traje.toml']
    for p in other:
        c = tomllib.loads(p.read_text(encoding='utf-8-sig')).get(p.stem, {})
        credits = p.with_name('CREDITOS.txt')
        reason = ('origen Infinite World (ports_oficiales.toml / traje enlazado)' if
                  p.parent.name in (*gt, 'traje_vis_iw_vegeta_saiyan') else
                  f'modelos comunitarios; {p.with_name("manifest.txt")}' if
                  p.parent.name=='traje_goku_armadura' else
                  f'arte de terceros o procedencia nativa no acreditada; {credits}'
                  if credits.is_file() else 'origen nativo no acreditado')
        cid = c.get('donante', c.get('personaje'))
        d = rb.IDS.get(cid, {})
        for i, rel in enumerate(c.get('modelos', [])):
            yield dict(manifest=str(p), name=p.parent.name, asset=rel, index=i+1,
                       ref=d.get('models', [None])[0], ref_name=d.get('name'), excluded=reason)


def models(b):
    k = 0
    while True:
        m = mr.Model(b, awg=k)
        yield m
        k += 1
        if k >= m.avb.n_awg:
            break


def groups(m):
    """Coincidencia exacta de posición y skin, un solo material y UV distinta.

    Sólo diferencias suaves [1°,20°]; no suelda posiciones ni suaviza cantos duros.
    Los grupos son disjuntos. Se excluyen normales nulas, marcadores especiales,
    reflejos y transparencia (su comportamiento no se deduce de la costura).
    """
    mts = defaultdict(set)
    for tri, mat in zip(m.tris, m.tri_mat):
        for i in tri:
            mts[int(i)].add(int(mat))
    bag = defaultdict(list)
    for i, ms in mts.items():
        if len(ms) != 1:
            continue
        mat = next(iter(ms))
        material = m.mats[mat]
        r = m.avb.raw_window(i)
        if (r[32:36] != b'\xff'*4 or material['blend'] or material['env'] or
                np.linalg.norm(m.nrm[i]) < .9 or not np.isfinite(m.nrm[i]).all()):
            continue
        bag[(*m.pos[i], r[12:20], int(m.vbone[i]), mat)].append(i)
    chosen = []
    for ids in bag.values():
        if len(ids) < 2 or np.ptp(m.uv[ids], axis=0).max() < 1e-5:
            continue
        if 1 <= angle(m.nrm[ids]) <= 20:
            chosen.append(sorted(ids))
    return chosen


def angle(n):
    nn = n / np.linalg.norm(n, axis=1, keepdims=True)
    return float(np.rad2deg(np.arccos(np.clip((nn @ nn.T).min(), -1, 1))))


def frames(m):
    return np.asarray(m.worlds())[m.vbone]


def scene_mesh(m, awg, ids):
    w = frames(m)
    n = np.einsum('nij,nj->ni', w[:, :3, :3], m.nrm)
    return dict(awg=awg, pos=(np.einsum('nij,nj->ni', w[:, :3, :3], m.pos)+w[:, :3, 3]).tolist(),
                nrm=n.tolist(), uv=m.uv.tolist(), tris=m.tris.reshape(-1, 3).tolist(), groups=ids)


def metrics(m, gg=None):
    if gg is None:
        gg = groups(m)
    idx = np.unique(m.tris)
    n = np.linalg.norm(m.nrm[idx], axis=1)
    v = m.avb.vertices()
    p, tx = defaultdict(set), set()
    for i in idx:
        p[tuple(m.pos[i]) + (m.vbone[i],)].add(tuple(m.uv[i]))
    for i in np.unique(m.tri_mat):
        t = m.mats[i]['tex']
        if t < len(m.tex):
            tx.add(t)
    slots = SkinSlots(m.avb)
    return dict(windows=len(v), triangles=len(m.tris), uv_split=sum(len(s)>1 for s in p.values()),
                zero_normals=int((n<1e-6).sum()), hidden_marker=sum(r['marker']==0xffffff00 for r in v),
                weights=Counter(str(round(r['w'],3)) for r in v), palette_slots=slots.n_slots,
                texture_sizes=Counter(f'{m.tex[t].shape[1]}x{m.tex[t].shape[0]}' for t in tx),
                textures=dict((str(t),dict(rgb=m.tex[t][...,:3].mean(axis=(0,1)).round(4).tolist(),
                                            alpha_ge_250=float((m.tex[t][...,3]>=250/255).mean())))
                              for t in sorted(tx)),
                seams=len(gg), angles=[round(angle(m.nrm[g]),5) for g in gg])


def integrate(before, scene, exported):
    require(sha(before) == scene['source_sha256'] == exported['source_sha256'], 'Fuente distinta')
    require(len(scene['meshes'])==len(exported['meshes']), 'Faltan AWG')
    out = bytearray(before)
    for m, spec, exp in zip(models(before), scene['meshes'], exported['meshes'], strict=True):
        require(spec['awg']==exp['awg'] and np.allclose(exp['pos'], spec['pos'], atol=1e-5, rtol=0),
                'Cambió posición u orden')
        require(m.avb.layout()=='ventana' and m.avb.vb0 >= m.avb.awg0, 'Layout no admitido')
        n = np.asarray(exp['nrm'])
        require(n.shape==m.nrm.shape and np.isfinite(n).all(), 'Exportación de normales inválida')
        for ids in spec['groups']:
            w = frames(m)[ids,:3,:3]
            local = np.linalg.solve(w, n[ids,:,None])[...,0]
            local /= np.linalg.norm(local,axis=1,keepdims=True)
            require(np.isfinite(local).all(), 'Normal degenerada')
            for i, norm in zip(ids,local):
                struct.pack_into('>3f',out,m.avb.vb0+i*WINDOW+20,*norm)
    return bytes(out)


def validate(before, after, scene):
    require(len(before)==len(after), 'Tamaño distinto')
    masked = bytearray(after)
    result = []
    for k, (old,new) in enumerate(zip(models(before), models(after), strict=True)):
        gg = scene['meshes'][k]['groups'] if scene else []
        require(np.array_equal(old.tris,new.tris) and np.array_equal(old.tri_mat,new.tri_mat), 'Draw distinto')
        require(all(0 <= v < new.avb.n or v==65535 for v in new.avb.indices()), 'Índice fuera del buffer')
        changes = []
        for g in gg:
            a,b = angle(old.nrm[g]),angle(new.nrm[g])
            require(a>=1 and b<.25 and b<a, f'Costura AWG{k} no mejorada: {a:.4f} -> {b:.4f}')
            changes.append((round(a,5),round(b,5)))
            for i in g:
                off = old.avb.vb0+i*WINDOW+20
                masked[off:off+12] = before[off:off+12]
        result.append(dict(awg=k, corrected_vertices=sum(len(g) for g in gg), angles=changes))
    require(bytes(masked)==before, 'Se alteró contenido fuera de normales seleccionadas')
    return result


def sheet(ms, titles, path):
    poses = [m.arms_down() for m in ms]
    result = []
    font = ImageFont.load_default(size=21)
    W,H = 432,500
    img = Image.new('RGB',(W*len(ms),H*2),(29,35,46))
    text = ImageDraw.Draw(img)
    pts = [m.posed(p)[0][np.unique(m.tris)] for m,p in zip(ms,poses)]
    for row, yaw in enumerate((-12,32)):
        view = mr.rot_y(yaw)
        allpos = np.concatenate(pts) @ view.T
        mid = (allpos.min(0)+allpos.max(0))/2 @ view
        scale = float(min((W-44)/np.ptp(allpos[:,0]),(H-72)/np.ptp(allpos[:,1])))
        for i,(m,p,title) in enumerate(zip(ms,poses,titles)):
            a = mr.render(m, size=(W,H-42), view=view, center=mid, scale=scale, pose=p, outline=2., ss=1)
            visible = a[...,3]>0
            dark = (a[...,:3]==0).all(axis=2)&visible
            require(visible.any(), f'Render vacío {path}')
            result.append(dict(yaw=yaw,column=title,pixels=int(visible.sum()),
                               black_fraction=float(dark.sum()/visible.sum()),
                               rgb_mean=a[visible,:3].mean(axis=0).round(2).tolist()))
            tile = Image.fromarray(a)
            img.paste(tile,(i*W,row*H+35),tile)
            text.text((i*W+14,row*H+10),title,font=font,fill=(243,245,251))
    img.resize((img.width//2,img.height//2),Image.Resampling.LANCZOS).save(path)
    return result


def blender(mode, plan, out, log):
    out.mkdir()
    env = dict(os.environ,TMP=str(HOME),TEMP=str(HOME),PYTHONDONTWRITEBYTECODE='1',
               BLENDER_USER_RESOURCES=str(HOME/'blender_usuario'))
    with log.open('xb') as f:
        subprocess.run([str(BLENDER),'--background','--factory-startup','--python-exit-code','1',
                        '--python',str(ROOT/'tools/r23_bl1_blender.py'),'--',mode,str(plan),str(out)],
                       stdout=f,stderr=subprocess.STDOUT,env=env,check=True)


def prepare():
    product = HOME/'datos'
    product.mkdir()
    cache = HOME/'cache'
    jobs, rows, done = [], [], {}
    for x in inventory():
        rows.append(x)
        if 'excluded' in x:
            continue
        p = Path(x['manifest']).parent / x['asset']
        key = x['name'] + '__' + Path(x['asset'].replace(':','_')).stem
        x['key'] = key
        if key in done:
            continue
        done[key] = x
        (product/key).mkdir()
        ref = native(x['ref'],cache)
        if x['origin']=='seleccion HD':
            data = ref
        elif x['origin']=='recolor HD':
            data,_ = colores.retint_modelo(ref,x['cfg']['tono'])
        else:
            data,x['original_sha256'] = port(p,cache,x['name'],x['cfg'])
        x['sha256_baseline'] = sha(data)
        x['sha256_ref'] = sha(ref)
        (product/key/'antes.amb').write_bytes(data)
        (product/key/'nativo.amb').write_bytes(ref)
        if x['origin'] not in ('b1','b2'):
            continue
        meshes = []
        for awg,m in enumerate(models(data)):
            gg = groups(m) if m.avb.layout()=='ventana' else []
            meshes.append(scene_mesh(m,awg,gg))
        if any(r['groups'] for r in meshes):
            scene = product/key/'escena.json'
            write_json(scene,dict(source_sha256=sha(data),meshes=meshes))
            jobs.append(dict(key=key,scene=str(scene)))
        print(key, 'costuras', sum(len(r['groups']) for r in meshes), flush=True)
    write_json(HOME/'inventario.json',rows)
    write_json(HOME/'plan.json',jobs)


def finish():
    blender('crear', HOME/'plan.json', HOME/'exportaciones', HOME/'blender.log')
    report, seen = {}, set()
    (HOME/'laminas').mkdir()
    (HOME/'salida').mkdir()
    jobs = {j['key']:Path(j['scene']) for j in load(HOME/'plan.json')}
    for x in load(HOME/'inventario.json'):
        key = x.get('key')
        if not key or key in seen:
            continue
        seen.add(key)
        p = HOME/'datos'/key
        before,native_data = (p/'antes.amb').read_bytes(),(p/'nativo.amb').read_bytes()
        orig,ref = mr.Model(before),mr.Model(native_data)
        rr = report[key] = dict(origin=x['origin'],ref=x['ref'], before=metrics(orig), native=metrics(ref))
        names = ['B3 HD | '+str(x['ref']), 'Antes | '+ x['name'].removeprefix('dlcb1_').removeprefix('vis_b2_')]
        mm = [ref,orig]
        if key in jobs:
            s = load(jobs[key]); exp = load(HOME/'exportaciones'/f'{key}.json')
            after = integrate(before,s,exp)
            rr['changes'] = validate(before,after,s)
            model = mr.Model(after)
            rr['after'] = metrics(model)
            rr['sha256_after'] = sha(after)
            dest = HOME/'salida'/x['name']/x['asset']
            dest.parent.mkdir(parents=True,exist_ok=True)
            with dest.open('xb') as f:
                f.write(after)
            mm.append(model)
            names.append('Después | Blender')
        rr['render'] = sheet(mm,names,HOME/'laminas'/f'{key}.png')
        print(key, 'OK', flush=True)
    write_json(HOME/'medidas.json',report)


def load(p):
    return json.loads(p.read_text(encoding='utf-8'))


def mounted_patch(clean, corrected, mounted, scene):
    """Transfiere únicamente normales a un roster ya montado tras altura/pies."""
    out = bytearray(mounted)
    for a,b,c,s in zip(models(clean),models(corrected),models(mounted),scene['meshes'],strict=True):
        require(a.avb.n==c.avb.n and a.labels==c.labels and a.avb.indices()==c.avb.indices()
                and a.avb.layout()==c.avb.layout()=='ventana'
                and np.array_equal(a.tris,c.tris) and np.array_equal(a.tri_mat,c.tri_mat),
                'El modelo montado no es la misma malla')
        require(all(a.avb.raw_window(i)==c.avb.raw_window(i) for i in range(a.avb.n)),
                'El modelo montado tiene otras ventanas de vértice')
        for group in s['groups']:
            for i in group:
                at = c.avb.vb0 + i*WINDOW + 20
                out[at:at+12] = b.avb.raw_window(i)[20:32]
    validate(mounted,bytes(out),scene)
    return bytes(out)


def overlay(mods, path, jobs, inv):
    roster = mods/'_roster'
    blocks = (roster/'roster.toml').read_text(encoding='utf-8-sig').split('[[personaje]]')[1:]
    path.mkdir()
    packed, records = path/'_roster/us/data_cmn.afs', []
    cache = path/'lzx'
    for job in jobs:
        x = inv[job['key']]
        if not (mods/x['name']/'personaje.toml').is_file():
            continue
        src = mods/x['name']/x['asset']
        require(sha(src.read_bytes())==x['original_sha256'], f'{src}: versión no verificada')
        matches = [b for b in blocks if re.search(rf'^# .* \(mod {re.escape(x["name"])}\)$',b,re.M)]
        require(len(matches)==1, f'{x["name"]}: falta un registro único en _roster')
        slots = json.loads(re.search(r'^modelos\s*=\s*(\[[\d,\s]+\])',matches[0],re.M)[1])
        fid = slots[x['index']-1]
        require(fid>=len(afs_pair.table(str(AFS))), f'Entrada {fid} nativa; no se aplicará')
        src_lzx = roster/'us/data_cmn.afs'/str(fid)/'geom.bin'
        base = decompress(src_lzx.read_bytes(),cache)
        p = Path(job['scene'])
        data = mounted_patch(p.with_name('antes.amb').read_bytes(),
                             (HOME/'salida'/x['name']/x['asset']).read_bytes(),base,load(p))
        dest = packed/str(fid)/'geom.bin'
        dest.parent.mkdir(parents=True)
        tmp = path/f'{fid}.amb'
        tmp.write_bytes(data)
        subprocess.run([str(swap_b3.XBCOMPRESS),'/N:2048',str(tmp),str(dest)],check=True,capture_output=True)
        require(decompress(dest.read_bytes(),cache)==data, f'Error ida y vuelta LZX: {fid}')
        records.append(dict(mod=x['name'],asset=x['asset'],fid=fid,
                            original_sha256=sha(src_lzx.read_bytes()), nuevo_sha256=sha(dest.read_bytes())))
    require(records, 'No hay mods BL1 instalados compatibles')
    write_json(path/'instalacion.json',records)
    print(f'PASS montaje LZX: {len(records)} entradas -> {packed}')
    return len(records)


def verify():
    path = HOME / ('verificacion_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f'))
    path.mkdir()
    jobs = load(HOME/'plan.json')
    require(bool(jobs), 'No hay modelos por verificar')
    blender('reexportar', HOME/'plan.json', path/'export', path/'blender.log')
    inv = {x['key']:x for x in load(HOME/'inventario.json') if 'key' in x}
    report = load(HOME/'medidas.json')
    expected = {}
    all_angles = []
    for j in jobs:
        key = j['key']; x = inv[key]
        p = Path(j['scene'])
        before = p.with_name('antes.amb').read_bytes()
        require(sha(before)==x['sha256_baseline'], f'{key}: baseline distinta')
        raw = (Path(x['manifest']).parent/x['asset']).read_bytes()
        require(sha(raw)==x['original_sha256'], f'{key}: fuente distinta')
        dst = Path(x['name']) / x['asset']
        delivered = (HOME/'salida'/dst).read_bytes()
        expected[dst] = sha(delivered)
        after = integrate(before,load(p),load(path/'export'/f'{key}.json'))
        require(after==delivered and sha(after)==report[key]['sha256_after'], f'{key}: reexportación distinta')
        checks = validate(before,delivered,load(p))
        local = [a for awg in checks for a in awg['angles']]
        require(bool(local), f'{key}: faltan mejoras')
        all_angles.extend(local)
        print(f'{key}: {len(local)} costuras; máximo {max(a for a,_ in local):.3f} -> {max(b for _,b in local):.3f}°')
    require({p.relative_to(HOME/'salida') for p in (HOME/'salida').rglob('*.amb')}==set(expected), 'Salidas adicionales o ausentes')
    print(f'PASS: {len(jobs)} AMB reexportados y reabiertos; {len(all_angles)} costuras; '
          f'media {np.mean(all_angles,axis=0)[0]:.3f} -> {np.mean(all_angles,axis=0)[1]:.3f}°')
    mods = sorted({Path(inv[j['key']]['manifest']).parent.parent for j in jobs})
    count = sum(overlay(m,path/f'montaje_{i:02d}',jobs,inv) for i,m in enumerate(mods,1))
    require(count==len(jobs), 'Faltan entradas LZX verificadas')


if __name__=='__main__':
    if sys.argv[1]=='montar':
        overlay(Path(sys.argv[2]).resolve(),HOME/('montaje_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')),
                load(HOME/'plan.json'),{x['key']:x for x in load(HOME/'inventario.json') if 'key' in x})
    else:
        {'preparar':prepare,'exportar':finish,'verificar':verify}[sys.argv[1]]()

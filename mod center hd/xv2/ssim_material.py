#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SSIM con parejas exactas: material offline XV2 -> AZT; o dos capturas alineadas.

--conversion informe.json --informe salida.json: comprueba el hash del bin y sus
indices de material. Mide el horneado/DXT3, NO la fidelidad del shader de XV2.
--referencia xv2.png --candidato b3.png: prueba visual, sin alinear ni escoger
la mejor pareja. No se confunden los dos criterios en el informe.
--selftest: datos inventados en memoria, sin juegos ni archivos temporales.
"""
import argparse
import os
import sys
import json
import hashlib
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, "..")]
import numpy as np  # noqa: E402
import xv2_a_b3 as X  # noqa: E402
import model_render as MR  # noqa: E402
from skimage.metrics import structural_similarity as ssim  # noqa: E402


def comparar(ref, candidate):
    ref, candidate = np.asarray(ref), np.asarray(candidate)
    if ref.shape != candidate.shape or ref.ndim != 3 or ref.shape[2] != 3 or min(ref.shape[:2]) < 3:
        raise ValueError('las parejas deben ser RGB, mismo tamano, lado >= 3')
    if not np.isfinite(ref).all() or not np.isfinite(candidate).all():
        raise ValueError('imagen no finita')
    window = min(7, min(ref.shape[:2]))
    window -= (window+1) % 2
    score = float(ssim(ref, candidate, channel_axis=2, data_range=255, win_size=window))
    return dict(ssim=score, mae=float(np.abs(ref.astype(float)-candidate).mean()),
                pixels=ref.shape[0]*ref.shape[1])


def medir_conversion(report, render_prefix=None):
    import xeno_a_b3 as C
    from xeno_importar import buscar_xv2
    from xeno_model import Cpk
    d = json.loads(Path(report).read_text(encoding='utf-8'))
    data = Path(d['output']).read_bytes()
    if hashlib.sha256(data).hexdigest() != d['sha256']:
        raise ValueError('el bin no coincide con el informe de conversion')
    source = buscar_xv2()
    if not source:
        raise FileNotFoundError('No encuentro Xenoverse 2 instalado')
    with Cpk(source) as c:
        paths = {n.casefold(): n for n in c.toc}
        get = lambda n: c.get(paths[n.casefold()])
        _, pieces, _ = C.load_source(get, d['lod'], d['character'], d['costume'])
    ref = {(p['part'], p['sm'].name, p['texture'], p['dyt_index']): p for p in pieces}
    tex = MR._textures(data)
    tpl = C.S.Template(d['template'])
    ramp = tpl.textures[tpl.ramp_of(lambda _: True)][3]
    res = []
    ideal = {}
    for bind in d['material_bindings']:
        key = tuple(bind[k] for k in ('part', 'material', 'texture', 'dyt_index'))
        p = ref[key]
        base = C.material_image(p['base'], p['dyt'], p['material'], max(bind['width'],bind['height']), bind['lit'])
        actual = np.rint(tex[bind['base_index']][..., :3]*255).astype(np.uint8)
        ideal[bind['base_index']] = base.astype(np.float32)/255
        if bind['lit']:
            expected_ramp = C.material_ramp(p['dyt'], p['material'], ramp)[0, :, :3]
            ideal[bind['ramp_index']] = C.material_ramp(p['dyt'], p['material'], ramp).astype(np.float32)/255
            actual_ramp = np.rint(tex[bind['ramp_index']][0, :, :3]*255).astype(np.uint8)
            # Curva entera, con 8 niveles de detalle DFD: parejas por coordenada,
            # no por la textura que de el numero mas favorable.
            levels = np.linspace(0, 1, 8)
            detail = np.quantile(255-base[..., :3].mean(2), levels)
            r = np.clip(255-detail[:,None,None]-expected_ramp[None],0,255).astype(np.uint8)
            v = np.clip(255-detail[:,None,None]-actual_ramp[None],0,255).astype(np.uint8)
            curve = comparar(r, v)
        else:
            curve = None
        result = comparar(base[..., :3], actual)
        res.append(dict(**bind, **result, curve=curve))
    if not res:
        raise ValueError('ningun material para comparar')
    total = sum(x['pixels'] for x in res)
    output = dict(criterion='offline_bake_and_texture_encoding_only', shader_capture_validated=False,
                capture_status='PENDIENTE: faltan capturas XV2/B3 de la misma escena y luz',
                model_sha256=d['sha256'], materials=res,
                weighted_ssim=sum(x['ssim']*x['pixels'] for x in res)/total,
                minimum_ssim=min(x['ssim'] for x in res),
                minimum_curve_ssim=min((x['curve']['ssim'] for x in res if x['curve']), default=1.0))
    if render_prefix:
        output['offline_pair'] = render_pair(data,ideal,render_prefix)
        output['capture_status'] = 'Offline: misma malla, pose, camara y luz; referencia DYT/DFD aproximada sin compresion, NO shader original XV2'
    return output


def render_pair(data,ideal,prefix):
    """Pareja controlada: aisla material/compresion de los cambios de topologia.

    No se llama captura de XV2 a un render del Kit ni se valida el shader del
    original con un renderer que no lo implementa. El manifiesto guarda esto.
    """
    import copy
    from PIL import Image
    candidate=MR.Model(data)
    reference=copy.copy(candidate)
    reference.tex=list(candidate.tex)
    for index,image in ideal.items(): reference.tex[index]=image
    pose=candidate.arms_down(68)
    pos,_=candidate.posed(pose)
    lo,hi=pos.min(0),pos.max(0)
    center=(lo+hi)/2
    size=384  # mitad de una captura 768; pocos renders y una luz fija.
    scale=size*.88/max(hi[0]-lo[0],hi[1]-lo[1])
    light=(-.45,.55,.70)
    args=dict(size=(size,size),center=center,scale=scale,pose=pose,light=light,ss=1)
    ref,actual=MR.render(reference,**args),MR.render(candidate,**args)
    paths=[str(prefix)+'_'+name+'.png' for name in ('fuente_offline','port_offline','pareja')]
    for p in paths:
        if Path(p).exists(): raise FileExistsError('no se sobreescribe: '+p)
    Image.fromarray(ref).save(paths[0]); Image.fromarray(actual).save(paths[1])
    pair=Image.new('RGB',(size*2,size),'white')
    def white(image):
        im=Image.fromarray(image).convert('RGBA'); bg=Image.new('RGBA',im.size,'white'); bg.alpha_composite(im)
        return np.asarray(bg.convert('RGB'))
    r,c=white(ref),white(actual)
    pair.paste(Image.fromarray(r),(0,0)); pair.paste(Image.fromarray(c),(size,0)); pair.save(paths[2])
    mask=(ref[...,3]>0) | (actual[...,3]>0)
    yy,xx=np.where(mask)
    crop=(slice(yy.min(),yy.max()+1),slice(xx.min(),xx.max()+1))
    return dict(reference=paths[0],candidate=paths[1],pair=paths[2],
                criterion='same_mesh_offline_approximation_not_original_XV2_shader',
                source_shader_validated=False,light=light,size=[size,size],
                camera_center=center.tolist(),camera_scale=float(scale),pose='arms_down_68',
                full=comparar(r,c),character_bbox=comparar(r[crop],c[crop]))


def selftest():
    rng = np.random.default_rng(7)
    a = rng.integers(0,256,(32,32,3),dtype=np.uint8)
    assert comparar(a,a)['ssim'] == 1
    assert comparar(a,255-a)['ssim'] < 0
    try:
        comparar(a,a[:16])
        raise AssertionError('se aceptaron parejas distintas')
    except ValueError:
        pass
    print('ssim_material: N0 OK (identidad, alteracion y tamanos; sin juego)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--conversion', type=Path)
    ap.add_argument('--referencia', type=Path)
    ap.add_argument('--candidato', type=Path)
    ap.add_argument('--informe', type=Path)
    ap.add_argument('--render-prefix', type=Path, help='pareja offline controlada (no captura del ejecutable XV2)')
    ap.add_argument('--umbral', type=float, default=.95)
    a = ap.parse_args()
    if a.selftest:
        selftest(); return 0
    if not 0 <= a.umbral <= 1:
        ap.error('--umbral debe estar entre 0 y 1')
    if a.conversion:
        result = medir_conversion(a.conversion, a.render_prefix)
        score = min(result['minimum_ssim'], result['minimum_curve_ssim'])
        if a.render_prefix:
            score=min(score,result['offline_pair']['character_bbox']['ssim'])
    elif a.referencia and a.candidato:
        from PIL import Image
        r = np.array(Image.open(a.referencia).convert('RGB'))
        c = np.array(Image.open(a.candidato).convert('RGB'))
        result = dict(criterion='aligned_captures', reference=str(a.referencia), candidate=str(a.candidato), **comparar(r,c))
        score = result['ssim']
    else:
        ap.error('indica --conversion o --referencia y --candidato')
    result.update(threshold=a.umbral, passed=score >= a.umbral)
    if a.informe:
        from xeno_binary import write_json
        write_json(a.informe, result)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result['passed'] else 1


if __name__ == "__main__":
    sys.exit(main())

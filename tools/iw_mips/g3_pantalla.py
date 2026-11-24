# SPDX-License-Identifier: MIT
"""R23 G3 en pantalla: que partes de la semantica del GS usan las primitivas 2D del original (oraculo, 4250..5050) y
cuales NO reproduce el pintor del port (src/iw_trad_bucle.cpp, Pintura + Panel::OnDraw: quads de ImGui con tinte).

  python tools/iw_mips/g3_pantalla.py CLAVE_HEX [CLAVE_HEX ...]   -> E:\\DBZ3HD_trabajo\\g23\\g3_<CLAVE>.json y tabla

Para cada primitiva 2D (sprite de 2 vertices, triangulo de 3) mide, ponderado por area en pantalla (recortada al SCISSOR y a
640x448), las funciones del GS que cambian el pixel y que el pintor no implementa. Lo que el pintor SI implementa:
TME con modulado por el color del vertice (x2, tope 255), ABE como alfa / suma / resta, SCISSOR, sprites y triangulos.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import gs_primitivas as G  # noqa: E402

TEX_MIN = 0x2C00


def rect(p):
    xs = [v[0] / 16 for v in p[17]]
    ys = [v[1] / 16 for v in p[17]]
    ofx, ofy = (p[7] & 0xFFFF) / 16, ((p[7] >> 32) & 0xFFFF) / 16
    x0, x1, y0, y1 = min(xs) - ofx, max(xs) - ofx, min(ys) - ofy, max(ys) - ofy
    sc = p[6]
    cx0, cx1 = sc & 0x7FF, ((sc >> 16) & 0x7FF) + 1
    cy0, cy1 = (sc >> 32) & 0x7FF, ((sc >> 48) & 0x7FF) + 1
    x0, x1 = max(x0, cx0, 0), min(x1, cx1, 640)
    y0, y1 = max(y0, cy0, 0), min(y1, cy1, 448)
    return max(0, x1 - x0) * max(0, y1 - y0), (x0, y0, x1, y1)


def clasifica(p, acc):
    attr, tex0, tex1, clamp, alpha, test = p[0], p[1], p[2], p[3], p[4], p[5]
    tipo = attr & 7
    nv = len(p[17])
    tri = 3 <= tipo <= 5 and nv == 3
    if not (tri or (tipo == 6 and nv == 2)):
        return
    area, r = rect(p)
    if area <= 0:
        return
    tme, abe = bool(attr & 0x10), bool(attr & 0x40)
    if tme and (tex0 & 0x3FFF) < TEX_MIN:
        acc['desc_tex_baja'] += area   # el pintor la descarta (copia del fotograma / profundidad)
        return
    if not tme and not abe and not tri:
        acc['desc_borrado'] += area    # sprite opaco liso: el pintor lo descarta (taparia la escena 3D)
        return
    acc['total'] += area
    acc['n'] += 1
    c = p[17][2 if tri else 1][4]
    ch = [c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24]
    if tme:
        tfx, tcc = (tex0 >> 35) & 3, (tex0 >> 34) & 1
        if tfx != 0:
            acc['tfx_no_modulado'] += area
        if tfx == 0 and any(v > 0x80 for v in ch[:3]):
            acc['modulado_sobre_0x80'] += area
        if tcc == 0:
            acc['tcc_rgb'] += area
        if not (tex1 >> 5) & 1:
            acc['mag_puntual'] += area
        wms, wmt = clamp & 3, (clamp >> 2) & 3
        if wms >= 2 or wmt >= 2:
            acc['clamp_region'] += area
            if attr & 0x100 and tex0:   # FST: UV en 12.4; comprueba si el sprite sale de la region
                us = [(v[7] & 0x3FFF) / 16 for v in p[17]]
                vs = [((v[7] >> 16) & 0x3FFF) / 16 for v in p[17]]
                mu0, mu1 = (clamp >> 4) & 0x3FF, (clamp >> 14) & 0x3FF
                mv0, mv1 = (clamp >> 24) & 0x3FF, (clamp >> 34) & 0x3FF
                fuera = (wms == 2 and (min(us) < mu0 - 0.01 or max(us) > mu1 + 1.01)) or                         (wmt == 2 and (min(vs) < mv0 - 0.01 or max(vs) > mv1 + 1.01)) or wms == 3 or wmt == 3
                if fuera:
                    acc['region_se_sale'] += area
        if wms == 0 or wmt == 0:
            acc['repeat'] += area
    ate = test & 1
    if ate and ((test >> 1) & 7) != 1:
        acc['prueba_alfa'] += area
        acc['ate_ST%d_REF%02X_FAIL%d_%s' % ((test >> 1) & 7, (test >> 4) & 0xFF, (test >> 12) & 3, 'abe' if abe else 'opaco')] += area
    if (test >> 14) & 1:
        acc['date'] += area
    if (test >> 16) & 1 and ((test >> 17) & 3) != 1:
        acc['prueba_z'] += area
        acc['z_ZTST%d_ZMSK%d' % ((test >> 17) & 3, (p[9] >> 32) & 1)] += area
        zs = [v[2] for v in p[17]]
        zpsm = (p[9] >> 24) & 15
        zmax = 0xFFFFFFFF if zpsm == 0 else 0xFFFFFF if zpsm == 1 else 0xFFFF
        acc['z_sprite_max' if min(zs) >= zmax - 0xF else 'z_sprite_otro'] += area
        if min(zs) < zmax - 0xF:
            acc['z_ej_%X_psm%d' % (min(zs), zpsm)] += area
    if p[10] & 1:
        acc['fba'] += area
    if abe:
        a, b, cc, d = alpha & 3, (alpha >> 2) & 3, (alpha >> 4) & 3, (alpha >> 6) & 3
        fix = (alpha >> 32) & 0xFF
        clave = 'abe_A%dB%dC%dD%d' % (a, b, cc, d)
        ok = (a == 0 and b == 2 and d == 1) or (a == 2 and b == 0 and d == 1) or (a == 0 and b == 1 and d == 1) \
            or a == b or (a, b, d) == (0, 1, 1)
        acc['abe_' + ('soportado' if ok else 'NO_soportado')] += area
        acc[clave] += area
        if p[11] & 1:
            acc['pabe'] += area


def main():
    for k in sys.argv[1:]:
        ruta = Path(r'E:\DBZ3HD_trabajo\g23') / ('orac_' + k) / 'gs.jsonl'
        t = G.Traductor()
        acc = {x: 0 for x in ('total', 'n', 'desc_tex_baja', 'desc_borrado')}
        from collections import defaultdict
        acc = defaultdict(float, acc)
        marcos = 0
        for l in open(ruta, encoding='utf-8'):
            f = json.loads(l)
            for o in f['ordenes']:
                if o[0] == 'G':
                    t.registro(o[1], o[2])
                elif o[0] == 'I':
                    t.imagen(bytes(0))
            for p in t.prims:
                clasifica(p, acc)
            t.fin_cuadro()
            marcos += 1
        acc['cuadros'] = marcos
        json.dump(acc, open(r'E:\DBZ3HD_trabajo\g23\g3_%s.json' % k, 'w'), indent=1, sort_keys=True)
        tot = acc['total'] or 1
        print(k, 'cuadros', marcos, 'prims2D', int(acc['n']))
        for c, v in sorted(acc.items()):
            if c in ('total', 'n', 'cuadros'):
                continue
            print('   %-22s %5.1f %%  (%d px-cuadro)' % (c, 100 * v / tot, v))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""NCAC · Juez de coherencia por forma del Creador de Personajes (CaC).

Comprueba directamente sobre los binarios #AMB reales montados desde `us/data_cmn.afs`
que cada forma (SSJ1, SSJ2, SSJ3 y formas nativas de otras razas) cumple sus reglas
con cualquier cuerpo, peinado, rostro, capa y traje:
  1. Pelo: sin residuos de pelo base apilados; en SSJ1-3 el pelo es DORADO (y en SSJ3
     no queda sustituido por un pelo base corto/negro como pelo_fid_cac2=75).
  2. Ojos: en SSJ1-3 las texturas faciales tienen iris AZUL/TURQUESA y no pierden
     los ojos nativos SSJ.
  3. Cejas: en SSJ3 NO hay cejas (ni oscuras NI rubias/amarillas sobre la frente).
  4. Estallido y forma-receta: `recetas.modelo_receta_forma` respeta las reglas de forma
     (incluidas razas no-Saiyan y tope de ventanas de la forma) y `cac_estallido_forma`
     se propaga al `personaje.toml` del heroe.

Solo lectura sobre el arbol raiz: todo se monta en memoria o directorio temporal.

Uso:
  python juez_formas_cac.py [--raiz RUTA] [--json SALIDA.json] [--rapido]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import numpy as np


@dataclass
class ResultadoCaso:
    caso: str
    cuerpo: int
    traje: int
    forma: str
    nivel_ssj: int
    ok: bool
    pelo_ok: bool
    ojos_ok: bool
    cejas_ok: bool
    estallido_ok: bool
    metricas: dict[str, Any]
    motivos: list[str]


def _cargar_vh(raiz: Path):
    sys.dont_write_bytecode = True
    for p in (raiz / "mod center hd" / "cac", raiz / "mod center hd", raiz / "awo_tools"):
        s = str(p)
        if s not in sys.path:
            sys.path.insert(0, s)
    import vista_heroe as vh
    import recetas
    return vh, recetas


def _medir_pelo_rgb(vh, amb_bytes: bytes, montaje_pelo: Any = None) -> dict[str, Any]:
    """Mide el color cocido mediano (RGB 0..1) y triangulos de la zona de pelo del #AMB."""
    import re as _re
    m = vh.mr.Model(amb_bytes)
    P, _ = m.posed(vh.pose_de(m))
    z = vh.zonas_ficha(m, P, pelo_color=True)
    ht = [i for i, x in enumerate(z) if x == "pelo"]
    # Si el montaje injerto un atlas de pelo en un material dedicado (ej. Bardock SSJ1/2:
    # tex 20, mat 1), usar tambien los triangulos de ese material aunque zonas_ficha
    # los confunda con tono de piel claro.
    if isinstance(montaje_pelo, dict) and isinstance(montaje_pelo.get("atlas"), str):
        ma = _re.search(r"material\s+(\d+)", montaje_pelo["atlas"])
        if ma:
            mat_atlas = int(ma.group(1))
            ht_atlas = [i for i in range(len(m.tris)) if m.tri_mat[i] == mat_atlas]
            if len(ht_atlas) > len(ht):
                ht = ht_atlas
    if not ht:
        return {"tris_pelo": 0, "rgb": None, "dorado": False, "y_min_head": None}
    # Muestrear texeles cocidos de cada triangulo de pelo
    rgbs = []
    por_mat: dict[int, list[int]] = {}
    for i in ht:
        por_mat.setdefault(m.tri_mat[i], []).append(i)
    for mi, tis in por_mat.items():
        mat = m.mats[mi]
        ti = mat["tex"]
        if ti >= len(m.tex):
            continue
        tex = vh._textura_cocida(m, mat)
        h, w = tex.shape[:2]
        uv = m.uv[m.tris[tis]].mean(axis=1)
        ix = np.clip((uv[:, 0] * w).astype(int), 0, w - 1)
        iy = np.clip((uv[:, 1] * h).astype(int), 0, h - 1)
        rgbs.append(tex[iy, ix, :3])
    if not rgbs:
        return {"tris_pelo": len(ht), "rgb": None, "dorado": False, "y_min_rel": None}
    cols = np.concatenate(rgbs, axis=0)
    # Quitar lineas negras de contorno puro (<0.08) si hay >30% no-negro
    no_negro = cols.max(axis=1) > 0.08
    muestra = cols[no_negro] if no_negro.mean() > 0.30 else cols
    med = np.median(muestra, axis=0)
    r, g, b = float(med[0]), float(med[1]), float(med[2])
    # Dorado SSJ: luminoso, rojo dominante, verde medio-alto, azul claramente menor que rojo
    dorado = (r >= 0.62 and g >= 0.45 and (r - b) >= 0.16 and r >= g - 0.05)
    # Altura minima relativa del pelo respecto a HEAD (para distinguir SSJ3 melena larga de pelo corto)
    L, _, _ = vh._local_head(m, P)
    y_min = float(L[np.unique(m.tris[ht])][:, 1].min()) if L is not None else None
    return {
        "tris_pelo": len(ht),
        "rgb": [round(r, 3), round(g, 3), round(b, 3)],
        "dorado": dorado,
        "y_min_head": round(y_min, 3) if y_min is not None else None,
    }


def _medir_cara_ssj(vh, amb_bytes: bytes) -> dict[str, Any]:
    """Mide pixeles de iris azul/turquesa y cejas (oscuras Y rubias) sobre las texturas faciales."""
    from scipy import ndimage
    m = vh.mr.Model(amb_bytes)
    n0 = len(m.tex)
    P, _ = m.posed(vh.pose_de(m))
    muestras = vh._muestras_cara(m, P, n0, {})
    azules_total = 0
    cejas_oscuras_total = 0
    cejas_rubias_total = 0
    por_tex = {}
    for ti, mu in sorted(muestras.items()):
        tex = np.asarray(m.tex[ti], np.float32)
        hgt, wid = tex.shape[:2]
        img = tex[..., :3]
        cara = np.zeros((hgt, wid), bool)
        for (y, x), (_xx, _yy, zz) in mu.items():
            if zz > 0.1:
                cara[y, x] = True
        # Iris azul/turquesa SSJ (tanto nativo B3 ~ (0.15..0.45, 0.55..0.90, 0.55..0.95) como SSJ_OJOS (0.2, 0.62, 0.7))
        azul = cara & (img[..., 2] >= 0.38) & (img[..., 1] >= 0.35) & ((img[..., 2] - img[..., 0]) >= 0.14) & ((img[..., 1] - img[..., 0]) >= 0.10)
        n_azul = int(azul.sum())
        azules_total += n_azul

        # Detectar ojos (esclerotica gris claro) para localizar la banda de cejas justo encima
        bl = (img.min(2) > 0.72) & (np.ptp(img, 2) < 0.08) & (img.max(2) < 0.985) & cara
        et, n = ndimage.label(bl)
        tam = ndimage.sum(np.ones_like(et), et, range(1, n + 1)) if n else np.zeros(0)
        ojos = [k + 1 for k in np.flatnonzero(tam >= max(40, 0.0015 * wid * hgt))]
        # Banda de cejas por encima de los ojos abiertos
        banda = np.zeros((hgt, wid), bool)
        L = max(1, wid // 128)
        for k in ojos:
            yy, xx = np.nonzero(et == k)
            if len(yy) < 3:
                continue
            x0, x1, y0, y1 = int(xx.min()), int(xx.max()), int(yy.min()), int(yy.max())
            hmax = int(1.3 * (y1 - y0)) + 4
            marg = int(0.25 * (x1 - x0)) + 1
            for x in range(max(0, x0 - marg), min(wid, x1 + marg + 1)):
                col_y = yy[xx == x]
                top = int(col_y.min()) if len(col_y) else y0
                lo = max(0, top - hmax)
                hi = max(lo, top - L - 2)
                if hi > lo:
                    banda[lo:hi, x] = True
        banda &= cara & ~bl
        # Pixeles de ceja oscuros (relleno negro grueso) y rubios/amarillos (relleno SSJ nativo)
        dk = banda & (img.max(2) < 0.35)
        rub = banda & (img[..., 0] >= 0.62) & (img[..., 1] >= 0.48) & ((img[..., 0] - img[..., 2]) >= 0.22)
        # Exigir manchas reales (>= 8 px conectados) para no contar lineas de arruga de 1px
        ed, nd_ = ndimage.label(dk)
        n_osc = sum(int((ed == k).sum()) for k in range(1, nd_ + 1) if int((ed == k).sum()) >= 12)
        er, nr_ = ndimage.label(rub)
        n_rub = sum(int((er == k).sum()) for k in range(1, nr_ + 1) if int((er == k).sum()) >= 10)
        cejas_oscuras_total += n_osc
        cejas_rubias_total += n_rub
        por_tex[ti] = {"azul": n_azul, "ceja_osc": n_osc, "ceja_rubia": n_rub}
    return {
        "azules": azules_total,
        "cejas_oscuras": cejas_oscuras_total,
        "cejas_rubias": cejas_rubias_total,
        "por_tex": por_tex,
    }


def evaluar_matriz(raiz: Path, rapido: bool = False, cejas_on: bool = False) -> dict[str, Any]:
    vh, recetas = _cargar_vh(raiz)
    work = tempfile.mkdtemp(prefix="ncac_juez_")
    resultados: list[ResultadoCaso] = []
    try:
        afs = vh.abrir_afs(str(raiz / "us"), work, str(raiz))

        # ------------------------------------------------------------------
        # BLOQUE A: Saiyans (0 Goku, 7 Vegeta, 8 Trunks, 39 Bardock) x outfits x SSJ1..3
        # ------------------------------------------------------------------
        escenarios_saiyan = [
            # Base limpio de cada cuerpo Saiyan con estilo Goku (tiene SSJ1, SSJ2, SSJ3)
            ("goku_base", 0, 0, {"donante": 0}),
            ("vegeta_base", 7, 0, {"donante": 0}),
            ("trunks_base", 8, 0, {"donante": 0}),
            ("bardock_base", 39, 0, {"donante": 0}),
            # Trajes alternativos
            ("goku_traje1", 0, 1, {"donante": 0}),
            ("vegeta_traje2", 7, 2, {"donante": 0}),
            ("trunks_traje1", 8, 1, {"donante": 0}),
            # Pelo comun CAC2 (Android 17 = 75 negro, Android 18 = 80 rubio) en Goku 0_0
            ("goku_pelo_cac2_75", 0, 0, {"donante": 0, "pelo_fid_cac2": 75}),
            ("goku_pelo_cac2_80", 0, 0, {"donante": 0, "pelo_fid_cac2": 80}),
            # Peinados nativos del creador
            ("goku_pelo_trunks", 0, 0, {"donante": 0, "pelo": 6}),
            ("vegeta_pelo_goku", 7, 0, {"donante": 0, "pelo": 1}),
            ("bardock_pelo_gohan", 39, 0, {"donante": 0, "pelo": 4}),
            # Rostros elegidos (especialmente rostros SSJ nativos Vegeta=2, Gohan=4, Trunks=5, y gesto A=9)
            ("goku_rostro_vegeta", 0, 0, {"donante": 0, "rostro": 2}),
            ("goku_rostro_gohan", 0, 0, {"donante": 0, "rostro": 4}),
            ("goku_rostro_trunks", 0, 0, {"donante": 0, "rostro": 5}),
            ("vegeta_rostro_tipoA", 7, 0, {"donante": 0, "rostro": 9}),
            # Capas y prendas B3 + colores personalizados en base (c1/ojos/texcol)
            ("goku_b3_torso_c1_azul", 0, 0, {"donante": 0, "b3_torso": 416, "c1": (230, 80, 0), "ojos": (0, 80, 20)}),
            ("goku_texcol_base", 0, 0, {"donante": 0, "texcol": "0,0,220,80,0|1,0,220,80,0"}),
            ("goku_capa_yamcha_447", 0, 0, {"donante": 0, "capa1": "447:0"}),
            ("trunks_capa_armadura", 8, 0, {"donante": 0, "capa1": "416:2"}),
        ]
        if rapido:
            escenarios_saiyan = escenarios_saiyan[:10] + [escenarios_saiyan[12], escenarios_saiyan[13], escenarios_saiyan[17]]

        # Referencia de ojos nativos Goku SSJ3 (268) sin modificaciones
        ref_268 = _medir_cara_ssj(vh, bytes(afs.entry(268)))
        ref_266 = _medir_cara_ssj(vh, bytes(afs.entry(266)))

        for nombre, cuerpo, traje, extra in escenarios_saiyan:
            ficha = {"cuerpo": cuerpo, "traje": traje, "partes": [vh.CERO] * 4,
                     "pelo": 0, "c1": vh.CERO, "c2": vh.CERO, "logo": 0, "scouter": 0}
            ficha.update(extra)
            if cejas_on:
                ficha["_cejas_ssj3"] = 1
            fid = vh.KFID[cuerpo][traje]
            r = {}
            try:
                ssj_list = vh.formas_ssj(afs, fid, ficha, str(raiz), r)
            except Exception as e:
                resultados.append(ResultadoCaso(nombre, cuerpo, traje, "SSJ*", 0, False, False, False, False, True,
                                                {"error": str(e)}, [f"formas_ssj fallo: {e}"]))
                continue
            for lvl, (datos_k, forma_idx) in enumerate(ssj_list, 1):
                mp_info = r.get("montaje_formas", {}).get(forma_idx, {}).get("pelo")
                mp = _medir_pelo_rgb(vh, datos_k, montaje_pelo=mp_info)
                mc = _medir_cara_ssj(vh, datos_k)
                motivos: list[str] = []

                # Regla 1: pelo dorado (y en SSJ3 con pelo_fid_cac2 no debe salir pelo negro/corto)
                pelo_ok = bool(mp["dorado"])
                if not pelo_ok:
                    motivos.append(f"pelo no dorado en SSJ{lvl}: rgb={mp['rgb']} (tris={mp['tris_pelo']})")
                # Comprobar que en SSJ3 sin capa de pelo explicita no quede corto si era Goku/pelo_fid_cac2
                if lvl == 3 and extra.get("pelo_fid_cac2") and mp.get("y_min_head") is not None and mp["y_min_head"] > -0.8:
                    pelo_ok = False
                    motivos.append(f"pelo_fid_cac2={extra['pelo_fid_cac2']} sobrescribio la melena SSJ3 (y_min_head={mp['y_min_head']})")

                # Regla 2: ojos azules/turquesa en SSJ1..3 (salvo rostro 4 Gohan joven 236/237 cuyo iris nativo es verde/gris en B3;
                # en rostros entornados proyectados sobre SSJ3 268 el iris mide >=10 px)
                ojos_ok = True if extra.get("rostro") == 4 else (mc["azules"] >= 10)
                if not ojos_ok:
                    motivos.append(f"sin suficientes pixeles de iris azul en SSJ{lvl}: azules={mc['azules']}")
                # Regresion de ojos nativos en Goku SSJ3 base y Trunks SSJ3
                if nombre == "goku_base" and lvl == 3 and mc["azules"] < int(ref_268["azules"] * 0.70):
                    ojos_ok = False
                    motivos.append(f"ojos nativos Goku SSJ3 degradados: {ref_268['azules']}->{mc['azules']}")
                if cuerpo == 8 and lvl == 3 and mc["azules"] < 175:
                    ojos_ok = False
                    motivos.append(f"ojos nativos Trunks SSJ3 erosionados por sin_cejas: {mc['azules']} (<175 de 198)")

                # Regla 3: SSJ3 SIN cejas (ni oscuras ni rubias!)
                cejas_ok = True
                if lvl == 3:
                    if mc["cejas_oscuras"] >= 20 or mc["cejas_rubias"] >= 20:
                        cejas_ok = False
                        motivos.append(f"SSJ3 conserva cejas visibles: oscuras={mc['cejas_oscuras']} px, rubias={mc['cejas_rubias']} px")

                # Regla 4: presupuesto de ventanas
                _, tope, exc = vh.tope_info(r.get("montaje_formas", {}).get(forma_idx, {}))
                if exc:
                    motivos.append(f"excede tope de ventanas ({tope})")

                ok = pelo_ok and ojos_ok and cejas_ok and not exc
                resultados.append(ResultadoCaso(
                    caso=f"{nombre}_ssj{lvl}",
                    cuerpo=cuerpo, traje=traje, forma=f"SSJ{lvl}", nivel_ssj=lvl,
                    ok=ok, pelo_ok=pelo_ok, ojos_ok=ojos_ok, cejas_ok=cejas_ok, estallido_ok=True,
                    metricas={"pelo": mp, "cara": mc, "ref_268_azules": ref_268["azules"], "ref_266_azules": ref_266["azules"]},
                    motivos=motivos,
                ))

        # ------------------------------------------------------------------
        # BLOQUE B: Receta por forma (`formaN` + `estallidoN`) en Saiyan y NO-Saiyan
        # ------------------------------------------------------------------
        # B1: Saiyan con forma2 (SSJ1) + estallido2="#FFAA00" -> debe producir personaje.toml con cac_estallido_forma
        f_base_txt = "cuerpo=0;traje=0;partes=0,0,0|0,0,0|0,0,0|0,0,0;pelo=0;c1=0,0,0;c2=0,0,0;logo=0;scouter=0"
        f_sub_txt = "cuerpo=0;traje=1;partes=120,40,10|0,0,0|0,0,0|0,0,0;pelo=0;c1=0,0,0;c2=0,0,0;logo=0;scouter=0"
        f_sai = vh.leer_ficha(f_base_txt + f";forma2={recetas.hex_de_ficha(f_sub_txt)};estallido2=#FFAA00;nombre=JuezBurst")
        f_sai["donante"] = 0
        r_sai = {}
        d_base, r_sai = vh.combate_bin(afs.entry(264), f_sai, str(raiz), afs)
        ssj_sai = vh.formas_ssj(afs, 264, f_sai, str(raiz), r_sai)
        juego_tmp = Path(work) / "juego_tmp"
        juego_tmp.mkdir(parents=True, exist_ok=True)
        vh.escribir_personaje_propio(str(juego_tmp), 1, f_sai, f_base_txt, d_base, r_sai, 264, ssj_sai, mod_nombre="juez_burst", afs=afs)
        pt_txt = (juego_tmp / "mods/juez_burst/personaje.toml").read_text(encoding="utf-8")
        tiene_est = 'cac_estallido_forma = { 2 = "#FFAA00" }' in pt_txt
        rec_f2 = (juego_tmp / "mods/juez_burst/modelos/receta_forma2.bin")
        f2_ok = rec_f2.is_file()
        mp_f2 = _medir_pelo_rgb(vh, rec_f2.read_bytes()) if f2_ok else {"dorado": False}
        mc_f2 = _medir_cara_ssj(vh, rec_f2.read_bytes()) if f2_ok else {"azules": 0}
        mots_b1 = []
        if not tiene_est:
            mots_b1.append("personaje.toml no escribio cac_estallido_forma")
        if not f2_ok or not mp_f2["dorado"]:
            mots_b1.append(f"receta_forma2 sin pelo dorado SSJ: {mp_f2}")
        if mc_f2.get("azules", 0) < 15:
            mots_b1.append(f"receta_forma2 sin ojos azules SSJ: {mc_f2}")
        resultados.append(ResultadoCaso(
            caso="receta_forma_saiyan_ssj1_estallido", cuerpo=0, traje=0, forma="forma2(SSJ1)+estallido", nivel_ssj=1,
            ok=not mots_b1, pelo_ok=bool(mp_f2.get("dorado")), ojos_ok=mc_f2.get("azules", 0) >= 15,
            cejas_ok=True, estallido_ok=tiene_est,
            metricas={"personaje_toml_estallido": tiene_est, "pelo": mp_f2, "cara": mc_f2},
            motivos=mots_b1,
        ))

        # B2: NO-Saiyan (Cell cuerpo=33, forma1 = Imperfecto->Semiperfecto) con receta de forma (`modelo_receta_forma`)
        # Comprobar que recetas.modelo_receta_forma monta el modelo de la FORMA 1 (fid=147), NO el de base (fid=146)!
        f_cell_sub = "cuerpo=33;traje=0;partes=120,40,10|0,0,0|0,0,0|0,0,0;pelo=0;c1=0,0,0;c2=0,0,0;logo=0;scouter=0"
        f_cell = {"cuerpo": 33, "traje": 0, "donante": 33}
        m_cell_f1 = recetas.modelo_receta_forma(str(raiz), f_cell, afs, 1, f_cell_sub)
        m_cell_base = vh.combate_bin(afs.entry(146), vh.leer_ficha(f_cell_sub), str(raiz), afs)[0]
        cell_es_distinta_base = m_cell_f1 is not None and m_cell_f1[0] != m_cell_base
        mots_b2 = [] if cell_es_distinta_base else [
            "modelo_receta_forma devolvio el modelo BASE (146) para Cell forma 1 en vez del modelo de forma 1 (147) porque no usa formas_cuerpo_nativo ni formas de raza."
        ]
        resultados.append(ResultadoCaso(
            caso="receta_forma_cell_forma1", cuerpo=33, traje=0, forma="Cell_F1(receta)", nivel_ssj=0,
            ok=cell_es_distinta_base, pelo_ok=True, ojos_ok=True, cejas_ok=True, estallido_ok=cell_es_distinta_base,
            metricas={"igual_a_base": not cell_es_distinta_base},
            motivos=mots_b2,
        ))

        # B3: Estallido sin forma-receta: comprobar que pasa si el usuario quiere estallido en forma nativa
        # y si existe consumidor runtime del color de estallido en src/
        heroe_cpp = (raiz / "src/heroe.cpp").read_text(encoding="utf-8", errors="replace")
        roster_src = (raiz / "mod center hd/roster_build.py").read_text(encoding="utf-8", errors="replace")
        consume_est_combate = "cac_estallido_forma" in roster_src
        resultados.append(ResultadoCaso(
            caso="estallido_contrato_combate_vs_previa", cuerpo=0, traje=0, forma="estallido_runtime", nivel_ssj=0,
            ok=True, pelo_ok=True, ojos_ok=True, cejas_ok=True, estallido_ok=True,
            metricas={"roster_build_consume_cac_estallido_forma": consume_est_combate,
                      "previa_cac3_existe": "PintarEstallidoCac3" in (raiz / "src/heroe_v2_creador.inl").read_text(encoding="utf-8", errors="replace")},
            motivos=[] if consume_est_combate else [
                "Nota documentada: cac_estallido_forma solo se muestra en la vista previa (CAC3); roster_build/combate aun no lo consumen."
            ],
        ))

        # B4: Comprobar tope_info en recetas.modelo_receta_forma sobre forma SSJ (evalua r de base o r de forma?)
        rec_src = (raiz / "mod center hd/cac/recetas.py").read_text(encoding="utf-8", errors="replace")
        m_mrf = rec_src[rec_src.find("def modelo_receta_forma"):rec_src.find("def aplicar_formas")]
        # comprobar si tope_info recibe r con montaje_formas o solo base
        # (formas_ssj si rellena r["montaje_formas"], pero solo si se paso `r`)
        resultados.append(ResultadoCaso(
            caso="modelo_receta_forma_tope", cuerpo=0, traje=0, forma="tope_ventanas", nivel_ssj=0,
            ok="formas_ssj(afs, fid, g, juego, r)" in m_mrf and "tope_info(r)" in m_mrf,
            pelo_ok=True, ojos_ok=True, cejas_ok=True, estallido_ok=True,
            metricas={"propaga_r": "formas_ssj(afs, fid, g, juego, r)" in m_mrf},
            motivos=[],
        ))

        # ------------------------------------------------------------------
        # BLOQUE C: Formas nativas de otras razas (Krilin 10, Piccolo 11, Freezer 27, Cell 33, Cooler 38)
        # ------------------------------------------------------------------
        for nombre_r, cid in (("krilin_potencial", 10), ("piccolo_nail_kami", 11),
                              ("freezer_formas", 27), ("cell_formas", 33), ("cooler_final", 38)):
            ficha_r = {"cuerpo": cid, "traje": 0, "donante": cid, "partes": [vh.CERO] * 4,
                       "pelo": 0, "c1": vh.CERO, "c2": vh.CERO, "logo": 0, "scouter": 0}
            fid0 = vh.KFID[cid][0]
            rr = {}
            try:
                fms = vh.formas_ssj(afs, fid0, ficha_r, str(raiz), rr)
                ok_r = len(fms) > 0
                mots_r = [] if ok_r else [f"cuerpo {cid} genero 0 formas nativas"]
                for idx_f, (dat_f, fm_i) in enumerate(fms, 1):
                    mf = vh.mr.Model(dat_f)
                    if len(mf.tris) == 0:
                        ok_r = False
                        mots_r.append(f"forma {fm_i} con 0 triangulos")
                resultados.append(ResultadoCaso(
                    caso=nombre_r, cuerpo=cid, traje=0, forma=f"{len(fms)}_formas", nivel_ssj=0,
                    ok=ok_r, pelo_ok=True, ojos_ok=True, cejas_ok=True, estallido_ok=True,
                    metricas={"n_formas": len(fms), "indices": [fm_i for _, fm_i in fms]},
                    motivos=mots_r,
                ))
            except Exception as e:
                resultados.append(ResultadoCaso(
                    caso=nombre_r, cuerpo=cid, traje=0, forma="error", nivel_ssj=0,
                    ok=False, pelo_ok=False, ojos_ok=False, cejas_ok=False, estallido_ok=False,
                    metricas={"error": str(e)}, motivos=[str(e)],
                ))

    finally:
        shutil.rmtree(work, ignore_errors=True)

    fallados = [r for r in resultados if not r.ok]
    return {
        "total": len(resultados),
        "pass": len(resultados) - len(fallados),
        "fail": len(fallados),
        "resultados": [asdict(r) for r in resultados],
    }


def imprimir(rep: dict[str, Any]) -> None:
    print(f"JUEZ DE COHERENCIA POR FORMA CaC — {rep['pass']}/{rep['total']} PASS, {rep['fail']} FAIL")
    print()
    print("| Caso | Cuerpo | Forma | Pelo | Ojos | Cejas | Estallido | Estado |")
    print("|---|---:|---|---|---|---|---|---|")
    for r in rep["resultados"]:
        st = "PASS" if r["ok"] else "FAIL"
        yn = lambda b: "OK" if b else "FAIL"
        print(f"| {r['caso']} | {r['cuerpo']} | {r['forma']} | {yn(r['pelo_ok'])} | {yn(r['ojos_ok'])} "
              f"| {yn(r['cejas_ok'])} | {yn(r['estallido_ok'])} | {st} |")
    fails = [r for r in rep["resultados"] if not r["ok"]]
    if fails:
        print("\nCOMBINACIONES QUE FALLAN:")
        for r in fails:
            print(f"  - {r['caso']} (cuerpo={r['cuerpo']}, traje={r['traje']}, forma={r['forma']}):")
            for m in r["motivos"]:
                print(f"      * {m}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--raiz", type=Path, default=Path.cwd())
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--rapido", action="store_true")
    ap.add_argument("--cejas-on", action="store_true", help="medir la variante visual experimental SSJ3")
    ap.add_argument("--autoprueba", action="store_true", help="control sintético de texturas sin AFS")
    a = ap.parse_args(argv)
    if a.autoprueba:
        from types import SimpleNamespace
        tex = np.zeros((4, 4, 4), dtype=np.float32)
        tex[1, 1] = (.2, .6, .7, 1)
        tex[2, 2] = (.2, .6, .7, 1)  # fuera de la región facial, no debe contar
        m = SimpleNamespace(tex=[tex], posed=lambda _: (None, None))
        vh = SimpleNamespace(mr=SimpleNamespace(Model=lambda _: m), pose_de=lambda _: None,
                             _muestras_cara=lambda *_: {0: {(1, 1): (0, 0, 1)}})
        assert _medir_cara_ssj(vh, b"")["azules"] == 1
        print("Juez de formas: autoprueba OK")
        return 0
    rep = evaluar_matriz(a.raiz.resolve(), rapido=a.rapido, cejas_on=a.cejas_on)
    imprimir(rep)
    if a.json:
        a.json.parent.mkdir(parents=True, exist_ok=True)
        a.json.write_text(json.dumps(rep, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0 if rep["fail"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

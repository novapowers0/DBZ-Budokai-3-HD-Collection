#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""juez_coleccion_tecnicas.py — Juez de coherencia entre promesas de UI y logica real en src/.

Contrasta sistematicamente:
  1. Textos de requisito de recompensas.toml (Coleccion) vs EvaluarRecompensas() y consumidores reales;
  2. Textos de requisito de desbloqueos.toml (Evolucion, SSJ, aura/ki/esquivas) vs ComprobarDesbloqueos() / ComprobarFormas();
  3. Textos de UI de Maestros (heroe_v2_maestros.inl, heroe_ui.cpp) vs PuedeCambiarA() / Desbloqueado() / AfinidadRuta();
  4. Textos de UI de Tecnicas (heroe_v2_tecnicas.inl, heroe_ui.cpp) vs TecnicasV2() / TecnicaPermitida() / TecnicaActiva() / BonoMaestria();
  5. Textos y pestanas de UI de Coleccion y Habitacion (heroe_v2_coleccion.inl, heroe_v2_habitacion.inl) vs heroe_recompensas.inl.

Cada discrepancia se verifica directamente contra las lineas actuales de src/ y res/.

Uso:
  python -X utf8 juez_coleccion_tecnicas.py [--json salida.json] [--skip-afs]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import heroe_datos as hd  # noqa: E402


def _find_line(path: Path, pattern: str) -> int:
    if not path.is_file():
        return -1
    for idx, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        if re.search(pattern, line):
            return idx
    return -1


def _find_toml_id_line(path: Path, item_id: str) -> int:
    return _find_line(path, rf'^\s*id\s*=\s*"{re.escape(item_id)}"')


def judge_all(data: dict[str, Any]) -> dict[str, Any]:
    root = data["root"]
    tramos = data["tramos"]
    canon = data["canon_sagas"]
    desbs = data["desbloqueos"]
    recs = data["recompensas"]
    cpp = data["cpp"]
    afs = data["afs"]

    mismatches: list[dict[str, Any]] = []

    def add(
        mid: str,
        area: str,
        severity: str,
        ui_where: str,
        ui_promise: str,
        logic_where: str,
        real_logic: str,
    ) -> None:
        mismatches.append(
            {
                "id": mid,
                "area": area,
                "severity": severity,
                "ui_where": ui_where,
                "ui_promise": ui_promise,
                "logic_where": logic_where,
                "real_logic": real_logic,
            }
        )

    p_rec = data["paths"]["recompensas"]
    p_desb = data["paths"]["desbloqueos"]
    p_sagas = data["paths"]["sagas"]
    p_heroe = root / "src" / "heroe.cpp"
    p_rec_inl = root / "src" / "heroe_recompensas.inl"
    p_col_inl = root / "src" / "heroe_v2_coleccion.inl"
    p_hab_inl = root / "src" / "heroe_v2_habitacion.inl"
    p_evo_inl = root / "src" / "heroe_v2_evolucion.inl"
    p_mae_inl = root / "src" / "heroe_v2_maestros.inl"
    p_tec_inl = root / "src" / "heroe_tecnicas_v2.inl"
    p_v2t_inl = root / "src" / "heroe_v2_tecnicas.inl"
    p_ui = root / "src" / "heroe_ui.cpp"
    p_test_rec = root / "tools" / "test_heroe_recompensas.py"

    # Compute reachable marks & rivals from sagas.toml dynamically
    all_saga_rivals = {int(c["rival"]) for t in tramos for c in t.get("combate", [])}
    all_saga_rival_forms = {(int(c["rival"]), int(c.get("forma", 0))) for t in tramos for c in t.get("combate", [])}
    all_saga_marks = set()
    for t in tramos:
        if t.get("marca"):
            all_saga_marks.add(t["marca"])
        if t.get("opcional_marca"):
            all_saga_marks.add(t["opcional_marca"])
    for mm in cpp["marca_maestro"]:
        all_saga_marks.add("maestro_" + mm)
    all_saga_marks.add("deseo_revivir")
    for rv in all_saga_rivals:
        all_saga_marks.add(f"vence_{rv}")

    canon_rivals = {int(c["rival"]) for s in canon for c in s.get("combate", [])}
    maestro_ids = {m["id"] for m in cpp["maestros"]}
    unlockable_donors = canon_rivals | maestro_ids

    # ---- 1. COLLECTION & REWARDS (recompensas.toml + heroe_v2_coleccion.inl + heroe_recompensas.inl) ----

    # J-01: r_h100 self-referential deadlock
    for r in recs:
        if "coleccion" in r and int(r["coleccion"]) == 100 and not data["cpp"]["hito_sin_ciclo"]:
            ln = _find_toml_id_line(p_rec, r["id"])
            ln_pct = _find_line(p_rec_inl, r"int ColeccionPorcentaje\(")
            add(
                "J-01",
                "Coleccion",
                "ALTA",
                f"res/heroe/recompensas.toml:{ln} ({r['id']}: '{r.get('cond_es', '')}')",
                f"Al alcanzar {r['coleccion']}% de la Coleccion, se otorga '{r.get('nombre_es', '')}'.",
                f"src/heroe_recompensas.inl:{ln_pct} (ColeccionPorcentaje) y :{_find_line(p_rec_inl, r'pct < r\.coleccion')}",
                f"ColeccionPorcentaje() = 100 * n / {len(recs)} incluye a '{r['id']}' en el denominador. "
                f"Con los otros {len(recs)-1} premios conseguidos, pct = {100*(len(recs)-1)//len(recs)}% < 100%: JAMAS se desbloquea.",
            )

    # J-02: Stage reward consumer never called
    esc_callers = []
    for p in (root / "src").rglob("*.cpp"):
        if "EscenarioPremiadoDisponible" in p.read_text(encoding="utf-8", errors="replace"):
            esc_callers.append(p.name)
    for r in recs:
        if r.get("tipo") == "escenario" and not esc_callers:
            ln = _find_toml_id_line(p_rec, r["id"])
            ln_fn = _find_line(p_rec_inl, r"bool EscenarioPremiadoDisponible\(")
            add(
                "J-02",
                "Coleccion",
                "MEDIA",
                f"res/heroe/recompensas.toml:{ln} ({r['id']}: '{r.get('nombre_es', '')}')",
                f"Promete desbloquear el escenario {r.get('escenario')} al cerrar el tramo {r.get('tramo')}.",
                f"src/heroe_recompensas.inl:{ln_fn} (0 llamadores en src/*.cpp)",
                "EscenarioPremiadoDisponible() no se llama desde src/escenarios.cpp ni ningun otro .cpp: el escenario no esta bloqueado ni cambia nada al ganarlo.",
            )

    # J-03: Luchador subtitle missing in Coleccion UI & rejected by test_heroe_recompensas.py
    col_txt = p_col_inl.read_text(encoding="utf-8")
    if 'c.tipo == 3' in col_txt and 'c.tipo == 4' not in col_txt:
        ln_sub = _find_line(p_col_inl, r'if \(c\.tipo == 3\)')
        add(
            "J-03",
            "Coleccion",
            "BAJA",
            f"src/heroe_v2_coleccion.inl:{ln_sub}",
            "Pestana 6 ('Luchadores') muestra la ficha detallada del premio seleccionado con su subtitulo de tipo.",
            f"src/heroe_v2_coleccion.inl:{ln_sub} (falta rama c.tipo == 4)",
            "CeldasCol() asigna c.tipo = 4 para pest == 6 ('luchador'), pero la ficha solo cubre tipo 0..3: 'sub' queda vacio para Omega Shenron.",
        )
    if p_test_rec.is_file() and '"luchador"' not in p_test_rec.read_text(encoding="utf-8"):
        ln_tr = _find_line(p_test_rec, r'not in \("tecnica"')
        add(
            "J-04",
            "Coleccion",
            "BAJA",
            f"res/heroe/recompensas.toml:{_find_toml_id_line(p_rec, 'r_luchador_omega')} (tipo = 'luchador')",
            "r_luchador_omega usa tipo = 'luchador' (soportado en heroe_recompensas.inl:45).",
            f"tools/test_heroe_recompensas.py:{ln_tr}",
            "El validador oficial tools/test_heroe_recompensas.py solo admite ('tecnica', 'titulo', 'escenario') y falla con exit 1 sobre datos validos.",
        )

    # J-05: Empty Collection tabs 0 and 3 & Room description vs ColeccionPorcentaje
    ln_celdas = _find_line(p_col_inl, r"if \(pest == 1 \|\| pest == 4")
    ln_hab_desc = _find_line(p_hab_inl, r"case 5: return T::T\(")
    add(
        "J-05",
        "Coleccion",
        "MEDIA",
        f"src/heroe_v2_habitacion.inl:{ln_hab_desc} y src/heroe_v2_coleccion.inl:12,19",
        "Habitacion describe Coleccion como 'Tecnicas, titulos y formas que has ganado'; Coleccion muestra 7 pestanas navegables (incl. 'Trajes y partes' y 'Capsulas').",
        f"src/heroe_v2_coleccion.inl:{ln_celdas} y src/heroe_recompensas.inl:{_find_line(p_rec_inl, r'int ColeccionPorcentaje')}",
        "1) Pestanas 0 ('Trajes y partes') y 3 ('Capsulas') no tienen rama en CeldasCol() y SIEMPRE salen vacias. "
        "2) Pestana 2 ('Formas') solo muestra las formas de la raza activa y NO suma al % de ColeccionPorcentaje().",
    )

    # J-06: Reward techniques whose donor is NEVER unlocked -> unusable in combat!
    if afs["available"]:
        unusable_rewards = []
        for r in recs:
            if r.get("tipo") == "tecnica":
                cap = int(r["capsula"])
                info = afs["attacks"].get(cap)
                if info and not any(o in unlockable_donors for o in info["owners"]):
                    unusable_rewards.append((r["id"], cap, info["owners"], [hd.pj_name(data, o) for o in info["owners"]]))
        for rid, cap, owners, names in unusable_rewards:
            ln = _find_toml_id_line(p_rec, rid)
            add(
                f"J-06-{rid}",
                "Coleccion/Tecnicas",
                "ALTA",
                f"res/heroe/recompensas.toml:{ln} ({rid}, capsula {cap})",
                f"Premia al jugador con la tecnica '{next(r['nombre_es'] for r in recs if r['id']==rid)}' (capsula {cap}, de {', '.join(names)}).",
                f"src/heroe.cpp:{_find_line(p_heroe, r'bool Desbloqueado\(int id\)')} y :{_find_line(p_heroe, r'bool TecnicaActiva\(int cap\)')}",
                f"Ningun dueno de la capsula {cap} ({owners} = {', '.join(names)}) es maestro ni aparece como rival en sagas.toml: "
                "Desbloqueado() devuelve false, nunca entra en Movesets() ni Estilos(), y TecnicaActiva() es SIEMPRE false (no se puede lanzar en combate!).",
            )

    # J-07: Collection rewards stored per-slot while rutas cannot replay branches
    ln_col_hdr = _find_line(p_rec_inl, r"g_desbloqueos\.push_back\(r\.id\)")
    ln_dup = _find_line(p_heroe, r"cierre repetido de etapa")
    add(
        "J-07",
        "Coleccion",
        "ALTA",
        f"src/heroe_v2_coleccion.inl:108-128 (barra de hitos 25/50/75/100%)",
        "La UI muestra los hitos 25/50/75/100% y los premios de los 14 tramos (0..13) como coleccionables de la ficha.",
        f"src/heroe_recompensas.inl:{ln_col_hdr}, src/heroe.cpp:{ln_dup - 1} y :{_find_line(p_heroe, r'HacerSitioDesbloqueos')}",
        "1) Los premios se guardan por ficha en cac.toml (tope 64 IDs; HacerSitioDesbloqueos() borra premios antiguos cuando se llena). "
        "2) En DU+Rutas una ficha solo juega 1 tramo por etapa y nunca reinicia g_busqueda -> al menos 4 premios de tramo variante son mutuamente excluyentes en una misma ficha.",
    )

    # ---- 2. EVOLUTION & DESBLOQUEOS (desbloqueos.toml vs heroe.cpp / heroe_formas.h) ----
    for d in desbs:
        did = d["id"]
        dtipo = d.get("tipo", "")
        ln = _find_toml_id_line(p_desb, did)
        cond_es = d.get("cond_es", d.get("nombre_es", ""))
        cond_en = d.get("cond_en", d.get("nombre_en", ""))

        # Specific textual vs data checks (generic + exact)
        if dtipo == "forma":
            mk = d.get("marca", "")
            vence = int(d.get("vence", -1))
            cap = int(d.get("capsula", 0))
            # Check unreachable mark
            if mk and mk not in all_saga_marks:
                add(
                    f"J-EVO-MARK-{did}",
                    "Evolucion",
                    "ALTA",
                    f"res/heroe/desbloqueos.toml:{ln} ({did}: '{cond_es}')",
                    f"Promete abrir '{d.get('nombre_es', did)}' cumpliendo: '{cond_es}'.",
                    f"src/heroe_formas.h:28 (Alcanzable exige marca='{mk}') y res/heroe/sagas.toml",
                    f"La marca '{mk}' NO existe ni se asigna en ningun tramo de sagas.toml ni en src/heroe.cpp: forma permanentemente cerrada.",
                )
            # Check unreachable rival
            if vence >= 0 and vence not in all_saga_rivals:
                add(
                    f"J-EVO-RIVAL-{did}",
                    "Evolucion",
                    "ALTA",
                    f"res/heroe/desbloqueos.toml:{ln} ({did}: '{cond_es}')",
                    f"Promete abrir '{d.get('nombre_es', did)}' cumpliendo: '{cond_es}'.",
                    f"src/heroe_formas.h:36 (exige vence={vence} = {hd.pj_name(data, vence)}) y res/heroe/sagas.toml",
                    f"El rival {vence} ({hd.pj_name(data, vence)}) NO aparece en ningun combate de sagas.toml"
                    + (" — ademas el texto dice 'Gohan' pero vence=37 es Dabura!" if "Gohan" in cond_es and vence == 37 else "."),
                )
            # Check text promising optional fight when data does not check opc_namek
            if "opcional de Namek" in cond_es and mk != "opc_namek":
                add(
                    f"J-EVO-TXT-{did}",
                    "Evolucion",
                    "MEDIA",
                    f"res/heroe/desbloqueos.toml:{ln} ({did}: '{cond_es}')",
                    f"Texto promete dos requisitos: '{cond_es}'.",
                    f"src/heroe_formas.h:28 (solo comprueba marca='{mk}')",
                    f"El codigo solo comprueba una marca ('{mk}'); NO exige 'opc_namek' ni el combate opcional de Namek.",
                )
            # Check text promising "any rival" while data hardcodes Cell (33)
            if ("un rival" in cond_es.lower() or "a rival" in cond_en.lower()) and vence >= 0:
                add(
                    f"J-EVO-ANY-{did}",
                    "Evolucion",
                    "MEDIA",
                    f"res/heroe/desbloqueos.toml:{ln} ({did}: '{cond_es}')",
                    f"Texto promete vencer a cualquier rival: '{cond_es}'.",
                    f"res/heroe/desbloqueos.toml:{ln} (vence = {vence} = {hd.pj_name(data, vence)})",
                    f"La logica exige vencer especificamente al rival ID {vence} ({hd.pj_name(data, vence)}), no a cualquier rival.",
                )
            # Check text promising stage/location while data only checks rival
            if ("en el Ring" in cond_es or "en el Laboratorio" in cond_es) and vence >= 0:
                add(
                    f"J-EVO-LOC-{did}",
                    "Evolucion",
                    "BAJA",
                    f"res/heroe/desbloqueos.toml:{ln} ({did}: '{cond_es}')",
                    f"Texto menciona un lugar especifico: '{cond_es}'.",
                    "src/heroe_formas.h:34-37 (CumpleCondicion solo recibe rival, no escenario ni tramo)",
                    f"La logica solo comprueba rival=={vence} ({hd.pj_name(data, vence)}) en cualquier escenario o saga (y f_fre_3 + f_fre_final se abren a la vez en cadena en una sola victoria contra Cell).",
                )
            # Check forms with capsula == 0 (no combat effect)
            if cap == 0:
                add(
                    f"J-EVO-CAP0-{did}",
                    "Evolucion",
                    "MEDIA",
                    f"res/heroe/desbloqueos.toml:{ln} ({did}: '{d.get('nombre_es', did)}')",
                    f"Aparece como forma de la raza {d.get('raza')} en el arbol de Evolucion.",
                    f"src/heroe.cpp:{_find_line(p_heroe, r'uint32_t FormasCombate')} (ignora d.capsula <= 0)",
                    "Tiene capsula = 0: FormasCombate() la ignora y nunca otorga ninguna transformacion ni efecto en combate.",
                )
        else:
            # Non-forma unlocks
            dm = int(d.get("maestro", -1))
            dv = int(d.get("vence", -1))
            df = int(d.get("forma", -1))
            if dm >= 0 and dm not in maestro_ids:
                add(
                    f"J-DESB-MAS-{did}",
                    "Maestros/Desbloqueos",
                    "ALTA",
                    f"res/heroe/desbloqueos.toml:{ln} ({did}: '{d.get('nombre_es', did)}')",
                    f"Promete desbloquear '{d.get('nombre_es', did)}' entrenando con maestro {dm} ({hd.pj_name(data, dm)}).",
                    f"src/heroe.cpp:{_find_line(p_heroe, r'constexpr int kNumMaestros')} (kNumMaestros=6; MaestroIndice({dm}) == -1)",
                    f"{hd.pj_name(data, dm)} (id={dm}) NO es maestro seleccionable en kEstilos[0..5]: desbloqueo imposible.",
                )
            if dv >= 0:
                if df >= 0 and (dv, df) not in all_saga_rival_forms:
                    add(
                        f"J-DESB-RIVF-{did}",
                        "Maestros/Tecnicas",
                        "ALTA",
                        f"res/heroe/desbloqueos.toml:{ln} ({did}: '{d.get('nombre_es', did)}')",
                        f"Promete '{d.get('nombre_es', did)}' al vencer a {hd.pj_name(data, dv)} en forma {df}.",
                        f"src/heroe.cpp:{_find_line(p_heroe, r'if \(d\.forma >= 0')} y res/heroe/sagas.toml",
                        f"Ningun combate de sagas.toml enfrenta a rival={dv} ({hd.pj_name(data, dv)}) con forma={df}: desbloqueo imposible.",
                    )
                elif df < 0 and dv not in all_saga_rivals:
                    add(
                        f"J-DESB-RIV-{did}",
                        "Evolucion/SSJ",
                        "ALTA",
                        f"res/heroe/desbloqueos.toml:{ln} ({did}: '{d.get('nombre_es', did)}')",
                        f"Promete '{d.get('nombre_es', did)}' al vencer a {hd.pj_name(data, dv)}.",
                        f"src/heroe.cpp:{_find_line(p_heroe, r'if \(d\.vence >= 0')} y res/heroe/sagas.toml",
                        f"El rival {dv} ({hd.pj_name(data, dv)}) no aparece en ningun combate de sagas.toml: desbloqueo imposible.",
                    )

    # ---- 3. MASTERS UI vs LOGIC (heroe_v2_maestros.inl, heroe_ui.cpp, heroe.cpp) ----
    ln_m77 = _find_line(p_mae_inl, r"puedes cambiar de maestro")
    ln_m133 = _find_line(p_mae_inl, r"Cambia de maestro cuando quieras")
    ln_puede = _find_line(p_heroe, r"bool PuedeCambiarA\(")
    if ln_m77 > 0:
        add(
            "J-MAE-01",
            "Maestros",
            "MEDIA",
            f"src/heroe_v2_maestros.inl:{ln_m77} (vs :{ln_m133})",
            "Bajo la barra dice '60 %: a partir de ahi puedes cambiar de maestro', mientras abajo dice 'Cambia de maestro cuando quieras; a 60% abres su tecnica clave'.",
            f"src/heroe.cpp:{ln_puede} (PuedeCambiarA)",
            "Contradiccion interna en la misma pantalla: PuedeCambiarA() NO comprueba el 60% de entrenamiento (solo Desbloqueado && AfinidadRuta). El texto de la linea 77 es resto obsoleto de la regla antigua (igual que heroe_ui.cpp:4017 y el log de heroe.cpp:3103).",
        )

    ln_aviso = _find_line(p_ui, r"AvisoAfinidadMaestro\(")
    ln_afin = _find_line(p_heroe, r"bool AfinidadRuta\(")
    add(
        "J-MAE-02",
        "Maestros",
        "MEDIA",
        f"src/heroe_ui.cpp:{ln_aviso} (AvisoAfinidadMaestro)",
        "Muestra el texto generico 'Aun te falta una decision o un reto de su ruta.' para cualquier maestro bloqueado por afinidad.",
        f"src/heroe.cpp:{ln_afin}-{ln_afin+10} (AfinidadRuta)",
        "Cada maestro tiene una condicion concreta (Vegeta: opc_namek|camino_vegeta; Piccolo/Krilin: opc_tierra; Freezer: camino_nave; Cell: vence_33), pero la UI nunca informa al jugador que decision/reto necesita.",
    )

    ln_entrena_v2 = _find_line(p_mae_inl, r"H::MaestroQueEntrena\(s\)")
    add(
        "J-MAE-03",
        "Maestros",
        "BAJA",
        f"src/heroe_v2_maestros.inl:{ln_entrena_v2} y src/heroe_ui.cpp:{_find_line(p_ui, r'H::MaestroQueEntrena\(s\)')}",
        "Muestra 'Dragon Universe: <sagas>' donde entrena cada maestro.",
        f"src/heroe.cpp:{_find_line(p_heroe, r'int MaestroQueEntrena\(')} (usa SagaDe, solo 0..7 canonicas)",
        "Solo recorre las 8 sagas canonicas y omite las variantes 8..12 de rutas (por ejemplo, tramo 8 entrena a Goku, 9..11 a Cell, 12 a Goku).",
    )

    # Deadlock between Desbloqueado(m) and Acto 2 route branches
    ln_desb_m = _find_line(p_heroe, r"const int k = SagaDelMaestro\(m\)")
    add(
        "J-MAE-04",
        "Maestros/Rutas",
        "ALTA",
        "res/heroe/sagas.toml:198-199 (Saga de Baby: condicion = 'maestro_vegeta|maestro_freezer') y Maestros UI",
        "Al entrar al Acto 2 (etapa 2, g_saga=2), el jugador deberia poder haber elegido a Vegeta (con afinidad camino_vegeta/opc_namek) o Freezer (camino_nave) o Krilin (opc_tierra).",
        f"src/heroe.cpp:{ln_desb_m} (return k < 0 || g_saga >= k)",
        "En partida nueva de DU+Rutas, al terminar Acto 1 g_saga==2: Vegeta pide g_saga>=3 (Saga de Baby!), Freezer pide g_saga>=4 (Omega) y Krilin pide g_saga>=5 (Reunion). "
        "Aunque el jugador cumpla AfinidadRuta() en Acto 1, Desbloqueado() los bloquea hasta DESPUES de la etapa 2 -> Saga de Baby es inalcanzable en DU+Rutas y Krilin nunca abre maestro_krilin en Acto 2.",
    )

    # ---- 4. TECHNIQUES UI vs LOGIC (heroe_v2_tecnicas.inl, heroe_tecnicas_v2.inl, heroe_ui.cpp) ----
    ln_f139 = _find_line(p_v2t_inl, r"x\.cap == 139")
    add(
        "J-TEC-01",
        "Tecnicas",
        "MEDIA",
        f"src/heroe_v2_tecnicas.inl:{ln_f139}",
        "Muestra 'solo Saiyan' para Spirit of Saiyans (cap 139) si el heroe no es Saiyan.",
        f"res/heroe/razas_reglas.toml:6 (tecnicas_completas = false) y src/cac/razas_reglas.h:26",
        "TecnicaPermitida(139) devuelve true para TODAS las razas mientras tecnicas_completas=false: la UI dice 'solo Saiyan', pero la logica no filtra por raza.",
    )

    ln_cond_tec = _find_line(p_v2t_inl, r'if \(x\.maestro < 0\) return')
    ln_own_style = _find_line(p_tec_inl, r'if \(MaestroIndice\(EstiloId\(\)\) < 0\)')
    add(
        "J-TEC-02",
        "Tecnicas",
        "BAJA",
        f"src/heroe_v2_tecnicas.inl:{ln_cond_tec}",
        "Etiqueta cualquier tecnica con maestro < 0 y escuela < 0 como 'Premio del camino' / 'Path reward'.",
        f"src/heroe_tecnicas_v2.inl:{ln_own_style}-{ln_own_style+5}",
        "Las tecnicas del estilo propio no-maestro tambien entran con maestro=-1 y escuela=-1, por lo que aparecen falsamente etiquetadas como 'Premio del camino'.",
    )

    ln_maestria_cvar = _find_line(p_tec_inl, r"REXCVAR_DEFINE_BOOL\(dbz3_heroe_maestria_dano")
    ln_nivel_ui = _find_line(p_v2t_inl, r"victorias más para Nv")
    add(
        "J-TEC-03",
        "Tecnicas",
        "MEDIA",
        f"src/heroe_v2_tecnicas.inl:{ln_nivel_ui}",
        "Muestra estrellas Nv 1..3 y cuenta 'X victorias mas para Nv 2/3' para cada tecnica equipada.",
        f"src/heroe_tecnicas_v2.inl:{ln_maestria_cvar} (dbz3_heroe_maestria_dano = false por defecto)",
        "El bono de dano por maestria (+2/+4%, tope +6%) esta APAGADO por defecto: subir de nivel las tecnicas no tiene efecto jugable de serie y la UI no indica que el nivel sea cosmetico ni que bono otorga.",
    )

    ln_moji = _find_line(p_ui, r'kNomHueco\[3\]')
    if ln_moji > 0:
        line_txt = p_ui.read_text(encoding="utf-8").splitlines()[ln_moji - 1]
        if "\u00e2\u0086" in line_txt or "â†" in line_txt:
            add(
                "J-TEC-04",
                "Tecnicas",
                "BAJA",
                f"src/heroe_ui.cpp:{ln_moji}",
                "Pantalla clasica de Tecnicas nombra los huecos 1 y 2 ('E + derecha' / 'E + izquierda').",
                f"src/heroe_ui.cpp:{ln_moji} (literal corrupto + sin T::T)",
                "Contiene flechas UTF-8 doblemente codificadas ('E + â†’', 'E + â† ') y no pasa los huecos 1 y 2 por T::T().",
            )

    # J-TEC-05: 60% dodge vs 100% master passive overlap for Krillin cap 445
    add(
        "J-TEC-05",
        "Tecnicas/Maestros",
        "BAJA",
        f"res/heroe/desbloqueos.toml:{_find_toml_id_line(p_desb, 'esquiva_concentracion_krilin')} vs src/heroe.cpp:120",
        "Esquiva 'Concentration' (cap 445) se desbloquea al 60% con Krilin; la pantalla Maestros dice 'Capsula Concentration: se desbloquea al 100%'.",
        "src/heroe.cpp:120 (kEstilos[3].hab = 445) y src/heroe_tecnicas_v2.inl:170-175",
        "La misma capsula 445 es simultaneamente el premio de Esquiva al 60% de Krilin y su capsula de maestro al 100% (y ademas activa desde 0% mientras Krilin es tu estilo en Reglas:2207).",
    )

    # J-EVO-R6: Race traits text (heroe_v2_evolucion.inl:22-29) vs real combat mechanics!
    ln_rasgo = _find_line(p_evo_inl, r"kRasgoEs\[kRazasEvo\]")
    add(
        "J-EVO-TRAITS",
        "Evolucion",
        "MEDIA",
        f"src/heroe_v2_evolucion.inl:{ln_rasgo}-{ln_rasgo+7} (kRasgoEs/kRasgoEn)",
        "Promete 6 rasgos mecanicos de raza en combate (Saiyan zenkai, Humano mas tecnicas/guardia, Namek regeneracion, Freezer 1 forma/saga, Majin mucha vida/poco ataque, Androide no gasta ki al cargar).",
        "src/heroe.cpp:2194-2305 (Reglas) y :1537-1541 (Vida)",
        "Reglas() y Vida() dependen SOLO del estilo (kEstilos), nivel y PH — NINGUNO de esos 6 rasgos raciales esta implementado en el combate (son texto puramente descriptivo).",
    )

    if data["cpp"]["acceso_por_afinidad"]:
        mismatches = [m for m in mismatches if m["id"] != "J-MAE-04"]
    by_sev: dict[str, int] = {}
    by_area: dict[str, int] = {}
    for m in mismatches:
        by_sev[m["severity"]] = by_sev.get(m["severity"], 0) + 1
        by_area[m["area"]] = by_area.get(m["area"], 0) + 1

    return {
        "summary": {
            "total_mismatches": len(mismatches),
            "by_severity": by_sev,
            "by_area": by_area,
        },
        "mismatches": mismatches,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Juez de Coleccion, Evolucion, Maestros y Tecnicas del Modo Heroe")
    ap.add_argument("--root", type=Path)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--skip-afs", action="store_true")
    ap.add_argument("--autoprueba", action="store_true", help="ejecutar el cotejo estructural sin AFS")
    args = ap.parse_args()

    data = hd.load_all(root=args.root, skip_afs=args.skip_afs or args.autoprueba)
    rep = judge_all(data)
    if args.autoprueba:
        ids = {m["id"] for m in rep["mismatches"]}
        assert len(ids) == rep["summary"]["total_mismatches"], ids
        assert ("J-01" not in ids) == data["cpp"]["hito_sin_ciclo"]
        print("Juez de Colección y Técnicas: autoprueba OK")
        return 0

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(rep, ensure_ascii=False, indent=2), encoding="utf-8")

    s = rep["summary"]
    print(f"=== JUEZ DE COLECCION Y TECNICAS: {s['total_mismatches']} discrepancias ({s['by_severity']}) ===")
    for m in rep["mismatches"]:
        print(f"[{m['severity']}] {m['id']} ({m['area']})")
        print(f"   UI ({m['ui_where']}): {m['ui_promise']}")
        print(f"   Codigo ({m['logic_where']}): {m['real_logic']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

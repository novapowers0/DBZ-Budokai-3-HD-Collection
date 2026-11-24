#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""grafo_progreso.py — Grafo completo de progreso del Modo Heroe (reutilizable y escalable).

Lee automaticamente res/heroe/{sagas,desbloqueos,recompensas,razas_reglas}.toml,
res/heroe/sagas_i18n.toml, res/ingame_ui/i18n_modos.toml, el catalogo #SKC/#CCM real
y las reglas de src/heroe*.cpp/.inl.

Construye el grafo dirigido (nodos + aristas con requisito) y detecta:
  1. Recompensas, desbloqueos y formas inalcanzables;
  2. Tecnicas sin maestro o inaccesibles;
  3. Maestros sin saga (o sagas sin maestro / maestros no registrados);
  4. Ciclos (incluidos ciclos de 1 nodo / bloqueos autorreferenciales como hitos de Coleccion);
  5. Requisitos imposibles para algun cuerpo o estilo;
  6. Textos ausentes en alguno de los 7 idiomas (es, en, fr, de, it, pt, ja).

Uso:
  python -X utf8 grafo_progreso.py [--sagas ...] [--desbloqueos ...] [--recompensas ...]
                                   [--json salida.json] [--mermaid salida.mmd] [--skip-afs]
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict, deque
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import heroe_datos as hd  # noqa: E402


class ProgressGraph:
    def __init__(self, data: dict[str, Any]):
        self.data = data
        self.nodes: dict[str, dict[str, Any]] = {}
        self.edges: list[dict[str, Any]] = []
        self.adj: dict[str, list[dict[str, Any]]] = defaultdict(list)

    def add_node(self, nid: str, kind: str, label: str, **meta: Any) -> None:
        self.nodes[nid] = {"id": nid, "kind": kind, "label": label, **meta}

    def add_edge(self, src: str, dst: str, rel: str, req: str = "") -> None:
        e = {"from": src, "to": dst, "rel": rel, "req": req}
        self.edges.append(e)
        self.adj[src].append(e)


def _reachable_tramos_and_marks(data: dict[str, Any]) -> dict[str, Any]:
    """Exploracion exacta de alcanzabilidad en el grafo de rutas (DBZ3_HEROE_RUTAS=true) y modo clasico."""
    hs = data["hs"]
    tramos = data["tramos"]
    canon = data["canon_sagas"]
    cpp = data["cpp"]
    num_m = cpp["num_maestros"]
    maestro_ids = [m["id"] for m in cpp["maestros"]]
    marca_maestro = cpp["marca_maestro"]

    # Classic / Misiones mode (rutas=false): sagas 0..kSagasJugables-1
    jugables = min(len(canon), cpp["consts"].get("kSagasJugables", 8))
    classic_tramos = set(range(jugables))
    classic_rivals = set()
    classic_rival_forms = set()
    classic_masters_trained = set()
    for i in range(jugables):
        s = canon[i]
        m = int(s.get("maestro", -1))
        if 0 <= m < num_m:
            classic_masters_trained.add(m)
        for c in s.get("combate", []):
            r = int(c["rival"])
            fm = int(c.get("forma", 0))
            classic_rivals.add(r)
            classic_rival_forms.add((r, fm))

    # Routes mode (rutas=true): BFS over (etapa_sig, frozenset(marcas), saga_count, decision)
    # Track all tramos, marks, rivals, (rival, forma), masters reachable across ANY legal run.
    def afinidad(i: int, marcas: set[str]) -> bool:
        if i == 0:
            return True
        if i == 1:
            return "opc_namek" in marcas or "camino_vegeta" in marcas
        if i in (2, 3):
            return "opc_tierra" in marcas
        if i == 4:
            return "camino_nave" in marcas
        if i == 5:
            return "vence_33" in marcas
        return False

    def saga_del_maestro(m_idx: int) -> int:
        for i, s in enumerate(canon):
            if int(s.get("maestro", -1)) == m_idx:
                return i
        return -1

    def master_unlocked(m_idx: int, g_saga: int, trained: set[int], marcas: set[str]) -> bool:
        if m_idx == 0 or m_idx in trained or (cpp["acceso_por_afinidad"] and afinidad(m_idx, marcas)):
            return True
        k = saga_del_maestro(m_idx)
        return k < 0 or g_saga >= k

    # BFS over exact single-run route states
    # State: (etapa_sig, frozenset(marcas), g_saga, frozenset(trained), decision)
    # decision: 0 none yet, 1 revivir, 2 seguir
    init_state = (0, frozenset(), 0, frozenset(), 0)
    visited = {init_state}
    queue = deque([init_state])

    routes_tramos: set[int] = set()
    routes_marks: set[str] = set()
    routes_rivals: set[int] = set()
    routes_rival_forms: set[tuple[int, int]] = set()
    routes_masters_trained: set[int] = set()
    routes_masters_selectable: set[int] = {0}
    endings_reached: set[int] = set()

    max_etapa = max((int(t.get("etapa", -1)) for t in tramos), default=5)
    while queue:
        etapa_sig, marcas_f, g_saga, trained_f, decision = queue.popleft()
        marcas = set(marcas_f)
        trained = set(trained_f)
        routes_marks.update(marcas)
        routes_masters_trained.update(trained)

        # At etapa_sig == 5 and decision == 0, player must make story wish (revivir=True or False)
        if etapa_sig == 5 and decision == 0:
            for dec in (1, 2):
                nm = set(marcas)
                if dec == 1:
                    nm.add("deseo_revivir")
                st = (5, frozenset(nm), g_saga, trained_f, dec)
                if st not in visited:
                    visited.add(st)
                    queue.append(st)
            continue

        # Before entering etapa_sig >= 2, if no maestro_* mark yet, ResolverRuta inserts maestro_<MaestroActual>
        has_maestro_mark = any(x.startswith("maestro_") for x in marcas)
        master_choices: list[set[str]] = [marcas]
        if etapa_sig >= 2 and not has_maestro_mark:
            master_choices = []
            for m_idx in range(num_m):
                if master_unlocked(m_idx, g_saga, trained, marcas) and afinidad(m_idx, marcas):
                    routes_masters_selectable.add(m_idx)
                    nm = set(marcas)
                    nm.add("maestro_" + marca_maestro[m_idx])
                    master_choices.append(nm)

        for m_set in master_choices:
            # Also check which masters are selectable at this point
            for m_idx in range(num_m):
                if master_unlocked(m_idx, g_saga, trained, m_set) and afinidad(m_idx, m_set):
                    routes_masters_selectable.add(m_idx)

            tramo_idx, cruce = hs.ruta_siguiente(tramos, etapa_sig, m_set)
            if tramo_idx < 0:
                continue

            branch_tramos = cruce if len(cruce) >= 2 else [tramo_idx]
            for bt in branch_tramos:
                routes_tramos.add(bt)
                tdef = tramos[bt]
                et = int(tdef.get("etapa", -1))
                # Mark set on ElegirCamino / ElegirTramo
                after_marks = set(m_set)
                if tdef.get("marca"):
                    after_marks.add(tdef["marca"])
                # Combat rivals and master training
                after_trained = set(trained)
                m_saga = int(tdef.get("maestro", -1))
                if 0 <= m_saga < num_m:
                    after_trained.add(m_saga)
                for c in tdef.get("combate", []):
                    rv = int(c["rival"])
                    fm = int(c.get("forma", 0))
                    routes_rivals.add(rv)
                    routes_rival_forms.add((rv, fm))
                    after_marks.add(f"vence_{rv}")
                # Master sparring mark on map gives ComprobarDesbloqueos(kEstilos[m].id, 0)
                if 0 <= m_saga < num_m:
                    routes_rivals.add(maestro_ids[m_saga])
                    routes_rival_forms.add((maestro_ids[m_saga], 0))

                if et == max_etapa:
                    endings_reached.add(bt)

                # Optional fight either skipped or completed
                opt_variants = [after_marks]
                if tdef.get("opcional") and tdef.get("opcional_marca"):
                    with_opt = set(after_marks)
                    with_opt.add(tdef["opcional_marca"])
                    opt_variants.append(with_opt)

                for om in opt_variants:
                    routes_marks.update(om)
                    for m_idx in range(num_m):
                        if master_unlocked(m_idx, g_saga + 1, after_trained, om) and afinidad(m_idx, om):
                            routes_masters_selectable.add(m_idx)
                    next_st = (et + 1, frozenset(om), g_saga + 1, frozenset(after_trained), decision)
                    if et < max_etapa and next_st not in visited:
                        visited.add(next_st)
                        queue.append(next_st)

    return {
        "classic": {
            "tramos": sorted(classic_tramos),
            "rivals": sorted(classic_rivals),
            "rival_forms": sorted(classic_rival_forms),
            "masters_trained": sorted(classic_masters_trained),
            "marks": [],
        },
        "routes": {
            "tramos": sorted(routes_tramos),
            "rivals": sorted(routes_rivals),
            "rival_forms": sorted(routes_rival_forms),
            "masters_trained": sorted(routes_masters_trained),
            "masters_selectable": sorted(routes_masters_selectable),
            "marks": sorted(routes_marks),
            "endings": sorted(endings_reached),
            "states_explored": len(visited),
        },
    }


def build_graph_and_audit(data: dict[str, Any]) -> dict[str, Any]:
    g = ProgressGraph(data)
    cpp = data["cpp"]
    tramos = data["tramos"]
    canon = data["canon_sagas"]
    desbs = data["desbloqueos"]
    recs = data["recompensas"]
    afs = data["afs"]
    i18n = data["i18n"]
    razas_reglas = data["razas_reglas"]

    num_m = cpp["num_maestros"]
    maestros = cpp["maestros"]
    maestro_id_to_idx = {m["id"]: i for i, m in enumerate(maestros)}
    marca_maestro = cpp["marca_maestro"]

    reach = _reachable_tramos_and_marks(data)
    all_reachable_tramos = set(reach["classic"]["tramos"]) | set(reach["routes"]["tramos"])
    all_reachable_rivals = set(reach["classic"]["rivals"]) | set(reach["routes"]["rivals"])
    all_reachable_rival_forms = set(reach["classic"]["rival_forms"]) | set(reach["routes"]["rival_forms"])
    all_reachable_marks = set(reach["routes"]["marks"])
    all_reachable_masters_trained = set(reach["classic"]["masters_trained"]) | set(reach["routes"]["masters_trained"])

    # ---- 1. Add nodes & edges to the Progress Graph ----
    g.add_node("root:start", "root", "Inicio del Modo Heroe")

    # Stages / Etapas & Tramos
    etapas_present = sorted({int(t.get("etapa", -1)) for t in tramos if int(t.get("etapa", -1)) >= 0})
    for et in etapas_present:
        g.add_node(f"etapa:{et}", "etapa", f"Etapa {et}")
        if et == 0:
            g.add_edge("root:start", "etapa:0", "starts")
        elif et == 5:
            g.add_node("wish:historia", "wish", "Deseo de Shenron (7 Esferas)")
            g.add_edge("etapa:4", "wish:historia", "requires_7_balls", "esferas == 127")
            g.add_edge("wish:historia", "mark:deseo_revivir", "choice_revivir", "revivir=true")
            g.add_edge("wish:historia", "etapa:5", "opens_finale", "decision != 0")
        else:
            g.add_edge(f"etapa:{et - 1}", f"etapa:{et}", "advances_to")

    # Masters
    for i, m in enumerate(maestros):
        mid = m["id"]
        nid = f"master:{mid}"
        g.add_node(
            nid,
            "master",
            f"Maestro {hd.pj_name(data, mid)} (idx={i}, id={mid})",
            master_idx=i,
            fighter_id=mid,
            hab_cap=m["hab"],
            hab_name=m["hab_n"],
        )
        # Unlock condition: SagaDelMaestro(i)
        s_idx = next((si for si, s in enumerate(canon) if int(s.get("maestro", -1)) == i), -1)
        if i == 0:
            g.add_edge("root:start", nid, "available_from_start")
        elif s_idx >= 0:
            g.add_edge(f"tramo:{s_idx}", nid, "unlocks_master_at_saga", f"g_saga >= {s_idx}")
        else:
            g.add_edge("root:start", nid, "no_saga_gate", "SagaDelMaestro == -1")

        # Affinity gate in routes mode
        if i == 1:
            g.add_edge("mark:opc_namek", nid, "affinity_or", "opc_namek | camino_vegeta")
            g.add_edge("mark:camino_vegeta", nid, "affinity_or", "opc_namek | camino_vegeta")
        elif i in (2, 3):
            g.add_edge("mark:opc_tierra", nid, "affinity", "opc_tierra")
        elif i == 4:
            g.add_edge("mark:camino_nave", nid, "affinity", "camino_nave")
        elif i == 5:
            g.add_edge("mark:vence_33", nid, "affinity", "vence_33")

        # Master mark emitted at Acto 2 entry
        g.add_node(f"mark:maestro_{marca_maestro[i]}", "mark", f"maestro_{marca_maestro[i]}")
        g.add_edge(nid, f"mark:maestro_{marca_maestro[i]}", "sets_mark_at_etapa2", "etapa_sig >= 2")

    # Tramos / Sagas
    for idx, t in enumerate(tramos):
        tid = f"tramo:{idx}"
        et = int(t.get("etapa", -1))
        g.add_node(
            tid,
            "tramo",
            f"[{idx}] {t.get('nombre_es', '?')}",
            etapa=et,
            variante=t["_es_variante"],
            maestro=int(t.get("maestro", -1)),
            condicion=t.get("condicion", ""),
            marca=t.get("marca", ""),
            opcional_marca=t.get("opcional_marca", ""),
            planeta=int(t.get("planeta", 1)),
            n_combates=len(t.get("combate", [])),
        )
        if et >= 0:
            g.add_edge(f"etapa:{et}", tid, "candidate_tramo", t.get("condicion", ""))
        for alt in [a for a in t.get("condicion", "").split("|") if a]:
            mk = alt[1:] if alt.startswith("!") else alt
            g.add_node(f"mark:{mk}", "mark", mk)
            g.add_edge(f"mark:{mk}", tid, "requires_mark", alt)
        if t.get("marca"):
            mk = t["marca"]
            g.add_node(f"mark:{mk}", "mark", mk)
            g.add_edge(tid, f"mark:{mk}", "grants_mark", "ElegirCamino/ElegirTramo")
        if t.get("opcional_marca"):
            mk = t["opcional_marca"]
            g.add_node(f"mark:{mk}", "mark", mk)
            g.add_edge(tid, f"mark:{mk}", "grants_optional_mark", "opcional ganado")
        m_idx = int(t.get("maestro", -1))
        if 0 <= m_idx < num_m:
            mid = maestros[m_idx]["id"]
            g.add_edge(tid, f"master:{mid}", "trains_master", "+20%/combate, 100% al cierre")
        for c in t.get("combate", []):
            rv = int(c["rival"])
            fm = int(c.get("forma", 0))
            rnid = f"rival:{rv}"
            g.add_node(rnid, "rival", f"{hd.pj_name(data, rv)} (id={rv})", fighter_id=rv)
            g.add_edge(tid, rnid, "has_fight", f"forma={fm}, vida={c.get('vida', 1500)}")
            g.add_node(f"mark:vence_{rv}", "mark", f"vence_{rv}")
            g.add_edge(rnid, f"mark:vence_{rv}", "grants_mark", "DuVictoria")

    # Techniques (#SKC)
    if afs["available"]:
        for cap, info in sorted(afs["attacks"].items()):
            if cap >= 596:
                continue
            tnid = f"tech:{cap}"
            ccm_info = afs["ccm_first"].get(cap)
            g.add_node(
                tnid,
                "technique",
                f"Cap #{cap} (owners={info['owners']}, ki={info['ki']})",
                cap=cap,
                owners=info["owners"],
                donante=info["donante"],
                ki=info["ki"],
                formas=info["formas"],
                requiere=info["requiere"],
                ccm_type=ccm_info[0] if ccm_info else -1,
            )
            for own_id in info["owners"]:
                if own_id in maestro_id_to_idx:
                    m_idx = maestro_id_to_idx[own_id]
                    g.add_edge(f"master:{own_id}", tnid, "teaches_technique", f"maestro idx={m_idx}")
                elif own_id in all_reachable_rivals:
                    g.add_edge(f"rival:{own_id}", tnid, "school_unlock", "Movesets() tras vencer")
        for cap, info in sorted(afs["esquivas"].items()):
            g.add_node(f"tech:{cap}", "dodge", f"Esquiva Cap #{cap}", cap=cap, ki=info["ki"])

    # Unlocks & Forms (desbloqueos.toml)
    for d in desbs:
        did = d["id"]
        dtipo = d.get("tipo", "")
        nid = f"unlock:{did}"
        g.add_node(
            nid,
            "form" if dtipo == "forma" else "unlock",
            f"{did}: {d.get('nombre_es', did)}",
            **{k: v for k, v in d.items() if k != "id"},
        )
        if dtipo == "forma":
            if d.get("previa"):
                g.add_edge(f"unlock:{d['previa']}", nid, "requires_prev_form", d["previa"])
            if d.get("marca"):
                mk = d["marca"]
                if f"mark:{mk}" not in g.nodes:
                    g.add_node(f"mark:{mk}", "mark", mk)
                g.add_edge(f"mark:{mk}", nid, "requires_mark", mk)
            if d.get("abre_con"):
                g.add_edge(f"unlock:{d['abre_con']}", nid, "alt_unlock_by", d["abre_con"])
            if "vence" in d and int(d["vence"]) >= 0:
                rv = int(d["vence"])
                if f"rival:{rv}" not in g.nodes:
                    g.add_node(f"rival:{rv}", "rival", f"{hd.pj_name(data, rv)} (id={rv})", fighter_id=rv)
                g.add_edge(f"rival:{rv}", nid, "requires_defeat", f"vence={rv}")
        else:
            if "maestro" in d and int(d["maestro"]) >= 0:
                mid = int(d["maestro"])
                if f"master:{mid}" not in g.nodes:
                    g.add_node(f"master:{mid}", "master_missing", f"{hd.pj_name(data, mid)} (id={mid}, NO en kEstilos)")
                g.add_edge(f"master:{mid}", nid, "requires_master_pct", f"entreno>={d.get('entreno', 0)}%")
            if "vence" in d and int(d["vence"]) >= 0:
                rv = int(d["vence"])
                if f"rival:{rv}" not in g.nodes:
                    g.add_node(f"rival:{rv}", "rival", f"{hd.pj_name(data, rv)} (id={rv})", fighter_id=rv)
                fm = int(d.get("forma", -1))
                g.add_edge(f"rival:{rv}", nid, "requires_defeat", f"vence={rv}" + (f", forma={fm}" if fm >= 0 else ""))
            if dtipo == "ssj" and int(d.get("nivel", 0)) > 1:
                prev_ssj = f"ssj{int(d['nivel']) - 1}"
                g.add_edge(f"unlock:{prev_ssj}", nid, "requires_prev_ssj", f"SsjNivel >= {int(d['nivel']) - 1}")
            if dtipo == "tecnica" and int(d.get("capsula", 0)) > 0:
                cap = int(d["capsula"])
                if f"tech:{cap}" not in g.nodes:
                    g.add_node(f"tech:{cap}", "technique", f"Cap #{cap}", cap=cap)
                g.add_edge(nid, f"tech:{cap}", "unlocks_technique")

    # Rewards (recompensas.toml)
    g.add_node("collection:pct", "collection", f"Coleccion % (total={len(recs)})", total=len(recs))
    for r in recs:
        rid = r["id"]
        rnid = f"reward:{rid}"
        g.add_node(rnid, "reward", f"{rid}: {r.get('nombre_es', rid)}", **{k: v for k, v in r.items() if k != "id"})
        g.add_edge(rnid, "collection:pct", "contributes_to_pct", f"+1/{len(recs)}")
        if "tramo" in r:
            g.add_edge(f"tramo:{int(r['tramo'])}", rnid, "requires_tramo", f"tramo=={int(r['tramo'])}")
        if "marca" in r:
            mk = r["marca"]
            if f"mark:{mk}" not in g.nodes:
                g.add_node(f"mark:{mk}", "mark", mk)
            g.add_edge(f"mark:{mk}", rnid, "requires_mark", mk)
        if "vence" in r:
            rv = int(r["vence"])
            if f"rival:{rv}" not in g.nodes:
                g.add_node(f"rival:{rv}", "rival", f"{hd.pj_name(data, rv)} (id={rv})", fighter_id=rv)
            g.add_edge(f"rival:{rv}", rnid, "requires_defeat", f"vence=={rv}")
        if "maestro" in r:
            mid = int(r["maestro"])
            if f"master:{mid}" not in g.nodes:
                g.add_node(f"master:{mid}", "master_missing", f"{hd.pj_name(data, mid)} (id={mid})")
            g.add_edge(f"master:{mid}", rnid, "requires_master_pct", f"entreno>={r.get('entreno', 0)}%")
        if "coleccion" in r:
            g.add_edge("collection:pct", rnid, "requires_collection_pct", f"pct>={int(r['coleccion'])}%")
        if r.get("tipo") == "tecnica" and int(r.get("capsula", 0)) > 0:
            cap = int(r["capsula"])
            if f"tech:{cap}" not in g.nodes:
                g.add_node(f"tech:{cap}", "technique", f"Cap #{cap}", cap=cap)
            g.add_edge(rnid, f"tech:{cap}", "grants_reward_technique")

    # ---- 2. Detect all 6 required anomaly classes ----

    # (A) Unreachable rewards, unlocks, and forms
    unreachable_rewards: list[dict[str, Any]] = []
    # Fixed-point simulation of reward reachability across:
    #  (i) single DU+Rutas run, (ii) Classic/Misiones mode, (iii) union of all modes (theoretical single-slot max)
    n_rec = len(recs)
    # Note: In classic mode (rutas=false), tramo is 0..7 and marcas is empty; in DU+rutas, a single slot can only pick
    # ONE branch per stage! We compute exact reachability for each individual condition here and full simulation in simulador_progreso.py.
    non_milestone_union_ok = set()
    for r in recs:
        rid = r["id"]
        if "coleccion" in r:
            continue
        reasons = []
        if "tramo" in r and int(r["tramo"]) not in all_reachable_tramos:
            reasons.append(f"tramo {r['tramo']} inalcanzable")
        if "marca" in r and r["marca"] not in all_reachable_marks:
            reasons.append(f"marca '{r['marca']}' nunca se asigna")
        if "vence" in r and int(r["vence"]) not in all_reachable_rivals:
            reasons.append(f"rival {r['vence']} ({hd.pj_name(data, int(r['vence']))}) no aparece en ninguna saga")
        if "maestro" in r and int(r["maestro"]) not in maestro_id_to_idx:
            reasons.append(f"maestro {r['maestro']} no existe en kEstilos[0..{num_m-1}]")
        if reasons:
            unreachable_rewards.append({"id": rid, "tipo": r.get("tipo"), "motivo": "; ".join(reasons), "alcance": "global"})
        else:
            non_milestone_union_ok.add(rid)

    # Check milestone rewards with exact integer formula from heroe_recompensas.inl:145: pct = 100 * n // len(recs)
    # Even assuming ALL non-milestone rewards were somehow simultaneously held on 1 slot:
    sim_held = set(non_milestone_union_ok)
    base_ids = {r["id"] for r in recs if int(r.get("coleccion", -1)) < 100}
    changed = True
    while changed:
        changed = False
        pct = (100 * len(sim_held)) // n_rec if n_rec else 0
        for r in recs:
            if "coleccion" in r and r["id"] not in sim_held:
                umbral = int(r["coleccion"])
                cumple = (bool(base_ids) and base_ids <= sim_held) if umbral == 100 and cpp["hito_sin_ciclo"] else pct >= umbral
                if cumple:
                    sim_held.add(r["id"])
                    changed = True
    for r in recs:
        if "coleccion" in r and r["id"] not in sim_held:
            max_pct_without_self = (100 * len(sim_held)) // n_rec if n_rec else 0
            unreachable_rewards.append(
                {
                    "id": r["id"],
                    "tipo": r.get("tipo"),
                    "motivo": (
                        "No se alcanzan todos los premios base en el modelo."
                        if int(r['coleccion']) == 100 and cpp["hito_sin_ciclo"] else
                        f"Pide coleccion>={r['coleccion']}%, pero "
                        f"ColeccionPorcentaje() = 100*n/{n_rec} cuenta al propio {r['id']} en el denominador "
                        f"(maximo sin el = {len(sim_held)}/{n_rec} = {max_pct_without_self}% < {r['coleccion']}%)"
                    ),
                    "alcance": "global_matematico",
                }
            )

    # Rewards unreachable specifically in Classic/Misiones mode or mutually exclusive on 1 slot in DU+Rutas
    classic_only_unreachable = []
    for r in recs:
        rid = r["id"]
        if "tramo" in r and int(r["tramo"]) not in reach["classic"]["tramos"]:
            classic_only_unreachable.append({"id": rid, "motivo": f"tramo variante {r['tramo']} no existe sin dbz3_heroe_rutas"})
        elif "marca" in r:
            classic_only_unreachable.append({"id": rid, "motivo": f"marca '{r['marca']}' solo se otorga en DU con rutas"})

    # Unreachable desbloqueos (non-forma + forma)
    unreachable_unlocks: list[dict[str, Any]] = []
    forms_by_id = {d["id"]: d for d in desbs if d.get("tipo") == "forma"}
    # Evaluate non-forma unlocks
    non_form_ok = set()
    for d in desbs:
        if d.get("tipo") == "forma":
            continue
        did = d["id"]
        reasons = []
        if "maestro" in d and int(d["maestro"]) >= 0:
            mid = int(d["maestro"])
            if mid not in maestro_id_to_idx:
                reasons.append(f"maestro={mid} ({hd.pj_name(data, mid)}) no esta en kEstilos[0..{num_m-1}]")
        if "vence" in d and int(d["vence"]) >= 0:
            rv = int(d["vence"])
            fm = int(d.get("forma", -1))
            if fm >= 0:
                if (rv, fm) not in all_reachable_rival_forms:
                    reasons.append(f"rival={rv} ({hd.pj_name(data, rv)}) forma={fm} nunca aparece en sagas.toml")
            elif rv not in all_reachable_rivals:
                reasons.append(f"rival={rv} ({hd.pj_name(data, rv)}) nunca aparece en sagas.toml")
        if d.get("tipo") == "ssj" and int(d.get("nivel", 0)) > 1:
            prev_id = f"ssj{int(d['nivel']) - 1}"
            if prev_id not in non_form_ok and not reasons:
                reasons.append(f"requiere {prev_id} previo")
        if reasons:
            unreachable_unlocks.append({"id": did, "tipo": d.get("tipo"), "motivo": "; ".join(reasons)})
        else:
            non_form_ok.add(did)

    # Evaluate forms (fixed point per race, ignoring race restrictions first to see global reachability)
    # Also check razas_reglas.toml filter!
    reglas_formas_by_race = {
        int(r["id"]): set(r.get("formas", [])) for r in razas_reglas.get("raza", [])
    }
    form_ok: set[str] = set()
    changed = True
    while changed:
        changed = False
        for d in desbs:
            if d.get("tipo") != "forma" or d["id"] in form_ok:
                continue
            raza = int(d["raza"])
            if d["id"] not in reglas_formas_by_race.get(raza, set()):
                continue
            mk = d.get("marca", "")
            if mk and mk not in all_reachable_marks:
                continue
            prev = d.get("previa", "")
            if prev and prev not in form_ok:
                continue
            vence = int(d.get("vence", -1))
            abre_con = d.get("abre_con", "")
            cond_ok = (vence < 0 and not abre_con) or (vence >= 0 and vence in all_reachable_rivals) or (abre_con and abre_con in non_form_ok)
            if cond_ok:
                form_ok.add(d["id"])
                changed = True

    unreachable_forms: list[dict[str, Any]] = []
    for d in desbs:
        if d.get("tipo") != "forma" or d["id"] in form_ok:
            continue
        reasons = []
        raza = int(d["raza"])
        if d["id"] not in reglas_formas_by_race.get(raza, set()):
            reasons.append("ausente en res/heroe/razas_reglas.toml")
        mk = d.get("marca", "")
        if mk and mk not in all_reachable_marks:
            reasons.append(f"marca '{mk}' nunca se asigna en sagas.toml ni heroe.cpp")
        prev = d.get("previa", "")
        if prev and prev not in form_ok:
            reasons.append(f"previa '{prev}' inalcanzable")
        vence = int(d.get("vence", -1))
        abre_con = d.get("abre_con", "")
        if vence >= 0 and vence not in all_reachable_rivals:
            if not (abre_con and abre_con in non_form_ok):
                reasons.append(f"rival vence={vence} ({hd.pj_name(data, vence)}) nunca aparece en sagas.toml")
        unreachable_forms.append(
            {
                "id": d["id"],
                "raza": raza,
                "nombre_es": d.get("nombre_es", ""),
                "cond_es": d.get("cond_es", ""),
                "motivo": "; ".join(reasons) or "bloqueada",
            }
        )

    # (B) Techniques with no master / unreachable techniques
    techs_no_master: list[dict[str, Any]] = []
    techs_no_source_at_all: list[dict[str, Any]] = []
    techs_no_ccm_type: list[dict[str, Any]] = []
    reward_tech_caps = {int(r["capsula"]): r["id"] for r in recs if r.get("tipo") == "tecnica" and int(r.get("capsula", 0)) > 0}
    estilos_ids = {e["id"] for e in cpp["estilos"]}
    # Schools = Movesets(): unlocked when fighter is rival of a completed canon saga (Desbloqueado(id))
    school_unlockable_ids = set(reach["classic"]["rivals"]) | set(maestro_id_to_idx.keys())

    if afs["available"]:
        for cap, info in sorted(afs["attacks"].items()):
            if cap >= 596:
                continue
            owners = info["owners"]
            ccm = afs["ccm_first"].get(cap)
            has_master = any(o in maestro_id_to_idx for o in owners)
            has_reward = cap in reward_tech_caps
            has_school = any(o in school_unlockable_ids and o < 44 for o in owners)
            has_style = any(o in estilos_ids for o in owners)
            if ccm is None:
                techs_no_ccm_type.append(
                    {
                        "cap": cap,
                        "owners": owners,
                        "owners_names": [hd.pj_name(data, o) for o in owners],
                        "has_master": has_master,
                    }
                )
            if not has_master:
                entry = {
                    "cap": cap,
                    "owners": owners,
                    "owners_names": [hd.pj_name(data, o) for o in owners],
                    "ccm_type": ccm[0] if ccm else -1,
                    "has_reward": reward_tech_caps.get(cap, ""),
                    "has_school_in_sagas": has_school,
                    "in_kEstilos": has_style,
                }
                techs_no_master.append(entry)
                if not has_reward and not has_school:
                    techs_no_source_at_all.append(entry)

    # (C) Masters with no saga (and sagas without master / unimplemented masters)
    masters_no_saga: list[dict[str, Any]] = []
    sagas_by_master: dict[int, list[int]] = defaultdict(list)
    canon_sagas_by_master: dict[int, list[int]] = defaultdict(list)
    for idx, t in enumerate(tramos):
        m = int(t.get("maestro", -1))
        if m >= 0:
            sagas_by_master[m].append(idx)
            if not t["_es_variante"]:
                canon_sagas_by_master[m].append(idx)
    for i, m in enumerate(maestros):
        if not canon_sagas_by_master.get(i):
            masters_no_saga.append(
                {
                    "master_idx": i,
                    "fighter_id": m["id"],
                    "nombre": hd.pj_name(data, m["id"]),
                    "motivo": "Sin saga canonica con maestro = %d" % i,
                }
            )
    # Also check masters referenced in desbloqueos/recompensas that aren't in kEstilos[0..5]
    referenced_masters = set()
    for d in desbs:
        if "maestro" in d and int(d["maestro"]) >= 0:
            referenced_masters.add(int(d["maestro"]))
    for r in recs:
        if "maestro" in r and int(r["maestro"]) >= 0:
            referenced_masters.add(int(r["maestro"]))
    for mid in sorted(referenced_masters):
        if mid not in maestro_id_to_idx:
            masters_no_saga.append(
                {
                    "master_idx": -1,
                    "fighter_id": mid,
                    "nombre": hd.pj_name(data, mid),
                    "motivo": f"Referenciado en desbloqueos.toml/recompensas.toml pero NO existe en kEstilos[0..{num_m-1}] ni tiene saga",
                }
            )
    sagas_without_master = [
        {"tramo": idx, "nombre_es": t.get("nombre_es", ""), "variante": t["_es_variante"], "etapa": t.get("etapa", -1)}
        for idx, t in enumerate(tramos)
        if int(t.get("maestro", -1)) < 0
    ]

    # (D) Cycles (Tarjan SCC on the full dependency graph + specific semantic cycles)
    cycles: list[dict[str, Any]] = []
    index_counter = 0
    indices: dict[str, int] = {}
    lowlink: dict[str, int] = {}
    stack: list[str] = []
    on_stack: set[str] = set()

    def strongconnect(v: str) -> None:
        nonlocal index_counter
        indices[v] = lowlink[v] = index_counter
        index_counter += 1
        stack.append(v)
        on_stack.add(v)
        for e in g.adj.get(v, []):
            w = e["to"]
            if w not in indices:
                strongconnect(w)
                lowlink[v] = min(lowlink[v], lowlink[w])
            elif w in on_stack:
                lowlink[v] = min(lowlink[v], indices[w])
        if lowlink[v] == indices[v]:
            scc = []
            while True:
                w = stack.pop()
                on_stack.remove(w)
                scc.append(w)
                if w == v:
                    break
            if len(scc) > 1:
                cycles.append({"nodes": sorted(scc), "size": len(scc), "kind": "scc"})
            elif any(e["to"] == v for e in g.adj.get(v, [])):
                cycles.append({"nodes": [v], "size": 1, "kind": "self_loop"})

    # Iterative-safe recursion limit
    sys.setrecursionlimit(max(5000, len(g.nodes) * 4))
    for nid in list(g.nodes.keys()):
        if nid not in indices:
            strongconnect(nid)

    # Also check master unlock vs Acto 2 master-branch ordering!
    # Look: Vegeta (idx=1) unlocks at SagaDelMaestro(1) = saga 3 (Baby), Piccolo (idx=2) at saga 2 (Piccolo),
    # Krillin (idx=3) at saga 5 (Reunion), Frieza (idx=4) at saga 4 (Omega), Cell (idx=5) at saga 1 (Androids)!
    # WAIT A SECOND! At Acto 2 (etapa 2, when g_saga == 2 in DU+Rutas):
    #   - Goku (idx=0): SagaDelMaestro=0 -> unlocked at g_saga=2.
    #   - Cell (idx=5): SagaDelMaestro=1 -> unlocked at g_saga=2 (if vence_33).
    #   - Piccolo (idx=2): SagaDelMaestro=2 -> unlocked at g_saga=2 (if opc_tierra).
    #   - Vegeta (idx=1): SagaDelMaestro=3 -> g_saga=2 < 3 -> LOCKED AT ACTO 2!!
    #   - Frieza (idx=4): SagaDelMaestro=4 -> g_saga=2 < 4 -> LOCKED AT ACTO 2!!
    #   - Krillin (idx=3): SagaDelMaestro=5 -> g_saga=2 < 5 -> LOCKED AT ACTO 2!!
    # WHOA!! Check that! Baby Saga (tramo 3, etapa 2) requires `condicion = "maestro_vegeta|maestro_freezer"`,
    # YET NEITHER Vegeta (saga 3) NOR Frieza (saga 4) IS UNLOCKED WHEN ENTERING ETAPA 2 (`g_saga == 2`)!!
    # Baby Saga is the ONLY saga that unlocks Vegeta (maestro=1 -> SagaDelMaestro(1)=3), creating a DEADLOCK CYCLE!
    master_saga_deadlocks: list[dict[str, Any]] = []
    for idx, t in enumerate(tramos):
        if int(t.get("etapa", -1)) == 2 and t.get("condicion"):
            cond_masters = [a[len("maestro_"):] for a in t["condicion"].split("|") if a.startswith("maestro_")]
            if cond_masters:
                # At entry to etapa 2 in a fresh DU+Rutas run, g_saga == 2
                unlock_sagas = {}
                for cm in cond_masters:
                    if cm in marca_maestro:
                        mi = marca_maestro.index(cm)
                        ks = next((si for si, s in enumerate(canon) if int(s.get("maestro", -1)) == mi), -1)
                        unlock_sagas[cm] = ks
                if idx not in reach["routes"]["tramos"] and not cpp["acceso_por_afinidad"] and unlock_sagas and all(ks > 2 for ks in unlock_sagas.values()):
                    master_saga_deadlocks.append(
                        {
                            "tramo": idx,
                            "nombre_es": t.get("nombre_es", ""),
                            "condicion": t["condicion"],
                            "unlock_sagas": unlock_sagas,
                            "motivo": (
                                f"CICLO/DEADLOCK en DU+Rutas: tramo {idx} (etapa 2, g_saga=2 al entrar) exige "
                                f"{t['condicion']}, pero todos esos maestros piden g_saga >= {min(unlock_sagas.values())} "
                                f"(Desbloqueado() en heroe.cpp:1744-1747) — nunca se puede elegir antes de la etapa 2 en partida nueva!"
                            ),
                        }
                    )

    # (E) Requirements impossible for some body or style
    body_style_impossibles: list[dict[str, Any]] = []
    # E1: Mission mode (Mision() in src/heroe.cpp:2310-2319) substitutes rival if rival == estilo || rival == cuerpo!
    # Check every reward/unlock with `vence = X` against bodies and styles containing X!
    all_bodies = []
    for rz in cpp["razas"]:
        for cb in rz["cuerpos"]:
            all_bodies.append({"raza_idx": rz["index"], "raza_es": rz["es"], "body_id": cb["id"]})
    all_style_ids = [e["id"] for e in cpp["estilos"]]

    vence_items: list[tuple[str, str, int]] = []
    for r in recs:
        if "vence" in r:
            vence_items.append(("recompensa", r["id"], int(r["vence"])))
    for d in desbs:
        if "vence" in d and int(d["vence"]) >= 0:
            vence_items.append((d.get("tipo", "desbloqueo"), d["id"], int(d["vence"])))

    for kind, item_id, rv in vence_items:
        matching_bodies = [b for b in all_bodies if b["body_id"] == rv]
        matching_style = rv in all_style_ids
        if matching_bodies or matching_style:
            body_style_impossibles.append(
                {
                    "category": "mision_mirror_substitution",
                    "item_kind": kind,
                    "item_id": item_id,
                    "rival": rv,
                    "rival_name": hd.pj_name(data, rv),
                    "affected_bodies": [f"{b['raza_es']}(id={b['body_id']})" for b in matching_bodies],
                    "affected_style": f"{hd.pj_name(data, rv)}(id={rv})" if matching_style else "",
                    "motivo": (
                        f"En modo Misiones (src/heroe.cpp:2310-2319), Mision() sustituye al rival {rv} ({hd.pj_name(data, rv)}) "
                        f"por alt/alt2 si cuerpo=={rv} o estilo=={rv}: el heroe nunca vence a {rv} y no puede abrir '{item_id}'."
                    ),
                }
            )

    # E2: Race/body vs Evolution & FormasCombate mismatches
    de_raza = cpp["de_raza"]
    ssj_idx_donors = {0, 3, 4, 6, 7, 8, 9}
    ssj_caps_flat = {c for grp in cpp["ssj_caps"] for c in grp}
    for rz in cpp["razas"]:
        ri = rz["index"]
        evo_r = de_raza[ri] if ri < len(de_raza) else 0
        # NivelesTransformacion() in heroe.cpp:2505
        niv_tr = 3 if ri in (0, 7, 8) else 1 if ri in (10, 11, 12) else 0
        evo_forms = [d for d in desbs if d.get("tipo") == "forma" and int(d["raza"]) == evo_r]
        # UT Saiyan (ri=6, cuerpos 100..105): evo_r=0 (Saiyan), niv_tr=0, CapsulasHeroe gives 0 forms!
        if ri == 6:
            body_style_impossibles.append(
                {
                    "category": "ut_saiyan_no_forms",
                    "raza_idx": ri,
                    "raza_es": rz["es"],
                    "evo_raza": evo_r,
                    "motivo": (
                        "Raza 6 'Saiyan (CaC UT)' (cuerpos 100..105): RazaEvo()=0 (Saiyan) muestra el arbol SSJ1/SSJ2/SSJ3/Majin, "
                        "pero NivelesTransformacion()=0 (heroe.cpp:2505), CapsulasHeroe() no incluye 100..105 (heroe.cpp:2794) "
                        "y vista_heroe.py:6265 no monta formas para UT."
                    ),
                }
            )
        # XV2 Human (ri=7,8): niv_tr=3 (SSJ1-3) while RazaEvo()=1 (Humano)!
        if ri in (7, 8):
            body_style_impossibles.append(
                {
                    "category": "xv2_human_evo_mismatch",
                    "raza_idx": ri,
                    "raza_es": rz["es"],
                    "evo_raza": evo_r,
                    "motivo": (
                        f"Raza {ri} '{rz['es']}': NivelesTransformacion()=3 (heroe.cpp:2507, abre ssj1..3), pero "
                        f"RazaEvo()=1 (Humano, heroe.cpp:2719): su arbol de Evolucion muestra formas Humanas, no SSJ."
                    ),
                }
            )
        # Native non-Saiyan bodies with forms that belong to another fighter in the same race
        if ri in (1, 2, 3, 4, 5) and afs["available"]:
            for cb in rz["cuerpos"]:
                bid = cb["id"]
                native_caps = set(afs["fighter_forms"].get(bid, []))
                missing_evo_caps = []
                for f in evo_forms:
                    fc = int(f.get("capsula", 0))
                    if fc > 0 and fc not in native_caps:
                        missing_evo_caps.append(f"{f['id']}(cap={fc})")
                    elif fc == 0:
                        missing_evo_caps.append(f"{f['id']}(cap=0,sin_capsula)")
                if missing_evo_caps:
                    body_style_impossibles.append(
                        {
                            "category": "body_lacks_race_evo_forms",
                            "raza_idx": ri,
                            "raza_es": rz["es"],
                            "body_id": bid,
                            "body_name": hd.pj_name(data, bid),
                            "missing_forms": missing_evo_caps,
                            "motivo": (
                                f"Cuerpo {hd.pj_name(data, bid)} (id={bid}, {rz['es']}) con su propio estilo no posee en char372 "
                                f"las capsulas de formas de su arbol de Evolucion: {', '.join(missing_evo_caps)} "
                                f"(CapsulasHeroe heroe.cpp:2796 exige don==cuerpo para no-Saiyan)."
                            ),
                        }
                    )
        # Non-Saiyan styles on Saiyan body (CapsulasHeroe: kSsjIdx only has {0,3,4,6,7,8,9})
        if ri == 0:
            non_ssj_masters = [m["id"] for m in maestros if m["id"] not in ssj_idx_donors]
            body_style_impossibles.append(
                {
                    "category": "saiyan_with_non_saiyan_style_no_ssj",
                    "raza_idx": 0,
                    "raza_es": rz["es"],
                    "affected_styles": [f"{hd.pj_name(data, s)}(id={s})" for s in non_ssj_masters],
                    "motivo": (
                        f"Cuerpo Saiyan con estilo/donante de maestro no-Saiyan ({', '.join(hd.pj_name(data, s) for s in non_ssj_masters)}): "
                        "kSsjIdx (heroe.cpp:2792) solo incluye donantes Saiyan {0,3,4,6,7,8,9}, por lo que ninguna forma SSJ se monta en combate."
                    ),
                }
            )

    # E3: Technique r_t7_spirit_saiyans (cap 139) is Saiyan-only in UI (heroe_v2_tecnicas.inl:42)
    # and check reward techniques whose donor is NOT unlocked in DU/sagas!
    for r in recs:
        if r.get("tipo") == "tecnica":
            cap = int(r["capsula"])
            info = afs["attacks"].get(cap) if afs["available"] else None
            if info:
                owners = info["owners"]
                # Check if ANY owner is unlocked via Desbloqueado() (either master or rival of a canon saga)
                any_owner_unlockable = any(o in school_unlockable_ids for o in owners)
                if not any_owner_unlockable:
                    body_style_impossibles.append(
                        {
                            "category": "reward_tech_unusable_donor",
                            "reward_id": r["id"],
                            "cap": cap,
                            "owners": owners,
                            "owners_names": [hd.pj_name(data, o) for o in owners],
                            "motivo": (
                                f"Premio '{r['id']}' da capsula {cap} (dueno {[hd.pj_name(data, o) for o in owners]}), "
                                f"pero ese luchador NUNCA se desbloquea como estilo/moveset (Desbloqueado() en heroe.cpp:1738-1751 "
                                f"solo abre maestros y rivales de sagas canonicas): la tecnica se gana en el inventario, pero "
                                f"TecnicaActiva({cap}) es SIEMPRE false y nunca se puede lanzar en combate!"
                            ),
                        }
                    )

    # (F) Texts missing in any of the 7 languages (es, en, fr, de, it, pt, ja)
    i18n_missing: list[dict[str, Any]] = []
    # F1: recompensas.toml (resolved via T::T(es, en) in heroe_recompensas.inl:209-210)
    for r in recs:
        for field_base in ("nombre", "cond"):
            es_t = r.get(f"{field_base}_es", "")
            en_t = r.get(f"{field_base}_en", "")
            miss = i18n.missing_langs_runtime_TT(es_t, en_t)
            if miss:
                i18n_missing.append(
                    {
                        "source": "res/heroe/recompensas.toml",
                        "item": f"{r['id']}.{field_base}",
                        "es": es_t,
                        "en": en_t,
                        "missing": miss,
                    }
                )
    # F2: desbloqueos.toml (resolved via T::T(es, en) in heroe.cpp:2826-2828, 2863)
    for d in desbs:
        fields = ["nombre"]
        if d.get("tipo") == "forma":
            fields.append("cond")
            if d.get("rama"):
                fields.append("camino")
        for field_base in fields:
            es_t = d.get(f"{field_base}_es", "")
            en_t = d.get(f"{field_base}_en", "")
            miss = i18n.missing_langs_runtime_TT(es_t, en_t)
            if miss:
                i18n_missing.append(
                    {
                        "source": "res/heroe/desbloqueos.toml",
                        "item": f"{d['id']}.{field_base}",
                        "es": es_t,
                        "en": en_t,
                        "missing": miss,
                    }
                )
    # F3: sagas.toml (both runtime T::T/Hist AND DU generator sagas_i18n.toml + i18n_modos.toml)
    for idx, t in enumerate(tramos):
        for field_base in ("nombre", "cruce", "lugar", "pregunta"):
            es_t = t.get(f"{field_base}_es", "")
            en_t = t.get(f"{field_base}_en", "")
            if not es_t and not en_t:
                continue
            inline = {lg: t.get(f"{field_base}_{lg}", "") for lg in ("fr", "de", "it", "pt", "ja")}
            miss_rt = i18n.missing_langs_runtime_TT(es_t, en_t)
            miss_du = i18n.missing_langs_saga_du(es_t, en_t, inline)
            # Runtime C++ only reads i18n_modos.toml, NOT sagas_i18n.toml!
            if miss_rt:
                i18n_missing.append(
                    {
                        "source": "res/heroe/sagas.toml",
                        "item": f"tramo[{idx}].{field_base}",
                        "es": es_t,
                        "en": en_t,
                        "missing": miss_rt,
                        "missing_du_gen": miss_du,
                    }
                )
        for arr_base in ("intro", "cierre"):
            es_l = t.get(f"{arr_base}_es", [])
            en_l = t.get(f"{arr_base}_en", [])
            for li, (es_t, en_t) in enumerate(zip(es_l, en_l)):
                miss_rt = i18n.missing_langs_runtime_TT(es_t, en_t)
                miss_du = i18n.missing_langs_saga_du(es_t, en_t)
                if miss_rt:
                    i18n_missing.append(
                        {
                            "source": "res/heroe/sagas.toml",
                            "item": f"tramo[{idx}].{arr_base}[{li}]",
                            "es": es_t,
                            "en": en_t,
                            "missing": miss_rt,
                            "missing_du_gen": miss_du,
                        }
                    )
        for ci, c in enumerate(t.get("combate", [])):
            es_t, en_t = c.get("es", ""), c.get("en", "")
            miss_rt = i18n.missing_langs_runtime_TT(es_t, en_t)
            if miss_rt:
                i18n_missing.append(
                    {
                        "source": "res/heroe/sagas.toml",
                        "item": f"tramo[{idx}].combate[{ci}]",
                        "es": es_t,
                        "en": en_t,
                        "missing": miss_rt,
                    }
                )
            if c.get("jefe_es") or c.get("jefe_en"):
                jes, jen = c.get("jefe_es", ""), c.get("jefe_en", "")
                miss_j = i18n.missing_langs_runtime_TT(jes, jen)
                if miss_j:
                    i18n_missing.append(
                        {
                            "source": "res/heroe/sagas.toml",
                            "item": f"tramo[{idx}].combate[{ci}].jefe",
                            "es": jes,
                            "en": jen,
                            "missing": miss_j,
                        }
                    )
        for oi, o in enumerate(t.get("opcional", [])):
            oes, oen = o.get("es", ""), o.get("en", "")
            miss_du = i18n.missing_langs_saga_du(oes, oen)
            if miss_du:
                i18n_missing.append(
                    {
                        "source": "res/heroe/sagas.toml",
                        "item": f"tramo[{idx}].opcional[{oi}]",
                        "es": oes,
                        "en": oen,
                        "missing": miss_du,
                    }
                )

    # F4: Epilogo
    ep = data["sagas_doc"].get("epilogo", {})
    for li, (es_t, en_t) in enumerate(zip(ep.get("es", []), ep.get("en", []))):
        miss_rt = i18n.missing_langs_runtime_TT(es_t, en_t)
        if miss_rt:
            i18n_missing.append(
                {
                    "source": "res/heroe/sagas.toml",
                    "item": f"epilogo[{li}]",
                    "es": es_t,
                    "en": en_t,
                    "missing": miss_rt,
                }
            )

    # F5: Hardcoded T::T(es, en) strings in Hero v2 UI files
    import re
    ui_files = [
        "src/heroe_v2_coleccion.inl",
        "src/heroe_v2_evolucion.inl",
        "src/heroe_v2_maestros.inl",
        "src/heroe_v2_tecnicas.inl",
        "src/heroe_v2_habitacion.inl",
        "src/heroe_v2_cruce.inl",
        "src/heroe_v2_resultado.inl",
    ]
    seen_ui_pairs: set[tuple[str, str]] = set()
    for rel in ui_files:
        p = data["root"] / rel
        if not p.is_file():
            continue
        st = p.read_text(encoding="utf-8")
        for m in re.finditer(r'T::T\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)', st):
            es_t = hd._unescape_c_str(m.group(1))
            en_t = hd._unescape_c_str(m.group(2))
            if (es_t, en_t) in seen_ui_pairs or (es_t == "1" and en_t == "0"):
                continue
            seen_ui_pairs.add((es_t, en_t))
            miss = i18n.missing_langs_runtime_TT(es_t, en_t)
            if miss:
                i18n_missing.append(
                    {
                        "source": rel,
                        "item": f"T::T({en_t[:36]})",
                        "es": es_t,
                        "en": en_t,
                        "missing": miss,
                    }
                )

    # Summarize i18n missing by source & language
    i18n_by_source: dict[str, int] = defaultdict(int)
    i18n_by_lang: dict[str, int] = defaultdict(int)
    for row in i18n_missing:
        i18n_by_source[row["source"]] += 1
        for lg in row["missing"]:
            i18n_by_lang[lg] += 1

    return {
        "summary": {
            "nodes": len(g.nodes),
            "edges": len(g.edges),
            "canon_sagas": len(canon),
            "total_tramos": len(tramos),
            "recompensas": len(recs),
            "desbloqueos": len(desbs),
            "maestros": num_m,
            "unreachable_rewards": len(unreachable_rewards),
            "unreachable_unlocks": len(unreachable_unlocks),
            "unreachable_forms": len(unreachable_forms),
            "techs_no_master": len(techs_no_master),
            "techs_no_source_at_all": len(techs_no_source_at_all),
            "techs_no_ccm_type": len(techs_no_ccm_type),
            "masters_no_saga": len(masters_no_saga),
            "sagas_without_master": len(sagas_without_master),
            "graph_cycles": len(cycles),
            "master_saga_deadlocks": len(master_saga_deadlocks),
            "body_style_impossibles": len(body_style_impossibles),
            "i18n_missing_entries": len(i18n_missing),
        },
        "reachability": reach,
        "anomalies": {
            "unreachable_rewards": unreachable_rewards,
            "classic_only_unreachable_rewards": classic_only_unreachable,
            "unreachable_unlocks": unreachable_unlocks,
            "unreachable_forms": unreachable_forms,
            "techs_no_master": techs_no_master,
            "techs_no_source_at_all": techs_no_source_at_all,
            "techs_no_ccm_type": techs_no_ccm_type,
            "masters_no_saga": masters_no_saga,
            "sagas_without_master": sagas_without_master,
            "cycles": cycles,
            "master_saga_deadlocks": master_saga_deadlocks,
            "body_style_impossibles": body_style_impossibles,
            "i18n_missing": i18n_missing,
            "i18n_by_source": dict(i18n_by_source),
            "i18n_by_lang": dict(i18n_by_lang),
        },
        "graph": {
            "nodes": list(g.nodes.values()),
            "edges": g.edges,
        },
    }


def export_mermaid(report: dict[str, Any]) -> str:
    """Genera un diagrama Mermaid compacto de etapas, tramos, maestros, deseos y recompensas clave."""
    lines = ["flowchart LR"]
    keep_kinds = {"root", "etapa", "wish", "tramo", "master", "master_missing", "collection"}
    kept = {n["id"]: n for n in report["graph"]["nodes"] if n["kind"] in keep_kinds}

    def safe_id(nid: str) -> str:
        return nid.replace(":", "_").replace("-", "_").replace(" ", "_")

    for nid, n in kept.items():
        lbl = n["label"].replace('"', "'")
        lines.append(f'  {safe_id(nid)}["{lbl}"]')
    for e in report["graph"]["edges"]:
        if e["from"] in kept and e["to"] in kept:
            req = e["req"].replace('"', "'")
            tag = f"{e['rel']}: {req}" if req else e["rel"]
            lines.append(f'  {safe_id(e["from"])} -->|"{tag}"| {safe_id(e["to"])}')
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description="Grafo completo de progreso del Modo Heroe")
    ap.add_argument("--root", type=Path)
    ap.add_argument("--sagas", type=Path)
    ap.add_argument("--desbloqueos", type=Path)
    ap.add_argument("--recompensas", type=Path)
    ap.add_argument("--razas-reglas", type=Path)
    ap.add_argument("--us", type=Path)
    ap.add_argument("--skip-afs", action="store_true")
    ap.add_argument("--json", type=Path, help="Guardar informe JSON completo")
    ap.add_argument("--mermaid", type=Path, help="Guardar diagrama Mermaid (.mmd)")
    ap.add_argument("--autoprueba", action="store_true", help="comprobar nodos/aristas y Mermaid sin AFS")
    args = ap.parse_args()
    if args.autoprueba:
        g = ProgressGraph({})
        g.add_node("tramo:0", "tramo", "Namek")
        g.add_node("tramo:1", "tramo", "Cell")
        g.add_edge("tramo:0", "tramo:1", "siguiente")
        assert 'tramo_0 -->|"siguiente"| tramo_1' in export_mermaid({"graph": {"nodes": list(g.nodes.values()), "edges": g.edges}})
        print("Grafo: autoprueba OK")
        return 0

    data = hd.load_all(
        root=args.root,
        sagas_path=args.sagas,
        desbloqueos_path=args.desbloqueos,
        recompensas_path=args.recompensas,
        razas_reglas_path=args.razas_reglas,
        us_dir=args.us,
        skip_afs=args.skip_afs,
    )
    rep = build_graph_and_audit(data)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(rep, ensure_ascii=False, indent=2), encoding="utf-8")
    if args.mermaid:
        args.mermaid.parent.mkdir(parents=True, exist_ok=True)
        args.mermaid.write_text(export_mermaid(rep), encoding="utf-8")

    s = rep["summary"]
    a = rep["anomalies"]
    print("=== GRAFO DE PROGRESO DEL MODO HEROE ===")
    print(
        f"Nodos: {s['nodes']} | Aristas: {s['edges']} | Sagas: {s['canon_sagas']} canonicas + "
        f"{s['total_tramos'] - s['canon_sagas']} variantes = {s['total_tramos']} tramos"
    )
    print(f"Recompensas: {s['recompensas']} | Desbloqueos: {s['desbloqueos']} | Maestros: {s['maestros']}")
    print(f"1. Recompensas inalcanzables (global): {s['unreachable_rewards']}")
    for x in a["unreachable_rewards"]:
        print(f"   - {x['id']} ({x['tipo']}): {x['motivo']}")
    print(f"   Desbloqueos no-forma inalcanzables: {s['unreachable_unlocks']}")
    for x in a["unreachable_unlocks"]:
        print(f"   - {x['id']} ({x['tipo']}): {x['motivo']}")
    print(f"   Formas inalcanzables: {s['unreachable_forms']}")
    for x in a["unreachable_forms"]:
        print(f"   - {x['id']} (raza {x['raza']}, '{x['nombre_es']}'): {x['motivo']}")
    print(
        f"2. Tecnicas #SKC sin maestro: {s['techs_no_master']} "
        f"(sin ninguna fuente en sagas/premios: {s['techs_no_source_at_all']}; sin tipo #CCM: {s['techs_no_ccm_type']})"
    )
    print(f"3. Maestros sin saga / no registrados: {s['masters_no_saga']} | Tramos sin maestro: {s['sagas_without_master']}")
    for x in a["masters_no_saga"]:
        print(f"   - {x['nombre']} (id={x['fighter_id']}): {x['motivo']}")
    print(f"4. Ciclos en grafo: {s['graph_cycles']} | Deadlocks maestro-etapa: {s['master_saga_deadlocks']}")
    for c in a["cycles"]:
        print(f"   - SCC ({c['size']}): {c['nodes']}")
    for d in a["master_saga_deadlocks"]:
        print(f"   - Tramo {d['tramo']} ({d['nombre_es']}): {d['motivo']}")
    print(f"5. Requisitos imposibles por cuerpo/estilo: {s['body_style_impossibles']}")
    for b in a["body_style_impossibles"]:
        print(f"   - [{b['category']}] {b['motivo']}")
    print(
        f"6. Textos sin traducir en alguno de los 7 idiomas: {s['i18n_missing_entries']} entradas "
        f"(por fuente: {a['i18n_by_source']}; por idioma: {a['i18n_by_lang']})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

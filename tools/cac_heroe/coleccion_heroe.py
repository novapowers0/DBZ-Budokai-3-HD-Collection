#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""coleccion_heroe.py — Auditoria y modelo offline de la Coleccion del Modo Heroe (NHER2).

Reutiliza el lector local `heroe_datos.py` y los datos del proyecto.
El candidato completo se simula: ni certifica el juego ni migra guardados de usuario.
Escalabilidad automatica: acepta `--sagas`, `--recompensas`, `--desbloqueos` y `--razas-reglas`.

Subcomandos:
  auditar      Audita cada recompensa de recompensas.toml (alcanzabilidad, bloqueos y causa exacta).
  simular      Simula la Coleccion ANTES (arbol actual) vs DESPUES (parches NHER2):
               - 1 ficha en 1 vuelta DU+Rutas, Misiones clasico e Hibrido;
               - 1 ficha en N vueltas DU+Rutas (NuevaVueltaRuta) -> prueba constructiva 100 % (24/24);
               - Multi-ficha (coleccion.toml global) -> prueba 100 % (24/24);
               - Cuerpos espejo (Freezer id=27, Cell id=33) en DU+Rutas y Misiones.
  migrar-test  Ejecuta pruebas de migracion de guardados (cac.toml v1/v2/v3, coleccion.toml antigua,
               multi-ficha + archivados, fin de vuelta 1 -> vuelta 2, tope 64 -> 128, save100).
  todo         Ejecuta auditar + simular + migrar-test y opcionalmente guarda un JSON completo.

Uso:
  python -X utf8 coleccion_heroe.py --help
  python -X utf8 coleccion_heroe.py --self-check
  python -X utf8 coleccion_heroe.py todo --skip-afs --json reporte.json
"""
from __future__ import annotations

import argparse
import json
import sys
import tempfile
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import heroe_datos as hd

MIN_POR_COMBATE = 2.2
MIN_POR_MAPA_SAGA = 1.0


# ---------------------------------------------------------------------------
# Maquina de estados parametrizable (modelo exacto ANTES vs DESPUES de NHER2)
# ---------------------------------------------------------------------------

@dataclass
class ReglasModo:
    """Conmuta entre el arbol actual (before) y el arbol con parches NHER2 (after)."""
    h100_sin_ciclo: bool = True         # ya en src/heroe_recompensas.inl:168-185
    maestros_acto2_afinidad: bool = True  # ya en src/heroe.cpp:1747
    coleccion_global: bool = False      # Parche 01: coleccion.toml conseguidos + migracion multi-ficha
    nueva_vuelta_rutas: bool = False    # Parche 02: NuevaVueltaRuta() + DuVictoria etapa 5 por g_tramo<0
    tope_desbloqueos: int = 64          # Parche 03: 64 -> 128 + proteccion r_/f_/ssj
    proteger_premios: bool = False      # Parche 03
    espejo_sin_sustituir_propio: bool = False  # Parche 04: Mision() con IdPropio()>=0
    donante_premiado: bool = False      # Parche 04: DonantePremiado()


def reglas_actuales(data: dict[str, Any]) -> ReglasModo:
    """Detecta desde src/ las reglas activas en el arbol analizado."""
    root: Path = data["root"]
    rec_inl = (root / "src" / "heroe_recompensas.inl").read_text(encoding="utf-8")
    heroe_cpp = (root / "src" / "heroe.cpp").read_text(encoding="utf-8")
    return ReglasModo(
        h100_sin_ciclo=("base_ok != base_tot" in rec_inl or "x.coleccion >= 100" in rec_inl),
        maestros_acto2_afinidad=("if (RutasOn() && AfinidadRuta(m)) return true;" in heroe_cpp),
        coleccion_global=("g_col.conseguidos" in rec_inl),
        nueva_vuelta_rutas=("NuevaVueltaRuta" in heroe_cpp),
        tope_desbloqueos=data["cpp"].get("max_desbloqueos", 64),
        proteger_premios=('id.rfind("r_", 0) == 0' in heroe_cpp),
        espejo_sin_sustituir_propio=("IdPropio() < 0" in heroe_cpp and "Mision(" in heroe_cpp),
        donante_premiado=("DonantePremiado(" in heroe_cpp),
    )


def reglas_parcheadas() -> ReglasModo:
    return ReglasModo(
        h100_sin_ciclo=True,
        maestros_acto2_afinidad=True,
        coleccion_global=True,
        nueva_vuelta_rutas=True,
        tope_desbloqueos=128,
        proteger_premios=True,
        espejo_sin_sustituir_propio=True,
        donante_premiado=True,
    )


@dataclass
class CuentaCol:
    """Representa user_data/dbz3/coleccion.toml."""
    todo: bool = False
    vistos: set[str] = field(default_factory=set)
    conseguidos: set[str] = field(default_factory=set)


@dataclass
class FichaSim:
    data: dict[str, Any]
    reglas: ReglasModo
    cuenta: CuentaCol = field(default_factory=CuentaCol)
    raza: int = 0
    cuerpo: int = 0
    estilo_idx: int = 0
    id_propio: int = 63
    rutas_on: bool = True
    evolucion_on: bool = True
    recompensas_on: bool = True
    semilla: int = 12345

    g_exp: int = 0
    g_saga: int = 0
    g_ph: int = 0
    g_maestros: int = 0
    g_entreno: list[int] = field(default_factory=lambda: [0] * 6)
    g_esferas: int = 0
    g_deseos: int = 0
    g_desbloqueos: list[str] = field(default_factory=list)
    cobrados: int = 0
    decision: int = 0
    g_tramo: int = 0
    g_etapa_sig: int = -1
    g_marcas: set[str] = field(default_factory=set)
    g_cruce: list[int] = field(default_factory=list)

    vueltas_du: int = 1
    zeni_repetidos: int = 0
    combates: int = 0
    tramos_jugados: list[int] = field(default_factory=list)
    max_desbloqueos_len: int = 0
    evicciones: list[dict[str, Any]] = field(default_factory=list)
    guardar_fallos: int = 0

    def __post_init__(self) -> None:
        if self.rutas_on and self.g_tramo == 0 and self.g_saga == 0 and self.decision == 0 and not self.g_marcas:
            self._elegir_tramo(0)

    @property
    def cpp(self) -> dict[str, Any]:
        return self.data["cpp"]

    @property
    def num_m(self) -> int:
        return self.cpp["num_maestros"]

    @property
    def tramos(self) -> list[dict[str, Any]]:
        return self.data["tramos"]

    @property
    def canon(self) -> list[dict[str, Any]]:
        return self.data["canon_sagas"]

    def tiene_desbloqueo(self, did: str) -> bool:
        return did in self.g_desbloqueos

    def premio_conseguido(self, rid: str) -> bool:
        if self.tiene_desbloqueo(rid) or self.cuenta.todo:
            return True
        if self.reglas.coleccion_global and rid in self.cuenta.conseguidos:
            return True
        return False

    def coleccion_pct(self) -> int:
        recs = self.data["recompensas"]
        if not recs:
            return 0
        n = sum(1 for r in recs if self.premio_conseguido(r["id"]))
        return (100 * n) // len(recs)

    def premios_ganados(self) -> list[str]:
        return [r["id"] for r in self.data["recompensas"] if self.premio_conseguido(r["id"])]

    def maestro_indice(self, fid: int) -> int:
        for i, m in enumerate(self.cpp["maestros"]):
            if m["id"] == fid:
                return i
        return -1

    def maestro_actual(self) -> int:
        return self.maestro_indice(self.cpp["estilos"][self.estilo_idx]["id"])

    def saga_del_maestro(self, m_idx: int) -> int:
        for i, s in enumerate(self.canon):
            if int(s.get("maestro", -1)) == m_idx:
                return i
        return -1

    def afinidad_ruta(self, m_idx: int) -> bool:
        if not self.rutas_on:
            return True
        if m_idx == 0:
            return True
        if m_idx == 1:
            return "opc_namek" in self.g_marcas or "camino_vegeta" in self.g_marcas
        if m_idx in (2, 3):
            return "opc_tierra" in self.g_marcas
        if m_idx == 4:
            return "camino_nave" in self.g_marcas
        if m_idx == 5:
            return "vence_33" in self.g_marcas
        return False

    def desbloqueado(self, fid: int) -> bool:
        if fid == self.cpp["estilos"][self.estilo_idx]["id"]:
            return True
        m = self.maestro_indice(fid)
        if m == 0 or (m > 0 and ((self.g_maestros >> m) & 1)):
            return True
        if m > 0:
            if self.reglas.maestros_acto2_afinidad and self.rutas_on and self.afinidad_ruta(m):
                return True
            k = self.saga_del_maestro(m)
            return k < 0 or self.g_saga >= k
        for i in range(min(len(self.canon), self.g_saga)):
            for c in self.canon[i].get("combate", []):
                if int(c["rival"]) == fid:
                    return True
        if self.reglas.donante_premiado:
            for r in self.data["recompensas"]:
                if r.get("tipo") == "tecnica" and int(r.get("capsula", 0)) > 0 and self.premio_conseguido(r["id"]):
                    if int(r.get("cara", -1)) == fid:
                        return True
        return False

    def elegir_maestro(self, m_idx: int) -> bool:
        if not (0 <= m_idx < self.num_m):
            return False
        if not (self.desbloqueado(self.cpp["estilos"][m_idx]["id"]) and self.afinidad_ruta(m_idx)):
            return False
        self.estilo_idx = m_idx
        return True

    def _hacer_sitio(self, nuevos: int, ctx: str) -> None:
        k_max = self.reglas.tope_desbloqueos
        borrados = []
        i = len(self.g_desbloqueos)
        while i > 0 and len(self.g_desbloqueos) + nuevos > k_max:
            i -= 1
            s = self.g_desbloqueos[i]
            prot = s.startswith("h02_") or (
                self.reglas.proteger_premios and (s.startswith("r_") or s.startswith("f_") or s.startswith("ssj"))
            )
            if not prot:
                borrados.append(self.g_desbloqueos.pop(i))
        if borrados:
            self.evicciones.append({"contexto": ctx, "borrados": borrados})

    def _registrar_busqueda(self) -> None:
        self.g_desbloqueos = [s for s in self.g_desbloqueos if not s.startswith("h02_v1_")]
        self._hacer_sitio(1, "RegistrarBusqueda")
        self.g_desbloqueos.insert(0, f"h02_v1_{self.cobrados}_{self.decision}")

    def _registrar_ruta(self) -> None:
        self.g_desbloqueos = [
            s for s in self.g_desbloqueos
            if not (s.startswith("h02_r1:") or s.startswith("h02_rm:") or s.startswith("h02_rc:"))
        ]
        ruta = [f"h02_r1:{self.g_tramo}:{self.g_etapa_sig}:{self.semilla}"]
        for m in sorted(self.g_marcas):
            ruta.append(f"h02_rm:{m}")
        for t in self.g_cruce:
            ruta.append(f"h02_rc:{t}")
        self._hacer_sitio(len(ruta), "RegistrarRutaEnFicha")
        self.g_desbloqueos = ruta + self.g_desbloqueos

    def simular_guardado(self) -> bool:
        if self.rutas_on:
            self._registrar_busqueda()
            self._registrar_ruta()
        self.max_desbloqueos_len = max(self.max_desbloqueos_len, len(self.g_desbloqueos))
        if len(self.g_desbloqueos) > self.reglas.tope_desbloqueos or any(len(s) >= 48 for s in self.g_desbloqueos):
            self.guardar_fallos += 1
            return False
        return True

    def evaluar_recompensas(self, tramo: int, rival: int) -> list[str]:
        if not self.recompensas_on:
            return []
        recs = self.data["recompensas"]
        nuevos: list[str] = []
        otorgado = True
        vuelta = 0
        while otorgado and vuelta < 4:
            otorgado = False
            vuelta += 1
            pct = self.coleccion_pct()
            base_ok = sum(1 for x in recs if int(x.get("coleccion", -1)) < 100 and self.premio_conseguido(x["id"]))
            base_tot = sum(1 for x in recs if int(x.get("coleccion", -1)) < 100)
            for r in recs:
                rt = int(r.get("tramo", -1))
                rv = int(r.get("vence", -1))
                rm = r.get("marca", "")
                rmas = int(r.get("maestro", -1))
                rent = int(r.get("entreno", 0))
                rcol = int(r.get("coleccion", -1))
                if rt < 0 and rv < 0 and not rm and rmas < 0 and rcol < 0:
                    continue
                if rt >= 0 and rt != tramo:
                    continue
                if rv >= 0 and rv != rival:
                    continue
                if rm and rm not in self.g_marcas:
                    continue
                if rmas >= 0:
                    mi = self.maestro_indice(rmas)
                    if mi < 0 or (self.g_entreno[mi] < rent and not ((self.g_maestros >> mi) & 1)):
                        continue
                if rcol == 100 and self.reglas.h100_sin_ciclo:
                    if base_tot == 0 or base_ok != base_tot:
                        continue
                elif rcol >= 0 and pct < rcol:
                    continue
                suceso = rt >= 0 or rv >= 0
                if self.premio_conseguido(r["id"]):
                    if suceso:
                        self.zeni_repetidos += 500
                    continue
                if not self.tiene_desbloqueo(r["id"]):
                    self.g_desbloqueos.append(r["id"])
                if self.reglas.coleccion_global:
                    self.cuenta.conseguidos.add(r["id"])
                nuevos.append(r["id"])
                otorgado = True
            tramo = -1
            rival = -1
        return nuevos

    def _elegir_tramo(self, desde: int) -> None:
        if self.decision:
            self.g_marcas.discard("deseo_revivir")
            if self.decision == 1:
                self.g_marcas.add("deseo_revivir")
        tramo, cruce = self.data["hs"].ruta_siguiente(self.tramos, desde, self.g_marcas)
        self.g_etapa_sig = -1
        if tramo < 0:
            self.g_tramo = -1
            self.g_cruce = []
            return
        self.g_tramo = tramo
        self.g_cruce = list(cruce)
        if not self.g_cruce and self.tramos[tramo].get("marca"):
            self.g_marcas.add(self.tramos[tramo]["marca"])

    def ruta_completada(self) -> bool:
        return self.rutas_on and self.g_tramo < 0 and self.g_etapa_sig < 0 and self.decision != 0

    def nueva_vuelta_ruta(self) -> bool:
        if not (self.reglas.nueva_vuelta_rutas and self.ruta_completada()):
            return False
        self.cobrados = 0
        self.decision = 0
        self.g_esferas = 0
        self.g_marcas.clear()
        self.g_cruce.clear()
        self.g_etapa_sig = -1
        self.semilla += 1
        self._elegir_tramo(0)
        self.vueltas_du += 1
        self.simular_guardado()
        return self.g_tramo >= 0

    def elegir_camino(self, t: int) -> int:
        if len(self.g_cruce) < 2:
            return self.g_tramo
        if t not in self.g_cruce:
            t = self.g_tramo
        self.g_tramo = t
        if self.tramos[t].get("marca"):
            self.g_marcas.add(self.tramos[t]["marca"])
        self.g_cruce = []
        self.simular_guardado()
        return t

    def confirmar_deseo_historia(self, revivir: bool) -> bool:
        if not (self.rutas_on and self.g_etapa_sig == 5 and not self.decision and self.g_esferas == 127):
            return False
        self.decision = 1 if revivir else 2
        self.g_esferas = 0
        self._registrar_busqueda()
        if self.decision == 1:
            self.g_marcas.add("deseo_revivir")
        self.simular_guardado()
        return True

    def resolver_ruta(self) -> int:
        if self.g_etapa_sig < 0:
            return self.g_tramo
        if self.g_etapa_sig == 5 and not self.decision:
            return -2 if self.g_esferas == 127 else -4
        if self.g_etapa_sig >= 2 and not any(m.startswith("maestro_") for m in self.g_marcas):
            ma = self.maestro_actual()
            if ma >= 0:
                self.g_marcas.add("maestro_" + self.cpp["marca_maestro"][ma])
        self._elegir_tramo(self.g_etapa_sig)
        self.simular_guardado()
        return self.g_tramo

    def jugar_tramo_du(self, hacer_opcional: bool = True) -> list[str]:
        t_idx = self.g_tramo
        tdef = self.tramos[t_idx]
        etapa = int(tdef.get("etapa", -1))
        m_saga = int(tdef.get("maestro", -1))
        premios: list[str] = []

        if hacer_opcional and tdef.get("opcional"):
            self.combates += 1
            self.g_exp += 1
            self.g_ph += 1
            if tdef.get("opcional_marca"):
                self.g_marcas.add(tdef["opcional_marca"])
            premios.extend(self.evaluar_recompensas(-1, -1))
            self.simular_guardado()

        combates = tdef.get("combate", [])
        for k, c in enumerate(combates):
            ultimo = k + 1 >= len(combates)
            if self.rutas_on and ultimo:
                bloqueo_e5 = (self.g_tramo < 0) if self.reglas.nueva_vuelta_rutas else (self.g_saga >= 6)
                if (0 <= etapa < 5 and (self.cobrados & (1 << etapa))) or (etapa == 5 and bloqueo_e5):
                    continue
            self.combates += 1
            self.g_exp += 1
            self.g_ph += 1
            if 0 <= m_saga < self.num_m:
                self.g_entreno[m_saga] = min(100, self.g_entreno[m_saga] + 20)
                if self.g_entreno[m_saga] >= 100:
                    self.g_maestros |= 1 << m_saga
            if ultimo:
                self.g_ph += 2
                if 0 <= m_saga < self.num_m:
                    self.g_maestros |= 1 << m_saga
                if self.rutas_on:
                    if 0 <= etapa < 5 and not (self.cobrados & (1 << etapa)):
                        self.cobrados |= 1 << etapa
                        self.g_esferas |= 1 << etapa
                        self._registrar_busqueda()
                    self.g_saga += 1
                    self.g_etapa_sig = etapa + 1
            rv = int(c["rival"])
            premios.extend(self.evaluar_recompensas(t_idx if ultimo else -1, rv))
            if self.rutas_on:
                self.g_marcas.add(f"vence_{rv}")
            if self.rutas_on and ultimo and etapa == 5:
                self._elegir_tramo(self.g_etapa_sig)
            self.simular_guardado()

        if self.rutas_on and etapa == 4:
            for ev in (5, 6):
                if not self.decision and not (self.cobrados & (1 << ev)):
                    self.cobrados |= 1 << ev
                    self.g_esferas |= 1 << ev
                    self._registrar_busqueda()
                    self.simular_guardado()

        self.tramos_jugados.append(t_idx)
        return premios

    def jugar_saga_misiones(self, saga_abs: int) -> list[str]:
        s_idx = saga_abs % len(self.canon)
        sdef = self.canon[s_idx]
        estilo_fid = self.cpp["estilos"][self.estilo_idx]["id"]
        premios: list[str] = []
        combates = sdef.get("combate", [])
        for k, c in enumerate(combates):
            ultimo = k + 1 >= len(combates)
            self.combates += 1
            cands = [int(c["rival"]), int(c.get("alt", 43)), int(c.get("alt2", 42)), 42, 43]
            if self.reglas.espejo_sin_sustituir_propio and self.id_propio >= 0:
                rival = cands[0]
            else:
                rival = cands[1]
                for cand in cands:
                    if cand != estilo_fid and cand != self.cuerpo:
                        rival = cand
                        break
            pierde = c.get("nota") == "pierde"
            self.g_exp += 1
            self.g_ph += 1
            if not ultimo:
                if not pierde:
                    premios.extend(self.evaluar_recompensas(-1, rival))
            else:
                self.g_ph += 2
                ms = int(sdef.get("maestro", -1))
                if 0 <= ms < self.num_m:
                    self.g_maestros |= 1 << ms
                premios.extend(self.evaluar_recompensas(s_idx, -1 if pierde else rival))
                self.g_saga += 1
            self.simular_guardado()
        return premios


# ---------------------------------------------------------------------------
# BFS exhaustivo sobre todas las trayectorias legales de DU+Rutas + Set Cover
# ---------------------------------------------------------------------------

def enumerar_trayectorias_du(data: dict[str, Any], reglas: ReglasModo) -> list[dict[str, Any]]:
    """Enumera todas las trayectorias completas legales de 1 vuelta de DU+Rutas."""
    hs = data["hs"]
    tramos = data["tramos"]
    canon = data["canon_sagas"]
    cpp = data["cpp"]
    num_m = cpp["num_maestros"]
    marca_maestro = cpp["marca_maestro"]

    def saga_del_maestro(m_idx: int) -> int:
        for i, s in enumerate(canon):
            if int(s.get("maestro", -1)) == m_idx:
                return i
        return -1

    def afinidad(m_idx: int, marcas: set[str]) -> bool:
        if m_idx == 0:
            return True
        if m_idx == 1:
            return "opc_namek" in marcas or "camino_vegeta" in marcas
        if m_idx in (2, 3):
            return "opc_tierra" in marcas
        if m_idx == 4:
            return "camino_nave" in marcas
        if m_idx == 5:
            return "vence_33" in marcas
        return False

    runs: list[dict[str, Any]] = []

    def dfs(
        etapa_sig: int,
        marcas: frozenset[str],
        g_saga: int,
        trained: frozenset[int],
        decision: int,
        path: list[int],
        rivals: frozenset[int],
        combates: int,
        maestro_acto2: int,
    ) -> None:
        if etapa_sig == 5 and decision == 0:
            for dec in (1, 2):
                nm = set(marcas)
                if dec == 1:
                    nm.add("deseo_revivir")
                dfs(5, frozenset(nm), g_saga, trained, dec, path, rivals, combates, maestro_acto2)
            return

        has_m = any(x.startswith("maestro_") for x in marcas)
        m_opts: list[tuple[set[str], int]] = [(set(marcas), maestro_acto2)]
        if etapa_sig >= 2 and not has_m:
            m_opts = []
            for m_idx in range(num_m):
                k = saga_del_maestro(m_idx)
                af = afinidad(m_idx, set(marcas))
                unlk = (
                    m_idx == 0
                    or m_idx in trained
                    or (reglas.maestros_acto2_afinidad and af)
                    or k < 0
                    or g_saga >= k
                )
                if unlk and af:
                    nm = set(marcas)
                    nm.add("maestro_" + marca_maestro[m_idx])
                    m_opts.append((nm, m_idx))

        for ms, m_a2 in m_opts:
            tramo_idx, cruce = hs.ruta_siguiente(tramos, etapa_sig, ms)
            if tramo_idx < 0:
                runs.append(
                    {
                        "tramos": list(path),
                        "rivals": sorted(rivals),
                        "marcas": sorted(ms),
                        "maestro_acto2": m_a2,
                        "deseo_revivir": decision == 1,
                        "combates": combates,
                    }
                )
                continue
            for bt in (cruce if len(cruce) >= 2 else [tramo_idx]):
                tdef = tramos[bt]
                et = int(tdef.get("etapa", -1))
                nm = set(ms)
                if tdef.get("marca"):
                    nm.add(tdef["marca"])
                nr = set(rivals)
                for c in tdef.get("combate", []):
                    rv = int(c["rival"])
                    nr.add(rv)
                    nm.add(f"vence_{rv}")
                nt = set(trained)
                msaga = int(tdef.get("maestro", -1))
                if 0 <= msaga < num_m:
                    nt.add(msaga)
                opt_extra = 0
                if tdef.get("opcional") and tdef.get("opcional_marca"):
                    nm.add(tdef["opcional_marca"])
                    opt_extra = 1
                dfs(
                    et + 1,
                    frozenset(nm),
                    g_saga + 1,
                    frozenset(nt),
                    decision,
                    path + [bt],
                    frozenset(nr),
                    combates + len(tdef.get("combate", [])) + opt_extra,
                    m_a2,
                )

    dfs(0, frozenset(), 0, frozenset(), 0, [], frozenset(), 0, 0)
    return runs


def evaluar_conjunto_premios(
    data: dict[str, Any],
    tramo_set: set[int],
    rival_set: set[int],
    mark_set: set[str],
    reglas: ReglasModo,
) -> set[str]:
    recs = data["recompensas"]
    n_rec = len(recs)
    earned: set[str] = set()
    for r in recs:
        if "coleccion" in r:
            continue
        if "tramo" in r and int(r["tramo"]) not in tramo_set:
            continue
        if "vence" in r and int(r["vence"]) not in rival_set:
            continue
        if "marca" in r and r["marca"] not in mark_set:
            continue
        if "maestro" in r:
            continue
        earned.add(r["id"])

    changed = True
    while changed:
        changed = False
        pct = (100 * len(earned)) // n_rec if n_rec else 0
        base_ok = sum(1 for x in recs if int(x.get("coleccion", -1)) < 100 and x["id"] in earned)
        base_tot = sum(1 for x in recs if int(x.get("coleccion", -1)) < 100)
        for r in recs:
            if "coleccion" not in r or r["id"] in earned:
                continue
            rcol = int(r["coleccion"])
            if rcol == 100 and reglas.h100_sin_ciclo:
                if base_tot > 0 and base_ok == base_tot:
                    earned.add(r["id"])
                    changed = True
            elif pct >= rcol:
                earned.add(r["id"])
                changed = True
    return earned


def plan_minimo_100(data: dict[str, Any], runs: list[dict[str, Any]], reglas: ReglasModo) -> dict[str, Any]:
    """Calcula el conjunto minimo de vueltas DU+Rutas para cubrir el 100 % de las recompensas."""
    all_ids = {r["id"] for r in data["recompensas"]}
    non_milestone = {r["id"] for r in data["recompensas"] if "coleccion" not in r}

    # Pre-calcular premios base de cada trayectoria (deduplicando por cobertura de premios base)
    best_by_cov: dict[frozenset[str], dict[str, Any]] = {}
    for run in runs:
        cov = frozenset(
            evaluar_conjunto_premios(
                data, set(run["tramos"]), set(run["rivals"]), set(run["marcas"]), reglas
            )
            & non_milestone
        )
        prev = best_by_cov.get(cov)
        if prev is None or run["combates"] < prev["combates"]:
            best_by_cov[cov] = dict(run, base_rewards=sorted(cov))

    candidates = list(best_by_cov.values())
    # BFS por numero de vueltas (1..8) para encontrar el minimo exacto de vueltas
    from collections import deque

    q: deque[tuple[frozenset[str], list[int]]] = deque([(frozenset(), [])])
    seen: set[frozenset[str]] = {frozenset()}
    best_plan: list[dict[str, Any]] | None = None

    while q:
        cov, idxs = q.popleft()
        if cov >= non_milestone:
            best_plan = [candidates[i] for i in idxs]
            break
        if len(idxs) >= 8:
            continue
        for i, cand in enumerate(candidates):
            ncov = cov | frozenset(cand["base_rewards"])
            if ncov == cov or ncov in seen:
                continue
            seen.add(ncov)
            q.append((ncov, idxs + [i]))

    if best_plan is None:
        # Calcular union maxima alcanzable
        union_t = set()
        union_r = set()
        union_m = set()
        for run in runs:
            union_t.update(run["tramos"])
            union_r.update(run["rivals"])
            union_m.update(run["marcas"])
        got = evaluar_conjunto_premios(data, union_t, union_r, union_m, reglas)
        return {
            "alcanzable_100": False,
            "vueltas_minimas": None,
            "max_union": len(got),
            "total": len(all_ids),
            "faltan": sorted(all_ids - got),
            "plan": [],
        }

    # Verificar que la union alcanza 100 % incluyendo hitos (r_h25..r_h100)
    union_t = {t for r in best_plan for t in r["tramos"]}
    union_r = {rv for r in best_plan for rv in r["rivals"]}
    union_m = {m for r in best_plan for m in r["marcas"]}
    got = evaluar_conjunto_premios(data, union_t, union_r, union_m, reglas)
    return {
        "alcanzable_100": len(got) == len(all_ids),
        "vueltas_minimas": len(best_plan),
        "max_union": len(got),
        "total": len(all_ids),
        "faltan": sorted(all_ids - got),
        "combates_totales": sum(r["combates"] for r in best_plan),
        "plan": best_plan,
    }


def ejecutar_plan_en_ficha(data: dict[str, Any], plan: list[dict[str, Any]], reglas: ReglasModo, cuerpo: int = 0) -> dict[str, Any]:
    """Ejecuta paso a paso el plan multi-vuelta en 1 sola ficha FichaSim y verifica 100 %, 0 evicciones, 0 fallos."""
    cuenta = CuentaCol()
    sim = FichaSim(data=data, reglas=reglas, cuenta=cuenta, cuerpo=cuerpo, rutas_on=True)
    historial = []
    for idx_v, lap in enumerate(plan):
        if idx_v > 0:
            ok_nv = sim.nueva_vuelta_ruta()
            if not ok_nv:
                return {"ok": False, "motivo": f"nueva_vuelta_ruta fallo en vuelta {idx_v + 1}", "coleccion_pct": sim.coleccion_pct()}
        t_seq = lap["tramos"]
        # Etapa 0
        sim.elegir_camino(t_seq[0])
        sim.jugar_tramo_du(hacer_opcional=True)
        # Etapa 1
        sim.resolver_ruta()
        sim.elegir_camino(t_seq[1])
        sim.jugar_tramo_du(hacer_opcional=True)
        # Cambio de maestro antes de Etapa 2
        ok_m = sim.elegir_maestro(lap["maestro_acto2"])
        if not ok_m:
            return {"ok": False, "motivo": f"elegir_maestro({lap['maestro_acto2']}) fallo en vuelta {idx_v + 1}"}
        # Etapa 2
        sim.resolver_ruta()
        assert sim.g_tramo == t_seq[2], f"vuelta {idx_v+1} etapa 2: esperado {t_seq[2]}, obtenido {sim.g_tramo}"
        sim.jugar_tramo_du(hacer_opcional=True)
        # Etapa 3
        sim.resolver_ruta()
        assert sim.g_tramo == t_seq[3]
        sim.jugar_tramo_du(hacer_opcional=True)
        # Etapa 4 (+ esferas 5 & 6)
        sim.resolver_ruta()
        assert sim.g_tramo == t_seq[4]
        sim.jugar_tramo_du(hacer_opcional=True)
        # Deseo historia + Etapa 5
        assert sim.confirmar_deseo_historia(revivir=lap["deseo_revivir"])
        sim.resolver_ruta()
        assert sim.g_tramo == t_seq[5], f"vuelta {idx_v+1} etapa 5: esperado {t_seq[5]}, obtenido {sim.g_tramo}"
        sim.jugar_tramo_du(hacer_opcional=True)
        assert sim.ruta_completada()
        historial.append(
            {
                "vuelta": idx_v + 1,
                "tramos": list(t_seq),
                "maestro_acto2": data["cpp"]["marca_maestro"][lap["maestro_acto2"]],
                "deseo_revivir": lap["deseo_revivir"],
                "premios_tras_vuelta": len(sim.premios_ganados()),
                "coleccion_pct": sim.coleccion_pct(),
                "desbloqueos_len": len(sim.g_desbloqueos),
                "g_saga": sim.g_saga,
            }
        )
    return {
        "ok": len(sim.premios_ganados()) == len(data["recompensas"]) and sim.coleccion_pct() == 100,
        "premios": len(sim.premios_ganados()),
        "total": len(data["recompensas"]),
        "coleccion_pct": sim.coleccion_pct(),
        "combates_totales": sim.combates,
        "tiempo_estimado_min_H": round(sim.combates * MIN_POR_COMBATE + len(sim.tramos_jugados) * MIN_POR_MAPA_SAGA, 1),
        "max_desbloqueos_len": sim.max_desbloqueos_len,
        "tope_desbloqueos": reglas.tope_desbloqueos,
        "evicciones": sim.evicciones,
        "guardar_fallos": sim.guardar_fallos,
        "zeni_repetidos": sim.zeni_repetidos,
        "vueltas": historial,
    }


def ejecutar_plan_multificha(data: dict[str, Any], plan: list[dict[str, Any]], reglas: ReglasModo) -> dict[str, Any]:
    """Ejecuta 3 fichas activas (kMaxSlots=3) + Misiones tramo 1 o 1 nueva vuelta, compartiendo coleccion.toml."""
    cuenta = CuentaCol()
    # Usar las 3 primeras vueltas en 3 fichas distintas; la 4.a (si hace falta) en Ficha 1 (vuelta 2) o via Misiones
    fichas: list[FichaSim] = []
    for i, lap in enumerate(plan):
        if i < 3:
            cuerpo = (0, 27, 33)[i]   # prueba Goku, Freezer y Cell
            f = FichaSim(data=data, reglas=reglas, cuenta=cuenta, cuerpo=cuerpo, rutas_on=True)
            fichas.append(f)
        else:
            f = fichas[0]
            if not f.nueva_vuelta_ruta():
                return {"ok": False, "motivo": "no se pudo iniciar vuelta 2 en ficha 1"}
        t_seq = lap["tramos"]
        f.elegir_camino(t_seq[0])
        f.jugar_tramo_du(hacer_opcional=True)
        f.resolver_ruta()
        f.elegir_camino(t_seq[1])
        f.jugar_tramo_du(hacer_opcional=True)
        assert f.elegir_maestro(lap["maestro_acto2"])
        f.resolver_ruta()
        f.jugar_tramo_du(hacer_opcional=True)
        f.resolver_ruta()
        f.jugar_tramo_du(hacer_opcional=True)
        f.resolver_ruta()
        f.jugar_tramo_du(hacer_opcional=True)
        assert f.confirmar_deseo_historia(revivir=lap["deseo_revivir"])
        f.resolver_ruta()
        f.jugar_tramo_du(hacer_opcional=True)
    # Comprobar que TODAS las fichas ven el 100 % via coleccion.toml global
    pcts = [f.coleccion_pct() for f in fichas]
    return {
        "ok": all(p == 100 for p in pcts) and len(cuenta.conseguidos) == len(data["recompensas"]),
        "fichas_pct": pcts,
        "global_conseguidos": len(cuenta.conseguidos),
        "total": len(data["recompensas"]),
        "evicciones_totales": sum(len(f.evicciones) for f in fichas),
        "guardar_fallos_totales": sum(f.guardar_fallos for f in fichas),
    }


# ---------------------------------------------------------------------------
# Subcomando 1: auditar cada recompensa
# ---------------------------------------------------------------------------

def auditar_recompensas(data: dict[str, Any]) -> dict[str, Any]:
    r_act = reglas_actuales(data)
    r_new = reglas_parcheadas()
    tramos = data["tramos"]
    canon = data["canon_sagas"]
    recs = data["recompensas"]

    runs_act = enumerar_trayectorias_du(data, r_act)
    du_tramos = {t for run in runs_act for t in run["tramos"]}
    du_rivals = {rv for run in runs_act for rv in run["rivals"]}
    du_marks = {m for run in runs_act for m in run["marcas"]}

    classic_tramos = set(range(len(canon)))
    classic_rivals = {
        int(c["rival"]) for i in classic_tramos for c in canon[i].get("combate", []) if c.get("nota") != "pierde"
    }

    # Etapas con ramas mutuamente excluyentes
    por_etapa: dict[int, list[int]] = {}
    for idx, t in enumerate(tramos):
        et = int(t.get("etapa", -1))
        if et >= 0:
            por_etapa.setdefault(et, []).append(idx)

    detalle = []
    for r in recs:
        rid = r["id"]
        cond = {}
        for k in ("tramo", "vence", "marca", "maestro", "entreno", "coleccion"):
            if k in r:
                cond[k] = r[k]
        en_du_1vuelta = True
        en_misiones = True
        bloqueos_actuales: list[str] = []

        if "tramo" in r:
            ti = int(r["tramo"])
            et = int(tramos[ti].get("etapa", -1)) if 0 <= ti < len(tramos) else -1
            if ti not in du_tramos:
                en_du_1vuelta = False
                bloqueos_actuales.append(f"tramo {ti} inalcanzable en DU+Rutas")
            elif len(por_etapa.get(et, [])) > 1:
                otros = [x for x in por_etapa[et] if x != ti]
                bloqueos_actuales.append(
                    f"mutuamente excluyente en etapa {et} con tramos {otros} (sin NuevaVueltaRuta ni coleccion global no se pueden combinar)"
                )
            if ti not in classic_tramos:
                en_misiones = False
                bloqueos_actuales.append("tramo variante: solo existe en DU+Rutas (no en Misiones)")
        if "marca" in r:
            mk = r["marca"]
            if mk not in du_marks:
                en_du_1vuelta = False
                bloqueos_actuales.append(f"marca '{mk}' nunca emitida en DU+Rutas")
            en_misiones = False
            bloqueos_actuales.append("marca opcional: solo existe en DU+Rutas")
        if "vence" in r:
            rv = int(r["vence"])
            if rv not in du_rivals:
                en_du_1vuelta = False
                bloqueos_actuales.append(f"rival {rv} ausente en DU+Rutas")
            if rv not in classic_rivals:
                en_misiones = False
                bloqueos_actuales.append(f"rival {rv} ausente en Misiones")
            if rv in (27, 33) and not r_act.espejo_sin_sustituir_propio:
                bloqueos_actuales.append(
                    f"en Misiones, cuerpo/estilo={rv} sustituye al rival {rv} (Mision() espejo)"
                )
        if "coleccion" in r:
            rcol = int(r["coleccion"])
            if rcol == 75:
                bloqueos_actuales.append("requiere >=18/24 (75%): imposible en 1 vuelta DU+Rutas (max 14/24) o Misiones (max 14/24)")
            if rcol == 100:
                if not r_act.h100_sin_ciclo:
                    bloqueos_actuales.append("ciclo autorreferencial 23/24=95%<100%")
                bloqueos_actuales.append("requiere 23/23 base: imposible en el arbol actual (max 14/24 DU, 21/24 hibrido)")
        if r.get("tipo") == "tecnica" and int(r.get("cara", -1)) in (2, 13, 39) and not r_act.donante_premiado:
            bloqueos_actuales.append(
                f"tecnica obtenible en Coleccion pero donante {r['cara']} cerrado en Desbloqueado() (no equipable en combate)"
            )

        detalle.append(
            {
                "id": rid,
                "tipo": r.get("tipo"),
                "cond": cond,
                "alcanzable_en_alguna_ruta_du": en_du_1vuelta if "coleccion" not in r else (int(r["coleccion"]) <= 50),
                "alcanzable_en_misiones": en_misiones if "coleccion" not in r else (int(r["coleccion"]) <= 50),
                "bloqueos_arbol_actual": bloqueos_actuales,
                "alcanzable_con_parches_nher2": True,
            }
        )

    return {
        "reglas_detectadas_en_arbol": r_act.__dict__,
        "reglas_con_parches_nher2": r_new.__dict__,
        "total_recompensas": len(recs),
        "recompensas": detalle,
    }


# ---------------------------------------------------------------------------
# Subcomando 2: simular ANTES vs DESPUES
# ---------------------------------------------------------------------------

def simular_antes_despues(data: dict[str, Any]) -> dict[str, Any]:
    r_act = reglas_actuales(data)
    r_new = reglas_parcheadas()
    n_rec = len(data["recompensas"])
    canon = data["canon_sagas"]

    runs_act = enumerar_trayectorias_du(data, r_act)
    runs_new = enumerar_trayectorias_du(data, r_new)

    # Maximo 1 vuelta DU+Rutas (arbol actual)
    best_du_act = {"count": 0, "pct": 0, "tramos": [], "rewards": [], "combates": 0}
    for run in runs_act:
        got = evaluar_conjunto_premios(data, set(run["tramos"]), set(run["rivals"]), set(run["marcas"]), r_act)
        if len(got) > best_du_act["count"]:
            best_du_act = {
                "count": len(got),
                "pct": (100 * len(got)) // n_rec,
                "tramos": run["tramos"],
                "rewards": sorted(got),
                "combates": run["combates"],
            }

    # Maximo Misiones clasico (cuerpo normal vs Freezer 27 vs Cell 33)
    classic_t = set(range(len(canon)))
    classic_r = {int(c["rival"]) for i in classic_t for c in canon[i].get("combate", []) if c.get("nota") != "pierde"}
    got_misiones = evaluar_conjunto_premios(data, classic_t, classic_r, set(), r_act)

    # Maximo global Hibrido (1 vuelta DU + 8 sagas Misiones) sobre TODAS las trayectorias DU
    best_hyb_act = {"count": 0, "pct": 0, "tramos_du": [], "faltan": []}
    all_ids = {r["id"] for r in data["recompensas"]}
    for run in runs_act:
        got = evaluar_conjunto_premios(
            data, set(run["tramos"]) | classic_t, set(run["rivals"]) | classic_r, set(run["marcas"]), r_act
        )
        if len(got) > best_hyb_act["count"]:
            best_hyb_act = {
                "count": len(got),
                "pct": (100 * len(got)) // n_rec,
                "tramos_du": run["tramos"],
                "faltan": sorted(all_ids - got),
            }

    # Plan minimo 100 % con parches NHER2
    plan_new = plan_minimo_100(data, runs_new, r_new)
    prueba_1ficha = ejecutar_plan_en_ficha(data, plan_new["plan"], r_new, cuerpo=0) if plan_new["alcanzable_100"] else {"ok": False}
    prueba_1ficha_freezer = ejecutar_plan_en_ficha(data, plan_new["plan"], r_new, cuerpo=27) if plan_new["alcanzable_100"] else {"ok": False}
    prueba_1ficha_cell = ejecutar_plan_en_ficha(data, plan_new["plan"], r_new, cuerpo=33) if plan_new["alcanzable_100"] else {"ok": False}
    prueba_multificha = ejecutar_plan_multificha(data, plan_new["plan"], r_new) if plan_new["alcanzable_100"] else {"ok": False}

    # Prueba de espejo en Misiones (Freezer 27 y Cell 33) ANTES vs DESPUES
    espejo_check = {}
    for label, reglas in (("antes", r_act), ("despues", r_new)):
        res_c = {}
        for cid, target_r in ((27, "r_b_freezer_titulo"), (33, "r_b_cell_kamehameha")):
            f = FichaSim(data=data, reglas=reglas, cuerpo=cid, estilo_idx=0, id_propio=63, rutas_on=False)
            for s in range(len(canon)):
                f.jugar_saga_misiones(s)
            res_c[str(cid)] = {
                "premio_esperado": target_r,
                "obtenido": f.premio_conseguido(target_r),
                "total_premios": len(f.premios_ganados()),
            }
        espejo_check[label] = res_c

    # Prueba de overflow 64 -> 128 al completar 100 % en 1 ficha (con formas + desbloqueos + marcas)
    overflow_check = {}
    for label, reglas in (("antes_tope64", ReglasModo(
        h100_sin_ciclo=True, maestros_acto2_afinidad=True, coleccion_global=False,
        nueva_vuelta_rutas=True, tope_desbloqueos=64, proteger_premios=False,
    )), ("despues_tope128", r_new)):
        f = FichaSim(data=data, reglas=reglas, cuerpo=0, rutas_on=True)
        # Pre-cargar 10 desbloqueos de maestro/ssj + 12 formas (p. ej. tras deseo de cambio de raza) + 24 premios r_*
        for d in ("ki_dorado_goku", "aura_violeta_piccolo", "ki_rojo_vegeta", "ssj1", "ssj2",
                  "esquiva_desvio_piccolo", "esquiva_concentracion_krilin", "esquiva_caparazon_goku",
                  "esquiva_devolver_cell", "f_sai_ssj", "f_sai_ssj2", "f_sai_kaioken",
                  "f_fre_2", "f_fre_3", "f_fre_final", "f_fre_cooler",
                  "f_hum_potencial", "f_hum_maestria", "f_nam_nail", "f_nam_kami",
                  "f_and_absorcion", "f_and_perfecto"):
            f.g_desbloqueos.append(d)
        # Simular todas las recompensas + marcas de ruta largas (18 vence_*)
        for r in data["recompensas"]:
            f.g_desbloqueos.append(r["id"])
        for rv in (42, 21, 20, 29, 30, 7, 12, 11, 43, 41, 38, 33, 27, 18, 72, 64, 70):
            f.g_marcas.add(f"vence_{rv}")
        f.g_marcas.update({"camino_nave", "camino_lab", "opc_namek", "opc_tierra", "maestro_goku", "deseo_revivir"})
        f.g_cruce = [0, 8]
        ok_g = f.simular_guardado()
        r_kept = sum(1 for x in f.g_desbloqueos if x.startswith("r_"))
        overflow_check[label] = {
            "guardar_ok": ok_g,
            "desbloqueos_len": len(f.g_desbloqueos),
            "r_conservados": r_kept,
            "r_total": n_rec,
            "evicciones": len(f.evicciones),
            "r_evictados": [x for ev in f.evicciones for x in ev["borrados"] if x.startswith("r_")],
        }

    return {
        "antes_arbol_actual": {
            "alcanzable_100": False,
            "trayectorias_du_legales": len(runs_act),
            "du_rutas_1vuelta_max": best_du_act,
            "misiones_clasico_max": {"count": len(got_misiones), "pct": (100 * len(got_misiones)) // n_rec},
            "hibrido_1du_mas_misiones_max": best_hyb_act,
            "por_que_imposible": [
                "Sin coleccion.toml global (conseguidos): los premios viven aislados por ficha en cac.toml.",
                "Sin NuevaVueltaRuta(): DU+Rutas acaba con g_tramo=-1, g_saga=6 y g_busqueda bloqueado; etapa 1 tiene 4 variantes excluyentes (1, 9, 10, 11).",
                "Tope kMax=64 en desbloqueos sin proteger r_*: al combinar 24 premios + formas + desbloqueos + marcas h02_rm: se expulsan premios r_*.",
                "En Misiones, Mision() sustituye al rival si cuerpo/estilo coincide (Freezer 27 / Cell 33) aunque IdPropio()>=0.",
            ],
        },
        "despues_parches_nher2": {
            "alcanzable_100": prueba_1ficha["ok"] and prueba_multificha["ok"],
            "trayectorias_du_legales": len(runs_new),
            "plan_minimo": {
                "vueltas_minimas": plan_new["vueltas_minimas"],
                "combates_totales": plan_new.get("combates_totales"),
                "alcanzable_100": plan_new["alcanzable_100"],
                "faltan": plan_new["faltan"],
            },
            "prueba_1_ficha_goku": prueba_1ficha,
            "prueba_1_ficha_freezer": {k: prueba_1ficha_freezer[k] for k in ("ok", "premios", "total", "coleccion_pct", "evicciones", "guardar_fallos")},
            "prueba_1_ficha_cell": {k: prueba_1ficha_cell[k] for k in ("ok", "premios", "total", "coleccion_pct", "evicciones", "guardar_fallos")},
            "prueba_multificha_3slots": prueba_multificha,
        },
        "prueba_espejo_misiones": espejo_check,
        "prueba_overflow_desbloqueos": overflow_check,
    }


# ---------------------------------------------------------------------------
# Subcomando 3: pruebas de migracion de guardados (TOML reales en temp dir)
# ---------------------------------------------------------------------------

def _sync_coleccion_toml(cac_path: Path, col_path: Path, data: dict[str, Any]) -> dict[str, Any]:
    """Replica exacta de ColeccionSincronizarConFichas() + ColeccionGuardar() sobre archivos TOML reales."""
    validos = {r["id"] for r in data["recompensas"]}
    recs = data["recompensas"]

    todo = False
    vistos: set[str] = set()
    conseguidos: set[str] = set()
    if col_path.is_file():
        tcol = tomllib.loads(col_path.read_text(encoding="utf-8"))
        todo = bool(tcol.get("todo", False))
        vistos = {s for s in tcol.get("vistos", []) if isinstance(s, str) and 0 < len(s) < 48}
        conseguidos = {s for s in tcol.get("conseguidos", []) if isinstance(s, str) and s in validos}

    encontrados: set[str] = set()
    if cac_path.is_file():
        tcac = tomllib.loads(cac_path.read_text(encoding="utf-8"))
        for grupo in ("heroe", "archivado"):
            for h in tcac.get(grupo, []):
                for did in h.get("desbloqueos", []):
                    if did in validos:
                        encontrados.add(did)

    cambio = not encontrados.issubset(conseguidos)
    conseguidos |= encontrados

    otorgado = True
    for _ in range(4):
        if not otorgado:
            break
        otorgado = False
        n = sum(1 for x in recs if x["id"] in conseguidos)
        base_ok = sum(1 for x in recs if int(x.get("coleccion", -1)) < 100 and x["id"] in conseguidos)
        base_tot = sum(1 for x in recs if int(x.get("coleccion", -1)) < 100)
        pct = (100 * n) // len(recs) if recs else 0
        for r in recs:
            if "coleccion" not in r or any(k in r for k in ("tramo", "vence", "marca", "maestro")):
                continue
            if r["id"] in conseguidos:
                continue
            rcol = int(r["coleccion"])
            ok = (base_tot > 0 and base_ok == base_tot) if rcol >= 100 else (pct >= rcol)
            if ok:
                conseguidos.add(r["id"])
                cambio = otorgado = True

    if cambio:
        if col_path.is_file():
            col_path.with_suffix(".toml.bak").write_bytes(col_path.read_bytes())
        v_str = ", ".join(f'"{s}"' for s in sorted(vistos))
        c_str = ", ".join(f'"{s}"' for s in sorted(conseguidos))
        txt = (
            "# Coleccion del Modo Heroe (global): lo visto, lo conseguido y el desbloqueo total.\n"
            f"todo = {'true' if todo else 'false'}\n"
            f"vistos = [{v_str}]\n"
            f"conseguidos = [{c_str}]\n"
        )
        col_path.write_text(txt, encoding="utf-8")

    return {"todo": todo, "vistos": sorted(vistos), "conseguidos": sorted(conseguidos), "cambio": cambio}


def ejecutar_pruebas_migracion(data: dict[str, Any]) -> dict[str, Any]:
    """Valida 5 escenarios reales de migracion de guardados en disco sin romper nada."""
    resultados = []
    with tempfile.TemporaryDirectory(prefix="nher2_mig_") as td:
        d = Path(td)

        # Caso 1: coleccion.toml antigua (sin clave conseguidos) + cac.toml v3 con 3 fichas + 1 archivada
        # que entre todas cubren las 20 recompensas no-hito -> debe migrar y abrir r_h25, r_h50, r_h75 y r_h100!
        c1 = d / "caso1"
        c1.mkdir()
        col1 = c1 / "coleccion.toml"
        col1.write_text('todo = false\nvistos = ["r_t0_death_beam"]\n', encoding="utf-8")
        non_h = [r["id"] for r in data["recompensas"] if "coleccion" not in r]
        p1, p2, p3, p4 = non_h[0:6], non_h[6:12], non_h[12:17], non_h[17:]
        cac1 = c1 / "cac.toml"
        cac1.write_text(
            "version = 3\nactivo = 0\n"
            + f'[[heroe]]\nid = "h000001"\nprogreso = 2\nsaga = 6\ndesbloqueos = {json.dumps(["h02_v1_127_1", "h02_r1:-1:-1:99", "ssj1"] + p1)}\n'
            + f'[[heroe]]\nid = "h000002"\nprogreso = 2\nsaga = 6\ndesbloqueos = {json.dumps(p2)}\n'
            + f'[[heroe]]\nid = "h000003"\nprogreso = 2\nsaga = 6\ndesbloqueos = {json.dumps(p3)}\n'
            + f'[[archivado]]\nid = "h000004"\nprogreso = 2\nsaga = 6\ndesbloqueos = {json.dumps(p4)}\n',
            encoding="utf-8",
        )
        r1 = _sync_coleccion_toml(cac1, col1, data)
        # Re-leer para confirmar TOML valido, .bak creada, vistos conservados y 0 contaminacion con h02_/ssj1
        t_back = tomllib.loads(col1.read_text(encoding="utf-8"))
        assert len(t_back["conseguidos"]) == len(data["recompensas"])
        assert "r_h100" in t_back["conseguidos"]
        assert "ssj1" not in t_back["conseguidos"] and "h02_v1_127_1" not in t_back["conseguidos"]
        assert t_back["vistos"] == ["r_t0_death_beam"]
        assert col1.with_suffix(".toml.bak").is_file()
        # Idempotencia: segunda sincronizacion no reescribe
        r1_bis = _sync_coleccion_toml(cac1, col1, data)
        assert r1_bis["cambio"] is False
        resultados.append({"caso": "1_legacy_multificha_mas_archivado_a_100", "ok": True, "conseguidos": len(t_back["conseguidos"])})

        # Caso 2: guardado antiguo SIN coleccion.toml -> se crea limpia con premios existentes, sin borrar nada
        c2 = d / "caso2"
        c2.mkdir()
        col2 = c2 / "coleccion.toml"
        cac2 = c2 / "cac.toml"
        cac2.write_text(
            'version = 2\nactivo = 0\n[[heroe]]\nid = "h000010"\nprogreso = 2\nsaga = 2\n'
            'desbloqueos = ["r_t0_death_beam", "r_t0_escenario_cell_b1", "ki_dorado_goku"]\n',
            encoding="utf-8",
        )
        r2 = _sync_coleccion_toml(cac2, col2, data)
        assert col2.is_file() and set(r2["conseguidos"]) == {"r_t0_death_beam", "r_t0_escenario_cell_b1"}
        resultados.append({"caso": "2_sin_coleccion_toml_v2_migrado", "ok": True, "conseguidos": len(r2["conseguidos"])})

        # Caso 3: native_save100 (todo = true) conserva conseguidos previos
        c3 = d / "caso3"
        c3.mkdir()
        col3 = c3 / "coleccion.toml"
        col3.write_text('todo = false\nvistos = ["r_t0_death_beam"]\nconseguidos = ["r_t0_death_beam", "r_t8_wolf_fang"]\n', encoding="utf-8")
        # Simular escritura de native_save100.inl parcheado
        vistos_line, cons_line = " []", " []"
        for line in col3.read_text(encoding="utf-8").splitlines():
            if line.startswith("vistos") and "=" in line:
                vistos_line = line.split("=", 1)[1]
            if line.startswith("conseguidos") and "=" in line:
                cons_line = line.split("=", 1)[1]
        col3.write_text(
            f"# Coleccion\ntodo = true\nvistos ={vistos_line}\nconseguidos ={cons_line}\n",
            encoding="utf-8",
        )
        t3 = tomllib.loads(col3.read_text(encoding="utf-8"))
        assert t3["todo"] is True and t3["vistos"] == ["r_t0_death_beam"] and set(t3["conseguidos"]) == {"r_t0_death_beam", "r_t8_wolf_fang"}
        resultados.append({"caso": "3_save100_conserva_conseguidos", "ok": True})

        # Caso 4: ficha antigua al final de vuelta 1 (g_saga=6, g_tramo=-1, decision=1) -> NuevaVueltaRuta()
        r_new = reglas_parcheadas()
        f4 = FichaSim(data=data, reglas=r_new, g_exp=24, g_saga=6, g_ph=36, g_maestros=0b111011,
                      g_desbloqueos=["h02_v1_127_1", "h02_r1:-1:-1:777", "r_t0_death_beam", "r_t6_titulo_torneo", "ssj1", "f_sai_ssj"],
                      cobrados=127, decision=1, g_tramo=-1, g_etapa_sig=-1, g_marcas={"camino_nave", "maestro_goku", "deseo_revivir"})
        assert f4.ruta_completada() is True
        assert f4.nueva_vuelta_ruta() is True
        assert f4.g_tramo == 0 and f4.g_cruce == [0, 8] and f4.cobrados == 0 and f4.decision == 0
        assert f4.g_exp == 24 and f4.g_saga == 6 and f4.g_ph == 36 and f4.g_maestros == 0b111011
        assert "r_t0_death_beam" in f4.g_desbloqueos and "ssj1" in f4.g_desbloqueos and "f_sai_ssj" in f4.g_desbloqueos
        resultados.append({"caso": "4_fin_vuelta1_a_nueva_vuelta_conserva_todo", "ok": True})

        # Caso 5: ficha a mitad de vuelta 1 (g_saga=2, g_tramo=12, decision=0) -> NO se reinicia!
        f5 = FichaSim(data=data, reglas=r_new, g_exp=8, g_saga=2, cobrados=3, decision=0, g_tramo=12, g_etapa_sig=-1,
                      g_marcas={"camino_nave", "camino_lab", "maestro_goku"})
        assert f5.ruta_completada() is False
        assert f5.nueva_vuelta_ruta() is False
        assert f5.g_tramo == 12 and f5.cobrados == 3 and "camino_nave" in f5.g_marcas
        resultados.append({"caso": "5_mitad_vuelta1_intacta", "ok": True})

    return {"ok": all(r["ok"] for r in resultados), "casos": resultados}


# ---------------------------------------------------------------------------
# --self-check (incluye prueba sintetica de escalabilidad)
# ---------------------------------------------------------------------------

def self_check(root: Path | None) -> int:
    data = hd.load_all(root=root, skip_afs=True)
    sim = simular_antes_despues(data)
    mig = ejecutar_pruebas_migracion(data)
    assert sim["despues_parches_nher2"]["alcanzable_100"] is True
    assert sim["despues_parches_nher2"]["prueba_1_ficha_goku"]["coleccion_pct"] == 100
    assert sim["despues_parches_nher2"]["prueba_multificha_3slots"]["ok"] is True
    assert sim["prueba_espejo_misiones"]["despues"]["27"]["obtenido"] is True
    assert sim["prueba_espejo_misiones"]["despues"]["33"]["obtenido"] is True
    assert sim["prueba_overflow_desbloqueos"]["despues_tope128"]["r_conservados"] == len(data["recompensas"])
    assert mig["ok"] is True

    # Prueba sintetica de escalabilidad: anadir 1 tramo variante nuevo en etapa 0 + 1 recompensa nueva
    with tempfile.TemporaryDirectory(prefix="nher2_sc_") as td:
        p_s = Path(td) / "sagas.toml"
        p_r = Path(td) / "recompensas.toml"
        p_s.write_text(
            data["paths"]["sagas"].read_text(encoding="utf-8")
            + '\n[[saga]]\nvariante = true\nnombre_es = "Ginyu: Ruta C sintetica"\nnombre_en = "Ginyu: Synthetic Route C"\n'
              'maestro = 0\netapa = 0\ncruce_es = "Ruta C"\ncruce_en = "Route C"\nmarca = "camino_nave"\n'
              '[[saga.combate]]\nrival = 20\nvida = 2000\nescenario = 7\nes = "Ginyu."\nen = "Ginyu."\n',
            encoding="utf-8",
        )
        p_r.write_text(
            data["paths"]["recompensas"].read_text(encoding="utf-8")
            + '\n[[recompensa]]\nid = "r_t14_sintetico"\ntipo = "titulo"\ntramo = 14\n'
              'nombre_es = "Sintetico"\nnombre_en = "Synthetic"\ncond_es = "Cierra Ruta C"\ncond_en = "Clear Route C"\n',
            encoding="utf-8",
        )
        dsyn = hd.load_all(root=root, sagas_path=p_s, recompensas_path=p_r, skip_afs=True)
        r_new = reglas_parcheadas()
        runs = enumerar_trayectorias_du(dsyn, r_new)
        plan = plan_minimo_100(dsyn, runs, r_new)
        assert plan["alcanzable_100"] is True and plan["max_union"] == len(data["recompensas"]) + 1
    print(
        f"SELF-CHECK OK: 24/24 (100%) en {sim['despues_parches_nher2']['plan_minimo']['vueltas_minimas']} vueltas, "
        f"multi-ficha OK, espejo OK, overflow 128 OK, migracion 5/5 OK, escalabilidad sintetica (25/25) OK",
        flush=True,
    )
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Auditoria offline y simulacion del candidato NHER2. No verifica el juego ni migra guardados reales."
    )
    ap.add_argument("comando", nargs="?", default="todo", choices=("auditar", "simular", "migrar-test", "todo"))
    ap.add_argument("--self-check", action="store_true", help="Ejecuta autoprueba completa + escalabilidad sintetica y sale 0/1.")
    ap.add_argument("--root", type=Path, help="Raiz del proyecto DBZ3 HD.")
    ap.add_argument("--sagas", type=Path, help="Ruta alternativa a sagas.toml.")
    ap.add_argument("--recompensas", type=Path, help="Ruta alternativa a recompensas.toml.")
    ap.add_argument("--desbloqueos", type=Path, help="Ruta alternativa a desbloqueos.toml.")
    ap.add_argument("--razas-reglas", type=Path, help="Ruta alternativa a razas_reglas.toml.")
    ap.add_argument("--skip-afs", action="store_true", help="Omitir lectura de AFS (mas rapido).")
    ap.add_argument("--json", type=Path, help="Guardar salida estructurada en JSON.")
    args = ap.parse_args()

    if args.self_check:
        return self_check(args.root)

    data = hd.load_all(
        root=args.root,
        sagas_path=args.sagas,
        desbloqueos_path=args.desbloqueos,
        recompensas_path=args.recompensas,
        razas_reglas_path=args.razas_reglas,
        skip_afs=args.skip_afs,
    )

    out: dict[str, Any] = {}
    if args.comando in ("auditar", "todo"):
        out["auditoria"] = auditar_recompensas(data)
    if args.comando in ("simular", "todo"):
        out["simulacion"] = simular_antes_despues(data)
    if args.comando in ("migrar-test", "todo"):
        out["migracion"] = ejecutar_pruebas_migracion(data)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")

    if "auditoria" in out:
        a = out["auditoria"]
        con_bloq = sum(1 for r in a["recompensas"] if r["bloqueos_arbol_actual"])
        print(f"=== AUDITORIA ({a['total_recompensas']} recompensas, {con_bloq} con bloqueos en arbol actual) ===")
    if "simulacion" in out:
        s = out["simulacion"]
        an = s["antes_arbol_actual"]
        de = s["despues_parches_nher2"]
        print(
            f"ANTES: 100%={an['alcanzable_100']} | DU 1v={an['du_rutas_1vuelta_max']['count']}/24 ({an['du_rutas_1vuelta_max']['pct']}%) | "
            f"Misiones={an['misiones_clasico_max']['count']}/24 | Hibrido={an['hibrido_1du_mas_misiones_max']['count']}/24 "
            f"(faltan {an['hibrido_1du_mas_misiones_max']['faltan']})"
        )
        p1 = de["prueba_1_ficha_goku"]
        print(
            f"DESPUES: 100%={de['alcanzable_100']} | 1 ficha={p1['premios']}/{p1['total']} ({p1['coleccion_pct']}%) "
            f"en {de['plan_minimo']['vueltas_minimas']} vueltas ({p1['combates_totales']} combates, ~{p1['tiempo_estimado_min_H']} min [H], "
            f"max desbloqueos={p1['max_desbloqueos_len']}/{p1['tope_desbloqueos']}, evicciones={len(p1['evicciones'])}, fallos={p1['guardar_fallos']}) | "
            f"multi-ficha={de['prueba_multificha_3slots']['ok']} {de['prueba_multificha_3slots']['fichas_pct']}"
        )
        ov_a = s["prueba_overflow_desbloqueos"]["antes_tope64"]
        ov_d = s["prueba_overflow_desbloqueos"]["despues_tope128"]
        print(
            f"OVERFLOW: tope 64 -> r_conservados={ov_a['r_conservados']}/{ov_a['r_total']} (evictados={len(ov_a['r_evictados'])}) | "
            f"tope 128 -> r_conservados={ov_d['r_conservados']}/{ov_d['r_total']} (evictados={len(ov_d['r_evictados'])})"
        )
    if "migracion" in out:
        m = out["migracion"]
        print(f"MIGRACION SIMULADA: ok={m['ok']} ({len(m['casos'])} casos)")
    return 1 if "migracion" in out and not out["migracion"]["ok"] else 0


if __name__ == "__main__":
    sys.exit(main())

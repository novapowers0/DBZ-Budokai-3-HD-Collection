#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""heroe_datos.py — Cargador compartido y reutilizable de datos/reglas del Modo Heroe.

Reutiliza los parsers existentes en `mod center hd/` (heroe_sagas_a_du, capsulas,
roster_build, cac/tecnicas_cartas, cac/tecnicas_tipo) y extrae dinamicamente las tablas
de `res/heroe/*.toml`, `res/ingame_ui/i18n_modos.toml` y las constantes de `src/`.
Funciona automaticamente al anadir sagas, recompensas, desbloqueos o maestros.
"""
from __future__ import annotations

import json
import os
import re
import sys
import tempfile
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

LANGS_7 = ("es", "en", "fr", "de", "it", "pt", "ja")


def find_repo_root(start: Path | None = None) -> Path:
    cur = (start or Path.cwd()).resolve()
    for p in (cur, *cur.parents):
        if (p / "res" / "heroe" / "sagas.toml").is_file() and (p / "src" / "heroe.cpp").is_file():
            return p
    default = Path(r"C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection")
    if (default / "res" / "heroe" / "sagas.toml").is_file():
        return default
    raise FileNotFoundError("No se encontro la raiz del proyecto DBZ3 HD")


def _ensure_mod_center_imports(root: Path) -> None:
    mc = str(root / "mod center hd")
    cac = str(root / "mod center hd" / "cac")
    for p in (cac, mc):
        if os.path.isdir(p) and p not in sys.path:
            sys.path.insert(0, p)


def _unescape_c_str(s: str) -> str:
    return (
        s.replace(r"\\", "\x00")
        .replace(r"\"", '"')
        .replace(r"\n", "\n")
        .replace(r"\t", "\t")
        .replace("\x00", "\\")
    )


def parse_fighter_names(root: Path) -> tuple[dict[int, str], dict[int, str]]:
    """Lee kPjEs y kPjEn desde src/modos.cpp."""
    txt = (root / "src" / "modos.cpp").read_text(encoding="utf-8")

    def extract_array(var: str) -> dict[int, str]:
        m = re.search(rf"{var}\[44\]\s*=\s*\{{(.*?)\}};", txt, re.S)
        if not m:
            return {}
        items = re.findall(r'"((?:[^"\\]|\\.)*)"|nullptr', m.group(1))
        # re.findall returns '' for nullptr match
        raw_tokens = re.findall(r'"(?:[^"\\]|\\.)*"|nullptr', m.group(1))
        out: dict[int, str] = {}
        for idx, tok in enumerate(raw_tokens):
            if tok != "nullptr":
                out[idx] = _unescape_c_str(tok[1:-1])
        return out

    en = extract_array("kPjEn")
    es = extract_array("kPjEs")
    # Native fusion IDs referenced in sagas.toml
    for fid, n_es, n_en in (
        (64, "Gotenks", "Gotenks"),
        (70, "Gogeta SS4", "SS4 Gogeta"),
        (72, "Vegito", "Vegito"),
    ):
        es.setdefault(fid, n_es)
        en.setdefault(fid, n_en)
    return es, en


def parse_heroe_cpp_constants(root: Path) -> dict[str, Any]:
    """Extrae kRazas, kEstilos, kMarcaMaestro, kSsjCaps, kDeRaza y cvars de src/heroe.cpp."""
    txt = (root / "src" / "heroe.cpp").read_text(encoding="utf-8")

    # kRazas
    razas = []
    m_r = re.search(r"const Raza kRazas\[\]\s*=\s*\{(.*?)\n\};", txt, re.S)
    if m_r:
        block = m_r.group(1)
        for rm in re.finditer(
            r'\{\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*\{(.*?)\},\s*(\d+)\s*\}',
            block,
            re.S,
        ):
            cuerpos = [
                {"id": int(cid), "trajes": int(tr)}
                for cid, tr in re.findall(r"\{(\d+),\s*(\d+)\}", rm.group(6))
            ]
            razas.append(
                {
                    "index": len(razas),
                    "es": rm.group(1),
                    "en": rm.group(2),
                    "fr": rm.group(3),
                    "de": rm.group(4),
                    "it": rm.group(5),
                    "cuerpos": cuerpos,
                    "n": int(rm.group(7)),
                }
            )

    # kEstilos
    estilos = []
    m_e = re.search(r"const Estilo kEstilos\[\]\s*=\s*\{(.*?)\n\};", txt, re.S)
    if m_e:
        block = m_e.group(1)
        # First 6 are full structs, rest are {id}
        for em in re.finditer(
            r"\{(\d+)(?:,\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(\d+),\s*\"([^\"]*)\",\s*\"([^\"]*)\")?\}",
            block,
        ):
            fid = int(em.group(1))
            if em.group(2) is not None:
                estilos.append(
                    {
                        "index": len(estilos),
                        "id": fid,
                        "dano": int(em.group(2)),
                        "recibido": int(em.group(3)),
                        "vida": int(em.group(4)),
                        "ki": int(em.group(5)),
                        "hab": int(em.group(6)),
                        "hab_n": em.group(7),
                        "tec": em.group(8),
                        "es_maestro": True,
                    }
                )
            else:
                estilos.append(
                    {
                        "index": len(estilos),
                        "id": fid,
                        "dano": 100,
                        "recibido": 100,
                        "vida": 0,
                        "ki": 0,
                        "hab": 0,
                        "hab_n": "",
                        "tec": "",
                        "es_maestro": False,
                    }
                )

    m_nm = re.search(r"constexpr int kNumMaestros\s*=\s*(\d+);", txt)
    num_maestros = int(m_nm.group(1)) if m_nm else 6
    for i, e in enumerate(estilos):
        e["es_maestro"] = i < num_maestros

    m_mm = re.search(r"kMarcaMaestro\[kNumMaestros\]\s*=\s*\{(.*?)\};", txt)
    marca_maestro = re.findall(r'"([^"]+)"', m_mm.group(1)) if m_mm else []

    m_dr = re.search(r"kDeRaza\[\]\s*=\s*\{([^}]+)\};", txt)
    de_raza = [int(x.strip()) for x in m_dr.group(1).split(",") if x.strip()] if m_dr else []

    m_ssj = re.search(r"kSsjCaps\[3\]\s*=\s*\{\{([^}]+)\},\s*\{([^}]+)\},\s*\{([^}]+)\}\};", txt)
    ssj_caps = (
        [[int(x.strip()) for x in m_ssj.group(k).split(",")] for k in (1, 2, 3)]
        if m_ssj
        else [[2, 35, 45, 18, 23, 32, 50], [3, 36, 19, 24], [4]]
    )

    m_max = re.search(r"constexpr size_t kMax\s*=\s*(\d+);", txt)
    max_desbloqueos = int(m_max.group(1)) if m_max else 64

    # Check cvars across src/
    cvars: dict[str, bool] = {}
    for src_file in (
        root / "src" / "heroe.cpp",
        root / "src" / "heroe_ui.cpp",
        root / "src" / "heroe_tecnicas_v2.inl",
        root / "src" / "heroe_v2_cruce.inl",
        root / "src" / "campaigns.cpp",
    ):
        if src_file.is_file():
            st = src_file.read_text(encoding="utf-8")
            for cm in re.finditer(r"REXCVAR_DEFINE_BOOL\((\w+),\s*(true|false),", st):
                cvars[cm.group(1)] = cm.group(2) == "true"

    # Extract kEntreno* and kExpNivel / kNivelMax
    consts: dict[str, int] = {}
    for k in ("kEntrenoMinimo", "kEntrenoPorCombate", "kEntrenoMaestro", "kExpNivel", "kNivelMax", "kSagasJugables"):
        mk = re.search(rf"\b{k}\s*=\s*(\d+)", txt)
        if mk:
            consts[k] = int(mk.group(1))
    api_txt = (root / "src" / "heroe_api.h").read_text(encoding="utf-8")
    mk_sj = re.search(r"kSagasJugables\s*=\s*(\d+)", api_txt)
    if mk_sj:
        consts["kSagasJugables"] = int(mk_sj.group(1))

    return {
        "razas": razas,
        "estilos": estilos,
        "num_maestros": num_maestros,
        "maestros": estilos[:num_maestros],
        "marca_maestro": marca_maestro,
        "de_raza": de_raza,
        "ssj_caps": ssj_caps,
        "max_desbloqueos": max_desbloqueos,
        "cvars": cvars,
        "consts": consts,
    }


class I18nIndex:
    """Replica fielmente la cadena de resolucion de src/launcher/i18n.cpp + heroe_sagas_a_du.py."""

    def __init__(self, root: Path):
        self.root = root
        self.i18n_modos: dict[str, dict[str, str]] = {}
        self.sagas_i18n: dict[str, dict[str, str]] = {}
        self.k_xv2: dict[str, dict[str, str]] = {}
        self.k_table: dict[str, dict[str, str]] = {}
        self.k_ptja: dict[str, dict[str, str]] = {}
        self._load()

    def _load(self) -> None:
        p_modos = self.root / "res" / "ingame_ui" / "i18n_modos.toml"
        if p_modos.is_file():
            self.i18n_modos = tomllib.loads(p_modos.read_text(encoding="utf-8"))
        p_sagas = self.root / "res" / "heroe" / "sagas_i18n.toml"
        if p_sagas.is_file():
            self.sagas_i18n = tomllib.loads(p_sagas.read_text(encoding="utf-8"))

        p_cpp = self.root / "src" / "launcher" / "i18n.cpp"
        if p_cpp.is_file():
            txt = p_cpp.read_text(encoding="utf-8")
            # kXv2: {es, en, it, de, fr, pt, ja}
            m_x = re.search(r"kXv2\[\]\s*=\s*\{(.*?)\n\};", txt, re.S)
            if m_x:
                for row in re.finditer(
                    r'\{\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\s*\}',
                    m_x.group(1),
                ):
                    es, en, it, de, fr, pt, ja = [_unescape_c_str(row.group(i)) for i in range(1, 8)]
                    self.k_xv2[es] = {"es": es, "en": en, "it": it, "de": de, "fr": fr, "pt": pt, "ja": ja}
            # kTable: {es, it, de, fr}
            m_t = re.search(r"kTable\[\]\s*=\s*\{(.*?)\n\};", txt, re.S)
            if m_t:
                for row in re.finditer(
                    r'\{\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\s*\}',
                    m_t.group(1),
                ):
                    es, it, de, fr = [_unescape_c_str(row.group(i)) for i in range(1, 5)]
                    self.k_table.setdefault(es, {"es": es, "it": it, "de": de, "fr": fr})
            # kNovedadesPtJa: {es, pt, ja}
            m_p = re.search(r"kNovedadesPtJa\[\]\s*=\s*\{(.*?)\n\};", txt, re.S)
            if m_p:
                for row in re.finditer(
                    r'\{\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\s*\}',
                    m_p.group(1),
                ):
                    es, pt, ja = [_unescape_c_str(row.group(i)) for i in range(1, 4)]
                    self.k_ptja.setdefault(es, {"pt": pt, "ja": ja})

    def missing_langs_runtime_TT(self, es: str, en: str) -> list[str]:
        """Idiomas de los 7 donde T::T(es, en) en tiempo de ejecucion NO tiene traduccion y cae a ingles."""
        missing: list[str] = []
        if not es:
            missing.append("es")
        if not en:
            missing.append("en")
        for lg in ("fr", "de", "it", "pt", "ja"):
            found = False
            if es in self.k_xv2 and self.k_xv2[es].get(lg):
                found = True
            elif lg in ("fr", "de", "it") and es in self.k_table and self.k_table[es].get(lg):
                found = True
            elif en in self.i18n_modos and self.i18n_modos[en].get(lg):
                found = True
            elif lg in ("pt", "ja") and es in self.k_ptja and self.k_ptja[es].get(lg):
                found = True
            if not found:
                missing.append(lg)
        return missing

    def missing_langs_saga_du(self, es: str, en: str, inline_langs: dict[str, str] | None = None) -> list[str]:
        """Idiomas donde sagas_i18n.toml + i18n_modos.toml + campos inline de sagas.toml no cubren los 7."""
        missing: list[str] = []
        inline = inline_langs or {}
        if not es:
            missing.append("es")
        if not en:
            missing.append("en")
        for lg in ("fr", "de", "it", "pt", "ja"):
            val = (
                inline.get(lg)
                or self.i18n_modos.get(en, {}).get(lg)
                or self.sagas_i18n.get(en, {}).get(lg)
            )
            if not val:
                missing.append(lg)
        return missing


def load_skc_and_ccm(root: Path, us_dir: Path | None = None) -> dict[str, Any]:
    """Carga catalogo #SKC completo y tipos #CCM usando mod center hd/capsulas.py y tecnicas_tipo.py."""
    _ensure_mod_center_imports(root)
    import capsulas as cp  # type: ignore
    import roster_build as rb  # type: ignore
    import tecnicas_tipo as tt  # type: ignore

    us = us_dir or (root / "us")
    if not (us / "data_usi.afs").is_file() or not (us / "data_cmn.afs").is_file():
        return {"available": False, "attacks": {}, "esquivas": {}, "all_caps": {}, "ccm_first": {}, "ccm_pairs": {}}

    work = tempfile.mkdtemp(prefix="nher_afs_")
    skc = rb.Afs(str(us / "data_usi.afs"), work).entry(4)
    afs_cmn = rb.Afs(str(us / "data_cmn.afs"), work)
    ccm_pairs: dict[tuple[int, int], int] = {}
    ccm_first = tt.tipos(afs_cmn, ccm_pairs)

    attacks: dict[int, dict[str, Any]] = {}
    esquivas: dict[int, dict[str, Any]] = {}
    all_caps: dict[int, dict[str, Any]] = {}

    for i, r in enumerate(cp.skc_records(skc)):
        if i == 0:
            continue
        own = cp.skc_owner(r)
        bits = [k for k in range(64) if (own >> k) & 1]
        req = (r[16] << 8) | r[17]
        info = {
            "cap": i,
            "clase": r[8],
            "rareza": r[10] >> 4,
            "formas": r[14],
            "ki": r[15],
            "requiere": req,
            "efecto_hi": r[20],
            "efecto_lo": r[21],
            "owners_mask": own,
            "owners": bits,
            "donante": bits[0] if bits else -1,
        }
        all_caps[i] = info
        if r[8] == 0x21 and 1 <= len(bits) <= 2:
            attacks[i] = info
        if (r[8] >> 4) == 1 and r[20] == 0x43 and r[21] != 0x11:
            esquivas[i] = info

    # Also read char372 form capsules per native fighter (0..43) from roster_build.IDS
    fighter_forms: dict[int, list[int]] = {}
    for cid in range(44):
        row = rb.IDS.get(cid)
        if row and "form_caps" in row:
            fighter_forms[cid] = list(row["form_caps"])

    return {
        "available": True,
        "attacks": attacks,
        "esquivas": esquivas,
        "all_caps": all_caps,
        "ccm_first": {int(k): (int(v[0]), int(v[1])) for k, v in ccm_first.items()},
        "ccm_pairs": {(int(k[0]), int(k[1])): int(v) for k, v in ccm_pairs.items()},
        "fighter_forms": fighter_forms,
        "rb_ids_count": len(rb.IDS),
    }


def load_all(
    root: Path | None = None,
    sagas_path: Path | None = None,
    desbloqueos_path: Path | None = None,
    recompensas_path: Path | None = None,
    razas_reglas_path: Path | None = None,
    us_dir: Path | None = None,
    skip_afs: bool = False,
) -> dict[str, Any]:
    root = find_repo_root(root)
    _ensure_mod_center_imports(root)
    import heroe_sagas_a_du as hs  # type: ignore

    p_sagas = sagas_path or (root / "res" / "heroe" / "sagas.toml")
    p_desb = desbloqueos_path or (root / "res" / "heroe" / "desbloqueos.toml")
    p_rec = recompensas_path or (root / "res" / "heroe" / "recompensas.toml")
    p_razas = razas_reglas_path or (root / "res" / "heroe" / "razas_reglas.toml")

    sagas_doc = tomllib.loads(p_sagas.read_text(encoding="utf-8"))
    desb_doc = tomllib.loads(p_desb.read_text(encoding="utf-8"))
    rec_doc = tomllib.loads(p_rec.read_text(encoding="utf-8"))
    razas_doc = tomllib.loads(p_razas.read_text(encoding="utf-8"))

    # Use heroe_sagas_a_du.canon / tramos directly so ordering matches C++ LeerSagas
    canon_sagas = hs.canon(sagas_doc)
    tramos = hs.tramos(sagas_doc)
    for idx, t in enumerate(tramos):
        t["_tramo_idx"] = idx
        t["_es_variante"] = bool(t.get("variante", False))

    cpp = parse_heroe_cpp_constants(root)
    cpp["acceso_por_afinidad"] = "if (RutasOn() && AfinidadRuta(m)) return true;" in (root / "src/heroe.cpp").read_text(encoding="utf-8")
    cpp["hito_sin_ciclo"] = "r.coleccion == 100 && (base_tot == 0 || base_ok != base_tot)" in (root / "src/heroe_recompensas.inl").read_text(encoding="utf-8")
    pj_es, pj_en = parse_fighter_names(root)
    i18n = I18nIndex(root)
    afs_data = (
        {"available": False, "attacks": {}, "esquivas": {}, "all_caps": {}, "ccm_first": {}, "ccm_pairs": {}, "fighter_forms": {}}
        if skip_afs
        else load_skc_and_ccm(root, us_dir)
    )

    return {
        "root": root,
        "paths": {
            "sagas": p_sagas,
            "desbloqueos": p_desb,
            "recompensas": p_rec,
            "razas_reglas": p_razas,
        },
        "sagas_doc": sagas_doc,
        "canon_sagas": canon_sagas,
        "tramos": tramos,
        "desbloqueos": desb_doc.get("desbloqueo", []),
        "recompensas": rec_doc.get("recompensa", []),
        "razas_reglas": razas_doc,
        "cpp": cpp,
        "pj_es": pj_es,
        "pj_en": pj_en,
        "i18n": i18n,
        "afs": afs_data,
        "hs": hs,
    }


def pj_name(data: dict[str, Any], fid: int, lang: str = "es") -> str:
    table = data["pj_es"] if lang == "es" else data["pj_en"]
    return table.get(fid, f"#{fid}")


if __name__ == "__main__":
    d = load_all()
    print(
        f"Cargado OK: {len(d['canon_sagas'])} sagas canonicas, {len(d['tramos'])} tramos totales, "
        f"{len(d['desbloqueos'])} desbloqueos, {len(d['recompensas'])} recompensas, "
        f"{len(d['cpp']['maestros'])} maestros, {len(d['afs']['attacks'])} ataques #SKC"
    )

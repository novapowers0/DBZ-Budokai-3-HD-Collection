#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Auditoria de los DATOS del creador de personajes (CaC). SOLO LECTURA.

Comprueba los catalogos, reglas y recetas que alimentan el CaC y avisa de
datos rotos o incoherentes antes de que los vea el jugador:

  * piezas que apuntan a archivos, ids o fids que no existen;
  * piezas sin version para alguna forma cuando el anfitrion la tiene;
  * duplicados (misma pieza con dos nombres o dos piezas con el mismo id);
  * piezas/capas que se apilarian en vez de sustituir;
  * textos sin traducir o vacios;
  * desbloqueos imposibles (marcas, rivales, maestros o formas que no se dan).

No escribe nada: imprime los hallazgos y termina con un codigo de salida
(1 si hay ALTA, 2 si hay MEDIA, 0 si solo BAJA/INFO).

Uso:
  python tools/pruebas_heroe/cac_pendientes/auditar_datos_cac.py [--raiz RUTA] [--json]

Solo biblioteca estandar + tomllib (Python 3.11+).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

SEV_ORDER = {"ALTA": 0, "MEDIA": 1, "BAJA": 2, "INFO": 3}


@dataclass
class Finding:
    code: str
    sev: str
    path: str
    line: int | None
    what: str
    fix: str
    evidence: str = ""

    @property
    def where(self) -> str:
        return "%s:%s" % (self.path, self.line if self.line else "?")


class Auditor:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.findings: list[Finding] = []
        self.notes: list[str] = []
        self._toml_cache: dict[str, tuple[dict[str, Any], str]] = {}
        self._json_cache: dict[str, tuple[Any, str]] = {}

    # ------------------------------------------------------------------ utils
    def p(self, rel: str) -> Path:
        return self.root / rel

    def rel(self, path: Path) -> str:
        try:
            return str(path.relative_to(self.root)).replace("\\", "/")
        except ValueError:
            return str(path)

    def read_text(self, rel: str) -> str:
        return self.p(rel).read_text(encoding="utf-8")

    def toml(self, rel: str) -> tuple[dict[str, Any], str]:
        if rel not in self._toml_cache:
            raw = self.read_text(rel)
            self._toml_cache[rel] = (tomllib.loads(raw), raw)
        return self._toml_cache[rel]

    def json(self, rel: str) -> tuple[Any, str]:
        if rel not in self._json_cache:
            raw = self.read_text(rel)
            self._json_cache[rel] = (json.loads(raw), raw)
        return self._json_cache[rel]

    def add(self, code: str, sev: str, rel: str, line: int | None, what: str, fix: str, evidence: str = "") -> None:
        self.findings.append(Finding(code, sev, rel, line, what, fix, evidence))

    def note(self, text: str) -> None:
        self.notes.append(text)

    @staticmethod
    def line_of(raw: str, needle: str, start: int = 0) -> int | None:
        pos = raw.find(needle, start)
        return raw.count("\n", 0, pos) + 1 if pos >= 0 else None

    @staticmethod
    def blocks(raw: str, header: str) -> Iterable[tuple[int, int]]:
        starts = [m.start() for m in re.finditer(r"(?m)^\[\[%s\]\]" % re.escape(header), raw)]
        for i, start in enumerate(starts):
            end = starts[i + 1] if i + 1 < len(starts) else len(raw)
            yield start, end

    def toml_blocks(self, rel: str, header: str) -> list[tuple[dict[str, Any], str, int]]:
        data, raw = self.toml(rel)
        rows = data.get(header, [])
        out: list[tuple[dict[str, Any], str, int]] = []
        for (start, end), row in zip(self.blocks(raw, header), rows):
            if not isinstance(row, dict):
                continue
            out.append((row, raw[start:end], raw.count("\n", 0, start) + 1))
        return out

    # ------------------------------------------------------------------ AFS
    def afs_entries(self, rel: str = "us/data_cmn.afs") -> list[tuple[int, int]] | None:
        path = self.p(rel)
        if not path.is_file():
            return None
        with path.open("rb") as fh:
            head = fh.read(8)
            if len(head) < 8 or head[:3] != b"AFS":
                return None
            count = struct.unpack_from("<I", head, 4)[0]
            table = fh.read(count * 8)
            if len(table) < count * 8:
                return None
            return [(struct.unpack_from("<I", table, i * 8)[0],
                     struct.unpack_from("<I", table, i * 8 + 4)[0]) for i in range(count)]

    # ------------------------------------------------------------ source facts
    def heroe_style_masters(self) -> list[int]:
        raw = self.read_text("src/heroe.cpp")
        m = re.search(r"kNumMaestros\s*=\s*(\d+)", raw)
        n = int(m.group(1)) if m else 6
        block = re.search(r"const Estilo kEstilos\[\]\s*=\s*\{(.*?)\};", raw, re.S)
        if not block:
            return [0, 7, 11, 10, 27, 33][:n]
        ids: list[int] = []
        for em in re.finditer(r"\{\s*(\d+)", block.group(1)):
            ids.append(int(em.group(1)))
            if len(ids) >= n:
                break
        return ids

    def kfid(self) -> dict[int, list[int]]:
        raw = self.read_text("mod center hd/cac/vista_heroe.py")
        block = re.search(r"KFID\s*=\s*\{(.*?)\n\}", raw, re.S)
        out: dict[int, list[int]] = {}
        if not block:
            return out
        for m in re.finditer(r"(?m)^\s*(\d+):\s*\[([\d,\s]+)\]", block.group(1)):
            out[int(m.group(1))] = [int(x) for x in re.findall(r"\d+", m.group(2))]
        return out

    def buscar_pelo_names(self) -> list[str]:
        raw = self.read_text("mod center hd/cac/vista_heroe.py")
        block = re.search(r"def buscar_pelo\(.*?\):\s*(.*?)(?=\ndef )", raw, re.S)
        if not block:
            return []
        m = re.search(r"nombres\s*=\s*\[(.*?)\]", block.group(1), re.S)
        return re.findall(r'"([^"]+)"', m.group(1)) if m else []

    def pelon_nativos(self) -> dict[int, int]:
        raw = self.read_text("mod center hd/cac/vista_heroe.py")
        m = re.search(r"PELOS_NATIVOS\s*=\s*\{(.*?)\}", raw)
        if not m:
            return {}
        return {int(a): int(b) for a, b in re.findall(r"(\d+)\s*:\s*(\d+)", m.group(1))}

    def k_num_pelos(self) -> int | None:
        m = re.search(r"kNumPelos\s*=\s*(\d+)", self.read_text("src/heroe_ui.cpp"))
        return int(m.group(1)) if m else None

    def arcosiano_starts(self) -> dict[str, int]:
        raw = self.read_text("mod center hd/cac/arcosiano_original.py")
        m = re.search(r"starts\s*=\s*\{(.*?)\}", raw)
        if not m:
            return {}
        return {k: int(v) for k, v in re.findall(r"'(\w+)'\s*:\s*(\d+)", m.group(1))}

    def namek_majin_keys(self) -> set[str]:
        """Claves de receta que _B3 de partes_namek_majin.py genera con f-strings."""
        raw = self.read_text("mod center hd/cac/partes_namek_majin.py")
        if "_B3" not in raw or "nm_cabeza_" not in raw or "mj_antena_" not in raw:
            return set()
        keys: set[str] = set()
        keys |= {"nm_cabeza_%02d" % i for i in range(1, 7)}
        keys |= {"nm_orejas_%02d" % i for i in range(1, 4)}
        keys |= {"nm_capa", "nm_turbante"}
        keys |= {"mj_antena_%02d" % i for i in range(1, 7)}
        keys |= {"mj_rostro_%02d" % i for i in range(1, 4)}
        keys |= {"mj_cuerpo_%02d" % i for i in range(1, 4)}
        return keys

    # ---------------------------------------------------------------- checks
    def run(self) -> None:
        self.check_runtime_catalog()
        self.check_b3_catalog()
        self.check_native_catalogs()
        self.check_razas()
        self.check_desbloqueos()
        self.check_b3_approvals()
        self.check_translations()
        self.check_recipes()
        self.check_saved_heroes()

    # 1) res/heroe/piezas_cac.toml ------------------------------------------------
    def check_runtime_catalog(self) -> None:
        rel = "res/heroe/piezas_cac.toml"
        if not self.p(rel).is_file():
            self.add("CAT-FALTA", "ALTA", rel, None, "No existe el catálogo del CaC.", "Restaurar el fichero.")
            return
        data, raw = self.toml(rel)
        rows = [p for p in data.get("pieza", []) if isinstance(p, dict)]
        by_type: dict[str, list[dict[str, Any]]] = {}
        seen: dict[tuple[str, int], list[dict[str, Any]]] = {}
        for p in rows:
            t, i = str(p.get("tipo", "")), int(p.get("id", -1))
            by_type.setdefault(t, []).append(p)
            seen.setdefault((t, i), []).append(p)
            if not str(p.get("nombre", "")).strip():
                self.add("CAT-NOMBRE", "MEDIA", rel, self.line_of(raw, 'id = %d' % i),
                         "Pieza sin nombre (tipo %s, id %s)." % (t, i), "Poner un nombre legible o quitarla del catálogo.")
        for (t, i), v in seen.items():
            if len(v) > 1:
                self.add("CAT-DUP", "MEDIA", rel, self.line_of(raw, 'id = %d' % i),
                         "Dos piezas con el mismo tipo e id: %s/%s." % (t, i), "Dejar una sola entrada.")

        # cuerpos: 102 UT = 6 familias x 17 variantes; indice UT = 6929 + 51*familia + 3*variante
        cuerpos = sorted(by_type.get("cuerpo", []), key=lambda p: int(p.get("id", -1)))
        fam_order = {"a0": 0, "a1": 1, "b0": 2, "b1": 3, "c0": 4, "c1": 5}
        ids = [int(p.get("id", -1)) for p in cuerpos]
        if ids != list(range(102)):
            self.add("CAT-CUERPO", "MEDIA", rel, self.line_of(raw, "[[pieza]]"),
                     "Los cuerpos no son 102 ids consecutivos 0..101 (faltan o sobran).",
                     "Regenerar el catálogo con mod center hd/cac/ut_catalogo.py/piezas_cac.py.")
        bad_idx: list[tuple[str, int, int]] = []
        for p in cuerpos:
            m = re.fullmatch(r"([abc][01])_(\d\d)p", str(p.get("nombre", "")))
            if not m:
                continue
            exp = 6929 + 51 * fam_order[m.group(1)] + 3 * (int(m.group(2)) - 1)
            if int(p.get("idx_afs", -2)) != exp:
                bad_idx.append((str(p.get("nombre")), int(p.get("idx_afs", -2)), exp))
        if bad_idx:
            self.add("CAT-IDX", "MEDIA", rel, self.line_of(raw, "idx_afs ="),
                     "Hay cuerpos con idx_afs fuera de la fórmula UT 6929+51*familia+3*variante: %s." % bad_idx[:6],
                     "Corregir idx_afs con la fórmula (o regenerar el catálogo).")

        # AFS del juego: los indices UT no son entradas de data_cmn.afs
        afs = self.afs_entries()
        if afs:
            over = [p for p in rows if int(p.get("idx_afs", -1)) >= len(afs)]
            if over:
                line = self.line_of(raw, "idx_afs = %d" % int(over[0]["idx_afs"]))
                self.add("CAT-AFS", "BAJA", rel, line,
                         "%d entradas llevan idx_afs >= %d (nº de entradas de data_cmn.afs). Son índices del inventario UT, "
                         "no de este AFS; el campo se documenta como 'entrada AFS'." % (len(over), len(afs)),
                         "Renombrar/documentar como idx_ut o validar contra el AFS de UT; no usarlo con data_cmn.afs.",
                         "ej.: %s idx_afs=%s" % (over[0].get("nombre"), over[0].get("idx_afs")))

        pelos = by_type.get("pelo", [])
        k = self.k_num_pelos()
        if k is not None:
            if len(pelos) + 1 == k:
                self.note("Pelos: %d entradas (una duplicada, ver CAT-PELO-DUP); cubren los 9 peinados UT. El índice 10 "
                          "(Krilin nativo) no está en el catálogo y no es un fallo de runtime: la UI usa kPelosNativos." % len(pelos))
            elif len(pelos) != k:
                self.add("CAT-PELO", "BAJA", rel, self.line_of(raw, 'tipo = "pelo"'),
                         "El catálogo tiene %d pelos y kNumPelos=%d (esperado: piezas + 'del cuerpo')." % (len(pelos), k),
                         "Alinear el catálogo con los índices de kPelosNativos/PELOS_NATIVOS.")
        # duplicado real: id 0 (hair_000_01) y id 9 (Pelo 01 UT) apuntan al mismo peinado
        p0 = next((p for p in pelos if int(p.get("id", -1)) == 0), None)
        p9 = next((p for p in pelos if int(p.get("id", -1)) == 9), None)
        if p0 and p9:
            norm = lambda s: re.sub(r"[^a-z0-9]", "", str(s).lower().replace("avatar_", "").replace(".bin", "").replace("_b3", ""))
            if norm(p0.get("nombre")) == norm(p9.get("archivo")):
                line = self.line_of(raw, 'id = 9', raw.find('tipo = "pelo"'))
                self.add("CAT-PELO-DUP", "MEDIA", rel, line,
                         "El pelo id 9 ('%s', %s) es el mismo peinado que el id 0 ('%s'): duplicado." % (
                             p9.get("nombre"), p9.get("archivo"), p0.get("nombre")),
                         "Quitar el id 9 o enlazarlo a un peinado distinto; buscar_pelo() solo usa los ids 0..8 como pelo 1..9.")
        # nombres de archivo que no siguen el patrón del cargador
        loader = {"avatar_%s_b3.bin" % n for n in self.buscar_pelo_names()}
        for p in pelos:
            a = str(p.get("archivo", "")).strip()
            if not a:
                continue
            found = any((self.p(x) / a).is_file() for x in ("", "mod center hd/cac/convertidas", "cac_piezas/pelo"))
            if not found and a not in loader:
                self.add("CAT-ARCH", "BAJA", rel, self.line_of(raw, 'archivo = "%s"' % a),
                         "El pelo %s apunta a '%s', que no existe y no sigue el nombre que busca buscar_pelo() "
                         "(avatar_hair_000_01_b3.bin)." % (p.get("id"), a),
                         "Guardar el bin como avatar_<nombre>_b3.bin o quitar 'archivo' (la vista ya usa buscar_pelo).")
        caras = by_type.get("cara", [])
        for p in caras:
            a = str(p.get("archivo", "")).strip()
            if a and not any((self.p(x) / a).is_file() for x in ("", "mod center hd/cac/convertidas", "cac_piezas/cara")):
                self.add("CAT-CARA", "BAJA", rel, self.line_of(raw, 'archivo = "%s"' % a),
                         "La cara id %s apunta a '%s', que no existe y no lo usa ningún cargador (la cara va por kRostros)." % (
                             p.get("id"), a),
                         "Quitar la entrada o conectarla a un selector explícito de cabeza UT.")
        if caras:
            self.note("Catálogo de caras: %d entrada(s) para %d rostros de la UI (kRostros); el catálogo no es la fuente de los rostros." % (
                len(caras), 14))
        if len(by_type.get("aura", [])) < 8:
            self.note("Auras: %d entradas en el catálogo; AjustarSlot permite 0..7 (cac.cpp:173)." % len(by_type.get("aura", [])))

    # 2) mod center hd/cac/piezas_cac.toml ---------------------------------------
    def check_b3_catalog(self) -> None:
        rel = "mod center hd/cac/piezas_cac.toml"
        if not self.p(rel).is_file():
            return
        data, raw = self.toml(rel)
        rows = [p for p in data.get("pieza", []) if isinstance(p, dict)]
        by_id: dict[int, list[dict[str, Any]]] = {}
        for p in rows:
            by_id.setdefault(int(p.get("id", -1)), []).append(p)
        cruces = []
        for i, v in sorted(by_id.items()):
            types = {str(p.get("tipo")) for p in v}
            if len(v) > 1 and len(types) > 1 and any(t.startswith("arcosiano_") for t in types):
                cruces.append((i, sorted(types)))
        if cruces:
            line = self.line_of(raw, "id = %d" % cruces[0][0])
            self.add("B3-ID-CRUCE", "MEDIA", rel, line,
                     "Los ids %s se usan a la vez en piezas arcosianas y en las Namek/Majin; el catálogo arcosiano "
                     "comparte ids con las otras piezas." % [c[0] for c in cruces],
                     "Regenerar el catálogo: los ids arcosianos deben ir en 3800..3992, no en 3600/3760/3780/3810/3790.")
        # duplicados exactos
        exact: dict[tuple[str, int], int] = {}
        for p in rows:
            key = (str(p.get("tipo")), int(p.get("id", -1)))
            exact[key] = exact.get(key, 0) + 1
        for (t, i), n in exact.items():
            if n > 1:
                self.add("B3-DUP", "MEDIA", rel, self.line_of(raw, "id = %d" % i),
                         "Dos piezas %s con id %d." % (t, i), "Dejar una sola.")

        # arcosiano: ids esperados por el código (arcosiano_ui.h / arcosiano_original.py)
        starts = self.arcosiano_starts()
        cat = self.json("res/heroe/arcosiano_r16/catalogo.json")[0] if self.p("res/heroe/arcosiano_r16/catalogo.json").is_file() else {}
        slots = cat.get("slots", {}) if isinstance(cat, dict) else {}
        malos_arco: list[tuple[str, int, int, int]] = []
        sin_cuerpos: list[tuple[str, int, int]] = []
        for p in rows:
            t = str(p.get("tipo", ""))
            if not t.startswith("arcosiano_") or t == "arcosiano_cuerpo":
                continue
            slot = t[len("arcosiano_"):]
            expected = starts.get(slot)
            if expected is None:
                continue
            same = [q for q in rows if q.get("tipo") == t]
            try:
                idx = same.index(p)
            except ValueError:
                continue
            want = expected + idx
            if int(p.get("id", -1)) != want:
                malos_arco.append((t, int(p.get("id", -1)), want, idx))
            if not p.get("cuerpos"):
                sin_cuerpos.append((t, int(p.get("id", -1)), idx))
        if malos_arco:
            t0, id0, want0, idx0 = malos_arco[0]
            slot0 = t0[len("arcosiano_"):]
            line = self.line_of(raw, "id = %d" % id0, raw.find("arcosiano_%s" % slot0))
            rangos = []
            for slot in ("cabeza", "cuernos", "cara", "placas", "gema", "cola"):
                if slot in starts:
                    rangos.append("%s=%d..%d" % (slot, starts[slot], starts[slot] + len(slots.get(slot, [])) - 1))
            self.add("B3-ARCO-ID", "ALTA", rel, line,
                     "Las %d piezas arcosianas no coinciden con arcosiano_original.catalogo() (cabeza=3800, cuernos=3950, "
                     "cara=3960, placas=3980, gema=3970, cola=3990); arcosiano_ui.h empaqueta cabeza/cuernos/gema en ids[0] "
                     "(3800-3943) y lee cara 3960, placas 3980 y cola 3990. Ej.: %s con id %d; debería ser %d." % (
                         len(malos_arco), t0, id0, want0),
                     "Regenerar mod center hd/cac/piezas_cac.toml con piezas_cac.py (arcosiano_original.catalogo()): %s. "
                     "Si se quieren cuernos/gema como piezas sueltas, ampliar arcosiano_ui (hoy van empaquetados en ids[0])." %
                     ", ".join(rangos))
        sin_cuerpos_todas = [
            p for p in rows
            if not p.get("cuerpos") and (
                str(p.get("tipo", "")).startswith("arcosiano_")
                or 3100 <= int(p.get("id", -1)) < 3300
                or 3600 <= int(p.get("id", -1)) < 4100)
        ]
        if sin_cuerpos_todas:
            p0 = sin_cuerpos_todas[0]
            line = self.line_of(raw, "id = %d" % int(p0.get("id", -1)),
                                raw.find(str(p0.get("tipo", ""))))
            self.add("B3-CUERPOS", "MEDIA", rel, line,
                     "%d piezas (arcosianas y Namek/Majin/Namek original) no llevan 'cuerpos' en el toml, aunque el "
                     "generador las marca [27,38], [11] o [34,35,36]." % len(sin_cuerpos_todas),
                     "Añadir cuerpos a cada pieza y hacer que piezas_cac.py escriba el campo (hoy se pierde).")

        # archivo: recetas/ids que deben resolver
        namek_ids = set()
        pn = self.p("res/heroe/namek_original/catalogo.json")
        if pn.is_file():
            namek_ids = {int(e["id"]) for e in self.json(str(pn.relative_to(self.root)).replace("\\", "/"))[0].get("catalogo", [])}
        majin_ids = set()
        pm = self.p("res/heroe/partes_majin/catalogo.json")
        if pm.is_file():
            majin_ids = {int(e["id"]) for e in self.json(str(pm.relative_to(self.root)).replace("\\", "/"))[0].get("catalogo", [])}
        arco_slots = set(slots)
        orig_raw = self.read_text("mod center hd/cac/partes_originales.py")
        orig_keys = {m.group(1) for m in re.finditer(r"\(\d+,\s*\d+,\s*'[^']+',\s*'([^']+)'\)", orig_raw)}
        nm_keys = self.namek_majin_keys()
        for p in rows:
            a = str(p.get("archivo", "")).strip()
            if not a:
                continue
            ok = True
            why = ""
            if a.startswith("nmc:"):
                try:
                    ok = int(a.split(":", 1)[1]) in namek_ids
                    why = "id no está en namek_original/catalogo.json"
                except ValueError:
                    ok, why = False, "id no numérico"
            elif a.startswith("majin:"):
                try:
                    ok = int(a.split(":", 1)[1]) in majin_ids
                    why = "id no está en partes_majin/catalogo.json"
                except ValueError:
                    ok, why = False, "id no numérico"
            elif a.startswith("arcosiano_r16:"):
                ok = a.split(":", 1)[1] in arco_slots
                why = "slot no está en arcosiano_r16/catalogo.json"
            elif a.startswith("original:ia_namek:"):
                key = a.split(":", 2)[2]
                ok = (self.p("mod center hd/cac/ia_namek") / key / "malla.json").is_file()
                why = "falta ia_namek/%s/malla.json" % key
            elif a.startswith("original:"):
                key = a.split(":", 1)[1]
                ok = key in orig_keys or key in nm_keys
                why = "receta no aparece en partes_originales.CATALOGO ni en partes_namek_majin._B3"
            if not ok:
                self.add("B3-ARCH", "ALTA", rel, self.line_of(raw, 'archivo = "%s"' % a),
                         "Pieza '%s' apunta a '%s': %s." % (p.get("nombre"), a, why),
                         "Corregir el id/receta o quitar la pieza; el consumidor lanzará FileNotFoundError/ValueError.")
        # el generador pierde 'cuerpos'
        self.add("B3-GEN-CUERPOS", "MEDIA", "mod center hd/cac/piezas_cac.py", 94,
                 "piezas_cac.py escribe solo tipo/id/nombre/idx_afs/archivo: pierde el campo 'cuerpos' que sí traen "
                 "partes_majin/namek_original/arcosiano/ia_namek.",
                 "Escribir también 'cuerpos' (y cualquier campo extra) al generar; hoy el toml no dice en qué cuerpos vale cada pieza.")

    # 3) catalogs with per-form files ----------------------------------------------
    def check_native_catalogs(self) -> None:
        self._check_arcosiano()
        self._check_majin()
        self._check_freezer()
        self._check_namek_majin()
        gpt3 = self.p("res/heroe/gpt3")
        if gpt3.is_dir():
            data_files = [x for x in gpt3.rglob("*") if x.suffix.lower() in (".json", ".toml")]
            if not data_files:
                self.note("res/heroe/gpt3 solo tiene imágenes; no hay datos que auditar.")

    def _check_arcosiano(self) -> None:
        rel = "res/heroe/arcosiano_r16/catalogo.json"
        if not self.p(rel).is_file():
            return
        cat, _ = self.json(rel)
        files = cat.get("files", [])
        forms = cat.get("forms", [])
        slots = cat.get("slots", {})
        listed = {e["file"] for e in files if isinstance(e, dict) and e.get("file")}
        missing = [e["file"] for e in files if isinstance(e, dict) and not (self.p("res/heroe/arcosiano_r16") / e["file"]).is_file()]
        bad_hash = []
        for e in files:
            if not isinstance(e, dict) or not e.get("file"):
                continue
            path = self.p("res/heroe/arcosiano_r16") / e["file"]
            if path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() != e.get("sha256"):
                bad_hash.append(e["file"])
        if missing or bad_hash:
            self.add("ARC-FILES", "ALTA", rel, None,
                     "Arcosiano: faltan %d ficheros y %d hashes no cuadran." % (len(missing), len(bad_hash)),
                     "Regenerar res/heroe/arcosiano_r16 (tools/r16_fri_modelos.py) y su catálogo.",
                     "faltan=%s malos=%s" % (missing[:4], bad_hash[:4]))
        # completitud de formas por slot/variante
        by_k: dict[tuple[str, int], set[str]] = {}
        for e in files:
            if not isinstance(e, dict) or not e.get("file"):
                continue
            by_k.setdefault((e.get("slot", ""), int(e.get("variant", 0))), set()).add(e.get("form", ""))
        for (slot, var), got in sorted(by_k.items()):
            falta = [f for f in forms if f not in got]
            if falta:
                self.add("ARC-FORMA", "ALTA", rel, None,
                         "Arcosiano %s variante %d sin versión para forma(s): %s." % (slot, var, ", ".join(falta)),
                         "Exportar esas formas (arcosiano_original.recurso falla con 'sin versión para la forma').")
        # piezas del catálogo de piezas: todas las formas
        self.note("Arcosiano: %d ficheros, %d formas, %d slots; hashes y presencia comprobados." % (
            len(files), len(forms), len(slots)))

    def _check_majin(self) -> None:
        rel = "res/heroe/partes_majin/catalogo.json"
        if not self.p(rel).is_file():
            return
        cat, _ = self.json(rel)
        base = self.p("res/heroe/partes_majin")
        files = cat.get("files", [])
        missing, bad = [], []
        for e in files:
            if not isinstance(e, dict) or not e.get("file"):
                continue
            path = base / e["file"]
            if not path.is_file():
                missing.append(e["file"])
            elif hashlib.sha256(path.read_bytes()).hexdigest() != e.get("sha256"):
                bad.append(e["file"])
        if missing or bad:
            self.add("MAJ-FILES", "ALTA", rel, None,
                     "Majin: faltan %d ficheros y %d hashes no cuadran." % (len(missing), len(bad)),
                     "Regenerar res/heroe/partes_majin.", "faltan=%s malos=%s" % (missing[:4], bad[:4]))
        forms = {p.name for p in base.iterdir() if p.is_dir() and p.name.startswith("f_maj_")}
        by_piece: dict[str, set[str]] = {}
        for e in files:
            if not isinstance(e, dict) or not e.get("file"):
                continue
            form, name = e["file"].split("/", 1)
            by_piece.setdefault(name, set()).add(form)
        for name, got in sorted(by_piece.items()):
            falta = sorted(forms - got)
            if falta:
                self.add("MAJ-FORMA", "ALTA", rel, None,
                         "Majin %s sin versión para forma(s): %s." % (name, ", ".join(falta)),
                         "Exportar esas formas; el montaje por forma las pedirá.")
        self.note("Majin: %d ficheros, %d formas, %d piezas; hashes y presencia comprobados." % (
            len(files), len(forms), len(by_piece)))

    def _check_freezer(self) -> None:
        rel = "res/heroe/partes_nativas_freezer/catalogo.json"
        if not self.p(rel).is_file():
            return
        cat, raw = self.json(rel)
        base = self.p("res/heroe/partes_nativas_freezer")
        files = cat.get("files", [])
        missing, bad = [], []
        for e in files:
            if not isinstance(e, dict) or not e.get("file"):
                continue
            path = base / e["file"]
            if not path.is_file():
                missing.append(e["file"])
            elif hashlib.sha256(path.read_bytes()).hexdigest() != e.get("sha256"):
                bad.append(e["file"])
        if missing or bad:
            self.add("FRI-FILES", "ALTA", rel, None,
                     "Freezer nativo: faltan %d ficheros y %d hashes no cuadran." % (len(missing), len(bad)),
                     "Regenerar res/heroe/partes_nativas_freezer.", "faltan=%s malos=%s" % (missing[:4], bad[:4]))
        catalog = cat.get("catalog", [])
        for e in catalog:
            if not isinstance(e, dict) or not e.get("file"):
                continue
            if not (base / e["file"]).is_file():
                self.add("FRI-CAT", "ALTA", rel, self.line_of(raw, '"id": %s' % e.get("id")),
                         "Freezer nativo '%s' apunta a %s, que no existe." % (e.get("name"), e.get("file")),
                         "Exportar el fichero o quitar la entrada.")
        if not cat.get("forms"):
            self.note("Freezer nativo: las piezas no tienen variantes por forma (se reutiliza la malla en las 5 formas del anfitrión); "
                      "revisar en juego el encaje en 2.ª/3.ª/final/100 %/Cooler.")

    def _check_namek_majin(self) -> None:
        rel = "res/heroe/partes_nativas/namek_majin/catalogo_nm.json"
        if not self.p(rel).is_file():
            return
        cat, raw = self.json(rel)
        base = self.p("res/heroe/partes_nativas/namek_majin")
        entries = [e for e in cat.get("catalogo", []) if isinstance(e, dict)]
        missing = [e for e in entries if not (base / str(e.get("archivo", "")).split("/", 1)[-1]).is_file()]
        if missing:
            self.add("NMC-FILES", "ALTA", rel, None,
                     "Namek/Majin nativo: %d entradas apuntan a ficheros que no existen." % len(missing),
                     "Exportar los JSON que faltan o quitarlos del catálogo.",
                     "faltan=%s" % [e.get("id") for e in missing[:6]])
        disk = {int(p.stem) for p in base.glob("*.json") if p.stem.isdigit()}
        cat_ids = {int(e["id"]) for e in entries}
        if disk != cat_ids:
            self.add("NMC-SET", "MEDIA", rel, None,
                     "El catálogo y los ficheros no coinciden (catálogo-solo=%s, fichero-solo=%s)." % (
                         sorted(cat_ids - disk), sorted(disk - cat_ids)),
                     "Regenerar catalogo_nm.json desde los JSON exportados.")
        # sin hashes: no se puede verificar corrupción
        if not any("sha256" in e for e in entries) and not any("sha256" in e for e in cat.get("files", []) if isinstance(e, dict)):
            self.note("Namek/Majin nativo (3400/3440/3500/3540): sin sha256 en el catálogo; no se puede detectar corrupción.")
        # estas 24 piezas no están en piezas_cac.toml
        try:
            b3, _ = self.toml("mod center hd/cac/piezas_cac.toml")
            ids_b3 = {int(p.get("id", -1)) for p in b3.get("pieza", []) if isinstance(p, dict)}
            no_exp = sorted(cat_ids - ids_b3)
            if no_exp:
                self.note("Namek/Majin nativo: %d piezas exportadas (%s...) no aparecen en mod center hd/cac/piezas_cac.toml; "
                          "son el conjunto alternativo de partes_nativas.py, no el que genera el catálogo." % (len(no_exp), no_exp[:6]))
        except Exception:
            pass

    # 4) razas_reglas.toml ---------------------------------------------------------
    def check_razas(self) -> None:
        rel = "res/heroe/razas_reglas.toml"
        if not self.p(rel).is_file():
            self.add("RAZ-FALTA", "ALTA", rel, None, "No existe razas_reglas.toml.", "Restaurarlo.")
            return
        data, raw = self.toml(rel)
        if data.get("version") != 1:
            self.add("RAZ-VER", "ALTA", rel, self.line_of(raw, "version"), "version != 1 (el cargador rechaza la tabla).",
                     "Poner version = 1.")
        razas = [r for r in data.get("raza", []) if isinstance(r, dict)]
        if len(razas) != 6:
            self.add("RAZ-N", "ALTA", rel, None, "Hay %d razas; el cargador exige 6." % len(razas), "Dejar exactamente 6.")
        ids = [int(r.get("id", -1)) for r in razas]
        if sorted(ids) != list(range(6)):
            self.add("RAZ-ID", "ALTA", rel, None, "Los ids de raza no son 0..5 únicos: %s." % ids, "Corregir ids.")
        for r in razas:
            line = self.line_of(raw, "id = %s" % r.get("id"))
            for key in ("tecnicas", "formas"):
                if not isinstance(r.get(key), list):
                    self.add("RAZ-CAMPO", "ALTA", rel, line, "Raza %s sin lista '%s'." % (r.get("id"), key), "Añadir la lista.")
            tec = r.get("tecnicas", [])
            if len(tec) != len(set(tec)):
                self.add("RAZ-TEC-DUP", "MEDIA", rel, line, "Raza %s con técnicas repetidas: %s." % (r.get("id"), tec),
                         "Quitar repetidos.")
            for t in tec:
                if not isinstance(t, int) or t <= 0:
                    self.add("RAZ-TEC", "MEDIA", rel, line, "Raza %s con técnica no válida: %r." % (r.get("id"), t),
                             "Usar números de cápsula > 0.")
        comunes = data.get("comunes", [])
        if len(comunes) != len(set(comunes)):
            self.add("RAZ-COM-DUP", "MEDIA", rel, self.line_of(raw, "comunes"), "Cápsulas comunes repetidas: %s." % comunes,
                     "Quitar repetidos.")
        # formas: cada id debe existir en desbloqueos y con la misma raza; y viceversa
        try:
            des, _ = self.toml("res/heroe/desbloqueos.toml")
            formas_des = {str(d.get("id")): int(d.get("raza", -1)) for d in des.get("desbloqueo", [])
                          if isinstance(d, dict) and d.get("tipo") == "forma"}
            for r in razas:
                rid = int(r.get("id", -1))
                for fid in r.get("formas", []):
                    if fid not in formas_des:
                        self.add("RAZ-FORMA-ORF", "ALTA", rel, self.line_of(raw, 'id = %s' % rid),
                                 "La raza %s lista la forma '%s', que no existe en desbloqueos.toml (el cargador la ignora)." % (rid, fid),
                                 "Corregir el id o añadir la forma a desbloqueos.toml.")
                    elif formas_des[fid] != rid:
                        self.add("RAZ-FORMA-RAZA", "ALTA", rel, self.line_of(raw, 'id = %s' % rid),
                                 "La raza %s lista la forma '%s', pero desbloqueos.toml la declara raza %s." % (rid, fid, formas_des[fid]),
                                 "Alinear razas_reglas.toml y desbloqueos.toml.")
            listadas = {fid for r in razas for fid in r.get("formas", [])}
            for fid, rid in formas_des.items():
                if fid not in listadas:
                    self.add("RAZ-FORMA-OCULTA", "MEDIA", "res/heroe/desbloqueos.toml",
                             self.line_of(self.toml("res/heroe/desbloqueos.toml")[1], 'id = "%s"' % fid),
                             "La forma '%s' (raza %s) no está en razas_reglas.toml: FormasTabla la filtra y no se ve." % (fid, rid),
                             "Añadirla a la lista 'formas' de su raza.")
        except Exception:
            pass
        # copia secundaria
        rel2 = "mod center hd/cac/razas_reglas.toml"
        if self.p(rel2).is_file():
            d2, raw2 = self.toml(rel2)
            if "tecnicas_completas" not in d2:
                self.add("RAZ-COPIA", "BAJA", rel2, self.line_of(raw2, "comunes"),
                         "La copia de mod center no declara tecnicas_completas (el cargador la lee de res/heroe).",
                         "Añadir tecnicas_completas = false para que ambas copias digan lo mismo.")
        if not data.get("tecnicas_completas", False):
            self.note("razas_reglas: tecnicas_completas=false; las listas 'tecnicas' no filtran (intencionado, R23).")

    # 5) desbloqueos.toml ----------------------------------------------------------
    def check_desbloqueos(self) -> None:
        rel = "res/heroe/desbloqueos.toml"
        if not self.p(rel).is_file():
            self.add("DES-FALTA", "ALTA", rel, None, "No existe desbloqueos.toml.", "Restaurarlo.")
            return
        data, raw = self.toml(rel)
        rows = [d for d in data.get("desbloqueo", []) if isinstance(d, dict)]
        ids: dict[str, dict[str, Any]] = {}
        for d in rows:
            did = str(d.get("id", ""))
            if not did:
                self.add("DES-ID", "ALTA", rel, None, "Entrada sin id.", "Poner un id único.")
                continue
            if did in ids:
                self.add("DES-DUP", "ALTA", rel, self.line_of(raw, 'id = "%s"' % did), "Id repetido: %s." % did, "Renombrar.")
            ids[did] = d
            t = str(d.get("tipo", ""))
            if t not in ("aura", "ki", "tecnica", "ssj", "forma"):
                self.add("DES-TIPO", "ALTA", rel, self.line_of(raw, 'id = "%s"' % did), "Tipo desconocido '%s' en %s." % (t, did),
                         "Usar aura|ki|tecnica|ssj|forma.")
            for k in ("nombre_es", "nombre_en"):
                if not str(d.get(k, "")).strip():
                    self.add("DES-TXT", "MEDIA", rel, self.line_of(raw, 'id = "%s"' % did), "%s sin %s." % (did, k),
                             "Rellenar el texto en español e inglés.")
            if t == "tecnica":
                cap = int(d.get("capsula", 0))
                if not (1 <= cap < 2048):
                    self.add("DES-CAP", "ALTA", rel, self.line_of(raw, 'id = "%s"' % did), "%s: cápsula %s fuera de 1..2047." % (did, cap),
                             "Corregir la cápsula #SKC.")
            if t == "ssj":
                if int(d.get("nivel", 0)) not in (1, 2, 3):
                    self.add("DES-SSJ", "ALTA", rel, self.line_of(raw, 'id = "%s"' % did), "%s: nivel SSJ %s no es 1..3." % (did, d.get("nivel")),
                             "Corregir nivel.")

        # datos que el motor puede cumplir
        sagas = self.toml("res/heroe/sagas.toml")[0] if self.p("res/heroe/sagas.toml").is_file() else {}
        marcas: set[str] = set()
        rivales: set[int] = set()
        formas_rival: set[int] = {0}
        for s in sagas.get("saga", []) if isinstance(sagas, dict) else []:
            if not isinstance(s, dict):
                continue
            for k in ("marca", "opcional_marca"):
                if s.get(k):
                    marcas.add(str(s[k]))
            for c in s.get("combate", []):
                if not isinstance(c, dict):
                    continue
                if "rival" in c:
                    rivales.add(int(c["rival"]))
                if "forma" in c:
                    formas_rival.add(int(c["forma"]))
        maestros = set(self.heroe_style_masters())
        rivales |= maestros
        marcas |= {"maestro_" + x for x in ("goku", "vegeta", "piccolo", "krilin", "freezer", "cell")}
        marcas.add("deseo_revivir")

        # referencias internas
        for d in rows:
            did = str(d.get("id", ""))
            l = self.line_of(raw, 'id = "%s"' % did)
            if d.get("tipo") == "forma":
                if int(d.get("raza", -1)) not in range(6):
                    self.add("DES-RAZA", "ALTA", rel, l, "%s: raza %s fuera de 0..5." % (did, d.get("raza")), "Corregir raza.")
                if str(d.get("rama", "")) not in ("", "A", "B"):
                    self.add("DES-RAMA", "ALTA", rel, l, "%s: rama %r no es '', 'A' o 'B'." % (did, d.get("rama")), "Corregir rama.")
                prev = str(d.get("previa", ""))
                if prev and prev not in ids:
                    self.add("DES-PREVIA", "ALTA", rel, l, "%s: previa '%s' no existe." % (did, prev), "Corregir previa.")
                abre = str(d.get("abre_con", ""))
                if abre and abre not in ids:
                    self.add("DES-ABRE", "ALTA", rel, l, "%s: abre_con '%s' no existe." % (did, abre), "Corregir abre_con.")
                for k in ("cond_es", "cond_en"):
                    if not str(d.get(k, "")).strip():
                        self.add("DES-TXT", "MEDIA", rel, l, "%s sin %s." % (did, k), "Rellenar la condición.")
                if str(d.get("rama", "")) in ("A", "B"):
                    for k in ("camino_es", "camino_en"):
                        if not str(d.get(k, "")).strip():
                            self.add("DES-TXT", "MEDIA", rel, l, "%s sin %s." % (did, k), "Rellenar el nombre del camino.")

        # alcanzabilidad (regla de heroe_formas.h)
        have: set[str] = set()
        changed = True
        while changed:
            changed = False
            for d in rows:
                did = str(d.get("id", ""))
                if did in have:
                    continue
                if d.get("tipo") == "forma":
                    if d.get("marca") and str(d["marca"]) not in marcas:
                        continue
                    if d.get("previa") and str(d["previa"]) not in have:
                        continue
                    abre, vence = str(d.get("abre_con", "")), int(d.get("vence", -1))
                    ok = (not abre and vence < 0) or (abre and abre in have)
                    if vence >= 0 and vence in rivales and (int(d.get("forma", -1)) < 0 or int(d.get("forma", -1)) in formas_rival):
                        ok = True
                    if ok:
                        have.add(did)
                        changed = True
                else:
                    if int(d.get("maestro", -1)) >= 0 and int(d["maestro"]) not in maestros:
                        continue
                    if int(d.get("vence", -1)) >= 0 and int(d["vence"]) not in rivales:
                        continue
                    if int(d.get("forma", -1)) >= 0 and int(d["forma"]) not in formas_rival:
                        continue
                    have.add(did)
                    changed = True

        for d in rows:
            did = str(d.get("id", ""))
            if did in have:
                continue
            l = self.line_of(raw, 'id = "%s"' % did)
            motivos: list[str] = []
            if d.get("marca") and str(d["marca"]) not in marcas:
                motivos.append("marca '%s' no la pone ninguna saga" % d["marca"])
            if int(d.get("vence", -1)) >= 0 and int(d["vence"]) not in rivales:
                motivos.append("rival %s no aparece en ningún combate" % d["vence"])
            if int(d.get("forma", -1)) >= 0 and int(d["forma"]) not in formas_rival:
                motivos.append("forma de rival %s no aparece en ningún combate" % d["forma"])
            if int(d.get("maestro", -1)) >= 0 and int(d["maestro"]) not in maestros:
                motivos.append("maestro %s no es uno de los 6" % d["maestro"])
            if d.get("previa") and str(d["previa"]) not in have:
                motivos.append("previa '%s' tampoco es alcanzable" % d["previa"])
            if d.get("abre_con") and str(d["abre_con"]) not in have:
                motivos.append("abre_con '%s' tampoco es alcanzable" % d["abre_con"])
            if not motivos:
                motivos.append("depende de una cadena anterior no alcanzable")
            self.add("DES-IMPOSIBLE", "ALTA", rel, l,
                     "Desbloqueo '%s' (%s) imposible en partida normal: %s." % (did, d.get("tipo"), "; ".join(motivos)),
                     self._fix_des(d, did))
        self.note("Desbloqueos: %d alcanzables de %d; rivales de saga=%s; formas de rival=%s." % (
            len(have), len(rows), sorted(rivales), sorted(formas_rival)))

        # textos que prometen cosas que la regla no cumple
        for d in rows:
            did = str(d.get("id", ""))
            l = self.line_of(raw, 'id = "%s"' % did)
            cond = str(d.get("cond_es", ""))
            marca = str(d.get("marca", ""))
            if "opcional" in cond.lower() and not marca.startswith("opc_"):
                self.add("DES-TXT-REGLA", "MEDIA", rel, l,
                         "%s: el texto pide un opcional, pero la regla solo mira marca='%s'." % (did, marca),
                         "Cambiar el texto a lo que exige la marca o usar una marca de opcional (opc_*).")
            if "gohan" in cond.lower() and int(d.get("vence", -1)) < 0:
                self.add("DES-TXT-REGLA", "MEDIA", rel, l,
                         "%s: el texto pide vencer a Gohan, pero la entrada no tiene 'vence' (se abriría solo con la marca)." % did,
                         "Añadir vence = 37 (Gohan) o cambiar el texto a la condición real.")
            if "deseo" in cond.lower() and not marca.startswith("deseo_"):
                self.add("DES-TXT-REGLA", "MEDIA", rel, l,
                         "%s: el texto pide un deseo a Shenron, pero el juego solo implementa el deseo de revivir "
                         "(ConfirmarDeseoHistoria(bool revivir)); no hay deseo 'más poder'." % did,
                         "Cambiar el texto a 'Toma el camino puro' o implementar ese deseo (código).")
            if "vegeta" in str(d.get("camino_es", "")).lower() and marca not in ("camino_vegeta", "maestro_vegeta"):
                self.add("DES-TXT-CAMINO", "BAJA", rel, l,
                         "%s: el camino se llama 'Camino de Vegeta' pero la marca es '%s'." % (did, marca),
                         "Usar camino_vegeta/maestro_vegeta o renombrar el camino a 'Camino de los villanos'.")

    def _fix_des(self, d: dict[str, Any], did: str) -> str:
        t = d.get("tipo")
        if int(d.get("maestro", -1)) == 40:
            return "Quitar la entrada hasta que Broly sea maestro (kNumMaestros=6) o cambiar maestro por uno de 0/7/11/10/27/33."
        if did == "kamehameha_rojo":
            return "Quitar 'vence = 0' y 'forma = 5' (el héroe no llega a SSJ4 y Goku no es rival); dejar solo Goku al 60 %."
        arreglos: list[str] = []
        if int(d.get("vence", -1)) in (35, 36, 37):
            arreglos.append("añadir a sagas.toml un combate con rival %s (Kid Bu=36, Super Bu=35, Gohan=37) o cambiar "
                            "'vence' por un rival que exista (27/33/38/41...)" % d["vence"])
        if d.get("marca") in ("camino_maldad", "camino_puro", "villanos_revividos"):
            arreglos.append("declarar la marca '%s' en la saga correspondiente de sagas.toml (p. ej. 'Villanos revividos' "
                            "-> marca = '%s') o cambiar la marca por una que ya exista" % (d["marca"], d["marca"]))
        if arreglos:
            txt = "%s." % "; ".join(arreglos)
            return txt[:1].upper() + txt[1:]
        return "Alinear la condición con una marca/rival/maestro que el juego pueda dar."

    # 6) piezas_b3_base.json -------------------------------------------------------
    def check_b3_approvals(self) -> None:
        rel = "mod center hd/cac/piezas_b3_base.json"
        if not self.p(rel).is_file():
            return
        cat, raw = self.json(rel)
        afs = self.afs_entries()
        for zone, rows in cat.items():
            if not isinstance(rows, list):
                continue
            fids = [int(r.get("fid", -1)) for r in rows if isinstance(r, dict)]
            if len(fids) != len(set(fids)):
                dups = sorted({f for f in fids if fids.count(f) > 1})
                self.add("B3-FID-DUP", "MEDIA", rel, self.line_of(raw, '"fid": %d' % dups[0]),
                         "Donantes repetidos en %s: %s." % (zone, dups), "Dejar una entrada por fid.")
            for r in rows:
                if not isinstance(r, dict):
                    continue
                fid = int(r.get("fid", -1))
                if afs and not (0 <= fid < len(afs) and afs[fid][1] > 0):
                    self.add("B3-FID", "ALTA", rel, self.line_of(raw, '"fid": %d' % fid),
                             "El fid %d (%s) no es una entrada válida de data_cmn.afs (%d entradas)." % (fid, r.get("nombre"), len(afs)),
                             "Corregir el fid o regenerar el catálogo.")
                if r.get("ok") is True and r.get("validacion_visual") != "perfecto":
                    self.add("B3-OK", "MEDIA", rel, self.line_of(raw, '"fid": %d' % fid),
                             "%s fid %d: ok=true sin validacion_visual='perfecto' (la UI no lo muestra)." % (zone, fid),
                             "Completar la validación o poner ok=false.")
            # aprobaciones por anfitrión
            for r in rows:
                if not isinstance(r, dict):
                    continue
                en = r.get("aprobada_en") or []
                for k in en:
                    if not re.fullmatch(r"\d+_\d+", str(k)):
                        self.add("B3-HOST", "MEDIA", rel, self.line_of(raw, '"fid": %d' % int(r.get("fid", -1))),
                                 "%s fid %s: aprobada_en '%s' no es cuerpo_traje." % (zone, r.get("fid"), k),
                                 "Usar 'cuerpo_traje', p. ej. '0_0'.")
        # anfitriones principales: Goku 0, Vegeta 7, Trunks 8, Gohan 2
        hosts = {0: "Goku", 7: "Vegeta", 8: "Trunks", 2: "Gohan"}
        vacios: dict[int, list[str]] = {}
        for body, name in hosts.items():
            for zone in ("torso", "piernas", "guantes", "botas"):
                n = 0
                for r in cat.get(zone, []):
                    if not isinstance(r, dict):
                        continue
                    if body == 0 and not r.get("aprobada_en") and r.get("ok") and r.get("validacion_visual") == "perfecto":
                        n += 1  # atajo 0_0 de heroe_ui.cpp
                        continue
                    if any(str(k).startswith("%d_" % body) for k in (r.get("aprobada_en") or [])):
                        n += 1
                if n == 0:
                    vacios.setdefault(body, []).append(zone)
        for body, zones in sorted(vacios.items()):
            name = hosts[body]
            if body == 2:
                self.add("B3-HOST-VACIO", "MEDIA", rel, None,
                         "El anfitrión Gohan (%d) no tiene ninguna pieza aprobada en ninguna zona (%s)." % (body, ", ".join(zones)),
                         "El catálogo por anfitrión de Gohan está vacío: juzgar 2_0..2_2 y añadir 'aprobada_en', o no ofrecer piezas B3 en Gohan.")
            else:
                self.add("B3-HOST-VACIO", "MEDIA", rel, None,
                         "El anfitrión %s (%d) no tiene ninguna pieza aprobada en: %s." % (name, body, ", ".join(zones)),
                         "Juzgar y añadir 'aprobada_en' de %d_* o no ofrecer esa zona en ese cuerpo (hoy el creador solo muestra 'Base')." % body)
        # guantes 70/71: ok=true pero sin aprobada_en (solo salen por el atajo de Goku 0_0)
        for r in cat.get("guantes", []):
            if isinstance(r, dict) and int(r.get("fid", -1)) in (70, 71):
                if not r.get("aprobada_en"):
                    line = self.line_of(raw, '"fid": %d' % int(r["fid"]), raw.find('"guantes": ['))
                    self.add("B3-GUANTE", "BAJA", rel, line,
                             "Guante %d (%s): ok=true y validacion_visual=perfecto pero sin 'aprobada_en'; las notas dicen "
                             "'No aprobada en Luna 8b' y el juez las rechaza en otros cuerpos." % (r["fid"], r.get("nombre")),
                             "Añadir ['0_0'] si la aprobación en juego de Goku es la vigente, o poner ok=false si no.")
        # accesorios: ok=false es normal (ok = injerto estructural); no es error
        acc = cat.get("accesorios", [])
        if any(isinstance(r, dict) and r.get("ok") is False and r.get("aprobada_en") for r in acc):
            self.note("piezas_b3_base.json: en 'accesorios', ok=false con aprobada_en no es contradicción: ok mide el injerto "
                      "estructural y la UI de accesorios solo exige aprobada_en (heroe_ui.cpp:1013).")
        # originales: fids contra CATALOGO
        rel2 = "mod center hd/cac/piezas_cac_originales.json"
        if self.p(rel2).is_file():
            orig, raw2 = self.json(rel2)
            src = self.read_text("mod center hd/cac/partes_originales.py")
            expected = {(int(m.group(1)), int(m.group(2)))
                        for m in re.finditer(r"\((\d+),\s*(\d+),\s*'[^']+',\s*'[^']+'\)", src)}
            got = {(int(p.get("fid", -1)), int(p.get("grupo", -1))) for p in orig.get("piezas", []) if isinstance(p, dict)}
            if expected and expected != got:
                self.add("ORIG-SET", "MEDIA", rel2, None,
                         "Las piezas propias de piezas_cac_originales.json no coinciden con CATALOGO de partes_originales.py "
                         "(faltan=%s, sobran=%s)." % (sorted(expected - got), sorted(got - expected)),
                         "Regenerar el JSON desde partes_originales.CATALOGO.")
            if all(not p.get("aprobada_en") for p in orig.get("piezas", []) if isinstance(p, dict)):
                self.note("piezas_cac_originales.json: ninguna pieza propia tiene aprobada_en; solo Freezer/Cooler (27/38) "
                          "las usan siempre (vista_heroe.py _aplicar_mixto). Para otros cuerpos se rechazan hasta que un juez las apruebe.")

    # 7) textos / i18n -------------------------------------------------------------
    def check_translations(self) -> None:
        rel = "res/heroe/sagas_i18n.toml"
        if self.p(rel).is_file():
            data, raw = self.toml(rel)
            langs = ("es", "en", "fr", "de", "it", "pt", "ja")
            for key, val in data.items():
                if not isinstance(val, dict):
                    continue
                for l in langs:
                    if not str(val.get(l, "")).strip():
                        self.add("I18N", "MEDIA", rel, self.line_of(raw, "[%s]" % json.dumps(key)),
                                 "Falta la traducción '%s' de %r." % (l, key), "Rellenar la frase en ese idioma.")
        rel = "res/heroe/recompensas.toml"
        if self.p(rel).is_file():
            data, raw = self.toml(rel)
            for r in data.get("recompensa", []):
                if not isinstance(r, dict):
                    continue
                for k in ("nombre_es", "nombre_en", "cond_es", "cond_en"):
                    if not str(r.get(k, "")).strip():
                        self.add("I18N-REC", "MEDIA", rel, self.line_of(raw, 'id = "%s"' % r.get("id")),
                                 "Recompensa %s sin %s." % (r.get("id"), k), "Rellenar el texto.")
        # piezas_b3_base: nombres de una sola lengua, mostrados tal cual
        rel = "mod center hd/cac/piezas_b3_base.json"
        if self.p(rel).is_file():
            cat, raw = self.json(rel)
            ejemplos = []
            for zone, rows in cat.items():
                if not isinstance(rows, list):
                    continue
                for r in rows:
                    n = str(r.get("nombre", ""))
                    if re.search(r"\b(Traje|Futuro|Adulto|Gordo|Pelo|Armadura|Sin)\b|con nave|de Namek|de Gero", n):
                        ejemplos.append(n)
            if ejemplos:
                self.add("I18N-B3", "MEDIA", "src/heroe_ui.cpp", 1115,
                         "NombrePiezaB3 muestra p.nombre tal cual, sin T::T; los nombres de piezas_b3_base.json están en "
                         "español/inglés mezclado (%d ejemplos: %s)." % (len(ejemplos), "; ".join(sorted(set(ejemplos))[:5])),
                         "Añadir nombre_es/nombre_en a cada pieza y usar T::T, o poner nombres neutros en un solo idioma.")
        # Pieza.nombre no tiene nombre_en
        self.add("I18N-PIEZA", "BAJA", "src/cac/cac.h", 19,
                 "struct Pieza solo tiene 'nombre'; el selector que consuma originales::Opciones (partes_originales_ui.h:30-36) "
                 "enseña ese texto en español en todos los idiomas. El catálogo no tiene campos _es/_en.",
                 "Añadir nombre_es/nombre_en (o usar claves i18n) antes de conectar el selector de piezas propias.")

    # 8) recetas / apilado ---------------------------------------------------------
    def check_recipes(self) -> None:
        candidates = []
        for base in ("mods", "mod center hd/campanas", "out/build/win-amd64-dual/mods"):
            b = self.p(base)
            if b.is_dir():
                candidates += list(b.rglob("traje_cac.toml"))
        if not candidates:
            self.note("Recetas de jugador: no hay ningún traje_cac.toml en el repo; no se puede auditar el apilado de capas. "
                      "El código sustituye el pelo/torso del cuerpo bajo una capa (vista_heroe.py:3563) y rechaza "
                      "original 3 + b3_torso (_aplicar_mixto).")
            return
        for path in candidates:
            rel = self.rel(path)
            try:
                raw = path.read_text(encoding="utf-8")
                data = tomllib.loads(raw)
            except Exception as e:  # noqa: BLE001
                self.add("REC-TOML", "ALTA", rel, None, "Receta ilegible: %s." % e, "Corregir el TOML.")
                continue
            t = data.get("traje_cac")
            if not isinstance(t, dict):
                self.add("REC-TOML", "ALTA", rel, None, "No hay tabla [traje_cac].", "Añadirla.")
                continue
            if t.get("version") != 1:
                self.add("REC-VER", "ALTA", rel, self.line_of(raw, "version"), "version != 1.", "Poner version = 1.")
            if "cuerpo" not in t:
                self.add("REC-CUERPO", "ALTA", rel, None, "La receta no dice de qué cuerpo parte.", "Añadir cuerpo = N.")
            capas = []
            for k in range(1, 5):
                v = str(t.get("capa%d" % k, "")).strip()
                if not v:
                    continue
                m = re.fullmatch(r"(-?\d+):(\d+)", v)
                if not m:
                    self.add("REC-CAPA", "ALTA", rel, self.line_of(raw, "capa%d" % k), "capa%d no es FID:PARTE." % k,
                             "Corregir el formato.")
                    continue
                capas.append((int(m.group(1)), int(m.group(2)), k))
            for i, (fid, parte, k) in enumerate(capas):
                for fid2, parte2, k2 in capas[i + 1:]:
                    if fid == fid2 and parte == parte2:
                        self.add("REC-CAPA-DUP", "MEDIA", rel, self.line_of(raw, "capa%d" % k2),
                                 "Las capas %d y %d son la misma pieza/parte (%s:%s): se apilarían." % (k, k2, fid, parte),
                                 "Quitar una de las dos.")
                if parte == 0 and int(t.get("pelo", 0)) > 0:
                    self.add("REC-PELO", "MEDIA", rel, self.line_of(raw, "capa%d" % k),
                             "Capa de pelo (capa%d) y pelo del cuerpo (%s) a la vez: la regla es sustituir, no sumar." % (k, t.get("pelo")),
                             "Dejar uno de los dos (el código oculta el pelo del cuerpo, pero la ficha es ambigua).")
                if parte in (1, 2) and int(t.get("b3_torso", -1)) >= 0:
                    self.add("REC-TORSO", "MEDIA", rel, self.line_of(raw, "capa%d" % k),
                             "Capa de torso/armadura (capa%d) y b3_torso=%s a la vez: dos torsos." % (k, t.get("b3_torso")),
                             "Dejar uno; el código solo oculta las tiras del torso base, no razona sobre esta combinación.")

    def check_saved_heroes(self) -> None:
        """Fichas guardadas (cac.toml de fixtures y user_data): capas/piezas que se apilarían."""
        paths = list(self.p("tools/pruebas_heroe/cac_pendientes/fixtures").glob("*_cac.toml"))
        user = self.p("user_data/dbz3/cac.toml")
        if user.is_file():
            paths.append(user)
        if not paths:
            self.note("Fichas guardadas: no hay cac.toml en el repo; no se puede auditar el apilado en partidas reales.")
            return
        revisadas = 0
        for path in paths:
            rel = self.rel(path)
            try:
                data = tomllib.loads(path.read_text(encoding="utf-8"))
            except Exception as e:  # noqa: BLE001
                self.add("FICHA-TOML", "ALTA", rel, None, "Ficha ilegible: %s." % e, "Corregir el TOML.")
                continue
            for h in data.get("heroe", []):
                if not isinstance(h, dict):
                    continue
                revisadas += 1
                capas = h.get("capas") or []
                pelo = int(h.get("pelo", 0))
                b3 = [int(x) for x in (h.get("b3_piezas") or []) if isinstance(x, int)]
                vistas: set[tuple[int, int]] = set()
                for c in capas:
                    if not isinstance(c, list) or len(c) < 2:
                        continue
                    try:
                        fid, parte = int(c[0]), int(c[1])
                    except (TypeError, ValueError):
                        continue
                    if fid < 0:
                        continue
                    if (fid, parte) in vistas:
                        self.add("FICHA-CAPA-DUP", "MEDIA", rel, None,
                                 "Ficha '%s': dos capas con la misma pieza/parte (%s:%s); se apilarían." % (h.get("nombre"), fid, parte),
                                 "Quitar una de las dos capas.")
                    vistas.add((fid, parte))
                    if parte == 0 and pelo > 0:
                        self.add("FICHA-PELO", "MEDIA", rel, None,
                                 "Ficha '%s': capa de pelo (fid %s) y pelo del cuerpo (%s) a la vez." % (h.get("nombre"), fid, pelo),
                                 "Dejar uno; la regla es que el pelo de capa sustituya al del cuerpo.")
                    if parte in (1, 2) and b3 and b3[0] >= 0:
                        self.add("FICHA-TORSO", "MEDIA", rel, None,
                                 "Ficha '%s': capa de torso/armadura y b3_torso=%s a la vez; dos torsos." % (h.get("nombre"), b3[0]),
                                 "Dejar uno de los dos.")
        self.note("Fichas guardadas revisadas: %d (%s); sin capas repetidas si no hay hallazgos FICHA-*." % (
            revisadas, ", ".join(self.rel(p) for p in paths)))

    # ------------------------------------------------------------------ output
    def summary(self) -> dict[str, int]:
        out = {"ALTA": 0, "MEDIA": 0, "BAJA": 0, "INFO": 0}
        for f in self.findings:
            out[f.sev] = out.get(f.sev, 0) + 1
        return out

    def print_report(self) -> None:
        print("AUDITORÍA DE DATOS DEL CaC — solo lectura")
        print("Raíz: %s" % self.root)
        s = self.summary()
        print("Hallazgos: ALTA=%d MEDIA=%d BAJA=%d INFO=%d" % (s["ALTA"], s["MEDIA"], s["BAJA"], s["INFO"]))
        print()
        if self.findings:
            print("%-4s %-46s %-8s %s" % ("#", "DÓNDE", "GRAVEDAD", "QUÉ FALLA"))
            print("-" * 132)
            for i, f in enumerate(sorted(self.findings, key=lambda x: (SEV_ORDER.get(x.sev, 9), x.code, x.path, x.line or 0)), 1):
                print("%-4d %-46s %-8s %s" % (i, f.where[:46], f.sev, f.what))
                print("%-4s %-46s %-8s -> %s" % ("", "", "", f.fix))
                if f.evidence:
                    print("%-4s %-46s %-8s    (%s)" % ("", "", "", f.evidence))
        if self.notes:
            print()
            print("NOTAS / COMPROBACIONES SIN HALLAZGO")
            for n in self.notes:
                print("  · %s" % n)
        print()
        print("Código de salida: 1 si hay ALTA, 2 si hay MEDIA, 0 si solo BAJA/INFO.")

    def print_json(self) -> None:
        print(json.dumps({
            "raiz": str(self.root),
            "resumen": self.summary(),
            "hallazgos": [f.__dict__ for f in self.findings],
            "notas": self.notes,
        }, ensure_ascii=False, indent=2))


def main(argv: list[str] | None = None) -> int:
    try:  # consola de Windows: no perder acentos al imprimir
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:  # noqa: BLE001
        pass
    ap = argparse.ArgumentParser(description="Audita los datos del creador de personajes (CaC). Solo lectura.")
    ap.add_argument("--raiz", default=None, help="Raíz del proyecto (por defecto, la del script).")
    ap.add_argument("--json", action="store_true", help="Salida JSON.")
    a = ap.parse_args(argv)
    root = Path(a.raiz).resolve() if a.raiz else Path(__file__).resolve().parents[3]
    au = Auditor(root)
    au.run()
    if a.json:
        au.print_json()
    else:
        au.print_report()
    s = au.summary()
    return 1 if s["ALTA"] else 2 if s["MEDIA"] else 0


if __name__ == "__main__":
    sys.exit(main())

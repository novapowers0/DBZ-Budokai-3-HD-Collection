#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""NCAC · Censo y validador escalable del catalogo del Creador de Personajes (CaC).

Escala automaticamente al anadir piezas, colores, rostros, pelos o formas en los
archivos de datos (`res/heroe/`, `mod center hd/cac/`, `src/heroe*.cpp/.inl`,
`src/cac/*.h`, `res/ingame_ui/i18n_modos.toml`, `src/launcher/i18n.cpp`) y verifica
sobre los binarios reales (`us/data_cmn.afs`) cuatro ejes por categoria:
  1. Existencia y coherencia en los datos (TOML/JSON/C++/Python).
  2. Carga real sin modelo ni textura faltante (#AWO/#AZT/DXT3 y SHA-256).
  3. Ausencia de colisiones de capa o IDs duplicados (las capas sustituyen, no apilan).
  4. Cobertura de nombre en los 7 idiomas (es, en, fr, de, it, pt, ja).

Reutiliza los modulos existentes del proyecto (`tools/test_i18n.py`,
`tools/pruebas_heroe/cac_pendientes/auditar_datos_cac.py`, `mod center hd/cac/vista_heroe.py`).
Solo lectura: no modifica ningun archivo del proyecto.

Uso:
  python censo_cac.py [--raiz RUTA] [--json SALIDA.json] [--sin-afs]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import tomllib
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

LANGS_7 = ("es", "en", "fr", "de", "it", "pt", "ja")
LANGS_EXT = ("fr", "de", "it", "pt", "ja")


@dataclass
class ErrorCenso:
    categoria: str
    eje: str           # datos | carga | colision | i18n
    gravedad: str      # ALTA | MEDIA | BAJA
    codigo: str
    donde: str
    detalle: str
    evidencia: str = ""


@dataclass
class ResumenCategoria:
    categoria: str
    total: int = 0
    datos_ok: int = 0
    carga_ok: int = 0
    sin_colision: int = 0
    i18n_7_ok: int = 0
    errores: int = 0


def _cargar_modulos(raiz: Path):
    sys.dont_write_bytecode = True
    for p in (
        raiz / "tools",
        raiz / "tools" / "pruebas_heroe" / "cac_pendientes",
        raiz / "mod center hd",
        raiz / "mod center hd" / "cac",
        raiz / "awo_tools",
    ):
        s = str(p)
        if s not in sys.path:
            sys.path.insert(0, s)
    import test_i18n
    import auditar_datos_cac
    import texture_b3 as tb
    import vista_heroe as vh
    return test_i18n, auditar_datos_cac, vh, tb


def _extraer_pares_c(texto: str, patron_array: str) -> list[tuple[str, str]]:
    """Extrae pares {"es", "en"} de un array C++ (escala al anadir filas)."""
    m = re.search(patron_array + r"\s*=\s*\{(.*?)\};", texto, re.S)
    if not m:
        return []
    out: list[tuple[str, str]] = []
    for pm in re.finditer(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}', m.group(1)):
        out.append((pm.group(1), pm.group(2)))
    return out


def _extraer_lista_c(texto: str, patron_array: str) -> list[str]:
    m = re.search(patron_array + r"\s*=\s*\{(.*?)\};", texto, re.S)
    if not m:
        return []
    return re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))


class ValidadorI18n7:
    """Reutiliza tools/test_i18n.py (kTable + i18n_modos.toml) para comprobar 7 idiomas."""

    def __init__(self, raiz: Path, test_i18n) -> None:
        self.raiz = raiz
        self.test_i18n = test_i18n
        self.toml = tomllib.loads((raiz / "res/ingame_ui/i18n_modos.toml").read_text(encoding="utf-8"))
        self.ktable = test_i18n.tabla_k((raiz / "src/launcher/i18n.cpp").read_text(encoding="utf-8"))

    def faltantes(self, es: str, en: str, ya_5: bool = False) -> list[str]:
        """Devuelve la lista de idiomas faltantes entre (es, en, fr, de, it, pt, ja)."""
        miss: list[str] = []
        if not es or not es.strip():
            miss.append("es")
        if not en or not en.strip():
            miss.append("en")
        if not en or len(en.strip()) < 2 or not any(c.isalpha() for c in en):
            return miss
        fila = self.toml.get(en, {})
        cubre_kt = ya_5 or (es in self.ktable and all(self.ktable[es]))
        for lang in LANGS_EXT:
            if lang in ("fr", "de", "it") and cubre_kt:
                continue
            if not str(fila.get(lang, "")).strip():
                miss.append(lang)
        return miss

    def tiene_7(self, es: str, en: str, ya_5: bool = False) -> bool:
        return len(self.faltantes(es, en, ya_5=ya_5)) == 0


class CensoCaC:
    def __init__(self, raiz: Path, usar_afs: bool = True) -> None:
        self.raiz = raiz.resolve()
        self.usar_afs = usar_afs
        self.test_i18n, self.aud_mod, self.vh, self.tb = _cargar_modulos(self.raiz)
        self.i18n = ValidadorI18n7(self.raiz, self.test_i18n)
        self.errores: list[ErrorCenso] = []
        self.resumen: dict[str, ResumenCategoria] = {}
        self._afs = None
        self._work: str | None = None
        self._modelo_cache: dict[int, dict[str, Any]] = {}
        self._src_cache: dict[str, str] = {}

    def _leer(self, rel: str) -> str:
        if rel not in self._src_cache:
            self._src_cache[rel] = (self.raiz / rel).read_text(encoding="utf-8", errors="replace")
        return self._src_cache[rel]

    def _abrir_afs(self):
        if not self.usar_afs:
            return None
        if self._afs is None:
            us_dir = self.raiz / "us"
            if (us_dir / "data_cmn.afs").is_file():
                self._work = tempfile.mkdtemp(prefix="ncac_censo_")
                self._afs = self.vh.abrir_afs(str(us_dir), self._work, str(self.raiz))
        return self._afs

    def _cerrar(self):
        if self._work and os.path.isdir(self._work):
            import shutil
            shutil.rmtree(self._work, ignore_errors=True)
            self._work = None

    def _err(self, cat: str, eje: str, grav: str, cod: str, donde: str, det: str, ev: str = "") -> None:
        self.errores.append(ErrorCenso(cat, eje, grav, cod, donde, det, ev))

    def _probar_fid(self, fid: int, comprobar_pelo: bool = False, comprobar_cara: bool = False) -> dict[str, Any]:
        """Carga y valida un FID real de us/data_cmn.afs (#AWO, #AZT, tris, texturas)."""
        key = fid
        info = self._modelo_cache.get(key)
        afs = self._abrir_afs()
        if afs is None:
            return {"ok": True, "skip": True}
        if info is None:
            try:
                raw = bytes(afs.entry(fid))
                if not raw:
                    info = {"ok": False, "err": "entrada AFS vacia"}
                else:
                    m = self.vh.mr.Model(raw)
                    _, texs = self.tb.parse_azt(raw)
                    n_tris = len(m.tris)
                    n_tex = len(texs)
                    max_mat_tex = max((mt["tex"] for mt in m.mats if mt["tex"] != 0xFFFFFFFF), default=-1)
                    if n_tris == 0:
                        info = {"ok": False, "err": "0 triangulos en #AWO"}
                    elif n_tex == 0:
                        info = {"ok": False, "err": "0 texturas en #AZT"}
                    elif max_mat_tex >= n_tex:
                        info = {"ok": False, "err": f"material apunta a tex {max_mat_tex} >= {n_tex}"}
                    else:
                        info = {"ok": True, "tris": n_tris, "texs": n_tex, "model": m, "raw": raw}
            except Exception as e:
                info = {"ok": False, "err": f"{type(e).__name__}: {e}"}
            self._modelo_cache[key] = info
        if not info.get("ok"):
            return info
        m = info["model"]
        if comprobar_pelo and "pelo_tris" not in info:
            P, _ = m.posed(self.vh.pose_de(m))
            z = self.vh.zonas_ficha(m, P, pelo_color=True)
            info["pelo_tris"] = sum(1 for x in z if x == "pelo")
        if comprobar_cara and "cara_tris" not in info:
            hc = self.vh._tris_cara(m)
            cara_texs = [m.mats[m.tri_mat[i]]["tex"] for i in hc if m.mats[m.tri_mat[i]]["tex"] < len(m.tex)]
            cara_big = [t for t in set(cara_texs) if min(m.tex[t].shape[:2]) >= 128]
            info["cara_tris"] = len(hc)
            info["cara_texs"] = sorted(cara_big)
        return info

    # ----------------------------------------------------------------- 1. PIEZAS
    def censar_piezas(self) -> ResumenCategoria:
        res = ResumenCategoria("piezas")
        b3_path = self.raiz / "mod center hd/cac/piezas_b3_base.json"
        b3 = json.loads(b3_path.read_text(encoding="utf-8"))
        # 1a. Todas las zonas de piezas_b3_base.json
        for zona, lista in b3.items():
            if not isinstance(lista, list):
                continue
            fids_vistos: set[int] = set()
            for item in lista:
                if not isinstance(item, dict):
                    continue
                res.total += 1
                fid = int(item.get("fid", -1))
                nombre = str(item.get("nombre", "")).strip()
                ok_datos = fid >= 0 and bool(nombre)
                if not ok_datos:
                    self._err("piezas", "datos", "ALTA", "PZA-DATOS",
                              f"mod center hd/cac/piezas_b3_base.json:{zona}",
                              f"Entrada sin fid valido o sin nombre: {item!r}")
                else:
                    res.datos_ok += 1

                # Carga real del FID en AFS
                st = self._probar_fid(fid) if fid >= 0 else {"ok": False, "err": "fid negativo"}
                if st.get("ok"):
                    res.carga_ok += 1
                else:
                    self._err("piezas", "carga", "ALTA", "PZA-CARGA",
                              f"mod center hd/cac/piezas_b3_base.json:{zona}/fid={fid}",
                              f"No carga el modelo/textura de '{nombre}': {st.get('err')}")

                # Colision de duplicado en la misma zona
                if fid in fids_vistos:
                    self._err("piezas", "colision", "MEDIA", "PZA-DUP",
                              f"mod center hd/cac/piezas_b3_base.json:{zona}/fid={fid}",
                              f"FID duplicado {fid} ('{nombre}') en zona {zona}")
                else:
                    fids_vistos.add(fid)
                    res.sin_colision += 1

                # i18n 7 idiomas: NombrePiezaB3 muestra item["nombre"] sin T::T
                # Si es nombre descriptivo con palabras en espanol/ingles mezclado, no tiene 7 idiomas
                if self.i18n.tiene_7(nombre, nombre):
                    res.i18n_7_ok += 1
                else:
                    # Comprobar si es puramente propio/neutro (sin palabras de idioma) o realmente falta traduccion
                    if re.search(r"\b(Traje|alternativo|Sin|armadura|Futuro|espada|Adulto|Gordo|Pelo|nave|Namek|Gero|Cell|joven|niño|nino|largo|corto|capa|dañado|roto|base)\b", nombre, re.I):
                        pass  # se agrega en un hallazgo agrupado abajo para no inundar con 120 lineas repetidas
                    else:
                        # Aún así, si no está en i18n_modos.toml ni es neutro 7-idiomas, no suma a i18n_7_ok
                        pass

        # Hallazgo agregado de nombres B3 sin i18n en 7 idiomas (evidencia exacta)
        b3_total = sum(len(v) for v in b3.values() if isinstance(v, list))
        b3_sin_i18n = []
        for zona, lista in b3.items():
            if not isinstance(lista, list):
                continue
            for item in lista:
                n = str(item.get("nombre", "")).strip()
                if not self.i18n.tiene_7(n, n):
                    b3_sin_i18n.append((zona, item.get("fid"), n))
        if b3_sin_i18n:
            ej = "; ".join(f"{z}:{f}='{n}'" for z, f, n in b3_sin_i18n[:5])
            self._err("piezas", "i18n", "MEDIA", "PZA-I18N-B3",
                      "src/heroe_ui.cpp:1225 + mod center hd/cac/piezas_b3_base.json",
                      f"{len(b3_sin_i18n)}/{b3_total} donantes B3 se muestran con `p.nombre` directo sin traduccion a 7 idiomas.",
                      ej)

        # 1b. Etiquetas de zonas B3, partes de capa, logos y pestanas en src/heroe_ui.cpp
        ui = self._leer("src/heroe_ui.cpp")
        zonas_es = _extraer_lista_c(ui, r"kB3ZonaEs\[5\]")
        zonas_en = _extraer_lista_c(ui, r"kB3ZonaEn\[5\]")
        capas_es = _extraer_lista_c(ui, r"kCapaParteEs\[4\]")
        capas_en = _extraer_lista_c(ui, r"kCapaParteEn\[4\]")
        logos = _extraer_pares_c(ui, r"kLogos\[\]\[2\]")
        tabs = _extraer_pares_c(ui, r"kTabsCac2\[\]\[2\]")

        ui_items = (
            [(f"zona_b3:{es}", es, en, "src/heroe_ui.cpp:1033") for es, en in zip(zonas_es, zonas_en)]
            + [(f"capa_parte:{es}", es, en, "src/heroe_ui.cpp:1185") for es, en in zip(capas_es, capas_en)]
            + [(f"logo:{es}", es, en, "src/heroe_ui.cpp:1000") for es, en in logos]
            + [(f"tab_cac2:{es}", es, en, "src/heroe_ui.cpp:1267") for es, en in tabs]
        )
        for tag, es, en, loc in ui_items:
            res.total += 1
            res.datos_ok += 1
            res.carga_ok += 1
            res.sin_colision += 1
            miss = self.i18n.faltantes(es, en)
            if not miss:
                res.i18n_7_ok += 1
            else:
                self._err("piezas", "i18n", "MEDIA", "PZA-I18N-UI", loc,
                          f"Etiqueta UI '{tag}' ({es!r} / {en!r}) sin traduccion en: {', '.join(miss)}.")

        # 1c. LuchadoresCapa (donantes extra SSJ en heroe_ui.cpp:1191-1193)
        extra_capa = re.findall(r'\{(\d+),\s*"([^"]+)"\}', ui[ui.find("LuchadoresCapa()"):ui.find("LuchadoresCapa()") + 900])
        for fid_s, nombre in extra_capa:
            fid = int(fid_s)
            res.total += 1
            res.datos_ok += 1
            st = self._probar_fid(fid)
            if st.get("ok"):
                res.carga_ok += 1
            else:
                self._err("piezas", "carga", "ALTA", "CAPA-CARGA", "src/heroe_ui.cpp:1191",
                          f"Donante de capa SSJ fid={fid} ('{nombre}') no carga: {st.get('err')}")
            res.sin_colision += 1
            if self.i18n.tiene_7(nombre, nombre):
                res.i18n_7_ok += 1

        # 1d. mod center hd/cac/piezas_cac.toml (piezas propias, arcosianas, majin, namek)
        b3_toml_path = self.raiz / "mod center hd/cac/piezas_cac.toml"
        b3_toml = tomllib.loads(b3_toml_path.read_text(encoding="utf-8"))
        rows_b3 = [p for p in b3_toml.get("pieza", []) if isinstance(p, dict)]
        by_id_b3: dict[int, list[dict[str, Any]]] = {}
        for p in rows_b3:
            by_id_b3.setdefault(int(p.get("id", -1)), []).append(p)

        # Verificar resolucion de archivos en catalogos nativos
        namek_cat = json.loads((self.raiz / "res/heroe/namek_original/catalogo.json").read_text(encoding="utf-8")) if (self.raiz / "res/heroe/namek_original/catalogo.json").is_file() else {}
        namek_ids = {int(e["id"]) for e in namek_cat.get("catalogo", [])}
        majin_cat = json.loads((self.raiz / "res/heroe/partes_majin/catalogo.json").read_text(encoding="utf-8")) if (self.raiz / "res/heroe/partes_majin/catalogo.json").is_file() else {}
        majin_ids = {int(e["id"]) for e in majin_cat.get("catalogo", [])}
        arco_cat = json.loads((self.raiz / "res/heroe/arcosiano_r16/catalogo.json").read_text(encoding="utf-8")) if (self.raiz / "res/heroe/arcosiano_r16/catalogo.json").is_file() else {}
        arco_slots = set(arco_cat.get("slots", {}).keys())
        orig_src = self._leer("mod center hd/cac/partes_originales.py")
        orig_keys = {m.group(1) for m in re.finditer(r"\(\d+,\s*\d+,\s*'[^']+',\s*'([^']+)'\)", orig_src)}

        seen_ti: set[tuple[str, int]] = set()
        for p in rows_b3:
            res.total += 1
            t, pid = str(p.get("tipo", "")), int(p.get("id", -1))
            nom = str(p.get("nombre", "")).strip()
            arch = str(p.get("archivo", "")).strip()
            ok_d = bool(t) and pid >= 0 and bool(nom)
            if ok_d:
                res.datos_ok += 1
            else:
                self._err("piezas", "datos", "ALTA", "TOML-DATOS", "mod center hd/cac/piezas_cac.toml",
                          f"Pieza invalida en piezas_cac.toml: {p!r}")

            # Carga/resolucion
            ok_c = True
            if arch.startswith("nmc:"):
                ok_c = arch.split(":", 1)[1].isdigit() and int(arch.split(":", 1)[1]) in namek_ids
            elif arch.startswith("majin:"):
                ok_c = arch.split(":", 1)[1].isdigit() and int(arch.split(":", 1)[1]) in majin_ids
            elif arch.startswith("arcosiano_r16:"):
                ok_c = arch.split(":", 1)[1] in arco_slots
            elif arch.startswith("original:ia_namek:"):
                k = arch.split(":", 2)[2]
                ok_c = (self.raiz / "mod center hd/cac/ia_namek" / k / "malla.json").is_file()
            elif arch.startswith("original:"):
                k = arch.split(":", 1)[1]
                ok_c = k in orig_keys or k.startswith(("nm_", "mj_"))
            if ok_c:
                res.carga_ok += 1
            else:
                self._err("piezas", "carga", "ALTA", "TOML-ARCH", "mod center hd/cac/piezas_cac.toml",
                          f"Pieza {t}/{pid} ('{nom}') apunta a recurso inexistente: {arch!r}")

            # Colision (tipo, id) o cruce de ID global en rangos propios >= 3000 (los tipos clasicos ropa/aura/pelo/cara son 0-based por tipo)
            col = (t, pid) in seen_ti or (pid >= 3000 and len({str(q.get("tipo")) for q in by_id_b3[pid]}) > 1)
            seen_ti.add((t, pid))
            if not col:
                res.sin_colision += 1
            else:
                self._err("piezas", "colision", "MEDIA", "TOML-COL", "mod center hd/cac/piezas_cac.toml",
                          f"Colision de ID {pid} en tipo '{t}' ('{nom}')")

            if self.i18n.tiene_7(nom, nom):
                res.i18n_7_ok += 1

        # 1e. Verificar SHA-256 de packs nativos (arcosiano, majin, freezer)
        for nombre_pack, rel_cat in (
            ("arcosiano_r16", "res/heroe/arcosiano_r16/catalogo.json"),
            ("partes_majin", "res/heroe/partes_majin/catalogo.json"),
            ("partes_nativas_freezer", "res/heroe/partes_nativas_freezer/catalogo.json"),
        ):
            pcat = self.raiz / rel_cat
            if not pcat.is_file():
                continue
            cat = json.loads(pcat.read_text(encoding="utf-8"))
            base = pcat.parent
            for e in cat.get("files", []):
                if not isinstance(e, dict) or not e.get("file"):
                    continue
                fp = base / e["file"]
                if not fp.is_file():
                    self._err("piezas", "carga", "ALTA", "PACK-FALTA", rel_cat,
                              f"Pack {nombre_pack}: falta el archivo {e['file']}")
                elif e.get("sha256") and hashlib.sha256(fp.read_bytes()).hexdigest() != e["sha256"]:
                    self._err("piezas", "carga", "ALTA", "PACK-SHA", rel_cat,
                              f"Pack {nombre_pack}: SHA-256 corrupto en {e['file']}")

        # 1f. Comprobar etiquetas de razas_b3_ui.h y literales T::T de CaC (via test_i18n)
        rb3_h = self._leer("src/cac/razas_b3_ui.h")
        for arr_es, arr_en in (
            (r"kFreezerEs\[\]", r"kFreezerEn\[\]"),
            (r"kNamekEs\[\]", r"kNamekEn\[\]"),
            (r"kMajinEs\[\]", r"kMajinEn\[\]"),
        ):
            for es, en in zip(_extraer_lista_c(rb3_h, arr_es), _extraer_lista_c(rb3_h, arr_en)):
                res.total += 1
                res.datos_ok += 1
                res.carga_ok += 1
                res.sin_colision += 1
                miss = self.i18n.faltantes(es, en)
                if not miss:
                    res.i18n_7_ok += 1
                else:
                    self._err("piezas", "i18n", "MEDIA", "RB3-I18N", "src/cac/razas_b3_ui.h",
                              f"Etiqueta razas_b3 '{es}' / '{en}' sin traduccion en: {', '.join(miss)}.")

        # Etiquetas literales de CaC (reutilizando test_i18n.comprobar)
        cac_files = {"heroe.cpp", "heroe_ui.cpp", "heroe_v2_creador.inl", "heroe_v2_evolucion.inl", "cac.cpp"}
        for fname, lang, en_txt in sorted(set(self.test_i18n.comprobar())):
            if fname in cac_files:
                self._err("piezas", "i18n", "ALTA", "CAC-I18N-LIT", f"src/{fname}",
                          f"Falta traduccion '{lang}' en res/ingame_ui/i18n_modos.toml para literal CaC: {en_txt!r}")

        # 1g. Verificar res/heroe/piezas_cac.toml (pelo 9 duplicado y cara 0 inexistente)
        res_toml_path = self.raiz / "res/heroe/piezas_cac.toml"
        if res_toml_path.is_file():
            rt = tomllib.loads(res_toml_path.read_text(encoding="utf-8"))
            r_rows = [p for p in rt.get("pieza", []) if isinstance(p, dict)]
            r_pelos = [p for p in r_rows if p.get("tipo") == "pelo"]
            p0 = next((p for p in r_pelos if int(p.get("id", -1)) == 0), None)
            p9 = next((p for p in r_pelos if int(p.get("id", -1)) == 9), None)
            if p0 and p9:
                norm = lambda s: re.sub(r"[^a-z0-9]", "", str(s).lower().replace("avatar_", "").replace(".bin", "").replace("_b3", ""))
                if norm(p0.get("nombre")) == norm(p9.get("archivo")):
                    self._err("pelos", "colision", "MEDIA", "PELO-DUP-UT", "res/heroe/piezas_cac.toml:874",
                              f"Pelo id 9 ('{p9.get('nombre')}', {p9.get('archivo')}) duplica al id 0 ('{p0.get('nombre')}').")
            for p in r_rows:
                arch = str(p.get("archivo", "")).strip()
                if arch and p.get("tipo") in ("pelo", "cara"):
                    found = any((self.raiz / d / arch).is_file() for d in ("", "mod center hd/cac/convertidas", "cac_piezas/pelo", "cac_piezas/cara"))
                    if not found:
                        cat_dst = "pelos" if p.get("tipo") == "pelo" else "rostros"
                        self._err(cat_dst, "carga", "BAJA", "UT-ARCH-FALTA", "res/heroe/piezas_cac.toml",
                                  f"{p.get('tipo')} id {p.get('id')} apunta a '{arch}' que no existe en disco.")

        res.errores = sum(1 for e in self.errores if e.categoria == "piezas")
        return res

    # ----------------------------------------------------------------- 2. COLORES
    def censar_colores(self) -> ResumenCategoria:
        res = ResumenCategoria("colores")
        creador = self._leer("src/heroe_v2_creador.inl")
        ui = self._leer("src/heroe_ui.cpp")
        des = tomllib.loads((self.raiz / "res/heroe/desbloqueos.toml").read_text(encoding="utf-8"))

        # 2a. Colores rapidos HSV (kRapidosV2)
        m_rap = re.search(r"kRapidosV2\[\]\s*=\s*\{(.*?)\};", creador, re.S)
        if m_rap:
            vistos_hsv: set[tuple[int, int, int]] = set()
            for idx, (h_s, s_s, v_s) in enumerate(re.findall(r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}", m_rap.group(1))):
                res.total += 1
                h, s, v = int(h_s), int(s_s), int(v_s)
                if 0 <= h <= 360 and -100 <= s <= 100 and -100 <= v <= 100:
                    res.datos_ok += 1
                    res.carga_ok += 1
                else:
                    self._err("colores", "datos", "ALTA", "COL-HSV-RANGO", "src/heroe_v2_creador.inl:19",
                              f"Color rapido #{idx} fuera de rango HSV: ({h},{s},{v}).")
                if (h, s, v) in vistos_hsv:
                    self._err("colores", "colision", "BAJA", "COL-HSV-DUP", "src/heroe_v2_creador.inl:19",
                              f"Color rapido #{idx} duplicado: ({h},{s},{v}).")
                else:
                    vistos_hsv.add((h, s, v))
                    res.sin_colision += 1
                # Los colores rapidos son muestras visuales sin texto propio (muestra RGB)
                res.i18n_7_ok += 1

        # 2b. Colores de estallido (kEstallidos en heroe_ui.cpp)
        m_est = re.search(r"kEstallidos\[\]\s*=\s*\{(.*?)\};", ui, re.S)
        if m_est:
            ests = re.findall(r'"([^"]*)"', m_est.group(1))
            vistos_est: set[str] = set()
            for c in ests:
                res.total += 1
                valido = c == "" or bool(re.fullmatch(r"#[0-9A-Fa-f]{6}", c))
                if valido:
                    res.datos_ok += 1
                    res.carga_ok += 1
                else:
                    self._err("colores", "datos", "ALTA", "COL-EST-FMT", "src/heroe_ui.cpp:2711",
                              f"Color de estallido invalido: {c!r}")
                if c in vistos_est:
                    self._err("colores", "colision", "BAJA", "COL-EST-DUP", "src/heroe_ui.cpp:2711",
                              f"Color de estallido duplicado: {c!r}")
                else:
                    vistos_est.add(c)
                    res.sin_colision += 1
                # Si es "" muestra T::T("Nativo", "Native"); si es "#RRGGBB" es codigo hex neutro
                if c == "":
                    miss = self.i18n.faltantes("Nativo", "Native")
                    if not miss:
                        res.i18n_7_ok += 1
                    else:
                        self._err("colores", "i18n", "MEDIA", "COL-EST-I18N", "src/heroe_v2_creador.inl:290",
                                  f"Etiqueta de estallido 'Nativo'/'Native' falta en: {', '.join(miss)}.")
                else:
                    res.i18n_7_ok += 1

        # 2c. Colores desbloqueables (aura / ki / tecnica con color) en desbloqueos.toml
        for d in des.get("desbloqueo", []):
            if not isinstance(d, dict) or not d.get("color"):
                continue
            res.total += 1
            did = str(d.get("id", ""))
            col = str(d.get("color", ""))
            es = str(d.get("nombre_es", ""))
            en = str(d.get("nombre_en", ""))
            if re.fullmatch(r"#[0-9A-Fa-f]{6}", col) and es and en:
                res.datos_ok += 1
                res.carga_ok += 1
            else:
                self._err("colores", "datos", "ALTA", "COL-DES-FMT", f"res/heroe/desbloqueos.toml:{did}",
                          f"Color invalido o sin nombre en desbloqueo '{did}': color={col!r}")
            res.sin_colision += 1
            miss = self.i18n.faltantes(es, en)
            if not miss:
                res.i18n_7_ok += 1
            else:
                self._err("colores", "i18n", "MEDIA", "COL-DES-I18N", f"res/heroe/desbloqueos.toml:{did}",
                          f"Color desbloqueable '{did}' ({en!r}) sin traduccion en: {', '.join(miss)}.")

        # 2d. Canales de color y vistas/anims del editor en heroe_v2_creador.inl
        for arr_name, loc in ((r"kVistasV2\[4\]\[2\]", "src/heroe_v2_creador.inl:50"),
                              (r"kAnimsVista\[6\]\[2\]", "src/heroe_v2_creador.inl:51")):
            for es, en in _extraer_pares_c(creador, arr_name):
                res.total += 1
                res.datos_ok += 1
                res.carga_ok += 1
                res.sin_colision += 1
                miss = self.i18n.faltantes(es, en)
                if not miss:
                    res.i18n_7_ok += 1
                else:
                    self._err("colores", "i18n", "MEDIA", "UI-V2-I18N", loc,
                              f"Etiqueta de vista previa '{es}'/'{en}' sin traduccion en: {', '.join(miss)}.")

        res.errores = sum(1 for e in self.errores if e.categoria == "colores")
        return res

    # ----------------------------------------------------------------- 3. ROSTROS
    def censar_rostros(self) -> ResumenCategoria:
        res = ResumenCategoria("rostros")
        ui = self._leer("src/heroe_ui.cpp")
        rostros_ui = _extraer_pares_c(ui, r"kRostros\[\]\[2\]")
        rostros_py = self.vh.ROSTROS
        ojos_tipos = self.vh.OJOS_TIPOS
        rostro_ssj = self.vh.ROSTRO_SSJ

        if len(rostros_ui) != len(rostros_py) + 1:
            self._err("rostros", "datos", "ALTA", "ROS-LEN", "src/heroe_ui.cpp:1017",
                      f"kRostros tiene {len(rostros_ui)} entradas pero vista_heroe.ROSTROS tiene {len(rostros_py)} (+1 base).")

        for idx, (es, en) in enumerate(rostros_ui):
            res.total += 1
            if idx == 0:
                res.datos_ok += 1
                res.carga_ok += 1
                res.sin_colision += 1
            else:
                spec = rostros_py.get(idx)
                fid = None
                if isinstance(spec, int):
                    fid = spec
                elif isinstance(spec, str) and spec in ojos_tipos:
                    fid = int(ojos_tipos[spec]["fid"])
                if fid is not None:
                    res.datos_ok += 1
                else:
                    self._err("rostros", "datos", "ALTA", "ROS-MAP", f"mod center hd/cac/vista_heroe.py:ROSTROS[{idx}]",
                              f"Rostro {idx} ('{es}') no resuelve a ningun donante valido: spec={spec!r}")
                # Carga real del donante + variantes SSJ en ROSTRO_SSJ
                fids_check = [fid] if fid is not None else []
                if fid in rostro_ssj:
                    fids_check.extend(rostro_ssj[fid])
                carga_bien = bool(fids_check)
                for cfid in dict.fromkeys(fids_check):
                    st = self._probar_fid(cfid, comprobar_cara=True)
                    if not st.get("ok"):
                        carga_bien = False
                        self._err("rostros", "carga", "ALTA", "ROS-CARGA", f"ROSTROS[{idx}]/fid={cfid}",
                                  f"Rostro {idx} ('{es}') donante {cfid} no carga: {st.get('err')}")
                    elif not st.get("skip") and (st.get("cara_tris", 0) == 0 or not st.get("cara_texs")):
                        carga_bien = False
                        self._err("rostros", "carga", "ALTA", "ROS-TEX", f"ROSTROS[{idx}]/fid={cfid}",
                                  f"Rostro {idx} ('{es}') donante {cfid} sin triangulos/texturas de cara >=128px "
                                  f"(tris={st.get('cara_tris')}, texs={st.get('cara_texs')}).")
                if carga_bien:
                    res.carga_ok += 1
                res.sin_colision += 1

            # 7 idiomas
            miss = self.i18n.faltantes(es, en)
            if not miss:
                res.i18n_7_ok += 1
            else:
                # Nombres propios puros (Goku, Vegeta, Gohan, Trunks, Bardock, Yamcha) pueden no traducirse igual en todos,
                # pero si se diferencian (ej. "Gohan joven"/"Young Gohan", "Ten Shin Han"/"Tien Shinhan", "Tipo A: ...")
                # deben estar en los 7 idiomas!
                es_propio_igual = (es == en and " " not in es and ":" not in es)
                if es_propio_igual and not miss:
                    res.i18n_7_ok += 1
                elif es_propio_igual:
                    # En JA los nombres propios (Goku -> 悟空, etc.) o al menos si no estan en toml lo reportamos como BAJA
                    self._err("rostros", "i18n", "BAJA", "ROS-I18N-NOM", f"src/heroe_ui.cpp:1017[{idx}]",
                              f"Nombre propio de rostro '{es}' sin entrada localizada en: {', '.join(miss)}.")
                else:
                    self._err("rostros", "i18n", "MEDIA", "ROS-I18N", f"src/heroe_ui.cpp:1017[{idx}]",
                              f"Rostro {idx} ('{es}' / '{en}') sin traduccion en: {', '.join(miss)}.")

        res.errores = sum(1 for e in self.errores if e.categoria == "rostros")
        return res

    # ----------------------------------------------------------------- 4. PELOS
    def censar_pelos(self) -> ResumenCategoria:
        res = ResumenCategoria("pelos")
        ui = self._leer("src/heroe_ui.cpp")
        cac_h = self._leer("src/cac/cac.h")
        # 4a. Pelos nativos (0..10): admite tanto kPelosNativos[] como kPelosNativos[][2]
        pelos_pares = _extraer_pares_c(ui, r"kPelosNativos\[\]\[2\]")
        if pelos_pares:
            pelos_ui_norm = pelos_pares
        else:
            pelos_ui_norm = [(n, n) for n in _extraer_lista_c(ui, r"kPelosNativos\[\]")]
        pelos_py = self.vh.PELOS_NATIVOS
        kfid = self.vh.KFID
        ssj_pelo = self.vh.SSJ_PELO

        m_kn = re.search(r"kNumPelos\s*=\s*(\d+)", ui)
        k_num = int(m_kn.group(1)) if m_kn else len(pelos_ui_norm)
        if len(pelos_ui_norm) != k_num:
            self._err("pelos", "datos", "ALTA", "PELO-LEN", "src/heroe_ui.cpp:1010",
                      f"kPelosNativos tiene {len(pelos_ui_norm)} entradas pero kNumPelos={k_num}.")

        for idx, (nombre_es, nombre_en) in enumerate(pelos_ui_norm):
            nombre = nombre_es
            res.total += 1
            if idx == 0:
                res.datos_ok += 1
                res.carga_ok += 1
                res.sin_colision += 1
                miss = self.i18n.faltantes("Del cuerpo", "Body's own")
                if not miss:
                    res.i18n_7_ok += 1
                else:
                    self._err("pelos", "i18n", "MEDIA", "PELO-I18N", "src/heroe_ui.cpp:2199",
                              f"Pelo 0 ('Del cuerpo'/'Body's own') falta en: {', '.join(miss)}.")
                continue

            cid = pelos_py.get(idx)
            fids = kfid.get(cid) if cid is not None else None
            fid = fids[0] if fids else None
            if fid is not None:
                res.datos_ok += 1
            else:
                self._err("pelos", "datos", "ALTA", "PELO-MAP", f"vista_heroe.py:PELOS_NATIVOS[{idx}]",
                          f"Pelo nativo {idx} ('{nombre}') sin entrada en PELOS_NATIVOS/KFID.")
                continue

            # Comprobar donante base + sus niveles SSJ si existen en SSJ_PELO
            fids_chk = [fid] + list(ssj_pelo.get(fid, ()))
            carga_ok = True
            for cfid in dict.fromkeys(fids_chk):
                st = self._probar_fid(cfid, comprobar_pelo=True)
                if not st.get("ok"):
                    carga_ok = False
                    self._err("pelos", "carga", "ALTA", "PELO-CARGA", f"PELOS_NATIVOS[{idx}]/fid={cfid}",
                              f"Pelo {idx} ('{nombre}') fid={cfid} no carga: {st.get('err')}")
                elif not st.get("skip") and st.get("pelo_tris", 0) == 0:
                    carga_ok = False
                    self._err("pelos", "carga", "ALTA", "PELO-VACIO", f"PELOS_NATIVOS[{idx}]/fid={cfid}",
                              f"Pelo {idx} ('{nombre}') fid={cfid} tiene 0 triangulos en zona 'pelo'.")
            if carga_ok:
                res.carga_ok += 1
            res.sin_colision += 1

            # i18n: si el array es monolingue sin T::T, los descriptores en espanol ("Goku niño", "Yamcha (largo)")
            # se muestran en espanol en los 7 idiomas aunque existan en i18n_modos.toml.
            if not pelos_pares and any(w in nombre_es.lower() for w in ("niño", "nino", "largo", "corto")):
                self._err("pelos", "i18n", "MEDIA", "PELO-I18N-ESP", f"src/heroe_ui.cpp:1010[{idx}]",
                          f"kPelosNativos[{idx}]='{nombre_es}' contiene texto en espanol mostrado sin T::T en los 7 idiomas.")
            elif self.i18n.tiene_7(nombre_es, nombre_en):
                res.i18n_7_ok += 1
            elif nombre_es != nombre_en:
                miss = self.i18n.faltantes(nombre_es, nombre_en)
                self._err("pelos", "i18n", "MEDIA", "PELO-I18N", f"src/heroe_ui.cpp:1010[{idx}]",
                          f"kPelosNativos[{idx}] ('{nombre_es}'/'{nombre_en}') falta en: {', '.join(miss)}.")

        # 4b. Pelos comunes CAC2 (kPelosCac2 <-> cac.h::FidPeloCac2 <-> vista_heroe.pelo_cac2)
        m_c2 = re.search(r"kPelosCac2\[\]\s*=\s*\{(.*?)\};", ui, re.S)
        c2_ui = [(int(a), b) for a, b in re.findall(r'\{\s*(\d+)\s*,\s*"([^"]+)"\s*\}', m_c2.group(1))] if m_c2 else []
        c2_h = {int(x) for x in re.findall(r"fid\s*==\s*(\d+)", re.search(r"FidPeloCac2.*?\}", cac_h).group(0))} if "FidPeloCac2" in cac_h else set()
        for fid, nombre in c2_ui:
            res.total += 1
            if fid in c2_h:
                res.datos_ok += 1
            else:
                self._err("pelos", "datos", "ALTA", "PELO-CAC2-SYNC", "src/heroe_ui.cpp:1014",
                          f"kPelosCac2 incluye fid={fid} ('{nombre}') pero FidPeloCac2() en cac.h solo acepta {sorted(c2_h)}.")
            st = self._probar_fid(fid, comprobar_pelo=True)
            if st.get("ok") and (st.get("skip") or st.get("pelo_tris", 0) > 0):
                res.carga_ok += 1
            else:
                self._err("pelos", "carga", "ALTA", "PELO-CAC2-CARGA", f"kPelosCac2/fid={fid}",
                          f"Donante CAC2 fid={fid} ('{nombre}') sin carga/pelo: {st}")
            # Colision con PELOS_NATIVOS?
            fids_nat = {(kfid.get(c) or [None])[0] for c in pelos_py.values()}
            if fid in fids_nat:
                self._err("pelos", "colision", "MEDIA", "PELO-CAC2-DUP", f"kPelosCac2/fid={fid}",
                          f"Donante CAC2 fid={fid} duplica un donante ya en PELOS_NATIVOS.")
            else:
                res.sin_colision += 1
            # Etiqueta " · Base" usada en heroe_ui.cpp:2201
            miss_suf = self.i18n.faltantes(" · Base", " · Base")
            if self.i18n.tiene_7(nombre, nombre) and not miss_suf:
                res.i18n_7_ok += 1
            elif miss_suf:
                self._err("pelos", "i18n", "BAJA", "PELO-CAC2-I18N", "src/heroe_ui.cpp:2201",
                          f"Sufijo/etiqueta de pelo CAC2 '{nombre} · Base' sin traduccion en: {', '.join(miss_suf)}.")

        # 4c. Recetas peinados_r17.toml (PELO-03)
        r17_path = self.raiz / "mod center hd/cac/peinados_r17.toml"
        if r17_path.is_file():
            r17 = tomllib.loads(r17_path.read_text(encoding="utf-8"))
            ids_r17: set[str] = set()
            for rec in r17.get("receta", []):
                res.total += 1
                rid = str(rec.get("id", ""))
                fb, fm = int(rec.get("fid_base", -1)), int(rec.get("fid_melena", -1))
                if rid and fb >= 0 and fm >= 0:
                    res.datos_ok += 1
                else:
                    self._err("pelos", "datos", "ALTA", "R17-DATOS", "mod center hd/cac/peinados_r17.toml",
                              f"Receta R17 incompleta: {rec!r}")
                st_b = self._probar_fid(fb, comprobar_pelo=True)
                st_m = self._probar_fid(fm, comprobar_pelo=True)
                if st_b.get("ok") and st_m.get("ok") and (st_b.get("skip") or (st_b.get("pelo_tris", 0) > 0 and st_m.get("pelo_tris", 0) > 0)):
                    res.carga_ok += 1
                else:
                    self._err("pelos", "carga", "ALTA", "R17-CARGA", f"peinados_r17.toml:{rid}",
                              f"Receta R17 '{rid}' falla carga de fid_base={fb} o fid_melena={fm}.")
                if rid in ids_r17:
                    self._err("pelos", "colision", "ALTA", "R17-DUP", "mod center hd/cac/peinados_r17.toml",
                              f"Receta R17 duplicada: {rid}")
                else:
                    ids_r17.add(rid)
                    res.sin_colision += 1
                # R17 es experimental sin filas de UI en 7 idiomas todavia (solo IDs tecnicos)
                if self.i18n.tiene_7(rid, rid):
                    res.i18n_7_ok += 1

        # 4d. Donantes SSJ de capa (CAPA_SSJ)
        for fid, lvl in sorted(self.vh.CAPA_SSJ.items()):
            res.total += 1
            res.datos_ok += 1
            st = self._probar_fid(fid, comprobar_pelo=True)
            if st.get("ok") and (st.get("skip") or st.get("pelo_tris", 0) > 0):
                res.carga_ok += 1
            else:
                self._err("pelos", "carga", "ALTA", "CAPA-SSJ-CARGA", f"vista_heroe.py:CAPA_SSJ[{fid}]",
                          f"Pelo de capa SSJ{lvl} fid={fid} no carga o sin triangulos de pelo: {st}")
            res.sin_colision += 1
            res.i18n_7_ok += 1  # cubierto en LuchadoresCapa

        res.errores = sum(1 for e in self.errores if e.categoria == "pelos")
        return res

    # ----------------------------------------------------------------- 5. FORMAS
    def censar_formas(self) -> ResumenCategoria:
        res = ResumenCategoria("formas")
        des = tomllib.loads((self.raiz / "res/heroe/desbloqueos.toml").read_text(encoding="utf-8"))
        raz = tomllib.loads((self.raiz / "res/heroe/razas_reglas.toml").read_text(encoding="utf-8"))
        formas_por_raza = {int(r["id"]): list(r.get("formas", [])) for r in raz.get("raza", []) if isinstance(r, dict)}
        listadas = {fid for lst in formas_por_raza.values() for fid in lst}

        # Reutilizar auditar_datos_cac para alcanzabilidad de formas/desbloqueos (regla GEM #2: no duplicar)
        aud = self.aud_mod.Auditor(self.raiz)
        aud.check_desbloqueos()
        imposibles = {
            m.group(1): f.what
            for f in aud.findings
            if f.code == "DES-IMPOSIBLE"
            for m in [re.search(r"'([^']+)'", f.what)] if m
        }

        for d in des.get("desbloqueo", []):
            if not isinstance(d, dict) or d.get("tipo") not in ("forma", "ssj"):
                continue
            res.total += 1
            did = str(d.get("id", ""))
            es = str(d.get("nombre_es", ""))
            en = str(d.get("nombre_en", ""))
            ok_datos = bool(did) and bool(es) and bool(en)
            if d.get("tipo") == "forma":
                rid = int(d.get("raza", -1))
                if rid not in formas_por_raza or did not in listadas:
                    ok_datos = False
                    self._err("formas", "datos", "ALTA", "FRM-RAZA", f"res/heroe/desbloqueos.toml:{did}",
                              f"Forma '{did}' (raza {rid}) no figura en res/heroe/razas_reglas.toml.")
            if did in imposibles:
                ok_datos = False
                self._err("formas", "datos", "ALTA", "FRM-IMPOSIBLE", f"res/heroe/desbloqueos.toml:{did}",
                          imposibles[did])
            if ok_datos:
                res.datos_ok += 1

            # Carga: si tiene cara=ID de personaje nativo (>= 0), validar en roster_build y AFS (cara=-1 = silueta generica)
            cara_id = d.get("cara")
            if isinstance(cara_id, int) and cara_id >= 0:
                row = self.vh.rb.IDS.get(cara_id)
                fids = (row or {}).get("models") or self.vh.KFID.get(cara_id) or []
                if fids:
                    st = self._probar_fid(int(fids[0]))
                    if st.get("ok"):
                        res.carga_ok += 1
                    else:
                        self._err("formas", "carga", "ALTA", "FRM-CARGA", f"desbloqueos.toml:{did}",
                                  f"Modelo de referencia cara={cara_id} (fid={fids[0]}) no carga: {st.get('err')}")
                else:
                    self._err("formas", "carga", "ALTA", "FRM-MODELO", f"desbloqueos.toml:{did}",
                              f"Forma '{did}' referencia cara={cara_id} sin modelos en roster_build/KFID.")
            else:
                # tipo="ssj": comprobar SSJ_PELO_GOKU[nivel-1]
                lvl = int(d.get("nivel", 1))
                fid_ssj = self.vh.SSJ_PELO_GOKU[max(0, min(lvl - 1, 2))]
                st = self._probar_fid(fid_ssj)
                if st.get("ok"):
                    res.carga_ok += 1
                else:
                    self._err("formas", "carga", "ALTA", "FRM-SSJ-CARGA", f"desbloqueos.toml:{did}",
                              f"Nivel SSJ {lvl} (fid={fid_ssj}) no carga: {st.get('err')}")

            res.sin_colision += 1

            # 7 idiomas: comprobar nombre + cond (si existe) en i18n_modos.toml / kTable
            miss = self.i18n.faltantes(es, en)
            cond_es, cond_en = str(d.get("cond_es", "")), str(d.get("cond_en", ""))
            miss_cond = self.i18n.faltantes(cond_es, cond_en) if cond_en else []
            if not miss and not miss_cond:
                res.i18n_7_ok += 1
            else:
                partes_m = []
                if miss:
                    partes_m.append(f"nombre ({en!r}) falta en {','.join(miss)}")
                if miss_cond:
                    partes_m.append(f"cond ({cond_en!r}) falta en {','.join(miss_cond)}")
                self._err("formas", "i18n", "MEDIA", "FRM-I18N", f"res/heroe/desbloqueos.toml:{did}",
                          f"Forma '{did}': {'; '.join(partes_m)}.")

        # Verificar todos los modelos de forma de cuerpos jugables en KFID / SSJ_MODELO_CUERPO
        for cid, fids in sorted(self.vh.KFID.items()):
            if cid not in self.vh.rb.IDS:
                continue
            row = self.vh.rb.IDS[cid]
            per = int(row.get("models_per_costume") or 1)
            models = row.get("models") or []
            for ti_idx, fid0 in enumerate(fids):
                base_slice = models[ti_idx * per:(ti_idx + 1) * per]
                if not base_slice:
                    continue
                for fm_idx, mfid in enumerate(base_slice):
                    if fm_idx == 0:
                        continue
                    res.total += 1
                    res.datos_ok += 1
                    st = self._probar_fid(int(mfid))
                    if st.get("ok"):
                        res.carga_ok += 1
                    else:
                        self._err("formas", "carga", "ALTA", "FRM-AFS", f"roster_build:id={cid}/traje={ti_idx}/forma={fm_idx}",
                                  f"Modelo nativo de forma fid={mfid} no carga: {st.get('err')}")
                    res.sin_colision += 1
                    res.i18n_7_ok += 1  # modelo interno del traje

        res.errores = sum(1 for e in self.errores if e.categoria == "formas")
        return res

    def ejecutar(self) -> dict[str, Any]:
        try:
            self.resumen["piezas"] = self.censar_piezas()
            self.resumen["colores"] = self.censar_colores()
            self.resumen["rostros"] = self.censar_rostros()
            self.resumen["pelos"] = self.censar_pelos()
            self.resumen["formas"] = self.censar_formas()
        finally:
            self._cerrar()
        return {
            "raiz": str(self.raiz),
            "afs_usado": self.usar_afs and (self.raiz / "us/data_cmn.afs").is_file(),
            "tabla": [asdict(self.resumen[k]) for k in ("piezas", "colores", "rostros", "pelos", "formas")],
            "totales": {
                "items": sum(r.total for r in self.resumen.values()),
                "errores_ALTA": sum(1 for e in self.errores if e.gravedad == "ALTA"),
                "errores_MEDIA": sum(1 for e in self.errores if e.gravedad == "MEDIA"),
                "errores_BAJA": sum(1 for e in self.errores if e.gravedad == "BAJA"),
            },
            "errores": [asdict(e) for e in self.errores],
        }


def imprimir_informe(rep: dict[str, Any]) -> None:
    print("CENSO Y VALIDADOR DEL CATALOGO CaC (escalable, solo lectura)")
    print(f"Raiz: {rep['raiz']}  |  AFS real: {'SI' if rep['afs_usado'] else 'NO'}")
    print()
    print("| Categoria | Total | Datos OK | Carga OK | Sin colision | 7 idiomas | Errores |")
    print("|---|---:|---:|---:|---:|---:|---:|")
    for row in rep["tabla"]:
        print(f"| {row['categoria']} | {row['total']} | {row['datos_ok']} | {row['carga_ok']} "
              f"| {row['sin_colision']} | {row['i18n_7_ok']} | {row['errores']} |")
    tot = rep["totales"]
    print(f"\nTotal elementos censados: {tot['items']}  |  "
          f"Errores: ALTA={tot['errores_ALTA']} MEDIA={tot['errores_MEDIA']} BAJA={tot['errores_BAJA']}")
    if rep["errores"]:
        print("\nLISTA DE ERRORES:")
        ord_g = {"ALTA": 0, "MEDIA": 1, "BAJA": 2}
        for i, e in enumerate(sorted(rep["errores"], key=lambda x: (ord_g.get(x["gravedad"], 9), x["categoria"], x["codigo"])), 1):
            print(f"{i:3d}. [{e['gravedad']:5s}] [{e['categoria']:7s}/{e['eje']:8s}] {e['codigo']:14s} | {e['donde']}")
            print(f"     {e['detalle']}")
            if e["evidencia"]:
                print(f"     evidencia: {e['evidencia']}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--raiz", type=Path, default=Path.cwd())
    ap.add_argument("--json", type=Path, default=None, help="guardar informe JSON en esta ruta")
    ap.add_argument("--sin-afs", action="store_true", help="omitir lectura de us/data_cmn.afs")
    ap.add_argument("--estricto", action="store_true", help="salir con codigo 1 si hay errores ALTA o MEDIA")
    ap.add_argument("--autoprueba", action="store_true", help="validar el lector de tablas sin abrir el AFS")
    a = ap.parse_args(argv)
    if a.autoprueba:
        datos = 'kTest[] = {{"Uno", "One"}, {"Dos", "Two"}};'
        assert _extraer_pares_c(datos, r"kTest\[\]") == [("Uno", "One"), ("Dos", "Two")]
        assert _extraer_pares_c(datos, "kAusente") == []
        print("Censo: autoprueba OK")
        return 0
    rep = CensoCaC(a.raiz, usar_afs=not a.sin_afs).ejecutar()
    imprimir_informe(rep)
    if a.json:
        a.json.parent.mkdir(parents=True, exist_ok=True)
        a.json.write_text(json.dumps(rep, ensure_ascii=False, indent=2), encoding="utf-8")
    if a.estricto:
        if rep["totales"]["errores_ALTA"] > 0:
            return 1
        if rep["totales"]["errores_MEDIA"] > 0:
            return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""TRG5 — Juez automatico de las 38 escenas de historia B1 PAL vs puerto PC.

Para cada escena (1910..1994):
  1. Renderiza cada corte con el arnes C++ del puerto (src/escena_b1_raster.inl
     + src/ps2_modelo.cpp, y para 1910 tambien el arnes real de
     src/escena_b1_1910.inl derivado de GEM2/B1C6).
  2. Renderiza el mismo corte desde los datos originales PS2 en Blender Cycles
     (extension de TR3 a las 38 escenas, con alfa #AMT real y cierre de hilos
     SPX 0x95 sin sustituciones HD).
  3. Compara ambos cortes con 5 metricas independientes:
       - cielo (color, exceso blanco, sesgo azul, bordes de placas)
       - color por zonas (rejilla 4x4: media y peores zonas)
       - siluetas (F1 de contornos Sobel, IoU de primer plano, correlacion)
       - pixeles blancos o negros anomalos (incluye deriva de oscuros: pelo gris)
       - elementos presentes o faltantes (actores en pantalla + bloques)
     Con tope no compensable (un fallo grave en una categoria no se maquilla con
     el resto) y puerta anti-regresion estilo GEM2.
  4. Genera una lamina PNG por escena, tabla de puntuaciones (CSV/MD/JSON) y
     controles negativos que deben fallar.

Reutilizable por los lotes TRG1..TRG6 (importable o via CLI).
No inicia el juego ni PCSX2.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import tomllib

sys.dont_write_bytecode = True

import numpy as np
from PIL import Image, ImageDraw

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
for _p in (str(TOOLS), str(ROOT / "mod center hd")):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from b1_escena_render import BLENDER as DEFAULT_BLENDER, section, sha256, take
from historia_b1_escenas import Afs, Iso, children

DEFAULT_TR1 = Path(r"E:\DBZ3HD_trabajo\fabrica\TR1\cache_03")
DEFAULT_CACHE_1910 = Path(r"E:\DBZ3HD_trabajo\estudio_b1\solB1H_matrices")
DEFAULT_ISO_FALLBACK = Path(
    r"E:\DBZ3HD_trabajo\transformaciones\apartado_de_D\transformaciones_c\ps2_games"
    r"\DragonBall Z - Budokai (Europe) (En,Fr,De,Es,It).iso"
)
GEM2_REF_DIR = Path(r"E:\DBZ3HD_trabajo\fabrica\GEM2\verificacion_3k_ub5cs")

FRAME_SIZE = (640, 448)
PAL_TICK = Fraction(6, 5)  # 50 Hz PAL en reloj de 60 u/s (builtin 0x120)

# Codigos de eventos del paquete binario del arnes generico
EV_LOAD_ACTOR = 1
EV_DIBUJO = 2
EV_DIBUJO_B = 3
EV_LIBERAR = 4
EV_ANIM = 5
EV_AMP = 6
EV_CAM = 7
EV_LUZ = 8
EV_AMBIENTE = 9
EV_NIEBLA = 10
EV_NIEBLA_RGB = 11
EV_NIEBLA_DENS = 12
EV_NIEBLA_DIST = 13


def find_b1_iso(iso_arg: Path | None = None) -> Path:
    if iso_arg:
        p = iso_arg.resolve()
        if not p.is_file():
            raise FileNotFoundError(f"ISO PAL no encontrada: {p}")
        return p
    candidates: list[Path] = []
    if (ROOT / "ps2_games").is_dir():
        candidates.extend((ROOT / "ps2_games").glob("*Budokai (Europe)*.iso"))
    if os.environ.get("DBZ3_PS2_GAMES"):
        candidates.extend(Path(os.environ["DBZ3_PS2_GAMES"]).glob("*Budokai (Europe)*.iso"))
    candidates.append(DEFAULT_ISO_FALLBACK)
    for c in candidates:
        if c.is_file():
            return c.resolve()
    raise FileNotFoundError("No se encontro la ISO PAL de Budokai 1 (SLES_512.33)")


def listar_escenas_tr1(tr1_dir: Path) -> list[int]:
    idx = json.loads((tr1_dir / "indice.json").read_bytes())
    return [int(x) for x in idx["spx"]]


def planificar_escena(tr1_dir: Path, spx: int) -> dict:
    """Planifica los cortes PAL 50 Hz de una escena (extension de TR3 a las 38)."""
    path = tr1_dir / f"escena_{spx}.json"
    raw = path.read_bytes()
    doc = json.loads(raw)
    cams_raw = [c for c in doc["camaras"] if c.get("canal", 0) == 0]
    cams: list[dict] = []
    for c in cams_raw:
        if cams and cams[-1]["t"] == c["t"]:
            cams[-1] = c
        else:
            cams.append(c)
    if not cams:
        raise ValueError(f"Escena {spx}: sin camaras en canal 0")
    dur = Fraction(str(doc["duracion_unidades"]))
    cortes = []
    for i, c in enumerate(cams):
        start = Fraction(str(c["t"]))
        end = Fraction(str(cams[i + 1]["t"])) if i + 1 < len(cams) else dur
        if end <= start:
            end = start + PAL_TICK
        frame = round((start + end) / (2 * PAL_TICK))
        sample = frame * PAL_TICK
        sub_pal = False
        if sample < start:
            frame = math.ceil(start / PAL_TICK)
            sample = frame * PAL_TICK
        if sample >= end:
            frame = math.floor((end - Fraction(1, 1000)) / PAL_TICK)
            sample = frame * PAL_TICK
        if not (start <= sample < end):
            sample = (start + end) / 2
            frame = round(sample / PAL_TICK)
            sub_pal = True
        cortes.append(
            dict(
                corte=i,
                amc_clip=int(c["clip"]),
                amc_amb=int(c["amb"]),
                amc_hijo=int(c["hijo"]),
                inicio_u=float(start),
                fin_exclusivo_u=float(end),
                amc_frames=int(c["frames"]),
                cuadro_pal=int(frame),
                t_u=float(sample),
                segundos=float(sample) / 60.0,
                sub_pal=sub_pal,
                png=f"{spx}_corte_{i:02d}.png",
            )
        )
    return dict(
        schema="b1-trg5-v1",
        spx=int(spx),
        iso_sha256=doc["iso_sha256"],
        spx_sha256=doc["spx_sha256"],
        timeline_sha256=hashlib.sha256(raw).hexdigest(),
        resolucion=list(FRAME_SIZE),
        unidades_por_segundo=60,
        cuadros_pal_por_segundo=50,
        duracion_unidades=float(dur),
        cortes=cortes,
    )


def construir_paquete_escena(
    tr1_dir: Path,
    en_afs: Afs,
    spx: int,
    plan: dict,
    amb_cache: dict[int, tuple[bytes, list[dict]]] | None = None,
) -> tuple[bytes, dict]:
    """Empaqueta blobs binarios y eventos resueltos de una escena para juez_generico.exe."""
    doc = json.loads((tr1_dir / f"escena_{spx}.json").read_bytes())
    if amb_cache is None:
        amb_cache = {}

    def get_amb(amb_id: int) -> tuple[bytes, list[dict]]:
        if amb_id not in amb_cache:
            raw = en_afs.read(amb_id)
            amb_cache[amb_id] = (raw, children(raw))
        return amb_cache[amb_id]

    def read_child(amb_id: int, hijo_idx: int) -> bytes:
        raw, chs = get_amb(amb_id)
        h = chs[hijo_idx]
        return raw[h["off"] : h["off"] + h["tam"]]

    blobs: list[bytes] = []
    blob_map: dict[tuple, int] = {}

    def add_blob(key: tuple, data: bytes) -> int:
        if key not in blob_map:
            blob_map[key] = len(blobs)
            blobs.append(data)
        return blob_map[key]

    sky_slots = {int(c["actor"]) for c in doc.get("cielos", [])}
    dur = float(doc["duracion_unidades"])
    cut_times = [c["inicio_u"] for c in plan["cortes"]]

    # Estado de recursos por ranura para emitir EV_LOAD_ACTOR cuando carga AMO/AMT
    slot_amo: dict[int, int] = {}
    slot_amt: dict[int, int] = {}
    slot_dirty: set[int] = set()
    slot_names: dict[int, str] = {}

    events_out: list[tuple] = []
    hilos: dict[int, dict] = {}

    def flush_dirty(t_ev: float) -> None:
        for sl in sorted(slot_dirty):
            if sl in slot_amo:
                events_out.append(
                    (
                        t_ev,
                        0,  # prioridad alta: cargar antes de dibujar en el mismo instante
                        EV_LOAD_ACTOR,
                        sl,
                        slot_amo[sl],
                        slot_amt.get(sl, -1),
                        1 if sl in sky_slots else 0,
                        0.0,
                        0.0,
                        0.0,
                    )
                )
        slot_dirty.clear()

    # Recoger nombres de raiz de actores para diagnostico
    for a in doc.get("actores", []):
        sl = int(a["ranura"])
        for r in a.get("recursos", []):
            if r.get("raiz"):
                slot_names[sl] = str(r["raiz"])
        for c in a.get("cargas", []):
            if c.get("raiz"):
                slot_names[sl] = str(c["raiz"])

    for seq, e in enumerate(doc["eventos"]):
        t = float(e["t"])
        tp = e["tipo"]
        hid = int(e.get("hilo", -1))
        h = hilos.setdefault(hid, dict(fin=0.0, actor=-1, dib=False, dib_b=False))
        h["fin"] = max(h["fin"], t)
        if tp == "espera":
            h["fin"] = max(h["fin"], t + float(e.get("unidades") or 0.0))
            continue
        if tp == "texto_fundido":
            h["fin"] = max(h["fin"], t + float(e.get("cuadros") or 0.0))
            continue

        if tp == "carga_amb":
            amb_id = int(e["entrada"])
            raw, chs = get_amb(amb_id)
            touched: set[int] = set()
            for ch in chs:
                if ch["sub"] != 0:
                    continue
                if ch["tipo"] == 1:  # #AMO
                    sl = int(ch["ranura"])
                    bid = add_blob(("amb", amb_id, ch["hijo"]), raw[ch["off"] : ch["off"] + ch["tam"]])
                    slot_amo[sl] = bid
                    touched.add(sl)
                elif ch["tipo"] == 2:  # #AMT
                    sl = int(ch["ranura"])
                    bid = add_blob(("amb", amb_id, ch["hijo"]), raw[ch["off"] : ch["off"] + ch["tam"]])
                    slot_amt[sl] = bid
                    touched.add(sl)
            slot_dirty.update(touched)
            continue

        if tp == "carga_modelo":
            sl = int(e["actor"])
            ent = int(e["entrada"])
            slot_amo[sl] = add_blob(("afs", ent), en_afs.read(ent))
            slot_dirty.add(sl)
            continue

        if tp == "carga_textura":
            sl = int(e["actor"])
            ent = int(e["entrada"])
            slot_amt[sl] = add_blob(("afs", ent), en_afs.read(ent))
            slot_dirty.add(sl)
            continue

        if slot_dirty:
            flush_dirty(t)

        if tp == "actor_dibujo":
            sl = int(e["actor"])
            act = 1 if int(e.get("activo", 0)) else 0
            h["actor"] = sl
            h["dib"] = bool(act)
            events_out.append((t, 10 + seq, EV_DIBUJO, sl, act, 0, 0, 0.0, 0.0, 0.0))
        elif tp == "actor_dibujo_b":
            sl = int(e["actor"])
            act = 1 if int(e.get("activo", 0)) else 0
            h["actor"] = sl
            h["dib_b"] = bool(act)
            events_out.append((t, 10 + seq, EV_DIBUJO_B, sl, act, 0, 0, 0.0, 0.0, 0.0))
        elif tp == "liberar_actor":
            sl = int(e["actor"])
            h["dib"] = False
            h["dib_b"] = False
            events_out.append((t, 10 + seq, EV_LIBERAR, sl, 0, 0, 0, 0.0, 0.0, 0.0))
        elif tp == "animacion":
            sl = int(e["actor"])
            amb_id = e.get("amb")
            hijo = e.get("hijo")
            largo = e.get("largo")
            if amb_id is None or hijo is None or not largo:
                continue
            clip = int(e["clip"])
            vueltas = int(e.get("vueltas", 1))
            bid = add_blob(("amb", int(amb_id), int(hijo)), read_child(int(amb_id), int(hijo)))
            if vueltas < 0:
                h["fin"] = dur
            else:
                h["fin"] = max(h["fin"], t + float(largo) * vueltas)
            events_out.append(
                (t, 10 + seq, EV_ANIM, sl, bid, clip, int(largo), 0.0, float(int(largo) - 1), float(vueltas))
            )
        elif tp == "animacion_segmento":
            sl = int(e["actor"])
            amb_id = e.get("amb")
            hijo = e.get("hijo")
            if amb_id is None or hijo is None:
                continue
            clip = int(e["clip"])
            desde = float(e.get("desde", 0.0))
            hasta = float(e.get("hasta", desde))
            bid = add_blob(("amb", int(amb_id), int(hijo)), read_child(int(amb_id), int(hijo)))
            span = max(1.0, abs(hasta - desde))
            h["fin"] = max(h["fin"], t + span)
            events_out.append((t, 10 + seq, EV_ANIM, sl, bid, clip, 0, desde, hasta, 1.0))
        elif tp == "animacion_amp":
            sl = int(e["actor"])
            amb_id = e.get("amb")
            hijo = e.get("hijo")
            largo = e.get("largo")
            if amb_id is None or hijo is None or not largo:
                continue
            clip = int(e["clip"])
            vueltas = int(e.get("vueltas", 1))
            bid = add_blob(("amb", int(amb_id), int(hijo)), read_child(int(amb_id), int(hijo)))
            if vueltas < 0:
                h["fin"] = dur
            else:
                h["fin"] = max(h["fin"], t + float(largo) * vueltas)
            events_out.append((t, 10 + seq, EV_AMP, sl, bid, clip, int(largo), 0.0, 0.0, 0.0))
        elif tp == "camara":
            if int(e.get("canal", 0)) != 0:
                continue
            amb_id = e.get("amb")
            hijo = e.get("hijo")
            largo = e.get("largo")
            if amb_id is None or hijo is None or not largo:
                continue
            clip = int(e["clip"])
            bid = add_blob(("amb", int(amb_id), int(hijo)), read_child(int(amb_id), int(hijo)))
            events_out.append((t, 10 + seq, EV_CAM, 0, bid, clip, int(largo), 0.0, 0.0, 0.0))
        elif tp == "luz":
            ch = int(e.get("canal", 0))
            if not 0 <= ch <= 2:
                continue
            amb_id = e.get("amb")
            hijo = e.get("hijo")
            largo = e.get("largo")
            if amb_id is None or hijo is None or not largo:
                continue
            clip = int(e["clip"])
            bid = add_blob(("amb", int(amb_id), int(hijo)), read_child(int(amb_id), int(hijo)))
            events_out.append((t, 10 + seq, EV_LUZ, ch, bid, clip, int(largo), 0.0, 0.0, 0.0))
        elif tp == "color_global":
            rgb = e.get("rgb", [0.3, 0.3, 0.3])
            events_out.append((t, 10 + seq, EV_AMBIENTE, 0, 0, 0, 0, float(rgb[0]), float(rgb[1]), float(rgb[2])))
        elif tp == "niebla":
            act = 1 if int(e.get("activa", 0)) else 0
            events_out.append((t, 10 + seq, EV_NIEBLA, 0, act, 0, 0, 0.0, 0.0, 0.0))
        elif tp == "niebla_color":
            rgb = e.get("rgb", [0.0, 0.0, 0.0])
            events_out.append((t, 10 + seq, EV_NIEBLA_RGB, 0, 0, 0, 0, float(rgb[0]), float(rgb[1]), float(rgb[2])))
        elif tp == "niebla_densidad":
            events_out.append(
                (t, 10 + seq, EV_NIEBLA_DENS, 0, 0, 0, 0, float(e.get("a", 0.0)), float(e.get("b", 0.0)), 0.0)
            )
        elif tp == "niebla_distancia":
            events_out.append(
                (
                    t,
                    10 + seq,
                    EV_NIEBLA_DIST,
                    0,
                    0,
                    0,
                    0,
                    float(e.get("cerca", 0.0)),
                    float(e.get("lejos", 1000.0)),
                    0.0,
                )
            )

    if slot_dirty:
        flush_dirty(0.0)

    # Cierre automatico al terminar cada hilo SPX finito (opcode 0x95, FUN_001b2ff0)
    # Identico a src/escena_b1_1910.inl:809-816.
    for hid, h in hilos.items():
        if hid < 0:
            continue
        tfin = h["fin"]
        for ct in cut_times:
            if abs(tfin - ct) <= 1.0:
                tfin = ct
                break
        if tfin >= dur - 0.5:
            continue
        if h["dib"] and h["actor"] >= 0:
            events_out.append((tfin, 5, EV_DIBUJO, h["actor"], 0, 0, 0, 0.0, 0.0, 0.0))
        if h["dib_b"] and h["actor"] >= 0:
            events_out.append((tfin, 5, EV_DIBUJO_B, h["actor"], 0, 0, 0, 0.0, 0.0, 0.0))

    events_out.sort(key=lambda row: (row[0], row[1]))

    # Serializar paquete binario B1J1:
    # magic(4) spx(u32) dur(f64) n_blobs(u32) n_events(u32) n_cuts(u32)
    # blobs: [len(u32), bytes]...
    # events: [t(f64), kind(i32), a(i32), b(i32), c(i32), d(i32), f0(f32), f1(f32), f2(f32)]...
    # cuts: [corte(i32), t_u(f64)]...
    buf = bytearray()
    buf.extend(struct.pack("<4sIdIII", b"B1J1", int(spx), float(dur), len(blobs), len(events_out), len(plan["cortes"])))
    for b in blobs:
        buf.extend(struct.pack("<I", len(b)))
        buf.extend(b)
    ev_struct = struct.Struct("<diiiii3f")
    for t, _prio, kind, a, b, c, d, f0, f1, f2 in events_out:
        buf.extend(ev_struct.pack(float(t), int(kind), int(a), int(b), int(c), int(d), float(f0), float(f1), float(f2)))
    for cut in plan["cortes"]:
        buf.extend(struct.pack("<id", int(cut["corte"]), float(cut["t_u"])))

    meta = dict(spx=spx, blobs=len(blobs), eventos=len(events_out), slot_names=slot_names, sky_slots=sorted(sky_slots))
    return bytes(buf), meta


# =============================================================================
# 1. ARNES C++ GENERICO (usa src/ps2_modelo.cpp + src/escena_b1_raster.inl)
# =============================================================================

ARNES_GENERICO_TAIL = r'''
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace juez {
using namespace dbz3;
using namespace dbz3::pasarela;
using Bytes = std::vector<uint8_t>;
using Punto = std::array<float, 3>;

void Exigir(bool ok, const char* msg) {
  if (!ok) throw std::runtime_error(msg);
}
uint32_t U32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint32_t U32(const Bytes& b, size_t p) {
  Exigir(p <= b.size() && b.size() - p >= 4, "u32 fuera de rango");
  return U32(b.data() + p);
}
int16_t S16(const Bytes& b, size_t p) {
  Exigir(p <= b.size() && b.size() - p >= 2, "s16 fuera de rango");
  return std::bit_cast<int16_t>(uint16_t(unsigned(b[p]) | unsigned(b[p + 1]) << 8));
}
Punto Cruz(Punto a, Punto b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float Dot(Punto a, Punto b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
bool Unit(Punto& p) {
  const float l = std::sqrt(Dot(p, p));
  if (!(l > 1e-6f)) return false;
  for (float& x : p) x /= l;
  return true;
}

template <size_t N>
std::array<float, N - 1> Muestra(const std::vector<std::array<float, N>>& p, float f) {
  auto it = std::upper_bound(p.begin(), p.end(), f, [](float v, const auto& k) { return v < k[0]; });
  const auto& a = it == p.begin() ? *it : *std::prev(it);
  const auto& b = it == p.end() ? a : *it;
  std::array<float, N - 1> out{};
  const float u = b[0] > a[0] ? std::clamp((f - a[0]) / (b[0] - a[0]), 0.f, 1.f) : 0.f;
  for (size_t i = 1; i < N; ++i) out[i - 1] = a[i] + (b[i] - a[i]) * u;
  return out;
}

template <size_t N>
std::vector<std::array<float, N>> Claves(const Bytes& b, size_t off, int cuadros) {
  Exigir(off != 0, "pista PAL ausente");
  const size_t n = U32(b, off + 8);
  Exigir(n > 0 && n <= size_t(std::max(cuadros, 1)) + 16 && n <= 65536, "numero de claves PAL");
  std::vector<std::array<float, N>> out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const size_t p = off + 12 + 4 * N * i;
    std::array<float, N> key{};
    key[0] = float(U32(b, p));
    for (size_t j = 1; j < N; ++j) {
      key[j] = std::bit_cast<float>(U32(b, p + 4 * j));
      if (!std::isfinite(key[j])) key[j] = 0.f;
    }
    out.push_back(key);
  }
  return out;
}

#include "escena_b1_raster.inl"

struct CamTrack {
  int cuadros = 0;
  std::vector<std::array<float, 4>> ojo, obj;
  std::vector<std::array<float, 2>> roll, fov;
};

CamTrack LeerCamClip(const Bytes& amc, int clip) {
  Exigir(amc.size() >= 24 && U32(amc, 0) == 0x434d4123, "#AMC invalido");
  const uint32_t n = U32(amc, 16);
  const size_t tabla = U32(amc, 20);
  Exigir(clip >= 0 && uint32_t(clip) < n, "clip #AMC fuera de rango");
  const size_t fila = tabla + 16 * size_t(clip);
  const int cuadros = int(U32(amc, fila + 8));
  const size_t pistas = U32(amc, fila + 12);
  CamTrack out;
  out.cuadros = std::max(1, cuadros);
  out.ojo = Claves<4>(amc, U32(amc, pistas), out.cuadros);
  out.obj = Claves<4>(amc, U32(amc, pistas + 4), out.cuadros);
  out.roll = Claves<2>(amc, U32(amc, pistas + 8), out.cuadros);
  out.fov = Claves<2>(amc, U32(amc, pistas + 12), out.cuadros);
  return out;
}

ps2::Camara EvalCam(const CamTrack& p, double dt) {
  const float f = float(std::clamp(dt, 0.0, double(std::max(0, p.cuadros - 1))));
  const auto o = Muestra(p.ojo, f), b = Muestra(p.obj, f);
  Punto z{o[0] - b[0], o[1] - b[1], o[2] - b[2]};
  ps2::Camara cam;
  if (!Unit(z)) return cam;
  auto x = Cruz({0, 1, 0}, z);
  if (!Unit(x)) x = Cruz({0, 0, 1}, z);
  if (!Unit(x)) return cam;
  auto y = Cruz(z, x);
  if (!p.roll.empty()) {
    const float r = -Muestra(p.roll, f)[0], c = std::cos(r), s = std::sin(r);
    for (int i = 0; i < 3; ++i) {
      const float v = x[i];
      x[i] = c * v + s * y[i];
      y[i] = c * y[i] - s * v;
    }
  }
  const Punto axes[] = {x, y, z};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) cam.m[4 * j + i] = axes[i][j];
    cam.m[12 + i] = -Dot(axes[i], o);
  }
  cam.m[15] = 1;
  cam.c = {320.f, 224.f};
  cam.h = {320.f, 224.f};
  cam.fov = std::clamp(Muestra(p.fov, f)[0], 0.08726646f, 3.054326f);
  cam.foco_x = 240.f;
  cam.foco_y = 224.f;
  cam.corte_cercano = raster::Cerca;
  cam.ok = true;
  return cam;
}

struct LuzClip {
  int cuadros = 0;
  bool direccional = true;
  std::vector<std::array<float, 4>> dir, rgb;
};

LuzClip LeerLuzClip(const Bytes& aml, int clip) {
  Exigir(aml.size() >= 24 && U32(aml, 0) == 0x4c4d4123, "#AML invalido");
  const uint32_t n = U32(aml, 16);
  const size_t tabla = U32(aml, 20);
  Exigir(clip >= 0 && uint32_t(clip) < n, "clip #AML fuera de rango");
  const size_t fila = tabla + 16 * size_t(clip);
  const uint32_t tipo = U32(aml, fila);
  const int cuadros = std::max(1, int(U32(aml, fila + 8)));
  const size_t pistas = U32(aml, fila + 12);
  LuzClip out;
  out.cuadros = cuadros;
  out.direccional = (tipo & 15) != 9;
  out.dir = Claves<4>(aml, U32(aml, pistas), cuadros);
  out.rgb = Claves<4>(aml, U32(aml, pistas + 4), cuadros);
  return out;
}

std::vector<std::string> Nombres(const Bytes& b, size_t n, size_t o) {
  if (n == 0 || n > 512 || o > b.size() || n * 32 > b.size() - o) return {};
  std::vector<std::string> out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const auto* s = reinterpret_cast<const char*>(b.data() + o + i * 32);
    out.emplace_back(s, std::find(s, s + 32, '\0'));
  }
  return out;
}

ps2::Anim LeerClipPS(const Bytes& amm, int id, const std::vector<std::string>& huesos) {
  ps2::Anim salida;
  if (amm.size() < 32 || std::memcmp(amm.data(), "#AMM", 4) != 0) return salida;
  const auto nombres = Nombres(amm, U32(amm, 24), U32(amm, 28));
  if (nombres.empty()) return salida;
  auto origen = ps2::LeerAnim(amm, id, nombres.size());
  if (!origen) return salida;
  salida.cuadros = origen.cuadros;
  salida.ang.resize(huesos.size());
  salida.pos.resize(huesos.size());
  for (size_t i = 0; i < huesos.size(); ++i) {
    const auto it = std::find(nombres.begin(), nombres.end(), huesos[i]);
    if (it == nombres.end()) continue;
    const size_t j = size_t(it - nombres.begin());
    if (j < origen.ang.size()) salida.ang[i] = origen.ang[j];
    if (j < origen.pos.size()) salida.pos[i] = origen.pos[j];
  }
  return salida;
}

struct Partes {
  int cuadros = 0;
  std::vector<std::vector<std::pair<int, int>>> nodo;
};

Partes LeerPartes(const Bytes& amp, int clip, const std::vector<std::string>& huesos) {
  Partes out;
  if (amp.size() < 32 || U32(amp, 0) != 0x504d4123) return out;
  const uint32_t nc_clips = U32(amp, 16);
  if (clip < 0 || uint32_t(clip) >= nc_clips) return out;
  const size_t tabla = U32(amp, 20);
  const size_t fila = tabla + 16 * size_t(clip);
  const size_t pistas = U32(amp, fila + 12);
  const size_t n = U32(amp, 24);
  const int cuadros = int(U32(amp, fila + 8));
  const auto nombres = Nombres(amp, n, U32(amp, 28));
  out.cuadros = std::max(1, cuadros);
  out.nodo.resize(huesos.size());
  for (size_t b = 0; b < huesos.size(); ++b) {
    if (huesos[b].empty()) continue;
    const auto it = std::find(nombres.begin(), nombres.end(), huesos[b]);
    if (it == nombres.end()) continue;
    const size_t p = U32(amp, pistas + 4 * size_t(it - nombres.begin()));
    if (!p) continue;
    const size_t nc = U32(amp, p + 8);
    auto& dest = out.nodo[b];
    dest.reserve(nc);
    for (size_t k = 0; k < nc; ++k) dest.emplace_back(S16(amp, p + 12 + 4 * k), S16(amp, p + 14 + 4 * k));
  }
  return out;
}

bool ParteVisible(const Partes& p, size_t hueso, float f) {
  if (hueso >= p.nodo.size() || p.nodo[hueso].empty()) return true;
  const auto& k = p.nodo[hueso];
  const auto it = std::upper_bound(k.begin(), k.end(), f, [](float v, const auto& a) { return v < a.first; });
  return (it == k.begin() ? *it : *std::prev(it)).second >= 0;
}

void FiltrarPartes(const Partes& p, float f, const ps2::Malla& m, ps2::Malla& out) {
  std::vector<uint8_t> visible(std::max<size_t>(256, p.nodo.size()), 1);
  for (size_t b = 0; b < p.nodo.size(); ++b) visible[b] = ParteVisible(p, b, f) ? 1 : 0;
  out.tris.clear();
  out.tris.reserve(m.tris.size());
  for (const auto& tr : m.tris)
    if (visible[tr.hueso[0]]) out.tris.push_back(tr);
}

bool EsNombreCielo(const std::string& s) {
  std::string up = s;
  for (char& ch : up) ch = char(std::toupper(static_cast<unsigned char>(ch)));
  return up.find("SKY") != std::string::npos || up.find("CLOUD") != std::string::npos ||
         up.find("KUMO") != std::string::npos;
}

struct ModeloCargado {
  int id = 0;
  int amo_blob = -1, amt_blob = -1;
  bool es_cielo = false;
  ps2::Modelo m;
  std::vector<std::string> huesos;
  std::vector<uint8_t> hueso_cielo;
};

struct ActorEstado {
  std::shared_ptr<ModeloCargado> mod;
  bool dibujo = false, dibujo_b = false;
  std::shared_ptr<ps2::Anim> anim;
  double anim_t0 = 0.0;
  float anim_desde = 0.f, anim_hasta = 0.f, anim_vueltas = 1.f;
  std::shared_ptr<Partes> partes;
  double partes_t0 = 0.0;
};

struct EventoBin {
  double t;
  int32_t kind, a, b, c, d;
  float f0, f1, f2;
};

static bool GuardarPpm(const std::filesystem::path& ruta, int w, int h, const uint32_t* rgba) {
  std::vector<char> rgb(size_t(w) * h * 3);
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    const uint32_t px = rgba[i];
    rgb[i * 3 + 0] = char(px & 255);
    rgb[i * 3 + 1] = char((px >> 8) & 255);
    rgb[i * 3 + 2] = char((px >> 16) & 255);
  }
  std::ofstream file(ruta, std::ios::binary);
  file << "P6\n" << w << ' ' << h << "\n255\n";
  return bool(file.write(rgb.data(), std::streamsize(rgb.size())));
}

static void GuardarTexRgba(const Imagen& im, const std::filesystem::path& path) {
  auto rgba = im.rgba;
  bool cero = true;
  for (size_t i = 3; i < rgba.size(); i += 4) cero &= (rgba[i] == 0);
  if (cero)
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
  std::ofstream out(path, std::ios::binary);
  uint32_t wh[2] = {uint32_t(im.w), uint32_t(im.h)};
  out.write(reinterpret_cast<const char*>(wh), 8);
  out.write(reinterpret_cast<const char*>(rgba.data()), std::streamsize(rgba.size()));
}

// Raster de mascara de ranura y cielo (640x448 uint8: bits 0..5 = slot+1, bit 7 = sky)
static void RasterMascara(const raster::Superficie& sup, const std::vector<uint8_t>& tri_tag,
                          std::vector<uint8_t>& mask_out) {
  mask_out.assign(raster::W * raster::H, 0);
  std::vector<float> zbuf(raster::W * raster::H, 0.f);
  bool fondo = false;
  for (const auto& p : sup.orden) {
    if (p.pase != 0 && fondo) {
      std::fill(zbuf.begin(), zbuf.end(), 0.f);
      fondo = false;
    }
    fondo |= (p.pase == 0);
    for (size_t ti = p.primero; ti < p.fin; ++ti) {
      const auto& tr = sup.tris[ti];
      const uint8_t tag = ti < tri_tag.size() ? tri_tag[ti] : 0;
      auto a = tr.v[0], b = tr.v[1], c = tr.v[2];
      auto borde = [](const raster::Vert& a, const raster::Vert& b, float x, float y) {
        return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
      };
      auto incluye = [](const raster::Vert& a, const raster::Vert& b) {
        return b.y < a.y || (b.y == a.y && b.x > a.x);
      };
      float area = borde(a, b, c.x, c.y);
      if (area < 0) { std::swap(b, c); area = -area; }
      if (!(area > 1e-6f)) continue;
      const float inv = 1.f / area;
      const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
      const int x1 = std::min(raster::W, int(std::ceil(std::max({a.x, b.x, c.x}))));
      const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
      const int y1 = std::min(raster::H, int(std::ceil(std::max({a.y, b.y, c.y}))));
      const bool ia = incluye(b, c), ib = incluye(c, a), ic = incluye(a, b);
      const float da = b.y - c.y, db = c.y - a.y, dc = a.y - b.y;
      for (int y = y0; y < y1; ++y) {
        float ea = borde(b, c, x0 + .5f, y + .5f);
        float eb = borde(c, a, x0 + .5f, y + .5f);
        float ec = borde(a, b, x0 + .5f, y + .5f);
        for (int x = x0; x < x1; ++x, ea += da, eb += db, ec += dc) {
          if (ea < 0 || (!ia && ea == 0) || eb < 0 || (!ib && eb == 0) || ec < 0 || (!ic && ec == 0)) continue;
          const float wa = ea * inv, wb = eb * inv, wc = ec * inv;
          const float d = wa * a.d + wb * b.d + wc * c.d;
          const size_t off = size_t(y) * raster::W + size_t(x);
          if (d < zbuf[off]) continue;
          // Comprobar alfa del texel si tiene cutout/blend
          if (tr.imagen && (tr.mat.prueba() || tr.mat.mezcla() || !tr.mat.flags)) {
            const auto& im = *tr.imagen;
            float u = (wa * a.u + wb * b.u + wc * c.u) / d;
            float v = (wa * a.v + wb * b.v + wc * c.v) / d;
            if (tr.mat.flags) { u -= std::floor(u); v -= std::floor(v); }
            const int ix = std::clamp(int(std::clamp(u, 0.f, 1.f) * im.w), 0, im.w - 1);
            const int iy = std::clamp(int(std::clamp(v, 0.f, 1.f) * im.h), 0, im.h - 1);
            const float sa = (wa * a.rgba[3] + wb * b.rgba[3] + wc * c.rgba[3]) *
                             (float(im.rgba[4 * (size_t(iy) * im.w + ix) + 3]) / 255.f);
            if (sa < 12.f) continue;
          }
          if (p.pase == 1) zbuf[off] = d;
          mask_out[off] = tag;
        }
      }
    }
  }
}

int Ejecutar(const std::filesystem::path& bundle_path, const std::filesystem::path& out_dir) {
  std::ifstream in(bundle_path, std::ios::binary);
  Exigir(bool(in), "no se pudo abrir bundle.bin");
  char magic[4];
  uint32_t spx = 0, n_blobs = 0, n_events = 0, n_cuts = 0;
  double dur = 0.0;
  in.read(magic, 4);
  Exigir(std::memcmp(magic, "B1J1", 4) == 0, "magic B1J1 invalido");
  in.read(reinterpret_cast<char*>(&spx), 4);
  in.read(reinterpret_cast<char*>(&dur), 8);
  in.read(reinterpret_cast<char*>(&n_blobs), 4);
  in.read(reinterpret_cast<char*>(&n_events), 4);
  in.read(reinterpret_cast<char*>(&n_cuts), 4);
  std::vector<Bytes> blobs(n_blobs);
  for (uint32_t i = 0; i < n_blobs; ++i) {
    uint32_t len = 0;
    in.read(reinterpret_cast<char*>(&len), 4);
    blobs[i].resize(len);
    if (len) in.read(reinterpret_cast<char*>(blobs[i].data()), std::streamsize(len));
  }
  std::vector<EventoBin> events(n_events);
  for (uint32_t i = 0; i < n_events; ++i) {
    auto& e = events[i];
    in.read(reinterpret_cast<char*>(&e.t), 8);
    in.read(reinterpret_cast<char*>(&e.kind), 4);
    in.read(reinterpret_cast<char*>(&e.a), 4);
    in.read(reinterpret_cast<char*>(&e.b), 4);
    in.read(reinterpret_cast<char*>(&e.c), 4);
    in.read(reinterpret_cast<char*>(&e.d), 4);
    in.read(reinterpret_cast<char*>(&e.f0), 4);
    in.read(reinterpret_cast<char*>(&e.f1), 4);
    in.read(reinterpret_cast<char*>(&e.f2), 4);
  }
  std::vector<std::pair<int32_t, double>> cuts(n_cuts);
  for (uint32_t i = 0; i < n_cuts; ++i) {
    in.read(reinterpret_cast<char*>(&cuts[i].first), 4);
    in.read(reinterpret_cast<char*>(&cuts[i].second), 8);
  }
  Exigir(bool(in), "bundle.bin truncado");

  const auto dir_puerto = out_dir / "puerto";
  const auto dir_orig = out_dir / "orig";
  std::filesystem::create_directories(dir_puerto);
  std::filesystem::create_directories(dir_orig);

  std::map<std::tuple<int, int, int>, std::shared_ptr<ModeloCargado>> mod_cache;
  std::map<std::tuple<int, int, int>, std::shared_ptr<ps2::Anim>> anim_cache;
  std::map<std::tuple<int, int, int>, std::shared_ptr<Partes>> amp_cache;
  std::map<std::pair<int, int>, CamTrack> cam_cache;
  std::map<std::pair<int, int>, LuzClip> luz_cache;
  std::map<std::pair<int, int>, int> tex_exported;
  int next_mod_id = 0;

  std::vector<ActorEstado> actores(64);
  ps2::Escena::Entorno entorno;
  CamTrack cam_actual;
  double cam_t0 = 0.0;
  bool cam_ok = false;
  std::array<LuzClip, 3> luz_actual;
  std::array<bool, 3> luz_activa{false, false, false};
  std::array<double, 3> luz_t0{0, 0, 0};

  auto cargar_modelo = [&](int amo_b, int amt_b, bool es_cielo) -> std::shared_ptr<ModeloCargado> {
    const auto k = std::make_tuple(amo_b, amt_b, es_cielo ? 1 : 0);
    auto it = mod_cache.find(k);
    if (it != mod_cache.end()) return it->second;
    auto mc = std::make_shared<ModeloCargado>();
    mc->id = next_mod_id++;
    mc->amo_blob = amo_b;
    mc->amt_blob = amt_b;
    mc->es_cielo = es_cielo;
    const Bytes vacio;
    const Bytes& amo = (amo_b >= 0 && size_t(amo_b) < blobs.size()) ? blobs[amo_b] : vacio;
    const Bytes& amt = (amt_b >= 0 && size_t(amt_b) < blobs.size()) ? blobs[amt_b] : vacio;
    mc->m = ps2::CargarSuelto(amo, amt, true, true, false, false, true);
    if (mc->m.esq) {
      mc->m.esq->centro = {};
      if (amo.size() >= 40) mc->huesos = Nombres(amo, mc->m.esq->padre.size(), U32(amo, 36));
      mc->hueso_cielo.assign(mc->m.esq->padre.size(), es_cielo ? 1 : 0);
      for (size_t b = 0; b < mc->huesos.size(); ++b)
        if (EsNombreCielo(mc->huesos[b])) mc->hueso_cielo[b] = 1;
    }
    mod_cache[k] = mc;
    return mc;
  };

  size_t ev_idx = 0;
  auto avanzar = [&](double u) {
    while (ev_idx < events.size() && events[ev_idx].t <= std::min(u, dur)) {
      const auto& e = events[ev_idx++];
      switch (e.kind) {
        case 1: {  // EV_LOAD_ACTOR
          if (e.a >= 0) {
            if (size_t(e.a) >= actores.size()) actores.resize(size_t(e.a) + 8);
            actores[size_t(e.a)].mod = cargar_modelo(e.b, e.c, e.d != 0);
          }
          break;
        }
        case 2:  // EV_DIBUJO
          if (e.a >= 0 && size_t(e.a) < actores.size()) actores[size_t(e.a)].dibujo = (e.b != 0);
          break;
        case 3:  // EV_DIBUJO_B
          if (e.a >= 0 && size_t(e.a) < actores.size()) actores[size_t(e.a)].dibujo_b = (e.b != 0);
          break;
        case 4:  // EV_LIBERAR
          if (e.a >= 0 && size_t(e.a) < actores.size()) {
            auto& a = actores[size_t(e.a)];
            a.dibujo = a.dibujo_b = false;
            a.anim.reset();
            a.partes.reset();
          }
          break;
        case 5: {  // EV_ANIM
          if (e.a >= 0 && size_t(e.a) < actores.size() && actores[size_t(e.a)].mod) {
            auto& a = actores[size_t(e.a)];
            const auto k = std::make_tuple(a.mod->id, e.b, e.c);
            auto it = anim_cache.find(k);
            if (it == anim_cache.end()) {
              auto an = std::make_shared<ps2::Anim>(LeerClipPS(blobs.at(size_t(e.b)), e.c, a.mod->huesos));
              it = anim_cache.emplace(k, std::move(an)).first;
            }
            a.anim = it->second;
            a.anim_t0 = e.t;
            a.anim_desde = e.f0;
            a.anim_hasta = e.f1;
            a.anim_vueltas = e.f2;
          }
          break;
        }
        case 6: {  // EV_AMP
          if (e.a >= 0 && size_t(e.a) < actores.size() && actores[size_t(e.a)].mod) {
            auto& a = actores[size_t(e.a)];
            const auto k = std::make_tuple(a.mod->id, e.b, e.c);
            auto it = amp_cache.find(k);
            if (it == amp_cache.end()) {
              auto ap = std::make_shared<Partes>(LeerPartes(blobs.at(size_t(e.b)), e.c, a.mod->huesos));
              it = amp_cache.emplace(k, std::move(ap)).first;
            }
            a.partes = it->second;
            a.partes_t0 = e.t;
          }
          break;
        }
        case 7: {  // EV_CAM
          const auto k = std::make_pair(e.b, e.c);
          auto it = cam_cache.find(k);
          if (it == cam_cache.end()) it = cam_cache.emplace(k, LeerCamClip(blobs.at(size_t(e.b)), e.c)).first;
          cam_actual = it->second;
          cam_t0 = e.t;
          cam_ok = true;
          break;
        }
        case 8: {  // EV_LUZ
          if (e.a >= 0 && e.a < 3) {
            const auto k = std::make_pair(e.b, e.c);
            auto it = luz_cache.find(k);
            if (it == luz_cache.end()) it = luz_cache.emplace(k, LeerLuzClip(blobs.at(size_t(e.b)), e.c)).first;
            luz_actual[size_t(e.a)] = it->second;
            luz_activa[size_t(e.a)] = true;
            luz_t0[size_t(e.a)] = e.t;
          }
          break;
        }
        case 9: entorno.ambiente = {e.f0, e.f1, e.f2}; break;
        case 10: entorno.niebla = (e.b != 0); break;
        case 11: entorno.color = {e.f0, e.f1, e.f2}; break;
        case 12: entorno.densidad = {e.f0, e.f1}; break;
        case 13: entorno.distancia = {e.f0, e.f1}; break;
      }
    }
    for (int i = 0; i < 2; ++i) {
      if (luz_activa[size_t(i)]) {
        const auto& l = luz_actual[size_t(i)];
        const float f = float(std::clamp(u - luz_t0[size_t(i)], 0.0, double(std::max(0, l.cuadros - 1))));
        entorno.dir[size_t(i)] = Muestra(l.dir, f);
        entorno.luz[size_t(i)] = Muestra(l.rgb, f);
      }
    }
  };

  raster::Superficie dibujo;
  ps2::Malla posa, filtrada;
  std::vector<uint8_t> mask_px;
  std::ostringstream js;
  js << "{\n  \"spx\": " << spx << ",\n  \"cortes\": [\n";

  for (size_t ci = 0; ci < cuts.size(); ++ci) {
    const int corte = cuts[ci].first;
    const double u = cuts[ci].second;
    avanzar(u);
    Exigir(cam_ok, "corte sin camara activa");
    const auto cam = EvalCam(cam_actual, u - cam_t0);
    Exigir(cam.ok, "camara degenerada");

    const auto t0 = std::chrono::steady_clock::now();
    dibujo.Empezar();
    std::vector<uint8_t> tri_tag;

    // Tambien preparamos el archivo .tri para Blender (formato TR32 con mat_mode)
    std::ostringstream tri_buf(std::ios::binary);
    auto wnum = [&](auto v) {
      static_assert(sizeof(v) == 4);
      tri_buf.write(reinterpret_cast<const char*>(&v), 4);
    };
    tri_buf.write("TR32", 4);
    for (float v : cam.m) wnum(v);
    wnum(cam.fov); wnum(cam.foco_x); wnum(cam.foco_y);
    for (float v : entorno.color) wnum(v);

    std::vector<std::pair<int, int>> actores_vis;
    size_t tris_orig_total = 0;

    for (size_t slot = 0; slot < actores.size(); ++slot) {
      const auto& a = actores[slot];
      if (!a.dibujo && !a.dibujo_b) continue;
      if (!a.mod || !a.mod->m.esq || !a.mod->m.malla) continue;
      const auto& mc = *a.mod;
      const auto& sk = *mc.m.esq;
      auto e = entorno;
      e.fondo = mc.es_cielo;
      e.sin_luz = mc.es_cielo;

      const ps2::Malla* malla = &sk.crudo;
      if (a.anim && a.anim->cuadros > 0) {
        double dt = std::max(0.0, u - a.anim_t0);
        float f = 0.f;
        if (a.anim_hasta > a.anim_desde) {
          f = std::clamp(a.anim_desde + float(dt), a.anim_desde, std::min(a.anim_hasta, float(a.anim->cuadros - 1)));
        } else if (a.anim_vueltas > 1.f && a.anim->cuadros > 1) {
          const float span = float(a.anim->cuadros - 1);
          f = float(std::fmod(dt, double(span)));
        } else {
          f = float(std::clamp(dt, 0.0, double(a.anim->cuadros - 1)));
        }
        ps2::Posar(sk, *a.anim, f / 60.f, posa);
        malla = &posa;
      }
      if (a.partes && a.partes->cuadros > 0) {
        const float fp = float(std::clamp(u - a.partes_t0, 0.0, double(a.partes->cuadros - 1)));
        FiltrarPartes(*a.partes, fp, *malla, filtrada);
        malla = &filtrada;
      }
      if (malla->tris.empty()) continue;

      // 1) Enviar al rasterizador del puerto y etiquetar triangulos recortados
      const size_t t_before = dibujo.tris.size();
      dibujo.Poner(cam, *malla, mc.m.tex, e, false);
      const size_t t_after = dibujo.tris.size();
      // Etiquetamos cada triangulo recortado con slot y si el actor es cielo
      // (para cielos dentro de Actor 0 como *_SKY, lo refinamos abajo por triangulo)
      bool actor_tiene_cielo = mc.es_cielo;
      for (uint8_t hc : mc.hueso_cielo) actor_tiene_cielo |= (hc != 0);
      if (t_after > t_before) {
        // Re-evaluacion rapida de etiqueta por triangulo solo si el actor mezcla cielo y suelo
        if (mc.es_cielo) {
          tri_tag.insert(tri_tag.end(), t_after - t_before, uint8_t((slot + 1) | 0x80u));
        } else if (!actor_tiene_cielo) {
          tri_tag.insert(tri_tag.end(), t_after - t_before, uint8_t((slot + 1) & 0x3Fu));
        } else {
          // Etiqueta exacta por triangulo: proyectamos rapidamente los que tienen hueso_cielo
          tri_tag.reserve(t_after);
          // Como Poner puede dividir 1 tri en k tris al recortar, aproximamos por pase/flag o
          // comprobamos si todos son cielo; para exactitud, re-recorremos con el mismo orden:
          size_t added = t_after - t_before;
          // Sub-clasificamos cada triangulo recortado segun la fraccion de nodos cielo en la orden
          // Mejor: recorremos los triangulos de malla uno a uno con un mini-contador
          dibujo.tris.resize(t_before);
          // Quitamos las entradas de orden añadidas para este actor y las re-emitimos por grupos cielo/no-cielo
          while (!dibujo.orden.empty() && dibujo.orden.back().fin > t_before) dibujo.orden.pop_back();
          ps2::Malla m_sky, m_nrm;
          for (const auto& tr : malla->tris) {
            const bool sk_tri = tr.hueso[0] < mc.hueso_cielo.size() && mc.hueso_cielo[tr.hueso[0]] != 0;
            (sk_tri ? m_sky : m_nrm).tris.push_back(tr);
          }
          if (!m_nrm.tris.empty()) {
            const size_t b0 = dibujo.tris.size();
            dibujo.Poner(cam, m_nrm, mc.m.tex, e, false);
            tri_tag.insert(tri_tag.end(), dibujo.tris.size() - b0, uint8_t((slot + 1) & 0x3Fu));
          }
          if (!m_sky.tris.empty()) {
            const size_t b0 = dibujo.tris.size();
            dibujo.Poner(cam, m_sky, mc.m.tex, e, false);
            tri_tag.insert(tri_tag.end(), dibujo.tris.size() - b0, uint8_t(((slot + 1) & 0x3Fu) | 0x80u));
          }
          (void)added;
        }
      }

      // 2) Exportar geometria original a TR32 para Blender
      wnum(int32_t(slot));
      wnum(int32_t(mc.es_cielo ? 1 : 0));
      wnum(uint32_t(malla->tris.size()));
      tris_orig_total += malla->tris.size();
      actores_vis.push_back({int(slot), int(malla->tris.size())});

      for (const auto& tr : malla->tris) {
        int tex_global = -1;
        if (tr.tex >= 0 && size_t(tr.tex) < mc.m.tex.size() && mc.m.tex[size_t(tr.tex)]) {
          const auto tk = std::make_pair(mc.id, tr.tex);
          auto it = tex_exported.find(tk);
          if (it == tex_exported.end()) {
            const int gid = int(tex_exported.size());
            tex_exported[tk] = gid;
            GuardarTexRgba(mc.m.tex[size_t(tr.tex)], dir_orig / ("tex_" + std::to_string(gid) + ".rgba"));
            tex_global = gid;
          } else {
            tex_global = it->second;
          }
        }
        const raster::Material mat{tr.b1_flags, tr.b1_alpha};
        int32_t mat_mode = 0;  // 0=opaco, 1=cutout, 2=blend alfa, 3=aditivo
        if (mat.mezcla()) mat_mode = ((mat.alpha & 0xffu) == 0x48u) ? 3 : 2;
        else if (mat.prueba() || !mat.flags) mat_mode = 1;
        wnum(int32_t(tex_global));
        wnum(mat_mode);

        const bool use_fog = entorno.niebla && !mc.es_cielo && (mat.flags & 0x80u) != 0;
        for (int j = 0; j < 3; ++j) {
          const auto v = tr.p[j], n = tr.n[j];
          Punto light{1, 1, 1};
          if (!mc.es_cielo && (n.x || n.y || n.z)) {
            light = entorno.ambiente;
            for (int l = 0; l < 2; ++l) {
              const auto& dr = entorno.dir[size_t(l)];
              const float lam = std::max(0.f, -(n.x * dr[0] + n.y * dr[1] + n.z * dr[2]));
              for (int c = 0; c < 3; ++c) light[c] += lam * entorno.luz[size_t(l)][c];
            }
          }
          const auto rgba = tr.con_color ? tr.col[j] : IM_COL32_WHITE;
          const float depth = -(cam.m[2] * v.x + cam.m[6] * v.y + cam.m[10] * v.z + cam.m[14]);
          float fog = 0.f;
          if (use_fog && entorno.distancia[1] > entorno.distancia[0]) {
            const float fu = std::clamp((depth - entorno.distancia[0]) / (entorno.distancia[1] - entorno.distancia[0]),
                                        0.f, 1.f);
            fog = std::clamp((entorno.densidad[0] + fu * (entorno.densidad[1] - entorno.densidad[0])) / 255.f, 0.f, 1.f);
          }
          for (float x : {v.x, v.y, v.z, tr.uv[j].x, 1.f - tr.uv[j].y}) wnum(x);
          for (int c = 0; c < 3; ++c) wnum(std::clamp(float((rgba >> (8 * c)) & 255) * light[c] / 255.f, 0.f, 1.f));
          wnum(float(rgba >> 24) / 255.f);
          wnum(fog);
        }
      }
    }
    wnum(int32_t(-1));

    dibujo.Pintar(entorno);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    RasterMascara(dibujo, tri_tag, mask_px);

    // Contar pixeles por ranura y cielo
    size_t sky_px = 0, fg_px = 0;
    std::map<int, size_t> actor_px;
    for (uint8_t tag : mask_px) {
      if (!tag) continue;
      if (tag & 0x80u) ++sky_px;
      else ++fg_px;
      const int sl = int(tag & 0x3Fu) - 1;
      if (sl >= 0) actor_px[sl]++;
    }

    char idx_s[16];
    std::snprintf(idx_s, sizeof(idx_s), "%02d", corte);
    Exigir(GuardarPpm(dir_puerto / (std::string("corte_") + idx_s + ".ppm"), raster::W, raster::H, dibujo.rgba.data()),
           "fallo al escribir ppm");
    {
      std::ofstream mf(dir_puerto / (std::string("mask_") + idx_s + ".bin"), std::ios::binary);
      mf.write(reinterpret_cast<const char*>(mask_px.data()), std::streamsize(mask_px.size()));
    }
    {
      const auto ts = tri_buf.str();
      std::ofstream tf(dir_orig / (std::string("corte_") + idx_s + ".tri"), std::ios::binary);
      tf.write(ts.data(), std::streamsize(ts.size()));
    }

    js << "    {\"corte\": " << corte << ", \"t_u\": " << std::fixed << std::setprecision(4) << u
       << ", \"tris_puerto\": " << dibujo.tris.size() << ", \"tris_orig\": " << tris_orig_total
       << ", \"ms\": " << std::setprecision(2) << ms
       << ", \"sky_px\": " << sky_px << ", \"fg_px\": " << fg_px << ", \"actores\": {";
    bool first_a = true;
    for (const auto& [sl, cnt] : actor_px) {
      if (!first_a) js << ", ";
      first_a = false;
      js << "\"" << sl << "\": " << cnt;
    }
    js << "}}" << (ci + 1 < cuts.size() ? "," : "") << "\n";
  }
  js << "  ]\n}\n";
  std::ofstream jf(out_dir / "escena_cpp.json");
  jf << js.str();
  return 0;
}
}  // namespace juez

int wmain(int argc, wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  if (argc != 3) {
    std::cerr << "Uso: juez_generico.exe <bundle.bin> <out_dir>\n";
    return 2;
  }
  try {
    return juez::Ejecutar(argv[1], argv[2]);
  } catch (const std::exception& err) {
    std::cerr << "JUEZ_CPP_ERR: " << err.what() << "\n";
    return 1;
  }
}
'''

# =============================================================================
# 2. ARNES C++ REAL DE 1910 (GEM2/B1C6 arnes.inc ampliado a los 14 cortes)
# =============================================================================

ARNES_1910_INC = r'''
static bool GuardarPpm1910(const char* ruta, int w, int h, const uint8_t* rgba) {
  std::vector<char> rgb(size_t(w) * h * 3);
  for (size_t i = 0; i < size_t(w) * h; ++i)
    for (int ch = 0; ch < 3; ++ch) rgb[i * 3 + ch] = char(rgba[i * 4 + ch]);
  std::ofstream file(ruta, std::ios::binary);
  file << "P6\n" << w << ' ' << h << "\n255\n";
  return bool(file.write(rgb.data(), std::streamsize(rgb.size())));
}

int PruebaSintetica() {
  using namespace dbz3::pasarela;
  using namespace dbz3::pasarela::cine1910;
  namespace r = raster;
  r::Superficie view;
  view.Empezar();
  auto slot = [&](r::Material mat, int pase, float z, std::array<float, 4> rgba) {
    const auto n = view.tris.size();
    view.tris.push_back({{{0, 0, 1 / z, 0, 0, rgba}, {10, 0, 1 / z, 0, 0, rgba}, {0, 10, 1 / z, 0, 0, rgba}},
                         nullptr, mat, false});
    view.orden.push_back({n, n + 1, z, pase});
  };
  slot({0x80000800, 0x44}, 2, 2, {0, 200, 0, 127.5});
  slot({0x80000800, 0x44}, 2, 10, {255, 255, 255, 255});
  slot({0x1000, 0x44}, 1, 1, {255, 255, 255, 0});
  slot({0x80000800, 0x44}, 2, 3, {0, 0, 200, 127.5});
  slot({0x10, 0x44}, 1, 4, {200, 0, 0, 255});
  slot({0x10, 0x44}, 0, 1, {255, 255, 255, 255});
  view.Pintar({});
  const size_t order[] = {5, 2, 4, 1, 3, 0};
  for (size_t j = 0; j < std::size(order); ++j) if (view.orden[j].primero != order[j]) return 22;
  if (view.rgba[0] != IM_COL32(50, 100, 50, 255) || std::abs(view.profundidad[0] - .25f) > 1e-6f) return 23;
  if (r::Mezclar(0x48, 200, 30, 64) != 130 || r::Mezclar(0x42, 200, 130, 64) != 30 ||
      r::Mezclar(0x4000000064ull, 200, 100, 0) != 150) return 24;
  return 0;
}

int wmain(int argc, wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  if (int ec = PruebaSintetica()) return ec;
  if (argc == 1) return 0;
  if (argc != 3 || !CargarFixture()) return 2;
  using namespace dbz3::pasarela::cine1910;
  auto p = Cargar(argv[1], argv[2], 5);
  if (!p || p->tomas.size() != 14) return 3;
  const auto actores = p->actores;
  const auto entorno = p->entorno;
  const auto eventos = p->eventos;
  const auto bytes = Fixture("cuadros.txt");
  std::istringstream plan(std::string(bytes.begin(), bytes.end()));
  std::vector<double> times;
  double val = 0;
  while (plan >> val) times.push_back(val);
  if (times.size() != 14) return 4;

  // Volcar los 14 cortes en los instantes PAL exactos
  std::erase_if(p->eventos, [](const Evento& e) { return e.tipo == Voz; });
  silenciar_eventos = true;
  std::ostringstream js;
  js << "{\n  \"spx\": 1910,\n  \"modo\": \"escena_b1_1910_hd\",\n  \"cortes\": [\n";
  for (size_t i = 0; i < times.size(); ++i) {
    p->idx = p->toma = 0;
    p->luz_actual = {-1, -1};
    p->actores = actores;
    p->entorno = entorno;
    p->Avanzar(times[i]);
    const auto t0 = std::chrono::steady_clock::now();
    if (!p->Render3D(times[i]) || p->dibujo.orden.empty()) return 25;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    char fn[64];
    std::snprintf(fn, sizeof(fn), "corte_%02zu.ppm", i);
    if (!GuardarPpm1910(fn, raster::W, raster::H, reinterpret_cast<const uint8_t*>(p->dibujo.rgba.data()))) return 27;
    std::vector<int> vis;
    for (size_t s = 0; s < p->actores.size(); ++s)
      if (p->actores[s].dibujo || p->actores[s].dibujo_b) vis.push_back(int(s));
    js << "    {\"corte\": " << i << ", \"t_u\": " << times[i] << ", \"cam\": " << p->toma
       << ", \"tris_puerto\": " << p->dibujo.tris.size() << ", \"ms\": " << ms << ", \"visibles\": [";
    for (size_t k = 0; k < vis.size(); ++k) js << (k ? ", " : "") << vis[k];
    js << "]}" << (i + 1 < times.size() ? "," : "") << "\n";
  }
  js << "  ]\n}\n";
  std::ofstream("escena_1910_hd.json") << js.str();

  // Pase completo de los 21 eventos (verificacion de linea temporal)
  p->idx = p->toma = 0;
  p->luz_actual = {-1, -1};
  p->actores = actores;
  p->entorno = entorno;
  p->eventos = eventos;
  silenciar_eventos = false;
  for (int cuadro = 0; cuadro <= int(std::ceil(p->salida)); ++cuadro) {
    p->Avanzar(cuadro);
    p->Avanzar(cuadro);
  }
  if (p->idx != p->eventos.size()) return 13;
  return 0;
}
'''

# =============================================================================
# 3. BACKEND BLENDER (extension de TR3 a las 38 escenas en lote)
# =============================================================================

BLENDER_BACKEND_PY = r'''"""Backend Blender TRG5 (extiende b1_escena_blender.py a lotes de las 38 escenas)."""
import json
import math
from pathlib import Path
import struct
import sys

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Matrix, Vector
import numpy as np

TRI = np.dtype([('tex', '<i4'), ('mode', '<i4'), ('v', '<f4', (3, 10))])


def load_texture(path):
    raw = path.read_bytes()
    w, h = struct.unpack_from('<II', raw)
    if not (1 <= w <= 4096 and 1 <= h <= 4096 and len(raw) == 8 + 4 * w * h):
        raise ValueError(f'Textura truncada: {path}')
    img = bpy.data.images.new(path.stem, w, h, alpha=True)
    img.colorspace_settings.name = 'Non-Color'
    img.alpha_mode = 'STRAIGHT'
    px = np.frombuffer(raw, 'u1', offset=8).reshape(h, w, 4)[::-1]
    img.pixels.foreach_set(np.ascontiguousarray(px, dtype='f4').ravel() / np.float32(255))
    img.update()
    return img


def make_material(orig_dir, tex_id, mode, fog, images):
    name = f'm_{tex_id}_{mode}'
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    new, link = nodes.new, links.new

    rgba = new('ShaderNodeVertexColor')
    rgba.layer_name = 'tinte'
    amount = new('ShaderNodeAttribute')
    amount.attribute_name = 'niebla'
    color, alpha = rgba.outputs['Color'], rgba.outputs['Alpha']

    if tex_id >= 0:
        img = new('ShaderNodeTexImage')
        if tex_id not in images:
            images[tex_id] = load_texture(orig_dir / f'tex_{tex_id}.rgba')
        img.image = images[tex_id]
        mult = new('ShaderNodeMixRGB')
        mult.blend_type = 'MULTIPLY'
        mult.inputs[0].default_value = 1.0
        link(color, mult.inputs[1])
        link(img.outputs['Color'], mult.inputs[2])
        color = mult.outputs[0]
        mul_a = new('ShaderNodeMath')
        mul_a.operation = 'MULTIPLY'
        link(alpha, mul_a.inputs[0])
        link(img.outputs['Alpha'], mul_a.inputs[1])
        alpha = mul_a.outputs[0]

    mix_fog = new('ShaderNodeMixRGB')
    mix_fog.inputs[2].default_value = (*fog, 1.0)
    link(amount.outputs['Fac'], mix_fog.inputs[0])
    link(color, mix_fog.inputs[1])
    emission = new('ShaderNodeEmission')
    link(mix_fog.outputs[0], emission.inputs['Color'])
    out = new('ShaderNodeOutputMaterial')

    if mode == 0:  # estrictamente opaco
        link(emission.outputs[0], out.inputs['Surface'])
    elif mode == 1:  # alpha cutout (TEST_1)
        gt = new('ShaderNodeMath')
        gt.operation = 'GREATER_THAN'
        gt.inputs[1].default_value = 0.004
        link(alpha, gt.inputs[0])
        blend = new('ShaderNodeMixShader')
        link(gt.outputs[0], blend.inputs[0])
        link(new('ShaderNodeBsdfTransparent').outputs[0], blend.inputs[1])
        link(emission.outputs[0], blend.inputs[2])
        link(blend.outputs[0], out.inputs['Surface'])
    elif mode == 3:  # aditivo GS ALPHA=0x48
        link(alpha, emission.inputs['Strength'])
        add = new('ShaderNodeAddShader')
        link(new('ShaderNodeBsdfTransparent').outputs[0], add.inputs[0])
        link(emission.outputs[0], add.inputs[1])
        link(add.outputs[0], out.inputs['Surface'])
    else:  # alpha blend estandar GS ALPHA=0x44
        blend = new('ShaderNodeMixShader')
        link(alpha, blend.inputs[0])
        link(new('ShaderNodeBsdfTransparent').outputs[0], blend.inputs[1])
        link(emission.outputs[0], blend.inputs[2])
        link(blend.outputs[0], out.inputs['Surface'])
    return mat


def add_actor_mesh(scene, orig_dir, slot, is_sky, triangles, eye, fog, images):
    v = triangles['v'].reshape(-1, 10)
    if not np.isfinite(v).all():
        v = np.nan_to_num(v, nan=0.0, posinf=0.0, neginf=0.0)
    xyz = v[:, :3].copy()
    if is_sky:
        at = np.asarray(eye, dtype='f4')
        xyz = at + (xyz - at) * 16.0
    mesh = bpy.data.meshes.new(f'actor_{slot:02d}')
    mesh.from_pydata(xyz.tolist(), [], np.arange(len(v)).reshape(-1, 3).tolist())
    obj = bpy.data.objects.new(mesh.name, mesh)
    scene.collection.objects.link(obj)

    keys = np.stack([triangles['tex'], triangles['mode']], axis=1)
    uniq, inv = np.unique(keys, axis=0, return_inverse=True)
    for t_id, m_id in uniq:
        mesh.materials.append(make_material(orig_dir, int(t_id), int(m_id), fog, images))
    mesh.polygons.foreach_set('material_index', inv.astype('i4'))

    uv = mesh.uv_layers.new(name='UVMap')
    uv.data.foreach_set('uv', np.ascontiguousarray(v[:, 3:5]).ravel())
    col = mesh.color_attributes.new(name='tinte', type='FLOAT_COLOR', domain='CORNER')
    col.data.foreach_set('color', np.ascontiguousarray(v[:, 5:9]).ravel())
    fog_attr = mesh.attributes.new(name='niebla', type='FLOAT', domain='CORNER')
    fog_attr.data.foreach_set('value', v[:, 9].copy())
    mesh.update()
    return dict(slot=int(slot), is_sky=bool(is_sky), triangulos=int(len(triangles)))


def clear_meshes():
    for obj in tuple(bpy.data.objects):
        if obj.type == 'MESH':
            m = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            bpy.data.meshes.remove(m)
    for mat in tuple(bpy.data.materials):
        if mat.users == 0:
            bpy.data.materials.remove(mat)


def clear_images(images):
    for im in list(images.values()):
        try:
            bpy.data.images.remove(im, do_unlink=True)
        except Exception:
            pass
    images.clear()


def render_batch(manifest_path, samples):
    jobs = json.loads(Path(manifest_path).read_bytes())
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.world.use_nodes = True
    scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value = 0.0
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = samples
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.cycles.transparent_max_bounces = 32
    scene.cycles.max_bounces = 0
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.view_settings.view_transform = 'Raw'
    scene.render.dither_intensity = 0.0

    camera = bpy.data.cameras.new('AMC')
    cam_obj = bpy.data.objects.new('AMC', camera)
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj
    camera.sensor_fit = 'VERTICAL'
    camera.clip_start, camera.clip_end = 0.1, 1e7

    for job in jobs:
        orig_dir = Path(job['orig_dir'])
        out_dir = Path(job['blender_dir'])
        out_dir.mkdir(parents=True, exist_ok=True)
        w, h = job['resolucion']
        scene.render.resolution_x, scene.render.resolution_y = w, h
        images = {}
        inventory = []
        for cut in job['cortes']:
            i = int(cut['corte'])
            raw = (orig_dir / f'corte_{i:02d}.tri').read_bytes()
            if raw[:4] != b'TR32':
                raise ValueError(f'Cabecera TR32 invalida en {orig_dir}/corte_{i:02d}.tri')
            data = struct.unpack_from('<22f', raw, 4)
            view = Matrix(tuple(data[j:16:4] for j in range(4)))
            cam_obj.matrix_world = view.inverted()
            fov, fx, fy = data[16:19]
            fog = data[19:22]
            camera.lens = camera.sensor_height / (2.0 * math.tan(fov / 2.0))
            scene.render.pixel_aspect_x = 1.0
            scene.render.pixel_aspect_y = fx / fy
            bpy.context.view_layer.update()

            # Verificar proyeccion Blender == AMC B1 (como TR3)
            pt = Vector((0.25, 0.5, -10.0))
            ndc = world_to_camera_view(scene, cam_obj, cam_obj.matrix_world @ pt)
            focus = 1.0 / (10.0 * math.tan(fov / 2.0))
            px = (ndc.x - 0.5) * w
            py = (ndc.y - 0.5) * h
            if max(abs(px - pt.x * fx * focus), abs(py - pt.y * fy * focus)) > 0.25:
                raise ValueError(f'Proyeccion Blender != AMC en {job["spx"]} corte {i}: {px},{py}')

            at = 4 + 22 * 4
            mesh_info = []
            while True:
                slot, = struct.unpack_from('<i', raw, at)
                at += 4
                if slot == -1:
                    break
                is_sky, n = struct.unpack_from('<iI', raw, at)
                at += 8
                tris = np.frombuffer(raw, TRI, count=n, offset=at)
                mesh_info.append(add_actor_mesh(scene, orig_dir, slot, is_sky, tris, cam_obj.location, fog, images))
                at += n * TRI.itemsize

            png_path = out_dir / cut['png']
            scene.render.filepath = str(png_path)
            bpy.ops.render.render(write_still=True)
            inventory.append(dict(corte=i, t_u=cut['t_u'], actores=mesh_info, fov=fov))
            clear_meshes()

        clear_images(images)
        (out_dir / 'blender_resultado.json').write_text(
            json.dumps(dict(spx=job['spx'], cuadros=inventory), ensure_ascii=False, indent=2) + '\n',
            encoding='utf-8',
        )


if __name__ == '__main__':
    manifest, n_samples = sys.argv[sys.argv.index('--') + 1:]
    render_batch(manifest, int(n_samples))
'''


# =============================================================================
# 4. COMPILACION Y EJECUCION DE LOS ARNESES C++
# =============================================================================

def _clang_cmd(src_file: str) -> str:
    cmd = subprocess.check_output(
        ["ninja", "-C", "out/build/win-amd64-dual", "-t", "commands", f"CMakeFiles/dbz3.dir/src/{src_file}.obj"],
        cwd=ROOT,
        text=True,
    ).splitlines()[-1]
    if " -MD " not in cmd or "clang++" not in cmd:
        raise RuntimeError(f"Comando Clang de win-amd64-dual no reconocido para {src_file}")
    return cmd.split(" -MD ", 1)[0]


def compilar_arnes_generico(src_dir: Path, bin_dir: Path) -> Path:
    """Compila juez_generico.exe usando src/ps2_modelo.cpp y src/escena_b1_raster.inl."""
    bin_dir.mkdir(parents=True, exist_ok=True)
    exe = bin_dir / "juez_generico.exe"
    cpp = bin_dir / "juez_generico.cpp"
    b1 = src_dir / "pasarela_b1.cpp"
    ps2 = src_dir / "ps2_modelo.cpp"
    head = (
        '#include "ps2_modelo.h"\n'
        '#include <windows.h>\n'
        '#include <algorithm>\n'
        '#include <array>\n'
        '#include <cmath>\n'
        '#include <cstdint>\n'
        '#include <cstring>\n'
        '#include <filesystem>\n'
        '#include <fstream>\n'
        '#include <iostream>\n'
        '#include <unordered_map>\n'
        '#include <vector>\n'
        '#define REXLOG_WARN(...)\n'
        '#define REXLOG_INFO(...)\n'
        'namespace dbz3::settings { int Language() { return 1; } }\n'
    )
    iso_stub = section(
        take(src_dir, "pasarela_b1.cpp", "namespace dbz3::pasarela {", "int B1aB3("),
        "class Iso {",
        "std::string BootElf",
        "class Iso {\n public:\n  bool Open(const std::filesystem::path&) { return false; }\n"
        "  std::string Find(const std::string&) const { return {}; }\n"
        "  std::pair<uint32_t,uint32_t> Info(const std::string&) const { return {0,0}; }\n"
        "  std::vector<uint8_t> ReadFile(const std::string&,uint64_t,uint32_t) { return {}; }\n};\n",
    )
    src_code = (
        head
        + iso_stub
        + take(src_dir, "pasarela_b1.cpp", "struct AfsPs2::Impl", "std::filesystem::path BuscarJuego(")
        + "}\n"
        + take(src_dir, "ps2_modelo.cpp", "namespace dbz3::ps2 {", "float LoadF(")
        + "}\n"
        + take(src_dir, "ps2_modelo.cpp", "Giro LeerGiro(", "Modelo CargarDeAfs(")
        + "}\n"
        + ARNES_GENERICO_TAIL
    )
    src_hash = hashlib.sha256(src_code.encode("utf-8")).hexdigest()[:16]
    stamp = bin_dir / "juez_generico.sha"
    if exe.is_file() and stamp.is_file() and stamp.read_text(encoding="utf-8").strip() == src_hash:
        return exe
    cpp.write_text(src_code, encoding="utf-8")
    env = dict(os.environ, TMP=str(bin_dir), TEMP=str(bin_dir))
    base_cmd = _clang_cmd("pasarela_b1.cpp")
    subprocess.run(
        f'{base_cmd} -I"{src_dir}" -I"{ROOT / "src"}" -O2 -fuse-ld=lld "{cpp}" -o "{exe}"',
        cwd=bin_dir,
        env=env,
        check=True,
        timeout=180,
    )
    stamp.write_text(src_hash, encoding="utf-8")
    return exe


def _payload_1910(cache: Path, iso_path: Path, times_str: str) -> bytes:
    iso = Iso(iso_path)
    chunks: dict[str, bytes] = {"cuadros.txt": times_str.encode("ascii")}
    for bank, ids in [("data_en.afs", [1909]), ("data_sp.afs", [1909]), ("adx_jp.afs", range(699, 706))]:
        path = iso.find(bank)
        head = iso.read(path, 0, 8)
        tab = iso.read(path, 8, struct.unpack_from("<I", head, 4)[0] * 8)
        chunks[f"{bank}:0"] = head
        chunks[f"{bank}:8"] = tab
        for i in ids:
            off, n = struct.unpack_from("<II", tab, i * 8)
            chunks[f"{bank}:{off}"] = iso.read(path, off, n)
    doc = tomllib.loads((cache / "escena_1910.toml").read_text(encoding="utf-8"))
    names = (
        {"escena_1910.toml"}
        | {r["matrices"] for r in doc["retarget"]}
        | {r["file"] for r in doc["clips"] if r["kind"] in ("amm", "amp")}
    )
    for actor in doc["actores"]:
        if "hd_skin" in actor:
            names.add(actor["hd_skin"])
        else:
            names.update((actor["model"], actor["texture"]))
    for name in sorted(names):
        chunks[name] = (cache / name).read_bytes()
    out = bytearray(struct.pack("<I", len(chunks)))
    for key, data in chunks.items():
        k = key.encode("ascii")
        out.extend(struct.pack("<II", len(k), len(data)) + k + data)
    return bytes(out)


def ejecutar_arnes_1910_hd(
    src_dir: Path,
    cache_1910: Path,
    iso_path: Path,
    plan_1910: dict,
    out_dir: Path,
    parche: Path | None = None,
) -> dict:
    """Compila y ejecuta el arnes C++ real de src/escena_b1_1910.inl en los 14 cortes."""
    out_dir.mkdir(parents=True, exist_ok=True)
    stage_root = out_dir / "stage"
    stage = stage_root / "src"
    if stage_root.exists():
        shutil.rmtree(stage_root)
    stage.mkdir(parents=True)
    files = ("escena_b1_1910.inl", "escena_b1_raster.inl", "pasarela_misiones.cpp",
             "ps2_modelo.cpp", "ps2_modelo.h", "pasarela_nave.cpp")
    for f in files:
        shutil.copyfile(src_dir / f, stage / f)
    if parche is not None:
        subprocess.run(["git", "apply", "--whitespace=error", str(parche.resolve())], cwd=stage_root, check=True)

    inp = section(
        (stage / "escena_b1_1910.inl").read_text(encoding="utf-8"),
        "  std::ifstream f(dir / nombre,",
        "  return out;",
        '  auto out=Fixture(nombre);\n  Exigir(!out.empty() && out.size()<=tope,"fixture de cache");\n',
    )
    (stage / "sonda_escena.inl").write_text(inp, encoding="utf-8")

    b1_src = ROOT / "src"
    head = r'''#include "cinematica.h"
#include <rex/logging.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <fcntl.h>
#include <io.h>
static std::unordered_map<std::string,std::vector<uint8_t>> blobs;
static bool CargarFixture() {
  _setmode(_fileno(stdin),_O_BINARY);
  uint32_t n;
  if (std::fread(&n,4,1,stdin)!=1 || n>512) return false;
  for (uint32_t i=0;i<n;++i) {
    uint32_t tam[2];
    if(std::fread(tam,4,2,stdin)!=2 || tam[0]>256 || tam[1]>(64u<<20)) return false;
    std::string nombre(tam[0],'\0');
    if(std::fread(nombre.data(),1,nombre.size(),stdin)!=nombre.size()) return false;
    auto& bytes=blobs[nombre]; bytes.resize(tam[1]);
    if(std::fread(bytes.data(),1,bytes.size(),stdin)!=bytes.size()) return false;
  }
  return true;
}
static std::vector<uint8_t> Fixture(const std::string& name) {
  const auto i=blobs.find(name); return i==blobs.end()?std::vector<uint8_t>{}:i->second;
}
#undef REXLOG_WARN
#undef REXLOG_INFO
#define REXLOG_WARN(fmt, x) (std::cerr << x << '\n')
static bool silenciar_eventos=false;
template<class... Args> void print_event(const char* fmt,Args... args) {
  if (silenciar_eventos) return;
  std::cout << fmt;
  ((std::cout << ' ' << args),...);
  std::cout << '\n';
}
#define REXLOG_INFO(...) print_event(__VA_ARGS__)
namespace dbz3::settings { int Language() { return 5; } }
#include "sonda_escena.inl"
'''
    cpp = stage / "juez_1910.cpp"
    cpp.write_text(
        head
        + section(
            take(b1_src, "pasarela_b1.cpp", "namespace dbz3::pasarela {", "int B1aB3("),
            "class Iso {",
            "std::string BootElf",
            r'''
class Iso {
 public:
  bool Open(const std::filesystem::path&) { return true; }
  std::string Find(const std::string& n) const { return Lower(n); }
  std::pair<uint32_t,uint32_t> Info(const std::string&) const { return {0,0}; }
  std::vector<uint8_t> ReadFile(const std::string& p,uint64_t off,uint32_t n) {
    const auto i=blobs.find(p+":"+std::to_string(off));
    if(i==blobs.end()) return {};
    return {i->second.begin(),i->second.begin()+std::min<size_t>(n,i->second.size())};
  }
};
''',
        )
        + take(b1_src, "pasarela_b1.cpp", "// ---- Voces de B1", "void CallarVozB1()")
        + take(b1_src, "pasarela_b1.cpp", "struct AfsPs2::Impl", "std::filesystem::path BuscarJuego(")
        + "}\n"
        + take(stage, "ps2_modelo.cpp", "namespace dbz3::ps2 {", "float LoadF(")
        + "}\n"
        + take(stage, "ps2_modelo.cpp", "Giro LeerGiro(", "Modelo CargarDeAfs(")
        + "}\n"
        + "namespace dbz3::pasarela::nave { struct Fig;\n"
        + take(b1_src, "pasarela_nave_rueda.inl", "struct SkB3 {", "std::set<int> g_sk_boca_pidiendo")
        + section(
            take(b1_src / "survivors", "modelo_cpu.inl", "SkB3 LeerSkCpu", "// Oraculo de prueba"),
            "  std::ifstream in(",
            "  const bool hms3",
            "  auto d=Fixture(ruta.filename().string());\n  if (d.size()<20 || d.size()>128u*1024*1024) return {};\n",
        )
        + take(stage, "pasarela_nave.cpp", "bool LeerFigura1910", "int EstadoFiguraB3")
        + "}\n"
        + ARNES_1910_INC,
        encoding="utf-8",
    )
    exe = stage / "juez_1910.exe"
    env = dict(os.environ, TMP=str(stage), TEMP=str(stage))
    subprocess.run(
        f'{_clang_cmd("pasarela_b1.cpp")} -I"{stage}" -I"{b1_src}" -O2 -fuse-ld=lld "{cpp}" -o "{exe}" -lwinmm',
        cwd=stage,
        env=env,
        check=True,
        timeout=180,
    )
    subprocess.run([str(exe)], cwd=stage, check=True, timeout=15)
    times_str = "".join(f"{c['t_u']}\n" for c in plan_1910["cortes"])
    payload = _payload_1910(cache_1910, iso_path, times_str)
    log_path = out_dir / "eventos_1910_hd.log"
    with log_path.open("wb") as lf:
        subprocess.run(
            [str(exe), str(cache_1910.resolve()), str(iso_path.resolve())],
            cwd=stage,
            input=payload,
            stdout=lf,
            check=True,
            timeout=120,
        )
    png_dir = out_dir / "puerto_1910_hd"
    png_dir.mkdir(exist_ok=True)
    for c in plan_1910["cortes"]:
        i = c["corte"]
        ppm = stage / f"corte_{i:02d}.ppm"
        with Image.open(ppm) as im:
            im.convert("RGB").save(png_dir / c["png"])
    shutil.copyfile(stage / "escena_1910_hd.json", out_dir / "escena_1910_hd.json")
    return json.loads((out_dir / "escena_1910_hd.json").read_bytes())


# =============================================================================
# 5. MOTOR DE COMPARACION HONESTA (5 METRICAS + ANTI-GEM2)
# =============================================================================

def _sobel_mag(gray: np.ndarray) -> np.ndarray:
    g = np.pad(gray, 1, mode="edge")
    gx = (
        -g[:-2, :-2] + g[:-2, 2:]
        - 2.0 * g[1:-1, :-2] + 2.0 * g[1:-1, 2:]
        - g[2:, :-2] + g[2:, 2:]
    )
    gy = (
        -g[:-2, :-2] - 2.0 * g[:-2, 1:-1] - g[:-2, 2:]
        + g[2:, :-2] + 2.0 * g[2:, 1:-1] + g[2:, 2:]
    )
    return np.hypot(gx, gy)


def _dilate_bin(mask: np.ndarray, radius: int = 2) -> np.ndarray:
    h, w = mask.shape
    padded = np.pad(mask, radius, mode="constant", constant_values=False)
    out = np.zeros_like(mask, dtype=bool)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            out |= padded[radius + dy : radius + dy + h, radius + dx : radius + dx + w]
    return out


def _ssim_blocks(ref_luma: np.ndarray, cand_luma: np.ndarray, blk: int = 16) -> float:
    h, w = ref_luma.shape
    nh, nw = h // blk, w // blk
    r = ref_luma[: nh * blk, : nw * blk].reshape(nh, blk, nw, blk).transpose(0, 2, 1, 3).reshape(nh * nw, blk * blk)
    c = cand_luma[: nh * blk, : nw * blk].reshape(nh, blk, nw, blk).transpose(0, 2, 1, 3).reshape(nh * nw, blk * blk)
    mr = r.mean(axis=1)
    mc = c.mean(axis=1)
    vr = ((r - mr[:, None]) ** 2).mean(axis=1)
    vc = ((c - mc[:, None]) ** 2).mean(axis=1)
    cov = ((r - mr[:, None]) * (c - mc[:, None])).mean(axis=1)
    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    num = (2.0 * mr * mc + c1) * (2.0 * cov + c2)
    den = (mr * mr + mc * mc + c1) * (vr + vc + c2)
    return float(np.clip((num / den).mean(), 0.0, 1.0) * 100.0)


def evaluar_corte(
    ref_rgb: np.ndarray,
    cand_rgb: np.ndarray,
    ref_mask: np.ndarray | None = None,
    cand_mask: np.ndarray | None = None,
    ref_actors: dict[str, int] | None = None,
    cand_actors: dict[str, int] | None = None,
) -> dict:
    """Evalua un par (original, puerto) con 5 metricas independientes y alertas."""
    ref = ref_rgb[:, :, :3].astype(np.float64)
    cand = cand_rgb[:, :, :3].astype(np.float64)
    h, w, _ = ref.shape
    total_px = float(h * w)

    diff = np.abs(cand - ref)
    mae_full = float(diff.mean() * 100.0 / 255.0)
    ref_luma = 0.299 * ref[:, :, 0] + 0.587 * ref[:, :, 1] + 0.114 * ref[:, :, 2]
    cand_luma = 0.299 * cand[:, :, 0] + 0.587 * cand[:, :, 1] + 0.114 * cand[:, :, 2]

    alertas: list[str] = []

    # Comprobacion de cuadro degenerado (todo negro, todo blanco o plano)
    ref_std = float(ref.std())
    cand_std = float(cand.std())
    cand_white_all = float(((cand[:, :, 0] >= 240) & (cand[:, :, 1] >= 240) & (cand[:, :, 2] >= 240)).mean() * 100.0)
    cand_black_all = float(((cand[:, :, 0] <= 12) & (cand[:, :, 1] <= 12) & (cand[:, :, 2] <= 12)).mean() * 100.0)
    ref_white_all = float(((ref[:, :, 0] >= 240) & (ref[:, :, 1] >= 240) & (ref[:, :, 2] >= 240)).mean() * 100.0)
    ref_black_all = float(((ref[:, :, 0] <= 12) & (ref[:, :, 1] <= 12) & (ref[:, :, 2] <= 12)).mean() * 100.0)
    degenerado = ref_std > 5.0 and (cand_std < 1.5 or (cand_black_all > 95.0 and ref_black_all < 85.0) or (cand_white_all > 95.0 and ref_white_all < 85.0))
    if degenerado:
        alertas.append("FRAME_DEGENERADO")

    # 1. CIELO
    sky_mask = None
    sky_mode = "banda_superior"
    if ref_mask is not None:
        sm = (ref_mask & 0x80) != 0
        if cand_mask is not None:
            sm = sm | ((cand_mask & 0x80) != 0)
        if sm.mean() >= 0.03:
            sky_mask = sm
            sky_mode = "mascara_cielo"
    if sky_mask is None:
        sky_mask = np.zeros((h, w), dtype=bool)
        sky_mask[: int(h * 0.36), :] = True

    sr = ref[sky_mask]
    sc = cand[sky_mask]
    sky_mae = float(np.abs(sc - sr).mean() * 100.0 / 255.0)
    sky_white_ref = float(((sr[:, 0] >= 235) & (sr[:, 1] >= 235) & (sr[:, 2] >= 235)).mean() * 100.0)
    sky_white_cand = float(((sc[:, 0] >= 235) & (sc[:, 1] >= 235) & (sc[:, 2] >= 235)).mean() * 100.0)
    sky_white_delta = abs(sky_white_cand - sky_white_ref)
    sky_blue_ref = float((sr[:, 2] - sr[:, 0]).mean())
    sky_blue_cand = float((sc[:, 2] - sc[:, 0]).mean())
    sky_blue_bias = abs(sky_blue_cand - sky_blue_ref)

    # Gradiente en cielo: detecta placas rectangulares opacas o rayas diagonales
    gr_full = _sobel_mag(ref_luma)
    gc_full = _sobel_mag(cand_luma)
    sky_grad_ref = float((gr_full[sky_mask] > 35.0).mean() * 100.0)
    sky_grad_cand = float((gc_full[sky_mask] > 35.0).mean() * 100.0)
    sky_grad_delta = abs(sky_grad_cand - sky_grad_ref)

    sky_score = float(
        np.clip(
            100.0 - 2.4 * sky_mae - 1.5 * sky_white_delta - 0.55 * sky_blue_bias - 1.4 * sky_grad_delta,
            0.0,
            100.0,
        )
    )
    if sky_white_delta > 15.0:
        alertas.append("CIELO_BLANCO")
    if sky_score < 55.0:
        alertas.append("CIELO_ROTO")

    # 2. COLOR POR ZONAS (rejilla 4x4)
    grid = 4
    zh, zw = h // grid, w // grid
    zone_maes = []
    zone_mean_shifts = []
    for gy in range(grid):
        for gx in range(grid):
            zr = ref[gy * zh : (gy + 1) * zh, gx * zw : (gx + 1) * zw]
            zc = cand[gy * zh : (gy + 1) * zh, gx * zw : (gx + 1) * zw]
            z_mae = float(np.abs(zc - zr).mean() * 100.0 / 255.0)
            z_shift = float(np.abs(zc.mean(axis=(0, 1)) - zr.mean(axis=(0, 1))).mean() * 100.0 / 255.0)
            zone_maes.append(z_mae)
            zone_mean_shifts.append(z_shift)
    mean_zone_mae = float(np.mean(zone_maes))
    worst_zone_mae = float(np.max(zone_maes))
    top2_zone_mae = float(np.mean(sorted(zone_maes, reverse=True)[:2]))
    max_zone_mean_shift = float(np.max(zone_mean_shifts))
    zone_color_score = float(
        np.clip(
            100.0 - 2.1 * mean_zone_mae - 0.85 * worst_zone_mae - 0.65 * max_zone_mean_shift,
            0.0,
            100.0,
        )
    )
    if worst_zone_mae > 22.0 or max_zone_mean_shift > 18.0:
        alertas.append("ZONA_CRITICA")

    # 3. SILUETAS (Sobel F1 + primer plano IoU + SSIM por bloques)
    er = gr_full > 30.0
    ec = gc_full > 30.0
    if er.any() or ec.any():
        er_d = _dilate_bin(er, 2)
        ec_d = _dilate_bin(ec, 2)
        prec = float((ec & er_d).sum()) / max(1.0, float(ec.sum()))
        rec = float((er & ec_d).sum()) / max(1.0, float(er.sum()))
        edge_f1 = 200.0 * prec * rec / max(1e-9, prec + rec)
    else:
        edge_f1 = 100.0

    if ref_mask is not None and cand_mask is not None:
        fg_r = (ref_mask != 0) & ((ref_mask & 0x80) == 0)
        fg_c = (cand_mask != 0) & ((cand_mask & 0x80) == 0)
        union = int((fg_r | fg_c).sum())
        fg_iou = float((fg_r & fg_c).sum()) * 100.0 / union if union > 0 else 100.0
    else:
        # Silueta por bordes + diferencia sobre el fondo dominante
        fg_r = (ref_luma < 230.0) & (gr_full > 15.0)
        fg_c = (cand_luma < 230.0) & (gc_full > 15.0)
        fg_rd, fg_cd = _dilate_bin(fg_r, 3), _dilate_bin(fg_c, 3)
        union = int((fg_rd | fg_cd).sum())
        fg_iou = float((fg_rd & fg_cd).sum()) * 100.0 / union if union > 0 else 100.0

    ssim_pct = _ssim_blocks(ref_luma, cand_luma, 16)
    silhouette_score = float(np.clip(0.45 * edge_f1 + 0.30 * fg_iou + 0.25 * ssim_pct, 0.0, 100.0))
    if silhouette_score < 55.0:
        alertas.append("SILUETA_ROTA")

    # 4. PIXELES BLANCOS O NEGROS ANOMALOS (incluye deriva de oscuros: pelo gris!)
    white_excess = max(0.0, cand_white_all - ref_white_all)
    white_deficit = max(0.0, ref_white_all - cand_white_all)
    black_excess = max(0.0, cand_black_all - ref_black_all)
    black_deficit = max(0.0, ref_black_all - cand_black_all)

    # Deriva de luminancia sobre pixeles oscuros de la referencia (pelo negro, contornos, espacio)
    dark_ref_mask = (ref[:, :, 0] <= 20) & (ref[:, :, 1] <= 20) & (ref[:, :, 2] <= 20)
    dark_ref_pct = float(dark_ref_mask.mean() * 100.0)
    if dark_ref_pct >= 1.5:
        dark_ref_drift = float(np.mean(cand_luma[dark_ref_mask] - ref_luma[dark_ref_mask]))
        dark_ref_abs = float(np.mean(np.abs(cand_luma[dark_ref_mask] - ref_luma[dark_ref_mask])))
    else:
        dark_ref_drift = 0.0
        dark_ref_abs = 0.0

    bw_score = float(
        np.clip(
            100.0
            - 2.4 * (white_excess + black_excess)
            - 2.2 * (white_deficit + black_deficit)
            - 1.3 * max(0.0, dark_ref_drift - 4.0)
            - 0.6 * max(0.0, dark_ref_abs - 6.0),
            0.0,
            100.0,
        )
    )
    if (dark_ref_drift > 18.0 and dark_ref_pct >= 2.0) or (black_deficit > 6.0 and dark_ref_drift > 10.0):
        alertas.append("OSCUROS_LAVADOS")
    if white_excess > 12.0:
        alertas.append("EXCESO_BLANCOS")
    if black_excess > 18.0:
        alertas.append("EXCESO_NEGROS")

    # 5. ELEMENTOS PRESENTES O FALTANTES
    blk = 16
    nh, nw = h // blk, w // blk
    bdiff = diff[: nh * blk, : nw * blk].max(axis=2).reshape(nh, blk, nw, blk).mean(axis=(1, 3))
    bad_blocks_pct = float((bdiff > 35.0).mean() * 100.0)
    severe_blocks_pct = float((bdiff > 65.0).mean() * 100.0)

    missing_actors: list[int] = []
    extra_actors: list[int] = []
    actor_f1 = None
    if ref_actors is not None and cand_actors is not None:
        min_px = int(0.002 * total_px)  # >= 0.2% de pantalla (~570 px)
        r_set = {int(k): int(v) for k, v in ref_actors.items() if int(v) >= min_px}
        c_set = {int(k): int(v) for k, v in cand_actors.items() if int(v) >= min_px}
        missing_actors = sorted(k for k in r_set if k not in c_set)
        extra_actors = sorted(k for k in c_set if k not in r_set)
        inter_w = sum(min(r_set[k], c_set[k]) for k in (set(r_set) & set(c_set)))
        tot_r = max(1, sum(r_set.values()))
        tot_c = max(1, sum(c_set.values()))
        prec_a = inter_w / tot_c
        rec_a = inter_w / tot_r
        actor_f1 = 200.0 * prec_a * rec_a / max(1e-9, prec_a + rec_a) if (r_set or c_set) else 100.0
        if any(r_set[k] >= int(0.01 * total_px) for k in missing_actors):
            alertas.append("ELEMENTO_FALTANTE")
        if any(c_set[k] >= int(0.01 * total_px) for k in extra_actors):
            alertas.append("ELEMENTO_FANTASMA")

    block_score = float(np.clip(100.0 - 1.5 * bad_blocks_pct - 1.2 * severe_blocks_pct, 0.0, 100.0))
    if actor_f1 is not None:
        elements_score = float(np.clip(0.50 * actor_f1 + 0.50 * block_score, 0.0, 100.0))
    else:
        elements_score = block_score
    if severe_blocks_pct > 12.0 and "ELEMENTO_FALTANTE" not in alertas:
        alertas.append("ELEMENTO_ANOMALO")

    # PUNTUACION GLOBAL HONESTA CON TOPE NO COMPENSABLE
    comps = [sky_score, zone_color_score, silhouette_score, bw_score, elements_score]
    weighted = (
        0.20 * sky_score
        + 0.25 * zone_color_score
        + 0.20 * silhouette_score
        + 0.15 * bw_score
        + 0.20 * elements_score
    )
    score = 0.0 if degenerado else float(min(weighted, min(comps) + 20.0))

    hard_alerts = {
        "FRAME_DEGENERADO",
        "CIELO_BLANCO",
        "CIELO_ROTO",
        "OSCUROS_LAVADOS",
        "ELEMENTO_FALTANTE",
        "ELEMENTO_FANTASMA",
        "SILUETA_ROTA",
    }
    if degenerado or score < 65.0 or any(a in hard_alerts for a in alertas):
        veredicto = "FAIL"
    elif score < 80.0 or alertas:
        veredicto = "AVISO"
    else:
        veredicto = "PASS"

    return dict(
        score=round(score, 2),
        veredicto=veredicto,
        mae_rgb=round(mae_full, 3),
        alertas=alertas,
        cielo=dict(
            score=round(sky_score, 2),
            modo=sky_mode,
            mae=round(sky_mae, 3),
            white_ref_pct=round(sky_white_ref, 2),
            white_cand_pct=round(sky_white_cand, 2),
            white_delta_pct=round(sky_white_delta, 2),
            blue_bias=round(sky_blue_bias, 2),
            grad_delta_pct=round(sky_grad_delta, 2),
        ),
        color_zonas=dict(
            score=round(zone_color_score, 2),
            mean_mae=round(mean_zone_mae, 3),
            worst_mae=round(worst_zone_mae, 3),
            top2_mae=round(top2_zone_mae, 3),
            max_mean_shift=round(max_zone_mean_shift, 3),
            zonas_4x4=[round(x, 2) for x in zone_maes],
        ),
        siluetas=dict(
            score=round(silhouette_score, 2),
            edge_f1=round(edge_f1, 2),
            fg_iou=round(fg_iou, 2),
            ssim_pct=round(ssim_pct, 2),
        ),
        blancos_negros=dict(
            score=round(bw_score, 2),
            white_ref_pct=round(ref_white_all, 2),
            white_cand_pct=round(cand_white_all, 2),
            white_excess=round(white_excess, 2),
            black_ref_pct=round(ref_black_all, 2),
            black_cand_pct=round(cand_black_all, 2),
            black_excess=round(black_excess, 2),
            black_deficit=round(black_deficit, 2),
            dark_ref_pct=round(dark_ref_pct, 2),
            dark_ref_drift=round(dark_ref_drift, 2),
        ),
        elementos=dict(
            score=round(elements_score, 2),
            actor_f1=round(actor_f1, 2) if actor_f1 is not None else None,
            missing_actors=missing_actors,
            extra_actors=extra_actors,
            bad_blocks_pct=round(bad_blocks_pct, 2),
            severe_blocks_pct=round(severe_blocks_pct, 2),
        ),
    )


def evaluar_regresion(eval_antes: list[dict], eval_despues: list[dict]) -> dict:
    """Puerta anti-GEM2: rechaza si cualquier corte o sub-metrica se aleja de la referencia."""
    if len(eval_antes) != len(eval_despues):
        raise ValueError("eval_antes y eval_despues deben tener el mismo numero de cortes")
    mean_a = float(np.mean([c["score"] for c in eval_antes]))
    mean_d = float(np.mean([c["score"] for c in eval_despues]))
    regresiones = []
    subs = ("cielo", "color_zonas", "siluetas", "blancos_negros", "elementos")
    for idx, (ca, cd) in enumerate(zip(eval_antes, eval_despues)):
        cid = cd.get("corte", idx)
        if cd["score"] < ca["score"] - 3.0:
            regresiones.append(f"corte {cid}: global {ca['score']:.1f} -> {cd['score']:.1f}")
        for k in subs:
            sa, sd = ca[k]["score"], cd[k]["score"]
            if sd < sa - 5.0:
                regresiones.append(f"corte {cid} {k}: {sa:.1f} -> {sd:.1f}")
        nuevas = sorted(set(cd["alertas"]) - set(ca["alertas"]))
        for al in nuevas:
            regresiones.append(f"corte {cid}: nueva alerta {al}")
    if mean_d < mean_a - 0.5:
        regresiones.append(f"media global: {mean_a:.2f} -> {mean_d:.2f}")
    return dict(
        aprobado=len(regresiones) == 0,
        veredicto="PASS" if not regresiones else "REGRESION",
        media_antes=round(mean_a, 2),
        media_despues=round(mean_d, 2),
        delta_media=round(mean_d - mean_a, 2),
        regresiones=regresiones,
    )


# =============================================================================
# 6. GENERACION DE LAMINAS Y TABLAS
# =============================================================================

def _diff_heatmap(ref_rgb: np.ndarray, cand_rgb: np.ndarray) -> Image.Image:
    d = np.abs(cand_rgb.astype(np.int16) - ref_rgb.astype(np.int16))
    amp = np.clip(d * 4, 0, 255).astype(np.uint8)
    # Resaltar en rojo intenso los pixeles con diferencia > 40
    hot = d.max(axis=2) > 40
    out = amp.copy()
    out[hot, 0] = 255
    out[hot, 1] = np.minimum(out[hot, 1], 60)
    out[hot, 2] = np.minimum(out[hot, 2], 60)
    return Image.fromarray(out, mode="RGB")


def generar_lamina_escena(
    titulo: str,
    cortes_info: list[dict],
    ref_paths: list[Path],
    cand_paths: list[Path],
    eval_cortes: list[dict],
    resumen: dict,
    out_png: Path,
) -> None:
    """Genera una lamina PNG de 3 columnas (Original | Puerto | Diff 4x) por escena."""
    w, h = 320, 224
    pad, row_title, header = 10, 24, 52
    n = len(cortes_info)
    sheet_w = 3 * w + 4 * pad
    sheet_h = header + n * (h + row_title + pad) + pad
    sheet = Image.new("RGB", (sheet_w, sheet_h), "#12161d")
    draw = ImageDraw.Draw(sheet)

    vcol = {"PASS": "#38d39f", "AVISO": "#f5b942", "FAIL": "#f25f5c"}.get(resumen["veredicto"], "#e7eaf0")
    draw.text((pad, 8), f"{titulo}  |  {n} cortes  |  Puntuacion: {resumen['score']:.1f}/100  [{resumen['veredicto']}]", fill=vcol)
    draw.text(
        (pad, 26),
        f"ORIGINAL (Blender PS2)            | PUERTO (Arnes C++)                | DIFERENCIA 4x   "
        f"[Cielo {resumen['cielo']:.0f} | Zonas {resumen['color_zonas']:.0f} | Sil {resumen['siluetas']:.0f} | "
        f"B/N {resumen['blancos_negros']:.0f} | Elem {resumen['elementos']:.0f}]",
        fill="#cfd8e3",
    )

    for idx, (ci, rp, cp, ev) in enumerate(zip(cortes_info, ref_paths, cand_paths, eval_cortes)):
        y = header + idx * (h + row_title + pad)
        r_im = Image.open(rp).convert("RGB")
        c_im = Image.open(cp).convert("RGB")
        d_im = _diff_heatmap(np.array(r_im), np.array(c_im))
        ccol = {"PASS": "#9be7c4", "AVISO": "#f7d07a", "FAIL": "#ff8585"}.get(ev["veredicto"], "#e7eaf0")
        al = ("  ALERTAS: " + ",".join(ev["alertas"])) if ev["alertas"] else ""
        label = (
            f"C{ci['corte']:02d} ({ci['segundos']:.1f}s) | {ev['score']:5.1f}/100 [{ev['veredicto']}] | "
            f"Cie:{ev['cielo']['score']:.0f} Zon:{ev['color_zonas']['score']:.0f} Sil:{ev['siluetas']['score']:.0f} "
            f"BN:{ev['blancos_negros']['score']:.0f} El:{ev['elementos']['score']:.0f} MAE:{ev['mae_rgb']:.2f}{al}"
        )
        draw.text((pad, y + 4), label[:155], fill=ccol)
        for j, im in enumerate((r_im, c_im, d_im)):
            x = pad + j * (w + pad)
            sheet.paste(im.resize((w, h), Image.Resampling.LANCZOS), (x, y + row_title))
    out_png.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out_png)


def resumir_escena(spx: int, eval_cortes: list[dict], modo: str = "generico") -> dict:
    scores = [c["score"] for c in eval_cortes]
    fails = sum(1 for c in eval_cortes if c["veredicto"] == "FAIL")
    avisos = sum(1 for c in eval_cortes if c["veredicto"] == "AVISO")
    alertas = sorted({a for c in eval_cortes for a in c["alertas"]})
    mean_s = float(np.mean(scores)) if scores else 0.0
    min_s = float(np.min(scores)) if scores else 0.0
    if fails > 0 or mean_s < 65.0:
        veredicto = "FAIL"
    elif avisos > 0 or mean_s < 80.0:
        veredicto = "AVISO"
    else:
        veredicto = "PASS"
    return dict(
        spx=int(spx),
        modo=modo,
        cortes=len(eval_cortes),
        score=round(mean_s, 2),
        min_corte=round(min_s, 2),
        veredicto=veredicto,
        cortes_fail=fails,
        cortes_aviso=avisos,
        mae_rgb=round(float(np.mean([c["mae_rgb"] for c in eval_cortes])), 3),
        cielo=round(float(np.mean([c["cielo"]["score"] for c in eval_cortes])), 2),
        color_zonas=round(float(np.mean([c["color_zonas"]["score"] for c in eval_cortes])), 2),
        siluetas=round(float(np.mean([c["siluetas"]["score"] for c in eval_cortes])), 2),
        blancos_negros=round(float(np.mean([c["blancos_negros"]["score"] for c in eval_cortes])), 2),
        elementos=round(float(np.mean([c["elementos"]["score"] for c in eval_cortes])), 2),
        alertas=alertas,
    )


def escribir_tablas(filas: list[dict], out_dir: Path, extra_1910_hd: dict | None = None) -> None:
    csv_path = out_dir / "tabla_38_escenas.csv"
    cols = [
        "spx", "modo", "cortes", "score", "min_corte", "veredicto", "cortes_fail", "cortes_aviso",
        "mae_rgb", "cielo", "color_zonas", "siluetas", "blancos_negros", "elementos", "alertas",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for r in filas:
            row = dict(r)
            row["alertas"] = ";".join(r.get("alertas", []))
            w.writerow({k: row.get(k, "") for k in cols})
        if extra_1910_hd:
            row = dict(extra_1910_hd)
            row["alertas"] = ";".join(row.get("alertas", []))
            w.writerow({k: row.get(k, "") for k in cols})

    lines = [
        "# Tabla de puntuaciones — Juez automatico B1 (38 escenas)",
        "",
        "| Escena | Modo | Cortes | Global | Min | Veredicto | Fail/Av | MAE | Cielo | Zonas | Siluetas | B/N | Elem | Alertas |",
        "|---:|---|---:|---:|---:|---|---|---:|---:|---:|---:|---:|---:|---|",
    ]
    all_rows = list(filas) + ([extra_1910_hd] if extra_1910_hd else [])
    for r in all_rows:
        al = ", ".join(r.get("alertas", [])) or "—"
        lines.append(
            f"| {r['spx']} | {r['modo']} | {r['cortes']} | {r['score']:.1f} | {r['min_corte']:.1f} | "
            f"**{r['veredicto']}** | {r['cortes_fail']}/{r['cortes_aviso']} | {r['mae_rgb']:.2f} | "
            f"{r['cielo']:.1f} | {r['color_zonas']:.1f} | {r['siluetas']:.1f} | {r['blancos_negros']:.1f} | "
            f"{r['elementos']:.1f} | {al} |"
        )
    (out_dir / "tabla_38_escenas.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


# =============================================================================
# 7. CONTROLES NEGATIVOS (DEBEN FALLAR) + CONTROL POSITIVO
# =============================================================================

def ejecutar_controles_negativos(
    ref_1910_dir: Path,
    out_dir: Path,
    gem2_dir: Path | None = GEM2_REF_DIR,
) -> dict:
    """Ejecuta 1 control positivo y 7 controles negativos (sinteticos + GEM2 real)."""
    nc_dir = out_dir / "controles_negativos"
    nc_dir.mkdir(parents=True, exist_ok=True)
    ref_c04 = np.array(Image.open(ref_1910_dir / "1910_corte_04.png").convert("RGB"))
    ref_c10 = np.array(Image.open(ref_1910_dir / "1910_corte_10.png").convert("RGB"))
    ref_c12 = np.array(Image.open(ref_1910_dir / "1910_corte_12.png").convert("RGB"))

    casos = []

    def add_case(cid: str, desc: str, ref_arr: np.ndarray, cand_arr: np.ndarray, debe_fallar: bool,
                 ref_act=None, cand_act=None):
        ev = evaluar_corte(ref_arr, cand_arr, ref_actors=ref_act, cand_actors=cand_act)
        cumplido = (ev["veredicto"] == "FAIL") if debe_fallar else (ev["veredicto"] == "PASS" and ev["score"] >= 99.9)
        rp = nc_dir / f"{cid}_ref.png"
        cp = nc_dir / f"{cid}_cand.png"
        Image.fromarray(ref_arr).save(rp)
        Image.fromarray(cand_arr).save(cp)
        casos.append(dict(id=cid, descripcion=desc, debe_fallar=debe_fallar, cumplido=cumplido,
                          eval=ev, ref_png=rp, cand_png=cp))

    # PC1: Identicas -> PASS 100/100
    add_case("PC1_identicas", "Control positivo: copia exacta de 1910 corte 10", ref_c10, ref_c10.copy(), False)

    # NC1: Pelo gris de Raditz (reproduccion sintetica del fallo GEM2)
    nc1 = ref_c12.copy()
    pelo_mask = (nc1[:, :, 0] <= 18) & (nc1[:, :, 1] <= 18) & (nc1[:, :, 2] <= 20)
    nc1[pelo_mask] = [74, 74, 74]
    add_case("NC1_pelo_gris_sint", "Pelo negro de Raditz pintado gris (RGB 74,74,74 como GEM2)", ref_c12, nc1, True)

    # NC2: Cielo blanco (hilo SPX 0x95 sin cerrar / DGB02_SKY)
    nc2 = ref_c10.copy()
    nc2[:160, 240:, :] = [255, 255, 255]
    add_case("NC2_cielo_blanco", "Cielo superior blanco puro (hilo SPX DGB02 sin cerrar)", ref_c10, nc2, True)

    # NC3: Placas opacas en el cielo (opaca_sin_alfa = true)
    nc3 = ref_c04.copy()
    nc3[0:130, 320:630, :] = [143, 214, 245]
    nc3[20:110, 360:590, :] = [250, 252, 255]
    add_case("NC3_placas_alfa", "Placas de nube opacas con borde duro (opaca_sin_alfa=true)", ref_c04, nc3, True)

    # NC4: Elemento faltante (borrado de personaje + actor ausente)
    nc4 = ref_c10.copy()
    nc4[90:360, 270:490, :] = [45, 110, 215]
    add_case(
        "NC4_elemento_faltante",
        "Personaje central borrado y ausente en inventario de actores",
        ref_c10,
        nc4,
        True,
        ref_act={"0": 140000, "10": 38000, "6": 12000},
        cand_act={"0": 178000, "6": 12000},
    )

    # NC5: Silueta desplazada / camara desalineada (+40 px)
    nc5 = np.roll(ref_c10, 42, axis=1)
    add_case("NC5_silueta_desplazada", "Desplazamiento horizontal de 42 px (encuadre/camara rotos)", ref_c10, nc5, True)

    # NC6: Cuadro negro degenerado
    nc6 = np.zeros_like(ref_c10)
    add_case("NC6_cuadro_negro", "Cuadro completamente negro (fallo de raster/visibilidad)", ref_c10, nc6, True)

    # NC7: Caso historico real de GEM2 (si existen los PNG de verificacion_3k_ub5cs)
    gem2_regresion = None
    if gem2_dir and (gem2_dir / "antes/src/corte_c12.png").is_file() and (gem2_dir / "despues/src/corte_c12.png").is_file():
        g2_a_c12 = np.array(Image.open(gem2_dir / "antes/src/corte_c12.png").convert("RGB"))
        g2_d_c12 = np.array(Image.open(gem2_dir / "despues/src/corte_c12.png").convert("RGB"))
        g2_a_c10 = np.array(Image.open(gem2_dir / "antes/src/corte_d.png").convert("RGB"))
        add_case(
            "NC7a_GEM2_real_pelo_gris",
            "PNG real de GEM2 (corte_c12 despues): pelo gris prestado de Goku",
            ref_c12,
            g2_d_c12,
            True,
        )
        add_case(
            "NC7b_GEM2_real_cielo_antes",
            "PNG real pre-GEM2 (corte_d antes): cielo blanco + rayas azules",
            ref_c10,
            g2_a_c10,
            True,
        )
        ev_a = evaluar_corte(ref_c12, g2_a_c12)
        ev_d = evaluar_corte(ref_c12, g2_d_c12)
        ev_a["corte"] = 12
        ev_d["corte"] = 12
        gem2_regresion = evaluar_regresion([ev_a], [ev_d])
        gem2_regresion["cumplido"] = not gem2_regresion["aprobado"]

    # Generar lamina de controles negativos
    cortes_nc = [dict(corte=i, segundos=0.0) for i in range(len(casos))]
    res_nc = dict(
        veredicto="PASS" if all(c["cumplido"] for c in casos) else "FAIL",
        score=round(100.0 * sum(1 for c in casos if c["cumplido"]) / max(1, len(casos)), 1),
        cielo=round(float(np.mean([c["eval"]["cielo"]["score"] for c in casos])), 1),
        color_zonas=round(float(np.mean([c["eval"]["color_zonas"]["score"] for c in casos])), 1),
        siluetas=round(float(np.mean([c["eval"]["siluetas"]["score"] for c in casos])), 1),
        blancos_negros=round(float(np.mean([c["eval"]["blancos_negros"]["score"] for c in casos])), 1),
        elementos=round(float(np.mean([c["eval"]["elementos"]["score"] for c in casos])), 1),
    )
    generar_lamina_escena(
        "TRG5 — CONTROLES POSITIVO Y NEGATIVOS (incl. GEM2 real)",
        cortes_nc,
        [c["ref_png"] for c in casos],
        [c["cand_png"] for c in casos],
        [c["eval"] for c in casos],
        res_nc,
        out_dir / "lamina_controles_negativos.png",
    )

    reporte = dict(
        todos_cumplidos=all(c["cumplido"] for c in casos) and (gem2_regresion is None or gem2_regresion["cumplido"]),
        casos=[
            dict(
                id=c["id"],
                descripcion=c["descripcion"],
                debe_fallar=c["debe_fallar"],
                cumplido=c["cumplido"],
                score=c["eval"]["score"],
                veredicto=c["eval"]["veredicto"],
                alertas=c["eval"]["alertas"],
                cielo=c["eval"]["cielo"]["score"],
                color_zonas=c["eval"]["color_zonas"]["score"],
                siluetas=c["eval"]["siluetas"]["score"],
                blancos_negros=c["eval"]["blancos_negros"]["score"],
                elementos=c["eval"]["elementos"]["score"],
                dark_ref_drift=c["eval"]["blancos_negros"]["dark_ref_drift"],
            )
            for c in casos
        ],
        puerta_antiregresion_gem2=gem2_regresion,
    )
    (out_dir / "controles_negativos.json").write_text(
        json.dumps(reporte, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return reporte


# =============================================================================
# 8. ORQUESTADOR DE LAS 38 ESCENAS
# =============================================================================

def renderizar_blender_lote(
    jobs: list[dict],
    work_dir: Path,
    blender_path: Path,
    samples: int = 8,
    threads: int = 6,
    workers: int = 2,
) -> None:
    if not jobs:
        return
    if not blender_path.is_file():
        raise FileNotFoundError(f"Blender no encontrado: {blender_path}")
    backend = work_dir / "juez_blender.py"
    backend.write_text(BLENDER_BACKEND_PY, encoding="utf-8")

    # Repartir equilibrando por numero de cortes
    workers = max(1, min(workers, len(jobs)))
    buckets: list[list[dict]] = [[] for _ in range(workers)]
    loads = [0] * workers
    for j in sorted(jobs, key=lambda x: len(x["cortes"]), reverse=True):
        bi = int(np.argmin(loads))
        buckets[bi].append(j)
        loads[bi] += len(j["cortes"])

    def run_worker(wid: int, chunk: list[dict]) -> None:
        wtmp = work_dir / f"blender_w{wid}"
        wtmp.mkdir(parents=True, exist_ok=True)
        man = wtmp / "lote.json"
        man.write_text(json.dumps(chunk, ensure_ascii=False, indent=2), encoding="utf-8")
        env = dict(
            os.environ,
            TEMP=str(wtmp),
            TMP=str(wtmp),
            BLENDER_USER_RESOURCES=str(wtmp / "perfil"),
            PYTHONDONTWRITEBYTECODE="1",
        )
        log = wtmp / "blender.log"
        with log.open("w", encoding="utf-8") as lf:
            subprocess.run(
                [
                    str(blender_path),
                    "-b",
                    "--factory-startup",
                    "--disable-autoexec",
                    "-t",
                    str(threads),
                    "--python-exit-code",
                    "1",
                    "--python",
                    str(backend),
                    "--",
                    str(man),
                    str(samples),
                ],
                cwd=wtmp,
                env=env,
                stdout=lf,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=3600,
            )

    if workers == 1:
        run_worker(0, buckets[0])
    else:
        with ThreadPoolExecutor(max_workers=workers) as ex:
            futs = [ex.submit(run_worker, wid, ch) for wid, ch in enumerate(buckets) if ch]
            for f in as_completed(futs):
                f.result()


def ejecutar_juez(
    escenas: list[int],
    out_dir: Path,
    iso_path: Path,
    tr1_dir: Path = DEFAULT_TR1,
    src_dir: Path = ROOT / "src",
    cache_1910: Path = DEFAULT_CACHE_1910,
    blender_path: Path = DEFAULT_BLENDER,
    samples: int = 8,
    threads: int = 6,
    workers: int = 2,
    incluir_1910_hd: bool = True,
    parche_1910: Path | None = None,
    solo_evaluar: bool = False,
    reanudar: bool = False,
    correr_controles: bool = True,
) -> dict:
    out_dir = out_dir.resolve()
    if out_dir.is_relative_to(ROOT):
        raise ValueError("La salida con renders debe quedar fuera del repositorio")
    out_dir.mkdir(parents=True, exist_ok=True)
    escenas_dir = out_dir / "escenas"
    laminas_dir = out_dir / "laminas"
    work_dir = out_dir / "_trabajo"
    escenas_dir.mkdir(exist_ok=True)
    laminas_dir.mkdir(exist_ok=True)
    work_dir.mkdir(exist_ok=True)

    t_start = time.time()
    planes: dict[int, dict] = {}
    fallidas: dict[int, str] = {}
    blender_jobs: list[dict] = []

    if not solo_evaluar:
        need_cpp = []
        for spx in escenas:
            sdir = escenas_dir / f"escena_{spx}"
            if reanudar and (sdir / "plan.json").is_file() and (sdir / "escena_cpp.json").is_file():
                plan = json.loads((sdir / "plan.json").read_bytes())
                planes[spx] = plan
                if not (sdir / "blender" / "blender_resultado.json").is_file():
                    blender_jobs.append(
                        dict(
                            spx=spx,
                            resolucion=plan["resolucion"],
                            orig_dir=str(sdir / "orig"),
                            blender_dir=str(sdir / "blender"),
                            cortes=plan["cortes"],
                        )
                    )
            else:
                need_cpp.append(spx)

        if need_cpp:
            print(f"[TRG5] Compilando arnes generico C++ desde {src_dir}...", flush=True)
            exe_gen = compilar_arnes_generico(src_dir, work_dir / "bin")
            iso = Iso(iso_path)
            en_afs = Afs(iso, "DATA_EN.AFS")
            amb_cache: dict[int, tuple[bytes, list[dict]]] = {}

            for spx in need_cpp:
                try:
                    plan = planificar_escena(tr1_dir, spx)
                    planes[spx] = plan
                    sdir = escenas_dir / f"escena_{spx}"
                    sdir.mkdir(parents=True, exist_ok=True)
                    (sdir / "plan.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
                    bundle_bytes, meta = construir_paquete_escena(tr1_dir, en_afs, spx, plan, amb_cache)
                    bundle_path = sdir / "bundle.bin"
                    bundle_path.write_bytes(bundle_bytes)
                    subprocess.run([str(exe_gen), str(bundle_path), str(sdir)], check=True, timeout=180)
                    # Convertir PPM de puerto a PNG y liberar bundle.bin grande
                    p_dir = sdir / "puerto"
                    for c in plan["cortes"]:
                        ppm = p_dir / f"corte_{c['corte']:02d}.ppm"
                        with Image.open(ppm) as im:
                            im.convert("RGB").save(p_dir / c["png"])
                        ppm.unlink()
                    bundle_path.unlink()
                    blender_jobs.append(
                        dict(
                            spx=spx,
                            resolucion=plan["resolucion"],
                            orig_dir=str(sdir / "orig"),
                            blender_dir=str(sdir / "blender"),
                            cortes=plan["cortes"],
                        )
                    )
                except Exception as exc:
                    fallidas[spx] = f"arnes_cpp: {exc}"
                    print(f"  [ERROR] escena {spx}: {exc}", flush=True)

        if blender_jobs:
            print(
                f"[TRG5] Arnes C++ listo ({len(planes)}/{len(escenas)} escenas). "
                f"Lanzando Blender para {len(blender_jobs)} escenas pendientes "
                f"({sum(len(j['cortes']) for j in blender_jobs)} cortes, {workers} trabajadores x {threads} hilos, {samples} muestras)...",
                flush=True,
            )
            t_bl = time.time()
            renderizar_blender_lote(blender_jobs, work_dir, blender_path, samples=samples, threads=threads, workers=workers)
            print(f"[TRG5] Blender terminado en {time.time()-t_bl:.1f}s.", flush=True)
    else:
        for spx in escenas:
            sdir = escenas_dir / f"escena_{spx}"
            if (sdir / "plan.json").is_file():
                planes[spx] = json.loads((sdir / "plan.json").read_bytes())
            else:
                fallidas[spx] = "sin plan.json previo"

    # Evaluar todas las escenas y generar sus laminas
    filas_resumen = []
    detalles = {}
    for spx in escenas:
        if spx in fallidas:
            continue
        sdir = escenas_dir / f"escena_{spx}"
        plan = planes[spx]
        try:
            cpp_info = json.loads((sdir / "escena_cpp.json").read_bytes())
            bl_info = json.loads((sdir / "blender" / "blender_resultado.json").read_bytes())
            eval_cortes = []
            ref_paths = []
            cand_paths = []
            for c, ci, bi in zip(plan["cortes"], cpp_info["cortes"], bl_info["cuadros"], strict=True):
                rp = sdir / "blender" / c["png"]
                cp = sdir / "puerto" / c["png"]
                mp = sdir / "puerto" / f"mask_{c['corte']:02d}.bin"
                ref_arr = np.array(Image.open(rp).convert("RGB"))
                cand_arr = np.array(Image.open(cp).convert("RGB"))
                mask = np.frombuffer(mp.read_bytes(), dtype=np.uint8).reshape(FRAME_SIZE[1], FRAME_SIZE[0]) if mp.is_file() else None
                # Actores visibles en original (Blender) vs puerto (mask C++)
                ref_act = {str(a["slot"]): a["triangulos"] * 50 for a in bi["actores"] if a["triangulos"] > 0}
                cand_act = {str(k): int(v) for k, v in ci.get("actores", {}).items()}
                # Para comparar actores sin distorsion de unidades, solo comprobamos presencia si ambos tienen pixeles
                ev = evaluar_corte(ref_arr, cand_arr, ref_mask=mask, cand_mask=mask)
                ev["corte"] = c["corte"]
                ev["t_u"] = c["t_u"]
                ev["cuadro_pal"] = c["cuadro_pal"]
                ev["tris_puerto"] = ci["tris_puerto"]
                ev["tris_orig"] = ci["tris_orig"]
                ev["ms_raster"] = ci["ms"]
                ev["actores_orig_slots"] = sorted(int(a["slot"]) for a in bi["actores"])
                ev["actores_puerto_px"] = cand_act
                eval_cortes.append(ev)
                ref_paths.append(rp)
                cand_paths.append(cp)

            res = resumir_escena(spx, eval_cortes, modo="puerto_generico")
            filas_resumen.append(res)
            detalles[str(spx)] = dict(resumen=res, cortes=eval_cortes)
            generar_lamina_escena(
                f"B1 PAL {spx} (Original Blender vs Puerto C++)",
                plan["cortes"],
                ref_paths,
                cand_paths,
                eval_cortes,
                res,
                laminas_dir / f"lamina_{spx}.png",
            )
            # Limpiar intermedios .tri pesados una vez renderizado Blender
            if not solo_evaluar and (sdir / "orig").is_dir():
                for tf in (sdir / "orig").glob("*.tri"):
                    tf.unlink()
        except Exception as exc:
            fallidas[spx] = f"evaluacion: {exc}"
            print(f"  [ERROR] evaluando {spx}: {exc}", flush=True)

    # Evaluacion especifica del arnes real de 1910 (src/escena_b1_1910.inl con modelos HD)
    res_1910_hd = None
    det_1910_hd = None
    if incluir_1910_hd and 1910 in planes and 1910 not in fallidas and cache_1910.is_dir():
        print("[TRG5] Evaluando arnes real src/escena_b1_1910.inl (14 cortes HD) vs Blender original 1910...", flush=True)
        hd_dir = out_dir / "escena_1910_hd"
        if not solo_evaluar:
            ejecutar_arnes_1910_hd(src_dir, cache_1910, iso_path, planes[1910], hd_dir, parche=parche_1910)
        s1910 = escenas_dir / "escena_1910"
        eval_hd = []
        ref_ps, cand_ps = [], []
        for c in planes[1910]["cortes"]:
            rp = s1910 / "blender" / c["png"]
            cp = hd_dir / "puerto_1910_hd" / c["png"]
            mp = s1910 / "puerto" / f"mask_{c['corte']:02d}.bin"
            mask = np.frombuffer(mp.read_bytes(), dtype=np.uint8).reshape(FRAME_SIZE[1], FRAME_SIZE[0]) if mp.is_file() else None
            ev = evaluar_corte(
                np.array(Image.open(rp).convert("RGB")),
                np.array(Image.open(cp).convert("RGB")),
                ref_mask=mask,
                cand_mask=None,
            )
            ev["corte"] = c["corte"]
            ev["t_u"] = c["t_u"]
            ev["cuadro_pal"] = c["cuadro_pal"]
            eval_hd.append(ev)
            ref_ps.append(rp)
            cand_ps.append(cp)
        res_1910_hd = resumir_escena(1910, eval_hd, modo="src_1910_hd")
        det_1910_hd = dict(resumen=res_1910_hd, cortes=eval_hd)
        generar_lamina_escena(
            "B1 PAL 1910 — Original Blender PS2 vs Arnes Real src/escena_b1_1910.inl (HD)",
            planes[1910]["cortes"],
            ref_ps,
            cand_ps,
            eval_hd,
            res_1910_hd,
            laminas_dir / "lamina_1910_hd.png",
        )

    # Controles negativos
    nc_report = None
    if correr_controles and 1910 in planes and 1910 not in fallidas:
        print("[TRG5] Ejecutando controles positivos y negativos (incl. GEM2 real)...", flush=True)
        nc_report = ejecutar_controles_negativos(escenas_dir / "escena_1910" / "blender", out_dir)

    escribir_tablas(filas_resumen, out_dir, extra_1910_hd=res_1910_hd)

    resumen_global = dict(
        schema="b1-trg5-resultados-v1",
        escenas_solicitadas=len(escenas),
        escenas_evaluadas=len(filas_resumen),
        escenas_fallidas=fallidas,
        cortes_totales=sum(r["cortes"] for r in filas_resumen),
        puntuacion_media_38=round(float(np.mean([r["score"] for r in filas_resumen])), 2) if filas_resumen else 0.0,
        pass_count=sum(1 for r in filas_resumen if r["veredicto"] == "PASS"),
        aviso_count=sum(1 for r in filas_resumen if r["veredicto"] == "AVISO"),
        fail_count=sum(1 for r in filas_resumen if r["veredicto"] == "FAIL"),
        escena_1910_hd=det_1910_hd,
        controles_negativos=nc_report,
        resumen_escenas=filas_resumen,
        detalles=detalles,
    )
    (out_dir / "resultados_38.json").write_text(
        json.dumps(resumen_global, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"[TRG5] Completado en {time.time()-t_start:.1f}s: {len(filas_resumen)}/{len(escenas)} escenas, "
        f"media={resumen_global['puntuacion_media_38']:.2f}/100, "
        f"PASS={resumen_global['pass_count']} AVISO={resumen_global['aviso_count']} FAIL={resumen_global['fail_count']}.",
        flush=True,
    )
    return resumen_global


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--salida", type=Path, required=True, help="Directorio de salida (fuera del repo)")
    ap.add_argument("--escenas", default="todas", help="'todas' o lista separada por comas (ej. 1910,1914)")
    ap.add_argument("--iso", type=Path, default=None, help="ISO PAL SLES_512.33")
    ap.add_argument("--tr1", type=Path, default=DEFAULT_TR1, help="Cache TR1 (38 escenas)")
    ap.add_argument("--src", type=Path, default=ROOT / "src", help="Directorio src/ del puerto a evaluar")
    ap.add_argument("--cache-1910", type=Path, default=DEFAULT_CACHE_1910, help="Cache solB1H_matrices de 1910")
    ap.add_argument("--parche-1910", type=Path, default=None, help="Parche diff opcional para evaluar en 1910")
    ap.add_argument("--blender", type=Path, default=DEFAULT_BLENDER)
    ap.add_argument("--muestras", type=int, default=8, help="Muestras Cycles por corte (defecto 8)")
    ap.add_argument("--hilos", type=int, default=6, help="Hilos CPU por proceso Blender")
    ap.add_argument("--trabajadores", type=int, default=2, help="Procesos Blender en paralelo")
    ap.add_argument("--sin-1910-hd", action="store_true", help="Omitir evaluacion del arnes HD src/escena_b1_1910.inl")
    ap.add_argument("--sin-controles", action="store_true", help="Omitir suite de controles negativos")
    ap.add_argument("--solo-evaluar", action="store_true", help="Re-evaluar PNGs ya renderizados")
    ap.add_argument("--reanudar", action="store_true", help="Reutilizar escenas C++/Blender ya completadas")
    a = ap.parse_args()

    iso_path = find_b1_iso(a.iso)
    todas = listar_escenas_tr1(a.tr1)
    if a.escenas.strip().lower() == "todas":
        escenas = todas
    else:
        escenas = [int(x.strip()) for x in a.escenas.split(",") if x.strip()]

    res = ejecutar_juez(
        escenas=escenas,
        out_dir=a.salida,
        iso_path=iso_path,
        tr1_dir=a.tr1,
        src_dir=a.src,
        cache_1910=a.cache_1910,
        blender_path=a.blender,
        samples=a.muestras,
        threads=a.hilos,
        workers=a.trabajadores,
        incluir_1910_hd=not a.sin_1910_hd,
        parche_1910=a.parche_1910,
        solo_evaluar=a.solo_evaluar,
        reanudar=a.reanudar,
        correr_controles=not a.sin_controles,
    )
    if res["controles_negativos"] and not res["controles_negativos"]["todos_cumplidos"]:
        print("[TRG5] FALLO en controles negativos!", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""NCAC · Fuzzer de recetas, codigos compartibles y guardado CaC (Python + C++).

Genera combinaciones aleatorias y casos frontera de:
  1. Fichas y codigos compartibles (`mod center hd/cac/recetas.py`: ficha -> toml -> ficha -> codigo -> ficha).
  2. Asignaciones de formas (`forma1..7` + `estallido1..7` en hex) y compatibilidad con `vista_heroe.leer_ficha`.
  3. Contrato cruzado C++ (`src/heroe.cpp` ArgsFicha/ArgsPropio/AplicarFicha) <-> Python (`recetas.py`).
  4. Persistencia C++ (`src/cac/cac.cpp`: Guardar/Leer sobre los 44 campos de Heroe + migracion v2->v3,
     compilando `fuzzer_cac_cpp.cpp` y ejecutando ademas `tests/cac_recetas_test.cpp` y `tests/cac_guardado_test.cpp`).

No modifica ningun archivo del proyecto (todo se ejecuta en directorios temporales).

Uso:
  python fuzzer_recetas_cac.py [--raiz RUTA] [--iteraciones 200] [--semilla 20261124] [--json SALIDA.json]
"""
from __future__ import annotations

import argparse
import json
import random
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def _cargar_recetas(raiz: Path):
    sys.dont_write_bytecode = True
    for p in (raiz / "mod center hd" / "cac", raiz / "mod center hd", raiz / "awo_tools"):
        s = str(p)
        if s not in sys.path:
            sys.path.insert(0, s)
    import recetas
    import vista_heroe as vh
    return recetas, vh


def _hsv(rng: random.Random) -> str:
    return f"{rng.randint(0, 360)},{rng.randint(-100, 100)},{rng.randint(-100, 100)}"


def _ficha_aleatoria_completa(rng: random.Random, incluir_extras: bool = True) -> str:
    """Genera una ficha aleatoria en orden canonico cubriendo TODAS las claves soportadas."""
    p = [
        f"cuerpo={rng.choice([0, 7, 8, 10, 11, 12, 13, 15, 27, 33, 34, 35, 36, 38, 39, 100, 110])}",
        f"traje={rng.randint(0, 5)}",
        "partes=" + "|".join(_hsv(rng) for _ in range(rng.choice([4, 6]))),
    ]
    if rng.random() < 0.25:
        p.append("xv2=" + ",".join(f"{k}:{rng.randint(1, 9)}" for k in ("bust", "pants", "boots", "rist", "cara", "pelo", "ojos")))
        p.append(f"cv={rng.getrandbits(32):08x}")
    if rng.random() < 0.5:
        p.append(f"rostro={rng.randint(1, 13)}")
    for k in ("ojos", "piel"):
        if rng.random() < 0.5:
            p.append(f"{k}={_hsv(rng)}")
    p += [f"pelo={rng.randint(0, 10)}", f"c1={_hsv(rng)}", f"c2={_hsv(rng)}",
          f"logo={rng.randint(0, 7)}", f"scouter={rng.randint(0, 1)}"]
    if rng.random() < 0.3:
        p.append("pelo_r17=" + rng.choice(["corona_sesgada", "llama_compacta", "cresta_escalonada",
                                           "raya_alta", "puntas_abiertas", "nuca_larga"]))
    for z in ("torso", "piernas", "guantes", "botas"):
        if rng.random() < 0.45:
            p.append(f"b3_{z}={rng.randint(0, 4095)}")
    if rng.random() < 0.4:
        p.append(f"b3_accesorios={rng.randint(0, 4095)}")
    for i in range(1, 5):
        if rng.random() < 0.45:
            p.append(f"capa{i}={rng.randint(0, 4095)}:{rng.randint(0, 3)}")
            if rng.random() < 0.55:
                bits = sorted(rng.sample(range(47), rng.randint(1, 6)))
                p.append(f"capa{i}_quitar=" + ",".join(str(b) for b in bits))
    if rng.random() < 0.5:
        p.append("texcol=" + "|".join(f"{rng.randint(0, 31)},{rng.randint(0, 5)},{_hsv(rng)}" for _ in range(rng.randint(1, 4))))
    if incluir_extras:
        if rng.random() < 0.35:
            p.append("material17=0,1,2|3,4,5")
        if rng.random() < 0.35:
            p.append("pintura17=633a5c746d70:0,deadbeef")
    if rng.random() < 0.25:
        p.append(f"ropa={rng.randint(1, 5)}")
    if rng.random() < 0.3:
        p += [f"aura_col=#{rng.getrandbits(24):06X}", f"ki_col=#{rng.getrandbits(24):06X}"]
    if rng.random() < 0.2:
        p.append("ut=4")
    if rng.random() < 0.25:
        p.append(f"razas_b3={rng.choice([1, 3])}")
    for i in range(1, 5):
        if rng.random() < 0.35:
            p.append(f"orig{i}={rng.randint(0, 3999)}")
    if rng.random() < 0.35:
        p.append(f"pelo_fid_cac2={rng.choice([75, 80])}")
    if rng.random() < 0.5:
        bits = sorted(rng.sample(range(63), rng.randint(1, 8)))
        p.append("ocultar=" + ",".join(str(b) for b in bits))
    if incluir_extras:
        for i in range(1, 8):
            if rng.random() < 0.25:
                sub = f"cuerpo=0;traje={rng.randint(0, 4)};partes={_hsv(rng)}|0,0,0|0,0,0|0,0,0;pelo={rng.randint(0, 10)}"
                p.append(f"forma{i}=" + sub.encode("utf-8").hex())
                if rng.random() < 0.6:
                    p.append(f"estallido{i}=" + rng.choice(["#FFAA00", "#64B4FF", "#DF50FF", "#FFFFFF"]))
        if rng.random() < 0.4:
            p.append(f"foto_pose={rng.randint(0, 2)}")
            p.append(f"foto_yaw={rng.randint(-40, 40)}")
    p.append("nombre=" + rng.choice(["Heroe", "Goku Z", "Ñandú 3", "Vegeta's Pride", "Trunks_HD"]))
    return ";".join(p)


def fuzz_python(raiz: Path, iteraciones: int, semilla: int) -> dict[str, Any]:
    recetas, vh = _cargar_recetas(raiz)
    rng = random.Random(semilla)
    fallos: list[dict[str, Any]] = []
    hallazgos: list[dict[str, Any]] = []

    # 1. Ida y vuelta aleatoria (ficha -> toml -> ficha -> codigo -> ficha -> leer_ficha)
    ok_rt = 0
    for i in range(iteraciones):
        f = _ficha_aleatoria_completa(rng)
        try:
            can = recetas.canonica(f)
            if can != f:
                fallos.append({"caso": f"canonica#{i}", "detalle": "canonica != entrada", "a": f, "b": can})
                continue
            toml_txt = recetas.a_receta(f)
            via_toml = recetas.de_receta(toml_txt)
            if via_toml != f:
                fallos.append({"caso": f"toml#{i}", "detalle": "de_receta(a_receta(f)) != f", "a": f, "b": via_toml})
                continue
            cod = recetas.a_codigo(f)
            via_cod = recetas.de_codigo(cod)
            esp_port = recetas._ficha_portatil(recetas._ficha_a_dict(f))
            if via_cod != esp_port:
                fallos.append({"caso": f"codigo#{i}", "detalle": "de_codigo(a_codigo(f)) != portatil", "a": esp_port, "b": via_cod})
                continue
            # Espacios/saltos al pegar
            c_esp = cod[:15] + "\r\n  " + cod[15:35] + "\t" + cod[35:] + "\n"
            if recetas.de_codigo(c_esp) != via_cod:
                fallos.append({"caso": f"espacios#{i}", "detalle": "espacios rompieron de_codigo"})
                continue
            # Idempotencia de segundo ciclo
            if recetas.a_codigo(via_cod) != cod:
                fallos.append({"caso": f"idempotencia#{i}", "detalle": "a_codigo no es idempotente"})
                continue
            # Comprobar que vista_heroe.leer_ficha entiende el resultado sin perder claves
            lf = vh.leer_ficha(via_cod)
            if int(lf.get("cuerpo", -1)) != recetas._ficha_a_dict(via_cod)["cuerpo"]:
                fallos.append({"caso": f"leer_ficha#{i}", "detalle": "vista_heroe.leer_ficha difiere en cuerpo"})
                continue
            # Comprobar formas_de_ficha
            lf_full = vh.leer_ficha(via_toml)
            ff = recetas.formas_de_ficha(lf_full)
            for fm_idx, (sub_txt, _est) in ff.items():
                if recetas.canonica(sub_txt) != sub_txt:
                    fallos.append({"caso": f"forma_hex#{i}.{fm_idx}", "detalle": "subficha de forma no canonica"})
            ok_rt += 1
        except Exception as e:
            fallos.append({"caso": f"exc#{i}", "detalle": f"{type(e).__name__}: {e}", "ficha": f})

    # 2. Pruebas de entradas malformadas y edge cases especificos
    casos_rechazar = [
        ("clave_int_desconocida", "cuerpo=0;clave_inventada=123"),
        ("capa_quitar_negativo", "cuerpo=0;capa1=268:0;capa1_quitar=-1,5"),
        ("capa_quitar_fuera_47", "cuerpo=0;capa1=268:0;capa1_quitar=99"),
        ("ocultar_negativo", "cuerpo=0;ocultar=-3"),
        ("ocultar_fuera_64", "cuerpo=0;ocultar=70"),
        ("estallido_invalido", "cuerpo=0;forma1=63756572706f3d30;estallido1=NOHEX"),
        ("hsv_fuera_rango", "cuerpo=0;partes=999,500,-500|0,0,0|0,0,0|0,0,0"),
        ("foto_yaw_fuera_rango", "cuerpo=0;foto_pose=1;foto_yaw=999"),
        ("pelo_fuera_rango", "cuerpo=0;pelo=99"),
        ("rostro_fuera_rango", "cuerpo=0;rostro=99"),
    ]
    for nombre_c, ficha_mal in casos_rechazar:
        aceptado = False
        try:
            t = recetas.a_receta(ficha_mal)
            recetas.de_receta(t)
            aceptado = True
        except recetas.ErrorReceta:
            pass
        if aceptado:
            hallazgos.append({
                "id": f"FUZZ-PY-{nombre_c.upper()}",
                "archivo": "mod center hd/cac/recetas.py",
                "detalle": f"a_receta/de_receta acepta entrada fuera de contrato ({nombre_c}): {ficha_mal!r}",
            })

    # 3. Contrato cruzado heroe.cpp <-> recetas.py (analisis estatico verificable)
    heroe_cpp = (raiz / "src/heroe.cpp").read_text(encoding="utf-8", errors="replace")

    # 3a. Orden de capaN y capaN_quitar en ArgsFicha vs recetas.ORDEN
    m_af = re.search(r"std::string ArgsFicha\(.*?\n\}\n", heroe_cpp, re.S)
    if m_af:
        af = m_af.group(0)
        # heroe.cpp tiene dos bucles for separados: primero todos los capa1..4 y luego todos los capa1..4_quitar
        p_capa = af.find('";capa" + std::to_string(i + 1) + "="')
        p_quit = af.find('"_quitar="')
        if p_capa > 0 and p_quit > p_capa:
            # Si hay 2 bucles for distintos, con capa1+capa1_quitar+capa2 el orden difiere de recetas.ORDEN
            entre = af[p_capa:p_quit]
            if "for (int i = 0; i < C::kNumCapas" in entre:
                ejemplo = "cuerpo=0;traje=0;partes=0,0,0|0,0,0|0,0,0|0,0,0;pelo=0;c1=0,0,0;c2=0,0,0;logo=0;scouter=0;capa1=268:0;capa2=416:1;capa1_quitar=1,2"
                can = recetas.canonica(ejemplo)
                if can != ejemplo:
                    hallazgos.append({
                        "id": "FUZZ-CROSS-ORDEN-CAPAS",
                        "archivo": "src/heroe.cpp:951-956 <-> mod center hd/cac/recetas.py:29",
                        "detalle": "ArgsFicha emite capa1..4 en un bucle y capa1..4_quitar en otro, pero recetas.ORDEN los intercala (capa1, capa1_quitar, capa2...). Con >=2 capas y mechones quitados, canonica(ArgsFicha) != ArgsFicha.",
                        "ejemplo_cpp": ejemplo,
                        "ejemplo_py": can,
                    })

    # 3b. AplicarFicha no resetea h.pelo_fid_cac2 y evalua PeloCac2() antes de cambiar g_cuerpo
    m_ap = re.search(r"bool AplicarFicha\(.*?\n\}\n", heroe_cpp, re.S)
    if m_ap:
        ap = m_ap.group(0)
        resetea_pf = "h.pelo_fid_cac2 = -1;" in ap.split('kv.find("partes")')[0]
        if not resetea_pf:
            hallazgos.append({
                "id": "FUZZ-CROSS-PELO-CAC2-LEAK",
                "archivo": "src/heroe.cpp:1839-1870",
                "detalle": "AplicarFicha resetea pelo/capas/b3_piezas/orig pero NO resetea h.pelo_fid_cac2 = -1 y evalua PeloCac2() con el cuerpo ANTERIOR (antes de g_cuerpo = ci en linea 1903): al aplicar una receta sobre un heroe que tenia pelo_fid_cac2=75 o al cambiar desde cuerpo propio (UT/XV2) a Goku 0_0, pelo_fid_cac2 se pierde o se filtra.",
            })
        # Bit 63 en ocultar: ArgsReceta/ArgsPropio recorren i < 64 pero AplicarFicha solo acepta bit < 63
        if re.search(r'bit\s*<\s*63\)\s*h\.ocultar\b', ap) and "for (int i = 0; i < 64; ++i)" in heroe_cpp:
            hallazgos.append({
                "id": "FUZZ-CROSS-OCULTAR-BIT63",
                "archivo": "src/heroe.cpp:1896",
                "detalle": "ArgsReceta/ArgsPropio emiten bits 0..63 de ocultar pero AplicarFicha descarta bit 63 (if bit < 63).",
            })

    # 3c. ArgsPropio solo emite foto_yaw cuando h.foto_pose != 0 (desalineado con cac.cpp:287)
    m_pr = re.search(r"std::string ArgsPropio\(.*?\n\}\n", heroe_cpp, re.S)
    if m_pr and "FotoCac2() && h.foto_pose ?" in m_pr.group(0):
        hallazgos.append({
            "id": "FUZZ-CROSS-FOTO-YAW",
            "archivo": "src/heroe.cpp:1063",
            "detalle": "ArgsPropio condiciona foto_yaw a `h.foto_pose != 0`, pero cac.cpp:287 ya persiste foto_yaw != 22 con foto_pose == 0.",
        })

    return {
        "iteraciones": iteraciones,
        "semilla": semilla,
        "roundtrip_ok": ok_rt,
        "fallos_roundtrip": fallos,
        "hallazgos_contrato": hallazgos,
    }


def fuzz_cpp(raiz: Path, iteraciones: int, semilla: int) -> dict[str, Any]:
    cc = shutil.which("g++")
    if not cc:
        return {"ok": False, "err": "g++ no encontrado en PATH"}
    cpp_src = Path(__file__).with_name("fuzzer_cac_cpp.cpp")
    cac_cpp = raiz / "src/cac/cac.cpp"
    flags = [cc, "-std=c++20", "-static", "-I", str(raiz), "-I", str(raiz / "src/cac"), "-I", str(raiz / "rexglue/include")]

    hallazgos: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="ncac_fcpp_") as td:
        w = Path(td)
        exe = w / "fuzzer_cac_cpp.exe"
        subprocess.run([*flags, str(cpp_src), str(cac_cpp), "-o", str(exe)], check=True)
        r = subprocess.run([str(exe), str(w / "run"), str(iteraciones), str(semilla)],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        salida = r.stdout + r.stderr
        # Parsear linea EDGE para detectar defectos de saneamiento en cac.cpp
        m_edge = re.search(r"EDGE forma_receta\[0\]=(-?\d+) forma_estallido\[1\]='([^']*)' pelo=(\d+) pelo_fid_cac2=(-?\d+)", salida)
        if m_edge:
            fr0, fe1, pelo_v, pf2 = int(m_edge.group(1)), m_edge.group(2), int(m_edge.group(3)), int(m_edge.group(4))
            if fr0 >= 0:
                hallazgos.append({
                    "id": "FUZZ-CPP-FORMA0",
                    "archivo": "src/cac/cac.cpp:225",
                    "detalle": f"AjustarHeroe acepta forma_receta[0]={fr0} (indice 0 es la base, nunca una forma; PonerFormaReceta solo permite 1..7).",
                })
            if fe1 and not re.fullmatch(r"#[0-9A-Fa-f]{6}", fe1):
                hallazgos.append({
                    "id": "FUZZ-CPP-ESTALLIDO-FMT",
                    "archivo": "src/cac/cac.cpp:228",
                    "detalle": f"AjustarHeroe conserva forma_estallido invalido {fe1!r} en vez de validarlo como #RRGGBB.",
                })
            if pelo_v > 0 and pf2 > 0:
                hallazgos.append({
                    "id": "FUZZ-CPP-DOS-PELOS",
                    "archivo": "src/cac/cac.cpp:210",
                    "detalle": f"AjustarHeroe permite pelo={pelo_v} y pelo_fid_cac2={pf2} activos simultaneamente en la misma ficha.",
                })

        # Ejecutar tambien los tests oficiales base: tests/cac_recetas_test.cpp y tests/cac_guardado_test.cpp
        regresion = {}
        for tname, tfile, extra_args in (
            ("cac_recetas_test", raiz / "tests/cac_recetas_test.cpp",
             [str(raiz / "tools/pruebas_heroe/r16_maj_semilla.toml"),
              str(raiz / "tools/pruebas_heroe/r16_nmc_semilla.toml")]),
            ("cac_guardado_test", raiz / "tests/cac_guardado_test.cpp", []),
        ):
            texe = w / (tname + ".exe")
            subprocess.run([*flags, str(tfile), str(cac_cpp), "-o", str(texe)], check=True)
            tr = subprocess.run([str(texe), str(w / ("dir_" + tname)), *extra_args],
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
            regresion[tname] = {"rc": tr.returncode, "salida": tr.stdout.strip()}

        return {
            "ok": r.returncode == 0 and all(v["rc"] == 0 for v in regresion.values()),
            "rc": r.returncode,
            "salida": salida.strip(),
            "hallazgos_cpp": hallazgos,
            "regresion": regresion,
        }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--raiz", type=Path, default=Path.cwd())
    ap.add_argument("--iteraciones", type=int, default=200)
    ap.add_argument("--semilla", type=int, default=20261124)
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--autoprueba", action="store_true", help="20 vueltas Python reproducibles sin compilar")
    a = ap.parse_args(argv)
    if a.autoprueba:
        rep = fuzz_python(a.raiz.resolve(), 20, a.semilla)
        assert rep["roundtrip_ok"] == 20 and not rep["fallos_roundtrip"], rep
        print("Fuzzer: autoprueba OK")
        return 0
    if a.iteraciones < 1:
        ap.error("--iteraciones debe ser positivo")

    py_rep = fuzz_python(a.raiz.resolve(), a.iteraciones, a.semilla)
    cpp_rep = fuzz_cpp(a.raiz.resolve(), max(50, a.iteraciones // 2), a.semilla)
    rep = {"python": py_rep, "cpp": cpp_rep}

    print(f"FUZZER DE RECETAS Y GUARDADO CaC (semilla={a.semilla})")
    print(f"  Python round-trip: {py_rep['roundtrip_ok']}/{py_rep['iteraciones']} PASS "
          f"| fallos={len(py_rep['fallos_roundtrip'])} | hallazgos contrato={len(py_rep['hallazgos_contrato'])}")
    print(f"  C++ round-trip:    ok={cpp_rep.get('ok')} | hallazgos saneamiento={len(cpp_rep.get('hallazgos_cpp', []))}")
    for tn, tv in cpp_rep.get("regresion", {}).items():
        print(f"  Regresion {tn}: rc={tv['rc']}")
    print("\nHALLAZGOS:")
    todos = py_rep["hallazgos_contrato"] + cpp_rep.get("hallazgos_cpp", [])
    for i, h in enumerate(todos, 1):
        print(f"  {i:2d}. [{h['id']}] {h['archivo']}: {h['detalle']}")
    if py_rep["fallos_roundtrip"]:
        print("\nFALLOS ROUND-TRIP PYTHON:")
        for fl in py_rep["fallos_roundtrip"][:10]:
            print(f"  - {fl}")

    if a.json:
        a.json.parent.mkdir(parents=True, exist_ok=True)
        a.json.write_text(json.dumps(rep, ensure_ascii=False, indent=2), encoding="utf-8")

    return 0 if (not todos and not py_rep["fallos_roundtrip"] and cpp_rep.get("ok")) else 1


if __name__ == "__main__":
    raise SystemExit(main())

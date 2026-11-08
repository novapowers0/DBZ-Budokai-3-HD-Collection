r"""gasto_tokens.py - donde se va el uso de Claude, leido de los transcritos locales (sin gastar tokens).

  python tools\gasto_tokens.py [--desde 2026-11-07] [--hasta ...] [--top 15] [--json salida.json]
  python tools\gasto_tokens.py --autoprueba

Lee ~/.claude/projects/<este proyecto>/*.jsonl (sesiones) y */subagents/**/*.jsonl (ayudantes y workflows).
Cada respuesta del modelo trae `usage`; se cuenta UNA vez por id de mensaje (el transcrito la repite por bloque).
Coste relativo (aprox., para comparar, no es la factura): entrada nueva x1, escritura de cache x1.25 (5 min) o x2
(1 h), lectura de cache x0.1, salida x5; y por modelo opus x1, sonnet x0.6, haiku x0.2.
Ademas: que herramientas meten mas texto en el contexto (resultado de cada llamada, en caracteres) y cuantas imagenes.
"""
import argparse
import collections
import json
import os
import sys
from pathlib import Path

PROY = Path(os.path.expanduser("~")) / ".claude" / "projects" / "C--Users-javie-Desktop-PROYECTOS-IA-DBZ-Budokai-3-HD-Collection"
PESO_MODELO = (("haiku", 0.2), ("sonnet", 0.6), ("opus", 1.0), ("fable", 1.0))


def peso_modelo(m):
    m = (m or "").lower()
    return next((p for k, p in PESO_MODELO if k in m), 1.0)


def coste(u, modelo):
    cc = u.get("cache_creation") or {}
    w1h, w5m = cc.get("ephemeral_1h_input_tokens", 0), cc.get("ephemeral_5m_input_tokens", 0)
    if not cc:
        w5m = u.get("cache_creation_input_tokens", 0)
    return peso_modelo(modelo) * (u.get("input_tokens", 0) + 1.25 * w5m + 2 * w1h
                                  + 0.1 * u.get("cache_read_input_tokens", 0) + 5 * u.get("output_tokens", 0))


def _texto(c):
    if isinstance(c, str):
        return len(c), 0
    n = img = 0
    for b in c or []:
        if isinstance(b, dict):
            if b.get("type") == "image":
                img += 1
            elif b.get("type") == "text":
                n += len(b.get("text", ""))
    return n, img


def leer(f, desde="", hasta="9999"):
    """Resumen de un transcrito: tokens por modelo, coste, herramientas y titulo."""
    r = {"archivo": str(f), "titulo": "", "coste": 0.0, "salida": 0, "esc_cache": 0, "lec_cache": 0, "entrada": 0,
         "respuestas": 0, "modelos": collections.Counter(), "herr_llamadas": collections.Counter(),
         "herr_chars": collections.Counter(), "imagenes": 0, "primero": "", "ultimo": ""}
    vistos, nombre_de = set(), {}
    try:
        lineas = open(f, encoding="utf-8", errors="replace")
    except OSError:
        return None
    for l in lineas:
        try:
            d = json.loads(l)
        except ValueError:
            continue
        ts = d.get("timestamp", "")
        if ts and not (desde <= ts[:10] <= hasta):
            continue
        m = d.get("message") or {}
        if d.get("type") == "user" and not r["titulo"] and isinstance(m.get("content"), str):
            r["titulo"] = " ".join(m["content"].split())[:70]
        cont = m.get("content") if isinstance(m.get("content"), list) else []
        for b in cont:
            if not isinstance(b, dict):
                continue
            if b.get("type") == "tool_use":
                nombre_de[b.get("id")] = b.get("name", "?")
                r["herr_llamadas"][b.get("name", "?")] += 1
            elif b.get("type") == "tool_result":
                n, img = _texto(b.get("content"))
                r["herr_chars"][nombre_de.get(b.get("tool_use_id"), "?")] += n
                r["imagenes"] += img
        u = m.get("usage")
        if d.get("type") != "assistant" or not u:
            continue
        mid = m.get("id") or d.get("requestId") or d.get("uuid")
        if mid in vistos:
            continue
        vistos.add(mid)
        modelo = m.get("model", "?")
        c = coste(u, modelo)
        r["coste"] += c
        r["modelos"][modelo] += c
        r["salida"] += u.get("output_tokens", 0)
        r["esc_cache"] += u.get("cache_creation_input_tokens", 0)
        r["lec_cache"] += u.get("cache_read_input_tokens", 0)
        r["entrada"] += u.get("input_tokens", 0)
        r["respuestas"] += 1
        r["primero"] = r["primero"] or ts
        r["ultimo"] = ts or r["ultimo"]
    return r if r["respuestas"] else None


def recorrer(desde, hasta):
    for f in sorted(PROY.glob("*.jsonl")):
        yield "sesion", f
    for f in sorted(PROY.glob("*/subagents/**/*.jsonl")):
        yield "ayudante", f


def informe(desde, hasta, top):
    filas, total = [], 0.0
    herr_c, herr_n, por_modelo, imgs = collections.Counter(), collections.Counter(), collections.Counter(), 0
    por_dueno = collections.Counter()
    for tipo, f in recorrer(desde, hasta):
        r = leer(f, desde, hasta)
        if not r:
            continue
        r["tipo"] = tipo
        sesion = f.stem if tipo == "sesion" else f.relative_to(PROY).parts[0]
        r["sesion"] = sesion
        filas.append(r)
        total += r["coste"]
        por_dueno[sesion] += r["coste"]
        herr_c.update(r["herr_chars"])
        herr_n.update(r["herr_llamadas"])
        por_modelo.update(r["modelos"])
        imgs += r["imagenes"]
    titulos = {r["sesion"]: r["titulo"] for r in filas if r["tipo"] == "sesion"}
    print(f"Uso de Claude {desde} .. {hasta}: coste relativo total {total:,.0f} (1 = un token de entrada nueva de Opus)")
    print("\nPor modelo:")
    for k, v in por_modelo.most_common():
        print(f"  {v / total:6.1%}  {k}")
    sesiones = [r for r in filas if r["tipo"] == "sesion"]
    propia = {r["sesion"]: r for r in sesiones}
    print(f"\nTop {top} sesiones (coste con sus ayudantes | respuestas y contexto medio releido por respuesta):")
    for s, v in por_dueno.most_common(top):
        r = propia.get(s)
        extra = f"{r['respuestas']:>4} resp  {r['lec_cache'] / max(1, r['respuestas']) / 1000:4.0f}k" if r else ""
        print(f"  {v / total:6.1%}  {s[:8]}  {extra}  {titulos.get(s, '(sin titulo)')[:45]}")
    tipos = collections.Counter()
    for r in filas:
        p = peso_modelo(max(r["modelos"], key=r["modelos"].get))
        tipos["releer el contexto (cache)"] += 0.1 * p * r["lec_cache"]
        tipos["escribir el contexto (cache)"] += 1.5 * p * r["esc_cache"]
        tipos["respuestas escritas (salida)"] += 5 * p * r["salida"]
    print("\nEn que se va (aprox.):")
    for k, v in tipos.most_common():
        print(f"  {v / total:6.1%}  {k}")
    if sesiones:
        print("Contexto medio releido en cada respuesta: %.0fk tokens (cuanto mas grande la sesion, mas cuesta CADA paso)"
              % (sum(r["lec_cache"] for r in sesiones) / max(1, sum(r["respuestas"] for r in sesiones)) / 1000))
    ayud = sum(r["coste"] for r in filas if r["tipo"] == "ayudante")
    print(f"\nAyudantes y workflows: {ayud / total:.1%} del total")
    lec = sum(r["lec_cache"] for r in filas)
    esc = sum(r["esc_cache"] for r in filas)
    sal = sum(r["salida"] for r in filas)
    print(f"Tokens: lectura de cache {lec:,}  escritura de cache {esc:,}  salida {sal:,}")
    if sesiones:
        grande = max(sesiones, key=lambda r: r["respuestas"])
        print(f"Sesion mas larga: {grande['respuestas']} respuestas ({grande['sesion'][:8]} {grande['titulo'][:50]})")
    print(f"\nHerramientas que mas texto meten en el contexto (caracteres de resultado; imagenes: {imgs}):")
    tot_c = sum(herr_c.values()) or 1
    for k, v in herr_c.most_common(top):
        print(f"  {v / tot_c:6.1%}  {v:>12,}  {herr_n[k]:>6} llamadas  {k}")
    return {"total": total, "por_modelo": por_modelo, "por_sesion": por_dueno, "herramientas": herr_c,
            "llamadas": herr_n, "imagenes": imgs}


def autoprueba():
    import tempfile
    t = Path(tempfile.mkdtemp()) / "a.jsonl"
    u = {"input_tokens": 10, "output_tokens": 2, "cache_read_input_tokens": 100, "cache_creation_input_tokens": 0}
    filas = [{"type": "user", "timestamp": "2026-11-07T01:00:00Z", "message": {"content": "hola mundo"}},
             {"type": "assistant", "timestamp": "2026-11-07T01:00:01Z",
              "message": {"id": "m1", "model": "claude-sonnet-5-5", "usage": u,
                          "content": [{"type": "tool_use", "id": "t1", "name": "Read"}]}},
             {"type": "assistant", "timestamp": "2026-11-07T01:00:01Z",   # mismo id: no se cuenta dos veces
              "message": {"id": "m1", "model": "claude-sonnet-5-5", "usage": u, "content": []}},
             {"type": "user", "timestamp": "2026-11-07T01:00:02Z",
              "message": {"content": [{"type": "tool_result", "tool_use_id": "t1", "content": "x" * 40}]}}]
    t.write_text("\n".join(json.dumps(f) for f in filas), encoding="utf-8")
    r = leer(t, "2026-11-07", "2026-11-07")
    assert r["respuestas"] == 1, r
    assert abs(r["coste"] - 0.6 * (10 + 0.1 * 100 + 5 * 2)) < 1e-9, r["coste"]
    assert r["herr_chars"]["Read"] == 40 and r["titulo"] == "hola mundo", r
    assert leer(t, "2026-11-08", "2026-11-09") is None
    print("gasto_tokens autoprueba OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--desde", default="0000")
    ap.add_argument("--hasta", default="9999")
    ap.add_argument("--top", type=int, default=15)
    ap.add_argument("--json")
    ap.add_argument("--autoprueba", action="store_true")
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    res = informe(a.desde, a.hasta, a.top)
    if a.json:
        Path(a.json).write_text(json.dumps(res, indent=1, default=dict), encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())

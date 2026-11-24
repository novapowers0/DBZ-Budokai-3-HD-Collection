r"""rotador_noche.py - overnight rotation: Gemini Neon improves (2 at a time), Sol applies.

  python tools\rotador_noche.py --hasta 09:00

- Neon: up to 2 lots from the TEMAS queue (each brief is generated from PLANTILLA_NEON). Every lot that ends
  with HECHO and has parches/ goes to the apply queue.
- If a Neon lot fails on quota ("Resets in", "quota") or login ("authentication"), the same brief goes to Sol
  (fallback slot, 1 at a time). Neon then rests until the reset (30 min on a login error).
- Sol applies: 1 at a time (it touches src/ and compiles). Brief generated from PLANTILLA_APLICAR.
- Extra lots to apply when their ESTADO line appears: --aplicar-tambien HB2B HIWB
- Every 25 min it writes E:\DBZ3HD_trabajo\fabrica\VIGILANTE.md (summary for the user and for the supervisor).
- After --ultimo-lanzamiento it launches nothing new; it waits for the running lots to finish or for --hasta.
ponytail: polling every 60 s, state in memory (if it dies, relaunch it; ESTADO.txt is the record).
"""
import argparse, datetime as dt, re, subprocess, sys, time
from pathlib import Path

R = Path(__file__).resolve().parents[1]
OC = Path(r"E:\DBZ3HD_trabajo\oc")
FAB = Path(r"E:\DBZ3HD_trabajo\fabrica")
ENC = R / "studies 5" / "ENTREGAS" / "R23" / "noche"
ESTADO = FAB / "ESTADO.txt"
VIG = FAB / "VIGILANTE.md"
NEON, SOL = "agy-gemini-4-neon-high", "sol"
CUOTA = re.compile(r"Resets in (?:(\d+)h)?(?:(\d+)m)?|quota|RESOURCE_EXHAUSTED|429", re.I)
AUTH = re.compile(r"authentication (failed|timed out)", re.I)

TEMAS = [
    ("NB2A", "B2 board: asset usage (sprites, portraits, icons, figures, effects). A census tool that compares which "
             "assets the original board uses on each screen/phase against what the port draws (data + decompiled code), "
             "lists the missing or swapped ones, and fixes in parches/ to use the real ones."),
    ("NIWA", "IW board/mission map: full flow (move around the map, mission card, Bulma's balloon, enter, result, return). "
             "A tool that simulates the original flow and compares it event by event with the port, with fixes in parches/."),
    ("NB2B", "B2 board animations: piece movement, steps, jumps, ally effects, radar, vignettes, transitions and the "
             "Shenron appearance. A tool that extracts timings/curves from the original and compares them with the port's; fixes."),
    ("NIWB", "IW minigames without a comparator (see E:\\DBZ3HD_trabajo\\fabrica\\HIW\\salida\\CENSO_IW.md): extend the "
             "generic comparator to as many of them as possible and fix the most visible differences."),
    ("NB2C", "B2 board events: dialogues, fights launched from a square, rewards, Dende/Popo, Kid Bu, closings. Expand "
             "the judge (tools/b2_juez*) to cover them and fix what doesn't match the original."),
    ("NIWC", "IW asset usage (models, textures, HUD, button icons, bars, fonts) in map and minigames: census original vs "
             "port, missing or reconstructed by hand, and fixes to use the real asset."),
    ("NB2D", "Iteration 2 on the B2 board: take the results of NB2A/NB2B/NB2C (read their INFORME.md), improve those "
             "tools (speed, coverage, a single entry point) and close the remaining differences."),
    ("NIWD", "Iteration 2 on IW: take NIWA/NIWB/NIWC, unify the tools into one entry point (census + comparator + "
             "assets) and close the remaining differences in the map and the minigames."),
]

PLANTILLA_NEON = """# {lote} ({quien}): {tema_corto}. Output E:\\DBZ3HD_trabajo\\fabrica\\{lote}

Read `studies 5/ENTREGAS/R23/REGLAS_GEM.md` and `REGLAS_POL157.md` first. They are mandatory.

## Topic
{tema}

## Goal
Leave tools that are BETTER than what we already have: reusable, scalable, with one clear entry point,
`--help` and a self-check. Improve fidelity to the original (port, not imitation: 1:1 with the original,
never replace native content).
- Before writing anything, look at what exists and reuse it, don't duplicate it: `tools/b2_oraculo/`, `tools/b2_juez*.py`,
  `tools/iw_mips/`, `tools/astra_iw_*`, and the deliveries `E:\\DBZ3HD_trabajo\\fabrica\\HB2`, `HB2B`, `HIW`, `HIWB`
  (read their INFORME.md).
- Logic comes from the decompiled code (`E:\\DBZ3HD_trabajo\\ghidra\\salida`) and the emulator-free interpreter
  (`tools/iw_mips/ee.py`). The game/emulator only validates once, briefly.

## Rules
- Don't edit `src/`, `tools/` or `res/`. Everything goes in your folder: `scripts/`, `parches/` (diffs for `src/` with their proof:
  `git apply --check` against the current tree), `INFORME.md` (verified / NOT verified), `verificar.py` (exit 0).
- No more than 1 game at a time, through `tools/pruebas.py`, with fixed copies only (`smoke_*`, `opus2`, `sol`).
- Add the line to `E:\\DBZ3HD_trabajo\\fabrica\\ESTADO.txt`: `{lote} HECHO HH:MM <folder> | summary` (or BLOQ).
- Don't publish anything.
- Run Python directly (`python tools/pruebas.py ...`), NEVER through `bash -lc "..."`: quoting gets broken and it opens an interactive console that fills the disk with errors.
"""

PLANTILLA_APLICAR = """# AP_{lote} (Sol): APPLY the improvements of {lote}. Output E:\\DBZ3HD_trabajo\\fabrica\\AP_{lote}

The user authorized (24/11, overnight) Sol to apply the improvements Gemini Neon delivers. Source: `E:\\DBZ3HD_trabajo\\fabrica\\{lote}\\`
(read INFORME.md, parches/, scripts/, verificar.py).

## Steps
1. **Run its `verificar.py` as a skeptic.** For each diff in `parches/`, run `git apply --check` against the current tree. Separate:
   - what has real proof;
   - what is inflated or not verified (don't apply that).
2. **Backup:** before touching anything, copy each file you'll change to `E:\\DBZ3HD_trabajo\\respaldo_integracion_AP_{lote}\\`.
3. **Apply** to `src/` (and the scripts worth keeping, into `tools/` with `--help`, documented in `docs/04_herramientas/TOOLS.md`).
   - New behavior that changes the visible look or rules goes behind a cvar.
   - It's ON by default only if it brings the port closer to the original and passes its test.
4. **Compile** with `python tools/compilar.py`. If it doesn't compile, revert from the backup.
5. **Test:** only the affected scenario, with `tools/pruebas.py correr <scenario>` (no `--forzar`, fixed copies, no more than 1 game).
   - Also run the source tool/judge again.
   - If something gets worse, revert that part.
6. **Don't touch `github/`** and don't publish anything.
- Run Python directly (`python tools/pruebas.py ...`), NEVER through `bash -lc "..."`: quoting gets broken and it opens an interactive console that fills the disk with errors.

## Deliver
- `INFORME.md`: applied / not applied and why, compile result, tests, list of changed files.
- `verificar.py` (exit 0).
- The line in ESTADO: `AP_{lote} HECHO HH:MM <folder> | summary`.
"""


def ahora():
    return dt.datetime.now()


def hora(s):
    h, m = map(int, s.split(":"))
    t = ahora().replace(hour=h, minute=m, second=0, microsecond=0)
    return t if t > ahora() else t + dt.timedelta(days=1)


def log(msg):
    linea = f"{ahora():%H:%M} {msg}"
    print(linea, flush=True)
    with open(OC / "rotador_noche.log", "a", encoding="utf-8") as f:
        f.write(linea + "\n")


def vivo(pid):
    out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True).stdout
    return str(pid) in out


def lanzar(nombre, encargo, modelo):
    out = subprocess.run([sys.executable, str(R / "tools" / "oc.py"), "lanzar", str(encargo), "--modelo", modelo,
                          "--nombre", nombre], cwd=R, capture_output=True, text=True).stdout
    m = re.search(r"pid (\d+)", out)
    log(f"launched {nombre} ({modelo}) {'pid ' + m.group(1) if m else 'FAILED: ' + out.strip()[-200:]}")
    return (int(m.group(1)) if m else 0), OC / f"oc_{nombre}.log"


def linea_estado(lote):
    if not ESTADO.exists():
        return ""
    ult = ""
    for l in ESTADO.read_text(encoding="utf-8", errors="replace").splitlines():
        if l.startswith(lote + " "):
            ult = l
    return ult


def tiene_parches(lote):
    d = FAB / lote / "parches"
    return d.is_dir() and any(d.glob("*.diff")) or (d.is_dir() and any(d.glob("*.patch")))


def escribir(nombre, texto):
    ENC.mkdir(parents=True, exist_ok=True)
    p = ENC / f"ENCARGO_{nombre}.md"
    p.write_text(texto, encoding="utf-8")
    return p


def fallo_cuota(logp):
    t = Path(logp).read_text(encoding="utf-8", errors="replace")[-6000:] if Path(logp).exists() else ""
    if AUTH.search(t):
        return 1800
    m = re.search(r"Resets in (?:(\d+)h)?(?:(\d+)m)?", t)
    if m:
        return int(m.group(1) or 0) * 3600 + int(m.group(2) or 0) * 60 + 120
    return 1800 if CUOTA.search(t) and len(t) < 3000 else 0   # long log = it worked; a stray mention is not a failure


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hasta", default="09:00")
    ap.add_argument("--ultimo-lanzamiento", default="08:00")
    ap.add_argument("--aplicar-tambien", nargs="*", default=[])
    ap.add_argument("--aplicador", default="sol", help="model that applies (sol or neon)")
    ap.add_argument("--sin-fallback", action="store_true", help="if Neon runs out of quota, wait for the reset (don't send to Sol)")
    ap.add_argument("--temas-json", help="[[lote, tema], ...] instead of TEMAS")
    ap.add_argument("--ocupan", nargs="*", type=int, default=[], help="pids of Neon lots already running (take a slot)")
    a = ap.parse_args()
    fin, corte = hora(a.hasta), hora(a.ultimo_lanzamiento)
    import json
    temas = [tuple(x) for x in json.loads(Path(a.temas_json).read_text(encoding="utf-8"))] if a.temas_json else list(TEMAS)
    neon = []            # (lote, pid, log, encargo)
    sol_fb = []          # Neon fallback on Sol
    sol_ap = []          # Sol applying
    cola_ap = []         # lots to apply
    esperar_ap = list(a.aplicar_tambien)
    neon_libre = ahora()
    tema_de = dict(temas)
    hechos, prox_vig = [], ahora()
    log(f"start: until {fin:%H:%M}, last launch {corte:%H:%M}, {len(temas)} topics")
    while ahora() < fin:
        puede = ahora() < corte
        # Neon
        for t in list(neon):
            lote, pid, lg, enc = t
            if vivo(pid):
                continue
            neon.remove(t)
            espera = fallo_cuota(lg)
            if espera and not linea_estado(lote):
                neon_libre = ahora() + dt.timedelta(seconds=espera)
                log(f"{lote}: Neon out of quota/login (rest {espera // 60} min) -> goes to Sol")
                txt = Path(enc).read_text(encoding="utf-8").replace("(Gemini Neon)", "(Sol, Neon fallback)")
                e2 = escribir(lote + "_SOL", txt)
                if a.sin_fallback:
                    temas.insert(0, (lote, tema_de.get(lote, txt)))   # retried with Neon after the reset
                    continue
                pid2, lg2 = lanzar(lote + "sol", e2, SOL) if puede else (0, None)
                if pid2:
                    sol_fb.append((lote, pid2, lg2, e2))
                continue
            est = linea_estado(lote)
            log(f"{lote}: Neon finished -> {est[:160] or 'no ESTADO line'}")
            hechos.append(lote)
            if "HECHO" in est and tiene_parches(lote):
                cola_ap.append(lote)
        for t in list(sol_fb):
            lote, pid, lg, enc = t
            if vivo(pid):
                continue
            sol_fb.remove(t)
            est = linea_estado(lote)
            log(f"{lote}: Sol (fallback) finished -> {est[:160] or 'no ESTADO line'}")
            hechos.append(lote)
            if "HECHO" in est and tiene_parches(lote):
                cola_ap.append(lote)
        for t in list(sol_ap):
            lote, pid, lg, enc = t
            if not vivo(pid):
                sol_ap.remove(t)
                log(f"AP_{lote}: finished -> {linea_estado('AP_' + lote)[:160] or 'no ESTADO line'}")
        for lote in list(esperar_ap):
            est = linea_estado(lote)
            if est:
                esperar_ap.remove(lote)
                if "HECHO" in est and tiene_parches(lote):
                    cola_ap.append(lote)
                    log(f"{lote}: ESTADO seen, to the apply queue")
        ocupados = [p for p in a.ocupan if vivo(p)]
        # launch
        while puede and temas and len(neon) + len(ocupados) < 2 and ahora() >= neon_libre:
            lote, tema = temas.pop(0)
            enc = escribir(lote, PLANTILLA_NEON.format(lote=lote, quien="Gemini Neon high", tema=tema,
                                                       tema_corto=tema.split(":")[0]))
            pid, lg = lanzar("neon" + lote, enc, NEON)
            if pid:
                neon.append((lote, pid, lg, enc))
            else:
                temas.insert(0, (lote, tema))
                break
        # topics waiting for Neon while it rests: one goes to Sol if the fallback slot is free
        if puede and temas and ahora() < neon_libre and not sol_fb and not a.sin_fallback:
            lote, tema = temas.pop(0)
            enc = escribir(lote + "_SOL", PLANTILLA_NEON.format(lote=lote, quien="Sol, Neon fallback", tema=tema,
                                                                 tema_corto=tema.split(":")[0]))
            pid, lg = lanzar(lote + "sol", enc, SOL)
            if pid:
                sol_fb.append((lote, pid, lg, enc))
        if puede and cola_ap and not sol_ap:
            lote = cola_ap.pop(0)
            enc = escribir("AP_" + lote, PLANTILLA_APLICAR.format(lote=lote))
            mod = NEON if a.aplicador == "neon" and ahora() >= neon_libre else SOL
            if a.aplicador == "neon" and mod == SOL:
                cola_ap.insert(0, lote)   # Neon is resting: the apply waits, Sol is not spent
                time.sleep(60)
                continue
            pid, lg = lanzar(("neonAP" if mod == NEON else "solAP") + lote, enc, mod)
            if pid:
                sol_ap.append((lote, pid, lg, enc))
        # supervisor
        if ahora() >= prox_vig:
            prox_vig = ahora() + dt.timedelta(minutes=25)
            VIG.write_text(
                f"# Overnight supervisor - {ahora():%d/%m %H:%M}\n\n"
                f"- Neon running: {', '.join(l for l, *_ in neon) or '-'} (Neon free from {neon_libre:%H:%M})\n"
                f"- Sol fallback: {', '.join(l for l, *_ in sol_fb) or '-'}\n"
                f"- Sol applying: {', '.join('AP_' + l for l, *_ in sol_ap) or '-'}\n"
                f"- Apply queue: {', '.join(cola_ap) or '-'}; waiting for ESTADO: {', '.join(esperar_ap) or '-'}\n"
                f"- Topics pending: {', '.join(l for l, _ in temas) or '-'}\n"
                f"- Finished: {', '.join(hechos) or '-'}\n\n## ESTADO lines from tonight\n"
                + "\n".join(f"- {linea_estado(x)[:220]}" for x in hechos + [f'AP_{y}' for y in hechos] if linea_estado(x))
                + "\n", encoding="utf-8")
        if not (neon or ocupados or sol_fb or sol_ap or cola_ap or esperar_ap) and (not temas or not puede):
            break
        time.sleep(60)
    log("end of rotator")


if __name__ == "__main__":
    main()

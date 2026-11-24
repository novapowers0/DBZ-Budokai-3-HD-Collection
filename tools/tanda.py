r"""tanda.py - tandas de sesiones baratas: encargos con las reglas de ahorro dentro y un estado que se lee SIN IA.

  python tools\tanda.py encargos  studies\ 5\TANDAS\tanda3.toml   -> un encargo .md por sesion (para start_session)
  python tools\tanda.py pon       tanda3 iw-campo "PASS 7D0; sigue 7D2"   (cada sesion, al acabar cada turno)
  python tools\tanda.py estado    [tanda3]       -> tabla de una linea por sesion + cola del emulador + atascos
  python tools\tanda.py item      B2-03 HECHO "D:/DBZ3HD/_pruebas/2026.../ | nota"   (PEND/CURSO/HECHO/BLOQ/FUERA)
  python tools\tanda.py tablero   [B2]           -> hechos/total por frente del plan + puntos en curso o bloqueados
  python tools\tanda.py --autoprueba

Los puntos son las filas `| ID | ...` de studies 5/PLAN_DEFINITIVO.md (unica lista); su estado vive en un registro
que solo crece (ITEMS). HECHO exige la ruta, que exista, de la prueba o captura: sin evidencia no hay hecho.

Por que (medido 8/11 con tools/gasto_tokens.py): el 75 % del gasto es releer el contexto en cada paso. El
coordinador no debe leer transcritos (list_events) ni despertarse para "sin novedades": lee `estado` (unas lineas).

tanda.toml:
    nombre = "tanda3"
    [[encargo]]
    nombre = "iw-campo"            # corto, [a-z0-9-]
    modelo = "sonnet"              # sonnet (por defecto) | opus (solo ingenieria inversa dificil) | haiku
    archivos = ["src/iw_mini_campo.cpp"]   # los UNICOS que puede editar
    objetivo = "..."               # que hacer
    hecho = "..."                  # como se comprueba que esta hecho
"""
import argparse
import json
import os
import re
import sys
import time
import tomllib
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
COORD = Path(r"E:\DBZ3HD_trabajo\coord\tandas")
EMU = Path(r"E:\DBZ3HD_trabajo\emu_turnos\registro.jsonl")
ATASCO_MIN = 45
PLAN = RAIZ / "studies 5" / "PLAN_DEFINITIVO.md"
ITEMS = Path(r"E:\DBZ3HD_trabajo\coord\plan\items.tsv")
ESTADOS = ("PEND", "CURSO", "HECHO", "BLOQ", "FUERA")
_FILA = re.compile(r"^\|\s*([A-Z][A-Z0-9]*-[A-Z0-9]+[a-z]?)\s*\|\s*([^|]*)\|")

REGLAS = """## Reglas (obligatorias; son el método de `studies 5/PLAN_DEFINITIVO.md` §2)
- Trabaja de verdad: el encargo lleva horas; NO pares tras el reconocimiento. Largo o difícil no es un bloqueo.
  `BLOQUEADO:` solo si falta algo externo (una decisión del usuario, un archivo o dato que no existe).
- Tus puntos son las filas del plan con tu prefijo (Grep `| PREFIJO-` en `studies 5/PLAN_DEFINITIVO.md`). Al empezar
  uno: `python tools/tanda.py item <ID> CURSO`. Al cerrarlo: `python tools/tanda.py item <ID> HECHO "<ruta de la
  prueba o captura> | nota"` (sin una ruta que exista, la orden lo rechaza). Si no se iguala tras 2 intentos serios:
  `python tools/tanda.py item <ID> BLOQ "<qué falta>"` y pasa al siguiente. No existe el estado «dudoso».
- Nada de estudios, planes ni índices nuevos. Una duda sobre el original se resuelve leyendo el decompilado
  (`E:\\DBZ3HD_trabajo\\ghidra\\salida`) o el código, no documentos viejos, y se anota en una línea.
- Pruebas del juego: las lanza y las lee un ayudante Haiku (Agent, model haiku) con un encargo así: «Ejecuta
  <orden exacta>. Devuelve PASS o FAIL, la expectativa que falló, 5 líneas del registro como mucho y las rutas de
  las capturas; si falla, di si es del guion, de la máquina o del juego.» Mientras, tú sigues con el punto
  siguiente. Varias pruebas a la vez = varios Haiku, uno por copia de prueba. No leas registros enteros.
- Antes de correr un guion, comprueba que cada texto que espera existe en `src/` (Grep; o `python tools/pruebas.py
  preflight <guion>` cuando exista). Solo el escenario afectado, sin `--forzar` y sin `DBZ3_TEST_EXCLUSIVA` (salvo
  al medir imágenes por segundo). Nunca repitas una prueba sin un cambio entre medias. No crees copias del juego.
- Lo visual se acepta con una captura a escala 0,5 al lado de su referencia (original, maqueta o combate de B3) y
  un juez que no lo programó (ayudante Haiku; Sonnet si es fino): SE PARECE, CASI o NO, con diferencias numeradas.
  Tú solo miras una captura si el juez dice que no coincide. Un PASS de registro no vale para lo visual.
- Compilar: `python tools/compilar.py` tras cada cambio pequeño; no dejes el árbol roto al acabar un turno. Si
  falla en un archivo que no es tuyo, espera 2 minutos y reintenta una vez.
- Archivos: solo tu zona. En los compartidos (CMakeLists, i18n, `main.cpp`, `pasarela.cpp`, `tools/pruebas.py`)
  solo añadidos mínimos de una línea, sin reformatear. No esperes a otro carril: si necesitas algo de su zona,
  pídelo en tu línea de estado y sigue con otro punto.
- No hay git: antes de editar un archivo por primera vez, deja una copia en `E:\\DBZ3HD_trabajo\\respaldos_r23\\{nombre}\\`
  (del inicio de la tanda ya hay una en `E:\\DBZ3HD_trabajo\\respaldos_r23\\inicio`).
- Arte: primero piezas del propio juego leídas de la copia del usuario; nada de primitivas ni caras prestadas en
  lo final; no se embarca arte del juego. El arte que solo pueda venir de un encargo, apúntalo en tu informe
  («arte por encargar») y usa mientras arte oficial del juego. Lo nuevo nace detrás de su interruptor; no cambies
  ningún valor por defecto de lo traducido del original; Tag Team y Online siguen en beta.
- Contexto: Grep primero; Read con offset/limit, nunca más de 800 líneas; recorta la salida de las órdenes
  (`| Select-Object -Last 15`); órdenes largas en segundo plano y sin sondear. Como mucho 3 ayudantes a la vez.
  Si al cerrar un punto pasas de ~300k tokens (`get_usage` "self"; si no tienes esa herramienta: tras cerrar 3 puntos
  o unas 3 horas de trabajo), escribe tu estado en `{estado_md}` y termina con una línea `RELEVO:`.
- Al acabar CADA turno: `python tools/tanda.py pon {tanda} {nombre} "<en qué estás>"`. Al terminar del todo, que
  empiece por `HECHO:`, `RELEVO:` o `BLOQUEADO:`.
- Informe único del carril, 10 líneas por punto como mucho: `studies 5/ENTREGAS/R23/{informe}`.
- Sin commit ni push; no se publica nada. Nunca borres: mueve a `D:\\_PARA_BORRAR_CLAUDE`. Temporales en
  `E:\\DBZ3HD_trabajo`. No toques `E:/Emuladores/Retrobat/saves`, `.claude/`, `*.local.inc` ni credenciales. No mates
  procesos que no lanzaste. `DBZ3_TEST_*` solo a través de `tools/pruebas.py`; PCSX2 solo con `tools/pcsx2_pool.py`
  (una validación corta por unidad, con trucos y a x2-x3). La IA local (Qwen) no se usa mientras haya pruebas.
"""


def _ruta_estado(tanda, nombre):
    return COORD / tanda / f"{nombre}.estado"


def encargos(toml_path, salida=None):
    cfg = tomllib.loads(Path(toml_path).read_text(encoding="utf-8-sig"))   # -sig: PowerShell 5.1 escribe BOM
    tanda = cfg["nombre"]
    d = Path(salida) if salida else COORD / tanda
    d.mkdir(parents=True, exist_ok=True)
    hechos = []
    for e in cfg["encargo"]:
        n = e["nombre"]
        modelo = e.get("modelo", "sonnet")
        assert modelo in ("sonnet", "opus", "haiku"), f"{n}: modelo {modelo}"
        informe = f"{tanda.upper()}_{n.upper().replace('-', '_')}.md"
        texto = (f"# Encargo {tanda}/{n} (modelo: {modelo})\n\n{e['objetivo'].strip()}\n\n"
                 f"**Tus archivos (los únicos que editas):** {', '.join('`%s`' % a for a in e.get('archivos', [])) or '(solo lectura)'}\n\n"
                 f"**Hecho significa:** {e['hecho'].strip()}\n\n"
                 + REGLAS.format(estado_md=_ruta_estado(tanda, n).with_suffix('.relevo.md'), tanda=tanda, nombre=n,
                                 informe=informe))
        (d / f"{n}.md").write_text(texto, encoding="utf-8")
        hechos.append((n, modelo, d / f"{n}.md"))
    for n, m, p in hechos:
        print(f"{n:<20} {m:<7} {p}")
    return hechos


def pon(tanda, nombre, texto):
    p = _ruta_estado(tanda, nombre)
    p.parent.mkdir(parents=True, exist_ok=True)
    with open(p, "a", encoding="utf-8") as f:
        f.write(f"{time.strftime('%Y-%m-%d %H:%M')}\t{' '.join(texto.split())[:300]}\n")


def estado(tanda=None, ahora=None):
    ahora = ahora or time.time()
    tandas = [COORD / tanda] if tanda else sorted((p for p in COORD.glob("*") if p.is_dir()),
                                                  key=lambda p: p.stat().st_mtime)[-1:]
    lineas = []
    for t in tandas:
        for p in sorted(t.glob("*.estado")):
            ult = p.read_text(encoding="utf-8").strip().splitlines()[-1:] or [""]
            hora, _, txt = ult[0].partition("\t")
            mins = (ahora - p.stat().st_mtime) / 60
            marca = ("ok " if txt.startswith("HECHO") else "!! " if txt.startswith("BLOQUEADO")
                     else ">> " if txt.startswith("RELEVO")
                     else f"?? {mins:.0f}min " if mins > ATASCO_MIN else "   ")
            lineas.append(f"{marca}{t.name}/{p.stem:<18} {hora}  {txt[:150]}")
    if EMU.is_file():
        u = EMU.read_text(encoding="utf-8", errors="replace").strip().splitlines()[-1:]
        if u:
            e = json.loads(u[0])
            lineas.append(f"    emulador: {e.get('hora', '')[11:16]} {e.get('evento')} {e.get('nombre', '')}"
                          f" {'' if e.get('ok', True) else 'FALLO ' + e.get('error', '')}")
    try:  # encargos de opencode lanzados con tools/oc.py (vivos o de las ultimas 24 h)
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import oc
        lineas += ["    oc " + f for f in oc.estado(ahora, imprimir=False)]
    except ImportError:
        pass
    print("\n".join(lineas) or "(sin tandas)")
    return lineas


def _catalogo():
    filas = map(_FILA.match, PLAN.read_text(encoding="utf-8-sig").splitlines())
    return {m.group(1): m.group(2).strip() for m in filas if m}


def item(id_, est, evidencia="", quien=""):
    est = est.upper()
    if est not in ESTADOS:
        sys.exit(f"estado {est}: usa {'/'.join(ESTADOS)}")
    if id_ not in _catalogo():
        sys.exit(f"{id_} no es un punto de {PLAN.name}")
    if est == "HECHO":   # sin evidencia no hay hecho: la ruta de la prueba o captura tiene que existir
        ruta = evidencia.split(" | ")[0].strip()
        if not ruta or not (RAIZ / ruta).exists():
            sys.exit('HECHO exige una ruta que exista (prueba o captura): item ID HECHO "ruta | nota"')
    ITEMS.parent.mkdir(parents=True, exist_ok=True)
    with open(ITEMS, "a", encoding="utf-8") as f:
        f.write("\t".join([time.strftime("%Y-%m-%d %H:%M"), id_, est, quien or os.environ.get("DBZ3_AGENTE", ""),
                           " ".join(evidencia.split())[:300]]) + "\n")


def tablero(prefijo=""):
    ult = {}
    if ITEMS.is_file():
        for l in ITEMS.read_text(encoding="utf-8").splitlines():
            c = l.split("\t")
            if len(c) >= 5:
                ult[c[1]] = c
    frentes = {}
    for i, titulo in _catalogo().items():
        if i.startswith(prefijo):
            c = ult.get(i)
            frentes.setdefault(i.split("-")[0], []).append((i, c[2] if c else "PEND", titulo, c))
    lineas = []
    for fr, its in frentes.items():
        n = {e: sum(1 for x in its if x[1] == e) for e in ESTADOS}
        lineas.append(f"{fr:<6} {n['HECHO']}/{len(its) - n['FUERA']} hechos"
                      + "".join(f", {n[e]} {e.lower()}" for e in ("CURSO", "BLOQ", "FUERA") if n[e]))
    for its in frentes.values():
        for i, est, titulo, c in its:
            if est in ("CURSO", "BLOQ") or (prefijo and est == "PEND"):
                lineas.append(f"  {est:<5} {i:<9} {titulo[:70]}" + (f"  [{c[3]} {c[0][5:]}] {c[4][:80]}" if c else ""))
    print("\n".join(lineas) or "(plan sin puntos)")
    return lineas


def autoprueba():
    import tempfile
    global COORD, EMU, PLAN, ITEMS
    viejo = COORD, EMU, PLAN, ITEMS
    COORD = Path(tempfile.mkdtemp())
    EMU = COORD / "nada.jsonl"
    PLAN, ITEMS = COORD / "plan.md", COORD / "plan" / "items.tsv"
    try:
        PLAN.write_text("| ID | Punto |\n|---|---|\n| B2-01 | nube | ve |\n| B2-02 | hud | ve |\n| IW-7D0 | Raditz | meta |\n",
                        encoding="utf-8")
        item("B2-01", "curso")
        item("B2-02", "HECHO", f"{PLAN} | visto")
        for malo in (("B2-01", "HECHO", "no/existe.txt"), ("B2-01", "HECHO", ""), ("B2-99", "CURSO", "")):
            try:
                item(*malo)
                raise AssertionError(malo)
            except SystemExit:
                pass
        out = tablero()
        assert out[0].startswith("B2     1/2 hechos, 1 curso") and out[1].startswith("IW     0/1"), out
        assert any("CURSO B2-01" in l for l in out) and not any("IW-7D0" in l for l in out), out
        assert any("PEND  IW-7D0" in l for l in tablero("IW")), "con prefijo salen los pendientes"
        t = COORD / "t.toml"
        t.write_text('nombre = "tx"\n[[encargo]]\nnombre = "a"\nobjetivo = "hacer A"\nhecho = "PASS"\n'
                     '[[encargo]]\nnombre = "b"\nmodelo = "opus"\narchivos = ["src/x.cpp"]\nobjetivo = "B"\nhecho = "ok"\n',
                     encoding="utf-8")
        h = encargos(t)
        assert [x[:2] for x in h] == [("a", "sonnet"), ("b", "opus")], h
        txt = (COORD / "tx" / "b.md").read_text(encoding="utf-8")
        assert "`src/x.cpp`" in txt and "tanda.py pon tx b" in txt and "TX_B.md" in txt, txt
        pon("tx", "a", "trabajando\n en  algo")
        pon("tx", "b", "HECHO: 3/3")
        out = estado("tx", ahora=time.time() + 60 * 60)
        assert any(l.startswith("ok ") and "/b" in l for l in out), out
        assert any(l.startswith("?? ") and "trabajando en algo" in l for l in out), out
    finally:
        COORD, EMU, PLAN, ITEMS = viejo
    print("tanda autoprueba OK")


def main():
    sys.stdout.reconfigure(errors="replace")   # consolas cp1252 (Git Bash) no pintan todos los signos
    ap = argparse.ArgumentParser()
    ap.add_argument("--autoprueba", action="store_true")
    sp = ap.add_subparsers(dest="cmd")
    s = sp.add_parser("encargos"); s.add_argument("toml"); s.add_argument("--salida")
    s = sp.add_parser("pon"); s.add_argument("tanda"); s.add_argument("nombre"); s.add_argument("texto")
    s = sp.add_parser("estado"); s.add_argument("tanda", nargs="?")
    s = sp.add_parser("item"); s.add_argument("id"); s.add_argument("estado"); s.add_argument("evidencia", nargs="?", default="")
    s.add_argument("--quien", default="")
    s = sp.add_parser("tablero"); s.add_argument("prefijo", nargs="?", default="")
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    if a.cmd == "item":
        item(a.id, a.estado, a.evidencia, a.quien)
    elif a.cmd == "tablero":
        tablero(a.prefijo)
    elif a.cmd == "encargos":
        encargos(a.toml, a.salida)
    elif a.cmd == "pon":
        pon(a.tanda, a.nombre, a.texto)
    elif a.cmd == "estado":
        estado(a.tanda)
    else:
        ap.print_help()


if __name__ == "__main__":
    sys.exit(main())

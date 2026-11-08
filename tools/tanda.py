r"""tanda.py - tandas de sesiones baratas: encargos con las reglas de ahorro dentro y un estado que se lee SIN IA.

  python tools\tanda.py encargos  studies\ 5\TANDAS\tanda3.toml   -> un encargo .md por sesion (para start_session)
  python tools\tanda.py pon       tanda3 iw-campo "PASS 7D0; sigue 7D2"   (cada sesion, al acabar cada turno)
  python tools\tanda.py estado    [tanda3]       -> tabla de una linea por sesion + cola del emulador + atascos
  python tools\tanda.py --autoprueba

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
import sys
import time
import tomllib
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
COORD = Path(r"E:\DBZ3HD_trabajo\coord\tandas")
EMU = Path(r"E:\DBZ3HD_trabajo\emu_turnos\registro.jsonl")
ATASCO_MIN = 45

REGLAS = """## Reglas de ahorro (obligatorias; medido 8/11: releer el contexto es el 75 % del gasto)
- Trabaja de verdad: el encargo puede llevar varias horas; NO pares tras el reconocimiento. Que sea largo o
  difícil no es un bloqueo. `BLOQUEADO:` solo si falta algo externo (una decisión del usuario, un archivo o
  dato que no existe, el emulador roto).
- Contexto: al acabar cada unidad (un minijuego, una fase), si pasas de ~350k tokens (`get_usage` "self"),
  escribe tu estado en `{estado_md}` y pide relevo; antes de eso, sigue en la misma sesión (empezar otra cuesta
  ~91k y releer lo ya leído).
- Lee por trozos: Grep primero; Read con offset/limit; nunca un archivo de más de 800 líneas entero.
- Recorta la salida de los comandos (`| Select-Object -Last 15`); pruebas largas en segundo plano.
- Ayudantes: como mucho 2, en Haiku (buscar/leer) o Sonnet; informes de ≤10 líneas. Sin workflows.
- Primero el CÓDIGO (orden del usuario 8/11): la lógica sale del decompilado (`E:\\DBZ3HD_trabajo\\ghidra\\salida`) y
  del intérprete sin juego (ee.py, bancos tipo qte7da.py). El emulador solo para UNA validación corta por
  minijuego/fase (tope de 5 min por toma en `pcsx2_turnos.Grabacion`; más solo con DBZ3_GRABACION_MOTIVO);
  si no cuadra, se mira el código, no se repite la grabación. Secuencias que vienen de DATOS (guiones del
  tablero, tablas) se leen de los datos, no se graban. Nunca repitas una prueba sin un
  cambio entre medias. En PCSX2 usa trucos (pnach: ganar el combate, vida infinita) y velocidad x2-x3 donde no
  haga falta ir fotograma a fotograma.
- Pruebas: solo el escenario afectado y sin `--forzar`; la batería `todo --max 3` una vez, al cerrar.
- PCSX2: una grabación a la vez (turno `Grabacion` de `tools/pcsx2_turnos.py`), solo la ventana de
  fotogramas que comparas.
- Al acabar CADA turno, una línea de estado (en vez de que el coordinador lea tu conversación):
  `python tools/tanda.py pon {tanda} {nombre} "<resultado o en qué estás>"`
  Al terminar del todo, que la línea empiece por `HECHO:` o `BLOQUEADO:`.
- Informe final de ≤10 líneas en `studies 5/ENTREGAS/{informe}`. Sin commit ni push. Nunca borres:
  mueve a `D:\\_PARA_BORRAR_CLAUDE`. Temporales en `E:\\DBZ3HD_trabajo`, nunca en `D:\\tmp`.
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
                 + REGLAS.format(estado_md=_ruta_estado(tanda, n).with_suffix('.md'), tanda=tanda, nombre=n,
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
                     else f"?? {mins:.0f}min " if mins > ATASCO_MIN else "   ")
            lineas.append(f"{marca}{t.name}/{p.stem:<18} {hora}  {txt[:150]}")
    if EMU.is_file():
        u = EMU.read_text(encoding="utf-8", errors="replace").strip().splitlines()[-1:]
        if u:
            e = json.loads(u[0])
            lineas.append(f"    emulador: {e.get('hora', '')[11:16]} {e.get('evento')} {e.get('nombre', '')}"
                          f" {'' if e.get('ok', True) else 'FALLO ' + e.get('error', '')}")
    print("\n".join(lineas) or "(sin tandas)")
    return lineas


def autoprueba():
    import tempfile
    global COORD, EMU
    viejo = COORD, EMU
    COORD = Path(tempfile.mkdtemp())
    EMU = COORD / "nada.jsonl"
    try:
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
        COORD, EMU = viejo
    print("tanda autoprueba OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--autoprueba", action="store_true")
    sp = ap.add_subparsers(dest="cmd")
    s = sp.add_parser("encargos"); s.add_argument("toml"); s.add_argument("--salida")
    s = sp.add_parser("pon"); s.add_argument("tanda"); s.add_argument("nombre"); s.add_argument("texto")
    s = sp.add_parser("estado"); s.add_argument("tanda", nargs="?")
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    if a.cmd == "encargos":
        encargos(a.toml, a.salida)
    elif a.cmd == "pon":
        pon(a.tanda, a.nombre, a.texto)
    elif a.cmd == "estado":
        estado(a.tanda)
    else:
        ap.print_help()


if __name__ == "__main__":
    sys.exit(main())

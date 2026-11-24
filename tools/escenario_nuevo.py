"""Escenarios de prueba (.toml) sin conocer tools/pruebas.py por dentro. No abre el juego.

  python tools/escenario_nuevo.py crear <nombre> [--desde <escenario existente>]
        escribe docs/14_frentes/escenarios_pruebas/<nombre>.toml (nunca pisa) y lo valida
  python tools/escenario_nuevo.py validar <nombre|ruta.toml>
        carga el guion como lo hara pruebas.py y comprueba cada orden y sus argumentos; dice la linea del error
  python tools/escenario_nuevo.py lista
        ordenes disponibles, sacadas del codigo de pruebas.py
  python tools/escenario_nuevo.py --autoprueba
Luego se corre con: python tools/pruebas.py correr <copia> <ruta.toml>
"""
import ast
import inspect
import os
import re
import sys
import tomllib
import types
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pruebas  # noqa: E402  (solo lee constantes al importarse; no toca el juego)

CARPETA = pruebas.R / "docs" / "14_frentes" / "escenarios_pruebas"


# --- lista de ordenes: del codigo de _dsl_bloques / _dsl_ejecutar -----------------------------------------
def _verbos_de(test):
    """Verbos de una condicion 'v == "x"' o 'v in ("x", "y")'."""
    if isinstance(test, ast.Compare) and isinstance(test.left, ast.Name) and test.left.id == "v":
        c = test.comparators[0]
        if isinstance(c, ast.Constant):
            return [c.value]
        if isinstance(c, (ast.Tuple, ast.List, ast.Set)):
            return [e.value for e in c.elts if isinstance(e, ast.Constant)]
    return []


def _ramas():
    """{verbo: [nodos del cuerpo]} de las dos funciones del DSL."""
    ramas = {}
    for f in (pruebas._dsl_bloques, pruebas._dsl_ejecutar):
        for n in ast.walk(ast.parse(inspect.cleandoc("\n" + inspect.getsource(f)))):
            if isinstance(n, ast.If):
                for v in _verbos_de(n.test):
                    ramas.setdefault(v, []).extend(n.body)
    return ramas


def ordenes():
    """[(verbo, uso, ayuda)] para cada verbo de pruebas.VERBOS."""
    ramas, out = _ramas(), []
    for v in sorted(pruebas.VERBOS):
        cuerpo = ramas.get(v, [])
        usados, opcionales, ayuda = set(), set(), ""
        for st in cuerpo:
            for n in ast.walk(st):
                if isinstance(n, ast.Subscript) and isinstance(n.value, ast.Name) and n.value.id == "a" \
                        and isinstance(n.slice, ast.Constant):
                    usados.add(n.slice.value)
                elif isinstance(n, ast.Subscript) and isinstance(n.value, ast.Name) and n.value.id == "a":
                    usados.update({0, 1})              # a[:2] (colocar)
                elif isinstance(n, ast.Compare) and isinstance(n.left, ast.Call) and getattr(n.left.func, "id", "") == "len" \
                        and isinstance(n.comparators[0], ast.Constant):
                    opcionales.add(n.comparators[0].value)
                elif isinstance(n, ast.IfExp) and isinstance(n.test, ast.Name) and n.test.id == "a":
                    opcionales.add(0)
                elif isinstance(n, ast.Call) and not ayuda:
                    fn = n.func
                    obj = getattr(pruebas.Juego, fn.attr, None) if isinstance(fn, ast.Attribute) and \
                        getattr(fn.value, "id", "") == "j" else getattr(pruebas, getattr(fn, "id", ""), None)
                    doc = inspect.getdoc(obj) if callable(obj) and getattr(obj, "__module__", "") == "pruebas" else None
                    if doc:
                        ayuda = doc.splitlines()[0]
        if not ayuda:
            ayuda = next((ast.unparse(s) for s in cuerpo if not isinstance(s, (ast.Continue, ast.Pass, ast.Import, ast.ImportFrom))), "")
        if v in pruebas.BLOQUES_DSL:                   # la sintaxis exacta esta en el docstring de _dsl_bloques
            ayuda = " ".join(inspect.getdoc(pruebas._dsl_bloques).split())
            ayuda = ayuda[ayuda.find("'" + v) + 1:].split("'")[0]
            ayuda = "bloque hasta 'fin': " + ayuda + {"repetir": " (N vueltas)"}.get(v, " (repite el bloque hasta que se cumpla)")
        if v == "fin":
            ayuda = "cierra el bloque abierto (repetir / hasta_log / hasta_pantalla)"
        args = " ".join(("[arg%d]" if i in opcionales else "arg%d") % (i + 1) for i in range(max(usados) + 1)) if usados else ""
        out.append((v, (v + " " + args).strip(), " ".join(ayuda.split())[:110]))
    return out


# --- validar -------------------------------------------------------------------------------------------
class _JuegoFalso:
    """Acepta cualquier llamada: sirve para que _dsl_ejecutar interprete los argumentos sin juego."""
    def __getattr__(self, nombre):
        return lambda *a, **k: 0


def _ensayo(nodo):
    """Ejecuta un paso suelto con el juego falso; da el error de argumentos que daria en la prueba real."""
    v, a, hijos = nodo
    if hijos is not None:
        return
    viejos = {k: getattr(pruebas, k) for k in ("dormir", "_colocar", "_esperar_pelea")}
    b2 = sys.modules.get("b2_f4_final")
    try:
        for k in viejos:
            setattr(pruebas, k, lambda *x, **y: None)
        sys.modules["b2_f4_final"] = types.SimpleNamespace(ejecutar=lambda *x, **y: None)
        pruebas._dsl_ejecutar(_JuegoFalso(), [nodo])
    finally:
        for k, f in viejos.items():
            setattr(pruebas, k, f)
        if b2 is None:
            sys.modules.pop("b2_f4_final", None)
        else:
            sys.modules["b2_f4_final"] = b2


def resolver(nombre):
    p = Path(nombre)
    if p.suffix.lower() == ".toml" and p.exists():
        return p
    hallados = sorted(CARPETA.rglob(Path(nombre).stem + ".toml"))
    if not hallados:
        raise SystemExit("no encuentro el escenario '%s' (ni ruta ni nombre en %s)" % (nombre, CARPETA))
    if len(hallados) > 1:
        print("aviso: '%s' esta %d veces; uso %s" % (nombre, len(hallados), hallados[0]))
    return hallados[0]


def _lineas_pasos(texto, pasos):
    """Linea (1..n) de cada paso en el .toml (busca en orden; los pasos en una sola linea comparten numero)."""
    lineas, res, desde = texto.splitlines(), [], 0
    ini = next((i for i, l in enumerate(lineas) if re.match(r"\s*pasos\s*=", l)), 0)
    desde = ini
    for p in pasos:
        variantes = (p, p.replace("\\", "\\\\"), p.replace('"', '\\"'), p.replace("\\", "\\\\").replace('"', '\\"'))
        n = next((i for i in range(desde, len(lineas)) if any(x in lineas[i] for x in variantes)), None)
        res.append(n + 1 if n is not None else ini + 1)
        desde = n if n is not None else desde
    return res


def validar(ruta):
    """Lista de errores ('linea N: ...'); vacia si el escenario carga y cada orden es valida."""
    ruta = Path(ruta)
    texto = ruta.read_text(encoding="utf-8-sig")   # INFRA-22: con BOM tambien (Bloc de notas / PowerShell 5.1)
    try:
        d = tomllib.loads(texto)
    except tomllib.TOMLDecodeError as e:
        return ["toml mal escrito: %s" % e]
    pasos = d.get("pasos")
    if not isinstance(pasos, list) or not pasos or not all(isinstance(p, str) for p in pasos):
        return ["falta 'pasos' (lista de textos, p. ej. pasos = [\"esperar 1000\"])"]
    lineas, errores, prof = _lineas_pasos(texto, pasos), [], []
    for i, (p, n) in enumerate(zip(pasos, lineas)):
        donde = "linea %d (paso %d %r)" % (n, i + 1, p)
        try:
            v = p.split()[0] if p.split() else ""
            if v == "fin":
                if not prof:
                    errores.append("%s: 'fin' sin bloque abierto" % donde)
                else:
                    prof.pop()
                continue
            if v in pruebas.BLOQUES_DSL:
                prof.append(donde)                     # aunque sus argumentos esten mal: su 'fin' sigue contando
            nodos = pruebas._dsl_bloques([p, "fin"] if v in pruebas.BLOQUES_DSL else [p], ruta.name)
            _ensayo(nodos[0])
        except SystemExit as e:
            errores.append("%s: %s" % (donde, e))
        except (IndexError, ValueError, TypeError, re.error) as e:
            errores.append("%s: argumentos mal (%s: %s)" % (donde, type(e).__name__, e or "faltan"))
    errores += ["%s: falta su 'fin'" % x for x in prof]
    if not errores:                                    # el cargador real (env, escena) solo si los pasos estan bien
        try:
            pruebas.cargar_escenario(str(ruta), {})
        except SystemExit as e:
            n = next((i + 1 for i, l in enumerate(texto.splitlines()) if re.match(r"\s*(env|escena)\s*=", l)), 1)
            errores.append("linea %d: %s" % (n, e))
    return errores


# --- crear ---------------------------------------------------------------------------------------------
def plantilla(nombre):
    ayuda = "\n".join("#   %-34s %s" % (u, h) for _, u, h in ordenes())
    return f'''# {nombre}: QUE prueba y QUE se espera ver (una linea; el criterio de PASS abajo).
# Correr:   python tools\\pruebas.py correr <copia> "docs\\14_frentes\\escenarios_pruebas\\{nombre}.toml"
# Validar:  python tools\\escenario_nuevo.py validar {nombre}
#
# criterio = la linea del log o la pantalla que demuestra que funciona
#
# Ordenes de 'pasos' (sacadas de tools/pruebas.py; teclas con ':' = varias a la vez, p. ej. "tecla R1:Circle"):
{ayuda}
descripcion = "{nombre}: describe la prueba"
# env = variables DBZ3_TEST_* que preparan el juego (las de los .toml vecinos), CLAVE=VALOR
env = [
  "DBZ3_TEST_RINGOUT=1",
]
# escena = "b2:fase3"   # opcional: salto directo (ver resolver_escena en pruebas.py)
pasos = [
  "esperar 3000",
  "esperar_pantalla 5 60",
  "captura 01_menu",
  "hasta_log 'combate acabado' 120",
  "tecla Space 1500",
  "fin",
  "log 'combate acabado' 20",
  "captura 02_final",
]
'''


def crear(nombre, desde=None, carpeta=None):
    carpeta = Path(carpeta or CARPETA)
    if not re.fullmatch(r"[\w.-]+", nombre):
        raise SystemExit("nombre con caracteres raros: usa letras, numeros, _ - .")
    destino = carpeta / (nombre + ".toml")
    if destino.exists():
        raise SystemExit("ya existe %s: no lo piso (elige otro nombre)" % destino)
    if desde:
        origen = resolver(desde)
        texto = "# %s: copiado de %s; cambia descripcion, criterio y pasos.\n%s" % (
            nombre, origen.relative_to(pruebas.R) if origen.is_relative_to(pruebas.R) else origen,
            origen.read_text(encoding="utf-8"))
    else:
        texto = plantilla(nombre)
    carpeta.mkdir(parents=True, exist_ok=True)
    with open(destino, "x", encoding="utf-8") as f:   # 'x': falla si aparecio entre medias
        f.write(texto)
    return destino


# --- autoprueba ----------------------------------------------------------------------------------------
def _autoprueba():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import infra
    zona = infra.tmp("escenario_nuevo")
    lista = {v: (u, h) for v, u, h in ordenes()}
    assert set(lista) == set(pruebas.VERBOS), set(pruebas.VERBOS) ^ set(lista)
    assert lista["tecla"][0] == "tecla arg1 [arg2]" and lista["clip"][0] == "clip arg1 arg2", lista["tecla"]
    assert lista["colocar"][0] == "colocar arg1 arg2" and lista["esperar"][0] == "esperar arg1"
    assert all(h for h in (x[1] for x in lista.values())), [v for v, x in lista.items() if not x[1]]

    p = crear("nuevo", carpeta=zona)
    assert validar(p) == [], validar(p)
    try:
        crear("nuevo", carpeta=zona)
        raise AssertionError("piso uno existente")
    except SystemExit:
        pass
    q = crear("copia", desde=str(CARPETA / "cine_intro.toml"), carpeta=zona)
    assert validar(q) == [] and "copiado de" in q.read_text(encoding="utf-8")

    malo = zona / "malo.toml"
    malo.write_text('descripcion = "x"\npasos = [\n  "esperar 100",\n  "tecla",\n  "saltar 3",\n'
                    '  "esperar abc",\n  "repetir 2",\n  "colocar 1,2 3,4,5",\n  "clip nombre",\n'
                    '  "hasta_log \'(\' 5",\n  "fin",\n]\n', encoding="utf-8")
    e = validar(malo)
    esperado = {4: "tecla", 5: "desconocido", 6: "esperar abc", 7: "falta su 'fin'", 8: "colocar", 9: "clip", 10: "hasta_log"}
    for linea, trozo in esperado.items():
        assert any(x.startswith("linea %d " % linea) and trozo in x for x in e), (linea, trozo, e)
    sobra = zona / "sobra.toml"
    sobra.write_text('pasos = ["esperar 1", "fin"]\nenv = ["SIN_IGUAL"]\n', encoding="utf-8")
    assert any("sin bloque abierto" in x for x in validar(sobra))
    env_mal = zona / "env.toml"
    env_mal.write_text('pasos = ["esperar 1"]\nenv = ["SIN_IGUAL"]\n', encoding="utf-8")
    assert any(x.startswith("linea 2:") and "CLAVE=VALOR" in x for x in validar(env_mal)), validar(env_mal)
    roto = zona / "roto.toml"
    roto.write_text('pasos = ["esperar 1"\n', encoding="utf-8")
    assert validar(roto)[0].startswith("toml mal escrito")

    # todos los .toml del proyecto que hoy carga pruebas.py deben validar igual (sin falsos errores)
    falsos = []
    for t in sorted(CARPETA.rglob("*.toml")):
        try:
            pruebas.cargar_escenario(str(t), {})
        except (SystemExit, Exception):
            continue
        if validar(t):
            falsos.append((t.name, validar(t)[:1]))
    assert not falsos, falsos
    print("autoprueba escenario_nuevo: OK (%d ordenes; %d .toml del proyecto validados; zona %s)"
          % (len(lista), len(list(CARPETA.rglob("*.toml"))), zona))


def main(argv):
    if argv[:1] == ["--autoprueba"]:
        _autoprueba()
        return 0
    if argv[:1] == ["lista"]:
        for _, u, h in ordenes():
            print("%-34s %s" % (u, h))
        return 0
    if argv[:1] == ["validar"] and len(argv) == 2:
        ruta = resolver(argv[1])
        errores = validar(ruta)
        for e in errores:
            print("ERROR %s" % e)
        print("%s: %s" % (ruta, "valido" if not errores else "%d error(es)" % len(errores)))
        return 1 if errores else 0
    if argv[:1] == ["crear"] and len(argv) in (2, 4):
        desde = argv[3] if len(argv) == 4 and argv[2] == "--desde" else None
        destino = crear(argv[1], desde)
        errores = validar(destino)
        print("creado %s (%s)" % (destino, "valido" if not errores else "; ".join(errores)))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

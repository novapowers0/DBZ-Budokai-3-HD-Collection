"""Audita pt/ja de modos y Rogue; --aplicar simula el TOML de entrega en memoria."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import tomllib


IDIOMAS = ('pt', 'ja')
CATALOGO = 'res/ingame_ui/i18n_modos.toml'
CAMINOS = 'res/datos/rogue_caminos.toml'
CARTAS = 'res/mejoras/rogue_cartas.toml'
COMENTARIO = r'//[^\n]*|/\*[\s\S]*?\*/'
LIT = r'(?:u8)?"(?:[^"\\\n]|\\[\s\S])*"'
RAW = r'(?:u8|[uUL])?R"(?P<delim>[^\s()\\]{0,16})\([\s\S]*?\)(?P=delim)"'
TOKEN = re.compile(rf'{COMENTARIO}|{RAW}|{LIT}|\'(?:[^\'\\\n]|\\.)*\'|[\w]+|[^\s]')
ESPACIO = rf'(?:\s|{COMENTARIO})*'
ARG = rf'(?:{LIT}{ESPACIO})+'
ESCAPE = re.compile(r'\\(?:[0-7]{1,3}|x[0-9a-fA-F]+|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|[\s\S])')
SIMPLES = dict(zip('abfnrtv\\"\'?', '\a\b\f\n\r\t\v\\"\'?'))
SIMPLES['\n'] = ''
MARCADOR = re.compile(
    r'\{\{|\}\}|\{[^{}]*\}|[{}]|%%|'
    r'%(?:\d+\$)?[-+#0\']*(?:\d+|\*(?:\d+\$)?)?(?:\.(?:\d*|\*(?:\d+\$)?))?'
    r'(?:hh|ll|[hljztLI])?[diuoxXfFeEgGaAcspn](?![A-Za-z])|%'
)


def literal(argumento):
    partes = bytearray()
    for token in TOKEN.finditer(argumento):
        s = token[0]
        if not re.fullmatch(LIT, s):
            continue
        cuerpo = s[s.index('"') + 1:-1]
        cursor = 0
        for m in ESCAPE.finditer(cuerpo):
            partes.extend(cuerpo[cursor:m.start()].encode())
            sec = m[0][1:]
            if sec in SIMPLES:
                partes.extend(SIMPLES[sec].encode())
            elif sec[0] in 'uU' and len(sec) > 1:
                partes.extend(chr(int(sec[1:], 16)).encode())
            elif sec[0] in '01234567x' and sec != 'x':
                partes.append(int(sec[1:], 16) if sec[0] == 'x' else int(sec, 8))
            else:
                raise ValueError(f'escape C++ sin resolver: {m[0]!r}')
            cursor = m.end()
        partes.extend(cuerpo[cursor:].encode())
    return partes.decode()


def llamadas(texto, simple, traduccion_simple=False):
    # El lexer evita coincidencias dentro de comentarios, caracteres y cadenas.
    tokens = [m for m in TOKEN.finditer(texto) if not m[0].startswith(('/*', '//'))]
    for i, m in enumerate(tokens):
        if m[0] not in ('L', 'Otro', 'T') or i + 1 == len(tokens) or tokens[i + 1][0] != '(':
            continue
        anterior = tokens[i - 3][0] if i >= 3 and tokens[i - 2][0] == tokens[i - 1][0] == ':' else None
        if m[0] == 'L' and not (anterior in ('M', 'modos') or (anterior is None and simple)):
            continue
        if m[0] == 'Otro' and anterior not in ('T', 'i18n'):
            continue
        if m[0] == 'T' and not (anterior in ('T', 'i18n') or (anterior is None and traduccion_simple)):
            continue
        n, ingles = {'L': (5, 1), 'Otro': (1, 0), 'T': (2, 1)}[m[0]]
        # Solo el argumento inglés ha de ser literal; los demás pueden ser expresiones.
        args, inicio, pila = [], tokens[i + 1].end(), []
        for j in range(i + 2, len(tokens)):
            t = tokens[j]
            v = t[0]
            if not pila and v in (',', ')'):
                args.append(texto[inicio:t.start()])
                inicio = t.end()
                if v == ')':
                    break
            elif v in ('(', '[', '{'):
                pila.append({'(': ')', '[': ']', '{': '}'}[v])
            elif pila and v == pila[-1]:
                pila.pop()
        if len(args) == n:
            arg = args[ingles]
            es = literal(args[0]) if m[0] == 'T' and re.fullmatch(ESPACIO + ARG, args[0]) else None
            yield m.start(), literal(arg) if re.fullmatch(ESPACIO + ARG, arg) else None, es


def leer(ruta):
    with ruta.open('rb') as f:
        return tomllib.load(f)


def filas_cpp(texto, tabla, n):
    cabecera = re.search(r'\b' + tabla + r'(?:\[[^\]]*\])+\s*=\s*\{', texto)
    if not cabecera:
        raise ValueError(f'no se encontró la tabla {tabla}')
    nivel, filas = 1, []
    patron = re.compile(r'\{' + ESPACIO + (',' + ESPACIO).join(f'({ARG})' for _ in range(n)) + r',?' + ESPACIO + r'\}')
    for t in TOKEN.finditer(texto, cabecera.end()):
        if t[0] == '{':
            if nivel == 1:
                inicio = t.start()
            nivel += 1
        elif t[0] == '}':
            nivel -= 1
            if nivel == 1 and (m := patron.fullmatch(texto[inicio:t.end()])):
                filas.append(tuple(map(literal, m.groups())))
            if nivel == 0:
                if not filas:
                    break
                return filas
    raise ValueError(f'no se pudieron leer filas de {tabla}')


def inventario(raiz, archivos):
    claves, dinamicas = defaultdict(list), []
    textos_i18n = (raiz / 'src/launcher/i18n.cpp').read_text(encoding='utf-8-sig')
    fallback = {es for es, pt, ja in filas_cpp(textos_i18n, 'kNovedadesPtJa', 3) if presente(pt) and presente(ja)}
    fallback.update(fila[0] for fila in filas_cpp(textos_i18n, 'kXv2', 6) if all(map(presente, fila[-2:])))

    def agrega(en, sitio):
        # Números y sufijos de racha: no traducibles o localizados antes de L.
        if any(c.isalpha() for c in en) and en not in ('s', ' day', ' days'):
            claves[en].append(sitio)

    for ruta in archivos or sorted(p for p in (raiz / 'src').rglob('*') if p.suffix in ('.cpp', '.inl', '.h')):
        if ruta.is_relative_to(raiz / 'src/launcher'):
            continue
        texto = ruta.read_text(encoding='utf-8-sig')
        if not re.search(r'\b(?:L|Otro|T)\s*\(', texto):
            continue
        simple = ruta.name == 'modos.cpp' or bool(re.search(r'\busing\s+(?:M|modos|dbz3::modos)::L\s*;', texto))
        simple_t = ruta.suffix == '.inl' or bool(re.search(r'\busing\s+(?:T|i18n|dbz3::i18n)::T\s*;', texto))
        for pos, en, es in llamadas(texto, simple, simple_t):
            sitio = f'{ruta.relative_to(raiz) if ruta.is_relative_to(raiz) else ruta}:{texto.count(chr(10), 0, pos) + 1}'
            if en is None:
                dinamicas.append(sitio)
            elif es not in fallback:
                agrega(en, sitio)

    if not archivos:
        datos = leer(raiz / CAMINOS)
        for grupo, campos in (('caminos', (('en', 'nombre'), ('t_en', 't_es'))), ('maldiciones', (('en', 'nombre'),))):
            for clave, fila in datos[grupo].items():
                if grupo == 'maldiciones' and clave == 'orden':
                    continue
                for en, es in campos:
                    agrega(fila.get(en) or fila.get(es, ''), f'{CAMINOS}:{grupo}.{clave}.{en}')
        for fila in leer(raiz / CARTAS)['carta']:
            for en, es in (('en', 'es'), ('t_en', 't_es')):
                agrega(fila.get(en) or fila.get(es, ''), f'{CARTAS}:{fila["id"]}.{en}')
        ruta = 'src/rogue_caminos.cpp'
        for fila in filas_cpp((raiz / ruta).read_text(encoding='utf-8-sig'), 'kExtra', 5):
            agrega(fila[1], f'{ruta}:kExtra')
    if not claves:
        raise ValueError('no se encontraron claves auditables')
    return claves, dinamicas


def presente(s):
    return isinstance(s, str) and bool(s.strip())


def problemas(en, valor):
    if not presente(valor):
        return ['traducción vacía o no textual']
    fallos = []
    if MARCADOR.findall(en) != MARCADOR.findall(valor):
        fallos.append('marcadores {} / % distintos')
    if 10 * len(valor) > 13 * len(en):
        fallos.append(f'longitud {len(valor)} > {13 * len(en) // 10}')
    if '\0' in valor:
        fallos.append('contiene NUL')
    return fallos


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('cpp', nargs='*', type=Path, help='limita el análisis a estos archivos')
    ap.add_argument('--raiz', type=Path, default=Path(__file__).resolve().parent.parent, help='raíz del proyecto')
    ap.add_argument('--aplicar', type=Path, help='superpone el TOML sin escribir el catálogo')
    ap.add_argument('--json', action='store_true', help='lista completa en formato JSON')
    ap.add_argument('--dinamicas', action='store_true', help='muestra los sitios sin argumento inglés literal')
    args = ap.parse_args()
    try:
        claves, dinamicas = inventario(args.raiz.resolve(), [p.resolve() for p in args.cpp])
        base = leer(args.raiz / CATALOGO)
        errores = []
        for en, fila in (leer(args.aplicar) if args.aplicar else {}).items():
            if en not in claves or not isinstance(fila, dict) or not fila or fila.keys() - set(IDIOMAS):
                errores.append(f'{en!r}: clave no inventariada o tabla no válida (solo pt y ja)')
                continue
            actual = base.setdefault(en, {})
            for idioma, valor in fila.items():
                fallos = problemas(en, valor)
                if presente(actual.get(idioma)) and actual[idioma] != valor:
                    fallos.append('conflicto con traducción existente')
                errores.extend(f'{en!r} [{idioma}]: {f}' for f in fallos)
                if not fallos:
                    actual[idioma] = valor
        huecos = [dict(en=en, idiomas=idiomas, sitios=claves[en])
                  for en in sorted(claves) if (idiomas := [i for i in IDIOMAS if not presente(base.get(en, {}).get(i))])]
    except (OSError, ValueError, TypeError, AttributeError, KeyError) as e:
        ap.exit(2, f'i18n_huecos: {e}\n')
    resumen = dict(huecos=huecos, errores=errores, claves=len(claves), sitios_no_literales=len(dinamicas))
    if args.json:
        print(json.dumps(resumen | ({'dinamicas': dinamicas} if args.dinamicas else {}), ensure_ascii=False, indent=2))
    else:
        if args.dinamicas:
            for sitio in dinamicas:
                print(f'{sitio} argumento inglés no literal')
        for h in huecos:
            print(f'{h["sitios"][0]} falta {"/".join(h["idiomas"])}: {h["en"]!r}')
        for error in errores:
            print(error)
        print(f'i18n_huecos: {sum(len(h["idiomas"]) for h in huecos)}; '
              f'{len(claves)} claves, {len(errores)} errores, {len(dinamicas)} sitios no literales')
    return int(bool(huecos or errores))


if __name__ == '__main__':
    raise SystemExit(main())

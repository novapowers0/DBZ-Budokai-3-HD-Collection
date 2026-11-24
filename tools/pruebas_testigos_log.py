"""Testigos estáticos de regex para mensajes C++ compuestos. Nunca intervienen al ejecutar un guion."""
from functools import cache
from hashlib import sha256
import json
import re


@cache
def _catalogo(raiz):
    ruta = raiz / 'tools' / 'pruebas_testigos_log.json'
    try:
        emisores = json.loads(ruta.read_text(encoding='utf-8'))
    except FileNotFoundError:
        return {}  # Sin catálogo, los avisos se mantienen (también en copias mínimas del arnés).
    return {rx: (e, args) for e in emisores for rx, args in e['casos'].items()}


@cache
def _fuente(ruta):
    try:
        return ruta.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return ''


# Solo el primer argumento de REXLOG, incluyendo literales C++ adyacentes.
_CADENA = r'"(?:[^"\\\n]|\\.)*"'
_FORMATO = re.compile(r'(?m)\bREXLOG_(?:INFO|WARN)\(\s*((?:' + _CADENA + r'\s*)+)(?=[,)])')


def formatos(texto):
    for m in _FORMATO.finditer(texto):
        if '//' in texto[texto.rfind('\n', 0, m.start()) + 1:m.start()]:
            continue
        # Acota el registro (no es un parser C++): además del formato se fija la
        # llamada revisada y sus argumentos, incluidas las ramas de las ternarias.
        fin = texto.find(');', m.end(), min(len(texto), m.end() + 1024))
        if fin >= 0:
            llamada = texto[m.start():fin + 2].replace('\r\n', '\n')
            yield (m.start(), ''.join(s[1:-1] for s in re.findall(_CADENA, m[1])),
                   sha256(llamada.encode('utf-8')).hexdigest())


@cache
def _formatos_fuente(ruta):
    return frozenset((fmt, sello) for _, fmt, sello in formatos(_fuente(ruta)))


def hay_testigo(raiz, regex):
    """Una regex exacta sigue requiriendo su formato actual y un ejemplo que lo satisfaga."""
    caso = _catalogo(raiz).get(regex.pattern)
    if caso is None:
        return False
    emisor, args = caso
    ruta = raiz / 'src' / emisor['fuente']
    if (emisor['formato'], emisor['llamada_sha256']) not in _formatos_fuente(ruta):
        return False
    return regex.search(emisor['formato'].format(*args)) is not None

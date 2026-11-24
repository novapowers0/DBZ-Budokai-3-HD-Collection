"""Resolucion estricta de piezas originales por forma del creador.

Las relaciones entre formas de historia y posiciones del donante deben ser
aportadas por quien conoce el moveset; su orden en razas_reglas no las prueba.
Este modulo no lee ni distribuye modelos del juego.
"""

from pathlib import Path

try:
    import tomllib
except ImportError:
    import tomli as tomllib


ZONAS = ("torso", "piernas", "guantes", "botas")
REGLAS = Path(__file__).with_name("razas_reglas.toml")


def formas_raza(raza, ruta=REGLAS):
    with open(ruta, "rb") as archivo:
        datos = tomllib.load(archivo)
    coincidencias = [r for r in datos["raza"] if r["id"] == raza]
    if len(coincidencias) != 1:
        raise ValueError("raza desconocida o duplicada: %s" % raza)
    formas = coincidencias[0]["formas"]
    if len(set(formas)) != len(formas):
        raise ValueError("formas duplicadas en las reglas")
    return tuple(formas)


def ficha_forma(ficha, raza, forma, variantes, ruta=REGLAS):
    """Copia ficha y sustituye cada ID declarado para forma.

    variantes = {id_base: {id_forma: id_pieza}}. Una pieza sin entrada
    conserva su ID; si declara variantes, debe cubrir la forma solicitada.
    Los IDs de las variantes deben ser enteros no negativos.
    """
    if forma not in formas_raza(raza, ruta):
        raise ValueError("forma %s no pertenece a la raza %s" % (forma, raza))
    salida = dict(ficha)
    for zona in ZONAS:
        clave = "b3_" + zona
        identificador = ficha.get(clave, -1)
        if identificador not in variantes:
            continue
        tabla = variantes[identificador]
        desconocidas = set(tabla) - set(formas_raza(raza, ruta))
        if desconocidas:
            raise ValueError("formas ajenas a la raza: %s" % sorted(desconocidas))
        valor = tabla.get(forma)
        if type(valor) is not int or valor < 0:
            raise ValueError("pieza %s sin variante valida para %s" % (identificador, forma))
        salida[clave] = valor
    return salida


def construir_formas(afs, ficha, juego, resumen, construir, raza, enlaces, variantes, ruta=REGLAS):
    """Genera [(bytes, indice_donante)] a partir de enlaces verificados.

    Cada enlace es (id_forma, indice_donante, fid_modelo). El llamador debe
    comprobar que el donante soporta cada forma y que el fid corresponde a su
    esqueleto. Se rechazan indices duplicados o fuera del rango 1..7.
    """
    indices = [indice for _, indice, _ in enlaces]
    if any(type(i) is not int or not 1 <= i <= 7 for i in indices) or indices != sorted(set(indices)):
        raise ValueError("indices de forma invalidos, repetidos o desordenados")
    preparadas = [(forma, indice, fid, ficha_forma(ficha, raza, forma, variantes, ruta))
                  for forma, indice, fid in enlaces]
    if len({forma for forma, _, _, _ in preparadas}) != len(preparadas):
        raise ValueError("forma repetida")
    out = []
    for forma, indice, fid, actual in preparadas:
        datos, detalle = construir(afs.entry(fid), actual, juego, afs)
        out.append((datos, indice))
        resumen["variante_" + forma] = {"fid": fid, "indice_donante": indice,
                                        "piezas": detalle.get("piezas_b3", {})}
    return out

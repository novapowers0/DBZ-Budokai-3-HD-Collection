"""Comprobaciones aisladas del registro de mods. No busca ni modifica el juego."""
import hashlib
import os
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from kit import choques, mis_mods, nucleo, planes, escenarios, traje


def escribir(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    raiz = Path(tempfile.mkdtemp(prefix="dbz3_k1_"))
    mods = raiz / "mods"
    mods.mkdir()
    env = SimpleNamespace(mods=str(mods), backups=str(raiz / "respaldos"), cwd=lambda: str(raiz))
    original = raiz / "original.afs"
    partida = raiz / "partida.dat"
    escribir(original, b"original de prueba")
    escribir(partida, b"partida de prueba")
    antes = (digest(original), digest(partida))

    # Entradas representativas: comprueba los mecanismos compartidos, no conversiones ni imagen en juego.
    destinos = {
        "swap": ("us/data_cmn.afs/327/geom.bin", b"swap"),
        "textura": ("0123456789abcdef_64x64_DXT1.dds", b"textura"),
        "escenario": ("us/data_cmn.afs/5007/geom.bin", b"escenario"),
        "tecnica": ("animaciones.json", b'[{"destino":{"personaje":0,"codigo":535}}]'),
    }
    for nombre, (rel, data) in destinos.items():
        a, b = "a_" + nombre, "b_" + nombre
        escribir(mods / a / rel, data)
        escribir(mods / b / rel, data)
        conflictos = choques.choques(str(mods))
        assert choques.de_mod(conflictos, a), (nombre, conflictos)
        assert mis_mods.poner(env, [b], False, nucleo.anotar_indice) == [b]
        assert not choques.de_mod(choques.choques(str(mods)), a)
        assert nucleo.deshacer_ultimo(env) and mis_mods.activo(str(mods), b)
        assert choques.de_mod(choques.choques(str(mods)), a)
        print("OK %s: conflicto, desactivar y deshacer" % nombre)

    assert len(traje.formas(0)) == 6
    try:
        traje.resolver(0, [None] * 6)
        raise AssertionError("traje vacio aceptado")
    except traje.NoEncaja:
        pass
    assert escenarios.validar("", "b2", 7, 16, str(mods))
    assert escenarios.entrada_libre({5007, 5107}, "b2", 7) != 5007
    print("OK traje: seis formas y rechazo de entrada vacia")
    print("OK escenario: nombre invalido y destino ocupado")

    # Dos lotes en el mismo segundo: demostrar si Deshacer los distingue.
    reloj = "20000101-000000"
    with patch.object(planes.time, "strftime", return_value=reloj):
        for nombre in ("lote_uno", "lote_dos"):
            lote = planes.Lote(env, nucleo.anotar_indice)
            lote.abrir([nombre])
            escribir(mods / nombre / "dato", nombre.encode())
            lote.cerrar()
    nucleo.deshacer_ultimo(env)
    fallo_lotes = not (mods / "lote_uno").exists()
    if fallo_lotes:
        print("FALLO REPRODUCIDO: dos asistentes en el mismo segundo se deshacen juntos")
    else:
        print("OK: dos lotes del mismo segundo permanecen independientes")
    assert (digest(original), digest(partida)) == antes
    print("OK hashes: original y partida de prueba intactos")
    print("Raiz conservada: %s" % raiz)
    if fallo_lotes:
        print("RESULTADO: FALLO (lotes simultaneos)")
        return 1
    print("RESULTADO: comprobaciones parciales completadas")
    return 0


if __name__ == "__main__":
    sys.exit(main())

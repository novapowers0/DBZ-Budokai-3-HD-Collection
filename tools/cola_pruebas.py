"""INFRA-21: cola unica de pruebas del juego (archivos, sin servidor ni demonio).

Cada lanzamiento de dbz3.exe (Juego._tomar_plaza en pruebas.py) deja un boleto en COLA mientras espera plaza.
Orden = prioridad (0 bateria de cierre, 1 prueba suelta, 2 prueba larga) y, dentro de ella, llegada (FIFO).
Solo puede coger plaza quien va por delante de los demas que esperan; las largas no pasan de LARGAS plazas a la vez
y ceden el turno en cada partida nueva (cada partida vuelve a la cola). Un boleto que su dueno no refresca en
CADUCA s (proceso muerto o que dejo de esperar) se aparta a COLA/_viejos (no se borra).

  python tools/cola_pruebas.py --autoprueba
  python tools/pruebas.py cola          (estado: plazas ocupadas y cola)
DBZ3_PRUEBAS_COLA=0 vuelve al reparto anterior (primero que llega, tope 3 / 2 ventanas).
"""
import os
import re
import sys
import threading
import time
from uuid import uuid4
from pathlib import Path

COLA = Path(os.environ.get("DBZ3_COLA_DIR") or r"D:\Caches\dbz3_pruebas\cola")   # 20/11: E: es un USB saturado)
ACTIVA = os.environ.get("DBZ3_PRUEBAS_COLA", "1") != "0"
TOPE = 4   # 21/11 PM-04: medido 1,2-1,5 GB (uss) por juego; con ~17 GB fijos de opencode/claude/opera, 6 juegos dejaron 74 MB libres. Tope 4 (3 si hay PCSX2)
LARGAS = int(os.environ.get("DBZ3_COLA_LARGAS", "2"))   # plazas que pueden tener a la vez las pruebas largas
LARGA_MIN = 15                                           # una larga con mas minutos se marca en el estado
CADUCA = 120
PRIOS = {"bateria": 0, "suelta": 1, "larga": 2}
NOMBRE_PRIO = {v: k for k, v in PRIOS.items()}
_BOLETO = re.compile(r"[012]_[0-9]{20}_[0-9]+_[0-9]+\.txt")


def prio_de(cmd):
    """Prioridad por subcomando de pruebas.py; DBZ3_PRUEBAS_PRIO=bateria|suelta|larga manda."""
    p = os.environ.get("DBZ3_PRUEBAS_PRIO", "")
    if p in PRIOS:
        return PRIOS[p]
    return {"todo": 0, "correr": 1, "paralelo": 1, None: 1}.get(cmd, 2)


def boleto(prio, desc, cola=None):
    """Crea (o refresca) un boleto. Nombre = prio_ns_pid_hilo: ordenar por nombre = orden de la cola."""
    cola = Path(cola or COLA)
    cola.mkdir(parents=True, exist_ok=True)
    b = cola / f"{prio}_{time.time_ns():020d}_{os.getpid()}_{threading.get_ident()}.txt"
    b.write_text(desc, encoding="utf-8")
    return b


def refrescar(b):
    try:
        os.utime(b)
    except OSError:          # lo aparto alguien por caducado: se vuelve a poner con su sitio original
        b.parent.mkdir(parents=True, exist_ok=True)
        b.write_text("(recreado)", encoding="utf-8")


def soltar(b):
    if b:
        try:
            b.unlink()       # boleto propio (archivo temporal de la cola), no datos
        except OSError:
            pass


def esperando(cola=None, ahora=None):
    """Boletos vivos en orden; los caducados se apartan a _viejos."""
    cola = Path(cola or COLA)
    ahora = ahora or time.time()
    vivos = []
    for b in sorted(cola.glob("*.txt")):
        if not _BOLETO.fullmatch(b.name) or not b.is_file():
            continue
        try:
            if ahora - b.stat().st_mtime > CADUCA:
                viejos = cola / "_viejos"
                viejos.mkdir(exist_ok=True)
                archivo = viejos / b.name
                if archivo.exists():  # boleto recreado y caducado por segunda vez
                    archivo = viejos / f"{b.stem}_{uuid4().hex}.txt"
                b.rename(archivo)
                continue
        except OSError:
            continue
        vivos.append(b)
    return vivos


def prio(b):
    return int(Path(b).name.split("_", 1)[0])


def turno(mio, vivos, libres, largas_ocupadas, largas=LARGAS):
    """True si `mio` puede coger plaza ya. Los de delante que no pueden coger (largas al tope) no cuentan."""
    if prio(mio) == 2 and largas_ocupadas >= largas:
        return False
    delante = 0
    for b in vivos:
        if b == mio:
            break
        if prio(b) == 2 and largas_ocupadas >= largas:
            continue
        delante += 1
    return delante < libres


def autoprueba():
    import tempfile
    d = Path(tempfile.mkdtemp(prefix="cola_", dir=os.environ.get("DBZ3_COLA_TMP") or None))
    larga = boleto(2, "larga", d)
    suelta = boleto(1, "suelta", d)
    bat = boleto(0, "bateria", d)
    v = esperando(d)
    assert v == [bat, suelta, larga], v                          # prioridad antes que llegada
    assert turno(bat, v, 1, 0) and not turno(suelta, v, 1, 0)    # una plaza: la bateria primero
    assert turno(suelta, v, 2, 0) and not turno(larga, v, 2, 0)
    assert not turno(larga, v, 6, 2)                             # larga al tope: no coge aunque haya sitio
    soltar(bat)
    soltar(suelta)
    v = esperando(d)
    assert turno(larga, v, 1, 1) and not turno(larga, v, 1, 2)
    s2 = boleto(1, "otra", d)
    v = esperando(d)
    assert turno(s2, v, 1, 2)                                    # la larga bloqueada no frena a la suelta
    os.utime(larga, (time.time() - CADUCA - 5,) * 2)             # dueno muerto: deja de contar y se aparta
    assert esperando(d) == [s2] and (d / "_viejos" / larga.name).exists()
    refrescar(larga)                                             # su dueno vivia: vuelve con su sitio
    assert esperando(d) == [s2, larga]
    assert prio_de("todo") == 0 and prio_de("correr") == 1 and prio_de(None) == 1 and prio_de("escena") == 2
    print(f"AUTOPRUEBA cola_pruebas OK ({d})")
    return 0


if __name__ == "__main__":
    if sys.argv[1:2] == ["--autoprueba"]:
        sys.exit(autoprueba())
    print(__doc__)

"""Simulador de red UDP solo para pruebas locales del online (R4): latencia, jitter y perdida aleatoria entre dos copias.

  python tools\\red_sim.py --front 7791 --host 7790 --ida 75 --jitter 4 --perdida 2 [--semilla 1]

El invitado llama al puerto --front; el simulador reenvia al anfitrion (--host) y devuelve sus respuestas. --ida = ms por
sentido (ida y vuelta = el doble). --perdida = % de paquetes de MANDOS (tipo 9) y SUMAS (tipo 15) perdidos; el emparejamiento
(HELLO, MODLIST, START...) solo se retrasa: no tiene reintento fino. Hilo unico con cola por fecha, precision ~0,5 ms.
Salida: una linea JSON al cerrar (Ctrl+C o --seg) con paquetes, perdidos y retraso medio. No instala nada.
"""
import argparse
import heapq
import json
import random
import select
import socket
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--front", type=int, required=True)
    ap.add_argument("--host", type=int, required=True)
    ap.add_argument("--ida", type=float, default=0.0)
    ap.add_argument("--jitter", type=float, default=0.0)
    ap.add_argument("--perdida", type=float, default=0.0)
    ap.add_argument("--semilla", type=int, default=1)
    ap.add_argument("--seg", type=float, default=0.0)
    ap.add_argument("--parar", default="", help="fichero: al existir, el simulador termina")
    a = ap.parse_args()
    rnd = random.Random(a.semilla)
    front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    front.bind(("127.0.0.1", a.front))
    back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    back.bind(("127.0.0.1", 0))
    for s in (front, back):   # WinSock: ICMP de un destino que ya cerro no debe matar el socket
        try:
            s.ioctl(socket.SIO_UDP_CONNRESET, False)
        except Exception:
            pass
    host = ("127.0.0.1", a.host)
    guest = None
    q = []   # (fecha, n, socket, destino, datos)
    n = 0
    st = {"paquetes": 0, "perdidos": 0, "mandos": 0, "retraso_ms": 0.0}
    fin = time.perf_counter() + a.seg if a.seg else None
    while fin is None or time.perf_counter() < fin:
        now = time.perf_counter()
        espera = 0.002
        if q:
            espera = max(0.0, min(espera, q[0][0] - now))
        r, _, _ = select.select([front, back], [], [], espera if espera > 0.0006 else 0)
        for s in r:
            try:
                data, addr = s.recvfrom(4096)
            except OSError:
                continue
            if s is front:
                guest = addr
                dest, out = host, back
            else:
                if guest is None:
                    continue
                dest, out = guest, front
            st["paquetes"] += 1
            tipo = data[4] if len(data) > 4 and data[:4] == b"DBZ3" else 0
            if tipo in (9, 15):
                st["mandos"] += 1
                if a.perdida and rnd.random() * 100 < a.perdida:
                    st["perdidos"] += 1
                    continue
            d = max(0.0, a.ida + (rnd.uniform(-a.jitter, a.jitter) if a.jitter else 0.0)) / 1000.0
            st["retraso_ms"] += d * 1000
            n += 1
            heapq.heappush(q, (time.perf_counter() + d, n, out, dest, data))
        now = time.perf_counter()
        while q and q[0][0] <= now:
            _, _, out, dest, data = heapq.heappop(q)
            try:
                out.sendto(data, dest)
            except OSError:
                pass
        if a.parar:
            import os
            if os.path.exists(a.parar):
                break
    if st["paquetes"] - st["perdidos"]:
        st["retraso_ms"] = round(st["retraso_ms"] / max(1, st["paquetes"] - st["perdidos"]), 2)
    print(json.dumps(st), flush=True)


if __name__ == "__main__":
    main()

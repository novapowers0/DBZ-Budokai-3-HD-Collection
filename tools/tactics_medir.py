r"""Dragon Tactics H1/T1: cuanto cuesta en triangulos cada figura B3 (cache sk_<fid>.sk, formato HMS3 de vista_b2.py).

Escena::Pintar emite 3 vertices por triangulo; cada triangulo de cara trasera (normal media de espaldas a la camara) añade
otro de borde toon, y los triangulos con textura de rampa se cortan por los tramos de luz. Aqui se mide lo estatico
(triangulos, vertices, % de caras traseras en pose de reposo promediado en 4 giros con camara elevada 30 grados, triangulos
en textura de rampa); lo emitido de verdad lo da el log del juego (DBZ3_TEST_TACTICS_FIGURAS).
Uso: python tools\tactics_medir.py [carpeta_con_ingame_ui] > salida.md
"""
import struct
import sys
from pathlib import Path

import numpy as np

# (nombre, fid de la forma que enseña la figura de la rueda: pasarela_nave.cpp FormaFigB3)
CANDIDATAS = [("Goku", 264), ("Vegeta", 416), ("Krilin", 327), ("Piccolo", 350), ("Freezer (final)", 184), ("A-16", 70),
              ("A-18", 80), ("Cell (perfecto)", 148)]   # el orden de kIdsB2 de src/tactics/prueba_figuras.cpp
TOPE = 65535


def leer(path):
    d = Path(path).read_bytes()
    o = 4
    ntex, ntri, nb = struct.unpack_from("<3I", d, o)
    o += 12 + 4  # alto
    for _ in range(ntex):
        w, h = struct.unpack_from("<2I", d, o)
        o += 8 + w * h * 4
    padre, loc = [], []
    for _ in range(nb):
        padre.append(struct.unpack_from("<i", d, o)[0])
        o += 4 + 24
        loc.append(np.array(struct.unpack_from("<12f", d, o)).reshape(3, 4))
        o += 48 + 4 + 36
    tri = np.frombuffer(d, dtype=np.dtype([("v", [("p", "<f4", 8), ("h", "<u4")], 3), ("tex", "<i4"), ("fl", "<u4")]), count=ntri, offset=o)
    o += ntri * 116
    a = {"v": tri["v"]["p"].reshape(-1, 8), "h": tri["v"]["h"].reshape(-1)}
    tex = tri["tex"]
    ramas = []
    if d[:4] == b"HMS3":
        no = struct.unpack_from("<I", d, o)[0]
        o += 4
        for _ in range(no):
            k = struct.unpack_from("<I", d, o)[0]
            o += 4 + 8 * k
            ramas.append(k)
    return ntex, ntri, padre, loc, a, tex, ramas


def medir(path):
    ntex, ntri, padre, loc, a, tex, ramas = leer(path)
    W = []
    for i, l in enumerate(loc):
        m = np.vstack([l, [0, 0, 0, 1]])
        W.append(W[padre[i]] @ m if padre[i] >= 0 else m)
    W = np.array(W)
    h = a["h"].astype(int).clip(0, len(W) - 1)
    nrm = np.einsum("nij,nj->ni", W[h][:, :3, :3], a["v"][:, 3:6])
    nrm = nrm.reshape(ntri, 3, 3).sum(1)
    tras = 0.0
    el = np.radians(30)
    for yaw in np.radians([0, 90, 180, 270]):  # hacia la camara: (sin yaw cos el, sin el, cos yaw cos el)
        c = np.array([np.sin(yaw) * np.cos(el), np.sin(el), np.cos(yaw) * np.cos(el)])
        tras += np.mean(nrm @ c < 0) / 4
    en_rampa = int(sum(1 for t in tex if 0 <= t < len(ramas) and ramas[t] > 1))
    return ntri, tras, en_rampa


def main():
    raiz = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"D:\DBZ3HD3m")
    emitidos = []
    if len(sys.argv) > 2:
        lineas = [l for l in Path(sys.argv[2]).read_text(encoding="utf-8", errors="replace").splitlines() if "triangulos por figura:" in l]
        emitidos = [int(x) for x in lineas[-1].split("triangulos por figura:")[1].split()]
    print("| Figura | fid | triangulos | vertices | % caras traseras | tri. en rampa | tri+borde (sin cortes de rampa) | emitidos en juego (tri) | vert. en juego (minimo, 3/tri) | caben en 65.535 vert. |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    tot = tot_j = 0
    for i, (nombre, fid) in enumerate(CANDIDATAS):
        p = raiz / "ingame_ui" / "nave_vista" / ("sk_%d.sk" % fid)
        if not p.exists():
            print("| %s | %d | FALTA %s | | | | | | | |" % (nombre, fid, p))
            continue
        ntri, tras, rampa = medir(p)
        emit = ntri + int(ntri * tras)
        tot += emit
        j = emitidos[i] if i < len(emitidos) else 0
        tot_j += j
        print("| %s | %d | %d | %d | %.0f %% | %d | %d | %s | %s | %s |" % (nombre, fid, ntri, ntri * 3, tras * 100, rampa, emit,
              j or "-", j * 3 or "-", "%.1f" % (TOPE / 3 / j) if j else "-"))
    print("\nSuma de las 8 (una de cada): %d tri+borde estaticos; en juego %d tri = %d vert. = %.1f listas de 65.535" % (tot, tot_j, tot_j * 3, tot_j * 3 / TOPE))


if __name__ == "__main__":
    main()

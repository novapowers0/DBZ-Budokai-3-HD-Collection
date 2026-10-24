r"""iw_original_lote.py (B4, studies 5) - graba en el IW ORIGINAL (PCSX2 por PINE) los 18 minijuegos, uno tras otro, sin humano.

  python tools\iw_original_lote.py [salida=D:\tmp\b4\orig] [indices=0,1,2,...] [--seg 45]

Por minijuego (indice 0..18, el 4 no existe): carga el savestate 4, fuerza el minijuego (como forzar_mini_iw.py) y graba
durante --seg segundos la serie de camara/luchadores (formato de medir_mini_iw.py, la que lee lector_camara.serie_tsv)
y capturas a unos segundos fijos. Necesita la instancia 'principal' de PCSX2 libre (PINE 28011); la lanza y la cierra.
"""
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, r"E:\DBZ3HD_trabajo\R7\emu")
import medir_mini_iw as M  # noqa: E402
import sonda  # noqa: E402
from forzar_mini_iw import B, CLAVE  # noqa: E402

CAPS = tuple(range(3, 200, 6))


def un_minijuego(p, idx, salida, seg, dif=None):
    p.cargar(4)
    time.sleep(2.5)
    th = threading.Thread(target=lambda: sonda.teclas("right espera300 z espera800 down down z"))
    th.start()
    fin, hecho = time.time() + 25, False
    while time.time() < fin:
        if p.u32(B + 8) == 0x7D0:
            p.escribir(B + 4, idx.to_bytes(4, "little"))
            p.escribir(B + 8, CLAVE[idx].to_bytes(4, "little"))
            if dif is not None:
                p.escribir(B + 0xC, dif.to_bytes(2, "little"))
            hecho = True
            break
    th.join()
    if not hecho:
        return False
    clave = "0x%X" % CLAVE[idx]
    t0 = time.time()

    def caps():
        for c in CAPS:
            if c >= seg:
                break
            while time.time() - t0 < c:
                time.sleep(0.01)
            try:
                sonda.captura("b4_%s_%02d.png" % (clave, c))
            except Exception as e:   # noqa: BLE001
                print("captura", e)

    tc = threading.Thread(target=caps)
    tc.start()
    cab = ["t"] + ["%s%X" % (r, o) for r in ("J", "R") for o, _ in M.CAMPOS_L] + ["C%X" % o for o, _ in M.CAMPOS_C]
    n, prev = 0, None
    with open(Path(salida) / ("orig_%s.tsv" % clave), "w", encoding="utf-8") as f:
        f.write("\t".join(cab) + "\n")
        while time.time() - t0 < seg:
            ca, cam = p.lote([(0x49CA70, 4), (0x49CD6C, 4)])
            if not ca or not cam:
                time.sleep(0.005)
                continue
            pares, tipos = [], []
            for base in (ca + 0x800, ca + 0x28D0):
                for o, t in M.CAMPOS_L:
                    pares.append((base + o, {"f": 4, "H": 2, "I": 4}[t]))
                    tipos.append(t)
            for o, t in M.CAMPOS_C:
                pares.append((cam + o, 4))
                tipos.append(t)
            v = [M.conv(x, t) for x, t in zip(p.lote(pares), tipos)]
            if v != prev:
                f.write("%.3f\t" % (time.time() - t0) + "\t".join(str(x) for x in v) + "\n")
                n += 1
                prev = v
    tc.join()
    print(clave, "filas", n, flush=True)
    return True


def main(a):
    salida = Path(a[0] if a and not a[0].startswith("--") else r"D:\tmp\b4\orig")
    salida.mkdir(parents=True, exist_ok=True)
    idxs = [int(x) for x in a[1].split(",")] if len(a) > 1 and not a[1].startswith("--") else sorted(CLAVE)
    seg = float(a[a.index("--seg") + 1]) if "--seg" in a else 45
    estado = r"E:\DBZ3HD_trabajo\R7\emu\pcsx2\sstates\SLES-55347 (335A5A1F).04.p2s"
    try:
        p = sonda.Pine(espera=120) if sonda.nuestro_pid() else sonda.lanzar("iw", estado)
    except ConnectionRefusedError:   # lanzar() espera 60 s y el PC va lento: el emulador sigue arrancando
        p = sonda.Pine(espera=240)
    time.sleep(8)
    try:
        for i in idxs:
            if not un_minijuego(p, i, salida, seg):
                print("indice", i, "NO forzado", flush=True)
    finally:
        sonda.main(["cerrar"])


if __name__ == "__main__":
    main(sys.argv[1:])

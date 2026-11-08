"""iw_estados.py N claves... : savestates de IW EU justo al empezar cada minijuego (clave 0x7D0-0x7E1), en la instancia N de la granja.
Parte del estado 04 (menu); teclea hasta el selector, escribe indice/clave en 0x5C0DC4 (como iwx_original_cuadros) y guarda el slot 30+(clave-0x7D0).
Uso: python tools/iw_estados.py 1 7D0 7D1 ...   (la instancia ya lanzada con el ISO EU y el estado 04)"""
import os, struct, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcsx2_sonda as s, pcsx2_pool as P

INDICES = [0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18]
CLAVE_ADDR = 0x5C0DC4


def hacer(p, clave, intentos=3):
    for _ in range(intentos):
        p.cargar(4)
        time.sleep(3)
        if p.status() == 1:
            s._post(s.ventana_juego(s.nuestro_pid()), 0x75, True); time.sleep(.1); s._post(s.ventana_juego(s.nuestro_pid()), 0x75, False)
        hilo = threading.Thread(target=lambda: s.teclas('right espera300 z espera800 down down z'))
        hilo.start()
        fin = time.monotonic() + 30
        ok = False
        while time.monotonic() < fin:
            if p.u32(0x5C0DC8) == 0x7D0:
                p.escribir(CLAVE_ADDR, struct.pack('<II', INDICES[clave - 0x7D0], clave)); ok = True
                break
            time.sleep(.02)
        hilo.join()
        if ok:
            time.sleep(0.3)
            slot = 30 + clave - 0x7D0
            p.guardar(slot)
            return slot
    return None


if __name__ == '__main__':
    n = int(sys.argv[1]); P.registrar_en_sonda(s, n); p = s.Pine(espera=200)
    while True:
        try: p.titulo(); break
        except Exception: time.sleep(2)
    for c in sys.argv[2:]:
        c = int(c, 16)
        print('%X' % c, hacer(p, c), flush=True)

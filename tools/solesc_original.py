"""Medida acotada de nieve B2: PCSX2 propio esb/PINE 28025, sin tocar otras instancias."""
import configparser
import ctypes
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import threading
import time

os.environ.pop("DBZ3_PCSX2_INSTANCIA", None)
import pcsx2_sonda as s
from pcsx2_turnos import Turno
import pcsx2_cuadros as C
import pruebas as P


def main():
    raiz = Path(r"D:\tmp\solESC") / ("esb_" + time.strftime("%H%M%S"))
    raiz.mkdir(parents=True, exist_ok=False)
    original = Path(s.RAIZ) / "pcsx2_esc"
    # portable.ini del ejecutable original anula -datapath. Se reutilizan sus
    # ficheros instalados en una instancia realmente separada, sin caches/saves.
    for fichero in original.iterdir():
        if fichero.suffix.lower() in (".dll", ".exe") or fichero.name in ("qt.conf", "portable.ini"):
            shutil.copy2(fichero, raiz / fichero.name)
    for carpeta in ("resources", "QtPlugins"):
        shutil.copytree(original / carpeta, raiz / carpeta)
    (raiz / "inis").mkdir()
    (raiz / "sstates").mkdir()
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read(original / "inis/PCSX2.ini", encoding="utf-8-sig")
    ini["EmuCore"]["PINESlot"] = "28025"
    ini["MemoryCards"]["Slot1_Enable"] = "false"
    ini["Folders"]["Bios"] = str(original / "bios")
    for nombre, carpeta in (("Snapshots", "snaps"), ("Savestates", "sstates"),
                             ("MemoryCards", "memcards"), ("Logs", "logs"), ("Cache", "cache")):
        (raiz / carpeta).mkdir(exist_ok=True)
        ini["Folders"][nombre] = str(raiz / carpeta)
    ini["Hotkeys"]["TogglePause"] = "Keyboard/F6"
    ini["Hotkeys"]["FrameAdvance"] = "Keyboard/F7"
    with (raiz / "inis/PCSX2.ini").open("w", encoding="utf-8") as f:
        ini.write(f)
    estado = original / "sstates/SLES-51839 (FE09A7CC).08.p2s"
    shutil.copy2(estado, raiz / "sstates" / estado.name)
    s.INSTANCIA, s.EMU, s.PUERTO = "esb", str(raiz), 28025
    s.EXE = str(raiz / "pcsx2.exe")
    s.TURNO = Turno("esb", s.PUERTO, s.EXE,
                    cuarentena=r"D:\_PARA_BORRAR_CLAUDE\solESC\turnos")
    s.TURNO.adquirir(os.getpid(), sys.executable)
    proc, c = None, None
    fin = threading.Event()
    filas = []
    hilo = None
    os.environ["DBZ3_PCSX2_CUADROS_RESPALDO"] = r"D:\_PARA_BORRAR_CLAUDE\solESC"
    try:
        env = dict(os.environ, SDL_JOYSTICK_WGI="0", SDL_JOYSTICK_RAWINPUT="0",
                   SDL_JOYSTICK_HIDAPI="0", SDL_JOYSTICK_DIRECTINPUT="0", SDL_XINPUT_ENABLED="0")
        proc = subprocess.Popen([s.EXE, "-portable", "-nogui", "-fastboot",
                                 "-statefile", str(estado), "--", s.ISOS["b2"]], cwd=raiz, env=env)
        s.nuestro_pid = lambda: proc.pid if proc.poll() is None else None
        s.TURNO.adoptar(proc.pid)
        p = s.Pine(espera=180)
        # PINE abre antes de que exista la VM: ID devuelve fallo durante boot.
        limite = time.monotonic() + 180
        while p.status() == 2 and time.monotonic() < limite:
            if proc.poll() is not None:
                raise RuntimeError("PCSX2 terminó durante el arranque")
            time.sleep(.25)
        if p.status() == 2:
            raise RuntimeError("La VM no arrancó en 180 s")
        # PINE atiende un cliente cada vez: Cuadros debe compartir el socket.
        c = object.__new__(C.Cuadros)
        c.s, c.p, c.contador = s, p, 0x371550
        p.s.settimeout(30)
        limite = time.monotonic() + 60
        while p.u32(0x373460) != 5 and time.monotonic() < limite:
            time.sleep(.25)
        if p.u32(0x373460) != 5:
            raise RuntimeError("El estado no es entrenamiento en nieve (escenario 5)")
        # Un estado recién cargado aún no ha dibujado: PINE lo guarda SIN
        # Screenshot.png. Esperar campos reales antes de la primera captura.
        campo0 = p.u32(0x371550)
        limite = time.monotonic() + 30
        while ((p.u32(0x371550) - campo0) & 0xFFFFFFFF) < 60 and time.monotonic() < limite:
            time.sleep(.1)
        P._a_pantalla_secundaria(proc)
        print("esb", proc.pid, p.id(), flush=True)
        # Las posiciones son los dos vectores cuyo punto medio usa FUN_001b3180.
        mgr = p.u32(0x3A44B4)  # gp(0x3ABCF0) + signed(0x87C4)

        def muestra():
            jugadores = [p.u32(mgr + 0x19C54 + 4 * k) for k in range(2)]
            return dict(campo=p.u32(0x371550), zona=p.u32(0x3734AC),
                        escenario=p.u32(0x373460), gestor=hex(mgr),
                        posiciones=[list(struct.unpack("<4f", p.leer(mgr + 0x250 + 0x9F50 * k, 16)))
                                    for k in range(2)],
                        evento_activo=p.lote([(mgr + 0x19C5C, 2), (mgr + 0x19C90, 1), (mgr + 0x19C91, 1)]),
                        guion=[dict(puntero=hex(x), posicion=list(struct.unpack("<4f", p.leer(x + 0x40, 16))),
                                    giro=p.u32(x + 0xFAC),
                                    pendiente=p.lote([(x + 0x9C26, 2), (x + 0x9C28, 4)]),
                                    trayectoria=hex(p.u32(x + 0x9C34)))
                               for x in jugadores if 0x100000 <= x < 0x2000000])

        def foto(nombre):
            c.pausa()
            fila = muestra()
            filas.append(fila)
            fila["captura"] = c.captura(raiz / (nombre + ".png"))
            c.pulso(0x75)

        foto("antes")

        def jugar():
            secuencia = ["a a a a s", "right*4 a a s s", "s s s", "w*3", "a a a 1", "left*6", "right+a", "x x x", "z right right"]
            for i in range(100):
                if fin.is_set():
                    break
                s.teclas(secuencia[i % len(secuencia)], ms=90, pausa=90)

        hilo = threading.Thread(target=jugar)
        hilo.start()
        inicio, cambio, escena = time.monotonic(), None, None
        anterior = -1
        while time.monotonic() - inicio < 150:
            campo = p.u32(0x371550)
            if campo != anterior:
                filas.append(muestra())
                anterior = campo
                if filas[-1]["evento_activo"][0] and escena is None:
                    escena = time.monotonic()
                    print("inicio escena", filas[-1], flush=True)
                    foto("inicio_escena")
                if filas[-1]["zona"] == 1 and cambio is None:
                    cambio = time.monotonic()
                    print("cambio", filas[-1], flush=True)
                    foto("cambio")
                if cambio and time.monotonic() - cambio >= 6:
                    break
            time.sleep(.02)
        fin.set()
        if hilo:
            hilo.join(10)
        foto("despues")
        print("medida", len(filas), "cambio observado", cambio is not None, flush=True)
    finally:
        fin.set()
        if hilo:
            hilo.join(10)
        (raiz / "medida.json").write_text(json.dumps(filas, indent=2), encoding="utf-8")
        if c:
            c.p.s.close()
        if proc:
            P.cerrar_pid(proc.pid)
            proc.wait(timeout=10)
            s.TURNO.liberar(proc.pid)
        else:
            s.TURNO.liberar(os.getpid(), fuerza=True)


if __name__ == "__main__":
    main()

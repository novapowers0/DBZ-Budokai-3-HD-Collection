"""PCSX2: pausa/FrameAdvance documentados + contador RAM leido por PINE.

No usa memoria del proceso host ni modifica codigo del juego. Reutiliza sonda.py
y su reserva. Preparar los atajos antes de lanzar, restaurarlos despues de cerrar.
Ejemplo (B2 PAL; indicar un contador RAM previamente validado):
  python tools/pcsx2_cuadros.py --instancia luna preparar
  python tools/pcsx2_cuadros.py --instancia luna iniciar b2 --estado ruta.p2s
  python tools/pcsx2_cuadros.py --instancia luna pausa
  python tools/pcsx2_cuadros.py --instancia luna --contador 0x... serie D:/tmp/toma -n 30
  python tools/pcsx2_cuadros.py --instancia luna cerrar

Contador B2 SLES-51839 validado: 0x371550 (+1 por campo PAL). Cada campo
equivale a 1,2 cuadros de animacion: no confundirlo con un reloj de 60 Hz.
La imagen se obtiene de Screenshot.png del estado PINE sin avanzar; se coteja
tambien el contador guardado dentro de ese estado. IW/B1 necesitan su contador.

Fuentes: PCSX2 pcsx2/Hotkeys.cpp (TogglePause, FrameAdvance, Screenshot),
pcsx2/PINE.cpp (Read32, Status). PINE no proporciona FrameAdvance ni contador.
"""
import argparse
import configparser
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile


def conectar(instancia):
    sys.path.insert(0, os.environ.get("DBZ3_SONDA_DIR", r"E:\DBZ3HD_trabajo\R7\emu"))
    import sonda
    sonda.seleccionar_instancia(["--instancia", instancia])
    # HWND de 64 bits; sonda antigua no declara estos prototipos.
    from ctypes import wintypes as wt
    sonda.u32dll.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
    sonda.u32dll.PostMessageW.restype = wt.BOOL
    return sonda


def limitar_port(pruebas):
    """Incluye PCSX2 en el cupo del arnes existente, sin editar pruebas.py."""
    original = pruebas.Juego._tomar_plaza

    def tomar(juego, oculto=False):
        p = subprocess.run(["powershell", "-NoProfile", "-Command",
            "@(Get-Process dbz3,pcsx2 -ErrorAction SilentlyContinue).Count"],
            capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, text=True, check=True)
        if int(p.stdout.strip()) >= pruebas.TOPE_JUEGOS:
            return False
        return original(juego, oculto)

    pruebas.Juego._tomar_plaza = tomar


def fijar_reloj(ruta, cuadro):
    """Publica un cuadro entero; fopen del port puede retener el destino unos us."""
    ruta = Path(ruta)
    tmp = ruta.with_suffix(".tmp")
    tmp.write_text(str(cuadro), encoding="ascii")
    fin = time.monotonic() + 2
    while True:
        try:
            os.replace(tmp, ruta)
            return
        except PermissionError:
            if time.monotonic() >= fin:
                raise
            time.sleep(.005)


def preparar(s):
    if s.nuestro_pid():
        raise RuntimeError("Cerrar esta instancia antes de preparar los atajos")
    ini = Path(s.EMU) / "inis/PCSX2.ini"
    backup = Path(os.environ.get("DBZ3_PCSX2_CUADROS_TMP", r"D:\tmp\solP")) / ("PCSX2_" + s.INSTANCIA + ".ini.original")
    backup.parent.mkdir(parents=True, exist_ok=True)
    if backup.exists():
        raise RuntimeError("Ya hay una configuracion pendiente de restaurar: " + str(backup))
    shutil.copy2(ini, backup)
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(ini, encoding="utf-8-sig")
    cfg["Hotkeys"]["TogglePause"] = "Keyboard/F6"
    cfg["Hotkeys"]["FrameAdvance"] = "Keyboard/F7"
    cfg["Hotkeys"]["Screenshot"] = "Keyboard/F8"
    with ini.open("w", encoding="utf-8") as f:
        cfg.write(f)
    print("Atajos F6 pausa / F7 un cuadro / F8 captura; respaldo:", backup)


class Cuadros:
    def __init__(self, s, contador=None):
        self.s = s
        self.p = s.Pine()
        self.contador = contador

    def tecla(self, vk, abajo):
        self.s.TURNO.validar()
        h = self.s.ventana_juego(self.s.nuestro_pid())
        if not h:
            raise RuntimeError("No hay ventana de render")
        # SOLO la ventana superior. Enviar a todos los hijos duplica FrameAdvance.
        sc = self.s.u32dll.MapVirtualKeyW(vk, 0)
        ext = (1 << 24) if 0x25 <= vk <= 0x28 else 0
        lp = 1 | (sc << 16) | ext | (0 if abajo else 3 << 30)
        if not self.s.u32dll.PostMessageW(h, 0x100 if abajo else 0x101, vk, lp):
            raise ctypes.WinError(ctypes.get_last_error())

    def pulso(self, vk):
        self.tecla(vk, True)
        time.sleep(0.025)
        self.tecla(vk, False)

    def pausa(self):
        status = self.p.status()
        if status == 2:
            raise RuntimeError("VM apagada")
        if status == 0:
            self.pulso(0x75)
            # Al restaurar GS/compilar shaders se han medido casi3s de demora.
            # Esperar el mismo evento; NO reenviar TogglePause (lo desharia).
            for _ in range(200):
                time.sleep(0.05)
                if self.p.status() != 0:
                    break
        if self.p.status() != 1:
            raise RuntimeError("TogglePause no ha pausado PCSX2")

    def numero(self):
        if self.contador is None:
            raise RuntimeError("Falta --contador (direccion RAM del contador validado)")
        return self.p.u32(self.contador)

    def paso(self, n=1):
        self.pausa()
        ini = self.numero() if self.contador is not None else None
        for _ in range(n):
            anterior = self.numero() if self.contador is not None else None
            self.pulso(0x76)
            # FrameAdvance vuelve a pausa por si mismo; no alternar TogglePause.
            # Un campo puede tardar mas durante cargas: esperar el mismo pulso,
            # nunca reenviarlo (duplicaria un avance que ya estuviera en cola).
            time.sleep(.12)
            fin = time.monotonic() + 10
            while time.monotonic() < fin:
                time.sleep(.02)
                if self.p.status() == 1 and (anterior is None or self.numero() != anterior):
                    break
            if self.p.status() != 1:
                raise RuntimeError("FrameAdvance no volvio a pausa")
            if anterior is not None and (self.numero() - anterior) & 0xffffffff != 1:
                raise RuntimeError(f"Paso inexacto: contador {anterior} -> {self.numero()}")
        return ini, self.numero() if self.contador is not None else None

    def captura(self, destino):
        from PIL import Image
        self.pausa()
        antes = self.numero()
        # F8 solo se procesa en el siguiente VSync: estando pausado capturaria
        # OTRO cuadro. SaveState de PINE si toma una imagen GS sin avanzar.
        # Slot de trabajo libre: nunca sobrescribir estados de otros.
        estados = Path(self.s.EMU) / "sstates"
        serial = self.p.id()
        candidatos = list(estados.glob(serial + " (*).*.p2s"))
        if not candidatos:
            raise RuntimeError("Falta un savestate base para identificar el CRC del juego")
        prefijo = candidatos[0].name.rsplit(".", 2)[0]
        slot = next((i for i in range(99, 9, -1) if not (estados / f"{prefijo}.{i:02d}.p2s").exists()), None)
        if slot is None:
            raise RuntimeError("No hay slot temporal libre")
        src = estados / f"{prefijo}.{slot:02d}.p2s"
        self.p.guardar(slot)
        # Plazo de I/O, no avance temporal del juego: permanece pausado.
        fin = time.monotonic() + 20
        while time.monotonic() < fin:
            if src.exists():
                try:
                    with zipfile.ZipFile(src) as z:
                        png = z.read("Screenshot.png")
                        with z.open("eeMemory.bin") as ram:
                            ram.seek(self.contador)
                            guardado = int.from_bytes(ram.read(4), "little")
                    with Image.open(io.BytesIO(png)) as im:
                        im.load()
                        rgb = im.convert("RGB")
                        reducido = rgb.resize((rgb.width // 2, rgb.height // 2))
                    break
                except (OSError, ValueError, KeyError, zipfile.BadZipFile):
                    pass
            time.sleep(0.05)
        else:
            raise RuntimeError("SaveState PINE no produjo imagen estando pausado")
        despues = self.numero()
        if antes != despues or guardado != antes or self.p.status() != 1:
            raise RuntimeError("La VM avanzo durante la captura")
        destino = Path(destino)
        destino.parent.mkdir(parents=True, exist_ok=True)
        if destino.exists():
            raise FileExistsError(destino)
        reducido.save(destino)
        # El original generado se archiva, nunca se borra.
        archivo = Path(os.environ.get("DBZ3_PCSX2_CUADROS_RESPALDO", r"D:\_PARA_BORRAR_CLAUDE\solP")) / "captura_estados"
        archivo.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(archivo / (str(time.time_ns()) + "_" + src.name)))
        return dict(cuadro=antes, despues=despues, imagen=destino.name,
                    contador_en_estado=guardado,
                    metodo="PINE SaveState / Screenshot.png (sin avanzar)",
                    sha256=hashlib.sha256(rgb.tobytes()).hexdigest())

    def serie(self, destino, n, tecla=None):
        destino = Path(destino)
        destino.mkdir(parents=True, exist_ok=False)
        self.pausa()
        inicial = self.numero()
        filas = []
        try:
            if tecla is not None:
                self.tecla(tecla, True)
                time.sleep(0.04)
            for i in range(n):
                self.paso()
                if tecla is not None and i == 0:
                    self.tecla(tecla, False)
                numero = self.numero()
                fila = self.captura(destino / f"{i+1:03d}_cuadro_{numero:010d}.png")
                fila["delta"] = (numero - (filas[-1]["cuadro"] if filas else inicial)) & 0xffffffff
                filas.append(fila)
                (destino / "medida.json").write_text(json.dumps(dict(
                    version=self.p.version(), juego=self.p.id(), contador=hex(self.contador),
                    inicial=inicial, capturas=filas, completa=len(filas) == n,
                    consecutivas=all(f["delta"] == 1 for f in filas)), indent=2), encoding="utf-8")
        finally:
            if tecla is not None:
                self.tecla(tecla, False)
        return dict(inicial=inicial, final=self.numero(), capturas=len(filas),
                    imagenes_distintas=len({f["sha256"] for f in filas}))

    def medir(self, n, observar, tecla=None, mantener=1, al_campo=None):
        """RAM por campo consecutivo; permite medir limites sin guardar n estados GS."""
        if n < 1 or mantener < 1:
            raise ValueError("n y mantener deben ser positivos")
        self.pausa()
        filas = [dict(cuadro=self.numero(), datos=observar(self.p))]
        if al_campo is not None:
            al_campo(filas)
        try:
            if tecla is not None:
                self.tecla(tecla, True)
                time.sleep(.04)
            for i in range(n):
                self.paso()
                if tecla is not None and i + 1 == mantener:
                    self.tecla(tecla, False)
                numero = self.numero()
                delta = (numero - filas[-1]["cuadro"]) & 0xffffffff
                if delta != 1:
                    raise RuntimeError(f"Traza inexacta: delta={delta}")
                filas.append(dict(cuadro=numero, delta=delta, datos=observar(self.p)))
                if al_campo is not None:
                    al_campo(filas)
        finally:
            if tecla is not None:
                self.tecla(tecla, False)
        return filas


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--instancia", default="luna", choices=["principal", "luna", "esc", "kit", "pre"])
    ap.add_argument("--contador", type=lambda a: int(a, 0))
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("preparar")
    ini = sub.add_parser("iniciar")
    ini.add_argument("juego", choices=["b1", "b2", "b3", "iw"])
    ini.add_argument("--estado")
    sub.add_parser("pausa")
    sub.add_parser("numero")
    paso = sub.add_parser("avanzar")
    paso.add_argument("n", type=int)
    serie = sub.add_parser("serie")
    serie.add_argument("destino")
    serie.add_argument("-n", type=int, default=30)
    serie.add_argument("--tecla", type=lambda a: int(a, 0), help="VK mantenida solo durante el primer cuadro")
    sub.add_parser("cerrar")
    a = ap.parse_args()
    s = conectar(a.instancia)
    if a.cmd == "preparar":
        preparar(s)
    elif a.cmd == "iniciar":
        cantidad = subprocess.run(["powershell", "-NoProfile", "-Command",
            "@(Get-Process dbz3,pcsx2 -ErrorAction SilentlyContinue).Count"], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, text=True, check=True)
        if int(cantidad.stdout.strip()) >= __import__("pruebas").TOPE_JUEGOS:
            raise RuntimeError("Ya esta el cupo de juegos en el equipo; no se abre otra ventana")
        p = s.lanzar(a.juego, a.estado)
        p.s.close()
        c = Cuadros(s, a.contador)
        c.pausa()
        print(c.p.version(), c.p.id(), "pausado")
    elif a.cmd == "cerrar":
        pid_cierre=s.nuestro_pid()
        if pid_cierre:
            s.main(["--instancia", a.instancia, "cerrar"])
            # taskkill /F puede regresar antes de que Windows quite el proceso.
            # Esperar SU evento de salida, no concluir que sigue abierto ni
            # restaurar la configuracion mientras la VM la mantiene abierta.
            k=ctypes.WinDLL("kernel32",use_last_error=True)
            k.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32];k.OpenProcess.restype=ctypes.c_void_p
            k.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32];k.CloseHandle.argtypes=[ctypes.c_void_p]
            h=k.OpenProcess(0x100000,False,pid_cierre)
            if h:
                try:k.WaitForSingleObject(h,10000)
                finally:k.CloseHandle(h)
            if not s.nuestro_pid() and s.TURNO.path.exists():
                datos=s.TURNO._datos()
                if int(datos.get("pid","0"))==pid_cierre:s.TURNO.liberar(pid_cierre)
        elif s.TURNO.path.exists():
            datos = s.TURNO._datos()
            if s.TURNO._es_dueno_activo(datos):
                raise RuntimeError("La instancia aun esta iniciando; no se restaura su configuracion en marcha")
            s.TURNO.liberar(int(datos["pid"]))
        if s.nuestro_pid():
            raise RuntimeError("PCSX2 sigue abierto; configuracion no restaurada")
        backup = Path(os.environ.get("DBZ3_PCSX2_CUADROS_TMP", r"D:\tmp\solP")) / ("PCSX2_" + a.instancia + ".ini.original")
        if backup.exists():
            ini = Path(s.EMU) / "inis/PCSX2.ini"
            archivo = Path(os.environ.get("DBZ3_PCSX2_CUADROS_RESPALDO", r"D:\_PARA_BORRAR_CLAUDE\solP"))
            archivo.mkdir(parents=True, exist_ok=True)
            shutil.move(str(ini), str(archivo / f"PCSX2_usado_{time.time_ns()}.ini"))
            shutil.move(str(backup), str(ini))
            print("PCSX2 cerrado; configuracion original restaurada")
    else:
        c = Cuadros(s, a.contador)
        try:
            if a.cmd == "pausa":
                c.pausa()
                print("pausado, PINE status=1")
            elif a.cmd == "numero":
                print(c.numero())
            elif a.cmd == "avanzar":
                if a.n < 1:
                    ap.error("n debe ser positivo")
                print(c.paso(a.n))
            elif a.cmd == "serie":
                if a.n < 1:
                    ap.error("n debe ser positivo")
                print(c.serie(a.destino, a.n, a.tecla))
        finally:
            c.p.s.close()


if __name__ == "__main__":
    main()

"""PCSX2: pausa/FrameAdvance documentados + contador RAM leido por PINE.

No usa memoria del proceso host ni modifica codigo del juego. Reutiliza sonda.py
y su reserva. Preparar los atajos antes de lanzar, restaurarlos despues de cerrar.
Ejemplo (B2 PAL; indicar un contador RAM previamente validado):
  python tools/pcsx2_cuadros.py --instancia luna preparar
  python tools/pcsx2_cuadros.py --instancia luna iniciar b2 --estado ruta.p2s
  python tools/pcsx2_cuadros.py --instancia luna pausa
  python tools/pcsx2_cuadros.py --instancia luna --contador 0x... serie E:/DBZ3HD_trabajo/toma -n 30
  python tools/pcsx2_cuadros.py --instancia luna cerrar

Contador B2 SLES-51839 validado: 0x371550 (+1 por campo PAL). Cada campo
equivale a 1,2 cuadros de animacion: no confundirlo con un reloj de 60 Hz.
La imagen se obtiene de Screenshot.png del estado PINE sin avanzar; se coteja
tambien el contador guardado dentro de ese estado. IW EU: ver CONTADORES (validar con `validar`).

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
    import pcsx2_sonda as sonda
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
    backup = Path(os.environ.get("DBZ3_PCSX2_CUADROS_TMP", r"E:\DBZ3HD_trabajo\solP")) / ("PCSX2_" + s.INSTANCIA + ".ini.original")
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


# Contadores de cuadros por juego (direccion EE). b2: +1 por campo PAL (validado). iw: sube 8 cuentas por cada 6 ticks
# de logica (medido 7/11): no es 1 por cuadro, se valida con `validar` y se usa como reloj de juego, no como paso.
CONTADORES = {"b2": 0x371550, "iw": 0x4A4D40}
CAMPOS_POR_S = 50            # PAL: 1 campo = 20 ms de juego (B2 y IW EU)


class Cuadros:
    def __init__(self, s, contador=None, p=None):
        self.s = s
        self.p = p or s.Pine()
        self.contador = contador
        self.exacto = True       # False: el contador cuenta cuadros de juego, no campos (IW EU); paso() avanza hasta que cambie

    def tecla(self, vk, abajo):
        # Medido 8/11: validar el turno y buscar la ventana (tasklist + EnumWindows) costaba ~0,35 s POR TECLA y era casi todo el
        # tiempo de un paso (1,3 pasos/s). Se resuelven una vez cada 5 s; si PostMessage falla se vuelve a resolver.
        ahora = time.monotonic()
        if getattr(self, "_h", None) is None or ahora - self._h_t > 5:
            self.s.TURNO.validar()
            self._h = self.s.ventana_juego(self.s.nuestro_pid())
            self._h_t = ahora
            if not self._h:
                raise RuntimeError("No hay ventana de render")
        h = self._h
        # SOLO la ventana superior. Enviar a todos los hijos duplica FrameAdvance.
        sc = self.s.u32dll.MapVirtualKeyW(vk, 0)
        ext = (1 << 24) if 0x25 <= vk <= 0x28 else 0
        lp = 1 | (sc << 16) | ext | (0 if abajo else 3 << 30)
        if not self.s.u32dll.PostMessageW(h, 0x100 if abajo else 0x101, vk, lp):
            self._h = None                  # ventana vieja: se resuelve de nuevo en la siguiente tecla
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
        from pcsx2_turnos import comprobar_tope
        comprobar_tope()
        self.pausa()
        ini = self.numero() if self.contador is not None else None
        for _ in range(n):
            anterior = self.numero() if self.contador is not None else None
            # exacto (B2): el contador sube 1 por FrameAdvance. No exacto (IW EU): el contador cuenta CUADROS DE JUEGO, que en
            # escenas pesadas duran 2 campos de video; entonces se avanza campo a campo hasta que el contador cambie (maximo 4).
            for campo in range(4 if not self.exacto else 1):
                self.pulso(0x76)
                # FrameAdvance vuelve a pausa por si mismo; no alternar TogglePause. Un pulso en cola nunca se reenvia
                # antes de 2,5 s (duplicaria el avance); en modo no exacto la propia repeticion cubre un pulso perdido.
                time.sleep(.03 if anterior is not None else .12)
                t_pulso = time.monotonic()
                fin = t_pulso + (10 if self.exacto else 3)
                reenviado = anterior is None or not self.exacto
                while time.monotonic() < fin:
                    time.sleep(.02)
                    if self.p.status() == 1 and (anterior is None or self.numero() != anterior):
                        break
                    if not reenviado and time.monotonic() - t_pulso > 2.5:    # un pulso en cola ya se habria ejecutado: se perdio
                        reenviado = True
                        self.pulso(0x76)
                if self.p.status() != 1:
                    self._recuperar_pausa()
                if self.exacto or anterior is None or self.numero() != anterior:
                    break
            if self.exacto and not os.environ.get("PCSX2_PASO_TOLERANTE") and anterior is not None and (self.numero() - anterior) & 0xffffffff != 1:
                raise RuntimeError(f"Paso inexacto: contador {anterior} -> {self.numero()}")
            if not self.exacto and anterior is not None and self.numero() == anterior:
                raise RuntimeError(f"El contador no avanza tras 4 campos: {anterior}")
        return ini, self.numero() if self.contador is not None else None

    def _recuperar_pausa(self, espera=20):
        """FrameAdvance no devolvio la VM a pausa (carga o cambio de escena que la mantiene en marcha): se espera y, si sigue
        corriendo, se vuelve a pausar con TogglePause. Solo aborta si la VM se apaga o no hay forma de pausarla."""
        from pcsx2_turnos import comprobar_tope, registrar
        registrar("pausa_perdida", instancia=getattr(self.s, "INSTANCIA", "?"))
        fin = time.monotonic() + espera
        while time.monotonic() < fin:
            comprobar_tope()
            st = self.p.status()
            if st == 1:
                return
            if st == 2:
                raise RuntimeError("VM apagada")
            time.sleep(0.1)
        for _ in range(3):
            self.pausa()
            if self.p.status() == 1:
                return
        raise RuntimeError("no consigo pausar PCSX2 tras FrameAdvance")

    def paso_leer(self, anterior, pares, hold=0.008):
        """UN paso de juego leyendo `pares` en la misma llamada PINE que el estado y el contador (mejora 8/11, antes eran ~6 llamadas
        y esperas fijas por paso). `anterior` = contador antes del paso. Devuelve (contador nuevo, valores de `pares`) leidos con la VM
        en pausa tras el avance. Si el contador no cambia en 0,15 s se reenvia el pulso (cuadro de juego de 2 campos o pulso perdido;
        un pulso en cola ya se habria ejecutado en ese plazo). El contador cuenta cuadros de juego: valido para IW y B2."""
        from pcsx2_turnos import comprobar_tope
        self.tecla(0x76, True)
        time.sleep(hold)
        self.tecla(0x76, False)
        t0, reenvios = time.monotonic(), 0
        while True:
            comprobar_tope()
            estado, v = self.p.lote_estado([(self.contador, 4)] + list(pares))
            if estado == 2:
                raise RuntimeError("VM apagada")
            if estado == 1 and v[0] != anterior:
                return v[0], v[1:]
            if estado == 0 and time.monotonic() - t0 > 5:      # sigue corriendo: se vuelve a pausar en vez de abortar
                self._recuperar_pausa()
                t0 = time.monotonic()
                continue
            if estado == 1 and time.monotonic() - t0 > 0.15:
                if reenvios >= 6:
                    raise RuntimeError("el contador no avanza tras 6 pulsos: %d" % anterior)
                reenvios += 1
                self.pulso(0x76)
                t0 = time.monotonic()
            time.sleep(0.002)

    def reanudar(self):
        """Quita la pausa (TogglePause) y espera a que PCSX2 corra."""
        if self.p.status() == 1:
            self.pulso(0x75)
            for _ in range(200):
                time.sleep(0.05)
                if self.p.status() == 0:
                    return
            raise RuntimeError("TogglePause no reanudo PCSX2")

    def avanzar_hasta(self, objetivo, margen=150, vigilar=None):
        """Lleva el contador EXACTAMENTE a `objetivo` (campos del juego, no segundos): corre libre hasta `margen` campos antes,
        pausa y avanza de uno en uno. `vigilar()` se llama en cada sondeo libre (p. ej. para leer un anillo de traza).
        Deja el emulador en pausa. Si ya se paso del objetivo lo dice (no retrocede)."""
        if self.numero() > objetivo:
            raise RuntimeError("contador %d ya pasa del objetivo %d" % (self.numero(), objetivo))
        margen = max(margen, 2 * getattr(self, "latencia", 0) + 10)      # la pausa tarda 0,4-0,7 s (20-35 campos) con el PC cargado
        if objetivo - self.numero() > margen + 5:
            self.reanudar()
            while objetivo - self.numero() > margen:
                if vigilar:
                    vigilar()
                from pcsx2_turnos import comprobar_tope
                comprobar_tope()
                time.sleep(0.004)
            disparo = self.numero()
        else:
            disparo = None
        self.pausa()
        actual = self.numero()
        if disparo is not None:
            self.latencia = max(getattr(self, "latencia", 0), actual - disparo)
        if actual > objetivo:
            raise RuntimeError("la pausa llego tarde: contador %d > objetivo %d (sube el margen)" % (actual, objetivo))
        while actual < objetivo:
            self.paso()
            actual = self.numero()
            if vigilar:
                vigilar()
        return actual

    def guion_a_eventos(self, guion, inicio, ms=120, pausa=150):
        """Guion de sonda.teclas ('z espera1500 down*3 z+right') -> [(campo, vk, abajo)] en campos del juego desde `inicio`.
        Los milisegundos del guion se convierten a campos (50 por segundo), asi el guion no depende del reloj del PC."""
        ev, t = [], inicio
        campos = lambda m: max(1, round(m * CAMPOS_POR_S / 1000))   # noqa: E731
        for tok in guion.split():
            if tok.startswith("espera"):
                t += campos(int(tok[6:]))
                continue
            rep = 1
            if "*" in tok:
                tok, r = tok.split("*")
                rep = int(r)
            ks = [self.s.VK[self.s.ALIAS.get(k, k)] for k in tok.lower().split("+")]
            for _ in range(rep):
                ev += [(t, k, True) for k in ks]
                t += campos(ms)
                ev += [(t, k, False) for k in ks]
                t += campos(pausa)
        return sorted(ev, key=lambda e: (e[0], e[2]))     # al mismo campo, primero se suelta y luego se pulsa

    def reproducir(self, guion, fin=None, vigilar=None, ms=120, pausa=150):
        """Reproduce el guion por CAMPOS del juego: cada tecla cae en el mismo campo en todas las pasadas.
        Termina en `fin` (campo absoluto) si se da. Devuelve el campo inicial y el final."""
        self.pausa()
        inicio = self.numero()
        for campo, vk, abajo in self.guion_a_eventos(guion, inicio, ms, pausa):
            self.avanzar_hasta(campo, vigilar=vigilar)
            self.tecla(vk, abajo)
        if fin is not None:
            self.avanzar_hasta(fin, vigilar=vigilar)
        return inicio, self.numero()

    def validar(self, n=12):
        """Valida el contador: n FrameAdvance sueltos (sin repetir) y los incrementos del contador. Todos 1 = sube un punto por
        campo de video; algun 0 = el contador cuenta cuadros de juego y una escena pesada gasta 2 campos (IW EU)."""
        self.pausa()
        d = []
        for _ in range(n):
            a = self.numero()
            self.pulso(0x76)
            time.sleep(.15)
            for _ in range(500):
                if self.p.status() == 1:
                    break
                time.sleep(.02)
            d.append((self.numero() - a) & 0xffffffff)
        return d

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


def autoprueba():
    import pcsx2_sonda as sonda
    c = Cuadros.__new__(Cuadros)
    c.s = sonda
    ev = c.guion_a_eventos("z espera1500 right*2 z+x", 1000)
    assert ev[0] == (1000, 0x5A, True) and ev[1][2] is False and ev[1][0] == 1006, ev[:3]   # 120 ms = 6 campos
    assert [e for e in ev if e[1] == 0x27][0][0] == 1006 + 8 + 75, ev       # 150 ms = 8 campos (7,5 redondeado), 1500 ms = 75
    assert sorted(ev, key=lambda e: e[0]) == ev
    assert CONTADORES["b2"] == 0x371550 and CONTADORES["iw"] == 0x4A4D40
    print("AUTOPRUEBA pcsx2_cuadros OK")


def main():
    if "--autoprueba" in sys.argv:
        return autoprueba()
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
    val = sub.add_parser("validar", help="avanza n cuadros y muestra los incrementos del contador")
    val.add_argument("-n", type=int, default=12)
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
        backup = Path(os.environ.get("DBZ3_PCSX2_CUADROS_TMP", r"E:\DBZ3HD_trabajo\solP")) / ("PCSX2_" + a.instancia + ".ini.original")
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
            elif a.cmd == "validar":
                print(c.validar(a.n))
            elif a.cmd == "serie":
                if a.n < 1:
                    ap.error("n debe ser positivo")
                print(c.serie(a.destino, a.n, a.tecla))
        finally:
            c.p.s.close()


if __name__ == "__main__":
    main()

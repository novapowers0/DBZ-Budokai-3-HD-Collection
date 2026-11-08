# -*- coding: utf-8 -*-
"""Cerrojos atómicos de PCSX2 para las sondas PINE de studies 5."""
import ctypes
import json
import os
import sys
import shutil
import threading
import time
from ctypes import wintypes
from datetime import datetime
from pathlib import Path

K32 = ctypes.WinDLL("kernel32", use_last_error=True)
K32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
K32.OpenProcess.restype = ctypes.c_void_p
K32.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
K32.GetExitCodeProcess.restype = wintypes.BOOL
K32.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, wintypes.DWORD, wintypes.LPWSTR,
                                           ctypes.POINTER(wintypes.DWORD)]
K32.QueryFullProcessImageNameW.restype = wintypes.BOOL
K32.CloseHandle.argtypes = [ctypes.c_void_p]

K32.GetSystemCpuSetInformation.argtypes = [ctypes.c_void_p, wintypes.ULONG, ctypes.POINTER(wintypes.ULONG), ctypes.c_void_p, wintypes.ULONG]
K32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
K32.SetPriorityClass.argtypes = [ctypes.c_void_p, wintypes.DWORD]

ENTREGAS_DEFAULT = Path(r"C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\studies 5\ENTREGAS")
CUARENTENA_DEFAULT = Path(r"D:\_PARA_BORRAR_CLAUDE") / "pcsx2_huerfanas"


def process_image(pid):
    h = K32.OpenProcess(0x1000, False, int(pid))
    if not h:
        return ""
    try:
        buf = ctypes.create_unicode_buffer(32768)
        n = wintypes.DWORD(len(buf))
        return buf.value if K32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) else ""
    finally:
        K32.CloseHandle(h)


def process_alive(pid):
    h = K32.OpenProcess(0x1000, False, int(pid))
    if not h:
        return False
    try:
        code = wintypes.DWORD()
        return bool(K32.GetExitCodeProcess(h, ctypes.byref(code))) and code.value == 259
    finally:
        K32.CloseHandle(h)


class Turno:
    """Reserva una instanciaN en _pcsx2 hasta que termine su PCSX2."""

    def __init__(self, instancia, puerto, exe, entregas=None, cuarentena=None):
        root = Path(entregas or os.environ.get("DBZ3_STUDIES5_ENTREGAS", ENTREGAS_DEFAULT))
        self.instancia = str(instancia)
        self.puerto = int(puerto)
        self.exe = os.path.normcase(os.path.abspath(exe))
        self.path = root / "_pcsx2" / self.instancia
        self.cuarentena = Path(cuarentena or os.environ.get("DBZ3_PCSX2_QUARANTINE", CUARENTENA_DEFAULT))

    def _datos(self):
        f = self.path / "quien.txt"
        d = {}
        for l in f.read_text(encoding="utf-8", errors="replace").splitlines():
            k, sep, v = l.partition("=")
            if sep:
                d[k.strip().lower()] = v.strip()
        return d

    def _es_dueno_activo(self, datos):
        try:
            pid = int(datos.get("pid", "0"))
            puerto = int(datos.get("puerto", "0"))
        except ValueError:
            return False
        image = os.path.normcase(os.path.abspath(datos.get("exe", "")))
        real = os.path.normcase(os.path.abspath(process_image(pid))) if pid else ""
        return bool(image) and real == image and process_alive(pid)

    def _mover(self, carpeta):
        if not self.path.exists():
            return
        carpeta = Path(carpeta)
        carpeta.mkdir(parents=True, exist_ok=True)
        base = carpeta / f"{self.instancia}_{datetime.now():%Y%m%d-%H%M%S}"
        dst, n = base, 2
        while dst.exists():
            dst = base.with_name(f"{base.name}_{n}")
            n += 1
        shutil.move(str(self.path), str(dst))
        print(f"PCSX2: cerrojo archivado en {dst}", flush=True)

    def _apartar(self):
        self._mover(self.cuarentena)

    def adquirir(self, pid, exe):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        while True:
            try:
                self.path.mkdir()
                self._escribir(pid, exe)
                return
            except FileExistsError:
                try:
                    datos = self._datos()
                except OSError:
                    datos = {}
                if self._es_dueno_activo(datos):
                    if int(datos.get("puerto", "0")) != self.puerto:
                        raise RuntimeError(f"{self.instancia} está activa en PINE {datos.get('puerto')}, no se reutiliza en {self.puerto}")
                    raise RuntimeError(f"{self.instancia} ya está reservada en PINE {self.puerto} por PID {datos.get('pid')}")
                self._apartar()

    def _escribir(self, pid, exe):
        tmp = self.path / "quien.txt.tmp"
        tmp.write_text(f"pid={int(pid)}\npuerto={self.puerto}\ninstancia={self.instancia}\n"
                       f"exe={os.path.abspath(exe)}\nhora={datetime.now():%Y%m%d-%H%M%S}\n", encoding="utf-8")
        os.replace(tmp, self.path / "quien.txt")

    def adoptar(self, pid):
        datos = self._datos()
        if int(datos.get("pid", "0")) != os.getpid():
            raise RuntimeError("el proceso de sonda no posee el cerrojo provisional")
        self._escribir(pid, self.exe)

    def validar(self):
        try:
            datos = self._datos()
        except OSError:
            raise RuntimeError(f"PINE bloqueado: falta reservar {self.instancia} (usa sonda.py reservar)")
        if not self._es_dueno_activo(datos):
            self._apartar()
            raise RuntimeError(f"PINE bloqueado: el cerrojo {self.instancia} era huérfano; vuelve a reservar")
        if os.path.normcase(os.path.abspath(datos.get("exe", ""))) != self.exe:
            raise RuntimeError(f"PINE bloqueado: {self.instancia} está iniciando; espera a que PCSX2 quede disponible")
        if int(datos["puerto"]) != self.puerto:
            raise RuntimeError(f"PINE bloqueado: {self.instancia} pertenece al puerto {datos['puerto']}, no {self.puerto}")
        return int(datos["pid"])

    def liberar(self, pid, fuerza=False):
        try:
            datos = self._datos()
            owner = int(datos.get("pid", "0"))
        except (OSError, ValueError):
            return False
        if owner != int(pid):
            raise RuntimeError(f"no se libera {self.instancia}: el PID {pid} no es su dueño ({owner})")
        if not fuerza and self._es_dueno_activo(datos):
            raise RuntimeError(f"no se libera {self.instancia}: PCSX2 PID {owner} aún está activo")
        self._mover(self.cuarentena.parent / "pcsx2_turnos_liberados")
        return True


# ---------------------------------------------------------------- mejora 1 (7/11): nucleos rapidos, prioridad, grabacion exclusiva
TRABAJO = Path(os.environ.get("DBZ3_EMU_TRABAJO", r"E:\DBZ3HD_trabajo\emu_turnos"))
REGISTRO = TRABAJO / "registro.jsonl"      # una linea JSON por evento: espera / grabacion / afinidad / reconexion


def registrar(evento, **datos):
    """Anade una linea al registro (esperas, grabaciones fallidas...). Nunca rompe a quien la llama."""
    try:
        TRABAJO.mkdir(parents=True, exist_ok=True)
        with open(REGISTRO, "a", encoding="utf-8") as f:
            f.write(json.dumps(dict(hora=datetime.now().isoformat(timespec="seconds"), pid=os.getpid(),
                                    evento=evento, **datos), ensure_ascii=False) + chr(10))
    except OSError:
        pass


def nucleos_rapidos():
    """Mascara de CPUs logicas de la clase de eficiencia MAS ALTA (nucleos P en un Intel hibrido); 0 si todos son iguales.
    Se detecta con GetSystemCpuSetInformation (SYSTEM_CPU_SET_INFORMATION: EfficiencyClass en el byte 18, LogicalProcessorIndex en el 14)."""
    n = wintypes.ULONG(0)
    K32.GetSystemCpuSetInformation(None, 0, ctypes.byref(n), None, 0)
    if not n.value:
        return 0
    buf = ctypes.create_string_buffer(n.value)
    if not K32.GetSystemCpuSetInformation(buf, n.value, ctypes.byref(n), None, 0):
        return 0
    cpus, o = [], 0
    while o + 8 <= n.value:
        tam = int.from_bytes(buf.raw[o:o + 4], "little")
        if tam < 20:
            break
        if int.from_bytes(buf.raw[o + 4:o + 8], "little") == 0:      # tipo CpuSet
            cpus.append((buf.raw[o + 18], buf.raw[o + 14]))           # (clase de eficiencia, indice logico)
        o += tam
    if not cpus or len({c for c, _ in cpus}) < 2 or max(i for _, i in cpus) >= 64:
        return 0
    alta = max(c for c, _ in cpus)
    return sum(1 << i for c, i in cpus if c == alta)


def afinar(pid):
    """Pone al proceso (el PCSX2 que acabamos de lanzar) en los nucleos rapidos y prioridad alta. Solo a ese pid.
    DBZ3_SIN_AFINIDAD=1 lo desactiva (para medir el antes). Devuelve la mascara usada (0 = no se toco la afinidad)."""
    if os.environ.get("DBZ3_SIN_AFINIDAD"):
        registrar("afinidad", pid_emu=int(pid), mascara=0, nota="desactivada por entorno")
        return 0
    h = K32.OpenProcess(0x0200 | 0x1000, False, int(pid))       # SET_INFORMATION | QUERY_LIMITED
    if not h:
        return 0
    try:
        mask = nucleos_rapidos()
        if mask:
            K32.SetProcessAffinityMask(h, mask)
        K32.SetPriorityClass(h, 0x80)                            # HIGH_PRIORITY_CLASS (no tiempo real: no congela el PC)
        registrar("afinidad", pid_emu=int(pid), mascara=hex(mask), prioridad="alta")
        return mask
    finally:
        K32.CloseHandle(h)


VENCIDA = threading.Event()      # se activa cuando la grabacion en curso agota su tope de tiempo


def comprobar_tope():
    """Los bucles largos de grabacion lo llaman: si el tope vencio, cortan con TimeoutError (el turno se libera)."""
    if VENCIDA.is_set():
        raise TimeoutError("tope de tiempo de la grabacion agotado")


class Grabacion:
    """Turno exclusivo para grabar el original: nunca dos grabaciones a la vez en el PC (todas las sesiones lo comparten).
    with Grabacion('iw 0x7D1') as g: ...   Registra la espera y si la grabacion acabo bien o con fallo."""

    def __init__(self, nombre, espera_max=3600, tope_s=None):
        self.nombre, self.espera_max = nombre, espera_max
        # 8/11 (usuario): primero el codigo; el emulador solo valida, en tomas cortas. Tope 5 min; mas largo solo con
        # DBZ3_GRABACION_MOTIVO explicando por que el decompilado/interprete no basta (queda en el registro).
        self.tope_s = float(os.environ.get("DBZ3_GRABACION_TOPE_S", 300)) if tope_s is None else tope_s
        self.motivo = os.environ.get("DBZ3_GRABACION_MOTIVO", "")
        if self.tope_s > 300 and not self.motivo:
            print("grabacion: tope %d s recortado a 300 s (pon DBZ3_GRABACION_MOTIVO para tomas largas)" % self.tope_s,
                  flush=True)
            self.tope_s = 300.0
        self.turno = Turno("grabacion", 0, sys.executable, entregas=TRABAJO, cuarentena=TRABAJO / "_cuarentena")

    def __enter__(self):
        t0 = time.time()
        avisado = False
        while True:
            try:
                self.turno.adquirir(os.getpid(), sys.executable)
                break
            except RuntimeError as e:
                if time.time() - t0 > self.espera_max:
                    registrar("grabacion_sin_turno", nombre=self.nombre, espera_s=round(time.time() - t0))
                    raise
                if not avisado:
                    print("grabacion: espero turno (%s)" % e, flush=True)
                    avisado = True
                time.sleep(5)
        self.t_ini = time.time()
        registrar("grabacion_turno", nombre=self.nombre, espera_s=round(self.t_ini - t0, 1), tope_s=self.tope_s,
                  motivo=self.motivo)
        VENCIDA.clear()

        def vencer():
            VENCIDA.set()
            print("AVISO: la grabacion %r agoto su tope de %d s; se corta para soltar el turno" % (self.nombre, self.tope_s), flush=True)
            registrar("grabacion_tope", nombre=self.nombre, tope_s=self.tope_s)
        self._reloj = threading.Timer(self.tope_s, vencer)
        self._reloj.daemon = True
        self._reloj.start()
        return self

    def __exit__(self, tipo, exc, tb):
        self._reloj.cancel()
        VENCIDA.clear()
        registrar("grabacion_fin", nombre=self.nombre, ok=tipo is None, dur_s=round(time.time() - self.t_ini, 1),
                  error=repr(exc) if exc else "")
        try:
            self.turno.liberar(os.getpid(), fuerza=True)
        except RuntimeError:
            pass
        return False


def resumen_registro():
    """Cifras del registro: esperas de turno, grabaciones (ok/fallidas) y reconexiones."""
    ev = []
    if REGISTRO.exists():
        ev = [json.loads(l) for l in REGISTRO.read_text(encoding="utf-8").splitlines() if l.strip()]
    esp = [e["espera_s"] for e in ev if e["evento"] == "grabacion_turno"]
    fin = [e for e in ev if e["evento"] == "grabacion_fin"]
    return dict(grabaciones=len(fin), fallidas=sum(1 for e in fin if not e["ok"]), espera_total_s=round(sum(esp), 1),
                espera_max_s=max(esp or [0]), reconexiones=sum(1 for e in ev if e["evento"] == "reconexion"),
                partidas_retomadas=sum(1 for e in ev if e["evento"] == "retomada"),
                partidas_perdidas=sum(1 for e in ev if e["evento"] == "partida_perdida"))


def autoprueba():
    import tempfile
    m = nucleos_rapidos()
    assert m == 0 or bin(m).count("1") >= 1
    global TRABAJO, REGISTRO
    with tempfile.TemporaryDirectory() as d:
        TRABAJO, REGISTRO = Path(d), Path(d) / "registro.jsonl"
        with Grabacion("prueba"):
            try:
                Grabacion("otra", espera_max=0).__enter__()
                raise AssertionError("dos grabaciones a la vez")
            except RuntimeError:
                pass
        try:
            with Grabacion("falla"):
                raise ValueError("x")
        except ValueError:
            pass
        try:
            with Grabacion("tope", tope_s=0.1):
                time.sleep(0.3)
                comprobar_tope()
            raise AssertionError("el tope no corto")
        except TimeoutError:
            pass
        r = resumen_registro()
        assert r["grabaciones"] == 3 and r["fallidas"] == 2, r
    print("AUTOPRUEBA pcsx2_turnos OK; nucleos rapidos mascara", hex(m), "(%d CPUs)" % bin(m).count("1"))


if __name__ == "__main__":
    import sys as _s
    if "--autoprueba" in _s.argv:
        autoprueba()
    else:
        print(json.dumps(resumen_registro(), indent=1))

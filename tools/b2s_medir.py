"""Medidas B2S: estados PCSX2 propios, RAM y registros GS; nunca estima alfa por color.

  python tools/b2s_medir.py estado fichero.p2s
  python tools/b2s_medir.py original --salida D:/tmp/solB2S/original

Los registros GS de un estado son SOLO el ultimo dibujo. No se atribuyen al humo:
para ello hace falta identificar su paquete GIF/material. Las capturas se reducen
a la mitad; los estados temporales se archivan sin borrar. Reutiliza Cuadros/PINE.
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
import struct
import subprocess
import sys
import threading
import time
import zipfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(r"D:\tmp\solB2S")
ARCHIVO = Path(r"D:\_PARA_BORRAR_CLAUDE\solB2S")


def guardar(ruta, datos):
    ruta.parent.mkdir(parents=True, exist_ok=True)
    ruta.write_text(json.dumps(datos, indent=2, ensure_ascii=False, default=str), encoding="utf-8")


def registros_gs(gs):
    # PCSX2 GSState::Freeze, version 9: u32 version, 15 registros globales
    # de 8 bytes y dos contextos de 12 registros de 8 bytes. No es un GS dump.
    version = struct.unpack_from("<I", gs)[0]
    if version != 9 or len(gs) != 4194813:
        return dict(version=version, bytes=len(gs), compatible=False)
    ctx = []
    for i in range(2):
        off = 4 + 15 * 8 + i * 12 * 8
        vals = struct.unpack_from("<12Q", gs, off)
        alpha = vals[7]
        ctx.append(dict(contexto=i, TEX0=hex(vals[1]), ALPHA=hex(alpha),
            A=alpha & 3, B=(alpha >> 2) & 3, C=(alpha >> 4) & 3,
            D=(alpha >> 6) & 3, FIX=(alpha >> 32) & 255))
    return dict(version=version, compatible=True, contextos=ctx,
                limite="Estado GS final, no prueba del material del humo")


def estado(ruta):
    with zipfile.ZipFile(ruta) as z:
        ram = z.read("eeMemory.bin")
        gs = z.read("GS.bin")
    etiquetas = []
    for nombre in (b"KEMURI_NULL", b"KEMURI_1\0", b"STC_BASE\0"):
        desde = 0
        while (off := ram.find(nombre, desde)) >= 0:
            etiquetas.append(dict(etiqueta=nombre.rstrip(b"\0").decode(), ram=hex(off)))
            desde = off + len(nombre)
    return dict(estado=str(ruta), sha256=hashlib.sha256(Path(ruta).read_bytes()).hexdigest(),
        contador_b1=struct.unpack_from("<I", ram, 0x583a10)[0],
        gs=registros_gs(gs), etiquetas=etiquetas)


def tablero(ruta, iso, salida):
    """Oraculo independiente: quad del archivo contra los vertices vivos del original."""
    sys.path.insert(0, str(ROOT / "mod center hd"))
    sys.path.insert(0, str(ROOT / "mod center hd/pasarela"))
    from iso import Iso
    from pasarela import Afs
    from PIL import Image
    with zipfile.ZipFile(ruta) as z:
        ram = z.read("eeMemory.bin")
        with Image.open(io.BytesIO(z.read("Screenshot.png"))) as im:
            salida.mkdir(parents=True, exist_ok=True)
            im.resize((im.width // 2, im.height // 2)).save(salida / "tablero_original.png")
    b = Afs(Iso(str(iso)), "/USR/DATA_EN.AFS").entry(667)
    u = lambda p: struct.unpack_from("<I", ram, p)[0]
    ronda, manager = u(0x5e9c14), u(0x361ec8)
    if not 0x100000 < manager < len(ram) - 40 * 0x110:
        raise ValueError("No es el estado B1 EU del tablero esperado")
    filas = []
    for slot in list(range(16, 27)) + [27, 28, 29]:
        reg = manager + slot * 0x110
        tarea = u(reg + 0xe0)
        if not tarea:
            continue
        k = slot - 16
        anim = (60 if k < ronda else 70) + k if slot <= 26 else 60+ronda if slot==27 else 49+ronda if slot==28 else 81
        r = struct.unpack_from("<I", b, 12)[0] + 64 * anim
        quad = [struct.unpack_from("<f", b, r+i)[0] for i in (0,4,16,20)]
        escala = struct.unpack_from("<2f", ram, tarea+0x50)
        original = [struct.unpack_from("<f", ram, reg+0x10+i)[0] for i in (0,4,16,20)]
        calculado = [v*escala[i%2] for i,v in enumerate(quad)]
        error = max(abs(a-c) for a,c in zip(original,calculado))
        filas.append(dict(slot=slot, anim=anim, escala=escala, quad_original=original,
                          quad_del_archivo=calculado, error_max=error,
                          ancla=struct.unpack_from("<2f", ram, reg+0x50)))
    resultado = dict(estado=estado(ruta), ronda=ronda, manager=hex(manager),
        ama_sha256=hashlib.sha256(b).hexdigest(), sprites=filas,
        max_error=max(f["error_max"] for f in filas),
        metodo="RAM de PCSX2 .04 contra #AMA 667 del disco, sin ajustar por imagen")
    guardar(salida / "tablero_ram.json", resultado)
    assert len(filas)==13 and resultado["max_error"] < 1e-4, resultado
    print("Tablero: 13 sprites originales, error de quad <", resultado["max_error"], flush=True)


def captura_estado(c, destino):
    """En el primer campo PCSX2 puede no tener Screenshot.png: conservar RAM/GS.

    Una imagen ausente es un resultado, no motivo para guardar/reintentar el
    mismo cuadro. Se comprueban los contadores igual que en Cuadros.captura.
    """
    from PIL import Image
    c.pausa()
    antes = c.numero()
    estados = Path(c.s.EMU) / "sstates"
    prefijo = "SLES-51233 (37472BE0)"
    slot = next(i for i in range(98, 9, -1) if not (estados / f"{prefijo}.{i:02d}.p2s").exists())
    src = estados / f"{prefijo}.{slot:02d}.p2s"
    c.p.guardar(slot)
    fin = time.monotonic() + 20
    while True:
        try:
            with zipfile.ZipFile(src) as z:
                with z.open("eeMemory.bin") as f:
                    f.seek(c.contador)
                    campo = int.from_bytes(f.read(4), "little")
                png = z.read("Screenshot.png") if "Screenshot.png" in z.namelist() else None
            break
        except (FileNotFoundError, zipfile.BadZipFile):
            if time.monotonic() > fin:
                raise
            time.sleep(.05)
    despues = c.numero()
    if campo != antes or despues != antes or c.p.status() != 1:
        raise RuntimeError("La VM avanzo al guardar el estado")
    if png:
        with Image.open(io.BytesIO(png)) as im:
            im.resize((im.width // 2, im.height // 2)).save(destino)
    archivo = ARCHIVO / "captura_estados"
    archivo.mkdir(parents=True, exist_ok=True)
    dst = archivo / (str(time.time_ns()) + "_" + src.name)
    shutil.move(str(src), dst)
    return dict(cuadro=antes, despues=despues, contador_en_estado=campo,
        imagen=destino.name if png else None, estado=str(dst),
        metodo="Cuadros/PINE SaveState inmovil; imagen opcional antes del primer VSync")


def restaurar_config(s):
    """Restaurar sin pasar por un comando que pudiera cerrar una VM ajena."""
    if s.nuestro_pid():
        raise RuntimeError("Luna ocupada: configuracion pendiente, no se toca la VM ajena")
    s.TURNO.adquirir(os.getpid(), sys.executable)
    try:
        backup = TMP / "PCSX2_luna.ini.original"
        if backup.exists():
            ARCHIVO.mkdir(parents=True, exist_ok=True)
            ini = Path(s.EMU) / "inis/PCSX2.ini"
            shutil.move(str(ini), ARCHIVO / f"PCSX2_usado_{time.time_ns()}.ini")
            shutil.move(str(backup), ini)
            print("Configuracion de PCSX2 restaurada")
    finally:
        s.TURNO.liberar(os.getpid())


def esperar_plaza(P, s):
    """Espera eventos de fin de procesos; no sondea juegos ni impone otro plazo."""
    from ctypes import wintypes as wt
    P.K32.WaitForMultipleObjects.argtypes = [wt.DWORD, ctypes.POINTER(wt.HANDLE), wt.BOOL, wt.DWORD]
    P.K32.WaitForMultipleObjects.restype = wt.DWORD
    P.K32.FindFirstChangeNotificationW.argtypes = [wt.LPCWSTR, wt.BOOL, wt.DWORD]
    P.K32.FindFirstChangeNotificationW.restype = wt.HANDLE
    P.K32.FindCloseChangeNotification.argtypes = [wt.HANDLE]
    while True:
        if pid := s.nuestro_pid():
            h = P.K32.OpenProcess(0x100000, False, pid)
            if not h:
                continue
            print("En cola por la instancia luna, PID", pid, flush=True)
            try:
                P.K32.WaitForSingleObject(h, 0xffffffff)
            finally:
                P.K32.CloseHandle(h)
            continue
        pids = subprocess.run(["powershell", "-NoProfile", "-Command",
            "@(Get-Process dbz3,pcsx2 -ErrorAction SilentlyContinue) | ForEach-Object {$_.Id}"],
            text=True, capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, check=True).stdout.split()
        if len(pids) < P.TOPE_JUEGOS:
            # Suscribirse ANTES de mkdir evita perder el evento de liberacion.
            aviso = P.K32.FindFirstChangeNotificationW(str(P.BASE), False, 2)
            if aviso in (None, ctypes.c_void_p(-1).value):
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                try:
                    P.LANZANDO.mkdir()
                    return P.LANZANDO.stat().st_ino
                except FileExistsError:
                    P.K32.WaitForSingleObject(aviso, 0xffffffff)
            finally:
                P.K32.FindCloseChangeNotification(aviso)
            continue
        handles = [h for pid in pids if (h := P.K32.OpenProcess(0x100000, False, int(pid)))]
        if not handles:
            raise RuntimeError("No se puede esperar la plaza: procesos sin acceso de sincronizacion")
        print("En cola: esperando que termine uno de", pids, flush=True)
        try:
            arr = (wt.HANDLE * len(handles))(*handles)
            if P.K32.WaitForMultipleObjects(len(handles), arr, False, 0xffffffff) == 0xffffffff:
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            for h in handles:
                P.K32.CloseHandle(h)


def original(salida):
    import pcsx2_cuadros as Q
    import pruebas as P
    from types import SimpleNamespace
    os.environ["DBZ3_PCSX2_CUADROS_TMP"] = str(TMP)
    os.environ["DBZ3_PCSX2_CUADROS_RESPALDO"] = str(ARCHIVO)
    s = Q.conectar("luna")
    semilla = Path(s.EMU) / "sstates/SLES-51233 (37472BE0).06.p2s"
    if not semilla.is_file():
        raise FileNotFoundError(semilla)
    if s.nuestro_pid():
        raise RuntimeError("Instancia luna ocupada: no se usa ni se cierra")
    salida.mkdir(parents=True, exist_ok=False)
    # Reserva atomica del mismo cerrojo; espera cambios, nunca roba otro turno.
    candado = esperar_plaza(P, s)
    parar_latido = threading.Event()
    def latido():
        # sonda espera hasta 180 s a PINE; pruebas archiva candados de >60 s.
        while not parar_latido.wait(10):
            try:
                if P.LANZANDO.stat().st_ino != candado:
                    return
                os.utime(P.LANZANDO, None)
            except FileNotFoundError:
                return
    hilo_latido = threading.Thread(target=latido, daemon=True)
    hilo_latido.start()
    propio = None
    preparado = False
    try:
        try:
            Q.preparar(s)
            preparado = True
            ini = Path(s.EMU) / "inis/PCSX2.ini"
            cfg = configparser.ConfigParser(interpolation=None)
            cfg.optionxform = str
            cfg.read(ini, encoding="utf-8-sig")
            gs = cfg["EmuCore/GS"]
            # Diagnostico nativo acotado: 40 campos con registros/vertices,
            # nunca imagenes de cada draw ni volcado ilimitado de texturas.
            for k,v in dict(DumpGSData="true", dumpGSData="true", SaveInfo="true",
                SaveRT="false", SaveFrame="false", SaveTexture="false", SaveDepth="false",
                SaveAlpha="false", SaveDrawStart="0", SaveDrawCount="2000000", SaveDrawBy="1",
                SaveFrameStart="0", SaveFrameCount="4000", SaveFrameBy="100",
                HWDumpDirectory=str(salida / "gs_dibujos"), SWDumpDirectory=str(salida / "gs_dibujos")).items():
                gs[k] = v
            (salida / "gs_dibujos").mkdir()
            with ini.open("w", encoding="utf-8") as f:
                cfg.write(f)
            p = s.lanzar("b1", str(semilla))
            propio = s.nuestro_pid()
            p.s.close()
        finally:
            parar_latido.set()
            hilo_latido.join()
            try:
                if P.LANZANDO.stat().st_ino == candado:
                    P.LANZANDO.rmdir()
            except FileNotFoundError:
                pass
        P._a_pantalla_secundaria(SimpleNamespace(pid=propio, poll=lambda: None))
        # FUN_00124488: el VBlank 0x361514 avanza TAMBIEN durante la carga.
        # 0x583a10 es el reloj de la escena y permanece a cero al entrar.
        c = Q.Cuadros(s, 0x361514)
        c.pausa()
        # No recargar el mismo estado sobre la VM pausada: PCSX2 2.9.103
        # deja ahi F7 sin consumir (RAM/VBlank inmoviles, sin Screenshot.png).
        # La semilla YA se paso por -statefile. Esta serie empieza tras el
        # arranque; no certifica el primer cuadro absoluto de la cinematica.
        for n in (0, 1, 14, 15):
            if n:
                c.paso(n)
            campo = c.numero()
            foto = captura_estado(c, salida / f"entrada_{campo}.png")
            foto["campo_escena"] = c.p.u32(0x583a10)
            guardar(salida / f"entrada_{campo}.json", foto)
        inicial = c.p.u32(0x583a10)
        # Acercamiento fuera de la serie: una espera unica; despues cada campo
        # de medida avanza con F7 y se comprueba el contador de Cuadros.
        if inicial < 1200:
            c.pulso(0x75)
            time.sleep((1200 - inicial) / 50.0)
            c.pausa()
        c.contador = 0x583a10
        if c.numero() == 0:
            raise RuntimeError("La escena sigue cargando: el reloj 0x583a10 no esta activo; no se declara una serie valida")
        guardar(salida / "inicio.json", dict(campo=c.numero(), juego=c.p.id(), version=c.p.version()))
        # RAM de la camara y contadores; dos estados GS de humo y uno limpio.
        for objetivo in (1740, 1810, 1870, 1935):
            if c.numero() > objetivo:
                raise RuntimeError(f"La carga paso el campo pedido {objetivo}: {c.numero()}")
            c.paso(objetivo - c.numero())
            foto = captura_estado(c, salida / f"campo_{objetivo}.png")
            foto["camara"] = struct.unpack("<12f", c.p.leer(0x555b00, 48))
            guardar(salida / f"campo_{objetivo}.json", foto)
            # Cuadros archiva el estado completo de esta misma captura.
            est = Path(foto["estado"])
            guardar(salida / f"gs_{objetivo}.json", estado(est))
        c.p.s.close()
    finally:
        if propio and s.nuestro_pid() == propio:
            s.main(["--instancia", "luna", "cerrar"])
        elif propio is None and s.TURNO.path.exists() and int(s.TURNO._datos().get("pid",0)) == os.getpid():
            s.TURNO.liberar(os.getpid())
        if preparado:
            restaurar_config(s)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("estado")
    e.add_argument("ruta", type=Path)
    t = sub.add_parser("tablero")
    t.add_argument("ruta", type=Path)
    t.add_argument("iso", type=Path)
    t.add_argument("--salida", type=Path, default=TMP / "tablero")
    o = sub.add_parser("original")
    o.add_argument("--salida", type=Path, default=TMP / "original")
    sub.add_parser("restaurar")
    sub.add_parser("lanzar")
    a = ap.parse_args()
    if a.cmd == "estado":
        print(json.dumps(estado(a.ruta), indent=2))
    elif a.cmd == "tablero":
        tablero(a.ruta, a.iso, a.salida)
    elif a.cmd == "original":
        original(a.salida)
    elif a.cmd == "restaurar":
        import pcsx2_cuadros as Q
        os.environ["DBZ3_PCSX2_CUADROS_TMP"] = str(TMP)
        os.environ["DBZ3_PCSX2_CUADROS_RESPALDO"] = str(ARCHIVO)
        s = Q.conectar("luna")
        restaurar_config(s)
    else:
        TMP.mkdir(parents=True, exist_ok=True)
        with (TMP / "original.log").open("x", encoding="utf-8") as out, (TMP / "original.err").open("x", encoding="utf-8") as err:
            p = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "original"],
                stdout=out, stderr=err, stdin=subprocess.DEVNULL,
                creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP)
        guardar(TMP / "original_proceso.json", dict(pid=p.pid))
        print("Medida en cola, PID", p.pid)


if __name__ == "__main__":
    main()

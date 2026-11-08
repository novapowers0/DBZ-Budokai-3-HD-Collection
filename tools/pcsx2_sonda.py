#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sonda.py - observa juegos de PS2 en las instancias portables de PCSX2 2.9.

Lee/escribe memoria EE por PINE (puerto 28011), inyecta teclas a la ventana del emulador sin darle foco
(PostMessage) y saca capturas (tecla F8 -> pcsx2\\snaps). La ventana se queda detras y el audio en Null.

   python sonda.py lanzar b2 [--estado N|ruta.p2s] [--instancia luna] [--puerto N]
   python sonda.py lanzar b3 [--instancia luna]            B3 USA Greatest Hits CHD
  python sonda.py vigilar 0x3734ac:4 0x699392:1 [--seg 60] [--log x.log]   cambios por sondeo (~1 ms)
  python sonda.py leer 0x3734ac [n]                       volcado hex
  python sonda.py escribir 0x3734ac 01000000              (hex, little endian tal cual)
  python sonda.py teclas "down*3 z start" [--ms 120]      z=X a=Cuadrado s=Triangulo x=Circulo q/w=L1/R1 1/2=L2/R2
  python sonda.py captura [nombre.png]                    -> emu\\capturas\\
  python sonda.py guardar N | cargar N                    slots de savestate (sstates de la copia)
   python sonda.py reservar | liberar      turno atomico; cada instancia/PINE tiene su espacio
    python sonda.py reservar | liberar      turno atomico; PINE solo conecta con la instancia reservada
    python sonda.py estado | cerrar
"""
import ctypes
import os
import socket
import struct
import subprocess
import sys
import time
from ctypes import wintypes

# Copia en el repo (6/11, fase0C). El codigo vive aqui; los ejecutables/perfiles de PCSX2 siguen en RAIZ (datos, no codigo).
CODIGO = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.environ.get("DBZ3_SONDA_DIR", r"E:\DBZ3HD_trabajo\R7\emu")
INSTANCIA = "principal"
EMU = os.path.join(RAIZ, "pcsx2")
EXE = os.path.join(EMU, "pcsx2.exe")
CAPTURAS = os.path.join(RAIZ, "capturas")
ISOS = {
    "b2": r"D:\DBZ3HD\ps2_games\DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso",
    "b1": r"D:\DBZ3HD\ps2_games\DragonBall Z - Budokai (Europe) (En,Fr,De,Es,It).iso",
    "iw": r"E:\Emuladores\Retrobat\roms\ps2\Dragon Ball Z - Infinite World.chd",   # SLES-55347 (= decompilado), solo lectura
    "b3": r"E:\Emuladores\Retrobat\roms\ps2\Dragon Ball Z - Budokai 3 (USA) (Greatest Hits).chd",
}
SERIAL = {"b2": "SLES-51839 (FE09A7CC)", "b3": "SLUS-20998 (C97EF0A4)", "iw": "SLES-55347 (335A5A1F)"}
PUERTO = 28011
INSTANCIAS = {
    "principal": (28011, "pcsx2", "capturas", "instancia1"),
    "luna": (28012, "pcsx2_luna", "capturas_luna", "instancia2"),
    "esc": (28015, "pcsx2_esc", "capturas_esc", "instancia5"),
    "kit": (28017, "pcsx2_kit", "capturas_kit", "instancia7"),
    "pre": (28019, "pcsx2_pre", "capturas_pre", "instancia9"),
}
sys.path.insert(0, CODIGO)
from pcsx2_turnos import Turno  # noqa: E402
TURNO = Turno(INSTANCIAS[INSTANCIA][3], PUERTO, EXE)


def seleccionar_instancia(args):
    """Consume --instancia/--puerto y cambia el endpoint sin alterar los defaults antiguos."""
    global INSTANCIA, EMU, EXE, CAPTURAS, PUERTO, TURNO
    out = []
    instancia = "principal"
    puerto = None
    i = 0
    while i < len(args):
        if args[i] in ("--instancia", "--puerto"):
            flag = args[i]
            if i + 1 >= len(args):
                raise SystemExit("falta el valor de " + flag)
            val = args[i + 1]
            if flag == "--instancia":
                instancia = val.lower()
            else:
                puerto = int(val)
            i += 2
        else:
            out.append(args[i])
            i += 1
    if instancia not in INSTANCIAS:
        raise SystemExit("instancia desconocida: %s (usa principal, luna o esc)" % instancia)
    conf_puerto, carpeta_emu, carpeta_capturas, lock_name = INSTANCIAS[instancia]
    if puerto is not None and puerto != conf_puerto:
        raise SystemExit("puerto %d no pertenece a %s (PINE %d); cada línea necesita su instancia reservada" %
                         (puerto, instancia, conf_puerto))
    INSTANCIA = instancia
    EMU = os.path.join(RAIZ, carpeta_emu)
    EXE = os.path.join(EMU, "pcsx2.exe")
    CAPTURAS = os.path.join(RAIZ, carpeta_capturas)
    PUERTO = conf_puerto
    TURNO = Turno(lock_name, PUERTO, EXE)
    return out


# Los clientes que importan `sonda.Pine()` eligen su PINE por entorno; si no se
# indica, conservan el endpoint principal, pero aun así exigen su cerrojo.
if os.environ.get("DBZ3_PCSX2_INSTANCIA"):
    seleccionar_instancia(["--instancia", os.environ["DBZ3_PCSX2_INSTANCIA"]])

# ---------------------------------------------------------------- PINE
R8, R16, R32, R64, W8, W16, W32, W64, VERSION, SAVE, LOAD, TITLE, ID, UUID, GVER, STATUS = range(16)


class PartidaPerdida(IOError):
    """PINE no vuelve tras los reintentos y el emulador ya no esta: la partida hay que retomarla desde un punto de control."""


class Pine:
    REINTENTOS = 4          # mejora 5: un corte de socket se reconecta (y se repite la orden) antes de dar la partida por perdida

    def __init__(self, puerto=None, espera=0.0):
        puerto = PUERTO if puerto is None else int(puerto)
        if puerto != PUERTO:
            raise RuntimeError("PINE bloqueado: el puerto no coincide con la instancia reservada")
        TURNO.validar()
        self.puerto, self.timeout = puerto, 5
        self._conectar(espera)

    def _conectar(self, espera=0.0):
        fin = time.time() + espera
        while True:
            try:
                self.s = socket.create_connection(("127.0.0.1", self.puerto), timeout=5)
                break
            except OSError:
                if time.time() > fin:
                    raise
                time.sleep(0.5)
        self.s.settimeout(self.timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def settimeout(self, t):
        self.timeout = t
        self.s.settimeout(t)

    def _cmd(self, cuerpo):
        for intento in range(self.REINTENTOS + 1):
            try:
                return self._cmd1(cuerpo)
            except (OSError, ConnectionError) as e:       # corte, timeout o PINE cerrado (IOError == OSError)
                if isinstance(e, IOError) and str(e) == "PINE: fallo":
                    raise                                  # fallo de la orden, no de la conexion
                if intento == self.REINTENTOS or not nuestro_pid():
                    from pcsx2_turnos import registrar
                    registrar("partida_perdida", instancia=INSTANCIA, error=repr(e), intento=intento)
                    raise PartidaPerdida("PINE perdido (%r) tras %d reintentos" % (e, intento)) from e
                from pcsx2_turnos import registrar
                registrar("reconexion", instancia=INSTANCIA, intento=intento + 1, error=repr(e))
                try:
                    self.s.close()
                except OSError:
                    pass
                time.sleep(min(8, 1 + 2 * intento))
                try:
                    self._conectar(espera=30)
                except OSError:
                    pass                                   # el siguiente intento lo dira

    def _cmd1(self, cuerpo):
        self.s.sendall(struct.pack("<I", len(cuerpo) + 4) + cuerpo)
        n = struct.unpack("<I", self._exact(4))[0]
        r = self._exact(n - 4)
        if r[0] != 0:
            raise IOError("PINE: fallo")
        return r[1:]

    def _exact(self, n):
        b = b""
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise IOError("PINE cerrado")
            b += c
        return b

    def lote(self, pares):
        """[(addr, tam)] -> [valor]; una sola ida y vuelta (tam 1/2/4/8)."""
        op = {1: R8, 2: R16, 4: R32, 8: R64}
        cuerpo = b"".join(struct.pack("<BI", op[t], a) for a, t in pares)
        r = self._cmd(cuerpo)
        out, o = [], 0
        for _, t in pares:
            out.append(int.from_bytes(r[o:o + t], "little"))
            o += t
        return out

    def lote_estado(self, pares):
        """Como lote() pero con el estado de la VM (0 corre, 1 pausa, 2 apagada) en la MISMA llamada PINE: -> (estado, [valores])."""
        op = {1: R8, 2: R16, 4: R32, 8: R64}
        r = self._cmd(bytes([STATUS]) + b"".join(struct.pack("<BI", op[t], a) for a, t in pares))
        out, o = [], 4
        for _, t in pares:
            out.append(int.from_bytes(r[o:o + t], "little"))
            o += t
        return struct.unpack("<I", r[:4])[0], out

    def leer(self, addr, n):
        """n bytes (lee en palabras de 8; lotes de 64)."""
        b = b""
        for base in range(addr, addr + n, 512):
            k = min(512, addr + n - base)
            vals = self.lote([(base + i, 8) for i in range(0, k, 8)])
            b += b"".join(v.to_bytes(8, "little") for v in vals)
        return b[:n]

    def escribir(self, addr, datos):
        cuerpo = b"".join(struct.pack("<BIB", W8, addr + i, x) for i, x in enumerate(datos))
        self._cmd(cuerpo)

    def u32(self, a):
        return self.lote([(a, 4)])[0]

    def version(self):
        return self._cmd(bytes([VERSION]))[4:].split(b"\0")[0].decode()

    def titulo(self):
        return self._cmd(bytes([TITLE]))[4:].split(b"\0")[0].decode()

    def id(self):
        return self._cmd(bytes([ID]))[4:].split(b"\0")[0].decode()

    def status(self):   # 0 en marcha, 1 pausa, 2 apagado
        return struct.unpack("<I", self._cmd(bytes([STATUS])))[0]

    def guardar(self, slot):
        self._cmd(bytes([SAVE, slot]))

    def cargar(self, slot):
        self._cmd(bytes([LOAD, slot]))


# ---------------------------------------------------------------- puntos de control (mejora 5)
SLOT_CONTROL = 80      # savestates de control (pares 80/81, 82/83...): el pool compartido ya tiene otros del 90-94 y 110+; Sesion elige un par libre


def ruta_estado(slot, serial="*", emu=None):
    """Ruta del savestate `slot` de ESTE juego (el nombre lleva serie y CRC; el pool guarda los de varios juegos)."""
    import glob
    c = sorted(glob.glob(os.path.join(emu or EMU, "sstates", "%s (*).%02d.p2s" % (serial, slot))))
    return c[0] if c else None


def punto_control(p, slot=SLOT_CONTROL, espera=40):
    """Guarda un savestate de control por PINE y espera a que el archivo este entero. Devuelve su ruta."""
    import zipfile
    serial = p.id()
    t0 = time.time()
    p.guardar(slot)
    while time.time() - t0 < espera:
        time.sleep(0.3)
        r = ruta_estado(slot, serial)
        if r and os.path.getmtime(r) >= t0 - 1:
            try:
                with zipfile.ZipFile(r) as z:
                    z.testzip()
                return r
            except (OSError, zipfile.BadZipFile):
                pass
    raise RuntimeError("el punto de control no se escribio")


def contador_en_estado(ruta, direccion):
    """Valor (u32) de la RAM del EE guardada dentro del savestate, sin lanzar el emulador."""
    import zipfile
    with zipfile.ZipFile(ruta) as z, z.open("eeMemory.bin") as ram:
        ram.seek(direccion)
        return int.from_bytes(ram.read(4), "little")


def retomar(lanzar_fn, ruta_control, espera=240):
    """Tras PartidaPerdida: mata solo el pid de ESTA instancia si queda vivo, relanza con el savestate de control y
    devuelve un Pine nuevo. `lanzar_fn(ruta)` arranca el emulador (pool o sonda) y devuelve su Pine."""
    from pcsx2_turnos import registrar
    pid = nuestro_pid()
    if pid:
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
        time.sleep(3)
    registrar("retomada", instancia=INSTANCIA, desde=os.path.basename(ruta_control))
    return lanzar_fn(ruta_control)


# ---------------------------------------------------------------- ventana / teclas
u32dll = ctypes.WinDLL("user32", use_last_error=True)
WM_KEYDOWN, WM_KEYUP = 0x100, 0x101
VK = {"up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27, "z": 0x5A, "x": 0x58, "a": 0x41, "s": 0x53,
      "q": 0x51, "w": 0x57, "1": 0x31, "2": 0x32, "start": 0x56, "select": 0x42, "f8": 0x77}
ALIAS = {"cruz": "z", "circulo": "x", "cuadrado": "a", "triangulo": "s", "l1": "q", "r1": "w", "l2": "1",
         "r2": "2", "enter": "start"}


def _pids():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq pcsx2.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, text=True).stdout
    pids = []
    for ln in out.splitlines():
        p = ln.split('","')
        if len(p) > 1:
            pids.append(int(p[1]))
    return pids


def _exe_de(pid):
    h = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
    buf = ctypes.create_unicode_buffer(600)
    n = wintypes.DWORD(600)
    ctypes.windll.kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n))
    ctypes.windll.kernel32.CloseHandle(h)
    return buf.value


def nuestro_pid():
    """Solo la copia portable (nunca el PCSX2 del usuario)."""
    for p in _pids():
        if os.path.normcase(_exe_de(p)) == os.path.normcase(EXE):
            return p
    return None


def ventanas(pid):
    hs = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _):
        p = wintypes.DWORD()
        u32dll.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u32dll.IsWindowVisible(h):
            t = ctypes.create_unicode_buffer(256)
            u32dll.GetWindowTextW(h, t, 256)
            hs.append((h, t.value))
        return True

    u32dll.EnumWindows(cb, 0)
    return hs


def ventana_juego(pid):
    """La ventana de render (la del titulo del juego, no la de la lista)."""
    vs = ventanas(pid)
    for h, t in vs:
        if t and "PCSX2" not in t:
            return h
    return vs[0][0] if vs else None


def al_fondo(pid):
    for h, _ in ventanas(pid):
        u32dll.SetWindowPos(h, 1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)   # HWND_BOTTOM, NOSIZE|NOMOVE|NOACTIVATE


def _hijos(h):
    hs = [h]

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(c, _):
        hs.append(c)
        return True

    u32dll.EnumChildWindows(h, cb, 0)
    return hs


def _post(h, vk, abajo):
    sc = u32dll.MapVirtualKeyW(vk, 0)
    ext = 1 << 24 if vk in (0x25, 0x26, 0x27, 0x28) else 0
    lp = 1 | (sc << 16) | ext | ((0 if abajo else 3 << 30))
    for c in _hijos(h):
        u32dll.PostMessageW(c, WM_KEYDOWN if abajo else WM_KEYUP, vk, lp)


def teclas(secuencia, ms=120, pausa=150):
    """'down*3 z start' ; 'z+right' = a la vez ; 'espera500' = 500 ms."""
    TURNO.validar()
    h = ventana_juego(nuestro_pid())
    for tok in secuencia.split():
        if tok.startswith("espera"):
            time.sleep(int(tok[6:]) / 1000)
            continue
        rep = 1
        if "*" in tok:
            tok, r = tok.split("*")
            rep = int(r)
        ks = [VK[ALIAS.get(k, k)] for k in tok.lower().split("+")]
        for _ in range(rep):
            for k in ks:
                _post(h, k, True)
            time.sleep(ms / 1000)
            for k in ks:
                _post(h, k, False)
            time.sleep(pausa / 1000)


def captura(nombre=None):
    """F8 del emulador (imagen nativa del GS, aunque la ventana este tapada) -> capturas\\nombre."""
    TURNO.validar()
    snaps = os.path.join(EMU, "snaps")
    antes = set()
    for r, _, fs in os.walk(snaps):
        antes |= {os.path.join(r, f) for f in fs}
    teclas("f8", ms=60, pausa=0)
    fin = time.time() + 40
    while time.time() < fin:
        nuevos = []
        for r, _, fs in os.walk(snaps):
            nuevos += [os.path.join(r, f) for f in fs if os.path.join(r, f) not in antes]
        if nuevos:
            os.makedirs(CAPTURAS, exist_ok=True)
            dst = os.path.join(CAPTURAS, nombre or os.path.basename(nuevos[0]))
            for _ in range(50):         # el emulador aun la esta escribiendo
                time.sleep(0.2)
                try:
                    os.replace(nuevos[0], dst)
                    return dst
                except PermissionError:
                    pass
        time.sleep(0.1)
    raise RuntimeError("sin captura (F8)")


# ---------------------------------------------------------------- ganchos de codigo (traza de llamadas)
def _i(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


T0, T1, T2, T3, T4, A0, SP = 8, 9, 10, 11, 12, 4, 29
BUF_GANCHO = 0xA0000     # hueco libre entre kernel y ELF (0x96000-0x100000 a cero en tablero y combate de B2)
PASO_GANCHO = 0x9000     # cada gancho k usa BUF_GANCHO + k*PASO_GANCHO (anillo 32 KB + stub); caben 6
N_GANCHO = 256           # entradas de 128 bytes: 64 de *a0, +64 ra del llamador, +68 n.o de secuencia


def gancho(p, sitio, original, pila_ra, buf=BUF_GANCHO, n=N_GANCHO):
    """Sustituye el 'jal original' de `sitio` por un stub que copia 64 bytes de *a0 y el ra guardado en
    sp+pila_ra (el llamador de la funcion que contiene `sitio`) a un anillo en `buf`, y salta a `original`."""
    assert p.u32(sitio) == 0x0C000000 | (original >> 2), "en %X no hay jal %X" % (sitio, original)
    stub = buf + 0x80 + n * 128
    c = [_i(0x0F, 0, T0, buf >> 16), _i(0x23, T0, T1, 0), _i(0x0C, T1, T2, n - 1),
         (T2 << 16) | (T3 << 11) | (7 << 6), (T3 << 21) | (T0 << 16) | (T3 << 11) | 0x21, _i(0x09, T3, T3, 0x80)]
    for k in range(0, 64, 8):
        c += [_i(0x37, A0, T4, k), _i(0x3F, T3, T4, k)]
    c += [_i(0x23, SP, T4, pila_ra), _i(0x2B, T3, T4, 64), _i(0x2B, T3, T1, 68), _i(0x09, T1, T1, 1),
          0x08000000 | (original >> 2), _i(0x2B, T0, T1, 0)]
    p.escribir(buf, b"\0" * 4)
    p.escribir(stub, b"".join(struct.pack("<I", x) for x in c))
    p.escribir(sitio, struct.pack("<I", 0x0C000000 | (stub >> 2)))


def gancho_puntero(p, ptr, buf, n=N_GANCHO, reg=5):
    """Para funciones llamadas por tabla (jalr): cambia el puntero de la tabla en `ptr` por un stub que copia
    64 bytes de *reg (a1 por defecto: el contexto del guion), guarda $ra y salta a la funcion original.
    Devuelve la direccion original (para quitar_gancho_puntero)."""
    original = p.u32(ptr)
    stub = buf + 0x80 + n * 128
    c = [_i(0x0F, 0, T0, buf >> 16), _i(0x23, T0, T1, 0), _i(0x0C, T1, T2, n - 1),
         (T2 << 16) | (T3 << 11) | (7 << 6), (T3 << 21) | (T0 << 16) | (T3 << 11) | 0x21, _i(0x09, T3, T3, 0x80)]
    for k in range(0, 64, 4):         # lw/sw: el contexto puede no estar alineado a 8
        c += [_i(0x23, reg, T4, k), _i(0x2B, T3, T4, k)]
    c += [_i(0x2B, T3, 31, 64), _i(0x2B, T3, T1, 68), _i(0x2B, T3, 4, 72), _i(0x2B, T3, reg, 76),
          _i(0x09, T1, T1, 1), 0x08000000 | (original >> 2), _i(0x2B, T0, T1, 0)]
    p.escribir(buf, b"\0" * 4)
    p.escribir(stub, b"".join(struct.pack("<I", x) for x in c))
    p.escribir(ptr, struct.pack("<I", stub))
    return original


def gancho_regs(p, sitio, original, buf, n=N_GANCHO):
    """Como gancho() pero guarda los registros a0-a3 (argumentos por valor) y el sitio; entrada = 16 bytes
    a0,a1,a2,a3 (32 bits bajos) en los primeros 16 de los 64."""
    assert p.u32(sitio) == 0x0C000000 | (original >> 2), "en %X no hay jal %X" % (sitio, original)
    stub = buf + 0x80 + n * 128
    c = [_i(0x0F, 0, T0, buf >> 16), _i(0x23, T0, T1, 0), _i(0x0C, T1, T2, n - 1),
         (T2 << 16) | (T3 << 11) | (7 << 6), (T3 << 21) | (T0 << 16) | (T3 << 11) | 0x21, _i(0x09, T3, T3, 0x80),
         _i(0x2B, T3, 4, 0), _i(0x2B, T3, 5, 4), _i(0x2B, T3, 6, 8), _i(0x2B, T3, 7, 12),
         _i(0x2B, T3, 31, 64), _i(0x2B, T3, T1, 68), _i(0x09, T1, T1, 1),
         0x08000000 | (original >> 2), _i(0x2B, T0, T1, 0)]
    p.escribir(buf, b"\0" * 4)
    p.escribir(stub, b"".join(struct.pack("<I", x) for x in c))
    p.escribir(sitio, struct.pack("<I", 0x0C000000 | (stub >> 2)))


def traza_tabla(p, tabla, indices, buf=0xE0000, n=1024, contador=None):
    """Engancha varias entradas de una tabla de punteros a funcion (p. ej. despacho de ordenes de guion)
    con un anillo comun: entrada de 32 bytes = a0, a1, a2, a3, indice, ra, n.o secuencia.
    Con `contador` (direccion del contador de cuadros del juego) la 8.a palabra guarda su valor en la llamada:
    cada orden queda fechada por fotograma del juego, no por reloj (mejora 7).
    Devuelve {indice: puntero original} para quitar_traza_tabla."""
    orig = {}
    p.escribir(buf, b"\0" * 4)
    base_stub = buf + 0x80 + n * 32
    for j, k in enumerate(indices):
        o = p.u32(tabla + 4 * k)
        if not o:
            continue
        stub = base_stub + j * 0x60
        c = [_i(0x0F, 0, T0, buf >> 16), _i(0x23, T0, T1, buf & 0xFFFF), _i(0x0C, T1, T2, n - 1),
             (T2 << 16) | (T3 << 11) | (5 << 6), (T3 << 21) | (T0 << 16) | (T3 << 11) | 0x21,
             _i(0x09, T3, T3, (buf & 0xFFFF) + 0x80),
             _i(0x2B, T3, 4, 0), _i(0x2B, T3, 5, 4), _i(0x2B, T3, 6, 8), _i(0x2B, T3, 7, 12),
             _i(0x09, 0, T4, k), _i(0x2B, T3, T4, 16), _i(0x2B, T3, 31, 20), _i(0x2B, T3, T1, 24),
             _i(0x09, T1, T1, 1), 0x08000000 | (o >> 2), _i(0x2B, T0, T1, buf & 0xFFFF)]
        if contador is not None:
            assert (contador & 0xFFFF) < 0x8000, 'el contador debe tener la parte baja < 0x8000 (lw con signo)'
            # lui t4, hi ; lw t4, lo(t4) ; sw t4, 28(t3)  -- antes del salto a la funcion original (indice 15)
            c[15:15] = [_i(0x0F, 0, T4, contador >> 16), _i(0x23, T4, T4, contador & 0xFFFF), _i(0x2B, T3, T4, 28)]
        p.escribir(stub, b"".join(struct.pack("<I", x) for x in c))
        orig[k] = o
    for k, o in orig.items():
        p.escribir(tabla + 4 * k, struct.pack("<I", base_stub + indices.index(k) * 0x60))
    return orig


def quitar_traza_tabla(p, tabla, orig):
    for k, o in orig.items():
        p.escribir(tabla + 4 * k, struct.pack("<I", o))


def leer_traza(p, desde=0, buf=0xE0000, n=1024):
    """-> (total, [(seq, indice, a0, a1, a2, a3, ra, *(a0+0x34))])"""
    tot = p.u32(buf)
    out = []
    for s in range(max(desde, tot - n), tot):
        a0, a1, a2, a3, k, ra, sq, arg = struct.unpack("<8I", p.leer(buf + 0x80 + (s % n) * 32, 32))
        out.append((sq, k, a0, a1, a2, a3, ra, arg))
    return tot, out


def quitar_gancho(p, sitio, original):
    p.escribir(sitio, struct.pack("<I", 0x0C000000 | (original >> 2)))


def leer_gancho(p, desde=0, buf=BUF_GANCHO, n=N_GANCHO):
    """-> (total, [(secuencia, ra, 64 bytes)]) de las entradas nuevas desde `desde`."""
    tot = p.u32(buf)
    ini = max(desde, tot - n)
    out = []
    for s in range(ini, tot):
        e = p.leer(buf + 0x80 + (s % n) * 128, 80)
        # 64 bytes copiados + (a0, reg) en gancho_puntero
        out.append((struct.unpack_from("<I", e, 68)[0], struct.unpack_from("<I", e, 64)[0], e[:64] + e[72:80]))
    return tot, out


# ---------------------------------------------------------------- lanzar / vigilar
def lanzar(juego, estado=None):
    if nuestro_pid():
        raise SystemExit("ya hay PCSX2 en esta instancia; usa 'sonda.py reservar' antes de conectar")
    TURNO.adquirir(os.getpid(), sys.executable)
    fg = u32dll.GetForegroundWindow()
    args = [EXE, "-nogui", "-fastboot"]
    if estado is not None:
        ruta = estado if os.path.exists(str(estado)) else os.path.join(
            EMU, "sstates", "%s.%02d.p2s" % (SERIAL[juego], int(estado)))
        args += ["-statefile", ruta]
    args += ["--", ISOS[juego]]
    si = subprocess.STARTUPINFO()
    si.dwFlags = 1
    si.wShowWindow = 4   # SW_SHOWNOACTIVATE
    proc = None
    try:
        proc = subprocess.Popen(args, cwd=EMU, startupinfo=si)
        from pcsx2_turnos import afinar
        afinar(proc.pid)                 # mejora 1: nucleos rapidos + prioridad alta (solo este pid)
        TURNO.adoptar(proc.pid)
    except Exception:
        if proc is None:
            TURNO.liberar(os.getpid(), fuerza=True)
        raise
    p = Pine(espera=180)   # 25/10: PCSX2 tarda ~80 s en abrir la VM (mandos SDL); con 60 PINE nunca llegaba
    for _ in range(40):          # devolver el foco al usuario y dejar el emulador detras
        time.sleep(0.25)
        pid = nuestro_pid()
        if pid and ventanas(pid):
            al_fondo(pid)
            if fg:
                u32dll.SetForegroundWindow(fg)
    return p


def vigilar(specs, seg=60, log=None, extra=()):
    """specs = [(addr, tam)]; registra cada cambio con tiempo y las 'extra' (contexto) en ese instante."""
    p = Pine()
    todos = list(specs) + list(extra)
    prev = None
    t0 = time.time()
    f = open(log, "a", encoding="utf-8") if log else None
    n = 0
    while time.time() - t0 < seg:
        v = p.lote(todos)
        n += 1
        if v[:len(specs)] != (prev or [None])[:len(specs)]:
            ln = "%8.3f s  " % (time.time() - t0) + "  ".join(
                "%X=%X" % (a, x) for (a, _), x in zip(todos, v))
            print(ln, flush=True)
            if f:
                f.write(ln + "\n")
                f.flush()
        prev = v
    print("sondeos: %d (%.0f/s)" % (n, n / seg))


def _spec(s):
    a, _, t = s.partition(":")
    return int(a, 16), int(t or 4)


def main(a):
    a = seleccionar_instancia(a)
    if not a:
        print(__doc__)
        return
    c = a[0]
    if c == "lanzar":
        est = a[a.index("--estado") + 1] if "--estado" in a else None
        p = lanzar(a[1], est)
        print(p.version(), "|", p.id(), "|", p.titulo())
    elif c == "reservar":
        pid = nuestro_pid()
        if not pid:
            raise SystemExit("no hay PCSX2 en esta instancia; usa 'lanzar' o abre la copia antes")
        TURNO.adquirir(pid, EXE)
        print("turno reservado: %s PINE %d PID %d" % (INSTANCIA, PUERTO, pid))
    elif c == "liberar":
        try:
            datos = TURNO._datos()
            pid = int(datos.get("pid", "0"))
        except (OSError, ValueError):
            raise SystemExit("no hay turno reservado para " + INSTANCIA)
        TURNO.liberar(pid)
        print("turno liberado: %s PINE %d" % (INSTANCIA, PUERTO))
    elif c == "vigilar":
        seg = float(a[a.index("--seg") + 1]) if "--seg" in a else 60
        log = a[a.index("--log") + 1] if "--log" in a else None
        ctx = [_spec(x) for x in a[a.index("--ctx") + 1].split(",")] if "--ctx" in a else []
        specs = [_spec(x) for x in a[1:] if x.startswith("0x")]
        vigilar(specs, seg, log, ctx)
    elif c == "leer":
        b = Pine().leer(int(a[1], 16), int(a[2]) if len(a) > 2 else 64)
        for i in range(0, len(b), 16):
            print("%08X  %s" % (int(a[1], 16) + i, b[i:i + 16].hex(" ")))
    elif c == "escribir":
        Pine().escribir(int(a[1], 16), bytes.fromhex(a[2]))
    elif c == "teclas":
        teclas(a[1], int(a[a.index("--ms") + 1]) if "--ms" in a else 120)
    elif c == "captura":
        print(captura(a[1] if len(a) > 1 else None))
    elif c == "guardar":
        Pine().guardar(int(a[1]))
    elif c == "cargar":
        Pine().cargar(int(a[1]))
    elif c == "estado":
        p = Pine()
        print(p.version(), "|", p.id(), "| status", p.status())
    elif c == "cerrar":
        TURNO.validar()
        pid = nuestro_pid()
        if pid:
            subprocess.run(["taskkill", "/PID", str(pid)], capture_output=True)
            time.sleep(3)
            if nuestro_pid():
                subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
            if not nuestro_pid():
                TURNO.liberar(pid)


if __name__ == "__main__":
    main(sys.argv[1:])

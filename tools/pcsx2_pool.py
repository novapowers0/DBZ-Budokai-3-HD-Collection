"""pcsx2_pool.py (fase0C, 6/11): granja de hasta 3 PCSX2 en paralelo para capturar el original.

Cada instancia N (1..3) tiene su sitio propio en D:\\DBZ3HD\\_pcsx2_pool\\N: exe y DLL copiados, resources/QtPlugins por junction,
PCSX2.ini propio (PINE en el puerto 28050+N, sin audio, sin guardar al cerrar), tarjetas de memoria COPIADAS (IW guarda solo:
nunca se comparten), savestates copiados y un cerrojo propio pool_N (mismo mecanismo que tools/pcsx2_turnos.py: carpeta atomica con
quien.txt = pid+puerto+exe, un cerrojo de un proceso muerto se aparta, no se borra).
La plantilla (E:\\DBZ3HD_trabajo\\R7\\emu\\pcsx2_esc) solo se lee. La BIOS no se copia: el ini apunta a la de la plantilla.
Nunca borra nada. Para el codigo de sondeo: `registrar_en_sonda(pcsx2_sonda, n)` (como hace iwx_original_cuadros.preparar).

uso (todo admite --dry-run: solo imprime lo que haria):
  python tools/pcsx2_pool.py estado
  python tools/pcsx2_pool.py preparar N [--estado "SLES-55347 (335A5A1F).04.p2s" ...] [--dry-run]
  python tools/pcsx2_pool.py adquirir [--agente NOMBRE] [--dry-run]    # elige la primera libre, la reserva y la imprime
  python tools/pcsx2_pool.py liberar N [--dry-run]
  python tools/pcsx2_pool.py comando N --iso RUTA [--estado ARCHIVO.p2s]  # linea de arranque (no lanza nada)
  python tools/pcsx2_pool.py lanzar N --iso RUTA [--estado ARCHIVO.p2s] --ejecutar   # unico comando que arranca PCSX2
  python tools/pcsx2_pool.py --autoprueba
"""
import argparse
import configparser
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

RAIZ_POOL = Path(os.environ.get('DBZ3_POOL_DIR', r'D:\DBZ3HD\_pcsx2_pool'))
PLANTILLA = Path(os.environ.get('DBZ3_POOL_BASE', r'E:\DBZ3HD_trabajo\R7\emu\pcsx2_esc'))
N_MAX = 3
PUERTO_BASE = 28050                      # instancia N -> PINE 28050+N (28051-28053)
SUBCARPETAS = ('inis', 'snaps', 'sstates', 'logs', 'cache', 'gamesettings', 'inputprofiles', 'memcards')
JUNCTIONS = ('QtPlugins', 'resources')
EXTENSIONES_TARJETA = ('.ps2', '.mcd', '.mc2')


def perfil(n):
    return RAIZ_POOL / str(n)


def puerto(n):
    return PUERTO_BASE + n


def _comprobar(n):
    if not 1 <= n <= N_MAX:
        raise SystemExit('instancia %s fuera de 1..%d' % (n, N_MAX))


def valores_ini(n):
    """Lo que se fuerza en el PCSX2.ini de la instancia (lo demas se hereda de la plantilla)."""
    return {'EmuCore': dict(EnablePINE='true', PINESlot=str(puerto(n)), SaveStateOnShutdown='false'),
            'SPU2/Output': dict(Backend='Null', OutputMuted='true', StandardVolume='0', FastForwardVolume='0'),
            'Hotkeys': dict(TogglePause='Keyboard/F6', FrameAdvance='Keyboard/F7', Screenshot='Keyboard/F8'),
            'UI': dict(PauseOnFocusLoss='false', StartFullscreen='false', ConfirmShutdown='false'),
            'Folders': dict(Bios=str(PLANTILLA / 'bios'), MemoryCards='memcards', Savestates='sstates',
                            Snapshots='snaps', Logs='logs', Cache='cache')}


def plan_preparar(n, estados=()):
    """Lista de (operacion, origen, destino) para dejar la instancia lista. No toca el disco."""
    p = perfil(n)
    ops = [('mkdir', None, p)]
    ops += [('mkdir', None, p / s) for s in SUBCARPETAS]
    for f in sorted(PLANTILLA.iterdir()) if PLANTILLA.is_dir() else []:
        if f.is_file():
            ops.append(('copiar', f, p / f.name))
    ops += [('junction', PLANTILLA / j, p / j) for j in JUNCTIONS]
    mc = PLANTILLA / 'memcards'
    for f in sorted(mc.iterdir()) if mc.is_dir() else []:
        if f.suffix.lower() in EXTENSIONES_TARJETA:
            ops.append(('copiar', f, p / 'memcards' / f.name))
    for e in estados:
        ops.append(('copiar', PLANTILLA / 'sstates' / e, p / 'sstates' / e))
    ops.append(('ini', PLANTILLA / 'inis' / 'PCSX2.ini', p / 'inis' / 'PCSX2.ini'))
    return ops


def crc_pnach(pnach):
    """CRC del juego (8 hex) de la linea gametitle=... (XXXXXXXX) del pnach: da el nombre <CRC>.pnach que busca PCSX2."""
    m = re.search(r'^gametitle=.*\(([0-9A-Fa-f]{8})\)', Path(pnach).read_text(encoding='utf-8'), re.M)
    if not m:
        raise SystemExit('el pnach %s no trae "gametitle=... (CRC)"' % pnach)
    return m.group(1).upper()


def instalar_truco(n, pnach):
    """Copia el pnach a <pool N>/cheats/<CRC>.pnach (solo en la instancia del pool; la plantilla no se toca)."""
    d = perfil(n) / 'cheats'
    d.mkdir(parents=True, exist_ok=True)
    destino = d / ('%s.pnach' % crc_pnach(pnach))
    shutil.copy2(pnach, destino)
    return destino


def escribir_ini(origen, destino, n, pausado=False, truco=False, velocidad=None):
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(origen, encoding='utf-8-sig')
    for sec, vals in valores_ini(n).items():
        if not cfg.has_section(sec):
            cfg.add_section(sec)
        cfg[sec].update(vals)
    if not cfg.has_section('MemoryCards'):
        cfg.add_section('MemoryCards')
    if pausado:                                      # mejora 7: el juego arranca parado en el cuadro exacto del savestate
        cfg['UI']['StartPaused'] = 'true'
    cfg['EmuCore']['EnableCheats'] = 'true' if truco else 'false'
    if velocidad and velocidad != 1:                 # limite de fotogramas x2/x3 (solo para el modo libre; el exacto va por pasos)
        if not cfg.has_section('Framerate'):
            cfg.add_section('Framerate')
        cfg['Framerate']['NominalScalar'] = str(float(velocidad))
    elif cfg.has_section('Framerate'):
        cfg['Framerate']['NominalScalar'] = '1'
    cfg['MemoryCards']['Slot2_Enable'] = 'false'     # el Slot1 y su fichero son los de la plantilla, ya copiados en memcards/
    with open(destino, 'w', encoding='utf-8') as f:
        cfg.write(f)


def preparar(n, estados=(), dry=False, pausado=False, truco=None, velocidad=None):
    _comprobar(n)
    if not PLANTILLA.is_dir():
        raise SystemExit('falta la plantilla de PCSX2: %s' % PLANTILLA)
    hecho = []
    for op, a, b in plan_preparar(n, estados):
        if op == 'mkdir':
            existe = b.is_dir()
            if not dry:
                b.mkdir(parents=True, exist_ok=True)
        elif op == 'copiar':
            existe = b.exists()      # nunca se sobrescribe lo que ya hay (savestates, tarjetas con progreso)
            if not existe and not dry:
                shutil.copy2(a, b)
        elif op == 'junction':
            existe = b.exists()
            if not existe and not dry:
                subprocess.run(['cmd', '/c', 'mklink', '/J', str(b), str(a)], check=True, capture_output=True)
        else:       # ini: se regenera siempre (es solo configuracion); antes se guarda copia numerada
            existe = False
            if not dry:
                if b.exists():
                    k = 1
                    while b.with_suffix('.ini.antes%d' % k).exists():
                        k += 1
                    shutil.copy2(b, b.with_suffix('.ini.antes%d' % k))
                escribir_ini(a, b, n, pausado, bool(truco), velocidad)
                if truco:
                    hecho.append('truco    %s' % instalar_truco(n, truco))
        hecho.append('%-8s %s%s' % (op + ('*' if dry else ''), b, '  (ya existe)' if existe else ''))
    return hecho


def _superbloque():
    """Superbloque de una tarjeta PS2 de 8 MB ya formateada (formato Sony 1.2.0.0), medido: el juego la acepta."""
    import struct
    b = bytearray([255]) * 8448
    b[0:28] = b'Sony PS2 Memory Card Format '
    b[28:40] = b'1.2.0.0' + b'\0' * 5
    struct.pack_into('<HHHH', b, 0x28, 512, 2, 16, 0xFF00)
    struct.pack_into('<IIIIII', b, 0x30, 8192, 41, 8135, 0, 1023, 1022)
    b[0x48:0x50] = b'\0' * 8
    struct.pack_into('<32I', b, 0x50, 8, *([0xFFFFFFFF] * 31))
    b[0x150], b[0x151] = 2, 0x2B
    return bytes(b)


def tarjeta_carpeta(n, nombre, *carpetas_partida):
    """Tarjeta de memoria CARPETA propia de la instancia n (nunca las del usuario): copia cada carpeta de partida
    dentro de memcards/<nombre> y la deja como Slot1 en el ini. Idempotente: no pisa una partida ya copiada."""
    _comprobar(n)
    base = perfil(n) / 'memcards' / nombre
    base.mkdir(parents=True, exist_ok=True)
    sb = base / '_pcsx2_superblock'
    if not sb.exists() or sb.stat().st_size < 8448:      # vacio = el juego dice «tarjeta sin formatear»
        sb.write_bytes(_superbloque())
    for c in carpetas_partida:
        c = Path(c)
        if not (base / c.name).exists():
            shutil.copytree(c, base / c.name)
    ini = perfil(n) / 'inis' / 'PCSX2.ini'
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(ini, encoding='utf-8-sig')
    cfg['MemoryCards']['Slot1_Filename'] = nombre
    cfg['MemoryCards']['Slot1_Enable'] = 'true'
    with open(ini, 'w', encoding='utf-8') as f:
        cfg.write(f)
    return base


# ---------------------------------------------------------------- cerrojos
def _turno(n):
    from pcsx2_turnos import Turno
    return Turno('pool_%d' % n, puerto(n), str(perfil(n) / 'pcsx2.exe'), entregas=RAIZ_POOL, cuarentena=RAIZ_POOL / '_cuarentena')


def estado_instancia(n):
    """'libre' | 'ocupada pid=...' (solo lectura)."""
    t = _turno(n)
    if not (t.path / 'quien.txt').exists():
        return 'libre'
    try:
        d = t._datos()
    except OSError:
        return 'libre'
    return ('ocupada pid=%s agente=%s' % (d.get('pid'), d.get('agente', '?'))) if t._es_dueno_activo(d) else 'libre (cerrojo de proceso muerto, se aparta al reservar)'


def adquirir(agente='', dry=False, pid=None):
    """Reserva la primera instancia libre a nombre de `pid` (por defecto este proceso; desde la linea de comandos, el shell que llama)."""
    for n in range(1, N_MAX + 1):
        if estado_instancia(n).startswith('libre'):
            if not dry:
                _turno(n).adquirir(pid or os.getpid(), sys.executable)
            return n
    raise SystemExit('las %d instancias estan ocupadas' % N_MAX)


def liberar(n, dry=False, forzar=False):
    """Libera el cerrojo (lo mueve a _cuarentena, no lo borra). Si su dueno sigue vivo hace falta forzar."""
    _comprobar(n)
    t = _turno(n)
    if dry or not (t.path / 'quien.txt').exists():
        return
    dueno = int(t._datos().get('pid', '0'))
    from pcsx2_turnos import process_alive
    if process_alive(dueno) and not forzar:
        raise SystemExit('pool_%d lo tiene el pid %d (vivo): usa --forzar si seguro' % (n, dueno))
    t.liberar(dueno, fuerza=True)


# ---------------------------------------------------------------- arranque
def comando(n, iso, estado=None):
    args = [str(perfil(n) / 'pcsx2.exe'), '-nogui', '-fastboot']
    if estado:
        ruta = Path(estado) if Path(estado).is_absolute() else perfil(n) / 'sstates' / estado
        args += ['-statefile', str(ruta)]
    return args + ['--', str(iso)]


def aplicar_opciones(n, truco=None, velocidad=None, pausado=False):
    """Regenera solo el ini de la instancia (sin copia .antesN) con truco y/o velocidad, e instala el pnach."""
    _comprobar(n)
    ini = perfil(n) / 'inis' / 'PCSX2.ini'
    if not ini.parent.is_dir():
        raise SystemExit('instancia %d sin preparar (usa "preparar %d" antes)' % (n, n))
    escribir_ini(PLANTILLA / 'inis' / 'PCSX2.ini', ini, n, pausado, bool(truco), velocidad)
    return instalar_truco(n, truco) if truco else None


def lanzar(n, iso, estado=None):
    """Arranca PCSX2 sin quitar el foco al usuario (SW_SHOWNOACTIVATE) y adopta el cerrojo con su pid."""
    _comprobar(n)
    t = _turno(n)
    t.adquirir(os.getpid(), sys.executable)
    si = subprocess.STARTUPINFO()
    si.dwFlags = 1
    si.wShowWindow = 4
    proc = None
    try:
        proc = subprocess.Popen(comando(n, iso, estado), cwd=perfil(n), startupinfo=si)
        from pcsx2_turnos import afinar
        afinar(proc.pid)                 # mejora 1: nucleos rapidos (detectados) + prioridad alta, solo para este pid
        t.adoptar(proc.pid)
    except Exception:
        if proc is None:
            t.liberar(os.getpid(), fuerza=True)
        raise
    return proc.pid


def registrar_en_sonda(s, n):
    """Apunta el modulo pcsx2_sonda a la instancia n (puerto, perfil y turno propios) sin tocar sus instancias fijas."""
    from pcsx2_turnos import Turno
    clave = 'pool%d' % n
    s.INSTANCIAS[clave] = (puerto(n), str(perfil(n)), str(perfil(n) / 'capturas'), 'pool_%d' % n)
    s.seleccionar_instancia(['--instancia', clave])
    s.TURNO = Turno('pool_%d' % n, puerto(n), s.EXE, entregas=RAIZ_POOL, cuarentena=RAIZ_POOL / '_cuarentena')
    return s


# ---------------------------------------------------------------- sesion resistente a cortes (mejora 5)
class Sesion:
    """Una instancia del pool lanzada, con reconexion y retoma desde un savestate de control.

    with Sesion(3, iso, estado, contador) as ses:
        ses.ejecutar(cuerpo)       # cuerpo(p, desde): graba; `desde` = contador del control (None = empezar de cero)
    El cuerpo llama a ses.control_cada(p, seg) en su bucle; si PINE se corta y no vuelve (PartidaPerdida) se relanza desde el
    ultimo control (o desde el estado original si aun no hay) y se llama otra vez a cuerpo con `desde`."""

    def __init__(self, n, iso, estado, contador, limite=600, max_retomas=3, pausado=False, truco=None, velocidad=None):
        self.n, self.iso, self.estado, self.contador = n, str(iso), str(estado), contador
        self.limite, self.max_retomas, self.pausado = limite, max_retomas, pausado
        self.truco, self.velocidad = truco, velocidad
        self.control = None          # (ruta, contador en el estado)
        self._base = None
        self._k, self._t = 0, 0.0
        self.pid, self.p = None, None

    def __enter__(self):
        preparar(self.n, [self.estado], pausado=self.pausado, truco=self.truco, velocidad=self.velocidad)
        self._arrancar(self.estado)
        return self

    def __exit__(self, *a):
        self.cerrar()
        return False

    def _arrancar(self, estado):
        import pcsx2_sonda as S
        lanzar(self.n, self.iso, estado)
        m = re.search(r'pid=(\d+)', estado_instancia(self.n))
        self.pid = int(m.group(1)) if m else None
        registrar_en_sonda(S, self.n)
        fin = time.time() + self.limite
        self.p = None
        while time.time() < fin:       # PINE contesta Y el contador de cuadros avanza
            try:
                p = S.Pine()
                a1 = p.lote([(self.contador, 4)])[0]
                if self.pausado:              # parado en el cuadro del savestate: el contador no avanza
                    if p.status() == 1:
                        self.p = p
                        break
                    continue
                time.sleep(2)
                if p.lote([(self.contador, 4)])[0] != a1:
                    self.p = p
                    break
            except Exception:
                time.sleep(4)
        if self.p is None:
            raise RuntimeError('PINE no contesta en %d s' % self.limite)
        self.p.settimeout(30)          # maquina cargada: la respuesta puede tardar

    def control_cada(self, p, seg=20):
        """Guarda un punto de control (slots 90/91 alternos) si pasaron `seg` s desde el ultimo. Un fallo no corta la grabacion."""
        import pcsx2_sonda as S
        if time.time() - self._t < seg:
            return
        self._t = time.time()
        try:
            if self._base is None:       # primer par de slots que este juego no use ya (nunca se pisa un savestate ajeno)
                serial = p.id()
                self._base = next(b for b in range(S.SLOT_CONTROL, 99, 2)
                                  if not S.ruta_estado(b, serial) and not S.ruta_estado(b + 1, serial))
            ruta = S.punto_control(p, self._base + self._k % 2)
            self._k += 1
            self.control = (ruta, S.contador_en_estado(ruta, self.contador))
        except (RuntimeError, OSError):
            pass

    def ejecutar(self, cuerpo):
        import pcsx2_sonda as S
        from pcsx2_turnos import registrar
        desde = None
        for intento in range(self.max_retomas + 1):
            try:
                return cuerpo(self.p, desde)
            except S.PartidaPerdida as e:
                if intento == self.max_retomas:
                    raise
                ruta = self.control[0] if self.control else self.estado
                desde = self.control[1] if self.control else None
                print('partida perdida (%s): retomo desde %s' % (e, os.path.basename(ruta)), flush=True)
                pid = self.pid
                if pid:
                    subprocess.run(['taskkill', '/F', '/PID', str(pid)], capture_output=True)
                    time.sleep(3)
                liberar(self.n, forzar=True)
                registrar('retomada', pool=self.n, desde=os.path.basename(ruta), contador=desde)
                self._arrancar(ruta)

    def cerrar(self):
        if self.pid:
            subprocess.run(['taskkill', '/F', '/PID', str(self.pid)], capture_output=True)
            time.sleep(2)
        subprocess.run([sys.executable, str(Path(__file__).resolve()), 'liberar', str(self.n)], capture_output=True)


def autoprueba():
    assert [puerto(n) for n in range(1, N_MAX + 1)] == [28051, 28052, 28053]
    assert len({str(perfil(n)) for n in range(1, N_MAX + 1)}) == N_MAX
    assert comando(2, 'x.iso', 'a.p2s')[-3:] == [str(perfil(2) / 'sstates' / 'a.p2s'), '--', 'x.iso']
    assert comando(2, 'x.iso')[-2:] == ['--', 'x.iso']
    for n in range(1, N_MAX + 1):
        v = valores_ini(n)
        assert v['EmuCore']['PINESlot'] == str(28050 + n) and v['EmuCore']['SaveStateOnShutdown'] == 'false'
        ops = plan_preparar(n, ['e.p2s'])
        # nada fuera de la carpeta de la instancia: ni lecturas de ella ni escrituras en la plantilla
        assert all(str(b).startswith(str(perfil(n))) for _, _, b in ops), 'una operacion escribe fuera de la instancia'
        assert not any(op == 'copiar' and str(b).startswith(str(PLANTILLA)) for op, _, b in ops)
    # el ini generado respeta la plantilla y fuerza solo lo necesario
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        o = Path(d) / 'a.ini'
        o.write_text('[EmuCore]\nEnablePINE = false\nPINESlot = 1\nOtra = 7\n[MemoryCards]\nSlot1_Enable = true\nSlot1_Filename = sonda1.ps2\n', encoding='utf-8')
        escribir_ini(o, Path(d) / 'b.ini', 3)
        c = configparser.ConfigParser(interpolation=None)
        c.optionxform = str
        c.read(Path(d) / 'b.ini', encoding='utf-8')
        assert c['EmuCore']['PINESlot'] == '28053' and c['EmuCore']['Otra'] == '7' and c['MemoryCards']['Slot1_Filename'] == 'sonda1.ps2'
        assert c['MemoryCards']['Slot2_Enable'] == 'false' and c['SPU2/Output']['OutputMuted'] == 'true'
        escribir_ini(o, Path(d) / 'c.ini', 2, truco=True, velocidad=3)       # truco y velocidad
        c = configparser.ConfigParser(interpolation=None)
        c.optionxform = str
        c.read(Path(d) / 'c.ini', encoding='utf-8')
        assert c['EmuCore']['EnableCheats'] == 'true' and c['Framerate']['NominalScalar'] == '3.0'
        escribir_ini(o, Path(d) / 'e.ini', 2)
        c.read(Path(d) / 'e.ini', encoding='utf-8')
        assert c['EmuCore']['EnableCheats'] == 'false'
    for pn in (Path(__file__).resolve().parent / 'pnach').glob('*.pnach'):
        assert re.fullmatch(r'[0-9A-F]{8}', crc_pnach(pn)), pn
    if PLANTILLA.is_dir():
        existia = perfil(1).exists()
        assert preparar(1, dry=True)
        assert perfil(1).exists() == existia, 'el dry-run no debe crear nada'
    import inspect
    assert 'afinar' in inspect.getsource(lanzar) and callable(Sesion.ejecutar)
    print('AUTOPRUEBA pcsx2_pool OK')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--autoprueba', action='store_true')
    ap.add_argument('--dry-run', action='store_true', dest='dry')
    sub = ap.add_subparsers(dest='cmd')
    sub.add_parser('estado')
    for c in ('preparar', 'liberar', 'comando', 'lanzar'):
        p = sub.add_parser(c)
        p.add_argument('n', type=int)
        p.add_argument('--dry-run', action='store_true', dest='dry')
        if c == 'liberar':
            p.add_argument('--forzar', action='store_true')
        if c in ('preparar',):
            p.add_argument('--estado', action='append', default=[])
        if c in ('comando', 'lanzar'):
            p.add_argument('--iso', required=True)
            p.add_argument('--estado')
        if c == 'lanzar':
            p.add_argument('--ejecutar', action='store_true')
            p.add_argument('--truco', help='pnach de tools/pnach/ (o ruta) que se instala y activa en este pool')
            p.add_argument('--velocidad', type=float, choices=(1, 2, 3), help='limite de fotogramas x1/x2/x3 (modo libre, no el exacto)')
    p = sub.add_parser('tarjeta')
    p.add_argument('n', type=int)
    p.add_argument('nombre')
    p.add_argument('partidas', nargs='*', help='carpetas de partida a copiar dentro de la tarjeta')
    p = sub.add_parser('adquirir')
    p.add_argument('--agente', default='')
    p.add_argument('--pid', type=int, default=os.getppid(), help='dueno del cerrojo (por defecto el proceso que llama)')
    p.add_argument('--dry-run', action='store_true', dest='dry')
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    if a.cmd == 'estado':
        for n in range(1, N_MAX + 1):
            print('pool %d  PINE %d  %s  %s' % (n, puerto(n), perfil(n), estado_instancia(n)) + ('' if perfil(n).exists() else '  [sin preparar]'))
    elif a.cmd == 'preparar':
        print('\n'.join(preparar(a.n, a.estado, a.dry)))
    elif a.cmd == 'tarjeta':
        print(tarjeta_carpeta(a.n, a.nombre, *a.partidas))
    elif a.cmd == 'adquirir':
        n = adquirir(a.agente, a.dry, a.pid)
        print('instancia %d%s  PINE %d  %s' % (n, ' (dry-run, no reservada)' if a.dry else ' reservada', puerto(n), perfil(n)))
    elif a.cmd == 'liberar':
        liberar(a.n, a.dry, a.forzar)
        print('instancia %d liberada%s' % (a.n, ' (dry-run)' if a.dry else ''))
    elif a.cmd == 'comando':
        print(subprocess.list2cmdline(comando(a.n, a.iso, a.estado)))
    elif a.cmd == 'lanzar':
        if not a.ejecutar or a.dry:
            print('SIN LANZAR (falta --ejecutar):', subprocess.list2cmdline(comando(a.n, a.iso, a.estado)))
        else:
            if a.truco or a.velocidad:
                t = Path(a.truco) if a.truco else None
                if t and not t.exists():
                    t = Path(__file__).resolve().parent / 'pnach' / (a.truco if a.truco.endswith('.pnach') else a.truco + '.pnach')
                print('opciones:', aplicar_opciones(a.n, t, a.velocidad))
            print('pid', lanzar(a.n, a.iso, a.estado))
    else:
        ap.print_help()


if __name__ == '__main__':
    main()

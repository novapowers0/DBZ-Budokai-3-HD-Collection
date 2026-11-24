# SPDX-License-Identifier: MIT
"""TRAD/1 (plan PLAN_TRADUCIR_NO_RECREAR): oraculo sin emulador de IW. Ejecuta el JUEGO ENTERO en ee.py desde la entrada del
ELF del jugador (EU SLES_553.47) con el hardware sustituido en su frontera, y saca un guion de ordenes por cuadro.

Cortes (medidos en el decompilado EU, ver el plan):
  - nucleo del EE: syscalls (hilos, semaforos, interrupciones) -> nucleo minimo de un solo hilo (este archivo)
  - SIF/IOP (modulos, RPC de CD y sonido) -> respuestas propias; las lecturas de CD se sirven desde la ISO
  - DMA/GS: sceDmaSend (FUN_00104a88) -> se recorre la cadena y se guardan los paquetes (la captura del dibujo)
  - mando: palabra de botones guionizada
  - reloj: un vblank por giro del bucle principal (FUN_001498d0)
Todo lo demas (libc, libgcc, motor, planificador de tareas) se ejecuta de verdad. Lo que no tiene puente se apunta.

  python tools/iw_mips/oraculo.py arranque [--limite N]   -> hasta donde llega el arranque y que hardware toca
"""
import os
import argparse
import pickle
import struct
import sys
import threading
import zlib
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import ee  # noqa: E402

ISO = Path(r'E:/DBZ3HD_trabajo/iw_eu/Dragon Ball Z - Infinite World (Europe).iso')
ELF = Path(r'E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47')
DEC = Path(r'E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c')
GP = 0x4A3DF0
ENTRADA, MAIN = 0x100008, 0x1498D0
BSS0, BSS1 = 0x49C600, 0x5D9F00


def nombres():
    n = {}
    for l in DEC.open(encoding='utf-8', errors='replace'):
        if l.startswith('// ==== '):
            p = l.split()
            n[int(p[2], 16)] = p[3] if len(p) > 3 else '?'
    return n


TTY = 0x1034F8   # escritura de texto de depuracion por DECI2 (printf del juego): se guarda, no hay depurador
CRI_PRINTF = 0x159B38   # printf de CRI (FUN_00159b38)


class FinHilo(Exception):
    """El hilo del juego termino (ExitThread o volvio su funcion): deshace su `run`."""


RUN, READY, WAIT, SUSP, DORM = 1, 2, 4, 8, 0x10   # estados de hilo del nucleo del PS2 (ReferThreadStatus)


class Hilo:
    def __init__(h, tid, func, pila, tam, gp, prio):
        h.tid, h.func, h.pila, h.tam, h.gp = tid, func, pila, tam, gp
        h.prio = h.prio0 = prio
        h.estado, h.susp, h.espera, h.despertar = DORM, False, None, 0
        h.ctx, h.ev, h.arg, h.seq, h.pc, h.v = None, None, 0, 0, None, 0

    def status(h):
        if h.susp:
            return 0xC if h.estado == WAIT else SUSP
        return h.estado


def estado_cpu(c):
    return (list(c.r), list(c.up), list(c.f), [list(v) for v in c.vf], list(c.vi), c.acc, list(c.vacc), c.q, c.c,
            c.lo, c.hi, c.lo1, c.hi1, getattr(c, 'cop0_status', 0))


def poner_cpu(c, e):
    (c.r[:], c.up[:], c.f[:], vf, c.vi[:], c.acc, c.vacc[:], c.q, c.c, c.lo, c.hi, c.lo1, c.hi1, st) = e
    for i, v in enumerate(vf):
        c.vf[i][:] = v
    if st:
        c.cop0_status = st


class Nucleo:
    """Nucleo del EE: hilos con prioridad como el del PS2 (corre el listo de prioridad mas alta; se cambia solo en una
    syscall o al volver de una interrupcion), semaforos, sueno/despertar, interrupciones. Cada hilo del juego corre en un
    hilo de Python con su propio `run` y solo uno avanza a la vez (testigo `ev`); al cambiar se guardan/ponen registros.
    Si ningun hilo esta listo pasa el tiempo: `ocio` (el reloj) entrega el siguiente vblank. Cada syscall se cuenta."""

    def __init__(s, c):
        s.c, s.cuenta, s.semas = c, Counter(), {}
        s.sig_sema, s.sig_hilo, s.seq = 1, 2, 0
        s.intc, s.dmac = {}, {}
        s.hilos = {1: Hilo(1, MAIN, ee.PILA, 0xFF010, GP, 1)}   # el principal: listo; `dirigir` le da el primer turno
        s.hilos[1].estado = READY
        s.actual, s.en_int, s.error, s.ocio = 0, 0, None, None
        s.ev_fin, s.limite = threading.Event(), 2_000_000_000
        s.cambios = Counter()
        for n in range(-0x80, 0x80):
            c.hooks[('syscall', n)] = s.despachar
        s.tty = []
        c.hooks[TTY] = s.escribir_tty
        c.hooks[CRI_PRINTF] = s.cri_printf

    def cri_printf(s, c):
        # printf de las bibliotecas CRI (errores de ADX/PS2RNA): se guarda el formato y sus argumentos, no se imprime
        a = c.r[4] & ee.M32
        b = bytearray()
        while len(b) < 200 and c.r8(a + len(b)):
            b.append(c.r8(a + len(b)))
        fmt = b.decode('latin-1')
        s.tty.append(f'[CRI] {fmt} {[hex(c.r[k] & ee.M32) for k in (5, 6, 7)]}\n')
        c.r[2] = 0

    def escribir_tty(s, c):
        a, n = c.r[4] & ee.M32, c.r[5] & ee.M32
        s.tty.append(bytes(c.r8(a + i) for i in range(min(n, 256))).decode('latin-1'))
        c.r[2] = n

    # ---------------- planificador ----------------
    def listo(s, h):
        s.seq += 1
        h.estado, h.espera, h.seq = READY, None, s.seq

    def elegible(s):
        cand = [h for h in s.hilos.values() if h.estado == READY and not h.susp]
        return min(cand, key=lambda h: (h.prio, h.seq)) if cand else None

    def ceder(s, c):
        """Tras una syscall (no dentro de una interrupcion): corre el listo de prioridad mas alta."""
        if s.en_int:
            return
        vacio = 0
        while True:
            act = s.hilos[s.actual]
            corre = act.estado == RUN and not act.susp
            mejor = s.elegible()
            if mejor is not None:
                if corre and act.prio <= mejor.prio:
                    return
                s.cambiar(c, act, mejor, corre)
                return
            if corre:
                return
            vacio += 1
            if vacio > 100_000:
                raise RuntimeError('ningun hilo listo tras 100000 vblanks (bloqueo)')
            s.ocio(c)    # nadie puede correr: pasa el tiempo hasta el siguiente vblank

    def cambiar(s, c, act, mejor, corre, saliendo=False):
        """Pasa el turno de `act` (None = el director, que no es un hilo del juego) a `mejor`."""
        if mejor is act:
            act.estado = RUN
            return
        if corre:               # expulsado por uno de mas prioridad: vuelve a la cabeza de su cola
            act.estado, act.seq = READY, -s.seq
        s.cambios[(act.tid if act else 0, mejor.tid)] += 1
        if act is not None and not saliendo:
            act.ctx = estado_cpu(c)
        nuevo = mejor.ev is None    # sin hilo de Python: recien arrancado o recuperado de un punto de partida
        if nuevo:
            if mejor.ctx is None:   # arranque: a0 = argumento, pila y gp propios, vuelta = fin del hilo
                c.r[4], c.r[28] = mejor.arg, mejor.gp
                c.r[29], c.r[31] = (mejor.pila + mejor.tam - 0x10) & ~0xF, ee.RETORNO
                mejor.pc = mejor.func
            else:                   # recuperado: sigue tras la syscall en la que se quedo
                poner_cpu(c, mejor.ctx)
            mejor.ev = threading.Event()
        else:
            poner_cpu(c, mejor.ctx)
        mejor.ctx = None
        mejor.estado, s.actual = RUN, mejor.tid
        if nuevo:
            threading.Thread(target=s.cuerpo, args=(mejor, mejor.pc), daemon=True).start()
        mejor.ev.set()
        if saliendo or act is None:
            return
        act.ev.wait()
        act.ev.clear()

    def cuerpo(s, h, pc):
        h.ev.wait()
        h.ev.clear()
        try:
            s.c.run(pc, s.limite)
            s.salir(s.c, h)
        except FinHilo:
            pass
        except BaseException as ex:   # el fallo (o la parada) de un hilo para el oraculo: se lleva al director
            s.error = ex
            s.ev_fin.set()

    def salir(s, c, h):
        h.estado, h.ctx, h.ev = DORM, None, None
        mejor = s.elegible()
        while mejor is None:
            s.ocio(c)
            mejor = s.elegible()
        s.cambiar(c, h, mejor, False, saliendo=True)

    def dirigir(s, c, limite):
        """El director: da el primer turno y espera a que un hilo falle, pare o agote su limite de instrucciones."""
        s.limite = limite
        c.r[28], c.r[29] = GP, ee.PILA + 0x20000   # pila para las interrupciones que se entreguen sin hilo corriendo
        mejor = s.elegible()
        while mejor is None:
            s.ocio(c)
            mejor = s.elegible()
        s.cambiar(c, None, mejor, False)
        s.ev_fin.wait()
        raise s.error

    # ---------------- punto de partida ----------------
    def foto(s, c):
        """Estado de los hilos para guardar. Solo vale cuando ninguno corre (en `ocio`): todos estan parados en una
        syscall, con su pc y el valor que esa syscall devolvera."""
        hs = []
        for h in s.hilos.values():
            ctx = h.ctx
            if ctx is None and h.tid == s.actual and h.estado != DORM:
                ctx = estado_cpu(c)
            if ctx is not None:
                r = list(ctx[0])
                r[2] = ee.sx32(h.v)
                ctx = (r,) + tuple(ctx[1:])
            d = {k: v for k, v in vars(h).items() if k not in ('ev', 'ctx')}
            d['ctx'] = ctx
            hs.append(d)
        return {'hilos': hs, 'nuc': {k: getattr(s, k) for k in ('semas', 'sig_sema', 'sig_hilo', 'seq', 'intc', 'dmac')},
                'sig_dma': getattr(s, 'sig_dma', 0)}

    def poner_foto(s, f):
        s.hilos = {}
        for d in f['hilos']:
            h = Hilo(d['tid'], d['func'], d['pila'], d['tam'], d['gp'], d['prio0'])
            vars(h).update(d)
            h.ev = None
            s.hilos[h.tid] = h
        for k, v in f['nuc'].items():
            setattr(s, k, v)
        s.sig_dma, s.actual = f['sig_dma'], 0

    def tid(s, a):
        return s.actual if a == 0 else a

    # ---------------- syscalls ----------------
    def despachar(s, c):
        n = ee.s64(c.r[3])
        s.cuenta[n] += 1
        k = abs(n)
        a0, a1 = c.r[4] & ee.M32, c.r[5] & ee.M32
        v, ced = 0, False
        H = s.hilos
        yo = None if s.en_int else H.get(s.actual)
        if yo is not None:
            yo.pc = c.pc_sys + 4
        if k == 0x3C:            # SetupThread(gp, stack, stack_size, args, root): devuelve la pila
            pila, tam = a1, c.r[6] & ee.M32
            v = (pila + tam) if pila != 0xFFFFFFFF else 0x2000000 - 0x10000
        elif k == 0x3D:          # SetupHeap
            v = a0
        elif k == 0x3E:          # EndOfHeap
            v = 0x1F00000
        elif k == 0x40:          # CreateSema(param): {count, max_count, init_count, ...}
            v = s.sig_sema
            s.semas[v] = [c.r32(a0 + 8), c.r32(a0 + 4) or 0x7FFFFFFF, c.r32(a0 + 8)]
            s.sig_sema += 1
        elif k == 0x41:          # DeleteSema
            s.semas.pop(a0, None)
            v = a0
        elif k in (0x42, 0x43):  # (i)SignalSema: despierta al primero que espera o suma
            esp = [h for h in H.values() if h.estado == WAIT and h.espera == ('sema', a0)]
            if esp:
                s.listo(min(esp, key=lambda h: h.seq))
            elif a0 in s.semas:
                se = s.semas[a0]
                se[0] = min(se[0] + 1, se[1])
            v, ced = a0, True
        elif k == 0x44:          # WaitSema: bloquea si esta a cero
            se = s.semas.get(a0)
            if se and se[0] > 0:
                se[0] -= 1
            elif se:
                h = H[s.actual]
                s.seq += 1
                h.estado, h.espera, h.seq = WAIT, ('sema', a0), s.seq
                ced = True
            v = a0
        elif k in (0x45, 0x46):  # (i)PollSema
            se = s.semas.get(a0)
            if se and se[0] > 0:
                se[0] -= 1
                v = a0
            else:
                v = -419         # KE_SEMA_ZERO
        elif k in (0x47, 0x48):  # (i)ReferSemaStatus(id, info)
            se = s.semas.get(a0, [0, 0, 0])
            esp = sum(1 for h in H.values() if h.estado == WAIT and h.espera == ('sema', a0))
            for i, x in enumerate((se[0], se[1], se[2], esp)):
                c.w32(a1 + 4 * i, x)
            v = a0
        elif k == 0x20:          # CreateThread(param): {status, func, stack, stack_size, gp, initial_priority}
            v = s.sig_hilo
            H[v] = Hilo(v, c.r32(a0 + 4), c.r32(a0 + 8), c.r32(a0 + 12), c.r32(a0 + 16), c.r32(a0 + 20))
            s.sig_hilo += 1
        elif k == 0x21:          # DeleteThread
            H.pop(a0, None)
            v = a0
        elif k == 0x22:          # StartThread(tid, arg)
            h = H[a0]
            h.arg, h.ctx, h.ev, h.prio = a1, None, None, h.prio0
            s.listo(h)
            v, ced = a0, True
        elif k in (0x23, 0x24):  # ExitThread / ExitDeleteThread
            h = H[s.actual]
            s.salir(c, h)
            if k == 0x24:
                H.pop(h.tid, None)
            raise FinHilo()
        elif k == 0x25:          # TerminateThread
            H[a0].estado, H[a0].ctx, H[a0].ev = DORM, None, None
            v = a0
        elif k in (0x29, 0x2A):  # (i)ChangeThreadPriority(tid, prio) -> prioridad anterior
            h = H[s.tid(a0)]
            v, h.prio = h.prio, a1
            ced = True
        elif k in (0x2B, 0x2C):  # (i)RotateThreadReadyQueue(prio): el que corre pasa al final de su cola
            h = H[s.actual]
            if h.prio == a0 and h.estado == RUN:
                s.listo(h)
            v, ced = a0, True
        elif k == 0x2F:          # GetThreadId
            v = s.actual
        elif k in (0x30, 0x31):  # (i)ReferThreadStatus(tid, info)
            h = H.get(s.tid(a0))
            if h is None:
                v = -407         # KE_UNKNOWN_THID
            else:
                if a1:
                    tipo = 0 if not h.espera else (1 if h.espera[0] == 'sueno' else 2)
                    sema = h.espera[1] if h.espera and h.espera[0] == 'sema' else 0
                    for i, x in enumerate((h.status(), h.func, h.pila, h.tam, h.gp, h.prio0, h.prio, 0, 0,
                                           tipo, sema, h.despertar)):
                        c.w32(a1 + 4 * i, x)
                v = h.status()
        elif k == 0x32:          # SleepThread
            h = H[s.actual]
            if h.despertar:
                h.despertar -= 1
            else:
                s.seq += 1
                h.estado, h.espera, h.seq = WAIT, ('sueno',), s.seq
                ced = True
            v = s.actual
        elif k in (0x33, 0x34):  # (i)WakeupThread(tid)
            h = H[a0]
            if h.estado == WAIT and h.espera == ('sueno',):
                s.listo(h)
            else:
                h.despertar += 1
            v, ced = a0, True
        elif k in (0x35, 0x36):  # (i)CancelWakeupThread -> cuenta anterior
            h = H[s.tid(a0)]
            v, h.despertar = h.despertar, 0
        elif k in (0x37, 0x38):  # (i)SuspendThread
            H[a0].susp = True
            v, ced = a0, True
        elif k in (0x39, 0x3A):  # (i)ResumeThread
            H[a0].susp = False
            v, ced = a0, True
        elif k == 0x77:          # sceSifSetDma(transferencias, n): envio EE->IOP; el IOP del oraculo lo da por recibido
            s.sig_dma = getattr(s, 'sig_dma', 0) + 1
            v = s.sig_dma
        elif k == 0x76:          # sceSifDmaStat(id): < 0 = terminada
            v = -1
        elif k == 0x7A:          # sceSifGetReg(reg): el IOP ya arranco (SMFLAG 0x60000: SIFINIT|CMDINIT)
            v = 0x60000 if a0 == 4 else 0x10000
        elif k == 0x10:          # AddIntcHandler(causa, funcion, siguiente, arg)
            s.intc.setdefault(a0, []).append((a1, c.r[7] & ee.M32))
            v = len(s.intc[a0])
        elif k == 0x12:          # AddDmacHandler(canal, funcion, siguiente, arg)
            s.dmac.setdefault(a0, []).append((a1, c.r[7] & ee.M32))
            v = len(s.dmac[a0])
        elif k == 0x7F:          # GetMemorySize
            v = 0x2000000
        if yo is not None:
            yo.v = v
        if ced:
            s.ceder(c)           # puede pasar a otro hilo; al volver este hilo tiene otra vez sus registros
        c.r[2] = ee.sx32(v)


BIND_RPC, CALL_RPC = 0x105C90, 0x105E70   # sceSifBindRpc (Ghidra: _CdSemaExit) / sceSifCallRpc (Ghidra: SceKernelDelay)


# Fronteras del IOP que no son RPC (reinicio y carga de modulos): el IOP del oraculo ya esta listo y con todo cargado.
# (direccion, valor devuelto, que es)
IOP_FIJO = [(0x10B658, 1, 'sceSifRebootIop (IOPPRP300Loader)'), (0x10B608, 1, 'sceSifSyncIop'),
            (0x10B128, 0, 'sceSifLoadModule (ModulesCompactor)'),
            # sonido: la biblioteca ADX de CRI habla con su controlador del IOP; su arranque se da por hecho y sus ordenes
            # (ADXT_*) se capturan aparte
            # (0x156F50 ADXPS2_Init ya no se sustituye: ADXF es tambien el lector de ficheros)
            # cache de datos: en el interprete no hay cache; vaciarla es no hacer nada (y ahorra millones de instrucciones)
            (0x102698, 0, 'SyncDCache (rango)'), (0x1025F0, 0, 'InvalidDCache (rango)'),
            # video: las peliculas (Sofdec) decodifican con la IPU, que el oraculo no tiene. TRAD/5b: modelo "pelicula
            # terminada": el reproductor da la ranura 0 sin crear el mwPly (0x4AD160 sigue a 0: el resto del envoltorio no
            # hace nada) y su estado (FUN_0013db50) es 3 = MWE_PLY_STAT_PLAYEND, el que los llamadores tratan como fin.
            # (Antes -1 sin mascara: ee.py hacia que ni bltz ni bgez saltaran; era un fallo, no un modelo.)
            (0x13D4D0, 0, 'arranque de pelicula/voz Sofdec (FUN_0013d4d0): ranura 0'),
            (0x13DB50, 3, 'estado de pelicula/voz Sofdec (FUN_0013db50): PLAYEND')]


class Iop:
    """El IOP visto desde el EE: solo por RPC. Cada servicio (sid) que el juego enlaza responde aqui; lo que no se conoce
    devuelve el buffer de respuesta a cero y se apunta en `rpc` (sid, funcion, tamanos)."""

    def __init__(s, c):
        s.c, s.sid, s.rpc = c, {}, Counter()
        s.iop_libre = 0x100000          # memoria del IOP (2 MB) que reparte el servicio de heap
        s.sig_id = 1
        s.servicios = {0x80000003: s.heap_iop, 0x90000200: s.dtx, 0x11: s.sonido, 0x80000400: s.tarjeta}
        s.fines = []
        s.snd = [0, False]
        c.hooks[BIND_RPC] = s.bind
        c.hooks[CALL_RPC] = s.call
        for a, v, _ in IOP_FIJO:
            c.hooks[a] = (lambda v: lambda c: c.r.__setitem__(2, v))(v)

    def heap_iop(s, c, fno, env, tam_env, resp, tam_resp):
        # sid 0x80000003 (SIFHEAP del IOP): fno 1 = AllocIopHeap(tam) -> direccion; 2 = Free; 3 = Load
        if fno == 1:
            tam = (c.r32(env) + 0xFF) & ~0xFF
            c.w32(resp, s.iop_libre)
            s.iop_libre += tam

    def tarjeta(s, c, fno, env, tam_env, resp, tam_resp):
        # sid 0x80000400 (libmc): no hay tarjeta de memoria. 0xFE = sceMcInit -> {0, version del modulo, version de
        # mcserv} (libmc exige >= 0x20A y >= 0x20E); el resto -> resultado -10 (sin tarjeta; tipo/libre/formato a 0).
        if not resp:
            return
        if fno == 0xFE:
            for i, x in enumerate((0, 0x20E, 0x20E)):
                c.w32(resp + 4 * i, x)
        else:
            c.w32(resp, -10 & ee.M32)

    def sonido(s, c, fno, env, tam_env, resp, tam_resp):
        # sid 0x11: controlador de sonido propio del juego (FUN_0013e7c0). Carga en flujo con dos buferes: 0x18 empieza
        # (bufer 0), 0x1A manda el bufer env[0], 0x19 pregunta cual suena (-1 = acabo). El IOP del oraculo consume cada
        # envio al momento: responde el ultimo bufer mandado y, si no llego nada nuevo desde la ultima pregunta, -1.
        if fno == 0x18:
            s.snd = [0, True]
        elif fno == 0x1A:
            s.snd = [c.r32(env), True]
        elif fno == 0x19 and resp:
            c.w32(resp, s.snd[0] if s.snd[1] else 0xFFFFFFFF)
            s.snd[1] = False

    def dtx(s, c, fno, env, tam_env, resp, tam_resp):
        # sid 0x90000200 (DTX de CRI: transferencias EE<->IOP de la ADX/Sofdec): fno 2 = crear -> identificador != 0
        # fno 0x400+N = orden N al controlador de sonido de CRI en el IOP (FUN_00168288). Las que crean algo devuelven un
        # identificador: 0 SJX, 8 PSJ, 0x20/0x21/0x22 SJRMT (anillo/memoria/uni). Las consultas devuelven 0 (IOP en reposo).
        if resp and (fno == 2 or fno - 0x400 in (0, 8, 0x20, 0x21, 0x22)):
            c.w32(resp, 0x80000 + 0x100 * s.sig_id)
            s.sig_id += 1

    def bind(s, c):
        cli, sid = c.r[4] & ee.M32, c.r[5] & ee.M32
        s.sid[cli] = sid
        c.w32(cli + 0x24, 0x1F000000 | (sid & 0xFFFF))   # servidor != 0: enlazado
        c.r[2] = 0

    def call(s, c):
        cli, fno, modo = c.r[4] & ee.M32, c.r[5] & ee.M32, c.r[6] & ee.M32
        env, tam_env, resp, tam_resp = c.r[7] & ee.M32, c.r[8] & ee.M32, c.r[9] & ee.M32, c.r[10] & ee.M32
        sid = s.sid.get(cli, 0)
        s.rpc[(hex(sid), fno, tam_env, tam_resp)] += 1
        f = s.servicios.get(sid)
        if resp and tam_resp:
            for k in range(0, tam_resp, 4):
                c.w32(resp + k, 0)
        if f:
            f(c, fno, env, tam_env, resp, tam_resp)
        # funcion de fin (8.o y 9.o argumento de sceSifCallRpc): la llama la interrupcion del SIF cuando el IOP contesta.
        # Aqui el IOP contesta en el acto; el aviso se entrega en el siguiente vblank (como los fines de DMA).
        fin = c.r[11] & ee.M32
        if fin:
            s.fines.append((fin, c.r32(c.r[29] & ee.M32)))
        c.r[2] = 0


class Hw:
    """Registros de hardware del EE vistos por el juego. Por defecto leen 0 (DMA parado, FIFO vacia). El reloj: cada espera de
    vblank (INTC_STAT bit 2, FUN_00102230) cuenta un vblank."""
    INTC_STAT = 0x1000F000

    def __init__(s, c):
        s.c, s.vblanks, s.escrituras = c, 0, Counter()
        s.reg, s.kicks, s.sin_soporte, s.pendientes, s.captura = {}, Counter(), Counter(), [], None
        c.leer_io, c.escribir_io = s.leer, s.escribir

    def leer(s, a, n):
        if a == s.INTC_STAT:
            return 0x0C            # VBLANK_S y VBLANK_E pendientes
        if a in s.reg and (a & 0xFFFF0000) == 0x10000000 and a not in (s.INTC_STAT,):
            return s.reg[a]
        if a == 0x12001000:        # GS_CSR: campo par/impar (bit 13) alterno en cada vblank (entrelazado)
            # VSINT disponible por defecto (volcado 7DE: FUN_00128b90 espera el bit 1); DBZ3_ORA_VSINT=0 da el modelo anterior
            return ((s.vblanks & 1) << 13) | (2 if os.environ.get('DBZ3_ORA_VSINT', '1') == '1' else 0)
        return 0

    CANALES = {0x10008000: 0, 0x10009000: 1, 0x1000A000: 2, 0x1000B000: 3, 0x1000B400: 4, 0x1000C000: 5,
               0x1000C400: 6, 0x1000C800: 7, 0x1000D000: 8, 0x1000D400: 9}

    def escribir(s, a, v, n):
        s.escrituras[a & 0xFFFFFFF0] += 1
        s.reg[a] = v & 0xFFFFFFFF
        if a == s.INTC_STAT and v & 4:
            s.vblanks += 1
        canal = s.CANALES.get(a)
        if canal is not None and v & 0x100:          # CHCR con STR: arranca la DMA del canal
            s.dma(canal, a, v)

    def dma(s, canal, base, chcr):
        c = s.c
        madr, qwc, tadr = s.reg.get(base + 0x10, 0), s.reg.get(base + 0x20, 0), s.reg.get(base + 0x30, 0)
        s.kicks[canal] += 1
        if canal in (1, 2) and s.captura is not None:      # VIF1 / GIF: el dibujo del cuadro
            s.captura(canal, chcr, madr, qwc, tadr)
        elif canal in (8, 9):                              # scratchpad: copia de verdad (modo normal)
            sadr = s.reg.get(base + 0x80, 0) & 0x3FF0
            tam = (qwc & 0xFFFF) * 16
            spr, ram = 0x70000000 + sadr, madr & 0x1FFFFFFF
            org, dst = (spr, ram) if canal == 8 else (ram, spr)
            if (chcr >> 2) & 3 == 0:
                for k in range(0, tam, 8):
                    c.w64(dst + k, c.r64(org + k))
            else:
                s.sin_soporte[('spr_cadena', canal)] += 1
        s.reg[base] = chcr & ~0x100                         # la DMA acaba en el acto (STR a 0)
        s.pendientes.append(canal)                           # y su interrupcion se entrega en el siguiente punto seguro


def anidada(c, addr, *args):
    """Llama a una funcion del juego en medio de otra (manejador de interrupcion) sin tocar sus registros."""
    guarda = estado_cpu(c)
    try:
        c.call(addr, *args, sp=(c.r[29] & ee.M32) - 0x400, limite=5_000_000)
    finally:
        poner_cpu(c, guarda)


PAD_READ = 0x10EB18   # scePadRead(puerto, ranura, datos) -> 32 bytes; botones activos a nivel bajo


class Mando:
    """Pulsaciones guionizadas: `guion` = {vblank: mascara de botones del PS2 (0x0010 arriba ... 0x4000 X, 0x0008 START)}; la
    mascara se mantiene hasta el siguiente cambio."""

    def __init__(s, c, reloj, guion=None):
        s.c, s.reloj, s.guion, s.actual = c, reloj, dict(guion or {}), 0
        c.hooks[PAD_READ] = s.leer
        # resto de libpad a la misma altura: un DualShock 2 en modo analogico, estable, en el puerto 0; el puerto 1 vacio.
        # (el bufer que rellena el IOP no se emula; se contesta lo que libpad sacaria de el)
        def r(f):
            return lambda c: c.r.__setitem__(2, f(c.r[4] & ee.M32, c.r[6] & ee.M32, ee.s64(c.r[7])))
        c.hooks[0x10EB90] = r(lambda puerto, a2, a3: 6 if puerto == 0 else 0)             # scePadGetState: STABLE / DISCONNECT
        c.hooks[0x10EFB8] = r(lambda puerto, term, offs:                                  # scePadInfoMode
                              0 if puerto else {1: 7, 2: 7, 3: 1, 4: 2 if offs < 0 else (4, 7)[offs & 1]}.get(term, 0))
        c.hooks[0x10ED88] = r(lambda puerto, a2, a3: 0)                                   # scePadInfoAct: sin motores
        c.hooks[0x10F4A8] = r(lambda puerto, a2, a3: 0)                                   # scePadInfoPressMode: sin presion

    def leer(s, c):
        puerto, datos = c.r[4] & ee.M32, c.r[6] & ee.M32
        if puerto == 0:                       # la ultima orden del guion hasta este vblank (el juego no lee en todos)
            antes = [k for k in s.guion if k <= s.reloj.n]
            if antes:
                s.actual = s.guion[max(antes)]
        b = (~s.actual if puerto == 0 else ~0) & 0xFFFF
        bs = [0x00, 0x73, b & 0xFF, b >> 8, 0x80, 0x80, 0x80, 0x80] + [0] * 24
        for i, x in enumerate(bs):
            c.w8(datos + i, x)
        c.r[2] = 32


class Cd:
    """El lector de DVD a nivel de libcdvd: los sectores salen de la ISO del jugador. Lecturas al instante (sceCdSync = hecho).
    sceCdRead 0x1113D0, sceCdSync 0x110998, sceCdSearchFile 0x110488, sceCdGetError 0x111680, sceCdDiskReady 0x1111B0."""

    def __init__(s, c):
        s.c, s.f, s.lecturas, s.busquedas = c, ISO.open('rb'), Counter(), []
        c.hooks[0x1113D0] = s.leer
        c.hooks[0x110998] = lambda c: c.r.__setitem__(2, 0)
        c.hooks[0x111680] = lambda c: c.r.__setitem__(2, 0)
        c.hooks[0x1111B0] = lambda c: c.r.__setitem__(2, 2)
        c.hooks[0x110488] = s.buscar

    def sector(s, lsn, n=1):
        s.f.seek(lsn * 2048)
        return s.f.read(2048 * n)

    def leer(s, c):
        lsn, n, buf = c.r[4] & ee.M32, c.r[5] & ee.M32, c.r[6] & ee.M32
        d = s.sector(lsn, n)
        p = c.ph(buf)
        c.m[p:p + len(d)] = d
        s.lecturas[lsn] += n
        c.r[2] = 1

    def buscar(s, c):
        fich, nom = c.r[4] & ee.M32, c.r[5] & ee.M32
        ruta = bytes(iter(lambda i=[nom]: (c.r8(i[0]), i.__setitem__(0, i[0] + 1))[0], 0)).decode('latin-1')
        hallado = s.iso_buscar(ruta)
        s.busquedas.append((ruta, hallado))
        if not hallado:
            c.r[2] = 0
            return
        lsn, tam, nombre = hallado
        c.w32(fich, lsn); c.w32(fich + 4, tam)
        for i in range(16):
            c.w8(fich + 8 + i, nombre[i] if i < len(nombre) else 0)
        c.r[2] = 1

    def iso_buscar(s, ruta):
        pvd = s.sector(16)
        raiz = pvd[156:156 + 34]
        lsn, tam = struct.unpack_from('<I', raiz, 2)[0], struct.unpack_from('<I', raiz, 10)[0]
        partes = [x for x in ruta.replace(chr(92), '/').split('/') if x]
        for k, parte in enumerate(partes):
            busca = parte.upper().split(';')[0]
            d = s.sector(lsn, (tam + 2047) // 2048)
            i, hallado = 0, None
            while i < len(d):
                l = d[i]
                if l == 0:
                    i = (i // 2048 + 1) * 2048
                    continue
                nl = d[i + 32]
                nom = d[i + 33:i + 33 + nl].decode('latin-1').upper().split(';')[0]
                if nom == busca:
                    hallado = (struct.unpack_from('<I', d, i + 2)[0], struct.unpack_from('<I', d, i + 10)[0], nom.encode())
                    break
                i += l
            if not hallado:
                return None
            lsn, tam = hallado[0], hallado[1]
        return hallado


ESPERA_COLA = 0x13B630   # espera activa del juego a que se vacie su cola de DMA (la vacian las interrupciones)


class Reloj:
    """El tiempo pasa cuando ningun hilo del juego puede correr (todos esperan): entonces llega el siguiente vblank. Se
    entregan los fines de DMA pendientes (manejadores DMAC) y la interrupcion VBLANK_S (causa 2); lo que esos manejadores
    despierten (iWakeupThread, iSignalSema) corre despues, por prioridad."""

    def __init__(s, c, nuc, hw, iop):
        s.c, s.nuc, s.hw, s.iop, s.n = c, nuc, hw, iop, 0
        s.sin_tiempo, s.en_espera = 0, False
        c.hooks[ESPERA_COLA] = s.espera_cola
        s.guardar, s.parar, s.partes = {}, None, {}   # {vblank: ruta}, vblank de parada, objetos con estado
        nuc.ocio = s.vblank

    def vblank(s, c):
        """Nadie puede correr. Si hay respuestas de hardware pendientes (fin de DMA, respuesta del IOP) se entregan ya, sin
        que pase el tiempo: en la consola llegan a los microsegundos. Si no hay, pasa el tiempo hasta el vblank."""
        pend, s.hw.pendientes = s.hw.pendientes, []
        fines, s.iop.fines[:] = list(s.iop.fines), []
        s.nuc.en_int += 1
        try:
            if pend or fines:
                for canal in pend:                       # fin de DMA: manejadores DMAC del canal
                    for f, arg in s.nuc.dmac.get(canal, []):
                        anidada(c, f, canal, arg)
                for f, arg in fines:                     # respuestas del IOP: funciones de fin de RPC
                    anidada(c, f, arg)
                s.sin_tiempo += 1
                if s.sin_tiempo < 100_000:               # (seguro: algo que se realimenta sin dormir nunca)
                    return
            s.sin_tiempo = 0
            hechos = [k for k in s.guardar if k <= s.n]
            if hechos and not s.en_espera:           # (dentro de una espera activa el hilo no esta en una syscall)
                s.guardar_punto(c, s.guardar.pop(hechos[0]))
            if s.parar is not None and s.n >= s.parar:
                raise RuntimeError(f'parada pedida en el vblank {s.n}')
            s.n += 1
            s.hw.vblanks += 1
            for f, arg in s.nuc.intc.get(2, []):
                anidada(c, f, 2, arg)
        finally:
            s.nuc.en_int -= 1

    def espera_cola(s, c):
        """FUN_0013b630(canal): el juego espera dando vueltas (sin dormir) a que su cola de envios al GS se vacie; la vacian
        las interrupciones. En la consola le interrumpen; aqui, mientras la condicion no se cumple, se entrega lo pendiente
        o pasa un vblank. La condicion es la misma que mira el juego."""
        canal = c.r[4] & ee.M32
        s.en_espera = True
        try:
            for _ in range(100_000):
                if c.r16(canal * 0x4C + 0x4AD0C4) == 0 and c.r32(canal * 4 + 0x49C8D0) & 0x80000000:
                    return
                s.vblank(c)
            raise RuntimeError('la cola de envios al GS no se vacia (FUN_0013b630)')
        finally:
            s.en_espera = False

    def guardar_punto(s, c, ruta):
        """Punto de partida propio del oraculo: RAM entera, hilos y el estado de cada frontera (no es un savestate de
        emulador: sale de ejecutar el juego aqui)."""
        NO = ('c', 'f', 'captura', 'servicios', 'reloj', 'guion', 'rpc', 'escrituras', 'kicks', 'sin_soporte', 'lecturas',
              'busquedas')
        d = {'mem': bytes(c.m), 'cop0': c.cop0_status, 'n': s.n, 'nucleo': s.nuc.foto(c),
             'partes': {k: {a: v for a, v in vars(o).items() if a not in NO} for k, o in s.partes.items()}}
        Path(ruta).parent.mkdir(parents=True, exist_ok=True)
        Path(ruta).write_bytes(zlib.compress(pickle.dumps(d), 1))

    def poner_punto(s, c, ruta):
        d = pickle.loads(zlib.decompress(Path(ruta).read_bytes()))
        c.m[:] = d['mem']
        c.cop0_status, s.n = d['cop0'], d['n']
        s.nuc.poner_foto(d['nucleo'])
        for k, v in d['partes'].items():
            vars(s.partes[k]).update(v)


def montar():
    d = ELF.read_bytes()
    mem = bytearray(ee.TOTAL)
    phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
    for i in range(phnum):
        tipo, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
        if tipo == 1 and fsz:
            mem[va:va + fsz] = d[off:off + fsz]
    mem[BSS0:BSS1] = bytes(BSS1 - BSS0)          # lo que hace `entry` antes de llamar a main
    c = ee.Ee(mem, GP, nombres=nombres())
    c.cop0_status = 0x70030C13   # hilo de usuario del EE con interrupciones activas (IE, EIE): sin esto StartThread se niega
    return c


def arranque(limite, guion=None, desde=None, guardar=None, parar=None, captura=None, captura_desde=0):
    c = montar()
    nuc = Nucleo(c)
    iop = Iop(c)
    hw = Hw(c)
    reloj = Reloj(c, nuc, hw, iop)
    mando = Mando(c, reloj, guion)
    cd = Cd(c)
    reloj.partes = {'iop': iop, 'hw': hw, 'mando': mando}
    reloj.guardar, reloj.parar = dict(guardar or {}), parar
    if desde:
        reloj.poner_punto(c, desde)
    cap = None
    if captura:                      # guion de ordenes de dibujo por cuadro (gs_captura.py)
        import gs_captura
        cap = hw.captura = gs_captura.Captura(c, reloj, captura_desde)
    try:
        nuc.dirigir(c, limite)
    except Exception as ex:
        fin = f'{type(ex).__name__}: {ex}'
    print('fin:', fin, '| instrucciones', c.n)
    if cap:
        cap.volcar(captura)
        res = cap.resumen()
        print('captura:', len(res), 'cuadros ->', captura, '| errores', cap.errores[:5], '| ultimo', list(res.items())[-1:] )
    import hashlib
    print('vblank', reloj.n, '| huella de la RAM:', hashlib.md5(bytes(c.m[:ee.RAM])).hexdigest())
    ts, a, k = [], c.r32(0x4AD558), 0          # lista de tareas del planificador del juego (FUN_00140200)
    while a != 0x4AD570 and a and k < 60:
        f = c.r32(a + 0x1C)
        ts.append(c.nombres.get(f, hex(f)))
        a, k = c.r32(a + 0x28), k + 1
    print('tareas del juego:', ts)
    print('syscalls:', {hex(k): v for k, v in sorted(nuc.cuenta.items())})
    print('hilos:', {t: (hex(h.func), h.prio, h.status(), h.espera) for t, h in nuc.hilos.items()}, '| cambios:', dict(nuc.cambios.most_common(8)))
    print('manejadores INTC:', {k: [hex(f) for f, _ in v] for k, v in nuc.intc.items()}, '| vblanks:', hw.vblanks, '| escrituras de hardware:', {hex(k): v for k, v in hw.escrituras.most_common(10)})
    print('texto del juego:', ''.join(nuc.tty)[-600:])
    print('DMA por canal:', dict(hw.kicks), '| sin soporte:', dict(hw.sin_soporte), '| DMAC:', {k: [hex(f) for f, _ in v] for k, v in nuc.dmac.items()})
    print('CD: busquedas', cd.busquedas[:12], '| sectores leidos', sum(cd.lecturas.values()))
    print('RPC:', dict(iop.rpc.most_common(20)))
    io = Counter()
    for a, v in c.io.items():
        io[a & 0xFFFFF000] += v
    print('E/S por pagina:', {hex(k): v for k, v in io.most_common(12)})
    top = c.llamadas.most_common(25)
    print('llamadas:', [(hex(a), c.nombres.get(a, '?'), v) for a, v in top])


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('que', choices=['arranque'])
    ap.add_argument('--limite', type=int, default=20_000_000)
    ap.add_argument('--pulsa', default='')
    ap.add_argument('--desde', help='punto de partida guardado')
    ap.add_argument('--guardar', default='', help="'vblank=ruta' (se guarda al llegar a ese vblank)")
    ap.add_argument('--parar', type=int, help='vblank en el que parar')
    ap.add_argument('--captura', help='JSONL con el guion de ordenes de dibujo por cuadro')
    ap.add_argument('--captura-desde', type=int, default=0)
    o = ap.parse_args()
    # --pulsa 'vblank:mascara,...' (hex): p. ej. 600:4000,604:0 = X entre los vblanks 600 y 604
    arranque(o.limite, {int(k): int(v, 16) for k, v in (x.split(':') for x in o.pulsa.split(',') if x)}, o.desde,
             {int(o.guardar.split('=', 1)[0]): o.guardar.split('=', 1)[1]} if o.guardar else None, o.parar, o.captura, o.captura_desde)

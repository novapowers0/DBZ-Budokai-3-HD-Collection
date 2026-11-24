# SPDX-License-Identifier: MIT
"""B2-2 (plan PLAN_TRADUCIR_NO_RECREAR, paso 4): oraculo sin emulador de Budokai 2 PAL (SLES-51839). Es el oraculo de IW
(tools/iw_mips/oraculo.py: nucleo de hilos, IOP, CD, mando, reloj, captura GS) con las direcciones del ELF de B2; aqui
solo va lo que cambia. Direcciones halladas con sdk_firmas.py y el decompilado (b2.c); ver B2_TRAD_RELEVO.md.

  python tools/b2_oraculo/oraculo_b2.py [--limite N] [--pulsa 'vblank:mascara,...'] [--parar V] [--guardar V=ruta]
         [--desde ruta] [--captura salida.jsonl] [--captura-desde V] [--spx salida.jsonl] [--para-io PAGINA_HEX]
  python tools/b2_oraculo/oraculo_b2.py --autoprueba      (20 s)
"""
import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / 'iw_mips'))
import ee  # noqa: E402
import oraculo as o  # noqa: E402

# ---- lo que cambia respecto a IW ----
o.ISO = Path(r'D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
o.ELF = Path(r'E:/DBZ3HD_trabajo/ghidra/elfs/b2.elf')
o.DEC = Path(r'E:/DBZ3HD_trabajo/ghidra/salida/b2.c')
o.GP = 0x3ABCF0
o.ENTRADA, o.MAIN = 0x100008, 0x150340          # entry -> FUN_00150340 (bucle principal)
o.BSS0, o.BSS1 = 0x3A4100, 0x69F580
o.TTY = 0x10B0A8
o.CRI_PRINTF = 0                                 # sin localizar: 0 = no se sustituye nada
o.BIND_RPC, o.CALL_RPC = 0x10CCD8, 0x10CEA8
o.PAD_READ = 0x115400
o.ESPERA_COLA = 0x1434C0                         # FUN_001434c0(canal): espera activa a que se vacie la cola de DMA


def espera_cola(s, c):
    """Reloj.espera_cola de IW con las variables de B2: cola 0x3B4344 + canal*0xC (16 bits) y gp-0x78D0 + canal*4."""
    canal = c.r[4] & ee.M32
    s.en_espera = True
    try:
        for _ in range(100_000):
            if c.r16(canal * 0xC + 0x3B4344) == 0 and c.r32(canal * 4 + 0x3A4420) & 0x80000000:
                return
            s.vblank(c)
        raise RuntimeError('la cola de envios al GS no se vacia (FUN_001434c0)')
    finally:
        s.en_espera = False


o.Reloj.espera_cola = espera_cola
o.IOP_FIJO = [(0x112168, 1, 'sceSifRebootIop'), (0x112118, 1, 'sceSifSyncIop'), (0x111C38, 0, 'sceSifLoadModule'),
              (0x10A3D8, 0, 'SyncDCache (rango)'), (0x10A518, 0, 'SyncDCache (2a copia)'),
              (0x10A330, 0, 'InvalidDCache (rango)'),
              # pelicula Sofdec (MWSFD 2.88; decodifica con la IPU, que el oraculo no tiene). Modelo "sin ranura libre":
              # el arranque (FUN_00145f20) devuelve -1, el camino que el propio juego trata (FUN_002a47a0: lVar2 < 0 ->
              # FUN_00232e10(3,0) + FUN_00232e00, sigue sin pelicula). El de IW (ranura 0 sin mwPly) aqui no vale: el
              # llamador usa el mwPly directamente (volumen, vtable +0x2c). Estado (FUN_001462c0) sin mwPly = -1 (original).
              (0x145F20, 0xFFFFFFFFFFFFFFFF, 'arranque de pelicula Sofdec (FUN_00145f20): sin ranura')]
# IW -> B2: libpad y libcdvd (las clases de IW cuelgan sus puentes en direcciones fijas; aqui se mudan)
MUDANZA = {0x10EB90: 0x115568, 0x10EFB8: 0x115948, 0x10ED88: 0x115708, 0x10F4A8: 0x115E38,      # libpad
           0x1113D0: 0x117BF0, 0x110998: 0x1171E0, 0x111680: 0x117EA0, 0x1111B0: 0x1179E8, 0x110488: 0x116CA0}  # libcdvd
# Lo que se apunta con sus argumentos (--spx). Tabla de builtins del #SPX: 0x379060 + 4*indice (0x157 -> FUN_002249A0,
# el de pasarela_b2.cpp:3651); de ahi salen 0x150..0x154. FUN_0012D100(afs << 16 | entrada) = lectura de un fichero del AFS.
SPX = {0x12D100: 'FUN_0012D100 lectura AFS (a0 = afs<<16 | entrada)',
       0x224C80: 'builtin 0x150', 0x1E8DC0: 'builtin 0x151', 0x1E7AE0: 'builtin 0x152', 0x224BB0: 'builtin 0x153',
       0x224B50: 'builtin 0x154'}
# Lista de dibujo del tablero (FUN_0023a9f0, tarea 0x23AAD0; lista en gp-0x7704, tipo en +0, siguiente en +4, dato en +0x10):
# las figuras 3D del tablero (van por la VU1) salen de aqui como ORDEN: se apunta el dato entero (12 palabras).
#   tipo 4 -> FUN_0023a050: figura del jugador (modelo en gp-0x7710; +0x10 animacion banco<<16|clip, +0x18 cuadro)
#   tipo 3 -> FUN_0023a2e0: otras figuras (modelo = tabla 0x6994C0[dato+0x28]; +0x10 animacion, +0x1C cuadro; x,y en +0,+4)
#   tipos 2 y 5 -> FUN_0023a620: pieza con animacion por claves (+0x10 clave, +0x14 clave siguiente, +0x18 t, +0x1C datos)
ACTOR = {0x23A050: 'figura jugador (tipo 4)', 0x23A2E0: 'figura (tipo 3)', 0x23A620: 'pieza animada (tipos 2/5)'}
SPX.update(ACTOR)


GUION_ES = '100:40,104:0,120:40,124:0,140:40,144:0,160:40,164:0,180:4000,184:0'   # idioma: abajo x4 + X = espanol
# Guion entero validado: de la entrada del ELF al tablero de Mundo Dragon (fase I, seleccion de companero) sin puntos.
GUION_TABLERO = ','.join([GUION_ES, '1200:10,1204:0,1220:4000,1224:0',            # tarjeta: arriba (Si) + X
                          '2410:8,2414:0',                                         # titulo: START
                          '2810:4000,2814:0,3210:4000,3214:0',                     # Nueva Partida, aviso de tarjeta
                          '3710:10,3714:0,3730:4000,3734:0',                       # sin autoguardado: Si
                          '4310:4000,4314:0',                                      # menu principal: Mundo Dragon
                          ','.join(f'{v}:4000,{v + 4}:0' for v in range(5010, 5701, 80)),   # (X durante la narracion)
                          '6010:8,6014:0',                                         # START: salta la narracion
                          '6800:8,6804:0,6950:8,6954:0',                           # START: salta la escena de Raditz
                          '7100:4000,7104:0,7250:4000,7254:0,7400:4000,7404:0,7500:20,7530:0,7560:4000,7564:0'])
TABLERO = (7700, 'd3ab335bead33b66fe1ba9cafd13e058')   # vblank y RAM al final de ese guion (corrida por puntos, 13/11)
AUTOPRUEBA = '43b7b7e34b3c5c986e792789343a51c5'   # RAM en el vblank 600 con GUION_ES (13/11, dos corridas iguales)


def mudar(c):
    for iw, b2 in MUDANZA.items():
        c.hooks[b2] = c.hooks.pop(iw)
    c.hooks.pop(0, None)     # (CRI_PRINTF sin localizar)


def arranque(a):
    c = o.montar()
    nuc, iop, hw = o.Nucleo(c), o.Iop(c), o.Hw(c)
    reloj = o.Reloj(c, nuc, hw, iop)
    guion = {int(k): int(v, 16) for k, v in (x.split(':') for x in a.pulsa.split(',') if x)}
    mando, cd = o.Mando(c, reloj, guion), o.Cd(c)
    mudar(c)
    reloj.partes = {'iop': iop, 'hw': hw, 'mando': mando}
    if a.guardar:
        v, ruta = a.guardar.split('=', 1)
        reloj.guardar = {int(v): ruta}
    reloj.parar = a.parar
    if a.desde:
        reloj.poner_punto(c, a.desde)
    cap = None
    if a.captura:
        import gs_captura
        cap = hw.captura = gs_captura.Captura(c, reloj, a.captura_desde)
    spx = []
    if a.spx:          # se apunta la llamada con sus argumentos y se ejecuta igual (no se sustituye)
        entrar0 = c.entrar

        def entrar(t):
            if t in SPX:
                spx.append({'n': reloj.n, 'f': hex(t), 'a': [hex(c.r[k] & ee.M32) for k in (4, 5, 6, 7)],
                            'ra': hex(c.r[31] & ee.M32)})
                if t in ACTOR:
                    spx[-1]['dato'] = [hex(c.r32((c.r[4] & ee.M32) + 4 * k)) for k in range(12)]
            return entrar0(t)
        c.entrar = entrar
    lect = []
    if getattr(a, 'cd', None):   # cada sceCdRead (vblank, lsn, sectores): de ahi sale QUE entradas de AFS lee el original
        leer_cd = c.hooks[0x117BF0]

        def leer_y_apuntar(c):
            lect.append([reloj.n, c.r[4] & ee.M32, c.r[5] & ee.M32])
            leer_cd(c)
        c.hooks[0x117BF0] = leer_y_apuntar
    if getattr(a, 'para_io', None):      # diagnostico: para en el primer acceso a esa pagina de E/S y ensena la pila de llamadas
        leer0, pag = c.leer_io, int(a.para_io, 16)

        def leer(d, n):
            if d & 0xFFFFF000 == pag:
                raise RuntimeError(f'E/S {d:#x}')
            return leer0(d, n)
        c.leer_io = leer
    try:
        nuc.dirigir(c, a.limite)
    except Exception as ex:
        fin = f'{type(ex).__name__}: {ex}'
    print('fin:', fin, '| instrucciones', c.n)
    sp = c.r[29] & ee.M32
    pila = [c.r32(sp + k) for k in range(0, 0x600, 4)]
    print('pila (retornos):', hex(c.r[31] & ee.M32), [hex(x) for x in pila if 0x100000 <= x < 0x2A4100 and x & 3 == 0
                                                       and c.r32(x - 8) >> 26 in (3, 0)][:24])
    if cap:
        cap.volcar(a.captura)
        print('captura:', len(cap.resumen()), 'cuadros ->', a.captura, '| errores', cap.errores[:5])
    if getattr(a, 'cd', None):
        Path(a.cd).write_text(json.dumps(lect), encoding='utf-8')
    if a.spx:
        Path(a.spx).write_text('\n'.join(json.dumps(x) for x in spx), encoding='utf-8')
    huella = hashlib.md5(bytes(c.m[:ee.RAM])).hexdigest()
    print('vblank', reloj.n, '| huella de la RAM:', huella)
    if getattr(a, 'tablero', False) and reloj.n == TABLERO[0]:
        print('tablero:', 'OK, RAM identica a la corrida por puntos' if huella == TABLERO[1] else 'DISTINTA de la corrida por puntos')
    if getattr(a, 'autoprueba', False):
        assert (reloj.n, huella, len(nuc.hilos)) == (600, AUTOPRUEBA, 5), 'el arranque de B2 ya no es el validado'
        print('autoprueba: OK (idioma -> aviso de tarjeta en el vblank 600, RAM identica, 5 hilos)')
    print('syscalls:', {hex(k): v for k, v in sorted(nuc.cuenta.items())})
    print('hilos:', {t: (hex(h.func), h.prio, h.status(), h.espera) for t, h in nuc.hilos.items()})
    print('INTC:', {k: [hex(f) for f, _ in v] for k, v in nuc.intc.items()}, '| DMAC:', {k: [hex(f) for f, _ in v] for k, v in nuc.dmac.items()})
    print('DMA por canal:', dict(hw.kicks), '| sin soporte:', dict(hw.sin_soporte))
    print('escrituras de hardware:', {hex(k): v for k, v in hw.escrituras.most_common(10)})
    print('texto del juego:', ''.join(nuc.tty)[-800:])
    print('CD: busquedas', cd.busquedas[:12], '| sectores leidos', sum(cd.lecturas.values()))
    print('RPC:', dict(iop.rpc.most_common(24)))
    io = Counter()
    for k, v in c.io.items():
        io[k & 0xFFFFF000] += v
    print('E/S por pagina:', {hex(k): v for k, v in io.most_common(12)})
    print('llamadas:', [(hex(k), v) for k, v in c.llamadas.most_common(25)])


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--limite', type=int, default=20_000_000)
    ap.add_argument('--pulsa', default='')
    ap.add_argument('--desde')
    ap.add_argument('--guardar', default='')
    ap.add_argument('--parar', type=int)
    ap.add_argument('--captura')
    ap.add_argument('--captura-desde', type=int, default=0)
    ap.add_argument('--spx')
    ap.add_argument('--para-io')
    ap.add_argument('--cd', help='JSON con las lecturas de CD (vblank, lsn, sectores)')
    ap.add_argument('--tablero', action='store_true', help='guion entero hasta el tablero (vblank 7700) y su huella')
    ap.add_argument('--autoprueba', action='store_true', help='arranca hasta el vblank 600 y comprueba la huella')
    a = ap.parse_args()
    if a.autoprueba:
        a.pulsa, a.parar, a.limite = GUION_ES, 600, 100_000_000
    if a.tablero:
        a.pulsa, a.parar, a.limite = GUION_TABLERO, a.parar or TABLERO[0], 4_000_000_000
    arranque(a)
    sys.exit()
    arranque(ap.parse_args())

"""Adaptadores focales PAL para el intérprete existente; no ejecutan el GS."""
import struct


BOARD = 0x2350C0
TILES = 0x239690
BALLS = 0x27B740
CLOSING = 0x29FCA0
ALLIES = 0x2A3E30
INTRO = 0x2A5FB0
ASSET = 0x12D100
# Se pasan direcciones centinela para reconocer el banco en la frontera de dibujo.
YDW, AMA, AMT = 0x1C00000, 0x1D00000, 0x1D10000


def carga(orig, base, fn, *args, fase=1, fusion=False):
    c = base.cpu({0x2A81A0})
    eventos = []
    c.w8(0x6992DA, fase - 1)
    c.w8(0x6992DC, 4 if fusion else 0)
    if fn == INTRO:
        c.w32(c.gp - 0x7CCC, orig.CTX)  # objeto privado para los dos resultados de carga
    def afs(cpu):
        rid = cpu.r[4] & 0xFFFFFFFF
        eventos.append(dict(banco=rid >> 16, entrada=rid & 0xFFFF,
                            retorno=hex(cpu.r[31] & 0xFFFFFFFF)))
        cpu.r[2] = 0x1000000 + len(eventos) * 0x1000
    c.hooks[ASSET] = afs
    servicios = []
    for addr, name in ((0x12D3C0, 'estado de petición'), (0x147E60, 'tareas'),
                       (0x148300, 'tareas'), (0x12BD70, 'audio'), (0x1484A0, 'tareas'),
                       (0x13BC50, 'reserva'), (0x12CF00, 'E/S'), (0x12CE90, 'E/S'),
                       (0x29CFE0, 'limpiar tareas de cierre')):
        orig.service(c, addr, name, servicios)
    c.hooks[0x123C18] = orig.memset
    orig.invoke(c, fn, *args, limit=120_000)
    return dict(funcion=hex(fn), solicitudes=eventos, traza=orig.trace(c, servicios))


def montar(c, ydw, tablas):
    c.m[YDW:YDW + len(ydw)] = ydw
    c.w32(c.gp - 0x7748, YDW)  # iGpffff88b8
    for off in tablas:
        c.w32(YDW + off, YDW + c.r32(YDW + off))
    c.w32(0x698DCC, AMA)
    c.w32(0x698DB4, AMT)


def casillas(orig, base, ydw, reloj=37.5):
    """Mueve la cámara de prueba a cada centro: todos los T1, ningún supuesto de visibilidad simultánea."""
    c = base.cpu()
    montar(c, ydw, [0x24])
    n, tabla = c.r32(YDW + 0x20), c.r32(YDW + 0x24)
    orig.service(c, 0x148EF0, 'selección de banco', [])
    capas = []
    def emitir(cpu):
        a = [cpu.r[i] & 0xFFFFFFFF for i in range(4, 12)]
        capas.append(dict(anim=a[0], img=a[1], ama=a[3], amt=a[4], reloj=orig.ee.fv(cpu.f[12])))
    c.hooks[0x23B640] = emitir
    filas = []
    # Un solo registro real en la ranura lógica 1 por invocación. El consumidor salta T1[0].
    c.w32(YDW + 0x20, 2)
    c.w32(YDW + 0x24, YDW + 0x90000)
    for i in range(1, n):
        p = tabla + 52*i
        datos = c.m[p:p+52]
        c.m[YDW + 0x90034:YDW + 0x90068] = datos
        c.w32(0x699400, c.r32(p))
        c.w32(0x699404, c.r32(p + 4))
        struct.pack_into('<f', c.m, c.gp - 0x7720, reloj)
        capas.clear()
        orig.invoke(c, TILES)
        if not capas or any((q['ama'], q['amt']) != (AMA, AMT) for q in capas):
            raise ValueError(f'T1[{i}]: banco o emisión desconocidos: {capas}')
        filas.append(dict(casilla=i, tipo=c.r32(p + 0x10), condicion=c.r8(p + 0x19),
                          capas=[q.copy() for q in capas]))
    return filas, orig.trace(c)


def objetos(orig, base, ydw, reloj=37.5):
    """Ejecuta la selección de recursos; visibilidad de T8 real, bolas de prueba 1..7."""
    c = base.cpu()
    montar(c, ydw, [0x5C])
    n, tabla = c.r32(YDW + 0x58), c.r32(YDW + 0x5C)
    if n >= 128 or not YDW <= tabla <= YDW + len(ydw) - 4*n:
        raise ValueError('Tabla T8 fuera del contrato')
    orig.service(c, 0x273A90, 'posición de la casilla (no se compara)', [])
    draws = []
    def emitir(cpu):
        a = [cpu.r[i] & 0xFFFFFFFF for i in range(4, 11)]
        draws.append(dict(anim=a[0], img=a[1], ama=a[3], amt=a[4], reloj=orig.ee.fv(cpu.f[12])))
    c.hooks[0x23BAC0] = emitir
    filas = []
    struct.pack_into('<f', c.m, c.gp - 0x7720, reloj)
    for i in range(n):
        off = c.r32(tabla + i*4)
        if not 0 < off <= len(ydw) - 12:
            continue
        p = YDW + off
        c.w32(tabla + i*4, p)
        casilla, enterrado, visible, tipo = struct.unpack_from('<HBBB', c.m, p)
        if casilla == 0:
            continue
        c.m[0x69BC20:0x69BC20+n] = bytes(n)
        for num in range(1, 8) if tipo == 0 else (None,):
            # 0x27B740 solo pinta estado 1. Inyectado deliberadamente, no una partida.
            c.w8(0x69BC20 + i, 1 if tipo == 0 or (visible and not enterrado) else 0)
            c.m[c.gp-0x72C8:c.gp-0x72C2] = bytes([i, num or 0, 255, 0, 255, 0])
            draws.clear()
            orig.invoke(c, BALLS)
            if any((q['ama'], q['amt']) != (AMA, AMT) for q in draws):
                raise ValueError(f'T8[{i}]: banco desconocido: {draws}')
            filas.append(dict(registro=i, casilla=casilla, tipo=tipo, bola=num,
                              visibilidad_ydw=bool(visible and not enterrado), capas=list(draws)))
    return filas, orig.trace(c)

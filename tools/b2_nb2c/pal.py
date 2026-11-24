"""Sondas focales: cada caso parte de RAM privada y declara sus puentes."""
from b2_oraculo import original as o
from mips_perfil import tabla_ordenes

P = 0x69B7A0
T = o.TASK
INV, SKA = 0x699050, 0x6942A0
ADDR = {8: 0x295C10, 35: 0x294A00, 38: 0x294840, 39: 0x2947B0,
        41: 0x294660, 42: 0x294510, 43: 0x2944A0, 70: 0x293740}


def piece(n):
    return P + 0x24 * n


def order(cpu, op):
    found = tabla_ordenes(cpu)[op][0]
    if found != ADDR[op]:
        raise ValueError(f'orden {op}: manejador inesperado {found:#x}')
    return found


def setup(base, allowed=()):
    cpu = base.cpu(allowed)
    cpu.m[T:T + 0x100] = bytes(0x100)
    return cpu, []


def board(c):
    b = c.r32(0x3A45A8)
    t = c.r32(b + 0x24)
    count = c.r32(b + 0x20)
    if not 2 <= count <= 64 or not 0 < t < o.ee.RAM - count * 52:
        raise ValueError('T1 no disponible en el estado')
    return t, count


def dialogo(base, abierta, textos):
    c, ev = setup(base)
    estado = 2 if abierta else 0
    for addr, name in ((0x2460D0, 'pedir_recursos'), (0x270130, 'textos'),
                       (0x270140, 'mostrar'), (0x1485F0, 'fin'), (0x246190, 'restaurar')):
        o.service(c, addr, name, ev)
    c.w32(T + 0x34, textos[0])
    c.w32(T + 0x38, textos[1])
    states = []
    for ready, panel in [(0, estado), (0, estado), (2, 2)]:
        o.service(c, 0x26FD80, 'estado_panel', ev, panel)
        o.service(c, 0x12BC60, 'estado_recursos', ev, ready)
        o.invoke(c, order(c, 38), T)
        states.append(c.r32(T + 0x40))
        if any(e['servicio'] == 'fin' for e in ev):
            break
    return dict(textos=[e['argumentos'][:2] for e in ev if e['servicio'] == 'textos'],
                pasos=states, termina=any(e['servicio'] == 'fin' for e in ev)), o.trace(c, ev)


def combate(base, condicion, vida_r):
    c, ev = setup(base)
    t, _ = board(c)
    c.w8(t + 52 + 0x19, condicion)
    for n, vida, ki, barras in [(1, 2501, 321, 2), (2, vida_r, 654, 2)]:
        c.w16(piece(n) + 0x14, vida)
        c.w16(piece(n) + 0x16, ki)
        c.w8(piece(n) + 0x1F, barras)
    o.invoke(c, 0x279050, 1, 2, 1)
    return dict(vida=[c.r16(0x69932A), c.r16(0x69936A)],
                ki=[c.r16(0x69932C), c.r16(0x69936C)]), o.trace(c, ev)


def salida_combate(base, lado):
    c, ev = setup(base, {0x278D60, 0x279050})
    c.w32(c.gp - 0x7744, 1)  # actores del contacto: jugador y rival
    c.w32(c.gp - 0x7740, 2)
    c.w16(piece(1) + 10, 10)  # casilla distinta para cada iniciador
    c.w16(piece(2) + 10, 12)
    c.w32(T + 0x30, 0x14)
    for addr, name, result in ((0x27F510, 'T4_sin_guion', -1),
                               (0x27EEF0, 'preparar', 0), (0x27EDB0, 'ajustes', 0),
                               (0x27ECF0, 'ajustes2', 0), (0x147420, 'volumen', 0),
                               (0x147260, 'sonido', 0), (0x232E10, 'modo', 0),
                               (0x249FF0, 'escena', 0), (0x2460D0, 'recursos', 0),
                               (0x1485F0, 'fin', 0)):
        o.service(c, addr, name, ev, result & o.ee.M64)
    addr = 0x236420 if lado == 'jugador' else 0x235780
    o.invoke(c, addr, T)
    o.invoke(c, addr, T)
    prep, = [e['argumentos'] for e in ev if e['servicio'] == 'preparar']
    modos = [e['argumentos'][0] for e in ev if e['servicio'] == 'modo']
    return dict(jugador=prep[0], rival=prep[1], casilla=prep[2], modos=modos), o.trace(c, ev)


def premio(base, op, elegido, stock, maximo):
    c, ev = setup(base, {0x278DE0})
    c.w32(T + 0x34, 35)
    c.w32(T + 0x38, 1)
    ident, roster = 17, (1 << 0) | (1 << 34)  # T2 real: pieza 1 = Goku (0)
    c.w8(INV + ident, stock)
    c.w8(SKA + ident * 32 + 0xD, maximo)
    closed = [-2]

    def sorteo(cpu):
        ev.append(dict(servicio='sorteo_inyectado', direccion='0x2835f0',
                       argumentos=[cpu.r[4], cpu.r[5], cpu.r[6]]))
        cpu.r[2] = elegido

    def ventana(cpu):
        ev.append(dict(servicio='estado_ventana', direccion='0x2a42b0', argumentos=[closed[0]]))
        cpu.r[2] = closed[0] & o.ee.M64

    c.hooks.update({0x2835F0: sorteo, 0x2A42B0: ventana})
    for addr, name, result in ((0x245DD0, 'roster_inyectado', roster),
                               (0x2A44C0, 'abrir', 0), (0x2A4410, 'nueva', 0),
                               (0x2A43E0, 'cerrar', 0), (0x1485F0, 'fin', 0)):
        o.service(c, addr, name, ev, result)
    addr = order(c, op)
    o.invoke(c, addr, T)
    counts = [c.r8(INV + ident)]
    if elegido:
        o.invoke(c, addr, T)
        counts.append(c.r8(INV + ident))
        closed[0] = -1
        o.invoke(c, addr, T)
        counts.append(c.r8(INV + ident))
    args = ev[[e['servicio'] for e in ev].index('sorteo_inyectado')]['argumentos']
    return dict(grupo=args[0], mascara=args[1], stock=counts, stock_final=counts[-1],
                ventana=[e['argumentos'][0] for e in ev if e['servicio'] == 'abrir'],
                termina=any(e['servicio'] == 'fin' for e in ev)), o.trace(c, ev)


def dende(base, barras):
    c, ev = setup(base)
    c.w8(piece(1) + 1, barras)
    c.w16(piece(1) + 0x14, 2501)
    c.w8(piece(1) + 0x1F, 0)
    o.invoke(c, 0x279F60, 1)
    return dict(barras=c.r8(piece(1) + 1), vida_raw=c.r16(piece(1) + 0x14),
                banda_vida=c.r8(piece(1) + 0x1F)), o.trace(c, ev)


def popo(base, destino, llegada=5):
    c, ev = setup(base, {0x272FF0, 0x278E20, 0x272FB0,
                         0x27A370, 0x27A2B0, 0x278A30, 0x273950, 0x273210})
    tiles, ntiles = board(c)
    b = c.r32(0x3A45A8)
    n = c.r32(b + 0x28)
    if not 1 < n <= 32 or not 0 < destino < ntiles or not 0 < llegada < ntiles or llegada == destino:
        raise ValueError('fixture Popo incompatible')
    if c.r32(tiles + 52 * llegada + 16) != 3 or c.r32(tiles + 52 * llegada + 20) != destino:
        raise ValueError('T1 no contiene la ruta de alfombra declarada')
    for i in range(1, n):
        c.w16(piece(i) + 6, 0)
    c.w16(piece(1) + 6, 0x12)
    c.w16(piece(1) + 8, llegada)
    c.w16(piece(1) + 10, llegada - 1)
    c.w32(c.gp - 0x72D0, o.HEAP)  # posición visual: salida privada de FUN_00273950
    c.w32(c.gp - 0x772C, 1)       # uGpffff88d4 = actor activo
    c.w32(T + 0x30, 4)            # efecto ya iniciado: sondear término y recolocación
    o.service(c, 0x276FF0, 'seguimiento_camara', ev)
    c.w32(c.gp - 0x72D8, 1)
    o.invoke(c, 0x236830, T)
    if c.r32(T + 0x30) != 4 or c.r16(piece(1) + 8) != llegada:
        raise ValueError('Popo: se movió con el efecto ocupado')
    c.w32(c.gp - 0x72D8, 0)
    o.invoke(c, 0x236830, T)
    if c.r32(T + 0x30) != 5:
        raise ValueError('Popo: no entró en la espera tras recolocar')
    espera = 0
    while espera < 30 and c.r32(T + 0x30) == 5:
        o.invoke(c, 0x236830, T)
        espera += 1
    return dict(casilla=c.r16(piece(1) + 8), desde=c.r16(piece(1) + 10),
                slot=c.r8(piece(1) + 12), slot_anterior=c.r8(piece(1) + 13)), dict(
                o.trace(c, ev), espera_llamadas=espera, paso_final=c.r32(T + 0x30))


def kid(base, n, guiones):
    c, ev = setup(base, {0x276E10})
    addr = order(c, 35)
    o.service(c, 0x1485F0, 'fin', ev)
    c.w32(T + 0x34, n)
    inicial = o.signed(c.r16(piece(n) + 0x10), 16)
    out, inc = [inicial], []
    b = c.r32(0x3A45A8)
    t6 = c.r32(b + 0x4C)
    for g in guiones:
        if not 0 <= g < c.r32(b + 0x48):
            raise ValueError('T6 fuera de rango')
        ptr = c.r32(t6 + 4 * g)
        count = c.r32(ptr)
        if not 0 < count <= 256:
            raise ValueError('guion T6 inválido')
        starts = (ptr + 4 + 24*i for i in range(count))
        v, = [c.r32(p + 12) for p in starts if c.r32(p + 4) == 35 and c.r32(p + 8) == n]
        inc.append(o.signed(v))
        c.w32(T + 0x38, v)
        o.invoke(c, addr, T)
        out.append(o.signed(c.r16(piece(n) + 0x10), 16))
    return dict(ataque=out), dict(o.trace(c, ev), t6=dict(guiones=guiones, pieza=n,
                personaje=c.r16(piece(n) + 4), ataque_inicial=inicial, incrementos=inc))


def cases():
    """Entradas declaradas. Los bancos no reconstruyen la partida que las alcanza."""
    return {
        'dialogo/abre': (dialogo, dict(abierta=False, textos=[5, 9])),
        'dialogo/abierto': (dialogo, dict(abierta=True, textos=[5, 9])),
        **{f'combate/cond-{i}': (combate, dict(condicion=i, vida_r=1900)) for i in range(3)},
        'combate/coherente': (combate, dict(condicion=0, vida_r=2345)),
        **{f'combate/salida-{i}': (salida_combate, dict(lado=i)) for i in ('jugador', 'rival')},
        'premio/8': (premio, dict(op=8, elegido=17, stock=0, maximo=2)),
        'premio/70-tope': (premio, dict(op=70, elegido=17, stock=2, maximo=2)),
        'premio/70-vacio': (premio, dict(op=70, elegido=0, stock=0, maximo=2)),
        'dende/4': (dende, dict(barras=4)), 'dende/5': (dende, dict(barras=5)),
        'popo/5-a-24': (popo, dict(destino=24, llegada=5)),
        'kid/35': (kid, dict(n=17, guiones=[312, 313, 314])),
        **{f'cierre/{i}': (o.phase, dict(order=i, following=4, current=8)) for i in (41, 42, 43)},
    }

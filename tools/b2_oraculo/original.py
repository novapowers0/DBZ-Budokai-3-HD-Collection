"""B2 PAL: funciones del ELF sobre una copia aislada de un savestate.

No escribe el ELF, la tarjeta, el savestate ni archivos del juego. Los puentes
se declaran por caso; una llamada sin contrato, instruccion desconocida o E/S
invalidan el resultado. No se instala ninguna dependencia.
"""
import hashlib
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from mips_perfil import cargar, leer_elf, PERFILES  # noqa: E402
import ee  # noqa: E402

TASK, CTX, SAVE, LIBC = 0x1F00000, 0x1F01000, 0x1F02000, 0x1F04000
HEAP = 0x1F10000
EXPECTED_ELF_SHA256 = '30c362969ad64184df0b4a4ee498badb101265470883948f3131b6302ae3c797'
# Rangos exclusivos, incluyen las funciones auxiliares efectivamente admitidas.
CODE = ((0x11DA00, 0x11DAA0), (0x124EA8, 0x124F10),
        (0x12E4E0, 0x12E520), (0x245E70, 0x245EC0),
        (0x274040, 0x2740B0), (0x278B00, 0x278B30),
        (0x278D80, 0x278DE0), (0x2791A0, 0x279350),
        (0x27A090, 0x27A0D0), (0x2835F0, 0x2837C0),
        (0x2944A0, 0x294840), (0x29B120, 0x29B6C0),
        (0x2A1230, 0x2A1310))


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def signed(x, bits=32):
    x &= (1 << bits) - 1
    return x - (1 << bits) if x >> (bits - 1) else x


class Original:
    def __init__(self, state):
        self.path = Path(state).resolve()
        c, _ = cargar('b2', self.path, texto_elf=False)
        data, _, segments, gp = leer_elf(PERFILES['b2']['elf'])
        digest = hashlib.sha256(data).hexdigest()
        if digest != EXPECTED_ELF_SHA256:
            raise ValueError('ELF distinto del B2 PAL revisado; revisar direcciones y contratos')
        if gp != 0x3ABCF0:
            raise ValueError('El banco solo admite el ELF B2 PAL SLES-51839 revisado')
        code = []
        for start, end in CODE:
            matches = [(off + start - va) for off, va, size, _ in segments
                       if va <= start and end <= va + size]
            if len(matches) != 1:
                raise ValueError(f'Rango ELF ambiguo/no mapeado: {start:#x}')
            offset = matches[0]
            expected = data[offset:offset + end - start]
            if c.m[start:end] != expected:
                raise ValueError(f'Codigo del estado distinto del ELF: {start:#x}')
            code.append(dict(inicio=hex(start), fin_exclusivo=hex(end),
                             sha256=hashlib.sha256(expected).hexdigest()))
        self.mem = bytes(c.m)
        self.elf = data
        self.segments = segments
        self.gp = gp
        self.identity = dict(estado=str(self.path), estado_sha256=sha(self.path),
                             elf=str(PERFILES['b2']['elf']),
                             elf_sha256=digest,
                             gp=hex(gp), codigo_verificado=code)

    def cpu(self, allowed=()):
        parent = self
        class CheckedEe(ee.Ee):
            def decodificar(self, pc):
                offsets = [off + pc - va for off, va, size, _ in parent.segments
                           if va <= pc and pc + 4 <= va + size]
                if len(offsets) != 1:
                    raise ValueError(f'Instruccion fuera del ELF: {pc:#x}')
                word = parent.elf[offsets[0]:offsets[0] + 4]
                if self.m[pc:pc + 4] != word:
                    raise ValueError(f'Instruccion distinta del ELF: {pc:#x}')
                self.verificadas[pc] = word
                return super().decodificar(pc)
        c = CheckedEe(bytearray(self.mem), self.gp)
        c.verificadas = {}
        c.whitelist = set(allowed)
        c.estricto = True
        return c


def invoke(c, address, *args, limit=250_000):
    # Ee.call extiende TODOS los argumentos como i32. La mascara de capsulas
    # es u64: conservar tambien los bits de los personajes 32..35.
    for i, value in enumerate(args):
        c.r[4 + i] = value & ee.M64
    c.r[28], c.r[29], c.r[31] = c.gp, ee.PILA + 0xFF000, ee.RETORNO
    c.run(address, limit)
    if c.desconocidas or c.sin_puente or c.io:
        raise RuntimeError(f'Ejecucion incompleta: {c.desconocidas}, {c.sin_puente}, {c.io}')
    return c.r[2] & ee.M32


def service(c, addr, label, events, result=0):
    def hook(cpu):
        events.append(dict(servicio=label, direccion=hex(addr),
                           argumentos=[int(cpu.r[i] & ee.M32) for i in range(4, 7)]))
        cpu.r[2] = result
    c.hooks[addr] = hook


def memset(c):
    a, byte, n = (c.r[i] & ee.M32 for i in (4, 5, 6))
    if not 0 <= a <= a + n <= ee.RAM or n > 0x10000:
        raise ValueError('memset fuera del contrato del banco')
    c.m[a:a + n] = bytes([byte & 255]) * n
    c.r[2] = a


def trace(c, events=()):
    code = b''.join(k.to_bytes(4, 'little') + v for k, v in sorted(c.verificadas.items()))
    return dict(instrucciones=c.n, llamadas={hex(k): v for k, v in sorted(c.llamadas.items())},
                puentes=list(events), instrucciones_desconocidas=dict(c.desconocidas),
                llamadas_sin_puente=dict(c.sin_puente), io=dict(c.io),
                palabras_codigo_verificadas=len(c.verificadas), codigo_ejecutado_sha256=hashlib.sha256(code).hexdigest())


def lfsr(seed, steps=32):
    """Modelo independiente del azar de tablero 0x12E4E0; devuelve u32."""
    for _ in range(steps):
        seed = ((seed << 1) | (((seed >> 31) ^ (seed >> 4)) & 1)) & ee.M32
    return (seed - 1) & ee.M32, seed


def phase(base, order, following, current=3):
    c, events = base.cpu(), []
    for addr, label in ((0x2A7F50, 'cerrar_paneles'), (0x232E40, 'cerrar_escena'),
                        (0x238320, 'guardar_partida_NO_ejecutado'),
                        (0x270C40, 'abrir_transicion'), (0x270A50, 'sonido'),
                        (0x270920, 'estado_transicion_terminada'),
                        (0x270950, 'cerrar_transicion'), (0x232E50, 'fin_escena'),
                        (0x232E10, 'cambiar_modo'),
                        (0x246190, 'actor_salida')):
        service(c, addr, label, events)
    service(c, 0x24A120, 'crear_tarea_privada', events, result=TASK + 0x100)
    c.hooks[0x123C18] = memset
    c.m[TASK:TASK + 0x100] = bytes(0x100)
    c.w8(0x693A96, current)
    c.w32(TASK + 0x34, following)
    c.w32(TASK + 0x40, 0)
    fn = {41: 0x294660, 42: 0x294510, 43: 0x2944A0}[order]
    invoke(c, fn, TASK)
    if order != 43:
        invoke(c, fn, TASK)  # contrato explicito: transicion ya terminada
    modes = [e['argumentos'][0] for e in events if e['servicio'] == 'cambiar_modo']
    return dict(fase_byte=c.r8(0x693A96), modo=modes[-1],
                guardado_solicitado=any(e['direccion'] == '0x238320' for e in events)), trace(c, events)


def rivals(base, neighbors, seed):
    c = base.cpu({0x12E4E0})
    # Reusar el YDW vivo; solo reemplazar las cuatro vecinas de la casilla 1.
    board = c.r32(0x3A45A8)
    tiles = c.r32(board + 0x24)
    if not 0 < tiles < ee.RAM - 0x68:
        raise ValueError('Tabla de casillas del estado no disponible')
    for i, value in enumerate(neighbors):
        c.w16(tiles + 0x34 + 8 + 2 * i, value)
    c.w16(TASK + 8, 1)
    c.w32(c.gp - 0x7FE8, seed)
    direction = invoke(c, 0x274040, TASK)
    step = neighbors[direction] if direction < 4 else -1
    return dict(paso=step, direccion=direction, semilla_final=c.r32(c.gp - 0x7FE8)), trace(c)


def rival_order(base, pieces=None):
    """Orden de actores: inicializar, recargar acciones, elegir primero y consumir.

    No simula el movimiento, el combate ni sus disparadores; las acciones se
    completan expresamente en el contrato de esta prueba.
    """
    c = base.cpu()
    c.hooks[0x123C18] = memset
    if pieces is not None:
        c.w32(c.gp - 0x7748, CTX)
        c.w32(CTX + 0x28, len(pieces) + 1)
        c.w32(CTX + 0x2C, CTX + 0x100)
        c.w32(CTX + 0x20, 2)
        c.m[CTX + 0x100:CTX + 0x100 + 32 * 40] = bytes(32 * 40)
        for i, row in enumerate(pieces, 1):
            c.w8(CTX + 0x100 + i * 40, row['personaje'])
            c.w8(CTX + 0x102 + i * 40, row['bando'])
            c.w16(CTX + 0x114 + i * 40, 1)
    inp = ally_input(c)
    invoke(c, 0x2791A0)
    # Bloque visual de actores desviado a memoria privada para la recarga.
    c.w32(c.gp - 0x72D0, HEAP)
    for i, row in enumerate(inp, 1):
        row['acciones_turno'] = pieces[i - 1].get('acciones_turno', 1) if pieces is not None else c.r8(0x69B7A0 + i * 36 + 3)
        c.w8(0x69B7A0 + i * 36 + 3, row['acciones_turno'])
    invoke(c, 0x278EE0)
    selected = []
    for _ in range(128):
        index = invoke(c, 0x279E30)
        if index == 0:
            break
        selected.append(index)
        invoke(c, 0x27A180, index)
    else:
        raise ValueError('Orden rival no termina en 128 acciones')
    return inp, selected, trace(c)


def ally_input(c):
    board = c.r32(c.gp - 0x7748)
    n, table = c.r32(board + 0x28), c.r32(board + 0x2C)
    if not 1 <= n <= 32 or not 0 < table < ee.RAM - 32 * 40:
        raise ValueError('T2 no disponible')
    ntiles = c.r32(board + 0x20)
    return [dict(indice=i, personaje=c.r8(table + i * 40), bando=c.r8(table + i * 40 + 2),
                 casilla=c.r16(table + i * 40 + 0x14),
                 casilla_valida=0 < c.r16(table + i * 40 + 0x14) < ntiles)
            for i in range(1, n)]


def allies(base, synthetic=None):
    c = base.cpu({0x2791A0, 0x278D80, 0x245E70, 0x278B00, 0x27A090})
    c.hooks[0x123C18] = memset
    if synthetic is not None:
        board, table = CTX, CTX + 0x100
        c.w32(c.gp - 0x7748, board)
        c.w32(board + 0x28, len(synthetic) + 1)
        c.w32(board + 0x2C, table)
        c.w32(board + 0x20, 2)
        c.m[table:table + 32 * 40] = bytes(32 * 40)
        c.m[0x699050:0x699050 + 520] = bytes(520)
        for i, row in enumerate(synthetic, 1):
            c.w8(table + i * 40, row['personaje'])
            c.w8(table + i * 40 + 2, row.get('bando', 0))
            c.w16(table + i * 40 + 0x14, 1)
            capsule = c.r16(0x377E40 + row['personaje'] * 2)
            if capsule:
                c.w8(0x699050 + capsule, row.get('abierto', 1))
    inp = ally_input(c)
    # Preparacion, no lista consultada despues de haber elegido aliados.
    invoke(c, 0x2791A0)
    invoke(c, 0x2A1230)
    n = c.r32(c.gp - 0x70D0)
    if n > 14:
        raise ValueError('Cantidad de aliados fuera del contrato')
    rows = [c.r16(0x69D030 + 4 * i + 2) for i in range(n)]
    return inp, rows, trace(c)


def capsules(base, group, mask, seed, records=None, inventory=None):
    c, events = base.cpu({0x12E4E0}), []
    if records is None and c.m[0x694280:0x694284] != b'#SKA':
        raise ValueError('El savestate no contiene la tabla de capsulas #SKA')
    if records is not None:
        # grupo 254/maximo 0: registros de relleno que no participan.
        c.m[0x6942A0:0x6942A0 + 520 * 32] = bytes(520 * 32)
        for i in range(520):
            c.w8(0x6942A0 + i * 32 + 0x14, 254)
        for row in records:
            at = 0x6942A0 + row['id'] * 32
            c.w64(at, row.get('mascara', 0))
            c.w8(at + 0x14, row.get('grupo', group))
            c.w8(at + 0xD, row.get('maximo', 1))
    if inventory is not None:
        c.m[0x699050:0x699050 + 520] = bytes(520)
        for key, value in inventory.items():
            c.w8(0x699050 + int(key), value)
    allocation = [HEAP]
    def alloc(cpu):
        size = cpu.r[4] & ee.M32
        if size != 0x820 or allocation[0] >= HEAP + 0x2000:
            raise ValueError('Reserva inesperada en capsulas')
        cpu.r[2] = allocation[0]
        allocation[0] += 0x1000
        events.append(dict(servicio='reserva_0x820', direccion='0x13bc50'))
    c.hooks[0x13BC50] = alloc
    service(c, 0x13BE30, 'liberar', events)
    c.w32(c.gp - 0x7FE8, seed)
    # La entrada compartida con el modelo se toma ANTES de ejecutar el ELF.
    inp = [(c.r64(0x6942A0 + i * 32), c.r8(0x6942A0 + i * 32 + 0x14),
            c.r8(0x6942A0 + i * 32 + 0xD), c.r8(0x699050 + i)) for i in range(520)]
    result = invoke(c, 0x2835F0, group, mask, 0x699050)
    evidence = trace(c, events)
    evidence['estado_rng_final'] = c.r32(c.gp - 0x7FE8)
    return inp, result, evidence


WISH_TABLES = ((0x37A560, 6, 0), (0x37A220, 29, 11), (0x37A4E0, 5, 6))


def wishes(base, flags, seed):
    c, events = base.cpu({0x124EA8, 0x11DA00}), []
    c.m[TASK:TASK + 0x100] = bytes(0x100)
    c.m[CTX:CTX + 0x100] = bytes(0x100)
    c.m[SAVE:SAVE + 0x1000] = bytes(0x1000)
    c.w32(c.gp - 0x7D08, SAVE)
    c.w32(c.gp - 0x720C, CTX)
    c.w32(0x33CB88, LIBC)
    c.w64(LIBC + 0xA8, seed)
    for flag in flags:
        c.w8(SAVE + 0x984 + flag, 1)
    # Estado tras 0x29B6C0; el sembrado de libc es entrada controlada.
    for off, val in ((0x48, 0), (0x4C, 6), (0x50, 0x69C830), (0x54, c.gp - 0x71EC)):
        c.w32(TASK + off, val)
    for _ in range(3):
        invoke(c, 0x29B4B0, TASK)  # relleno y filtros ORIGINALES, sin traducirlos
    if c.r32(TASK + 0x1C) != 0x29B120:
        raise ValueError('El productor no alcanzo el sorteo')
    pools = dict(fijas=c.r32(TASK + 0x44), candidatas_538=c.r32(TASK + 0x40),
                 candidatas_539=c.r32(TASK + 0x3C), elegibles_538=c.r32(TASK + 0x34),
                 elegibles_539=c.r32(TASK + 0x30))
    service(c, 0x1485F0, 'fin_tarea', events)
    invoke(c, 0x29B120, TASK)
    count = c.r32(c.gp - 0x71E8)
    if count > 3:
        raise ValueError('Sorteo fuera de contrato')
    lookup = {start + 24 * i: first + i for start, n, first in WISH_TABLES for i in range(n)}
    rows = [lookup[c.r32(CTX + 0x44 + i * 4)] for i in range(count)]
    evidence = trace(c, events)
    evidence['estado_rng_final_u64'] = c.r64(LIBC + 0xA8)
    return rows, pools, evidence

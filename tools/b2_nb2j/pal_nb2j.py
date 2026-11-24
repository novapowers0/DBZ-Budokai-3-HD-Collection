"""Sondas PAL: amplía el banco NB2C, con RAM privada y servicios explícitos."""
import struct

from pal import board, o, order, piece, setup, T


def dende(base, barras, hp, condicion, nuevo_hp=1900):
    c, ev = setup(base, {0x279050})
    t, _ = board(c)
    c.w8(t + 52 + 0x19, condicion)
    for n in (1, 2):
        c.w8(piece(n) + 1, barras)
        c.w16(piece(n) + 0x14, hp)
        c.w8(piece(n) + 0x1F, 1)  # banda anterior deliberadamente distinta para 2501
    result = []
    for n in (1, 2):
        o.invoke(c, 0x279F60, n)
        o.invoke(c, 0x279050, 1, 2, 1)
        result.append(dict(barras=c.r8(piece(n) + 1), vida_raw=c.r16(piece(n) + 0x14),
                           banda_vida=c.r8(piece(n) + 0x1F),
                           hp_peticion=c.r16(0x69932A if n == 1 else 0x69936A)))
        o.invoke(c, 0x276CF0, n, nuevo_hp)  # escritor PAL de la orden 54
        o.invoke(c, 0x279050, 1, 2, 1)
        result[-1]['hp_tras_54'] = c.r16(0x69932A if n == 1 else 0x69936A)
    return dict(jugador=result[0], rival=result[1], ee=o.trace(c, ev))


def combate(base, rival_mueve, origen, destino, condicion, antes):
    allowed = {0x278D60, 0x279050, 0x27F510, 0x278DE0, 0x278260, 0x277890}
    c, ev = setup(base, allowed)
    t, count = board(c)
    if not (0 < origen < count and 0 < destino < count and origen != destino):
        raise ValueError('casillas inválidas para el estado PAL')
    b = c.r32(0x3A45A8)
    c.w32(b + 0x28, 3)       # nulo + dos actores de prueba
    c.w32(b + 0x38, 1)       # T4 sin filas de usuario; se ejecuta la búsqueda real
    c.w32(c.gp - 0x72D0, o.HEAP)
    c.w8(t + 52 * origen + 0x19, antes)
    c.w8(t + 52 * destino + 0x19, condicion)
    actor, espera = (2, 1) if rival_mueve else (1, 2)
    for n in (1, 2):
        c.w16(piece(n) + 6, (0x12 if n == 1 else 0x22))
        c.w16(piece(n) + 8, origen if n == actor else destino)
        c.w16(piece(n) + 10, origen if n == actor else destino)
        c.w8(piece(n) + 12, 0)
        c.w16(piece(n) + 0x14, 2500)
        c.w8(piece(n) + 0x1F, 2)
        c.w16(piece(n) + 0x16, 300)
    o.invoke(c, 0x277F40, actor, destino, 0)  # escribe +0x0A con la rutina PAL
    encontrado = o.invoke(c, 0x278260, actor)
    if encontrado != espera:
        raise ValueError(f'PAL no encontró el otro actor en el destino: {encontrado}')
    if rival_mueve:
        # 0x235C30 mira +8 tras la llegada. Inyectamos esa finalización visual,
        # sin cambiar +0A: el despacho que sigue es código original.
        c.w16(piece(actor) + 8, destino)
    for n in (1, 2):
        o.invoke(c, 0x277890, n)
    c.w32(c.gp - 0x7744, 1)
    c.w32(c.gp - 0x7740, 2)
    c.w32(T + 0x30, 0x14)
    for addr, label in ((0x27EEF0, 'preparar'), (0x27EDB0, 'ajustes'),
                        (0x27ECF0, 'ajustes2'), (0x147420, 'volumen'), (0x147260, 'sonido'),
                        (0x232E10, 'modo'), (0x249FF0, 'escena'), (0x2460D0, 'recursos'),
                        (0x1485F0, 'fin')):
        o.service(c, addr, label, ev)
    for _ in range(2):
        o.invoke(c, 0x235780 if rival_mueve else 0x236420, T)
    prep, = [e['argumentos'] for e in ev if e['servicio'] == 'preparar']
    return dict(fichas=prep[:2], casilla=prep[2], vida=[c.r16(0x69932A), c.r16(0x69936A)],
                ki=[c.r16(0x69932C), c.r16(0x69936C)],
                pide_combate=any(e['servicio'] == 'modo' and e['argumentos'][0] == 7 for e in ev),
                destinos=[c.r16(piece(n) + 10) for n in (1, 2)], ee=o.trace(c, ev))


def dialogo(base, abierta, native=None):
    panel, tareas, script, args = o.CTX, o.HEAP, o.SAVE, o.LIBC
    allowed = {0x26FD80, 0x270130, 0x270140, 0x26F280, 0x26EFC0, 0x26ED50, 0x26EB00}
    c, ev = setup(base, allowed)
    c.w32(0x3A49CC, panel)
    c.w32(c.gp - 0x7324, panel)
    c.m[panel:panel+0x80] = bytes(0x80)
    c.w32(c.gp - 0x7994, int.from_bytes(struct.pack('<f', 1), 'little'))
    for addr, label, result in ((0x2460D0, 'pedir_recursos', 0), (0x246190, 'restaurar', 0),
                                (0x1485F0, 'fin_tarea', 0), (0x245C00, 'duracion_clip_fixture', 2),
                                (0x23B640, 'dibujo', 0), (0x148650, 'limpiar_tareas', 0)):
        o.service(c, addr, label, ev, result)
    ready = [0]
    def recursos(cpu):
        ev.append(dict(servicio='espera_recursos', direccion='0x12bc60', resultado=ready[0]))
        cpu.r[2] = ready[0]

    c.hooks[0x12BC60] = recursos
    # T6 mínimo: 38 en paso 1 y un marcador sin efectos (35) en paso 2.
    # El ejecutable original monta y lee el guion; solo se puentean alta y copia de operandos.
    b = c.r32(0x3A45A8)
    c.w32(b + 0x4C, args)
    c.w32(args, script)
    words = (2, 1, 38, 5, 9, 0, 0, 2, 35, 1, 10, 0, 0, 3, 0)
    if native:
        words = (len(native), *(v for row in native for v in row), native[-1][0]+1, 0)
    for i, v in enumerate(words):
        c.w32(script+4*i, v)
    lanzadas = []

    def alta(cpu):
        lanzadas.append(cpu.r[4])
        cpu.r[2] = tareas + 0x100 * len(lanzadas)
        cpu.m[cpu.r[2]:cpu.r[2]+0x100] = bytes(0x100)

    def copia(cpu):
        dst, src, num = cpu.r[4:7]
        if num != 16 or not tareas <= dst < tareas + 0x600 or not script <= src <= script + len(words)*4 - num:
            raise ValueError('copia PAL fuera del contrato')
        cpu.m[dst:dst+num] = cpu.m[src:src+num]

    c.hooks.update({0x1484A0: alta, 0x123A60: copia})
    o.invoke(c, 0x2960C0, 0)
    o.invoke(c, 0x295F20)
    if lanzadas != [order(c, 38)]:
        raise ValueError(f'no se despachó el primer 38: {lanzadas}')
    frames = []

    def tick(label, advance=False, run_order=True, task_n=1, op=38):
        if advance:
            addr = c.r32(panel+0x1C)
            o.invoke(c, addr, panel)
        current = tareas + 0x100 * task_n
        if run_order:
            o.invoke(c, order(c, op), current)
        o.invoke(c, 0x295F20)
        frames.append(dict(hito=label, paso=c.r32(current+0x40), panel=o.invoke(c, 0x26FD80),
                           textos=[o.signed(c.r32(panel+off)) for off in (0x50, 0x54)],
                           fin=any(e['servicio']=='fin_tarea' for e in ev), siguiente=len(lanzadas)>1,
                           lanzadas=len(lanzadas)))

    c.w32(panel+0x1C, 0x26ED50 if abierta else 0x26F280)
    c.w32(panel+0x3C, 2 if abierta else 0)
    if abierta:
        for off, v in ((0x34, 0x40000000), (0x50, 1), (0x54, 2)):
            c.w32(panel+off, v)
        tick('orden_con_panel_en_reposo')
        tick('retirada', True)
        tick('fin_retirada', True)
        tick('oculto', True)
    else:
        tick('pide_recursos')
        tick('recursos_no_listos')
        ready[0] = 2
        tick('recursos_listos')
        tick('inicia_entrada', True)
        tick('entra', True)
        tick('reposo', True)
        tick('mismo_panel_sin_pulsar', True, False)
        if native:
            if lanzadas != [order(c, 38), order(c, 39)]:
                raise ValueError('T6: 39 no recibió el control con el panel abierto')
            c.w32(0x693738, 0)
            c.w16(0x3B43AE, 0)
            tick('arma_39', True, task_n=2, op=39)
            tick('39_sin_cruz', True, task_n=2, op=39)
            c.w16(0x3B43AE, 0x4000)
            tick('cruz_despacha_38', True, task_n=2, op=39)
            c.w16(0x3B43AE, 0)
            if lanzadas != [order(c, 38), order(c, 39), order(c, 38)]:
                raise ValueError('T6: segundo 38 no despachado por 39')
            tick('segundo_38_con_panel_abierto', task_n=3)
            tick('retirada_del_primero', True, task_n=3)
            tick('oculto_tras_segundo_38', True, task_n=3)
    return dict(frames=frames, lanzadas=list(map(hex, lanzadas)), ee=o.trace(c, ev))


def secuencia_301(base):
    """Extrae el tramo literal y comprueba el despacho con la herramienta existente."""
    from mips_perfil import despacho_b2
    c = base.cpu()
    des = despacho_b2(c, 301)['ordenes']
    i, = [i for i in range(len(des)-2) if [r['orden'] for r in des[i:i+3]] == [38,39,38]]
    tramo = des[i:i+3]
    # El prefijo de cámara/SPX queda fuera del banco. Solo se rebasa el reloj del guion.
    anchor = c.r32(int(tramo[0]['sitio'], 16))
    rows = [[c.r32(int(r['sitio'],16))-anchor+1, r['orden'], *r['argumentos']] for r in tramo]
    if not rows[0][0] < rows[1][0] < rows[2][0]:
        raise ValueError('tramo 301 no secuencial')
    return dict(id=301, original=tramo, filas_rebasadas=rows)

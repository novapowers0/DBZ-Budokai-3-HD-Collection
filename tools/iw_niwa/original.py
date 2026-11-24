"""Sondas de Dragon Mission sobre el ELF PAL: sólo lógica de frontera, sin juego."""
import hashlib
import struct

import mapa_banco as mb
import mapa_espejo as me
import mapa_recompensas as premios

ELF_HASH = 'afe8c7b99f31e635750a17618ae8248d1a64fe3ad0669996990abdf33e1819f4'
NQ, CMD, ARG = 0x5D92A0, 0x5D9220, 0x5D9260
CFG = 0x11A3000
POSE, RECARGA = 0x2F9100, 0x2F9060


def sha(data):
    return hashlib.sha256(data).hexdigest()


def paso(cpu, valor):
    cpu.w32(mb.G_PASO, struct.unpack('<I', struct.pack('<f', valor))[0])


def cola(c):
    n = c.r32(NQ)
    if n > 16:
        raise ValueError(f'cola inesperada: {n}')
    return [[c.r32(CMD + 4 * j), c.r32(ARG + 4 * j)] for j in range(n)]


def retorno(s, delta, config=0):
    c, obj = s.c, cobj(s)
    paso(c, delta)
    c.w32(0x49CBA4, CFG)  # GameState_GetConfigBlock lo lee de aquí
    c.w32(CFG + 0x48, config & 0xFFFFFFFF)
    c.w32(obj + 0x34, 0)
    c.w32(NQ, 1)  # el despachador ya consumió los comandos anteriores a 4
    s.ev.clear()
    c.call(0x2F9200)
    estadios = []
    for i in range(1, 501):
        handler = c.r32(obj + 0x1C)
        if handler not in (POSE, RECARGA):
            raise ValueError(f'callback de vuelta inesperado: {handler:#x}')
        c.call(handler, obj)
        if handler == POSE and c.r32(obj + 0x1C) == RECARGA:
            if c.r32(obj + 0x34) != 0:
                raise ValueError('el callback no reinició el reloj')
            estadios.append({'evento': 'pose', 'tick': i,
                             'anim': c.r32(s.M + 0x1D8 + 0x48)})
        if c.r32(NQ) == 0:
            return [*estadios, {'evento': 'recarga', 'tick': i}]
    raise ValueError('el comando 4 no terminó')


def cobj(s):
    return s.c.r32(s.M)


def traza(delta=1.0, con_mini=True, afs=None, dificultad=2, rango=2, saldo=0):
    digest = sha(me._elf())
    if digest != ELF_HASH:
        raise ValueError(f'ELF PAL incompatible: {digest}')
    afs = afs or me.B.ELF.parent / 'USR' / 'DATA_CMN.AFS'
    dmd = me.dmd_de(me.entrada_afs(1093, afs))
    scd = me.entrada_afs(1127, afs)
    if dmd[:4] != b'#DMD' or scd[:4] != b'#SCD':
        raise ValueError('faltan DMD/SCD nativos')
    hechas = (*range(0x7E2, 0x7EC), *((0x7D0,) if con_mini else ()))
    s = me.Espejo(goku=(890, 610), hechas=hechas, dmd=dmd, scd=scd)
    c, m, obj = s.c, s.M, cobj(s)
    paso(c, delta)
    eventos, cuadros, servicios = [], [], []

    def evento(nombre, **campos):
        eventos.append(dict(evento=nombre, **campos))

    def stub(addr, salida=0, efecto=None):
        def llamada(cpu):
            args = tuple(cpu.r[j] & 0xFFFFFFFF for j in range(4, 8))
            servicios.append((addr, args))
            if efecto:
                efecto(cpu, args)
            cpu.r[2] = salida
        c.hooks[addr] = llamada

    # Fronteras gráficas/archivos/loader. El envío del registro se captura en la llamada real.
    for addr in (0x300890, 0x14C960, 0x125350, 0x1BA4F0, 0x14CB00,
                 0x1B18E0, 0x2C4DF0, 0x2FB4E0, 0x2C4BA0, 0x285D50,
                 0x2ADD80):
        stub(addr)
    registro = []
    stub(0x2ABA20, efecto=lambda cpu, a: registro.append(struct.unpack_from('<iiih', cpu.m, a[0])))
    # El loader de escena no se ejecuta; escribe un parámetro válido de prueba para la transición.
    stub(0x289C20, salida=1, efecto=lambda cpu, a: cpu.w32(a[2], 0))
    aparicion = s.aparecer()
    nodo = next(s.ctx + slot * 0x30 for slot, _, fila, clave, _ in s.nodos() if fila == 6 and clave == 0x7EC)
    byte = c.r8(me.ESTADO + s.clave_idx(0x7EC))
    en_cola = any(c.r32(m + 0x3A0 + 4*j) == nodo for j in range(c.r32(m + 0x4A0)))
    evento('mapa', saga=0, cuadros_aparicion=aparicion, capsula=byte, en_cola=en_cola)
    if en_cola:
        c.call(0x31B960, nodo, 0, 0)  # final del desbloqueo; no se mide su duración
        evento('desbloqueo', fila=6, flags=c.r32(nodo + 4))
    c.w32(NQ, 0)
    c.w32(m + 0x334, 0xBF800000)  # globo inicialmente cerrado
    c.w32(m + 0x338, 0xFFFFFFFF)
    for i, (lx, pulsa) in enumerate(((100, 0), (0, 0), (0, 0x4000)), 1):
        x, y, pick = s.cuadro(mant=pulsa, pulsa=pulsa, lx=lx)
        cuadros.append(dict(i=i, in_lx=lx, in_pulsa=pulsa, x=round(x, 4), y=round(y, 4), flags=c.r32(nodo + 4),
                            fila=c.r16(pick) if pick else -1, reloj=round(s.f(m + 0x334), 4),
                            fila_globo=c.r32(m + 0x338) if c.r32(m + 0x338) != 0xFFFFFFFF else -1))
        if i == 1:
            evento('movimiento', x=cuadros[-1]['x'], y=cuadros[-1]['y'])
        if i == 2 and c.r32(m + 0x338) == 6:
            evento('globo', fila=6, tick=i, reloj=cuadros[-1]['reloj'])
    if cola(c):
        stub(0x11DEF8)
        if cola(c) != [[9, 0]] or c.r32(m + 0x1C) != nodo:
            raise ValueError('la Cruz no eligió la ficha esperada')
        evento('ficha', mision=c.r32(m + 0x10) + 1, tipo=c.r32(m + 0x14), fila=6)
        c.call(0x2FCBC0, obj)
        c.w32(m + 0x370, dificultad)
        c.call(c.r32(obj + 0x1C), obj)  # TitleScripts: fija panel inicial y espera
        panel = c.r32(m + 0x358)
        count = 0
        while c.r32(obj + 0x1C) == 0x2FC2E0 and count < 100:
            c.call(0x2FC2E0, obj)
            count += 1
        if c.r32(obj + 0x1C) != 0x2FBD40:
            raise ValueError('ficha no llega a aceptar mando')
        evento('ficha_lista', panel=panel, tick=count)
        # 0x2FBD40 cambia foco izquierda→derecha; abajo dos veces hasta MISSION START.
        ficha_pasos = []
        for pulsa, direccion in ((0x4000, 0), (0, 0x40), (0, 0x40), (0x4000, 0)):
            s.pad(pulsa=pulsa)
            c.w16(mb.PAD + 6, direccion)
            c.call(0x2FBD40, obj)
            ficha_pasos.append([c.r32(m + o) for o in (0x358, 0x35C, 0x360, 0x36C)])
        if len(registro) != 1 or c.r32(obj + 0x1C) != 0:
            raise ValueError(f'no se envió un registro de combate: {registro}; ficha={ficha_pasos}')
        tipo, idx, clave, dif = registro[0]
        evento('entrada', mision=idx + 1, tipo=tipo, clave=clave, dificultad=dif)

        # Combate fuera de alcance: se inyectan resultado/rango en el bloque real.
        # ApplyResultRewards usa la misma parcela para variantes de moderación,
        # pero la misión y su byte vienen de la selección real, no del port.
        zeni = premios.Banco(premios.REG['eu']).zeni(idx + 1, dif, 0, rango)
        c.w32(0x49CBA4, CFG)
        for off, v in ((0, tipo), (4, idx), (12, dif), (0x164, rango), (0x168, zeni)):
            c.w32(CFG + off, v)
        c.w32(0x5C154C, saldo)
        c.w32(0x5C1550, saldo)
        c.call(0x28A020)
        evento('resultado', mision=idx + 1, rango=rango, zeni=zeni, saldo=c.r32(0x5C154C))
        c.w32(NQ, 0)
        c.w32(0x5C0F54, 2)  # fase de salida de recompensa, no sus sprites ni la tarjeta de memoria
        c.call(0x285F30, obj)
        evento('vuelta', cola=cola(c))
        eventos.extend(retorno(s, delta))
        evento('iconos', cuadros_aparicion=s.aparecer(de_vuelta=True))

    return dict(elf=digest, dmd=sha(dmd), scd=sha(scd), paso=delta, eventos=eventos, cuadros=cuadros,
                servicios=sorted({hex(addr) for addr, _ in servicios}))

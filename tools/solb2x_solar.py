"""Lectura acotada de la tarea solar PAL; no escribe ni fuerza su creación."""
import math
import struct


def comprobar_par_capas(con, sin, guest_igual):
    """Valida la atribución de una ablación UI, sin afirmar ejecución o igualdad PAL."""
    if not guest_igual or con['guest_publicado'] != sin['guest_publicado']:
        raise ValueError('el par solar no conserva el mismo guest')
    if con['sin_solar'] or not sin['sin_solar']:
        raise ValueError('el par solar no contiene las dos condiciones declaradas')
    if con['quads_emitidos'] != 5 or sin['quads_emitidos'] != 0:
        raise ValueError('el par solar requiere cinco quads frente a ninguno')
    if len(con['quads']) != 5 or con['quads'] != sin['quads'] or con['guest_image'] != sin['guest_image']:
        raise ValueError('el par solar cambia geometría, color, UV o viewport')
    if sin['ui_frame'] <= con['ui_frame']:
        raise ValueError('la ablación solar no sigue a la observación inicial')
    return dict(guest_publicado=con['guest_publicado'], quads_emitidos=[5, 0],
                ui_frames=[con['ui_frame'], sin['ui_frame']], aceptacion_pal=False)


def camara_solar_original(muestra):
    """Recupera ojo/frente de la matriz dibujada, no de la cámara del paso siguiente.

    Solo admite vistas rígidas sin balanceo. Es un fixture explícito, no una
    aceptación de cámara nativa ni una reconstrucción de campos ausentes.
    """
    if muestra['evento'] or muestra['stage'] != 0 or muestra['zona'] != 0:
        raise ValueError('La referencia requiere Llanura zona0 fuera de transición')
    solar = muestra['solar']
    m = solar['matriz']
    if len(m) != 16 or not all(math.isfinite(v) for v in m):
        raise ValueError('Matriz PAL incompleta o no finita')
    if any(abs(m[i]-v) > .0001 for i, v in ((3,0), (7,0), (11,0), (15,1))):
        raise ValueError('La referencia no es una vista afín')
    for i in range(3):
        for j in range(3):
            if abs(sum(m[4*k+i]*m[4*k+j] for k in range(3))-(i == j)) > .001:
                raise ValueError('La referencia no es una vista rígida')
    if max(abs(m[i]) for i in (1,4,6,9)) > .0001 or abs(m[5]+1) > .0001:
        raise ValueError('La sonda aún no admite inclinación o balanceo PAL')
    foco = solar['proyeccion'][15]
    if not math.isfinite(foco) or foco <= 0:
        raise ValueError('Foco PAL inválido')
    ojo = [-sum(m[4*j+i]*m[12+i] for i in range(3)) for j in range(3)]
    objetivo = [ojo[j]+100*m[4*j+2] for j in range(3)]
    return ojo+objetivo+[0., 2*math.atan(240/foco)]


def posiciones_solar():
    """Preparación terrestre común: cámara nativa, ningún estado solar escrito."""
    #1CD000 elige el semiplano por el producto cruzado con la vista ANTERIOR.
    #Teletransportar el centro a(100,100) invierte esa elección(094351/094559).
    #Conservar el centro original y separar luchadores, sin escribir la cámara.
    return ((-42.426406871, 0., 42.426406871), (42.426406871, 0., -42.426406871))


def solar_original(leer):
    def datos(p, n):
        if not 0x10000 <= p <= 0x2000000-n:
            raise ValueError(f'Rango solar PAL inválido: {p:X}+{n:X}')
        b = leer(p, n)
        if len(b) != n:
            raise ValueError('Lectura solar PAL incompleta')
        return b

    def fs(p, n):
        return list(struct.unpack('<'+'f'*n, datos(p, n*4)))

    tabla = struct.unpack('<256I', datos(0x3dce50, 1024))
    tareas = []
    for p in tabla:
        if not p:
            continue
        cab = struct.unpack('<12I', datos(p, 48))
        if cab[5:7] != (0x1abcd0, 0x1ac3e0):
            continue
        b = datos(p+48, 48)
        tareas.append(dict(direccion=p, prioridad=cab[7], flags=cab[8],
                           estado=list(struct.unpack('<11f', b[:44])),
                           banco=struct.unpack_from('<I', b, 44)[0]))
    return dict(tareas=tareas, matriz=fs(0x37bb10, 16),
                proyeccion=fs(0x3b28b0, 16), viewport=fs(0x3a4390, 2))


def solar_hd(leer, mgr):
    """Lee solo tareas propias B2SOL, también después del fin de VM."""
    def datos(p, n):
        b = leer(p, n)
        if len(b) != n:
            raise ValueError('Lectura solar HD incompleta')
        return b

    tabla = struct.unpack('>256I', datos(0x82398940, 1024))
    tareas = []
    for i, p in enumerate(tabla):
        if not p:
            continue
        if p != 0x82398d40+176*i:
            raise ValueError('Tabla de tareas HD fuera del pool/indexación original')
        b = datos(p, 176)
        if struct.unpack_from('>I', b, 172)[0] != 0x4232534f:
            continue
        cab = struct.unpack_from('>12I', b)
        if cab[5:7] != (0x820c8068, 0x820c7c68):
            raise ValueError('Marca solar propia con callbacks diferentes')
        tareas.append(dict(direccion=p, prioridad=cab[7], flags=cab[8],
                           estado=list(struct.unpack_from('>11f', b, 48)),
                           pasos=struct.unpack_from('>I', b, 156)[0],
                           dibujos=struct.unpack_from('>I', b, 160)[0],
                           quads_generados=struct.unpack_from('>I', b, 164)[0],
                           transicion=bool(struct.unpack_from('>I', b, 168)[0])))
    return dict(tareas=tareas, matriz_hd=list(struct.unpack('>16f', datos(mgr+96, 64))),
                fov=struct.unpack('>f', datos(mgr+452, 4))[0])

"""F4: escenario final para Z. Adquisicion HD solo en fase 3; no emulador.
La prueba funcional no acredita igualdad visual ni restauracion del renderer.
Paredes: ISO PAL, ELF SHA256 30c362969ad64184df0b4a4ee498badb101265470883948f3131b6302ae3c797.
Evidencia completa: E:/DBZ3HD_trabajo/solF4/paredes_iso.json.
"""
import ctypes
import json
import math
import re
import struct

# grupo: (registro MDD, entrada SPX, punto interior, normal); y=9.
PAREDES = {
    0: (86, 1, (-67.4799774090449, 9., 252.71515909830728),
        (3.012204103924887e-7, 1.3130842814706566e-14, -1.)),
    2: (35, 0, (-9.90432328743888e-10, 9., 150.25926208496094),
        (9.869020090036429e-8, 0., -1.)),
    4: (54, 0, (-161.1959482828776, 9., -148.4544881184896),
        (.16377727687358856, -0., .9864973425865173)),
    7: (116, 0, (166.04580688476562, 9., 43.61970520019531),
        (-.9985625743865967, 0., -.053598832339048386)),
    8: (15, 0, (-36.750038146972656, 9., 172.3749237060547), (-0., -0., -1.)),
}
FIN = r"dbz3 B2 HD: (VM bloqueada|guion finalizado)"

def eventos_windows():
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.OpenEventW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
    k.OpenEventW.restype = ctypes.c_void_p
    k.SetEvent.argtypes = [ctypes.c_void_p]
    k.SetEvent.restype = ctypes.c_int
    k.CloseHandle.argtypes = [ctypes.c_void_p]
    k.CloseHandle.restype = ctypes.c_int
    return k

def foto(P, j, cuadro, eventos=None, estado=None):
    j.log(rf'dbz3 B2 foto: detenida cuadro={cuadro} |{FIN}', 30)
    if re.search(FIN, j.texto_log()):
        raise P.Fallo(f'F4: la VM termino antes de la foto {cuadro}')
    k = eventos if eventos is not None else eventos_windows()
    h = k.OpenEventW(2, False, f'Local\\DBZ3_GP1_B2_Foto_{j.proc.pid}')
    if not h:
        raise P.Fallo('F4: evento de foto inaccesible')
    try:
        ruta = j.dest / f'dbz3_b2_foto_{cuadro}.json'
        antes = ruta.read_bytes()
        dato = json.loads(antes)
        if dato.get('cuadro') != cuadro or len(dato.get('actores', [])) != 2:
            raise P.Fallo('F4: sidecar de foto incompleto o de otro cuadro')
        j.captura(f'f4_cuadro_{cuadro}')
        if ruta.read_bytes() != antes:
            raise P.Fallo('F4: el estado cambio durante la foto')
        (j.run / f'f4_cuadro_{cuadro}.json').write_bytes(antes)
        if estado is not None:
            dato['estado_memoria'] = estado()
        return dato
    finally:
        try:
            if not k.SetEvent(h):
                raise P.Fallo('F4: no se pudo liberar la foto')
        finally:
            k.CloseHandle(h)

def ejecutar(P, j, grupo):
    if grupo not in PAREDES:
        raise P.Fallo('F4: grupo fuera del contrato')
    registro, entrada, punto, normal = PAREDES[grupo]
    P._esperar_pelea(j, 90)
    def leer(a, n):
        b = P.leer(j.h, P.GBASE+a, n)
        if b is None or len(b) != n:
            raise P.Fallo(f'F4: lectura incompleta {a:X}')
        return b
    def u(a):
        return int.from_bytes(leer(a, 4), 'big')
    def vec(a):
        return struct.unpack('>3f', leer(a, 12))
    def escribir(a, b):
        n = ctypes.c_size_t()
        if not P.K32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(P.GBASE+P.DESPL+a),
                                        b, len(b), ctypes.byref(n)) or n.value != len(b):
            raise P.Fallo(f'F4: preparacion no escrita {a:X}')
        if leer(a, len(b)) != b:
            raise P.Fallo(f'F4: preparacion sin readback {a:X}')
    mgr = u(0x823D6CF4)
    zona_antes = u(0x823a3dc0)
    actores = [mgr+2096+8240*i for i in range(2)]
    posiciones = []
    for i, actor in enumerate(actores):
        pos = vec(actor+64)
        # Los f4_b2_*.toml usan DBZ3_COMBATE_LADOS=2: el teclado (atacante) es el actor 1 y el
        # muneco (victima) el actor 0; la victima va entre el atacante y la pared (B2-P06, 11/11).
        # Igual que la toma original (original_g*/preparacion.json): punto+normal*27 (atacante) y
        # *15 (victima) en x/z, en el suelo, SIN restar el centro de colision: restarlo movia
        # al atacante de lado (Nieve z+30) y la victima salia volando hacia -z en vez de +z.
        d = 15 if i == 0 else 27
        posiciones.append((punto[0]+normal[0]*d, pos[1], punto[2]+normal[2]*d))
    h = P.K32.OpenProcess(0x0438, False, j.proc.pid)
    if not h:
        raise P.Fallo('F4: no se pudo abrir la preparacion de posiciones')
    try:
        for i, actor in enumerate(actores):
            for off in (48, 64):
                escribir(actor+off, struct.pack('>3f', *posiciones[i]))
            dx, dz = (posiciones[1-i][q]-posiciones[i][q] for q in (0, 2))
            giro = int(math.atan2(dx, dz)*65536/(2*math.pi)) & 65535
            escribir(actor+6052, struct.pack('>I', giro))
    finally:
        P.K32.CloseHandle(ctypes.c_void_p(h))
    # Preparar posiciones/orientacion; no escribir velocidad, reaccion,
    # contacto pendiente, umbral, camara ni evento del guion.
    datos = dict(grupo=grupo, registro_fixture=registro, entrada=entrada,
                 posiciones=posiciones, fotos=[], captura_atomica_gpu=False)
    try:
        # Guardia+patada mantenidas (golpe 241 -> reaccion 20/22), como el arnes del 7/11.
        # L+P daba la reaccion 25 (empujon), que no lanza (B2-P06, 11/11).
        j.tecla('Space+P 3000', 0)
        j.log(r'dbz3 B2 HD: inicio por colision real|dbz3 B2 HD: (?:inicio|preflight) bloqueado', 30)
        j.log(rf'dbz3 B2 HD: inicio validado grupo={grupo} entrada={entrada} ajuste_aislado=true', 3)
        rol = re.search(r'actores=([0-9A-Fa-f]{8})/([0-9A-Fa-f]{8})', j.texto_log())
        if not rol:
            raise P.Fallo('F4: faltan roles fisicos de la VM')
        roles = [int(x, 16) for x in rol.groups()]
        if set(roles) != set(actores):
            raise P.Fallo('F4: roles fuera de los actores actuales')
        datos['roles_atacante_victima'] = roles
        def estado_roles():
            return [dict(pos=vec(x+64), vida=struct.unpack('>f', leer(x+6080, 4))[0],
                         ki=struct.unpack('>f', leer(x+6100, 4))[0],
                         vida_bits=leer(x+6080, 4).hex(), ki_bits=leer(x+6100, 4).hex())
                    for x in roles]
        # Base/Kami acaban antes de 300; ese cuadro pertenece al combate restaurado.
        # Nieve/Llanura/Ciudad conservan las tres muestras del contrato.
        fotos = (30, 120) if grupo in (0, 2) else (30, 120, 300)
        for cuadro in fotos:
            datos['fotos'].append(foto(P, j, cuadro, estado=estado_roles))
        j.log(FIN, 30)
        terminal = re.search(FIN+r" cuadro=([0-9.]+) bloqueo='([^']*)' VM='([^']*)'", j.texto_log())
        if not terminal or terminal[1] != 'guion finalizado' or terminal[3] or terminal[4]:
            raise P.Fallo('F4: final ausente o VM bloqueada')
        datos['cuadros_vm'] = float(terminal[2])
        if grupo == 7 and abs(datos['cuadros_vm']-719) > 2:
            raise P.Fallo('F4: Nieve fuera de 719 +/-2 cuadros')
        if not j.vivo() or u(0x823D6CF4) != mgr or u(mgr+35128) != 4:
            raise P.Fallo('F4: el combate no volvio tras la escena')
        fin = estado_roles()
        if any(not math.isfinite(v) for a in fin for v in (*a['pos'], a['vida'], a['ki'])):
            raise P.Fallo('F4: estado final no finito')
        datos['actores_fin'] = fin
        datos['zona_antes'] = zona_antes
        datos['zona_despues'] = u(0x823a3dc0)
        if grupo in (0, 2) and datos['zona_despues'] != zona_antes:
            raise P.Fallo('F4: Base/Kami no deben cambiar de zona')
        if grupo == 8 and abs(fin[1]['pos'][2]+25) > 1:
            raise P.Fallo('F4: Ciudad no llega a z=-25 +/-1')
        # La primera foto es DESPUES del golpe: no confundir dano legitimo
        # con dano del guion. Comparar aqui no mide el intervalo anterior a30.
        # Los bits evitan comparar el JSON de seis cifras con el float completo.
        for i in range(2):
            for k in ('vida_bits', 'ki_bits'):
                if fin[i][k] != datos['fotos'][0]['estado_memoria'][i][k]:
                    raise P.Fallo(f'F4: cambio de {k} despues de foto30, lado{i}')
        j.captura('f4_fin')
        datos['funcional'] = True
    finally:
        (j.run / 'f4_medida.json').write_text(json.dumps(datos, indent=2), encoding='utf-8')

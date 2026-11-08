"""Audita estados PAL vinculados y ofrece un lector HD US para el arnés.

1B29D0 consulta 13AC10 con máscara1000 y la posición del hueso10.
820C8068 consulta 820886C0 con máscara100 y la matriz raíz. Se muestran las
dos reglas de admisión sobre las MISMAS banderas PS2, no una captura de HD.
No se reconstruye la selección por influencia ni el resultado del dibujo.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import zipfile

from solb2x_censo import E, u


ELF_PAL = '30c362969ad64184df0b4a4ee498badb101265470883948f3131b6302ae3c797'


def gp_elf(elf):
    if hashlib.sha256(elf).hexdigest() != ELF_PAL:
        raise ValueError('ELF distinto del perfil PAL acreditado')
    candidatos = []
    ph, sh = u(elf, 28), u(elf, 32)
    pes, pn, ses, sn = struct.unpack_from('<4H', elf, 42)
    for i in range(pn):
        p = ph+i*pes
        if u(elf, p) == 0x70000000:  # PT_MIPS_REGINFO
            candidatos.append((u(elf, p+4), u(elf, p+16), 'programa', i))
    for i in range(sn):
        p = sh+i*ses
        if u(elf, p+4) == 0x70000006:  # SHT_MIPS_REGINFO
            candidatos.append((u(elf, p+16), u(elf, p+20), 'seccion', i))
    registros = []
    for off, size, tabla, indice in candidatos:
        if size != 24 or off+size > len(elf):
            raise ValueError('Elf32_RegInfo incompleto o de tamaño inesperado')
        registros.append(dict(tabla=tabla, indice=indice, offset=off,
                              bytes=size, gp=u(elf, off+20)))
    valores = {r['gp'] for r in registros}
    if len(valores) != 1 or not 0x10000 <= next(iter(valores)) < 0x2000000:
        raise ValueError('GP no resuelto de forma única por Elf32_RegInfo')
    return next(iter(valores)), registros


def luces_ram(ram, gp):
    if len(ram) != 0x2000000:
        raise ValueError('RAM PAL de tamaño inesperado')

    def rango(p, n):
        if p < 0x10000 or p+n > len(ram):
            raise ValueError(f'puntero fuera de RAM: {p:X}+{n:X}')

    def vec(p):
        rango(p, 16)
        v = list(struct.unpack_from('<4f', ram, p))
        if not all(math.isfinite(x) for x in v):
            raise ValueError(f'vector no finito: {p:X}')
        return v

    cabeza = gp-0x7b08  # iGpffff84f8, leído por13AC10
    rango(cabeza, 4)
    p, vistos, filas = u(ram, cabeza), set(), []
    while p:
        rango(p, 80)
        if p in vistos or len(vistos) >= 1024:
            raise ValueError('lista de luces cíclica o sin final dentro del límite')
        vistos.add(p)
        flags = struct.unpack_from('<H', ram, p+0x32)[0]
        filas.append(dict(direccion=p, tipo=struct.unpack_from('<h', ram, p+0x30)[0],
                          flags=flags, posicion=vec(p), direccion_luz=vec(p+16),
                          color=vec(p+32),
                          admite_original_1000=not flags & 0xf000 or bool(flags & 0x1000),
                          admite_regla_hd_0100_sin_traducir=not flags & 0xff00 or bool(flags & 0x100)))
        p = u(ram, p+0x4c)
    return dict(global_cabeza=cabeza, luces=filas,
                admitidas_original=[r['direccion'] for r in filas if r['admite_original_1000']],
                admitidas_regla_hd_sin_traducir=[r['direccion'] for r in filas
                                               if r['admite_regla_hd_0100_sin_traducir']])


def perfil_escenario(ram, elf, gp, luces):
    """Contrasta 1D6430 con RAM. No interpreta ni traduce un banco #AML."""
    if len(ram) != 0x2000000:
        raise ValueError('RAM PAL de tamaño inesperado')

    def rango(p, n):
        if p < 0x10000 or p+n > len(ram):
            raise ValueError(f'perfil fuera de RAM: {p:X}+{n:X}')

    def vec(p):
        rango(p, 16)
        v = list(struct.unpack_from('<4f', ram, p))
        if not all(math.isfinite(x) for x in v):
            raise ValueError(f'perfil no finito: {p:X}')
        return v

    def va(p, n):
        ph, es, cantidad = u(elf, 28), *struct.unpack_from('<HH', elf, 42)
        for i in range(cantidad):
            typ, off, addr, _, size, *_ = struct.unpack_from('<8I', elf, ph+i*es)
            if typ == 1 and addr <= p and p+n <= addr+size:
                return elf[off+p-addr:off+p-addr+n]
        raise ValueError(f'perfil sin respaldo en ELF: {p:X}+{n:X}')

    stage, zona = u(ram, 0x373460), u(ram, 0x3734ac)
    if stage >= 9 or zona*0x1c+4 > 0x9c:
        raise ValueError('stage/zona fuera del perfil de escenarios B2')
    celda = 0x372ad4+stage*0x9c+zona*0x1c
    indice = struct.unpack_from('<i', ram, celda)[0]
    override = u(ram, gp-0x7870)  # pauGpffff8790, leído por1D6430
    if not override and indice < 0:
        raise ValueError('escenario sin perfil de luz ni sustitución activa')
    p = override or 0x372330+indice*0x50
    rango(p, 0x50)
    direccion = vec(p+16)
    modulo = math.sqrt(sum(x*x for x in direccion[:3]))
    if modulo == 0:
        raise ValueError('dirección de luz del perfil nula')
    normal = [x/modulo for x in direccion[:3]]
    mgr = u(ram, gp-0x783c)  # iGpffff87c4
    rango(mgr, 0x19c90)
    luz = u(ram, mgr+0x1f8)
    fila = next((f for f in luces if f['direccion'] == luz), None)
    if fila is None:
        raise ValueError('la luz del escenario no está en la lista observada')
    ambiental = vec(p)
    color = vec(p+32)[:3]+[struct.unpack('<f', struct.pack('<I', 0x3d8f5c29))[0]]
    ambiente_ram = vec(0x3b2940)
    reorienta = (ram[mgr+0x19c8f] == 0 and
                 u(ram, 0x374b00+stage*8+zona*4) != 0)
    contraste_elf = None
    if not override:
        original = va(p, 0x50)
        valores = struct.unpack_from('<12f', original)
        if not all(math.isfinite(x) for x in valores):
            raise ValueError('perfil ELF no finito')
        modulo_elf = math.sqrt(sum(x*x for x in valores[4:7]))
        if modulo_elf == 0:
            raise ValueError('dirección ELF nula')
        normal_elf = [x/modulo_elf for x in valores[4:7]]
        contraste_elf = dict(
            offsets_bytes_distintos=[i for i, (x, y) in enumerate(zip(original, ram[p:p+0x50])) if x != y],
            direccion=list(valores[4:8]), direccion_normalizada=normal_elf,
            error_direccion_normalizada=max(abs(x-y) for x, y in zip(normal_elf, normal)),
            error_color=max(abs(x-y) for x, y in zip(valores[8:11], color[:3])),
            error_ambiental=max(abs(x-y) for x, y in zip(valores[:3], ambiental[:3])))
    return dict(stage=stage, zona=zona, celda=celda, indice=indice,
                celda_igual_elf=ram[celda:celda+4] == va(celda, 4),
                sustitucion=override, perfil=p,
                perfil_igual_elf=None if override else ram[p:p+0x50] == va(p, 0x50),
                contraste_elf=contraste_elf,
                luz_escenario=luz, tipo_esperado_1d6430=0,
                tipo_observado=fila['tipo'], flags_observadas=fila['flags'],
                bits_1003_presentes=fila['flags'] & 0x1003 == 0x1003,
                bits_anadidos_1d6430=0x1003,
                direccion_normalizada_esperada=normal,
                error_direccion=max(abs(x-y) for x, y in zip(normal, fila['direccion_luz'][:3])),
                color_esperado=color,
                error_color=max(abs(x-y) for x, y in zip(color, fila['color'])),
                ambiental=ambiental[:3]+[0.0], ambiental_ram=ambiente_ram,
                error_ambiental=max(abs(x-y) for x, y in zip(ambiental[:3]+[0.0], ambiente_ram)),
                reorientacion_desde_punto_1d6370=reorienta)


def luces_hd(leer, *, con_controles=False):
    """Lectura HD US por direcciones guest; sin escrituras ni captura atómica.

    C1D60: registro82323760, paso92, variante32 y perfil80 en82322B20.
    886C0: cabeza8238A4C4, ambiente8238A4B0 y luces de80B. Los vectores
    del perfil son datos crudos: su destino depende del callback8201ED04.
    FB998 enlaza control/luz en mgr+616+8*i. Opcionalmente se lee el tipo
    del CONTROL y sus tablas; no confundirlo con el tipo de la luz en+48.
    """
    def bloque(p, n):
        if p < 0x10000 or p+n > 0x100000000:
            raise ValueError('puntero HD fuera del espacio guest')
        b = leer(p, n)
        if b is None or len(b) != n:
            raise ValueError(f'lectura HD incompleta: {p:X}+{n:X}')
        return b

    def entero(p):
        return struct.unpack('>I', bloque(p, 4))[0]

    def vec(b, p=0):
        v = list(struct.unpack_from('>4f', b, p))
        if not all(math.isfinite(x) for x in v):
            raise ValueError('vector HD no finito')
        return v

    g, cabeza = 0x823a3d80, 0x8238a4c4
    stage, zona, primera = entero(g), entero(g+64), entero(cabeza)
    if stage >= 17 or zona >= 2:
        raise ValueError('stage/zona fuera del registro HD de dos variantes')
    celda = 0x82323760+stage*92+28+zona*32
    registro = bloque(celda, 32)
    indice = struct.unpack_from('>i', registro)[0]
    if not 0 <= indice < 256:  # límite de lectura, no censo de perfiles
        raise ValueError('índice HD fuera del límite de lectura')
    perfil = 0x82322b20+indice*80
    datos_perfil = bloque(perfil, 80)
    filas, vistos, p = [], set(), primera
    while p:
        if p in vistos or len(vistos) >= 1024:
            raise ValueError('lista HD cíclica o sin final dentro del límite')
        vistos.add(p)
        b = bloque(p, 80)
        tipo, flags = struct.unpack_from('>hH', b, 48)
        filas.append(dict(direccion=p, tipo=tipo, flags=flags,
                          posicion=vec(b), direccion_luz=vec(b, 16), color=vec(b, 32),
                          admite_0100=not flags & 0xff00 or bool(flags & 0x100),
                          bytes_hex=b.hex(), sha256=hashlib.sha256(b).hexdigest()))
        p = struct.unpack_from('>I', b, 76)[0]
    ambiental = vec(bloque(0x8238a4b0, 16))
    callback = entero(0x8201ed04)
    controles = None
    if con_controles:
        global_mgr = 0x823d6cf4  # lis−32195, lwz+27892 enFB998/FB718
        mgr = entero(global_mgr)
        cantidad = entero(mgr+35096)
        if cantidad > 16:  # límite de adquisición, no censo de actores
            raise ValueError('cantidad HD fuera del límite de controles')
        tabla = bloque(mgr+616, (2*cantidad+1)*8)
        registros = []
        for i in range(2*cantidad+1):
            control, luz = struct.unpack_from('>2I', tabla, i*8)
            fila = dict(indice=i, control=control, luz=luz,
                        luz_en_lista=luz in vistos)
            if control:
                b = bloque(control, 56)
                tipo = struct.unpack_from('>i', b, 4)[0]
                if not -1 <= tipo <= 5:
                    raise ValueError('tipo HD fuera de la tabla de controles')
                destinos = {}
                if tipo >= 0:
                    for nombre, base in (('inicializacion', 0x8201fa84),
                                         ('actualizacion', 0x8201fa9c)):
                        destino = entero(base+tipo*4)
                        if destino & 3 or not 0x82000000 <= destino < 0x82300000:
                            raise ValueError('destino HD inválido en tabla de controles')
                        destinos[nombre] = destino
                fila.update(tipo_control=tipo, luz_del_control=struct.unpack_from('>I', b)[0],
                            coincide_luz=struct.unpack_from('>I', b)[0] == luz,
                            destinos=destinos, bytes_hex=b.hex(),
                            sha256=hashlib.sha256(b).hexdigest())
            registros.append(fila)
        if (mgr != entero(global_mgr) or cantidad != entero(mgr+35096) or
                tabla != bloque(mgr+616, len(tabla))):
            raise ValueError('cambió la tabla HD de controles durante la lectura')
        controles = dict(global_mgr=global_mgr, mgr=mgr, cantidad=cantidad,
                         tabla=mgr+616, tabla_hex=tabla.hex(), registros=registros,
                         cobertura='controles leídos; no acredita que se ejecutaran sus destinos')
    if (stage, zona, primera) != (entero(g), entero(g+64), entero(cabeza)):
        raise ValueError('cambió la cabecera HD durante la lectura')
    return dict(stage=stage, zona=zona, celda=celda, indice=indice, perfil=perfil,
                registro_hex=registro.hex(), perfil_hex=datos_perfil.hex(),
                perfil_sha256=hashlib.sha256(datos_perfil).hexdigest(),
                vectores_perfil=[vec(datos_perfil, i*16) for i in range(3)],
                global_cabeza=cabeza, primera=primera, luces=filas,
                 ambiental_ram=ambiental, callback=callback, controles=controles,
                cobertura='HD US en el punto de lectura del arnés; no dibujo del robot',
                 limitaciones=['Lectura no atómica; se revalidan cabecera y enlaces de controles seleccionados.',
                              'Admisión0100 no acredita selección por influencia ni imagen.',
                              'No acredita el destino de los vectores del perfil ni un banco ACL.'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--iso', default='D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
    ap.add_argument('--estados-vinculados', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    if a.salida.exists() or not a.salida.parent.is_dir():
        ap.error('elegir salida nueva en una carpeta existente')
    fuente = E.iso.Iso(a.iso)
    try:
        elf = fuente.read(fuente.boot_elf())
    finally:
        fuente._f.close()
    gp, registros = gp_elf(elf)
    bruto = a.estados_vinculados.read_bytes()
    vinculados = json.loads(bruto)
    muestras = []
    for muestra in vinculados['muestras']:
        tareas = [t for t in muestra['tareas'] if 'controles90' in t]
        if not tareas:
            continue
        estado = Path(muestra['estado'])
        with estado.open('rb') as f:
            digest = hashlib.file_digest(f, 'sha256').hexdigest()
        if digest != muestra['sha256']:
            raise ValueError(f'estado cambiado: {estado}')
        with zipfile.ZipFile(estado) as z:
            ram = z.read('eeMemory.bin')
        if hashlib.sha256(ram).hexdigest() != muestra['ram_sha256']:
            raise ValueError(f'RAM distinta de la muestra vinculada: {estado}')
        datos = luces_ram(ram, gp)
        perfil = perfil_escenario(ram, elf, gp, datos['luces'])
        puntos = []
        for tarea in tareas:
            control = next(c for c in tarea['controles90'] if c and c['offset'] == 8)
            modelo = control['modelo']
            if not 0x10000 <= modelo <= len(ram)-0x40c:
                raise ValueError('modelo fuera de RAM')
            matrices = u(ram, modelo+0x408)
            if not 0x10000 <= matrices <= len(ram)-0x2c0:
                raise ValueError('matrices fuera de RAM')
            raiz = list(struct.unpack_from('<3f', ram, matrices+48))
            hueso10 = list(struct.unpack_from('<3f', ram, matrices+0x2b0))
            if not all(math.isfinite(x) for x in raiz+hueso10):
                raise ValueError('posición de hueso no finita')
            puntos.append(dict(tarea=tarea['direccion'], modelo=modelo,
                               raiz=raiz, hueso10=hueso10,
                               distancia=math.dist(raiz, hueso10)))
        muestras.append(dict(estado=str(estado), sha256=digest, enlaces=muestra['enlaces'],
                             puntos=puntos, perfil_escenario=perfil, **datos))
    if not muestras:
        raise ValueError('ninguna tarea90 vinculada para auditar')
    resultado = dict(elf_sha256=ELF_PAL, gp=gp, reginfo=registros,
                     vinculados_sha256=hashlib.sha256(bruto).hexdigest(), muestras=muestras,
                      cobertura='estados archivados con tarea90; no curva completa ni captura HD',
                      limitaciones=['La admisión no acredita las tres luces seleccionadas por influencia.',
                                    'No se ha traducido ni leído aquí el banco de luces HD.',
                                    'El perfil original no demuestra que el tema donante HD coincida.',
                                    'La distancia raíz/hueso10 no demuestra una diferencia de iluminación.',
                                    'No acredita matrices finales, imagen ni fidelidad de la escena.'])
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(resultado, f, ensure_ascii=False, indent=2, allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida), gp=hex(gp), muestras=len(muestras),
                          resumen=[dict(enlaces=m['enlaces'],
                                        perfil={k:m['perfil_escenario'][k] for k in
                                                ('indice', 'celda_igual_elf', 'perfil_igual_elf',
                                                 'contraste_elf', 'error_direccion', 'error_color',
                                                 'error_ambiental', 'reorientacion_desde_punto_1d6370')},
                                        luces=len(m['luces']), original=m['admitidas_original'],
                                        regla_hd_sin_traducir=m['admitidas_regla_hd_sin_traducir'])
                                   for m in muestras]), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

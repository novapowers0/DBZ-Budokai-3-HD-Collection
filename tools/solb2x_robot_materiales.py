"""Lectura acotada de materiales/programas del robot durante la foto detenida.

Las direcciones de dibujo proceden de la sonda CPU del par240. La memoria se
lee después, en el cuadro fotografiado: no acredita ejecución ni estado GPU.
"""
import hashlib
import re
import struct


def capturar(leer, texto_log, cuadro):
    def valido(p, n):
        return 0x10000000 <= p <= 0x100000000-n and 0 < n <= 65536

    def datos(p, n):
        if not valido(p, n):
            return None
        try:
            b = leer(p, n)
        except (OSError, RuntimeError):
            return None
        if b is None or len(b) != n:
            return None
        return b

    def bloque(p, n):
        b = datos(p, n)
        if b is None:
            return dict(direccion=f'{p:08X}', disponible=False)
        return dict(direccion=f'{p:08X}', disponible=True, bytes=n,
                    sha256=hashlib.sha256(b).hexdigest(), hex=b.hex())

    def u(p):
        b = datos(p, 4)
        return struct.unpack('>I', b)[0] if b is not None else None

    def programa(p):
        r = bloque(p, 128)
        if r['disponible']:
            b = bytes.fromhex(r['hex'])
            recurso = struct.unpack_from('>I', b, 8)[0]
            inicio = struct.unpack_from('>I', b, 52)[0]
            cantidad = struct.unpack_from('>I', b, 60)[0]
            r['recurso_8'] = bloque(recurso, 4096)
            r['buffer_52_60'] = bloque(inicio, cantidad)
            if r['recurso_8']['disponible']:
                cabecera = bytes.fromhex(r['recurso_8']['hex'])
                tipo = struct.unpack_from('>I', cabecera)[0] & 0xffff
                if tipo in (6, 7):
                    offset = 32 if tipo == 6 else 24
                    puntero = struct.unpack_from('>I', cabecera, offset)[0]
                    r['candidato_codigo_recurso'] = dict(offset=offset,
                        lectura_directa_ajusta_ventana=False,
                        lectura_directa=bloque(puntero, 4096))
                    # SDK xmemory.cpp::PhysicalHeap::GetPhysicalAddress suma
                    # 0x1000 a la ventana E0000000. Se lee su alias A0000000;
                    # el cotejo posterior exige el ucode completo, no prefijos.
                    if 0xe0000000 <= puntero < 0xffffe000:
                        alias = 0xa0000000 + puntero-0xe0000000+0x1000
                        r['candidato_codigo_recurso']['alias_fisico_sdk'] = bloque(alias,4096)
        return r

    partes = []
    patron = (r'dbz3 B2 robot material: pasada=(\d+) dibujo=(\d+) '
              r'modelo=([0-9A-F]+) grupo=([0-9A-F]+) parte=([0-9A-F]+)')
    for m in re.finditer(patron, texto_log):
        pasada, dibujo = map(int, m.group(1, 2))
        modelo, grupo, parte = (int(m.group(i), 16) for i in (3, 4, 5))
        r = dict(pasada=pasada, dibujo=dibujo, modelo=f'{modelo:08X}',
                 grupo=f'{grupo:08X}', parte=bloque(parte, 96))
        if r['parte']['disponible'] and valido(grupo, 64):
            raw = bytes.fromhex(r['parte']['hex'])
            indice = struct.unpack_from('>H', raw, 20)[0]
            tabla = u(grupo+32)
            if tabla is not None:
                r.update(indice_material=indice, tabla_materiales=f'{tabla:08X}',
                         material=bloque(tabla+80*indice, 80))
            else:
                r['tabla_materiales_disponible'] = False
        partes.append(r)

    programas = []
    combinaciones = sorted(set(tuple(map(int, m)) for m in re.findall(
        r'dbz3 B2 robot programa: pasada=\d+ dibujo=\d+ arg4=(\d+) '
        r'arg5=(\d+) arg6=(\d+)', texto_log)))
    # 97290 carga el dispositivo por [823958A4]. A6148 pasa dispositivo+408
    # a A9BC0 (vector de registros de20B) y A95A0 (PS, pares de8B desde+276).
    dispositivo = u(0x823958a4)
    if dispositivo is not None and valido(dispositivo, 4096):
        ini, fin = u(dispositivo+412), u(dispositivo+416)
        n = ((fin-ini)//20 if ini is not None and fin is not None and
             fin >= ini and (fin-ini)%20 == 0 else -1)
        for vs, ps, formato in combinaciones:
            r = dict(vs=vs, ps=ps, formato=formato, candidatos_vertex=[])
            r['registros_vertex_disponibles'] = 0 <= n <= 1024 and valido(ini, max(20, n*20))
            if r['registros_vertex_disponibles']:
                for i in range(n):
                    b = datos(ini+20*i, 20)
                    if b is None:
                        r['registros_vertex_disponibles'] = False
                        continue
                    a, c, d, shader, referencia = struct.unpack('>5I', b)
                    if (a, c, d) == (vs, ps, formato):
                        r['candidatos_vertex'].append(dict(registro=f'{ini+20*i:08X}',
                            objeto=programa(shader), referencia=f'{referencia:08X}'))
            if 0 <= ps < 256:
                p = u(dispositivo+684+8*ps)
                if p is not None:
                    r['pixel'] = programa(p)
                else:
                    r['pixel_disponible'] = False
            programas.append(r)
    return dict(cuadro_memoria=cuadro, par_direcciones_cpu=240,
                dispositivo=f'{dispositivo:08X}' if dispositivo is not None else None,
                partes=partes, programas=programas,
                alcance='memoria CPU posterior a la sonda; sin captura de ejecución GPU',
                escena_aceptada=False)

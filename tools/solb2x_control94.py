"""Contrato estático de94 sobre las copias PAL/US del jugador; no ejecuta el juego."""
import argparse
from pathlib import Path
import struct

from solb2x_robot_contrato import (E, IMAGEN_US, cuerpo_hd, exigir, guardar,
                                  gp_elf, mapear_elf, sha)


def cuerpo_pal(elf, inicio, fin):
    exigir(inicio % 4 == fin % 4 == 0 and fin > inicio, 'Rango PAL inválido')
    off, b = mapear_elf(elf, inicio, fin-inicio)
    palabras, llamadas = [], []
    for i in range(0, len(b), 4):
        pc, w = inicio+i, struct.unpack_from('<I', b, i)[0]
        palabras.append(dict(pc=f'{pc:08X}', palabra=f'{w:08X}'))
        if w >> 26 == 3:  # JAL MIPS: los cuatro bits altos son de PC+4.
            destino = ((pc+4) & 0xf0000000) | ((w & 0x03ffffff) << 2)
            llamadas.append(dict(pc=f'{pc:08X}', destino=f'{destino:08X}'))
    return dict(funcion=f'{inicio:08X}', fin_exclusivo=f'{fin:08X}', offset=off,
                bytes_sha256=sha(b), palabras=palabras, llamadas_jal=llamadas)


def auditar(elf, imagen):
    gp, _ = gp_elf(elf)
    exigir(sha(imagen) == IMAGEN_US, 'Imagen HD distinta del perfil US acreditado')
    pal = [cuerpo_pal(elf, a, b) for a, b in [
        (0x1b8ff0, 0x1b9130), (0x1af9b0, 0x1afa60),
        (0x1afa60, 0x1afc50), (0x1b2bc0, 0x1b2c60),
        (0x1b2990, 0x1b29d0), (0x1ac610, 0x1ac6a0),
        (0x1ac6a0, 0x1ac910)]]
    palabras = {int(r['pc'], 16): int(r['palabra'], 16)
                for c in pal for r in c['palabras']}
    guardas = [
        (0x1b8ff4, 0x2402ffff, 'ADDIU v0,zero,-1: sentinel entero FFFFFFFF'),
        (0x1b9014, 0x8c700008, 'LW s0,arg2: modo sin conversión de float'),
        (0x1b9018, 0xc4740004, 'LWC1 f20,arg1: tiempo AMM float'),
        (0x1b901c, 0x8c71000c, 'LW s1,arg3: índice entero'),
        (0x1b9020, 0x16020009, 'BNE s0,v0,1B9048: no compara NaN'),
        (0x1b9024, 0xc4750000, 'Delay slot: LWC1 f21,arg0, también al saltar'),
        (0x1b9064, 0x12400014, 'BEQ s2,zero,1B90B8: UV sigue sin AMM'),
        (0x1b906c, 0x1a000002, 'BLEZ s0,1B9078: modo positivo o cero'),
        (0x1b9074, 0x32020003, 'ANDI modo AMM,3'),
        (0x1b908c, 0xe6540004, 'SWC1 f20,4(s2): no trunca el tiempo AMM'),
        (0x1b90c8, 0x32020005, 'ANDI modo UV,5'),
        (0x1b90e0, 0xe6350004, 'SWC1 f21,4(s1): tiempo UV float'),
        (0x1af9d4, 0x30630400, 'ANDI flags,400: filtro de eliminación PAL'),
        (0x1af9e4, 0x14640018, 'BNE usuario,a0: solo la raíz solicitada'),
        (0x1afa20, 0xac600000, 'SW zero,slot: libera la entrada de tarea'),
        (0x1afa4c, 0x1528ffe0, 'BNE siguiente,fin,1AF9D0: continúa tras eliminar'),
        (0x1afabc, 0x1071005c, 'BEQ usuario,registro,1AFC30: reutiliza tarea existente'),
        (0x1afae8, 0x24e7c610, 'Dibujo normal en1AC610'),
        (0x1afaec, 0x0c06b710, 'JAL1ADC40: crea tarea normal'),
        (0x1afaf0, 0x2508c6a0, 'Delay slot: paso normal en1AC6A0'),
        (0x1afb0c, 0x34630400, 'ORI flags,400: tarea normal eliminable por94'),
        (0x1b2be8, 0x24e729d0, 'Dibujo del robot en1B29D0'),
        (0x1b2bec, 0x25082990, 'Paso del robot en1B2990'),
        (0x1b2bf0, 0x0c06b710, 'JAL1ADC40: crea tarea del robot'),
        (0x1b2bf4, 0x0200302d, 'Delay slot: usuario=registro de raíz0'),
        (0x1b2c0c, 0x34630400, 'ORI flags,400: robot también eliminable por94'),
    ]
    for pc, w, _ in guardas:
        exigir(palabras[pc] == w, f'Instrucción PAL inesperada: {pc:08X}')
    # Las dos etapas de la tabla se leen como punteros, no como matriz plana.
    _, b = mapear_elf(elf, 0x375190+4*9, 4)
    grupo = struct.unpack('<I', b)[0]
    _, b = mapear_elf(elf, grupo+4*4, 4)
    exigir(struct.unpack('<I', b)[0] == 0x1b8ff0, 'Tabla94 distinta')
    hd = [cuerpo_hd(imagen, s, f) for s, f in [
        ('820BFBA8', 'dbz3_recomp.41.cpp'),
        ('820C1390', 'dbz3_recomp.18.cpp'),
        ('820BE1D0', 'dbz3_recomp.37.cpp'),
        ('820BD628', 'dbz3_recomp.20.cpp')]]
    return dict(elf_sha256=sha(elf), imagen_sha256=sha(imagen), gp=gp,
                tabla94=dict(grupo=f'{grupo:08X}', destino='001B8FF0'),
                cuerpos_pal=pal, cuerpos_hd=hd,
                guardas=[dict(pc=f'{pc:08X}', palabra=f'{w:08X}', lectura=t)
                         for pc, w, t in guardas],
                contrato=dict(sentinel_entero_bits='FFFFFFFF',
                    arg0='Tiempo UV float; negativo conserva el anterior',
                    arg1='Tiempo AMM float sin truncado; negativo conserva el anterior',
                    arg2='Entero: -1 elimina; otro valor asegura tarea; positivo aplica &3/&5',
                    arg3='Índice entero de raíz; registro PAL base+16*índice',
                    sin_amm='Sigue procesando UV si el segundo controlador existe',
                    eliminar_pal='1AF9B0: recorre tareas por usuario y flags400; elimina todas las coincidencias',
                    asegurar_pal='1AFA60: reutiliza cualquier tarea del usuario; si no existe crea paso1AC6A0/dibujo1AC610',
                    tarea90='1B2BC0: mismo usuario de raíz0 y flag400; paso1B2990/dibujo1B29D0',
                    eliminar_hd='BFBA8: primera coincidencia de usuario, sin filtro400; BD570'),
                limites=[
                    'Las guardas acreditan palabras originales; no son replay R5900 ni FPU.',
                    'Los rangos PAL conservan palabras, sin alias MSA de Capstone.',
                    'Tiempos negativos finitos acreditados; no se equiparan casos FPU NaN.',
                    'BFBA8 no sustituye sin más la eliminación PAL si90 añade otra tarea.',
                    'UV sin AMM y restauración UV siguen pendientes en el consumidor HD.',
                    'Matrices, luces, pasadas y enlace final del consumidor90 siguen abiertos.',
                ], aceptacion_escena=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', type=Path, required=True)
    p.add_argument('--imagen', type=Path, required=True)
    p.add_argument('--salida', type=Path, required=True)
    a = p.parse_args()
    if a.salida.exists():
        raise FileExistsError(a.salida)
    iso = E.iso.Iso(str(a.iso))
    try:
        elf = iso.read(iso.boot_elf())
    finally:
        iso._f.close()
    datos = auditar(elf, a.imagen.read_bytes())
    guardar(a.salida, datos)
    print(f"94: {len(datos['guardas'])} guardas PAL verificadas; sentinel entero FFFFFFFF.")
    print(f"{sum(len(c['palabras']) for c in datos['cuerpos_pal'])} palabras PAL; "
          f"{sum(len(c['instrucciones']) for c in datos['cuerpos_hd'])} instrucciones PPC.")
    print('AMM/UV conservan tiempos float. Eliminación PAL/HD distinta; escena no aceptada.')
    print(a.salida)


if __name__ == '__main__':
    main()

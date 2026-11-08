"""Contrato estático acotado del robot RR; no ejecuta ni adapta el juego.

Trabaja con la ISO PAL y la imagen US aportadas por el jugador. Las fórmulas
son una lectura algebraica de instrucciones; los ejemplos no emulan la FPU
R5900 ni suponen que los dos auxiliares trigonométricos sean equivalentes.
"""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import struct

from solb2x_robot_luces import E, gp_elf


ROOT = Path(__file__).resolve().parent.parent
IMAGEN_US = 'ae3dc16b90c0c48cbf362a9fd7377ff549963ca162af6e3cea1928e9452513f4'
DISTANCIA_PAL = 'dde3138109201b3db5b27e8da08c7b2480191232ba7590942b37ef571f272ed9'
REFERENCIAS = [
    'https://psi-rockin.github.io/ps2tek/#eeinstructiondecoding',
    'https://raw.githubusercontent.com/PCSX2/pcsx2/master/pcsx2/FPU.cpp',
]


def sha(b):
    return hashlib.sha256(b).hexdigest()


def exigir(condicion, mensaje):
    if not condicion:
        raise ValueError(mensaje)


def mapear_elf(elf, direccion, longitud):
    """Exige una única región PT_LOAD respaldada íntegramente por el archivo."""
    exigir(elf[:6] == b'\x7fELF\x01\x01', 'Se requiere ELF32 little-endian')
    exigir(longitud > 0, 'Longitud inválida')
    ph = struct.unpack_from('<I', elf, 28)[0]
    paso, n = struct.unpack_from('<HH', elf, 42)
    exigir(paso >= 32 and ph+n*paso <= len(elf), 'Tabla ELF incompleta')
    regiones = []
    for i in range(n):
        tipo, off, base, _, size, *_ = struct.unpack_from('<8I', elf, ph+i*paso)
        if tipo == 1 and base <= direccion and direccion+longitud <= base+size:
            inicio = off+direccion-base
            exigir(inicio+longitud <= len(elf), 'Segmento ELF truncado')
            regiones.append((inicio, elf[inicio:inicio+longitud]))
    exigir(len(regiones) == 1, 'Mapeo ELF ausente o ambiguo')
    return regiones[0]


def decodificar_acc(palabra):
    """Solo los dos COP1.S con acumulador necesarios aquí; no un R5900 general."""
    exigir(palabra >> 26 == 0x11 and (palabra >> 21) & 31 == 16,
           'No es COP1.S')
    ft, fs, fd, funcion = ((palabra >> 16) & 31, (palabra >> 11) & 31,
                          (palabra >> 6) & 31, palabra & 63)
    if funcion == 0x1a:
        exigir(fd == 0, 'Campo reservado MULA.S no nulo')
        return dict(mnemonic='mula.s', fs=fs, ft=ft, destino='ACC')
    exigir(funcion == 0x1c, 'COP1.S fuera del contrato acotado')
    return dict(mnemonic='madd.s', fs=fs, ft=ft, destino=f'f{fd}')


def constante_pal(cuerpo, pc):
    p = pc-0x266a50
    a, b = struct.unpack_from('<2I', cuerpo, p)
    registro = (a >> 16) & 31
    exigir(a >> 26 == 15 and (a >> 21) & 31 == 0 and b >> 26 == 13
           and (b >> 21) & 31 == registro and (b >> 16) & 31 == registro,
           f'Literal LUI/ORI inesperado en {pc:08X}')
    bits = ((a & 65535) << 16) | (b & 65535)
    return dict(pc=f'{pc:08X}', bits=f'{bits:08X}',
                valor=struct.unpack('<f', struct.pack('<I', bits))[0])


def cuerpo_hd(imagen, simbolo, archivo):
    """Retiene desensamblado del binario; el generado solo delimita el cuerpo."""
    from capstone import Cs, CS_ARCH_PPC, CS_MODE_64, CS_MODE_BIG_ENDIAN
    texto = (ROOT / 'generated' / archivo).read_text(encoding='utf-8')
    firma = f'DEFINE_REX_FUNC(sub_{simbolo}) {{'
    exigir(texto.count(firma) == 1, f'Símbolo no único: {simbolo}')
    inicio = texto.index(firma)
    fin = re.search(r'^}', texto[inicio:], re.MULTILINE)
    exigir(fin is not None, f'Cuerpo sin final: {simbolo}')
    fuente = texto[inicio:inicio+fin.end()]
    comentarios = re.findall(r'^\s*// (.*)$', fuente, re.MULTILINE)
    direccion = int(simbolo, 16)
    off = direccion-0x82000000
    exigir(comentarios and 0 <= off and off+4*len(comentarios) <= len(imagen),
           f'Rango HD inválido: {simbolo}')
    codigo = imagen[off:off+4*len(comentarios)]
    cs = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
    filas, llamadas, indirectas = [], [], []
    for i in range(0, len(codigo), 4):
        palabra, pc = codigo[i:i+4], direccion+i
        ins = list(cs.disasm(palabra, pc, count=1))
        exigir(len(ins) == 1 and ins[0].size == 4, f'PPC no decodificado: {pc:08X}')
        filas.append(dict(pc=f'{pc:08X}', bytes_hex=palabra.hex(),
                          mnemonic=ins[0].mnemonic, operandos=ins[0].op_str))
        w = int.from_bytes(palabra, 'big')
        if w >> 26 == 18 and w & 1:  # b/ba con LK, no inferir llamadas indirectas
            delta = w & 0x03fffffc
            if delta & 0x02000000:
                delta -= 0x04000000
            destino = (delta if w & 2 else pc+delta) & 0xffffffff
            llamadas.append(dict(pc=f'{pc:08X}', destino=f'{destino:08X}'))
        if w >> 26 == 19 and w & 1 and (w >> 1) & 1023 in (16, 528):
            indirectas.append(dict(pc=f'{pc:08X}', destino=None,
                                   registro='LR' if (w >> 1) & 1023 == 16 else 'CTR'))
    return dict(simbolo=simbolo, archivo=archivo, fuente_sha256=sha(fuente.encode()),
                bytes_sha256=sha(codigo), fin_exclusivo=f'{direccion+len(codigo):08X}',
                instrucciones=filas, llamadas_directas=llamadas,
                llamadas_indirectas=indirectas)


def ejemplos(constantes):
    """Contraejemplos algebraicos con factor y retorno auxiliar igualados."""
    c = {k: Fraction.from_float(v['valor']) for k, v in constantes.items()}
    filas = []
    for factor in (Fraction(1), Fraction(2)):
        for rama, s in [('cerca', None), ('lejos', None),
                        *[('intermedia', Fraction(i, 4)) for i in (0, 1, 2, 4)]]:
            if rama == 'cerca':
                pal, hd = factor*c['pal_min'], factor*c['hd_min']
            elif rama == 'lejos':
                pal, hd = factor*c['pal_max'], factor*c['hd_max']
            else:
                pal = factor*s*c['pal_max']+(1-s)*c['pal_min']
                hd = factor*(s*c['hd_max']+(1-s)*c['hd_min'])
            filas.append(dict(rama=rama, factor=float(factor),
                              retorno_auxiliar=None if s is None else float(s),
                              pal=float(pal), hd=float(hd), diferencia=float(pal-hd)))
    return filas


def auditar(elf, imagen):
    gp, _ = gp_elf(elf)  # Incluye la comprobación SHA256 del ELF PAL.
    exigir(sha(imagen) == IMAGEN_US, 'Imagen HD distinta del perfil US acreditado')
    off, pal = mapear_elf(elf, 0x266a50, 0x140)
    exigir(sha(pal) == DISTANCIA_PAL, '266A50 difiere de la extracción acreditada')
    acc = []
    for pc, esperado in ((0x266b68, 0x4602181a), (0x266b6c, 0x4600a01c)):
        w = struct.unpack_from('<I', pal, pc-0x266a50)[0]
        exigir(w == esperado, f'Instrucción ACC inesperada: {pc:08X}')
        acc.append(dict(pc=f'{pc:08X}', palabra=f'{w:08X}', **decodificar_acc(w)))
    constantes = dict(pal_min=constante_pal(pal, 0x266ac4),
                      pal_max=constante_pal(pal, 0x266af4))
    for nombre, direccion in [('hd_min', 0x8201d348), ('hd_max', 0x820009f8),
                              ('hd_factor_fov', 0x8201fbb4), ('hd_cerca', 0x8201b37c),
                              ('hd_lejos', 0x8201fbb0), ('hd_inverso_170', 0x8201fbac),
                              ('hd_pi_mitad', 0x8201b49c)]:
        b = imagen[direccion-0x82000000:direccion-0x82000000+4]
        constantes[nombre] = dict(direccion=f'{direccion:08X}', bits=b.hex(),
                                 valor=struct.unpack('>f', b)[0])
    cuerpos = [cuerpo_hd(imagen, simbolo, archivo) for simbolo, archivo in [
        ('82139BD8', 'dbz3_recomp.12.cpp'), ('820C8068', 'dbz3_recomp.4.cpp'),
        ('8208F438', 'dbz3_recomp.30.cpp'), ('8208F7F8', 'dbz3_recomp.10.cpp'),
        ('821391A0', 'dbz3_recomp.14.cpp'), ('82139430', 'dbz3_recomp.20.cpp')]]
    cola = [(r['mnemonic'], r['operandos'].replace(' ', ''))
            for r in cuerpos[0]['instrucciones'] if r['mnemonic'] in ('fsubs', 'fmuls', 'fmadds')]
    exigir(cola[-4:] == [('fsubs', 'f0,f0,f11'), ('fmuls', 'f13,f11,f13'),
                         ('fmadds', 'f0,f0,f12,f13'), ('fmuls', 'f1,f0,f31')],
           'Fórmula HD final distinta de la examinada')
    llamadas_extra = [r['destino'] for r in cuerpos[4]['llamadas_directas']]
    exigir('8208F438' in llamadas_extra, '391A0 sin el dibujo adicional examinado')
    return dict(elf_sha256=sha(elf), imagen_sha256=sha(imagen), gp=gp,
                pal=dict(funcion='00266A50', offset=off, bytes_sha256=sha(pal), acc=acc),
                constantes=constantes, cuerpos_hd=cuerpos,
                formulas=dict(pal_cerca='P*pal_min', pal_lejos='P*pal_max',
                    pal_intermedia='P*S*pal_max+(1-S)*pal_min',
                    P='((FOV*360)/f32(0x40C90FDB))/45',
                    S='FUN_0011D7E0(((distancia-10)/170)*f32(0x3FC90FDB))',
                    hd_cerca='H*hd_min', hd_lejos='H*hd_max',
                    hd_intermedia='H*((1-T)*hd_min+T*hd_max)',
                    H='FOV*hd_factor_fov',
                    T='sub_822F66D0((distancia-10)*hd_inverso_170*hd_pi_mitad)'),
                ejemplos_algebraicos=ejemplos(constantes), referencias=REFERENCIAS,
                limites=[
                    'Lectura estática; no acredita ejecución ni equivalencia del dibujo.',
                    'S y T se mantienen simbólicos: no se ha probado su identidad matemática.',
                    'Ejemplos: factor y retorno auxiliar igualados; aritmética racional exacta.',
                    'No reproduce redondeo, acumulador ni casos especiales de la FPU R5900/PPC.',
                     'Los generados delimitan cuerpos; no se verifica aquí su traducción C++.',
                    'Las llamadas indirectas conservan destino desconocido; no se infiere ejecución.',
                     'Selección final de luces, matrices, estados y eliminación siguen abiertos.',
                ], aceptacion_escena=False)


def guardar(salida, datos):
    # Serializar antes de abrir: un error de datos no deja un archivo incompleto.
    texto = json.dumps(datos, ensure_ascii=False, indent=2, allow_nan=False)+'\n'
    with salida.open('x', encoding='utf-8') as f:
        f.write(texto)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', type=Path, required=True)
    p.add_argument('--imagen', type=Path, required=True)
    p.add_argument('--salida', type=Path, required=True)
    args = p.parse_args()
    if args.salida.exists():
        raise FileExistsError(args.salida)
    fuente = E.iso.Iso(str(args.iso))
    try:
        elf = fuente.read(fuente.boot_elf())
    finally:
        fuente._f.close()
    datos = auditar(elf, args.imagen.read_bytes())
    guardar(args.salida, datos)
    n = sum(len(c['instrucciones']) for c in datos['cuerpos_hd'])
    print(f'Contrato acotado verificado: 2 instrucciones ACC y {n} instrucciones PPC.')
    print('PAL .015/.35; HD .01/.30; fórmulas intermedias distintas. Escena no aceptada.')
    print(args.salida)


if __name__ == '__main__':
    main()

"""Coteja estados/ancla con escrituras del ELF EU y prueba el selector C++."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

SHA_EU = 'afe8c7b99f31e635750a17618ae8248d1a64fe3ad0669996990abdf33e1819f4'
# nombre port, funcion EU, estado, codigo IW, codigo port.
ACCIONES = (
    ('carga', 0x2A3B10, 5, 2, 0x8D0),
    ('carga-fin', 0x2A37D0, 6, 4, 0x8D2),
    ('salto-vuelo', 0x2A19C0, 0x11, 0x24, 0x824),
    ('despegue', 0x2A1760, 0x12, 0x25, 0x825),
    ('choque', 0x29FC60, 0x19, 0x3D, 0x83D),
    ('resbalon', 0x29E9A0, 0x1E, 0xCA, 0x8CA),
    ('golpe', 0x29E260, 0x20, 0x40, 0x840),
    ('golpe', 0x29CC90, 0x27, 0x43, 0x843),
    ('golpe', 0x29CA70, 0x28, 0x44, 0x844),
    ('golpe', 0x29C840, 0x29, 0x45, 0x845),
    ('muerte', 0x29C6A0, 0x2A, 0x46, 0x846),
    ('muerte', 0x29C530, 0x2B, 0x47, 0x847),
)


def escrituras(elf, inicio, fin):
    """Solo hechos locales: inmediato construido junto al store, sin emular ramas.

    No propaga constantes a traves de llamadas/ramas ni identifica objetos por
    el nombre del registro. Conserva desconocidos y bytes para revision.
    """
    ph, = struct.unpack_from('<I', elf, 28)
    paso, n = struct.unpack_from('<HH', elf, 42)
    bloques = [struct.unpack_from('<8I', elf, ph+i*paso) for i in range(n)]
    for tipo, off, va, _, tam, *_ in bloques:
        if tipo == 1 and va <= inicio < fin <= va+tam:
            datos = elf[off+inicio-va:off+fin-va]
            break
    else:
        raise ValueError('Funcion fuera de PT_LOAD')
    rows = []
    for o in range(0, len(datos), 4):
        word, = struct.unpack_from('<I', datos, o)
        op, rs, rt, imm = word>>26, (word>>21)&31, (word>>16)&31, word&65535
        if op not in (0x29, 0x2B) or imm not in (0xCBC, 0xB60, 0x1C8):
            continue
        valor, constructor = (0, None) if rt == 0 else (None, None)
        # Ultima definicion local del registro, sin atravesar control de flujo.
        # El compilador intercala cargas de otros registros antes del store.
        if rt:
            for p in range(o-4, max(-1, o-132), -4):
                prev, = struct.unpack_from('<I', datos, p)
                pop, prs, prt, pim = prev>>26, (prev>>21)&31, (prev>>16)&31, prev&65535
                if pop in (1,2,3,4,5,6,7,0x14,0x15,0x16,0x17) or (pop==0 and prev&63 in (8,9)):
                    # Un store en el delay-slot lee ANTES de transferir control,
                    # tambien si la instruccion anterior es jal BonesMovement.
                    if p==o-4 and rt!=31:
                        continue
                    break
                if pop==0 and ((prev>>11)&31)==rt and prev&63 not in (0x18,0x19,0x1A,0x1B):
                    break
                escribe_rt = pop not in (0,0x11,0x12,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,0x31,0x39)
                if escribe_rt and prt==rt:
                    if prs == 0 and pop in (9, 0x19, 0x0D):
                        valor = pim if pop == 0x0D or pim < 32768 else pim-65536
                        constructor = hex(inicio+p)
                    break
        rows.append(dict(pc=hex(inicio+o), offset=hex(imm), base=rs,
            registro=rt, valor=valor, constructor=constructor, word=hex(word),
            contexto=[dict(pc=hex(inicio+p), word=hex(struct.unpack_from('<I',datos,p)[0]))
                for p in range(max(0,o-32),o+4,4)] if valor is None else []))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, required=True)
    ap.add_argument('--decomp', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--inventario', action='store_true', help='Retener solo escrituras/bytes sin ejecutar pruebas')
    a = ap.parse_args()
    a.salida.mkdir(exist_ok=False)
    elf = a.elf.read_bytes()
    digest = hashlib.sha256(elf).hexdigest()
    if digest != SHA_EU:
        raise ValueError('No es el ELF EU autenticado')
    text = a.decomp.read_text(encoding='utf-8')
    starts = sorted(set(int(x, 16) for x in re.findall(r'^// ==== ([0-9a-fA-F]{8}) ', text, re.M)))
    evidencia, casos = [], []
    for nombre, entrada, estado, codigo_iw, codigo_port in ACCIONES:
        fin = next(s for s in starts if s > entrada)
        rows = escrituras(elf, entrada, fin)
        if a.inventario:
            evidencia.append(dict(nombre=nombre, entrada=hex(entrada), fin=hex(fin), escrituras=rows))
            continue
        estados = [r['valor'] for r in rows if r['offset'] == '0xcbc']
        codigos = [r['valor'] for r in rows if r['offset'] == '0xb60']
        modos = [r['valor'] for r in rows if r['offset'] == '0x1c8']
        # En variantes con branch-likely dejamos el store nativo desconocido.
        # La correspondencia de clip la respalda la decompilacion, ademas del
        # literal construido junto al store; no se afirma haber emulado la rama.
        funcion = text.split(f'// ==== {entrada:08x} ',1)[1].split('// ==== ',1)[0]
        derivados = [int(v,0) for v in re.findall(r'\+ 0xb60\) = (0x[0-9a-f]+|\d+);',funcion)]
        literales = []
        for r in rows:
            if r['offset'] != '0xb60':
                continue
            for c in r['contexto']:
                w = int(c['word'],16)
                if w>>26 in (9,0x19,0x0D) and (w>>21)&31 == 0 and (w>>16)&31 == r['registro']:
                    literales.append(w&65535)
        codigo_acreditado = codigo_iw in codigos or (codigo_iw in derivados and codigo_iw in literales)
        if estados != [estado] or not codigo_acreditado:
            raise ValueError(f'No acreditado en ELF: {nombre} {hex(entrada)}: {rows}')
        if any(m not in (0, 1) for m in modos):
            raise ValueError(f'Modo no resuelto en {hex(entrada)}: {rows}')
        modo = modos[-1] if modos else None
        evidencia.append(dict(nombre=nombre, entrada=hex(entrada), fin=hex(fin),
            estado=estado, codigo_iw=codigo_iw, codigo_port=codigo_port,
            modo=modo, escrituras=rows, codigos_decompilados=derivados,
            enlace_clip='store_nativo_local' if codigo_iw in codigos else 'decompilacion_y_literal_nativo'))
        for previo in (0, 1):
            casos.append((nombre, codigo_port, estado, previo, previo if modo is None else modo))
            if nombre == 'carga':
                casos.append(('carga-bucle', codigo_port+1, estado, previo, modo))
            if codigo_port == 0x840:
                if 0x41 not in codigos and not (0x41 in derivados and 0x41 in literales):
                    raise ValueError('Falta variante aerea en ELF')
                casos.append((nombre, 0x841, estado, previo, modo))
    if a.inventario:
        (a.salida/'escrituras.json').write_text(json.dumps(evidencia,indent=2),encoding='utf-8')
        print('Inventario nativo retenido:',a.salida/'escrituras.json')
        return 0
    # El bucle pertenece a5; solo cambia B60 a3 y no escribe CBC/1C8.
    bucle = escrituras(elf, 0x2A3940, 0x2A3B10)
    if not any(r['offset']=='0xb60' and r['valor']==3 for r in bucle):
        raise ValueError('No acreditado el codigo3 de carga')
    if any(r['offset'] in ('0xcbc','0x1c8') for r in bucle):
        raise ValueError('El bucle escribe estado o ancla inesperadamente')
    for nombre, codigo in (('desconocido', 0), ('<null>', 0), ('golpe', 0x999), ('muerte', 0x999)):
        for previo in (0, 1):
            casos.append((nombre, codigo, -1, previo, previo))
    fixture = ''.join(' '.join(map(str, c))+'\n' for c in casos)
    (a.salida/'casos.txt').write_text(fixture, encoding='utf-8')
    root = Path(__file__).resolve().parent.parent
    exe = a.salida/'iwx_estados.exe'
    subprocess.run(['C:/Program Files/LLVM/bin/clang++.exe', '-std=c++17',
        '-ffp-contract=off', str(root/'tools/iwx_estados.cpp'), '-o', str(exe)], check=True)
    run = subprocess.run([str(exe)], input=fixture, text=True, capture_output=True)
    (a.salida/'cpp_stdout.txt').write_text(run.stdout, encoding='utf-8')
    (a.salida/'cpp_stderr.txt').write_text(run.stderr, encoding='utf-8')
    resultado = dict(elf=str(a.elf), sha256=digest, acciones=evidencia,
        carga_bucle=bucle, cpp=json.loads(run.stdout), codigo=run.returncode,
        paridad_global=False, paridad_trayectoria=False)
    (a.salida/'resultado.json').write_text(json.dumps(resultado, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in resultado.items() if k not in ('acciones','carga_bucle')}))
    return run.returncode


if __name__ == '__main__':
    raise SystemExit(main())

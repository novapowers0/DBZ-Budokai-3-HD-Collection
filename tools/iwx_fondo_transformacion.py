"""Reejecucion EE acotada del fondo IW con RAM propia retenida.

Contrasta dependencia de la traslacion, no imagen GS ni ejecucion natural.
Las rutinas de matriz/proyeccion se ejecutan; dibujo y memset se interceptan.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import zipfile

from iwx_bubbles_eventos import cargar_elf, memoria_elf
from iwx_fondo_original import ELF_SHA


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, required=True)
    ap.add_argument('--original', type=Path, required=True)
    ap.add_argument('--oraculo', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    elf = cargar_elf(a.elf)
    reference_bytes = a.original.read_bytes()
    reference = json.loads(reference_bytes)
    if reference['elf_sha256'] != ELF_SHA:
        raise ValueError('Referencia distinta del ELF EU')
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('iwx_fondo_oraculo', a.oraculo)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    hooks = {0x11DEF8, 0x1281D0, 0x138830}
    executed = set()

    class FondoEE(oracle.EE):
        def __init__(self, ram):
            super().__init__(SimpleNamespace(elf=elf))
            self.mem[:] = ram
            self.selecciones = []
            self.servicios = []

        def get(self, addr, n=4):
            if not 0 <= addr <= len(self.mem)-n:
                raise ValueError(f'Lectura fuera de EE: {addr:X}+{n:X}')
            return super().get(addr, n)

        def put(self, addr, value, n=4):
            if not 0 <= addr <= len(self.mem)-n:
                raise ValueError(f'Escritura fuera de EE: {addr:X}+{n:X}')
            super().put(addr, value, n)

        def step(self, pc, in_delay=False):
            word = self.get(pc)
            if word != int.from_bytes(memoria_elf(elf, pc, 4), 'little'):
                raise ValueError(f'Instruccion distinta del ELF: {pc:X}')
            executed.add(pc)
            if word >> 26 == 0 and word & 63 == 4:
                rs, rt, rd = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
                if rd:
                    self.r[rd] = (self.r[rt] << (self.r[rs] & 31)) & 0xFFFFFFFF
                return None, False, False
            if word >> 26 == 0 and word & 63 == 0x22:
                rs, rt, rd = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
                value = oracle.S(self.r[rs])-oracle.S(self.r[rt])
                if not -(1 << 31) <= value < (1 << 31):
                    raise ValueError('SUB con overflow fuera del contrato')
                if rd:
                    self.r[rd] = value & 0xFFFFFFFF
                return None, False, False
            if word >> 26 == 0x12 and ((word >> 21) & 31) >= 16 and word & 0x7FF == 0x33C:
                # VMOVE: destino ft, fuente fs, mascara xyzw. Extension local;
                # conserva componentes sin mascara y el vf0 inmutable.
                dst, src, mask = (word >> 16) & 31, (word >> 11) & 31, (word >> 21) & 15
                values = self.v[src][:]
                if dst:
                    for i in range(4):
                        if mask & (8 >> i):
                            self.v[dst][i] = values[i]
                return None, False, False
            if pc == 0x141390:
                # Este punto configura el recurso: no es una observacion GS.
                self.selecciones.append(dict(recurso=self.r[4] & 0xFFFFFFFF,
                    seleccion=self.r[5] & 0xFFFFFFFF,
                    vista=self.mem[0x486090:0x4860D0].hex(),
                    matriz=[self.vec(0x486090+16*i) for i in range(4)]))
            target, delay, _ = super().step(pc, in_delay)
            # No usar los helpers de matrices interceptados por el oraculo base.
            # Aqui sus instrucciones originales deben ejecutarse.
            return target, delay, word >> 26 == 3 and target in hooks

        def hook(self, addr):
            if addr not in hooks:
                raise AssertionError(f'Servicio no previsto: {addr:X}')
            args = [r & 0xFFFFFFFF for r in self.r[4:7]]
            self.servicios.append(dict(destino=hex(addr), argumentos=args))
            if addr == 0x11DEF8:
                dst, value, size = args
                if not 0 <= dst <= len(self.mem)-size:
                    raise ValueError('Memset fuera de EE')
                self.mem[dst:dst+size] = bytes([value & 255])*size
                self.r[2] = dst

    variants = [(0., 0., 0., 1.), (123., 0., 0., 1.),
                (0., -456., 0., 1.), (0., 0., 789., 1.),
                (123., -456., 789., 1.)]
    rows = []
    for retained in reference['instantaneas']:
        state = Path(retained['estado'])
        if sha(state.read_bytes()) != retained['sha256']:
            raise ValueError('Cambio el guardado retenido')
        with zipfile.ZipFile(state) as z:
            ram = z.read('eeMemory.bin')
        if len(ram) != 0x2000000:
            raise ValueError('Faltan los 32MiB EE')
        u = lambda addr: struct.unpack_from('<I', ram, addr)[0]
        if u(0x4A4D40) != retained['n']:
            raise ValueError('Serial distinto de la referencia')
        stage = u(0x49CDB8)
        if not 0x100000 <= stage <= len(ram)-0xAE0 or not u(stage+0x630) & 8:
            raise ValueError('Esta prueba exige la rama de fondo flags8')
        original_view = [struct.unpack_from('<4f', ram, 0x486090+16*i) for i in range(4)]
        constant = struct.unpack_from('<4f', ram, 0x47DEC0)
        if constant != (0., 0., 0., 1.):
            raise ValueError('Constante de traslacion original inesperada')
        depth = u(0x49C704)
        if depth > 29:
            raise ValueError('Pila de matrices sin margen para el consumidor')
        cases = []
        # Variante retenida y perturbaciones explicitas: ninguna se presenta
        # como captura natural de esos valores de camara.
        for translation in [tuple(original_view[3]), *variants]:
            e = FondoEE(ram)
            e.pv(0x4860C0, translation)
            before_view = bytes(e.mem[0x486090:0x4860D0])
            before_stack = bytes(e.mem[0x49C700:0x49C708])
            n = e.run(0x306320)
            if len(e.selecciones) != 2:
                raise AssertionError(('No se observaron las dos selecciones de recurso', len(e.selecciones)))
            if bytes(e.mem[0x486090:0x4860D0]) != before_view:
                raise AssertionError('La vista no se restauro byte a byte')
            if bytes(e.mem[0x49C700:0x49C708]) != before_stack:
                raise AssertionError('La pila de matrices no se restauro')
            for draw in e.selecciones:
                if bytes.fromhex(draw['vista'])[:48] != ram[0x486090:0x4860C0]:
                    raise AssertionError('La base de vista cambio')
                if draw['matriz'][3] != [0., 0., 0., 1.]:
                    raise AssertionError(('La traslacion no fue anulada', draw['matriz'][3]))
            cases.append(dict(traslacion_entrada=translation, selecciones_recurso=e.selecciones,
                vista_restaurada=True, pila_restaurada=True, instrucciones=n,
                servicios_interceptados=e.servicios))
        if len({tuple(d['vista'] for d in c['selecciones_recurso']) for c in cases}) != 1:
            raise AssertionError('El fondo flags8 depende de la traslacion de entrada')
        # Control positivo: 12B820 por si solo SI conserva una traslacion no
        # nula (con conversion de ejes); evita atribuir un cero del interprete.
        control = FondoEE(ram)
        for i in range(4):
            control.v[28+i] = list(original_view[i])
        control.v[31] = [123., -456., 789., 1.]
        control.r[4] = 0
        control.run(0x12B820)
        if control.vec(0x4860C0) != [123., 456., -789., 1.]:
            raise AssertionError('Fallo el control positivo de traslacion')
        rows.append(dict(n=retained['n'], estado=str(state), sha256=retained['sha256'],
            flags=hex(u(stage+0x630)), casos=cases,
            control_12B820=control.vec(0x4860C0),
            traslacion_dibujo_independiente=True))
    ranges = []
    for pc in sorted(executed):
        if ranges and ranges[-1][1] == pc:
            ranges[-1][1] += 4
        else:
            ranges.append([pc, pc+4])
    report = dict(elf_sha256=ELF_SHA, referencia_sha256=sha(reference_bytes),
        herramienta_sha256=sha(Path(__file__).read_bytes()),
        oraculo_sha256=sha(a.oraculo.read_bytes()), instantaneas=rows,
        instrucciones_autenticadas=[dict(inicio=hex(lo), fin_exclusivo=hex(hi),
            sha256=sha(memoria_elf(elf, lo, hi-lo))) for lo, hi in ranges],
        pasa=True, ejecucion_natural=False, imagen_gs_comparada=False,
        paridad_port=False,
        limites=['Reejecucion local con RAM retenida y traslaciones sinteticas',
                 'Memset interceptado; envio GS y regulador del modelo omitidos',
                 'La semantica float32 acotada no certifica toda la FPU/VU de PS2',
                 'Solo rama flags8; no UV por material ni ecuacion completa del port'])
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida), pasa=True,
        casos=sum(len(r['casos']) for r in rows), controles=len(rows),
        instrucciones_distintas=len(executed), paridad_port=False)))


if __name__ == '__main__':
    main()

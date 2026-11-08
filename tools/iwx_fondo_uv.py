"""Contraste acotado del reparto UV del fondo884 con instrucciones IW EU.

Reejecuta306320 con RAM retenida; detiene el regulador antes de dibujar.
Despues ejecuta133770 y su callback con descriptores autenticos o controles
sinteticos. No ejecuta GS ni convierte esto en paridad visual del port.
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
    ap.add_argument('--modelos', type=Path, required=True)
    ap.add_argument('--oraculo', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    a = ap.parse_args()
    elf = cargar_elf(a.elf)
    model_bytes = a.modelos.read_bytes()
    models = json.loads(model_bytes)
    if models['fuente']['elf_sha256'] != ELF_SHA:
        raise ValueError('El inventario no pertenece a IW EU')
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('iwx_fondo_uv_oraculo', a.oraculo)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    executed = set()
    reads = set()
    hooks = {0x11DEF8, 0x1281D0, 0x138830}

    class FondoUVEE(oracle.EE):
        def __init__(self, ram):
            super().__init__(SimpleNamespace(elf=elf))
            self.mem[:] = ram
            self.publicaciones = []
            self.recurso = None

        def get(self, addr, n=4):
            if not 0 <= addr <= len(self.mem)-n:
                raise ValueError(f'Lectura fuera de EE: {addr:X}+{n:X}')
            if 0x4C4E20 <= addr < 0x4C4EA0:
                reads.add(addr)
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
                dst, src, mask = (word >> 16) & 31, (word >> 11) & 31, (word >> 21) & 15
                values = self.v[src][:]
                if dst:
                    for i in range(4):
                        if mask & (8 >> i):
                            self.v[dst][i] = values[i]
                return None, False, False
            if pc == 0x141390:
                self.recurso = self.r[4] & 0xFFFFFFFF
            target, delay, _ = super().step(pc, in_delay)
            return target, delay, word >> 26 == 3 and target in hooks

        def hook(self, addr):
            if addr not in hooks:
                raise AssertionError(f'Servicio no previsto: {addr:X}')
            if addr == 0x11DEF8:
                dst, value, size = [r & 0xFFFFFFFF for r in self.r[4:7]]
                if not 0 <= dst <= len(self.mem)-size:
                    raise ValueError('Memset fuera de EE')
                self.mem[dst:dst+size] = bytes([value & 255])*size
                self.r[2] = dst
            elif addr == 0x138830:
                self.publicaciones.append(dict(modelo=self.r[4] & 0xFFFFFFFF,
                    recurso=self.recurso, modo=self.get(0x49C66C),
                    uv_bits=[self.get(0x4C4E20+4*i) for i in range(32)],
                    selectores=[self.get(p) for p in (0x4AB670, 0x4AB678, 0x4AB67C)]))

    def caso(ram, publication, flags, sentinel):
        e = FondoUVEE(ram)
        e.put(0x49C66C, publication['modo'])
        for addr, value in zip((0x4AB670, 0x4AB678, 0x4AB67C), publication['selectores']):
            e.put(addr, value)
        for i, value in enumerate(publication['uv_bits']):
            e.put(0x4C4E20+4*i, value)
        if sentinel:
            for i in range(16):
                e.pf(0x4C4E20+4*i, (i+1)/8)
                e.pf(0x4C4E60+4*i, -(i+1)/16)
        # Cache invalida y paquetes privados: no se presenta como llamada natural.
        e.put(0x49C644, 0xFFFFFFFF)
        e.put(0x1D00000, 0x1D00100)
        e.r[4], e.r[5], e.r[6] = 0x1D00000, 7, flags
        reads.clear()
        instructions = e.run(0x133770, limit=1000)
        callback = e.get(0x4AB674)
        packet = 0x1D00200
        e.mem[packet:packet+0x100] = bytes([0xA5])*0x100
        before = bytes(e.mem[packet:packet+0x100])
        if callback:
            if not 0x1B0A40 <= callback < 0x1B0CD0:
                raise AssertionError(('Callback fuera del contrato', hex(callback)))
            e.r[4] = packet
            instructions += e.run(callback, limit=1000)
        addresses = sorted(reads)
        channel = None
        if addresses:
            if len(addresses) != 2 or addresses[1]-addresses[0] != 0x40:
                raise AssertionError(('Lecturas UV no clasificadas', addresses))
            channel = (addresses[0]-0x4C4E20)//4
            if bytes(e.mem[packet+0x88:packet+0x90]) != b''.join(
                    struct.pack('<I', e.get(p)) for p in addresses):
                raise AssertionError('El callback no copio UV bit a bit')
        changed = [i for i, (x, y) in enumerate(zip(before, e.mem[packet:packet+0x100])) if x != y]
        if callback and (channel is None or any(i not in range(0x88, 0x90) for i in changed)):
            raise AssertionError('Escrituras fuera del contrato de desplazamiento UV')
        return dict(flags=hex(flags), control_sintetico=sentinel,
            callback=hex(callback), canal=channel,
            lecturas=[hex(p) for p in addresses],
            uv_bits=[e.get(packet+0x88), e.get(packet+0x8C)] if callback else None,
            copia_byteidentica=bool(callback), instrucciones=instructions)

    rows = []
    flags_used = sorted({p['vtype'] for g in models['geometria_fuente'].values() for p in g['partes']})
    for retained in models['instantaneas']:
        path = Path(retained['estado'])
        if sha(path.read_bytes()) != retained['sha256']:
            raise ValueError('Cambio el guardado retenido')
        with zipfile.ZipFile(path) as z:
            ram = z.read('eeMemory.bin')
        u = lambda addr: struct.unpack_from('<I', ram, addr)[0]
        if len(ram) != 0x2000000 or u(0x4A4D40) != retained['n']:
            raise ValueError('RAM o serial distinto')
        table = memoria_elf(elf, 0x493850, 64)
        if ram[0x493850:0x493890] != table:
            raise ValueError('La tabla de despacho difiere del ELF')
        for child, geometry in models['geometria_fuente'].items():
            leaf = next(r for r in retained['hojas'] if r['indice'] == int(child))
            if sha(ram[leaf['direccion']:leaf['direccion']+leaf['bytes']]) != leaf['ram_sha256']:
                raise ValueError('Inventario RAM distinto')
            for part in geometry['partes']:
                if u(leaf['direccion']+part['offset']) != part['vtype']:
                    raise ValueError('Los flags de parte cambiaron en RAM')
        e = FondoUVEE(ram)
        e.run(0x306320)
        if len(e.publicaciones) != 2:
            raise AssertionError('No se observaron dos llamadas al regulador')
        cases = []
        for publication in e.publicaciones:
            selected = next(s for s in retained['slots'] if s['modelo'] == publication['modelo'])
            if selected['hijo_modelo'] not in (1, 3):
                raise AssertionError('El regulador selecciono otro modelo')
            per_flags = [caso(ram, publication, flags, False) for flags in flags_used]
            controls = [caso(ram, publication, (0x800008B6 & ~0x3C00000) | (k << 22), True)
                        for k in range(6, 10)]
            if [c['canal'] for c in controls] != [6, 7, 8, 9]:
                raise AssertionError(('Canales sinteticos inesperados', controls))
            mapping = {int(c['flags'], 16): c['canal'] for c in per_flags}
            geometry = models['geometria_fuente'][str(selected['hijo_modelo'])]
            parts = [dict(offset=p['offset'], hueso=p['hueso'], textura=p['textura'],
                          vertices=p['vertices'], flags=hex(p['vtype']), canal=mapping[p['vtype']])
                     for p in geometry['partes']]
            cases.append(dict(slot=selected['indice'], hijo_modelo=selected['hijo_modelo'],
                publicacion=publication, descriptores=per_flags, controles=controls, partes=parts))
        rows.append(dict(n=retained['n'], estado=str(path), sha256=retained['sha256'], modelos=cases))
    ranges = []
    for pc in sorted(executed):
        if ranges and ranges[-1][1] == pc:
            ranges[-1][1] += 4
        else:
            ranges.append([pc, pc+4])
    report = dict(elf_sha256=ELF_SHA, modelos_sha256=sha(model_bytes),
        herramienta_sha256=sha(Path(__file__).read_bytes()), oraculo_sha256=sha(a.oraculo.read_bytes()),
        instantaneas=rows,
        tabla_despacho=dict(direccion='0x493850', sha256=sha(table),
                           destinos=[hex(x) for x in struct.unpack('<16I', table)]),
        instrucciones_autenticadas=[dict(inicio=hex(lo), fin_exclusivo=hex(hi),
            sha256=sha(memoria_elf(elf, lo, hi-lo))) for lo, hi in ranges],
        pasa=True, ejecucion_natural=False, imagen_gs_comparada=False, paridad_port=False,
        limites=['RAM retenida y reejecucion offline; no historial natural de llamadas',
                 '306320 intercepta memset, envio GS y regulador; captura sus entradas',
                 '133770 se prueba con cache invalida, modo7 y paquetes privados',
                 'Controles cambian bits de selector y globals UV; no son estados capturados',
                 'Solo partes visibles recuperadas por el parser de hijos1/3',
                 'El callback copia parametros de paquete; no ejecuta microprograma VU ni GS'])
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida), pasa=True, instantaneas=len(rows),
        casos=sum(len(m['descriptores'])+len(m['controles']) for r in rows for m in r['modelos']),
        instrucciones_distintas=len(executed), reparto=[dict(hijo=m['hijo_modelo'],
            partes=len(m['partes']), partes_con_canal=[p for p in m['partes'] if p['canal'] is not None])
            for m in rows[0]['modelos']], paridad_port=False)))


if __name__ == '__main__':
    main()

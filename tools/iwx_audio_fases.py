"""Solicitudes ADX de las fases de Serpiente: instrucciones EU, entradas sinteticas.

No clasifica el contenido audible de las hojas ni ejecuta los servicios de escena.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace

from iwx_bubbles_eventos import cargar_elf, llamadas, memoria_elf
from iwx_fondo_original import ELF_SHA


RANGOS = {
    0x31: (0x29A7C0, 0x29A8F0),
    0x2C: (0x29BFF0, 0x29C4C0),
    0x2E: (0x29B3B0, 0x29B890),
    0x30: (0x29AA90, 0x29AFA0),
    0x2F: (0x29B120, 0x29B270),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, required=True)
    ap.add_argument('--inventario', action='store_true')
    ap.add_argument('--bancos', type=Path)
    ap.add_argument('--oraculo', type=Path)
    ap.add_argument('--salida', type=Path)
    a = ap.parse_args()
    elf = cargar_elf(a.elf)
    inventory = {hex(state): dict(inicio=hex(lo), fin_exclusivo=hex(hi),
        sha256=sha(memoria_elf(elf, lo, hi-lo)), llamadas=llamadas(elf, lo, hi))
        for state, (lo, hi) in RANGOS.items()}
    if a.inventario:
        print(json.dumps(dict(elf_sha256=ELF_SHA, fases=inventory), indent=1))
        return
    if not a.oraculo or not a.salida or not a.bancos:
        ap.error('El contraste requiere --oraculo, --bancos y --salida')
    bank_bytes = a.bancos.read_bytes()
    banks = json.loads(bank_bytes)
    resource, = struct.unpack('<I', memoria_elf(elf, 0x484760+2*4, 4))
    if banks['fuente']['elf_sha256'] != ELF_SHA or resource != 941:
        raise ValueError('Referencia de bancos o recurso de Serpiente distinto')
    leaves = next(b['hojas'] for b in banks['bancos'] if b['entrada'] == resource)
    if [r['indice'] for r in leaves] != list(range(7)):
        raise ValueError('Banco Serpiente distinto de las siete hojas retenidas')
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('iwx_fases_oraculo', a.oraculo)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    actor, work, mini, manager, camera, node = (
        0x800000, 0x810000, 0x820000, 0x830000, 0x840000, 0x850000)
    hooks = {0x298280, 0x29A5D0, 0x28A9D0, 0x296F20, 0x2972D0,
             0x238670, 0x28A980, 0x35EB60, 0x323820, 0x35EA50,
             0x3050A0, 0x1FCE00, 0x2997F0, 0x2A80B0, 0x11DEF8,
             0x2F7950, 0x31B280, 0x3389D0}
    executed = set()

    class FaseEE(oracle.EE):
        def __init__(self):
            super().__init__(SimpleNamespace(elf=elf))
            self.eventos = []

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
            target, delay, _ = super().step(pc, in_delay)
            return target, delay, word >> 26 == 3 and target in hooks

        def hook(self, addr):
            if addr not in hooks:
                raise AssertionError(f'Servicio no previsto: {addr:X}')
            args = [r & 0xFFFFFFFF for r in self.r[4:8]]
            self.eventos.append(dict(destino=hex(addr), argumentos=args))
            if addr == 0x11DEF8:
                dst, value, size = args[:3]
                if not 0 <= dst <= len(self.mem)-size:
                    raise ValueError('Memset fuera de EE')
                self.mem[dst:dst+size] = bytes([value & 255])*size
                self.r[2] = dst
            else:
                getters = {0x298280: camera, 0x29A5D0: work,
                           0x28A9D0: mini, 0x296F20: manager,
                           0x323820: manager, 0x3050A0: node}
                self.r[2] = getters.get(addr, 0)

    expected_audio = {
        0x31: {p: [[0, 1]] for p in (0, 0xB, 0xC, 0xD)},
        0x2C: {0: [], 0xB: [], 0xC: [[4, 0]], 0xD: []},
        0x2E: {0: [[1, 0]], 0xB: [], 0xC: [], 0xD: [[5, 0]]},
        0x30: {0: [[2, 0]], 0xB: [[3, 0]], 0xC: [[3, 0]], 0xD: [[6, 0]]},
        0x2F: {p: [[2, 0]] for p in (0, 0xB, 0xC, 0xD)},
    }
    expected_camera = {
        0x31: {p: 0 for p in (0, 0xB, 0xC, 0xD)},
        0x2C: {0: 700, 0xB: 709, 0xC: 703, 0xD: 706},
        0x2E: {0: 701, 0xB: 710, 0xC: 704, 0xD: 707},
        0x30: {0: 702, 0xB: 711, 0xC: 705, 0xD: 708},
        0x2F: {p: 30 for p in (0, 0xB, 0xC, 0xD)},
    }
    rows = []
    for state, (start, _) in RANGOS.items():
        for profile in (0, 0xB, 0xC, 0xD):
            for side in (0, 1):
                e = FaseEE()
                e.r[4] = actor
                e.put(0x49CA70, manager)
                e.put(actor+0x1380, side)
                e.put(actor+0xB8C, profile)
                e.put(actor+0xCBC, 0x31, 2)
                e.put(mini+4, 2)
                e.put(manager+0x7EC, 0x123456)
                e.put(manager+0x25C, 0x123456)
                n = e.run(start)
                audio = [x['argumentos'][:2] for x in e.eventos if x['destino'] == '0x35eb60']
                stops = [x['argumentos'][0] for x in e.eventos if x['destino'] == '0x35ea50']
                cameras = [x['argumentos'][1] for x in e.eventos if x['destino'] == '0x2972d0']
                expected = expected_audio[state][profile] if side == 0 else []
                if (audio != expected or stops != ([1] if state == 0x2C else [])
                        or cameras != [expected_camera[state][profile]]
                        or e.get(actor+0xCBC, 2) != state):
                    raise AssertionError(('Fase distinta', hex(state), profile, side,
                                          audio, stops, cameras))
                if any(leaf >= len(leaves) for leaf, _ in audio):
                    raise AssertionError('Hoja fuera del banco Serpiente')
                rows.append(dict(estado=hex(state), perfil=profile, lado=side,
                    solicitudes=audio, paradas=stops, camaras=cameras,
                    codigo_actor=e.get(actor+0xB60), instrucciones=n,
                    servicios_interceptados=e.eventos))
    report = dict(elf_sha256=ELF_SHA, herramienta_sha256=sha(Path(__file__).read_bytes()),
        oraculo_sha256=sha(a.oraculo.read_bytes()), bancos_sha256=sha(bank_bytes),
        indice_minijuego=2, recurso=resource,
        hojas=[dict(indice=r['indice'], sha256=r['sha256'], adx=r.get('adx')) for r in leaves],
        fases=inventory, casos=rows, pasa=True,
        instrucciones_distintas=len(executed), ejecucion_natural=False,
        contenido_escuchado=False, ambiente_identificado=False, paridad_port=False,
        limites=['Entradas sinteticas: indice2, perfiles0/B/C/D y lados0/1',
                 'Servicios de escena, animacion, camara y audio interceptados',
                 'Sin clasificar voces/musica/ambiente ni comparar PCM o mezcla',
                 'El archivo de bancos es una referencia retenida, no una nueva extraccion'])
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida), pasa=True, casos=len(rows),
        hojas_solicitadas=sorted({leaf for row in rows for leaf, _ in row['solicitudes']}),
        instrucciones_distintas=len(executed), ambiente_identificado=False)))


if __name__ == '__main__':
    main()

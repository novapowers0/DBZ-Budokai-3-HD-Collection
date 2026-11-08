"""Inspeccion y ejecucion acotada del creador de objetos Bubbles IW EU.

Solo lee el ELF y el interprete externo. Las salidas van a un directorio nuevo.
No crea objetos en el juego ni acredita su dibujo, sonido o trayectoria.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

from iwx_bubbles_eventos import cargar_elf, memoria_elf, llamadas, ELF_SHA

sys.dont_write_bytecode = True
INICIO, FIN = 0x3177F0, 0x317B70


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, required=True)
    ap.add_argument('--inventario', action='store_true')
    ap.add_argument('--ventanas', action='store_true', help='incluir ensamblador de las llamadas')
    ap.add_argument('--oraculo', type=Path)
    ap.add_argument('--salida', type=Path)
    args = ap.parse_args()
    data = cargar_elf(args.elf)
    inv = dict(elf_sha256=ELF_SHA, inicio=hex(INICIO), fin_exclusivo=hex(FIN),
               sha256=hashlib.sha256(memoria_elf(data, INICIO, FIN-INICIO)).hexdigest(),
               llamadas=llamadas(data, INICIO, FIN))
    inv['desvio_llamadas'] = llamadas(data, 0x3176F0, INICIO)
    # 499630 es la etiqueta ASCII del task, no una tabla de callbacks.
    inv['nombre_tarea'] = memoria_elf(data, 0x499630, 32).split(b'\0', 1)[0].decode('ascii')
    inv['callback'] = dict(inicio='0x31a2f0', fin_exclusivo='0x31a570',
                           sha256=hashlib.sha256(memoria_elf(data, 0x31A2F0, 0x280)).hexdigest(),
                           llamadas=llamadas(data, 0x31A2F0, 0x31A570))
    if args.inventario:
        if not args.ventanas:
            print(json.dumps(inv, indent=2))
            return
        # La firma del decomp pierde registros: conservar instrucciones crudas y
        # una ventana antes de cada llamada, tambien las que el desensamblador
        # generico no reconoce como instrucciones EE.
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_MIPS,
                         capstone.CS_MODE_MIPS64 | capstone.CS_MODE_LITTLE_ENDIAN)
        inv['ventanas'] = []
        for call in inv['llamadas']:
            pc = int(call['pc'], 16)
            lines = []
            for addr in range(max(INICIO, pc-24), pc+8, 4):
                raw = memoria_elf(data, addr, 4)
                word, = struct.unpack('<I', raw)
                decoded = list(md.disasm(raw, addr, count=1))
                text = ('EE LQ/SQ' if word >> 26 in (0x1e, 0x1f) else
                        'EE LQC2/SQC2' if word >> 26 in (0x36, 0x3e) else
                        'EE COP2/VU (palabra cruda)' if word >> 26 == 0x12 else
                        decoded[0].mnemonic+' '+decoded[0].op_str if decoded else 'EE/desconocido')
                lines.append(f'{addr:08X} {raw.hex()} {text}')
            inv['ventanas'].append(dict(llamada=call, instrucciones=lines))
        inv['helpers_vectoriales'] = []
        for addr in range(0x142470,0x142570,4):
            raw=memoria_elf(data,addr,4)
            decoded=list(md.disasm(raw,addr,count=1))
            text=decoded[0].mnemonic+' '+decoded[0].op_str if decoded else 'EE/desconocido'
            inv['helpers_vectoriales'].append(f'{addr:08X} {raw.hex()} {text}')
        print(json.dumps(inv, indent=2))
        return
    if not args.oraculo or not args.salida:
        ap.error('la ejecucion requiere --oraculo y --salida')
    spec = importlib.util.spec_from_file_location('iwx_objetos_ee', args.oraculo)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    from types import SimpleNamespace
    actor, target, work, obj = 0x800000, 0x810000, 0x820000, 0x830000
    externos = {0x29A5D0, 0x140460, 0x140300, 0x11DEF8, 0x240740, 0x127CA0}

    class CreadorEE(oracle.EE):
        def __init__(self, azar):
            super().__init__(SimpleNamespace(elf=data))
            self.servicios = []
            self.azar = azar

        def step(self, pc, in_delay=False):
            result = super().step(pc, in_delay)
            w = self.get(pc)
            if w >> 26 == 3 and ((w & 0x3FFFFFF) << 2) in externos:
                return result[0], result[1], True
            return result

        def hook(self, addr):
            if addr not in externos:
                return super().hook(addr)
            self.servicios.append(dict(destino=hex(addr), registros=self.r[4:10],
                                        f12_bits=self.f[12], f13_bits=self.f[13]))
            if addr == 0x29A5D0:
                self.r[2] = work-0x260
            elif addr == 0x140300:
                self.r[2] = obj
            elif addr == 0x11DEF8:
                p, value, count = self.r[4:7]
                if not (obj <= p < obj+0x200 and 0 <= count <= obj+0x200-p):
                    raise AssertionError(('memset fuera del objeto', p, count))
                self.mem[p:p+count] = bytes([value & 255])*count
                self.r[2] = p
            elif addr == 0x127CA0:
                self.r[2] = self.azar
            # Eliminar tarea previa y emitir sonido: conservar solicitud, sin
            # ejecutar gestor ni SPU. La reserva del objeto comienza a ceros.

    args.salida.mkdir(exist_ok=False)
    cases = []
    for typ in range(5):
        for azar in (0, 1):
            for index in (0, 47):
                for ocupado in (0, 1):
                    ee = CreadorEE(azar)
                    ee.r[4], ee.r[5] = actor, typ
                    ee.f[12] = oracle.U(.3)
                    ee.put(actor+0x1380, 1)
                    ee.put(actor+0x1384, target)
                    ee.pv(actor+0x640, [10., 2., -20., 1.])
                    ee.pv(actor+0x730, [12., 8., -18., 1.])
                    ee.pv(target+0x640, [40., 1., 50., 1.])
                    ee.pv(target+0x6A0, [42., 11., 54., 1.])
                    ee.put(work+0x198, index, 2)
                    if ocupado:
                        ee.put(work+0xBC+index*4, 0x840000)
                    n = ee.run(INICIO)
                    cases.append(dict(tipo=typ, azar=azar, indice=index, ocupado=ocupado,
                                      instrucciones=n, indice_siguiente=ee.get(work+0x198, 2),
                                      puntero=ee.get(work+0xBC+index*4),
                                       objeto_hex=bytes(ee.mem[obj:obj+0x70]).hex(),
                                      posicion=ee.vec(obj+0x30), velocidad=ee.vec(obj+0x40),
                                      limite=ee.fv(obj+0x54), servicios=ee.servicios))
    report = dict(inventario=inv, casos=cases, normal=False,
                  oraculo_sha256=hashlib.sha256(args.oraculo.read_bytes()).hexdigest(),
                  alcance='creacion original con reserva/memset/RNG/sonido interceptados; no callbacks, vuelo, imagen o sonido fisico')
    (args.salida/'original.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(dict(casos=len(cases), salida=str(args.salida), normal=False,
                         muestras=[c for c in cases if c['azar']==0 and c['indice']==0 and c['ocupado']==0]), indent=2))


if __name__ == '__main__':
    main()

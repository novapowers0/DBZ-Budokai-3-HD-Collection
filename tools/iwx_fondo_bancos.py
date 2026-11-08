"""Identifica bancos candidatos del sonido de intro sin reproducirlos.

941 procede de tabla484760/indice2; 938 y954 de las llamadas302EDC/302F00.
La carga de un banco no demuestra ambiente ni reproduccion de sus hojas.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

from assets_censo import Game
from iwx_censo import entry, child, u32
from iwx_fondo_original import ELF_SHA, leer


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fuente', type=Path, required=True)
    ap.add_argument('--original', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    args = ap.parse_args()
    original_bytes = args.original.read_bytes()
    original = json.loads(original_bytes)
    if original['elf_sha256'] != ELF_SHA:
        raise ValueError('Referencia distinta del ELF EU esperado')
    game = Game('iw_eu', args.fuente)
    if game.source['elf_sha256'] != ELF_SHA:
        raise ValueError('Fuente distinta del ELF EU esperado')
    cmn = next(p for p in game.files if Path(p).name.upper() == 'DATA_CMN.AFS')
    states, memories, resources = [], [], {938, 954}
    for retained in original['instantaneas']:
        path = Path(retained['estado'])
        if hashlib.sha256(path.read_bytes()).hexdigest() != retained['sha256']:
            raise ValueError('Cambio la instantanea retenida')
        with zipfile.ZipFile(path) as z:
            ram = z.read('eeMemory.bin')
        row = leer(lambda addr, size: ram[addr:addr+size])
        # Tuplas de la lectura pasan a listas en JSON.
        if json.loads(json.dumps(row)) != {k: v for k, v in retained.items()
                                           if k not in ('estado', 'sha256', 'imagen_retenida')}:
            raise ValueError('No coincide la lectura retenida')
        candidate = row.get('carga_audio_minijuego_derivada')
        if candidate:
            resources.add(candidate['recurso'])
        ptr = struct.unpack_from('<I', ram, 0x49CEA0)[0]
        context = None
        if 0x100000 <= ptr <= 0x2000000-0xCC:
            count, handle, bundle = struct.unpack_from('<3I', ram, ptr)
            if count > 16:
                raise ValueError('Contexto ADX con mas entradas de las esperadas')
            context = dict(puntero=ptr, entradas=count, carga=handle, paquete=bundle,
                ids=list(struct.unpack_from('<%dI' % count, ram, ptr+0xC)),
                slots=list(struct.unpack_from('<16I', ram, ptr+0x50)))
        states.append(dict(n=row['n'], sha256=retained['sha256'],
            contexto_adx=context, constante_traslacion=list(struct.unpack_from('<4f', ram, 0x47DEC0)),
            factor_vista=list(struct.unpack_from('<4f', ram, 0x489FF0))))
        memories.append((ram, row))
    banks = []
    for index in sorted(resources):
        pack = entry(game, cmn, index)
        if pack[:4] != b'#AMB' or u32(pack, 16) > 64:
            raise ValueError('Banco candidato no es AMB acotado')
        leaves = []
        for i in range(u32(pack, 16)):
            raw = child(pack, i)
            item = dict(indice=i, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                        cabecera=raw[:32].hex())
            matches = []
            for state, (ram, row) in zip(states, memories):
                context = state['contexto_adx']
                if context and i < len(context['ids']):
                    address = context['ids'][i]
                    if 0x100000 <= address <= len(ram)-len(raw) and ram[address:address+len(raw)] == raw:
                        matches.append(dict(n=state['n'], tipo='contexto_adx', direccion=address))
                for slot in row['slots_sonido']:
                    for column in ('tabla_a', 'tabla_b'):
                        address = slot[column]
                        if 0x100000 <= address <= len(ram)-len(raw) and ram[address:address+len(raw)] == raw:
                            matches.append(dict(n=state['n'], tipo=column, slot=slot['indice'], direccion=address))
            item['coincidencias_ram_bytes_completos'] = matches
            if len(raw) >= 32 and raw[:2] == b'\x80\0' and raw[4] == 3 and raw[6] == 4:
                start = struct.unpack_from('>H', raw, 2)[0]+4
                block, channels = raw[5], raw[7]
                hz, samples = struct.unpack_from('>2I', raw, 8)
                if block < 3 or channels not in (1, 2) or not 0 < hz <= 192000:
                    raise ValueError('Cabecera ADX invalida')
                if start > len(raw) or samples > ((len(raw)-start)//(block*channels))*((block-2)*2):
                    raise ValueError('ADX truncado')
                item['adx'] = dict(hz=hz, canales=channels, muestras=samples,
                                   segundos=samples/hz, contenido_escuchado=False)
            leaves.append(item)
        banks.append(dict(entrada=index, bytes=len(pack), sha256=hashlib.sha256(pack).hexdigest(),
                          hojas=leaves))
    report = dict(fuente=game.source, archivo=cmn, referencia=str(args.original.resolve()),
        referencia_sha256=hashlib.sha256(original_bytes).hexdigest(), instantaneas=states,
        bancos=banks, ambiente_identificado=False, sonido_audible=False, paridad_port=False,
        limites=['35EDCC solo deriva una carga desde el estado; no acredita su ejecucion en esas muestras',
                 'Coincidencia completa en RAM identifica la hoja retenida, no su reproduccion',
                 'Cabeceras ADX y slots no identifican por si solos voz, musica o ambiente',
                 'No reproduce audio, ni compara PCM/mezcla/imagen GS con el port'])
    with args.salida.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(dict(salida=str(args.salida), instantaneas=states,
        bancos=[dict(entrada=b['entrada'], hojas=len(b['hojas']),
                     adx=sum('adx' in x for x in b['hojas']),
                     hojas_con_coincidencia_ram=sum(bool(x['coincidencias_ram_bytes_completos'])
                                                   for x in b['hojas'])) for b in banks],
        ambiente_identificado=False), ensure_ascii=False, allow_nan=False))


if __name__ == '__main__':
    main()

"""Cruza el consumidor ADX original con instantaneas retenidas de Serpiente.

Lee el ELF y savestates propios; no ejecuta el juego ni reproduce audio.
Los enlaces del decodificador son estado retenido, no una captura de llamadas.
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


RANGOS = (
    ('Intro_Enter31', 0x29A7C0, 0x29A8F0),
    ('Intro_Update31', 0x29BB70, 0x29BFF0),
    ('Presentacion_Enter2C_Prefix', 0x29BFF0, 0x29C070),
    ('Audio_StopSlot', 0x35EA50, 0x35EAA0),
    ('Audio_PlaySlot', 0x35EB60, 0x35EC20),
    ('Audio_StartMemoryHandle', 0x126750, 0x126880),
    ('ADX_StartMemoryWrapper', 0x161AB0, 0x161AF0),
    ('ADX_StartMemoryDefaultSize', 0x161AF0, 0x161B08),
    ('ADX_StartMemory', 0x161B58, 0x161C30),
    ('ADX_BindSource', 0x15E6D0, 0x15E7D8),
    ('ADX_MemorySource', 0x170728, 0x1707F0),
    ('ADX_ReadStatus', 0x15EB30, 0x15EB98),
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def ventana(elf, pc, lo, hi, md):
    lines = []
    for address in range(max(lo, pc-40), min(hi, pc+8), 4):
        raw = memoria_elf(elf, address, 4)
        word, = struct.unpack('<I', raw)
        decoded = list(md.disasm(raw, address, count=1))
        text = (decoded[0].mnemonic+' '+decoded[0].op_str
                if decoded else 'EE: no interpretado')
        if word >> 26 == 0x12:
            text = 'EE COP2/VU: no interpretado'
        elif word >> 26 in (0x1E, 0x1F):
            text = 'EE LQ/SQ: no interpretado'
        lines.append(dict(pc=hex(address), word=f'{word:08X}', texto=text))
    return lines


def contrastar(elf, path):
    """Ejecuta instrucciones EE; servicios de audio externos interceptados."""
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('iwx_audio_oraculo', path)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    ctx, mini, actor, work, escena, camara = (
        0x800000, 0x810000, 0x820000, 0x830000, 0x840000, 0x850000)
    hooks_intro = {0x298280, 0x29A5D0, 0x296F20, 0x28A9D0,
                   0x2972D0, 0x238670, 0x28A980, 0x35EB60}
    hooks_audio = {0x126880, 0x126D90, 0x161AB0, 0x126E40, 0x126B70, 0x126CA0}

    class AudioEE(oracle.EE):
        def __init__(self, intro=False):
            super().__init__(SimpleNamespace(elf=elf))
            self.hooks = set(hooks_intro if intro else hooks_audio)
            self.eventos = []
            self.reserva = 2

        def step(self, pc, in_delay=False):
            result = super().step(pc, in_delay)
            w = self.get(pc)
            if w >> 26 == 3 and ((w & 0x3FFFFFF) << 2) in self.hooks:
                return result[0], result[1], True
            return result

        def hook(self, addr):
            if addr not in self.hooks:
                raise AssertionError(f'Servicio imprevisto: {addr:X}')
            args = [r & 0xFFFFFFFF for r in self.r[4:7]]
            self.eventos.append(dict(destino=hex(addr), argumentos=args))
            getters = {0x298280: camara, 0x29A5D0: work,
                       0x296F20: escena, 0x28A9D0: mini}
            self.r[2] = getters.get(addr, self.reserva if addr == 0x126D90 else 0)

    intros = []
    for index in range(19):
        for side in (0, 1):
            e = AudioEE(intro=True)
            e.r[4], e.r[5] = actor, 0xBAADF00D
            e.put(actor+0x1380, side)
            e.put(mini+4, index)
            e.put(escena+0x25C, 0x123456)
            n = e.run(0x29A7C0)
            requests = [x['argumentos'][:2] for x in e.eventos if x['destino'] == '0x35eb60']
            expected = [[0, 1]] if side == 0 and index != 1 else []
            if requests != expected or e.get(actor+0xCBC, 2) != 0x31:
                raise AssertionError(('Ruta de intro inesperada', index, side, requests))
            intros.append(dict(indice=index, lado=side, solicitudes=requests, instrucciones=n))
    cases = []
    for leaf in (0, 1, 6):
        for slot in (0, 1, 7):
            for old in (-1, 4):
                for allocation in ('libre', 'reutiliza', 'agotado'):
                    e = AudioEE()
                    e.put(0x49CEA0, ctx)
                    e.put(ctx+0xC+leaf*4, 0x900000+leaf*0x1000)
                    e.put(ctx+0x50+slot*8, old)
                    e.put(ctx+0x4C+slot*8, 1)
                    e.put(0x49C61C, 0 if allocation == 'agotado' else 1)
                    e.put(0x49C614, 0x860000)
                    e.put(0x860000, 5, 1)
                    e.reserva = 2 if allocation == 'libre' else 0xFFFFFFFF
                    # La llamada original pasa -1 y -3 en 126750; esa funcion
                    # y 35EAA0 se ejecutan completas, no se sustituyen por Python.
                    e.r[4], e.r[5] = leaf, slot
                    n = e.run(0x35EB60)
                    chosen = 2 if allocation == 'libre' else 5 if allocation == 'reutiliza' else 0xFFFFFFFF
                    starts = [x['argumentos'][1] for x in e.eventos if x['destino'] == '0x161ab0']
                    stops = [x['argumentos'][0] for x in e.eventos if x['destino'] == '0x126880']
                    pauses = [x['argumentos'][:2] for x in e.eventos if x['destino'] == '0x126ca0']
                    expected_start = [] if allocation == 'agotado' else [0x900000+leaf*0x1000]
                    expected_pause = [] if allocation == 'agotado' else [[chosen, 0]]
                    if (starts != expected_start or stops != ([] if old == -1 else [old])
                            or pauses != expected_pause or e.get(ctx+0x50+slot*8) != chosen):
                        raise AssertionError(('Contrato ADX inesperado', leaf, slot, old, allocation, e.eventos))
                    cases.append(dict(hoja=leaf, slot=slot, handle_anterior=old,
                        reserva=allocation, handle_final=chosen, solicitudes_origen=starts,
                        paradas=stops, pausas=pauses, instrucciones=n))
    # Solo se ejecuta el prefijo de la fase siguiente hasta solicitar la parada;
    # no se sustituye el resto de esa fase por una supuesta ejecucion completa.
    class ParadaObservada(Exception):
        pass

    class PresentacionEE(AudioEE):
        def __init__(self):
            super().__init__(intro=True)
            self.hooks.update((0x323820, 0x35EA50))

        def hook(self, addr):
            if addr == 0x35EA50:
                self.eventos.append(dict(destino=hex(addr),
                    argumentos=[r & 0xFFFFFFFF for r in self.r[4:7]]))
                raise ParadaObservada()
            super().hook(addr)

    presentation = []
    for index in range(19):
        for side in (0, 1):
            e = PresentacionEE()
            e.r[4] = actor
            e.put(actor+0x1380, side)
            e.put(mini+4, index)
            try:
                e.run(0x29BFF0)
            except ParadaObservada:
                pass
            else:
                raise AssertionError('El prefijo de presentacion no solicito parada')
            stops = [x['argumentos'][0] for x in e.eventos if x['destino'] == '0x35ea50']
            if stops != [1]:
                raise AssertionError(('Slot de presentacion inesperado', index, side, stops))
            presentation.append(dict(indice=index, lado=side, paradas=stops))
    stop_cases = []
    for slot in range(8):
        for old in (-1, 2, 7):
            e = AudioEE()
            e.put(0x49CEA0, ctx)
            before = [100+i for i in range(8)]
            before[slot] = old & 0xFFFFFFFF
            for i, handle in enumerate(before):
                e.put(ctx+0x50+i*8, handle)
            e.r[4] = slot
            n = e.run(0x35EA50)
            stops = [x['argumentos'][0] for x in e.eventos if x['destino'] == '0x126880']
            after = [e.get(ctx+0x50+i*8) for i in range(8)]
            expected = before.copy()
            expected[slot] = 0xFFFFFFFF
            if stops != ([] if old == -1 else [old]) or after != expected:
                raise AssertionError(('Parada no aislada', slot, old, stops, after))
            stop_cases.append(dict(slot=slot, handle_anterior=old, paradas=stops,
                                   otros_slots_conservados=True, instrucciones=n))
    return dict(oraculo_sha256=sha(path.read_bytes()), intros=intros, audio=cases,
        presentacion_prefijo=presentation, parada_slots=stop_cases,
        limite_presentacion='Ejecutado desde29BFF0 hasta la llamada a35EA50; resto no ejecutado',
        servicios_interceptados=dict(intro=[hex(x) for x in sorted(hooks_intro)],
                                      audio=[hex(x) for x in sorted(hooks_audio)],
                                      presentacion=[hex(x) for x in sorted(hooks_intro | {0x323820, 0x35EA50})]),
        entrada='sintetica; servicios de reserva, motor ADX, ganancia y pausa interceptados',
        pasa=True, ejecucion_natural=False, sonido_audible=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, required=True)
    ap.add_argument('--original', type=Path, required=True)
    ap.add_argument('--bancos', type=Path, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--oraculo', type=Path)
    ap.add_argument('--capturas', type=Path, help='Directorio nuevo para PNG ya retenidos en los savestates')
    args = ap.parse_args()
    elf = cargar_elf(args.elf)
    original_bytes = args.original.read_bytes()
    original = json.loads(original_bytes)
    bancos_bytes = args.bancos.read_bytes()
    bancos = json.loads(bancos_bytes)
    if (original['elf_sha256'] != ELF_SHA or bancos['fuente']['elf_sha256'] != ELF_SHA
            or bancos['referencia_sha256'] != sha(original_bytes)):
        raise ValueError('Referencias originales incompatibles')
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_MIPS,
                     capstone.CS_MODE_MIPS64 | capstone.CS_MODE_LITTLE_ENDIAN)
    consumers = []
    for name, lo, hi in RANGOS:
        raw = memoria_elf(elf, lo, hi-lo)
        calls = []
        for off in range(0, len(raw), 4):
            word, = struct.unpack_from('<I', raw, off)
            pc = lo+off
            if word >> 26 == 3:
                calls.append(dict(pc=hex(pc), destino=hex(
                    ((pc+4) & 0xF0000000) | ((word & 0x3FFFFFF) << 2)),
                    ventana=ventana(elf, pc, lo, hi, md)))
        consumers.append(dict(nombre=name, inicio=hex(lo), fin_exclusivo=hex(hi),
                              sha256=sha(raw), llamadas=calls))
    states = []
    if args.capturas:
        args.capturas.mkdir(parents=False, exist_ok=False)
    for retained, bank_state in zip(original['instantaneas'], bancos['instantaneas'], strict=True):
        path = Path(retained['estado'])
        if sha(path.read_bytes()) != retained['sha256'] or retained['sha256'] != bank_state['sha256']:
            raise ValueError('Cambio la instantanea retenida')
        with zipfile.ZipFile(path) as z:
            ram = z.read('eeMemory.bin')
            screenshot = z.read('Screenshot.png') if args.capturas else None
        if len(ram) != 0x2000000:
            raise ValueError('Instantanea sin los 32MiB EE esperados')

        def raw(addr, size):
            if not 0x100000 <= addr <= len(ram)-size:
                raise ValueError(f'Puntero EE fuera de rango: {addr:X}+{size:X}')
            return ram[addr:addr+size]

        def u(addr):
            return struct.unpack('<I', raw(addr, 4))[0]

        for _, lo, hi in RANGOS:
            if raw(lo, hi-lo) != memoria_elf(elf, lo, hi-lo):
                raise ValueError(f'Codigo retenido distinto del ELF: {lo:X}')
        context = bank_state['contexto_adx']
        if not context or u(0x49CEA0) != context['puntero']:
            raise ValueError('Contexto ADX distinto del retenido')
        ptr = context['puntero']
        if list(struct.unpack('<16I', raw(ptr+0x50, 64))) != context['slots']:
            raise ValueError('Slots ADX distintos de los retenidos')
        count = struct.unpack('<b', raw(0x49C604, 1))[0]
        if not 0 < count <= 64:
            raise ValueError('Cantidad de decodificadores fuera del contrato')
        table = u(0x49C60C)
        decoders = []
        for index in range(count):
            record = table+16*index
            handle = u(record)
            item = dict(indice=index, registro=record,
                        palabras=list(struct.unpack('<4I', raw(record, 16))),
                        handle_cri=handle,
                        slots_contexto=[slot for slot in range(8)
                                        if context['slots'][slot*2] == index])
            if 0x100000 <= handle <= len(ram)-0xCC:
                item['estado_cri_bytes'] = list(raw(handle, 4))
                source = u(handle+0x14)
                item['fuente_cri'] = source
                # 170728 crea hasta 32 objetos de 0x24 en 43A9B8; +14 es
                # el puntero de bytes entregado por 161B58/15E6D0.
                if 0x43A9B8 <= source < 0x43A9B8+32*0x24 and (source-0x43A9B8) % 0x24 == 0:
                    item['fuente_palabras'] = list(struct.unpack('<9I', raw(source, 36)))
                    data_ptr = u(source+0x14)
                    item['bytes_origen'] = data_ptr
                    matches = []
                    for bank in bancos['bancos']:
                        for leaf in bank['hojas']:
                            size = leaf['bytes']
                            if 0x100000 <= data_ptr <= len(ram)-size and sha(raw(data_ptr, size)) == leaf['sha256']:
                                matches.append(dict(entrada=bank['entrada'], hoja=leaf['indice'],
                                    bytes=size, sha256=leaf['sha256'], adx=leaf.get('adx')))
                    item['hojas_identificadas_bytes_completos'] = matches
            decoders.append(item)
        if u(0x4A4D40) != retained['n']:
            raise ValueError('Serial distinto del retenido')
        state = dict(n=retained['n'], estado=str(path), sha256=retained['sha256'],
            actor_estado=retained['actor_estado'], contexto=ptr,
            consumidores_coinciden_elf=True,
            decodificadores_tabla=table, decodificadores=decoders)
        if screenshot is not None:
            if not screenshot.startswith(b'\x89PNG\r\n\x1a\n'):
                raise ValueError('Captura retenida no es PNG')
            output = args.capturas/f"{retained['n']}.png"
            with output.open('xb') as f:
                f.write(screenshot)
            state['captura'] = dict(ruta=str(output.resolve()), sha256=sha(screenshot))
        states.append(state)
    report = dict(elf_sha256=ELF_SHA, original_sha256=sha(original_bytes),
        bancos_sha256=sha(bancos_bytes), consumidores=consumers, instantaneas=states,
        llamadas_naturales_capturadas=False, contenido_escuchado=False,
        ambiente_identificado=False, paridad_port=False,
        limites=['Los enlaces CRI son RAM retenida, no historial de llamadas ni PCM',
                 'Un origen retenido en un decodificador puede estar inactivo o ser residual',
                 'Las ventanas JAL no resuelven todas las rutas ni acreditan ejecucion natural',
                 'El serial n es libpad/DMA; no campos de video ni tiempo audible'])
    if args.oraculo:
        report['contraste_ee'] = contrastar(elf, args.oraculo)
    with args.salida.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(dict(salida=str(args.salida), consumidores=len(consumers),
        instantaneas=[dict(n=s['n'], actor_estado=s['actor_estado'],
            decodificadores=[dict(indice=d['indice'], slots=d['slots_contexto'],
                estado=d.get('estado_cri_bytes'), fuente=d.get('fuente_cri'),
                coincidencias=[dict(entrada=m['entrada'], hoja=m['hoja'])
                    for m in d.get('hojas_identificadas_bytes_completos', [])])
                for d in s['decodificadores'] if d['slots_contexto']
                or d.get('hojas_identificadas_bytes_completos')]) for s in states],
        contraste_ee=(dict(pasa=report['contraste_ee']['pasa'],
            casos_intro=len(report['contraste_ee']['intros']),
            casos_audio=len(report['contraste_ee']['audio']),
            casos_presentacion_prefijo=len(report['contraste_ee']['presentacion_prefijo']),
            casos_parada=len(report['contraste_ee']['parada_slots'])) if args.oraculo else None),
        ambiente_identificado=False), ensure_ascii=False))


if __name__ == '__main__':
    main()

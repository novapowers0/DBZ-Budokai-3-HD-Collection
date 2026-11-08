"""Camara y estado del fondo original: lectura pura de instantaneas propias.

No acredita continuidad/VSync, imagen GS ni sonido audible. EU20E290/1E2880
es una comparacion con combate: no demuestra la ruta de minijuegos306320.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import zipfile

ELF_SHA = 'afe8c7b99f31e635750a17618ae8248d1a64fe3ad0669996990abdf33e1819f4'


def leer(read):
    def raw(a, n):
        if not 0x100000 <= a or a+n > 0x2000000:
            raise ValueError(f'lectura fuera de EE: {a:X}+{n:X}')
        value = read(a, n)
        if len(value) != n:
            raise ValueError('lectura EE incompleta')
        return value
    def u(a):
        return struct.unpack('<I', raw(a, 4))[0]
    def vec(a):
        value = struct.unpack('<4f', raw(a, 16))
        if not all(math.isfinite(x) for x in value):
            raise ValueError('camara original no finita')
        return value
    n = u(0x4A4D40)
    mgr = u(0x49CA70)
    if not 0x100000 <= mgr < 0x2000000-0x8D24:
        raise ValueError('gestor original ausente/fuera de rango')
    activa = raw(mgr+0x8D20, 1)[0]
    estado = u(mgr+0x8B8C) if activa else -1
    camera = u(mgr+0x244)
    ojo = vec(camera)
    campo = u(0x49CBBC)
    actor = mgr+0x800
    data = dict(n=n, clave=hex(u(0x5C0DC8)), mgr=mgr,
                actor_estado=struct.unpack('<H', raw(actor+0xCBC, 2))[0],
                batalla_activa=activa, estado_batalla=estado,
                camara_dibujo=camera, ojo_dibujo=ojo,
                traslacion_combate_derivada=(ojo[0], 0, ojo[2]) if estado != 0 else (0, 0, 0),
                rama_combate_derivada='camara' if estado != 0 else 'origen',
                campo=campo)
    if 0x100000 <= campo < 0x2000000-0x140:
        data.update(ojo_campo=vec(campo+0x80), c0=vec(campo+0xC0),
                    clip=u(campo+0x114), animada=u(campo+0x12C))
    stage = u(0x49CDB8)
    if 0x100000 <= stage < 0x2000000-0xAE0:
        data['fondo_minijuego'] = dict(work=stage, flags=hex(u(stage+0x630)),
            rama_ajuste_vertical=(u(stage+0x630) & 8) == 0,
            limites=struct.unpack('<2f', raw(stage+0xAD4, 8)),
            uv=[struct.unpack('<2f', raw(stage+0x5F0+8*i, 8)) for i in range(8)],
            scroll=[vec(stage+0x650+16*i) for i in range(8)],
            recursos=[u(stage+0x118+4*i) for i in range(2)],
            modelos=[u(stage+0x230+4*i) for i in range(2)],
            vista=[vec(0x486090+16*i) for i in range(4)])
    data['slots_sonido'] = []
    for i in range(4):
        slot = mgr+0x270+i*0xCC
        item = dict(indice=i, tipo=u(slot), modo=u(slot+0x20),
            recurso=u(slot+0x30), tabla_a=u(slot+0x10), tabla_b=u(slot+0x28))
        # Cabeceras observadas, no prueba de que estos bancos esten sonando.
        for key in ('tabla_a', 'tabla_b'):
            ptr = item[key]
            if 0x100000 <= ptr <= 0x2000000-16:
                item[key+'_cabecera'] = raw(ptr, 16).hex()
        data['slots_sonido'].append(item)
    state = u(0x49CBA4)
    if 0x100000 <= state <= 0x2000000-8:
        index = u(state+4)
        # 35EDB0..35EDD0: a0 = tabla484760[estado+4], a1=0, a2=1.
        # Solo el rango de indices de minijuego conocido; no inferir ambiente.
        if index <= 18:
            data['carga_audio_minijuego_derivada'] = dict(estado=state, indice=index,
                tabla='0x484760', recurso=u(0x484760+4*index), a1=0, a2=1,
                consumidor='0x35edcc', ejecucion_observada=False)
    if n != u(0x4A4D40):
        raise ValueError('lectura cruza campos originales')
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, required=True)
    ap.add_argument('--estado', type=Path, nargs='*', default=[])
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--ventanas', action='store_true', help='llamadas y palabras EE de los consumidores')
    a = ap.parse_args()
    elf = a.elf.read_bytes()
    if hashlib.sha256(elf).hexdigest() != ELF_SHA:
        raise ValueError('ELF distinto de la referencia IW EU')
    ranges = []
    for name, ini, fin in (('Battle_GetStateOrInvalid', 0x20E290, 0x20E2D0),
                           ('Stage_DrawCameraFollowModel', 0x1E2880, 0x1E29A0),
                           ('Node_DrawBackgroundLayers', 0x306320, 0x306500),
                           ('Node_ScrollBackgroundUV', 0x306570, 0x3067E0),
                           ('MiniGame_UpdateTimers', 0x296110, 0x296210),
                           ('MinigameMovementsAudioEffects', 0x302E50, 0x303000),
                           ('LoadWaitMinigameAudios', 0x35EDA0, 0x35EDF0)):
        code = elf[ini-0x100000+0x80:fin-0x100000+0x80]
        if len(code) != fin-ini:
            raise ValueError('rango original truncado')
        item = dict(nombre=name, inicio=hex(ini), fin_exclusivo=hex(fin),
                    sha256=hashlib.sha256(code).hexdigest())
        if a.ventanas:
            import capstone
            md = capstone.Cs(capstone.CS_ARCH_MIPS,
                capstone.CS_MODE_MIPS64 | capstone.CS_MODE_LITTLE_ENDIAN)
            item['llamadas'] = []
            item['vsub'] = []
            for off in range(0, len(code), 4):
                word, = struct.unpack_from('<I', code, off)
                if word >> 26 == 0x12 and ((word >> 21) & 31) >= 16 and word & 63 == 0x2C:
                    mask = (word >> 21) & 15
                    item['vsub'].append(dict(pc=hex(ini+off), word=f'{word:08X}',
                        mascara=''.join(c for j, c in enumerate('xyzw') if mask & (8 >> j)),
                        destino=(word >> 6) & 31, fuente_a=(word >> 11) & 31,
                        fuente_b=(word >> 16) & 31))
                if word >> 26 != 3:
                    continue
                lines = []
                for rel in range(max(0, off-40), min(len(code), off+8), 4):
                    raw = code[rel:rel+4]
                    w, = struct.unpack('<I', raw)
                    decoded = list(md.disasm(raw, ini+rel, count=1))
                    text = ('EE COP2/VU: no interpretado' if w >> 26 == 0x12 else
                            'EE LQ/SQ' if w >> 26 in (0x1E, 0x1F) else
                            'EE LQC2/SQC2' if w >> 26 in (0x36, 0x3E) else
                            decoded[0].mnemonic+' '+decoded[0].op_str if decoded else
                            'EE: no interpretado')
                    lines.append(dict(pc=hex(ini+rel), word=f'{w:08X}', texto=text))
                item['llamadas'].append(dict(pc=hex(ini+off),
                    destino=hex(((ini+off+4) & 0xF0000000) | ((word & 0x3FFFFFF) << 2)),
                    ventana=lines))
        ranges.append(item)
    rows = []
    for path in a.estado:
        with zipfile.ZipFile(path) as z:
            info = z.getinfo('eeMemory.bin')
            if info.file_size != 0x2000000:
                raise ValueError('instantanea sin los 32MiB EE esperados')
            with z.open(info) as ram:
                def read(addr, count):
                    ram.seek(addr)
                    return ram.read(count)
                rows.append(dict(estado=str(path.resolve()),
                    sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    imagen_retenida='Screenshot.png' in z.namelist(), **leer(read)))
    report = dict(elf_sha256=ELF_SHA, consumidores=ranges, instantaneas=rows,
        paridad_port=False, continuidad_video=False, sonido_audible=False,
         limites=['Muestras discretas, no campos consecutivos acreditados',
                  'La rama1E2880 pertenece a combate; no acredita el dibujo306320 del minijuego',
                  'Flags/UV/modelos son datos retenidos, no prueba de ejecucion del callback',
                  'Las ventanas de llamadas no resuelven todos los argumentos ni interpretan VU',
                  'No compara UV, imagen final ni PCM del original con el port'])
    with a.salida.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida), consumidores=len(ranges),
        instantaneas=len(rows), paridad_port=False, sonido_audible=False), ensure_ascii=False))
    for item in ranges:
        if item.get('vsub'):
            print(json.dumps(dict(nombre=item['nombre'], vsub=item['vsub']), ensure_ascii=False))
    for row in rows:
        stage = row.get('fondo_minijuego', {})
        print(json.dumps(dict(n=row['n'], flags=stage.get('flags'), uv=stage.get('uv'),
            recursos=stage.get('recursos'), modelos=stage.get('modelos'),
            slots_sonido=row['slots_sonido'],
            carga_audio_minijuego_derivada=row.get('carga_audio_minijuego_derivada')),
            ensure_ascii=False, allow_nan=False))


if __name__ == '__main__':
    main()

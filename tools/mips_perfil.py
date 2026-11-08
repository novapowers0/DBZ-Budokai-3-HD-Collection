"""Perfil de juego para tools/iw_mips/ee.py: carga un ELF de PS2 (+ RAM de un savestate .p2s opcional)
y devuelve un Ee listo. El nucleo (ee.py) no sabe nada del juego; cada juego es una entrada de PERFILES.
Autoprueba: python tools/mips_perfil.py [savestate.p2s]
"""
import hashlib
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / 'iw_mips'))
import ee  # noqa: E402

ELFS = Path('E:/DBZ3HD_trabajo/ghidra/elfs')
# gp = campo gp_value de la seccion SHT_MIPS_REGINFO (lo que Ghidra llama _mips_gp0_value).
# tabla/bloq: tabla de ordenes del tablero de B2 (docs MAPAS_CODIGO/B2_TABLERO.md); b1 aun sin mapear.
PERFILES = {
    'b2': dict(elf=ELFS / 'b2.elf', serie='SLES-51839', tabla=0x379EC0, bloq=0x37A020, ordenes=88),
    'b1': dict(elf=ELFS / 'b1.elf', serie=None, tabla=None, bloq=None, ordenes=0),
}


def leer_elf(p):
    d = Path(p).read_bytes()
    assert d[:4] == b'\x7fELF', 'no es un ELF'
    entry = struct.unpack_from('<I', d, 24)[0]
    phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
    shoff, shsz, shn = struct.unpack_from('<I', d, 32)[0], struct.unpack_from('<H', d, 46)[0], struct.unpack_from('<H', d, 48)[0]
    segs = [struct.unpack_from('<8I', d, phoff + 32 * i) for i in range(phnum)]
    segs = [(off, va, fsz, msz) for t, off, va, _, fsz, msz, _, _ in segs if t == 1]
    gp = None
    for i in range(shn):
        s = struct.unpack_from('<10I', d, shoff + shsz * i)
        if s[1] == 0x70000006:  # REGINFO: gp_value en +20
            gp = struct.unpack_from('<I', d, s[4] + 20)[0]
    return d, entry, segs, gp


def cargar(juego='b2', p2s=None, texto_elf=True):
    """-> (Ee, info). Con p2s: RAM del savestate y encima el codigo del ELF (para ejecutar la logica real
    con los datos vivos del juego); sin p2s: solo el ELF (datos iniciales, BSS a cero)."""
    pf = PERFILES[juego]
    d, entry, segs, gp = leer_elf(pf['elf'])
    mem = bytearray(ee.TOTAL)
    dif = None
    if p2s:
        with zipfile.ZipFile(p2s) as z:
            mem[:ee.RAM] = z.read('eeMemory.bin')
            mem[ee.SPR:ee.SPR + 0x4000] = z.read('Scratchpad.bin')
    for off, va, fsz, msz in segs[:1]:  # segmento 0 = codigo+datos iniciales; el 2o es BSS
        if p2s:
            dif = sum(1 for i in range(0, min(fsz, 0x250000 - 0x100000), 4)
                      if d[off + i:off + i + 4] != mem[va + i:va + i + 4])
        if texto_elf or not p2s:
            n = fsz if not p2s else 0x250000 - va  # con savestate solo el texto (los datos son los vivos)
            mem[va:va + n] = d[off:off + n]
    cpu = ee.Ee(mem, gp)
    cpu.hooks = {}
    return cpu, dict(entry=entry, gp=gp, dif_texto=dif, sha1=hashlib.sha1(d).hexdigest())


def tabla_ordenes(cpu, juego='b2'):
    pf = PERFILES[juego]
    return [(cpu.r32(pf['tabla'] + 4 * i), cpu.r8(pf['bloq'] + i)) for i in range(pf['ordenes'])]


def cuadros_orden(p2s, a0, a1=1, paso=1.2, orden=19):
    """Ejecuta el manejador real de la orden `orden` del tablero de B2 (EU 0x2956A0 para la 19) una vez por cuadro
    hasta que la tarea se acaba (FUN_001485F0). `paso` = fGpffff866c: 1,2 por cuadro en PAL (cGpffff8688 == 3).
    Devuelve (cuadros que vive la tarea, llamadas a FUN_0023B900 = efecto emitido por cuadro)."""
    import struct
    cpu, info = cargar('b2', p2s)
    gp = info['gp']
    cpu.w32(gp - 0x7994, struct.unpack('<I', struct.pack('<f', paso))[0])
    estado = dict(fin=False, efectos=0)
    def fin(c): estado['fin'] = True
    def efecto(c): estado['efectos'] += 1
    def pieza(c): pass   # FUN_0027A790: posicion de la pieza a1; no cambia la espera
    cpu.hooks.update({0x1485F0: fin, 0x23B900: efecto, 0x27A790: pieza, 0x273AD0: pieza})
    tarea = 0x1F00000   # tarea falsa en RAM libre: +0x30 temporizador (float), +0x34 a0, +0x38 a1
    cpu.w32(tarea + 0x30, 0); cpu.w32(tarea + 0x34, a0); cpu.w32(tarea + 0x38, a1)
    n = 0
    while not estado['fin'] and n < 5000:
        cpu.call(PERFILES['b2']['tabla'] and cpu.r32(PERFILES['b2']['tabla'] + 4 * orden), tarea)
        n += 1
    return n, estado['efectos']


def autoprueba(p2s=None):
    for j, pf in PERFILES.items():
        cpu, info = cargar(j)
        assert info['gp'] and info['entry'] == 0x100008, (j, info)
        print(j, 'gp=%#x entry=%#x sha1=%s' % (info['gp'], info['entry'], info['sha1'][:10]))
    cpu, info = cargar('b2')
    t = tabla_ordenes(cpu)
    assert t[19] == (0x2956A0, 0), t[19]          # orden 19: efecto sobre la pieza, no bloquea el guion
    assert t[7][1] == 1 and t[38][1] == 1 and t[0][0] == 0, (t[7], t[38], t[0])
    print('b2 tabla OK: 19 ->', hex(t[19][0]), 'bloquea =', t[19][1], '| bloqueantes:', sum(b for _, b in t))
    if p2s:
        cpu, info = cargar('b2', p2s)
        print('texto ELF distinto del savestate en %d palabras (0 = misma copia SLES-51839)' % info['dif_texto'])
        assert info['dif_texto'] == 0
        # guion de la fase 8: tres 19 con los efectos 15, 16, 17 sobre la pieza 13 (duraciones 131, 141, 151)
        r = [cuadros_orden(p2s, k)[0] for k in (15, 16, 17)]
        print('orden 19, efectos 15/16/17 -> cuadros', r, 'total', sum(r), '(grabacion del original: 354 llamadas)')
        assert r == [110, 118, 126] and sum(r) == 354
    print('AUTOPRUEBA OK')


if __name__ == '__main__':
    autoprueba(sys.argv[1] if len(sys.argv) > 1 else None)

"""Inventario focal del combate de tablero; no instala ni extrae recursos."""
from collections import Counter
import hashlib
import struct
import sys


BANCOS = {'es': 'DATA_SP.AFS', 'en': 'DATA_UK.AFS', 'fr': 'DATA_FR.AFS',
          'de': 'DATA_DU.AFS', 'it': 'DATA_IT.AFS'}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def exigir(ok, mensaje):
    if not ok:
        raise ValueError(mensaje)


def dependencias(repo):
    sys.path[:0] = [str(repo / p) for p in ('tools', 'tools/iw_mips', 'tools/b2_nb2a')]
    import censo as anterior
    return anterior, anterior.repo_imports(repo)


def original(a, deps):
    from mips_perfil import leer_elf
    o, Iso, Afs, amt_n, leer_ama, Amt, hijo = deps
    img = Iso(str(a.iso))
    d = a.anterior.Disco(img, Afs, amt_n, leer_ama, Amt, hijo,
                         BANCOS[a.idioma], o.EXPECTED_ELF_SHA256)
    raw, _, segments, gp = leer_elf(o.PERFILES['b2']['elf'])
    exigir(sha(raw) == o.EXPECTED_ELF_SHA256, 'ELF PAL no revisado')

    def va(addr, form):
        n = struct.calcsize(form)
        offsets = [off + addr - start for off, start, size, _ in segments
                   if start <= addr and addr + n <= start + size]
        exigir(len(offsets) == 1, f'VA no mapeada unívocamente: {addr:#x}')
        return struct.unpack_from(form, raw, offsets[0])

    ptr, = va(0x3749A4, '<I')
    return d, va, va(ptr, '<24h'), gp


def fases(a, disco):
    data = []
    for f in a.fase or range(1, 10):
        entry = 386 + f
        raw = disco.entrada(0, entry)
        a.anterior.divide_ydw(raw)  # valida los límites T1/T2 con el lector común
        n1, t1, n2, t2 = struct.unpack_from('<4I', raw, 0x20)
        tiles = [dict(casilla=i, escenario=raw[t1 + i*52 + 0x18],
                      condicion=raw[t1 + i*52 + 0x19], musica=raw[t1 + i*52 + 0x1f],
                      xyz_batalla=list(struct.unpack_from('<3f', raw, t1 + i*52 + 0x24)))
                 for i in range(1, n1)]
        overrides = [dict(pieza=i, personaje=raw[t2 + i*40], indice=raw[t2 + i*40 + 10])
                     for i in range(1, n2) if raw[t2 + i*40 + 10]]
        data.append(dict(fase=f, cmn=entry, sha256=sha(raw), casillas=tiles, t2_musica=overrides,
                         indices_t1=dict(sorted(Counter(c['musica'] for c in tiles).items()))))
    return data


def inventario(a, deps):
    from historia_b1_1910 import afs_entry, adx_info
    d, va, tabla, gp = original(a, deps)
    fs = fases(a, d)
    caja = [(0, 408), (0, 410), (1, 189), (1, 190), (1, 1145)]
    sonido = [(1, 142), (1, 143)]
    for bank, item in caja + sonido:
        d.describe(bank, item)
    stages = {}
    # FUN_001D2CC0 (B2) pide la escena con el byte T1+0x18; FUN_001B2790
    # consulta estas ocho palabras. La presencia en la tabla no implica un draw.
    for stage in sorted({t['escenario'] for f in fs for t in f['casillas']}):
        exigir(0 <= stage <= 12, f'escenario T1 fuera del rango revisado: {stage}')
        ids = va(0x372AB0 + stage*0x9C, '<8i')
        stages[stage] = [dict(codigo=i, recurso=d.describe(i >> 16, i & 0xFFFF)
                              if i >= 0 and i >> 16 in d.bancos else None) for i in ids]
    pistas = {}
    for i in sorted({i for i in tabla if i > 0}):
        raw = afs_entry(d.img, 'ADX_US.AFS', i)
        pistas[i] = dict(bytes=len(raw), sha256=sha(raw), **adx_info(raw))

    funciones = (0x235780, 0x236420, 0x276FF0, 0x23B290, 0x249490, 0x249DD0,
                 0x249FF0, 0x24A080, 0x2460D0, 0x246190, 0x27EEF0,
                 0x1D6CD0, 0x1D5740, 0x1D2CC0, 0x1B2790, 0x2AD940, 0x2B1EF0)
    texto = (a.repo / 'src/pasarela_b2.cpp').read_text(encoding='utf-8-sig')
    anchors = {
        'contacto_tablero': ('void Contacto(', 'SonidoEventoB2("contacto", 7)'),
        'tarjeta': ('bool DibTarjeta(', 'leer_ama(408, d.tarjeta_ama)', 'amt(loc, 189, d.tarjeta_cond)',
                    'amt(loc, 190, d.tarjeta)', 'const float u = std::clamp((c - 70.0f)'),
        'puente_hd': ('int TableroOriginal(', 'const auto& cc = g_d.casillas[size_t(s.casilla_combate)]'),
        'retorno': ('void Resultado(int ganador, int vida)', 'g_iris_pend = true;   // S1-F01.2', 'void DibIris('),
        'audio': ('void SonidoCanal(int canal, int id)', 'int PistaCombateB2(', 'FundirMusicaPs2(iso_musica'),
    }
    links = a.anterior.funciones_ghidra(a.ghidra, funciones)
    exigir(len(links) == len(funciones), 'export de Ghidra incompleto para el censo')
    return dict(formato='nb2f-censo-v1', iso=str(a.iso), elf_sha256=deps[0].EXPECTED_ELF_SHA256,
                idioma=a.idioma, gp=hex(gp), fases=fs, tabla_musica=list(tabla), pistas_adx=pistas,
                escenarios=stages,
                recursos=d.recursos, claves_ama={i: a.anterior.claves_ama(d, i) for i in (408, 410)},
                tipo_fundido={i: hex(va(0x377EE0 + 4*i, '<I')[0]) for i in range(1, 11)},
                original={hex(i): line for i, line in links.items()},
                port={k: a.anterior.referencias(texto, v) for k, v in anchors.items()},
                cobertura=dict(tablero='T1 completo por fase (sin la fila 0); T2 sólo overrides no nulos',
                               tarjeta='Identidad/formato de bancos; no acredita su uso o composición',
                               combate='Parámetros T1 y pistas candidatos; animación/cámara/FX 3D HD sin pareja medida',
                               audio='Metadatos ADX y bancos de contacto; no PCM, mezcla ni bucle',
                               cero='El índice 23 contiene 0 (el port exige pista > 0); no se presume silencio PAL'))

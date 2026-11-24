"""Censo PAL del tablero B2: carga AFS, selección de dibujo y rutas C++.

Salida 0 = informe generado (puede contener diferencias); 2 = entradas inválidas.
No lanza el juego ni el emulador. Solo escribe los destinos explícitos.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
from subprocess import SubprocessError
import re
import struct
import sys

import sonda_pal as pal

REPO = Path(__file__).resolve().parents[2]
TRABAJO = Path('E:/DBZ3HD_trabajo')
ISO = Path('D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
FUENTE = Path('src/pasarela_b2.cpp')


def require(ok, msg):
    if not ok:
        raise ValueError(msg)


def repo_imports(repo):
    sys.path[:0] = [str(repo / p) for p in ('tools', 'tools/iw_mips', 'mod center hd/pasarela', 'mod center hd')]
    from b2_oraculo import original
    from iso import Iso
    from pasarela import Afs, amt_n
    from s4_nave_hud import leer_ama, Amt, hijo
    return original, Iso, Afs, amt_n, leer_ama, Amt, hijo


class Disco:
    def __init__(self, img, afs, amt_n, leer_ama, Amt, hijo, loc, digest):
        self.img, self.loc = img, loc
        self.leer_ama, self.amt_n, self.Amt, self.hijo = leer_ama, amt_n, Amt, hijo
        self.bancos = {}
        self.recursos, self.amas = {}, {}
        files = img.files()
        exe = '/SLES_518.39'
        require(hashlib.sha256(img.read(exe, 0, files[exe][1])).hexdigest() == digest,
                'El ejecutable de la ISO no es el ELF PAL revisado')
        for banco, name in ((0, 'DATA_CMN.AFS'), (1, loc)):
            path = '/USR/' + name
            cab = img.read(path, 0, 8)
            require(cab[:4] == b'AFS\0' and len(cab) == 8, f'Cabecera AFS no válida: {name}')
            n = struct.unpack_from('<I', cab, 4)[0]
            size = files[path][1]
            require(0 < n <= min(10000, (size - 8)//8), f'Tabla AFS fuera de rango: {name}')
            self.bancos[banco] = (name, afs(img, path), size)

    def entrada(self, banco, i):
        name, afs, size = self.bancos[banco]
        require(0 <= i < len(afs.tab), f'Índice fuera de {name}: {i}')
        off, sz = afs.tab[i]
        require(8 + 8*len(afs.tab) <= off and 0 < sz <= min(size-off, 32*1024*1024),
                f'Entrada inválida de {name}: {i}')
        data = afs.entry(i)
        require(len(data) == sz, f'Lectura corta de {name}: {i}')
        return data

    def describe(self, banco, i):
        key = f'{self.bancos[banco][0]}/{i}'
        if key not in self.recursos:
            data = self.entrada(banco, i)
            magic = data[:5] if data.startswith((b'#AMO0', b'#AMM0')) else data[:4]
            tipo = magic.decode('ascii', errors='replace')
            rec = dict(tipo=tipo, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            if tipo == '#AMT':
                amt = self.Amt(data)
                require(0 < amt.n <= 1024, f'AMT fuera de contrato: {key}')
                rec['texturas'] = [amt.info(k) for k in range(amt.n)]
            if tipo == '#AMA':
                ama = self.leer_ama(data)
                require(len(ama) == struct.unpack_from('<I', data, 8)[0], f'AMA incompleto: {key}')
                rec['registros_ama'] = len(ama)
                self.amas[key] = ama
            if tipo == '#AMB':
                n = struct.unpack_from('<I', data, 0x10)[0]
                require(0 < n <= 1024, f'AMB fuera de contrato: {key}')
                rec['hijos'] = [self.hijo(data, k)[:4].decode('ascii', errors='replace') or None for k in range(n)]
            self.recursos[key] = rec
        return key

    def capas(self, registros, ama_id, amt_id):
        ka, kt = self.describe(*ama_id), self.describe(*amt_id)
        ama, tex = self.amas[ka], self.recursos[kt]['texturas']
        out = []
        for q in registros:
            a, i = q['anim'], q['img']
            require(0 <= a < len(ama) and 0 <= i < len(ama), f'Animación fuera de rango: {ka} {a}/{i}')
            posibles = sorted({p.tex for p in ama[i].p})
            require(posibles and all(t < len(tex) and tex[t] for t in posibles), f'Textura fuera de rango: {kt} {i}: {posibles}')
            out.append(dict(ama=ka, anim=a, imagen=i, amt=kt, texturas=posibles,
                            reloj=q['reloj'], duracion=ama[a].dur))
        return out


def divide_ydw(data):
    """Proyección T1/T2/T7 según Cargar; los demás campos quedan fuera del análisis."""
    require(data.startswith(b'#YDW') and 0x70 <= len(data) < 0x90000, 'YDW inválido')
    n1, o1, n2, o2 = struct.unpack_from('<4I', data, 0x20)
    require(3 <= n1 <= 128 and 0 <= n2 <= 128 and o1 + 52*n1 <= len(data) and o2 + 40*n2 <= len(data),
            'T1/T2 fuera de contrato')
    t2 = [(*struct.unpack_from('<3B', data, o2+40*k), struct.unpack_from('<H', data, o2+40*k+0x14)[0])
          for k in range(1, n2)]
    n7, o7 = struct.unpack_from('<2I', data, 0x50)
    require(n7 <= 4096 and o7 + 4*n7 <= len(data), 'T7 fuera de contrato')
    t7 = []
    for i in range(n7):
        off, = struct.unpack_from('<I', data, o7+4*i)
        if not off:
            continue
        anim, tipo, x, y = struct.unpack_from('<IIff', data, off)
        if tipo <= 4:
            t7.append(dict(registro=i, tipo=tipo, registro_ama=anim, x=x, y=y))
    return t2, t7


def linea(text, substring, start=0):
    index = text.find(substring, start)
    return None if index == -1 else (1 + text.count('\n', 0, index))


def referencias(text, anchors):
    return [dict(texto=a, linea=linea(text, a)) for a in anchors]


def cargas_cpp(text, fase):
    """Sintaxis revisada de Cargar: solo literales y n[+k*fase], nunca eval()."""
    # d.<campo> conserva la diferencia entre adquirir un banco y darle un uso.
    start = text.index('auto amt = [&](AfsPs2& afs,')
    end = text.index('dbz3 tablero b2: fase {} leida de', start)
    out = []
    for num, l in enumerate(text[start:end].splitlines(), 1 + text.count('\n', 0, start)):
        l = l.split('//', 1)[0]
        m = re.search(r'\b(?:(amt)\((cmn|loc), |(leer_ama)\()([0-9]+)(?: \+ (?:(\d+) \* )?size_t\(fase\))?, (d\.\w+|v)\);', l)
        if not m:
            continue
        _, banco, ama, i, mult, dest = m.groups()
        j = int(i) + ((int(mult or '1') * (fase-1)) if 'size_t(fase)' in m.group() else 0)
        out.append(dict(banco=int(banco == 'loc'), entrada=j, espera='#AMA' if ama else '#AMT',
                        destino=dest, linea=num))
    return out


def comparar(original, port):
    """Multiconjunto para preservar capas repetidas; el orden se informa aparte."""
    if original is None or port is None:
        return dict(estado='PENDIENTE', faltan=None, sobran=None)
    a, b = Counter(map(tuple, original)), Counter(map(tuple, port))
    return dict(estado='IGUAL' if original == port else ('ORDEN_DISTINTO' if a == b else 'DIFERENTE'),
                faltan=list((a-b).elements()), sobran=list((b-a).elements()))


def claves_ama(disco, entrada):
    key = disco.describe(0, entrada)
    data = disco.entrada(0, entrada)
    tabla, = struct.unpack_from('<I', data, 12)
    out = []
    for i, a in enumerate(disco.amas[key]):
        n, off = struct.unpack_from('<2I', data, tabla+i*0x40+0x38)
        # Extiende el lector común: conserva el valor GS crudo de cada clave B.
        require(n < 256 and off + 0x30*n <= len(data), f'Claves B truncadas: {entrada}/{i}')
        claves = [struct.unpack_from('<4I', data, off+j*0x30) for j in range(n)]
        out.append(dict(dur=a.dur, claves=[dict(cuadro=c, tex=t, gs=g, fog=f) for c,t,g,f in claves]))
    return out


PANTALLAS = {
    'intro': ((0x2A5FB0,), [(0,396), (1,155)], ['leer_ama(396, d.intro_ama)', 'loc.Entrada(155)', 'const auto& ia = g_d.intro_ama;']),
    'aliados': ((pal.ALLIES,), [], ['leer_ama(399, d.aliados_ama)', 'amt(loc, 184, d.aliados)', 'bool DibAliadosCuerpo(']),
    'HUD / radar / ayuda': ((pal.BOARD,0x26FFF0), [(0,402),(1,188),(1,1145)], ['void DibHud(', 'DibHudSp(', 'DibAyudaTiras(', 'S::HelpLine(']),
    'caminos y destino': ((0x239380,0x236EA0), [(0,398),(0,402),(1,188)], ['dl->AddLine(a, b, IM_COL32(int(sr', 'dl->AddEllipseFilled(p, ImVec2(36 * e']),
    'tarjeta Battle!': ((), [(0,408),(0,410),(1,189),(1,190)], ['bool DibTarjeta(', 'crear(g_d.tarjeta_cond, tc)', 'leer_ama(408, d.tarjeta_ama)']),
    'viñetas M/P/T': ((), [(0,788),(0,789),(0,790),(0,791),(0,1196)], ['void DibEscena(', 'DibCeldasMpt(', 'DibujarFiguraB2En(']),
    'zenie': ((), [(0,930),(1,1147)], ['leer_ama(930, d.zenie_ama)', 'g_tzenie']),
    'game over': ((pal.CLOSING,0x29F4E0,0x29D950), [], ['if (derrota && g_tgo)', 'if (!gana && g_tgo)', 'amt(cmn, 403, d.fin_textos)']),
    'resumen de Bolas': ((pal.CLOSING,0x29EDD0), [], ['void DibResumenNativo(', 'g_d.resumen_ama', 'g_tresumen']),
    'epílogo': ((), [(1,289),(0,341),(1,145)], ['bool DibEpilogoNativo(', 'd.fin_textos', 'DibNativa01(dl, v, g_tresumen, 0']),
    'Shenron': ((), [(0,327),(0,328),(0,329),(0,849),(0,850),(1,535),(1,537),(1,538),(1,539),(1,540)],
                ['const std::vector<uint8_t> a327', 'g_shen_sitio', 'g_shen_luz =', 'if (g_estado == kDeseo)']),
}


def funciones_ghidra(path, direcciones):
    base = re.compile(r'^// ==== ([0-9a-fA-F]+) ')
    out = {}
    with path.open(encoding='utf-8', errors='replace') as f:
        for n, line in enumerate(f, 1):
            if m := base.match(line):
                a = int(m[1], 16)
                if a in direcciones:
                    out[a] = f'{path.name}:{n}'
    return out


def censo(args):
    orig, Iso, Afs, amt_n, leer_ama, Amt, hijo = repo_imports(args.repo)
    base = orig.Original(args.estado)
    src = args.src or args.repo / FUENTE
    cpp = src.read_text(encoding='utf-8-sig')
    decomp = funciones_ghidra(args.ghidra, {pal.TILES, pal.BALLS, 0x239380, 0x236EA0} |
                              {fn for fns, _, _ in PANTALLAS.values() for fn in fns})
    img = Iso(str(args.iso))
    try:
        disco = Disco(img, Afs, amt_n, leer_ama, Amt, hijo, args.loc, base.identity['elf_sha256'])
        def documenta(carga):
            for s in carga['solicitudes']:
                s['id'] = disco.describe(s['banco'], s['entrada'])
            return carga
        fases = {}
        for f in args.fase or range(1, 10):
            k = f-1
            y = disco.entrada(0, 387+k)
            t2, t7 = divide_ydw(y)
            original = documenta(pal.carga(orig, base, pal.BOARD, fase=f))
            cargas = cargas_cpp(cpp, f)
            for r in cargas:
                r['id'] = disco.describe(r['banco'], r['entrada'])
                r['formato_ok'] = disco.recursos[r['id']]['tipo'] == r['espera']
            tiles, tt = pal.casillas(orig, base, y)
            balls, tb = pal.objetos(orig, base, y)
            for fila in tiles + balls:
                fila['capas'] = disco.capas(fila['capas'], (0,397), (0,398))
            kf = disco.describe(0, 342+5*k)
            figs = [dict(pieza=i, personaje=c, bando=b, casilla=ca, indice=ix,
                         atlas=kf, retrato=2*ix, dorso=2*ix+1,
                         pareja_en_atlas=(2*ix+1 < len(disco.recursos[kf]['texturas']) and
                                          bool(disco.recursos[kf]['texturas'][2*ix] and disco.recursos[kf]['texturas'][2*ix+1])))
                    for i, (c, ix, b, ca) in enumerate(t2, 1) if ca]
            ka = disco.describe(0,344+5*k)
            atm = claves_ama(disco,344+5*k)
            for row in t7:
                row['banco_ama'] = disco.describe(0, 344+5*k)
                row['banco_amt'] = disco.describe(0, 345+5*k)
                rid = row['registro_ama']
                if rid >= len(atm):
                    row['uso_estatico'] = 'índice no resuelto (posible grupo)'
                    continue
                a = disco.amas[ka][rid]
                row['uso_estatico'] = 'registro directo candidato; no expande grupos T7'
                row['texturas_posibles'] = sorted({p.tex for p in a.p})
                row['traslacion_en_A'] = any(p[4] or p[5] for p in a.k)
                row['rotacion_en_A'] = any(p[3] for p in a.k)
                row['modulacion_color_en_B'] = any(p.m != (1.,1.,1.) or p.alpha != 1. for p in a.p)
                row['gs_en_B'] = sorted({p['gs'] for p in atm[rid]['claves']})
            seleccion = {(r['banco'],r['entrada']) for r in cargas if r['formato_ok']}
            fases[str(f)] = dict(ydw=disco.describe(0,387+k), carga_pal=original, lecturas_cpp=cargas,
                                 sin_lectura_literal_cpp=[s for s in original['solicitudes']
                                                          if (s['banco'],s['entrada']) not in seleccion],
                                 casillas=tiles, objetos=balls, retratos=figs, adornos=t7,
                                 traza_casillas=tt, traza_objetos=tb)
        extras = {'intro': documenta(pal.carga(orig, base, pal.INTRO)),
                  'aliados': documenta(pal.carga(orig, base, pal.ALLIES)),
                  'game over': documenta(pal.carga(orig, base, pal.CLOSING, 0)),
                  'resumen de Bolas': documenta(pal.carga(orig, base, pal.CLOSING, 1))}
        pantallas = {}
        for name, (fns, bancos, anchors) in PANTALLAS.items():
            pantallas[name] = dict(referencias_original={hex(fn):decomp.get(fn) for fn in fns},
                                   afs_revisados=[disco.describe(*a) for a in bancos], carga_pal=extras.get(name),
                                   referencias_cpp=referencias(cpp, anchors), comparacion_draw='PENDIENTE')
        fusion = documenta(pal.carga(orig, base, pal.BOARD, fase=9, fusion=True)) if '9' in fases else None
        return dict(formato='nb2a-censo-v1', autoridad={'original':'PAL / ELF aislado + ISO',
                    'port':'Referencias de fuente y banco focal opcional; no son emisiones de GPU'}, iso=str(args.iso.resolve()), loc=args.loc,
                    estado=base.identity, fuente=str(src.resolve()), fuente_sha256=orig.sha(src),
                    ghidra=str(args.ghidra), ghidra_sha256=orig.sha(args.ghidra),
                    pal_239690=decomp.get(pal.TILES), pal_27b740=decomp.get(pal.BALLS),
                    fases=fases, pantallas=pantallas, carga_variante_fase9=fusion, recursos=disco.recursos,
                    registros_ama397=claves_ama(disco,397))
    finally:
        # Iso no implementa context manager.
        img._f.close()


def md(data):
    out = ['# Censo B2 PAL (NB2A)', '', f"Idioma AFS: `{data['loc']}`.", '', '## Casillas T1 y objetos T8', '',
           'Dibujo PAL = solicitudes al dibujo medidas en EE; no son píxeles. T2/T7 = inventario de datos.', '',
           '| Fase | Solicitudes AFS | Casillas | Dibujos T1 | Dibujos T8 dirigidos | Fichas T2 | Adornos T7 |',
           '|---:|---:|---:|---:|---:|---:|---:|']
    for n, f in data['fases'].items():
        out.append(f"| {n} | {len(f['carga_pal']['solicitudes'])} | {len(f['casillas'])} | "
                   f"{sum(len(r['capas']) for r in f['casillas'])} | {sum(len(r['capas']) for r in f['objetos'])} | "
                   f"{len(f['retratos'])} | {len(f['adornos'])} |")
    out += ['', '## Otras pantallas', '', 'Bancos revisados o solicitudes medidas al cargar; su orden de dibujo se mantiene pendiente.', '',
            '| Pantalla | Original (peticiones EE o bancos revisados) | Referencias C++ (líneas) |', '|---|---|---|']
    for nombre, p in data['pantallas'].items():
        ee = p['carga_pal']
        lista = [s['id'] for s in ee['solicitudes']] if ee else p['afs_revisados']
        lines = ', '.join(str(r['linea']) for r in p['referencias_cpp'] if r['linea'])
        out.append(f"| {nombre} | {', '.join(lista)} | {lines or 'pendiente'} |")
    out += ['', '`sin_lectura_literal_cpp` no significa ausente: no incluye cargadores por tabla u otras funciones.']
    if cmp := data.get('cotejo_cpp'):
        out += ['', '## Selección de capas C++ frente a PAL (casillas T1 y Bolas T8)', '',
                'Igualdad de registros y textura elegida en el banco, sin comparar píxeles. Un icono directo no equivale a una capa AMA.', '',
                '| Fase | Casos | Coinciden OFF | Coinciden ON |','|---:|---:|---:|---:|']
        for n in data['fases']:
            filas = [r for r in cmp['filas'] if r['fase'] == int(n)]
            out.append(f"| {n} | {len(filas)} | {sum(r['off']['estado']=='IGUAL' for r in filas)} | {sum(r['on']['estado']=='IGUAL' for r in filas)} |")
        out += ['', 'JSON: `cotejo_cpp.filas` contiene las capas que faltan y sobran en cada caso.']
    else:
        out += ['', 'Cotejo C++ no solicitado; usar `--cotejo` para comparar la selección del port.']
    return '\n'.join(out) + '\n'


def autoprueba():
    a = [('AMA', 0, 0), ('AMA', 16, 16), ('AMA', 16, 16)]
    require(comparar(a, a)['estado'] == 'IGUAL', 'Identidad')
    require(comparar(a, a[::-1])['estado'] == 'ORDEN_DISTINTO', 'Orden')
    require(comparar(a, a[:-1])['faltan'] == [a[-1]], 'Capa repetida perdida')
    require(comparar(a, None)['estado'] == 'PENDIENTE', 'Falta de evidencia')
    require(comparar(a, [('TEX', 17)])['estado'] == 'DIFERENTE', 'La textura sola no equivale a una animación')
    c = 'auto amt = [&](AfsPs2& afs, arg) {};\n  amt(cmn, 342 + 5 * size_t(fase), d.fichas);\n'
    c += ' // amt(cmn, 123, d.fake);\n  dbz3 tablero b2: fase {} leida de'
    require([r['entrada'] for r in cargas_cpp(c, 9)] == [382], 'Fórmula YDW/AFS o comentario')
    print('Autoprueba: 6 controles OK')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=Path, default=REPO, help='raíz local del port (por defecto, la de la herramienta)')
    p.add_argument('--iso', type=Path, default=ISO, help='ISO PAL propia SLES-51839')
    p.add_argument('--loc', choices=['DATA_'+i+'.AFS' for i in ('SP','UK','FR','IT','DU')], default='DATA_SP.AFS')
    p.add_argument('--estado', type=Path, default=TRABAJO/'A2/sstates/fase1.p2s', help='RAM base; T1/T8 se inyectan de la ISO de cada fase')
    p.add_argument('--ghidra', type=Path, default=TRABAJO/'ghidra/salida/b2.c')
    p.add_argument('--src', type=Path, help='pasarela_b2.cpp alternativo (parche en copia)')
    p.add_argument('--fase', type=int, choices=range(1,10), action='append', help='repetible; por defecto todas')
    p.add_argument('--json', type=Path, help='archivo nuevo para el censo completo')
    p.add_argument('--md', type=Path, help='archivo nuevo para la lectura humana')
    p.add_argument('--cotejo', type=Path, metavar='CARPETA_NUEVA', help='compila el banco focal C++ en este destino')
    p.add_argument('--clang', type=Path, default=Path('C:/Program Files/LLVM/bin/clang++.exe'))
    p.add_argument('--autoprueba', '--self-check', dest='autoprueba', action='store_true', help='controles del comparador sin entradas locales')
    try:
        a = p.parse_args(argv)
        if a.autoprueba:
            autoprueba()
            return 0
        data = censo(a)
        if a.cotejo:
            import banco_cpp
            a.cotejo.mkdir()
            req, obs, controles = banco_cpp.ejecutar(a.src or a.repo/FUENTE, data, a.cotejo.resolve(), 'selector', a.clang)
            filas, resumen = banco_cpp.evaluar(req, obs, data['registros_ama397'], comparar)
            data['cotejo_cpp'] = dict(filas=filas, resumen=resumen, controles=controles)
        for archivo, contenido in ((a.json, json.dumps(data, ensure_ascii=True, indent=2)+'\n'), (a.md, md(data))):
            if archivo:
                with archivo.open('x', encoding='utf-8', newline='\n') as f:
                    f.write(contenido)
        if not (a.json or a.md):
            print(md(data))
        else:
            print(f"Censo: {len(data['fases'])} fases, {len(data['pantallas'])} pantallas, {len(data['recursos'])} entradas AFS")
        return 0
    except (OSError, RuntimeError, ValueError, KeyError, IndexError, struct.error, SubprocessError) as e:
        print(f'Censo incompleto: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

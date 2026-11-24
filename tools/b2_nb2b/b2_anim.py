"""Coteja curvas focales del tablero B2 (ELF PAL vs. funciones C++ del port). Sin juego ni emulador."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
TASK, VIS, OUT = 0x1F00000, 0x1F01000, 0x1F02000

# Índices de recurso, no inferencias de orden de ejecución. 790 es caja M/P/T.
BANKS = {
    399: ('aliados', (0, 1, 2, 3, 7, 13, 16, 38, 45)),
    402: ('radar', (0, 1, 2, 16, 17, 18, 87, 88, 89)),
    790: ('vinetas', (0, 1, 2, 3, 4, 5)),
    849: ('shenron_ventana', (0, 1, 2)),
}
FOCI = ('movimiento', 'pasos', 'saltos', 'aliados', 'radar', 'vinetas', 'transiciones', 'shenron')
CXX = Path('C:/Program Files/LLVM/bin/clang++.exe')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def deps(root):
    sys.path[:0] = list(map(str, (root/'tools', root/'mod center hd', root/'mod center hd/pasarela')))
    from b2_pm23_carga import tramo
    from b2_oraculo import original as orig
    from b2_diag_recursos import radar, sitio_luz
    from iso import Iso
    from pasarela import Afs
    from s4_nave_hud import leer_ama
    return orig, tramo, radar, sitio_luz, Iso, Afs, leer_ama


def require(ok, message):
    if not ok:
        raise ValueError(message)


def cpp(source, tramo):
    def block(start, end='\n}'):
        return tramo(source, start, end) + end
    def line(start):
        return tramo(source, start, '\n')
    types = [block('struct Casilla {', '\n};'), block('struct Ficha {', '\n};'),
             block('struct Pose {', '\n};')]
    fns = [line('constexpr int kGoku = '), line('constexpr float kVelGiro = '), line('float Dif(float a, float b) {'),
           block('float GiroDe(const Ficha& f, float t) {'), block('float GiroDeOrden(const Ficha& f, float t) {'),
           block('void RecalcularSlots() {'), line('int PasosHasta(float dist) {'),
           block('void EmpezarPaso(Ficha& f, int dest, float ahora) {'), block('ImVec2 PosFicha(const Ficha& f) {'),
           block('void SonidosPasosB2(float anterior, float ahora) {'), block('Pose PoseDe(const Datos::Sprite& sp, float c) {'),
           *([block('std::array<float, 4> ColorAmaPal(const Datos::Sprite& sp, float c) {')]
             if 'ColorAmaPal(' in source else []),
           block('float AlfaB(const Datos::Sprite& sp, float c) {'), block('ImU32 TintB(ImU32 col, const Datos::Sprite& sp, float c) {')]
    values = {
        'TIPOS': '\n'.join(types), 'SPRITE': block('  struct Sprite {', '\n  };'),
        'FUNCIONES': '\n'.join(fns),
        'CARGADOR': tramo(source, '    auto a32 = [&](size_t o) -> uint32_t {', '\n  };\n  leer_ama(344'),
        'SHEN_RELOJ': line('    constexpr float kShenTexto = '),
        'SHEN_START': line('      if (te > 1.5f && (pressed & kStart))'),
        'IRIS': line('  const float p = g_iris_cierra ? std::min(1.0f, cuadros * 0.01f)'),
    }
    return values


class Port:
    def __init__(self, src, tramo, work, cxx, step, color, profile=False):
        values = cpp(src, tramo)
        text = (HERE/'sonda.cpp.in').read_text(encoding='utf-8')
        for key, value in values.items():
            text = text.replace(f'@{key}@', value)
        self.digest = sha(text.encode())
        program = work/'sonda.cpp'
        self.exe = work/('sonda.exe' if sys.platform == 'win32' else 'sonda')
        program.write_text(text, encoding='utf-8')
        subprocess.run([cxx, '-std=c++20', '-ffp-contract=off', str(program), '-o', str(self.exe)],
                       cwd=work, timeout=90, check=True)
        self.opts = [str(int(step)),str(int(color))]
        if profile:
            self.opts.append('1')

    def run(self, commands, expected):
        out = subprocess.run([str(self.exe), *self.opts], input='\n'.join(commands)+'\n', text=True,
                             capture_output=True, cwd=self.exe.parent, timeout=30, check=True).stdout
        rows = [list(map(float, line.split())) for line in out.splitlines()]
        require(len(rows) == expected, 'salida incompleta del arnés C++')
        return rows


def compare(a, b, fields, tol):
    require(len(a) == len(b) and len(a) > 0 and len(fields) == len(tol), 'rejilla incompleta o vacía')
    maxima = [0.] * len(fields)
    changed = [0] * len(fields)
    first = None
    dif = 0
    for i, (x, y) in enumerate(zip(a, b, strict=True)):
        require(len(x) == len(y) == len(fields), 'faltan valores por muestra')
        require(all(map(math.isfinite, (*x, *y))), 'valor no finito')
        errs = [abs(u-v) for u, v in zip(x, y, strict=True)]
        maxima = list(map(max, zip(maxima, errs, strict=True)))
        flags = [e > t for e, t in zip(errs, tol, strict=True)]
        changed = [n + x for n, x in zip(changed, flags, strict=True)]
        if any(flags):
            dif += 1
            if first is None:
                first = dict(indice=i, original=x, port=y)
    return dict(muestras=len(a), distintas=dif, campos=list(fields), tolerancia=list(tol),
                max_abs=maxima, distintas_por_campo=dict(zip(fields,changed,strict=True)), primera=first)


def selfcheck():
    require(compare([[0], [1]], [[0], [1]], ['x'], [0])['distintas'] == 0, 'control positivo')
    c = compare([[0], [1]], [[0], [2]], ['x'], [0])
    require(c['distintas'] == 1 and c['primera']['indice'] == 1 and c['max_abs'] == [1], 'control negativo')
    for a, b in (([], []), ([[1]], []), ([[1]], [[float('nan')]]), ([[1]], [[]])):
        try:
            compare(a, b, ['x'], [0])
        except ValueError:
            continue
        raise ValueError('se admitió cobertura inválida')
    print('Autoprueba: 6/6 (incluye diferencia, vacío, faltante, NaN y aridad).')


def putf(c, a, x):
    c.w32(a, struct.unpack('<I', struct.pack('<f', x))[0])


def getf(c, a):
    return struct.unpack('<f', c.r32(a).to_bytes(4, 'little'))[0]


def walk_cases(c, n):
    board = c.r32(0x3A45A8)
    count, table = c.r32(board+0x20), c.r32(board+0x24)
    require(1 < count <= 512 and 0 < table <= len(c.m)-count*0x34, 'T1 no disponible')
    real = []
    for i in range(1, count):
        for k in range(4):
            j = c.r16(table+i*0x34+8+2*k)
            if not 0 < j < count or j == i:
                continue
            xy = tuple(getf(c, table+t*0x34+d) for t in (i, j) for d in (0, 4))
            if 20 <= math.dist(xy[:2], xy[2:]) <= 750 and (i, j, xy) not in real:
                real.append((i, j, xy))
    require(real, 'sin aristas T1 válidas')
    return [(f'T1:{i}>{j}', xy, 0, 0) for i, j, xy in real[:n]] + [
        ('distancia_exacta', (0., 0., 100., 0.), 0, 0),
        ('diagonal', (20., -10., 53., 37.), 0, 0),
        ('hueco_1', (0., 0., -93., 31.), 1, 1),
        ('cambio_hueco_control', (0., 0., 100., 0.), 0, 1)]


def walk(orig, base, port, n):
    c = base.cpu({0x273950})
    cases = walk_cases(c, n)
    c.w32(c.gp-0x7748, TASK)
    c.w32(TASK+0x24, TASK+0x100)
    result = []
    for name, xy, a, b in cases:
        for i, v in enumerate(xy):
            putf(c, TASK+0x100+(i//2)*0x34+(i%2)*4, v)
        duration = orig.invoke(c, 0x273800, 0, 1)
        require(0 < duration <= 200, 'duración de paso fuera de contrato')
        pal = []
        # FUN_00273630 recibe contador positivo durante el paso. El cuadro 0 es el ancla.
        for t in range(duration+2):
            status = orig.invoke(c, 0x273630, 0, a, 1, b, t, OUT, OUT+4)
            pal.append([getf(c, OUT), getf(c, OUT+4), status])
        native = port.run(['mov ' + ' '.join(map(str, (*xy, a, b, 0))),
                           *(f'paso {t/60:.12g}' for t in range(len(pal)))], len(pal)+1)
        result.append(dict(caso=name, entrada=dict(xy=xy, huecos=[a,b]), duracion=[duration,native[0][0]],
                           ticks=list(range(len(pal))), original=pal, port=native[1:],
                           cotejo=compare([x[:2] for x in pal], [x[:2] for x in native[1:]], ['x','y'], [.002,.002])))
    return dict(casos=result, traza=orig.trace(c), alineacion='índice de avance (4 u/llamada), no segundos PAL',
                campos_A=['x','y','estado_273630'], campos_B=['x','y','sonido'],
                limite='Posición aislada. Hueco cambiante es diagnóstico, no criterio de la corrección.')


def pasos(orig, base, port):
    cases = []
    for name, char, delta in [('goku',0,1.), ('otro',1,1.), ('goku_pal',0,1.2), ('otro_pal',1,1.2)]:
        c = base.cpu({0x273630,0x273950})
        events = []
        orig.service(c, 0x274720, 'giro_terminado', [], result=1)
        orig.service(c, 0x147420, 'audio_preparar', [])
        orig.service(c, 0x147260, 'audio_id', events)
        orig.service(c, 0x238300, 'notificar_movimiento', [])
        def pose(cpu):
            # Aísla la lógica de locomoción/sonido; contrato del reloj de 0x2762b0, sin fin del clip.
            p = cpu.r[5]+0x10
            putf(cpu, p, getf(cpu,p)+delta)
            cpu.r[2] = 0
        c.hooks[0x2762B0] = pose
        c.w32(c.gp-0x7748, TASK)
        c.w32(TASK+0x24, TASK+0x100)
        putf(c,TASK+0x134,220.)
        c.w16(OUT+4,char)
        c.w16(OUT+10,1)
        samples = []
        for t in range(45):
            k = len(events)
            clock = getf(c,VIS+0x10)
            orig.invoke(c,0x275EB0,OUT,VIS)
            samples.append([clock, c.r16(OUT+0xe), getf(c,VIS), events[k]['argumentos'][2] if len(events)>k else 0])
        p = port.run([f'mov 0 0 220 0 0 0 {char}', *(f'paso {t*delta/60:.12g}' for t in range(45))],46)[1:]
        cases.append(dict(caso=name, delta=delta, ticks=list(range(45)), original=samples, port=p,
                          cotejo_sonido=compare([[r[3]] for r in samples],[[r[2]] for r in p],['id'],[0]),
                          cotejo_avance=compare([[r[2]] for r in samples],[[r[0]] for r in p],['x'],[.002]),
                          traza=orig.trace(c,events)))
    return dict(casos=cases, campos_A=['reloj_previo','contador_paso','x','sonido'], campos_B=['x','y','sonido'],
                limite='Giro terminado, clip sin reinicio, un tramo horizontal. Índice de llamada con t=índice*delta/60 en el arnés.')


def shen_clock(orig, base, port):
    out = []
    for mode, hz in ((3,50), (2,60)):
        for press in (None, 75, 76, 90, 91):
            c = base.cpu()
            events = []
            orig.service(c, 0x249F60, 'pedir_transicion_START', events)
            c.w32(0x693738,0)
            c.w8(c.gp-0x7978,mode)
            putf(c,TASK+0x30,1500.)
            samples = []
            for i in range(1, 1502):
                c.w16(0x3B43AE,8 if i == press else 0)
                orig.invoke(c,0x29B020,TASK)
                nxt = c.r32(TASK+0x1c)
                samples.append([getf(c,TASK+0x30), int(nxt==0x29AC10),int(nxt==0x29AEF0)])
                if nxt:
                    require(nxt in (0x29AC10,0x29AEF0), 'siguiente tarea Shenron imprevista')
                    break
            p = port.run([f'shen {i/hz:.12g} {8 if i == press else 0}' for i in range(1,len(samples)+1)],len(samples))
            out.append(dict(modo=mode,hz=hz,pulsa_start_campo=press,fin_A=len(samples),
                            fin_B_en_ventana=next((i for i,r in enumerate(p,1) if any(r)),None), original=samples,port=p,
                            cotejo=compare([r[1:] for r in samples],p,['texto','peticion_salto'],[0,0]),
                            traza=orig.trace(c,events)))
    return out


def iris(orig, base, port):
    out = []
    for name, fn, init in [('abre',0x249CB0,1.),('cierra',0x249DD0,0.)]:
        c = base.cpu()
        paddr = c.gp-0x7594
        putf(c,paddr,init)
        putf(c,paddr+4,.01)
        # Tipo 0 con callback nulo: se comprueba únicamente el reloj común del iris.
        for addr in (paddr-4,paddr+8,paddr+12,paddr+16,c.gp-0x7BAC):
            c.w32(addr,0)
        pal = [[init]]
        ended = None
        for i in range(1,102):
            orig.invoke(c,fn,TASK)
            pal.append([getf(c,paddr)])
            if c.r32(TASK+0x1c):
                ended = i
                break
        require(ended is not None,'iris no terminó dentro de la ventana')
        b = port.run([f'iris {i} {int(init==0)}' for i in range(len(pal))],len(pal))
        out.append(dict(caso=name, original=pal,port=b,fin_A=ended,fin_B_curva=next(i for i,row in enumerate(b) if row[0]==1-init),
                        cotejo=compare(pal,b,['progreso'],[.00002]),traza=orig.trace(c)))
    return out


def iso_bank(Iso, Afs, path, expected):
    iso = Iso(str(path))
    files = iso.files()
    idx = {key.lstrip('/').upper():key for key in files}
    elf, cmn = idx['SLES_518.39'], idx['USR/DATA_CMN.AFS']
    require(sha(iso.read(elf,0,files[elf][1])) == expected, 'ISO distinta del ELF PAL')
    require(iso.read(cmn,0,4) == b'AFS\0', 'CMN sin firma')
    return Afs(iso,cmn), files[cmn][1]


def entry(afs, extent, n, magic):
    off, size = afs.tab[n]
    require(8+len(afs.tab)*8 <= off and 0 < size <= min(extent-off,8*1024*1024), f'entrada CMN {n} fuera de rango')
    b = afs.entry(n)
    require(len(b)==size and b.startswith(magic), f'CMN {n}: firma o longitud inesperada')
    return b


def ama(orig, base, port, leer_ama, bank, raw, clips):
    parsed = leer_ama(raw)
    count, table = struct.unpack_from('<II',raw,8)
    require(len(parsed)==count and table==0x10 and table+count*0x40 <= len(raw), f'CMN {bank}: tabla AMA')
    c = base.cpu()
    arena = 0x1E00000
    require(len(raw) <= TASK-arena, 'AMA no cabe en arena privada')
    c.m[arena:arena+len(raw)] = raw
    commands = [f'load {len(raw)} ' + ' '.join(map(str,raw))]
    payloads = []
    for k in clips:
        sp = parsed[k]
        off = table+0x40*k
        for np, stride, keys in ((0x30,0x20,sp.k),(0x38,0x30,sp.p)):
            n, pos = struct.unpack_from('<II',raw,off+np)
            require(n == len(keys) and pos+stride*n <= len(raw), f'CMN {bank}/{k}: claves truncadas')
            c.w32(arena+off+np+4,arena+pos if n else 0)
        dur = orig.invoke(c,0x245C00,arena,k)
        require(dur == sp.dur and 0 <= dur <= 2000, f'CMN {bank}/{k}: duración inválida')
        grid = sorted({float(t) for t in range(dur+1)} | {0.5, *(a[0] for a in sp.k), *(b.t for b in sp.p)})
        fields = ['dur'] + (['x','y','ex','ey','alfa_A'] if sp.k else []) + (['alfa_B','r8','g8','b8','a8'] if sp.p else [])
        pa = []
        for t in grid:
            values = [dur]
            for fn, keys, offsets in ((0x1A86E0,sp.k,(16,20,4,8,28)), (0x1A8200,sp.p,(28,16,20,24,28))):
                if keys:
                    c.f[12] = int.from_bytes(struct.pack('<f',t),'little')
                    orig.invoke(c,fn,arena+off,OUT)
                    native = [getf(c,OUT+d) for d in offsets]
                    if fn==0x1A8200:
                        native[1:] = [math.floor(255*min(1,max(0,x))+.5) for x in native[1:]]
                    values.extend(native)
            pa.append(values)
            commands.append(f'ama {k} {t}')
        payloads.append((k,grid,fields,pa,sp))
    b = port.run(commands,len(commands))
    require(b[0]==[count], f'CMN {bank}: recuento C++')
    rows = []
    i = 1
    for k,grid,fields,pa,sp in payloads:
        pb = [r[:1]+(r[1:6] if sp.k else [])+(r[6:] if sp.p else []) for r in b[i:i+len(grid)]]
        i += len(grid)
        rows.append(dict(clip=k,cuadros=grid,original=pa,port=pb,
                         cotejo=compare(pa,pb,fields,[0 if f=='dur' else 1 if f.endswith('8') else .002 for f in fields])))
    return dict(cmn=bank,sha256=sha(raw),clips=rows,traza=orig.trace(c),
                limite='Mismo #AMA: duración ELF y evaluadores A/B vs cargador C++ + PoseDe/AlfaB/TintB. RGBA8 de A cuantizado con la misma regla de la API del port; no son valores de GS. Sin compositor ni llamadas al dibujo.')


def measure(a):
    root, out = a.raiz.resolve(), a.salida.resolve()
    require(out.parent.is_dir() and not out.exists(), '--salida debe ser una carpeta nueva con padre existente')
    src = (a.src or root/'src/pasarela_b2.cpp').resolve()
    raw = src.read_bytes()
    text = raw.decode('utf-8').replace('\r\n','\n')
    require(not a.paso_4px or 'REXCVAR_GET(dbz3_b2_paso_4px)' in text, '--paso-4px requiere el parche')
    require(not a.color_ama_pal or 'REXCVAR_GET(dbz3_b2_color_ama_pal)' in text, '--color-ama-pal requiere el parche')
    profile = getattr(a, 'presentacion_pal', False)
    require(not profile or 'REXCVAR_GET(dbz3_b2_presentacion_pal)' in text, '--presentacion-pal requiere NB2E')
    orig,tramo,radar,sitio_luz,Iso,Afs,leer_ama = deps(root)
    base = orig.Original(a.estado)
    out.mkdir()
    port = Port(text,tramo,out,a.cxx,a.paso_4px,a.color_ama_pal,profile)
    requested = set(a.focos or FOCI)
    r = dict(formato='nb2b-anim-v1',fuentes=dict(PAL=base.identity, cpp=str(src),cpp_sha256=sha(raw),sonda_sha256=port.digest),
              opciones=dict(paso_4px=a.paso_4px,color_ama_pal=a.color_ama_pal,aristas=a.aristas), focos={}, pendientes={})
    if profile:
        r['opciones']['presentacion_pal'] = True
    f, missing = r['focos'], r['pendientes']
    if 'movimiento' in requested:
        f['movimiento'] = walk(orig,base,port,a.aristas)
    if requested & {'pasos','saltos'}:
        m = pasos(orig,base,port)
        if 'pasos' in requested:
            f['pasos'] = {**m,'casos':[{k:v for k,v in row.items() if k!='cotejo_avance'} for row in m['casos']]}
        if 'saltos' in requested:
            f['saltos'] = dict(contador_y_x=[row for row in m['casos'] if row['caso'].startswith('otro')],
                               limite='Avance acotado por el reloj del salto (rama != Goku). No hay par 3D del clip.')
            missing['saltos_3D'] = 'Faltan poses y fin del clip; la regla de avance sí se ejecuta.'
    if 'transiciones' in requested:
        f['transiciones'] = iris(orig,base,port)
    if 'shenron' in requested:
        f['shenron'] = dict(reloj=shen_clock(orig,base,port))
        missing['shenron_3D'] = 'Falta cotejo de SPX/AMM/AMP con malla, cámara y mezcla en la escena.'
    if requested & {'aliados','radar','vinetas','shenron'}:
        afs, extent = iso_bank(Iso,Afs,a.iso,orig.EXPECTED_ELF_SHA256)
        r['fuentes']['iso'] = str(a.iso.resolve())
        for bank,(name,clips) in BANKS.items():
            key = 'shenron' if name=='shenron_ventana' else name
            if key not in requested:
                continue
            report = ama(orig,base,port,leer_ama,bank,entry(afs,extent,bank,b'#AMA'),clips)
            if key=='shenron':
                f[key]['ventana'] = report
            else:
                f[key] = report
        if 'radar' in requested:
            f['radar']['apertura_original'] = radar(base)
        if 'shenron' in requested:
            amb = entry(afs,extent,328,b'#AMB')
            spx = entry(afs,extent,329,b'#SPX')
            f['shenron']['cielo_luz_original'] = dict(cmn_328_sha256=sha(amb),cmn_329_sha256=sha(spx),**sitio_luz(amb))
    def judgments(d, omit_sound=False):
        if isinstance(d,dict):
            if 'muestras' in d and 'distintas' in d:
                yield d
            else:
                for k,v in d.items():
                    if k!='cotejo_sonido' or not omit_sound:
                        yield from judgments(v, omit_sound)
        elif isinstance(d,list):
            for v in d:
                if isinstance(v,(dict,list)) and (not isinstance(v,list) or v and isinstance(v[0],dict)):
                    yield from judgments(v, omit_sound)
    r['resumen'] = {}
    for key, data in f.items():
        j = list(judgments(data, key=='saltos'))
        r['resumen'][key] = dict(muestras=sum(x['muestras'] for x in j),distintas=sum(x['distintas'] for x in j))
    with (out/'cotejo.json').open('x',encoding='utf-8') as dest:
        json.dump(r,dest,ensure_ascii=True,allow_nan=False,indent=1)
        dest.write('\n')
    print(json.dumps(r['resumen']),flush=True)
    return 1 if any(x['distintas'] for x in r['resumen'].values()) else 2 if missing else 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, epilog='0: sin divergencias en lo comparado; 1: diferencias; 2: faltan datos; 3: error.')
    p.add_argument('focos', nargs='*', metavar='FOCO', help=' '.join(FOCI) + ' (por defecto todos)')
    p.add_argument('--self-check', '--autoprueba', dest='self_check', action='store_true', help='controles sin recursos ni compilador')
    p.add_argument('--raiz', type=Path, default=ROOT)
    p.add_argument('--estado', type=Path, help='estado fase1.p2s del oráculo PAL SLES-51839')
    p.add_argument('--iso', type=Path, help='ISO propia de B2 PAL para aliados, radar, viñetas y Shenron')
    p.add_argument('--src', type=Path, help='pasarela_b2.cpp alternativa (p.ej., copia parcheada)')
    p.add_argument('--paso-4px', action='store_true', help='activa el interruptor candidato en el arnés')
    p.add_argument('--color-ama-pal', action='store_true', help='activa la curva candidata de color en el arnés')
    p.add_argument('--presentacion-pal', action='store_true', help='activa el perfil conjunto NB2E solo en el arnés')
    p.add_argument('--aristas', type=int, default=12, help='máximo de aristas T1 a comparar (0 a 512)')
    p.add_argument('--cxx', default=str(CXX) if CXX.is_file() else shutil.which('clang++') or 'c++')
    p.add_argument('--salida', type=Path, help='carpeta nueva con padre existente; guarda JSON y el arnés')
    a = p.parse_args(argv)
    if a.self_check:
        selfcheck()
        return 0
    if set(a.focos)-set(FOCI) or not 0 <= a.aristas <= 512 or a.salida is None or a.estado is None:
        p.error('se requiere --estado, --salida, focos conocidos y 0 <= aristas <= 512')
    if set(a.focos or FOCI) & {'aliados', 'radar', 'vinetas', 'shenron'} and a.iso is None:
        p.error('se requiere --iso para aliados, radar, viñetas y Shenron')
    try:
        return measure(a)
    except FileNotFoundError as e:
        print(f'PENDIENTE_DATO: {e}', file=sys.stderr)
        return 2
    except (ValueError, KeyError, OSError, RuntimeError, ImportError, struct.error, zipfile.BadZipFile,
            subprocess.SubprocessError) as e:
        print(f'ERROR: {e}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())

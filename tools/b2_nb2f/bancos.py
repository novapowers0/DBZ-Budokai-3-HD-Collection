"""Bancos focales: ensamblado PAL verificado por palabra vs fragmentos C++ compilados."""
from pathlib import Path
import struct
import subprocess

from inventario import exigir, sha, original, fases


def ejecutar(argv, cwd, entrada=None):
    r = subprocess.run(list(map(str, argv)), cwd=cwd, input=entrada, encoding='utf-8',
                       errors='replace', capture_output=True, timeout=120)
    if r.returncode:
        raise ValueError(f'{argv[0]} rc={r.returncode}\n{r.stdout}{r.stderr}')
    return r.stdout


def musica_pal(o, base, tabla, casilla, datos):
    from b2_nb2c.pal import setup
    c, ev = setup(base, {0x276E40, 0x276E60})
    ydw = c.r32(c.gp - 0x7748)
    t1, t2 = c.r32(ydw+0x24), c.r32(ydw+0x2c)
    exigir(1 <= casilla < c.r32(ydw+0x20) and c.r32(ydw+0x28) > 2, 'fixture T1/T2 fuera de rango')
    for ptr, value in zip((t1+52*casilla+31, t2+40+10, t2+80+10), datos, strict=True):
        c.w8(ptr, value)
    c.hooks[0x123C18] = o.memset
    o.invoke(c, 0x27EEF0, 1, 2, casilla)
    raw = c.r8(0x699392)
    # Resolución estática de 001D5740: rama de byte con signo válido.
    # No dar semántica de reproducción a entradas 0/-1 del ELF.
    pista = (tabla[raw] if tabla[raw] > 0 else None) if raw < len(tabla) else (-1 if raw >= 128 else None)
    return raw, pista, o.trace(c, ev)


def transiciones(o, base, va):
    from b2_nb2c import pal
    from b2_nb2b.b2_anim import putf, getf
    contactos = {}
    for lado in ('jugador', 'rival'):
        dato, traza = pal.salida_combate(base, lado)
        contactos[lado] = dict(resultado=dato, traza=traza)
    # La tabla de despacho es estática; las llamadas GS/SPU son fronteras, no píxeles/PCM.
    fn, = va(0x377EE0+10*4, '<I')
    exigir(fn == 0x249490, 'despacho de fundido 10 inesperado')
    c, ev = pal.setup(base, {fn})
    prog = c.gp-0x7594
    c.w32(c.gp-0x759C, o.TASK)  # tarea que inicializa 249FF0
    c.w32(c.gp-0x7BAC, 0)      # no simular espera del cargador
    c.w32(prog+16, 0)          # callback nulo
    paso = getf(c, prog+4)
    exigir(0 < paso < 1, 'paso del fundido inválido')
    volumen = []
    for addr, label in ((0x1331C0, 'gs_estado'), (0x138150, 'gs_primitiva'),
                        (0x139040, 'gs_rectangulo'), (0x139310, 'gs_fin'),
                        (0x12BB70, 'audio_fin')):
        o.service(c, addr, label, ev)
    c.hooks[0x147C70] = lambda cpu: volumen.append(o.ee.fv(cpu.f[12]))
    o.invoke(c, 0x249FF0, 10, 0)
    inicial = getf(c, prog)
    samples = []
    for i in range(1, 160):
        o.invoke(c, 0x249DD0, o.TASK)
        samples.append([i, getf(c, prog), volumen[-1]])
        if c.r32(o.TASK+0x1c) != 0x249DD0:
            break
    exigir(samples[-1][1] == 1.0 and len(samples) < 159, 'fundido no concluyó')

    c2, ev2 = pal.setup(base)
    vis = o.TASK + 0x200
    c2.w32(c2.gp-0x72D0, vis)
    for addr, value in ((vis+0x50,640.),(vis+0x54,448.),(c2.gp-0x7940,640.),(c2.gp-0x793C,448.)):
        putf(c2, addr, value)
    camara = []
    c2.hooks[0x23AF30] = lambda cpu: camara.append([o.ee.fv(cpu.f[k]) for k in (12, 13)])
    o.invoke(c2, 0x276FF0, 1)
    return dict(contactos=contactos, fundido10=dict(funcion=hex(fn), paso=paso, inicial=inicial,
                muestras=samples, traza=o.trace(c, ev)),
                camara=dict(pieza_xy=[640,448], vista_wh=[640,448], destino=camara, traza=o.trace(c2, ev2)),
                cobertura='Franja aislada PAL: contactos T4 sin guion; fundido y cámara sin compositor. Falta port sincronizado.')


def fragmentos_musica(path):
    from b2_nb2c.cpp import fragment
    src = (path / 'src/pasarela_b2.cpp').read_text(encoding='utf-8-sig')
    trad = (path / 'src/iw_trad_bucle.cpp').read_text(encoding='utf-8-sig')
    inicio = src.index('    if (callar) {\n      iso_musica = g_iso_b2;')
    audio = src[inicio:src.index(' else if (const int menu = PistaMenuB2()', inicio)].rstrip()
    bloc = fragment(src, 'int TableroOriginal(')
    wiring = [line for line in bloc.splitlines() if 'casilla_musica = ' in line or 'indice_musica = ' in line]
    i = trad.index('  s.combate = b.combate_b2; s.victoria = b.victoria_b2; s.salida = b.salida_b2;')
    j = trad.index('  if (b.retorno_perdedor_b2', i)
    trad_block = trad[i:j]
    if wiring:
        exigir(len(wiring)==2 and 'c.R8(0x699392)' in trad_block and
               'TableroOriginal(dl, a, t, auto_on, out, casilla_musica, indice_musica)' in src and
               'DibujarTablero(dl, a, t, pressed, auto_on, out, casilla_musica, indice_musica)' in src,
               'puente de música incompleto')
    nativos = [line for line in fragment(src, 'int DibujarTablero(').splitlines()
               if 'casilla_musica = ' in line or 'indice_musica = ' in line]
    return ('\n'.join(wiring), '\n'.join(nativos), audio, trad_block, fragment(src, 'int PistaCombateB2('))


def sonda_cpp(path, work, cxx, casos):
    trad, nat, audio, readback, tabla = fragmentos_musica(path)
    text = (Path(__file__).with_name('sonda_musica.cpp.in')).read_text(encoding='utf-8')
    for key, value in zip(('TRAD', 'NAT', 'AUDIO', 'READBACK', 'TABLA'), (trad, nat, audio, readback, tabla), strict=True):
        text = text.replace('@'+key+'@', value)
    source = work / 'sonda.cpp'
    source.write_text(text, encoding='utf-8')
    exe = work / 'sonda.exe'
    ejecutar([cxx, '-std=c++20', source, '-o', exe], work)
    out = ejecutar([exe], work, ''.join(' '.join(map(str, row))+'\n' for row in casos))
    result = [list(map(int, row.split())) for row in out.splitlines()]
    exigir(len(result)==len(casos) and all(len(r)==2 for r in result), 'salida C++ con aridad incorrecta')
    return result, sha(source.read_bytes())


def centro_cpp(path, work, cxx, muestras, candidato):
    from b2_nb2c.cpp import fragment
    src = (path/'src/pasarela_b2.cpp').read_text(encoding='utf-8-sig')
    dib = fragment(src, 'bool DibTarjeta(')
    i = dib.index('    const float u = std::clamp((c - 70.0f)')
    body = dib[i:dib.index('    dl->AddImage(tc[ic].id()', i)]
    if candidato:
        exigir('REXCVAR_DEFINE_BOOL(dbz3_b2_tarjeta_cond_pal, false,' in src and
               'leer_ama(410, d.tarjeta_cond_ama);' in src, 'cargador u opt-in AMA410 incompletos')
    else:
        body += '\n    const float y = 240.0f;'
    program = r'''
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <vector>
#define REXCVAR_GET(x) x
struct Pose { float px, py; };
struct Clip { Pose pose; std::vector<int> claves; };
Pose PoseDe(const Clip& c, float) { return c.pose; }
int main() {
  bool dbz3_b2_tarjeta_cond_pal, banco;
  float c, px, py;
  while (std::cin >> dbz3_b2_tarjeta_cond_pal >> banco >> c >> px >> py) {
    struct { std::vector<Clip> tarjeta_cond_ama; } g_d;
    if (banco) g_d.tarjeta_cond_ama.push_back({{px,py},{1}});
''' + body + '''
    std::cout << std::setprecision(9) << x << ' ' << y << '\\n';
  }
}
'''
    f, exe = work/'centro.cpp', work/'centro.exe'
    f.write_text(program, encoding='utf-8')
    ejecutar([cxx, '-std=c++20', f, '-o', exe], work)
    opts = [(0,1),(1,0),(1,1)] if candidato else [(0,0)]
    out = ejecutar([exe], work, ''.join(' '.join(map(str, (*op,*s)))+'\n' for op in opts for s in muestras))
    rows = [list(map(float,row.split())) for row in out.splitlines()]
    exigir(len(rows) == len(opts)*len(muestras), 'salida de la sonda de texto incompleta')
    return rows, sha(f.read_bytes())


def banco(a, deps, copia, work):
    d, va, tabla, _ = original(a, deps)
    o = deps[0]
    base = o.Original(a.estado)
    filas, inputs = [], []
    for f in fases(a, d):
        vistos = set()
        for tile in f['casillas']:
            idx = tile['musica']
            if idx in vistos:
                continue
            vistos.add(idx)
            # Cargamos el byte representativo del #YDW; la pieza/casilla son fixtures de fase1.
            filas.append((f'fase{f["fase"]}/t1/{tile["casilla"]}', (idx,0,0)))
        for ficha in f['t2_musica']:
            filas.append((f'fase{f["fase"]}/t2/{ficha["pieza"]}', (f['casillas'][0]['musica'],0,ficha['indice'])))
    filas += [('t2/jugador', (17,1,0)), ('t2/rival', (17,0,4)), ('t2/ambos', (17,1,4)),
              ('t2/rival-sin-indice', (17,1,255)), ('tabla/sentinel-negativo', (9,0,0)),
              ('tabla/entrada-cero', (23,0,0)), ('tabla/fuera', (127,0,0))]
    casos = []
    for key, fixture in filas:
        raw, pista, tr = musica_pal(o, base, tabla, 1, fixture)
        casos.append(dict(id=key, entrada_t1_t2=list(fixture), indice_pal=raw, adx_pal=pista, traza=tr))
        inputs.append((1, 1, 1, raw, 2, 0, 12))   # traducido, mod ON, ISO presente, byte leído, r, sin lista custom

    # Orden: desactivado, ISO ausente, cuadro r=0 con byte no nulo, lista propia y vía no traducida.
    # El último control conserva la guarda nativa de casilla fuera de rango (r == 2, pero sin pista).
    controles = [(1,0,1,17,2,0,12), (1,1,0,17,2,0,12), (1,1,1,17,0,0,12),
                 (1,1,1,17,2,1,12), (0,1,1,17,2,0,12), (0,1,1,17,2,0,13)]
    b, bh = sonda_cpp(a.base, work/'base', a.cxx, inputs+controles)
    p, ph = sonda_cpp(copia, work/'parche', a.cxx, inputs+controles)
    for i, row in enumerate(casos):
        row.update(base=b[i], parche=p[i])
        for version in ('base', 'parche'):
            pista = row['adx_pal']
            row[version+'_juicio'] = 'PENDIENTE' if pista is None else 'IGUAL' if row[version][1]==pista else 'DIF'
    from b2_nb2b import b2_anim as anim
    from b2_pm23_carga import tramo
    work_ama = work/'ama'
    work_ama.mkdir()
    text = (a.repo/'src/pasarela_b2.cpp').read_text(encoding='utf-8-sig')
    port_ama = anim.Port(text, tramo, work_ama, a.cxx, False, False)
    amas = [anim.ama(o, base, port_ama, d.leer_ama, i, d.entrada(0,i), clips)
            for i, clips in ((408, (6,7)), (410, (0,)))]
    clip = amas[-1]['clips'][0]
    campos = ['x','y']
    ia = [clip['cotejo']['campos'].index(k) for k in campos]
    pal_xy = [[r[i] for i in ia] for r in clip['original']]
    cpp_xy = [[r[i] for i in ia] for r in clip['port']]
    m = list(zip(clip['cuadros'], (x[0] for x in cpp_xy), (x[1] for x in cpp_xy), strict=True))
    base_xy, hb = centro_cpp(a.base, work/'base', a.cxx, m, False)
    cand, hp = centro_cpp(copia, work/'parche', a.cxx, m, True)
    n = len(m)
    pos = dict(base=base_xy, off=cand[:n], sin_banco=cand[n:2*n], on=cand[2*n:])
    exigir(pos['off'] == pos['sin_banco'] == pos['base'], 'OFF o banco ausente cambió la trayectoria base')
    tarjeta = dict(cuadros=clip['cuadros'], pal=pal_xy, port=pos, sha_sondas=[hb,hp],
                   cotejo={k: anim.compare(pal_xy, xy, campos, [.002,.002]) for k,xy in pos.items()},
                   limite='Ejecuta la decisión del centro en DibTarjeta. PoseDe está medido en el banco #AMA y se inyecta aquí; no compone píxeles.')
    return dict(formato='nb2f-banco-v1', original=base.identity, casos=casos,
                controles=dict(entrada=controles, base=b[len(casos):], parche=p[len(casos):]),
                sha_sondas=dict(base=bh, parche=ph, ama=port_ama.digest), transiciones=transiciones(o, base, va),
                ama=dict(bancos=amas, limite='408: título/pergamino del port. 410: banco de pose del texto. Sin composición.'),
                tarjeta=tarjeta)

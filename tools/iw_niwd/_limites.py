"""Nuevos bancos NIWD: original ejecutado y fragmentos del port compilados."""
import hashlib
from pathlib import Path
import re
from tempfile import TemporaryDirectory

BANCOS = {
    'intro': ('MAPA', 'pasarela_iw.cpp', 'dbz3_iw_mapa_intro_limite_original'),
    'low': ('0x7DE', 'iw_mini_disparo.cpp', 'dbz3_iw_7de_campo_low_original'),
    'rango-7d7': ('0x7D7', 'iw_mini_disparo.cpp', 'dbz3_iw_7d7_siete_aciertos_original'),
}
AJUSTES = {'serie': -1, 'off': 0, 'on': 1}
INTRO = 0x2F8790
SCROLL, UNLOCK = 0x2F8390, 0x2F8830


def intro_original(elf):
    import bubbles
    import ee
    from _iw_cotejo_focos import CTX, cpu_eu, llamar, wf
    from mapa_banco import G_M, G_PASO

    c, sha = cpu_eu(elf, {INTRO, *bubbles.leer_simbolos()})
    m, task = CTX, CTX + 0x4000
    reloj = m + 0x129 * 4
    c.w32(G_M, m)
    c.w32(m, task)
    casos = [('desde_cero_60', 0, [1] * 104, 0), ('desde_cero_50', 0, [1.2] * 88, 0),
             ('umbral_desde_099', .99, [1, 0, 1], 0),
             ('igualdad_y_desbloqueo', 1, [0, 0, 1], 1), ('superado', .995, [1], 1)]
    out = []
    for nombre, inicial, pasos, unlock in casos:
        c.w32(task + 0x1C, INTRO)
        c.w32(m + 0x128 * 4, unlock)
        wf(c, reloj, inicial)
        estados, reloj_bits, callback = [], [], []
        for paso in pasos:
            wf(c, G_PASO, paso)
            if c.r32(task + 0x1C) == INTRO:
                llamar(c, INTRO)
            fn = c.r32(task + 0x1C)
            if fn not in (INTRO, UNLOCK if unlock else SCROLL):
                raise RuntimeError(f'callback inesperado: {fn:#x}')
            callback.append(hex(fn))
            estados.append(int(fn != INTRO))
            reloj_bits.append(f'{c.r32(reloj):08x}')
        out.append(dict(caso=nombre, inicial=inicial, pasos=pasos, desbloqueo=unlock, listo=estados,
                        reloj_bits=reloj_bits, callback=callback))
    return dict(elf_sha256=sha, region='PAL', casos=out)


def low_original(elf, region):
    from disparo_low import LowCell, bits
    from disparo_banco import ELF_HASH, sha256

    sha = sha256(elf)
    if sha != ELF_HASH[region]:
        raise ValueError(f'ELF {region} no coincide con el perfil: {sha}')
    s = LowCell(region, elf)
    s.preparar_aim(bits(.05), False)
    entrada = s.pasar_aim()
    if entrada['estado'] != 'low' or not any(e['tipo'] == 'timer_ui' and e['args'] == [3] for e in entrada['eventos']):
        raise RuntimeError('Aim no ha armado la espera con pausa')
    filas = []
    for i in range(1, 501):
        f = s.pasar_low()
        filas.append(dict(campo=i, estado=f['estado'], reloj_bits=f['reloj_bits'],
                          timer_ui=[e['args'][0] for e in f['eventos'] if e['tipo'] == 'timer_ui']))
        if f['estado'] == 'aim':
            break
    if filas[-1]['estado'] != 'aim' or filas[-1]['timer_ui'] != [1]:
        raise RuntimeError('Low no ha vuelto a Aim con reanudación')
    return dict(elf_sha256=sha, region=region, entrada=entrada, campos=filas)


def intro_cpp(cpp):
    from iw_niwb._sonda_cpp import defecto, trozo
    cvar = BANCOS['intro'][2]
    serie = defecto(cpp, cvar)
    metodo = trozo(cpp, 'bool PantallaDM::EnIntroMapa() const {', '\n}\n').replace('PantallaDM::', '') + '\n}'
    cuerpo = trozo(cpp, '  // EU 0x2F8790 Story_MapIntroDelayTick\n', '\n  // EU 0x2F98D0')
    retorno = trozo(cpp, '    if (volviendo_) {\n      t_intro_ = ', '\n      ap_t_ = ')
    salto = trozo(cpp, '  if (EnIntroMapa() || bloqueo_scroll_) {', '\n      REXLOG_INFO')
    return r'''#include <iostream>
#define REXLOG_INFO(...) ((void)0)
struct Intro {
  float t_intro_, ap_t_ = 0, tcam_ = 0;
  bool bloqueo_scroll_ = false;
  bool limite_original;
#define REXCVAR_GET(name) limite_original
''' + metodo + r'''
  bool paso(float dt) {
    struct { int size() const { return 1; } } cas;
''' + cuerpo + r'''
    return listo;
  }
  void volver() {
    const bool volviendo_ = true;
''' + retorno + r'''
    }
  }
  void start() {
    const int kStart = 1, pulsado = kStart;
''' + salto + r'''
    }
   }
  }
};
int main() {
  char modo; float inicial, paso; int n, ajuste;
  while (std::cin >> ajuste >> modo >> inicial >> n) {
    Intro x{inicial};
    x.limite_original = ajuste < 0 ? ''' + serie + r''' : ajuste != 0;
    if (modo == 'v') x.volver();
    if (modo == 's') x.start();
    while (n--) {
      if (!(std::cin >> paso)) return 2;
      std::cout << int(x.paso(paso / 60.f)) << ' ';
    }
    std::cout << int(x.EnIntroMapa()) << ' ' << int(x.bloqueo_scroll_) << '\n';
  }
}'''


def low_cpp(cpp):
    from iw_niwb._sonda_cpp import defecto, trozo
    duracion = trozo(cpp, '        } else if (sub15_ == 4) {', '\n        }\n      }\n      if (continuo')
    reloj = trozo(cpp, '      if (continuo && fase_ == kJuego) {\n        if (sub15_ != 4) CellCuadro', '\n      if (continuo) Pitido15')
    cvar = BANCOS['low'][2]
    serie = defecto(cpp, cvar)
    cvar_pausa = 'dbz3_iw_7de_pausa_reloj_original'
    pausa = defecto(cpp, cvar_pausa)
    return r'''#include <iostream>
#include <cstdlib>
#define REXLOG_INFO(...) ((void)0)
int main() {
  constexpr int kJuego = 1, fase_ = kJuego, d = 0;
  int ajuste, aim, cont, pausa, n;
  while (std::cin >> ajuste >> aim >> cont >> pausa >> n) {
    int sub15_ = 4, t15_ = 0, reloj15_ = 999, cell = 0;
    const bool fuego_elf_ = aim, continuo = cont;
    auto CellCuadro = [&](int) { ++cell; };
    const bool ''' + cvar + ' = ajuste < 0 ? ' + serie + r''' : ajuste != 0;
    const bool ''' + cvar_pausa + ' = pausa < 0 ? ' + pausa + r''' : pausa != 0;
    #define REXCVAR_GET(name) (name)
    for (int i = 0; i < n; ++i) {
      if (sub15_ == 0) break;  // solo se mide Low hasta salir, incluido el campo de salida
      if (false) {
''' + duracion + r'''
      }
''' + reloj + r'''
    }
    std::cout << sub15_ << ' ' << t15_ << ' ' << 999-reloj15_ << ' ' << cell << '\n';
  }
}'''


def compilar(codigo, cxx, d, entrada):
    from iw_niwb._sonda_cpp import run
    fuente, exe = d / 'sonda.cpp', d / 'sonda.exe'
    fuente.write_text(codigo, encoding='utf-8')
    run([cxx, '-std=c++17', '-O2', '-ffp-contract=off', '-o', str(exe), str(fuente)])
    filas = [list(map(int, fila.split())) for fila in run([str(exe)], input=entrada).splitlines()]
    if len(filas) != len(entrada.splitlines()):
        raise RuntimeError('salida truncada de la sonda')
    return filas


def primer_listo(f):
    return next((i for i, v in enumerate(f, 1) if v), None)


def comparar_intro(original, cpp, cxx, d, ajuste):
    casos = original['casos']
    entrada = ''.join(f'{ajuste} n {x["inicial"]} {len(x["pasos"])} {" ".join(map(str, x["pasos"]))}\n' for x in casos)
    filas = compilar(intro_cpp(cpp), cxx, d, entrada + f'{ajuste} s 0 2 0 1\n{ajuste} v 0 2 0 1\n')
    if any(len(f) != len(c['pasos']) + 2 for f, c in zip(filas, casos)):
        raise RuntimeError('campos de intro incompletos')
    resultados = [dict(caso=c['caso'], original=primer_listo(c['listo']), port=primer_listo(f[:-2]),
                       campos_diferentes=[i for i, (a,b) in enumerate(zip(c['listo'], f[:-2]), 1) if a != b], listo_port=f[:-2])
                  for c, f in zip(casos, filas)]
    controles = dict(start=filas[-2], vuelta=filas[-1])
    return dict(casos=resultados, controles=controles,
                coincide=all(not x['campos_diferentes'] for x in resultados) and all(f == [1,1,0,0] for f in controles.values()))


def comparar_low(original, cpp, cxx, d, ajuste_niwd):
    limites = {len(v['campos']) for v in original}
    if len(limites) != 1:
        raise RuntimeError('los perfiles originales requieren comparaciones separadas')
    fin = limites.pop()
    casos = [(aim, continuo, ajuste, n) for aim in (0, 1)
             for continuo, ajuste in ((0,-1), (1,-1), (1,0), (1,1)) for n in (1, fin-2, fin-1, fin)]
    filas = compilar(low_cpp(cpp), cxx, d, ''.join(' '.join(map(str, (ajuste_niwd, *f)))+'\n' for f in casos))
    if any(len(f) != 4 for f in filas):
        raise RuntimeError('campos de Low incompletos')
    # Se compara el callback de salida. Reloj y Cell quedan como controles separados
    # del comportamiento del host, ya que el banco original intercepta esos servicios.
    resultados = []
    for (aim, cont, ajuste, n), (sub, ticks, reloj, cell) in zip(casos, filas):
        want = 0 if original[0]['campos'][n-1]['estado'] == 'aim' else 4
        control = cell == int(cont and sub == 0) and (
            reloj == 0 if not cont else reloj == ticks if ajuste == 0 and not aim else reloj == cell)
        resultados.append(dict(mira='elf' if aim else 'respaldo', continuo=bool(cont), pausa=ajuste,
                               campo=n, subfase_original=want, subfase_cpp=sub, campos_cpp=ticks,
                               descuentos=reloj, llamadas_cell=cell, coincide=sub == want, control=control))
    return dict(casos=resultados, coincide=all(x['coincide'] and x['control'] for x in resultados))


def medir(repo, a):
    from disparo_banco import ELF_EU, ELF_US
    from ._rango import comparar_rango, rango_original
    seleccion = BANCOS if a.foco == 'todos' else {a.foco: BANCOS[a.foco]}
    ajustes = AJUSTES if a.ajuste == 'todos' else (a.ajuste,)
    pal = (a.elf_pal or ELF_EU).resolve()
    usa = (a.elf_usa or ELF_US).resolve()
    resultados = {}
    for foco, (curso, nombre, cvar) in seleccion.items():
        if foco == 'intro':
            originales = intro_original(pal)
            comparar = comparar_intro
            alcance = 'Transición de intro al callback siguiente; no se ejecuta el scroll ni el desbloqueo.'
        elif foco == 'low':
            originales = [low_original(pal, 'eu'), low_original(usa, 'us')]
            comparar = comparar_low
            alcance = 'Low a paso 1: salida y controles del contador continuo del host; no HUD, Aim ni Fire.'
        else:
            originales = rango_original(pal, usa)
            comparar = comparar_rango
            alcance = 'Rango 7D7 con tiempo inyectado y g_rango no disponible; no se mide el cómputo del tiempo ni la recompensa.'
        fuente = repo / 'src' / nombre
        raw = fuente.read_bytes()
        cpp = raw.decode('utf-8').replace('\r\n', '\n')
        prueba = dict(curso=curso, alcance=alcance, original=originales, cvar=cvar,
                      fuente=nombre, sha256=hashlib.sha256(raw).hexdigest(), ajustes={})
        with TemporaryDirectory(prefix='iw-niwd-') as folder:
            d = Path(folder)
            for modo in ajustes:
                prueba['ajustes'][modo] = comparar(originales, cpp, a.cxx, d, AJUSTES[modo])
        if fuente.read_bytes() != raw:
            raise RuntimeError('la fuente cambió durante el cotejo')
        resultados[foco] = prueba
    return dict(formato='iw-niwd-integrado-limites-v1', resultados=resultados), int(any(
        not r['coincide'] for v in resultados.values() for r in v['ajustes'].values()))

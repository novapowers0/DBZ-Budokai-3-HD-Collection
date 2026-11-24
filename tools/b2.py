#!/usr/bin/env python3
"""Tablero B2 (Dragon World): entrada unica CLI para las herramientas offline,
autopruebas, inventario y auditoria de cvars OFF.

Reutiliza sin duplicar las herramientas instaladas de HB2B, NB2A-NB2G
y los modulos base b2_juez / b2_oraculo:
  - carga         -> tools/b2_pm23_carga.py       (HB2B)
  - escenario     -> tools/b2_escenario.py        (HB2B)
  - diag-recursos -> tools/b2_diag_recursos.py    (HB2B)
  - capas         -> tools/b2_nb2a/censo.py       (NB2A)
  - animacion     -> tools/b2_nb2b/b2_anim.py     (NB2B)
  - eventos       -> tools/b2_nb2c/b2_eventos.py  (NB2C)
  - bateria       -> tools/b2_nb2d/b2.py          (NB2D)
  - perfil        -> tools/b2_nb2e.py             (NB2E)
  - batalla       -> tools/b2_nb2f/b2_batalla.py  (NB2F)
  - cierre11      -> tools/b2_cierre11.py        (NB2G)
  - juez          -> tools/b2_juez.py             (Base PM-23)
  - juez-bateria  -> tools/b2_juez_bateria.py     (Base PM-23)
  - oraculo       -> tools/b2_oraculo/banco.py    (Base ELF PAL)

Comandos propios (sin abrir el juego ni PCSX2):
  - inventario    : comprueba presencia, documentacion en TOOLS.md, --help y autopruebas
  - cvars         : lee REXCVAR_DEFINE_* en src/, cuenta usos y emite tabla de OFF con recomendacion
  - autoprueba    : ejecuta los controles propios + las autopruebas offline disponibles

Codigos de salida: 0 OK / coincide en su alcance; 1 diferencias o fallo; 2 datos/uso/pendiente; 3 error.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

# Registro declarativo de herramientas del tablero B2 (noche HB2/HB2B + NB2A-F, mas NB2G y base).
# Cada entrada define: id, lote, subcomando, ruta instalada, flag de autoprueba,
# referencia esperada en TOOLS.md y resumen.
HERRAMIENTAS = (
    dict(
        id='hb2b-carga', lote='HB2B', modo='carga',
        rel='tools/b2_pm23_carga.py', selfcheck='--self-check',
        doc_token='tools/b2_pm23_carga.py',
        funcion='Cotejo offline de fichas iniciales PM-23 (9 fases, ELF PAL vs C++)',
    ),
    dict(
        id='hb2b-escenario', lote='HB2B', modo='escenario',
        rel='tools/b2_escenario.py', selfcheck='--self-check',
        doc_token='tools/b2_escenario.py',
        funcion='Generador de guiones cortos .toml (pm23, radar, cierre11, shenron) con preflight',
    ),
    dict(
        id='hb2b-diag', lote='HB2B', modo='diag-recursos',
        rel='tools/b2_diag_recursos.py', selfcheck='--self-check',
        doc_token='tools/b2_diag_recursos.py',
        funcion='Sonda ELF/ISO de radar PAL y recursos de cielo/luz de Shenron (CMN 328)',
    ),
    dict(
        id='nb2a-capas', lote='NB2A', modo='capas',
        rel='tools/b2_nb2a/censo.py', selfcheck='--autoprueba',
        doc_token='tools/b2_nb2a/censo.py',
        funcion='Censo AFS/YDW de 9 fases y 11 pantallas + cotejo C++ de capas PAL (490 casos)',
    ),
    dict(
        id='nb2b-anim', lote='NB2B', modo='animacion',
        rel='tools/b2_nb2b/b2_anim.py', selfcheck='--self-check',
        doc_token='tools/b2_nb2b/b2_anim.py',
        funcion='Cotejo de avance 4 px, pasos, saltos, fundidos #AMA, iris y reloj de Shenron',
    ),
    dict(
        id='nb2c-eventos', lote='NB2C', modo='eventos',
        rel='tools/b2_nb2c/b2_eventos.py', selfcheck='--autoprueba',
        doc_token='tools/b2_nb2c/b2_eventos.py',
        funcion='Juez de 18 contratos de eventos PAL (Popo, dialogo, combate, premio, Dende, cierres)',
    ),
    dict(
        id='nb2d-bateria', lote='NB2D', modo='bateria',
        rel='tools/b2_nb2d/b2.py', selfcheck='--self-check',
        doc_token='tools/b2_nb2d/b2.py',
        funcion='Bateria offline unificada NB2A-D (recursos, animacion, eventos, radar, popo, pm23)',
    ),
    dict(
        id='nb2e-perfil', lote='NB2E', modo='perfil',
        rel='tools/b2_nb2e.py', selfcheck='--self-check',
        doc_token='tools/b2_nb2e.py',
        funcion='Diagnostico conjunto del perfil PAL y generador de laminas comparativas',
    ),
    dict(
        id='nb2f-batalla', lote='NB2F', modo='batalla',
        rel='tools/b2_nb2f/b2_batalla.py', selfcheck='--self-check',
        doc_token='tools/b2_nb2f/b2_batalla.py',
        funcion='Censo de combates/musica PAL, cotejo de tarjeta Battle! (CMN 410) y juez de trazas',
    ),
    dict(
        id='nb2g-cierre11', lote='NB2G', modo='cierre11',
        rel='tools/b2_cierre11.py', selfcheck='--self-check',
        doc_token='b2_cierre11.py',
        funcion='Salto dirigido a cierre 11/Shenron y juez de hitos de dbz3.log',
    ),
    dict(
        id='base-juez', lote='BASE', modo='juez',
        rel='tools/b2_juez.py', selfcheck='--autoprueba',
        doc_token='b2_juez.py',
        funcion='Juez diferencial b2-state-v1 (alineacion por fase/hito, sin PASS por ausencia)',
    ),
    dict(
        id='base-juez-bat', lote='BASE', modo='juez-bateria',
        rel='tools/b2_juez_bateria.py', selfcheck=None, fuera_bateria='sin bandera propia; usa b2_juez',
        doc_token='b2_juez_bateria.py',
        funcion='Bateria de 9 fases sobre tools/b2_juez.py (0 OK / 1 FAIL / 2 PENDIENTE_DATO)',
    ),
    dict(
        id='base-oraculo', lote='BASE', modo='oraculo',
        rel='tools/b2_oraculo/banco.py', selfcheck='--autoprueba', fuera_bateria='requiere datos locales y fuentes revisadas',
        doc_token='b2_oraculo',
        funcion='Oraculo ELF PAL (SLES-51839) vs modelos Python revisados con sellos SHA-256',
    ),
)

DELEGACIONES = {h['modo']: h for h in HERRAMIENTAS}

# Evidencia historica de los lotes indicados; `cvars` no vuelve a ejecutar esos bancos o partidas.
# Las lineas y valores por defecto reales SIEMPRE se leen de src/ al ejecutar `cvars`.
CVARS_GUIA = {
    # --- Grupo A: Integrados en la noche (HB2B, NB2A-F y NB2G) ---
    'dbz3_b2_pm23_slot_inicial_pal': dict(
        grupo='noche', lote='AP_HB2B', recomendacion='Mantener OFF',
        evidencia_offline='b2_pm23_carga.py --slot-pal: 1 -> 0 difs en fichas de carga (F7 ficha 4 c=26); 9 fases PENDIENTE_DATO (sin partida).',
        evidencia_juego='AP_HB2B PASS en fase VII (74 s, 21 fichas iguales al cargar), pero sin cotejo visual sincronizado de fichas 3 y 4 al inicio.',
        criterio_on='Comprobar visualmente en fase VII que fichas 3 y 4 (casilla 15) no se solapan ni rompen el guion inicial.',
    ),
    'dbz3_b2_capas_tablero_pal': dict(
        grupo='noche', lote='AP_NB2A', recomendacion='Mantener OFF',
        evidencia_offline='b2_nb2a/censo.py: 27/490 -> 490/490 selecciones iguales en 9 fases + 4 controles sin textura OK.',
        evidencia_juego='AP_NB2A PASS en fase VII (70,7 s): 2 Popos y 2 senales visibles; Dende y Bolas sin identificar en esas tomas.',
        criterio_on='Confirmar en juego Dende y al menos una Bola visible en suelo sin artefactos de recorte/orden.',
    ),
    'dbz3_b2_paso_4px': dict(
        grupo='noche', lote='AP_NB2B', recomendacion='Mantener OFF',
        evidencia_offline='b2_nb2b/b2_anim.py: 752/798 -> 27/798 difs (770/770 iguales sin cambio de hueco); saltos sigue 89/90 difs.',
        evidencia_juego='AP_NB2B PASS solo en dialogo de aliados (14,5 s); 0 desplazamientos de ficha medidos en juego.',
        criterio_on='Verificar visualmente un trayecto sin contacto y una transicion con cambio de hueco sin tirones.',
    ),
    'dbz3_b2_color_ama_pal': dict(
        grupo='noche', lote='AP_NB2B', recomendacion='Mantener OFF',
        evidencia_offline='b2_nb2b/b2_anim.py: aliados 69->0/240, vinetas 39->0/258, radar 150->1/322 (residuo escala clip 89 cuadro 121).',
        evidencia_juego='AP_NB2B PASS en foto fija de confirmacion de Krillin; no mide rampa de entrada/salida ni campo alineado.',
        criterio_on='Verificar entrada y salida del velo de aliados y una vineta en campos equivalentes contra PAL.',
    ),
    'dbz3_b2_radar_indicadores_pal': dict(
        grupo='noche', lote='AP_NB2D', recomendacion='Mantener OFF',
        evidencia_offline='b2_nb2d/b2.py radar: 36/90 -> 0/90 difs en solicitudes de sprites y reloj (0 insns desconocidas).',
        evidencia_juego='AP_NB2D FAIL (66 s): 0 volcados con base/brillo del radar ni llegada al turno rival.',
        criterio_on='Partida con radar abierto e indicador visible en turno jugador, confirmando ocultacion al pasar al rival.',
    ),
    'dbz3_b2_popo_hueco_pal': dict(
        grupo='noche', lote='AP_NB2D', recomendacion='Mantener OFF',
        evidencia_offline='b2_nb2d/b2.py popo: 5/8 -> 0/8 difs en ruta 5->24 con destino vacio o compartido con aliado; no cubre destino bloqueado.',
        evidencia_juego='AP_NB2D no ejecuto prueba de alfombra en partida.',
        criterio_on='Aterrizar con Popo sobre casilla con aliado y comprobar huecos sin solape en turnos siguientes.',
    ),
    'dbz3_b2_presentacion_pal': dict(
        grupo='noche', lote='AP_NB2E', recomendacion='Mantener OFF',
        evidencia_offline='288/288 combinaciones de selector; con perfil ON replica exactamente capas (490/490), paso 4 px (770/770) y color AMA.',
        evidencia_juego='AP_NB2E FAIL (42 s) por timeout de 25 s en fase VII; solo 2 senales en captura de fallo, sin Dende/Bolas/avance/fundido.',
        criterio_on='Promover solo cuando las 3 piezas (capas, paso 4 px, color AMA) pasen cotejo visual; util hoy como toggle unico de prueba.',
    ),
    'dbz3_b2_tarjeta_cond_pal': dict(
        grupo='noche', lote='AP_NB2F', recomendacion='Mantener OFF',
        evidencia_offline='b2_nb2f/b2_batalla.py: 111/193 -> 0/193 difs en rebote CMN 410/0 (x=234->231->224); respaldo automatico si faltan claves.',
        evidencia_juego='AP_NB2F FAIL (177 s): su guion activo dbz3_b2_traducido (deshabilitado) y dejo tarjeta_cond_pal OFF.',
        criterio_on='Probar el guion dirigido con DBZ3_TEST_B2_TARJETA=0 y dbz3_b2_tarjeta_cond_pal=true para verificar el rebote visual.',
    ),
    'dbz3_b2_shenron_luz_alfa_pal': dict(
        grupo='noche', lote='AP_NB2G', recomendacion='Mantener OFF',
        evidencia_offline='HB2/HB2B/NB2G: 634/1280 entradas CLUT translucidas de CMN 328/12 (fl=0x21) pasaban a opaco 255 -> 0/1280 difs.',
        evidencia_juego='AP_NB2G PASS en smoke_b (56,1 s, 5 texturas alfa PAL, AMP exacto, sonido cuadro 6, UV 0xF0); falta revision visual de las 6 capturas vs PS2.',
        criterio_on='Revisar visualmente 03_columna_luz.png de AP_NB2G para confirmar mezcla aditiva limpia sin velo opaco.',
    ),
    # --- Grupo B: Otros cvars OFF del tablero B2 previos en src/ ---
    'dbz3_b2_alfombra_fiel': dict(
        grupo='tablero_previo', lote='B2-P05', recomendacion='Mantener OFF (sin uso en C++)',
        evidencia_offline='0 lecturas REXCVAR_GET en todo src/ (solo declarada en pasarela_b2.cpp y expuesta en quick_settings.cpp).',
        evidencia_juego='Sin efecto de tablero demostrado en partida.',
        criterio_on='Limpiar declaracion/UI o cablear de verdad la ruta FUN_00273210 con banco PAL.',
    ),
    'dbz3_b2_poscombate_fiel': dict(
        grupo='tablero_previo', lote='B2-P05', recomendacion='Mantener OFF (sin uso en C++)',
        evidencia_offline='0 lecturas REXCVAR_GET en todo src/ (solo declarada en pasarela_b2.cpp y expuesta en quick_settings.cpp).',
        evidencia_juego='Sin efecto de tablero demostrado en partida.',
        criterio_on='Limpiar declaracion/UI o cablear de verdad FUN_002360B0 con banco PAL.',
    ),
    'dbz3_b2_audio_dende_tarea': dict(
        grupo='tablero_previo', lote='B2-previo', recomendacion='Mantener OFF (sin uso en C++)',
        evidencia_offline='0 lecturas REXCVAR_GET en todo src/ (declaracion muerta en pasarela_b2.cpp).',
        evidencia_juego='Sin efecto demostrado en partida.',
        criterio_on='Retirar declaracion muerta en una limpieza de mantenimiento.',
    ),
    'dbz3_b2_traducido': dict(
        grupo='tablero_previo', lote='R18', recomendacion='Mantener OFF (deshabilitada)',
        evidencia_offline='src/iw_gs_traductor.cpp:71-76 fija ActivoB2() { return false; } por politica R18.',
        evidencia_juego='AP_NB2F confirmo que activarla no activa la ruta traducida (tablero nativo sigue como autoridad unica).',
        criterio_on='Politica del proyecto: conservar el ELF solo para bancos offline.',
    ),
    'dbz3_b2_panel_prefijo': dict(
        grupo='tablero_previo', lote='B2-P09', recomendacion='Mantener OFF',
        evidencia_offline='Fallback heuristico en pasarela_b2.cpp:5275 sin banco PAL de cobertura de todos los paneles.',
        evidencia_juego='Sin evidencia comparada contra original.',
        criterio_on='Medir todos los personajes afectados frente al ELF PAL antes de activarla.',
    ),
    'dbz3_b2_anim_datos': dict(
        grupo='tablero_previo', lote='B2-dev', recomendacion='Mantener OFF (dev)',
        evidencia_offline='Instrumentacion de desarrollo: lee b2_animaciones.toml (pasarela_b2.cpp:3281).',
        evidencia_juego='No es correccion de paridad para usuarios.',
        criterio_on='Conservar siempre OFF de serie (herramienta de calibracion).',
    ),
    'dbz3_b2_vineta_retrato_t': dict(
        grupo='tablero_previo', lote='B2-previo', recomendacion='Mantener OFF',
        evidencia_offline='Lee tabla parcial b2_vinetas.toml (pasarela_b2.cpp:5729); NB2C mantiene 3 pendientes en dialogo.',
        evidencia_juego='Sin cobertura completa de retratos T verificada.',
        criterio_on='Completar tabla contra PAL y validar en juego.',
    ),
    'dbz3_b2_vineta_demora_cuadros': dict(
        grupo='tablero_previo', lote='B2-dev', recomendacion='Mantener en 0 (OFF)',
        evidencia_offline='Demora artificial antes del combate (pasarela_b2.cpp:11823); demora original no confirmada.',
        evidencia_juego='Sin evidencia PAL que justifique demora distinta de 0.',
        criterio_on='Mantener 0 salvo medicion sincronizada con PAL.',
    ),
    'dbz3_b2_audio_fundido': dict(
        grupo='tablero_previo', lote='B2-previo', recomendacion='Mantener OFF',
        evidencia_offline='Fundido secuencial de musica con tasas del ELF (pasarela_b2.cpp:12386), sin banco de mezcla audible.',
        evidencia_juego='Sin cotejo auditivo (riesgo de cortes o solapes de pista).',
        criterio_on='Validacion auditiva en transiciones de tablero y combate.',
    ),
    'dbz3_b2_avisos': dict(
        grupo='tablero_previo', lote='B2-07', recomendacion='Mantener OFF',
        evidencia_offline='Tiras de texto inventadas por el port (pasarela_b2.cpp:1176,9296); el original PAL no las tiene.',
        evidencia_juego='Contradice la fidelidad 1:1 si se enciende de serie.',
        criterio_on='Dejar siempre OFF de serie (opcion voluntaria de accesibilidad/debug).',
    ),
    'dbz3_b2_dende_espera': dict(
        grupo='tablero_previo', lote='B2T', recomendacion='Mantener OFF',
        evidencia_offline='Espera efecto CMN 397 6-9 antes de curar (pasarela_b2.cpp:3565); NB2C mantiene 4 campos de Dende pendientes.',
        evidencia_juego='Sin validacion de secuencia completa de curacion.',
        criterio_on='Cerrar los 4 pendientes de Dende en NB2C y validar visualmente.',
    ),
    'dbz3_b2_shenron_hd': dict(
        grupo='tablero_previo', lote='B2-P13-HD', recomendacion='Mantener OFF',
        evidencia_offline='Sustituye el modelo PS2 de Shenron por el de B3 HD (pasarela_b2.cpp:11628).',
        evidencia_juego='Opcion estetica; el original B2 usa su modelo PS2 con bigotes.',
        criterio_on='Dejar OFF de serie (preferencia estetica opcional del jugador).',
    ),
}

CPP_COMENTARIO_LITERAL_RE = re.compile(
    r'(?P<comentario>//[^\n]*|/\*[\s\S]*?\*/)|'
    r'R"(?P<delim>[^\s()\\]{0,16})\([\s\S]*?\)(?P=delim)"|'
    r'"(?:\\[\s\S]|[^"\\\n])*"|\'(?:\\[\s\S]|[^\'\\\n])*\''
)
REXCVAR_DEF_RE = re.compile(
    r'\bREXCVAR_DEFINE_(BOOL|INT32|STRING|FLOAT)\s*\(\s*(dbz3_\w+)\s*,\s*'
    r'("(?:\\.|[^"\\\n])*"|[^,\n]+)\s*,'
)
REXCVAR_GET_RE = re.compile(r'\bREXCVAR_GET\s*\(\s*(dbz3_\w+)\s*\)')


def en_blanco(texto: str) -> str:
    """Oculta un token conservando posiciones y numeros de linea."""
    return re.sub(r'[^\n]', ' ', texto)


def require(cond: bool, msg: str) -> None:
    if not cond:
        raise ValueError(msg)


def detectar_repo(explicit: Path | None = None) -> Path:
    """Detecta la raiz del proyecto tanto desde tools/b2.py como desde fabrica/NB2H/scripts/b2.py."""
    if explicit is not None:
        r = explicit.resolve()
        if (r / 'src' / 'pasarela_b2.cpp').is_file() and (r / 'tools' / 'b2_juez.py').is_file():
            return r
        raise FileNotFoundError(f'Raiz invalida en --repo: {r} (faltan src/pasarela_b2.cpp o tools/b2_juez.py)')
    candidatos = []
    here = Path(__file__).resolve()
    # Si esta instalado en <repo>/tools/b2.py -> parents[1]
    if len(here.parents) >= 2:
        candidatos.append(here.parents[1])
    candidatos.append(Path.cwd())
    if 'DBZ3_REPO' in os.environ:
        candidatos.append(Path(os.environ['DBZ3_REPO']))
    for c in candidatos:
        r = c.resolve()
        if (r / 'src' / 'pasarela_b2.cpp').is_file() and (r / 'tools' / 'b2_juez.py').is_file():
            return r
    raise FileNotFoundError(
        'No se encontro la raiz del proyecto (src/pasarela_b2.cpp y tools/b2_juez.py). '
        'Pasa --repo <ruta> antes del subcomando.'
    )


def resolver_herramienta(item: dict, repo: Path) -> Path | None:
    p = repo / item['rel']
    return p if p.is_file() else None


def ejecutar_py(script: Path, args: list[str], cwd: Path, capture: bool = False, timeout: int = 180):
    env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1', 'PYTHONUTF8': '1'}
    cmd = [sys.executable, '-B', '-X', 'utf8', str(script), *args]
    return subprocess.run(
        cmd, cwd=cwd, env=env, check=False, timeout=timeout,
        capture_output=capture, text=True, encoding='utf-8', errors='replace',
    )


def escanear_cvars(repo: Path, ambito: str = 'tablero', solo_off: bool = False) -> list[dict]:
    """Cuenta llamadas literales en src/, sin preprocesar macros ni inferir ejecucion."""
    src_dir = repo / 'src'
    require(src_dir.is_dir(), f'Falta directorio src en {repo}')
    archivos = sorted(p for p in src_dir.rglob('*') if p.suffix in ('.cpp', '.h', '.inl'))
    textos = {}
    get_por_cvar = {}
    for p in archivos:
        rel = p.relative_to(repo).as_posix()
        txt = p.read_text(encoding='utf-8', errors='replace')
        texto = CPP_COMENTARIO_LITERAL_RE.sub(
            lambda m: en_blanco(m[0]) if m['comentario'] else m[0], txt,
        )
        codigo = CPP_COMENTARIO_LITERAL_RE.sub(lambda m: en_blanco(m[0]), texto)
        textos[rel] = (texto, codigo)
        for m in REXCVAR_GET_RE.finditer(codigo):
            linea = codigo.count('\n', 0, m.start()) + 1
            get_por_cvar.setdefault(m[1], []).append(f'{rel}:{linea}')

    resultados = []
    for rel, (txt, codigo) in textos.items():
        es_tablero = rel in ('src/pasarela_b2.cpp', 'src/pasarela_b2_shenron.inl', 'src/iw_gs_traductor.cpp')
        for m in REXCVAR_DEF_RE.finditer(txt):
            if not codigo.startswith('REXCVAR_DEFINE_', m.start()):
                continue
            tipo, nombre, raw_def = m.group(1), m.group(2), m.group(3)
            if 'b2' not in nombre.lower() and 'b2' not in Path(rel).name.lower():
                continue
            if ambito == 'tablero' and not es_tablero:
                continue
            linea = txt.count('\n', 0, m.start()) + 1
            def_val = raw_def.strip()
            es_off = def_val in ('false', '0', '""', '0.0f', '0.0')
            if solo_off and not es_off:
                continue
            word_pat = re.compile(rf'\b{re.escape(nombre)}\b')
            lecturas = get_por_cvar.get(nombre, [])
            refs = [f'{f2}:{idx}' for f2, (t, _) in textos.items()
                    for idx, l in enumerate(t.splitlines(), 1) if word_pat.search(l)]
            guia = CVARS_GUIA.get(nombre) if es_off else None
            if es_off and guia is None:
                guia = dict(
                    grupo='escenarios_b2' if not es_tablero else 'sin_clasificar',
                    lote='ESCENARIOS' if not es_tablero else 'NUEVA',
                    recomendacion='Mantener OFF (revisar)' if es_tablero else 'Fuera de tablero B2',
                    evidencia_offline='Declarada en src/; sin ficha curada en b2.py.',
                    evidencia_juego='Pendiente de revision.',
                    criterio_on='Documentar evidencia PAL y prueba en juego.',
                )
            resultados.append(dict(
                nombre=nombre,
                archivo=rel,
                linea=linea,
                tipo=tipo,
                por_defecto=def_val,
                estado='OFF' if es_off else 'ON',
                lecturas_get=len(lecturas),
                ubicaciones_get=lecturas,
                referencias_totales=len(refs),
                en_quick_settings=any(r.startswith('src/ingame/quick_settings.cpp:') for r in refs),
                **(guia or dict(grupo='on_de_serie', lote='ACTIVA', recomendacion='ON (de serie)')),
            ))
    return resultados


def auditar_inventario(repo: Path, comprobar_help: bool = False, comprobar_sc: bool = False) -> list[dict]:
    docs_path = repo / 'docs' / '04_herramientas' / 'TOOLS.md'
    docs_txt = docs_path.read_text(encoding='utf-8', errors='replace') if docs_path.is_file() else ''
    filas = []
    for h in HERRAMIENTAS:
        ruta = resolver_herramienta(h, repo)
        existe = ruta is not None
        doc_ok = h['doc_token'] in docs_txt
        fila = dict(
            id=h['id'],
            lote=h['lote'],
            modo=h['modo'],
            ruta=str(ruta) if ruta else h['rel'],
            existe=existe,
            en_tools=existe,
            documentada_tools_md=doc_ok,
            funcion=h['funcion'],
            help_rc=None,
            selfcheck_flag=h['selfcheck'],
            selfcheck_rc=None,
            selfcheck_nota=None,
        )
        if existe and comprobar_help:
            r = ejecutar_py(ruta, ['--help'], repo, capture=True, timeout=30)
            fila['help_rc'] = r.returncode
        if existe and comprobar_sc:
            if h['selfcheck'] is None:
                fila['selfcheck_nota'] = 'sin_flag_propio'
            else:
                r = ejecutar_py(ruta, [h['selfcheck']], repo, capture=True, timeout=60)
                fila['selfcheck_rc'] = r.returncode
                lineas = (r.stdout + '\n' + r.stderr).strip().splitlines()
                if lineas:
                    fila['selfcheck_nota'] = (lineas[0] if r.returncode == 0 else lineas[-1])[:240]
        filas.append(fila)
    return filas


def autoprueba_propia() -> list[str]:
    """Controles deterministas de b2.py sin depender de discos, ELF, juego ni compilador."""
    notas = []
    # 1. Unicidad de IDs y modos en el registro
    ids = [h['id'] for h in HERRAMIENTAS]
    modos = [h['modo'] for h in HERRAMIENTAS]
    require(len(ids) == len(set(ids)), 'IDs duplicados en HERRAMIENTAS')
    require(len(modos) == len(set(modos)), 'modos duplicados en HERRAMIENTAS')
    require(len(HERRAMIENTAS) >= 12, 'registro incompleto de herramientas B2')
    notas.append(f'registro={len(HERRAMIENTAS)} herramientas unicas')

    # 2. Escaner de cvars sobre un arbol sintetico con ON, OFF usada, OFF muerta y OFF nueva
    with tempfile.TemporaryDirectory(prefix='b2-nb2h-sc-') as td:
        fake = Path(td)
        (fake / 'src' / 'ingame').mkdir(parents=True)
        (fake / 'src' / 'pasarela_b2.cpp').write_text(
            'REXCVAR_DEFINE_BOOL(dbz3_b2_capas_tablero_pal, false, "DBZ3/Dev", "demo");\n'
            'REXCVAR_DEFINE_BOOL(dbz3_b2_alfombra_fiel, false, "DBZ3/Dev", "muerta");\n'
            'REXCVAR_DEFINE_BOOL(dbz3_b2_popo_origen_pal, true, "DBZ3/Dev", "activa");\n'
            'REXCVAR_DEFINE_BOOL(dbz3_b2_nueva_sin_ficha, false, "DBZ3/Dev", "nueva");\n'
            'void f(){ REXCVAR_GET(dbz3_b2_capas_tablero_pal); REXCVAR_GET (\n'
            '  dbz3_b2_capas_tablero_pal); REXCVAR_GET(dbz3_b2_popo_origen_pal); }\n'
            '// REXCVAR_GET(dbz3_b2_alfombra_fiel)\n'
            '/* REXCVAR_DEFINE_BOOL(dbz3_b2_comentada, false, "Categoria", "Demo"); */\n'
            'auto nota = "REXCVAR_GET(dbz3_b2_alfombra_fiel)";\n',
            encoding='utf-8',
        )
        (fake / 'src' / 'ingame' / 'quick_settings.cpp').write_text(
            'Toggle("x", "dbz3_b2_alfombra_fiel", "y");\n', encoding='utf-8',
        )
        todas = escanear_cvars(fake, ambito='tablero', solo_off=False)
        off = escanear_cvars(fake, ambito='tablero', solo_off=True)
        by_name = {c['nombre']: c for c in todas}
        require(len(todas) == 4 and len(off) == 3, 'conteo ON/OFF sintetico incorrecto')
        require(by_name['dbz3_b2_capas_tablero_pal']['lecturas_get'] == 2, 'lecturas REXCVAR_GET no contadas')
        require(by_name['dbz3_b2_alfombra_fiel']['lecturas_get'] == 0 and by_name['dbz3_b2_alfombra_fiel']['en_quick_settings'],
                'cvar muerta expuesta en UI no detectada')
        require(by_name['dbz3_b2_nueva_sin_ficha']['grupo'] == 'sin_clasificar', 'cvar nueva debe marcarse sin_clasificar')
        notas.append('escaner_cvars=4/4 controles OK (incluye muerta y nueva)')

        # 3. Control negativo: detectar_repo rechaza una carpeta vacia
        try:
            detectar_repo(fake)
        except FileNotFoundError:
            notas.append('control_negativo_repo=rechaza carpeta invalida')
        else:
            raise AssertionError('detectar_repo acepto una carpeta sin pasarela_b2.cpp')

    # 4. Parser CLI acepta --help y los subcomandos esperados
    p = construir_parser()
    a = p.parse_args(['--self-check'])
    require(a.self_check and a.accion is None, 'parseo --self-check')
    a2 = p.parse_args(['cvars', '--solo-off'])
    require(a2.accion == 'cvars' and a2.solo_off, 'parseo cvars --solo-off')
    notas.append('cli=OK')
    return notas


def escribir_salida(destino: Path | None, texto: str) -> None:
    if destino is None:
        print(texto, end='' if texto.endswith('\n') else '\n')
    else:
        with destino.open('x', encoding='utf-8', newline='\n') as f:
            f.write(texto)


def formatear_inventario_md(filas: list[dict]) -> str:
    lines = [
        '| Lote | Subcomando | Ruta | TOOLS.md | --help | Autoprueba | Funcion |',
        '|---|---|---|---|---:|---|---|',
    ]
    for f in filas:
        doc = 'Si' if f['documentada_tools_md'] else 'No'
        h_rc = '-' if f['help_rc'] is None else str(f['help_rc'])
        if f['selfcheck_rc'] is not None:
            sc = f"`{f['selfcheck_flag']}` -> rc={f['selfcheck_rc']}"
        elif f['selfcheck_nota']:
            sc = f['selfcheck_nota']
        else:
            sc = f['selfcheck_flag'] or '-'
        ruta = Path(f['ruta']).name if f['existe'] else f"AUSENTE ({f['ruta']})"
        lines.append(f"| {f['lote']} | `b2.py {f['modo']}` | `{ruta}` | {doc} | {h_rc} | {sc} | {f['funcion']} |")
    return '\n'.join(lines) + '\n'


def formatear_cvars_md(cvars: list[dict]) -> str:
    lines = [
        '| Cvar | Archivo:Linea | Def | GET | Lote | Recomendacion | Evidencia resumida |',
        '|---|---|---|---:|---|---|---|',
    ]
    for c in cvars:
        ev = c.get('evidencia_offline', '')
        if c.get('evidencia_juego'):
            ev = f"{ev} Juego: {c['evidencia_juego']}".strip()
        lines.append(
            f"| `{c['nombre']}` | `{c['archivo']}:{c['linea']}` | `{c['por_defecto']}` | "
            f"{c['lecturas_get']} | {c.get('lote', '-')} | **{c.get('recomendacion', '-')}** | {ev} |"
        )
    return ('GET: llamadas literales en el codigo, sin preprocesar. '
            'La evidencia de los lotes es historica.\n\n' + '\n'.join(lines) + '\n')


def ejecutar_autoprueba_completa(repo: Path) -> tuple[int, dict]:
    notas_propias = autoprueba_propia()
    resultados = []
    fallos = 0
    excluidas = []
    for h in HERRAMIENTAS:
        if 'fuera_bateria' in h:
            excluidas.append(dict(id=h['id'], motivo=h['fuera_bateria']))
            continue
        ruta = resolver_herramienta(h, repo)
        if ruta is None:
            resultados.append(dict(id=h['id'], lote=h['lote'], estado='AUSENTE', rc=2, flag=h['selfcheck']))
            fallos += 1
            continue
        r = ejecutar_py(ruta, [h['selfcheck']], repo, capture=True, timeout=60)
        texto = (r.stdout + '\n' + r.stderr).strip()
        estado = 'OK' if r.returncode == 0 else 'FAIL'
        fallos += r.returncode != 0
        resultados.append(dict(id=h['id'], lote=h['lote'], ruta=str(ruta), flag=h['selfcheck'],
                               rc=r.returncode, estado=estado, salida=texto[-500:]))
    resumen = dict(
        formato='nb2h-autoprueba-v1',
        propia='OK',
        notas_propias=notas_propias,
        herramientas=resultados,
        ok=sum(1 for x in resultados if x['estado'] == 'OK'),
        excluidas=excluidas,
        fallos=fallos,
    )
    rc = 1 if fallos else 0
    return rc, resumen


def construir_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog='b2.py',
        description='Entrada unica CLI para el tablero de Budokai 2 (HB2B, NB2A-F, NB2G y base). No abre el juego ni PCSX2.',
        epilog='Codigos: 0 OK en su alcance; 1 diferencias/fallo; 2 datos faltantes/pendiente/uso; 3 error. '
               'Pasa --help tras cualquier subcomando delegado para ver sus opciones exactas.',
        allow_abbrev=False,
    )
    p.add_argument('--repo', type=Path, help='raiz del proyecto (antes del subcomando)')
    p.add_argument('--self-check', '--autoprueba', dest='self_check', action='store_true',
                   help='ejecuta los controles propios + autopruebas offline disponibles (sin juego, ISO ni compilador)')

    sub = p.add_subparsers(dest='accion')

    si = sub.add_parser('inventario', help='inventariar todas las herramientas B2, su doc en TOOLS.md y controles')
    si.add_argument('--comprobar-help', action='store_true', help='ejecuta --help en cada herramienta')
    si.add_argument('--comprobar-autoprueba', action='store_true', help='ejecuta la autoprueba de cada herramienta')
    si.add_argument('--json', type=Path, help='guardar JSON en un archivo nuevo')
    si.add_argument('--md', type=Path, help='guardar tabla Markdown en un archivo nuevo')

    sc = sub.add_parser('cvars', help='inventariar cvars B2 en src/ con referencias estaticas y recomendacion')
    sc.add_argument('--solo-off', action='store_true', help='mostrar solo las cvars apagadas (false/0)')
    sc.add_argument('--ambito', choices=('tablero', 'todos'), default='tablero',
                    help='tablero (pasarela_b2 + shenron + traductor) o todos los ficheros B2 en src/')
    sc.add_argument('--json', type=Path, help='guardar JSON en un archivo nuevo')
    sc.add_argument('--md', type=Path, help='guardar tabla Markdown en un archivo nuevo')

    sa = sub.add_parser('autoprueba', help='correr las autopruebas offline del conjunto B2')
    sa.add_argument('--exigir-todas', action='store_true',
                    help='compatibilidad con NB2H: la bateria sin datos ya exige todas sus pruebas por defecto')
    sa.add_argument('--json', type=Path, help='guardar informe JSON en un archivo nuevo')

    for h in HERRAMIENTAS:
        sub.add_parser(h['modo'], help=f"[{h['lote']}] {h['funcion']} (--help para opciones)", add_help=False)
    return p


def main(argv: list[str] | None = None) -> int:
    p = construir_parser()
    a, resto = p.parse_known_args(argv)
    if a.self_check and a.accion:
        p.error('--self-check/--autoprueba no se combina con un subcomando')
    if resto and a.accion not in DELEGACIONES:
        p.error(f'argumentos no reconocidos: {" ".join(resto)}')

    if not a.self_check and not a.accion:
        p.print_help()
        return 0

    try:
        repo = detectar_repo(a.repo)
        if a.self_check:
            rc, res = ejecutar_autoprueba_completa(repo)
            print(f"b2.py autoprueba propia: OK ({'; '.join(res['notas_propias'])})")
            print(f"Bateria sin datos: {res['ok']} OK, {res['fallos']} fallos")
            for item in res['herramientas']:
                print(f"  [{item['estado']}] {item['id']} ({item['flag']} rc={item['rc']})")
                if item['estado'] == 'FAIL':
                    print(item['salida'], file=sys.stderr)
            for item in res['excluidas']:
                print(f"  [FUERA DE BATERIA] {item['id']}: {item['motivo']}")
            return rc

        if a.accion == 'autoprueba':
            rc, res = ejecutar_autoprueba_completa(repo)
            if a.json:
                escribir_salida(a.json, json.dumps(res, ensure_ascii=False, indent=2) + '\n')
            else:
                print(json.dumps(res, ensure_ascii=False, indent=2))
            return rc

        if a.accion == 'inventario':
            filas = auditar_inventario(repo, a.comprobar_help, a.comprobar_autoprueba)
            doc = dict(formato='nb2h-inventario-v1', repo=str(repo), herramientas=filas)
            if a.json:
                escribir_salida(a.json, json.dumps(doc, ensure_ascii=False, indent=2) + '\n')
            if a.md:
                escribir_salida(a.md, formatear_inventario_md(filas))
            if not a.json and not a.md:
                print(formatear_inventario_md(filas), end='')
            # El inventario monitoriza resultados; su rc no sustituye los de los bancos.
            return 0 if all(f['existe'] for f in filas) else 2

        if a.accion == 'cvars':
            filas = escanear_cvars(repo, ambito=a.ambito, solo_off=a.solo_off)
            resumen = dict(
                total=len(filas),
                off=sum(1 for c in filas if c['estado'] == 'OFF'),
                on=sum(1 for c in filas if c['estado'] == 'ON'),
                sin_lectura_get=[c['nombre'] for c in filas if c['lecturas_get'] == 0],
            )
            doc = dict(formato='nb2h-cvars-v1', repo=str(repo), ambito=a.ambito,
                       solo_off=a.solo_off, resumen=resumen, cvars=filas)
            if a.json:
                escribir_salida(a.json, json.dumps(doc, ensure_ascii=False, indent=2) + '\n')
            if a.md:
                escribir_salida(a.md, formatear_cvars_md(filas))
            if not a.json and not a.md:
                print(formatear_cvars_md(filas), end='')
            return 0

        # Subcomandos delegados
        meta = DELEGACIONES[a.accion]
        script = resolver_herramienta(meta, repo)
        if script is None:
            print(f"PENDIENTE: no se encontro la herramienta para '{a.accion}' ({meta['rel']})", file=sys.stderr)
            return 2
        # Inyectar --repo / --proyecto / --raiz solo si la herramienta lo soporta y no vino en resto
        extra = []
        if '--help' not in resto and '-h' not in resto:
            opciones = {arg.split('=', 1)[0] for arg in resto if arg.startswith('--')}
            if a.accion in ('bateria', 'perfil', 'batalla', 'capas') and '--repo' not in opciones:
                extra = ['--repo', str(repo)]
            elif a.accion == 'animacion' and '--raiz' not in opciones:
                extra = ['--raiz', str(repo)]
            elif a.accion == 'eventos' and '--proyecto' not in opciones:
                extra = ['--proyecto', str(repo)]
        r = ejecutar_py(script, extra + resto, repo, capture=False, timeout=600)
        return r.returncode if r.returncode in (0, 1, 2, 3) else 3
    except FileNotFoundError as exc:
        print(f'PENDIENTE: {exc}', file=sys.stderr)
        return 2
    except (OSError, ValueError, KeyError, TypeError, AssertionError, subprocess.SubprocessError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())

#!/usr/bin/env python3
"""Infinite World: entrada unica CLI para inventario, censo, activos, cotejos,
autopruebas y auditoria de cvars OFF (HIW/HIWB, NIWA-NIWG y herramientas base).

Reutiliza sin duplicar las herramientas instaladas en tools/ (y, cuando aun no
estan instaladas, las entregas de fabrica/NIWE, NIWF y NIWG):
  - censo         -> tools/iw_censo.py + iw_niwc/iw_activos.py + iw_niwb/cotejo.py + iw_niwd/_limites.py
  - activos       -> tools/iw_niwc/iw_activos.py             (NIWC)
  - cotejo:
      limites     -> tools/iw_niwd/_limites.py               (NIWD)
      mapa        -> tools/iw_niwa/niwa.py                   (NIWA)
      focos       -> tools/iw_niwb/cotejo.py                 (NIWB)
      legado      -> tools/iw_cotejo.py                      (HIWB + Astra + GS + trazas)
      niwe        -> tools/iw_niwe/iw.py | fabrica/NIWE      (NIWE)
      ventanas    -> tools/iw_niwf/ventanas_iw.py | NIWF     (NIWF)
  - niwe          -> alias directo de `cotejo niwe`          (NIWE)
  - ventanas      -> alias directo de `cotejo ventanas`      (NIWF)
  - validar       -> tools/iw_validar.py | fabrica/NIWG      (NIWG)
  - log-7de       -> tools/iw_7de_log.py                     (AP_NIWG)
  - campana       -> tools/iw_campana.py                     (Base)
  - ladrillos     -> tools/astra_iw_ladrillos_cotejo.py      (Base)
  - comparar      -> tools/iw_mips/comparar.py               (Base)
  - comparar-trad -> tools/iw_mips/comparar_trad.py          (Base)
  - cierre-r23    -> tools/iw_cierre_r23.py                  (Base)

Comandos propios de gobierno (sin abrir el juego ni PCSX2):
  - inventario    : audita presencia en tools/, documentacion en TOOLS.md, --help y autopruebas
  - cvars         : lee REXCVAR_DEFINE_BOOL de IW en src/, cuenta menciones de REXCVAR_GET y lista las OFF
  - autoprueba    : ejecuta controles propios + autopruebas offline disponibles (--self-check / --autoprueba)

Codigos de salida: 0 OK / coincide en su alcance; 1 diferencias o fallo; 2 datos/uso/pendiente; 3 error.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

sys.dont_write_bytecode = True

TRABAJO_DEF = Path(os.environ.get('DBZ3_TRABAJO', r'E:\DBZ3HD_trabajo'))

BACKENDS = {
    'legado': 'iw_cotejo.py',
    'focos': 'iw_niwb/cotejo.py',
    'mapa': 'iw_niwa/niwa.py',
}

# Registro declarativo de todas las herramientas de Infinite World (HIW/HIWB, NIWA-G y base).
HERRAMIENTAS = (
    dict(
        id='hiw-censo', lote='HIW', modo=None, instalada=False, sustituida='HIWB',
        rel=None, fabrica_rel='fabrica/HIW/scripts/censo.py',
        selfcheck=None, alt_selfcheck=None, doc_token=None,
        funcion='Censo inicial de 20 cursos y 53 cvars IW (sustituida por tools/iw_censo.py de HIWB)',
    ),
    dict(
        id='hiw-cotejo', lote='HIW', modo=None, instalada=False, sustituida='HIWB',
        rel=None, fabrica_rel='fabrica/HIW/scripts/cotejo.py',
        selfcheck=None, alt_selfcheck=None, doc_token=None,
        funcion='Comparador inicial 7DD/7DE/MAPA (sustituida por tools/iw_cotejo.py de HIWB)',
    ),
    dict(
        id='hiwb-censo', lote='HIWB', modo='censo', instalada=True,
        rel='tools/iw_censo.py',
        selfcheck='--self-check', alt_selfcheck='--autoprueba',
        doc_token='tools/iw_censo.py',
        funcion='Censo de 20 cursos IW, booleanos dbz3_iw_* en src/ y #DMD 1093',
    ),
    dict(
        id='hiwb-cotejo', lote='HIWB', modo='cotejo legado', instalada=True,
        rel='tools/iw_cotejo.py',
        selfcheck='--self-check', alt_selfcheck='--autoprueba',
        doc_token='tools/iw_cotejo.py',
        funcion='Cotejo focal de 14 bancos IW, trazas JSONL, GS/VU1 y ladrillos Astra',
    ),
    dict(
        id='niwa-mapa', lote='NIWA', modo='cotejo mapa', instalada=True,
        rel='tools/iw_niwa/niwa.py',
        selfcheck='--self-check', alt_selfcheck='--autoprueba',
        doc_token='tools/iw_niwa/niwa.py',
        funcion='Banco sin emulador de vuelta Dragon Mission M11 y replay de logs del mapa',
    ),
    dict(
        id='niwb-focos', lote='NIWB', modo='cotejo focos', instalada=True,
        rel='tools/iw_niwb/cotejo.py',
        selfcheck='--self-check', alt_selfcheck='--autoprueba',
        doc_token='tools/iw_niwb/cotejo.py',
        funcion='Bancos PAL vs C++ compilado: pulsacion, palancas, rango 7D5 y captura 7DD',
    ),
    dict(
        id='niwc-activos', lote='NIWC', modo='activos', instalada=True,
        rel='tools/iw_niwc/iw_activos.py',
        selfcheck='--self-check', alt_selfcheck='--autoprueba',
        doc_token='tools/iw_niwc/iw_activos.py',
        funcion='Inventario de 165 filas de recursos visuales USA/PAL y sondas PAL de efectos/iconos',
    ),
    dict(
        id='niwd-iw', lote='NIWD/NIWH', modo='iw', instalada=True,
        rel='tools/iw.py',
        selfcheck='--self-check', alt_selfcheck='--autoprueba', sub_selfcheck='autoprueba',
        doc_token='tools/iw.py',
        funcion='Entrada unica IW: censo, activos, limites, delegaciones, inventario, cvars y autoprueba',
    ),
    dict(
        id='niwe-bancos', lote='NIWE', modo='niwe', instalada=False,
        rel='tools/iw_niwe/iw.py',
        fabrica_rel='fabrica/NIWE/scripts/iw.py',
        selfcheck='--self-check', alt_selfcheck='autoprueba',
        doc_token='NIWE',
        funcion='Bancos PAL/USA vs C++ de cruceta 7DE, tarea/panel 7DD, reloj EE M11 y juez cierre-7dd',
    ),
    dict(
        id='niwf-ventanas', lote='NIWF', modo='ventanas', instalada=False,
        rel='tools/iw_niwf/ventanas_iw.py',
        alt_rel=('tools/ventanas_iw.py',),
        fabrica_rel='fabrica/NIWF/scripts/ventanas_iw.py',
        selfcheck='--autoprueba', alt_selfcheck='--self-check',
        doc_token='ventanas_iw.py',
        funcion='Cotejo focal QTE: flancos 7D4/7D5/7DB/7DC, palanca 7D4, remate 0x2F y reloj 7D5',
    ),
    dict(
        id='niwg-validar', lote='NIWG', modo='validar', instalada=False,
        rel='tools/iw_validar.py',
        alt_rel=('tools/iw_niwg/iw_validar.py',),
        fabrica_rel='fabrica/NIWG/scripts/iw_validar.py',
        selfcheck='--self-check', alt_selfcheck='autoprueba',
        doc_token='iw_validar.py',
        funcion='Auditoria de logs de juego IW (7 familias), diagnostico semilla 7DE y matriz NIWG',
    ),
    dict(
        id='ap-niwg-7de-log', lote='AP_NIWG', modo='log-7de', instalada=True,
        rel='tools/iw_7de_log.py',
        selfcheck='--self-check', alt_selfcheck='--autoprueba',
        doc_token='tools/iw_7de_log.py',
        funcion='Auditor de transicion Low->Aim (221 campos) y semilla en dbz3.log de Stop Cell 7DE',
    ),
    dict(
        id='base-campana', lote='BASE', modo='campana', instalada=True,
        rel='tools/iw_campana.py',
        selfcheck=None, alt_selfcheck=None,
        doc_token='iw_campana.py',
        funcion='Verificacion y soporte de requisitos de campana de Infinite World',
    ),
    dict(
        id='base-astra-aud', lote='BASE', modo=None, instalada=True,
        rel='tools/astra_iw_auditoria.py',
        selfcheck=None, alt_selfcheck=None, no_cli_help=True,
        doc_token='astra_iw_auditoria.py',
        funcion='Lector y auditoria de contenedores AFS/AMB de Infinite World (script directo sin argparse)',
    ),
    dict(
        id='base-astra-lad', lote='BASE', modo='ladrillos', instalada=True,
        rel='tools/astra_iw_ladrillos_cotejo.py',
        selfcheck=None, alt_selfcheck=None,
        doc_token='astra_iw_ladrillos_cotejo.py',
        funcion='Cotejo original vs port de capturas de ladrillos 0x7D3',
    ),
    dict(
        id='base-comparar', lote='BASE', modo='comparar', instalada=True,
        rel='tools/iw_mips/comparar.py',
        selfcheck='--autoprueba', alt_selfcheck=None,
        doc_token='comparar.py',
        funcion='Comparador generico JSONL y regresion 7D1 (dorado PCSX2 vs EE)',
    ),
    dict(
        id='base-comparar-trad', lote='BASE', modo='comparar-trad', instalada=True,
        rel='tools/iw_mips/comparar_trad.py',
        selfcheck=None, alt_selfcheck=None,
        doc_token='comparar_trad.py',
        funcion='Comparador GS/VU1 del traductor 2D/HUD de Infinite World',
    ),
    dict(
        id='base-cierre-r23', lote='BASE', modo='cierre-r23', instalada=True,
        rel='tools/iw_cierre_r23.py',
        selfcheck=None, alt_selfcheck=None,
        doc_token='iw_cierre_r23.py',
        funcion='Proyeccion de tablas de cierre PM-18 (series y escenarios IW EU/USA)',
    ),
)

DELEGACIONES_EXTRA = {
    'niwe': 'niwe-bancos',
    'ventanas': 'niwf-ventanas',
    'validar': 'niwg-validar',
    'log-7de': 'ap-niwg-7de-log',
    'campana': 'base-campana',
    'ladrillos': 'base-astra-lad',
    'comparar': 'base-comparar',
    'comparar-trad': 'base-comparar-trad',
    'cierre-r23': 'base-cierre-r23',
}

HERR_POR_ID = {h['id']: h for h in HERRAMIENTAS}

# Evidencia curada y recomendacion para cvars OFF de Infinite World.
# El estado real (ON/OFF), archivo:linea y conteo REXCVAR_GET SIEMPRE se leen en vivo de src/.
CVARS_GUIA = {
    # --- Grupo A: Integrados durante la noche (AP_NIWA..AP_NIWD) aun OFF ---
    'dbz3_iw_vuelta_tras_resultado': dict(
        grupo='noche', lote='AP_NIWA', curso='MAPA', recomendacion='Mantener OFF (candidata #1 a prueba ampliada)',
        evidencia_offline='NIWA: sonda C++ 5/5 (una sola transicion en victoria DM; respeta OFF, Road, minijuego y fuera de panel).',
        evidencia_juego='M11 victoria: AP_NIWA 1692 cuadros sin diferencias; NIWG-03 1700 cuadros sin diferencias observadas, replay incompleto (rc=2; ver AP_NIWG).',
        criterio_on='Probar 1 derrota de combate y 1 salida de minijuego/Road en Dragon Mission para confirmar que no retiene ni bloquea la vuelta.',
    ),
    'dbz3_iw_7dd_captura_derrota': dict(
        grupo='noche', lote='AP_NIWB', curso='0x7DD', recomendacion='Mantener OFF (candidata #2: corrige bug evidente en respaldo)',
        evidencia_offline='NIWB: sonda C++ 0/1 -> 1/1 (g_final=2, superado=false; Bubbles sigue victoria; rutas nativas excluidas). NIWE: reloj de panel 420 verificado.',
        evidencia_juego='AP_NIWB: derrota y panel observados (L2352-L2388), pero el guion no confirmo el cierre; `ap_niwb_7dd_confirmar.toml` pendiente.',
        criterio_on='Correr 1 vez `AP_NIWB/ap_niwb_7dd_confirmar.toml` (2 pulsaciones tras 420) y confirmar `resultado superado false` limpio en log.',
    ),
    'dbz3_iw_7d5_rango_pal': dict(
        grupo='noche', lote='AP_NIWB', curso='0x7D5', recomendacion='Mantener OFF',
        evidencia_offline='NIWB: 13/19 -> 19/19 contra PAL `0x293820`/`0x295C10` en tiempos de laboratorio (cubre huecos PAL como 25,005 s y 0 s).',
        evidencia_juego='Sin prueba en partida real ni comprobacion de equivalencia con el ELF USA.',
        criterio_on='Comprobar tabla equivalente en ELF USA (`SLUS_218.42`) y validar 1 partida completa de 0x7D5 en respaldo.',
    ),
    'dbz3_iw_disparo_texturas_directas': dict(
        grupo='noche', lote='AP_NIWC', curso='0x7D3/0x7D7/0x7D8/0x7DE', recomendacion='Mantener OFF (bajo riesgo; falta ver dibujo)',
        evidencia_offline='NIWC: 8/8 recuperaciones RGBA exactas (`t35` 32x128 y `t56` 32x32 en 935/0 y 890/6/0 USA+PAL), 158 indexadas identicas, 68 huecos intactos.',
        evidencia_juego='AP_NIWC PASS en 7DE (34 s): atlas 58 ranuras cargado sin error con t35/t56 normal y aditiva, pero las llamadas `Lanzar(...)` actuales no disparan hijos AME 8/9/16.',
        criterio_on='Confirmar que estado original dispara hijos AME 8, 9 o 16 y ver t35/t56 dibujados sin artefactos de alfa en juego.',
    ),
    # --- Grupo B: Cvars previos probados/investigados en la noche (HIW..NIWG) ---
    'dbz3_iw_7dd_original': dict(
        grupo='investigado_noche', lote='NIWG-01', curso='0x7DD', recomendacion='Mantener OFF (bloqueada en juego)',
        evidencia_offline='HIW/HIWB: modelo respaldo 0/120 frente a EE+WCF; comentario `src/iw_mips.cpp:33` ya documentaba 2x lentitud por decision del usuario (7/11).',
        evidencia_juego='NIWG-01 FAIL (102,9 s): en 3.721 ticks (90 s) con jugador quieto, Piccolo no bajo de 300,54 u (2.333 ticks en estado 15).',
        criterio_on='Depurar por que la patrulla EE de Piccolo queda lejos en mapa 906 y validar persecucion + captura real a ritmo original.',
    ),
    'dbz3_iw_7dd_captura_original': dict(
        grupo='investigado_noche', lote='NIWE/NIWG-01', curso='0x7DD', recomendacion='Mantener OFF (bloqueada por 7dd_original)',
        evidencia_offline='NIWE: `0x2A6260` 144/144 en PAL y USA + replay de 15 fotos PAL (`12,3573 -> 2,7429 u`, CBC1 -> CBC9).',
        evidencia_juego='NIWG-01 FAIL: distancia minima de Piccolo 300,54 u; no hubo `objetivo7dd creado` (`cierre-7dd` rc=2).',
        criterio_on='Depende de resolver `dbz3_iw_7dd_original` y confirmar `objetivo7dd` + pose 9 + cierre de panel en una sola partida.',
    ),
    'dbz3_iw_disparo_mira_original': dict(
        grupo='investigado_noche', lote='NIWE/NIWG-02', curso='0x7DE', recomendacion='Mantener OFF',
        evidencia_offline='HIW/HIWB: digital 1/6, analogica 0/6 en respaldo. NIWE-02 (ya integrado en puente): cruceta 2704 -> 3136/3136 PAL y USA.',
        evidencia_juego='NIWG-02 PASS parcial (35,3 s): 180 muestras digitales sin fallos (`z=200->60,5`, Aim->Fire), pero sin probar stick analogico ni partida completa con Low/impactos.',
        criterio_on='Validar en juego stick analogico (fuera de zona muerta 32), entrada/salida LowEnergyWait y partida completa 7DE con impactos.',
    ),
    # --- Grupo C: Cvars OFF previos en src/ ---
    'dbz3_iw_7d5_original': dict(
        grupo='previo', lote='previo', curso='0x7D5', recomendacion='Mantener OFF',
        evidencia_offline='NIWF: adaptador de botones 47/47 tras parche NIWG; reloj PAL 50 Hz (351 tics) vs respaldo 60 Hz (420 tics) aun sin cerrar.',
        evidencia_juego='Sin validacion de partida completa 0x7D5 con la CPU original del ELF.',
        criterio_on='Completar cotejo de rondas/tecleo/camara y pasar 1 partida completa 7D5 en juego (EU y USA).',
    ),
    'dbz3_iw_7d5_fin_camara_original': dict(
        grupo='previo', lote='previo', curso='0x7D5', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_mini_botones.cpp:4420`: espera fin de camara 0xBE antes de pose 0x353 en respaldo; sin banco offline dedicado.',
        evidencia_juego='Sin evidencia visual archivada de esa transicion en 7D5.',
        criterio_on='Capturar en juego el cierre de 7D5 con la opcion activa y verificar camara 0xBE -> pose 0x353 sin corte ni bloqueo.',
    ),
    'dbz3_iw_7d1_original': dict(
        grupo='previo', lote='previo', curso='0x7D1', recomendacion='Mantener OFF',
        evidencia_offline='HIWB `iw_cotejo 7D1`: regresion IA Bubbles 6841/6841 (dorado PCSX2 vs EE), no valida rama C++ completa ni agarre/fallo en host.',
        evidencia_juego='Marcada `(en pruebas)` en `src/iw_mips_campo.inl:88`; sin validacion completa en juego.',
        criterio_on='Probar en juego reloj, cartel START, avisos, agarre de Bubbles y derrota por tiempo en EU y USA.',
    ),
    'dbz3_iw_campo_aislado': dict(
        grupo='previo', lote='previo', curso='0x7D0/7D1/7D2/7D9/7DD/7DF', recomendacion='Mantener OFF (herramienta de prueba)',
        evidencia_offline='`src/iw_mips_campo.inl:86`: imagen EE propia para `s_SPACE` sin pisar IA de Bubbles/Piccolo; declarada expresamente como prueba.',
        evidencia_juego='No certifica ningun minijuego completo.',
        criterio_on='Solo activar si se prueba coexistencia con `7d1_original`/`7dd_original` en partida completa.',
    ),
    'dbz3_iw_disparo_original': dict(
        grupo='previo', lote='previo', curso='0x7D7/0x7D8/0x7DE', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_mips_disparo.inl:72`: solo rango desde ELF EU/USA ("motor completo pendiente"); 7D7 ya iguala rango en respaldo (3960/3960 con NIWD).',
        evidencia_juego='Sin motor completo ni validacion en juego para los 3 cursos de disparo.',
        criterio_on='Validar rango + panel en 7D7, 7D8 y 7DE (ganar y perder) en EU y USA.',
    ),
    'dbz3_iw_disparo_carga_original': dict(
        grupo='previo', lote='previo', curso='0x7DE', recomendacion='Mantener OFF',
        evidencia_offline='HIW/HIWB: banco de carga 15/15 en contrato ELF; `src/iw_mips_disparo.inl:74` advierte "solo vivo/sin caducidad; HUD/FX del port".',
        evidencia_juego='Sin prueba en partida completa con barras/FX y caducidad.',
        criterio_on='Cubrir caducidad/interrupcion de carga y validar HUD/FX en partida real 7DE.',
    ),
    'dbz3_iw_disparo_intro_original': dict(
        grupo='previo', lote='previo', curso='0x7DE', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_mips_disparo.inl:76`: PreIntro/Intro/Ready desde ELF con camaras y actores del port; sin banco de camaras sincronizado.',
        evidencia_juego='Sin prueba visual lado a lado en juego.',
        criterio_on='Validar visualmente PreIntro/Intro/Ready en 7DE sin desajuste de camara ni actores.',
    ),
    'dbz3_iw_disparo_voces_poses_original': dict(
        grupo='previo', lote='previo', curso='0x7D8/0x7DE', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_mini_disparo.cpp:95`: voces 1248/1249/1253/1255/1260/1261 y frases de Ten; sin banco de audio automatizado.',
        evidencia_juego='Sin validacion auditiva del usuario en 7D8/7DE.',
        criterio_on='Escucha humana en 7D8 y 7DE (entrada, frases de Ten y victoria/derrota).',
    ),
    'dbz3_iw_7d8_brillo_ready_original': dict(
        grupo='previo', lote='previo', curso='0x7D8', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_mini_disparo.cpp:224`: brillo 935/7 de manos al entrar en READY (pruebas); sin cotejo visual GS.',
        evidencia_juego='Sin captura comparativa en 7D8.',
        criterio_on='Revisar captura de READY en 7D8 comparando brillo de manos con PS2.',
    ),
    'dbz3_iw_serpiente_original': dict(
        grupo='previo', lote='previo', curso='0x7D2', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_minijuegos.cpp:92`: planificador y cierre de 0x7D2 (`0x299900 -> 0x299000`); solo pruebas focales en `iwx_serpiente_focal.py`.',
        evidencia_juego='Sin validacion completa de recorrido y meta en juego.',
        criterio_on='Pasar recorrido completo de Camino de la Serpiente (meta + caida) en juego.',
    ),
    'dbz3_iw_hito_original': dict(
        grupo='previo', lote='previo', curso='0x7D0/0x7D9', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_minijuegos.cpp:100`: cierre en hito `#ITM+0x30` tras todos los aros (`s_SPACE`); requiere cierre de campo.',
        evidencia_juego='Sin validacion completa en 7D0 y 7D9 en partida.',
        criterio_on='Validar en juego victoria por hito tras recoger todos los aros en 7D0 y 7D9.',
    ),
    'dbz3_iw_seguimiento_original': dict(
        grupo='previo', lote='previo', curso='0x7D2', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_minijuegos.cpp:111`: camara de seguimiento `0x299A70` (k=0,3, sin salto >150 u); prueba focal `iwx_seguimiento_focal.py`.',
        evidencia_juego='Sin validacion visual en recorrido real de 7D2.',
        criterio_on='Revisar fluidez y encuadre de camara durante un recorrido completo de 7D2.',
    ),
    'dbz3_iw_caida_original': dict(
        grupo='previo', lote='previo', curso='0x7D2', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_minijuegos.cpp:114`: caida fuera de carretera `0x2AA0A0` hasta `y < -1000` y reaparicion.',
        evidencia_juego='Sin prueba en juego de caida y reaparicion sin atasco.',
        criterio_on='Forzar 1 caida fuera del camino en 7D2 y comprobar reaparicion y reloj.',
    ),
    'dbz3_iw_botones_ui_anim': dict(
        grupo='previo', lote='previo', curso='0x7DB', recomendacion='Mantener OFF (candidata previa de alta evidencia offline)',
        evidencia_offline='`src/iw_mips_botones.inl:91-95`: `UI_UpdateFrame` (`0x2F1190`/`0x2F2F40`) baja difs en dificultad 0 de 132/4499 a 2/4499 (`cmp7db.py --ui`).',
        evidencia_juego='Sin validacion en partida real en dificultades 0, 2 y 5.',
        criterio_on='Correr 7DB en dificultad 0 y 2 comprobando desaparicion limpia de iconos acertados.',
    ),
    'dbz3_iw_botones_ui_anim_d4': dict(
        grupo='previo', lote='previo', curso='0x7D4', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_mips_botones.inl:89`: extiende `botones_ui_anim` a 0x7D4; sin banco equivalente dedicado a 7D4 en dificultad 0.',
        evidencia_juego='Sin validacion en partida real con la opcion activa.',
        criterio_on='Validar despues de `dbz3_iw_botones_ui_anim` probando 7D4 (carriles + remate) en dificultad 0 y 2.',
    ),
    'dbz3_iw_tinte_original': dict(
        grupo='previo', lote='previo', curso='0x7D4/7D5/7DA/7DB/7DC/7D8', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_mini_botones.cpp:489`: tinte de golpe (QTE blanco 15 cuadros; Piccolo DC903C 20); focal `iwx_chr_fade_focal.py`.',
        evidencia_juego='Sin revision visual en juego del flash sobre personajes HD.',
        criterio_on='Ojo humano en 1 golpe QTE y 1 rayo de Piccolo confirmando color y duracion.',
    ),
    'dbz3_iw_mezcla_entrada_vuelo_original': dict(
        grupo='previo', lote='previo', curso='0x7D6/0x7E0', recomendacion='Mantener OFF',
        evidencia_offline='`src/anim_iw.cpp:53`: primera pose de vuelo (`0x320..0x330`) entra sin mezcla (`EU 0x341770`).',
        evidencia_juego='Sin captura comparativa en 7D6/7E0.',
        criterio_on='Revisar inicio visual de 7D6 y 7E0 confirmando que no produce salto brusco.',
    ),
    'dbz3_iw_vuelo_objeto8_viaje': dict(
        grupo='previo', lote='previo', curso='0x7D6/0x7E0', recomendacion='Mantener OFF',
        evidencia_offline='`src/iw_mini_vuelo.cpp:109-112`: congela medidor al llegar efecto (33 PAL / 39 NTSC); comentario advierte "+-1 campo sin medir en PINE".',
        evidencia_juego='Sin medicion PINE del orden de tareas ni prueba en juego.',
        criterio_on='Medir G+0x520 en PINE o confirmar visualmente en juego que el efecto y la congelacion coinciden.',
    ),
    'dbz3_iw_rango_fight40': dict(
        grupo='previo', lote='previo', curso='MAPA/Combate', recomendacion='Mantener OFF',
        evidencia_offline='`src/pasarela_misiones.cpp:153-155`: primer corte Fight de `Char_ComputeStatGrade` (`0x2AC0D0`) en 40% en vez de 39%; no cubre contadores RAMEvents por si solo.',
        evidencia_juego='Sin prueba de campaña comparando rango final de combate.',
        criterio_on='Evaluar junto con `dbz3_iw_rango_contadores` en combates de Dragon Mission.',
    ),
    'dbz3_iw_rango_contadores': dict(
        grupo='previo', lote='previo', curso='MAPA/Combate', recomendacion='Mantener OFF (candidata previa de alta evidencia offline)',
        evidencia_offline='`src/pasarela_misiones.cpp:157-160`: formula completa de `Char_ComputeStatGrade` (`0x2AC0D0`), 3488/3488 filas iguales al ELF (`h08_rango_candidato40.json`).',
        evidencia_juego='Sin validacion en partida real comprobando que el combate alimenta las 5 palabras sin desvio.',
        criterio_on='Validar en 2 combates reales de Dragon Mission que esquivas, teletransportes y ki producen el rango esperado.',
    ),
}

# Cvars promovidas a ON durante esta noche (referencia informativa para el informe/resumen).
PROMOVIDAS_NOCHE_ON = (
    ('dbz3_iw_7de_pausa_reloj_original', 'AP_HIWB', 'PASS en opus2 (68->68->64)'),
    ('dbz3_iw_reloj_vuelta_original', 'AP_NIWA', 'PASS en smoke_b M11 (1692/1692)'),
    ('dbz3_iw_mapa_intro_limite_original', 'AP_NIWD', 'Sonda PAL/C++ 100% + integracion'),
    ('dbz3_iw_7d7_siete_aciertos_original', 'AP_NIWD', 'Sonda PAL/USA 3960/3960'),
    ('dbz3_iw_7de_campo_low_original', 'NIWG/AP_NIWG', 'PASS en sol (221 campos, 3,683 s observados)'),
    ('dbz3_iw_reloj_vuelta_suma_ee', 'NIWG/AP_NIWG', 'M11: 1700 cuadros observados sin diferencias; replay incompleto'),
    ('dbz3_iw_qte_flancos_completos', 'NIWG/AP_NIWG', 'PASS en smoke_a 7DB (0xC000)'),
    ('dbz3_iw_qte_palanca_ps2', 'NIWG/AP_NIWG', 'PASS en smoke_b 7D4 (194/201, rango Z)'),
    ('dbz3_iw_7d4_ventana_remate_pal', 'NIWG/AP_NIWG', 'PASS en opus2 7D4 (acierto x~407-410)'),
)

R_COMENTARIO = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*[\s\S]*?\*/')
R_CVAR_DEF = re.compile(r'\bREXCVAR_DEFINE_BOOL\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([^,]+),')


def conectar(repo: Path) -> None:
    if not (repo / 'src/iw_mips.cpp').is_file() or not (repo / 'tools/iw_censo.py').is_file():
        raise ValueError('indique la raiz del proyecto con --repo (antes del subcomando)')
    for p in ('tools', 'tools/iw_mips', 'awo_tools'):
        s = str(repo / p)
        if s not in sys.path:
            sys.path.insert(0, s)


def proceso(cmd, repo: Path, **kw):
    timeout = kw.pop('timeout', 300)
    env_extra = kw.pop('env_extra', None)
    env = {**os.environ, 'PYTHONUTF8': '1', 'PYTHONDONTWRITEBYTECODE': '1'}
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        cmd, cwd=repo, encoding='utf-8', errors='replace',
        env=env, timeout=timeout, **kw,
    )


def resolver_ruta(repo: Path, trabajo: Path, h: dict) -> tuple[Path | None, str]:
    """Devuelve (ruta, origen) priorizando tools/ del repo y cayendo a fabrica/ si aun no se instalo."""
    candidatas = []
    if h.get('rel'):
        candidatas.append((repo / h['rel'], 'tools'))
    for alt in h.get('alt_rel', ()):
        candidatas.append((repo / alt, 'tools'))
    if h.get('fabrica_rel'):
        candidatas.append((trabajo / h['fabrica_rel'], 'fabrica'))
    for p, origen in candidatas:
        if p.is_file():
            return p, origen
    return None, 'ausente'


def delegar(repo: Path, modo: str, args: list[str], **kw):
    extras = []
    if modo == 'mapa':
        extras = ['--repo', str(repo)]
    elif modo == 'focos':
        extras = ['--raiz', str(repo)]
    return proceso([sys.executable, '-B', '-X', 'utf8', str(repo / 'tools' / BACKENDS[modo]),
                    *extras, *args], repo, **kw)


def delegar_herramienta(repo: Path, trabajo: Path, hid: str, args: list[str]):
    h = HERR_POR_ID[hid]
    ruta, origen = resolver_ruta(repo, trabajo, h)
    if ruta is None:
        raise FileNotFoundError(f'herramienta {hid} no encontrada en tools/ ni en {trabajo}')
    extras = []
    out_args = list(args)
    if hid in ('niwe-bancos', 'niwg-validar'):
        # NIWE/NIWG solo reconocen --self-check como argumento unico; con --repo requieren el subcomando autoprueba.
        if out_args == ['--self-check'] or out_args == ['--autoprueba']:
            out_args = ['autoprueba']
        if '--repo' not in out_args and '--help' not in out_args and '-h' not in out_args:
            extras = ['--repo', str(repo)]
    elif hid == 'niwf-ventanas' and '--raiz' not in out_args and '--help' not in out_args and '-h' not in out_args:
        extras = ['--raiz', str(repo)]
    return proceso([sys.executable, '-B', '-X', 'utf8', str(ruta), *extras, *out_args], repo)


def emitir(obj, destino: Path | None) -> None:
    text = json.dumps(obj, ensure_ascii=True, default=str, allow_nan=False, indent=2)
    if destino:
        with destino.open('x', encoding='utf-8', newline='\n') as f:
            print(text, file=f)
    else:
        print(text)


def escribir_texto_nuevo(texto: str, destino: Path | None) -> None:
    if destino:
        with destino.open('x', encoding='utf-8', newline='\n') as f:
            f.write(texto)
    else:
        print(texto, end='')


def catalogo(repo: Path, a, *, solo_activos: bool = False):
    from iw_censo import AFS, CURSOS, generar
    from iw_cotejo import normalizar
    from iw_niwc import iw_activos as activos
    curso = normalizar(a.curso) if a.curso else None
    copies = {}
    for lengua in (a.idioma or ['SPN']):
        copies.update(activos.variants(repo, a.pal or activos.EU, lengua))
    if a.afs:
        copies['USA']['C'] = a.afs
    if getattr(a, 'original', False):
        if curso:
            raise ValueError('--original no admite filtro por curso')
        pal = a.pal or activos.EU
        bancos = {'USA': copies['USA'], 'PAL': next(v for k, v in copies.items() if k.startswith('PAL-'))}
        return dict(ee=activos.ee_probe(pal / 'SLES_553.47'), efectos=activos.effect_evidence(bancos)), 0

    afs = a.afs or repo / AFS
    incluir = solo_activos or a.con_activos
    recursos = activos.census(repo, copies, a.catalogo or activos.HERE / 'catalogo.txt', curso) if incluir else None
    keys = set(CURSOS) - {'MAPA', 'ROAD'}
    corpus = recursos['dmd'].values() if incluir else ()
    falta = any(d['filas'] is None or keys - d['filas'].keys() for d in corpus)
    if solo_activos:
        return recursos, 2 if falta or not recursos['filas'] or 'revisar' in recursos['resumen'] else 0

    d = generar(repo, afs)
    filas = d['dmd_1093']['filas']
    falta |= filas is None or bool(keys - filas.keys())
    indices = defaultdict(list)
    if incluir:
        for fila in recursos['filas']:
            indices[fila['curso']].append(fila['id'])
        falta |= 'revisar' in recursos['resumen']
    registro = indice_focos(repo)
    for row in d['cursos']:
        k = row['clave']
        row['bancos'] = registro[k]
        if incluir:
            row['ids_activos'] = indices[k]
    if incluir and not recursos['filas']:
        falta = True
    d['cursos'] = [r for r in d['cursos'] if not curso or r['clave'] == curso]
    d.update(formato='iw-niwd-censo-v1', filtro=curso, activos=recursos,
             alcance='Inventario estatico. Bancos disponibles no equivalen a paridad ni a rutas observadas en juego.')
    return d, 2 if falta else 0


def indice_focos(repo: Path):
    from iw_censo import CURSOS
    from iw_niwd._limites import BANCOS
    r = delegar(repo, 'focos', ['--listar'], capture_output=True)
    if r.returncode:
        raise RuntimeError(r.stderr or f'indice de NIWB: {r.returncode}')
    tabla = {k: [{'modo': 'legado', 'tipo': v[3]}] for k, v in CURSOS.items()}
    for linea in r.stdout.splitlines():
        foco, _, cursos = linea.partition(': ')
        if not cursos:
            raise RuntimeError(f'indice NIWB inesperado: {linea}')
        for curso in cursos.split(', '):
            tabla[curso].append({'modo': 'focos', 'foco': foco})
    tabla['MAPA'].append({'modo': 'mapa', 'foco': 'M11 y replay de mapa'})
    for nombre, (curso, _, cvar) in BANCOS.items():
        tabla[curso].append({'modo': 'limites', 'foco': nombre, 'ajuste': cvar})
    return tabla


def _linea_en_tools_md(repo: Path, token: str | None) -> int | None:
    if not token:
        return None
    md = repo / 'docs/04_herramientas/TOOLS.md'
    if not md.is_file():
        return None
    for idx, line in enumerate(md.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
        if token in line:
            return idx
    return None


def _probar_flag(repo: Path, script: Path, flag: str, *, trabajo: Path | None = None, timeout: int = 30) -> int:
    env_extra = {'DBZ3_IW_EN_AUTOPRUEBA': '1'} if flag != '--help' else None
    args = ['--repo', str(repo), '--trabajo', str(trabajo)] if trabajo and script == repo / 'tools/iw.py' else []
    r = proceso([sys.executable, '-B', '-X', 'utf8', str(script), *args, flag], repo,
                capture_output=True, timeout=timeout, env_extra=env_extra)
    return r.returncode


def auditar_inventario(repo: Path, trabajo: Path, comprobar_help: bool = False, comprobar_autoprueba: bool = False) -> dict:
    filas = []
    for h in HERRAMIENTAS:
        ruta, origen = resolver_ruta(repo, trabajo, h)
        doc_linea = _linea_en_tools_md(repo, h.get('doc_token'))
        fila = dict(
            id=h['id'],
            lote=h['lote'],
            modo=h['modo'],
            origen=origen,
            ruta=str(ruta) if ruta else None,
            instalada_esperada=bool(h.get('instalada')),
            sustituida_por=h.get('sustituida'),
            doc_tools_linea=doc_linea,
            funcion=h['funcion'],
            help_rc=None,
            selfcheck_rc=None,
            alt_selfcheck_rc=None,
            selfcheck_ok=None,
        )
        if ruta and comprobar_help:
            fila['help_rc'] = None if h.get('no_cli_help') else _probar_flag(repo, ruta, '--help')
        if ruta and comprobar_autoprueba:
            sc = h.get('selfcheck')
            alt = h.get('alt_selfcheck')
            if sc:
                fila['selfcheck_rc'] = _probar_flag(repo, ruta, sc, trabajo=trabajo)
            if alt:
                fila['alt_selfcheck_rc'] = _probar_flag(repo, ruta, alt, trabajo=trabajo)
            if sc or alt:
                fila['selfcheck_ok'] = (fila['selfcheck_rc'] == 0) or (fila['alt_selfcheck_rc'] == 0)
        filas.append(fila)
    resumen = dict(
        total=len(filas),
        en_tools=sum(1 for f in filas if f['origen'] == 'tools'),
        en_fabrica=sum(1 for f in filas if f['origen'] == 'fabrica'),
        ausentes=sum(1 for f in filas if f['origen'] == 'ausente'),
        documentadas_tools_md=sum(1 for f in filas if f['doc_tools_linea'] is not None),
        help_ok=sum(1 for f in filas if f['help_rc'] == 0) if comprobar_help else None,
        autoprueba_ok=sum(1 for f in filas if f['selfcheck_ok'] is True) if comprobar_autoprueba else None,
    )
    return dict(formato='iw-niwh-inventario-v1', repo=str(repo), trabajo=str(trabajo), resumen=resumen, herramientas=filas)


def inventario_md(doc: dict) -> str:
    lines = [
        '# Inventario de herramientas Infinite World',
        '',
        f"Repositorio: `{doc['repo']}`",
        '',
        '| ID | Lote | Origen | TOOLS.md | `--help` | `--self-check` / `--autoprueba` | Funcion |',
        '|---|---|---|---|---:|---|---|',
    ]
    for h in doc['herramientas']:
        doc_s = f"L{h['doc_tools_linea']}" if h['doc_tools_linea'] else ('sustituida' if h['sustituida_por'] else 'NO')
        h_rc = '-' if h['help_rc'] is None else str(h['help_rc'])
        sc_parts = []
        if h['selfcheck_rc'] is not None:
            sc_parts.append(f"pref={h['selfcheck_rc']}")
        if h['alt_selfcheck_rc'] is not None:
            sc_parts.append(f"alt={h['alt_selfcheck_rc']}")
        sc_s = ', '.join(sc_parts) if sc_parts else 'sin flag'
        lines.append(f"| `{h['id']}` | {h['lote']} | {h['origen']} | {doc_s} | {h_rc} | {sc_s} | {h['funcion']} |")
    return '\n'.join(lines) + '\n'


def escanear_cvars(repo: Path, ambito: str = 'iw', solo_off: bool = False) -> dict:
    src = repo / 'src'
    files = sorted(
        p for p in src.rglob('*')
        if p.is_file() and p.suffix in {'.cpp', '.cc', '.cxx', '.hpp', '.h', '.inl'}
    )
    raw_texts = {p.relative_to(repo).as_posix(): p.read_text(encoding='utf-8', errors='replace') for p in files}
    clean = {k: R_COMENTARIO.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]), v) for k, v in raw_texts.items()}

    # Conteo en una sola pasada O(src) en lugar de O(N_cvars * src)
    r_get = re.compile(r'\bREXCVAR_GET\s*\(\s*([A-Za-z0-9_]+)\s*\)')
    r_set = re.compile(r'\bREXCVAR_SET\s*\(\s*([A-Za-z0-9_]+)\s*,')
    get_counts: dict[str, int] = defaultdict(int)
    set_counts: dict[str, int] = defaultdict(int)
    for t in clean.values():
        for gm in r_get.finditer(t):
            get_counts[gm.group(1)] += 1
        for sm in r_set.finditer(t):
            set_counts[sm.group(1)] += 1

    defs = []
    for rel, txt in clean.items():
        for m in R_CVAR_DEF.finditer(txt):
            name, val = m.group(1), ' '.join(m.group(2).split())
            is_dbz3_iw = name.startswith('dbz3_iw_')
            is_other_iw = (not is_dbz3_iw) and ('iw' in name.lower()) and (not name.startswith('dbz3_b2_'))
            if ambito == 'iw' and not is_dbz3_iw:
                continue
            if ambito == 'todos' and not (is_dbz3_iw or is_other_iw):
                continue
            line = 1 + txt.count('\n', 0, m.start())
            estado = {'false': 'OFF', 'true': 'ON'}.get(val, 'DESCONOCIDO')
            get_n = get_counts[name]
            set_n = set_counts[name]
            guia = CVARS_GUIA.get(name)
            entry = dict(
                nombre=name,
                tipo='BOOL',
                valor_defecto=val,
                estado=estado,
                archivo=rel,
                linea=line,
                lecturas_get=get_n,
                escrituras_set=set_n,
                sin_get_textual=(get_n == 0),
            )
            if guia and estado == 'OFF':
                entry.update(guia)
            elif estado == 'OFF':
                entry.update(
                    grupo='sin_clasificar',
                    lote='desconocido',
                    curso='?',
                    recomendacion='Revisar',
                    evidencia_offline='No catalogada en CVARS_GUIA.',
                    evidencia_juego='Sin catalogar.',
                    criterio_on='Documentar evidencia antes de encender.',
                )
            defs.append(entry)

    defs.sort(key=lambda d: (d['estado'] != 'OFF', d.get('grupo', 'z'), d['archivo'], d['linea'], d['nombre']))
    resumen = dict(
        ambito=ambito,
        total=len(defs),
        on=sum(1 for d in defs if d['estado'] == 'ON'),
        off=sum(1 for d in defs if d['estado'] == 'OFF'),
        sin_get_textual=sum(1 for d in defs if d['sin_get_textual']),
        defecto_desconocido=sum(1 for d in defs if d['estado'] == 'DESCONOCIDO'),
        off_noche=sum(1 for d in defs if d['estado'] == 'OFF' and d.get('grupo') == 'noche'),
        off_investigados_noche=sum(1 for d in defs if d['estado'] == 'OFF' and d.get('grupo') == 'investigado_noche'),
        off_previos=sum(1 for d in defs if d['estado'] == 'OFF' and d.get('grupo') == 'previo'),
        off_sin_clasificar=sum(1 for d in defs if d['estado'] == 'OFF' and d.get('grupo') == 'sin_clasificar'),
        promovidas_noche_on=[dict(nombre=n, lote=l, nota=m) for n, l, m in PROMOVIDAS_NOCHE_ON],
    )
    filtrados = [d for d in defs if (not solo_off) or d['estado'] == 'OFF']
    return dict(formato='iw-niwh-cvars-v1', repo=str(repo), solo_off=solo_off, resumen=resumen, cvars=filtrados)


def cvars_md(doc: dict) -> str:
    r = doc['resumen']
    lines = [
        '# Auditoria de cvars Infinite World',
        '',
        f"Repositorio: `{doc['repo']}` · Ambito: `{r['ambito']}` · Total: **{r['total']}** "
        f"(**{r['on']} ON**, **{r['off']} OFF**, **{r['defecto_desconocido']} sin valor literal**, "
        f"**{r['sin_get_textual']} sin menciones de `REXCVAR_GET`**).",
        'Escaneo textual de booleanos en src/: no evalua el preprocesador ni mide lecturas en juego.',
        '',
        '| Cvar | Fuente | Def | `GET` | Grupo / Lote | Curso | Recomendacion | Evidencia resumida |',
        '|---|---|---|---:|---|---|---|---|',
    ]
    for c in doc['cvars']:
        ev = c.get('evidencia_juego') or c.get('evidencia_offline') or '-'
        rec = c.get('recomendacion', c['estado'])
        gl = f"{c.get('grupo', '-')} / {c.get('lote', '-')}"
        lines.append(
            f"| `{c['nombre']}` | `{c['archivo']}:{c['linea']}` | `{c['valor_defecto']}` | {c['lecturas_get']} | "
            f"{gl} | {c.get('curso', '-')} | {rec} | {ev} |"
        )
    return '\n'.join(lines) + '\n'


def autoprueba(repo: Path, trabajo: Path, exigir_todas: bool = False) -> dict:
    from iw_censo import autoprueba as censo_ap
    from iw_niwc.iw_activos import selfcheck as niwc_ap
    censo_ap()
    niwc_ap()

    # Enlace curso/banco de NIWD
    tabla = indice_focos(repo)
    if len(tabla) != 20 or not all(any(x['modo'] == 'limites' for x in tabla[k]) for k in ('MAPA', '0x7DE')):
        raise RuntimeError('enlace curso/banco incompleto')

    # Guardia antirecursion: NIWE/NIWG invocan `tools/iw.py autoprueba` dentro de su propia autoprueba.
    if os.environ.get('DBZ3_IW_EN_AUTOPRUEBA') == '1':
        r_foc = delegar(repo, 'focos', ['--autoprueba'], capture_output=True, timeout=30)
        if r_foc.returncode:
            raise RuntimeError(f'autoprueba NIWB: {r_foc.returncode}')
        print('IW autoprueba base OK (reentrada desde herramienta delegada)')
        return dict(ok=1, pendientes=0, herramientas=[], cvars={})

    # Controles propios de NIWH (inventario + escaner de cvars en vivo, sin discos ni juego)
    inv = auditar_inventario(repo, trabajo, comprobar_help=False, comprobar_autoprueba=False)
    if inv['resumen']['total'] < 15:
        raise RuntimeError('registro HERRAMIENTAS incompleto')
    cv = escanear_cvars(repo, ambito='iw', solo_off=False)

    resultados = []
    # Autopruebas offline de herramientas con soporte sin discos/ELF/compilador
    for h in HERRAMIENTAS:
        if h['id'] == 'niwd-iw':
            continue
        sc = h.get('selfcheck')
        alt = h.get('alt_selfcheck')
        if not sc and not alt:
            continue
        ruta, origen = resolver_ruta(repo, trabajo, h)
        if ruta is None:
            if h['instalada']:
                raise RuntimeError(f"no se encuentra la herramienta instalada {h['id']}")
            resultados.append(dict(id=h['id'], estado='ausente', rc=None))
            continue
        # Para niwa-mapa, `autoprueba` (subcomando) usa ELF PAL; primero probamos --self-check offline
        elegido = None
        rc = None
        for flag in (sc, alt):
            if not flag:
                continue
            r = proceso([sys.executable, '-B', '-X', 'utf8', str(ruta), flag], repo,
                        capture_output=True, timeout=60, env_extra={'DBZ3_IW_EN_AUTOPRUEBA': '1'})
            elegido, rc = flag, r.returncode
            if rc != 2:
                break
        if rc != 0:
            raise RuntimeError(f"autoprueba fallo en {h['id']} ({ruta} {elegido}): rc={rc}")
        resultados.append(dict(id=h['id'], origen=origen, flag=elegido, rc=0, estado='ok'))

    ok_n = sum(1 for r in resultados if r['estado'] == 'ok')
    pend_n = sum(1 for r in resultados if r['estado'] != 'ok')
    if exigir_todas and pend_n:
        raise RuntimeError(f'faltan {pend_n} herramientas por pasar autoprueba: {resultados}')
    print(f'IW autoprueba OK ({ok_n} herramientas OK, {pend_n} pendientes; 20 cursos; {cv["resumen"]["total"]} cvars, {cv["resumen"]["off"]} OFF)')
    return dict(ok=ok_n, pendientes=pend_n, herramientas=resultados, cvars=cv['resumen'])


def cli() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description=__doc__, allow_abbrev=False,
        epilog='Atajos: --self-check / --autoprueba equivalen al subcomando autoprueba. '
               'Codigos: 0 OK/coincide; 1 diferencia; 2 datos/uso/pendiente; 3 error.',
    )
    p.add_argument('--repo', type=Path, default=Path.cwd(), help='raiz de src y tools (por defecto, directorio actual)')
    p.add_argument('--trabajo', type=Path, default=TRABAJO_DEF, help='carpeta DBZ3HD_trabajo (para localizar entregas en fabrica/)')
    sp = p.add_subparsers(dest='accion', required=True)

    for nombre, texto in [('censo', 'cursos, cvars y bancos disponibles'), ('activos', 'recursos nativos y lectores del port')]:
        s = sp.add_parser(nombre, help=texto, allow_abbrev=False)
        s.add_argument('curso', nargs='?', help='7D0..7E1, MAPA o ROAD')
        s.add_argument('--afs', type=Path, help='alternativa USA DATA_CMN.AFS')
        s.add_argument('--pal', type=Path, help='copia PAL extraida (para activos)')
        s.add_argument('--idioma', action='append', choices=('ENG', 'SPN', 'FRA', 'GER', 'ITA'), help='repetible; PAL SPN por defecto')
        s.add_argument('--catalogo', type=Path, help='catalogo compatible NIWC')
        s.add_argument('--json', type=Path, help='JSON nuevo; por defecto, salida estandar')
        if nombre == 'censo':
            s.add_argument('--con-activos', action='store_true', help='unir cada curso a sus filas de recursos USA/PAL')
        else:
            s.add_argument('--original', action='store_true', help='ejecutar los selectores PAL y leer las referencias AME')

    ap = sp.add_parser('autoprueba', help='controles rapidos sin discos, ELF, compilador ni juego')
    ap.add_argument('--exigir-todas', action='store_true', help='falla si falta alguna herramienta con autoprueba offline registrada')

    inv = sp.add_parser('inventario', help='audita herramientas IW (presencia, TOOLS.md, --help, autopruebas)')
    inv.add_argument('--comprobar-help', action='store_true', help='ejecuta --help en cada herramienta localizada')
    inv.add_argument('--comprobar-autoprueba', action='store_true', help='ejecuta --self-check/--autoprueba en cada herramienta')
    inv.add_argument('--json', type=Path, help='guardar JSON nuevo')
    inv.add_argument('--md', type=Path, help='guardar tabla Markdown nueva')

    cv = sp.add_parser('cvars', help='audita booleanos IW de src/ y menciones textuales de REXCVAR_GET')
    cv.add_argument('--solo-off', action='store_true', help='muestra solo las cvars apagadas de serie')
    cv.add_argument('--ambito', choices=('iw', 'todos'), default='iw', help='iw = dbz3_iw_*; todos = incluye dbz3_*iw* de escenarios/anim')
    cv.add_argument('--json', type=Path, help='guardar JSON nuevo')
    cv.add_argument('--md', type=Path, help='guardar tabla Markdown nueva')

    c = sp.add_parser('cotejo', help='elegir banco de comparacion')
    cp = c.add_subparsers(dest='cotejo', required=True)
    l = cp.add_parser('limites', help='PAL/USA vs C++ actual: entrada al mapa, 7DE y 7D7')
    l.add_argument('foco', choices=('todos', 'intro', 'low', 'rango-7d7'), nargs='?', default='todos')
    l.add_argument('--ajuste', choices=('serie', 'on', 'off', 'todos'), default='serie',
                   help='valor de los ajustes NIWD en la sonda; serie usa el valor declarado en C++')
    l.add_argument('--cxx', default='g++')
    l.add_argument('--elf-pal', type=Path)
    l.add_argument('--elf-usa', type=Path)
    l.add_argument('--json', type=Path)
    for nombre, texto in [
        ('mapa', 'banco NIWA: M11, hitos y replay'),
        ('focos', 'bancos NIWB: C++ integrado'),
        ('legado', 'HIWB/Astra: trazas, GS, ladrillos y bancos originales'),
        ('niwe', 'bancos NIWE: 7DD contacto/panel, 7DE cruceta, M11 reloj EE y cierre-7dd'),
        ('ventanas', 'bancos NIWF: botones, palanca 7D4, remate 0x2F y reloj 7D5'),
    ]:
        cp.add_parser(nombre, help=texto + '; use --help para sus opciones', add_help=False)

    for nombre, texto in [
        ('niwe', 'delega en NIWE (banco-7dd/7de/m11, cierre-7dd)'),
        ('ventanas', 'delega en NIWF ventanas_iw.py (--banco, --trazas, --listar)'),
        ('validar', 'delega en NIWG iw_validar.py (auditar-log, diag-7de-semilla, matriz)'),
        ('log-7de', 'delega en tools/iw_7de_log.py (auditoria Low->Aim y semilla 7DE)'),
        ('campana', 'delega en tools/iw_campana.py'),
        ('ladrillos', 'delega en tools/astra_iw_ladrillos_cotejo.py'),
        ('comparar', 'delega en tools/iw_mips/comparar.py'),
        ('comparar-trad', 'delega en tools/iw_mips/comparar_trad.py'),
        ('cierre-r23', 'delega en tools/iw_cierre_r23.py'),
    ]:
        sp.add_parser(nombre, help=texto + '; use --help para sus opciones', add_help=False).set_defaults(pass_through=True)

    return p


def _normalizar_atajos(argv: list[str]) -> list[str]:
    """Permite --self-check y --autoprueba como banderas de nivel superior (solo antes del subcomando)."""
    out = []
    i = 0
    en_cabecera = True
    while i < len(argv):
        tok = argv[i]
        if en_cabecera and tok in ('--repo', '--trabajo') and i + 1 < len(argv):
            out.extend([tok, argv[i + 1]])
            i += 2
            continue
        if en_cabecera and tok in ('--self-check', '--autoprueba'):
            out.append('autoprueba')
            en_cabecera = False
            i += 1
            continue
        if en_cabecera and not tok.startswith('-'):
            en_cabecera = False
        out.append(tok)
        i += 1
    return out


def main(argv=None) -> int:
    raw_argv = list(sys.argv[1:] if argv is None else argv)
    norm_argv = _normalizar_atajos(raw_argv)
    p = cli()
    a, args = p.parse_known_args(norm_argv)
    modo = getattr(a, 'cotejo', None)
    pass_through = getattr(a, 'pass_through', False) or (modo in BACKENDS) or (modo in ('niwe', 'ventanas'))
    if args and not pass_through:
        p.error(f'argumentos no reconocidos: {" ".join(args)}')
    repo = a.repo.resolve()
    trabajo = a.trabajo.resolve()
    try:
        conectar(repo)
        if modo in BACKENDS:
            r = delegar(repo, modo, args)
            return r.returncode if r.returncode in (0, 1, 2, 3) else 3
        if modo in ('niwe', 'ventanas'):
            r = delegar_herramienta(repo, trabajo, DELEGACIONES_EXTRA[modo], args)
            return r.returncode if r.returncode in (0, 1, 2, 3) else 3
        if a.accion in DELEGACIONES_EXTRA:
            r = delegar_herramienta(repo, trabajo, DELEGACIONES_EXTRA[a.accion], args)
            return r.returncode if r.returncode in (0, 1, 2, 3) else 3
        if a.accion == 'autoprueba':
            autoprueba(repo, trabajo, exigir_todas=a.exigir_todas)
            return 0
        if a.accion == 'inventario':
            doc = auditar_inventario(repo, trabajo, a.comprobar_help, a.comprobar_autoprueba)
            if a.json:
                emitir(doc, a.json)
            if a.md:
                escribir_texto_nuevo(inventario_md(doc), a.md)
            if not a.json and not a.md:
                print(inventario_md(doc), end='')
            return 0
        if a.accion == 'cvars':
            doc = escanear_cvars(repo, ambito=a.ambito, solo_off=a.solo_off)
            if a.json:
                emitir(doc, a.json)
            if a.md:
                escribir_texto_nuevo(cvars_md(doc), a.md)
            if not a.json and not a.md:
                print(cvars_md(doc), end='')
            return 0
        if modo == 'limites':
            from iw_niwd._limites import medir
            d, rc = medir(repo, a)
        else:
            d, rc = catalogo(repo, a, solo_activos=a.accion == 'activos')
        emitir(d, a.json)
        return rc
    except (FileNotFoundError, FileExistsError, ValueError, argparse.ArgumentTypeError) as e:
        print(f'IW sin datos/uso: {e}', file=sys.stderr)
        return 2
    except (KeyError, RuntimeError, AssertionError, OSError, struct.error, subprocess.SubprocessError) as e:
        print(f'IW error: {e}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())

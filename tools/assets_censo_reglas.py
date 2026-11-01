"""Reglas revisadas solAST. NO deducir un consumidor de un numero aislado.

port_lectura solo acredita las anclas revisadas como lectura, nunca comentarios.
verificado solo si el ID esta en la llamada o en una tabla ELF leida por ella.
"""
RULES = []


def add(modo, juego, entradas, port, archivo='DATA_CMN.AFS', **kw):
    RULES.append(dict(modo=modo, juego=juego, entradas=list(entradas), archivo=archivo, port=port, **kw))


# IW: son grupos de recursos; el decomp PAL tiene llamadas sin parametros.
add('IW_MINIJUEGOS','iw',range(864,941),
    [('src/iw_minijuegos.cpp','const std::vector<uint8_t> pack = cmn.Entrada'),
     ('src/iw_mini_vuelo.cpp','const std::vector<uint8_t> amb = cmn.Entrada(891)'),
     ('src/iw_mini_disparo.cpp','const std::vector<uint8_t> amb = cmn.Entrada(890)')],
    original=[dict(funcion='298760',contiene='LoadWaitDragonMissionGroundMinigameItems_')],
    limite='Grupo candidato 864..940; NO demuestra consumo de cada hijo. IW USA frente a decomp PAL; cargas sin argumentos.')
add('IW_MINIJUEGOS','iw',[204],
    [('src/anim_iw.cpp','constexpr size_t kIwFid = 204')],
    original=[dict(funcion='207ce0')],
    limite='Banco comun identificado en port; consumidores individuales por codigo/BSK pendientes.')
for entry, needle in [(6,'const std::vector<uint8_t> ui = P::HijoAmb(usa.Entrada(6), 0)'),
                      (5,'const auto paquete=usa.Entrada(5)'),
                      (9,'const std::vector<uint8_t> hud = P::HijoAmb(usa.Entrada(9), 1)'),
                      (334,'usa.Entrada(334)')]:
    file='src/iw_mini_vuelo.cpp' if entry==5 else 'src/iw_mini_disparo.cpp' if entry==9 else 'src/iw_minijuegos.cpp'
    add('IW_MINIJUEGOS','iw',[entry],[(file,needle)],archivo='DATA_USA.AFS',
        limite='Lectura real del paquete; sin validar todos sus hijos ni cadena ID PAL/USA.')
add('IW_CAMPANA','iw',[1093,1127,967],
    [('src/pasarela_iw.cpp','CargarHabilidadesIW('),('src/pasarela_iw.cpp','g_deseos_iw')],
    original=[dict(funcion='2ade30'),dict(funcion='2ab8e0')],
    limite='DMD/SCD/deseos: clasificacion por fuente del port; faltan consumidores por registro y prueba ID original.')
add('IW_CAMPANA','iw',range(132,334),
    [('src/pasarela_b1.cpp','mi.titulo = Textura(tit, 0)')],archivo='DATA_USA.AFS',
    limite='Titulos y descripciones por mision; limites de rango incluyen candidatos no resueltos, no todos son usados.')
add('IW_CAMPANA','iw',[334],
    [('src/pasarela_iw.cpp','g_ama_mapa_datos'),('src/pasarela_iw.cpp','g_ama_historia_datos')],archivo='DATA_USA.AFS',
    limite='Mapa e insignias identificados; consumidor original por hijo pendiente.')

# B2: IDs regionales codificados con bit 0x10000 para DATA_<idioma>.
for mode in ('B2_TABLERO','B2_NAVE'):
    add(mode,'b2',[933],[('src/pasarela_nave.cpp','g_animv = LeerAma(cmn.Entrada(933))')],
        original=[dict(funcion='2a44c0',contiene='DAT_003a4018',verificado=True,tabla='0x3a4018',n=2)],
        estado='bien',port_lectura=True,limite='Carga y lectura comprobadas; equivalencia visual no medida.')
    add(mode,'b2',[1148],[('src/pasarela_nave.cpp','leer(1148, g_pzv)')],archivo='DATA_UK.AFS',
        original=[dict(funcion='2a44c0',contiene='DAT_003a4018',verificado=True,tabla='0x3a4018',n=2)],
        estado='bien',port_lectura=True,limite='Tabla de carga y lector comprobados; imagenes internas no cruzadas una a una.')
add('B2_TABLERO','b2',range(334,405),
    [('src/pasarela_b2.cpp','Fase k (0..8)'),('src/pasarela_b2.cpp','fichas CMN 334')],
    original=[dict(funcion='2350c0',contiene='FUN_0012d100')],
    limite='Incluye YDW, mapas, AMA, AMM, fichas; rango candidato. Una lectura del paquete no prueba todos los subrecursos.')
add('B2_TABLERO','b2',list(range(146,156))+[184,188,189,190,1145,1147],
    [('src/pasarela_b2.cpp','DATA_<idioma> 1145')],archivo='DATA_UK.AFS',
    original=[dict(funcion='2350c0',contiene='FUN_0012d100')])
for entry, literal, needle in [(398,'0x18e','amt(cmn, 398, d.iconos)'),(402,'0x192','leer_ama(402, d.hud_ama)'),
                               (397,'0x18d','leer_ama(397, d.life_ama)')]:
    add('B2_TABLERO','b2',[entry],[('src/pasarela_b2.cpp',needle)],
        original=[dict(funcion='2350c0',contiene=f'FUN_0012d100({literal},',verificado=True,literal_id=True)],
        estado='bien',port_lectura=True,limite='Carga literal original + lectura port; fidelidad visual no probada.')
add('B2_TABLERO','b2',[188],[('src/pasarela_b2.cpp','amt(loc, 188, d.hud)')],archivo='DATA_UK.AFS',
    original=[dict(funcion='2350c0',contiene='FUN_0012d100(0x100bc,',verificado=True,literal_id=True)],
    estado='bien',port_lectura=True,limite='Carga del banco comprobada; no prueba cada imagen.')
for entries, table, n, anchor in [
        (range(387,396),'0x377c50',9,'cmn.Entrada(387 +'),
        (range(342,387,5),'0x377b70',36,'amt(cmn, 342 +'),
        (range(346,387,5),'0x377b70',36,'amt(cmn, 346 +'),
        (range(343,387,5),'0x377c00',18,'leer_ama(343 +'),
        (range(344,387,5),'0x377c00',18,'leer_ama(344 +'),
        (range(345,387,5),'0x377b70',36,'amt(cmn, 345 +')]:
    add('B2_TABLERO','b2',entries,[('src/pasarela_b2.cpp',anchor)],
        original=[dict(funcion='2350c0',contiene='DAT_'+table[2:].zfill(8),verificado=True,tabla=table,n=n)],
        estado='bien',port_lectura=True,limite='Tabla ELF de la misma ISO PAL que el decomp; carga del banco, no equivalencia visual.')
add('B2_TABLERO','b2',range(146,155),[('src/pasarela_b2.cpp','amt(loc, 146 + size_t(fase), d.loc)')],archivo='DATA_UK.AFS',
    original=[dict(funcion='2350c0',contiene='DAT_00377b70',verificado=True,tabla='0x377b70',n=36)],
    estado='bien',port_lectura=True,limite='Carga por fase confirmada; imagenes Stage/objetivo seleccionadas aparte.')
add('B2_NAVE','b2',range(897,915),
    [('src/pasarela_nave.cpp','g_anim = LeerAma(cmn.Entrada(905))'),
     ('src/pasarela_nave.cpp','g_principal_avion = ps2::CargarSuelto')],
    original=[dict(funcion='262dd0'),dict(funcion='23dae0',contiene='FUN_0012d100')])
add('B2_NAVE','b2',[931,851,852]+list(range(801,849)),
    [('src/pasarela_nave.cpp','if (g_amm931.empty())'),('src/pasarela_nave.cpp','cmn.Entrada(851)')])
add('B2_NAVE','b2',[1127,1131,1132,1133,1134,1135,1145,541]+list(range(542,1062)),
    [('src/pasarela_nave.cpp','texs(loc.Entrada(1127)'),('src/pasarela_nave.cpp','leer(541, g_ficha)')],archivo='DATA_UK.AFS',
    limite='Incluye textos de capsulas por ID; no se ha probado que todas sean ganables en la nave.')

# B1 usa DATA_EN para AMM/cinematica, DATA_SP para arte localizado.
add('SATAN_B1','b1',range(665,671),
    [('src/pasarela_b1.cpp','const int banco = BancoCharlas(afs)'),
     ('src/pasarela_b1.cpp','out.res.clave = Textura(res, 25)')],archivo='DATA_SP.AFS',
    original=[dict(funcion='232a80'),dict(funcion='235310')],
    limite='Indices PAL del detector BancoCharlas. Falta cruce de las tablas de carga del original.')
add('SATAN_B1','b1',[2358,2363,1297,2445,2528,1960],
    [('src/pasarela.cpp','Satan 2358'),('src/pasarela.cpp','LeerTrayectoriaB1(HijoAmb(paq, 3)')],archivo='DATA_EN.AFS',
    limite='Candidatos a pose, cuerpo y escena por notas del port; no cuenta como lectura de cada ID.')

# Refinamientos posteriores a los rangos candidatos (la regla mas especifica gana).
for entry, addr, anchor in [(666,'22d5f0','const std::vector<uint8_t> pantalla = afs.Entry(size_t(banco - 3))'),
                            (668,'232fb0','const std::vector<uint8_t> res = afs.Entry(size_t(banco - 1))'),
                            (669,'232fb0','const std::vector<uint8_t> charlas = afs.Entry(size_t(banco))')]:
    add('SATAN_B1','b1',[entry],[('src/pasarela_b1.cpp',anchor)],archivo='DATA_SP.AFS',
        original=[dict(funcion=addr,contiene=f'FUN_00138240({hex(entry)},',verificado=True,literal_id=True)],
        estado='bien',port_lectura=True,limite='B1 PAL: IDs literales originales y lectura relativa al banco 669; no aprueba cada sprite.')
for entry, file, anchor in [(905,'DATA_CMN.AFS','g_anim = LeerAma(cmn.Entrada(905))'),
                             (1131,'DATA_UK.AFS','leer(1131, g_pz)'),
                             (1132,'DATA_UK.AFS','leer(1132, g_frases)')]:
    add('B2_NAVE','b2',[entry],[('src/pasarela_nave.cpp',anchor)],archivo=file,
        original=[dict(funcion='265f20',contiene='DAT_00378350',verificado=True,tabla='0x378350',n=3)],
        estado='bien',port_lectura=True,limite='Tabla de tres recursos leida por el original y mismos bancos leidos por el port.')
for entry, file, anchor in [(930,'DATA_CMN.AFS','leer_ama(930, d.zenie_ama)'),
                             (1147,'DATA_UK.AFS','amt(loc, 1147, d.zenie)')]:
    add('B2_TABLERO','b2',[entry],[('src/pasarela_b2.cpp',anchor)],archivo=file,
        original=[dict(funcion='2547a0',contiene='DAT_003a3ee0',verificado=True,tabla='0x3a3ee0',n=2)],
        estado='bien',port_lectura=True,limite='Ventana de zenie: carga comprobada, no cada cifra.')
add('IW_MINIJUEGOS','iw',[895],[('src/iw_mini_disparo.cpp','const std::vector<uint8_t> manos = cmn.Entrada(895)')],
    original=[dict(funcion='30cef0',contiene='RAMShootingMinigame_(0,0x37f,0,0)')],
    limite='ID literal PAL 0x37F=895 coincide con lectura USA; falta homologar hijos y consumidor de cada malla/textura.')
add('IW_MINIJUEGOS','iw',[813],[('src/iw_minijuegos.cpp','const auto hit=cmn.Entrada(813)')],
    original=[dict(funcion='1fc290')],limite='Impactos AP5; atlas0 y efectos1/6 cargados. Uso por evento sin comparar con original.')
add('IW_MINIJUEGOS','iw',[938,954],[('src/anim_iw.cpp','archivo.Entrada(size_t(se.banco))'),
                                  ('src/iw_ap_eventos.h','secuencia del banco comun de minijuegos')],
    limite='Audio SE: 938 por superficie y 954 por AP1; faltan secuencia/evento/funcion por sonido.')
add('IW_MINIJUEGOS','iw',[893],[('src/iw_minijuegos.cpp','d.m_roca[i] = CargarModelo(pack, 16, 15, 0)')],
    hijos=['16'],estado='NO COMPROBADO',prioridad='P3',
    original=[dict(funcion='2a9bf0')],
    impacto='Variante0 repetida; no se ha demostrado que el original use otras en esta malla.',
    arreglo='Seguir slots de Ki_UpdateProjectileFlight; 321350 y umbral50/100 corresponden a objetosD, no a rocas.',
    limite='Rectificacion solAST2: la referencia anterior a321350 no demostraba la seleccion de roca.')

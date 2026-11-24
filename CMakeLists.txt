# dbz3 - ReXGlue Recompiled Project
# Dragon Ball Z Budokai 3 (PAL / Xbox 360 HD Collection)

cmake_minimum_required(VERSION 3.25)
project(dbz3 LANGUAGES CXX)

option(DBZ3_LINUX_ARM64_CHECK "Check the Linux ARM64 build and both generated regions" OFF)
if(DBZ3_LINUX_ARM64_CHECK)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
       NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        message(FATAL_ERROR "linux-arm64-release requires a Linux ARM64 target")
    endif()
    if(NOT DBZ3_DUAL_REGION OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/sources.cmake" OR
       NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated_eu/sources.cmake")
        message(FATAL_ERROR "linux-arm64-release requires both US and EU generated sources")
    endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find ReXGlue SDK (pre-built distribution)
find_package(rexglue REQUIRED CONFIG)

# User source files
set(DBZ3_SOURCES
    src/main.cpp
    src/hooks.cpp
    src/roster_trace.cpp
    src/roster_ext.cpp
    src/select_ext.cpp
    src/stage_ext.cpp
    src/mods.cpp
    src/mods_opcionales.cpp
    src/native_mods.cpp
    src/region.cpp
    src/launcher/settings.cpp
    src/launcher/launcher_state.cpp
    src/launcher/ui_kit.cpp
    src/launcher/mod_pipeline.cpp
    src/launcher/ids_mods.cpp
    src/launcher/i18n.cpp
    src/launcher/update_check.cpp
    src/launcher/novas_mods.cpp
    src/ingame/menu.cpp
    src/ingame/quick_settings.cpp
    src/ingame/game_style.cpp
    src/ingame/native_ui.cpp
    src/xma_propio.cpp
    src/ingame/menu_pilot.cpp
    src/ingame/online_panel.cpp
    src/ingame/tagteam_panel.cpp
    src/ingame/color_panel.cpp
    src/ingame/tutorial.cpp
    src/net/netplay.cpp
    src/net/room.cpp
    src/net/netplay_hooks.cpp
    src/tagteam.cpp
    src/tagteam_slots.cpp
    src/campaigns.cpp
    src/hub.cpp
    src/escenarios.cpp
    src/clima.cpp
    src/cine.cpp
    src/gigantes.cpp
    src/pasarela.cpp
    src/pasarela_b1.cpp
    src/pasarela_misiones.cpp
    src/hub_menu.cpp
    src/ingame/hub_panel.cpp
    src/modos.cpp
    src/rpg.cpp
    src/rogue.cpp
    src/rogue_evoluciones.cpp
    src/rogue_caminos.cpp
    src/rogue_resultado.cpp
    src/rogue_fx.cpp
    src/ranking_online.cpp
    src/heroe.cpp
    src/heroe_avatar.cpp
    src/heroe_ui.cpp
    src/cac/cac.cpp
    src/fusion.cpp
    src/pasarela_iw.cpp
    src/pasarela_b2.cpp
    src/pasarela_nave.cpp
    src/tactics/prueba_figuras.cpp
    src/tactics/entrada.cpp
    src/tactics/sim.cpp
    src/tactics/catalogo.cpp
    src/tactics/partida.cpp
    src/survivors/sim.cpp
    src/survivors/prueba.cpp
    src/survivors/juego.cpp
    src/survivors/entrada.cpp
    src/survivors/recursos.cpp
    src/survivors/impostores.cpp
    src/survivors/modo.cpp
    src/survivors/modo_mundo.cpp
    src/survivors/modo_pantallas.cpp
    src/survivors/modo_audio.cpp
    src/survivors/tienda.cpp
    src/survivors/ambiente.cpp
    src/iw_minijuegos.cpp
    src/cinematica.cpp
    src/freecam.cpp
    src/ports_guard.cpp
    src/ports_regs_luchadores.cpp
    src/combate.cpp
    $<$<OR:$<BOOL:${DBZ3_DUAL_REGION}>,$<NOT:$<STREQUAL:${DBZ3_GENERATED_DIR},generated_eu>>>:src/comp_aura.cpp>
    $<$<OR:$<BOOL:${DBZ3_DUAL_REGION}>,$<NOT:$<STREQUAL:${DBZ3_GENERATED_DIR},generated_eu>>>:src/comp_hud.cpp>
    src/plugins.cpp
    src/transformaciones.cpp
    src/video_sfd.cpp
    src/anim_iw.cpp
    src/boca.cpp
    src/suelo_iw.cpp
    src/ps2_modelo.cpp
    src/iw_mini_botones.cpp
    src/iw_mini_vuelo.cpp
    src/iw_mini_disparo.cpp
    src/iw_mini_extra.cpp
    src/iw_mips.cpp
    src/iw_gs_traductor.cpp
    src/iw_trad_bucle.cpp
    src/datos.cpp
)

# Codegen output directory. Default is the US/NA build (generated/); a second
# variant built from the EU/PAL executable uses -DDBZ3_GENERATED_DIR=generated_eu
# and produces a core that boots the EU xex (see dbz3_manifest_eu.toml).
set(DBZ3_GENERATED_DIR "generated" CACHE STRING
    "Codegen output dir to build against (generated = US/NA, generated_eu = EU/PAL)")

# Dual-region core: ONE executable containing BOTH codegens. At runtime the
# core detects the default.xex (MD5) and activates the matching image config.
# This is the user-facing release build (single exe for US and EU).
set(DBZ3_DUAL_REGION OFF CACHE BOOL
    "Build one core with BOTH the US and EU codegens (auto-detects the xex)")

# Include generated code if codegen has been run
if(DBZ3_DUAL_REGION OR DBZ3_GENERATED_DIR STREQUAL "generated")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/sources.cmake")
        include(${CMAKE_CURRENT_SOURCE_DIR}/generated/sources.cmake)
        list(APPEND DBZ3_SOURCES ${GENERATED_SOURCES})
        # Opus R5 (rollback): escrituras vectoriales/dcbz/atomicas del codigo US al registro de deshacer (src/net/rb_store.h)
        set_source_files_properties(${GENERATED_SOURCES} PROPERTIES COMPILE_DEFINITIONS DBZ3_RBW_TU=1)
        # El PCH se regenera sin el registro: asegurar un include final fuera de bloques condicionales/comentarios.
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/dbz3_pch.h")
            message(FATAL_ERROR "Falta generated/dbz3_pch.h: regenerar el core US antes de configurar rollback")
        endif()
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/generated/dbz3_pch.h" DBZ3_PCH_TEXTO)
        # La linea vacia anterior evita continuaciones de macros; EOF evita aceptar un include dentro de #if 0.
        # rb_store.h tiene pragma once: un include previo no hace dano. Tras anadirlo, configurar es idempotente.
        if(NOT DBZ3_PCH_TEXTO MATCHES "(^|\n[ \t]*\n)[ \t]*#[ \t]*include[ \t]*\"\\.\\./src/net/rb_store\\.h\"[ \t\r\n]*$")
            file(APPEND "${CMAKE_CURRENT_SOURCE_DIR}/generated/dbz3_pch.h" "\n\n#include \"../src/net/rb_store.h\"\n")
        endif()
    endif()
endif()
if(DBZ3_DUAL_REGION OR DBZ3_GENERATED_DIR STREQUAL "generated_eu")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated_eu/sources.cmake")
        include(${CMAKE_CURRENT_SOURCE_DIR}/generated_eu/sources.cmake)
        list(APPEND DBZ3_SOURCES ${GENERATED_SOURCES})
    endif()
endif()

# hooks.cpp only applies to builds that include the US/NA codegen (single US
# and dual-region). The single-EU build excludes it: the EU guest addresses
# differ and the US hooks would not apply.
if(DBZ3_GENERATED_DIR STREQUAL "generated_eu" AND NOT DBZ3_DUAL_REGION)
    list(REMOVE_ITEM DBZ3_SOURCES src/hooks.cpp src/heroe_avatar.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/roster_trace.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/combate.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/transformaciones.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/video_sfd.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/anim_iw.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/boca.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/suelo_iw.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/ps2_modelo.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/iw_mini_botones.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/iw_mini_vuelo.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/iw_mini_disparo.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/roster_ext.cpp src/select_ext.cpp src/stage_ext.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/net/netplay_hooks.cpp src/tagteam.cpp src/tagteam_slots.cpp src/campaigns.cpp src/hub.cpp src/escenarios.cpp src/clima.cpp src/cine.cpp src/gigantes.cpp src/pasarela.cpp src/pasarela_b1.cpp src/pasarela_misiones.cpp src/hub_menu.cpp src/ingame/hub_panel.cpp src/ingame/tutorial.cpp src/modos.cpp src/rpg.cpp src/rogue.cpp src/rogue_caminos.cpp src/rogue_resultado.cpp src/ranking_online.cpp src/rogue_evoluciones.cpp src/heroe.cpp src/heroe_ui.cpp src/cac/cac.cpp src/fusion.cpp src/pasarela_iw.cpp src/pasarela_b2.cpp src/pasarela_nave.cpp src/tactics/prueba_figuras.cpp src/tactics/entrada.cpp src/tactics/sim.cpp src/tactics/catalogo.cpp src/tactics/partida.cpp src/iw_minijuegos.cpp src/iw_mips.cpp src/iw_gs_traductor.cpp src/iw_trad_bucle.cpp src/cinematica.cpp src/freecam.cpp)
endif()

# src/escenarios_sel_gen.inc sale del codigo generado (US) y no va al repo publico: si falta, se crea aqui
# (tools/gen_escenarios_sel.py lee generated/).
# (y su gemela EU escenarios_sel_gen_eu.inc en el nucleo dual: generated_eu/ + src/eu_map_gen.h, frente eu)
if(src/escenarios.cpp IN_LIST DBZ3_SOURCES AND (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/escenarios_sel_gen.inc" OR
   (DBZ3_DUAL_REGION AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/escenarios_sel_gen_eu.inc")))
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_escenarios_sel.py"
                    RESULT_VARIABLE DBZ3_ESC_RC)
    if(NOT DBZ3_ESC_RC EQUAL 0)
        message(FATAL_ERROR "tools/gen_escenarios_sel.py fallo (necesita generated/ de la US)")
    endif()
endif()

if(WIN32)
    # dbz3.manifest: pagina de codigos UTF-8 (CMake lo funde con el manifiesto del enlazador)
    add_executable(dbz3 WIN32 ${DBZ3_SOURCES} src/version.rc src/dbz3.manifest)
    # UCRT marks getenv/fopen as "deprecated" even though they are the portable
    # choice here (the game also builds on Linux where they are not). Silence
    # the -Wdeprecated-declarations noise project-wide on MSVC/clang-cl.
    target_compile_definitions(dbz3 PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    add_executable(dbz3 ${DBZ3_SOURCES})
endif()

# CaC: prueba de persistencia real, sin motor; se compila con tools/compilar.py.
add_executable(cac_guardado_test tests/cac_guardado_test.cpp src/cac/cac.cpp)
target_include_directories(cac_guardado_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rexglue/include $<TARGET_PROPERTY:rex::runtime,INTERFACE_INCLUDE_DIRECTORIES>)
add_executable(cac_recetas_test tests/cac_recetas_test.cpp src/cac/cac.cpp)   # cac1: migracion v2->v3 y recetas
target_include_directories(cac_recetas_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rexglue/include $<TARGET_PROPERTY:rex::runtime,INTERFACE_INCLUDE_DIRECTORIES>)

# Dragon Tactics H2 (T8/T9): simulacion de combate + catalogo + pruebas, sin el juego ni el SDK. No entra en "all":
#   cmake --build <build> --target tactics_sim_test   y luego   tactics_sim_test "mod center hd" D:/tmp   (desde la raiz del repo)
add_executable(tactics_sim_test EXCLUDE_FROM_ALL tests/tactics_sim_test.cpp src/tactics/sim.cpp src/tactics/catalogo.cpp)
target_include_directories(tactics_sim_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rexglue/include)
# Dragon Tactics H3: partida (economia, tienda, copias, sinergias, bot). Mismo uso:   tactics_partida_test "mod center hd" D:/tmp [partidas]
add_executable(tactics_partida_test EXCLUDE_FROM_ALL tests/tactics_partida_test.cpp src/tactics/partida.cpp src/tactics/sim.cpp src/tactics/catalogo.cpp)
target_include_directories(tactics_partida_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rexglue/include)
# Dragon Tactics H5: equilibrio (bots contra bots: victorias por unidad, sinergia y coste, y duracion de la partida).   tactics_equilibrio "mod center hd" [duelos] [torneos]
add_executable(tactics_equilibrio EXCLUDE_FROM_ALL tests/tactics_equilibrio.cpp src/tactics/partida.cpp src/tactics/sim.cpp src/tactics/catalogo.cpp)
target_include_directories(tactics_equilibrio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rexglue/include)
# Dragon Budokai Survivors (prueba tecnica): simulacion de la horda (determinismo, solape y ms por paso con 100..2000 enemigos).   survivors_sim_test
add_executable(survivors_sim_test EXCLUDE_FROM_ALL tests/survivors_sim_test.cpp src/survivors/sim.cpp)
target_include_directories(survivors_sim_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Dragon Budokai Survivors: partida (determinismo, 20 partidas con bot, cada tecnica hace dano, ms por paso).   survivors_juego_test [res/survivors/survivors.toml] [partidas]
add_executable(survivors_juego_test EXCLUDE_FROM_ALL tests/survivors_juego_test.cpp src/survivors/juego.cpp src/survivors/sim.cpp src/survivors/tienda.cpp)
target_include_directories(survivors_juego_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rexglue/include)
# Dragon Budokai Survivors, P1 (A02, A05, A06, A08, A09): lógica nueva sin juego.   survivors_p1_test
add_executable(survivors_p1_test EXCLUDE_FROM_ALL tests/survivors_p1_test.cpp src/survivors/juego.cpp src/survivors/sim.cpp src/survivors/tienda.cpp)
target_include_directories(survivors_p1_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rexglue/include)
# SUR-07: decodificador XMA propio (bancos #AMB de B3 HD) sin el juego.   xma_propio_test <entrada.dec> [carpeta_wav]
add_executable(xma_propio_test EXCLUDE_FROM_ALL tests/xma_propio_test.cpp src/xma_propio.cpp)
target_compile_definitions(xma_propio_test PRIVATE XMA_PROPIO_TEST=1)
target_include_directories(xma_propio_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/rexglue-sdk-0.10/thirdparty/FFmpeg)
target_link_libraries(xma_propio_test PRIVATE rex::libavcodec)

target_include_directories(dbz3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
if(DBZ3_DUAL_REGION OR DBZ3_GENERATED_DIR STREQUAL "generated")
    target_include_directories(dbz3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/generated)
endif()
if(DBZ3_DUAL_REGION OR DBZ3_GENERATED_DIR STREQUAL "generated_eu")
    target_include_directories(dbz3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/generated_eu)
endif()

# Variant compile definitions (selects the init headers in main.cpp).
if(DBZ3_DUAL_REGION)
    target_compile_definitions(dbz3 PRIVATE DBZ3_DUAL_REGION=1)
elseif(DBZ3_GENERATED_DIR STREQUAL "generated_eu")
    target_compile_definitions(dbz3 PRIVATE DBZ3_EU_VARIANT=1)
endif()

target_link_libraries(dbz3 PRIVATE
    rex::runtime
    rex::gpu-xenos
)
# SUR-07 (src/xma_propio.cpp, xmaframes) usa el mismo FFmpeg estatico que los videos SFD (src/video_sfd.cmake). Con
# rex::libavcodec enlazado ademas, su avcodec_find_decoder (lista solo de audio) tapaba el MPEG-2: los SFD de IW no abrian.
target_include_directories(dbz3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rexglue-sdk-0.10/thirdparty/FFmpeg)

if(WIN32)
    # Launcher update check (GitHub releases over WinHTTP) and VERSIONINFO
    # reading (winver) for the running build.
    target_link_libraries(dbz3 PRIVATE winhttp version ws2_32 ole32 oleaut32 bcrypt advapi32)
else()
    # xma_propio usa avcodec tambien fuera de Windows. Alli no se enlaza dbz3_ffvideo.
    target_link_libraries(dbz3 PRIVATE rex::libavcodec)
endif()

# libjuice v1.7.4 (ICE/STUN/TURN, MPL-2.0, third_party/libjuice sin cambios): biblioteca estatica para el transporte de R5. APAGADO por
# defecto y sin uso en el codigo: el online sigue con el socket UDP propio de src/net (netplay.cpp: OpenSocket/Send/Recv/ResetTransport).
option(DBZ3_LIBJUICE "Enlazar libjuice (transporte ICE de R5; aun sin usar)" OFF)
if(DBZ3_LIBJUICE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjuice/CMakeLists.txt")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(NO_SERVER ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/libjuice ${CMAKE_BINARY_DIR}/third_party_libjuice EXCLUDE_FROM_ALL)
    target_link_libraries(dbz3 PRIVATE juice-static)
    target_compile_definitions(dbz3 PRIVATE DBZ3_LIBJUICE=1)
endif()

if(UNIX AND NOT APPLE)
    # Whole-archive linking for kernel hooks
    target_link_options(dbz3 PRIVATE
        -Wl,--whole-archive
        $<TARGET_FILE:rex::kernel>
        -Wl,--no-whole-archive
    )
endif()

# SDK host-app configuration: adds SDL entry point + ReXApp source,
# REXGLUE_BUILD_CONFIG define, runtime DLL staging (rexruntime, TracyClient),
# and copies the xenos GPU plugin next to the executable.
rexglue_configure_target(dbz3 GPU_PLUGINS xenos)

# Plataformas fuera de PC (Android, Switch): su carpeta trae un platform.cmake que anade a `dbz3` sus
# fuentes, definiciones y DBZ3_PLATFORM_HEADER (enganches de src/main.cpp). Vacio = PC, sin cambios.
set(DBZ3_PLATFORM_DIR "" CACHE PATH "Carpeta de plataforma con platform.cmake (vacio = PC)")
if(DBZ3_PLATFORM_DIR)
    include("${DBZ3_PLATFORM_DIR}/platform.cmake")
endif()

# Videos SFD de IW (frente R7): MPEG-2 del FFmpeg del SDK, objetivo estatico propio solo en el exe
include(src/video_sfd.cmake)

# SDL game controller mappings (community GameControllerDB, zlib). The runtime's
# `hid_mappings_file` cvar defaults to "gamecontrollerdb.txt" and loads it from
# the working directory, so shipping it next to the exe gives the SDL input
# backend real mappings for generic pads. See AGENTS.md seccion 7.
set(DBZ3_CONTROLLERDB "${CMAKE_CURRENT_SOURCE_DIR}/gamecontrollerdb.txt")
if(EXISTS "${DBZ3_CONTROLLERDB}")
    add_custom_command(TARGET dbz3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DBZ3_CONTROLLERDB}" "$<TARGET_FILE_DIR:dbz3>/gamecontrollerdb.txt"
        COMMENT "Staging gamecontrollerdb.txt next to dbz3"
        VERBATIM)
endif()

# Recursos de las pantallas propias (TAG TEAM, ONLINE, fichas del Tag Team): tools/ingame_ui.py
# (el repo publico no lleva res/: sin la carpeta, las pantallas escriben el titulo con texto)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/ingame_ui")
add_custom_command(TARGET dbz3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/res/ingame_ui" "$<TARGET_FILE_DIR:dbz3>/ingame_ui"
    COMMENT "Staging ingame_ui next to dbz3"
    VERBATIM)
endif()

# Datos medidos del original (tools/extraer_datos.py -> res/datos/*.toml; src/datos.cpp los lee y recarga con F5)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/datos")
add_custom_command(TARGET dbz3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/res/datos" "$<TARGET_FILE_DIR:dbz3>/res/datos"
    COMMENT "Staging res/datos next to dbz3"
    VERBATIM)
endif()

# Contenido del Modo Heroe (8 sagas de res/heroe/sagas.toml): el cargador mira
# <exe>/res/heroe/sagas.toml (heroe.cpp Sagas). G06a.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/heroe")
add_custom_command(TARGET dbz3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/res/heroe" "$<TARGET_FILE_DIR:dbz3>/res/heroe"
    COMMENT "Staging res/heroe next to dbz3"
    VERBATIM)
endif()

# Dragon Budokai Survivors y Dragon Rogue: arte propio (res/mejoras: iconos y piezas de interfaz comunes; res/survivors: fondos y
# survivors.toml). survivors/recursos.cpp los busca en <exe>/res/.
foreach(_dbz3_res mejoras survivors)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/${_dbz3_res}")
add_custom_command(TARGET dbz3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/res/${_dbz3_res}" "$<TARGET_FILE_DIR:dbz3>/res/${_dbz3_res}"
    COMMENT "Staging res/${_dbz3_res} next to dbz3"
    VERBATIM)
endif()
endforeach()

# Codegen target - run 'cmake --build . --target dbz3_codegen'
add_custom_target(dbz3_codegen
    COMMAND $<TARGET_FILE:rex::rexglue> codegen ${CMAKE_CURRENT_SOURCE_DIR}/dbz3_manifest.toml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating recompiled code for dbz3"
    VERBATIM
)

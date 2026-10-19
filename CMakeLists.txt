# dbz3 - ReXGlue Recompiled Project
# Dragon Ball Z Budokai 3 (PAL / Xbox 360 HD Collection)

cmake_minimum_required(VERSION 3.25)
project(dbz3 LANGUAGES CXX)

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
    src/launcher/i18n.cpp
    src/launcher/update_check.cpp
    src/launcher/novas_mods.cpp
    src/ingame/menu.cpp
    src/ingame/quick_settings.cpp
    src/ingame/game_style.cpp
    src/ingame/native_ui.cpp
    src/ingame/menu_pilot.cpp
    src/ingame/online_panel.cpp
    src/ingame/tagteam_panel.cpp
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
    src/heroe.cpp
    src/fusion.cpp
    src/pasarela_iw.cpp
    src/pasarela_b2.cpp
    src/pasarela_nave.cpp
    src/iw_minijuegos.cpp
    src/cinematica.cpp
    src/ports_guard.cpp
    src/combate.cpp
    src/transformaciones.cpp
    src/video_sfd.cpp
    src/anim_iw.cpp
    src/suelo_iw.cpp
    src/ps2_modelo.cpp
    src/iw_mini_botones.cpp
    src/iw_mini_vuelo.cpp
    src/iw_mini_disparo.cpp
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
    endif()
endif()
if(DBZ3_DUAL_REGION OR DBZ3_GENERATED_DIR STREQUAL "generated_eu")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated_eu/sources.cmake")
        include(${CMAKE_CURRENT_SOURCE_DIR}/generated_eu/sources.cmake)
        list(APPEND DBZ3_SOURCES ${GENERATED_SOURCES})
    endif()
endif()

# Motor grafico "Direct3D 12 nativo (experimental)" del launcher: renderer propio (native_gpu/) para el
# guest EU/PAL en Windows. Ver src/native_gpu.h y native_gpu/INTEGRATION.md.
option(DBZ3_NATIVE_GPU "Build the experimental native Direct3D 12 renderer (Windows, EU/PAL guest)" ON)
set(DBZ3_NATIVE_GPU_ACTIVE OFF)
if(WIN32 AND DBZ3_NATIVE_GPU AND (DBZ3_DUAL_REGION OR DBZ3_GENERATED_DIR STREQUAL "generated_eu"))
    set(DBZ3_NATIVE_GPU_ACTIVE ON)
    list(APPEND DBZ3_SOURCES
        src/native_gpu_hooks.cpp
        native_gpu/lab/native_stream.cpp
        native_gpu/lab/native_services.cpp)
    set_source_files_properties(native_gpu/lab/native_stream.cpp native_gpu/lab/native_services.cpp
        PROPERTIES COMPILE_OPTIONS "-msse4.1;-Wno-deprecated-declarations")
endif()
list(APPEND DBZ3_SOURCES src/native_gpu.cpp)

# hooks.cpp only applies to builds that include the US/NA codegen (single US
# and dual-region). The single-EU build excludes it: the EU guest addresses
# differ and the US hooks would not apply.
if(DBZ3_GENERATED_DIR STREQUAL "generated_eu" AND NOT DBZ3_DUAL_REGION)
    list(REMOVE_ITEM DBZ3_SOURCES src/hooks.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/roster_trace.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/combate.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/transformaciones.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/video_sfd.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/anim_iw.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/suelo_iw.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/ps2_modelo.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/iw_mini_botones.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/iw_mini_vuelo.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/iw_mini_disparo.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/roster_ext.cpp src/select_ext.cpp src/stage_ext.cpp)
    list(REMOVE_ITEM DBZ3_SOURCES src/net/netplay_hooks.cpp src/tagteam.cpp src/tagteam_slots.cpp src/campaigns.cpp src/hub.cpp src/escenarios.cpp src/clima.cpp src/cine.cpp src/gigantes.cpp src/pasarela.cpp src/pasarela_b1.cpp src/pasarela_misiones.cpp src/hub_menu.cpp src/ingame/hub_panel.cpp src/ingame/tutorial.cpp src/modos.cpp src/rpg.cpp src/heroe.cpp src/fusion.cpp src/pasarela_iw.cpp src/pasarela_b2.cpp src/pasarela_nave.cpp src/iw_minijuegos.cpp src/cinematica.cpp)
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

if(WIN32)
    # Launcher update check (GitHub releases over WinHTTP) and VERSIONINFO
    # reading (winver) for the running build.
    target_link_libraries(dbz3 PRIVATE winhttp version ws2_32 ole32 oleaut32)   # ws2_32: online (src/net); ole32: UPnP del router
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

# Renderer D3D12 nativo junto al exe, en native_gpu/ (lo carga el proceso del juego, src/native_gpu.cpp)
if(DBZ3_NATIVE_GPU_ACTIVE)
    target_compile_definitions(dbz3 PRIVATE DBZ3_NATIVE_GPU=1)
    add_subdirectory(native_gpu EXCLUDE_FROM_ALL)
    add_dependencies(dbz3 dbz3-native-renderer)
    add_custom_command(TARGET dbz3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:dbz3>/native_gpu"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:dbz3-native-renderer>"
            "${CMAKE_CURRENT_SOURCE_DIR}/native_gpu/materials.hlsl"
            "${CMAKE_CURRENT_BINARY_DIR}/native_gpu/materials.dbzregistry"
            "$<TARGET_FILE_DIR:dbz3>/native_gpu/"
        COMMENT "Staging the native D3D12 renderer in native_gpu/"
        VERBATIM)
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

# Codegen target - run 'cmake --build . --target dbz3_codegen'
add_custom_target(dbz3_codegen
    COMMAND $<TARGET_FILE:rex::rexglue> codegen ${CMAKE_CURRENT_SOURCE_DIR}/dbz3_manifest.toml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating recompiled code for dbz3"
    VERBATIM
)

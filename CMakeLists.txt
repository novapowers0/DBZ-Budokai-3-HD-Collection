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
    src/mods.cpp
    src/region.cpp
    src/launcher/settings.cpp
    src/launcher/launcher_state.cpp
    src/launcher/mod_pipeline.cpp
    src/launcher/i18n.cpp
    src/ingame/menu.cpp
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

# hooks.cpp only applies to builds that include the US/NA codegen (single US
# and dual-region). The single-EU build excludes it: the EU guest addresses
# differ and the US hooks would not apply.
if(DBZ3_GENERATED_DIR STREQUAL "generated_eu" AND NOT DBZ3_DUAL_REGION)
    list(REMOVE_ITEM DBZ3_SOURCES src/hooks.cpp)
endif()

if(WIN32)
    add_executable(dbz3 WIN32 ${DBZ3_SOURCES} src/version.rc)
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

# Codegen target - run 'cmake --build . --target dbz3_codegen'
add_custom_target(dbz3_codegen
    COMMAND $<TARGET_FILE:rex::rexglue> codegen ${CMAKE_CURRENT_SOURCE_DIR}/dbz3_manifest.toml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating recompiled code for dbz3"
    VERBATIM
)

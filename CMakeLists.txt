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

# Include generated code if codegen has been run
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${DBZ3_GENERATED_DIR}/sources.cmake")
    include(${DBZ3_GENERATED_DIR}/sources.cmake)
    list(APPEND DBZ3_SOURCES ${GENERATED_SOURCES})
endif()

# EU/PAL variant: the generated code has different guest addresses and its own
# init headers (dbz3_eu_init.h). hooks.cpp only applies to the US/NA build.
if(DBZ3_GENERATED_DIR STREQUAL "generated_eu")
    list(REMOVE_ITEM DBZ3_SOURCES src/hooks.cpp)
endif()

if(WIN32)
    add_executable(dbz3 WIN32 ${DBZ3_SOURCES})
else()
    add_executable(dbz3 ${DBZ3_SOURCES})
endif()

# ISA bootstrap: baseline x86-64 dispatcher (no -march) that checks for the
# x86-64-v3 feature set and launches dbz3_avx2\dbz3.exe or
# dbz3_legacy\dbz3.exe. No SDK dependencies, so it runs on any CPU.
if(WIN32)
    add_executable(dbz3_bootstrap WIN32 src/bootstrap.cpp)
    target_compile_options(dbz3_bootstrap PRIVATE -march=x86-64)
    target_compile_definitions(dbz3_bootstrap PRIVATE NOMINMAX)
endif()

target_include_directories(dbz3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/${DBZ3_GENERATED_DIR}
)

# EU/PAL variant compile definition (selects dbz3_eu_init.h in main.cpp).
if(DBZ3_GENERATED_DIR STREQUAL "generated_eu")
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

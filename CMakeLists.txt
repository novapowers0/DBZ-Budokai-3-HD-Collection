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
    src/ingame/menu.cpp
)

# Include generated code if codegen has been run
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/sources.cmake")
    include(generated/sources.cmake)
    list(APPEND DBZ3_SOURCES ${GENERATED_SOURCES})
endif()

if(WIN32)
    add_executable(dbz3 WIN32 ${DBZ3_SOURCES})
else()
    add_executable(dbz3 ${DBZ3_SOURCES})
endif()

target_include_directories(dbz3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

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

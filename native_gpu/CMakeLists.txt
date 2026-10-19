cmake_minimum_required(VERSION 3.25)
project(dbz3_native_gpu LANGUAGES CXX)
if(NOT WIN32)
    message(FATAL_ERROR "The native renderer currently targets Windows / Direct3D 12")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(DBZ3_REGISTRY "${CMAKE_CURRENT_BINARY_DIR}/materials.dbzregistry")
add_custom_command(OUTPUT "${DBZ3_REGISTRY}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_registry.py"
        --output "${DBZ3_REGISTRY}"
    DEPENDS build_registry.py materials.registry.json
    VERBATIM)
add_custom_target(dbz3-material-registry ALL DEPENDS "${DBZ3_REGISTRY}")
add_executable(dbz3-native-gpu native_renderer.cpp)
add_library(dbz3-native-renderer SHARED native_renderer.cpp)
target_compile_definitions(dbz3-native-renderer PRIVATE DBZ_NATIVE_DLL)
foreach(target dbz3-native-gpu dbz3-native-renderer)
    add_dependencies(${target} dbz3-material-registry)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${target} PRIVATE d3d12 dxgi dxguid user32 gdi32 ole32)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()
    if(MINGW)
        target_link_libraries(${target} PRIVATE d3dcompiler_47)
    else()
        target_link_libraries(${target} PRIVATE d3dcompiler)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/materials.hlsl" "$<TARGET_FILE_DIR:${target}>/materials.hlsl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DBZ3_REGISTRY}" "$<TARGET_FILE_DIR:${target}>/materials.dbzregistry")
endforeach()
# Optional NVIDIA DLSS in the renderer DLL (DBZ3_NATIVE_DLSS=1 at run time). Same convention as the
# SDK's REXGLUE_DLSS_SDK_DIR: an extracted NVIDIA/DLSS release; nvngx_dlss.dll ships next to the exe.
set(DBZ3_DLSS_SDK_DIR "${REXGLUE_DLSS_SDK_DIR}" CACHE PATH "NVIDIA DLSS SDK root for the native renderer (optional)")
if(DBZ3_DLSS_SDK_DIR AND EXISTS "${DBZ3_DLSS_SDK_DIR}/include/nvsdk_ngx.h")
    target_include_directories(dbz3-native-renderer PRIVATE "${DBZ3_DLSS_SDK_DIR}/include")
    target_link_libraries(dbz3-native-renderer PRIVATE "${DBZ3_DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
    target_compile_definitions(dbz3-native-renderer PRIVATE DBZ_NATIVE_DLSS=1)
    message(STATUS "Native renderer DLSS: ${DBZ3_DLSS_SDK_DIR}")
endif()
# Independent CPU bridge: no ReX runtime or reference graphics libraries.
add_library(indexed-replay SHARED lab/native_indexed.cpp lab/native_stream.cpp lab/replay_api.cpp)
set_target_properties(indexed-replay PROPERTIES CXX_STANDARD 23)
target_compile_definitions(indexed-replay PRIVATE _CRT_SECURE_NO_WARNINGS)

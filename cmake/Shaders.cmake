# SPDX-License-Identifier: MIT
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/AcquireInputs.cmake")
function(kgfx_add_shader_tools)
    if(TARGET glslang-standalone)
        return()
    endif()
    if(CMAKE_CROSSCOMPILING)
        if(NOT KSTAR_GFX_HOST_TOOLS)
            message(FATAL_ERROR "Set KSTAR_GFX_HOST_TOOLS to the completed native host build directory")
        endif()
        foreach(tool IN ITEMS glslang-standalone spirv-cross spirv-val)
            if(tool STREQUAL "glslang-standalone")
                set(filename glslang)
                set(fallback "tools/glslang/StandAlone/Release")
            elseif(tool STREQUAL "spirv-cross")
                set(filename spirv-cross)
                set(fallback "tools/spirv-cross/Release")
            else()
                set(filename spirv-val)
                set(fallback "tools/spirv-tools/tools/Release")
            endif()
            unset(host_program CACHE)
            find_program(host_program NAMES ${filename} HINTS "${KSTAR_GFX_HOST_TOOLS}/bin/Release"
                "${KSTAR_GFX_HOST_TOOLS}/${fallback}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
            add_executable(${tool} IMPORTED GLOBAL)
            set_target_properties(${tool} PROPERTIES IMPORTED_LOCATION "${host_program}")
        endforeach()
        return()
    endif()
    kgfx_tool_source(glslang glslang_src)
    kgfx_tool_source(spirv-cross cross_src)
    kgfx_tool_source(spirv-headers headers_src)
    kgfx_tool_source(spirv-tools tools_src)
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
    add_subdirectory("${glslang_src}" "${PROJECT_BINARY_DIR}/tools/glslang" EXCLUDE_FROM_ALL)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_MSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory("${cross_src}" "${PROJECT_BINARY_DIR}/tools/spirv-cross" EXCLUDE_FROM_ALL)
    set(SPIRV-Headers_SOURCE_DIR "${headers_src}")
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
    set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
    set(SPIRV_SKIP_EXECUTABLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${tools_src}" "${PROJECT_BINARY_DIR}/tools/spirv-tools" EXCLUDE_FROM_ALL)
endfunction()

function(kgfx_compile_shaders target source_dir output_dir output_variable)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    kgfx_add_shader_tools()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(outputs)
    foreach(shader IN LISTS ARG_SOURCES)
        set(source "${source_dir}/${shader}")
        set(output "${output_dir}/${shader}")
        get_filename_component(stage "${shader}" LAST_EXT)
        string(SUBSTRING "${stage}" 1 -1 stage)
        add_custom_command(OUTPUT "${output}.glsl" "${output}.spv" "${output}.json" "${output}.h" "${output}.gnm.spv" "${output}.package.json"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
            COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.0 "${source}" -o "${output}.spv"
            COMMAND $<TARGET_FILE:spirv-val> --target-env vulkan1.0 "${output}.spv"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/prepare_gnm_shader.py" "${source}" "${output}.gnm.glsl"
            COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.0 -S "${stage}" "${output}.gnm.glsl" -o "${output}.gnm.spv"
            COMMAND $<TARGET_FILE:spirv-val> --target-env vulkan1.0 "${output}.gnm.spv"
            COMMAND $<TARGET_FILE:spirv-cross> "${output}.spv" --version 410 --no-es --no-420pack-extension --output "${output}.glsl"
            COMMAND $<TARGET_FILE:spirv-cross> "${output}.spv" --reflect --output "${output}.json"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/shader_metadata.py"
                "${output}.json" "${output}.spv" "${output}.h" "${source}"
            DEPENDS "${source}" glslang-standalone spirv-val spirv-cross
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/shader_metadata.py"
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/prepare_gnm_shader.py"
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../deps/toolchain-lock.json"
            VERBATIM COMMENT "Compiling and validating ${shader}")
        list(APPEND outputs "${output}.glsl" "${output}.spv" "${output}.json" "${output}.h" "${output}.gnm.spv" "${output}.package.json")
    endforeach()
    if(KSTAR_GFX_BACKEND STREQUAL "GNM")
        if(NOT KSTAR_GFX_PSBC_EXECUTABLE OR NOT EXISTS "${KSTAR_GFX_PSBC_EXECUTABLE}")
            message(FATAL_ERROR "Set KSTAR_GFX_PSBC_EXECUTABLE to the host compiler built from deps/opengnm-psbc")
        endif()
        foreach(shader IN LISTS ARG_SOURCES)
            if(shader MATCHES "\\.vert$")
                set(stage vertex)
            else()
                set(stage fragment)
            endif()
            set(output "${output_dir}/${shader}")
            add_custom_command(OUTPUT "${output}.sb"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/package_gnm_shader.py"
                    --psbc "${KSTAR_GFX_PSBC_EXECUTABLE}" --stage "${stage}" --input "${output}.gnm.spv" --output "${output}.sb"
                DEPENDS "${output}.gnm.spv" "${KSTAR_GFX_PSBC_EXECUTABLE}"
                    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/package_gnm_shader.py"
                    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/validate_gnm_shader.py" VERBATIM)
            list(APPEND outputs "${output}.sb")
        endforeach()
    endif()
    add_custom_target(${target} DEPENDS ${outputs})
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_test(NAME ${target}_abi COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/test_shaders.py" "${output_dir}" --cc "${CMAKE_C_COMPILER}")
    endif()
    set(${output_variable} "${outputs}" PARENT_SCOPE)
endfunction()

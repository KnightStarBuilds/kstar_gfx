cmake_minimum_required(VERSION 3.24)
project(kstar_gfx VERSION 0.1.0 LANGUAGES C CXX)
if(NOT CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
    message(FATAL_ERROR "Use -G 'Ninja Multi-Config' and --config Debug|Release")
endif()
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Build configurations" FORCE)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(KSTAR_GFX_BACKEND "OPENGL" CACHE STRING "Compile-time backend: OPENGL or GNM")
set_property(CACHE KSTAR_GFX_BACKEND PROPERTY STRINGS OPENGL GNM)
option(KSTAR_GFX_BUILD_DEMOS "Build standalone graphics demos" ${PROJECT_IS_TOP_LEVEL})
option(KSTAR_GFX_BUILD_TESTS "Build API and GPU conformance tests" ${PROJECT_IS_TOP_LEVEL})
option(KSTAR_GFX_BUILD_TOOLS "Build offline shader tools without demo assets" OFF)
option(KSTAR_GFX_OFFLINE "Require cached, verified dependencies and assets" OFF)
option(KSTAR_GFX_LTO "Enable release interprocedural optimization" ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(kstar_gfx STATIC)
add_library(kstar_gfx::kstar_gfx ALIAS kstar_gfx)
add_library(kstar_gfx::gfx ALIAS kstar_gfx)
set_target_properties(kstar_gfx PROPERTIES EXPORT_NAME gfx)
target_include_directories(kstar_gfx PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(kstar_gfx PRIVATE cxx_std_23)
if(KSTAR_GFX_BACKEND STREQUAL "OPENGL")
    target_sources(kstar_gfx PRIVATE src/opengl/backend.cpp)
    target_compile_definitions(kstar_gfx PUBLIC KSTAR_GFX_BACKEND_OPENGL=1)
elseif(KSTAR_GFX_BACKEND STREQUAL "GNM")
    include(cmake/Gnm.cmake)
    kgfx_configure_gnm(kstar_gfx)
    target_compile_definitions(kstar_gfx PUBLIC KSTAR_GFX_BACKEND_GNM=1)
else()
    message(FATAL_ERROR "KSTAR_GFX_BACKEND must be OPENGL or GNM")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(kstar_gfx PRIVATE -Wall -Wextra -Wpedantic -fwrapv -fno-strict-aliasing)
elseif(MSVC)
    target_compile_options(kstar_gfx PRIVATE /W4 /permissive-)
endif()
if(KSTAR_GFX_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_property(TARGET kstar_gfx PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        if(KSTAR_GFX_BACKEND STREQUAL "GNM")
            set_property(TARGET opengnm PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    else()
        message(STATUS "IPO unavailable: ${ipo_error}")
    endif()
endif()

if(KSTAR_GFX_BUILD_TESTS)
    enable_testing()
    set(BUILD_TESTING ON)
    add_executable(kgfx_c_api tests/c_api.c)
    target_link_libraries(kgfx_c_api PRIVATE kstar_gfx)
    set_target_properties(kgfx_c_api PROPERTIES LINKER_LANGUAGE CXX)
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME kgfx_c_api COMMAND kgfx_c_api)
        add_executable(kgfx_pad_history tests/pad_history_test.cpp)
        add_test(NAME kgfx_pad_history COMMAND kgfx_pad_history)
    endif()
endif()

if(KSTAR_GFX_BACKEND STREQUAL "OPENGL" AND (KSTAR_GFX_BUILD_DEMOS OR KSTAR_GFX_BUILD_TESTS))
    include(cmake/Dependencies.cmake)
    kgfx_add_sdl()
endif()
if(KSTAR_GFX_BUILD_TESTS AND KSTAR_GFX_BACKEND STREQUAL "OPENGL")
    add_executable(kgfx_conformance tests/conformance.cpp)
    target_link_libraries(kgfx_conformance PRIVATE kstar_gfx SDL2::SDL2-static ${CMAKE_DL_LIBS})
    add_executable(kgfx_dispatch_benchmark tests/dispatch_benchmark.cpp)
    target_link_libraries(kgfx_dispatch_benchmark PRIVATE kstar_gfx SDL2::SDL2-static)
    add_test(NAME kgfx_gpu_conformance COMMAND kgfx_conformance)
    set_tests_properties(kgfx_gpu_conformance PROPERTIES LABELS "gpu" TIMEOUT 60)
endif()
if(KSTAR_GFX_BUILD_TOOLS AND NOT CMAKE_CROSSCOMPILING)
    include(cmake/Shaders.cmake)
    kgfx_add_shader_tools()
endif()

if(KSTAR_GFX_BUILD_DEMOS AND KSTAR_GFX_BACKEND STREQUAL "OPENGL")
    include(cmake/Assets.cmake)
    include(cmake/Shaders.cmake)
    kgfx_prepare_assets(cooked_assets)
    set(shader_dir "${CMAKE_BINARY_DIR}/resources/$<CONFIG>/shaders")
    kgfx_compile_shaders(kgfx_shaders "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${shader_dir}" shader_outputs
        SOURCES triangle.vert triangle.frag scene.vert scene.frag shadow.vert shadow.frag
            fullscreen.vert environment.frag tonemap.frag present.frag)
    add_executable(kstar_gfx_demo src/demos/desktop_main.cpp src/demos/demo.cpp src/demos/assets.cpp)
    target_include_directories(kstar_gfx_demo PRIVATE tools/include "${shader_dir}")
    target_link_libraries(kstar_gfx_demo PRIVATE kstar_gfx SDL2::SDL2-static)
    add_dependencies(kstar_gfx_demo kgfx_shaders kgfx_assets)
    # Refresh independently of relinking: shader/asset-only edits must reach demos.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(kgfx_demo_resources
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/package_resources.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}" --assets "${cooked_assets}"
            --shaders "${shader_dir}" --destination "$<TARGET_FILE_DIR:kstar_gfx_demo>/resources"
        DEPENDS kgfx_assets kgfx_shaders
        VERBATIM)
    add_dependencies(kstar_gfx_demo kgfx_demo_resources)
endif()

if(KSTAR_GFX_BACKEND STREQUAL "GNM")
    include(cmake/GnmDemos.cmake)
endif()

# Consumers control their own IPO setting; demos/tests enable it with the library
# so direct C entry points can inline across translation units in Release.
if(KSTAR_GFX_LTO AND ipo_supported)
    foreach(target kstar_gfx_demo kgfx_gnm_probe kgfx_c_api kgfx_conformance kgfx_dispatch_benchmark)
        if(TARGET ${target})
            set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    endforeach()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
configure_file(cmake/kstar_gfxConfig.cmake "${CMAKE_CURRENT_BINARY_DIR}/kstar_gfxConfig.cmake" @ONLY)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/kstar_gfxConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS kstar_gfx EXPORT kstar_gfxTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT kstar_gfxTargets NAMESPACE kstar_gfx:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kstar_gfx)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/kstar_gfxConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/kstar_gfxConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kstar_gfx)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/kstar_gfx)

if(KSTAR_GFX_BUILD_TESTS AND TARGET kstar_gfx_demo AND KSTAR_GFX_BACKEND STREQUAL "OPENGL")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME kgfx_capture_comparison
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_comparison_test.py")
    add_test(NAME kgfx_gnm_demo_runner
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/gnm_demo_runner_test.py")
    set_tests_properties(kgfx_gnm_demo_runner PROPERTIES TIMEOUT 30)
    foreach(scene triangle cubes instances offscreen stress pbr)
        add_test(NAME kgfx_demo_${scene}
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/demo_capture_test.py"
                $<TARGET_FILE:kstar_gfx_demo> --scene ${scene}
                --scratch "${CMAKE_BINARY_DIR}/test-captures/$<CONFIG>")
        set_tests_properties(kgfx_demo_${scene} PROPERTIES LABELS "gpu;demo" TIMEOUT 180 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME kgfx_demo_pbr_night
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/demo_capture_test.py"
            $<TARGET_FILE:kstar_gfx_demo> --scene pbr --environment 1
            --scratch "${CMAKE_BINARY_DIR}/test-captures/$<CONFIG>")
    set_tests_properties(kgfx_demo_pbr_night PROPERTIES LABELS "gpu;demo" TIMEOUT 180 RUN_SERIAL TRUE)
    foreach(view rifle hat)
        foreach(environment 0 1)
            add_test(NAME kgfx_demo_pbr_${view}_${environment}
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/demo_capture_test.py"
                    $<TARGET_FILE:kstar_gfx_demo> --scene pbr --view ${view} --environment ${environment}
                    --scratch "${CMAKE_BINARY_DIR}/test-captures/$<CONFIG>")
            set_tests_properties(kgfx_demo_pbr_${view}_${environment} PROPERTIES LABELS "gpu;demo" TIMEOUT 180 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
endif()

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
# Emission accumulators cross integer thresholds: ARM fused multiply-add must
# round the same way as the x86 guest's separate operations at fixed ticks.
if(MSVC)
    set_source_files_properties(src/demos/showcase_world.cpp src/demos/lighting_cpu.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(src/demos/showcase_world.cpp src/demos/lighting_cpu.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
set(KSTAR_GFX_BACKEND "OPENGL" CACHE STRING "Compile-time backend: OPENGL or GNM")
set_property(CACHE KSTAR_GFX_BACKEND PROPERTY STRINGS OPENGL GNM)
option(KSTAR_GFX_BUILD_DEMOS "Build standalone graphics demos" ${PROJECT_IS_TOP_LEVEL})
option(KSTAR_GFX_BUILD_TESTS "Build API and GPU conformance tests" ${PROJECT_IS_TOP_LEVEL})
option(KSTAR_GFX_BUILD_TOOLS "Build offline shader tools without demo assets" OFF)
option(KSTAR_GFX_OFFLINE "Require cached, verified dependencies and assets" OFF)
option(KSTAR_GFX_LTO "Enable release interprocedural optimization" ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(KSTAR_GFX_BUILD_DEMOS)
    include(cmake/SceneCatalog.cmake)
endif()

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
        add_executable(kgfx_graphics_settings tests/graphics_settings_test.cpp)
        add_test(NAME kgfx_graphics_settings COMMAND kgfx_graphics_settings)
        add_executable(kgfx_shadow_cache tests/shadow_cache_test.cpp)
        add_test(NAME kgfx_shadow_cache COMMAND kgfx_shadow_cache)
        add_executable(kgfx_water_surface tests/water_surface_test.cpp)
        add_test(NAME kgfx_water_surface COMMAND kgfx_water_surface)
        add_executable(kgfx_stream_geometry tests/stream_geometry_test.cpp)
        add_test(NAME kgfx_stream_geometry COMMAND kgfx_stream_geometry)
        add_executable(kgfx_showcase_world tests/showcase_world_test.cpp src/demos/showcase_world.cpp src/demos/lighting_cpu.cpp)
        add_test(NAME kgfx_showcase_world COMMAND kgfx_showcase_world)
        add_executable(kgfx_lighting_cpu tests/lighting_cpu_test.cpp src/demos/showcase_world.cpp src/demos/lighting_cpu.cpp)
        add_test(NAME kgfx_lighting_cpu COMMAND kgfx_lighting_cpu "${CMAKE_CURRENT_SOURCE_DIR}/.tmp/lighting-tests")
    endif()
endif()

if(KSTAR_GFX_BACKEND STREQUAL "OPENGL" AND (KSTAR_GFX_BUILD_DEMOS OR KSTAR_GFX_BUILD_TESTS))
    include(cmake/Dependencies.cmake)
    kgfx_add_sdl()
endif()
if(KSTAR_GFX_BUILD_TESTS AND KSTAR_GFX_BACKEND STREQUAL "OPENGL")
    add_executable(kgfx_gpu_timing tests/gpu_timing_test.cpp)
    target_link_libraries(kgfx_gpu_timing PRIVATE kstar_gfx)
    add_test(NAME kgfx_gpu_timing COMMAND kgfx_gpu_timing)
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
    file(GLOB showcase_shaders CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/showcase*.vert" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/showcase*.frag")
    kgfx_compile_shaders(kgfx_shaders "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${shader_dir}" shader_outputs
        SOURCES triangle.vert triangle.frag scene.vert scene.frag shadow.vert shadow.frag
            fullscreen.vert environment.frag tonemap.frag present.frag ${showcase_shaders})
    add_executable(kstar_gfx_demo src/demos/desktop_main.cpp src/demos/demo.cpp src/demos/assets.cpp
        src/demos/showcase_world.cpp src/demos/lighting_cpu.cpp src/demos/showcase_renderer.cpp)
    target_include_directories(kstar_gfx_demo PRIVATE tools/include "${shader_dir}" "${KGFX_SCENE_INCLUDE_DIR}")
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
    add_test(NAME kgfx_demo_options COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/demo_options_test.py" "$<TARGET_FILE:kstar_gfx_demo>")
    add_executable(kgfx_demo_session tests/demo_session_test.cpp src/demos/demo.cpp src/demos/assets.cpp
        src/demos/showcase_world.cpp src/demos/lighting_cpu.cpp src/demos/showcase_renderer.cpp)
    target_include_directories(kgfx_demo_session PRIVATE tools/include "${shader_dir}" "${KGFX_SCENE_INCLUDE_DIR}")
    target_link_libraries(kgfx_demo_session PRIVATE kstar_gfx SDL2::SDL2-static)
    add_dependencies(kgfx_demo_session kgfx_demo_resources)
    add_test(NAME kgfx_demo_session COMMAND kgfx_demo_session "$<TARGET_FILE_DIR:kstar_gfx_demo>/resources"
        "${CMAKE_BINARY_DIR}/test-captures/$<CONFIG>/sessions")
    set_tests_properties(kgfx_demo_session PROPERTIES LABELS "gpu;demo" TIMEOUT 300 RUN_SERIAL TRUE)
    add_executable(kgfx_showcase_renderer_test tests/showcase_renderer_test.cpp src/demos/lighting_cpu.cpp src/demos/showcase_renderer.cpp src/demos/assets.cpp)
    target_include_directories(kgfx_showcase_renderer_test PRIVATE src/demos tools/include "${shader_dir}")
    target_link_libraries(kgfx_showcase_renderer_test PRIVATE kstar_gfx SDL2::SDL2-static)
    add_dependencies(kgfx_showcase_renderer_test kgfx_demo_resources)
    add_test(NAME kgfx_showcase_renderer COMMAND kgfx_showcase_renderer_test "$<TARGET_FILE_DIR:kstar_gfx_demo>/resources")
    set_tests_properties(kgfx_showcase_renderer PROPERTIES LABELS "gpu;showcase" TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(NAME kgfx_capture_comparison
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_comparison_test.py")
    add_test(NAME kgfx_gnm_demo_runner
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/gnm_demo_runner_test.py")
    set_tests_properties(kgfx_gnm_demo_runner PROPERTIES TIMEOUT 30)
    foreach(scene IN LISTS KGFX_LEGACY_SCENES)
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
    foreach(scene IN LISTS KGFX_SHOWCASE_SCENES)
        add_test(NAME kgfx_showcase_${scene}
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/showcase_capture_test.py"
                $<TARGET_FILE:kstar_gfx_demo> --scene ${scene} --samples 1
                --scratch "${CMAKE_BINARY_DIR}/test-captures/$<CONFIG>")
        set_tests_properties(kgfx_showcase_${scene} PROPERTIES LABELS "gpu;showcase" TIMEOUT 900 RUN_SERIAL TRUE)
    endforeach()
    foreach(scene waterworks boiler_room)
        add_test(NAME kgfx_showcase_${scene}_msaa4
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/showcase_capture_test.py"
                $<TARGET_FILE:kstar_gfx_demo> --scene ${scene} --samples 4 --environment 1
                --scratch "${CMAKE_BINARY_DIR}/test-captures/$<CONFIG>")
        set_tests_properties(kgfx_showcase_${scene}_msaa4 PROPERTIES LABELS "gpu;showcase" TIMEOUT 900 RUN_SERIAL TRUE)
    endforeach()
    foreach(scene glass_lab waterworks)
        foreach(samples 1 4)
            add_test(NAME kgfx_showcase_${scene}_diagnostics_msaa${samples}
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/showcase_capture_test.py"
                    $<TARGET_FILE:kstar_gfx_demo> --scene ${scene} --samples ${samples} --diagnostics
                    --scratch "${CMAKE_BINARY_DIR}/test-captures/$<CONFIG>")
            set_tests_properties(kgfx_showcase_${scene}_diagnostics_msaa${samples}
                PROPERTIES LABELS "gpu;showcase" TIMEOUT 240 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
endif()

# The demo's two persistent GI workers are independent of the C rendering ABI.
if(NOT CMAKE_CROSSCOMPILING AND (KSTAR_GFX_BUILD_DEMOS OR KSTAR_GFX_BUILD_TESTS))
    find_package(Threads REQUIRED)
    foreach(target kstar_gfx_demo kgfx_demo_session kgfx_showcase_world kgfx_lighting_cpu kgfx_showcase_renderer_test kgfx_lighting_cooker)
        if(TARGET ${target})
            target_link_libraries(${target} PRIVATE Threads::Threads)
        endif()
    endforeach()
endif()

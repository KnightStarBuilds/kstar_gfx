function(kgfx_ps4_package target)
  if(CMAKE_HOST_APPLE)
    set(converter "${OO_PS4_TOOLCHAIN}/bin/macos/create-fself-macos")
  elseif(CMAKE_HOST_WIN32)
    set(converter "${OO_PS4_TOOLCHAIN}/bin/windows/create-fself.exe")
  else()
    set(converter "${OO_PS4_TOOLCHAIN}/bin/linux/create-fself")
  endif()
  set(stage "${CMAKE_BINARY_DIR}/app0/$<CONFIG>/${target}")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${stage}/sce_module"
    COMMAND "${CMAKE_COMMAND}" -E env "OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN}"
      "${converter}" "-in=$<TARGET_FILE:${target}>" "-out=${stage}/${target}.oelf"
      --eboot "${stage}/eboot.bin" --paid 0x3800000000000011
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${OO_PS4_TOOLCHAIN}/samples/hello_world/sce_module/libc.prx" "${stage}/sce_module/libc.prx"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${OO_PS4_TOOLCHAIN}/samples/hello_world/sce_module/libSceFios2.prx" "${stage}/sce_module/libSceFios2.prx"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${stage}/notices"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${stage}/notices/kstar_gfx-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_BINARY_DIR}/deps-src/opengnm/LICENSE" "${stage}/notices/OpenGNM-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${OO_PS4_TOOLCHAIN}/LICENSE" "${stage}/notices/OpenOrbis-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/src/gnm/sdk-include/COPYRIGHT.musl" "${stage}/notices/musl-COPYRIGHT.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/sdk-compat.json" "${stage}/notices/sdk-compat.json"
    VERBATIM)
endfunction()
if(KSTAR_GFX_BUILD_TESTS OR KSTAR_GFX_BUILD_DEMOS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()
if(KSTAR_GFX_BUILD_TESTS)
  add_executable(kgfx_gnm_probe tests/gnm/probe.cpp src/gnm/demo_runtime.cpp)
  target_link_libraries(kgfx_gnm_probe PRIVATE kstar_gfx SceSystemService)
  kgfx_ps4_package(kgfx_gnm_probe)
  set(KSTAR_GFX_PROBE_SHADERS "" CACHE PATH "Offline-compiled GNM conformance shaders")
  if(IS_DIRECTORY "${KSTAR_GFX_PROBE_SHADERS}")
    add_custom_target(kgfx_gnm_probe_resources
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync_directory.py"
        "${KSTAR_GFX_PROBE_SHADERS}" "${CMAKE_BINARY_DIR}/app0/$<CONFIG>/kgfx_gnm_probe"
      VERBATIM)
    add_dependencies(kgfx_gnm_probe kgfx_gnm_probe_resources)
  endif()
endif()
if(KSTAR_GFX_BUILD_DEMOS)
  set(KSTAR_GFX_HOST_RESOURCES "" CACHE PATH "Prepared host resources with compiled GNM shaders")
  if(NOT IS_DIRECTORY "${KSTAR_GFX_HOST_RESOURCES}")
    message(FATAL_ERROR "GNM demos need KSTAR_GFX_HOST_RESOURCES; cook assets and compile shaders on the host first")
  endif()
  add_executable(kstar_gfx_demo src/gnm/demo_main.cpp src/gnm/demo_runtime.cpp src/demos/demo.cpp src/demos/assets.cpp)
  target_include_directories(kstar_gfx_demo PRIVATE tools/include)
  target_link_libraries(kstar_gfx_demo PRIVATE kstar_gfx SceSystemService ScePad SceUserService)
  kgfx_ps4_package(kstar_gfx_demo)
  add_custom_target(kgfx_gnm_demo_resources
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync_directory.py"
      "${KSTAR_GFX_HOST_RESOURCES}" "${CMAKE_BINARY_DIR}/app0/$<CONFIG>/kstar_gfx_demo/resources" VERBATIM)
  add_dependencies(kstar_gfx_demo kgfx_gnm_demo_resources)
endif()

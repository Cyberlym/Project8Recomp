cmake_minimum_required(VERSION 3.25)
project(Project8AndroidProbe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID OR NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "The probe requires the Android arm64-v8a toolchain")
endif()

set(REXGLUE_SDK_ROOT "" CACHE PATH "Patched ReXGlue SDK root")
if(NOT EXISTS "${REXGLUE_SDK_ROOT}/include/rex/ui/surface_android.h")
  message(FATAL_ERROR "REXGLUE_SDK_ROOT must point to the patched ReXGlue SDK")
endif()

set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_FIDELITYFX OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
# rexgpu-xenos and the app share one perf registry through rexruntime. Keeping
# this enabled puts counter.cpp in the linked runtime DSO instead of compiling
# a private copy into libmain, which a runtime-loaded plugin cannot import.
set(REXGLUE_ENABLE_PERF_COUNTERS ON CACHE BOOL "" FORCE)
set(REXGLUE_USE_D3D12 OFF CACHE BOOL "" FORCE)
set(REXGLUE_USE_VULKAN ON CACHE BOOL "" FORCE)
add_subdirectory("${REXGLUE_SDK_ROOT}" rexglue-sdk EXCLUDE_FROM_ALL)

# Codegen output belongs to the owner's private build cache. Keep the path a
# cache setting so this project never embeds generated game code or a user's
# content location. Stage 8D only compiles and links these objects; it does not
# create a game runtime or invoke any guest entrypoint.
set(P8_GENERATED_DIR "" CACHE PATH
    "Private directory containing generated Project 8 C++ sources")

if(P8_GENERATED_DIR)
  file(REAL_PATH "${P8_GENERATED_DIR}" P8_GENERATED_DIR)
  foreach(required_file
      sources.cmake
      thps_p8_pch.h
      thps_p8_funcs.h
      thps_p8_init.h
      thps_p8_init.cpp
      thps_p8_register.cpp
      codegen.partition.json
      codegen.stamp)
    if(NOT EXISTS "${P8_GENERATED_DIR}/${required_file}")
      message(FATAL_ERROR "P8_GENERATED_DIR is missing ${required_file}")
    endif()
  endforeach()

  include("${P8_GENERATED_DIR}/sources.cmake")
  file(GLOB P8_RECOMP_UNITS "${P8_GENERATED_DIR}/thps_p8_recomp.*.cpp")
  list(LENGTH GENERATED_SOURCES P8_GENERATED_SOURCE_COUNT)
  list(LENGTH P8_RECOMP_UNITS P8_RECOMP_UNIT_COUNT)
  math(EXPR P8_EXPECTED_GENERATED_SOURCE_COUNT "${P8_RECOMP_UNIT_COUNT} + 2")
  if(P8_RECOMP_UNIT_COUNT EQUAL 0 OR
     NOT P8_GENERATED_SOURCE_COUNT EQUAL P8_EXPECTED_GENERATED_SOURCE_COUNT)
    message(FATAL_ERROR "P8_GENERATED_DIR source list does not match its recompilation units")
  endif()
  foreach(generated_source IN LISTS GENERATED_SOURCES)
    if(NOT EXISTS "${generated_source}")
      message(FATAL_ERROR "P8_GENERATED_DIR references missing source ${generated_source}")
    endif()
  endforeach()

  add_library(thps_p8_recomp OBJECT ${GENERATED_SOURCES})
  target_include_directories(thps_p8_recomp PRIVATE
    "${P8_GENERATED_DIR}"
    "${REXGLUE_SDK_ROOT}/src"
  )
  target_link_libraries(thps_p8_recomp PRIVATE rex::runtime libucontext)
  rexglue_apply_target_settings(thps_p8_recomp)
  target_precompile_headers(thps_p8_recomp PRIVATE
    "${P8_GENERATED_DIR}/thps_p8_pch.h")
endif()

add_library(main SHARED
  probe.cpp
  fiber_test.cpp
  android_preflight.cpp
  android_fd_game_image_reader.cpp
  android_xdvdfs_device.cpp
  android_game_session.cpp
  android_game_install.cpp
  android_gameplay_log.cpp
  android_game_app.cpp
  "${REXGLUE_SDK_ROOT}/src/ui/rex_app.cpp"
  game_image_jni.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/game_image_reader.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/xdvdfs_reader.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/xex_identity.cpp"
)
set_target_properties(main PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_include_directories(main PRIVATE
  "${REXGLUE_SDK_ROOT}/src"
  "${REXGLUE_SDK_ROOT}/thirdparty/imgui"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/game/src"
)
if(P8_GENERATED_DIR)
  target_include_directories(main PRIVATE "${P8_GENERATED_DIR}")
endif()
target_compile_definitions(main PRIVATE REXGLUE_ANDROID_PRESENTATION_DIAGNOSTICS=1)
target_compile_definitions(main PRIVATE REXGLUE_BUILD_CONFIG="$<CONFIG>")
target_compile_definitions(rexui PRIVATE REXGLUE_ANDROID_PRESENTATION_DIAGNOSTICS=1)
target_link_libraries(main PRIVATE rexruntime renderdoc libucontext log android)
# The GPU plugin is runtime-loaded rather than linked. Building main must still
# produce it so Gradle can package the exact ABI-matched plugin for Android.
add_dependencies(main rexgpu-xenos)
if(TARGET thps_p8_recomp)
  target_sources(main PRIVATE $<TARGET_OBJECTS:thps_p8_recomp>)
endif()

# A standalone backend test keeps fiber validation independent from the
# monolithic rexcore target. It compiles the same implementation that rexcore
# uses and can be pushed to an Android device for the runtime checks.
add_executable(rex_fiber_android_test
  fiber_test_main.cpp
  fiber_test.cpp
  "${REXGLUE_SDK_ROOT}/src/core/fiber_posix.cpp"
)
target_include_directories(rex_fiber_android_test PRIVATE
  "${REXGLUE_SDK_ROOT}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/rexglue-sdk/include"
)
target_link_libraries(rex_fiber_android_test PRIVATE libucontext)

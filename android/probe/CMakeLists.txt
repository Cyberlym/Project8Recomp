cmake_minimum_required(VERSION 3.25)
project(Project8AndroidProbe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID OR NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "The probe requires the Android arm64-v8a toolchain")
endif()

set(SDL3_AAR_ROOT "" CACHE PATH "Extracted SDL3 AAR root")
if(NOT EXISTS "${SDL3_AAR_ROOT}/prefab/modules/SDL3-Headers/include/SDL3/SDL.h")
  message(FATAL_ERROR "SDL3_AAR_ROOT must point to an extracted SDL3 AAR")
endif()

add_library(SDL3::Headers INTERFACE IMPORTED)
set_target_properties(SDL3::Headers PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES
    "${SDL3_AAR_ROOT}/prefab/modules/SDL3-Headers/include")

add_library(SDL3::SDL3 SHARED IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES
  IMPORTED_LOCATION
    "${SDL3_AAR_ROOT}/prefab/modules/SDL3-shared/libs/android.arm64-v8a/libSDL3.so"
  INTERFACE_INCLUDE_DIRECTORIES
    "${SDL3_AAR_ROOT}/prefab/modules/SDL3-Headers/include"
  INTERFACE_LINK_LIBRARIES "SDL3::Headers")

find_library(VULKAN_LIBRARY vulkan REQUIRED)

add_library(main SHARED probe.cpp)
target_compile_definitions(main PRIVATE SDL_MAIN_NEEDED=1)
target_link_libraries(main PRIVATE SDL3::SDL3 ${VULKAN_LIBRARY} log android)

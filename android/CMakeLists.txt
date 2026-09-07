# Minimal Android configuration scaffold for Project8Recomp.
#
# This file intentionally declares no game target, generated source, asset, or
# runtime dependency. It records the first supported ABI/API contract so the
# later SDL/Vulkan probe cannot silently select a desktop configuration.

cmake_minimum_required(VERSION 3.25)
project(Project8RecompAndroid LANGUAGES C CXX)

if(NOT ANDROID)
  message(FATAL_ERROR "android/CMakeLists.txt requires the Android NDK toolchain")
endif()

if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "Only arm64-v8a is supported by the initial Android port")
endif()

if(NOT ANDROID_PLATFORM)
  set(ANDROID_PLATFORM android-26 CACHE STRING "Minimum Android API level" FORCE)
endif()

if(ANDROID_PLATFORM VERSION_LESS 26)
  message(FATAL_ERROR "The initial Android port requires Android API 26 or newer")
endif()

find_package(SDL3 3.2 CONFIG REQUIRED)
find_library(ANDROID_VULKAN vulkan REQUIRED)

# Targets are added only after the SDLActivity/native-window contract is
# implemented. Keeping this interface target empty prevents accidental codegen
# or game-content dependencies during the scaffold stage.
add_library(project8_android_contract INTERFACE)
target_link_libraries(project8_android_contract INTERFACE SDL3::SDL3 ${ANDROID_VULKAN})

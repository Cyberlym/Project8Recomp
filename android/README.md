# Android port scaffold

This directory records the initial target contract only: Android NDK CMake,
`arm64-v8a`, minimum API 26, SDL3, and Vulkan. It does not contain an Activity,
JNI bridge, game target, assets, XISO reader, `default.xex`, codegen output, or
recompiled translation units.

The first later probe should add a tiny SDL3 native-window/Vulkan surface target
without loading the game runtime. The Android toolchain must be supplied by the
caller, for example with `android.toolchain.cmake`, `-DANDROID_ABI=arm64-v8a`,
and `-DANDROID_PLATFORM=android-26`. No build is run by this scaffold change.

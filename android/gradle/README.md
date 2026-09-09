# Android Gradle packaging layer

This is the Stage 5.5A Android packaging layer. It preserves the existing
`ProbeActivity`, manifest and native ABI, and has no `externalNativeBuild`,
CMake or Ninja integration.

Pinned tooling: JDK 17.0.12+7, Gradle 8.7 and Android Gradle Plugin 8.5.2.
The local Android SDK must already provide platform and Build Tools 35.0.0.

It stages only these already-built ARM64 libraries when an APK task is run:
`libmain.so`, `librexruntime.so` and `libSDL3.so`. Their locations default to
the existing project build cache and can be overridden with Gradle properties
`project8BuildCache`, `project8RexGlueRoot` and `project8NativeBuildRoot`.

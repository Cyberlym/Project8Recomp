#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sdk_root="${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT to the installed SDK}"
ndk_root="${ANDROID_NDK_ROOT:-${sdk_root}/ndk/27.2.12479018}"
rexglue_sdk_root="${REXGLUE_SDK_ROOT:?Set REXGLUE_SDK_ROOT to the patched ReXGlue SDK}"
build_root="${ANDROID_PROBE_BUILD_DIR:-/tmp/project8-android-probe-build}"
app_root="${build_root}/app"
sdl_java_root="${rexglue_sdk_root}/thirdparty/sdl3/android-project/app/src/main/java"
sdl_native_lib="${rexglue_sdk_root}/out/android-arm64/libSDL3.so"
rexruntime_native_lib="${rexglue_sdk_root}/out/android-arm64/librexruntime.so"

mkdir -p "${app_root}/classes" "${app_root}/dex" "${app_root}/res"

cmake -S "${repo_root}/android/probe" -B "${build_root}/native" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${ndk_root}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release \
  -DREXGLUE_SDK_ROOT="${rexglue_sdk_root}"
cmake --build "${build_root}/native" --parallel 2

android_jar="${sdk_root}/platforms/android-35/android.jar"
build_tools="${sdk_root}/build-tools/35.0.0"
javac -source 8 -target 8 -classpath "${android_jar}" \
  -d "${app_root}/classes" \
  "${sdl_java_root}"/org/libsdl/app/*.java \
  "${repo_root}/android/probe/app/src/main/java/com/cyberlym/project8probe/ProbeActivity.java"

jar --create --file "${app_root}/probe-classes.jar" -C "${app_root}/classes" .

"${build_tools}/d8" --min-api 26 --lib "${android_jar}" \
  --output "${app_root}/dex" "${app_root}/probe-classes.jar"

"${build_tools}/aapt2" link -I "${android_jar}" \
  --manifest "${repo_root}/android/probe/app/src/main/AndroidManifest.xml" \
  -o "${app_root}/unsigned.apk"

mkdir -p "${app_root}/zip/lib/arm64-v8a"
cp "${build_root}/native/libmain.so" "${app_root}/zip/lib/arm64-v8a/"
cp "${sdl_native_lib}" "${app_root}/zip/lib/arm64-v8a/"
cp "${rexruntime_native_lib}" "${app_root}/zip/lib/arm64-v8a/"
cp "${app_root}/dex/classes.dex" "${app_root}/zip/"
(cd "${app_root}/zip" && zip -q -r "${app_root}/unsigned-with-native.apk" .)
cat "${app_root}/unsigned.apk" > "${app_root}/merged.apk"
(cd "${app_root}/zip" && zip -q -r "${app_root}/merged.apk" .)
"${build_tools}/zipalign" -f 4 "${app_root}/merged.apk" "${app_root}/aligned.apk"

keystore="${build_root}/debug.keystore"
if [[ ! -f "${keystore}" ]]; then
  keytool -genkeypair -keystore "${keystore}" -storepass android -alias androiddebugkey \
    -keypass android -dname 'CN=Android Debug,O=Android,C=US' -keyalg RSA -validity 10000
fi
"${build_tools}/apksigner" sign --ks "${keystore}" --ks-pass pass:android \
  --ks-key-alias androiddebugkey --key-pass pass:android \
  --out "${repo_root}/android/Project8VulkanProbe-arm64-v8a.apk" "${app_root}/aligned.apk"
"${build_tools}/apksigner" verify --verbose "${repo_root}/android/Project8VulkanProbe-arm64-v8a.apk"
printf 'APK=%s\n' "${repo_root}/android/Project8VulkanProbe-arm64-v8a.apk"

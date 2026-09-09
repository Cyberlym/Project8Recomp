#pragma once

#include <jni.h>

namespace thps::android {

// The Activity owns the private app-files stream; native code synchronously
// hands it each event so a crash cannot strand gameplay diagnostics in logcat.
void InitializeGameplayLogBridge(JNIEnv* env, jclass activity_class);
void LogGameplayEvent(const char* format, ...);

}  // namespace thps::android

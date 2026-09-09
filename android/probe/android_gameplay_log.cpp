#include "android_gameplay_log.h"

#include <android/log.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace thps::android {
namespace {

std::mutex g_bridge_mutex;
JavaVM* g_vm = nullptr;
jclass g_activity_class = nullptr;
jmethodID g_append_method = nullptr;

}  // namespace

void InitializeGameplayLogBridge(JNIEnv* env, jclass activity_class) {
  std::lock_guard<std::mutex> lock(g_bridge_mutex);
  env->GetJavaVM(&g_vm);
  if (g_activity_class) env->DeleteGlobalRef(g_activity_class);
  g_activity_class = static_cast<jclass>(env->NewGlobalRef(activity_class));
  g_append_method = g_activity_class
      ? env->GetStaticMethodID(g_activity_class, "appendNativeGameplayLog",
                               "(Ljava/lang/String;)V")
      : nullptr;
}

void LogGameplayEvent(const char* format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  __android_log_print(ANDROID_LOG_INFO, "Project8Game", "%s", message);

  std::lock_guard<std::mutex> lock(g_bridge_mutex);
  if (!g_vm || !g_activity_class || !g_append_method) return;
  JNIEnv* env = nullptr;
  bool attached = false;
  if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    attached = true;
  }
  jstring line = env->NewStringUTF(message);
  if (line) {
    env->CallStaticVoidMethod(g_activity_class, g_append_method, line);
    env->DeleteLocalRef(line);
  }
  if (env->ExceptionCheck()) env->ExceptionClear();
  if (attached) g_vm->DetachCurrentThread();
}

}  // namespace thps::android

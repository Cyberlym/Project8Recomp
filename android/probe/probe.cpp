#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <jni.h>

#include <rex/ui/vulkan/instance.h>
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context_sdl.h>
#include <rex/runtime.h>

#include "ui/android_presentation_diagnostics.h"

#include "fiber_test.h"
#include "android_preflight.h"

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

extern "C" void rexglue_android_presentation_diagnostic(
    rex::ui::AndroidPresentationDiagnosticEvent event, int32_t result,
    uintptr_t handle, uint32_t width, uint32_t height);

namespace {

FILE* g_log = nullptr;
std::string g_log_path;
std::mutex g_state_mutex;
std::string g_report;
std::string g_error;

bool g_surface_created = false;
bool g_surface_create_attempted = false;
bool g_native_window_valid = false;
uint32_t g_surface_width = 0;
uint32_t g_surface_height = 0;
bool g_provider_initialized = false;
bool g_provider_init_attempted = false;
bool g_instance_valid = false;
bool g_presenter_entered = false;
bool g_presenter_attach_attempted = false;
bool g_surface_call_completed = false;
int32_t g_surface_result = 0;
uintptr_t g_surface_handle = 0;
FiberTestResult g_fiber_result;
bool g_fiber_test_attempted = false;
AndroidPreflightResult g_preflight_result;
bool g_preflight_attempted = false;
bool g_runtime_object_constructed = false;

const char* Status(bool attempted, bool value) {
  return !attempted ? "NOT TESTED" : value ? "OK" : "FAILED";
}
const char* Boolean(bool value) { return value ? "true" : "false"; }

void RefreshReportLocked() {
  g_report = "Project 8 ReXGlue Android Presenter Probe\n\n";
  g_report += "REXUI_SURFACE_CREATE: " +
              std::string(Status(g_surface_create_attempted, g_surface_created)) + "\n";
  g_report += "REXUI_SURFACE_TYPE: ";
  g_report += g_surface_created ? "AndroidNativeWindow\n" : "NOT TESTED\n";
  g_report += "REXUI_NATIVE_WINDOW_VALID: " +
              std::string(Boolean(g_native_window_valid)) + "\n";
  if (g_surface_created) {
    g_report += "REXUI_SURFACE_PIXEL_SIZE: " + std::to_string(g_surface_width) +
                " x " + std::to_string(g_surface_height) + "\n";
  } else {
    g_report += "REXUI_SURFACE_PIXEL_SIZE: NOT TESTED\n";
  }
  g_report += "REXUI_VULKAN_PROVIDER_INIT: " +
              std::string(Status(g_provider_init_attempted, g_provider_initialized)) + "\n";
  g_report += "REXUI_VK_INSTANCE_VALID: " + std::string(Boolean(g_instance_valid)) + "\n";
  g_report += "REXUI_VULKAN_PRESENTER_ENTER: " +
              std::string(Status(g_presenter_attach_attempted, g_presenter_entered)) + "\n";
  if (g_surface_call_completed) {
    g_report += "REXUI_VK_CREATE_ANDROID_SURFACE: VkResult=" +
                std::to_string(g_surface_result) + "\n";
    g_report += "REXUI_VK_SURFACE_VALID: " +
                std::string(Boolean(g_surface_handle != 0)) + "\n";
  } else {
    g_report += "REXUI_VK_CREATE_ANDROID_SURFACE: NOT TESTED\n";
    g_report += "REXUI_VK_SURFACE_VALID: false\n";
  }
  const bool ready = g_surface_created && g_native_window_valid &&
                     g_surface_width != 0 && g_surface_height != 0 &&
                     g_provider_initialized && g_instance_valid &&
                     g_presenter_entered && g_surface_call_completed &&
                     g_surface_result == 0 && g_surface_handle != 0;
  g_report += "REXUI_5B3_READY: ";
  g_report += ready ? "OK\n" : !g_error.empty() ? "FAILED\n" : "NOT TESTED\n";
  g_report += "\nFIBER_BACKEND: ";
  g_report += g_fiber_test_attempted
                  ? Status(true, g_fiber_result.backend)
                  : "DEVICE TEST REQUIRED";
  g_report += "\nFIBER_CREATE: " +
              std::string(Status(g_fiber_test_attempted, g_fiber_result.created));
  g_report += "\nFIBER_ENTER: " +
              std::string(Status(g_fiber_test_attempted, g_fiber_result.entered));
  g_report += "\nFIBER_YIELD: " +
              std::string(Status(g_fiber_test_attempted, g_fiber_result.yielded));
  g_report += "\nFIBER_RESUME: " +
              std::string(Status(g_fiber_test_attempted, g_fiber_result.resumed));
  g_report += "\nFIBER_RETURN: " +
              std::string(Status(g_fiber_test_attempted, g_fiber_result.returned));
  g_report += "\nFIBER_MULTIPLE_SWITCHES: " +
              std::string(Status(g_fiber_test_attempted,
                                 g_fiber_result.multiple_switches));
  g_report += "\n\nREXCORE_LINK: BUILD VERIFIED";
  g_report += "\nREXRUNTIME_LINK: BUILD VERIFIED\n";
  g_report += "REXRUNTIME_OBJECT_NO_GAME: " +
              std::string(Status(g_preflight_attempted,
                                 g_runtime_object_constructed)) +
              "\nREXRUNTIME_SETUP: NOT TESTED (guest memory/fault handlers)\n";
  g_report += "\nANDROID_AARCH64: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.aarch64));
  g_report += "\nPAGE_SIZE: ";
  g_report += g_preflight_attempted
                  ? std::to_string(g_preflight_result.page_size)
                  : "DEVICE TEST REQUIRED";
  g_report += "\nMMAP_BASIC: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.mmap_basic));
  g_report += "\nMPROTECT_READ: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.mprotect_read));
  g_report += "\nMPROTECT_RX: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.mprotect_rx));
  g_report += "\nMPROTECT_RW_RESTORE: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.mprotect_rw_restore));
  g_report += "\nW_X_POLICY: RW->R->RX->RW (no RWX requested)";
  g_report += "\nSHARED_MEMORY: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.shared_memory));
  g_report += "\nTHREADS: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.threads));
  g_report += "\nMUTEX_CONDITION: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.mutex_condition));
  g_report += "\nMONOTONIC_CLOCK: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.monotonic_clock));
  g_report += "\nAPP_PRIVATE_FILESYSTEM: " +
              std::string(Status(g_preflight_attempted,
                                 g_preflight_result.private_filesystem)) +
              "\n";
  if (g_preflight_attempted && !g_preflight_result.failure_operation.empty()) {
    g_report += "PREFLIGHT_FIRST_FAILURE: " +
                g_preflight_result.failure_operation + " errno=" +
                std::to_string(g_preflight_result.failure_errno) + "\n";
  }
  if (!g_error.empty()) {
    g_report += "\nERROR: " + g_error + "\n";
  }
}

void RefreshReport() {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  RefreshReportLocked();
}

void LogLine(const char* format, ...) {
  char message[2048];
  va_list args;
  va_start(args, format);
  std::vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  SDL_Log("%s", message);
  if (g_log) {
    std::fprintf(g_log, "%s\n", message);
    std::fflush(g_log);
  }
}

void Fail(const char* message) {
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_error = message;
    RefreshReportLocked();
  }
  LogLine("REXUI_5B3_FAILED: %s", message);
}

void OpenLog() {
  char* directory = SDL_GetPrefPath("Cyberlym", "Project8VulkanProbe");
  if (!directory) {
    return;
  }
  g_log_path = std::string(directory) + "probe.log";
  g_log = std::fopen(g_log_path.c_str(), "ab");
  SDL_free(directory);
  if (g_log) {
    LogLine("REXUI_5B3_LOG_PATH=%s", g_log_path.c_str());
  }
}

void CloseLog() {
  if (g_log) {
    std::fflush(g_log);
    std::fclose(g_log);
    g_log = nullptr;
  }
}

int RunProbe() {
  rex::ui::SetAndroidPresentationDiagnosticCallback(
      rexglue_android_presentation_diagnostic);
  RefreshReport();
  OpenLog();

  const FiberTestResult fiber_result = RunFiberTest();
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_fiber_result = fiber_result;
    g_fiber_test_attempted = true;
    RefreshReportLocked();
  }
  LogLine("FIBER_BACKEND: %s", Status(true, fiber_result.backend));
  LogLine("FIBER_CREATE: %s", Status(true, fiber_result.created));
  LogLine("FIBER_ENTER: %s", Status(true, fiber_result.entered));
  LogLine("FIBER_YIELD: %s", Status(true, fiber_result.yielded));
  LogLine("FIBER_RESUME: %s", Status(true, fiber_result.resumed));
  LogLine("FIBER_RETURN: %s", Status(true, fiber_result.returned));
  LogLine("FIBER_MULTIPLE_SWITCHES: %s",
          Status(true, fiber_result.multiple_switches));

  rex::ui::SDLWindowedAppContext app_context;
  if (!app_context.Initialize()) {
    Fail("SDLWindowedAppContext::Initialize failed");
    CloseLog();
    return 1;
  }

  const AndroidPreflightResult preflight_result = RunAndroidPreflight();
  bool runtime_object_constructed = false;
  {
    rex::Runtime runtime(std::filesystem::path{});
    runtime_object_constructed = runtime.memory() == nullptr &&
                                 rex::Runtime::instance() == nullptr;
  }
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_preflight_result = preflight_result;
    g_preflight_attempted = true;
    g_runtime_object_constructed = runtime_object_constructed;
    RefreshReportLocked();
  }

  auto window = rex::ui::Window::Create(
      app_context, "Project 8 ReXGlue Presenter Probe", 640, 360);
  if (!window || !window->Open() ||
      window->phase() != rex::ui::Window::Phase::kOpen) {
    Fail("WindowSDL did not open a real SDL_Window");
    CloseLog();
    return 1;
  }

  auto provider = rex::ui::vulkan::VulkanProvider::Create(false, true);
  std::unique_ptr<rex::ui::Presenter> presenter;
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_provider_init_attempted = true;
    g_provider_initialized = provider != nullptr;
    if (provider) {
      g_instance_valid = provider->vulkan_instance() &&
                         provider->vulkan_instance()->instance() != VK_NULL_HANDLE;
    }
    RefreshReportLocked();
  }
  if (!provider) {
    Fail("VulkanProvider::Create returned null");
  } else {
    LogLine("REXUI_VULKAN_PROVIDER_INIT: OK");
    LogLine("REXUI_VK_INSTANCE_VALID: %s", Boolean(g_instance_valid));
    if (!g_instance_valid) {
      Fail("VulkanProvider has no valid VkInstance");
    } else {
      presenter = provider->CreatePresenter();
      if (!presenter) {
        Fail("VulkanProvider::CreatePresenter returned null");
      } else {
        {
          std::lock_guard<std::mutex> lock(g_state_mutex);
          g_presenter_attach_attempted = true;
          RefreshReportLocked();
        }
        window->SetPresenter(presenter.get());
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (!(g_surface_created && g_native_window_valid && g_presenter_entered &&
              g_surface_call_completed && g_surface_result == VK_SUCCESS &&
              g_surface_handle != 0)) {
          g_error = "real VulkanPresenter Android surface path did not complete";
        }
        RefreshReportLocked();
      }
    }
  }

  const int loop_result = app_context.RunMainMessageLoop();
  window->SetPresenter(nullptr);
  window.reset();
  presenter.reset();
  provider.reset();
  CloseLog();
  return loop_result;
}

}  // namespace

extern "C" void rexglue_android_presentation_diagnostic(
    rex::ui::AndroidPresentationDiagnosticEvent event, int32_t result, uintptr_t handle,
    uint32_t width, uint32_t height) {
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    switch (event) {
      case rex::ui::AndroidPresentationDiagnosticEvent::kSurfaceCreated:
        g_surface_create_attempted = true;
        g_surface_created = result == 0 && handle != 0;
        g_native_window_valid = handle != 0;
        g_surface_width = width;
        g_surface_height = height;
        break;
      case rex::ui::AndroidPresentationDiagnosticEvent::kVulkanPresenterEnter:
        g_presenter_entered = true;
        break;
      case rex::ui::AndroidPresentationDiagnosticEvent::kVulkanSurfaceCreated:
        g_surface_call_completed = true;
        g_surface_result = result;
        g_surface_handle = handle;
        break;
    }
    RefreshReportLocked();
  }
  switch (event) {
    case rex::ui::AndroidPresentationDiagnosticEvent::kSurfaceCreated:
      LogLine("REXUI_SURFACE_CREATE: %s", result == 0 && handle != 0 ? "OK" : "FAILED");
      LogLine("REXUI_SURFACE_TYPE: AndroidNativeWindow");
      LogLine("REXUI_NATIVE_WINDOW_VALID: %s", Boolean(handle != 0));
      LogLine("REXUI_SURFACE_PIXEL_SIZE: %u x %u", width, height);
      break;
    case rex::ui::AndroidPresentationDiagnosticEvent::kVulkanPresenterEnter:
      LogLine("REXUI_VULKAN_PRESENTER_ENTER: OK");
      break;
    case rex::ui::AndroidPresentationDiagnosticEvent::kVulkanSurfaceCreated:
      LogLine("REXUI_VK_CREATE_ANDROID_SURFACE: VkResult=%d", result);
      LogLine("REXUI_VK_SURFACE_VALID: %s handle=%p", Boolean(handle != 0),
              reinterpret_cast<void*>(handle));
      break;
  }
}

int main(int, char**) { return RunProbe(); }

extern "C" JNIEXPORT jstring JNICALL
Java_com_cyberlym_project8probe_ProbeActivity_nativeGetReport(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  return env->NewStringUTF(g_report.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_cyberlym_project8probe_ProbeActivity_nativeGetLog(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  FILE* file = g_log_path.empty() ? nullptr : std::fopen(g_log_path.c_str(), "rb");
  if (!file) {
    return env->NewStringUTF(g_report.c_str());
  }
  std::fseek(file, 0, SEEK_END);
  long size = std::ftell(file);
  std::rewind(file);
  std::string contents(size > 0 ? static_cast<size_t>(size) : 0, '\0');
  if (size > 0) {
    std::fread(contents.data(), 1, contents.size(), file);
  }
  std::fclose(file);
  return env->NewStringUTF(contents.c_str());
}

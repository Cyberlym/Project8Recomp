#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <jni.h>

#include <rex/ui/surface_android.h>

#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

namespace {

FILE* g_log = nullptr;
std::string g_log_path;
std::string g_last_checkpoint = "NONE";
std::mutex g_state_mutex;
std::string g_report = "Project 8 Android Vulkan Probe\n";
std::string g_error;
std::string g_gpu;
std::string g_surface = "WAITING";
std::string g_vulkan_evidence;
std::string g_rexui_evidence =
    "SDL_WINDOW_REAL: WAITING\nANDROID_NATIVE_WINDOW: WAITING\n"
    "SURFACE_PIXEL_SIZE: NOT TESTED\n";
std::vector<std::string> g_ok_checkpoints;

void RefreshReport() {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  std::string report = "Project 8 Android Vulkan Probe\n\n";
  const char* checkpoints[] = {"ANDROID_ENTRY", "SDL_INIT_OK", "SDL_WINDOW_OK",
                               "VULKAN_INSTANCE_OK", "ANDROID_SURFACE_OK",
                               "GPU_ENUM_OK", "PROBE_READY"};
  for (const char* checkpoint : checkpoints) {
    report += std::string(checkpoint) + ": " +
              (std::find(g_ok_checkpoints.begin(), g_ok_checkpoints.end(), checkpoint) != g_ok_checkpoints.end() ? "OK\n" : "WAITING\n");
  }
  report += "\n" + g_rexui_evidence + "\n" + g_vulkan_evidence + "\n" +
            g_gpu + "\nSURFACE: " + g_surface;
  if (!g_error.empty()) report += "\n\n" + g_error;
  g_report = report;
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
  { std::lock_guard<std::mutex> lock(g_state_mutex); g_report += message; g_report += "\n"; }
  RefreshReport();
}

void OpenLog() {
  char* directory = SDL_GetPrefPath("Cyberlym", "Project8VulkanProbe");
  if (!directory) {
    SDL_Log("probe.log unavailable: %s", SDL_GetError());
    return;
  }
  g_log_path = std::string(directory) + "probe.log";
  g_log = std::fopen(g_log_path.c_str(), "ab");
  SDL_free(directory);
  if (!g_log) {
    SDL_Log("probe.log unavailable at %s", g_log_path.c_str());
    return;
  }
  LogLine("LOG_PATH=%s", g_log_path.c_str());
}

void CloseLog() {
  if (g_log) {
    std::fflush(g_log);
    std::fclose(g_log);
    g_log = nullptr;
  }
}

void Checkpoint(const char* name) {
  g_last_checkpoint = name;
  { std::lock_guard<std::mutex> lock(g_state_mutex); g_ok_checkpoints.emplace_back(name); }
  LogLine("%s", name);
}

void LogVulkanError(const char* operation, VkResult result) {
  LogLine("ERROR last_checkpoint=%s %s failed: VkResult=%d", g_last_checkpoint.c_str(),
          operation, static_cast<int>(result));
}

void LogError(const char* message) {
  { std::lock_guard<std::mutex> lock(g_state_mutex); g_error = std::string("FAILED AT: ") + g_last_checkpoint + "\nERROR: " + message; }
  LogLine("ERROR last_checkpoint=%s %s", g_last_checkpoint.c_str(), message);
}

bool HasExtension(const std::vector<VkExtensionProperties>& extensions,
                  const char* name) {
  for (const auto& extension : extensions) {
    if (std::strcmp(extension.extensionName, name) == 0) return true;
  }
  return false;
}

bool HasName(const std::vector<const char*>& names, const char* wanted) {
  for (const char* name : names) {
    if (std::strcmp(name, wanted) == 0) return true;
  }
  return false;
}

void DestroySurface(VkInstance instance, VkSurfaceKHR* surface) {
  if (*surface != VK_NULL_HANDLE) {
    SDL_Vulkan_DestroySurface(instance, *surface, nullptr);
    *surface = VK_NULL_HANDLE;
    { std::lock_guard<std::mutex> lock(g_state_mutex); g_surface = "DESTROYED"; }
    RefreshReport();
  }
}

bool CreateSurface(SDL_Window* window, VkInstance instance, VkSurfaceKHR* surface) {
  if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, surface)) {
    LogLine("ERROR last_checkpoint=%s SDL_Vulkan_CreateSurface failed: %s",
            g_last_checkpoint.c_str(), SDL_GetError());
    return false;
  }
  if (*surface == VK_NULL_HANDLE) {
    LogError("SDL_Vulkan_CreateSurface returned success with a null VkSurfaceKHR");
    return false;
  }
  LogLine("ANDROID_SURFACE_CREATE SDL_result=success surface_valid=true");
  { std::lock_guard<std::mutex> lock(g_state_mutex);
    g_vulkan_evidence += "ANDROID_SURFACE_CREATE: SDL success, VkSurfaceKHR valid\n"; }
  Checkpoint("ANDROID_SURFACE_OK");
  { std::lock_guard<std::mutex> lock(g_state_mutex); g_surface = "OK"; }
  RefreshReport();
  return true;
}

bool EnumerateGpus(VkInstance instance) {
  uint32_t count = 0;
  VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
  LogLine("GPU_ENUM_FIRST VkResult=%d deviceCount=%u", static_cast<int>(result), count);
  { std::lock_guard<std::mutex> lock(g_state_mutex);
    g_vulkan_evidence += "GPU_ENUM_FIRST: VkResult=" + std::to_string(static_cast<int>(result)) +
                         " deviceCount=" + std::to_string(count) + "\n"; }
  if (result != VK_SUCCESS || count == 0) {
    LogVulkanError("vkEnumeratePhysicalDevices", result);
    return false;
  }

  std::vector<VkPhysicalDevice> devices(count);
  result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
  LogLine("GPU_ENUM_SECOND VkResult=%d deviceCount=%u", static_cast<int>(result), count);
  { std::lock_guard<std::mutex> lock(g_state_mutex);
    g_vulkan_evidence += "GPU_ENUM_SECOND: VkResult=" + std::to_string(static_cast<int>(result)) +
                         " deviceCount=" + std::to_string(count) + "\n"; }
  if (result != VK_SUCCESS || count == 0 || count > devices.size() ||
      std::any_of(devices.begin(), devices.begin() + count,
                  [](VkPhysicalDevice device) { return device == VK_NULL_HANDLE; })) {
    LogVulkanError("vkEnumeratePhysicalDevices", result);
    return false;
  }

  for (VkPhysicalDevice device : devices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                         extensions.data());
    LogLine("GPU name=%s vendor_id=%u (0x%04x) device_id=%u (0x%04x) "
            "api_original=%u (0x%08x) api_decoded=%u.%u.%u "
            "driver_original=%u (0x%08x) driver_decoded=%u.%u.%u type=%d extensions=%u",
            properties.deviceName, properties.vendorID, properties.vendorID,
            properties.deviceID, properties.deviceID,
            properties.apiVersion, properties.apiVersion,
            VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
            VK_VERSION_PATCH(properties.apiVersion),
            properties.driverVersion, properties.driverVersion,
            VK_VERSION_MAJOR(properties.driverVersion),
            VK_VERSION_MINOR(properties.driverVersion),
            VK_VERSION_PATCH(properties.driverVersion), properties.deviceType,
            extension_count);
    { std::lock_guard<std::mutex> lock(g_state_mutex);
      g_gpu = std::string("GPU: ") + properties.deviceName + "\nVENDOR ID: " +
              std::to_string(properties.vendorID) + " (0x" +
              [&] { char value[16]; std::snprintf(value, sizeof(value), "%04x", properties.vendorID); return std::string(value); }() +
              ")\nDEVICE ID: " + std::to_string(properties.deviceID) + " (0x" +
              [&] { char value[16]; std::snprintf(value, sizeof(value), "%04x", properties.deviceID); return std::string(value); }() +
              ")\nVULKAN API: " +
              std::to_string(VK_VERSION_MAJOR(properties.apiVersion)) + "." +
              std::to_string(VK_VERSION_MINOR(properties.apiVersion)) + "." +
              std::to_string(VK_VERSION_PATCH(properties.apiVersion)) + "\nDRIVER: " +
              std::to_string(VK_VERSION_MAJOR(properties.driverVersion)) + "." +
              std::to_string(VK_VERSION_MINOR(properties.driverVersion)) + "." +
              std::to_string(VK_VERSION_PATCH(properties.driverVersion)); }
    RefreshReport();
    for (const auto& extension : extensions) {
      LogLine("GPU_EXTENSION %s", extension.extensionName);
    }
  }
  Checkpoint("GPU_ENUM_OK");
  return true;
}

int RunProbe() {
  OpenLog();
  Checkpoint("ANDROID_ENTRY");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    LogLine("ERROR last_checkpoint=%s SDL_Init failed: %s", g_last_checkpoint.c_str(),
            SDL_GetError());
    CloseLog();
    return 1;
  }
  LogLine("SDL_INIT result=success");
  Checkpoint("SDL_INIT_OK");

  SDL_Window* window = SDL_CreateWindow("Project8 Android Vulkan Probe", 640, 360,
                                        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    LogLine("ERROR last_checkpoint=%s SDL_CreateWindow failed: %s",
            g_last_checkpoint.c_str(), SDL_GetError());
    SDL_Quit();
    CloseLog();
    return 1;
  }
  LogLine("SDL_WINDOW result=success window_valid=true");
  Checkpoint("SDL_WINDOW_OK");

  auto rexui_surface = rex::ui::AndroidNativeWindowSurface::Create(window);
  if (!rexui_surface) {
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_rexui_evidence =
          "SDL_WINDOW_REAL: OK\nANDROID_NATIVE_WINDOW: FAILED\n"
          "SURFACE_PIXEL_SIZE: NOT TESTED\n";
    }
    LogError("rexui could not obtain ANativeWindow from SDL_Window properties");
    SDL_DestroyWindow(window);
    SDL_Quit();
    CloseLog();
    return 1;
  }
  uint32_t surface_width = 0;
  uint32_t surface_height = 0;
  if (!rexui_surface->GetSize(surface_width, surface_height)) {
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_rexui_evidence =
          "SDL_WINDOW_REAL: OK\nANDROID_NATIVE_WINDOW: OK\n"
          "SURFACE_PIXEL_SIZE: FAILED\n";
    }
    LogError("rexui Android surface did not return a valid physical size");
    SDL_DestroyWindow(window);
    SDL_Quit();
    CloseLog();
    return 1;
  }
  {
    char native_window_pointer[32];
    std::snprintf(native_window_pointer, sizeof(native_window_pointer), "%p",
                  static_cast<void*>(rexui_surface->window()));
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_rexui_evidence =
        "SDL_WINDOW_REAL: OK\nANDROID_NATIVE_WINDOW: OK (pointer=" +
        std::string(native_window_pointer) + ")\n"
        "SURFACE_PIXEL_SIZE: " + std::to_string(surface_width) + " x " +
        std::to_string(surface_height) + "\n";
  }
  LogLine("REXUI_ANDROID_NATIVE_WINDOW pointer=%p pixel_width=%u pixel_height=%u",
          static_cast<void*>(rexui_surface->window()), surface_width,
          surface_height);

  Uint32 extension_count = 0;
  const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
  if (!sdl_extensions || extension_count == 0) {
    LogLine("ERROR last_checkpoint=%s SDL_Vulkan_GetInstanceExtensions failed: %s",
            g_last_checkpoint.c_str(), SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    CloseLog();
    return 1;
  }
  std::vector<const char*> extensions(sdl_extensions, sdl_extensions + extension_count);
  if (!HasName(extensions, VK_KHR_SURFACE_EXTENSION_NAME) ||
      !HasName(extensions, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
    LogError("SDL did not provide the required Android Vulkan extensions");
    SDL_DestroyWindow(window);
    SDL_Quit();
    CloseLog();
    return 1;
  }

  uint32_t available_count = 0;
  VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &available_count, nullptr);
  if (result != VK_SUCCESS) {
    LogVulkanError("vkEnumerateInstanceExtensionProperties", result);
    SDL_DestroyWindow(window);
    SDL_Quit();
    CloseLog();
    return 1;
  }
  std::vector<VkExtensionProperties> available(available_count);
  vkEnumerateInstanceExtensionProperties(nullptr, &available_count, available.data());
  if (!HasExtension(available, VK_KHR_SURFACE_EXTENSION_NAME) ||
      !HasExtension(available, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
    LogError("Required Vulkan surface extensions are unavailable");
    SDL_DestroyWindow(window);
    SDL_Quit();
    CloseLog();
    return 1;
  }
  VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app_info.pApplicationName = "Project8 Android Vulkan Probe";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "Project8 Probe";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instance_info.pApplicationInfo = &app_info;
  instance_info.enabledExtensionCount = extension_count;
  instance_info.ppEnabledExtensionNames = extensions.data();
  VkInstance instance = VK_NULL_HANDLE;
  result = vkCreateInstance(&instance_info, nullptr, &instance);
  LogLine("VK_INSTANCE_CREATE VkResult=%d instance_valid=%s", static_cast<int>(result),
          instance != VK_NULL_HANDLE ? "true" : "false");
  { std::lock_guard<std::mutex> lock(g_state_mutex);
    g_vulkan_evidence = "VK_INSTANCE_CREATE: VkResult=" + std::to_string(static_cast<int>(result)) +
                        " instance_valid=" + (instance != VK_NULL_HANDLE ? "true\n" : "false\n"); }
  if (result != VK_SUCCESS) {
    LogVulkanError("vkCreateInstance", result);
    SDL_DestroyWindow(window);
    SDL_Quit();
    CloseLog();
    return 1;
  }
  if (instance == VK_NULL_HANDLE) {
    LogError("vkCreateInstance returned VK_SUCCESS with a null VkInstance");
    SDL_DestroyWindow(window);
    SDL_Quit();
    CloseLog();
    return 1;
  }
  Checkpoint("VULKAN_INSTANCE_OK");

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!CreateSurface(window, instance, &surface) || !EnumerateGpus(instance)) {
    DestroySurface(instance, &surface);
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  Checkpoint("PROBE_READY");

  bool running = true;
  bool surface_available = true;
  while (running) {
    SDL_Event event{};
    if (!SDL_WaitEvent(&event)) break;
    switch (event.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        running = false;
        break;
      case SDL_EVENT_WINDOW_MINIMIZED:
      case SDL_EVENT_WINDOW_HIDDEN:
        if (surface_available) {
          DestroySurface(instance, &surface);
          surface_available = false;
          SDL_Log("ANDROID_SURFACE_DESTROYED lifecycle=minimized_or_hidden");
        }
        break;
      case SDL_EVENT_WINDOW_RESTORED:
      case SDL_EVENT_WINDOW_SHOWN:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (!surface_available) {
          surface_available = CreateSurface(window, instance, &surface);
          SDL_Log("ANDROID_SURFACE_RECREATED result=%s",
                  surface_available ? "ok" : "failed");
        }
        SDL_Log("WINDOW_EVENT type=%u", event.type);
        break;
      default:
        break;
    }
  }

  DestroySurface(instance, &surface);
  vkDestroyInstance(instance, nullptr);
  SDL_DestroyWindow(window);
  SDL_Quit();
  CloseLog();
  return 0;
}

}  // namespace

int main(int, char**) {
  return RunProbe();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_cyberlym_project8probe_ProbeActivity_nativeGetReport(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  return env->NewStringUTF(g_report.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_cyberlym_project8probe_ProbeActivity_nativeGetLog(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  FILE* file = g_log_path.empty() ? nullptr : std::fopen(g_log_path.c_str(), "rb");
  if (!file) return env->NewStringUTF(g_report.c_str());
  std::fseek(file, 0, SEEK_END); long size = std::ftell(file); std::rewind(file);
  std::string contents(size > 0 ? static_cast<size_t>(size) : 0, '\0');
  if (size > 0) std::fread(contents.data(), 1, contents.size(), file);
  std::fclose(file);
  return env->NewStringUTF(contents.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_cyberlym_project8probe_ProbeActivity_nativeGetLogPath(JNIEnv* env, jobject) {
  return env->NewStringUTF(g_log_path.c_str());
}

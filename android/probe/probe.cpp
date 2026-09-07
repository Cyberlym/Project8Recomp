#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <jni.h>

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
  report += "\n" + g_gpu + "\nSURFACE: " + g_surface;
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
  Checkpoint("ANDROID_SURFACE_OK");
  { std::lock_guard<std::mutex> lock(g_state_mutex); g_surface = "OK"; }
  RefreshReport();
  return true;
}

bool EnumerateGpus(VkInstance instance) {
  uint32_t count = 0;
  VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
  if (result != VK_SUCCESS || count == 0) {
    LogVulkanError("vkEnumeratePhysicalDevices", result);
    return false;
  }

  std::vector<VkPhysicalDevice> devices(count);
  result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
  if (result != VK_SUCCESS) {
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
    LogLine("GPU name=%s vendor_id=0x%04x device_id=0x%04x api=%u.%u.%u "
            "driver=%u.%u.%u type=%d extensions=%u",
            properties.deviceName, properties.vendorID, properties.deviceID,
            VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
            VK_VERSION_PATCH(properties.apiVersion),
            VK_VERSION_MAJOR(properties.driverVersion),
            VK_VERSION_MINOR(properties.driverVersion),
            VK_VERSION_PATCH(properties.driverVersion), properties.deviceType,
            extension_count);
    { std::lock_guard<std::mutex> lock(g_state_mutex);
      g_gpu = std::string("GPU: ") + properties.deviceName + "\nVENDOR ID: " +
              std::to_string(properties.vendorID) + "\nDEVICE ID: " +
              std::to_string(properties.deviceID) + "\nVULKAN API: " +
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
  Checkpoint("SDL_WINDOW_OK");

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
  if (result != VK_SUCCESS) {
    LogVulkanError("vkCreateInstance", result);
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

#include "android_game_session.h"
#include "android_gameplay_log.h"

#include <atomic>
#include <cstdint>
#include <jni.h>
#include <memory>

#include <rex/ppc/context.h>
#include <rex/graphics/flags.h>
#include <rex/system/function_dispatcher.h>
#include <rex/ui/android_performance.h>
#include <rex/ui/windowed_app.h>

#include "thps_p8_init.h"
#include "thps_p8_app.h"
#include "guest_vertex_unpack.h"
#include "project8_native_scene.h"

namespace thps::android {
namespace {

constexpr uint32_t kFaultingGuestFunction = 0x82468980;

// This dispatch target is reached only indirectly, so the generated sources
// cannot identify the runtime owner that supplied its `this` pointer. Keep the
// wrapper Android-only and observational: the first null call records the PPC
// return address before passing the untouched context to the original target.
PPCFunc* g_faulting_function_original = nullptr;
std::atomic_bool g_faulting_function_logged = false;

void CaptureFaultingFunctionEntry(PPCContext& ctx, uint8_t* base) {
  if (ctx.r3.u32 == 0 &&
      !g_faulting_function_logged.exchange(true, std::memory_order_relaxed)) {
    uintptr_t host_caller = 0;
#if defined(__aarch64__)
    host_caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
#endif
    LogGameplayEvent(
        "P8_82468980_NULL_THIS guest_function=%08X r3=%08X guest_lr=%08X "
        "host_caller=%016llX original=%p",
        kFaultingGuestFunction, ctx.r3.u32, static_cast<uint32_t>(ctx.lr),
        static_cast<unsigned long long>(host_caller),
        reinterpret_cast<void*>(g_faulting_function_original));
  }
  g_faulting_function_original(ctx, base);
}

void InstallFaultingFunctionCapture(rex::Runtime* runtime) {
  if (!runtime) return;
  auto* dispatcher = runtime->function_dispatcher();
  if (!dispatcher) return;
  PPCFunc* original = dispatcher->GetFunction(kFaultingGuestFunction);
  if (!original || !dispatcher->SetFunction(kFaultingGuestFunction,
                                            &CaptureFaultingFunctionEntry)) {
    LogGameplayEvent("P8_82468980_CAPTURE_INSTALL_FAILED");
    return;
  }
  g_faulting_function_original = original;
  LogGameplayEvent("P8_82468980_CAPTURE_READY original=%p",
                   reinterpret_cast<void*>(original));
}

}  // namespace

class AndroidThpsP8App final : public ThpsP8App {
 public:
  using ThpsP8App::ThpsP8App;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& context) {
    return std::unique_ptr<AndroidThpsP8App>(
        new AndroidThpsP8App(context, "thps_p8", PPCImageConfig));
  }

 protected:
  void OnPreSetup(rex::RuntimeConfig& config) override {
    LogGameplayEvent("P8_RUNTIME_SETUP_BEGIN");
    config.setup_trace = [](std::string_view step) { LogGameplayEvent("%.*s",
        static_cast<int>(step.size()), step.data()); };
    ThpsP8App::OnPreSetup(config);
    // The desktop launcher supplies --gpu_plugin=xenos. Android enters through
    // SDL library mode instead, so choose the same required runtime plugin here
    // rather than silently accepting the headless default.
    config.gpu_plugin = "xenos";
    // GPU CVars are registered by the runtime-loaded rexgpu-xenos plugin, so
    // Android policy lives in that plugin (patch 0030), not before it loads.
    // No game_device_factory: Runtime's normal HostPathDevice mounts the
    // completed app-private installation. AndroidXdvdfsDevice stays built for
    // rollback/diagnostics but is deliberately outside the PLAY path.
  }

  void OnLoadXexImage(std::string& xex_image) override {
    LogGameplayEvent("P8_MODULE_LOAD image=%s", xex_image.c_str());
  }

  void OnPostSetup() override {
    InstallFaultingFunctionCapture(runtime());
    LogGameplayEvent("P8_RUNTIME_SETUP_READY");
    LogGameplayEvent("P8_ENTRYPOINT_READY");
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    paths.game_data_root = GameplayGameRoot();
    ThpsP8App::OnConfigurePaths(paths);
  }
};

}  // namespace thps::android

REX_DEFINE_APP(thps_p8, thps::android::AndroidThpsP8App::Create)

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_cyberlym_project8probe_GameplayActivity_nativeGetPresentationPerformance(
    JNIEnv* env, jclass) {
  constexpr jsize kSnapshotValueCount = 21;
  const auto snapshot = rex::ui::AndroidPerformanceGetSnapshot();
  const auto native_scene = thps::native_scene::GetSnapshot();
  const jlong values[] = {
      static_cast<jlong>(snapshot.successful_present_count),
      static_cast<jlong>(snapshot.last_present_monotonic_ns),
      static_cast<jlong>(snapshot.last_present_interval_ns),
      static_cast<jlong>(snapshot.snapshot_monotonic_ns),
      static_cast<jlong>(snapshot.skipped_placeholder_present_count),
      static_cast<jlong>(snapshot.graphics_pipeline_create_count),
      static_cast<jlong>(snapshot.shader_translate_count),
      static_cast<jlong>(snapshot.pipeline_description_cache_hit_count),
      static_cast<jlong>(snapshot.pipeline_description_cache_miss_count),
      static_cast<jlong>(snapshot.swapchain_recreation_count),
      static_cast<jlong>(snapshot.guest_frame_count),
      static_cast<jlong>(snapshot.last_guest_frame_monotonic_ns),
      static_cast<jlong>(snapshot.last_guest_frame_interval_ns),
      static_cast<jlong>(snapshot.guest_frame_window_count),
      static_cast<jlong>(snapshot.guest_frame_window_elapsed_ns),
      static_cast<jlong>(snapshot.xenos_draw_count),
      static_cast<jlong>(snapshot.queue_submit_count),
      static_cast<jlong>(snapshot.occlusion_query_count),
      static_cast<jlong>(snapshot.occlusion_query_wait_count),
      static_cast<jlong>(native_scene.native_cpu_preparations),
      static_cast<jlong>(native_scene.native_cpu_preparation_ns),
  };
  jlongArray array = env->NewLongArray(kSnapshotValueCount);
  if (array) {
    env->SetLongArrayRegion(array, 0, kSnapshotValueCount, values);
  }
  return array;
}

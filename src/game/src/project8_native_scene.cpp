#include "project8_native_scene.h"

#include <rex/cvar.h>
#include <rex/platform.h>

namespace thps::native_scene {
namespace {

#if REX_PLATFORM_ANDROID
REXCVAR_DEFINE_BOOL(project8_native_renderer_enable, false, "Project8/NativeRenderer",
                    "Enable Project 8 Android native CPU preparation hooks. "
                    "Xenos rendering remains the fallback.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
#endif

std::atomic<uint64_t> native_cpu_preparations{0};
std::atomic<uint64_t> native_cpu_preparation_ns{0};
std::atomic<uint32_t> last_source_address{0};
std::atomic<uint32_t> last_destination_address{0};
std::atomic<uint32_t> last_source_bytes{0};
std::atomic<uint32_t> last_destination_bytes{0};

}  // namespace

bool NativeCpuPreparationEnabled() {
#if REX_PLATFORM_ANDROID
  return REXCVAR_GET(project8_native_renderer_enable);
#else
  return true;
#endif
}

void RecordVertexPreparation(const VertexPreparationRecord& record, uint64_t elapsed_ns) {
  native_cpu_preparations.fetch_add(1, std::memory_order_relaxed);
  native_cpu_preparation_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);
  last_source_address.store(record.source_address, std::memory_order_relaxed);
  last_destination_address.store(record.destination_address, std::memory_order_relaxed);
  last_source_bytes.store(record.source_bytes, std::memory_order_relaxed);
  last_destination_bytes.store(record.destination_bytes, std::memory_order_relaxed);
}

Snapshot GetSnapshot() {
  return {
      native_cpu_preparations.load(std::memory_order_relaxed),
      native_cpu_preparation_ns.load(std::memory_order_relaxed),
      {last_source_address.load(std::memory_order_relaxed),
       last_destination_address.load(std::memory_order_relaxed),
       last_source_bytes.load(std::memory_order_relaxed),
       last_destination_bytes.load(std::memory_order_relaxed)},
  };
}

}  // namespace thps::native_scene

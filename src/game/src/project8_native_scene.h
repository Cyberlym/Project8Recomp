// Project 8 Android native-renderer foundation.
//
// This intentionally captures only data whose layout is proven by the guest
// hook. A world draw must not be inferred from a vertex-format transform.
#pragma once

#include <atomic>
#include <cstdint>

namespace thps::native_scene {

struct VertexPreparationRecord {
  uint32_t source_address = 0;
  uint32_t destination_address = 0;
  uint32_t source_bytes = 0;
  uint32_t destination_bytes = 0;
};

struct Snapshot {
  uint64_t native_cpu_preparations = 0;
  uint64_t native_cpu_preparation_ns = 0;
  VertexPreparationRecord last_vertex_preparation{};
};

bool NativeCpuPreparationEnabled();
void RecordVertexPreparation(const VertexPreparationRecord& record, uint64_t elapsed_ns);
Snapshot GetSnapshot();

}  // namespace thps::native_scene

#include <rex/thread/fiber.h>

#include <cstdint>

#include "fiber_test.h"

namespace {

constexpr int kSwitchCount = 64;
constexpr uint64_t kSeed = 0x8A5CD789635D2DFFull;

struct FiberTestState {
  rex::thread::Fiber* main_fiber = nullptr;
  uint64_t observed = 0;
  int yields = 0;
  int resume_command = 0;
  bool entered = false;
  bool completed = false;
  bool failed = false;
};

uint64_t NextValue(uint64_t value, int iteration) {
  return (value ^ (0x9E3779B97F4A7C15ull + static_cast<uint64_t>(iteration))) *
         0xD6E8FEB86659FD93ull;
}

void FiberEntry(void* raw_state) {
  auto* state = static_cast<FiberTestState*>(raw_state);
  state->entered = true;

  uint64_t local_value = kSeed;
  for (int iteration = 0; iteration < kSwitchCount; ++iteration) {
    if (iteration != 0 && state->resume_command != iteration) {
      state->failed = true;
    }
    local_value = NextValue(local_value, iteration);
    state->observed = local_value;
    state->yields = iteration + 1;
    rex::thread::Fiber::SwitchTo(state->main_fiber);
  }

  // ReXGlue's runtime uses this same explicit final handoff if a guest fiber
  // entrypoint returns. uc_link is null, so the host entry must not return.
  state->completed = true;
  rex::thread::Fiber::SwitchTo(state->main_fiber);
  state->failed = true;
}

}  // namespace

FiberTestResult RunFiberTest() {
  FiberTestResult result;
  FiberTestState state;
  state.main_fiber = rex::thread::Fiber::ConvertCurrentThread();
  result.backend = state.main_fiber != nullptr;
  auto* test_fiber =
      rex::thread::Fiber::Create(256 * 1024, FiberEntry, &state);

  result.created = state.main_fiber != nullptr && test_fiber != nullptr;
  if (!result.created) {
    if (test_fiber) {
      test_fiber->Destroy();
    }
    if (state.main_fiber) {
      state.main_fiber->Destroy();
    }
    return result;
  }

  uint64_t expected = kSeed;
  bool data_valid = true;
  for (int iteration = 0; iteration < kSwitchCount; ++iteration) {
    rex::thread::Fiber::SwitchTo(test_fiber);
    expected = NextValue(expected, iteration);
    data_valid = data_valid && state.observed == expected &&
                 state.yields == iteration + 1 && !state.failed;
    state.resume_command = iteration + 1;
  }

  result.entered = state.entered;
  result.yielded = state.yields >= 1 && data_valid;
  result.resumed = state.yields >= 2 && data_valid;
  result.multiple_switches = state.yields == kSwitchCount && data_valid;

  rex::thread::Fiber::SwitchTo(test_fiber);
  result.returned = state.completed && !state.failed;

  test_fiber->Destroy();
  state.main_fiber->Destroy();

  return result;
}

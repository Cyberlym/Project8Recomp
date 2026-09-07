#include "fiber_test.h"

#include <cstdio>

namespace {

void PrintCheckpoint(const char* name, bool passed) {
  std::printf("%s: %s\n", name, passed ? "OK" : "FAILED");
}

}  // namespace

int main() {
  const FiberTestResult result = RunFiberTest();
  PrintCheckpoint("FIBER_BACKEND", result.backend);
  PrintCheckpoint("FIBER_CREATE", result.created);
  PrintCheckpoint("FIBER_ENTER", result.entered);
  PrintCheckpoint("FIBER_YIELD", result.yielded);
  PrintCheckpoint("FIBER_RESUME", result.resumed);
  PrintCheckpoint("FIBER_RETURN", result.returned);
  PrintCheckpoint("FIBER_MULTIPLE_SWITCHES", result.multiple_switches);
  return result.created && result.entered && result.yielded && result.resumed &&
                 result.returned && result.multiple_switches
             ? 0
             : 1;
}
